/*============================================================================================================================================
                                                         WORKSPACEDOCUMENTENCODER.H
============================================================================================================================================*/
// 🧩 The saved-scene document: the on-disk TOML form of what the live SceneDirectory holds. A WorkspaceDocument is a shared table of geometry
//    blocks (each an engine-native PolygonCluster, stored ONCE) plus a flat list of placed objects that reference a geometry block by index and
//    carry their own title, local placement, tint, and source nesting. This is the honest shape for the P2b Suzanne scenes — N placed heads over
//    one shared head — so the file stays small and the renderer keeps its single-upload / instanced-draw path. Standalone: unlike the model
//    decoders (glTF / OBJ / FBX) this is NOT on the extension dispatch — a document is a whole scene, not one imported model. The reader / writer
//    reuse PolygonCluster (Authoring/Geometry/Modeling) and LocalPlacement (EngineContext/Scene); no geometry type is duplicated here.

#pragma once
#ifndef FRONTIER_AUTHORING_GEOMETRY_INTERCHANGE_WORKSPACEDOCUMENTENCODER_H
#define FRONTIER_AUTHORING_GEOMETRY_INTERCHANGE_WORKSPACEDOCUMENTENCODER_H

#include "PolygonCluster.h"
#include "EngineContext/Scene/RecordEntry.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONTRACT
//------------------------------------------------------------------------------------------------------------------------

// 📝 One shared geometry block: an engine-native authoring polygon stored ONCE and referenced by any number of placed objects. The document holds
//    a table of these; a placed object names one by its index. Kept a thin wrapper (rather than a bare PolygonCluster) so a block can later grow a
//    stable title / origin hint without disturbing the object rows that reference it.
struct WorkspaceGeometryBlock
{
    PolygonCluster Geometry = {};   // [-] - shared authoring polygon (double precision, topology preserved); derived to a RenderVertexStream on load
    std::string    Title    = {};   // [-] - block name (for diagnostics / a future geometry outliner group; empty = unnamed)
};

// 📝 One placed object: a reference into the shared geometry table plus everything that makes it a distinct outliner row — its own title, local
//    placement (TRS in enclosure space, reused from RecordEntry), a debug tint, a surface-material reference, and the source nesting
//    (EnclosureIndex names another object's slot, or -1 for a document-root object). One WorkspaceObject becomes one RecordEntry when the document
//    is registered into the scene.
//
//    Tint and MaterialId are NOT alternatives — they feed different passes. Tint is the flat debug colour the visibility resolve's id view reads;
//    MaterialId names the SurfacePresetTable record the deferred shade pass evaluates a real BRDF from. A document authored before materials
//    existed simply carries 0 (the flat Standard record), which is why the key decodes with a default rather than being required.
struct WorkspaceObject
{
    uint32_t       GeometryIndex  = 0u;                  // [idx] - slot in WorkspaceDocument.Geometry this object instances
    std::string    Title          = {};                 // [-]   - outliner row title
    LocalPlacement Placement       = {};                // [-]   - TRS in enclosure space (reused from EngineContext/Scene)
    float          Tint[3]         = { 1.0f, 1.0f, 1.0f };// [-]  - linear-RGB debug tint the visibility resolve reads
    uint32_t       MaterialId      = 0u;                // [idx] - SurfacePresetTable record the shade pass evaluates (0 = flat Standard)
    int32_t        EnclosureIndex  = -1;                // [-]   - enclosing object's slot in Objects, or -1 for a document-root object
};

// 📝 The whole document: the shared geometry table and the placed objects that reference it. Encode writes it as TOML; Decode reconstructs it.
//    The two vectors are independent — Objects[i].GeometryIndex must address Geometry, which Decode validates before returning.
struct WorkspaceDocument
{
    std::vector<WorkspaceGeometryBlock> Geometry = {};   // [-] - shared geometry blocks, each stored once
    std::vector<WorkspaceObject>        Objects  = {};   // [-] - placed objects referencing the blocks
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Write Document to Path as TOML (a "FRWSDOC" magic + version-1 header, the shared [[geometry]] blocks as flat number arrays, then the [[object]]
// rows referencing them). Returns false on an unwritable path or an internal inconsistency (an object referencing a missing block). Human-readable
// and diffable by design — this is authored once by the writer tool, then only ever loaded.
bool EncodeWorkspaceDocument(const char* Path, const WorkspaceDocument& Document);

// Read a TOML document at Path into Result (cleared first). Validates the magic + version, that every array length is self-consistent, and that
// every object's GeometryIndex addresses a present block. Returns false with Result emptied on a missing / malformed file, a version mismatch, or a
// dangling geometry reference; a well-formed file yields a document ready for RegisterWorkspaceDocument + ConstructRenderVertexStream.
bool DecodeWorkspaceDocument(const char* Path, WorkspaceDocument& Result);

} // namespace Frontier

#endif
