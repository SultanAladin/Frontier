/*============================================================================================================================================
                                                          RIGIDBODYCONTROLWINDOW.H
============================================================================================================================================*/
// 🧩 The one ImGui window this validation drives itself from, drawn entirely out of the shared ControlsGallery components (ValueSlider,
//    BooleanEntry) over the same ThemeConfiguration the rest of the engine's panels resolve — so it looks like the gallery rather than raw ImGui.
//    Reads the live readout and writes the tuning in place, so a slider change lands on the next advance with no mirrored copy to fall out of step.

#pragma once
#ifndef FRONTIER_VALIDATION_RIGIDBODYDROP_RIGIDBODYCONTROLWINDOW_H
#define FRONTIER_VALIDATION_RIGIDBODYDROP_RIGIDBODYCONTROLWINDOW_H

#include "EngineContext/Interface/Theme/ThemeConfiguration.h"
#include "RigidBodySimulation.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONTRACT
//------------------------------------------------------------------------------------------------------------------------

// 📝 What the window asked for that the caller must act on. The window itself never touches the world — it only reports intent, so the host owns
//    the ordering between a reseed and the frame's advance.
struct RigidBodyWindowOutcome
{
    bool ReseedRequested = false;   // [-] - the Reset row was pressed this cycle
    bool SingleAdvance   = false;   // [-] - the Step row was pressed while paused: advance exactly one interval
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Record the control window. Tuning is edited in place; Readout is read-only. Returns what the caller must act on.
RigidBodyWindowOutcome DrawRigidBodyControlWindow(const ThemeConfiguration& Theme, RigidBodyTuning& Tuning, const RigidBodyReadout& Readout);

} // namespace Frontier

#endif
