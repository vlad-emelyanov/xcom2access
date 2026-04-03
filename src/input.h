#pragma once

namespace Input {

// Start the keyboard polling thread.
void Init();

// Stop the polling thread and clean up.
void Shutdown();

} // namespace Input
