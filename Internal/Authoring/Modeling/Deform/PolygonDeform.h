/*============================================================================================================================================
                                                              POLYGONDEFORM.H
============================================================================================================================================*/
// 🧩 Three Blender-standard vertex-deform kernels for the authoring PolygonCluster — Smooth, Shrink/Fatten, and Randomize. Unlike
//    Extrude / Inset these change NO topology: they move existing vertices in place, so the face stream, corner UVs and adjacency all
//    stay valid (the caller need not rebuild adjacency for topology — only re-stream the moved positions to the device). Smooth is an
//    iterative Laplacian relaxation (each affected vertex is pulled toward the mean of its one-ring, Factor per iteration, Iterations
//    passes) offered two ways: SmoothPolygonSelection applies it one-shot, while AccumulateSmoothSnapshot + SmoothOffsetAdvance let the
//    viewport drive the strength live with the mouse (blend origin -> baked full-strength target). Shrink/Fatten and Randomize are
//    likewise interactive: AccumulateDeformSnapshot records each affected vertex's arm-time
//    position + its unit normal + a stable per-vertex pseudo-random vector, then ShrinkOffsetAdvance / RandomizeOffsetAdvance
//    re-derive the live positions idempotently from that snapshot as the viewport drives one scalar with the mouse. All three read the
//    BASE adjacency (one-ring / face normals) but never mutate it. This mirrors PolygonInset.{h,cpp} so the viewport session math is reused.

#pragma once
#ifndef FRONTIER_AUTHORING_MODELING_DEFORM_POLYGONDEFORM_H
#define FRONTIER_AUTHORING_MODELING_DEFORM_POLYGONDEFORM_H

