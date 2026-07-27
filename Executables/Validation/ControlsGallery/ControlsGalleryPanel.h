/*==============================================================================================================================================
                                                            CONTROLSGALLERYPANEL.H
==============================================================================================================================================*/
// 🧩 A standalone test surface that exercises EVERY control in ControlsPreview.html against the real Interface controls. This is a UI-only
//    validation: it owns nothing but placeholder state and draws one card per control family so the pill styling, editable numbers, dropdown
//    popup and Figma colour picker can be eyeballed in isolation, fully decoupled from the modelling / paint / bake applications.

#pragma once
#ifndef FRONTIER_CONTROLSGALLERY_PANEL_H
#define FRONTIER_CONTROLSGALLERY_PANEL_H

#include "EngineContext/Interface/Theme/ThemeConfiguration.h"

using namespace Frontier;


//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 All caller-owned state the gallery edits in place. One instance lives in main() for the life of the window; the panel only reads/writes
//    through it so the controls themselves stay stateless (the descriptor-in, value-owned-by-caller contract).
struct ControlsGalleryState
{
    // -- ValueSlider (bounded slider + editable pill) --
    float Degree      = 45.0f;
    float Percent     = 0.62f;
    float Pixel       = 128.0f;

    // -- ScalarEntry (unbounded drag pill) --
    float Intensity   = 1.20f;
    float Radius      = 24.0f;

    // -- VectorEntry (XYZ) --
    float Position[3] = { 1.0f, 0.5f, -2.0f };

    // -- ColorEntry (Figma picker) --
    float BaseColor[4] = { 0.29f, 0.56f, 0.89f, 1.0f };

    // -- SelectionEntry (segmented) --
    int   CastShadow  = 2;      // Off / Hard / Soft / Merged
    int   SizePreset  = 1;      // S / M / L / XL

    // -- BooleanEntry (toggle) --
    bool  IndirectGi  = true;
    bool  Wireframe   = false;

    // -- PathEntry (editable text + browse) --
    char  MeshSource[260] = "Content/Meshes/Suzanne.glb";

    // -- Dropdown (combo popup) --
    int   Shading     = 0;      // Lit / Unlit / Normals / Wireframe

    // -- Card collapse flags (caller-owned so collapse survives cycles) --
    bool  ValueSliderExpanded = true;
    bool  ScalarExpanded      = true;
    bool  VectorExpanded      = true;
    bool  ColorExpanded       = true;
    bool  SelectionExpanded   = true;
    bool  SizeExpanded        = true;
    bool  BooleanExpanded     = true;
    bool  PathExpanded        = true;
    bool  DropdownExpanded    = true;
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Draw the whole gallery inside the current ImGui window: one property card per control family from ControlsPreview.html.
void ConstructControlsGalleryPanel(const ThemeConfiguration& Theme, ControlsGalleryState& State);

#endif
