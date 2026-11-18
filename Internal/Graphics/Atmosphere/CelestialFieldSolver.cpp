/*==============================================================================================================================================
                                                        CELESTIALFIELDSOLVER.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the six-moon lunar system (decoded equirectangular regolith maps, luminance-gradient relief normals, Hapke opposition
//    surge, Lommel-Seeliger + Oren-Nayar regolith BRDF, cryogenic ice specular, planetshine, true 3D ray-marched spherical lunar atmospheres
//    with detached haze and body umbra), the J2000 bright star catalogue + cubemap-indexed Pogson deep field, Ballesteros/Planck Rec.709
//    stellar chromaticity, Airy-Moffat optical PSF, Kasten-Young scintillation, IAU galactic Milky Way + Great Rift, and deep-sky nebulae.

#include "Graphics/Atmosphere/CelestialFieldSolver.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

// 📝 Self-contained static JPEG decoder instance. STB_IMAGE_STATIC keeps every stbi_* symbol internal to this translation unit, so the
//    sole external stb_image instantiation (ParametricSketchMatcapTexture.cpp, PNG-only) never collides at link time (no LNK2005).
#if defined(__has_include)
#if __has_include("stb_image.h")
#define FRONTIER_CELESTIAL_JPEG_DECODER_ENABLED 1
#endif
#endif

#if defined(FRONTIER_CELESTIAL_JPEG_DECODER_ENABLED)
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#endif
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4100 4244 4245 4456 4457 4701)
#endif
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#endif

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    using Atmosphere::EvaluateRotatedFbm3D;
    using Atmosphere::HashLatticeCell;

    constexpr uint32_t CubeCellsPerAxis       = 48u;     // [-]   - Cubemap star-index cells per face edge
    constexpr uint32_t DeepFieldStarCount     = 15200u;  // [-]   - Procedural Pogson deep-field stars (V 4.6 .. 9.6)
    constexpr float    SolarApparentMagnitude = -26.74f; // [mag] - Johnson V apparent magnitude of the Sun
    // [-] - Irradiance of a V = 0 star in solar-calibrated units: E_sun * 10^(0.4 * V_sun) (Pogson, ~2.6e-10)
    const float        StellarFluxReference   = Atmosphere::SolarIlluminanceCalibration * std::pow(10.0f, 0.4f * SolarApparentMagnitude);
    // [-] - Converts the authored relative galactic / nebular brightness to absolute surface brightness (bulge ~20 mag/arcsec^2)
    const float        GalacticRadianceCalibration = StellarFluxReference / 2.4e-5f;

    [[nodiscard]] float SafeSquareRoot(float Argument) noexcept
    {
        return std::sqrt(std::max(0.0f, Argument));
    }

    [[nodiscard]] float EvaluateSmoothStep(float LowerEdge, float UpperEdge, float Sample) noexcept
    {
        const float Ratio = std::clamp((Sample - LowerEdge) / std::max(1.0e-8f, UpperEdge - LowerEdge), 0.0f, 1.0f);
        return Ratio * Ratio * (3.0f - 2.0f * Ratio);
    }

    // 📝 Numerically stable angular separation θ = 2 asin(|a - b| / 2) between two unit vectors (exact near θ → 0).
    [[nodiscard]] float EvaluateAngularSeparation(const Vector3f& FirstAxis, const Vector3f& SecondAxis) noexcept
    {
        const float HalfChord = 0.5f * VectorLength(SubtractVector(FirstAxis, SecondAxis));
        return 2.0f * std::asin(std::clamp(HalfChord, 0.0f, 1.0f));
    }

    [[nodiscard]] Vector3f ConstructEquatorialAxis(float RightAscensionRadians, float DeclinationRadians) noexcept
    {
        const float CosineDeclination = std::cos(DeclinationRadians);
        return Vector3f{ CosineDeclination * std::cos(RightAscensionRadians), CosineDeclination * std::sin(RightAscensionRadians), std::sin(DeclinationRadians) };
    }

    [[nodiscard]] uint32_t HashInteger(uint32_t Word) noexcept
    {
        Word ^= Word >> 16u;
        Word *= 0x7FEB352Du;
        Word ^= Word >> 15u;
        Word *= 0x846CA68Bu;
        Word ^= Word >> 16u;
        return Word;
    }

    [[nodiscard]] float HashUnitInterval(uint32_t Word) noexcept
    {
        return static_cast<float>(HashInteger(Word) >> 8u) * (1.0f / 16777216.0f);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                     EQUATORIAL <-> SITE-LOCAL (+Y UP) ROTATION
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Site-local axes (+X east, +Y up, +Z north). Celestial pole P sits at elevation φ toward north; the equatorial meridian
    //    point M at elevation 90° - φ toward south; hour angle H increases westward (-X).
    struct CelestialRotation
    {
        Vector3f MeridianAxis   {};     // [-] - Local direction of (δ = 0, H = 0)
        Vector3f WestAxis       {};     // [-] - Local direction of (δ = 0, H = +6h)
        Vector3f PoleAxis       {};     // [-] - Local direction of north celestial pole
        float    CosineSidereal = 1.0f; // [-] - cos(local sidereal angle)
        float    SineSidereal   = 0.0f; // [-] - sin(local sidereal angle)
    };

    [[nodiscard]] CelestialRotation ConstructCelestialRotation(const StarfieldConfiguration& StarConfiguration) noexcept
    {
        const float       Latitude = std::clamp(StarConfiguration.SiteLatitudeRadians, -1.5707f, 1.5707f);
        CelestialRotation Rotation {};
        Rotation.MeridianAxis   = Vector3f{ 0.0f, std::cos(Latitude), -std::sin(Latitude) };
        Rotation.WestAxis       = Vector3f{ -1.0f, 0.0f, 0.0f };
        Rotation.PoleAxis       = Vector3f{ 0.0f, std::sin(Latitude), std::cos(Latitude) };
        Rotation.CosineSidereal = std::cos(StarConfiguration.CelestialRotationRadians);
        Rotation.SineSidereal   = std::sin(StarConfiguration.CelestialRotationRadians);
        return Rotation;
    }

    [[nodiscard]] Vector3f TransformLocalToEquatorial(const CelestialRotation& Rotation, const Vector3f& LocalAxis) noexcept
    {
        const float HourCosine = DotVector(LocalAxis, Rotation.MeridianAxis);
        const float HourSine   = DotVector(LocalAxis, Rotation.WestAxis);
        const float PoleSine   = DotVector(LocalAxis, Rotation.PoleAxis);
        return Vector3f{
            Rotation.CosineSidereal * HourCosine + Rotation.SineSidereal * HourSine,
            Rotation.SineSidereal * HourCosine - Rotation.CosineSidereal * HourSine,
            PoleSine };
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                       IAU J2000 GALACTIC COORDINATE BASIS
    //--------------------------------------------------------------------------------------------------------------------

    struct GalacticBasis
    {
        Vector3f CentreAxis{};     // [-] - Equatorial direction of galactic centre (l = 0, b = 0)
        Vector3f QuadratureAxis{}; // [-] - Equatorial direction of (l = 90°, b = 0) — Cygnus
        Vector3f PoleAxis{};       // [-] - Equatorial direction of north galactic pole
    };

    [[nodiscard]] const GalacticBasis& RetrieveGalacticBasis() noexcept
    {
        static const GalacticBasis Basis = []() noexcept
        {
            GalacticBasis Constructed{};
            Constructed.PoleAxis = ConstructEquatorialAxis(192.85948f * Atmosphere::DegreesToRadians, 27.12825f * Atmosphere::DegreesToRadians);
            const Vector3f CentreRaw = ConstructEquatorialAxis(266.40500f * Atmosphere::DegreesToRadians, -28.93617f * Atmosphere::DegreesToRadians);
            Constructed.CentreAxis     = NormalizeVector(SubtractVector(CentreRaw, ScaleVector(Constructed.PoleAxis, DotVector(CentreRaw, Constructed.PoleAxis))));
            Constructed.QuadratureAxis = CrossVector(Constructed.PoleAxis, Constructed.CentreAxis);
            return Constructed;
        }();
        return Basis;
    }

    void ResolveGalacticCoordinates(const Vector3f& EquatorialAxis, float& OutLongitude, float& OutLatitude) noexcept
    {
        const GalacticBasis& Basis = RetrieveGalacticBasis();
        OutLongitude = std::atan2(DotVector(EquatorialAxis, Basis.QuadratureAxis), DotVector(EquatorialAxis, Basis.CentreAxis));
        OutLatitude  = std::asin(std::clamp(DotVector(EquatorialAxis, Basis.PoleAxis), -1.0f, 1.0f));
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                  J2000 BRIGHT STAR CATALOGUE (RA h, Dec °, V mag, B-V)
    //--------------------------------------------------------------------------------------------------------------------

    struct CatalogueStarEntry
    {
        float RightAscensionHours; // [h]   - J2000 right ascension
        float DeclinationDegrees;  // [°]   - J2000 declination
        float VisualMagnitude;     // [mag] - Johnson V apparent magnitude
        float ColourIndexBV;       // [mag] - Johnson B-V colour index
    };

    constexpr CatalogueStarEntry BrightStarCatalogue[] = {
        {  6.752f, -16.716f, -1.46f,  0.00f }, {  6.399f, -52.696f, -0.74f,  0.15f }, { 14.261f,  19.182f, -0.05f,  1.23f },
        { 14.660f, -60.834f, -0.27f,  0.71f }, { 18.616f,  38.784f,  0.03f,  0.00f }, {  5.278f,  45.998f,  0.08f,  0.80f },
        {  5.242f,  -8.202f,  0.13f, -0.03f }, {  7.655f,   5.225f,  0.34f,  0.42f }, {  1.629f, -57.237f,  0.46f, -0.16f },
        {  5.919f,   7.407f,  0.50f,  1.85f }, { 14.064f, -60.373f,  0.61f, -0.23f }, { 19.846f,   8.868f,  0.76f,  0.22f },
        { 12.443f, -63.099f,  0.76f, -0.24f }, {  4.599f,  16.509f,  0.86f,  1.54f }, { 16.490f, -26.432f,  0.96f,  1.83f },
        { 13.420f, -11.161f,  0.97f, -0.23f }, {  7.755f,  28.026f,  1.14f,  1.00f }, { 22.961f, -29.622f,  1.16f,  0.09f },
        { 20.690f,  45.280f,  1.25f,  0.09f }, { 12.795f, -59.689f,  1.25f, -0.23f }, { 10.139f,  11.967f,  1.35f, -0.11f },
        {  6.977f, -28.972f,  1.50f, -0.21f }, {  7.577f,  31.888f,  1.58f,  0.03f }, { 12.519f, -57.113f,  1.63f,  1.59f },
        { 17.560f, -37.104f,  1.62f, -0.22f }, {  5.419f,   6.350f,  1.64f, -0.22f }, {  5.438f,  28.608f,  1.65f, -0.13f },
        {  9.220f, -69.717f,  1.68f,  0.07f }, {  5.604f,  -1.202f,  1.69f, -0.18f }, { 22.137f, -46.961f,  1.74f, -0.13f },
        {  5.679f,  -1.943f,  1.77f, -0.21f }, { 12.900f,  55.960f,  1.77f, -0.02f }, { 11.062f,  61.751f,  1.79f,  1.07f },
        {  3.405f,  49.861f,  1.79f,  0.48f }, {  7.140f, -26.393f,  1.84f,  0.68f }, { 17.622f, -42.998f,  1.86f,  0.40f },
        { 18.403f, -34.385f,  1.85f, -0.03f }, {  8.375f, -59.510f,  1.86f,  1.28f }, { 13.792f,  49.313f,  1.86f, -0.19f },
        {  5.992f,  44.947f,  1.90f,  0.08f }, { 16.811f, -69.028f,  1.92f,  1.44f }, {  6.628f,  16.399f,  1.93f,  0.00f },
        { 20.427f, -56.735f,  1.94f, -0.20f }, {  2.530f,  89.264f,  1.98f,  0.60f }, {  6.378f, -17.956f,  1.98f, -0.23f },
        {  9.460f,  -8.659f,  1.99f,  1.44f }, {  2.120f,  23.463f,  2.00f,  1.15f }, {  0.727f, -17.987f,  2.04f,  1.02f },
        { 18.921f, -26.297f,  2.05f, -0.13f }, { 14.111f, -36.370f,  2.06f,  1.01f }, {  0.140f,  29.091f,  2.06f, -0.11f },
        {  1.162f,  35.621f,  2.05f,  1.58f }, {  5.796f,  -9.670f,  2.07f, -0.17f }, { 14.845f,  74.156f,  2.08f,  1.47f },
        { 17.582f,  12.560f,  2.08f,  0.15f }, {  3.136f,  40.956f,  2.12f, -0.05f }, {  2.065f,  42.330f,  2.10f,  1.37f },
        { 11.818f,  14.572f,  2.14f,  0.09f }, {  8.060f, -40.003f,  2.21f, -0.27f }, {  0.945f,  60.717f,  2.15f, -0.15f },
        {  9.285f, -59.275f,  2.21f,  0.18f }, { 15.578f,  26.715f,  2.23f, -0.02f }, { 13.399f,  54.925f,  2.23f,  0.02f },
        { 20.370f,  40.257f,  2.23f,  0.68f }, {  0.675f,  56.537f,  2.24f,  1.17f }, { 17.943f,  51.489f,  2.24f,  1.52f },
        {  5.533f,  -0.299f,  2.25f, -0.22f }, {  0.153f,  59.150f,  2.28f,  0.34f }, { 16.006f, -22.622f,  2.29f, -0.12f },
        { 16.836f, -34.293f,  2.29f,  1.15f }, { 11.031f,  56.382f,  2.37f, -0.02f }, { 14.750f,  27.074f,  2.37f,  0.97f },
        { 21.736f,   9.875f,  2.39f,  1.53f }, {  0.438f, -42.306f,  2.40f,  1.09f }, { 11.897f,  53.695f,  2.44f,  0.00f },
        { 17.173f, -15.725f,  2.43f,  0.06f }, { 23.063f,  28.083f,  2.42f,  1.67f }, { 21.310f,  62.586f,  2.45f,  0.26f },
        {  7.402f, -29.303f,  2.45f, -0.08f }, { 23.079f,  15.205f,  2.49f, -0.04f }, {  3.038f,   4.090f,  2.54f,  1.64f },
        { 11.235f,  20.524f,  2.56f,  0.12f }, {  5.546f, -17.822f,  2.58f,  0.21f }, { 12.263f, -17.542f,  2.59f, -0.11f },
        { 15.738f,   6.426f,  2.63f,  1.17f }, { 15.283f,  -9.383f,  2.61f, -0.11f }, {  1.911f,  20.808f,  2.64f,  0.13f },
        { 16.504f,  21.490f,  2.78f,  0.94f }, { 17.507f,  52.301f,  2.79f,  0.98f }, { 19.512f,  27.960f,  3.05f,  1.13f },
        { 13.036f,  10.959f,  2.83f,  0.94f }, { 19.771f,  10.613f,  2.72f,  1.52f }, { 10.333f,  19.842f,  2.01f,  1.15f },
        {  3.791f,  24.105f,  2.87f, -0.09f }, {  3.819f,  24.053f,  3.62f, -0.07f }, {  3.747f,  24.113f,  3.70f, -0.11f },
        {  3.763f,  24.368f,  3.87f, -0.07f }, {  3.772f,  23.948f,  4.18f, -0.06f }, {  3.754f,  24.467f,  4.30f, -0.11f },
        {  3.819f,  24.137f,  5.05f, -0.08f }, {  3.748f,  24.289f,  5.45f, -0.04f }, {  5.585f,   9.934f,  3.33f, -0.16f },
        {  5.590f,  -5.910f,  2.77f, -0.24f }, {  5.588f,  -5.390f,  4.00f,  0.02f }, { 16.091f, -19.806f,  2.62f, -0.07f },
        { 16.598f, -28.216f,  2.82f, -0.25f }, { 17.513f, -37.296f,  2.70f, -0.22f }, { 17.708f, -39.030f,  2.39f, -0.22f },
        { 18.350f, -29.828f,  2.70f,  1.38f }, { 18.466f, -25.422f,  2.81f,  1.04f }, { 19.044f, -29.880f,  2.60f,  0.08f },
        { 19.163f, -21.024f,  2.89f,  0.35f }, { 18.097f, -30.424f,  2.98f,  1.00f }, { 20.770f,  33.970f,  2.48f,  1.03f },
        { 19.750f,  45.131f,  2.87f, -0.03f }, { 18.835f,  33.363f,  3.52f,  0.00f }, { 18.982f,  32.690f,  3.25f, -0.05f },
        {  1.430f,  60.235f,  2.68f,  0.13f }, {  1.907f,  63.670f,  3.37f, -0.15f }, { 12.257f,  57.033f,  3.31f,  0.08f },
        { 12.252f, -58.749f,  2.79f, -0.23f }, { 13.665f, -53.466f,  2.30f, -0.22f }, { 14.592f, -42.158f,  2.31f, -0.19f },
        { 13.926f, -47.288f,  2.55f, -0.18f }, { 10.716f, -64.394f,  2.76f, -0.22f }, {  9.133f, -43.433f,  2.21f,  1.66f },
        {  8.745f, -54.709f,  1.96f,  0.04f }, {  9.368f, -55.011f,  2.47f, -0.18f }, {  8.159f, -47.337f,  1.83f, -0.22f },
        {  6.732f,  25.131f,  2.98f,  1.38f }, {  6.383f,  22.514f,  2.87f,  1.64f }, {  5.995f,  37.213f,  2.62f, -0.08f },
        { 19.922f,   6.407f,  3.71f,  0.86f }, { 13.911f,  18.398f,  2.68f,  0.58f }, { 16.619f, -10.567f,  2.56f,  0.02f },
        {  3.967f, -13.509f,  2.95f,  1.59f }, {  5.131f,  -5.086f,  2.78f,  0.13f }, {  5.471f, -20.759f,  2.84f,  0.82f },
        {  5.661f, -34.074f,  2.65f, -0.12f }, { 22.711f, -46.885f,  2.07f,  1.60f }, { 17.422f, -55.530f,  2.84f,  1.46f },
        {  5.627f,  21.143f,  3.00f, -0.19f }, {  6.338f, -30.063f,  3.02f, -0.19f }, { 14.699f, -47.388f,  2.30f, -0.20f },
        { 14.073f,  64.376f,  3.65f, -0.05f }, { 15.345f,  71.834f,  3.00f,  0.05f }, { 21.526f,  -5.571f,  2.87f,  0.83f },
        { 22.096f,  -0.320f,  2.95f,  0.98f }, { 21.784f, -16.127f,  2.85f,  0.29f }, { 14.848f, -16.042f,  2.75f,  0.15f },
        { 12.694f,  -1.449f,  2.74f,  0.36f }, { 12.573f, -23.397f,  2.65f,  0.89f }, { 16.688f,  31.603f,  2.81f,  0.65f },
        {  9.764f,  23.774f,  2.98f,  0.81f }, {  0.429f, -77.254f,  2.80f,  0.62f }, { 22.308f, -60.260f,  2.86f,  1.39f },
    };

    //--------------------------------------------------------------------------------------------------------------------
    //                                   CUBEMAP-INDEXED STELLAR FIELD (CATALOGUE + DEEP FIELD)
    //--------------------------------------------------------------------------------------------------------------------

    struct ResolvedStarEntry
    {
        Vector3f     EquatorialAxis    {};     // [-]   - Unit J2000 equatorial direction
        LinearColour Chromaticity      {};     // [-]   - Unit-luminance Planckian Rec.709 tristimulus
        float        IntegratedFlux    = 0.0f; // [-]   - Pogson flux 10^(-0.4 V) * StellarFluxReference
        float        ScintillationSeed = 0.0f; // [-] - Per-star turbulence phase seed in [0, 1)
        float        HaloReachRadians  = 0.0f; // [rad] - Angular extent of the Moffat halo above the noise floor
    };

    struct StellarCubeIndex
    {
        std::vector<ResolvedStarEntry> Stars;               // [-] - Catalogue stars followed by deep-field stars
        std::vector<uint32_t>          CellOffsets;         // [-] - Prefix offsets (6 * N * N + 1)
        std::vector<uint32_t>          CellStarIndices;     // [-] - Flattened per-cell star index lists (margin-replicated)
        uint32_t                       CatalogueCount = 0u; // [-] - Number of J2000 catalogue stars at the front of Stars
    };

    [[nodiscard]] uint32_t ResolveCubeCell(const Vector3f& Axis) noexcept
    {
        const float AbsoluteX = std::abs(Axis.XCoord);
        const float AbsoluteY = std::abs(Axis.YCoord);
        const float AbsoluteZ = std::abs(Axis.ZCoord);
        uint32_t    Face      = 0u;
        float       CoordU    = 0.0f;
        float       CoordV    = 0.0f;
        if (AbsoluteX >= AbsoluteY && AbsoluteX >= AbsoluteZ)
        {
            Face   = (Axis.XCoord >= 0.0f) ? 0u : 1u;
            CoordU = Axis.YCoord / AbsoluteX;
            CoordV = Axis.ZCoord / AbsoluteX;
        }
        else if (AbsoluteY >= AbsoluteZ)
        {
            Face   = (Axis.YCoord >= 0.0f) ? 2u : 3u;
            CoordU = Axis.XCoord / AbsoluteY;
            CoordV = Axis.ZCoord / AbsoluteY;
        }
        else
        {
            Face   = (Axis.ZCoord >= 0.0f) ? 4u : 5u;
            CoordU = Axis.XCoord / AbsoluteZ;
            CoordV = Axis.YCoord / AbsoluteZ;
        }
        const uint32_t CellU = std::min(CubeCellsPerAxis - 1u, static_cast<uint32_t>(std::max(0.0f, (CoordU + 1.0f) * 0.5f * CubeCellsPerAxis)));
        const uint32_t CellV = std::min(CubeCellsPerAxis - 1u, static_cast<uint32_t>(std::max(0.0f, (CoordV + 1.0f) * 0.5f * CubeCellsPerAxis)));
        return (Face * CubeCellsPerAxis + CellV) * CubeCellsPerAxis + CellU;
    }

    [[nodiscard]] float EvaluateMilkyWayStarDensity(float GalacticLongitude, float GalacticLatitude) noexcept
    {
        const float AbsoluteLatitude = std::abs(GalacticLatitude);
        const float Thickness        = 0.10f + 0.10f * std::exp(-GalacticLongitude * GalacticLongitude / 0.6f);
        const float DiskProfile      = std::exp(-AbsoluteLatitude / Thickness);
        const float Bulge            = std::exp(-(GalacticLongitude * GalacticLongitude) / 0.18f - (GalacticLatitude * GalacticLatitude) / 0.04f);
        return std::clamp(0.16f + 0.84f * DiskProfile * (0.55f + 0.45f * std::cos(GalacticLongitude * 0.5f)) + 0.6f * Bulge, 0.0f, 1.6f);
    }

    [[nodiscard]] const StellarCubeIndex& RetrieveStellarCubeIndex()
    {
        static const StellarCubeIndex Index = []()
        {
            StellarCubeIndex      Constructed     {};
            constexpr std::size_t CatalogueLength = sizeof(BrightStarCatalogue) / sizeof(BrightStarCatalogue[0]);
            Constructed.Stars.reserve(CatalogueLength + DeepFieldStarCount);

            const auto AppendStar = [&](const Vector3f& Axis, float Magnitude, float ColourIndex, uint32_t SeedWord)
            {
                ResolvedStarEntry Entry{};
                Entry.EquatorialAxis    = NormalizeVector(Axis);
                Entry.Chromaticity      = ConvertColourIndexToLinearRec709(ColourIndex);
                Entry.IntegratedFlux    = StellarFluxReference * std::pow(10.0f, -0.4f * Magnitude);
                Entry.ScintillationSeed = HashUnitInterval(SeedWord * 0x9E3779B9u + 17u);
                // Halo reach: angle at which the Moffat wing drops below ~1e-6 linear radiance (bright stars bloom wider).
                Entry.HaloReachRadians  = std::clamp(0.0035f * std::pow(10.0f, -0.18f * (Magnitude - 1.0f)), 0.0018f, 0.030f);
                Constructed.Stars.push_back(Entry);
            };

            for (std::size_t Ordinal = 0u; Ordinal < CatalogueLength; ++Ordinal)
            {
                const CatalogueStarEntry& Source = BrightStarCatalogue[Ordinal];
                AppendStar(
                    ConstructEquatorialAxis(Source.RightAscensionHours * (Atmosphere::TauConstant / 24.0f), Source.DeclinationDegrees * Atmosphere::DegreesToRadians),
                    Source.VisualMagnitude,
                    Source.ColourIndexBV,
                    static_cast<uint32_t>(Ordinal));
            }
            Constructed.CatalogueCount = static_cast<uint32_t>(CatalogueLength);

            // Rejection-sample deep-field stars concentrated toward the galactic plane + bulge (Pogson N(<m) ∝ 10^(0.5 m)).
            const GalacticBasis& Basis   = RetrieveGalacticBasis();
            uint32_t             Attempt = 0u;
            while (Constructed.Stars.size() < CatalogueLength + DeepFieldStarCount && Attempt < 400000u)
            {
                ++Attempt;
                const float    Vertical  = 2.0f * HashUnitInterval(Attempt * 3u + 1u) - 1.0f;
                const float    Azimuth   = Atmosphere::TauConstant * HashUnitInterval(Attempt * 3u + 2u);
                const float    Radial    = SafeSquareRoot(1.0f - Vertical * Vertical);
                const Vector3f Galactic  { Radial * std::cos(Azimuth), Radial * std::sin(Azimuth), Vertical };
                const float    Longitude = std::atan2(Galactic.YCoord, Galactic.XCoord);
                const float    Latitude  = std::asin(Vertical);
                if (HashUnitInterval(Attempt * 3u + 3u) * 1.6f > EvaluateMilkyWayStarDensity(Longitude, Latitude))
                    continue;

                const Vector3f Equatorial = AddVector(
                    AddVector(ScaleVector(Basis.CentreAxis, Galactic.XCoord), ScaleVector(Basis.QuadratureAxis, Galactic.YCoord)),
                    ScaleVector(Basis.PoleAxis, Galactic.ZCoord));

                const float MagnitudeUniform = HashUnitInterval(Attempt * 7u + 5u);
                const float Magnitude        = 4.6f + std::log10(1.0f + MagnitudeUniform * (std::pow(10.0f, 0.5f * 5.0f) - 1.0f)) / 0.5f;
                const float ColourUniform    = HashUnitInterval(Attempt * 11u + 7u);
                const float ColourIndex      = -0.28f + 2.05f * std::pow(ColourUniform, 1.15f);
                AppendStar(Equatorial, Magnitude, ColourIndex, Attempt + 100000u);
            }

            // Margin-replicated cube binning: each star lands in every cell its PSF/halo reach can touch, so a query reads one cell only.
            const uint32_t CellCount = 6u * CubeCellsPerAxis * CubeCellsPerAxis;
            std::vector<std::vector<uint32_t>> CellBuckets(CellCount);
            for (uint32_t StarIndex = 0u; StarIndex < static_cast<uint32_t>(Constructed.Stars.size()); ++StarIndex)
            {
                const ResolvedStarEntry& Entry         = Constructed.Stars[StarIndex];
                const Vector3f           ReferenceAxis = (std::abs(Entry.EquatorialAxis.ZCoord) < 0.9f) ? Vector3f{ 0.0f, 0.0f, 1.0f } : Vector3f{ 1.0f, 0.0f, 0.0f };
                const Vector3f           TangentU      = NormalizeVector(CrossVector(ReferenceAxis, Entry.EquatorialAxis));
                const Vector3f           TangentV      = CrossVector(Entry.EquatorialAxis, TangentU);
                const float              Reach         = Entry.HaloReachRadians + 0.004f;

                std::array<uint32_t, 9> Touched      {};
                uint32_t                TouchedCount = 0u;
                for (int32_t OffsetV = -1; OffsetV <= 1; ++OffsetV)
                {
                    for (int32_t OffsetU = -1; OffsetU <= 1; ++OffsetU)
                    {
                        const Vector3f Probe = NormalizeVector(AddVector(Entry.EquatorialAxis,
                            AddVector(ScaleVector(TangentU, Reach * static_cast<float>(OffsetU)), ScaleVector(TangentV, Reach * static_cast<float>(OffsetV)))));
                        const uint32_t Cell  = ResolveCubeCell(Probe);
                        if (std::find(Touched.begin(), Touched.begin() + TouchedCount, Cell) == Touched.begin() + TouchedCount)
                            Touched[TouchedCount++] = Cell;
                    }
                }
                for (uint32_t Slot = 0u; Slot < TouchedCount; ++Slot)
                    CellBuckets[Touched[Slot]].push_back(StarIndex);
            }

            Constructed.CellOffsets.assign(CellCount + 1u, 0u);
            for (uint32_t Cell = 0u; Cell < CellCount; ++Cell)
                Constructed.CellOffsets[Cell + 1u] = Constructed.CellOffsets[Cell] + static_cast<uint32_t>(CellBuckets[Cell].size());
            Constructed.CellStarIndices.reserve(Constructed.CellOffsets[CellCount]);
            for (uint32_t Cell = 0u; Cell < CellCount; ++Cell)
                Constructed.CellStarIndices.insert(Constructed.CellStarIndices.end(), CellBuckets[Cell].begin(), CellBuckets[Cell].end());
            return Constructed;
        }();
        return Index;
    }

    // 📝 Diffraction-limited optical PSF normalised over solid angle: Gaussian Airy central lobe (82 %) + Moffat β = 2.5 scatter wing (18 %).
    [[nodiscard]] float EvaluateStellarPointSpread(float AngleRadians, float PixelAngleRadians) noexcept
    {
        const float LobeSigma    = 0.62f * PixelAngleRadians;
        const float LobeVariance = LobeSigma * LobeSigma;
        const float LobeTerm     = std::exp(-0.5f * AngleRadians * AngleRadians / LobeVariance) / (Atmosphere::TauConstant * LobeVariance);

        constexpr float MoffatBeta  = 2.5f;
        const float     MoffatAlpha = 2.4f * PixelAngleRadians;
        const float     MoffatRatio = (AngleRadians * AngleRadians) / (MoffatAlpha * MoffatAlpha);
        const float     MoffatTerm  = (MoffatBeta - 1.0f) / (Atmosphere::PiConstant * MoffatAlpha * MoffatAlpha) * std::pow(1.0f + MoffatRatio, -MoffatBeta);
        return 0.82f * LobeTerm + 0.18f * MoffatTerm;
    }

    // 📝 Kasten & Young (1989) relative optical airmass for apparent elevation h [rad].
    [[nodiscard]] float EvaluateKastenYoungAirmass(float ElevationRadians) noexcept
    {
        const float ElevationDegrees = std::max(-0.5f, ElevationRadians * Atmosphere::RadiansToDegrees);
        return 1.0f / (std::sin(std::max(0.0f, ElevationRadians)) + 0.50572f * std::pow(ElevationDegrees + 6.07995f, -1.6364f));
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                          MILKY WAY & DEEP-SKY NEBULAE
    //--------------------------------------------------------------------------------------------------------------------

    [[nodiscard]] LinearColour EvaluateMilkyWaySurfaceBrightness(const Vector3f& EquatorialAxis) noexcept
    {
        float Longitude = 0.0f;
        float Latitude  = 0.0f;
        ResolveGalacticCoordinates(EquatorialAxis, Longitude, Latitude);
        const GalacticBasis& Basis        = RetrieveGalacticBasis();
        const Vector3f       GalacticAxis {
            DotVector(EquatorialAxis, Basis.CentreAxis),
            DotVector(EquatorialAxis, Basis.QuadratureAxis),
            DotVector(EquatorialAxis, Basis.PoleAxis) };

        const auto Lobe = [](float Offset, float Width) noexcept { return std::exp(-(Offset * Offset) / (Width * Width)); };

        // ① Longitudinal disc luminosity: galactic-centre peak + Scutum, Cygnus, Carina, and Perseus-arm star clouds.
        const float LongitudeProfile = 0.20f
                                     + 0.80f * Lobe(Longitude, 0.95f)
                                     + 0.32f * Lobe(Longitude - 0.47f, 0.22f)
                                     + 0.36f * Lobe(Longitude - 1.38f, 0.38f)
                                     + 0.34f * Lobe(Longitude + 1.30f, 0.34f)
                                     + 0.12f * Lobe(std::abs(Longitude) - 2.6f, 0.6f);

        // ② Vertical sech² disc profile thickening toward the bulge.
        const float ScaleHeight = 0.052f + 0.040f * Lobe(Longitude, 0.6f);
        const float CoshTerm    = std::cosh(std::clamp(Latitude / ScaleHeight, -20.0f, 20.0f));
        const float DiscProfile = 1.0f / (CoshTerm * CoshTerm);
        const float ThickDisc   = 0.35f * std::exp(-std::abs(Latitude) / 0.22f);

        // ③ Boxy-peanut bulge (Gaussian ellipsoid, 12° x 8°).
        const float Bulge = 1.45f * std::exp(-(Longitude * Longitude) / (2.0f * 0.19f * 0.19f) - (Latitude * Latitude) / (2.0f * 0.13f * 0.13f));

        // ④ Clumpy unresolved star clouds (domain-rotated 3D fBm on the galactic sphere).
        const float CloudNoise = EvaluateRotatedFbm3D(ScaleVector(GalacticAxis, 9.0f), 5u, 1201u);
        const float FineNoise  = EvaluateRotatedFbm3D(ScaleVector(GalacticAxis, 31.0f), 4u, 1307u);
        const float Clumping   = 0.50f + 1.05f * CloudNoise + 0.35f * (FineNoise - 0.5f);

        const float DiscLuminance  = LongitudeProfile * (DiscProfile + ThickDisc) * Clumping;
        const float BulgeLuminance = Bulge * (0.75f + 0.5f * CloudNoise);

        // ⑤ Great Rift + Coalsack dark dust lanes with interstellar reddening (A_B : A_V : A_R ≈ 1.32 : 1.00 : 0.75).
        const float RiftCentre    = 0.012f + 0.022f * std::sin(2.4f * Longitude + 0.6f);
        const float RiftMask      = EvaluateSmoothStep(-0.75f, -0.35f, Longitude) * (1.0f - EvaluateSmoothStep(1.35f, 1.65f, Longitude));
        const float RiftNoise     = EvaluateRotatedFbm3D(ScaleVector(GalacticAxis, 14.0f), 5u, 1409u);
        const float RiftWidth     = 0.032f + 0.018f * RiftNoise;
        const float RiftDepth     = 2.2f * RiftMask * Lobe(Latitude - RiftCentre, RiftWidth) * (0.80f + 0.40f * RiftNoise);
        const float CoalsackDepth = 1.6f * Lobe(Longitude + 0.9f, 0.05f) * Lobe(Latitude + 0.015f, 0.05f);

        // Filamentary dust: domain-warped ridged fBm, compressed in galactic latitude so lanes stream along the plane (molecular
        // cloud filaments), gated by a low-frequency complex mask so lanes cluster into dark-cloud complexes rather than a uniform web.
        const Vector3f StretchedAxis { GalacticAxis.XCoord * 7.0f, GalacticAxis.YCoord * 7.0f, GalacticAxis.ZCoord * 26.0f };
        const Vector3f DustWarp      {
            EvaluateRotatedFbm3D(ScaleVector(StretchedAxis, 0.8f), 3u, 1601u) - 0.5f,
            EvaluateRotatedFbm3D(ScaleVector(StretchedAxis, 0.8f), 3u, 1607u) - 0.5f,
            EvaluateRotatedFbm3D(ScaleVector(StretchedAxis, 0.8f), 3u, 1613u) - 0.5f };
        const Vector3f WarpedAxis    = AddVector(StretchedAxis, ScaleVector(DustWarp, 1.6f));
        const float    RidgeCoarse   = 1.0f - std::abs(2.0f * EvaluateRotatedFbm3D(WarpedAxis, 4u, 1511u) - 1.0f);
        const float    RidgeFine     = 1.0f - std::abs(2.0f * EvaluateRotatedFbm3D(ScaleVector(WarpedAxis, 2.7f), 3u, 1523u) - 1.0f);
        const float    Filaments     = EvaluateSmoothStep(0.62f, 0.95f, 0.70f * RidgeCoarse + 0.30f * RidgeFine);
        const float    ComplexMask   = EvaluateSmoothStep(0.42f, 0.66f, EvaluateRotatedFbm3D(ScaleVector(GalacticAxis, 4.0f), 3u, 1531u));
        const float    DarkNebulae   = 1.15f * Filaments * ComplexMask * Lobe(Latitude, 0.11f);
        const float    DustDepth     = RiftDepth + CoalsackDepth + DarkNebulae;

        const LinearColour DiscTint { 0.78f, 0.86f, 1.00f };
        const LinearColour BulgeTint{ 1.00f, 0.80f, 0.58f };
        const LinearColour Emitted = AddColour(ScaleColour(DiscTint, DiscLuminance), ScaleColour(BulgeTint, BulgeLuminance));
        return LinearColour{
            Emitted.Red   * std::exp(-DustDepth * 0.75f),
            Emitted.Green * std::exp(-DustDepth),
            Emitted.Blue  * std::exp(-DustDepth * 1.32f) };
    }

    // 📝 Local gnomonic tangent offsets (east, north) [rad] of EquatorialAxis relative to TargetAxis.
    void ResolveTangentOffsets(const Vector3f& EquatorialAxis, const Vector3f& TargetAxis, float& OutEast, float& OutNorth) noexcept
    {
        const Vector3f EastAxis  = NormalizeVector(CrossVector(Vector3f{ 0.0f, 0.0f, 1.0f }, TargetAxis));
        const Vector3f NorthAxis = CrossVector(TargetAxis, EastAxis);
        const float    Forward   = std::max(1.0e-4f, DotVector(EquatorialAxis, TargetAxis));
        OutEast  = DotVector(EquatorialAxis, EastAxis) / Forward;
        OutNorth = DotVector(EquatorialAxis, NorthAxis) / Forward;
    }

    [[nodiscard]] LinearColour EvaluateDeepSkyNebulae(const Vector3f& EquatorialAxis) noexcept
    {
        static const Vector3f GreatNebulaAxis  = ConstructEquatorialAxis(5.588f * (Atmosphere::TauConstant / 24.0f), -5.39f * Atmosphere::DegreesToRadians);
        static const Vector3f PleiadesAxis     = ConstructEquatorialAxis(3.772f * (Atmosphere::TauConstant / 24.0f), 24.10f * Atmosphere::DegreesToRadians);
        static const Vector3f AndromedaAxis    = ConstructEquatorialAxis(0.712f * (Atmosphere::TauConstant / 24.0f), 41.27f * Atmosphere::DegreesToRadians);
        static const Vector3f LargeCloudAxis   = ConstructEquatorialAxis(5.392f * (Atmosphere::TauConstant / 24.0f), -69.76f * Atmosphere::DegreesToRadians);
        static const Vector3f SmallCloudAxis   = ConstructEquatorialAxis(0.877f * (Atmosphere::TauConstant / 24.0f), -72.83f * Atmosphere::DegreesToRadians);
        static const Vector3f LagoonNebulaAxis = ConstructEquatorialAxis(18.06f * (Atmosphere::TauConstant / 24.0f), -24.38f * Atmosphere::DegreesToRadians);
        static const Vector3f CarinaNebulaAxis = ConstructEquatorialAxis(10.75f * (Atmosphere::TauConstant / 24.0f), -59.87f * Atmosphere::DegreesToRadians);

        LinearColour Accumulated {};
        float        East        = 0.0f;
        float        North       = 0.0f;
        const float  Degree      = Atmosphere::DegreesToRadians;

        // M42 — H-α / [S II] crimson emission envelope + [O III] teal inner Trapezium cavity.
        if (DotVector(EquatorialAxis, GreatNebulaAxis) > 0.995f)
        {
            ResolveTangentOffsets(EquatorialAxis, GreatNebulaAxis, East, North);
            const float CentreDistanceSquared = (East * East + North * North) / (Degree * Degree);
            const float Filaments             = EvaluateRotatedFbm3D(ScaleVector(EquatorialAxis, 900.0f), 4u, 1601u);
            const float NebulaGlow            = (0.030f * std::exp(-CentreDistanceSquared / 0.10f) + 0.010f * std::exp(-CentreDistanceSquared / 0.55f) * (0.4f + 1.2f * Filaments));
            const float Cavity                = 0.016f * std::exp(-CentreDistanceSquared / 0.012f);
            Accumulated = AddColour(Accumulated, AddColour(ScaleColour(LinearColour{ 1.00f, 0.30f, 0.46f }, NebulaGlow),
                                                           ScaleColour(LinearColour{ 0.42f, 0.92f, 0.86f }, Cavity)));
        }

        // M45 — blue reflection nebulosity scattering B-type starlight (Merope wisps).
        if (DotVector(EquatorialAxis, PleiadesAxis) > 0.997f)
        {
            ResolveTangentOffsets(EquatorialAxis, PleiadesAxis, East, North);
            const float CentreDistanceSquared = (East * East + North * North) / (Degree * Degree);
            const float Wisps                 = EvaluateRotatedFbm3D(Vector3f{ EquatorialAxis.XCoord * 1400.0f, EquatorialAxis.YCoord * 380.0f, EquatorialAxis.ZCoord * 900.0f }, 4u, 1703u);
            Accumulated = AddColour(Accumulated, ScaleColour(LinearColour{ 0.42f, 0.62f, 1.00f }, 0.0045f * std::exp(-CentreDistanceSquared / 0.30f) * (0.3f + 1.4f * Wisps)));
        }

        // M31 — inclined spiral: warm stellar bulge + exponential disc (3.2° x 1.0°, PA 38°).
        if (DotVector(EquatorialAxis, AndromedaAxis) > 0.997f)
        {
            ResolveTangentOffsets(EquatorialAxis, AndromedaAxis, East, North);
            const float PositionAngle = 38.0f * Degree;
            const float MajorOffset   = (North * std::cos(PositionAngle) + East * std::sin(PositionAngle)) / Degree;
            const float MinorOffset   = (East * std::cos(PositionAngle) - North * std::sin(PositionAngle)) / Degree;
            const float Elliptic      = std::sqrt((MajorOffset / 1.6f) * (MajorOffset / 1.6f) + (MinorOffset / 0.42f) * (MinorOffset / 0.42f));
            const float BulgeGlow     = 0.020f * std::exp(-Elliptic * Elliptic * 22.0f);
            const float DiscGlow      = 0.0042f * std::exp(-Elliptic * 3.2f);
            Accumulated = AddColour(Accumulated, AddColour(ScaleColour(LinearColour{ 1.00f, 0.86f, 0.70f }, BulgeGlow),
                                                           ScaleColour(LinearColour{ 0.86f, 0.88f, 1.00f }, DiscGlow)));
        }

        // Magellanic Clouds — irregular satellite galaxies (southern sky).
        const auto Satellite = [&](const Vector3f& TargetAxis, float RadiusDegrees, float Brightness, uint32_t Salt)
        {
            if (DotVector(EquatorialAxis, TargetAxis) < 0.97f)
                return;
            ResolveTangentOffsets(EquatorialAxis, TargetAxis, East, North);
            const float CentreDistanceSquared = (East * East + North * North) / (Degree * Degree * RadiusDegrees * RadiusDegrees);
            const float Clumps                = EvaluateRotatedFbm3D(ScaleVector(EquatorialAxis, 160.0f), 5u, Salt);
            Accumulated = AddColour(Accumulated, ScaleColour(LinearColour{ 0.86f, 0.90f, 1.00f }, Brightness * std::exp(-CentreDistanceSquared) * (0.35f + 1.3f * Clumps)));
        };
        Satellite(LargeCloudAxis, 3.4f, 0.0060f, 1801u);
        Satellite(SmallCloudAxis, 1.8f, 0.0035f, 1907u);

        // M8 Lagoon + NGC 3372 Carina — bright galactic H-II regions.
        const auto AppendNebulaKnot = [&](const Vector3f& TargetAxis, float RadiusDegrees, float Brightness, uint32_t Salt)
        {
            if (DotVector(EquatorialAxis, TargetAxis) < 0.996f)
                return;
            ResolveTangentOffsets(EquatorialAxis, TargetAxis, East, North);
            const float CentreDistanceSquared = (East * East + North * North) / (Degree * Degree * RadiusDegrees * RadiusDegrees);
            const float Knots                 = EvaluateRotatedFbm3D(ScaleVector(EquatorialAxis, 700.0f), 4u, Salt);
            Accumulated = AddColour(Accumulated, ScaleColour(LinearColour{ 1.00f, 0.36f, 0.50f }, Brightness * std::exp(-CentreDistanceSquared) * (0.3f + 1.4f * Knots)));
        };
        AppendNebulaKnot(LagoonNebulaAxis, 0.45f, 0.010f, 2003u);
        AppendNebulaKnot(CarinaNebulaAxis, 0.80f, 0.012f, 2111u);

        return Accumulated;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                           LUNAR SURFACE MAP DECODING
    //--------------------------------------------------------------------------------------------------------------------

    constexpr const char* LunarMapFileNames[MaximumLunarBodyCount] = {
        "Luna2K.jpg", "Ember2K.jpg", "Glacier2K.jpg", "Sulfur2K.jpg", "Shroud2K.jpg", "Shard2K.jpg" };

    // 📝 Normalise a decoded map so its mean Rec.709 luminance equals 1 (relative reflectance; absolute scale = GeometricAlbedo).
    void NormalizeSurfaceMapLuminance(DecodedLunarSurfaceMap& Map) noexcept
    {
        const std::size_t TexelCount = static_cast<std::size_t>(Map.Width) * Map.Height;
        if (TexelCount == 0u)
            return;
        double LuminanceSum = 0.0;
        for (std::size_t Texel = 0u; Texel < TexelCount; ++Texel)
        {
            LuminanceSum += 0.2126 * Map.LinearRgb[Texel * 3u] + 0.7152 * Map.LinearRgb[Texel * 3u + 1u] + 0.0722 * Map.LinearRgb[Texel * 3u + 2u];
        }
        const float InverseMean = static_cast<float>(static_cast<double>(TexelCount) / std::max(1.0e-9, LuminanceSum));
        for (float& Channel : Map.LinearRgb)
            Channel *= InverseMean;
    }

    // 📝 Deterministic procedural regolith map (crater field + mare basins + preset tint) used when an asset cannot be decoded.
    void SynthesizeProceduralSurfaceMap(LunarPresetIdentifier Preset, DecodedLunarSurfaceMap& OutMap)
    {
        constexpr uint32_t Width  = 512u;
        constexpr uint32_t Height = 256u;
        OutMap.Width  = Width;
        OutMap.Height = Height;
        OutMap.LinearRgb.assign(static_cast<std::size_t>(Width) * Height * 3u, 0.0f);
        OutMap.LoadedFromAsset = false;

        static constexpr LinearColour PresetTints[MaximumLunarBodyCount] = {
            { 0.60f, 0.58f, 0.55f }, { 0.62f, 0.30f, 0.16f }, { 0.78f, 0.80f, 0.82f },
            { 0.80f, 0.70f, 0.30f }, { 0.62f, 0.46f, 0.24f }, { 0.64f, 0.60f, 0.62f } };
        const uint32_t                PresetIndex                        = static_cast<uint32_t>(Preset);
        const LinearColour            Tint                               = PresetTints[std::min(PresetIndex, MaximumLunarBodyCount - 1u)];

        for (uint32_t Row = 0u; Row < Height; ++Row)
        {
            const float Latitude = (0.5f - (static_cast<float>(Row) + 0.5f) / static_cast<float>(Height)) * Atmosphere::PiConstant;
            for (uint32_t Column = 0u; Column < Width; ++Column)
            {
                const float       Longitude   = ((static_cast<float>(Column) + 0.5f) / static_cast<float>(Width) - 0.5f) * Atmosphere::TauConstant;
                const Vector3f    Sphere      { std::cos(Latitude) * std::sin(Longitude), std::sin(Latitude), std::cos(Latitude) * std::cos(Longitude) };
                const float       Basins      = EvaluateRotatedFbm3D(ScaleVector(Sphere, 2.2f), 4u, 2201u + PresetIndex * 13u);
                const float       Detail      = EvaluateRotatedFbm3D(ScaleVector(Sphere, 18.0f), 5u, 2309u + PresetIndex * 13u);
                const float       Craters     = EvaluateRotatedFbm3D(ScaleVector(Sphere, 46.0f), 3u, 2411u + PresetIndex * 13u);
                const float       Mare        = EvaluateSmoothStep(0.50f, 0.62f, Basins);
                const float       Rims        = EvaluateSmoothStep(0.60f, 0.72f, Craters) - 0.6f * EvaluateSmoothStep(0.72f, 0.80f, Craters);
                const float       Relative    = std::clamp(1.0f - 0.45f * Mare + 0.35f * (Detail - 0.5f) + 0.25f * Rims, 0.15f, 2.0f);
                const std::size_t TexelOffset = (static_cast<std::size_t>(Row) * Width + Column) * 3u;
                OutMap.LinearRgb[TexelOffset + 0u] = Tint.Red   * Relative;
                OutMap.LinearRgb[TexelOffset + 1u] = Tint.Green * Relative;
                OutMap.LinearRgb[TexelOffset + 2u] = Tint.Blue  * Relative;
            }
        }
        NormalizeSurfaceMapLuminance(OutMap);
    }

    [[nodiscard]] bool DecodeSurfaceMapAsset(const std::string& FilePath, DecodedLunarSurfaceMap& OutMap)
    {
#if defined(FRONTIER_CELESTIAL_JPEG_DECODER_ENABLED)
        int      DecodedWidth    = 0;
        int      DecodedHeight   = 0;
        int      DecodedChannels = 0;
        stbi_uc* Pixels          = stbi_load(FilePath.c_str(), &DecodedWidth, &DecodedHeight, &DecodedChannels, 3);
        if (Pixels == nullptr || DecodedWidth < 16 || DecodedHeight < 8)
        {
            if (Pixels != nullptr)
                stbi_image_free(Pixels);
            return false;
        }

        // Precomputed IEC 61966-2-1 EOTF table (exact sRGB byte -> linear reflectance).
        std::array<float, 256> DecodeTable{};
        for (uint32_t Code = 0u; Code < 256u; ++Code)
            DecodeTable[Code] = DecodeSrgbByteToLinear(static_cast<uint8_t>(Code));

        OutMap.Width  = static_cast<uint32_t>(DecodedWidth);
        OutMap.Height = static_cast<uint32_t>(DecodedHeight);
        const std::size_t ChannelCount = static_cast<std::size_t>(OutMap.Width) * OutMap.Height * 3u;
        OutMap.LinearRgb.resize(ChannelCount);
        for (std::size_t Channel = 0u; Channel < ChannelCount; ++Channel)
            OutMap.LinearRgb[Channel] = DecodeTable[Pixels[Channel]];
        stbi_image_free(Pixels);

        OutMap.LoadedFromAsset = true;
        NormalizeSurfaceMapLuminance(OutMap);
        return true;
#else
        (void)FilePath;
        (void)OutMap;
        return false;
#endif
    }

    // 📝 Bilinear equirectangular lookup (longitude wrap, latitude clamp) at (CoordU, CoordV) in [0, 1]^2.
    [[nodiscard]] LinearColour SampleSurfaceMap(const DecodedLunarSurfaceMap& Map, float CoordU, float CoordV) noexcept
    {
        const float TexelX = (CoordU - std::floor(CoordU)) * static_cast<float>(Map.Width) - 0.5f;
        const float TexelY = std::clamp(CoordV, 0.0f, 1.0f) * static_cast<float>(Map.Height) - 0.5f;
        const float FloorX = std::floor(TexelX);
        const float FloorY = std::floor(TexelY);
        const float BlendX = TexelX - FloorX;
        const float BlendY = std::clamp(TexelY - FloorY, 0.0f, 1.0f);

        const int32_t SignedWidth = static_cast<int32_t>(Map.Width);
        const int32_t ColumnA     = ((static_cast<int32_t>(FloorX) % SignedWidth) + SignedWidth) % SignedWidth;
        const int32_t ColumnB     = (ColumnA + 1) % SignedWidth;
        const int32_t RowA        = std::clamp(static_cast<int32_t>(FloorY), 0, static_cast<int32_t>(Map.Height) - 1);
        const int32_t RowB        = std::min(RowA + 1, static_cast<int32_t>(Map.Height) - 1);

        const auto ReadTexel = [&](int32_t Column, int32_t Row) noexcept
        {
            const std::size_t TexelOffset = (static_cast<std::size_t>(Row) * Map.Width + static_cast<std::size_t>(Column)) * 3u;
            return LinearColour{ Map.LinearRgb[TexelOffset], Map.LinearRgb[TexelOffset + 1u], Map.LinearRgb[TexelOffset + 2u] };
        };
        return InterpolateColour(
            InterpolateColour(ReadTexel(ColumnA, RowA), ReadTexel(ColumnB, RowA), BlendX),
            InterpolateColour(ReadTexel(ColumnA, RowB), ReadTexel(ColumnB, RowB), BlendX),
            BlendY);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                     3D SPHERICAL LUNAR ATMOSPHERE INTEGRATION
    //--------------------------------------------------------------------------------------------------------------------

    struct LunarMediumSample
    {
        LinearColour Scattering{};   // [1/km] - Total scattering σ_s (Rayleigh + Mie + detached haze)
        LinearColour Extinction{};   // [1/km] - Total extinction σ_t
        LinearColour RayleighPart{}; // [1/km] - Rayleigh scattering share (phase P_R)
        LinearColour MiePart{};      // [1/km] - Mie + haze scattering share (phase P_CS)
    };

    [[nodiscard]] LunarMediumSample EvaluateLunarMedium(const LunarAtmosphereProfile& LunarAtmosphere, float AltitudeKm) noexcept
    {
        LunarMediumSample Medium{};
        if (AltitudeKm > LunarAtmosphere.TopAltitudeKm)
            return Medium;
        const float Altitude = std::max(0.0f, AltitudeKm);
        // Smooth top taper avoids a visible hard boundary at R_m + H_a; it begins above the detached haze Gaussian (peak + 3 half-widths)
        // so the layer's upper flank is never truncated into a hard outer ring.
        const float HazeClearance   = (LunarAtmosphere.DetachedHazeAltitudeKm > 0.0f) ? LunarAtmosphere.DetachedHazeAltitudeKm + 3.0f * LunarAtmosphere.DetachedHazeHalfWidthKm : 0.0f;
        const float TaperStart      = std::min(LunarAtmosphere.TopAltitudeKm * 0.95f, std::max(LunarAtmosphere.TopAltitudeKm * 0.70f, HazeClearance));
        const float TopTaper        = 1.0f - EvaluateSmoothStep(TaperStart, LunarAtmosphere.TopAltitudeKm, Altitude);
        const float RayleighDensity = std::exp(-Altitude / std::max(0.1f, LunarAtmosphere.RayleighScaleHeightKm)) * TopTaper;
        const float MieDensity      = std::exp(-Altitude / std::max(0.1f, LunarAtmosphere.MieScaleHeightKm)) * TopTaper;
        float       HazeDensity     = 0.0f;
        if (LunarAtmosphere.DetachedHazeAltitudeKm > 0.0f)
        {
            const float HazeOffset = (Altitude - LunarAtmosphere.DetachedHazeAltitudeKm) / std::max(0.5f, LunarAtmosphere.DetachedHazeHalfWidthKm);
            HazeDensity = std::exp(-HazeOffset * HazeOffset) * TopTaper;
        }

        Medium.RayleighPart = ScaleColour(LunarAtmosphere.RayleighScatteringPerKm, RayleighDensity);
        Medium.MiePart      = AddColour(ScaleColour(LunarAtmosphere.MieScatteringPerKm, MieDensity), ScaleColour(LunarAtmosphere.DetachedHazeScatterPerKm, HazeDensity));
        Medium.Scattering   = AddColour(Medium.RayleighPart, Medium.MiePart);
        Medium.Extinction   = AddColour(AddColour(Medium.RayleighPart, ScaleColour(LunarAtmosphere.MieExtinctionPerKm, MieDensity)),
                                        ScaleColour(LunarAtmosphere.DetachedHazeScatterPerKm, HazeDensity));
        return Medium;
    }

    // 📝 Radius-uniform chord partition. The medium depends on altitude only, so each side of the closest approach (s = 0, impact b)
    //    is split into equal radial increments Δr: r(s) = sqrt(b² + s²). This resolves the exponential base at the tangent point and any
    //    detached layer crossed obliquely far from it; uniform-in-s midpoint steps alias both and produce ghost limb rings.
    //    Outputs the sub-segment bounds and the radius-midpoint parameter of sub-segment Ordinal (of Count) over [Start, End].
    void ResolveRadialChordSegment(
        uint32_t Ordinal,
        uint32_t Count,
        float    Start,
        float    End,
        float    ImpactKm,
        float&   OutLower,
        float&   OutUpper,
        float&   OutMiddle) noexcept
    {
        const float ImpactSquared          = ImpactKm * ImpactKm;
        const float Pivot                  = std::clamp(0.0f, Start, End);
        const auto  RadiusAt               = [&](float Param) noexcept { return std::sqrt(ImpactSquared + Param * Param); };
        const auto  ChordParameterAtRadius = [&](float Radius, float Sign) noexcept { return Sign * std::sqrt(std::max(0.0f, Radius * Radius - ImpactSquared)); };
        const float PivotRadius            = RadiusAt(Pivot);
        const float FrontRange             = std::max(0.0f, RadiusAt(Start) - PivotRadius);
        const float RearRange              = std::max(0.0f, RadiusAt(End) - PivotRadius);

        uint32_t FrontCount = 0u;
        if (FrontRange > 1.0e-6f && RearRange > 1.0e-6f)
            FrontCount = std::clamp(static_cast<uint32_t>(std::lround(static_cast<float>(Count) * FrontRange / (FrontRange + RearRange))), 1u, Count - 1u);
        else if (FrontRange > 1.0e-6f)
            FrontCount = Count;

        if (Ordinal < FrontCount)
        {
            // Front side (s <= Pivot <= 0 or both negative): radius decreases toward the pivot.
            const float StartRadius = RadiusAt(Start);
            const float Increment   = FrontRange / static_cast<float>(FrontCount);
            OutLower  = (Ordinal == 0u) ? Start : ChordParameterAtRadius(StartRadius - Increment * static_cast<float>(Ordinal), -1.0f);
            OutUpper  = (Ordinal + 1u == FrontCount) ? Pivot : ChordParameterAtRadius(StartRadius - Increment * static_cast<float>(Ordinal + 1u), -1.0f);
            OutMiddle = ChordParameterAtRadius(StartRadius - Increment * (static_cast<float>(Ordinal) + 0.5f), -1.0f);
        }
        else
        {
            // Rear side (s >= Pivot >= 0 or both positive): radius increases away from the pivot.
            const uint32_t RearOrdinal = Ordinal - FrontCount;
            const uint32_t RearCount   = Count - FrontCount;
            const float    Increment   = RearRange / static_cast<float>(RearCount);
            OutLower  = (RearOrdinal == 0u) ? Pivot : ChordParameterAtRadius(PivotRadius + Increment * static_cast<float>(RearOrdinal), 1.0f);
            OutUpper  = (RearOrdinal + 1u == RearCount) ? End : ChordParameterAtRadius(PivotRadius + Increment * static_cast<float>(RearOrdinal + 1u), 1.0f);
            OutMiddle = ChordParameterAtRadius(PivotRadius + Increment * (static_cast<float>(RearOrdinal) + 0.5f), 1.0f);
        }
        OutMiddle = std::clamp(OutMiddle, OutLower, OutUpper);
    }

    // 📝 Radial step budget resolving the thinnest vertical structure of the lunar medium (≈ 0.5 of the smallest scale height / haze
    //    half-width per radial increment), bounded by the caller's request and a hard cap.
    [[nodiscard]] uint32_t ResolveRadialStepCount(const LunarAtmosphereProfile& LunarAtmosphere, float RadialSpanKm, uint32_t RequestedCount) noexcept
    {
        float Structure = std::min(LunarAtmosphere.RayleighScaleHeightKm, LunarAtmosphere.MieScaleHeightKm);
        if (LunarAtmosphere.DetachedHazeAltitudeKm > 0.0f)
            Structure = std::min(Structure, LunarAtmosphere.DetachedHazeHalfWidthKm);
        const float    Increment = std::max(0.5f, 0.5f * Structure);
        const uint32_t Derived   = static_cast<uint32_t>(std::ceil(std::max(0.0f, RadialSpanKm) / Increment));
        return std::clamp(Derived, std::max(4u, RequestedCount), 96u);
    }

    // 📝 Sun-ward transmittance from moon-centred point Position [km] through the lunar atmosphere, including soft body umbra/penumbra.
    [[nodiscard]] LinearColour EvaluateLunarSunTransmittance(
        const LunarAtmosphereProfile& LunarAtmosphere,
        const Vector3f&               Position,
        const Vector3f&               SolarAxis,
        uint32_t                      StepCount) noexcept
    {
        const float BodyRadius    = LunarAtmosphere.BodyRadiusKm;
        const float AlongSun      = DotVector(Position, SolarAxis);
        const float RadiusSquared = DotVector(Position, Position);
        const float ImpactSquared = std::max(0.0f, RadiusSquared - AlongSun * AlongSun);

        // Body umbra: sun-ward ray passes the moon centre ahead of the point with impact parameter < R_m (penumbra ~0.53° solar disc).
        float Umbra = 1.0f;
        if (AlongSun < 0.0f)
        {
            const float ImpactKm      = std::sqrt(ImpactSquared);
            const float PenumbraWidth = 0.0046f * std::abs(AlongSun) + 1.5f;
            Umbra = EvaluateSmoothStep(BodyRadius - PenumbraWidth, BodyRadius + PenumbraWidth, ImpactKm);
            if (Umbra <= 0.0f)
                return LinearColour{};
        }
        if (!LunarAtmosphere.AtmosphereEnabled)
            return LinearColour{ Umbra, Umbra, Umbra };

        const float TopRadius        = BodyRadius + LunarAtmosphere.TopAltitudeKm;
        const float ExitDiscriminant = AlongSun * AlongSun - (RadiusSquared - TopRadius * TopRadius);
        if (ExitDiscriminant <= 0.0f)
            return LinearColour{ Umbra, Umbra, Umbra };
        const float ExitDistance = -AlongSun + std::sqrt(ExitDiscriminant);
        if (ExitDistance <= 0.0f)
            return LinearColour{ Umbra, Umbra, Umbra };

        // Re-centre the sun ray on its closest approach (t* = -AlongSun) so the radial partition applies directly.
        const float    SunImpact    = std::sqrt(ImpactSquared);
        const uint32_t SegmentCount = std::max(2u, StepCount);
        LinearColour   OpticalDepth {};
        for (uint32_t Step = 0u; Step < SegmentCount; ++Step)
        {
            float Lower  = 0.0f;
            float Upper  = 0.0f;
            float Middle = 0.0f;
            ResolveRadialChordSegment(Step, SegmentCount, AlongSun, ExitDistance + AlongSun, SunImpact, Lower, Upper, Middle);
            const float             StepKm      = Upper - Lower;
            const Vector3f          SamplePoint = AddVector(Position, ScaleVector(SolarAxis, Middle - AlongSun));
            const float             AltitudeKm  = VectorLength(SamplePoint) - BodyRadius;
            const LunarMediumSample Medium      = EvaluateLunarMedium(LunarAtmosphere, AltitudeKm);
            OpticalDepth = AddColour(OpticalDepth, ScaleColour(Medium.Extinction, StepKm));
        }
        return LinearColour{
            Umbra * std::exp(-OpticalDepth.Red),
            Umbra * std::exp(-OpticalDepth.Green),
            Umbra * std::exp(-OpticalDepth.Blue) };
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                              REGOLITH BRDF
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Hapke shadow-hiding opposition surge B(g) = B0 / (1 + tan(g/2) / h).
    [[nodiscard]] float EvaluateHapkeOppositionSurge(float PhaseAngleRadians, float SurgeAmplitude, float SurgeWidth) noexcept
    {
        const float HalfTangent = std::tan(std::clamp(0.5f * PhaseAngleRadians, 0.0f, 1.55f));
        return SurgeAmplitude / (1.0f + HalfTangent / std::max(1.0e-3f, SurgeWidth));
    }

    // 📝 Combined porous-regolith reflectance factor: Lommel-Seeliger 2μ0/(μ0+μ) blended with Oren-Nayar qualitative rough diffuse.
    [[nodiscard]] float EvaluateRegolithReflectance(
        const Vector3f& ShadingNormal,
        const Vector3f& SolarAxis,
        const Vector3f& ViewAxis,
        float           Roughness,
        float           LommelSeeligerWeight) noexcept
    {
        const float IncidentCosine = DotVector(ShadingNormal, SolarAxis);
        if (IncidentCosine <= 0.0f)
            return 0.0f;
        const float EmergentCosine = std::max(1.0e-3f, DotVector(ShadingNormal, ViewAxis));

        const float LommelSeeliger = 2.0f * IncidentCosine / (IncidentCosine + EmergentCosine);

        const float    Variance             = Roughness * Roughness;
        const float    TermA                = 1.0f - 0.5f * Variance / (Variance + 0.33f);
        const float    TermB                = 0.45f * Variance / (Variance + 0.09f);
        const Vector3f SunTangent           = SubtractVector(SolarAxis, ScaleVector(ShadingNormal, IncidentCosine));
        const Vector3f ViewTangent          = SubtractVector(ViewAxis,  ScaleVector(ShadingNormal, EmergentCosine));
        const float    TangentLengthProduct = VectorLength(SunTangent) * VectorLength(ViewTangent);
        const float    AzimuthCosine        = (TangentLengthProduct > 1.0e-6f) ? std::max(0.0f, DotVector(SunTangent, ViewTangent) / TangentLengthProduct) : 0.0f;
        const float    IncidentAngle        = std::acos(std::clamp(IncidentCosine, 0.0f, 1.0f));
        const float    EmergentAngle        = std::acos(std::clamp(EmergentCosine, 0.0f, 1.0f));
        const float    AlphaAngle           = std::max(IncidentAngle, EmergentAngle);
        const float    BetaAngle            = std::min(IncidentAngle, EmergentAngle);
        const float    OrenNayar            = IncidentCosine * (TermA + TermB * AzimuthCosine * std::sin(AlphaAngle) * std::tan(std::min(BetaAngle, 1.5f)));

        const float Weight = std::clamp(LommelSeeligerWeight, 0.0f, 1.0f);
        return Weight * LommelSeeliger + (1.0f - Weight) * OrenNayar;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                     SINGLE LUNAR BODY GEOMETRY & ORIENTATION
    //--------------------------------------------------------------------------------------------------------------------

    struct LunarOrientation
    {
        Vector3f RightAxis{};   // [-] - Body +X (east on the visible face)
        Vector3f UpAxis{};      // [-] - Body +Y (spin pole, tilted)
        Vector3f ForwardAxis{}; // [-] - Body +Z (sub-viewer point direction from centre)
    };

    [[nodiscard]] LunarOrientation ConstructLunarOrientation(const LunarBodyDescriptor& Moon, const Vector3f& MoonAxis) noexcept
    {
        LunarOrientation Orientation{};
        Orientation.ForwardAxis = ScaleVector(MoonAxis, -1.0f);
        const Vector3f VerticalHint = (std::abs(MoonAxis.YCoord) < 0.97f) ? Vector3f{ 0.0f, 1.0f, 0.0f } : Vector3f{ 0.0f, 0.0f, 1.0f };
        Vector3f       Up           = NormalizeVector(SubtractVector(VerticalHint, ScaleVector(Orientation.ForwardAxis, DotVector(VerticalHint, Orientation.ForwardAxis))));
        Vector3f       Right        = CrossVector(Up, Orientation.ForwardAxis);
        const float    CosineTilt   = std::cos(Moon.AxialTiltRadians);
        const float    SineTilt     = std::sin(Moon.AxialTiltRadians);
        Orientation.UpAxis    = NormalizeVector(AddVector(ScaleVector(Up, CosineTilt), ScaleVector(Right, SineTilt)));
        Orientation.RightAxis = NormalizeVector(CrossVector(Orientation.UpAxis, Orientation.ForwardAxis));
        return Orientation;
    }

    [[nodiscard]] float EvaluateLunarDistanceKm(const LunarBodyDescriptor& Moon) noexcept
    {
        const float AngularRadius = std::clamp(Moon.AngularRadiusRadians, 1.0e-4f, 1.2f);
        return Moon.Atmosphere.BodyRadiusKm / std::sin(AngularRadius);
    }

    [[nodiscard]] LinearColour EvaluateLunarSurfaceRadiance(
        const LunarBodyDescriptor& Moon,
        const LunarSurfaceAtlas*   OptionalAtlas,
        const LunarOrientation&    Orientation,
        const Vector3f&            SurfaceNormal,
        const Vector3f&            SurfacePoint,
        const Vector3f&            ViewAxis,
        const Vector3f&            SolarAxis,
        const Vector3f&            MoonAxis,
        const LinearColour&        SolarIrradiance) noexcept
    {
        // Body-frame spherical coordinates of the surface point (longitude rotated by selenographic spin).
        const float BodyX         = DotVector(SurfaceNormal, Orientation.RightAxis);
        const float BodyY         = DotVector(SurfaceNormal, Orientation.UpAxis);
        const float BodyZ         = DotVector(SurfaceNormal, Orientation.ForwardAxis);
        const float Latitude      = std::asin(std::clamp(BodyY, -1.0f, 1.0f));
        const float FaceLongitude = std::atan2(BodyX, BodyZ);
        const float CoordU        = (FaceLongitude + Moon.SpinLongitudeRadians) / Atmosphere::TauConstant + 0.5f;
        const float CoordV        = 0.5f - Latitude / Atmosphere::PiConstant;

        const uint32_t                PresetIndex = std::min(static_cast<uint32_t>(Moon.Preset), MaximumLunarBodyCount - 1u);
        const DecodedLunarSurfaceMap* Map         = nullptr;
        if (OptionalAtlas != nullptr && OptionalAtlas->Maps[PresetIndex].Width > 0u)
            Map = &OptionalAtlas->Maps[PresetIndex];

        LinearColour Relative      { 1.0f, 1.0f, 1.0f };
        Vector3f     ShadingNormal = SurfaceNormal;
        if (Map != nullptr)
        {
            Relative = SampleSurfaceMap(*Map, CoordU, CoordV);

            // Luminance-gradient relief normal: central differences one texel east/north perturb the geometric normal.
            const float TexelU         = 1.0f / static_cast<float>(Map->Width);
            const float TexelV         = 1.0f / static_cast<float>(Map->Height);
            const float EastHeight     = EvaluateRelativeLuminance(SampleSurfaceMap(*Map, CoordU + TexelU, CoordV));
            const float WestHeight     = EvaluateRelativeLuminance(SampleSurfaceMap(*Map, CoordU - TexelU, CoordV));
            const float NorthHeight    = EvaluateRelativeLuminance(SampleSurfaceMap(*Map, CoordU, CoordV - TexelV));
            const float SouthHeight    = EvaluateRelativeLuminance(SampleSurfaceMap(*Map, CoordU, CoordV + TexelV));
            const float CosineLatitude = std::max(0.25f, std::cos(Latitude));
            const float GradientEast   = (EastHeight - WestHeight) * 0.5f;
            const float GradientNorth  = (NorthHeight - SouthHeight) * 0.5f;

            const float    SineLongitude   = std::sin(FaceLongitude);
            const float    CosineLongitude = std::cos(FaceLongitude);
            const Vector3f EastAxis        = NormalizeVector(AddVector(ScaleVector(Orientation.RightAxis, CosineLongitude), ScaleVector(Orientation.ForwardAxis, -SineLongitude)));
            const Vector3f NorthAxis       = NormalizeVector(CrossVector(SurfaceNormal, EastAxis));
            const float    ReliefGain      = 1.6f * Moon.ReliefStrength;
            ShadingNormal = NormalizeVector(SubtractVector(SurfaceNormal,
                AddVector(ScaleVector(EastAxis, GradientEast * ReliefGain / CosineLatitude),
                          ScaleVector(NorthAxis, GradientNorth * ReliefGain))));
        }
        else
        {
            const float Speckle = EvaluateRotatedFbm3D(ScaleVector(Vector3f{ BodyX, BodyY, BodyZ }, 9.0f), 4u, 2503u + PresetIndex * 7u);
            Relative = LinearColour{ 0.7f + 0.6f * Speckle, 0.7f + 0.6f * Speckle, 0.7f + 0.6f * Speckle };
        }

        // Mare-to-highland contrast applied to luminance only (chromaticity preserved).
        const float        RelativeLuminance = std::max(1.0e-4f, EvaluateRelativeLuminance(Relative));
        const float        ContrastScale     = std::pow(RelativeLuminance, Moon.AlbedoContrast) / RelativeLuminance;
        const LinearColour Albedo            = ScaleColour(MultiplyColour(Relative, Moon.RegolithTint), Moon.GeometricAlbedo * ContrastScale);

        // Geometric terminator gate: relief may catch grazing light slightly past the terminator but never deep on the night side.
        const float GeometricIncidence = DotVector(SurfaceNormal, SolarAxis);
        const float TerminatorGate     = EvaluateSmoothStep(-0.06f, 0.02f, GeometricIncidence);

        const float PhaseAngle  = std::acos(std::clamp(DotVector(SolarAxis, ViewAxis), -1.0f, 1.0f));
        const float Opposition  = 1.0f + EvaluateHapkeOppositionSurge(PhaseAngle, Moon.OppositionSurgeAmp, Moon.OppositionSurgeWidth);
        const float Reflectance = EvaluateRegolithReflectance(ShadingNormal, SolarAxis, ViewAxis, Moon.RegolithRoughness, Moon.LommelSeeligerWeight)
                                * Opposition * TerminatorGate;

        // Sunlight reaching the surface through the moon's own atmosphere (reddened terminator on hazy bodies).
        const LinearColour SurfaceSunTransmittance = EvaluateLunarSunTransmittance(Moon.Atmosphere, SurfacePoint, SolarAxis, 8u);
        const LinearColour Incident                = MultiplyColour(SolarIrradiance, SurfaceSunTransmittance);
        LinearColour       Radiance                = ScaleColour(MultiplyColour(Albedo, Incident), Reflectance / Atmosphere::PiConstant);

        // Cryogenic water-ice Fresnel highlight (GGX, α = 0.22, F0 = 0.02).
        if (Moon.IceSpecularStrength > 0.0f && GeometricIncidence > 0.0f)
        {
            const Vector3f  HalfAxis     = NormalizeVector(AddVector(SolarAxis, ViewAxis));
            const float     NormalHalf   = std::max(0.0f, DotVector(ShadingNormal, HalfAxis));
            const float     Incidence    = std::max(0.0f, DotVector(ShadingNormal, SolarAxis));
            const float     Emergence    = std::max(1.0e-3f, DotVector(ShadingNormal, ViewAxis));
            constexpr float AlphaSq      = 0.22f * 0.22f;
            const float     Denominator  = NormalHalf * NormalHalf * (AlphaSq - 1.0f) + 1.0f;
            const float     Distribution = AlphaSq / (Atmosphere::PiConstant * Denominator * Denominator);
            const float     HalfCosine   = std::clamp(DotVector(ViewAxis, HalfAxis), 0.0f, 1.0f);
            const float     Fresnel      = 0.02f + 0.98f * std::pow(1.0f - HalfCosine, 5.0f);
            const float     Specular     = Distribution * Fresnel * Incidence / (4.0f * std::max(Incidence, 1.0e-3f) * Emergence) * Incidence;
            Radiance = AddColour(Radiance, ScaleColour(Incident, Moon.IceSpecularStrength * Specular * TerminatorGate));
        }

        // Planetshine: the host planet's sunlit hemisphere (fraction ½(1 + S·D)) back-lights the moon's night side with Rayleigh-blue light.
        if (Moon.PlanetshineIntensity > 0.0f)
        {
            const float        PlanetPhase  = 0.5f * (1.0f + DotVector(SolarAxis, MoonAxis));
            const float        FacingPlanet = std::max(0.0f, DotVector(SurfaceNormal, ScaleVector(MoonAxis, -1.0f)));
            const LinearColour Earthlight   = ScaleColour(MultiplyColour(SolarIrradiance, LinearColour{ 0.74f, 0.84f, 1.00f }),
                                                          Moon.PlanetshineIntensity * PlanetPhase * FacingPlanet / Atmosphere::PiConstant);
            Radiance = AddColour(Radiance, MultiplyColour(Albedo, Earthlight));
        }
        return Radiance;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

LunarBodyDescriptor BuildLunarPreset(
    LunarPresetIdentifier Preset,
    const Vector3f&       DirectionLocal,
    float                 AngularRadiusRadians) noexcept
{
    LunarBodyDescriptor Moon{};
    Moon.Preset               = Preset;
    Moon.VisibilityEnabled    = true;
    Moon.DirectionLocal       = NormalizeVector(DirectionLocal);
    Moon.AngularRadiusRadians = AngularRadiusRadians;

    LunarAtmosphereProfile& LunarAtmosphere = Moon.Atmosphere;
    switch (Preset)
    {
        case LunarPresetIdentifier::Luna:
            // Silicate regolith; tenuous sunlit Na/K exosphere + levitated electrostatic dust (faint yellow limb glow).
            Moon.GeometricAlbedo                    = 0.14f;
            Moon.AlbedoContrast                     = 1.10f;
            Moon.RegolithTint                       = LinearColour{ 1.00f, 0.97f, 0.92f };
            Moon.RegolithRoughness                  = 0.62f;
            Moon.LommelSeeligerWeight               = 0.78f;
            Moon.OppositionSurgeAmp                 = 0.60f;
            Moon.OppositionSurgeWidth               = 0.075f;
            Moon.ReliefStrength                     = 1.40f;
            Moon.PlanetshineIntensity               = 0.0105f;
            LunarAtmosphere.BodyRadiusKm            = 1737.4f;
            LunarAtmosphere.TopAltitudeKm           = 90.0f;
            LunarAtmosphere.RayleighScaleHeightKm   = 30.0f;
            LunarAtmosphere.MieScaleHeightKm        = 12.0f;
            LunarAtmosphere.RayleighScatteringPerKm = LinearColour{ 0.00010f, 0.00014f, 0.00022f };
            LunarAtmosphere.MieScatteringPerKm      = LinearColour{ 0.00070f, 0.00058f, 0.00026f };
            LunarAtmosphere.MieExtinctionPerKm      = LinearColour{ 0.00080f, 0.00068f, 0.00034f };
            LunarAtmosphere.MieAsymmetry            = 0.62f;
            break;

        case LunarPresetIdentifier::Ember:
            // Ferric-oxide dust troposphere (amber, low scale height) beneath a CO2 Rayleigh column that wins at altitude (blue upper limb).
            Moon.GeometricAlbedo                    = 0.17f;
            Moon.AlbedoContrast                     = 1.15f;
            Moon.RegolithTint                       = LinearColour{ 1.00f, 0.96f, 0.92f };
            Moon.RegolithRoughness                  = 0.55f;
            Moon.LommelSeeligerWeight               = 0.55f;
            Moon.OppositionSurgeAmp                 = 0.35f;
            Moon.OppositionSurgeWidth               = 0.10f;
            Moon.ReliefStrength                     = 1.25f;
            Moon.PlanetshineIntensity               = 0.0063f;
            LunarAtmosphere.BodyRadiusKm            = 3389.5f;
            LunarAtmosphere.TopAltitudeKm           = 160.0f;
            LunarAtmosphere.RayleighScaleHeightKm   = 26.0f;
            LunarAtmosphere.MieScaleHeightKm        = 9.0f;
            LunarAtmosphere.RayleighScatteringPerKm = LinearColour{ 0.0011f, 0.0026f, 0.0064f };
            LunarAtmosphere.MieScatteringPerKm      = LinearColour{ 0.0072f, 0.0042f, 0.0019f };
            LunarAtmosphere.MieExtinctionPerKm      = LinearColour{ 0.0084f, 0.0056f, 0.0032f };
            LunarAtmosphere.MieAsymmetry            = 0.70f;
            break;

        case LunarPresetIdentifier::Glacier:
            // Fractured water-ice crust; cryovolcanic ice-crystal plume atmosphere scattering cyan-sapphire.
            Moon.GeometricAlbedo                    = 0.40f;
            Moon.AlbedoContrast                     = 1.05f;
            Moon.RegolithTint                       = LinearColour{ 0.96f, 0.99f, 1.00f };
            Moon.RegolithRoughness                  = 0.30f;
            Moon.LommelSeeligerWeight               = 0.35f;
            Moon.OppositionSurgeAmp                 = 0.45f;
            Moon.OppositionSurgeWidth               = 0.05f;
            Moon.ReliefStrength                     = 1.10f;
            Moon.IceSpecularStrength                = 0.55f;
            Moon.PlanetshineIntensity               = 0.0091f;
            LunarAtmosphere.BodyRadiusKm            = 1560.8f;
            LunarAtmosphere.TopAltitudeKm           = 170.0f;
            LunarAtmosphere.RayleighScaleHeightKm   = 34.0f;
            LunarAtmosphere.MieScaleHeightKm        = 26.0f;
            LunarAtmosphere.RayleighScatteringPerKm = LinearColour{ 0.0007f, 0.0024f, 0.0046f };
            LunarAtmosphere.MieScatteringPerKm      = LinearColour{ 0.0011f, 0.0016f, 0.0021f };
            LunarAtmosphere.MieExtinctionPerKm      = LinearColour{ 0.0012f, 0.0017f, 0.0022f };
            LunarAtmosphere.MieAsymmetry            = 0.80f;
            break;

        case LunarPresetIdentifier::Sulfur:
            // Silicate-sulfur volcanic plains; luminous golden SO2/S8 plume aerosol under a violet upper Rayleigh fringe.
            Moon.GeometricAlbedo                    = 0.34f;
            Moon.AlbedoContrast                     = 1.20f;
            Moon.RegolithTint                       = LinearColour{ 1.00f, 0.98f, 0.90f };
            Moon.RegolithRoughness                  = 0.48f;
            Moon.LommelSeeligerWeight               = 0.60f;
            Moon.OppositionSurgeAmp                 = 0.40f;
            Moon.OppositionSurgeWidth               = 0.09f;
            Moon.ReliefStrength                     = 1.00f;
            Moon.PlanetshineIntensity               = 0.0070f;
            LunarAtmosphere.BodyRadiusKm            = 1821.6f;
            LunarAtmosphere.TopAltitudeKm           = 200.0f;
            LunarAtmosphere.RayleighScaleHeightKm   = 46.0f;
            LunarAtmosphere.MieScaleHeightKm        = 22.0f;
            LunarAtmosphere.RayleighScatteringPerKm = LinearColour{ 0.0016f, 0.0009f, 0.0042f };
            LunarAtmosphere.MieScatteringPerKm      = LinearColour{ 0.0048f, 0.0034f, 0.0009f };
            LunarAtmosphere.MieExtinctionPerKm      = LinearColour{ 0.0054f, 0.0042f, 0.0020f };
            LunarAtmosphere.MieAsymmetry            = 0.66f;
            break;

        case LunarPresetIdentifier::Shroud:
            // Organic methane-nitrogen world: optically thick amber tholin troposphere + detached cobalt stratospheric haze (~ 500 km).
            Moon.GeometricAlbedo                     = 0.21f;
            Moon.AlbedoContrast                      = 1.05f;
            Moon.RegolithTint                        = LinearColour{ 1.00f, 0.95f, 0.86f };
            Moon.RegolithRoughness                   = 0.50f;
            Moon.LommelSeeligerWeight                = 0.40f;
            Moon.OppositionSurgeAmp                  = 0.15f;
            Moon.OppositionSurgeWidth                = 0.20f;
            Moon.ReliefStrength                      = 0.70f;
            Moon.PlanetshineIntensity                = 0.0049f;
            LunarAtmosphere.BodyRadiusKm             = 2574.7f;
            LunarAtmosphere.TopAltitudeKm            = 700.0f;
            LunarAtmosphere.RayleighScaleHeightKm    = 60.0f;
            LunarAtmosphere.MieScaleHeightKm         = 70.0f;
            LunarAtmosphere.RayleighScatteringPerKm  = LinearColour{ 0.0004f, 0.0009f, 0.0022f };
            LunarAtmosphere.MieScatteringPerKm       = LinearColour{ 0.0070f, 0.0044f, 0.0016f };
            LunarAtmosphere.MieExtinctionPerKm       = LinearColour{ 0.0090f, 0.0066f, 0.0040f };
            LunarAtmosphere.MieAsymmetry             = 0.64f;
            LunarAtmosphere.DetachedHazeAltitudeKm   = 500.0f;
            LunarAtmosphere.DetachedHazeHalfWidthKm  = 22.0f;
            LunarAtmosphere.DetachedHazeScatterPerKm = LinearColour{ 0.00045f, 0.00090f, 0.00230f };
            break;

        case LunarPresetIdentifier::Shard:
        default:
            // Scarred ancient basin moon; crisp thin N2/CH4 pale-indigo Rayleigh atmosphere with layered photochemical haze bands.
            Moon.GeometricAlbedo                     = 0.30f;
            Moon.AlbedoContrast                      = 1.25f;
            Moon.RegolithTint                        = LinearColour{ 0.98f, 0.97f, 1.00f };
            Moon.RegolithRoughness                   = 0.58f;
            Moon.LommelSeeligerWeight                = 0.65f;
            Moon.OppositionSurgeAmp                  = 0.50f;
            Moon.OppositionSurgeWidth                = 0.07f;
            Moon.ReliefStrength                      = 1.45f;
            Moon.PlanetshineIntensity                = 0.0077f;
            LunarAtmosphere.BodyRadiusKm             = 1353.4f;
            LunarAtmosphere.TopAltitudeKm            = 210.0f;
            LunarAtmosphere.RayleighScaleHeightKm    = 38.0f;
            LunarAtmosphere.MieScaleHeightKm         = 16.0f;
            LunarAtmosphere.RayleighScatteringPerKm  = LinearColour{ 0.0009f, 0.0015f, 0.0036f };
            LunarAtmosphere.MieScatteringPerKm       = LinearColour{ 0.0003f, 0.0003f, 0.0003f };
            LunarAtmosphere.MieExtinctionPerKm       = LinearColour{ 0.0004f, 0.0004f, 0.0004f };
            LunarAtmosphere.MieAsymmetry             = 0.70f;
            LunarAtmosphere.DetachedHazeAltitudeKm   = 95.0f;
            LunarAtmosphere.DetachedHazeHalfWidthKm  = 9.0f;
            LunarAtmosphere.DetachedHazeScatterPerKm = LinearColour{ 0.0006f, 0.0009f, 0.0018f };
            break;
    }
    return Moon;
}

void LoadLunarSurfaceAtlas(LunarSurfaceAtlas& OutAtlas, const std::string& OptionalSearchRoot)
{
    OutAtlas.DecodedAssetCount = 0u;

    std::vector<std::string> SearchRoots;
    if (!OptionalSearchRoot.empty())
        SearchRoots.push_back(OptionalSearchRoot + "/");
    for (const char* Ascent : { "", "../", "../../", "../../../", "../../../../" })
        SearchRoots.push_back(std::string(Ascent) + "Documentation/Assets/CelestialMaps/");
    SearchRoots.push_back("Assets/CelestialMaps/");

    for (uint32_t PresetIndex = 0u; PresetIndex < MaximumLunarBodyCount; ++PresetIndex)
    {
        DecodedLunarSurfaceMap& Map = OutAtlas.Maps[PresetIndex];
        Map                         = DecodedLunarSurfaceMap{};
        bool DecodedEnabled         = false;
        for (const std::string& Root : SearchRoots)
        {
            const std::string Candidate = Root + LunarMapFileNames[PresetIndex];
            std::FILE*        Probe     = std::fopen(Candidate.c_str(), "rb");
            if (Probe == nullptr)
                continue;
            std::fclose(Probe);
            if (DecodeSurfaceMapAsset(Candidate, Map))
            {
                DecodedEnabled = true;
                break;
            }
        }
        if (DecodedEnabled)
            ++OutAtlas.DecodedAssetCount;
        else
            SynthesizeProceduralSurfaceMap(static_cast<LunarPresetIdentifier>(PresetIndex), Map);
    }
}

LinearColour ConvertColourIndexToLinearRec709(float ColourIndexBV) noexcept
{
    const float ColourIndex = std::clamp(ColourIndexBV, -0.40f, 2.00f);
    // Ballesteros (2012) effective temperature from Johnson B-V.
    const float Temperature = 4600.0f * (1.0f / (0.92f * ColourIndex + 1.7f) + 1.0f / (0.92f * ColourIndex + 0.62f));

    // Planck spectral radiance integrated against the Wyman-Sloan-Shirley (2013) analytic CIE 1931 2° colour-matching functions.
    const auto Lobe = [](float Wavelength, float Mean, float LowerSigma, float UpperSigma) noexcept
    {
        const float Offset = (Wavelength - Mean) / ((Wavelength < Mean) ? LowerSigma : UpperSigma);
        return std::exp(-0.5f * Offset * Offset);
    };

    double TristimulusX = 0.0;
    double TristimulusY = 0.0;
    double TristimulusZ = 0.0;
    for (float Wavelength = 380.0f; Wavelength <= 780.0f; Wavelength += 5.0f)
    {
        const double Metres   = static_cast<double>(Wavelength) * 1.0e-9;
        const double Exponent = 1.438776877e-2 / (Metres * static_cast<double>(Temperature));
        const double Spectral = 1.0 / (Metres * Metres * Metres * Metres * Metres * (std::exp(std::min(Exponent, 80.0)) - 1.0));

        const float MatchX = 1.056f * Lobe(Wavelength, 599.8f, 37.9f, 31.0f) + 0.362f * Lobe(Wavelength, 442.0f, 16.0f, 26.7f)
                           - 0.065f * Lobe(Wavelength, 501.1f, 20.4f, 26.2f);
        const float MatchY = 0.821f * Lobe(Wavelength, 568.8f, 46.9f, 40.5f) + 0.286f * Lobe(Wavelength, 530.9f, 16.3f, 31.1f);
        const float MatchZ = 1.217f * Lobe(Wavelength, 437.0f, 11.8f, 36.0f) + 0.681f * Lobe(Wavelength, 459.0f, 26.0f, 13.8f);
        TristimulusX += Spectral * MatchX;
        TristimulusY += Spectral * MatchY;
        TristimulusZ += Spectral * MatchZ;
    }

    const float  NormalisedX = static_cast<float>(TristimulusX / std::max(1.0e-30, TristimulusY));
    const float  NormalisedZ = static_cast<float>(TristimulusZ / std::max(1.0e-30, TristimulusY));
    LinearColour Rec709      {
        std::max(0.0f,  3.2406f * NormalisedX - 1.5372f - 0.4986f * NormalisedZ),
        std::max(0.0f, -0.9689f * NormalisedX + 1.8758f + 0.0415f * NormalisedZ),
        std::max(0.0f,  0.0557f * NormalisedX - 0.2040f + 1.0570f * NormalisedZ) };

    // Re-normalise to unit relative luminance so flux is carried entirely by the Pogson magnitude.
    const float Luminance = std::max(1.0e-6f, EvaluateRelativeLuminance(Rec709));
    return ScaleColour(Rec709, 1.0f / Luminance);
}

LunarRayResult EvaluateSingleLunarBody(
    const LunarBodyDescriptor&     Moon,
    const LunarSurfaceAtlas*       OptionalAtlas,
    const Vector3f&                LocalDirection,
    const Vector3f&                SolarDirectionLocal,
    const LinearColour&            SolarIlluminance,
    uint32_t                       AtmosphereStepCount) noexcept
{
    LunarRayResult Result{};
    if (!Moon.VisibilityEnabled)
        return Result;

    const LunarAtmosphereProfile& LunarAtmosphere = Moon.Atmosphere;
    const Vector3f                MoonAxis        = NormalizeVector(Moon.DirectionLocal);
    const float                   DistanceKm      = EvaluateLunarDistanceKm(Moon);
    const float                   BodyRadius      = LunarAtmosphere.BodyRadiusKm;
    const float                   TopRadius       = BodyRadius + (LunarAtmosphere.AtmosphereEnabled ? LunarAtmosphere.TopAltitudeKm : 0.0f);

    const float Separation = EvaluateAngularSeparation(LocalDirection, MoonAxis);
    const float ReachAngle = std::asin(std::clamp(TopRadius / DistanceKm, 0.0f, 1.0f)) * 1.02f + 1.0e-5f;
    const float ImpactKm   = DistanceKm * std::sin(Separation);
    Result.NormalizedImpact = ImpactKm / BodyRadius;
    if (Separation > ReachAngle || Separation > 1.5f)
        return Result;

    const Vector3f     SolarAxis       = (VectorLength(Moon.IlluminationDirection) > 0.5f)
                                       ? NormalizeVector(Moon.IlluminationDirection)
                                       : NormalizeVector(SolarDirectionLocal);
    const LinearColour SolarIrradiance = ScaleColour(SolarIlluminance, Moon.SolarFluxScale);

    // Moon-centred chord parameterisation: p(s) = Perpendicular + v * s, |Perpendicular| = b (avoids catastrophic cancellation at d ≫ R).
    const Vector3f Perpendicular = ScaleVector(SubtractVector(ScaleVector(LocalDirection, std::cos(Separation)), MoonAxis), DistanceKm);

    const float ImpactSquared       = ImpactKm * ImpactKm;
    const bool  SolidHit            = ImpactKm < BodyRadius;
    const float SolidEntry          = SolidHit ? -std::sqrt(BodyRadius * BodyRadius - ImpactSquared) : 0.0f;
    const float AtmosphereHalfChord = SafeSquareRoot(TopRadius * TopRadius - ImpactSquared);

    LinearColour Scattered{};
    LinearColour ViewTransmittance{ 1.0f, 1.0f, 1.0f };

    if (LunarAtmosphere.AtmosphereEnabled && AtmosphereHalfChord > 0.0f)
    {
        const float    SegmentStart = -AtmosphereHalfChord;
        const float    SegmentEnd   = SolidHit ? SolidEntry : AtmosphereHalfChord;
        const float    SegmentKm    = std::max(0.0f, SegmentEnd - SegmentStart);
        const float    LowestRadius = SolidHit ? BodyRadius : ImpactKm;
        const uint32_t StepCount    = ResolveRadialStepCount(LunarAtmosphere, 2.0f * (TopRadius - LowestRadius), AtmosphereStepCount);

        const float     CosineScatter  = DotVector(LocalDirection, SolarAxis);
        const float     RayleighPh     = EvaluateRayleighPhaseFunction(CosineScatter);
        const float     MiePh          = EvaluateCornetteShanksPhaseFunction(CosineScatter, LunarAtmosphere.MieAsymmetry);
        constexpr float IsotropicPhase = 1.0f / (4.0f * Atmosphere::PiConstant);

        for (uint32_t Step = 0u; Step < StepCount && SegmentKm > 0.0f; ++Step)
        {
            float Lower          = 0.0f;
            float Upper          = 0.0f;
            float ChordParameter = 0.0f;
            ResolveRadialChordSegment(Step, StepCount, SegmentStart, SegmentEnd, ImpactKm, Lower, Upper, ChordParameter);
            const float             StepKm      = Upper - Lower;
            const Vector3f          SamplePoint = AddVector(Perpendicular, ScaleVector(LocalDirection, ChordParameter));
            const float             AltitudeKm  = VectorLength(SamplePoint) - BodyRadius;
            const LunarMediumSample Medium      = EvaluateLunarMedium(LunarAtmosphere, AltitudeKm);
            if (Medium.Extinction.Red + Medium.Extinction.Green + Medium.Extinction.Blue <= 1.0e-12f)
                continue;

            const LinearColour SunTransmittance  = EvaluateLunarSunTransmittance(LunarAtmosphere, SamplePoint, SolarAxis, 12u);
            const LinearColour StepTransmittance {
                std::exp(-Medium.Extinction.Red   * StepKm),
                std::exp(-Medium.Extinction.Green * StepKm),
                std::exp(-Medium.Extinction.Blue  * StepKm) };

            // Single scattering + isotropic multiple-scattering approximation (Ψ_ms ≈ 0.35 σ_s / 4π of attenuated sunlight).
            const float        SunMean = (SunTransmittance.Red + SunTransmittance.Green + SunTransmittance.Blue) * (1.0f / 3.0f);
            const LinearColour Source  {
                (Medium.RayleighPart.Red   * RayleighPh + Medium.MiePart.Red   * MiePh) * SunTransmittance.Red   + 0.35f * Medium.Scattering.Red   * IsotropicPhase * SunMean,
                (Medium.RayleighPart.Green * RayleighPh + Medium.MiePart.Green * MiePh) * SunTransmittance.Green + 0.35f * Medium.Scattering.Green * IsotropicPhase * SunMean,
                (Medium.RayleighPart.Blue  * RayleighPh + Medium.MiePart.Blue  * MiePh) * SunTransmittance.Blue  + 0.35f * Medium.Scattering.Blue  * IsotropicPhase * SunMean };

            const LinearColour StepIntegral{
                Source.Red   * (1.0f - StepTransmittance.Red)   / std::max(Medium.Extinction.Red,   1.0e-12f),
                Source.Green * (1.0f - StepTransmittance.Green) / std::max(Medium.Extinction.Green, 1.0e-12f),
                Source.Blue  * (1.0f - StepTransmittance.Blue)  / std::max(Medium.Extinction.Blue,  1.0e-12f) };

            Scattered         = AddColour(Scattered, MultiplyColour(ViewTransmittance, StepIntegral));
            ViewTransmittance = MultiplyColour(ViewTransmittance, StepTransmittance);
        }
        Scattered = MultiplyColour(Scattered, SolarIrradiance);
    }

    Result.Radiance = Scattered;
    if (SolidHit)
    {
        const Vector3f         SurfacePoint  = AddVector(Perpendicular, ScaleVector(LocalDirection, SolidEntry));
        const Vector3f         SurfaceNormal = NormalizeVector(SurfacePoint);
        const LunarOrientation Orientation   = ConstructLunarOrientation(Moon, MoonAxis);
        const LinearColour     Surface       = EvaluateLunarSurfaceRadiance(
            Moon, OptionalAtlas, Orientation, SurfaceNormal, SurfacePoint,
            ScaleVector(LocalDirection, -1.0f), SolarAxis, MoonAxis, SolarIrradiance);
        Result.Radiance             = AddColour(Result.Radiance, MultiplyColour(ViewTransmittance, Surface));
        Result.TransmittanceToStars = LinearColour{};
        Result.SolidDiscHit         = true;
    }
    else
    {
        Result.TransmittanceToStars = ViewTransmittance;
        Result.OffLimbAtmosphereHit = LunarAtmosphere.AtmosphereEnabled && AtmosphereHalfChord > 0.0f;
    }
    return Result;
}

LunarRayResult EvaluateLunarSystem(
    const CelestialSceneConfiguration& Scene,
    const LunarSurfaceAtlas*           OptionalAtlas,
    const Vector3f&                    LocalDirection,
    const Vector3f&                    SolarDirectionLocal,
    const LinearColour&                SolarIlluminance) noexcept
{
    LunarRayResult Combined  {};
    const uint32_t MoonCount = std::min(Scene.MoonCount, MaximumLunarBodyCount);

    // Far-to-near order by physical distance d = R_m / sin(α) so nearer moons occlude (and are composited over) farther ones.
    std::array<uint32_t, MaximumLunarBodyCount> Order       {};
    std::array<float, MaximumLunarBodyCount>    Distances   {};
    uint32_t                                    ActiveCount = 0u;
    for (uint32_t Slot = 0u; Slot < MoonCount; ++Slot)
    {
        if (!Scene.Moons[Slot].VisibilityEnabled)
            continue;
        Distances[ActiveCount] = EvaluateLunarDistanceKm(Scene.Moons[Slot]);
        Order[ActiveCount]     = Slot;
        ++ActiveCount;
    }
    for (uint32_t Outer = 1u; Outer < ActiveCount; ++Outer)
    {
        for (uint32_t Inner = Outer; Inner > 0u && Distances[Inner - 1u] < Distances[Inner]; --Inner)
        {
            std::swap(Distances[Inner - 1u], Distances[Inner]);
            std::swap(Order[Inner - 1u], Order[Inner]);
        }
    }

    for (uint32_t Rank = 0u; Rank < ActiveCount; ++Rank)
    {
        const LunarRayResult Single = EvaluateSingleLunarBody(
            Scene.Moons[Order[Rank]], OptionalAtlas, LocalDirection, SolarDirectionLocal, SolarIlluminance);
        Combined.NormalizedImpact = std::min(Combined.NormalizedImpact, Single.NormalizedImpact);
        if (!Single.SolidDiscHit && !Single.OffLimbAtmosphereHit)
            continue;
        Combined.Radiance             = AddColour(Single.Radiance, MultiplyColour(Single.TransmittanceToStars, Combined.Radiance));
        Combined.TransmittanceToStars = MultiplyColour(Combined.TransmittanceToStars, Single.TransmittanceToStars);
        Combined.SolidDiscHit         = Combined.SolidDiscHit || Single.SolidDiscHit;
        Combined.OffLimbAtmosphereHit = Combined.OffLimbAtmosphereHit || Single.OffLimbAtmosphereHit;
    }
    return Combined;
}

LinearColour EvaluateStarfieldGalaxy(
    const StarfieldConfiguration&  StarConfiguration,
    const Vector3f&                LocalDirection,
    float                          SequenceSeconds,
    bool                           VacuumSpaceCondition) noexcept
{
    LinearColour            Radiance   {};
    const CelestialRotation Rotation   = ConstructCelestialRotation(StarConfiguration);
    const Vector3f          Equatorial = NormalizeVector(TransformLocalToEquatorial(Rotation, LocalDirection));

    if (StarConfiguration.MilkyWayEnabled && StarConfiguration.MilkyWayRadianceScale > 0.0f)
        Radiance = AddColour(Radiance, ScaleColour(EvaluateMilkyWaySurfaceBrightness(Equatorial), 0.0042f * GalacticRadianceCalibration * StarConfiguration.MilkyWayRadianceScale));

    if (StarConfiguration.NebulaeEnabled)
        Radiance = AddColour(Radiance, ScaleColour(EvaluateDeepSkyNebulae(Equatorial), GalacticRadianceCalibration * StarConfiguration.MilkyWayRadianceScale));

    if (StarConfiguration.StarsEnabled && StarConfiguration.RadianceScale > 0.0f)
    {
        const StellarCubeIndex& Index      = RetrieveStellarCubeIndex();
        const uint32_t          Cell       = ResolveCubeCell(Equatorial);
        const uint32_t          First      = Index.CellOffsets[Cell];
        const uint32_t          Last       = Index.CellOffsets[Cell + 1u];
        const float             PixelAngle = std::max(1.0e-5f, StarConfiguration.PixelAngularSizeRadians);

        // Kolmogorov scintillation amplitude ∝ X^1.5 (Kasten-Young airmass); strictly zero in vacuum.
        float ScintillationAmplitude = 0.0f;
        if (!VacuumSpaceCondition && StarConfiguration.ScintillationStrength > 0.0f)
        {
            const float Airmass = EvaluateKastenYoungAirmass(std::asin(std::clamp(LocalDirection.YCoord, -1.0f, 1.0f)));
            ScintillationAmplitude = std::clamp(StarConfiguration.ScintillationStrength * 0.10f * std::pow(Airmass, 1.5f), 0.0f, 0.85f);
        }

        for (uint32_t Cursor = First; Cursor < Last; ++Cursor)
        {
            const ResolvedStarEntry& Star = Index.Stars[Index.CellStarIndices[Cursor]];
            if (DotVector(Star.EquatorialAxis, Equatorial) < 0.9990f)
                continue;
            const float Angle = EvaluateAngularSeparation(Star.EquatorialAxis, Equatorial);
            if (Angle > std::max(Star.HaloReachRadians, 7.0f * PixelAngle))
                continue;

            float        Flux = Star.IntegratedFlux * StarConfiguration.RadianceScale * EvaluateStellarPointSpread(Angle, PixelAngle);
            LinearColour Tint = Star.Chromaticity;
            if (ScintillationAmplitude > 0.0f)
            {
                const float Phase   = Star.ScintillationSeed * Atmosphere::TauConstant;
                const float Flicker = 0.62f * std::sin(SequenceSeconds * 13.7f + Phase * 3.0f)
                                    + 0.38f * std::sin(SequenceSeconds * 29.3f + Phase * 7.0f);
                Flux *= std::max(0.0f, 1.0f + ScintillationAmplitude * Flicker);
                // Chromatic scintillation near the limb (atmospheric dispersion separates the R/B wavefronts).
                const float Chromatic = 0.35f * ScintillationAmplitude * std::sin(SequenceSeconds * 21.1f + Phase * 5.0f);
                Tint = LinearColour{ Tint.Red * (1.0f + Chromatic), Tint.Green, Tint.Blue * (1.0f - Chromatic) };
            }
            Radiance = AddColour(Radiance, ScaleColour(Tint, Flux));
        }
    }
    return Radiance;
}

LinearColour EvaluateCelestialBackdrop(
    const CelestialSceneConfiguration& Scene,
    const LunarSurfaceAtlas*           OptionalAtlas,
    const AtmosphericLookupTables&     Tables,
    const AtmosphereUniformBlock&      Profile,
    const Vector3f&                    LocalDirection,
    float                              SequenceSeconds,
    bool                               VacuumSpaceCondition) noexcept
{
    // Terrestrial Beer-Lambert extinction T(r, μ) along the line of sight; zero below the geometric planetary limb.
    LinearColour Extinction{ 1.0f, 1.0f, 1.0f };
    if (!VacuumSpaceCondition)
    {
        const float ViewerRadiusKm = Profile.BottomRadius + Atmosphere::RetrieveViewerAltitudeKm(Profile);
        if (QueryGroundSphereIntersection(ViewerRadiusKm, LocalDirection.YCoord, Profile.BottomRadius))
            return LinearColour{};
        Extinction = SampleTransmittanceToSpace(Tables, Profile, ViewerRadiusKm, LocalDirection.YCoord);
    }

    const Vector3f     SolarAxis = NormalizeVector(Vector3f{ Profile.SolarDirection[0], Profile.SolarDirection[1], Profile.SolarDirection[2] });
    const LinearColour SolarFlux { Profile.SolarIlluminance[0], Profile.SolarIlluminance[1], Profile.SolarIlluminance[2] };

    const LunarRayResult Lunar    = EvaluateLunarSystem(Scene, OptionalAtlas, LocalDirection, SolarAxis, SolarFlux);
    LinearColour         Backdrop = Lunar.Radiance;
    if (!Lunar.SolidDiscHit)
    {
        const LinearColour Stars = EvaluateStarfieldGalaxy(Scene.Stars, LocalDirection, SequenceSeconds, VacuumSpaceCondition);
        Backdrop = AddColour(Backdrop, MultiplyColour(Lunar.TransmittanceToStars, Stars));
    }
    return MultiplyColour(Backdrop, Extinction);
}

void EncodeCelestialUniformBlock(
    const CelestialSceneConfiguration& Scene,
    float                              SequenceSeconds,
    CelestialUniformBlock&             OutUniforms) noexcept
{
    OutUniforms = CelestialUniformBlock{};
    const StarfieldConfiguration& Stars = Scene.Stars;
    Atmosphere::EncodeFloat4(OutUniforms.StarControlPack,
        Stars.CelestialRotationRadians,
        Stars.SiteLatitudeRadians,
        Stars.StarsEnabled ? Stars.RadianceScale : 0.0f,
        Stars.MilkyWayEnabled ? Stars.MilkyWayRadianceScale : 0.0f);

    const uint32_t MoonCount   = std::min(Scene.MoonCount, MaximumLunarBodyCount);
    uint32_t       PackedCount = 0u;
    for (uint32_t Slot = 0u; Slot < MoonCount; ++Slot)
    {
        const LunarBodyDescriptor& Moon = Scene.Moons[Slot];
        if (!Moon.VisibilityEnabled)
            continue;
        LunarUniformEntry&            Entry           = OutUniforms.Moons[PackedCount++];
        const Vector3f                Axis            = NormalizeVector(Moon.DirectionLocal);
        const LunarAtmosphereProfile& LunarAtmosphere = Moon.Atmosphere;
        const float                   AtmosphereGate  = LunarAtmosphere.AtmosphereEnabled ? 1.0f : 0.0f;
        Atmosphere::EncodeFloat4(Entry.DirectionRadius, Axis.XCoord, Axis.YCoord, Axis.ZCoord, Moon.AngularRadiusRadians);
        Atmosphere::EncodeFloat4(Entry.AlbedoRoughness,
            Moon.RegolithTint.Red * Moon.GeometricAlbedo, Moon.RegolithTint.Green * Moon.GeometricAlbedo,
            Moon.RegolithTint.Blue * Moon.GeometricAlbedo, Moon.RegolithRoughness);
        Atmosphere::EncodeFloat4(Entry.PhaseSpinPack,
            Moon.LommelSeeligerWeight, Moon.OppositionSurgeAmp, Moon.SpinLongitudeRadians, static_cast<float>(static_cast<uint32_t>(Moon.Preset)));
        Atmosphere::EncodeFloat4(Entry.AtmosphereGeometryPack, LunarAtmosphere.BodyRadiusKm, LunarAtmosphere.TopAltitudeKm * AtmosphereGate, LunarAtmosphere.RayleighScaleHeightKm, LunarAtmosphere.MieScaleHeightKm);
        Atmosphere::EncodeFloat4(Entry.AtmosphereRayleighAsymmetry,
            LunarAtmosphere.RayleighScatteringPerKm.Red * AtmosphereGate, LunarAtmosphere.RayleighScatteringPerKm.Green * AtmosphereGate,
            LunarAtmosphere.RayleighScatteringPerKm.Blue * AtmosphereGate, LunarAtmosphere.MieAsymmetry);
        Atmosphere::EncodeFloat4(Entry.AtmosphereMieHazePack,
            LunarAtmosphere.MieScatteringPerKm.Red * AtmosphereGate, LunarAtmosphere.MieScatteringPerKm.Green * AtmosphereGate,
            LunarAtmosphere.MieScatteringPerKm.Blue * AtmosphereGate, LunarAtmosphere.DetachedHazeAltitudeKm);
    }

    Atmosphere::EncodeFloat4(OutUniforms.OpticsCountPack,
        Stars.PixelAngularSizeRadians,
        Stars.ScintillationStrength,
        SequenceSeconds,
        static_cast<float>(PackedCount));
}

} // namespace Frontier
