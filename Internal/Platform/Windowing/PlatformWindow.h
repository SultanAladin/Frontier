/*==============================================================================================================================================
                                                              PLATFORMWINDOW.H
==============================================================================================================================================*/
// 🧩 The native windowing seam — one decorated OS window, its Vulkan WSI surface, the OS event pump, monotonic timing, and one poll of decoded
//    input (InputPacket), with NO windowing-library dependency. This replaces GLFW: each platform provides its own backend behind this one
//    header (Win32 real; X11 + Wayland real but built only on Linux; Apple / Android / iOS a linkable stub). The design mirrors the raw-input
//    seam that preceded it — one interface, a backend .cpp per platform guarded by the platform macro, a stub floor everywhere else — so the
//    code above the seam (swapchain, camera, UI) is identical on every target. Callers include only <vulkan/vulkan.h> and this header; no OS
//    header, no GLFW header. See EngineDocs/PLAN-Windowing.md for the resize-backstop and minimize-skip contract this preserves.

#pragma once
#ifndef FRONTIER_PLATFORM_WINDOWING_PLATFORMWINDOW_H
#define FRONTIER_PLATFORM_WINDOWING_PLATFORMWINDOW_H

#include <vulkan/vulkan.h>
#include <cstdint>

#include "EngineContext/Input/InputPacket.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The UI message-observer signature. A UI layer (ImGui) that needs the raw OS messages installs one of these on the window;
//    the backend calls it FIRST for every native message, before its own handling, passing the untyped OS message quadruple
//    (on Win32: HWND, UINT, WPARAM, LPARAM as uintptr_t) plus the observer's own context. A non-zero return means the observer
//    consumed the message and the backend suppresses its default handling for it. The signature is OS-neutral (all-integer) so
//    this header never names an OS type; the ImGui apps cast the quadruple back inside their hook. Platform.lib itself never
//    links ImGui — the coupling lives only in the app that installs the hook.
using PlatformMessageObserver = int (*)(void*    ObserverContext,
                                        void*    NativeWindowHandle,
                                        uint32_t NativeMessage,
                                        uint64_t NativeWordParam,
                                        int64_t  NativeMessageData);


// 📝 One native window plus everything the render loop and camera read off it. NativeState is an opaque per-platform record
//    (the Win32 backend stores its HWND / HINSTANCE / raw-input accumulators there; other backends their own) so this struct
//    stays free of OS types and portable. RebuildRequested / MinimizedCondition are the resize-backstop and minimize-skip the
//    old WindowContext carried: the OS resize / iconify path raises them and the render loop reads them, so a stale swapchain
//    is rebuilt and zero-extent polls are skipped without trusting the driver to report VK_ERROR_OUT_OF_DATE_KHR. Input holds
//    this poll's decoded InputPacket, refilled by PollPlatformEvents. MessageObserver / MessageObserverContext are the optional
//    UI-layer tap described above (null when no UI observes the window).
struct PlatformWindow
{
    void*         NativeState          = nullptr;          // [-]  - Opaque per-platform record (owned by the backend)
    VkSurfaceKHR  PresentationSurface  = VK_NULL_HANDLE;   // [-]  - Vulkan WSI surface bound to the window
    uint32_t      FramebufferWidth     = 0;                // [px] - Current framebuffer width
    uint32_t      FramebufferHeight    = 0;                // [px] - Current framebuffer height

    bool          RebuildRequested     = false;            // [-]  - Surface changed size; the swapchain is stale (resize backstop)
    bool          MinimizedCondition   = false;            // [-]  - Window is iconified; the loop must skip frames
    bool          CloseRequested       = false;            // [-]  - User asked the window to close
    bool          FullscreenEnabled    = false;            // [-]  - Borderless-fullscreen toggle state

    PlatformMessageObserver MessageObserver        = nullptr;   // [-] - Optional UI-layer message tap (see above)
    void*                   MessageObserverContext = nullptr;   // [-] - Opaque context passed back to the observer

    InputPacket   Input;                                   // [-]  - This poll's decoded device input (refilled each poll)
};


//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Create a decorated, resizable window titled TitleText at the requested size, register the OS raw-input taps, and seed the
// framebuffer extent. Returns false with the window left safely finalizable on any failure (unimplemented platform, OS error).
bool InitializePlatformWindow(PlatformWindow& Window,
                              const char*     TitleText,
                              uint32_t        RequestedWidth,
                              uint32_t        RequestedHeight);

// Construct the Vulkan WSI surface for the window (VK_KHR_*_surface for the platform); stores it in PresentationSurface.
bool ConstructPresentationSurface(PlatformWindow& Window, VkInstance VulkanInstance);

// Report the instance extensions this platform needs for surface creation — always VK_KHR_surface plus the platform surface
// extension. The returned pointer is to storage owned by the backend, valid for the process; ExtensionCount receives the count.
const char** QueryRequiredInstanceExtensions(uint32_t& ExtensionCount);

// Refresh FramebufferWidth / FramebufferHeight from the live window and return them.
void QueryFramebufferExtent(PlatformWindow& Window, uint32_t& Width, uint32_t& Height);

// A monotonic clock in seconds, independent of any window (used for poll-to-poll timing). Replaces glfwGetTime.
double QueryMonotonicSeconds();

// Pump the OS event queue once (non-blocking): drains messages, updates RebuildRequested / MinimizedCondition / CloseRequested,
// resolves the F11 fullscreen toggle, and refills Window.Input with this poll's decoded InputPacket (rolling its delta / scroll
// accumulators to zero after the reader consumed the previous poll).
void PollPlatformEvents(PlatformWindow& Window);

// Report whether the user has requested the window to close (mirrors Window.CloseRequested for call-site symmetry).
bool QueryWindowCloseRequested(const PlatformWindow& Window);

// Retrieve the opaque native window handle (on Win32 the HWND). Returned as void* so this header names no OS type; a UI layer
// that stands up its own OS backend (ImGui_ImplWin32_Init) casts it back. Null until the window is created / after finalize.
void* QueryNativeWindowHandle(const PlatformWindow& Window);

// Install (or clear, with null) the UI-layer message observer. Once installed, the backend invokes Observer FIRST for every OS
// message with the untyped message quadruple; a non-zero return suppresses the backend's default handling for that message.
void InstallMessageObserver(PlatformWindow& Window, PlatformMessageObserver Observer, void* ObserverContext);

// Destroy the window and release all OS resources (raw-input taps, window class). The Vulkan surface must already be released
// by the renderer. Safe on partially-initialized state.
void FinalizePlatformWindow(PlatformWindow& Window);

}   // namespace Frontier

#endif
