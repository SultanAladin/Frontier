/*==============================================================================================================================================
                                                              UVWORKSPACE.CPP
==============================================================================================================================================*/
// 🧩 Wires the shared panels into the UV workspace. Identical composition to Modeling but the viewport is PLANAR and the outliner lists UV
//    islands. Proves the shared components are truly reused — no viewport / outliner / property code is duplicated here.

#include "UVWorkspace.h"

#include "UVPropertyPanel.h"

#include "../../WorkspaceHost/Outliner/OutlinerPanel.h"

#include "../../WorkspaceHost/Outliner/OutlinerConfiguration.h"

#include "../../Components/Cards/ActionToolbar.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    void SeedPlaceholderIslands(OutlinerModel& Model)
    {
        ResetOutlinerModel(Model);
        for (int Island = 0; Island < 3; ++Island)
        {
            OutlinerRow Row = {};
            Row.Record.Index = static_cast<unsigned>(Island + 1); Row.Record.Generation = 1;
            Row.Title = (Island == 0) ? "Island.Head" : (Island == 1) ? "Island.Body" : "Island.Base";
            Row.Depth = 0; Row.ChildCount = 0; Row.Expanded = false;
            Row.TintColor = IM_COL32(110, 160, 120, 255); Row.Visible = true; Row.Selected = (Island == 0);
            AppendOutlinerRow(Model, Row);
        }
    }

    void ConstructViewport(const ThemeConfiguration& Theme, void* Context)
    {
        UVWorkspaceState& State = *static_cast<UVWorkspaceState*>(Context);
        ConstructViewportPanel(Theme, State.Viewport);
    }

    void ConstructOutliner(const ThemeConfiguration& Theme, void* Context)
    {
        UVWorkspaceState& State = *static_cast<UVWorkspaceState*>(Context);
        const OutlinerConfiguration Configuration = ResolveDefaultOutlinerConfiguration(Theme, nullptr);
        const OutlinerPanelResult Result = ConstructOutlinerPanel(Theme, Configuration, State.Islands);
        if (Result.Interaction == OutlinerInteraction::Selected)
        {
            for (int Index = 0; Index < State.Islands.RowCount; ++Index)
            {
                OutlinerRow& Row = State.Islands.Rows[Index];
                Row.Selected = OutlinerRecordTokensEqual(Row.Record, Result.Record)
                                   || (Result.AdditiveSelect && Row.Selected);
            }
        }
    }

    void ConstructProperties(const ThemeConfiguration& Theme, void* Context)
    {
        UVWorkspaceState& State = *static_cast<UVWorkspaceState*>(Context);
        ConstructUVPropertyPanel(Theme, State);
    }

    void ConstructControl(const ThemeConfiguration& Theme, void* Context)
    {
        UVWorkspaceState& State = *static_cast<UVWorkspaceState*>(Context);
        ActionToolItem Live[4] = {
            { "Unwrap", 0, false, true },
            { "Pack",   0, false, true },
            { "Relax",  0, false, true },
            { "Pin",    0, false, true },
        };
        for (int Index = 0; Index < 4; ++Index)
        {
            Live[Index].Active = (Index == State.ActiveToolIndex);
        }
        ActionToolbarDescriptor Bar = {};
        Bar.Identifier = "##uv-tools"; Bar.Items = Live; Bar.ItemCount = 4; Bar.ButtonSize = 0.0f;
        const int Pressed = ConstructActionToolbar(Theme, Bar);
        if (Pressed >= 0) { State.ActiveToolIndex = Pressed; }
    }

    void ActivateUVRegistry(PanelRegistry& Registry, void* Context)
    {
        ResetPanelRegistry(Registry);

        PanelRegistration Viewport = {};
        Viewport.Identifier = "uv.viewport"; Viewport.Title = "UV";
        Viewport.Region = PanelDockRegion::Viewport; Viewport.Construct = &ConstructViewport; Viewport.Context = Context;
        RegisterPanel(Registry, Viewport);

        PanelRegistration Outliner = {};
        Outliner.Identifier = "uv.outliner"; Outliner.Title = "Islands";
        Outliner.Region = PanelDockRegion::LeftOutliner; Outliner.Construct = &ConstructOutliner; Outliner.Context = Context;
        RegisterPanel(Registry, Outliner);

        PanelRegistration Properties = {};
        Properties.Identifier = "uv.properties"; Properties.Title = "Properties";
        Properties.Region = PanelDockRegion::RightProperties; Properties.Construct = &ConstructProperties; Properties.Context = Context;
        RegisterPanel(Registry, Properties);

        PanelRegistration Control = {};
        Control.Identifier = "uv.control"; Control.Title = "Tools";
        Control.Region = PanelDockRegion::BottomControl; Control.Construct = &ConstructControl; Control.Context = Context;
        RegisterPanel(Registry, Control);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializeUVWorkspace(UVWorkspaceState& State)
{
    InitializeViewportPanelState(State.Viewport, ViewportProjection::Planar);
    SeedPlaceholderIslands(State.Islands);
    State.LayoutExpanded  = true;
    State.DisplayExpanded = true;
    State.PackMargin      = 0.02f;
    State.ShowSeams       = true;
    State.ActiveToolIndex = 0;
}


WorkspaceDescriptor ResolveUVWorkspaceDescriptor(UVWorkspaceState& State)
{
    WorkspaceDescriptor Descriptor = {};
    Descriptor.Identifier       = "UV";
    Descriptor.Title            = "UV";
    Descriptor.ActivateRegistry = &ActivateUVRegistry;
    Descriptor.Context          = &State;
    return Descriptor;
}

}   // namespace Frontier
