/*==============================================================================================================================================
                                                        SKETCHMODELSHAPEDRAW.CPP
==============================================================================================================================================*/
// 🧩 The interactive click-to-draw body for the 2D sketch primitives. Drives the store's DrawingCategory / PendingPoints / RubberEnd fields,
//    previews the shape-in-progress with the SAME analytic solver the seal uses (ConstructParametricSketchShape → EvaluateShapePolyline), and
//    seals via AppendParametricSketchShape (which records the history entry). Ground picking + world→pixel projection come from the shared
//    SketchModelGroundProjection unit so this draw and the workplane overlay agree on placement. The store owns the geometry + history; this file
//    owns only the gesture.

#include "SketchModelShapeDraw.h"
#include "SketchModelCommandTools.h"

#include "SketchModelViewportPanel.h"
#include "SketchModelGroundProjection.h"
#include "SketchModelFilletModal.h"
#include "SketchModelInsetModal.h"

#include "Operations/Fillet/ParametricSketchFillet.h"
#include "Operations/Boolean/ParametricSketchBoolean.h"

#include "SceneDirectoryInspectorPanel.h"
#include "InspectorContentProfile.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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

    // 📝 The open-ended (Required == 0) families collect control points click-by-click until a FINISH gesture seals them — there is no fixed count.
    //    This returns the MINIMUM seated points a finish will accept, matching the store's per-family evaluators (below which EvaluateShapePolyline
    //    just echoes the raw points): Spline (Catmull-Rom) needs 3; Polyline / Bezier / BSpline / Nurbs evaluate from 2. A finish below the minimum
    //    is ignored (the run keeps collecting), so a stray Enter never seals a degenerate one-point curve.
    int OpenEndedMinimumPoints(Frontier::ParametricSketchShapeCategory Category)
    {
        return (Category == Frontier::ParametricSketchShapeCategory::Spline) ? 3 : 2;
    }

    // 📝 Whether a category is one of the open-ended free-curve / polyline families (Required == 0): it seats points on every click and seals only on
    //    a finish gesture, rather than auto-sealing when a fixed count is reached.
    bool CategoryIsOpenEnded(Frontier::ParametricSketchShapeCategory Category)
    {
        return Frontier::ResolveParametricSketchDefiningCount(Category) == 0;
    }

    // 📝 The CENTRE-rectangle affordance (SketchCentreRect): the store has ONE Rectangle solver that takes two OPPOSITE corners, so a centre-drawn box
    //    is expressed by mapping the two clicks — [centre C, corner P] — to the two opposite corners [2C - P, P] before the solver sees them. The box
    //    then grows symmetrically about the first click. A no-op (returns the points verbatim) unless CentreRect is set AND exactly two points are in
    //    hand, so a single seated centre (one point) still previews nothing and the fixed-2 seal still fires on the second click.
    std::vector<ImVec2> ApplyCentreRectangle(const std::vector<ImVec2>& Points, bool CentreRect)
    {
        if (!CentreRect || Points.size() != 2)
            return Points;
        const ImVec2 Centre = Points[0];
        const ImVec2 Corner = Points[1];
        const ImVec2 Opposite(2.0f * Centre.x - Corner.x, 2.0f * Centre.y - Corner.y);
        return { Opposite, Corner };
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

    // 📝 Cue a caught snap target: a small marker at the snapped pixel whose SHAPE reads the category (a diamond for a vertex endpoint, a square for a
    //    midpoint, a ring for a round-family centre, an X for a point along a curve), stroked in the snap green so it reads as "the magnet grabbed here".
    //    Purely a hint — the actual override already moved RubberEnd, so a click seals on this point whether the marker draws or not.
    void DrawSnapMarker(ImDrawList* Draw, ImVec2 Pixel, Frontier::ParametricSketchSnapCategory Category)
    {
        const ImU32 Ink = ImGui::GetColorU32(ImVec4(0.30f, 0.95f, 0.45f, 1.0f));
        const float R   = 5.5f;
        switch (Category)
        {
            case Frontier::ParametricSketchSnapCategory::Endpoint:   // diamond
            {
                const ImVec2 P[4] = { ImVec2(Pixel.x, Pixel.y - R), ImVec2(Pixel.x + R, Pixel.y),
                                      ImVec2(Pixel.x, Pixel.y + R), ImVec2(Pixel.x - R, Pixel.y) };
                Draw->AddPolyline(P, 4, Ink, ImDrawFlags_Closed, 1.6f);
                break;
            }
            case Frontier::ParametricSketchSnapCategory::Midpoint:   // square
                Draw->AddRect(ImVec2(Pixel.x - R, Pixel.y - R), ImVec2(Pixel.x + R, Pixel.y + R), Ink, 0.0f, 0, 1.6f);
                break;
            case Frontier::ParametricSketchSnapCategory::Center:     // ring
                Draw->AddCircle(Pixel, R, Ink, 16, 1.6f);
                break;
            default:                                                 // AlongCurve (and any other) — an X
                Draw->AddLine(ImVec2(Pixel.x - R, Pixel.y - R), ImVec2(Pixel.x + R, Pixel.y + R), Ink, 1.6f);
                Draw->AddLine(ImVec2(Pixel.x - R, Pixel.y + R), ImVec2(Pixel.x + R, Pixel.y - R), Ink, 1.6f);
                break;
        }
    }

    // 📝 Whether a category solves a round-family CENTRE worth snapping to (Arc / Circle / Ellipse / Polygon). The straight + free-curve families
    //    leave Centre at its default (0,0), so offering a centre candidate there would be a phantom point at the world origin — only these four contribute.
    bool CategoryHasCentre(Frontier::ParametricSketchShapeCategory Category)
    {
        return Category == Frontier::ParametricSketchShapeCategory::Arc     ||
               Category == Frontier::ParametricSketchShapeCategory::Circle  ||
               Category == Frontier::ParametricSketchShapeCategory::Ellipse ||
               Category == Frontier::ParametricSketchShapeCategory::Polygon;
    }

    // 📝 A world-space (mm) segment projected + NEAR-PLANE CLIPPED to a pixel segment, with the world-space parameters of the surviving span. Perspective
    //    makes this necessary: an edge with ONE endpoint behind the eye (ClipW <= 0) is STILL on screen for its visible length — dropping the whole edge
    //    (the naive `if (!InFront) skip`) makes any shape you stand close to unpickable. So work in clip space: clip the edge to w > wmin, then divide.
    //    Returns false only when the whole edge is behind the eye. OutT0/OutT1 are the clipped span's params along the ORIGINAL world edge (A→B), so the
    //    caller maps a pixel-space foot back to an exact world-mm point. Column-major Column[c][r], same convention as ProjectWorldPoint.
    struct ClippedEdge
    {
        ImVec2 PixelA = ImVec2(0, 0);
        ImVec2 PixelB = ImVec2(0, 0);
        float  T0     = 0.0f;   // [-] world param of PixelA along A→B
        float  T1     = 1.0f;   // [-] world param of PixelB along A→B
    };

    bool ProjectClippedEdge(const Frontier::Matrix4f& VP, ImVec2 CanvasOrigin, ImVec2 CanvasSize,
                            ImVec2 WorldMmA, ImVec2 WorldMmB, ClippedEdge& Out)
    {
        // World mm → metres → clip. Z = 0 (ground plane), so the Column[2] column drops out.
        const float ax = WorldMmA.x * MillimetresToMetres, ay = WorldMmA.y * MillimetresToMetres;
        const float bx = WorldMmB.x * MillimetresToMetres, by = WorldMmB.y * MillimetresToMetres;

        float AX = VP.Column[0][0]*ax + VP.Column[1][0]*ay + VP.Column[3][0];
        float AY = VP.Column[0][1]*ax + VP.Column[1][1]*ay + VP.Column[3][1];
        float AW = VP.Column[0][3]*ax + VP.Column[1][3]*ay + VP.Column[3][3];
        float BX = VP.Column[0][0]*bx + VP.Column[1][0]*by + VP.Column[3][0];
        float BY = VP.Column[0][1]*bx + VP.Column[1][1]*by + VP.Column[3][1];
        float BW = VP.Column[0][3]*bx + VP.Column[1][3]*by + VP.Column[3][3];

        // Clip the segment to the half-space W >= wmin (the near plane, keeping the eye-side portion). Both behind → whole edge invisible.
        constexpr float WMin = 1e-4f;
        const bool AIn = AW > WMin, BIn = BW > WMin;
        if (!AIn && !BIn)
            return false;

        float t0 = 0.0f, t1 = 1.0f;   // clip params along the CLIP-space edge A→B (linear in clip space — the divide comes after)
        if (AIn != BIn)
        {
            // One endpoint crossed: solve for the crossing s where W(s) = wmin, W(s) = AW + s*(BW-AW).
            const float s = (WMin - AW) / (BW - AW);
            if (AIn) t1 = s; else t0 = s;   // keep the in-front side
        }

        // Interpolate clip coords to the clipped params, then perspective-divide each to NDC → pixel.
        auto ToPixel = [&](float t, ImVec2& Pixel)
        {
            const float cx = AX + (BX - AX) * t;
            const float cy = AY + (BY - AY) * t;
            const float cw = AW + (BW - AW) * t;
            const float nx = cx / cw, ny = cy / cw;
            Pixel.x = CanvasOrigin.x + (nx * 0.5f + 0.5f) * CanvasSize.x;
            Pixel.y = CanvasOrigin.y + (ny * 0.5f + 0.5f) * CanvasSize.y;
        };
        ToPixel(t0, Out.PixelA);
        ToPixel(t1, Out.PixelB);
        Out.T0 = t0;
        Out.T1 = t1;
        return true;
    }

    // Nearest point (squared pixel distance + clamped param) from a cursor to a pixel segment. T is clamped to [0,1] along PixelA→PixelB.
    float PixelSegmentDistanceSq(ImVec2 PixelA, ImVec2 PixelB, ImVec2 Cursor, float& OutT)
    {
        const float ex = PixelB.x - PixelA.x, ey = PixelB.y - PixelA.y;
        const float LenSq = ex * ex + ey * ey;
        float T = 0.0f;
        if (LenSq > 1e-6f)
        {
            T = ((Cursor.x - PixelA.x) * ex + (Cursor.y - PixelA.y) * ey) / LenSq;
            if (T < 0.0f) T = 0.0f; else if (T > 1.0f) T = 1.0f;
        }
        OutT = T;
        const float fx = PixelA.x + ex * T - Cursor.x, fy = PixelA.y + ey * T - Cursor.y;
        return fx * fx + fy * fy;
    }

    // 🔴 The ON-SCREEN mm→pixel scale at a world anchor (authored mm). ResolveAdaptiveSampleBudget sizes a curve's segment count from its world length ×
    //    this scale (aiming ~6 px per chord), so a curve stays smooth at ANY zoom AND grows its budget as control points push the hull longer — the cure
    //    for "more points ⇒ reads like a polygon" (a fixed 48-sample flatten stretched thinner over each new span). Measured empirically rather than
    //    derived: project the anchor and a 1 mm-offset point through the SAME view-projection the render uses and take their pixel distance, so the scale
    //    already carries perspective foreshortening + zoom. A degenerate projection (anchor behind the eye / a zero span) falls back to 0, which makes the
    //    budget resolver take its per-category default — never a divide-by-zero, never a coarser curve than the fixed path gave before.
    float EstimatePixelsPerMm(const Frontier::Matrix4f& ViewProjection, ImVec2 CanvasOrigin, ImVec2 CanvasSize, ImVec2 AnchorMm)
    {
        const ProjectedPoint Base   = ProjectWorldPoint(ViewProjection, CanvasOrigin, CanvasSize, AnchorMm.x,        AnchorMm.y, 0.0f);
        const ProjectedPoint Offset = ProjectWorldPoint(ViewProjection, CanvasOrigin, CanvasSize, AnchorMm.x + 1.0f, AnchorMm.y, 0.0f);
        if (!Base.InFront || !Offset.InFront)
            return 0.0f;
        const float dx = Offset.Pixel.x - Base.Pixel.x, dy = Offset.Pixel.y - Base.Pixel.y;
        return std::sqrt(dx * dx + dy * dy);   // pixels spanned by 1 mm at the anchor
    }

    // The world-mm anchor to measure a shape's on-screen scale at: the round families carry a solved Centre, the straight / free-curve families
    //    do not, so fall back to the first defining point (their hull origin). Both sit on the shape, so the local scale is representative.
    ImVec2 ResolveShapeScaleAnchor(const Frontier::ParametricSketchShape& Shape)
    {
        if (CategoryHasCentre(Shape.Category))
            return Shape.Centre;
        return Shape.Points.empty() ? Shape.Centre : Shape.Points.front();
    }

    // 📝 The result of a SCREEN-SPACE snap scan: the world-mm point to seat on, the category (drives the marker glyph), and whether a candidate landed
    //    within the pixel radius. Mirrors Frontier::ParametricSketchSnapCandidate but is resolved in PIXELS, not mm — so DrawSnapMarker + the seat read it the same.
    struct ScreenSnap
    {
        ImVec2                                 Point    = ImVec2(0, 0);   // [mm] - the world position to snap the seat / rubber end onto
        Frontier::ParametricSketchSnapCategory Category = Frontier::ParametricSketchSnapCategory::Endpoint;
        bool                                   Resolved = false;         // [-]  - a candidate within the pixel radius was found
    };

    // 🔴 SCREEN-SPACE draw-time snap — the perspective-correct replacement for the store's mm-radius ResolveSnapCandidate. The mm approach is
    //    fundamentally wrong in perspective: the store compares the cursor to each feature in ground-mm space against ONE tolerance, but mm-per-pixel
    //    varies ACROSS the scene (the ground recedes), so two lines that look nearly on top of each other on screen can be tens of mm apart on the
    //    ground — the user's "in perspective the lines are on top of each other by the camera but there's still a large ground distance". No mm
    //    tolerance measured at the cursor can rescue a feature that sits at a DIFFERENT depth. The cure is to measure closeness where the user sees it:
    //    PROJECT each candidate to canvas pixels and take the nearest within a fixed PIXEL radius. That holds the catch radius at exactly RadiusPixels
    //    on screen in EVERY view, at every zoom, with NO mm-per-pixel derivation and NO radius inflation (which the user rightly rejected as a cheat).
    //    Candidates mirror the store's snap categories: each shape's defining Points (Endpoint), the round families' solved Centre (Center), and — off
    //    the cached display outline — every segment midpoint (Midpoint) and the nearest point on each segment (AlongCurve). Priority Endpoint > Center >
    //    Midpoint > AlongCurve breaks a within-a-pixel tie so a vertex always wins over the edge it lies on.
    //    SELF-SNAP: the in-progress stroke is NOT in the store, so it is passed separately as SelfPoints (the live PendingPoints). Each seated point of
    //    the current stroke is scanned as an Endpoint too, so a stroke can close back onto its own start / an earlier vertex (the "snap on itself" the
    //    user asked for). The live rubber end is excluded by the caller (it only passes the SEATED points), so the cursor never snaps to itself.
    ScreenSnap ResolveScreenSpaceSnap(const SketchModelViewportState&       State,
                                      Frontier::ParametricSketchShapeStore& Store,
                                      ImVec2 CanvasOrigin, ImVec2 CanvasSize,
                                      ImVec2 CursorPixel, float RadiusPixels,
                                      const std::vector<ImVec2>&            SelfPoints)
    {
        ScreenSnap Best;
        float BestScore = RadiusPixels * RadiusPixels;   // squared pixel distance; only a strictly-closer candidate (or an equal-distance higher rank) replaces it
        int   BestRank  = -1;                             // category priority of the current best (higher wins a near-tie)

        const Frontier::Matrix4f ViewProjection = AssembleGroundViewProjection(State);

        // Category priority so a vertex beats the edge it lies on when both fall inside the radius: Endpoint 3 > Center 2 > Midpoint 1 > AlongCurve 0.
        auto RankOf = [](Frontier::ParametricSketchSnapCategory C) -> int
        {
            switch (C)
            {
                case Frontier::ParametricSketchSnapCategory::Endpoint: return 3;
                case Frontier::ParametricSketchSnapCategory::Center:   return 2;
                case Frontier::ParametricSketchSnapCategory::Midpoint: return 1;
                default:                                               return 0;   // AlongCurve
            }
        };

        // Consider one world-mm candidate: project it, reject a behind-eye point, and keep it when it is closer — or an equal-distance higher-priority
        // category — than the running best.
        auto Consider = [&](ImVec2 WorldMm, Frontier::ParametricSketchSnapCategory Category)
        {
            const ProjectedPoint P = ProjectWorldPoint(ViewProjection, CanvasOrigin, CanvasSize, WorldMm.x, WorldMm.y, 0.0f);
            if (!P.InFront)
                return;
            const float dx = P.Pixel.x - CursorPixel.x, dy = P.Pixel.y - CursorPixel.y;
            const float Score = dx * dx + dy * dy;
            const int   Rank  = RankOf(Category);
            const bool  Closer    = Score < BestScore - 1.0f;                       // clearly nearer
            const bool  NearTieUp = Score < BestScore + 1.0f && Rank > BestRank;    // within ~1px but a higher-priority category
            if (Closer || NearTieUp)
            {
                Best.Point    = WorldMm;
                Best.Category = Category;
                Best.Resolved = true;
                BestScore     = Score;
                BestRank      = Rank;
            }
        };

        for (Frontier::ParametricSketchShape& Shape : Store.Shapes)
        {
            if (!Shape.Displayed)
                continue;

            // Endpoints: every DEFINING point (the user's placement vertices / control points).
            for (const ImVec2& Point : Shape.Points)
                Consider(Point, Frontier::ParametricSketchSnapCategory::Endpoint);

            // Centre: only the round families solve one; a straight / free-curve shape leaves it at the default, so guard on the category.
            if (CategoryHasCentre(Shape.Category))
                Consider(Shape.Centre, Frontier::ParametricSketchSnapCategory::Center);

            // Midpoint + AlongCurve: walk the CACHED display outline (the same flatten the render + pick paths use, so the snap agrees exactly with
            //    what is drawn). Midpoint is each segment's world-space centre; AlongCurve is the cursor's nearest point on each segment, clamped in
            //    SCREEN space so a long edge receding into depth still catches where it looks closest.
            const std::vector<ImVec2>& Outline = Frontier::RetrieveCachedOutline(Shape);   // warms the flatten cache (non-const store)
            for (size_t Index = 0; Index + 1 < Outline.size(); ++Index)
            {
                const ImVec2 A = Outline[Index];
                const ImVec2 B = Outline[Index + 1];

                // Midpoint of the WORLD segment (projecting the two ends' pixel midpoint would drift under perspective — the world midpoint is exact).
                Consider(ImVec2((A.x + B.x) * 0.5f, (A.y + B.y) * 0.5f), Frontier::ParametricSketchSnapCategory::Midpoint);

                // AlongCurve: project the edge with NEAR-PLANE clipping (an edge straddling the camera plane still snaps along its visible length),
                //    clamp the cursor's foot onto the surviving pixel segment, then map that pixel param back through the clipped WORLD span [T0,T1]
                //    to an exact world-mm point. The point is re-scored in pixels by Consider, so a small perspective non-linearity along one short
                //    display chord is self-correcting and marker-exact.
                ClippedEdge Edge;
                if (!ProjectClippedEdge(ViewProjection, CanvasOrigin, CanvasSize, A, B, Edge))
                    continue;
                float PixelT = 0.0f;
                PixelSegmentDistanceSq(Edge.PixelA, Edge.PixelB, CursorPixel, PixelT);
                const float WorldT = Edge.T0 + (Edge.T1 - Edge.T0) * PixelT;   // pixel foot → clipped world param → world point
                Consider(ImVec2(A.x + (B.x - A.x) * WorldT, A.y + (B.y - A.y) * WorldT),
                         Frontier::ParametricSketchSnapCategory::AlongCurve);
            }
        }

        // SELF-SNAP: the SEATED points of the in-progress stroke, so it can close back onto its own start / an earlier vertex. Scanned as Endpoints
        //    through the SAME pixel metric + priority, so a live vertex and a committed one compete fairly and the nearer on screen wins.
        for (const ImVec2& Point : SelfPoints)
            Consider(Point, Frontier::ParametricSketchSnapCategory::Endpoint);

        return Best;
    }

    // 🔴 SCREEN-SPACE whole-shape pick — the perspective-correct twin of the snap scan, for AdvanceShapeSelection. A mm tolerance has the same defect
    //    here (a receding shape right under the pointer on screen is nearly unclickable in perspective), so project each displayed shape's cached
    //    outline to pixels and measure the cursor's minimum point-to-segment distance IN PIXELS; the closest shape within RadiusPixels wins. Returns 0
    //    when nothing is close. Frozen shapes stay pickable (so they can be inspected / deleted) — the drag / edit gates are what skip them elsewhere.
    uint32_t ResolveScreenSpacePick(const SketchModelViewportState&       State,
                                    Frontier::ParametricSketchShapeStore& Store,
                                    ImVec2 CanvasOrigin, ImVec2 CanvasSize,
                                    ImVec2 CursorPixel, float RadiusPixels)
    {
        uint32_t Best      = 0;
        float    BestScore = RadiusPixels * RadiusPixels;   // squared pixel distance
        const Frontier::Matrix4f ViewProjection = AssembleGroundViewProjection(State);

        for (Frontier::ParametricSketchShape& Shape : Store.Shapes)
        {
            if (!Shape.Displayed)
                continue;

            const std::vector<ImVec2>& Outline = Frontier::RetrieveCachedOutline(Shape);   // warms the flatten cache (non-const store)
            if (Outline.size() < 2)
                continue;

            // Minimum point-to-segment distance (squared pixels) from the cursor to any outline edge, plus the closing edge for a loop. Each edge is
            //    NEAR-PLANE clipped in world space (ProjectClippedEdge), so an edge with one vertex behind the eye still contributes its visible span —
            //    a shape you stand close to (one corner behind the camera) stays fully pickable, which the old whole-shape behind-eye skip broke.
            const size_t Count = Outline.size();
            const size_t Last  = Shape.ClosedEnabled ? Count : Count - 1;
            for (size_t Index = 0; Index < Last; ++Index)
            {
                const ImVec2 A = Outline[Index];
                const ImVec2 B = Outline[(Index + 1) % Count];
                ClippedEdge Edge;
                if (!ProjectClippedEdge(ViewProjection, CanvasOrigin, CanvasSize, A, B, Edge))
                    continue;   // whole edge behind the eye
                float T = 0.0f;
                const float Score = PixelSegmentDistanceSq(Edge.PixelA, Edge.PixelB, CursorPixel, T);
                if (Score < BestScore)
                {
                    BestScore = Score;
                    Best      = Shape.Identifier;
                }
            }
        }
        return Best;
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

    // 📝 Resolve or create a leaf Folder row named Label inside HostRegion, returning ITS nested region (expanded so a fresh row is visible). Reused for
    //    both the "Profiles" and "Curves" leaf folders so the split is one code path. HostRegion is the root region or a workplane's nested region.
    std::vector<Frontier::SketchOutlinerUi::RecordEntry>& ResolveLeafFolderRegion(
        SceneDirectoryInspectorValidation::InspectorPanelState&        Directory,
        std::vector<Frontier::SketchOutlinerUi::RecordEntry>&          HostRegion,
        const char*                                                   Label)
    {
        namespace SDI = SceneDirectoryInspectorValidation;
        namespace SO  = Frontier::SketchOutlinerUi;

        const int FolderId = static_cast<int>(SDI::RecordClassification::Folder);

        for (SO::RecordEntry& Row : HostRegion)
            if (Row.ClassificationId == FolderId && Row.Label == Label)
            {
                Row.ExpandedState = true;
                return Row.NestedRegion;
            }

        SO::RecordEntry Folder;
        Folder.Token            = Directory.Directory.NextToken++;
        Folder.ClassificationId = FolderId;
        ResolveClassAppearance(FolderId, "folder", SDI::ClassificationHue(SDI::RecordClassification::Folder),
                               Folder.IconKey, Folder.TintColor);
        Folder.ExpandedState  = true;
        Folder.ConcealedState = false;
        Folder.Label          = Label;
        SDI::RecordProfile& FolderBag = SDI::ProfileFor(Directory, Folder.Token);
        SDI::EstablishProfile(SDI::RecordClassification::Folder, 0, true, FolderBag);
        HostRegion.push_back(std::move(Folder));
        return HostRegion.back().NestedRegion;
    }

    // 📝 Resolve WHERE a sketch row is parented, honouring the user's rules: (a) closed shapes go in a "Profiles" leaf folder, open shapes in a "Curves"
    //    leaf folder — the store's own auto-category vocabulary, unambiguous for open/closed curves and profiles alike; (b) if the tree holds a
    //    construction workplane the split folders nest under the MOST RECENT top-level Workplane row (drawn "within" it), else they sit at root. Returns
    //    the destination region + expands every container so the new row is visible. Never parents at the bare root: the Profiles/Curves folder always
    //    stands between, so a loose sketch and a workplane-owned sketch never read the same.
    std::vector<Frontier::SketchOutlinerUi::RecordEntry>& ResolveSketchParentRegion(
        SceneDirectoryInspectorValidation::InspectorPanelState& Directory,
        bool                                                    Closed)
    {
        namespace SDI = SceneDirectoryInspectorValidation;
        namespace SO  = Frontier::SketchOutlinerUi;

        SO::SketchOutlinerState& Tree = Directory.Directory;
        const int WorkplaneId = static_cast<int>(SDI::RecordClassification::Workplane);

        // -- Pick the host region: the LAST top-level workplane's nested region when one is present, else the bare root. --
        std::vector<SO::RecordEntry>* Host = &Tree.RootRegion;
        for (int Index = static_cast<int>(Tree.RootRegion.size()) - 1; Index >= 0; --Index)
            if (Tree.RootRegion[Index].ClassificationId == WorkplaneId && !Tree.RootRegion[Index].ConcealedState)
            {
                Tree.RootRegion[Index].ExpandedState = true;
                Host = &Tree.RootRegion[Index].NestedRegion;
                break;
            }

        // -- Then the closed/open split leaf folder inside that host: "Profiles" for a closed loop, "Curves" for an open run. --
        return ResolveLeafFolderRegion(Directory, *Host, Closed ? "Profiles" : "Curves");
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

    // 🔴 SELECTION highlight: a SELECTED shape strokes bright green (its outline reads as picked), a merely HOVERED one a dimmer green so the pointer
    //    leaves a live trail the click can confirm. Selection is the store's Selected id / SelectionSet (multi-pick); hover is Store.Hovered. Green by
    //    request — later sub-element (vertex / edge) picks reuse the same hue on the sub-run rather than the whole outline.
    const ImU32 SelectInk  = ImGui::GetColorU32(ImVec4(0.30f, 0.95f, 0.45f, 0.98f));
    const ImU32 SelectFill = ImGui::GetColorU32(ImVec4(0.30f, 0.95f, 0.45f, 0.14f));
    const ImU32 HoverInk   = ImGui::GetColorU32(ImVec4(0.30f, 0.95f, 0.45f, 0.55f));

    for (Frontier::ParametricSketchShape& Shape : Store.Shapes)
    {
        if (!Shape.Displayed)
            continue;

        // 🔴 ADAPTIVE flatten: size the curve's segment count to its on-screen length, not a fixed 48. Measure the mm→pixel scale at the shape and let
        //    ResolveAdaptiveSampleBudget pick a budget that keeps every chord ~6 px — so a long / many-control-point curve gets MORE segments (no polygon
        //    facets) and a tiny one stays cheap. The budget is cached per (shape, budget) inside RetrieveCachedOutline, so this only re-flattens when the
        //    budget actually changes (a zoom step or a new control point), not every frame. 0 (a behind-eye anchor) falls back to the per-category default.
        const float PixelsPerMm = EstimatePixelsPerMm(ViewProjection, CanvasOrigin, CanvasSize, ResolveShapeScaleAnchor(Shape));
        const int   Budget      = Frontier::ResolveAdaptiveSampleBudget(Shape, PixelsPerMm);

        const std::vector<ImVec2>& Outline = Frontier::RetrieveCachedOutline(Shape, Budget);   // warms the flatten cache (non-const store)
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

        // 🔴 Standing decides the ink, selected-over-hover-over-placed. A shape is SELECTED if it is the store's primary Selected id or sits anywhere in
        //    the ordered SelectionSet (shift-click multi-pick); only the primary carries the fill so a multi-selection does not flood the canvas green.
        bool Selected = (Shape.Identifier != 0 && Shape.Identifier == Store.Selected);
        if (!Selected)
            for (uint32_t Id : Store.SelectionSet)
                if (Id == Shape.Identifier) { Selected = true; break; }
        const bool Hovered = (!Selected && Shape.Identifier != 0 && Shape.Identifier == Store.Hovered);

        const ImU32 Ink       = Selected ? SelectInk : (Hovered ? HoverInk : CommitInk);
        const bool  DrawFill   = Selected && (Shape.Identifier == Store.Selected);
        const float Thickness = Selected ? 2.4f : (Hovered ? 2.0f : 1.7f);

        const bool Closed = Shape.ClosedEnabled;
        if (Closed && Pixels.size() >= 3)
        {
            if (DrawFill)
                Draw->AddConvexPolyFilled(Pixels.data(), (int)Pixels.size(), SelectFill);
            else if (Shape.FillEnabled)
                Draw->AddConvexPolyFilled(Pixels.data(), (int)Pixels.size(), CommitFill);
        }
        Draw->AddPolyline(Pixels.data(), (int)Pixels.size(), Ink, Closed ? ImDrawFlags_Closed : ImDrawFlags_None, Thickness);
    }

    Draw->PopClipRect();
}


namespace
{
    // 📝 Append ONE sketch row for ShapeId into the correct Profiles/Curves leaf folder and seed its Properties bag — the geometry-only half of a
    //    mirror, with NO History revision. Both the public mirror (which then logs History) and the reconcile pass (History already logged by the
    //    panel's tool hooks) share it, so a row lands in exactly one place regardless of how the shape was born. Returns the issued row token, or 0
    //    when ShapeId is absent from the store.
    uint32_t AppendSketchRow(Frontier::ParametricSketchShapeStore&                   Store,
                             SceneDirectoryInspectorValidation::InspectorPanelState& Directory,
                             uint32_t                                                ShapeId)
    {
        namespace SDI = SceneDirectoryInspectorValidation;
        namespace SO  = Frontier::SketchOutlinerUi;

        const Frontier::ParametricSketchShape* Shape = Frontier::ResolveParametricSketchShape(Store, ShapeId);
        if (Shape == nullptr)
            return 0;

        // -- Copy the Sketch classification's icon key + tint from the content profile so the row matches an add-menu creation exactly. --
        const int     SketchId = static_cast<int>(SDI::RecordClassification::Sketch);
        std::string   IconKey;
        std::uint32_t Tint = 0;
        ResolveClassAppearance(SketchId, "sketch", SDI::ClassificationHue(SDI::RecordClassification::Sketch), IconKey, Tint);

        // 🔴 Resolve the parent FIRST (it may create + push a Profiles/Curves folder, growing RootRegion), THEN issue the sketch row's token and append
        //    — reading a region reference across a later push_back would dangle. Order: resolve region (all mutation done) → issue token → append.
        SO::SketchOutlinerState&      Tree         = Directory.Directory;
        std::vector<SO::RecordEntry>& ParentRegion = ResolveSketchParentRegion(Directory, Shape->ClosedEnabled);
        const SDI::RecordToken        Token        = Tree.NextToken++;

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
        return Token;
    }
}


void MirrorSketchShapeIntoDirectory(Frontier::ParametricSketchShapeStore&                   Store,
                                    SceneDirectoryInspectorValidation::InspectorPanelState& Directory,
                                    uint32_t                                                ShapeId,
                                    uint32_t&                                               OutRowToken)
{
    namespace SDI = SceneDirectoryInspectorValidation;

    OutRowToken = 0;

    const Frontier::ParametricSketchShape* Shape = Frontier::ResolveParametricSketchShape(Store, ShapeId);
    if (Shape == nullptr)
        return;

    OutRowToken = AppendSketchRow(Store, Directory, ShapeId);   // hand the row's identity back for the ShapeId -> token back-map
    if (OutRowToken == 0)
        return;

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


namespace
{
    // 📝 Remove the row carrying Token from the tree, wherever it nests, returning true on a hit. Sketch rows are leaves inside a Profiles/Curves
    //    folder, but the walk recurses so a workplane-nested folder is reached too. Erases the first match and stops.
    bool EraseRowByToken(std::vector<Frontier::SketchOutlinerUi::RecordEntry>& Region, uint32_t Token)
    {
        for (size_t Index = 0; Index < Region.size(); ++Index)
        {
            if (Region[Index].Token == Token)
            {
                Region.erase(Region.begin() + (long)Index);
                return true;
            }
            if (EraseRowByToken(Region[Index].NestedRegion, Token))
                return true;
        }
        return false;
    }

    // 📝 Resolve the label of the leaf Folder that directly OWNS the row carrying Token (its immediate parent), or nullptr when the token is at root /
    //    absent. Reconcile compares this against the folder the shape's current ClosedEnabled demands to decide whether a row must be re-homed.
    const char* ResolveOwningFolderLabel(const std::vector<Frontier::SketchOutlinerUi::RecordEntry>& Region,
                                         uint32_t Token, const char* ParentLabel)
    {
        for (const Frontier::SketchOutlinerUi::RecordEntry& Row : Region)
        {
            if (Row.Token == Token)
                return ParentLabel;
            const char* Deeper = ResolveOwningFolderLabel(Row.NestedRegion, Token, Row.Label.c_str());
            if (Deeper != nullptr)
                return Deeper;
        }
        return nullptr;
    }
}


void ReconcileSketchDirectory(Frontier::ParametricSketchShapeStore&                    Store,
                              SceneDirectoryInspectorValidation::InspectorPanelState&  Directory,
                              std::vector<std::pair<uint32_t, uint32_t>>&              ShapeRowTokens)
{
    namespace SO = Frontier::SketchOutlinerUi;

    std::vector<SO::RecordEntry>& Root = Directory.Directory.RootRegion;

    // ── 1. Drop rows for shapes that have LEFT the store (join-consumed, cut, deleted), and their back-map pairs. ──────────
    for (size_t Index = 0; Index < ShapeRowTokens.size(); )
    {
        const uint32_t ShapeId  = ShapeRowTokens[Index].first;
        const uint32_t RowToken = ShapeRowTokens[Index].second;
        if (Frontier::ResolveParametricSketchShape(Store, ShapeId) == nullptr)
        {
            EraseRowByToken(Root, RowToken);
            ShapeRowTokens.erase(ShapeRowTokens.begin() + (long)Index);
            continue;   // do not advance — the erase shifted the next pair into this slot
        }
        ++Index;
    }

    // ── 2. Re-home rows now in the WRONG leaf folder (a shape's ClosedEnabled flipped: Join closed a run, Cut opened a loop). Erase the stale row and
    //       re-append into the folder its current closedness demands, refreshing the back-map token so right-click Properties still resolves. ─────────
    for (std::pair<uint32_t, uint32_t>& Pairing : ShapeRowTokens)
    {
        const Frontier::ParametricSketchShape* Shape = Frontier::ResolveParametricSketchShape(Store, Pairing.first);
        if (Shape == nullptr)
            continue;   // covered by pass 1, but guard anyway
        const char* Owning = ResolveOwningFolderLabel(Root, Pairing.second, nullptr);
        const char* Wanted = Shape->ClosedEnabled ? "Profiles" : "Curves";
        if (Owning != nullptr && std::strcmp(Owning, Wanted) != 0)
        {
            EraseRowByToken(Root, Pairing.second);
            Pairing.second = AppendSketchRow(Store, Directory, Pairing.first);
        }
    }

    // ── 3. Mirror shapes with NO row yet (an Offset-appended Profile, a Join result — born outside the seal path). History for these is already logged
    //       by the panel's tool hooks, so this appends only the row (AppendSketchRow, no revision) and records the fresh back-map pair. ──────────────
    for (const Frontier::ParametricSketchShape& Shape : Store.Shapes)
    {
        if (!Shape.Displayed)
            continue;
        bool HasRow = false;
        for (const std::pair<uint32_t, uint32_t>& Pairing : ShapeRowTokens)
            if (Pairing.first == Shape.Identifier) { HasRow = true; break; }
        if (HasRow)
            continue;

        const uint32_t Token = AppendSketchRow(Store, Directory, Shape.Identifier);
        if (Token != 0)
            ShapeRowTokens.emplace_back(Shape.Identifier, Token);
    }
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
                          ImVec2 CanvasOrigin, ImVec2 CanvasSize, float WheelNotches, bool CentreRect,
                          bool Suppressed)
{
    (void)Directory;   // threaded for a later mirror step; the seal writes only the store here
    if (!Store.DrawingEnabled)
        return 0;
    if (CanvasSize.x < 1.0f || CanvasSize.y < 1.0f)
        return 0;

    // 🔴 FROZEN this frame (the Properties card is open, or a right-click PICK just consumed the press). Do NOTHING — do not seat a point, do not advance
    //    the rubber band, and above all do NOT run the right-click cancel below. The tool stays armed (DrawingEnabled untouched) and resumes when the card
    //    closes and Suppressed clears. Returning here BEFORE the cancel branch is the whole point: a right-click that opened the card must not also cancel.
    if (Suppressed)
        return 0;

    const ImGuiIO& Io = ImGui::GetIO();

    // -- Cancel: Escape OR a right-click abandons the in-progress stroke with nothing added (PendingPoints wiped, DrawingEnabled dropped). The two behave
    //    IDENTICALLY now — both cancel the DRAWING, not the TOOL. The panel leaves the tool latch intact for either, so SustainSketchToolCycle re-arms the
    //    same category and the user stays in the tool on a fresh blank stroke. Dropping DrawingEnabled here is safe: the re-arm decision lives in the latch. --
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        Store.DrawingEnabled = false;
        Store.PendingPoints.clear();
        return 0;
    }

    const Frontier::ParametricSketchShapeCategory Category = Store.DrawingCategory;
    const int  Required   = Frontier::ResolveParametricSketchDefiningCount(Category);   // 0 = open-ended (Polyline / Bezier / Spline / …)
    const bool OpenEnded  = CategoryIsOpenEnded(Category);
    const int  OpenMinimum = OpenEndedMinimumPoints(Category);

    // -- Where the cursor meets the ground this frame (authored mm). Only act while the pointer is over the canvas rect. --
    const bool OverCanvas =
        Io.MousePos.x >= CanvasOrigin.x && Io.MousePos.x <= CanvasOrigin.x + CanvasSize.x &&
        Io.MousePos.y >= CanvasOrigin.y && Io.MousePos.y <= CanvasOrigin.y + CanvasSize.y;

    float GroundMmX = 0.0f, GroundMmY = 0.0f;
    const bool GroundHit = OverCanvas &&
        CastCursorToGroundMillimetres(State, CanvasOrigin, CanvasSize, Io.MousePos, GroundMmX, GroundMmY);
    if (GroundHit)
        Store.RubberEnd = ImVec2(GroundMmX, GroundMmY);

    // 🔴 DRAW-TIME SNAP — ALWAYS ON (no toggle), and DECOUPLED FROM THE GROUND CAST. The snap is a pure SCREEN-SPACE operation: it projects each
    //    candidate feature on ANOTHER shape (endpoints, segment midpoints, round-family centres, nearest point along an outline) to canvas pixels and
    //    catches the nearest within a fixed pixel radius (ResolveScreenSpaceSnap). Nothing about that needs a ground hit — so it runs on OverCanvas,
    //    NOT on GroundHit. Two things had to change together to make snap work in perspective:
    //      (1) MEASURE IN PIXELS, not mm. The store's mm-radius ResolveSnapCandidate compares the cursor to each feature in ground-mm space against one
    //          tolerance, but mm-per-pixel varies across a receding ground, so a line sitting almost under the cursor ON SCREEN but far away in depth
    //          reads as tens of mm distant and never catches. No mm tolerance measured at the cursor can fix a feature that lives at another depth, and
    //          simply INFLATING the mm radius (or the pixel radius) is a cheat that mis-catches everything near the cursor — the user rejected it. A
    //          per-candidate pixel distance is the only correct model: the catch radius is then exactly RadiusPixels on screen, in every view + zoom.
    //      (2) GATE ON OverCanvas, not GroundHit. In a perspective view aimed near the horizon CastCursorToGroundMillimetres returns false (near-parallel
    //          ray, or the plane only meets the ray behind the eye) even while a committed shape sits right under the cursor on screen — so the old
    //          `if (GroundHit && …)` gate skipped the snap entirely and there was nothing to seat. The ground cast now only supplies a FREE-SPACE point
    //          when no feature is near.
    //    Snap catches BOTH already-committed geometry (Store.Shapes) AND the in-progress stroke's own seated points (Store.PendingPoints, passed as
    //    SelfPoints) so a stroke can close back onto itself. Only the SEATED points are passed — the live rubber end is excluded, so the cursor never
    //    snaps to itself. When a candidate resolves, RubberEnd is overridden onto it and the marker is drawn below.
    constexpr float SnapRadiusPixels = 11.0f;   // [px] - the on-screen catch radius, constant in every view / zoom (matches the store's legacy feel)
    ScreenSnap Snap;   // Resolved == false until caught
    if (OverCanvas && (!Store.Shapes.empty() || !Store.PendingPoints.empty()))
        Snap = ResolveScreenSpaceSnap(State, Store, CanvasOrigin, CanvasSize, Io.MousePos, SnapRadiusPixels, Store.PendingPoints);
    if (Snap.Resolved)
        Store.RubberEnd = Snap.Point;   // override onto the caught feature; the seat + preview below both read RubberEnd

    // The placement point this frame: a caught snap ALWAYS wins (it works with or without a ground hit), else the raw ground point. PlacementHit is the
    // effective "the cursor has a valid point to seat / preview" flag — true whenever EITHER the snap caught or the ground cast hit — so the preview /
    // crosshair / seat logic below no longer dies when only the ground cast missed (a near-horizon perspective graze over a shape).
    const bool PlacementHit = GroundHit || Snap.Resolved;

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
    if (PlacementHit)
        Defining.push_back(Store.RubberEnd);

    // -- Analytic preview. A fixed-count family previews once its defining points (seated + live cursor) reach Required; an open-ended curve previews
    //    the whole control run (seated points PLUS the live cursor as a provisional last control point) as soon as two points exist, flattening with the
    //    SAME evaluator the seal uses so the rubber curve reads exactly like the committed one. --
    const bool FixedReady = Required > 0 && (int)Defining.size() >= Required;
    const bool OpenReady  = OpenEnded && Defining.size() >= 2;
    if (FixedReady || OpenReady)
    {
        const int SideOverride = (Category == Frontier::ParametricSketchShapeCategory::Polygon) ? Store.PendingSideCount : 0;
        // Centre-rect: remap [centre, corner] → [far corner, corner] so the preview box grows symmetrically about the first click. A no-op otherwise.
        const std::vector<ImVec2> Solved = ApplyCentreRectangle(Defining, CentreRect);
        Frontier::ParametricSketchShape Preview = Frontier::ConstructParametricSketchShape(Category, Solved, SideOverride);
        std::vector<ImVec2> Outline;
        // ADAPTIVE preview flatten (mirrors the committed render): size the rubber curve to its on-screen length so it reads exactly as smooth as the
        //    sealed one at every zoom — and grows its segment budget as control points extend the hull, instead of faceting a fixed 48 over a longer span.
        const Frontier::Matrix4f PreviewViewProjection = AssembleGroundViewProjection(State);
        const float PreviewPixelsPerMm = EstimatePixelsPerMm(PreviewViewProjection, CanvasOrigin, CanvasSize, ResolveShapeScaleAnchor(Preview));
        const int   PreviewBudget      = Frontier::ResolveAdaptiveSampleBudget(Preview, PreviewPixelsPerMm);
        Frontier::EvaluateShapePolyline(Preview, Outline, PreviewBudget);
        StrokeGroundPolyline(State, CanvasOrigin, CanvasSize, Outline, CategoryCloses(Category), Draw);

        // A live primary-dimension readout at the cursor (mm). Open-ended curves also report the seated control-point count + the finish hint, so the
        // user knows the run can be sealed (once the minimum is met) and how.
        const char* PrimaryLabel = nullptr;
        const float Primary = Frontier::ResolvePrimaryDimension(Preview, &PrimaryLabel);
        char Readout[112];
        if (Category == Frontier::ParametricSketchShapeCategory::Polygon)
            std::snprintf(Readout, sizeof(Readout), "%s %.0f mm  (%d sides)", PrimaryLabel ? PrimaryLabel : "", Primary, Store.PendingSideCount);
        else if (OpenEnded)
            std::snprintf(Readout, sizeof(Readout), "%s %.0f mm  (%d pts \xC2\xB7 %s)", PrimaryLabel ? PrimaryLabel : "", Primary,
                          (int)Store.PendingPoints.size(),
                          (int)Store.PendingPoints.size() >= OpenMinimum ? "Enter / dbl-click to finish" : "keep clicking");
        else
            std::snprintf(Readout, sizeof(Readout), "%s %.0f mm", PrimaryLabel ? PrimaryLabel : "", Primary);
        DrawReadout(Draw, Io.MousePos, Readout);
    }
    else
    {
        // Not enough points yet: rubber-band a guideline from the last seated point to the cursor, and pip the seated points.
        if (!Store.PendingPoints.empty() && PlacementHit)
        {
            std::vector<ImVec2> Guide = { Store.PendingPoints.back(), Store.RubberEnd };
            StrokeGroundPolyline(State, CanvasOrigin, CanvasSize, Guide, false, Draw);
        }
        // A crosshair on the cursor while seating the first point (over the ground, or on a caught feature).
        if (PlacementHit)
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

    // -- The snap marker, when the magnet caught a target this frame: a category-shaped cue at the snapped point (RubberEnd was already moved onto it),
    //    drawn last so it sits above the rubber band + pips. --
    if (Snap.Resolved)
    {
        const ProjectedPoint P = ProjectGroundMillimetres(State, CanvasOrigin, CanvasSize, Store.RubberEnd.x, Store.RubberEnd.y);
        if (P.InFront)
            DrawSnapMarker(Draw, P.Pixel, Snap.Category);
    }

    // -- OPEN-ENDED FINISH GESTURE (checked BEFORE the seat, so a double-click's second press seals rather than seating a duplicate control point).
    //    Enter, or a left double-click, seals the collected run once the family minimum is met. The double-click's FIRST press already seated its point
    //    below on the prior frame; here the SECOND press (IsMouseDoubleClicked) finishes the run with the points already down. A finish under the minimum
    //    is ignored so a stray Enter never seals a degenerate curve. --
    if (OpenEnded && (int)Store.PendingPoints.size() >= OpenMinimum)
    {
        const bool FinishByKey    = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        const bool FinishByDouble = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        if (FinishByKey || FinishByDouble)
        {
            Sealed = Frontier::AppendParametricSketchShape(Store, Category, Store.PendingPoints, 0);
            Store.PendingPoints.clear();
            Store.DrawingEnabled = false;   // one shape per arm (the sticky-tool cycle re-arms for the next); mirrors the fixed-count seal
            Draw->PopClipRect();
            return Sealed;                  // the completing gesture ends this frame; no seat below
        }
    }

    // -- A left click over the ground seats the cursor point. A FIXED-count family seals when the count is reached; an OPEN-ENDED family only seats
    //    (it seals on the finish gesture above), so it keeps collecting control points click after click. --
    if (PlacementHit && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        Store.PendingPoints.push_back(Store.RubberEnd);

        if (Required > 0 && (int)Store.PendingPoints.size() >= Required)
        {
            const int SideOverride = (Category == Frontier::ParametricSketchShapeCategory::Polygon) ? Store.PendingSideCount : 0;
            // Centre-rect: remap [centre, corner] → [far corner, corner] so the sealed box matches the symmetric preview. A no-op otherwise.
            const std::vector<ImVec2> Solved = ApplyCentreRectangle(Store.PendingPoints, CentreRect);
            Sealed = Frontier::AppendParametricSketchShape(Store, Category, Solved, SideOverride);
            Store.PendingPoints.clear();
            Store.DrawingEnabled = false;   // one shape per arm (the console re-arms for the next); mirrors the workplane draw
        }
    }

    Draw->PopClipRect();
    return Sealed;
}


void AdvanceShapeSelection(const SketchModelViewportState&       State,
                           Frontier::ParametricSketchShapeStore& Store,
                           ImVec2 CanvasOrigin, ImVec2 CanvasSize)
{
    // 🔴 Selection is the IDLE-tool behaviour: it runs ONLY when no draw is armed. While a Sketch* tool is active every click seats a defining point
    //    (AdvanceShapeDraw owns the press), so picking here would fight the seat. The panel gates the call on !DrawingEnabled too; this is the belt.
    if (Store.DrawingEnabled)
        return;
    if (CanvasSize.x < 1.0f || CanvasSize.y < 1.0f)
        return;

    const ImGuiIO& Io = ImGui::GetIO();

    const bool OverCanvas =
        Io.MousePos.x >= CanvasOrigin.x && Io.MousePos.x <= CanvasOrigin.x + CanvasSize.x &&
        Io.MousePos.y >= CanvasOrigin.y && Io.MousePos.y <= CanvasOrigin.y + CanvasSize.y;

    // 🔴 SCREEN-SPACE pick — the perspective-correct twin of the draw-time snap. Picking an existing outline needs NO ground hit: a shape sitting under
    //    the cursor near the horizon is on-screen selectable even where CastCursorToGroundMillimetres returns false, so gate on OverCanvas, not a ground
    //    cast. ResolveScreenSpacePick projects each displayed outline to pixels and measures the cursor's min point-to-segment distance IN PIXELS, so the
    //    catch radius is exactly PickRadiusPixels on screen in every view + zoom — no mm tolerance to collapse in depth (the old X-only offset cast did).
    if (!OverCanvas)
    {
        Store.Hovered = 0;
        return;
    }

    constexpr float PickRadiusPixels = 8.0f;   // [px] - the on-screen catch radius for an outline pick

    // -- Hover every frame: the shape under the cursor (0 = none). The render pass reads Store.Hovered for the dim-green trail. --
    Store.Hovered = ResolveScreenSpacePick(State, Store, CanvasOrigin, CanvasSize, Io.MousePos, PickRadiusPixels);

    // -- A left click resolves the pick and updates the selection. Plain click REPLACES (single-select, or clears on empty); Shift click TOGGLES the
    //    hit into the ordered SelectionSet (add-to / remove-from), leaving Selected == SelectionSet.back() so the primary tracks the last touched. --
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        return;

    const uint32_t Hit = Store.Hovered;   // freshly picked this frame

    if (!Io.KeyShift)
    {
        // Replace: an empty click clears; a hit becomes the sole selection.
        Store.SelectionSet.clear();
        if (Hit != 0)
            Store.SelectionSet.push_back(Hit);
        Store.Selected = Hit;
        return;
    }

    // Shift: toggle the hit. An empty shift-click leaves the set untouched (a stray shift-click never wipes a multi-selection).
    if (Hit == 0)
        return;

    bool Removed = false;
    for (size_t Index = 0; Index < Store.SelectionSet.size(); ++Index)
        if (Store.SelectionSet[Index] == Hit)
        {
            Store.SelectionSet.erase(Store.SelectionSet.begin() + Index);
            Removed = true;
            break;
        }
    if (!Removed)
        Store.SelectionSet.push_back(Hit);

    Store.Selected = Store.SelectionSet.empty() ? 0 : Store.SelectionSet.back();
}


void AdvanceElementSelection(const SketchModelViewportState&       State,
                             Frontier::ParametricSketchShapeStore& Store,
                             bool VertexStratum, bool EdgeStratum,
                             ImVec2 CanvasOrigin, ImVec2 CanvasSize)
{
    // 🔴 Sub-element pick against the ONE selected shape, resolved in SCREEN PIXELS through the same projection the whole-shape pick uses (perspective-
    //    correct, constant on-screen catch radius). The result is written into the store's EXISTING component-selection slots — AlignPickCategory
    //    (0 none / 1 vertex / 2 edge) + AlignPickEndA/EndB (world mm) — so ResolveActiveDimension (and the Properties align path) read one shared window
    //    into the pick with nothing new invented. Cleared to "none" whenever the stratum is WholeShape or no shape is selected, so a stale pick never
    //    keeps feeding the gate a Vertex/Edge dimension after the mode changes.
    if ((!VertexStratum && !EdgeStratum) || Store.Selected == 0 ||
        CanvasSize.x < 1.0f || CanvasSize.y < 1.0f)
    {
        Store.AlignPickCategory = 0;
        return;
    }

    Frontier::ParametricSketchShape* const Shape = Frontier::ResolveParametricSketchShape(Store, Store.Selected);
    if (Shape == nullptr || !Shape->Displayed)
    {
        Store.AlignPickCategory = 0;
        return;
    }

    const ImGuiIO& Io = ImGui::GetIO();
    const bool OverCanvas =
        Io.MousePos.x >= CanvasOrigin.x && Io.MousePos.x <= CanvasOrigin.x + CanvasSize.x &&
        Io.MousePos.y >= CanvasOrigin.y && Io.MousePos.y <= CanvasOrigin.y + CanvasSize.y;
    if (!OverCanvas)
    {
        Store.AlignPickCategory = 0;
        return;
    }

    constexpr float PickRadiusPixels = 9.0f;   // [px] - the on-screen catch radius for a vertex / edge pick (a touch wider than the whole-shape 8)

    const Frontier::Matrix4f ViewProjection = AssembleGroundViewProjection(State);
    ImDrawList* const        Draw           = ImGui::GetWindowDrawList();

    // -- VERTEX stratum: nearest DEFINING point of the selected shape within the pixel radius. The defining Points ARE the grabbable corners (a fillet
    //    keeps the corner one real vertex), so this catches exactly the corners the Fillet / Chamfer ops round. --
    if (VertexStratum)
    {
        int    BestIndex = -1;
        ImVec2 BestPixel(0, 0);
        float  BestScore = PickRadiusPixels * PickRadiusPixels;
        for (size_t Index = 0; Index < Shape->Points.size(); ++Index)
        {
            const ImVec2         Point = Shape->Points[Index];
            const ProjectedPoint P     = ProjectWorldPoint(ViewProjection, CanvasOrigin, CanvasSize, Point.x, Point.y, 0.0f);
            if (!P.InFront)
                continue;
            const float dx = P.Pixel.x - Io.MousePos.x, dy = P.Pixel.y - Io.MousePos.y;
            const float Score = dx * dx + dy * dy;
            if (Score < BestScore)
            {
                BestScore = Score;
                BestIndex = static_cast<int>(Index);
                BestPixel = P.Pixel;
            }
        }

        if (BestIndex < 0)
        {
            Store.AlignPickCategory = 0;
            return;
        }

        // Hover feedback + latch the pick into the shared slots (a vertex leaves EndB == EndA per the slot contract). The green diamond cues the catch.
        DrawSnapMarker(Draw, BestPixel, Frontier::ParametricSketchSnapCategory::Endpoint);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || Store.AlignPickCategory != 1)
        {
            Store.AlignPickCategory = 1;
            Store.AlignPickEndA     = Shape->Points[BestIndex];
            Store.AlignPickEndB     = Shape->Points[BestIndex];
        }
        return;
    }

    // -- EDGE stratum: nearest OUTLINE SEGMENT of the selected shape within the pixel radius. Measured over the cached display outline (so a round
    //    family's edge is a smooth chord run, not just the raw defining polygon), each segment near-plane clipped exactly as the whole-shape pick. --
    const std::vector<ImVec2>& Outline = Frontier::RetrieveCachedOutline(*Shape);
    if (Outline.size() < 2)
    {
        Store.AlignPickCategory = 0;
        return;
    }

    int    BestIndex = -1;
    float  BestScore = PickRadiusPixels * PickRadiusPixels;
    ImVec2 BestA(0, 0), BestB(0, 0);
    const size_t Count = Outline.size();
    const size_t Last  = Shape->ClosedEnabled ? Count : Count - 1;
    for (size_t Index = 0; Index < Last; ++Index)
    {
        const ImVec2 A = Outline[Index];
        const ImVec2 B = Outline[(Index + 1) % Count];
        ClippedEdge Edge;
        if (!ProjectClippedEdge(ViewProjection, CanvasOrigin, CanvasSize, A, B, Edge))
            continue;
        float T = 0.0f;
        const float Score = PixelSegmentDistanceSq(Edge.PixelA, Edge.PixelB, Io.MousePos, T);
        if (Score < BestScore)
        {
            BestScore = Score;
            BestIndex = static_cast<int>(Index);
            BestA     = A;
            BestB     = B;
        }
    }

    if (BestIndex < 0)
    {
        Store.AlignPickCategory = 0;
        return;
    }

    // Hover feedback: re-stroke the caught segment in the snap green (near-plane clipped), and latch the edge into the shared slots on a click.
    ClippedEdge Caught;
    if (ProjectClippedEdge(ViewProjection, CanvasOrigin, CanvasSize, BestA, BestB, Caught))
        Draw->AddLine(Caught.PixelA, Caught.PixelB, ImGui::GetColorU32(ImVec4(0.30f, 0.95f, 0.45f, 1.0f)), 2.6f);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || Store.AlignPickCategory != 2)
    {
        Store.AlignPickCategory = 2;
        Store.AlignPickEndA     = BestA;
        Store.AlignPickEndB     = BestB;
    }
}


bool AdvanceShapePropertiesInvoke(const SketchModelViewportState&                          State,
                                  Frontier::ParametricSketchShapeStore&                    Store,
                                  SceneDirectoryInspectorValidation::InspectorPanelState&  Directory,
                                  const std::vector<std::pair<uint32_t, uint32_t>>&        ShapeRowTokens,
                                  ImVec2 CanvasOrigin, ImVec2 CanvasSize)
{
    namespace SDI = SceneDirectoryInspectorValidation;

    // 🔴 Runs whether or not a Sketch* tool is armed. A right-click that HITS a shape opens its Properties card and (via the panel) SUPPRESSES the draw
    //    for as long as the card is up — the tool stays armed, it just stops seating points until the card closes. On a MISS this returns false and the
    //    panel lets AdvanceShapeDraw take the right-click as the stroke-cancel. So the pick must run FIRST and the draw's cancel branch must not have
    //    already fired: the panel orders this before AdvanceShapeDraw and passes the consume result down as the draw's suppression flag.
    if (CanvasSize.x < 1.0f || CanvasSize.y < 1.0f)
        return false;

    const ImGuiIO& Io = ImGui::GetIO();
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        return false;

    const bool OverCanvas =
        Io.MousePos.x >= CanvasOrigin.x && Io.MousePos.x <= CanvasOrigin.x + CanvasSize.x &&
        Io.MousePos.y >= CanvasOrigin.y && Io.MousePos.y <= CanvasOrigin.y + CanvasSize.y;
    if (!OverCanvas)
        return false;

    // -- Screen-space pick under the cursor, the SAME metric the left-click selection uses (constant on-screen catch radius, perspective-correct). --
    constexpr float PickRadiusPixels = 8.0f;   // [px] - match AdvanceShapeSelection so the hover trail and this pick agree exactly
    const uint32_t  Hit = ResolveScreenSpacePick(State, Store, CanvasOrigin, CanvasSize, Io.MousePos, PickRadiusPixels);
    if (Hit == 0)
        return false;   // empty right-click over the canvas — left for the stroke-cancel / other consumers

    // -- Resolve the picked ShapeId to its outliner row token through the caller's back-map. A miss (shape never mirrored) opens nothing. --
    uint32_t RowToken = 0;
    for (const std::pair<uint32_t, uint32_t>& Pairing : ShapeRowTokens)
        if (Pairing.first == Hit) { RowToken = Pairing.second; break; }
    if (RowToken == 0)
        return false;

    // -- Select the shape in the store too, so it strokes green while its card is up (the render pass reads Store.Selected / SelectionSet). --
    Store.SelectionSet.clear();
    Store.SelectionSet.push_back(Hit);
    Store.Selected = Hit;

    // -- Point the outliner selection at the row so the inspector resolves ITS Properties card, then open the card straight onto the inspect face —
    //    the one-gesture replacement for Tab (open) → click the row (select) → Tab again (slide to inspect). --
    Directory.Directory.SelectionSet.clear();
    Directory.Directory.SelectionSet.push_back(RowToken);
    Directory.Directory.RangeAnchor = RowToken;

    Directory.SummonRequested = true;
    Directory.RequestX        = Io.MousePos.x;
    Directory.RequestY        = Io.MousePos.y;

    // 🔴 The summon applier (ConstructSceneDirectoryInspectorPanel) forces OnInspect = false on every fresh summon ("always open on slide 1"), so
    //    setting OnInspect here would be overwritten. OpenOnInspect is the one-shot request the applier honours AFTER placing the card, landing it
    //    directly on the Properties face for this right-click open. It self-clears there.
    Directory.OpenOnInspect = true;

    return true;   // consumed the right-click: the caller suppresses the stroke-cancel + console for this press
}


//------------------------------------------------------------------------------------------------------------------------
//                                                  BEVEL / CHAMFER CORNER MODAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 The green the pick hover + preview arc stroke in — the same snap-green the sub-element pick uses, so the corner language reads consistently.
    ImU32 ModalInk()      { return ImGui::GetColorU32(ImVec4(0.30f, 0.95f, 0.45f, 1.0f)); }
    ImU32 PreviewInk()    { return ImGui::GetColorU32(ImVec4(1.0f, 0.78f, 0.28f, 1.0f)); }   // the live edit preview (amber, distinct from the committed blue)

    // 📝 Resolve the shape the modal edits: the SELECTED shape when one is selected, else the FIRST displayed shape. Mirrors ResolveActiveDimension's
    //    "selected, else front" fallback so the pick lands on the same shape the Q gate resolved the op against. Null when the store has no shape.
    Frontier::ParametricSketchShape* ResolveModalTargetShape(Frontier::ParametricSketchShapeStore& Store)
    {
        if (Store.Selected != 0)
        {
            Frontier::ParametricSketchShape* const Selected = Frontier::ResolveParametricSketchShape(Store, Store.Selected);
            if (Selected != nullptr && Selected->Displayed)
                return Selected;
        }
        for (Frontier::ParametricSketchShape& Candidate : Store.Shapes)
            if (Candidate.Displayed)
                return &Candidate;
        return nullptr;
    }

    // 📝 Nearest defining corner of Shape to the cursor, in SCREEN PIXELS — the same metric AdvanceElementSelection's vertex stratum uses. Returns the
    //    Points index + its projected pixel, or -1 when none is inside the catch radius. The defining Points ARE the grabbable corners a fillet rounds.
    int NearestCornerVertex(const SketchModelViewportState& State, const Frontier::ParametricSketchShape& Shape,
                            ImVec2 CanvasOrigin, ImVec2 CanvasSize, ImVec2 Cursor, float RadiusPixels, ImVec2& OutPixel)
    {
        const Frontier::Matrix4f ViewProjection = AssembleGroundViewProjection(State);
        int   BestIndex = -1;
        float BestScore = RadiusPixels * RadiusPixels;
        for (size_t Index = 0; Index < Shape.Points.size(); ++Index)
        {
            const ImVec2         Point = Shape.Points[Index];
            const ProjectedPoint P     = ProjectWorldPoint(ViewProjection, CanvasOrigin, CanvasSize, Point.x, Point.y, 0.0f);
            if (!P.InFront)
                continue;
            const float dx = P.Pixel.x - Cursor.x, dy = P.Pixel.y - Cursor.y;
            const float Score = dx * dx + dy * dy;
            if (Score < BestScore)
            {
                BestScore = Score;
                BestIndex = static_cast<int>(Index);
                OutPixel  = P.Pixel;
            }
        }
        return BestIndex;
    }

    // 📝 Stroke the resolved corner-edit preview (world mm) through the shared forward map: a FILLET samples its analytic arc (Centre / Radius / start
    //    + sweep) into a short world-mm polyline and strokes it; a CHAMFER strokes the single TangentA→TangentB edge. Both stroke in the amber preview
    //    ink — a background hint, never a committed line. Also pips the two tangent points so the setback along each leg reads. Drops behind-eye points.
    void StrokeCornerPreview(const SketchModelViewportState& State, ImVec2 CanvasOrigin, ImVec2 CanvasSize,
                             const Frontier::ParametricSketchCornerSolution& Solution,
                             Frontier::ParametricSketchCornerCategory Category, ImDrawList* Draw)
    {
        if (!Solution.Resolved)
            return;

        std::vector<ImVec2> WorldRun;
        if (Category == Frontier::ParametricSketchCornerCategory::Fillet && Solution.Radius > 1e-4f)
        {
            // Sample the analytic arc from StartAngle across SweepAngle. A constant segment budget keeps the preview smooth at any radius (a
            //    background hint, so an exact curve-resolution budget is unnecessary here).
            constexpr int Segments = 24;
            WorldRun.reserve(Segments + 1);
            for (int Step = 0; Step <= Segments; ++Step)
            {
                const float T     = static_cast<float>(Step) / static_cast<float>(Segments);
                const float Angle = Solution.StartAngle + Solution.SweepAngle * T;
                WorldRun.emplace_back(Solution.Centre.x + Solution.Radius * std::cos(Angle),
                                      Solution.Centre.y + Solution.Radius * std::sin(Angle));
            }
        }
        else
        {
            // Chamfer (or a degenerate-radius fillet): the straight setback edge between the two tangent points.
            WorldRun.push_back(Solution.TangentA);
            WorldRun.push_back(Solution.TangentB);
        }

        const Frontier::Matrix4f ViewProjection = AssembleGroundViewProjection(State);
        std::vector<ImVec2> Pixels;
        Pixels.reserve(WorldRun.size());
        for (const ImVec2& Point : WorldRun)
        {
            const ProjectedPoint P = ProjectWorldPoint(ViewProjection, CanvasOrigin, CanvasSize, Point.x, Point.y, 0.0f);
            if (!P.InFront)
                return;                       // any behind-eye vertex drops the whole preview this frame
            Pixels.push_back(P.Pixel);
        }
        if (Pixels.size() >= 2)
            Draw->AddPolyline(Pixels.data(), static_cast<int>(Pixels.size()), PreviewInk(), ImDrawFlags_None, 2.4f);

        // Pip the two tangent (setback) points so the leg trim reads at any magnitude.
        const ProjectedPoint TA = ProjectWorldPoint(ViewProjection, CanvasOrigin, CanvasSize, Solution.TangentA.x, Solution.TangentA.y, 0.0f);
        const ProjectedPoint TB = ProjectWorldPoint(ViewProjection, CanvasOrigin, CanvasSize, Solution.TangentB.x, Solution.TangentB.y, 0.0f);
        if (TA.InFront) Draw->AddCircleFilled(TA.Pixel, 3.0f, PreviewInk());
        if (TB.InFront) Draw->AddCircleFilled(TB.Pixel, 3.0f, PreviewInk());
    }

    // 📝 Paint the short HUD readout pill at the cursor — the live "Fillet R: 12.3 mm" / "Chamfer: 8 mm" string the modal fills each frame. A small
    //    dark rounded rect behind the text so it reads over any grid tone. Suppressed on an empty string (the modal writes '\0' while idle).
    void DrawModalReadout(ImDrawList* Draw, ImVec2 Cursor, const char* Text)
    {
        if (Text == nullptr || Text[0] == '\0')
            return;
        const ImVec2 TextSize = ImGui::CalcTextSize(Text);
        const ImVec2 Pad(7.0f, 4.0f);
        const ImVec2 Origin(Cursor.x + 16.0f, Cursor.y + 16.0f);
        const ImVec2 Min(Origin.x - Pad.x, Origin.y - Pad.y);
        const ImVec2 Max(Origin.x + TextSize.x + Pad.x, Origin.y + TextSize.y + Pad.y);
        Draw->AddRectFilled(Min, Max, ImGui::GetColorU32(ImVec4(0.06f, 0.08f, 0.10f, 0.92f)), 4.0f);
        Draw->AddRect(Min, Max, ImGui::GetColorU32(ImVec4(0.30f, 0.95f, 0.45f, 0.9f)), 4.0f);
        Draw->AddText(Origin, ImGui::GetColorU32(ImVec4(0.94f, 0.97f, 0.95f, 1.0f)), Text);
    }
}


