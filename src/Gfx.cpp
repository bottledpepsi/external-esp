#include "Gfx.h"
#include "Logger.h"
#include <algorithm>
#include <cfloat>

namespace
{
    std::wstring ToWide(const std::string& s)
    {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring w(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
        return w;
    }

    D2D1_COLOR_F ToD2D(Col c) { return D2D1_COLOR_F{c.r, c.g, c.b, c.a}; }
    D2D1_RECT_F RectF(float x, float y, float w, float h) { return D2D1_RECT_F{x, y, x + w, y + h}; }

    struct FontSpec
    {
        float size;
        DWRITE_FONT_WEIGHT weight;
    };

    const FontSpec kFonts[] = {
        {10.f, DWRITE_FONT_WEIGHT_SEMI_BOLD}, // Name10
        {13.f, DWRITE_FONT_WEIGHT_SEMI_BOLD}, // Name13
        {17.f, DWRITE_FONT_WEIGHT_SEMI_BOLD}, // Name17
        {12.f, DWRITE_FONT_WEIGHT_NORMAL}, // Ui
        {11.f, DWRITE_FONT_WEIGHT_NORMAL}, // UiSmall
        {12.f, DWRITE_FONT_WEIGHT_BOLD}, // UiBold
        {10.f, DWRITE_FONT_WEIGHT_NORMAL}, // Hint
    };

    // Entries per text-cache map before it is flushed (labels repeat frame to frame, so hits dominate).
    constexpr size_t kLayoutCacheMax = 384;
    constexpr size_t kWidthCacheMax = 768;

    // First installed family from a preference list (Segoe UI is missing on some stripped-down Windows
    // installs and under Wine). Returns the last candidate if none is found.
    std::wstring PickFamily(IDWriteFactory* dw)
    {
        static const wchar_t* const kCandidates[] = {
            L"Segoe UI", L"Tahoma", L"Arial", L"Verdana", L"DejaVu Sans", L"Liberation Sans"
        };
        ComPtr<IDWriteFontCollection> coll;
        if (FAILED(dw->GetSystemFontCollection(coll.put(), FALSE))) return kCandidates[0];
        for (const wchar_t* name : kCandidates)
        {
            UINT32 idx = 0;
            BOOL exists = FALSE;
            if (SUCCEEDED(coll->FindFamilyName(name, &idx, &exists)) && exists) return name;
        }
        return kCandidates[0];
    }

#ifdef ESP_GPU_GFX
    using DCompCreateFn = HRESULT (WINAPI*)(IDXGIDevice*, REFIID, void**);
    using D3DCreateFn = HRESULT (WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*,
                                          UINT, UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
    constexpr UINT kSwapFlags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
#endif
} // namespace

Gfx::~Gfx()
{
#ifdef ESP_GPU_GFX
    ReleaseGpu();
#endif
    FreeSurface();
}

ID2D1Factory* Gfx::Fac() const
{
#ifdef ESP_GPU_GFX
    if (factory1_) return factory1_.get();
#endif
    return factory_.get();
}

bool Gfx::Init(bool allowGpu)
{
    HRESULT hr = E_FAIL;
#ifdef ESP_GPU_GFX
    if (allowGpu)
    {
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), nullptr,
                               reinterpret_cast<void**>(factory1_.put()));
        if (FAILED(hr)) LOG_WARN("ID2D1Factory1 unavailable (0x%08lX) - using the legacy renderer.",
                                 static_cast<unsigned long>(hr));
    }
#else
    (void)allowGpu;
#endif
    if (!Fac())
    {
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr,
                               reinterpret_cast<void**>(factory_.put()));
        if (FAILED(hr))
        {
            LOG_ERROR("D2D1CreateFactory failed (0x%08lX)", static_cast<unsigned long>(hr));
            return false;
        }
    }
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(dw_.put()));
    if (FAILED(hr))
    {
        LOG_ERROR("DWriteCreateFactory failed (0x%08lX)", static_cast<unsigned long>(hr));
        return false;
    }

    const std::wstring family = PickFamily(dw_.get());
    LOG_INFO("UI font family: %ls", family.c_str());
    for (int f = 0; f < kFontCount; ++f)
    {
        for (int a = 0; a < 3; ++a)
        {
            hr = dw_->CreateTextFormat(family.c_str(), nullptr, kFonts[f].weight, DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL, kFonts[f].size, L"en-us", fmt_[f][a].put());
            if (FAILED(hr))
            {
                LOG_ERROR("CreateTextFormat failed (0x%08lX)", static_cast<unsigned long>(hr));
                return false;
            }
            fmt_[f][a]->SetTextAlignment(a == 0
                                             ? DWRITE_TEXT_ALIGNMENT_LEADING
                                             : a == 1
                                             ? DWRITE_TEXT_ALIGNMENT_CENTER
                                             : DWRITE_TEXT_ALIGNMENT_TRAILING);
            fmt_[f][a]->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }

#ifdef ESP_GPU_GFX
    if (factory1_)
    {
        gpu_ = InitGpu();
        if (!gpu_)
        {
            LOG_WARN("GPU overlay backend unavailable - using the legacy GDI/layered-window renderer.");
            ReleaseGpu();
        }
    }
#endif
    if (gpu_) return true;
    return CreateRenderTarget();
}

