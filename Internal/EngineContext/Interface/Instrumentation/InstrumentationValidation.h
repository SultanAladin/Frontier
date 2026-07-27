/*==============================================================================================================================================
                                                          INSTRUMENTATIONVALIDATION.H
==============================================================================================================================================*/
// 🧩 The headless coverage scaffold for the instrumentation data layer: exercises the ring accumulator, the vacancy recycler, and the token-stale store logic without an ImGui context, so the non-drawing half of the overlay has build + behaviour coverage a host can run at startup. Reports pass/fail counts; touches nothing graphical.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_INSTRUMENTATIONVALIDATION_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_INSTRUMENTATIONVALIDATION_H

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The tally a validation run reports. CheckedCount is every assertion attempted; PassedCount those that held. Equal counts
//    mean a clean run. Trivial POD so a host can print it without pulling in the validation internals.
struct InstrumentationValidationReport
{
    uint32_t CheckedCount = 0u;   // [-] - total assertions attempted
    uint32_t PassedCount  = 0u;   // [-] - assertions that held
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Run the data-layer checks (ring append/unroll ordering + min/max, vacancy LIFO recycling, store register/withdraw/resolve
// with token staleness) and return the tally. Pure CPU, no ImGui, no allocation beyond stack scratch — safe to call at
// startup. A clean run has CheckedCount == PassedCount.
InstrumentationValidationReport RunInstrumentationValidation();

}   // namespace Frontier

#endif
