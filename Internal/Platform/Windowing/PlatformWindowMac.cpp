/*==============================================================================================================================================
                                                             PLATFORMWINDOWMAC.CPP
==============================================================================================================================================*/
// 🧩 macOS backend for the native windowing seam. Opens a Cocoa NSWindow, attaches a CAMetalLayer as its content view's backing layer, and
//    creates the Vulkan surface with vkCreateMetalSurfaceEXT (VK_EXT_metal_surface — MoltenVK translates it to Metal). Pointer motion for the
//    camera comes off the NSEvent locationInWindow / deltaX / deltaY (the un-accelerated deltas an NSTrackingArea + NSEventTypeMouseMoved
//    deliver, the Cocoa analogue of Win32 WM_INPUT / X11 XI_RawMotion); keyboard make / break folds into the portable KeyIdentity table via the
//    NSEvent keyCode. CVDisplayLink is not used — timing comes off mach_absolute_time for a window-independent monotonic clock. Compiled on
//    Apple platforms only (guarded on __APPLE__), so precisely one backend compiles per OS and the stub covers everything else.
//
//    ⚠️ Cocoa (NSWindow / NSApplication / NSView) and the Metal layer (CAMetalLayer) are Objective-C, so the real window + surface wiring lives
//    behind FRONTIER_APPLE_COCOA and is realized only when this unit is built as Objective-C++ (a .mm translation unit) on a Mac, linking the
//    Cocoa + QuartzCore frameworks that ship with Xcode's Command Line Tools — nothing is vendored or downloaded. Compiled as plain C++ (the
//    default, and the only mode available on a Windows box) the file builds against core <vulkan/vulkan.h> alone: the connection stubs return
//    cleanly so every Apple target still links, and the Cocoa layer switches on the moment the unit is compiled as .mm with the frameworks
//    present. This is a deliberate seam, not a throwaway stub — the Vulkan/Metal surface path and the poll structure are real. UNVERIFIED until
//    built on macOS.

#include "PlatformWindow.h"

#if defined(__APPLE__)

#include <mach/mach_time.h>
#include <cstring>

#if defined(FRONTIER_APPLE_COCOA)
    // Realized only in an Objective-C++ (.mm) build with the macOS SDK present. These headers ship with Xcode's Command Line
    // Tools; none is vendored here. VK_EXT_metal_surface is part of the standard Vulkan headers already in ExternalPackages.
    #import <Cocoa/Cocoa.h>
    #import <QuartzCore/CAMetalLayer.h>
    #define VK_USE_PLATFORM_METAL_EXT
    #include <vulkan/vulkan_metal.h>
#endif

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr uint32_t MinimumWidth  = 320;   // [px] - Floor so a resize cannot collapse the window
constexpr uint32_t MinimumHeight = 240;   // [px] - Floor so a resize cannot collapse the window


//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The opaque per-window record for macOS. The Cocoa objects (window, content view, Metal layer) are typed void* so this
//    struct stays free of Objective-C types in a plain-C++ build; the FRONTIER_APPLE_COCOA path casts them back to the real
//    Cocoa classes. FramebufferWidth/Height are authoritative on the PlatformWindow — Cocoa reports the drawable size through
//    the content view's bounds (in backing-store pixels, so a Retina scale is already folded in). The F11 edge state drives the
//    borderless-fullscreen toggle.
struct PlatformWindowMacState
{
    void*  Window        = nullptr;   // [-] - NSWindow* (the decorated top-level window)
    void*  ContentView   = nullptr;   // [-] - NSView*   (the layer-backed content view)
    void*  MetalLayer    = nullptr;   // [-] - CAMetalLayer* (the drawable surface Vulkan presents into)

    bool   FullscreenKeyPrevious = false;   // [-] - F11 state last poll (rising-edge detect)
};


//------------------------------------------------------------------------------------------------------------------------
//                                                     INSTANCE-EXTENSION STORAGE
//------------------------------------------------------------------------------------------------------------------------