bool AdvanceSketchFilletModal(const SketchModelViewportState&       State,
                              Frontier::ParametricSketchShapeStore& Store,
                              SketchModelFilletModal&               Modal,
                              bool&                                 PickPending,
                              ImVec2 CanvasOrigin, ImVec2 CanvasSize)
{
    if (CanvasSize.x < 1.0f || CanvasSize.y < 1.0f)
        return false;
    if (!PickPending && !Modal.Armed)
        return false;   // idle — the caller does not veto the normal pick / draw

    const ImGuiIO& Io   = ImGui::GetIO();
    ImDrawList*    Draw = ImGui::GetWindowDrawList();

    const bool OverCanvas =
        Io.MousePos.x >= CanvasOrigin.x && Io.MousePos.x <= CanvasOrigin.x + CanvasSize.x &&
        Io.MousePos.y >= CanvasOrigin.y && Io.MousePos.y <= CanvasOrigin.y + CanvasSize.y;

    // A right-click OR Escape abandons the whole gesture (whether still picking or already armed) — the shared cancel edge.
    const bool CancelEdge = ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);

    // ── PHASE 1 — PICK PENDING: no corner armed yet; hover-highlight the nearest corner, a left click Activates the modal on it. ─────────────────
    if (PickPending && !Modal.Armed)
    {
        if (CancelEdge)
        {
            PickPending = false;
            return true;   // owned the press — the caller vetoes the normal pick this frame
        }

        Frontier::ParametricSketchShape* const Shape = ResolveModalTargetShape(Store);
        if (Shape == nullptr)
        {
            PickPending = false;   // no shape to edit (should not happen — the op gated on ≥1 shape) — drop the pick
            return false;
        }

        if (!OverCanvas)
            return true;   // still picking; hold the press away from the canvas without acting

        constexpr float PickRadiusPixels = 10.0f;   // [px] - a touch wider than the sub-element pick, the corner is the whole gesture here
        ImVec2          CornerPixel(0, 0);
        const int       CornerIndex = NearestCornerVertex(State, *Shape, CanvasOrigin, CanvasSize, Io.MousePos, PickRadiusPixels, CornerPixel);

        if (CornerIndex >= 0)
        {
            DrawSnapMarker(Draw, CornerPixel, Frontier::ParametricSketchSnapCategory::Endpoint);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                ActivateSketchModelFilletModal(Modal, Store, Shape->Identifier, CornerIndex);
                PickPending = false;   // whether the arm took (a real corner) or was a no-op (degenerate), the pick phase is over
                return true;
            }
        }
        return true;   // owned the frame while picking (veto the normal selection under the cursor)
    }

    // ── PHASE 2 — ARMED: fold the cursor into a signed magnitude, preview, and commit / cancel through the modal. ────────────────────────────────
    SketchModelFilletModalInput Input;
    Input.CancelPressed   = CancelEdge;
    Input.ConfirmPressed  = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    Input.BackspacePressed = ImGui::IsKeyPressed(ImGuiKey_Backspace, false);

    // Absorb this frame's typed characters (0-9 / '.' / '-') into TypedDigits for the numeric-entry path (an exact amount overrides the drag).
    {
        size_t Written = 0;
        for (int Character = 0; Character < Io.InputQueueCharacters.Size && Written + 1 < sizeof(Input.TypedDigits); ++Character)
        {
            const ImWchar Ch = Io.InputQueueCharacters[Character];
            if ((Ch >= '0' && Ch <= '9') || Ch == '.' || Ch == '-')
                Input.TypedDigits[Written++] = static_cast<char>(Ch);
        }
        Input.TypedDigits[Written] = '\0';
    }

    // Cast the cursor to the ground (authored mm) for the drag projection; hold the last valid point on a near-horizon graze (GroundHit false).
    float GroundMmX = Modal.CornerWorld.x, GroundMmY = Modal.CornerWorld.y;
    const bool GroundHit = CastCursorToGroundMillimetres(State, CanvasOrigin, CanvasSize, Io.MousePos, GroundMmX, GroundMmY);
    Input.CursorMoved = GroundHit;
    Input.CursorX     = GroundMmX;
    Input.CursorY     = GroundMmY;

    // Resolve the preview BEFORE the integrate consumes a confirm (a commit resets the modal, so the last preview would otherwise vanish the same
    //    frame — resolving first paints the shape the click seals). Purely a read; nothing mutates.
    const Frontier::ParametricSketchCornerSolution Preview = ResolveSketchFilletModalPreview(Modal, Store);
    const Frontier::ParametricSketchCornerCategory Category = Modal.Category;

    // Corner marker at the arm-time vertex, so the pivot the edit rotates about stays visible through the drag.
    const ProjectedPoint CornerP = ProjectGroundMillimetres(State, CanvasOrigin, CanvasSize, Modal.CornerWorld.x, Modal.CornerWorld.y);
    if (CornerP.InFront)
        Draw->AddCircle(CornerP.Pixel, 5.0f, ModalInk(), 16, 1.8f);

    StrokeCornerPreview(State, CanvasOrigin, CanvasSize, Preview, Category, Draw);
    DrawModalReadout(Draw, Io.MousePos, Modal.ReadoutText);

    const uint32_t SerialBefore = Modal.CommitSerial;
    const bool     Owned        = IntegrateSketchModelFilletModal(Modal, Store, Input);

    // 🔴 STICKY TOOL: a fresh-corner commit (the serial advanced) re-arms the pick so the Fillet/Chamfer tool stays active for the NEXT corner —
    //    exactly like the sketch draw tools, which stay latched until the tool changes. The gesture ends only on an explicit cancel (right-click /
    //    Escape) during either phase, a new draw tool, or a new op. A cancel-disarm leaves the serial untouched, so it does NOT re-arm.
    if (Owned && Modal.CommitSerial != SerialBefore)
        PickPending = true;

    return Owned;
}


