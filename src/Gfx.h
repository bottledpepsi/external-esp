// Gfx.h - Direct2D + DirectWrite drawing for the transparent overlay window. Two interchangeable backends:
//
//  GPU (default when available): a Direct3D 11 device, a flip-model DXGI swap chain with premultiplied alpha that
//    is bound to the overlay window through DirectComposition, and a Direct2D *device context* that draws straight
//    into the swap chain's back buffer. Nothing is copied through system memory: no full-frame clear, no DIB
//    read-back, no UpdateLayeredWindow upload - the per-frame cost no longer scales with the window's pixel count.
//    Frame pacing uses the swap chain's frame-latency waitable object (replaces DwmFlush).
//
//  Legacy (fallback / --gdi): Direct2D renders through a GDI-compatible DC render target into a 32-bit
//    premultiplied-alpha DIB, handed to the window manager with UpdateLayeredWindow. Needs only d2d1/dwrite, but
//    does three full-surface passes per frame (clear, EndDraw copy into the DIB, UpdateLayeredWindow upload).
//
// The drawing API below is identical for both, so EspRenderer / ConfigUi do not care which one is active.
#pragma once
#include "GpuRecovery.h"
#include "Platform.h"
#include "Image.h"
#include "RaiiHandle.h"
#ifdef DrawText
#undef DrawText          // windows.h maps DrawText -> DrawTextA; that would rename ID2D1RenderTarget::DrawText
#endif
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <unordered_map>

// The GPU backend needs these headers (all present in mingw-w64 and the Windows SDK). If any is missing the
// project still builds and simply uses the legacy backend. The libraries are loaded at run time (LoadLibrary),
// so no extra link flags are required.
#if __has_include(<d2d1_1.h>) && __has_include(<d3d11.h>) && __has_include(<dxgi1_3.h>) && __has_include(<dcomp.h>)
#define ESP_GPU_GFX 1
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#endif

struct Col
{
    float r = 1, g = 1, b = 1, a = 1;
    Col with(float alpha) const { return {r, g, b, alpha}; }
};

enum class Font { Name10, Name13, Name17, Ui, UiSmall, UiBold, Hint, Count };

class Gfx
{
public:
    Gfx() = default;
    ~Gfx();
    Gfx(const Gfx&) = delete;
    Gfx& operator=(const Gfx&) = delete;

    // factories, text formats and (if allowGpu) the D3D11/D2D device stack. Falls back to the legacy backend
    // by itself when the GPU stack cannot be created.
    bool Init(bool allowGpu = true);
    bool IsGpu() const { return gpu_; }
    // GPU backend: bind the swap chain to the overlay window (DirectComposition). The window must have been created
    // with WS_EX_NOREDIRECTIONBITMAP. Returns false if that fails (caller recreates the window and calls ForceLegacy()).
    bool Attach(HWND hwnd);
    bool ForceLegacy(); // drop the GPU stack, use the DIB / UpdateLayeredWindow path
    // Device loss (docs/windows-checklist.md): recovery is decided by GpuRecovery. After it gives up WantsLegacy() is true and the owner of the window must
    // recreate it and call ForceLegacy(). SimulateLoss() is for --simulate-device-loss: one loss at the next Present (persistent: every recovery fails too).
    // The window was moved or resized by something other than us (DPI change, display change): the next Present must place it again.
    void InvalidatePlacement();
    bool WantsLegacy() const;
    bool DeviceLost() const;
    void SimulateLoss(bool persistent);
    std::string LossSummary() const; // "" when nothing happened, else e.g. "GPU device lost 2x, recovered 2x"

    bool Resize(int w, int h); // (re)create the render surface; false => skip this frame
    int Width() const { return w_; }
    int Height() const { return h_; }

    // GPU backend: block until the compositor can accept another frame (call at the START of a frame). No-op otherwise.
    void WaitFrame();
    void SetOpacity(float o) { opacity_ = o; } // global overlay opacity 0..1 (applied at Begin() in the GPU backend)
    void SetVSync(bool v) { vsync_ = v; } // GPU backend: Present sync interval 1 (on) or 0 (off)

    void Begin(Col clear); // BeginDraw + Clear
    bool End(); // EndDraw; false => render target lost (recreated on a later Resize)
    // Publishes the frame at screen position x,y. `opacity` is used by the legacy backend only (GPU uses SetOpacity).
    bool Present(HWND hwnd, int x, int y, float opacity);

