/*============================================================================================================================================
                                                            UVOUTLINERPANEL.H
============================================================================================================================================*/
// 🧩 Frontier-styled UV Space Outliner pane with Search, UDIM Filter, Inline Layer Rename (TextInputField), Dup/Delete & Selection Modes

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Workspaces/UV/UVWorkspaceSession.h"

namespace Frontier::Workspaces
{

class UVOutlinerPanel
{
public:
    UVOutlinerPanel() noexcept = default;

    void AttachSession(UVWorkspaceSession* TargetSession) noexcept;

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  Δt);

    void PresentInImGuiPane(double Δt, float ScaleFactor = 1.0f);

private:
    UVWorkspaceSession* Session            = nullptr;
    uint32_t            CreatedLayerCount  = 3u;
    TextEditBuffer      SearchEditBuffer   {};
    TextEditBuffer      RenameEditBuffer   {};
    uint16_t            RenamingLayerOrd   = 0u;
};

} // namespace Frontier::Workspaces