bool AdvanceSketchInsetModal(const SketchModelViewportState&       State,
                             Frontier::ParametricSketchShapeStore& Store,
                             SketchModelInsetModal&                Modal,
                             bool&                                 PickPending,
                             ImVec2 CanvasOrigin, ImVec2 CanvasSize)
{
    if (CanvasSize.x < 1.0f || CanvasSize.y < 1.0f)
        return false;
    if (!PickPending && !Modal.Armed)
        return false;   // idle — the caller does not veto the normal pick / draw

    const ImGuiIO& Io   = ImGui::GetIO();
    ImDrawList*    Draw = ImGui::GetWindowDrawList();

    // A right-click OR Escape abandons the whole gesture (whether still picking or already armed) — the shared cancel edge, exactly like the fillet modal.
    const bool CancelEdge = ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);

    // ── PHASE 1 — PICK PENDING: no shape armed yet; hover-highlight the edge / face under the cursor, a left click Activates the modal on that shape. ──
    if (PickPending && !Modal.Armed)
    {
        if (CancelEdge)
        {
            PickPending = false;
            return true;   // owned the press — the caller vetoes the normal pick this frame
        }

        const bool OverCanvas =
            Io.MousePos.x >= CanvasOrigin.x && Io.MousePos.x <= CanvasOrigin.x + CanvasSize.x &&
            Io.MousePos.y >= CanvasOrigin.y && Io.MousePos.y <= CanvasOrigin.y + CanvasSize.y;
        if (!OverCanvas)
            return true;   // still picking; hold the press away from the canvas without acting

        // The shape under the cursor (screen-space outline pick, same catch radius the idle selection uses). Highlight it so the user sees what a click
        //    will offset, then a left click arms the drag on it. The pick anchor is the click's ground point (the drag's zero-distance origin).
        constexpr float          PickRadiusPixels = 8.0f;   // [px] - the on-screen catch radius for the outline pick
        const uint32_t           Hit = ResolveScreenSpacePick(State, Store, CanvasOrigin, CanvasSize, Io.MousePos, PickRadiusPixels);
        if (Hit != 0)
        {
            // Stroke the hovered outline in the guide tint so the target reads before the click (the Fillet corner marker's whole-shape twin).
            const Frontier::Matrix4f       ViewProjection = AssembleGroundViewProjection(State);
            const ImU32                    HoverInk       = ImGui::GetColorU32(ImVec4(0.36f, 0.62f, 1.0f, 0.9f));
            Frontier::ParametricSketchShape* const HoverShape = Frontier::ResolveParametricSketchShape(Store, Hit);
            if (HoverShape != nullptr)
            {
                const std::vector<ImVec2>& Outline = Frontier::RetrieveCachedOutline(*HoverShape);
                const size_t Count = Outline.size();
                const size_t Last  = HoverShape->ClosedEnabled ? Count : (Count > 0 ? Count - 1 : 0);
                for (size_t Index = 0; Index < Last; ++Index)
                {
                    const ImVec2 A = Outline[Index];
                    const ImVec2 B = Outline[(Index + 1) % Count];
                    ClippedEdge  Edge;
                    if (ProjectClippedEdge(ViewProjection, CanvasOrigin, CanvasSize, A, B, Edge))
                        Draw->AddLine(Edge.PixelA, Edge.PixelB, HoverInk, 2.5f);
                }
            }

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                float GroundMmX = 0.0f, GroundMmY = 0.0f;
                CastCursorToGroundMillimetres(State, CanvasOrigin, CanvasSize, Io.MousePos, GroundMmX, GroundMmY);
                ActivateSketchModelInsetModalOnShape(Modal, Store, Hit, GroundMmX, GroundMmY);
                PickPending = false;   // whether the arm took (a live shape) or was a no-op (vanished), the pick phase is over
                return true;
            }
        }
        return true;   // owned the frame while picking (veto the normal selection under the cursor)
    }

    // Assemble the per-frame input the same way the fillet modal does: cursor → ground mm, confirm/cancel edges, typed digits, backspace.
    SketchModelInsetModalInput Input;
    Input.CancelPressed    = ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    Input.ConfirmPressed   = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                             ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    Input.BackspacePressed = ImGui::IsKeyPressed(ImGuiKey_Backspace, false);

    {
        size_t Written = 0;
        for (int Character = 0; Character < Io.InputQueueCharacters.Size && Written + 1 < sizeof(Input.TypedDigits); ++Character)
        {
            const ImWchar Ch = Io.InputQueueCharacters[Character];
            if ((Ch >= '0' && Ch <= '9') || Ch == '.' || Ch == '-')
                Input.TypedDigits[Written++] = static_cast<char>(Ch);
        }
        Input.TypedDigits[Written] = '\0';
    }

    float GroundMmX = Modal.AnchorWorld.x, GroundMmY = Modal.AnchorWorld.y;
    const bool GroundHit = CastCursorToGroundMillimetres(State, CanvasOrigin, CanvasSize, Io.MousePos, GroundMmX, GroundMmY);
    Input.CursorMoved = GroundHit;
    Input.CursorX     = GroundMmX;
    Input.CursorY     = GroundMmY;

    // Resolve the preview BEFORE the integrate consumes a confirm (a commit resets the modal, so the last preview would otherwise vanish the same
    //    frame). Pure read; nothing mutates. Stroke each offset loop in the guide tint so the offset outline reads as a live guide, not placed geometry.
    const std::vector<SketchInsetPreviewLoop> Preview = ResolveSketchInsetModalPreview(Modal, Store);
    const ImU32 PreviewInk = ImGui::GetColorU32(ImVec4(0.36f, 0.62f, 1.0f, 0.9f));
    for (const SketchInsetPreviewLoop& Loop : Preview)
    {
        ImVec2 First(0, 0);
        ImVec2 Prev(0, 0);
        bool   HaveFirst = false, HavePrev = false;
        for (const ImVec2& Point : Loop.Points)
        {
            const ProjectedPoint P = ProjectGroundMillimetres(State, CanvasOrigin, CanvasSize, Point.x, Point.y);
            if (!P.InFront) { HavePrev = false; continue; }
            if (HavePrev) Draw->AddLine(Prev, P.Pixel, PreviewInk, 2.0f);
            if (!HaveFirst) { First = P.Pixel; HaveFirst = true; }
            Prev = P.Pixel;
            HavePrev = true;
        }
        // Close the loop ONLY for a closed Profile preview — an open parallel curve must not draw a chord back to its start.
        if (Loop.Closed && HaveFirst && HavePrev) Draw->AddLine(Prev, First, PreviewInk, 2.0f);
    }

    DrawModalReadout(Draw, Io.MousePos, Modal.ReadoutText);

    const uint32_t SerialBefore = Modal.CommitSerial;
    const bool     Owned        = IntegrateSketchModelInsetModal(Modal, Store, Input);

    // 🔴 STICKY TOOL: a fresh commit (the serial advanced) re-raises the PICK so the Offset tool stays active for the NEXT edge / face — exactly like the
    //    sticky fillet re-raises its corner pick. The integrate already disarmed the modal on commit, so re-raising PickPending drops us back to PHASE 1;
    //    the next click picks a fresh target. A cancel-disarm leaves the serial untouched, so it does NOT re-raise.
    if (Owned && Modal.CommitSerial != SerialBefore)
        PickPending = true;

    return Owned;
}