#include "LinearAlgebra_Float64.h"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace Frontier
{

struct PolygonCluster;   // 📝 forward-declared — the kernels mutate its vertex positions only; the .cpp pulls the full header.
struct AdjacencyIndex;   // 📝 forward-declared — the kernels read one-ring adjacency / face normals over the BASE topology only.

//------------------------------------------------------------------------------------------------------------------------
//                                                          ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The randomize axis, taken from the toolbar's Axis choice. `.Category` suffix per the naming skill (replaces `Kind`). Uniform
//    jitters each vertex along its own stable random vector (all directions); NormalOnly jitters strictly along the vertex normal.
enum class RandomizeCategory
{
    UniformCategory,     // [-] - jitter along a per-vertex random unit vector (every direction)
    NormalOnlyCategory   // [-] - jitter strictly along the vertex normal (in / out only)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One affected vertex and the axes an interactive deform slides it along, captured at arm time. NormalDirection is the unit
//    vertex normal (Shrink's slide axis, and Randomize's axis in NormalOnly). RandomDirection is a stable per-vertex unit random
//    vector (Randomize's axis in Uniform). OriginPosition is the arm-time position, so an Advance re-derives the live position
//    idempotently (a fresh scalar overwrites, never accumulates) and a cancel (scalar 0) rewinds exactly to the origin.
struct DeformOffsetSample
{
    uint32_t NewVertex       = 0;                    // [-]  - affected vertex index into the cluster's VertexField
    Vector3d NormalDirection = { 0.0, 0.0, 0.0 };    // [-]  - unit vertex normal (Shrink slide axis / Randomize NormalOnly axis)
    Vector3d RandomDirection = { 0.0, 0.0, 0.0 };    // [-]  - stable per-vertex unit random vector (Randomize Uniform axis)
    Vector3d OriginPosition  = { 0.0, 0.0, 0.0 };    // [cm] - position at arm time (the idempotent base every Advance re-derives from)
    Vector3d SmoothedTarget  = { 0.0, 0.0, 0.0 };    // [cm] - fully-relaxed position (Iterations at Factor 1); the interactive Smooth blend target
};

// 📝 The snapshot an interactive deform (Shrink / Randomize) captures at arm time: one sample per affected vertex. The viewport
//    holds this for the life of the drag and hands it to the matching Advance each frame. Completed is false + OffsetSamples empty
//    on a degenerate request (no affected vertices, an index past the vertex count).
struct DeformSnapshot
{
    bool                            Completed = false;   // [-] - false + no samples on a degenerate request
    std::vector<DeformOffsetSample> OffsetSamples;      // [-] - one per affected vertex (drives the interactive offset)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Relax AffectedVertices toward the mean of their one-ring adjacency: a Laplacian smooth, Factor (clamped to [0,1]) of the way to
// the adjacent mean per iteration, Iterations (clamped to >= 1) passes. Adjacent vertices OUTSIDE the affected set still contribute
// their (unmoved) position, so the selection boundary is drawn toward its surrounding ring exactly as Blender's Smooth Vertices does.
// Each iteration is double-buffered (all new positions derive from the previous iteration's positions), so the pass is
// order-independent. A vertex with no adjacency holds still. No topology changes. BaseAdjacency must describe Target's current
// topology. Returns false (Target untouched) on a degenerate request (empty AffectedVertices, an index past the vertex count).
bool SmoothPolygonSelection(PolygonCluster&                     Target,
                            const AdjacencyIndex&               BaseAdjacency,
                            const std::unordered_set<uint32_t>& AffectedVertices,
                            double                              Factor,
                            int                                 Iterations);

// Capture the arm-time snapshot an interactive Smooth drives from: for each affected vertex, its current position (OriginPosition)
// and its FULLY relaxed position (SmoothedTarget) — the Laplacian result of Iterations passes at Factor 1.0 over the same one-ring
// SmoothPolygonSelection uses. The viewport then blends Origin -> SmoothedTarget by the drag scalar (SmoothOffsetAdvance), so the
// mouse drives the smoothing strength live instead of a stiff one-shot button. BaseAdjacency must describe Target's current
// topology. Fills Snapshot and returns true; returns false (Snapshot.Completed false, no samples) on a degenerate request.
bool AccumulateSmoothSnapshot(const PolygonCluster&               Target,
                              const AdjacencyIndex&               BaseAdjacency,
                              const std::unordered_set<uint32_t>& AffectedVertices,
                              int                                 Iterations,
                              DeformSnapshot&                     Snapshot);

// Drive an interactive Smooth: re-derive each sampled vertex as OriginPosition + (SmoothedTarget - OriginPosition) * Strength from
// the snapshot, so it is idempotent (a fresh Strength each frame overwrites, never accumulates) and a cancel (Strength 0) rewinds
// exactly to the origin. Strength is clamped to [0, 1] — 0 leaves the vertices at rest, 1 is the fully relaxed target. Returns false
// (no writes) if any sample's NewVertex is out of range for Target.
bool SmoothOffsetAdvance(PolygonCluster&                        Target,
                         const std::vector<DeformOffsetSample>& Samples,
                         double                                 Strength);

// Capture the arm-time snapshot an interactive Shrink / Randomize drives from: for each affected vertex, its current position, its
// unit vertex normal (mean of the incident base-face normals, +Z fallback), and a stable per-vertex unit random vector derived from
// splitmix64(Seed ^ vertexIndex) — deterministic, so the same Seed re-rolls the identical pattern and no runtime RNG is needed.
// BaseAdjacency must describe Target's current topology. Fills Snapshot and returns true; returns false (Snapshot.Completed false,
// no samples) on a degenerate request (empty AffectedVertices, an index past the vertex count).
bool AccumulateDeformSnapshot(const PolygonCluster&               Target,
                              const AdjacencyIndex&               BaseAdjacency,
                              const std::unordered_set<uint32_t>& AffectedVertices,
                              uint32_t                            Seed,
                              DeformSnapshot&                     Snapshot);

// Drive an interactive Shrink/Fatten: re-derive each sampled vertex as OriginPosition + NormalDirection * Offset from the snapshot,
// so it is idempotent (a fresh Offset each frame overwrites, never accumulates) and a cancel (Offset 0) rewinds exactly. A positive
// Offset fattens outward along the normal, a negative Offset shrinks inward. Returns false (no writes) if any sample's NewVertex is
// out of range for Target.
bool ShrinkOffsetAdvance(PolygonCluster&                        Target,
                         const std::vector<DeformOffsetSample>& Samples,
                         double                                 Offset);

// Drive an interactive Randomize: re-derive each sampled vertex as OriginPosition + Axis * (Amount * ScalarHash), where Axis is
// RandomDirection (Uniform) or NormalDirection (NormalOnly) and ScalarHash in [-1, 1] is a second stable per-vertex hash baked into
// RandomDirection's magnitude bookkeeping — so every vertex jitters by a different signed magnitude, deterministically from the
// snapshot Seed. Idempotent (a fresh Amount overwrites) and a cancel (Amount 0) rewinds. Returns false (no writes) if any sample's
// NewVertex is out of range for Target.
bool RandomizeOffsetAdvance(PolygonCluster&                        Target,
                            const std::vector<DeformOffsetSample>& Samples,
                            double                                 Amount,
                            RandomizeCategory                      Category);

} // namespace Frontier

#endif   // FRONTIER_AUTHORING_MODELING_DEFORM_POLYGONDEFORM_H