bool Gfx::ForceLegacy()
{
#ifdef ESP_GPU_GFX
    ReleaseGpu();
#endif
    gpu_ = false;
    w_ = h_ = 0;
    return CreateRenderTarget();
}

// ------------------------------------------------------------------------------------------ device loss (public part)

void Gfx::InvalidatePlacement()
{
#ifdef ESP_GPU_GFX
    winX_ = -32768; winY_ = -32768; winW_ = -1; winH_ = -1;
#endif
}

bool Gfx::WantsLegacy() const { return wantLegacy_; }
bool Gfx::DeviceLost() const { return deviceLost_; }

void Gfx::SimulateLoss(bool persistent)
{
    LOG_WARN("--simulate-device-loss: injecting a device loss at the next Present%s.", persistent ? " and failing every recovery (the GDI fallback should follow)" : "");
    injectLoss_ = true;
    if (persistent) failInit_ = 1 << 30;
}

std::string Gfx::LossSummary() const
{
    if (rec_.TotalLosses() == 0) return {};
    return "GPU device lost " + std::to_string(rec_.TotalLosses()) + "x, recovered " + std::to_string(rec_.Recoveries()) + "x" + (wantLegacy_ ? ", gave up -> legacy renderer" : "");
}

// ------------------------------------------------------------------------------------------ legacy backend

bool Gfx::CreateRenderTarget()
{
    bitmaps_.clear(); // device resources belong to the old render target
    brush_.reset();
    dcrt_.reset();
    rt_ = nullptr;
    D2D1_RENDER_TARGET_PROPERTIES p{};
    p.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
    p.pixelFormat = D2D1_PIXEL_FORMAT{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}; // per-pixel alpha
    p.dpiX = 96.f;
    p.dpiY = 96.f;
    p.usage = D2D1_RENDER_TARGET_USAGE_NONE;
    p.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
    HRESULT hr = Fac()->CreateDCRenderTarget(&p, dcrt_.put());
    if (FAILED(hr))
    {
        LOG_ERROR("CreateDCRenderTarget failed (0x%08lX)", static_cast<unsigned long>(hr));
        return false;
    }
    rt_ = dcrt_.get();
    // Grayscale AA: ClearType needs an opaque background, which a transparent overlay does not have.
    rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    D2D1_COLOR_F white{1, 1, 1, 1};
    hr = rt_->CreateSolidColorBrush(white, brush_.put());
    return SUCCEEDED(hr);
}

void Gfx::FreeSurface()
{
    if (memDc_)
    {
        if (oldBmp_) SelectObject(memDc_, oldBmp_);
        oldBmp_ = nullptr;
        if (dib_) DeleteObject(dib_);
        dib_ = nullptr;
        DeleteDC(memDc_);
        memDc_ = nullptr;
    }
}

