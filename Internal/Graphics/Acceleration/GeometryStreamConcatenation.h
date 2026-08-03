/*==============================================================================================================================================
                                                     GEOMETRYSTREAMCONCATENATION.H
==============================================================================================================================================*/
// 🧩 Folds several meshes' RenderVertexStreams into ONE vertex run and ONE index run, recording where each mesh landed. The trace binds a single
//    geometry pair no matter how many distinct meshes the world holds, which is what lets a GeometryArenaSlice name any mesh's triangles at all:
//    a slice stores VertexOffset / IndexOffset as absolute positions in a SHARED stream, so a mesh living in its own private buffer is unreachable
//    by construction — not merely unbound. This is the host-side fold only; the device claim stays with ConstructPolygonBufferAllocation, which
//    already carries STORAGE usage on both buffers for the software micro-raster and therefore needs no widening for the trace.
//
//    📝 INDICES ARE REBASED ON APPEND, AND THAT IS THE ENTIRE POINT OF THIS FILE. Each mesh's indices are authored against its own vertex run
//       starting at 0, so appending mesh N's indices verbatim would address mesh 0's vertices. Every appended index has the mesh's VertexOffset
//       added to it, which is what makes one draw over the merged buffer equivalent to the separate draws it replaces.
//
//    ⚠️ APPEND ORDER IS THE ORDINAL ORDER AND IS LOAD-BEARING. The Nth appended mesh is mesh ordinal N forever after, and an instance names its
//       mesh through SuzanneSceneInstance::MeshOrdinal. Appending conditionally — skipping a mesh whose document failed to load, say — shifts every
//       later ordinal, so a scene that merely loaded fewer meshes traces against the wrong ones. Append unconditionally and gate elsewhere.

#pragma once
#ifndef FRONTIER_GRAPHICS_ACCELERATION_GEOMETRYSTREAMCONCATENATION_H
#define FRONTIER_GRAPHICS_ACCELERATION_GEOMETRYSTREAMCONCATENATION_H

#include "Authoring/Geometry/Modeling/PolygonCluster.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Where one mesh landed inside the merged run. These are the two values a GeometryArenaSlice carries verbatim, in the SAME unit the arena uses:
//    ELEMENTS, not bytes — VertexOffset counts RenderVertices and IndexOffset counts uint32 indices. The raster needs the same pair to keep drawing
//    each mesh as its own draw call out of the shared buffers (vkCmdDrawIndexed's firstIndex / vertexOffset).
struct GeometryStreamPlacement
{
    uint32_t VertexOffset = 0;   // [-] - first RenderVertex of this mesh within the merged vertex run
    uint32_t VertexCount  = 0;   // [-] - RenderVertices this mesh contributed
    uint32_t IndexOffset  = 0;   // [-] - first index of this mesh within the merged index run
    uint32_t IndexCount   = 0;   // [-] - indices this mesh contributed; the draw count for this mesh alone
};

// 📝 The accumulating merged stream plus one placement per appended mesh, indexed by the ordinal AppendGeometryStream hands back. Merged is a plain
//    RenderVertexStream so it feeds ConstructPolygonBufferAllocation unchanged — no second upload path, no second vertex format.
struct GeometryStreamConcatenation
{
    RenderVertexStream                  Merged;       // [-] - the single vertex + index run every appended mesh lives in
    std::vector<GeometryStreamPlacement> Placements;  // [-] - one per appended mesh, indexed by mesh ordinal
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Clear the concatenation to empty, ready to accumulate. Safe on a never-initialized value.
void ResetGeometryStreamConcatenation(GeometryStreamConcatenation& Concatenation);

// Append one mesh's stream to the merged run, rebasing its indices by the mesh's vertex offset, and hand back the ordinal that names it.
// The ordinal is simply the append count, so the FIRST appended mesh is ordinal 0.
//
// An EMPTY stream is appended successfully as a zero-extent placement and still consumes an ordinal. That is deliberate: a mesh whose document
// failed to load must not renumber the meshes after it (see the header's ordering warning), and a zero IndexCount is already the "draw nothing"
// case every raster path gates on. Returns false only when the stream is internally malformed — an index count that is not a multiple of three,
// or an index that addresses past its own vertex run, either of which would corrupt every LATER mesh's triangles once rebased.
bool AppendGeometryStream(GeometryStreamConcatenation& Concatenation,
                          const RenderVertexStream&    Stream,
                          uint32_t&                    OutMeshOrdinal);

// The placement recorded for a mesh ordinal, or a zero-extent placement when the ordinal is out of range.
GeometryStreamPlacement RetrieveGeometryStreamPlacement(const GeometryStreamConcatenation& Concatenation, uint32_t MeshOrdinal);

} // namespace Frontier

#endif