// The two extensions a Metal (MoltenVK) Vulkan surface needs. Storage is process-lifetime so the returned pointer stays valid.
static const char* RequiredInstanceExtensionNames[] =
{
    VK_KHR_SURFACE_EXTENSION_NAME,
    "VK_EXT_metal_surface",   // named as a literal so the plain-C++ build needs no VK_EXT_metal_surface header
};


//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Translate a Cocoa virtual key code into the portable KeyIdentity. The kVK_* codes are the fixed ANSI keyboard positions from
// <Carbon/HIToolbox/Events.h>; only the keys the editor reads are mapped, anything else returns Unknown so it never lands in the
// held table. Coded as raw hex so the mapping stands without the Carbon header in a plain-C++ build.
static KeyIdentity TranslateKeyCode(uint16_t KeyCode)
{
    switch (KeyCode)
    {
        // ANSI letter row positions (kVK_ANSI_A … kVK_ANSI_Z are not contiguous, so each is named).
        case 0x00: return KeyIdentity::A;
        case 0x0B: return KeyIdentity::B;
        case 0x08: return KeyIdentity::C;
        case 0x02: return KeyIdentity::D;
        case 0x0E: return KeyIdentity::E;
        case 0x03: return KeyIdentity::F;
        case 0x05: return KeyIdentity::G;
        case 0x04: return KeyIdentity::H;
        case 0x22: return KeyIdentity::I;
        case 0x26: return KeyIdentity::J;
        case 0x28: return KeyIdentity::K;
        case 0x25: return KeyIdentity::L;
        case 0x2E: return KeyIdentity::M;
        case 0x2D: return KeyIdentity::N;
        case 0x1F: return KeyIdentity::O;
        case 0x23: return KeyIdentity::P;
        case 0x0C: return KeyIdentity::Q;
        case 0x0F: return KeyIdentity::R;
        case 0x01: return KeyIdentity::S;
        case 0x11: return KeyIdentity::T;
        case 0x20: return KeyIdentity::U;
        case 0x09: return KeyIdentity::V;
        case 0x0D: return KeyIdentity::W;
        case 0x07: return KeyIdentity::X;
        case 0x10: return KeyIdentity::Y;
        case 0x06: return KeyIdentity::Z;

        case 0x38: return KeyIdentity::LeftShift;     // kVK_Shift
        case 0x3C: return KeyIdentity::RightShift;    // kVK_RightShift
        case 0x3B: return KeyIdentity::LeftControl;   // kVK_Control
        case 0x3E: return KeyIdentity::RightControl;  // kVK_RightControl
        case 0x3A: return KeyIdentity::LeftAlt;       // kVK_Option
        case 0x3D: return KeyIdentity::RightAlt;      // kVK_RightOption

        case 0x7A: return KeyIdentity::F1;
        case 0x78: return KeyIdentity::F2;
        case 0x63: return KeyIdentity::F3;
        case 0x76: return KeyIdentity::F4;
        case 0x60: return KeyIdentity::F5;
        case 0x61: return KeyIdentity::F6;
        case 0x62: return KeyIdentity::F7;
        case 0x64: return KeyIdentity::F8;
        case 0x65: return KeyIdentity::F9;
        case 0x6D: return KeyIdentity::F10;
        case 0x67: return KeyIdentity::F11;
        case 0x6F: return KeyIdentity::F12;

        case 0x31: return KeyIdentity::Space;
        case 0x35: return KeyIdentity::Escape;
        default:   return KeyIdentity::Unknown;
    }
}

#if defined(FRONTIER_APPLE_COCOA)

