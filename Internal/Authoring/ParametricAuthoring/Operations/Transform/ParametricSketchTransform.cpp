//==========================================================================================================================================
//                                                            ParametricSketchTransform.cpp
//==========================================================================================================================================
// 🧩 2D construction transforms for the parametricSketching workspace. Offset: each selected closed shape is flattened densely (Clipper2
//    is a straight-segment offsetter — an analytic arc must enter as a polyline), inflated / deflated by a signed world-mm distance through
//    Clipper2's ClipperOffset (outer + hole rings fed jointly, so a punched region's voids survive), and the surviving outer loops are
//    re-emitted as analytic Profiles (contained holes carried), the originals kept. The Clipper2 conversion + winding conventions mirror
//    ParametricSketchBoolean.cpp so both engines read the same loop orientation (outer CCW, hole CW). Mirror reflects each selected shape
//    across its datum's active axis (ConstructMirrorCopy: clone-then-flip, never a re-solve); Array stamps linear / radial / instance-on-points
//    copies (ConstructArrayCopy).

#include "ParametricSketchTransform.h"

#include <algorithm>   // 📝 std::reverse — orient the offset contours
#include <cmath>       // 📝 std::fabs — sliver-area cutoff
#include <cstdio>      // 📝 std::snprintf — seed the Profile title
#include <cstdint>

