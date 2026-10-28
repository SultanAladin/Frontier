/*==========================================================================================================================================
                                                               SCENE RECORD
                                     ------------------------------------------------------------------
             The concrete empty scene object: what the editor spawns when no richer record type applies yet.
==========================================================================================================================================*/

// 🧩 The concrete empty scene object: what the editor spawns when no richer record type applies yet.

#pragma once

#include "RecordEntry.h"

#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    #include "SceneDirectory.h"
#endif

#include <string>

namespace Frontier
{

// 📝 A leaf RecordEntry with no payload of its own — the Outliner's "empty object". Richer scene objects
//    (vehicle, geometry, lights…) derive from RecordEntry directly as their phases land; until then, editor
//    spawn requests and scene placeholders are SceneRecords named through the SceneDirectory.
class SceneRecord final : public RecordEntry
{
  public:
    explicit SceneRecord(RecordClassification Classification) : RecordEntry(Classification)
    {
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
        // 📝 Editor construction: every record carries its own Inspector specification (plan §6 pattern).
        DeclareEditorDescription("Spawned record");
        DeclareEditorToggle("record.visible", "Visible", "Outliner eye and viewport visibility", true,
                            [this]() { return SceneDirectory::Instance().QueryVisible(Token()); },
                            [this](bool Visible) { (void)SceneDirectory::Instance().ConfigureVisible(Token(), Visible); },
                            "Eye");
        DeclareEditorCard("Record", "");
        DeclareEditorReadout("record.classification", "Classification",
                             [this]() { return std::string(RecordClassificationName(RecordEntry::Classification())); });
        DeclareEditorReadout("record.token", "Token",
                             [this]() { return std::to_string(Token().Value); });
#endif
    }
};

} // namespace Frontier