bool Gfx::Resize(int w, int h)
{
    if (w <= 0 || h <= 0) return false;
#ifdef ESP_GPU_GFX
    if (gpu_) return ResizeGpu(w, h);
#endif
    if (w == w_ && h == h_ && dib_ && dcrt_) return true;
    FreeSurface();
    ScreenDC screen;
    memDc_ = CreateCompatibleDC(screen.get());
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // negative = top-down rows
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    dib_ = CreateDIBSection(screen.get(), &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib_ || !memDc_)
    {
        LOG_ERROR("CreateDIBSection failed (%lu)", static_cast<unsigned long>(GetLastError()));
        FreeSurface();
        return false;
    }
    oldBmp_ = static_cast<HBITMAP>(SelectObject(memDc_, dib_));
    w_ = w;
    h_ = h;
    if (!dcrt_ && !CreateRenderTarget()) return false;
    LOG_INFO("Overlay surface %dx%d (legacy)", w, h);
    return true;
}

// ------------------------------------------------------------------------------------------ GPU backend

#ifdef ESP_GPU_GFX

void Gfx::ReleaseGpu()
{
    // Order matters: everything that references the device goes first.
    if (dc_) dc_->SetTarget(nullptr);
    bitmaps_.clear();
    brush_.reset();
    rt_ = nullptr;
    target_.reset();
    dc_.reset();
    d2dDev_.reset();
    dcompVisual_.reset();
    dcompTarget_.reset();
    dcompDev_.reset();
    latencyWait_.reset();
    swap_.reset();
    dxgiFac_.reset();
    dxgiDev_.reset();
    d3d_.reset();
    layerOn_ = false;
    winX_ = winY_ = -32768;
    winW_ = winH_ = -1;
}

bool Gfx::InitGpu()
{
    if (failInit_ > 0)
    {
        LOG_WARN("InitGpu: failing on purpose (--simulate-device-loss=persistent).");
        return false;
    }
    if (!dcompCreate_)
    {
        HMODULE dcomp = LoadLibraryW(L"dcomp.dll");
        if (dcomp) dcompCreate_ = GetProcAddress(dcomp, "DCompositionCreateDevice");
    }
    if (!d3dCreate_)
    {
        HMODULE d3d = LoadLibraryW(L"d3d11.dll");
        if (d3d) d3dCreate_ = GetProcAddress(d3d, "D3D11CreateDevice");
    }
    if (!dcompCreate_ || !d3dCreate_)
    {
        LOG_WARN("dcomp.dll / d3d11.dll not available.");
        return false;
    }

    const auto create = reinterpret_cast<D3DCreateFn>(reinterpret_cast<void*>(d3dCreate_));
    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL got{};
    HRESULT hr = create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 4,
                        D3D11_SDK_VERSION, d3d_.put(), &got, nullptr);
    if (hr == E_INVALIDARG) // pre-11.1 runtime does not know FEATURE_LEVEL_11_1
        hr = create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels + 1, 3,
                    D3D11_SDK_VERSION, d3d_.put(), &got, nullptr);
    if (FAILED(hr))
    {
        LOG_WARN("D3D11CreateDevice failed (0x%08lX)", static_cast<unsigned long>(hr));
        return false;
    }
    hr = d3d_->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(dxgiDev_.put()));
    if (FAILED(hr)) return false;

    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDev_->GetAdapter(adapter.put()))) return false;
    if (FAILED(adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(dxgiFac_.put())))) return false;

    hr = factory1_->CreateDevice(dxgiDev_.get(), d2dDev_.put());
    if (FAILED(hr))
    {
        LOG_WARN("ID2D1Factory1::CreateDevice failed (0x%08lX)", static_cast<unsigned long>(hr));
        return false;
    }
    hr = d2dDev_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, dc_.put());
    if (FAILED(hr))
    {
        LOG_WARN("CreateDeviceContext failed (0x%08lX)", static_cast<unsigned long>(hr));
        return false;
    }
    rt_ = dc_.get();
    rt_->SetDpi(96.f, 96.f);
    rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE); // transparent target: no ClearType
    D2D1_COLOR_F white{1, 1, 1, 1};
    if (FAILED(rt_->CreateSolidColorBrush(white, brush_.put()))) return false;
    LOG_INFO("GPU renderer: D3D11 feature level 0x%X, D2D device context, flip-model swap chain + DirectComposition.",
             static_cast<unsigned>(got));
    return true;
}

