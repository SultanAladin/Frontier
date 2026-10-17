/*==========================================================================================================================================
                                                        FOUNDATIONPANEL.CPP
==========================================================================================================================================*/
// 🧩 Record interactive controls and SVG atlas images through existing ImGui drawing code.

#include "FoundationPanel.h"
#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/ControlKit.h"
#include <imgui.h>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     INTERFACE RECORDING
//------------------------------------------------------------------------------------------------------------------------

void FoundationPanel::RecordInterface() noexcept
{
    if (QueryPhase() != ActivationPhase::Active) return;

    ImGui::SetNextWindowSize(ImVec2(700.0f, 480.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Frontier | UI foundations"))
    {
        ImGui::TextUnformatted("UI-only GPU path | scene renderer intentionally excluded");
        ImGui::Checkbox("Controls enabled", &ControlsEnabled);
        ImGui::SliderFloat("Interface scale", &InterfaceScale, 1.0f, 3.0f);
        if (ImGui::BeginTabBar("FoundationTabs"))
        {
            if (ImGui::BeginTabItem("SVG artwork"))
            {
                ImGui::TextUnformatted("Original SVG -> ThorVG RGBA -> ImGui texture -> existing GPU backend");
                const float IconSize = 32.0f * InterfaceScale;
                for (int SymbolIndex = 0; SymbolIndex < static_cast<int>(IconSymbol::Count); ++SymbolIndex)
                {
                    const auto Symbol = static_cast<IconSymbol>(SymbolIndex);
                    const ImVec2 Origin = ImGui::GetCursorScreenPos();
                    if (!IconPresentation::Draw(ImGui::GetWindowDrawList(), Symbol, Origin, IconSize))
                        ImGui::TextUnformatted("Atlas unavailable");
                    ImGui::Dummy(ImVec2(IconSize, IconSize));
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s: %s", IconArt::Name(Symbol), IconPresentation::Diagnostic(Symbol));
                    if (SymbolIndex % 10 != 9) ImGui::SameLine();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Reusable controls"))
            {
                ImGui::BeginDisabled(!ControlsEnabled);
                ImGui::Button("Reusable UI lifecycle active");
                ImGui::EndDisabled();
                PixelSpace Surface;
                const auto Display = ImGui::GetIO().DisplaySize;
                if (Surface.Begin(SurfaceLayer::Window, Display.x, Display.y))
                {
                    const auto Origin = ImGui::GetCursorScreenPos();
                    ControlKit::ValuePill(Surface, Origin.x, Origin.y, "24", "px");
                    ImGui::Dummy(ImVec2(160.0f, 40.0f));
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}
}
