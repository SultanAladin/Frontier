/*==============================================================================================================================================
                                                             PLATFORMWINDOWX11.CPP
==============================================================================================================================================*/
// 🧩 Linux / X11 backend for the native windowing seam. Opens the X display, creates a decorated top-level window, selects for the events the
//    editor needs, and creates the Vulkan surface with vkCreateXlibSurfaceKHR. Mouse motion for the camera comes from the XInput2 raw-motion
//    stream (XI_RawMotion — un-accelerated device deltas, the X analogue of Win32 WM_INPUT) while the ordinary pointer keeps the visible cursor
//    position for UI hit-testing; keyboard make / break folds into the portable KeyIdentity table via the keysym. Compiled on Linux only, and
//    only when the Wayland backend is NOT selected (FRONTIER_LINUX_WAYLAND unset), so exactly one Linux backend links. Built on a Windows box
//    it compiles to nothing; it is written for parity and is UNVERIFIED until built on Linux.

#include "PlatformWindow.h"

#if defined(__linux__) && !defined(FRONTIER_LINUX_WAYLAND)

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XInput2.h>

#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan_xlib.h>

#include <time.h>
#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr unsigned int MinimumWidth  = 320;   // [px] - Floor so a resize cannot collapse the window
constexpr unsigned int MinimumHeight = 240;   // [px] - Floor so a resize cannot collapse the window


//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The opaque per-window record for X11. Holds the display + window + the WM_DELETE_WINDOW atom (so the close button routes
//    through the client-message path instead of killing the connection), the XI2 opcode for raw-motion routing, and the F11
//    edge state for the fullscreen toggle.
struct PlatformWindowX11State
{
    Display*     DisplayHandle = nullptr;   // [-]  - X display connection
    Window       WindowHandle  = 0;         // [-]  - Top-level window
    Atom         DeleteAtom    = 0;         // [-]  - WM_DELETE_WINDOW protocol atom
    int          XInputOpcode  = 0;         // [-]  - XInput2 major opcode (raw events carry it)
    bool         FullscreenKeyPrevious = false;   // [-] - F11 state last poll (rising-edge detect)
};


//------------------------------------------------------------------------------------------------------------------------
//                                                     INSTANCE-EXTENSION STORAGE
//------------------------------------------------------------------------------------------------------------------------

static const char* RequiredInstanceExtensionNames[] =
{
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
};


//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Translate an X keysym into the portable KeyIdentity. Only the editor's keys are mapped; anything else returns Unknown.
static KeyIdentity TranslateKeysym(KeySym Symbol)
{
    if (Symbol >= XK_a && Symbol <= XK_z)
        return static_cast<KeyIdentity>(static_cast<int>(KeyIdentity::A) + (int)(Symbol - XK_a));
    if (Symbol >= XK_A && Symbol <= XK_Z)
        return static_cast<KeyIdentity>(static_cast<int>(KeyIdentity::A) + (int)(Symbol - XK_A));

    switch (Symbol)
    {
        case XK_Shift_L:   return KeyIdentity::LeftShift;
        case XK_Shift_R:   return KeyIdentity::RightShift;
        case XK_Control_L: return KeyIdentity::LeftControl;
        case XK_Control_R: return KeyIdentity::RightControl;
        case XK_Alt_L:     return KeyIdentity::LeftAlt;
        case XK_Alt_R:     return KeyIdentity::RightAlt;
        case XK_F1:        return KeyIdentity::F1;
        case XK_F2:        return KeyIdentity::F2;
        case XK_F3:        return KeyIdentity::F3;
        case XK_F4:        return KeyIdentity::F4;
        case XK_F5:        return KeyIdentity::F5;
        case XK_F6:        return KeyIdentity::F6;
        case XK_F7:        return KeyIdentity::F7;
        case XK_F8:        return KeyIdentity::F8;
        case XK_F9:        return KeyIdentity::F9;
        case XK_F10:       return KeyIdentity::F10;
        case XK_F11:       return KeyIdentity::F11;
        case XK_F12:       return KeyIdentity::F12;
        case XK_space:     return KeyIdentity::Space;
        case XK_Escape:    return KeyIdentity::Escape;
        default:           return KeyIdentity::Unknown;
    }
}

