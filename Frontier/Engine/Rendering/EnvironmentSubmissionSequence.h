/*==========================================================================================================================================
                                             ENVIRONMENT SUBMISSION SEQUENCE
                                     ------------------------------------------------------------------
             End-to-end multi-realm & portal-ready GPU coordinator orchestrating all 5 systems:
             1. AtmosphereLutSubmission  2. CelestialDomeRasterization  3. VolumetricCloudSequence
             4. OpticalLensFlareSequence 5. VolumetricFogSubmission     + Portal G-Buffer Routing.
==========================================================================================================================================*/
// 🧩 Top-level GPU environment & celestial frame coordinator with HDR readback and ACES filmic tone-mapping.

#pragma once

#include "Engine/Rendering/AtmosphereLutSubmission.h"
#include "Engine/Rendering/CelestialDomeRasterization.h"
#include "Engine/Rendering/EnvironmentRealmEnsemble.h"
#include "Engine/Rendering/OpticalLensFlareSequence.h"
#include "Engine/Rendering/VolumetricCloudSequence.h"
#include "Engine/Rendering/VolumetricFogSubmission.h"
#include "Engine/Rendering/VulkanComputeSubstrate.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Frontier
{

class EnvironmentSubmissionSequence
{
  public:
    bool Initialize(VulkanComputeSubstrate& Substrate,
                    const std::string&      ShaderBinaryDirectory,
                    const std::string&      LunaSourceJpgPath = "EngineContent/EnvironmentArchives/CelestialMaps/MoonAlbedoSource2048.jpg",
                    const std::string&      HipparcosCsvPath  = "EngineContent/EnvironmentArchives/CelestialMaps/Hipparcos8.csv",
                    std::string*            OutDiagnostic     = nullptr);
    void Shutdown(VulkanComputeSubstrate& Substrate) noexcept;

    bool ExecuteFrame(VulkanComputeSubstrate&         Substrate,
                      const EnvironmentRealmEnsemble& Ensemble,
                      const EnvironmentCameraView&    CameraView);

    bool ReadbackHdrFrame(VulkanComputeSubstrate& Substrate, std::vector<float>& OutLinearRgbaFloats);
    bool ReadbackGbufferLayer(VulkanComputeSubstrate& Substrate, uint32_t LayerIndex, std::vector<float>& OutRgbaFloats);

    [[nodiscard]] AtmosphereLutSubmission&          AtmosphereLuts() noexcept { return AtmosphereLuts_; }
    [[nodiscard]] const AtmosphereLutSubmission&    AtmosphereLuts() const noexcept { return AtmosphereLuts_; }
    [[nodiscard]] CelestialDomeRasterization&       CelestialDome() noexcept { return CelestialDome_; }
    [[nodiscard]] const CelestialDomeRasterization& CelestialDome() const noexcept { return CelestialDome_; }
    [[nodiscard]] VolumetricCloudSequence&          Clouds() noexcept { return Clouds_; }
    [[nodiscard]] const VolumetricCloudSequence&    Clouds() const noexcept { return Clouds_; }
    [[nodiscard]] VolumetricFogSubmission&          Fog() noexcept { return Fog_; }
    [[nodiscard]] const VolumetricFogSubmission&    Fog() const noexcept { return Fog_; }
    [[nodiscard]] OpticalLensFlareSequence&         LensFlare() noexcept { return LensFlare_; }
    [[nodiscard]] const OpticalLensFlareSequence&   LensFlare() const noexcept { return LensFlare_; }
    [[nodiscard]] const PackedEnsembleGpuPayload&   LastPackedPayload() const noexcept { return LastPayload_; }

  private:
    VulkanComputeProgram       GbufferProgram_{};
    VulkanBufferSlot           EnsembleBuffer_{};
    VulkanImageSlot            GbufferArray_{};
    VulkanImageSlot            HdrFrameTarget_{};
    AtmosphereLutSubmission    AtmosphereLuts_{};
    CelestialDomeRasterization CelestialDome_{};
    VolumetricCloudSequence    Clouds_{};
    VolumetricFogSubmission    Fog_{};
    OpticalLensFlareSequence   LensFlare_{};
    PackedEnsembleGpuPayload   LastPayload_{};
};

// ACES filmic tone-mapping + linear-to-sRGB8 conversion for PNG showcase presentation.
void TonemapHdrToSrgb8(std::span<const float> LinearRgbaFloats,
                       uint32_t               Width,
                       uint32_t               Height,
                       float                  ExposureScale,
                       std::vector<uint8_t>&  OutSrgbRgbaBytes);

} // namespace Frontier
