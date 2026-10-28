/*==========================================================================================================================================
                                                      SCENE INSPECTOR PANEL
                                     ------------------------------------------------------------------
     Portable Inspector view-model: the selected record's own editor specification, resolved to live values.
==========================================================================================================================================*/

// 🧩 Portable Inspector view-model: selection identity plus the record's declared cards, resolved live.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Scene/RecordClassification.h"
#include "Engine/Scene/RecordToken.h"

#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    #include "Engine/Scene/RecordEditorSpecification.h"
#endif

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

class SceneDirectory;
class SceneDirectoryPanel;

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct SceneInspectorStatCell
{
    std::string Label;                                          // [text]
    std::string Value;                                          // [text]
    std::string Unit;                                           // [text]
};

struct SceneInspectorControl
{
    std::string Key;                                            // [text]
    std::string Label;                                          // [text]
    std::string Note;                                           // [text]
    std::string Unit;                                           // [text]
    std::string Text;                                           // [text] - Muted paragraph
    std::string Glyph;                                          // [text] - Tile icon name
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    RecordEditorControlCategory Category = RecordEditorControlCategory::Readout;
#endif
    bool                     TilePresentation = false;          // [-]
    double                   Minimum = 0.0;
    double                   Maximum = 1.0;
    double                   Step    = 0.0;
    int                      Digits  = 2;                       // [digits]
    std::string              RangeLabelMinimum;                 // [text]
    std::string              RangeLabelMaximum;                 // [text]
    std::vector<std::string> Options;
    std::vector<SceneInspectorStatCell> StatCells;
    bool                     ToggleValue    = false;            // [-]
    double                   SliderValue    = 0.0;
    size_t                   SegmentedIndex = 0;                // [choice]
    std::string              ReadoutText;                       // [text] - Readout / Metric value
    std::string              PillText;                          // [text] - Metric pill caption
    std::string              PillState;                         // [text] - "" | "live" | "warm"
};

struct SceneInspectorCard
{
    std::string                       Title;                    // [text]
    std::string                       Subtitle;                 // [text]
    std::string                       Glyph;                    // [text] - Heading glyph
    int                               Index = -1;               // [-] - Explicit card index; -1 = none
    std::vector<SceneInspectorControl> Controls;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  SCENE INSPECTOR PANEL
//------------------------------------------------------------------------------------------------------------------------

// 📝 HTML 1:1 (EditorShell.js ConstructInspector): crumbs "Inspector › <group>", object header (eyebrow =
//    classification, name, Enabled pill), PROPERTIES section, property-switch tiles, then the record's cards,
//    then the footer. The panel owns no record values: Synchronize resolves the selected record's declared
//    bindings, and every Apply* writes through those bindings to the record itself.
class SceneInspectorPanel
{
  public:
    void Synchronize(const SceneDirectory& Directory, const SceneDirectoryPanel& Outliner);

    [[nodiscard]] bool        QueryHasSelection() const { return HasSelection; }
    [[nodiscard]] RecordToken QuerySelected() const { return Selected; }
    [[nodiscard]] const std::string&           QueryName() const { return Name; }
    [[nodiscard]] RecordClassification         QueryClassification() const { return Classification; }
    [[nodiscard]] const std::string&           QueryDescription() const { return Description; }
    [[nodiscard]] const std::string&           QueryTypeName() const { return TypeName; }
    [[nodiscard]] const std::string&           QueryGroupName() const { return GroupName; }
    [[nodiscard]] bool                         QueryEnabled() const { return Enabled; }
    [[nodiscard]] const std::vector<SceneInspectorCard>& QueryCards() const { return Cards; }

    // Writes through the selected record's live bindings; false for unknown keys / stale selection.
    bool ApplyToggle(const std::string& Key, bool Value);
    bool ApplySlider(const std::string& Key, double Value);
    bool ApplySegmented(const std::string& Key, size_t Index);
    bool ApplyAction(const std::string& Key);

    // Native drawing (interface-SDK builds; an empty stub without the SDK, so portable builds keep linking).
    void ConstructInterface();

  private:
    bool                 HasSelection  = false;
    RecordToken          Selected;
    std::string          Name;                                          // [text]
    RecordClassification Classification = RecordClassification::Unclassified;
    std::string          Description;                                   // [text]
    std::string          TypeName;                                      // [text] - HTML Type (.eyebrow)
    std::string          GroupName;                                     // [text] - HTML Group (.crumbs)
    bool                 Enabled       = true;                          // [-] - Directory visibility of the record
    std::vector<SceneInspectorCard> Cards;
};

} // namespace Frontier
