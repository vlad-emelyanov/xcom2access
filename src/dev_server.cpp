// See dev_server.h for the gating and the threading contract.
//
// This TU owns Winsock: <winsock2.h> MUST come before anything that can drag in <windows.h>,
// which otherwise pulls the winsock 1 header and produces a wall of redefinition errors.
#ifdef X2ACCESS_DEVSERVER

// NOMINMAX before anything drags in <windows.h>: its min/max MACROS otherwise eat every
// std::max/std::min below and the errors point at the STL, not at the include.
#define NOMINMAX

#include <winsock2.h>
#include <windows.h>

#include "dev_server.h"

#include "game_ui.h"
#include "log.h"
#include "menu_navigator.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace DevServer {

namespace {

// 8772 is the RTAccess harness, 8773 CyberAccess. Distinct ports so all three can be armed at
// once and a stray curl never hits the wrong game.
constexpr uint16_t kDefaultPort = 8774;

constexpr size_t kSpeechCapacity = 500;
constexpr size_t kMaxHeaderBytes = 16 * 1024;
constexpr size_t kMaxBodyBytes = 256 * 1024;
constexpr uint32_t kJobTimeoutMs = 15'000;

// Bounded per tick: /status walks the whole screen stack and calls into UnrealScript, and a
// scripted flood must not land inside one frame. The remainder stays queued — the callers are
// already parked on their responses, so carrying over costs them nothing.
constexpr size_t kMaxJobsPerPump = 4;

// One queued unit of game-thread work. shared_ptr on both sides: a timed-out HTTP caller
// abandons its job, and the pump may still run and complete it later without dangling.
struct Job {
    std::function<std::string()> work;
    std::string result;
    std::mutex lock;
    std::condition_variable done;
    bool finished = false;
};

std::atomic<bool> gEnabled{false};
std::atomic<bool> gStopping{false};
bool gWsaInitialized = false;
SOCKET gListener = INVALID_SOCKET;
std::atomic<SOCKET> gClient{INVALID_SOCKET};
std::thread gThread;
uint16_t gPort = kDefaultPort;

// Last Pump() timestamp. THE liveness signal: a /health that says the pump last ran 8 seconds
// ago tells you the game thread is wedged, which is otherwise indistinguishable from a dead
// server. Written on the game thread, read on the accept thread.
std::atomic<uint64_t> gLastPumpMs{0};

std::mutex gJobsLock;
std::vector<std::shared_ptr<Job>> gJobs;

std::mutex gSpeechLock;
std::deque<std::string> gSpeech;
long long gSpeechBase = 0; // global index of gSpeech.front()

// ---- job plumbing ---------------------------------------------------------------------------

void FinishJob(const std::shared_ptr<Job>& job, std::string result)
{
    {
        std::lock_guard<std::mutex> lock(job->lock);
        job->result = std::move(result);
        job->finished = true;
    }
    job->done.notify_all();
}

// Accept thread: run one unit of work on the game thread, blocking up to the timeout.
std::string OnGameThread(std::function<std::string()> work, uint32_t timeoutMs = kJobTimeoutMs)
{
    if (gStopping.load())
        return "[shutdown]\n";

    auto job = std::make_shared<Job>();
    job->work = std::move(work);
    {
        std::lock_guard<std::mutex> lock(gJobsLock);
        gJobs.push_back(job);
    }

    std::unique_lock<std::mutex> lock(job->lock);
    if (!job->done.wait_for(lock, std::chrono::milliseconds(timeoutMs),
            [&] { return job->finished; })) {
        // Not a server fault: the frame pump rides ProcessEvent traffic, so a load screen or a
        // hang starves it. Say which, so the caller does not go hunting the harness.
        const uint64_t age = GetTickCount64() - gLastPumpMs.load();
        char msg[160];
        snprintf(msg, sizeof(msg),
            "[timeout] game thread did not run the job (last pump %llu ms ago — "
            "loading screen / movie / hang?)\n",
            static_cast<unsigned long long>(age));
        return msg;
    }
    return job->result;
}

// ---- rings ----------------------------------------------------------------------------------

// Shared shape for /speech and /log: "cursor: N" then "<index>: <line>" for everything after
// `since`. Polling with the returned cursor gives exactly-once delivery of new lines.
template <typename Ring>
std::string RenderRing(const Ring& ring, long long base, long long since)
{
    const long long end = base + static_cast<long long>(ring.size());
    std::string out = "cursor: " + std::to_string(end) + "\n";
    for (long long i = std::max(since, base); i < end; i++) {
        out += std::to_string(i);
        out += ": ";
        out += ring[static_cast<size_t>(i - base)];
        out += "\n";
    }
    return out;
}

std::string RenderSpeech(long long since)
{
    std::lock_guard<std::mutex> lock(gSpeechLock);
    return RenderRing(gSpeech, gSpeechBase, since);
}

// ---- endpoint bodies ------------------------------------------------------------------------

std::string HealthText()
{
    const uint64_t last = gLastPumpMs.load();
    std::string out = "ok\n";
    out += "build: " __DATE__ " " __TIME__ "\n";
    out += "port: " + std::to_string(gPort) + "\n";
    if (last == 0)
        out += "pump: never (the game thread has not ticked yet)\n";
    else
        out += "pump-age-ms: " + std::to_string(GetTickCount64() - last) + "\n";
    return out;
}

// Game thread. Everything here reads through GameUI, which SEH-guards its own engine reads.
std::string StatusText()
{
    std::string out;
    char buf[512];

    const uint64_t last = gLastPumpMs.load();
    snprintf(buf, sizeof(buf), "pump-age-ms: %llu\n",
        static_cast<unsigned long long>(last ? GetTickCount64() - last : 0));
    out += buf;

    out += std::string("movie: ") + (GameUI::MoviePlaying() ? "playing" : "no") + "\n";
    out += std::string("loadscreen: ") + (GameUI::LoadScreenShowing() ? "showing" : "no") + "\n";

    const GameUI::StackState st = GameUI::ReadStackState();
    if (!st.valid) {
        out += "stack: UNREADABLE (no live UIScreenStack)\n";
    } else {
        snprintf(buf, sizeof(buf),
            "stack: %s (inputBlocked=%d screensHidden=%d cinematic=%d)\n",
            st.Interactive() ? "interactive" : "GATED",
            st.inputBlocked, st.screensHidden, st.cinematic);
        out += buf;
    }

    out += "navigator: ";
    if (MenuNavigator::IsAttached()) {
        const char* recipe = MenuNavigator::AttachedRecipeName();
        out += std::string("attached (") + (recipe ? recipe : "?") + ")\n";
    } else {
        out += "detached\n";
    }

    const GameUI::TacticalGate gate = GameUI::ReadTacticalGate();
    snprintf(buf, sizeof(buf),
        "tile-reader: %s (hudOnStack=%d menuRaised=%d blocked=%d blocker=%s)\n",
        gate.Active() ? "active" : "inactive",
        gate.hudOnStack, gate.menuRaised, gate.blocked,
        gate.blocker[0] ? gate.blocker : "-");
    out += buf;

    out += "tracked: " + GameUI::DescribeTrackedScreens() + "\n";
    out += "screens (top first):\n" + GameUI::DescribeScreenStack();
    return out;
}

// ---- HTTP -----------------------------------------------------------------------------------

struct Request {
    std::string method;
    std::string path;
    std::string query;
    std::string body;
};

// The named query parameter's value ("since=12&tail=40" -> "12"), or empty when absent.
std::string QueryParam(const std::string& query, const char* name)
{
    const std::string needle = std::string(name) + "=";
    size_t pos = 0;
    while (pos < query.size()) {
        const size_t amp = query.find('&', pos);
        const size_t end = (amp == std::string::npos) ? query.size() : amp;
        if (query.compare(pos, needle.size(), needle) == 0)
            return query.substr(pos + needle.size(), end - pos - needle.size());
        if (amp == std::string::npos)
            break;
        pos = amp + 1;
    }
    return {};
}

long long QueryInt(const std::string& query, const char* name, long long fallback)
{
    const std::string value = QueryParam(query, name);
    if (value.empty())
        return fallback;
    return _strtoi64(value.c_str(), nullptr, 10);
}

bool RecvRequest(SOCKET sock, Request& out, int& status)
{
    std::string buf;
    size_t headerEnd = std::string::npos;
    char chunk[2048];
    while (headerEnd == std::string::npos) {
        if (buf.size() > kMaxHeaderBytes) {
            status = 400;
            return false;
        }
        const int n = recv(sock, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            status = 400;
            return false;
        }
        buf.append(chunk, static_cast<size_t>(n));
        headerEnd = buf.find("\r\n\r\n");
    }

    // Request line: METHOD SP target SP HTTP/x
    const std::string line = buf.substr(0, buf.find("\r\n"));
    const size_t sp1 = line.find(' ');
    const size_t sp2 = (sp1 == std::string::npos) ? std::string::npos : line.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) {
        status = 400;
        return false;
    }
    out.method = line.substr(0, sp1);
    const std::string target = line.substr(sp1 + 1, sp2 - sp1 - 1);
    const size_t q = target.find('?');
    out.path = target.substr(0, q);
    out.query = (q == std::string::npos) ? std::string{} : target.substr(q + 1);

