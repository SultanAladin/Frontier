/*==============================================================================================================================================
                                                         WORKSPACEDOCUMENTREGISTER.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the document → directory bridge. Walk the document's placed objects in order; for each, resolve the enclosure (an
//    earlier object it names, else the caller's root), SpawnInto that enclosure at the tail, then ResolveEntry to write its TRS placement,
//    classification, and outliner-visible presentation onto the freshly-minted RecordEntry. The object-index → token table is filled as we go so a
//    later object can enclose an earlier one. Titles/placements come straight from the document; nothing here parses TOML or touches the GPU.

#include "EngineContext/Scene/WorkspaceDocumentRegister.h"

#include "EngineContext/MicroUtils/TokenAuthentication.h"
#include "EngineContext/Scene/AdjacencyTable.h"
#include "EngineContext/Scene/RecordEntry.h"
#include "EngineContext/Scene/SceneDirectory.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

WorkspaceRegistration RegisterWorkspaceDocument(SceneExtension&          Scene,
                                                const WorkspaceDocument& Document,
                                                const RecordToken&       DestinationEnclosure,
                                                uint32_t                 ClassificationIndex)
{
    WorkspaceRegistration Registration;
    Registration.RootEnclosure = DestinationEnclosure;
    Registration.Tokens.assign(Document.Objects.size(), NullRecordToken);

    // Reserve backing for the whole batch so a bulk load grows the archive at most once.
    ReserveArchive(Scene.Directory.Archive, static_cast<uint32_t>(Document.Objects.size()));

    const InsertSite TailSite{ InsertPlacement::Last, NullRecordToken };

    for (size_t ObjectIterator = 0; ObjectIterator < Document.Objects.size(); ++ObjectIterator)
    {
        const WorkspaceObject& Object = Document.Objects[ObjectIterator];

        // Resolve the enclosure: an earlier object this one names (already spawned), else the document root. A dangling or
        // not-yet-spawned enclosure falls back to the root rather than orphaning the entry.
        RecordToken Enclosure = DestinationEnclosure;
        if (Object.EnclosureIndex >= 0 &&
            static_cast<size_t>(Object.EnclosureIndex) < ObjectIterator &&
            PopulatedToken(Registration.Tokens[Object.EnclosureIndex]))
        {
            Enclosure = Registration.Tokens[Object.EnclosureIndex];
        }

        const RecordToken Token = SpawnInto(Scene.Directory, Object.Title, Enclosure, TailSite);
        if (!PopulatedToken(Token))
            continue;   // population ceiling / stale enclosure — leave this slot null, keep going

        // Write the object's payload onto the freshly-minted entry. The pointer is valid until the next store mutation; we use
        // it immediately and never hold it across the next spawn.
        if (RecordEntry* Entry = ResolveEntry(Scene.Directory.Archive, Token))
        {
            Entry->Placement                        = Object.Placement;
            Entry->Classification.ClassificationIndex = ClassificationIndex;
            Entry->Presentation                     = PresentationCategory::Displayed |
                                                      PresentationCategory::Selectable |
                                                      PresentationCategory::VisibleInOutliner;
        }

        Registration.Tokens[ObjectIterator] = Token;
        ++Registration.SpawnedCount;
    }

    return Registration;
}

} // namespace Frontier
