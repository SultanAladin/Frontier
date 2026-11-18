/*==============================================================================================================================================
                                                  ATMOSPHERECELESTIALVALIDATIONENTRY.CPP
==============================================================================================================================================*/
// 🧩 Standalone CPU validation for the atmosphere / cloud / celestial solvers: strict quantitative physics gates (transmittance, Rayleigh
//    chromaticity, ozone tent, flow divergence, RK4 reversibility, Pogson photometry, Planckian chromaticity, scintillation in vacuum, lunar
//    occlusion / umbra / opposition surge, limb darkening, corona monotonicity, cloud coverage / shadow / energy bounds, fog Beer-Lambert /
//    analytic height integral / flow coupling / energy bounds, solar glare, UBO layouts) and an 18-exhibit 1280x720 PNG gallery written to
//    "Audit Proof/". Exit code 0 only when every gate passes.

#include "Graphics/Atmosphere/AtmosphereProfile.h"
#include "Graphics/Atmosphere/AtmosphericFlowField.h"
#include "Graphics/Atmosphere/AtmosphericScatteringSolver.h"
#include "Graphics/Atmosphere/CelestialFieldSolver.h"
#include "Graphics/Atmosphere/VolumetricCloudSolver.h"
#include "Graphics/Atmosphere/VolumetricFogSolver.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

using namespace Frontier;

namespace
{
    constexpr uint32_t ExhibitWidth  = 1280u; // [px] - Gallery exhibit width
    constexpr uint32_t ExhibitHeight = 720u;  // [px] - Gallery exhibit height

    //--------------------------------------------------------------------------------------------------------------------
    //                                        MINIMAL PNG ENCODER (STORED DEFLATE)
    //--------------------------------------------------------------------------------------------------------------------

    [[nodiscard]] uint32_t EvaluateCrc32(const uint8_t* Bytes, std::size_t Length, uint32_t Running = 0xFFFFFFFFu) noexcept
    {
        static const std::array<uint32_t, 256> Table = []() noexcept
        {
            std::array<uint32_t, 256> Constructed{};
            for (uint32_t Entry = 0u; Entry < 256u; ++Entry)
            {
                uint32_t Remainder = Entry;
                for (uint32_t Bit = 0u; Bit < 8u; ++Bit)
                    Remainder = (Remainder & 1u) ? (0xEDB88320u ^ (Remainder >> 1u)) : (Remainder >> 1u);
                Constructed[Entry] = Remainder;
            }
            return Constructed;
        }();
        for (std::size_t Cursor = 0u; Cursor < Length; ++Cursor)
            Running = Table[(Running ^ Bytes[Cursor]) & 0xFFu] ^ (Running >> 8u);
        return Running;
    }

    void AppendBigEndian32(std::vector<uint8_t>& Stream, uint32_t Word)
    {
        Stream.push_back(static_cast<uint8_t>(Word >> 24u));
        Stream.push_back(static_cast<uint8_t>(Word >> 16u));
        Stream.push_back(static_cast<uint8_t>(Word >> 8u));
        Stream.push_back(static_cast<uint8_t>(Word));
    }

    void AppendPngChunk(std::vector<uint8_t>& Stream, const char* ChunkTag, const std::vector<uint8_t>& Payload)
    {
        AppendBigEndian32(Stream, static_cast<uint32_t>(Payload.size()));
        const std::size_t TagOffset = Stream.size();
        Stream.insert(Stream.end(), ChunkTag, ChunkTag + 4);
        Stream.insert(Stream.end(), Payload.begin(), Payload.end());
        const uint32_t Crc = EvaluateCrc32(Stream.data() + TagOffset, Payload.size() + 4u) ^ 0xFFFFFFFFu;
        AppendBigEndian32(Stream, Crc);
    }