// Fold one Cocoa NSEvent into the window's InputPacket. Keyboard make / break maps through TranslateKeyCode; the mouse buttons
// fold level state; mouse-moved / dragged sum the un-accelerated deltaX / deltaY into the pointer-delta accumulator and track the
// absolute locationInWindow for UI hit-testing; scroll-wheel sums into the scroll accumulator. Called from the drain loop for
// every event the application dispatched this poll.
static void ResolveCocoaEvent(PlatformWindow& Owner, NSEvent* Event)
{
    switch ([Event type])
    {
        case NSEventTypeKeyDown:
        case NSEventTypeKeyUp:
        {
            const KeyIdentity Key = TranslateKeyCode((uint16_t)[Event keyCode]);
            if (Key != KeyIdentity::Unknown)
                Owner.Input.KeyHeld[static_cast<int>(Key)] = ([Event type] == NSEventTypeKeyDown);
            break;
        }

        case NSEventTypeFlagsChanged:
        {
            // Modifier make / break arrives here (not KeyDown/Up). Resolve the side off the key code and read the matching
            // modifier flag to decide held vs released.
            const KeyIdentity Key = TranslateKeyCode((uint16_t)[Event keyCode]);
            if (Key != KeyIdentity::Unknown)
            {
                const NSEventModifierFlags Flags = [Event modifierFlags];
                bool Held = false;
                switch (Key)
                {
                    case KeyIdentity::LeftShift:   case KeyIdentity::RightShift:   Held = (Flags & NSEventModifierFlagShift)   != 0; break;
                    case KeyIdentity::LeftControl: case KeyIdentity::RightControl: Held = (Flags & NSEventModifierFlagControl) != 0; break;
                    case KeyIdentity::LeftAlt:     case KeyIdentity::RightAlt:     Held = (Flags & NSEventModifierFlagOption)  != 0; break;
                    default: break;
                }
                Owner.Input.KeyHeld[static_cast<int>(Key)] = Held;
            }
            break;
        }

        case NSEventTypeLeftMouseDown:  Owner.Input.ButtonHeld[(int)PointerButton::Left]   = true;  break;
        case NSEventTypeLeftMouseUp:    Owner.Input.ButtonHeld[(int)PointerButton::Left]   = false; break;
        case NSEventTypeRightMouseDown: Owner.Input.ButtonHeld[(int)PointerButton::Right]  = true;  break;
        case NSEventTypeRightMouseUp:   Owner.Input.ButtonHeld[(int)PointerButton::Right]  = false; break;
        case NSEventTypeOtherMouseDown: Owner.Input.ButtonHeld[(int)PointerButton::Middle] = true;  break;
        case NSEventTypeOtherMouseUp:   Owner.Input.ButtonHeld[(int)PointerButton::Middle] = false; break;

        case NSEventTypeMouseMoved:
        case NSEventTypeLeftMouseDragged:
        case NSEventTypeRightMouseDragged:
        case NSEventTypeOtherMouseDragged:
        {
            // deltaX / deltaY are the un-accelerated device motion the camera wants; locationInWindow is the absolute cursor for
            // hit-testing. Cocoa's y grows upward, so flip it to the top-left origin the rest of the seam uses.
            Owner.Input.PointerDeltaX += (double)[Event deltaX];
            Owner.Input.PointerDeltaY += (double)[Event deltaY];
            const NSPoint Location = [Event locationInWindow];
            Owner.Input.PointerPositionX = (double)Location.x;
            Owner.Input.PointerPositionY = (double)Location.y;
            break;
        }

        case NSEventTypeScrollWheel:
            Owner.Input.ScrollDelta += (double)[Event scrollingDeltaY];
            break;

        default:
            break;
    }
}

#endif  // FRONTIER_APPLE_COCOA


