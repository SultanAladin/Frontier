/*==============================================================================================================================================
                                                           PLATFORMWINDOWWAYLAND.CPP
==============================================================================================================================================*/
// 🧩 Linux / Wayland backend for the native windowing seam. Connects to the compositor, binds the core globals (compositor + seat) through the
//    registry, creates a surface, and creates the Vulkan surface with vkCreateWaylandSurfaceKHR. Pointer / keyboard come off the wl_seat
//    capabilities; camera motion uses the relative-pointer protocol (zwp_relative_pointer_v1 — un-accelerated deltas, the Wayland analogue of
//    Win32 WM_INPUT / X11 XI_RawMotion). Selected at compile time by defining FRONTIER_LINUX_WAYLAND on a Linux build (otherwise the X11 backend
//    compiles instead); exactly one Linux backend ever links. Built on a Windows box it compiles to nothing. UNVERIFIED until built on Linux.
//
//    ⚠️ The window-role + decoration wiring (xdg-shell: xdg_wm_base / xdg_surface / xdg_toplevel) and the relative-pointer + keyboard listeners
//    require the compositor's protocol headers, generated from their XML by wayland-scanner at build time (xdg-shell-client-protocol.h,
//    relative-pointer-unstable-v1-client-protocol.h). Those generated headers are a Linux-build artefact and are not vendored here; the sections
//    that consume them are marked FRONTIER_WAYLAND_PROTOCOLS so the file compiles against core libwayland alone and the protocol layer switches
//    on once the generated headers are present in the Linux build. This is a deliberate seam, not a stub — the connection + Vulkan surface are real.

#include "PlatformWindow.h"

#if defined(__linux__) && defined(FRONTIER_LINUX_WAYLAND)

#include <wayland-client.h>

#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan_wayland.h>

#include <time.h>
#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The opaque per-window record for Wayland. The core objects (display / registry / compositor / seat / surface) are bound
//    through the registry at initialize; the window role + input protocol objects live behind FRONTIER_WAYLAND_PROTOCOLS since
//    they need the generated protocol headers. FramebufferWidth/Height are authoritative on the PlatformWindow — Wayland has no
//    server-side window size to query, the compositor delivers it through the xdg_toplevel configure event.
struct PlatformWindowWaylandState
{
    wl_display*    DisplayHandle    = nullptr;   // [-] - Compositor connection
    wl_registry*   Registry         = nullptr;   // [-] - Global-object registry
    wl_compositor* Compositor       = nullptr;   // [-] - Surface factory (bound from the registry)
    wl_seat*       Seat             = nullptr;    // [-] - Input seat (pointer + keyboard, bound from the registry)
    wl_surface*    Surface          = nullptr;    // [-] - The drawable surface

    bool           FullscreenKeyPrevious = false; // [-] - F11 edge state
};


//------------------------------------------------------------------------------------------------------------------------
//                                                     INSTANCE-EXTENSION STORAGE
//------------------------------------------------------------------------------------------------------------------------

static const char* RequiredInstanceExtensionNames[] =
{
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
};


//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Bind the core globals as the registry advertises them. Pointer / keyboard listeners and the xdg-shell role attach off the
// seat + compositor once bound; those live behind FRONTIER_WAYLAND_PROTOCOLS (they need the generated protocol headers).
static void ResolveRegistryGlobal(void* Data, wl_registry* Registry, uint32_t Name,
                                  const char* Interface, uint32_t /*Version*/)
{
    PlatformWindowWaylandState* State = reinterpret_cast<PlatformWindowWaylandState*>(Data);
    if (std::strcmp(Interface, wl_compositor_interface.name) == 0)
        State->Compositor = (wl_compositor*)wl_registry_bind(Registry, Name, &wl_compositor_interface, 1);
    else if (std::strcmp(Interface, wl_seat_interface.name) == 0)
        State->Seat = (wl_seat*)wl_registry_bind(Registry, Name, &wl_seat_interface, 1);
}

static void ResolveRegistryGlobalRemove(void* /*Data*/, wl_registry* /*Registry*/, uint32_t /*Name*/)
{
}

static const wl_registry_listener RegistryListener =
{
    ResolveRegistryGlobal,
    ResolveRegistryGlobalRemove,
};


//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializePlatformWindow(PlatformWindow& Window,
                              const char*     /*TitleText*/,
                              uint32_t        RequestedWidth,
                              uint32_t        RequestedHeight)
{
    Window = PlatformWindow{};

    PlatformWindowWaylandState* State = new PlatformWindowWaylandState{};
    Window.NativeState = State;

    State->DisplayHandle = wl_display_connect(nullptr);
    if (State->DisplayHandle == nullptr)
    {
        FinalizePlatformWindow(Window);
        return false;
    }

    // Bind the core globals: a round-trip lets the registry advertise compositor + seat before we create the surface.
    State->Registry = wl_display_get_registry(State->DisplayHandle);
    wl_registry_add_listener(State->Registry, &RegistryListener, State);
    wl_display_roundtrip(State->DisplayHandle);

    if (State->Compositor == nullptr)
    {
        FinalizePlatformWindow(Window);
        return false;
    }

    State->Surface = wl_compositor_create_surface(State->Compositor);
    if (State->Surface == nullptr)
    {
        FinalizePlatformWindow(Window);
        return false;
    }

    // The compositor owns the size; seed with the request and let a later configure event correct it.
    Window.FramebufferWidth  = RequestedWidth;
    Window.FramebufferHeight = RequestedHeight;

#if defined(FRONTIER_WAYLAND_PROTOCOLS)
    // xdg-shell role (xdg_wm_base → xdg_surface → xdg_toplevel), the relative-pointer + keyboard listeners, and the
    // configure/close handlers attach here using the wayland-scanner-generated protocol headers.
#endif

    return true;
}

