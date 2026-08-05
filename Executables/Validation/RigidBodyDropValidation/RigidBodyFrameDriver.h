/*============================================================================================================================================
                                                          RIGIDBODYFRAMEDRIVER.H
============================================================================================================================================*/
// 🧩 The per-frame seam, built entirely from the renderer's EXISTING public surface — no engine edit.
//
//    The problem: RenderExtension's SynthesizeOutputSequence both installs the two frame recorders AND enters the blocking present loop, so there
//    is no moment between "recorders assigned" and "loop running" for a host to reach in. And the physics drive must land at a precise point:
//    inside the ImGui frame (which RecordPreamble opens and RecordSequence closes with ImGui::Render), because the control window is submitted from
//    the same call that advances the solver.
//
//    The seam: WindowSubstrate::RecordSequence / RecordPreamble are plain public std::function members. So this driver
//      1. lets SynthesizeOutputSequence's caller install the renderer's own recorders,
//      2. MOVES them out and installs its own pair that calls the host action and then delegates,
//      3. runs the substrate loop itself through the public RunWindowSubstrate.
//    ⚠️ Ordering: the host action runs INSIDE the renderer's RecordSequence window — after the renderer's own draws are recorded but before
//       ImGui::Render is reached — which is why the delegate is invoked with the action wrapped around it rather than merely before it. Getting
//       this backwards submits ImGui widgets after Render and they silently never appear.

#pragma once
#ifndef FRONTIER_VALIDATION_RIGIDBODYDROP_RIGIDBODYFRAMEDRIVER_H
#define FRONTIER_VALIDATION_RIGIDBODYDROP_RIGIDBODYFRAMEDRIVER_H

#include "Graphics/RenderExtension/RenderExtension.h"

#include <functional>

namespace Frontier
{

// Install the renderer's recorders, wrap them with HostAction, and run the present loop until the window closes. HostAction is called once per
// frame from inside the ImGui frame scope, so it may both advance a simulation and submit ImGui windows. Returns when the loop exits; the caller
// still owns Finalize.
//
// 🔴 This REPLACES the call to SynthesizeOutputSequence — do not call both, or the recorders are installed twice and the loop runs twice.
void DriveRigidBodyPresentLoop(RenderExtension& Extension, const std::function<void()>& HostAction);

} // namespace Frontier

#endif