//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializePlatformWindow(PlatformWindow& Window,
                              const char*     TitleText,
                              uint32_t        RequestedWidth,
                              uint32_t        RequestedHeight)
{
    Window = PlatformWindow{};

    PlatformWindowMacState* State = new PlatformWindowMacState{};
    Window.NativeState = State;

    // The compositor owns nothing here; seed the request and let the content-view bounds correct it after the window is placed.
    Window.FramebufferWidth  = RequestedWidth;
    Window.FramebufferHeight = RequestedHeight;

#if defined(FRONTIER_APPLE_COCOA)
    @autoreleasepool
    {
        // ① Bring up the shared application (a windowed app must be a regular activation-policy process).
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        // ② Create a decorated, resizable, titled window sized to the requested client extent.
        const NSRect Content = NSMakeRect(0, 0, (CGFloat)RequestedWidth, (CGFloat)RequestedHeight);
        const NSWindowStyleMask Style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                        NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        NSWindow* CocoaWindow = [[NSWindow alloc] initWithContentRect:Content
                                                           styleMask:Style
                                                             backing:NSBackingStoreBuffered
                                                               defer:NO];
        if (CocoaWindow == nil)
        {
            FinalizePlatformWindow(Window);
            return false;
        }
        [CocoaWindow setTitle:[NSString stringWithUTF8String:(TitleText != nullptr && TitleText[0] != '\0') ? TitleText : "Frontier"]];
        [CocoaWindow setContentMinSize:NSMakeSize((CGFloat)MinimumWidth, (CGFloat)MinimumHeight)];

        // ③ Attach a CAMetalLayer as the content view's backing layer — the drawable Vulkan (MoltenVK) presents into.
        NSView* View = [CocoaWindow contentView];
        CAMetalLayer* Layer = [CAMetalLayer layer];
        [View setWantsLayer:YES];
        [View setLayer:Layer];

        State->Window      = (void*)CFBridgingRetain(CocoaWindow);
        State->ContentView = (__bridge void*)View;
        State->MetalLayer  = (__bridge void*)Layer;

        [CocoaWindow makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        // ④ Seed the true drawable extent from the backing-store bounds (folds the Retina scale in).
        const NSRect Backing = [View convertRectToBacking:[View bounds]];
        Window.FramebufferWidth  = (uint32_t)Backing.size.width;
        Window.FramebufferHeight = (uint32_t)Backing.size.height;
    }
    return true;
#else
    // Plain-C++ build (e.g. cross-compiling headers on a non-Mac box): the Cocoa layer is unavailable, so report failure and let
    // the host exit cleanly. The Apple target still LINKS — the seam is present — and turns real when built as .mm on a Mac.
    FinalizePlatformWindow(Window);
    return false;
#endif
}

bool ConstructPresentationSurface(PlatformWindow& Window, VkInstance VulkanInstance)
{
    PlatformWindowMacState* State = reinterpret_cast<PlatformWindowMacState*>(Window.NativeState);
    if (State == nullptr || State->MetalLayer == nullptr)
        return false;

#if defined(FRONTIER_APPLE_COCOA)
    VkMetalSurfaceCreateInfoEXT SurfaceInfo = {};
    SurfaceInfo.sType  = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    SurfaceInfo.pLayer = (const CAMetalLayer*)State->MetalLayer;
    return vkCreateMetalSurfaceEXT(VulkanInstance, &SurfaceInfo, nullptr, &Window.PresentationSurface) == VK_SUCCESS;
#else
    (void)VulkanInstance;
    return false;
#endif
}

const char** QueryRequiredInstanceExtensions(uint32_t& ExtensionCount)
{
    ExtensionCount = (uint32_t)(sizeof(RequiredInstanceExtensionNames) / sizeof(RequiredInstanceExtensionNames[0]));
    return const_cast<const char**>(RequiredInstanceExtensionNames);
}

void QueryFramebufferExtent(PlatformWindow& Window, uint32_t& Width, uint32_t& Height)
{
#if defined(FRONTIER_APPLE_COCOA)
    PlatformWindowMacState* State = reinterpret_cast<PlatformWindowMacState*>(Window.NativeState);
    if (State != nullptr && State->ContentView != nullptr)
    {
        NSView* View = (__bridge NSView*)State->ContentView;
        const NSRect Backing = [View convertRectToBacking:[View bounds]];
        Window.FramebufferWidth  = (uint32_t)Backing.size.width;
        Window.FramebufferHeight = (uint32_t)Backing.size.height;
    }
#endif
    Width  = Window.FramebufferWidth;
    Height = Window.FramebufferHeight;
}

double QueryMonotonicSeconds()
{
    // mach_absolute_time is the macOS monotonic counter; timebase converts its abstract units to nanoseconds once.
    static mach_timebase_info_data_t Timebase = {};
    if (Timebase.denom == 0)
        mach_timebase_info(&Timebase);
    const uint64_t Now = mach_absolute_time();
    return (double)(Now * Timebase.numer / Timebase.denom) * 1e-9;
}

void PollPlatformEvents(PlatformWindow& Window)
{
    PlatformWindowMacState* State = reinterpret_cast<PlatformWindowMacState*>(Window.NativeState);
    if (State == nullptr)
        return;

    // Roll the per-poll accumulators to zero BEFORE draining (level state persists across polls — only the deltas reset).
    Window.Input.PointerDeltaX = 0.0;
    Window.Input.PointerDeltaY = 0.0;
    Window.Input.ScrollDelta   = 0.0;

#if defined(FRONTIER_APPLE_COCOA)
    @autoreleasepool
    {
        // Drain every queued event without blocking (distantPast = return immediately when the queue empties).
        for (;;)
        {
            NSEvent* Event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:[NSDate distantPast]
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
            if (Event == nil)
                break;
            ResolveCocoaEvent(Window, Event);
            [NSApp sendEvent:Event];   // let Cocoa perform default handling (window drag, resize, close button)
        }

        NSWindow* CocoaWindow = (__bridge NSWindow*)State->Window;
        if (CocoaWindow != nil)
        {
            Window.MinimizedCondition = [CocoaWindow isMiniaturized];
            if (!Window.MinimizedCondition)
            {
                NSView* View = [CocoaWindow contentView];
                const NSRect Backing = [View convertRectToBacking:[View bounds]];
                const uint32_t Width  = (uint32_t)Backing.size.width;
                const uint32_t Height = (uint32_t)Backing.size.height;
                if (Width != Window.FramebufferWidth || Height != Window.FramebufferHeight)
                {
                    Window.FramebufferWidth  = Width;
                    Window.FramebufferHeight = Height;
                    Window.RebuildRequested  = true;   // backstop: mark the swapchain stale on every real resize
                }
            }
        }
    }
#endif

    // Resolve the F11 fullscreen toggle on the rising edge (level state was folded by the drain above).
    const bool FullscreenKeyHeld = PacketKeyHeld(Window.Input, KeyIdentity::F11);
#if defined(FRONTIER_APPLE_COCOA)
    if (FullscreenKeyHeld && !State->FullscreenKeyPrevious && State->Window != nullptr)
    {
        NSWindow* CocoaWindow = (__bridge NSWindow*)State->Window;
        [CocoaWindow toggleFullScreen:nil];
        Window.FullscreenEnabled = !Window.FullscreenEnabled;
    }
#endif
    State->FullscreenKeyPrevious = FullscreenKeyHeld;
}

bool QueryWindowCloseRequested(const PlatformWindow& Window)
{
    return Window.CloseRequested;
}

void* QueryNativeWindowHandle(const PlatformWindow& Window)
{
    // 📝 macOS identifies a window by its NSWindow*, but a Vulkan UI bridge wants the CAMetalLayer it presents into; the layer is
    //    returned as the primary handle so an ImGui Metal / Cocoa bridge attaches to the same drawable. NSWindow* lives on
    //    NativeState for a bridge that needs the window itself.
    const PlatformWindowMacState* State = reinterpret_cast<const PlatformWindowMacState*>(Window.NativeState);
    return (State != nullptr) ? State->MetalLayer : nullptr;
}

void InstallMessageObserver(PlatformWindow& Window, PlatformMessageObserver Observer, void* ObserverContext)
{
    Window.MessageObserver        = Observer;
    Window.MessageObserverContext = ObserverContext;
}

void FinalizePlatformWindow(PlatformWindow& Window)
{
    PlatformWindowMacState* State = reinterpret_cast<PlatformWindowMacState*>(Window.NativeState);
    if (State != nullptr)
    {
#if defined(FRONTIER_APPLE_COCOA)
        if (State->Window != nullptr)
        {
            NSWindow* CocoaWindow = (NSWindow*)CFBridgingRelease(State->Window);   // balances the CFBridgingRetain at create
            [CocoaWindow close];
        }
#endif
        delete State;
    }
    Window.NativeState = nullptr;
    Window.PresentationSurface = VK_NULL_HANDLE;
}

}   // namespace Frontier

#endif  // __APPLE__
