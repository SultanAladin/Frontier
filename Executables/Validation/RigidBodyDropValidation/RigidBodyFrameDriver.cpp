/*============================================================================================================================================
                                                         RIGIDBODYFRAMEDRIVER.CPP
============================================================================================================================================*/
// 🧩 See RigidBodyFrameDriver.h. Implemented with a DEFERRED-RUN latch, because SynthesizeOutputSequence installs the recorders and then enters the
//    blocking loop in one call — leaving no return point to wrap at.
//
//    The mechanism: RunWindowSubstrate's loop is entered only while the window has not been asked to close. So this driver
//      1. asks the window to close BEFORE calling SynthesizeOutputSequence, so its RunWindowSubstrate installs the recorders and then falls straight
//         through its `while (!QueryWindowCloseRequested(...))` guard without presenting a single frame,
//      2. takes the now-installed recorders, clears the close request, and installs its own wrapped pair,
//      3. runs RunWindowSubstrate again — this time for real.
//
//    ⚠️ Step 1 relies on the loop testing the close flag BEFORE its first present, which it does (WindowSubstrate.cpp: the `while` guard precedes
//       PollPlatformEvents). It also relies on SynthesizeOutputSequence doing no per-frame work outside the recorders, which holds — everything it
//       does before RunWindowSubstrate is assignment.

#include "RigidBodyFrameDriver.h"

#include "Graphics/RenderExtension/Device/WindowSubstrate.h"

#include <utility>

namespace Frontier
{

void DriveRigidBodyPresentLoop(RenderExtension& Extension, const std::function<void()>& HostAction)
{
    // -- 1. Arm the close latch so the renderer's own loop exits before its first frame -------------------------------
    // PlatformWindow::CloseRequested is a plain public bool and QueryWindowCloseRequested only mirrors it, so setting it here is exactly what an
    // interactive close does. No API is being subverted — the loop is simply told it is already finished.
    Extension.Substrate.Window.CloseRequested = true;

    SynthesizeOutputSequence(Extension);   // installs RecordPreamble + RecordSequence, runs zero frames

    // -- 2. Take the installed recorders and wrap them ---------------------------------------------------------------
    FrameRecorder EnginePreamble = std::move(Extension.Substrate.RecordPreamble);
    FrameRecorder EngineSequence = std::move(Extension.Substrate.RecordSequence);

    Extension.Substrate.RecordPreamble = EnginePreamble;   // nothing to add ahead of the ImGui frame open

    // 📝 The host action runs INSIDE the engine's sequence recorder — the engine's own draws are recorded first, then the action advances the solver
    //    and submits its control window, and only then does the engine's ImGui::Render (at the tail of EngineSequence) consume the draw data.
    //    🔴 Which means the action cannot simply run after EngineSequence returns: Render has already happened by then and the window would never
    //       appear. It runs BEFORE, so the widgets are queued into the frame Render is about to flush.
    Extension.Substrate.RecordSequence =
        [&Extension, EngineSequence, HostAction](VkCommandBuffer CommandBuffer, VkExtent2D Extent)
        {
            if (HostAction)
                HostAction();
            if (EngineSequence)
                EngineSequence(CommandBuffer, Extent);
        };

    // -- 3. Clear the latch and run the loop for real -----------------------------------------------------------------
    Extension.Substrate.Window.CloseRequested = false;
    RunWindowSubstrate(Extension.Substrate);
}

} // namespace Frontier
