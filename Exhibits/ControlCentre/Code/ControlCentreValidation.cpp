/*==========================================================================================================================================
                                                    CONTROLCENTREVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Exercise actual native appearance controls, persistence, font discovery, and CPU image capture.

#include "UI/Editor/ControlCentrePanel.h"
#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/FontCodec.h"
#include "CpuRasterization.h"
#include <imgui_internal.h>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                    VALIDATION SUPPORT
//------------------------------------------------------------------------------------------------------------------------

void Require(bool Condition, const char *Description)
{
    if (!Condition)
        throw std::runtime_error(Description);
}

void Capture(const std::filesystem::path &Path)
{
    constexpr int              Width  = 1100;
    constexpr int              Height = 1000;
    std::vector<unsigned char> Pixels(Width * Height * 3, 24);
    const auto                *Commands = ImGui::GetDrawData();
    for (const auto *List : Commands->CmdLists)
        FrontierExhibit::RasterizeCommands(List, Pixels.data(), Width, Height, Commands->DisplayPos,
                                           Commands->FramebufferScale);
    std::ofstream Output(Path, std::ios::binary);
    Output << "P6\n" << Width << ' ' << Height << "\n255\n";
    Output.write(reinterpret_cast<const char *>(Pixels.data()), Pixels.size());
    Require(Output.good(), "Could not save actual native draw output");
}
} // namespace Frontier

//------------------------------------------------------------------------------------------------------------------------
//                                                       VERIFICATION
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char **Arguments)
{
    using namespace Frontier;
    try
    {
        Require(ArgumentCount == 3, "Expected font directory and evidence output");
        const std::filesystem::path Fonts(Arguments[1]);
        const std::filesystem::path Evidence(Arguments[2]);
        std::filesystem::create_directories(Evidence);
        const auto PreferencesPath = Evidence / "Appearance.toml";
        std::filesystem::remove(PreferencesPath);
        FontCodec Catalogue;
        Require(Catalogue.ScanDirectory(Fonts.string()), "Native font discovery failed");
        Require(Catalogue.QueryFamilyCount() == 4 && Catalogue.QueryTotalVariantCount() == 30,
                "Downloaded font inventory differs");
        Require(Catalogue.ScanDirectory(Fonts.string()) && Catalogue.QueryTotalVariantCount() == 30,
                "Repeated discovery duplicates fonts");
        const auto LocalFonts = Evidence / "LocalFonts";
        std::filesystem::create_directories(LocalFonts);
        const auto LocalFace = LocalFonts / "LocalTypeface-Regular.ttf";
        std::filesystem::copy_file(Fonts / "Archivo/Archivo-Regular.ttf", LocalFace,
                                   std::filesystem::copy_options::overwrite_existing);
        Require(Catalogue.ScanDirectory(LocalFonts.string()) && Catalogue.QueryTotalVariantCount() == 1,
                "New local family was not discovered");
        std::filesystem::remove(LocalFace);
        Require(!Catalogue.ScanDirectory(LocalFonts.string()) && Catalogue.QueryTotalVariantCount() == 0,
                "Removed font survived through a stale descriptor");
        Require(Catalogue.ScanEngineAndGameContent(Fonts.string(), LocalFonts.string()) &&
                    Catalogue.QueryTotalVariantCount() == 30,
                "Empty application fonts erased shared fonts");

        ImGui::CreateContext();
        auto &IO       = ImGui::GetIO();
        IO.IniFilename = nullptr;
        IO.DisplaySize = ImVec2(1100, 1000);
        IO.DeltaTime   = 1.0f / 60.0f;
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth  = 4096;
        ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;
        IconPresentation::Attach((Fonts.parent_path() / "Icons").string());
        ActivationOwnership Ownership(new ControlCentrePanel(Fonts, PreferencesPath));
        auto               *Panel = static_cast<ControlCentrePanel *>(Ownership.get());
        Require(Panel->Initialize() && Panel->Activate(), "Control Centre initialization failed");
        Require(Panel->QueryTypefaces().QueryFamilyCount() == 4, "Registry did not load downloaded fonts");
        const auto Integrate = [&]()
        {
            (void)Panel->Integrate(1.0 / 60.0);
            Panel->PrepareInterface();
            ImGui::NewFrame();
            Panel->RecordInterface();
            ImGui::Render();
            FrontierExhibit::AcknowledgeTextures();
            Require(ImGui::GetDrawData()->TotalVtxCount > 0, "Control Centre produced no commands");
        };
        const auto Click = [&](PlaneExtent Extent)
        {
            IO.AddMousePosEvent((Extent.MinimumX + Extent.MaximumX) * 0.5f, (Extent.MinimumY + Extent.MaximumY) * 0.5f);
            Integrate();
            IO.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            Integrate();
            IO.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            Integrate();
        };
        for (int Ordinal = 0; Ordinal < 30; ++Ordinal)
            Integrate();
        Capture(Evidence / "Fonts.ppm");
        Click(Spanning(IO.DisplaySize.x * 0.5f - 100, 26, 200, 18));
        Require(!Panel->QueryQuickOpen(), "Quick-panel collapse did not respond");
        Click(Spanning(IO.DisplaySize.x * 0.5f - 100, 26, 200, 18));
        Require(Panel->QueryQuickOpen(), "Quick-panel expansion did not respond");
        IO.AddKeyEvent(ImGuiMod_Ctrl, true);
        IO.AddKeyEvent(ImGuiKey_Comma, true);
        Integrate();
        Require(!Panel->QueryCentreOpen(), "Keyboard shortcut did not close Control Centre");
        IO.AddKeyEvent(ImGuiKey_Comma, false);
        IO.AddKeyEvent(ImGuiMod_Ctrl, false);
        Integrate();
        Panel->ActivatePage(AppearancePage::Fonts);
        Integrate();
        for (auto *Window : ImGui::GetCurrentContext()->Windows)
            if (std::strstr(Window->Name, "Appearance content"))
                ImGui::SetScrollY(Window, 850.0f);
        Integrate();
        Integrate();
        Capture(Evidence / "TypeScale.ppm");
        for (auto *Window : ImGui::GetCurrentContext()->Windows)
            if (std::strstr(Window->Name, "Appearance content"))
                ImGui::SetScrollY(Window, 0.0f);
        Integrate();
        Integrate();
        Click(Panel->QueryAppearance().QueryFontCardExtent(1));
        Require(Panel->QueryAppearance().QueryDraft().FontFamily == 1, "Font card did not respond to click");
        Require(Panel->QueryAppearance().QueryApplied().FontFamily == 0, "Draft font applied prematurely");
        Panel->ResetPreferences();
        Require(!Panel->QueryAppearance().QueryDirty(), "Discard did not restore applied font");
        Panel->ActivatePage(AppearancePage::Theme);
        Integrate();
        Integrate();
        Capture(Evidence / "Theme.ppm");
        Click(Panel->QueryAppearance().QueryThemeTileExtent(2));
        Require(Panel->QueryAppearance().QueryDraft().Theme == ThemeCategory::Light,
                "Theme tile did not respond to native pointer events");
        Require(Panel->QueryAppearance().QueryApplied().Theme == ThemeCategory::Dark,
                "Theme draft applied prematurely");
        Require(Panel->FinalizePreferences(), "Could not save applied theme");
        Integrate();
        Integrate();
        Require(ControlKit::Palette().LightSurface, "Theme apply did not change the actual palette");
        Capture(Evidence / "LightTheme.ppm");

        auto Preferences           = Panel->QueryAppearance().QueryDraft();
        Preferences.FontFamily     = 1;
        Preferences.RoleSize[3]    = 18;
        Preferences.RoleWeight[3]  = FontWeightCategory::SemiBold;
        Preferences.InterfaceScale = 125;
        Panel->QueryAppearance().ConfigureDraft(Preferences);
        Require(Panel->FinalizePreferences(), "Typography apply failed");
        Integrate();
        Require(ImGui::GetIO().FontDefault == Panel->QueryTypefaces().QueryHandle(1, FontWeightCategory::SemiBold),
                "Applied body weight did not select the actual font face");
        Require(ImGui::GetStyle().FontSizeBase == 18, "Applied body size differs");
        Require(ImGui::GetStyle().FontScaleMain == 1.25f, "Applied interface scale differs");
        const auto ScaledTile = Panel->QueryAppearance().QueryThemeTileExtent(0);
        Click(PlaneExtent{ScaledTile.MinimumX * 1.25f, ScaledTile.MinimumY * 1.25f, ScaledTile.MaximumX * 1.25f,
                          ScaledTile.MaximumY * 1.25f});
        Require(Panel->QueryAppearance().QueryDraft().Theme == ThemeCategory::Oled,
                "Scaled pointer coordinates did not select the correct theme");
        Panel->ResetPreferences();
        const auto       *Family         = Panel->QueryTypefaces().QueryFamily(1);
        const std::string ExpectedFamily = Family->Name;
        Ownership.reset();
        Require(TypefaceRegistry::QueryCurrent() == nullptr, "Finalization left a dangling global registry");
        Ownership.reset(new ControlCentrePanel(Fonts, PreferencesPath));
        Panel = static_cast<ControlCentrePanel *>(Ownership.get());
        Require(Panel->Initialize() && Panel->Activate(), "Restored initialization failed");
        Require(Panel->QueryAppearance().QueryApplied().Theme == ThemeCategory::Light,
                "Theme was not restored from disk");
        Require(Panel->QueryTypefaces().QueryFamily(Panel->QueryAppearance().QueryApplied().FontFamily)->Name ==
                    ExpectedFamily,
                "Font family was not restored by name");
        Require(Panel->QueryAppearance().QueryApplied().RoleSize[3] == 18, "Body size was not restored");
        auto Invalid         = Panel->QueryAppearance().QueryDraft();
        Invalid.CornerRadius = std::numeric_limits<float>::quiet_NaN();
        Require(!AppearanceCodec::Validate(Invalid), "NaN appearance accepted");
        std::string Diagnostic;
        Require(!AppearanceCodec::Encode(PreferencesPath, Invalid, ExpectedFamily, Diagnostic),
                "Invalid preferences overwrote the saved file");
        AppearanceSettings Restored;
        std::string        FamilyName;
        Require(AppearanceCodec::Decode(PreferencesPath, Restored, FamilyName, Diagnostic),
                "Failed save corrupted previously valid preferences");
        Ownership.reset();
        ImGui::DestroyContext();
        std::cout << "PASS: 4 families / 30 faces, discovery, removal, native font/theme clicks, draft/discard/apply, "
                  << "actual palette/font selection, persistent preferences, invalid settings, lifecycle cleanup\n"
                  << "Images are actual ImGui CPU raster output, not Windows GPU screenshots.\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << '\n';
        if (ImGui::GetCurrentContext())
            ImGui::DestroyContext();
        return 1;
    }
}
