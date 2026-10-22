// 🧩 Portable Outliner view-model: flattened rows, real category tabs with counts, selection for the inspector.

#include "Engine/Interface/SceneDirectoryPanel.h"

#include "Engine/Scene/SceneDirectory.h"

#include <unordered_map>

namespace Frontier
{

namespace
{

// Directory parenting is cycle-free by construction (ConfigureParent refuses cycles); the cap only guards
// against corrupted state turning a projection into unbounded recursion.
constexpr uint32_t MaximumDepthTier = 128;   // [tiers]

void AppendSubtree(const SceneDirectory& Directory, RecordToken Token, uint32_t DepthTier,
                   const std::unordered_map<uint64_t, std::vector<RecordToken>>& Children,
                   std::vector<SceneDirectoryRow>& Out)
{
    SceneDirectoryRow Row;
    Row.Token          = Token;
    Row.Name           = Directory.QueryName(Token);
    Row.Classification = Directory.QueryClassification(Token);
    Row.DepthTier      = DepthTier;
    Row.ChildCount     = Directory.QueryChildCount(Token);
    Out.push_back(Row);

    if (DepthTier >= MaximumDepthTier)
    {
        return;
    }
    const auto It = Children.find(Token.Value);
    if (It != Children.end())
    {
        for (const RecordToken Child : It->second)
        {
            AppendSubtree(Directory, Child, DepthTier + 1, Children, Out);
        }
    }
}

} // namespace

void SceneDirectoryPanel::Synchronize(const SceneDirectory& Directory)
{
    std::vector<RecordToken> Order;
    Directory.CopyRegistrationOrder(Order);
    Total = static_cast<uint32_t>(Order.size());

    std::unordered_map<uint64_t, std::vector<RecordToken>> Children;
    std::vector<RecordToken>                               Roots;
    for (const RecordToken Token : Order)
    {
        const RecordToken Parent = Directory.QueryParent(Token);
        if (Parent.IsValid() && Directory.QueryRegistered(Parent))
        {
            Children[Parent.Value].push_back(Token);
        }
        else
        {
            Roots.push_back(Token);
        }
    }

    Rows.clear();
    for (const RecordToken Root : Roots)
    {
        AppendSubtree(Directory, Root, 0, Children, Rows);
    }

    Tabs.clear();
    for (uint32_t Index = 0; Index < static_cast<uint32_t>(RecordClassification::Count); ++Index)
    {
        Tabs.push_back(SceneClassificationTab{static_cast<RecordClassification>(Index), 0});
    }
    for (const SceneDirectoryRow& Row : Rows)
    {
        Tabs[static_cast<size_t>(Row.Classification)].Count += 1;
    }

    Inspector = SceneInspectorSummary{};
    if (Selected.IsValid() && Directory.QueryRegistered(Selected))
    {
        Inspector.HasSelection   = true;
        Inspector.Token          = Selected;
        Inspector.Name           = Directory.QueryName(Selected);
        Inspector.Classification = Directory.QueryClassification(Selected);
        Inspector.ChildCount     = Directory.QueryChildCount(Selected);
        const RecordToken Parent = Directory.QueryParent(Selected);
        Inspector.ParentName     = Parent.IsValid() ? Directory.QueryName(Parent) : std::string{};
        for (const SceneDirectoryRow& Row : Rows)
        {
            if (Row.Token == Selected)
            {
                Inspector.DepthTier = Row.DepthTier;
                break;
            }
        }
    }
    else
    {
        Selected = RecordToken{};
    }

    RebuildVisible();
}

void SceneDirectoryPanel::ConfigureClassificationFilter(std::optional<RecordClassification> Filter)
{
    ClassificationFilter = Filter;
    RebuildVisible();
}

void SceneDirectoryPanel::Select(RecordToken Token)
{
    Selected = Token;
}

void SceneDirectoryPanel::ClearSelection()
{
    Selected  = RecordToken{};
    Inspector = SceneInspectorSummary{};
}

void SceneDirectoryPanel::RebuildVisible()
{
    Visible.clear();
    for (const SceneDirectoryRow& Row : Rows)
    {
        if (!ClassificationFilter.has_value() || Row.Classification == *ClassificationFilter)
        {
            Visible.push_back(Row);
        }
    }
}

} // namespace Frontier
