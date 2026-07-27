/*==============================================================================================================================================
                                                              PAINTPROPERTYPANEL.CPP
==============================================================================================================================================*/
// 🧩 Composes the paint property column from shared cards + ValueSlider / ColorEntry / SelectionEntry controls over placeholder brush state.

#include "PaintPropertyPanel.h"

#include "../../Components/PropertyPanelBase.h"

#include "../../Components/Controls/ValueSlider.h"

#include "../../Components/Controls/ColorEntry.h"

#include "../../Components/Controls/SelectionEntry.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ConstructPaintPropertyPanel(const ThemeConfiguration& Theme, TexturePaintWorkspaceState& State)
{
    BeginPropertyPanel(Theme, "##paint-properties");

    // -- Brush card ----------------------------------------------------------------------------------------------------
    if (BeginPropertyCard(Theme, "Brush", &State.BrushExpanded))
    {
        ValueSliderDescriptor Radius = {};
        Radius.Label = "Radius"; Radius.Value = &State.BrushRadius;
        Radius.Minimum = 1.0f; Radius.Maximum = 256.0f; Radius.Format = "%.0f"; Radius.Unit = "px"; Radius.Enabled = true;
        ConstructValueSlider(Theme, Radius);

        ValueSliderDescriptor Flow = {};
        Flow.Label = "Flow"; Flow.Value = &State.BrushFlow;
        Flow.Minimum = 0.0f; Flow.Maximum = 1.0f; Flow.Format = "%.2f"; Flow.Enabled = true;
        ConstructValueSlider(Theme, Flow);

        ColorEntryDescriptor Color = {};
        Color.Label = "Color"; Color.Channels = State.BrushColor; Color.IncludeAlpha = true; Color.Enabled = true;
        ConstructColorEntry(Theme, Color);
        EndPropertyCard(Theme);
    }

    // -- Channel card --------------------------------------------------------------------------------------------------
    if (BeginPropertyCard(Theme, "Channel", &State.ChannelExpanded))
    {
        static const char* const ChannelOptions[] = { "Base Color", "Roughness", "Metallic", "Normal", "Height" };
        SelectionEntryDescriptor Channel = {};
        Channel.Label = "Target"; Channel.SelectedIndex = &State.ActiveChannelIndex;
        Channel.Options = ChannelOptions; Channel.OptionCount = 5; Channel.Enabled = true;
        ConstructSelectionEntry(Theme, Channel);
        EndPropertyCard(Theme);
    }

    EndPropertyPanel(Theme);
}

}   // namespace Frontier
