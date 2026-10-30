#include "input_arbiter.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <mutex>
#include <vector>

namespace InputArbiter {

namespace {

std::vector<Claim> sClaims; // append-only; queue entries carry indices into it
std::vector<Observer> sObservers; // append-only; passive, never consume (see input_arbiter.h)

HWND sHwnd = nullptr;
WNDPROC sOriginalProc = nullptr;
DWORD sOwnerThreadId = 0;

// Release-swallow latch (spec P4): claim index that swallowed the key's down, or -1. A swallowed
// down's repeats and release are swallowed and routed to the SAME claim regardless of what the
// claim set says by then — the game must never see an unpaired key-up, and the handler must never
// see a release it got no press for.
std::array<int, 256> sDownClaim = [] {
    std::array<int, 256> a{};
    a.fill(-1);
    return a;
}();

// A swallowed key-down's WM_CHAR (posted by TranslateMessage before our swallow decision ran)
// must not reach the game either. True from a swallowed char-producing down until the next
// non-WM_CHAR keyboard message.
bool sSwallowChars = false;

// Bounded drop-oldest queue (spec P4): never replay seconds of stale input as phantom presses.
constexpr size_t kQueueSize = 64;
std::array<KeyEvent, kQueueSize> sQueue;
size_t sQueueHead = 0; // next slot to read
size_t sQueueCount = 0;
std::mutex sQueueMutex;

// Claim indices travel beside the events, same ring positions.
std::array<int, kQueueSize> sQueueClaim;

uint64_t sSwallowed = 0; // total chords swallowed, for the status verb

void Enqueue(const KeyEvent& e, int claimIndex)
{
    std::lock_guard<std::mutex> lock(sQueueMutex);
    if (sQueueCount == kQueueSize) {
        sQueueHead = (sQueueHead + 1) % kQueueSize; // drop the oldest
        sQueueCount--;
    }
    size_t tail = (sQueueHead + sQueueCount) % kQueueSize;
    sQueue[tail] = e;
    sQueueClaim[tail] = claimIndex;
    sQueueCount++;
}

uint8_t CurrentMods()
{
    uint8_t mods = ModNone;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= ModCtrl;
    if (GetKeyState(VK_SHIFT) & 0x8000)   mods |= ModShift;
    if (GetKeyState(VK_MENU) & 0x8000)    mods |= ModAlt;
    return mods;
}

// Passive fan-out, called from the window procedure. A throwing observer must not take the
// message path down with it, and must not be able to change what the game receives either —
// hence the swallow-all catch and the void return.
void NotifyKey(const KeyEvent& e)
{
    for (const Observer& o : sObservers) {
        if (!o.onKey)
            continue;
        try {
            o.onKey(e);
        } catch (...) {
        }
    }
}

void NotifyChar(wchar_t ch)
{
    for (const Observer& o : sObservers) {
        if (!o.onChar)
            continue;
        try {
            o.onChar(ch);
        } catch (...) {
        }
    }
}

// WM_KEYDOWN reports the GENERIC VK for a modifier (VK_SHIFT, never VK_LSHIFT) — the side lives
// in lParam's scan code and extended bit. Claims name a side, because the two are not
// interchangeable to the game: only LeftShift carries a binding in [XComGame.XComTacticalInput].
uint8_t ResolveModifierSide(uint8_t vk, LPARAM lParam)
{
    UINT scan = static_cast<UINT>((lParam >> 16) & 0xFF);
    bool extended = ((lParam >> 24) & 0x01) != 0;
    switch (vk) {
    case VK_SHIFT: {
        // MAPVK_VSC_TO_VK_EX is the only reliable left/right split for Shift; Shift is not an
        // extended key, so the extended bit says nothing about it.
        UINT side = MapVirtualKeyW(scan, MAPVK_VSC_TO_VK_EX);
        return side ? static_cast<uint8_t>(side) : vk;
    }
    case VK_CONTROL: return extended ? VK_RCONTROL : VK_LCONTROL;
    case VK_MENU:    return extended ? VK_RMENU : VK_LMENU;
    default:         return vk;
    }
}

bool IsModifierKey(uint8_t vk)
{
    switch (vk) {
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_MENU: case VK_LMENU: case VK_RMENU:
        return true;
    default:
        return false;
    }
}

// The modifier bit a modifier KEY contributes to CurrentMods(). Used to strip a modifier's own
// bit before matching a claim on that key: GetKeyState already reports Shift down while the
// window procedure is handling Shift's own WM_KEYDOWN, so an unstripped match would need the
// claim to name ModShift — a chord of the key with itself.
uint8_t OwnModifierBit(uint8_t vk)
{
    switch (vk) {
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:     return ModShift;
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return ModCtrl;
    case VK_MENU: case VK_LMENU: case VK_RMENU:        return ModAlt;
    default:                                            return 0;
    }
}

int FindClaim(uint8_t vk, uint8_t mods)
{
    for (size_t i = 0; i < sClaims.size(); i++) {
        const Claim& c = sClaims[i];
        if (c.vk != vk || c.mods != mods)
            continue;
        // No host callback may leak a throw into the frame path (spec §7.9); a throwing
        // predicate reads as "not active".
        try {
            if (c.activeWhen && !c.activeWhen())
                continue;
        } catch (...) {
            continue;
        }
        return static_cast<int>(i);
    }
    return -1;
}

// Liveness proof for the self-healing check below: bumped on every message we receive.
uint64_t sProcMessages = 0;

LRESULT CALLBACK ArbiterProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    sProcMessages++;
    switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        uint8_t vk = static_cast<uint8_t>(wParam & 0xFF);
        if (IsModifierKey(vk)) {
            // Almost always: a modifier is not a chord, it only decorates one. The exception is
            // an explicit side-named claim (see Claim's doc comment) — that swallows the key so
            // the engine never counts it as pressed at all. Observers stay unnotified either
            // way, and sSwallowChars is left alone: a modifier produces no WM_CHAR.
            uint8_t side = ResolveModifierSide(vk, lParam);
            bool repeatMod = (lParam & 0x40000000) != 0;
            uint8_t bare = static_cast<uint8_t>(CurrentMods() & ~OwnModifierBit(side));
            int modClaim = repeatMod ? sDownClaim[side] : FindClaim(side, bare);
            if (modClaim < 0) {
                if (!repeatMod)
                    sDownClaim[side] = -1;
                break;
            }
            if (sClaims[modClaim].passThrough) {
                // Never latched: the game sees this down AND its up, and the repeats (which
                // look the latch up and find nothing) go straight past too. The handler hears
                // the press and nothing else. repeatMod is necessarily false here.
                sDownClaim[side] = -1;
                LOG_DEBUG("InputArbiter: passed modifier vk 0x%02X through -> claim \"%s\"",
                          side, sClaims[modClaim].description);
                if (sClaims[modClaim].onKey)
                    Enqueue(KeyEvent{side, bare, /*down*/ true, false}, modClaim);
                break;
            }
            sDownClaim[side] = modClaim;
            sSwallowed++;
            if (!repeatMod)
                LOG_DEBUG("InputArbiter: swallowed modifier vk 0x%02X -> claim \"%s\"", side,
                          sClaims[modClaim].description);
            // A handler-less claim is a PURE SWALLOW and needs no dispatch — and must not get
            // one: a modifier held down auto-repeats, and queueing tens of no-op events per
            // second would evict real presses from the bounded ring during a frame hitch.
            if (sClaims[modClaim].onKey)
                Enqueue(KeyEvent{side, bare, /*down*/ true, repeatMod}, modClaim);
            return 0;
        }
        sSwallowChars = false;