// Fold one XI2 raw-motion event's relative deltas into the pointer-delta accumulator. Raw values live in raw_values, gated by
// the valuator mask; the first two set valuators are X and Y on a standard mouse. These are un-accelerated device counts.
static void AccumulateRawMotion(PlatformWindow& Owner, const XIRawEvent& Raw)
{
    const double* Value = Raw.raw_values;
    int Emitted = 0;
    for (int Axis = 0; Axis < Raw.valuators.mask_len * 8 && Emitted < 2; ++Axis)
    {
        if (XIMaskIsSet(Raw.valuators.mask, Axis))
        {
            if (Emitted == 0) Owner.Input.PointerDeltaX += *Value;
            else              Owner.Input.PointerDeltaY += *Value;
            ++Value;
            ++Emitted;
        }
    }
}

// Toggle fullscreen through the EWMH _NET_WM_STATE_FULLSCREEN client message (the portable, window-manager-cooperative path).
static void ToggleFullscreen(PlatformWindow& Window, PlatformWindowX11State& State)
{
    Window.FullscreenEnabled = !Window.FullscreenEnabled;

    Atom StateAtom      = XInternAtom(State.DisplayHandle, "_NET_WM_STATE", False);
    Atom FullscreenAtom = XInternAtom(State.DisplayHandle, "_NET_WM_STATE_FULLSCREEN", False);

    XEvent Message = {};
    Message.type                 = ClientMessage;
    Message.xclient.window       = State.WindowHandle;
    Message.xclient.message_type = StateAtom;
    Message.xclient.format       = 32;
    Message.xclient.data.l[0]    = Window.FullscreenEnabled ? 1 : 0;   // _NET_WM_STATE_ADD / _REMOVE
    Message.xclient.data.l[1]    = (long)FullscreenAtom;
    Message.xclient.data.l[2]    = 0;
    XSendEvent(State.DisplayHandle, DefaultRootWindow(State.DisplayHandle), False,
               SubstructureNotifyMask | SubstructureRedirectMask, &Message);
    XFlush(State.DisplayHandle);
}