bool Gfx::AttachDComp()
{
    if (!dxgiDev_ || !hwnd_) return false;
    const auto create = reinterpret_cast<DCompCreateFn>(reinterpret_cast<void*>(dcompCreate_));
    HRESULT hr = create(dxgiDev_.get(), __uuidof(IDCompositionDevice), reinterpret_cast<void**>(dcompDev_.put()));
    if (FAILED(hr))
    {
        LOG_WARN("DCompositionCreateDevice failed (0x%08lX)", static_cast<unsigned long>(hr));
        return false;
    }
    hr = dcompDev_->CreateTargetForHwnd(hwnd_, TRUE, dcompTarget_.put());
    if (FAILED(hr))
    {
        LOG_WARN("CreateTargetForHwnd failed (0x%08lX)", static_cast<unsigned long>(hr));
        return false;
    }
    if (FAILED(dcompDev_->CreateVisual(dcompVisual_.put()))) return false;
    if (swap_ && FAILED(dcompVisual_->SetContent(swap_.get()))) return false;
    if (FAILED(dcompTarget_->SetRoot(dcompVisual_.get()))) return false;
    return SUCCEEDED(dcompDev_->Commit());
}

static const char* HrName(HRESULT hr)
{
    switch (static_cast<unsigned long>(hr))
    {
    case 0x887A0005UL: return "DXGI_ERROR_DEVICE_REMOVED";
    case 0x887A0006UL: return "DXGI_ERROR_DEVICE_HUNG";
    case 0x887A0007UL: return "DXGI_ERROR_DEVICE_RESET";
    case 0x887A0020UL: return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";
    case 0x887A0001UL: return "DXGI_ERROR_INVALID_CALL";
    case 0x88990001UL: return "D2DERR_RECREATE_TARGET";
    default: return "";
    }
}

// The HRESULTs that mean "the device is gone" - plus INVALID_CALL when the device reports a removed reason (a dead device often answers that way).
bool Gfx::IsLoss(HRESULT hr) const
{
    switch (static_cast<unsigned long>(hr))
    {
    case 0x887A0005UL: case 0x887A0006UL: case 0x887A0007UL: case 0x887A0020UL: case 0x88990001UL: return true;
    default: break;
    }
    return d3d_ && d3d_->GetDeviceRemovedReason() != S_OK;
}

void Gfx::NoteLoss(const char* where, HRESULT hr)
{
    const HRESULT reason = d3d_ ? d3d_->GetDeviceRemovedReason() : S_OK;
    if (!rec_.Lost())
        LOG_WARN("GPU device lost in %s: 0x%08lX %s; removed reason 0x%08lX %s (loss #%d, %d recovered so far).", where, static_cast<unsigned long>(hr), HrName(hr),
                 static_cast<unsigned long>(reason), HrName(reason), rec_.TotalLosses() + 1, rec_.Recoveries());
    deviceLost_ = true;
    rec_.OnLoss(GpuRecovery::Clock::now());
}

bool Gfx::RecoverGpu()
{
    const auto t0 = GpuRecovery::Clock::now();
    LOG_WARN("GPU device lost - recreating the renderer (attempt after %d failed).", rec_.ConsecutiveFailures());
    ReleaseGpu();
    w_ = h_ = 0;
    const bool ok = InitGpu() && AttachDComp();
    const auto now = GpuRecovery::Clock::now();
    if (!ok)
    {
        rec_.OnRecoverFailed(now);
        LOG_WARN("GPU recovery failed after %lld ms (%d in a row).", static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count()), rec_.ConsecutiveFailures());
        return false;
    }
    deviceLost_ = false;
    rec_.OnRecovered(now);
    LOG_INFO("GPU renderer recovered in %lld ms (%d loss(es), %d recovery(ies)).", static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count()),
             rec_.TotalLosses(), rec_.Recoveries());
    return true;
}

