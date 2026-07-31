/*==============================================================================================================================================
                                                            SKYATMOSPHERE.H
==============================================================================================================================================*/
// 🧩 A modular Hillaire-2020 sky/atmosphere pass. Bakes three precomputed lookup tables once at boot — transmittance (256×64), isotropic
//    multi-scatter (32×32), and a sun-dependent sky-view radiance table (192×108) — then, per frame, draws a fullscreen sky dome that samples
//    the sky-view table along the reconstructed view ray and composites an analytic sun disc, exposure-tonemapped. Built once (LUT images,
//    samplers, descriptor sets, four pipelines); recorded once per frame into the substrate's open dynamic-rendering scope, BEFORE the grid.
//    The sky-view table re-bakes only when the sun direction changes. Struct + free-function style, raw Vulkan (no VMA), Pascal-floor safe.

#pragma once
#ifndef FRONTIER_GRAPHICS_ATMOSPHERE_SKYATMOSPHERE_H
#define FRONTIER_GRAPHICS_ATMOSPHERE_SKYATMOSPHERE_H

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "Graphics/Atmosphere/AtmosphereProfile.h"

#include <vulkan/vulkan.h>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Per-frame push data for the sky-dome draw. Byte-compatible with the SkyPush push_constant block in SkyDome.frag: one mat4 (64) + one
//    vec4 (16) + four floats (16) = 96 bytes, inside the Pascal 256-byte push limit. The InverseViewProjection is the clip→world matrix the
//    camera already hands the grid; the four scalars steer the sun disc and exposure so a UI can drive them without touching the pass.
struct SkyDomeConstants
{
    float InverseViewProjection[16];                 // [-]   - Clip → world (column-major, matches Matrix4f)
    float CameraPosition[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // [m] - World eye xyz; w unused
    float SunAngularRadius  = 0.0047f;               // [rad] - Solar disc half-angle (~0.27°, real sun)

    // 🔴 DEAD, AND KEPT ONLY TO HOLD THE PUSH BLOCK'S SHAPE. SkyDome.frag no longer reads this — it writes linear radiance and
    //    RadianceResolve owns exposure and the tone curve. Removing the float would shrink the 96-byte block and shift DomeEnabled,
    //    so it stays as an explicit hole. Set to zero, NOT to its old 8.0: a live-looking value here is how the sky's real
    //    calibration got lost once already (it now lives in AtmosphereProfile.h::SolarIlluminanceCalibration). Writing anything
    //    here changes nothing on screen.
    float ExposureUnused    = 0.0f;                  // [-]   - inert push-block hole; see above
    float SunIntensity      = 20.0f;                 // [-]   - Brightness of the disc itself, RELATIVE to SolarIlluminance
    float DomeEnabled       = 1.0f;                  // [-]   - 1 draws the sky, 0 leaves the clear
};

// 📝 The pass's device-side resources, built once. Holds the three LUT images + their views/memory/sampler, the shared UBO (atmosphere profile
//    mirrored to the GPU, re-uploaded on sun move), the descriptor infrastructure, the three bake pipelines, and the per-frame dome pipeline.
//    SunDirtyCondition forces a sky-view re-bake next frame; the pass owns the profile so a UI edits it through UpdateSkyAtmosphereProfile.
struct SkyAtmospherePass
{
    // -- Atmosphere state -------------------------------------------------------------------------------------------------
    AtmosphereUniformBlock Profile;                          // [-] - CPU-side authoritative atmosphere description
    bool                   SunDirtyCondition   = true;       // [-] - Sky-view needs a (re)bake before next dome draw

    // -- Uniform buffer (profile mirror) ----------------------------------------------------------------------------------
    VkBuffer               ProfileBuffer       = VK_NULL_HANDLE;
    VkDeviceMemory         ProfileMemory       = VK_NULL_HANDLE;
    void*                  ProfileMapping      = nullptr;     // [-] - Persistent host map, re-written on sun move

    // -- LUT images -------------------------------------------------------------------------------------------------------
    VkImage                TransmittanceImage  = VK_NULL_HANDLE;
    VkDeviceMemory         TransmittanceMemory = VK_NULL_HANDLE;
    VkImageView            TransmittanceView   = VK_NULL_HANDLE;

