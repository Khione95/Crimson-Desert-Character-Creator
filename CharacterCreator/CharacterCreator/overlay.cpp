#include "pch.h"
#include "overlay.h"
#include "log.h"
#include "switches.h"

#include <d3d11on12.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wincodec.h>

#include "MinHook.h"

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

typedef HRESULT(STDMETHODCALLTYPE* PresentFn)(IDXGISwapChain* self, UINT sync, UINT flags);
typedef HRESULT(STDMETHODCALLTYPE* Present1Fn)(IDXGISwapChain1* self, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* params);
typedef HRESULT(STDMETHODCALLTYPE* ResizeBuffersFn)(IDXGISwapChain* self, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags);
typedef HRESULT(STDMETHODCALLTYPE* SetColorSpace1Fn)(IDXGISwapChain3* self, DXGI_COLOR_SPACE_TYPE space);
typedef void(STDMETHODCALLTYPE* ExecuteCommandListsFn)(ID3D12CommandQueue* self, UINT count, ID3D12CommandList* const* lists);

static PresentFn g_originalPresent = NULL;
static Present1Fn g_originalPresent1 = NULL;
static ResizeBuffersFn g_originalResizeBuffers = NULL;
static SetColorSpace1Fn g_originalSetColorSpace1 = NULL;
static ExecuteCommandListsFn g_originalExecute = NULL;

// The colour space the game (or an HDR mod such as RenoDX) set on its swap
// chain: HDR10 and scRGB screens get the menu converted to match.
static volatile LONG g_colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

static OverlayDrawFn g_draw = NULL;
static OverlayKeyFn g_key = NULL;
static volatile LONG g_visible = 0;
static volatile LONG g_drawing = 0;     // drawn without taking the keys (a notice)

// The game's direct command queues, seen through ExecuteCommandLists. The
// overlay must draw on the one the swap chain presents with: drawing on
// another (a loading queue, for example) is not in step with the frames
// shown, and the panel flickers or disappears.
static const int MAX_QUEUES = 16;
static ID3D12CommandQueue* volatile g_queues[MAX_QUEUES];
static volatile LONG g_queueCount = 0;
static SRWLOCK g_queueLock = SRWLOCK_INIT;
static __declspec(thread) ID3D12CommandQueue* t_lastQueue = NULL;   // per thread
static int g_setupAttempts = 0;


struct RenderState
{
    bool ready;
    bool failed;
    IDXGISwapChain3* swapChain;
    ID3D12CommandQueue* queue;      // the queue the Direct3D 11 device draws on
    ID3D11Device* d11;
    ID3D11DeviceContext* d11Context;
    ID3D11On12Device* on12;
    ID2D1Factory1* d2dFactory;
    ID2D1Device* d2dDevice;
    ID2D1DeviceContext* dc;
    IDWriteFactory* write;
    IWICImagingFactory* wic;
    DXGI_FORMAT format;
    float width, height;

    // Composite path for screens Direct2D cannot draw on (HDR formats): the
    // menu is drawn into an 8-bit image, then painted onto the screen.
    bool compositeFailed;
    UINT imageWidth, imageHeight;
    ID3D11Texture2D* image;
    ID3D11ShaderResourceView* imageView;
    ID2D1Bitmap1* imageTarget;
    ID3D11VertexShader* vs;
    ID3D11PixelShader* ps;
    ID3D11SamplerState* sampler;
    ID3D11BlendState* blend;
    ID3D11Buffer* constants;
};

static RenderState g_rs = {};
static volatile LONG g_generation = 1;  // bumped when the drawing objects are made anew
static SRWLOCK g_renderLock = SRWLOCK_INIT;

static HWND g_window = NULL;
static WNDPROC g_originalWndProc = NULL;

template <typename T> static void SafeRelease(T*& p)
{
    if (p)
    {
        p->Release();
        p = NULL;
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

static bool IsKeyboardMessage(UINT msg)
{
    return msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN ||
        msg == WM_SYSKEYUP || msg == WM_CHAR || msg == WM_SYSCHAR;
}

// Games that read raw input may turn off the normal key messages. Key presses
// are then taken from the raw input instead, but never from both.
static bool g_sawKeyMessages = false;
static volatile LONG g_rawKeyCount = 0;
static volatile LONG g_legacyKeyCount = 0;

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN)
    {
        g_sawKeyMessages = true;
        InterlockedIncrement(&g_legacyKeyCount);
    }

    if (g_visible)
    {
        if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN)
        {
            // With an input method on (Chinese, Japanese, Korean) letter keys
            // arrive as VK_PROCESSKEY: the key is taken from its scan code.
            int vk = (int)wParam;

            if (vk == VK_PROCESSKEY)
                vk = (int)MapVirtualKeyW((lParam >> 16) & 0xFF, MAPVK_VSC_TO_VK);

            if (g_key && vk)
                g_key(vk);
            return 0;
        }

        if (IsKeyboardMessage(msg))
            return 0;

        // Raw keyboard input is kept from the game while the menu is open.
        // Mouse input still goes through.
        if (msg == WM_INPUT)
        {
            RAWINPUT raw;
            UINT size = sizeof(raw);

            if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
                raw.header.dwType == RIM_TYPEKEYBOARD)
            {
                InterlockedIncrement(&g_rawKeyCount);
                bool down = (raw.data.keyboard.Flags & RI_KEY_BREAK) == 0;

                if (down && !g_sawKeyMessages && g_key)
                    g_key(raw.data.keyboard.VKey);

                return DefWindowProcW(hwnd, msg, wParam, lParam);
            }
        }
    }
    else if (msg == WM_INPUT)
    {
        RAWINPUT raw;
        UINT size = sizeof(raw);

        if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
            raw.header.dwType == RIM_TYPEKEYBOARD)
            InterlockedIncrement(&g_rawKeyCount);
    }

    return CallWindowProcW(g_originalWndProc, hwnd, msg, wParam, lParam);
}

