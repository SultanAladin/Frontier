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

#include <cstdio>
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
    //    glyph names (SketchLine / SketchRectangle / …) that map 1:1 onto ParametricSketchShapeCategory. The fixed-count families (Line / Rectangle /
    //    Circle / Ellipse / Polygon / Slot / Arc) AND the open-ended curve families (Polyline / Bezier / Spline via the catalogue's SketchPolyline /
    //    SketchBezier / SketchSpline glyphs) are matched here; AdvanceShapeDraw seals the former on the completing click and the latter on a finish
    //    gesture. Same dense (cluster, action) → catalogue mapping as the workplane check. The band now exposes the full free-curve set (SketchPolyline
    //    / SketchBezier / SketchSpline / SketchBSpline / SketchNurbs — all open-ended — plus the fixed-3-point SketchConic), each mapping 1:1 onto its
    //    store category. The former SketchArcThree / SketchCircleThree 3-point glyphs were dropped: Arc already IS the 3-point circumcircle solve, and no
    //    3-point circle solver exists, so they were duplicate / unbacked.
    bool CommittedActionIsSketchShape(int ClusterIndex, int ActionIndex,
                                      Frontier::ParametricSketchShapeCategory& OutCategory, bool& OutCentreRect)
    {
        OutCentreRect = false;
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

        // 📝 CentreRect shares the Rectangle store category (there is one Rectangle solver, two opposite corners) but changes how the TWO clicks are
        //    read: click 1 is the CENTRE, click 2 is one corner, and AdvanceShapeDraw mirrors the corner about the centre to seal a full centred box.
        //    That interpretation is a viewport-draw affordance, so it rides a flag out of here rather than a distinct store category.
        struct GlyphCategory { const char* Glyph; Frontier::ParametricSketchShapeCategory Category; bool CentreRect; };
        static const GlyphCategory Table[] =
        {
            { "SketchLine",       Frontier::ParametricSketchShapeCategory::Line,      false },
            { "SketchRectangle",  Frontier::ParametricSketchShapeCategory::Rectangle, false },
            { "SketchCentreRect", Frontier::ParametricSketchShapeCategory::Rectangle, true  },
            { "SketchCircle",     Frontier::ParametricSketchShapeCategory::Circle,    false },
            { "SketchEllipse",    Frontier::ParametricSketchShapeCategory::Ellipse,   false },
            { "SketchPolygon",    Frontier::ParametricSketchShapeCategory::Polygon,   false },
            { "SketchSlot",       Frontier::ParametricSketchShapeCategory::Slot,      false },
            // -- The Arc: three points solve the circumcircle-arc (fixed-3). --
            { "SketchArc",        Frontier::ParametricSketchShapeCategory::Arc,       false },
            // -- The Conic: three points solve the conic section (fixed-3). --
            { "SketchConic",      Frontier::ParametricSketchShapeCategory::Conic,     false },
            // -- The open-ended curve families: each collects control points until a finish gesture (Enter / double-click) seals it. --
            { "SketchPolyline",   Frontier::ParametricSketchShapeCategory::Polyline,  false },
            { "SketchBezier",     Frontier::ParametricSketchShapeCategory::Bezier,    false },
            { "SketchSpline",     Frontier::ParametricSketchShapeCategory::Spline,    false },
            { "SketchBSpline",    Frontier::ParametricSketchShapeCategory::BSpline,   false },
            { "SketchNurbs",      Frontier::ParametricSketchShapeCategory::Nurbs,     false },
        };
        for (const GlyphCategory& Row : Table)
            if (std::strcmp(Glyph, Row.Glyph) == 0)
            {
                OutCategory   = Row.Category;
                OutCentreRect = Row.CentreRect;
                return true;
            }
        return false;
    }

    // 📝 Whether a committed console action is a 2D SKETCH-MODIFY op (Fillet / Chamfer / Trim / Extend / Offset), and — through OutLabel — WHICH. These
    //    are the ops the selection → ActiveDimension wire just un-hid, so recognising a commit here is the surface-only first pass: it names the op and
    //    reports the operands the pick resolved WITHOUT running a geometry kernel yet (the Fillet / Chamfer solver is the scoped follow-up). Same dense
    //    (cluster, action) → catalogue mapping as CommittedActionIsWorkplane / CommittedActionIsSketchShape — the bridge builds clusters 1:1 from the
    //    bands, so the indices resolve straight back to the authored op's GlyphName. OutLabel receives the human op name for the log line; false (and
    //    OutLabel is left untouched) when the commit is not a Modify op.
    bool CommittedActionIsSketchModify(int ClusterIndex, int ActionIndex, const char*& OutLabel)
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

        struct ModifyGlyph { const char* Glyph; const char* Label; };
        static const ModifyGlyph Table[] =
        {
            { "SketchSelect",  "Select"  },
            { "SketchFillet",  "Fillet"  },
            { "SketchChamfer", "Chamfer" },
            { "SketchTrim",    "Trim"    },
            { "SketchExtend",  "Extend"  },
            { "SketchOffset",  "Offset"  },
            { "SketchCut",     "Cut"     },
            { "SketchJoin",    "Join"    },
            { "SketchRemove",  "Remove"  },
        };
        for (const ModifyGlyph& Row : Table)
            if (std::strcmp(Glyph, Row.Glyph) == 0)
            {
                OutLabel = Row.Label;
                return true;
            }
        return false;
    }

    // 📝 The Select tool's Stratum reading (0 Shape / 1 Vertex / 2 Edge) out of the console's live parameter block — its ONE authored row, so row 0
    //    carries it. Falls back to 0 (whole-shape, the historical default) whenever the block holds no rows, which is what an action committed
    //    straight off the grid without ever opening its options looks like.
    int ResolveSketchSelectStratum(const Frontier::ParameterBlock& Readings)
    {
        if (Readings.RowCount < 1)
            return 0;
        const int Chosen = Readings.Rows[0].ChosenOption;
        return (Chosen >= 0 && Chosen <= 2) ? Chosen : 0;
    }

    // 📝 Whether a committed console action is specifically the Fillet or Chamfer corner-edit op — the two that arm the drag modal (the ported
    //    Plasticity `B` tool). Trim / Extend / Offset are still surface-only, so they take the log branch, NOT the arm. Same dense (cluster, action)
    //    → GlyphName mapping; a thin wrapper over CommittedActionIsSketchModify that additionally checks the label is one of the corner ops.
    bool CommittedActionIsCornerEdit(int ClusterIndex, int ActionIndex)
    {
        const char* Label = nullptr;
        if (!CommittedActionIsSketchModify(ClusterIndex, ActionIndex, Label))
            return false;
        return std::strcmp(Label, "Fillet") == 0 || std::strcmp(Label, "Chamfer") == 0;
    }

    // 📝 Classify a committed Sketch-Modify op as one of the five click-to-apply COMMAND tools (Trim / Cut / Join / Remove / Extend), returning its
    //    SketchCommandTool (None when the op is not one of them — Offset is the drag modal, Fillet / Chamfer the corner modal). A thin label lookup over
    //    CommittedActionIsSketchModify: a match here arms the command-tool driver instead of the surface-only report / the corner-edit modal.
    SketchCommandTool CommittedActionCommandTool(int ClusterIndex, int ActionIndex)
    {
        const char* Label = nullptr;
        if (!CommittedActionIsSketchModify(ClusterIndex, ActionIndex, Label))
            return SketchCommandTool::None;
        if (std::strcmp(Label, "Trim")   == 0) return SketchCommandTool::Trim;
        if (std::strcmp(Label, "Cut")    == 0) return SketchCommandTool::Cut;
        if (std::strcmp(Label, "Join")   == 0) return SketchCommandTool::Join;
        if (std::strcmp(Label, "Remove") == 0) return SketchCommandTool::Remove;
        if (std::strcmp(Label, "Extend") == 0) return SketchCommandTool::Extend;
        return SketchCommandTool::None;
    }

    // 📝 Whether a committed console action is the SWEEP-band EXTRUDE op ("SweepPrism"). Same dense (cluster, action) → GlyphName mapping every other
    //    classifier here uses. Extrude is the first Sweep-band op with a live dispatcher; the rest of the band still commits nothing.
    bool CommittedActionIsExtrude(int ClusterIndex, int ActionIndex)
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
        return Glyph != nullptr && std::strcmp(Glyph, "SweepPrism") == 0;
    }

    // 🔴 The one thing the Sweep band's parameter block still decides: whether the sweep STRADDLES the sketch plane. Row 1 is the Direction segmented
    //    option {Normal, Reversed, Symmetric}, and only Symmetric (index 2) changes anything the gesture cannot express:
    //      • Normal / Reversed are just the SIGN of the height, and the drag already carries a sign — pulling the pointer up the sweep axis extrudes up,
    //        pulling it down extrudes down — so forcing a sign from the menu would fight the pointer;
    //      • Symmetric is a CENTRING, which no pointer direction can convey, so it must be read here.
    //    Row 0's "Length" slider is likewise no longer read: the pointer sets the height, and typing a number during the drag sets it exactly (which is
    //    the same affordance the slider offered, but in the gesture where it belongs). Taper (row 2) and Axis (row 3) stay unhonoured — a prism sweep has
    //    no draft angle and always follows the plane normal, so reading them would imply support that does not exist.
    bool ResolveExtrudeSymmetricEnabled(const Frontier::ParameterBlock& Readings)
    {
        return Readings.RowCount >= 2 && Readings.Rows[1].ChosenOption == 2;
    }

    // 📝 Whether a shape's category is a CLOSED profile by construction — the round + box families always loop (Rectangle / Circle / Ellipse / Polygon /
    //    Slot), and a Profile is a closed loop by definition. The open families (Line / Polyline / Arc / the free curves) are open UNLESS a later join /
    //    close raised ClosedEnabled, so the caller reads that flag first and only falls back to this for the always-closed set. A closed profile reads as
    //    a Wire dimension to the gate (a Wire that closes → the raising law makes a Solid); an open one reads as an Edge.
    bool CategoryAlwaysClosed(Frontier::ParametricSketchShapeCategory Category)
    {
        using C = Frontier::ParametricSketchShapeCategory;
        return Category == C::Rectangle || Category == C::Circle || Category == C::Ellipse ||
               Category == C::Polygon   || Category == C::Slot   || Category == C::Profile;
    }

    // 🔴 THE SELECTION → DIMENSION WIRE. Map the current selection + stratum to the ConstructionDimension the gate reads, and set the count / closure
    //    document fields the Sketch-Modify ops need to resolve Live. This is the one change that un-hides the whole Modify / raising half of the
    //    catalogue: EvaluateOperation returns DimensionMismatch (hidden) for every op whose DimensionMask excludes ActiveDimension, and the default
    //    document pins ActiveDimension = Nothing forever — so before this, only the Nothing-accepting creation ops ever survived the gate. The strata:
    //      • WholeShape, nothing selected     → Nothing (creation menu, the historical behaviour).
    //      • WholeShape, an OPEN curve         → Edge   (Trim / Extend / Offset / Fillet apply).
    //      • WholeShape, a CLOSED profile      → Wire   (+ ClosedProfileCondition, so a raising op predicts a Solid).
    //      • Vertex stratum, a vertex picked   → Vertex (Fillet / Chamfer round the corner).
    //      • Edge stratum, an edge picked       → Edge.
    //    SelectedCount is the picked-component count Fillet / Chamfer gate on (MinimumCount 2): a corner pick offers the two edges meeting there, so a
    //    single vertex / edge pick presents 2. TangentEndpointCondition (NeedMask bit 8 on Fillet / Chamfer) is the "a corner exists to round" fact —
    //    true exactly when a vertex / an interior edge is in hand — so the two ops read Live at a real corner and gated ("needs a shared tangent
    //    endpoint") otherwise, which is the truthful CAD state. Every other document field is left at the default the caller already seeded.
    CC::ConstructionDimension ResolveActiveDimension(const SketchModelSummonedState& State, bool& OutClosed, int& OutSelectedCount, bool& OutTangentCorner)
    {
        using D = CC::ConstructionDimension;
        OutClosed        = false;
        OutSelectedCount = 0;
        OutTangentCorner = false;

        const Frontier::ParametricSketchShapeStore& Store = State.ShapeStore;

        // 🔴 USER RULING: "if we have shapes (at least 1) you can enable the sketch operations." The Modify half of the catalogue is gated on
        //    a NON-EMPTY store, NOT on a pre-selection of a shape / edge / vertex — you pick the corner AFTER choosing Bevel/Chamfer, by going
        //    to it and dragging (the modal's Activate no-ops on a degenerate corner, so corner-existence is proven at pick time, not here). So
        //    an empty sketch stays Nothing (creation menu only), and the instant one shape exists the Modify ops read Live. The tangent + count
        //    fields Fillet/Chamfer need (NeedMask 0x0101 = Workplane + Tangent, MinimumCount 2) are forced true for the same reason: "remove any
        //    requirements" — the truthful "does a real corner exist" check moved to SolveCornerEdit at drag time.
        if (Store.Shapes.empty())
            return D::Nothing;

        OutSelectedCount = 2;      // ≥ the MinimumCount 2 Fillet/Chamfer gate on (the two legs a corner offers)
        OutTangentCorner = true;   // "a corner exists to round" is asserted at pick time, not gated at the menu

        // A sub-element pick (vertex / edge) latched into the shared slots is the most specific stratum, and still refines the reported
        // dimension when the user has one in hand — but it is no longer REQUIRED for the ops to appear.
        if (State.Stratum == SelectionStratum::Vertex && Store.AlignPickCategory == 1)
            return D::Vertex;
        if (State.Stratum == SelectionStratum::Edge && Store.AlignPickCategory == 2)
            return D::Edge;

        // Otherwise read the SELECTED shape's closure if one is selected, else fall back to the FIRST shape in the store (a corner still exists
        // to bevel even before an explicit whole-shape select). A const resolve — the gate feed must never mutate geometry.
        const Frontier::ParametricSketchShape* Shape = nullptr;
        if (Store.Selected != 0)
            for (const Frontier::ParametricSketchShape& Candidate : Store.Shapes)
                if (Candidate.Identifier == Store.Selected) { Shape = &Candidate; break; }
        if (Shape == nullptr)
            Shape = &Store.Shapes.front();

        const bool Closed = Shape->ClosedEnabled || CategoryAlwaysClosed(Shape->Category);
        if (Closed)
        {
            OutClosed = true;
            return D::Wire;
        }
        return D::Edge;
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

    // -- Q TOGGLES the console, the same way Tab toggles the directory card. --
    //    🔴 The context menu is bound to Q, NOT right-click: right-drag now drives the camera orbit (see the sketch panel), so a right press is a
    //       navigation gesture, not a request for the catalogue. Q now works LIKE TAB: press it to open the console, press it again to close it — a
    //       clean toggle. Crucially it works EVEN WHILE A TOOL IS ACTIVE, so you can Q → pick a different primitive without first cancelling the one
    //       you are holding: the console floats over the canvas and owns the press while open, and the tool latch is left intact so the previously-held
    //       tool resumes if you dismiss the console without picking a new one. Only the directory card blocks it (both can't stack over one canvas).
    const bool DirectorySummoned = State.Directory.SummonOpen;
    if (!DirectorySummoned && ImGui::IsKeyPressed(ImGuiKey_Q, false))
    {
        if (State.ConsoleOpen)
        {
            // Open → Q closes it (the toggle's off-stroke). Handled here, before the console's own outside-press dismissal runs below.
            State.ConsoleOpen = false;
        }
        else if (WithinSummonField(State, Io.MousePos))
        {
            // Closed → Q opens it at the cursor. LATCHED, not acted on: the console reads an outside press as a dismissal, so the placement is
            // applied at the tail (below) after the console has reported, else the same press would open and immediately dismiss it.
            State.ConsoleReanchorRequested = true;
        }
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

        // 🔴 THE SELECTION → DIMENSION FEED. Every other gate input was already seeded (workplane above, the rest at DefaultConstructionDocument); this
        //    is the one field the viewport must update each frame from the live selection, so the console's Sketch-Modify + raising ops appear / gate /
        //    hide against what is actually picked instead of staying hidden under the pinned ActiveDimension = Nothing. See ResolveActiveDimension for the
        //    full map. SelectedCount + ClosedProfileCondition + TangentEndpointCondition ride along so the count- and closure-gated ops (Fillet / Chamfer /
        //    Extrude) resolve to the truthful state; ProfileCount tracks a selected closed profile so a raising op that needs one is not spuriously gated.
        bool  SelectionClosed = false;
        int   SelectionCount  = 0;
        bool  TangentCorner   = false;
        State.Document.ActiveDimension         = ResolveActiveDimension(State, SelectionClosed, SelectionCount, TangentCorner);
        State.Document.SelectedCount           = SelectionCount;
        State.Document.ClosedProfileCondition  = SelectionClosed;
        State.Document.ProfileCount            = SelectionClosed ? 1 : 0;
        State.Document.TangentEndpointCondition = TangentCorner;

        CC::BindConstructionConsoleContext(State.Bridge, State.Document);
        const Frontier::WorkspaceContextConsoleDescriptor Descriptor = CC::ComposeConstructionConsoleDescriptor(State.Bridge);

        const Frontier::ConsoleResult Outcome = Frontier::ConstructWorkspaceContextConsole(
            Icons, Descriptor, ImVec2(State.ConsoleAnchorX, State.ConsoleAnchorY),
            State.ConsoleFocus, State.ConsoleCarousel, State.ConsoleReadings, Theme);

        if (Outcome.DismissRequested) { State.ConsoleOpen = false; }

        // 🔴 ANY commit that is not itself an Extrude ENDS a live sweep gesture first. This one guard stands in for a release line inside every arm
        //    branch below (draw / Select / corner-edit / command tool / Offset) and covers whatever branch is wired next, which matters more here than
        //    for the other modals: the extrude drag has the live height WRITTEN ONTO THE SHAPES, so a gesture abandoned silently would leave whatever
        //    partial height the pointer happened to be at baked in as if it had been confirmed. Abandon restores the arm-time snapshot instead.
        //    The Extrude branch is excluded only for tidiness — it abandons on its own before re-arming, and the verb is idempotent when idle.
        if (Outcome.CommitRequested && !CommittedActionIsExtrude(Outcome.ActivatedCluster, Outcome.ActivatedAction))
        {
            AbandonSketchModelExtrudeModal(State.ExtrudeModal, State.ShapeStore);
            State.ExtrudePickPending = false;
        }

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
        bool SketchCentreRect = false;
        if (Outcome.CommitRequested && CommittedActionIsSketchShape(Outcome.ActivatedCluster, Outcome.ActivatedAction, SketchCategory, SketchCentreRect))
        {
            // 🔴 LATCH the tool so it stays active: after each shape seals, the panel re-arms this same category for the next cycle (SustainSketchToolCycle),
            //    so the user draws rectangle after rectangle without reopening the menu. The latch is the cycle owner; Escape clears it to leave the tool.
            //    The centre-rect affordance (mirror the 2nd click about the 1st) rides the latch too, so it survives every re-arm of the cycle.
            LatchSketchTool(State.ToolLatch, SketchCategory);
            State.ToolLatch.CentreRect = SketchCentreRect;
            ArmShapeDraw(State.ShapeStore, SketchCategory);
            // A new draw tool ENDS any Fillet/Chamfer gesture and hides its redo box — the committed geometry stays, but the box no longer offers it.
            State.FilletModal        = SketchModelFilletModal{};
            State.FilletPickPending  = false;
            State.ConsoleOpen = false;
        }

        // 🔴 A committed Sketch-MODIFY op (Fillet / Chamfer / Trim / Extend / Offset) — the surface-only first pass. The op only ever reaches a commit
        //    because the selection → ActiveDimension wire above let the gate resolve it Live, so at this point a valid operand set is in hand. This
        //    branch NAMES the recognised op and REPORTS its operands (the resolved dimension, the selection count, and — when a sub-element pick is
        //    latched — the picked component's world-mm endpoints) but runs NO geometry kernel: the Fillet / Chamfer solver is the scoped follow-up. The
        //    log line is the visible proof the menu → gate → commit path is fully live end to end. The tool is NOT latched (unlike a Sketch primitive):
        //    a Modify op acts once on the current selection, so the console simply closes.
        const char* ModifyLabel = nullptr;
        if (Outcome.CommitRequested && CommittedActionIsSketchModify(Outcome.ActivatedCluster, Outcome.ActivatedAction, ModifyLabel))
        {
            if (ModifyLabel != nullptr && std::strcmp(ModifyLabel, "Select") == 0)
            {
                // 🔴 SELECT is the RELEASE op — the only Modify entry that arms nothing. Every other tool here claims the canvas click (a draw seats a
                //    point, a command tool applies a verb, a modal picks a corner / an edge), which is exactly what makes a plain shift-multi-select
                //    impossible while one is held. Select ends all of them and returns the canvas to the idle pick, so the clicks reach
                //    AdvanceShapeSelection and build the SelectionSet the boolean popup reconciles against. This is the "back to select mode" gesture.
                //
                //    Order matters: this branch precedes the corner-edit / command-tool / offset tests so no later arm can fire off the same commit.
                ClearSketchToolLatch(State.ToolLatch);
                State.ShapeStore.DrawingEnabled = false;
                State.ShapeStore.PendingPoints.clear();

                State.FilletModal       = SketchModelFilletModal{};
                State.FilletPickPending = false;
                State.InsetModal        = SketchModelInsetModal{};
                State.InsetPickPending  = false;
                ReleaseSketchCommandTool(State.CommandTools, State.ShapeStore);

                // 📝 The Stratum reading rides the commit: the menu's Shape / Vertex / Edge segmented option IS the viewport's 1/2/3 mode, so choosing
                //    Select also chooses what the next click catches. Index 0/1/2 map 1:1 onto SelectionStratum.
                const int Stratum = ResolveSketchSelectStratum(State.ConsoleReadings);
                State.Stratum = (Stratum == 1) ? SelectionStratum::Vertex
                              : (Stratum == 2) ? SelectionStratum::Edge
                                               : SelectionStratum::WholeShape;

                State.ConsoleOpen = false;
            }
            else if (CommittedActionIsCornerEdit(Outcome.ActivatedCluster, Outcome.ActivatedAction))
            {
                // 🔴 FILLET / CHAMFER arm the drag modal (the ported Plasticity `B` tool). A commit does NOT pick a corner yet — one tool, the drag sign
                //    later choosing fillet↔chamfer — so it only raises FilletPickPending. The console closes and the NEXT canvas click resolves the
                //    nearest corner vertex and Activates the modal on it (see AdvanceSketchFilletModal in the panel). Re-arming from a fresh commit is
                //    idempotent: any prior half-armed pick is cleared, and a still-armed modal from a previous op is reset before the new pick lands.
                //
                // 🔴 END the sticky DRAW cycle first. A Modify op is NOT a draw tool, but the last-committed Sketch primitive stays latched (that is the
                //    whole point of the sticky cycle), and SustainSketchToolCycle re-arms its DrawingEnabled every frame. If the latch survived, the
                //    corner-pick click below would ALSO seat a draw point and spawn another primitive. So clear the latch and drop any in-progress /
                //    re-armable stroke here — the corner click must reach the modal alone.
                ClearSketchToolLatch(State.ToolLatch);
                State.ShapeStore.DrawingEnabled = false;
                State.ShapeStore.PendingPoints.clear();

                State.FilletModal        = SketchModelFilletModal{};
                State.FilletPickPending  = true;
                State.ConsoleOpen        = false;
            }
            else if (CommittedActionCommandTool(Outcome.ActivatedCluster, Outcome.ActivatedAction) != SketchCommandTool::None)
            {
                // 🔴 TRIM / CUT / JOIN / REMOVE / EXTEND arm the sticky command-tool driver. Like the Fillet arm, a commit does NOT act yet — it arms the tool and
                //    closes the console; the NEXT canvas click hovers a target and applies the verb, and the tool stays armed for the next. End the sticky
                //    DRAW cycle first (the same trap the fillet arm fixes: a latched primitive would else seat a draw point on the command click), then arm.
                ClearSketchToolLatch(State.ToolLatch);
                State.FilletModal       = SketchModelFilletModal{};
                State.FilletPickPending = false;

                const SketchCommandTool Tool = CommittedActionCommandTool(Outcome.ActivatedCluster, Outcome.ActivatedAction);
                ArmSketchCommandTool(State.CommandTools, State.ShapeStore, Tool);
                State.ConsoleOpen = false;
            }
            else if (ModifyLabel != nullptr && std::strcmp(ModifyLabel, "Offset") == 0)
            {
                // 🔴 OFFSET arms the drag modal (the ported `I` tool) the SAME TWO-PHASE way as Fillet/Chamfer: a commit does NOT Activate the drag yet —
                //    it only raises InsetPickPending and closes the console. The NEXT canvas click over an edge / face resolves the hovered shape and
                //    Activates the modal on THAT shape; the radial drag then grows a signed distance with a live preview and a click/Enter commits
                //    (AppendOffsetResult). This makes "select the tool, then point at the edge / face and drag" work — the chamfer-tool feel the user wants
                //    — instead of a one-shot arm on whatever was pre-selected. End the sticky DRAW cycle first (a latched primitive would else seat a draw
                //    point on the pick click) and clear any Fillet / command residue, then raise the pending pick.
                ClearSketchToolLatch(State.ToolLatch);
                State.ShapeStore.DrawingEnabled = false;
                State.ShapeStore.PendingPoints.clear();
                State.FilletModal       = SketchModelFilletModal{};
                State.FilletPickPending = false;
                ReleaseSketchCommandTool(State.CommandTools, State.ShapeStore);

                State.InsetModal      = SketchModelInsetModal{};   // drop any prior arm / retained residue before the fresh pick
                State.InsetPickPending = true;
                State.ConsoleOpen      = false;
            }
            else
            {
                // The residual surface-only fallback: name the op + report its operands, run no kernel. Every recognised Sketch-Modify op now routes to a
                // live branch above — the corner-edit modal (Fillet / Chamfer), the five command tools (Trim / Cut / Join / Remove / Extend), and the
                // Offset drag modal — so this only fires for a NEW catalogue label nobody has wired yet.
                const Frontier::ParametricSketchShapeStore& Store = State.ShapeStore;
                if (Store.AlignPickCategory == 1 || Store.AlignPickCategory == 2)
                {
                    const char* const Kind = (Store.AlignPickCategory == 1) ? "vertex" : "edge";
                    std::printf("[SketchModify] %s on shape %u (%s pick: A=(%.1f, %.1f) B=(%.1f, %.1f) mm; %d operand(s))\n",
                                ModifyLabel, Store.Selected, Kind,
                                Store.AlignPickEndA.x, Store.AlignPickEndA.y,
                                Store.AlignPickEndB.x, Store.AlignPickEndB.y,
                                State.Document.SelectedCount);
                }
                else
                {
                    std::printf("[SketchModify] %s on shape %u (whole-shape; %d operand(s))\n",
                                ModifyLabel, Store.Selected, State.Document.SelectedCount);
                }
                std::fflush(stdout);
                State.ConsoleOpen = false;
            }
        }

        // 🔴 THE EXTRUDE COMMIT — arms the SWEEP DRAG, it does not extrude. This is the Blender `E` order: with a profile already selected the commit
        //    captures it at ZERO height and hands the pointer control of the height (drag = taller, click = confirmed, Esc = back to where it was), so
        //    the user never gets a finished solid they did not ask for. With NOTHING selected there is no operand, so it instead raises
        //    ExtrudePickPending — the two-phase form Fillet / Offset use — and the next canvas click over a profile both picks it and starts the drag
        //    there. Either way the height comes from the gesture; nothing here writes a depth.
        //
        //    (This REPLACES a one-shot that wrote the console's authored depth the instant the tile committed. That produced a small fixed prism with
        //     no control over it, which is exactly the behaviour being removed. The Depth field on the Sweep row is now only a hint — the drag, or a
        //     typed number during the drag, is the height. Symmetric is still read here and carried into the arm.)
        //
        //    The console's Direction reading is taken NOW rather than at confirm time because the console is closed for the whole gesture, so this is
        //    the last moment it can be read.
        if (Outcome.CommitRequested && CommittedActionIsExtrude(Outcome.ActivatedCluster, Outcome.ActivatedAction))
        {
            Frontier::ParametricSketchShapeStore& Store = State.ShapeStore;

            // End the sticky DRAW cycle first — the same trap the fillet / command / offset arms fix. A latched primitive would keep re-arming
            // DrawingEnabled and seat a stray point on the first click of the sweep gesture, which has nothing to do with the extrude.
            ClearSketchToolLatch(State.ToolLatch);
            Store.DrawingEnabled = false;
            Store.PendingPoints.clear();
            State.FilletModal       = SketchModelFilletModal{};
            State.FilletPickPending = false;
            State.InsetPickPending  = false;
            ReleaseSketchCommandTool(State.CommandTools, State.ShapeStore);

            const bool Symmetric = ResolveExtrudeSymmetricEnabled(State.ConsoleReadings);

            AbandonSketchModelExtrudeModal(State.ExtrudeModal, Store);   // a re-commit over a live gesture restores it first, never stacks on it

            const bool OperandSelected = !Store.SelectionSet.empty() || Store.Selected != 0;
            if (OperandSelected)
            {
                ActivateSketchModelExtrudeModal(State.ExtrudeModal, Store, Symmetric);
                State.ExtrudePickPending = State.ExtrudeModal.VacantArmReported;   // the selection resolved to nothing displayed → fall back to a pick
            }
            else
            {
                State.ExtrudePickPending = true;
            }
            State.ExtrudeSymmetricPending = Symmetric;
            State.ConsoleOpen             = false;
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
