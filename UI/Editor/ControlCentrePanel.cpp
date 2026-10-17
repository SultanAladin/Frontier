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
        if (ImGui::GetCurrentContext() == nullptr || Typefaces.Load(FontDirectory) == 0)
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
    Notch.Integrate(Δτ);
    Appearance.IntegrateFontStrip(static_cast<float>(std::min(Δτ, 0.1)));
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
    auto       &IO      = ImGui::GetIO();
    const auto  Display = IO.DisplaySize;
    const float Scale   = Appearance.QueryApplied().InterfaceScale / 100.0f;
    Notch.ConfigureExtent(Display.x, Display.y, Scale);
    const auto Origin = ImGui::GetMainViewport()->Pos;
    ImGui::SetNextWindowPos(Origin);
    ImGui::SetNextWindowSize(Display);
    const bool Visible = Notch.QueryHeight() > 0.5f || Notch.QueryOpen();
    auto       Options = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse;
    if (!Visible)
        Options |= ImGuiWindowFlags_NoInputs;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Frontier notch overlay", nullptr, Options);
    auto       *Commands = ImGui::GetWindowDrawList();
    const auto &Palette  = ControlKit::Palette();
    const auto  Colour   = [](ColorQuad Tone)
    { return ImGui::ColorConvertFloat4ToU32(ImVec4(Tone.Red, Tone.Green, Tone.Blue, Tone.Alpha)); };
    const auto At     = [&](float X, float Y) { return ImVec2(Origin.x + X, Origin.y + Y); };
    const auto ShadeY = Notch.QueryHeight();
    if (Visible)
    {
        Commands->AddRectFilled(Origin, At(Display.x, Display.y),
                                IM_COL32(0, 0, 0, static_cast<int>(102 * Notch.QueryProgress())));
        Commands->AddRectFilled(Origin, At(Display.x, ShadeY), Colour(Palette.Field));
    }
    const auto Contour = Notch.QueryContour();
    ImVec2     Vertices[4];
    for (size_t Index = 0; Index < Contour.size(); ++Index)
        Vertices[Index] = At(Contour[Index].X, Contour[Index].Y);
    Commands->AddConvexPolyFilled(Vertices, 4, Colour(Palette.Raised));
    const auto NotchBounds   = Notch.QueryNotchExtent();
    const auto CaptionExtent = ImGui::CalcTextSize("Frontier");
    Commands->AddText(At(NotchBounds.MinimumX + (NotchBounds.Width() - CaptionExtent.x) * 0.5f,
                         ShadeY + (NotchBounds.Height() - CaptionExtent.y) * 0.5f - 2 * Scale),
                      Colour(Palette.TextDim), "Frontier");
    const auto Grip = Notch.QueryGripExtent();
    Commands->AddRectFilled(At(Grip.MinimumX, Grip.MinimumY), At(Grip.MaximumX, Grip.MaximumY),
                            Colour(Palette.TextDim));
    bool CardContact = false;
    if (Visible)
    {
        const float Width =
            CentreOpen ? std::max(200.0f, Display.x - 80 * Scale) : std::min(420 * Scale, Display.x - 32);
        const float Height =
            CentreOpen ? std::max(160.0f, Display.y - 100 * Scale) : std::min(480 * Scale, Display.y - 80);
        const auto Card =
            Spanning((Display.x - Width) * 0.5f, (Display.y - Height) * 0.5f + ShadeY - Display.y, Width, Height);
        Commands->AddRectFilled(At(Card.MinimumX, Card.MinimumY), At(Card.MaximumX, Card.MaximumY),
                                Colour(Palette.Raised), 24 * Scale);
        ImGui::SetCursorScreenPos(At(Card.MinimumX + 16 * Scale, Card.MinimumY + 12 * Scale));
        ImGui::BeginChild("Notch card", ImVec2(Width - 32 * Scale, Height - 24 * Scale), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground);
        ImGui::BeginDisabled(!Notch.QueryOpen() || Notch.QueryDragging());
        if (CentreOpen)
            RecordAppearance();
        else
        {
            ImGui::TextUnformatted("Control Centre");
            ImGui::Dummy(ImVec2(0, 24 * Scale));
            if (ImGui::Button("Fonts", ImVec2((Width - 48 * Scale) * 0.5f, 100 * Scale)))
                ActivatePage(AppearancePage::Fonts);
            ImGui::SameLine();
            if (ImGui::Button("Theme", ImVec2((Width - 48 * Scale) * 0.5f, 100 * Scale)))
                ActivatePage(AppearancePage::Theme);
            ImGui::Dummy(ImVec2(0, 24 * Scale));
            ImGui::Checkbox("Show tooltips", &TooltipsEnabled);
            if (TooltipsEnabled && ImGui::IsItemHovered())
                ImGui::SetTooltip("Interface help tooltips only.");
            auto Preferences = Appearance.QueryDraft();
            if (ImGui::SliderFloat("UI scale", &Preferences.InterfaceScale, 75, 150, "%.0f%%"))
                Appearance.ConfigureDraft(Preferences);
            if (ImGui::Button("Apply preferences"))
                (void)FinalizePreferences();
            ImGui::SameLine();
            if (ImGui::Button("Discard"))
                ResetPreferences();
        }
        ImGui::EndDisabled();
        const auto  Mouse   = IO.MousePos;
        const float CursorX = Mouse.x - Origin.x;
        const float CursorY = Mouse.y - Origin.y;
        // Header and blank quick-card space carry the sheet. Typography sliders and scroll content keep ownership.
        CardContact = Card.Encloses(CursorX, CursorY) && (!CentreOpen || CursorY < Card.MinimumY + 42 * Scale) &&
                      !ImGui::IsAnyItemActive() && !ImGui::IsAnyItemHovered();
        ImGui::EndChild();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !Card.Encloses(CursorX, CursorY) &&
            !Grip.Encloses(CursorX, CursorY) && !NotchBounds.Encloses(CursorX, CursorY))
            Notch.ConfigureOpen(false);
    }
    Notch.IntegratePointer(IO.MousePos.x - Origin.x, IO.MousePos.y - Origin.y,
                           ImGui::IsMouseClicked(ImGuiMouseButton_Left), ImGui::IsMouseDown(ImGuiMouseButton_Left),
                           ImGui::IsMouseReleased(ImGuiMouseButton_Left), IO.DeltaTime, CardContact);
    if (Notch.QueryDragging() || NotchBounds.Encloses(IO.MousePos.x - Origin.x, IO.MousePos.y - Origin.y))
        ImGui::SetNextFrameWantCaptureMouse(true);
    ImGui::End();
    ImGui::PopStyleVar();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     APPEARANCE PAGE
