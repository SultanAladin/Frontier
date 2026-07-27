/*==============================================================================================================================================
                                                            PLATFORMWINDOWWIN32.CPP
==============================================================================================================================================*/
// 🧩 Win32 backend for the native windowing seam. Registers a window class, creates a decorated resizable window, and owns its window proc
//    directly (no library to subclass), so every message — resize, minimize, close, keyboard, mouse, and the WM_INPUT raw stream — routes to
//    one place. The Vulkan surface is created with vkCreateWin32SurfaceKHR. Mouse motion is summed from WM_INPUT raw relative counts (the
//    un-accelerated, un-quantized device motion a camera wants; the Win32 analogue of an immediate-mode UI's mouse delta) while the cursor
//    stays VISIBLE — no capture, no hide. Keyboard make / break folds into the portable KeyIdentity table; buttons and wheel come off the
//    ordinary mouse messages. QueryPerformanceCounter drives the monotonic clock. Compiled on Windows only (guarded on _WIN32); every other
//    platform links its own backend or the stub.

#include "PlatformWindow.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan_win32.h>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr int MinimumWidth  = 320;   // [px] - Floor so a resize cannot collapse the window
constexpr int MinimumHeight = 240;   // [px] - Floor so a resize cannot collapse the window

static const wchar_t* WindowClassName = L"FrontierPlatformWindow";   // [-] - Registered class name (one per process)


//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The opaque per-window record PlatformWindow.NativeState points at. Holds the OS handles, the saved windowed rect for the
//    fullscreen restore, and a back-pointer to the owning PlatformWindow so the static window proc can fold input + resize
//    signals straight into it. One record per window, freed on finalize.
struct PlatformWindowWin32State
{
    HINSTANCE       Instance      = nullptr;   // [-]  - Module handle the class registered under
    HWND            Window        = nullptr;   // [-]  - The created window
    PlatformWindow* Owner         = nullptr;   // [-]  - Back-pointer so the proc reaches the InputPacket + backstop flags

    // Saved windowed placement for the borderless-fullscreen restore.
    RECT            WindowedRect  = {};         // [px] - Window rect before entering fullscreen
    DWORD           WindowedStyle = 0;          // [-]  - Window style before entering fullscreen

    bool            FullscreenKeyPrevious = false;   // [-] - F11 state last poll (rising-edge detect)
};


//------------------------------------------------------------------------------------------------------------------------
//                                                     INSTANCE-EXTENSION STORAGE
//------------------------------------------------------------------------------------------------------------------------

// The two extensions a Win32 Vulkan surface needs. Storage is process-lifetime so the returned pointer stays valid.
static const char* RequiredInstanceExtensionNames[] =
{
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
};


//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Translate a Win32 virtual-key code into the portable KeyIdentity. Only the keys the editor reads are mapped; anything else
// returns Unknown so it never lands in the held table. VK_A..VK_Z are the ASCII letter codes ('A'..'Z'); the modifiers use the
// side-specific VK_L*/VK_R* codes so left / right are distinguished.
static KeyIdentity TranslateVirtualKey(WPARAM VirtualKey)
{
    if (VirtualKey >= 'A' && VirtualKey <= 'Z')
        return static_cast<KeyIdentity>(static_cast<int>(KeyIdentity::A) + (int)(VirtualKey - 'A'));

    switch (VirtualKey)
    {
        case VK_LSHIFT:   return KeyIdentity::LeftShift;
        case VK_RSHIFT:   return KeyIdentity::RightShift;
        case VK_LCONTROL: return KeyIdentity::LeftControl;
        case VK_RCONTROL: return KeyIdentity::RightControl;
        case VK_LMENU:    return KeyIdentity::LeftAlt;
        case VK_RMENU:    return KeyIdentity::RightAlt;
        case VK_F1:       return KeyIdentity::F1;
        case VK_F2:       return KeyIdentity::F2;
        case VK_F3:       return KeyIdentity::F3;
        case VK_F4:       return KeyIdentity::F4;
        case VK_F5:       return KeyIdentity::F5;
        case VK_F6:       return KeyIdentity::F6;
        case VK_F7:       return KeyIdentity::F7;
        case VK_F8:       return KeyIdentity::F8;
        case VK_F9:       return KeyIdentity::F9;
        case VK_F10:      return KeyIdentity::F10;
        case VK_F11:      return KeyIdentity::F11;
        case VK_F12:      return KeyIdentity::F12;
        case VK_SPACE:    return KeyIdentity::Space;
        case VK_ESCAPE:   return KeyIdentity::Escape;
        default:          return KeyIdentity::Unknown;
    }
}

