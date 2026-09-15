#pragma once
#include <string>

namespace TTS {

bool Init();
void Shutdown();

// Core speech. There is deliberately NO interrupt flag anywhere in this API: every line is handed
// to the backend as queued text, and the SCREEN READER decides whether it cuts in — the player's
// own reader settings govern that, not us. The Narrator orders lines; it never cuts them.
void Speak(const std::string& text);
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
