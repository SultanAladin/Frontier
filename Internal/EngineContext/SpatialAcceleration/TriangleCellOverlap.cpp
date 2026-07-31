/*==============================================================================================================================================
                                                         TRIANGLECELLOVERLAP.CPP
==============================================================================================================================================*/
// 🧩 The Schwarz & Seidel setup/test-split triangle-cell overlap predicate and the mesh sweep over it. See the header for why an AABB is not
//    an acceptable substitute and what 26- versus 6-separating buys.

#include "EngineContext/SpatialAcceleration/TriangleCellOverlap.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Frontier
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

// Transform a local position by a column-major 4x4 (translation in elements 12..14), assuming an affine transform (no projective row).
Vector3f TransformWorldPosition(const float Model[16], float LocalX, float LocalY, float LocalZ)
{
    return Vector3f{
        Model[0] * LocalX + Model[4] * LocalY + Model[8]  * LocalZ + Model[12],
        Model[1] * LocalX + Model[5] * LocalY + Model[9]  * LocalZ + Model[13],
        Model[2] * LocalX + Model[6] * LocalY + Model[10] * LocalZ + Model[14]
    };
}

// One projection-plane block of the edge-cross setup. Given the triangle's three corners projected onto a plane (as (First, Second) pairs) and
// the sign of the triangle normal's component along that plane's third axis, build the three outward 2D edge normals and their offsets.
//
// 📝 The NormalAxisSign factor is what makes the test orientation-independent: it flips the edge normals so they always point AWAY from the
//    triangle interior, whichever winding the source mesh uses. Without it a clockwise triangle rejects every cell.
void ConfigureEdgePlane(const float ProjectedFirst[3],
                        const float ProjectedSecond[3],
                        float       NormalAxisComponent,
                        float       CellMetres,
                        float       EdgeNormal[3][2],
                        float       EdgeOffset[3])
{
    const float Orientation = (NormalAxisComponent >= 0.0f) ? 1.0f : -1.0f;

    for (uint32_t Edge = 0; Edge < 3; ++Edge)
    {
        const uint32_t Next = (Edge + 1) % 3;

        const float DeltaFirst  = ProjectedFirst[Next]  - ProjectedFirst[Edge];
        const float DeltaSecond = ProjectedSecond[Next] - ProjectedSecond[Edge];

        // Outward normal of the edge within the projection plane, oriented by the triangle's facing.
        EdgeNormal[Edge][0] = -DeltaSecond * Orientation;
        EdgeNormal[Edge][1] =  DeltaFirst  * Orientation;

        // Constant term, with the cell's extent already folded in: the largest positive contribution a cell of this size can add is the sum of
        // the positive normal components times the edge length, so adding it here makes the inner test a bare compare against zero.
        const float CellContribution = CellMetres * (std::max(0.0f, EdgeNormal[Edge][0]) +
                                                     std::max(0.0f, EdgeNormal[Edge][1]));

        EdgeOffset[Edge] = -(EdgeNormal[Edge][0] * ProjectedFirst[Edge] +
                             EdgeNormal[Edge][1] * ProjectedSecond[Edge]) + CellContribution;
    }
}

// Evaluate one projection-plane block: all three edge functions at the cell's minimum corner. A negative result on any edge is a separating
// axis, so the cell misses the triangle.
bool EvaluateEdgePlane(const float EdgeNormal[3][2],
                       const float EdgeOffset[3],
                       float       CellFirst,
                       float       CellSecond)
{
    for (uint32_t Edge = 0; Edge < 3; ++Edge)
    {
        const float EdgeExtent = EdgeNormal[Edge][0] * CellFirst +
                                 EdgeNormal[Edge][1] * CellSecond + EdgeOffset[Edge];
        if (EdgeExtent < 0.0f)
            return false;
    }
    return true;
}

// Hash a world cell so one sweep can deduplicate the cells many triangles share. Cheap 64-bit mix of the three signed axes.
struct CellCoordinateHash
{
    size_t operator()(const CellCoordinate& Cell) const noexcept
    {
        const uint64_t PackedX = (uint64_t)(uint32_t)Cell.XCell;
        const uint64_t PackedY = (uint64_t)(uint32_t)Cell.YCell;
        const uint64_t PackedZ = (uint64_t)(uint32_t)Cell.ZCell;
        uint64_t Mixed = PackedX * 0x9E3779B97F4A7C15ull;
        Mixed ^= (PackedY + 0x9E3779B97F4A7C15ull + (Mixed << 6) + (Mixed >> 2));
        Mixed ^= (PackedZ + 0x9E3779B97F4A7C15ull + (Mixed << 6) + (Mixed >> 2));
        return (size_t)Mixed;
    }
};

