/*==============================================================================================================================================
                                                              CONTROLLAYOUT.H
==============================================================================================================================================*/
// 🧩 The shared label-plus-field row every control draws itself into, PLUS the shared pill-drawing primitives that give every numeric row the
//    ControlsPreview.html look: a fully-rounded "[ grey axis | black number | grey unit ]" value box with a white hairline outline, and a
//    black/grey/white slider track. Written ONCE here so ValueSlider, ScalarEntry, VectorEntry, ColorEntry, SelectionEntry, BooleanEntry,
//    PathEntry and Dropdown all align + paint identically — a control never re-derives the split or re-draws a pill (the "shared, not
//    duplicated" rule). Crucially, the numeric segment hosts a REAL ImGui drag/input widget, so the field is genuinely editable: click-drag to
//    scrub, double-click to select-all and type, arrow keys, and mouse text-selection all work natively.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CONTROLS_CONTROLLAYOUT_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_COMPONENTS_CONTROLS_CONTROLLAYOUT_H

#include "imgui.h"

#include "../../Theme/ThemeConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One value pill's geometry, resolved by BeginValuePill. The caller draws its number/segments inside these rects.
struct ValuePillLayout
{
    ImVec2 PillMin;        // [px] - Top-left of the whole pill
    ImVec2 PillMax;        // [px] - Bottom-right of the whole pill
    ImVec2 NumberMin;      // [px] - Top-left of the central (black) number segment
    ImVec2 NumberMax;      // [px] - Bottom-right of the number segment
    float  Height;         // [px] - Pill height (== themed row height)
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Emit the label of a control row and advance the cursor to the field column. Returns the width [px] remaining for the field so the
//    caller can size its widget to fill the row. Every control opens with this — identical alignment for free.
float BeginControlRow(const ThemeConfiguration& Theme, const char* Label);

// 📝 Close a control row: restore the item width the row pushed and add the inter-control spacing. Pairs with BeginControlRow.
void EndControlRow(const ThemeConfiguration& Theme);


// 📝 Paint a fully-rounded value pill spanning [TopLeft, TopLeft+Size]: black number segment in the middle, optional grey side segments
//    carrying AxisText (left) and UnitText (right), and a white hairline outline. Returns the geometry so the caller can host an editable
//    widget in the number segment. Pass null AxisText/UnitText to omit that grey cap. Segment glyphs are drawn muted + centred.
ValuePillLayout DrawValuePill(const ThemeConfiguration& Theme, ImVec2 TopLeft, ImVec2 Size,
                              const char* AxisText, const char* UnitText, bool Enabled);

// 📝 Overlay a real, fully-editable numeric drag-field exactly inside a pill's number segment. Transparent background + no frame so only the
//    typed number shows over the black segment; the native widget still gives drag-to-scrub, double-click-select and text-cursor editing.
//    Returns true the cycle Value changed. Speed is the drag step per pixel; Minimum==Maximum leaves it unbounded.
bool EditPillNumber(const ThemeConfiguration& Theme, const ValuePillLayout& Layout, const char* Identifier,
                    float* Value, float Speed, float Minimum, float Maximum, const char* Format);


// 📝 Paint the slider track that ValueSlider/ScalarEntry share: pitch-black rounded track, a lighter-grey fill up to Fraction (0-1, pass <0 to
//    hide the fill for an unbounded drag), a white circular knob centred at Fraction, and a white hairline outline. Returns the knob centre so
//    a caller can hit-test if needed. Drawn into the current window draw list at [TopLeft, TopLeft+Size].
ImVec2 DrawSliderTrack(const ThemeConfiguration& Theme, ImVec2 TopLeft, ImVec2 Size, float Fraction, bool Enabled);


// 📝 The themed pill row height in pixels (RowHeight * UiScale), clamped to at least the base frame height so text never clips.
float ResolvePillHeight(const ThemeConfiguration& Theme);

}   // namespace Frontier

#endif