void OverlayInputStats(long* keyMessages, long* rawKeys)
{
    *keyMessages = g_legacyKeyCount;
    *rawKeys = g_rawKeyCount;
}

static void HookWindow(HWND window)
{
    if (g_window || !window)
        return;

    g_window = window;
    g_originalWndProc = (WNDPROC)SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)OverlayWndProc);
    Log("overlay: listening to game window %p", window);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

// The overlay holds no reference to the game's back buffers (or its swap
// chain) between frames: it wraps the current back buffer only while drawing
// the menu. Holding them made the game crash when it resized or rebuilt its
// swap chain (starting a new game).
static void ReadSize(IDXGISwapChain* swapChain)
{
    DXGI_SWAP_CHAIN_DESC desc;

    if (FAILED(swapChain->GetDesc(&desc)))
        return;

    g_rs.width = (float)desc.BufferDesc.Width;
    g_rs.height = (float)desc.BufferDesc.Height;
    g_rs.format = desc.BufferDesc.Format;
}

// Looks for a known queue inside the swap chain object, which keeps the queue
// it was created with. Returns the offset, or -1.
static int FindQueueInSwapChain(IDXGISwapChain* swapChain, ID3D12CommandQueue** found)
{
    static const size_t SCAN_BYTES = 0x2000;
    LONG count = g_queueCount;

    __try
    {
        const uintptr_t* p = (const uintptr_t*)swapChain;

        for (size_t i = 0; i < SCAN_BYTES / sizeof(uintptr_t); ++i)
        {
            for (LONG q = 0; q < count; ++q)
            {
                if (p[i] == (uintptr_t)g_queues[q])
                {
                    *found = g_queues[q];
                    return (int)(i * sizeof(uintptr_t));
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    return -1;
}

// The queue the game created its swap chain with (see OverlayEarlyInit).
static ID3D12CommandQueue* volatile g_creationQueue = NULL;

// The queue the swap chain presents with (called on the presenting thread).
static ID3D12CommandQueue* PresentQueue(IDXGISwapChain* swapChain)
{
    if (g_creationQueue)
    {
        Log("overlay: drawing on the queue the game created its swap chain with (%p)", g_creationQueue);
        return g_creationQueue;
    }

    if (g_queueCount == 0)
        return NULL;    // wait until the game has submitted work

    ID3D12CommandQueue* queue = NULL;
    int offset = FindQueueInSwapChain(swapChain, &queue);

    if (offset >= 0)
    {
        Log("overlay: drawing on the swap chain's queue %p (1 of %ld queues, found at +0x%X)", queue, g_queueCount, offset);
        return queue;
    }

    // Give the game a few frames to submit on its presenting queue.
    if (++g_setupAttempts < 120)
        return NULL;

    queue = t_lastQueue ? t_lastQueue : g_queues[0];
    Log("overlay: swap chain queue not found - drawing on %s queue %p (%ld queues seen)",
        t_lastQueue ? "the presenting thread's" : "the first", queue, g_queueCount);
    return queue;
}

static bool Setup(IDXGISwapChain* swapChain)
{
    ID3D12CommandQueue* queue = PresentQueue(swapChain);

    if (!queue)
        return false;

    // Kept only to recognise the swap chain, without a reference.
    if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&g_rs.swapChain))))
    {
        Log("overlay: swap chain is not DXGI 1.4");
        return false;
    }

    g_rs.swapChain->Release();

    ID3D12Device* device = NULL;

    if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&device))))
    {
        Log("overlay: swap chain is not DirectX 12");
        return false;
    }

    g_rs.queue = queue;
    IUnknown* queues[] = { queue };
    HRESULT hr = D3D11On12CreateDevice(device, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
        queues, 1, 0, &g_rs.d11, &g_rs.d11Context, NULL);
    device->Release();

    if (FAILED(hr))
    {
        Log("overlay: D3D11On12CreateDevice failed (0x%08X)", hr);
        return false;
    }

    g_rs.d11->QueryInterface(IID_PPV_ARGS(&g_rs.on12));

    D2D1_FACTORY_OPTIONS options = {};
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options, (void**)&g_rs.d2dFactory);

    IDXGIDevice* dxgiDevice = NULL;
    g_rs.d11->QueryInterface(IID_PPV_ARGS(&dxgiDevice));

    if (!g_rs.d2dFactory || !dxgiDevice || FAILED(g_rs.d2dFactory->CreateDevice(dxgiDevice, &g_rs.d2dDevice)))
    {
        SafeRelease(dxgiDevice);
        Log("overlay: Direct2D setup failed");
        return false;
    }

    SafeRelease(dxgiDevice);
    g_rs.d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &g_rs.dc);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_rs.write);
    CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_rs.wic));

    if (!g_rs.dc || !g_rs.write)
        return false;

    DXGI_SWAP_CHAIN_DESC desc;
    swapChain->GetDesc(&desc);
    ReadSize(swapChain);
    HookWindow(desc.OutputWindow);

    Log("overlay ready (%.0fx%.0f format %d)", g_rs.width, g_rs.height, g_rs.format);
    return true;
}

