/*==========================================================================================================================================
                                                               RECORD ENTRY
                                     ------------------------------------------------------------------
          Base of every scene object: the constructor registers it, the destructor unregisters it — nothing else can.
==========================================================================================================================================*/

// 🧩 Base of every scene object: the constructor registers it, the destructor unregisters it — nothing else can.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "RecordClassification.h"
#include "RecordToken.h"

#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    #include "RecordEditorSpecification.h"
#endif

#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      RECORD ENTRY
//------------------------------------------------------------------------------------------------------------------------

// 📝 Plan §6: construction issues a token, inserts into the RecordStore (every profile) and — Editor/Development
//    only — registers with the SceneDirectory so the object appears in the Outliner. Destruction reverses both.
//    Forgetting to register is impossible; there is no other registration API. Display name and Outliner nesting
//    are directory-side metadata set after construction — the base class never guesses a parent.
//    ⚠️ Records are constructed on the main thread in editor builds; job threads never construct a RecordEntry.
//    ⚠️ Records must not have static storage duration (the store/directory are function-local statics).
class RecordEntry
{
  public:
    explicit RecordEntry(RecordClassification Classification);
    virtual ~RecordEntry();

    RecordEntry(const RecordEntry&)            = delete;
    RecordEntry& operator=(const RecordEntry&) = delete;
    RecordEntry(RecordEntry&&)                 = delete;
    RecordEntry& operator=(RecordEntry&&)      = delete;

    [[nodiscard]] RecordToken          Token() const noexcept { return IssuedToken; }                  // [-] - Issued at construction
    [[nodiscard]] RecordClassification Classification() const noexcept { return EntryClassification; }   // [-] - Outliner category + page

#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
  protected:
    // 📝 Editor construction (plan §5/§6, EditorSceneInspection step 3): a derived record's constructor calls
    //    these inside its own `#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)`
    //    block to build its Inspector menu/editor specification. Controls bind to the record itself, so the
    //    Inspector edits the live object. Shipping declares none of this and pays nothing for it.
    void DeclareEditorDescription(std::string Description);
    void DeclareEditorIdentity(std::string TypeName, std::string GroupName);
    void DeclareEditorCard(std::string Title, std::string Subtitle = {}, int Index = -1, std::string Glyph = {});
    void DeclareEditorToggle(std::string Key, std::string Label, std::string Note, bool TilePresentation,
                             std::function<bool()> Query, std::function<void(bool)> Configure,
                             std::string Glyph = {});
    void DeclareEditorSlider(std::string Key, std::string Label, std::string Unit, double Minimum, double Maximum,
                             double Step, std::function<double()> Query, std::function<void(double)> Configure,
                             int Digits = 2, std::string RangeLabelMinimum = {}, std::string RangeLabelMaximum = {});
    void DeclareEditorSegmented(std::string Key, std::string Label, std::vector<std::string> Options,
                                std::function<size_t()> Query, std::function<void(size_t)> Configure);
    void DeclareEditorSelect(std::string Key, std::string Label, std::vector<std::string> Options,
                             std::function<size_t()> Query, std::function<void(size_t)> Configure);
    void DeclareEditorMetric(std::string Key, std::function<std::string()> QueryValue, std::string Unit,
                             std::function<std::pair<std::string, std::string>()> QueryPill = {});
    void DeclareEditorMuted(std::string Text);
    void DeclareEditorStats(std::string Key, std::vector<RecordEditorStatItem> Items);
    void DeclareEditorReadout(std::string Key, std::string Label, std::function<std::string()> Query);
    void DeclareEditorTextBlock(std::string Key, std::function<std::string()> Query);
    void DeclareEditorAction(std::string Key, std::string Label, std::function<void()> Trigger);
#endif

  private:
    RecordToken          IssuedToken;           // [-] - From TokenIssuer; authenticated through RecordStore
    RecordClassification EntryClassification;   // [-] - Fixed for the entry's whole lifetime
};

} // namespace Frontier
