/*==============================================================================================================================================
                                                        SKETCHMODELVIEWPORTCHROME.CPP
==============================================================================================================================================*/
// 🧩 Builds the three menu pills' row tables and applies whatever the user picks. The Views pill arms the viewport's OWN OrientationTransition —
//    the same one the spatial compass drives — so a menu pick and a cube-face click produce one identical eased snap rather than two rival
//    animations. The Settings pill drives the grid-layer selection the panel expands into the shader's layer floats. The Units pill relabels only:
//    the world is metric by construction, so switching unit must never rescale the camera (that would silently move the scene).

#include "SketchModelViewportChrome.h"

#include "EngineContext/Interface/Components/Controls/MenuPill.h"
#include "EngineContext/Interface/SpatialCompass/Projection/TransformAlignment.h"
#include "EngineContext/Navigation/Camera/CameraProjection/ProjectionEvaluator.h"

#include "imgui.h"

namespace SketchModelViewportValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 The Views menu rows, in the mockup's order. The trailing row is the projection toggle rather than an orbit preset, so
    //    the preset lookup below is indexed only over the first seven.
    const int OrbitPresetCount = 7;

    const Frontier::AlignmentPreset OrbitPresetOrder[OrbitPresetCount] =
    {
        Frontier::AlignmentPreset::Front,
        Frontier::AlignmentPreset::Back,
        Frontier::AlignmentPreset::Right,
        Frontier::AlignmentPreset::Left,
        Frontier::AlignmentPreset::Top,
        Frontier::AlignmentPreset::Bottom,
        Frontier::AlignmentPreset::Isometric
    };

    const char* const OrbitPresetCaptions[OrbitPresetCount] =
    {
        "Front", "Back", "Right", "Left", "Top", "Bottom", "Home / Iso"
    };

    const char* const OrbitPresetKeys[OrbitPresetCount] =
    {
        "1", nullptr, "3", nullptr, "7", nullptr, "Home"
    };

    const int ProjectionRowIndex = OrbitPresetCount;   // [idx] - the row after the seven presets
    const int ViewRowCount       = OrbitPresetCount + 1;

    // 📝 Settings rows: three mutually-exclusive grid layers, then four mutually-exclusive reach choices under their own
    //    caption, then a reset action behind a separator.
    const int GridLineRowIndex    = 0;
    const int GridDotRowIndex     = 1;
    const int GridOffRowIndex     = 2;
    const int ReachCloseRowIndex  = 3;
    const int ReachStandardRowIndex = 4;
    const int ReachFarRowIndex    = 5;
    const int ReachOpenRowIndex   = 6;
    const int ResetViewRowIndex   = 7;
    const int SettingsRowCount    = 8;

    const int ReachRowCount = 4;

    const GridReachSelection ReachOrder[ReachRowCount] =
    {
        GridReachSelection::Close,
        GridReachSelection::Standard,
        GridReachSelection::Far,
        GridReachSelection::Unbounded
    };

    const char* const ReachCaptions[ReachRowCount] =
    {
        "Near fade", "Standard", "Far", "To horizon"
    };

    const int UnitRowCount = 4;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

float ResolveGridReachExtent(GridReachSelection Reach)
{
    switch (Reach)
    {
        case GridReachSelection::Close:     return  40.0f;
        case GridReachSelection::Far:       return 220.0f;
        case GridReachSelection::Unbounded: return 900.0f;
        case GridReachSelection::Standard:
        default:                            return  80.0f;   // the pass default
    }
}


float ResolveGridReachSharpness(GridReachSelection Reach)
{
    // 📝 Flattening the exponent alongside the extent is what actually removes the "clips early" read: at 2.0 the grid is already
    //    half-faded at 30% of its radius, so a longer extent alone buys almost no visible reach.
    switch (Reach)
    {
        case GridReachSelection::Close:     return 2.0f;   // the pass default curve, on a short radius
        case GridReachSelection::Far:       return 1.2f;
        case GridReachSelection::Unbounded: return 0.7f;   // below 1 the grid holds full strength most of the way out
        case GridReachSelection::Standard:
        default:                            return 2.0f;
    }
}


