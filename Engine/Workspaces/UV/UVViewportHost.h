/*============================================================================================================================================
                                                            UVVIEWPORTHOST.H
============================================================================================================================================*/
// 🧩 Central UV Workspace viewport pane combining UVOperationToolbar, 2D UDIM UVTileCanvas, and 3D ProjectionViewport

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Workspaces/UV/UVWorkspaceSession.h"

#include <imgui.h>

namespace Frontier::Workspaces
{

class UVViewportHost
{
public:
    UVViewportHost();
    ~UVViewportHost();

    UVViewportHost(const UVViewportHost&)            = delete;
    UVViewportHost& operator=(const UVViewportHost&) = delete;

    void AttachSession(UVWorkspaceSession* TargetSession) noexcept;

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  Δt);

    void PresentInImGuiPane(double Δt, float ScaleFactor = 1.0f);

private:
    void SyncImGuiTexture(ImTextureData& TexData, const std::vector<uint8_t>& RgbaBytes, uint32_t WidthPx, uint32_t HeightPx, uint64_t TexIdTag);

    UVWorkspaceSession* Session     = nullptr;
    ImTextureData       Texture3D   {};
    ImTextureData       Texture2D   {};
};

} // namespace Frontier::Workspaces
