/*==============================================================================================================================================
                                                            PLATFORMWINDOWSTUB.CPP
==============================================================================================================================================*/
// 🧩 The linkable floor for any platform without a native windowing backend yet — macOS, iOS, Android, and anything else. It implements the
//    PlatformWindow seam so every target links, but every window it "creates" reports failure (InitializePlatformWindow returns false), so a
//    host on an unsupported platform exits cleanly at bring-up instead of crashing. This is NOT a placeholder to delete — it is the permanent
//    complement of the real backends: the guard below is the exact negation of the Win32 + Linux guards, so precisely one backend compiles per
//    OS and this one covers the rest. Fill a real backend for a platform (mirroring PlatformWindowWin32.cpp) and its guard turns this off there.

#include "PlatformWindow.h"

// The strict complement of every real backend's guard. Add a platform's macro here the moment its real backend lands.
#if !defined(_WIN32) && !defined(__linux__) && !defined(__APPLE__)

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     INSTANCE-EXTENSION STORAGE
//------------------------------------------------------------------------------------------------------------------------

// Only the base surface extension is named; a real backend on this platform would add its own platform surface extension.
static const char* RequiredInstanceExtensionNames[] =
{
    VK_KHR_SURFACE_EXTENSION_NAME,
};


//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializePlatformWindow(PlatformWindow& Window,
                              const char*     /*TitleText*/,
                              uint32_t        /*RequestedWidth*/,
                              uint32_t        /*RequestedHeight*/)
{
    // No native windowing on this platform yet — report failure so the host exits cleanly rather than presenting into nothing.
    Window = PlatformWindow{};
    return false;
}

bool ConstructPresentationSurface(PlatformWindow& /*Window*/, VkInstance /*VulkanInstance*/)
{
    return false;
}

const char** QueryRequiredInstanceExtensions(uint32_t& ExtensionCount)
{
    ExtensionCount = (uint32_t)(sizeof(RequiredInstanceExtensionNames) / sizeof(RequiredInstanceExtensionNames[0]));
    return const_cast<const char**>(RequiredInstanceExtensionNames);
}

void QueryFramebufferExtent(PlatformWindow& Window, uint32_t& Width, uint32_t& Height)
{
    Width  = Window.FramebufferWidth;
    Height = Window.FramebufferHeight;
}

double QueryMonotonicSeconds()
{
    return 0.0;
}

void PollPlatformEvents(PlatformWindow& /*Window*/)
{
}

bool QueryWindowCloseRequested(const PlatformWindow& Window)
{
    return Window.CloseRequested;
}

void* QueryNativeWindowHandle(const PlatformWindow& /*Window*/)
{
    return nullptr;
}

void InstallMessageObserver(PlatformWindow& Window, PlatformMessageObserver Observer, void* ObserverContext)
{
    Window.MessageObserver        = Observer;
    Window.MessageObserverContext = ObserverContext;
}

void FinalizePlatformWindow(PlatformWindow& Window)
{
    Window = PlatformWindow{};
}

}   // namespace Frontier

#endif  // !_WIN32 && !__linux__ && !__APPLE__
