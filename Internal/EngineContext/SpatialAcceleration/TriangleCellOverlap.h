/*==============================================================================================================================================
                                                          TRIANGLECELLOVERLAP.H
==============================================================================================================================================*/
// 🧩 Exact triangle-versus-cell overlap, and the surface voxelization built on it: which cells of a cubic lattice a triangle mesh ACTUALLY
//    passes through. The overlap predicate is the Schwarz & Seidel (2010) setup/test split of the 13-axis separating-axis theorem — the three
//    cell face normals collapse to a cheap per-axis interval compare, the triangle plane to one dot product against a precomputed critical
//    corner, and the nine edge-cross axes to nine 2D edge functions evaluated at the cell's minimum corner. Per triangle the setup is paid
//    ONCE; the inner per-cell test is then dot-products-and-compares with no divides and no square roots.
// 📝 Why not an AABB. A bounding box is conservative but NOT tight: an L-shaped or concave object marks the whole enclosing box, lighting cells
//    that hold no surface at all. For a GI / shadow consumer those false cells are wrong answers, not cosmetic noise — they leak light and cast
//    from nothing. This unit tests the triangle itself, so a concave object marks only the cells its surface crosses.
// 📝 The result is a 26-SEPARATING supercover: every cell the triangle touches is marked, so a closed surface encloses a watertight cell shell
//    with no leaks. That is the conservative direction — it never misses a cell, and it never marks one the triangle misses.
// 📝 Load balance is structural here, not an optimization. A one-thread-per-triangle sweep collapses on a triangle that spans the whole lattice
//    (the ground slab is exactly that case), because its candidate cell box is the entire field. Every entry point therefore takes an explicit
//    cell budget and reports what it refused, so a truncated sweep can never be mistaken for full coverage.
// 📝 Pure math over the right-handed Z-up metres world frame. No Vulkan, no allocation beyond the caller's output container.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_SPATIALACCELERATION_TRIANGLECELLOVERLAP_H
#define FRONTIER_ENGINECONTEXT_SPATIALACCELERATION_TRIANGLECELLOVERLAP_H

#include "EngineContext/Math/LinearAlgebra_Float32.h"
#include "EngineContext/SpatialAcceleration/ToroidalClipmapField.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 How thick a marked surface shell is. Both are gap-free for a closed surface; they differ in how many cells one triangle claims.
//      • Supercover26 — every cell the triangle geometrically touches, including cells it only grazes at a corner or edge. The strict SAT
//                       answer. 26-separating: a closed surface's shell blocks every path out, including diagonal ones.
//      • Thin6        — Huang et al. (1998) thin voxelization: the plane test tightens from the cell's full diagonal extent to half a cell,
//                       so a triangle claims noticeably fewer cells (published measurements: 26–42% fewer) while still leaving no 6-connected
//                       gap. Cheaper to store and to walk; a diagonal ray can slip through, so use it where 6-connectivity is enough.
enum class CellOverlapSeparation : uint32_t
{
    Supercover26 = 0,   // strict touch test — thickest, leak-proof against diagonal paths
    Thin6        = 1,   // half-cell plane band — fewer cells, still 6-connected
};

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Everything about one triangle that does NOT depend on which cell is being tested, computed once before the cell loop. This is the whole
//    point of the setup/test split: the nine edge-cross separating axes reduce to nine precomputed 2D edge normals plus nine scalar offsets,
//    so the inner test never re-derives them. CellExtent is baked in, so a table is valid for ONE cell size only.
struct TriangleCellOverlapSetup
{
    Vector3f VertexA;                       // [m] - triangle corner, world space
    Vector3f VertexB;                       // [m] - triangle corner, world space
    Vector3f VertexC;                       // [m] - triangle corner, world space

    Vector3f PlaneNormal;                   // [-] - (B-A) x (C-A), NOT normalized (the tests are scale-invariant in it)
    float    PlaneOffset       = 0.0f;      // [m] - dot(PlaneNormal, VertexA)
    float    PlaneCriticalSpan = 0.0f;      // [m] - half-extent of a cell projected on PlaneNormal (the band the plane may sit in)

