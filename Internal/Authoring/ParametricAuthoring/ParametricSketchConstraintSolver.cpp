/*==============================================================================================================================================
                                                         PARAMETRICSKETCHCONSTRAINTSOLVER.CPP
==============================================================================================================================================*/
// 🧩 The parametric constraint solver: builds a shared point pool over the store's shapes, welds coincident handles, relaxes the free
//    points across a capped run of iterations (early-out once the residual settles) so the constraints + dimensions hold, then writes
//    the solved positions back + re-derives each touched shape's analytic scalars. Coincident constraints hard-weld their two handles
//    into one pooled point regardless of separation; Tangent is a real distance rule (line↔circle = Radius, circle↔circle = R₁ ± R₂)
//    with round centres pooled like ordinary points. Round dimensions write the scalar directly. Pure CPU + analytic.

#include "ParametricSketchConstraintSolver.h"

#include <algorithm> // 📝 std::min / std::max — clamp + boundary maths.
#include <cmath>     // 📝 std::sqrt / std::atan2 / std::cos / std::sin / std::hypot / std::fabs — the relaxation maths.
#include <cstdio>    // 📝 std::snprintf — preserve a shape's title across the write-back rebuild.

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr float Pi        = 3.14159265358979323846f;   // [rad] - half a turn, for the perpendicular offset + degree conversion
    constexpr float WeldWorld = 0.001f;                    // [mm]  - coincident weld tolerance (points closer than this pool together)
    constexpr int   CentrePointIndex     = -1;             // [idx] - a round shape's centre raw-slot marker (see ConstructPointPool)
    constexpr int   MaxSolveIterations   = 500;            // [-]   - hard cap on the relaxation (never loop unbounded)
    constexpr float SolveResidualTolerance = 1e-4f;        // [mm]  - settled when the largest per-iteration point move is below this

    // 📝 One moveable point in the solve-time pool. Position is the live world-mm location the relaxation nudges; Fixed pins it (a Fixed
    //    constraint or a locked shape). Multiple shape point-handles that welded together map to ONE pooled point, so moving it drags
    //    every welded edge at once — this is how the pool gives Frontier's identity-less points the shared-point behaviour p4 has by id.
    struct PooledPoint
    {
        ImVec2 Position = ImVec2(0, 0);   // [mm] - live world position the relaxation moves
        bool   Fixed    = false;          // [-]  - pinned (never moved)
    };

    // 📝 The solve-time point pool: one PooledPoint per welded location + a lookup from a shape's (Identifier, PointIndex) to its pool
    //    slot. Built fresh each Solve from the store's shapes; the constraints / dimensions address pooled points through Locate.
    struct ParametricSketchPointPool
    {
        std::vector<PooledPoint>              Points;        // [-] - the moveable pooled points
        std::vector<uint32_t>                 ShapeOfSlot;   // [-] - one owning shape id per raw slot (parallel to the raw slot list)
        // Raw-slot bookkeeping: one raw slot per shape point (before welding). RawToPool maps a raw slot -> its pooled point index.
        std::vector<uint32_t>                 RawShapeId;    // [-] - the shape id of each raw slot
        std::vector<int>                      RawPointIndex; // [-] - the point index of each raw slot
        std::vector<int>                      RawToPool;     // [-] - pooled-point index each raw slot welded into
    };

    // 📝 Whether a category's Points are the geometry the solver relaxes (straight + curve families) rather than placement clicks the
    //    scalars supersede (round families). Only point-driven shapes contribute pooled points + are written back from the pool.
    bool PointDrivenCategory(ParametricSketchShapeCategory Category)
    {
        switch (Category)
        {
            case ParametricSketchShapeCategory::Line:
            case ParametricSketchShapeCategory::Polyline:
            case ParametricSketchShapeCategory::Rectangle:
            case ParametricSketchShapeCategory::Bezier:
            case ParametricSketchShapeCategory::BSpline:
            case ParametricSketchShapeCategory::Nurbs:
            case ParametricSketchShapeCategory::Spline:
            case ParametricSketchShapeCategory::Profile:
                return true;
            default:   // Arc / Circle / Ellipse / Polygon / Conic / Slot — scalar-driven
                return false;
        }
    }

    // 📝 Resolve the raw slot for a shape point-handle (before welding). -1 when the handle names a shape / index not in the pool.
    int LocateRawSlot(const ParametricSketchPointPool& Pool, ParametricSketchPointHandle Handle)
    {
        for (size_t Slot = 0; Slot < Pool.RawShapeId.size(); ++Slot)
            if (Pool.RawShapeId[Slot] == Handle.ShapeIdentifier && Pool.RawPointIndex[Slot] == Handle.PointIndex)
                return (int)Slot;
        return -1;
    }

    // 📝 Resolve the pooled-point index a shape point-handle addresses (-1 when absent). Constraints / dimensions relax through this.
    int LocatePool(const ParametricSketchPointPool& Pool, ParametricSketchPointHandle Handle)
    {
        const int Raw = LocateRawSlot(Pool, Handle);
        return Raw < 0 ? -1 : Pool.RawToPool[Raw];
    }

    // ── the relaxation primitives (ported verbatim in behaviour from p4.html, lines 4253-4258) ──

    void MoveTo(PooledPoint& Point, float X, float Y)
    {
        if (Point.Fixed)
            return;
        Point.Position.x = X;
        Point.Position.y = Y;
    }

    float Distance(const PooledPoint& A, const PooledPoint& B)
    {
        return std::hypot(A.Position.x - B.Position.x, A.Position.y - B.Position.y);
    }

    void EnforceDistance(PooledPoint& A, PooledPoint& B, float Target)
    {
        float Length = Distance(A, B);
        if (Length < 1e-4f)
            Length = 1e-4f;
        const float DirX = (B.Position.x - A.Position.x) / Length;
        const float DirY = (B.Position.y - A.Position.y) / Length;
        if (A.Fixed && B.Fixed)
            return;
        if (A.Fixed)
        {
            B.Position.x = A.Position.x + DirX * Target;
            B.Position.y = A.Position.y + DirY * Target;
            return;
        }
        if (B.Fixed)
        {
            A.Position.x = B.Position.x - DirX * Target;
            A.Position.y = B.Position.y - DirY * Target;
            return;
        }
        const float Half = (Target - Length) / 2.0f;
        A.Position.x -= DirX * Half;
        A.Position.y -= DirY * Half;
        B.Position.x += DirX * Half;
        B.Position.y += DirY * Half;
    }

    // Enforce a signed component span (0 = x/horizontal, 1 = y/vertical). Preserves the current sign, matching p4's enforceComponent.
    void EnforceComponent(PooledPoint& A, PooledPoint& B, float Target, int Axis)
    {
        float& AComponent = (Axis == 0) ? A.Position.x : A.Position.y;
        float& BComponent = (Axis == 0) ? B.Position.x : B.Position.y;
        const float Current = BComponent - AComponent;
        const float Sign    = Current < 0.0f ? -1.0f : 1.0f;
        const float Want     = Sign * Target;
        if (A.Fixed && B.Fixed)
            return;
        if (A.Fixed)
        {
            BComponent = AComponent + Want;
            return;
        }
        if (B.Fixed)
        {
            AComponent = BComponent - Want;
            return;
        }
        const float Half = (Want - Current) / 2.0f;
        AComponent -= Half;
        BComponent += Half;
    }

    // Align edge (E2A..E2B) so its angle = edge (E1A..E1B) angle + Offset (0 = parallel, pi/2 = perpendicular), about its midpoint.
    void AlignAngle(PooledPoint& E1A, PooledPoint& E1B, PooledPoint& E2A, PooledPoint& E2B, float Offset)
    {
        const float Angle1 = std::atan2(E1B.Position.y - E1A.Position.y, E1B.Position.x - E1A.Position.x);
        const float Target = Angle1 + Offset;
        const float Length = Distance(E2A, E2B);
        const float MidX   = (E2A.Position.x + E2B.Position.x) / 2.0f;
        const float MidY   = (E2A.Position.y + E2B.Position.y) / 2.0f;
        const float DirX   = std::cos(Target);
        const float DirY   = std::sin(Target);
        if (!E2A.Fixed)
        {
            E2A.Position.x = MidX - DirX * Length / 2.0f;
            E2A.Position.y = MidY - DirY * Length / 2.0f;
        }
        if (!E2B.Fixed)
        {
            E2B.Position.x = MidX + DirX * Length / 2.0f;
            E2B.Position.y = MidY + DirY * Length / 2.0f;
        }
    }

    void EnforceEqualLength(PooledPoint& E1A, PooledPoint& E1B, PooledPoint& E2A, PooledPoint& E2B)
    {
        EnforceDistance(E2A, E2B, Distance(E1A, E1B));
    }

    // Set edge (E2A..E2B) angle to edge (E1A..E1B) angle + Degrees, rotating about E2A (matches p4's setAngle).
    void SetEdgeAngle(PooledPoint& E1A, PooledPoint& E1B, PooledPoint& E2A, PooledPoint& E2B, float Degrees)
    {
        const float Angle1 = std::atan2(E1B.Position.y - E1A.Position.y, E1B.Position.x - E1A.Position.x);
        const float Target = Angle1 + Degrees * Pi / 180.0f;
        const float Length = Distance(E2A, E2B);
        const float DirX   = std::cos(Target);
        const float DirY   = std::sin(Target);
        if (!E2B.Fixed)
        {
            E2B.Position.x = E2A.Position.x + DirX * Length;
            E2B.Position.y = E2A.Position.y + DirY * Length;
        }
    }

    // 📝 One side of a Tangent constraint, resolved to what the solver can relax: either a straight edge (two pooled endpoints) or a
    //    round shape (its pooled centre + tangency radius). The round side addresses the shape by Identifier; the CentrePointIndex
    //    sentinel names the centre slot the pool built for it (round families hold their defining points but only the centre is pooled).
    struct TangentOperand
    {
        bool  RoundEnabled = false;   // [-]  - true = a round shape (centre + radius); false = a straight edge (two endpoints)
        int   CentreSlot   = -1;      // [idx] - pooled centre slot (round side)
        float Radius       = 0.0f;    // [mm]  - tangency radius (Arc / Circle / Polygon / Slot: Radius; Ellipse: MajorAxis)
        int   LineSlotA    = -1;      // [idx] - pooled line endpoints (edge side)
        int   LineSlotB    = -1;      // [idx] - pooled line endpoints (edge side)
    };

    const ParametricSketchShape* ResolveShapeConst(const ParametricSketchShapeStore& Store, uint32_t Identifier)
    {
        for (const ParametricSketchShape& Shape : Store.Shapes)
            if (Shape.Identifier == Identifier)
                return &Shape;
        return nullptr;
    }

    bool ResolveTangentSide(const ParametricSketchShapeStore& Store, const ParametricSketchPointPool& Pool,
                            ParametricSketchPointHandle Start, ParametricSketchPointHandle End, TangentOperand& Operand)
    {
        const ParametricSketchShape* Shape = ResolveShapeConst(Store, Start.ShapeIdentifier);
        if (!Shape)
            return false;
        switch (Shape->Category)
        {
            case ParametricSketchShapeCategory::Arc:
            case ParametricSketchShapeCategory::Circle:
            case ParametricSketchShapeCategory::Ellipse:
            case ParametricSketchShapeCategory::Polygon:
            case ParametricSketchShapeCategory::Slot:
            {
                ParametricSketchPointHandle Centre;
                Centre.ShapeIdentifier = Shape->Identifier;
                Centre.PointIndex      = CentrePointIndex;
                Operand.RoundEnabled = true;
                Operand.CentreSlot   = LocatePool(Pool, Centre);
                Operand.Radius       = (Shape->Category == ParametricSketchShapeCategory::Ellipse) ? Shape->MajorAxis : Shape->Radius;
                return Operand.CentreSlot >= 0;
            }
            default:
                Operand.RoundEnabled = false;
                Operand.LineSlotA    = LocatePool(Pool, Start);
                Operand.LineSlotB    = LocatePool(Pool, End);
                return Operand.LineSlotA >= 0 && Operand.LineSlotB >= 0;
        }
    }

    // Enforce line↔circle tangency: the perpendicular distance from the round centre to the line through A..B must equal Radius.
    // Moves the free endpoints and the free centre symmetrically along the line's normal — the residual loop converges the pair to
    // the ring. Respects the Fixed pins per pooled point; a fully fixed edge leaves only the centre to move (and vice-versa).
    void EnforceLineCircleTangent(std::vector<PooledPoint>& Points, int LineSlotA, int LineSlotB, int CentreSlot, float Radius)
    {
        if (LineSlotA < 0 || LineSlotB < 0 || CentreSlot < 0 || Radius <= 0.0f)
            return;
        PooledPoint& A = Points[LineSlotA];
        PooledPoint& B = Points[LineSlotB];
        PooledPoint& C = Points[CentreSlot];
        if (A.Fixed && B.Fixed && C.Fixed)
            return;

        const float EdgeX  = B.Position.x - A.Position.x;
        const float EdgeY  = B.Position.y - A.Position.y;
        const float Length = std::hypot(EdgeX, EdgeY);
        if (Length < 1e-4f)
            return;   // degenerate edge — no line to be tangent to
        const float Cross    = EdgeX * (C.Position.y - A.Position.y) - EdgeY * (C.Position.x - A.Position.x);
        const float Distance = std::fabs(Cross) / Length;       // [mm] - perpendicular distance centre → line
        const float Error    = Distance - Radius;               // [mm] - +ve = the line is too far; pull it closer

        // Unit normal pointing from the line toward the centre (an arbitrary perpendicular when the centre sits on the line).
        float NormalX, NormalY;
        if (Distance > 1e-9f)
        {
            const float Sign = (Cross >= 0.0f) ? 1.0f : -1.0f;
            NormalX = -Sign * EdgeY / Length;
            NormalY =  Sign * EdgeX / Length;
        }
        else
        {
            NormalX = -EdgeY / Length;
            NormalY =  EdgeX / Length;
        }

        if (Error > 0.0f)   // need a smaller distance: the line moves toward the centre, the centre toward the line
        {
            const float LineCorrection   = (A.Fixed && B.Fixed) ? 0.0f : (C.Fixed ? Error : Error * 0.5f);
            const float CentreCorrection = C.Fixed ? 0.0f : ((A.Fixed && B.Fixed) ? Error : Error * 0.5f);
            if (!A.Fixed) { A.Position.x -= NormalX * LineCorrection;   A.Position.y -= NormalY * LineCorrection; }
            if (!B.Fixed) { B.Position.x -= NormalX * LineCorrection;   B.Position.y -= NormalY * LineCorrection; }
            if (!C.Fixed) { C.Position.x -= NormalX * CentreCorrection; C.Position.y -= NormalY * CentreCorrection; }
        }
        else if (Error < 0.0f)   // need a larger distance: the line moves away from the centre, the centre away from the line
        {
            const float Need             = -Error;
            const float LineCorrection   = (A.Fixed && B.Fixed) ? 0.0f : (C.Fixed ? Need : Need * 0.5f);
            const float CentreCorrection = C.Fixed ? 0.0f : ((A.Fixed && B.Fixed) ? Need : Need * 0.5f);
            if (!A.Fixed) { A.Position.x += NormalX * LineCorrection;   A.Position.y += NormalY * LineCorrection; }
            if (!B.Fixed) { B.Position.x += NormalX * LineCorrection;   B.Position.y += NormalY * LineCorrection; }
            if (!C.Fixed) { C.Position.x += NormalX * CentreCorrection; C.Position.y += NormalY * CentreCorrection; }
        }
    }

    // Enforce circle↔circle tangency: the centre distance must equal R₁ + R₂ (external tangency). Moves the free centres
    // symmetrically along the line between them. Same Fixed-aware relaxation as the line case.
    void EnforceCircleCircleTangent(std::vector<PooledPoint>& Points, int CentreSlotA, int CentreSlotB, float RadiusA, float RadiusB)
    {
        if (CentreSlotA < 0 || CentreSlotB < 0)
            return;
        PooledPoint& A = Points[CentreSlotA];
        PooledPoint& B = Points[CentreSlotB];
        if (A.Fixed && B.Fixed)
            return;

        const float OffsetX  = B.Position.x - A.Position.x;
        const float OffsetY  = B.Position.y - A.Position.y;
        const float Distance = std::hypot(OffsetX, OffsetY);
        float DirX = 1.0f, DirY = 0.0f;
        if (Distance > 1e-9f)
        {
            DirX = OffsetX / Distance;
            DirY = OffsetY / Distance;
        }
        const float Error = Distance - (RadiusA + RadiusB);
        if (Error > 0.0f)   // too far apart — pull together
        {
            const float CorrectionA = A.Fixed ? 0.0f : (B.Fixed ? Error : Error * 0.5f);
            const float CorrectionB = B.Fixed ? 0.0f : (A.Fixed ? Error : Error * 0.5f);
            if (!A.Fixed) { A.Position.x += DirX * CorrectionA; A.Position.y += DirY * CorrectionA; }
            if (!B.Fixed) { B.Position.x -= DirX * CorrectionB; B.Position.y -= DirY * CorrectionB; }
        }
        else if (Error < 0.0f)   // too close — push apart
        {
            const float Need         = -Error;
            const float CorrectionA = A.Fixed ? 0.0f : (B.Fixed ? Need : Need * 0.5f);
            const float CorrectionB = B.Fixed ? 0.0f : (A.Fixed ? Need : Need * 0.5f);
            if (!A.Fixed) { A.Position.x -= DirX * CorrectionA; A.Position.y -= DirY * CorrectionA; }
            if (!B.Fixed) { B.Position.x += DirX * CorrectionB; B.Position.y += DirY * CorrectionB; }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          POINT POOL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Build the solve-time pool from the store: one raw slot per point-driven shape's point, welded so raw slots at the same world
    //    location (within tolerance) share one pooled point — this satisfies Coincident + the draw-time welds by construction. A pooled
    //    point inherits Fixed from a Fixed constraint on any of its welded handles, or from its shape being locked.
    ParametricSketchPointPool ConstructPointPool(const ParametricSketchShapeStore& Store)
    {
        ParametricSketchPointPool Pool;

        // 1) One raw slot per point of every point-driven shape, PLUS one centre slot per round family — a tangent / coincident on a
        //    circle / arc / ellipse must be able to MOVE the round centre, so it pools exactly like an ordinary point. The round
        //    centre slot carries the CentrePointIndex (-1) marker so LocateRawSlot can address it.
        for (const ParametricSketchShape& Shape : Store.Shapes)
        {
            if (PointDrivenCategory(Shape.Category))
            {
                for (int Index = 0; Index < (int)Shape.Points.size(); ++Index)
                {
                    Pool.RawShapeId.push_back(Shape.Identifier);
                    Pool.RawPointIndex.push_back(Index);
                }
            }
            else
            {
                Pool.RawShapeId.push_back(Shape.Identifier);
                Pool.RawPointIndex.push_back(CentrePointIndex);
            }
        }

        const size_t RawCount = Pool.RawShapeId.size();
        Pool.RawToPool.assign(RawCount, -1);

        // 2) Weld raw slots at the same world position into one pooled point (proximity within WeldWorld).
        for (size_t Raw = 0; Raw < RawCount; ++Raw)
        {
            const ParametricSketchShape* Shape = nullptr;
            for (const ParametricSketchShape& Entry : Store.Shapes)
                if (Entry.Identifier == Pool.RawShapeId[Raw])
                {
                    Shape = &Entry;
                    break;
                }
            if (!Shape || Pool.RawPointIndex[Raw] >= (int)Shape->Points.size())
            {
                // A stale slot (should not happen); give it its own dead pooled point so indices stay aligned.
                Pool.RawToPool[Raw] = (int)Pool.Points.size();
                Pool.Points.push_back(PooledPoint());
                Pool.ShapeOfSlot.push_back(Pool.RawShapeId[Raw]);
                continue;
            }

            // A round centre slot reads the solved Centre scalar; every other slot reads its defining point.
            const ImVec2 World = (Pool.RawPointIndex[Raw] < 0) ? Shape->Centre : Shape->Points[Pool.RawPointIndex[Raw]];

            int Matched = -1;
            for (size_t Slot = 0; Slot < Pool.Points.size(); ++Slot)
            {
                const ImVec2 Existing = Pool.Points[Slot].Position;
                if (std::fabs(Existing.x - World.x) <= WeldWorld && std::fabs(Existing.y - World.y) <= WeldWorld)
                {
                    Matched = (int)Slot;
                    break;
                }
            }

            if (Matched >= 0)
            {
                Pool.RawToPool[Raw] = Matched;
            }
            else
            {
                PooledPoint Fresh;
                Fresh.Position = World;
                Fresh.Fixed    = ParametricSketchShapeFrozen(*Shape);   // a locked shape OR a driven mirror child pins all its points
                Pool.RawToPool[Raw] = (int)Pool.Points.size();
                Pool.Points.push_back(Fresh);
                Pool.ShapeOfSlot.push_back(Pool.RawShapeId[Raw]);
            }
        }

        // 3) Apply Fixed constraints — pin the pooled point their point handle resolves to.
        for (const ParametricSketchConstraint& Constraint : Store.Constraints)
        {
            if (Constraint.Category != ParametricSketchConstraintCategory::Fixed)
                continue;
            const int Slot = LocatePool(Pool, Constraint.PrimaryA);
            if (Slot >= 0)
                Pool.Points[Slot].Fixed = true;
        }

        // 4) Coincident constraints are a HARD weld — merge their two pooled points into one regardless of separation, so the rule
        //    holds even when the points were authored apart (the proximity weld above only covers draw-time coincidences). The merge
        //    rewrites every raw slot that pointed at the second pooled point to the first, unions the Fixed pins, and seats the pair
        //    at the fixed partner's position when one side is pinned. Merged slots go dead but stay in the vector so indices align.
        for (const ParametricSketchConstraint& Constraint : Store.Constraints)
        {
            if (Constraint.Category != ParametricSketchConstraintCategory::Coincident)
                continue;
            const int SlotA = LocatePool(Pool, Constraint.PrimaryA);
            const int SlotB = LocatePool(Pool, Constraint.PrimaryB);
            if (SlotA < 0 || SlotB < 0 || SlotA == SlotB)
                continue;
            if (Pool.Points[SlotB].Fixed && !Pool.Points[SlotA].Fixed)
                Pool.Points[SlotA].Position = Pool.Points[SlotB].Position;
            Pool.Points[SlotA].Fixed = Pool.Points[SlotA].Fixed || Pool.Points[SlotB].Fixed;
            for (size_t Raw = 0; Raw < Pool.RawToPool.size(); ++Raw)
                if (Pool.RawToPool[Raw] == (uint32_t)SlotB)
                    Pool.RawToPool[Raw] = (uint32_t)SlotA;
        }

        return Pool;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

ParametricSketchSolveOutcome SolveParametricSketchSketch(ParametricSketchShapeStore& Store)
{
    if (Store.Constraints.empty() && Store.Dimensions.empty())
        return ParametricSketchSolveOutcome{ ParametricSketchSolveCategory::Settled, 0, 0.0f };

    ParametricSketchPointPool Pool = ConstructPointPool(Store);

    // 📝 Relaxation: constraints then dimensions per iteration, until the largest single-point move settles below the tolerance or the
    //    hard cap is hit. The residual is the only honest convergence probe — the old fixed-60 port neither knew it had settled nor
    //    reported when it had not (a conflicting constraint / dimension pair flip-flopped silently forever). The outcome reports which
    //    way the run ended; the write-back below still keeps the best-effort positions either way.
    ParametricSketchSolveOutcome Outcome;
    Outcome.Category = ParametricSketchSolveCategory::Settled;
    std::vector<ImVec2> BeforePositions(Pool.Points.size());
    for (int Iteration = 0; Iteration < MaxSolveIterations; ++Iteration)
    {
        for (size_t Slot = 0; Slot < Pool.Points.size(); ++Slot)
            BeforePositions[Slot] = Pool.Points[Slot].Position;
        // ── constraints ──
        for (const ParametricSketchConstraint& Constraint : Store.Constraints)
        {
            switch (Constraint.Category)
            {
                case ParametricSketchConstraintCategory::Horizontal:
                {
                    const int A = LocatePool(Pool, Constraint.PrimaryA);
                    const int B = LocatePool(Pool, Constraint.PrimaryB);
                    if (A < 0 || B < 0)
                        break;
                    const float MidY = (Pool.Points[A].Position.y + Pool.Points[B].Position.y) / 2.0f;
                    MoveTo(Pool.Points[A], Pool.Points[A].Position.x, MidY);
                    MoveTo(Pool.Points[B], Pool.Points[B].Position.x, MidY);
                    break;
                }
                case ParametricSketchConstraintCategory::Vertical:
                {
                    const int A = LocatePool(Pool, Constraint.PrimaryA);
                    const int B = LocatePool(Pool, Constraint.PrimaryB);
                    if (A < 0 || B < 0)
                        break;
                    const float MidX = (Pool.Points[A].Position.x + Pool.Points[B].Position.x) / 2.0f;
                    MoveTo(Pool.Points[A], MidX, Pool.Points[A].Position.y);
                    MoveTo(Pool.Points[B], MidX, Pool.Points[B].Position.y);
                    break;
                }
                case ParametricSketchConstraintCategory::Parallel:
                case ParametricSketchConstraintCategory::Perpendicular:
                {
                    const int E1A = LocatePool(Pool, Constraint.PrimaryA);
                    const int E1B = LocatePool(Pool, Constraint.PrimaryB);
                    const int E2A = LocatePool(Pool, Constraint.SecondaryA);
                    const int E2B = LocatePool(Pool, Constraint.SecondaryB);
                    if (E1A < 0 || E1B < 0 || E2A < 0 || E2B < 0)
                        break;
                    const float Offset = (Constraint.Category == ParametricSketchConstraintCategory::Perpendicular) ? Pi / 2.0f : 0.0f;
                    AlignAngle(Pool.Points[E1A], Pool.Points[E1B], Pool.Points[E2A], Pool.Points[E2B], Offset);
                    break;
                }
                case ParametricSketchConstraintCategory::Tangent:
                {
                    // Real tangency: two straight edges are tangent when parallel; an edge touching a round shape sits Radius from its
                    // centre; two round shapes sit R₁ + R₂ apart. The round side resolves through its pooled centre slot.
                    TangentOperand S1, S2;
                    if (!ResolveTangentSide(Store, Pool, Constraint.PrimaryA, Constraint.PrimaryB, S1)) break;
                    if (!ResolveTangentSide(Store, Pool, Constraint.SecondaryA, Constraint.SecondaryB, S2)) break;
                    if (S1.RoundEnabled && S2.RoundEnabled)
                        EnforceCircleCircleTangent(Pool.Points, S1.CentreSlot, S2.CentreSlot, S1.Radius, S2.Radius);
                    else if (S1.RoundEnabled)
                        EnforceLineCircleTangent(Pool.Points, S2.LineSlotA, S2.LineSlotB, S1.CentreSlot, S1.Radius);
                    else if (S2.RoundEnabled)
                        EnforceLineCircleTangent(Pool.Points, S1.LineSlotA, S1.LineSlotB, S2.CentreSlot, S2.Radius);
                    else
                        AlignAngle(Pool.Points[S1.LineSlotA], Pool.Points[S1.LineSlotB],
                                   Pool.Points[S2.LineSlotA], Pool.Points[S2.LineSlotB], 0.0f);   // two edges: tangent ⇔ parallel
                    break;
                }
                case ParametricSketchConstraintCategory::Equal:
                {
                    const int E1A = LocatePool(Pool, Constraint.PrimaryA);
                    const int E1B = LocatePool(Pool, Constraint.PrimaryB);
                    const int E2A = LocatePool(Pool, Constraint.SecondaryA);
                    const int E2B = LocatePool(Pool, Constraint.SecondaryB);
                    if (E1A < 0 || E1B < 0 || E2A < 0 || E2B < 0)
                        break;
                    EnforceEqualLength(Pool.Points[E1A], Pool.Points[E1B], Pool.Points[E2A], Pool.Points[E2B]);
                    break;
                }
                case ParametricSketchConstraintCategory::Coincident:   // satisfied by the hard weld at pool construction (step 4)
                case ParametricSketchConstraintCategory::Fixed:        // applied once, at pool construction
                default:
                    break;
            }
        }

        // ── dimensions ──
        for (const ParametricSketchDimension& Dimension : Store.Dimensions)
        {
            switch (Dimension.Category)
            {
                case ParametricSketchDimensionCategory::Radius:
                case ParametricSketchDimensionCategory::Diameter:
                    break;   // round families write their scalar directly in the write-back (never relaxed as points)
                case ParametricSketchDimensionCategory::Angular:
                {
                    const int E1A = LocatePool(Pool, Dimension.PrimaryA);
                    const int E1B = LocatePool(Pool, Dimension.PrimaryB);
                    const int E2A = LocatePool(Pool, Dimension.SecondaryA);
                    const int E2B = LocatePool(Pool, Dimension.SecondaryB);
                    if (E1A < 0 || E1B < 0 || E2A < 0 || E2B < 0)
                        break;
                    SetEdgeAngle(Pool.Points[E1A], Pool.Points[E1B], Pool.Points[E2A], Pool.Points[E2B], Dimension.TargetValue);
                    break;
                }
                case ParametricSketchDimensionCategory::Horizontal:
                {
                    const int A = LocatePool(Pool, Dimension.PrimaryA);
                    const int B = LocatePool(Pool, Dimension.PrimaryB);
                    if (A < 0 || B < 0)
                        break;
                    EnforceComponent(Pool.Points[A], Pool.Points[B], Dimension.TargetValue, 0);
                    break;
                }
                case ParametricSketchDimensionCategory::Vertical:
                {
                    const int A = LocatePool(Pool, Dimension.PrimaryA);
                    const int B = LocatePool(Pool, Dimension.PrimaryB);
                    if (A < 0 || B < 0)
                        break;
                    EnforceComponent(Pool.Points[A], Pool.Points[B], Dimension.TargetValue, 1);
                    break;
                }
                case ParametricSketchDimensionCategory::Aligned:
                default:
                {
                    const int A = LocatePool(Pool, Dimension.PrimaryA);
                    const int B = LocatePool(Pool, Dimension.PrimaryB);
                    if (A < 0 || B < 0)
                        break;
                    EnforceDistance(Pool.Points[A], Pool.Points[B], Dimension.TargetValue);
                    break;
                }
            }
        }

        // Residual: the largest single-point move this iteration. Below the tolerance = settled; the cap running out instead = a
        // constraint / dimension conflict the relaxation cannot satisfy (reported, not swallowed).
        float MaxMove = 0.0f;
        for (size_t Slot = 0; Slot < Pool.Points.size(); ++Slot)
        {
            MaxMove = std::max(MaxMove, std::fabs(Pool.Points[Slot].Position.x - BeforePositions[Slot].x));
            MaxMove = std::max(MaxMove, std::fabs(Pool.Points[Slot].Position.y - BeforePositions[Slot].y));
        }
        Outcome.IterationsUsed = Iteration + 1;
        Outcome.FinalResidual  = MaxMove;
        if (MaxMove <= SolveResidualTolerance)
        {
            Outcome.Category = ParametricSketchSolveCategory::Settled;
            break;
        }
        Outcome.Category = ParametricSketchSolveCategory::Unsettled;
    }

    // ── write-back: copy the solved pooled positions into each point-driven shape, then re-derive its scalars + cache ──
    for (ParametricSketchShape& Shape : Store.Shapes)
    {
        if (!PointDrivenCategory(Shape.Category) || ParametricSketchShapeFrozen(Shape))
            continue;
        bool Touched = false;
        std::vector<ImVec2> Points = Shape.Points;
        for (int Index = 0; Index < (int)Points.size(); ++Index)
        {
            ParametricSketchPointHandle Handle;
            Handle.ShapeIdentifier = Shape.Identifier;
            Handle.PointIndex      = Index;
            const int Slot = LocatePool(Pool, Handle);
            if (Slot < 0)
                continue;
            const ImVec2 Solved = Pool.Points[Slot].Position;
            if (std::fabs(Solved.x - Points[Index].x) > 1e-5f || std::fabs(Solved.y - Points[Index].y) > 1e-5f)
                Touched = true;
            Points[Index] = Solved;
        }
        if (Touched)
        {
            if (Shape.Category == ParametricSketchShapeCategory::Rectangle)
            {
                // A rectangle's four corners are FREE once a dimension / constraint moves them — write them back verbatim so a single
                //    edge can shorten into a trapezoid, exactly like p4 relaxes its corner points. Routing through ConstructParametricSketchShape
                //    would take the axis-aligned bounding box of the four corners and RE-SQUARE the box (dragging the opposite parallel
                //    edge too). The outline flattens straight from Points (Line / Polyline / Rectangle carry vertices verbatim), so the
                //    trapezoid renders + picks correctly; we only need to invalidate the memoised outline so it re-tessellates.
                Shape.Points             = Points;
                Shape.ClosedEnabled      = true;
                Shape.CachedOutlineValid = false;
            }
            else
            {
                const uint32_t KeepIdentifier   = Shape.Identifier;
                const uint32_t KeepTint         = Shape.TintIndex;
                const uint32_t KeepFolder       = Shape.FolderIdentifier;
                const bool     KeepDisplayed    = Shape.Displayed;
                const bool     KeepFill         = Shape.FillEnabled;
                const bool     KeepLock         = Shape.LockEnabled;
                char           KeepTitle[48];
                std::snprintf(KeepTitle, sizeof(KeepTitle), "%s", Shape.Title);

                ParametricSketchShape Rebuilt = ConstructParametricSketchShape(Shape.Category, Points, Shape.SideCount, Shape.Rho, Shape.Degree);
                Rebuilt.Identifier       = KeepIdentifier;
                Rebuilt.TintIndex        = KeepTint;
                Rebuilt.FolderIdentifier = KeepFolder;
                Rebuilt.Displayed        = KeepDisplayed;
                Rebuilt.FillEnabled      = KeepFill;
                Rebuilt.LockEnabled      = KeepLock;
                std::snprintf(Rebuilt.Title, sizeof(Rebuilt.Title), "%s", KeepTitle);
                Shape = Rebuilt;
            }
        }
    }

    // ── write-back: a round family whose pooled centre moved translates its defining points + centre together ──
    //    A translation changes nothing intrinsic (radius / angles / axes stay put), so the scalars are updated in place and only the
    //    flatten cache is invalidated — no rebuild, so the shape keeps its elevation / extrude / fillet edits untouched.
    for (ParametricSketchShape& Shape : Store.Shapes)
    {
        if (PointDrivenCategory(Shape.Category) || ParametricSketchShapeFrozen(Shape))
            continue;
        ParametricSketchPointHandle CentreHandle;
        CentreHandle.ShapeIdentifier = Shape.Identifier;
        CentreHandle.PointIndex      = CentrePointIndex;
        const int Slot = LocatePool(Pool, CentreHandle);
        if (Slot < 0)
            continue;
        const float DeltaX = Pool.Points[Slot].Position.x - Shape.Centre.x;
        const float DeltaY = Pool.Points[Slot].Position.y - Shape.Centre.y;
        if (std::fabs(DeltaX) < 1e-5f && std::fabs(DeltaY) < 1e-5f)
            continue;
        for (ImVec2& Point : Shape.Points)
        {
            Point.x += DeltaX;
            Point.y += DeltaY;
        }
        Shape.Centre.x += DeltaX;
        Shape.Centre.y += DeltaY;
        Shape.CachedOutlineValid = false;
    }

    // ── round dimensions: write the analytic scalar directly (Radius / Diameter) ──
    for (const ParametricSketchDimension& Dimension : Store.Dimensions)
    {
        if (Dimension.Category != ParametricSketchDimensionCategory::Radius && Dimension.Category != ParametricSketchDimensionCategory::Diameter)
            continue;
        ParametricSketchShape* Shape = ResolveParametricSketchShape(Store, Dimension.PrimaryA.ShapeIdentifier);
        if (!Shape || ParametricSketchShapeFrozen(*Shape))
            continue;
        const float Radius = (Dimension.Category == ParametricSketchDimensionCategory::Diameter) ? Dimension.TargetValue / 2.0f
                                                                                        : Dimension.TargetValue;
        if (Shape->Category == ParametricSketchShapeCategory::Ellipse)
            EnforceEllipseAxes(*Shape, Radius, Shape->MinorAxis);
        else
            EnforceShapeRadius(*Shape, Radius);
    }

    AppendParametricSketchEdit(Store, 0, "Solved sketch", "ruler");

    return Outcome;
}

int ResolveDegreesOfFreedom(const ParametricSketchShapeStore& Store)
{
    // 📝 DOF counts over WELDED GROUPS, not raw handles: build the same pool the solver relaxes (proximity weld + Coincident hard
    //    weld + Fixed pins + round centres) and count the pooled points that are free. A Coincident pair is ONE pooled point (the
    //    merge already removed the two raw DOFs), so Coincident is not subtracted again; a Fixed point is excluded by the pool count
    //    rather than subtracted again. Only the relative rules (Horizontal / Vertical / Parallel / Perpendicular / Equal / Tangent)
    //    and the driving dimensions each remove one DOF.
    const ParametricSketchPointPool Pool = ConstructPointPool(Store);

    int FreePooledDoubled = 0;
    for (const PooledPoint& Point : Pool.Points)
        if (!Point.Fixed)
            FreePooledDoubled += 2;

    int ReducingConstraintCount = 0;
    for (const ParametricSketchConstraint& Constraint : Store.Constraints)
        if (Constraint.Category != ParametricSketchConstraintCategory::Coincident &&
            Constraint.Category != ParametricSketchConstraintCategory::Fixed)
            ++ReducingConstraintCount;

    return FreePooledDoubled - (int)Store.Dimensions.size() - ReducingConstraintCount;
}

bool ResolvePointFixed(const ParametricSketchShapeStore& Store, ParametricSketchPointHandle Handle)
{
    if (Handle.ShapeIdentifier == 0)
        return false;

    // A locked shape OR a driven mirror child pins all its points.
    for (const ParametricSketchShape& Shape : Store.Shapes)
        if (Shape.Identifier == Handle.ShapeIdentifier)
        {
            if (ParametricSketchShapeFrozen(Shape))
                return true;
            break;
        }

    for (const ParametricSketchConstraint& Constraint : Store.Constraints)
        if (Constraint.Category == ParametricSketchConstraintCategory::Fixed &&
            Constraint.PrimaryA.ShapeIdentifier == Handle.ShapeIdentifier &&
            Constraint.PrimaryA.PointIndex      == Handle.PointIndex)
            return true;
    return false;
}

} // namespace Frontier
