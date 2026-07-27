/*==============================================================================================================================================
                                                              TEXTUREPAINTWORKSPACE.CPP
==============================================================================================================================================*/
// 🧩 Wires the shared panels into the texture-paint workspace: a 3D viewport, the paint layer stack as the outliner, the paint property panel,
//    and a brush-tool strip. Composition only — no chrome defined here.

#include "TexturePaintWorkspace.h"

#include "PaintPropertyPanel.h"

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
    void SeedPlaceholderLayers(OutlinerModel& Model)
    {
        ResetOutlinerModel(Model);
        const char* Names[3] = { "Base", "Detail", "Wear" };
        for (int Layer = 0; Layer < 3; ++Layer)
        {
            OutlinerRow Row = {};
            Row.Record.Index = static_cast<unsigned>(Layer + 1); Row.Record.Generation = 1;
            Row.Title = Names[Layer]; Row.Depth = 0; Row.ChildCount = 0; Row.Expanded = false;
            Row.TintColor = IM_COL32(150, 120, 170, 255); Row.Visible = true; Row.Selected = (Layer == 0);
            AppendOutlinerRow(Model, Row);
        }
    }

    void ConstructViewport(const ThemeConfiguration& Theme, void* Context)
    {
        TexturePaintWorkspaceState& State = *static_cast<TexturePaintWorkspaceState*>(Context);
        ConstructViewportPanel(Theme, State.Viewport);
    }

    void ConstructOutliner(const ThemeConfiguration& Theme, void* Context)
    {
        TexturePaintWorkspaceState& State = *static_cast<TexturePaintWorkspaceState*>(Context);
        const OutlinerConfiguration Configuration = ResolveDefaultOutlinerConfiguration(Theme, nullptr);
        const OutlinerPanelResult Result = ConstructOutlinerPanel(Theme, Configuration, State.Layers);
        if (Result.Interaction == OutlinerInteraction::Selected)
        {
            for (int Index = 0; Index < State.Layers.RowCount; ++Index)
            {
                OutlinerRow& Row = State.Layers.Rows[Index];
                Row.Selected = OutlinerRecordTokensEqual(Row.Record, Result.Record)
                                   || (Result.AdditiveSelect && Row.Selected);
            }
        }
        else if (Result.Interaction == OutlinerInteraction::ToggledVisible)
        {
            for (int Index = 0; Index < State.Layers.RowCount; ++Index)
            {
                if (OutlinerRecordTokensEqual(State.Layers.Rows[Index].Record, Result.Record))
                {
                    State.Layers.Rows[Index].Visible = !State.Layers.Rows[Index].Visible;
                }
            }
        }
    }

    void ConstructProperties(const ThemeConfiguration& Theme, void* Context)
    {
        TexturePaintWorkspaceState& State = *static_cast<TexturePaintWorkspaceState*>(Context);
        ConstructPaintPropertyPanel(Theme, State);
    }

    void ConstructControl(const ThemeConfiguration& Theme, void* Context)
    {
        TexturePaintWorkspaceState& State = *static_cast<TexturePaintWorkspaceState*>(Context);
        ActionToolItem Live[4] = {
            { "Paint",  0, false, true },
            { "Erase",  0, false, true },
            { "Smear",  0, false, true },
            { "Clone",  0, false, true },
        };
        for (int Index = 0; Index < 4; ++Index)
        {
            Live[Index].Active = (Index == State.ActiveToolIndex);
        }
        ActionToolbarDescriptor Bar = {};
        Bar.Identifier = "##paint-tools"; Bar.Items = Live; Bar.ItemCount = 4; Bar.ButtonSize = 0.0f;
        const int Pressed = ConstructActionToolbar(Theme, Bar);
        if (Pressed >= 0) { State.ActiveToolIndex = Pressed; }
    }

    void ActivatePaintRegistry(PanelRegistry& Registry, void* Context)
    {
        ResetPanelRegistry(Registry);

        PanelRegistration Viewport = {};
        Viewport.Identifier = "paint.viewport"; Viewport.Title = "Viewport";
        Viewport.Region = PanelDockRegion::Viewport; Viewport.Construct = &ConstructViewport; Viewport.Context = Context;
        RegisterPanel(Registry, Viewport);

        PanelRegistration Outliner = {};
        Outliner.Identifier = "paint.outliner"; Outliner.Title = "Layers";
        Outliner.Region = PanelDockRegion::LeftOutliner; Outliner.Construct = &ConstructOutliner; Outliner.Context = Context;
        RegisterPanel(Registry, Outliner);

        PanelRegistration Properties = {};
        Properties.Identifier = "paint.properties"; Properties.Title = "Properties";
        Properties.Region = PanelDockRegion::RightProperties; Properties.Construct = &ConstructProperties; Properties.Context = Context;
        RegisterPanel(Registry, Properties);

        PanelRegistration Control = {};
        Control.Identifier = "paint.control"; Control.Title = "Brush";
        Control.Region = PanelDockRegion::BottomControl; Control.Construct = &ConstructControl; Control.Context = Context;
        RegisterPanel(Registry, Control);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializeTexturePaintWorkspace(TexturePaintWorkspaceState& State)
{
    InitializeViewportPanelState(State.Viewport, ViewportProjection::Perspective);
    SeedPlaceholderLayers(State.Layers);
    State.BrushExpanded      = true;
    State.ChannelExpanded    = true;
    State.BrushRadius        = 48.0f;
    State.BrushFlow          = 0.85f;
    State.BrushColor[0]      = 0.85f; State.BrushColor[1] = 0.4f; State.BrushColor[2] = 0.3f; State.BrushColor[3] = 1.0f;
    State.ActiveChannelIndex = 0;
    State.ActiveToolIndex    = 0;
}


WorkspaceDescriptor ResolveTexturePaintWorkspaceDescriptor(TexturePaintWorkspaceState& State)
{
    WorkspaceDescriptor Descriptor = {};
    Descriptor.Identifier       = "TexturePaint";
    Descriptor.Title            = "Paint";
    Descriptor.ActivateRegistry = &ActivatePaintRegistry;
    Descriptor.Context          = &State;
    return Descriptor;
}

}   // namespace Frontier
