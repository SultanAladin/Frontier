/*==========================================================================================================================================
                                                           MATERIALLIBRARY.CPP
==========================================================================================================================================*/
// 🧩 Discover authored TOML materials and retain last-valid compilation across edits, failures and explicit disk saves.

#include "MaterialLibrary.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include <charconv>
#include <toml++/toml.hpp>
#include "Engine/Interface/ContentDirectory.h"
#include "Engine/Interface/TextFileReplacement.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace Frontier
{
namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     MATERIAL FILE OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

std::string FoldMaterialText(std::string Text)
{
    for (auto &Character : Text)
        if (Character >= 'A' && Character <= 'Z') Character += 'a'-'A';
    return Text;
}

std::filesystem::path NormalizeMaterialPath(const std::filesystem::path &Path)
{
    // 📝 lexical normalization uses native separators on Windows; retain portable UTF-8 separators in library selections.
    return std::filesystem::path(Path.lexically_normal().generic_u8string());
}

std::string DecodeMaterialSource(const std::filesystem::path &Path)
{
    std::ifstream Input(Path, std::ios::binary | std::ios::ate);
    const auto Bytes = Input.tellg();
    if (!Input || Bytes < 0 || Bytes > 1048576) throw std::runtime_error("Material source is missing or exceeds 1 MiB.");
    std::string Source(static_cast<size_t>(Bytes), '\0');
    Input.seekg(0);
    Input.read(Source.data(), Bytes);
    if (!Input) throw std::runtime_error("Material source could not be read completely.");
    return Source;
}

void CreateMaterialSource(const std::filesystem::path &Path, const std::string &Source)
{
    // 📝 Exclusive creation refuses an existing file even if another writer creates it after path validation.
#ifdef _WIN32
    HANDLE File = CreateFileW(Path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (File == INVALID_HANDLE_VALUE) throw std::runtime_error("Material destination exists or cannot be created.");
    DWORD Written = 0;
    const bool Complete = WriteFile(File, Source.data(), static_cast<DWORD>(Source.size()), &Written, nullptr) &&
                          Written == Source.size() && FlushFileBuffers(File);
    const bool Closed = CloseHandle(File) != FALSE;
#else
    const int File = open(Path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (File < 0) throw std::runtime_error("Material destination exists or cannot be created.");
    size_t Written = 0;
    while (Written < Source.size())
    {
        const auto Count = write(File, Source.data()+Written, Source.size()-Written);
        if (Count < 0 && errno == EINTR) continue;
        if (Count <= 0) break;
        Written += static_cast<size_t>(Count);
    }
    const bool Complete = Written == Source.size() && fsync(File) == 0;
    const bool Closed = close(File) == 0;
#endif
    if (!Complete || !Closed)
    {
        std::error_code Ignored;
        std::filesystem::remove(Path, Ignored);
        throw std::runtime_error("New material could not be written completely.");
    }
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     CATALOGUE ENUMERATION
//------------------------------------------------------------------------------------------------------------------------

std::filesystem::path MaterialLibrary::ResolveMaterial(const std::filesystem::path &RelativePath) const
{
    if (Directory.empty() || RelativePath.empty() || RelativePath.is_absolute() || RelativePath.has_root_name() ||
        FoldMaterialText(EncodeContentPath(RelativePath.extension())) != ".material")
        throw std::runtime_error("Select a ContentDirectory-relative .material path.");
    const auto Text = EncodeContentPath(RelativePath);
    if (Text.find(':') != std::string::npos || Text.find('\\') != std::string::npos)
        throw std::runtime_error("Material paths must be portable relative paths.");
    const auto Resolved = std::filesystem::weakly_canonical(Directory/RelativePath);
    const auto Within = Resolved.lexically_relative(Directory);
    if (Within.empty() || *Within.begin() == "..") throw std::runtime_error("Material path escapes ContentDirectory.");
    return Resolved;
}

bool MaterialLibrary::ConfigureDirectory(const std::filesystem::path &Requested)
{
    DiagnosticJournal::Append("MaterialDirectory", DiagnosticJournal::EncodePath(Requested));
    try
    {
        if (!std::filesystem::is_directory(Requested)) throw std::runtime_error("Library directory does not exist.");
        MaterialLibrary Candidate;
        Candidate.Directory = std::filesystem::canonical(Requested);
        Candidate.Search = Search;
        if (!Candidate.Refresh()) throw std::runtime_error(Candidate.Diagnostic);
        Directory = std::move(Candidate.Directory);
        Entries = std::move(Candidate.Entries);
        Visible = std::move(Candidate.Visible);
        Truncated = Candidate.Truncated;
        Selection.clear();
        ParkedSources.clear();
        Source.clear();
        SavedSource.clear();
        AppliedSource.clear();
        Applied.reset();
        ++Revision;
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

bool MaterialLibrary::Refresh()
{
    try
    {
        if (Directory.empty()) throw std::runtime_error("Open a material library directory first.");
        std::vector<MaterialCatalogueEntry> Replacement;
        size_t Visited = 0;
        bool Limited = false;
        for (std::filesystem::recursive_directory_iterator Position(Directory), End; Position != End; ++Position)
        {
            if (++Visited > 8192 || Replacement.size() >= 4096) { Limited = true; break; }
            if (Position.depth() >= 16) { Position.disable_recursion_pending(); Limited = true; }
            if (Position->is_symlink()) { Position.disable_recursion_pending(); continue; }
            if (!Position->is_regular_file() || FoldMaterialText(EncodeContentPath(Position->path().extension())) != ".material") continue;
            MaterialCatalogueEntry Entry;
            Entry.RelativePath = NormalizeMaterialPath(Position->path().lexically_relative(Directory));
            Entry.Name = EncodeContentPath(Entry.RelativePath.stem());
            try
            {
                const auto Material = DecodeSurfaceMaterial(DecodeMaterialSource(Position->path()));
                Entry.Name = Material.Name;
                Entry.StratumCount = Material.Scattering.Illumination.Configuration[0];
            }
            catch (const std::exception &Failure) { Entry.Diagnostic = Failure.what(); }
            Replacement.push_back(std::move(Entry));
        }
        // 📝 Optional catalogue metadata retains the HTML order without changing authored material syntax.
        const auto CataloguePath = Directory / "Library.toml";
        if (std::filesystem::is_regular_file(CataloguePath))
        {
            const auto Catalogue = toml::parse(DecodeMaterialSource(CataloguePath));
            if (Catalogue["Format"].value<int64_t>() != 1) throw std::runtime_error("Unsupported library catalogue format.");
            if (const auto *Materials = Catalogue["Materials"].as_array())
                for (const auto &MaterialTerm : *Materials)
                {
                    const toml::node_view<const toml::node> Material(&MaterialTerm);
                    const auto Path = Material["Path"].value<std::string>();
                    if (!Path) continue;
                    const auto Match = std::find_if(Replacement.begin(), Replacement.end(), [&](const auto &Entry)
                    { return EncodeContentPath(Entry.RelativePath) == *Path; });
                    if (Match == Replacement.end()) continue;
                    Match->CatalogueOrdinal = Material["Ordinal"].value_or(uint32_t(UINT32_MAX));
                    Match->Category = Material["Category"].value_or(std::string{});
                    Match->Label = Material["Label"].value_or(std::string{});
                    Match->Description = Material["Description"].value_or(std::string{});
                    Match->RecipeTitle = Material["RecipeTitle"].value_or(std::string{});
                    Match->RecipeCaption = Material["RecipeCaption"].value_or(std::string{});
                    Match->ColourHex = Material["Color"].value_or(std::string{});
                    if (const auto *Controls = Material["Controls"].as_array())
                        for (const auto &ControlTerm : *Controls)
                        {
                            const toml::node_view<const toml::node> Control(&ControlTerm);
                            if (Match->RecipeControls.size() >= 64) break;
                            Match->RecipeControls.push_back({Control["Id"].value_or(std::string{}),
                                Control["Caption"].value_or(std::string{}), Control["Group"].value_or(std::string{}),
                                float(Control["Position"].value_or(0.0)), float(Control["Minimum"].value_or(0.0)),
                                float(Control["Maximum"].value_or(1.0)), {}});
                            auto &RecipeControl = Match->RecipeControls.back();
                            RecipeControl.Direct = Control["Direct"].value_or(false);
                            RecipeControl.Logarithmic = Control["Logarithmic"].value_or(false);
                            RecipeControl.Step = float(Control["Step"].value_or(.01));
                            RecipeControl.Unit = Control["Unit"].value_or(std::string{});
                            if (const auto *Targets = Control["Targets"].as_array())
                                for (const auto &TargetTerm : *Targets)
                                {
                                    const toml::node_view<const toml::node> Target(&TargetTerm);
                                    if (Match->RecipeControls.back().Targets.size() >= 8) break;
                                    Match->RecipeControls.back().Targets.push_back({Target["Key"].value_or(std::string{}),
                                        float(Target["Minimum"].value_or(0.0)), float(Target["Maximum"].value_or(1.0))});
                                }
                        }
                }
        }
        std::sort(Replacement.begin(), Replacement.end(), [](const auto &Left, const auto &Right)
        {
            if (Left.CatalogueOrdinal != Right.CatalogueOrdinal) return Left.CatalogueOrdinal < Right.CatalogueOrdinal;
            return Left.RelativePath.generic_u8string() < Right.RelativePath.generic_u8string();
        });
        Entries = std::move(Replacement);
        for (auto Position = ParkedSources.begin(); Position != ParkedSources.end();)
        {
            const auto Present = std::find_if(Entries.begin(), Entries.end(),
                [&](const auto &Entry) { return Entry.RelativePath == Position->first; });
            if (Present == Entries.end()) Position = ParkedSources.erase(Position);
            else ++Position;
        }
        Truncated = Limited;
        FilterEntries();
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

void MaterialLibrary::ConfigureFilter(std::string Requested)
{
    std::replace(Requested.begin(), Requested.end(), '\\', '/');
    Search = FoldMaterialText(std::move(Requested));
    FilterEntries();
}

void MaterialLibrary::FilterEntries()
{
    Visible.clear();
    for (size_t Index = 0; Index < Entries.size(); ++Index)
    {
        // 📝 Search paths use portable separators and UTF-8, not the Windows-native backslash encoding.
        const auto RelativeText = Entries[Index].RelativePath.generic_u8string();
        const std::string RelativeName(RelativeText.begin(), RelativeText.end());
        if (Search.empty() || FoldMaterialText(Entries[Index].Name + " " + Entries[Index].Category + " " + RelativeName).find(Search) != std::string::npos)
            Visible.push_back(Index);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SOURCE TRANSACTIONS
//------------------------------------------------------------------------------------------------------------------------

bool MaterialLibrary::OpenMaterial(const std::filesystem::path &RelativePath)
{
    DiagnosticJournal::Append("MaterialOpen", DiagnosticJournal::EncodePath(RelativePath));
    try
    {
        const auto Target = NormalizeMaterialPath(RelativePath);
        auto ReplacementSource = DecodeMaterialSource(ResolveMaterial(RelativePath));
        auto Replacement = std::make_unique<CompiledSurfaceMaterial>(CompileSurfaceMaterial(DecodeSurfaceMaterial(ReplacementSource), Directory));
        // 📝 Unsaved drafts ride along across selections; reopening the same material is an explicit refresh from disk.
        const auto Previous = Selection;
        if (Target != Previous)
        {
            if (!Previous.empty() && Source != SavedSource) ParkedSources[Previous] = Source;
        }
        else ParkedSources.erase(Target);
        Selection = Target;
        Source = std::move(ReplacementSource);
        SavedSource = Source;
        AppliedSource = Source;
        Applied = std::move(Replacement);
        if (Target != Previous)
        {
            const auto Parked = ParkedSources.find(Target);
            if (Parked != ParkedSources.end())
            {
                Source = Parked->second;
                ParkedSources.erase(Parked);
            }
        }
        ++Revision;
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

void MaterialLibrary::ReplaceSource(std::string Replacement)
{
    Source = std::move(Replacement);
    Diagnostic.clear();
}

bool MaterialLibrary::ConfigureStratumCoefficient(uint32_t Index, std::string_view Name, float Coefficient)
{
    if (Name != "Roughness" && Name != "Metallic" && Name != "Anisotropy" && Name != "Coverage" && Name != "Ior" && Name != "Specular" && Name != "HeightStrength" && Name != "NormalStrength")
    {
        Diagnostic = "Unsupported structured material coefficient.";
        return false;
    }
    char Number[64]{};
    const auto Encoded = std::to_chars(Number, Number + sizeof(Number), Coefficient);
    if (Encoded.ec != std::errc{}) { Diagnostic = "Material coefficient encoding failed."; return false; }
    std::string Numeric(Number, Encoded.ptr);
    if (Numeric.find_first_of(".eE") == std::string::npos) Numeric += ".0";
    return ConfigureStratumLiteral(Index, Name, Numeric);
}

bool MaterialLibrary::ConfigureStratumColour(uint32_t Index, const std::array<float, 3> &Spectrum)
{
    std::string Numeric = "[";
    for (size_t Channel = 0; Channel < Spectrum.size(); ++Channel)
    {
        char Number[64]{};
        const auto Encoded = std::to_chars(Number, Number + sizeof(Number), Spectrum[Channel]);
        if (Encoded.ec != std::errc{}) { Diagnostic = "Material colour encoding failed."; return false; }
        if (Channel) Numeric += ", ";
        Numeric.append(Number, Encoded.ptr);
    }
    return ConfigureStratumLiteral(Index, "Albedo", Numeric + "]");
}

bool MaterialLibrary::ConfigureStratumLiteral(uint32_t Index, std::string_view Name, const std::string &Numeric)
{
    try
    {
        auto Parsed = toml::parse(Source);
        auto *Strata = Parsed["Strata"].as_array();
        if (!Strata || Index >= Strata->size()) throw std::runtime_error("Material stratum does not exist.");
        auto *Stratum = Strata->get(Index)->as_table();
        if (!Stratum) throw std::runtime_error("Material stratum is not a table.");
        const auto Offset = [&](toml::source_position Position)
        {
            size_t Cursor = 0;
            for (uint32_t Line = 1; Line < Position.line; ++Line)
            {
                Cursor = Source.find('\n', Cursor);
                if (Cursor == std::string::npos) throw std::runtime_error("Invalid material source coordinates.");
                ++Cursor;
            }
            for (uint32_t Column = 1; Column < Position.column; ++Column)
            {
                if (Cursor >= Source.size()) throw std::runtime_error("Invalid material source column.");
                ++Cursor;
                while (Cursor < Source.size() && (static_cast<unsigned char>(Source[Cursor]) & 0xc0) == 0x80) ++Cursor;
            }
            return Cursor;
        };
        auto Replacement = Source;
        if (const auto *Existing = Stratum->get(Name))
        {
            const auto Region = Existing->source();
            const auto Begin = Offset(Region.begin), End = Offset(Region.end);
            Replacement.replace(Begin, End - Begin, Numeric);
        }
        else
        {
            const auto Begin = Offset(Stratum->source().begin);
            if (Begin < Source.size() && Source[Begin] == '{')
                Replacement.insert(Begin + 1, std::string(Name) + " = " + Numeric + (Stratum->empty() ? "" : ", "));
            else
            {
                const auto Header = Source.rfind("[[Strata]]", Begin);
                if (Header == std::string::npos) throw std::runtime_error("Add this coefficient in Source view first.");
                const auto End = Source.find('\n', Header);
                if (End == std::string::npos) Replacement += "\n" + std::string(Name) + " = " + Numeric + "\n";
                else Replacement.insert(End + 1, std::string(Name) + " = " + Numeric + (End && Source[End - 1] == '\r' ? "\r\n" : "\n"));
            }
        }
        (void)DecodeSurfaceMaterial(Replacement);
        Source = std::move(Replacement);
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

bool MaterialLibrary::ConfigurePaint(const AutomotivePaint &Paint)
{
    try
    {
        auto Definition = DecodeSurfaceMaterial(Source);
        Definition.Scattering.Paint = Paint;
        const auto Encoded = toml::parse(EncodeSurfaceMaterial(Definition));
        const auto Original = toml::parse(Source);
        const auto *PaintTable = Original["AutomotivePaint"].as_table();
        const auto *ReplacementTable = Encoded["AutomotivePaint"].as_table();
        if (!PaintTable || !ReplacementTable) throw std::runtime_error("Add an AutomotivePaint table in Source first.");
        const auto Offset = [&](toml::source_position Position)
        {
            size_t Cursor = 0;
            for (uint32_t Line = 1; Line < Position.line; ++Line)
            {
                Cursor = Source.find('\n', Cursor);
                if (Cursor == std::string::npos) throw std::runtime_error("Invalid paint source line.");
                ++Cursor;
            }
            for (uint32_t Column = 1; Column < Position.column; ++Column)
            {
                if (Cursor >= Source.size()) throw std::runtime_error("Invalid paint source column.");
                ++Cursor;
                while (Cursor < Source.size() && (static_cast<unsigned char>(Source[Cursor]) & 0xc0) == 0x80) ++Cursor;
            }
            return Cursor;
        };
        struct PaintReplacement { size_t Begin, End; std::string Literal; };
        std::vector<PaintReplacement> Replacements;
        for (const auto &[Key, Coefficient] : *ReplacementTable)
        {
            const auto *Existing = PaintTable->get(Key.str());
            if (!Existing) throw std::runtime_error("Add the missing paint coefficient in Source first.");
            std::ostringstream Literal;
            Literal << toml::toml_formatter(Coefficient);
            Replacements.push_back({Offset(Existing->source().begin), Offset(Existing->source().end), Literal.str()});
        }
        std::sort(Replacements.begin(), Replacements.end(), [](const auto &First, const auto &Second) { return First.Begin > Second.Begin; });
        auto Replacement = Source;
        for (const auto &Term : Replacements) Replacement.replace(Term.Begin, Term.End - Term.Begin, Term.Literal);
        (void)DecodeSurfaceMaterial(Replacement);
        Source = std::move(Replacement);
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

bool MaterialLibrary::ApplySource()
{
    DiagnosticJournal::Append("MaterialOperation", "ApplySource " + DiagnosticJournal::EncodePath(Selection) + " revision=" + std::to_string(Revision));
    try
    {
        if (Selection.empty()) throw std::runtime_error("Select a material first.");
        auto Replacement = std::make_unique<CompiledSurfaceMaterial>(CompileSurfaceMaterial(DecodeSurfaceMaterial(Source), Directory));
        Applied = std::move(Replacement);
        AppliedSource = Source;
        ++Revision;
        Diagnostic.clear();
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

bool MaterialLibrary::RevertSource()
{
    DiagnosticJournal::Append("MaterialOperation", "RevertSource " + DiagnosticJournal::EncodePath(Selection) + " revision=" + std::to_string(Revision));
    const auto Previous = Source;
    Source = SavedSource;
    if (ApplySource())
    {
        ParkedSources.erase(Selection);
        return true;
    }
    Source = Previous;
    return false;
}

bool MaterialLibrary::QueryParked(const std::filesystem::path &RelativePath) const
{
    return ParkedSources.find(NormalizeMaterialPath(RelativePath)) != ParkedSources.end();
}

bool MaterialLibrary::SaveMaterial()
{
    DiagnosticJournal::Append("MaterialOperation", "SaveMaterial " + DiagnosticJournal::EncodePath(Selection) + " revision=" + std::to_string(Revision));
    try
    {
        if (Selection.empty()) throw std::runtime_error("Select a material first.");
        const auto Destination = ResolveMaterial(Selection);
        if (DecodeMaterialSource(Destination) != SavedSource)
            throw std::runtime_error("Material changed on disk; reopen it before saving to avoid overwriting external edits.");
        if (QueryUnapplied() && !ApplySource()) return false;
        if (!TextFileReplacement::Finalize(Destination, Source, Diagnostic)) return false;
        SavedSource = Source;
        ParkedSources.erase(Selection);
        const auto Saved = Source;
        if (!Refresh()) return false;
        Source = Saved;
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

bool MaterialLibrary::CreateMaterial(const std::filesystem::path &RelativePath, const std::string &Name)
{
    try
    {
        MaterialDefinition Definition;
        Definition.Name = Name;
        const auto Replacement = EncodeSurfaceMaterial(Definition);
        const auto Destination = ResolveMaterial(RelativePath);
        // 📝 Folder creation is explicit in the directory picker; creating a material never creates arbitrary folder trees.
        if (!std::filesystem::is_directory(Destination.parent_path())) throw std::runtime_error("Destination folder does not exist.");
        CreateMaterialSource(Destination, Replacement);
        return Refresh() && OpenMaterial(RelativePath);
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}

bool MaterialLibrary::DuplicateMaterial(const std::filesystem::path &RelativePath)
{
    try
    {
        if (Selection.empty()) throw std::runtime_error("Select a material first.");
        const auto Destination = ResolveMaterial(RelativePath);
        // 📝 Duplicate the currently authored source, but never write an invalid/uncompilable draft.
        (void)CompileSurfaceMaterial(DecodeSurfaceMaterial(Source), Directory);
        CreateMaterialSource(Destination, Source);
        return Refresh();
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); DiagnosticJournal::Append("MaterialError", Diagnostic); return false; }
}
} // namespace Frontier
