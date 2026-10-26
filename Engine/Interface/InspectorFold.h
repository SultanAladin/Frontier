/*============================================================================================================================================
                                                              INSPECTORFOLD.H
============================================================================================================================================*/
// 🧩 One tiny reusable primitive behind every collapsible inspector card's chevron in this app.
// UI/TextureStudio/MaterialLibraryPanel.cpp's per-layer property cards and UI/TextureStudio/
// BakingWorkspacePanel.cpp's bake-settings cards both toggle their fold state through this exact function, so
// "collapsed" means the same click geometry and the same std::set<std::string> key convention in both
// inspectors instead of two hand-rolled copies that could silently drift apart.

#pragma once
#include <imgui.h>
#include <set>
#include <string>

namespace Frontier
{
// 📝 Draws nothing itself (callers draw their own chevron glyph/background) - just owns the invisible click
// region and the toggle, and reports whether Key is folded *before* this call's own toggle takes effect, so a
// click this frame still renders this frame's body with last frame's state (the same one-frame-latency every
// existing caller of this pattern already relied on).
inline bool ToggleInspectorFold(std::set<std::string> &Folded, const std::string &Key, ImVec2 ClickRegionMinimum,
    ImVec2 ClickRegionSize, const char *ButtonId) noexcept
{
    const bool WasFolded = Folded.contains(Key);
    const ImVec2 SavedCursor = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ClickRegionMinimum);
    if (ImGui::InvisibleButton(ButtonId, ClickRegionSize))
    {
        if (!Folded.erase(Key)) Folded.insert(Key);
    }
    ImGui::SetCursorScreenPos(SavedCursor);
    return WasFolded;
}
} // namespace Frontier
