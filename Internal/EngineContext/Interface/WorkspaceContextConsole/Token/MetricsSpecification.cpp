/*==============================================================================================================================================
                                                         METRICSSPECIFICATION.CPP
==============================================================================================================================================*/
// 🧩 Resolves the console's metrics from the theme, scaling every length by UiScale. A workspace that disagrees on a field overrides it on the
//    resolved struct AFTER this call. Ported from ToolCardSpecification's ResolveToolCardMetrics.

#include "MetricsSpecification.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

MetricsSpecification ResolveConsoleMetrics(const ThemeConfiguration& Theme)
{
    MetricsSpecification Resolved;   // defaults are the modelling prototype's CSS numbers

    // 📝 One knob scales the whole card. The column widths and the card width are scaled together and the width is then RE-DERIVED from them
    //    rather than scaled independently: scaling three related numbers separately lets rounding put the card a pixel wide of its own columns,
    //    which is exactly the seam-stepping the pinned columns exist to prevent. Durations are never scaled.
    const float Scale = (Theme.Metrics.UiScale > 0.0f) ? Theme.Metrics.UiScale : 1.0f;
    if (Scale != 1.0f)
    {
        Resolved.LeftColumnWidth   *= Scale;
        Resolved.RightColumnWidth  *= Scale;
        Resolved.CardHeight        *= Scale;
        Resolved.HeaderHeight      *= Scale;
        Resolved.GridFootHeight    *= Scale;
        Resolved.OptionsFootHeight *= Scale;
        Resolved.RailRowHeight     *= Scale;
        Resolved.CardRounding      *= Scale;
        Resolved.TileRounding      *= Scale;
        Resolved.TileGap           *= Scale;
        Resolved.TileArtEdge       *= Scale;

        Resolved.ParameterPadding  *= Scale;
        Resolved.ParameterRowGap   *= Scale;
        Resolved.ParameterLabelGap *= Scale;
        Resolved.ValuePillHeight   *= Scale;
        Resolved.ValuePillWidth    *= Scale;
        Resolved.SliderTrackHeight *= Scale;
        Resolved.SliderKnobEdge    *= Scale;
        Resolved.SegmentHeight     *= Scale;
        Resolved.SegmentGap        *= Scale;
        Resolved.SwitchWidth       *= Scale;
        Resolved.SwitchHeight      *= Scale;
        Resolved.SwitchNubEdge     *= Scale;
        Resolved.DropdownHeight    *= Scale;

        Resolved.CardWidth          = Resolved.LeftColumnWidth + Resolved.RightColumnWidth + 1.0f;
    }

    return Resolved;
}

} // namespace Frontier