//------------------------------------------------------------------------------------------------------------------------

void ControlCentrePanel::RecordAppearance() noexcept
{
    if (ImGui::Button("< Quick panel"))
        CentreOpen = false;
    ImGui::SameLine();
    if (ImGui::Button("Fonts"))
        SelectedPage = AppearancePage::Fonts;
    ImGui::SameLine();
    if (ImGui::Button("Theme"))
        SelectedPage = AppearancePage::Theme;
    ImGui::SameLine();
    ImGui::Text("%u installed font archives", Typefaces.QueryFamilyCount());
    if (ImGui::BeginChild("Appearance content", ImVec2(0, -64), ImGuiChildFlags_None))
    {
        const float Scale  = Appearance.QueryApplied().InterfaceScale / 100.0f;
        const auto  Origin = ImGui::GetCursorScreenPos();
        const float Width  = std::max(200.0f, (ImGui::GetContentRegionAvail().x - 12) / Scale);
        const auto  Body   = Spanning(Origin.x / Scale, Origin.y / Scale, Width, 10000);
        PixelSpace  Surface;
        const auto  Display = ImGui::GetIO().DisplaySize;
        if (Surface.Begin(SurfaceLayer::Window, Display.x, Display.y, Scale))
        {
            ControlPointer Pointer;
            Pointer.X        = ImGui::GetIO().MousePos.x / Scale;
            Pointer.Y        = ImGui::GetIO().MousePos.y / Scale;
            Pointer.Pressed  = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            Pointer.Released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
            Pointer.Down     = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            Pointer.Enabled  = Notch.QueryOpen() && !Notch.QueryDragging() &&
                              ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            const float ContentHeight = SelectedPage == AppearancePage::Fonts
                                            ? Appearance.ConstructFontsTabLayout(Surface, Body, 0, Pointer, 1)
                                            : Appearance.ConstructThemeTabLayout(Surface, Body, 0, Pointer, 1);
            ImGui::Dummy(ImVec2(Width * Scale, ContentHeight * Scale));
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

void ControlCentrePanel::RecordInterface() noexcept
{
    if (QueryPhase() != ActivationPhase::Active)
        return;
    if ((ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Comma, false)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        Notch.ConfigureOpen(!Notch.QueryOpen() && !ImGui::IsKeyPressed(ImGuiKey_Escape, false));
    RecordQuickPanel();
}
} // namespace Frontier
