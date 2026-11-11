/*============================================================================================================================================
                                                       DOCKINGPANEL.CPP
============================================================================================================================================*/
// 🧩 Application-scoped domain token issuance and patched Dear ImGui trapezoidal dockspace layout builder

#include "Engine/Components/Docking/DockingPanel.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Frontier::Components
{

namespace
{

std::atomic<uint64_t> NextApplicationDomainCounter{ 0x46524E5400000001ULL }; // [id] - Monotonic 'FRNT' prefixed domain generator

[[nodiscard]] ImGuiDir ConvertPlacementToImGuiDir(DockPlacementDirection Direction) noexcept
{
    switch (Direction)
    {
        case DockPlacementDirection::SplitLeft:   return ImGuiDir_Left;
        case DockPlacementDirection::SplitRight:  return ImGuiDir_Right;
        case DockPlacementDirection::SplitTop:    return ImGuiDir_Up;
        case DockPlacementDirection::SplitBottom: return ImGuiDir_Down;
        case DockPlacementDirection::CenterTabGroup:
        default:                                  return ImGuiDir_None;
    }
}

void FormatWindowRootLabel(
    char*                      Buffer,
    std::size_t                BufferSize,
    ApplicationDockDomainToken Domain,
    HostWindowToken            WindowToken) noexcept
{
    std::snprintf(
        Buffer,
        BufferSize,
        "##FrontierDockRoot_%llX_%u",
        static_cast<unsigned long long>(Domain.Identifier),
        WindowToken.SlotIndex);
}

void FormatUniquePanelLabel(
    char*                      Buffer,
    std::size_t                BufferSize,
    std::string_view           Title,
    ApplicationDockDomainToken Domain,
    uint32_t                   PanelIdentifier) noexcept
{
    std::snprintf(
        Buffer,
        BufferSize,
        "%.*s##FRNT_%llX_%u",
        static_cast<int>(Title.size()),
        Title.data(),
        static_cast<unsigned long long>(Domain.Identifier),
        PanelIdentifier);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 DOMAIN TOKEN ISSUER
//------------------------------------------------------------------------------------------------------------------------

ApplicationDockDomainToken IssueApplicationDockDomain() noexcept
{
    return ApplicationDockDomainToken{
        NextApplicationDomainCounter.fetch_add(1ULL, std::memory_order_acq_rel)
    };
}

uint32_t FoldDomainToImGuiClassId(ApplicationDockDomainToken Domain) noexcept
{
    if (!Domain.Valid())
    {
        return 0u;
    }
    const uint32_t Folded = static_cast<uint32_t>(Domain.Identifier ^ (Domain.Identifier >> 32u));
    return (Folded != 0u) ? Folded : 1u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                          IMGUI DOCK REGION PRESENTATION BRIDGE
//------------------------------------------------------------------------------------------------------------------------

void PresentHostWindowImGuiDockRegion(
    ApplicationDockDomainToken                      DomainToken,
    HostWindowToken                                 WindowToken,
    float                                           SurfaceWidthPx,
    float                                           SurfaceHeightPx,
    bool                                            RebuildLayout,
    const std::vector<ImGuiPanelPresentationEntry>& PanelsInWindow,
    uint32_t*                                       OutAddButtonClickedCount)
{
    if (SurfaceWidthPx <= 1.0f || SurfaceHeightPx <= 1.0f)
    {
        return;
    }

    ImGuiWindowClass ApplicationDockClass{};
    ApplicationDockClass.ClassId                  = FoldDomainToImGuiClassId(DomainToken);
    ApplicationDockClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_NoCloseButton;
    ApplicationDockClass.DockingAllowUnclassed    = false;

    char RootWindowName[96]{};
    FormatWindowRootLabel(RootWindowName, sizeof(RootWindowName), DomainToken, WindowToken);

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(SurfaceWidthPx, SurfaceHeightPx));

    constexpr ImGuiWindowFlags RootHostFlags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoDocking;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(RootWindowName, nullptr, RootHostFlags);
    ImGui::PopStyleVar(3);

    const ImGuiID RootDockId = ImGui::GetID(RootWindowName);

    if (RebuildLayout || ImGui::DockBuilderGetNode(RootDockId) == nullptr)
    {
        ImGui::DockBuilderRemoveNode(RootDockId);
        ImGui::DockBuilderAddNode(RootDockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(RootDockId, ImVec2(SurfaceWidthPx, SurfaceHeightPx));

        ImGuiID RemainingCenterId = RootDockId;
        ImGuiID LeftRegionId      = 0;
        ImGuiID RightRegionId     = 0;
        ImGuiID TopRegionId       = 0;
        ImGuiID BottomRegionId    = 0;

        for (const auto& Entry : PanelsInWindow)
        {
            char PanelLabel[160]{};
            FormatUniquePanelLabel(
                PanelLabel,
                sizeof(PanelLabel),
                Entry.TitleText,
                DomainToken,
                Entry.PanelIdentifier);

            const float ClampedRatio = std::clamp(Entry.SplitRatio, 0.10f, 0.85f);
            ImGuiID     TargetId     = RemainingCenterId;

            switch (Entry.Placement)
            {
                case DockPlacementDirection::SplitLeft:
                    if (LeftRegionId == 0)
                    {
                        LeftRegionId = ImGui::DockBuilderSplitNode(
                            RemainingCenterId,
                            ImGuiDir_Left,
                            ClampedRatio,
                            nullptr,
                            &RemainingCenterId);
                    }
                    TargetId = LeftRegionId;
                    break;

                case DockPlacementDirection::SplitRight:
                    if (RightRegionId == 0)
                    {
                        RightRegionId = ImGui::DockBuilderSplitNode(
                            RemainingCenterId,
                            ImGuiDir_Right,
                            ClampedRatio,
                            nullptr,
                            &RemainingCenterId);
                    }
                    TargetId = RightRegionId;
                    break;

                case DockPlacementDirection::SplitTop:
                    if (TopRegionId == 0)
                    {
                        TopRegionId = ImGui::DockBuilderSplitNode(
                            RemainingCenterId,
                            ImGuiDir_Up,
                            ClampedRatio,
                            nullptr,
                            &RemainingCenterId);
                    }
                    TargetId = TopRegionId;
                    break;

                case DockPlacementDirection::SplitBottom:
                    if (BottomRegionId == 0)
                    {
                        BottomRegionId = ImGui::DockBuilderSplitNode(
                            RemainingCenterId,
                            ImGuiDir_Down,
                            ClampedRatio,
                            nullptr,
                            &RemainingCenterId);
                    }
                    TargetId = BottomRegionId;
                    break;

                case DockPlacementDirection::CenterTabGroup:
                default:
                    TargetId = RemainingCenterId;
                    break;
            }

            (void)ConvertPlacementToImGuiDir(Entry.Placement);
            ImGui::DockBuilderDockWindow(PanelLabel, TargetId);
        }

        ImGui::DockBuilderFinish(RootDockId);
    }

    ImGui::DockSpace(RootDockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None, &ApplicationDockClass);
    ImGui::End();

    uint32_t ClickedAddTotal = 0u;

    for (const auto& Entry : PanelsInWindow)
    {
        char PanelLabel[160]{};
        FormatUniquePanelLabel(
            PanelLabel,
            sizeof(PanelLabel),
            Entry.TitleText,
            DomainToken,
            Entry.PanelIdentifier);

        ImGui::SetNextWindowClass(&ApplicationDockClass);

        bool* ClosePointer = QueryPanelDirectiveEnabled(Entry.Directives, PanelDirectiveMask::EnableCloseButton)
                                 ? Entry.VisibilityToggle
                                 : nullptr;

        if (ImGui::Begin(PanelLabel, ClosePointer, ImGuiWindowFlags_NoCollapse))
        {
            const ImGuiID ActiveDockRegionId = ImGui::GetWindowDockID();
            if (ActiveDockRegionId != 0 &&
                QueryPanelDirectiveEnabled(Entry.Directives, PanelDirectiveMask::EnableTrailingAddButton))
            {
                ImGui::DockNodeSetAddButton(ActiveDockRegionId, true);
                if (ImGui::DockNodeConsumeAddRequest(ActiveDockRegionId))
                {
                    ++ClickedAddTotal;
                }
            }

            if (Entry.ContentPresenter)
            {
                Entry.ContentPresenter();
            }
        }
        ImGui::End();
    }

    if (OutAddButtonClickedCount != nullptr)
    {
        *OutAddButtonClickedCount = ClickedAddTotal;
    }
}

} // namespace Frontier::Components