    // The nine edge-cross axes, factored per projection plane (XY, YZ, ZX) x three triangle edges. Each is a 2D outward edge normal plus the
    // offset that already folds in the cell's extent, so the inner test is one dot product and one compare against zero.
    float    EdgeNormalXY[3][2] = { { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f } };   // [-] - per edge, (x,y) normal
    float    EdgeOffsetXY[3]    = { 0.0f, 0.0f, 0.0f };                                 // [m] - matching constant
    float    EdgeNormalYZ[3][2] = { { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f } };   // [-] - per edge, (y,z) normal
    float    EdgeOffsetYZ[3]    = { 0.0f, 0.0f, 0.0f };                                 // [m] - matching constant
    float    EdgeNormalZX[3][2] = { { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f } };   // [-] - per edge, (z,x) normal
    float    EdgeOffsetZX[3]    = { 0.0f, 0.0f, 0.0f };                                 // [m] - matching constant

    Vector3f BoundMinimum;                  // [m] - triangle's own world lower corner (the candidate cell box comes from this)
    Vector3f BoundMaximum;                  // [m] - triangle's own world upper corner
    float    CellExtent       = 0.0f;       // [m] - cell edge this table was built for
    bool     DegenerateCondition = false;   // [-] - true when the triangle has zero area (its normal vanished); tested by bounds only
};

// 📝 What a voxelization sweep did, so the caller can report it instead of silently under-covering. RefusedCellCount > 0 means the budget ran
//    out and the marking is INCOMPLETE — a consumer that treats vacancy as "no geometry" must not trust the result in that case.
struct CellOverlapOutcome
{
    uint32_t MarkedCellCount    = 0;   // [-] - distinct cells the sweep marked
    uint32_t TestedCellCount    = 0;   // [-] - candidate cells the overlap predicate ran on (the real cost measure)
    uint32_t RefusedCellCount   = 0;   // [-] - candidate cells the budget refused; > 0 means incomplete coverage
    uint32_t DegenerateTriangleCount = 0; // [-] - zero-area triangles folded in by bounds alone
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build the per-triangle table for a given cell size. Pay this once per triangle, then call EvaluateCellOverlap for every candidate cell.
// A zero-area triangle yields DegenerateCondition and is treated as its bounding box by the predicate (a sliver still occupies cells).
[[nodiscard]] TriangleCellOverlapSetup ConfigureTriangleCellOverlap(Vector3f              VertexA,
                                                                   Vector3f              VertexB,
                                                                   Vector3f              VertexC,
                                                                   float                 CellMetres,
                                                                   CellOverlapSeparation Separation);

// The exact predicate: does the triangle the setup describes intersect the cubic cell whose lower corner is CellMinimum? Runs the 13-axis SAT
// in the factored form — three interval compares, one plane band test, nine edge functions — and returns on the first separating axis found.
[[nodiscard]] bool EvaluateCellOverlap(const TriangleCellOverlapSetup& Setup, Vector3f CellMinimum);

// Voxelize a triangle-list mesh into the world cells of one clipmap level, appending every marked cell to MarkedCells (deduplicated against
// what a single sweep marks, so one cell appears once however many triangles cross it). Positions are read from a stride-interleaved float
// array — PositionStride is in FLOATS, matching a RenderVertex-style interleaved stream — and transformed by the column-major Model.
//
// The sweep is two-level by construction: each triangle's own bounds give the candidate cell box, so a triangle only ever tests cells it could
// plausibly touch, and the exact predicate then rejects the rest. CellBudget bounds the total candidate tests; when it runs out the sweep stops
// and reports the shortfall in RefusedCellCount rather than quietly returning a partial answer.
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
                                          std::vector<CellCoordinate>& MarkedCells);

// Reduce a marked cell set to its OUTER SHELL: keep only cells that have at least one vacant 6-neighbour, discarding those buried on all six
// sides. Order is not preserved. Returns the number of interior cells removed.
//
// 📝 This is a VIEWING reduction, not an occupancy one. A solid slab's interior cells are geometrically real but visually unreachable — they sit
//    behind their own outer faces, so a wire-cube overlay that draws them pays their whole vertex cost for pixels no observer can distinguish.
//    A thick voxelized floor is the pathological case: nearly every cell is interior, and the overlay spends most of its frame on cages hidden
//    inside the slab. A GI or shadow consumer must read the UNCULLED set — vacancy there means "no geometry", and hollowing a solid would report
//    empty space inside it. So call this on a copy destined for display and keep the full set for the consumers.
uint32_t ReduceCellsToOuterShell(std::vector<CellCoordinate>& MarkedCells);

} // namespace Frontier

#endif