        bool repeat = (lParam & 0x40000000) != 0;
        uint8_t mods = CurrentMods();
        NotifyKey(KeyEvent{vk, mods, /*down*/ true, repeat});

        // Claims are decided at the PRESS; repeats follow the down's fate in BOTH directions. A
        // claim going active mid-hold must not start eating repeats of a down the game saw — the
        // release latch would then eat the key-up too and leave the game with a stuck key. The
        // mirror case (claim going inactive mid-hold) keeps swallowing via the latch.
        int claim = repeat ? sDownClaim[vk] : FindClaim(vk, mods);
        if (claim < 0) {
            sDownClaim[vk] = -1; // an unswallowed down clears any stale latch (lost focus mid-hold)
            // Keys we deliberately leave to the game (Escape above all) drive the screen changes
            // we then have to react to, so without this the log has no t0 to measure that
            // reaction against. Presses only — repeats would flood it while a key is held.
            if (!repeat)
                LOG_DEBUG("InputArbiter: passed vk 0x%02X mods 0x%X to the game", vk, mods);
            break;
        }

        if (sClaims[claim].passThrough) {
            // Same rule as the modifier branch: no latch, so the game gets the up and the
            // repeats; the handler gets the press. WM_CHAR stays the game's too.
            sDownClaim[vk] = -1;
            LOG_DEBUG("InputArbiter: passed vk 0x%02X mods 0x%X through -> claim \"%s\"",
                      vk, mods, sClaims[claim].description);
            Enqueue(KeyEvent{vk, mods, /*down*/ true, false}, claim);
            break;
        }