// Draws straight onto the back buffer (8-bit screens). Returns false if
// Direct2D cannot draw on this format.
static bool RenderDirect(IDXGISwapChain* self)
{
    IDXGISwapChain3* swapChain = NULL;
    ID3D12Resource* buffer = NULL;
    ID3D11Resource* wrapped = NULL;
    IDXGISurface* surface = NULL;
    ID2D1Bitmap1* target = NULL;

    HRESULT hr = self->QueryInterface(IID_PPV_ARGS(&swapChain));
    const char* step = "swap chain";

    if (SUCCEEDED(hr) && (step = "back buffer", SUCCEEDED(hr = swapChain->GetBuffer(swapChain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&buffer)))))
    {
        ReadSize(swapChain);

        D3D11_RESOURCE_FLAGS flags = { D3D11_BIND_RENDER_TARGET };
        D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(g_rs.format, D2D1_ALPHA_MODE_PREMULTIPLIED));

        if ((step = "wrap", SUCCEEDED(hr = g_rs.on12->CreateWrappedResource(buffer, &flags, D3D12_RESOURCE_STATE_PRESENT,
                D3D12_RESOURCE_STATE_PRESENT, IID_PPV_ARGS(&wrapped)))) &&
            (step = "surface", SUCCEEDED(hr = wrapped->QueryInterface(IID_PPV_ARGS(&surface)))) &&
            (step = "bitmap", SUCCEEDED(hr = g_rs.dc->CreateBitmapFromDxgiSurface(surface, &props, &target))))
        {
            g_rs.on12->AcquireWrappedResources(&wrapped, 1);
            g_rs.dc->SetTarget(target);
            g_rs.dc->BeginDraw();

            OverlayDrawContext ctx = { g_rs.dc, g_rs.write, g_rs.width, g_rs.height, (unsigned)g_generation };
            g_draw(ctx);

            step = "draw";
            hr = g_rs.dc->EndDraw();
            g_rs.dc->SetTarget(NULL);
            g_rs.on12->ReleaseWrappedResources(&wrapped, 1);

            // Hands the drawing to the game's queue before the back buffer
            // is let go.
            g_rs.d11Context->Flush();
        }
    }

    static int failures = 0;

    if (FAILED(hr) && failures < 5)
    {
        ++failures;
        Log("overlay: drawing failed at %s (0x%08X, format %d)", step, hr, g_rs.format);
    }

    SafeRelease(target);
    SafeRelease(surface);
    SafeRelease(wrapped);
    SafeRelease(buffer);
    SafeRelease(swapChain);
    g_rs.d11Context->Flush();
    return !(FAILED(hr) && strcmp(step, "bitmap") == 0);
}

// ---------------------------------------------------------------------------
// Composite path: HDR and other formats Direct2D cannot draw on
// ---------------------------------------------------------------------------

// Full-screen triangle; the pixel shader converts the menu's sRGB colours
// for the screen: mode 0 as they are, 1 scRGB (linear, 1.0 = 80 nits),
// 2 HDR10 (BT.2020 primaries, PQ), 3 linear for an sRGB back buffer.
static const char COMPOSITE_HLSL[] = R"(
Texture2D menu : register(t0);
SamplerState pointSampler : register(s0);
cbuffer Settings : register(b0) { uint mode; float white; float2 unused; };

struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };

Vertex vs_main(uint id : SV_VertexID)
{
    Vertex v;
    v.uv = float2((id << 1) & 2, id & 2);
    v.position = float4(v.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return v;
}

float3 Linear(float3 c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }

float3 PQ(float3 l)
{
    float3 p = pow(max(l, 0), 0.1593017578125);
    return pow((0.8359375 + 18.8515625 * p) / (1 + 18.6875 * p), 78.84375);
}

float4 ps_main(Vertex v) : SV_Target
{
    float4 c = menu.Sample(pointSampler, v.uv);
    if (c.a <= 0) discard;
    float3 rgb = c.rgb / c.a;

    if (mode == 1)
        rgb = Linear(rgb) * white;
    else if (mode == 2)
    {
        float3 l = Linear(rgb);
        l = float3(dot(float3(0.6274, 0.3293, 0.0433), l), dot(float3(0.0691, 0.9195, 0.0114), l), dot(float3(0.0164, 0.0880, 0.8956), l));
        rgb = PQ(l * white);
    }
    else if (mode == 3)
        rgb = Linear(rgb);

    return float4(rgb * c.a, c.a);
}
)";

// The menu's white on HDR screens (BT.2408 reference white).
static const float HDR_WHITE_NITS = 203.0f;

struct CompositeSettings
{
    UINT mode;
    float white;
    float unused[2];
};

