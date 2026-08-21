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
#include <shlobj.h>  // SHGetKnownFolderPath: Launch.log lives under the real Documents folder,
                     // which OneDrive redirection makes unguessable from environment variables

#include "dev_server.h"

#include "game_ui.h"
#include "gui_dump.h"
#include "input_arbiter.h"
#include "log.h"
#include "menu_navigator.h"
#include "screen_capture.h"
#include "tutorial_reader.h"

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

// Where the .asi lives: the marker file sits here, and it is the default screenshot
// destination — a directory the game process can certainly write to.
std::string gAsiDir;

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

// Last-write time of the loaded .asi, "YYYY-MM-DD HH:MM:SS", or a reason it is unknown.
// Resolved from OUR module handle rather than a composed path, so renaming the plugin or
// loading it from somewhere else still reports the file actually running.
std::string AsiStamp()
{
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&AsiStamp), &self)
        || !self)
        return "unknown (no module handle)";

    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(self, path, MAX_PATH))
        return "unknown (no module path)";

    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &info))
        return "unknown (cannot stat)";

    SYSTEMTIME utc{};
    SYSTEMTIME local{};
    if (!FileTimeToSystemTime(&info.ftLastWriteTime, &utc)
        || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
        return "unknown (bad timestamp)";

    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", local.wYear, local.wMonth,
        local.wDay, local.wHour, local.wMinute, local.wSecond);
    return buf;
}

std::string gAsiStamp = "unknown";

std::string HealthText()
{
    const uint64_t last = gLastPumpMs.load();
    std::string out = "ok\n";
    // __DATE__/__TIME__ alone is a trap: it is this TU's compile time, so an incremental build
    // that only touched gui_dump.cpp leaves it unchanged and /health cheerfully reports the old
    // stamp for a binary that IS new. The .asi's own mtime is what actually answers "is the
    // build I just made the one running?".
    out += "build: " __DATE__ " " __TIME__ " (dev_server.cpp)\n";
    out += "asi: " + gAsiStamp + "\n";
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

// ---- the game's own log ------------------------------------------------------------------------
//
// Console output does NOT come back through ConsoleCommand's return value in this build:
// `obj list class=XComTacticalCheatManager` returns an empty FString and writes its table to
// Launch.log, and so does "Command not recognized". An /exec that reported only the return value
// would therefore look like it did nothing for every command worth running. So we snapshot the
// log's size, run the command, and hand back what got appended.

std::wstring GameLogPath()
{
    static std::wstring cached;
    if (!cached.empty())
        return cached;
    PWSTR docs = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs) {
        cached = std::wstring(docs)
            + L"\\My Games\\XCOM2 War of the Chosen\\XComGame\\Logs\\Launch.log";
        CoTaskMemFree(docs);
    }
    return cached;
}

// The game holds Launch.log open for writing, so every share flag has to be granted or the
// open fails outright rather than merely reading stale bytes.
HANDLE OpenGameLog()
{
    const std::wstring path = GameLogPath();
    if (path.empty())
        return INVALID_HANDLE_VALUE;
    return CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
}

uint64_t GameLogSize()
{
    HANDLE file = OpenGameLog();
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    CloseHandle(file);
    return static_cast<uint64_t>(size.QuadPart);
}

std::string ReadGameLogFrom(uint64_t offset)
{
    // Bounded: `obj list class=Actor` is megabytes, and a dev harness answer nobody can read is
    // no better than no answer.
    constexpr uint64_t kMaxTail = 256 * 1024;

    HANDLE file = OpenGameLog();
    if (file == INVALID_HANDLE_VALUE)
        return {};
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    const uint64_t end = static_cast<uint64_t>(size.QuadPart);
    std::string out;
    if (end > offset) {
        uint64_t start = offset;
        bool truncated = false;
        if (end - start > kMaxTail) {
            start = end - kMaxTail;
            truncated = true;
        }
        LARGE_INTEGER pos{};
        pos.QuadPart = static_cast<LONGLONG>(start);
        if (SetFilePointerEx(file, pos, nullptr, FILE_BEGIN)) {
            out.resize(static_cast<size_t>(end - start));
            DWORD read = 0;
            if (ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr))
                out.resize(read);
            else
                out.clear();
        }
        if (truncated && !out.empty())
            out.insert(0, "[... truncated to the last 256 KB ...]\n");
    }
    CloseHandle(file);
    return out;
}

