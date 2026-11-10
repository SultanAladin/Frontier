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

    PendingSyntheticSignals.Initialize();
    ActiveExtent.PixelWidth     = Descriptor.PixelWidth;
    ActiveExtent.PixelHeight    = Descriptor.PixelHeight;
    ActiveExtent.DpiScaleFactor = Descriptor.DpiScaleFactor;
    ActiveExtent.Minimized      = false;
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
            (void)Coordinator->SubmitHardwareSignal(Packet);
        }
    };

    ForwardToCoordinatorSink Forwarder{ &InputCoordinator };
    (void)PendingSyntheticSignals.RouteQueuedSignals(Forwarder);
}

void HeadlessPlatformWindow::Finalize() noexcept
{
    PendingSyntheticSignals.Finalize();
    ActiveExtent       = {};
    ClosingRequested   = false;
    SurfaceInitialized = false;
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
    return PendingSyntheticSignals.IntakeSignal(Packet);
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

WindowSubstrateCategory HeadlessPlatformWindow::SubstrateCategory() const noexcept
{
    return WindowSubstrateCategory::HeadlessStub;
}

} // namespace Frontier::Windowing
