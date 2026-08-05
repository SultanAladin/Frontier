/*==============================================================================================================================================
                                                          SURFELSTOREBINDINGS.GLSL
==============================================================================================================================================*/
// 🧩 The single declaration of set 0 — every buffer SurfelStore owns, bound the same way by every surfel compute unit. The lifecycle's four passes, the
//    radiance trace and the integrate all include this file instead of restating their own bindings, because Vulkan requires the descriptor set layout a
//    pipeline was built against to match the one bound at dispatch, and four hand-written copies of a twelve-binding table drift.
//
// 🔴 THE BINDING NUMBERS ARE A CONTRACT WITH SurfelLifecycleSubmission.cpp, WHICH BUILDS THE LAYOUT FROM THE SAME ORDINALS. A mismatch does not fail
//    validation when both sides declare a storage buffer at the same index — it silently hands a pass the wrong buffer, and a pass reading cell spans as
//    surfel records produces a plausible-looking field of nonsense. ⚠️ Adding a binding means editing the ordinal list in that .cpp in the same edit.
//
// 📝 Every binding is declared for the whole set even when a given pass touches only three of them. An unused storage binding costs nothing at dispatch
//    (the descriptor is written either way) and it keeps ONE layout across the chain, which is what lets the submission bind the set once and then run
//    all four dispatches without rebinding.
//
// ⚠️ Buffers a pass only reads are still declared writable. GLSL's `readonly` is a qualifier on the block, not on the descriptor, so mixing readonly and
//    read-write declarations of the same binding across passes is legal — but it makes the layout harder to verify by eye than it is worth. The passes
//    that must not write a buffer simply do not write it.
//
// 🔴 A CONSUMER WITH ITS OWN CONSTANTS MUST #define SURFEL_STORE_BINDINGS_WITHOUT_LIFECYCLE_CONSTANTS BEFORE INCLUDING THIS FILE. GLSL allows exactly ONE
//    push_constant block per stage, so the lifecycle's block below cannot coexist with the radiance trace's — which needs sun, sky and step fields the
//    lifecycle has no use for, and has no use for the ray ladder or the cell count. The twelve bindings are the part worth sharing; the push block is not,
//    and pretending otherwise would mean one ever-growing block whose fields most passes ignore. ⚠️ A consumer that takes the opt-out owns FetchCameraOrigin
//    too, since that helper reads the block. SurfelRadianceTrace.comp is the reference spelling.

#ifndef FRONTIER_SURFEL_STORE_BINDINGS_GLSL
#define FRONTIER_SURFEL_STORE_BINDINGS_GLSL

#include "SurfelTypes.glsl"

//------------------------------------------------------------------------------------------------------------------------
//                                                            SET 0
//------------------------------------------------------------------------------------------------------------------------

layout(std430, set = 0, binding =  0) buffer SurfelRecordBlock    { Surfel              Records[];  } SurfelRecordStorage;
layout(std430, set = 0, binding =  1) buffer SurfelLiveBlock      { uint                Ordinals[]; } LiveIndexStorage;
layout(std430, set = 0, binding =  2) buffer SurfelPendingBlock   { uint                Ordinals[]; } PendingIndexStorage;
layout(std430, set = 0, binding =  3) buffer SurfelVacancyBlock   { uint                Ordinals[]; } VacancyStorage;
layout(std430, set = 0, binding =  4) buffer SurfelCellSpanBlock  { SurfelCellSpan      Spans[];    } CellSpanStorage;
layout(std430, set = 0, binding =  5) buffer SurfelCellListBlock  { uint                Ordinals[]; } CellListStorage;
layout(std430, set = 0, binding =  6) buffer SurfelReservationBlock { uint              Reserved[]; } ReservationStorage;
layout(std430, set = 0, binding =  7) buffer SurfelTallyBlock     { uint                Tally[];    } ReferenceTallyStorage;
layout(std430, set = 0, binding =  8) buffer SurfelRecycleBlock   { SurfelRecycleRecord Records[];  } RecycleStorage;
layout(std430, set = 0, binding =  9) buffer SurfelRayBlock       { SurfelRayOutcome    Outcomes[]; } RayOutcomeStorage;
layout(std430, set = 0, binding = 10) buffer SurfelCounterBlock   { uint                Slots[];    } CounterStorage;
layout(std430, set = 0, binding = 11) buffer SurfelHitLocatorBlock { uvec4              Anchors[];  } HitLocatorStorage;

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUSH BLOCK
//------------------------------------------------------------------------------------------------------------------------

// 📝 One push block shared by the whole lifecycle, mirroring SurfelLifecycleConstants in the host header field for field. Scalars only, for the same
//    reason every record in SurfelTypes.glsl is scalar: a vec3 here would carry 16-byte alignment and silently shift every field after it.
// 🔴 THE CAMERA POSITION MUST BE THE SAME BYTES FOR EVERY PASS IN A FRAME. The census and the scatter each resolve a surfel's cell from it, and
//    ResolveCellCoordinate's round() is free either way on a half-cell tie (see SurfelCellGrid.glsl). Two passes given even slightly different origins can
//    land a boundary surfel in different cells: the census reserves a slot in one cell, the scatter writes into another, and the cell whose count was
//    reserved holds one uninitialized entry. The submission pushes this ONCE for all four dispatches, which is what makes that impossible.
#ifndef SURFEL_STORE_BINDINGS_WITHOUT_LIFECYCLE_CONSTANTS

layout(push_constant) uniform SurfelLifecycleConstants
{
    float CameraX, CameraY, CameraZ;    // [m]      - the frame's camera origin; the grid is relative to it
    uint  ResolutionX, ResolutionY;     // [px]     - render target extent, for the screen-projected radius
    float VerticalFieldOfView;          // [rad]    - vertical FOV, same
    float TargetArea;                   // [px²]    - screen area one surfel aims to cover
    float VarianceSensitivity;          // [-]      - how hard MSME variance pushes the ray count up
    uint  MinimumRayCount;              // [-]      - ray ladder floor (a sleeping surfel's ceiling)
    uint  MaximumRayCount;              // [-]      - ray ladder ceiling
    uint  LockEnabled;                  // [-]      - non-zero freezes the field: no radius/ray/recycle writes (upstream mLockSurfel)
    uint  CellCount;                    // [-]      - CellDimension³, the offset scan's dispatch bound
} Constants;

vec3 FetchCameraOrigin() { return vec3(Constants.CameraX, Constants.CameraY, Constants.CameraZ); }

uvec2 FetchRenderExtent() { return uvec2(Constants.ResolutionX, Constants.ResolutionY); }

#endif // SURFEL_STORE_BINDINGS_WITHOUT_LIFECYCLE_CONSTANTS

#endif
