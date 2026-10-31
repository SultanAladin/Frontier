/*==========================================================================================================================================
                                            SURFACE SCATTERING ATLAS DEFINITION
==========================================================================================================================================*/
// 🧩 Validated TextureWorkspace ScatteringSequence + AutomotivePaint presets & GPU float4 packing.

#include "Engine/Rendering/SurfaceScatteringAtlas.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

SurfaceScatteringAtlas::SurfaceScatteringAtlas()
{
    for (uint32_t Idx = 0u; Idx < kMaxClassificationSlots; ++Idx)
    {
        Entries_[Idx].ClassificationId                = Idx;
        Entries_[Idx].Scattering.Configuration        = {1u, 0u, 0u, 0u};
        Entries_[Idx].Scattering.Strata[0].AlbedoRoughness = {0.52f, 0.54f, 0.57f, 0.42f};
        Entries_[Idx].Scattering.Strata[0].Optics          = {0.0f, 1.50f, 1.0f, 0.0f};
    }
}

bool SurfaceScatteringAtlas::RegisterScatteringEntry(const SurfaceScatteringEntry& Entry)
{
    if (Entry.ClassificationId >= kMaxClassificationSlots)
    {
        return false;
    }
    if (!ValidatePreviewScattering(Entry.Scattering) || !ValidateAutomotivePaint(Entry.Paint))
    {
        return false;
    }
    Entries_[Entry.ClassificationId] = Entry;
    return true;
}

