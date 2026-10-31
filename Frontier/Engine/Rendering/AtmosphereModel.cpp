/*==========================================================================================================================================
                                                   ATMOSPHERE MODEL DEFINITION
==========================================================================================================================================*/

#include "AtmosphereModel.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

constexpr double Pi = 3.14159265358979323846;

SpatialVector Add(SpatialVector A, SpatialVector B) noexcept { return {A.X + B.X, A.Y + B.Y, A.Z + B.Z}; }
SpatialVector Mul(SpatialVector A, double S) noexcept { return {A.X * S, A.Y * S, A.Z * S}; }
double        Dot(SpatialVector A, SpatialVector B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
double        Length(SpatialVector A) noexcept { return std::sqrt(Dot(A, A)); }

SpectrumRgb ExpNeg(const SpectrumRgb& OpticalDepth) noexcept
{
    return {std::exp(-OpticalDepth.R), std::exp(-OpticalDepth.G), std::exp(-OpticalDepth.B)};
}

SpectrumRgb AddRgb(SpectrumRgb A, SpectrumRgb B) noexcept { return {A.R + B.R, A.G + B.G, A.B + B.B}; }
SpectrumRgb MulRgb(SpectrumRgb A, double S) noexcept { return {A.R * S, A.G * S, A.B * S}; }
SpectrumRgb MulRgb(SpectrumRgb A, SpectrumRgb B) noexcept { return {A.R * B.R, A.G * B.G, A.B * B.B}; }
SpectrumRgb Scale(SpectrumRgb A, double S) noexcept { return {A.R * S, A.G * S, A.B * S}; }

double SegmentMean(double SigmaSDt, double Tau, double SegTrans) noexcept
{
    return (Tau > 1.0e-6) ? (SigmaSDt * (1.0 - SegTrans) / Tau) : SigmaSDt;
}

// Distance from Position along unit Direction to a sphere of Radius (planet-centred); <0 when no hit ahead.
double RaySphereAhead(SpatialVector Position, SpatialVector Direction, double Radius) noexcept
{
    const double B    = Dot(Position, Direction);
    const double C    = Dot(Position, Position) - Radius * Radius;
    const double Disc = B * B - C;
    if (Disc < 0.0)
    {
        return -1.0;
    }
    const double Root = std::sqrt(Disc);
    const double Near = -B - Root;
    return Near >= 0.0 ? Near : (-B + Root >= 0.0 ? -B + Root : -1.0);
}

// Optical depth accumulated along segment A->B (trapezoidal on Steps segments).
SpectrumRgb OpticalDepthBetween(const AtmosphereParameters& P, SpatialVector A, SpatialVector B, uint32_t Steps) noexcept
{
    const SpatialVector Delta{B.X - A.X, B.Y - A.Y, B.Z - A.Z};
    const double        LengthKm = Length(Delta);
    if (LengthKm <= 0.0 || Steps == 0)
    {
        return {};
    }
    double TauR = 0.0, TauM = 0.0, TauO = 0.0;
    const double Dt = LengthKm / static_cast<double>(Steps);
    double PrevR = 0.0, PrevM = 0.0, PrevO = 0.0;
    for (uint32_t I = 0; I <= Steps; ++I)
    {
        const double        F = static_cast<double>(I) / static_cast<double>(Steps);
        const SpatialVector At{A.X + Delta.X * F, A.Y + Delta.Y * F, A.Z + Delta.Z * F};
        const double        Altitude = std::max(Length(At) - P.GroundRadiusKm, 0.0);
        const double        DR = RayleighDensity(P, Altitude);
        const double        DM = MieDensity(P, Altitude);
        const double        DO = OzoneDensity(P, Altitude);
        if (I > 0)
        {
            TauR += 0.5 * (PrevR + DR) * Dt;
            TauM += 0.5 * (PrevM + DM) * Dt;
            TauO += 0.5 * (PrevO + DO) * Dt;
        }
        PrevR = DR; PrevM = DM; PrevO = DO;
    }
    const double MieExtinction = P.MieScattering + P.MieAbsorption;
    return {P.RayleighScattering.R * TauR + MieExtinction * TauM + P.OzoneAbsorption.R * TauO,
            P.RayleighScattering.G * TauR + MieExtinction * TauM + P.OzoneAbsorption.G * TauO,
            P.RayleighScattering.B * TauR + MieExtinction * TauM + P.OzoneAbsorption.B * TauO};
}

double AltitudeToV(const AtmosphereParameters& P, double RadiusKm) noexcept
{
    const double H = std::clamp((RadiusKm - P.GroundRadiusKm) / (P.TopRadiusKm - P.GroundRadiusKm), 0.0, 1.0);
    return std::sqrt(H);
}
double VToAltitudeRadius(const AtmosphereParameters& P, double V) noexcept
{
    return P.GroundRadiusKm + V * V * (P.TopRadiusKm - P.GroundRadiusKm);
}
double MuToU(double Mu) noexcept
{
    return 0.5 + 0.5 * std::copysign(std::pow(std::abs(std::clamp(Mu, -1.0, 1.0)), 0.35), Mu);
}
double UToMu(double U) noexcept
{
    const double S = 2.0 * std::clamp(U, 0.0, 1.0) - 1.0;
    return std::copysign(std::pow(std::abs(S), 1.0 / 0.35), S);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                          MEDIUM
//------------------------------------------------------------------------------------------------------------------------

double RayleighDensity(const AtmosphereParameters& P, double AltitudeKm) noexcept
{
    return std::exp(-std::max(AltitudeKm, 0.0) / P.RayleighScaleHeightKm);
}
double MieDensity(const AtmosphereParameters& P, double AltitudeKm) noexcept
{
    return std::exp(-std::max(AltitudeKm, 0.0) / P.MieScaleHeightKm);
}
double OzoneDensity(const AtmosphereParameters& P, double AltitudeKm) noexcept
{
    return std::max(1.0 - std::abs(AltitudeKm - P.OzonePeakKm) / P.OzoneWidthKm, 0.0);
}

double RayleighPhase(double CosTheta) noexcept
{
    return 3.0 / (16.0 * Pi) * (1.0 + CosTheta * CosTheta);
}
double HenyeyGreensteinPhase(double CosTheta, double G) noexcept
{
    const double Denom = 1.0 + G * G - 2.0 * G * CosTheta;
    return (1.0 - G * G) / (4.0 * Pi * Denom * std::sqrt(std::max(Denom, 1e-12)));
}

// Cornette–Shanks aerosol phase: the demonstration's Rayleigh-shaped HG correction.
double CornetteShanksPhase(double CosTheta, double G) noexcept
{
    const double Cosine = std::clamp(CosTheta, -1.0, 1.0);
    const double Asymmetry = std::clamp(G, -0.999, 0.999);
    return HenyeyGreensteinPhase(Cosine, Asymmetry) *
           1.5 * (1.0 + Cosine * Cosine) / (2.0 + Asymmetry * Asymmetry);
}

SpectrumRgb TransmittanceBetween(const AtmosphereParameters& P, SpatialVector A, SpatialVector B, uint32_t Steps) noexcept
{
    return ExpNeg(OpticalDepthBetween(P, A, B, Steps));
}

SpectrumRgb TransmittanceToTop(const AtmosphereParameters& P, double RadiusKm, double CosZenith, uint32_t Steps) noexcept
{
    // Canonical frame: observer on +Y at RadiusKm, view in the YZ plane by cos-zenith.
    const double        SinZenith = std::sqrt(std::max(1.0 - CosZenith * CosZenith, 0.0));
    const SpatialVector Position{0.0, RadiusKm, 0.0};
    const SpatialVector Direction{0.0, CosZenith, SinZenith};

    const double ToGround = RaySphereAhead(Position, Direction, P.GroundRadiusKm);
    const double ToTop    = RaySphereAhead(Position, Direction, P.TopRadiusKm);
    double       LengthKm = ToTop;
    if (ToGround > 1e-4 && (LengthKm < 0.0 || ToGround < LengthKm))   // > eps: tangent self-hit is no hit
    {
        LengthKm = ToGround;   // looking down: integrate to the surface
    }
    if (LengthKm < 0.0)
    {
        return {1.0, 1.0, 1.0};
    }
    const SpatialVector End = Add(Position, Mul(Direction, LengthKm));
    return ExpNeg(OpticalDepthBetween(P, Position, End, Steps));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRANSMITTANCE LUT
//------------------------------------------------------------------------------------------------------------------------

bool TransmittanceLut::Bake(const AtmosphereParameters& P, uint32_t WidthMu, uint32_t HeightAltitude,
                            uint32_t IntegrationSteps)
{
    Values_.clear();
    if (WidthMu < 2 || HeightAltitude < 2)
    {
        WidthMu_ = HeightAltitude_ = 0;
        return false;
    }
    WidthMu_        = WidthMu;
    HeightAltitude_ = HeightAltitude;
    Values_.resize(static_cast<size_t>(WidthMu) * HeightAltitude);
    for (uint32_t Row = 0; Row < HeightAltitude; ++Row)
    {
        const double V        = static_cast<double>(Row) / static_cast<double>(HeightAltitude - 1);
        const double RadiusKm = VToAltitudeRadius(P, V);
        for (uint32_t Col = 0; Col < WidthMu; ++Col)
        {
            const double U  = static_cast<double>(Col) / static_cast<double>(WidthMu - 1);
            const double Mu = UToMu(U);
            Values_[static_cast<size_t>(Row) * WidthMu + Col] = TransmittanceToTop(P, RadiusKm, Mu, IntegrationSteps);
        }
    }
    return true;
}

SpectrumRgb TransmittanceLut::Sample(const AtmosphereParameters& P, double RadiusKm, double CosZenith) const noexcept
{
    if (Values_.empty())
    {
        return TransmittanceToTop(P, RadiusKm, CosZenith);
    }
    const double U  = MuToU(CosZenith) * static_cast<double>(WidthMu_ - 1);
    const double V  = AltitudeToV(P, RadiusKm) * static_cast<double>(HeightAltitude_ - 1);
    const uint32_t X0 = std::min(static_cast<uint32_t>(U), WidthMu_ - 2);
    const uint32_t Y0 = std::min(static_cast<uint32_t>(V), HeightAltitude_ - 2);
    const double FX = U - X0, FY = V - Y0;
    const auto At = [&](uint32_t X, uint32_t Y) { return Values_[static_cast<size_t>(Y) * WidthMu_ + X]; };
    const SpectrumRgb C00 = AddRgb(MulRgb(At(X0, Y0), 1 - FX), MulRgb(At(X0 + 1, Y0), FX));
    const SpectrumRgb C10 = AddRgb(MulRgb(At(X0, Y0 + 1), 1 - FX), MulRgb(At(X0 + 1, Y0 + 1), FX));
    return AddRgb(MulRgb(C00, 1 - FY), MulRgb(C10, FY));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       SCATTERING
//------------------------------------------------------------------------------------------------------------------------

SpectrumRgb SingleScattering(const AtmosphereParameters& P, SpatialVector PositionKm, SpatialVector Direction,
                             SpatialVector SunDirection, const SpectrumRgb& SunIrradiance,
                             const TransmittanceLut* Lut, uint32_t Steps) noexcept
{
    const double ToGround = RaySphereAhead(PositionKm, Direction, P.GroundRadiusKm);
    const double ToTop    = RaySphereAhead(PositionKm, Direction, P.TopRadiusKm);
    double       LengthKm = ToTop;
    if (ToGround > 1e-4 && (LengthKm < 0.0 || ToGround < LengthKm))   // > eps: tangent self-hit is no hit
    {
        LengthKm = ToGround;
    }
    if (LengthKm <= 0.0 || Steps == 0)
    {
        return {};
    }
    const double CosTheta = Dot(Direction, SunDirection);
    const double PhaseR   = RayleighPhase(CosTheta);
    const double PhaseM   = CornetteShanksPhase(CosTheta, P.MiePhaseG);
    const double Dt       = LengthKm / static_cast<double>(Steps);

    SpectrumRgb Radiance{};
    // March with running transmittance from the eye (segment-by-segment, trapezoidal densities).
    SpectrumRgb ViewTransmittance{1.0, 1.0, 1.0};
    for (uint32_t I = 0; I < Steps; ++I)
    {
        const double        T0 = Dt * static_cast<double>(I);
        const double        T1 = Dt * static_cast<double>(I + 1);
        const SpatialVector At0 = Add(PositionKm, Mul(Direction, T0));
        const SpatialVector At1 = Add(PositionKm, Mul(Direction, T1));
        const SpectrumRgb   SegmentDepth = OpticalDepthBetween(P, At0, At1, 1);
        const SpectrumRgb   SegmentTransmittance = ExpNeg(SegmentDepth);

        // Sample at segment midpoint: sun transmittance from there to the top along the sun dir.
        const SpatialVector Mid      = Add(PositionKm, Mul(Direction, 0.5 * (T0 + T1)));
        const double        MidRadius = Length(Mid);
        const double        Altitude  = std::max(MidRadius - P.GroundRadiusKm, 0.0);
        const double        CosSunZenith = Dot(Mul(Mid, 1.0 / std::max(MidRadius, 1e-9)), SunDirection);
        const double        SunGroundHit = RaySphereAhead(Mid, SunDirection, P.GroundRadiusKm);
        const bool          SunVisible = SunGroundHit < 0.0 || SunGroundHit <= 1e-4;
        if (SunVisible)
        {
            const SpectrumRgb SunT = Lut != nullptr ? Lut->Sample(P, MidRadius, CosSunZenith)
                                                    : TransmittanceToTop(P, MidRadius, CosSunZenith, 24);
            const double DnsR = RayleighDensity(P, Altitude);
            const double DnsM = MieDensity(P, Altitude);
            // Integral of (1 - exp(-tau_seg * s)) weighting: use the segment-mean transmittance
            // closed form per channel via the total-transmittance ratio (exact for the segment's
            // own extinction when scattering coefficient were constant within it; evaluated per
            // component with its share of the segment optical depth).
            const auto SegmentMean = [](double TauComponent, double TauTotal, double SegT) {
                if (TauTotal <= 1e-12)
                {
                    return 0.0;
                }
                return (TauComponent / TauTotal) * (1.0 - SegT);
            };
            const double SegR_R = P.RayleighScattering.R * DnsR * Dt;
            const double SegR_G = P.RayleighScattering.G * DnsR * Dt;
            const double SegR_B = P.RayleighScattering.B * DnsR * Dt;
            const double SegM   = P.MieScattering * DnsM * Dt;
            const SpectrumRgb Scatter{
                PhaseR * SegmentMean(SegR_R, SegmentDepth.R, SegmentTransmittance.R) +
                    PhaseM * SegmentMean(SegM, SegmentDepth.R, SegmentTransmittance.R),
                PhaseR * SegmentMean(SegR_G, SegmentDepth.G, SegmentTransmittance.G) +
                    PhaseM * SegmentMean(SegM, SegmentDepth.G, SegmentTransmittance.G),
                PhaseR * SegmentMean(SegR_B, SegmentDepth.B, SegmentTransmittance.B) +
                    PhaseM * SegmentMean(SegM, SegmentDepth.B, SegmentTransmittance.B)};
            Radiance = AddRgb(Radiance, MulRgb(MulRgb(MulRgb(ViewTransmittance, SunT), SunIrradiance), Scatter));
        }
        ViewTransmittance = MulRgb(ViewTransmittance, SegmentTransmittance);
    }
    return Radiance;
}

SpectrumRgb MultipleScatteringApprox(const AtmosphereParameters& P, SpatialVector PositionKm,
                                     SpatialVector Direction, SpatialVector SunDirection,
                                     const SpectrumRgb& SingleScattered, const SpectrumRgb& SunIrradiance) noexcept
{
    // Directional average of sun transmittance at the observer (fixed 8x4 sphere lattice),
    // turned into an isotropic second-bounce factor ~ albedo-weighted throughput of the air mass.
    (void)Direction;
    const double RadiusKm = Length(PositionKm);
    const SpatialVector Up = Mul(PositionKm, 1.0 / std::max(RadiusKm, 1e-9));
    double AverageR = 0.0, AverageG = 0.0, AverageB = 0.0;
    uint32_t Count = 0;
    for (uint32_t Ring = 0; Ring < 4; ++Ring)
    {
        const double CosZenith = -0.75 + 0.5 * static_cast<double>(Ring);
        const SpectrumRgb T = TransmittanceToTop(P, RadiusKm, CosZenith, 24);
        AverageR += T.R; AverageG += T.G; AverageB += T.B;
        ++Count;
    }
    const SpectrumRgb Average{AverageR / Count, AverageG / Count, AverageB / Count};
    // 📝 Sun gating: the return is gathered sunlight, so it carries the sun's own transmittance at
    //    the observer and vanishes when the ground sphere occults the sun. (The previous form was
    //    sun-independent and returned its full factor at midnight.)
    const double      CosSunZenith = Dot(Up, SunDirection);
    const double      SunGroundHit = RaySphereAhead(PositionKm, SunDirection, P.GroundRadiusKm);
    const bool        SunUp        = SunGroundHit < 0.0 || SunGroundHit <= 1e-4;
    const SpectrumRgb SunT         = SunUp ? TransmittanceToTop(P, RadiusKm, CosSunZenith, 24) : SpectrumRgb{};
    // Spherical-albedo return ψ/(1−ψ) with ψ the direction-averaged atmospheric reflectance. The
    // 0.35 calibration is the clear-air spherical albedo scale (ψ ≲ 0.3): it keeps the boost in the
    // measured 10–30% range over single scattering instead of the previous unbounded ~190%, which
    // also desaturated the zenith. GroundAlbedo enters as the surface-return term.
    const double      Altitude  = std::max(RadiusKm - P.GroundRadiusKm, 0.0);
    const double      AirAlbedo = std::clamp(0.6 * RayleighDensity(P, Altitude) + 0.1, 0.0, 0.9);
    const auto        ChannelFactor = [](double AverageT, double SunTransmittance, double ReturnAlbedo,
                                          double GroundAlbedo) {
        const double Psi = std::clamp(AverageT * ReturnAlbedo * 0.35 + GroundAlbedo * 0.06, 0.0, 0.55);
        return std::clamp(Psi / (1.0 - Psi), 0.0, 1.2) * SunTransmittance;
    };
    const SpectrumRgb Factor{ChannelFactor(Average.R, SunT.R, AirAlbedo, P.GroundAlbedo),
                             ChannelFactor(Average.G, SunT.G, AirAlbedo, P.GroundAlbedo),
                             ChannelFactor(Average.B, SunT.B, AirAlbedo, P.GroundAlbedo)};
    (void)SunIrradiance;
    return MulRgb(SingleScattered, Factor);
}

//------------------------------------------------------------------------------------------------------------------------
//                                      MULTI-SCATTERING, SKY-VIEW & AERIAL PERSPECTIVE
//------------------------------------------------------------------------------------------------------------------------

bool MultiScatteringLut::Bake(const AtmosphereParameters& P,
                              const TransmittanceLut&     Transmittance,
                              uint32_t                    WidthSunCos,
                              uint32_t                    HeightAltitude,
                              uint32_t                    DirectionCount)
{
    if (WidthSunCos < 2 || HeightAltitude < 2 || DirectionCount == 0 || P.TopRadiusKm <= P.GroundRadiusKm)
    {
        return false;
    }
    WidthSunCos_    = WidthSunCos;
    HeightAltitude_ = HeightAltitude;
    Values_.assign(static_cast<size_t>(WidthSunCos) * HeightAltitude, SpectrumRgb{});

    const double SpanKm = P.TopRadiusKm - P.GroundRadiusKm;
    const uint32_t AzimuthRings = std::max(4u, DirectionCount);
    constexpr uint32_t MarchSteps = 16;

    for (uint32_t Row = 0; Row < HeightAltitude; ++Row)
    {
        const double V        = (static_cast<double>(Row) + 0.5) / static_cast<double>(HeightAltitude);
        const double RadiusKm = P.GroundRadiusKm + std::clamp(V * V * SpanKm, 1e-3, SpanKm - 1e-3);
        const SpatialVector PosKm{0.0, RadiusKm, 0.0};

        for (uint32_t Col = 0; Col < WidthSunCos; ++Col)
        {
            const double U            = (static_cast<double>(Col) + 0.5) / static_cast<double>(WidthSunCos);
            const double CosSunZenith = std::clamp(2.0 * U - 1.0, -1.0, 1.0);
            const double SinSunZenith = std::sqrt(std::max(0.0, 1.0 - CosSunZenith * CosSunZenith));
            const SpatialVector SunDir{SinSunZenith, CosSunZenith, 0.0};

            SpectrumRgb LSecond{};
            SpectrumRgb Fms{};
            const double SolidWeight = (4.0 * Pi) / static_cast<double>(AzimuthRings);
            const double IsoPhase    = 1.0 / (4.0 * Pi);

            for (uint32_t DirIdx = 0; DirIdx < AzimuthRings; ++DirIdx)
            {
                const double Mu    = -1.0 + 2.0 * (static_cast<double>(DirIdx) + 0.5) / static_cast<double>(AzimuthRings);
                const double SinMu = std::sqrt(std::max(0.0, 1.0 - Mu * Mu));
                const double Phi   = 2.399963229728653 * static_cast<double>(DirIdx);
                const SpatialVector RayDir{SinMu * std::cos(Phi), Mu, SinMu * std::sin(Phi)};

                const double ToGround = RaySphereAhead(PosKm, RayDir, P.GroundRadiusKm);
                const double ToTop    = RaySphereAhead(PosKm, RayDir, P.TopRadiusKm);
                double       RayLen   = ToTop;
                const bool   HitsGround = (ToGround > 0.0 && (RayLen < 0.0 || ToGround < RayLen));
                if (HitsGround)
                {
                    RayLen = ToGround;
                }
                if (RayLen <= 0.0)
                {
                    continue;
                }

                const double Dt = RayLen / static_cast<double>(MarchSteps);
                SpectrumRgb  ViewT{1.0, 1.0, 1.0};
                SpectrumRgb  DirInscatter{};
                SpectrumRgb  DirTransfer{};

                for (uint32_t S = 0; S < MarchSteps; ++S)
                {
                    const SpatialVector At0 = Add(PosKm, Mul(RayDir, Dt * static_cast<double>(S)));
                    const SpatialVector At1 = Add(PosKm, Mul(RayDir, Dt * static_cast<double>(S + 1)));
                    const SpatialVector Mid = Mul(Add(At0, At1), 0.5);
                    const SpectrumRgb   SegDepth = OpticalDepthBetween(P, At0, At1, 2);
                    const SpectrumRgb   SegT     = ExpNeg(SegDepth);

                    const double MidR   = Length(Mid);
                    const double AltKm  = std::max(MidR - P.GroundRadiusKm, 0.0);
                    const double DnsR   = RayleighDensity(P, AltKm);
                    const double DnsM   = MieDensity(P, AltKm);
                    const SpectrumRgb SigmaS{
                        P.RayleighScattering.R * DnsR + P.MieScattering * DnsM,
                        P.RayleighScattering.G * DnsR + P.MieScattering * DnsM,
                        P.RayleighScattering.B * DnsR + P.MieScattering * DnsM};
                    const SpectrumRgb ScatterIntegral{
                        SegmentMean(SigmaS.R * Dt, SegDepth.R, SegT.R),
                        SegmentMean(SigmaS.G * Dt, SegDepth.G, SegT.G),
                        SegmentMean(SigmaS.B * Dt, SegDepth.B, SegT.B)};

                    DirTransfer = AddRgb(DirTransfer, MulRgb(ViewT, Scale(ScatterIntegral, IsoPhase)));

                    const double SunGround = RaySphereAhead(Mid, SunDir, P.GroundRadiusKm);
                    if (SunGround < 0.0 || SunGround <= 1e-4)
                    {
                        const double CosSun = Dot(Mul(Mid, 1.0 / std::max(MidR, 1e-9)), SunDir);
                        const SpectrumRgb SunT = Transmittance.Baked()
                                                     ? Transmittance.Sample(P, MidR, CosSun)
                                                     : TransmittanceToTop(P, MidR, CosSun, 16);
                        const double CosScatter = Dot(RayDir, SunDir);
                        const double Pr         = RayleighPhase(CosScatter);
                        const double Pm         = CornetteShanksPhase(CosScatter, P.MiePhaseG);
                        const SpectrumRgb PhaseWeighted{
                            SegmentMean((P.RayleighScattering.R * DnsR * Pr + P.MieScattering * DnsM * Pm) * Dt,
                                        SegDepth.R, SegT.R),
                            SegmentMean((P.RayleighScattering.G * DnsR * Pr + P.MieScattering * DnsM * Pm) * Dt,
                                        SegDepth.G, SegT.G),
                            SegmentMean((P.RayleighScattering.B * DnsR * Pr + P.MieScattering * DnsM * Pm) * Dt,
                                        SegDepth.B, SegT.B)};
                        DirInscatter = AddRgb(DirInscatter, MulRgb(MulRgb(ViewT, SunT), PhaseWeighted));
                    }
                    ViewT = MulRgb(ViewT, SegT);
                }

                if (HitsGround && P.GroundAlbedo > 0.0)
                {
                    const SpatialVector GroundPt = Add(PosKm, Mul(RayDir, RayLen));
                    const SpatialVector GroundUp = Mul(GroundPt, 1.0 / std::max(Length(GroundPt), 1e-9));
                    const double        CosGroundSun = Dot(GroundUp, SunDir);
                    if (CosGroundSun > 0.0)
                    {
                        const SpectrumRgb SunGroundT = Transmittance.Baked()
                                                           ? Transmittance.Sample(P, P.GroundRadiusKm + 1e-3, CosGroundSun)
                                                           : TransmittanceToTop(P, P.GroundRadiusKm + 1e-3, CosGroundSun, 16);
                        const double Brdf = (P.GroundAlbedo / Pi) * CosGroundSun;
                        DirInscatter = AddRgb(DirInscatter, Scale(MulRgb(ViewT, SunGroundT), Brdf));
                    }
                }

                LSecond = AddRgb(LSecond, Scale(DirInscatter, SolidWeight * IsoPhase));
                Fms     = AddRgb(Fms, Scale(DirTransfer, SolidWeight));
            }

            const auto SolveSeries = [](double L, double F) {
                return std::max(0.0, L / std::max(1.0 - std::clamp(F, 0.0, 0.92), 0.08));
            };
            Values_[static_cast<size_t>(Row) * WidthSunCos + Col] = {
                SolveSeries(LSecond.R, Fms.R),
                SolveSeries(LSecond.G, Fms.G),
                SolveSeries(LSecond.B, Fms.B)};
        }
    }
    return true;
}

SpectrumRgb MultiScatteringLut::Sample(const AtmosphereParameters& P, double RadiusKm, double CosSunZenith) const noexcept
{
    if (Values_.empty())
    {
        return {};
    }
    const double SpanKm = std::max(P.TopRadiusKm - P.GroundRadiusKm, 1e-6);
    const double AltFrac = std::clamp((RadiusKm - P.GroundRadiusKm) / SpanKm, 0.0, 1.0);
    const double V = std::sqrt(AltFrac);
    const double U = std::clamp(0.5 + 0.5 * CosSunZenith, 0.0, 1.0);

    const double Fx = U * static_cast<double>(WidthSunCos_ - 1);
    const double Fy = V * static_cast<double>(HeightAltitude_ - 1);
    const uint32_t X0 = static_cast<uint32_t>(Fx);
    const uint32_t Y0 = static_cast<uint32_t>(Fy);
    const uint32_t X1 = std::min(X0 + 1, WidthSunCos_ - 1);
    const uint32_t Y1 = std::min(Y0 + 1, HeightAltitude_ - 1);
    const double   Tx = Fx - static_cast<double>(X0);
    const double   Ty = Fy - static_cast<double>(Y0);

    const auto At = [&](uint32_t X, uint32_t Y) -> const SpectrumRgb& {
        return Values_[static_cast<size_t>(Y) * WidthSunCos_ + X];
    };
    const auto Lerp = [](const SpectrumRgb& A, const SpectrumRgb& B, double T) -> SpectrumRgb {
        return {A.R + (B.R - A.R) * T, A.G + (B.G - A.G) * T, A.B + (B.B - A.B) * T};
    };
    return Lerp(Lerp(At(X0, Y0), At(X1, Y0), Tx), Lerp(At(X0, Y1), At(X1, Y1), Tx), Ty);
}

SpectrumRgb EvaluateTwilightArchRadiance(SpatialVector      LocalDirectionUnit,
                                         SpatialVector      LocalSunDirectionUnit,
                                         const SpectrumRgb& SunIrradiance,
                                         double             TwilightStrength) noexcept
{
    if (TwilightStrength <= 0.0)
    {
        return {};
    }
    const double DirLen = Length(LocalDirectionUnit);
    const double SunLen = Length(LocalSunDirectionUnit);
    if (DirLen <= 1e-9 || SunLen <= 1e-9)
    {
        return {};
    }
    const SpatialVector D = Mul(LocalDirectionUnit, 1.0 / DirLen);
    const SpatialVector S = Mul(LocalSunDirectionUnit, 1.0 / SunLen);

    // Solar elevation in degrees: twilight arch peaks between +3 deg golden hour and -14 deg nautical/astronomical dusk.
    const double SunElevDeg = std::asin(std::clamp(S.Z, -1.0, 1.0)) * (180.0 / Pi);
    if (SunElevDeg > 6.5 || SunElevDeg < -18.5)
    {
        return {};
    }
    const double WindowRising  = std::clamp((SunElevDeg + 18.0) / 8.0, 0.0, 1.0);
    const double WindowSetting = std::clamp((6.0 - SunElevDeg) / 7.5, 0.0, 1.0);
    const double TwilightGate  = WindowRising * WindowSetting * TwilightStrength;
    if (TwilightGate <= 1e-5)
    {
        return {};
    }

    const double ViewElev = std::max(D.Z, -0.04);
    if (ViewElev < -0.03)
    {
        return {};
    }
    const double HorizBand   = std::exp(-std::pow(std::max(D.Z, 0.0) / 0.19, 1.45));
    const double HorizDot    = (D.X * S.X + D.Y * S.Y) /
                               std::max(1e-4, std::hypot(D.X, D.Y) * std::hypot(S.X, S.Y));
    const double SolarAzPeak = std::pow(0.5 + 0.5 * std::clamp(HorizDot, -1.0, 1.0), 2.8);
    const double AntiSolar   = std::pow(0.5 - 0.5 * std::clamp(HorizDot, -1.0, 1.0), 2.2) *
                               std::exp(-std::pow((D.Z - 0.11) / 0.12, 2.0));

    // Warm forward Chappuis-filtered sunset arch + subtle rose/violet Belt of Venus opposite the setting sun.
    const SpectrumRgb WarmArch{0.022 * SolarAzPeak * HorizBand,
                               0.0085 * SolarAzPeak * HorizBand,
                               0.0024 * SolarAzPeak * HorizBand};
    const SpectrumRgb BeltOfVenus{0.0042 * AntiSolar, 0.0026 * AntiSolar, 0.0058 * AntiSolar};
    const SpectrumRgb Combined = Scale(AddRgb(WarmArch, BeltOfVenus), TwilightGate);
    return MulRgb(SunIrradiance, Combined);
}

bool SkyViewLut::Bake(const AtmosphereParameters&            P,
                      const TransmittanceLut&                Transmittance,
                      const MultiScatteringLut&              MultiScatter,
                      double                                 ObserverAltitudeKm,
                      std::span<const AtmosphereSolarSource> Suns,
                      uint32_t                               WidthAzimuth,
                      uint32_t                               HeightElevation,
                      uint32_t                               MarchSteps)
{
    if (WidthAzimuth < 4 || HeightElevation < 4 || MarchSteps == 0 || P.TopRadiusKm <= P.GroundRadiusKm)
    {
        return false;
    }
    WidthAzimuth_    = WidthAzimuth;
    HeightElevation_ = HeightElevation;
    Values_.assign(static_cast<size_t>(WidthAzimuth) * HeightElevation, SpectrumRgb{});

    const double SafeAltKm = std::clamp(ObserverAltitudeKm, 1e-3, (P.TopRadiusKm - P.GroundRadiusKm) - 1e-3);
    const double RadiusKm  = P.GroundRadiusKm + SafeAltKm;
    const SpatialVector CanonicalPos{0.0, RadiusKm, 0.0};

    for (uint32_t Row = 0; Row < HeightElevation; ++Row)
    {
        // Horizon-stretched elevation parameterization: concentrates 50% of rows near the horizon!
        const double V       = (static_cast<double>(Row) + 0.5) / static_cast<double>(HeightElevation);
        const double Signed  = 2.0 * V - 1.0;
        const double ElevRad = (Signed >= 0.0 ? 1.0 : -1.0) * (Signed * Signed) * (0.5 * Pi);
        const double SinEl   = std::sin(ElevRad);
        const double CosEl   = std::cos(ElevRad);

        for (uint32_t Col = 0; Col < WidthAzimuth; ++Col)
        {
            const double AzRad = 2.0 * Pi * (static_cast<double>(Col) + 0.5) / static_cast<double>(WidthAzimuth);
            // Local frame: X = east, Y = north, Z = up; Canonical frame: X = east, Y = up, Z = north.
            const SpatialVector LocalDir{CosEl * std::sin(AzRad), CosEl * std::cos(AzRad), SinEl};
            const SpatialVector CanonicalDir{LocalDir.X, LocalDir.Z, LocalDir.Y};

            SpectrumRgb Accum{};
            for (const AtmosphereSolarSource& Sun : Suns)
            {
                const double SunLen = Length(Sun.Direction);
                if (SunLen <= 1e-9)
                {
                    continue;
                }
                const SpatialVector LocalSun     = Mul(Sun.Direction, 1.0 / SunLen);
                const SpatialVector CanonicalSun = {LocalSun.X, LocalSun.Z, LocalSun.Y};

                const SpectrumRgb Single = SingleScattering(
                    P, CanonicalPos, CanonicalDir, CanonicalSun, Sun.Irradiance,
                    Transmittance.Baked() ? &Transmittance : nullptr, MarchSteps);

                SpectrumRgb Multi{};
                if (MultiScatter.Baked())
                {
                    const SpectrumRgb Psi = MultiScatter.Sample(P, RadiusKm, LocalSun.Z);
                    const double      DnsR = RayleighDensity(P, SafeAltKm);
                    const double      DnsM = MieDensity(P, SafeAltKm);
                    const double      PathScale = std::min(35.0, 8.0 / std::max(0.18, std::abs(LocalDir.Z) + 0.08));
                    const SpectrumRgb ScatterCol{
                        (P.RayleighScattering.R * DnsR + P.MieScattering * DnsM) * PathScale,
                        (P.RayleighScattering.G * DnsR + P.MieScattering * DnsM) * PathScale,
                        (P.RayleighScattering.B * DnsR + P.MieScattering * DnsM) * PathScale};
                    Multi = MulRgb(MulRgb(Psi, ScatterCol), Sun.Irradiance);
                }
                else
                {
                    Multi = MultipleScatteringApprox(P, CanonicalPos, CanonicalDir, CanonicalSun, Single, Sun.Irradiance);
                }

                const SpectrumRgb Twilight = EvaluateTwilightArchRadiance(
                    LocalDir, LocalSun, Sun.Irradiance, Sun.TwilightStrength);
                Accum = AddRgb(Accum, AddRgb(AddRgb(Single, Multi), Twilight));
            }

            Values_[static_cast<size_t>(Row) * WidthAzimuth + Col] = Accum;
        }
    }
    return true;
}

SpectrumRgb SkyViewLut::Sample(SpatialVector LocalDirectionUnit) const noexcept
{
    if (Values_.empty())
    {
        return {};
    }
    const double Len = Length(LocalDirectionUnit);
    if (Len <= 1e-9)
    {
        return {};
    }
    const SpatialVector D = Mul(LocalDirectionUnit, 1.0 / Len);
    const double ElevRad  = std::asin(std::clamp(D.Z, -1.0, 1.0));
    double       AzRad    = std::atan2(D.X, D.Y);
    if (AzRad < 0.0)
    {
        AzRad += 2.0 * Pi;
    }

    const double NormEl = ElevRad / (0.5 * Pi);
    const double V      = std::clamp(0.5 + 0.5 * (NormEl >= 0.0 ? 1.0 : -1.0) * std::sqrt(std::abs(NormEl)), 0.0, 1.0);
    const double U      = std::clamp(AzRad / (2.0 * Pi), 0.0, 1.0);

    const double Fx = U * static_cast<double>(WidthAzimuth_);
    const double Fy = std::clamp(V * static_cast<double>(HeightElevation_) - 0.5, 0.0, static_cast<double>(HeightElevation_ - 1));

    const uint32_t X0 = static_cast<uint32_t>(std::floor(Fx)) % WidthAzimuth_;
    const uint32_t X1 = (X0 + 1) % WidthAzimuth_;
    const uint32_t Y0 = static_cast<uint32_t>(Fy);
    const uint32_t Y1 = std::min(Y0 + 1, HeightElevation_ - 1);
    const double   Tx = Fx - std::floor(Fx);
    const double   Ty = Fy - static_cast<double>(Y0);

    const auto At = [&](uint32_t X, uint32_t Y) -> const SpectrumRgb& {
        return Values_[static_cast<size_t>(Y) * WidthAzimuth_ + X];
    };
    const auto Lerp = [](const SpectrumRgb& A, const SpectrumRgb& B, double T) -> SpectrumRgb {
        return {A.R + (B.R - A.R) * T, A.G + (B.G - A.G) * T, A.B + (B.B - A.B) * T};
    };
    return Lerp(Lerp(At(X0, Y0), At(X1, Y0), Tx), Lerp(At(X0, Y1), At(X1, Y1), Tx), Ty);
}

AerialPerspectiveSample EvaluateAerialPerspective(
    const AtmosphereParameters&            P,
    const TransmittanceLut*                Transmittance,
    const MultiScatteringLut*              MultiScatter,
    double                                 ObserverAltitudeKm,
    SpatialVector                          OriginLocalMeters,
    SpatialVector                          DirectionLocalUnit,
    double                                 SurfaceDistanceMeters,
    std::span<const AtmosphereSolarSource> Suns,
    uint32_t                               Steps) noexcept
{
    AerialPerspectiveSample Result;
    const double DirLen = Length(DirectionLocalUnit);
    if (SurfaceDistanceMeters <= 0.0 || Steps == 0 || DirLen <= 1e-9)
    {
        return Result;
    }
    const SpatialVector LocalDir     = Mul(DirectionLocalUnit, 1.0 / DirLen);
    const SpatialVector CanonicalDir = {LocalDir.X, LocalDir.Z, LocalDir.Y};
    const SpatialVector StartKm{1e-3 * OriginLocalMeters.X,
                                P.GroundRadiusKm + std::max(1e-3, ObserverAltitudeKm + 1e-3 * OriginLocalMeters.Z),
                                1e-3 * OriginLocalMeters.Y};
    const double LengthKm = 1e-3 * SurfaceDistanceMeters;
    const double Dt       = LengthKm / static_cast<double>(Steps);

    SpectrumRgb ViewT{1.0, 1.0, 1.0};
    SpectrumRgb Inscatter{};

    for (uint32_t Step = 0; Step < Steps; ++Step)
    {
        const SpatialVector At0      = Add(StartKm, Mul(CanonicalDir, Dt * static_cast<double>(Step)));
        const SpatialVector At1      = Add(StartKm, Mul(CanonicalDir, Dt * static_cast<double>(Step + 1)));
        const SpatialVector Mid      = Mul(Add(At0, At1), 0.5);
        const SpectrumRgb   SegDepth = OpticalDepthBetween(P, At0, At1, 2);
        const SpectrumRgb   SegT     = ExpNeg(SegDepth);

        const double MidR  = Length(Mid);
        const double AltKm = std::max(MidR - P.GroundRadiusKm, 0.0);
        const double DnsR  = RayleighDensity(P, AltKm);
        const double DnsM  = MieDensity(P, AltKm);

        for (const AtmosphereSolarSource& Sun : Suns)
        {
            const double SunLen = Length(Sun.Direction);
            if (SunLen <= 1e-9)
            {
                continue;
            }
            const SpatialVector LocalSun     = Mul(Sun.Direction, 1.0 / SunLen);
            const SpatialVector CanonicalSun = {LocalSun.X, LocalSun.Z, LocalSun.Y};
            const double        SunGround    = RaySphereAhead(Mid, CanonicalSun, P.GroundRadiusKm);
            if (SunGround >= 0.0 && SunGround > 1e-4)
            {
                continue;
            }
            const double      CosSun = Dot(Mul(Mid, 1.0 / std::max(MidR, 1e-9)), CanonicalSun);
            const SpectrumRgb SunT   = (Transmittance && Transmittance->Baked())
                                           ? Transmittance->Sample(P, MidR, CosSun)
                                           : TransmittanceToTop(P, MidR, CosSun, 16);
            const double CosScatter  = Dot(CanonicalDir, CanonicalSun);
            const double Pr          = RayleighPhase(CosScatter);
            const double Pm          = CornetteShanksPhase(CosScatter, P.MiePhaseG);
            SpectrumRgb  Scatter{
                Pr * SegmentMean(P.RayleighScattering.R * DnsR * Dt, SegDepth.R, SegT.R) +
                    Pm * SegmentMean(P.MieScattering * DnsM * Dt, SegDepth.R, SegT.R),
                Pr * SegmentMean(P.RayleighScattering.G * DnsR * Dt, SegDepth.G, SegT.G) +
                    Pm * SegmentMean(P.MieScattering * DnsM * Dt, SegDepth.G, SegT.G),
                Pr * SegmentMean(P.RayleighScattering.B * DnsR * Dt, SegDepth.B, SegT.B) +
                    Pm * SegmentMean(P.MieScattering * DnsM * Dt, SegDepth.B, SegT.B)};

            SpectrumRgb DirectTerm = MulRgb(MulRgb(SunT, Sun.Irradiance), Scatter);
            if (MultiScatter && MultiScatter->Baked())
            {
                const SpectrumRgb Psi = MultiScatter->Sample(P, MidR, CosSun);
                const SpectrumRgb MsStep{
                    (P.RayleighScattering.R * DnsR + P.MieScattering * DnsM) * Dt * Psi.R * Sun.Irradiance.R,
                    (P.RayleighScattering.G * DnsR + P.MieScattering * DnsM) * Dt * Psi.G * Sun.Irradiance.G,
                    (P.RayleighScattering.B * DnsR + P.MieScattering * DnsM) * Dt * Psi.B * Sun.Irradiance.B};
                DirectTerm = AddRgb(DirectTerm, MsStep);
            }
            Inscatter = AddRgb(Inscatter, MulRgb(ViewT, DirectTerm));
        }
        ViewT = MulRgb(ViewT, SegT);
    }

    Result.Inscatter     = Inscatter;
    Result.Transmittance = ViewT;
    return Result;
}

} // namespace Frontier
