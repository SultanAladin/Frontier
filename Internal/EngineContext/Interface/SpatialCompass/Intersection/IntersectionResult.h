/*==============================================================================================================================================
                                                          INTERSECTIONRESULT.H
==============================================================================================================================================*/
// 🧩 What one record cycle of the spatial compass reports back: which zone the cursor resolved to, which preset a click committed to (if any),
//    and whether the camera pose changed this cycle. The .cpp fills one of these and the caller reacts — the mockup's face-click / roll / home /
//    projection-toggle outcomes collapsed into a single return payload. Header-only POD.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_INTERSECTIONRESULT_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_INTERSECTIONRESULT_H

#include "../Configuration/AlignmentPreset.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The per-cycle outcome. HoverZone is what the cursor is over (for the hover highlight); Committed is true when a face was
//    clicked / Home pressed this cycle and CommittedPreset names it. CameraChanged is true whenever the overlay moved the
//    camera (a drag orbit, a snap step, or a roll), so the caller can request a redraw / mark the view dirty.
struct IntersectionResult
{
    CompassZone     HoverZone        = CompassZone::None;          // [-] - Zone under the cursor this cycle
    AlignmentPreset HoverPreset      = AlignmentPreset::Count;     // [-] - Face under the cursor (valid when HoverZone==Face)
    bool            Committed        = false;                      // [-] - A face/Home was picked this cycle
    AlignmentPreset CommittedPreset  = AlignmentPreset::Count;     // [-] - The picked view (valid when Committed)
    bool            CameraChanged    = false;                      // [-] - The overlay moved the camera this cycle
};

}   // namespace Frontier

#endif