    size_t contentLength = 0;
    {
        std::string headers = buf.substr(0, headerEnd);
        std::transform(headers.begin(), headers.end(), headers.begin(),
            [](unsigned char c) { return static_cast<char>(tolower(c)); });
        const size_t cl = headers.find("content-length:");
        if (cl != std::string::npos)
            contentLength = strtoul(headers.c_str() + cl + 15, nullptr, 10);
    }
    if (contentLength > kMaxBodyBytes) {
        status = 400;
        return false;
    }

    out.body = buf.substr(headerEnd + 4);
    while (out.body.size() < contentLength) {
        const int n = recv(sock, chunk, sizeof(chunk), 0);
        if (n <= 0)
            break;
        out.body.append(chunk, static_cast<size_t>(n));
    }
    return true;
}

std::string Route(const Request& req, int& status)
{
    // Off-thread endpoints first: these are exactly the ones that must keep answering when the
    // game thread is the thing under investigation.
    if (req.path == "/" || req.path == "/health")
        return HealthText();

    if (req.path == "/speech")
        return RenderSpeech(QueryInt(req.query, "since", 0));

    if (req.path == "/log") {
        long long base = 0;
        const std::vector<std::string> lines = Log::Recent(base);
        const long long cursor = base + static_cast<long long>(lines.size());
        // `since` defaults to "the last `tail` lines" so a bare /log is useful without having
        // to learn the cursor first.
        const long long tail = QueryInt(req.query, "tail", 50);
        const long long since = QueryInt(req.query, "since", std::max(0LL, cursor - tail));
        return RenderRing(lines, base, since);
    }

    // Game-thread endpoints.
    if (req.path == "/status")
        return OnGameThread(StatusText);

    status = 404;
    return "unknown endpoint\n"
           "  GET /health              liveness + build stamp + game-thread pump age\n"
           "  GET /speech?since=N      everything TTS::Speak was handed, cursor-paged\n"
           "  GET /log?since=N&tail=N  the ASI log ring, cursor-paged\n"
           "  GET /status              screen stack, gates, navigator + tile-reader state\n";
}