#include "clipper2/clipper.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        GEOMETRY HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr float  AreaEps          = 1e-6f;   // [mm²] - drop loops below this magnitude as slivers
    constexpr double OffsetPrecision  = 4;       // [-]   - Clipper2 decimal precision: 1e-4 mm (0.1 µm) fixed-point grid (matches boolean)
    constexpr int    OffsetFlattenBudget = 256;  // [-]   - segments per curved operand fed to the offsetter (circle → 256-gon)

    // 📝 Twice-signed shoelace area; positive when the loop winds counter-clockwise in world mm. (Same convention as ParametricSketchBoolean.)
    float LoopTwiceArea(const std::vector<ImVec2>& Loop)
    {
        const size_t Count = Loop.size();
        if (Count < 3)
            return 0.0f;
        float Sum = 0.0f;
        for (size_t Index = 0; Index < Count; ++Index)
        {
            const ImVec2& Current = Loop[Index];
            const ImVec2& Next     = Loop[(Index + 1) % Count];
            Sum += Current.x * Next.y - Next.x * Current.y;
        }
        return Sum;
    }

    // 📝 Even-odd raycast containment: is (X,Y) inside the loop Points? Used to nest offset holes under their outer.
    bool PointInsideLoop(float X, float Y, const std::vector<ImVec2>& Points)
    {
        bool Inside = false;
        const size_t Count = Points.size();
        for (size_t Index = 0, Previous = Count - 1; Index < Count; Previous = Index++)
        {
            const float Xi = Points[Index].x,    Yi = Points[Index].y;
            const float Xj = Points[Previous].x, Yj = Points[Previous].y;
            if (((Yi > Y) != (Yj > Y)) && (X < (Xj - Xi) * (Y - Yi) / (Yj - Yi) + Xi))
                Inside = !Inside;
        }
        return Inside;
    }

    // 📝 Convert one world-mm loop into a Clipper2 double path (no repeated closing point; Clipper2 treats every path as closed).
    Clipper2Lib::PathD ConvertLoopToPath(const std::vector<ImVec2>& Loop)
    {
        Clipper2Lib::PathD Path;
        Path.reserve(Loop.size());
        for (const ImVec2& Point : Loop)
            Path.emplace_back((double)Point.x, (double)Point.y);
        return Path;
    }

    // 📝 Convert one Clipper2 double path back to a world-mm loop, forcing the requested winding (outer CCW, hole CW). Clipper2's
    //    positive-Y-down convention matches our mm shoelace, so a positive signed area is CCW here too.
    std::vector<ImVec2> ConvertPathToLoop(const Clipper2Lib::PathD& Path, bool CounterClockwiseEnabled)
    {
        std::vector<ImVec2> Loop;
        Loop.reserve(Path.size());
        for (const Clipper2Lib::PointD& Point : Path)
            Loop.emplace_back((float)Point.x, (float)Point.y);
        if (Loop.size() >= 3)
        {
            const bool CurrentlyCounterClockwise = LoopTwiceArea(Loop) >= 0.0f;
            if (CurrentlyCounterClockwise != CounterClockwiseEnabled)
                std::reverse(Loop.begin(), Loop.end());
        }
        return Loop;
    }

    // 📝 Split a raw loop set into outer loops (CCW, positive area) and hole loops (CW, negative area). Mirrors ParametricSketchBoolean.
    void PartitionOutersAndHoles(const std::vector<std::vector<ImVec2>>& Loops,
                                 std::vector<std::vector<ImVec2>>&        Outers,
                                 std::vector<std::vector<ImVec2>>&        Holes)
    {
        for (const auto& Loop : Loops)
        {
            if (Loop.size() < 3)
                continue;
            if (LoopTwiceArea(Loop) >= 0.0f)
                Outers.push_back(Loop);
            else
                Holes.push_back(Loop);
        }
    }

    void RaiseNotice(ParametricSketchShapeStore& Store, const char* Message)
    {
        std::snprintf(Store.Notice, sizeof(Store.Notice), "%s", Message);
        Store.NoticeTimer = 4.0f;   // [s] - the view decays this each frame
    }

    // 📝 Reflect Point across the line through Anchor whose UNIT normal is AxisNormal: P' = P - 2·(AxisNormal·(P-Anchor))·AxisNormal. The
    //    axis line runs perpendicular to AxisNormal, so a mirror across the datum's HORIZONTAL axis passes the vertical normal, and vice-versa.
    ImVec2 ReflectPoint(ImVec2 Point, ImVec2 Anchor, ImVec2 AxisNormal)
    {
        const float OffsetX = Point.x - Anchor.x;
        const float OffsetY = Point.y - Anchor.y;
        const float NormalProjection = OffsetX * AxisNormal.x + OffsetY * AxisNormal.y;
        return ImVec2(Point.x - 2.0f * NormalProjection * AxisNormal.x,
                      Point.y - 2.0f * NormalProjection * AxisNormal.y);
    }

    // 📝 Build one mirror copy of Source reflected across the datum line (Anchor + unit AxisNormal). Clone the source's EXACT solved geometry —
    //    never re-solve through ConstructParametricSketchShape, which snaps a per-vertex-edited or rotated shape back to its category's ideal
    //    form (a dragged / rotated Rectangle collapses to its axis-aligned bounding box). A reflection is an IMPROPER (determinant-negative)
    //    affine, so every defining point + hole point reflects across the line, and a CLOSED loop's order REVERSES to restore its winding (a
    //    reflection flips CCW↔CW; reversing restores outer-CCW / hole-CW). The solved scalars carry through the reflection — Centre reflects and
    //    the orientation angles transform as α → 2φ − α + 180° where φ = the line normal's angle (the exact map ReflectPoint applies) — so a
    //    filled circle / arc / ellipse / polygon copy stays exact without a re-solve. Carries tint / folder / elevation / fill; a corner fillet
    //    survives too, its CornerIndex remapped under the reversal (original corner i sits at N−1−i). Returns the sealed copy (no id).
    ParametricSketchShape ConstructMirrorCopy(const ParametricSketchShape& Source, ImVec2 Anchor, ImVec2 AxisNormal)
    {
        ParametricSketchShape Fresh = Source;
        Fresh.MirrorSource    = 0;   // a clone carries none of the source's driven-link identity — the caller re-stamps the mirror link fields
        Fresh.MirrorAxisIndex = 0;
        Fresh.ArraySource     = 0;
        Fresh.ArrayMode       = 0;
        Fresh.ArrayInstance   = 0;
        Fresh.ArrayEnabled    = 0;   // the clone must never itself drive an array (that would chain drivers / recurse the reflow)
        Fresh.DatumEnabled    = false;

        for (ImVec2& Point : Fresh.Points)
            Point = ReflectPoint(Point, Anchor, AxisNormal);
        if (Source.ClosedEnabled)
            std::reverse(Fresh.Points.begin(), Fresh.Points.end());   // a reflection flips winding — reversing restores the outer-CCW loop
        for (std::vector<ImVec2>& Hole : Fresh.HoleLoops)
        {
            for (ImVec2& Point : Hole)
                Point = ReflectPoint(Point, Anchor, AxisNormal);
            std::reverse(Hole.begin(), Hole.end());   // holes are closed loops — restore CW winding after the reflection
        }

        // Carry the solved scalars through the reflection so the round / curve families stay exact without a re-solve. ReflectPoint reflects a
        //    vector at angle α to 2φ − α + 180° (φ = the line normal's angle), so the orientation angles transform by that same rule.
        Fresh.Centre = ReflectPoint(Source.Centre, Anchor, AxisNormal);
        const float LineNormalAngle = std::atan2(AxisNormal.y, AxisNormal.x);              // [rad] - φ, the reflection line normal's angle
        const float ReflectedAngle  = 2.0f * LineNormalAngle + 3.14159265f;                // [rad] - the α → 2φ − α + 180° constant
        if (Source.ClosedEnabled)
        {
            Fresh.StartAngle = ReflectedAngle - Source.StartAngle;
            Fresh.Rotation   = ReflectedAngle - Source.Rotation;
        }
        else
        {
            // An OPEN run keeps its traversal order (no winding to restore); a CCW arc's reflection is a CW arc, so its parameterization reads
            //    from the OTHER end: the sweep end angle flips the sign of the start under the reflection, sweep magnitude unchanged.
            Fresh.StartAngle = ReflectedAngle - (Source.StartAngle + Source.SweepAngle);
            Fresh.Rotation   = ReflectedAngle - Source.Rotation;
        }
        // Radius / MajorAxis / MinorAxis / SweepAngle / Rho / Degree / SideCount are reflection-invariant magnitudes — kept from the clone as-is.

        // A corner fillet survives, but the closed-loop reversal remaps its corner: original corner i sits at index N−1−i in the reversed loop.
        if (Source.ClosedEnabled && Fresh.Points.size() >= 2)
        {
            const size_t Count = Fresh.Points.size();
            for (ParametricSketchCornerFillet& Fillet : Fresh.CornerFillets)
                if (Fillet.CornerIndex >= 0 && Fillet.CornerIndex < (int)Count)
                    Fillet.CornerIndex = (int)(Count - 1 - Fillet.CornerIndex);
        }

        Fresh.Displayed          = true;
        Fresh.CachedOutlineValid = false;
        return Fresh;
    }

    // 📝 One reflection a datum emits: which axis produced it (AxisIndex 0 = across the horizontal line, 1 = across the vertical line) and the
    //    unit line normal to reflect along. A mirror ACROSS the horizontal axis reflects along the VERTICAL normal, and vice-versa.
    struct MirrorAxis
    {
        int    AxisIndex;   // [-] - 0 = horizontal-line reflection, 1 = vertical-line reflection
        ImVec2 Normal;      // [-] - unit normal of the reflection line
    };

    // 📝 Resolve which reflections a shape's datum emits from its axis choice + rotation (None → none; Horizontal / Vertical → one; Cross →
    //    both, a 4-up with the original). The horizontal axis direction is (cos R, sin R); its normal (reflect across horizontal) is (-sin R,
    //    cos R). The vertical axis is the perpendicular; its normal (reflect across vertical) is (cos R, sin R). Shared by AppendMirrorResult
    //    (fresh copies) and ReflowMirrorChildren (rebuild). Returns fewer entries when the axis is None / a single axis — the caller matches
    //    a child's MirrorAxisIndex against AxisIndex, so a Cross→single downgrade orphans the now-absent copy.
    std::vector<MirrorAxis> ResolveDatumAxisNormals(const ParametricSketchShape& Shape)
    {
        std::vector<MirrorAxis> Result;
        const float AxisCos = std::cos(Shape.DatumRotation);
        const float AxisSin = std::sin(Shape.DatumRotation);
        const ImVec2 HorizontalNormal(-AxisSin, AxisCos);   // normal to the horizontal axis (reflect across horizontal → use this)
        const ImVec2 VerticalNormal(AxisCos, AxisSin);      // normal to the vertical axis   (reflect across vertical   → use this)
        if (Shape.DatumAxis == ParametricSketchDatumAxis::Horizontal || Shape.DatumAxis == ParametricSketchDatumAxis::Cross)
            Result.push_back({ 0, HorizontalNormal });
        if (Shape.DatumAxis == ParametricSketchDatumAxis::Vertical || Shape.DatumAxis == ParametricSketchDatumAxis::Cross)
            Result.push_back({ 1, VerticalNormal });
        return Result;
    }

    // 📝 Affine (translate + rotate about a pivot) copy of Source — the ARRAY analogue of ConstructMirrorCopy, but orientation-PRESERVING (a
    //    rotation + translation is determinant-positive), so NO winding flip: every defining point + hole point maps P' = Pivot + R·(P - Pivot)
    //    + Offset with R = [[Cos,-Sin],[Sin,Cos]]. Re-solves from the mapped points via ConstructParametricSketchShape so every analytic scalar rebuilds,
    //    tracking a source category change. Carries tint / folder / elevation / fill; corner fillets are dropped this pass (like the mirror copy).
    ImVec2 ApplyArrayAffine(ImVec2 Point, ImVec2 Pivot, float CosAngle, float SinAngle, ImVec2 Offset)
    {
        const float LocalX = Point.x - Pivot.x;
        const float LocalY = Point.y - Pivot.y;
        return ImVec2(Pivot.x + CosAngle * LocalX - SinAngle * LocalY + Offset.x,
                      Pivot.y + SinAngle * LocalX + CosAngle * LocalY + Offset.y);
    }

    ParametricSketchShape ConstructArrayCopy(const ParametricSketchShape& Source, ImVec2 Pivot, float CosAngle, float SinAngle, ImVec2 Offset)
    {
        // Clone the source's EXACT solved geometry (never re-solve through ConstructParametricSketchShape — that snaps a per-vertex-edited or rotated
        //    shape back to its category's ideal form, e.g. a dragged / rotated Rectangle collapses to its axis-aligned bounding box, so only
        //    dimensions would track, not the true outline). A copy is a rigid affine of what the source LOOKS like right now: transform every
        //    defining point + hole point, and carry the solved scalars through the SAME affine (Centre translates + rotates; the round/ellipse
        //    angles add the rotation) so a filled circle / arc / ellipse copy stays exact without a re-solve. Orientation-preserving → no flip.
        ParametricSketchShape Fresh = Source;
        Fresh.MirrorSource    = 0;   // a clone carries none of the source's driven-link identity — the reflow re-stamps the array link fields
        Fresh.MirrorAxisIndex = 0;
        Fresh.ArraySource     = 0;
        Fresh.ArrayMode       = 0;
        Fresh.ArrayInstance   = 0;
        Fresh.ArrayEnabled    = 0;   // the clone must never itself drive an array (that would chain drivers / recurse the reflow)
        Fresh.DatumEnabled    = false;

        for (ImVec2& Point : Fresh.Points)
            Point = ApplyArrayAffine(Point, Pivot, CosAngle, SinAngle, Offset);
        for (std::vector<ImVec2>& Hole : Fresh.HoleLoops)
            for (ImVec2& Point : Hole)
                Point = ApplyArrayAffine(Point, Pivot, CosAngle, SinAngle, Offset);

        // Carry the solved analytic scalars through the affine so the round / arc / ellipse families flatten from the transformed definition.
        Fresh.Centre = ApplyArrayAffine(Source.Centre, Pivot, CosAngle, SinAngle, Offset);
        const float AddedAngle = std::atan2(SinAngle, CosAngle);   // the rotation the affine applies (0 for linear / instance-on-points)
        Fresh.StartAngle += AddedAngle;   // arc start orientation rotates with the copy
        Fresh.Rotation   += AddedAngle;   // ellipse / polygon major-axis orientation rotates with the copy
        // Radius / MajorAxis / MinorAxis / SweepAngle / Rho / Degree / SideCount are rotation-invariant magnitudes — kept from the clone as-is.

        Fresh.Displayed          = true;
        Fresh.CachedOutlineValid = false;
        return Fresh;
    }

    // 📝 One array copy's placement: how many instances the source emits (INCLUDING the original at ordinal 0) and, per copy ordinal, the affine
    //    that stamps it — pivot + rotation + translation fed to ConstructArrayCopy. Resolved once per source in ReflowArrayChildren so pass ① and
    //    the erase pass ② agree on the count. Linear rotates the step vector through the datum angle; Radial rotates about the anchor; Instance-
    //    on-points translates so the source centroid lands on each host vertex. Returns the affine for a given instance i in [1 .. Count-1].
    struct ArrayPlacement
    {
        int    Count;    // [-] - total instances incl. the original (>= 2 emits copies; < 2 emits nothing)
        ImVec2 Pivot;    // [mm]- rotation pivot (datum anchor)
    };

    // 📝 Resolve the instance count a source emits for its current mode. Linear / Radial use ArrayCount (clamped >= 1). Instance-on-points counts
    //    the host shape's defining vertices (+1 for the original slot); a missing / empty host yields count 1 (no copies) → the reflow drops any.
    int ResolveArrayCount(const ParametricSketchShapeStore& Store, const ParametricSketchShape& Source)
    {
        if (Source.ArrayModeChoice == 2)
        {
            const ParametricSketchShape* Host = ResolveParametricSketchShape(const_cast<ParametricSketchShapeStore&>(Store), Source.ArrayHostShape);
            if (!Host || Host->Points.empty())
                return 1;
            return (int)Host->Points.size() + 1;   // one copy per host vertex, plus the original slot at ordinal 0
        }
        return Source.ArrayCount < 1 ? 1 : Source.ArrayCount;
    }

    // 📝 Compute the affine for copy ordinal Instance (1 .. Count-1) of Source. Writes CosAngle / SinAngle / Offset / Pivot. Returns false when the
    //    ordinal has no placement (instance-on-points host vertex missing). Linear: Offset = Instance · R(DatumRotation)·ArrayLinearStep, no
    //    rotation. Radial: rotate about DatumAnchor by Instance · Sweep / Divisor (Divisor = Count for a full turn to avoid a 360° duplicate, else
    //    Count-1 so the last copy lands on the sweep end). Instance-on-points: translate the source centroid onto host Points[Instance-1].
    bool ResolveArrayInstanceAffine(const ParametricSketchShapeStore& Store,
                                    const ParametricSketchShape&       Source,
                                    int                       Instance,
                                    int                       Count,
                                    float&                    CosAngle,
                                    float&                    SinAngle,
                                    ImVec2&                   Offset,
                                    ImVec2&                   Pivot)
    {
        Pivot    = Source.DatumAnchor;
        CosAngle = 1.0f;
        SinAngle = 0.0f;
        Offset   = ImVec2(0, 0);

        if (Source.ArrayModeChoice == 1)   // radial
        {
            constexpr float FullTurn = 6.2831853f;
            const bool  WholeTurn = std::fabs(std::fabs(Source.ArrayRadialSweep) - FullTurn) < 1e-3f;
            const int   Divisor    = WholeTurn ? Count : (Count > 1 ? Count - 1 : 1);
            const float Angle      = Source.ArrayRadialSweep * (float)Instance / (float)Divisor;
            CosAngle = std::cos(Angle);
            SinAngle = std::sin(Angle);
            return true;
        }

        if (Source.ArrayModeChoice == 2)   // instance-on-points
        {
            const ParametricSketchShape* Host = ResolveParametricSketchShape(const_cast<ParametricSketchShapeStore&>(Store), Source.ArrayHostShape);
            const int HostVertex = Instance - 1;   // ordinal 1 → host vertex 0
            if (!Host || HostVertex < 0 || HostVertex >= (int)Host->Points.size())
                return false;
            const ImVec2 Centroid = ResolveParametricSketchShapeCentroid(const_cast<ParametricSketchShape&>(Source));
            const ImVec2 Target   = Host->Points[(size_t)HostVertex];
            Offset = ImVec2(Target.x - Centroid.x, Target.y - Centroid.y);
            return true;
        }

        // linear — step the datum-rotated offset per instance
        const float DatumCos = std::cos(Source.DatumRotation);
        const float DatumSin = std::sin(Source.DatumRotation);
        const ImVec2 Step(DatumCos * Source.ArrayLinearStep.x - DatumSin * Source.ArrayLinearStep.y,
                          DatumSin * Source.ArrayLinearStep.x + DatumCos * Source.ArrayLinearStep.y);
        Offset = ImVec2(Step.x * (float)Instance, Step.y * (float)Instance);
        return true;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CLIPPER2-BACKED OFFSET
//------------------------------------------------------------------------------------------------------------------------

std::vector<std::vector<ImVec2>> SolveRegionOffset(const std::vector<std::vector<ImVec2>>& Loops, float DistanceMm,
                                                   SketchOffsetCornerStyle CornerStyle)
{
    std::vector<std::vector<ImVec2>> Result;
    if (Loops.empty() || Loops.front().size() < 3 || DistanceMm == 0.0f)
        return Result;

    // Map the workspace corner style onto Clipper2's JoinType: Round (arc, the CAD default), Miter (sharp intersection, clamped by the miter
    //    limit below), Bevel -> Square (a flat cut across the corner). The default caller passes Round, so existing behaviour is unchanged.
    Clipper2Lib::JoinType Join = Clipper2Lib::JoinType::Round;
    switch (CornerStyle)
    {
        case SketchOffsetCornerStyle::Miter: Join = Clipper2Lib::JoinType::Miter;  break;
        case SketchOffsetCornerStyle::Bevel: Join = Clipper2Lib::JoinType::Square; break;
        default:                             Join = Clipper2Lib::JoinType::Round;  break;
    }

    // A closed region offset: the JoinType above builds each convex corner, EndType::Polygon closes the path so the whole region inflates /
    //    deflates. The 2.0 is Clipper2's miter limit (only consulted for JoinType::Miter). Clipper2 scales to its fixed-point grid at OffsetPrecision.
    //    🔴 The region's HOLE loops ride in as sibling paths: ClipperOffset offsets nested paths JOINTLY, so a hole boundary moves against the outer
    //       (a positive inflate shrinks the void, a negative deflate grows it) instead of being ignored — a punched profile's voids survive the offset.
    Clipper2Lib::PathsD Source;
    Source.reserve(Loops.size());
    for (const std::vector<ImVec2>& Loop : Loops)
        if (Loop.size() >= 3)
            Source.push_back(ConvertLoopToPath(Loop));
    if (Source.empty())
        return Result;
    const Clipper2Lib::PathsD Offset = Clipper2Lib::InflatePaths(Source,
                                                                 (double)DistanceMm,
                                                                 Join,
                                                                 Clipper2Lib::EndType::Polygon,
                                                                 2.0,
                                                                 (int)OffsetPrecision);

    for (const Clipper2Lib::PathD& Path : Offset)
    {
        // Force the workspace winding by signed area: an outward island winds CCW (outer), an inward void winds CW (hole).
        const bool CounterClockwise = [&]() {
            std::vector<ImVec2> Probe;
            Probe.reserve(Path.size());
            for (const Clipper2Lib::PointD& Point : Path)
                Probe.emplace_back((float)Point.x, (float)Point.y);
            return LoopTwiceArea(Probe) >= 0.0f;
        }();
        std::vector<ImVec2> Converted = ConvertPathToLoop(Path, CounterClockwise);
        if (Converted.size() >= 3 && std::fabs(LoopTwiceArea(Converted) * 0.5f) >= AreaEps)
            Result.push_back(std::move(Converted));
    }
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  OPEN-CURVE PARALLEL OFFSET
//------------------------------------------------------------------------------------------------------------------------

// 📝 Offset ONE open polyline by a signed distance into a SINGLE parallel open curve on one side (the Blender "offset open curve" result, not
//    Clipper2's both-sided ribbon — Clipper2's open EndTypes all close the path). The sign picks the side: positive shifts along the LEFT normal
//    (the segment direction rotated +90°), negative shifts right. Each interior vertex rides the AVERAGED adjacent-segment normal, lengthened by
//    1/cos(half-angle) so the parallel curve holds a constant perpendicular distance through the joint (a true miter); the scale is clamped so a
//    sharp reflex corner cannot fire the vertex off to infinity — it bevels instead, the graceful degenerate. The CornerStyle rides along for the
//    caller's readout but a single open curve has only miter/bevel behaviour here (Round would need arc insertion — deferred). Returns the offset
//    polyline, or empty when the source has < 2 points or the distance is ~zero.
std::vector<ImVec2> SolveOpenCurveOffset(const std::vector<ImVec2>& Polyline, float DistanceMm)
{
    std::vector<ImVec2> Result;
    const size_t Count = Polyline.size();
    if (Count < 2 || DistanceMm == 0.0f)
        return Result;

    // The unit LEFT normal of the segment A→B (direction rotated +90°: (dx,dy) → (-dy,dx)). Zero-length segments contribute no normal.
    auto SegmentNormal = [](const ImVec2& A, const ImVec2& B) -> ImVec2
    {
        const float Dx = B.x - A.x, Dy = B.y - A.y;
        const float Length = std::sqrt(Dx * Dx + Dy * Dy);
        if (Length < 1e-6f)
            return ImVec2(0.0f, 0.0f);
        return ImVec2(-Dy / Length, Dx / Length);
    };

    constexpr float MiterClampScale = 4.0f;   // [-] - cap the joint lengthening so a sharp corner bevels rather than spiking to infinity

    Result.reserve(Count);
    for (size_t Index = 0; Index < Count; ++Index)
    {
        // Endpoints ride the single adjacent segment's normal (a square butt cap at the requested distance). Interior vertices average the two
        //    adjacent normals and lengthen by 1/cos(half-angle) = 1/|averaged normal| so the perpendicular distance stays exactly DistanceMm.
        ImVec2 Normal;
        if (Index == 0)
        {
            Normal = SegmentNormal(Polyline[0], Polyline[1]);
        }
        else if (Index + 1 == Count)
        {
            Normal = SegmentNormal(Polyline[Count - 2], Polyline[Count - 1]);
        }
        else
        {
            const ImVec2 NormalBefore = SegmentNormal(Polyline[Index - 1], Polyline[Index]);
            const ImVec2 NormalAfter  = SegmentNormal(Polyline[Index],     Polyline[Index + 1]);
            ImVec2       Averaged(NormalBefore.x + NormalAfter.x, NormalBefore.y + NormalAfter.y);
            const float  AverageLength = std::sqrt(Averaged.x * Averaged.x + Averaged.y * Averaged.y);
            if (AverageLength < 1e-6f)
            {
                // A ~180° reversal (the curve doubles back): the two normals cancel. Fall back to the incoming normal, unscaled.
                Normal = NormalBefore;
            }
            else
            {
                // |Averaged| = 2·cos(half-angle) for two unit normals, so 1/half of that is the miter scale. Clamp it so a spike bevels.
                float MiterScale = 2.0f / AverageLength;
                if (MiterScale > MiterClampScale)
                    MiterScale = MiterClampScale;
                Normal = ImVec2(Averaged.x / AverageLength * MiterScale, Averaged.y / AverageLength * MiterScale);
            }
        }

        Result.emplace_back(Polyline[Index].x + Normal.x * DistanceMm,
                            Polyline[Index].y + Normal.y * DistanceMm);
    }
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        ORCHESTRATION
//------------------------------------------------------------------------------------------------------------------------

OffsetOutcome AppendOffsetResult(ParametricSketchShapeStore& Store, float DistanceMm, SketchOffsetCornerStyle CornerStyle)
{
    // Work over the multi-select SelectionSet, or the lone Selected shape when nothing multi-picked (a single object select seeds only
    //    Store.Selected). Snapshot the sources BEFORE appending (appending grows Store.Shapes and could reallocate; ids stay stable).
    std::vector<uint32_t> SourceIdentifiers = Store.SelectionSet;
    if (SourceIdentifiers.empty() && Store.Selected != 0)
        SourceIdentifiers.push_back(Store.Selected);
    if (SourceIdentifiers.empty())
    {
        RaiseNotice(Store, "Offset needs a selected shape");
        return OffsetOutcome::NothingSelected;
    }
    if (DistanceMm == 0.0f)
    {
        RaiseNotice(Store, "Offset distance is zero");
        return OffsetOutcome::EmptyResult;
    }

    std::vector<uint32_t> AppendedIdentifiers;
    int  OffsettableSourceCount = 0;
    for (uint32_t Identifier : SourceIdentifiers)
    {
        ParametricSketchShape* Shape = ResolveParametricSketchShape(Store, Identifier);
        if (!Shape)
            continue;

        const uint32_t SourceTint      = Shape->TintIndex;
        const uint32_t SourceFolder    = Shape->FolderIdentifier;
        const float    SourceElevation = Shape->Elevation;

        // ── OPEN source (a Line / Polyline / open curve): a single PARALLEL curve on the side the sign selects, staying an open Polyline (Curves
        //    group, not a filled Profile). Flatten the open run the same way the view does (EvaluateShapePolyline — no forced closing point). ──
        if (!Shape->ClosedEnabled)
        {
            std::vector<ImVec2> Open;
            EvaluateShapePolyline(*Shape, Open, OffsetFlattenBudget);
            if (Open.size() < 2)
                continue;                   // a degenerate open source — nothing to offset
            ++OffsettableSourceCount;

            std::vector<ImVec2> Parallel = SolveOpenCurveOffset(Open, DistanceMm);
            if (Parallel.size() < 2)
                continue;

            ParametricSketchShape Fresh = ConstructParametricSketchShape(ParametricSketchShapeCategory::Polyline, Parallel);
            Fresh.Identifier       = Store.NextIdentifier++;
            Fresh.ClosedEnabled    = false;
            Fresh.FillEnabled      = false;
            Fresh.Displayed        = true;
            Fresh.TintIndex        = SourceTint;
            Fresh.FolderIdentifier = SourceFolder;
            Fresh.Elevation        = SourceElevation;
            std::snprintf(Fresh.Title, sizeof(Fresh.Title), "Offset Curve %u", Fresh.Identifier);

            Store.Shapes.push_back(Fresh);
            AppendedIdentifiers.push_back(Fresh.Identifier);

            char LogLabel[64];
            std::snprintf(LogLabel, sizeof(LogLabel), "Added %s", Fresh.Title);
            AppendParametricSketchEdit(Store, Fresh.Identifier, LogLabel, "spline");
            continue;
        }

        // ── CLOSED source (Rectangle / Circle / Polygon / Profile): the region inflates / deflates into one Profile per surviving outer loop. ──
        ++OffsettableSourceCount;

        // Flatten the source densely (the offsetter is a straight-segment sweep, like the boolean) into its region: the CCW outer loop + every CW
        //    hole ring. A Profile's HoleLoops are already verbatim flat loops in world mm (the analytic shapes carry none), so only the outer needs
        //    the polygon flatten. Feeding the holes jointly is what keeps a punched profile's voids — see SolveRegionOffset.
        std::vector<std::vector<ImVec2>> Region;
        std::vector<ImVec2> Outline;
        EvaluateFilledPolygon(*Shape, Outline, OffsetFlattenBudget);
        if (Outline.size() < 3)
            continue;
        Region.push_back(std::move(Outline));
        for (const std::vector<ImVec2>& Hole : Shape->HoleLoops)
        {
            if (Hole.size() >= 3)
                Region.push_back(Hole);
        }

        std::vector<std::vector<ImVec2>> Offset = SolveRegionOffset(Region, DistanceMm, CornerStyle);
        if (Offset.empty())
            continue;                       // this shape collapsed under a large inward offset — try the next

        std::vector<std::vector<ImVec2>> Outers, Holes;
        PartitionOutersAndHoles(Offset, Outers, Holes);
        if (Outers.empty())
            continue;

        for (const auto& Outer : Outers)
        {
            ParametricSketchShape Fresh;
            Fresh.Identifier       = Store.NextIdentifier++;
            Fresh.Category         = ParametricSketchShapeCategory::Profile;
            Fresh.Points           = Outer;
            Fresh.ClosedEnabled    = true;
            Fresh.FillEnabled      = true;
            Fresh.Displayed        = true;
            Fresh.TintIndex        = SourceTint;
            Fresh.FolderIdentifier = SourceFolder;
            Fresh.Elevation        = SourceElevation;
            for (const auto& Hole : Holes)
            {
                if (PointInsideLoop(Hole.front().x, Hole.front().y, Outer))
                    Fresh.HoleLoops.push_back(Hole);
            }
            std::snprintf(Fresh.Title, sizeof(Fresh.Title), "Offset Profile %u", Fresh.Identifier);

            Store.Shapes.push_back(Fresh);
            AppendedIdentifiers.push_back(Fresh.Identifier);

            char LogLabel[64];
            std::snprintf(LogLabel, sizeof(LogLabel), "Added %s", Fresh.Title);
            AppendParametricSketchEdit(Store, Fresh.Identifier, LogLabel, "spline");
        }
    }

    if (OffsettableSourceCount == 0)
    {
        RaiseNotice(Store, "Offset needs a shape to offset");
        return OffsetOutcome::NeedsClosedShapes;
    }
    if (AppendedIdentifiers.empty())
    {
        RaiseNotice(Store, "Offset produced no region");
        return OffsetOutcome::EmptyResult;
    }

    // Re-select the fresh Profiles (originals kept, no longer selected) so the next action / gizmo acts on the offset result.
    Store.SelectionSet = AppendedIdentifiers;
    Store.Selected     = AppendedIdentifiers.back();
    Store.Hovered      = 0;
    return OffsetOutcome::Committed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      DATUM MIRROR
//------------------------------------------------------------------------------------------------------------------------

MirrorOutcome AppendMirrorResult(ParametricSketchShapeStore& Store)
{
    // Work over the multi-select SelectionSet, or the lone Selected shape when nothing multi-picked. Snapshot the sources BEFORE appending
    //    (appending grows Store.Shapes and could reallocate; the captured ids stay stable). Mirrors AppendOffsetResult.
    std::vector<uint32_t> SourceIdentifiers = Store.SelectionSet;
    if (SourceIdentifiers.empty() && Store.Selected != 0)
        SourceIdentifiers.push_back(Store.Selected);
    if (SourceIdentifiers.empty())
    {
        RaiseNotice(Store, "Mirror needs a selected shape");
        return MirrorOutcome::NothingSelected;
    }

    std::vector<uint32_t> AppendedIdentifiers;
    int      DatumSourceCount   = 0;
    uint32_t LastMirroredSource = 0;   // [-] - the last source that actually produced copies (re-selected below so its datum stays shown)
    for (uint32_t Identifier : SourceIdentifiers)
    {
        ParametricSketchShape* Shape = ResolveParametricSketchShape(Store, Identifier);
        if (!Shape || !Shape->DatumEnabled)
            continue;                       // no datum on this shape — nothing to reflect it across
        if (Shape->MirrorSource != 0)
            continue;                       // a driven mirror child cannot itself source a mirror (that would chain drivers)
        ++DatumSourceCount;
        LastMirroredSource = Shape->Identifier;

        // Refresh-not-duplicate: a second Mirror on a shape that already drives live children must not STACK a fresh set. Erase this shape's
        //    existing driven children first (ReflowMirrorChildren rebuilds fresh ones below). Snapshot the id before Shape may dangle on erase.
        const uint32_t SourceId = Shape->Identifier;
        Store.Shapes.erase(std::remove_if(Store.Shapes.begin(), Store.Shapes.end(),
                                          [SourceId](const ParametricSketchShape& Candidate) { return Candidate.MirrorSource == SourceId; }),
                           Store.Shapes.end());
        Shape = ResolveParametricSketchShape(Store, SourceId);   // re-resolve after the erase (the vector may have shifted / reallocated)
        if (!Shape)
            continue;

        // One driven copy per reflection the datum emits (None → none; Horizontal / Vertical → one; Cross → both). Each copy is STAMPED with
        //    its source + axis index; from here ReflowMirrorChildren rebuilds its geometry on every edit, so the copy follows source + datum.
        const ImVec2 Anchor = Shape->DatumAnchor;
        for (const MirrorAxis& Axis : ResolveDatumAxisNormals(*Shape))
        {
            ParametricSketchShape Fresh = ConstructMirrorCopy(*Shape, Anchor, Axis.Normal);
            if (Fresh.Points.size() < 2)
                continue;                   // the category rejected the reflected points — skip this copy

            Fresh.Identifier      = Store.NextIdentifier++;
            Fresh.MirrorSource    = SourceId;
            Fresh.MirrorAxisIndex = Axis.AxisIndex;
            Fresh.DatumEnabled    = false;   // a driven copy carries no datum of its own (it would confuse the reflow / render gate)
            std::snprintf(Fresh.Title, sizeof(Fresh.Title), "Mirror %u", Fresh.Identifier);

            Store.Shapes.push_back(Fresh);
            AppendedIdentifiers.push_back(Fresh.Identifier);
            Shape = ResolveParametricSketchShape(Store, SourceId);   // push_back may reallocate — re-resolve for the next iteration

            char LogLabel[64];
            std::snprintf(LogLabel, sizeof(LogLabel), "Added %s", Fresh.Title);
            AppendParametricSketchEdit(Store, Fresh.Identifier, LogLabel, "flip-horizontal");
            if (!Shape)
                break;
        }
    }

    if (DatumSourceCount == 0)
    {
        RaiseNotice(Store, "Mirror needs a datum on the shape");
        return MirrorOutcome::NoDatum;
    }
    if (AppendedIdentifiers.empty())
    {
        RaiseNotice(Store, "Mirror produced no shape");
        return MirrorOutcome::EmptyResult;
    }

    // Keep the SOURCE selected (not the fresh copies): the copies are driven / non-editable, and leaving the source selected keeps its datum
    //    shown so the user can immediately keep tweaking source or datum and watch the live mirror follow.
    Store.SelectionSet.clear();
    if (LastMirroredSource != 0)
        Store.SelectionSet.push_back(LastMirroredSource);
    Store.Selected = LastMirroredSource;
    Store.Hovered  = 0;
    return MirrorOutcome::Committed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    LIVE MIRROR REFLOW
//------------------------------------------------------------------------------------------------------------------------

void ReflowMirrorChildren(ParametricSketchShapeStore& Store)
{
    // Two passes over the shape list. ① Rebuild every surviving driven child in place from its live source (so a source edit or a datum move
    //    already committed to Store propagates before the caller snapshots). ② Erase any child whose relationship is gone (source vanished /
    //    datum disabled / axis no longer emitted). We resolve sources by id (never by pointer) because rebuilding never resizes the vector, but
    //    the erase at the end may — so the erase runs last, after all rebuilds. A child is recognised by MirrorSource != 0.

    // ① Rebuild. Snapshot the child indices first (we mutate in place, never resize here), reflecting the source across the child's stored axis.
    for (ParametricSketchShape& DrivenCopy : Store.Shapes)
    {
        if (DrivenCopy.MirrorSource == 0)
            continue;
        const ParametricSketchShape* Source = ResolveParametricSketchShape(Store, DrivenCopy.MirrorSource);
        if (!Source || !Source->DatumEnabled || Source->MirrorSource != 0)
            continue;                       // orphaned / datum-off / chained-source child — the erase pass below drops it

        // Find the reflection matching this child's axis index among the source datum's current emissions. A Cross→single downgrade leaves the
        //    now-absent axis unmatched → the child is not rebuilt here and is erased below.
        const std::vector<MirrorAxis> Axes = ResolveDatumAxisNormals(*Source);
        const MirrorAxis* Match = nullptr;
        for (const MirrorAxis& Axis : Axes)
            if (Axis.AxisIndex == DrivenCopy.MirrorAxisIndex)
                Match = &Axis;
        if (!Match)
            continue;

        // Rebuild the reflected geometry, then graft it onto the child while preserving the child's identity + presentation. ConstructMirrorCopy
        //    clones the source's exact solved geometry and reflects it (never re-solves), so a vertex-edited or rotated source keeps its true
        //    outline, and a source category change still tracks because the clone copies the category field. Re-resolve Source each
        //    call? No — ConstructMirrorCopy only READS Source and appends to a fresh struct; it never touches Store.Shapes, so Source stays valid.
        ParametricSketchShape Rebuilt = ConstructMirrorCopy(*Source, Source->DatumAnchor, Match->Normal);
        if (Rebuilt.Points.size() < 2)
            continue;                       // the category rejected the reflected points — keep the child's last good geometry

        // Preserve identity / link / presentation; overwrite the geometry + solved scalars from the rebuild.
        const uint32_t KeepIdentifier   = DrivenCopy.Identifier;
        const uint32_t KeepMirrorSource = DrivenCopy.MirrorSource;
        const int      KeepAxisIndex    = DrivenCopy.MirrorAxisIndex;
        const uint32_t KeepTint         = DrivenCopy.TintIndex;
        const bool     KeepDisplayed    = DrivenCopy.Displayed;
        const bool     KeepLocked       = DrivenCopy.LockEnabled;
        char           KeepTitle[48];
        std::snprintf(KeepTitle, sizeof(KeepTitle), "%s", DrivenCopy.Title);

        DrivenCopy = Rebuilt;                    // whole-struct graft (resets the flatten cache to invalid by default)
        DrivenCopy.Identifier      = KeepIdentifier;
        DrivenCopy.MirrorSource    = KeepMirrorSource;
        DrivenCopy.MirrorAxisIndex = KeepAxisIndex;
        DrivenCopy.TintIndex       = KeepTint;
        DrivenCopy.Displayed       = KeepDisplayed;
        DrivenCopy.LockEnabled     = KeepLocked;
        DrivenCopy.DatumEnabled    = false;      // a driven copy never carries its own datum
        DrivenCopy.CachedOutlineValid = false;
        std::snprintf(DrivenCopy.Title, sizeof(DrivenCopy.Title), "%s", KeepTitle);
    }

    // ② Erase orphaned children (relationship gone). A driven child is orphaned when its source is missing, the source's datum is disabled, the
    //    source is itself a driven child, or the source no longer emits this child's axis index.
    Store.Shapes.erase(std::remove_if(Store.Shapes.begin(), Store.Shapes.end(),
        [&Store](const ParametricSketchShape& DrivenCopy)
        {
            if (DrivenCopy.MirrorSource == 0)
                return false;
            const ParametricSketchShape* Source = ResolveParametricSketchShape(Store, DrivenCopy.MirrorSource);
            if (!Source || !Source->DatumEnabled || Source->MirrorSource != 0)
                return true;
            for (const MirrorAxis& Axis : ResolveDatumAxisNormals(*Source))
                if (Axis.AxisIndex == DrivenCopy.MirrorAxisIndex)
                    return false;           // its axis is still emitted — keep it
            return true;                    // axis no longer emitted (a Cross → single downgrade) — drop it
        }), Store.Shapes.end());
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      DATUM ARRAY
//------------------------------------------------------------------------------------------------------------------------

ArrayOutcome AppendArrayResult(ParametricSketchShapeStore& Store)
{
    // Work over the multi-select SelectionSet, or the lone Selected shape when nothing multi-picked. Mirrors AppendMirrorResult: stamp the
    //    source's array on, erase its existing driven copies (refresh-not-duplicate), and let ReflowArrayChildren build the run fresh below.
    std::vector<uint32_t> SourceIdentifiers = Store.SelectionSet;
    if (SourceIdentifiers.empty() && Store.Selected != 0)
        SourceIdentifiers.push_back(Store.Selected);
    if (SourceIdentifiers.empty())
    {
        RaiseNotice(Store, "Array needs a selected shape");
        return ArrayOutcome::NothingSelected;
    }

    int      EnabledSourceCount = 0;
    uint32_t LastArraySource    = 0;   // [-] - the last source that emitted copies (re-selected below so its datum stays shown)
    for (uint32_t Identifier : SourceIdentifiers)
    {
        ParametricSketchShape* Shape = ResolveParametricSketchShape(Store, Identifier);
        if (!Shape || Shape->ArrayEnabled == 0)
            continue;                       // array not turned on for this shape — nothing to replicate
        if (Shape->ArraySource != 0)
            continue;                       // a driven array copy cannot itself source an array (that would chain drivers)
        ++EnabledSourceCount;
        LastArraySource = Shape->Identifier;

        // Stamp the source's current mode so its children carry the matching ArrayMode; erase this source's existing copies (the reflow rebuilds).
        const uint32_t SourceId = Shape->Identifier;
        Shape->ArrayMode = Shape->ArrayModeChoice;
        Store.Shapes.erase(std::remove_if(Store.Shapes.begin(), Store.Shapes.end(),
                                          [SourceId](const ParametricSketchShape& Candidate) { return Candidate.ArraySource == SourceId; }),
                           Store.Shapes.end());
    }

    if (EnabledSourceCount == 0)
    {
        RaiseNotice(Store, "Array needs Enable on the shape");
        return ArrayOutcome::NotEnabled;
    }

    // Build the run now so the outcome reflects whether any copy actually emitted (count < 2 / no host vertices → EmptyResult).
    ReflowArrayChildren(Store);
    bool AnyEmitted = false;
    for (const ParametricSketchShape& Candidate : Store.Shapes)
        if (Candidate.ArraySource != 0)
        {
            AnyEmitted = true;
            break;
        }
    if (!AnyEmitted)
    {
        RaiseNotice(Store, "Array produced no copy");
        return ArrayOutcome::EmptyResult;
    }

    // Keep the SOURCE selected (not the driven copies) so its datum stays shown for continued tweaking, mirroring AppendMirrorResult.
    Store.SelectionSet.clear();
    if (LastArraySource != 0)
        Store.SelectionSet.push_back(LastArraySource);
    Store.Selected = LastArraySource;
    Store.Hovered  = 0;
    return ArrayOutcome::Committed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    LIVE ARRAY REFLOW
//------------------------------------------------------------------------------------------------------------------------

void ReflowArrayChildren(ParametricSketchShapeStore& Store)
{
    // Two passes, mirroring ReflowMirrorChildren. ① For each ENABLED source (ArrayEnabled, not itself a child), resolve the instance count and,
    //    per ordinal 1..Count-1, PATCH the matching existing child in place (or APPEND a fresh one) from the source stamped at that slot. ② Erase
    //    orphans (source gone / array off / source is a child / ordinal >= count / instance-on-points host gone). A child is recognised by
    //    ArraySource != 0. Sources are resolved by id (never pointer): pass ① may push_back (reallocating), so re-resolve the source each append.

    // ① Patch / append. Snapshot the source ids first so appended children never re-enter this walk. Iterate by index — Store.Shapes may grow.
    std::vector<uint32_t> SourceIdentifiers;
    for (const ParametricSketchShape& Candidate : Store.Shapes)
        if (Candidate.ArrayEnabled != 0 && Candidate.ArraySource == 0)
            SourceIdentifiers.push_back(Candidate.Identifier);

    for (uint32_t SourceId : SourceIdentifiers)
    {
        const ParametricSketchShape* Source = ResolveParametricSketchShape(Store, SourceId);
        if (!Source)
            continue;
        const int Count = ResolveArrayCount(Store, *Source);
        const int Mode  = Source->ArrayModeChoice;

        for (int Instance = 1; Instance < Count; ++Instance)
        {
            Source = ResolveParametricSketchShape(Store, SourceId);   // re-resolve (a prior append may have reallocated the vector)
            if (!Source)
                break;

            float  CosAngle = 1.0f, SinAngle = 0.0f;
            ImVec2 Offset(0, 0), Pivot(0, 0);
            if (!ResolveArrayInstanceAffine(Store, *Source, Instance, Count, CosAngle, SinAngle, Offset, Pivot))
                continue;                   // this ordinal has no placement (host vertex missing) — the erase pass drops any stale child

            ParametricSketchShape Rebuilt = ConstructArrayCopy(*Source, Pivot, CosAngle, SinAngle, Offset);
            if (Rebuilt.Points.size() < 2)
                continue;                   // the category rejected the mapped points — keep the child's last good geometry

            // Find an existing child for (SourceId, Instance) to PATCH; else APPEND a fresh one. Search by index each time (the vector may grow).
            int MatchIndex = -1;
            for (int ShapeIndex = 0; ShapeIndex < (int)Store.Shapes.size(); ++ShapeIndex)
                if (Store.Shapes[(size_t)ShapeIndex].ArraySource == SourceId &&
                    Store.Shapes[(size_t)ShapeIndex].ArrayInstance == Instance)
                {
                    MatchIndex = ShapeIndex;
                    break;
                }

            if (MatchIndex >= 0)
            {
                ParametricSketchShape& DrivenCopy = Store.Shapes[(size_t)MatchIndex];
                const uint32_t KeepIdentifier = DrivenCopy.Identifier;
                const uint32_t KeepTint       = DrivenCopy.TintIndex;
                const bool     KeepDisplayed  = DrivenCopy.Displayed;
                const bool     KeepLocked     = DrivenCopy.LockEnabled;
                char           KeepTitle[48];
                std::snprintf(KeepTitle, sizeof(KeepTitle), "%s", DrivenCopy.Title);

                DrivenCopy = Rebuilt;            // whole-struct graft (resets the flatten cache to invalid by default)
                DrivenCopy.Identifier      = KeepIdentifier;
                DrivenCopy.ArraySource     = SourceId;
                DrivenCopy.ArrayMode       = Mode;
                DrivenCopy.ArrayInstance   = Instance;
                DrivenCopy.TintIndex       = KeepTint;
                DrivenCopy.Displayed       = KeepDisplayed;
                DrivenCopy.LockEnabled     = KeepLocked;
                DrivenCopy.DatumEnabled    = false;   // a driven copy never carries its own datum
                DrivenCopy.CachedOutlineValid = false;
                std::snprintf(DrivenCopy.Title, sizeof(DrivenCopy.Title), "%s", KeepTitle);
            }
            else
            {
                Rebuilt.Identifier    = Store.NextIdentifier++;
                Rebuilt.ArraySource   = SourceId;
                Rebuilt.ArrayMode     = Mode;
                Rebuilt.ArrayInstance = Instance;
                Rebuilt.DatumEnabled  = false;
                Rebuilt.CachedOutlineValid = false;
                std::snprintf(Rebuilt.Title, sizeof(Rebuilt.Title), "Array %u", Rebuilt.Identifier);
                Store.Shapes.push_back(Rebuilt);
            }
        }
    }

    // ② Erase orphaned copies (relationship gone). A driven copy is orphaned when its source is missing, the source's array is disabled, the
    //    source is itself a driven copy, the copy's ordinal now exceeds the source's instance count, or an instance-on-points host vertex is gone.
    Store.Shapes.erase(std::remove_if(Store.Shapes.begin(), Store.Shapes.end(),
        [&Store](const ParametricSketchShape& DrivenCopy)
        {
            if (DrivenCopy.ArraySource == 0)
                return false;
            const ParametricSketchShape* Source = ResolveParametricSketchShape(Store, DrivenCopy.ArraySource);
            if (!Source || Source->ArrayEnabled == 0 || Source->ArraySource != 0)
                return true;
            const int Count = ResolveArrayCount(Store, *Source);
            if (DrivenCopy.ArrayInstance < 1 || DrivenCopy.ArrayInstance >= Count)
                return true;                // ordinal dropped out of range (count lowered / host shrank)
            if (Source->ArrayModeChoice == 2)
            {
                const ParametricSketchShape* Host = ResolveParametricSketchShape(Store, Source->ArrayHostShape);
                const int HostVertex = DrivenCopy.ArrayInstance - 1;
                if (!Host || HostVertex < 0 || HostVertex >= (int)Host->Points.size())
                    return true;            // its host vertex is gone
            }
            return false;
        }), Store.Shapes.end());
}

} // namespace Frontier
