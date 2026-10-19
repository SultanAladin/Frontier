/*==========================================================================================================================================
                                                        WINDOWSTEXTURESTUDIO.CPP
==========================================================================================================================================*/
// 🧩 Host native Material and Pattern workspaces through the shared Engine lifecycle, docking and Vulkan presentation.

#define GLFW_INCLUDE_NONE
#include "Engine/Interface/WindowsApplication.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include "Engine/Interface/VulkanPresentation.h"
#include "Engine/ContentInterchange/ProjectSpaceCodec.h"
#include "Engine/Render/MaterialLibraryRendering.h"
#include "Engine/Render/VectorAuthoringCoverageRendering.h"
#include "Engine/Render/VectorSelectionEvaluation.h"
#include "Engine/Interface/TextFileReplacement.h"
#include <imgui_impl_vulkan.h>
#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/ContentDirectory.h"
#include "Engine/Interface/TypefaceRegistry.h"
#include "UI/Editor/ControlCentrePanel.h"
#include "UI/TextureStudio/MaterialLibraryPanel.h"
#include "UI/TextureStudio/PatternWorkspacePanel.h"
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <fstream>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <iostream>

//------------------------------------------------------------------------------------------------------------------------
//                                                     STANDALONE APPLICATION
//------------------------------------------------------------------------------------------------------------------------

