/*==========================================================================================================================================
                                          ENVIRONMENT SUBMISSION SEQUENCE DEFINITION
==========================================================================================================================================*/
// 🧩 Executes all 5 environment/celestial GPU systems + cross-realm portal routing in dependency order.

#include "Engine/Rendering/EnvironmentSubmissionSequence.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier
{
namespace
{
struct PushHeader
{
    uint32_t Counts[4];
    float    Tuning[4];
};

inline uint8_t LinearToSrgbByte(float Linear) noexcept
{
    const float C = std::clamp(Linear, 0.0f, 1.0f);
    const float S = (C <= 0.0031308f) ? (12.92f * C) : (1.055f * std::pow(C, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::lround(S * 255.0f), 0l, 255l));
}

inline float AcesFilmicChannel(float X) noexcept
{
    const float V = std::max(0.0f, X);
    return std::clamp((V * (2.51f * V + 0.03f)) / (V * (2.43f * V + 0.59f) + 0.14f), 0.0f, 1.0f);
}
} // namespace

bool EnvironmentSubmissionSequence::Initialize(VulkanComputeSubstrate& Substrate,
                                               const std::string&      ShaderBinaryDirectory,
                                               const std::string&      LunaSourceJpgPath,
                                               const std::string&      HipparcosCsvPath,
                                               std::string*            OutDiagnostic)
{
    const std::array<VkDescriptorType, 4> GbBindings{{
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // Ensemble
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // TransLut
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // CloudShadowLut
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // GbufferOut (4 layers)
    }};

    if (!Substrate.LoadComputeProgram(ShaderBinaryDirectory + "/RasterizeRealmGbuffer.spv",
                                      "RasterizeRealmGbufferMain",
                                      GbBindings,
                                      sizeof(PushHeader),
                                      GbufferProgram_,
                                      OutDiagnostic))
    {
        return false;
    }

    if (!AtmosphereLuts_.Initialize(Substrate, ShaderBinaryDirectory, OutDiagnostic) ||
        !CelestialDome_.Initialize(Substrate, ShaderBinaryDirectory, LunaSourceJpgPath, HipparcosCsvPath, OutDiagnostic) ||
        !Clouds_.Initialize(Substrate, ShaderBinaryDirectory, OutDiagnostic) ||
        !Fog_.Initialize(Substrate, ShaderBinaryDirectory, OutDiagnostic) ||
        !LensFlare_.Initialize(Substrate, ShaderBinaryDirectory, OutDiagnostic))
    {
        return false;
    }

    return Substrate.AllocateRgba16fArray(8u, 8u, 4u, GbufferArray_) &&
           Substrate.AllocateRgba16fArray(8u, 8u, 1u, HdrFrameTarget_);
}

void EnvironmentSubmissionSequence::Shutdown(VulkanComputeSubstrate& Substrate) noexcept
{
    LensFlare_.Shutdown(Substrate);
    Fog_.Shutdown(Substrate);
    Clouds_.Shutdown(Substrate);
    CelestialDome_.Shutdown(Substrate);
    AtmosphereLuts_.Shutdown(Substrate);
    Substrate.ReleaseImage(HdrFrameTarget_);
    Substrate.ReleaseImage(GbufferArray_);
    Substrate.ReleaseBuffer(EnsembleBuffer_);
    Substrate.ReleaseProgram(GbufferProgram_);
    LastPayload_ = PackedEnsembleGpuPayload{};
}

bool EnvironmentSubmissionSequence::ExecuteFrame(VulkanComputeSubstrate&         Substrate,
                                                 const EnvironmentRealmEnsemble& Ensemble,
                                                 const EnvironmentCameraView&    CameraView)
{
    const uint32_t W = std::max(1u, CameraView.ViewportWidth);
    const uint32_t H = std::max(1u, CameraView.ViewportHeight);

    if (GbufferArray_.Width != W || GbufferArray_.Height != H)
    {
        if (!Substrate.AllocateRgba16fArray(W, H, 4u, GbufferArray_) ||
            !Substrate.AllocateRgba16fArray(W, H, 1u, HdrFrameTarget_))
        {
            return false;
        }
    }

    LastPayload_ = PackEnsembleGpuPayload(Ensemble, CameraView, CelestialDome_.IndexedStarCount());
    if (!Substrate.UploadBuffer(EnsembleBuffer_,
                                LastPayload_.Float4Slots.data(),
                                LastPayload_.Float4Slots.size() * sizeof(float) * 4u))
    {
        return false;
    }

    // 1. Dirty-gated per-realm Transmittance (256x64xR), Multi-Scattering (32x32xR), and Sky-View (192x108xR) LUTs
    if (!AtmosphereLuts_.Execute(Substrate, EnsembleBuffer_, Ensemble))
    {
        return false;
    }

    // 2. Orthographic 256x256xR CloudShadowProjection per realm (sampled by surface direct lighting + volumetric fog)
    if (!Clouds_.ExecuteShadowProjections(Substrate, EnsembleBuffer_, AtmosphereLuts_, Ensemble, GbufferArray_))
    {
        return false;
    }

    // 3. Multi-realm & cross-portal G-buffer + reversed-Z depth + cloud-shadowed surface lighting + glass transmission
    {
        const PushHeader Push{{W, H, CameraView.FrameIndex, 0u}, {0.0f, 0.0f, 0.0f, 0.0f}};
        const std::array<VulkanDescriptorArgument, 4> GbArgs{{
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &EnsembleBuffer_, nullptr},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &AtmosphereLuts_.TransmittanceArray()},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &Clouds_.CloudShadowArray()},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &GbufferArray_},
        }};
        if (!Substrate.DispatchCompute(GbufferProgram_, GbArgs, &Push, sizeof(Push), (W + 7u) / 8u, (H + 7u) / 8u, 1u))
        {
            return false;
        }
    }

    // 4. Celestial Dome (sky + twilight + Hipparcos twinkling stars + 2K 6-body lunar atlas + ringed planets + suns)
    if (!CelestialDome_.Execute(Substrate, EnsembleBuffer_, AtmosphereLuts_, GbufferArray_, HdrFrameTarget_, W, H))
    {
        return false;
    }

    // 5. Volumetric Clouds (3D Perlin-Worley global slabs + bounded local boxes + realm/depth bilateral temporal filter)
    if (!Clouds_.ExecuteTraceAndReconstruct(
            Substrate, EnsembleBuffer_, AtmosphereLuts_, GbufferArray_, HdrFrameTarget_, W, H, CameraView.FrameIndex))
    {
        return false;
    }

    // 6. Volumetric Fog (analytic height fog + planet-curved aerial perspective + 3D froxel fog + sun shafts + IES + lightning + fire)
    if (!Fog_.Execute(Substrate, EnsembleBuffer_, AtmosphereLuts_, Clouds_, GbufferArray_, HdrFrameTarget_, W, H))
    {
        return false;
    }

    // 7. Optical Lens Flare (1x1-per-source GPU visibility reduction + chromatic ghosts + halo + streaks + aperture starburst)
    const auto FlareCount = static_cast<uint32_t>(LastPayload_.FlareCandidates.size());
    if (!LensFlare_.Execute(Substrate, EnsembleBuffer_, GbufferArray_, Clouds_, HdrFrameTarget_, FlareCount, W, H))
    {
        return false;
    }

    return true;
}

