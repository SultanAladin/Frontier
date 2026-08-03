/*==============================================================================================================================================
                                                        SKETCHMODELSHAPEDRAW.CPP
==============================================================================================================================================*/
// 🧩 The interactive click-to-draw body for the 2D sketch primitives. Drives the store's DrawingCategory / PendingPoints / RubberEnd fields,
//    previews the shape-in-progress with the SAME analytic solver the seal uses (ConstructParametricSketchShape → EvaluateShapePolyline), and
//    seals via AppendParametricSketchShape (which records the history entry). Ground picking + world→pixel projection come from the shared
//    SketchModelGroundProjection unit so this draw and the workplane overlay agree on placement. The store owns the geometry + history; this file
//    owns only the gesture.

#include "SketchModelShapeDraw.h"

#include "SketchModelViewportPanel.h"
#include "SketchModelGroundProjection.h"

#include "SceneDirectoryInspectorPanel.h"
#include "InspectorContentProfile.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace SketchModelViewportValidation
{

namespace
{
    // The XY-plane blue the primitives + rubber band stroke (matches the workplane sheet's guide ink for a consistent draw language).
    ImU32 GuideInk()   { return ImGui::GetColorU32(ImVec4(0.36f, 0.62f, 1.0f, 0.95f)); }
    ImU32 GuideFill()  { return ImGui::GetColorU32(ImVec4(0.36f, 0.62f, 1.0f, 0.16f)); }
    ImU32 PointInk()   { return ImGui::GetColorU32(ImVec4(1.0f, 0.86f, 0.35f, 1.0f)); }   // a seated defining point (amber pip)

    // Whether a category's flattened outline closes back on itself (fill + closing edge). Mirrors the store's closed families.
    bool CategoryCloses(Frontier::ParametricSketchShapeCategory Category)
    {
        return Category == Frontier::ParametricSketchShapeCategory::Rectangle ||
               Category == Frontier::ParametricSketchShapeCategory::Circle    ||
               Category == Frontier::ParametricSketchShapeCategory::Ellipse   ||
               Category == Frontier::ParametricSketchShapeCategory::Polygon   ||
               Category == Frontier::ParametricSketchShapeCategory::Slot;
    }

    // Project + stroke a world-mm polyline (the analytic preview or a seated-point run) through the shared forward map. Closed adds the fill +
    // the closing edge. Drops the whole polyline if any vertex is behind the eye (a partial projection would smear across the screen).
    void StrokeGroundPolyline(const SketchModelViewportState& State, ImVec2 CanvasOrigin, ImVec2 CanvasSize,
                              const std::vector<ImVec2>& Polyline, bool Closed, ImDrawList* Draw)
    {
        if (Polyline.size() < 2)
            return;

        const Frontier::Matrix4f ViewProjection = AssembleGroundViewProjection(State);

        std::vector<ImVec2> Pixels;
        Pixels.reserve(Polyline.size());
        for (const ImVec2& Point : Polyline)
        {
            const ProjectedPoint P = ProjectWorldPoint(ViewProjection, CanvasOrigin, CanvasSize, Point.x, Point.y, 0.0f);
            if (!P.InFront)
                return;                       // any behind-eye vertex drops the whole preview this frame
            Pixels.push_back(P.Pixel);
        }

        if (Closed && Pixels.size() >= 3)
            Draw->AddConvexPolyFilled(Pixels.data(), (int)Pixels.size(), GuideFill());
        Draw->AddPolyline(Pixels.data(), (int)Pixels.size(), GuideInk(), Closed ? ImDrawFlags_Closed : ImDrawFlags_None, 1.8f);
    }

    // The live dimension readout at the cursor (proper mm, no scale slider): the primary span of the shape-in-progress.
    void DrawReadout(ImDrawList* Draw, ImVec2 Anchor, const char* Text)
    {
        const ImVec2 Label(Anchor.x + 14.0f, Anchor.y + 8.0f);
        Draw->AddText(ImVec2(Label.x + 1.0f, Label.y + 1.0f), IM_COL32(0, 0, 0, 200), Text);
        Draw->AddText(Label, IM_COL32(255, 255, 255, 235), Text);
    }

    // 📝 Copy a classification's authored icon key + tint out of the content profile (so a mirrored row looks exactly like an add-menu creation of
    //    that class), falling back to the passed defaults when the profile has no row for it.
    void ResolveClassAppearance(int ClassificationId, const char* FallbackIcon, std::uint32_t FallbackTint,
                                std::string& OutIcon, std::uint32_t& OutTint)
    {
        namespace SDI = SceneDirectoryInspectorValidation;
        const Frontier::SketchOutlinerUi::OutlinerContentProfile& Profile = SDI::ResolveInspectorContentProfile();
        OutIcon = FallbackIcon;
        OutTint = FallbackTint;
        for (int Index = 0; Index < Profile.ClassRowCount; ++Index)
            if (Profile.ClassRows[Index].ClassificationId == ClassificationId)
            {
                OutIcon = Profile.ClassRows[Index].IconKey;
                OutTint = Profile.ClassRows[Index].Tint;
                break;
            }
    }

    // 📝 Resolve WHERE a freshly-drawn sketch row is parented, honouring the user's rule: if the tree already holds a construction workplane, the
    //    sketch nests under the MOST RECENT top-level Workplane row (drawn "within" it); otherwise it nests under a single shared "Sketches" folder,
    //    created once and reused. Returns the destination region + expands the container so the new row is visible. Never parents at the bare root:
    //    a loose sketch and a workplane-owned sketch would then read the same, losing the parent relationship the user asked for.
    std::vector<Frontier::SketchOutlinerUi::RecordEntry>& ResolveSketchParentRegion(
        SceneDirectoryInspectorValidation::InspectorPanelState& Directory)
    {
        namespace SDI = SceneDirectoryInspectorValidation;
        namespace SO  = Frontier::SketchOutlinerUi;

        SO::SketchOutlinerState& Tree = Directory.Directory;
        const int WorkplaneId = static_cast<int>(SDI::RecordClassification::Workplane);
        const int FolderId    = static_cast<int>(SDI::RecordClassification::Folder);

        // -- A workplane present → nest under the LAST top-level workplane row (the most recently drawn), and open it. --
        for (int Index = static_cast<int>(Tree.RootRegion.size()) - 1; Index >= 0; --Index)
            if (Tree.RootRegion[Index].ClassificationId == WorkplaneId && !Tree.RootRegion[Index].ConcealedState)
            {
                Tree.RootRegion[Index].ExpandedState = true;
                return Tree.RootRegion[Index].NestedRegion;
            }

        // -- No workplane → reuse the shared "Sketches" folder (a top-level Folder row we own), or create it once. --
        for (SO::RecordEntry& Row : Tree.RootRegion)
            if (Row.ClassificationId == FolderId && Row.Label == "Sketches")
            {
                Row.ExpandedState = true;
                return Row.NestedRegion;
            }

        SO::RecordEntry Folder;
        Folder.Token            = Tree.NextToken++;
        Folder.ClassificationId = FolderId;
        ResolveClassAppearance(FolderId, "folder", SDI::ClassificationHue(SDI::RecordClassification::Folder),
                               Folder.IconKey, Folder.TintColor);
        Folder.ExpandedState  = true;
        Folder.ConcealedState = false;
        Folder.Label          = "Sketches";
        SDI::RecordProfile& FolderBag = SDI::ProfileFor(Directory, Folder.Token);
        SDI::EstablishProfile(SDI::RecordClassification::Folder, 0, true, FolderBag);
        Tree.RootRegion.push_back(std::move(Folder));
        return Tree.RootRegion.back().NestedRegion;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void RenderSketchModelShapes(const SketchModelViewportState&       State,
                             Frontier::ParametricSketchShapeStore& Store,
                             ImVec2 CanvasOrigin, ImVec2 CanvasSize)
{
    if (CanvasSize.x < 1.0f || CanvasSize.y < 1.0f || Store.Shapes.empty())
        return;

    const Frontier::Matrix4f ViewProjection = AssembleGroundViewProjection(State);

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    Draw->PushClipRect(CanvasOrigin, ImVec2(CanvasOrigin.x + CanvasSize.x, CanvasOrigin.y + CanvasSize.y), true);

    // A committed shape strokes in a warm off-white so it reads as PLACED geometry (distinct from the blue draw-in-progress guide ink).
    const ImU32 CommitInk  = ImGui::GetColorU32(ImVec4(0.92f, 0.92f, 0.96f, 0.95f));
    const ImU32 CommitFill = ImGui::GetColorU32(ImVec4(0.92f, 0.92f, 0.96f, 0.10f));

    for (Frontier::ParametricSketchShape& Shape : Store.Shapes)
    {
        if (!Shape.Displayed)
            continue;

        const std::vector<ImVec2>& Outline = Frontier::RetrieveCachedOutline(Shape);   // warms the flatten cache (non-const store)
        if (Outline.size() < 2)
            continue;

        std::vector<ImVec2> Pixels;
        Pixels.reserve(Outline.size());
        bool AllInFront = true;
        for (const ImVec2& Point : Outline)
        {
            const ProjectedPoint P = ProjectWorldPoint(ViewProjection, CanvasOrigin, CanvasSize, Point.x, Point.y, 0.0f);
            if (!P.InFront) { AllInFront = false; break; }
            Pixels.push_back(P.Pixel);
        }
        if (!AllInFront)
            continue;

        const bool Closed = Shape.ClosedEnabled;
        if (Closed && Shape.FillEnabled && Pixels.size() >= 3)
            Draw->AddConvexPolyFilled(Pixels.data(), (int)Pixels.size(), CommitFill);
        Draw->AddPolyline(Pixels.data(), (int)Pixels.size(), CommitInk, Closed ? ImDrawFlags_Closed : ImDrawFlags_None, 1.7f);
    }

    Draw->PopClipRect();
}


void MirrorSketchShapeIntoDirectory(Frontier::ParametricSketchShapeStore&                   Store,
                                    SceneDirectoryInspectorValidation::InspectorPanelState& Directory,
                                    uint32_t                                                ShapeId)
{
    namespace SDI = SceneDirectoryInspectorValidation;
    namespace SO  = Frontier::SketchOutlinerUi;

    const Frontier::ParametricSketchShape* Shape = Frontier::ResolveParametricSketchShape(Store, ShapeId);
    if (Shape == nullptr)
        return;

    // -- Copy the Sketch classification's icon key + tint from the content profile so the row matches an add-menu creation exactly. --
    const int     SketchId = static_cast<int>(SDI::RecordClassification::Sketch);
    std::string   IconKey;
    std::uint32_t Tint = 0;
    ResolveClassAppearance(SketchId, "sketch", SDI::ClassificationHue(SDI::RecordClassification::Sketch), IconKey, Tint);

    // 🔴 Resolve the parent FIRST (it may create + push the "Sketches" folder, growing RootRegion), THEN issue the sketch row's token and append —
    //    the folder's own token is issued inside the resolve, so taking the sketch token before would not clash but reading a region reference across
    //    a later push_back would dangle. Order: resolve region (all mutation to RootRegion done) → issue token → append into the returned region.
    SO::SketchOutlinerState&                  Tree        = Directory.Directory;
    std::vector<SO::RecordEntry>&             ParentRegion = ResolveSketchParentRegion(Directory);
    const SDI::RecordToken                    Token        = Tree.NextToken++;

    SO::RecordEntry Entry;
    Entry.Token            = Token;
    Entry.ClassificationId = SketchId;
    Entry.IconKey          = IconKey;
    Entry.TintColor        = Tint;
    Entry.ExpandedState    = false;
    Entry.ConcealedState   = false;
    Entry.Label            = Shape->Title;   // "Rectangle 1", "Circle 2", … — the store already seeded it
    ParentRegion.push_back(std::move(Entry));

    // -- Seed a Sketch profile so the Properties card has a bag to read (the store still owns the true analytic geometry). --
    SDI::RecordProfile& Bag = SDI::ProfileFor(Directory, Token);
    SDI::EstablishProfile(SDI::RecordClassification::Sketch, 0, true, Bag);

    // -- Record one History-panel revision. The store's newest EditLog entry carries the reader detail ("Points=…;Length=… mm"); reflect it. --
    const char* Subtitle = "";
    if (!Store.EditLog.empty())
        Subtitle = Store.EditLog.back().Detail;

    // A synthetic "HH:MM" stamp (the headless validation build has no wall clock): a monotonic minute past the seed tip, so edits read as later.
    static int Minute = 20;
    char TimeText[8];
    std::snprintf(TimeText, sizeof(TimeText), "%02d:%02d", 9 + (Minute / 60), Minute % 60);
    ++Minute;

    char Title[64];
    std::snprintf(Title, sizeof(Title), "Added %s", Shape->Title);
    SDI::RecordRevision(Directory.Revisions, SDI::RevisionCategory::Sketch, Title, Subtitle, TimeText);
}


void ArmShapeDraw(Frontier::ParametricSketchShapeStore& Store, Frontier::ParametricSketchShapeCategory Category)
{
    Store.DrawingEnabled  = true;
    Store.DrawingCategory = Category;
    Store.PendingPoints.clear();
    Store.RubberEnd = ImVec2(0, 0);
}


bool ShapeDrawActive(const Frontier::ParametricSketchShapeStore& Store)
{
    return Store.DrawingEnabled;
}


uint32_t AdvanceShapeDraw(const SketchModelViewportState&                         State,
                          Frontier::ParametricSketchShapeStore&                   Store,
                          SceneDirectoryInspectorValidation::InspectorPanelState& Directory,
                          ImVec2 CanvasOrigin, ImVec2 CanvasSize, float WheelNotches)
{
    (void)Directory;   // threaded for a later mirror step; the seal writes only the store here
    if (!Store.DrawingEnabled)
        return 0;
    if (CanvasSize.x < 1.0f || CanvasSize.y < 1.0f)
        return 0;

    const ImGuiIO& Io = ImGui::GetIO();

    // -- Cancel: Escape abandons the draw with nothing added. (Right-click is no longer a cancel — it now drives the camera orbit.) --
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        Store.DrawingEnabled = false;
        Store.PendingPoints.clear();
        return 0;
    }

    const Frontier::ParametricSketchShapeCategory Category = Store.DrawingCategory;
    const int Required = Frontier::ResolveParametricSketchDefiningCount(Category);   // 0 = open-ended (not wired this cut)

    // -- Where the cursor meets the ground this frame (authored mm). Only act while the pointer is over the canvas rect. --
    const bool OverCanvas =
        Io.MousePos.x >= CanvasOrigin.x && Io.MousePos.x <= CanvasOrigin.x + CanvasSize.x &&
        Io.MousePos.y >= CanvasOrigin.y && Io.MousePos.y <= CanvasOrigin.y + CanvasSize.y;

    float GroundMmX = 0.0f, GroundMmY = 0.0f;
    const bool GroundHit = OverCanvas &&
        CastCursorToGroundMillimetres(State, CanvasOrigin, CanvasSize, Io.MousePos, GroundMmX, GroundMmY);
    if (GroundHit)
        Store.RubberEnd = ImVec2(GroundMmX, GroundMmY);

    // -- Wheel adjusts the live polygon side count mid-draw (>= 3), matching the retired draw's affordance. WheelNotches comes from the panel's
    //    global wheel guard (HoldWheelFromCamera), which already zeroed Io.MouseWheel so the same notch never ALSO dollied the camera. --
    if (Category == Frontier::ParametricSketchShapeCategory::Polygon && WheelNotches != 0.0f)
    {
        Store.PendingSideCount += (WheelNotches > 0.0f) ? 1 : -1;
        if (Store.PendingSideCount < 3)   Store.PendingSideCount = 3;
        if (Store.PendingSideCount > 64)  Store.PendingSideCount = 64;
    }

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    Draw->PushClipRect(CanvasOrigin, ImVec2(CanvasOrigin.x + CanvasSize.x, CanvasOrigin.y + CanvasSize.y), true);

    uint32_t Sealed = 0;

    // -- Assemble the defining points so far PLUS the live cursor point, for both the preview and (on a click) the seat/seal. --
    std::vector<ImVec2> Defining = Store.PendingPoints;
    if (GroundHit)
        Defining.push_back(Store.RubberEnd);

    // -- Analytic preview: as soon as enough points exist, flatten the shape-in-progress exactly as the seal will, and stroke it. --
    if ((int)Defining.size() >= Required && Required > 0)
    {
        const int SideOverride = (Category == Frontier::ParametricSketchShapeCategory::Polygon) ? Store.PendingSideCount : 0;
        Frontier::ParametricSketchShape Preview = Frontier::ConstructParametricSketchShape(Category, Defining, SideOverride);
        std::vector<ImVec2> Outline;
        Frontier::EvaluateShapePolyline(Preview, Outline);
        StrokeGroundPolyline(State, CanvasOrigin, CanvasSize, Outline, CategoryCloses(Category), Draw);

        // A live primary-dimension readout at the cursor (mm).
        const char* PrimaryLabel = nullptr;
        const float Primary = Frontier::ResolvePrimaryDimension(Preview, &PrimaryLabel);
        char Readout[80];
        if (Category == Frontier::ParametricSketchShapeCategory::Polygon)
            std::snprintf(Readout, sizeof(Readout), "%s %.0f mm  (%d sides)", PrimaryLabel ? PrimaryLabel : "", Primary, Store.PendingSideCount);
        else
            std::snprintf(Readout, sizeof(Readout), "%s %.0f mm", PrimaryLabel ? PrimaryLabel : "", Primary);
        DrawReadout(Draw, Io.MousePos, Readout);
    }
    else
    {
        // Not enough points yet: rubber-band a guideline from the last seated point to the cursor, and pip the seated points.
        if (!Store.PendingPoints.empty() && GroundHit)
        {
            std::vector<ImVec2> Guide = { Store.PendingPoints.back(), Store.RubberEnd };
            StrokeGroundPolyline(State, CanvasOrigin, CanvasSize, Guide, false, Draw);
        }
        // A crosshair on the cursor over the ground while seating the first point.
        if (GroundHit)
        {
            const ProjectedPoint P = ProjectGroundMillimetres(State, CanvasOrigin, CanvasSize, Store.RubberEnd.x, Store.RubberEnd.y);
            if (P.InFront)
            {
                Draw->AddLine(ImVec2(P.Pixel.x - 9.0f, P.Pixel.y), ImVec2(P.Pixel.x + 9.0f, P.Pixel.y), GuideInk(), 1.4f);
                Draw->AddLine(ImVec2(P.Pixel.x, P.Pixel.y - 9.0f), ImVec2(P.Pixel.x, P.Pixel.y + 9.0f), GuideInk(), 1.4f);
            }
        }
    }

    // -- Pip every seated defining point so the user sees the run building. --
    for (const ImVec2& Seated : Store.PendingPoints)
    {
        const ProjectedPoint P = ProjectGroundMillimetres(State, CanvasOrigin, CanvasSize, Seated.x, Seated.y);
        if (P.InFront)
            Draw->AddCircleFilled(P.Pixel, 3.2f, PointInk());
    }

    // -- A left click over the ground seats the cursor point; if that completes the category, seal the analytic shape into the store. --
    if (GroundHit && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        Store.PendingPoints.push_back(Store.RubberEnd);

        if (Required > 0 && (int)Store.PendingPoints.size() >= Required)
        {
            const int SideOverride = (Category == Frontier::ParametricSketchShapeCategory::Polygon) ? Store.PendingSideCount : 0;
            Sealed = Frontier::AppendParametricSketchShape(Store, Category, Store.PendingPoints, SideOverride);
            Store.PendingPoints.clear();
            Store.DrawingEnabled = false;   // one shape per arm (the console re-arms for the next); mirrors the workplane draw
        }
    }

    Draw->PopClipRect();
    return Sealed;
}

}   // namespace SketchModelViewportValidation