int RunTextureWorkspace()
{
    Frontier::DiagnosticJournal::Append("Startup", "Texture Workspace: Material Library and native Pattern UI port");
    bool PatternActive = false, QuitRequested = false;
    std::filesystem::path Directory;
    std::filesystem::path ShaderDirectory = "EngineContent/ShaderArchives/Surface";
    int ArgumentCount = 0;
    auto **Arguments = CommandLineToArgvW(GetCommandLineW(), &ArgumentCount);
    for (int Index = 1; Arguments && Index < ArgumentCount; ++Index)
    {
        if (std::wstring(Arguments[Index]) == L"--library" && Index+1 < ArgumentCount) Directory = Arguments[++Index];
        else if (std::wstring(Arguments[Index]) == L"--pattern") PatternActive = true;
        else if (std::wstring(Arguments[Index]) == L"--shaders" && Index+1 < ArgumentCount) ShaderDirectory = Arguments[++Index];
        else
        {
            std::cerr << "Usage: TextureWorkspace.exe [--library ContentDirectory] [--shaders ShaderDirectory] [--pattern]\n";
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
    std::unique_ptr<Frontier::VectorAuthoringCoverageRendering> PatternRendering;
    std::unique_ptr<Frontier::VectorSelectionEvaluation> PatternSelection;
    std::array<VkDescriptorSet, 3> PatternDescriptors{};
    uint64_t PatternLayoutResetRevision = 0;
    std::filesystem::path PatternFilename;
    std::string PatternGpuDiagnostic;
    VkDescriptorSet AtlasDescriptor = VK_NULL_HANDLE, PreviewDescriptor = VK_NULL_HANDLE;
    uint64_t RenderingRevision = UINT64_MAX, LayoutResetRevision = 0;
    bool LayoutBlocked = false;
    Frontier::ApplicationWindowCallbacks Callbacks;
    Callbacks.Title = "TextureWorkspace - Material Library";
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
        PatternOwnership.reset(new Frontier::PatternWorkspacePanel());
        Pattern = static_cast<Frontier::PatternWorkspacePanel *>(PatternOwnership.get());
        if (!Pattern->Initialize()) throw std::runtime_error("Pattern workspace initialization failed.");
        if (PatternActive && !Pattern->Activate()) throw std::runtime_error("Pattern workspace activation failed.");
        Pattern->ActivateMaterial = [&]()
        {
            PatternActive = false;
            (void)Pattern->Suspend();
            Frontier::DiagnosticJournal::Append("Workspace", "Material");
            if (QuitRequested)
            {
                QuitRequested = false;
                if (Library->RequestClose()) glfwSetWindowShouldClose(Window, GLFW_TRUE);
            }
        };
        Pattern->CancelClose = [&]() { QuitRequested = false; };
        Library->ActivatePattern = [&]()
        {
            if (!PatternRendering)
            {
                MessageBoxA(GetActiveWindow(), PatternGpuDiagnostic.c_str(), "Pattern GPU initialization unavailable", MB_OK | MB_ICONWARNING);
                return;
            }
            PatternActive = true;
            (void)Pattern->Activate();
            Frontier::DiagnosticJournal::Append("Workspace", "Pattern");
        };
        Pattern->SaveSource = [&](std::string_view Source)
        {
            std::array<wchar_t, 32768> Filename{};
            if (!PatternFilename.empty()) std::copy_n(PatternFilename.c_str(), std::min(PatternFilename.native().size(), Filename.size() - 1), Filename.data());
            else std::copy_n(L"Pattern.pattern", 15, Filename.data());
            OPENFILENAMEW Dialogue{};
            Dialogue.lStructSize = sizeof(Dialogue);
            Dialogue.hwndOwner = GetActiveWindow();
            Dialogue.lpstrFilter = L"Native Pattern (*.pattern)\0*.pattern\0\0";
            Dialogue.lpstrFile = Filename.data();
            Dialogue.nMaxFile = DWORD(Filename.size());
            Dialogue.lpstrDefExt = L"pattern";
            Dialogue.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (!GetSaveFileNameW(&Dialogue)) return false;
            std::string Diagnostic;
            if (!Frontier::TextFileReplacement::Finalize(Filename.data(), Source, Diagnostic)) throw std::runtime_error(Diagnostic);
            PatternFilename = Filename.data();
            Frontier::DiagnosticJournal::Append("PatternSave", Frontier::DiagnosticJournal::EncodePath(PatternFilename));
            return true;
        };
        Pattern->LoadSource = [&]() -> std::optional<std::string>
        {
            std::array<wchar_t, 32768> Filename{};
            OPENFILENAMEW Dialogue{};
            Dialogue.lStructSize = sizeof(Dialogue);
            Dialogue.hwndOwner = GetActiveWindow();
            Dialogue.lpstrFilter = L"Native Pattern (*.pattern)\0*.pattern\0\0";
            Dialogue.lpstrFile = Filename.data();
            Dialogue.nMaxFile = DWORD(Filename.size());
            Dialogue.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (!GetOpenFileNameW(&Dialogue)) return std::nullopt;
            std::ifstream Source(std::filesystem::path(Filename.data()), std::ios::binary | std::ios::ate);
            const auto Bytes = Source.tellg();
            if (!Source || Bytes < 0 || Bytes > 600000) throw std::runtime_error("Native Pattern source is unreadable or exceeds 600 KB.");
            std::string Text(size_t(Bytes), '\0');
            Source.seekg(0);
            if (!Source.read(Text.data(), Bytes)) throw std::runtime_error("Native Pattern read failed.");
            (void)Frontier::DecodeVectorAuthoring(Text);
            PatternFilename = Filename.data();
            return Text;
        };
        if (auto *Registry = Frontier::TypefaceRegistry::QueryCurrent())
        {
            const int32_t Family = Registry->ResolveFamilyIndex("DM Sans");
            if (Family >= 0)
            {
                auto *Regular = Registry->QueryHandle(uint32_t(Family), Frontier::FontWeightCategory::Regular);
                auto *Light = Registry->QueryHandle(uint32_t(Family), Frontier::FontWeightCategory::Light);
                Library->ConfigureTypeface(Regular, Light);
                Pattern->ConfigureTypeface(Regular, Light);
            }
        }
        if (Directory.empty())
        {
            const auto Presets = ContentRoot / "EngineContent/MaterialArchives/TextureStudio";
            Directory = LocalDirectory ? std::filesystem::path(LocalDirectory) / "Frontier/TextureStudio/Materials" : Presets;
            if (Directory != Presets)
            {
                std::filesystem::create_directories(Directory);
                for (const auto &Entry : std::filesystem::directory_iterator(Presets))
                    if (Entry.is_regular_file()) std::filesystem::copy_file(Entry.path(), Directory / Entry.path().filename(), std::filesystem::copy_options::skip_existing);
            }
        }
        if (!Library->ConfigureDirectory(Directory)) throw std::runtime_error(Library->QueryLibrary().QueryDiagnostic());
        if (std::filesystem::is_regular_file(Directory / "racing-green.material")) (void)Library->SelectMaterial("racing-green.material");
        LayoutResetRevision = Appearance->QueryLayoutResetRevision();
        PatternLayoutResetRevision = LayoutResetRevision;
        if (!Preferences.empty() && std::filesystem::exists(Preferences))
        {
            Frontier::ProjectSpaceDocument Document;
            std::string Message;
            if (!Frontier::ProjectSpaceCodec::Decode(Preferences, Document, Message)) LayoutBlocked = true;
            else if (const auto Layout = Document.Sections["workspace"]["texture_studio_layout"]; Layout)
                LayoutBlocked = !Layout.is_table() || !Library->QueryPanels().ConfigureLayout(*Layout.as_table(), Message);
            if (!LayoutBlocked)
                if (const auto Layout = Document.Sections["workspace"]["pattern_workspace_layout"]; Layout)
                    LayoutBlocked = !Layout.is_table() || !Pattern->QueryPanels().ConfigureLayout(*Layout.as_table(), Message);
            if (LayoutBlocked) Appearance->ConfigureLayoutDiagnostic("TextureWorkspace layout read refused; defaults remain session-only. " + Message);
        }
    };
    Callbacks.InitializeVulkan = [&](Frontier::VulkanPresentation &Display)
    {
        Rendering = std::make_unique<Frontier::MaterialLibraryRendering>();
        Rendering->Initialize(Display.QueryRasterAccess(), ShaderDirectory);
        AtlasDescriptor = ImGui_ImplVulkan_AddTexture(Rendering->QueryAtlasView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        PreviewDescriptor = ImGui_ImplVulkan_AddTexture(Rendering->QueryPreviewView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        try
        {
            PatternRendering = std::make_unique<Frontier::VectorAuthoringCoverageRendering>();
            PatternRendering->Initialize(Display.QueryRasterAccess(), ShaderDirectory / "VectorCoverageCompute.spv");
            PatternSelection = std::make_unique<Frontier::VectorSelectionEvaluation>();
            PatternSelection->Initialize(Display.QueryRasterAccess(), ShaderDirectory / "VectorSelectionCompute.spv");
            for (size_t Slot = 0; Slot < PatternDescriptors.size(); ++Slot)
                PatternDescriptors[Slot] = ImGui_ImplVulkan_AddTexture(PatternRendering->QueryImageView(Slot), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        catch (const std::exception &Failure)
        {
            PatternGpuDiagnostic = Failure.what();
            PatternSelection.reset();
            PatternRendering.reset();
            Frontier::DiagnosticJournal::Append("PatternGpuUnavailable", PatternGpuDiagnostic);
            if (PatternActive) throw;
        }
        Pattern->QueryCoverage = [&](const Frontier::VectorCoverageProgram &Program) -> uint64_t
        {
            // ⚠️ Secondary-window submissions do not yet participate in the authoring image retirement serials.
            if (!PatternRendering || ImGui::GetWindowViewport() != ImGui::GetMainViewport()) return 0;
            const int Slot = PatternRendering->SelectProgram(Program);
            return Slot < 0 ? 0 : (uint64_t)PatternDescriptors[size_t(Slot)];
        };
        Pattern->QuerySelectionToken = [&](const Frontier::VectorAuthoringDocument &Document, Frontier::PlanePoint Point)
        {
            if (!PatternSelection || ImGui::GetWindowViewport() != ImGui::GetMainViewport())
                throw std::runtime_error("GPU contour selection is unavailable in this viewport; use the layer list.");
            return PatternSelection->QueryToken(Frontier::ConstructVectorSelectionProgram(Document), {Point.X, Point.Y});
        };
        Display.ConfigureRendering([&](VkCommandBuffer Commands, uint64_t Completed, uint64_t Serial)
        {
            Rendering->ConstructCommands(Commands, Completed, Serial);
            if (PatternSelection) PatternSelection->ConstructCommands(Commands, Completed, Serial);
            if (PatternRendering) PatternRendering->ConstructCommands(Commands, Completed, Serial);
        }, [&]()
        {
            Rendering->ConfirmSubmission();
            if (PatternSelection) PatternSelection->ConfirmSubmission();
            if (PatternRendering) PatternRendering->ConfirmSubmission();
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
            LayoutBlocked = false;
            LayoutResetRevision = Appearance->QueryLayoutResetRevision();
        }
        if (PatternLayoutResetRevision != Appearance->QueryLayoutResetRevision())
        {
            Pattern->QueryPanels().ResetLayout();
            PatternLayoutResetRevision = Appearance->QueryLayoutResetRevision();
        }
        Pattern->QueryPanels().ConfigureDetachedWindows(Appearance->QueryDetachedWindows());
        if (PatternActive) (void)Pattern->QueryPanels().ConfigurePlatform();
        Library->QueryPanels().ConfigureDetachedWindows(Appearance->QueryDetachedWindows());
        Library->ConfigurePlatform();
        if (glfwWindowShouldClose(Window))
        {
            if (Pattern->QueryModified())
            {
                QuitRequested = PatternActive = true;
                (void)Pattern->Activate();
                (void)Pattern->RequestClose();
                glfwSetWindowShouldClose(Window, GLFW_FALSE);
            }
            else if (!Library->RequestClose()) glfwSetWindowShouldClose(Window, GLFW_FALSE);
        }
    };
    Callbacks.PrepareInterface = [&]() { Appearance->PrepareInterface(); };
    Callbacks.ConstructInterface = [&](bool MainVisible)
    {
        (void)Appearance->Integrate(ImGui::GetIO().DeltaTime);
        (void)Library->Integrate(ImGui::GetIO().DeltaTime);
        (void)Pattern->Integrate(ImGui::GetIO().DeltaTime);
        if (MainVisible)
        {
            if (Rendering) Rendering->RequestLibrary(Library->QueryLibrary().QueryDirectory(), {});
            Library->EvaluateNavigationDelta = [&](Frontier::PlanePoint Displacement) { return Appearance->QueryInput().QueryApplied().EvaluatePointerDelta(Displacement); };
            Library->ConfigureInterfaceScale(Appearance->QueryAppearance().QueryApplied().InterfaceScale / 100.f);
            if (PatternActive)
            {
                Library->QueryPanels().PreserveDockspace();
                Pattern->EvaluateNavigationDelta = [&](Frontier::PlanePoint Δ) { return Appearance->QueryInput().QueryApplied().EvaluatePointerDelta(Δ); };
                Pattern->ConfigureInterfaceScale(Appearance->QueryAppearance().QueryApplied().InterfaceScale / 100.f);
                Pattern->ConstructInterface();
            }
            else
            {
                Pattern->QueryPanels().PreserveDockspace();
                Library->ConstructInterface();
            }
            const auto &Catalogue = Library->QueryLibrary();
            if (Rendering && Catalogue.QueryApplied() && RenderingRevision != Catalogue.QueryRevision())
            {
                Rendering->ConfigureSelection(Catalogue.QuerySelection(), *Catalogue.QueryApplied());
                RenderingRevision = Catalogue.QueryRevision();
            }
            if (Library->ConsumeClose()) glfwSetWindowShouldClose(Window, GLFW_TRUE);
            Appearance->ConstructInterface();
        }
    };
    Callbacks.Finalize = [&]()
    {
        // 📝 The shared application loop has retired all GPU work before this callback.
        if (AtlasDescriptor) ImGui_ImplVulkan_RemoveTexture(AtlasDescriptor);
        if (PreviewDescriptor) ImGui_ImplVulkan_RemoveTexture(PreviewDescriptor);
        for (auto Descriptor : PatternDescriptors) if (Descriptor) ImGui_ImplVulkan_RemoveTexture(Descriptor);
        PatternSelection.reset();
        PatternRendering.reset();
        Rendering.reset();
        if (Library && Appearance && Library->QueryPanels().QuerySeated() && !LayoutBlocked)
        {
            std::string Message;
            const auto Layout = Library->QueryPanels().EncodeLayout();
            if (!Layout.empty() && !Appearance->PersistWorkspaceLayout(Layout, Message, "texture_studio_layout"))
            {
                Frontier::DiagnosticJournal::Append("LayoutSaveError", Message);
                std::cerr << "TextureWorkspace layout not saved: " << Message << '\n';
            }
        }
        if (Pattern && Appearance && Pattern->QueryPanels().QuerySeated() && !LayoutBlocked)
        {
            std::string Message;
            const auto Layout = Pattern->QueryPanels().EncodeLayout();
            if (!Layout.empty() && !Appearance->PersistWorkspaceLayout(Layout, Message, "pattern_workspace_layout"))
                Frontier::DiagnosticJournal::Append("PatternLayoutSaveError", Message);
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