        sDownClaim[vk] = claim;
        sSwallowChars = true;
        sSwallowed++;
        if (!repeat)
            LOG_DEBUG("InputArbiter: swallowed vk 0x%02X mods 0x%X -> claim \"%s\"",
                      vk, mods, sClaims[claim].description);
        Enqueue(KeyEvent{vk, mods, /*down*/ true, repeat}, claim);
        return 0;
    }

    case WM_KEYUP:
    case WM_SYSKEYUP: {
        uint8_t vk = static_cast<uint8_t>(wParam & 0xFF);
        if (IsModifierKey(vk)) {
            // Same latch rule as every other key: a swallowed down owns its up, whatever the
            // claim set says by now. Releasing a modifier the game never saw pressed would be
            // an unpaired key-up — the one thing the arbiter must never emit.
            uint8_t side = ResolveModifierSide(vk, lParam);
            int modClaim = sDownClaim[side];
            if (modClaim < 0)
                break;
            sDownClaim[side] = -1;
            if (sClaims[modClaim].onKey)
                Enqueue(KeyEvent{side,
                            static_cast<uint8_t>(CurrentMods() & ~OwnModifierBit(side)),
                            /*down*/ false, false},
                    modClaim);
            return 0;
        }
        sSwallowChars = false;
        int claim = sDownClaim[vk];
        if (claim < 0)
            break; // the game saw the down — it gets the up, claimed or not
        sDownClaim[vk] = -1;
        Enqueue(KeyEvent{vk, CurrentMods(), /*down*/ false, false}, claim);
        return 0;
    }

    case WM_CHAR:
    case WM_SYSCHAR:
        if (sSwallowChars)
            return 0;
        NotifyChar(static_cast<wchar_t>(wParam));
        break;

    case WM_KILLFOCUS:
        // Releases will go to another window; drop the latches so a future unclaimed press
        // doesn't have its key-up eaten.
        for (int& c : sDownClaim)
            c = -1;
        sSwallowChars = false;
        break;

    default:
        break;
    }

    return CallWindowProcW(sOriginalProc, hwnd, msg, wParam, lParam);
}

BOOL CALLBACK FindGameWindow(HWND hwnd, LPARAM lParam)
{
    DWORD pid = 0;
    DWORD thread = GetWindowThreadProcessId(hwnd, &pid);
    // The game window is the visible top-level window owned by THIS thread — OnTick runs on the
    // game thread via ProcessEvent, and UE3 creates and pumps its window on that same thread.
    if (pid != GetCurrentProcessId() || thread != GetCurrentThreadId())
        return TRUE;
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr)
        return TRUE;
    *reinterpret_cast<HWND*>(lParam) = hwnd;
    return FALSE;
}

void TryInstall()
{
    HWND hwnd = nullptr;
    EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&hwnd));
    if (!hwnd)
        return;

    WNDPROC original = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ArbiterProc)));
    if (!original) {
        LOG_ERROR("InputArbiter: SetWindowLongPtr failed (error %lu)", GetLastError());
        return;
    }

    sHwnd = hwnd;
    sOriginalProc = original;
    sOwnerThreadId = GetCurrentThreadId();

    char cls[128] = {};
    GetClassNameA(hwnd, cls, sizeof(cls));
    char title[128] = {};
    GetWindowTextA(hwnd, title, sizeof(title));
    LOG_INFO("InputArbiter: subclassed game window 0x%p (class \"%s\", title \"%s\", thread %lu)",
             hwnd, cls, title, sOwnerThreadId);
}

} // namespace

void RegisterClaim(Claim claim)
{
    LOG_INFO("InputArbiter: claim registered — vk 0x%02X mods 0x%X (%s)%s",
             claim.vk, claim.mods, claim.description ? claim.description : "",
             claim.passThrough ? " [pass-through]" : "");
    sClaims.push_back(std::move(claim));
}