static bool CompileShader(const char* entry, const char* target, ID3DBlob** out)
{
    typedef HRESULT(WINAPI* CompileFn)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR,
        UINT, UINT, ID3DBlob**, ID3DBlob**);
    static CompileFn compile = NULL;

    if (!compile)
    {
        HMODULE library = LoadLibraryA("d3dcompiler_47.dll");
        compile = library ? (CompileFn)GetProcAddress(library, "D3DCompile") : NULL;
    }

    ID3DBlob* errors = NULL;

    if (!compile || FAILED(compile(COMPOSITE_HLSL, sizeof(COMPOSITE_HLSL) - 1, "overlay", NULL, NULL, entry, target,
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &errors)))
    {
        Log("overlay: shader %s could not be compiled%s%s", entry, errors ? ": " : "",
            errors ? (const char*)errors->GetBufferPointer() : "");
        SafeRelease(errors);
        return false;
    }

    SafeRelease(errors);
    return true;
}

static bool SetupComposite()
{
    if (g_rs.vs)
        return true;

    ID3DBlob* vs = NULL;
    ID3DBlob* ps = NULL;

    bool ok = CompileShader("vs_main", "vs_5_0", &vs) && CompileShader("ps_main", "ps_5_0", &ps) &&
        SUCCEEDED(g_rs.d11->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), NULL, &g_rs.vs)) &&
        SUCCEEDED(g_rs.d11->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), NULL, &g_rs.ps));

    SafeRelease(vs);
    SafeRelease(ps);

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    D3D11_BUFFER_DESC cb = {};
    cb.ByteWidth = sizeof(CompositeSettings);
    cb.Usage = D3D11_USAGE_DEFAULT;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    ok = ok && SUCCEEDED(g_rs.d11->CreateSamplerState(&sd, &g_rs.sampler)) &&
        SUCCEEDED(g_rs.d11->CreateBlendState(&bd, &g_rs.blend)) &&
        SUCCEEDED(g_rs.d11->CreateBuffer(&cb, NULL, &g_rs.constants));

    if (!ok)
        Log("overlay: the HDR drawing path could not be set up");

    return ok;
}

// The 8-bit image the menu is drawn into, sized like the screen.
static bool PrepareImage(UINT width, UINT height)
{
    if (g_rs.image && g_rs.imageWidth == width && g_rs.imageHeight == height)
        return true;

    SafeRelease(g_rs.imageTarget);
    SafeRelease(g_rs.imageView);
    SafeRelease(g_rs.image);

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    IDXGISurface* surface = NULL;
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    bool ok = SUCCEEDED(g_rs.d11->CreateTexture2D(&td, NULL, &g_rs.image)) &&
        SUCCEEDED(g_rs.d11->CreateShaderResourceView(g_rs.image, NULL, &g_rs.imageView)) &&
        SUCCEEDED(g_rs.image->QueryInterface(IID_PPV_ARGS(&surface))) &&
        SUCCEEDED(g_rs.dc->CreateBitmapFromDxgiSurface(surface, &props, &g_rs.imageTarget));

    SafeRelease(surface);
    g_rs.imageWidth = ok ? width : 0;
    g_rs.imageHeight = ok ? height : 0;
    return ok;
}

