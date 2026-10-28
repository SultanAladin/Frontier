// 🧩 Portable Inspector view-model: selection identity plus the record's declared cards, resolved live.

#include "Engine/Interface/SceneInspectorPanel.h"

#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Engine/Scene/SceneDirectory.h"

#include <algorithm>
#include <memory>

namespace Frontier
{

namespace
{

#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
std::shared_ptr<const RecordEditorSpecification> SpecificationFor(RecordToken Token)
{
    return RecordEditorRegistry::Instance().QuerySpecification(Token);
}

const RecordEditorControl* FindControl(const RecordEditorSpecification& Specification, const std::string& Key)
{
    for (const RecordEditorCard& Card : Specification.Cards)
    {
        for (const RecordEditorControl& Control : Card.Controls)
        {
            if (Control.Key == Key) return &Control;
        }
    }
    return nullptr;
}
#endif

} // namespace

void SceneInspectorPanel::Synchronize(const SceneDirectory& Directory, const SceneDirectoryPanel& Outliner)
{
    Cards.clear();
    Description.clear();
    TypeName.clear();
    GroupName.clear();
    const SceneInspectorSummary& Summary = Outliner.QueryInspector();
    HasSelection  = Summary.HasSelection;
    Selected      = Summary.Token;
    Name          = Summary.Name;
    Classification = Summary.Classification;
    Enabled       = HasSelection && Directory.QueryVisible(Selected);
    if (!HasSelection) return;

#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    const std::shared_ptr<const RecordEditorSpecification> Specification = SpecificationFor(Selected);
    if (!Specification) return;
    Description = Specification->Description;
    TypeName    = Specification->TypeName;
    GroupName   = Specification->GroupName;
    for (const RecordEditorCard& SourceCard : Specification->Cards)
    {
        SceneInspectorCard Card;
        Card.Title    = SourceCard.Title;
        Card.Subtitle = SourceCard.Subtitle;
        Card.Glyph    = SourceCard.Glyph;
        Card.Index    = SourceCard.Index;
        for (const RecordEditorControl& Source : SourceCard.Controls)
        {
            SceneInspectorControl Control;
            Control.Key              = Source.Key;
            Control.Label            = Source.Label;
            Control.Note             = Source.Note;
            Control.Unit             = Source.Unit;
            Control.Category         = Source.Category;
            Control.TilePresentation = Source.TilePresentation;
            Control.Minimum          = Source.Minimum;
            Control.Maximum          = Source.Maximum;
            Control.Step             = Source.Step;
            Control.Digits           = Source.Digits;
            Control.Text             = Source.Text;
            Control.Glyph            = Source.Glyph;
            Control.RangeLabelMinimum = Source.RangeLabelMinimum;
            Control.RangeLabelMaximum = Source.RangeLabelMaximum;
            Control.Options          = Source.Options;
            for (const RecordEditorStatItem& Item : Source.StatItems)
            {
                Control.StatCells.push_back(SceneInspectorStatCell{
                    Item.Label, Item.Query ? Item.Query() : std::string{}, Item.Unit});
            }
            if (Source.QueryToggle)    Control.ToggleValue    = Source.QueryToggle();
            if (Source.QuerySlider)    Control.SliderValue    = Source.QuerySlider();
            if (Source.QuerySegmented) Control.SegmentedIndex = Source.QuerySegmented();
            if (Source.QueryReadout)   Control.ReadoutText    = Source.QueryReadout();
            if (Source.QueryPill)
            {
                const std::pair<std::string, std::string> Pill = Source.QueryPill();
                Control.PillText  = Pill.first;
                Control.PillState = Pill.second;
            }
            Card.Controls.push_back(std::move(Control));
        }
        Cards.push_back(std::move(Card));
    }
#endif
}

bool SceneInspectorPanel::ApplyToggle(const std::string& Key, bool Value)
{
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    if (!HasSelection) return false;
    const std::shared_ptr<const RecordEditorSpecification> Specification = SpecificationFor(Selected);
    const RecordEditorControl* Control = Specification ? FindControl(*Specification, Key) : nullptr;
    if (Control == nullptr || !Control->ConfigureToggle) return false;
    Control->ConfigureToggle(Value);
    return true;
#else
    (void)Key; (void)Value;
    return false;
#endif
}

bool SceneInspectorPanel::ApplySlider(const std::string& Key, double Value)
{
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    if (!HasSelection) return false;
    const std::shared_ptr<const RecordEditorSpecification> Specification = SpecificationFor(Selected);
    const RecordEditorControl* Control = Specification ? FindControl(*Specification, Key) : nullptr;
    if (Control == nullptr || !Control->ConfigureSlider) return false;
    double Clamped = std::clamp(Value, Control->Minimum, Control->Maximum);
    if (Control->Step > 0.0)
    {
        Clamped = Control->Minimum + Control->Step * static_cast<double>(
                      static_cast<long long>((Clamped - Control->Minimum) / Control->Step + 0.5));
        Clamped = std::clamp(Clamped, Control->Minimum, Control->Maximum);
    }
    Control->ConfigureSlider(Clamped);
    return true;
#else
    (void)Key; (void)Value;
    return false;
#endif
}

bool SceneInspectorPanel::ApplySegmented(const std::string& Key, size_t Index)
{
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    if (!HasSelection) return false;
    const std::shared_ptr<const RecordEditorSpecification> Specification = SpecificationFor(Selected);
    const RecordEditorControl* Control = Specification ? FindControl(*Specification, Key) : nullptr;
    if (Control == nullptr || !Control->ConfigureSegmented || Index >= Control->Options.size()) return false;
    Control->ConfigureSegmented(Index);
    return true;
#else
    (void)Key; (void)Index;
    return false;
#endif
}

bool SceneInspectorPanel::ApplyAction(const std::string& Key)
{
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    if (!HasSelection) return false;
    const std::shared_ptr<const RecordEditorSpecification> Specification = SpecificationFor(Selected);
    const RecordEditorControl* Control = Specification ? FindControl(*Specification, Key) : nullptr;
    if (Control == nullptr || !Control->TriggerAction) return false;
    Control->TriggerAction();
    return true;
#else
    (void)Key;
    return false;
#endif
}

} // namespace Frontier
