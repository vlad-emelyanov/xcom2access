#pragma once
#include <string>

namespace TTS {

bool Init();
void Shutdown();

// Core speech
void Speak(const std::string& text, bool interrupt = false);
void SpeakInterrupt(const std::string& text);
void Silence();

// Queued speech for multi-part announcements
void QueueSpeak(const std::string& text);
void FlushQueue();

// Toggle
void SetEnabled(bool enabled);
bool IsEnabled();

// Status
bool IsAvailable();
std::string GetEngineName();
std::string GetEngineInfo();

} // namespace TTS
