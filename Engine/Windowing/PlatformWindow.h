/*============================================================================================================================================
                                                      PLATFORMWINDOW.H
============================================================================================================================================*/
// 🧩 Cross-platform window and surface interface with style flags, application presets, and dynamic fullscreen mode transitions

#pragma once

#include "Engine/Input/InputExtension.h"

#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>

namespace Frontier::Windowing
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class WindowPlatformCategory : uint8_t
{
    HeadlessStub = 0, // [-] - Deterministic offscreen validation surface
    Win32        = 1, // [-] - Windows HWND / DXGI / Vulkan surface
    X11          = 2, // [-] - Linux Xlib / XCB surface
    Wayland      = 3, // [-] - Linux Wayland XDG surface
    Android      = 4, // [-] - Android NDK ANativeWindow surface
    GlfwNull     = 5, // [-] - GLFW 3.4 offscreen desktop state-machine backend
};

enum class WindowPresentationMode : uint8_t
{
    SplashBorderless     = 0, // [-] - Frameless centered startup splash surface (undecorated, non-resizable)
    Windowed             = 1, // [-] - Standard decorated movable/resizable desktop application surface
    MaximizedWindowed    = 2, // [-] - Decorated desktop surface occupying full monitor work-area
    BorderlessFullscreen = 3, // [-] - Undecorated surface covering full monitor bounds without video-mode switch
    ExclusiveFullscreen  = 4, // [-] - Dedicated hardware video-mode fullscreen surface for game applications
};

enum class WindowStyleFlags : uint32_t
{
    None              = 0u,       // [mask] - All optional window attributes cleared
    DecoratedFrame    = 1u << 0,  // [mask] - OS title bar, frame borders, and caption buttons (0 = borderless)
    ResizableBorder   = 1u << 1,  // [mask] - User drag-resizable frame edges and corners
    CenteredOnMonitor = 1u << 2,  // [mask] - Center window within target monitor work-area upon placement
    FloatingTopmost   = 1u << 3,  // [mask] - Keep surface above normal Z-order peers (splash / tool palettes)
    AutoMinimizeFocus = 1u << 4,  // [mask] - Auto-minimize exclusive fullscreen game surface on focus loss
    HighDpiScale      = 1u << 5,  // [mask] - Per-monitor physical framebuffer DPI scaling
    TransparentClient = 1u << 6,  // [mask] - Alpha-composited client background (shaped splash surfaces)
    AcceptFileDrops   = 1u << 7,  // [mask] - Deliver OS drag-and-drop asset paths to workspaces
    StartVisible      = 1u << 8,  // [mask] - Map window immediately upon creation (0 = hidden until first frame)
    FocusOnShow       = 1u << 9,  // [mask] - Acquire keyboard focus when shown (0 = passive splash / overlay)

    DefaultSplashFlags = CenteredOnMonitor | FloatingTopmost | HighDpiScale | StartVisible,
    DefaultAppFlags    = DecoratedFrame | ResizableBorder | CenteredOnMonitor | HighDpiScale |
                         AcceptFileDrops | StartVisible | FocusOnShow,
    DefaultGameFlags   = DecoratedFrame | ResizableBorder | CenteredOnMonitor | AutoMinimizeFocus |
                         HighDpiScale | StartVisible | FocusOnShow,
};

[[nodiscard]] constexpr WindowStyleFlags operator|(WindowStyleFlags Left, WindowStyleFlags Right) noexcept
{
    return static_cast<WindowStyleFlags>(static_cast<uint32_t>(Left) | static_cast<uint32_t>(Right));
}

[[nodiscard]] constexpr WindowStyleFlags operator&(WindowStyleFlags Left, WindowStyleFlags Right) noexcept
{
    return static_cast<WindowStyleFlags>(static_cast<uint32_t>(Left) & static_cast<uint32_t>(Right));
}

[[nodiscard]] constexpr WindowStyleFlags operator^(WindowStyleFlags Left, WindowStyleFlags Right) noexcept
{
    return static_cast<WindowStyleFlags>(static_cast<uint32_t>(Left) ^ static_cast<uint32_t>(Right));
}

[[nodiscard]] constexpr WindowStyleFlags operator~(WindowStyleFlags Flags) noexcept
{
    return static_cast<WindowStyleFlags>(~static_cast<uint32_t>(Flags));
}

constexpr WindowStyleFlags& operator|=(WindowStyleFlags& Left, WindowStyleFlags Right) noexcept
{
    Left = Left | Right;
    return Left;
}

