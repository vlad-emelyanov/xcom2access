#include "no_mouse.h"

#include "log.h"

#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

#include <atomic>
#include <cstring>

namespace NoMouse {
namespace {

// Written out here rather than taken from dxguid.lib, so the build links nothing new.
constexpr GUID kSysMouse = { 0x6F1D2B60, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
constexpr GUID kDirectInput8W = { 0xBF798031, 0x483A, 0x4DA2, { 0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00 } };
constexpr GUID kDirectInputDevice8W = { 0x54D41081, 0xDC15, 0x4833, { 0xA4, 0x1B, 0x74, 0x8F, 0x73, 0xA3, 0x81, 0x79 } };
constexpr int kCreateDeviceSlot = 3; // IUnknown is 0-2, CreateDevice is the first IDirectInput8 method

using CreateFn = HRESULT(STDMETHODCALLTYPE*)(void*, REFGUID, LPDIRECTINPUTDEVICE8W*, LPUNKNOWN);
using Create8Fn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

Create8Fn sRealCreate8 = nullptr;
void** sIatSlot = nullptr;
std::atomic<CreateFn> sRealCreateDevice{ nullptr };
std::atomic<void**> sPatchedVtable{ nullptr };
std::atomic<bool> sStubLogged{ false };

// Deriving from the header's interface makes a missing or misspelled method a compile error (the
// interface is pure virtual), so the 32-entry table cannot be hand-miscounted.
class StubDevice final : public IDirectInputDevice8W {
public:
    STDMETHOD(QueryInterface)(REFIID riid, LPVOID* ppv) override
    {
        if (!ppv)
            return E_POINTER;
        *ppv = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, kDirectInputDevice8W)) {
            *ppv = this;
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() override { return 1; }
    STDMETHOD_(ULONG, Release)() override { return 1; }
    STDMETHOD(GetCapabilities)(LPDIDEVCAPS) override { return DI_OK; }
    STDMETHOD(EnumObjects)(LPDIENUMDEVICEOBJECTSCALLBACKW, LPVOID, DWORD) override { return DI_OK; }
    STDMETHOD(GetProperty)(REFGUID, LPDIPROPHEADER) override { return DI_OK; }
    STDMETHOD(SetProperty)(REFGUID, LPCDIPROPHEADER) override { return DI_OK; }
    STDMETHOD(Acquire)() override { return DI_OK; }
    STDMETHOD(Unacquire)() override { return DI_OK; }
    STDMETHOD(GetDeviceState)(DWORD cb, LPVOID data) override
    {
        if (data && cb)
            std::memset(data, 0, cb);
        return DI_OK;
    }
    STDMETHOD(GetDeviceData)(DWORD, LPDIDEVICEOBJECTDATA, LPDWORD inOut, DWORD) override
    {
        if (inOut)
            *inOut = 0;
        return DI_OK;
    }
    STDMETHOD(SetDataFormat)(LPCDIDATAFORMAT) override { return DI_OK; }
    STDMETHOD(SetEventNotification)(HANDLE) override { return DI_OK; }
    STDMETHOD(SetCooperativeLevel)(HWND, DWORD) override { return DI_OK; }
    STDMETHOD(GetObjectInfo)(LPDIDEVICEOBJECTINSTANCEW, DWORD, DWORD) override { return DI_OK; }
    STDMETHOD(GetDeviceInfo)(LPDIDEVICEINSTANCEW) override { return DI_OK; }
    STDMETHOD(RunControlPanel)(HWND, DWORD) override { return DI_OK; }
    STDMETHOD(Initialize)(HINSTANCE, DWORD, REFGUID) override { return DI_OK; }
    STDMETHOD(CreateEffect)(REFGUID, LPCDIEFFECT, LPDIRECTINPUTEFFECT*, LPUNKNOWN) override { return DI_OK; }
    STDMETHOD(EnumEffects)(LPDIENUMEFFECTSCALLBACKW, LPVOID, DWORD) override { return DI_OK; }
    STDMETHOD(GetEffectInfo)(LPDIEFFECTINFOW, REFGUID) override { return DI_OK; }
    STDMETHOD(GetForceFeedbackState)(LPDWORD) override { return DI_OK; }
    STDMETHOD(SendForceFeedbackCommand)(DWORD) override { return DI_OK; }
    STDMETHOD(EnumCreatedEffectObjects)(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK, LPVOID, DWORD) override { return DI_OK; }
    STDMETHOD(Escape)(LPDIEFFESCAPE) override { return DI_OK; }
    STDMETHOD(Poll)() override { return DI_OK; }
    STDMETHOD(SendDeviceData)(DWORD, LPCDIDEVICEOBJECTDATA, LPDWORD, DWORD) override { return DI_OK; }
    STDMETHOD(EnumEffectsInFile)(LPCWSTR, LPDIENUMEFFECTSINFILECALLBACK, LPVOID, DWORD) override { return DI_OK; }
    STDMETHOD(WriteEffectToFile)(LPCWSTR, DWORD, LPDIFILEEFFECT, DWORD) override { return DI_OK; }
    STDMETHOD(BuildActionMap)(LPDIACTIONFORMATW, LPCWSTR, DWORD) override { return DI_OK; }
    STDMETHOD(SetActionMap)(LPDIACTIONFORMATW, LPCWSTR, DWORD) override { return DI_OK; }
    STDMETHOD(GetImageInfo)(LPDIDEVICEIMAGEINFOHEADERW) override { return DI_OK; }
};

StubDevice sStub;

HRESULT STDMETHODCALLTYPE WrappedCreateDevice(void* self, REFGUID guid, LPDIRECTINPUTDEVICE8W* out, LPUNKNOWN outer)
{
    const HRESULT hr = sRealCreateDevice.load(std::memory_order_acquire)(self, guid, out, outer);
    if (FAILED(hr) && out && IsEqualGUID(guid, kSysMouse)) {
        *out = &sStub;
        if (!sStubLogged.exchange(true))
            LOG_INFO("NoMouse: no mouse device, stub in place");
        return DI_OK;
    }
    return hr;
}

void PatchVtable(void* iface)
{
    void** vtable = *reinterpret_cast<void***>(iface);
    if (sPatchedVtable.load(std::memory_order_acquire) == vtable)
        return;
    void** slot = &vtable[kCreateDeviceSlot];
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
        LOG_ERROR("NoMouse: VirtualProtect failed on the CreateDevice slot (%lu)", GetLastError());
        return;
    }
    if (*slot != reinterpret_cast<void*>(&WrappedCreateDevice)) {
        sRealCreateDevice.store(reinterpret_cast<CreateFn>(*slot), std::memory_order_release);
        *slot = reinterpret_cast<void*>(&WrappedCreateDevice);
    }
    VirtualProtect(slot, sizeof(void*), old, &old);
    sPatchedVtable.store(vtable, std::memory_order_release);
}

HRESULT WINAPI WrappedCreate8(HINSTANCE inst, DWORD ver, REFIID riid, LPVOID* out, LPUNKNOWN outer)
{
    const HRESULT hr = sRealCreate8(inst, ver, riid, out, outer);
    // The game asks for the wide interface (IID BF798031, read off its call site); the narrow one has a
    // different CreateDevice out-type, so it is left alone.
    if (SUCCEEDED(hr) && out && *out && IsEqualIID(riid, kDirectInput8W))
        PatchVtable(*out);
    return hr;
}

} // namespace

void Install()
{
    if (sIatSlot)
        return;
    auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) {
        LOG_WARN("NoMouse: the exe has no import table, nothing installed");
        return;
    }
    auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    for (; desc->Name; ++desc) {
        const char* dll = reinterpret_cast<const char*>(base + desc->Name);
        if (_strnicmp(dll, "DINPUT8", 7) != 0)
            continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            auto* byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(byName->Name, "DirectInput8Create") != 0)
                continue;
            void** slot = reinterpret_cast<void**>(&slots->u1.Function);
            DWORD old = 0;
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
                LOG_ERROR("NoMouse: VirtualProtect failed on the DirectInput8Create import (%lu)", GetLastError());
                return;
            }
            sRealCreate8 = reinterpret_cast<Create8Fn>(*slot);
            sIatSlot = slot;
            InterlockedExchangePointer(slot, reinterpret_cast<void*>(&WrappedCreate8));
            VirtualProtect(slot, sizeof(void*), old, &old);
            LOG_INFO("NoMouse: wrapping DirectInput8Create from %s", dll);
            return;
        }
    }
    LOG_WARN("NoMouse: no DirectInput8Create import found, nothing installed");
}

void Shutdown()
{
    if (!sIatSlot || !sRealCreate8)
        return;
    DWORD old = 0;
    if (VirtualProtect(sIatSlot, sizeof(void*), PAGE_READWRITE, &old)) {
        InterlockedExchangePointer(sIatSlot, reinterpret_cast<void*>(sRealCreate8));
        VirtualProtect(sIatSlot, sizeof(void*), old, &old);
    }
}

} // namespace NoMouse