void SurfaceScatteringAtlas::RegisterControlVehicleScatteringPresets()
{
    auto MakeOneStratum = [](std::array<float, 3> Albedo, float Roughness, float Metallic, float Ior = 1.5f) {
        PreviewScattering S{};
        S.Configuration                 = {1u, 0u, 0u, 0u};
        S.Strata[0].AlbedoRoughness     = {Albedo[0], Albedo[1], Albedo[2], std::clamp(Roughness, 0.045f, 1.0f)};
        S.Strata[0].Optics              = {std::clamp(Metallic, 0.0f, 1.0f), Ior, 1.0f, 0.0f};
        S.Strata[0].Classification      = {0u, 0u, 0u, 0u};
        return S;
    };

    // Class 0 / 1: Proving-ground asphalt & warm concrete architectural surfaces
    {
        SurfaceScatteringEntry Ground{};
        Ground.ClassificationId = 0u;
        Ground.Scattering       = MakeOneStratum({0.26f, 0.27f, 0.29f}, 0.56f, 0.0f, 1.48f);
        (void)RegisterScatteringEntry(Ground);

        SurfaceScatteringEntry Concrete{};
        Concrete.ClassificationId = 1u;
        Concrete.Scattering       = MakeOneStratum({0.64f, 0.62f, 0.58f}, 0.48f, 0.0f, 1.50f);
        (void)RegisterScatteringEntry(Concrete);
    }

    // Class 6: Multi-stratum metallic sapphire-titanium automotive paint + absorbing dielectric clearcoat
    {
        SurfaceScatteringEntry Body{};
        Body.ClassificationId = 6u;
        Body.Scattering.Configuration = {2u, 0u, 0u, 0u};
        // Stratum 0: Metallic flake basecoat
        Body.Scattering.Strata[0].AlbedoRoughness = {0.16f, 0.34f, 0.62f, 0.20f};
        Body.Scattering.Strata[0].Optics          = {0.84f, 1.52f, 1.0f, 0.0f};
        Body.Scattering.Strata[0].Classification  = {0u, 0u, 0u, 0u};
        // Stratum 1: Absorbing polyurethane clearcoat
        Body.Scattering.Strata[1].AlbedoRoughness     = {1.0f, 1.0f, 1.0f, 0.055f};
        Body.Scattering.Strata[1].Optics              = {0.0f, 1.54f, 0.96f, 0.0f};
        Body.Scattering.Strata[1].AbsorptionThickness = {0.25f, 0.12f, 0.04f, 0.04f};
        Body.Scattering.Strata[1].Classification      = {1u, 0u, 0u, 0u};

        Body.Paint.Configuration  = {1u, 3u, 0u, 2u};
        Body.Paint.Distribution   = {0.68f, 380.0f, 28.0f, 0.65f};
        Body.Paint.Finish         = {0.10f, 0.24f, 0.82f, 0.96f};
        Body.Paint.Relief         = {0.18f, 0.52f, 0.12f, 26.0f};
        Body.Paint.Palette[0]     = {0.14f, 0.32f, 0.64f, 0.0f};
        Body.Paint.Palette[1]     = {0.32f, 0.56f, 0.88f, 1.0f};
        Body.Paint.AverageTint    = {0.20f, 0.40f, 0.72f, 0.0f};
        (void)RegisterScatteringEntry(Body);
    }

    // Class 7: Laminated automotive windshield & side glazing
    {
        SurfaceScatteringEntry Glass{};
        Glass.ClassificationId    = 7u;
        Glass.Scattering          = MakeOneStratum({0.90f, 0.96f, 0.98f}, 0.05f, 0.0f, 1.52f);
        Glass.TransmissiveGlass   = true;
        Glass.GlassIor            = 1.52f;
        Glass.GlassAbsorptionPerM = {0.85f, 0.22f, 0.28f};
        (void)RegisterScatteringEntry(Glass);
    }

    // Class 8 & 9: Satin carbon aero splitter & matte dark brake/chassis trim
    {
        SurfaceScatteringEntry Carbon{};
        Carbon.ClassificationId = 8u;
        Carbon.Scattering       = MakeOneStratum({0.09f, 0.10f, 0.11f}, 0.28f, 0.22f, 1.52f);
        (void)RegisterScatteringEntry(Carbon);

        SurfaceScatteringEntry Trim{};
        Trim.ClassificationId = 9u;
        Trim.Scattering       = MakeOneStratum({0.14f, 0.15f, 0.17f}, 0.46f, 0.15f, 1.50f);
        (void)RegisterScatteringEntry(Trim);
    }

    // Class 10: Brushed machined alloy multi-spoke wheel rim
    {
        SurfaceScatteringEntry Rim{};
        Rim.ClassificationId = 10u;
        Rim.Scattering       = MakeOneStratum({0.84f, 0.86f, 0.90f}, 0.15f, 0.94f, 1.50f);
        (void)RegisterScatteringEntry(Rim);
    }

    // Class 11: Vulcanized treaded tyre rubber
    {
        SurfaceScatteringEntry Tyre{};
        Tyre.ClassificationId = 11u;
        Tyre.Scattering       = MakeOneStratum({0.065f, 0.068f, 0.072f}, 0.74f, 0.0f, 1.48f);
        (void)RegisterScatteringEntry(Tyre);
    }

    // Class 12: Polycarbonate headlamp outer lens
    {
        SurfaceScatteringEntry Lens{};
        Lens.ClassificationId    = 12u;
        Lens.Scattering          = MakeOneStratum({0.95f, 0.97f, 1.00f}, 0.045f, 0.0f, 1.58f);
        Lens.TransmissiveGlass   = true;
        Lens.GlassIor            = 1.58f;
        Lens.GlassAbsorptionPerM = {0.15f, 0.12f, 0.08f};
        (void)RegisterScatteringEntry(Lens);
    }

    // Class 13 & 14: Smoked titanium LED headlamp fascia & sculpted crimson taillamp bar
    {
        SurfaceScatteringEntry Headlamp{};
        Headlamp.ClassificationId    = 13u;
        Headlamp.Scattering          = MakeOneStratum({0.68f, 0.72f, 0.78f}, 0.16f, 0.55f, 1.50f);
        Headlamp.EmissiveRadianceRgb = {1.35f, 1.48f, 1.72f};
        (void)RegisterScatteringEntry(Headlamp);

        SurfaceScatteringEntry Taillamp{};
        Taillamp.ClassificationId    = 14u;
        Taillamp.Scattering          = MakeOneStratum({0.86f, 0.08f, 0.08f}, 0.14f, 0.15f, 1.54f);
        Taillamp.EmissiveRadianceRgb = {2.4f, 0.16f, 0.14f};
        (void)RegisterScatteringEntry(Taillamp);
    }

    // Class 15: Dark intake grille
    {
        SurfaceScatteringEntry Grille{};
        Grille.ClassificationId = 15u;
        Grille.Scattering       = MakeOneStratum({0.07f, 0.075f, 0.085f}, 0.52f, 0.35f, 1.50f);
        (void)RegisterScatteringEntry(Grille);
    }

    // Class 16: Interior Alcantara cabin seats & console with Charlie fabric sheen!
    {
        SurfaceScatteringEntry Cabin{};
        Cabin.ClassificationId                        = 16u;
        Cabin.Scattering                              = MakeOneStratum({0.58f, 0.42f, 0.28f}, 0.62f, 0.0f, 1.48f);
        Cabin.Scattering.Strata[0].Sheen              = {0.82f, 0.68f, 0.52f, 0.48f};
        Cabin.Scattering.Strata[0].SheenFinish        = {0.38f, 0.0f, 0.0f, 0.0f};
        (void)RegisterScatteringEntry(Cabin);
    }

    // Class 20: Warm studio/proving-ground overhead softbox luminaire facet
    {
        SurfaceScatteringEntry Softbox{};
        Softbox.ClassificationId    = 20u;
        Softbox.Scattering          = MakeOneStratum({0.92f, 0.91f, 0.88f}, 0.35f, 0.0f, 1.48f);
        Softbox.EmissiveRadianceRgb = {3.8f, 3.65f, 3.45f};
        (void)RegisterScatteringEntry(Softbox);
    }
}