    // ---- primitives (device-independent pixels == screen pixels; the target runs at 96 dpi) ----
    void Line(float x1, float y1, float x2, float y2, Col c, float thick = 1.f);
    void Rect(float x, float y, float w, float h, Col c, float thick = 1.f);
    void FillRect(float x, float y, float w, float h, Col c);
    void RRect(float x, float y, float w, float h, float r, Col c, float thick = 1.f);
    void FillRRect(float x, float y, float w, float h, float r, Col c);
    void Circle(float x, float y, float r, Col c, float thick = 1.f);
    void FillCircle(float x, float y, float r, Col c);
    void FillTriangle(float x1, float y1, float x2, float y2, float x3, float y3, Col c);
    // Draws a premultiplied-BGRA image scaled to (x,y,w,h) with nearest-neighbour filtering (crisp pixel art).
    // The Direct2D bitmap is created on first use and cached under img.id (dropped if the render target is lost).
    void DrawImage(const Image& img, float x, float y, float w, float h, float opacity = 1.f);
    void PushClip(float x, float y, float w, float h);
    void PopClip();

    // ---- text (UTF-8). Left-aligned at x, centered on x, or right-aligned to x. ----
    void Text(const std::string& s, float x, float y, Font f, Col c, bool shadow = false);
    void TextCentered(const std::string& s, float cx, float y, Font f, Col c, bool shadow = false);
    void TextRight(const std::string& s, float rx, float y, Font f, Col c, bool shadow = false);
    float TextWidth(const std::string& s, Font f);

private:
    static constexpr int kFontCount = static_cast<int>(Font::Count);

    bool CreateRenderTarget(); // legacy DC render target
    void DrawStr(const std::string& s, float x, float y, float w, Font f, Col c, int align, bool shadow);
    IDWriteTextLayout* Layout(const std::string& s, Font f, int align); // cached 400x40 layout
    void SetBrush(Col c);
    void FreeSurface();
    ID2D1Factory* Fac() const;

    ComPtr<ID2D1Factory> factory_; // legacy-only factory (when no ID2D1Factory1 is available)
    ComPtr<ID2D1DCRenderTarget> dcrt_; // legacy backend render target
    ID2D1RenderTarget* rt_ = nullptr; // non-owning: whichever backend is active; every primitive draws through it
    ComPtr<ID2D1SolidColorBrush> brush_; // single scratch brush, recoloured per draw call
    ComPtr<IDWriteFactory> dw_;
    ComPtr<IDWriteTextFormat> fmt_[kFontCount][3]; // [font][0=left 1=center 2=right]
    std::unordered_map<std::string, ComPtr<IDWriteTextLayout>> layouts_[kFontCount][3]; // text layout cache
    std::unordered_map<std::string, float> widths_[kFontCount]; // TextWidth cache

    std::unordered_map<uint64_t, ComPtr<ID2D1Bitmap>> bitmaps_;
    HDC memDc_ = nullptr; // legacy
    HBITMAP dib_ = nullptr, oldBmp_ = nullptr; // legacy
    int w_ = 0, h_ = 0;
    bool gpu_ = false;
    float opacity_ = 1.f;
    bool vsync_ = true;
    // device-loss bookkeeping: always present (the public queries are unconditional), only the GPU backend ever changes it
    GpuRecovery rec_;
    bool deviceLost_ = false, wantLegacy_ = false, injectLoss_ = false;
    int failInit_ = 0; // simulated persistent loss: InitGpu fails while > 0

#ifdef ESP_GPU_GFX
    bool InitGpu();
    bool AttachDComp();
    bool ResizeGpu(int w, int h);
    bool PresentGpu(HWND hwnd, int x, int y);
    bool RecoverGpu();
    void ReleaseGpu();
    void NoteLoss(const char* where, HRESULT hr);
    bool IsLoss(HRESULT hr) const;

    ComPtr<ID2D1Factory1> factory1_;
    ComPtr<ID3D11Device> d3d_;
    ComPtr<IDXGIDevice> dxgiDev_;
    ComPtr<IDXGIFactory2> dxgiFac_;
    ComPtr<ID2D1Device> d2dDev_;
    ComPtr<ID2D1DeviceContext> dc_;
    ComPtr<ID2D1Bitmap1> target_; // the swap chain's back buffer as a D2D bitmap
    ComPtr<IDXGISwapChain2> swap_;
    UniqueHandle latencyWait_;
    ComPtr<IDCompositionDevice> dcompDev_;
    ComPtr<IDCompositionTarget> dcompTarget_;
    ComPtr<IDCompositionVisual> dcompVisual_;
    FARPROC dcompCreate_ = nullptr; // DCompositionCreateDevice
    FARPROC d3dCreate_ = nullptr; // D3D11CreateDevice
    HWND hwnd_ = nullptr;
    bool layerOn_ = false;

    int winX_ = -32768, winY_ = -32768, winW_ = -1, winH_ = -1; // last window rect applied by Present
#endif
};