    [[nodiscard]] bool EncodePngFile(const std::string& FilePath, uint32_t Width, uint32_t Height, const std::vector<uint8_t>& ColourBytes)
    {
        std::vector<uint8_t> Scanlines;
        Scanlines.reserve(static_cast<std::size_t>(Height) * (Width * 3u + 1u));
        for (uint32_t Row = 0u; Row < Height; ++Row)
        {
            Scanlines.push_back(0u);
            const uint8_t* RowStart = ColourBytes.data() + static_cast<std::size_t>(Row) * Width * 3u;
            Scanlines.insert(Scanlines.end(), RowStart, RowStart + Width * 3u);
        }

        std::vector<uint8_t> Zlib   { 0x78u, 0x01u };
        std::size_t          Offset = 0u;
        while (Offset < Scanlines.size())
        {
            const std::size_t BlockLength = std::min<std::size_t>(65535u, Scanlines.size() - Offset);
            const bool        FinalBlock  = (Offset + BlockLength) == Scanlines.size();
            Zlib.push_back(FinalBlock ? 1u : 0u);
            Zlib.push_back(static_cast<uint8_t>(BlockLength & 0xFFu));
            Zlib.push_back(static_cast<uint8_t>(BlockLength >> 8u));
            Zlib.push_back(static_cast<uint8_t>(~BlockLength & 0xFFu));
            Zlib.push_back(static_cast<uint8_t>((~BlockLength >> 8u) & 0xFFu));
            Zlib.insert(Zlib.end(), Scanlines.begin() + static_cast<std::ptrdiff_t>(Offset), Scanlines.begin() + static_cast<std::ptrdiff_t>(Offset + BlockLength));
            Offset += BlockLength;
        }
        uint32_t AdlerLow  = 1u;
        uint32_t AdlerHigh = 0u;
        for (uint8_t Byte : Scanlines)
        {
            AdlerLow  = (AdlerLow + Byte) % 65521u;
            AdlerHigh = (AdlerHigh + AdlerLow) % 65521u;
        }
        AppendBigEndian32(Zlib, (AdlerHigh << 16u) | AdlerLow);

        std::vector<uint8_t> Stream{ 0x89u, 'P', 'N', 'G', 0x0Du, 0x0Au, 0x1Au, 0x0Au };
        std::vector<uint8_t> Header;
        AppendBigEndian32(Header, Width);
        AppendBigEndian32(Header, Height);
        Header.insert(Header.end(), { 8u, 2u, 0u, 0u, 0u });
        AppendPngChunk(Stream, "IHDR", Header);
        AppendPngChunk(Stream, "IDAT", Zlib);
        AppendPngChunk(Stream, "IEND", {});

        std::FILE* Output = std::fopen(FilePath.c_str(), "wb");
        if (Output == nullptr)
            return false;
        const bool Written = std::fwrite(Stream.data(), 1u, Stream.size(), Output) == Stream.size();
        std::fclose(Output);
        return Written;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                               VALIDATION GATES
    //--------------------------------------------------------------------------------------------------------------------

    struct GateLedger
    {
        uint32_t PassedCount = 0u;
        uint32_t FailedCount = 0u;
    };

    void EnforceGate(GateLedger& Ledger, bool Condition, const char* GateName, const char* Detail, double Measured)
    {
        if (Condition)
            ++Ledger.PassedCount;
        else
            ++Ledger.FailedCount;
        std::printf("  [%s] %-58s %-44s = %.6g\n", Condition ? "PASS" : "FAIL", GateName, Detail, Measured);
    }

    [[nodiscard]] Vector3f ConstructDirection(float ElevationDegrees, float AzimuthDegrees) noexcept
    {
        const float Elevation = ElevationDegrees * Atmosphere::DegreesToRadians;
        const float Azimuth   = AzimuthDegrees * Atmosphere::DegreesToRadians;
        return Vector3f{ std::cos(Elevation) * std::cos(Azimuth), std::sin(Elevation), std::cos(Elevation) * std::sin(Azimuth) };
    }

    void ValidatePhysics(GateLedger& Ledger, const AtmosphericLookupTables& FoundationTables, const LunarSurfaceAtlas& Atlas)
    {
        std::printf("\n== Physics gates ==\n");
        AtmosphereUniformBlock Profile = Atmosphere::ConstructEarthProfile();

        // ① UBO std140 layouts.
        EnforceGate(Ledger, sizeof(AtmosphereUniformBlock) == 208u, "AtmosphereUniformBlock std140 size", "bytes == 208", static_cast<double>(sizeof(AtmosphereUniformBlock)));
        EnforceGate(Ledger, sizeof(FlowFieldUniformBlock) == 560u, "FlowFieldUniformBlock std140 size", "bytes == 560", static_cast<double>(sizeof(FlowFieldUniformBlock)));
        EnforceGate(Ledger, sizeof(CelestialUniformBlock) == 608u, "CelestialUniformBlock std140 size", "bytes == 608", static_cast<double>(sizeof(CelestialUniformBlock)));
        EnforceGate(Ledger, sizeof(CloudFieldUniformBlock) == 96u, "CloudFieldUniformBlock std140 size", "bytes == 96", static_cast<double>(sizeof(CloudFieldUniformBlock)));

        // ② Ozone Chappuis tent: peak 1 at 25 km, 0.5 at 17.5 / 32.5 km, zero below 10 km and above 40 km.
        LinearColour Rayleigh{}, Mie{}, Extinction25{}, Extinction45{}, Extinction325{};
        EvaluateMediumCoefficients(Profile, 25.0f, Rayleigh, Mie, Extinction25);
        const float Ozone25 = Extinction25.Blue - Rayleigh.Blue - Profile.MieExtinctionSeaLevel[2] * std::exp(-25.0f / 1.2f);
        EvaluateMediumCoefficients(Profile, 32.5f, Rayleigh, Mie, Extinction325);
        const float Ozone325 = Extinction325.Green - Rayleigh.Green - Profile.MieExtinctionSeaLevel[1] * std::exp(-32.5f / 1.2f);
        EvaluateMediumCoefficients(Profile, 45.0f, Rayleigh, Mie, Extinction45);
        const float Ozone45 = Extinction45.Green - Rayleigh.Green - Profile.MieExtinctionSeaLevel[1] * std::exp(-45.0f / 1.2f);
        EnforceGate(Ledger, std::abs(Ozone25 / Profile.OzoneAbsorptionPeak[2] - 1.0f) < 0.02f, "Ozone tent peak density at 25 km", "|rho - 1| < 0.02", Ozone25 / Profile.OzoneAbsorptionPeak[2]);
        EnforceGate(Ledger, std::abs(Ozone325 / Profile.OzoneAbsorptionPeak[1] - 0.5f) < 0.02f, "Ozone tent half density at 32.5 km", "|rho - 0.5| < 0.02", Ozone325 / Profile.OzoneAbsorptionPeak[1]);
        EnforceGate(Ledger, std::abs(Ozone45) < 1.0e-9f, "Ozone tent vanishes above 40 km", "|beta_O3| < 1e-9", Ozone45);

        // ③ Transmittance: zenith sea-level T(550) in [0.84, 0.93]; skyline far lower; blue attenuates more than red.
        const LinearColour OverheadTransmittance = SampleTransmittanceToSpace(FoundationTables, Profile, Profile.BottomRadius + 0.01f, 1.0f);
        const LinearColour SkylineTransmittance  = SampleTransmittanceToSpace(FoundationTables, Profile, Profile.BottomRadius + 0.01f, 0.02f);
        EnforceGate(Ledger, OverheadTransmittance.Green > 0.84f && OverheadTransmittance.Green < 0.93f, "Sea-level vertical transmittance (G)", "0.84 < T < 0.93", OverheadTransmittance.Green);
        EnforceGate(Ledger, OverheadTransmittance.Blue < OverheadTransmittance.Green && OverheadTransmittance.Green < OverheadTransmittance.Red, "Vertical transmittance chromatic order", "T_B < T_G < T_R", OverheadTransmittance.Red - OverheadTransmittance.Blue);
        EnforceGate(Ledger, SkylineTransmittance.Green < 0.35f * OverheadTransmittance.Green, "Grazing transmittance << vertical", "T(mu=0.02) < 0.35 T(1)", SkylineTransmittance.Green / OverheadTransmittance.Green);
        const LinearColour BelowLimb = SampleTransmittanceToSpace(FoundationTables, Profile, Profile.BottomRadius + 0.5f, -0.3f);
        EnforceGate(Ledger, BelowLimb.Green == 0.0f, "Ground-occluded transmittance is zero", "T == 0", BelowLimb.Green);

        // ④ Rayleigh chromaticity: noon zenith is blue; sunset skyline near the sun is red-dominant.
        AtmosphericLookupTables NoonTables = FoundationTables;
        Atmosphere::ConfigureSolarDirection(Profile, 60.0f * Atmosphere::DegreesToRadians, 0.0f);
        Atmosphere::ConfigureViewerAltitudeKm(Profile, 0.2f);
        SynthesizeSkyViewTable(Profile, NoonTables);
        const LinearColour NoonOverhead = SampleSkyViewRadiance(NoonTables, Profile, Vector3f{ 0.0f, 1.0f, 0.0f });
        EnforceGate(Ledger, NoonOverhead.Blue > NoonOverhead.Green && NoonOverhead.Green > NoonOverhead.Red, "Noon vertical sky is Rayleigh blue", "B > G > R", NoonOverhead.Blue / std::max(1e-6f, NoonOverhead.Red));
        const LinearColour NoonSkyline = SampleSkyViewRadiance(NoonTables, Profile, ConstructDirection(1.0f, 180.0f));
        EnforceGate(Ledger, EvaluateRelativeLuminance(NoonSkyline) > EvaluateRelativeLuminance(NoonOverhead), "Skyline brighter than vertical sky (noon)", "L_skyline > L_vertical", EvaluateRelativeLuminance(NoonSkyline) / EvaluateRelativeLuminance(NoonOverhead));

        AtmosphericLookupTables SunsetTables = FoundationTables;
        Atmosphere::ConfigureSolarDirection(Profile, 0.8f * Atmosphere::DegreesToRadians, 0.0f);
        SynthesizeSkyViewTable(Profile, SunsetTables);
        const LinearColour SunsetGlow = SampleSkyViewRadiance(SunsetTables, Profile, ConstructDirection(2.0f, 4.0f));
        EnforceGate(Ledger, SunsetGlow.Red > SunsetGlow.Blue, "Sunset glow near sun is red-dominant", "R > B", SunsetGlow.Red / std::max(1e-6f, SunsetGlow.Blue));
        const float SeamLeft  = SampleSkyViewRadiance(NoonTables, Profile, ConstructDirection(20.0f, 179.99f)).Blue;
        const float SeamRight = SampleSkyViewRadiance(NoonTables, Profile, ConstructDirection(20.0f, -179.99f)).Blue;
        EnforceGate(Ledger, std::abs(SeamLeft - SeamRight) / std::max(1e-6f, SeamLeft) < 0.01f, "SkyView azimuth seam continuity", "|dL|/L < 1%", std::abs(SeamLeft - SeamRight) / SeamLeft);
        const float LimbUpper = EvaluateRelativeLuminance(SampleSkyViewRadiance(NoonTables, Profile, ConstructDirection(0.05f, 90.0f)));
        const float LimbLower = EvaluateRelativeLuminance(SampleSkyViewRadiance(NoonTables, Profile, ConstructDirection(-0.6f, 90.0f)));
        EnforceGate(Ledger, LimbLower / LimbUpper > 0.25f && LimbLower / LimbUpper < 1.6f, "Geometric limb C0 continuity (sky vs ocean)", "0.25 < L_lo/L_up < 1.6", LimbLower / LimbUpper);
        const LinearColour Ocean = SampleSkyViewRadiance(NoonTables, Profile, ConstructDirection(-30.0f, 90.0f));
        EnforceGate(Ledger, Ocean.Blue > Ocean.Red, "Lower hemisphere oceanic (no brown/beige)", "B > R", Ocean.Blue / std::max(1e-6f, Ocean.Red));

        // ⑤ Solar disc: Neckel-Labs limb darkening (centre > limb; blue darkens more) + monotonic corona.
        Atmosphere::ConfigureSolarDirection(Profile, 60.0f * Atmosphere::DegreesToRadians, 0.0f);
        const auto DiscAt = [&](float OffsetRadians) noexcept
        {
            return EvaluateSolarRadiometricDisc(NoonTables, Profile, ConstructDirection(60.0f + OffsetRadians * Atmosphere::RadiansToDegrees, 0.0f), 0.00465f, 160.0f, 0.2f, true);
        };
        const LinearColour DiscCentre = DiscAt(0.0f);
        const LinearColour DiscLimb   = DiscAt(0.00465f * 0.95f);
        EnforceGate(Ledger, DiscLimb.Green < 0.75f * DiscCentre.Green, "Solar limb darkening (95% radius)", "L_limb < 0.75 L_centre", DiscLimb.Green / DiscCentre.Green);
        EnforceGate(Ledger, DiscLimb.Blue / DiscCentre.Blue < DiscLimb.Red / DiscCentre.Red, "Limb darkening stronger in blue", "ratio_B < ratio_R", (DiscLimb.Blue / DiscCentre.Blue) / (DiscLimb.Red / DiscCentre.Red));
        bool  CoronaMonotonic = true;
        float PreviousCorona  = 1.0e30f;
        for (float Radii = 1.6f; Radii < 46.0f; Radii += 0.4f)
        {
            const float Corona = DiscAt(0.00465f * Radii).Green;
            CoronaMonotonic = CoronaMonotonic && (Corona <= PreviousCorona + 1.0e-7f);
            PreviousCorona  = Corona;
        }
        EnforceGate(Ledger, CoronaMonotonic, "K+F corona strictly monotonic (1.6..46 radii)", "dL/dtheta <= 0", PreviousCorona);

        // ⑥ Flow field: divergence-free curl and RK4 backward-forward reversibility.
        // The curl is a central-difference stencil (step h = 0.035 wavelengths) over a smooth vector potential, so its divergence is
        // identically zero under the matching discrete operator and O(h^2) under the continuum operator.
        const auto MeasureDivergence = [](float DeltaMetres) noexcept
        {
            double DivergenceSum = 0.0;
            double ScaleSum      = 0.0;
            for (uint32_t Probe = 0u; Probe < 64u; ++Probe)
            {
                const Vector3f Point      { 317.0f * Probe, -211.0f * Probe + 900.0f, 1500.0f + 37.0f * Probe };
                const auto     Curl       = [&](const Vector3f& Offset) noexcept { return EvaluateDivergenceFreeCurl3D(AddVector(Point, Offset), 2800.0f, 12.0f); };
                const Vector3f PlusX      = Curl(Vector3f{ DeltaMetres, 0.0f, 0.0f });
                const Vector3f MinusX     = Curl(Vector3f{ -DeltaMetres, 0.0f, 0.0f });
                const Vector3f PlusY      = Curl(Vector3f{ 0.0f, DeltaMetres, 0.0f });
                const Vector3f MinusY     = Curl(Vector3f{ 0.0f, -DeltaMetres, 0.0f });
                const Vector3f PlusZ      = Curl(Vector3f{ 0.0f, 0.0f, DeltaMetres });
                const Vector3f MinusZ     = Curl(Vector3f{ 0.0f, 0.0f, -DeltaMetres });
                const double   Divergence = (PlusX.XCoord - MinusX.XCoord) + (PlusY.YCoord - MinusY.YCoord) + (PlusZ.ZCoord - MinusZ.ZCoord);
                const double   Gradient   = std::abs(PlusX.XCoord - MinusX.XCoord) + std::abs(PlusY.YCoord - MinusY.YCoord) + std::abs(PlusZ.ZCoord - MinusZ.ZCoord);
                DivergenceSum += std::abs(Divergence);
                ScaleSum      += Gradient;
            }
            return DivergenceSum / std::max(1.0e-12, ScaleSum);
        };
        const double DiscreteDivergence  = MeasureDivergence(0.035f * 2800.0f);
        const double ContinuumDivergence = MeasureDivergence(4.0f);
        EnforceGate(Ledger, DiscreteDivergence < 1.0e-3, "Curl field discretely divergence-free", "sum|div| / sum|du_i/dx_i| < 1e-3", DiscreteDivergence);
        EnforceGate(Ledger, ContinuumDivergence < 0.15, "Curl field continuum divergence O(h^2)", "sum|div| / sum|du_i/dx_i| < 0.15", ContinuumDivergence);

        AtmosphericFlowField Flow  {};
        FlowSourceDescriptor Swirl {};
        Swirl.Category          = FlowSourceCategory::CyclonicSwirl;
        Swirl.ActivationEnabled = true;
        Swirl.WorldCentreMetres = Vector3f{ 0.0f, 0.0f, 2500.0f };
        Swirl.OuterRadiusMetres = 12000.0f;
        RegisterFlowSource(Flow, Swirl);
        const Vector3f           Origin   { 3200.0f, -1800.0f, 2300.0f };
        const FlowAdvectedSample Backward = TraceFlowCharacteristicBackward(Flow, Origin, 60.0f, 16u);
        EnforceGate(Ledger, Backward.DisplacementMetres > 100.0f, "RK4 backward characteristic displaces parcel", "|x - X| > 100 m", Backward.DisplacementMetres);
        // Forward-integrate the recovered source with RK4 (u evaluated along increasing time) and require closure.
        Vector3f           Parcel       = Backward.SourcePositionMetres;
        constexpr uint32_t ForwardSteps = 240u;
        const float        StepSeconds  = 60.0f / ForwardSteps;
        for (uint32_t Step = 0u; Step < ForwardSteps; ++Step)
        {
            const float    Clock = Step * StepSeconds;
            const Vector3f K1    = EvaluateAtmosphericFlow(Flow, Parcel, Clock).VelocityMetresPerSec;
            const Vector3f K2    = EvaluateAtmosphericFlow(Flow, AddVector(Parcel, ScaleVector(K1, 0.5f * StepSeconds)), Clock + 0.5f * StepSeconds).VelocityMetresPerSec;
            const Vector3f K3    = EvaluateAtmosphericFlow(Flow, AddVector(Parcel, ScaleVector(K2, 0.5f * StepSeconds)), Clock + 0.5f * StepSeconds).VelocityMetresPerSec;
            const Vector3f K4    = EvaluateAtmosphericFlow(Flow, AddVector(Parcel, ScaleVector(K3, StepSeconds)), Clock + StepSeconds).VelocityMetresPerSec;
            Parcel = AddVector(Parcel, ScaleVector(AddVector(AddVector(K1, ScaleVector(K2, 2.0f)), AddVector(ScaleVector(K3, 2.0f), K4)), StepSeconds / 6.0f));
        }
        const float ClosureError = VectorLength(SubtractVector(Parcel, Origin));
        EnforceGate(Ledger, ClosureError < 0.03f * Backward.DisplacementMetres + 5.0f, "RK4 characteristic reversibility", "|fwd(bwd(x)) - x| < 3% travel", ClosureError);

        // ⑦ Stellar photometry & chromaticity.
        const LinearColour HotStar  = ConvertColourIndexToLinearRec709(-0.30f);
        const LinearColour SunStar  = ConvertColourIndexToLinearRec709(0.65f);
        const LinearColour CoolStar = ConvertColourIndexToLinearRec709(1.85f);
        EnforceGate(Ledger, HotStar.Blue > HotStar.Red, "B-V = -0.30 star is blue-white", "B > R", HotStar.Blue / HotStar.Red);
        EnforceGate(Ledger, CoolStar.Red > 2.0f * CoolStar.Blue, "B-V = +1.85 star is orange-red", "R > 2B", CoolStar.Red / std::max(1e-6f, CoolStar.Blue));
        EnforceGate(Ledger, std::abs(EvaluateRelativeLuminance(SunStar) - 1.0f) < 1.0e-3f, "Planckian chromaticity unit luminance", "|Y - 1| < 1e-3", EvaluateRelativeLuminance(SunStar));

        StarfieldConfiguration Stars{};
        Stars.MilkyWayEnabled          = false;
        Stars.NebulaeEnabled           = false;
        Stars.CelestialRotationRadians = 1.7f;
        Stars.SiteLatitudeRadians      = -0.46f;
        // Sirius (V -1.46) vs Adhara (V 1.50): locate both by brute-force peak search around catalogue directions in the site frame.
        const auto PeakNear = [&](float RightAscensionHours, float DeclinationDegrees, float SequenceSeconds, bool Vacuum)
        {
            // Equatorial -> site-local (mirrors the solver rotation).
            const float    Latitude       = Stars.SiteLatitudeRadians;
            const Vector3f Meridian       { 0.0f, std::cos(Latitude), -std::sin(Latitude) };
            const Vector3f West           { -1.0f, 0.0f, 0.0f };
            const Vector3f Pole           { 0.0f, std::sin(Latitude), std::cos(Latitude) };
            const float    RightAscension = RightAscensionHours * (Atmosphere::TauConstant / 24.0f);
            const float    Declination    = DeclinationDegrees * Atmosphere::DegreesToRadians;
            const float    HourAngle      = Stars.CelestialRotationRadians - RightAscension;
            const Vector3f Local          = AddVector(AddVector(ScaleVector(Meridian, std::cos(Declination) * std::cos(HourAngle)),
                                                          ScaleVector(West, std::cos(Declination) * std::sin(HourAngle))),
                                                ScaleVector(Pole, std::sin(Declination)));
            return EvaluateStarfieldGalaxy(Stars, NormalizeVector(Local), SequenceSeconds, Vacuum);
        };
        const LinearColour Sirius         = PeakNear(6.752f, -16.716f, 0.0f, true);
        const LinearColour Adhara         = PeakNear(6.977f, -28.972f, 0.0f, true);
        const float        PogsonRatio    = EvaluateRelativeLuminance(Sirius) / std::max(1e-12f, EvaluateRelativeLuminance(Adhara));
        const float        PogsonExpected = std::pow(10.0f, 0.4f * (1.50f + 1.46f));
        EnforceGate(Ledger, std::abs(PogsonRatio / PogsonExpected - 1.0f) < 0.05f, "Pogson flux ratio Sirius / Adhara", "|ratio / 10^(0.4 dm) - 1| < 5%", PogsonRatio / PogsonExpected);
        const LinearColour VacuumA = PeakNear(6.752f, -16.716f, 1.0f, true);
        const LinearColour VacuumB = PeakNear(6.752f, -16.716f, 7.3f, true);
        EnforceGate(Ledger, VacuumA.Green == VacuumB.Green && VacuumA.Green == Sirius.Green, "Scintillation strictly zero in vacuum", "L(t1) == L(t2)", std::abs(VacuumA.Green - VacuumB.Green));
        float TwinkleMinimum = 1.0e30f, TwinkleMaximum = 0.0f;
        for (uint32_t Tick = 0u; Tick < 40u; ++Tick)
        {
            const float Sample = PeakNear(6.752f, -16.716f, 0.05f * Tick, false).Green;
            TwinkleMinimum = std::min(TwinkleMinimum, Sample);
            TwinkleMaximum = std::max(TwinkleMaximum, Sample);
        }
        EnforceGate(Ledger, TwinkleMaximum > 1.03f * TwinkleMinimum, "Atmospheric scintillation modulates flux", "max > 1.03 min", TwinkleMaximum / TwinkleMinimum);

        // ⑧ Lunar system: atlas decode, solid occlusion, off-limb atmosphere, umbra, opposition surge.
        EnforceGate(Ledger, Atlas.DecodedAssetCount == MaximumLunarBodyCount, "Six authentic lunar maps decoded", "count == 6", Atlas.DecodedAssetCount);
        const LinearColour   Flux      { 12.8f, 12.8f, 12.8f };
        const Vector3f       MoonAxis  { 0.0f, 0.0f, 1.0f };
        const Vector3f       SideSun   = NormalizeVector(Vector3f{ 1.0f, 0.0f, 0.0f });
        LunarBodyDescriptor  Shroud    = BuildLunarPreset(LunarPresetIdentifier::Shroud, MoonAxis, 0.05f);
        const LunarRayResult CentreHit = EvaluateSingleLunarBody(Shroud, &Atlas, MoonAxis, SideSun, Flux);
        EnforceGate(Ledger, CentreHit.SolidDiscHit && CentreHit.TransmittanceToStars.Green == 0.0f, "Solid moon disc occludes stars", "T_stars == 0", CentreHit.TransmittanceToStars.Green);
        const float          LimbAngle = std::asin((Shroud.Atmosphere.BodyRadiusKm + 150.0f) * std::sin(0.05f) / Shroud.Atmosphere.BodyRadiusKm);
        const LunarRayResult LitLimb   = EvaluateSingleLunarBody(Shroud, &Atlas, NormalizeVector(Vector3f{ std::sin(LimbAngle), 0.0f, std::cos(LimbAngle) }), SideSun, Flux);
        EnforceGate(Ledger, LitLimb.OffLimbAtmosphereHit && LitLimb.TransmittanceToStars.Green < 0.98f && LitLimb.TransmittanceToStars.Green > 0.0f,
                    "Off-limb 3D lunar atmosphere attenuates stars", "0 < T < 0.98", LitLimb.TransmittanceToStars.Green);
        EnforceGate(Ledger, EvaluateRelativeLuminance(LitLimb.Radiance) > 1.0e-3f, "Sunlit off-limb lunar atmosphere scatters", "L > 1e-3", EvaluateRelativeLuminance(LitLimb.Radiance));
        const LunarRayResult NightLimb = EvaluateSingleLunarBody(Shroud, &Atlas, NormalizeVector(Vector3f{ -std::sin(LimbAngle), 0.0f, std::cos(LimbAngle) }),
                                                                 NormalizeVector(Vector3f{ 1.0f, 0.0f, 0.15f }), Flux);
        EnforceGate(Ledger, EvaluateRelativeLuminance(NightLimb.Radiance) < 0.02f * EvaluateRelativeLuminance(LitLimb.Radiance), "Lunar body umbra darkens anti-solar limb", "L_umbra < 2% L_lit", EvaluateRelativeLuminance(NightLimb.Radiance) / EvaluateRelativeLuminance(LitLimb.Radiance));

        LunarBodyDescriptor Luna          = BuildLunarPreset(LunarPresetIdentifier::Luna, MoonAxis, 0.05f);
        Luna.Atmosphere.AtmosphereEnabled = false;
        Luna.PlanetshineIntensity         = 0.0f;
        const Vector3f ProbeView    = NormalizeVector(Vector3f{ 0.01f, 0.01f, 1.0f });
        const auto     OppositionAt = [&](float PhaseDegrees)
        {
            const float    Phase = PhaseDegrees * Atmosphere::DegreesToRadians;
            const Vector3f Sun   = NormalizeVector(Vector3f{ std::sin(Phase), 0.0f, -std::cos(Phase) });
            return EvaluateRelativeLuminance(EvaluateSingleLunarBody(Luna, nullptr, ProbeView, Sun, Flux).Radiance);
        };
        const float SurgeRatio = OppositionAt(0.5f) / OppositionAt(10.0f);
        EnforceGate(Ledger, SurgeRatio > 1.15f, "Hapke opposition surge (0.5 deg vs 10 deg)", "ratio > 1.15", SurgeRatio);

        // ⑨ Clouds: density bounds, coverage monotonicity, shadow, energy bounds.
        AtmosphereUniformBlock CloudProfile = Atmosphere::ConstructEarthProfile();
        Atmosphere::ConfigureSolarDirection(CloudProfile, 40.0f * Atmosphere::DegreesToRadians, 0.3f);
        Atmosphere::ConfigureViewerAltitudeKm(CloudProfile, 0.2f);
        AtmosphericLookupTables CloudTables = FoundationTables;
        SynthesizeSkyViewTable(CloudProfile, CloudTables);
        double PreviousMean      = -1.0;
        bool   CoverageMonotonic = true;
        bool   DensityBounded    = true;
        for (float Coverage = 0.1f; Coverage <= 0.91f; Coverage += 0.2f)
        {
            const VolumetricCloudConfiguration CumulusConfiguration = BuildCloudGenusPreset(CloudGenusIdentifier::Cumulus, Coverage);
            double                             Sum                  = 0.0;
            for (uint32_t Probe = 0u; Probe < 4000u; ++Probe)
            {
                const float    AltitudeKm = 1.4f + 2.2f * ((Probe * 37u) % 101u) / 101.0f;
                const Vector3f Position   { (Probe % 63u) * 0.73f - 20.0f, CloudProfile.BottomRadius + AltitudeKm, (Probe / 63u) * 0.81f - 25.0f };
                const float    Density    = EvaluateCloudDensity(CumulusConfiguration, CloudProfile, nullptr, Position, 0.0f, true);
                DensityBounded = DensityBounded && Density >= 0.0f && Density <= 1.0f;
                Sum += Density;
            }
            const double Mean = Sum / 4000.0;
            CoverageMonotonic = CoverageMonotonic && Mean > PreviousMean;
            PreviousMean      = Mean;
        }
        EnforceGate(Ledger, DensityBounded, "Cloud density bounded in [0, 1]", "all probes", 1.0);
        EnforceGate(Ledger, CoverageMonotonic, "Mean cloud density monotonic in coverage", "mean(c_i+1) > mean(c_i)", PreviousMean);

        const VolumetricCloudConfiguration Overcast = BuildCloudGenusPreset(CloudGenusIdentifier::Stratus, 0.98f);
        VolumetricCloudConfiguration       Clear    = BuildCloudGenusPreset(CloudGenusIdentifier::Cumulus, 0.0f);
        Clear.Cirrus.VeilEnabled           = false;
        const Vector3f GroundPoint    { 0.0f, CloudProfile.BottomRadius + 0.01f, 0.0f };
        const float    OvercastShadow = EvaluateCloudShadowTransmittance(Overcast, CloudProfile, nullptr, GroundPoint, 0.0f);
        const float    ClearShadow    = EvaluateCloudShadowTransmittance(Clear, CloudProfile, nullptr, GroundPoint, 0.0f);
        EnforceGate(Ledger, ClearShadow == 1.0f, "Clear sky casts no cloud shadow", "T_shadow == 1", ClearShadow);
        EnforceGate(Ledger, OvercastShadow < 0.2f, "Overcast stratus casts deep shadow", "T_shadow < 0.2", OvercastShadow);

        const VolumetricCloudConfiguration Cumulus       = BuildCloudGenusPreset(CloudGenusIdentifier::Cumulus, 0.6f);
        bool                               EnergyBounded = true;
        float                              MaximumCloud  = 0.0f;
        uint32_t                           OpaqueCount   = 0u;
        for (uint32_t Probe = 0u; Probe < 600u; ++Probe)
        {
            const Vector3f       Direction = ConstructDirection(25.0f + (Probe % 30u) * 2.0f, (Probe / 30u) * 18.0f);
            const CloudRayResult Ray       = IntegrateVolumetricClouds(Cumulus, CloudTables, CloudProfile, nullptr, Direction, 0.0f, Probe, 7u, 0u);
            EnergyBounded = EnergyBounded && Ray.Transmittance.Green >= 0.0f && Ray.Transmittance.Green <= 1.0f && std::isfinite(Ray.Radiance.Green)
                         && Ray.Radiance.Green >= 0.0f && Ray.Radiance.Green < 60.0f;
            MaximumCloud = std::max(MaximumCloud, Ray.Radiance.Green);
            OpaqueCount += (Ray.Transmittance.Green < 0.5f) ? 1u : 0u;
        }
        EnforceGate(Ledger, EnergyBounded, "Cloud radiance finite and energy bounded", "0 <= L < 60, 0 <= T <= 1", MaximumCloud);
        // Slant rays at 25-83 deg see cumulus towers side-on, so the opaque fraction exceeds the 60 % plan-view coverage; the upper
        // bound only rejects a saturated overcast deck (600 / 600), the lower bound a vanished field.
        EnforceGate(Ledger, OpaqueCount > 90u && OpaqueCount < 570u, "Cumulus 60% broken sky above 25 deg elevation", "90 < opaque rays < 570", OpaqueCount);

        // ⑩ Fog: Beer-Lambert in a uniform medium, closed-form height integral vs a fine Riemann sum, flow coupling, energy bounds, layout.
        EnforceGate(Ledger, sizeof(FogFieldUniformBlock) == 272u, "FogFieldUniformBlock std140 size", "bytes == 272", sizeof(FogFieldUniformBlock));
        AtmosphereUniformBlock FogProfile = CloudProfile;
        Atmosphere::ConfigureViewerAltitudeKm(FogProfile, 0.05f);
        const FogLightingContext FogLighting = PrepareFogLighting(CloudTables, FogProfile);
        {
            // Base far above the viewer -> σ_t is uniform along the whole 5 km chord, so T must equal e^{-σ d} exactly.
            VolumetricFogConfiguration Uniform{};
            Uniform.Height.FloorAltitudeKm        = 40.0f;
            Uniform.Height.GroundExtinctionPerKm = 0.8f;
            Uniform.Height.FlowVariation         = 0.0f;
            Uniform.MaximumTraceDistanceKm       = 5.0f;
            const FogRayResult Ray      = IntegrateVolumetricFog(Uniform, FogLighting, FogProfile, nullptr, ConstructDirection(17.0f, 40.0f), 0.0f, 0.0f, 3u, 5u, 0u);
            const float        Expected = std::exp(-0.8f * 5.0f);
            const float        Relative = std::fabs(Ray.Transmittance.Green - Expected) / Expected;
            EnforceGate(Ledger, Relative < 1.0e-4f, "Fog Beer-Lambert in a uniform medium", "|T - e^-sd| / e^-sd < 1e-4", Relative);
        }
        {
            // Exponential height fog along a near-grazing 20 km curved-Earth chord: 48 closed-form segments vs a 200 000-step midpoint sum.
            VolumetricFogConfiguration Height{};
            Height.Height.FloorAltitudeKm        = 0.0f;
            Height.Height.GroundExtinctionPerKm = 2.0f;
            Height.Height.ScaleHeightKm         = 0.15f;
            Height.Height.FlowVariation         = 0.0f;
            Height.MaximumTraceDistanceKm       = 20.0f;
            const Vector3f     Direction = ConstructDirection(2.9f, 0.0f);
            const FogRayResult Ray       = IntegrateVolumetricFog(Height, FogLighting, FogProfile, nullptr, Direction, 0.0f, 0.0f, 9u, 2u, 0u);
            const double       ViewerRadius = FogProfile.BottomRadius + 0.05;
            double             Depth        = 0.0;
            constexpr uint32_t Slices       = 200000u;
            for (uint32_t Slice = 0u; Slice < Slices; ++Slice)
            {
                const double Distance = (Slice + 0.5) * 20.0 / Slices;
                const double Altitude = std::sqrt(Distance * Distance + 2.0 * Distance * ViewerRadius * Direction.YCoord + ViewerRadius * ViewerRadius) - FogProfile.BottomRadius;
                Depth += 2.0 * std::exp(-std::max(0.0, Altitude) / 0.15) * (20.0 / Slices);
            }
            const double Relative = std::fabs(-std::log(std::max(1.0e-30, static_cast<double>(Ray.Transmittance.Green))) - Depth) / Depth;
            EnforceGate(Ledger, Relative < 5.0e-3, "Height fog closed form vs Riemann sum (20 km chord)", "|tau - tau_ref| / tau_ref < 0.5%", Relative);
        }
        {
            // Uniform wind (no shear, eddies or operators): the advected parcel origin must sit exactly upstream by u · (blended age).
            AtmosphericFlowField Wind{};
            Wind.Synoptic.ReferenceVelocity     = Vector3f{ 9.0f, -4.0f, 0.0f };
            Wind.Synoptic.AltitudeShearExponent = 0.0f;
            Wind.Synoptic.EkmanVeerRadians      = 0.0f;
            Wind.Synoptic.CurlSpeedMetresPerSec = 0.0f;
            Vector3f    Displacement{};
            float       Condensation = 0.0f;
            const float Sequence     = 13.0f;
            const float Period       = VolumetricFogConfiguration{}.AdvectionPeriodSeconds;
            ResolveFogAdvection(&Wind, Vector3f{ 400.0f, -250.0f, 2000.0f }, Sequence, Period, Displacement, Condensation);
            const float PhaseFirst  = std::fmod(Sequence, Period);
            const float PhaseSecond = std::fmod(Sequence + 0.5f * Period, Period);
            const float WeightFirst = 1.0f - std::fabs(2.0f * PhaseFirst / Period - 1.0f);
            const float Age         = WeightFirst * PhaseFirst + (1.0f - WeightFirst) * PhaseSecond;
            const float Error       = VectorLength(AddVector(Displacement, ScaleVector(Wind.Synoptic.ReferenceVelocity, Age)));
            EnforceGate(Ledger, Error < 0.05f, "Fog advection follows the shared wind", "|X - (x - u age)| < 5 cm", Error);
        }
        {
            // A radial outflow operator (the same force field that clears a cloud cavity) must hollow out the fog bank at its centre.
            VolumetricFogConfiguration Bank{};
            Bank.Height.HeightFogEnabled = false;
            FogVolumeDescriptor Volume{};
            Volume.WorldCentreMetres = Vector3f{ 0.0f, 0.0f, 60.0f };
            Volume.NoiseStrength     = 0.0f;
            RegisterFogVolume(Bank, Volume);
            AtmosphericFlowField Calm{};
            Calm.Synoptic.WindEnabled = false;
            Calm.Synoptic.CurlSpeedMetresPerSec = 0.0f;
            AtmosphericFlowField Outflow = Calm;
            FlowSourceDescriptor Burst{};
            Burst.Category             = FlowSourceCategory::RadialOutflow;
            Burst.WorldCentreMetres    = Vector3f{ 0.0f, 0.0f, 60.0f };
            Burst.OuterRadiusMetres    = 2500.0f;
            Burst.InnerEyeRadiusMetres = 700.0f;
            Burst.PeakSpeedMetresPerSec = 12.0f;
            RegisterFlowSource(Outflow, Burst);
            const float Still   = EvaluateFogDensity(Bank, &Calm, Vector3f{ 0.0f, 0.0f, 40.0f }, 30.0f);
            const float Cleared = EvaluateFogDensity(Bank, &Outflow, Vector3f{ 0.0f, 0.0f, 40.0f }, 30.0f);
            const float Ratio   = Cleared / std::max(1.0e-6f, Still);
            EnforceGate(Ledger, Still > 1.0f && Ratio < 0.8f, "Radial outflow clears the fog bank core", "rho_outflow / rho_calm < 0.8", Ratio);
        }
        {
            VolumetricFogConfiguration Dense{};
            Dense.Height.GroundExtinctionPerKm = 9.0f;
            Dense.Height.ScaleHeightKm         = 0.2f;
            FogVolumeDescriptor Volume{};
            Volume.ExtinctionPerKm = 25.0f;
            RegisterFogVolume(Dense, Volume);
            const AtmosphericFlowField Flow = [] { AtmosphericFlowField Field{}; return Field; }();
            bool  Bounded = true;
            float Peak    = 0.0f;
            for (uint32_t Probe = 0u; Probe < 240u; ++Probe)
            {
                const FogRayResult Ray = IntegrateVolumetricFog(Dense, FogLighting, FogProfile, &Flow, ConstructDirection(-10.0f + (Probe % 24u) * 2.5f, (Probe / 24u) * 36.0f),
                                                                45.0f, 0.0f, Probe, 1u, 0u);
                Bounded = Bounded && std::isfinite(Ray.Radiance.Green) && Ray.Radiance.Green >= 0.0f && Ray.Radiance.Green < 60.0f
                       && Ray.Transmittance.Green >= 0.0f && Ray.Transmittance.Green <= 1.0f;
                Peak = std::max(Peak, Ray.Radiance.Green);
            }
            EnforceGate(Ledger, Bounded, "Fog radiance finite and energy bounded", "0 <= L < 60, 0 <= T <= 1", Peak);
        }

        // ⑪ Radiant-sun glare: zero when muted, monotonically fading, and vanishing at its 30° window.
        {
            AtmosphereUniformBlock GlareProfile = CloudProfile;
            const Vector3f SunAxis = ConstructDirection(40.0f, 0.3f * Atmosphere::RadiansToDegrees);
            const float    Muted   = EvaluateSolarGlareRadiance(CloudTables, GlareProfile, SunAxis, 0.00465f, 0.0f, 0.2f).Green;
            float          Previous = 1.0e30f;
            bool           Monotonic = true;
            for (float Offset = 0.2f; Offset < 31.0f; Offset += 0.4f)
            {
                const float Glare = EvaluateSolarGlareRadiance(CloudTables, GlareProfile, ConstructDirection(40.0f + Offset, 0.3f * Atmosphere::RadiansToDegrees), 0.00465f, 1.0f, 0.2f).Green;
                Monotonic = Monotonic && Glare <= Previous;
                Previous  = Glare;
            }
            EnforceGate(Ledger, Muted == 0.0f && Monotonic && Previous == 0.0f, "Solar glare monotone, windowed, off when muted", "dG/dtheta <= 0, G(30 deg) = 0", Previous);
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                               EXHIBIT RENDERER
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Exhibit exposure metering: fixed linear gain, camera-style log-average key (Reinhard 0.18), or solar-disc centre normalisation.
    enum class ExposureCalibration : uint8_t
    {
        FixedGain,
        SceneKey,
        SolarDisc
    };

    struct ExhibitSpecification
    {
        std::string                  FileName;
        float                        SunElevationDegrees  = 30.0f;
        float                        SunAzimuthDegrees    = 0.0f;
        float                        ViewerAltitudeKm     = 0.2f;
        float                        CameraYawDegrees     = 0.0f;
        float                        CameraPitchDegrees   = 10.0f;
        float                        HorizontalFovDegrees = 90.0f;
        float                        Exposure             = 1.0f;
        float                        SequenceSeconds      = 0.0f;
        uint32_t                     SamplesPerPixel      = 2u;
        bool                         OrbitalEnabled       = false;
        float                        OrbitalCloudCoverage = 0.5f;
        bool                         CloudsEnabled        = false;
        VolumetricCloudConfiguration Clouds               {};
        bool                         FlowEnabled          = false;
        AtmosphericFlowField         Flow                 {};
        CelestialSceneConfiguration  Celestial            {};
        float                        SolarDiscIntensity   = 160.0f;
        float                        SolarGlareStrength   = 0.0f;   // 0 = muted sun, 1 = radiant sun (glare bloom + aureole)
        bool                         FogEnabled           = false;
        VolumetricFogConfiguration   Fog                  {};
        ExposureCalibration          Metering             = ExposureCalibration::FixedGain;
    };

    struct RenderCamera
    {
        Vector3f ForwardAxis       {};
        Vector3f RightAxis         {};
        Vector3f UpAxis            {};
        float    TangentHalfWidth  = 1.0f;
        float    TangentHalfHeight = 1.0f;
    };

    [[nodiscard]] RenderCamera ConstructCamera(float YawDegrees, float PitchDegrees, float HorizontalFovDegrees) noexcept
    {
        RenderCamera Camera{};
        Camera.ForwardAxis       = ConstructDirection(PitchDegrees, YawDegrees);
        Camera.RightAxis         = NormalizeVector(CrossVector(Vector3f{ 0.0f, 1.0f, 0.0f }, Camera.ForwardAxis));
        Camera.UpAxis            = CrossVector(Camera.ForwardAxis, Camera.RightAxis);
        Camera.TangentHalfWidth  = std::tan(0.5f * HorizontalFovDegrees * Atmosphere::DegreesToRadians);
        Camera.TangentHalfHeight = Camera.TangentHalfWidth * static_cast<float>(ExhibitHeight) / static_cast<float>(ExhibitWidth);
        return Camera;
    }

    void ExecuteParallelRows(uint32_t RowCount, const std::function<void(uint32_t)>& RowRoutine)
    {
        const uint32_t        ThreadCount = std::max(1u, std::thread::hardware_concurrency());
        std::atomic<uint32_t> NextRow     { 0u };
        std::vector<std::thread> Threads;
        for (uint32_t Slot = 0u; Slot < ThreadCount; ++Slot)
        {
            Threads.emplace_back([&]()
            {
                for (uint32_t Row = NextRow.fetch_add(1u); Row < RowCount; Row = NextRow.fetch_add(1u))
                    RowRoutine(Row);
            });
        }
        for (std::thread& Lane : Threads)
            Lane.join();
    }

    [[nodiscard]] bool RenderExhibit(
        const ExhibitSpecification&    Exhibit,
        const AtmosphericLookupTables& FoundationTables,
        const LunarSurfaceAtlas&       Atlas,
        const std::string&             OutputDirectory)
    {
        const auto             StartClock = std::chrono::steady_clock::now();
        AtmosphereUniformBlock Profile    = Atmosphere::ConstructEarthProfile();
        Atmosphere::ConfigureSolarDirection(Profile, Exhibit.SunElevationDegrees * Atmosphere::DegreesToRadians, Exhibit.SunAzimuthDegrees * Atmosphere::DegreesToRadians);
        Atmosphere::ConfigureViewerAltitudeKm(Profile, Exhibit.OrbitalEnabled ? 0.2f : Exhibit.ViewerAltitudeKm);
        Atmosphere::ConfigureSequenceSeconds(Profile, Exhibit.SequenceSeconds, Exhibit.FlowEnabled ? 1.0f : 0.0f);
        AtmosphericLookupTables Tables = FoundationTables;
        SynthesizeSkyViewTable(Profile, Tables);

        const RenderCamera          Camera      = ConstructCamera(Exhibit.CameraYawDegrees, Exhibit.CameraPitchDegrees, Exhibit.HorizontalFovDegrees);
        const AtmosphericFlowField* FlowPointer = Exhibit.FlowEnabled ? &Exhibit.Flow : nullptr;
        CelestialSceneConfiguration Celestial   = Exhibit.Celestial;
        Celestial.Stars.PixelAngularSizeRadians = 2.0f * Camera.TangentHalfWidth / static_cast<float>(ExhibitWidth);
        const Vector3f OrbitalCamera{ 0.0f, Profile.BottomRadius + Exhibit.ViewerAltitudeKm, 0.0f };

        float Exposure = Exhibit.Exposure;
        if (Exhibit.Metering == ExposureCalibration::SolarDisc)
        {
            const Vector3f     SunAxis = ConstructDirection(Exhibit.SunElevationDegrees, Exhibit.SunAzimuthDegrees);
            const LinearColour Centre  = EvaluateSolarRadiometricDisc(Tables, Profile, SunAxis, 0.00465f, Exhibit.SolarDiscIntensity, Exhibit.ViewerAltitudeKm, false);
            Exposure = 0.42f / std::max(1.0e-6f, std::max(Centre.Red, std::max(Centre.Green, Centre.Blue)));
        }

        const std::size_t PixelCount = static_cast<std::size_t>(ExhibitWidth) * ExhibitHeight;
        std::vector<LinearColour> Backdrop(PixelCount);
        std::vector<LinearColour> CloudRadiance(PixelCount);
        std::vector<LinearColour> CloudTransmittance(PixelCount, LinearColour{ 1.0f, 1.0f, 1.0f });
        std::vector<float>        CloudDistance(PixelCount, 0.0f);
        std::vector<LinearColour> FogRadiance(PixelCount);
        std::vector<LinearColour> FogTransmittance(PixelCount, LinearColour{ 1.0f, 1.0f, 1.0f });
        const FogLightingContext  FogLighting = Exhibit.FogEnabled ? PrepareFogLighting(Tables, Profile) : FogLightingContext{};

        ExecuteParallelRows(ExhibitHeight, [&](uint32_t Row)
        {
            for (uint32_t Column = 0u; Column < ExhibitWidth; ++Column)
            {
                const std::size_t Pixel                 = static_cast<std::size_t>(Row) * ExhibitWidth + Column;
                const uint32_t    Seed                  = Atmosphere::HashPixelSeed(Column, Row, 0x5EEDu);
                LinearColour      BackdropSum           {};
                LinearColour      CloudSum              {};
                LinearColour      CloudTransmittanceSum {};
                LinearColour      FogSum                {};
                LinearColour      FogTransmittanceSum   {};
                float             DistanceSum           = 0.0f;
                for (uint32_t Sample = 0u; Sample < Exhibit.SamplesPerPixel; ++Sample)
                {
                    float JitterX = 0.5f;
                    float JitterY = 0.5f;
                    Atmosphere::EvaluateOwenSobolPair(Sample, Seed, JitterX, JitterY);
                    const float    NdcX      = ((static_cast<float>(Column) + JitterX) / ExhibitWidth) * 2.0f - 1.0f;
                    const float    NdcY      = 1.0f - ((static_cast<float>(Row) + JitterY) / ExhibitHeight) * 2.0f;
                    const Vector3f Direction = NormalizeVector(AddVector(Camera.ForwardAxis,
                        AddVector(ScaleVector(Camera.RightAxis, NdcX * Camera.TangentHalfWidth), ScaleVector(Camera.UpAxis, NdcY * Camera.TangentHalfHeight))));

                    LinearColour Radiance{};
                    if (Exhibit.OrbitalEnabled)
                    {
                        const OrbitalAtmosphereResult Orbital = EvaluateOrbitalPlanetRadiance(Tables, Profile, OrbitalCamera, Direction, FlowPointer,
                                                                                              Exhibit.OrbitalCloudCoverage, Exhibit.SequenceSeconds, 48u);
                        Radiance = Orbital.CombinedRadiance;
                        if (Orbital.AtmosphereTransmittance.Green > 0.0f)
                        {
                            const LinearColour Space = AddColour(
                                EvaluateCelestialBackdrop(Celestial, &Atlas, Tables, Profile, Direction, Exhibit.SequenceSeconds, true),
                                EvaluateSolarRadiometricDisc(Tables, Profile, Direction, 0.00465f, Exhibit.SolarDiscIntensity, Exhibit.ViewerAltitudeKm, true));
                            Radiance = AddColour(Radiance, MultiplyColour(Orbital.AtmosphereTransmittance, Space));
                        }
                    }
                    else
                    {
                        Radiance = SampleSkyViewRadiance(Tables, Profile, Direction);
                        LinearColour VeilTransmittance{ 1.0f, 1.0f, 1.0f };
                        if (FlowPointer != nullptr)
                            Radiance = AddColour(Radiance, EvaluateFlowAdvectedSkyVeil(Tables, Profile, *FlowPointer, Direction, Exhibit.SequenceSeconds, VeilTransmittance));
                        const LinearColour Distant = AddColour(
                            EvaluateSolarRadiometricDisc(Tables, Profile, Direction, 0.00465f, Exhibit.SolarDiscIntensity, Exhibit.ViewerAltitudeKm, false),
                            EvaluateCelestialBackdrop(Celestial, &Atlas, Tables, Profile, Direction, Exhibit.SequenceSeconds, false));
                        Radiance = AddColour(Radiance, MultiplyColour(VeilTransmittance, Distant));

                        if (Exhibit.CloudsEnabled)
                        {
                            const CloudRayResult Cloud = IntegrateVolumetricClouds(Exhibit.Clouds, Tables, Profile, FlowPointer, Direction,
                                                                                   Exhibit.SequenceSeconds, Column, Row, Sample);
                            CloudSum              = AddColour(CloudSum, Cloud.Radiance);
                            CloudTransmittanceSum = AddColour(CloudTransmittanceSum, Cloud.Transmittance);
                            DistanceSum  += Cloud.WeightedDistanceKm;
                        }
                        if (Exhibit.FogEnabled)
                        {
                            const FogRayResult Fog = IntegrateVolumetricFog(Exhibit.Fog, FogLighting, Profile, FlowPointer, Direction, Exhibit.SequenceSeconds, 0.0f,
                                                                            Column, Row, Sample);
                            FogSum              = AddColour(FogSum, Fog.Radiance);
                            FogTransmittanceSum = AddColour(FogTransmittanceSum, Fog.Transmittance);
                        }
                    }
                    BackdropSum = AddColour(BackdropSum, Radiance);
                }
                const float Inverse = 1.0f / static_cast<float>(Exhibit.SamplesPerPixel);
                Backdrop[Pixel] = ScaleColour(BackdropSum, Inverse);
                if (Exhibit.CloudsEnabled)
                {
                    CloudRadiance[Pixel]      = ScaleColour(CloudSum, Inverse);
                    CloudTransmittance[Pixel] = ScaleColour(CloudTransmittanceSum, Inverse);
                    CloudDistance[Pixel]      = DistanceSum * Inverse;
                }
                if (Exhibit.FogEnabled)
                {
                    FogRadiance[Pixel]      = ScaleColour(FogSum, Inverse);
                    FogTransmittance[Pixel] = ScaleColour(FogTransmittanceSum, Inverse);
                }
            }
        });

        if (Exhibit.CloudsEnabled)
            DenoiseCloudLayerBilateral(ExhibitWidth, ExhibitHeight, CloudRadiance, CloudTransmittance, CloudDistance);

        // Radiant-sun glare veils everything in front of it (it forms in the optics), scaled once by the sun's own cloud visibility.
        std::vector<LinearColour> GlareRadiance(PixelCount);
        if (Exhibit.SolarGlareStrength > 0.0f)
        {
            const Vector3f SunAxis       = ConstructDirection(Exhibit.SunElevationDegrees, Exhibit.SunAzimuthDegrees);
            float          SunVisibility = 1.0f;
            if (Exhibit.CloudsEnabled)
            {
                float VisibilitySum = 0.0f;
                for (uint32_t Sample = 0u; Sample < 8u; ++Sample)
                    VisibilitySum += IntegrateVolumetricClouds(Exhibit.Clouds, Tables, Profile, FlowPointer, SunAxis, Exhibit.SequenceSeconds, 0u, 0u, Sample).Transmittance.Green;
                SunVisibility = VisibilitySum / 8.0f;
            }
            if (Exhibit.FogEnabled)
                SunVisibility *= IntegrateVolumetricFog(Exhibit.Fog, FogLighting, Profile, FlowPointer, SunAxis, Exhibit.SequenceSeconds, 0.0f, 0u, 0u, 0u).Transmittance.Green;
            ExecuteParallelRows(ExhibitHeight, [&](uint32_t Row)
            {
                for (uint32_t Column = 0u; Column < ExhibitWidth; ++Column)
                {
                    const float    NdcX      = ((static_cast<float>(Column) + 0.5f) / ExhibitWidth) * 2.0f - 1.0f;
                    const float    NdcY      = 1.0f - ((static_cast<float>(Row) + 0.5f) / ExhibitHeight) * 2.0f;
                    const Vector3f Direction = NormalizeVector(AddVector(Camera.ForwardAxis,
                        AddVector(ScaleVector(Camera.RightAxis, NdcX * Camera.TangentHalfWidth), ScaleVector(Camera.UpAxis, NdcY * Camera.TangentHalfHeight))));
                    GlareRadiance[static_cast<std::size_t>(Row) * ExhibitWidth + Column] = ScaleColour(
                        EvaluateSolarGlareRadiance(Tables, Profile, Direction, 0.00465f, Exhibit.SolarGlareStrength, Exhibit.ViewerAltitudeKm, Exhibit.OrbitalEnabled),
                        SunVisibility);
                }
            });
        }

        if (Exhibit.Metering == ExposureCalibration::SceneKey)
        {
            double LogLuminanceSum = 0.0;
            for (std::size_t Pixel = 0u; Pixel < PixelCount; ++Pixel)
            {
                const LinearColour Final = AddColour(AddColour(FogRadiance[Pixel], MultiplyColour(FogTransmittance[Pixel], AddColour(CloudRadiance[Pixel], MultiplyColour(CloudTransmittance[Pixel], Backdrop[Pixel])))), GlareRadiance[Pixel]);
                LogLuminanceSum += std::log(1.0e-12 + std::max(0.0f, EvaluateRelativeLuminance(Final)));
            }
            const double LogAverage = std::exp(LogLuminanceSum / static_cast<double>(PixelCount));
            Exposure = static_cast<float>(Exhibit.Exposure * 0.18 / std::max(1.0e-12, LogAverage));
        }

        std::vector<uint8_t> Encoded(PixelCount * 3u);
        double LuminanceSum = 0.0;
        for (std::size_t Pixel = 0u; Pixel < PixelCount; ++Pixel)
        {
            const LinearColour Final  = AddColour(AddColour(FogRadiance[Pixel], MultiplyColour(FogTransmittance[Pixel], AddColour(CloudRadiance[Pixel], MultiplyColour(CloudTransmittance[Pixel], Backdrop[Pixel])))), GlareRadiance[Pixel]);
            const LinearColour Mapped = EvaluatePbrNeutralTonemap(Final, Exposure);
            // Triangular-PDF dither (±1 LSB) removes 8-bit banding in smooth twilight gradients.
            const uint32_t Hash   = Atmosphere::HashPixelSeed(static_cast<uint32_t>(Pixel % ExhibitWidth), static_cast<uint32_t>(Pixel / ExhibitWidth), 0xD17Eu);
            const float    Dither = (static_cast<float>(Hash & 0xFFFFu) + static_cast<float>(Hash >> 16u)) / 65535.0f - 1.0f;
            Encoded[Pixel * 3u + 0u] = EncodeLinearToSrgbByte(Mapped.Red, Dither);
            Encoded[Pixel * 3u + 1u] = EncodeLinearToSrgbByte(Mapped.Green, Dither);
            Encoded[Pixel * 3u + 2u] = EncodeLinearToSrgbByte(Mapped.Blue, Dither);
            LuminanceSum += EvaluateRelativeLuminance(Mapped);
        }

        const std::string FilePath = OutputDirectory + "/" + Exhibit.FileName;
        const bool        Written  = EncodePngFile(FilePath, ExhibitWidth, ExhibitHeight, Encoded);
        const double      Seconds  = std::chrono::duration<double>(std::chrono::steady_clock::now() - StartClock).count();
        std::printf("  [%s] %-40s exposure %.4g  mean display luminance %.3f  (%.1f s)\n", Written ? "PNG " : "FAIL", Exhibit.FileName.c_str(), Exposure,
                    LuminanceSum / PixelCount, Seconds);
        return Written;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                              EXHIBIT CATALOGUE
    //--------------------------------------------------------------------------------------------------------------------

    void ConfigureDefaultNightSky(CelestialSceneConfiguration& Celestial, float SiteLatitudeRadians, float SiderealRadians)
    {
        Celestial.Stars.SiteLatitudeRadians      = SiteLatitudeRadians;
        Celestial.Stars.CelestialRotationRadians = SiderealRadians;
    }

    void AppendMoon(CelestialSceneConfiguration& Celestial, LunarPresetIdentifier Preset, float ElevationDegrees, float AzimuthDegrees, float AngularRadius, float SpinRadians = 0.0f)
    {
        if (Celestial.MoonCount >= MaximumLunarBodyCount)
            return;
        LunarBodyDescriptor Moon               = BuildLunarPreset(Preset, ConstructDirection(ElevationDegrees, AzimuthDegrees), AngularRadius);
        Moon.SpinLongitudeRadians              = SpinRadians;
        Celestial.Moons[Celestial.MoonCount++] = Moon;
    }

    [[nodiscard]] AtmosphericFlowField ConstructSynopticFlow() noexcept
    {
        AtmosphericFlowField Flow{};
        Flow.Synoptic.ReferenceVelocity = Vector3f{ 16.0f, 5.0f, 0.0f };
        return Flow;
    }

    [[nodiscard]] std::vector<ExhibitSpecification> ConstructExhibitCatalogue()
    {
        std::vector<ExhibitSpecification> Catalogue;

        {   // 01 — Clear noon sky over the ocean with the flow-advected stratospheric veil.
            ExhibitSpecification Exhibit{};
            Exhibit.FileName            = "01_ClearNoonOceanSky.png";
            Exhibit.SunElevationDegrees = 58.0f; Exhibit.SunAzimuthDegrees = 200.0f;
            Exhibit.CameraYawDegrees    = 20.0f; Exhibit.CameraPitchDegrees = 14.0f; Exhibit.HorizontalFovDegrees = 100.0f;
            Exhibit.FlowEnabled         = true; Exhibit.Flow = ConstructSynopticFlow(); Exhibit.SequenceSeconds = 120.0f;
            Catalogue.push_back(Exhibit);
        }
        {   // 02 — Golden-hour fair-weather cumulus field.
            ExhibitSpecification Exhibit{};
            Exhibit.FileName            = "02_GoldenHourCumulus.png";
            Exhibit.SunElevationDegrees = 7.0f; Exhibit.SunAzimuthDegrees = 10.0f;
            Exhibit.CameraYawDegrees    = -25.0f; Exhibit.CameraPitchDegrees = 9.0f; Exhibit.HorizontalFovDegrees = 85.0f;
            Exhibit.CloudsEnabled       = true; Exhibit.Clouds = BuildCloudGenusPreset(CloudGenusIdentifier::Cumulus, 0.42f);
            Exhibit.FlowEnabled         = true; Exhibit.Flow = ConstructSynopticFlow(); Exhibit.SequenceSeconds = 60.0f;
            Catalogue.push_back(Exhibit);
        }
        {   // 03 — Sunset stratocumulus rolls back-lit by the low sun.
            ExhibitSpecification Exhibit{};
            Exhibit.FileName            = "03_SunsetStratocumulus.png";
            Exhibit.SunElevationDegrees = 1.5f; Exhibit.SunAzimuthDegrees = 0.0f;
            Exhibit.CameraYawDegrees    = 8.0f; Exhibit.CameraPitchDegrees = 7.0f; Exhibit.HorizontalFovDegrees = 80.0f;
            Exhibit.CloudsEnabled       = true; Exhibit.Clouds = BuildCloudGenusPreset(CloudGenusIdentifier::Stratocumulus, 0.55f);
            Catalogue.push_back(Exhibit);
        }
        {   // 04 — Overcast stratus deck.
            ExhibitSpecification Exhibit{};
            Exhibit.FileName            = "04_OvercastStratus.png";
            Exhibit.SunElevationDegrees = 38.0f; Exhibit.SunAzimuthDegrees = 120.0f;
            Exhibit.CameraYawDegrees    = 0.0f; Exhibit.CameraPitchDegrees = 8.0f; Exhibit.HorizontalFovDegrees = 90.0f;
            Exhibit.CloudsEnabled       = true; Exhibit.Clouds = BuildCloudGenusPreset(CloudGenusIdentifier::Stratus, 0.97f);
            Catalogue.push_back(Exhibit);
        }
        {   // 05 — Deep convective cumulonimbus towers with anvils.
            ExhibitSpecification Exhibit{};
            Exhibit.FileName            = "05_CumulonimbusTowers.png";
            Exhibit.SunElevationDegrees = 26.0f; Exhibit.SunAzimuthDegrees = 140.0f;
            Exhibit.CameraYawDegrees    = 0.0f; Exhibit.CameraPitchDegrees = 9.0f; Exhibit.HorizontalFovDegrees = 80.0f;
            Exhibit.CloudsEnabled       = true; Exhibit.Clouds = BuildCloudGenusPreset(CloudGenusIdentifier::Cumulonimbus, 0.10f);
            Catalogue.push_back(Exhibit);
        }
        {   // 06 — Cirrus veil with the 22° ice-crystal halo.
            ExhibitSpecification Exhibit{};
            Exhibit.FileName            = "06_CirrusHalo.png";
            Exhibit.SunElevationDegrees = 34.0f; Exhibit.SunAzimuthDegrees = 90.0f;
            Exhibit.CameraYawDegrees    = 90.0f; Exhibit.CameraPitchDegrees = 30.0f; Exhibit.HorizontalFovDegrees = 100.0f;
            Exhibit.CloudsEnabled       = true; Exhibit.Clouds = BuildCloudGenusPreset(CloudGenusIdentifier::Cirrus, 0.70f);
            Exhibit.FlowEnabled         = true; Exhibit.Flow = ConstructSynopticFlow();
            Catalogue.push_back(Exhibit);
        }
        {   // 07 — Civil twilight with a young crescent moon and the first stars.
            ExhibitSpecification Exhibit{};
            Exhibit.FileName            = "07_CivilTwilightCrescent.png";
            Exhibit.SunElevationDegrees = -4.5f; Exhibit.SunAzimuthDegrees = 270.0f;
            Exhibit.CameraYawDegrees    = 262.0f; Exhibit.CameraPitchDegrees = 10.0f; Exhibit.HorizontalFovDegrees = 80.0f;
            Exhibit.Exposure            = 1.0f; Exhibit.Metering = ExposureCalibration::SceneKey; Exhibit.SamplesPerPixel = 4u;
            ConfigureDefaultNightSky(Exhibit.Celestial, -0.46f, 2.2f);
            AppendMoon(Exhibit.Celestial, LunarPresetIdentifier::Luna, 13.0f, 252.0f, 0.020f);
            Catalogue.push_back(Exhibit);
        }
        {   // 08 — Southern-hemisphere Milky Way bulge overhead (Great Rift, Magellanic Clouds).
            ExhibitSpecification Exhibit{};
            Exhibit.FileName            = "08_MilkyWayGalacticBulge.png";
            Exhibit.SunElevationDegrees = -32.0f; Exhibit.SunAzimuthDegrees = 0.0f;
            Exhibit.CameraYawDegrees    = 200.0f; Exhibit.CameraPitchDegrees = 42.0f; Exhibit.HorizontalFovDegrees = 110.0f;
            Exhibit.Exposure            = 0.28f; Exhibit.Metering = ExposureCalibration::SceneKey; Exhibit.SamplesPerPixel = 4u;
            ConfigureDefaultNightSky(Exhibit.Celestial, -0.46f, 266.4f * Atmosphere::DegreesToRadians);
            Catalogue.push_back(Exhibit);
        }
        {   // 09 — Six simultaneous moons with distinct atmospheres in a deep twilight sky.
            ExhibitSpecification Exhibit{};
            Exhibit.FileName            = "09_SixMoonSystem.png";
            Exhibit.SunElevationDegrees = -7.0f; Exhibit.SunAzimuthDegrees = 20.0f;
            Exhibit.CameraYawDegrees    = 190.0f; Exhibit.CameraPitchDegrees = 26.0f; Exhibit.HorizontalFovDegrees = 95.0f;
            Exhibit.Exposure            = 2.6f; Exhibit.SamplesPerPixel = 4u;
            ConfigureDefaultNightSky(Exhibit.Celestial, -0.46f, 1.2f);
            AppendMoon(Exhibit.Celestial, LunarPresetIdentifier::Shroud,  32.0f, 194.0f, 0.075f, 0.6f);
            AppendMoon(Exhibit.Celestial, LunarPresetIdentifier::Ember,   16.0f, 160.0f, 0.048f, 1.9f);
            AppendMoon(Exhibit.Celestial, LunarPresetIdentifier::Glacier, 44.0f, 164.0f, 0.036f, 0.3f);
            AppendMoon(Exhibit.Celestial, LunarPresetIdentifier::Sulfur,  12.0f, 214.0f, 0.032f, 2.6f);
            AppendMoon(Exhibit.Celestial, LunarPresetIdentifier::Luna,    46.0f, 214.0f, 0.040f, 0.0f);
            AppendMoon(Exhibit.Celestial, LunarPresetIdentifier::Shard,   26.0f, 228.0f, 0.026f, 4.1f);
            Catalogue.push_back(Exhibit);
        }
        {   // 10 — Telephoto: back-lit lunar atmospheres (forward-scattering limb rings + detached haze).
            ExhibitSpecification Exhibit{};
            Exhibit.FileName            = "10_LunarAtmosphereLimbs.png";
            Exhibit.SunElevationDegrees = -18.0f; Exhibit.SunAzimuthDegrees = 180.0f;
            Exhibit.CameraYawDegrees    = 180.0f; Exhibit.CameraPitchDegrees = 36.0f; Exhibit.HorizontalFovDegrees = 16.0f;
            Exhibit.Exposure            = 3.0f; Exhibit.SamplesPerPixel = 4u;
            ConfigureDefaultNightSky(Exhibit.Celestial, -0.46f, 3.1f);
            AppendMoon(Exhibit.Celestial, LunarPresetIdentifier::Shroud, 36.5f, 177.0f, 0.042f, 0.6f);
            AppendMoon(Exhibit.Celestial, LunarPresetIdentifier::Ember,  34.5f, 185.0f, 0.026f, 1.2f);
            Catalogue.push_back(Exhibit);
        }
        {   // 11 — Telephoto solar photosphere: Neckel-Labs limb darkening + aureole, differentially reddened near the skyline.
            ExhibitSpecification Exhibit{};
            Exhibit.FileName            = "11_SolarPhotosphereLimbDarkening.png";
            Exhibit.SunElevationDegrees = 4.0f; Exhibit.SunAzimuthDegrees = 0.0f;
            Exhibit.CameraYawDegrees    = 0.0f; Exhibit.CameraPitchDegrees = 4.0f; Exhibit.HorizontalFovDegrees = 3.2f;
            Exhibit.SamplesPerPixel     = 2u; Exhibit.Metering = ExposureCalibration::SolarDisc;
            Catalogue.push_back(Exhibit);
        }
        {   // 12 — Looking up into a flow-field cyclone: spiral cloud bands around a cleared eye.
            ExhibitSpecification Exhibit{};
            Exhibit.FileName            = "12_FlowFieldCycloneVault.png";
            Exhibit.SunElevationDegrees = 48.0f; Exhibit.SunAzimuthDegrees = 30.0f;
            Exhibit.CameraYawDegrees    = 0.0f; Exhibit.CameraPitchDegrees = 89.0f; Exhibit.HorizontalFovDegrees = 120.0f;
            Exhibit.CloudsEnabled       = true; Exhibit.Clouds = BuildCloudGenusPreset(CloudGenusIdentifier::Stratocumulus, 0.62f);
            Exhibit.FlowEnabled         = true; Exhibit.Flow = ConstructSynopticFlow(); Exhibit.SequenceSeconds = 420.0f;
            FlowSourceDescriptor Swirl{};
            Swirl.Category              = FlowSourceCategory::CyclonicSwirl;
            Swirl.ActivationEnabled     = true;
            Swirl.WorldCentreMetres     = Vector3f{ 0.0f, 0.0f, 1600.0f };
            Swirl.OuterRadiusMetres     = 9000.0f;
            Swirl.InnerEyeRadiusMetres  = 1300.0f;
            Swirl.PeakSpeedMetresPerSec = 34.0f;
            Swirl.CondensationGain      = 0.8f;
            RegisterFlowSource(Exhibit.Flow, Swirl);
            Catalogue.push_back(Exhibit);
        }
        {   // 13 — Low-orbit dayside: curved limb, Rayleigh airglow rim, cloud deck, Cox-Munk glint.
            ExhibitSpecification Exhibit{};
            Exhibit.FileName            = "13_OrbitalDaysideLimb.png";
            Exhibit.OrbitalEnabled      = true; Exhibit.ViewerAltitudeKm = 420.0f; Exhibit.OrbitalCloudCoverage = 0.55f;
            Exhibit.SunElevationDegrees = 32.0f; Exhibit.SunAzimuthDegrees = 0.0f;
            Exhibit.CameraYawDegrees    = 10.0f; Exhibit.CameraPitchDegrees = -14.0f; Exhibit.HorizontalFovDegrees = 78.0f;
            Exhibit.FlowEnabled         = true; Exhibit.Flow = ConstructSynopticFlow(); Exhibit.SequenceSeconds = 900.0f;
            Catalogue.push_back(Exhibit);
        }
        {   // 14 — High orbit across the terminator: city lights, twilight band, vacuum starfield, and moons.
            ExhibitSpecification Exhibit{};
            Exhibit.FileName            = "14_OrbitalTerminatorMoons.png";
            Exhibit.OrbitalEnabled      = true; Exhibit.ViewerAltitudeKm = 2600.0f; Exhibit.OrbitalCloudCoverage = 0.45f;
            Exhibit.SunElevationDegrees = 4.0f; Exhibit.SunAzimuthDegrees = 180.0f;
            Exhibit.CameraYawDegrees    = 90.0f; Exhibit.CameraPitchDegrees = -30.0f; Exhibit.HorizontalFovDegrees = 90.0f;
            Exhibit.Exposure            = 1.0f; Exhibit.SamplesPerPixel = 3u; Exhibit.SequenceSeconds = 300.0f;
            ConfigureDefaultNightSky(Exhibit.Celestial, 0.35f, 0.9f);
            AppendMoon(Exhibit.Celestial, LunarPresetIdentifier::Luna,    -12.0f, 66.0f, 0.040f, 0.0f);
            AppendMoon(Exhibit.Celestial, LunarPresetIdentifier::Glacier, -17.0f, 118.0f, 0.024f, 1.0f);
            Catalogue.push_back(Exhibit);
        }
        {   // 15 — Radiant sun: clean-air afternoon, the disc flooding the optics with its glare bloom and broad aureole.
            ExhibitSpecification Exhibit{};
            Exhibit.FileName            = "15_RadiantSun.png";
            Exhibit.SunElevationDegrees = 24.0f; Exhibit.SunAzimuthDegrees = 0.0f;
            Exhibit.CameraYawDegrees    = 6.0f; Exhibit.CameraPitchDegrees = 14.0f; Exhibit.HorizontalFovDegrees = 90.0f;
            Exhibit.CloudsEnabled       = true; Exhibit.Clouds = BuildCloudGenusPreset(CloudGenusIdentifier::Cumulus, 0.26f);
            Exhibit.FlowEnabled         = true; Exhibit.Flow = ConstructSynopticFlow(); Exhibit.SequenceSeconds = 60.0f;
            Exhibit.SolarGlareStrength  = 1.0f;
            Catalogue.push_back(Exhibit);
        }
        {   // 16 — Muted sun: the identical scene with glare off — photosphere, corona and faint aureole only.
            ExhibitSpecification Exhibit = Catalogue.back();
            Exhibit.FileName            = "16_MutedSun.png";
            Exhibit.SolarGlareStrength  = 0.0f;
            Catalogue.push_back(Exhibit);
        }
        {   // 17 — Morning sea fog: shallow advected height fog plus three drifting fog banks, low radiant sun forward-scattering through them.
            ExhibitSpecification Exhibit{};
            Exhibit.FileName            = "17_MorningSeaFog.png";
            Exhibit.SunElevationDegrees = 8.0f; Exhibit.SunAzimuthDegrees = 0.0f;
            Exhibit.CameraYawDegrees    = 22.0f; Exhibit.CameraPitchDegrees = 4.0f; Exhibit.HorizontalFovDegrees = 85.0f;
            Exhibit.ViewerAltitudeKm    = 0.02f;
            Exhibit.FlowEnabled         = true; Exhibit.Flow = ConstructSynopticFlow(); Exhibit.SequenceSeconds = 60.0f;
            Exhibit.SolarGlareStrength  = 1.0f;
            Exhibit.FogEnabled          = true;
            Exhibit.Fog.Height.GroundExtinctionPerKm = 0.45f;
            Exhibit.Fog.Height.ScaleHeightKm         = 0.08f;
            Exhibit.Fog.Height.FlowVariation         = 0.5f;
            Exhibit.Fog.Height.VariationScaleKm      = 0.6f;
            FogVolumeDescriptor Bank{};
            Bank.WorldCentreMetres = Vector3f{ 1500.0f, 700.0f, 0.0f };   Bank.HalfExtentMetres = Vector3f{ 900.0f, 700.0f, 260.0f };   Bank.ExtinctionPerKm = 5.0f;
            RegisterFogVolume(Exhibit.Fog, Bank);
            Bank.WorldCentreMetres = Vector3f{ 3600.0f, 2300.0f, 0.0f };  Bank.HalfExtentMetres = Vector3f{ 1600.0f, 1200.0f, 380.0f };  Bank.ExtinctionPerKm = 4.0f;
            RegisterFogVolume(Exhibit.Fog, Bank);
            Bank.WorldCentreMetres = Vector3f{ 2400.0f, -900.0f, 0.0f };  Bank.HalfExtentMetres = Vector3f{ 1100.0f, 800.0f, 200.0f };   Bank.ExtinctionPerKm = 6.0f;
            RegisterFogVolume(Exhibit.Fog, Bank);
            Catalogue.push_back(Exhibit);
        }
        {   // 18 — Fog bank caught in a cyclonic swirl: the same force-field operator that spins a cloud vortex winds the fog into a spiral.
            ExhibitSpecification Exhibit{};
            Exhibit.FileName            = "18_FogBankCycloneSwirl.png";
            Exhibit.SunElevationDegrees = 30.0f; Exhibit.SunAzimuthDegrees = 140.0f;
            Exhibit.CameraYawDegrees    = 0.0f; Exhibit.CameraPitchDegrees = -36.0f; Exhibit.HorizontalFovDegrees = 90.0f;
            Exhibit.ViewerAltitudeKm    = 2.2f;
            // Light steering flow so the vortex dominates the parcel paths (a strong synoptic jet would only translate the band).
            Exhibit.FlowEnabled         = true; Exhibit.Flow = ConstructSynopticFlow(); Exhibit.SequenceSeconds = 80.0f;
            Exhibit.Flow.Synoptic.ReferenceVelocity     = Vector3f{ 2.5f, 0.8f, 0.0f };
            Exhibit.Flow.Synoptic.CurlSpeedMetresPerSec = 1.2f;
            // Rankine vortex: solid-body spin inside the eye, ~1/r outside, so the angular velocity falls ~5x from the eyewall to 1.8 km
            // and a straight band laid across it winds into a two-armed spiral; eye subsidence clears the centre.
            FlowSourceDescriptor Swirl{};
            Swirl.Category               = FlowSourceCategory::CyclonicSwirl;
            Swirl.WorldCentreMetres      = Vector3f{ 3000.0f, 0.0f, 150.0f };
            Swirl.PrimaryAxis            = Vector3f{ 0.0f, 0.0f, 1.0f };
            Swirl.OuterRadiusMetres      = 3600.0f;
            Swirl.InnerEyeRadiusMetres   = 600.0f;
            Swirl.VerticalHalfSpanMetres = 900.0f;
            Swirl.PeakSpeedMetresPerSec  = 18.0f;
            Swirl.RadialInflowFraction   = 0.25f;
            Swirl.VerticalLiftSpeed      = 1.2f;
            Swirl.CondensationGain       = 0.6f;
            RegisterFlowSource(Exhibit.Flow, Swirl);
            Exhibit.FogEnabled                       = true;
            Exhibit.Fog.AdvectionPeriodSeconds       = 160.0f;   // single 80 s characteristic at t = P / 2
            Exhibit.Fog.Height.GroundExtinctionPerKm = 0.18f;
            Exhibit.Fog.Height.ScaleHeightKm         = 0.15f;
            FogVolumeDescriptor Band{};
            Band.WorldCentreMetres = Vector3f{ 3000.0f, 0.0f, 0.0f }; Band.HalfExtentMetres = Vector3f{ 3400.0f, 760.0f, 260.0f };
            Band.ExtinctionPerKm   = 7.0f; Band.EdgeSoftness = 0.55f; Band.NoiseStrength = 0.6f; Band.NoiseScaleMetres = 340.0f;
            RegisterFogVolume(Exhibit.Fog, Band);
            Catalogue.push_back(Exhibit);
        }
        // Cloud exhibits integrate 4 Owen-Sobol samples per pixel ahead of the bilateral pass (jittered-march noise floor).
        for (ExhibitSpecification& Exhibit : Catalogue)
            if (Exhibit.CloudsEnabled)
                Exhibit.SamplesPerPixel = std::max(Exhibit.SamplesPerPixel, 4u);
        return Catalogue;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** ArgumentVector)
{
    const std::string OutputDirectory = (ArgumentCount > 1) ? ArgumentVector[1] : "Audit Proof";
    const std::string ExhibitFilter   = (ArgumentCount > 2) ? ArgumentVector[2] : "";
    std::printf("AtmosphereCelestialValidation -> %s\n", OutputDirectory.c_str());

    AtmosphericLookupTables FoundationTables{};
    SynthesizeAtmosphereFoundationTables(Atmosphere::ConstructEarthProfile(), FoundationTables);
    LunarSurfaceAtlas Atlas{};
    LoadLunarSurfaceAtlas(Atlas);

    GateLedger Ledger{};
    ValidatePhysics(Ledger, FoundationTables, Atlas);

    std::printf("\n== Exhibit gallery (%ux%u) ==\n", ExhibitWidth, ExhibitHeight);
    bool GalleryWritten = true;
    for (const ExhibitSpecification& Exhibit : ConstructExhibitCatalogue())
    {
        if (!ExhibitFilter.empty() && Exhibit.FileName.find(ExhibitFilter) == std::string::npos)
            continue;
        GalleryWritten = RenderExhibit(Exhibit, FoundationTables, Atlas, OutputDirectory) && GalleryWritten;
    }
    EnforceGate(Ledger, GalleryWritten, "Exhibit gallery written", "all PNGs", GalleryWritten ? 1.0 : 0.0);

    std::printf("\n%u passed, %u failed\n", Ledger.PassedCount, Ledger.FailedCount);
    return (Ledger.FailedCount == 0u) ? 0 : 1;
}
