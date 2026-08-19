#pragma once

// Dev harness screenshot (/screenshot). Captures a display via DXGI Desktop Duplication and
// writes a PNG.
//
// Desktop Duplication and not a GDI BitBlt / GDI+ CopyFromScreen: those read the GDI surface,
// which comes back BLACK for a flip-model swapchain — the presentation path every modern
// fullscreen D3D11 game uses. Duplication reads the SCANNED-OUT desktop image instead, so it
// captures what is actually on the monitor.
//
// Runs entirely on the accept thread: the capture creates its own D3D11 device and never touches
// the game's renderer, so it needs neither the game thread nor a present hook. In-process rather
// than shelling out to PowerShell — the sibling CyberAccess harness found an external
// CopyFromScreen script gets AMSI/AV-blocked, on top of returning black anyway.
//
// This TU owns the Direct3D/DXGI/GDI+ includes; keep them out of the other harness sources.

#ifdef X2ACCESS_DEVSERVER

#include <string>

namespace DevServer {

/// Capture one display output to a PNG at `outPath` (UTF-8). Output selection:
///   outputOverride >= 0    -> that DXGI output index
///   else preferredMonitor  -> the output on that monitor (an HMONITOR as void*)
///   else                   -> output 0
/// On success returns an empty string and fills outW/outH; on failure returns a human-readable
/// error and leaves the size at 0.
std::string CaptureScreenToPng(const std::string& outPath, void* preferredMonitor,
    int outputOverride, int& outW, int& outH);

} // namespace DevServer

#endif // X2ACCESS_DEVSERVER
