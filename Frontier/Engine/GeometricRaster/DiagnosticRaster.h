/*==========================================================================================================================================
                                                   DIAGNOSTIC RASTER
                                     ------------------------------------------------------------------
             Editor & Development ONLY GPU Vulkan 1.3 diagnostic cluster preview & inspection rasterizer.
             Strictly compiled out under FRONTIER_SHIPPING_PROFILE (FRONTIER_EDITOR == 0) so Shipping
             binaries never include debug/preview rasterization code or inspection shaders.
==========================================================================================================================================*/
// 🧩 Editor/Development GPU diagnostic rasterizer: simple sky/atmosphere + 10 cluster/material/glass inspection modes.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"

#if FRONTIER_EDITOR

#include "Engine/DeviceExchange/VulkanComputeExchange.h"
#include "Engine/GeometricRaster/ClusterHierarchy.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

enum class DiagnosticViewMode : uint32_t
{
    ShadedPreview      = 0, // Analytic sky/atmosphere + 3-tier virtualized material + 2-layer glass refraction
    ShadedWireframe    = 1, // ShadedPreview + 1 px barycentric cluster triangle wireframe
    WireframeTopology  = 2, // Dark theme blueprint + tier-colored triangle wireframe
    ClusterIdentity    = 3, // Golden-ratio per-cluster ID palette + translucent cyan glass highlight
    HierarchyTierHeat  = 4, // Finest Tier 0 warm coral-amber -> coarsest root cool slate-blue
    ProjectedErrorHeat = 5, // Screen-space pixel error heatmap relative to BaseErrorThresholdPixels
    MaterialTierHeat   = 6, // FullStrata (cyan-blue) / FilteredToksvig (amber) / CompactProxy (violet)
    SurfaceNormals     = 7, // World-space geometric normals
    TextureCoordinates = 8, // Triplanar/UV checker & mip density
    GlassLayerPeel     = 9, // Opaque behind glass + 1-layer vs 2-layer dielectric glazing peel
};

struct DiagnosticAtmosphereBackdrop
{
    ClusterFloat3 SolarDirectionUnit = {0.48f, 0.72f, 0.50f};
    ClusterFloat3 ZenithSkyRgb       = {0.13f, 0.28f, 0.62f};
    ClusterFloat3 HorizonSkyRgb      = {0.56f, 0.68f, 0.84f};
    float         ExposureScale      = 1.05f;
};

struct DiagnosticFrameExecution
{
    ClusterRuntimeFrameReport CutReport{};
    ClusterMemoryTelemetry    Memory{};
    uint32_t                  UploadedClusterCount  = 0u;
    uint32_t                  UploadedTriangleCount = 0u;
};

class DiagnosticRaster
{
  public:
    bool Initialize(VulkanComputeExchange& Exchange,
                    const std::string&     ShaderBinaryDirectory,
                    std::string*           OutDiagnostic = nullptr);
    void Shutdown(VulkanComputeExchange& Exchange) noexcept;

    bool RenderView(VulkanComputeExchange&              Exchange,
                    const ClusterHierarchy&             Hierarchy,
                    const ClusterCameraView&            Camera,
                    DiagnosticViewMode                  ViewMode,
                    const DiagnosticAtmosphereBackdrop& Backdrop,
                    DiagnosticFrameExecution*           OutExecution = nullptr);

    bool ReadbackLinearRgba(VulkanComputeExchange& Exchange, std::vector<float>& OutLinearRgbaFloats);
    bool ReadbackTonemappedSrgb8(VulkanComputeExchange& Exchange,
                                 float                  ExposureScale,
                                 std::vector<uint8_t>&  OutSrgbRgbaBytes);

    [[nodiscard]] const DeviceImageArraySlot& TargetImage() const noexcept { return TargetImage_; }

  private:
    ComputeProgramSlot   TileBinProgram_{};
    ComputeProgramSlot   ResolveProgram_{};
    DeviceMemorySpan     SceneHeaderSpan_{};
    DeviceMemorySpan     TriangleSpan_{};
    DeviceMemorySpan     TileBinsSpan_{};
    DeviceImageArraySlot TargetImage_{};
};

} // namespace Frontier

#endif // FRONTIER_EDITOR
