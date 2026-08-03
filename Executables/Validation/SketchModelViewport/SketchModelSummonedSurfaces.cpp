/*==============================================================================================================================================
                                                    SKETCHMODELSUMMONEDSURFACES.CPP
==============================================================================================================================================*/
// 🧩 The arbitration and driving of the viewport's two summoned surfaces. Nothing here draws a row, a tile, a rail or a carousel: the directory card
//    is ConstructSceneDirectoryInspectorPanel and the action console is ConstructWorkspaceContextConsole, both embedded whole. This file decides
//    only WHEN each may open, and holds the console's summon placement.
//
//    🔴 The two openers cannot collide. Tab reaches the inspector because the host omits ImGuiConfigFlags_NavEnableKeyboard; the right-click reaches
//       the console because the shared viewport surface button binds only left + middle. The console additionally refuses to arm while the directory
//       card is summoned, so the card's own dismiss veil is never competing with a console opening beneath it.

#include "SketchModelSummonedSurfaces.h"

#include "EngineContext/Interface/Icons/SvgIconRegistry.h"

#include <cstring>

#include "imgui.h"

namespace SketchModelViewportValidation
{

namespace SDI = SceneDirectoryInspectorValidation;
namespace CC  = ConstructionCatalogueValidation;

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // Whether an absolute point lies inside the reported summon field (always true before a field has been reported).
    bool WithinSummonField(const SketchModelSummonedState& State, ImVec2 Point)
    {
        if (State.SummonFieldWidth <= 0.0f || State.SummonFieldHeight <= 0.0f) { return true; }
        return Point.x >= State.SummonFieldLeft && Point.x <= State.SummonFieldLeft + State.SummonFieldWidth &&
               Point.y >= State.SummonFieldTop  && Point.y <= State.SummonFieldTop  + State.SummonFieldHeight;
    }

    // 📝 Whether a committed console action (its cluster + action index) is the References→Workplane op. The bridge builds the console's
    //    clusters 1:1 from the catalogue bands (cluster index == band index, action index == op index within it, dense, no skipping), so the
    //    indices resolve straight back into the authored catalogue. Identify the op by its GlyphName ("RefWorkplane") rather than a fixed
    //    (cluster, action) pair, so a re-ordered catalogue still arms the right action. -1 indices (a single-shot cluster) never match.
    bool CommittedActionIsWorkplane(int ClusterIndex, int ActionIndex)
    {
        if (ClusterIndex < 0 || ActionIndex < 0)
            return false;

        int BandCount = 0;
        const CC::ConstructionBand* const Bands = CC::ResolveConstructionBands(BandCount);
        if (ClusterIndex >= BandCount)
            return false;

        const CC::ConstructionBand& Band = Bands[ClusterIndex];
        if (ActionIndex >= Band.OperationCount)
            return false;

        const char* const Glyph = Band.Operations[ActionIndex].GlyphName;
        return Glyph != nullptr && std::strcmp(Glyph, "RefWorkplane") == 0;
    }

    // 📝 Resolve a committed console action to a drawable 2D primitive category, or false if it is not one. The Sketch Geometry band's ops carry
    //    glyph names (SketchLine / SketchRectangle / …) that map 1:1 onto ParametricSketchShapeCategory; only the categories this cut draws are
    //    matched (Line / Rectangle / Circle / Ellipse / Polygon / Slot). Same dense (cluster, action) → catalogue mapping as the workplane check.
    bool CommittedActionIsSketchShape(int ClusterIndex, int ActionIndex, Frontier::ParametricSketchShapeCategory& OutCategory)
    {
        if (ClusterIndex < 0 || ActionIndex < 0)
            return false;

        int BandCount = 0;
        const CC::ConstructionBand* const Bands = CC::ResolveConstructionBands(BandCount);
        if (ClusterIndex >= BandCount)
            return false;

        const CC::ConstructionBand& Band = Bands[ClusterIndex];
        if (ActionIndex >= Band.OperationCount)
            return false;

        const char* const Glyph = Band.Operations[ActionIndex].GlyphName;
        if (Glyph == nullptr)
            return false;

        struct GlyphCategory { const char* Glyph; Frontier::ParametricSketchShapeCategory Category; };
        static const GlyphCategory Table[] =
        {
            { "SketchLine",      Frontier::ParametricSketchShapeCategory::Line      },
            { "SketchRectangle", Frontier::ParametricSketchShapeCategory::Rectangle },
            { "SketchCircle",    Frontier::ParametricSketchShapeCategory::Circle    },
            { "SketchEllipse",   Frontier::ParametricSketchShapeCategory::Ellipse   },
            { "SketchPolygon",   Frontier::ParametricSketchShapeCategory::Polygon   },
            { "SketchSlot",      Frontier::ParametricSketchShapeCategory::Slot      },
        };
        for (const GlyphCategory& Row : Table)
            if (std::strcmp(Glyph, Row.Glyph) == 0)
            {
                OutCategory = Row.Category;
                return true;
            }
        return false;
    }

