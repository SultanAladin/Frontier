/*============================================================================================================================================
                                                      PLATFORMWINDOW.H
============================================================================================================================================*/
// 🧩 Cross-platform native surface abstraction spanning Win32, X11, Wayland, Android NDK, and Headless validation

#pragma once

#include "Engine/Input/InputExtension.h"

#include <concepts>
#include <cstdint>
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
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct WindowConfigurationDescriptor
{
    std::string_view TitleText       = "Frontier"; // [-]   - Initial surface caption on desktop targets
    uint32_t         PixelWidth      = 1600;       // [px]  - Initial drawable surface width
    uint32_t         PixelHeight     = 900;        // [px]  - Initial drawable surface height
    float            DpiScaleFactor  = 1.0f;       // [-]   - Logical-to-physical pixel ratio
    bool             ResizableBorder = true;       // [-]   - Permits interactive border resize on desktop
};

struct WindowExtentReading
{
    uint32_t PixelWidth     = 0;     // [px] - Current swapchain-drawable width
    uint32_t PixelHeight    = 0;     // [px] - Current swapchain-drawable height
    float    DpiScaleFactor = 1.0f;  // [-]  - Active display DPI scale factor
    bool     Minimized      = false; // [-]  - True when surface area drops to zero (suspend rendering)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      CONCEPTS
//------------------------------------------------------------------------------------------------------------------------

template <typename SurfaceType>
concept PlatformWindowSurface = requires(
    SurfaceType&                         Surface,
    const WindowConfigurationDescriptor& Descriptor,
    Input::InputExtension&               InputCoordinator)
{
    { Surface.Initialize(Descriptor) }                noexcept -> std::same_as<bool>;
    { Surface.PollOperatingEvents(InputCoordinator) } noexcept -> std::same_as<void>;
    { Surface.Finalize() }                            noexcept -> std::same_as<void>;
    { Surface.CloseRequested() }                      noexcept -> std::same_as<bool>;
    { Surface.InspectExtent() }                       noexcept -> std::same_as<WindowExtentReading>;
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

    void ApplyCursorConfinement(Input::CursorConfinementMode Confinement) noexcept;
    void ApplyHapticFeedback(const Input::HapticFeedbackDirective& Haptic) noexcept;

    void               ResizeSurface(uint32_t NewWidth, uint32_t NewHeight, float NewDpiScale) noexcept;
    void               RequestClose() noexcept;
    [[nodiscard]] bool QueueSyntheticSignal(const Input::InputSignalPacket& Packet) noexcept;

    [[nodiscard]] bool                         CloseRequested()          const noexcept;
    [[nodiscard]] WindowExtentReading          InspectExtent()           const noexcept;
    [[nodiscard]] WindowPlatformCategory       PlatformCategory()        const noexcept;
    [[nodiscard]] Input::CursorConfinementMode AppliedCursorMode()       const noexcept;
    [[nodiscard]] uint32_t                     AppliedHapticPulseCount() const noexcept;

private:
    Input::InputExchange         PendingSyntheticExchange{};                                               // [-]     - Synthetic OS events queued for next poll
    WindowExtentReading          ActiveExtent{};                                                           // [px]    - Current simulated surface dimensions
    Input::CursorConfinementMode CurrentCursorMode   = Input::CursorConfinementMode::FreeVisible;          // [-]     - Latest cursor confinement applied by Engine
    uint32_t                     DeliveredHapticCount = 0;                                                 // [count] - Outbound haptic directives received
    bool                         SurfaceInitialized  = false;                                              // [-]     - True while headless window is active
    bool                         ClosingRequested    = false;                                              // [-]     - True once RequestClose has been triggered
};

static_assert(PlatformWindowSurface<HeadlessPlatformWindow>,
              "HeadlessPlatformWindow must satisfy the PlatformWindowSurface C++20 concept.");
static_assert(Input::InputFeedbackTarget<HeadlessPlatformWindow>,
              "HeadlessPlatformWindow must satisfy the InputFeedbackTarget C++20 concept.");

} // namespace Frontier::Windowing
