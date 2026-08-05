/*==============================================================================================================================================
                                                            SURFACEPRESETTABLE.H
==============================================================================================================================================*/
// 🧩 The resolved material table the surface shade reads: one SurfacePresetParameters record per MaterialId, uploaded once as a std140 uniform block
//    and indexed by the shade pass through the visibility buffer's partition ordinal (partition -> instance -> MaterialId -> this table). A record is
//    the FLATTENED form of a Documentation/PLAN-MaterialPresets.md preset — a shading-model id, a feature mask, and the channel defaults that model
//    authors — so the shader never knows the name "Fabric", only Cloth plus a sheen channel. This is the load-time-flatten half of that plan's two
//    resolution paths; the authoring-time SurfacePreset record and its library land later at M7.
//
//    The table holds the floor plus thirteen heads: twelve fixed presets and one COMPOSITE record that exposes every channel at once. Composite is
//    what makes the feature mask load-bearing rather than decorative — its lobes are gated by a PUSH-CONSTANT mask instead of the record's own, so a
//    runtime toggle re-shades without re-uploading the table. Anisotropic is deliberately ABSENT: the stride-32 vertex format carries no tangent
//    (position @0, normal @12, texcoord @24), and an anisotropic highlight needs a per-pixel tangent frame to point the lobe along.

#pragma once
#ifndef FRONTIER_GRAPHICS_SCENE_SURFACEPRESETTABLE_H
#define FRONTIER_GRAPHICS_SCENE_SURFACEPRESETTABLE_H

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The shading model a record resolves to — the ONE value the uber-shader's switch dispatches on, mirroring the 4-bit model id of
//    PLAN-UnifiedMaterialModels.md. Preset-to-model is many-to-one by design: Plastic, Ceramic and Rubber all ride Standard and differ only in
//    channel defaults, exactly as that plan's ss3 table specifies. Values are explicit because the GLSL mirror in SurfaceShade.comp must match.
enum class SurfaceShadingModel : uint32_t
{
    Standard    = 0,   // Lambert diffuse + GGX specular dielectric — Plastic, Ceramic, Rubber, floor
    Metal       = 1,   // No diffuse; chromatic f0 = BaseColour (Metal, Chrome)
    Clearcoat   = 2,   // Standard base + a second Kelemen-visibility GGX lobe
    Cloth       = 3,   // Charlie sheen, Fresnel-free (Fabric)
    Glass       = 4,   // Approximated: alpha-blend + Fresnel rim (no refraction this slice)
    Subsurface  = 5,   // Approximated: warm-tinted wrap diffuse (Skin)
    Iridescent  = 6,   // Approximated: NdotV ramp REPLACING the Fresnel term
    Emissive    = 7,   // Emissive-dominant (LED)
    Matcap      = 8,   // Non-PBR: view-space normal -> sphere-texture UV; no light loop, no tonemap
    Composite   = 9    // Every channel live at once; lobes gated by the push-constant mask
};

