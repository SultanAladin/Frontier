/*==========================================================================================================================================
                                                     RECORD EDITOR SPECIFICATION
                                     ------------------------------------------------------------------
        Editor-only per-record Inspector construction: a record declares its own cards and controls in its
        constructor (Editor/Development profiles); Shipping builds contain none of this (plan §5/§6).
==========================================================================================================================================*/

// 🧩 Editor-only per-record Inspector construction: cards and bound controls declared by each record itself.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"

#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)

#include "RecordToken.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Control vocabulary of the HTML Inspector (EditorShell.js + FrontierEditor.css): property-switch tiles and
//    switch rows are both toggles; a control line is a slider with a number field; segmented is the native
//    Dynamic|Baked pair; readouts and actions appear in cards and action rows.
enum class RecordEditorControlCategory : uint8_t
{
    Toggle,      // [-] - Switch row, or property-switch tile when TilePresentation is set
    Slider,      // [-] - SliderControl.css .frontier-slider: head (label + value capsule), 22 px track
    Segmented,   // [-] - Segmented control over Options
    Select,      // [-] - .select-field dropdown over Options (presentation cycles on click in the native kit)
    Metric,      // [-] - .metric: 38 px Light value + unit, optional .small-pill
    Muted,       // [-] - .muted paragraph (Text)
    Stats,       // [-] - .stats grid over StatItems
    Readout,     // [-] - Live text readout, refreshed by the panel each Synchronize
    TextBlock,   // [-] - .readout <pre>: multi-line block, QueryReadout returns lines joined by '\n'
    Action       // [-] - Action-row button
};

struct RecordEditorStatItem
{
    std::string                  Label;                               // [text]
    std::string                  Unit;                                // [text]
    std::function<std::string()> Query;
};

struct RecordEditorControl
{
    std::string                   Key;                                  // [text] - Stable per-record key, e.g. "vehicle.throttle"
    std::string                   Label;                                // [text]
    std::string                   Note;                                 // [text] - Small line under a switch-row label
    std::string                   Unit;                                 // [text] - Number-field unit (slider)
    std::string                   Text;                                 // [text] - Muted paragraph body
    std::string                   Glyph;                                // [text] - Tile icon name (EditorIcons vocabulary)
    RecordEditorControlCategory   Category = RecordEditorControlCategory::Readout;
    bool                          TilePresentation = false;             // [-] - Toggle only: property-switch tile
    double                        Minimum = 0.0;                        // Slider bounds / step
    double                        Maximum = 1.0;
    double                        Step    = 0.0;                        // 0 = continuous
    int                           Digits  = 2;                          // [digits] - Slider value / metric formatting
    std::string                   RangeLabelMinimum;                    // [text] - .range-labels left (empty = none)
    std::string                   RangeLabelMaximum;                    // [text] - .range-labels right
    std::vector<std::string>      Options;                              // Segmented / Select choices
    std::vector<RecordEditorStatItem> StatItems;                        // Stats cells
    std::function<bool()>         QueryToggle;
    std::function<void(bool)>     ConfigureToggle;
    std::function<double()>       QuerySlider;
    std::function<void(double)>   ConfigureSlider;
    std::function<size_t()>       QuerySegmented;
    std::function<void(size_t)>   ConfigureSegmented;
    std::function<std::string()>  QueryReadout;
    std::function<std::pair<std::string, std::string>()> QueryPill;     // Metric pill: (caption, "" | "live" | "warm")
    std::function<void()>         TriggerAction;
};

struct RecordEditorCard
{
    std::string                    Title;                               // [text] - .card-heading
    std::string                    Subtitle;                            // [text] - .card-subtitle (may be empty)
    std::string                    Glyph;                               // [text] - Heading glyph (EditorIcons vocabulary)
    int                            Index = -1;                          // [-] - Explicit .card-index (HTML Index option); -1 = none
    std::vector<RecordEditorControl> Controls;
};

// What one record builds for the Inspector in its constructor: a description line plus ordered cards.
// Controls hold live bindings to the record, so an edit writes the record itself — never a panel-side copy.
struct RecordEditorSpecification
{
    std::string                 Description;                            // [text] - Right side of .section-label
    std::string                 TypeName;                               // [text] - HTML entity Type (.eyebrow); empty = classification
    std::string                 GroupName;                              // [text] - HTML entity Group (.crumbs); empty = classification
    std::vector<RecordEditorCard> Cards;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  RECORD EDITOR REGISTRY
//------------------------------------------------------------------------------------------------------------------------

// 📝 Directory-side store for editor specifications, keyed by RecordToken exactly like SceneDirectory metadata:
//    RecordEntry's constructor-side Declare* calls fill it, its destructor removes it. It exists only under
//    Editor/Development profiles; Shipping links none of it.
class RecordEditorRegistry
{
  public:
    [[nodiscard]] static RecordEditorRegistry& Instance();

    RecordEditorRegistry()                                   = default;
    RecordEditorRegistry(const RecordEditorRegistry&)        = delete;
    RecordEditorRegistry& operator=(const RecordEditorRegistry&) = delete;

    void DeclareDescription(RecordToken Token, std::string Description);
    void DeclareIdentity(RecordToken Token, std::string TypeName, std::string GroupName);
    void DeclareCard(RecordToken Token, std::string Title, std::string Subtitle, int Index = -1, std::string Glyph = {});
    void DeclareControl(RecordToken Token, RecordEditorControl Control);

    void Unregister(RecordToken Token);   // Called only by RecordEntry's destructor

    [[nodiscard]] bool        QueryRegistered(RecordToken Token) const;
    [[nodiscard]] size_t      QueryCount() const;
    // Snapshot copy (bindings included) for the Inspector view-model; nullptr when the record declared none.
    [[nodiscard]] std::shared_ptr<const RecordEditorSpecification> QuerySpecification(RecordToken Token) const;

  private:
    mutable std::mutex                                                   Mutex;
    std::unordered_map<uint64_t, std::shared_ptr<RecordEditorSpecification>> Specifications;
};

} // namespace Frontier

#endif // FRONTIER_EDITOR_PROFILE || FRONTIER_DEVELOPMENT_PROFILE
