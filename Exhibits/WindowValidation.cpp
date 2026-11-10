/*============================================================================================================================================
                                                    WINDOWVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation executable verifying splash, windowed, maximized, and dynamic game fullscreen transitions across window backends

#include "Engine/Input/InputExtension.h"
#include "Engine/Windowing/PlatformWindow.h"

#include <cstdio>

namespace
{

using namespace Frontier::Input;
using namespace Frontier::Windowing;

template <PlatformWindowSurface WindowBackend>
bool VerifyWindowLifecycleAndModeTransitions(const char* BackendLabel) noexcept
{
    // ① Borderless Splash Screen verification (frameless, non-resizable, floating topmost, centered, passive focus)
    {
        WindowBackend Splash{};
        const auto    SplashDesc = WindowConfigurationDescriptor::MakeSplashWindow("Frontier Splash", 720, 420);
        if (!Splash.Initialize(SplashDesc))
        {
            std::fprintf(stderr, "[FAILED] %s: Splash window initialization failed.\n", BackendLabel);
            return false;
        }

        const auto SplashFlags  = Splash.ActiveStyleFlags();
        const auto SplashExtent = Splash.InspectExtent();
        if (Splash.ActivePresentationMode() != WindowPresentationMode::SplashBorderless ||
            HasWindowFlag(SplashFlags, WindowStyleFlags::DecoratedFrame) ||
            HasWindowFlag(SplashFlags, WindowStyleFlags::ResizableBorder) ||
            !HasWindowFlag(SplashFlags, WindowStyleFlags::FloatingTopmost) ||
            SplashExtent.PixelWidth != 720 ||
            SplashExtent.PixelHeight != 420 ||
            !SplashExtent.Visible)
        {
            std::fprintf(stderr, "[FAILED] %s: Splash window flags or dimensions mismatch.\n", BackendLabel);
            return false;
        }
        Splash.Finalize();
    }

    // ② Standard Windowed + Maximized Application Window verification (shared workspace shell)
    {
        WindowBackend AppWindow{};
        const auto    AppDesc = WindowConfigurationDescriptor::MakeApplicationWindow("Frontier Workspace", 1600, 900, false);
        if (!AppWindow.Initialize(AppDesc))
        {
            std::fprintf(stderr, "[FAILED] %s: Application window initialization failed.\n", BackendLabel);
            return false;
        }

        const auto AppFlags = AppWindow.ActiveStyleFlags();
        if (AppWindow.ActivePresentationMode() != WindowPresentationMode::Windowed ||
            !HasWindowFlag(AppFlags, WindowStyleFlags::DecoratedFrame) ||
            !HasWindowFlag(AppFlags, WindowStyleFlags::ResizableBorder) ||
            !HasWindowFlag(AppFlags, WindowStyleFlags::AcceptFileDrops))
        {
            std::fprintf(stderr, "[FAILED] %s: Application window flags mismatch.\n", BackendLabel);
            return false;
        }

        if (!AppWindow.ApplyPresentationMode(WindowPresentationMode::MaximizedWindowed) ||
            AppWindow.ActivePresentationMode() != WindowPresentationMode::MaximizedWindowed ||
            !AppWindow.InspectExtent().Maximized)
        {
            std::fprintf(stderr, "[FAILED] %s: MaximizedWindowed transition failed.\n", BackendLabel);
            return false;
        }

        // Runtime borderless toggle via ApplyStyleFlags
        if (!AppWindow.ApplyPresentationMode(WindowPresentationMode::Windowed) ||
            !AppWindow.ApplyStyleFlags(AppFlags & ~WindowStyleFlags::DecoratedFrame) ||
            HasWindowFlag(AppWindow.ActiveStyleFlags(), WindowStyleFlags::DecoratedFrame))
        {
            std::fprintf(stderr, "[FAILED] %s: Runtime ApplyStyleFlags borderless toggle failed.\n", BackendLabel);
            return false;
        }

        AppWindow.Finalize();
    }

    // ③ Game Application Dynamic Mode Transitions:
    //    SplashBorderless -> Windowed (custom placement) -> BorderlessFullscreen -> ExclusiveFullscreen -> Windowed (restored!)
    {
        WindowBackend GameWindow{};
        auto          GameDesc = WindowConfigurationDescriptor::MakeSplashWindow("Frontier Boot", 640, 360);
        if (!GameWindow.Initialize(GameDesc))
        {
            std::fprintf(stderr, "[FAILED] %s: Game boot splash initialization failed.\n", BackendLabel);
            return false;
        }

        // Transition SplashBorderless -> Windowed game window at explicit (220, 140), 1440x810
        if (!GameWindow.ApplyStyleFlags(WindowStyleFlags::DefaultGameFlags) ||
            !GameWindow.ApplyPresentationMode(WindowPresentationMode::Windowed))
        {
            std::fprintf(stderr, "[FAILED] %s: Splash -> Windowed transition failed.\n", BackendLabel);
            return false;
        }
        GameWindow.MoveSurface(220, 140);
        GameWindow.ApplySizeLimits(640, 360, 3840, 2160);

        InputExtension Coordinator{};
        Coordinator.Initialize();
        Coordinator.BeginTick();
        Coordinator.RequestCursorConfinement(CursorConfinementMode::LockedRelativeHidden);
        GameWindow.PollOperatingEvents(Coordinator);

        // Transition Windowed -> BorderlessFullscreen (must strip DecoratedFrame & ResizableBorder, saving 220, 140)
        if (!GameWindow.ApplyPresentationMode(WindowPresentationMode::BorderlessFullscreen))
        {
            std::fprintf(stderr, "[FAILED] %s: Windowed -> BorderlessFullscreen failed.\n", BackendLabel);
            return false;
        }
        if (HasWindowFlag(GameWindow.ActiveStyleFlags(), WindowStyleFlags::DecoratedFrame) ||
            HasWindowFlag(GameWindow.ActiveStyleFlags(), WindowStyleFlags::ResizableBorder) ||
            GameWindow.SavedWindowedBounds().OriginX != 220 ||
            GameWindow.SavedWindowedBounds().OriginY != 140)
        {
            std::fprintf(stderr, "[FAILED] %s: BorderlessFullscreen did not strip frame or preserve bounds.\n", BackendLabel);
            return false;
        }

        // Transition BorderlessFullscreen -> ExclusiveFullscreen (1920x1080 @ 120 Hz)
        if (!GameWindow.ApplyPresentationMode(WindowPresentationMode::ExclusiveFullscreen, 1920, 1080, 120))
        {
            std::fprintf(stderr, "[FAILED] %s: BorderlessFullscreen -> ExclusiveFullscreen failed.\n", BackendLabel);
            return false;
        }
        const auto ExclusiveExtent = GameWindow.InspectExtent();
        if (GameWindow.ActivePresentationMode() != WindowPresentationMode::ExclusiveFullscreen ||
            ExclusiveExtent.PixelWidth != 1920 ||
            ExclusiveExtent.PixelHeight != 1080 ||
            ExclusiveExtent.RefreshRateHz != 120)
        {
            std::fprintf(stderr, "[FAILED] %s: ExclusiveFullscreen dimensions or refresh rate mismatch.\n", BackendLabel);
            return false;
        }

        // Transition ExclusiveFullscreen -> Windowed (must restore saved (220, 140) placement and DecoratedFrame!)
        if (!GameWindow.ApplyPresentationMode(WindowPresentationMode::Windowed))
        {
            std::fprintf(stderr, "[FAILED] %s: ExclusiveFullscreen -> Windowed restore failed.\n", BackendLabel);
            return false;
        }
        const auto RestoredExtent = GameWindow.InspectExtent();
        if (GameWindow.ActivePresentationMode() != WindowPresentationMode::Windowed ||
            !HasWindowFlag(GameWindow.ActiveStyleFlags(), WindowStyleFlags::DecoratedFrame) ||
            !HasWindowFlag(GameWindow.ActiveStyleFlags(), WindowStyleFlags::ResizableBorder) ||
            RestoredExtent.OriginX != 220 ||
            RestoredExtent.OriginY != 140)
        {
            std::fprintf(stderr,
                         "[FAILED] %s: Windowed restore mismatch (Origin=%d,%d).\n",
                         BackendLabel,
                         RestoredExtent.OriginX,
                         RestoredExtent.OriginY);
            return false;
        }

        Coordinator.Finalize();
        GameWindow.Finalize();
    }

    std::printf("[OK] %s verified (SplashBorderless, Windowed, MaximizedWindowed, BorderlessFullscreen, ExclusiveFullscreen).\n",
                BackendLabel);
    return true;
}

} // namespace

int main()
{
    if (!VerifyWindowLifecycleAndModeTransitions<HeadlessPlatformWindow>("HeadlessPlatformWindow"))
    {
        return 1;
    }

#if !defined(__ANDROID__)
    if (!VerifyWindowLifecycleAndModeTransitions<DesktopPlatformWindow>("DesktopPlatformWindow[GLFW-3.4]"))
    {
        return 1;
    }
#endif

    return 0;
}
