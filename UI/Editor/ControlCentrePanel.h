/*==========================================================================================================================================
                                                            CONTROLCENTREPANEL.H
==========================================================================================================================================*/
// 🧩 Lifecycle-owned quick panel and native theme and typography controls.

#pragma once

#include "Engine/Lifecycle/ActivationSequence.h"
#include "Engine/Interface/AppearanceCodec.h"
#include "Engine/Interface/TypefaceRegistry.h"
#include <filesystem>
#include <string>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                    CONTROL CENTRE
//------------------------------------------------------------------------------------------------------------------------

enum class AppearancePage
{
    Fonts,
    Theme
};

class ControlCentrePanel final : public ActivationSequence
{
  public:
    explicit ControlCentrePanel(std::filesystem::path FontDirectory   = "UI/Assets/Fonts",
                                std::filesystem::path PreferencesPath = {});
    void PrepareInterface() noexcept;
    void RecordInterface() noexcept;
    void ActivatePage(AppearancePage Page) noexcept
    {
        SelectedPage = Page;
        CentreOpen   = true;
    }
    [[nodiscard]] bool                    FinalizePreferences() noexcept;
    void                                  ResetPreferences() noexcept { Appearance.Discard(); }
    [[nodiscard]] AppearanceInspector    &QueryAppearance() noexcept { return Appearance; }
    [[nodiscard]] const TypefaceRegistry &QueryTypefaces() const noexcept { return Typefaces; }
    [[nodiscard]] const std::string      &QueryDiagnostic() const noexcept { return Diagnostic; }
    [[nodiscard]] bool                    QueryCentreOpen() const noexcept { return CentreOpen; }
    [[nodiscard]] bool                    QueryQuickOpen() const noexcept { return QuickOpen; }

  protected:
    bool InitializeContent() noexcept override;
    void IntegrateContent(double Δτ) noexcept override;
    void FinalizeContent() noexcept override;

  private:
    void RecordQuickPanel() noexcept;
    void RecordAppearance() noexcept;

    std::filesystem::path FontDirectory;
    std::filesystem::path PreferencesPath;
    TypefaceRegistry      Typefaces;
    AppearanceInspector   Appearance;
    ThemeStructure        Theme;
    std::string           Diagnostic;
    AppearancePage        SelectedPage    = AppearancePage::Fonts;
    uint32_t              AppliedRevision = ~0u;
    bool                  CentreOpen      = true;
    bool                  QuickOpen       = true;
    bool                  TooltipsEnabled = true;
    float                 Expansion       = 0.0f;
};
} // namespace Frontier
