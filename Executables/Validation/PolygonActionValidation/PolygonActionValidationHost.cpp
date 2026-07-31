/*==============================================================================================================================================
                                                    POLYGONACTIONVALIDATIONHOST.CPP
==============================================================================================================================================*/
// 🧩 The standalone window for the polygon-mutation menu validation. Win32 + Direct3D 11, linking ONLY EngineContext.lib plus the vendored ImGui it
//    compiles itself — no Graphics pillar, no Vulkan, no RootSystem. That constraint is why the menu's glyphs are ImDrawList vector paths rather than
//    SvgIconRegistry textures: the registry is Vulkan-bound and could not be reached from here. Validation hosts keep the proven D3D11 backend for
//    quick eyeballing; the editors run on the native Vulkan substrate.

#include "PolygonActionPanel.h"

#include "EngineContext/Interface/Theme/ThemeResolver.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include <d3d11.h>
#include <tchar.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace Frontier;


//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL STATE
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    ID3D11Device*           g_Device        = nullptr;
    ID3D11DeviceContext*    g_DeviceContext = nullptr;
    IDXGISwapChain*         g_SwapChain     = nullptr;
    ID3D11RenderTargetView* g_RenderTarget  = nullptr;

    void CreateRenderTarget()
    {
        ID3D11Texture2D* BackBuffer = nullptr;
        g_SwapChain->GetBuffer(0, IID_PPV_ARGS(&BackBuffer));
        if (BackBuffer != nullptr)
        {
            g_Device->CreateRenderTargetView(BackBuffer, nullptr, &g_RenderTarget);
            BackBuffer->Release();
        }
    }

    void CleanupRenderTarget()
    {
        if (g_RenderTarget != nullptr) { g_RenderTarget->Release(); g_RenderTarget = nullptr; }
    }

    bool CreateDeviceD3D(HWND Window)
    {
        DXGI_SWAP_CHAIN_DESC Description = {};
        Description.BufferCount                        = 2;
        Description.BufferDesc.Width                   = 0;
        Description.BufferDesc.Height                  = 0;
        Description.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
        Description.BufferDesc.RefreshRate.Numerator   = 60;
        Description.BufferDesc.RefreshRate.Denominator = 1;
        Description.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        Description.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        Description.OutputWindow                       = Window;
        Description.SampleDesc.Count                   = 1;
        Description.SampleDesc.Quality                 = 0;
        Description.Windowed                           = TRUE;
        Description.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

        UINT              Flags        = 0;
        D3D_FEATURE_LEVEL FeatureLevel = D3D_FEATURE_LEVEL_11_0;
        const D3D_FEATURE_LEVEL LevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

        HRESULT Result = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, Flags, LevelArray, 2, D3D11_SDK_VERSION,
            &Description, &g_SwapChain, &g_Device, &FeatureLevel, &g_DeviceContext);
        if (Result == DXGI_ERROR_UNSUPPORTED)   // fall back to WARP for machines without a hardware D3D11 device
        {
            Result = D3D11CreateDeviceAndSwapChain(
                nullptr, D3D_DRIVER_TYPE_WARP, nullptr, Flags, LevelArray, 2, D3D11_SDK_VERSION,
                &Description, &g_SwapChain, &g_Device, &FeatureLevel, &g_DeviceContext);
        }
        if (Result != S_OK)
        {
            return false;
        }

        CreateRenderTarget();
        return true;
    }

    void CleanupDeviceD3D()
    {
        CleanupRenderTarget();
        if (g_SwapChain     != nullptr) { g_SwapChain->Release();     g_SwapChain     = nullptr; }
        if (g_DeviceContext != nullptr) { g_DeviceContext->Release(); g_DeviceContext = nullptr; }
        if (g_Device        != nullptr) { g_Device->Release();        g_Device        = nullptr; }
    }
}

// 📝 The Win32 backend's message handler is declared inside a #if 0 block in imgui_impl_win32.h (to keep <windows.h> out of that header);
//    the canonical usage is to forward-declare it here in the app .cpp.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT WINAPI WindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    if (ImGui_ImplWin32_WndProcHandler(Window, Message, WParam, LParam))
    {
        return true;
    }

    switch (Message)
    {
    case WM_SIZE:
        if (g_Device != nullptr && WParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_SwapChain->ResizeBuffers(0, (UINT)LOWORD(LParam), (UINT)HIWORD(LParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((WParam & 0xFFF0) == SC_KEYMENU)   // swallow the Alt "application menu" beep
        {
            return 0;
        }
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProc(Window, Message, WParam, LParam);
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main(int, char**)
{
    // -- Window --------------------------------------------------------------------------------------------------------
    WNDCLASSEXW WindowClass = { sizeof(WindowClass), CS_CLASSDC, WindowProc, 0L, 0L,
                                GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr,
                                L"FrontierPolygonActionValidation", nullptr };
    ::RegisterClassExW(&WindowClass);
    HWND Window = ::CreateWindowW(WindowClass.lpszClassName, L"Frontier - Polygon Action Menu",
                                  WS_OVERLAPPEDWINDOW, 100, 60, 1280, 940,
                                  nullptr, nullptr, WindowClass.hInstance, nullptr);

    if (!CreateDeviceD3D(Window))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(WindowClass.lpszClassName, WindowClass.hInstance);
        return 1;
    }

    ::ShowWindow(Window, SW_SHOWDEFAULT);
    ::UpdateWindow(Window);

    // -- ImGui ---------------------------------------------------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& Io = ImGui::GetIO();
    Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    Io.IniFilename = nullptr;   // don't litter an imgui.ini next to the exe — this is a throwaway validation

    ImGui_ImplWin32_Init(Window);
    ImGui_ImplDX11_Init(g_Device, g_DeviceContext);

    // 📝 Resolve the shared theme once and mirror it into ImGui's style so nested raw widgets inherit the palette.
    const ThemeConfiguration Theme = ResolveActiveTheme();
    EnforceThemeStyle(Theme);

    PolygonActionState State;

    // -- Frame loop ----------------------------------------------------------------------------------------------------
    bool Running = true;
    while (Running)
    {
        MSG Message;
        while (::PeekMessage(&Message, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&Message);
            ::DispatchMessage(&Message);
            if (Message.message == WM_QUIT)
            {
                Running = false;
            }
        }
        if (!Running)
        {
            break;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // 📝 One full-viewport host window. NoBringToFrontOnFocus matters here: the menu card is a separate floating window, and
        //    without it the host would jump above the card whenever the host itself was clicked.
        const ImGuiViewport* Viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(Viewport->WorkPos);
        ImGui::SetNextWindowSize(Viewport->WorkSize);
        const ImGuiWindowFlags HostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme.Palette.DeskBackground);
        if (ImGui::Begin("Polygon Action Menu", nullptr, HostFlags))
        {
            ConstructPolygonActionPanel(Theme, State);
        }
        ImGui::End();
        ImGui::PopStyleColor();

        ImGui::Render();

        const float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };   // --bg #000
        g_DeviceContext->OMSetRenderTargets(1, &g_RenderTarget, nullptr);
        g_DeviceContext->ClearRenderTargetView(g_RenderTarget, ClearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_SwapChain->Present(1, 0);   // vsync
    }

    // -- Teardown ------------------------------------------------------------------------------------------------------
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(Window);
    ::UnregisterClassW(WindowClass.lpszClassName, WindowClass.hInstance);
    return 0;
}