bool Gfx::ResizeGpu(int w, int h)
{
    if (deviceLost_)
    {
        const GpuRecovery::Action act = rec_.Poll(GpuRecovery::Clock::now());
        if (act == GpuRecovery::Action::GiveUp)
        {
            if (!wantLegacy_)
            {
                wantLegacy_ = true;
                LOG_ERROR("GPU device could not be recovered (%d losses, %d failed attempts in a row) - switching to the legacy GDI renderer.", rec_.TotalLosses(), rec_.ConsecutiveFailures());
            }
            return false;
        }
        if (act != GpuRecovery::Action::TryRecover || !RecoverGpu()) return false;
    }
    else rec_.Poll(GpuRecovery::Clock::now());                // a long healthy spell clears the history
    if (w == w_ && h == h_ && swap_ && target_) return true;

    dc_->SetTarget(nullptr);
    target_.reset();

    if (!swap_)
    {
        DXGI_SWAP_CHAIN_DESC1 d{};
        d.Width = static_cast<UINT>(w);
        d.Height = static_cast<UINT>(h);
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        d.BufferCount = 2;
        d.Scaling = DXGI_SCALING_STRETCH; // the only scaling mode composition swap chains accept
        d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        d.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED; // per-pixel transparency
        d.Flags = kSwapFlags;
        ComPtr<IDXGISwapChain1> sc1;
        HRESULT hr = dxgiFac_->CreateSwapChainForComposition(dxgiDev_.get(), &d, nullptr, sc1.put());
        if (FAILED(hr))
        {
            LOG_ERROR("CreateSwapChainForComposition failed (0x%08lX)", static_cast<unsigned long>(hr));
            NoteLoss("CreateSwapChainForComposition", hr);
            return false;
        }
        hr = sc1->QueryInterface(__uuidof(IDXGISwapChain2), reinterpret_cast<void**>(swap_.put()));
        if (FAILED(hr))
        {
            LOG_ERROR("IDXGISwapChain2 unavailable (0x%08lX)", static_cast<unsigned long>(hr));
            NoteLoss("QueryInterface(IDXGISwapChain2)", hr);
            return false;
        }
        swap_->SetMaximumFrameLatency(1); // one frame in flight: lowest latency, the frames are tiny anyway
        latencyWait_.reset(swap_->GetFrameLatencyWaitableObject());
        if (dcompVisual_ && dcompDev_)
        {
            dcompVisual_->SetContent(swap_.get());
            dcompDev_->Commit();
        }
    }
    else
    {
        const HRESULT hr = swap_->ResizeBuffers(0, static_cast<UINT>(w), static_cast<UINT>(h), DXGI_FORMAT_UNKNOWN,
                                                kSwapFlags);
        if (FAILED(hr))
        {
            LOG_WARN("ResizeBuffers failed (0x%08lX)", static_cast<unsigned long>(hr));
            NoteLoss("ResizeBuffers", hr);
            return false;
        }
    }

    ComPtr<IDXGISurface> surf;
    HRESULT hr = swap_->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(surf.put()));
    if (FAILED(hr))
    {
        NoteLoss("GetBuffer", hr);
        return false;
    }
    D2D1_BITMAP_PROPERTIES1 bp{};
    bp.pixelFormat = D2D1_PIXEL_FORMAT{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
    bp.dpiX = bp.dpiY = 96.f;
    bp.bitmapOptions = static_cast<D2D1_BITMAP_OPTIONS>(
        static_cast<UINT>(D2D1_BITMAP_OPTIONS_TARGET) | static_cast<UINT>(D2D1_BITMAP_OPTIONS_CANNOT_DRAW));
    hr = dc_->CreateBitmapFromDxgiSurface(surf.get(), &bp, target_.put());
    if (FAILED(hr))
    {
        LOG_ERROR("CreateBitmapFromDxgiSurface failed (0x%08lX)", static_cast<unsigned long>(hr));
        NoteLoss("CreateBitmapFromDxgiSurface", hr);
        return false;
    }
    dc_->SetTarget(target_.get());
    w_ = w;
    h_ = h;
    winW_ = -1; // force the window to be resized to match on the next Present
    LOG_INFO("Overlay surface %dx%d (GPU)", w, h);
    return true;
}

