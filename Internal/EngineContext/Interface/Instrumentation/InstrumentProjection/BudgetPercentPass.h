/*==============================================================================================================================================
                                                            BUDGETPERCENTPASS.H
==============================================================================================================================================*/
// 🧩 The mid-right "Budget" panel: a large percent value, threshold-tinted (coral under 20, yellow under 40, else green), over a muted caption sentence. Reads signal slot 0 as the percent (0..100, the app pre-scales it) and Presentation.Payload.Caption as the static line beneath. One Construct call draws one budget body.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_BUDGETPERCENTPASS_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_BUDGETPERCENTPASS_H

#include "InstrumentBodyContext.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Draw the budget body: the threshold-tinted percent and the caption. Appends to Context.DrawList only.
void ConstructBudgetPercent(const InstrumentBodyContext& Context);

}   // namespace Frontier

#endif
