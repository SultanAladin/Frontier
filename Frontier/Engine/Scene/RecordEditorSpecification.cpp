// 🧩 Editor-only per-record Inspector construction: cards and bound controls declared by each record itself.

#include "Engine/Scene/RecordEditorSpecification.h"

#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)

#include <memory>
#include <utility>

namespace Frontier
{

RecordEditorRegistry& RecordEditorRegistry::Instance()
{
    static RecordEditorRegistry Registry;
    return Registry;
}

void RecordEditorRegistry::DeclareDescription(RecordToken Token, std::string Description)
{
    if (!Token.IsValid()) return;
    std::lock_guard<std::mutex> Guard(Mutex);
    auto& Slot = Specifications[Token.Value];
    if (!Slot) Slot = std::make_shared<RecordEditorSpecification>();
    Slot->Description = std::move(Description);
}

void RecordEditorRegistry::DeclareIdentity(RecordToken Token, std::string TypeName, std::string GroupName)
{
    if (!Token.IsValid()) return;
    std::lock_guard<std::mutex> Guard(Mutex);
    auto& Slot = Specifications[Token.Value];
    if (!Slot) Slot = std::make_shared<RecordEditorSpecification>();
    Slot->TypeName  = std::move(TypeName);
    Slot->GroupName = std::move(GroupName);
}

void RecordEditorRegistry::DeclareCard(RecordToken Token, std::string Title, std::string Subtitle, int Index, std::string Glyph)
{
    if (!Token.IsValid()) return;
    std::lock_guard<std::mutex> Guard(Mutex);
    auto& Slot = Specifications[Token.Value];
    if (!Slot) Slot = std::make_shared<RecordEditorSpecification>();
    RecordEditorCard Card;
    Card.Title    = std::move(Title);
    Card.Subtitle = std::move(Subtitle);
    Card.Glyph    = std::move(Glyph);
    Card.Index    = Index;
    Slot->Cards.push_back(std::move(Card));
}

void RecordEditorRegistry::DeclareControl(RecordToken Token, RecordEditorControl Control)
{
    if (!Token.IsValid() || Control.Key.empty()) return;
    std::lock_guard<std::mutex> Guard(Mutex);
    auto& Slot = Specifications[Token.Value];
    if (!Slot) Slot = std::make_shared<RecordEditorSpecification>();
    if (Slot->Cards.empty()) Slot->Cards.push_back(RecordEditorCard{"Properties", "", {}, -1, {}});
    Slot->Cards.back().Controls.push_back(std::move(Control));
}

void RecordEditorRegistry::Unregister(RecordToken Token)
{
    std::lock_guard<std::mutex> Guard(Mutex);
    Specifications.erase(Token.Value);
}

bool RecordEditorRegistry::QueryRegistered(RecordToken Token) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    return Specifications.find(Token.Value) != Specifications.end();
}

size_t RecordEditorRegistry::QueryCount() const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    return Specifications.size();
}

std::shared_ptr<const RecordEditorSpecification> RecordEditorRegistry::QuerySpecification(RecordToken Token) const
{
    std::lock_guard<std::mutex> Guard(Mutex);
    const auto Found = Specifications.find(Token.Value);
    return Found == Specifications.end() ? nullptr : Found->second;
}

} // namespace Frontier

#endif // FRONTIER_EDITOR_PROFILE || FRONTIER_DEVELOPMENT_PROFILE
