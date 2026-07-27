/*==============================================================================================================================================
                                                          PARAMETRICSKETCHERWORKSPACE.CPP
==============================================================================================================================================*/
// 🧩 Wires the shared panels into the parametric-sketcher workspace: a planar sketch-plane viewport, the sketch record tree as the outliner,
//    the constraint/dimension property panel, and a sketch-tool strip. Composition only.

#include "ParametricSketcherWorkspace.h"

#include "SketchPropertyPanel.h"

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
    void SeedPlaceholderSketchRecords(OutlinerModel& Model)
    {
        ResetOutlinerModel(Model);

        OutlinerRow Sketch = {};
        Sketch.Record.Index = 1; Sketch.Record.Generation = 1;
        Sketch.Title = "Sketch.001"; Sketch.Depth = 0; Sketch.ChildCount = 3; Sketch.Expanded = true;
        Sketch.TintColor = IM_COL32(120, 140, 170, 255); Sketch.Visible = true;
        AppendOutlinerRow(Model, Sketch);

        const char* Names[3] = { "Line", "Arc", "Circle" };
        for (int Record = 0; Record < 3; ++Record)
        {
            OutlinerRow Row = {};
            Row.Record.Index = static_cast<unsigned>(Record + 2); Row.Record.Generation = 1;
            Row.Title = Names[Record]; Row.Depth = 1; Row.ChildCount = 0; Row.Expanded = false;
            Row.TintColor = IM_COL32(150, 170, 190, 255); Row.Visible = true; Row.Selected = (Record == 0);
            AppendOutlinerRow(Model, Row);
        }
    }

    void ConstructViewport(const ThemeConfiguration& Theme, void* Context)
    {
        ParametricSketcherWorkspaceState& State = *static_cast<ParametricSketcherWorkspaceState*>(Context);
        ConstructViewportPanel(Theme, State.Viewport);
    }

    void ConstructOutliner(const ThemeConfiguration& Theme, void* Context)
    {
        ParametricSketcherWorkspaceState& State = *static_cast<ParametricSketcherWorkspaceState*>(Context);
        const OutlinerConfiguration Configuration = ResolveDefaultOutlinerConfiguration(Theme, nullptr);
        const OutlinerPanelResult Result = ConstructOutlinerPanel(Theme, Configuration, State.SketchRecords);
        if (Result.Interaction == OutlinerInteraction::Selected)
        {
            for (int Index = 0; Index < State.SketchRecords.RowCount; ++Index)
            {
                OutlinerRow& Row = State.SketchRecords.Rows[Index];
                Row.Selected = OutlinerRecordTokensEqual(Row.Record, Result.Record)
                                   || (Result.AdditiveSelect && Row.Selected);
            }
        }
        else if (Result.Interaction == OutlinerInteraction::ToggledExpand)
        {
            for (int Index = 0; Index < State.SketchRecords.RowCount; ++Index)
            {
                if (OutlinerRecordTokensEqual(State.SketchRecords.Rows[Index].Record, Result.Record))
                {
                    State.SketchRecords.Rows[Index].Expanded = !State.SketchRecords.Rows[Index].Expanded;
                }
            }
        }
    }

    void ConstructProperties(const ThemeConfiguration& Theme, void* Context)
    {
        ParametricSketcherWorkspaceState& State = *static_cast<ParametricSketcherWorkspaceState*>(Context);
        ConstructSketchPropertyPanel(Theme, State);
    }

    void ConstructControl(const ThemeConfiguration& Theme, void* Context)
    {
        ParametricSketcherWorkspaceState& State = *static_cast<ParametricSketcherWorkspaceState*>(Context);
        ActionToolItem Live[6] = {
            { "Line",      0, false, true },
            { "Arc",       0, false, true },
            { "Circle",    0, false, true },
            { "Fillet",    0, false, true },
            { "Trim",      0, false, true },
            { "Dimension", 0, false, true },
        };
        for (int Index = 0; Index < 6; ++Index)
        {
            Live[Index].Active = (Index == State.ActiveToolIndex);
        }
        ActionToolbarDescriptor Bar = {};
        Bar.Identifier = "##sketch-tools"; Bar.Items = Live; Bar.ItemCount = 6; Bar.ButtonSize = 0.0f;
        const int Pressed = ConstructActionToolbar(Theme, Bar);
        if (Pressed >= 0) { State.ActiveToolIndex = Pressed; }
    }

    void ActivateSketcherRegistry(PanelRegistry& Registry, void* Context)
    {
        ResetPanelRegistry(Registry);

        PanelRegistration Viewport = {};
        Viewport.Identifier = "sketch.viewport"; Viewport.Title = "Sketch Plane";
        Viewport.Region = PanelDockRegion::Viewport; Viewport.Construct = &ConstructViewport; Viewport.Context = Context;
        RegisterPanel(Registry, Viewport);

        PanelRegistration Outliner = {};
        Outliner.Identifier = "sketch.outliner"; Outliner.Title = "Records";
        Outliner.Region = PanelDockRegion::LeftOutliner; Outliner.Construct = &ConstructOutliner; Outliner.Context = Context;
        RegisterPanel(Registry, Outliner);

        PanelRegistration Properties = {};
        Properties.Identifier = "sketch.properties"; Properties.Title = "Properties";
        Properties.Region = PanelDockRegion::RightProperties; Properties.Construct = &ConstructProperties; Properties.Context = Context;
        RegisterPanel(Registry, Properties);

        PanelRegistration Control = {};
        Control.Identifier = "sketch.control"; Control.Title = "Tools";
        Control.Region = PanelDockRegion::BottomControl; Control.Construct = &ConstructControl; Control.Context = Context;
        RegisterPanel(Registry, Control);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializeParametricSketcherWorkspace(ParametricSketcherWorkspaceState& State)
{
    InitializeViewportPanelState(State.Viewport, ViewportProjection::Planar);
    SeedPlaceholderSketchRecords(State.SketchRecords);
    State.ConstraintExpanded = true;
    State.DimensionExpanded  = true;
    State.DimensionValue      = 25.0f;
    State.ConstraintIndex     = 0;
    State.ActiveToolIndex     = 0;
}


WorkspaceDescriptor ResolveParametricSketcherWorkspaceDescriptor(ParametricSketcherWorkspaceState& State)
{
    WorkspaceDescriptor Descriptor = {};
    Descriptor.Identifier       = "ParametricSketcher";
    Descriptor.Title            = "Sketcher";
    Descriptor.ActivateRegistry = &ActivateSketcherRegistry;
    Descriptor.Context          = &State;
    return Descriptor;
}

}   // namespace Frontier
