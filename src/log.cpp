#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <share.h>
#include <string>

namespace Log {

static FILE* sFile = nullptr;
static std::mutex sMutex;

bool Init(const std::string& logDir) {
    std::lock_guard<std::mutex> lock(sMutex);
    if (sFile) return true;

    std::string path = logDir + "\\X2Access.log";
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

    // Timestamp
    time_t now = time(nullptr);
    struct tm tm;
    localtime_s(&tm, &now);
    fprintf(sFile, "[%04d-%02d-%02d %02d:%02d:%02d] [%s] ",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec,
            LevelToStr(level));

    // Message
    va_list args;
    va_start(args, fmt);
    vfprintf(sFile, fmt, args);
    va_end(args);

    fprintf(sFile, "\n");
    fflush(sFile); // Crash resilience
}

} // namespace Log
