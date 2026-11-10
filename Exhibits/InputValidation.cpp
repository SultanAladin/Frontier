/*============================================================================================================================================
                                                    INPUTVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation executable verifying BuildConfiguration profiles, RawInputRouter concepts, desktop input, and Android multi-touch

#include "Engine/Configuration/BuildConfiguration.h"
#include "Engine/Input/InputExtension.h"
#include "Engine/Windowing/PlatformWindow.h"

#include <cstdio>

namespace
{

struct CountingSignalReceiver
{
    uint32_t ObservedPackets = 0; // [count] - Total packets received through concept-constrained routing

    void AcceptInputSignal(const Frontier::Input::InputSignalPacket& Packet) noexcept
    {
        (void)Packet;
        ++ObservedPackets;
    }
};

static_assert(Frontier::Input::InputSignalSink<CountingSignalReceiver>,
              "CountingSignalReceiver must satisfy InputSignalSink.");

} // namespace

int main()
{
    using namespace Frontier;
    using namespace Frontier::Input;
    using namespace Frontier::Windowing;

    // ① Verify compile-time build profile exclusivity
    constexpr uint32_t ActiveProfileSum =
        (BuildConfiguration::EditorProfileActive ? 1u : 0u) +
        (BuildConfiguration::DevelopmentProfileActive ? 1u : 0u) +
        (BuildConfiguration::ShippingProfileActive ? 1u : 0u);
    if (ActiveProfileSum != 1u)
    {
        std::fprintf(stderr, "[FAILED] Expected exactly 1 active BuildConfiguration profile.\n");
        return 1;
    }

    // ② Initialize headless cross-platform surface and InputExtension coordinator
    HeadlessPlatformWindow Surface{};
    WindowConfigurationDescriptor SurfaceDescriptor{};
    SurfaceDescriptor.TitleText      = "FrontierValidation";
    SurfaceDescriptor.PixelWidth     = 1920;
    SurfaceDescriptor.PixelHeight    = 1080;
    SurfaceDescriptor.DpiScaleFactor = 1.5f;

    if (!Surface.Initialize(SurfaceDescriptor))
    {
        std::fprintf(stderr, "[FAILED] HeadlessPlatformWindow initialization failed.\n");
        return 1;
    }

    InputExtension Coordinator{};
    Coordinator.Initialize();
    Coordinator.BeginTick();

    // ③ Inject keyboard press, pointer motion, and two Android-style touch contacts
    InputSignalPacket KeyPress{};
    KeyPress.TimestampNanoseconds = 1000;
    KeyPress.Category             = InputSignalCategory::KeyboardKey;
    KeyPress.ControlCode          = 42;
    KeyPress.Transition           = ButtonTransitionCategory::Pressed;

    InputSignalPacket CursorMove{};
    CursorMove.TimestampNanoseconds = 2000;
    CursorMove.Category             = InputSignalCategory::PointerMotion;
    CursorMove.PrimaryAxis          = 640.0f;
    CursorMove.SecondaryAxis        = 360.0f;
    CursorMove.DeltaPrimary         = 12.5f;
    CursorMove.DeltaSecondary       = -4.0f;

    InputSignalPacket FirstFingerTouch{};
    FirstFingerTouch.TimestampNanoseconds = 3000;
    FirstFingerTouch.Category             = InputSignalCategory::TouchContact;
    FirstFingerTouch.ControlCode          = 0;
    FirstFingerTouch.TouchPhase           = TouchContactCategory::Began;
    FirstFingerTouch.PrimaryAxis          = 300.0f;
    FirstFingerTouch.SecondaryAxis        = 500.0f;
    FirstFingerTouch.DeltaPrimary         = 0.8f;

    InputSignalPacket SecondFingerTouch{};
    SecondFingerTouch.TimestampNanoseconds = 3100;
    SecondFingerTouch.Category             = InputSignalCategory::TouchContact;
    SecondFingerTouch.ControlCode          = 1;
    SecondFingerTouch.TouchPhase           = TouchContactCategory::Began;
    SecondFingerTouch.PrimaryAxis          = 700.0f;
    SecondFingerTouch.SecondaryAxis        = 520.0f;
    SecondFingerTouch.DeltaPrimary         = 0.75f;

    if (!Surface.QueueSyntheticSignal(KeyPress) ||
        !Surface.QueueSyntheticSignal(CursorMove) ||
        !Surface.QueueSyntheticSignal(FirstFingerTouch) ||
        !Surface.QueueSyntheticSignal(SecondFingerTouch))
    {
        std::fprintf(stderr, "[FAILED] Synthetic signal enqueue failed.\n");
        return 1;
    }

    // ④ Poll surface into InputExtension and route through concept-constrained sink
    Surface.PollOperatingEvents(Coordinator);

    CountingSignalReceiver Receiver{};
    const std::size_t RoutedCount = Coordinator.TickRoutingWithSink(Receiver);

    if (RoutedCount != 4u || Receiver.ObservedPackets != 4u)
    {
        std::fprintf(stderr, "[FAILED] Expected 4 routed signals, observed %zu.\n", RoutedCount);
        return 1;
    }

    if (!Coordinator.QueryScanCodeHeld(42u) || Coordinator.ActiveTouchContactCount() != 2u)
    {
        std::fprintf(stderr, "[FAILED] Scancode or multi-touch tracking mismatch.\n");
        return 1;
    }

    const PointerSnapshot& Pointer = Coordinator.InspectPointerSnapshot();
    if (Pointer.PositionX != 640.0f || Pointer.PositionY != 360.0f || Pointer.TickDeltaX != 12.5f)
    {
        std::fprintf(stderr, "[FAILED] Pointer snapshot mismatch.\n");
        return 1;
    }

    // ⑤ Lift one touch contact and verify active touch count decrements to 1
    InputSignalPacket FirstFingerLift = FirstFingerTouch;
    FirstFingerLift.TimestampNanoseconds = 4000;
    FirstFingerLift.TouchPhase           = TouchContactCategory::Ended;
    (void)Coordinator.IntakeHardwareSignal(FirstFingerLift);
    (void)Coordinator.TickRouting();

    if (Coordinator.ActiveTouchContactCount() != 1u)
    {
        std::fprintf(stderr, "[FAILED] Expected 1 remaining touch contact after lift.\n");
        return 1;
    }

    Coordinator.Finalize();
    Surface.Finalize();

    std::printf("[OK] InputValidation passed (EditorIncluded=%d, DiagnosticsIncluded=%d, PackagingRequired=%d)\n",
                BuildConfiguration::EditorIncluded ? 1 : 0,
                BuildConfiguration::DiagnosticsIncluded ? 1 : 0,
                BuildConfiguration::BinaryPackagingRequired ? 1 : 0);
    return 0;
}