// ---- synthetic input ---------------------------------------------------------------------------

// A key spec: "Down", "Shift+Period", "Ctrl+S", "F5", "A". Case-insensitive.
struct KeySpec {
    uint8_t vk = 0;
    uint8_t mods = 0;
    bool extended = false; // the arrows/nav cluster need KEYEVENTF_EXTENDEDKEY to scan correctly
    std::string token;     // as the caller wrote it, so the reply names keys the way they asked
};

struct NamedKey {
    const char* name;
    uint8_t vk;
    bool extended;
};

const NamedKey kNamedKeys[] = {
    {"up", VK_UP, true}, {"down", VK_DOWN, true}, {"left", VK_LEFT, true},
    {"right", VK_RIGHT, true}, {"home", VK_HOME, true}, {"end", VK_END, true},
    {"pageup", VK_PRIOR, true}, {"pagedown", VK_NEXT, true}, {"insert", VK_INSERT, true},
    {"delete", VK_DELETE, true}, {"del", VK_DELETE, true},
    {"enter", VK_RETURN, false}, {"return", VK_RETURN, false},
    {"esc", VK_ESCAPE, false}, {"escape", VK_ESCAPE, false},
    {"space", VK_SPACE, false}, {"tab", VK_TAB, false},
    {"backspace", VK_BACK, false}, {"back", VK_BACK, false},
    {"comma", VK_OEM_COMMA, false}, {"period", VK_OEM_PERIOD, false},
};

bool ParseKeySpec(std::string token, KeySpec& out)
{
    // Modifier prefixes, repeatable: "Ctrl+Shift+Left".
    //
    // A SPACE separates too, and that is not a convenience: in a query string "+" means space,
    // so "?keys=Shift+Period" arrives here as "Shift Period" no matter what the caller intended.
    // Accepting both is what makes the obvious spelling work over a URL and in a POST body alike.
    for (;;) {
        const size_t plus = token.find_first_of("+ ");
        if (plus == std::string::npos || plus == 0)
            break;
        std::string mod = token.substr(0, plus);
        std::transform(mod.begin(), mod.end(), mod.begin(),
            [](unsigned char c) { return static_cast<char>(tolower(c)); });
        if (mod == "ctrl" || mod == "control")
            out.mods |= InputArbiter::ModCtrl;
        else if (mod == "shift")
            out.mods |= InputArbiter::ModShift;
        else if (mod == "alt")
            out.mods |= InputArbiter::ModAlt;
        else
            return false;
        token = token.substr(plus + 1);
    }
    if (token.empty())
        return false;

    std::string lower = token;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(tolower(c)); });

    for (const NamedKey& k : kNamedKeys) {
        if (lower == k.name) {
            out.vk = k.vk;
            out.extended = k.extended;
            return true;
        }
    }
    if (lower.size() >= 2 && lower[0] == 'f') {
        const int n = atoi(lower.c_str() + 1);
        if (n >= 1 && n <= 24) {
            out.vk = static_cast<uint8_t>(VK_F1 + n - 1);
            return true;
        }
    }
    if (lower.size() == 1) {
        const char c = lower[0];
        if (c >= 'a' && c <= 'z') {
            out.vk = static_cast<uint8_t>('A' + (c - 'a'));
            return true;
        }
        if (c >= '0' && c <= '9') {
            out.vk = static_cast<uint8_t>(c);
            return true;
        }
    }
    return false;
}

std::vector<std::string> SplitCommas(const std::string& text)
{
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t comma = text.find(',', pos);
        const size_t end = (comma == std::string::npos) ? text.size() : comma;
        std::string part = text.substr(pos, end - pos);
        // Tolerate "Down, Down" as well as "Down,Down".
        while (!part.empty() && isspace(static_cast<unsigned char>(part.front())))
            part.erase(part.begin());
        while (!part.empty() && isspace(static_cast<unsigned char>(part.back())))
            part.pop_back();
        if (!part.empty())
            parts.push_back(part);
        if (comma == std::string::npos)
            break;
        pos = comma + 1;
    }
    return parts;
}

