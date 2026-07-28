/*==============================================================================================================================================
                                                          WORKSPACEDOCUMENTREGISTER.H
==============================================================================================================================================*/
// 🧩 The bridge that lifts a decoded WorkspaceDocument into the live scene directory: one placed WorkspaceObject becomes one RecordEntry, spawned
//    under the caller's enclosure (or under another object it names via EnclosureIndex), carrying its title, TRS placement, and a classification.
//    It returns the object-index → RecordToken table so the renderer can map each shared-geometry instance back to the outliner row that stands for
//    it. Pure directory population: no geometry upload, no Vulkan — the GPU stream is built separately from the same document via the existing
//    ConstructRenderVertexStream. Reuses SpawnInto + ResolveEntry; it invents no new tree vocabulary.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_SCENE_WORKSPACEDOCUMENTREGISTER_H
#define FRONTIER_ENGINECONTEXT_SCENE_WORKSPACEDOCUMENTREGISTER_H

#include <cstdint>
#include <vector>

#include "Authoring/Geometry/Interchange/WorkspaceDocumentEncoder.h"
#include "EngineContext/MicroUtils/RecordToken.h"
#include "EngineContext/Scene/SceneExtension.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The result of registering a document: a token per document object (Tokens[i] names the RecordEntry spawned for Document.Objects[i]), the
//    enclosure the whole set landed under, and how many entries were actually spawned. A refused spawn (population ceiling / stale enclosure)
//    leaves that slot NullRecordToken and drops SpawnedCount below Objects.size(), so the caller can detect a partial registration.
struct WorkspaceRegistration
{
    std::vector<RecordToken> Tokens        = {};                // [-]   - Tokens[i] == entry for Document.Objects[i] (null == refused)
    RecordToken              RootEnclosure  = NullRecordToken;  // [-]   - the enclosure the document's root objects were spawned under
    uint32_t                 SpawnedCount   = 0u;               // [-]   - count of entries actually spawned (== Objects.size() on full success)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Spawn one RecordEntry per Document.Objects entry into Scene's directory, under DestinationEnclosure (NullRecordToken == top level). Each entry
// takes the object's Title, LocalPlacement, and a classification of ClassificationIndex (the caller's registered PolygonComplex slot; 0 == the
// unclassified folder). An object whose EnclosureIndex names an earlier object is spawned under that object's freshly-minted entry instead of the
// document root, so a nested document reconstructs its nesting — this REQUIRES enclosing objects to precede the objects they enclose in
// Document.Objects (the writer guarantees this by emitting in enclosure order); an object naming a not-yet-spawned enclosure falls back to the
// document root. Returns the object-index → token table (see WorkspaceRegistration). Reserves directory backing for the whole batch up front.
[[nodiscard]] WorkspaceRegistration RegisterWorkspaceDocument(SceneExtension&          Scene,
                                                              const WorkspaceDocument& Document,
                                                              const RecordToken&       DestinationEnclosure,
                                                              uint32_t                 ClassificationIndex);

} // namespace Frontier

#endif
