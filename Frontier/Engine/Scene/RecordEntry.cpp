// 🧩 Base of every scene object: the constructor registers it, the destructor unregisters it — nothing else can.

#include "Engine/Scene/RecordEntry.h"

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Scene/RecordStore.h"
#include "Engine/Scene/SceneDirectory.h"

#include <utility>

namespace Frontier
{

RecordEntry::RecordEntry(RecordClassification Classification)
    : IssuedToken(TokenIssuer::Issue())
    , EntryClassification(Classification)
{
    RecordStore::Instance().Insert(*this);
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    SceneDirectory::Instance().Register(*this);   // 📝 Outliner visibility only; Shipping contains no directory
#endif
}

RecordEntry::~RecordEntry()
{
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    RecordEditorRegistry::Instance().Unregister(IssuedToken);   // 📝 Editor specification dies with its record
    SceneDirectory::Instance().Unregister(IssuedToken);
#endif
    RecordStore::Instance().Remove(IssuedToken);
}

#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)

void RecordEntry::DeclareEditorDescription(std::string Description)
{
    RecordEditorRegistry::Instance().DeclareDescription(IssuedToken, std::move(Description));
}

void RecordEntry::DeclareEditorIdentity(std::string TypeName, std::string GroupName)
{
    RecordEditorRegistry::Instance().DeclareIdentity(IssuedToken, std::move(TypeName), std::move(GroupName));
}

void RecordEntry::DeclareEditorCard(std::string Title, std::string Subtitle, int Index, std::string Glyph)
{
    RecordEditorRegistry::Instance().DeclareCard(IssuedToken, std::move(Title), std::move(Subtitle), Index, std::move(Glyph));
}

void RecordEntry::DeclareEditorToggle(std::string Key, std::string Label, std::string Note, bool TilePresentation,
                                      std::function<bool()> Query, std::function<void(bool)> Configure,
                                      std::string Glyph)
{
    RecordEditorControl Control;
    Control.Key              = std::move(Key);
    Control.Label            = std::move(Label);
    Control.Note             = std::move(Note);
    Control.Glyph            = std::move(Glyph);
    Control.Category         = RecordEditorControlCategory::Toggle;
    Control.TilePresentation = TilePresentation;
    Control.QueryToggle      = std::move(Query);
    Control.ConfigureToggle  = std::move(Configure);
    RecordEditorRegistry::Instance().DeclareControl(IssuedToken, std::move(Control));
}

void RecordEntry::DeclareEditorSlider(std::string Key, std::string Label, std::string Unit, double Minimum,
                                      double Maximum, double Step, std::function<double()> Query,
                                      std::function<void(double)> Configure, int Digits,
                                      std::string RangeLabelMinimum, std::string RangeLabelMaximum)
{
    RecordEditorControl Control;
    Control.Key               = std::move(Key);
    Control.Label             = std::move(Label);
    Control.Unit              = std::move(Unit);
    Control.Category          = RecordEditorControlCategory::Slider;
    Control.Minimum           = Minimum;
    Control.Maximum           = Maximum;
    Control.Step              = Step;
    Control.Digits            = Digits;
    Control.RangeLabelMinimum = std::move(RangeLabelMinimum);
    Control.RangeLabelMaximum = std::move(RangeLabelMaximum);
    Control.QuerySlider       = std::move(Query);
    Control.ConfigureSlider   = std::move(Configure);
    RecordEditorRegistry::Instance().DeclareControl(IssuedToken, std::move(Control));
}

void RecordEntry::DeclareEditorSelect(std::string Key, std::string Label, std::vector<std::string> Options,
                                      std::function<size_t()> Query, std::function<void(size_t)> Configure)
{
    RecordEditorControl Control;
    Control.Key                = std::move(Key);
    Control.Label              = std::move(Label);
    Control.Category           = RecordEditorControlCategory::Select;
    Control.Options            = std::move(Options);
    Control.QuerySegmented     = std::move(Query);
    Control.ConfigureSegmented = std::move(Configure);
    RecordEditorRegistry::Instance().DeclareControl(IssuedToken, std::move(Control));
}

void RecordEntry::DeclareEditorMetric(std::string Key, std::function<std::string()> QueryValue, std::string Unit,
                                      std::function<std::pair<std::string, std::string>()> QueryPill)
{
    RecordEditorControl Control;
    Control.Key         = std::move(Key);
    Control.Unit        = std::move(Unit);
    Control.Category    = RecordEditorControlCategory::Metric;
    Control.QueryReadout = std::move(QueryValue);
    Control.QueryPill   = std::move(QueryPill);
    RecordEditorRegistry::Instance().DeclareControl(IssuedToken, std::move(Control));
}

void RecordEntry::DeclareEditorMuted(std::string Text)
{
    RecordEditorControl Control;
    Control.Key      = "muted";
    Control.Text     = std::move(Text);
    Control.Category = RecordEditorControlCategory::Muted;
    RecordEditorRegistry::Instance().DeclareControl(IssuedToken, std::move(Control));
}

void RecordEntry::DeclareEditorStats(std::string Key, std::vector<RecordEditorStatItem> Items)
{
    RecordEditorControl Control;
    Control.Key       = std::move(Key);
    Control.Category  = RecordEditorControlCategory::Stats;
    Control.StatItems = std::move(Items);
    RecordEditorRegistry::Instance().DeclareControl(IssuedToken, std::move(Control));
}

void RecordEntry::DeclareEditorSegmented(std::string Key, std::string Label, std::vector<std::string> Options,
                                         std::function<size_t()> Query, std::function<void(size_t)> Configure)
{
    RecordEditorControl Control;
    Control.Key                = std::move(Key);
    Control.Label              = std::move(Label);
    Control.Category           = RecordEditorControlCategory::Segmented;
    Control.Options            = std::move(Options);
    Control.QuerySegmented     = std::move(Query);
    Control.ConfigureSegmented = std::move(Configure);
    RecordEditorRegistry::Instance().DeclareControl(IssuedToken, std::move(Control));
}

void RecordEntry::DeclareEditorReadout(std::string Key, std::string Label, std::function<std::string()> Query)
{
    RecordEditorControl Control;
    Control.Key         = std::move(Key);
    Control.Label       = std::move(Label);
    Control.Category    = RecordEditorControlCategory::Readout;
    Control.QueryReadout = std::move(Query);
    RecordEditorRegistry::Instance().DeclareControl(IssuedToken, std::move(Control));
}

void RecordEntry::DeclareEditorTextBlock(std::string Key, std::function<std::string()> Query)
{
    RecordEditorControl Control;
    Control.Key          = std::move(Key);
    Control.Category     = RecordEditorControlCategory::TextBlock;
    Control.QueryReadout = std::move(Query);
    RecordEditorRegistry::Instance().DeclareControl(IssuedToken, std::move(Control));
}

void RecordEntry::DeclareEditorAction(std::string Key, std::string Label, std::function<void()> Trigger)
{
    RecordEditorControl Control;
    Control.Key           = std::move(Key);
    Control.Label         = std::move(Label);
    Control.Category      = RecordEditorControlCategory::Action;
    Control.TriggerAction = std::move(Trigger);
    RecordEditorRegistry::Instance().DeclareControl(IssuedToken, std::move(Control));
}

#endif // FRONTIER_EDITOR_PROFILE || FRONTIER_DEVELOPMENT_PROFILE

} // namespace Frontier