constexpr WindowStyleFlags& operator&=(WindowStyleFlags& Left, WindowStyleFlags Right) noexcept
{
    Left = Left & Right;
    return Left;
}

[[nodiscard]] constexpr bool HasWindowFlag(WindowStyleFlags Mask, WindowStyleFlags Flag) noexcept
{
    return (static_cast<uint32_t>(Mask) & static_cast<uint32_t>(Flag)) == static_cast<uint32_t>(Flag);
}

[[nodiscard]] constexpr WindowStyleFlags ReconcileFlagsForMode(
    WindowPresentationMode Mode,
    WindowStyleFlags       RequestedFlags) noexcept
{
    switch (Mode)
    {
        case WindowPresentationMode::SplashBorderless:
            return RequestedFlags & ~(WindowStyleFlags::DecoratedFrame | WindowStyleFlags::ResizableBorder);

        case WindowPresentationMode::BorderlessFullscreen:
        case WindowPresentationMode::ExclusiveFullscreen:
            return RequestedFlags & ~(WindowStyleFlags::DecoratedFrame | WindowStyleFlags::ResizableBorder);

        case WindowPresentationMode::Windowed:
        case WindowPresentationMode::MaximizedWindowed:
        default:
            return RequestedFlags;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct WindowPlacementBounds
{
    int32_t  OriginX     = 120;  // [px] - Desktop screen-space X coordinate of top-left corner
    int32_t  OriginY     = 80;   // [px] - Desktop screen-space Y coordinate of top-left corner
    uint32_t PixelWidth  = 1600; // [px] - Client logical width in windowed mode
    uint32_t PixelHeight = 900;  // [px] - Client logical height in windowed mode
};

struct WindowConfigurationDescriptor
{
    std::string_view       TitleText           = "Frontier";                        // [-]   - Surface caption text
    int32_t                OriginX             = 120;                               // [px]  - Initial X position when not centered
    int32_t                OriginY             = 80;                                // [px]  - Initial Y position when not centered
    uint32_t               PixelWidth          = 1600;                              // [px]  - Initial client surface width
    uint32_t               PixelHeight         = 900;                               // [px]  - Initial client surface height
    uint32_t               MinimumWidth        = 320;                               // [px]  - Minimum interactive resize width
    uint32_t               MinimumHeight       = 200;                               // [px]  - Minimum interactive resize height
    uint32_t               TargetRefreshRateHz = 0;                                 // [Hz]  - 0 = monitor native refresh rate
    uint32_t               TargetMonitorIndex  = 0;                                 // [idx] - 0 = primary monitor
    float                  DpiScaleFactor      = 1.0f;                              // [-]   - Logical-to-physical pixel ratio
    WindowPresentationMode PresentationMode    = WindowPresentationMode::Windowed;  // [-]   - Initial presentation mode
    WindowStyleFlags       StyleFlags          = WindowStyleFlags::DefaultAppFlags; // [mask]- Composable window attributes

    [[nodiscard]] static constexpr WindowConfigurationDescriptor MakeSplashWindow(
        std::string_view Title       = "Frontier",
        uint32_t         SplashWidth = 720,
        uint32_t         SplashHeight = 420) noexcept
    {
        WindowConfigurationDescriptor Descriptor{};
        Descriptor.TitleText        = Title;
        Descriptor.PixelWidth       = SplashWidth;
        Descriptor.PixelHeight      = SplashHeight;
        Descriptor.MinimumWidth     = SplashWidth;
        Descriptor.MinimumHeight    = SplashHeight;
        Descriptor.PresentationMode = WindowPresentationMode::SplashBorderless;
        Descriptor.StyleFlags       = WindowStyleFlags::DefaultSplashFlags;
        return Descriptor;
    }

    [[nodiscard]] static constexpr WindowConfigurationDescriptor MakeApplicationWindow(
        std::string_view Title          = "Frontier",
        uint32_t         ClientWidth    = 1600,
        uint32_t         ClientHeight   = 900,
        bool             StartMaximized = false) noexcept
    {
        WindowConfigurationDescriptor Descriptor{};
        Descriptor.TitleText        = Title;
        Descriptor.PixelWidth       = ClientWidth;
        Descriptor.PixelHeight      = ClientHeight;
        Descriptor.MinimumWidth     = 960;
        Descriptor.MinimumHeight    = 540;
        Descriptor.PresentationMode = StartMaximized ? WindowPresentationMode::MaximizedWindowed
                                                     : WindowPresentationMode::Windowed;
        Descriptor.StyleFlags       = WindowStyleFlags::DefaultAppFlags;
        return Descriptor;
    }

    [[nodiscard]] static constexpr WindowConfigurationDescriptor MakeGameWindow(
        std::string_view       Title         = "Frontier Game",
        uint32_t               SurfaceWidth  = 1920,
        uint32_t               SurfaceHeight = 1080,
        WindowPresentationMode Mode          = WindowPresentationMode::Windowed,
        uint32_t               RefreshRateHz = 0) noexcept
    {
        WindowConfigurationDescriptor Descriptor{};
        Descriptor.TitleText           = Title;
        Descriptor.PixelWidth          = SurfaceWidth;
        Descriptor.PixelHeight         = SurfaceHeight;
        Descriptor.MinimumWidth        = 640;
        Descriptor.MinimumHeight       = 360;
        Descriptor.TargetRefreshRateHz = RefreshRateHz;
        Descriptor.PresentationMode    = Mode;
        Descriptor.StyleFlags          = ReconcileFlagsForMode(Mode, WindowStyleFlags::DefaultGameFlags);
        return Descriptor;
    }
};

struct WindowExtentReading
{
    int32_t  OriginX           = 0;     // [px] - Current desktop X position
    int32_t  OriginY           = 0;     // [px] - Current desktop Y position
    uint32_t PixelWidth        = 0;     // [px] - Current client logical width
    uint32_t PixelHeight       = 0;     // [px] - Current client logical height
    uint32_t FramebufferWidth  = 0;     // [px] - Current physical swapchain-drawable width
    uint32_t FramebufferHeight = 0;     // [px] - Current physical swapchain-drawable height
    uint32_t RefreshRateHz     = 60;    // [Hz] - Active display refresh rate
    float    DpiScaleFactor    = 1.0f;  // [-]  - Active display DPI scale factor
    bool     Minimized         = false; // [-]  - True when iconified or zero drawable area
    bool     Maximized         = false; // [-]  - True when occupying maximized work-area
    bool     Focused           = true;  // [-]  - True when holding OS keyboard focus
    bool     Visible           = true;  // [-]  - True when mapped on screen
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      CONCEPTS
//------------------------------------------------------------------------------------------------------------------------

template <typename SurfaceType>
concept PlatformWindowSurface = requires(
    SurfaceType&                         Surface,
    const WindowConfigurationDescriptor& Descriptor,
    Input::InputExtension&               InputCoordinator,
    WindowPresentationMode               Mode,
    WindowStyleFlags                     Flags)
{
    { Surface.Initialize(Descriptor) }                noexcept -> std::same_as<bool>;
    { Surface.PollOperatingEvents(InputCoordinator) } noexcept -> std::same_as<void>;
    { Surface.ApplyPresentationMode(Mode, 0u, 0u, 0u) } noexcept -> std::same_as<bool>;
    { Surface.ApplyStyleFlags(Flags) }                noexcept -> std::same_as<bool>;
    { Surface.Finalize() }                            noexcept -> std::same_as<void>;
    { Surface.CloseRequested() }                      noexcept -> std::same_as<bool>;
    { Surface.InspectExtent() }                       noexcept -> std::same_as<WindowExtentReading>;
    { Surface.ActivePresentationMode() }              noexcept -> std::same_as<WindowPresentationMode>;
    { Surface.ActiveStyleFlags() }                    noexcept -> std::same_as<WindowStyleFlags>;
};

//------------------------------------------------------------------------------------------------------------------------
//                                            HEADLESS VALIDATION SURFACE
//------------------------------------------------------------------------------------------------------------------------

class HeadlessPlatformWindow
{
public:
    HeadlessPlatformWindow() noexcept = default;

    //--------------------------------------------------------------------------------------------------------------------
    //                                                FUNCTION PROTOTYPES
    //--------------------------------------------------------------------------------------------------------------------

    [[nodiscard]] bool Initialize(const WindowConfigurationDescriptor& Descriptor) noexcept;
    void               PollOperatingEvents(Input::InputExtension& InputCoordinator) noexcept;
    void               Finalize() noexcept;

    [[nodiscard]] bool ApplyPresentationMode(
        WindowPresentationMode NewMode,
        uint32_t               FullscreenWidth  = 0,
        uint32_t               FullscreenHeight = 0,
        uint32_t               RefreshRateHz    = 0) noexcept;
    [[nodiscard]] bool ApplyStyleFlags(WindowStyleFlags NewFlags) noexcept;
    void               ApplyTitle(std::string_view NewTitle) noexcept;
    void               ApplySizeLimits(
        uint32_t MinWidth,
        uint32_t MinHeight,
        uint32_t MaxWidth  = 0,
        uint32_t MaxHeight = 0) noexcept;
    void               MoveSurface(int32_t NewOriginX, int32_t NewOriginY) noexcept;
    void               ResizeSurface(uint32_t NewWidth, uint32_t NewHeight, float NewDpiScale) noexcept;
    void               RequestClose() noexcept;
    [[nodiscard]] bool QueueSyntheticSignal(const Input::InputSignalPacket& Packet) noexcept;

    void ApplyCursorConfinement(Input::CursorConfinementMode Confinement) noexcept;
    void ApplyHapticFeedback(const Input::HapticFeedbackDirective& Haptic) noexcept;

    [[nodiscard]] bool                         CloseRequested()          const noexcept;
    [[nodiscard]] WindowExtentReading          InspectExtent()           const noexcept;
    [[nodiscard]] WindowPlatformCategory       PlatformCategory()        const noexcept;
    [[nodiscard]] WindowPresentationMode       ActivePresentationMode()  const noexcept;
    [[nodiscard]] WindowStyleFlags             ActiveStyleFlags()        const noexcept;
    [[nodiscard]] WindowPlacementBounds        SavedWindowedBounds()     const noexcept;
    [[nodiscard]] std::string_view             TitleText()               const noexcept;
    [[nodiscard]] Input::CursorConfinementMode AppliedCursorMode()       const noexcept;
    [[nodiscard]] uint32_t                     AppliedHapticPulseCount() const noexcept;

private:
    void RecomputeFramebufferExtent() noexcept;

    Input::InputExchange         PendingSyntheticExchange{};                                               // [-]     - Synthetic OS events queued for next poll
    std::string                  CaptionText          = "Frontier";                                        // [-]     - Current window caption
    WindowExtentReading          ActiveExtent{};                                                           // [px]    - Current simulated surface metrics
    WindowPlacementBounds        RestorableBounds{};                                                       // [px]    - Last windowed position/size before fullscreen
    WindowStyleFlags             AuthoringFlags       = WindowStyleFlags::DefaultAppFlags;                 // [mask]  - Windowed base flags restored on exit fullscreen
    WindowStyleFlags             EffectiveFlags       = WindowStyleFlags::DefaultAppFlags;                 // [mask]  - Active flags reconciled with presentation mode
    WindowPresentationMode       CurrentMode          = WindowPresentationMode::Windowed;                  // [-]     - Current presentation mode
    uint32_t                     MinWidthLimit        = 1;                                                 // [px]    - Minimum resize clamp width
    uint32_t                     MinHeightLimit       = 1;                                                 // [px]    - Minimum resize clamp height
    uint32_t                     MaxWidthLimit        = 0;                                                 // [px]    - Maximum resize clamp width (0 = unbounded)
    uint32_t                     MaxHeightLimit       = 0;                                                 // [px]    - Maximum resize clamp height (0 = unbounded)
    Input::CursorConfinementMode CurrentCursorMode    = Input::CursorConfinementMode::FreeVisible;         // [-]     - Latest cursor confinement applied by Engine
    uint32_t                     DeliveredHapticCount = 0;                                                 // [count] - Outbound haptic directives received
    bool                         SurfaceInitialized   = false;                                             // [-]     - True while headless window is active
    bool                         ClosingRequested     = false;                                             // [-]     - True once RequestClose has been triggered
};

static_assert(PlatformWindowSurface<HeadlessPlatformWindow>,
              "HeadlessPlatformWindow must satisfy the PlatformWindowSurface C++20 concept.");
static_assert(Input::InputFeedbackTarget<HeadlessPlatformWindow>,
              "HeadlessPlatformWindow must satisfy the InputFeedbackTarget C++20 concept.");

#if !defined(__ANDROID__)

//------------------------------------------------------------------------------------------------------------------------
//                                        DESKTOP CROSS-PLATFORM SURFACE (GLFW 3.4)
//------------------------------------------------------------------------------------------------------------------------

class DesktopPlatformWindow
{
public:
    DesktopPlatformWindow() noexcept = default;
    ~DesktopPlatformWindow() noexcept;

    DesktopPlatformWindow(const DesktopPlatformWindow&)            = delete;
    DesktopPlatformWindow& operator=(const DesktopPlatformWindow&) = delete;

    //--------------------------------------------------------------------------------------------------------------------
    //                                                FUNCTION PROTOTYPES
    //--------------------------------------------------------------------------------------------------------------------

    [[nodiscard]] bool Initialize(const WindowConfigurationDescriptor& Descriptor) noexcept;
    void               PollOperatingEvents(Input::InputExtension& InputCoordinator) noexcept;
    void               Finalize() noexcept;

    [[nodiscard]] bool ApplyPresentationMode(
        WindowPresentationMode NewMode,
        uint32_t               FullscreenWidth  = 0,
        uint32_t               FullscreenHeight = 0,
        uint32_t               RefreshRateHz    = 0) noexcept;
    [[nodiscard]] bool ApplyStyleFlags(WindowStyleFlags NewFlags) noexcept;
    void               ApplyTitle(std::string_view NewTitle) noexcept;
    void               ApplySizeLimits(
        uint32_t MinWidth,
        uint32_t MinHeight,
        uint32_t MaxWidth  = 0,
        uint32_t MaxHeight = 0) noexcept;
    void               MoveSurface(int32_t NewOriginX, int32_t NewOriginY) noexcept;
    void               ResizeSurface(uint32_t NewWidth, uint32_t NewHeight) noexcept;
    void               RequestClose() noexcept;

    void ApplyCursorConfinement(Input::CursorConfinementMode Confinement) noexcept;
    void ApplyHapticFeedback(const Input::HapticFeedbackDirective& Haptic) noexcept;

    [[nodiscard]] bool                         CloseRequested()          const noexcept;
    [[nodiscard]] WindowExtentReading          InspectExtent()           const noexcept;
    [[nodiscard]] WindowPlatformCategory       PlatformCategory()        const noexcept;
    [[nodiscard]] WindowPresentationMode       ActivePresentationMode()  const noexcept;
    [[nodiscard]] WindowStyleFlags             ActiveStyleFlags()        const noexcept;
    [[nodiscard]] WindowPlacementBounds        SavedWindowedBounds()     const noexcept;
    [[nodiscard]] std::string_view             TitleText()               const noexcept;
    [[nodiscard]] Input::CursorConfinementMode AppliedCursorMode()       const noexcept;
    [[nodiscard]] uint32_t                     AppliedHapticPulseCount() const noexcept;
    [[nodiscard]] void*                        NativeGlfwHandle()        const noexcept;

private:
    void RefreshCachedExtent() noexcept;

    void*                        GlfwWindowHandle     = nullptr;                                           // [-]     - Opaque GLFWwindow* instance
    Input::InputExchange         CallbackExchange{};                                                       // [-]     - OS callback events buffered until next poll
    std::string                  CaptionText          = "Frontier";                                        // [-]     - Current window caption
    WindowExtentReading          ActiveExtent{};                                                           // [px]    - Latest queried window & framebuffer dimensions
    WindowPlacementBounds        RestorableBounds{};                                                       // [px]    - Saved windowed placement prior to fullscreen
    WindowStyleFlags             AuthoringFlags       = WindowStyleFlags::DefaultAppFlags;                 // [mask]  - Windowed base flags restored on exit fullscreen
    WindowStyleFlags             EffectiveFlags       = WindowStyleFlags::DefaultAppFlags;                 // [mask]  - Active flags reconciled with presentation mode
    WindowPresentationMode       CurrentMode          = WindowPresentationMode::Windowed;                  // [-]     - Current presentation mode
    WindowPlatformCategory       ActiveBackend        = WindowPlatformCategory::HeadlessStub;              // [-]     - Resolved runtime backend (Win32/X11/Wayland/Null)
    uint32_t                     TargetMonitorSlot    = 0;                                                 // [idx]   - Selected monitor index
    uint32_t                     MinWidthLimit        = 1;                                                 // [px]    - Minimum resize clamp width
    uint32_t                     MinHeightLimit       = 1;                                                 // [px]    - Minimum resize clamp height
    uint32_t                     MaxWidthLimit        = 0;                                                 // [px]    - Maximum resize clamp width
    uint32_t                     MaxHeightLimit       = 0;                                                 // [px]    - Maximum resize clamp height
    Input::CursorConfinementMode CurrentCursorMode    = Input::CursorConfinementMode::FreeVisible;         // [-]     - Latest cursor confinement applied
    uint32_t                     DeliveredHapticCount = 0;                                                 // [count] - Outbound haptic directives received
};

static_assert(PlatformWindowSurface<DesktopPlatformWindow>,
              "DesktopPlatformWindow must satisfy the PlatformWindowSurface C++20 concept.");
static_assert(Input::InputFeedbackTarget<DesktopPlatformWindow>,
              "DesktopPlatformWindow must satisfy the InputFeedbackTarget C++20 concept.");

#endif // !defined(__ANDROID__)

} // namespace Frontier::Windowing
