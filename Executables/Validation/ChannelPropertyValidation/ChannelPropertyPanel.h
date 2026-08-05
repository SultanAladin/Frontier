/*==============================================================================================================================================
                                                            CHANNELPROPERTYPANEL.H
==============================================================================================================================================*/
// 🧩 The channel slide of the paint-surface inspector: a chip region listing the live channels with a "+" popup to add more, then one collapsible
//    card per channel whose body leads with the Value / Texture / Generator segment. Ported 1:1 from Documentation/Prototypes/ChannelPropertyPanel.html.

#pragma once
#ifndef FRONTIER_CHANNELPROPERTYVALIDATION_CHANNELPROPERTYPANEL_H
#define FRONTIER_CHANNELPROPERTYVALIDATION_CHANNELPROPERTYPANEL_H

#include "ChannelSlotTable.h"

#include "EngineContext/Interface/Theme/ThemeConfiguration.h"

namespace ChannelPropertyValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Record the whole channel slide for one cycle. State is caller-owned so folds and edits survive across frames.
void ConstructChannelPropertyPanel(const Frontier::ThemeConfiguration& Theme, ChannelPropertyState& State);

}   // namespace ChannelPropertyValidation

#endif
