/*==========================================================================================================================================
                                            SURFACE CARD RADIOSITY DEFINITION
==========================================================================================================================================*/
// 🧩 GPU Vulkan compute & CPU Jacobi 6-axis surface-card multi-bounce radiosity implementation.

#include "Engine/Rendering/SurfaceCardRadiosity.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier
{
namespace
{

struct alignas(16) CardRadiosityPushGpu
{
    uint32_t CardCount             = 0u;
    uint32_t LuminaireCount        = 0u;
    float    IndirectBounceGain    = 1.0f;
    float    SkyVisibilityScale    = 1.0f;
    float    SunDirection[3]       = {0.42f, 0.78f, 0.46f};
    float    SunIlluminanceLux     = 4.5f;
    float    SunRadianceTint[3]    = {1.0f, 0.96f, 0.90f};
    float    SkyAmbientLuminance   = 0.35f;
    float    SkyZenithRadiance[3]  = {0.26f, 0.44f, 0.82f};
    float    Padding0              = 0.0f;
    float    SkyHorizonRadiance[3] = {0.62f, 0.68f, 0.78f};
    uint32_t EnableVisibilityCones = 1u;
};

} // namespace

bool SurfaceCardRadiositySolver::Initialize(VulkanComputeExchange& Device,
                                      const std::string&     ShaderSpvDirectory,
                                      std::string*           OutDiagnostic)
{
    Shutdown(Device);
    if (!Device.Ready())
    {
        return false;
    }

    const std::array<VkDescriptorType, 6> Bindings = {
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // gCardDescriptors
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // gDistanceVolume
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // gLuminaires
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // gSourceExitance
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // gDirectExitance
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // gTargetExitance
    };

    if (!Device.LoadComputeProgram(ShaderSpvDirectory + "/InjectCardDirectIrradiance.spv",
                                   "InjectDirectCardRadianceMain",
                                   Bindings,
                                   sizeof(CardRadiosityPushGpu),
                                   InjectProgram_,
                                   OutDiagnostic) ||
        !Device.LoadComputeProgram(ShaderSpvDirectory + "/IterateCardRadiosityBounce.spv",
                                   "IterateCardRadiosityMain",
                                   Bindings,
                                   sizeof(CardRadiosityPushGpu),
                                   IterateProgram_,
                                   OutDiagnostic))
    {
        Shutdown(Device);
        return false;
    }

    // Seed non-empty 1-card neutral buffers so descriptor sets are valid even when SurfaceCards.Enabled == false.
    const std::array<float, 16> ZeroCard{};
    if (!Device.UploadMemorySpan(DescriptorSpan_, ZeroCard.data(), sizeof(ZeroCard)) ||
        !Device.AllocateMemorySpan(sizeof(float) * 4u, DirectExitanceSpan_) ||
        !Device.AllocateMemorySpan(sizeof(float) * 4u, PingSpanA_) ||
        !Device.AllocateMemorySpan(sizeof(float) * 4u, PingSpanB_))
    {
        Shutdown(Device);
        return false;
    }

    ActiveCardCount_ = 0u;
    ActivePingIndex_ = 0u;
    return true;
}

void SurfaceCardRadiositySolver::Shutdown(VulkanComputeExchange& Device) noexcept
{
    Device.ReleaseProgram(InjectProgram_);
    Device.ReleaseProgram(IterateProgram_);
    Device.ReleaseMemorySpan(DescriptorSpan_);
    Device.ReleaseMemorySpan(DirectExitanceSpan_);
    Device.ReleaseMemorySpan(PingSpanA_);
    Device.ReleaseMemorySpan(PingSpanB_);
    ActiveCardCount_ = 0u;
    ActivePingIndex_ = 0u;
}

void SurfaceCardRadiositySolver::AppendSixAxisObjectCards(std::array<float, 3>           BoundsMin,
                                                    std::array<float, 3>           BoundsMax,
                                                    std::array<float, 3>           DiffuseReflectance,
                                                    std::array<float, 3>           SelfEmission,
                                                    uint32_t                       CardsPerObject,
                                                    std::vector<SurfaceCardPatch>& InOutPatches)
{
    const float Dx = std::max(BoundsMax[0] - BoundsMin[0], 0.10f);
    const float Dy = std::max(BoundsMax[1] - BoundsMin[1], 0.10f);
    const float Dz = std::max(BoundsMax[2] - BoundsMin[2], 0.10f);
    const float Cx = 0.5f * (BoundsMin[0] + BoundsMax[0]);
    const float Cy = 0.5f * (BoundsMin[1] + BoundsMax[1]);
    const float Cz = 0.5f * (BoundsMin[2] + BoundsMax[2]);
    const float R  = std::max(1.25f, 1.35f * std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz));

    const std::array<SurfaceCardPatch, 6> Faces = {{
        {{BoundsMax[0], Cy, Cz}, Dy * Dz, { 1.0f,  0.0f,  0.0f}, 0.72f, DiffuseReflectance, R, SelfEmission},
        {{BoundsMin[0], Cy, Cz}, Dy * Dz, {-1.0f,  0.0f,  0.0f}, 0.72f, DiffuseReflectance, R, SelfEmission},
        {{Cx, BoundsMax[1], Cz}, Dx * Dz, { 0.0f,  1.0f,  0.0f}, 1.00f, DiffuseReflectance, R, SelfEmission},
        {{Cx, BoundsMin[1], Cz}, Dx * Dz, { 0.0f, -1.0f,  0.0f}, 0.18f, DiffuseReflectance, R, SelfEmission},
        {{Cx, Cy, BoundsMax[2]}, Dx * Dy, { 0.0f,  0.0f,  1.0f}, 0.72f, DiffuseReflectance, R, SelfEmission},
        {{Cx, Cy, BoundsMin[2]}, Dx * Dy, { 0.0f,  0.0f, -1.0f}, 0.72f, DiffuseReflectance, R, SelfEmission},
    }};

    const uint32_t Count = std::clamp(CardsPerObject, 1u, 6u);
    for (uint32_t I = 0u; I < Count; ++I)
    {
        InOutPatches.push_back(Faces[I]);
    }
}

