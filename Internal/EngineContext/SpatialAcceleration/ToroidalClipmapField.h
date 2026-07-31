/*==============================================================================================================================================
                                                          TOROIDALCLIPMAPFIELD.H
==============================================================================================================================================*/
// 🧩 The camera-tracked 3D voxel clipmap: a fixed-resolution cubic cell window per level that SCROLLS with the camera by integer cell shifts,
//    physical address computed by modular wrap — PhysicalCell = (WorldCell + ToroidalOrigin) mod Resolution. When the camera moves one cell,
//    only the newly-exposed L-slab of cells is invalidated; every retained cell keeps its cached contents. Each coarser level DOUBLES the cell
//    size (radius-doubling ladder), so the field is dense near the camera and coarse far away — one addressing primitive giving adaptive
//    resolution. Here it carries only a trivial relight-ramp STUB per cell (not real irradiance) so the scroll/streaming path is visible and
//    unit-checkable in isolation. Pure math, no Vulkan — built on the right-handed Z-up metres world frame and the Float32 vector type.
//
// 🔴 This is a WORLD-space lattice, and it is NOT the shared spine an earlier version of this banner promised. It serves the GI irradiance-probe
//    clipmap (P7b) only. The sun-shadow clipmap does NOT stand on it: shadow tiles are addressed in LIGHT space, whose basis rotates with the
//    sun, whereas these cells are floored straight from world XYZ and are rigidly axis-aligned. No relabelling of axes reconciles the two, and
//    unifying them would need a basis-rotation member here — which would poison every world-aligned consumer. What the two genuinely share is
//    the integer arithmetic in ToroidalAddressing.h and nothing else; the sun window lives in Graphics/Shadow/SunShadowClipmap.h.
//    (PLAN-SunShadowClipmap.md §3 — a ruling reached by two independent fit studies.)

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_SPATIALACCELERATION_TOROIDALCLIPMAPFIELD_H
#define FRONTIER_ENGINECONTEXT_SPATIALACCELERATION_TOROIDALCLIPMAPFIELD_H

#include "EngineContext/Math/LinearAlgebra_Float32.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Defaults derived from the Unreal/Blender clipmap convention (radius-doubling levels), scaled to a VRAM-cheap 3D debug field.
//    Base cell 1 m, 4 levels doubling to 8 m, 32 cells per cubic edge — level 0 spans 32 m, level 3 spans 256 m around the camera.
constexpr uint32_t ClipmapDefaultLevelCount     = 4;
constexpr uint32_t ClipmapDefaultResolution     = 32;    // [cells] - cells per cubic edge, one level
constexpr float    ClipmapDefaultBaseCellMetres = 1.0f;  // [m]     - level-0 cell edge; each coarser level doubles it
constexpr uint32_t ClipmapMaximumLevelCount     = 8;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 An integer cell coordinate in the clipmap's cubic lattice. Signed so a world cell left/below/behind the origin addresses correctly;
//    the modular wrap in ResolvePhysicalCell folds it into the [0, Resolution) physical range.
struct CellCoordinate
{
    int32_t XCell = 0;   // [cell]
    int32_t YCell = 0;   // [cell]
    int32_t ZCell = 0;   // [cell]
};

// 📝 One resident cubic window of cells at a single resolution. ToroidalOrigin is the integer world-cell coordinate the physical [0,0,0]
//    corner currently maps to — advancing it is the scroll. CellMetres is this level's cell edge (doubles per coarser level). Residency and
//    the relight-ramp STUB are dense per-cell arrays of length Resolution³, indexed by the PHYSICAL cell (ResolvePhysicalCell output).
struct ClipmapLevel
{
    uint32_t             Resolution   = ClipmapDefaultResolution;   // [cells] - cubic edge count
    float                CellMetres   = ClipmapDefaultBaseCellMetres; // [m]   - world edge of one cell at this level
    CellCoordinate       ToroidalOrigin;                            // [cell]  - world cell mapped to physical corner [0,0,0]
    bool                 OriginSeeded = false;                      // [-]     - false until the first EvaluateClipmapScroll centres it

    std::vector<uint8_t> ResidencyTable;                            // [-]     - 1 == resident (cached), 0 == vacant (needs fill); length Resolution³
    std::vector<float>   RelightRamp;                               // [-]     - STUB payload: 0 on scroll-in, ramps toward 1 while resident; length Resolution³
};

// 📝 The whole clipmap field — a stack of levels sharing a centre (the camera). Level 0 is the finest; each coarser level doubles CellMetres.
struct ToroidalClipmapField
{
    uint32_t                  LevelCount = ClipmapDefaultLevelCount;   // [-] - active levels in Levels[0..LevelCount)
    std::vector<ClipmapLevel> Levels;                                 // [-] - finest first; Levels.size() == LevelCount
};