// The game window, brought to the foreground. Both callers need it and for the same reason:
// synthetic input goes to the FOREGROUND window, and Desktop Duplication photographs whatever is
// VISIBLE — a backgrounded game is occluded by whatever sits on top of it.
HWND RaiseGameWindow()
{
    HWND hwnd = FindWindowW(L"LaunchUnrealUWindowsClient", nullptr);
    if (!hwnd)
        return nullptr;
    if (GetForegroundWindow() != hwnd) {
        SetForegroundWindow(hwnd);
        Sleep(120); // the engine needs a beat to notice it has focus before keys mean anything
    }
    return hwnd;
}

void SendScanKey(uint8_t vk, bool extended, bool down)
{
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = 0; // scancode-only: UE3 reads Raw Input, which ignores a VK-only synthetic press
    in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (extended ? KEYEVENTF_EXTENDEDKEY : 0)
        | (down ? 0 : KEYEVENTF_KEYUP);
    SendInput(1, &in, sizeof(in));
}

// Accept thread. Raises the game window first: synthetic input goes to the FOREGROUND window,
// so without this the keys land in whatever the user was looking at.
std::string SendInputKeys(const std::vector<KeySpec>& keys, uint32_t holdMs, uint32_t gapMs)
{
    if (!RaiseGameWindow())
        return "[input] game window not found\n";

    for (const KeySpec& k : keys) {
        if (k.mods & InputArbiter::ModCtrl) SendScanKey(VK_CONTROL, false, true);
        if (k.mods & InputArbiter::ModShift) SendScanKey(VK_SHIFT, false, true);
        if (k.mods & InputArbiter::ModAlt) SendScanKey(VK_MENU, false, true);
        SendScanKey(k.vk, k.extended, true);
        Sleep(holdMs);
        SendScanKey(k.vk, k.extended, false);
        if (k.mods & InputArbiter::ModAlt) SendScanKey(VK_MENU, false, false);
        if (k.mods & InputArbiter::ModShift) SendScanKey(VK_SHIFT, false, false);
        if (k.mods & InputArbiter::ModCtrl) SendScanKey(VK_CONTROL, false, false);
        Sleep(gapMs);
    }
    return "sent " + std::to_string(keys.size()) + " key(s) via SendInput\n";
}

// Game thread. Reports per key whether a claim took it — "not claimed" is a real answer, not a
// failure: it means that chord would have gone straight to the game.
std::string InjectKernelKeys(const std::vector<KeySpec>& keys)
{
    std::string out;
    for (const KeySpec& k : keys) {
        InputArbiter::KeyEvent down{};
        down.vk = k.vk;
        down.mods = k.mods;
        down.down = true;
        const bool claimed = InputArbiter::InjectSynthetic(down);
        if (claimed) {
            InputArbiter::KeyEvent up = down;
            up.down = false;
            InputArbiter::InjectSynthetic(up);
        }
        char line[160];
        snprintf(line, sizeof(line), "%s (vk 0x%02X mods 0x%02X): %s\n", k.token.c_str(),
            k.vk, k.mods, claimed ? "claimed" : "NOT claimed — this chord reaches the game");
        out += line;
    }
    return out;
}

// Accept thread — the capture builds its own D3D11 device and never touches the game's renderer.
std::string ScreenshotText(const std::string& pathParam, bool foreground, int outputOverride)
{
    std::string path = pathParam;
    if (path.empty())
        path = gAsiDir + "\\X2Access-shot.png";

    // Which monitor: the game's, so a multi-head setup does not photograph the empty one.
    HMONITOR monitor = nullptr;
    HWND hwnd = foreground ? RaiseGameWindow()
                           : FindWindowW(L"LaunchUnrealUWindowsClient", nullptr);
    if (hwnd)
        monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

    int width = 0;
    int height = 0;
    const std::string error = CaptureScreenToPng(path, monitor, outputOverride, width, height);
    if (!error.empty())
        return error + "\n";
    return path + "\n" + std::to_string(width) + "x" + std::to_string(height) + "\n";
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

std::string UrlDecode(const std::string& in);

// Every occurrence of `name=`, in query order. /call takes one per declared argument, and the
// order is the signature's order — so this must preserve it rather than collapse duplicates the
// way QueryParam's first-match does.
std::vector<std::string> QueryParamAll(const std::string& query, const char* name)
{
    std::vector<std::string> values;
    const std::string needle = std::string(name) + "=";
    size_t pos = 0;
    while (pos < query.size()) {
        const size_t amp = query.find('&', pos);
        const size_t end = (amp == std::string::npos) ? query.size() : amp;
        if (query.compare(pos, needle.size(), needle) == 0)
            values.push_back(UrlDecode(query.substr(pos + needle.size(), end - pos - needle.size())));
        if (amp == std::string::npos)
            break;
        pos = amp + 1;
    }
    return values;
}

std::string UrlDecode(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] == '+') {
            out += ' ';
        } else if (in[i] == '%' && i + 2 < in.size()
            && isxdigit(static_cast<unsigned char>(in[i + 1]))
            && isxdigit(static_cast<unsigned char>(in[i + 2]))) {
            const char hex[3] = {in[i + 1], in[i + 2], '\0'};
            out += static_cast<char>(strtoul(hex, nullptr, 16));
            i += 2;
        } else {
            out += in[i];
        }
    }
    return out;
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

