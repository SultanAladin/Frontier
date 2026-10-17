/*==========================================================================================================================================
                                                           CONTROLCENTREPANEL.CPP
==========================================================================================================================================*/
// 🧩 Lifecycle-owned quick panel and native theme and typography controls.

#include "ControlCentrePanel.h"
#include "Engine/Interface/IconPresentation.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <utility>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                      LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

ControlCentrePanel::ControlCentrePanel(std::filesystem::path Directory, std::filesystem::path Preferences)
    : FontDirectory(std::move(Directory)), PreferencesPath(std::move(Preferences))
{
}

bool ControlCentrePanel::InitializeContent() noexcept
{
    try
    {
        if (ImGui::GetCurrentContext() == nullptr || Typefaces.Load(FontDirectory.string()) == 0)
        {
            Diagnostic = "No usable fonts. Run python Automation/PrepareFonts.py before launching.";
            return false;
        }
        TypefaceRegistry::Install(&Typefaces);
        AppearanceSettings Preferences;
        if (!PreferencesPath.empty() && std::filesystem::exists(PreferencesPath))
        {
            std::string FamilyName;
            if (AppearanceCodec::Decode(PreferencesPath, Preferences, FamilyName, Diagnostic))
            {
                const auto FamilyIndex = Typefaces.ResolveFamilyIndex(FamilyName);
                Preferences.FontFamily = FamilyIndex >= 0 ? static_cast<uint32_t>(FamilyIndex) : 0;
                if (FamilyIndex < 0)
                    Diagnostic = "Saved font unavailable; using the first installed family.";
            }
        }
        Appearance.Seed(Preferences);
        return true;
    }
    catch (const std::exception &Failure)
    {
        Diagnostic = Failure.what();
        return false;
    }
}

void ControlCentrePanel::IntegrateContent(double Δτ) noexcept
{
    const float Duration = static_cast<float>(std::min(Δτ, 0.1));
    const float Target   = QuickOpen ? 1.0f : 0.0f;
    Expansion += (Target - Expansion) * (1.0f - std::exp(-Duration * 18.0f));
    Appearance.IntegrateFontStrip(Duration);
}

