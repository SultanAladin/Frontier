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
#include "Graphics/Scene/SuzanneScene.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Decode the .wsdoc at Path and produce the two things the raster needs: the shared geometry block's triangulated GPU stream (Geometry, from
// block 0 via ConstructDisplayPolygons) and one SuzanneSceneInstance per placed object referencing block 0 (Instances — TRS recomposed into a
// column-major model matrix + a rotation-only normal basis, tinted, identity = placement ordinal). Document, when non-null, receives the whole
// decoded document so the caller can register it into the SceneDirectory. TriangleSourceFace, when non-null, receives one entry per emitted triangle
// (parallel to Geometry.Indices in groups of three): the ORIGINATING editable-face ordinal from the triangulation provenance, so a debug view can
// tell an added triangulation diagonal (same face on both sides) from a real topology edge (differing faces) — the ngon/quad/tri the head was
// authored with. Returns false (all outputs cleared) on a missing / malformed file, an empty document, or a geometry block that fails to
// triangulate. Objects referencing a block other than 0 are skipped (single-block runtime path).
bool LoadWorkspaceScene(const char*                        Path,
                        RenderVertexStream&                Geometry,
                        std::vector<SuzanneSceneInstance>& Instances,
                        WorkspaceDocument*                 Document,
                        std::vector<uint32_t>*             TriangleSourceFace = nullptr);

// Decode a STANDALONE document whose single geometry block is drawn as a second mesh alongside the main scene (the checkered floor). Identical to
// LoadWorkspaceScene in mechanics — triangulate block 0 into Geometry, recompose every block-0 object into a SuzanneSceneInstance — but named apart
// because the caller draws it into the SHARED visibility buffer as a distinct mesh (its own vertex/index buffers + instance set), not as more heads.
// The floor doc holds one grey object at identity, so Instances is normally length 1. Returns false (outputs cleared) on a missing / malformed file,
// an empty document, or a block that fails to triangulate. This is the runtime side of "the floor is a real mesh baked into its own .wsdoc".
bool LoadFloorDocument(const char*                        Path,
                       RenderVertexStream&                Geometry,
                       std::vector<SuzanneSceneInstance>& Instances);

} // namespace Frontier

#endif