    VkImage                MultiScatterImage   = VK_NULL_HANDLE;
    VkDeviceMemory         MultiScatterMemory  = VK_NULL_HANDLE;
    VkImageView            MultiScatterView    = VK_NULL_HANDLE;

    VkImage                SkyViewImage        = VK_NULL_HANDLE;
    VkDeviceMemory         SkyViewMemory       = VK_NULL_HANDLE;
    VkImageView            SkyViewView         = VK_NULL_HANDLE;

    VkSampler              LinearSampler       = VK_NULL_HANDLE;   // [-] - Clamp-to-edge linear sampler for all LUTs

    // -- Descriptors ------------------------------------------------------------------------------------------------------
    VkDescriptorPool       DescriptorPool      = VK_NULL_HANDLE;
    VkDescriptorSetLayout  TransmittanceSetLayout = VK_NULL_HANDLE; // UBO + storage image
    VkDescriptorSetLayout  MultiScatterSetLayout  = VK_NULL_HANDLE; // UBO + transmittance sampler + storage image
    VkDescriptorSetLayout  SkyViewSetLayout       = VK_NULL_HANDLE; // UBO + transmittance + multiscatter samplers
    VkDescriptorSetLayout  DomeSetLayout          = VK_NULL_HANDLE; // UBO + transmittance + skyview samplers
    VkDescriptorSet        TransmittanceSet    = VK_NULL_HANDLE;
    VkDescriptorSet        MultiScatterSet     = VK_NULL_HANDLE;
    VkDescriptorSet        SkyViewSet          = VK_NULL_HANDLE;
    VkDescriptorSet        DomeSet             = VK_NULL_HANDLE;

    // -- Pipelines --------------------------------------------------------------------------------------------------------
    VkPipelineLayout       TransmittanceLayout = VK_NULL_HANDLE;
    VkPipeline             TransmittancePipeline = VK_NULL_HANDLE;   // compute
    VkPipelineLayout       MultiScatterLayout  = VK_NULL_HANDLE;
    VkPipeline             MultiScatterPipeline = VK_NULL_HANDLE;    // compute
    VkPipelineLayout       SkyViewLayout       = VK_NULL_HANDLE;
    VkPipeline             SkyViewPipeline     = VK_NULL_HANDLE;     // graphics (bakes into SkyViewImage)
    VkPipelineLayout       DomeLayout          = VK_NULL_HANDLE;
    VkPipeline             DomePipeline        = VK_NULL_HANDLE;     // graphics (per-frame, to swapchain)

    bool                   ReadyCondition      = false;             // [-] - True once every resource + pipeline built
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build all LUT images, samplers, descriptor sets, the three bake pipelines, and the per-frame dome pipeline (whose colour attachment matches
// ColourFormat). Loads SPIR-V from ShaderDirectory. Returns false (ReadyCondition left false) on any failure; the caller then skips the sky.
bool InitializeSkyAtmospherePass(SkyAtmospherePass& Pass,
                                 const VulkanHost&  Host,
                                 VkFormat           ColourFormat,
                                 const char*        ShaderDirectory);

// Bake the sun-independent LUTs (transmittance + multi-scatter) once. Submits its own one-shot command buffer on the graphics queue and waits.
// Call once after Initialize. Safe no-op when the pass is not ready.
void BakeSkyAtmosphereConstants(SkyAtmospherePass& Pass, const VulkanHost& Host);

// Replace the atmosphere profile (e.g. from the outliner UI) and flag the sky-view for re-bake. Re-uploads the UBO immediately.
void UpdateSkyAtmosphereProfile(SkyAtmospherePass& Pass, const AtmosphereUniformBlock& Profile);

// Record the frame's sky work into an already-open dynamic-rendering scope: if the sun moved, first re-bake the sky-view table (its own render
// scope, opened and closed here), then draw the fullscreen sky dome with the pushed constants. A no-op when the pass is not ready.
void RecordSkyAtmospherePass(SkyAtmospherePass&      Pass,
                             const VulkanHost&       Host,
                             VkCommandBuffer         CommandBuffer,
                             VkExtent2D              Extent,
                             const SkyDomeConstants& Constants);

// Destroy every resource. Safe on partially-initialized state.
void FinalizeSkyAtmospherePass(SkyAtmospherePass& Pass, const VulkanHost& Host);

} // namespace Frontier

#endif
