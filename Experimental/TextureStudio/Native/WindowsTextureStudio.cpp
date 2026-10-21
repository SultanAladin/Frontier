/*==========================================================================================================================================
                                                        WINDOWSTEXTURESTUDIO.CPP
==========================================================================================================================================*/
// 🧩 Integrate native Pattern authoring and the selected-material preview into the shared Vulkan workspace loop.

#define GLFW_INCLUDE_NONE
#ifndef FRONTIER_BUILD_COMMIT
#define FRONTIER_BUILD_COMMIT "local"
#endif
#include "Engine/Interface/WindowsApplication.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include "Engine/Interface/VulkanPresentation.h"
#include "Engine/ContentInterchange/ProjectSpaceCodec.h"
#include "Engine/Render/MaterialLibraryRendering.h"
#include "Engine/Render/MaterialThumbnailAtlas.h"
#include "Engine/Render/VectorSurfaceMaterialBridge.h"
#include "Engine/Render/VectorAuthoringCoverageRendering.h"
#include "Engine/Render/VectorSelectionEvaluation.h"
#include "Engine/ContentInterchange/VectorAuthoringSelectionEditing.h"
#include "UI/TextureStudio/PatternWorkspacePanel.h"
#include <imgui_impl_vulkan.h>
#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/ContentDirectory.h"
#include "Engine/Interface/TextFileReplacement.h"
#include "Engine/Interface/TypefaceRegistry.h"
#include "UI/Editor/ControlCentrePanel.h"
#include "UI/TextureStudio/MaterialLibraryPanel.h"
#include <array>
#include <map>
#include <windows.h>
#include <shellapi.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <fstream>
#include <iostream>
#include <optional>
#include <string_view>

//------------------------------------------------------------------------------------------------------------------------
//                                                     STANDALONE APPLICATION
//------------------------------------------------------------------------------------------------------------------------

namespace
{
#ifdef _DEBUG
void AppendPatternPreviewDebug(std::string_view Description)
{
    Frontier::DiagnosticJournal::Append("PatternPreviewDebug", Description);
}
#else
void AppendPatternPreviewDebug(std::string_view) noexcept {}
#endif
bool SameFileContent(const std::filesystem::path &First, const std::filesystem::path &Second) noexcept
{
    try
    {
        std::error_code FirstError, SecondError;
        const auto FirstBytes = std::filesystem::file_size(First, FirstError);
        const auto SecondBytes = std::filesystem::file_size(Second, SecondError);
        if (FirstError || SecondError || FirstBytes != SecondBytes) return false;
        std::ifstream A(First, std::ios::binary), B(Second, std::ios::binary);
        if (!A || !B) return false;
        std::array<char, 8192> BlockA{}, BlockB{};
        for (;;)
        {
            A.read(BlockA.data(), std::streamsize(BlockA.size()));
            B.read(BlockB.data(), std::streamsize(BlockB.size()));
            const auto CountA = A.gcount(), CountB = B.gcount();
            if (CountA != CountB) return false;
            if (!CountA) return true;
            for (std::streamsize At = 0; At < CountA; ++At)
                if (BlockA[size_t(At)] != BlockB[size_t(At)]) return false;
        }
    }
    catch (...) { return false; }
}
}

