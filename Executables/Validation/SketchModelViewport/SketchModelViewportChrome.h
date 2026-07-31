/*==============================================================================================================================================
                                                        SKETCHMODELVIEWPORTCHROME.H
==============================================================================================================================================*/
// 🧩 The chrome selections the viewport bands own across cycles, and the three menu pills that edit them. Mirrors the mockup's module-scope
//    script variables: which camera view is current, which grid layers are lit, and which unit the scene reads in. Kept beside the panel rather
//    than inside the shared ViewportPanelState because these are the VALIDATION host's chrome choices — the shared viewport has no opinion about
//    unit labels or bookmark rows. Each pill returns whether it changed something, so the caller re-derives only what moved.

#pragma once
#ifndef FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELVIEWPORTCHROME_H
#define FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELVIEWPORTCHROME_H

#include "EngineContext/Interface/Theme/ThemeConfiguration.h"
#include "EngineContext/Interface/WorkspaceHost/Viewport/ViewportPanel.h"
#include "EngineContext/Interface/Icons/SvgIconRegistry.h"

namespace SketchModelViewportValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                           ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which grid layers the Settings menu has lit. The mockup offers the three states as mutually-exclusive rows, so this is one
//    selection rather than two independent toggles — the panel expands it into the two layer floats the shader reads.
enum class GridLayerSelection
{
    LineGrid,   // [-] - Analytic line grid only
    DotGrid,    // [-] - Dotted layer only (the mockup default)
    Suppressed  // [-] - Neither layer; the origin axes still draw
};


// 📝 How far the ground grid reaches before it dissolves. The analytic grid fades radially, and at the shader's default squared
//    falloff the lines thin out well inside the extent — which reads as the scene clipping early. Each choice here carries BOTH
//    the extent and the falloff exponent (see ResolveGridReachExtent / ResolveGridReachSharpness), because widening the reach
//    without also flattening the curve just moves a fade that still starts too soon.
enum class GridReachSelection
{
    Close,      // [-] - Tight fade, for close-up detail work
    Standard,   // [-] - The pass default
    Far,        // [-] - Wider reach on a gentler curve
    Unbounded   // [-] - Nearly linear falloff at a long extent; the grid runs to the horizon
};


// 📝 The scene's display unit. Purely a chrome label in this host — the world stays in metres (the camera and grid are metric by
//    construction); the unit only relabels the readout, exactly as the mockup's units pill does.
enum class SceneUnitSelection
{
    Kilometres,   // [-] - km
    Metres,       // [-] - m (default)
    Centimetres,  // [-] - cm
    Millimetres   // [-] - mm
};


//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The chrome state one viewport carries across cycles. CameraViewLabel is the text the Views pill shows; it tracks the last
//    preset snapped to and reverts to "Perspective" / "Orthographic" once the user orbits freely off that preset.
struct SketchModelChromeState
{
    GridLayerSelection GridLayers      = GridLayerSelection::DotGrid;     // [-] - Settings → grid layers
    GridReachSelection GridReach       = GridReachSelection::Far;         // [-] - Settings → how far the grid reaches
    SceneUnitSelection SceneUnit       = SceneUnitSelection::Metres;      // [-] - Units → display unit
    const char*        CameraViewLabel = "Perspective";                   // [-] - Views pill caption (borrowed literal)
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Record the Views pill: the seven orbit presets plus the projection toggle. Arms the SAME eased snap the spatial compass
//    uses (ArmOrientationSnap on the viewport's own transition), so a menu pick and a cube click animate identically.
//    Returns true the cycle the camera or its projection changed.
bool ConstructCameraViewPill(const Frontier::ThemeConfiguration& Theme,
                             Frontier::ViewportPanelState&      Viewport,
                             SketchModelChromeState&            Chrome);

// 📝 Record the Settings pill: the three grid-layer rows and a reset-view action. Returns true the cycle a selection changed.
bool ConstructViewportSettingsPill(const Frontier::ThemeConfiguration& Theme,
                                   const Frontier::SvgIconRegistry&   Icons,
                                   Frontier::ViewportPanelState&      Viewport,
                                   SketchModelChromeState&            Chrome);

// 📝 Record the compact Units pill (footer variant, menu opens upward). Returns true the cycle the unit changed.
bool ConstructSceneUnitPill(const Frontier::ThemeConfiguration& Theme,
                            SketchModelChromeState&             Chrome);

// 📝 The grid's fade radius [m] for the chosen reach — pushed into GroundGridConstants.GridExtent.
float ResolveGridReachExtent(GridReachSelection Reach);

// 📝 The radial falloff exponent for the chosen reach — pushed into GroundGridConstants.FadeSharpness. Pairs with the extent
//    above; a wider extent on the default squared curve would still dissolve too early to read as "further".
float ResolveGridReachSharpness(GridReachSelection Reach);

// 📝 The unit's short suffix ("m", "mm", …) for a coordinate readout.
const char* ResolveSceneUnitSuffix(SceneUnitSelection Unit);

// 📝 The unit's full name ("Metres", …) for the pill caption.
const char* ResolveSceneUnitCaption(SceneUnitSelection Unit);

// 📝 Total width the top band's trailing cluster needs, so the band can right-align it before the pills are recorded.
float ResolveTopClusterSpan(const Frontier::ThemeConfiguration& Theme, const SketchModelChromeState& Chrome);

// 📝 Total width the footer band's trailing cluster needs.
float ResolveFooterClusterSpan(const Frontier::ThemeConfiguration& Theme, const SketchModelChromeState& Chrome);

}   // namespace SketchModelViewportValidation

#endif
