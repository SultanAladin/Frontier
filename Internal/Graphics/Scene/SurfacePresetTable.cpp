/*==============================================================================================================================================
                                                           SURFACEPRESETTABLE.CPP
==============================================================================================================================================*/
// 🧩 The resolved channel defaults for the floor plus thirteen head presets. Every value here traces to Documentation/PLAN-MaterialPresets.md ss3/ss4
//    (which channels a family authors and which stay hidden) with the numeric conventions taken from Filament / glTF: Reflectance 0.5 is the canonical
//    4% dielectric f0, metals set Metallic 1 so f0 becomes BaseColour, and Roughness is perceptual everywhere.
//
//    Each record also carries its feature mask, which is what keeps the shader a single shared code path instead of a switch arm per family: the
//    twelve presets are masks plus channel values over one BRDF core, and Composite is that same core with every bit set. That is strictly LESS
//    shader code than a per-family branch, which is what makes the thirteenth head nearly free.

#include "Graphics/Scene/SurfacePresetTable.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// Assign one record's colour + emissive slots. Keeps the table below readable as a channel sheet rather than 14 blocks of index arithmetic.
void SetColours(SurfacePresetParameters& Preset,
                float ColourRed, float ColourGreen, float ColourBlue, float ColourAlpha,
                float EmissiveRed = 0.0f, float EmissiveGreen = 0.0f, float EmissiveBlue = 0.0f, float EmissiveStrength = 0.0f)
{
    Preset.BaseColour[0] = ColourRed;    Preset.BaseColour[1] = ColourGreen;
    Preset.BaseColour[2] = ColourBlue;   Preset.BaseColour[3] = ColourAlpha;
    Preset.EmissiveColour[0] = EmissiveRed;  Preset.EmissiveColour[1] = EmissiveGreen;
    Preset.EmissiveColour[2] = EmissiveBlue; Preset.EmissiveColour[3] = EmissiveStrength;
}