// /gui and /props take the same selector: an address from an earlier dump, the newest instance
// of a class anywhere, or a screen on the stack (the default, topmost when unnamed).
GuiDump::Selector ParseSelector(const std::string& query)
{
    GuiDump::Selector sel;
    const std::string ptr = QueryParam(query, "ptr");
    if (!ptr.empty()) {
        // Always hex, with or without the 0x a dump line prints.
        const char* text = ptr.c_str();
        if (ptr.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
            text += 2;
        sel.ptr = reinterpret_cast<void*>(_strtoui64(text, nullptr, 16));
    }
    sel.cdo = UrlDecode(QueryParam(query, "cdo"));
    sel.instance = UrlDecode(QueryParam(query, "instance"));
    sel.screen = UrlDecode(QueryParam(query, "screen"));
    return sel;
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

    // Synthetic input. The SendInput tier deliberately stays on the accept thread: it sleeps
    // between keys, and sleeping on the game thread would stall the frame it is trying to drive.
    if (req.path == "/input") {
        const std::string spec = UrlDecode(QueryParam(req.query, "keys"));
        if (spec.empty()) {
            status = 400;
            return "usage: /input?keys=Down,Down,Enter[&mode=kernel][&hold=ms][&gap=ms]\n"
                   "  names: Up Down Left Right Home End PageUp PageDown Insert Delete Enter\n"
                   "         Esc Space Tab Backspace Comma Period F1-F24 A-Z 0-9\n"
                   "  modifiers: Ctrl+ Shift+ Alt+ (repeatable, e.g. Ctrl+Shift+Left;\n"
                   "             a space separates too, since \"+\" is a space in a query)\n";
        }
        std::vector<KeySpec> keys;
        for (const std::string& token : SplitCommas(spec)) {
            KeySpec k;
            if (!ParseKeySpec(token, k)) {
                status = 400;
                return "[input] unrecognised key: " + token + "\n";
            }
            k.token = token;
            keys.push_back(k);
        }
        if (QueryParam(req.query, "mode") == "kernel")
            return OnGameThread([keys] { return InjectKernelKeys(keys); });
        return SendInputKeys(keys, static_cast<uint32_t>(QueryInt(req.query, "hold", 40)),
            static_cast<uint32_t>(QueryInt(req.query, "gap", 90)));
    }

    if (req.path == "/screenshot") {
        return ScreenshotText(UrlDecode(QueryParam(req.query, "path")),
            QueryInt(req.query, "foreground", 1) != 0,
            static_cast<int>(QueryInt(req.query, "output", -1)));
    }

    // Game-thread endpoints.
    if (req.path == "/status")
        return OnGameThread(StatusText);

    // The tutorial mission's state as structured data: replay frame, the on-rails rejection
    // count, the modal, and every active world message with its RAW text next to what the reader
    // would actually speak. That last pair is the point — markup translation is the half of this
    // feature a screenshot cannot check.
    if (req.path == "/tutorial")
        return OnGameThread(TutorialReader::Describe);

    if (req.path == "/exec") {
        // Body first so a command needing no URL-encoding can just be POSTed.
        std::string cmd = req.body;
        if (cmd.empty())
            cmd = UrlDecode(QueryParam(req.query, "cmd"));
        while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r'))
            cmd.pop_back();
        if (cmd.empty()) {
            status = 400;
            return "usage: /exec?cmd=<console command>   (or POST the command as the body)\n";
        }
        const uint64_t logBefore = GameLogSize();
        const std::string result = OnGameThread([cmd] { return GameUI::ConsoleCommand(cmd); });
        Sleep(250); // the game flushes Launch.log on its own schedule, not on our call's return
        const std::string logTail = ReadGameLogFrom(logBefore);

        std::string out;
        if (!result.empty())
            out += result + "\n";
        if (!logTail.empty())
            out += "--- Launch.log ---\n" + logTail;
        // Saying so beats an empty body, which reads like a transport failure.
        return out.empty() ? "(ran; no output, nothing logged)\n" : out;
    }

    // The widget tree and the property dump. Both take the same selector, so a session reads
    // /gui to see the tree, copies an address out of it, and drills in with /props?ptr=.
    if (req.path == "/gui") {
        const GuiDump::Selector sel = ParseSelector(req.query);
        const int depth = static_cast<int>(QueryInt(req.query, "depth", 8));
        return OnGameThread([sel, depth] { return GuiDump::Tree(sel, depth); });
    }

    if (req.path == "/props") {
        const GuiDump::Selector sel = ParseSelector(req.query);
        const std::string filter = UrlDecode(QueryParam(req.query, "filter"));
        return OnGameThread([sel, filter] { return GuiDump::Props(sel, filter); });
    }

    // The third verb on that selector: /gui sees the tree, /props reads an object out, /call
    // makes it DO something. Its reason for existing is the controller-gated function — the
    // game's own code, correct and complete, that a mouse+keyboard player's branch never
    // reaches. Measuring an ungate this way is the difference between one HTTP call and a full
    // build-deploy-relaunch cycle per hypothesis.
    if (req.path == "/call") {
        const GuiDump::Selector sel = ParseSelector(req.query);
        const std::string fn = UrlDecode(QueryParam(req.query, "fn"));
        // &arg=, repeated once per declared argument, in declaration order. This is what makes
        // the Scaleform bridge reachable: everything about a live movie is read through
        // GFxMoviePlayer.GetVariableString / ActionScriptInt / ActionScriptString, and all of
        // them take a path string. That is the only way to see the half of the UI that is
        // Flash-only — a text field's caret, the key-bindings rows, anything a screen wrote out
        // and did not retain.
        const std::vector<std::string> args = QueryParamAll(req.query, "arg");
        return OnGameThread([sel, fn, args] { return GuiDump::Call(sel, fn, args); });
    }

    status = 404;
    return "unknown endpoint\n"
           "  GET  /health              liveness + build stamp + game-thread pump age\n"
           "  GET  /speech?since=N      everything TTS::Speak was handed, cursor-paged\n"
           "  GET  /log?since=N&tail=N  the ASI log ring, cursor-paged\n"
           "  GET  /status              screen stack, gates, navigator + tile-reader state\n"
           "  GET  /tutorial            tutorial replay state + active world messages\n"
           "  GET  /input?keys=A,B      synthetic keys (&mode=kernel drives only our layer)\n"
           "  POST /exec  <command>     run a console command, return what it printed\n"
           "  GET  /gui[?depth=N]       widget tree of a screen: class, state, text\n"
           "  GET  /props[?filter=]     one object's properties: offset, type, live value\n"
           "  GET  /call?fn=<F>&arg=X   invoke a script function; prints its return. Repeat\n"
           "                            &arg once per declared argument, in order — which is\n"
           "                            how the Flash bridge is reached (GetVariableString,\n"
           "                            ActionScriptInt/String on ?instance=UIMovie_2D)\n"
           "  GET  /screenshot[?path=]  PNG of the monitor the game is on\n"
           "\n"
           "  /gui, /props and /call select with ?screen=<Class> (the screen stack, topmost by\n"
           "  default), ?instance=<Class> (newest instance anywhere, falling back to the\n"
           "  class default object when there is none), ?cdo=<Class> (that default object\n"
           "  explicitly — how to read the companion models) or ?ptr=0x<addr>\n";
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

    gAsiStamp = AsiStamp();

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

    gAsiDir = asiDir;

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