void SurfaceCardRadiositySolver::AppendGroundRadiosityGrid(std::array<float, 2>           MinXZ,
                                                     std::array<float, 2>           MaxXZ,
                                                     float                          GroundY,
                                                     uint32_t                       SubdivisionsPerAxis,
                                                     std::array<float, 3>           GroundReflectance,
                                                     std::vector<SurfaceCardPatch>& InOutPatches)
{
    const uint32_t Sub   = std::clamp(SubdivisionsPerAxis, 1u, 8u);
    const float    StepX = std::max(MaxXZ[0] - MinXZ[0], 1.0f) / static_cast<float>(Sub);
    const float    StepZ = std::max(MaxXZ[1] - MinXZ[1], 1.0f) / static_cast<float>(Sub);
    const float    Area  = StepX * StepZ;
    const float    Rad   = std::max(2.5f, 1.25f * std::sqrt(StepX * StepX + StepZ * StepZ));

    for (uint32_t Z = 0u; Z < Sub; ++Z)
    {
        for (uint32_t X = 0u; X < Sub; ++X)
        {
            SurfaceCardPatch Patch{};
            Patch.WorldCenter        = {MinXZ[0] + (static_cast<float>(X) + 0.5f) * StepX,
                                        GroundY + 0.02f,
                                        MinXZ[1] + (static_cast<float>(Z) + 0.5f) * StepZ};
            Patch.SurfaceAreaM2      = Area;
            Patch.WorldNormal        = {0.0f, 1.0f, 0.0f};
            Patch.SkyVisibility      = 1.0f;
            Patch.DiffuseReflectance = GroundReflectance;
            Patch.InfluenceRadiusM   = Rad;
            Patch.SelfEmission       = {0.0f, 0.0f, 0.0f};
            InOutPatches.push_back(Patch);
        }
    }
}