bool Gfx::PresentGpu(HWND hwnd, int x, int y)
{
    if (!swap_) return false;
    if (x != winX_ || y != winY_ || w_ != winW_ || h_ != winH_)
    {
        // The composition surface has no window bitmap, so the window itself just has to cover the game.
        SetWindowPos(hwnd, HWND_TOPMOST, x, y, w_, h_, SWP_NOACTIVATE);
        winX_ = x;
        winY_ = y;
        winW_ = w_;
        winH_ = h_;
    }
    if (injectLoss_)
    {
        injectLoss_ = false;
        NoteLoss("Present (simulated)", DXGI_ERROR_DEVICE_REMOVED);
        return false;
    }
    const HRESULT hr = swap_->Present(vsync_ ? 1 : 0, 0);
    if (FAILED(hr) && IsLoss(hr))
    {
        NoteLoss("Present", hr);
        return false;
    }
    return SUCCEEDED(hr);
}

#endif // ESP_GPU_GFX

bool Gfx::Attach(HWND hwnd)
{
#ifdef ESP_GPU_GFX
    if (!gpu_) return false;
    hwnd_ = hwnd;
    // A layered window only shows its DirectComposition content once an alpha has been set (Windows 8+).
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    if (!AttachDComp())
    {
        hwnd_ = nullptr;
        return false;
    }
    return true;
#else
    (void)hwnd;
    return false;
#endif
}

void Gfx::WaitFrame()
{
#ifdef ESP_GPU_GFX
    if (gpu_ && latencyWait_.valid()) WaitForSingleObjectEx(latencyWait_.get(), 100, TRUE);
#endif
}

// ------------------------------------------------------------------------------------------ frame

void Gfx::Begin(Col clear)
{
#ifdef ESP_GPU_GFX
    if (gpu_)
    {
        rt_->BeginDraw();
        D2D1_MATRIX_3X2_F id = D2D1::Matrix3x2F::Identity();
        rt_->SetTransform(&id);
        D2D1_COLOR_F c = ToD2D(clear);
        rt_->Clear(&c);
        layerOn_ = false;
        if (opacity_ < 0.995f)
        {
            // Global overlay opacity < 100%: draw the whole frame inside one layer with that opacity.
            D2D1_LAYER_PARAMETERS1 lp{};
            lp.contentBounds = D2D1_RECT_F{-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX};
            lp.maskTransform = D2D1::Matrix3x2F::Identity();
            lp.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
            lp.opacity = std::max(0.f, opacity_);
            lp.layerOptions = D2D1_LAYER_OPTIONS1_NONE;
            dc_->PushLayer(&lp, nullptr);
            layerOn_ = true;
        }
        return;
    }
#endif
    RECT rc{0, 0, w_, h_};
    dcrt_->BindDC(memDc_, &rc);
    rt_->BeginDraw();
    D2D1_MATRIX_3X2_F id = D2D1::Matrix3x2F::Identity();
    rt_->SetTransform(&id);
    D2D1_COLOR_F c = ToD2D(clear);
    rt_->Clear(&c);
}

