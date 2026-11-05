/*==========================================================================================================================================
                                                     HMI VIEWPORT FRAME DECLARATION
                                     ------------------------------------------------------------------
                 Dedicated 1024x640 single-vehicle ReSTIR GI + SVGF + X-Ray Scan preview graph writing
                 into HmiPreviewTexture for the in-cabin dashboard tablet (replacing ForwardRaster).
==========================================================================================================================================*/

#pragma once

#include "EnvironmentUnits.h"
#include "IlluminationUnits.h"
#include "RenderTarget.h"
#include "SubmissionOrder.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

inline constexpr uint32_t HmiPreviewDefaultWidth = 1024;
inline constexpr uint32_t HmiPreviewDefaultHeight = 640;

struct HmiOccupantDescriptor
{
    uint32_t OccupantIndex = 0;
    uint32_t VehicleIdentifier = 0;   // 0 = static studio/world geometry, >= 1 = vehicle slot id
};

struct HmiPreviewFrameOptions
{
    uint32_t HeroVehicleIdentifier = 1;
    bool     IncludeScanOverlay = true;
};

struct HmiPreviewFrameDeclaration
{
    std::vector<SubmissionUnitDeclaration> Units;
    std::vector<RenderTargetDeclaration>   Targets;
};

// Computes the per-occupant exclusion mask for the single-vehicle HMI configurator pass:
// static studio geometry (VehicleIdentifier == 0) and the hero vehicle (HeroVehicleIdentifier) remain visible (0),
// while ALL other vehicles are excluded (1) from rasterization, shadow rays, reflections, and ReSTIR GI.
[[nodiscard]] std::vector<uint8_t> ResolveSingleVehicleExclusionMask(
    const std::vector<HmiOccupantDescriptor>& Occupants,
    uint32_t HeroVehicleIdentifier);

// Declares the 1024x640 ReSTIR GI + SVGF + Environment + VehicleScanOverlay frame graph targeting HmiPreviewTexture.
[[nodiscard]] HmiPreviewFrameDeclaration DeclareHmiPreviewFrame(
    uint32_t Width = HmiPreviewDefaultWidth,
    uint32_t Height = HmiPreviewDefaultHeight,
    HmiPreviewFrameOptions Options = {});

} // namespace Frontier
