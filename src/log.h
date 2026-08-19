#pragma once
#include <string>
#include <vector>

namespace Log {

enum class Level { Debug, Info, Warn, Error };

// Initialize logger. logDir is the directory to write X2Access.log into.
bool Init(const std::string& logDir);
void Shutdown();

void Write(Level level, const char* fmt, ...);

#ifdef X2ACCESS_DEVSERVER
/// The tail of the log kept in memory, for the dev harness's /log endpoint. `outBase` receives
/// the global line index of element 0, so a caller can page with a cursor instead of re-reading
/// the whole window (the file on disk has no such handle — tailing it cannot tell you whether
/// you already saw a line). Any thread; the ring only exists in dev builds.
std::vector<std::string> Recent(long long& outBase);
#endif

} // namespace Log

#define LOG_DEBUG(...) Log::Write(Log::Level::Debug, __VA_ARGS__)
#define LOG_INFO(...)  Log::Write(Log::Level::Info,  __VA_ARGS__)
#define LOG_WARN(...)  Log::Write(Log::Level::Warn,  __VA_ARGS__)
#define LOG_ERROR(...) Log::Write(Log::Level::Error, __VA_ARGS__)
