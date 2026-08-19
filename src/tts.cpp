#include "tts.h"
#include "log.h"

#include <SRAL.h>

#include <mutex>
#include <vector>
#include <string>

namespace TTS {

static bool sAvailable = false;
static bool sEnabled = true;
static std::mutex sMutex;
static std::vector<std::string> sQueue;

bool Init() {
    if (!SRAL_Initialize(SRAL_ENGINE_UIA)) {
        LOG_ERROR("SRAL_Initialize failed");
        return false;
    }

    sAvailable = true;

    int engine = SRAL_GetCurrentEngine();
    const char* name = SRAL_GetEngineName(engine);
    LOG_INFO("TTS initialized — engine: %s (id: %d)", name, engine);

    return true;
}

void Shutdown() {
    if (sAvailable) {
        Silence();
        SRAL_Uninitialize();
        sAvailable = false;
        LOG_INFO("TTS shut down");
    }
}

void Speak(const std::string& text, bool interrupt) {
    std::lock_guard<std::mutex> lock(sMutex);
    if (!sAvailable || !sEnabled) return;
    // The speech log: every spoken line, so a session can be reconstructed from X2Access.log
    // ("what did it actually say when I pressed Left?").
    LOG_INFO("TTS%s: \"%s\"", interrupt ? "!" : "", text.c_str());
    SRAL_Speak(text.c_str(), interrupt);
}

void SpeakInterrupt(const std::string& text) {
    Speak(text, true);
}

void Silence() {
    std::lock_guard<std::mutex> lock(sMutex);
    if (!sAvailable) return;
    SRAL_StopSpeech();
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
        SRAL_Speak(text.c_str(), false);
    }
    sQueue.clear();
}

void SetEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(sMutex);
    sEnabled = enabled;
    if (!enabled) {
        SRAL_StopSpeech();
    }
    LOG_INFO("TTS %s", enabled ? "enabled" : "disabled");
}

bool IsEnabled() {
    return sEnabled;
}

bool IsAvailable() {
    return sAvailable;
}

std::string GetEngineName() {
    if (!sAvailable) return "None";
    int engine = SRAL_GetCurrentEngine();
    const char* name = SRAL_GetEngineName(engine);
    return name ? name : "Unknown";
}

std::string GetEngineInfo() {
    if (!sAvailable) return "TTS not available";

    int engine = SRAL_GetCurrentEngine();
    const char* name = SRAL_GetEngineName(engine);
    int features = SRAL_GetEngineFeatures(engine);

    std::string info = "Engine: ";
    info += (name ? name : "Unknown");
    info += ". Features:";

    if (features & SRAL_SUPPORTS_SPEECH)       info += " Speech";
    if (features & SRAL_SUPPORTS_BRAILLE)      info += " Braille";
    if (features & SRAL_SUPPORTS_SPEECH_RATE)   info += " Rate";
    if (features & SRAL_SUPPORTS_SPEECH_VOLUME) info += " Volume";

    return info;
}

} // namespace TTS
