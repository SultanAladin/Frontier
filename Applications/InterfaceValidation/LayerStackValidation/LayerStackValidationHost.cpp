/*==============================================================================================================================================
                                                        LAYERSTACKVALIDATIONHOST.CPP
==============================================================================================================================================*/
// 🧩 Standalone validation host for the 3D Texture paint layer stack panel (1:1 port of LayerStackR2.html).
//    Runs a native Win32 + Direct3D 11 window displaying LayerStackPanel driven by LayerStackModel.

#include "../../../Internal/Interface/Components/LayerStackPanel.h"

#include "../../../Internal/Interface/Theme/ThemeResolver.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include <d3d11.h>
#include <tchar.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL STATE
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    ID3D11Device*           g_Device          = nullptr;
    ID3D11DeviceContext*    g_DeviceContext   = nullptr;
    IDXGISwapChain*         g_SwapChain       = nullptr;
    ID3D11RenderTargetView* g_RenderTarget    = nullptr;

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

        UINT               Flags        = 0;
        D3D_FEATURE_LEVEL  FeatureLevel = D3D_FEATURE_LEVEL_11_0;
        const D3D_FEATURE_LEVEL LevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

        HRESULT Result = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, Flags, LevelArray, 2, D3D11_SDK_VERSION,
            &Description, &g_SwapChain, &g_Device, &FeatureLevel, &g_DeviceContext);
        if (Result == DXGI_ERROR_UNSUPPORTED)
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
        if ((WParam & 0xFFF0) == SC_KEYMENU)
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
    WNDCLASSEXW WindowClass = { sizeof(WindowClass), CS_CLASSDC, WindowProc, 0L, 0L,
                                GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr,
                                L"FrontierLayerStackValidation", nullptr };
    ::RegisterClassExW(&WindowClass);
    HWND Window = ::CreateWindowW(WindowClass.lpszClassName, L"Frontier - Layer Stack Validation",
                                  WS_OVERLAPPEDWINDOW, 100, 100, 420, 850,
                                  nullptr, nullptr, WindowClass.hInstance, nullptr);

    if (!CreateDeviceD3D(Window))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(WindowClass.lpszClassName, WindowClass.hInstance);
        return 1;
    }

    ::ShowWindow(Window, SW_SHOWDEFAULT);
    ::UpdateWindow(Window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& Io = ImGui::GetIO();
    Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    Io.IniFilename = nullptr;

    ImGui_ImplWin32_Init(Window);
    ImGui_ImplDX11_Init(g_Device, g_DeviceContext);

    const ThemeConfiguration Theme = ResolveActiveTheme();
    EnforceThemeStyle(Theme);

    LayerStackModel Model;
    InitializeLayerStackModel(Model);

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

        const ImGuiViewport* Viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(Viewport->WorkPos);
        ImGui::SetNextWindowSize(Viewport->WorkSize);
        const ImGuiWindowFlags HostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme.Palette.DeskBackground);
        if (ImGui::Begin("Layer Stack Validation Host", nullptr, HostFlags))
        {
            ConstructLayerStackPanel(Theme, Model);
        }
        ImGui::End();
        ImGui::PopStyleColor();

        ImGui::Render();

        const float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        g_DeviceContext->OMSetRenderTargets(1, &g_RenderTarget, nullptr);
        g_DeviceContext->ClearRenderTargetView(g_RenderTarget, ClearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_SwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(Window);
    ::UnregisterClassW(WindowClass.lpszClassName, WindowClass.hInstance);
    return 0;
}