//------------------------------------------------------------------------------------------------------------------------
//                                    THE 2D MODIFY COMMAND TOOLS — Trim / Cut / Join / Remove
//------------------------------------------------------------------------------------------------------------------------
// 📝 One sticky click-to-apply driver for the four command tools (no drag, unlike Fillet/Chamfer). Each hovers a target under the cursor
//    (screen-space pick, constant on-screen catch radius) and, on a left click, APPLIES the tool's verb through the store; the tool STAYS ARMED
//    so the next target can be operated. Trim additionally strokes the exact span its click would remove BEFORE the click (ResolveTrimPreviewSpan).
//    Join grows a pick SET one click at a time and commits at ≥2 operands. Escape / right-click releases the tool.
namespace
{
    // The store's live command tool ↔ the store's SketchCommandTool enum. Names only; the store owns the verbs.
    const char* CommandToolLabel(SketchCommandTool Tool)
    {
        switch (Tool)
        {
            case SketchCommandTool::Trim:   return "Trim";
            case SketchCommandTool::Cut:    return "Cut";
            case SketchCommandTool::Join:   return "Join";
            case SketchCommandTool::Remove: return "Remove";
            case SketchCommandTool::Extend: return "Extend";
            default:                        return "";
        }
    }

    // Mirror Join's growing operand set into the store's SelectionSet so RenderSketchModelShapes strokes each picked shape green. Selected tracks the
    //    back() the same way a shift-pick multi-selection does, so the render highlight and the store invariant agree.
    void EchoJoinPicksIntoSelection(Frontier::ParametricSketchShapeStore& Store, const std::vector<uint32_t>& Picks)
    {
        Store.SelectionSet = Picks;
        Store.Selected     = Picks.empty() ? 0u : Picks.back();
    }
}