// Fold a key make / break into the InputPacket. The generic VK_SHIFT / VK_CONTROL / VK_MENU that WM_KEYDOWN also delivers are
// resolved to their side via the scancode + extended bit so left / right stay distinct; the side-specific codes pass straight
// through TranslateVirtualKey.
static void ResolveKeyMessage(PlatformWindow& Owner, WPARAM VirtualKey, LPARAM MessageData, bool Held)
{
    if (VirtualKey == VK_SHIFT)
        VirtualKey = MapVirtualKeyW((UINT)((MessageData >> 16) & 0xFF), MAPVK_VSC_TO_VK_EX);
    else if (VirtualKey == VK_CONTROL)
        VirtualKey = ((MessageData >> 24) & 0x1) ? VK_RCONTROL : VK_LCONTROL;
    else if (VirtualKey == VK_MENU)
        VirtualKey = ((MessageData >> 24) & 0x1) ? VK_RMENU : VK_LMENU;

    const KeyIdentity Key = TranslateVirtualKey(VirtualKey);
    if (Key != KeyIdentity::Unknown)
        Owner.Input.KeyHeld[static_cast<int>(Key)] = Held;
}

// Fold one WM_INPUT raw-mouse report's relative motion into the pointer-delta accumulator. Relative mode (the default) carries
// signed device counts in lLastX / lLastY since the last report; absolute-mode reports (rare — some tablets / RDP) are skipped
// since a single absolute sample yields no delta. Summing every sub-frame report here is what keeps a slow drag smooth.
static void AccumulateRawMouse(PlatformWindow& Owner, const RAWMOUSE& Mouse)
{
    if ((Mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0)
        return;
    Owner.Input.PointerDeltaX += (double)Mouse.lLastX;
    Owner.Input.PointerDeltaY += (double)Mouse.lLastY;
}

// Read one WM_INPUT report and route the mouse half into the accumulator. Keyboard raw is not needed (WM_KEYDOWN / WM_KEYUP
// already give reliable make / break for the portable key set), and HID / gamepad is out of scope.
static void AccumulateRawInput(PlatformWindow& Owner, LPARAM MessageData)
{
    UINT DataSize = 0;
    if (GetRawInputData((HRAWINPUT)MessageData, RID_INPUT, nullptr, &DataSize, sizeof(RAWINPUTHEADER)) != 0)
        return;
    if (DataSize == 0 || DataSize > sizeof(RAWINPUT))
        return;

    RAWINPUT Raw = {};
    if (GetRawInputData((HRAWINPUT)MessageData, RID_INPUT, &Raw, &DataSize, sizeof(RAWINPUTHEADER)) != DataSize)
        return;

    if (Raw.header.dwType == RIM_TYPEMOUSE)
        AccumulateRawMouse(Owner, Raw.data.mouse);
}

// The window proc. Every message routes here; the owner is recovered from the window's user data. WM_INPUT feeds raw mouse
// motion; keyboard / button messages fold level state; size / iconify / close raise the backstop flags the render loop reads.
static LRESULT CALLBACK PlatformWindowProc(HWND Window, UINT Message, WPARAM WordParam, LPARAM MessageData)
{
    PlatformWindow* Owner = reinterpret_cast<PlatformWindow*>(GetWindowLongPtrW(Window, GWLP_USERDATA));
    if (Owner == nullptr)
        return DefWindowProcW(Window, Message, WordParam, MessageData);

    // The UI-layer observer (ImGui) sees every message first. A non-zero return means it consumed the message — the camera
    // still reads the raw-input delta stream (unaffected by ImGui's want-capture), but the ordinary button / wheel state below
    // is suppressed so a click on a widget does not also fall through to the viewport.
    if (Owner->MessageObserver != nullptr)
    {
        const int Consumed = Owner->MessageObserver(Owner->MessageObserverContext, (void*)Window,
                                                    (uint32_t)Message, (uint64_t)WordParam, (int64_t)MessageData);
        if (Consumed != 0)
            return DefWindowProcW(Window, Message, WordParam, MessageData);
    }

    switch (Message)
    {
        case WM_INPUT:
            AccumulateRawInput(*Owner, MessageData);
            return DefWindowProcW(Window, Message, WordParam, MessageData);   // let the system clean up the raw buffer

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            ResolveKeyMessage(*Owner, WordParam, MessageData, true);
            return 0;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            ResolveKeyMessage(*Owner, WordParam, MessageData, false);
            return 0;

        case WM_LBUTTONDOWN: Owner->Input.ButtonHeld[(int)PointerButton::Left]   = true;  SetCapture(Window); return 0;
        case WM_LBUTTONUP:   Owner->Input.ButtonHeld[(int)PointerButton::Left]   = false; ReleaseCapture();   return 0;
        case WM_RBUTTONDOWN: Owner->Input.ButtonHeld[(int)PointerButton::Right]  = true;  SetCapture(Window); return 0;
        case WM_RBUTTONUP:   Owner->Input.ButtonHeld[(int)PointerButton::Right]  = false; ReleaseCapture();   return 0;
        case WM_MBUTTONDOWN: Owner->Input.ButtonHeld[(int)PointerButton::Middle] = true;  SetCapture(Window); return 0;
        case WM_MBUTTONUP:   Owner->Input.ButtonHeld[(int)PointerButton::Middle] = false; ReleaseCapture();   return 0;

        case WM_MOUSEMOVE:
            // Absolute position for UI hit-testing. Motion for the camera comes from WM_INPUT, not this, so a captured drag
            // stays un-quantized; this only tracks where the visible cursor sits.
            Owner->Input.PointerPositionX = (double)GET_X_LPARAM(MessageData);
            Owner->Input.PointerPositionY = (double)GET_Y_LPARAM(MessageData);
            return 0;

        case WM_MOUSEWHEEL:
            Owner->Input.ScrollDelta += (double)GET_WHEEL_DELTA_WPARAM(WordParam) / (double)WHEEL_DELTA;
            return 0;

        case WM_SIZE:
        {
            const uint32_t Width  = (uint32_t)LOWORD(MessageData);
            const uint32_t Height = (uint32_t)HIWORD(MessageData);
            Owner->MinimizedCondition = (WordParam == SIZE_MINIMIZED) || (Width == 0) || (Height == 0);
            if (!Owner->MinimizedCondition)
            {
                Owner->FramebufferWidth  = Width;
                Owner->FramebufferHeight = Height;
                Owner->RebuildRequested  = true;   // backstop: mark the swapchain stale on every real resize
            }
            return 0;
        }

        case WM_GETMINMAXINFO:
        {
            MINMAXINFO* MinMax = reinterpret_cast<MINMAXINFO*>(MessageData);
            MinMax->ptMinTrackSize.x = MinimumWidth;
            MinMax->ptMinTrackSize.y = MinimumHeight;
            return 0;
        }

        case WM_CLOSE:
            Owner->CloseRequested = true;
            return 0;

        case WM_DESTROY:
            Owner->CloseRequested = true;
            return 0;

        default:
            return DefWindowProcW(Window, Message, WordParam, MessageData);
    }
}

// Toggle borderless fullscreen: drop the decoration and cover the window's monitor, restoring the saved windowed rect + style
// on exit. The subsequent WM_SIZE raises RebuildRequested, so the swapchain follows the new extent.
static void ToggleFullscreen(PlatformWindow& Window, PlatformWindowWin32State& State)
{
    Window.FullscreenEnabled = !Window.FullscreenEnabled;
    if (Window.FullscreenEnabled)
    {
        GetWindowRect(State.Window, &State.WindowedRect);
        State.WindowedStyle = (DWORD)GetWindowLongPtrW(State.Window, GWL_STYLE);

        HMONITOR Monitor = MonitorFromWindow(State.Window, MONITOR_DEFAULTTONEAREST);
        MONITORINFO MonitorInfo = { sizeof(MONITORINFO) };
        if (!GetMonitorInfoW(Monitor, &MonitorInfo))
        {
            Window.FullscreenEnabled = false;
            return;
        }
        const RECT& Area = MonitorInfo.rcMonitor;
        SetWindowLongPtrW(State.Window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(State.Window, HWND_TOP, Area.left, Area.top,
                     Area.right - Area.left, Area.bottom - Area.top, SWP_FRAMECHANGED);
    }
    else
    {
        SetWindowLongPtrW(State.Window, GWL_STYLE, State.WindowedStyle);
        SetWindowPos(State.Window, HWND_TOP,
                     State.WindowedRect.left, State.WindowedRect.top,
                     State.WindowedRect.right - State.WindowedRect.left,
                     State.WindowedRect.bottom - State.WindowedRect.top, SWP_FRAMECHANGED);
    }
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

    PlatformWindowWin32State* State = new PlatformWindowWin32State{};
    State->Instance = GetModuleHandleW(nullptr);
    State->Owner    = &Window;
    Window.NativeState = State;

    // ① Register the window class once per process. A second window reuses the class (RegisterClassEx fails with
    //    ERROR_CLASS_ALREADY_EXISTS, which is fine). CS_OWNDC keeps a private device context for the window.
    WNDCLASSEXW ClassInfo = {};
    ClassInfo.cbSize        = sizeof(WNDCLASSEXW);
    ClassInfo.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    ClassInfo.lpfnWndProc   = PlatformWindowProc;
    ClassInfo.hInstance     = State->Instance;
    ClassInfo.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    ClassInfo.lpszClassName = WindowClassName;
    if (RegisterClassExW(&ClassInfo) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        FinalizePlatformWindow(Window);
        return false;
    }

    // ② Size the window so its CLIENT area equals the requested framebuffer, then create it decorated + resizable.
    RECT Rect = { 0, 0, (LONG)RequestedWidth, (LONG)RequestedHeight };
    const DWORD Style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRect(&Rect, Style, FALSE);

    // Title is UTF-8; widen it for the wide-char window. A missing title falls back to "Frontier".
    wchar_t TitleWide[512];
    if (TitleText != nullptr && TitleText[0] != '\0')
        MultiByteToWideChar(CP_UTF8, 0, TitleText, -1, TitleWide, 512);
    else
        wcscpy_s(TitleWide, L"Frontier");

    State->Window = CreateWindowExW(0, WindowClassName, TitleWide, Style,
                                    CW_USEDEFAULT, CW_USEDEFAULT,
                                    Rect.right - Rect.left, Rect.bottom - Rect.top,
                                    nullptr, nullptr, State->Instance, nullptr);
    if (State->Window == nullptr)
    {
        FinalizePlatformWindow(Window);
        return false;
    }

    // Stash the owner on the window so the proc recovers it, then show the window.
    SetWindowLongPtrW(State->Window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&Window));
    ShowWindow(State->Window, SW_SHOW);
    UpdateWindow(State->Window);

    // ③ Register the raw mouse for WM_INPUT on this window. No RIDEV_INPUTSINK: raw motion is wanted only while focused (a
    //    look drag always is). Failure is non-fatal — WM_MOUSEMOVE still tracks position; only the un-quantized delta is lost.
    RAWINPUTDEVICE RawMouse = {};
    RawMouse.usUsagePage = 0x01;
    RawMouse.usUsage     = 0x02;   // generic desktop mouse
    RawMouse.dwFlags     = 0;
    RawMouse.hwndTarget  = State->Window;
    RegisterRawInputDevices(&RawMouse, 1, sizeof(RAWINPUTDEVICE));

    // ④ Seed the true client extent (may differ from the requested size once the OS placed the window).
    uint32_t SeedWidth  = 0;
    uint32_t SeedHeight = 0;
    QueryFramebufferExtent(Window, SeedWidth, SeedHeight);
    return true;
}

bool ConstructPresentationSurface(PlatformWindow& Window, VkInstance VulkanInstance)
{
    PlatformWindowWin32State* State = reinterpret_cast<PlatformWindowWin32State*>(Window.NativeState);
    if (State == nullptr || State->Window == nullptr)
        return false;

    VkWin32SurfaceCreateInfoKHR SurfaceInfo = {};
    SurfaceInfo.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    SurfaceInfo.hinstance = State->Instance;
    SurfaceInfo.hwnd      = State->Window;
    return vkCreateWin32SurfaceKHR(VulkanInstance, &SurfaceInfo, nullptr, &Window.PresentationSurface) == VK_SUCCESS;
}

const char** QueryRequiredInstanceExtensions(uint32_t& ExtensionCount)
{
    ExtensionCount = (uint32_t)(sizeof(RequiredInstanceExtensionNames) / sizeof(RequiredInstanceExtensionNames[0]));
    return const_cast<const char**>(RequiredInstanceExtensionNames);
}

void QueryFramebufferExtent(PlatformWindow& Window, uint32_t& Width, uint32_t& Height)
{
    PlatformWindowWin32State* State = reinterpret_cast<PlatformWindowWin32State*>(Window.NativeState);
    if (State != nullptr && State->Window != nullptr)
    {
        RECT Client = {};
        GetClientRect(State->Window, &Client);
        Window.FramebufferWidth  = (uint32_t)(Client.right - Client.left);
        Window.FramebufferHeight = (uint32_t)(Client.bottom - Client.top);
    }
    Width  = Window.FramebufferWidth;
    Height = Window.FramebufferHeight;
}

double QueryMonotonicSeconds()
{
    static LARGE_INTEGER Frequency = {};
    if (Frequency.QuadPart == 0)
        QueryPerformanceFrequency(&Frequency);
    LARGE_INTEGER Now;
    QueryPerformanceCounter(&Now);
    return (double)Now.QuadPart / (double)Frequency.QuadPart;
}

void PollPlatformEvents(PlatformWindow& Window)
{
    PlatformWindowWin32State* State = reinterpret_cast<PlatformWindowWin32State*>(Window.NativeState);
    if (State == nullptr)
        return;

    // Roll the per-poll accumulators to zero BEFORE draining messages, so this poll's deltas reflect only this poll's reports.
    // Level state (KeyHeld / ButtonHeld / PointerPosition) persists across polls — only the deltas reset.
    Window.Input.PointerDeltaX = 0.0;
    Window.Input.PointerDeltaY = 0.0;
    Window.Input.ScrollDelta   = 0.0;

    MSG Message;
    while (PeekMessageW(&Message, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&Message);
        DispatchMessageW(&Message);
    }

    // Resolve the F11 fullscreen toggle on the rising edge (level state was folded by the proc).
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
    const PlatformWindowWin32State* State = reinterpret_cast<const PlatformWindowWin32State*>(Window.NativeState);
    return (State != nullptr) ? (void*)State->Window : nullptr;
}

void InstallMessageObserver(PlatformWindow& Window, PlatformMessageObserver Observer, void* ObserverContext)
{
    Window.MessageObserver        = Observer;
    Window.MessageObserverContext = ObserverContext;
}

void FinalizePlatformWindow(PlatformWindow& Window)
{
    PlatformWindowWin32State* State = reinterpret_cast<PlatformWindowWin32State*>(Window.NativeState);
    if (State != nullptr)
    {
        if (State->Window != nullptr)
        {
            // Unregister the raw mouse (RIDEV_REMOVE requires a null target) before the window goes away.
            RAWINPUTDEVICE RawMouse = {};
            RawMouse.usUsagePage = 0x01;
            RawMouse.usUsage     = 0x02;
            RawMouse.dwFlags     = RIDEV_REMOVE;
            RawMouse.hwndTarget  = nullptr;
            RegisterRawInputDevices(&RawMouse, 1, sizeof(RAWINPUTDEVICE));

            SetWindowLongPtrW(State->Window, GWLP_USERDATA, 0);
            DestroyWindow(State->Window);
        }
        delete State;
    }
    Window.NativeState = nullptr;
    Window.PresentationSurface = VK_NULL_HANDLE;
}

}   // namespace Frontier

#endif  // _WIN32
