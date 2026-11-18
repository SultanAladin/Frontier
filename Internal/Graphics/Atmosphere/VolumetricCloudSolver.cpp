/*==============================================================================================================================================
                                                       VOLUMETRICCLOUDSOLVER.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the curved volumetric cloud stratum + cirrus veil: tileable 3D Perlin-Worley / Worley-billow noise volumes (built
//    once), genus height profiles, Worley edge erosion, RK4 flow-field advection, Owen-Sobol jittered marching with analytic step
//    integration, 4-octave Wrenninge multiple scattering, dual-lobe Henyey-Greenstein + 22° ice halo, Beer-Powder, solar cone shadowing,
//    Hillaire aerial perspective, cloud shadow transmittance, and joint bilateral denoising.

#include "Graphics/Atmosphere/VolumetricCloudSolver.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    using Atmosphere::EvaluateRotatedFbm3D;
    using Atmosphere::HashLatticeCell;

    constexpr uint32_t ShapeVolumeExtent  = 64u;  // [texel] - Tileable Perlin-Worley shape volume edge
    constexpr uint32_t DetailVolumeExtent = 64u;  // [texel] - Tileable Worley detail volume edge (4 texels per finest cell)
    constexpr uint32_t WeatherMapExtent   = 128u; // [texel] - Tileable mesoscale coverage map edge
    constexpr uint32_t ShapeQuantileCount = 129u; // [-]     - Inverse-CDF table resolution for coverage calibration

    [[nodiscard]] float EvaluateSmoothStep(float LowerEdge, float UpperEdge, float Sample) noexcept
    {
        const float Ratio = std::clamp((Sample - LowerEdge) / std::max(1.0e-8f, UpperEdge - LowerEdge), 0.0f, 1.0f);
        return Ratio * Ratio * (3.0f - 2.0f * Ratio);
    }

    [[nodiscard]] float RemapRange(float Sample, float SourceLow, float SourceHigh, float TargetLow, float TargetHigh) noexcept
    {
        return TargetLow + (Sample - SourceLow) * (TargetHigh - TargetLow) / std::max(1.0e-6f, SourceHigh - SourceLow);
    }

    // 📝 Liquid-water saturation: the eroded shape field rises linearly across a translucent fringe and saturates to unit density
    //    a third of the way in (σ_t reaches its full value within ~100 m of a sculpted cumulus surface).
    [[nodiscard]] float ResolveSaturatedDensity(float ShapeField) noexcept
    {
        return std::min(1.0f, ShapeField * 2.0f);
    }

    [[nodiscard]] int32_t WrapLattice(int32_t Index, int32_t Period) noexcept
    {
        return ((Index % Period) + Period) % Period;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                       TILEABLE GRADIENT PERLIN & WORLEY NOISE
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Periodic Perlin gradient noise in approximately [-1, 1] with lattice period Period (12 cube-edge gradients).
    [[nodiscard]] float EvaluatePeriodicPerlin3D(const Vector3f& Coord, int32_t Period, uint32_t Salt) noexcept
    {
        static constexpr float GradientTable[12][3] = {
            { 1, 1, 0 }, { -1, 1, 0 }, { 1, -1, 0 }, { -1, -1, 0 }, { 1, 0, 1 }, { -1, 0, 1 },
            { 1, 0, -1 }, { -1, 0, -1 }, { 0, 1, 1 }, { 0, -1, 1 }, { 0, 1, -1 }, { 0, -1, -1 } };

        const float   FloorX = std::floor(Coord.XCoord);
        const float   FloorY = std::floor(Coord.YCoord);
        const float   FloorZ = std::floor(Coord.ZCoord);
        const float   FracX  = Coord.XCoord - FloorX;
        const float   FracY  = Coord.YCoord - FloorY;
        const float   FracZ  = Coord.ZCoord - FloorZ;
        const int32_t CellX  = static_cast<int32_t>(FloorX);
        const int32_t CellY  = static_cast<int32_t>(FloorY);
        const int32_t CellZ  = static_cast<int32_t>(FloorZ);

        const auto Corner = [&](int32_t OffsetX, int32_t OffsetY, int32_t OffsetZ) noexcept
        {
            const float    Hashed = HashLatticeCell(WrapLattice(CellX + OffsetX, Period), WrapLattice(CellY + OffsetY, Period),
                                                    WrapLattice(CellZ + OffsetZ, Period), Salt);
            const uint32_t Select = std::min(11u, static_cast<uint32_t>(Hashed * 12.0f));
            return GradientTable[Select][0] * (FracX - static_cast<float>(OffsetX))
                 + GradientTable[Select][1] * (FracY - static_cast<float>(OffsetY))
                 + GradientTable[Select][2] * (FracZ - static_cast<float>(OffsetZ));
        };
        const auto  Quintic = [](float Ratio) noexcept { return Ratio * Ratio * Ratio * (Ratio * (Ratio * 6.0f - 15.0f) + 10.0f); };
        const auto  Mix     = [](float Low, float High, float Ratio) noexcept { return Low + (High - Low) * Ratio; };
        const float BlendX  = Quintic(FracX);
        const float BlendY  = Quintic(FracY);
        const float BlendZ  = Quintic(FracZ);

        const float Lower = Mix(Mix(Corner(0, 0, 0), Corner(1, 0, 0), BlendX), Mix(Corner(0, 1, 0), Corner(1, 1, 0), BlendX), BlendY);
        const float Upper = Mix(Mix(Corner(0, 0, 1), Corner(1, 0, 1), BlendX), Mix(Corner(0, 1, 1), Corner(1, 1, 1), BlendX), BlendY);
        return Mix(Lower, Upper, BlendZ);
    }

    // 📝 Periodic Worley F1 cellular noise, inverted to [0, 1] (1 at feature points) with lattice period Period.
    [[nodiscard]] float EvaluatePeriodicWorley3D(const Vector3f& Coord, int32_t Period, uint32_t Salt) noexcept
    {
        const float   FloorX = std::floor(Coord.XCoord);
        const float   FloorY = std::floor(Coord.YCoord);
        const float   FloorZ = std::floor(Coord.ZCoord);
        const int32_t CellX  = static_cast<int32_t>(FloorX);
        const int32_t CellY  = static_cast<int32_t>(FloorY);
        const int32_t CellZ  = static_cast<int32_t>(FloorZ);

        float NearestSquared = 8.0f;
        for (int32_t OffsetZ = -1; OffsetZ <= 1; ++OffsetZ)
        {
            for (int32_t OffsetY = -1; OffsetY <= 1; ++OffsetY)
            {
                for (int32_t OffsetX = -1; OffsetX <= 1; ++OffsetX)
                {
                    const int32_t WrapX    = WrapLattice(CellX + OffsetX, Period);
                    const int32_t WrapY    = WrapLattice(CellY + OffsetY, Period);
                    const int32_t WrapZ    = WrapLattice(CellZ + OffsetZ, Period);
                    const float   FeatureX = static_cast<float>(CellX + OffsetX) + HashLatticeCell(WrapX, WrapY, WrapZ, Salt);
                    const float   FeatureY = static_cast<float>(CellY + OffsetY) + HashLatticeCell(WrapX, WrapY, WrapZ, Salt + 101u);
                    const float   FeatureZ = static_cast<float>(CellZ + OffsetZ) + HashLatticeCell(WrapX, WrapY, WrapZ, Salt + 211u);
                    const float   DeltaX   = FeatureX - Coord.XCoord;
                    const float   DeltaY   = FeatureY - Coord.YCoord;
                    const float   DeltaZ   = FeatureZ - Coord.ZCoord;
                    NearestSquared = std::min(NearestSquared, DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ);
                }
            }
        }
        return 1.0f - std::clamp(std::sqrt(NearestSquared), 0.0f, 1.0f);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                     PRECOMPUTED TILEABLE NOISE VOLUMES (BUILT ONCE)
    //--------------------------------------------------------------------------------------------------------------------

    struct CloudNoiseVolumes
    {
        std::vector<float> ShapeTexels;                            // [-] - 64^3 x 4: Perlin-Worley, Worley fBm at periods 8, 16, 32
        std::vector<float> DetailTexels;                           // [-] - 64^3 x 3: Worley at periods 4, 8, 16
        std::vector<float> WeatherTexels;                          // [-] - 128^2 x 2: mesoscale coverage, convective height variation
        std::array<float, ShapeQuantileCount> CellularQuantiles{}; // [-] - Empirical inverse CDF of the cellular primary shape
        std::array<float, ShapeQuantileCount> SheetQuantiles{};    // [-] - Empirical inverse CDF of the stratiform primary shape
    };

    // 📝 Primary cloud shape from the four shape channels (Perlin-Worley dilated by the Worley billow fBm).
    [[nodiscard]] float ResolvePrimaryShape(const float* ShapeChannels, bool SheetCondition) noexcept
    {
        if (SheetCondition)
            return 0.55f + 0.45f * ShapeChannels[0];
        const float BillowFbm = ShapeChannels[1] * 0.625f + ShapeChannels[2] * 0.25f + ShapeChannels[3] * 0.125f;
        return std::clamp(RemapRange(ShapeChannels[0], BillowFbm - 1.0f, 1.0f, 0.0f, 1.0f), 0.0f, 1.0f);
    }

    [[nodiscard]] const CloudNoiseVolumes& RetrieveCloudNoiseVolumes()
    {
        static const CloudNoiseVolumes Volumes = []()
        {
            CloudNoiseVolumes  Constructed   {};
            constexpr uint32_t ShapeExtent   = ShapeVolumeExtent;
            constexpr uint32_t DetailExtent  = DetailVolumeExtent;
            constexpr uint32_t WeatherExtent = WeatherMapExtent;
            Constructed.ShapeTexels.resize(static_cast<std::size_t>(ShapeExtent) * ShapeExtent * ShapeExtent * 4u);
            Constructed.DetailTexels.resize(static_cast<std::size_t>(DetailExtent) * DetailExtent * DetailExtent * 3u);
            Constructed.WeatherTexels.resize(static_cast<std::size_t>(WeatherExtent) * WeatherExtent * 2u);

            for (uint32_t Slice = 0u; Slice < ShapeExtent; ++Slice)
            {
                for (uint32_t Row = 0u; Row < ShapeExtent; ++Row)
                {
                    for (uint32_t Column = 0u; Column < ShapeExtent; ++Column)
                    {
                        const Vector3f Unit     {
                            (static_cast<float>(Column) + 0.5f) / ShapeExtent,
                            (static_cast<float>(Row) + 0.5f) / ShapeExtent,
                            (static_cast<float>(Slice) + 0.5f) / ShapeExtent };
                        const auto     WorleyAt = [&](int32_t Period, uint32_t Salt)
                        {
                            return EvaluatePeriodicWorley3D(ScaleVector(Unit, static_cast<float>(Period)), Period, Salt);
                        };
                        float Perlin    = 0.0f;
                        float Amplitude = 0.5f;
                        for (int32_t Period = 4; Period <= 32; Period *= 2)
                        {
                            Perlin += Amplitude * EvaluatePeriodicPerlin3D(ScaleVector(Unit, static_cast<float>(Period)), Period, 3301u + static_cast<uint32_t>(Period));
                            Amplitude *= 0.5f;
                        }
                        Perlin = std::clamp(Perlin * 0.9f + 0.5f, 0.0f, 1.0f);
                        const float WorleyBillow = 0.625f * WorleyAt(4, 3411u) + 0.25f * WorleyAt(8, 3511u) + 0.125f * WorleyAt(16, 3611u);
                        const float PerlinWorley = std::clamp(RemapRange(Perlin, WorleyBillow - 1.0f, 1.0f, 0.0f, 1.0f), 0.0f, 1.0f);

                        const std::size_t TexelOffset = ((static_cast<std::size_t>(Slice) * ShapeExtent + Row) * ShapeExtent + Column) * 4u;
                        Constructed.ShapeTexels[TexelOffset + 0u] = PerlinWorley;
                        Constructed.ShapeTexels[TexelOffset + 1u] = 0.625f * WorleyAt(8, 3711u)  + 0.25f * WorleyAt(16, 3811u) + 0.125f * WorleyAt(32, 3911u);
                        Constructed.ShapeTexels[TexelOffset + 2u] = 0.625f * WorleyAt(16, 4011u) + 0.25f * WorleyAt(32, 4111u) + 0.125f * WorleyAt(64, 4211u);
                        Constructed.ShapeTexels[TexelOffset + 3u] = 0.625f * WorleyAt(32, 4311u) + 0.375f * WorleyAt(64, 4411u);
                    }
                }
            }

            // Coverage calibration: tabulate the inverse CDF so a threshold at quantile (1 - c) leaves exactly a fraction c of space cloudy.
            const auto TabulateQuantiles = [&](bool SheetCondition, std::array<float, ShapeQuantileCount>& OutQuantiles)
            {
                const std::size_t TexelCount = Constructed.ShapeTexels.size() / 4u;
                std::vector<float> Sorted(TexelCount);
                for (std::size_t Texel = 0u; Texel < TexelCount; ++Texel)
                    Sorted[Texel] = ResolvePrimaryShape(&Constructed.ShapeTexels[Texel * 4u], SheetCondition);
                std::sort(Sorted.begin(), Sorted.end());
                for (uint32_t Entry = 0u; Entry < ShapeQuantileCount; ++Entry)
                {
                    const std::size_t Rank = std::min(TexelCount - 1u, static_cast<std::size_t>(
                        static_cast<double>(Entry) / static_cast<double>(ShapeQuantileCount - 1u) * static_cast<double>(TexelCount - 1u)));
                    OutQuantiles[Entry] = Sorted[Rank];
                }
            };
            TabulateQuantiles(false, Constructed.CellularQuantiles);
            TabulateQuantiles(true, Constructed.SheetQuantiles);

            for (uint32_t Slice = 0u; Slice < DetailExtent; ++Slice)
            {
                for (uint32_t Row = 0u; Row < DetailExtent; ++Row)
                {
                    for (uint32_t Column = 0u; Column < DetailExtent; ++Column)
                    {
                        const Vector3f    Unit        {
                            (static_cast<float>(Column) + 0.5f) / DetailExtent,
                            (static_cast<float>(Row) + 0.5f) / DetailExtent,
                            (static_cast<float>(Slice) + 0.5f) / DetailExtent };
                        const std::size_t TexelOffset = ((static_cast<std::size_t>(Slice) * DetailExtent + Row) * DetailExtent + Column) * 3u;
                        Constructed.DetailTexels[TexelOffset + 0u] = EvaluatePeriodicWorley3D(ScaleVector(Unit, 4.0f),  4,  4511u);
                        Constructed.DetailTexels[TexelOffset + 1u] = EvaluatePeriodicWorley3D(ScaleVector(Unit, 8.0f),  8,  4611u);
                        Constructed.DetailTexels[TexelOffset + 2u] = EvaluatePeriodicWorley3D(ScaleVector(Unit, 16.0f), 16, 4711u);
                    }
                }
            }

            for (uint32_t Row = 0u; Row < WeatherExtent; ++Row)
            {
                for (uint32_t Column = 0u; Column < WeatherExtent; ++Column)
                {
                    const Vector3f Unit      { (static_cast<float>(Column) + 0.5f) / WeatherExtent, 0.37f, (static_cast<float>(Row) + 0.5f) / WeatherExtent };
                    float          Coverage  = 0.0f;
                    float          Variation = 0.0f;
                    float          Amplitude = 0.5f;
                    for (int32_t Period = 3; Period <= 48; Period *= 2)
                    {
                        const Vector3f Planar{ Unit.XCoord * Period, 0.5f, Unit.ZCoord * Period };
                        Coverage  += Amplitude * EvaluatePeriodicPerlin3D(Planar, Period, 4811u + static_cast<uint32_t>(Period));
                        Variation += Amplitude * EvaluatePeriodicPerlin3D(Planar, Period, 4911u + static_cast<uint32_t>(Period));
                        Amplitude *= 0.55f;
                    }
                    const std::size_t TexelOffset = (static_cast<std::size_t>(Row) * WeatherExtent + Column) * 2u;
                    Constructed.WeatherTexels[TexelOffset + 0u] = std::clamp(Coverage * 1.1f + 0.5f, 0.0f, 1.0f);
                    Constructed.WeatherTexels[TexelOffset + 1u] = std::clamp(Variation * 1.1f + 0.5f, 0.0f, 1.0f);
                }
            }
            return Constructed;
        }();
        return Volumes;
    }

    // 📝 Trilinear REPEAT sampling of a tileable volume at normalized coordinate (any real; wraps every 1.0) into OutChannels.
    template <uint32_t ChannelCount>
    void SampleTileableVolume(const std::vector<float>& Texels, uint32_t Extent, const Vector3f& Unit, std::array<float, ChannelCount>& OutChannels) noexcept
    {
        const float   ScaledX = Unit.XCoord * Extent - 0.5f;
        const float   ScaledY = Unit.YCoord * Extent - 0.5f;
        const float   ScaledZ = Unit.ZCoord * Extent - 0.5f;
        const float   FloorX  = std::floor(ScaledX);
        const float   FloorY  = std::floor(ScaledY);
        const float   FloorZ  = std::floor(ScaledZ);
        const float   BlendX  = ScaledX - FloorX;
        const float   BlendY  = ScaledY - FloorY;
        const float   BlendZ  = ScaledZ - FloorZ;
        const int32_t Period  = static_cast<int32_t>(Extent);
        const int32_t X0      = WrapLattice(static_cast<int32_t>(FloorX), Period);
        const int32_t Y0      = WrapLattice(static_cast<int32_t>(FloorY), Period);
        const int32_t Z0      = WrapLattice(static_cast<int32_t>(FloorZ), Period);
        const int32_t X1      = (X0 + 1) % Period;
        const int32_t Y1      = (Y0 + 1) % Period;
        const int32_t Z1      = (Z0 + 1) % Period;

        const auto Texel = [&](int32_t Column, int32_t Row, int32_t Slice, uint32_t Channel) noexcept
        {
            return Texels[((static_cast<std::size_t>(Slice) * Extent + static_cast<std::size_t>(Row)) * Extent + static_cast<std::size_t>(Column)) * ChannelCount + Channel];
        };
        for (uint32_t Channel = 0u; Channel < ChannelCount; ++Channel)
        {
            const float C00   = Texel(X0, Y0, Z0, Channel) + (Texel(X1, Y0, Z0, Channel) - Texel(X0, Y0, Z0, Channel)) * BlendX;
            const float C10   = Texel(X0, Y1, Z0, Channel) + (Texel(X1, Y1, Z0, Channel) - Texel(X0, Y1, Z0, Channel)) * BlendX;
            const float C01   = Texel(X0, Y0, Z1, Channel) + (Texel(X1, Y0, Z1, Channel) - Texel(X0, Y0, Z1, Channel)) * BlendX;
            const float C11   = Texel(X0, Y1, Z1, Channel) + (Texel(X1, Y1, Z1, Channel) - Texel(X0, Y1, Z1, Channel)) * BlendX;
            const float Lower = C00 + (C10 - C00) * BlendY;
            const float Upper = C01 + (C11 - C01) * BlendY;
            OutChannels[Channel] = Lower + (Upper - Lower) * BlendZ;
        }
    }

    void SampleWeatherMap(float CoordU, float CoordV, float& OutCoverage, float& OutVariation) noexcept
    {
        const CloudNoiseVolumes& Volumes = RetrieveCloudNoiseVolumes();
        constexpr uint32_t       Extent  = WeatherMapExtent;
        const float              ScaledX = CoordU * Extent - 0.5f;
        const float              ScaledY = CoordV * Extent - 0.5f;
        const float              FloorX  = std::floor(ScaledX);
        const float              FloorY  = std::floor(ScaledY);
        const float              BlendX  = ScaledX - FloorX;
        const float              BlendY  = ScaledY - FloorY;
        const int32_t            Period  = static_cast<int32_t>(Extent);
        const int32_t            X0      = WrapLattice(static_cast<int32_t>(FloorX), Period);
        const int32_t            Y0      = WrapLattice(static_cast<int32_t>(FloorY), Period);
        const int32_t            X1      = (X0 + 1) % Period;
        const int32_t            Y1      = (Y0 + 1) % Period;
        const auto               Texel   = [&](int32_t Column, int32_t Row, uint32_t Channel) noexcept
        {
            return Volumes.WeatherTexels[(static_cast<std::size_t>(Row) * Extent + static_cast<std::size_t>(Column)) * 2u + Channel];
        };
        const auto Bilinear = [&](uint32_t Channel) noexcept
        {
            const float Top    = Texel(X0, Y0, Channel) + (Texel(X1, Y0, Channel) - Texel(X0, Y0, Channel)) * BlendX;
            const float Bottom = Texel(X0, Y1, Channel) + (Texel(X1, Y1, Channel) - Texel(X0, Y1, Channel)) * BlendX;
            return Top + (Bottom - Top) * BlendY;
        };
        OutCoverage  = Bilinear(0u);
        OutVariation = Bilinear(1u);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                           GENUS DENSITY MODEL
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Vertical density envelope per genus over normalized stratum height fraction in [0, 1].
    [[nodiscard]] float EvaluateGenusHeightEnvelope(CloudGenusIdentifier Genus, float HeightFraction, float TopVariation) noexcept
    {
        switch (Genus)
        {
            case CloudGenusIdentifier::Cumulus:
            {
                const float TopFraction = 0.55f + 0.45f * TopVariation;
                // Dome taper from a fifth of the local top height: cells narrow steadily upward into rounded cauliflower crowns.
                return EvaluateSmoothStep(0.0f, 0.06f, HeightFraction) * (1.0f - EvaluateSmoothStep(TopFraction * 0.2f, TopFraction, HeightFraction));
            }
            case CloudGenusIdentifier::Stratocumulus:
                return EvaluateSmoothStep(0.0f, 0.18f, HeightFraction) * (1.0f - EvaluateSmoothStep(0.50f, 0.95f, HeightFraction));
            case CloudGenusIdentifier::Stratus:
                return EvaluateSmoothStep(0.0f, 0.25f, HeightFraction) * (1.0f - EvaluateSmoothStep(0.65f, 1.0f, HeightFraction));
            case CloudGenusIdentifier::Cumulonimbus:
                return EvaluateSmoothStep(0.0f, 0.03f, HeightFraction) * (1.0f - EvaluateSmoothStep(0.90f, 1.0f, HeightFraction));
            case CloudGenusIdentifier::Cirrus:
            default:
                return EvaluateSmoothStep(0.0f, 0.3f, HeightFraction) * (1.0f - EvaluateSmoothStep(0.6f, 1.0f, HeightFraction));
        }
    }

    // 📝 Worley edge erosion of a soft shape field followed by liquid-water saturation (footprint-prefiltered detail octaves).
    [[nodiscard]] float ApplyDetailErosion(
        const CloudStratumProfile& Stratum,
        const CloudNoiseVolumes&   Volumes,
        float                      HorizontalX,
        float                      HorizontalZ,
        float                      AltitudeKm,
        float                      HeightFraction,
        float                      ShapeField,
        float                      SampleFootprintKm) noexcept
    {
        // Worley edge erosion: billowy crowns at the top, wispy ragged floor (inverted detail) near the condensation level.
        const float          DetailTile = std::max(0.02f, Stratum.DetailScaleKm * 4.0f);
        std::array<float, 3> Detail     {};
        SampleTileableVolume<3>(Volumes.DetailTexels, DetailVolumeExtent,
            Vector3f{ HorizontalX / DetailTile, AltitudeKm * 1.3f / DetailTile, HorizontalZ / DetailTile }, Detail);
        // Footprint prefilter: an octave finer than the march step aliases into per-pixel speckle, so it fades toward the next
        // coarser octave (same value distribution) once the step exceeds its feature size (tile / 16 and tile / 8).
        const float FineOctaveWeight   = std::clamp(2.0f - SampleFootprintKm * 16.0f / DetailTile, 0.0f, 1.0f);
        const float MiddleOctaveWeight = std::clamp(2.0f - SampleFootprintKm * 8.0f / DetailTile, 0.0f, 1.0f);
        const float MiddleOctave       = Detail[0] + (Detail[1] - Detail[0]) * MiddleOctaveWeight;
        const float FineOctave         = MiddleOctave + (Detail[2] - MiddleOctave) * FineOctaveWeight;
        const float DetailFbm          = Detail[0] * 0.625f + MiddleOctave * 0.25f + FineOctave * 0.125f;
        // The detail volume stores inverted Worley (1 at feature points). Eroding by its complement (1 - fbm) carves the gaps
        // between feature points and leaves convex puffs centred on them (cauliflower crowns); eroding by the fbm itself hollows
        // the feature points into wisps, which is reserved for the ragged condensation floor (lowest 20 % of the stratum).
        const float FloorBlend         = std::clamp(1.0f - HeightFraction * 5.0f, 0.0f, 1.0f);
        const float DetailModifier     = (1.0f - DetailFbm) + (2.0f * DetailFbm - 1.0f) * FloorBlend;
        const float Eroded         = std::clamp(RemapRange(ShapeField, DetailModifier * Stratum.ErosionStrength, 1.0f, 0.0f, 1.0f), 0.0f, 1.0f);
        return ResolveSaturatedDensity(Eroded);
    }

    // 📝 Mesoscale weather (coverage swing, convective top variation) at a horizontal stratum position [km].
    void SampleStratumWeather(const CloudStratumProfile& Stratum, float HorizontalX, float HorizontalZ, float& OutCoverage, float& OutVariation) noexcept
    {
        const float WeatherTile = std::max(1.0f, Stratum.WeatherScaleKm * 4.0f);
        SampleWeatherMap(HorizontalX / WeatherTile, HorizontalZ / WeatherTile, OutCoverage, OutVariation);
    }

    // 📝 Local coverage: mesoscale clustering swings the configured mean (clear gaps + organised cloud streets) and flow-driven
    //    condensation (lift / convergence) adds to it. The stratus swing fades to zero as the deck closes (c -> 1), so an overcast
    //    preset is genuinely unbroken.
    [[nodiscard]] float ResolveLocalCoverage(const CloudStratumProfile& Stratum, float WeatherCoverage, float CondensationDelta) noexcept
    {
        const float Coverage = std::clamp(Stratum.CoverageFraction, 0.0f, 1.0f);
        if (Stratum.Genus == CloudGenusIdentifier::Stratus)
            return std::clamp(Coverage + (WeatherCoverage - 0.5f) * 0.25f * std::min(1.0f, 4.0f * (1.0f - Coverage)), 0.0f, 1.0f);
        return std::clamp(Coverage + (WeatherCoverage - 0.5f) * 1.10f * (1.0f - 0.6f * Coverage * Coverage) + CondensationDelta * 0.25f, 0.0f, 1.0f);
    }

    // 📝 Turret displacement amplitude in normalized cell-field units (Cumulonimbus towers are lumpier than fair-weather domes).
    [[nodiscard]] float ResolveTurretAmplitude(CloudGenusIdentifier Genus) noexcept
    {
        if (Genus == CloudGenusIdentifier::Cumulonimbus)
            return 0.55f;
        return (Genus == CloudGenusIdentifier::Stratocumulus) ? 0.32f : 0.38f;
    }

    // Polynomial smooth maximum (Quílez): neighbouring thermals merge through a rounded fillet instead of the hard crease a plain
    // max() leaves where two sphere-like cells intersect — the crease read as a flat lit facet beside a flat shadowed one.
    [[nodiscard]] float BlendFieldUnion(float Left, float Right) noexcept
    {
        constexpr float BlendWidth = 0.3f;
        const float     Overlap    = std::max(BlendWidth - std::fabs(Left - Right), 0.0f) / BlendWidth;
        return std::max(Left, Right) + Overlap * Overlap * BlendWidth * 0.25f;
    }

    // 📝 Convective cell field for Cumulus / Stratocumulus / Cumulonimbus: a Boolean germ-grain model on a jittered 2D lattice (spacing = shape
    //    scale). Each lattice cell holds at most one thermal with probability P, radius r = ρ S (0.7 + 0.6 u) and its own top height;
    //    the plan-view coverage of such Poisson discs is c = 1 - exp(-P π ρ²), inverted here so the configured local coverage is met
    //    exactly (P saturating at 1 grows the radii instead). The vertical profile is a flat-based dome (r √(1 - q²), q = h / top);
    //    a Cumulonimbus tower narrows slowly with height and spreads into a flat anvil just under the tropopause; Stratocumulus cells
    //    are flat-topped rolls stretched along the shear. Returns the smooth union of the signed normalized fields 1 - d / R(h)
    //    (positive inside, 1 on the axis).
    [[nodiscard]] float EvaluateConvectiveCellField(
        const CloudStratumProfile& Stratum,
        float                      HorizontalX,
        float                      HorizontalZ,
        float                      HeightFraction,
        float                      CondensationDelta) noexcept
    {
        const bool  TowerCondition = Stratum.Genus == CloudGenusIdentifier::Cumulonimbus;
        const float Spacing        = std::max(0.1f, Stratum.ShapeScaleKm);
        const bool  RollCondition  = Stratum.Genus == CloudGenusIdentifier::Stratocumulus;
        const float RadiusRatio    = TowerCondition ? 0.40f : (RollCondition ? 0.42f : 0.36f);
        const float DiscArea       = Atmosphere::PiConstant * RadiusRatio * RadiusRatio;

        // Lattice reach: a thermal plus its turret bulge (1 + amplitude) and sheared anvil must stay inside the searched
        // neighbourhood, otherwise its surface is clipped flat at a lattice line. Towers search 5 × 5 cells, domes 3 × 3.
        const int32_t Reach        = TowerCondition ? 2 : 1;
        const float   RadiusLimit  = (static_cast<float>(Reach) - 0.02f) / (1.0f + ResolveTurretAmplitude(Stratum.Genus));
        const float   CellX        = HorizontalX / Spacing;
        const float   CellZ        = HorizontalZ / Spacing;
        const int32_t AnchorX        = static_cast<int32_t>(std::floor(CellX));
        const int32_t AnchorZ        = static_cast<int32_t>(std::floor(CellZ));
        float         BestField    = -1.0f;
        for (int32_t OffsetZ = -Reach; OffsetZ <= Reach; ++OffsetZ)
        {
            for (int32_t OffsetX = -Reach; OffsetX <= Reach; ++OffsetX)
            {
                const int32_t LatticeX = AnchorX + OffsetX;
                const int32_t LatticeZ = AnchorZ + OffsetZ;
                const float CentreX = static_cast<float>(LatticeX) + HashLatticeCell(LatticeX, 1, LatticeZ, 6203u);
                const float CentreZ = static_cast<float>(LatticeZ) + HashLatticeCell(LatticeX, 2, LatticeZ, 6307u);
                // Coverage, and so P and the radius gain, are evaluated at the thermal centre: sampled at the point instead, the
                // coverage gradient would grow and shrink one thermal across its own body and slice it into flat walls.
                float WeatherCoverage  = 0.5f;
                float WeatherVariation = 0.5f;
                SampleStratumWeather(Stratum, CentreX * Spacing, CentreZ * Spacing, WeatherCoverage, WeatherVariation);
                const float LocalCoverage = ResolveLocalCoverage(Stratum, WeatherCoverage, CondensationDelta);
                float       Occupancy     = -std::log(1.0f - std::min(LocalCoverage, 0.985f)) / DiscArea;
                float       RadiusGain    = 1.0f;
                if (Occupancy > 1.0f)
                {
                    RadiusGain = std::sqrt(Occupancy);
                    Occupancy  = 1.0f;
                }
                // Soft existence (short ramp around the lattice hash) so thermals swell in rather than pop as coverage evolves.
                const float Existence = std::clamp((Occupancy - HashLatticeCell(LatticeX, 0, LatticeZ, 6101u)) / 0.12f, 0.0f, 1.0f);
                if (Existence <= 0.0f)
                    continue;
                const float Radius  = std::min(RadiusLimit, RadiusRatio * RadiusGain * (0.7f + 0.6f * HashLatticeCell(LatticeX, 3, LatticeZ, 6409u))) * std::sqrt(Existence);
                const float TopHash = HashLatticeCell(LatticeX, 4, LatticeZ, 6511u);

                // Stratocumulus cells are rolls stretched along the shear vector (+X): the along-roll offset counts at 60 %.
                const float DeltaX  = (CellX - CentreX) * (RollCondition ? 0.6f : 1.0f);
                const float DeltaZ  = CellZ - CentreZ;
                const float Offset  = std::sqrt(DeltaX * DeltaX + DeltaZ * DeltaZ);
                if (TowerCondition)
                {
                    const float TopFraction = 0.86f + 0.14f * TopHash;
                    const float Ratio       = HeightFraction / TopFraction;
                    if (Ratio >= 1.0f)
                        continue;
                    const float Tower  = Radius * (1.0f - 0.15f * Ratio) * std::sqrt(std::max(0.0f, 1.0f - std::pow(Ratio, 8.0f)));
                    const float Spread = EvaluateSmoothStep(0.60f, 0.90f, Ratio);
                    const float Anvil  = std::min(RadiusLimit - 0.45f * Spread, Radius * (1.0f + 4.0f * Stratum.AnvilSpread) * Spread * std::sqrt(Spread)
                                                                                * (1.0f - EvaluateSmoothStep(0.92f, 1.0f, Ratio)));
                    // Upper-level shear drags the anvil downwind (+X): its centre leans out of the tower as the spread grows.
                    const float AnvilDeltaX = DeltaX - 0.45f * Spread;
                    const float TowerField  = 1.0f - Offset / std::max(1.0e-3f, Tower);
                    const float AnvilField  = 1.0f - std::sqrt(AnvilDeltaX * AnvilDeltaX + DeltaZ * DeltaZ) / std::max(1.0e-3f, Anvil);
                    BestField = BlendFieldUnion(BestField, std::max(TowerField, AnvilField));
                }
                else if (RollCondition)
                {
                    // Shallow boxy cell: nearly full-depth, flat-topped (√(1 - q⁴)) under the capping inversion.
                    const float TopFraction = 0.62f + 0.38f * TopHash;
                    const float Ratio       = HeightFraction / TopFraction;
                    if (Ratio >= 1.0f)
                        continue;
                    BestField = BlendFieldUnion(BestField, 1.0f - Offset / std::max(1.0e-3f, Radius * std::sqrt(1.0f - Ratio * Ratio * Ratio * Ratio)));
                }
                else
                {
                    const float TopFraction = std::clamp(0.30f + 0.40f * TopHash + 0.30f * WeatherVariation, 0.25f, 1.0f);
                    const float Ratio       = HeightFraction / TopFraction;
                    if (Ratio >= 1.0f)
                        continue;
                    BestField = BlendFieldUnion(BestField, 1.0f - Offset / std::max(1.0e-3f, Radius * std::sqrt(1.0f - Ratio * Ratio)));
                }
            }
        }
        return BestField;
    }

    // 📝 Density at an already-advected planet-local position (horizontal X/Z in km, altitude above BottomRadius in km).
    [[nodiscard]] float EvaluateStratumDensityAdvected(
        const CloudStratumProfile& Stratum,
        float                      HorizontalX,
        float                      HorizontalZ,
        float                      AltitudeKm,
        float                      CondensationDelta,
        bool                       DetailEnabled,
        float                      SampleFootprintKm) noexcept
    {
        const float Thickness      = std::max(0.05f, Stratum.CeilingAltitudeKm - Stratum.FloorAltitudeKm);
        const float HeightFraction = (AltitudeKm - Stratum.FloorAltitudeKm) / Thickness;
        if (HeightFraction <= 0.0f || HeightFraction >= 1.0f)
            return 0.0f;

        const CloudNoiseVolumes& Volumes             = RetrieveCloudNoiseVolumes();
        float                    ShapeField           = 0.0f;
        const bool               ConvectiveCondition = Stratum.Genus == CloudGenusIdentifier::Cumulus || Stratum.Genus == CloudGenusIdentifier::Cumulonimbus
                                                    || Stratum.Genus == CloudGenusIdentifier::Stratocumulus;
        if (ConvectiveCondition)
        {
            // Thermal domes from the germ-grain field, their surfaces displaced by 3D Perlin-Worley turrets so every dome breaks
            // into stacked convective bubbles before the Worley erosion adds the cauliflower.
            const float CellField = EvaluateConvectiveCellField(Stratum, HorizontalX, HorizontalZ, HeightFraction, CondensationDelta);
            const bool  TowerCondition  = Stratum.Genus == CloudGenusIdentifier::Cumulonimbus;
            const float TurretAmplitude = ResolveTurretAmplitude(Stratum.Genus);
            if (CellField <= -TurretAmplitude)
                return 0.0f;
            // Two turret octaves: bubbles at ~0.4 and ~0.15 of the thermal spacing (Cumulonimbus: ~0.2 and ~0.08, so a 15 km tower
            // breaks into kilometre-scale convective cells all the way up instead of reading as a smooth column).
            const float TurretTileLarge = std::max(0.1f, Stratum.ShapeScaleKm * (TowerCondition ? 0.8f : (Stratum.Genus == CloudGenusIdentifier::Stratocumulus ? 1.2f : 1.6f)));
            const float TurretTileSmall = TurretTileLarge * 0.38f;
            const float TurretSpan      = Volumes.CellularQuantiles[(ShapeQuantileCount - 1u) * 7u / 8u] - Volumes.CellularQuantiles[(ShapeQuantileCount - 1u) / 8u];
            const float TurretMedian    = Volumes.CellularQuantiles[ShapeQuantileCount / 2u];
            std::array<float, 4> TurretLarge {};
            std::array<float, 4> TurretSmall {};
            SampleTileableVolume<4>(Volumes.ShapeTexels, ShapeVolumeExtent, Vector3f{ HorizontalX / TurretTileLarge, AltitudeKm * 1.2f / TurretTileLarge, HorizontalZ / TurretTileLarge }, TurretLarge);
            SampleTileableVolume<4>(Volumes.ShapeTexels, ShapeVolumeExtent, Vector3f{ HorizontalZ / TurretTileSmall + 0.37f, AltitudeKm * 1.2f / TurretTileSmall, HorizontalX / TurretTileSmall + 0.61f }, TurretSmall);
            // Standardised turret noise: centred on the shape median and scaled by the central 75 % quantile span (the raw
            // Perlin-Worley distribution is narrow, ~±0.08), so ±0.5 spans three quarters of all turret samples. The sum is
            // clamped to ±1 so a bulge never outgrows the early-out bound or the lattice reach (no flat clipped walls).
            const float TurretBlend = 0.65f * (ResolvePrimaryShape(TurretLarge.data(), false) - TurretMedian) + 0.35f * (ResolvePrimaryShape(TurretSmall.data(), false) - TurretMedian);
            const float TurretShape = std::clamp(TurretBlend / std::max(1.0e-3f, TurretSpan) * 1.25f, -1.0f, 1.0f);
            const float FloorFade   = EvaluateSmoothStep(0.0f, 0.04f, HeightFraction) * (1.0f - EvaluateSmoothStep(0.97f, 1.0f, HeightFraction));
            // The anvil is glaciated outflow, not convection: its ice sheet is smooth and fibrous, so turrets and erosion fade out
            // across the spreading level.
            const float Glaciation = TowerCondition ? EvaluateSmoothStep(0.66f, 0.84f, HeightFraction) : 0.0f;
            ShapeField = std::clamp((CellField + TurretShape * TurretAmplitude * (1.0f - 0.75f * Glaciation)) * FloorFade / 0.45f, 0.0f, 1.0f);
            const float Density = ResolveSaturatedDensity(ShapeField);
            if (Density <= 0.0f || !DetailEnabled)
                return Density;
            const float Eroded = ApplyDetailErosion(Stratum, Volumes, HorizontalX, HorizontalZ, AltitudeKm, HeightFraction, ShapeField, SampleFootprintKm);
            return Density + (Eroded - Density) * (1.0f - 0.7f * Glaciation);
        }
        float WeatherCoverage  = 0.5f;
        float WeatherVariation = 0.5f;
        SampleStratumWeather(Stratum, HorizontalX, HorizontalZ, WeatherCoverage, WeatherVariation);
        float LocalCoverage = ResolveLocalCoverage(Stratum, WeatherCoverage, CondensationDelta);
        if (LocalCoverage <= 0.01f)
            return 0.0f;
        if (Stratum.AnvilSpread > 0.0f)
            LocalCoverage = std::clamp(LocalCoverage + Stratum.AnvilSpread * EvaluateSmoothStep(0.72f, 0.92f, HeightFraction), 0.0f, 1.0f);

        const float Envelope = EvaluateGenusHeightEnvelope(Stratum.Genus, HeightFraction, WeatherVariation);
        if (Envelope <= 0.0f)
            return 0.0f;

        const float              ShapeTile     = std::max(0.1f, Stratum.ShapeScaleKm * 4.0f);
        float                    StretchX      = 1.0f;
        float                    StretchZ      = 1.0f;
        float                    VerticalScale = 1.6f;
        if (Stratum.Genus == CloudGenusIdentifier::Stratocumulus)
        {
            StretchX      = 0.55f;   // roll-cloud elongation across the shear vector
            VerticalScale = 2.4f;
        }

        std::array<float, 4> Shape{};
        SampleTileableVolume<4>(Volumes.ShapeTexels, ShapeVolumeExtent,
            Vector3f{ HorizontalX * StretchX / ShapeTile, AltitudeKm * VerticalScale / ShapeTile, HorizontalZ * StretchZ / ShapeTile }, Shape);

        const bool  SheetCondition = Stratum.Genus == CloudGenusIdentifier::Stratus;
        const float PrimaryShape   = ResolvePrimaryShape(Shape.data(), SheetCondition);

        // Calibrated coverage threshold: the (1 - c) quantile of the shape distribution, so the cloudy volume fraction equals the
        // local coverage c. A short ramp above the threshold gives crisp but antialiased cell boundaries (liquid water rises fast).
        const std::array<float, ShapeQuantileCount>& Quantiles        = SheetCondition ? Volumes.SheetQuantiles : Volumes.CellularQuantiles;
        const float                                  QuantilePosition = (1.0f - LocalCoverage) * static_cast<float>(ShapeQuantileCount - 1u);
        const uint32_t                               QuantileLower    = std::min(ShapeQuantileCount - 2u, static_cast<uint32_t>(QuantilePosition));
        const float                                  Threshold        = Quantiles[QuantileLower] + (Quantiles[QuantileLower + 1u] - Quantiles[QuantileLower]) * (QuantilePosition - static_cast<float>(QuantileLower));
        // Soft shape field: a wide ramp above the threshold (instead of a near-step) leaves a density gradient across the outer
        // few hundred metres of every cell, so the Worley erosion below sculpts real billows rather than dusting a hard shell.
        const float                                  SoftRamp         = std::max(0.06f, 0.35f * (Quantiles[ShapeQuantileCount - 1u] - Threshold));
                                                     ShapeField        = std::clamp((PrimaryShape * Envelope - Threshold) / SoftRamp, 0.0f, 1.0f);
        float                                        Density          = ResolveSaturatedDensity(ShapeField);
        // Closing stratus deck: the residual low quantile of a 24 km sheet noise clusters into one large hole, but a real
        // overcast only thins there; a coverage-gated floor keeps an optically thin (tau ~ 5) sheet across those patches.
        if (SheetCondition)
            Density = std::max(Density, Envelope * 0.35f * EvaluateSmoothStep(0.90f, 1.0f, LocalCoverage));
        if (Density <= 0.0f || !DetailEnabled)
            return Density;

        Density = ApplyDetailErosion(Stratum, Volumes, HorizontalX, HorizontalZ, AltitudeKm, HeightFraction, ShapeField, SampleFootprintKm);
        if (SheetCondition)
            Density = std::max(Density, Envelope * 0.35f * EvaluateSmoothStep(0.90f, 1.0f, LocalCoverage));
        return Density;
    }

    // 📝 Planet-local km -> flow world metres (+Z up) and the backward-advected horizontal displacement [km].
    void ResolveFlowDisplacement(
        const AtmosphericFlowField* OptionalFlowField,
        const Vector3f&             PlanetPositionKm,
        float                       AltitudeKm,
        float                       SequenceSeconds,
        float&                      OutDeltaX,
        float&                      OutDeltaZ,
        float&                      OutCondensation) noexcept
    {
        OutDeltaX       = 0.0f;
        OutDeltaZ       = 0.0f;
        OutCondensation = 0.0f;
        if (OptionalFlowField == nullptr)
            return;
        const Vector3f WorldMetres{ PlanetPositionKm.XCoord * 1000.0f, PlanetPositionKm.ZCoord * 1000.0f, AltitudeKm * 1000.0f };

        // Dual-phase semi-Lagrangian advection: two characteristics of bounded age (period P, offset P/2) cross-faded with
        // triangular weights. Unbounded backward tracing shears the noise into taffy-like marbling after a few eddy turnovers;
        // bounded ages keep cloud texture local while the blend stays continuous in time (each phase resets at zero weight).
        // P = 40 s caps the differential rotation of a 34 m/s eye wall at r = 1.3 km to ~1 rad per phase (150 s gave ~3.9 rad).
        constexpr float          AdvectionPeriodSeconds = 40.0f;
        const float              PhaseFirst             = std::fmod(std::max(0.0f, SequenceSeconds), AdvectionPeriodSeconds);
        const float              PhaseSecond            = std::fmod(std::max(0.0f, SequenceSeconds) + 0.5f * AdvectionPeriodSeconds, AdvectionPeriodSeconds);
        const float              WeightFirst            = 1.0f - std::abs(2.0f * PhaseFirst / AdvectionPeriodSeconds - 1.0f);
        const FlowAdvectedSample First                  = TraceFlowCharacteristicBackward(*OptionalFlowField, WorldMetres, PhaseFirst, 4u);
        const FlowAdvectedSample Second                 = TraceFlowCharacteristicBackward(*OptionalFlowField, WorldMetres, PhaseSecond, 4u);
        const float              WeightSecond           = 1.0f - WeightFirst;
        OutDeltaX       = ((First.SourcePositionMetres.XCoord - WorldMetres.XCoord) * WeightFirst
                           + (Second.SourcePositionMetres.XCoord - WorldMetres.XCoord) * WeightSecond) * 0.001f;
        OutDeltaZ       = ((First.SourcePositionMetres.YCoord - WorldMetres.YCoord) * WeightFirst
                           + (Second.SourcePositionMetres.YCoord - WorldMetres.YCoord) * WeightSecond) * 0.001f;
        // Condensation is a property of the instantaneous flow (lift / convergence), not of parcel age.
        const FlowVelocitySample Instantaneous = EvaluateAtmosphericFlow(*OptionalFlowField, WorldMetres, SequenceSeconds);
        OutCondensation = std::clamp(0.5f * (First.CondensationDelta + Second.CondensationDelta) + 0.5f * Instantaneous.CondensationDelta, -1.0f, 1.0f);
    }

    [[nodiscard]] float EvaluateHenyeyGreenstein(float CosineAngle, float Asymmetry) noexcept
    {
        const float G           = std::clamp(Asymmetry, -0.99f, 0.99f);
        const float G2          = G * G;
        const float Denominator = std::max(1.0e-4f, 1.0f + G2 - 2.0f * G * CosineAngle);
        return (1.0f - G2) / (4.0f * Atmosphere::PiConstant * Denominator * std::sqrt(Denominator));
    }

    [[nodiscard]] float EvaluateDualLobePhase(const VolumetricCloudConfiguration& CloudConfiguration, float CosineAngle, float EccentricityScale) noexcept
    {
        const float Weight = std::clamp(CloudConfiguration.ForwardLobeWeight, 0.0f, 1.0f);
        return Weight * EvaluateHenyeyGreenstein(CosineAngle, CloudConfiguration.ForwardAsymmetry * EccentricityScale)
             + (1.0f - Weight) * EvaluateHenyeyGreenstein(CosineAngle, CloudConfiguration.BackwardAsymmetry * EccentricityScale);
    }

    // 📝 Wrenninge (2013) multiple-scattering octaves (a = b = c = 0.5 on extinction, energy, eccentricity) plus a diffusion tail. The
    //    octave series decays as e^{-τ/2^n} and leaves the far side of a τ ~ 100 cumulus black, but radiative transfer is diffusive
    //    there: the conservative two-stream flux transmission 1 / (1 + ¾ (1 - g) τ) falls off only algebraically. The tail feeds that
    //    isotropic diffuse field (mean intensity ~ T_d / π per unit solar flux, half of it scattered back toward the viewer), gated by
    //    1 - e^{-τ/2} so thin sunlit fringes stay octave-dominated.
    [[nodiscard]] float EvaluateScatterOctaves(const VolumetricCloudConfiguration& CloudConfiguration, float CosineScatter, float SunOpticalDepth, uint32_t Octaves) noexcept
    {
        float ScatterSum = 0.0f;
        float Extinction = 1.0f;
        float Energy     = 1.0f;
        float Eccentric  = 1.0f;
        for (uint32_t Octave = 0u; Octave < Octaves; ++Octave)
        {
            ScatterSum += Energy * EvaluateDualLobePhase(CloudConfiguration, CosineScatter, Eccentric) * std::exp(-Extinction * SunOpticalDepth);
            Extinction *= 0.5f;
            Energy     *= 0.5f;
            Eccentric  *= 0.5f;
        }
        const float DiffuseTransmission = 1.0f / (1.0f + 0.75f * (1.0f - 0.85f) * SunOpticalDepth);
        return ScatterSum + 0.3f / Atmosphere::PiConstant * DiffuseTransmission * (1.0f - std::exp(-0.5f * SunOpticalDepth));
    }

    // 📝 Beer-Powder in-scatter deficit at cloud edges seen away from the sun (dark crenellated rims, bright sunlit cores).
    [[nodiscard]] float EvaluatePowderFactor(const VolumetricCloudConfiguration& CloudConfiguration, float Density, float CosineScatter) noexcept
    {
        const float LocalDepth   = Density * CloudConfiguration.Stratum.ExtinctionPerKm * 0.09f;
        const float PowderTerm   = 1.0f - std::exp(-2.0f * LocalDepth);
        const float PowderWeight = std::clamp(CloudConfiguration.PowderStrength, 0.0f, 1.0f) * (0.5f - 0.5f * CosineScatter);
        return (1.0f - PowderWeight) + PowderWeight * PowderTerm;
    }

    // 📝 Conservative two-stream diffuse transmission T = 1 / (1 + ¾ (1 - g) τ) with cloud-droplet asymmetry g ≈ 0.85.
    [[nodiscard]] float EvaluateAmbientDiffuseTransmission(float OpticalDepth) noexcept
    {
        return 1.0f / (1.0f + 0.75f * (1.0f - 0.85f) * OpticalDepth);
    }

    // 📝 Empty-space probe step [km]: 1/24 of the stratum (capped at 200 m) near the viewer, then 1.5 % of range.
    [[nodiscard]] float ResolveCoarseStepKm(float ThicknessKm, float DistanceKm) noexcept
    {
        return std::max(std::min(ThicknessKm / 24.0f, 0.2f), 0.015f * DistanceKm);
    }

    // 📝 In-cloud integration step [km]: 25 m at the viewer growing with range at ~the pixel footprint, capped at 1/12 of the stratum.
    [[nodiscard]] float ResolveFineStepKm(float ThicknessKm, float DistanceKm) noexcept
    {
        return std::min(ThicknessKm / 12.0f, 0.025f + 0.0035f * DistanceKm);
    }

    // 📝 Cosine-weighted mean radiance of the upper (or lower) hemisphere from the sky-view table: 4 elevation rings × 8 azimuths
    //    (offset half a sector from the sun), weighted by sin(e)·cos(e) (projected solid angle).
    [[nodiscard]] LinearColour ResolveHemisphereRadiance(
        const AtmosphericLookupTables& Tables,
        const AtmosphereUniformBlock&  Profile,
        const Vector3f&                SolarAxis,
        bool                           UpperCondition) noexcept
    {
        static constexpr float RingElevations[4] = { 0.20f, 0.60f, 1.05f, 1.45f }; // [rad]
        const float  SunAzimuth = std::atan2(SolarAxis.ZCoord, SolarAxis.XCoord);
        LinearColour Sum        {};
        float        WeightSum  = 0.0f;
        for (const float Elevation : RingElevations)
        {
            const float SignedElevation = UpperCondition ? Elevation : -Elevation;
            const float Weight          = std::sin(Elevation) * std::cos(Elevation);
            for (uint32_t Sector = 0u; Sector < 8u; ++Sector)
            {
                const float    Azimuth   = SunAzimuth + (static_cast<float>(Sector) + 0.5f) * (Atmosphere::TauConstant / 8.0f);
                const Vector3f Direction { std::cos(SignedElevation) * std::cos(Azimuth), std::sin(SignedElevation), std::cos(SignedElevation) * std::sin(Azimuth) };
                Sum        = AddColour(Sum, ScaleColour(SampleSkyViewRadiance(Tables, Profile, Direction), Weight));
                WeightSum += Weight;
            }
        }
        return ScaleColour(Sum, 1.0f / WeightSum);
    }

    // 📝 Ray-sphere span helper for a ray from (0, ViewerRadius, 0) along unit Direction: near/far roots [km]; false on miss.
    [[nodiscard]] bool IntersectConcentricSphere(float ViewerRadiusKm, const Vector3f& Direction, float SphereRadiusKm, float& OutNear, float& OutFar) noexcept
    {
        const float HalfB        = ViewerRadiusKm * Direction.YCoord;
        const float Discriminant = HalfB * HalfB - (ViewerRadiusKm * ViewerRadiusKm - SphereRadiusKm * SphereRadiusKm);
        if (Discriminant < 0.0f)
            return false;
        const float Root = std::sqrt(Discriminant);
        OutNear = -HalfB - Root;
        OutFar  = -HalfB + Root;
        return OutFar > 0.0f;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                CIRRUS VEIL
    //--------------------------------------------------------------------------------------------------------------------

    [[nodiscard]] float EvaluateCirrusFilamentDensity(
        const CirrusVeilProfile&    Cirrus,
        const AtmosphericFlowField* OptionalFlowField,
        const Vector3f&             PlanetPositionKm,
        float                       SequenceSeconds) noexcept
    {
        float DeltaX       = 0.0f;
        float DeltaZ       = 0.0f;
        float Condensation = 0.0f;
        ResolveFlowDisplacement(OptionalFlowField, PlanetPositionKm, Cirrus.AltitudeKm, SequenceSeconds, DeltaX, DeltaZ, Condensation);

        // Along-wind fibre axis from the synoptic wind (default west-south-westerly jet).
        float WindX = 0.95f;
        float WindZ = 0.31f;
        if (OptionalFlowField != nullptr)
        {
            const float Speed = std::hypot(OptionalFlowField->Synoptic.ReferenceVelocity.XCoord, OptionalFlowField->Synoptic.ReferenceVelocity.YCoord);
            if (Speed > 0.1f)
            {
                WindX = OptionalFlowField->Synoptic.ReferenceVelocity.XCoord / Speed;
                WindZ = OptionalFlowField->Synoptic.ReferenceVelocity.YCoord / Speed;
            }
        }
        const float PositionX = PlanetPositionKm.XCoord + DeltaX;
        const float PositionZ = PlanetPositionKm.ZCoord + DeltaZ;
        const float AlongWind = (PositionX * WindX + PositionZ * WindZ) / std::max(0.2f, Cirrus.FilamentScaleKm);
        const float CrossWind = (-PositionX * WindZ + PositionZ * WindX) / std::max(0.2f, Cirrus.FilamentScaleKm) * std::max(1.0f, Cirrus.StreakAnisotropy);

        // Domain-warped fibres: long along-wind strands (fibratus) with uncinus hooks from a low-frequency warp of the cross-wind
        // coordinate, embedded in a soft translucent patch body so the veil never fragments into isolated hard dashes.
        const float Warp      = EvaluateRotatedFbm3D(Vector3f{ AlongWind * 0.12f, 3.1f, CrossWind * 0.05f }, 3u, 5101u) - 0.5f;
        const float Fibres    = EvaluateRotatedFbm3D(Vector3f{ AlongWind * 0.16f, 7.7f, CrossWind * 0.8f + Warp * 5.0f }, 5u, 5203u);
        const float Patches   = EvaluateRotatedFbm3D(Vector3f{ PositionX / (Cirrus.FilamentScaleKm * 2.4f), 1.3f, PositionZ / (Cirrus.FilamentScaleKm * 2.4f) }, 4u, 5309u);
        const float Coverage  = std::clamp(Cirrus.CoverageFraction + Condensation * 0.15f, 0.0f, 1.0f);
        const float PatchBody = EvaluateSmoothStep(0.70f - 0.45f * Coverage, 0.98f - 0.45f * Coverage, Patches);
        const float Strands   = EvaluateSmoothStep(0.40f, 0.78f, Fibres);
        return PatchBody * (0.22f + 0.78f * Strands * Strands);
    }

    // 📝 Single-scatter ice sheet radiance (dual HG + 22° halo) and transmittance along LocalDirection.
    [[nodiscard]] CloudRayResult IntegrateCirrusVeil(
        const VolumetricCloudConfiguration& CloudConfiguration,
        const AtmosphericLookupTables&      Tables,
        const AtmosphereUniformBlock&       Profile,
        const AtmosphericFlowField*         OptionalFlowField,
        float                               ViewerRadiusKm,
        const Vector3f&                     LocalDirection,
        const Vector3f&                     SolarAxis,
        const LinearColour&                 SolarFlux,
        const LinearColour&                 AmbientRadiance,
        float                               SequenceSeconds) noexcept
    {
        CloudRayResult           Result {};
        const CirrusVeilProfile& Cirrus = CloudConfiguration.Cirrus;
        if (!Cirrus.VeilEnabled || Cirrus.CoverageFraction <= 0.0f)
            return Result;

        const float SheetRadius = Profile.BottomRadius + Cirrus.AltitudeKm;
        float       NearRoot    = 0.0f;
        float       FarRoot     = 0.0f;
        if (!IntersectConcentricSphere(ViewerRadiusKm, LocalDirection, SheetRadius, NearRoot, FarRoot))
            return Result;
        const float     HitDistance      = (ViewerRadiusKm < SheetRadius) ? FarRoot : ((NearRoot > 0.0f) ? NearRoot : -1.0f);
        constexpr float SheetFadeStartKm = 150.0f; // [km] - Synoptic sheet extent begins to thin
        constexpr float SheetFadeEndKm   = 320.0f; // [km] - Sheet fully thinned (well inside skyline aerial perspective)
        if (HitDistance <= 0.0f || HitDistance >= SheetFadeEndKm)
            return Result;
        if (ViewerRadiusKm < SheetRadius)
        {
            float GroundNear = 0.0f;
            float GroundFar  = 0.0f;
            if (IntersectConcentricSphere(ViewerRadiusKm, LocalDirection, Profile.BottomRadius, GroundNear, GroundFar) && GroundNear > 0.0f)
                return Result;
        }

        const Vector3f HitPosition { LocalDirection.XCoord * HitDistance, ViewerRadiusKm + LocalDirection.YCoord * HitDistance, LocalDirection.ZCoord * HitDistance };
        const float    Filament    = EvaluateCirrusFilamentDensity(Cirrus, OptionalFlowField, HitPosition, SequenceSeconds);
        if (Filament <= 1.0e-4f)
            return Result;

        const Vector3f SheetNormal = NormalizeVector(HitPosition);
        // Exact chord through the curved ice slab [R_c - h/2, R_c + h/2] (bounded at grazing incidence by planetary curvature).
        const float HalfThickness = 0.5f * std::max(0.05f, Cirrus.SheetThicknessKm);
        float       OuterNear     = 0.0f, OuterFar = 0.0f, InnerNear = 0.0f, InnerFar = 0.0f;
        float       ChordKm       = HalfThickness * 2.0f;
        const bool  OuterHit      = IntersectConcentricSphere(ViewerRadiusKm, LocalDirection, SheetRadius + HalfThickness, OuterNear, OuterFar);
        const bool  InnerHit      = IntersectConcentricSphere(ViewerRadiusKm, LocalDirection, SheetRadius - HalfThickness, InnerNear, InnerFar);
        if (OuterHit && ViewerRadiusKm < SheetRadius - HalfThickness && InnerHit)
            ChordKm = OuterFar - InnerFar;
        else if (OuterHit && ViewerRadiusKm > SheetRadius + HalfThickness)
            ChordKm = (InnerHit && InnerNear > 0.0f) ? (InnerNear - OuterNear) : (OuterFar - OuterNear);
        const float SheetFade = 1.0f - EvaluateSmoothStep(SheetFadeStartKm, SheetFadeEndKm, HitDistance);
        // δ-M scaling (Wiscombe 1977): the ice diffraction spike (fraction f ≈ 0.5 within ~2° of forward) is indistinguishable from
        // unscattered light, so it is removed from the extinction and the residual phase function carries g' ≈ 0.45.
        constexpr float DiffractionFraction = 0.5f;
        const float     OpticalDepth        = Cirrus.OpticalDepthPeak * Filament * SheetFade * std::max(1.0f, ChordKm / (2.0f * HalfThickness))
                                              * (1.0f - DiffractionFraction);
        const float     SheetTransmittance  = std::exp(-OpticalDepth);

        const float        SolarCosine   = DotVector(SheetNormal, SolarAxis);
        const LinearColour SunAtSheet    = MultiplyColour(SolarFlux, SampleTransmittanceToSpace(Tables, Profile, SheetRadius, SolarCosine));
        const float        ScatterCosine = DotVector(LocalDirection, SolarAxis);
        const float        ScatterAngle  = std::acos(std::clamp(ScatterCosine, -1.0f, 1.0f));
        // 22° hexagonal-plate halo: no rays refract inside the 21.7° minimum deviation (sharp inner edge, dark inner sky), and a
        // caustic pile-up just outside it decaying over a few degrees (diffuse outer tail).
        constexpr float HaloMinimumDeviation = 21.7f * Atmosphere::DegreesToRadians;
        const float     HaloSigma            = (ScatterAngle < HaloMinimumDeviation ? 0.30f : 2.2f) * Atmosphere::DegreesToRadians;
        const float     HaloOffset           = (ScatterAngle - HaloMinimumDeviation) / HaloSigma;
        const float     HaloLobe             = 0.17f * std::exp(-HaloOffset * HaloOffset);
        const float     IcePhase             = 0.78f * EvaluateHenyeyGreenstein(ScatterCosine, 0.45f) + 0.22f * EvaluateHenyeyGreenstein(ScatterCosine, -0.20f) + HaloLobe;
        // Prismatic halo dispersion: red inner edge (refraction minimum at 21.7°), bluish outer edge.
        const float HaloDispersion = 0.35f * HaloLobe / std::max(1.0e-4f, IcePhase) * std::clamp(HaloOffset - 0.5f, -1.0f, 1.0f);

        const LinearColour Source{
            SunAtSheet.Red   * IcePhase * (1.0f - HaloDispersion) + AmbientRadiance.Red   * 0.9f,
            SunAtSheet.Green * IcePhase                           + AmbientRadiance.Green * 0.9f,
            SunAtSheet.Blue  * IcePhase * (1.0f + HaloDispersion) + AmbientRadiance.Blue  * 0.9f };

        // Analytic single-scatter sheet integral: L = S ω (1 - e^{-τ}) (energy-bounded for any τ).
        Result.Radiance           = ScaleColour(Source, 0.995f * (1.0f - SheetTransmittance));
        Result.Transmittance      = LinearColour{ SheetTransmittance, SheetTransmittance, SheetTransmittance };
        Result.WeightedDistanceKm = HitDistance;
        return Result;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                     STRATUM DENSITY WITH FLOW ADVECTION (PLANET KM)
    //--------------------------------------------------------------------------------------------------------------------

    [[nodiscard]] float EvaluateStratumAtPlanetPoint(
        const VolumetricCloudConfiguration& CloudConfiguration,
        const AtmosphereUniformBlock&       Profile,
        const Vector3f&                     PlanetPositionKm,
        float                               DeltaX,
        float                               DeltaZ,
        float                               Condensation,
        bool                                DetailEnabled,
        float                               SampleFootprintKm = 0.0f) noexcept
    {
        const float AltitudeKm = VectorLength(PlanetPositionKm) - Profile.BottomRadius;
        return EvaluateStratumDensityAdvected(
            CloudConfiguration.Stratum,
            PlanetPositionKm.XCoord + DeltaX,
            PlanetPositionKm.ZCoord + DeltaZ,
            AltitudeKm,
            Condensation,
            DetailEnabled,
            SampleFootprintKm);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

VolumetricCloudConfiguration BuildCloudGenusPreset(CloudGenusIdentifier Genus, float CoverageFraction) noexcept
{
    VolumetricCloudConfiguration CloudConfiguration {};
    CloudStratumProfile&         Stratum            = CloudConfiguration.Stratum;
    Stratum.Genus                              = Genus;
    Stratum.CoverageFraction                   = std::clamp(CoverageFraction, 0.0f, 1.0f);
    CloudConfiguration.Cirrus.CoverageFraction = 0.22f;

    switch (Genus)
    {
        case CloudGenusIdentifier::Cumulus:
            Stratum.FloorAltitudeKm   = 1.3f;
            Stratum.CeilingAltitudeKm = 3.8f;
            Stratum.ExtinctionPerKm   = 44.0f;
            Stratum.ShapeScaleKm      = 2.2f;
            Stratum.DetailScaleKm     = 0.32f;
            Stratum.ErosionStrength   = 0.45f;
            Stratum.WeatherScaleKm    = 14.0f;
            break;
        case CloudGenusIdentifier::Stratocumulus:
            Stratum.FloorAltitudeKm   = 1.1f;
            Stratum.CeilingAltitudeKm = 2.1f;
            Stratum.ExtinctionPerKm   = 36.0f;
            Stratum.ShapeScaleKm      = 2.2f;
            Stratum.DetailScaleKm     = 0.35f;
            Stratum.ErosionStrength   = 0.38f;
            Stratum.WeatherScaleKm    = 10.0f;
            break;
        case CloudGenusIdentifier::Stratus:
            Stratum.FloorAltitudeKm               = 0.55f;
            Stratum.CeilingAltitudeKm             = 1.25f;
            Stratum.ExtinctionPerKm               = 22.0f;
            Stratum.ShapeScaleKm                  = 6.0f;
            Stratum.DetailScaleKm                 = 0.80f;
            Stratum.ErosionStrength               = 0.18f;
            Stratum.WeatherScaleKm                = 30.0f;
            CloudConfiguration.Cirrus.VeilEnabled = false;
            break;
        case CloudGenusIdentifier::Cumulonimbus:
            Stratum.FloorAltitudeKm              = 1.2f;
            Stratum.CeilingAltitudeKm            = 11.5f;
            Stratum.ExtinctionPerKm              = 52.0f;
            Stratum.ShapeScaleKm                 = 7.5f;
            Stratum.DetailScaleKm                = 0.70f;
            Stratum.ErosionStrength              = 0.40f;
            Stratum.WeatherScaleKm               = 26.0f;
            Stratum.AnvilSpread                  = 0.38f;
            CloudConfiguration.Cirrus.AltitudeKm = 12.4f;
            break;
        case CloudGenusIdentifier::Cirrus:
        default:
            Stratum.CoverageFraction                   = 0.0f;
            CloudConfiguration.Cirrus.VeilEnabled      = true;
            CloudConfiguration.Cirrus.CoverageFraction = std::clamp(CoverageFraction, 0.0f, 1.0f);
            CloudConfiguration.Cirrus.OpticalDepthPeak = 0.75f;
            break;
    }
    return CloudConfiguration;
}

float EvaluateWorleyNoise3D(const Vector3f& Coord, uint32_t Salt) noexcept
{
    return EvaluatePeriodicWorley3D(Coord, 1 << 20, Salt);
}

float EvaluateCloudDensity(
    const VolumetricCloudConfiguration& CloudConfiguration,
    const AtmosphereUniformBlock&       Profile,
    const AtmosphericFlowField*         OptionalFlowField,
    const Vector3f&                     PlanetPositionKm,
    float                               SequenceSeconds,
    bool                                DetailEnabled) noexcept
{
    if (!CloudConfiguration.CloudsEnabled)
        return 0.0f;
    const float AltitudeKm   = VectorLength(PlanetPositionKm) - Profile.BottomRadius;
    float       DeltaX       = 0.0f;
    float       DeltaZ       = 0.0f;
    float       Condensation = 0.0f;
    ResolveFlowDisplacement(OptionalFlowField, PlanetPositionKm, AltitudeKm, SequenceSeconds, DeltaX, DeltaZ, Condensation);
    return EvaluateStratumAtPlanetPoint(CloudConfiguration, Profile, PlanetPositionKm, DeltaX, DeltaZ, Condensation, DetailEnabled);
}

CloudRayResult IntegrateVolumetricClouds(
    const VolumetricCloudConfiguration& CloudConfiguration,
    const AtmosphericLookupTables&      Tables,
    const AtmosphereUniformBlock&       Profile,
    const AtmosphericFlowField*         OptionalFlowField,
    const Vector3f&                     LocalDirection,
    float                               SequenceSeconds,
    uint32_t                            PixelX,
    uint32_t                            PixelY,
    uint32_t                            SampleOrdinal) noexcept
{
    CloudRayResult Result{};
    if (!CloudConfiguration.CloudsEnabled)
        return Result;

    const float        ViewerRadius   = Profile.BottomRadius + Atmosphere::RetrieveViewerAltitudeKm(Profile);
    const Vector3f     ViewerPosition { 0.0f, ViewerRadius, 0.0f };
    const Vector3f     SolarAxis      = NormalizeVector(Vector3f{ Profile.SolarDirection[0], Profile.SolarDirection[1], Profile.SolarDirection[2] });
    const LinearColour SolarFlux      { Profile.SolarIlluminance[0], Profile.SolarIlluminance[1], Profile.SolarIlluminance[2] };

    // Sky ambient: cosine-weighted hemispherical radiance from the baked sky-view table — the upper hemisphere (blue vault + bright
    // skyline) above the stratum, the lower hemisphere (ocean-reflected sky, glint, and low-level airlight) beneath it.
    const LinearColour AmbientTop   = ScaleColour(ResolveHemisphereRadiance(Tables, Profile, SolarAxis, true), CloudConfiguration.AmbientStrength);
    const LinearColour AmbientFloor = ScaleColour(ResolveHemisphereRadiance(Tables, Profile, SolarAxis, false), CloudConfiguration.AmbientStrength);

    // ① Primary stratum span on the curved stratum [R_b + floor, R_b + ceiling].
    LinearColour StratumRadiance      {};
    LinearColour StratumTransmittance { 1.0f, 1.0f, 1.0f };
    float        DistanceWeightSum    = 0.0f;
    float        DistanceWeighted     = 0.0f;

    const CloudStratumProfile& Stratum       = CloudConfiguration.Stratum;
    const float                FloorRadius   = Profile.BottomRadius + Stratum.FloorAltitudeKm;
    const float                CeilingRadius = Profile.BottomRadius + Stratum.CeilingAltitudeKm;

    float SpanStart = -1.0f;
    float SpanEnd   = -1.0f;
    if (Stratum.CoverageFraction > 0.0f)
    {
        float      OuterNear = 0.0f, OuterFar = 0.0f, InnerNear = 0.0f, InnerFar = 0.0f, GroundNear = 0.0f, GroundFar = 0.0f;
        const bool OuterHit  = IntersectConcentricSphere(ViewerRadius, LocalDirection, CeilingRadius, OuterNear, OuterFar);
        const bool InnerHit  = IntersectConcentricSphere(ViewerRadius, LocalDirection, FloorRadius, InnerNear, InnerFar);
        const bool GroundHit = IntersectConcentricSphere(ViewerRadius, LocalDirection, Profile.BottomRadius, GroundNear, GroundFar) && GroundNear > 0.0f;
        if (OuterHit)
        {
            if (ViewerRadius < FloorRadius)
            {
                if (!GroundHit && InnerHit)
                {
                    SpanStart = InnerFar;
                    SpanEnd   = OuterFar;
                }
            }
            else if (ViewerRadius <= CeilingRadius)
            {
                SpanStart = 0.0f;
                SpanEnd   = (InnerHit && InnerNear > 0.0f) ? InnerNear : OuterFar;
            }
            else if (OuterNear > 0.0f)
            {
                SpanStart = OuterNear;
                SpanEnd   = (InnerHit && InnerNear > 0.0f) ? InnerNear : OuterFar;
            }
        }
        SpanEnd = std::min(SpanEnd, SpanStart + CloudConfiguration.MaximumTraceDistanceKm);
    }

    if (SpanEnd > SpanStart && SpanStart >= 0.0f)
    {
        const float Thickness = std::max(0.05f, Stratum.CeilingAltitudeKm - Stratum.FloorAltitudeKm);

        float JitterFirst  = 0.5f;
        float JitterSecond = 0.5f;
        Atmosphere::EvaluateOwenSobolPair(SampleOrdinal, Atmosphere::HashPixelSeed(PixelX, PixelY, 0x51A7u), JitterFirst, JitterSecond);

        const float    CosineScatter = DotVector(LocalDirection, SolarAxis);
        const float    SigmaT        = Stratum.ExtinctionPerKm;
        const float    SigmaS        = SigmaT * std::clamp(Stratum.SingleScatterAlbedo, 0.0f, 1.0f);
        const uint32_t Octaves       = std::clamp(CloudConfiguration.ScatterOctaveCount, 1u, 8u);
        const uint32_t ConeTaps      = std::clamp(CloudConfiguration.SolarConeTapCount, 1u, 12u);
        const uint32_t StepBudget    = std::max(32u, CloudConfiguration.PrimaryStepLimit);

        // Unit-disc offsets for the solar shadow cone taps. The cone keeps one fixed orientation (rotating it per pixel turned the
        // first, nearest taps into surface-scale shading grain); the second Owen-Sobol dimension only jitters the tap radii.
        static constexpr float ConeOffsets[12][2] = {
            {  0.38f,  0.12f }, { -0.22f,  0.41f }, { -0.35f, -0.28f }, {  0.18f, -0.44f }, {  0.47f,  0.36f }, { -0.51f,  0.07f },
            {  0.05f,  0.58f }, {  0.60f, -0.21f }, { -0.30f, -0.62f }, {  0.27f,  0.66f }, { -0.68f,  0.40f }, {  0.71f, -0.55f } };
        const Vector3f SunReferenceAxis = (std::abs(SolarAxis.YCoord) < 0.95f) ? Vector3f{ 0.0f, 1.0f, 0.0f } : Vector3f{ 1.0f, 0.0f, 0.0f };
        const Vector3f SunTangentFixedU = NormalizeVector(CrossVector(SunReferenceAxis, SolarAxis));
        const Vector3f SunTangentFixedV = CrossVector(SolarAxis, SunTangentFixedU);
        const Vector3f SunTangentU      = SunTangentFixedU;
        const Vector3f SunTangentV      = SunTangentFixedV;
        const float    ConeRadiusScale  = 0.14f + 0.08f * JitterSecond;
        const float    ConeLengthKm     = std::min(2.8f, Thickness * 1.1f);

        float AdvectX            = 0.0f;
        float AdvectZ            = 0.0f;
        float Condensation       = 0.0f;
        float LastAdvectDistance = -1.0e9f;

        // Two-speed march: coarse empty-space probes (~1/24 of the stratum, growing with range) until the uneroded field is hit,
        // then a back-step and fine integration at a pixel-footprint-scaled step (~1/96 of the stratum near the viewer). The fine
        // step resolves the ~25 m mean free path of a saturated cell surface, which is what kills the jittered-march speckle.
        float    DistanceKm  = SpanStart + JitterFirst * ResolveCoarseStepKm(Thickness, SpanStart);
        bool     InsideCloud = false;
        uint32_t EmptyRun    = 0u;
        for (uint32_t Step = 0u; Step < StepBudget && DistanceKm < SpanEnd; ++Step)
        {
            const Vector3f SamplePosition = AddVector(ViewerPosition, ScaleVector(LocalDirection, DistanceKm));

            // Semi-Lagrangian RK4 characteristic refreshed every ~1.2 km of ray travel (flow varies on multi-km scales).
            if (DistanceKm - LastAdvectDistance > 1.2f)
            {
                const float SampleAltitude = VectorLength(SamplePosition) - Profile.BottomRadius;
                ResolveFlowDisplacement(OptionalFlowField, SamplePosition, SampleAltitude, SequenceSeconds, AdvectX, AdvectZ, Condensation);
                LastAdvectDistance = DistanceKm;
            }

            // Cheap uneroded probe first (an upper bound of the eroded density, so no cloud is skipped).
            const float Coarse = EvaluateStratumAtPlanetPoint(CloudConfiguration, Profile, SamplePosition, AdvectX, AdvectZ, Condensation, false);
            if (!InsideCloud)
            {
                const float CoarseStepKm = ResolveCoarseStepKm(Thickness, DistanceKm);
                if (Coarse <= 0.0f)
                {
                    DistanceKm += CoarseStepKm;
                    continue;
                }
                InsideCloud = true;
                EmptyRun    = 0u;
                DistanceKm  = std::max(SpanStart, DistanceKm - CoarseStepKm) + JitterFirst * ResolveFineStepKm(Thickness, DistanceKm);
                continue;
            }

            const float StepKm = ResolveFineStepKm(Thickness, DistanceKm);
            if (Coarse <= 0.0f)
            {
                DistanceKm += StepKm;
                if (++EmptyRun >= 6u)
                    InsideCloud = false;
                continue;
            }
            EmptyRun = 0u;
            const float Density = EvaluateStratumAtPlanetPoint(CloudConfiguration, Profile, SamplePosition, AdvectX, AdvectZ, Condensation, true, StepKm);
            if (Density <= 1.0e-4f)
            {
                DistanceKm += StepKm;
                continue;
            }

            // Solar cone shadow taps with geometrically growing spacing. Taps read the uneroded field: the eroded billows are
            // sharper than the diffusion length of in-cloud light (~4 mean free paths), and casting their hard-edged shadows
            // onto neighbouring turrets posterised the crowns into flat light/dark patches.
            float SunOpticalDepth = 0.0f;
            float PreviousTap     = 0.0f;
            for (uint32_t Tap = 0u; Tap < ConeTaps; ++Tap)
            {
                const float TapFraction = (static_cast<float>(Tap) + 1.0f) / static_cast<float>(ConeTaps);
                const float TapDistance = ConeLengthKm * TapFraction * TapFraction;
                const float TapLength   = TapDistance - PreviousTap;
                PreviousTap = TapDistance;
                const float    ConeRadius  = TapDistance * ConeRadiusScale;
                const Vector3f TapPosition = AddVector(
                    AddVector(SamplePosition, ScaleVector(SolarAxis, TapDistance)),
                    AddVector(ScaleVector(SunTangentU, ConeOffsets[Tap][0] * ConeRadius), ScaleVector(SunTangentV, ConeOffsets[Tap][1] * ConeRadius)));
                const float    TapDensity  = EvaluateStratumAtPlanetPoint(CloudConfiguration, Profile, TapPosition, AdvectX, AdvectZ, Condensation, false);
                SunOpticalDepth += TapDensity * SigmaT * TapLength;
            }
            // Distant unresolved shadowing beyond the cone (long-range coarse tap).
            {
                const Vector3f FarTap = AddVector(SamplePosition, ScaleVector(SolarAxis, ConeLengthKm * 2.2f));
                SunOpticalDepth += EvaluateStratumAtPlanetPoint(CloudConfiguration, Profile, FarTap, AdvectX, AdvectZ, Condensation, false) * SigmaT * ConeLengthKm * 1.2f;
            }

            const float        SampleRadius   = VectorLength(SamplePosition);
            const float        LocalSunCosine = DotVector(ScaleVector(SamplePosition, 1.0f / SampleRadius), SolarAxis);
            const LinearColour SunRadiance    = MultiplyColour(SolarFlux, SampleTransmittanceToSpace(Tables, Profile, SampleRadius, LocalSunCosine));
            const float        ScatterSum     = EvaluateScatterOctaves(CloudConfiguration, CosineScatter, SunOpticalDepth, Octaves);
            const float        Powder         = EvaluatePowderFactor(CloudConfiguration, Density, CosineScatter);

            // Sky ambient through the overlying and underlying columns: two coarse taps each way estimate τ_up and τ_down; each
            // hemisphere's cosine-weighted radiance reaches the sample through the conservative two-stream diffuse transmission
            // T = 1 / (1 + ¾ (1 - g) τ) and scatters isotropically (2π · L / 4π = L / 2 per hemisphere).
            const Vector3f UpAxis           = ScaleVector(SamplePosition, 1.0f / SampleRadius);
            const float    CeilingGap       = std::max(0.0f, CeilingRadius - SampleRadius);
            const float    FloorGap         = std::max(0.0f, SampleRadius - FloorRadius);
            float          UpOpticalDepth   = 0.0f;
            float          DownOpticalDepth = 0.0f;
            for (const float TapFraction : { 0.30f, 0.75f })
            {
                const Vector3f UpTap   = AddVector(SamplePosition, ScaleVector(UpAxis, CeilingGap * TapFraction));
                const Vector3f DownTap = AddVector(SamplePosition, ScaleVector(UpAxis, -FloorGap * TapFraction));
                UpOpticalDepth   += EvaluateStratumAtPlanetPoint(CloudConfiguration, Profile, UpTap, AdvectX, AdvectZ, Condensation, false) * SigmaT * CeilingGap * 0.5f;
                DownOpticalDepth += EvaluateStratumAtPlanetPoint(CloudConfiguration, Profile, DownTap, AdvectX, AdvectZ, Condensation, false) * SigmaT * FloorGap * 0.5f;
            }
            const LinearColour Ambient = AddColour(ScaleColour(AmbientTop, 0.5f * EvaluateAmbientDiffuseTransmission(UpOpticalDepth)),
                                                   ScaleColour(AmbientFloor, 0.5f * EvaluateAmbientDiffuseTransmission(DownOpticalDepth)));
            const LinearColour Source  = AddColour(ScaleColour(SunRadiance, ScatterSum * Powder), Ambient);

            const float        SigmaTLocal       = Density * SigmaT;
            const float        StepTransmittance = std::exp(-SigmaTLocal * StepKm);
            const float        StepWeight        = SigmaS / SigmaT * (1.0f - StepTransmittance); // analytic ∫ σ_s e^{-σ_t s} ds / σ_t
            const LinearColour Contribution      = ScaleColour(MultiplyColour(StratumTransmittance, Source), StepWeight);
            StratumRadiance = AddColour(StratumRadiance, Contribution);

            const float Absorbed = StratumTransmittance.Green * (1.0f - StepTransmittance);
            DistanceWeighted  += Absorbed * DistanceKm;
            DistanceWeightSum += Absorbed;
            StratumTransmittance = ScaleColour(StratumTransmittance, StepTransmittance);
            if (StratumTransmittance.Green < 0.004f)
            {
                StratumTransmittance = LinearColour{};
                break;
            }
            DistanceKm += StepKm;
        }
    }
    float StratumDistance = (DistanceWeightSum > 1.0e-5f) ? DistanceWeighted / DistanceWeightSum : 0.0f;

    // ② Hillaire aerial perspective between the viewer and the cloud's transmittance-weighted depth.
    if (DistanceWeightSum > 1.0e-5f && CloudConfiguration.AerialPerspectiveScale > 0.0f)
    {
        LinearColour       SegmentTransmittance {};
        const LinearColour InScatter            = IntegrateAtmosphericSegment(Tables, Profile, ViewerPosition, LocalDirection, StratumDistance, 10u, SegmentTransmittance);
        const float        Opacity              = 1.0f - StratumTransmittance.Green;
        const float        ApScale              = CloudConfiguration.AerialPerspectiveScale;
        const LinearColour Faded                {
            StratumRadiance.Red   * (1.0f + (SegmentTransmittance.Red   - 1.0f) * ApScale),
            StratumRadiance.Green * (1.0f + (SegmentTransmittance.Green - 1.0f) * ApScale),
            StratumRadiance.Blue  * (1.0f + (SegmentTransmittance.Blue  - 1.0f) * ApScale) };
        StratumRadiance = AddColour(Faded, ScaleColour(InScatter, Opacity * ApScale));
    }

    // ③ Cirrus veil (above the stratum for surface viewers; in front for viewers above the sheet).
    const CloudRayResult Veil         = IntegrateCirrusVeil(CloudConfiguration, Tables, Profile, OptionalFlowField, ViewerRadius, LocalDirection, SolarAxis, SolarFlux,
                                                            AmbientTop, SequenceSeconds);
    LinearColour         VeilRadiance = Veil.Radiance;
    if (Veil.WeightedDistanceKm > 0.0f)
    {
        // Aerial perspective in front of the veil: the backdrop's in-scatter between viewer and sheet is never occluded by the
        // sheet, so attenuate the ice radiance and restore that in-scatter in proportion to the sheet opacity.
        LinearColour       ViewToVeil      { 1.0f, 1.0f, 1.0f };
        const LinearColour InScatterToVeil = IntegrateAtmosphericSegment(Tables, Profile, ViewerPosition, LocalDirection, Veil.WeightedDistanceKm, 10u, ViewToVeil);
        const float        VeilOpacity     = 1.0f - Veil.Transmittance.Green;
        VeilRadiance = AddColour(MultiplyColour(VeilRadiance, ViewToVeil), ScaleColour(InScatterToVeil, VeilOpacity));
    }

    const bool VeilInFront = ViewerRadius > Profile.BottomRadius + CloudConfiguration.Cirrus.AltitudeKm;
    if (VeilInFront)
    {
        Result.Radiance = AddColour(VeilRadiance, MultiplyColour(Veil.Transmittance, StratumRadiance));
    }
    else
    {
        Result.Radiance = AddColour(StratumRadiance, MultiplyColour(StratumTransmittance, VeilRadiance));
    }
    Result.Transmittance = MultiplyColour(StratumTransmittance, Veil.Transmittance);
    if (StratumDistance <= 0.0f)
        StratumDistance = Veil.WeightedDistanceKm;
    Result.WeightedDistanceKm = StratumDistance;
    return Result;
}

float EvaluateCloudShadowTransmittance(
    const VolumetricCloudConfiguration& CloudConfiguration,
    const AtmosphereUniformBlock&       Profile,
    const AtmosphericFlowField*         OptionalFlowField,
    const Vector3f&                     PlanetPositionKm,
    float                               SequenceSeconds) noexcept
{
    if (!CloudConfiguration.CloudsEnabled)
        return 1.0f;
    const Vector3f SolarAxis = NormalizeVector(Vector3f{ Profile.SolarDirection[0], Profile.SolarDirection[1], Profile.SolarDirection[2] });
    const float    Radius    = VectorLength(PlanetPositionKm);
    const Vector3f Up        = ScaleVector(PlanetPositionKm, 1.0f / std::max(1.0f, Radius));
    const float    SunCosine = DotVector(Up, SolarAxis);
    if (SunCosine <= 0.01f)
        return 0.0f;

    const CloudStratumProfile& Stratum         = CloudConfiguration.Stratum;
    float                      OpticalDepth    = 0.0f;
    const float                FloorDistance   = std::max(0.0f, (Profile.BottomRadius + Stratum.FloorAltitudeKm - Radius) / SunCosine);
    const float                CeilingDistance = std::max(0.0f, (Profile.BottomRadius + Stratum.CeilingAltitudeKm - Radius) / SunCosine);
    if (Stratum.CoverageFraction > 0.0f && CeilingDistance > FloorDistance)
    {
        constexpr uint32_t ShadowSteps = 16u;
        const float        StepKm      = (CeilingDistance - FloorDistance) / static_cast<float>(ShadowSteps);
        float              AdvectX     = 0.0f, AdvectZ = 0.0f, Condensation = 0.0f;
        const Vector3f     MiddlePoint = AddVector(PlanetPositionKm, ScaleVector(SolarAxis, 0.5f * (FloorDistance + CeilingDistance)));
        ResolveFlowDisplacement(OptionalFlowField, MiddlePoint, VectorLength(MiddlePoint) - Profile.BottomRadius, SequenceSeconds, AdvectX, AdvectZ, Condensation);
        for (uint32_t Step = 0u; Step < ShadowSteps; ++Step)
        {
            const Vector3f SamplePoint = AddVector(PlanetPositionKm, ScaleVector(SolarAxis, FloorDistance + (static_cast<float>(Step) + 0.5f) * StepKm));
            OpticalDepth += EvaluateStratumAtPlanetPoint(CloudConfiguration, Profile, SamplePoint, AdvectX, AdvectZ, Condensation, true, StepKm) * Stratum.ExtinctionPerKm * StepKm;
        }
    }
    if (CloudConfiguration.Cirrus.VeilEnabled && CloudConfiguration.Cirrus.CoverageFraction > 0.0f)
    {
        const float    VeilDistance = std::max(0.0f, (Profile.BottomRadius + CloudConfiguration.Cirrus.AltitudeKm - Radius) / SunCosine);
        const Vector3f VeilPoint    = AddVector(PlanetPositionKm, ScaleVector(SolarAxis, VeilDistance));
        OpticalDepth += CloudConfiguration.Cirrus.OpticalDepthPeak * EvaluateCirrusFilamentDensity(CloudConfiguration.Cirrus, OptionalFlowField, VeilPoint, SequenceSeconds) / SunCosine;
    }
    return std::exp(-OpticalDepth);
}

void DenoiseCloudLayerBilateral(
    uint32_t                   Width,
    uint32_t                   Height,
    std::vector<LinearColour>& InOutRadiance,
    std::vector<LinearColour>& InOutTransmittance,
    const std::vector<float>&  DistanceGuideKm)
{
    const std::size_t PixelCount = static_cast<std::size_t>(Width) * Height;
    if (InOutRadiance.size() < PixelCount || InOutTransmittance.size() < PixelCount || DistanceGuideKm.size() < PixelCount)
        return;

    std::vector<LinearColour> FilteredRadiance(PixelCount);
    std::vector<LinearColour> FilteredTransmittance(PixelCount);
    constexpr int32_t Radius       = 2;
    constexpr float   SpatialSigma = 1.4f;

    for (uint32_t Row = 0u; Row < Height; ++Row)
    {
        for (uint32_t Column = 0u; Column < Width; ++Column)
        {
            const std::size_t Centre              = static_cast<std::size_t>(Row) * Width + Column;
            const float       CentreDistance      = DistanceGuideKm[Centre];
            const float       CentreTransmittance = InOutTransmittance[Centre].Green;
            const float       DistanceTolerance   = 0.12f * CentreDistance + 0.25f;

            LinearColour RadianceSum      {};
            LinearColour TransmittanceSum {};
            float        WeightSum        = 0.0f;
            for (int32_t OffsetY = -Radius; OffsetY <= Radius; ++OffsetY)
            {
                const int32_t SampleRow = static_cast<int32_t>(Row) + OffsetY;
                if (SampleRow < 0 || SampleRow >= static_cast<int32_t>(Height))
                    continue;
                for (int32_t OffsetX = -Radius; OffsetX <= Radius; ++OffsetX)
                {
                    const int32_t SampleColour = static_cast<int32_t>(Column) + OffsetX;
                    if (SampleColour < 0 || SampleColour >= static_cast<int32_t>(Width))
                        continue;
                    const std::size_t Neighbour          = static_cast<std::size_t>(SampleRow) * Width + static_cast<std::size_t>(SampleColour);
                    const float       SpatialWeight      = std::exp(-static_cast<float>(OffsetX * OffsetX + OffsetY * OffsetY) / (2.0f * SpatialSigma * SpatialSigma));
                    const float       DistanceDelta      = (DistanceGuideKm[Neighbour] - CentreDistance) / DistanceTolerance;
                    const float       TransmittanceDelta = (InOutTransmittance[Neighbour].Green - CentreTransmittance) / 0.18f;
                    const float       Weight             = SpatialWeight * std::exp(-DistanceDelta * DistanceDelta - TransmittanceDelta * TransmittanceDelta);
                    RadianceSum      = AddColour(RadianceSum, ScaleColour(InOutRadiance[Neighbour], Weight));
                    TransmittanceSum = AddColour(TransmittanceSum, ScaleColour(InOutTransmittance[Neighbour], Weight));
                    WeightSum  += Weight;
                }
            }
            const float Inverse = 1.0f / std::max(1.0e-6f, WeightSum);
            FilteredRadiance[Centre]      = ScaleColour(RadianceSum, Inverse);
            FilteredTransmittance[Centre] = ScaleColour(TransmittanceSum, Inverse);
        }
    }
    InOutRadiance.swap(FilteredRadiance);
    InOutTransmittance.swap(FilteredTransmittance);
}

void EncodeCloudFieldUniformBlock(
    const VolumetricCloudConfiguration& CloudConfiguration,
    float                               SequenceSeconds,
    CloudFieldUniformBlock&             OutUniforms) noexcept
{
    const CloudStratumProfile& Stratum         = CloudConfiguration.Stratum;
    const CirrusVeilProfile&   Cirrus          = CloudConfiguration.Cirrus;
    const float                StratumCoverage = CloudConfiguration.CloudsEnabled ? Stratum.CoverageFraction : 0.0f;
    Atmosphere::EncodeFloat4(OutUniforms.StratumGeometryPack, Stratum.FloorAltitudeKm, Stratum.CeilingAltitudeKm, StratumCoverage,
                             static_cast<float>(static_cast<uint32_t>(Stratum.Genus)));
    Atmosphere::EncodeFloat4(OutUniforms.StratumNoisePack, Stratum.ShapeScaleKm, Stratum.DetailScaleKm, Stratum.ErosionStrength, Stratum.WeatherScaleKm);
    Atmosphere::EncodeFloat4(OutUniforms.StratumOpticsPack, Stratum.ExtinctionPerKm, Stratum.SingleScatterAlbedo, Stratum.AnvilSpread, CloudConfiguration.PowderStrength);
    Atmosphere::EncodeFloat4(OutUniforms.PhasePack, CloudConfiguration.ForwardAsymmetry, CloudConfiguration.BackwardAsymmetry, CloudConfiguration.ForwardLobeWeight,
                             static_cast<float>(CloudConfiguration.ScatterOctaveCount));
    Atmosphere::EncodeFloat4(OutUniforms.CirrusPack, Cirrus.AltitudeKm, Cirrus.CoverageFraction, Cirrus.OpticalDepthPeak,
                             (CloudConfiguration.CloudsEnabled && Cirrus.VeilEnabled) ? Cirrus.FilamentScaleKm : 0.0f);
    Atmosphere::EncodeFloat4(OutUniforms.QualityPack, static_cast<float>(CloudConfiguration.PrimaryStepLimit), static_cast<float>(CloudConfiguration.SolarConeTapCount),
                             CloudConfiguration.MaximumTraceDistanceKm, SequenceSeconds);
}

} // namespace Frontier