static bool RenderComposite(IDXGISwapChain* self)
{
    if (!SetupComposite())
        return false;

    IDXGISwapChain3* swapChain = NULL;
    ID3D12Resource* buffer = NULL;
    ID3D11Resource* wrapped = NULL;
    ID3D11RenderTargetView* view = NULL;
    bool ok = false;

    if (SUCCEEDED(self->QueryInterface(IID_PPV_ARGS(&swapChain))) &&
        SUCCEEDED(swapChain->GetBuffer(swapChain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&buffer))))
    {
        ReadSize(swapChain);
        UINT width = (UINT)g_rs.width, height = (UINT)g_rs.height;

        // 1. The menu into the 8-bit image.
        if (PrepareImage(width, height))
        {
            g_rs.dc->SetTarget(g_rs.imageTarget);
            g_rs.dc->BeginDraw();
            g_rs.dc->Clear(D2D1::ColorF(0, 0, 0, 0));
            OverlayDrawContext ctx = { g_rs.dc, g_rs.write, g_rs.width, g_rs.height, (unsigned)g_generation };
            g_draw(ctx);
            g_rs.dc->EndDraw();
            g_rs.dc->SetTarget(NULL);
        }

        // 2. The image onto the screen, converted for its colour space.
        D3D11_RESOURCE_FLAGS flags = { D3D11_BIND_RENDER_TARGET };
        D3D11_RENDER_TARGET_VIEW_DESC rd = {};
        rd.Format = g_rs.format;
        rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

        if (g_rs.imageView &&
            SUCCEEDED(g_rs.on12->CreateWrappedResource(buffer, &flags, D3D12_RESOURCE_STATE_PRESENT,
                D3D12_RESOURCE_STATE_PRESENT, IID_PPV_ARGS(&wrapped))) &&
            SUCCEEDED(g_rs.d11->CreateRenderTargetView(wrapped, &rd, &view)))
        {
            CompositeSettings settings = {};
            LONG space = g_colorSpace;

            if (g_rs.format == DXGI_FORMAT_R16G16B16A16_FLOAT)
                settings = { 1, HDR_WHITE_NITS / 80.0f };
            else if (space == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
                settings = { 2, HDR_WHITE_NITS / 10000.0f };
            else if (g_rs.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || g_rs.format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
                settings = { 3, 1.0f };

            D3D11_VIEWPORT viewport = { 0, 0, g_rs.width, g_rs.height, 0, 1 };
            ID3D11DeviceContext* c = g_rs.d11Context;
            float factor[4] = { 0, 0, 0, 0 };

            g_rs.on12->AcquireWrappedResources(&wrapped, 1);
            c->UpdateSubresource(g_rs.constants, 0, NULL, &settings, 0, 0);
            c->OMSetRenderTargets(1, &view, NULL);
            c->OMSetBlendState(g_rs.blend, factor, 0xFFFFFFFF);
            c->RSSetViewports(1, &viewport);
            c->IASetInputLayout(NULL);
            c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            c->VSSetShader(g_rs.vs, NULL, 0);
            c->PSSetShader(g_rs.ps, NULL, 0);
            c->PSSetShaderResources(0, 1, &g_rs.imageView);
            c->PSSetSamplers(0, 1, &g_rs.sampler);
            c->PSSetConstantBuffers(0, 1, &g_rs.constants);
            c->Draw(3, 0);

            ID3D11ShaderResourceView* none = NULL;
            c->PSSetShaderResources(0, 1, &none);
            c->OMSetRenderTargets(0, NULL, NULL);
            g_rs.on12->ReleaseWrappedResources(&wrapped, 1);
            c->Flush();
            ok = true;
        }
    }

    SafeRelease(view);
    SafeRelease(wrapped);
    SafeRelease(buffer);
    SafeRelease(swapChain);
    g_rs.d11Context->Flush();
    return ok;
}

// Direct2D draws only on 8-bit screens; HDR formats (RenoDX and other HDR
// mods) and sRGB back buffers go through the composite path.
static void Render(IDXGISwapChain* self)
{
    if ((!g_visible && !g_drawing) || !g_draw)
        return;

    // disable.txt "direct" forces the composite path (for testing it).
    static bool noDirect = PartDisabled("direct");
    bool direct = !noDirect && (g_rs.format == DXGI_FORMAT_R8G8B8A8_UNORM || g_rs.format == DXGI_FORMAT_B8G8R8A8_UNORM);
    static DXGI_FORMAT reported = DXGI_FORMAT_UNKNOWN;

    if (!direct && reported != g_rs.format && !g_rs.compositeFailed)
    {
        reported = g_rs.format;
        Log("overlay: screen format %d (colour space %ld) - drawing through the HDR path", g_rs.format, g_colorSpace);
    }

    if (direct && RenderDirect(self))
        return;

    if (!g_rs.compositeFailed && !RenderComposite(self))
    {
        g_rs.compositeFailed = true;
        Log("overlay: could not draw on screen format %d", g_rs.format);
    }
}

ID2D1Bitmap* OverlayLoadImage(const wchar_t* path)
{
    if (!g_rs.wic || !g_rs.dc)
        return NULL;

    IWICBitmapDecoder* decoder = NULL;
    IWICBitmapFrameDecode* frame = NULL;
    IWICFormatConverter* converter = NULL;
    ID2D1Bitmap* bitmap = NULL;

    if (SUCCEEDED(g_rs.wic->CreateDecoderFromFilename(path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) &&
        SUCCEEDED(g_rs.wic->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom)))
    {
        g_rs.dc->CreateBitmapFromWicBitmap(converter, NULL, &bitmap);
    }

    SafeRelease(converter);
    SafeRelease(frame);
    SafeRelease(decoder);
    return bitmap;
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

// Lets go of every drawing object (the menu makes its own anew, see the
// generation in OverlayDrawContext).
static void TearDown()
{
    if (g_rs.d11Context)
    {
        g_rs.d11Context->ClearState();
        g_rs.d11Context->Flush();
    }

    SafeRelease(g_rs.constants);
    SafeRelease(g_rs.blend);
    SafeRelease(g_rs.sampler);
    SafeRelease(g_rs.ps);
    SafeRelease(g_rs.vs);
    SafeRelease(g_rs.imageTarget);
    SafeRelease(g_rs.imageView);
    SafeRelease(g_rs.image);
    SafeRelease(g_rs.wic);
    SafeRelease(g_rs.write);
    SafeRelease(g_rs.dc);
    SafeRelease(g_rs.d2dDevice);
    SafeRelease(g_rs.d2dFactory);
    SafeRelease(g_rs.on12);
    SafeRelease(g_rs.d11Context);
    SafeRelease(g_rs.d11);
    g_rs = {};
    g_setupAttempts = 0;
    InterlockedIncrement(&g_generation);
}

static void Follow(IDXGISwapChain* self)
{
    // A new swap chain made on another queue (starting a new game does):
    // drawing through the old queue crashed the game on the menu's first
    // frame, so everything is set up again on the new one.
    if (g_creationQueue && g_creationQueue != g_rs.queue)
    {
        Log("overlay: the game's new swap chain uses another queue - setting the drawing up again");
        TearDown();
        return;
    }

    IDXGISwapChain3* swapChain = NULL;
    DXGI_SWAP_CHAIN_DESC desc;

    if (FAILED(self->QueryInterface(IID_PPV_ARGS(&swapChain))) || FAILED(swapChain->GetDesc(&desc)))
    {
        SafeRelease(swapChain);
        return;
    }

    g_rs.swapChain = swapChain;     // kept only to recognise it, without a reference
    swapChain->Release();
    ReadSize(self);

    if (desc.OutputWindow != g_window)
        HookWindow(desc.OutputWindow);

    Log("overlay: following the game's new swap chain (%.0fx%.0f)", g_rs.width, g_rs.height);
}

// Runs before every frame is shown, from Present or Present1 (frame
// generation and some other mods present with Present1).
static void BeforePresent(IDXGISwapChain* self)
{
    AcquireSRWLockExclusive(&g_renderLock);

    if (!g_rs.ready && !g_rs.failed)
    {
        if (Setup(self))
            g_rs.ready = true;
        else if (g_rs.d11)      // got far enough to know it will not work
            g_rs.failed = true;
    }

    // Draw on the swap chain the game presents with. When the game replaces
    // it (starting a new game does), the old one stops presenting and the
    // overlay follows the new one.
    static DWORD lastOwnPresent = 0;
    DWORD now = GetTickCount();

    if (g_rs.ready && (IUnknown*)self != (IUnknown*)g_rs.swapChain && now - lastOwnPresent > 500)
        Follow(self);

    if (g_rs.ready && (IUnknown*)self == (IUnknown*)g_rs.swapChain)
    {
        lastOwnPresent = now;
        Render(self);
    }

    ReleaseSRWLockExclusive(&g_renderLock);
}

// One implementation may call the other; the frame is drawn only once.
static __declspec(thread) int t_presenting = 0;

static HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* self, UINT sync, UINT flags)
{
    if (!t_presenting++)
        BeforePresent(self);

    HRESULT hr = g_originalPresent(self, sync, flags);
    --t_presenting;
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookedPresent1(IDXGISwapChain1* self, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* params)
{
    if (!t_presenting++)
        BeforePresent(self);

    HRESULT hr = g_originalPresent1(self, sync, flags, params);
    --t_presenting;
    return hr;
}

static HRESULT STDMETHODCALLTYPE HookedSetColorSpace1(IDXGISwapChain3* self, DXGI_COLOR_SPACE_TYPE space)
{
    HRESULT hr = g_originalSetColorSpace1(self, space);

    if (SUCCEEDED(hr) && g_colorSpace != (LONG)space)
    {
        InterlockedExchange(&g_colorSpace, (LONG)space);
        Log("overlay: the game set colour space %d", (int)space);
    }

    return hr;
}

static HRESULT STDMETHODCALLTYPE HookedResizeBuffers(IDXGISwapChain* self, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
{
    // Nothing of the swap chain is held between frames; the size is read
    // again on the next frame drawn.
    AcquireSRWLockExclusive(&g_renderLock);
    HRESULT hr = g_originalResizeBuffers(self, count, width, height, format, flags);
    ReleaseSRWLockExclusive(&g_renderLock);
    return hr;
}

static void STDMETHODCALLTYPE HookedExecuteCommandLists(ID3D12CommandQueue* self, UINT count, ID3D12CommandList* const* lists)
{
    if (self->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        t_lastQueue = self;
        bool known = false;
        LONG count = g_queueCount;

        for (LONG i = 0; i < count && !known; ++i)
            known = g_queues[i] == self;

        if (!known && count < MAX_QUEUES)
        {
            AcquireSRWLockExclusive(&g_queueLock);
            count = g_queueCount;
            known = false;

            for (LONG i = 0; i < count && !known; ++i)
                known = g_queues[i] == self;

            if (!known && count < MAX_QUEUES)
            {
                self->AddRef();
                g_queues[count] = self;
                InterlockedExchange(&g_queueCount, count + 1);
            }

            ReleaseSRWLockExclusive(&g_queueLock);
        }
    }

    g_originalExecute(self, count, lists);
}

// Creates a throwaway device and swap chain to find the addresses of the
// DXGI / D3D12 functions every swap chain and queue share.
static bool FindFunctions(void** present, void** present1, void** resize, void** colorSpace, void** execute)
{
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"CharacterCreatorProbe";
    RegisterClassExW(&wc);
    HWND window = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, NULL, NULL, wc.hInstance, NULL);

    bool ok = false;
    ID3D12Device* device = NULL;
    ID3D12CommandQueue* queue = NULL;
    IDXGIFactory4* factory = NULL;
    IDXGISwapChain1* swapChain = NULL;

    if (window &&
        SUCCEEDED(D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) &&
        SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
    {
        D3D12_COMMAND_QUEUE_DESC qd = {};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

        if (SUCCEEDED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))))
        {
            DXGI_SWAP_CHAIN_DESC1 sd = {};
            sd.Width = 64;
            sd.Height = 64;
            sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.SampleDesc.Count = 1;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.BufferCount = 2;
            sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

            if (SUCCEEDED(factory->CreateSwapChainForHwnd(queue, window, &sd, NULL, NULL, &swapChain)))
            {
                void** scVtable = *(void***)swapChain;
                void** qVtable = *(void***)queue;
                *present = scVtable[8];         // IDXGISwapChain::Present
                *resize = scVtable[13];         // IDXGISwapChain::ResizeBuffers
                *present1 = scVtable[22];       // IDXGISwapChain1::Present1
                *colorSpace = scVtable[38];     // IDXGISwapChain3::SetColorSpace1
                *execute = qVtable[10];         // ID3D12CommandQueue::ExecuteCommandLists
                ok = true;
            }
        }
    }

    SafeRelease(swapChain);
    SafeRelease(factory);
    SafeRelease(queue);
    SafeRelease(device);

    if (window)
        DestroyWindow(window);

    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return ok;
}

