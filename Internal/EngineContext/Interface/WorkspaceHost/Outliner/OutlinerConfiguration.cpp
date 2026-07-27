/*==============================================================================================================================================
                                                              OUTLINERCONFIGURATION.CPP
==============================================================================================================================================*/
// 🧩 Default outliner parameterization, seeded from the theme so metrics stay consistent with every other panel.

#include "OutlinerConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

OutlinerConfiguration ResolveDefaultOutlinerConfiguration(const ThemeConfiguration& Theme, const char* HeaderCaption)
{
    OutlinerConfiguration Configuration = {};
    Configuration.HeaderCaption    = HeaderCaption;
    Configuration.RowHeight        = Theme.Metrics.RowHeight;
    Configuration.IndentWidth      = Theme.Metrics.IndentWidth;
    Configuration.VisibilityColumn = true;
    Configuration.TintChips        = true;
    return Configuration;
}

}   // namespace Frontier
