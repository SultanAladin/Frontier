/*==============================================================================================================================================
                                                            SURFACEBAKECONTRACT.H
==============================================================================================================================================*/
// 🧩 The surface-map bake CONTRACT the GPU dispatch shares with its callers: which map to produce (SurfaceMapIdentity), the finished
//    R8G8B8A8 image container (BakedImageBuffer), and the trace + shaping controls read off the selected set's profile
//    (SurfaceBakeParameters). The CPU encoders that turn a rasterized SurfaceSampleField into each map are unported this slice — only the
//    data contract lands here, so EvaluateSurfaceMapGpu and any future encoder share one identity enum + one parameter struct.

#pragma once
#ifndef FRONTIER_AUTHORING_BAKING_SURFACEBAKECONTRACT_H
#define FRONTIER_AUTHORING_BAKING_SURFACEBAKECONTRACT_H

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS / ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which surface map an encode produces. Dispatched from the pressed map's catalogue identity. The first six are the cage-independent
//    standard maps; the trailing six are the low-poly-only procedural masks (bevel-rounded normal, the four curvature remaps, and the
//    up-facing dust mask). Cavity / Convexity / Concavity / Pointiness all share one gather on the CPU encoder side.
enum class SurfaceMapIdentity
{
    WorldNormal,
    TangentNormal,
    BentNormal,
    Position,
    Thickness,
    Curvature,
    Bevel,
    Cavity,
    Convexity,
    Concavity,
    Pointiness,
    Dust,
    AmbientOcclusion,   // 📝 hemisphere-traced occlusion scalar (1 = open, 0 = enclosed)
    Height,             // 📝 world extent along one axis, normalized to greyscale
    IslandIdentity,     // 📝 flat hue per connected UV island
    Wireframe,          // 📝 bright triangle edges over a dark interior
    MaterialIdentity,   // 📝 flat hue per per-face material index
    VertexColour        // 📝 barycentric-interpolated authored vertex colour
};

// 📝 The finished map: a square edge in texels and its row-major R8G8B8A8 pixels (Edge² × 4 bytes). Uploaded straight into the
//    preview texture — no post filtering this slice.
struct BakedImageBuffer
{
    uint32_t             Edge = 0;
    std::vector<uint8_t> Pixels;
};

// 📝 Trace + shaping controls read off the selected set's profile. SecondaryRays / SpreadAngle / OcclusionMaximum drive both the
//    bent-normal hemisphere and the thickness inward-ray fan. PositionNormalize picks the position map's normalization volume
//    (false = per-axis bounding box, true = uniform bounding sphere). SamplingRadius widens the curvature neighbourhood, Contrast
//    scales the curvature signal about mid-grey, and AutoTonemapEnabled remaps the curvature range to fill [0,1] before contrast.
//    Fields a given encoder does not read are simply ignored by it.
struct SurfaceBakeParameters
{
    int   SecondaryRays      = 64;
    int   SpreadAngle        = 90;
    float OcclusionMaximum   = 1.0f;
    bool  PositionNormalize  = false;
    float SamplingRadius     = 1.0f;
    float Contrast           = 1.0f;
    bool  AutoTonemapEnabled = true;

    // 📝 Procedural-map controls. Bevel widens the cone-trace that averages nearby surface normals; the dust triple shapes the up-facing
    //    normal mask; InvertMaskEnabled flips the cavity / dust masks (dark ↔ bright). Encoders ignore the fields they do not read.
    float BevelRadius        = 0.05f;   // [cm] - bevel cone-trace reach
    int   BevelSamples       = 16;      // [-]  - bevel cone-trace fan count
    float DustUpBias         = 0.65f;   // [-]  - up-normal threshold before dust settles
    float DustFalloff        = 0.55f;   // [-]  - dust edge falloff band width (0-1)
    float DustPower          = 1.4f;    // [-]  - dust accumulation curve exponent
    bool  InvertMaskEnabled  = false;   // [-]  - invert the cavity / dust mask

    // 📝 Ambient-occlusion + height + wireframe + vertex-colour controls (the six new maps read these; every other encoder ignores
    //    them). AO reuses the shared SecondaryRays / SpreadAngle / OcclusionMaximum trace params and adds a near-range floor + two
    //    toggles; height picks its axis + normalization; wireframe its line width; vertex colour its gamma; material / island read none.
    float OcclusionMinimum     = 0.0f;    // [cm] - AO near-range floor: hits closer than this do not occlude (skips contact acne)
    bool  SelfOcclusionEnabled = true;    // [-]  - AO counts the texel's own covering triangle as an occluder
    bool  GroundPlaneEnabled   = false;   // [-]  - AO adds a virtual floor at the specimen's minimum-Y bound
    bool  HeightNormalizeEnabled = true;  // [-]  - height fills [0,1] across the axis extent (else raw position × HeightScale)
    float HeightScale          = 1.0f;    // [-]  - height multiplier when normalization is off
    int   HeightAxis           = 1;       // [-]  - height axis: 0 = X, 1 = Y, 2 = Z
    float WireWidth            = 1.5f;    // [texel] - wireframe line half-width at the triangle edges
    int   IdentitySource       = 0;       // [-]  - material-identity hue source (0 = per-face material index)
    float GammaCurve           = 1.0f;    // [-]  - vertex-colour output gamma (pow exponent, 1 = linear pass-through)
};

} // namespace Frontier

#endif