void RegisterObserver(Observer observer)
{
    LOG_INFO("InputArbiter: observer registered (%s)",
             observer.description ? observer.description : "");
    sObservers.push_back(std::move(observer));
}

bool IsClaimed(uint8_t vk, uint8_t mods)
{
    return FindClaim(vk, mods) >= 0;
}

bool InjectSynthetic(const KeyEvent& e)
{
    int claim = FindClaim(e.vk, e.mods);
    if (claim < 0)
        return false;
    Enqueue(e, claim);
    return true;
}

void OnTick()
{
    // Install (or re-install after a window recreation) — time-throttled so the check rate is
    // independent of how often the pump calls us.
    static uint64_t sLastCheckMs = 0;
    uint64_t now = GetTickCount64();
    if (now - sLastCheckMs >= 500) {
        sLastCheckMs = now;
        if (sHwnd && !IsWindow(sHwnd)) {
            LOG_WARN("InputArbiter: game window went away — will re-subclass");
            sHwnd = nullptr;
            sOriginalProc = nullptr;
        }
        // The engine RESETS the window procedure after us (UE3 installs its real viewport proc
        // shortly after startup, and again on fullscreen switches) — the window survives but our
        // subclass is silently gone. Distinguish "replaced" from "someone chained on top of us"
        // by LIVENESS: if the current proc isn't ours AND no message has reached ours across two
        // consecutive checks (~1 s), we are out of the chain — re-install, chaining to whatever
        // is current now. A subclasser on top of us still forwards messages our way, keeps the
        // counter moving, and is left alone (re-installing then would double-process every key).
        if (sHwnd && sOriginalProc) {
            static uint64_t sLastSeenMessages = 0;
            static int sBypassChecks = 0;
            WNDPROC current = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(sHwnd, GWLP_WNDPROC));
            if (current != ArbiterProc && sProcMessages == sLastSeenMessages) {
                if (++sBypassChecks >= 2) {
                    LOG_WARN("InputArbiter: window procedure replaced (now 0x%p), ours unreachable — re-subclassing",
                             reinterpret_cast<void*>(current));
                    sBypassChecks = 0;
                    sHwnd = nullptr;
                    sOriginalProc = nullptr;
                }
            } else {
                sBypassChecks = 0;
            }
            sLastSeenMessages = sProcMessages;
        }
        if (!sHwnd) {
            for (int& c : sDownClaim)
                c = -1;
            TryInstall();
        }
    }

    // Drain. Handlers run here on the game thread, where P2 (ProcessEvent) calls are legal.
    while (true) {
        KeyEvent e;
        int claim;
        {
            std::lock_guard<std::mutex> lock(sQueueMutex);
            if (sQueueCount == 0)
                break;
            e = sQueue[sQueueHead];
            claim = sQueueClaim[sQueueHead];
            sQueueHead = (sQueueHead + 1) % kQueueSize;
            sQueueCount--;
        }
        if (claim >= 0 && claim < static_cast<int>(sClaims.size()) && sClaims[claim].onKey) {
            try {
                sClaims[claim].onKey(e);
            } catch (const std::exception& ex) {
                LOG_ERROR("InputArbiter: handler threw (%s): %s",
                          sClaims[claim].description, ex.what());
            } catch (...) {
                LOG_ERROR("InputArbiter: handler threw (%s)", sClaims[claim].description);
            }
        }
    }
}

void Shutdown()
{
    if (sHwnd && IsWindow(sHwnd) && sOriginalProc) {
        // Restore only if we are still the current procedure — someone may have subclassed after
        // us, and yanking ourselves from under them would orphan their chain.
        WNDPROC current = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(sHwnd, GWLP_WNDPROC));
        if (current == ArbiterProc) {
            SetWindowLongPtrW(sHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(sOriginalProc));
            LOG_INFO("InputArbiter: original window procedure restored");
        } else {
            LOG_WARN("InputArbiter: not the current window procedure at shutdown — leaving in place");
        }
    }
    sHwnd = nullptr;
    sOriginalProc = nullptr;
}

bool IsInstalled()
{
    return sHwnd != nullptr;
}

bool IsGameForeground()
{
    return sHwnd && GetForegroundWindow() == sHwnd;
}

} // namespace InputArbiter
