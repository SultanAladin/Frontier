/*==============================================================================================================================================
                                                              CONTENTSECTION.CPP
==============================================================================================================================================*/
// 🧩 Opens/closes the padded, indented body under a section header. Stateless; a fixed height turns the body into a scrollable child.

#include "ContentSection.h"

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Track whether the currently open section is a scrollable child so End* knows whether to close one.
    //    A single flag suffices because sections do not nest (a card body never contains another card body).
    bool ActiveSectionIsChild = false;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void BeginContentSection(const ThemeConfiguration& Theme, const ContentSectionDescriptor& Descriptor)
{
    const char* Identifier = Descriptor.Identifier ? Descriptor.Identifier : "##section";

    ImGui::Indent(Theme.Metrics.PanelPadding);
    ActiveSectionIsChild = (Descriptor.FixedHeight > 0.0f);

    if (ActiveSectionIsChild)
    {
        const float Width = ImGui::GetContentRegionAvail().x - Theme.Metrics.PanelPadding;
        ImGui::BeginChild(Identifier, ImVec2(Width, Descriptor.FixedHeight), false, ImGuiWindowFlags_None);
    }

    // 📝 A little breathing room above the first control.
    ImGui::Dummy(ImVec2(0.0f, Theme.Metrics.ControlSpacing * 0.5f));
}


void EndContentSection(const ThemeConfiguration& Theme)
{
    ImGui::Dummy(ImVec2(0.0f, Theme.Metrics.ControlSpacing * 0.5f));

    if (ActiveSectionIsChild)
    {
        ImGui::EndChild();
        ActiveSectionIsChild = false;
    }

    ImGui::Unindent(Theme.Metrics.PanelPadding);
}

}   // namespace Frontier
