#include "tts.h"
#include "dev_server.h"
#include "log.h"
#include "vocab.h"

#include <prism.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace TTS {

namespace {

// Prism's context owns the registry and the availability poll thread; the backend is ONE live
// screen reader or speech engine. Prism guarantees the registry is thread-safe but a BACKEND
// INSTANCE explicitly is not — X2Access speaks from the init thread (the load greeting) as well
// as the game thread (every arbiter/tick handler), so sMutex below is what serializes them. It
// is the same mutex SRAL was called under; nothing about the call sites changed.
PrismContext* sCtx = nullptr;
PrismBackend* sBackend = nullptr;

bool sAvailable = false;
bool sEnabled = true;
std::mutex sMutex;
std::vector<std::string> sQueue;

// Set from Prism's poll thread when a backend appears or disappears, consumed under sMutex on
// whichever thread speaks next. The callback must not touch sBackend itself — see above — so it
// raises this and lets the next Speak() do the work.
std::atomic<bool> sRebindRequested{false};

void PRISM_CALL OnPrismLog(void*, PrismLogLevel level, const char* source, const char* message) {
    // Prism's own logging drain thread. Log::Write is mutex-guarded, and the "handler must not
    // call any logging function" rule in Prism's manual is about PRISM's logging entry points,
    // which we never touch from here.
    const char* src = source ? source : "prism";
    const char* msg = message ? message : "";
    switch (level) {
        case PRISM_LOG_LEVEL_ERROR: LOG_ERROR("[%s] %s", src, msg); break;
        case PRISM_LOG_LEVEL_WARN:  LOG_WARN("[%s] %s", src, msg);  break;
        case PRISM_LOG_LEVEL_INFO:  LOG_INFO("[%s] %s", src, msg);  break;
        default:                    LOG_DEBUG("[%s] %s", src, msg); break;
    }
}

void PRISM_CALL OnBackendAvailability(void*, PrismBackendId, const char* name, bool available) {
    // Poll thread. Do the minimum and return — Prism cannot scan again until this returns, and
    // `name` dies with the call.
    LOG_INFO("Speech backend '%s' became %s", name ? name : "?",
             available ? "available" : "unavailable");
    sRebindRequested.store(true, std::memory_order_relaxed);
}

// Drop whatever backend we hold and take the highest-priority one that initializes now. This is
// how a screen reader that started, stopped, or restarted mid-session gets picked up: SRAL
// re-selected on EVERY call (a process-table walk per utterance); Prism moves that probe onto its
// own debounced thread and leaves the re-bind to us.
//
// Caller must hold sMutex. Returns true when a usable backend is held afterwards.
bool RebindBackend() {
    std::string previous = sBackend ? prism_backend_name(sBackend) : std::string{};

    if (sBackend) {
        (void)prism_backend_stop(sBackend);
        prism_backend_free(sBackend);
        sBackend = nullptr;
    }

    sBackend = prism_registry_create_best(sCtx);
    if (!sBackend) {
        LOG_WARN("No speech backend available (was '%s')",
                 previous.empty() ? "none" : previous.c_str());
        return false;
    }

    const char* current = prism_backend_name(sBackend);
    if (previous != current) {
        // Not spoken: the player hears the voice change anyway, and announcing it would be
        // speaking for them. The log is what a bug report needs.
        LOG_INFO("Speech backend now '%s' (was '%s')", current,
                 previous.empty() ? "none" : previous.c_str());
    }
    return true;
}

// One utterance, straight to the backend. Caller must hold sMutex and have gated on
// sAvailable/sEnabled; the /speech ring and the log line are the caller's job so that queued and
// direct speech both record exactly once.
void SpeakRaw(const std::string& text) {
    // A screen reader that came or went since the last line: take the best backend now, BEFORE
    // this utterance rather than after it.
    if (sRebindRequested.exchange(false, std::memory_order_relaxed))
        RebindBackend();

    if (!sBackend) return;

    // interrupt = false, always. Whether a new line cuts off the one being read is the screen
    // reader's decision (its own queue and the player's settings), never the mod's.
    constexpr bool kInterrupt = false;
    PrismError err = prism_backend_speak(sBackend, text.c_str(), kInterrupt);
    if (err == PRISM_OK) return;

    // The failure SRAL could not report: the backend died under us (NVDA exited, its RPC endpoint
    // is gone, and Prism never reconnects an existing instance). One re-bind and one retry, so a
    // dead screen reader costs a single line rather than the rest of the session.
    LOG_WARN("Speech failed on '%s': %s", prism_backend_name(sBackend), prism_error_string(err));
    if (!RebindBackend()) return;

    err = prism_backend_speak(sBackend, text.c_str(), kInterrupt);
    if (err != PRISM_OK)
        LOG_ERROR("Speech failed after re-bind: %s", prism_error_string(err));
}

// Caller must hold sMutex and have checked sBackend. ⛔ JAWS's COM StopSpeech reports success and
// stops nothing: its audio ran on for the full sentence, the same as with no call at all
// (docs/input-map.md). A blank line that flushes is the stop JAWS honours, and Prism leaves the
// blank alone; an empty one fails its conversion. A line queued right after still plays.
void StopBackend() {
    (void)prism_backend_stop(sBackend);
    if (std::string(prism_backend_name(sBackend)) == "JAWS")
        (void)prism_backend_speak(sBackend, " ", /*interrupt*/ true);
}

} // namespace