struct CellCoordinateMatch
{
    bool operator()(const CellCoordinate& Left, const CellCoordinate& Right) const noexcept
    {
        return Left.XCell == Right.XCell && Left.YCell == Right.YCell && Left.ZCell == Right.ZCell;
    }
};

// Floor a world coordinate to a cell index at the given cell size. Matches ResolveCameraCell's convention so a voxelized cell and a camera cell
// address the same lattice.
int32_t FloorWorldCell(float WorldCoordinate, float CellMetres)
{
    return (int32_t)std::floor(WorldCoordinate / CellMetres);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

TriangleCellOverlapSetup ConfigureTriangleCellOverlap(Vector3f              VertexA,
                                                     Vector3f              VertexB,
                                                     Vector3f              VertexC,
                                                     float                 CellMetres,
                                                     CellOverlapSeparation Separation)
{
    TriangleCellOverlapSetup Setup;
    Setup.VertexA    = VertexA;
    Setup.VertexB    = VertexB;
    Setup.VertexC    = VertexC;
    Setup.CellExtent = CellMetres;

    // -- Triangle bounds. These give the candidate cell box, and are the whole answer for a degenerate triangle. --------------------------
    Setup.BoundMinimum = Vector3f{ std::min({ VertexA.XCoord, VertexB.XCoord, VertexC.XCoord }),
                                   std::min({ VertexA.YCoord, VertexB.YCoord, VertexC.YCoord }),
                                   std::min({ VertexA.ZCoord, VertexB.ZCoord, VertexC.ZCoord }) };
    Setup.BoundMaximum = Vector3f{ std::max({ VertexA.XCoord, VertexB.XCoord, VertexC.XCoord }),
                                   std::max({ VertexA.YCoord, VertexB.YCoord, VertexC.YCoord }),
                                   std::max({ VertexA.ZCoord, VertexB.ZCoord, VertexC.ZCoord }) };

    // -- Triangle plane. Left unnormalized: every test using it compares two quantities that scale together. ------------------------------
    const float EdgeBAx = VertexB.XCoord - VertexA.XCoord;
    const float EdgeBAy = VertexB.YCoord - VertexA.YCoord;
    const float EdgeBAz = VertexB.ZCoord - VertexA.ZCoord;
    const float EdgeCAx = VertexC.XCoord - VertexA.XCoord;
    const float EdgeCAy = VertexC.YCoord - VertexA.YCoord;
    const float EdgeCAz = VertexC.ZCoord - VertexA.ZCoord;

    Setup.PlaneNormal = Vector3f{ EdgeBAy * EdgeCAz - EdgeBAz * EdgeCAy,
                                  EdgeBAz * EdgeCAx - EdgeBAx * EdgeCAz,
                                  EdgeBAx * EdgeCAy - EdgeBAy * EdgeCAx };

    const float NormalMagnitudeSquared = Setup.PlaneNormal.XCoord * Setup.PlaneNormal.XCoord +
                                         Setup.PlaneNormal.YCoord * Setup.PlaneNormal.YCoord +
                                         Setup.PlaneNormal.ZCoord * Setup.PlaneNormal.ZCoord;

    // A vanished normal means the three corners are collinear — there is no plane to test against, so the predicate falls back to bounds.
    if (!(NormalMagnitudeSquared > 0.0f))
    {
        Setup.DegenerateCondition = true;
        return Setup;
    }

    Setup.PlaneOffset = Setup.PlaneNormal.XCoord * VertexA.XCoord +
                        Setup.PlaneNormal.YCoord * VertexA.YCoord +
                        Setup.PlaneNormal.ZCoord * VertexA.ZCoord;

    // The band the plane may occupy while still cutting the cell. Supercover uses the cell's full projected diagonal (half-extent summed over
    // the three axes); the thin variant tightens it to half a cell edge, which is what sheds the 26-42% of grazing cells.
    const float NormalSpan = std::fabs(Setup.PlaneNormal.XCoord) +
                             std::fabs(Setup.PlaneNormal.YCoord) +
                             std::fabs(Setup.PlaneNormal.ZCoord);

    if (Separation == CellOverlapSeparation::Thin6)
    {
        const float LargestComponent = std::max({ std::fabs(Setup.PlaneNormal.XCoord),
                                                  std::fabs(Setup.PlaneNormal.YCoord),
                                                  std::fabs(Setup.PlaneNormal.ZCoord) });
        Setup.PlaneCriticalSpan = 0.5f * CellMetres * LargestComponent;
    }
    else
    {
        Setup.PlaneCriticalSpan = 0.5f * CellMetres * NormalSpan;
    }

    // -- The nine edge-cross axes, factored into three projection planes. -----------------------------------------------------------------
    const float ProjectedX[3] = { VertexA.XCoord, VertexB.XCoord, VertexC.XCoord };
    const float ProjectedY[3] = { VertexA.YCoord, VertexB.YCoord, VertexC.YCoord };
    const float ProjectedZ[3] = { VertexA.ZCoord, VertexB.ZCoord, VertexC.ZCoord };

    ConfigureEdgePlane(ProjectedX, ProjectedY, Setup.PlaneNormal.ZCoord, CellMetres, Setup.EdgeNormalXY, Setup.EdgeOffsetXY);
    ConfigureEdgePlane(ProjectedY, ProjectedZ, Setup.PlaneNormal.XCoord, CellMetres, Setup.EdgeNormalYZ, Setup.EdgeOffsetYZ);
    ConfigureEdgePlane(ProjectedZ, ProjectedX, Setup.PlaneNormal.YCoord, CellMetres, Setup.EdgeNormalZX, Setup.EdgeOffsetZX);

    return Setup;
}

bool EvaluateCellOverlap(const TriangleCellOverlapSetup& Setup, Vector3f CellMinimum)
{
    const float CellExtent = Setup.CellExtent;

    // -- Axes 1-3: the cell's own face normals. An interval overlap per axis against the triangle's bounds. ---------------------------------
    if (Setup.BoundMinimum.XCoord > CellMinimum.XCoord + CellExtent || Setup.BoundMaximum.XCoord < CellMinimum.XCoord ||
        Setup.BoundMinimum.YCoord > CellMinimum.YCoord + CellExtent || Setup.BoundMaximum.YCoord < CellMinimum.YCoord ||
        Setup.BoundMinimum.ZCoord > CellMinimum.ZCoord + CellExtent || Setup.BoundMaximum.ZCoord < CellMinimum.ZCoord)
        return false;

    // A degenerate (zero-area) triangle has no plane and no meaningful edge normals; the bounds overlap above is the whole verdict.
    if (Setup.DegenerateCondition)
        return true;

    // -- Axis 4: the triangle's plane against the cell centre, within the critical band. ---------------------------------------------------
    const float CentreDistance = Setup.PlaneNormal.XCoord * (CellMinimum.XCoord + 0.5f * CellExtent) +
                                 Setup.PlaneNormal.YCoord * (CellMinimum.YCoord + 0.5f * CellExtent) +
                                 Setup.PlaneNormal.ZCoord * (CellMinimum.ZCoord + 0.5f * CellExtent) -
                                 Setup.PlaneOffset;
    if (std::fabs(CentreDistance) > Setup.PlaneCriticalSpan)
        return false;

    // -- Axes 5-13: the nine edge-cross axes, as three 2D edge-function triples at the cell's minimum corner. ------------------------------
    if (!EvaluateEdgePlane(Setup.EdgeNormalXY, Setup.EdgeOffsetXY, CellMinimum.XCoord, CellMinimum.YCoord))
        return false;
    if (!EvaluateEdgePlane(Setup.EdgeNormalYZ, Setup.EdgeOffsetYZ, CellMinimum.YCoord, CellMinimum.ZCoord))
        return false;
    if (!EvaluateEdgePlane(Setup.EdgeNormalZX, Setup.EdgeOffsetZX, CellMinimum.ZCoord, CellMinimum.XCoord))
        return false;

    return true;
}

CellOverlapOutcome VoxelizeTriangleStream(const float*                 PositionStream,
                                          uint32_t                     PositionStride,
                                          uint32_t                     VertexCount,
                                          const uint32_t*              IndexStream,
                                          uint32_t                     IndexCount,
                                          const float                  Model[16],
                                          const ToroidalClipmapField&  Field,
                                          uint32_t                     Level,
                                          CellOverlapSeparation        Separation,
                                          uint32_t                     CellBudget,
                                          std::vector<CellCoordinate>& MarkedCells)
{
    CellOverlapOutcome Outcome;

    if (PositionStream == nullptr || IndexStream == nullptr || PositionStride < 3 || IndexCount < 3 ||
        Level >= Field.LevelCount || Level >= Field.Levels.size())
        return Outcome;

    const float CellMetres = Field.Levels[Level].CellMetres;
    if (!(CellMetres > 0.0f))
        return Outcome;

    std::unordered_set<CellCoordinate, CellCoordinateHash, CellCoordinateMatch> AlreadyMarked;

    const uint32_t TriangleCount = IndexCount / 3;
    for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
    {
        const uint32_t IndexA = IndexStream[Triangle * 3 + 0];
        const uint32_t IndexB = IndexStream[Triangle * 3 + 1];
        const uint32_t IndexC = IndexStream[Triangle * 3 + 2];
        if (IndexA >= VertexCount || IndexB >= VertexCount || IndexC >= VertexCount)
            continue;

        const float* LocalA = PositionStream + (size_t)IndexA * PositionStride;
        const float* LocalB = PositionStream + (size_t)IndexB * PositionStride;
        const float* LocalC = PositionStream + (size_t)IndexC * PositionStride;

        const Vector3f WorldA = TransformWorldPosition(Model, LocalA[0], LocalA[1], LocalA[2]);
        const Vector3f WorldB = TransformWorldPosition(Model, LocalB[0], LocalB[1], LocalB[2]);
        const Vector3f WorldC = TransformWorldPosition(Model, LocalC[0], LocalC[1], LocalC[2]);

        const TriangleCellOverlapSetup Setup =
            ConfigureTriangleCellOverlap(WorldA, WorldB, WorldC, CellMetres, Separation);

        if (Setup.DegenerateCondition)
            ++Outcome.DegenerateTriangleCount;

        // The coarse half of the two-level split: only cells inside THIS triangle's bounds are candidates. A scene-spanning triangle still
        // costs a large box, which is exactly why the budget below is not optional.
        const int32_t MinimumXCell = FloorWorldCell(Setup.BoundMinimum.XCoord, CellMetres);
        const int32_t MinimumYCell = FloorWorldCell(Setup.BoundMinimum.YCoord, CellMetres);
        const int32_t MinimumZCell = FloorWorldCell(Setup.BoundMinimum.ZCoord, CellMetres);
        const int32_t MaximumXCell = FloorWorldCell(Setup.BoundMaximum.XCoord, CellMetres);
        const int32_t MaximumYCell = FloorWorldCell(Setup.BoundMaximum.YCoord, CellMetres);
        const int32_t MaximumZCell = FloorWorldCell(Setup.BoundMaximum.ZCoord, CellMetres);

        for (int32_t ZCell = MinimumZCell; ZCell <= MaximumZCell; ++ZCell)
        for (int32_t YCell = MinimumYCell; YCell <= MaximumYCell; ++YCell)
        for (int32_t XCell = MinimumXCell; XCell <= MaximumXCell; ++XCell)
        {
            if (Outcome.TestedCellCount >= CellBudget)
            {
                // Count what is left unexamined across the whole remaining sweep so the shortfall is reported, not hidden.
                ++Outcome.RefusedCellCount;
                continue;
            }

            ++Outcome.TestedCellCount;

            const Vector3f CellMinimum{ (float)XCell * CellMetres,
                                        (float)YCell * CellMetres,
                                        (float)ZCell * CellMetres };

            if (!EvaluateCellOverlap(Setup, CellMinimum))
                continue;

            const CellCoordinate Marked{ XCell, YCell, ZCell };
            if (AlreadyMarked.insert(Marked).second)
            {
                MarkedCells.push_back(Marked);
                ++Outcome.MarkedCellCount;
            }
        }
    }

    return Outcome;
}

uint32_t ReduceCellsToOuterShell(std::vector<CellCoordinate>& MarkedCells)
{
    if (MarkedCells.size() < 7)
        return 0;   // fewer than seven cells cannot enclose one on all six sides

    const std::unordered_set<CellCoordinate, CellCoordinateHash, CellCoordinateMatch>
        Occupied(MarkedCells.begin(), MarkedCells.end());

    const int32_t NeighbourOffset[6][3] = { { 1, 0, 0 }, { -1, 0, 0 },
                                           { 0, 1, 0 }, { 0, -1, 0 },
                                           { 0, 0, 1 }, { 0, 0, -1 } };

    // Compact in place: a kept cell is written forward over the discarded ones, so no second container is allocated.
    size_t KeptCount = 0;
    for (const CellCoordinate& Cell : MarkedCells)
    {
        bool ExposedCondition = false;
        for (uint32_t Direction = 0; Direction < 6 && !ExposedCondition; ++Direction)
        {
            const CellCoordinate Neighbour{ Cell.XCell + NeighbourOffset[Direction][0],
                                            Cell.YCell + NeighbourOffset[Direction][1],
                                            Cell.ZCell + NeighbourOffset[Direction][2] };
            ExposedCondition = (Occupied.find(Neighbour) == Occupied.end());
        }

        if (ExposedCondition)
            MarkedCells[KeptCount++] = Cell;
    }

    const uint32_t RemovedCount = (uint32_t)(MarkedCells.size() - KeptCount);
    MarkedCells.resize(KeptCount);
    return RemovedCount;
}

} // namespace Frontier
