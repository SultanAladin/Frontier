/*==============================================================================================================================================
                                                              SUZANNESCENE.H
==============================================================================================================================================*/
// 🧩 The two authored test scenes the visibility raster draws at P2b, both built from Suzanne heads (the engine reference mesh): the RADIAL ARRAY
//    (a ring of N heads on the floor, each facing the centre, unique tints) and the PYRAMID STRESS (stacked square pyramids of the subdivided
//    Suzanne, the C++ 6-high 91-head-per-pyramid target). A scene is a flat list of SceneInstance transforms over ONE shared mesh — the raster
//    binds the mesh once and issues one instanced draw, reading the per-instance model matrix + identity from an instance buffer. These match the
//    SuzanneRadialArray.html / SuzannePyramidStress.html prototypes so the GPU result is checkable against the browser reference.

#pragma once
#ifndef FRONTIER_GRAPHICS_SCENE_SUZANNESCENE_H
#define FRONTIER_GRAPHICS_SCENE_SUZANNESCENE_H

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which authored scene to build. Radial and MaterialRings use the base Suzanne (507 v / 968 t); Pyramid uses the Catmull-Clark-2x subdivision
//    (7958 v / 15744 t) — the mesh a scene expects is reported by SuzanneSceneMeshPath so the caller loads the matching asset.
enum class SuzanneSceneChoice : uint8_t
{
    RadialArray = 0,   // A ring of heads facing the centre (base mesh)
    PyramidStress,     // Stacked square pyramids of the subdivided head (stress mesh)
    MaterialRings      // Two concentric rings (inner 5 + outer 8) carrying one material preset each (base mesh)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One placed head: a column-major 4x4 model matrix (world = Model * local), a 3x3 normal basis (the rotation-only upper-left, packed as three
//    column vec3s padded to vec4 for std140), a linear-RGB tint, the partition identity the visibility frag writes (one instance == one
//    MicroSurfacePartition at this phase), and the material the shade pass resolves through. The layout is std140-friendly so it uploads straight
//    into an instance storage buffer.
//
//    MaterialId is the second hop of the shade pass's lookup chain: the visibility buffer stores a partition ordinal, the ordinal indexes THIS
//    buffer, and MaterialId then indexes the SurfacePresetTable. It stays 0 (the floor's flat Standard record) for every scene that predates the
//    material work, so the debug-hash views are untouched — only MaterialRings authors it.
struct SuzanneSceneInstance
{
    float    Model[16]      = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };  // [-] - column-major world transform
    float    NormalBasis[12] = { 1,0,0,0, 0,1,0,0, 0,0,1,0 };          // [-] - 3x vec3 (padded to vec4) rotation-only basis
    float    Tint[4]        = { 1,1,1,1 };                              // [-] - linear RGB (+pad) debug tint
    uint32_t PartitionId    = 0;                                        // [-] - instance identity written into the visibility buffer
    uint32_t MaterialId     = 0;                                        // [-] - index into the SurfacePresetTable (0 = the flat Standard record)
    uint32_t Padding[2]     = { 0,0 };                                  // [-] - std140 tail pad to a 16-byte boundary
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// The reference-mesh asset path a scene expects (Documentation/Assets JSON). RadialArray and MaterialRings -> base Suzanne; PyramidStress ->
// subdivided. Returned as a stable string literal so the caller can feed it to LoadReferenceMeshAsset without owning storage.
const char* SuzanneSceneMeshPath(SuzanneSceneChoice Choice);

// Build the placed-instance list for Choice into Result (cleared first). RadialArray: RingCount heads (clamped 4..12) on a ring of radius 6.5,
// each facing the centre, tints cycled. PyramidStress: PyramidCount square pyramids (clamped 1..10) of 6-high layers (6x6..1x1 = 91 heads each)
// laid out on a grid. MaterialRings: a FIXED 13 heads (Count ignored) — an inner ring of 5 and an outer ring of 8, carrying MaterialId 1..13 in
// placement order. The counts mirror the prototypes; pass 0 to take the documented default (7 heads / 4 pyramids at the WebGL scale, or the
// C++ target where noted). Transforms are world-space, Z-up, centimetres implied by the unit-normalized mesh scaled per head.
void BuildSuzanneScene(SuzanneSceneChoice Choice, uint32_t Count, std::vector<SuzanneSceneInstance>& Result);

} // namespace Frontier

#endif
