/*==========================================================================================================================================
                                                               WINDOWSUI.CPP
==========================================================================================================================================*/
// 🧩 Host the native UI in a GLFW window with a DX11 ImGui adapter; primary engine rendering remains Vulkan/Slang.

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <d3d11.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_dx11.h>
#include "UI/Editor/ControlCentrePanel.h"
#include "Engine/Interface/IconPresentation.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                    DISPLAY RESOURCES
//------------------------------------------------------------------------------------------------------------------------

struct DisplayResources
{
    ID3D11Device           *Device    = nullptr;
    ID3D11DeviceContext    *Context   = nullptr;
    IDXGISwapChain         *SwapChain = nullptr;
    ID3D11RenderTargetView *Target    = nullptr;

    ~DisplayResources()
    {
        if (Target)
            Target->Release();
        if (SwapChain)
            SwapChain->Release();
        if (Context)
            Context->Release();
        if (Device)
            Device->Release();
    }

    bool ConstructTarget()
    {
        ID3D11Texture2D *BackBuffer = nullptr;
        if (FAILED(SwapChain->GetBuffer(0, IID_PPV_ARGS(&BackBuffer))))
            return false;
        const auto Result = Device->CreateRenderTargetView(BackBuffer, nullptr, &Target);
        BackBuffer->Release();
        return SUCCEEDED(Result);
    }

    bool Initialize(HWND Window)
    {
        DXGI_SWAP_CHAIN_DESC Description{};
        Description.BufferCount       = 2;
        Description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        Description.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        Description.OutputWindow      = Window;
        Description.SampleDesc.Count  = 1;
        Description.Windowed          = TRUE;
        Description.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;
        D3D_FEATURE_LEVEL           Level;
        constexpr D3D_FEATURE_LEVEL Levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
        auto                        Result =
            D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, Levels, 2, D3D11_SDK_VERSION,
                                          &Description, &SwapChain, &Device, &Level, &Context);
        if (Result == DXGI_ERROR_UNSUPPORTED)
            Result =
                D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, Levels, 2, D3D11_SDK_VERSION,
                                              &Description, &SwapChain, &Device, &Level, &Context);
        if (FAILED(Result))
            return false;
        IDXGIFactory *Factory = nullptr;
        if (SUCCEEDED(SwapChain->GetParent(IID_PPV_ARGS(&Factory))))
        {
            Factory->MakeWindowAssociation(Window, DXGI_MWA_NO_ALT_ENTER);
            Factory->Release();
        }
        return ConstructTarget();
    }

    bool Reconfigure(int Width, int Height)
    {
        Context->OMSetRenderTargets(0, nullptr, nullptr);
        if (Target)
        {
            Target->Release();
            Target = nullptr;
        }
        return SUCCEEDED(SwapChain->ResizeBuffers(0, Width, Height, DXGI_FORMAT_UNKNOWN, 0)) && ConstructTarget();
    }
};

void ReportGlfwError(int Code, const char *Description)
{
    std::cerr << "GLFW " << Code << ": " << Description << '\n';
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      WINDOWS HOST
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    glfwSetErrorCallback(ReportGlfwError);
    if (!glfwInit())
        return 1;
    // No OpenGL context: this window can host Vulkan; this exhibit alone attaches a DX11 display adapter.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    GLFWwindow *Window = glfwCreateWindow(1280, 900, "Frontier - Control Centre", nullptr, nullptr);
    if (!Window)
    {
        glfwTerminate();
        return 1;
    }
    int ExitCode = 0;
    {
        DisplayResources Display;
        if (!Display.Initialize(glfwGetWin32Window(Window)))
            ExitCode = 1;
        else
        {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            auto &IO = ImGui::GetIO();
            IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
            IO.IniFilename           = nullptr;
            const bool PlatformReady = ImGui_ImplGlfw_InitForOther(Window, true);
            const bool DisplayReady  = ImGui_ImplDX11_Init(Display.Device, Display.Context);
            Frontier::IconPresentation::Attach("EngineContent/Icons");
            const wchar_t                *LocalDirectory = _wgetenv(L"LOCALAPPDATA");
            const auto                    Preferences    = LocalDirectory
                                                               ? std::filesystem::path(LocalDirectory) / L"Frontier/Appearance.toml"
                                                               : std::filesystem::path{};
            Frontier::ActivationOwnership Ownership(
                new Frontier::ControlCentrePanel("EngineContent/FontArchives", Preferences));
            auto *Panel = static_cast<Frontier::ControlCentrePanel *>(Ownership.get());
            if (!PlatformReady || !DisplayReady || !Panel->Initialize() || !Panel->Activate())
            {
                std::cerr << "UI initialization failed: " << Panel->QueryDiagnostic() << '\n';
                ExitCode = 1;
            }
            else
            {
                int PreviousWidth  = 0;
                int PreviousHeight = 0;
                while (!glfwWindowShouldClose(Window))
                {
                    glfwPollEvents();
                    int Width, Height;
                    glfwGetFramebufferSize(Window, &Width, &Height);
                    if (Width == 0 || Height == 0)
                    {
                        glfwWaitEventsTimeout(0.05);
                        continue;
                    }
                    if (Width != PreviousWidth || Height != PreviousHeight)
                    {
                        if (!Display.Reconfigure(Width, Height))
                        {
                            ExitCode = 1;
                            break;
                        }
                        PreviousWidth  = Width;
                        PreviousHeight = Height;
                    }
                    float DensityX, DensityY;
                    glfwGetWindowContentScale(Window, &DensityX, &DensityY);
                    ImGui::GetStyle().FontScaleDpi = DensityY;
                    Panel->PrepareInterface();
                    ImGui_ImplDX11_NewFrame();
                    ImGui_ImplGlfw_NewFrame();
                    ImGui::NewFrame();
                    (void)Panel->Integrate(IO.DeltaTime);
                    Panel->ConstructInterface();
                    ImGui::Render();
                    constexpr float Background[] = {0.065f, 0.065f, 0.07f, 1};
                    Display.Context->OMSetRenderTargets(1, &Display.Target, nullptr);
                    Display.Context->ClearRenderTargetView(Display.Target, Background);
                    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                    if (FAILED(Display.SwapChain->Present(1, 0)))
                    {
                        ExitCode = 1;
                        break;
                    }
                }
            }
            Ownership.reset();
            if (DisplayReady)
                ImGui_ImplDX11_Shutdown();
            if (PlatformReady)
                ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        }
    }
    glfwDestroyWindow(Window);
    glfwTerminate();
    return ExitCode;
}
