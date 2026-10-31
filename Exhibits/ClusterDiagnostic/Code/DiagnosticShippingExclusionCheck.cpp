/*==========================================================================================================================================
                                         DIAGNOSTIC SHIPPING EXCLUSION CHECK
==========================================================================================================================================*/
// Compiled strictly with -DFRONTIER_SHIPPING_PROFILE=1 -DFRONTIER_EDITOR=0 to prove at compile & link time
// that DiagnosticRaster and ThemeCardPalette emit zero classes, shaders, or debug rasterization code in Shipping.

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/GeometricRaster/DiagnosticRaster.h"
#include "Engine/GeometricRaster/ThemeCardPalette.h"

static_assert(FRONTIER_EDITOR == 0, "Shipping exclusion check must compile under FRONTIER_EDITOR == 0");
static_assert(!Frontier::BuildConfiguration::EditorMachineryEnabled,
              "EditorMachineryEnabled must be false in Shipping builds");

namespace Frontier
{

bool VerifyDiagnosticRasterExcludedInShipping() noexcept
{
    return (FRONTIER_EDITOR == 0) && (!BuildConfiguration::EditorMachineryEnabled);
}

} // namespace Frontier
