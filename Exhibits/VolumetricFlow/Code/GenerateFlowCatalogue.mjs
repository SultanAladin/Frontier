// Extract the original editor's constants and default wiring into the C++ authoring catalogue.
import fs from "node:fs";
import vm from "node:vm";
import assert from "node:assert/strict";

const SourcePath = "Documentation/Plans/References/InheritedEngine/Prototypes/VolumetricFlowSolver.html";
const Destination = "Engine/Render/VolumetricFlowCatalogue.cpp";
const Source = fs.readFileSync(SourcePath, "utf8");
function Section(Start, End)
{
    const First = Source.indexOf(Start), Last = Source.indexOf(End, First);
    assert.ok(First >= 0 && Last > First, `Reference section missing: ${Start}`);
    return Source.slice(First, Last);
}
const Input = [Section("const SolverParameterization =", "const UniformFieldOrder ="),
               Section("const ViewParameterization =", "const ViewByteCount ="),
               Section("const AppearancePresets =", "let EngagedPresetIndex ="),
               Section("const PinCategoryTint =", "const GraphStore =")].join("\n");
const Reference = JSON.parse(vm.runInNewContext(Input +
    "\nJSON.stringify({ SolverParameterization, ViewParameterization, AppearancePresets, UnitCatalogue })"));
const Defaults = Section("function ConstructDefaultGraph()", "//                                                   SETTINGS SHEET");
const UnitMatches = [...Defaults.matchAll(/const\s+(\w+)\s*=\s*IntegrateUnit\("([^"]+)",\s*(\d+),\s*(\d+)\);/g)];
const Aliases = new Map(UnitMatches.map((Match, Index) => [Match[1], Index]));
const LinkMatches = [...Defaults.matchAll(/IntegrateLink\((\w+),\s*"([^"]+)",\s*(\w+),\s*"([^"]+)"\);/g)];
assert.equal(UnitMatches.length, 17); assert.equal(LinkMatches.length, 16);
assert.equal(Reference.AppearancePresets.length, 5); assert.equal(Object.keys(Reference.UnitCatalogue).length, 18);

const Quote = Text => JSON.stringify(Text);
const NumberText = NumberValue => {
    assert.equal(typeof NumberValue, "number"); assert.ok(Number.isFinite(NumberValue));
    return JSON.stringify(NumberValue);
};
const Maybe = (ObjectValue, Field) => Object.hasOwn(ObjectValue, Field) ? NumberText(ObjectValue[Field]) : "std::nullopt";
const Parameters = Parameterization => "{\n" + Object.entries(Parameterization)
    .map(([Field, Reading]) => `        { ${Quote(Field)}, ${NumberText(Reading)} },`).join("\n") + "\n    }";
const Pins = PinList => "{" + PinList.map(Pin => `{ ${Quote(Pin.Name)}, ${Quote(Pin.Category)} }`).join(", ") + "}";
const Dials = DialList => "{\n" + DialList.map(Dial =>
    `            { ${Quote(Dial.Field)}, ${Quote(Dial.Caption)}, ${Maybe(Dial, "Minimum")}, ${Maybe(Dial, "Maximum")}, ` +
    `${Maybe(Dial, "Increment")}, ${Maybe(Dial, "Precision")}, ${Quote(Dial.Unit || "")}, ` +
    `{ ${Array.from(Dial.Choices || [], Quote).join(", ")} } },`).join("\n") + "\n        }";
const Definitions = [];
Definitions.push(`const FlowParameters& FlowSolverDefaults()\n{\n    static const FlowParameters Entries = ${Parameters(Reference.SolverParameterization)};\n    return Entries;\n}`);
Definitions.push(`const FlowParameters& FlowViewDefaults()\n{\n    static const FlowParameters Entries = ${Parameters(Reference.ViewParameterization)};\n    return Entries;\n}`);
Definitions.push(`const std::vector<FlowPreset>& FlowPresets()\n{\n    static const std::vector<FlowPreset> Entries = {\n` +
    Reference.AppearancePresets.map(Preset => `        { ${Quote(Preset.Caption)}, ${Parameters(Preset.Flow)}, ${Parameters(Preset.View)} },`).join("\n") +
    `\n    };\n    return Entries;\n}`);
Definitions.push(`const std::vector<FlowUnitSpecification>& FlowUnitCatalogue()\n{\n    static const std::vector<FlowUnitSpecification> Entries = {\n` +
    Object.entries(Reference.UnitCatalogue).map(([Category, Specification]) =>
        `        { ${Quote(Category)}, ${Quote(Specification.Label)}, ${Quote(Specification.Band)}, ${Quote(Specification.Tint)},\n` +
        `          ${Pins(Specification.Inbound)}, ${Pins(Specification.Outbound)}, ${Dials(Specification.Owned)},\n` +
        `          FlowSurface::${Specification.Surface === "View" ? "View" : "Solver"}, ${Quote(Specification.Toggle || "")}, ` +
        `${Quote(Specification.Editor || "")}, ${Specification.Terminal ? "true" : "false"} },`).join("\n") +
    `\n    };\n    return Entries;\n}`);
Definitions.push(`const std::vector<FlowDefaultUnit>& FlowDefaultUnits()\n{\n    static const std::vector<FlowDefaultUnit> Entries = {\n` +
    UnitMatches.map(Match => `        { ${Quote(Match[2])}, ${Match[3]}, ${Match[4]} },`).join("\n") +
    `\n    };\n    return Entries;\n}`);
Definitions.push(`const std::vector<FlowDefaultLink>& FlowDefaultLinks()\n{\n    static const std::vector<FlowDefaultLink> Entries = {\n` +
    LinkMatches.map(Match => {
        assert.ok(Aliases.has(Match[1]) && Aliases.has(Match[3]));
        return `        { ${Aliases.get(Match[1])}, ${Quote(Match[2])}, ${Aliases.get(Match[3])}, ${Quote(Match[4])} },`;
    }).join("\n") + `\n    };\n    return Entries;\n}`);
const Result = `/*==========================================================================================================================================\n` +
    `                                                  VOLUMETRICFLOWCATALOGUE.CPP\n` +
    `==========================================================================================================================================*/\n` +
    `// Generated from VolumetricFlowSolver.html by Exhibits/VolumetricFlow/Code/GenerateFlowCatalogue.mjs.\n` +
    `// Do not alter the generated constants independently of their retained reference.\n\n` +
    `#include "VolumetricFlowAuthoring.h"\n\nnamespace Frontier\n{\n\n${Definitions.join("\n\n")}\n\n} // namespace Frontier\n`;
if (process.argv.includes("--check"))
{
    assert.equal(fs.readFileSync(Destination, "utf8"), Result, "Generated catalogue differs from the original reference");
    console.log("Flow catalogue matches the retained reference: 5 presets, 18 unit types, 17 default units and 16 links.");
}
else
{
    fs.writeFileSync(Destination, Result);
    console.log(`Wrote ${Destination}`);
}