const char* ResolveSceneUnitSuffix(SceneUnitSelection Unit)
{
    switch (Unit)
    {
        case SceneUnitSelection::Kilometres:  return "km";
        case SceneUnitSelection::Centimetres: return "cm";
        case SceneUnitSelection::Millimetres: return "mm";
        case SceneUnitSelection::Metres:
        default:                              return "m";
    }
}


const char* ResolveSceneUnitCaption(SceneUnitSelection Unit)
{
    switch (Unit)
    {
        case SceneUnitSelection::Kilometres:  return "Kilometres";
        case SceneUnitSelection::Centimetres: return "Centimetres";
        case SceneUnitSelection::Millimetres: return "Millimetres";
        case SceneUnitSelection::Metres:
        default:                              return "Metres";
    }
}


bool ConstructCameraViewPill(const Frontier::ThemeConfiguration& Theme,
                             Frontier::ViewportPanelState&      Viewport,
                             SketchModelChromeState&            Chrome)
{
    const bool OrthographicActive = (Viewport.Camera.Projection == Frontier::ProjectionMode::Orthographic);

    Frontier::MenuPillItemDescriptor Rows[ViewRowCount] = {};
    for (int Index = 0; Index < OrbitPresetCount; ++Index)
    {
        Rows[Index].Label              = OrbitPresetCaptions[Index];
        Rows[Index].KeyHint            = OrbitPresetKeys[Index];
        Rows[Index].GroupCaption       = (Index == 0) ? "ORIENTATION" : nullptr;
        Rows[Index].CheckmarkEnabled   = false;   // action rows — the mockup's `.gp-noindent`
        Rows[Index].Marked             = false;
        Rows[Index].SeparatorPreceding = false;
    }

    Rows[ProjectionRowIndex].Label              = "Orthographic";
    Rows[ProjectionRowIndex].KeyHint            = "5";
    Rows[ProjectionRowIndex].GroupCaption       = "PROJECTION";
    Rows[ProjectionRowIndex].CheckmarkEnabled   = true;
    Rows[ProjectionRowIndex].Marked             = OrthographicActive;
    Rows[ProjectionRowIndex].SeparatorPreceding = true;

    Frontier::MenuPillDescriptor Pill = {};
    Pill.Identifier       = "viewport-views";
    Pill.MainText         = Chrome.CameraViewLabel;
    Pill.PrefixText       = nullptr;
    Pill.IconTexture      = 0;
    Pill.StatusDotEnabled = true;                                   // the mockup's `.vdot`
    Pill.CompactEnabled   = false;
    Pill.Anchor           = Frontier::MenuPillAnchor::BelowLeadingEdge;   // `.gp-menu-left`
    Pill.MenuTitle        = "Views";
    Pill.Items            = Rows;
    Pill.ItemCount        = ViewRowCount;

    const Frontier::MenuPillResult Outcome = Frontier::ConstructMenuPill(Theme, Pill);
    if (Outcome.ActivatedIndex < 0)
    {
        return false;
    }

    if (Outcome.ActivatedIndex == ProjectionRowIndex)
    {
        // 📝 The SAME entry point the compass's toggle button uses — it flips the lens and refits the ortho extent from the
        //    current orbit distance in one edit, so the scene keeps its framing and the compass glyph (which reads
        //    Camera.Projection directly) can never disagree with what the menu just did.
        Frontier::AlignProjectionMode(Viewport.Camera,
                                      OrthographicActive ? Frontier::ProjectionMode::Perspective
                                                         : Frontier::ProjectionMode::Orthographic);
        Chrome.CameraViewLabel = (Viewport.Camera.Projection == Frontier::ProjectionMode::Orthographic)
                                     ? "Orthographic" : "Perspective";
        return true;
    }

    // 📝 Arm the viewport's own transition — the compass advances it every cycle, so the menu pick rides the SAME eased snap a
    //    cube-face click uses. Nothing here writes Yaw/Pitch directly; that would fight the animation mid-flight.
    Frontier::ArmOrientationSnap(Viewport.SpatialCompass.Transition,
                                 Viewport.Camera.Yaw,
                                 Viewport.Camera.Pitch,
                                 OrbitPresetOrder[Outcome.ActivatedIndex],
                                 Viewport.SpatialCompass.Configuration.TransitionDuration);
    Viewport.SpatialCompass.SelectedPreset = OrbitPresetOrder[Outcome.ActivatedIndex];
    Chrome.CameraViewLabel                 = OrbitPresetCaptions[Outcome.ActivatedIndex];
    return true;
}