// 📝 The result of advancing one level to a new camera position: the origin BEFORE and AFTER the scroll, the integer shift, and the exposed
//    axis-aligned slabs (in world-cell coordinates) that were vacated and need filling. A 3D scroll exposes up to 3 slabs (one per moved
//    axis); each is expressed as an inclusive-min / exclusive-max world-cell box. ExposedSpanCount == 0 means the camera stayed in the same
//    cell (no scroll). The physical wrap is handled by ResolvePhysicalCell, so these spans are pure world-cell ranges.
struct ClipmapScrollSpan
{
    CellCoordinate MinimumCell;   // [cell] - inclusive lower corner (world cells)
    CellCoordinate MaximumCell;   // [cell] - exclusive upper corner (world cells)
};

struct ClipmapScrollResult
{
    uint32_t          Level              = 0;   // [-]    - which level this result advanced
    CellCoordinate    OriginBefore;             // [cell] - ToroidalOrigin prior to the scroll
    CellCoordinate    OriginAfter;              // [cell] - ToroidalOrigin after the scroll (new physical-corner world cell)
    CellCoordinate    CellShift;                // [cell] - OriginAfter - OriginBefore
    uint32_t          ExposedSpanCount   = 0;   // [-]    - valid entries in ExposedSpans[0..ExposedSpanCount)
    ClipmapScrollSpan ExposedSpans[3];          // [-]    - one per moved axis; the vacated L-slabs in world-cell coordinates
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Allocate the level stack with a radius-doubling ladder: level 0 uses BaseCellMetres, each coarser level doubles it; all levels share
// Resolution. Residency starts all-vacant, ramps all-zero, origins unseeded (the first EvaluateClipmapScroll centres them). LevelCount is
// clamped to [1, ClipmapMaximumLevelCount]; Resolution to >= 1. Idempotent — reconfiguring resizes and resets the field.
void ConfigureClipmapField(ToroidalClipmapField& Field,
                           uint32_t              LevelCount,
                           uint32_t              Resolution,
                           float                 BaseCellMetres);

// Fold a signed world-cell coordinate into this level's physical [0, Resolution) cubic range via the toroidal wrap
// PhysicalCell = ((WorldCell + Origin) mod Resolution), the modulo taken positive. The result indexes ResidencyTable / RelightRamp
// through FlattenPhysicalCell.
[[nodiscard]] CellCoordinate ResolvePhysicalCell(const ToroidalClipmapField& Field, uint32_t Level, CellCoordinate WorldCell);

// Row-major flatten of a PHYSICAL cell (each axis already in [0, Resolution)) into a dense array index: X + Resolution*(Y + Resolution*Z).
[[nodiscard]] uint32_t FlattenPhysicalCell(const ToroidalClipmapField& Field, uint32_t Level, CellCoordinate PhysicalCell);

// The world cell a camera position falls in at this level: floor(CameraPosition / CellMetres) per axis, so the level stays camera-centred.
[[nodiscard]] CellCoordinate ResolveCameraCell(const ToroidalClipmapField& Field, uint32_t Level, Vector3f CameraPosition);

// Compute (do NOT apply) the scroll for one level to a new camera position: the new origin that re-centres the window on the camera, the
// integer shift, and the exposed L-slabs. A shift of zero yields ExposedSpanCount == 0. On the first call for an unseeded level the whole
// window counts as exposed (one span covering the full window). Pair with IntegrateScrollResidency to apply it.
[[nodiscard]] ClipmapScrollResult EvaluateClipmapScroll(const ToroidalClipmapField& Field, uint32_t Level, Vector3f CameraPosition);

// Apply a scroll result to the field: advance ToroidalOrigin to OriginAfter, then mark every cell in the exposed slabs VACANT with its
// relight ramp reset to zero (they scroll in fresh). Retained cells keep residency and ramp. This is the streaming keystone — only the
// L-slab is invalidated per move.
void IntegrateScrollResidency(ToroidalClipmapField& Field, const ClipmapScrollResult& ScrollOutcome);

// Advance the relight-ramp STUB one step for every RESIDENT cell across all levels (vacant cells stay at zero). RampRate is the per-call
// increment, clamped at 1. This stands in for the real P7b per-frame probe relight so the scroll-in transition is visible; it carries no
// irradiance. A newly-exposed cell reads zero until IntegrateScrollResidency is followed by a residency fill (the debug viz treats a
// resident cell with a rising ramp as "relighting").
void AdvanceRelightRamp(ToroidalClipmapField& Field, float RampRate);

// Mark a single world cell RESIDENT at this level (its ramp begins climbing on the next AdvanceRelightRamp). The debug object-in-cell
// highlight and the fill of scrolled-in cells both route through this. Out-of-window coordinates fold via the wrap, so any world cell is
// addressable.
void ActivateWorldCell(ToroidalClipmapField& Field, uint32_t Level, CellCoordinate WorldCell);

} // namespace Frontier

#endif
