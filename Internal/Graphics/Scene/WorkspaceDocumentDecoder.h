/*==============================================================================================================================================
                                                          WORKSPACEDOCUMENTDECODER.H
==============================================================================================================================================*/
// 🧩 The runtime bridge from a saved WorkspaceDocument (.wsdoc) to what the visibility raster draws. It DECODES the document (reusing
//    DecodeWorkspaceDocument), derives the shared geometry block's GPU stream through the ENGINE'S OWN ConstructRenderVertexStream (no bespoke
//    scanner — that lived only in the writer tool), and recomposes each placed object's stored TRS back into the raster's per-instance
//    SuzanneSceneInstance (model matrix + rotation-only normal basis + tint + running identity). This replaces the hardcoded BuildSuzanneScene +
//    reference-JSON path: the renderer now LOADS its scene instead of constructing it in C++. Single-shared-block by design — both Suzanne scenes
//    are N heads over one block; a document with more than one geometry block loads block 0 and reports the rest as unsupported here.

#pragma once
#ifndef FRONTIER_GRAPHICS_SCENE_WORKSPACEDOCUMENTDECODER_H
#define FRONTIER_GRAPHICS_SCENE_WORKSPACEDOCUMENTDECODER_H

#include "Authoring/Geometry/Interchange/WorkspaceDocumentEncoder.h"
#include "Authoring/Geometry/Modeling/PolygonCluster.h"
#include "Graphics/Acceleration/GeometryTreeBuild.h"
#include "Graphics/Scene/SuzanneScene.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 "This triangle side is a fan diagonal, not an authored edge." Triangulating an ngon invents interior sides that exist only in the display mesh, and
//    a component overlay must never let one be selected — picking a diagonal would hand the modelling tools an edge the model does not have. The CPU
//    picker states the same rule at RayPickIntersection.cpp:126 (a side is real only if its key is in AdjacencyIndex.EdgeFaces); this sentinel is how
//    that verdict is carried to the GPU, one slot per triangle side.
constexpr uint32_t InvalidAuthoredEdge = 0xFFFFFFFFu;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The authored-topology provenance of a triangulated scene: for every emitted display triangle, WHICH authored face / corner vertices / loop edges it
//    came from. This is what lets a component overlay address the ngons and quads the model was BUILT with instead of the triangles it is drawn with —
//    hovering a quad highlights the whole quad, and its interior fan diagonal is neither drawn nor selectable.
//
// ⚠️ CornerVertex is in a DIFFERENT INDEX SPACE from the render stream's indices, and conflating the two is the bug this struct exists to prevent.
//    ConstructDisplayPolygons expands vertices PER CORNER (DisplayPolygonAssembly.cpp: one Stream.Vertices entry per face corner), so a cluster vertex
//    shared by four quads becomes four distinct render vertices. The render index is therefore a corner slot, useless as a component identity — the
//    same authored vertex wears several of them, so keying on it lights only one face's copy. CornerVertex is the CLUSTER vertex index
//    (FaceVertexIndices[SourceCorner]), which is shared, stable, and the thing a modeller means by "that vertex".
//
// 📝 All three tables are parallel to the triangle list: entry T describes the triangle whose render indices are Indices[3T .. 3T+2]. SourceFace holds
//    one entry per triangle; CornerVertex and SideEdge hold THREE per triangle (slot S of triangle T at [3T + S]). SideEdge slot S is the side joining
//    corner S to corner (S+1)%3, matching the winding order, so a shader walking sides in that order indexes it directly.
struct AuthoredTopologyMap
{
    std::vector<uint32_t> SourceFace   = {};   // [-] - per triangle: authored face ordinal it tessellates part of
    std::vector<uint32_t> CornerVertex = {};   // [-] - per triangle corner (3 per triangle): CLUSTER vertex index, winding order
    std::vector<uint32_t> SideEdge     = {};   // [-] - per triangle side  (3 per triangle): authored edge ordinal, or InvalidAuthoredEdge for a fan diagonal
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 First partition identity the floor mesh's instances take. Every mesh drawn into the SHARED visibility buffer must occupy a DISJOINT partition
//    range, because a consumer unpacks the identity's partition ordinal and indexes that mesh's instance buffer with it — two meshes both starting at
//    0 would make "partition 0" ambiguous (head 0 or floor slab 0?) and shade one with the other's record. The heads take the low range from 0; the
//    floor is based high, far above any plausible head count (the pyramid-stress worst case is 910). The partition field is 12 bits
//    (VisibilityRaster.frag), so identities must stay under 4096 — 2048 leaves both ranges ample room.
constexpr uint32_t FloorPartitionBase = 2048u;

// Compose the world→local matrix the ray trace enters an instance's bottom-level tree through, from the same LocalPlacement the model matrix is
// built from, into OutInverse[16] (column-major, element index Column*4 + Row). Inverted ANALYTICALLY from the TRS — S⁻¹ * Rᵀ * T⁻¹ — rather than
// by a numeric inversion of the composed matrix, so the pair is exact by construction. Exposed because the load path is not the only consumer: the
// validation gate judges Model * InverseModel against identity, and it must judge the SHIPPED composer rather than a transcription of it.
//
// 🔴 A ZERO OR NEAR-ZERO SCALE AXIS IS CLAMPED TO A FINITE RECIPROCAL, NOT ALLOWED TO PRODUCE inf/NaN. A NaN here propagates into every ray
//    entering the instance and reads as a black or missing object with no malformed data to catch, so a degenerate axis collapses the instance
//    visually — which is what the author asked for — instead of poisoning the trace.
void ComposeInverseModelMatrix(const LocalPlacement& Placement, float OutInverse[16]);

// Build the bottom-level tree over an already-triangulated render stream, in the stream's own LOCAL space, into Result. De-interleaves the
// positions out of RenderVertex (a 32-byte interleaved record) into the tightly packed XYZ run BuildGeometryTree takes, then delegates. Returns
// false with Result cleared on an empty stream or an index run that is not a whole number of triangles.
//
// 🔴 THE STREAM MUST BE THE ONE THE INSTANCES SHARE, NOT ONE INSTANCE'S WORLD-SPACE COPY. Every instance of a mesh walks this one tree and enters
//    it through its own InverseModel, which is the entire reason a CPU build is affordable here — see GeometryTreeBuild.h's header. A tree built
//    over transformed vertices is correct for exactly one instance and silently wrong for every other.
bool BuildGeometryTreeForStream(const RenderVertexStream&  Geometry,
                                const GeometryTreeOptions& Options,
                                GeometryTree&              Result);

// Decode the .wsdoc at Path and produce the two things the raster needs: the shared geometry block's triangulated GPU stream (Geometry, from
// block 0 via ConstructDisplayPolygons) and one SuzanneSceneInstance per placed object referencing block 0 (Instances — TRS recomposed into a
// column-major model matrix + a rotation-only normal basis, tinted, identity = PartitionBase + placement ordinal). Document, when non-null, receives
// the whole decoded document so the caller can register it into the SceneDirectory. TriangleSourceFace, when non-null, receives one entry per emitted
// triangle (parallel to Geometry.Indices in groups of three): the ORIGINATING editable-face ordinal from the triangulation provenance, so a debug view
// can tell an added triangulation diagonal (same face on both sides) from a real topology edge (differing faces) — the ngon/quad/tri the head was
// authored with. PartitionBase offsets every emitted identity so meshes sharing one visibility buffer stay in disjoint ranges (see
// FloorPartitionBase); pass 0 for the primary scene. Topology, when non-null, receives the fuller authored-topology provenance the component overlay
// needs (per-triangle authored face + corner vertices + loop-edge ordinals — see AuthoredTopologyMap); it supersedes TriangleSourceFace, whose
// SourceFace table it also carries, and resolving it additionally builds the adjacency so fan diagonals can be told from real edges. Tree, when
// non-null, additionally receives the bottom-level acceleration tree built over block 0's local-space triangles (BuildGeometryTreeForStream), which
// the caller appends to a GeometryArenaSubmission and whose returned mesh ordinal it writes back over every emitted instance's MeshOrdinal. Returns
// false (all outputs cleared) on a missing / malformed file, an empty document, or a geometry block that fails to triangulate. Objects referencing a
// block other than 0 are skipped (single-block runtime path).
//
// ⚠️ EVERY EMITTED INSTANCE LEAVES HERE WITH MeshOrdinal 0, WHICH IS A VALID ORDINAL AND THEREFORE NOT SELF-ANNOUNCING. This function cannot know
//    the ordinal: it is assigned by AppendGeometryTreeToArena, which the caller owns. A caller that requests Tree and forgets to write the returned
//    ordinal back has every mesh in the scene tracing against whichever mesh happens to be arena slot 0 — geometry that renders correctly under the
//    raster and traces against the wrong triangles.
bool LoadWorkspaceScene(const char*                        Path,
                        RenderVertexStream&                Geometry,
                        std::vector<SuzanneSceneInstance>& Instances,
                        WorkspaceDocument*                 Document,
                        std::vector<uint32_t>*             TriangleSourceFace = nullptr,
                        uint32_t                           PartitionBase      = 0u,
                        AuthoredTopologyMap*               Topology           = nullptr,
                        GeometryTree*                      Tree               = nullptr);

// Decode a STANDALONE document whose single geometry block is drawn as a second mesh alongside the main scene (the checkered floor). Identical to
// LoadWorkspaceScene in mechanics — triangulate block 0 into Geometry, recompose every block-0 object into a SuzanneSceneInstance — but named apart
// because the caller draws it into the SHARED visibility buffer as a distinct mesh (its own vertex/index buffers + instance set), not as more heads.
// The floor doc holds one grey object at identity, so Instances is normally length 1. Identities are based at FloorPartitionBase so they cannot
// collide with the heads' low range in the shared visibility buffer — a consumer that unpacks a partition ordinal subtracts the base before indexing
// this mesh's instance buffer. Returns false (outputs cleared) on a missing / malformed file, an empty document, or a block that fails to
// triangulate. This is the runtime side of "the floor is a real mesh baked into its own .wsdoc".
//
// Tree carries the same contract as LoadWorkspaceScene's: when non-null it receives the floor slab's bottom-level tree, built over the slab's
// LOCAL-space triangles, for the caller to append to the arena. The floor is a ray target like any other surface — it is the surface most indirect
// bounces actually land on — so it needs a real tree rather than being the one mesh the trace cannot see. The returned MeshOrdinal must be written
// back over every emitted instance for the same reason documented above: 0 is a valid ordinal and will silently trace against the heads.
bool LoadFloorDocument(const char*                        Path,
                       RenderVertexStream&                Geometry,
                       std::vector<SuzanneSceneInstance>& Instances,
                       GeometryTree*                      Tree = nullptr);

} // namespace Frontier

#endif
