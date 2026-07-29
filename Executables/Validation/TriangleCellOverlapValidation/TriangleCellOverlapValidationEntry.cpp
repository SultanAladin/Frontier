/*==============================================================================================================================================
                                                TRIANGLECELLOVERLAPVALIDATIONENTRY.CPP
==============================================================================================================================================*/
// 🧩 Correctness check for the exact triangle-cell overlap predicate and the mesh sweep over it. No window, no Vulkan — pure math asserted
//    against cases whose answers are known by construction. The predicate is a 13-axis separating-axis test whose failure modes are SUBTLE
//    (a wrong edge-normal orientation still marks plausible-looking cell counts), so each case below pins one specific way it could be wrong.
// 📝 The decisive case is CONCAVITY: an L-shaped surface must leave the cell in its notch VACANT. That is the whole reason this unit exists
//    instead of a bounding box, so it is asserted directly rather than inferred from a total count.

#include "EngineContext/SpatialAcceleration/TriangleCellOverlap.h"
#include "EngineContext/SpatialAcceleration/ToroidalClipmapField.h"

#include <cstdio>
#include <vector>

using namespace Frontier;

namespace
{

uint32_t FailureCount = 0;
uint32_t CheckCount   = 0;

void Expect(bool Condition, const char* Description)
{
    ++CheckCount;
    if (Condition)
    {
        std::printf("  [ok]   %s\n", Description);
    }
    else
    {
        std::printf("  [FAIL] %s\n", Description);
        ++FailureCount;
    }
}

// Does the marked set contain this cell?
bool CellMarked(const std::vector<CellCoordinate>& Marked, int32_t XCell, int32_t YCell, int32_t ZCell)
{
    for (const CellCoordinate& Cell : Marked)
    {
        if (Cell.XCell == XCell && Cell.YCell == YCell && Cell.ZCell == ZCell)
            return true;
    }
    return false;
}

const float IdentityModel[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE PREDICATE ITSELF
//------------------------------------------------------------------------------------------------------------------------

void InspectOverlapPredicate()
{
    std::printf("\n-- predicate: single cell, unit cell size --------------------------------------\n");

    // A triangle sitting well inside the cell [0,1)^3 must overlap it, and must NOT overlap its neighbours.
    {
        const TriangleCellOverlapSetup Setup = ConfigureTriangleCellOverlap(
            Vector3f{ 0.2f, 0.2f, 0.5f }, Vector3f{ 0.8f, 0.2f, 0.5f }, Vector3f{ 0.5f, 0.8f, 0.5f },
            1.0f, CellOverlapSeparation::Supercover26);

        Expect(EvaluateCellOverlap(Setup, Vector3f{ 0.0f, 0.0f, 0.0f }), "interior triangle overlaps its own cell");
        Expect(!EvaluateCellOverlap(Setup, Vector3f{ 1.0f, 0.0f, 0.0f }), "interior triangle misses the +X neighbour");
        Expect(!EvaluateCellOverlap(Setup, Vector3f{ 0.0f, 0.0f, 1.0f }), "interior triangle misses the +Z neighbour");
        Expect(!EvaluateCellOverlap(Setup, Vector3f{ -1.0f, 0.0f, 0.0f }), "interior triangle misses the -X neighbour");
    }

    // WINDING INDEPENDENCE. The edge normals are oriented by the triangle's facing; if that orientation were dropped, a
    // reversed-winding triangle would reject every cell. Same triangle, both windings, must give the same verdict.
    {
        const Vector3f VertexA{ 0.2f, 0.2f, 0.5f };
        const Vector3f VertexB{ 0.8f, 0.2f, 0.5f };
        const Vector3f VertexC{ 0.5f, 0.8f, 0.5f };

        const TriangleCellOverlapSetup Forward = ConfigureTriangleCellOverlap(VertexA, VertexB, VertexC, 1.0f, CellOverlapSeparation::Supercover26);
        const TriangleCellOverlapSetup Reversed = ConfigureTriangleCellOverlap(VertexA, VertexC, VertexB, 1.0f, CellOverlapSeparation::Supercover26);

        Expect(EvaluateCellOverlap(Forward,  Vector3f{ 0.0f, 0.0f, 0.0f }), "counter-clockwise winding overlaps");
        Expect(EvaluateCellOverlap(Reversed, Vector3f{ 0.0f, 0.0f, 0.0f }), "clockwise winding overlaps the SAME cell");
    }

    // A triangle far away on the plane z = 0.5 but outside the cell in X must be rejected by the cell-face axes alone.
    {
        const TriangleCellOverlapSetup Setup = ConfigureTriangleCellOverlap(
            Vector3f{ 10.2f, 0.2f, 0.5f }, Vector3f{ 10.8f, 0.2f, 0.5f }, Vector3f{ 10.5f, 0.8f, 0.5f },
            1.0f, CellOverlapSeparation::Supercover26);
        Expect(!EvaluateCellOverlap(Setup, Vector3f{ 0.0f, 0.0f, 0.0f }), "distant triangle rejected on a cell-face axis");
    }

    // PLANE TEST. A large triangle whose plane passes well clear of a cell must be rejected even though its BOUNDS enclose that
    // cell — this is precisely the rejection a bounding box cannot make, so it is the sharpest single check here.
    {
        const TriangleCellOverlapSetup Setup = ConfigureTriangleCellOverlap(
            Vector3f{ 0.0f, 0.0f, 0.0f }, Vector3f{ 10.0f, 0.0f, 10.0f }, Vector3f{ 0.0f, 10.0f, 0.0f },
            1.0f, CellOverlapSeparation::Supercover26);

        // The plane contains x=0,z=0 and rises in z with x. The cell at (5,0,0) sits far below that plane in z.
        Expect(!EvaluateCellOverlap(Setup, Vector3f{ 5.0f, 0.0f, 0.0f }),
               "cell inside the triangle's BOUNDS but off its plane is rejected");
        Expect(EvaluateCellOverlap(Setup, Vector3f{ 0.0f, 0.0f, 0.0f }), "cell on the plane at the triangle corner overlaps");
    }

    // EDGE-CROSS AXES. A cell near a triangle's corner, inside both the bounds and the plane band, but beyond the triangle's
    // edge, is only rejectable by one of the nine edge axes.
    {
        const TriangleCellOverlapSetup Setup = ConfigureTriangleCellOverlap(
            Vector3f{ 0.0f, 0.0f, 0.5f }, Vector3f{ 8.0f, 0.0f, 0.5f }, Vector3f{ 0.0f, 8.0f, 0.5f },
            1.0f, CellOverlapSeparation::Supercover26);

        // The hypotenuse runs x+y=8. The cell at (7,7) is inside the bounding box and exactly on the plane, but far outside the edge.
        Expect(!EvaluateCellOverlap(Setup, Vector3f{ 7.0f, 7.0f, 0.0f }),
               "cell past the hypotenuse rejected by an edge-cross axis");
        Expect(EvaluateCellOverlap(Setup, Vector3f{ 1.0f, 1.0f, 0.0f }), "cell well inside the hypotenuse overlaps");
    }

    // A degenerate (zero-area) triangle has no plane; it must still occupy the cells its extent covers rather than vanishing.
    {
        const TriangleCellOverlapSetup Setup = ConfigureTriangleCellOverlap(
            Vector3f{ 0.2f, 0.5f, 0.5f }, Vector3f{ 0.8f, 0.5f, 0.5f }, Vector3f{ 0.5f, 0.5f, 0.5f },
            1.0f, CellOverlapSeparation::Supercover26);
        Expect(Setup.DegenerateCondition, "collinear triangle flagged degenerate");
        Expect(EvaluateCellOverlap(Setup, Vector3f{ 0.0f, 0.0f, 0.0f }), "degenerate sliver still occupies its cell");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE CONCAVITY GUARANTEE
//------------------------------------------------------------------------------------------------------------------------

// 📝 THE point of this unit. An L-shaped surface in the z=0 plane, built from 4 unit-cell quads (8 triangles), occupying an L
//    within a 3x3 cell footprint. The notch cells must come back VACANT. A bounding-box marker would mark all 9.
void InspectConcaveFootprint()
{
    std::printf("\n-- concavity: an L must not mark its notch -------------------------------------\n");

    ToroidalClipmapField Field;
    ConfigureClipmapField(Field, 1, 32, 1.0f);   // one level, 1 m cells — cell (i,j,0) is the unit square at (i,j)

    // An L covering the cells (0,0) (1,0) (2,0) and (0,1) (0,2) — leaving (1,1) (2,1) (1,2) (2,2) empty.
    // Each covered cell is two triangles of a unit quad, inset slightly so the surface sits strictly inside its cell.
    struct CellPatch { int32_t XCell; int32_t YCell; };
    const CellPatch Patches[] = { {0,0}, {1,0}, {2,0}, {0,1}, {0,2} };

    std::vector<float>    Positions;
    std::vector<uint32_t> Indices;
    for (const CellPatch& Patch : Patches)
    {
        const float MinimumX = (float)Patch.XCell + 0.1f;
        const float MinimumY = (float)Patch.YCell + 0.1f;
        const float MaximumX = (float)Patch.XCell + 0.9f;
        const float MaximumY = (float)Patch.YCell + 0.9f;

        const uint32_t Base = (uint32_t)(Positions.size() / 3);

        // Four corners at z = 0.5 (mid-cell in Z, so the surface stays inside the z=0 cell layer).
        Positions.insert(Positions.end(), { MinimumX, MinimumY, 0.5f });
        Positions.insert(Positions.end(), { MaximumX, MinimumY, 0.5f });
        Positions.insert(Positions.end(), { MaximumX, MaximumY, 0.5f });
        Positions.insert(Positions.end(), { MinimumX, MaximumY, 0.5f });

        Indices.insert(Indices.end(), { Base + 0, Base + 1, Base + 2 });
        Indices.insert(Indices.end(), { Base + 0, Base + 2, Base + 3 });
    }

    std::vector<CellCoordinate> Marked;
    const CellOverlapOutcome Outcome =
        VoxelizeTriangleStream(Positions.data(), 3, (uint32_t)(Positions.size() / 3),
                               Indices.data(), (uint32_t)Indices.size(),
                               IdentityModel, Field, 0, CellOverlapSeparation::Supercover26,
                               1000000, Marked);

    std::printf("  (marked %u cells, %u tests, %u refused)\n",
                (unsigned)Outcome.MarkedCellCount, (unsigned)Outcome.TestedCellCount, (unsigned)Outcome.RefusedCellCount);

    Expect(Outcome.RefusedCellCount == 0, "budget was sufficient (no truncation)");

    // Every covered cell present.
    Expect(CellMarked(Marked, 0, 0, 0), "L arm cell (0,0) marked");
    Expect(CellMarked(Marked, 1, 0, 0), "L arm cell (1,0) marked");
    Expect(CellMarked(Marked, 2, 0, 0), "L arm cell (2,0) marked");
    Expect(CellMarked(Marked, 0, 1, 0), "L arm cell (0,1) marked");
    Expect(CellMarked(Marked, 0, 2, 0), "L arm cell (0,2) marked");

    // THE notch. A bounding box would mark all four of these.
    Expect(!CellMarked(Marked, 1, 1, 0), "NOTCH cell (1,1) left vacant");
    Expect(!CellMarked(Marked, 2, 1, 0), "NOTCH cell (2,1) left vacant");
    Expect(!CellMarked(Marked, 1, 2, 0), "NOTCH cell (1,2) left vacant");
    Expect(!CellMarked(Marked, 2, 2, 0), "NOTCH cell (2,2) left vacant");

    Expect(Outcome.MarkedCellCount == 5, "exactly the 5 occupied cells marked, no more");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SWEEP BEHAVIOUR
//------------------------------------------------------------------------------------------------------------------------

void InspectSweepContract()
{
    std::printf("\n-- sweep: dedup, budget honesty, thin variant ----------------------------------\n");

    ToroidalClipmapField Field;
    ConfigureClipmapField(Field, 1, 32, 1.0f);

    // Two triangles sharing one cell must yield ONE marked cell, not two.
    {
        const float Positions[] = {
            0.2f, 0.2f, 0.5f,   0.8f, 0.2f, 0.5f,   0.5f, 0.8f, 0.5f,
            0.3f, 0.3f, 0.6f,   0.7f, 0.3f, 0.6f,   0.5f, 0.7f, 0.6f
        };
        const uint32_t Indices[] = { 0,1,2, 3,4,5 };

        std::vector<CellCoordinate> Marked;
        const CellOverlapOutcome Outcome =
            VoxelizeTriangleStream(Positions, 3, 6, Indices, 6, IdentityModel, Field, 0,
                                   CellOverlapSeparation::Supercover26, 1000000, Marked);
        Expect(Outcome.MarkedCellCount == 1 && Marked.size() == 1, "two triangles in one cell dedup to a single mark");
    }

    // BUDGET HONESTY. A budget of zero must mark nothing and REPORT the refusal — never report success on no work.
    {
        const float Positions[] = { 0.2f, 0.2f, 0.5f,  0.8f, 0.2f, 0.5f,  0.5f, 0.8f, 0.5f };
        const uint32_t Indices[] = { 0,1,2 };

        std::vector<CellCoordinate> Marked;
        const CellOverlapOutcome Outcome =
            VoxelizeTriangleStream(Positions, 3, 3, Indices, 3, IdentityModel, Field, 0,
                                   CellOverlapSeparation::Supercover26, 0, Marked);
        Expect(Outcome.MarkedCellCount == 0, "zero budget marks nothing");
        Expect(Outcome.RefusedCellCount > 0, "zero budget REPORTS the refusal (silent truncation would be a lie)");
    }

    // THIN vs SUPERCOVER. A slanted triangle must claim no MORE cells thin than supercover — the thin variant is a subset.
    {
        const float Positions[] = { 0.5f, 0.5f, 0.5f,  6.5f, 0.5f, 3.5f,  0.5f, 6.5f, 1.5f };
        const uint32_t Indices[] = { 0,1,2 };

        std::vector<CellCoordinate> SupercoverMarked;
        const CellOverlapOutcome SupercoverOutcome =
            VoxelizeTriangleStream(Positions, 3, 3, Indices, 3, IdentityModel, Field, 0,
                                   CellOverlapSeparation::Supercover26, 1000000, SupercoverMarked);

        std::vector<CellCoordinate> ThinMarked;
        const CellOverlapOutcome ThinOutcome =
            VoxelizeTriangleStream(Positions, 3, 3, Indices, 3, IdentityModel, Field, 0,
                                   CellOverlapSeparation::Thin6, 1000000, ThinMarked);

        std::printf("  (supercover %u cells, thin %u cells)\n",
                    (unsigned)SupercoverOutcome.MarkedCellCount, (unsigned)ThinOutcome.MarkedCellCount);

        Expect(SupercoverOutcome.MarkedCellCount > 0, "slanted triangle marks cells under supercover");
        Expect(ThinOutcome.MarkedCellCount > 0, "slanted triangle marks cells under thin");
        Expect(ThinOutcome.MarkedCellCount <= SupercoverOutcome.MarkedCellCount,
               "thin voxelization is a SUBSET of supercover (never more cells)");
    }

    // A transformed instance must land in the cells its WORLD position implies, not its local one.
    {
        const float Positions[] = { 0.2f, 0.2f, 0.5f,  0.8f, 0.2f, 0.5f,  0.5f, 0.8f, 0.5f };
        const uint32_t Indices[] = { 0,1,2 };
        // Translate by (10, 20, 0) — column-major translation in elements 12..14.
        const float TranslatedModel[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 10,20,0,1 };

        std::vector<CellCoordinate> Marked;
        VoxelizeTriangleStream(Positions, 3, 3, Indices, 3, TranslatedModel, Field, 0,
                               CellOverlapSeparation::Supercover26, 1000000, Marked);
        Expect(CellMarked(Marked, 10, 20, 0), "instance transform places the mark at its WORLD cell");
        Expect(!CellMarked(Marked, 0, 0, 0), "the untransformed local cell is NOT marked");
    }
}

} // namespace

int main()
{
    std::printf("================================================================================\n");
    std::printf(" TriangleCellOverlap validation\n");
    std::printf("================================================================================\n");

    InspectOverlapPredicate();
    InspectConcaveFootprint();
    InspectSweepContract();

    std::printf("\n================================================================================\n");
    if (FailureCount == 0)
        std::printf(" PASS - %u checks, 0 failures\n", (unsigned)CheckCount);
    else
        std::printf(" FAIL - %u checks, %u FAILURES\n", (unsigned)CheckCount, (unsigned)FailureCount);
    std::printf("================================================================================\n");

    return (FailureCount == 0) ? 0 : 1;
}
