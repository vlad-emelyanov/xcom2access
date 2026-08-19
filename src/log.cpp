#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>
#include <share.h>
#include <string>

namespace Log {

static FILE* sFile = nullptr;
static std::mutex sMutex;

#ifdef X2ACCESS_DEVSERVER
// In-memory tail for the dev harness's /log endpoint (dev builds only — release pays nothing).
// The file stays the record of truth; this is only what the harness can hand out with a cursor.
static constexpr size_t kRingCapacity = 1000;
static constexpr size_t kRingLineMax = 1024; // ring copies truncate here; the FILE never does
static std::deque<std::string> sRing;
static long long sRingBase = 0; // global line index of sRing.front()
#endif

// How many previous sessions to keep beside the live log. Debugging this project routinely
// means comparing a run against the one before it ("did this build change the timing?"), and
// the log used to be truncated on open — so the moment the game restarted, the evidence for
// whatever you were chasing was already gone.
static constexpr int kKeptLogs = 3;

// X2Access.log -> .1.log -> .2.log -> .3.log, oldest dropped. Every failure here is ignored
// on purpose: no previous log, or one still held open by another instance, must not stop the
// new session from logging — worst case we fall back to the old truncate-on-open behavior.
static void RotateLogs(const std::string& base) {
    DeleteFileA((base + "." + std::to_string(kKeptLogs) + ".log").c_str());
    for (int i = kKeptLogs - 1; i >= 1; i--) {
        MoveFileA((base + "." + std::to_string(i) + ".log").c_str(),
                  (base + "." + std::to_string(i + 1) + ".log").c_str());
    }
    MoveFileA((base + ".log").c_str(), (base + ".1.log").c_str());
}

bool Init(const std::string& logDir) {
    std::lock_guard<std::mutex> lock(sMutex);
    if (sFile) return true;

    std::string base = logDir + "\\X2Access";
    RotateLogs(base);
    std::string path = base + ".log";
    // _SH_DENYWR: others may READ the live log (tailing it while the game runs); only writing
    // is exclusive. Plain fopen_s denied all sharing and made the log unreadable mid-session.
    sFile = _fsopen(path.c_str(), "w", _SH_DENYWR);
    if (!sFile) {
        return false;
    }

    // Write BOM-less UTF-8 header line
    fprintf(sFile, "=== X2Access Log Started ===\n");
    fflush(sFile);
    return true;
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(sMutex);
    if (sFile) {
        fprintf(sFile, "=== X2Access Log Ended ===\n");
        fflush(sFile);
        fclose(sFile);
        sFile = nullptr;
    }
}

static const char* LevelToStr(Level level) {
    switch (level) {
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO";
        case Level::Warn:  return "WARN";
        case Level::Error: return "ERROR";
    }
    return "???";
}

void Write(Level level, const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(sMutex);
    if (!sFile) return;

    // Milliseconds are not decoration: at one-second resolution "detached at :24, spoke at :25"
    // could mean 100 ms or 1900 ms, and telling those two apart was the whole question behind the
    // screen-transition latency work.
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(sFile, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] [%s] ",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
            st.wMilliseconds, LevelToStr(level));

    // Message
    va_list args;
    va_start(args, fmt);
    vfprintf(sFile, fmt, args);
    va_end(args);

    fprintf(sFile, "\n");
    fflush(sFile); // Crash resilience
}

} // namespace Log