void ControlCentrePanel::FinalizeContent() noexcept
{
    if (TypefaceRegistry::QueryCurrent() == &Typefaces)
        TypefaceRegistry::Install(nullptr);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                APPLIED PREFERENCES
//------------------------------------------------------------------------------------------------------------------------

void ControlCentrePanel::PrepareInterface() noexcept
{
    if (QueryPhase() != ActivationPhase::Active || AppliedRevision == Appearance.QueryRevision())
        return;
    const auto &Preferences = Appearance.QueryApplied();
    Theme.ConfigureTheme(Preferences.Theme);
    Theme.ConfigureAccent(Preferences.Accent);
    Theme.ConfigureCornerRadius(Preferences.CornerRadius);
    ControlKit::ConfigureTheme(Theme, AppearanceInspector::QuerySemanticColour(0, Preferences.WarningSwatch),
                               AppearanceInspector::QuerySemanticColour(1, Preferences.SuccessSwatch),
                               AppearanceInspector::QuerySemanticColour(2, Preferences.InfoSwatch),
                               AppearanceInspector::QuerySemanticColour(3, Preferences.CautionSwatch));
    if (ControlKit::Palette().LightSurface)
        ImGui::StyleColorsLight();
    else
        ImGui::StyleColorsDark();
    auto       &Style   = ImGui::GetStyle();
    const auto &Palette = Theme.QueryPalette();
    const auto  Colour  = [](ColorQuad Tone) { return ImVec4(Tone.Red, Tone.Green, Tone.Blue, Tone.Alpha); };
    Style.Colors[ImGuiCol_WindowBg]      = Colour(Palette.MainBackground);
    Style.Colors[ImGuiCol_ChildBg]       = Colour(Palette.MainBackground);
    Style.Colors[ImGuiCol_Text]          = Colour(Palette.TextMain);
    Style.Colors[ImGuiCol_TextDisabled]  = Colour(Palette.TextMuted);
    Style.Colors[ImGuiCol_Button]        = Colour(Palette.ActiveBackground);
    Style.Colors[ImGuiCol_ButtonHovered] = Colour(Palette.CardSubBackground);
    Style.Colors[ImGuiCol_ButtonActive]  = Colour(Theme.QueryAccentColor());
    Style.Colors[ImGuiCol_CheckMark]     = Colour(Theme.QueryAccentColor());
    Style.Colors[ImGuiCol_SliderGrab]    = Colour(Theme.QueryAccentColor());
    Style.WindowRounding                 = Preferences.CornerRadius;
    Style.ChildRounding                  = Preferences.CornerRadius;
    Style.FrameRounding                  = Preferences.CornerRadius * 0.5f;
    Style.FontSizeBase                   = Preferences.RoleSize[3];
    Style.FontScaleMain                  = Preferences.InterfaceScale / 100.0f;
    ImGui::GetIO().FontDefault           = static_cast<ImFont *>(Appearance.QueryAppliedFace(3));
    AppliedRevision                      = Appearance.QueryRevision();
}

bool ControlCentrePanel::FinalizePreferences() noexcept
{
    const auto &Preferences = Appearance.QueryDraft();
    const auto *Family      = Typefaces.QueryFamily(Preferences.FontFamily);
    if (!Family || !AppearanceCodec::Validate(Preferences))
    {
        Diagnostic = "Invalid appearance preferences.";
        return false;
    }
    if (!PreferencesPath.empty() && !AppearanceCodec::Encode(PreferencesPath, Preferences, Family->Name, Diagnostic))
        return false;
    Appearance.Apply();
    Diagnostic = PreferencesPath.empty() ? "Applied for this session." : "Preferences saved.";
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     QUICK PANEL
//------------------------------------------------------------------------------------------------------------------------

void ControlCentrePanel::RecordQuickPanel() noexcept
{
    const auto Display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(Display.x * 0.5f, 14.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(std::min(420.0f, Display.x - 24.0f), 0.0f));
    const auto Options = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("Quick panel", nullptr, Options))
    {
        if (ImGui::Button(QuickOpen ? "Control Centre  -" : "Control Centre  +", ImVec2(-1, 32)))
            QuickOpen = !QuickOpen;
        if (QuickOpen || Expansion > 0.05f)
        {
            ImGui::BeginDisabled(!QuickOpen);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * Expansion);
            const auto Icon = [](IconSymbol Symbol)
            {
                const auto Origin = ImGui::GetCursorScreenPos();
                (void)IconPresentation::Draw(ImGui::GetWindowDrawList(), Symbol, Origin, 24.0f);
                ImGui::Dummy(ImVec2(24.0f, 24.0f));
                ImGui::SameLine();
            };
            Icon(IconSymbol::AssetFolderFonts);
            if (ImGui::Button("Fonts", ImVec2(140, 32)))
                ActivatePage(AppearancePage::Fonts);
            ImGui::SameLine();
            if (ImGui::Button("Theme", ImVec2(140, 32)))
                ActivatePage(AppearancePage::Theme);
            ImGui::Checkbox("Show tooltips", &TooltipsEnabled);
            if (TooltipsEnabled && ImGui::IsItemHovered())
                ImGui::SetTooltip("Interface help tooltips only.");
            auto Preferences = Appearance.QueryDraft();
            if (ImGui::SliderFloat("UI scale", &Preferences.InterfaceScale, 75.0f, 150.0f, "%.0f%%"))
                Appearance.ConfigureDraft(Preferences);
            if (ImGui::Button("Apply preferences"))
                (void)FinalizePreferences();
            ImGui::SameLine();
            if (ImGui::Button("Discard"))
                ResetPreferences();
            ImGui::PopStyleVar();
            ImGui::EndDisabled();
        }
    }
    ImGui::End();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     APPEARANCE PAGE
//------------------------------------------------------------------------------------------------------------------------

void ControlCentrePanel::RecordAppearance() noexcept
{
    if (!CentreOpen)
        return;
    const auto DisplayExtent = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(30, 220), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(
        ImVec2(std::min(960.0f, DisplayExtent.x - 40.0f), std::min(640.0f, DisplayExtent.y - 240.0f)),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(760, 320), ImVec2(1600, 1600));
    if (ImGui::Begin("Control Centre", &CentreOpen))
    {
        if (ImGui::Button("Fonts"))
            ActivatePage(AppearancePage::Fonts);
        ImGui::SameLine();
        if (ImGui::Button("Theme"))
            ActivatePage(AppearancePage::Theme);
        ImGui::SameLine();
        ImGui::Text("%u installed font families", Typefaces.QueryFamilyCount());
        const float FooterHeight = 76.0f;
        if (ImGui::BeginChild("Appearance content", ImVec2(0, -FooterHeight), ImGuiChildFlags_None,
                              ImGuiWindowFlags_HorizontalScrollbar))
        {
            const float       InterfaceScale = Appearance.QueryApplied().InterfaceScale / 100.0f;
            const auto        Origin         = ImGui::GetCursorScreenPos();
            const float       Width = std::max(780.0f, (ImGui::GetContentRegionAvail().x - 12.0f) / InterfaceScale);
            const PlaneExtent Body  = Spanning(Origin.x / InterfaceScale, Origin.y / InterfaceScale, Width, 10000);
            PixelSpace        Surface;
            const auto        Display = ImGui::GetIO().DisplaySize;
            if (Surface.Begin(SurfaceLayer::Window, Display.x, Display.y, InterfaceScale))
            {
                ControlPointer Pointer;
                Pointer.X        = ImGui::GetIO().MousePos.x / InterfaceScale;
                Pointer.Y        = ImGui::GetIO().MousePos.y / InterfaceScale;
                Pointer.Pressed  = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                Pointer.Released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
                Pointer.Down     = ImGui::IsMouseDown(ImGuiMouseButton_Left);
                Pointer.Enabled  = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
                // Native scrolling owns the offset; Origin already includes that offset.
                const float ContentHeight = SelectedPage == AppearancePage::Fonts
                                                ? Appearance.ConstructFontsTabLayout(Surface, Body, 0, Pointer, 1)
                                                : Appearance.ConstructThemeTabLayout(Surface, Body, 0, Pointer, 1);
                ImGui::Dummy(ImVec2(Width * InterfaceScale, ContentHeight * InterfaceScale));
            }
        }
        ImGui::EndChild();
        ImGui::BeginDisabled(!Appearance.QueryDirty());
        if (ImGui::Button("Apply"))
            (void)FinalizePreferences();
        ImGui::SameLine();
        if (ImGui::Button("Discard changes"))
            ResetPreferences();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("%u unsaved changes", Appearance.QueryDifference().Count);
        ImGui::TextWrapped("%s", Diagnostic.c_str());
    }
    ImGui::End();
}

void ControlCentrePanel::RecordInterface() noexcept
{
    if (QueryPhase() != ActivationPhase::Active)
        return;
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Comma))
        CentreOpen = !CentreOpen;
    RecordAppearance();
    RecordQuickPanel();
}
} // namespace Frontier
