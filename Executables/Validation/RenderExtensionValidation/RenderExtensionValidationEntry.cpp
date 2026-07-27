/*==============================================================================================================================================
                                                     RENDEREXTENSIONVALIDATIONENTRY.CPP
==============================================================================================================================================*/
// 🧩 Standalone test bed for the modular RenderExtension. Opens one window, initializes the extension (which brings up the Vulkan host, the
//    swapchain, and inspects the GPU's feature profile), drives the frame sequence until the window closes, then finalizes. This app exists
//    only to exercise the extension in isolation — the same three calls are what any real application makes to embed the renderer. The extension
//    owns its own WindowSubstrate (window + Vulkan host + present loop), so there is no separate ImGui host seam here: Initialize / Synthesize /
//    Finalize is the whole surface.

#include "Graphics/RenderExtension/RenderExtension.h"


//------------------------------------------------------------------------------------------------------------------------
//                                                              MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** ArgumentValues)
{
    (void)ArgumentCount;
    (void)ArgumentValues;

    Frontier::RenderExtension Extension;
    if (!Frontier::InitializeRenderExtension(Extension, "Frontier \xE2\x80\x94 RenderExtension Validation", 1600, 900))
    {
        Frontier::FinalizeRenderExtension(Extension);
        return 1;
    }

    Frontier::SynthesizeOutputSequence(Extension);

    Frontier::FinalizeRenderExtension(Extension);
    return 0;
}