bool Init() {
    std::lock_guard<std::mutex> lock(sMutex);
    if (sAvailable) return true;

    // Prism's diagnostics land in X2Access.log before anything else can fail, so a backend that
    // refuses to initialize explains itself instead of just going quiet.
    (void)prism_set_log_handler(PrismLogHandler{&OnPrismLog, nullptr});
    (void)prism_set_log_level(PRISM_LOG_LEVEL_INFO);

    PrismConfig cfg = prism_config_init();
    cfg.availability_callback = &OnBackendAvailability;
    // Two seconds, debounced twice, backing off to thirty while nothing moves: a screen reader
    // starting or stopping is a human-scale event, and this thread must never be a reason the
    // game loses frames.
    cfg.availability_poll_interval_ms = 2000;
    cfg.availability_debounce_samples = 2;
    cfg.availability_backoff_max_ms = 30000;
    cfg.availability_auto_power_manage = true;

    sCtx = prism_init(&cfg);
    if (!sCtx) {
        LOG_ERROR("prism_init failed");
        return false;
    }

    // A missing backend is NOT a failed init. The poll thread is running, so a screen reader
    // started after the game will raise the availability callback and the next line binds to it.
    // (SRAL had to be given up on here; with Prism there is nothing to give up on.)
    sAvailable = true;
    if (RebindBackend())
        LOG_INFO("TTS initialized — backend: %s", prism_backend_name(sBackend));
    else
        LOG_WARN("TTS initialized with no backend — waiting for one to appear");

    return true;
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(sMutex);
    if (!sAvailable) return;

    sAvailable = false;
    sQueue.clear();

    // ⛔ Do NOT call prism_shutdown() / prism_log_shutdown() here. Our only caller is
    // ShutdownAll() from DllMain(DLL_PROCESS_DETACH), i.e. under the LOADER LOCK, and both of
    // those join a worker thread (BackendEnumerator's jthread, the logging drain) — while
    // prism_shutdown additionally calls CoUninitialize and a run of __FUnloadDelayLoadedDLL2,
    // which is FreeLibrary. Joining a thread and unloading a DLL are the two canonical loader-lock
    // deadlocks, and at process exit the threads being joined are already dead, so the wait never
    // returns. Same rule the dev server's bounded Stop() already follows. The context is therefore
    // leaked deliberately: the process is going away and the OS reclaims it.
    //
    // What IS safe here is un-hooking our own callbacks so nothing can re-enter this module after
    // Log::Shutdown runs (next line in ShutdownAll), and cutting off speech in progress — a player
    // quitting the game should not hear the rest of the sentence. JAWS ignores this stop.
    (void)prism_set_log_level(PRISM_LOG_LEVEL_NONE);
    (void)prism_set_log_handler(PrismLogHandler{nullptr, nullptr});
    if (sBackend)
        (void)prism_backend_stop(sBackend);

    LOG_INFO("TTS shut down");
}

void Speak(const std::string& text) {
    if (text.empty()) return;
    std::lock_guard<std::mutex> lock(sMutex);
    if (!sAvailable || !sEnabled) return;
    // The speech log: every spoken line, so a session can be reconstructed from X2Access.log
    // ("what did it actually say when I pressed Left?").
    LOG_INFO("TTS: \"%s\"", text.c_str());
    // Dev harness /speech ring. Deliberately AFTER the availability/enabled gate, next to the
    // log line: the ring answers "what did the player hear", not "what did the mod consider
    // saying" — otherwise a muted session would read as a talkative one.
    DevServer::RecordSpeech(text);
    SpeakRaw(text);
}

void Silence() {
    std::lock_guard<std::mutex> lock(sMutex);
    if (!sAvailable || !sBackend) return;
    StopBackend();
}

void QueueSpeak(const std::string& text) {
    std::lock_guard<std::mutex> lock(sMutex);
    sQueue.push_back(text);
}

void FlushQueue() {
    std::lock_guard<std::mutex> lock(sMutex);
    if (!sAvailable || !sEnabled) {
        sQueue.clear();
        return;
    }
    for (const auto& text : sQueue) {
        DevServer::RecordSpeech(text);
        SpeakRaw(text);
    }
    sQueue.clear();
}

void SetEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(sMutex);
    sEnabled = enabled;
    if (!enabled && sBackend)
        StopBackend();
    LOG_INFO("TTS %s", enabled ? "enabled" : "disabled");
}

bool IsEnabled() {
    return sEnabled;
}

bool IsAvailable() {
    return sAvailable;
}

std::string GetEngineName() {
    std::lock_guard<std::mutex> lock(sMutex);
    // Copied rather than handed out straight from Prism: the backend owns that string and a
    // re-bind frees it.
    if (!sAvailable || !sBackend) return Vocab::EngineNone;
    return prism_backend_name(sBackend);
}

std::string GetEngineInfo() {
    std::lock_guard<std::mutex> lock(sMutex);
    if (!sAvailable) return Vocab::EngineTtsUnavailable;
    if (!sBackend) return Vocab::EngineBackendUnavailable;

    std::string info = Vocab::EngineLabel;
    info += prism_backend_name(sBackend);
    info += std::string(Vocab::EngineFeatures);

    // Only the four SRAL used to report, in the same words — this string is SPOKEN (F-key engine
    // info), and Prism's other twenty-odd bits are voice-enumeration and PCM plumbing a player has
    // no use for.
    const uint64_t features = prism_backend_get_features(sBackend);
    if (features & PRISM_BACKEND_SUPPORTS_SPEAK)      info += std::string(Vocab::EngineSpeech);
    if (features & PRISM_BACKEND_SUPPORTS_BRAILLE)    info += std::string(Vocab::EngineBraille);
    if (features & PRISM_BACKEND_SUPPORTS_SET_RATE)   info += std::string(Vocab::EngineRate);
    if (features & PRISM_BACKEND_SUPPORTS_SET_VOLUME) info += std::string(Vocab::EngineVolume);

    return info;
}

} // namespace TTS
