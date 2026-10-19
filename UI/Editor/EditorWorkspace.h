/*==========================================================================================================================================
                                                        EDITORWORKSPACE.H
==========================================================================================================================================*/
// 🧩 Adapt existing editor-owned content to one shared docked/detached host without constructing duplicate panel instances.

#pragma once
#include "ControlCentrePanel.h"
#include "Engine/Interface/PanelDockspace.h"

namespace Frontier
{
class EditorWorkspace
{
  public:
    explicit EditorWorkspace(ControlCentrePanel &ControlCentre);
    ~EditorWorkspace();
    EditorWorkspace(const EditorWorkspace &)              = delete;
    EditorWorkspace   &operator=(const EditorWorkspace &) = delete;
    bool               FinalizeLayout();
    void               IntegrateLayoutPersistence(double ElapsedSeconds);
    [[nodiscard]] bool QueryLayoutRestored() const noexcept
    {
        return LayoutRestored;
    }
    [[nodiscard]] const std::string &QueryLayoutDiagnostic() const noexcept
    {
        return LayoutDiagnostic;
    }
    bool                          ConfigurePlatform() noexcept;
    void                          ConstructInterface();
    [[nodiscard]] PanelDockspace &QueryPanels() noexcept
    {
        return Panels;
    }

  private:
    ControlCentrePanel &Centre;
    PanelDockspace      Panels;
    bool                DockableBrowser   = false;
    bool                LayoutRestored    = false;
    bool                LayoutBlocked     = false;
    bool                Rendered          = false;
    uint64_t            ResetRevision     = 0;
    unsigned            PersistenceFrames = 0;
    double              StableSeconds     = 0;
    double              RetrySeconds      = 0;
    toml::table         ObservedSnapshot;
    toml::table         CaptureLayout() const;
    std::string         SavedLayout;
    std::string         ObservedLayout;
    std::string         LayoutDiagnostic;
    void                ConfigureLayoutDiagnostic(std::string Message);
};
} // namespace Frontier