// send() may accept fewer bytes than offered; a /status dump is small but /log is not bounded
// that tightly, so loop.
void SendAll(SOCKET sock, const char* data, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        const int n = send(sock, data + sent,
            static_cast<int>(std::min<size_t>(len - sent, 1 << 20)), 0);
        if (n <= 0)
            return;
        sent += static_cast<size_t>(n);
    }
}

void SendResponse(SOCKET sock, int status, const std::string& body)
{
    const char* reason = (status == 200) ? "OK"
        : (status == 400)               ? "Bad Request"
        : (status == 404)               ? "Not Found"
                                        : "Internal Server Error";
    const std::string head = "HTTP/1.1 " + std::to_string(status) + " " + reason
        + "\r\nContent-Type: text/plain; charset=utf-8"
        + "\r\nContent-Length: " + std::to_string(body.size())
        + "\r\nConnection: close\r\n\r\n";
    SendAll(sock, head.data(), head.size());
    if (!body.empty())
        SendAll(sock, body.data(), body.size());
}

// Serial, one request per connection. A dev harness never needs concurrency, and a single
// thread keeps the shutdown reasoning trivial — which matters when shutdown runs under the
// loader lock.
void AcceptLoop()
{
    while (!gStopping.load()) {
        const SOCKET client = accept(gListener, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (gStopping.load())
                break;
            continue;
        }
        gClient.store(client);

        // A stuck client must not wedge the loop — recv would otherwise block forever.
        DWORD recvTimeoutMs = 5000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char*>(&recvTimeoutMs), sizeof(recvTimeoutMs));

        Request req;
        int status = 200;
        if (!RecvRequest(client, req, status)) {
            SendResponse(client, status, "bad request\n");
        } else {
            std::string body;
            try {
                body = Route(req, status);
            } catch (const std::exception& e) {
                status = 500;
                body = std::string("[handler error] ") + e.what() + "\n";
            } catch (...) {
                status = 500;
                body = "[handler error]\n";
            }
            SendResponse(client, status, body);
        }

        const SOCKET mine = gClient.exchange(INVALID_SOCKET);
        if (mine != INVALID_SOCKET)
            closesocket(mine);
    }
}

} // namespace

