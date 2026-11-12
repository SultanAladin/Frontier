/*============================================================================================================================================
                                                           UVVIEWPORTHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of UVViewportHost delegating 2D UDIM + 3D Projection GPU views and live interaction to ViewportProjectionPane

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/UVViewportHost.h"

namespace Frontier::Workspaces
{

UVViewportHost::UVViewportHost()
{
    ProjectionPane.ConfigurePaneBadges(
        "2D UDIM UVTILE CANVAS + GIZMO",
        "3D PROJECTION + MATCAP + GIZMO");
}

UVViewportHost::~UVViewportHost() = default;

void UVViewportHost::AttachSession(UVWorkspaceSession* TargetSession) noexcept
{
    Session = TargetSession;
    Interactor.AttachSession(Session);

    if (Session == nullptr)
    {
        ProjectionPane.AttachDocument(nullptr);
        ProjectionPane.ConfigureToolbarChips({});
        ProjectionPane.AssignInteractionCallback({});
        ProjectionPane.AssignFramebufferProvider({});
        ProjectionPane.AssignSplitModeCallback({});
        return;
    }

    ProjectionPane.AttachDocument(&Session->AccessDocument());

    ProjectionPane.ConfigureToolbarChips({
        ViewportToolbarChip{
            "Frame",
            false,
            [this]()
            {
                if (Session != nullptr)
                {
                    Session->FrameSelectionOrNetwork();
                }
            },
        },
        ViewportToolbarChip{
            "Seam [K]",
            false,
            [this]()
            {
                if (Session != nullptr)
                {
                    Session->SeamBrushActive  = !Session->SeamBrushActive;
                    Session->FramebuffersDirty = true;
                }
            },
        },
        ViewportToolbarChip{
            "Unwrap",
            false,
            [this]()
            {
                if (Session != nullptr)
                {
                    Session->QueueDirective(UVCommandDirective::UnwrapConformal);
                }
            },
        },
        ViewportToolbarChip{
            "Relax GPU",
            false,
            [this]()
            {
                if (Session != nullptr)
                {
                    Session->QueueDirective(UVCommandDirective::RelaxStretchGPU);
                }
            },
        },
        ViewportToolbarChip{
            "Pack UVs",
            true,
            [this]()
            {
                if (Session != nullptr)
                {
                    Session->QueueDirective(UVCommandDirective::PackUVsConcaveSkyline);
                }
            },
        },
    });

    ProjectionPane.AssignInteractionCallback(
        [this](const ViewportInteractionFrame& Frame)
        {
            Interactor.ProcessViewportInput(Frame);
        });

    ProjectionPane.AssignSplitModeCallback(
        [this](Editor::ViewportSplitMode NewMode)
        {
            if (Session == nullptr)
            {
                return;
            }
            Session->WorkspaceConfig.AccessUVPreferences().InitialViewportLayout =
                static_cast<Configuration::UVViewportLayoutMode>(static_cast<uint8_t>(NewMode));
            Session->FramebuffersDirty = true;
        });

    ProjectionPane.AssignFramebufferProvider(
        [this](uint32_t SpatialW, uint32_t SpatialH, uint32_t PlanarW, uint32_t PlanarH) -> DualViewportFramebuffers
        {
            DualViewportFramebuffers Result {};
            if (Session == nullptr)
            {
                return Result;
            }
            Session->RefreshViewportFramebuffers(SpatialW, SpatialH, PlanarW, PlanarH);
            if (!Session->Framebuffer3DRGBA.empty())
            {
                Result.Spatial3D = {
                    Session->Framebuffer3DRGBA.data(),
                    Session->Framebuffer3DWidth,
                    Session->Framebuffer3DHeight,
                };
            }
            if (!Session->Framebuffer2DRGBA.empty())
            {
                Result.Planar2D = {
                    Session->Framebuffer2DRGBA.data(),
                    Session->Framebuffer2DWidth,
                    Session->Framebuffer2DHeight,
                };
            }
            return Result;
        });
}

void UVViewportHost::SynchronizeProjectionPolicy() noexcept
{
    if (Session == nullptr)
    {
        return;
    }
    const auto& Prefs = Session->WorkspaceConfig.InspectUVPreferences();
    auto&       Proj  = Session->AccessViewportProjection();
    Proj.ConfigureSplitMode(static_cast<Editor::ViewportSplitMode>(static_cast<uint8_t>(Prefs.InitialViewportLayout)));
    Proj.ConfigureSplitRatio(Prefs.SplitRatioNormalized);
    ProjectionPane.ConfigureStatusBanner(Session->StatusBannerText);
}

void UVViewportHost::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  Δt)
{
    SynchronizeProjectionPolicy();
    ProjectionPane.Render(Surface, Bounds, Input, Δt);
}

void UVViewportHost::PresentInImGuiPane(double Δt, float ScaleFactor)
{
    SynchronizeProjectionPolicy();
    ProjectionPane.PresentInImGuiPane(Δt, ScaleFactor);
}

} // namespace Frontier::Workspaces
