/*==============================================================================================================================================
                                                            SURFACESAMPLEFIELD.H
==============================================================================================================================================*/
// 🧩 The per-texel geometry substrate every 2D bake map reads. The low-poly UV layout is scan-converted into an Edge × Edge grid; each
//    covered texel records the barycentric-interpolated world position + surface normal, a Lengyel tangent basis matching a UV-gradient
//    convention, the source triangle, and a coverage mask (unset texels are UV gutter). This is the GPU-path CONTRACT ONLY — the field is
//    rasterized on the CPU (unported this slice) and arrives here already built; the GPU surface encoders read its texel array verbatim.

#pragma once
#ifndef FRONTIER_AUTHORING_BAKING_SURFACESAMPLEFIELD_H
#define FRONTIER_AUTHORING_BAKING_SURFACESAMPLEFIELD_H

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One texel's resolved surface geometry. WorldPosition / SurfaceNormal are barycentric-interpolated from the covering triangle;
//    TangentBasisT / TangentBasisB are the Gram-Schmidt-orthonormalized Lengyel tangent frame (paired with SurfaceNormal they form the
//    tangent-space basis the tangent-normal encoder projects into). TriangleIndex is the covering triangle (-1 when uncovered), and
//    CoverageMask marks a texel the UV layout actually paints (a false texel is gutter — left at the map's neutral background).
struct TexelSample
{
    float   WorldPosition[3] = { 0.0f, 0.0f, 0.0f };
    float   SurfaceNormal[3] = { 0.0f, 0.0f, 1.0f };
    float   TangentBasisT[3] = { 1.0f, 0.0f, 0.0f };
    float   TangentBasisB[3] = { 0.0f, 1.0f, 0.0f };
    int32_t TriangleIndex    = -1;
    bool    CoverageMask     = false;
};

// 📝 The rasterized field: a square edge in texels and the row-major Edge × Edge texel records.
struct SurfaceSampleField
{
    uint32_t                 Edge = 0;
    std::vector<TexelSample> Texels;
};

} // namespace Frontier

#endif
