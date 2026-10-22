/*============================================================================================================================================
                                                      PATTERNLIBRARYPRESETVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Prove every Pattern library preset builds bounded, GPU-valid vector artwork without authoring a pixel.

#include "Engine/ContentInterchange/PatternLibraryPresets.h"
#include "Engine/Render/VectorCoverage.h"
#include <toml++/toml.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

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
            Confirm(DecodeVectorAuthoring(EncodeVectorAuthoring(Document)) == Document, "Preset TOML round-trip changed artwork.");
        }
        // Shipped Alloy starters: the Library.toml sidecar enumerates every family and
        // palette file (Automation/ExportPatternLibrary.mjs generates both). Each file
        // must decode and coverage-validate; the counts pin the catalog so families
        // can only change alongside this test.
        const std::filesystem::path LibraryRoot("EngineContent/PatternArchives/TextureStudio/starters");
        const auto Library = toml::parse_file((LibraryRoot / "Library.toml").string());
        Confirm(std::string(Library["format"].value_or("")) == "frontier.pattern_library", "Pattern library sidecar format changed.");
        Confirm(Library["version"].value_or(0) == 1, "Pattern library sidecar version changed.");
        Confirm(std::string(Library["default_palette"].value_or("")) == "Indigo", "Pattern library default palette changed.");
        const auto *Palettes = Library["palettes"].as_array();
        const auto *Groups = Library["groups"].as_array();
        const auto *Families = Library["families"].as_array();
        Confirm(Palettes && Palettes->size() == 4, "Pattern library palette list changed.");
        Confirm(Groups && Groups->size() == 8, "Pattern library collection list changed.");
        Confirm(Families && Families->size() == 103, "Pattern library family count changed.");
        std::set<std::string> ShippedNames;
        size_t ShippedFiles = 0;
        for (const auto &Item : *Families)
        {
            const auto *Family = Item.as_table();
            Confirm(Family != nullptr, "Pattern library family is malformed.");
            const std::string Name = (*Family)["name"].value_or("");
            const std::string Group = (*Family)["group"].value_or("");
            Confirm(!Name.empty() && !Group.empty(), "Pattern library family lost its name or group.");
            bool GroupListed = false;
            for (const auto &Listed : *Groups)
                if (Listed.value_or("") == Group) GroupListed = true;
            Confirm(GroupListed, "Pattern library family references an unlisted collection.");
            const auto *Files = (*Family)["files"].as_table();
            Confirm(Files && !Files->empty(), "Pattern library family holds no palette files.");
            for (const auto &[Palette, File] : *Files)
            {
                const std::string Filename = File.value_or("");
                Confirm(!Filename.empty(), "Pattern library palette file is unnamed.");
                std::ifstream Input(LibraryRoot / Filename, std::ios::binary | std::ios::ate);
                Confirm(bool(Input), "Shipped preset file unreadable.");
                Confirm(Input.tellg() > 0 && Input.tellg() <= 4000000, "Shipped preset file size out of bounds.");
                std::string Source(size_t(Input.tellg()), '\0');
                Input.seekg(0);
                Confirm(bool(Input.read(Source.data(), Source.size())), "Shipped preset file truncated.");
                const auto Document = DecodeVectorAuthoring(Source);
                Confirm(!Document.Layers.empty() && !Document.Name.empty(), "Shipped preset file holds no named artwork.");
                ValidateVectorCoverage(ConstructVectorAuthoringCoverage(Document));
                ShippedNames.insert(Document.Name);
                ++ShippedFiles;
            }
        }
        Confirm(ShippedFiles == 178, "Shipped preset palette file count changed.");
        Confirm(ShippedNames.size() == ShippedFiles, "Shipped preset artwork names collided.");
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