// ---------------------------------------------------------------------------
// Finding the game's swap chain
// ---------------------------------------------------------------------------
//
// At start the plugin hooks the DXGI factory functions that create swap
// chains (a factory is cheap to create and touches no graphics device). When
// the game creates its swap chain, the overlay hooks that swap chain's
// functions and takes the queue the game passed in.
//
// The older way - creating a throwaway D3D12 device, window and swap chain
// to read the functions from - is only the fallback: on some systems it
// crashed or froze the game at start (other overlays and drivers react to the
// extra swap chain).

typedef HRESULT(STDMETHODCALLTYPE* CreateSwapChainFn)(IDXGIFactory* self, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** out);
typedef HRESULT(STDMETHODCALLTYPE* CreateSwapChainForHwndFn)(IDXGIFactory2* self, IUnknown* device, HWND window,
    const DXGI_SWAP_CHAIN_DESC1* desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen, IDXGIOutput* output, IDXGISwapChain1** out);

static CreateSwapChainFn g_originalCreateSwapChain = NULL;
static CreateSwapChainForHwndFn g_originalCreateSwapChainForHwnd = NULL;
static volatile LONG g_swapChainHooked = 0;     // Present & co. hooked (by either way)
static bool g_watching = false;                 // the factory hooks are in place

static bool HookSwapChainFunctions(void** vtable)
{
    void* present = vtable[8];          // IDXGISwapChain::Present
    void* resize = vtable[13];          // IDXGISwapChain::ResizeBuffers
    void* present1 = vtable[22];        // IDXGISwapChain1::Present1
    void* colorSpace = vtable[38];      // IDXGISwapChain3::SetColorSpace1

    if (MH_CreateHook(present, (void*)&HookedPresent, (void**)&g_originalPresent) != MH_OK ||
        MH_CreateHook(resize, (void*)&HookedResizeBuffers, (void**)&g_originalResizeBuffers) != MH_OK ||
        MH_EnableHook(present) != MH_OK || MH_EnableHook(resize) != MH_OK)
        return false;

    // Optional: without them the menu still works on most setups.
    if (MH_CreateHook(present1, (void*)&HookedPresent1, (void**)&g_originalPresent1) != MH_OK || MH_EnableHook(present1) != MH_OK)
        Log("overlay: could not hook Present1");

    if (MH_CreateHook(colorSpace, (void*)&HookedSetColorSpace1, (void**)&g_originalSetColorSpace1) != MH_OK ||
        MH_EnableHook(colorSpace) != MH_OK)
        Log("overlay: could not hook SetColorSpace1");

    return true;
}

