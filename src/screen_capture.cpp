// See screen_capture.h for why this is Desktop Duplication and not a BitBlt.
#ifdef X2ACCESS_DEVSERVER

#include "screen_capture.h"

#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <gdiplus.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

namespace DevServer {

namespace {

using Microsoft::WRL::ComPtr;

// The PNG encoder's CLSID, via GDI+'s encoder registry (there is no constant for it).
bool GetPngEncoderClsid(CLSID& out)
{
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0)
        return false;
    std::vector<uint8_t> buffer(bytes);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(count, bytes, encoders) != Gdiplus::Ok)
        return false;
    for (UINT i = 0; i < count; i++) {
        if (wcscmp(encoders[i].MimeType, L"image/png") == 0) {
            out = encoders[i].Clsid;
            return true;
        }
    }
    return false;
}

std::wstring Widen(const std::string& utf8)
{
    if (utf8.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
        nullptr, 0);
    if (n <= 0)
        return {};
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

std::string SavePng(const D3D11_TEXTURE2D_DESC& desc, const D3D11_MAPPED_SUBRESOURCE& mapped,
    const std::string& outPath, int& outW, int& outH)
{
    ULONG_PTR gdiToken = 0;
    Gdiplus::GdiplusStartupInput startup;
    if (Gdiplus::GdiplusStartup(&gdiToken, &startup, nullptr) != Gdiplus::Ok)
        return "[error] GdiplusStartup failed";

    std::string result;
    {
        // Duplication surfaces are BGRA8; 32bppRGB drops the alpha so the PNG comes out opaque
        // rather than mysteriously transparent where the game rendered alpha.
        Gdiplus::Bitmap bitmap(static_cast<INT>(desc.Width), static_cast<INT>(desc.Height),
            static_cast<INT>(mapped.RowPitch), PixelFormat32bppRGB,
            static_cast<BYTE*>(mapped.pData));
        if (bitmap.GetLastStatus() != Gdiplus::Ok) {
            result = "[error] GDI+ bitmap construction failed";
        } else {
            CLSID pngClsid{};
            if (!GetPngEncoderClsid(pngClsid)) {
                result = "[error] PNG encoder not found";
            } else if (bitmap.Save(Widen(outPath).c_str(), &pngClsid, nullptr) != Gdiplus::Ok) {
                result = "[error] PNG save failed (is the path writable? " + outPath + ")";
            } else {
                outW = static_cast<int>(desc.Width);
                outH = static_cast<int>(desc.Height);
            }
        }
    }
    Gdiplus::GdiplusShutdown(gdiToken);
    return result;
}

} // namespace

std::string CaptureScreenToPng(const std::string& outPath, void* preferredMonitor,
    int outputOverride, int& outW, int& outH)
{
    outW = 0;
    outH = 0;

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
        &device, nullptr, &context);
    if (FAILED(hr))
        return "[error] D3D11CreateDevice failed (hr=" + std::to_string(hr) + ")";

    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(device.As(&dxgiDevice)))
        return "[error] IDXGIDevice query failed";
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter)))
        return "[error] GetAdapter failed";

    // Pick the output: an explicit index, else the one the game's window sits on, else the
    // first. DXGI_OUTPUT_DESC.Monitor is an HMONITOR, so the match is direct.
    const auto preferred = static_cast<HMONITOR>(preferredMonitor);
    ComPtr<IDXGIOutput> output;
    for (UINT i = 0;; i++) {
        ComPtr<IDXGIOutput> candidate;
        if (FAILED(adapter->EnumOutputs(i, &candidate)))
            break;
        if (outputOverride >= 0) {
            if (static_cast<int>(i) == outputOverride) {
                output = candidate;
                break;
            }
            continue;
        }
        if (!output)
            output = candidate; // fallback = first output
        if (preferred) {
            DXGI_OUTPUT_DESC desc{};
            if (SUCCEEDED(candidate->GetDesc(&desc)) && desc.Monitor == preferred) {
                output = candidate;
                break;
            }
        } else {
            break; // no preference and no override: the first output is the answer
        }
    }
    if (!output)
        return "[error] no matching display output (is the override index out of range?)";
    ComPtr<IDXGIOutput1> output1;
    if (FAILED(output.As(&output1)))
        return "[error] IDXGIOutput1 query failed";

    ComPtr<IDXGIOutputDuplication> duplication;
    hr = output1->DuplicateOutput(device.Get(), &duplication);
    if (FAILED(hr))
        return "[error] DuplicateOutput failed (hr=" + std::to_string(hr)
            + "). Exclusive fullscreen defeats Desktop Duplication — switch the game to "
              "windowed or borderless, or another capturer already holds this output.";

    // The first AcquireNextFrame after DuplicateOutput usually reports no accumulated frames
    // until the desktop next changes, so a genuinely static screen (a paused menu, which is
    // exactly what we want to photograph) needs retries rather than one shot.
    ComPtr<ID3D11Texture2D> frameTexture;
    for (int attempt = 0; attempt < 30 && !frameTexture; attempt++) {
        DXGI_OUTDUPL_FRAME_INFO info{};
        ComPtr<IDXGIResource> resource;
        hr = duplication->AcquireNextFrame(500, &info, &resource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT)
            continue;
        if (FAILED(hr))
            return "[error] AcquireNextFrame failed (hr=" + std::to_string(hr) + ")";
        resource.As(&frameTexture);
        if (!frameTexture) {
            duplication->ReleaseFrame();
            return "[error] the frame resource was not a texture";
        }
        if (info.LastPresentTime.QuadPart == 0 && info.AccumulatedFrames == 0 && attempt < 29) {
            frameTexture.Reset();
            duplication->ReleaseFrame();
        }
    }
    if (!frameTexture)
        return "[error] no frame acquired (the desktop never refreshed within the retry window)";

    D3D11_TEXTURE2D_DESC desc{};
    frameTexture->GetDesc(&desc);

    // A GPU texture cannot be mapped for CPU read — copy into a STAGING texture first.
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    hr = device->CreateTexture2D(&stagingDesc, nullptr, &staging);
    if (FAILED(hr)) {
        duplication->ReleaseFrame();
        return "[error] staging texture creation failed (hr=" + std::to_string(hr) + ")";
    }
    context->CopyResource(staging.Get(), frameTexture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        duplication->ReleaseFrame();
        return "[error] Map failed (hr=" + std::to_string(hr) + ")";
    }

    std::string result = SavePng(desc, mapped, outPath, outW, outH);

    context->Unmap(staging.Get(), 0);
    duplication->ReleaseFrame();
    return result;
}

} // namespace DevServer

#endif // X2ACCESS_DEVSERVER
