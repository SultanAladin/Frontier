/*==============================================================================================================================================
                                                             TRIANGLERAYVOLUME.H
==============================================================================================================================================*/
// 🧩 The triangle bounding-volume data the GPU bake dispatch uploads: a median-split AABB tree over a RenderVertexStream where each
//    RayTriangle carries its per-corner shading normal + UV + colour so a texel resolving a triangle can barycentric-interpolate the
//    surface frame. This is the GPU-path CONTRACT ONLY — the CPU tree builder + the closest-point / ray-cast queries live in the CPU
//    baker (unported this slice). The volume arrives here already built by the caller; the compute dispatch reads its arrays verbatim.

#pragma once
#ifndef FRONTIER_AUTHORING_BAKING_TRIANGLERAYVOLUME_H
#define FRONTIER_AUTHORING_BAKING_TRIANGLERAYVOLUME_H

#include "Authoring/Geometry/Modeling/PolygonCluster.h"   // 📝 RenderVertexStream — the interleaved float vertices + triangle indices the volume is built from.

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One bake triangle: its three corners, centroid, unit face normal, its own AABB (the tree split key), the angle-weighted
//    pseudonormals the closest-point sign reads, and — unique to the ray volume — the interpolated authoring frame carried per corner:
//    VertexNormal[i] is the render-stream shading normal at CornerA/B/C, Texture[i] its UV, VertexColour[i] its linear RGB (white when the
//    stream carries none). MaterialIndex is the face's material identity. A texel resolving this triangle interpolates VertexNormal / colour
//    / a world position by barycentric weight; CornerNormal / EdgeNormal drive the exact inside/outside sign like the CPU baker's tree.
//    Positions/normals are single precision to keep the tree traversal tight with no external vector dependency.
struct RayTriangle
{
    float   CornerA[3]      = { 0.0f, 0.0f, 0.0f };
    float   CornerB[3]      = { 0.0f, 0.0f, 0.0f };
    float   CornerC[3]      = { 0.0f, 0.0f, 0.0f };
    float   Centroid[3]     = { 0.0f, 0.0f, 0.0f };
    float   FaceNormal[3]   = { 0.0f, 0.0f, 0.0f };
    float   CornerNormal[3][3] = {};      // [-] - angle-weighted pseudonormal at CornerA, CornerB, CornerC
    float   EdgeNormal[3][3]   = {};      // [-] - mean face pseudonormal across edge AB, BC, CA
    float   VertexNormal[3][3] = {};      // [-] - render-stream shading normal at CornerA, CornerB, CornerC (interpolation source)
    float   Texture[3][2]      = {};      // [-] - UV at CornerA, CornerB, CornerC (interpolation source)
    float   VertexColour[3][3] = { { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f } };   // [-] - linear RGB at each corner
    int32_t MaterialIndex      = 0;       // [-] - per-face material identity (0 when the stream carries no material)
    float   BoundaryMinimum[3] = {};      // [cm] - triangle AABB lower corner
    float   BoundaryMaximum[3] = {};      // [cm] - triangle AABB upper corner
};

// 📝 One tree element: its AABB plus either a leaf's triangle span (into the ordered index list) or two down-links. LeftDownLink < 0
//    marks a leaf. Named RayBoundaryElement to avoid the banned "Node".
struct RayBoundaryElement
{
    float   BoundaryMinimum[3] = {};
    float   BoundaryMaximum[3] = {};
    int32_t FirstTriangle  = 0;    // [-] - leaf: first ordered-index entry; internal: -1
    int32_t TriangleCount  = 0;    // [-] - leaf: span length; internal: 0
    int32_t LeftDownLink   = -1;   // [-] - internal: left element; leaf: -1
    int32_t RightDownLink  = -1;   // [-] - internal: right element; leaf: -1
};

// 📝 The triangle bounding volume: the bake triangles, the median-split tree over them, and the leaf-ordered index list the traversals
//    walk. Struct + free-function style — data plus its queries, no method-heavy class. Built by the CPU baker, uploaded by the GPU dispatch.
struct TriangleRayVolume
{
    std::vector<RayTriangle>        Triangles;
    std::vector<RayBoundaryElement> Elements;
    std::vector<int32_t>            OrderedIndices;
};

} // namespace Frontier

#endif
