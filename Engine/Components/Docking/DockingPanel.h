/*============================================================================================================================================
                                                        DOCKINGPANEL.H
============================================================================================================================================*/
// 🧩 Shared trapezoidal docking panel coordinator enforcing intra-application multi-window docking and OS window tear-off

#pragma once

#include "Engine/Components/Docking/TrapezoidalTabProfile.h"
#include "Engine/Windowing/PlatformWindow.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::Components
{

//------------------------------------------------------------------------------------------------------------------------
//                                             STRONGLY-TYPED IDENTIFIERS
//------------------------------------------------------------------------------------------------------------------------

struct ApplicationDockDomainToken
{
    uint64_t Identifier = 0; // [id] - Monotonic non-zero domain identity unique to one Frontier application instance

    [[nodiscard]] constexpr bool Valid() const noexcept { return Identifier != 0; }
    [[nodiscard]] constexpr bool operator==(const ApplicationDockDomainToken&) const noexcept = default;
};

struct HostWindowToken
{
    uint32_t SlotIndex = 0; // [idx] - Index into application host window register (0 = primary window)

    [[nodiscard]] constexpr bool operator==(const HostWindowToken&) const noexcept = default;
};

struct DockingPanelToken
{
    uint32_t Identifier = 0; // [id] - Non-zero monotonic panel identity within a dock coordinator

    [[nodiscard]] constexpr bool Valid() const noexcept { return Identifier != 0; }
    [[nodiscard]] constexpr bool operator==(const DockingPanelToken&) const noexcept = default;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class DockPlacementDirection : uint8_t
{
    CenterTabGroup = 0, // [-] - Insert as trapezoidal tab inside target dock strip
    SplitLeft      = 1, // [-] - Partition left side of target host window dock region
    SplitRight     = 2, // [-] - Partition right side of target host window dock region
    SplitTop       = 3, // [-] - Partition upper side of target host window dock region
    SplitBottom    = 4, // [-] - Partition lower side of target host window dock region
};

enum class PanelDirectiveMask : uint32_t
{
    EmptyDirectives          = 0u,      // [mask] - No optional panel directives active
    EnableCloseButton        = 1u << 0, // [mask] - Enable circular close button on trapezoidal tab
    EnableTrailingAddButton  = 1u << 1, // [mask] - Enable adjacent '+' button at end of dock strip (PatchD/PatchG)
    EnableOperatingTearOff   = 1u << 2, // [mask] - Enable undocking panel into a standalone OS PlatformWindow
    EnableCrossWindowDocking = 1u << 3, // [mask] - Enable docking onto any host window within this application
    AssignCentralViewport    = 1u << 4, // [mask] - Assign panel as central scene/canvas viewport region
    EnableInitialVisibility  = 1u << 5, // [mask] - Enable immediate panel visibility upon registration

    StandardPanelDirectives  = EnableCloseButton | EnableTrailingAddButton |
                               EnableOperatingTearOff | EnableCrossWindowDocking | EnableInitialVisibility,
    CentralViewportDirectives = EnableCloseButton | EnableTrailingAddButton |
                                EnableCrossWindowDocking | AssignCentralViewport | EnableInitialVisibility,
};

[[nodiscard]] constexpr PanelDirectiveMask operator|(PanelDirectiveMask Left, PanelDirectiveMask Right) noexcept
{
    return static_cast<PanelDirectiveMask>(static_cast<uint32_t>(Left) | static_cast<uint32_t>(Right));
}

[[nodiscard]] constexpr PanelDirectiveMask operator&(PanelDirectiveMask Left, PanelDirectiveMask Right) noexcept
{
    return static_cast<PanelDirectiveMask>(static_cast<uint32_t>(Left) & static_cast<uint32_t>(Right));
}

[[nodiscard]] constexpr PanelDirectiveMask operator~(PanelDirectiveMask Mask) noexcept
{
    return static_cast<PanelDirectiveMask>(~static_cast<uint32_t>(Mask));
}

[[nodiscard]] constexpr bool QueryPanelDirectiveEnabled(PanelDirectiveMask Mask, PanelDirectiveMask Directive) noexcept
{
    return (static_cast<uint32_t>(Mask) & static_cast<uint32_t>(Directive)) == static_cast<uint32_t>(Directive);
}

enum class DockTransferVerdict : uint8_t
{
    Permitted                  = 0, // [-] - Same-application window transfer valid and permitted
    RejectedNullDomain         = 1, // [-] - Source or target window lacks an application domain token
    RejectedForeignApplication = 2, // [-] - Target belongs to an external process or foreign application instance
    RejectedUnknownPanel       = 3, // [-] - Panel token is unregistered in this coordinator
    RejectedUnknownWindow      = 4, // [-] - Target host window slot is inactive or out of range
    RejectedTearOffDisabled    = 5, // [-] - Panel lacks EnableOperatingTearOff directive
    RejectedCrossWindowLocked  = 6, // [-] - Panel lacks EnableCrossWindowDocking directive
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

using PanelContentPresenter = std::function<void()>;

struct DockingPanelDescriptor
{
    std::string_view      PanelTitle       = "Panel";                                      // [-]    - Visible trapezoidal tab label
    DockPlacementDirection InitialDirection = DockPlacementDirection::CenterTabGroup;      // [-]    - Initial dock placement inside primary window
    float                 InitialSplitRatio = 0.22f;                                       // [0..1] - Fractional extent when splitting dock region
    PanelDirectiveMask    Directives       = PanelDirectiveMask::StandardPanelDirectives;  // [mask] - Panel capabilities & chrome directives
    PanelContentPresenter ContentPresenter = {};                                           // [-]    - Optional ImGui interior presentation callback
};

struct DockingPanelReading
{
    DockingPanelToken      Token            {};                                            // [id]   - Panel identifier
    HostWindowToken        AssignedWindow   {};                                            // [idx]  - Host OS window currently holding this panel
    DockPlacementDirection Placement        = DockPlacementDirection::CenterTabGroup;      // [-]    - Active dock slot placement in host window
    float                  SplitRatio       = 0.22f;                                       // [0..1] - Fractional split ratio within host window
    PanelDirectiveMask     Directives       = PanelDirectiveMask::StandardPanelDirectives; // [mask] - Active panel directives
    std::string_view       TitleText        {};                                            // [-]    - Visible trapezoidal tab caption
    bool                   VisibilityActive = true;                                        // [-]    - True while panel tab is open
    bool                   DetachedWindow   = false;                                       // [-]    - True when hosted inside a torn-off OS window
};

struct HostWindowReading
{
    HostWindowToken               WindowToken      {};     // [idx]   - Host window slot index
    ApplicationDockDomainToken    DomainToken      {};     // [id]    - Owning application domain token
    Windowing::WindowExtentReading SurfaceExtent   {};     // [px]    - Current OS window position and drawable dimensions
    std::string_view              TitleText        {};     // [-]     - Current OS window title
    uint32_t                      DockedPanelCount = 0;    // [count] - Open panels currently docked inside this OS window
    bool                          PrimaryWindow    = true; // [-]     - True for primary workspace window (slot 0)
    bool                          Active           = false;// [-]     - True while underlying OS PlatformWindow is alive
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 DOMAIN TOKEN ISSUER
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] ApplicationDockDomainToken IssueApplicationDockDomain() noexcept;
[[nodiscard]] uint32_t                   FoldDomainToImGuiClassId(ApplicationDockDomainToken Domain) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                          IMGUI DOCK REGION PRESENTATION BRIDGE
//------------------------------------------------------------------------------------------------------------------------

struct ImGuiPanelPresentationEntry
{
    uint32_t               PanelIdentifier  = 0;                                      // [id]   - Unique panel identifier
    std::string_view       TitleText        {};                                       // [-]    - Visible trapezoidal tab label
    DockPlacementDirection Placement        = DockPlacementDirection::CenterTabGroup; // [-]    - Dock placement inside window
    float                  SplitRatio       = 0.22f;                                  // [0..1] - Split fraction
    PanelDirectiveMask     Directives       = PanelDirectiveMask::StandardPanelDirectives;
    PanelContentPresenter  ContentPresenter {};                                       // [-]    - Interior content presentation callback
    bool*                  VisibilityToggle = nullptr;                                // [-]    - Mutable open toggle bound to tab 'x' button
};

void PresentHostWindowImGuiDockRegion(
    ApplicationDockDomainToken                      DomainToken,
    HostWindowToken                                 WindowToken,
    float                                           SurfaceWidthPx,
    float                                           SurfaceHeightPx,
    bool                                            RebuildLayout,
    const std::vector<ImGuiPanelPresentationEntry>& PanelsInWindow,
    uint32_t*                                       OutAddButtonClickedCount = nullptr);

//------------------------------------------------------------------------------------------------------------------------
//                                        TRAPEZOIDAL DOCKING COORDINATOR TEMPLATE
//------------------------------------------------------------------------------------------------------------------------

template <Windowing::PlatformWindowSurface WindowSurfaceType = Windowing::HeadlessPlatformWindow>
class TrapezoidalDockCoordinator
{
public:
    TrapezoidalDockCoordinator() noexcept = default;
    ~TrapezoidalDockCoordinator() noexcept { Finalize(); }

    TrapezoidalDockCoordinator(const TrapezoidalDockCoordinator&)            = delete;
    TrapezoidalDockCoordinator& operator=(const TrapezoidalDockCoordinator&) = delete;

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  LIFECYCLE
    //--------------------------------------------------------------------------------------------------------------------

    [[nodiscard]] bool Initialize(
        const Windowing::WindowConfigurationDescriptor& PrimaryWindowDescriptor,
        ApplicationDockDomainToken                      ExplicitDomain = {}) noexcept
    {
        Finalize();

        OwningDomain = ExplicitDomain.Valid() ? ExplicitDomain : IssueApplicationDockDomain();
        TabMetrics   = ConstructProjectZeroTabMetrics(PrimaryWindowDescriptor.DpiScaleFactor);

        auto PrimarySlot         = std::make_unique<HostWindowEntry>();
        PrimarySlot->Token       = HostWindowToken{ 0u };
        PrimarySlot->Domain      = OwningDomain;
        PrimarySlot->Primary     = true;
        PrimarySlot->LayoutDirty = true;

        if (!PrimarySlot->Surface.Initialize(PrimaryWindowDescriptor))
        {
            OwningDomain = {};
            return false;
        }

        PrimarySlot->Active = true;
        HostWindows.push_back(std::move(PrimarySlot));
        CoordinatorActive = true;
        return true;
    }

    void PollOperatingWindows(Input::InputExtension& InputCoordinator) noexcept
    {
        if (!CoordinatorActive)
        {
            return;
        }

        for (std::size_t SlotIdx = 0; SlotIdx < HostWindows.size(); ++SlotIdx)
        {
            auto& Slot = HostWindows[SlotIdx];
            if (!Slot || !Slot->Active)
            {
                continue;
            }
            Slot->Surface.PollOperatingEvents(InputCoordinator);
            if (!Slot->Primary && Slot->Surface.CloseRequested())
            {
                CloseDetachedOperatingWindow(Slot->Token);
            }
        }
    }

    void Finalize() noexcept
    {
        for (auto& Slot : HostWindows)
        {
            if (Slot && Slot->Active)
            {
                Slot->Surface.Finalize();
                Slot->Active = false;
            }
        }
        HostWindows.clear();
        Panels.clear();
        NextPanelId           = 1u;
        RejectedTransfers     = 0u;
        CompletedTearOffCount = 0u;
        CompletedDockMoves    = 0u;
        OwningDomain          = {};
        CoordinatorActive     = false;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                       PANEL REGISTRATION & DOCK OPERATIONS
    //--------------------------------------------------------------------------------------------------------------------

    [[nodiscard]] DockingPanelToken RegisterPanel(const DockingPanelDescriptor& Descriptor) noexcept
    {
        if (!CoordinatorActive || HostWindows.empty())
        {
            return {};
        }

        PanelEntry Entry{};
        Entry.Token            = DockingPanelToken{ NextPanelId++ };
        Entry.AssignedWindow   = HostWindowToken{ 0u };
        Entry.Title.assign(Descriptor.PanelTitle.data(), Descriptor.PanelTitle.size());
        Entry.Placement        = Descriptor.InitialDirection;
        Entry.SplitRatio       = Descriptor.InitialSplitRatio;
        Entry.Directives       = Descriptor.Directives;
        Entry.ContentPresenter = Descriptor.ContentPresenter;
        Entry.VisibilityActive = QueryPanelDirectiveEnabled(
            Descriptor.Directives,
            PanelDirectiveMask::EnableInitialVisibility);

        Panels.push_back(std::move(Entry));
        HostWindows[0]->LayoutDirty = true;
        return Panels.back().Token;
    }

    // Invariant (3): Verify whether a panel may dock onto a candidate domain + host window
    [[nodiscard]] DockTransferVerdict VerifyDockTransfer(
        DockingPanelToken          PanelToken,
        ApplicationDockDomainToken CandidateTargetDomain,
        HostWindowToken            CandidateTargetWindow) const noexcept
    {
        if (!OwningDomain.Valid() || !CandidateTargetDomain.Valid())
        {
            return DockTransferVerdict::RejectedNullDomain;
        }
        if (CandidateTargetDomain != OwningDomain)
        {
            return DockTransferVerdict::RejectedForeignApplication;
        }

        const PanelEntry* Panel = LocatePanel(PanelToken);
        if (Panel == nullptr)
        {
            return DockTransferVerdict::RejectedUnknownPanel;
        }

        if (CandidateTargetWindow.SlotIndex >= HostWindows.size() ||
            !HostWindows[CandidateTargetWindow.SlotIndex] ||
            !HostWindows[CandidateTargetWindow.SlotIndex]->Active)
        {
            return DockTransferVerdict::RejectedUnknownWindow;
        }

        if (Panel->AssignedWindow != CandidateTargetWindow &&
            !QueryPanelDirectiveEnabled(Panel->Directives, PanelDirectiveMask::EnableCrossWindowDocking))
        {
            return DockTransferVerdict::RejectedCrossWindowLocked;
        }

        return DockTransferVerdict::Permitted;
    }

    // Invariant (1) + (3): Dock a panel to ANY active window belonging to the specified domain (must match OwningDomain)
    [[nodiscard]] DockTransferVerdict DockPanelToApplicationWindow(
        DockingPanelToken          PanelToken,
        ApplicationDockDomainToken TargetDomain,
        HostWindowToken            TargetWindow,
        DockPlacementDirection     Direction  = DockPlacementDirection::CenterTabGroup,
        float                      SplitRatio = 0.25f) noexcept
    {
        const DockTransferVerdict Verdict = VerifyDockTransfer(PanelToken, TargetDomain, TargetWindow);
        if (Verdict != DockTransferVerdict::Permitted)
        {
            ++RejectedTransfers;
            return Verdict;
        }

        PanelEntry* Panel               = LocatePanelMutable(PanelToken);
        const HostWindowToken OldWindow = Panel->AssignedWindow;

        Panel->AssignedWindow   = TargetWindow;
        Panel->Placement        = Direction;
        Panel->SplitRatio       = SplitRatio;
        Panel->VisibilityActive = true;

        HostWindows[TargetWindow.SlotIndex]->LayoutDirty = true;
        if (OldWindow.SlotIndex < HostWindows.size() && HostWindows[OldWindow.SlotIndex])
        {
            HostWindows[OldWindow.SlotIndex]->LayoutDirty = true;
            ReclaimDetachedWindowIfEmpty(OldWindow);
        }

        ++CompletedDockMoves;
        return DockTransferVerdict::Permitted;
    }

    [[nodiscard]] DockTransferVerdict DockPanelToWindow(
        DockingPanelToken      PanelToken,
        HostWindowToken        TargetWindow,
        DockPlacementDirection Direction  = DockPlacementDirection::CenterTabGroup,
        float                  SplitRatio = 0.25f) noexcept
    {
        return DockPanelToApplicationWindow(PanelToken, OwningDomain, TargetWindow, Direction, SplitRatio);
    }

    // Invariant (2): Undocking tears the panel off into a dedicated native OS PlatformWindow owned by this application
    [[nodiscard]] HostWindowToken DetachPanelToOperatingWindow(
        DockingPanelToken PanelToken,
        int32_t           ScreenOriginX = 260,
        int32_t           ScreenOriginY = 140,
        uint32_t          PixelWidth    = 460,
        uint32_t          PixelHeight   = 620) noexcept
    {
        PanelEntry* Panel = LocatePanelMutable(PanelToken);
        if (Panel == nullptr ||
            !QueryPanelDirectiveEnabled(Panel->Directives, PanelDirectiveMask::EnableOperatingTearOff))
        {
            ++RejectedTransfers;
            return HostWindowToken{ 0u };
        }

        const uint32_t TargetSlotIndex = FindOrAllocateWindowSlot();
        auto&          DetachedSlot    = HostWindows[TargetSlotIndex];
        DetachedSlot                   = std::make_unique<HostWindowEntry>();
        DetachedSlot->Token            = HostWindowToken{ TargetSlotIndex };
        DetachedSlot->Domain           = OwningDomain;
        DetachedSlot->Primary          = false;
        DetachedSlot->LayoutDirty      = true;

        const auto WindowDescriptor = Windowing::WindowConfigurationDescriptor::ConstructDetachedPanelWindow(
            Panel->Title,
            ScreenOriginX,
            ScreenOriginY,
            PixelWidth,
            PixelHeight);

        if (!DetachedSlot->Surface.Initialize(WindowDescriptor))
        {
            DetachedSlot.reset();
            return HostWindowToken{ 0u };
        }
        DetachedSlot->Active = true;

        const HostWindowToken PreviousWindow = Panel->AssignedWindow;
        Panel->AssignedWindow                = DetachedSlot->Token;
        Panel->Placement                     = DockPlacementDirection::CenterTabGroup;
        Panel->SplitRatio                    = 1.0f;
        Panel->VisibilityActive              = true;

        if (PreviousWindow.SlotIndex < HostWindows.size() && HostWindows[PreviousWindow.SlotIndex])
        {
            HostWindows[PreviousWindow.SlotIndex]->LayoutDirty = true;
            ReclaimDetachedWindowIfEmpty(PreviousWindow);
        }

        ++CompletedTearOffCount;
        return DetachedSlot->Token;
    }

    void TogglePanelVisibility(DockingPanelToken PanelToken, bool EnableVisible) noexcept
    {
        if (PanelEntry* Panel = LocatePanelMutable(PanelToken))
        {
            Panel->VisibilityActive = EnableVisible;
            if (Panel->AssignedWindow.SlotIndex < HostWindows.size() &&
                HostWindows[Panel->AssignedWindow.SlotIndex])
            {
                HostWindows[Panel->AssignedWindow.SlotIndex]->LayoutDirty = true;
                if (!EnableVisible)
                {
                    ReclaimDetachedWindowIfEmpty(Panel->AssignedWindow);
                }
            }
        }
    }

    // Present the ImGui dock region and all panels belonging to a specific host OS window
    void PresentWindowImGuiPanels(HostWindowToken WindowToken, uint32_t* OutAddButtonClicks = nullptr)
    {
        if (WindowToken.SlotIndex >= HostWindows.size() ||
            !HostWindows[WindowToken.SlotIndex] ||
            !HostWindows[WindowToken.SlotIndex]->Active)
        {
            return;
        }

        auto&                                    Slot   = *HostWindows[WindowToken.SlotIndex];
        const auto                               Extent = Slot.Surface.InspectExtent();
        std::vector<ImGuiPanelPresentationEntry> WindowPanels;
        WindowPanels.reserve(Panels.size());

        for (auto& Panel : Panels)
        {
            if (Panel.AssignedWindow == WindowToken && Panel.VisibilityActive)
            {
                ImGuiPanelPresentationEntry PresentationEntry{};
                PresentationEntry.PanelIdentifier  = Panel.Token.Identifier;
                PresentationEntry.TitleText        = Panel.Title;
                PresentationEntry.Placement        = Panel.Placement;
                PresentationEntry.SplitRatio       = Panel.SplitRatio;
                PresentationEntry.Directives       = Panel.Directives;
                PresentationEntry.ContentPresenter = Panel.ContentPresenter;
                PresentationEntry.VisibilityToggle = &Panel.VisibilityActive;
                WindowPanels.push_back(std::move(PresentationEntry));
            }
        }

        PresentHostWindowImGuiDockRegion(
            OwningDomain,
            WindowToken,
            static_cast<float>(Extent.PixelWidth),
            static_cast<float>(Extent.PixelHeight),
            Slot.LayoutDirty,
            WindowPanels,
            OutAddButtonClicks);

        Slot.LayoutDirty = false;
        ReclaimDetachedWindowIfEmpty(WindowToken);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                   INSPECTION
    //--------------------------------------------------------------------------------------------------------------------

    [[nodiscard]] ApplicationDockDomainToken DomainToken()            const noexcept { return OwningDomain; }
    [[nodiscard]] TrapezoidalTabMetrics      ActiveTabMetrics()       const noexcept { return TabMetrics; }
    [[nodiscard]] uint32_t                   RejectedTransferCount()  const noexcept { return RejectedTransfers; }
    [[nodiscard]] uint32_t                   TearOffOperatingCount()  const noexcept { return CompletedTearOffCount; }
    [[nodiscard]] uint32_t                   CompletedDockMoveCount() const noexcept { return CompletedDockMoves; }

    [[nodiscard]] uint32_t ActiveOperatingWindowCount() const noexcept
    {
        uint32_t Count = 0;
        for (const auto& Slot : HostWindows)
        {
            if (Slot && Slot->Active)
            {
                ++Count;
            }
        }
        return Count;
    }

    [[nodiscard]] std::size_t RegisteredPanelCount() const noexcept
    {
        return Panels.size();
    }

    [[nodiscard]] DockingPanelReading InspectPanel(DockingPanelToken PanelToken) const noexcept
    {
        DockingPanelReading Reading{};
        const PanelEntry*   Panel = LocatePanel(PanelToken);
        if (Panel == nullptr)
        {
            return Reading;
        }

        Reading.Token            = Panel->Token;
        Reading.AssignedWindow   = Panel->AssignedWindow;
        Reading.Placement        = Panel->Placement;
        Reading.SplitRatio       = Panel->SplitRatio;
        Reading.Directives       = Panel->Directives;
        Reading.TitleText        = Panel->Title;
        Reading.VisibilityActive = Panel->VisibilityActive;
        Reading.DetachedWindow   = (Panel->AssignedWindow.SlotIndex != 0u);
        return Reading;
    }

    [[nodiscard]] HostWindowReading InspectHostWindow(HostWindowToken WindowToken) const noexcept
    {
        HostWindowReading Reading{};
        if (WindowToken.SlotIndex >= HostWindows.size() ||
            !HostWindows[WindowToken.SlotIndex] ||
            !HostWindows[WindowToken.SlotIndex]->Active)
        {
            return Reading;
        }

        const auto& Slot         = *HostWindows[WindowToken.SlotIndex];
        Reading.WindowToken      = Slot.Token;
        Reading.DomainToken      = Slot.Domain;
        Reading.SurfaceExtent    = Slot.Surface.InspectExtent();
        Reading.TitleText        = Slot.Surface.TitleText();
        Reading.DockedPanelCount = CountVisiblePanelsInWindow(WindowToken);
        Reading.PrimaryWindow    = Slot.Primary;
        Reading.Active           = Slot.Active;
        return Reading;
    }

    [[nodiscard]] WindowSurfaceType* AccessOperatingSurface(HostWindowToken WindowToken) noexcept
    {
        if (WindowToken.SlotIndex >= HostWindows.size() ||
            !HostWindows[WindowToken.SlotIndex] ||
            !HostWindows[WindowToken.SlotIndex]->Active)
        {
            return nullptr;
        }
        return &HostWindows[WindowToken.SlotIndex]->Surface;
    }

private:
    struct HostWindowEntry
    {
        HostWindowToken            Token       {};
        ApplicationDockDomainToken Domain      {};
        WindowSurfaceType          Surface     {};
        bool                       Primary     = false;
        bool                       Active      = false;
        bool                       LayoutDirty = true;
    };

    struct PanelEntry
    {
        DockingPanelToken      Token            {};
        HostWindowToken        AssignedWindow   {};
        std::string            Title            {};
        DockPlacementDirection Placement        = DockPlacementDirection::CenterTabGroup;
        float                  SplitRatio       = 0.22f;
        PanelDirectiveMask     Directives       = PanelDirectiveMask::StandardPanelDirectives;
        PanelContentPresenter  ContentPresenter {};
        bool                   VisibilityActive = true;
    };

    [[nodiscard]] const PanelEntry* LocatePanel(DockingPanelToken Token) const noexcept
    {
        for (const auto& Entry : Panels)
        {
            if (Entry.Token == Token)
            {
                return &Entry;
            }
        }
        return nullptr;
    }

    [[nodiscard]] PanelEntry* LocatePanelMutable(DockingPanelToken Token) noexcept
    {
        for (auto& Entry : Panels)
        {
            if (Entry.Token == Token)
            {
                return &Entry;
            }
        }
        return nullptr;
    }

    [[nodiscard]] uint32_t CountVisiblePanelsInWindow(HostWindowToken WindowToken) const noexcept
    {
        uint32_t Count = 0;
        for (const auto& Entry : Panels)
        {
            if (Entry.AssignedWindow == WindowToken && Entry.VisibilityActive)
            {
                ++Count;
            }
        }
        return Count;
    }

    [[nodiscard]] uint32_t FindOrAllocateWindowSlot() noexcept
    {
        for (uint32_t Index = 1u; Index < static_cast<uint32_t>(HostWindows.size()); ++Index)
        {
            if (!HostWindows[Index] || !HostWindows[Index]->Active)
            {
                return Index;
            }
        }
        HostWindows.push_back(nullptr);
        return static_cast<uint32_t>(HostWindows.size() - 1u);
    }

    void ReclaimDetachedWindowIfEmpty(HostWindowToken WindowToken) noexcept
    {
        if (WindowToken.SlotIndex == 0u || WindowToken.SlotIndex >= HostWindows.size())
        {
            return;
        }
        auto& Slot = HostWindows[WindowToken.SlotIndex];
        if (!Slot || !Slot->Active || Slot->Primary)
        {
            return;
        }
        if (CountVisiblePanelsInWindow(WindowToken) == 0u)
        {
            Slot->Surface.Finalize();
            Slot->Active = false;
            Slot.reset();
        }
    }

    void CloseDetachedOperatingWindow(HostWindowToken WindowToken) noexcept
    {
        if (WindowToken.SlotIndex == 0u || WindowToken.SlotIndex >= HostWindows.size())
        {
            return;
        }
        for (auto& Panel : Panels)
        {
            if (Panel.AssignedWindow == WindowToken)
            {
                Panel.VisibilityActive = false;
                Panel.AssignedWindow   = HostWindowToken{ 0u };
            }
        }
        ReclaimDetachedWindowIfEmpty(WindowToken);
    }

    std::vector<std::unique_ptr<HostWindowEntry>> HostWindows{};
    std::vector<PanelEntry>                       Panels{};
    ApplicationDockDomainToken                    OwningDomain          {};
    TrapezoidalTabMetrics                         TabMetrics            {};
    uint32_t                                      NextPanelId           = 1u;
    uint32_t                                      RejectedTransfers     = 0u;
    uint32_t                                      CompletedTearOffCount = 0u;
    uint32_t                                      CompletedDockMoves    = 0u;
    bool                                          CoordinatorActive     = false;
};

using HeadlessTrapezoidalDockCoordinator = TrapezoidalDockCoordinator<Windowing::HeadlessPlatformWindow>;
#if !defined(__ANDROID__)
using DesktopTrapezoidalDockCoordinator  = TrapezoidalDockCoordinator<Windowing::DesktopPlatformWindow>;
#endif

} // namespace Frontier::Components
