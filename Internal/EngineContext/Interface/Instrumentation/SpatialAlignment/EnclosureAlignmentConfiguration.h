/*==============================================================================================================================================
                                                        ENCLOSUREALIGNMENTCONFIGURATION.H
==============================================================================================================================================*/
// 🧩 The optional first-placement hint for an instrument's card: where it should sit and how big it should be the first time it appears, before ImGui takes over persisting its rect. After first use the user's drag/resize/dock wins — this configuration only seeds the initial layout so a fresh overlay doesn't stack every card at the origin.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_ENCLOSUREALIGNMENTCONFIGURATION_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_INSTRUMENTATION_ENCLOSUREALIGNMENTCONFIGURATION_H

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A first-frame placement seed. AlignmentActive gates whether the configuration is applied at all (an app that wants pure
//    auto-layout leaves it false). OriginX/OriginY are the desired top-left in viewport pixels; ExtentWidth/ExtentHeight the
//    desired size. Applied with ImGuiCond_FirstUseEver so it never fights the user after the first appearance. Trivial POD.
struct EnclosureAlignmentConfiguration
{
    bool  AlignmentActive = false;    // [-] - false = let the overlay auto-place; true = use the fields below on first use
    float OriginX         = 24.0f;    // [px] - desired first-frame top-left x (viewport pixels)
    float OriginY         = 24.0f;    // [px] - desired first-frame top-left y
    float ExtentWidth     = 240.0f;   // [px] - desired first-frame width
    float ExtentHeight    = 120.0f;   // [px] - desired first-frame height
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Apply the configuration's placement to the NEXT ImGui window opened, using FirstUseEver so it seeds only the initial
// layout. A no-op when AlignmentActive is false. Declared here; defined in the .cpp so this header stays ImGui-free for
// callers that only construct configurations.
void ApplyEnclosureAlignment(const EnclosureAlignmentConfiguration& Profile);

}   // namespace Frontier

#endif