// The two lobes almost every opaque family runs. Spelled out once so the per-preset lines below only state what is UNUSUAL about that family.
constexpr uint32_t OpaqueBaseMask = SurfaceFeatureDiffuse | SurfaceFeatureSpecular;

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void BuildSurfacePresetTable(SurfacePresetParameters* OutTable)
{
    if (OutTable == nullptr)
        return;

    // Every entry starts from the struct's own defaults, so a family only overrides the channels ss4 marks as authored for it.
    for (uint32_t PresetIterator = 0; PresetIterator < SurfacePresetCount; ++PresetIterator)
        OutTable[PresetIterator] = SurfacePresetParameters{};

    // -- 0: FLOOR. Flat near-white dielectric. Deliberately a touch under 1.0 so the tonemap's shoulder is not already engaged by the backdrop, and
    //    rough enough that the ground carries no highlight to compete with the heads. ----------------------------------------------------------------
    SurfacePresetParameters& Floor = OutTable[SurfacePresetFloor];
    SetColours(Floor, 0.88f, 0.88f, 0.90f, 1.0f);
    Floor.Roughness      = 0.85f;
    Floor.Reflectance    = 0.35f;
    Floor.ShadingModelId = (uint32_t)SurfaceShadingModel::Standard;
    Floor.FeatureMask    = OpaqueBaseMask;

    // -- 1: PLASTIC (Standard). ss3 row 1: Roughness ~.4, Metallic 0, Reflectance ~.5. ------------------------------------------------------------
    SurfacePresetParameters& Plastic = OutTable[SurfacePresetPlastic];
    SetColours(Plastic, 0.82f, 0.18f, 0.16f, 1.0f);
    Plastic.Roughness      = 0.40f;
    Plastic.Reflectance    = 0.50f;
    Plastic.ShadingModelId = (uint32_t)SurfaceShadingModel::Standard;
    Plastic.FeatureMask    = OpaqueBaseMask;

    // -- 2: METAL (Metal). No diffuse lobe at all — f0 IS the base colour. Gold-ish so the chromatic f0 is obvious next to Chrome. ---------------
    SurfacePresetParameters& Metal = OutTable[SurfacePresetMetal];
    SetColours(Metal, 0.94f, 0.72f, 0.28f, 1.0f);
    Metal.Roughness      = 0.28f;
    Metal.Metallic       = 1.0f;
    Metal.ShadingModelId = (uint32_t)SurfaceShadingModel::Metal;
    Metal.FeatureMask    = SurfaceFeatureSpecular;   // ss4: Metal authors no diffuse

    // -- 3: CHROME (Metal, polished variation — ss3 row 2 "Polished r=.05"). The roughness floor in the shader is what keeps this from going NaN. ---
    SurfacePresetParameters& Chrome = OutTable[SurfacePresetChrome];
    SetColours(Chrome, 0.95f, 0.96f, 0.97f, 1.0f);
    Chrome.Roughness      = 0.05f;
    Chrome.Metallic       = 1.0f;
    Chrome.ShadingModelId = (uint32_t)SurfaceShadingModel::Metal;
    Chrome.FeatureMask    = SurfaceFeatureSpecular;

    // -- 4: CLEARCOAT (Clearcoat, car-paint variation): a metallic-leaning base under a tight coat lobe. Two lobes at different widths is the whole
    //    read of this material, which is exactly what LDR clipping destroys if the tonemap is skipped. ---------------------------------------------
    SurfacePresetParameters& Clearcoat = OutTable[SurfacePresetClearcoat];
    SetColours(Clearcoat, 0.10f, 0.14f, 0.55f, 1.0f);
    Clearcoat.Roughness      = 0.45f;
    Clearcoat.Metallic       = 0.0f;
    Clearcoat.Reflectance    = 0.55f;
    Clearcoat.CoatWeight     = 1.0f;
    Clearcoat.CoatRoughness  = 0.05f;
    Clearcoat.ShadingModelId = (uint32_t)SurfaceShadingModel::Clearcoat;
    Clearcoat.FeatureMask    = OpaqueBaseMask | SurfaceFeatureCoat;

    // -- 5: FABRIC (Cloth / Charlie, velvet variation): rough diffuse plus a bright sheen rim. Sheen replaces Fresnel rather than adding over it. ---
    SurfacePresetParameters& Fabric = OutTable[SurfacePresetFabric];
    SetColours(Fabric, 0.35f, 0.06f, 0.12f, 1.0f);
    Fabric.Roughness      = 0.90f;
    Fabric.SheenColour[0] = 0.95f;  Fabric.SheenColour[1] = 0.55f;  Fabric.SheenColour[2] = 0.65f;
    Fabric.SheenRoughness = 0.30f;
    Fabric.ShadingModelId = (uint32_t)SurfaceShadingModel::Cloth;
    Fabric.FeatureMask    = SurfaceFeatureDiffuse | SurfaceFeatureSheen;   // cloth drops the standard GGX lobe

    // -- 6: GLASS (approximated). No refraction this slice: alpha-blend plus a strong Fresnel rim. Khronos is explicit that with no environment map
    //    and no scene copy this reads better than IBL-only refraction. Alpha is the channel doing the work. -----------------------------------------
    SurfacePresetParameters& Glass = OutTable[SurfacePresetGlass];
    SetColours(Glass, 0.85f, 0.93f, 0.92f, 0.25f);
    Glass.Roughness      = 0.04f;
    Glass.Reflectance    = 0.50f;
    // 📝 The one preset that authors a real refractive index: 1.52 is soda-lime glass, which ReflectanceForRefractionIndex resolves to f0 ≈ 4.3% —
    //    a touch above the canonical 4% the Reflectance route gives, and the reason the rim reads slightly stronger than before on this head alone.
    //    Every other preset leaves RefractionIndex at 0 and therefore keeps the Reflectance route byte-identically.
    Glass.RefractionIndex     = 1.52f;
    Glass.TransmissionWeight  = 1.0f;
    Glass.ShadingModelId = (uint32_t)SurfaceShadingModel::Glass;
    Glass.FeatureMask    = SurfaceFeatureSpecular | SurfaceFeatureTransmission;

    // -- 7: CERAMIC (Standard, glazed variation — ss3 row 7: light base, low roughness, optional thin coat). ------------------------------------
    SurfacePresetParameters& Ceramic = OutTable[SurfacePresetCeramic];
    SetColours(Ceramic, 0.92f, 0.90f, 0.84f, 1.0f);
    Ceramic.Roughness      = 0.15f;
    Ceramic.Reflectance    = 0.60f;
    Ceramic.CoatWeight     = 0.35f;
    Ceramic.CoatRoughness  = 0.08f;
    Ceramic.ShadingModelId = (uint32_t)SurfaceShadingModel::Clearcoat;   // glazed = Standard + a thin coat, per ss3's "COAT (opt)"
    Ceramic.FeatureMask    = OpaqueBaseMask | SurfaceFeatureCoat;

    // -- 8: RUBBER (Standard — ss3 row 8: dark base, Roughness ~.8, low reflectance). The broad low-contrast falloff here is the entry most likely
    //    to show 8-bit banding; that is a target-depth artefact, not a shading bug. -------------------------------------------------------------
    SurfacePresetParameters& Rubber = OutTable[SurfacePresetRubber];
    SetColours(Rubber, 0.06f, 0.06f, 0.07f, 1.0f);
    Rubber.Roughness      = 0.80f;
    Rubber.Reflectance    = 0.25f;
    Rubber.ShadingModelId = (uint32_t)SurfaceShadingModel::Standard;
    Rubber.FeatureMask    = OpaqueBaseMask;

    // -- 9: SKIN (Subsurface, approximated as warm-tinted wrap diffuse). Reflectance 0.35 -> ~2.8% f0, Filament's measured value for skin. The wrap
    //    term's trap (never re-multiply the diffuse by NoL) lives in the shader, not here. ------------------------------------------------------
    SurfacePresetParameters& Skin = OutTable[SurfacePresetSkin];
    SetColours(Skin, 0.82f, 0.56f, 0.47f, 1.0f);
    Skin.Roughness      = 0.42f;
    Skin.Reflectance    = 0.35f;
    Skin.ShadingModelId = (uint32_t)SurfaceShadingModel::Subsurface;
    Skin.FeatureMask    = OpaqueBaseMask | SurfaceFeatureSubsurface;

    // -- 10: IRIDESCENT (approximated by a NdotV hue ramp that REPLACES Fresnel). glTF defaults: IOR 1.3, thickness 100..400 nm. ---------------
    SurfacePresetParameters& Iridescent = OutTable[SurfacePresetIridescent];
    SetColours(Iridescent, 0.12f, 0.12f, 0.14f, 1.0f);
    Iridescent.Roughness            = 0.18f;
    Iridescent.Metallic             = 1.0f;
    Iridescent.IridescenceIor       = 1.30f;
    Iridescent.IridescenceThickness = 380.0f;
    Iridescent.ShadingModelId       = (uint32_t)SurfaceShadingModel::Iridescent;
    Iridescent.FeatureMask          = SurfaceFeatureSpecular | SurfaceFeatureIridescence;

    // -- 11: EMISSIVE / LED. Strength deliberately above 1 so the tonemap's desaturating shoulder gives a near-white distinguishable from a diffuse
    //    white at 1.0. That rolloff is the ONLY cue available here: with no HDR intermediate and no bloom an LED cannot outrun white. ------------
    SurfacePresetParameters& Emissive = OutTable[SurfacePresetEmissive];
    SetColours(Emissive, 0.02f, 0.02f, 0.02f, 1.0f,  0.20f, 0.95f, 0.55f, 4.0f);
    Emissive.Roughness      = 0.60f;
    Emissive.ShadingModelId = (uint32_t)SurfaceShadingModel::Emissive;
    Emissive.FeatureMask    = SurfaceFeatureEmissive;

    // -- 12: MATCAP (non-PBR). No light loop and no tonemap — a matcap texture is already display-referred, so running it through either would
    //    double-correct it. Until a sphere texture is bound the shader synthesizes a studio ramp from the same lookup. --------------------------
    SurfacePresetParameters& Matcap = OutTable[SurfacePresetMatcap];
    SetColours(Matcap, 0.72f, 0.74f, 0.78f, 1.0f);
    Matcap.ShadingModelId = (uint32_t)SurfaceShadingModel::Matcap;
    Matcap.FeatureMask    = SurfaceFeatureMatcap;

    // -- 13: COMPOSITE. Every channel authored to a visible mid value so that toggling any single lobe at runtime produces an obvious change. Its
    //    FeatureMask here is only the STARTING state — the shade pass substitutes a push-constant mask for this record, which is what lets the lobes
    //    be switched live without touching the uploaded table. ------------------------------------------------------------------------------------
    SurfacePresetParameters& Composite = OutTable[SurfacePresetComposite];
    SetColours(Composite, 0.55f, 0.42f, 0.72f, 1.0f,  0.35f, 0.20f, 0.55f, 1.2f);
    Composite.Roughness            = 0.35f;
    Composite.Metallic             = 0.0f;
    Composite.Reflectance          = 0.55f;
    Composite.SheenColour[0]       = 0.70f;  Composite.SheenColour[1] = 0.60f;  Composite.SheenColour[2] = 0.85f;
    Composite.SheenRoughness       = 0.35f;
    Composite.CoatWeight           = 0.80f;
    Composite.CoatRoughness        = 0.07f;
    Composite.IridescenceIor       = 1.32f;
    Composite.IridescenceThickness = 340.0f;
    Composite.ShadingModelId       = (uint32_t)SurfaceShadingModel::Composite;
    Composite.FeatureMask          = OpaqueBaseMask | SurfaceFeatureCoat | SurfaceFeatureSheen | SurfaceFeatureEmissive;
}

