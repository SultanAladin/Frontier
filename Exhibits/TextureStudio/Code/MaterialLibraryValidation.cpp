/*==========================================================================================================================================
                                                      MATERIALLIBRARYVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Exercise real library IO, failed-apply retention, source conflicts and native ImGui edit actions without claiming a window launch.

#include "UI/TextureStudio/MaterialLibraryPanel.h"
#include "RasterizeLibraryEvidence.h"
#include <imgui_internal.h>
#include "Engine/Interface/ContentDirectory.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <imgui.h>

namespace
{
std::string ExtractText(const std::filesystem::path &Path)
{
    std::ifstream Input(Path, std::ios::binary);
    return {std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
}

void EncodeText(const std::filesystem::path &Path, const std::string &Text)
{
    std::ofstream Output(Path, std::ios::binary);
    Output << Text;
    if (!Output) throw std::runtime_error("Fixture could not be written.");
}
} // namespace

int main(int ArgumentCount, char **Arguments)
{
    using namespace Frontier;
    try
    {
        uint32_t Checks = 0;
        const auto Confirm = [&](bool Condition, const char *Description)
        {
            if (!Condition) throw std::runtime_error(Description);
            ++Checks;
        };
        Confirm(ArgumentCount == 3, "Expected material specimens and scratch output directory.");
        const auto Output = std::filesystem::path(Arguments[2]) / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto Content = Output/"Library";
        std::filesystem::create_directories(Content/"Nested");
        std::filesystem::copy(Arguments[1], Content, std::filesystem::copy_options::recursive);
        const std::string Minimal = "# Retain this authored comment\nFormat=1\nName='Nested reference'\n[[Strata]]\nCategory='Opaque'\n";
        EncodeText(Content/"Nested/Reference.material", Minimal);
        EncodeText(Content/"Broken.material", "Not valid material TOML!");
        EncodeText(Content/"Ignore.texture.json", "{}");
        EncodeText(Content/"Oversized.material", std::string(1048577, 'x'));
        MaterialLibrary Library;
        Confirm(!Library.Refresh(), "Unconfigured library refreshed.");
        Confirm(!Library.ConfigureDirectory(Output/"Missing"), "Missing library was silently created.");
        Confirm(Library.ConfigureDirectory(Content), "Library did not open.");
        Confirm(Library.QueryEntries().size() == 7, "Recursive catalogue missed material files or included unrelated formats.");
        size_t Invalid = 0;
        for (const auto &Entry : Library.QueryEntries())
        {
            Invalid += !Entry.Diagnostic.empty();
            Confirm(EncodeContentPath(Entry.RelativePath).find('\\') == std::string::npos,
                "Stored material selection uses platform-native separators.");
        }
        Confirm(Invalid == 2, "Invalid and oversized files did not remain discoverable with diagnostics.");
        const auto Locations = Library.QueryEntries();
        Confirm(Library.Refresh(), "Catalogue refresh failed.");
        for (size_t Index = 0; Index < Locations.size(); ++Index)
            Confirm(Locations[Index].RelativePath == Library.QueryEntries()[Index].RelativePath, "Catalogue ordering changed.");
        Library.ConfigureFilter("NESTED REFERENCE");
        Confirm(Library.QueryVisible().size() == 1, "ASCII case-insensitive name search failed.");
        Library.ConfigureFilter("Nested/");
        Confirm(Library.QueryVisible().size() == 1, "Relative path search failed.");
        Library.ConfigureFilter("Nested\\");
        Confirm(Library.QueryVisible().size() == 1, "Windows separator path search failed.");
        Library.ConfigureFilter("");
        Confirm(Library.OpenMaterial("Nested/Reference.material"), "Nested material did not open.");
        Confirm(Library.QuerySource() == Minimal && !Library.QueryModified() && !Library.QueryUnapplied(), "Opening changed authored source or dirty status.");
        const auto Revision = Library.QueryRevision();
        const auto *Applied = Library.QueryApplied();
        Library.ReplaceSource(Minimal+"Roughness=-1\n");
        Confirm(Library.QueryModified() && Library.QueryUnapplied(), "Draft state was not separated from applied/disk source.");
        Confirm(!Library.ApplySource() && Library.QueryApplied() == Applied && Library.QueryRevision() == Revision, "Failed apply replaced the last valid material.");
        Confirm(!Library.SaveMaterial() && ExtractText(Content/"Nested/Reference.material") == Minimal, "Invalid draft was saved.");
        Confirm(Library.RevertSource() && Library.QuerySource() == Minimal && !Library.QueryModified(), "Revert did not restore saved source.");
        Confirm(Library.ConfigureStratumCoefficient(0, "Roughness", .6f), "Missing structured coefficient could not be inserted.");
        Confirm(Library.QuerySource().find("# Retain this authored comment") != std::string::npos &&
                ExtractText(Content/"Nested/Reference.material") == Minimal, "Structured edit changed comments or wrote to disk.");
        Confirm(!Library.ConfigureStratumCoefficient(0, "Roughness", -1), "Invalid structured coefficient accepted.");
        Confirm(Library.ConfigureStratumCoefficient(0, "Anisotropy", .35f), "Missing anisotropy could not be inserted.");
        Confirm(Library.RevertSource(), "Structured draft did not revert.");
        const auto Edited = Minimal+"Roughness=0.75\n# User-authored footer\n";
        Library.ReplaceSource(Edited);
        Confirm(Library.ApplySource(), "Valid edited material failed to compile.");
        Confirm(Library.QueryModified() && !Library.QueryUnapplied(), "Apply incorrectly saved the file.");
        Confirm(ExtractText(Content/"Nested/Reference.material") == Minimal, "Apply wrote to disk.");
        const bool Saved = Library.SaveMaterial();
        Confirm(Saved, Library.QueryDiagnostic().c_str());
        Confirm(ExtractText(Content/"Nested/Reference.material") == Edited, "Save changed comments/source bytes.");
        Confirm(!Library.QueryModified(), "Save left material dirty.");
        EncodeText(Content/"Nested/Reference.material", Minimal+"# External edit\n");
        Library.ReplaceSource(Edited+"# Local edit\n");
        Confirm(!Library.SaveMaterial() && ExtractText(Content/"Nested/Reference.material") == Minimal+"# External edit\n", "Save overwrote external changes.");
        Confirm(Library.OpenMaterial("Nested/Reference.material"), "Explicit reopen after conflict failed.");
        const auto BeforeFailedOpen = Library.QuerySource();
        Confirm(!Library.OpenMaterial("Broken.material") && Library.QuerySource() == BeforeFailedOpen, "Invalid open discarded the current document.");
        Confirm(!Library.ConfigureDirectory(Output/"Missing") && Library.QuerySource() == BeforeFailedOpen, "Failed library switch discarded the current document.");
        for (const auto &Path : {"../Escape.material", "/Outside.material", "C:/Outside.material", "Bad\\Path.material", "Wrong.json"})
            Confirm(!Library.CreateMaterial(DecodeContentPath(Path), "Refused"), "Unsafe/wrong-extension destination accepted.");
        const auto Unicode = DecodeContentPath("Nested/Brass-\xc3\xa9.material");
        Confirm(Library.CreateMaterial(Unicode, "Brass \xc3\xa9"), "Unicode material creation failed.");
        const auto Created = ExtractText(Content/Unicode);
        Confirm(!Library.CreateMaterial(Unicode, "Overwrite") && ExtractText(Content/Unicode) == Created, "Create overwrote existing material.");
        Confirm(Library.DuplicateMaterial("Copy.material") && ExtractText(Content/"Copy.material") == Created, "Duplicate changed authored source.");
        Confirm(!Library.DuplicateMaterial("Copy.material") && ExtractText(Content/"Copy.material") == Created, "Duplicate overwrote destination.");
        Library.ReplaceSource("invalid draft");
        Confirm(!Library.DuplicateMaterial("InvalidCopy.material") && !std::filesystem::exists(Content/"InvalidCopy.material"), "Invalid draft duplicated.");
        Confirm(Library.OpenMaterial("CoatedCeramic.material") && Library.QueryApplied()->Textures[1].Layers == 2, "Independent layer textures did not compile through library.");
        auto Definition = DecodeSurfaceMaterial(Minimal);
        Definition.TextureSources[0] = "Textures/Albedo.png";
        EncodeText(Content/"Nested/RootTexture.material", EncodeSurfaceMaterial(Definition));
        Confirm(Library.OpenMaterial("Nested/RootTexture.material") && Library.QueryApplied()->Textures[0].Levels[0].Width == 128,
                "Nested document changed ContentDirectory-relative texture meaning.");
        auto InvalidTexture = DecodeSurfaceMaterial(Minimal);
        InvalidTexture.TextureSources[0] = "../Escape.png";
        Library.ReplaceSource(EncodeSurfaceMaterial(InvalidTexture));
        const auto *BeforeTextureFailure = Library.QueryApplied();
        Confirm(!Library.ApplySource() && Library.QueryApplied() == BeforeTextureFailure, "Failed texture compilation replaced applied material.");
#ifndef _WIN32
        std::filesystem::create_directories(Output/"Outside");
        EncodeText(Output/"Outside/Escape.material", Minimal);
        std::filesystem::create_directory_symlink(Output/"Outside", Content/"Linked");
        Confirm(!Library.OpenMaterial("Linked/Escape.material"), "Symlink escaped ContentDirectory.");
        Confirm(!Library.CreateMaterial("Linked/New.material", "Escape"), "Create escaped via symlink.");
        Confirm(!std::filesystem::exists(Output/"Outside/New.material"), "Rejected create wrote outside root.");
#endif
        MaterialLibrary ReferenceLibrary;
        Confirm(ReferenceLibrary.ConfigureDirectory("EngineContent/MaterialArchives/TextureStudio"), "HTML library did not open.");
        Confirm(ReferenceLibrary.QueryEntries().size() == 127, "HTML library count differs.");
        Confirm(ReferenceLibrary.QueryEntries().front().Name == "Racing Green" && ReferenceLibrary.QueryEntries()[1].Name == "Midnight Amethyst", "HTML catalogue order differs.");
        for (const auto &Entry : ReferenceLibrary.QueryEntries())
            Confirm(Entry.Diagnostic.empty() && !Entry.Category.empty() && !Entry.RecipeControls.empty(), "HTML preset metadata/physical definition is invalid.");
        Confirm(ReferenceLibrary.OpenMaterial("racing-green.material"), "Racing Green did not compile.");
        Confirm(ReferenceLibrary.ConfigureStratumColour(0, {.1f, .2f, .3f}), "Structured albedo failed.");
        Confirm(ReferenceLibrary.ConfigureStratumCoefficient(1, "Coverage", .8f), "Structured coat coverage failed.");
        Confirm(!ReferenceLibrary.ConfigureStratumColour(0, {-1, 0, 0}), "Invalid albedo accepted.");
        ImGui::CreateContext();
        auto &IO = ImGui::GetIO();
        IO.IniFilename = nullptr;
        if (std::filesystem::is_regular_file("EngineContent/FontArchives/Inter/Inter-Regular.otf"))
            IO.Fonts->AddFontFromFileTTF("EngineContent/FontArchives/Inter/Inter-Regular.otf", 14);
        else IO.Fonts->AddFontDefault();
        unsigned char *Pixels = nullptr;
        int AtlasWidth = 0, AtlasHeight = 0;
        IO.Fonts->GetTexDataAsRGBA32(&Pixels, &AtlasWidth, &AtlasHeight);
        ActivationOwnership PanelOwnership(new MaterialLibraryPanel());
        auto *Panel = static_cast<MaterialLibraryPanel *>(PanelOwnership.get());
        Confirm(Panel->Initialize() && Panel->Activate(), "Library panel lifecycle failed.");
        Confirm(Panel->ConfigureDirectory(Content) && Panel->SelectMaterial(Unicode), "Native panel did not open material.");
        const auto IntegrateInterface = [&](float Width, float Density)
        {
            IO.DisplaySize = ImVec2(Width, 900);
            IO.DeltaTime = 1.f/60;
            ImGui::GetStyle().FontScaleDpi = Density;
            ImGui::NewFrame();
            Panel->ConstructInterface();
            ImGui::Render();
            Confirm(ImGui::GetDrawData()->TotalVtxCount > 0, "Native library produced no ImGui draw commands.");
        };
        IntegrateInterface(1280, 1);
        IntegrateInterface(1280, 1);
        const auto Click = [&](PlaneExtent Extent)
        {
            Confirm(Extent.Width() > 0 && Extent.Height() > 0, "Material operation has no interactive extent.");
            IO.AddMousePosEvent((Extent.MinimumX+Extent.MaximumX)/2, (Extent.MinimumY+Extent.MaximumY)/2);
            IO.AddMouseButtonEvent(0, true);
            IntegrateInterface(1280, 1);
            IO.AddMouseButtonEvent(0, false);
            IntegrateInterface(1280, 1);
        };
        Panel->ReplaceSource(Minimal+"Roughness=0.65\n");
        IntegrateInterface(1280, 1);
        Confirm(!Panel->SelectMaterial("CoatedCeramic.material"), "Native selection discarded unsaved edits.");
        Click(Panel->QueryApplyExtent());
        Confirm(!Panel->QueryLibrary().QueryUnapplied() && Panel->QueryLibrary().QueryModified(), "Native Apply failed or silently saved.");
        Click(Panel->QuerySaveExtent());
        Confirm(!Panel->QueryLibrary().QueryModified(), "Native Save click failed.");
        Panel->ReplaceSource("Broken draft");
        IntegrateInterface(1280, 1);
        Click(Panel->QueryApplyExtent());
        Confirm(!Panel->QueryLibrary().QueryDiagnostic().empty(), "Native Apply error absent.");
        Click(Panel->QueryRevertExtent());
        Confirm(!Panel->QueryLibrary().QueryModified(), "Native Revert click failed.");
        Confirm(Panel->SelectMaterial("BrushedCopper.material"), "Native selection after save failed.");
        IntegrateInterface(1280, 1);
        IntegrateInterface(1280, 1);
        auto *LibraryWindow = ImGui::FindWindowByName("Material library###FrontierPanel-MaterialLibrary");
        auto *ViewportWindow = ImGui::FindWindowByName("Viewport###FrontierPanel-MaterialViewport");
        auto *InspectorWindow = ImGui::FindWindowByName("Inspector###FrontierPanel-MaterialInspector");
        Confirm(LibraryWindow && ViewportWindow && InspectorWindow, "Registered material dock panels are absent.");
        Confirm(LibraryWindow->DockId && ViewportWindow->DockId && InspectorWindow->DockId, "Material panels are not docked.");
        Confirm(LibraryWindow->DockId != ViewportWindow->DockId && ViewportWindow->DockId != InspectorWindow->DockId,
                "Material panels share one tab region instead of three columns.");
        Confirm(LibraryWindow->Pos.x < ViewportWindow->Pos.x && ViewportWindow->Pos.x < InspectorWindow->Pos.x,
                "Default material columns differ from HTML ordering.");
        Confirm(!Panel->QueryPanels().QueryDetachedWindows(), "Detached OS windows enabled by default.");
        const float OriginalRoughness = Panel->QueryLibrary().QueryApplied()->Scattering.Illumination.Strata[0].AlbedoRoughness[3];
        Click(Panel->QueryRoughnessExtent());
        Confirm(Panel->QueryLibrary().QueryModified() && !Panel->QueryLibrary().QueryUnapplied(), "Released structured slider did not live-apply its draft.");
        Confirm(Panel->QueryLibrary().QueryApplied()->Scattering.Illumination.Strata[0].AlbedoRoughness[3] != OriginalRoughness,
                "Released structured slider did not update the applied material used by the renderer.");
        Click(Panel->QueryRevertExtent());
        Confirm(!Panel->QueryLibrary().QueryModified(), "Structured property edit did not revert.");
        IntegrateInterface(1280, 1);
        IO.Fonts->GetTexDataAsRGBA32(&Pixels, &AtlasWidth, &AtlasHeight);
        EncodeLibraryDrawCapture(Output/"MaterialLibrary.ppm", *ImGui::GetDrawData(), Pixels, AtlasWidth, AtlasHeight);
        Confirm(Panel->ConfigureDirectory("EngineContent/MaterialArchives/TextureStudio") && Panel->SelectMaterial("racing-green.material"),
                "Native automotive inspector did not open the actual preset.");
        IntegrateInterface(1280, 1);
        IntegrateInterface(1280, 1);
        auto Gloss = Panel->QueryRoughnessExtent();
        Gloss.MinimumX = Gloss.MaximumX - 2;
        Gloss.MinimumY += 168;
        Gloss.MaximumY += 168;
        Click(Gloss);
        const auto GlossDefinition = DecodeSurfaceMaterial(Panel->QueryLibrary().QuerySource());
        Confirm(Panel->QueryLibrary().QueryDiagnostic().empty() && GlossDefinition.Scattering.Illumination.Strata[1].AlbedoRoughness[3] >= .045f,
                "Clearcoat gloss endpoint generated an invalid native roughness.");
        Confirm(Panel->QueryLibrary().QueryModified() && !Panel->QueryLibrary().QueryUnapplied() &&
                Panel->QueryLibrary().QueryApplied()->Scattering.Illumination.Strata[1].Optics[2] > .97f,
                "Clearcoat gloss endpoint did not live-update the applied coating.");
        Confirm(Panel->QueryLibrary().RevertSource(), "Automotive inspector edit did not revert.");
        Panel->ReplaceSource(Panel->QueryLibrary().QuerySource());
        Panel->ReplaceSource("Unsaved closing draft");
        Confirm(!Panel->RequestClose(), "Closing discarded unsaved material without confirmation.");
        IntegrateInterface(1280, 1);
        Confirm(!Panel->ConsumeClose(), "Unsaved close automatically authorized.");
        (void)Panel->QueryLibrary().RevertSource();
        Confirm(Panel->RequestClose(), "Clean library refused closing.");
        IntegrateInterface(760, 1);
        IntegrateInterface(1280, 1.5f);
        PanelOwnership.reset();
        ImGui::DestroyContext();
        std::ofstream Report(Output/"MaterialLibraryChecks.json");
        Report << "{\"result\":\"passed\",\"checks\":" << Checks << ",\"native_window_execution\":false,\"windows_execution\":false}\n";
        std::cout << "PASS: " << Checks << " material catalogue, source transaction and native ImGui checks\n";
        std::cout << "REPORT " << (Output/"MaterialLibraryChecks.json").string() << '\n';
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << '\n';
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        return 1;
    }
}
