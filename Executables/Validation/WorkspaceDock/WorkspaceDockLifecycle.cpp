/*==============================================================================================================================================
                                                            WORKSPACEDOCKLIFECYCLE.CPP
==============================================================================================================================================*/
// 🧩 The whole standalone window for the workspace-dock validation app. Win32 + Direct3D 11 host (both backends vendored in
//    ExternalPackages/imgui, so NOTHING outside imgui + EngineContext.lib is linked — fully decoupled from Editor / TexturePaint / etc.). Resolves
//    the shared theme once, mirrors it into ImGui's style (so the (+) dropdown inherits the palette), and drives the SHARED
//    ConstructWorkspacePanelDock (from EngineContext.lib) each frame. That panel owns the ENTIRE dock model itself — a self-owned partition tree
//    drawn on the foreground draw list, NOT ImGui's DockSpace — so main() does NOT enable the docking branch and does NOT wrap the panel in a
//    Begin; it just calls it. Deliberately minimal: no RootSystem, no ApplicationConfiguration, no engine spine — just enough to see the real
//    dock chrome behave. Named WorkspaceDockLifecycle to avoid colliding with the pillar's WorkspaceHost/WorkspaceDockHost translation unit.

#include "EngineContext/Interface/WorkspaceHost/WorkspacePanelDock.h"

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
//    the canonical usage is to forward-declare it here in the app .cpp. It handles ImGui's share of the message pump.
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
                                L"FrontierWorkspaceDock", nullptr };
    ::RegisterClassExW(&WindowClass);
    HWND Window = ::CreateWindowW(WindowClass.lpszClassName, L"Frontier - Workspace Dock",
                                  WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800,
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
    Io.IniFilename  = nullptr;                          // don't litter an imgui.ini next to the exe — this is a throwaway validation

    ImGui_ImplWin32_Init(Window);
    ImGui_ImplDX11_Init(g_Device, g_DeviceContext);

    // 📝 Resolve the shared theme once and mirror it into ImGui's style so nested raw widgets inherit the palette.
    const ThemeConfiguration Theme = ResolveActiveTheme();
    EnforceThemeStyle(Theme);

    WorkspacePanelDock State;
    InitializeWorkspacePanelDock(State);

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

        // 📝 The panel owns its own host window + DockSpace + trapezoid strip — main() just drives it once per frame.
        ConstructWorkspacePanelDock(Theme, State);

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