int RunTextureWorkspace()
{
    Frontier::DiagnosticJournal::Append("Startup", "Texture Workspace: Material Library / Preview / Inspector / experimental Pattern preview");
#ifdef _DEBUG
    AppendPatternPreviewDebug("Debug-only Pattern GPU tracing active; log=%LOCALAPPDATA%/Frontier/Logs/TextureWorkspace-*.log");
#endif
    std::filesystem::path Directory;
    std::filesystem::path ShaderDirectory = "EngineContent/ShaderArchives/Surface";
    int ArgumentCount = 0;
    auto **Arguments = CommandLineToArgvW(GetCommandLineW(), &ArgumentCount);
    for (int Index = 1; Arguments && Index < ArgumentCount; ++Index)
    {
        if (std::wstring(Arguments[Index]) == L"--library" && Index+1 < ArgumentCount) Directory = Arguments[++Index];
        else if (std::wstring(Arguments[Index]) == L"--shaders" && Index+1 < ArgumentCount) ShaderDirectory = Arguments[++Index];
        else
        {
            std::cerr << "Usage: TextureWorkspace.exe [--library ContentDirectory] [--shaders ShaderDirectory]\n";
            LocalFree(Arguments);
            return 2;
        }
    }
    if (Arguments) LocalFree(Arguments);
    std::array<wchar_t, 32768> ExecutablePath{};
    const auto PathLength = GetModuleFileNameW(nullptr, ExecutablePath.data(), DWORD(ExecutablePath.size()));
    const auto ExecutableDirectory = PathLength && PathLength < ExecutablePath.size() ?
        std::filesystem::path(ExecutablePath.data()).parent_path() : std::filesystem::current_path();
    if (!std::filesystem::exists(ShaderDirectory) && ShaderDirectory == "EngineContent/ShaderArchives/Surface")
        ShaderDirectory = ExecutableDirectory / ShaderDirectory;
    const auto ContentRoot = std::filesystem::exists("EngineContent/FontArchives") ? std::filesystem::current_path() : ExecutableDirectory;
    Frontier::DiagnosticJournal::Append("Paths", "Executable=" + Frontier::DiagnosticJournal::EncodePath(ExecutableDirectory));
    Frontier::DiagnosticJournal::Append("Paths", "Content=" + Frontier::DiagnosticJournal::EncodePath(ContentRoot) + " shaders=" + Frontier::DiagnosticJournal::EncodePath(ShaderDirectory));
    Frontier::ActivationOwnership AppearanceOwnership, LibraryOwnership, PatternOwnership;
    Frontier::ControlCentrePanel *Appearance = nullptr;
    Frontier::MaterialLibraryPanel *Library = nullptr;
    Frontier::PatternWorkspacePanel *Pattern = nullptr;
    GLFWwindow *Window = nullptr;
    std::unique_ptr<Frontier::MaterialLibraryRendering> Rendering;
    std::unique_ptr<Frontier::VectorSurfaceMaterialBridge> MaterialBridge;
    std::unique_ptr<Frontier::VectorAuthoringCoverageRendering> CoverageRendering;
    std::optional<std::array<Frontier::MaterialTexturePyramid, 6>> PatternAuthoringMaps;
    std::optional<std::array<Frontier::MaterialTexturePyramid, 6>> AssignedPatternMaps;
    std::filesystem::path AssignedPatternSelection;
    bool PatternApplyRequested = false;
    Frontier::VectorAuthoringDocument SubmittedPatternDocument;
    std::optional<Frontier::VectorAuthoringDocument> ObservedPatternDocument;
    const std::filesystem::path PatternPreviewPath = "Pattern/live-preview";
    uint64_t PatternBridgeSubmission = 0;
    bool PatternBridgeRecorded = false;
    std::unique_ptr<Frontier::VectorSelectionEvaluation> SelectionEvaluation;
    std::unique_ptr<Frontier::MaterialThumbnailAtlas> PatternAtlas;
    VkDescriptorSet AtlasDescriptor = VK_NULL_HANDLE, PreviewDescriptor = VK_NULL_HANDLE;
    VkDescriptorSet PatternAtlasDescriptor = VK_NULL_HANDLE;
    uint64_t PatternAtlasPublished = 0;
    std::map<std::pair<size_t, uint32_t>, VkDescriptorSet> CoverageDescriptors;
    std::filesystem::path PatternSource;
    bool PatternActive = false;
    uint64_t RenderingRevision = UINT64_MAX, LayoutResetRevision = 0;
    bool LayoutBlocked = false;
    Frontier::ApplicationWindowCallbacks Callbacks;
    Callbacks.Title = "TextureWorkspace - Material Library [" FRONTIER_BUILD_COMMIT "]";
    Callbacks.MaterialRendering = true;
    Callbacks.Initialize = [&](GLFWwindow *CurrentWindow)
    {
        Window = CurrentWindow;
        Frontier::IconPresentation::Attach(Frontier::EncodeContentPath(ContentRoot / "EngineContent/Icons"));
        const wchar_t *LocalDirectory = _wgetenv(L"LOCALAPPDATA");
        const auto Preferences = LocalDirectory ? std::filesystem::path(LocalDirectory)/L"Frontier/Personal.projectspace" : std::filesystem::path{};
        AppearanceOwnership.reset(new Frontier::ControlCentrePanel(ContentRoot / "EngineContent/FontArchives", Preferences));
        Appearance = static_cast<Frontier::ControlCentrePanel *>(AppearanceOwnership.get());
        Appearance->ConfigureContentBrowserVisibility(false);
        if (!Appearance->Initialize() || !Appearance->Activate()) throw std::runtime_error(Appearance->QueryDiagnostic());
        LibraryOwnership.reset(new Frontier::MaterialLibraryPanel());
        Library = static_cast<Frontier::MaterialLibraryPanel *>(LibraryOwnership.get());
        Library->PresentIcon = [](Frontier::IconSymbol Symbol, Frontier::PlanePoint Point, float Size, Frontier::ColorQuad Tint)
        {
            return Frontier::IconPresentation::Draw(ImGui::GetWindowDrawList(), Symbol, ImVec2(Point.X, Point.Y), Size, ImGui::GetStyle().Alpha,
                Frontier::IconDomain::Texture, ImGui::ColorConvertFloat4ToU32(ImVec4(Tint.Red, Tint.Green, Tint.Blue, Tint.Alpha)));
        };
        if (!Library->Initialize() || !Library->Activate()) throw std::runtime_error("Material Library activation failed.");
        if (auto *Registry = Frontier::TypefaceRegistry::QueryCurrent())
        {
            const int32_t Family = Registry->ResolveFamilyIndex("DM Sans");
            if (Family >= 0) Library->ConfigureTypeface(Registry->QueryHandle(uint32_t(Family), Frontier::FontWeightCategory::Regular),
                Registry->QueryHandle(uint32_t(Family), Frontier::FontWeightCategory::Light));
        }
        if (Directory.empty())
        {
            const auto Presets = ContentRoot / "EngineContent/MaterialArchives/TextureStudio";
            Directory = LocalDirectory ? std::filesystem::path(LocalDirectory) / "Frontier/TextureStudio/Materials" : Presets;
            if (Directory != Presets)
            {
                // 📝 Mirror the shipped preset tree (materials, Textures/ maps, presets) with skip_existing, so
                // fresh installs resolve texture files and existing installs backfill missing additions
                // without touching user-edited materials.
                std::filesystem::create_directories(Directory);
                size_t Copied = 0, Scanned = 0, Refreshed = 0;
                for (const auto &Entry : std::filesystem::recursive_directory_iterator(Presets,
                    std::filesystem::directory_options::skip_permission_denied))
                {
                    if (!Entry.is_regular_file() || Copied + Refreshed >= 2048) continue;
                    const auto Relative = Entry.path().lexically_relative(Presets);
                    if (Relative.empty() || *Relative.begin() == "..") continue;
                    const auto Target = Directory / Relative;
                    std::error_code CopyError;
                    std::filesystem::create_directories(Target.parent_path(), CopyError);
                    if (CopyError) continue;
                    ++Scanned;
                    if (std::filesystem::exists(Target, CopyError) && !CopyError)
                    {
                        // 📝 skip_existing froze user libraries at their first seed, so shipped fixes (phase-1b
                        // texture authoring) never arrived. Refresh changed files, keeping a versioned backup of
                        // the displaced user copy so no edit is silently lost.
                        if (SameFileContent(Entry.path(), Target)) continue;
                        const std::string Stem = Target.filename().string() + ".user-backup";
                        std::filesystem::path Backup = Target.parent_path() / Stem;
                        for (int Version = 2; Version <= 8 && std::filesystem::exists(Backup, CopyError); ++Version)
                            Backup = Target.parent_path() / (Stem + "-" + std::to_string(Version));
                        std::filesystem::rename(Target, Backup, CopyError);
                        if (CopyError) continue;
                        if (!std::filesystem::copy_file(Entry.path(), Target, CopyError) || CopyError) continue;
                        ++Refreshed;
                        Frontier::DiagnosticJournal::Append("MaterialRefresh", Frontier::DiagnosticJournal::EncodePath(Relative));
                        continue;
                    }
                    if (CopyError) continue;
                    if (std::filesystem::copy_file(Entry.path(), Target, CopyError) && !CopyError) ++Copied;
                }
                Frontier::DiagnosticJournal::Append("MaterialBackfill", "scanned=" + std::to_string(Scanned) + " copied=" +
                    std::to_string(Copied) + " refreshed=" + std::to_string(Refreshed));
            }
        }
        if (!Library->ConfigureDirectory(Directory)) throw std::runtime_error(Library->QueryLibrary().QueryDiagnostic());
        if (std::filesystem::is_regular_file(Directory / "racing-green.material")) (void)Library->SelectMaterial("racing-green.material");
        PatternOwnership.reset(new Frontier::PatternWorkspacePanel());
        Pattern = static_cast<Frontier::PatternWorkspacePanel *>(PatternOwnership.get());
        if (!Pattern->Initialize() || !Pattern->Activate()) throw std::runtime_error("Pattern workspace activation failed.");
        (void)Pattern->ConfigurePresetDirectory(ContentRoot / "EngineContent/PatternArchives/TextureStudio");
        PatternSource = Directory / "pattern-workspace.vector.toml";
        Pattern->SaveSource = [&](std::string_view Source)
        {
            if (Source.size() > 4000000) return false;
            std::string Diagnostic;
            return Frontier::TextFileReplacement::Finalize(PatternSource, Source, Diagnostic);
        };
        Pattern->LoadSource = [&]() -> std::optional<std::string>
        {
            std::ifstream Input(PatternSource, std::ios::binary | std::ios::ate);
            const auto Bytes = Input.tellg();
            if (!Input || Bytes < 0 || Bytes > 4000000) return std::nullopt;
            std::string Source(size_t(Bytes), '\0');
            Input.seekg(0);
            if (!Input.read(Source.data(), Bytes)) return std::nullopt;
            return Source;
        };
        Pattern->ActivateMaterial = [&]()
        {
            PatternActive = false;
            AppendPatternPreviewDebug("Pattern workspace deactivated; Material Library preview selected.");
        };
        Library->ActivatePattern = [&]()
        {
            PatternActive = true;
            AppendPatternPreviewDebug("Pattern workspace activated; Float32 authoring-map generation enabled.");
        };
        if (auto *Registry = Frontier::TypefaceRegistry::QueryCurrent())
        {
            const int32_t Family = Registry->ResolveFamilyIndex("DM Sans");
            if (Family >= 0) Pattern->ConfigureTypeface(Registry->QueryHandle(uint32_t(Family), Frontier::FontWeightCategory::Regular),
                Registry->QueryHandle(uint32_t(Family), Frontier::FontWeightCategory::Light));
        }
        if (std::filesystem::is_regular_file(PatternSource))
        {
            std::ifstream Input(PatternSource, std::ios::binary | std::ios::ate);
            const auto Bytes = Input.tellg();
            if (Input && Bytes >= 0 && Bytes <= 4000000)
            {
                std::string Source(size_t(Bytes), '\0');
                Input.seekg(0);
                if (Input.read(Source.data(), Bytes))
                {
                    try
                    {
                        Pattern->ConfigureDocument(Frontier::DecodeVectorAuthoring(Source));
                    }
                    catch (const std::exception &Failure)
                    {
                        Frontier::DiagnosticJournal::Append("PatternSourceRefused", Failure.what());
                    }
                }
            }
        }
        LayoutResetRevision = Appearance->QueryLayoutResetRevision();
        if (!Preferences.empty() && std::filesystem::exists(Preferences))
        {
            Frontier::ProjectSpaceDocument Document;
            std::string Message;
            if (!Frontier::ProjectSpaceCodec::Decode(Preferences, Document, Message)) LayoutBlocked = true;
            else
            {
                if (const auto Layout = Document.Sections["workspace"]["texture_studio_layout"]; Layout)
                    LayoutBlocked = !Layout.is_table() || !Library->QueryPanels().ConfigureLayout(*Layout.as_table(), Message);
                if (!LayoutBlocked)
                    if (const auto Layout = Document.Sections["workspace"]["texture_studio_pattern_layout"]; Layout)
                        LayoutBlocked = !Layout.is_table() || !Pattern->QueryPanels().ConfigureLayout(*Layout.as_table(), Message);
            }
            if (LayoutBlocked) Appearance->ConfigureLayoutDiagnostic("TextureWorkspace layout read refused; defaults remain session-only. " + Message);
        }
    };
    Callbacks.InitializeVulkan = [&](Frontier::VulkanPresentation &Display)
    {
        Rendering = std::make_unique<Frontier::MaterialLibraryRendering>();
        Rendering->Initialize(Display.QueryRasterAccess(), ShaderDirectory);
        MaterialBridge = std::make_unique<Frontier::VectorSurfaceMaterialBridge>();
        MaterialBridge->Initialize(Display.QueryRasterAccess(), ShaderDirectory / "VectorSurfaceCompute.spv", 512, 512);
        AppendPatternPreviewDebug("Vulkan material bridge initialized; authoring_extent=512x512; channels=Colour,Normal,Roughness,Metalness,Occlusion; shader=" + Frontier::DiagnosticJournal::EncodePath(ShaderDirectory / "VectorSurfaceCompute.spv"));
        AtlasDescriptor = ImGui_ImplVulkan_AddTexture(Rendering->QueryAtlasView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        PreviewDescriptor = ImGui_ImplVulkan_AddTexture(Rendering->QueryPreviewView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        PatternAtlas = std::make_unique<Frontier::MaterialThumbnailAtlas>();
        PatternAtlas->Initialize(Display.QueryRasterAccess(), Frontier::PatternWorkspacePanel::PatternAtlasTile,
            Frontier::PatternWorkspacePanel::PatternAtlasColumns);
        PatternAtlasDescriptor = ImGui_ImplVulkan_AddTexture(PatternAtlas->QueryImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        CoverageRendering = std::make_unique<Frontier::VectorAuthoringCoverageRendering>();
        CoverageRendering->Initialize(Display.QueryRasterAccess(), ShaderDirectory / "VectorCoverageCompute.spv");
        SelectionEvaluation = std::make_unique<Frontier::VectorSelectionEvaluation>();
        SelectionEvaluation->Initialize(Display.QueryRasterAccess(), ShaderDirectory / "VectorSelectionCompute.spv");
        Pattern->QueryCoverage = [&](const Frontier::VectorCoverageProgram &Program)
        {
            // 📝 Pooled slot extents keep their images alive until Finalize, so lazily retained
            // descriptors stay valid without ever freeing a set a previous frame still samples.
            const int Slot = CoverageRendering->SelectProgram(Program);
            if (Slot < 0) return uint64_t(0);
            const auto Key = std::make_pair(size_t(Slot), CoverageRendering->QuerySlotExtent(size_t(Slot)));
            const auto Position = CoverageDescriptors.find(Key);
            if (Position != CoverageDescriptors.end()) return uint64_t(Position->second);
            const auto View = CoverageRendering->QueryImageView(size_t(Slot));
            if (!View) return uint64_t(0);
            const auto Descriptor = ImGui_ImplVulkan_AddTexture(View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            CoverageDescriptors.emplace(Key, Descriptor);
            return uint64_t(Descriptor);
        };
        Pattern->QueryCoverageReady = [&]() { return CoverageRendering ? CoverageRendering->QuerySelectedReady() : true; };
        Pattern->QuerySelectionToken = [&](const Frontier::VectorAuthoringDocument &Document, Frontier::PlanePoint Point)
        {
            return SelectionEvaluation->QueryToken(Frontier::ConstructVectorSelectionProgram(Document), {Point.X, Point.Y});
        };
        Pattern->QueryPatternAtlas = [&]()
        {
            const auto Uv = PatternAtlas ? PatternAtlas->QueryUv(0) : Frontier::ThumbnailUvRectangle{};
            return Frontier::PatternWorkspacePanel::PatternAtlasImage{PatternAtlasDescriptor ? uint64_t(PatternAtlasDescriptor) : uint64_t(0),
                Uv.MinimumU, Uv.MinimumV, Uv.MaximumU, Uv.MaximumV};
        };
        const auto AssignPatternMapsToPreview = [&]()
        {
            // 📝 Pattern edits only reach the selected material through an explicit Apply to material press.
            if (!PatternAuthoringMaps) return false;
            const auto *Applied = Library->QueryLibrary().QueryApplied();
            if (!Applied) return false;
            auto PreviewMaterial = *Applied;
            PreviewMaterial.AuthoringMaps = *PatternAuthoringMaps;
            AssignedPatternMaps = *PatternAuthoringMaps;
            AssignedPatternSelection = Library->QueryLibrary().QuerySelection();
            Rendering->ConfigureSelection(PatternPreviewPath, PreviewMaterial);
            RenderingRevision = Library->QueryLibrary().QueryRevision();
            return true;
        };
        Display.ConfigureRendering([&](VkCommandBuffer Commands, uint64_t Completed, uint64_t Serial)
        {
            SelectionEvaluation->ConstructCommands(Commands, Completed, Serial);
            CoverageRendering->ConstructCommands(Commands, Completed, Serial);
            if (Pattern->ConsumeMaterialApplyRequest()) PatternApplyRequested = true;
            if (PatternApplyRequested && !PatternBridgeSubmission && PatternAuthoringMaps &&
                Pattern->QueryDocument() == SubmittedPatternDocument && AssignPatternMapsToPreview())
                PatternApplyRequested = false;
            if (PatternBridgeSubmission && PatternBridgeSubmission <= Completed)
            {
                AppendPatternPreviewDebug("GPU readback retired; submission=" + std::to_string(PatternBridgeSubmission) + "; completed=" + std::to_string(Completed));
                auto ReplacementMaps = MaterialBridge->RetrieveAuthoringMaps(Completed);
                if (Pattern->QueryDocument() == SubmittedPatternDocument)
                {
                    PatternAuthoringMaps = std::move(ReplacementMaps);
                    AppendPatternPreviewDebug("Float32 authoring maps accepted; extent=512x512; channel_count=6; awaiting Apply to material.");
                    if (PatternApplyRequested && AssignPatternMapsToPreview()) PatternApplyRequested = false;
                }
                else
                {
                    PatternAuthoringMaps.reset();
                    AppendPatternPreviewDebug("Discarded retired Float32 maps because the authored Pattern changed while GPU work was in flight.");
                }
                PatternBridgeSubmission = 0;
            }
            if (PatternActive && !PatternBridgeSubmission && Pattern->QueryAuthoringSettled())
            {
                // 📝 Gestures publish a fresh draft every frame; regenerate Float32 maps only after the gesture ends.
                const auto CurrentPattern = Pattern->QueryDocument();
                if (CurrentPattern == SubmittedPatternDocument) ObservedPatternDocument.reset();
                else if (ObservedPatternDocument && *ObservedPatternDocument == CurrentPattern)
                {
                    ObservedPatternDocument.reset();
                    if (MaterialBridge->ConfigureDocument(CurrentPattern, Completed))
                    {
                        SubmittedPatternDocument = CurrentPattern;
                        AppendPatternPreviewDebug("Pattern document compiled; layers=" + std::to_string(SubmittedPatternDocument.Layers.size()) + "; instructions=" + std::to_string(SubmittedPatternDocument.Instructions.size()) + "; cubic_segments=" + std::to_string(SubmittedPatternDocument.Segments.size()) + "; completed_serial=" + std::to_string(Completed));
                        PatternAuthoringMaps.reset();
                        const bool AssignedSnapshotLive = AssignedPatternMaps.has_value() &&
                            AssignedPatternSelection == Library->QueryLibrary().QuerySelection();
                        if (!AssignedSnapshotLive)
                        {
                            if (const auto *Applied = Library->QueryLibrary().QueryApplied())
                            {
                                Rendering->ConfigureSelection(Library->QueryLibrary().QuerySelection(), *Applied);
                                RenderingRevision = Library->QueryLibrary().QueryRevision();
                                AppendPatternPreviewDebug("Selected-material preview restored without pending Pattern maps while Float32 regeneration is in flight.");
                            }
                        }
                        if (MaterialBridge->ConstructCommands(Commands, Completed, Serial))
                        {
                            PatternBridgeSubmission = Serial;
                            PatternBridgeRecorded = true;
                            AppendPatternPreviewDebug("GPU Float32 map generation commands recorded; submission=" + std::to_string(Serial));
                        }
                    }
                }
                else ObservedPatternDocument = CurrentPattern;
            }
            // 📝 The gallery bakes its 24-card page on the CPU; upload once per page flip, gated by the panel revision.
            const uint64_t PatternAtlasRevision = Pattern->QueryPatternAtlasRevision();
            if (PatternAtlas && PatternAtlasRevision != PatternAtlasPublished)
            {
                PatternAtlas->StagePixels(Pattern->QueryPatternAtlasPixels());
                PatternAtlas->ConstructUpload(Commands);
                PatternAtlasPublished = PatternAtlasRevision;
            }
            Rendering->ConstructCommands(Commands, Completed, Serial);
        }, [&]()
        {
            SelectionEvaluation->ConfirmSubmission();
            if (PatternAtlas) PatternAtlas->ConfirmSubmission();
            if (PatternBridgeRecorded)
            {
                MaterialBridge->ConfirmSubmission();
                AppendPatternPreviewDebug("GPU Float32 map generation submission confirmed; submission=" + std::to_string(PatternBridgeSubmission));
                PatternBridgeRecorded = false;
            }
            CoverageRendering->ConfirmSubmission();
            Rendering->ConfirmSubmission();
        });
        Library->RequestThumbnails = [&](const std::vector<std::filesystem::path> &Paths)
        {
            Rendering->RequestLibrary(Library->QueryLibrary().QueryDirectory(), Paths);
        };
        Library->QueryThumbnail = [&](const std::filesystem::path &Path)
        {
            const auto Thumbnail = Rendering->QueryThumbnail(Path);
            const auto &Uv = Thumbnail.Uv;
            return Frontier::MaterialPreviewImage{Thumbnail.Ready ? (uint64_t)AtlasDescriptor : 0,
                Uv.MinimumU, Uv.MinimumV, Uv.MaximumU, Uv.MaximumV};
        };
        Library->InvalidateThumbnails = [&]() { Rendering->InvalidateThumbnails(); };
        Library->QueryThumbnailDiagnostic = [&](const std::filesystem::path &Path) { return Rendering->QueryThumbnailDiagnostic(Path); };
        Library->ConfigurePreview = [&](const Frontier::MaterialPreviewConfiguration &Configuration) { Rendering->ConfigurePreview(Configuration); };
        Library->QueryPreview = [&]()
        {
            return Frontier::MaterialPreviewImage{Rendering->QueryPreviewReady() ? (uint64_t)PreviewDescriptor : 0};
        };
    };
    Callbacks.ConfigurePlatform = [&]()
    {
        if (Appearance->QueryLayoutResetRevision() != LayoutResetRevision)
        {
            Library->QueryPanels().ResetLayout();
            Pattern->QueryPanels().ResetLayout();
            LayoutBlocked = false;
            LayoutResetRevision = Appearance->QueryLayoutResetRevision();
        }
        Library->QueryPanels().ConfigureDetachedWindows(Appearance->QueryDetachedWindows());
        Pattern->QueryPanels().ConfigureDetachedWindows(Appearance->QueryDetachedWindows());
        Library->ConfigurePlatform();
        (void)Pattern->QueryPanels().ConfigurePlatform();
        if (glfwWindowShouldClose(Window))
        {
            const bool Authorized = PatternActive ? Pattern->RequestClose() : Library->RequestClose();
            if (!Authorized) glfwSetWindowShouldClose(Window, GLFW_FALSE);
        }
    };
    Callbacks.PrepareInterface = [&]() { Appearance->PrepareInterface(); };
    Callbacks.ConstructInterface = [&](bool MainVisible)
    {
        (void)Appearance->Integrate(ImGui::GetIO().DeltaTime);
        if (PatternActive) (void)Pattern->Integrate(ImGui::GetIO().DeltaTime);
        else (void)Library->Integrate(ImGui::GetIO().DeltaTime);
        if (MainVisible)
        {
            if (PatternActive)
            {
                Pattern->EvaluateNavigationDelta = [&](Frontier::PlanePoint Displacement)
                {
                    return Appearance->QueryInput().QueryApplied().EvaluatePointerDelta(Displacement);
                };
                Pattern->ConfigureInterfaceScale(Appearance->QueryAppearance().QueryApplied().InterfaceScale / 100.f);
                Pattern->ConstructInterface();
            }
            else
            {
                if (Rendering) Rendering->RequestLibrary(Library->QueryLibrary().QueryDirectory(), {});
                Library->EvaluateNavigationDelta = [&](Frontier::PlanePoint Displacement)
                {
                    return Appearance->QueryInput().QueryApplied().EvaluatePointerDelta(Displacement);
                };
                Library->ConfigureInterfaceScale(Appearance->QueryAppearance().QueryApplied().InterfaceScale / 100.f);
                Library->ConstructInterface();
                const auto &Catalogue = Library->QueryLibrary();
                if (Rendering && Catalogue.QueryApplied() && RenderingRevision != Catalogue.QueryRevision())
                {
                    if (Catalogue.QuerySelection() != AssignedPatternSelection) AssignedPatternMaps.reset();
                    if (AssignedPatternMaps)
                    {
                        auto PreviewMaterial = *Catalogue.QueryApplied();
                        PreviewMaterial.AuthoringMaps = *AssignedPatternMaps;
                        Rendering->ConfigureSelection(PatternPreviewPath, PreviewMaterial);
                    }
                    else Rendering->ConfigureSelection(Catalogue.QuerySelection(), *Catalogue.QueryApplied());
                    RenderingRevision = Catalogue.QueryRevision();
                }
                if (Library->ConsumeClose()) glfwSetWindowShouldClose(Window, GLFW_TRUE);
            }
            Appearance->ConstructInterface();
        }
    };
    Callbacks.Finalize = [&]()
    {
        // 📝 The shared application loop has retired all GPU work before this callback.
        if (AtlasDescriptor) ImGui_ImplVulkan_RemoveTexture(AtlasDescriptor);
        if (PreviewDescriptor) ImGui_ImplVulkan_RemoveTexture(PreviewDescriptor);
        if (PatternAtlasDescriptor) ImGui_ImplVulkan_RemoveTexture(PatternAtlasDescriptor);
        for (const auto &Entry : CoverageDescriptors)
            if (Entry.second) ImGui_ImplVulkan_RemoveTexture(Entry.second);
        CoverageDescriptors.clear();
        if (SelectionEvaluation) SelectionEvaluation->Finalize();
        if (MaterialBridge) MaterialBridge->Finalize();
        if (CoverageRendering) CoverageRendering->Finalize();
        if (PatternAtlas) PatternAtlas->Finalize();
        SelectionEvaluation.reset();
        MaterialBridge.reset();
        CoverageRendering.reset();
        PatternAtlas.reset();
        Rendering.reset();
        if (Library && Appearance && !LayoutBlocked)
        {
            std::string Message;
            if (Library->QueryPanels().QuerySeated())
            {
                const auto Layout = Library->QueryPanels().EncodeLayout();
                if (!Layout.empty() && !Appearance->PersistWorkspaceLayout(Layout, Message, "texture_studio_layout"))
                {
                    Frontier::DiagnosticJournal::Append("LayoutSaveError", Message);
                    std::cerr << "TextureWorkspace layout not saved: " << Message << '\n';
                }
            }
            if (Pattern && Pattern->QueryPanels().QuerySeated())
            {
                Message.clear();
                const auto Layout = Pattern->QueryPanels().EncodeLayout();
                if (!Layout.empty() && !Appearance->PersistWorkspaceLayout(Layout, Message, "texture_studio_pattern_layout"))
                {
                    Frontier::DiagnosticJournal::Append("PatternLayoutSaveError", Message);
                    std::cerr << "TextureWorkspace Pattern layout not saved: " << Message << '\n';
                }
            }
        }
        PatternOwnership.reset();
        LibraryOwnership.reset();
        AppearanceOwnership.reset();
    };
    return Frontier::RunWindowsApplication(Callbacks);
}

int main()
{
    Frontier::DiagnosticJournal::Initialize("TextureWorkspace");
    try
    {
        return RunTextureWorkspace();
    }
    catch (const std::exception &Failure)
    {
        Frontier::DiagnosticJournal::Append("SEVERE", Failure.what());
        const auto Description = std::string("Startup failed: ") + Failure.what() + "\nLogs: " +
            Frontier::DiagnosticJournal::EncodePath(Frontier::DiagnosticJournal::QueryFilename());
        MessageBoxA(nullptr, Description.c_str(), "Texture Workspace - Severe failure", MB_OK | MB_ICONERROR | MB_TASKMODAL);
        return 1;
    }
    catch (...)
    {
        Frontier::DiagnosticJournal::Append("SEVERE", "Unknown startup failure");
        MessageBoxW(nullptr, L"Unknown startup failure. Collect %LOCALAPPDATA%/Frontier/Logs.",
            L"Texture Workspace - Severe failure", MB_OK | MB_ICONERROR | MB_TASKMODAL);
        return 1;
    }
}
