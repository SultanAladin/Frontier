/*==============================================================================================================================================
                                                                MESHASSET.H
==============================================================================================================================================*/
// 🧩 A tiny load boundary that turns one of the engine's pre-parsed reference-geometry JSON files (the flat { vertexCount, triCount, positions,
//    normals, indices } shape produced from ReferenceGeometry/Suzzane.obj and its Catmull-Clark-2x subdivision) into the renderer's stride-32
//    RenderVertexStream — the one presentation contract BufferAllocation consumes. It is the raster's geometry source at P2b: no OBJ parser, no
//    authoring PolygonComplex, just position + normal + a zeroed texcoord interleaved into RenderVertex, plus the uint32 triangle-list indices.
//    The JSON is trusted engine content (not user input); the reader is a flat number-array scanner, not a general JSON parser.

#pragma once
#ifndef FRONTIER_GRAPHICS_SCENE_MESHASSET_H
#define FRONTIER_GRAPHICS_SCENE_MESHASSET_H

#include "Authoring/Geometry/Modeling/PolygonCluster.h"

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Load a reference-geometry JSON file at FilePath into Result (a RenderVertexStream: interleaved stride-32 vertices + triangle-list indices).
// Expects the engine's flat shape — "positions" (vertexCount*3 floats), "normals" (vertexCount*3 floats, optional; zeroed if absent), and
// "indices" (triCount*3 uint). TextureCoordinate is left zero (the reference meshes carry none). Returns false with Result emptied on a missing
// file, a truncated array, or an index out of range; a well-formed file yields a stream ready for ConstructPolygonBufferAllocation.
bool LoadReferenceMeshAsset(const char* FilePath, RenderVertexStream& Result);

} // namespace Frontier

#endif