bool Gfx::End()
{
#ifdef ESP_GPU_GFX
    if (gpu_)
    {
        if (layerOn_)
        {
            rt_->PopLayer();
            layerOn_ = false;
        }
        const HRESULT hr = rt_->EndDraw();
        if (FAILED(hr) && IsLoss(hr))
        {
            NoteLoss("EndDraw", hr);
            return false;
        }
        return SUCCEEDED(hr);
    }
#endif
    HRESULT hr = rt_->EndDraw();
    if (hr == static_cast<HRESULT>(D2DERR_RECREATE_TARGET))
    {
        LOG_WARN("D2D render target lost - recreating.");
        w_ = h_ = 0; // force Resize() to rebuild everything
        dcrt_.reset();
        rt_ = nullptr;
        return false;
    }
    return SUCCEEDED(hr);
}

bool Gfx::Present(HWND hwnd, int x, int y, float opacity)
{
#ifdef ESP_GPU_GFX
    if (gpu_) return PresentGpu(hwnd, x, y);
#endif
    POINT dst{x, y}, src{0, 0};
    SIZE sz{w_, h_};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, static_cast<BYTE>(std::clamp(opacity, 0.f, 1.f) * 255.f + 0.5f), AC_SRC_ALPHA};
    return UpdateLayeredWindow(hwnd, nullptr, &dst, &sz, memDc_, &src, 0, &bf, ULW_ALPHA) != 0;
}

// ------------------------------------------------------------------------------------------ primitives

void Gfx::SetBrush(Col c)
{
    D2D1_COLOR_F d = ToD2D(c);
    brush_->SetColor(&d);
}

void Gfx::Line(float x1, float y1, float x2, float y2, Col c, float t)
{
    SetBrush(c);
    rt_->DrawLine(D2D1_POINT_2F{x1, y1}, D2D1_POINT_2F{x2, y2}, brush_.get(), t);
}

void Gfx::Rect(float x, float y, float w, float h, Col c, float t)
{
    SetBrush(c);
    D2D1_RECT_F r = RectF(x, y, w, h);
    rt_->DrawRectangle(&r, brush_.get(), t);
}

void Gfx::FillRect(float x, float y, float w, float h, Col c)
{
    SetBrush(c);
    D2D1_RECT_F r = RectF(x, y, w, h);
    rt_->FillRectangle(&r, brush_.get());
}

void Gfx::RRect(float x, float y, float w, float h, float r, Col c, float t)
{
    SetBrush(c);
    D2D1_ROUNDED_RECT rr{RectF(x, y, w, h), r, r};
    rt_->DrawRoundedRectangle(&rr, brush_.get(), t);
}

void Gfx::FillRRect(float x, float y, float w, float h, float r, Col c)
{
    SetBrush(c);
    D2D1_ROUNDED_RECT rr{RectF(x, y, w, h), r, r};
    rt_->FillRoundedRectangle(&rr, brush_.get());
}

void Gfx::Circle(float x, float y, float r, Col c, float t)
{
    SetBrush(c);
    D2D1_ELLIPSE e{D2D1_POINT_2F{x, y}, r, r};
    rt_->DrawEllipse(&e, brush_.get(), t);
}

void Gfx::FillCircle(float x, float y, float r, Col c)
{
    SetBrush(c);
    D2D1_ELLIPSE e{D2D1_POINT_2F{x, y}, r, r};
    rt_->FillEllipse(&e, brush_.get());
}

void Gfx::FillTriangle(float x1, float y1, float x2, float y2, float x3, float y3, Col c)
{
    ComPtr<ID2D1PathGeometry> g;
    if (FAILED(Fac()->CreatePathGeometry(g.put()))) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(g->Open(sink.put()))) return;
    sink->BeginFigure(D2D1_POINT_2F{x1, y1}, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1_POINT_2F{x2, y2});
    sink->AddLine(D2D1_POINT_2F{x3, y3});
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    SetBrush(c);
    rt_->FillGeometry(g.get(), brush_.get());
}

