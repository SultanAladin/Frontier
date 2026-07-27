/*==============================================================================================================================================
                                                              PARAMETRICSKETCHFILLET.CPP
==============================================================================================================================================*/
// 🧩 The analytic corner Fillet / Chamfer solver. A corner is the triple (P[i-1], P[i], P[i+1]) on a shape's defining loop; the half-angle
//    between its two legs fixes both the tangent setback and the arc geometry. Filleting / chamfering a PARAMETRIC closed family (Rectangle
//    / Polygon) converts it to a Profile — Points-verbatim — because those families re-solve from their axis-aligned boundary and would
//    otherwise snap the corner straight back to a box; an open Polyline keeps its family with the corner expanded. The arc is stored exact
//    (Centre / Radius / start + sweep) and its display span is spliced into the loop as samples so the closed Profile fills correctly;
//    picking + snapping run on that same flattening. Pure analytic geometry — no pixels, no Vulkan.

#include "ParametricSketchFillet.h"

#include <algorithm>   // 📝 std::max / std::min / std::clamp — clamp the setback to the safe limit + the sample count.
#include <cmath>       // 📝 std::sqrt / std::atan2 / std::cos / std::sin / std::acos — the corner + arc trigonometry.
#include <cstdio>      // 📝 std::snprintf — seed the History label.

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr float TwoPi     = 6.28318530717958647692f;   // [rad] - a full turn (matches ParametricSketchShapeStore's constant)
    constexpr float CornerEps = 1e-4f;                     // [mm]  - shortest leg / setback below which a corner is degenerate

    // 📝 Vector length in world mm.
    float VectorLength(ImVec2 Delta)
    {
        return std::sqrt(Delta.x * Delta.x + Delta.y * Delta.y);
    }

    // 📝 Normalize a vector; returns false (leaving Out untouched) for a near-zero vector so a degenerate leg is caught upstream.
    bool NormalizeVector(ImVec2 Delta, ImVec2& Out)
    {
        const float Length = VectorLength(Delta);
        if (Length < CornerEps)
            return false;
        Out = ImVec2(Delta.x / Length, Delta.y / Length);
        return true;
    }

    // 📝 Resolve the three loop points that form the corner at Index: the previous / corner / next vertices. A closed shape wraps the
    //    neighbours modulo the count; an open shape rejects its two endpoints (they carry only one leg). Returns false when the shape has
    //    too few points or Index has no second leg.
    bool ResolveCornerTriple(const ParametricSketchShape& Shape,
                             int                 Index,
                             ImVec2&             Previous,
                             ImVec2&             Corner,
                             ImVec2&             Next)
    {
        const int Count = (int)Shape.Points.size();
        if (Count < 3 || Index < 0 || Index >= Count)
            return false;

        if (Shape.ClosedEnabled)
        {
            Previous = Shape.Points[(Index - 1 + Count) % Count];
            Corner   = Shape.Points[Index];
            Next     = Shape.Points[(Index + 1) % Count];
            return true;
        }

        // Open run: the first + last vertices have only one leg, so they cannot be rounded.
        if (Index == 0 || Index == Count - 1)
            return false;
        Previous = Shape.Points[Index - 1];
        Corner   = Shape.Points[Index];
        Next     = Shape.Points[Index + 1];
        return true;
    }

    // 📝 Record a PARAMETRIC corner edit on the shape Identifier: validate the corner is roundable (SolveCornerEdit resolves), then store a
    //    ParametricSketchCornerFillet (CornerIndex + Magnitude + ChamferEnabled) on the shape — leaving Points untouched so the corner stays ONE real
    //    grabbable vertex and the flatten path solves + tessellates the arc / edge for display only (no dots baked into the definition). An
    //    existing edit on the same corner is overwritten (re-editable from Properties); Magnitude ≤ 0 clears it. The flatten cache is
    //    invalidated so the display re-solves. Shared by FilletShapeCorner + ChamferShapeCorner. Returns false when nothing was applied.
    bool ApplyCornerEdit(ParametricSketchShapeStore&    Store,
                         uint32_t              Identifier,
                         int                   Index,
                         float                 Magnitude,
                         ParametricSketchCornerCategory Category)
    {
        ParametricSketchShape* Shape = ResolveParametricSketchShape(Store, Identifier);
        if (!Shape)
            return false;

        // A positive magnitude must resolve a real corner (collinear / endpoint / degenerate corners round nothing). A non-positive
        //    magnitude is the ERASE request (clear the corner back to sharp) — it skips the solve gate, which rejects Magnitude <= 0.
        if (Magnitude > 0.0f)
        {
            const ParametricSketchCornerSolution Solution = SolveCornerEdit(*Shape, Index, Magnitude, Category);
            if (!Solution.Resolved)
                return false;
        }

        const bool ChamferEnabled = (Category == ParametricSketchCornerCategory::Chamfer);

        // Overwrite an existing edit on this corner, else append one. A non-positive magnitude erases the corner's edit (back to sharp).
        ParametricSketchCornerFillet* Existing = nullptr;
        for (ParametricSketchCornerFillet& Candidate : Shape->CornerFillets)
            if (Candidate.CornerIndex == Index)
                Existing = &Candidate;

        if (Magnitude <= 0.0f)
        {
            if (!Existing)
                return false;   // nothing to erase — no edit on this corner
            Shape->CornerFillets.erase(Shape->CornerFillets.begin() + (Existing - Shape->CornerFillets.data()));
        }
        else if (Existing)
        {
            Existing->Magnitude      = Magnitude;
            Existing->ChamferEnabled = ChamferEnabled;
        }
        else
        {
            ParametricSketchCornerFillet Fresh;
            Fresh.CornerIndex    = Index;
            Fresh.Magnitude      = Magnitude;
            Fresh.ChamferEnabled = ChamferEnabled;
            Shape->CornerFillets.push_back(Fresh);
        }

        // Invalidate the flatten cache so the next RetrieveCachedOutline re-solves the corner (no whole-struct replace needed — Points and
        //    the analytic scalars are untouched, so the parametric family stays intact and the edit survives moves / scalar edits).
        Shape->CachedOutlineValid = false;
        Shape->CachedSampleBudget = -1;

        char LogLabel[64];
        const bool Erased = (Magnitude <= 0.0f);
        const char* Verb = Erased ? "Cleared corner" : (ChamferEnabled ? "Chamfered" : "Filleted");
        std::snprintf(LogLabel, sizeof(LogLabel), "%s %s", Verb, Shape->Title);
        // Detail — the corner index + its rounding magnitude (the prototype's Fillet ev.details ["Radius","5.00 mm"],["Corner",…]). An
        //    erase reads the corner back to sharp so the timeline shows what the edit did.
        char LogDetail[64];
        if (Erased)
            std::snprintf(LogDetail, sizeof(LogDetail), "Corner=C%d;Radius=sharp", Index);
        else
            std::snprintf(LogDetail, sizeof(LogDetail), "Corner=C%d;Radius=%.2f mm", Index, Magnitude);
        AppendParametricSketchEditDetailed(Store, Identifier, LogLabel, ChamferEnabled ? "scissors" : "git-commit-horizontal", LogDetail);

        Store.Selected = Identifier;
        return true;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         CORNER SOLVE
//------------------------------------------------------------------------------------------------------------------------

ParametricSketchCornerSolution SolveCornerEdit(const ParametricSketchShape&    Shape,
                                      int                    Index,
                                      float                  Magnitude,
                                      ParametricSketchCornerCategory  Category)
{
    ParametricSketchCornerSolution Solution;
    if (Magnitude <= 0.0f)
        return Solution;

    ImVec2 Previous, Corner, Next;
    if (!ResolveCornerTriple(Shape, Index, Previous, Corner, Next))
        return Solution;

    // Unit leg directions OUT of the corner toward each neighbour.
    ImVec2 LegU, LegV;
    const ImVec2 RawU = ImVec2(Previous.x - Corner.x, Previous.y - Corner.y);
    const ImVec2 RawV = ImVec2(Next.x - Corner.x,     Next.y - Corner.y);
    if (!NormalizeVector(RawU, LegU) || !NormalizeVector(RawV, LegV))
        return Solution;

    const float LegLengthU = VectorLength(RawU);
    const float LegLengthV = VectorLength(RawV);

    // Interior angle between the legs; the half-angle drives both the setback and the arc radius. A near-straight (or fully-folded)
    //    corner has no meaningful fillet — bail so a collinear vertex is left untouched.
    float CosAngle = LegU.x * LegV.x + LegU.y * LegV.y;
    CosAngle = std::max(-1.0f, std::min(1.0f, CosAngle));
    const float FullAngle = std::acos(CosAngle);
    const float HalfAngle = FullAngle * 0.5f;
    const float SinHalf = std::sin(HalfAngle);
    const float TanHalf = std::tan(HalfAngle);
    if (SinHalf < CornerEps || TanHalf < CornerEps || FullAngle > (TwoPi * 0.5f - CornerEps))
        return Solution;

    // Safe limit: the setback T along each leg cannot exceed the shorter leg. For a fillet T = R / tan(half); for a chamfer T = distance
    //    directly. Express the ceiling as the largest radius the legs allow, then derive the per-category clamp from it.
    const float ShortestLeg = std::min(LegLengthU, LegLengthV);
    const float RadiusCeiling = ShortestLeg * TanHalf;   // R for which T == ShortestLeg
    Solution.SafeLimit = (Category == ParametricSketchCornerCategory::Fillet) ? RadiusCeiling : ShortestLeg;

    float Setback;   // [mm] - distance from the corner to each tangent point along its leg
    float Radius;    // [mm] - fillet radius (0 for a chamfer)
    if (Category == ParametricSketchCornerCategory::Fillet)
    {
        Radius  = std::min(Magnitude, RadiusCeiling);
        Setback = Radius / TanHalf;
    }
    else
    {
        Setback = std::min(Magnitude, ShortestLeg);
        Radius  = 0.0f;
    }
    if (Setback < CornerEps)
        return Solution;

    // Tangent points on each leg.
    Solution.TangentA = ImVec2(Corner.x + LegU.x * Setback, Corner.y + LegU.y * Setback);
    Solution.TangentB = ImVec2(Corner.x + LegV.x * Setback, Corner.y + LegV.y * Setback);

    if (Category == ParametricSketchCornerCategory::Fillet)
    {
        // Arc centre lies along the interior bisector at distance R / sin(half) from the corner.
        ImVec2 Bisector;
        if (!NormalizeVector(ImVec2(LegU.x + LegV.x, LegU.y + LegV.y), Bisector))
            return Solution;
        const float CentreDistance = Radius / SinHalf;
        Solution.Centre = ImVec2(Corner.x + Bisector.x * CentreDistance, Corner.y + Bisector.y * CentreDistance);
        Solution.Radius = Radius;

        // Signed sweep from TangentA to TangentB about the centre; pick the short arc (|sweep| = PI - fullAngle) on the interior side.
        const float AngleA = std::atan2(Solution.TangentA.y - Solution.Centre.y, Solution.TangentA.x - Solution.Centre.x);
        const float AngleB = std::atan2(Solution.TangentB.y - Solution.Centre.y, Solution.TangentB.x - Solution.Centre.x);
        float Sweep = AngleB - AngleA;
        while (Sweep <= -TwoPi * 0.5f) Sweep += TwoPi;
        while (Sweep >   TwoPi * 0.5f) Sweep -= TwoPi;
        Solution.StartAngle = AngleA;
        Solution.SweepAngle = Sweep;
    }

    Solution.Resolved = true;
    return Solution;
}

float ResolveCornerSafeLimit(const ParametricSketchShape& Shape, int Index)
{
    // Solve a nominal fillet purely to read its SafeLimit (the geometry cost is trivial; a tiny magnitude still resolves the corner).
    const ParametricSketchCornerSolution Probe = SolveCornerEdit(Shape, Index, CornerEps * 2.0f, ParametricSketchCornerCategory::Fillet);
    return Probe.Resolved ? Probe.SafeLimit : 0.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC EDITS
//------------------------------------------------------------------------------------------------------------------------

bool FilletShapeCorner(ParametricSketchShapeStore& Store, uint32_t Identifier, int Index, float Magnitude)
{
    return ApplyCornerEdit(Store, Identifier, Index, Magnitude, ParametricSketchCornerCategory::Fillet);
}

bool ChamferShapeCorner(ParametricSketchShapeStore& Store, uint32_t Identifier, int Index, float Magnitude)
{
    return ApplyCornerEdit(Store, Identifier, Index, Magnitude, ParametricSketchCornerCategory::Chamfer);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       PARAMETRIC SNAPS
//------------------------------------------------------------------------------------------------------------------------

void EvaluateArcParametricSnaps(const ParametricSketchCornerSolution& Solution, std::vector<ImVec2>& Out)
{
    Out.clear();
    if (!Solution.Resolved || Solution.Radius <= CornerEps)
        return;   // a chamfer / degenerate solution exposes no arc snaps

    // The three interior parametric points (t = 0.25 / 0.5 / 0.75); t = 0 / 1 are the tangent endpoints the caller already has.
    const float Params[3] = { 0.25f, 0.5f, 0.75f };
    for (float T : Params)
    {
        const float Theta = Solution.StartAngle + T * Solution.SweepAngle;
        Out.push_back(ImVec2(Solution.Centre.x + std::cos(Theta) * Solution.Radius,
                             Solution.Centre.y + std::sin(Theta) * Solution.Radius));
    }
    // The mathematical centre / radius point.
    Out.push_back(Solution.Centre);
}

} // namespace Frontier
