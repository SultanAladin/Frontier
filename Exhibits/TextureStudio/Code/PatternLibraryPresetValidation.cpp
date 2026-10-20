/*============================================================================================================================================
                                                      PATTERNLIBRARYPRESETVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Prove every Pattern library preset builds bounded, GPU-valid vector artwork without authoring a pixel.

#include "Engine/ContentInterchange/PatternLibraryPresets.h"
#include "Engine/Render/VectorCoverage.h"
#include <cmath>
#include <iostream>

using namespace Frontier;

namespace
{
uint32_t Assertions = 0;

void Confirm(bool Condition, const char *Caption)
{
    if (!Condition) throw std::runtime_error(Caption);
    ++Assertions;
}
}

int main()
{
    try
    {
        Confirm(QueryPatternLibraryPresetCount() == 6, "Pattern library preset count changed.");
        for (size_t Index = 0; Index < QueryPatternLibraryPresetCount(); ++Index)
        {
            const auto *Name = QueryPatternLibraryPresetName(Index);
            Confirm(Name && Name[0], "Pattern preset is unnamed.");
            const auto Document = ConstructPatternLibraryPreset(Index);
            Confirm(!Document.Layers.empty(), "Pattern preset has no motifs.");
            Confirm(Document.Name == Name, "Pattern preset document lost its gallery name.");
            Confirm(Document.Layers.size() == Document.Instructions.size(), "Preset layers and sources diverged.");
            for (const auto &Source : Document.Instructions)
            {
                for (float Coordinate : Source.Bounds)
                    Confirm(std::isfinite(Coordinate), "Preset motif is non-finite.");
                Confirm(Source.Bounds[0] >= 0 && Source.Bounds[0] <= 512 && Source.Bounds[1] >= 0 && Source.Bounds[1] <= 512,
                    "Preset motif centre escaped the 512 tile.");
                Confirm(Source.Bounds[2] >= 1 && Source.Bounds[3] >= 1 && Source.Bounds[2] <= 512 && Source.Bounds[3] <= 512,
                    "Preset motif extent is degenerate or larger than the tile.");
            }
            const auto Program = ConstructVectorAuthoringCoverage(Document);
            ValidateVectorCoverage(Program);
        }
        bool Refused = false;
        try { (void)ConstructPatternLibraryPreset(QueryPatternLibraryPresetCount()); }
        catch (const std::exception &) { Refused = true; }
        Confirm(Refused, "Out-of-range preset index accepted.");
        std::cout << "{\"result\":\"passed\",\"checks\":" << Assertions << "}\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cout << "{\"result\":\"failed\",\"error\":\"" << Failure.what() << "\"}\n";
        return 1;
    }
}