//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializePlatformWindow(PlatformWindow& Window,
                              const char*     TitleText,
                              uint32_t        RequestedWidth,
                              uint32_t        RequestedHeight)
{
    Window = PlatformWindow{};

    PlatformWindowX11State* State = new PlatformWindowX11State{};
    Window.NativeState = State;

    State->DisplayHandle = XOpenDisplay(nullptr);
    if (State->DisplayHandle == nullptr)
    {
        FinalizePlatformWindow(Window);
        return false;
    }
    Display* Dpy = State->DisplayHandle;
    const int Screen = DefaultScreen(Dpy);

    // ① Create a decorated top-level window selecting the events the editor consumes.
    State->WindowHandle = XCreateSimpleWindow(Dpy, RootWindow(Dpy, Screen),
                                              0, 0, RequestedWidth, RequestedHeight, 0,
                                              BlackPixel(Dpy, Screen), BlackPixel(Dpy, Screen));
    if (State->WindowHandle == 0)
    {
        FinalizePlatformWindow(Window);
        return false;
    }
    XSelectInput(Dpy, State->WindowHandle,
                 KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                 PointerMotionMask | StructureNotifyMask);
    XStoreName(Dpy, State->WindowHandle, (TitleText != nullptr && TitleText[0] != '\0') ? TitleText : "Frontier");

    // Minimum size hint so a resize cannot collapse the window.
    XSizeHints* Hints = XAllocSizeHints();
    if (Hints != nullptr)
    {
        Hints->flags      = PMinSize;
        Hints->min_width  = (int)MinimumWidth;
        Hints->min_height = (int)MinimumHeight;
        XSetWMNormalHints(Dpy, State->WindowHandle, Hints);
        XFree(Hints);
    }

    // ② Route the window-manager close button through WM_DELETE_WINDOW so it becomes a CloseRequested flag, not a killed connection.
    State->DeleteAtom = XInternAtom(Dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(Dpy, State->WindowHandle, &State->DeleteAtom, 1);

    // ③ Register for XInput2 raw motion (un-accelerated device deltas). Non-fatal on failure — PointerMotion still tracks the
    //    visible cursor; only the raw camera delta is lost, and the poll falls back to differencing the pointer position.
    int Event = 0;
    int Error = 0;
    if (XQueryExtension(Dpy, "XInputExtension", &State->XInputOpcode, &Event, &Error))
    {
        unsigned char Mask[XIMaskLen(XI_LASTEVENT)] = {};
        XISetMask(Mask, XI_RawMotion);
        XIEventMask EventMask = {};
        EventMask.deviceid = XIAllMasterDevices;
        EventMask.mask_len = sizeof(Mask);
        EventMask.mask     = Mask;
        XISelectEvents(Dpy, DefaultRootWindow(Dpy), &EventMask, 1);
    }

    XMapWindow(Dpy, State->WindowHandle);
    XFlush(Dpy);

    Window.FramebufferWidth  = RequestedWidth;
    Window.FramebufferHeight = RequestedHeight;
    return true;
}

bool ConstructPresentationSurface(PlatformWindow& Window, VkInstance VulkanInstance)
{
    PlatformWindowX11State* State = reinterpret_cast<PlatformWindowX11State*>(Window.NativeState);
    if (State == nullptr || State->WindowHandle == 0)
        return false;

    VkXlibSurfaceCreateInfoKHR SurfaceInfo = {};
    SurfaceInfo.sType  = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    SurfaceInfo.dpy    = State->DisplayHandle;
    SurfaceInfo.window = State->WindowHandle;
    return vkCreateXlibSurfaceKHR(VulkanInstance, &SurfaceInfo, nullptr, &Window.PresentationSurface) == VK_SUCCESS;
}

const char** QueryRequiredInstanceExtensions(uint32_t& ExtensionCount)
{
    ExtensionCount = (uint32_t)(sizeof(RequiredInstanceExtensionNames) / sizeof(RequiredInstanceExtensionNames[0]));
    return const_cast<const char**>(RequiredInstanceExtensionNames);
}

void QueryFramebufferExtent(PlatformWindow& Window, uint32_t& Width, uint32_t& Height)
{
    PlatformWindowX11State* State = reinterpret_cast<PlatformWindowX11State*>(Window.NativeState);
    if (State != nullptr && State->WindowHandle != 0)
    {
        XWindowAttributes Attributes = {};
        XGetWindowAttributes(State->DisplayHandle, State->WindowHandle, &Attributes);
        Window.FramebufferWidth  = (uint32_t)Attributes.width;
        Window.FramebufferHeight = (uint32_t)Attributes.height;
    }
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
    PlatformWindowX11State* State = reinterpret_cast<PlatformWindowX11State*>(Window.NativeState);
    if (State == nullptr)
        return;
    Display* Dpy = State->DisplayHandle;

    // Roll the per-poll accumulators to zero before draining (level state persists).
    Window.Input.PointerDeltaX = 0.0;
    Window.Input.PointerDeltaY = 0.0;
    Window.Input.ScrollDelta   = 0.0;

    while (XPending(Dpy) > 0)
    {
        XEvent Event;
        XNextEvent(Dpy, &Event);

        switch (Event.type)
        {
            case KeyPress:
            case KeyRelease:
            {
                KeySym Symbol = XLookupKeysym(&Event.xkey, 0);
                const KeyIdentity Key = TranslateKeysym(Symbol);
                if (Key != KeyIdentity::Unknown)
                    Window.Input.KeyHeld[static_cast<int>(Key)] = (Event.type == KeyPress);
                break;
            }

            case ButtonPress:
            case ButtonRelease:
            {
                const bool Held = (Event.type == ButtonPress);
                switch (Event.xbutton.button)
                {
                    case Button1: Window.Input.ButtonHeld[(int)PointerButton::Left]   = Held; break;
                    case Button2: Window.Input.ButtonHeld[(int)PointerButton::Middle] = Held; break;
                    case Button3: Window.Input.ButtonHeld[(int)PointerButton::Right]  = Held; break;
                    case Button4: if (Held) Window.Input.ScrollDelta += 1.0; break;   // wheel up
                    case Button5: if (Held) Window.Input.ScrollDelta -= 1.0; break;   // wheel down
                    default: break;
                }
                break;
            }

            case MotionNotify:
                Window.Input.PointerPositionX = (double)Event.xmotion.x;
                Window.Input.PointerPositionY = (double)Event.xmotion.y;
                break;

            case ConfigureNotify:
                if (Event.xconfigure.width > 0 && Event.xconfigure.height > 0)
                {
                    const uint32_t Width  = (uint32_t)Event.xconfigure.width;
                    const uint32_t Height = (uint32_t)Event.xconfigure.height;
                    if (Width != Window.FramebufferWidth || Height != Window.FramebufferHeight)
                    {
                        Window.FramebufferWidth  = Width;
                        Window.FramebufferHeight = Height;
                        Window.RebuildRequested  = true;
                        Window.MinimizedCondition = false;
                    }
                }
                break;

            case ClientMessage:
                if ((Atom)Event.xclient.data.l[0] == State->DeleteAtom)
                    Window.CloseRequested = true;
                break;

            case GenericEvent:
                // XInput2 raw motion arrives as a cookied generic event carrying the XInput opcode.
                if (Event.xcookie.extension == State->XInputOpcode &&
                    XGetEventData(Dpy, &Event.xcookie))
                {
                    if (Event.xcookie.evtype == XI_RawMotion)
                        AccumulateRawMotion(Window, *reinterpret_cast<XIRawEvent*>(Event.xcookie.data));
                    XFreeEventData(Dpy, &Event.xcookie);
                }
                break;

            default:
                break;
        }
    }

    // Resolve the F11 fullscreen toggle on the rising edge.
    const bool FullscreenKeyHeld = PacketKeyHeld(Window.Input, KeyIdentity::F11);
    if (FullscreenKeyHeld && !State->FullscreenKeyPrevious)
        ToggleFullscreen(Window, *State);
    State->FullscreenKeyPrevious = FullscreenKeyHeld;
}

bool QueryWindowCloseRequested(const PlatformWindow& Window)
{
    return Window.CloseRequested;
}

void* QueryNativeWindowHandle(const PlatformWindow& Window)
{
    // 📝 X11 identifies a window by a (Display*, Window-XID) pair, not one handle, so there is no single HWND-equivalent. The
    //    Display connection is returned as the primary handle; a future Linux UI bridge reads the XID off NativeState directly.
    const PlatformWindowX11State* State = reinterpret_cast<const PlatformWindowX11State*>(Window.NativeState);
    return (State != nullptr) ? (void*)State->DisplayHandle : nullptr;
}

void InstallMessageObserver(PlatformWindow& Window, PlatformMessageObserver Observer, void* ObserverContext)
{
    Window.MessageObserver        = Observer;
    Window.MessageObserverContext = ObserverContext;
}

void FinalizePlatformWindow(PlatformWindow& Window)
{
    PlatformWindowX11State* State = reinterpret_cast<PlatformWindowX11State*>(Window.NativeState);
    if (State != nullptr)
    {
        if (State->DisplayHandle != nullptr)
        {
            if (State->WindowHandle != 0)
                XDestroyWindow(State->DisplayHandle, State->WindowHandle);
            XCloseDisplay(State->DisplayHandle);
        }
        delete State;
    }
    Window.NativeState = nullptr;
    Window.PresentationSurface = VK_NULL_HANDLE;
}

}   // namespace Frontier

#endif  // __linux__ && !FRONTIER_LINUX_WAYLAND
