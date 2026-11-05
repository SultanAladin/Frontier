/*==========================================================================================================================================
                                                   VOLUMETRICFLOWAUTHORING.CPP
==========================================================================================================================================*/
// 🧩 The retained flow editor's typed links, preset write-through and upstream evaluation.

#include "VolumetricFlowAuthoring.h"

#include <algorithm>
#include <deque>
#include <stdexcept>
#include <utility>

namespace Frontier
{
namespace
{

const FlowUnitSpecification* FindSpecification(std::string_view Category)
{
    const auto& Catalogue = FlowUnitCatalogue();
    const auto Found = std::find_if(Catalogue.begin(), Catalogue.end(),
                                    [Category](const auto& Entry) { return Entry.Category == Category; });
    return Found == Catalogue.end() ? nullptr : &*Found;
}

const FlowPinSpecification* FindPin(const std::vector<FlowPinSpecification>& Pins, std::string_view Name)
{
    const auto Found = std::find_if(Pins.begin(), Pins.end(),
                                    [Name](const auto& Entry) { return Entry.Name == Name; });
    return Found == Pins.end() ? nullptr : &*Found;
}

void WriteParameters(FlowParameters& Destination, const FlowParameters& Source)
{
    for (const auto& [Field, Reading] : Source) Destination[Field] = Reading;
}

} // namespace

VolumetricFlowAuthoring::VolumetricFlowAuthoring(bool WithDefault)
    : SolverParameters(FlowSolverDefaults()), ViewParameters(FlowViewDefaults())
{
    if (!WithDefault) return;

    // 📝 The reference applies Fire before creating units; each unit seeds its dials from that preset.
    if (!ApplyPreset(1)) throw std::logic_error("Missing default Fire preset");
    std::vector<std::string> Tokens;
    for (const auto& Entry : FlowDefaultUnits())
    {
        Tokens.push_back(IntegrateUnit(Entry.Category, Entry.X, Entry.Y));
        if (Tokens.back().empty()) throw std::logic_error("Unknown default flow unit");
    }
    for (const auto& Link : FlowDefaultLinks())
    {
        if (!IntegrateLink(Tokens.at(Link.Origin), Link.OriginPin, Tokens.at(Link.Target), Link.TargetPin))
            throw std::logic_error("Invalid default flow link");
    }
    Evaluate();
}

FlowUnitEntry* VolumetricFlowAuthoring::FindUnit(std::string_view Token)
{
    const auto Found = std::find_if(UnitEntries.begin(), UnitEntries.end(),
                                    [Token](const auto& Entry) { return Entry.Token == Token; });
    return Found == UnitEntries.end() ? nullptr : &*Found;
}

const FlowUnitEntry* VolumetricFlowAuthoring::FindUnit(std::string_view Token) const
{
    const auto Found = std::find_if(UnitEntries.begin(), UnitEntries.end(),
                                    [Token](const auto& Entry) { return Entry.Token == Token; });
    return Found == UnitEntries.end() ? nullptr : &*Found;
}

std::string VolumetricFlowAuthoring::IntegrateUnit(std::string_view Category, double X, double Y)
{
    const auto* Specification = FindSpecification(Category);
    if (!Specification) return {};
    FlowUnitEntry Entry;
    Entry.Token = "Unit" + std::to_string(++TokenCounter);
    Entry.Category = Specification->Category;
    Entry.X = X;
    Entry.Y = Y;
    const auto& Surface = Specification->Surface == FlowSurface::View ? ViewParameters : SolverParameters;
    for (const auto& Dial : Specification->Dials) Entry.Readings[Dial.Field] = Surface.at(Dial.Field);
    UnitEntries.push_back(std::move(Entry));
    return UnitEntries.back().Token;
}

void VolumetricFlowAuthoring::DetachUnit(std::string_view Token)
{
    const std::string Detached(Token); // The caller may have supplied a view into UnitEntries.
    std::erase_if(UnitEntries, [&Detached](const auto& Entry) { return Entry.Token == Detached; });
    std::erase_if(UnitLinks, [&Detached](const auto& Link) { return Link.Origin == Detached || Link.Target == Detached; });
}

bool VolumetricFlowAuthoring::PathExists(std::string_view From, std::string_view To) const
{
    std::vector<std::string> Pending{std::string(From)};
    std::set<std::string, std::less<>> Visited;
    while (!Pending.empty())
    {
        std::string Current = std::move(Pending.back());
        Pending.pop_back();
        if (Current == To) return true;
        if (!Visited.insert(Current).second) continue;
        for (const auto& Link : UnitLinks)
            if (Link.Origin == Current) Pending.push_back(Link.Target);
    }
    return false;
}

bool VolumetricFlowAuthoring::IntegrateLink(std::string_view Origin, std::string_view OriginPin,
                                            std::string_view Target, std::string_view TargetPin)
{
    if (Origin == Target) return false;
    const auto* OriginEntry = FindUnit(Origin);
    const auto* TargetEntry = FindUnit(Target);
    if (!OriginEntry || !TargetEntry) return false;
    const auto* OriginSpecification = FindSpecification(OriginEntry->Category);
    const auto* TargetSpecification = FindSpecification(TargetEntry->Category);
    const auto* Outbound = FindPin(OriginSpecification->Outbound, OriginPin);
    const auto* Inbound = FindPin(TargetSpecification->Inbound, TargetPin);
    if (!Outbound || !Inbound || Outbound->Category != Inbound->Category || PathExists(Target, Origin)) return false;

    // 📝 Copy the four strings before erasing; a caller may pass views into an existing link.
    //    Only one wire may feed an inbound pin; the source tests for cycles before replacing the old wire.
    FlowUnitLink Replacement{std::string(Origin), std::string(OriginPin), std::string(Target), std::string(TargetPin)};
    std::erase_if(UnitLinks, [&Replacement](const auto& Link)
                  { return Link.Target == Replacement.Target && Link.TargetPin == Replacement.TargetPin; });
    UnitLinks.push_back(std::move(Replacement));
    return true;
}

void VolumetricFlowAuthoring::DetachPin(std::string_view Token, std::string_view Pin, bool Inbound)
{
    const std::string DetachedToken(Token), DetachedPin(Pin);
    std::erase_if(UnitLinks, [&DetachedToken, &DetachedPin, Inbound](const auto& Link)
    {
        return Inbound ? Link.Target == DetachedToken && Link.TargetPin == DetachedPin
                       : Link.Origin == DetachedToken && Link.OriginPin == DetachedPin;
    });
}

bool VolumetricFlowAuthoring::WriteDial(std::string_view Token, std::string_view Field, double Reading)
{
    auto* Entry = FindUnit(Token);
    if (!Entry) return false;
    const auto Found = Entry->Readings.find(Field);
    if (Found == Entry->Readings.end()) return false;
    Found->second = Reading;
    Evaluate();
    return true;
}

bool VolumetricFlowAuthoring::BypassUnit(std::string_view Token, bool Bypassed)
{
    auto* Entry = FindUnit(Token);
    if (!Entry || FindSpecification(Entry->Category)->Terminal) return false;
    Entry->Bypassed = Bypassed;
    Evaluate();
    return true;
}

bool VolumetricFlowAuthoring::ApplyPreset(std::size_t Index)
{
    if (Index >= FlowPresets().size()) return false;
    const auto& Preset = FlowPresets()[Index];
    EngagedPreset = Index;
    WriteParameters(SolverParameters, Preset.Solver);
    WriteParameters(ViewParameters, Preset.View);
    for (auto& Entry : UnitEntries)
    {
        const auto* Specification = FindSpecification(Entry.Category);
        const auto& Surface = Specification->Surface == FlowSurface::View ? Preset.View : Preset.Solver;
        for (const auto& Dial : Specification->Dials)
        {
            const auto Found = Surface.find(Dial.Field);
            if (Found != Surface.end()) Entry.Readings[Dial.Field] = Found->second;
        }
    }
    if (!UnitEntries.empty()) Evaluate();
    return true;
}

void VolumetricFlowAuthoring::Evaluate()
{
    const auto Terminal = std::find_if(UnitEntries.begin(), UnitEntries.end(), [](const auto& Entry)
                                       { return FindSpecification(Entry.Category)->Terminal; });
    if (Terminal == UnitEntries.end())
    {
        CurrentEvaluation.Fault = "no Composite unit — nothing is being rendered";
        CurrentEvaluation.ContributingCount = 0;
        return;
    }

    std::vector<const FlowUnitEntry*> Contributing;
    std::set<std::string, std::less<>> Visited;
    std::deque<std::string> Pending{Terminal->Token};
    while (!Pending.empty())
    {
        const std::string Current = std::move(Pending.front());
        Pending.pop_front();
        if (!Visited.insert(Current).second) continue;
        const auto* Entry = FindUnit(Current);
        if (!Entry || Entry->Bypassed) continue;
        Contributing.push_back(Entry);
        for (const auto& Link : UnitLinks)
            if (Link.Target == Current) Pending.push_back(Link.Origin);
    }
    std::reverse(Contributing.begin(), Contributing.end());

    std::set<std::string, std::less<>> Categories;
    CurrentEvaluation.ClaimedFields.clear();
    for (const auto* Entry : Contributing)
    {
        Categories.insert(Entry->Category);
        const auto* Specification = FindSpecification(Entry->Category);
        auto& Surface = Specification->Surface == FlowSurface::View ? ViewParameters : SolverParameters;
        for (const auto& Dial : Specification->Dials)
        {
            Surface[Dial.Field] = Entry->Readings.at(Dial.Field);
            CurrentEvaluation.ClaimedFields.insert(Dial.Field);
        }
        if (!Specification->Toggle.empty()) SolverParameters[Specification->Toggle] = 1;
    }
    for (const auto& Specification : FlowUnitCatalogue())
        if (!Specification.Toggle.empty() && !Categories.contains(Specification.Category))
            SolverParameters[Specification.Toggle] = 0;

    if (!Categories.contains("UniformWind"))
    {
        SolverParameters["WindLongitudinal"] = 0;
        SolverParameters["WindVertical"] = 0;
        SolverParameters["WindLateral"] = 0;
    }
    if (!Categories.contains("CurlGust")) SolverParameters["WindGustStrength"] = 0;
    if (!Categories.contains("ReactionCore")) ViewParameters["CoreLuminosity"] = 0;
    if (!Categories.contains("ColourStamp")) ViewParameters["PaletteBlend"] = 0;
    if (!Categories.contains("SourceInjection"))
    {
        SolverParameters["InjectionDensity"] = 0;
        SolverParameters["InjectionTemperature"] = 0;
    }
    if (!Categories.contains("SourceTurbulence"))
    {
        SolverParameters["NoiseGain"] = 0;
        SolverParameters["SwirlStrength"] = 0;
        SolverParameters["InjectionVelocity"] = 0;
    }
    if (!Categories.contains("DetailSynthesis")) ViewParameters["TurbulenceStrength"] = 0;
    if (!Categories.contains("MultipleScattering"))
    {
        ViewParameters["ScatterBandCount"] = 1;
        ViewParameters["MultiScatterAmbient"] = 0;
    }
    CurrentEvaluation.ContributingCount = Contributing.size();
    CurrentEvaluation.Fault = Contributing.size() < UnitEntries.size()
        ? std::to_string(UnitEntries.size() - Contributing.size()) + " unit(s) unreachable from Composite" : "";
}

} // namespace Frontier