void ArmSketchCommandTool(SketchModelCommandToolState&          Tools,
                          Frontier::ParametricSketchShapeStore& Store,
                          SketchCommandTool                     Tool)
{
    // A command click must never ALSO seat a draw point or fold the fillet modal — clear the draw arm the same way ArmShapeDraw's peers do.
    Store.DrawingEnabled = false;
    Store.PendingPoints.clear();

    // Dropping any prior tool's pick set (and its selection echo) before arming the next, so a leftover Join operand never rides into a new tool.
    Tools.JoinPicks.clear();
    Store.SelectionSet.clear();
    Store.Selected = 0;

    Tools.Armed = Tool;   // arming None disarms; the retained Last* / ApplySerial are left untouched (a log edge / box in flight is not lost)
}


void ReleaseSketchCommandTool(SketchModelCommandToolState&          Tools,
                              Frontier::ParametricSketchShapeStore& Store)
{
    if (Tools.Armed == SketchCommandTool::None)
        return;   // already idle

    Tools.Armed = SketchCommandTool::None;
    Tools.JoinPicks.clear();
    Store.SelectionSet.clear();   // clear the echo Join grew; does NOT undo applied geometry
    Store.Selected = 0;
    // The retained Last* is deliberately kept — the caller's History-log edge already consumed it off ApplySerial.
}


