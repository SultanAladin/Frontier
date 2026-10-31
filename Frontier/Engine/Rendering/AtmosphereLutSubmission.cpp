/*==========================================================================================================================================
                                            ATMOSPHERE LUT SUBMISSION DEFINITION
==========================================================================================================================================*/
// 🧩 Dirty-gated per-realm RGBA16F Transmittance, Multi-Scattering, and Sky-View 2D-Array compute bakes.

#include "Engine/Rendering/AtmosphereLutSubmission.h"

#include <algorithm>
#include <array>

namespace Frontier
{
namespace
{
struct PushHeader
{
    uint32_t Counts[4];
    float    Tuning[4];
};
} // namespace

bool AtmosphereLutSubmission::Initialize(VulkanComputeSubstrate& Substrate,
                                         const std::string&      ShaderBinaryDirectory,
                                         std::string*            OutDiagnostic)
{
    const std::array<VkDescriptorType, 4> Bindings{{
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    }};

    if (!Substrate.LoadComputeProgram(ShaderBinaryDirectory + "/BakeTransmittanceLut.spv",
                                      "BakeTransmittanceLutMain",
                                      Bindings,
                                      sizeof(PushHeader),
                                      TransmittanceProgram_,
                                      OutDiagnostic))
    {
        return false;
    }
    if (!Substrate.LoadComputeProgram(ShaderBinaryDirectory + "/BakeMultiScatterLut.spv",
                                      "BakeMultiScatterLutMain",
                                      Bindings,
                                      sizeof(PushHeader),
                                      MultiScatterProgram_,
                                      OutDiagnostic))
    {
        return false;
    }
    if (!Substrate.LoadComputeProgram(ShaderBinaryDirectory + "/BakeSkyViewLut.spv",
                                      "BakeSkyViewLutMain",
                                      Bindings,
                                      sizeof(PushHeader),
                                      SkyViewProgram_,
                                      OutDiagnostic))
    {
        return false;
    }
    return true;
}

void AtmosphereLutSubmission::Shutdown(VulkanComputeSubstrate& Substrate) noexcept
{
    Substrate.ReleaseImage(SkyViewArray_);
    Substrate.ReleaseImage(MultiScatteringArray_);
    Substrate.ReleaseImage(TransmittanceArray_);
    Substrate.ReleaseProgram(SkyViewProgram_);
    Substrate.ReleaseProgram(MultiScatterProgram_);
    Substrate.ReleaseProgram(TransmittanceProgram_);
    BakedAtmosphereRevisions_.clear();
    BakedSkyViewRevisions_.clear();
}

bool AtmosphereLutSubmission::Execute(VulkanComputeSubstrate&         Substrate,
                                      const VulkanBufferSlot&         EnsembleBuffer,
                                      const EnvironmentRealmEnsemble& Ensemble)
{
    const uint32_t RealmCount = std::max<uint32_t>(1u, static_cast<uint32_t>(Ensemble.Realms.size()));
    if (TransmittanceArray_.ArrayLayers < RealmCount)
    {
        if (!Substrate.AllocateRgba16fArray(kTransmittanceLutWidth, kTransmittanceLutHeight, RealmCount, TransmittanceArray_) ||
            !Substrate.AllocateRgba16fArray(kMultiScatteringLutDim, kMultiScatteringLutDim, RealmCount, MultiScatteringArray_) ||
            !Substrate.AllocateRgba16fArray(kSkyViewLutWidth, kSkyViewLutHeight, RealmCount, SkyViewArray_))
        {
            return false;
        }
        BakedAtmosphereRevisions_.assign(RealmCount, 0u);
        BakedSkyViewRevisions_.assign(RealmCount, 0u);
    }

    LastAtmosphereBakedCount_ = 0u;
    LastSkyViewBakedCount_    = 0u;

    for (size_t RIdx = 0u; RIdx < Ensemble.Realms.size(); ++RIdx)
    {
        const EnvironmentRealm& Realm      = Ensemble.Realms[RIdx];
        const bool              AtmoDirty  = (BakedAtmosphereRevisions_[RIdx] != Realm.AtmosphereRevision);
        const bool              SkyDirty   = AtmoDirty || (BakedSkyViewRevisions_[RIdx] != Realm.SkyViewRevision);

        if (AtmoDirty)
        {
            PushHeader Push{{kTransmittanceLutWidth, kTransmittanceLutHeight, static_cast<uint32_t>(RIdx), 0u},
                            {0.0f, 0.0f, 0.0f, 0.0f}};
            const std::array<VulkanDescriptorArgument, 4> TransArgs{{
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &EnsembleBuffer, nullptr},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &MultiScatteringArray_},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &SkyViewArray_},
                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &TransmittanceArray_},
            }};
            if (!Substrate.DispatchCompute(TransmittanceProgram_,
                                           TransArgs,
                                           &Push,
                                           sizeof(Push),
                                           (kTransmittanceLutWidth + 7u) / 8u,
                                           (kTransmittanceLutHeight + 7u) / 8u,
                                           1u))
            {
                return false;
            }

            Push.Counts[0] = kMultiScatteringLutDim;
            Push.Counts[1] = kMultiScatteringLutDim;
            const std::array<VulkanDescriptorArgument, 4> MsArgs{{
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &EnsembleBuffer, nullptr},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &TransmittanceArray_},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &SkyViewArray_},
                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &MultiScatteringArray_},
            }};
            if (!Substrate.DispatchCompute(MultiScatterProgram_,
                                           MsArgs,
                                           &Push,
                                           sizeof(Push),
                                           (kMultiScatteringLutDim + 7u) / 8u,
                                           (kMultiScatteringLutDim + 7u) / 8u,
                                           1u))
            {
                return false;
            }

            BakedAtmosphereRevisions_[RIdx] = Realm.AtmosphereRevision;
            ++LastAtmosphereBakedCount_;
        }

        if (SkyDirty)
        {
            PushHeader Push{{kSkyViewLutWidth, kSkyViewLutHeight, static_cast<uint32_t>(RIdx), 0u},
                            {0.0f, 0.0f, 0.0f, 0.0f}};
            const std::array<VulkanDescriptorArgument, 4> SkyArgs{{
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &EnsembleBuffer, nullptr},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &TransmittanceArray_},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &MultiScatteringArray_},
                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &SkyViewArray_},
            }};
            if (!Substrate.DispatchCompute(SkyViewProgram_,
                                           SkyArgs,
                                           &Push,
                                           sizeof(Push),
                                           (kSkyViewLutWidth + 7u) / 8u,
                                           (kSkyViewLutHeight + 7u) / 8u,
                                           1u))
            {
                return false;
            }

            BakedSkyViewRevisions_[RIdx] = Realm.SkyViewRevision;
            ++LastSkyViewBakedCount_;
        }
    }

    return true;
}

bool AtmosphereLutSubmission::ReadbackTransmittanceLayer(VulkanComputeSubstrate& Substrate,
                                                         uint32_t                RealmIndex,
                                                         std::vector<float>&     OutRgba)
{
    return Substrate.DownloadRgba16fLayer(TransmittanceArray_, RealmIndex, OutRgba);
}

bool AtmosphereLutSubmission::ReadbackMultiScatteringLayer(VulkanComputeSubstrate& Substrate,
                                                           uint32_t                RealmIndex,
                                                           std::vector<float>&     OutRgba)
{
    return Substrate.DownloadRgba16fLayer(MultiScatteringArray_, RealmIndex, OutRgba);
}

bool AtmosphereLutSubmission::ReadbackSkyViewLayer(VulkanComputeSubstrate& Substrate,
                                                   uint32_t                RealmIndex,
                                                   std::vector<float>&     OutRgba)
{
    return Substrate.DownloadRgba16fLayer(SkyViewArray_, RealmIndex, OutRgba);
}

} // namespace Frontier
