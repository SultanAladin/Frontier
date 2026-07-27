/*==============================================================================================================================================
                                                        ENCLOSUREALIGNMENTCONFIGURATION.CPP
==============================================================================================================================================*/
// 🧩 Seeds a card's first-frame position + size via ImGui FirstUseEver, then defers to the user's layout forever after

#include "EnclosureAlignmentConfiguration.h"

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Seed the next window's position and size only on its very first appearance. FirstUseEver means a persisted .ini rect or a
// later user drag always overrides this — the configuration never re-asserts itself.
void ApplyEnclosureAlignment(const EnclosureAlignmentConfiguration& Profile)
{
    if (!Profile.AlignmentActive)
    {
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(Profile.OriginX, Profile.OriginY), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(Profile.ExtentWidth, Profile.ExtentHeight), ImGuiCond_FirstUseEver);
}

}   // namespace Frontier
