/*==============================================================================================================================================
                                                            IMGUIPLATFORMRELAY.CPP
==============================================================================================================================================*/
// 🧩 Win32 realization of the ImGui OS-half attach point. Attach calls ImGui_ImplWin32_Init on the window's HWND (recovered as the opaque native
//    handle) and installs a message observer that forwards every OS message into ImGui_ImplWin32_WndProcHandler. The observer returns 0 (does not
//    consume): ImGui's handler updates its own input state, but the native window proc still runs its default handling for the message, exactly as
//    the old imgui_impl_glfw install-callbacks path coexisted with the window. App logic that must yield to a hovered widget reads ImGui's
//    WantCaptureMouse / WantCaptureKeyboard, not a suppressed message. Compiled on Windows only; the stub below is the floor for every other
//    platform until a native Linux ImGui relay (fed from InputSnapshot) lands.

#include "ImguiPlatformRelay.h"

#include "Platform/Windowing/PlatformWindow.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"

// The ImGui Win32 backend exports this raw-message handler; the header declares it with Windows types. We forward the untyped
// observer quadruple into it, casting back to the OS types here (the only place this .cpp reaches ImGui's OS half).
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND Window, UINT Message, WPARAM WordParam, LPARAM MessageData);

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // The message observer the window calls first for every OS message. Forward it into ImGui's handler so ImGui tracks hover /
    // click / wheel / key / char / focus, then return 0 so the window proc still runs its own default handling (coexistence, not
    // capture). ObserverContext is unused on Win32 — ImGui's backend keeps its state in the current ImGui context.
    int ImguiMessageObserver(void*    /*ObserverContext*/,
                             void*    NativeWindowHandle,
                             uint32_t NativeMessage,
                             uint64_t NativeWordParam,
                             int64_t  NativeMessageData)
    {
        ImGui_ImplWin32_WndProcHandler(reinterpret_cast<HWND>(NativeWindowHandle),
                                       (UINT)NativeMessage,
                                       (WPARAM)NativeWordParam,
                                       (LPARAM)NativeMessageData);
        return 0;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool AttachImguiPlatform(PlatformWindow& Window)
{
    HWND NativeWindow = reinterpret_cast<HWND>(QueryNativeWindowHandle(Window));
    if (NativeWindow == nullptr)
        return false;

    if (!ImGui_ImplWin32_Init(NativeWindow))
        return false;

    // 📝 The Win32 backend advertises multi-viewport support by default, which makes imgui_impl_vulkan assert for a
    //    Platform_CreateVkSurface handler at first frame. Frontier renders one OS window (no OS-native secondary viewports),
    //    so clear the platform-viewport backend flag — the single-window swapchain the VulkanImguiInterface owns is all we drive.
    ImGui::GetIO().BackendFlags &= ~ImGuiBackendFlags_PlatformHasViewports;

    InstallMessageObserver(Window, &ImguiMessageObserver, nullptr);
    return true;
}

void AdvanceImguiPlatform()
{
    ImGui_ImplWin32_NewFrame();
}

void DetachImguiPlatform(PlatformWindow& Window)
{
    InstallMessageObserver(Window, nullptr, nullptr);
    ImGui_ImplWin32_Shutdown();
}

}   // namespace Frontier

#else   // !_WIN32 — floor for platforms without a native ImGui OS backend yet.

namespace Frontier
{

bool AttachImguiPlatform(PlatformWindow& /*Window*/)
{
    return false;
}

void AdvanceImguiPlatform()
{
}

void DetachImguiPlatform(PlatformWindow& /*Window*/)
{
}

}   // namespace Frontier

#endif  // _WIN32