bool SketchCommandToolActive(const SketchModelCommandToolState& Tools)
{
    return Tools.Armed != SketchCommandTool::None;
}


bool AdvanceSketchCommandTools(const SketchModelViewportState&       State,
                               Frontier::ParametricSketchShapeStore& Store,
                               SketchModelCommandToolState&          Tools,
                               ImVec2 CanvasOrigin, ImVec2 CanvasSize)
{
    if (CanvasSize.x < 1.0f || CanvasSize.y < 1.0f)
        return false;
    if (Tools.Armed == SketchCommandTool::None)
        return false;   // idle — the caller does not veto the normal pick / draw

    const ImGuiIO& Io   = ImGui::GetIO();
    ImDrawList*    Draw = ImGui::GetWindowDrawList();

    const bool OverCanvas =
        Io.MousePos.x >= CanvasOrigin.x && Io.MousePos.x <= CanvasOrigin.x + CanvasSize.x &&
        Io.MousePos.y >= CanvasOrigin.y && Io.MousePos.y <= CanvasOrigin.y + CanvasSize.y;

    // Escape / right-click RELEASES the tool (the sticky exit) — the same shared cancel edge the fillet modal reads.
    const bool CancelEdge = ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (CancelEdge)
    {
        ReleaseSketchCommandTool(Tools, Store);
        return true;   // owned the press — the caller vetoes the normal pick / stroke-cancel this frame
    }

    if (!OverCanvas)
        return true;   // still armed; hold the press away from the canvas without acting

    // HOVER-PICK the shape under the cursor in screen space (constant on-screen catch radius), so the render pass trails it in dim green.
    constexpr float PickRadiusPixels = 9.0f;   // [px] - matches the whole-shape selection radius
    const uint32_t  Hit = ResolveScreenSpacePick(State, Store, CanvasOrigin, CanvasSize, Io.MousePos, PickRadiusPixels);
    Store.Hovered = Hit;

    // The world-mm cursor drives the section / split location for the store verbs, plus an on-screen mm tolerance at the pick anchor.
    float GroundMmX = 0.0f, GroundMmY = 0.0f;
    const bool GroundHit = CastCursorToGroundMillimetres(State, CanvasOrigin, CanvasSize, Io.MousePos, GroundMmX, GroundMmY);
    const ImVec2 CursorMm(GroundMmX, GroundMmY);

    // Convert the pixel catch radius to an mm tolerance at the hit shape's anchor, so the verb's mm-radius test agrees with the screen-space pick.
    float ToleranceMm = 0.5f;   // fallback when the projection is degenerate / nothing hovered
    if (Hit != 0)
    {
        const Frontier::ParametricSketchShape* HitShape = Frontier::ResolveParametricSketchShape(Store, Hit);
        if (HitShape != nullptr)
        {
            const Frontier::Matrix4f ViewProjection = AssembleGroundViewProjection(State);
            const float PixelsPerMm = EstimatePixelsPerMm(ViewProjection, CanvasOrigin, CanvasSize, ResolveShapeScaleAnchor(*HitShape));
            if (PixelsPerMm > 1e-4f)
                ToleranceMm = PickRadiusPixels / PixelsPerMm;
        }
    }

    const bool ClickEdge = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    // ── TRIM — stroke the doomed span under the cursor, then a click excises it. ─────────────────────────────────────────────────────────────────
    if (Tools.Armed == SketchCommandTool::Trim)
    {
        if (Hit != 0 && GroundHit)
        {
            std::vector<ImVec2> Span;
            if (Frontier::ResolveTrimPreviewSpan(Store, Hit, CursorMm, ToleranceMm, Span) && Span.size() >= 2)
            {
                // Stroke the span that a click would remove in the warning tint, so the cut is visible first.
                const ImU32 WarnInk = ImGui::GetColorU32(ImVec4(1.0f, 0.42f, 0.32f, 0.95f));
                ImVec2 Prev(0, 0);
                bool   HavePrev = false;
                for (const ImVec2& Point : Span)
                {
                    const ProjectedPoint P = ProjectGroundMillimetres(State, CanvasOrigin, CanvasSize, Point.x, Point.y);
                    if (!P.InFront) { HavePrev = false; continue; }
                    if (HavePrev)
                        Draw->AddLine(Prev, P.Pixel, WarnInk, 3.0f);
                    Prev = P.Pixel;
                    HavePrev = true;
                }
            }
        }

        if (ClickEdge && Hit != 0 && GroundHit)
        {
            if (Frontier::PartitionShapeSegment(Store, Hit, CursorMm, ToleranceMm))
            {
                ++Tools.ApplySerial;
                Tools.LastLabel    = CommandToolLabel(SketchCommandTool::Trim);
                Tools.LastShapeId  = Hit;
                Tools.LastOperands = 1;
            }
            return true;   // owned the press whether or not it landed on the outline
        }
        return true;
    }

    // ── EXTEND — stroke the span the click would ADD (the end out to the first curve ahead), then a click grows it. Trim's mirror image. ─────────
    if (Tools.Armed == SketchCommandTool::Extend)
    {
        if (Hit != 0 && GroundHit)
        {
            std::vector<ImVec2> Span;
            if (Frontier::ResolveExtendPreviewSpan(Store, Hit, CursorMm, ToleranceMm, Span) && Span.size() >= 2)
            {
                // Stroke the ADDED span in the guide tint (not Trim's warning red — this grows geometry rather than removing it) and mark the endpoint
                //    the extension would land on, so both the direction and the boundary it stops at read before the click.
                const ImU32 GuideInk = ImGui::GetColorU32(ImVec4(0.36f, 0.82f, 0.55f, 0.95f));
                ImVec2 Prev(0, 0);
                bool   HavePrev = false;
                for (const ImVec2& Point : Span)
                {
                    const ProjectedPoint P = ProjectGroundMillimetres(State, CanvasOrigin, CanvasSize, Point.x, Point.y);
                    if (!P.InFront) { HavePrev = false; continue; }
                    if (HavePrev)
                        Draw->AddLine(Prev, P.Pixel, GuideInk, 3.0f);
                    Prev = P.Pixel;
                    HavePrev = true;
                }
                const ProjectedPoint Landing = ProjectGroundMillimetres(State, CanvasOrigin, CanvasSize, Span.back().x, Span.back().y);
                if (Landing.InFront)
                    Draw->AddCircle(Landing.Pixel, 5.0f, GuideInk, 16, 2.0f);
            }
        }

        if (ClickEdge && Hit != 0 && GroundHit)
        {
            if (Frontier::ExtendShapeToBoundary(Store, Hit, CursorMm, ToleranceMm))
            {
                ++Tools.ApplySerial;
                Tools.LastLabel    = CommandToolLabel(SketchCommandTool::Extend);
                Tools.LastShapeId  = Hit;
                Tools.LastOperands = 1;
            }
            return true;   // owned the press whether or not it landed on an extendable end
        }
        return true;
    }

    // ── CUT — a click splits the picked curve at the clicked point. ──────────────────────────────────────────────────────────────────────────────
    if (Tools.Armed == SketchCommandTool::Cut)
    {
        if (ClickEdge && Hit != 0 && GroundHit)
        {
            if (Frontier::CutShapeAtPoint(Store, Hit, CursorMm, ToleranceMm))
            {
                ++Tools.ApplySerial;
                Tools.LastLabel    = CommandToolLabel(SketchCommandTool::Cut);
                Tools.LastShapeId  = Hit;
                Tools.LastOperands = 1;
            }
            return true;
        }
        return true;
    }

    // ── REMOVE — a click detaches the picked shape. ─────────────────────────────────────────────────────────────────────────────────────────────
    if (Tools.Armed == SketchCommandTool::Remove)
    {
        if (ClickEdge && Hit != 0)
        {
            Frontier::DetachParametricSketchShape(Store, Hit);
            ++Tools.ApplySerial;
            Tools.LastLabel    = CommandToolLabel(SketchCommandTool::Remove);
            Tools.LastShapeId  = Hit;
            Tools.LastOperands = 1;
            Store.Hovered = 0;   // the shape is gone; drop the stale hover so the trail does not point at a detached id
            return true;
        }
        return true;
    }

    // ── JOIN — grow the operand set one click at a time; commit at ≥2. ───────────────────────────────────────────────────────────────────────────
    if (Tools.Armed == SketchCommandTool::Join)
    {
        if (ClickEdge)
        {
            if (Hit != 0)
            {
                // Add the hit to the ordered set (ignore a re-click on an already-picked shape — no duplicates, per the boolean invariant).
                const bool Already = std::find(Tools.JoinPicks.begin(), Tools.JoinPicks.end(), Hit) != Tools.JoinPicks.end();
                if (!Already)
                    Tools.JoinPicks.push_back(Hit);
                EchoJoinPicksIntoSelection(Store, Tools.JoinPicks);

                // At ≥2 operands, COMMIT: any closed operand routes to the boolean Union; an all-open set welds end-to-end. The store's SelectionSet
                //    (mirrored above) is the operand list both verbs read.
                if (Tools.JoinPicks.size() >= 2)
                {
                    bool AnyClosed = false;
                    for (uint32_t Id : Tools.JoinPicks)
                    {
                        const Frontier::ParametricSketchShape* S = Frontier::ResolveParametricSketchShape(Store, Id);
                        if (S != nullptr && S->ClosedEnabled) { AnyClosed = true; break; }
                    }

                    bool Committed = false;
                    if (AnyClosed)
                        Committed = (Frontier::AppendBooleanResult(Store, Frontier::BooleanCategory::Union) ==
                                     Frontier::BooleanOutcome::Committed);
                    else
                        Committed = Frontier::AssembleOpenShapes(Store, Tools.WeldToleranceMm);

                    if (Committed)
                    {
                        ++Tools.ApplySerial;
                        Tools.LastLabel    = CommandToolLabel(SketchCommandTool::Join);
                        Tools.LastShapeId  = Store.Selected;   // the surviving / base id the store reselected
                        Tools.LastOperands = static_cast<int>(Tools.JoinPicks.size());
                    }

                    // Whether or not it committed, the set clears and the tool STAYS ARMED for the next join.
                    Tools.JoinPicks.clear();
                    Store.SelectionSet.clear();
                    if (!Committed)
                        Store.Selected = 0;
                }
            }
            else
            {
                // An empty click during a Join drops the in-progress set (the "start over" gesture) without leaving the tool.
                Tools.JoinPicks.clear();
                Store.SelectionSet.clear();
                Store.Selected = 0;
            }
            return true;
        }
        return true;
    }

    return true;   // armed but no click this frame — still owns the canvas (veto the normal pick under the cursor)
}

}   // namespace SketchModelViewportValidation