bool ConstructViewportSettingsPill(const Frontier::ThemeConfiguration& Theme,
                                   const Frontier::SvgIconRegistry&   Icons,
                                   Frontier::ViewportPanelState&      Viewport,
                                   SketchModelChromeState&            Chrome)
{
    Frontier::MenuPillItemDescriptor Rows[SettingsRowCount] = {};

    Rows[GridLineRowIndex].Label            = "Grid lines";
    Rows[GridLineRowIndex].GroupCaption     = "GRID";
    Rows[GridLineRowIndex].CheckmarkEnabled = true;
    Rows[GridLineRowIndex].Marked           = (Chrome.GridLayers == GridLayerSelection::LineGrid);

    Rows[GridDotRowIndex].Label             = "Dots";
    Rows[GridDotRowIndex].CheckmarkEnabled  = true;
    Rows[GridDotRowIndex].Marked            = (Chrome.GridLayers == GridLayerSelection::DotGrid);

    Rows[GridOffRowIndex].Label             = "None";
    Rows[GridOffRowIndex].CheckmarkEnabled  = true;
    Rows[GridOffRowIndex].Marked            = (Chrome.GridLayers == GridLayerSelection::Suppressed);

    // 📝 The reach block under its own caption — how far the grid runs before the radial fade takes it.
    for (int Index = 0; Index < ReachRowCount; ++Index)
    {
        Frontier::MenuPillItemDescriptor& Row = Rows[ReachCloseRowIndex + Index];
        Row.Label            = ReachCaptions[Index];
        Row.GroupCaption     = (Index == 0) ? "FADE" : nullptr;
        Row.CheckmarkEnabled = true;
        Row.Marked           = (Chrome.GridReach == ReachOrder[Index]);
    }

    Rows[ResetViewRowIndex].Label              = "Reset view";
    Rows[ResetViewRowIndex].KeyHint            = "Home";
    Rows[ResetViewRowIndex].CheckmarkEnabled   = false;
    Rows[ResetViewRowIndex].SeparatorPreceding = true;

    Frontier::MenuPillDescriptor Pill = {};
    Pill.Identifier       = "viewport-settings";
    Pill.MainText         = "Settings";
    Pill.IconTexture      = Frontier::ResolveIconTexture(Icons, "g-settings-gear");
    Pill.StatusDotEnabled = false;
    Pill.CompactEnabled   = false;
    Pill.Anchor           = Frontier::MenuPillAnchor::BelowTrailingEdge;   // `.gp-menu`
    Pill.MenuTitle        = "Viewport Settings";
    Pill.Items            = Rows;
    Pill.ItemCount        = SettingsRowCount;

    const Frontier::MenuPillResult Outcome = Frontier::ConstructMenuPill(Theme, Pill);
    if (Outcome.ActivatedIndex < 0)
    {
        return false;
    }

    if (Outcome.ActivatedIndex >= ReachCloseRowIndex && Outcome.ActivatedIndex <= ReachOpenRowIndex)
    {
        Chrome.GridReach = ReachOrder[Outcome.ActivatedIndex - ReachCloseRowIndex];
        return true;
    }

    switch (Outcome.ActivatedIndex)
    {
        case GridLineRowIndex:
            Chrome.GridLayers = GridLayerSelection::LineGrid;
            return true;
        case GridDotRowIndex:
            Chrome.GridLayers = GridLayerSelection::DotGrid;
            return true;
        case GridOffRowIndex:
            Chrome.GridLayers = GridLayerSelection::Suppressed;
            return true;
        case ResetViewRowIndex:
        default:
            // 📝 Reset rides the same eased snap as the compass Home button, landing on the isometric pose.
            Frontier::ArmOrientationSnap(Viewport.SpatialCompass.Transition,
                                         Viewport.Camera.Yaw,
                                         Viewport.Camera.Pitch,
                                         Frontier::AlignmentPreset::Isometric,
                                         Viewport.SpatialCompass.Configuration.TransitionDuration);
            Chrome.CameraViewLabel = "Home / Iso";
            return true;
    }
}