bool SurfaceCardRadiositySolver::UploadPatches(VulkanComputeExchange& Device, std::span<const SurfaceCardPatch> Patches)
{
    if (!Device.Ready())
    {
        return false;
    }

    if (Patches.empty())
    {
        ActiveCardCount_ = 0u;
        const std::array<float, 4> ZeroExit = {0.0f, 0.0f, 0.0f, 0.0f};
        (void)Device.UploadMemorySpan(PingSpanA_, ZeroExit.data(), sizeof(ZeroExit));
        (void)Device.UploadMemorySpan(PingSpanB_, ZeroExit.data(), sizeof(ZeroExit));
        return true;
    }

    std::vector<std::array<float, 4>> Packed;
    Packed.reserve(Patches.size() * 4u);
    for (const SurfaceCardPatch& P : Patches)
    {
        Packed.push_back({P.WorldCenter[0], P.WorldCenter[1], P.WorldCenter[2], std::max(P.SurfaceAreaM2, 0.02f)});
        Packed.push_back({P.WorldNormal[0], P.WorldNormal[1], P.WorldNormal[2], std::clamp(P.SkyVisibility, 0.0f, 1.0f)});
        Packed.push_back({std::clamp(P.DiffuseReflectance[0], 0.0f, 0.95f),
                          std::clamp(P.DiffuseReflectance[1], 0.0f, 0.95f),
                          std::clamp(P.DiffuseReflectance[2], 0.0f, 0.95f),
                          std::max(P.InfluenceRadiusM, 0.75f)});
        Packed.push_back({P.SelfEmission[0], P.SelfEmission[1], P.SelfEmission[2], 0.0f});
    }

    const VkDeviceSize ExitBytes = static_cast<VkDeviceSize>(Patches.size()) * sizeof(float) * 4u;
    if (!Device.UploadMemorySpan(DescriptorSpan_, Packed.data(), Packed.size() * sizeof(float) * 4u) ||
        !Device.AllocateMemorySpan(ExitBytes, DirectExitanceSpan_) ||
        !Device.AllocateMemorySpan(ExitBytes, PingSpanA_) ||
        !Device.AllocateMemorySpan(ExitBytes, PingSpanB_))
    {
        return false;
    }

    ActiveCardCount_ = static_cast<uint32_t>(Patches.size());
    ActivePingIndex_ = 0u;
    return true;
}

bool SurfaceCardRadiositySolver::SolveGpuRadiosity(VulkanComputeExchange&               Device,
                                             const DeviceMemorySpan&              DistanceVolumeSpan,
                                             const DeviceMemorySpan&              LuminairesSpan,
                                             uint32_t                             LuminaireCount,
                                             const SurfaceCardLightingParameters& Parameters,
                                             SurfaceCardSolveReport*              OutReport)
{
    if (OutReport)
    {
        *OutReport           = SurfaceCardSolveReport{};
        OutReport->CardCount = ActiveCardCount_;
    }
    if (ActiveCardCount_ == 0u)
    {
        return true;
    }

    CardRadiosityPushGpu Push{};
    Push.CardCount             = ActiveCardCount_;
    Push.LuminaireCount        = LuminaireCount;
    Push.IndirectBounceGain    = Parameters.IndirectBounceGain;
    Push.SkyVisibilityScale    = Parameters.SkyVisibilityWeight;
    Push.SunDirection[0]       = Parameters.SunDirectionUnit[0];
    Push.SunDirection[1]       = Parameters.SunDirectionUnit[1];
    Push.SunDirection[2]       = Parameters.SunDirectionUnit[2];
    Push.SunIlluminanceLux     = Parameters.SunIlluminanceLux;
    Push.SunRadianceTint[0]    = Parameters.SunRadianceTint[0];
    Push.SunRadianceTint[1]    = Parameters.SunRadianceTint[1];
    Push.SunRadianceTint[2]    = Parameters.SunRadianceTint[2];
    Push.SkyAmbientLuminance   = Parameters.SkyAmbientLuminance;
    Push.SkyZenithRadiance[0]  = Parameters.SkyZenithRadiance[0];
    Push.SkyZenithRadiance[1]  = Parameters.SkyZenithRadiance[1];
    Push.SkyZenithRadiance[2]  = Parameters.SkyZenithRadiance[2];
    Push.SkyHorizonRadiance[0] = Parameters.SkyHorizonRadiance[0];
    Push.SkyHorizonRadiance[1] = Parameters.SkyHorizonRadiance[1];
    Push.SkyHorizonRadiance[2] = Parameters.SkyHorizonRadiance[2];
    Push.EnableVisibilityCones = Parameters.EnableVisibilityCones ? 1u : 0u;

    const uint32_t GroupsX = (ActiveCardCount_ + 63u) / 64u;

    DispatchTimingSample StepTiming{};
    double               TotalGpuMs = 0.0;
    double               TotalCpuMs = 0.0;
    bool                 GpuAvail   = false;

    const std::array<ComputeDescriptorArgument, 6> InjectArgs = {{
        ComputeDescriptorArgument::Span(&DescriptorSpan_),
        ComputeDescriptorArgument::Span(&DistanceVolumeSpan),
        ComputeDescriptorArgument::Span(&LuminairesSpan),
        ComputeDescriptorArgument::Span(&PingSpanB_),
        ComputeDescriptorArgument::Span(&DirectExitanceSpan_),
        ComputeDescriptorArgument::Span(&PingSpanA_),
    }};
    if (!Device.DispatchCompute(InjectProgram_, InjectArgs, &Push, sizeof(Push), GroupsX, 1u, 1u, &StepTiming))
    {
        return false;
    }
    TotalGpuMs += StepTiming.GpuMilliseconds;
    TotalCpuMs += StepTiming.CpuMilliseconds;
    GpuAvail   = GpuAvail || StepTiming.GpuTimestampAvailable;
    ActivePingIndex_ = 0u;

    const uint32_t Bounces = std::min<uint32_t>(Parameters.BounceIterations, 12u);
    for (uint32_t Bounce = 0u; Bounce < Bounces; ++Bounce)
    {
        const DeviceMemorySpan& Src = (ActivePingIndex_ == 0u) ? PingSpanA_ : PingSpanB_;
        const DeviceMemorySpan& Dst = (ActivePingIndex_ == 0u) ? PingSpanB_ : PingSpanA_;

        const std::array<ComputeDescriptorArgument, 6> BounceArgs = {{
            ComputeDescriptorArgument::Span(&DescriptorSpan_),
            ComputeDescriptorArgument::Span(&DistanceVolumeSpan),
            ComputeDescriptorArgument::Span(&LuminairesSpan),
            ComputeDescriptorArgument::Span(&Src),
            ComputeDescriptorArgument::Span(&DirectExitanceSpan_),
            ComputeDescriptorArgument::Span(&Dst),
        }};
        if (!Device.DispatchCompute(IterateProgram_, BounceArgs, &Push, sizeof(Push), GroupsX, 1u, 1u, &StepTiming))
        {
            return false;
        }
        TotalGpuMs += StepTiming.GpuMilliseconds;
        TotalCpuMs += StepTiming.CpuMilliseconds;
        ActivePingIndex_ = 1u - ActivePingIndex_;
    }

    if (OutReport)
    {
        OutReport->CompletedBounces      = Bounces;
        OutReport->GpuMilliseconds       = TotalGpuMs;
        OutReport->CpuMilliseconds       = TotalCpuMs;
        OutReport->GpuTimestampAvailable = GpuAvail;
    }
    return true;
}

