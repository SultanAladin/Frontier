/*============================================================================================================================================
                                                  PLATFORMWINDOWSTUB.CPP
============================================================================================================================================*/
// 🧩 Headless deterministic platform window implementation for CI, SwiftShader offscreen rendering, and Exhibits validation

#include "Engine/Windowing/PlatformWindow.h"

namespace Frontier::Windowing
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  LIFECYCLE OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

bool HeadlessPlatformWindow::Initialize(const WindowConfigurationDescriptor& Descriptor) noexcept
{
    if (Descriptor.PixelWidth == 0 || Descriptor.PixelHeight == 0)
    {
        return false;
    }

    PendingSyntheticExchange.Initialize();
    ActiveExtent.PixelWidth     = Descriptor.PixelWidth;
    ActiveExtent.PixelHeight    = Descriptor.PixelHeight;
    ActiveExtent.DpiScaleFactor = Descriptor.DpiScaleFactor;
    ActiveExtent.Minimized      = false;
    CurrentCursorMode           = Input::CursorConfinementMode::FreeVisible;
    DeliveredHapticCount        = 0;
    ClosingRequested            = false;
    SurfaceInitialized          = true;
    return true;
}

void HeadlessPlatformWindow::PollOperatingEvents(Input::InputExtension& InputCoordinator) noexcept
{
    if (!SurfaceInitialized)
    {
        return;
    }

    struct ForwardToCoordinatorSink
    {
        Input::InputExtension* Coordinator;

        void AcceptInputSignal(const Input::InputSignalPacket& Packet) noexcept
        {
            (void)Coordinator->IntakeHardwareSignal(Packet);
        }
    };

    ForwardToCoordinatorSink Forwarder{ &InputCoordinator };
    (void)PendingSyntheticExchange.DrainSignals(Forwarder);
    (void)InputCoordinator.FlushOutboundDirectives(*this);
}

void HeadlessPlatformWindow::Finalize() noexcept
{
    PendingSyntheticExchange.Finalize();
    ActiveExtent         = {};
    CurrentCursorMode    = Input::CursorConfinementMode::FreeVisible;
    DeliveredHapticCount = 0;
    ClosingRequested     = false;
    SurfaceInitialized   = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            OUTBOUND OS FEEDBACK APPLICATION
//------------------------------------------------------------------------------------------------------------------------

void HeadlessPlatformWindow::ApplyCursorConfinement(Input::CursorConfinementMode Confinement) noexcept
{
    CurrentCursorMode = Confinement;
}

void HeadlessPlatformWindow::ApplyHapticFeedback(const Input::HapticFeedbackDirective& Haptic) noexcept
{
    (void)Haptic;
    ++DeliveredHapticCount;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              SYNTHETIC SURFACE CONTROLS
//------------------------------------------------------------------------------------------------------------------------

void HeadlessPlatformWindow::ResizeSurface(uint32_t NewWidth, uint32_t NewHeight, float NewDpiScale) noexcept
{
    ActiveExtent.PixelWidth     = NewWidth;
    ActiveExtent.PixelHeight    = NewHeight;
    ActiveExtent.DpiScaleFactor = NewDpiScale;
    ActiveExtent.Minimized      = (NewWidth == 0 || NewHeight == 0);
}

void HeadlessPlatformWindow::RequestClose() noexcept
{
    ClosingRequested = true;
}

bool HeadlessPlatformWindow::QueueSyntheticSignal(const Input::InputSignalPacket& Packet) noexcept
{
    if (!SurfaceInitialized)
    {
        return false;
    }
    return PendingSyntheticExchange.IntakeSignal(Packet);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   INSPECTION QUERIES
//------------------------------------------------------------------------------------------------------------------------

bool HeadlessPlatformWindow::CloseRequested() const noexcept
{
    return ClosingRequested;
}

WindowExtentReading HeadlessPlatformWindow::InspectExtent() const noexcept
{
    return ActiveExtent;
}

WindowPlatformCategory HeadlessPlatformWindow::PlatformCategory() const noexcept
{
    return WindowPlatformCategory::HeadlessStub;
}

Input::CursorConfinementMode HeadlessPlatformWindow::AppliedCursorMode() const noexcept
{
    return CurrentCursorMode;
}

uint32_t HeadlessPlatformWindow::AppliedHapticPulseCount() const noexcept
{
    return DeliveredHapticCount;
}

} // namespace Frontier::Windowing