bool ConstructSceneUnitPill(const Frontier::ThemeConfiguration& Theme,
                            SketchModelChromeState&             Chrome)
{
    const SceneUnitSelection UnitOrder[UnitRowCount] =
    {
        SceneUnitSelection::Kilometres,
        SceneUnitSelection::Metres,
        SceneUnitSelection::Centimetres,
        SceneUnitSelection::Millimetres
    };

    Frontier::MenuPillItemDescriptor Rows[UnitRowCount] = {};
    for (int Index = 0; Index < UnitRowCount; ++Index)
    {
        Rows[Index].Label            = ResolveSceneUnitCaption(UnitOrder[Index]);
        Rows[Index].KeyHint          = ResolveSceneUnitSuffix(UnitOrder[Index]);
        Rows[Index].CheckmarkEnabled = true;
        Rows[Index].Marked           = (Chrome.SceneUnit == UnitOrder[Index]);
    }

    Frontier::MenuPillDescriptor Pill = {};
    Pill.Identifier       = "viewport-units";
    Pill.MainText         = ResolveSceneUnitCaption(Chrome.SceneUnit);
    Pill.PrefixText       = "World \xC2\xB7";                              // "World ·"
    Pill.IconTexture      = 0;
    Pill.StatusDotEnabled = false;
    Pill.CompactEnabled   = true;                                          // `.vp-pill-sm`
    Pill.Anchor           = Frontier::MenuPillAnchor::AboveTrailingEdge;    // `.gp-menu-up`
    Pill.MenuTitle        = "Units";
    Pill.Items            = Rows;
    Pill.ItemCount        = UnitRowCount;

    const Frontier::MenuPillResult Outcome = Frontier::ConstructMenuPill(Theme, Pill);
    if (Outcome.ActivatedIndex < 0)
    {
        return false;
    }

    Chrome.SceneUnit = UnitOrder[Outcome.ActivatedIndex];
    return true;
}


float ResolveTopClusterSpan(const Frontier::ThemeConfiguration& Theme, const SketchModelChromeState& Chrome)
{
    (void)Theme;
    (void)Chrome;

    // 📝 One standard pill: gear glyph + "Settings" + caret. Measured rather than guessed so the band's right-alignment is exact.
    const float PillHeight  = Frontier::ResolveMenuPillHeight(false);
    const float GlyphEdge   = PillHeight - 14.0f;
    const float TextPadding = 11.0f;
    const float ContentGap  =  7.0f;
    const float CaretWidth  = 26.0f;
    return TextPadding * 2.0f + GlyphEdge + ContentGap + ImGui::CalcTextSize("Settings").x + CaretWidth;
}


float ResolveFooterClusterSpan(const Frontier::ThemeConfiguration& Theme, const SketchModelChromeState& Chrome)
{
    (void)Theme;

    // 📝 One compact pill: dim "World ·" prefix + the unit caption + caret.
    const float TextPadding = 9.0f;
    const float CaretWidth  = 22.0f;
    return TextPadding * 2.0f
         + ImGui::CalcTextSize("World \xC2\xB7").x + 4.0f
         + ImGui::CalcTextSize(ResolveSceneUnitCaption(Chrome.SceneUnit)).x
         + CaretWidth;
}

}   // namespace SketchModelViewportValidation