bool SurfaceCardRadiositySolver::DownloadSolvedExitance(const VulkanComputeExchange&       Device,
                                                  std::vector<std::array<float, 4>>& OutCardExitance) const
{
    OutCardExitance.assign(ActiveCardCount_, {0.0f, 0.0f, 0.0f, 0.0f});
    if (ActiveCardCount_ == 0u)
    {
        return true;
    }
    return Device.DownloadMemorySpan(
        SolvedExitanceSpan(), OutCardExitance.data(), static_cast<VkDeviceSize>(ActiveCardCount_) * sizeof(float) * 4u);
}

void SurfaceCardRadiositySolver::SolveCpuReferenceRadiosity(std::span<const SurfaceCardPatch>    Patches,
                                                      const SurfaceCardLightingParameters& Parameters,
                                                      std::vector<std::array<float, 3>>&   OutDirectExitance,
                                                      std::vector<std::array<float, 3>>&   OutSolvedExitance,
                                                      std::vector<float>*                  OutBounceResiduals)
{
    const size_t N = Patches.size();
    OutDirectExitance.assign(N, {0.0f, 0.0f, 0.0f});
    OutSolvedExitance.assign(N, {0.0f, 0.0f, 0.0f});
    if (OutBounceResiduals)
    {
        OutBounceResiduals->clear();
    }
    if (N == 0u)
    {
        return;
    }

    auto Normalize = [](std::array<float, 3> V) -> std::array<float, 3> {
        const float L = std::sqrt(V[0] * V[0] + V[1] * V[1] + V[2] * V[2]);
        return (L > 1.0e-8f) ? std::array<float, 3>{V[0] / L, V[1] / L, V[2] / L} : std::array<float, 3>{0.0f, 1.0f, 0.0f};
    };

    const std::array<float, 3> SunDir = Normalize(Parameters.SunDirectionUnit);

    for (size_t I = 0u; I < N; ++I)
    {
        const SurfaceCardPatch&    P    = Patches[I];
        const std::array<float, 3> Norm = Normalize(P.WorldNormal);
        const float                NdotS = std::max(0.0f, Norm[0] * SunDir[0] + Norm[1] * SunDir[1] + Norm[2] * SunDir[2]);
        std::array<float, 3>       Irr   = {0.0f, 0.0f, 0.0f};

        if (NdotS > 0.0f && Parameters.SunIlluminanceLux > 0.0f)
        {
            for (int C = 0; C < 3; ++C)
            {
                Irr[C] += Parameters.SunRadianceTint[C] * Parameters.SunIlluminanceLux * NdotS;
            }
        }
        if (Parameters.SkyAmbientLuminance > 0.0f)
        {
            const float Up   = std::clamp(Norm[1] * 0.5f + 0.5f, 0.0f, 1.0f);
            const float Mult = 3.14159265f * Parameters.SkyAmbientLuminance *
                               std::clamp(P.SkyVisibility, 0.0f, 1.0f) * Parameters.SkyVisibilityWeight;
            for (int C = 0; C < 3; ++C)
            {
                const float SkyCol = Parameters.SkyHorizonRadiance[C] * (1.0f - Up) + Parameters.SkyZenithRadiance[C] * Up;
                Irr[C] += SkyCol * Mult;
            }
        }

        for (int C = 0; C < 3; ++C)
        {
            const float RhoOverPi   = std::clamp(P.DiffuseReflectance[C], 0.0f, 0.95f) * 0.318309886f;
            OutDirectExitance[I][C] = P.SelfEmission[C] + RhoOverPi * Irr[C];
        }
    }

    OutSolvedExitance = OutDirectExitance;
    std::vector<std::array<float, 3>> Next(N, {0.0f, 0.0f, 0.0f});
    const float                       Gain = std::clamp(Parameters.IndirectBounceGain, 0.0f, 1.35f);

    for (uint32_t Bounce = 0u; Bounce < std::min<uint32_t>(Parameters.BounceIterations, 12u); ++Bounce)
    {
        float MaxResidual = 0.0f;
        for (size_t I = 0u; I < N; ++I)
        {
            const SurfaceCardPatch&    Pi    = Patches[I];
            const std::array<float, 3> NormI = Normalize(Pi.WorldNormal);
            std::array<float, 3>       Sum   = {0.0f, 0.0f, 0.0f};

            for (size_t J = 0u; J < N; ++J)
            {
                if (I == J)
                {
                    continue;
                }
                const SurfaceCardPatch& Pj = Patches[J];
                const float             Dx = Pj.WorldCenter[0] - Pi.WorldCenter[0];
                const float             Dy = Pj.WorldCenter[1] - Pi.WorldCenter[1];
                const float             Dz = Pj.WorldCenter[2] - Pi.WorldCenter[2];
                const float             D2 = Dx * Dx + Dy * Dy + Dz * Dz;
                if (D2 <= 1.0e-4f || D2 > 6400.0f)
                {
                    continue;
                }
                const float                D     = std::sqrt(D2);
                const std::array<float, 3> Omega = {Dx / D, Dy / D, Dz / D};
                const std::array<float, 3> NormJ = Normalize(Pj.WorldNormal);
                const float                CosI  = std::max(0.0f, NormI[0] * Omega[0] + NormI[1] * Omega[1] + NormI[2] * Omega[2]);
                const float                CosJ  = std::max(0.0f, -(NormJ[0] * Omega[0] + NormJ[1] * Omega[1] + NormJ[2] * Omega[2]));
                if (CosI <= 1.0e-4f || CosJ <= 1.0e-4f)
                {
                    continue;
                }
                const float AreaJ = std::max(Pj.SurfaceAreaM2, 0.02f);
                const float Fij   = std::min(0.22f, (CosI * CosJ * AreaJ) / (3.14159265f * (D2 + AreaJ * 0.318309886f)));
                for (int C = 0; C < 3; ++C)
                {
                    Sum[C] += OutSolvedExitance[J][C] * Fij;
                }
            }

            for (int C = 0; C < 3; ++C)
            {
                const float Rho = std::clamp(Pi.DiffuseReflectance[C], 0.0f, 0.92f);
                Next[I][C]      = std::max(0.0f, OutDirectExitance[I][C] + Gain * Rho * Sum[C]);
                MaxResidual     = std::max(MaxResidual, std::abs(Next[I][C] - OutSolvedExitance[I][C]));
            }
        }
        OutSolvedExitance = Next;
        if (OutBounceResiduals)
        {
            OutBounceResiduals->push_back(MaxResidual);
        }
    }
}

} // namespace Frontier