// ---- lifecycle --------------------------------------------------------------------------------

void Start(const std::string& asiDir)
{
    if (gEnabled.load() || gThread.joinable())
        return;

    const std::string marker = asiDir + "\\X2Access.devserver";
    {
        std::ifstream probe(marker);
        if (!probe) {
            LOG_INFO("Dev server: disarmed (no marker file at %s)", marker.c_str());
            return;
        }
        // Optional override: the marker file's first token as a port.
        long value = 0;
        if (probe >> value && value >= 1024 && value <= 65535)
            gPort = static_cast<uint16_t>(value);
    }

    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_ERROR("Dev server: WSAStartup failed");
        return;
    }
    gWsaInitialized = true;

    gListener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(gPort);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // loopback only, never a listening port
    if (gListener == INVALID_SOCKET
        || bind(gListener, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR
        || listen(gListener, 4) == SOCKET_ERROR) {
        LOG_ERROR("Dev server: bind/listen on 127.0.0.1:%u failed (WSA error %d)",
            static_cast<unsigned>(gPort), WSAGetLastError());
        if (gListener != INVALID_SOCKET) {
            closesocket(gListener);
            gListener = INVALID_SOCKET;
        }
        WSACleanup();
        gWsaInitialized = false;
        return;
    }

    gStopping.store(false);
    gThread = std::thread(AcceptLoop);
    gEnabled.store(true);
    LOG_INFO("Dev server listening on 127.0.0.1:%u", static_cast<unsigned>(gPort));
}

void Stop()
{
    if (!gThread.joinable())
        return;

    gStopping.store(true);

    // Release any HTTP caller blocked on a job the pump will never run again — otherwise the
    // wait below burns its whole timeout against our own accept thread.
    {
        std::lock_guard<std::mutex> lock(gJobsLock);
        for (auto& job : gJobs)
            FinishJob(job, "[shutdown]\n");
        gJobs.clear();
    }

    if (gListener != INVALID_SOCKET) {
        closesocket(gListener); // unblocks accept()
        gListener = INVALID_SOCKET;
    }
    const SOCKET client = gClient.exchange(INVALID_SOCKET);
    if (client != INVALID_SOCKET)
        closesocket(client); // unblocks a recv/send mid-request

    // BOUNDED wait, not join(). Stop() runs from DllMain(DLL_PROCESS_DETACH), i.e. under the
    // loader lock, and a thread that is trying to exit needs that same lock inside ntdll — so an
    // unconditional join can deadlock the game's shutdown. If the thread has not finished in a
    // second, detach and let process teardown reclaim it: a hung exit is a far worse failure
    // than a leaked thread in a process that is already dying.
    if (WaitForSingleObject(gThread.native_handle(), 1000) == WAIT_OBJECT_0) {
        gThread.join();
    } else {
        LOG_WARN("Dev server: accept thread did not exit in 1s — detaching");
        gThread.detach();
    }

    if (gWsaInitialized) {
        WSACleanup();
        gWsaInitialized = false;
    }
    gEnabled.store(false);
    LOG_INFO("Dev server stopped");
}

void Pump()
{
    if (!gEnabled.load(std::memory_order_relaxed))
        return;

    // Stamped unconditionally, before any early-out: /health's whole value is that this number
    // keeps moving while nothing else does.
    gLastPumpMs.store(GetTickCount64(), std::memory_order_relaxed);

    std::vector<std::shared_ptr<Job>> jobs;
    {
        std::lock_guard<std::mutex> lock(gJobsLock);
        if (gJobs.empty())
            return;
        const size_t take = std::min(gJobs.size(), kMaxJobsPerPump);
        jobs.assign(gJobs.begin(), gJobs.begin() + take);
        gJobs.erase(gJobs.begin(), gJobs.begin() + take);
    }

    for (auto& job : jobs) {
        std::string result;
        try {
            result = job->work();
        } catch (const std::exception& e) {
            result = std::string("[host error] ") + e.what() + "\n";
        } catch (...) {
            result = "[host error]\n";
        }
        FinishJob(job, std::move(result));
    }
}

void RecordSpeech(const std::string& text, bool interrupt)
{
    if (text.empty())
        return;

    std::lock_guard<std::mutex> lock(gSpeechLock);
    gSpeech.push_back(interrupt ? "! " + text : "  " + text);
    while (gSpeech.size() > kSpeechCapacity) {
        gSpeech.pop_front();
        gSpeechBase++;
    }
}

} // namespace DevServer

#endif // X2ACCESS_DEVSERVER