const char* SurfacePresetName(uint32_t MaterialId)
{
    switch (MaterialId)
    {
        case SurfacePresetFloor:      return "floor";
        case SurfacePresetPlastic:    return "plastic";
        case SurfacePresetMetal:      return "metal";
        case SurfacePresetChrome:     return "chrome";
        case SurfacePresetClearcoat:  return "clearcoat";
        case SurfacePresetFabric:     return "fabric";
        case SurfacePresetGlass:      return "glass";
        case SurfacePresetCeramic:    return "ceramic";
        case SurfacePresetRubber:     return "rubber";
        case SurfacePresetSkin:       return "skin";
        case SurfacePresetIridescent: return "iridescent";
        case SurfacePresetEmissive:   return "emissive";
        case SurfacePresetMatcap:     return "matcap";
        case SurfacePresetComposite:  return "composite";
        default:                      return "unknown";
    }
}

const char* SurfaceFeatureName(uint32_t FeatureBit)
{
    switch (FeatureBit)
    {
        case SurfaceFeatureDiffuse:      return "diffuse";
        case SurfaceFeatureSpecular:     return "specular";
        case SurfaceFeatureCoat:         return "coat";
        case SurfaceFeatureSheen:        return "sheen";
        case SurfaceFeatureIridescence:  return "iridescence";
        case SurfaceFeatureEmissive:     return "emissive";
        case SurfaceFeatureSubsurface:   return "subsurface";
        case SurfaceFeatureTransmission: return "transmission";
        case SurfaceFeatureMatcap:       return "matcap";
        default:                         return "unknown";
    }
}

} // namespace Frontier
