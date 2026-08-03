/*==============================================================================================================================================
                                                    SKETCHMODELWORKPLANEOVERLAY.CPP
==============================================================================================================================================*/
// 🧩 Draws the authored construction planes over the viewport canvas as an ImGui DrawList overlay. Walks the summoned directory's outliner tree
//    for Workplane records, rebuilds a Frontier::Workplane from each record's RecordProfile, solves its Z-up world frame + tessellates the sheet
//    and grid, then projects each world point through the ONE viewport camera (world→view→clip→NDC→canvas pixel) and strokes the fill quad +
//    outline + lattice. The projection chain mirrors AssembleSketchModelGridConstants exactly (SolveOrbitOrientation + EvaluateProjectionFrame +
//    MultiplyMatrix), so a plane sits precisely where the analytic ground grid says its world point is. A point behind the eye (clip w <= 0) drops
//    the primitive it belongs to rather than smearing it across the screen.

#include "SketchModelWorkplaneOverlay.h"

#include "SketchModelViewportPanel.h"
#include "SketchModelGroundProjection.h"

#include "Workplane.h"

#include "SceneDirectoryInspectorPanel.h"
#include "InspectorContentProfile.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace SketchModelViewportValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        TREE WALK
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Rebuild a Frontier::Workplane from an inspector RecordProfile's authored definition + display cues (the WorkplaneExplainer split the
    //    profile carries). The solved frame is filled by SolveWorkplaneFrame afterward; this only ports the authored scalars across.
    Frontier::Workplane BuildWorkplaneFromProfile(const SceneDirectoryInspectorValidation::RecordProfile& Profile)
    {
        Frontier::Workplane Plane;

        // Method: the profile's PlaneMethod index is the same 0..9 order as WorkplaneConstructionCategory.
        int MethodIndex = Profile.PlaneMethod;
        if (MethodIndex < 0) MethodIndex = 0;
        if (MethodIndex > 9) MethodIndex = 9;
        Plane.Method = static_cast<Frontier::WorkplaneConstructionCategory>(MethodIndex);

        Plane.OffsetDistance    = Profile.PlaneOffset;
        Plane.AngleDegrees      = Profile.PlaneAngle;

        // The authored base origin (world mm) of a PLACED plane, so the solve seats it at the drawn location instead of world zero.
        Plane.AuthoredOriginX   = Profile.PlaneOriginX;
        Plane.AuthoredOriginY   = Profile.PlaneOriginY;
        Plane.AuthoredOriginZ   = Profile.PlaneOriginZ;

        // Angle hinge: the profile's PlaneAnglePivot index is the same 0..2 order as WorkplaneAnglePivot (U tilt / V tilt / Normal roll).
        int PivotIndex = Profile.PlaneAnglePivot;
        if (PivotIndex < 0) PivotIndex = 0;
        if (PivotIndex > 2) PivotIndex = 2;
        Plane.AnglePivot = static_cast<Frontier::WorkplaneAnglePivot>(PivotIndex);

        Plane.FlipNormalEnabled = Profile.FlipNormal;

        Plane.Extent      = Profile.PlaneExtent;
        Plane.GridEnabled = Profile.PlaneGrid;
        Plane.GridSpacing = Profile.PlaneGridSpacing;
        Plane.SnapEnabled = Profile.PlaneSnap;
        Plane.LockEnabled = Profile.PlaneLock;
        Plane.Displayed   = Profile.Visible;

        // 📝 The sheet tint: the profile carries appearance as Albedo[4] (RGBA 0..255), so scale it to 0..1 for the RGB and hold the sheet at a
        //    translucent wash (a solid sheet would occlude the model). Alpha is a fixed 0.16 wash regardless of the opaque albedo alpha.
        Plane.ColourRGBA[0] = (float)Profile.Albedo[0] / 255.0f;
        Plane.ColourRGBA[1] = (float)Profile.Albedo[1] / 255.0f;
        Plane.ColourRGBA[2] = (float)Profile.Albedo[2] / 255.0f;
        Plane.ColourRGBA[3] = 0.16f;

        return Plane;
    }

    // 📝 Walk the outliner tree region, and for every Workplane record (ClassificationId == (int)RecordClassification::Workplane) resolve its
    //    profile from the inspector side-table + append a solved Frontier::Workplane to OutStore. Recurses into every nested region.
    void CollectWorkplanes(const SceneDirectoryInspectorValidation::InspectorPanelState& Directory,
                           const std::vector<Frontier::SketchOutlinerUi::RecordEntry>&    Region,
                           Frontier::WorkplaneStore&                                      OutStore)
    {
        const int WorkplaneClass = static_cast<int>(SceneDirectoryInspectorValidation::RecordClassification::Workplane);

        for (const Frontier::SketchOutlinerUi::RecordEntry& Entry : Region)
        {
            if (Entry.ClassificationId == WorkplaneClass && !Entry.ConcealedState)
            {
                // The inspector seeds a profile lazily on first selection; an unvisited plane has no bag yet, so seed the defaults here so a
                // freshly-added plane still draws with the WorkplaneExplainer defaults (XY, 200 mm sheet, 10 mm grid).
                const SceneDirectoryInspectorValidation::RecordProfile* Profile = nullptr;
                for (const auto& Pair : Directory.Profiles)
                    if (Pair.first == Entry.Token) { Profile = &Pair.second; break; }

                SceneDirectoryInspectorValidation::RecordProfile Seeded;
                if (Profile == nullptr || !Profile->Populated)
                {
                    SceneDirectoryInspectorValidation::EstablishProfile(
                        SceneDirectoryInspectorValidation::RecordClassification::Workplane, 0, !Entry.ConcealedState, Seeded);
                    Profile = &Seeded;
                }

                Frontier::Workplane Plane = BuildWorkplaneFromProfile(*Profile);
                Plane.Identifier = Entry.Token;
                Frontier::SolveWorkplaneFrame(Plane);
                OutStore.Planes.push_back(Plane);
            }

            if (!Entry.NestedRegion.empty())
                CollectWorkplanes(Directory, Entry.NestedRegion, OutStore);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        OVERLAY
//------------------------------------------------------------------------------------------------------------------------

void RenderSketchModelWorkplaneOverlay(const SketchModelViewportState& State, ImVec2 CanvasOrigin, ImVec2 CanvasSize)
{
    if (CanvasSize.x < 1.0f || CanvasSize.y < 1.0f)
        return;

    // -- Gather the authored planes from the summoned directory tree --
    Frontier::WorkplaneStore Store;
    CollectWorkplanes(State.Summoned.Directory,
                      State.Summoned.Directory.Directory.RootRegion,
                      Store);
    if (Store.Planes.empty())
        return;

    // -- Tessellate each displayed plane into a sheet quad + on-plane lattice (world mm) --
    std::vector<Frontier::WorkplaneBody> Bodies;
    Frontier::AssembleWorkplaneBodies(Store, Bodies);
    if (Bodies.empty())
        return;

    // -- The world→clip matrix: exactly the chain AssembleSketchModelGridConstants derives its grid constants from (shared projection unit) --
    const Frontier::Matrix4f ViewProjection = AssembleGroundViewProjection(State);

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    Draw->PushClipRect(CanvasOrigin, ImVec2(CanvasOrigin.x + CanvasSize.x, CanvasOrigin.y + CanvasSize.y), true);

    for (const Frontier::WorkplaneBody& Body : Bodies)
    {
        // Sheet tint + a slightly-brighter outline / lattice stroke keyed off the same hue.
        const ImU32 FillColour = ImGui::GetColorU32(ImVec4(Body.ColourRGBA[0], Body.ColourRGBA[1], Body.ColourRGBA[2], Body.ColourRGBA[3]));
        const ImU32 EdgeColour = ImGui::GetColorU32(ImVec4(Body.ColourRGBA[0], Body.ColourRGBA[1], Body.ColourRGBA[2], 0.85f));
        const ImU32 GridColour = ImGui::GetColorU32(ImVec4(Body.ColourRGBA[0], Body.ColourRGBA[1], Body.ColourRGBA[2], 0.35f));

        // -- The sheet fill + outline: project the four corners; skip the whole sheet if any corner is behind the eye --
        ProjectedPoint Corners[4];
        bool AllInFront = true;
        for (int CornerIndex = 0; CornerIndex < 4; ++CornerIndex)
        {
            Corners[CornerIndex] = ProjectWorldPoint(ViewProjection, CanvasOrigin, CanvasSize,
                                                     Body.Quad[CornerIndex * 3 + 0],
                                                     Body.Quad[CornerIndex * 3 + 1],
                                                     Body.Quad[CornerIndex * 3 + 2]);
            AllInFront = AllInFront && Corners[CornerIndex].InFront;
        }

        if (AllInFront)
        {
            const ImVec2 Poly[4] = { Corners[0].Pixel, Corners[1].Pixel, Corners[2].Pixel, Corners[3].Pixel };
            Draw->AddConvexPolyFilled(Poly, 4, FillColour);
            Draw->AddPolyline(Poly, 4, EdgeColour, ImDrawFlags_Closed, 1.6f);

            // -- The on-plane lattice: each segment is six floats (Ax,Ay,Az, Bx,By,Bz). Draw only both-in-front segments --
            const size_t SegmentCount = Body.GridLines.size() / 6;
            for (size_t Segment = 0; Segment < SegmentCount; ++Segment)
            {
                const size_t Base = Segment * 6;
                const ProjectedPoint A = ProjectWorldPoint(ViewProjection, CanvasOrigin, CanvasSize,
                                                           Body.GridLines[Base + 0], Body.GridLines[Base + 1], Body.GridLines[Base + 2]);
                const ProjectedPoint B = ProjectWorldPoint(ViewProjection, CanvasOrigin, CanvasSize,
                                                           Body.GridLines[Base + 3], Body.GridLines[Base + 4], Body.GridLines[Base + 5]);
                if (A.InFront && B.InFront)
                    Draw->AddLine(A.Pixel, B.Pixel, GridColour, 1.0f);
            }
        }
    }

    Draw->PopClipRect();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     INTERACTIVE DRAW
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 The cursor→ground pick + world→pixel projection used below live in SketchModelGroundProjection (CastCursorToGroundMillimetres,
    //    ProjectGroundMillimetres, ProjectWorldPoint, ProjectedPoint) so this overlay and the primitive-draw unit share ONE chain.

    // 📝 Inject a committed Workplane into the outliner tree + profile side-table, sized to the swept ground rectangle (world mm). The sheet is
    //    centred on the drag midpoint (AuthoredOrigin) with a half-extent of half the LARGER swept side, so the drawn box is fully covered. A
    //    fresh token is issued off the outliner's own issuer; the profile is seeded to the Workplane defaults, then overwritten with the drawn
    //    placement + size (real mm, no scale slider). Returns the new token.
    SceneDirectoryInspectorValidation::RecordToken CommitDrawnWorkplane(
        SceneDirectoryInspectorValidation::InspectorPanelState& Directory,
        float CornerAX, float CornerAY, float CornerBX, float CornerBY)
    {
        namespace SDI = SceneDirectoryInspectorValidation;
        namespace SO  = Frontier::SketchOutlinerUi;

        const float CentreX = (CornerAX + CornerBX) * 0.5f;
        const float CentreY = (CornerAY + CornerBY) * 0.5f;
        const float SpanX   = std::fabs(CornerBX - CornerAX);
        const float SpanY   = std::fabs(CornerBY - CornerAY);
        // Half-extent = half the larger side; clamp to a sane minimum so a stray click still yields a visible sheet.
        float HalfExtent = 0.5f * ((SpanX > SpanY) ? SpanX : SpanY);
        if (HalfExtent < 100.0f) HalfExtent = 100.0f;   // 100 mm floor (a 0.2 m sheet), never a zero-area plane

        // -- Copy the Workplane classification's icon key + tint from the content profile so the row matches an add-menu creation exactly. --
        const SO::OutlinerContentProfile& Profile = SDI::ResolveInspectorContentProfile();
        const int WorkplaneId = static_cast<int>(SDI::RecordClassification::Workplane);
        const char*   IconKey = "workplane";
        std::uint32_t Tint    = SDI::ClassificationHue(SDI::RecordClassification::Workplane);
        for (int Index = 0; Index < Profile.ClassRowCount; ++Index)
            if (Profile.ClassRows[Index].ClassificationId == WorkplaneId)
            {
                IconKey = Profile.ClassRows[Index].IconKey;
                Tint    = Profile.ClassRows[Index].Tint;
                break;
            }

        // -- Issue a token + append the row at the tree root. --
        SO::SketchOutlinerState& Tree  = Directory.Directory;
        const SDI::RecordToken   Token = Tree.NextToken++;

        SO::RecordEntry Entry;
        Entry.Token            = Token;
        Entry.ClassificationId = WorkplaneId;
        Entry.IconKey          = IconKey;
        Entry.TintColor        = Tint;
        Entry.ExpandedState    = false;
        Entry.ConcealedState   = false;

        char Name[64];
        std::snprintf(Name, sizeof(Name), "Workplane %u", static_cast<unsigned>(Token));
        Entry.Label = Name;

        Tree.RootRegion.push_back(std::move(Entry));

        // -- Seed + author the profile: Offset method (keeps the sheet flat on the ground at the placed origin), drawn extent + origin. --
        SDI::RecordProfile& Bag = SDI::ProfileFor(Directory, Token);
        SDI::EstablishProfile(SDI::RecordClassification::Workplane, 0, true, Bag);
        Bag.PlaneMethod  = 0;              // XY — flat on the ground; the origin carries the placement
        Bag.PlaneExtent  = HalfExtent;     // real half-size from the drag (mm), NOT a slider default
        Bag.PlaneOriginX = CentreX;
        Bag.PlaneOriginY = CentreY;
        Bag.PlaneOriginZ = 0.0f;
        Bag.Visible      = true;

        return Token;
    }
}

void ArmWorkplaneDraw(WorkplaneDrawState& Draw)
{
    Draw.Phase        = WorkplaneDrawPhase::Armed;
    Draw.CornerBValid = false;
}

bool WorkplaneDrawActive(const WorkplaneDrawState& Draw)
{
    return Draw.Phase != WorkplaneDrawPhase::Idle;
}

void AdvanceWorkplaneDraw(const SketchModelViewportState&                        State,
                          SceneDirectoryInspectorValidation::InspectorPanelState& Directory,
                          WorkplaneDrawState&                                     Draw,
                          ImVec2 CanvasOrigin, ImVec2 CanvasSize)
{
    if (Draw.Phase == WorkplaneDrawPhase::Idle)
        return;
    if (CanvasSize.x < 1.0f || CanvasSize.y < 1.0f)
        return;

    const ImGuiIO& Io = ImGui::GetIO();

    // -- Cancel: Escape OR a right-click abandons the draw with nothing added. --
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        Draw.Phase        = WorkplaneDrawPhase::Idle;
        Draw.CornerBValid = false;
        return;
    }

    // -- Where the cursor meets the ground this frame (world mm). Only act while the pointer is over the canvas rect. --
    const bool OverCanvas =
        Io.MousePos.x >= CanvasOrigin.x && Io.MousePos.x <= CanvasOrigin.x + CanvasSize.x &&
        Io.MousePos.y >= CanvasOrigin.y && Io.MousePos.y <= CanvasOrigin.y + CanvasSize.y;

    float GroundMmX = 0.0f, GroundMmY = 0.0f;
    const bool GroundHit = OverCanvas &&
        CastCursorToGroundMillimetres(State, CanvasOrigin, CanvasSize, Io.MousePos, GroundMmX, GroundMmY);

    ImDrawList* Draw2D = ImGui::GetWindowDrawList();
    Draw2D->PushClipRect(CanvasOrigin, ImVec2(CanvasOrigin.x + CanvasSize.x, CanvasOrigin.y + CanvasSize.y), true);

    const ImU32 GuideInk  = ImGui::GetColorU32(ImVec4(0.36f, 0.62f, 1.0f, 0.95f));   // the XY blue the sheet uses
    const ImU32 GuideFill = ImGui::GetColorU32(ImVec4(0.36f, 0.62f, 1.0f, 0.16f));

    if (Draw.Phase == WorkplaneDrawPhase::Armed)
    {
        // A crosshair follows the cursor over the ground; the first click seats corner A and begins the sweep.
        if (GroundHit)
        {
            Draw.CornerBX = GroundMmX; Draw.CornerBY = GroundMmY; Draw.CornerBValid = true;
            const ProjectedPoint P = ProjectGroundMillimetres(State, CanvasOrigin, CanvasSize, GroundMmX, GroundMmY);
            if (P.InFront)
            {
                Draw2D->AddLine(ImVec2(P.Pixel.x - 9.0f, P.Pixel.y), ImVec2(P.Pixel.x + 9.0f, P.Pixel.y), GuideInk, 1.4f);
                Draw2D->AddLine(ImVec2(P.Pixel.x, P.Pixel.y - 9.0f), ImVec2(P.Pixel.x, P.Pixel.y + 9.0f), GuideInk, 1.4f);
            }

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                Draw.CornerAX = GroundMmX; Draw.CornerAY = GroundMmY;
                Draw.Phase = WorkplaneDrawPhase::Sweeping;
            }
        }
    }
    else if (Draw.Phase == WorkplaneDrawPhase::Sweeping)
    {
        // Track corner B under the cursor; hold the last valid corner while the ray grazes off the ground.
        if (GroundHit) { Draw.CornerBX = GroundMmX; Draw.CornerBY = GroundMmY; Draw.CornerBValid = true; }

        // Live rubber-band rectangle: the four ground corners of the A→B box, projected + stroked.
        const float RectX0 = Draw.CornerAX, RectY0 = Draw.CornerAY, RectX1 = Draw.CornerBX, RectY1 = Draw.CornerBY;
        const float BoxX[4] = { RectX0, RectX1, RectX1, RectX0 };
        const float BoxY[4] = { RectY0, RectY0, RectY1, RectY1 };
        ProjectedPoint Corner[4];
        bool AllInFront = true;
        for (int Index = 0; Index < 4; ++Index)
        {
            Corner[Index] = ProjectGroundMillimetres(State, CanvasOrigin, CanvasSize, BoxX[Index], BoxY[Index]);
            AllInFront = AllInFront && Corner[Index].InFront;
        }
        if (AllInFront)
        {
            const ImVec2 Poly[4] = { Corner[0].Pixel, Corner[1].Pixel, Corner[2].Pixel, Corner[3].Pixel };
            Draw2D->AddConvexPolyFilled(Poly, 4, GuideFill);
            Draw2D->AddPolyline(Poly, 4, GuideInk, ImDrawFlags_Closed, 1.8f);

            // A live dimension readout at the cursor: the real swept span in mm (proper units, no slider).
            const float SpanX = std::fabs(RectX1 - RectX0);
            const float SpanY = std::fabs(RectY1 - RectY0);
            char Readout[64];
            std::snprintf(Readout, sizeof(Readout), "%.0f x %.0f mm", SpanX, SpanY);
            const ImVec2 Label(Io.MousePos.x + 14.0f, Io.MousePos.y + 8.0f);
            Draw2D->AddText(ImVec2(Label.x + 1.0f, Label.y + 1.0f), IM_COL32(0, 0, 0, 200), Readout);
            Draw2D->AddText(Label, IM_COL32(255, 255, 255, 235), Readout);
        }

        // The second click confirms: commit the plane to the outliner + profile, then return to idle.
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && Draw.CornerBValid)
        {
            CommitDrawnWorkplane(Directory, Draw.CornerAX, Draw.CornerAY, Draw.CornerBX, Draw.CornerBY);
            Draw.Phase        = WorkplaneDrawPhase::Idle;
            Draw.CornerBValid = false;
        }
    }

    Draw2D->PopClipRect();
}

}   // namespace SketchModelViewportValidation
