/*==========================================================================================================================================
                                                            MATERIALLIBRARY.H
==========================================================================================================================================*/
// 🧩 Root-contained material catalogue and source/apply/save transactions, independent of any application or UI.

#pragma once
#include "Engine/Render/SurfaceMaterial.h"
#include <memory>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     MATERIAL CATALOGUE
//------------------------------------------------------------------------------------------------------------------------

struct MaterialRecipeTarget
{
    std::string Key;
    float Minimum = 0, Maximum = 1;
};

struct MaterialRecipeControl
{
    std::string Id, Caption, Group;
    float Position = 0, Minimum = 0, Maximum = 1;
    std::vector<MaterialRecipeTarget> Targets;
    bool Direct = false, Logarithmic = false;
    float Step = .01f;
    std::string Unit;
};

struct MaterialCatalogueEntry
{
    std::filesystem::path RelativePath;
    std::string           Name;
    std::string           Diagnostic;
    uint32_t              StratumCount = 0;
    uint32_t              CatalogueOrdinal = UINT32_MAX;
    std::string           Category, Label, Description, RecipeTitle, RecipeCaption, ColourHex;
    std::vector<MaterialRecipeControl> RecipeControls;
};

// ⚠️ Single-thread ownership; containment/conflict checks do not lock a concurrently hostile filesystem.
class MaterialLibrary
{
  public:
    bool ConfigureDirectory(const std::filesystem::path &Directory);
    bool Refresh();
    void ConfigureFilter(std::string Search);
    bool OpenMaterial(const std::filesystem::path &RelativePath);
    void ReplaceSource(std::string Source);
    bool ApplySource();
    bool ApplyCoefficients();
    bool ConfigureStratumCoefficient(uint32_t Index, std::string_view Name, float Coefficient);
    bool ConfigurePaint(const AutomotivePaint &Paint);
    bool ConfigureStratumColour(uint32_t Index, const std::array<float, 3> &Spectrum);
    bool RevertSource();
    bool SaveMaterial();
    bool CreateMaterial(const std::filesystem::path &RelativePath, const std::string &Name);
    bool DuplicateMaterial(const std::filesystem::path &RelativePath);

    [[nodiscard]] const std::filesystem::path &QueryDirectory() const noexcept { return Directory; }
    [[nodiscard]] const std::filesystem::path &QuerySelection() const noexcept { return Selection; }
    [[nodiscard]] const std::vector<MaterialCatalogueEntry> &QueryEntries() const noexcept { return Entries; }
    [[nodiscard]] const std::vector<size_t> &QueryVisible() const noexcept { return Visible; }
    [[nodiscard]] const std::string &QuerySource() const noexcept { return Source; }
    [[nodiscard]] const std::string &QueryDiagnostic() const noexcept { return Diagnostic; }
    [[nodiscard]] const CompiledSurfaceMaterial *QueryApplied() const noexcept { return Applied.get(); }
    [[nodiscard]] bool QueryModified() const noexcept { return Source != SavedSource; }
    [[nodiscard]] bool QueryUnapplied() const noexcept { return Source != AppliedSource; }
    [[nodiscard]] bool QueryTruncated() const noexcept { return Truncated; }
    [[nodiscard]] uint64_t QueryRevision() const noexcept { return Revision; }

  private:
    std::filesystem::path Directory, Selection;
    std::vector<MaterialCatalogueEntry> Entries;
    std::vector<size_t> Visible;
    std::string Source, SavedSource, AppliedSource, Search, Diagnostic;
    std::unique_ptr<CompiledSurfaceMaterial> Applied;
    uint64_t Revision = 0;
    bool Truncated = false;

    std::filesystem::path ResolveMaterial(const std::filesystem::path &RelativePath) const;
    void FilterEntries();
    bool ConfigureStratumLiteral(uint32_t Index, std::string_view Name, const std::string &Numeric);
};
} // namespace Frontier