    // 📝 Place the console fully on screen from the pointer: it opens down-right of the press, folding back when either edge would cross the
    //    viewport. The console's own metrics give its extent, so the clamp tracks a re-scaled card with no second constant here.
    ImVec2 ResolveConsolePlacement(const Frontier::ThemeConfiguration& Theme, ImVec2 Pointer, ImVec2 DisplaySpan)
    {
        const Frontier::MetricsSpecification Metrics = Frontier::ResolveConsoleMetrics(Theme);
        const float Margin = 12.0f;                                    // [px] - the gap the console keeps off every viewport edge
        float Left = Pointer.x + 10.0f;
        float Top  = Pointer.y + 10.0f;
        if (Left + Metrics.CardWidth  > DisplaySpan.x - Margin) { const float Folded = Pointer.x - Metrics.CardWidth - 10.0f;       Left = (Folded > Margin) ? Folded : Margin; }
        if (Top  + Metrics.CardHeight > DisplaySpan.y - Margin) { const float Folded = DisplaySpan.y - Margin - Metrics.CardHeight; Top  = (Folded > Margin) ? Folded : Margin; }
        return ImVec2(Left, Top);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializeSketchModelSummonedSurfaces(SketchModelSummonedState& State)
{
    // -- The directory card: an empty tree + an empty history, summoned on Tab (its own seed leaves both blank) --
    SDI::InitializeInspectorSample(State.Directory);

    // -- The action console: the default document, closed until a right-click asks for it --
    State.Document = CC::DefaultConstructionDocument();

    State.ConsoleFocus.OpenCluster = 0;    // hold the first cluster; the console resolves the first populated one itself
    State.ConsoleFocus.OpenAction  = -1;

    State.ConsoleCarousel.ShowingOptions = false;
    State.ConsoleCarousel.Travel         = 0.0f;
    State.ConsoleCarousel.OpenAge        = 0.0f;

    State.ConsoleReadings.Cluster = -1;
    State.ConsoleReadings.Action  = -1;

    State.ConsoleOpen              = false;
    State.ConsoleAnchorX           = 0.0f;
    State.ConsoleAnchorY           = 0.0f;
    State.ConsoleReanchorRequested = false;
}


void ConfineSketchModelSummonField(SketchModelSummonedState& State, ImVec2 CanvasOrigin, ImVec2 CanvasSpan)
{
    State.SummonFieldLeft   = CanvasOrigin.x;
    State.SummonFieldTop    = CanvasOrigin.y;
    State.SummonFieldWidth  = CanvasSpan.x;
    State.SummonFieldHeight = CanvasSpan.y;
}


void ConstructSketchModelSummonedSurfaces(const Frontier::ThemeConfiguration& Theme,
                                          SketchModelSummonedState&           State,
                                          const Frontier::SvgIconRegistry*    Icons)
{
    const ImGuiIO& Io = ImGui::GetIO();

    // -- The directory card. It owns its own Tab summon, its two carousels, its dismiss veil and its Escape stepping, so it is simply recorded
    //    every frame and draws nothing while closed. --
    SDI::ConstructSceneDirectoryInspectorPanel(Theme, State.Directory, Icons);

    // -- Arm the console's summon on the Q key, but only over the canvas and only while the directory card is NOT summoned. --
    //    🔴 The context menu is bound to Q, NOT right-click: right-drag now drives the camera orbit (see the sketch panel), so a right press is a
    //       navigation gesture, not a request for the catalogue. The press is LATCHED, not acted on, and applied at the tail after the console has
    //       reported — the console reads an outside press as a dismissal, so opening here would fight it. Q lands over the hovered canvas rect.
    const bool DirectorySummoned = State.Directory.SummonOpen;
    const bool DrawActive        = WorkplaneDrawActive(State.WorkplaneDraw) || ShapeDrawActive(State.ShapeStore);
    // 🔴 While an interactive draw is armed / in progress the console must NOT re-arm: Q would drop a fresh catalogue over an in-progress gesture.
    //    Blocking the latch here keeps the two off each other.
    if (!DirectorySummoned && !DrawActive && ImGui::IsKeyPressed(ImGuiKey_Q, false) && WithinSummonField(State, Io.MousePos))
    {
        State.ConsoleReanchorRequested = true;
    }

    // -- Advance the console's slide travel, then draw it against the live gate. --
    Frontier::AdvanceConsoleCarousel(State.ConsoleCarousel, Io.DeltaTime, Frontier::ResolveConsoleMetrics(Theme));

    if (State.ConsoleOpen)
    {
        // 🔴 This viewport is a 2D SKETCH surface: the ground plane Z = 0 IS the always-present workplane the primitives draw onto, so the catalogue's
        //    workplane requirement is satisfied by construction here. Without this the Sketch* ops carry NeedMask's workplane bit (0x0001) and the gate
        //    returns WorkplaneAbsent → the tile greys to "set workplane" and never fires CommitRequested, so a primitive can never arm. Raise the
        //    document's WorkplaneActivation so those ops resolve Live and commit. (Parenting a drawn shape to an authored workplane vs. a new folder is
        //    decided later, at mirror time, off whether a Workplane row actually exists in the tree — not off this gate field.)
        State.Document.WorkplaneActivation = true;
        CC::BindConstructionConsoleContext(State.Bridge, State.Document);
        const Frontier::WorkspaceContextConsoleDescriptor Descriptor = CC::ComposeConstructionConsoleDescriptor(State.Bridge);

        const Frontier::ConsoleResult Outcome = Frontier::ConstructWorkspaceContextConsole(
            Icons, Descriptor, ImVec2(State.ConsoleAnchorX, State.ConsoleAnchorY),
            State.ConsoleFocus, State.ConsoleCarousel, State.ConsoleReadings, Theme);

        if (Outcome.DismissRequested) { State.ConsoleOpen = false; }

        // 🔴 References→Workplane commit ARMS the interactive draw and closes the console — nothing is added to the outliner yet. The plane is
        //    added only after the second ground click confirms (AdvanceWorkplaneDraw). Detect it off the committed (cluster, action) indices.
        if (Outcome.CommitRequested && CommittedActionIsWorkplane(Outcome.ActivatedCluster, Outcome.ActivatedAction))
        {
            ArmWorkplaneDraw(State.WorkplaneDraw);
            State.ConsoleOpen = false;
        }

        // 🔴 A committed Sketch* primitive op ARMS a shape draw on the store (nothing is added to the outliner yet — the shape seals only when the
        //    click gesture completes, and AdvanceShapeDraw mirrors it then). Same commit-off-(cluster, action) detection as the workplane.
        Frontier::ParametricSketchShapeCategory SketchCategory = Frontier::ParametricSketchShapeCategory::Line;
        if (Outcome.CommitRequested && CommittedActionIsSketchShape(Outcome.ActivatedCluster, Outcome.ActivatedAction, SketchCategory))
        {
            // 🔴 LATCH the tool so it stays active: after each shape seals, the panel re-arms this same category for the next cycle (SustainSketchToolCycle),
            //    so the user draws rectangle after rectangle without reopening the menu. The latch is the cycle owner; Escape clears it to leave the tool.
            LatchSketchTool(State.ToolLatch, SketchCategory);
            ArmShapeDraw(State.ShapeStore, SketchCategory);
            State.ConsoleOpen = false;
        }

        // 🔴 The console reports no dismissal of its own (ConsoleResult.DismissRequested is never set inside the pane), so the OUTSIDE press
        //    must close it here — otherwise Q opens it and only a Tab card summon ever closes it, so it hangs over the canvas while the user
        //    orbits. Any mouse press (orbit now starts on either left- OR right-drag) or Escape that lands OUTSIDE the console's own rect
        //    dismisses it. The Q summon is a KEY, not a mouse press, so it never trips this outside-press dismissal.
        const Frontier::MetricsSpecification Metrics = Frontier::ResolveConsoleMetrics(Theme);
        const bool OverConsole =
            Io.MousePos.x >= State.ConsoleAnchorX && Io.MousePos.x <= State.ConsoleAnchorX + Metrics.CardWidth &&
            Io.MousePos.y >= State.ConsoleAnchorY && Io.MousePos.y <= State.ConsoleAnchorY + Metrics.CardHeight;
        const bool PressedOutside =
            !OverConsole &&
            (ImGui::IsMouseClicked(ImGuiMouseButton_Left)   ||
             ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
             ImGui::IsMouseClicked(ImGuiMouseButton_Right));
        if (PressedOutside && !State.ConsoleReanchorRequested) { State.ConsoleOpen = false; }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))       { State.ConsoleOpen = false; }
    }

    // 📝 The directory card dismisses the console: both are summoned over the same canvas, and the card draws its own veil over everything, so
    //    leaving the console standing underneath would put an unreachable surface behind the veil.
    if (DirectorySummoned) { State.ConsoleOpen = false; }

    // -- Apply the latched Q summon now (place + clamp the console fully on screen at the cursor). --
    if (State.ConsoleReanchorRequested)
    {
        const ImVec2 Placement = ResolveConsolePlacement(Theme, Io.MousePos, Io.DisplaySize);
        State.ConsoleAnchorX           = Placement.x;
        State.ConsoleAnchorY           = Placement.y;
        State.ConsoleOpen              = true;
        State.ConsoleCarousel.OpenAge  = 0.0f;
        State.ConsoleReanchorRequested = false;
    }
}

}   // namespace SketchModelViewportValidation
