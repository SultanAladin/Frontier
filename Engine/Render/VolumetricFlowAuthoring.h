#pragma once
/*==========================================================================================================================================
                                                    VOLUMETRICFLOWAUTHORING.H
==========================================================================================================================================*/
// 🧩 Editable flow-unit catalogue and evaluation, translated from VolumetricFlowSolver.html.
//    This is the authoring model only: it does not yet render the editor or execute the solver.

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{

using FlowParameters = std::map<std::string, double, std::less<>>;

enum class FlowSurface { Solver, View };

struct FlowPinSpecification
{
    std::string Name;
    std::string Category;
};

struct FlowDialSpecification
{
    std::string Field;
    std::string Caption;
    std::optional<double> Minimum;
    std::optional<double> Maximum;
    std::optional<double> Increment;
    std::optional<int> Precision;
    std::string Unit;
    std::vector<std::string> Choices;
};

struct FlowUnitSpecification
{
    std::string Category;
    std::string Label;
    std::string Band;
    std::string Tint;
    std::vector<FlowPinSpecification> Inbound;
    std::vector<FlowPinSpecification> Outbound;
    std::vector<FlowDialSpecification> Dials;
    FlowSurface Surface = FlowSurface::Solver;
    std::string Toggle;
    std::string Editor;
    bool Terminal = false;
};

struct FlowPreset
{
    std::string Caption;
    FlowParameters Solver;
    FlowParameters View;
};

struct FlowDefaultUnit
{
    std::string Category;
    double X;
    double Y;
};

struct FlowDefaultLink
{
    std::size_t Origin;
    std::string OriginPin;
    std::size_t Target;
    std::string TargetPin;
};

struct FlowUnitEntry
{
    std::string Token;
    std::string Category;
    double X = 0;
    double Y = 0;
    bool Bypassed = false;
    FlowParameters Readings;
};

struct FlowUnitLink
{
    std::string Origin;
    std::string OriginPin;
    std::string Target;
    std::string TargetPin;
};

struct FlowEvaluation
{
    std::size_t ContributingCount = 0;
    std::string Fault;
    std::set<std::string, std::less<>> ClaimedFields;
};

// Generated verbatim from the retained source by the exhibit's catalogue generator.
const FlowParameters& FlowSolverDefaults();
const FlowParameters& FlowViewDefaults();
const std::vector<FlowPreset>& FlowPresets();
const std::vector<FlowUnitSpecification>& FlowUnitCatalogue();
const std::vector<FlowDefaultUnit>& FlowDefaultUnits();
const std::vector<FlowDefaultLink>& FlowDefaultLinks();

class VolumetricFlowAuthoring
{
public:
    // WithDefault creates the reference's Fire preset and default 17-unit wiring.
    explicit VolumetricFlowAuthoring(bool WithDefault = true);

    // Structural edits mirror the reference's low-level operations; call Evaluate() after the edit.
    // WriteDial, BypassUnit and ApplyPreset perform the reference UI's immediate evaluation.
    [[nodiscard]] std::string IntegrateUnit(std::string_view Category, double X, double Y);
    void DetachUnit(std::string_view Token);
    [[nodiscard]] bool IntegrateLink(std::string_view Origin, std::string_view OriginPin,
                                     std::string_view Target, std::string_view TargetPin);
    void DetachPin(std::string_view Token, std::string_view Pin, bool Inbound);
    [[nodiscard]] bool WriteDial(std::string_view Token, std::string_view Field, double Reading);
    [[nodiscard]] bool BypassUnit(std::string_view Token, bool Bypassed);
    [[nodiscard]] bool ApplyPreset(std::size_t Index);
    void Evaluate();

    [[nodiscard]] const FlowParameters& Solver() const { return SolverParameters; }
    [[nodiscard]] const FlowParameters& View() const { return ViewParameters; }
    [[nodiscard]] const std::vector<FlowUnitEntry>& Units() const { return UnitEntries; }
    [[nodiscard]] const std::vector<FlowUnitLink>& Links() const { return UnitLinks; }
    [[nodiscard]] const FlowEvaluation& Evaluation() const { return CurrentEvaluation; }
    [[nodiscard]] std::size_t PresetIndex() const { return EngagedPreset; }

private:
    [[nodiscard]] bool PathExists(std::string_view From, std::string_view To) const;
    [[nodiscard]] FlowUnitEntry* FindUnit(std::string_view Token);
    [[nodiscard]] const FlowUnitEntry* FindUnit(std::string_view Token) const;

    FlowParameters SolverParameters;
    FlowParameters ViewParameters;
    std::vector<FlowUnitEntry> UnitEntries;
    std::vector<FlowUnitLink> UnitLinks;
    FlowEvaluation CurrentEvaluation;
    uint32_t TokenCounter = 0;
    std::size_t EngagedPreset = 1;
};

} // namespace Frontier