const SurfaceScatteringEntry& SurfaceScatteringAtlas::FindEntry(uint32_t ClassificationId) const noexcept
{
    return Entries_[std::min(ClassificationId, kMaxClassificationSlots - 1u)];
}

ScatteringSpectrum SurfaceScatteringAtlas::EvaluateBrdfCpu(uint32_t             ClassificationId,
                                                           std::array<float, 3> SurfaceNormal,
                                                           std::array<float, 3> ViewDirectionToEye,
                                                           std::array<float, 3> LightDirectionToSource) const noexcept
{
    const SurfaceScatteringEntry& Entry = FindEntry(ClassificationId);
    const double Nv = static_cast<double>(SurfaceNormal[0]) * ViewDirectionToEye[0] +
                      static_cast<double>(SurfaceNormal[1]) * ViewDirectionToEye[1] +
                      static_cast<double>(SurfaceNormal[2]) * ViewDirectionToEye[2];
    const double Nl = static_cast<double>(SurfaceNormal[0]) * LightDirectionToSource[0] +
                      static_cast<double>(SurfaceNormal[1]) * LightDirectionToSource[1] +
                      static_cast<double>(SurfaceNormal[2]) * LightDirectionToSource[2];
    if (Nv <= 0.0 || Nl <= 0.0)
    {
        return {0.0, 0.0, 0.0};
    }
    double Hx = static_cast<double>(ViewDirectionToEye[0]) + LightDirectionToSource[0];
    double Hy = static_cast<double>(ViewDirectionToEye[1]) + LightDirectionToSource[1];
    double Hz = static_cast<double>(ViewDirectionToEye[2]) + LightDirectionToSource[2];
    const double HLen = std::sqrt(Hx * Hx + Hy * Hy + Hz * Hz);
    if (HLen > 1.0e-12)
    {
        Hx /= HLen;
        Hy /= HLen;
        Hz /= HLen;
    }
    const double Nh = std::clamp(SurfaceNormal[0] * Hx + SurfaceNormal[1] * Hy + SurfaceNormal[2] * Hz, 0.0, 1.0);
    const double Vh = std::clamp(ViewDirectionToEye[0] * Hx + ViewDirectionToEye[1] * Hy + ViewDirectionToEye[2] * Hz, 0.0, 1.0);
    return EvaluateScatteringSequence(Entry.Scattering, Nv, Nl, Nh, Vh);
}

void SurfaceScatteringAtlas::PackForGpu(std::vector<std::array<float, 4>>& OutMaterialFloat4Table) const
{
    OutMaterialFloat4Table.clear();
    OutMaterialFloat4Table.reserve(kMaxClassificationSlots * kFloat4StridePerSlot);

    for (uint32_t Idx = 0u; Idx < kMaxClassificationSlots; ++Idx)
    {
        const SurfaceScatteringEntry& E    = Entries_[Idx];
        const ScatteringStratum&      Base = E.Scattering.Strata[0];
        const bool                    HasCoat =
            (E.Scattering.Configuration[0] >= 2u && E.Scattering.Strata[1].Classification[0] == 1u);
        const ScatteringStratum& Coat = E.Scattering.Strata[1];

        const float CoatCov   = HasCoat ? Coat.Optics[2] : 0.0f;
        const float CoatRough = HasCoat ? Coat.AlbedoRoughness[3] : 0.08f;
        const float CoatIor   = HasCoat ? Coat.Optics[1] : 1.50f;
        const float CoatThick = HasCoat ? Coat.AbsorptionThickness[3] : 0.0f;

        const float FlakeCov  = (E.Paint.Configuration[0] != 0u) ? E.Paint.Distribution[0] : 0.0f;
        const float FlakeFreq = (E.Paint.Configuration[0] != 0u) ? E.Paint.Distribution[2] : 1.0f;

        OutMaterialFloat4Table.push_back(Base.AlbedoRoughness);
        OutMaterialFloat4Table.push_back({Base.Optics[0], Base.Optics[1], CoatCov, CoatRough});
        OutMaterialFloat4Table.push_back({HasCoat ? Coat.AbsorptionThickness[0] * CoatThick : 0.0f,
                                          HasCoat ? Coat.AbsorptionThickness[1] * CoatThick : 0.0f,
                                          HasCoat ? Coat.AbsorptionThickness[2] * CoatThick : 0.0f,
                                          CoatIor});
        OutMaterialFloat4Table.push_back(Base.Sheen);
        OutMaterialFloat4Table.push_back({E.EmissiveRadianceRgb[0],
                                          E.EmissiveRadianceRgb[1],
                                          E.EmissiveRadianceRgb[2],
                                          Base.SheenFinish[0]});
        OutMaterialFloat4Table.push_back({FlakeCov,
                                          FlakeFreq,
                                          E.TransmissiveGlass ? 1.0f : 0.0f,
                                          E.GlassIor});
    }
}

} // namespace Frontier
