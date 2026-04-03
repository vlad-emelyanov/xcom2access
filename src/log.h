#pragma once
#include <string>

namespace Log {

enum class Level { Debug, Info, Warn, Error };

// Initialize logger. logDir is the directory to write X2Access.log into.
bool Init(const std::string& logDir);
void Shutdown();

void Write(Level level, const char* fmt, ...);

} // namespace Log

#define LOG_DEBUG(...) Log::Write(Log::Level::Debug, __VA_ARGS__)
#define LOG_INFO(...)  Log::Write(Log::Level::Info,  __VA_ARGS__)
#define LOG_WARN(...)  Log::Write(Log::Level::Warn,  __VA_ARGS__)
#define LOG_ERROR(...) Log::Write(Log::Level::Error, __VA_ARGS__)
