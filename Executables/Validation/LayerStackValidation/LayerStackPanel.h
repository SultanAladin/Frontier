/*==============================================================================================================================================
                                                             LAYERSTACKPANEL.H
==============================================================================================================================================*/
// 🧩 The Layers slide of the paint-surface inspector: an "Add Layer" affordance, a filter box, the scrollable stack of rows, and an inline expand
//    that drops open beneath the focused row carrying its settings and mask editor. Ported 1:1 from
//    Documentation/Prototypes/PaintingSurface/Interface/LayerInspector.js (BuildRow / BuildExpand / BuildMaskSection / RenderStackFoot).

#pragma once
#ifndef FRONTIER_LAYERSTACKVALIDATION_LAYERSTACKPANEL_H
#define FRONTIER_LAYERSTACKVALIDATION_LAYERSTACKPANEL_H

#include "LayerRecordTable.h"

#include "EngineContext/Interface/Theme/ThemeConfiguration.h"

namespace LayerStackValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Record the whole layer rail for one cycle. State is caller-owned so folds, focus and edits survive across frames.
void ConstructLayerStackPanel(const Frontier::ThemeConfiguration& Theme, LayerStackState& State);

}   // namespace LayerStackValidation

#endif
