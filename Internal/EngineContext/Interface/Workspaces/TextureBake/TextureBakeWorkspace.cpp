/*==============================================================================================================================================
                                                              TEXTUREBAKEWORKSPACE.CPP
==============================================================================================================================================*/
// 🧩 Wires the shared panels into the texture-bake workspace: a 3D preview viewport, the bake-set list as the outliner, the bake property
//    panel, and a bake-action strip. Composition only.

#include "TextureBakeWorkspace.h"

#include "BakePropertyPanel.h"

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
    void SeedPlaceholderBakeSets(OutlinerModel& Model)
    {
        ResetOutlinerModel(Model);
        const char* Names[2] = { "HighPoly -> LowPoly", "Cage" };
        for (int Set = 0; Set < 2; ++Set)
        {
            OutlinerRow Row = {};
            Row.Record.Index = static_cast<unsigned>(Set + 1); Row.Record.Generation = 1;
            Row.Title = Names[Set]; Row.Depth = 0; Row.ChildCount = 0; Row.Expanded = false;
            Row.TintColor = IM_COL32(160, 140, 90, 255); Row.Visible = true; Row.Selected = (Set == 0);
            AppendOutlinerRow(Model, Row);
        }
    }

    void ConstructViewport(const ThemeConfiguration& Theme, void* Context)
    {
        TextureBakeWorkspaceState& State = *static_cast<TextureBakeWorkspaceState*>(Context);
        ConstructViewportPanel(Theme, State.Viewport);
    }

    void ConstructOutliner(const ThemeConfiguration& Theme, void* Context)
    {
        TextureBakeWorkspaceState& State = *static_cast<TextureBakeWorkspaceState*>(Context);
        const OutlinerConfiguration Configuration = ResolveDefaultOutlinerConfiguration(Theme, nullptr);
        const OutlinerPanelResult Result = ConstructOutlinerPanel(Theme, Configuration, State.BakeSets);
        if (Result.Interaction == OutlinerInteraction::Selected)
        {
            for (int Index = 0; Index < State.BakeSets.RowCount; ++Index)
            {
                OutlinerRow& Row = State.BakeSets.Rows[Index];
                Row.Selected = OutlinerRecordTokensEqual(Row.Record, Result.Record)
                                   || (Result.AdditiveSelect && Row.Selected);
            }
        }
    }

    void ConstructProperties(const ThemeConfiguration& Theme, void* Context)
    {
        TextureBakeWorkspaceState& State = *static_cast<TextureBakeWorkspaceState*>(Context);
        ConstructBakePropertyPanel(Theme, State);
    }

    void ConstructControl(const ThemeConfiguration& Theme, void* Context)
    {
        TextureBakeWorkspaceState& State = *static_cast<TextureBakeWorkspaceState*>(Context);
        ActionToolItem Live[2] = {
            { "Bake Selected", 0, false, true },
            { "Bake All",      0, false, true },
        };
        for (int Index = 0; Index < 2; ++Index)
        {
            Live[Index].Active = (Index == State.ActiveToolIndex);
        }
        ActionToolbarDescriptor Bar = {};
        Bar.Identifier = "##bake-tools"; Bar.Items = Live; Bar.ItemCount = 2; Bar.ButtonSize = 0.0f;
        const int Pressed = ConstructActionToolbar(Theme, Bar);
        if (Pressed >= 0) { State.ActiveToolIndex = Pressed; }
    }

    void ActivateBakeRegistry(PanelRegistry& Registry, void* Context)
    {
        ResetPanelRegistry(Registry);

        PanelRegistration Viewport = {};
        Viewport.Identifier = "bake.viewport"; Viewport.Title = "Preview";
        Viewport.Region = PanelDockRegion::Viewport; Viewport.Construct = &ConstructViewport; Viewport.Context = Context;
        RegisterPanel(Registry, Viewport);

        PanelRegistration Outliner = {};
        Outliner.Identifier = "bake.outliner"; Outliner.Title = "Bake Sets";
        Outliner.Region = PanelDockRegion::LeftOutliner; Outliner.Construct = &ConstructOutliner; Outliner.Context = Context;
        RegisterPanel(Registry, Outliner);

        PanelRegistration Properties = {};
        Properties.Identifier = "bake.properties"; Properties.Title = "Properties";
        Properties.Region = PanelDockRegion::RightProperties; Properties.Construct = &ConstructProperties; Properties.Context = Context;
        RegisterPanel(Registry, Properties);

        PanelRegistration Control = {};
        Control.Identifier = "bake.control"; Control.Title = "Bake";
        Control.Region = PanelDockRegion::BottomControl; Control.Construct = &ConstructControl; Control.Context = Context;
        RegisterPanel(Registry, Control);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializeTextureBakeWorkspace(TextureBakeWorkspaceState& State)
{
    InitializeViewportPanelState(State.Viewport, ViewportProjection::Perspective);
    SeedPlaceholderBakeSets(State.BakeSets);
    State.OutputExpanded       = true;
    State.MapsExpanded         = true;
    State.ResolutionIndex      = 1;
    State.BakeAmbientOcclusion = true;
    State.BakeNormal           = true;
    State.BakeCurvature        = false;
    State.ActiveToolIndex      = 0;
}


WorkspaceDescriptor ResolveTextureBakeWorkspaceDescriptor(TextureBakeWorkspaceState& State)
{
    WorkspaceDescriptor Descriptor = {};
    Descriptor.Identifier       = "TextureBake";
    Descriptor.Title            = "Bake";
    Descriptor.ActivateRegistry = &ActivateBakeRegistry;
    Descriptor.Context          = &State;
    return Descriptor;
}

}   // namespace Frontier