static void OnSwapChainCreated(IUnknown* device, IUnknown* swapChain)
{
    ID3D12CommandQueue* queue = NULL;

    // A D3D12 swap chain is created with the command queue that presents it.
    if (!device || FAILED(device->QueryInterface(IID_PPV_ARGS(&queue))))
        return;

    IDXGISwapChain3* chain3 = NULL;

    if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&chain3))))
    {
        queue->Release();
        return;
    }

    InterlockedExchangePointer((void* volatile*)&g_creationQueue, queue);    // kept (the game keeps it too)

    if (InterlockedCompareExchange(&g_swapChainHooked, 1, 0) == 0)
    {
        if (HookSwapChainFunctions(*(void***)chain3))
            Log("overlay: hooked the game's swap chain (queue %p)", queue);
        else
        {
            InterlockedExchange(&g_swapChainHooked, 0);
            Log("overlay: could not hook the game's swap chain");
        }
    }

    chain3->Release();
}

static HRESULT STDMETHODCALLTYPE HookedCreateSwapChain(IDXGIFactory* self, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** out)
{
    HRESULT hr = g_originalCreateSwapChain(self, device, desc, out);

    if (SUCCEEDED(hr) && out && *out)
        OnSwapChainCreated(device, *out);

    return hr;
}

static HRESULT STDMETHODCALLTYPE HookedCreateSwapChainForHwnd(IDXGIFactory2* self, IUnknown* device, HWND window,
    const DXGI_SWAP_CHAIN_DESC1* desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen, IDXGIOutput* output, IDXGISwapChain1** out)
{
    HRESULT hr = g_originalCreateSwapChainForHwnd(self, device, window, desc, fullscreen, output, out);

    if (SUCCEEDED(hr) && out && *out)
        OnSwapChainCreated(device, *out);

    return hr;
}

