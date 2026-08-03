/*==============================================================================================================================================
                                                            SURFELRADIALDEPTH.H
==============================================================================================================================================*/
// 🧩 The host-side companion to SurfelRadialDepth.glsl — a HEADER OF CONSTANTS, not a submission. Radial depth has no dispatch of its own: the
//    per-surfel MSM tile is WRITTEN inside SurfelIntegrate.comp (update_surfel_depth2) and READ inside the Phase-3 resolve; master-plan §4 still lists
//    it as a file so the one place the occlusion tunables + the tile sizing live is named. It also carries a thin CPU mirror of the shader's
//    compute_surfel_depth_weight so a headless gate (I4) can oracle a probed tile's visibility without a device readback of the shader math.
//
//    🔴 THE OCCLUSION TUNABLES ARE THE PUSH-BLOCK OcclusionParams (SurfelIntegrate.comp). (shadowStrength, bleedReduction, grazingBiasScale,
//       varianceBleedScale) = (1.2, 0.2, 0.25, 0.15) — ported 1:1 from surfelRadialDepth.ts. The integrate reads them, the gather's radial-occlusion
//       gate consumes them; keep this the single source so a change lands in both. NOT calibrated to world scale — these are unitless MSM tunables.
//
//    📝 The tile sizing mirrors SurfelDepthBaseIndex (SurfelRadialDepth.glsl): SURFEL_DEPTH_TEXELS = 4, so surfel i owns 16 vec4 moments at
//       i * 16. SurfelPool already allocates SurfelDepthFloats (= 64) per surfel; this header only names the texel geometry for the gate + any host
//       reasoning, and never allocates.

#pragma once
#ifndef FRONTIER_GRAPHICS_SURFEL_SURFELRADIALDEPTH_H
#define FRONTIER_GRAPHICS_SURFEL_SURFELRADIALDEPTH_H

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The per-surfel MSM tile edge — MUST match SURFEL_DEPTH_TEXELS in SurfelRadialDepth.glsl. 4x4 = 16 texels of vec4 moments per surfel.
constexpr uint32_t SurfelDepthTileEdge   = 4;
constexpr uint32_t SurfelDepthTileTexels = SurfelDepthTileEdge * SurfelDepthTileEdge;   // 16

// 🔴 The occlusion tunables the integrate pushes as OcclusionParams (ported 1:1 from surfelRadialDepth.ts). Order is (shadowStrength, bleedReduction,
//    grazingBiasScale, varianceBleedScale) — the exact order SurfelComputeDepthWeight / SurfelRadialOcclusionRw read Params.x..Params.w.
struct SurfelOcclusionParams
{
    float ShadowStrength    = 1.2f;   // [-] - Params.x — shadow-strength multiply on the raw MSM shadow
    float BleedReduction    = 0.2f;   // [-] - Params.y — base light-bleed remap floor
    float GrazingBiasScale  = 0.25f;  // [-] - Params.z — grazing-angle bleed term (scaled by variance factor)
    float VarianceBleedScale = 0.15f; // [-] - Params.w — variance-driven bleed term
};

// The first tile-texel index for a surfel — mirror of SurfelDepthBaseIndex in the shader (surfelIndex * 16). vec4-indexed.
inline uint32_t SurfelDepthBaseIndex(uint32_t SurfelIndex)
{
    return SurfelIndex * SurfelDepthTileTexels;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        CPU GATE ORACLE
//------------------------------------------------------------------------------------------------------------------------

// A CPU mirror of compute_surfel_depth_weight (SurfelRadialDepth.glsl) — the Hamburger 4MSM visibility of a query `Distance` against the four moments
// (M1,M2,M3,M4) at cosine `CosTheta`, with the four occlusion tunables. Returns 1.0 (visible) on the uninitialised sentinel (M4 == 0). Used ONLY by
// the headless I4 gate to check a probed tile without re-running the shader; it is not on any render path. 1:1 with the shader — every epsilon matches.
float SurfelComputeDepthWeightHost(float M1, float M2, float M3, float M4,
                                   float Distance, float CosTheta,
                                   const SurfelOcclusionParams& Params);

} // namespace Frontier

#endif
