/*==========================================================================================================================================
                                                            CONTROLCENTREPANEL.H
==========================================================================================================================================*/
// 🧩 Lifecycle-owned quick panel and native theme and typography controls.

#pragma once

#include "Engine/Lifecycle/ActivationSequence.h"
#include "Engine/Interface/AppearanceCodec.h"
#include "Engine/Interface/TypefaceRegistry.h"
#include "Engine/Interface/NotchMotion.h"
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
    explicit ControlCentrePanel(std::filesystem::path FontDirectory   = "EngineContent/FontArchives",
                                std::filesystem::path PreferencesPath = {});
    void PrepareInterface() noexcept;
    void RecordInterface() noexcept;
    void ActivatePage(AppearancePage Page) noexcept
    {
        SelectedPage = Page;
        CentreOpen   = true;
        Notch.ConfigureOpen(true);
    }
    [[nodiscard]] bool                    FinalizePreferences() noexcept;
    void                                  ResetPreferences() noexcept { Appearance.Discard(); }
    [[nodiscard]] AppearanceInspector    &QueryAppearance() noexcept { return Appearance; }
    [[nodiscard]] const TypefaceRegistry &QueryTypefaces() const noexcept { return Typefaces; }
    [[nodiscard]] const std::string      &QueryDiagnostic() const noexcept { return Diagnostic; }
    [[nodiscard]] bool                    QueryCentreOpen() const noexcept { return CentreOpen && Notch.QueryOpen(); }
    [[nodiscard]] bool                    QueryQuickOpen() const noexcept { return Notch.QueryOpen(); }

    [[nodiscard]] const NotchMotion &QueryNotch() const noexcept { return Notch; }

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
    bool                  CentreOpen      = false;
    NotchMotion           Notch;
    bool                  TooltipsEnabled = true;
};
} // namespace Frontier