// ReShade (and similar tools) install their own dxgi.dll in the game folder;
// its factory is a wrapper, and hooking the wrapper's functions misses the
// real swap chain. The factory comes from Windows' own dxgi.dll instead, so
// the real functions are hooked - the wrappers call them in the end.
static HRESULT CreateSystemFactory(IDXGIFactory2** factory)
{
    typedef HRESULT(WINAPI* CreateFactoryFn)(REFIID, void**);
    char path[MAX_PATH];
    UINT length = GetSystemDirectoryA(path, MAX_PATH);
    HMODULE dxgi = NULL;

    if (length && length < MAX_PATH - 10)
    {
        strcat_s(path, "\\dxgi.dll");
        dxgi = LoadLibraryA(path);
    }

    CreateFactoryFn create = dxgi ? (CreateFactoryFn)GetProcAddress(dxgi, "CreateDXGIFactory1") : NULL;

    if (!create)
    {
        Log("overlay: Windows' dxgi.dll not found - using the one the game loads");
        return CreateDXGIFactory1(IID_PPV_ARGS(factory));
    }

    if (GetModuleHandleA("dxgi.dll") != dxgi)
        Log("overlay: the game folder has its own dxgi.dll (ReShade or similar) - hooking Windows' dxgi.dll underneath it");

    return create(__uuidof(IDXGIFactory2), (void**)factory);
}

bool OverlayEarlyInit()
{
    IDXGIFactory2* factory = NULL;

    if (FAILED(CreateSystemFactory(&factory)))
    {
        Log("overlay: could not create a DXGI factory");
        return false;
    }

    void** vtable = *(void***)factory;
    void* createSwapChain = vtable[10];         // IDXGIFactory::CreateSwapChain
    void* createForHwnd = vtable[15];           // IDXGIFactory2::CreateSwapChainForHwnd
    factory->Release();

    MH_STATUS init = MH_Initialize();    // other modules may have started MinHook already

    if ((init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) ||
        MH_CreateHook(createSwapChain, (void*)&HookedCreateSwapChain, (void**)&g_originalCreateSwapChain) != MH_OK ||
        MH_CreateHook(createForHwnd, (void*)&HookedCreateSwapChainForHwnd, (void**)&g_originalCreateSwapChainForHwnd) != MH_OK ||
        MH_EnableHook(createSwapChain) != MH_OK || MH_EnableHook(createForHwnd) != MH_OK)
    {
        Log("overlay: could not watch for the game's swap chain");
        return false;
    }

    g_watching = true;
    Log("overlay: watching for the game's swap chain");
    return true;
}

bool OverlayInit()
{
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    if (g_swapChainHooked)
    {
        Log("overlay: DirectX hooks installed (from the game's swap chain)");
        return true;
    }

    // Slower systems create the swap chain later; it is hooked whenever that
    // happens. The probe fallback crashed the game on some systems, and
    // running both hooked Present twice.
    if (g_watching)
    {
        Log("overlay: waiting for the game to create its swap chain");
        return true;
    }

    // The factory could not be watched: the probe is the only way left.
    if (InterlockedCompareExchange(&g_swapChainHooked, 1, 0) != 0)
        return true;

    Log("overlay: could not watch for the game's swap chain - using the fallback");
    void* present = NULL;
    void* present1 = NULL;
    void* resize = NULL;
    void* colorSpace = NULL;
    void* execute = NULL;

    if (!FindFunctions(&present, &present1, &resize, &colorSpace, &execute))
    {
        Log("overlay: could not find the DirectX functions");
        return false;
    }

    MH_STATUS init = MH_Initialize();    // other modules may have started MinHook already

    if ((init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) ||
        MH_CreateHook(present, (void*)&HookedPresent, (void**)&g_originalPresent) != MH_OK ||
        MH_CreateHook(resize, (void*)&HookedResizeBuffers, (void**)&g_originalResizeBuffers) != MH_OK ||
        MH_CreateHook(execute, (void*)&HookedExecuteCommandLists, (void**)&g_originalExecute) != MH_OK ||
        MH_EnableHook(present) != MH_OK || MH_EnableHook(resize) != MH_OK || MH_EnableHook(execute) != MH_OK)
    {
        Log("overlay: hooking DirectX failed");
        return false;
    }

    // Optional: without them the menu still works on most setups.
    if (MH_CreateHook(present1, (void*)&HookedPresent1, (void**)&g_originalPresent1) != MH_OK || MH_EnableHook(present1) != MH_OK)
        Log("overlay: could not hook Present1");

    if (MH_CreateHook(colorSpace, (void*)&HookedSetColorSpace1, (void**)&g_originalSetColorSpace1) != MH_OK ||
        MH_EnableHook(colorSpace) != MH_OK)
        Log("overlay: could not hook SetColorSpace1");

    Log("overlay: DirectX hooks installed");
    return true;
}

void OverlaySetCallbacks(OverlayDrawFn draw, OverlayKeyFn key)
{
    g_draw = draw;
    g_key = key;
}

void OverlaySetVisible(bool visible)
{
    InterlockedExchange(&g_visible, visible ? 1 : 0);
}

bool OverlayVisible()
{
    return g_visible != 0;
}

void OverlaySetDrawing(bool drawing)
{
    InterlockedExchange(&g_drawing, drawing ? 1 : 0);
}
