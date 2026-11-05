/*==========================================================================================================================================
                                                  VOLUMETRICFLOWVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Exercises the production authoring model and emits snapshots for comparison with the original editor.

#include "Engine/Render/VolumetricFlowAuthoring.h"

#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace Frontier;

namespace
{
void Require(bool Condition, std::string_view Message)
{
    if (!Condition) throw std::runtime_error(std::string(Message));
}

void Quote(std::string_view Text)
{
    std::cout << '"';
    for (const char Character : Text)
    {
        if (Character == '"' || Character == '\\') std::cout << '\\';
        std::cout << Character;
    }
    std::cout << '"';
}

void Parameters(const FlowParameters& Entries)
{
    std::cout << '{';
    bool First = true;
    for (const auto& [Field, Reading] : Entries)
    {
        if (!First) std::cout << ',';
        First = false;
        Quote(Field);
        std::cout << ':' << std::setprecision(std::numeric_limits<double>::max_digits10) << Reading;
    }
    std::cout << '}';
}

void Snapshot(std::string_view Caption, const VolumetricFlowAuthoring& Authoring)
{
    std::cout << "{\"name\":"; Quote(Caption);
    std::cout << ",\"preset\":" << Authoring.PresetIndex() << ",\"solver\":"; Parameters(Authoring.Solver());
    std::cout << ",\"view\":"; Parameters(Authoring.View());
    std::cout << ",\"units\":[";
    bool First = true;
    for (const auto& Entry : Authoring.Units())
    {
        if (!First) std::cout << ',';
        First = false;
        std::cout << "{\"Token\":"; Quote(Entry.Token); std::cout << ",\"Category\":"; Quote(Entry.Category);
        std::cout << ",\"Origin\":{\"X\":" << Entry.X << ",\"Y\":" << Entry.Y << "},\"Bypassed\":"
                  << (Entry.Bypassed ? "true" : "false") << ",\"Values\":";
        Parameters(Entry.Readings); std::cout << '}';
    }
    std::cout << "],\"links\":[";
    First = true;
    for (const auto& Link : Authoring.Links())
    {
        if (!First) std::cout << ',';
        First = false;
        std::cout << "{\"Origin\":"; Quote(Link.Origin); std::cout << ",\"OriginPin\":"; Quote(Link.OriginPin);
        std::cout << ",\"Target\":"; Quote(Link.Target); std::cout << ",\"TargetPin\":"; Quote(Link.TargetPin);
        std::cout << '}';
    }
    const auto& Evaluation = Authoring.Evaluation();
    std::cout << "],\"count\":" << Evaluation.ContributingCount << ",\"fault\":";
    if (Evaluation.Fault.empty()) std::cout << "null"; else Quote(Evaluation.Fault);
    std::cout << ",\"claimed\":[";
    First = true;
    for (const auto& Field : Evaluation.ClaimedFields)
    {
        if (!First) std::cout << ',';
        First = false;
        Quote(Field);
    }
    std::cout << "]}\n";
}
}

int main()
{
    try
    {
        Require(FlowPresets().size() == 5 && FlowUnitCatalogue().size() == 18, "catalogue size");
        VolumetricFlowAuthoring Authoring;
        Require(Authoring.Units().size() == 17 && Authoring.Links().size() == 16, "default layout");
        Require(Authoring.Evaluation().ContributingCount == 16, "default contribution count");
        Snapshot("default", Authoring);
        for (std::size_t Index = 0; Index < FlowPresets().size(); ++Index)
        {
            Require(Authoring.ApplyPreset(Index), "valid preset");
            Snapshot(FlowPresets()[Index].Caption, Authoring);
        }
        Require(!Authoring.ApplyPreset(99), "unknown preset must be rejected");

        Authoring = VolumetricFlowAuthoring();
        Require(Authoring.WriteDial("Unit7", "WindGustStrength", 2.25), "write unreachable gust");
        Require(Authoring.Solver().at("WindGustStrength") == 0, "unreachable gust must be disabled");
        Snapshot("unreachable gust", Authoring);
        Require(!Authoring.IntegrateLink("Unit8", "Momentum", "Unit2", "Carrier"), "pin category gate");
        Require(!Authoring.IntegrateLink("Unit4", "Carrier", "Unit3", "Carrier"), "cycle gate");
        Require(!Authoring.IntegrateLink("Unit3", "Carrier", "Unit3", "Carrier"), "self-link gate");
        Require(!Authoring.IntegrateLink("Missing", "Carrier", "Unit3", "Carrier"), "unknown unit gate");
        Require(Authoring.IntegrateLink("Unit7", "Momentum", "Unit8", "Momentum"), "replace inbound wire");
        Authoring.Evaluate();
        Require(Authoring.Solver().at("WindGustStrength") == 2.25, "connected gust must contribute");
        Snapshot("gust connected", Authoring);

        // Callers may use string views from existing links, which are erased during replacement.
        const auto& Existing = Authoring.Links().front();
        Require(Authoring.IntegrateLink(Existing.Origin, Existing.OriginPin, Existing.Target, Existing.TargetPin),
                "reconnect using views into a link");
        Authoring.Evaluate();
        Snapshot("reconnected", Authoring);
        Require(Authoring.BypassUnit("Unit11", true), "bypass detail");
        Snapshot("detail bypassed", Authoring);
        Require(Authoring.BypassUnit("Unit11", false), "restore detail");
        Snapshot("detail restored", Authoring);
        Require(Authoring.BypassUnit("Unit16", true), "bypass scattering");
        Require(Authoring.View().at("ScatterBandCount") == 1 && Authoring.View().at("MultiScatterAmbient") == 0,
                "absent scattering must fall back to single scatter");
        Snapshot("scattering bypassed", Authoring);
        Require(Authoring.BypassUnit("Unit16", false), "restore scattering");
        Authoring.DetachPin("Unit15", "Radiance", true);
        Authoring.Evaluate();
        Snapshot("pin detached", Authoring);

        Require(!Authoring.WriteDial("Unit7", "NotAField", 1), "unknown dial must be rejected");
        Require(!Authoring.BypassUnit("Unit17", true), "terminal has no bypass control");
        // The token view aliases the unit being removed.
        Authoring.DetachUnit(Authoring.Units().back().Token);
        Authoring.Evaluate();
        Require(Authoring.Evaluation().ContributingCount == 0, "missing terminal");
        Snapshot("terminal removed", Authoring);
        const auto NewTerminal = Authoring.IntegrateUnit("Composite", 10, 20);
        Require(NewTerminal == "Unit18", "monotonic unit tokens");
        Require(Authoring.IntegrateLink("Unit16", "Radiance", NewTerminal, "Radiance"), "new terminal link");
        Authoring.Evaluate();
        Snapshot("terminal restored", Authoring);
        return 0;
    }
    catch (const std::exception& Failure)
    {
        std::cerr << "Volumetric flow validation: " << Failure.what() << '\n';
        return 1;
    }
}