void Gfx::DrawImage(const Image& img, float x, float y, float w, float h, float opacity)
{
    if (img.bgra.empty() || img.w <= 0 || img.h <= 0) return;
    auto it = bitmaps_.find(img.id);
    if (it == bitmaps_.end())
    {
        if (bitmaps_.size() > 128) bitmaps_.clear();
        D2D1_BITMAP_PROPERTIES bp{};
        bp.pixelFormat = D2D1_PIXEL_FORMAT{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
        bp.dpiX = bp.dpiY = 96.f;
        ComPtr<ID2D1Bitmap> bmp;
        if (FAILED(rt_->CreateBitmap(D2D1_SIZE_U{static_cast<UINT32>(img.w), static_cast<UINT32>(img.h)},
                                     img.bgra.data(),
                                     static_cast<UINT32>(img.w) * 4, &bp, bmp.put())))
            return;
        it = bitmaps_.emplace(img.id, std::move(bmp)).first;
    }
    D2D1_RECT_F d = RectF(x, y, w, h);
    rt_->DrawBitmap(it->second.get(), &d, opacity, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, nullptr);
}

void Gfx::PushClip(float x, float y, float w, float h)
{
    D2D1_RECT_F r = RectF(x, y, w, h);
    rt_->PushAxisAlignedClip(&r, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void Gfx::PopClip() { rt_->PopAxisAlignedClip(); }

// ------------------------------------------------------------------------------------------ text

// DrawText() builds a fresh IDWriteTextLayout on every call. Labels (names, counts, enchantments) repeat from
// frame to frame, so the layouts are cached by (font, alignment, text) and drawn with DrawTextLayout().
IDWriteTextLayout* Gfx::Layout(const std::string& s, Font f, int align)
{
    auto& m = layouts_[static_cast<int>(f)][align];
    auto it = m.find(s);
    if (it != m.end()) return it->second.get();
    if (m.size() >= kLayoutCacheMax) m.clear();
    const std::wstring ws = ToWide(s);
    ComPtr<IDWriteTextLayout> l;
    if (FAILED(dw_->CreateTextLayout(ws.c_str(), static_cast<UINT32>(ws.size()),
                                     fmt_[static_cast<int>(f)][align].get(), 400.f, 40.f, l.put())))
        return nullptr;
    IDWriteTextLayout* raw = l.get();
    m.emplace(s, std::move(l));
    return raw;
}

// align: 0 left (text starts at x), 1 centered on x, 2 right edge at x. The layout box is 400 px wide.
void Gfx::DrawStr(const std::string& s, float x, float y, float w, Font f, Col c, int align, bool shadow)
{
    if (s.empty()) return;
    IDWriteTextLayout* layout = Layout(s, f, align);
    if (!layout) return;
    const float left = align == 0 ? x : align == 1 ? x - w * 0.5f : x - w;
    if (shadow)
    {
        SetBrush(Col{0, 0, 0, 0.85f * c.a});
        rt_->DrawTextLayout(D2D1_POINT_2F{left + 1, y + 1}, layout, brush_.get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
    }
    SetBrush(c);
    rt_->DrawTextLayout(D2D1_POINT_2F{left, y}, layout, brush_.get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
}

void Gfx::Text(const std::string& s, float x, float y, Font f, Col c, bool sh)
{
    DrawStr(s, x, y, 400.f, f, c, 0, sh);
}

void Gfx::TextCentered(const std::string& s, float cx, float y, Font f, Col c, bool sh)
{
    DrawStr(s, cx, y, 400.f, f, c, 1, sh);
}

void Gfx::TextRight(const std::string& s, float rx, float y, Font f, Col c, bool sh)
{
    DrawStr(s, rx, y, 400.f, f, c, 2, sh);
}

float Gfx::TextWidth(const std::string& s, Font f)
{
    auto& cache = widths_[static_cast<int>(f)];
    auto it = cache.find(s);
    if (it != cache.end()) return it->second;
    std::wstring ws = ToWide(s);
    if (ws.empty()) return 0.f;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dw_->CreateTextLayout(ws.c_str(), static_cast<UINT32>(ws.size()), fmt_[static_cast<int>(f)][0].get(),
                                     1000.f, 40.f, layout.put()))) return 0.f;
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    if (cache.size() >= kWidthCacheMax) cache.clear();
    cache.emplace(s, m.widthIncludingTrailingWhitespace);
    return m.widthIncludingTrailingWhitespace;
}