// 📝 Independent lobe gates. A preset carries a FIXED mask; the Composite record's mask is overridden per-frame from a push constant so lobes toggle
//    at runtime with no table re-upload. Gating by mask rather than by zeroing a weight is deliberate — Filament notes a clear-coat lobe "effectively
//    doubles the cost of specular computations", so a zeroed weight still pays for the branch it was meant to avoid.
enum SurfaceFeatureBit : uint32_t
{
    SurfaceFeatureDiffuse      = 1u << 0,   // Lambert diffuse lobe
    SurfaceFeatureSpecular     = 1u << 1,   // GGX specular lobe
    SurfaceFeatureCoat         = 1u << 2,   // Second (clear-coat) specular lobe
    SurfaceFeatureSheen        = 1u << 3,   // Charlie sheen rim
    SurfaceFeatureIridescence  = 1u << 4,   // Thin-film ramp replacing Fresnel
    SurfaceFeatureEmissive     = 1u << 5,   // Emissive contribution
    SurfaceFeatureSubsurface   = 1u << 6,   // Wrap-diffuse scatter
    SurfaceFeatureTransmission = 1u << 7,   // Fresnel-rim glass approximation
    SurfaceFeatureMatcap       = 1u << 8    // Sphere-texture lookup (bypasses the light loop)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                           CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 MaterialId assignments. 0 is the floor (flat white Standard); 1..13 are the ring heads in placement order, so the inner ring reads
//    Plastic -> Fabric and the outer ring Glass -> Composite. Composite is last so it is the visually distinct "everything on" head.
constexpr uint32_t SurfacePresetFloor      = 0u;
constexpr uint32_t SurfacePresetPlastic    = 1u;
constexpr uint32_t SurfacePresetMetal      = 2u;
constexpr uint32_t SurfacePresetChrome     = 3u;
constexpr uint32_t SurfacePresetClearcoat  = 4u;
constexpr uint32_t SurfacePresetFabric     = 5u;
constexpr uint32_t SurfacePresetGlass      = 6u;
constexpr uint32_t SurfacePresetCeramic    = 7u;
constexpr uint32_t SurfacePresetRubber     = 8u;
constexpr uint32_t SurfacePresetSkin       = 9u;
constexpr uint32_t SurfacePresetIridescent = 10u;
constexpr uint32_t SurfacePresetEmissive   = 11u;
constexpr uint32_t SurfacePresetMatcap     = 12u;
constexpr uint32_t SurfacePresetComposite  = 13u;

// Live entries in the table (floor + 13 heads). The shade pass clamps its lookup to this so a stray MaterialId cannot read past the block.
constexpr uint32_t SurfacePresetCount = 14u;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One flattened preset: 112 bytes as seven 16-byte std140 slots, byte-compatible with the SurfacePreset block in SurfaceShade.frag. Every member
//    sits on a 16-byte boundary, so std140 inserts no hidden padding and the C++ array uploads straight into the uniform block.
//
//    Channel semantics follow Filament / glTF rather than being invented here:
//      Reflectance -> dielectric f0 via f0 = 0.16 * Reflectance^2; for metals f0 = BaseColour instead (Metallic selects between them).
//      Roughness   -> PERCEPTUAL; the shader squares it to alpha and clamps to >= 0.089, without which a mirror-smooth Chrome yields NaN / fireflies.
//      SheenColour -> replaces the Fresnel term outright in the Charlie cloth lobe (Filament removes Fresnel from cloth entirely).
//      Emissive    -> linear RGB intensity; .w is a strength multiplier. On this LDR target values above 1 cannot outrun white (see the shade pass).
//      RefractionIndex -> KHR_ior. <= 1.0 means UNAUTHORED, which keeps the legacy Reflectance route to f0; above 1.0 the shade derives f0 from the
//                         index instead (f0 = ((n-1)/(n+1))^2). Dielectric-only — the derivation assumes κ = 0, so it never feeds a conductor's f0.
//      AmbientOcclusion -> scales the INDIRECT fill only. The direct sun's occlusion is the area-sampled shadow ray's job; applying AO to it as well
//                         double-darkens every contact region, by a term carrying no directional information.
struct SurfacePresetParameters
{
    float    BaseColour[4]        = { 1.0f, 1.0f, 1.0f, 1.0f };   // [-] - linear RGB; .w = alpha (Glass leans on it)
    float    EmissiveColour[4]    = { 0.0f, 0.0f, 0.0f, 0.0f };   // [-] - linear RGB intensity; .w = strength multiplier
    float    Roughness            = 0.5f;                          // [-] - perceptual; squared to alpha in the shader
    float    Metallic             = 0.0f;                          // [-] - 0 dielectric, 1 conductor (prefer the extremes)
    float    Reflectance          = 0.5f;                          // [-] - dielectric f0 control; 0.5 -> the canonical 4%
    float    ReflectancePadding   = 0.0f;                          // [-] - pad to the 16-byte slot boundary
    float    SheenColour[3]       = { 0.0f, 0.0f, 0.0f };          // [-] - Charlie sheen tint (Fabric authors all four of this slot)
    float    SheenRoughness       = 0.3f;                          // [-] - sheen lobe width
    float    CoatWeight           = 0.0f;                          // [-] - clear-coat lobe strength
    float    CoatRoughness        = 0.1f;                          // [-] - clear-coat lobe width
    float    IridescenceIor       = 1.3f;                          // [-] - thin-film IOR (glTF default)
    float    IridescenceThickness = 400.0f;                        // [-] - film thickness in nm (glTF 100..400 range)
    float    TransmissionWeight   = 0.0f;                          // [-] - KHR_transmission factor; 0 opaque (reserved: the Glass path still rides BaseColour.w)
    float    RefractionIndex      = 0.0f;                          // [-] - KHR_ior; <= 1.0 means UNAUTHORED and keeps the Reflectance route to f0
    float    AmbientOcclusion     = 1.0f;                          // [-] - authored AO; 1 = unoccluded. Scales the INDIRECT fill only, never direct light
    float    ScatterPadding       = 0.0f;                          // [-] - pad to the 16-byte slot boundary
    uint32_t ShadingModelId       = 0u;                            // [-] - SurfaceShadingModel; the uber-shader's switch value
    uint32_t FeatureMask          = 0u;                            // [-] - SurfaceFeatureBit set (overridden for Composite)
    uint32_t TablePadding[2]      = { 0u, 0u };                    // [-] - std140 tail pad to a 16-byte boundary
};

// 🔴 The GLSL mirror (SurfacePreset in SurfaceShade.frag) byte-matches this struct with NO DIAGNOSTIC when it drifts: a stale copy still compiles and
//    still validates, it merely strides by the wrong size so material N reads the tail of material N-1. This assert is the only thing that catches it.
static_assert(sizeof(SurfacePresetParameters) == 112, "SurfacePresetParameters must stay seven 16-byte std140 slots — update SurfaceShade.frag's SurfacePreset in the SAME edit.");

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Fill OutTable[0 .. SurfacePresetCount-1] with the resolved floor + thirteen head presets, channel defaults per PLAN-MaterialPresets.md ss3/ss4.
// OutTable must have room for SurfacePresetCount entries. Pure data — no device work; the caller uploads the block.
void BuildSurfacePresetTable(SurfacePresetParameters* OutTable);

// The human-readable family name for a MaterialId (for log lines / debug overlays). Returns a stable literal; "unknown" past the table.
const char* SurfacePresetName(uint32_t MaterialId);

// The feature-bit name for a single SurfaceFeatureBit (for the runtime Composite toggle's log line). Returns a stable literal.
const char* SurfaceFeatureName(uint32_t FeatureBit);

} // namespace Frontier

#endif
