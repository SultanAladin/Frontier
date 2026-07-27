/*==============================================================================================================================================
                                                         DRAUGHTCONSTRAINTSOLVER.CPP
==============================================================================================================================================*/
// 🧩 The parametric constraint solver: builds a shared point pool over the store's shapes, welds coincident handles, relaxes the free
//    points across 60 fixed iterations so the constraints + dimensions hold (faithful to p4.html solve()), then writes the solved
//    positions back + re-derives each touched shape's analytic scalars. Round dimensions write the scalar directly. Pure CPU + analytic.

#include "DraughtConstraintSolver.h"

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
    struct DraughtPointPool
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
    bool PointDrivenCategory(DraughtShapeCategory Category)
    {
        switch (Category)
        {
            case DraughtShapeCategory::Line:
            case DraughtShapeCategory::Polyline:
            case DraughtShapeCategory::Rectangle:
            case DraughtShapeCategory::Bezier:
            case DraughtShapeCategory::BSpline:
            case DraughtShapeCategory::Nurbs:
            case DraughtShapeCategory::Spline:
            case DraughtShapeCategory::Profile:
                return true;
            default:   // Arc / Circle / Ellipse / Polygon / Conic / Slot — scalar-driven
                return false;
        }
    }

    // 📝 Resolve the raw slot for a shape point-handle (before welding). -1 when the handle names a shape / index not in the pool.
    int LocateRawSlot(const DraughtPointPool& Pool, DraughtPointHandle Handle)
    {
        for (size_t Slot = 0; Slot < Pool.RawShapeId.size(); ++Slot)
            if (Pool.RawShapeId[Slot] == Handle.ShapeIdentifier && Pool.RawPointIndex[Slot] == Handle.PointIndex)
                return (int)Slot;
        return -1;
    }

    // 📝 Resolve the pooled-point index a shape point-handle addresses (-1 when absent). Constraints / dimensions relax through this.
    int LocatePool(const DraughtPointPool& Pool, DraughtPointHandle Handle)
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
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          POINT POOL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Build the solve-time pool from the store: one raw slot per point-driven shape's point, welded so raw slots at the same world
    //    location (within tolerance) share one pooled point — this satisfies Coincident + the draw-time welds by construction. A pooled
    //    point inherits Fixed from a Fixed constraint on any of its welded handles, or from its shape being locked.
    DraughtPointPool ConstructPointPool(const DraughtShapeStore& Store)
    {
        DraughtPointPool Pool;

        // 1) One raw slot per point of every point-driven, displayed shape.
        for (const DraughtShape& Shape : Store.Shapes)
        {
            if (!PointDrivenCategory(Shape.Category))
                continue;
            for (int Index = 0; Index < (int)Shape.Points.size(); ++Index)
            {
                Pool.RawShapeId.push_back(Shape.Identifier);
                Pool.RawPointIndex.push_back(Index);
            }
        }

        const size_t RawCount = Pool.RawShapeId.size();
        Pool.RawToPool.assign(RawCount, -1);

        // 2) Weld raw slots at the same world position into one pooled point (proximity within WeldWorld).
        for (size_t Raw = 0; Raw < RawCount; ++Raw)
        {
            const DraughtShape* Shape = nullptr;
            for (const DraughtShape& Entry : Store.Shapes)
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

            const ImVec2 World = Shape->Points[Pool.RawPointIndex[Raw]];

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
                Fresh.Fixed    = DraughtShapeFrozen(*Shape);   // a locked shape OR a driven mirror child pins all its points
                Pool.RawToPool[Raw] = (int)Pool.Points.size();
                Pool.Points.push_back(Fresh);
                Pool.ShapeOfSlot.push_back(Pool.RawShapeId[Raw]);
            }
        }

        // 3) Apply Fixed constraints — pin the pooled point their point handle resolves to.
        for (const DraughtConstraint& Constraint : Store.Constraints)
        {
            if (Constraint.Category != DraughtConstraintCategory::Fixed)
                continue;
            const int Slot = LocatePool(Pool, Constraint.PrimaryA);
            if (Slot >= 0)
                Pool.Points[Slot].Fixed = true;
        }

        return Pool;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void SolveDraughtSketch(DraughtShapeStore& Store)
{
    if (Store.Constraints.empty() && Store.Dimensions.empty())
        return;

    DraughtPointPool Pool = ConstructPointPool(Store);

    // 60 fixed relaxation iterations, constraints then dimensions per iteration (matches p4 solve()).
    constexpr int IterationCount = 60;
    for (int Iteration = 0; Iteration < IterationCount; ++Iteration)
    {
        // ── constraints ──
        for (const DraughtConstraint& Constraint : Store.Constraints)
        {
            switch (Constraint.Category)
            {
                case DraughtConstraintCategory::Horizontal:
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
                case DraughtConstraintCategory::Vertical:
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
                case DraughtConstraintCategory::Parallel:
                case DraughtConstraintCategory::Perpendicular:
                case DraughtConstraintCategory::Tangent:
                {
                    const int E1A = LocatePool(Pool, Constraint.PrimaryA);
                    const int E1B = LocatePool(Pool, Constraint.PrimaryB);
                    const int E2A = LocatePool(Pool, Constraint.SecondaryA);
                    const int E2B = LocatePool(Pool, Constraint.SecondaryB);
                    if (E1A < 0 || E1B < 0 || E2A < 0 || E2B < 0)
                        break;
                    const float Offset = (Constraint.Category == DraughtConstraintCategory::Perpendicular) ? Pi / 2.0f : 0.0f;
                    AlignAngle(Pool.Points[E1A], Pool.Points[E1B], Pool.Points[E2A], Pool.Points[E2B], Offset);
                    break;
                }
                case DraughtConstraintCategory::Equal:
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
                case DraughtConstraintCategory::Coincident:   // satisfied by the weld — the pooled point is already shared
                case DraughtConstraintCategory::Fixed:        // applied once, at pool construction
                default:
                    break;
            }
        }

        // ── dimensions ──
        for (const DraughtDimension& Dimension : Store.Dimensions)
        {
            switch (Dimension.Category)
            {
                case DraughtDimensionCategory::Radius:
                case DraughtDimensionCategory::Diameter:
                    break;   // round families write their scalar directly in the write-back (never relaxed as points)
                case DraughtDimensionCategory::Angular:
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
                case DraughtDimensionCategory::Horizontal:
                {
                    const int A = LocatePool(Pool, Dimension.PrimaryA);
                    const int B = LocatePool(Pool, Dimension.PrimaryB);
                    if (A < 0 || B < 0)
                        break;
                    EnforceComponent(Pool.Points[A], Pool.Points[B], Dimension.TargetValue, 0);
                    break;
                }
                case DraughtDimensionCategory::Vertical:
                {
                    const int A = LocatePool(Pool, Dimension.PrimaryA);
                    const int B = LocatePool(Pool, Dimension.PrimaryB);
                    if (A < 0 || B < 0)
                        break;
                    EnforceComponent(Pool.Points[A], Pool.Points[B], Dimension.TargetValue, 1);
                    break;
                }
                case DraughtDimensionCategory::Aligned:
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
    }

    // ── write-back: copy the solved pooled positions into each point-driven shape, then re-derive its scalars + cache ──
    for (DraughtShape& Shape : Store.Shapes)
    {
        if (!PointDrivenCategory(Shape.Category) || DraughtShapeFrozen(Shape))
            continue;
        bool Touched = false;
        std::vector<ImVec2> Points = Shape.Points;
        for (int Index = 0; Index < (int)Points.size(); ++Index)
        {
            DraughtPointHandle Handle;
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
            if (Shape.Category == DraughtShapeCategory::Rectangle)
            {
                // A rectangle's four corners are FREE once a dimension / constraint moves them — write them back verbatim so a single
                //    edge can shorten into a trapezoid, exactly like p4 relaxes its corner points. Routing through ConstructDraughtShape
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

                DraughtShape Rebuilt = ConstructDraughtShape(Shape.Category, Points, Shape.SideCount, Shape.Rho, Shape.Degree);
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

    // ── round dimensions: write the analytic scalar directly (Radius / Diameter) ──
    for (const DraughtDimension& Dimension : Store.Dimensions)
    {
        if (Dimension.Category != DraughtDimensionCategory::Radius && Dimension.Category != DraughtDimensionCategory::Diameter)
            continue;
        DraughtShape* Shape = ResolveDraughtShape(Store, Dimension.PrimaryA.ShapeIdentifier);
        if (!Shape || DraughtShapeFrozen(*Shape))
            continue;
        const float Radius = (Dimension.Category == DraughtDimensionCategory::Diameter) ? Dimension.TargetValue / 2.0f
                                                                                        : Dimension.TargetValue;
        if (Shape->Category == DraughtShapeCategory::Ellipse)
            EnforceEllipseAxes(*Shape, Radius, Shape->MinorAxis);
        else
            EnforceShapeRadius(*Shape, Radius);
    }

    AppendDraughtEdit(Store, 0, "Solved sketch", "ruler");
}

int ResolveDegreesOfFreedom(const DraughtShapeStore& Store)
{
    int FreePointDoubled = 0;
    for (const DraughtShape& Shape : Store.Shapes)
    {
        if (!PointDrivenCategory(Shape.Category) || DraughtShapeFrozen(Shape))
            continue;
        for (int Index = 0; Index < (int)Shape.Points.size(); ++Index)
        {
            DraughtPointHandle Handle;
            Handle.ShapeIdentifier = Shape.Identifier;
            Handle.PointIndex      = Index;
            if (!ResolvePointFixed(Store, Handle))
                FreePointDoubled += 2;
        }
    }
    return FreePointDoubled - (int)Store.Dimensions.size() - (int)Store.Constraints.size();
}

bool ResolvePointFixed(const DraughtShapeStore& Store, DraughtPointHandle Handle)
{
    if (Handle.ShapeIdentifier == 0)
        return false;

    // A locked shape OR a driven mirror child pins all its points.
    for (const DraughtShape& Shape : Store.Shapes)
        if (Shape.Identifier == Handle.ShapeIdentifier)
        {
            if (DraughtShapeFrozen(Shape))
                return true;
            break;
        }

    for (const DraughtConstraint& Constraint : Store.Constraints)
        if (Constraint.Category == DraughtConstraintCategory::Fixed &&
            Constraint.PrimaryA.ShapeIdentifier == Handle.ShapeIdentifier &&
            Constraint.PrimaryA.PointIndex      == Handle.PointIndex)
            return true;
    return false;
}

} // namespace Frontier