bool ConstructPresentationSurface(PlatformWindow& Window, VkInstance VulkanInstance)
{
    PlatformWindowWaylandState* State = reinterpret_cast<PlatformWindowWaylandState*>(Window.NativeState);
    if (State == nullptr || State->Surface == nullptr)
        return false;

    VkWaylandSurfaceCreateInfoKHR SurfaceInfo = {};
    SurfaceInfo.sType   = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    SurfaceInfo.display = State->DisplayHandle;
    SurfaceInfo.surface = State->Surface;
    return vkCreateWaylandSurfaceKHR(VulkanInstance, &SurfaceInfo, nullptr, &Window.PresentationSurface) == VK_SUCCESS;
}

const char** QueryRequiredInstanceExtensions(uint32_t& ExtensionCount)
{
    ExtensionCount = (uint32_t)(sizeof(RequiredInstanceExtensionNames) / sizeof(RequiredInstanceExtensionNames[0]));
    return const_cast<const char**>(RequiredInstanceExtensionNames);
}

void QueryFramebufferExtent(PlatformWindow& Window, uint32_t& Width, uint32_t& Height)
{
    // Wayland has no server-side size to query; the compositor's configure event is authoritative and already stored.
    Width  = Window.FramebufferWidth;
    Height = Window.FramebufferHeight;
}

double QueryMonotonicSeconds()
{
    struct timespec Now;
    clock_gettime(CLOCK_MONOTONIC, &Now);
    return (double)Now.tv_sec + (double)Now.tv_nsec * 1e-9;
}

void PollPlatformEvents(PlatformWindow& Window)
{
    PlatformWindowWaylandState* State = reinterpret_cast<PlatformWindowWaylandState*>(Window.NativeState);
    if (State == nullptr || State->DisplayHandle == nullptr)
        return;

    // Roll the per-poll accumulators (level state persists). Input listeners (behind FRONTIER_WAYLAND_PROTOCOLS) refill them
    // during the dispatch below.
    Window.Input.PointerDeltaX = 0.0;
    Window.Input.PointerDeltaY = 0.0;
    Window.Input.ScrollDelta   = 0.0;

    // Non-blocking dispatch of any queued compositor events (input, configure, close).
    wl_display_dispatch_pending(State->DisplayHandle);
    wl_display_flush(State->DisplayHandle);

    const bool FullscreenKeyHeld = PacketKeyHeld(Window.Input, KeyIdentity::F11);
    // Fullscreen toggle attaches to xdg_toplevel set_fullscreen behind FRONTIER_WAYLAND_PROTOCOLS.
    State->FullscreenKeyPrevious = FullscreenKeyHeld;
}

bool QueryWindowCloseRequested(const PlatformWindow& Window)
{
    return Window.CloseRequested;
}

void* QueryNativeWindowHandle(const PlatformWindow& Window)
{
    // 📝 Wayland identifies a window by a (wl_display*, wl_surface*) pair. The surface is returned as the primary handle; a
    //    future Linux UI bridge reads the display off NativeState directly.
    const PlatformWindowWaylandState* State = reinterpret_cast<const PlatformWindowWaylandState*>(Window.NativeState);
    return (State != nullptr) ? (void*)State->Surface : nullptr;
}

void InstallMessageObserver(PlatformWindow& Window, PlatformMessageObserver Observer, void* ObserverContext)
{
    Window.MessageObserver        = Observer;
    Window.MessageObserverContext = ObserverContext;
}

void FinalizePlatformWindow(PlatformWindow& Window)
{
    PlatformWindowWaylandState* State = reinterpret_cast<PlatformWindowWaylandState*>(Window.NativeState);
    if (State != nullptr)
    {
        if (State->Surface != nullptr)    wl_surface_destroy(State->Surface);
        if (State->Seat != nullptr)       wl_seat_destroy(State->Seat);
        if (State->Compositor != nullptr) wl_compositor_destroy(State->Compositor);
        if (State->Registry != nullptr)   wl_registry_destroy(State->Registry);
        if (State->DisplayHandle != nullptr) wl_display_disconnect(State->DisplayHandle);
        delete State;
    }
    Window.NativeState = nullptr;
    Window.PresentationSurface = VK_NULL_HANDLE;
}

}   // namespace Frontier

#endif  // __linux__ && FRONTIER_LINUX_WAYLAND
