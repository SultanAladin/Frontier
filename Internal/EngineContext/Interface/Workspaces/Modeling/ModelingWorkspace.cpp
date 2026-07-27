/*==============================================================================================================================================
                                                              MODELINGWORKSPACE.CPP
==============================================================================================================================================*/
// 🧩 Wires the shared panels into the modeling workspace. ActivateRegistry registers four panels — viewport, outliner, properties, control —
//    each a thin callback over a SHARED component with this workspace's state as context. The dock host records them; this file owns no chrome.
//    Placeholder outliner rows stand in until Scene is wired, so the workspace is live + navigable now.

#include "ModelingWorkspace.h"

#include "ModelingPropertyPanel.h"

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
    // 📝 Seed a few placeholder rows so the outliner reads as populated before Scene is wired. Tokens carry a generation stamp (never slots).
    void SeedPlaceholderOutliner(OutlinerModel& Model)
    {
        ResetOutlinerModel(Model);

        OutlinerRow Root = {};
        Root.Record.Index = 1; Root.Record.Generation = 1;
        Root.Title = "Scene"; Root.Depth = 0; Root.ChildCount = 2; Root.Expanded = true;
        Root.TintColor = IM_COL32(120, 120, 130, 255); Root.Visible = true;
        AppendOutlinerRow(Model, Root);

        OutlinerRow NestedA = {};
        NestedA.Record.Index = 2; NestedA.Record.Generation = 1;
        NestedA.Title = "Suzanne"; NestedA.Depth = 1; NestedA.ChildCount = 0; NestedA.Expanded = false;
        NestedA.TintColor = IM_COL32(196, 150, 90, 255); NestedA.Visible = true; NestedA.Selected = true;
        AppendOutlinerRow(Model, NestedA);

        OutlinerRow NestedB = {};
        NestedB.Record.Index = 3; NestedB.Record.Generation = 1;
        NestedB.Title = "Ground"; NestedB.Depth = 1; NestedB.ChildCount = 0; NestedB.Expanded = false;
        NestedB.TintColor = IM_COL32(90, 130, 160, 255); NestedB.Visible = true;
        AppendOutlinerRow(Model, NestedB);
    }

    // -- Panel callbacks (registered into the dock; Context is the ModelingWorkspaceState) ------------------------------

    void ConstructViewport(const ThemeConfiguration& Theme, void* Context)
    {
        ModelingWorkspaceState& State = *static_cast<ModelingWorkspaceState*>(Context);
        ConstructViewportPanel(Theme, State.Viewport);
    }

    void ConstructOutliner(const ThemeConfiguration& Theme, void* Context)
    {
        ModelingWorkspaceState& State = *static_cast<ModelingWorkspaceState*>(Context);
        const OutlinerConfiguration Configuration = ResolveDefaultOutlinerConfiguration(Theme, nullptr);
        const OutlinerPanelResult Result = ConstructOutlinerPanel(Theme, Configuration, State.Outliner);

        // 📝 Apply outliner interactions against the placeholder model (Scene routing lands with the Scene extension).
        if (Result.Interaction == OutlinerInteraction::ToggledVisible)
        {
            for (int Index = 0; Index < State.Outliner.RowCount; ++Index)
            {
                if (OutlinerRecordTokensEqual(State.Outliner.Rows[Index].Record, Result.Record))
                {
                    State.Outliner.Rows[Index].Visible = !State.Outliner.Rows[Index].Visible;
                }
            }
        }
        else if (Result.Interaction == OutlinerInteraction::Selected)
        {
            for (int Index = 0; Index < State.Outliner.RowCount; ++Index)
            {
                OutlinerRow& Row = State.Outliner.Rows[Index];
                Row.Selected = OutlinerRecordTokensEqual(Row.Record, Result.Record)
                                   || (Result.AdditiveSelect && Row.Selected);
            }
        }
    }

    void ConstructProperties(const ThemeConfiguration& Theme, void* Context)
    {
        ModelingWorkspaceState& State = *static_cast<ModelingWorkspaceState*>(Context);
        ConstructModelingPropertyPanel(Theme, State);
    }

    void ConstructControl(const ThemeConfiguration& Theme, void* Context)
    {
        ModelingWorkspaceState& State = *static_cast<ModelingWorkspaceState*>(Context);

        static const ActionToolItem Tools[] = {
            { "Select",    0, false, true },
            { "Extrude",   0, false, true },
            { "Inset",     0, false, true },
            { "Bevel",     0, false, true },
            { "Subdivide", 0, false, true },
        };
        const int ToolCount = static_cast<int>(sizeof(Tools) / sizeof(Tools[0]));

        // 📝 Rebuild the item array each cycle with the active flag reflecting state (items are caller-owned per record).
        ActionToolItem Live[8];
        for (int Index = 0; Index < ToolCount; ++Index)
        {
            Live[Index] = Tools[Index];
            Live[Index].Active = (Index == State.ActiveToolIndex);
        }

        ActionToolbarDescriptor Bar = {};
        Bar.Identifier = "##modeling-tools";
        Bar.Items      = Live;
        Bar.ItemCount  = ToolCount;
        Bar.ButtonSize = 0.0f;
        const int Pressed = ConstructActionToolbar(Theme, Bar);
        if (Pressed >= 0)
        {
            State.ActiveToolIndex = Pressed;
        }
    }

    // 📝 Registry activation: register the four standard panels with their dock regions + this workspace's context.
    void ActivateModelingRegistry(PanelRegistry& Registry, void* Context)
    {
        ResetPanelRegistry(Registry);

        PanelRegistration Viewport = {};
        Viewport.Identifier = "modeling.viewport"; Viewport.Title = "Viewport";
        Viewport.Region = PanelDockRegion::Viewport; Viewport.Construct = &ConstructViewport; Viewport.Context = Context;
        RegisterPanel(Registry, Viewport);

        PanelRegistration Outliner = {};
        Outliner.Identifier = "modeling.outliner"; Outliner.Title = "Scene";
        Outliner.Region = PanelDockRegion::LeftOutliner; Outliner.Construct = &ConstructOutliner; Outliner.Context = Context;
        RegisterPanel(Registry, Outliner);

        PanelRegistration Properties = {};
        Properties.Identifier = "modeling.properties"; Properties.Title = "Properties";
        Properties.Region = PanelDockRegion::RightProperties; Properties.Construct = &ConstructProperties; Properties.Context = Context;
        RegisterPanel(Registry, Properties);

        PanelRegistration Control = {};
        Control.Identifier = "modeling.control"; Control.Title = "Tools";
        Control.Region = PanelDockRegion::BottomControl; Control.Construct = &ConstructControl; Control.Context = Context;
        RegisterPanel(Registry, Control);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializeModelingWorkspace(ModelingWorkspaceState& State)
{
    InitializeViewportPanelState(State.Viewport, ViewportProjection::Perspective);
    SeedPlaceholderOutliner(State.Outliner);

    State.TransformExpanded = true;
    State.GeometryExpanded  = true;
    State.ShadingExpanded   = false;

    for (int Axis = 0; Axis < 3; ++Axis)
    {
        State.TransformPosition[Axis] = 0.0f;
        State.TransformRotation[Axis] = 0.0f;
        State.TransformScale[Axis]    = 1.0f;
    }
    State.ActiveToolIndex = 0;
}


WorkspaceDescriptor ResolveModelingWorkspaceDescriptor(ModelingWorkspaceState& State)
{
    WorkspaceDescriptor Descriptor = {};
    Descriptor.Identifier       = "Modeling";
    Descriptor.Title            = "Modeling";
    Descriptor.ActivateRegistry = &ActivateModelingRegistry;
    Descriptor.Context          = &State;
    return Descriptor;
}

}   // namespace Frontier
