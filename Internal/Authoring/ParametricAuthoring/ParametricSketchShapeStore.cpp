/*==============================================================================================================================================
                                                             PARAMETRICSKETCHSHAPESTORE.CPP
==============================================================================================================================================*/
// 🧩 The analytic 2D shape model + per-view store: lazy resolve / release keyed by panel, the append that names a shape + records the
//    edit log, and the analytic distance / pick / length / winding / fill / snap evaluators the view + panels share. Pure CPU +
//    analytic — no pixels stored. Under the Authoring pillar (Authoring/Modeling/ParametricSketching) as the sketch layer's kernel.

#include "ParametricSketchShapeStore.h"

#include <algorithm> // 📝 std::max / std::min / std::reverse — clamp the curve degree + sample budgets, flip the winding.
#include <cmath>     // 📝 std::sqrt / std::atan2 / std::cos / std::sin / std::fmod / std::floor — the analytic evaluators + the stroke-swatch hue walk.
#include <cstdio>    // 📝 std::snprintf — seed a shape's title + a log entry's label.
#include <cstring>   // 📝 std::memcpy — fold shape-outline float bits into the stroke-body change hash.

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr float TwoPi = 6.28318530717958647692f;   // [rad] - a full turn, shared by the round + curve evaluators

    // 📝 A curve closed by clicking back on its own start places a near-duplicate final control point; within this world-mm radius the
    //    periodic evaluator welds it (drops the tail) so the seam fairs smoothly instead of pinching a knot on the collapsed closing span.
    constexpr float ClosingWeldSquared = 0.5f * 0.5f;   // [mm²] - a 0.5 mm coincidence radius (well under any real click gap)

    // 📝 The reader label for a shape category (title stem + history verb source). One place so the outliner + log read the same word.
    const char* ShapeCategoryLabel(ParametricSketchShapeCategory Category)
    {
        switch (Category)
        {
            case ParametricSketchShapeCategory::Line:      return "Line";
            case ParametricSketchShapeCategory::Polyline:  return "Polyline";
            case ParametricSketchShapeCategory::Arc:       return "Arc";
            case ParametricSketchShapeCategory::Bezier:    return "Bezier";
            case ParametricSketchShapeCategory::BSpline:   return "B-Spline";
            case ParametricSketchShapeCategory::Nurbs:     return "NURBS";
            case ParametricSketchShapeCategory::Spline:    return "Spline";
            case ParametricSketchShapeCategory::Conic:     return "Conic";
            case ParametricSketchShapeCategory::Rectangle: return "Rectangle";
            case ParametricSketchShapeCategory::Circle:    return "Circle";
            case ParametricSketchShapeCategory::Ellipse:   return "Ellipse";
            case ParametricSketchShapeCategory::Polygon:   return "Polygon";
            case ParametricSketchShapeCategory::Slot:      return "Slot";
            default:                              return "Shape";
        }
    }

    // 📝 The timeline node icon a shape's history entry leads with (a single-tone Lucide name), mirroring the prototype's FEAT_ICON
    //    map so the History rail reads the same as the design. Parameter edits + folder edits pass their own glyph directly.
    const char* ShapeCategoryGlyph(ParametricSketchShapeCategory Category)
    {
        switch (Category)
        {
            case ParametricSketchShapeCategory::Line:      return "minus";
            case ParametricSketchShapeCategory::Polyline:  return "spline";
            case ParametricSketchShapeCategory::Arc:       return "git-commit-horizontal";
            case ParametricSketchShapeCategory::Bezier:    return "pen-tool";
            case ParametricSketchShapeCategory::BSpline:   return "spline";
            case ParametricSketchShapeCategory::Nurbs:     return "spline";
            case ParametricSketchShapeCategory::Spline:    return "spline";
            case ParametricSketchShapeCategory::Conic:     return "git-commit-horizontal";
            case ParametricSketchShapeCategory::Rectangle: return "square";
            case ParametricSketchShapeCategory::Circle:    return "circle";
            case ParametricSketchShapeCategory::Ellipse:   return "egg";
            case ParametricSketchShapeCategory::Polygon:   return "hexagon";
            case ParametricSketchShapeCategory::Slot:      return "rectangle-horizontal";
            default:                              return "dot";
        }
    }

    // 📝 The squared distance from Point to the segment [First, Second] — the standard clamped-projection form, squared to defer the
    //    sqrt to the caller so a nearest-of-many scan compares cheaply. All in world mm.
    float DistanceSquaredPointToSegment(ImVec2 Point, ImVec2 First, ImVec2 Second)
    {
        const float SegmentX = Second.x - First.x;
        const float SegmentY = Second.y - First.y;
        const float LengthSquared = SegmentX * SegmentX + SegmentY * SegmentY;
        float Parameter = 0.0f;
        if (LengthSquared > 1e-9f)
            Parameter = ((Point.x - First.x) * SegmentX + (Point.y - First.y) * SegmentY) / LengthSquared;
        Parameter = Parameter < 0.0f ? 0.0f : (Parameter > 1.0f ? 1.0f : Parameter);
        const float ProjectedX = First.x + Parameter * SegmentX;
        const float ProjectedY = First.y + Parameter * SegmentY;
        const float DeltaX = Point.x - ProjectedX;
        const float DeltaY = Point.y - ProjectedY;
        return DeltaX * DeltaX + DeltaY * DeltaY;
    }

    // 📝 The nearest point on the segment [First, Second] to Point (the clamped projection itself, not its distance). Used by the
    //    along-curve snap to return the caught outline position.
    ImVec2 NearestOnSegment(ImVec2 Point, ImVec2 First, ImVec2 Second)
    {
        const float SegmentX = Second.x - First.x;
        const float SegmentY = Second.y - First.y;
        const float LengthSquared = SegmentX * SegmentX + SegmentY * SegmentY;
        float Parameter = 0.0f;
        if (LengthSquared > 1e-9f)
            Parameter = ((Point.x - First.x) * SegmentX + (Point.y - First.y) * SegmentY) / LengthSquared;
        Parameter = Parameter < 0.0f ? 0.0f : (Parameter > 1.0f ? 1.0f : Parameter);
        return ImVec2(First.x + Parameter * SegmentX, First.y + Parameter * SegmentY);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  CURVE EVALUATORS
    //--------------------------------------------------------------------------------------------------------------------
    // 📝 Ported verbatim from the CAD prototype's CurveEvaluators.js so the C++ sketch layer draws every curve with the SAME math the
    //    design was validated against (de Casteljau, Cox-de Boor, rational conic, rotated ellipse, open-uniform knots). Each maps a
    //    parameter T in [0,1] to a world-mm position; EvaluateShapePolyline samples them into the display polyline.

    // De Casteljau — a Bezier of arbitrary degree (degree = point count - 1). Repeatedly lerps neighbouring points toward T.
    ImVec2 EvaluateDeCasteljau(const std::vector<ImVec2>& Points, float T)
    {
        if (Points.empty())
            return ImVec2(0, 0);
        std::vector<ImVec2> Working = Points;
        while (Working.size() > 1)
        {
            for (size_t Index = 0; Index + 1 < Working.size(); ++Index)
                Working[Index] = ImVec2(Working[Index].x * (1.0f - T) + Working[Index + 1].x * T,
                                        Working[Index].y * (1.0f - T) + Working[Index + 1].y * T);
            Working.pop_back();
        }
        return Working[0];
    }

    // Catmull-Rom position across an interpolating spline of Points at global parameter T in [0,1]. The endpoints are duplicated so the
    //    curve passes through the first + last control points (the Spline tool's interpolating feel, distinct from the Bezier hull).
    ImVec2 EvaluateCatmullRom(const std::vector<ImVec2>& Points, float T)
    {
        const int Count = (int)Points.size();
        if (Count == 0) return ImVec2(0, 0);
        if (Count == 1) return Points[0];
        if (Count == 2) return ImVec2(Points[0].x * (1.0f - T) + Points[1].x * T,
                                      Points[0].y * (1.0f - T) + Points[1].y * T);
        const int   Spans = Count - 1;
        float       Scaled = std::min(1.0f, std::max(0.0f, T)) * (float)Spans;
        int         Span = (int)Scaled;
        if (Span >= Spans) Span = Spans - 1;
        const float Local = Scaled - (float)Span;
        auto At = [&](int Index) -> ImVec2
        {
            const int Clamped = Index < 0 ? 0 : (Index >= Count ? Count - 1 : Index);
            return Points[Clamped];
        };
        const ImVec2 P0 = At(Span - 1), P1 = At(Span), P2 = At(Span + 1), P3 = At(Span + 2);
        const float T2 = Local * Local, T3 = T2 * Local;
        const float B0 = -0.5f * T3 + T2 - 0.5f * Local;
        const float B1 =  1.5f * T3 - 2.5f * T2 + 1.0f;
        const float B2 = -1.5f * T3 + 2.0f * T2 + 0.5f * Local;
        const float B3 =  0.5f * T3 - 0.5f * T2;
        return ImVec2(P0.x * B0 + P1.x * B1 + P2.x * B2 + P3.x * B3,
                      P0.y * B0 + P1.y * B1 + P2.y * B2 + P3.y * B3);
    }

    // Cox-de Boor basis N(Index, Degree) at U over the knot vector — the recursive B-spline basis. Right-closed at the domain end so
    //    the last span includes U == KnotMax, matching the prototype so the curve reaches its final control point.
    float EvaluateCoxDeBoor(int Index, int Degree, float U, const std::vector<float>& Knots)
    {
        if (Degree == 0)
        {
            const int Last = (int)Knots.size() - 1;
            if (Index == Last - 1 && U >= Knots[Index] && U <= Knots[Index + 1])
                return 1.0f;
            return (U >= Knots[Index] && U < Knots[Index + 1]) ? 1.0f : 0.0f;
        }
        float Left = 0.0f, Right = 0.0f;
        const float DenomLeft  = Knots[Index + Degree] - Knots[Index];
        const float DenomRight = Knots[Index + Degree + 1] - Knots[Index + 1];
        if (DenomLeft > 1e-12f)
            Left = ((U - Knots[Index]) / DenomLeft) * EvaluateCoxDeBoor(Index, Degree - 1, U, Knots);
        if (DenomRight > 1e-12f)
            Right = ((Knots[Index + Degree + 1] - U) / DenomRight) * EvaluateCoxDeBoor(Index + 1, Degree - 1, U, Knots);
        return Left + Right;
    }

    // Open-uniform (clamped) knot vector: Degree+1 leading zeros, evenly spaced interior knots, Degree+1 trailing ones. This clamps the
    //    curve to its first + last control points. Mirrors ConstructOpenUniformKnots from the prototype.
    std::vector<float> ConstructOpenUniformKnots(int Count, int Degree)
    {
        const int Order = Degree + 1;
        const int Interior = std::max(0, Count - Order);
        std::vector<float> Knots;
        Knots.reserve(Order * 2 + Interior);
        for (int Index = 0; Index < Order; ++Index)
            Knots.push_back(0.0f);
        for (int Index = 1; Index <= Interior; ++Index)
            Knots.push_back((float)Index / (float)(Interior + 1));
        for (int Index = 0; Index < Order; ++Index)
            Knots.push_back(1.0f);
        return Knots;
    }

    // Rational B-spline (NURBS) position at T. Weights null → all-1 (a plain B-spline). Under-determined (Count <= Degree) falls back
    //    to a Bezier blend, exactly as the prototype does.
    ImVec2 EvaluateRationalBSpline(const std::vector<ImVec2>& Points,
                                   const std::vector<float>&  Weights,
                                   const std::vector<float>&  Knots,
                                   int                        Degree,
                                   float                      T)
    {
        const int Count = (int)Points.size();
        if (Count == 0) return ImVec2(0, 0);
        if (Count <= Degree || (int)Knots.size() < Count + Degree + 1)
            return EvaluateDeCasteljau(Points, T);

        const float DomainMin = Knots[Degree];
        const float DomainMax = Knots[Count];
        const float U = DomainMin + (DomainMax - DomainMin) * std::min(1.0f, std::max(0.0f, T));

        float NumeratorX = 0.0f, NumeratorY = 0.0f, Denominator = 0.0f;
        for (int Index = 0; Index < Count; ++Index)
        {
            const float Basis  = EvaluateCoxDeBoor(Index, Degree, U, Knots);
            const float Weight = Weights.empty() ? 1.0f : Weights[Index];
            const float Blended = Basis * Weight;
            NumeratorX += Blended * Points[Index].x;
            NumeratorY += Blended * Points[Index].y;
            Denominator += Blended;
        }
        if (Denominator < 1e-9f)
            return Points[Count - 1];
        return ImVec2(NumeratorX / Denominator, NumeratorY / Denominator);
    }

    // Rational-quadratic conic through [Start, Shoulder, End], the middle weighted by Rho (0.5 = parabola). Bernstein-blended in
    //    homogeneous coords, divided back. Mirrors EvaluateConic.
    ImVec2 EvaluateConicPosition(const std::vector<ImVec2>& Points, float Rho, float T)
    {
        if (Points.size() < 3)
            return EvaluateDeCasteljau(Points, T);
        const float ClampedRho = std::min(0.98f, std::max(0.02f, Rho));
        const float MiddleWeight = ClampedRho / (1.0f - ClampedRho);
        const float Weights[3] = { 1.0f, MiddleWeight, 1.0f };
        const float Bernstein[3] = { (1.0f - T) * (1.0f - T), 2.0f * (1.0f - T) * T, T * T };
        float NumeratorX = 0.0f, NumeratorY = 0.0f, Denominator = 0.0f;
        for (int Index = 0; Index < 3; ++Index)
        {
            const float Blended = Bernstein[Index] * Weights[Index];
            NumeratorX += Blended * Points[Index].x;
            NumeratorY += Blended * Points[Index].y;
            Denominator += Blended;
        }
        if (Denominator < 1e-9f)
            return Points[2];
        return ImVec2(NumeratorX / Denominator, NumeratorY / Denominator);
    }

    // Periodic (closed) Catmull-Rom position at global T in [0,1] over Points treated as a LOOP: the parameter sweeps every span
    //    including the final Points[N-1] → Points[0] closing span AS A CURVE, and the tangent neighbours wrap around (P[-1] = P[N-1],
    //    P[N] = P[0]) so the seam is C¹-continuous. This is the smooth "rubber-band" a closed curve is expected to read as — replacing
    //    the straight last-sample → first-sample chord that produced a cusp / knot at the join. Interpolates the control points, so a
    //    closed Spline / BSpline / Bezier all fair into one smooth loop through their hull. Count < 3 has no meaningful loop (a 2-point
    //    "loop" is a degenerate back-and-forth) so it falls back to the open evaluator's straight blend at the call site.
    ImVec2 EvaluateClosedCatmullRom(const std::vector<ImVec2>& Points, float T)
    {
        // Weld a coincident closing control point. When a curve is closed by clicking back onto its own start, the last placed point
        //    lands on (or a hair from) Points[0]; the periodic wrap then walks a near-zero closing span between two coincident anchors,
        //    which collapses the seam tangent and pinches a visible knot at the join. Dropping that duplicate leaves the wrap to fair the
        //    real first → real last span smoothly (verified: the seam turn falls from ~15° back to the clean ~5°). A local view over the
        //    weld point so the caller's Points stay intact (the raw click remains a grabbable vertex).
        const ImVec2* Data  = Points.data();
        int           Count = (int)Points.size();
        if (Count >= 4)
        {
            const ImVec2 Head = Data[0], Tail = Data[Count - 1];
            const float  GapX = Tail.x - Head.x, GapY = Tail.y - Head.y;
            if (GapX * GapX + GapY * GapY <= ClosingWeldSquared)
                --Count;   // ignore the duplicate tail; the wrap supplies the closing span
        }
        if (Count == 0) return ImVec2(0, 0);
        if (Count == 1) return Data[0];
        // N spans around the loop (the extra span is the closing P[N-1] → P[0] edge, walked as a curve not a chord).
        const int   Spans  = Count;
        const float Scaled = std::min(1.0f, std::max(0.0f, T)) * (float)Spans;
        int         Span   = (int)Scaled;
        if (Span >= Spans) Span = Spans - 1;
        const float Local  = Scaled - (float)Span;
        auto At = [&](int Index) -> ImVec2
        {
            // Wrap-around indexing: the neighbours of the seam come from the far side of the loop, giving matched tangents at the join.
            int Wrapped = Index % Count;
            if (Wrapped < 0) Wrapped += Count;
            return Data[Wrapped];
        };
        const ImVec2 P0 = At(Span - 1), P1 = At(Span), P2 = At(Span + 1), P3 = At(Span + 2);
        const float T2 = Local * Local, T3 = T2 * Local;
        const float B0 = -0.5f * T3 + T2 - 0.5f * Local;
        const float B1 =  1.5f * T3 - 2.5f * T2 + 1.0f;
        const float B2 = -1.5f * T3 + 2.0f * T2 + 0.5f * Local;
        const float B3 =  0.5f * T3 - 0.5f * T2;
        return ImVec2(P0.x * B0 + P1.x * B1 + P2.x * B2 + P3.x * B3,
                      P0.y * B0 + P1.y * B1 + P2.y * B2 + P3.y * B3);
    }

    // Rotated-ellipse position at T (full sweep over [0,1]). Local parametric point scaled by the axes, rotated, offset by the centre.
    ImVec2 EvaluateEllipsePosition(const ParametricSketchShape& Shape, float T)
    {
        const float Theta = T * TwoPi;
        const float Cos = std::cos(Theta), Sin = std::sin(Theta);
        const float CosR = std::cos(Shape.Rotation), SinR = std::sin(Shape.Rotation);
        const float LocalX = Cos * Shape.MajorAxis;
        const float LocalY = Sin * Shape.MinorAxis;
        return ImVec2(Shape.Centre.x + LocalX * CosR - LocalY * SinR,
                      Shape.Centre.y + LocalX * SinR + LocalY * CosR);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  ROUND-FAMILY SOLVERS
    //--------------------------------------------------------------------------------------------------------------------

    // Circumcentre of three points (null returned as ResolvedEnabled = false when collinear). Ported from ArcTool.Circumcenter.
    bool SolveCircumcentre(ImVec2 A, ImVec2 B, ImVec2 C, ImVec2& Centre)
    {
        const float D = 2.0f * (A.x * (B.y - C.y) + B.x * (C.y - A.y) + C.x * (A.y - B.y));
        if (std::fabs(D) < 1e-9f)
            return false;
        const float A2 = A.x * A.x + A.y * A.y;
        const float B2 = B.x * B.x + B.y * B.y;
        const float C2 = C.x * C.x + C.y * C.y;
        Centre = ImVec2((A2 * (B.y - C.y) + B2 * (C.y - A.y) + C2 * (A.y - B.y)) / D,
                        (A2 * (C.x - B.x) + B2 * (A.x - C.x) + C2 * (B.x - A.x)) / D);
        return true;
    }

    // Wrap an angle into [0, 2π). Mirrors ArcTool.Normalize.
    float NormalizeAngle(float Angle)
    {
        float Wrapped = std::fmod(Angle, TwoPi);
        if (Wrapped < 0.0f)
            Wrapped += TwoPi;
        return Wrapped;
    }

    // 📝 Round ONE corner of an outline for DISPLAY only: given the incoming / corner / outgoing outline vertices, solve the tangent arc
    //    (Fillet) or the straight setback edge (Chamfer) at Magnitude and emit the replacement run into Out (in loop order, corner point
    //    dropped). Returns false — leaving Out untouched so the caller keeps the raw corner — for a degenerate corner (collinear legs,
    //    a zero leg, a non-positive magnitude). Self-contained analytic geometry (mirrors ParametricSketchFillet's SolveCornerEdit) so the flatten
    //    layer stays independent of the fillet edit module. Segment count is chord-bounded: ~one segment per 9°, 2..64.
    bool ExpandCornerFilletSpan(ImVec2               Previous,
                                ImVec2               Corner,
                                ImVec2               Next,
                                float                Magnitude,
                                bool                 ChamferEnabled,
                                float                PixelsPerMm,
                                std::vector<ImVec2>& Out)
    {
        constexpr float CornerEps = 1e-4f;
        if (Magnitude <= CornerEps)
            return false;

        // Unit leg directions OUT of the corner toward each neighbour + their lengths.
        const ImVec2 RawU = ImVec2(Previous.x - Corner.x, Previous.y - Corner.y);
        const ImVec2 RawV = ImVec2(Next.x - Corner.x,     Next.y - Corner.y);
        const float  LegLengthU = std::sqrt(RawU.x * RawU.x + RawU.y * RawU.y);
        const float  LegLengthV = std::sqrt(RawV.x * RawV.x + RawV.y * RawV.y);
        if (LegLengthU < CornerEps || LegLengthV < CornerEps)
            return false;
        const ImVec2 LegU = ImVec2(RawU.x / LegLengthU, RawU.y / LegLengthU);
        const ImVec2 LegV = ImVec2(RawV.x / LegLengthV, RawV.y / LegLengthV);

        // Interior half-angle drives the setback + arc radius; a near-straight / fully-folded corner rounds nothing.
        float CosAngle = LegU.x * LegV.x + LegU.y * LegV.y;
        CosAngle = std::max(-1.0f, std::min(1.0f, CosAngle));
        const float FullAngle = std::acos(CosAngle);
        const float HalfAngle = FullAngle * 0.5f;
        const float SinHalf   = std::sin(HalfAngle);
        const float TanHalf   = std::tan(HalfAngle);
        if (SinHalf < CornerEps || TanHalf < CornerEps || FullAngle > (TwoPi * 0.5f - CornerEps))
            return false;

        const float ShortestLeg   = std::min(LegLengthU, LegLengthV);
        const float RadiusCeiling = ShortestLeg * TanHalf;   // R for which the setback == the shorter leg

        float Setback;   // [mm] - distance corner → tangent point along each leg
        float Radius;    // [mm] - fillet radius (0 for a chamfer)
        if (ChamferEnabled)
        {
            Setback = std::min(Magnitude, ShortestLeg);
            Radius  = 0.0f;
        }
        else
        {
            Radius  = std::min(Magnitude, RadiusCeiling);
            Setback = Radius / TanHalf;
        }
        if (Setback < CornerEps)
            return false;

        const ImVec2 TangentA = ImVec2(Corner.x + LegU.x * Setback, Corner.y + LegU.y * Setback);
        const ImVec2 TangentB = ImVec2(Corner.x + LegV.x * Setback, Corner.y + LegV.y * Setback);

        if (ChamferEnabled)
        {
            // A straight edge: the corner becomes its two setback points.
            Out.push_back(TangentA);
            Out.push_back(TangentB);
            return true;
        }

        // Fillet: the arc centre lies along the interior bisector at R / sin(half) from the corner; sweep the short arc A → B.
        const float BisX = LegU.x + LegV.x, BisY = LegU.y + LegV.y;
        const float BisLength = std::sqrt(BisX * BisX + BisY * BisY);
        if (BisLength < CornerEps)
            return false;
        const float CentreDistance = Radius / SinHalf;
        const ImVec2 Centre = ImVec2(Corner.x + (BisX / BisLength) * CentreDistance,
                                     Corner.y + (BisY / BisLength) * CentreDistance);

        const float AngleA = std::atan2(TangentA.y - Centre.y, TangentA.x - Centre.x);
        const float AngleB = std::atan2(TangentB.y - Centre.y, TangentB.x - Centre.x);
        float Sweep = AngleB - AngleA;
        while (Sweep <= -TwoPi * 0.5f) Sweep += TwoPi;
        while (Sweep >   TwoPi * 0.5f) Sweep -= TwoPi;

        // Screen-space chord tessellation: size the segment count so each chord spans ~one TargetChordPixels on screen at the current
        //    zoom, exactly as ResolveAdaptiveSampleBudget does for the whole-shape curves. The arc's on-screen length is |Sweep|·R·Scale;
        //    dividing by the chord target gives a count that refines as you zoom in (a true smooth curve) and coarsens far out (cheap),
        //    so a fillet never reads as a plotted polyline. Clamped 4..256: never visibly faceted, never an unbounded per-frame cost.
        constexpr float TargetChordPixels = 6.0f;
        const float     Scale        = PixelsPerMm > 1e-6f ? PixelsPerMm : 1.0f;
        const float     ScreenArcLen = std::fabs(Sweep) * Radius * Scale;
        const int Segments = std::max(4, std::min(256, (int)std::ceil(ScreenArcLen / TargetChordPixels)));
        Out.reserve(Out.size() + Segments + 1);
        for (int Step = 0; Step <= Segments; ++Step)
        {
            const float Theta = AngleA + ((float)Step / (float)Segments) * Sweep;
            Out.push_back(ImVec2(Centre.x + std::cos(Theta) * Radius,
                                 Centre.y + std::sin(Theta) * Radius));
        }
        return true;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       PER-PANEL STATE
//------------------------------------------------------------------------------------------------------------------------

ParametricSketchShapeStore& ResolveParametricSketchShapeStore(ParametricSketchShapeRegistry& Registry, uint32_t OwnerDocument)
{
    for (ParametricSketchShapeStore& Entry : Registry.Stores)
        if (Entry.OwnerDocument == OwnerDocument)
            return Entry;
    ParametricSketchShapeStore Fresh;
    Fresh.OwnerDocument = OwnerDocument;
    Registry.Stores.push_back(Fresh);
    return Registry.Stores.back();
}

void ReleaseParametricSketchShapeStore(ParametricSketchShapeRegistry& Registry, uint32_t OwnerDocument)
{
    for (size_t Index = 0; Index < Registry.Stores.size(); ++Index)
        if (Registry.Stores[Index].OwnerDocument == OwnerDocument)
        {
            Registry.Stores.erase(Registry.Stores.begin() + Index);
            return;
        }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        SOURCE BRIDGE
//------------------------------------------------------------------------------------------------------------------------

// 📝 The active parametricSketching store the runtime publishes each frame so the separate outliner / Properties / History boxes read + edit
//    the same shapes without threading a pointer through the dock-host chain. Null outside a live parametricSketching view. Mirrors the Scene
//    outliner's RegisterSceneOutlinerSource bridge exactly.
namespace
{
    ParametricSketchShapeStore* PublishedShapeSource = nullptr;
    ParametricSketchSceneView   PublishedSceneView;     // [-] - reset to a not-ready default each time the runtime clears the bridge
    ImTextureID        PublishedSolidImage  = (ImTextureID)0;   // [-] - the offscreen solid target the view composites (0 = none this frame)
    uint32_t           PublishedSolidWidth  = 0;                // [px] - the published image's live extent width
    uint32_t           PublishedSolidHeight = 0;                // [px] - the published image's live extent height
    ImTextureID        PublishedStrokeImage  = (ImTextureID)0;  // [-] - the offscreen outline/curve target the view composites ON TOP (0 = none this frame)
    uint32_t           PublishedStrokeWidth  = 0;               // [px] - the published stroke image's live extent width
    uint32_t           PublishedStrokeHeight = 0;               // [px] - the published stroke image's live extent height
    std::vector<ParametricSketchShapeBody> PublishedShapeBodies;        // [-] - tessellated closed-shape / solid bodies the GPU scene pass draws
    std::vector<ParametricSketchStrokeBody> PublishedStrokeBodies;      // [-] - flattened shape outlines the GPU curve pass strokes
}

void RegisterParametricSketchShapeSource(ParametricSketchShapeStore* Store)
{
    PublishedShapeSource = Store;
}

ParametricSketchShapeStore* RetrieveParametricSketchShapeSource()
{
    return PublishedShapeSource;
}

void RegisterParametricSketchSceneView(const ParametricSketchSceneView& View)
{
    PublishedSceneView = View;
}

ParametricSketchSceneView RetrieveParametricSketchSceneView()
{
    return PublishedSceneView;
}

void RegisterParametricSketchSolidImage(ImTextureID Image, uint32_t Width, uint32_t Height)
{
    PublishedSolidImage  = Image;
    PublishedSolidWidth  = Width;
    PublishedSolidHeight = Height;
}

ImTextureID RetrieveParametricSketchSolidImage(uint32_t& Width, uint32_t& Height)
{
    Width  = PublishedSolidWidth;
    Height = PublishedSolidHeight;
    return PublishedSolidImage;
}

void RegisterParametricSketchStrokeImage(ImTextureID Image, uint32_t Width, uint32_t Height)
{
    PublishedStrokeImage  = Image;
    PublishedStrokeWidth  = Width;
    PublishedStrokeHeight = Height;
}

ImTextureID RetrieveParametricSketchStrokeImage(uint32_t& Width, uint32_t& Height)
{
    Width  = PublishedStrokeWidth;
    Height = PublishedStrokeHeight;
    return PublishedStrokeImage;
}

void RegisterParametricSketchShapeBodies(const std::vector<ParametricSketchShapeBody>& Bodies)
{
    PublishedShapeBodies = Bodies;
}

const std::vector<ParametricSketchShapeBody>& RetrieveParametricSketchShapeBodies()
{
    return PublishedShapeBodies;
}

void RegisterParametricSketchStrokeBodies(const std::vector<ParametricSketchStrokeBody>& Bodies)
{
    PublishedStrokeBodies = Bodies;
}

const std::vector<ParametricSketchStrokeBody>& RetrieveParametricSketchStrokeBodies()
{
    return PublishedStrokeBodies;
}

namespace
{
    // 📝 Resolve a stroke swatch from a shape's rolling TintIndex without a tint table in this pillar (the outliner owns the display ladder; the
    //    GPU pass must not depend on it). A golden-ratio hue walk gives distinct, evenly-spread hues for consecutive ids, converted from a fixed
    //    high-value / mid-saturation HSV so every stroke reads bright on the dark canvas. Index 0 (the default) still lands on a stable hue.
    void ResolveStrokeSwatch(uint32_t TintIndex, float OutRGBA[4])
    {
        const float Hue        = std::fmod(static_cast<float>(TintIndex) * 0.61803398875f, 1.0f); // golden-ratio conjugate walk
        const float Saturation = 0.45f;
        const float Value      = 0.95f;
        const float Sector     = Hue * 6.0f;
        const int   Index      = static_cast<int>(Sector) % 6;
        const float Fraction   = Sector - std::floor(Sector);
        const float P = Value * (1.0f - Saturation);
        const float Q = Value * (1.0f - Saturation * Fraction);
        const float T = Value * (1.0f - Saturation * (1.0f - Fraction));
        float R = Value, G = Value, B = Value;
        switch (Index)
        {
            case 0: R = Value; G = T;     B = P;     break;
            case 1: R = Q;     G = Value; B = P;     break;
            case 2: R = P;     G = Value; B = T;     break;
            case 3: R = P;     G = Q;     B = Value; break;
            case 4: R = T;     G = P;     B = Value; break;
            default:R = Value; G = P;     B = Q;     break;
        }
        OutRGBA[0] = R; OutRGBA[1] = G; OutRGBA[2] = B; OutRGBA[3] = 1.0f;
    }

    // 📝 A change-sensitive revision for one shape's OUTLINE, so the GPU consumer re-uploads only when the flattened outline actually moved. The
    //    shape carries no monotonic revision field, so this hashes the outline itself (already flattened this frame) plus the closed flag: any
    //    geometry edit rewrites the outline through ConstructParametricSketchShape, changing the hash. An FNV-1a walk over the point floats +
    //    point count is enough to distinguish edits; a hash collision only costs a skipped re-upload, which the sample-budget field below guards
    //    against separately (a re-flatten at a new budget changes the point count, so the hash moves).
    uint32_t HashOutlineRevision(const std::vector<ImVec2>& Outline, bool ClosedLoop)
    {
        uint32_t Hash = 2166136261u;                       // FNV-1a offset basis
        auto Fold = [&Hash](uint32_t Word)
        {
            Hash ^= Word;
            Hash *= 16777619u;                             // FNV-1a prime
        };
        Fold(static_cast<uint32_t>(Outline.size()));
        Fold(ClosedLoop ? 1u : 0u);
        for (const ImVec2& Point : Outline)
        {
            uint32_t Bits;
            std::memcpy(&Bits, &Point.x, sizeof(Bits)); Fold(Bits);
            std::memcpy(&Bits, &Point.y, sizeof(Bits)); Fold(Bits);
        }
        return Hash;
    }
}

void AssembleParametricSketchStrokeBodies(ParametricSketchShapeStore&              Store,
                                          std::vector<ParametricSketchStrokeBody>& OutBodies,
                                          int                                      SampleBudget)
{
    OutBodies.clear();
    OutBodies.reserve(Store.Shapes.size());

    for (ParametricSketchShape& Shape : Store.Shapes)
    {
        if (!Shape.Displayed)
            continue;

        // 📝 One flatten path only — RetrieveCachedOutline warms + returns the same cached outline the pick / length / CPU-render loops use, so
        //    the GPU stroke can never drift from the CPU outline. A shape with fewer than two samples has no segment to stroke.
        const std::vector<ImVec2>& Outline = RetrieveCachedOutline(Shape, SampleBudget);
        if (Outline.size() < 2)
            continue;

        ParametricSketchStrokeBody Body;
        Body.Identifier = Shape.Identifier;
        Body.ClosedLoop = Shape.ClosedEnabled;
        Body.Revision   = HashOutlineRevision(Outline, Body.ClosedLoop);
        // 📝 Construction geometry (locked reference shapes) strokes dashed; everything else solid this pass. Centerline (style 2) is reserved for
        //    the datum-axis work and not emitted from a plain shape walk yet.
        Body.LineStyle  = Shape.LockEnabled ? 1u : 0u;
        ResolveStrokeSwatch(Shape.TintIndex, Body.ColourRGBA);

        Body.Polyline.reserve(Outline.size());
        for (const ImVec2& Point : Outline)
        {
            ParametricSketchStrokeVertex Vertex;
            Vertex.PositionX = Point.x;
            Vertex.PositionY = Point.y;
            Vertex.PositionZ = Shape.Elevation;   // lift the outline to the shape's sketch-plane height
            Body.Polyline.push_back(Vertex);
        }

        OutBodies.push_back(std::move(Body));
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         SHAPE MODEL
//------------------------------------------------------------------------------------------------------------------------

int ResolveParametricSketchDefiningCount(ParametricSketchShapeCategory Category)
{
    switch (Category)
    {
        case ParametricSketchShapeCategory::Line:      return 2;
        case ParametricSketchShapeCategory::Arc:       return 3;
        case ParametricSketchShapeCategory::Conic:     return 3;
        case ParametricSketchShapeCategory::Rectangle: return 2;
        case ParametricSketchShapeCategory::Circle:    return 2;
        case ParametricSketchShapeCategory::Ellipse:   return 3;
        case ParametricSketchShapeCategory::Polygon:   return 2;
        case ParametricSketchShapeCategory::Slot:      return 3;
        default:                              return 0;   // Polyline / Bezier / BSpline / Nurbs / Spline collect open-ended
    }
}

ParametricSketchShape ConstructParametricSketchShape(ParametricSketchShapeCategory       Category,
                                   const std::vector<ImVec2>& DefiningPoints,
                                   int                        SideCountOverride,
                                   float                      RhoOverride,
                                   int                        DegreeOverride)
{
    ParametricSketchShape Fresh;
    Fresh.Category = Category;
    Fresh.Points   = DefiningPoints;
    if (RhoOverride > 0.0f)       Fresh.Rho       = RhoOverride;
    if (DegreeOverride > 0)       Fresh.Degree    = DegreeOverride;
    if (SideCountOverride >= 3)   Fresh.SideCount = SideCountOverride;

    switch (Category)
    {
        case ParametricSketchShapeCategory::Arc:
        {
            // Three clicks (start, end, through) → the circumscribed circle; pick the sweep direction containing the through point.
            if (DefiningPoints.size() >= 3)
            {
                const ImVec2 Start = DefiningPoints[0], End = DefiningPoints[1], Through = DefiningPoints[2];
                ImVec2 Centre;
                if (SolveCircumcentre(Start, Through, End, Centre))
                {
                    Fresh.Centre = Centre;
                    Fresh.Radius = std::sqrt((Start.x - Centre.x) * (Start.x - Centre.x) +
                                             (Start.y - Centre.y) * (Start.y - Centre.y));
                    Fresh.StartAngle = std::atan2(Start.y - Centre.y, Start.x - Centre.x);
                    const float EndAngle     = std::atan2(End.y - Centre.y, End.x - Centre.x);
                    const float ThroughAngle = std::atan2(Through.y - Centre.y, Through.x - Centre.x);
                    float Sweep        = NormalizeAngle(EndAngle - Fresh.StartAngle);
                    const float ThroughSweep = NormalizeAngle(ThroughAngle - Fresh.StartAngle);
                    if (ThroughSweep > Sweep)
                        Sweep -= TwoPi;
                    Fresh.SweepAngle = Sweep;
                }
            }
            break;
        }
        case ParametricSketchShapeCategory::Circle:
        {
            // Centre + a radius handle. A full circle is a closed loop, so it fills like the other round families.
            if (DefiningPoints.size() >= 2)
            {
                Fresh.Centre = DefiningPoints[0];
                const ImVec2 Handle = DefiningPoints[1];
                Fresh.Radius = std::sqrt((Handle.x - Fresh.Centre.x) * (Handle.x - Fresh.Centre.x) +
                                         (Handle.y - Fresh.Centre.y) * (Handle.y - Fresh.Centre.y));
                Fresh.ClosedEnabled = true;
            }
            break;
        }
        case ParametricSketchShapeCategory::Ellipse:
        {
            // Centre, a major-axis end (sets major length + rotation), and a third click whose perpendicular distance sets the minor.
            if (DefiningPoints.size() >= 3)
            {
                Fresh.Centre = DefiningPoints[0];
                const ImVec2 MajorEnd = DefiningPoints[1];
                const ImVec2 MinorHandle = DefiningPoints[2];
                Fresh.MajorAxis = std::sqrt((MajorEnd.x - Fresh.Centre.x) * (MajorEnd.x - Fresh.Centre.x) +
                                            (MajorEnd.y - Fresh.Centre.y) * (MajorEnd.y - Fresh.Centre.y));
                Fresh.Rotation = std::atan2(MajorEnd.y - Fresh.Centre.y, MajorEnd.x - Fresh.Centre.x);
                const float DX = MinorHandle.x - Fresh.Centre.x;
                const float DY = MinorHandle.y - Fresh.Centre.y;
                Fresh.MinorAxis = std::fabs(-std::sin(Fresh.Rotation) * DX + std::cos(Fresh.Rotation) * DY);
                Fresh.ClosedEnabled = true;
            }
            break;
        }
        case ParametricSketchShapeCategory::Polygon:
        {
            // Centre + a vertex handle: the handle fixes the circum-radius + base rotation; SideCount governs the vertex count.
            if (DefiningPoints.size() >= 2)
            {
                Fresh.Centre = DefiningPoints[0];
                const ImVec2 Handle = DefiningPoints[1];
                Fresh.Radius   = std::sqrt((Handle.x - Fresh.Centre.x) * (Handle.x - Fresh.Centre.x) +
                                           (Handle.y - Fresh.Centre.y) * (Handle.y - Fresh.Centre.y));
                Fresh.Rotation = std::atan2(Handle.y - Fresh.Centre.y, Handle.x - Fresh.Centre.x);
                Fresh.ClosedEnabled = true;
            }
            break;
        }
        case ParametricSketchShapeCategory::Slot:
        {
            // Two endpoints (A, B) fix the slot spine; the third click's perpendicular distance from that spine sets the half-width
            //    radius (the semicircle cap + the parallel-edge offset). Points keeps the two spine endpoints; the outline flattens
            //    from them + Radius. A closed stadium (obround) loop.
            if (DefiningPoints.size() >= 2)
            {
                const ImVec2 A = DefiningPoints[0], B = DefiningPoints[1];
                Fresh.Points = { A, B };
                Fresh.Centre = ImVec2((A.x + B.x) * 0.5f, (A.y + B.y) * 0.5f);
                float SpanX = B.x - A.x, SpanY = B.y - A.y;
                const float SpanLength = std::sqrt(SpanX * SpanX + SpanY * SpanY);
                if (DefiningPoints.size() >= 3 && SpanLength > 1e-6f)
                {
                    // Perpendicular distance of the third click from the A->B spine = the half-width.
                    const ImVec2 Handle = DefiningPoints[2];
                    const float NormalX = -SpanY / SpanLength, NormalY = SpanX / SpanLength;
                    Fresh.Radius = std::fabs((Handle.x - A.x) * NormalX + (Handle.y - A.y) * NormalY);
                }
                Fresh.ClosedEnabled = true;
            }
            break;
        }
        case ParametricSketchShapeCategory::Conic:
        {
            // Clicks arrive in gesture order [Start, End, Shoulder]; reorder to the [Start, Shoulder, End] control triple the
            //    rational-quadratic evaluator expects. Rho carries the shoulder weight (0.5 = parabola default).
            if (DefiningPoints.size() >= 3)
                Fresh.Points = { DefiningPoints[0], DefiningPoints[2], DefiningPoints[1] };
            break;
        }
        case ParametricSketchShapeCategory::Rectangle:
        {
            // Two opposite corners → four corners, closed. Expand into Points so the outline draws + picks as a plain loop. When
            //    re-solving from the four-corner output (a transform hands back all four points, whose [0] / [1] are ADJACENT, not
            //    opposite), take the AXIS-ALIGNED BOUNDARY of every supplied point as the two opposite corners — so a moved / scaled
            //    rectangle re-solves to the right box instead of collapsing to a degenerate diagonal line.
            if (DefiningPoints.size() >= 2)
            {
                ImVec2 A = DefiningPoints[0];
                ImVec2 B = DefiningPoints[1];
                if (DefiningPoints.size() >= 3)
                {
                    float MinimumX = DefiningPoints[0].x, MaximumX = DefiningPoints[0].x;
                    float MinimumY = DefiningPoints[0].y, MaximumY = DefiningPoints[0].y;
                    for (const ImVec2& Corner : DefiningPoints)
                    {
                        MinimumX = std::min(MinimumX, Corner.x);
                        MaximumX = std::max(MaximumX, Corner.x);
                        MinimumY = std::min(MinimumY, Corner.y);
                        MaximumY = std::max(MaximumY, Corner.y);
                    }
                    A = ImVec2(MinimumX, MinimumY);
                    B = ImVec2(MaximumX, MaximumY);
                }
                Fresh.Points = { ImVec2(A.x, A.y), ImVec2(B.x, A.y), ImVec2(B.x, B.y), ImVec2(A.x, B.y) };
                Fresh.ClosedEnabled = true;
            }
            break;
        }
        case ParametricSketchShapeCategory::BSpline:
        case ParametricSketchShapeCategory::Nurbs:
        {
            // Clamp the degree to the control-point count so a short run still evaluates (falls back to a Bezier blend inside).
            Fresh.Degree = std::min(Fresh.Degree, std::max(1, (int)DefiningPoints.size() - 1));
            break;
        }
        default:
            break;   // Line / Polyline / Bezier / Spline keep their points verbatim
    }
    return Fresh;
}

void EvaluateShapePolyline(const ParametricSketchShape& Shape, std::vector<ImVec2>& Polyline, int SampleBudget)
{
    Polyline.clear();
    switch (Shape.Category)
    {
        case ParametricSketchShapeCategory::Circle:
        {
            const int Segments = SampleBudget > 0 ? SampleBudget : 64;
            Polyline.reserve(Segments + 1);
            for (int Index = 0; Index <= Segments; ++Index)
            {
                const float Theta = ((float)Index / (float)Segments) * TwoPi;
                Polyline.push_back(ImVec2(Shape.Centre.x + std::cos(Theta) * Shape.Radius,
                                          Shape.Centre.y + std::sin(Theta) * Shape.Radius));
            }
            break;
        }
        case ParametricSketchShapeCategory::Arc:
        {
            const int Base = SampleBudget > 0 ? SampleBudget : 64;
            const int Steps = std::max(2, (int)std::ceil((std::fabs(Shape.SweepAngle) / TwoPi) * (float)Base));
            Polyline.reserve(Steps + 1);
            for (int Index = 0; Index <= Steps; ++Index)
            {
                const float Theta = Shape.StartAngle + ((float)Index / (float)Steps) * Shape.SweepAngle;
                Polyline.push_back(ImVec2(Shape.Centre.x + std::cos(Theta) * Shape.Radius,
                                          Shape.Centre.y + std::sin(Theta) * Shape.Radius));
            }
            break;
        }
        case ParametricSketchShapeCategory::Ellipse:
        {
            const int Segments = SampleBudget > 0 ? SampleBudget : 72;
            Polyline.reserve(Segments + 1);
            for (int Index = 0; Index <= Segments; ++Index)
                Polyline.push_back(EvaluateEllipsePosition(Shape, (float)Index / (float)Segments));
            break;
        }
        case ParametricSketchShapeCategory::Polygon:
        {
            const int Sides = std::max(3, Shape.SideCount);
            Polyline.reserve(Sides);
            for (int Index = 0; Index < Sides; ++Index)
            {
                const float Theta = Shape.Rotation + ((float)Index / (float)Sides) * TwoPi;
                Polyline.push_back(ImVec2(Shape.Centre.x + std::cos(Theta) * Shape.Radius,
                                          Shape.Centre.y + std::sin(Theta) * Shape.Radius));
            }
            break;
        }
        case ParametricSketchShapeCategory::Bezier:
        {
            if (Shape.Points.size() < 2) { Polyline = Shape.Points; break; }
            const int Samples = SampleBudget > 0 ? SampleBudget : 48;
            Polyline.reserve(Samples + 1);
            // Closed (≥3 pts): sweep the periodic loop through the control points so the join fairs smoothly (a lone Bezier cannot be
            //    periodic, so a closed Bezier reads as a smooth interpolating loop through its hull — the rubber-band, not a knot).
            if (Shape.ClosedEnabled && Shape.Points.size() >= 3)
            {
                for (int Index = 0; Index <= Samples; ++Index)
                    Polyline.push_back(EvaluateClosedCatmullRom(Shape.Points, (float)Index / (float)Samples));
                break;
            }
            for (int Index = 0; Index <= Samples; ++Index)
                Polyline.push_back(EvaluateDeCasteljau(Shape.Points, (float)Index / (float)Samples));
            break;
        }
        case ParametricSketchShapeCategory::Spline:
        {
            if (Shape.Points.size() < 3) { Polyline = Shape.Points; break; }
            const int Samples = SampleBudget > 0 ? SampleBudget : 48;
            Polyline.reserve(Samples + 1);
            // Closed: wrap the Catmull-Rom neighbours around the loop so the seam tangent matches — a smooth closed spline, no cusp.
            const bool Loop = Shape.ClosedEnabled;
            for (int Index = 0; Index <= Samples; ++Index)
            {
                const float Parameter = (float)Index / (float)Samples;
                Polyline.push_back(Loop ? EvaluateClosedCatmullRom(Shape.Points, Parameter)
                                        : EvaluateCatmullRom(Shape.Points, Parameter));
            }
            break;
        }
        case ParametricSketchShapeCategory::BSpline:
        case ParametricSketchShapeCategory::Nurbs:
        {
            const int Count = (int)Shape.Points.size();
            if (Count < 2) { Polyline = Shape.Points; break; }
            const int Samples = SampleBudget > 0 ? SampleBudget : 48;
            Polyline.reserve(Samples + 1);
            // Closed (≥3 pts): a clamped open B-spline does not return to its first control point, so joining last→first chords a cusp.
            //    Sweep the periodic loop through the control hull instead (matched tangents at the seam), the smooth closed profile.
            if (Shape.ClosedEnabled && Count >= 3)
            {
                for (int Index = 0; Index <= Samples; ++Index)
                    Polyline.push_back(EvaluateClosedCatmullRom(Shape.Points, (float)Index / (float)Samples));
                break;
            }
            const int Degree = std::min(std::max(1, Shape.Degree), Count - 1);
            const std::vector<float> Knots = ConstructOpenUniformKnots(Count, Degree);
            const std::vector<float> Weights;   // unit weights this phase (NURBS distinct entity, same numeric core)
            for (int Index = 0; Index <= Samples; ++Index)
                Polyline.push_back(EvaluateRationalBSpline(Shape.Points, Weights, Knots, Degree,
                                                           (float)Index / (float)Samples));
            break;
        }
        case ParametricSketchShapeCategory::Conic:
        {
            if (Shape.Points.size() < 3) { Polyline = Shape.Points; break; }
            const int Samples = SampleBudget > 0 ? SampleBudget : 48;
            Polyline.reserve(Samples + 1);
            for (int Index = 0; Index <= Samples; ++Index)
                Polyline.push_back(EvaluateConicPosition(Shape.Points, Shape.Rho, (float)Index / (float)Samples));
            break;
        }
        case ParametricSketchShapeCategory::Slot:
        {
            // Stadium (obround): the spine [A, B] offset by ±Radius along its normal gives the two parallel edges, joined by a
            //    semicircle cap swept about each endpoint. Walk cap-B (outer arc), down one edge to cap-A, sweep cap-A, back up the
            //    other edge — one closed loop. Degenerate spine (A == B) falls back to a plain circle about the midpoint.
            if (Shape.Points.size() < 2 || Shape.Radius <= 1e-6f) { Polyline = Shape.Points; break; }
            const ImVec2 A = Shape.Points[0], B = Shape.Points[1];
            const float SpanX = B.x - A.x, SpanY = B.y - A.y;
            const float SpanLength = std::sqrt(SpanX * SpanX + SpanY * SpanY);
            const int CapSteps = SampleBudget > 0 ? std::max(2, SampleBudget / 2) : 16;
            if (SpanLength < 1e-6f)
            {
                Polyline.reserve(CapSteps * 2 + 1);
                for (int Index = 0; Index <= CapSteps * 2; ++Index)
                {
                    const float Theta = ((float)Index / (float)(CapSteps * 2)) * TwoPi;
                    Polyline.push_back(ImVec2(A.x + std::cos(Theta) * Shape.Radius,
                                              A.y + std::sin(Theta) * Shape.Radius));
                }
                break;
            }
            // Spine direction + its left normal; the cap at each endpoint sweeps a half turn centred on the outward spine direction.
            const float DirX = SpanX / SpanLength, DirY = SpanY / SpanLength;
            const float BaseAngle = std::atan2(DirY, DirX);
            Polyline.reserve(CapSteps * 2 + 4);
            // Cap at B: sweep from +normal to -normal through the +direction side (angle BaseAngle - π/2 .. BaseAngle + π/2).
            for (int Index = 0; Index <= CapSteps; ++Index)
            {
                const float Theta = BaseAngle - 1.57079632679f + ((float)Index / (float)CapSteps) * 3.14159265359f;
                Polyline.push_back(ImVec2(B.x + std::cos(Theta) * Shape.Radius,
                                          B.y + std::sin(Theta) * Shape.Radius));
            }
            // Cap at A: sweep the opposite half (angle BaseAngle + π/2 .. BaseAngle + 3π/2), closing the loop back at cap-B's start.
            for (int Index = 0; Index <= CapSteps; ++Index)
            {
                const float Theta = BaseAngle + 1.57079632679f + ((float)Index / (float)CapSteps) * 3.14159265359f;
                Polyline.push_back(ImVec2(A.x + std::cos(Theta) * Shape.Radius,
                                          A.y + std::sin(Theta) * Shape.Radius));
            }
            break;
        }
        default:
            Polyline = Shape.Points;   // Line / Polyline / Rectangle carry their vertices verbatim
            break;
    }

    // Parametric corner fillets / chamfers are solved + tessellated HERE (display only): the defining corner stays one real vertex in
    //    Points (so it reads as a single grabbable corner + one clean curve, never a run of stored dots); each edit rounds its corner in
    //    the flattened outline. Only the straight-vertex families whose outline is Points verbatim (Line / Polyline / Rectangle / Profile)
    //    carry corners to round — the round families (Circle / Arc / …) have no corner index, so a mismatched count skips the pass safely.
    if (!Shape.CornerFillets.empty() && Polyline.size() == Shape.Points.size() && Shape.Points.size() >= 3)
    {
        const int Count = (int)Shape.Points.size();

        // The fillet arcs tessellate to the SAME on-screen chord target the rest of the outline uses, so a rounded corner reads as one
        //    smooth curve at every zoom (never a plotted polyline). EvaluateShapePolyline is handed a whole-shape SampleBudget, not the
        //    zoom directly — but for these straight-vertex families ResolveAdaptiveSampleBudget set Budget ≈ Perimeter·PixelsPerMm / 6,
        //    so PixelsPerMm ≈ Budget·6 / Perimeter recovers the effective zoom to drive the per-arc chord count. A zero budget (the fixed
        //    callers: pick / boolean / offset) yields a neutral scale, keeping their fillet arcs at the clamp floor — smooth enough, cheap.
        float Perimeter = 0.0f;
        for (int Edge = 0; Edge < Count; ++Edge)
        {
            const ImVec2 A = Shape.Points[Edge];
            const ImVec2 B = Shape.Points[(Edge + 1) % Count];
            Perimeter += std::sqrt((B.x - A.x) * (B.x - A.x) + (B.y - A.y) * (B.y - A.y));
        }
        float EffectivePixelsPerMm = 1.0f;
        if (SampleBudget > 0 && Perimeter > 1e-4f)
            EffectivePixelsPerMm = (float)SampleBudget * 6.0f / Perimeter;

        std::vector<ImVec2> Rounded;
        Rounded.reserve(Count * 2);
        for (int Walk = 0; Walk < Count; ++Walk)
        {
            // The active fillet edit on this corner (last wins if duplicated); -1 magnitude / absent = leave the raw vertex.
            const ParametricSketchCornerFillet* Edit = nullptr;
            for (const ParametricSketchCornerFillet& Candidate : Shape.CornerFillets)
                if (Candidate.CornerIndex == Walk && Candidate.Magnitude > 0.0f)
                    Edit = &Candidate;

            // A corner needs both neighbours: a closed loop wraps them; an open run's two endpoints have only one leg, so they never round.
            bool HasBothLegs = Shape.ClosedEnabled || (Walk > 0 && Walk < Count - 1);
            if (Edit && HasBothLegs)
            {
                const ImVec2 Previous = Shape.Points[(Walk - 1 + Count) % Count];
                const ImVec2 Corner   = Shape.Points[Walk];
                const ImVec2 Next      = Shape.Points[(Walk + 1) % Count];
                if (ExpandCornerFilletSpan(Previous, Corner, Next, Edit->Magnitude, Edit->ChamferEnabled, EffectivePixelsPerMm, Rounded))
                    continue;   // the solved arc / edge replaced this corner
            }
            Rounded.push_back(Shape.Points[Walk]);   // untouched or degenerate corner: keep the raw vertex
        }
        Polyline.swap(Rounded);
    }
}

const std::vector<ImVec2>& RetrieveCachedOutline(ParametricSketchShape& Shape, int SampleBudget)
{
    // Rebuild only when the memo is stale — invalid (a fresh / edited struct) or built at a different budget. The idle-frame common
    // case (no edit, same budget) returns the stored polyline with no re-tessellation.
    if (Shape.CachedOutlineValid && Shape.CachedSampleBudget == SampleBudget)
        return Shape.CachedOutline;

    EvaluateShapePolyline(Shape, Shape.CachedOutline, SampleBudget);
    Shape.CachedSampleBudget = SampleBudget;
    Shape.CachedOutlineValid = true;

    // Recompute the AABB the broad-phase cull tests. Empty outline → a degenerate box at the origin (the cull then rejects it).
    if (Shape.CachedOutline.empty())
    {
        Shape.CachedBoundaryMinimum = ImVec2(0, 0);
        Shape.CachedBoundaryMaximum = ImVec2(0, 0);
    }
    else
    {
        ImVec2 Minimum = Shape.CachedOutline[0];
        ImVec2 Maximum = Shape.CachedOutline[0];
        for (const ImVec2& Point : Shape.CachedOutline)
        {
            Minimum.x = std::min(Minimum.x, Point.x);
            Minimum.y = std::min(Minimum.y, Point.y);
            Maximum.x = std::max(Maximum.x, Point.x);
            Maximum.y = std::max(Maximum.y, Point.y);
        }
        Shape.CachedBoundaryMinimum = Minimum;
        Shape.CachedBoundaryMaximum = Maximum;
    }
    return Shape.CachedOutline;
}

bool ResolveAnalyticEdgeSpan(ParametricSketchShape&        Shape,
                             int                  IndexA,
                             int                  IndexB,
                             int                  SampleBudget,
                             std::vector<ImVec2>& OutSpan)
{
    // The analytic span of ONE edge = the sub-run of the display outline that runs from defining vertex IndexA to IndexB. When a
    //    fillet / chamfer rounds either endpoint, the corner vertex is REPLACED in the flattened outline by its arc / setback run, so the
    //    span naturally follows that smooth curve (never the raw corner, never a ring of stray sample dots). This is the entity an edge
    //    pick names: the outline from one real endpoint to the next, curve and all — exactly what the user asked to select over the dots.
    OutSpan.clear();
    const int PointCount = (int)Shape.Points.size();
    if (PointCount < 2 || IndexA < 0 || IndexB < 0 || IndexA >= PointCount || IndexB >= PointCount)
        return false;

    const std::vector<ImVec2>& Outline = RetrieveCachedOutline(Shape, SampleBudget);
    const int OutlineCount = (int)Outline.size();
    if (OutlineCount < 2)
        return false;

    // Locate the outline sample nearest each endpoint's world position. With no fillet the corner IS an outline vertex (exact hit); with
    //    a fillet the nearest sample is the arc's tangent point at that corner — the correct span boundary either way.
    const ImVec2 WorldA = Shape.Points[IndexA];
    const ImVec2 WorldB = Shape.Points[IndexB];
    int   NearestA = 0, NearestB = 0;
    float BestA = 1e30f, BestB = 1e30f;
    for (int Index = 0; Index < OutlineCount; ++Index)
    {
        const ImVec2& Sample = Outline[Index];
        const float DistanceA = (Sample.x - WorldA.x) * (Sample.x - WorldA.x) + (Sample.y - WorldA.y) * (Sample.y - WorldA.y);
        const float DistanceB = (Sample.x - WorldB.x) * (Sample.x - WorldB.x) + (Sample.y - WorldB.y) * (Sample.y - WorldB.y);
        if (DistanceA < BestA) { BestA = DistanceA; NearestA = Index; }
        if (DistanceB < BestB) { BestB = DistanceB; NearestB = Index; }
    }

    // Walk forward NearestA → NearestB along the outline (wrapping on a closed loop). The forward run is the edge; the wrap-around run
    //    would be the whole rest of the outline, so a closed shape always takes the shorter forward arc between adjacent corners.
    const bool Wraps = Shape.ClosedEnabled;
    int Walk = NearestA;
    OutSpan.push_back(Outline[Walk]);
    for (int Guard = 0; Guard < OutlineCount; ++Guard)
    {
        if (Walk == NearestB)
            break;
        Walk = (Walk + 1) % OutlineCount;
        if (Walk == 0 && !Wraps)
            break;
        OutSpan.push_back(Outline[Walk]);
    }
    return OutSpan.size() >= 2;
}

int ResolveAdaptiveSampleBudget(const ParametricSketchShape& Shape, float PixelsPerMm)
{
    // Polygon is a true n-gon — its vertex count is geometry, not a sampling budget; leave it to the fixed path.
    if (Shape.Category == ParametricSketchShapeCategory::Polygon)
        return 0;

    constexpr float TargetChordPixels = 6.0f;    // [px]  - the on-screen chord length one flattened segment aims for
    constexpr int   MinimumSamples    = 12;      // [-]   - never coarser than this (a tiny curve still reads smooth)
    constexpr int   MaximumSamples    = 512;     // [-]   - never finer than this (bound the per-frame flatten cost)
    const float Scale = PixelsPerMm > 1e-6f ? PixelsPerMm : 1.0f;

    // Estimate the shape's world-mm length cheaply per family: the analytic circumference for the round shapes, the control-hull
    //    perimeter for the free curves. This is the on-screen arc length once scaled — the segment count that keeps chord error low.
    float WorldLength = 0.0f;
    switch (Shape.Category)
    {
        case ParametricSketchShapeCategory::Circle:
            WorldLength = TwoPi * Shape.Radius;
            break;
        case ParametricSketchShapeCategory::Arc:
            WorldLength = std::fabs(Shape.SweepAngle) * Shape.Radius;
            break;
        case ParametricSketchShapeCategory::Ellipse:
        {
            // Ramanujan's first approximation of an ellipse perimeter — accurate enough to size the budget.
            const float A = Shape.MajorAxis, B = Shape.MinorAxis;
            WorldLength = 3.14159265359f * (3.0f * (A + B) - std::sqrt((3.0f * A + B) * (A + 3.0f * B)));
            break;
        }
        case ParametricSketchShapeCategory::Slot:
        {
            // Two straight edges of the spine length + two semicircle caps (= one full circle of the half-width radius).
            float SpanLength = 0.0f;
            if (Shape.Points.size() >= 2)
            {
                const float DX = Shape.Points[1].x - Shape.Points[0].x;
                const float DY = Shape.Points[1].y - Shape.Points[0].y;
                SpanLength = std::sqrt(DX * DX + DY * DY);
            }
            WorldLength = 2.0f * SpanLength + TwoPi * Shape.Radius;
            break;
        }
        default:
        {
            // The free curves (Bezier / Spline / BSpline / Nurbs / Conic) — the control-hull perimeter bounds the true arc length.
            for (size_t Index = 0; Index + 1 < Shape.Points.size(); ++Index)
            {
                const float DX = Shape.Points[Index + 1].x - Shape.Points[Index].x;
                const float DY = Shape.Points[Index + 1].y - Shape.Points[Index].y;
                WorldLength += std::sqrt(DX * DX + DY * DY);
            }
            break;
        }
    }

    const float ScreenLength = WorldLength * Scale;
    int Budget = (int)std::ceil(ScreenLength / TargetChordPixels);
    if (Budget < MinimumSamples) Budget = MinimumSamples;
    if (Budget > MaximumSamples) Budget = MaximumSamples;
    return Budget;
}

uint32_t AppendParametricSketchShape(ParametricSketchShapeStore&         Store,
                            ParametricSketchShapeCategory       Category,
                            const std::vector<ImVec2>& DefiningPoints,
                            int                        SideCountOverride,
                            float                      RhoOverride,
                            int                        DegreeOverride)
{
    const int Required = ResolveParametricSketchDefiningCount(Category);
    const int Minimum  = Required > 0 ? Required : 2;   // open-ended families still need at least two points
    if ((int)DefiningPoints.size() < Minimum)
        return 0;

    ParametricSketchShape Fresh = ConstructParametricSketchShape(Category, DefiningPoints, SideCountOverride, RhoOverride, DegreeOverride);
    Fresh.Identifier = Store.NextIdentifier++;
    Fresh.TintIndex  = Fresh.Identifier;   // a rolling cue; the outliner maps it into its tint ladder
    std::snprintf(Fresh.Title, sizeof(Fresh.Title), "%s %u", ShapeCategoryLabel(Category), Fresh.Identifier);

    Store.Shapes.push_back(Fresh);

    char LogLabel[48];
    std::snprintf(LogLabel, sizeof(LogLabel), "Added %s", Fresh.Title);
    // Detail — the new shape's defining-point count + its total length (the prototype's creation ev.details ["Entities",…],["Length",…]).
    char LogDetail[64];
    std::snprintf(LogDetail, sizeof(LogDetail), "Points=%d;Length=%.2f mm", (int)Fresh.Points.size(), EvaluateParametricSketchShapeLength(Fresh));
    AppendParametricSketchEditDetailed(Store, Fresh.Identifier, LogLabel, ShapeCategoryGlyph(Category), LogDetail);

    return Fresh.Identifier;
}

ParametricSketchShape* ResolveParametricSketchShape(ParametricSketchShapeStore& Store, uint32_t Identifier)
{
    if (Identifier == 0)
        return nullptr;
    for (ParametricSketchShape& Entry : Store.Shapes)
        if (Entry.Identifier == Identifier)
            return &Entry;
    return nullptr;
}

uint32_t AppendInscription(ParametricSketchShapeStore& Store, ImVec2 AnchorMm, const char* Text)
{
    ParametricSketchInscription Fresh;
    Fresh.Identifier = Store.NextInscriptionIdentifier++;
    Fresh.Anchor     = AnchorMm;
    Fresh.TintIndex  = Fresh.Identifier;   // a rolling cue; the outliner maps it into its tint ladder (mirrors AppendParametricSketchShape)
    // 📝 Seed a visible default when the placement tool passes no text, so a freshly placed inscription renders immediately (the Properties
    //    card that edits the string is a later follow-up — until then a blank placement would be invisible on the canvas).
    if (Text != nullptr && Text[0] != '\0')
        std::snprintf(Fresh.Text, sizeof(Fresh.Text), "%s", Text);
    else
        std::snprintf(Fresh.Text, sizeof(Fresh.Text), "%s", "Text");

    Store.Inscriptions.push_back(Fresh);

    char LogLabel[48];
    std::snprintf(LogLabel, sizeof(LogLabel), "Added Inscription %u", Fresh.Identifier);
    AppendParametricSketchEdit(Store, 0, LogLabel, "type");

    return Fresh.Identifier;
}

ParametricSketchInscription* ResolveInscription(ParametricSketchShapeStore& Store, uint32_t Identifier)
{
    if (Identifier == 0)
        return nullptr;
    for (ParametricSketchInscription& Entry : Store.Inscriptions)
        if (Entry.Identifier == Identifier)
            return &Entry;
    return nullptr;
}

void EnforceInscriptionAnchor(ParametricSketchInscription& Inscription, ImVec2 AnchorMm)
{
    Inscription.Anchor = AnchorMm;
}

void DetachInscription(ParametricSketchShapeStore& Store, uint32_t Identifier)
{
    if (Identifier == 0)
        return;
    bool Removed = false;
    for (size_t Index = 0; Index < Store.Inscriptions.size(); ++Index)
        if (Store.Inscriptions[Index].Identifier == Identifier)
        {
            Store.Inscriptions.erase(Store.Inscriptions.begin() + Index);
            Removed = true;
            break;
        }
    if (!Removed)
        return;

    if (Store.SelectedInscription == Identifier) Store.SelectedInscription = 0;
    if (Store.HoveredInscription  == Identifier) Store.HoveredInscription  = 0;

    char LogLabel[48];
    std::snprintf(LogLabel, sizeof(LogLabel), "Removed Inscription %u", Identifier);
    AppendParametricSketchEdit(Store, 0, LogLabel, "inscribe");
}

uint32_t AppendDatum(ParametricSketchShapeStore& Store, ImVec2 AnchorMm)
{
    ParametricSketchDatum Fresh;
    Fresh.Identifier  = Store.NextDatumIdentifier++;
    Fresh.Anchor      = AnchorMm;
    Fresh.AxisDisplay = ParametricSketchDatumAxis::Cross;   // default: a full cross so a fresh datum reads as a reference frame immediately
    Fresh.TintIndex   = Fresh.Identifier;          // a rolling cue; the outliner maps it into its tint ladder (mirrors AppendInscription)
    std::snprintf(Fresh.Title, sizeof(Fresh.Title), "Datum %u", Fresh.Identifier);

    Store.Datums.push_back(Fresh);

    char LogLabel[48];
    std::snprintf(LogLabel, sizeof(LogLabel), "Added Datum %u", Fresh.Identifier);
    AppendParametricSketchEdit(Store, 0, LogLabel, "crosshair");

    return Fresh.Identifier;
}

ParametricSketchDatum* ResolveDatum(ParametricSketchShapeStore& Store, uint32_t Identifier)
{
    if (Identifier == 0)
        return nullptr;
    for (ParametricSketchDatum& Entry : Store.Datums)
        if (Entry.Identifier == Identifier)
            return &Entry;
    return nullptr;
}

void EnforceDatumAnchor(ParametricSketchDatum& Datum, ImVec2 AnchorMm)
{
    Datum.Anchor = AnchorMm;
}

uint32_t PickDatum(ParametricSketchShapeStore& Store, ImVec2 WorldPoint, float ToleranceMm)
{
    uint32_t Nearest        = 0;
    float    NearestSquared = ToleranceMm * ToleranceMm;
    for (const ParametricSketchDatum& Entry : Store.Datums)
    {
        if (!Entry.Displayed || Entry.LockEnabled)
            continue;
        const float DeltaX  = WorldPoint.x - Entry.Anchor.x;
        const float DeltaY  = WorldPoint.y - Entry.Anchor.y;
        const float Squared = DeltaX * DeltaX + DeltaY * DeltaY;
        if (Squared <= NearestSquared)
        {
            NearestSquared = Squared;
            Nearest        = Entry.Identifier;
        }
    }
    return Nearest;
}

void DetachDatum(ParametricSketchShapeStore& Store, uint32_t Identifier)
{
    if (Identifier == 0)
        return;
    bool Removed = false;
    for (size_t Index = 0; Index < Store.Datums.size(); ++Index)
        if (Store.Datums[Index].Identifier == Identifier)
        {
            Store.Datums.erase(Store.Datums.begin() + Index);
            Removed = true;
            break;
        }
    if (!Removed)
        return;

    if (Store.SelectedDatum == Identifier) Store.SelectedDatum = 0;
    if (Store.HoveredDatum  == Identifier) Store.HoveredDatum  = 0;

    char LogLabel[48];
    std::snprintf(LogLabel, sizeof(LogLabel), "Removed Datum %u", Identifier);
    AppendParametricSketchEdit(Store, 0, LogLabel, "crosshair");
}

float EvaluateDistanceToShape(ParametricSketchShape& Shape, ImVec2 WorldPoint, int SampleBudget)
{
    const std::vector<ImVec2>& Polyline = RetrieveCachedOutline(Shape, SampleBudget);
    const size_t PointCount = Polyline.size();
    if (PointCount == 0)
        return 1e30f;
    if (PointCount == 1)
    {
        const float DeltaX = WorldPoint.x - Polyline[0].x;
        const float DeltaY = WorldPoint.y - Polyline[0].y;
        return std::sqrt(DeltaX * DeltaX + DeltaY * DeltaY);
    }

    float NearestSquared = 1e30f;
    for (size_t Index = 0; Index + 1 < PointCount; ++Index)
    {
        const float Candidate = DistanceSquaredPointToSegment(WorldPoint, Polyline[Index], Polyline[Index + 1]);
        if (Candidate < NearestSquared)
            NearestSquared = Candidate;
    }
    if (Shape.ClosedEnabled && PointCount >= 3)
    {
        const float Candidate = DistanceSquaredPointToSegment(WorldPoint, Polyline[PointCount - 1], Polyline[0]);
        if (Candidate < NearestSquared)
            NearestSquared = Candidate;
    }
    return std::sqrt(NearestSquared);
}

uint32_t PickParametricSketchShape(ParametricSketchShapeStore& Store, ImVec2 WorldPoint, float ToleranceMm)
{
    uint32_t NearestIdentifier = 0;
    float    NearestDistance   = ToleranceMm;
    for (ParametricSketchShape& Entry : Store.Shapes)
    {
        if (!Entry.Displayed)
            continue;
        // Broad-phase: warm the cache (cheap when already valid) then reject any shape whose AABB, expanded by the catch radius,
        // does not contain the cursor — skips the point-to-segment sweep for every far shape (the common case is ~1 near shape).
        RetrieveCachedOutline(Entry);
        if (WorldPoint.x < Entry.CachedBoundaryMinimum.x - ToleranceMm ||
            WorldPoint.x > Entry.CachedBoundaryMaximum.x + ToleranceMm ||
            WorldPoint.y < Entry.CachedBoundaryMinimum.y - ToleranceMm ||
            WorldPoint.y > Entry.CachedBoundaryMaximum.y + ToleranceMm)
            continue;
        const float Distance = EvaluateDistanceToShape(Entry, WorldPoint);
        if (Distance <= NearestDistance)
        {
            NearestDistance   = Distance;
            NearestIdentifier = Entry.Identifier;
        }
    }
    return NearestIdentifier;
}

float EvaluateParametricSketchShapeLength(const ParametricSketchShape& Shape)
{
    std::vector<ImVec2> Polyline;
    EvaluateShapePolyline(Shape, Polyline);
    const size_t PointCount = Polyline.size();
    if (PointCount < 2)
        return 0.0f;

    float Total = 0.0f;
    for (size_t Index = 0; Index + 1 < PointCount; ++Index)
    {
        const float DeltaX = Polyline[Index + 1].x - Polyline[Index].x;
        const float DeltaY = Polyline[Index + 1].y - Polyline[Index].y;
        Total += std::sqrt(DeltaX * DeltaX + DeltaY * DeltaY);
    }
    if (Shape.ClosedEnabled && PointCount >= 3)
    {
        const float DeltaX = Polyline[0].x - Polyline[PointCount - 1].x;
        const float DeltaY = Polyline[0].y - Polyline[PointCount - 1].y;
        Total += std::sqrt(DeltaX * DeltaX + DeltaY * DeltaY);
    }
    return Total;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WINDING + FILL
//------------------------------------------------------------------------------------------------------------------------

float EvaluateSignedArea(const std::vector<ImVec2>& Polyline)
{
    const size_t Count = Polyline.size();
    if (Count < 3)
        return 0.0f;
    float Twice = 0.0f;
    for (size_t Index = 0; Index < Count; ++Index)
    {
        const ImVec2& Current = Polyline[Index];
        const ImVec2& Next     = Polyline[(Index + 1) % Count];
        Twice += Current.x * Next.y - Next.x * Current.y;
    }
    return Twice * 0.5f;
}

void EnforceWindingOrder(std::vector<ImVec2>& Polyline, bool CounterClockwiseEnabled)
{
    const float Area = EvaluateSignedArea(Polyline);
    if (std::fabs(Area) < 1e-9f)
        return;
    const bool CurrentlyCounterClockwise = Area > 0.0f;
    if (CurrentlyCounterClockwise != CounterClockwiseEnabled)
        std::reverse(Polyline.begin(), Polyline.end());
}

void EvaluateFilledPolygon(const ParametricSketchShape& Shape, std::vector<ImVec2>& OutPolyline, int SampleBudget)
{
    OutPolyline.clear();
    if (!Shape.ClosedEnabled)
        return;

    EvaluateShapePolyline(Shape, OutPolyline, SampleBudget);
    if (OutPolyline.size() < 3)
    {
        OutPolyline.clear();
        return;
    }

    // Drop a trailing duplicate of the first sample (the round evaluators emit a closing point) so the fill run has no zero-length edge.
    const ImVec2& First = OutPolyline.front();
    const ImVec2& Last  = OutPolyline.back();
    if (std::fabs(First.x - Last.x) < 1e-6f && std::fabs(First.y - Last.y) < 1e-6f)
        OutPolyline.pop_back();
    if (OutPolyline.size() < 3)
    {
        OutPolyline.clear();
        return;
    }

    // Orient counter-clockwise so every fill reads consistently regardless of the user's click order.
    EnforceWindingOrder(OutPolyline, true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         SNAPPING
//------------------------------------------------------------------------------------------------------------------------

ParametricSketchSnapCandidate ResolveSnapCandidate(ParametricSketchShapeStore& Store,
                                          ImVec2             WorldCursor,
                                          float              PixelsPerMm,
                                          unsigned           Mask)
{
    ParametricSketchSnapCandidate Best;
    constexpr float TolerancePixels = 11.0f;  // [px] - the on-screen catch radius (converted to world mm by the scale)
    // Clamp the incoming scale to a sane px/mm band before inverting it. A momentary bad measurement (a degenerate projection step,
    //    a not-yet-settled camera on the first frame after a zoom) can hand in a scale far from the true value; left unclamped that
    //    poisons ToleranceMm and the catch radius collapses (snaps when zoomed in, misses when zoomed out — a pure scale artefact).
    //    The band keeps the on-screen radius honest across the whole working zoom range regardless of one stray frame.
    const float Scale = std::min(400.0f, std::max(0.02f, PixelsPerMm > 1e-6f ? PixelsPerMm : 1.0f));
    const float ToleranceMm = TolerancePixels / Scale;
    float NearestSquared = ToleranceMm * ToleranceMm;

    // Endpoints out-rank the continuous targets: a curve tip / vertex is a precise feature the user is usually AIMING at, while the
    //    along-curve nearest point is almost always microscopically closer (it slides to meet the cursor), so without a bias the endpoint
    //    could never win near a tip. Endpoints are compared against the true radius; the softer targets (Midpoint / Center / AlongCurve)
    //    are compared against a SHRUNK radius, so an endpoint within the full radius beats a nearer curve point unless the endpoint is
    //    clearly out of reach. This is the standard CAD "vertices beat edges" snap priority.
    constexpr float SoftBias = 0.55f;   // [-] - the softer targets must be this fraction (or nearer) of the radius to beat an endpoint
    const float     SoftSquared = NearestSquared * SoftBias * SoftBias;

    auto Consider = [&](ImVec2 Candidate, ParametricSketchSnapCategory Category)
    {
        const float DX = Candidate.x - WorldCursor.x;
        const float DY = Candidate.y - WorldCursor.y;
        const float DistanceSquared = DX * DX + DY * DY;
        // An endpoint catch already latched keeps the floor unless a NEARER endpoint arrives; a softer target must beat the shrunk
        //    radius AND the current best to displace it. Endpoints always win a tie against a soft target at the same distance.
        const bool  Endpoint = Category == ParametricSketchSnapCategory::Endpoint;
        const float Ceiling  = Endpoint ? NearestSquared : SoftSquared;
        if (DistanceSquared > Ceiling)
            return;
        if (Best.Resolved && Best.Category == ParametricSketchSnapCategory::Endpoint && !Endpoint)
            return;   // never let a soft target overwrite an already-caught endpoint
        NearestSquared = DistanceSquared;
        Best.Point    = Candidate;
        Best.Category = Category;
        Best.Resolved = true;
    };

    const bool EndpointEnabled = (Mask & (1u << (unsigned)ParametricSketchSnapCategory::Endpoint))   != 0;
    const bool MidpointEnabled = (Mask & (1u << (unsigned)ParametricSketchSnapCategory::Midpoint))   != 0;
    const bool CenterEnabled   = (Mask & (1u << (unsigned)ParametricSketchSnapCategory::Center))     != 0;
    const bool AlongEnabled    = (Mask & (1u << (unsigned)ParametricSketchSnapCategory::AlongCurve)) != 0;

    // The IN-PROGRESS draw's own placed points are endpoint targets too — they are not yet in Store.Shapes, so without this the point a
    //    user is CLOSING a curve onto (its first placed vertex) can never be caught and the ring never turns green. The first pending
    //    point is the close target; every placed point is a vertex to chain onto. Only the endpoint category applies (a half-drawn run has
    //    no solved centre / analytic midpoints yet). This is why "join the start + end of the curve" felt un-snappable — the target was live
    //    geometry the scan never saw.
    if (EndpointEnabled && Store.DrawingEnabled)
        for (const ImVec2& Pending : Store.PendingPoints)
            Consider(Pending, ParametricSketchSnapCategory::Endpoint);

    for (ParametricSketchShape& Shape : Store.Shapes)
    {
        if (!Shape.Displayed)
            continue;

        // Broad-phase: warm the cache, then reject any shape whose AABB (expanded by the catch radius) misses the cursor. A missed
        // shape can hold no snap point within tolerance — its defining points, centre, midpoints, and along-curve nearest all lie
        // inside the outline's own bounds, so the expanded box is a sound conservative reject that skips every far shape's scan.
        RetrieveCachedOutline(Shape);
        if (WorldCursor.x < Shape.CachedBoundaryMinimum.x - ToleranceMm ||
            WorldCursor.x > Shape.CachedBoundaryMaximum.x + ToleranceMm ||
            WorldCursor.y < Shape.CachedBoundaryMinimum.y - ToleranceMm ||
            WorldCursor.y > Shape.CachedBoundaryMaximum.y + ToleranceMm)
            continue;

        // Endpoint / vertex — the defining points verbatim (the strongest catch, so it wins ties against midpoints).
        if (EndpointEnabled)
        {
            for (const ImVec2& Point : Shape.Points)
                Consider(Point, ParametricSketchSnapCategory::Endpoint);

            // The VISIBLE curve tips — the first + last flattened outline point of an OPEN shape. For the free curves (Bezier /
            //    BSpline / Spline / Conic) the tip a user aims at to chain / close is the outline end, which need NOT coincide with a
            //    control point (a clamped B-spline's tip does; a Catmull-Rom's control points sit on the curve; but the outline tip is
            //    always the exact end regardless of family). Offering both tips explicitly makes "snap to the end of this curve" land
            //    precisely. A closed shape has no free tip (its ends meet), so it is skipped — its vertices already cover the joins.
            if (!Shape.ClosedEnabled && Shape.CachedOutline.size() >= 2)
            {
                Consider(Shape.CachedOutline.front(), ParametricSketchSnapCategory::Endpoint);
                Consider(Shape.CachedOutline.back(),  ParametricSketchSnapCategory::Endpoint);
            }
        }

        // Center — the round families' solved Centre.
        if (CenterEnabled)
            switch (Shape.Category)
            {
                case ParametricSketchShapeCategory::Arc:
                case ParametricSketchShapeCategory::Circle:
                case ParametricSketchShapeCategory::Ellipse:
                case ParametricSketchShapeCategory::Polygon:
                case ParametricSketchShapeCategory::Slot:
                    Consider(Shape.Centre, ParametricSketchSnapCategory::Center);
                    break;
                default:
                    break;
            }

        // Arc parametric points — the EXACT characteristic points on the analytic arc (t = 0 / 0.25 / 0.5 / 0.75 / 1), evaluated from
        // Centre + Radius + StartAngle + SweepAngle rather than read off the flattened chord midpoints. These are the registration
        // points a filleted corner exposes to the snapping engine (the endpoints are already caught as Endpoints above; the centre by
        // the Center case above). Grouped under the Midpoint mask so no new snap category / default-mask change is needed.
        if (MidpointEnabled && Shape.Category == ParametricSketchShapeCategory::Arc && Shape.Radius > 1e-4f)
        {
            const float ArcParameters[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
            for (float Parameter : ArcParameters)
            {
                const float Theta = Shape.StartAngle + Parameter * Shape.SweepAngle;
                Consider(ImVec2(Shape.Centre.x + std::cos(Theta) * Shape.Radius,
                                Shape.Centre.y + std::sin(Theta) * Shape.Radius),
                         ParametricSketchSnapCategory::Midpoint);
            }
        }

        // Midpoint + along-curve run over the flattened outline (the segment midpoints + the nearest point on any segment). Reads
        // the cache warmed by the broad-phase above — no re-tessellation.
        if (MidpointEnabled || AlongEnabled)
        {
            const std::vector<ImVec2>& Polyline = Shape.CachedOutline;
            const size_t Count = Polyline.size();
            for (size_t Index = 0; Index + 1 < Count; ++Index)
            {
                if (MidpointEnabled)
                    Consider(ImVec2((Polyline[Index].x + Polyline[Index + 1].x) * 0.5f,
                                    (Polyline[Index].y + Polyline[Index + 1].y) * 0.5f),
                             ParametricSketchSnapCategory::Midpoint);
                if (AlongEnabled)
                    Consider(NearestOnSegment(WorldCursor, Polyline[Index], Polyline[Index + 1]),
                             ParametricSketchSnapCategory::AlongCurve);
            }
            if (Shape.ClosedEnabled && Count >= 3)
            {
                if (MidpointEnabled)
                    Consider(ImVec2((Polyline[Count - 1].x + Polyline[0].x) * 0.5f,
                                    (Polyline[Count - 1].y + Polyline[0].y) * 0.5f),
                             ParametricSketchSnapCategory::Midpoint);
                if (AlongEnabled)
                    Consider(NearestOnSegment(WorldCursor, Polyline[Count - 1], Polyline[0]),
                             ParametricSketchSnapCategory::AlongCurve);
            }
        }
    }

    return Best;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      PRIMARY DIMENSION
//------------------------------------------------------------------------------------------------------------------------

float ResolvePrimaryDimension(const ParametricSketchShape& Shape, const char** PrimaryLabel)
{
    switch (Shape.Category)
    {
        case ParametricSketchShapeCategory::Circle:
        case ParametricSketchShapeCategory::Arc:
        case ParametricSketchShapeCategory::Polygon:
        case ParametricSketchShapeCategory::Slot:
            if (PrimaryLabel) *PrimaryLabel = "R";
            return Shape.Radius;
        case ParametricSketchShapeCategory::Ellipse:
            if (PrimaryLabel) *PrimaryLabel = "Major";
            return Shape.MajorAxis;
        default:
            if (PrimaryLabel) *PrimaryLabel = "Length";
            return EvaluateParametricSketchShapeLength(Shape);
    }
}

void EnforcePrimaryDimension(ParametricSketchShape& Shape, float TargetMillimetres)
{
    if (TargetMillimetres <= 0.0f)
        return;

    switch (Shape.Category)
    {
        case ParametricSketchShapeCategory::Circle:
        case ParametricSketchShapeCategory::Arc:
        case ParametricSketchShapeCategory::Polygon:
        {
            // Round families keyed on Radius: scale the radius; the flattening re-derives the outline from Centre + Radius. Keep the
            //    Points handle consistent (Circle / Polygon store [Centre, VertexHandle]) so a later re-solve reads the right radius.
            if (Shape.Radius <= 1e-6f)
                return;
            const float Ratio = TargetMillimetres / Shape.Radius;
            Shape.Radius = TargetMillimetres;
            if (Shape.Points.size() >= 2)
            {
                Shape.Points[1] = ImVec2(Shape.Centre.x + (Shape.Points[1].x - Shape.Centre.x) * Ratio,
                                         Shape.Centre.y + (Shape.Points[1].y - Shape.Centre.y) * Ratio);
            }
            break;
        }
        case ParametricSketchShapeCategory::Slot:
        {
            // The slot's primary dimension is its half-width Radius; the spine endpoints [A, B] stay put so only the width changes.
            Shape.Radius = TargetMillimetres;
            break;
        }
        case ParametricSketchShapeCategory::Ellipse:
        {
            // Major axis is primary; the minor tracks the major's existing ratio so the shape scales uniformly, not just its length.
            if (Shape.MajorAxis <= 1e-6f)
                return;
            const float Ratio = TargetMillimetres / Shape.MajorAxis;
            Shape.MajorAxis = TargetMillimetres;
            Shape.MinorAxis *= Ratio;
            if (Shape.Points.size() >= 2)
                Shape.Points[1] = ImVec2(Shape.Centre.x + (Shape.Points[1].x - Shape.Centre.x) * Ratio,
                                         Shape.Centre.y + (Shape.Points[1].y - Shape.Centre.y) * Ratio);
            break;
        }
        default:
        {
            // Straight + free-curve families: uniformly scale every defining point about the centroid to the requested outline length.
            const float Current = EvaluateParametricSketchShapeLength(Shape);
            if (Current <= 1e-6f || Shape.Points.size() < 2)
                return;
            const float Ratio = TargetMillimetres / Current;
            ImVec2 Centroid(0, 0);
            for (const ImVec2& Point : Shape.Points)
            {
                Centroid.x += Point.x;
                Centroid.y += Point.y;
            }
            Centroid.x /= (float)Shape.Points.size();
            Centroid.y /= (float)Shape.Points.size();
            for (ImVec2& Point : Shape.Points)
                Point = ImVec2(Centroid.x + (Point.x - Centroid.x) * Ratio,
                               Centroid.y + (Point.y - Centroid.y) * Ratio);
            break;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PER-PARAMETER ENFORCE
//------------------------------------------------------------------------------------------------------------------------

void EnforceShapeRadius(ParametricSketchShape& Shape, float RadiusMillimetres)
{
    if (RadiusMillimetres <= 0.0f)
        return;

    switch (Shape.Category)
    {
        case ParametricSketchShapeCategory::Circle:
        case ParametricSketchShapeCategory::Arc:
        case ParametricSketchShapeCategory::Polygon:
        {
            // Round families keyed on Radius: set the radius + drag the stored vertex handle so a later re-solve reads it back.
            const float Ratio = Shape.Radius > 1e-6f ? RadiusMillimetres / Shape.Radius : 0.0f;
            Shape.Radius = RadiusMillimetres;
            if (Ratio > 0.0f && Shape.Points.size() >= 2)
                Shape.Points[1] = ImVec2(Shape.Centre.x + (Shape.Points[1].x - Shape.Centre.x) * Ratio,
                                         Shape.Centre.y + (Shape.Points[1].y - Shape.Centre.y) * Ratio);
            break;
        }
        case ParametricSketchShapeCategory::Slot:
            // The slot's Radius is its half-width; the spine endpoints stay put so only the width follows.
            Shape.Radius = RadiusMillimetres;
            break;
        default:
            break;   // a non-round category has no radius to enforce
    }
}

void EnforceEllipseAxes(ParametricSketchShape& Shape, float MajorMillimetres, float MinorMillimetres)
{
    if (Shape.Category != ParametricSketchShapeCategory::Ellipse || MajorMillimetres <= 0.0f || MinorMillimetres <= 0.0f)
        return;

    // Scale the stored major-end handle by the major ratio so the rotation + centre re-solve identically; the minor is a scalar.
    const float Ratio = Shape.MajorAxis > 1e-6f ? MajorMillimetres / Shape.MajorAxis : 0.0f;
    Shape.MajorAxis = MajorMillimetres;
    Shape.MinorAxis = MinorMillimetres;
    if (Ratio > 0.0f && Shape.Points.size() >= 2)
        Shape.Points[1] = ImVec2(Shape.Centre.x + (Shape.Points[1].x - Shape.Centre.x) * Ratio,
                                 Shape.Centre.y + (Shape.Points[1].y - Shape.Centre.y) * Ratio);
}

void ResolveRectangleDimensions(const ParametricSketchShape& Shape, float& Width, float& Height)
{
    // A rectangle carries no width / height scalar — its two dimensions are the span of its four corner Points, read straight off
    //    the axis-aligned boundary. Zero for a non-rectangle or a shape with no corners so a caller reads a safe rest.
    Width  = 0.0f;
    Height = 0.0f;
    if (Shape.Category != ParametricSketchShapeCategory::Rectangle || Shape.Points.empty())
        return;

    float MinimumX = Shape.Points[0].x, MaximumX = Shape.Points[0].x;
    float MinimumY = Shape.Points[0].y, MaximumY = Shape.Points[0].y;
    for (const ImVec2& Corner : Shape.Points)
    {
        MinimumX = std::min(MinimumX, Corner.x);
        MaximumX = std::max(MaximumX, Corner.x);
        MinimumY = std::min(MinimumY, Corner.y);
        MaximumY = std::max(MaximumY, Corner.y);
    }
    Width  = MaximumX - MinimumX;
    Height = MaximumY - MinimumY;
}

void EnforceRectangleDimensions(ParametricSketchShape& Shape, float WidthMillimetres, float HeightMillimetres)
{
    if (Shape.Category != ParametricSketchShapeCategory::Rectangle || WidthMillimetres <= 0.0f || HeightMillimetres <= 0.0f ||
        Shape.Points.empty())
        return;

    // Rebuild the four corners centred on the CURRENT box centre at the requested span, so only the size changes — position holds.
    //    Re-solve through ConstructParametricSketchShape (two opposite corners) so the closed loop + cache invalidation match every draw path.
    float MinimumX = Shape.Points[0].x, MaximumX = Shape.Points[0].x;
    float MinimumY = Shape.Points[0].y, MaximumY = Shape.Points[0].y;
    for (const ImVec2& Corner : Shape.Points)
    {
        MinimumX = std::min(MinimumX, Corner.x);
        MaximumX = std::max(MaximumX, Corner.x);
        MinimumY = std::min(MinimumY, Corner.y);
        MaximumY = std::max(MaximumY, Corner.y);
    }
    const float CentreX   = (MinimumX + MaximumX) * 0.5f;
    const float CentreY   = (MinimumY + MaximumY) * 0.5f;
    const float HalfWidth  = WidthMillimetres  * 0.5f;
    const float HalfHeight = HeightMillimetres * 0.5f;

    const std::vector<ImVec2> Corners = { ImVec2(CentreX - HalfWidth, CentreY - HalfHeight),
                                          ImVec2(CentreX + HalfWidth, CentreY + HalfHeight) };
    ParametricSketchShape Solved     = ConstructParametricSketchShape(ParametricSketchShapeCategory::Rectangle, Corners);
    Solved.Identifier       = Shape.Identifier;
    std::snprintf(Solved.Title, sizeof(Solved.Title), "%s", Shape.Title);
    Solved.Displayed        = Shape.Displayed;
    Solved.FillEnabled      = Shape.FillEnabled;
    Solved.MatcapFillEnabled = Shape.MatcapFillEnabled;   // preserve the matcap-solid promotion across the rectangle bbox rebuild
    Solved.TintIndex        = Shape.TintIndex;
    Solved.FolderIdentifier = Shape.FolderIdentifier;
    Shape                   = Solved;   // whole-struct replace → the flatten cache self-invalidates
}

void EnforceShapeRotation(ParametricSketchShape& Shape, float RotationRadians)
{
    if (Shape.Category != ParametricSketchShapeCategory::Ellipse && Shape.Category != ParametricSketchShapeCategory::Polygon)
        return;

    Shape.Rotation = NormalizeAngle(RotationRadians);
    // Re-place the major-end / vertex handle at the new angle so a later re-solve reads the same rotation back from the points.
    if (Shape.Points.size() >= 2)
    {
        const float Reach = Shape.Category == ParametricSketchShapeCategory::Ellipse ? Shape.MajorAxis : Shape.Radius;
        Shape.Points[1] = ImVec2(Shape.Centre.x + std::cos(Shape.Rotation) * Reach,
                                 Shape.Centre.y + std::sin(Shape.Rotation) * Reach);
    }
}

void EnforcePolygonSideCount(ParametricSketchShape& Shape, int SideCount)
{
    if (Shape.Category != ParametricSketchShapeCategory::Polygon)
        return;
    // The outline re-flattens to the new vertex count about the same Centre + Radius + Rotation; nothing else changes.
    Shape.SideCount = std::max(3, SideCount);
}

void EnforceConicRho(ParametricSketchShape& Shape, float Rho)
{
    if (Shape.Category != ParametricSketchShapeCategory::Conic)
        return;
    // Clamp away from the degenerate 0 / 1 ends so the rational-quadratic stays a finite conic.
    Shape.Rho = std::min(0.98f, std::max(0.02f, Rho));
}

void EnforceCurveDegree(ParametricSketchShape& Shape, int Degree)
{
    if (Shape.Category != ParametricSketchShapeCategory::BSpline && Shape.Category != ParametricSketchShapeCategory::Nurbs)
        return;
    // A degree past the control-point count collapses the basis; clamp to [1, count - 1] so the curve still evaluates.
    const int Ceiling = std::max(1, (int)Shape.Points.size() - 1);
    Shape.Degree = std::min(Ceiling, std::max(1, Degree));
}

void EnforceShapePoint(ParametricSketchShape& Shape, int Index, ImVec2 WorldMillimetres)
{
    if (Index < 0 || Index >= (int)Shape.Points.size())
        return;

    Shape.Points[Index] = WorldMillimetres;

    // The round families derive their scalars from the defining points; re-solve the affected ones so Centre / Radius stay exact.
    switch (Shape.Category)
    {
        case ParametricSketchShapeCategory::Circle:
        case ParametricSketchShapeCategory::Polygon:
        {
            if (Shape.Points.size() >= 2)
            {
                Shape.Centre = Shape.Points[0];
                const ImVec2 Handle = Shape.Points[1];
                Shape.Radius = std::sqrt((Handle.x - Shape.Centre.x) * (Handle.x - Shape.Centre.x) +
                                         (Handle.y - Shape.Centre.y) * (Handle.y - Shape.Centre.y));
                if (Shape.Category == ParametricSketchShapeCategory::Polygon)
                    Shape.Rotation = std::atan2(Handle.y - Shape.Centre.y, Handle.x - Shape.Centre.x);
            }
            break;
        }
        case ParametricSketchShapeCategory::Slot:
        {
            // The two points are the spine endpoints A, B; the half-width Radius is independent, so only the centre re-derives.
            if (Shape.Points.size() >= 2)
                Shape.Centre = ImVec2((Shape.Points[0].x + Shape.Points[1].x) * 0.5f,
                                      (Shape.Points[0].y + Shape.Points[1].y) * 0.5f);
            break;
        }
        default:
            break;   // Line / Polyline / Rectangle / free-curve families carry the moved point verbatim
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       EDIT LOG + FOLDERS
//------------------------------------------------------------------------------------------------------------------------

void CaptureParametricSketchRevision(const ParametricSketchShapeStore& Store, ParametricSketchRevision& OutRevision)
{
    // Copy every persisted authored field; the flatten caches on each shape ride along but carry CachedOutlineValid, which a restore
    //    forces false so the next draw re-flattens. The edit log itself is deliberately NOT captured (that would recurse per entry).
    OutRevision.Shapes                    = Store.Shapes;
    OutRevision.Inscriptions              = Store.Inscriptions;
    OutRevision.Datums                    = Store.Datums;
    OutRevision.LoftBodies                = Store.LoftBodies;
    OutRevision.Constraints               = Store.Constraints;
    OutRevision.Dimensions                = Store.Dimensions;
    OutRevision.Folders                   = Store.Folders;
    OutRevision.NextIdentifier            = Store.NextIdentifier;
    OutRevision.NextInscriptionIdentifier = Store.NextInscriptionIdentifier;
    OutRevision.NextDatumIdentifier       = Store.NextDatumIdentifier;
    OutRevision.NextLoftIdentifier        = Store.NextLoftIdentifier;
    OutRevision.NextFolderIdentifier      = Store.NextFolderIdentifier;
    OutRevision.NextConstraintIdentifier  = Store.NextConstraintIdentifier;
    OutRevision.NextDimensionIdentifier   = Store.NextDimensionIdentifier;
    OutRevision.Selected                  = Store.Selected;
    OutRevision.SelectedInscription       = Store.SelectedInscription;
    OutRevision.SelectedDatum             = Store.SelectedDatum;
    OutRevision.SelectedLoft              = Store.SelectedLoft;
}

// 📝 Live-mirror reflow — defined in ParametricSketchTransform.cpp (which owns the reflection math). Forward-declared here (ParametricSketchShapeStore.h must
//    not include ParametricSketchTransform.h — the dependency runs the other way) so every committed edit refreshes the driven mirror children BEFORE
//    the history snapshot, keeping parent + copies coherent across undo / redo.
void ReflowMirrorChildren(ParametricSketchShapeStore& Store);

// 📝 Live-array reflow — likewise defined in ParametricSketchTransform.cpp. Forward-declared here so a committed edit refreshes the driven ARRAY copies
//    (linear / radial / instance-on-points) right after the mirror copies, before the snapshot, keeping source + copies coherent across undo/redo.
void ReflowArrayChildren(ParametricSketchShapeStore& Store);

// 📝 Live-loft reflow — defined in ParametricSketchLoft.cpp. Forward-declared here so a committed edit re-solves every lofted body from its live source
//    sections (after the mirror / array copies, before the snapshot), keeping each ParametricSketchLoftBody coherent with its profiles across undo/redo.
void ReflowLoftBodies(ParametricSketchShapeStore& Store);

void AppendParametricSketchEditDetailed(ParametricSketchShapeStore& Store,
                               uint32_t           ShapeId,
                               const char*        Label,
                               const char*        Glyph,
                               const char*        Detail)
{
    // Refresh every driven mirror copy from its live source BEFORE snapshotting, so a source edit / datum move propagates to the copies and
    //    the captured revision holds a coherent parent + children pair (undo / redo restore both together). No-op when nothing is mirrored.
    ReflowMirrorChildren(Store);
    ReflowArrayChildren(Store);
    ReflowLoftBodies(Store);

    // Discard any redoable tail first so a fresh edit after an undo forks cleanly, then append at the cursor + advance it.
    if (Store.LogCursor < (int)Store.EditLog.size())
        Store.EditLog.erase(Store.EditLog.begin() + Store.LogCursor, Store.EditLog.end());

    ParametricSketchHistoryEntry Entry;
    Entry.ShapeId    = ShapeId;
    Entry.FlashLevel = 1.0f;
    if (Label)
        std::snprintf(Entry.Label, sizeof(Entry.Label), "%s", Label);
    if (Glyph)
        std::snprintf(Entry.Glyph, sizeof(Entry.Glyph), "%s", Glyph);
    if (Detail)
        std::snprintf(Entry.Detail, sizeof(Entry.Detail), "%s", Detail);

    // Snapshot the store's authored state AFTER this edit so undo / redo / jump can restore this exact step in O(1).
    CaptureParametricSketchRevision(Store, Entry.Revision);

    Store.EditLog.push_back(Entry);
    Store.LogCursor = (int)Store.EditLog.size();
}

void AppendParametricSketchEdit(ParametricSketchShapeStore& Store, uint32_t ShapeId, const char* Label, const char* Glyph)
{
    AppendParametricSketchEditDetailed(Store, ShapeId, Label, Glyph, nullptr);
}

void RestoreParametricSketchRevisionAt(ParametricSketchShapeStore& Store, int Step)
{
    // Step counts APPLIED entries: 0 = the empty sketch before the first edit, N = after entry[N-1]. Clamp + no-op when unchanged.
    const int Count = (int)Store.EditLog.size();
    if (Step < 0)
        Step = 0;
    if (Step > Count)
        Step = Count;
    if (Step == Store.LogCursor)
        return;

    if (Step == 0)
    {
        // Restore the empty sketch (the state before any edit), keeping the id sources at their defaults.
        Store.Shapes.clear();
        Store.Inscriptions.clear();
        Store.Datums.clear();
        Store.LoftBodies.clear();
        Store.Constraints.clear();
        Store.Dimensions.clear();
        Store.Folders.clear();
        Store.NextIdentifier            = 1;
        Store.NextInscriptionIdentifier = 1;
        Store.NextDatumIdentifier       = 1;
        Store.NextLoftIdentifier        = 1;
        Store.NextFolderIdentifier      = 1;
        Store.NextConstraintIdentifier  = 1;
        Store.NextDimensionIdentifier   = 1;
        Store.Selected            = 0;
        Store.SelectedInscription = 0;
        Store.SelectedDatum       = 0;
        Store.SelectedLoft        = 0;
    }
    else
    {
        const ParametricSketchRevision& Revision = Store.EditLog[Step - 1].Revision;
        Store.Shapes                    = Revision.Shapes;
        Store.Inscriptions              = Revision.Inscriptions;
        Store.Datums                    = Revision.Datums;
        Store.LoftBodies                = Revision.LoftBodies;
        Store.Constraints               = Revision.Constraints;
        Store.Dimensions                = Revision.Dimensions;
        Store.Folders                   = Revision.Folders;
        Store.NextIdentifier            = Revision.NextIdentifier;
        Store.NextInscriptionIdentifier = Revision.NextInscriptionIdentifier;
        Store.NextDatumIdentifier       = Revision.NextDatumIdentifier;
        Store.NextLoftIdentifier        = Revision.NextLoftIdentifier;
        Store.NextFolderIdentifier      = Revision.NextFolderIdentifier;
        Store.NextConstraintIdentifier  = Revision.NextConstraintIdentifier;
        Store.NextDimensionIdentifier   = Revision.NextDimensionIdentifier;
        Store.Selected                  = Revision.Selected;
        Store.SelectedInscription       = Revision.SelectedInscription;
        Store.SelectedDatum             = Revision.SelectedDatum;
        Store.SelectedLoft              = Revision.SelectedLoft;
    }

    // Force every restored shape's flatten cache to rebuild on the next draw (the snapshot copy may carry a stale valid flag).
    for (ParametricSketchShape& Shape : Store.Shapes)
        Shape.CachedOutlineValid = false;

    // Re-seat the selection on a surviving shape (drop it when the snapshot's Selected no longer exists), and clear the multi-set +
    //    hover so a restore never leaves a dangling boolean base or a hover on a vanished shape.
    if (Store.Selected != 0 && !ResolveParametricSketchShape(Store, Store.Selected))
        Store.Selected = 0;
    Store.SelectionSet.clear();
    if (Store.Selected != 0)
        Store.SelectionSet.push_back(Store.Selected);
    Store.Hovered = 0;

    // Same re-seat for inscriptions: drop a selection the snapshot no longer contains, and clear the transient hover.
    if (Store.SelectedInscription != 0 && !ResolveInscription(Store, Store.SelectedInscription))
        Store.SelectedInscription = 0;
    Store.HoveredInscription = 0;

    // Same re-seat for datums: drop a selection the snapshot no longer contains, and clear the transient hover.
    if (Store.SelectedDatum != 0 && !ResolveDatum(Store, Store.SelectedDatum))
        Store.SelectedDatum = 0;
    Store.HoveredDatum = 0;

    // Same re-seat for lofts: drop a selection the snapshot no longer contains (loft bodies have no hover state this pass).
    if (Store.SelectedLoft != 0)
    {
        bool LoftSurvives = false;
        for (const ParametricSketchLoftBody& Body : Store.LoftBodies)
            if (Body.Identifier == Store.SelectedLoft)
            {
                LoftSurvives = true;
                break;
            }
        if (!LoftSurvives)
            Store.SelectedLoft = 0;
    }

    // Drop the transient per-shape datum-move pick when its owning shape no longer exists or no longer carries an enabled datum.
    if (Store.DatumSelected != 0)
    {
        ParametricSketchShape* DatumOwner = ResolveParametricSketchShape(Store, Store.DatumSelected);
        if (!DatumOwner || !DatumOwner->DatumEnabled)
            Store.DatumSelected = 0;
    }

    Store.LogCursor = Step;
}

bool UndoParametricSketchEdit(ParametricSketchShapeStore& Store)
{
    if (Store.LogCursor <= 0)
        return false;
    RestoreParametricSketchRevisionAt(Store, Store.LogCursor - 1);
    return true;
}

bool RedoParametricSketchEdit(ParametricSketchShapeStore& Store)
{
    if (Store.LogCursor >= (int)Store.EditLog.size())
        return false;
    RestoreParametricSketchRevisionAt(Store, Store.LogCursor + 1);
    return true;
}

uint32_t AttachParametricSketchFolder(ParametricSketchShapeStore& Store, const char* Title)
{
    ParametricSketchFolder Folder;
    Folder.Identifier = Store.NextFolderIdentifier++;
    if (Title && Title[0])
        std::snprintf(Folder.Title, sizeof(Folder.Title), "%s", Title);
    else
        std::snprintf(Folder.Title, sizeof(Folder.Title), "Folder %u", Folder.Identifier);
    Store.Folders.push_back(Folder);

    char LogLabel[48];
    std::snprintf(LogLabel, sizeof(LogLabel), "Added %s", Folder.Title);
    AppendParametricSketchEdit(Store, 0, LogLabel, "folder-plus");
    return Folder.Identifier;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        SHAPE OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

uint32_t DuplicateParametricSketchShape(ParametricSketchShapeStore& Store, uint32_t Identifier, ImVec2 OffsetMm)
{
    ParametricSketchShape* Source = ResolveParametricSketchShape(Store, Identifier);
    if (!Source)
        return 0;

    // Nudge every defining point by the offset, then re-solve from those points so the clone's scalars + cache rebuild cleanly.
    std::vector<ImVec2> Points = Source->Points;
    for (ImVec2& Point : Points)
        Point = ImVec2(Point.x + OffsetMm.x, Point.y + OffsetMm.y);

    const ParametricSketchShapeCategory Category = Source->Category;
    const int   SideCount = Source->SideCount;
    const float Rho       = Source->Rho;
    const int   Degree    = Source->Degree;

    ParametricSketchShape Fresh = ConstructParametricSketchShape(Category, Points, SideCount, Rho, Degree);
    Fresh.Identifier    = Store.NextIdentifier++;
    Fresh.TintIndex     = Fresh.Identifier;
    Fresh.FolderIdentifier = Source->FolderIdentifier;   // 📝 land the copy beside its origin
    std::snprintf(Fresh.Title, sizeof(Fresh.Title), "%s %u", ShapeCategoryLabel(Category), Fresh.Identifier);

    Store.Shapes.push_back(Fresh);
    Store.Selected = Fresh.Identifier;
    Store.SelectionSet.assign(1, Fresh.Identifier);

    char LogLabel[48];
    std::snprintf(LogLabel, sizeof(LogLabel), "Duplicated %s", Fresh.Title);
    AppendParametricSketchEdit(Store, Fresh.Identifier, LogLabel, "copy");

    return Fresh.Identifier;
}

ImVec2 ResolveParametricSketchShapeCentroid(ParametricSketchShape& Shape)
{
    // Warm the flatten cache so CachedBoundaryMinimum / Maximum reflect the current definition, then take the AABB midpoint — a uniform
    //    centre across every category (straight / round / curve). The round families' solved Centre is an alternative, but the AABB midpoint
    //    needs no per-category branch and is already computed for pick / Fit.
    RetrieveCachedOutline(Shape);
    return ImVec2((Shape.CachedBoundaryMinimum.x + Shape.CachedBoundaryMaximum.x) * 0.5f,
                  (Shape.CachedBoundaryMinimum.y + Shape.CachedBoundaryMaximum.y) * 0.5f);
}

void DetachParametricSketchShape(ParametricSketchShapeStore& Store, uint32_t Identifier)
{
    if (Identifier == 0)
        return;

    // Locate the shape + keep its title for the log before the erase invalidates the pointer.
    char DetachedTitle[48] = {};
    bool Located = false;
    for (const ParametricSketchShape& Entry : Store.Shapes)
        if (Entry.Identifier == Identifier)
        {
            std::snprintf(DetachedTitle, sizeof(DetachedTitle), "%s", Entry.Title);
            Located = true;
            break;
        }
    if (!Located)
        return;

    // Drop any constraint / dimension that references the detached shape (a dangling handle would crash the solver).
    for (size_t Index = Store.Constraints.size(); Index-- > 0; )
    {
        const ParametricSketchConstraint& Constraint = Store.Constraints[Index];
        if (Constraint.PrimaryA.ShapeIdentifier   == Identifier ||
            Constraint.PrimaryB.ShapeIdentifier   == Identifier ||
            Constraint.SecondaryA.ShapeIdentifier == Identifier ||
            Constraint.SecondaryB.ShapeIdentifier == Identifier)
            Store.Constraints.erase(Store.Constraints.begin() + Index);
    }
    for (size_t Index = Store.Dimensions.size(); Index-- > 0; )
    {
        const ParametricSketchDimension& Dimension = Store.Dimensions[Index];
        if (Dimension.PrimaryA.ShapeIdentifier   == Identifier ||
            Dimension.PrimaryB.ShapeIdentifier   == Identifier ||
            Dimension.SecondaryA.ShapeIdentifier == Identifier ||
            Dimension.SecondaryB.ShapeIdentifier == Identifier)
            Store.Dimensions.erase(Store.Dimensions.begin() + Index);
    }

    // Erase the shape, then clear it from selection / hover.
    for (size_t Index = 0; Index < Store.Shapes.size(); ++Index)
        if (Store.Shapes[Index].Identifier == Identifier)
        {
            Store.Shapes.erase(Store.Shapes.begin() + Index);
            break;
        }
    for (size_t Index = Store.SelectionSet.size(); Index-- > 0; )
        if (Store.SelectionSet[Index] == Identifier)
            Store.SelectionSet.erase(Store.SelectionSet.begin() + Index);
    Store.Selected = Store.SelectionSet.empty() ? 0u : Store.SelectionSet.back();
    if (Store.Hovered == Identifier)
        Store.Hovered = 0;

    char LogLabel[48];
    std::snprintf(LogLabel, sizeof(LogLabel), "Deleted %s", DetachedTitle);
    AppendParametricSketchEdit(Store, 0, LogLabel, "trash-2");
}

// 📝 The Trim / Cut engine (Plasticity-faithful). A curve is partitioned into SECTIONS by its INTERSECTIONS with every other displayed
//    curve, plus (for a straight / compound run) its own defining vertices, plus the two endpoints of an open run. Trim deletes the ONE
//    section under the cursor; Cut splits the curve at every intersection into separate pieces. Both run on the FLATTENED outline (the
//    same display polyline the view paints), so they work for every family — Line / Polyline / Arc / Bezier / Circle / Profile alike —
//    not only the straight ones. All maths in world mm; a section is a span of arc length along the outline.
namespace
{
    // 📝 Point-to-segment squared distance — projects Point onto [A,B], clamps the parameter to [0,1], returns the squared gap (the
    //    inner loop compares magnitudes, so the sqrt is skipped). Reused by the cursor → nearest-outline-point scan.
    // 📝 Squared distance between two world-mm points (the endpoint-weld test the Join concatenation uses to chain runs + close a loop).
    float PointGapSquared(ImVec2 A, ImVec2 B)
    {
        const ImVec2 Delta(B.x - A.x, B.y - A.y);
        return Delta.x * Delta.x + Delta.y * Delta.y;
    }

    float SegmentGapSquared(ImVec2 Point, ImVec2 A, ImVec2 B)
    {
        const ImVec2 AB(B.x - A.x, B.y - A.y);
        const ImVec2 AP(Point.x - A.x, Point.y - A.y);
        const float  LengthSquared = AB.x * AB.x + AB.y * AB.y;
        float        Parameter     = 0.0f;
        if (LengthSquared > 1e-12f)
            Parameter = (AP.x * AB.x + AP.y * AB.y) / LengthSquared;
        if (Parameter < 0.0f) Parameter = 0.0f;
        if (Parameter > 1.0f) Parameter = 1.0f;
        const ImVec2 Closest(A.x + AB.x * Parameter, A.y + AB.y * Parameter);
        const ImVec2 Gap(Point.x - Closest.x, Point.y - Closest.y);
        return Gap.x * Gap.x + Gap.y * Gap.y;
    }

    // 📝 Segment ↔ segment crossing. Fills ParameterA (the fraction along A0→A1 where the crossing lands) + the crossing point, and
    //    returns true when the two finite segments genuinely cross. Parallel / collinear pairs return false (an overlap is not a point
    //    crossing, so it forms no section boundary). A small epsilon lets a touch at an endpoint still register.
    bool ResolveSegmentCrossing(ImVec2 A0, ImVec2 A1, ImVec2 B0, ImVec2 B1, float& ParameterA, ImVec2& Crossing)
    {
        const ImVec2 R(A1.x - A0.x, A1.y - A0.y);
        const ImVec2 S(B1.x - B0.x, B1.y - B0.y);
        const float  Denominator = R.x * S.y - R.y * S.x;
        if (std::fabs(Denominator) < 1e-12f)
            return false;   // parallel / collinear — no single crossing point
        const ImVec2 Delta(B0.x - A0.x, B0.y - A0.y);
        const float  ParamA = (Delta.x * S.y - Delta.y * S.x) / Denominator;
        const float  ParamB = (Delta.x * R.y - Delta.y * R.x) / Denominator;
        if (ParamA < -1e-4f || ParamA > 1.0f + 1e-4f || ParamB < -1e-4f || ParamB > 1.0f + 1e-4f)
            return false;   // the crossing lies off one of the finite segments
        ParameterA = ParamA < 0.0f ? 0.0f : (ParamA > 1.0f ? 1.0f : ParamA);
        Crossing   = ImVec2(A0.x + R.x * ParameterA, A0.y + R.y * ParameterA);
        return true;
    }

    // 📝 Flatten a shape into a clean working polyline (its display tessellation) + report whether the run is a closed loop. A closed
    //    family whose flatten duplicates the first point as the last has that duplicate dropped, so the closing edge is the implicit
    //    (last → first) wrap and every segment index reads uniformly. Mutates the cache (RetrieveCachedOutline), hence the non-const ref.
    void BuildWorkingPolyline(ParametricSketchShape& Shape, std::vector<ImVec2>& Out, bool& Closed)
    {
        Out    = RetrieveCachedOutline(Shape);
        Closed = Shape.ClosedEnabled;
        if (Closed && Out.size() >= 2)
        {
            const ImVec2& First = Out.front();
            const ImVec2& Last  = Out.back();
            if (std::fabs(First.x - Last.x) < 1e-5f && std::fabs(First.y - Last.y) < 1e-5f)
                Out.pop_back();
        }
    }

    // 📝 Per-vertex forward arc length along the working polyline (cumulative distance from vertex 0). Fills CumulativeLength (size N)
    //    and returns the TOTAL length — for an open run the last vertex's cumulative distance; for a closed loop that plus the closing
    //    edge (last → first). The Trim / Cut sections are spans of this arc-length parameter.
    float ResolveArcLengths(const std::vector<ImVec2>& Points, bool Closed, std::vector<float>& CumulativeLength)
    {
        const int Count = (int)Points.size();
        CumulativeLength.assign(Count, 0.0f);
        for (int Index = 1; Index < Count; ++Index)
        {
            const ImVec2 Delta(Points[Index].x - Points[Index - 1].x, Points[Index].y - Points[Index - 1].y);
            CumulativeLength[Index] = CumulativeLength[Index - 1] + std::sqrt(Delta.x * Delta.x + Delta.y * Delta.y);
        }
        float Total = Count > 0 ? CumulativeLength[Count - 1] : 0.0f;
        if (Closed && Count >= 2)
        {
            const ImVec2 Delta(Points[0].x - Points[Count - 1].x, Points[0].y - Points[Count - 1].y);
            Total += std::sqrt(Delta.x * Delta.x + Delta.y * Delta.y);
        }
        return Total;
    }

    // 📝 The world-mm point at arc length S along the working polyline (S wraps modulo Total for a closed loop). Walks to the segment
    //    that contains S, then lerps within it. Used to place the exact partial endpoints of a surviving span at a cut point.
    ImVec2 PointAtArcLength(const std::vector<ImVec2>& Points,
                            bool                       Closed,
                            const std::vector<float>&  CumulativeLength,
                            float                      Total,
                            float                      S)
    {
        const int Count = (int)Points.size();
        if (Count == 0) return ImVec2(0, 0);
        if (Count == 1) return Points[0];
        if (Closed && Total > 1e-6f)
        {
            S = std::fmod(S, Total);
            if (S < 0.0f) S += Total;
        }
        const int SegmentCount = Closed ? Count : Count - 1;
        for (int Segment = 0; Segment < SegmentCount; ++Segment)
        {
            const int   NextIndex   = (Segment + 1) % Count;
            const float SegmentStart = CumulativeLength[Segment];
            const float SegmentEnd   = (Segment + 1 < Count) ? CumulativeLength[NextIndex] : Total;
            const float SegmentSpan  = SegmentEnd - SegmentStart;
            if (S <= SegmentEnd + 1e-4f || Segment == SegmentCount - 1)
            {
                const float Fraction = SegmentSpan > 1e-6f ? (S - SegmentStart) / SegmentSpan : 0.0f;
                const float Clamped  = Fraction < 0.0f ? 0.0f : (Fraction > 1.0f ? 1.0f : Fraction);
                return ImVec2(Points[Segment].x + (Points[NextIndex].x - Points[Segment].x) * Clamped,
                              Points[Segment].y + (Points[NextIndex].y - Points[Segment].y) * Clamped);
            }
        }
        return Points[Count - 1];
    }

    // 📝 Extract the surviving vertex run for the span (SpanStart → SpanEnd) of arc length: the exact partial start point, every whole
    //    vertex strictly inside the span (walked forward, wrapping once for a closed loop where SpanEnd may exceed Total), then the exact
    //    partial end point. The result is an OPEN polyline following the original tessellation between the two cut points.
    std::vector<ImVec2> ExtractArcSpan(const std::vector<ImVec2>& Points,
                                       bool                       Closed,
                                       const std::vector<float>&  CumulativeLength,
                                       float                      Total,
                                       float                      SpanStart,
                                       float                      SpanEnd)
    {
        std::vector<ImVec2> Run;
        const int Count = (int)Points.size();
        Run.push_back(PointAtArcLength(Points, Closed, CumulativeLength, Total, SpanStart));
        const int Steps = Closed ? Count * 2 : Count;
        for (int Step = 0; Step < Steps; ++Step)
        {
            const int   Index = Step % Count;
            const float Arc   = CumulativeLength[Index] + (Step >= Count ? Total : 0.0f);
            if (Arc > SpanStart + 1e-3f && Arc < SpanEnd - 1e-3f)
                Run.push_back(Points[Index]);
        }
        Run.push_back(PointAtArcLength(Points, Closed, CumulativeLength, Total, SpanEnd));
        return Run;
    }

    // 📝 Collect the arc-length positions where the working polyline W crosses EVERY other displayed, unlocked shape's outline. Each
    //    crossing becomes a section boundary; deduped near-equal hits are merged by the caller. Skips the target itself + hidden / locked
    //    shapes. Runs on flattened outlines, so a line crossing a circle registers both crossings, a line crossing a spline registers
    //    each, and so on — the faithful Plasticity behaviour my earlier vertex-only pass missed.
    void CollectIntersectionParameters(ParametricSketchShapeStore&         Store,
                                       uint32_t                   TargetIdentifier,
                                       const std::vector<ImVec2>& W,
                                       bool                       WClosed,
                                       const std::vector<float>&  CumulativeLength,
                                       float                      Total,
                                       std::vector<float>&        Breaks)
    {
        const int Count = (int)W.size();
        const int WSegments = WClosed ? Count : Count - 1;
        for (ParametricSketchShape& Other : Store.Shapes)
        {
            if (Other.Identifier == TargetIdentifier || !Other.Displayed || Other.LockEnabled)
                continue;
            std::vector<ImVec2> OtherPoints;
            bool                OtherClosed = false;
            BuildWorkingPolyline(Other, OtherPoints, OtherClosed);
            const int OtherCount = (int)OtherPoints.size();
            if (OtherCount < 2)
                continue;
            const int OtherSegments = OtherClosed ? OtherCount : OtherCount - 1;
            for (int Segment = 0; Segment < WSegments; ++Segment)
            {
                const ImVec2 A0 = W[Segment];
                const ImVec2 A1 = W[(Segment + 1) % Count];
                const float  SegmentStart = CumulativeLength[Segment];
                const float  SegmentEnd   = (Segment + 1 < Count) ? CumulativeLength[(Segment + 1) % Count] : Total;
                const float  SegmentSpan  = SegmentEnd - SegmentStart;
                for (int OtherSegment = 0; OtherSegment < OtherSegments; ++OtherSegment)
                {
                    const ImVec2 B0 = OtherPoints[OtherSegment];
                    const ImVec2 B1 = OtherPoints[(OtherSegment + 1) % OtherCount];
                    float  ParameterA = 0.0f;
                    ImVec2 Crossing;
                    if (ResolveSegmentCrossing(A0, A1, B0, B1, ParameterA, Crossing))
                        Breaks.push_back(SegmentStart + ParameterA * SegmentSpan);
                }
            }
        }
    }

    // 📝 Sort + merge arc-length break positions that are within a small tolerance of each other (two curves crossing at nearly the same
    //    place, or an intersection coinciding with a vertex) so a section is never a zero-width sliver.
    void SortAndMergeBreaks(std::vector<float>& Breaks, float Total)
    {
        std::sort(Breaks.begin(), Breaks.end());
        const float Merge = std::max(1e-3f, Total * 1e-4f);
        std::vector<float> Merged;
        Merged.reserve(Breaks.size());
        for (float Value : Breaks)
            if (Merged.empty() || Value - Merged.back() > Merge)
                Merged.push_back(Value);
        Breaks.swap(Merged);
    }

    // 📝 The nearest arc-length position on the working polyline to a world point (which section the cursor sits in). Scans each segment
    //    for the closest projected point + returns its cumulative arc length.
    float ResolveNearestArcLength(const std::vector<ImVec2>& Points,
                                  bool                       Closed,
                                  const std::vector<float>&  CumulativeLength,
                                  float                      Total,
                                  ImVec2                     Cursor,
                                  float&                     NearestGapSquared)
    {
        const int Count = (int)Points.size();
        const int SegmentCount = Closed ? Count : Count - 1;
        float BestArc = 0.0f;
        NearestGapSquared = 1e30f;
        for (int Segment = 0; Segment < SegmentCount; ++Segment)
        {
            const int   NextIndex = (Segment + 1) % Count;
            const ImVec2 A = Points[Segment];
            const ImVec2 B = Points[NextIndex];
            const ImVec2 AB(B.x - A.x, B.y - A.y);
            const ImVec2 AP(Cursor.x - A.x, Cursor.y - A.y);
            const float  LengthSquared = AB.x * AB.x + AB.y * AB.y;
            float        Parameter = LengthSquared > 1e-12f ? (AP.x * AB.x + AP.y * AB.y) / LengthSquared : 0.0f;
            if (Parameter < 0.0f) Parameter = 0.0f;
            if (Parameter > 1.0f) Parameter = 1.0f;
            const ImVec2 Closest(A.x + AB.x * Parameter, A.y + AB.y * Parameter);
            const ImVec2 Gap(Cursor.x - Closest.x, Cursor.y - Closest.y);
            const float  GapSquared = Gap.x * Gap.x + Gap.y * Gap.y;
            if (GapSquared < NearestGapSquared)
            {
                NearestGapSquared = GapSquared;
                const float SegmentStart = CumulativeLength[Segment];
                const float SegmentEnd   = (Segment + 1 < Count) ? CumulativeLength[NextIndex] : Total;
                BestArc = SegmentStart + (SegmentEnd - SegmentStart) * Parameter;
            }
        }
        return BestArc;
    }

    // 📝 Whether a family carries genuine per-vertex corners (its defining Points ARE the outline). Only these contribute their interior
    //    vertices as Trim section boundaries; a smooth curve's flatten samples are NOT boundaries (they are tessellation, not corners).
    bool StraightRunFamily(ParametricSketchShapeCategory Category)
    {
        return Category == ParametricSketchShapeCategory::Line ||
               Category == ParametricSketchShapeCategory::Polyline ||
               Category == ParametricSketchShapeCategory::Rectangle ||
               Category == ParametricSketchShapeCategory::Profile;
    }

    // 📝 The ANALYTIC section boundaries of a straight-run shape's outline W — one arc-length position per real corner, treating a
    //    filleted / chamfered corner as ONE indivisible span bounded by its two tangent points (NEVER a break at every interior arc dot).
    //    This is what makes Trim / Cut / Remove operate on the whole fillet arc as one entity: the outline still stores the subdivided
    //    samples, but the sections the tools carve at land only on real endpoints. For each defining corner the flatten either emitted the
    //    raw vertex (one outline sample → one break) or an arc / setback run (a contiguous outline sub-run → a break at its FIRST and LAST
    //    sample, with the interior samples deliberately skipped). We rebuild that same correspondence by matching each defining corner's
    //    world point to its nearest outline sample: a raw corner sits exactly on one sample; a filleted corner's nearest sample is the
    //    arc's near-tangent, and we widen the span to cover the whole contiguous run whose samples all lie within the fillet magnitude of
    //    the corner. Fills Breaks with the arc-length of every such boundary; the caller merges + sorts them with the endpoint / crossing
    //    breaks. A corner with no fillet contributes its single arc-length; a filleted corner contributes its two tangent arc-lengths.
    void CollectAnalyticCornerBreaks(const ParametricSketchShape&        Shape,
                                     const std::vector<ImVec2>& W,
                                     bool                       Closed,
                                     const std::vector<float>&  CumulativeLength,
                                     std::vector<float>&        Breaks)
    {
        const int OutlineCount = (int)W.size();
        const int CornerCount  = (int)Shape.Points.size();
        if (OutlineCount < 2 || CornerCount < 2)
            return;

        // Per defining corner: its fillet magnitude (0 = raw corner) so we know how wide its arc run may reach around it.
        for (int Corner = 0; Corner < CornerCount; ++Corner)
        {
            // An open run's two endpoints never round (one leg only) — they are always plain break corners.
            const bool Endpoint = !Closed && (Corner == 0 || Corner == CornerCount - 1);
            float FilletMagnitude = 0.0f;
            if (!Endpoint)
                for (const ParametricSketchCornerFillet& Edit : Shape.CornerFillets)
                    if (Edit.CornerIndex == Corner && Edit.Magnitude > 0.0f)
                        FilletMagnitude = Edit.Magnitude;

            const ImVec2 CornerWorld = Shape.Points[Corner];

            // Nearest outline sample to this corner's world point.
            int   Nearest = 0;
            float Best    = 1e30f;
            for (int Index = 0; Index < OutlineCount; ++Index)
            {
                const float Distance = (W[Index].x - CornerWorld.x) * (W[Index].x - CornerWorld.x) +
                                       (W[Index].y - CornerWorld.y) * (W[Index].y - CornerWorld.y);
                if (Distance < Best) { Best = Distance; Nearest = Index; }
            }

            if (FilletMagnitude <= 0.0f)
            {
                // Raw corner: it sits on exactly one outline sample → one break there.
                Breaks.push_back(CumulativeLength[Nearest]);
                continue;
            }

            // Filleted / chamfered corner: the arc run is the contiguous span of outline samples within the fillet reach of the corner.
            //    Widen left + right from the nearest sample while samples stay inside (Magnitude · 1.5)^2 of the corner (a generous bound —
            //    a fillet's farthest tangent sits at the setback distance ≤ shorter leg, always < the leg, so 1.5× the magnitude clears the
            //    whole run without leaking into the neighbouring straight edge). The run's first + last samples are the two real endpoints.
            const float Reach = FilletMagnitude * 1.5f;
            const float ReachSquared = Reach * Reach;
            int Low  = Nearest;
            int High = Nearest;
            for (int Guard = 0; Guard < OutlineCount; ++Guard)
            {
                const int Prev = (Low - 1 + OutlineCount) % OutlineCount;
                if (!Closed && Low == 0) break;
                const float Distance = (W[Prev].x - CornerWorld.x) * (W[Prev].x - CornerWorld.x) +
                                       (W[Prev].y - CornerWorld.y) * (W[Prev].y - CornerWorld.y);
                if (Distance > ReachSquared) break;
                Low = Prev;
            }
            for (int Guard = 0; Guard < OutlineCount; ++Guard)
            {
                const int Next = (High + 1) % OutlineCount;
                if (!Closed && High == OutlineCount - 1) break;
                const float Distance = (W[Next].x - CornerWorld.x) * (W[Next].x - CornerWorld.x) +
                                       (W[Next].y - CornerWorld.y) * (W[Next].y - CornerWorld.y);
                if (Distance > ReachSquared) break;
                High = Next;
            }
            // Two boundary breaks — the arc's start + end tangent — so the whole run is ONE section between them (interior dots skipped).
            Breaks.push_back(CumulativeLength[Low]);
            Breaks.push_back(CumulativeLength[High]);
        }
    }

    // 📝 Replace the shape Identifier in place with an OPEN polyline of Run, preserving identity + tint + folder + lock. Used to seat the
    //    first surviving piece of a Trim / Cut back onto the original id so History + selection stay coherent.
    void ReplaceShapeWithPolyline(ParametricSketchShapeStore&         Store,
                                  uint32_t                   Identifier,
                                  const std::vector<ImVec2>& Run)
    {
        ParametricSketchShape* Live = ResolveParametricSketchShape(Store, Identifier);
        if (Live == nullptr)
            return;
        const uint32_t Tint   = Live->TintIndex;
        const uint32_t Folder = Live->FolderIdentifier;
        const bool     Locked = Live->LockEnabled;
        char           KeepTitle[48];
        std::snprintf(KeepTitle, sizeof(KeepTitle), "%s", Live->Title);
        *Live = ConstructParametricSketchShape(ParametricSketchShapeCategory::Polyline, Run);
        Live->Identifier       = Identifier;
        Live->TintIndex        = Tint;
        Live->FolderIdentifier = Folder;
        Live->LockEnabled      = Locked;
        Live->ClosedEnabled    = false;
        Live->FillEnabled      = false;
        std::snprintf(Live->Title, sizeof(Live->Title), "%s", KeepTitle);
    }

    // 📝 Append one ANALYTIC Arc piece spanning [AngleStart, AngleEnd] (radians, AngleEnd may exceed AngleStart by up to a full turn) on
    //    the circle (Centre, Radius). Appends the three placement clicks the Arc family solves from — start, end, and a through point at the
    //    span's mid-angle — so the piece stays one exact arc (Centre / Radius / start + sweep), never a dotted sample run, and re-solves
    //    cleanly on a later move. Carries tint + folder. Returns the new id (0 when the span is too short to solve). Used by the Cut tool
    //    so a cut circle / arc yields real arcs, matching Plasticity.
    uint32_t AppendArcPiece(ParametricSketchShapeStore& Store,
                            ImVec2             Centre,
                            float              Radius,
                            float              AngleStart,
                            float              AngleEnd,
                            uint32_t           Tint,
                            uint32_t           Folder)
    {
        if (Radius <= 1e-5f || std::fabs(AngleEnd - AngleStart) < 1e-4f)
            return 0;
        const float AngleMid = (AngleStart + AngleEnd) * 0.5f;
        const ImVec2 Start  (Centre.x + std::cos(AngleStart) * Radius, Centre.y + std::sin(AngleStart) * Radius);
        const ImVec2 End    (Centre.x + std::cos(AngleEnd)   * Radius, Centre.y + std::sin(AngleEnd)   * Radius);
        const ImVec2 Through(Centre.x + std::cos(AngleMid)   * Radius, Centre.y + std::sin(AngleMid)   * Radius);
        const uint32_t Identifier = AppendParametricSketchShape(Store, ParametricSketchShapeCategory::Arc, { Start, End, Through });
        if (Identifier != 0)
        {
            if (ParametricSketchShape* Live = ResolveParametricSketchShape(Store, Identifier))
            {
                Live->TintIndex        = Tint;
                Live->FolderIdentifier = Folder;
            }
        }
        return Identifier;
    }
}

bool PartitionShapeSegment(ParametricSketchShapeStore& Store, uint32_t Identifier, ImVec2 CursorMm, float ToleranceMm)
{
    ParametricSketchShape* Shape = ResolveParametricSketchShape(Store, Identifier);
    if (Shape == nullptr)
        return false;

    // Flatten the target to its display polyline + build the arc-length parameterisation the sections live on.
    std::vector<ImVec2> W;
    bool                Closed = false;
    BuildWorkingPolyline(*Shape, W, Closed);
    const int Count = (int)W.size();
    if (Count < 2)
    {
        DetachParametricSketchShape(Store, Identifier);
        return true;
    }
    const ParametricSketchShapeCategory Category = Shape->Category;
    std::vector<float> CumulativeLength;
    const float Total = ResolveArcLengths(W, Closed, CumulativeLength);
    if (Total < 1e-5f)
    {
        DetachParametricSketchShape(Store, Identifier);
        return true;
    }

    // ── Section boundaries: intersections with every other curve, the two endpoints of an open run, and (straight family only) the
    //    interior defining vertices. These arc-length positions partition the outline into the sections one of which the click removes. ─
    std::vector<float> Breaks;
    if (!Closed)
    {
        Breaks.push_back(0.0f);
        Breaks.push_back(Total);
    }
    if (StraightRunFamily(Category))
    {
        // Break at ANALYTIC corners only — a filleted / chamfered corner is one indivisible span bounded by its two tangent points, so a
        //    Trim carves the WHOLE arc as one entity rather than a slice between interior sample dots (the dotted-removal the user rejected).
        CollectAnalyticCornerBreaks(*Shape, W, Closed, CumulativeLength, Breaks);
    }
    CollectIntersectionParameters(Store, Identifier, W, Closed, CumulativeLength, Total, Breaks);
    SortAndMergeBreaks(Breaks, Total);

    // No usable boundary (a lone curve nothing crosses): the whole shape is the only section → removing it detaches the shape.
    if ((Closed && Breaks.size() < 2) || (!Closed && Breaks.size() < 2))
    {
        DetachParametricSketchShape(Store, Identifier);
        return true;
    }

    // Where the cursor sits along the outline → which section to remove.
    float CursorGapSquared = 0.0f;
    const float CursorArc = ResolveNearestArcLength(W, Closed, CumulativeLength, Total, CursorMm, CursorGapSquared);
    if (CursorGapSquared > ToleranceMm * ToleranceMm)
        return false;   // the click missed the outline — no-op

    // Locate the section [SectionStart, SectionEnd] the cursor arc falls inside. A closed loop's final section wraps past Total back to
    //    the first break (SectionEnd = Breaks[0] + Total); the cursor arc is lifted by Total when it sits in that wrap span.
    const int BreakCount = (int)Breaks.size();
    float SectionStart = 0.0f;
    float SectionEnd   = Total;
    bool  Found        = false;
    const int SectionCount = Closed ? BreakCount : BreakCount - 1;
    for (int Section = 0; Section < SectionCount; ++Section)
    {
        const float Start = Breaks[Section];
        const float End   = Closed ? ((Section + 1 < BreakCount) ? Breaks[Section + 1] : Breaks[0] + Total) : Breaks[Section + 1];
        float Probe = CursorArc;
        if (Closed && End > Total && Probe < Start)
            Probe += Total;   // lift a cursor sitting in the wrap span
        if (Probe >= Start - 1e-3f && Probe <= End + 1e-3f)
        {
            SectionStart = Start;
            SectionEnd   = End;
            Found        = true;
            break;
        }
    }
    if (!Found)
        return false;

    // Carry identity + analytic scalars for the History label + the round-family survivor path.
    char KeepTitle[48];
    std::snprintf(KeepTitle, sizeof(KeepTitle), "%s", Shape->Title);
    const uint32_t Tint         = Shape->TintIndex;
    const uint32_t Folder       = Shape->FolderIdentifier;
    const ImVec2   ArcCentre    = Shape->Centre;
    const float    ArcRadius    = Shape->Radius;
    const float    ArcStart     = Shape->StartAngle;
    const float    ArcSweep     = Shape->SweepAngle;

    // ── ROUND families (Circle / Arc) keep their survivor ANALYTIC — a trimmed circle-half stays one arc, never a dotted sample run
    //    (matching the "circle halfway in a rectangle, trim the inside/outside" case). The surviving arc-length span(s) map to angle
    //    spans about the centre. A closed Circle survives as the single span the OTHER way round; an open Arc survives as head + tail. ──
    if ((Category == ParametricSketchShapeCategory::Circle || Category == ParametricSketchShapeCategory::Arc) && ArcRadius > 1e-5f)
    {
        std::vector<std::pair<float, float>> Spans;   // [mm] - surviving arc-length spans (start, end); end may exceed Total (wrap)
        if (Closed)
        {
            Spans.push_back({ SectionEnd, SectionStart + Total });
        }
        else
        {
            if (SectionStart > 1e-3f)          Spans.push_back({ 0.0f, SectionStart });
            if (SectionEnd < Total - 1e-3f)    Spans.push_back({ SectionEnd, Total });
        }
        if (Spans.empty())
        {
            DetachParametricSketchShape(Store, Identifier);
            return true;
        }
        DetachParametricSketchShape(Store, Identifier);
        uint32_t LastRound = 0;
        for (const std::pair<float, float>& Span : Spans)
        {
            // Angle is proportional to arc length: a full circle spans TwoPi from base angle 0; an arc spans ArcSweep from ArcStart.
            const float AngleFrom = (Category == ParametricSketchShapeCategory::Circle)
                                    ? (Span.first  / Total) * TwoPi
                                    : ArcStart + (Span.first  / Total) * ArcSweep;
            const float AngleTo   = (Category == ParametricSketchShapeCategory::Circle)
                                    ? (Span.second / Total) * TwoPi
                                    : ArcStart + (Span.second / Total) * ArcSweep;
            const uint32_t Fresh = AppendArcPiece(Store, ArcCentre, ArcRadius, AngleFrom, AngleTo, Tint, Folder);
            if (Fresh != 0)
                LastRound = Fresh;
        }
        if (LastRound == 0)
            return false;
        Store.Hovered  = 0;
        Store.Selected = LastRound;
        char RoundLabel[64];
        std::snprintf(RoundLabel, sizeof(RoundLabel), "Trimmed %s", KeepTitle);
        AppendParametricSketchEdit(Store, Store.Selected, RoundLabel, "scissors");
        return true;
    }

    // ── Build the surviving run(s) for the STRAIGHT / FREE families. A CLOSED loop loses one section and survives as the single open
    //    span the OTHER way round (SectionEnd → SectionStart + Total). An OPEN run loses a section and survives as the head [0,
    //    SectionStart] + tail [SectionEnd, Total], each kept only when it still spans a real length. ────────────────────────────────────
    std::vector<std::vector<ImVec2>> Survivors;
    if (Closed)
    {
        std::vector<ImVec2> Run = ExtractArcSpan(W, Closed, CumulativeLength, Total, SectionEnd, SectionStart + Total);
        if (Run.size() >= 2)
            Survivors.push_back(std::move(Run));
    }
    else
    {
        if (SectionStart > 1e-3f)
        {
            std::vector<ImVec2> Head = ExtractArcSpan(W, Closed, CumulativeLength, Total, 0.0f, SectionStart);
            if (Head.size() >= 2)
                Survivors.push_back(std::move(Head));
        }
        if (SectionEnd < Total - 1e-3f)
        {
            std::vector<ImVec2> Tail = ExtractArcSpan(W, Closed, CumulativeLength, Total, SectionEnd, Total);
            if (Tail.size() >= 2)
                Survivors.push_back(std::move(Tail));
        }
    }

    // Nothing survived (the only section was removed) → detach the whole shape.
    if (Survivors.empty())
    {
        DetachParametricSketchShape(Store, Identifier);
        return true;
    }

    // First survivor reuses the original id; any second is appended fresh so no geometry is lost.
    ReplaceShapeWithPolyline(Store, Identifier, Survivors[0]);
    uint32_t LastIdentifier = Identifier;
    for (size_t Index = 1; Index < Survivors.size(); ++Index)
    {
        const uint32_t Fresh = AppendParametricSketchShape(Store, ParametricSketchShapeCategory::Polyline, Survivors[Index]);
        if (Fresh != 0)
        {
            if (ParametricSketchShape* Live = ResolveParametricSketchShape(Store, Fresh))
            {
                Live->TintIndex        = Tint;
                Live->FolderIdentifier = Folder;
            }
            LastIdentifier = Fresh;
        }
    }

    Store.Hovered  = 0;
    Store.Selected = LastIdentifier;

    char LogLabel[64];
    std::snprintf(LogLabel, sizeof(LogLabel), "Trimmed %s", KeepTitle);
    AppendParametricSketchEdit(Store, Store.Selected, LogLabel, "scissors");
    return true;
}

bool ResolveTrimPreviewSpan(ParametricSketchShapeStore&   Store,
                            uint32_t             Identifier,
                            ImVec2               CursorMm,
                            float                ToleranceMm,
                            std::vector<ImVec2>& OutSpan)
{
    OutSpan.clear();
    ParametricSketchShape* Shape = ResolveParametricSketchShape(Store, Identifier);
    if (Shape == nullptr)
        return false;

    // Mirror PartitionShapeSegment's boundary build EXACTLY (minus the mutation) so the highlight is the span the click would remove.
    std::vector<ImVec2> W;
    bool                Closed = false;
    BuildWorkingPolyline(*Shape, W, Closed);
    const int Count = (int)W.size();
    if (Count < 2)
        return false;
    const ParametricSketchShapeCategory Category = Shape->Category;
    std::vector<float> CumulativeLength;
    const float Total = ResolveArcLengths(W, Closed, CumulativeLength);
    if (Total < 1e-5f)
        return false;

    std::vector<float> Breaks;
    if (!Closed)
    {
        Breaks.push_back(0.0f);
        Breaks.push_back(Total);
    }
    if (StraightRunFamily(Category))
    {
        // Break at ANALYTIC corners only — a filleted / chamfered corner is one indivisible span bounded by its two tangent points, so a
        //    Trim carves the WHOLE arc as one entity rather than a slice between interior sample dots (the dotted-removal the user rejected).
        CollectAnalyticCornerBreaks(*Shape, W, Closed, CumulativeLength, Breaks);
    }
    CollectIntersectionParameters(Store, Identifier, W, Closed, CumulativeLength, Total, Breaks);
    SortAndMergeBreaks(Breaks, Total);
    if (Breaks.size() < 2)
    {
        // The whole shape is the only section → the click would remove all of it; preview the entire outline.
        OutSpan = W;
        if (Closed && !OutSpan.empty())
            OutSpan.push_back(OutSpan.front());
        return true;
    }

    float CursorGapSquared = 0.0f;
    const float CursorArc = ResolveNearestArcLength(W, Closed, CumulativeLength, Total, CursorMm, CursorGapSquared);
    if (CursorGapSquared > ToleranceMm * ToleranceMm)
        return false;

    const int BreakCount = (int)Breaks.size();
    float SectionStart = 0.0f;
    float SectionEnd   = Total;
    bool  Found        = false;
    const int SectionCount = Closed ? BreakCount : BreakCount - 1;
    for (int Section = 0; Section < SectionCount; ++Section)
    {
        const float Start = Breaks[Section];
        const float End   = Closed ? ((Section + 1 < BreakCount) ? Breaks[Section + 1] : Breaks[0] + Total) : Breaks[Section + 1];
        float Probe = CursorArc;
        if (Closed && End > Total && Probe < Start)
            Probe += Total;
        if (Probe >= Start - 1e-3f && Probe <= End + 1e-3f)
        {
            SectionStart = Start;
            SectionEnd   = End;
            Found        = true;
            break;
        }
    }
    if (!Found)
        return false;

    OutSpan = ExtractArcSpan(W, Closed, CumulativeLength, Total, SectionStart, SectionEnd);
    return OutSpan.size() >= 2;
}

namespace
{
    constexpr float ExtendFallbackLengthMm = 10.0f;   // [mm] - how far an end pushes out when NO boundary lies ahead (the fixed-length fallback)

    // 📝 Cast a RAY (Origin + Direction * t, t > 0, Direction unit) at a finite segment [B0, B1] and report the ray parameter t of the hit. Unlike
    //    ResolveSegmentCrossing (which clamps BOTH parameters to their finite segments) the ray side is unbounded forward, which is exactly what an
    //    Extend needs: the curve's end is being pushed OUT past where its own geometry stops. The segment side stays bounded. Rejects a parallel pair
    //    and any hit at / behind the origin (t <= a small epsilon), so an end already touching a boundary does not re-hit it at zero distance.
    bool ResolveRaySegmentHit(ImVec2 Origin, ImVec2 Direction, ImVec2 B0, ImVec2 B1, float& OutRayParameter, ImVec2& OutHit)
    {
        const ImVec2 S(B1.x - B0.x, B1.y - B0.y);
        const float  Denominator = Direction.x * S.y - Direction.y * S.x;
        if (std::fabs(Denominator) < 1e-12f)
            return false;   // parallel / collinear — no single crossing
        const ImVec2 Delta(B0.x - Origin.x, B0.y - Origin.y);
        const float  RayParameter     = (Delta.x * S.y - Delta.y * S.x) / Denominator;
        const float  SegmentParameter = (Delta.x * Direction.y - Delta.y * Direction.x) / Denominator;
        if (RayParameter <= 1e-4f)
            return false;   // at or behind the end — not "ahead"
        if (SegmentParameter < -1e-4f || SegmentParameter > 1.0f + 1e-4f)
            return false;   // the crossing lies off the finite boundary segment
        OutRayParameter = RayParameter;
        OutHit          = ImVec2(Origin.x + Direction.x * RayParameter, Origin.y + Direction.y * RayParameter);
        return true;
    }

    // 📝 Resolve ONE end of an OPEN working polyline for an Extend: which end the cursor is nearer, that end's outgoing tangent (unit), and the world
    //    point the extension grows from. StartEnd true = the run's FIRST point (tangent points back off W[1] → W[0]); false = its LAST point. Returns
    //    false when the run is degenerate (< 2 points, or a zero-length terminal segment that yields no direction).
    bool ResolveExtendEnd(const std::vector<ImVec2>& W, ImVec2 CursorMm, bool& OutStartEnd, ImVec2& OutOrigin, ImVec2& OutDirection)
    {
        const int Count = (int)W.size();
        if (Count < 2)
            return false;

        const ImVec2 First = W.front();
        const ImVec2 Last  = W.back();
        const float  ToFirst = (CursorMm.x - First.x) * (CursorMm.x - First.x) + (CursorMm.y - First.y) * (CursorMm.y - First.y);
        const float  ToLast  = (CursorMm.x - Last.x)  * (CursorMm.x - Last.x)  + (CursorMm.y - Last.y)  * (CursorMm.y - Last.y);
        OutStartEnd = (ToFirst <= ToLast);

        // The outgoing tangent at the chosen end, taken from its terminal segment so a curved run extends along its true leaving direction.
        const ImVec2 Origin = OutStartEnd ? First : Last;
        const ImVec2 Inner  = OutStartEnd ? W[1]  : W[Count - 2];
        ImVec2       Direction(Origin.x - Inner.x, Origin.y - Inner.y);
        const float  Length = std::sqrt(Direction.x * Direction.x + Direction.y * Direction.y);
        if (Length < 1e-6f)
            return false;   // a zero-length terminal segment gives no direction
        Direction.x /= Length;
        Direction.y /= Length;

        OutOrigin    = Origin;
        OutDirection = Direction;
        return true;
    }

    // 📝 The world point an Extend of this open run would reach: cast the chosen end's tangent ray at EVERY other displayed, unlocked shape and take the
    //    NEAREST forward hit (first crossing — the tool is sticky, so a second click reaches the next boundary out). When nothing lies ahead the end
    //    pushes out by ExtendFallbackLengthMm instead, so the tool always does something visible. OutReachedBoundary reports which of the two happened.
    ImVec2 ResolveExtendTarget(ParametricSketchShapeStore& Store,
                               uint32_t                    TargetIdentifier,
                               ImVec2                      Origin,
                               ImVec2                      Direction,
                               bool&                       OutReachedBoundary)
    {
        float  NearestParameter = 0.0f;
        ImVec2 NearestHit(0, 0);
        bool   Found = false;

        for (ParametricSketchShape& Other : Store.Shapes)
        {
            if (Other.Identifier == TargetIdentifier || !Other.Displayed || Other.LockEnabled)
                continue;
            std::vector<ImVec2> OtherPoints;
            bool                OtherClosed = false;
            BuildWorkingPolyline(Other, OtherPoints, OtherClosed);
            const int OtherCount = (int)OtherPoints.size();
            if (OtherCount < 2)
                continue;
            const int OtherSegments = OtherClosed ? OtherCount : OtherCount - 1;
            for (int Segment = 0; Segment < OtherSegments; ++Segment)
            {
                const ImVec2 B0 = OtherPoints[Segment];
                const ImVec2 B1 = OtherPoints[(Segment + 1) % OtherCount];
                float        RayParameter = 0.0f;
                ImVec2       Hit;
                if (!ResolveRaySegmentHit(Origin, Direction, B0, B1, RayParameter, Hit))
                    continue;
                if (!Found || RayParameter < NearestParameter)
                {
                    NearestParameter = RayParameter;
                    NearestHit       = Hit;
                    Found            = true;
                }
            }
        }

        OutReachedBoundary = Found;
        if (Found)
            return NearestHit;
        return ImVec2(Origin.x + Direction.x * ExtendFallbackLengthMm, Origin.y + Direction.y * ExtendFallbackLengthMm);
    }
}

bool ExtendShapeToBoundary(ParametricSketchShapeStore& Store, uint32_t Identifier, ImVec2 CursorMm, float ToleranceMm)
{
    ParametricSketchShape* Shape = ResolveParametricSketchShape(Store, Identifier);
    if (Shape == nullptr || Shape->LockEnabled)
        return false;

    std::vector<ImVec2> W;
    bool                Closed = false;
    BuildWorkingPolyline(*Shape, W, Closed);
    if (Closed)
        return false;   // a closed loop has no free end to extend
    if (W.size() < 2)
        return false;

    // The click must land near the run itself (the same tolerance gate Trim / Cut use), else a stray click far away would silently move an endpoint.
    std::vector<float> CumulativeLength;
    const float        Total = ResolveArcLengths(W, Closed, CumulativeLength);
    if (Total < 1e-5f)
        return false;
    float       CursorGapSquared = 0.0f;
    const float CursorArc = ResolveNearestArcLength(W, Closed, CumulativeLength, Total, CursorMm, CursorGapSquared);
    (void)CursorArc;
    if (CursorGapSquared > ToleranceMm * ToleranceMm)
        return false;   // the click missed the outline — no-op

    bool   StartEnd = false;
    ImVec2 Origin(0, 0), Direction(0, 0);
    if (!ResolveExtendEnd(W, CursorMm, StartEnd, Origin, Direction))
        return false;

    bool         ReachedBoundary = false;
    const ImVec2 Target = ResolveExtendTarget(Store, Identifier, Origin, Direction, ReachedBoundary);

    // Grow the run: the extended end gains the target point (the run keeps every existing vertex, so a curve's shape is preserved and only its
    //    terminal segment lengthens). A start-end extension prepends; an end-end extension appends.
    std::vector<ImVec2> Extended;
    Extended.reserve(W.size() + 1);
    if (StartEnd)
    {
        Extended.push_back(Target);
        Extended.insert(Extended.end(), W.begin(), W.end());
    }
    else
    {
        Extended.insert(Extended.end(), W.begin(), W.end());
        Extended.push_back(Target);
    }

    char KeepTitle[48];
    std::snprintf(KeepTitle, sizeof(KeepTitle), "%s", Shape->Title);

    ReplaceShapeWithPolyline(Store, Identifier, Extended);

    Store.Hovered  = 0;
    Store.Selected = Identifier;

    char LogLabel[64];
    std::snprintf(LogLabel, sizeof(LogLabel), "Extended %s", KeepTitle);
    AppendParametricSketchEdit(Store, Identifier, LogLabel, "scissors");
    return true;
}

bool ResolveExtendPreviewSpan(ParametricSketchShapeStore& Store,
                              uint32_t                    Identifier,
                              ImVec2                      CursorMm,
                              float                       ToleranceMm,
                              std::vector<ImVec2>&        OutSpan)
{
    OutSpan.clear();
    ParametricSketchShape* Shape = ResolveParametricSketchShape(Store, Identifier);
    if (Shape == nullptr || Shape->LockEnabled)
        return false;

    // Mirror ExtendShapeToBoundary's resolve EXACTLY (minus the mutation) so the highlight is the span the click would ADD.
    std::vector<ImVec2> W;
    bool                Closed = false;
    BuildWorkingPolyline(*Shape, W, Closed);
    if (Closed || W.size() < 2)
        return false;

    std::vector<float> CumulativeLength;
    const float        Total = ResolveArcLengths(W, Closed, CumulativeLength);
    if (Total < 1e-5f)
        return false;
    float CursorGapSquared = 0.0f;
    ResolveNearestArcLength(W, Closed, CumulativeLength, Total, CursorMm, CursorGapSquared);
    if (CursorGapSquared > ToleranceMm * ToleranceMm)
        return false;

    bool   StartEnd = false;
    ImVec2 Origin(0, 0), Direction(0, 0);
    if (!ResolveExtendEnd(W, CursorMm, StartEnd, Origin, Direction))
        return false;

    bool         ReachedBoundary = false;
    const ImVec2 Target = ResolveExtendTarget(Store, Identifier, Origin, Direction, ReachedBoundary);

    // The preview is just the ADDED span — the two-point run from the current end out to the target, so the user sees exactly what the click grows.
    OutSpan.push_back(Origin);
    OutSpan.push_back(Target);
    return true;
}

bool CutShapeAtPoint(ParametricSketchShapeStore& Store, uint32_t Identifier, ImVec2 CursorMm, float ToleranceMm)
{
    ParametricSketchShape* Shape = ResolveParametricSketchShape(Store, Identifier);
    if (Shape == nullptr)
        return false;

    std::vector<ImVec2> W;
    bool                Closed = false;
    BuildWorkingPolyline(*Shape, W, Closed);
    const int Count = (int)W.size();
    if (Count < 2)
        return false;
    std::vector<float> CumulativeLength;
    const float Total = ResolveArcLengths(W, Closed, CumulativeLength);
    if (Total < 1e-5f)
        return false;

    // Where along the outline the user clicked → the single cut point. Miss the outline within tolerance = no-op.
    float CursorGapSquared = 0.0f;
    const float CutArc = ResolveNearestArcLength(W, Closed, CumulativeLength, Total, CursorMm, CursorGapSquared);
    if (CursorGapSquared > ToleranceMm * ToleranceMm)
        return false;

    // Capture identity + display cues to carry onto the pieces.
    char KeepTitle[48];
    std::snprintf(KeepTitle, sizeof(KeepTitle), "%s", Shape->Title);
    const uint32_t Tint             = Shape->TintIndex;
    const uint32_t Folder           = Shape->FolderIdentifier;
    const ParametricSketchShapeCategory Category = Shape->Category;
    const ImVec2  ArcCentre         = Shape->Centre;
    const float   ArcRadius         = Shape->Radius;
    const float   ArcStartAngle     = Shape->StartAngle;
    const float   ArcSweepAngle     = Shape->SweepAngle;

    // ── ANALYTIC round families stay exact: a cut CIRCLE / ARC yields real arcs (Centre / Radius / start + sweep), never a dotted sample
    //    run. The click point maps to an angle about the centre; a closed circle OPENS into one full-sweep arc, an open arc SPLITS into
    //    two arcs at that angle. Matches Plasticity ("same if it were a circle it would be 2 arcs" once cut twice). ─────────────────────
    if (Category == ParametricSketchShapeCategory::Circle && ArcRadius > 1e-5f)
    {
        const ImVec2 CutPoint = PointAtArcLength(W, Closed, CumulativeLength, Total, CutArc);
        const float  CutAngle = std::atan2(CutPoint.y - ArcCentre.y, CutPoint.x - ArcCentre.x);
        DetachParametricSketchShape(Store, Identifier);   // replaced by its opened arc (never silently dropped — logged below)
        const uint32_t Opened = AppendArcPiece(Store, ArcCentre, ArcRadius, CutAngle, CutAngle + TwoPi, Tint, Folder);
        if (Opened == 0)
            return false;
        Store.Hovered  = 0;
        Store.Selected = Opened;
        char LogLabel[64];
        std::snprintf(LogLabel, sizeof(LogLabel), "Cut %s", KeepTitle);
        AppendParametricSketchEdit(Store, Opened, LogLabel, "scissors");
        return true;
    }
    if (Category == ParametricSketchShapeCategory::Arc && ArcRadius > 1e-5f)
    {
        const ImVec2 CutPoint = PointAtArcLength(W, Closed, CumulativeLength, Total, CutArc);
        // The cut angle expressed as a fraction of the signed sweep so the two pieces span [Start, Cut] and [Cut, End] in the arc's own
        //    direction. Reject a cut at either endpoint (nothing to divide).
        float CutAngle = std::atan2(CutPoint.y - ArcCentre.y, CutPoint.x - ArcCentre.x);
        float Delta = CutAngle - ArcStartAngle;
        while (Delta >  TwoPi * 0.5f) Delta -= TwoPi;
        while (Delta < -TwoPi * 0.5f) Delta += TwoPi;
        const float Fraction = ArcSweepAngle != 0.0f ? Delta / ArcSweepAngle : 0.0f;
        if (Fraction <= 1e-3f || Fraction >= 1.0f - 1e-3f)
            return false;
        const float SplitAngle = ArcStartAngle + Delta;
        DetachParametricSketchShape(Store, Identifier);
        const uint32_t First  = AppendArcPiece(Store, ArcCentre, ArcRadius, ArcStartAngle, SplitAngle, Tint, Folder);
        const uint32_t Second = AppendArcPiece(Store, ArcCentre, ArcRadius, SplitAngle, ArcStartAngle + ArcSweepAngle, Tint, Folder);
        if (First == 0 && Second == 0)
            return false;
        Store.Hovered  = 0;
        Store.Selected = Second != 0 ? Second : First;
        char LogLabel[64];
        std::snprintf(LogLabel, sizeof(LogLabel), "Cut %s", KeepTitle);
        AppendParametricSketchEdit(Store, Store.Selected, LogLabel, "scissors");
        return true;
    }

    // ── STRAIGHT / FREE families: a CLOSED shape (Rectangle / Polygon / Ellipse / Slot / Profile) OPENS at the cut point into one open
    //    run (still the same outline, no longer a loop); an OPEN run (Line / Polyline / Bezier / Spline) SPLITS at the cut point into a
    //    head + tail. Pieces follow the display tessellation between the cut points. ───────────────────────────────────────────────────
    std::vector<std::vector<ImVec2>> Pieces;
    if (Closed)
    {
        std::vector<ImVec2> Run = ExtractArcSpan(W, Closed, CumulativeLength, Total, CutArc, CutArc + Total);
        if (Run.size() >= 2)
            Pieces.push_back(std::move(Run));
    }
    else
    {
        if (CutArc > 1e-3f)
        {
            std::vector<ImVec2> Head = ExtractArcSpan(W, Closed, CumulativeLength, Total, 0.0f, CutArc);
            if (Head.size() >= 2)
                Pieces.push_back(std::move(Head));
        }
        if (CutArc < Total - 1e-3f)
        {
            std::vector<ImVec2> Tail = ExtractArcSpan(W, Closed, CumulativeLength, Total, CutArc, Total);
            if (Tail.size() >= 2)
                Pieces.push_back(std::move(Tail));
        }
    }

    if (Pieces.empty())
        return false;   // the cut landed on an endpoint of an open run — nothing to divide

    // First piece reuses the original id (identity + tint + folder carried by ReplaceShapeWithPolyline); the rest are appended.
    ReplaceShapeWithPolyline(Store, Identifier, Pieces[0]);
    uint32_t LastIdentifier = Identifier;
    for (size_t Index = 1; Index < Pieces.size(); ++Index)
    {
        const uint32_t Fresh = AppendParametricSketchShape(Store, ParametricSketchShapeCategory::Polyline, Pieces[Index]);
        if (Fresh != 0)
        {
            if (ParametricSketchShape* Live = ResolveParametricSketchShape(Store, Fresh))
            {
                Live->TintIndex        = Tint;
                Live->FolderIdentifier = Folder;
            }
            LastIdentifier = Fresh;
        }
    }

    Store.Hovered  = 0;
    Store.Selected = LastIdentifier;

    char LogLabel[64];
    std::snprintf(LogLabel, sizeof(LogLabel), "Cut %s", KeepTitle);
    AppendParametricSketchEdit(Store, Store.Selected, LogLabel, "scissors");
    return true;
}

bool AssembleOpenShapes(ParametricSketchShapeStore& Store, float WeldToleranceMm)
{
    // Flatten every SELECTED open shape into a working run (paired with its source id) — closed loops are the boolean Union's job.
    struct OpenRun
    {
        uint32_t            Identifier = 0;      // [-]  - the source shape (first one keeps its id; the rest are detached)
        std::vector<ImVec2> Points;             // [mm] - its display polyline (open)
        bool                Consumed = false;    // [-]  - already welded into the chain
    };
    std::vector<OpenRun> Runs;
    for (uint32_t Identifier : Store.SelectionSet)
    {
        ParametricSketchShape* Shape = ResolveParametricSketchShape(Store, Identifier);
        if (Shape == nullptr || Shape->ClosedEnabled)
            continue;   // absent or a closed loop — not an open run to concatenate
        std::vector<ImVec2> W;
        bool                Closed = false;
        BuildWorkingPolyline(*Shape, W, Closed);
        if (W.size() >= 2)
            Runs.push_back({ Identifier, std::move(W), false });
    }
    if (Runs.size() < 2)
        return false;   // fewer than two open runs — nothing to join

    const float WeldSquared = WeldToleranceMm * WeldToleranceMm;

    // ── Chain the runs end-to-end. Seed the path with the first run, then repeatedly find a remaining run whose either endpoint
    //    coincides with the path's growing tail (reversing that run when its FAR end is the match), appending it minus the shared seam
    //    point. A single pass over the tail suffices because each weld extends only the tail; the head is fixed by the seed. ────────────
    std::vector<ImVec2> Path = Runs[0].Points;
    Runs[0].Consumed = true;
    std::vector<uint32_t> Welded;
    Welded.push_back(Runs[0].Identifier);

    bool Extended = true;
    while (Extended)
    {
        Extended = false;
        const ImVec2 Tail = Path.back();
        for (OpenRun& Candidate : Runs)
        {
            if (Candidate.Consumed)
                continue;
            const ImVec2 Front = Candidate.Points.front();
            const ImVec2 Back   = Candidate.Points.back();
            const float  ToFront = PointGapSquared(Tail, Front);
            const float  ToBack   = PointGapSquared(Tail, Back);
            if (ToFront <= WeldSquared)
            {
                for (size_t Index = 1; Index < Candidate.Points.size(); ++Index)
                    Path.push_back(Candidate.Points[Index]);   // skip the shared seam point
            }
            else if (ToBack <= WeldSquared)
            {
                for (int Index = (int)Candidate.Points.size() - 2; Index >= 0; --Index)
                    Path.push_back(Candidate.Points[Index]);   // reversed; skip the shared seam point
            }
            else
            {
                continue;   // neither end meets the tail — try the next candidate
            }
            Candidate.Consumed = true;
            Welded.push_back(Candidate.Identifier);
            Extended = true;
            break;
        }
    }

    if (Welded.size() < 2)
        return false;   // the selected runs did not connect end-to-end — leave them untouched

    // The path closes into a Profile when its two free ends meet; otherwise it stays an open Polyline.
    bool ClosedLoop = false;
    if (Path.size() >= 3 && PointGapSquared(Path.front(), Path.back()) <= WeldSquared)
    {
        Path.pop_back();   // drop the duplicate closing point — ClosedEnabled implies the (last → first) wrap
        ClosedLoop = true;
    }

    // Seat the joined path onto the FIRST welded shape's id (identity + tint + folder carried); detach every other welded operand.
    const uint32_t KeepIdentifier = Welded.front();
    ParametricSketchShape* Live = ResolveParametricSketchShape(Store, KeepIdentifier);
    if (Live == nullptr)
        return false;
    const uint32_t Tint   = Live->TintIndex;
    const uint32_t Folder = Live->FolderIdentifier;

    for (size_t Index = 1; Index < Welded.size(); ++Index)
        DetachParametricSketchShape(Store, Welded[Index]);

    Live = ResolveParametricSketchShape(Store, KeepIdentifier);   // re-resolve — Detach may have reallocated the vector
    if (Live == nullptr)
        return false;
    *Live = ConstructParametricSketchShape(ClosedLoop ? ParametricSketchShapeCategory::Profile : ParametricSketchShapeCategory::Polyline, Path);
    Live->Identifier       = KeepIdentifier;
    Live->TintIndex        = Tint;
    Live->FolderIdentifier = Folder;
    Live->ClosedEnabled    = ClosedLoop;
    Live->FillEnabled      = ClosedLoop;
    std::snprintf(Live->Title, sizeof(Live->Title), "%s %u",
                  ClosedLoop ? "Profile" : "Polyline", KeepIdentifier);

    Store.Hovered  = 0;
    Store.Selected = KeepIdentifier;
    Store.SelectionSet.clear();

    char LogLabel[64];
    std::snprintf(LogLabel, sizeof(LogLabel), "Joined %s", Live->Title);
    AppendParametricSketchEdit(Store, KeepIdentifier, LogLabel, ClosedLoop ? "vector-square" : "spline");
    return true;
}

bool ResolveShapesBoundary(ParametricSketchShapeStore& Store, ImVec2& BoundaryMinimum, ImVec2& BoundaryMaximum)
{
    bool AnyDisplayed = false;
    ImVec2 Minimum(0, 0);
    ImVec2 Maximum(0, 0);

    for (ParametricSketchShape& Shape : Store.Shapes)
    {
        if (!Shape.Displayed)
            continue;
        const std::vector<ImVec2>& Outline = RetrieveCachedOutline(Shape);
        if (Outline.empty())
            continue;

        if (!AnyDisplayed)
        {
            Minimum = Shape.CachedBoundaryMinimum;
            Maximum = Shape.CachedBoundaryMaximum;
            AnyDisplayed = true;
        }
        else
        {
            Minimum.x = std::min(Minimum.x, Shape.CachedBoundaryMinimum.x);
            Minimum.y = std::min(Minimum.y, Shape.CachedBoundaryMinimum.y);
            Maximum.x = std::max(Maximum.x, Shape.CachedBoundaryMaximum.x);
            Maximum.y = std::max(Maximum.y, Shape.CachedBoundaryMaximum.y);
        }
    }

    if (!AnyDisplayed)
        return false;

    BoundaryMinimum = Minimum;
    BoundaryMaximum = Maximum;
    return true;
}

} // namespace Frontier