bool EnvironmentSubmissionSequence::ReadbackHdrFrame(VulkanComputeSubstrate& Substrate, std::vector<float>& OutLinearRgbaFloats)
{
    return Substrate.DownloadRgba16fLayer(HdrFrameTarget_, 0u, OutLinearRgbaFloats);
}

bool EnvironmentSubmissionSequence::ReadbackGbufferLayer(VulkanComputeSubstrate& Substrate,
                                                         uint32_t                LayerIndex,
                                                         std::vector<float>&     OutRgbaFloats)
{
    return Substrate.DownloadRgba16fLayer(GbufferArray_, LayerIndex, OutRgbaFloats);
}

void TonemapHdrToSrgb8(std::span<const float> LinearRgbaFloats,
                       uint32_t               Width,
                       uint32_t               Height,
                       float                  ExposureScale,
                       std::vector<uint8_t>&  OutSrgbRgbaBytes)
{
    const size_t PixelCount = static_cast<size_t>(Width) * Height;
    OutSrgbRgbaBytes.resize(PixelCount * 4u, 255u);
    if (LinearRgbaFloats.size() < PixelCount * 4u)
    {
        return;
    }
    for (size_t I = 0u; I < PixelCount; ++I)
    {
        const float R                 = AcesFilmicChannel(LinearRgbaFloats[I * 4u + 0u] * ExposureScale);
        const float G                 = AcesFilmicChannel(LinearRgbaFloats[I * 4u + 1u] * ExposureScale);
        const float B                 = AcesFilmicChannel(LinearRgbaFloats[I * 4u + 2u] * ExposureScale);
        OutSrgbRgbaBytes[I * 4u + 0u] = LinearToSrgbByte(R);
        OutSrgbRgbaBytes[I * 4u + 1u] = LinearToSrgbByte(G);
        OutSrgbRgbaBytes[I * 4u + 2u] = LinearToSrgbByte(B);
        OutSrgbRgbaBytes[I * 4u + 3u] = 255u;
    }
}

} // namespace Frontier
