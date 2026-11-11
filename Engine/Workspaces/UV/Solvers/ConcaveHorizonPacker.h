/*============================================================================================================================================
                                                          CONCAVEHORIZONPACKER.H
============================================================================================================================================*/
// 🧩 Priority-Layer-Weighted Concave Skyline Horizon Packer accelerated by ConcaveHorizonCompute.comp.spv & ExecutionChannels

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Configuration/WorkspaceConfiguration.h"
#include "Engine/GeometricRaster/Backend/VulkanDeviceContext.h"
#include "Engine/GeometricRaster/Compute/ComputeDispatchEncoder.h"
#include "Engine/GeometrySpace/Network/FaceGroupReference.h"
#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/Threading/Tasks/ExecutionChannels.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace Frontier::Workspaces
{

struct ConcavePackReport
{
    uint32_t IslandsPacked         = 0u;     // [count] - Total islands placed without overlap
    uint32_t UDIMTilesOccupied     = 1u;     // [count] - Number of UDIM tiles populated (1 = 1001 only, >1 = multi-UDIM)
    float    PrimaryTileEfficiency = 0.0f;   // [0..1]  - Fraction of UDIM 1001 covered by UV island area
    bool     EvaluatedOnVulkanGPU  = false;  // [-]     - True when candidate horizons were scored on Vulkan
    double   ElapsedMilliseconds   = 0.0;    // [ms]    - End-to-end concave skyline packing duration
    std::vector<uint32_t> ActiveUDIMNumbers {}; // [code] - Sorted list of occupied UDIM codes (1001, 1002, ...)
};

class ConcaveHorizonPacker
{
public:
    ConcaveHorizonPacker() = default;
    ~ConcaveHorizonPacker();

    ConcaveHorizonPacker(const ConcaveHorizonPacker&)            = delete;
    ConcaveHorizonPacker& operator=(const ConcaveHorizonPacker&) = delete;

    [[nodiscard]] bool InitializeGPUKernel(
        GeometricRaster::VulkanDeviceContext* Context,
        const std::filesystem::path&          CompiledShaderDir);

    void TeardownGPUKernel() noexcept;

    // Packs all islands into UDIM 1001 (or across multiple UDIMs when AllowMultiUDIMOverflow == true),
    // weighting each layer's target share (e.g., 55%, 25%, Remainder 20%) and scoring concave horizons on GPU.
    ConcavePackReport PackConcaveSkyline(
        GeometrySpace::PolygonNetwork&               Network,
        const GeometrySpace::PriorityLayerRegistry&  LayerRegistry,
        const Configuration::UVWorkspacePreferences& Preferences,
        bool                                         AllowMultiUDIMOverflow = false,
        Threading::ExecutionChannels*                OptionalChannels       = nullptr);

private:
    GeometricRaster::VulkanDeviceContext*   VkContext            = nullptr;
    GeometricRaster::ComputeDispatchEncoder Encoder              {};
    uint32_t                                HorizonProgramHandle = UINT32_MAX;
};

} // namespace Frontier::Workspaces
