/*====================================================================================================================================
                                                       LAYERKINDS.JS
====================================================================================================================================*/
// 🧩 What a layer IS — paint / fill / material / generator — plus the material presets and generator recipes

import { CHANNEL_ORDER } from "./ChannelSet.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                       LAYER KINDS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 The kind is not a label, it is a CAPABILITY. Only a `paint` layer accepts brush strokes; the other
//    three are generated or flooded wholesale. This has to be enforced in the stroke path rather than
//    merely shown in the UI, because the failure is otherwise silent: a stroke aimed at a material layer
//    would be swallowed with no dab, no error and no clue, which reads as "painting randomly stops
//    working" the moment the user selects the wrong row.
export const LAYER_KINDS = {
    paint: {
        Label:    "Paint",
        Tint:     "#f97316",
        Paintable: true,
        // A paint layer starts EMPTY and waits for a stroke. Flooding it would defeat the point: the
        // whole surface would already be covered and there would be nothing to reveal by painting.
        Flooded:  false,
        Summary:  "Hand-painted. Accepts brush strokes."
    },
    fill: {
        Label:    "Fill",
        Tint:     "#3b82f6",
        // 📝 A fill is the user's own custom flat layer — the thing they create when they want a solid
        //    colour or a uniform roughness across the whole surface. It is not paintable; its content is
        //    entirely its authored channel values.
        Paintable: false,
        Flooded:  true,
        Summary:  "Uniform authored values across the whole surface."
    },
    material: {
        Label:    "Material",
        Tint:     "#8b5cf6",
        Paintable: false,
        Flooded:  true,
        Summary:  "A PBR preset flooded over the surface."
    },
    generator: {
        Label:    "Generator",
        Tint:     "#10b981",
        Paintable: false,
        // 🔴 NOT flooded. A generator's own pass writes every texel it wants, including its own coverage,
        //    so flooding first would lay a flat colour under the pattern and the generator's alpha would
        //    then be indistinguishable from full coverage — the mottling would vanish into a solid fill.
        Flooded:  false,
        Summary:  "Procedural pattern evaluated over the UV atlas."
    }
};

export const LAYER_KIND_ORDER = ["paint", "fill", "material", "generator"];

export const IsPaintable = (Kind) => Boolean(LAYER_KINDS[Kind]?.Paintable);
export const KindTint    = (Kind) => LAYER_KINDS[Kind]?.Tint  ?? "#8a8a8a";
export const KindLabel   = (Kind) => LAYER_KINDS[Kind]?.Label ?? String(Kind);

//------------------------------------------------------------------------------------------------------------------------
//                                                      CHANNEL MODES
//------------------------------------------------------------------------------------------------------------------------

// The per-channel source, mirroring TexturePaintPropertiesR2's Value / Texture / Generator segment.
//
// 📝 "Texture" means PAINTED BY HAND here, not loaded from disk. The engine's storage for a channel IS
//    the painted atlas, so a hand-painted channel and a texture-mapped channel are the same thing from
//    the compositor's point of view; there is no texture library to assign from.
export const CHANNEL_MODES = ["Value", "Texture", "Generator"];

export const CHANNEL_MODE_NOTE = {
    Value:     "One authored value across the whole layer.",
    Texture:   "Painted by hand. Storage is allocated on the first stroke.",
    Generator: "Driven by this layer's procedural pass."
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   MATERIAL PRESETS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 Values are LINEAR [0..1], not sRGB hex. The shader works in linear space and tone-maps at the end,
//    so pasting an 8-bit hex triple straight in here would wash every preset out by roughly a 2.2 power.
//
// 📝 Metallic is deliberately 0 or 1 and never in between. A partially metallic surface is not a real
//    material — the channel selects between two different BRDF interpretations, and mid values only make
//    sense as a blend across a boundary within one texel.
export const MATERIAL_PRESETS = {
    plastic: {
        Label:  "Plastic — Red",
        Tint:   "#c0392b",
        Values: { baseColour: [0.52, 0.06, 0.04], metallic: 0.0, roughness: 0.34,
                  emission: [0, 0, 0], height: 0.5 },
        Channels: ["baseColour", "metallic", "roughness"]
    },
    metal: {
        Label:  "Metal — Brushed Steel",
        Tint:   "#95a5a6",
        // Brushed steel is a bright dielectric-looking grey in linear terms; a proper metal takes its
        // base colour as its reflectance, so this triple IS the specular colour.
        Values: { baseColour: [0.56, 0.57, 0.58], metallic: 1.0, roughness: 0.28,
                  emission: [0, 0, 0], height: 0.5 },
        Channels: ["baseColour", "metallic", "roughness"]
    },
    ceramic: {
        Label:  "Ceramic — Glazed White",
        Tint:   "#ecf0f1",
        // A glaze is very smooth but NOT a mirror; 0.08 keeps a tight highlight without hitting the
        // roughness floor the shader clamps at.
        Values: { baseColour: [0.86, 0.85, 0.82], metallic: 0.0, roughness: 0.09,
                  emission: [0, 0, 0], height: 0.5 },
        Channels: ["baseColour", "metallic", "roughness"]
    }
};

export const MATERIAL_ORDER = ["plastic", "metal", "ceramic"];

//------------------------------------------------------------------------------------------------------------------------
//                                                  GENERATOR RECIPES
//------------------------------------------------------------------------------------------------------------------------

// Each generator declares which channels it drives and the defaults for the four shared parameters.
//
// 🔴 `Drives` is load-bearing: a generator writes ONLY these channels, and the atlases it does not drive
//    stay transparent so the layers beneath show through. A rust generator that also wrote height and
//    emissive would silently flatten a height pass underneath it and make the model glow.
export const GENERATOR_RECIPES = {
    rust: {
        Label:  "Rust",
        Tint:   "#b7410e",
        Drives: ["baseColour", "roughness", "height"],
        // Rust is patchy, high-contrast and rough. It eats the metal underneath, so it drives colour and
        // roughness together — rust that stayed shiny would read as painted-on brown, not corrosion.
        Params: { Scale: 0.70, Contrast: 0.82, Amount: 0.55, Seed: 24 },
        Palette: { Low: [0.30, 0.12, 0.04], High: [0.62, 0.28, 0.09],
                   RoughLow: 0.55, RoughHigh: 0.95 }
    },
    noise: {
        Label:  "Noise",
        Tint:   "#7f8c8d",
        Drives: ["baseColour", "roughness"],
        Params: { Scale: 0.45, Contrast: 0.50, Amount: 1.00, Seed: 7 },
        Palette: { Low: [0.14, 0.14, 0.16], High: [0.78, 0.78, 0.80],
                   RoughLow: 0.25, RoughHigh: 0.85 }
    },
    scratches: {
        Label:  "Scratches",
        Tint:   "#bdc3c7",
        // Scratches cut the surface, so they drive HEIGHT as well — that is what makes the derived normal
        // catch the light along each groove instead of just tinting it.
        Drives: ["baseColour", "roughness", "height"],
        Params: { Scale: 0.85, Contrast: 0.90, Amount: 0.40, Seed: 91 },
        Palette: { Low: [0.72, 0.73, 0.75], High: [0.92, 0.93, 0.95],
                   RoughLow: 0.12, RoughHigh: 0.40 }
    }
};

export const GENERATOR_ORDER = ["rust", "noise", "scratches"];

export const GENERATOR_PARAMS = [
    { Key: "Scale",    Label: "Scale",    Min: 0.02, Max: 1,   Step: 0.01 },
    { Key: "Contrast", Label: "Contrast", Min: 0,    Max: 1,   Step: 0.01 },
    { Key: "Amount",   Label: "Amount",   Min: 0,    Max: 1,   Step: 0.01 },
    // 🔴 Seed is an INTEGER and its step must stay 1. A fractional seed still hashes to something, but
    //    dragging the slider would then walk through a continuum of near-identical patterns instead of
    //    giving the discrete re-rolls the control implies.
    { Key: "Seed",     Label: "Seed",     Min: 0,    Max: 999, Step: 1 }
];

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// The default per-channel mode map for a new layer of a given kind.
//
// 📝 A paint layer's channels default to Texture because that is the only mode a stroke can reach; a
//    fill or material defaults to Value; a generator defaults its driven channels to Generator and
//    leaves the rest on Value.
export function DefaultChannelModes(Kind, Generator)
{
    const Modes  = {};
    const Recipe = Generator ? GENERATOR_RECIPES[Generator] : null;

    for (const Key of CHANNEL_ORDER)
    {
        if (Kind === "generator") { Modes[Key] = Recipe?.Drives.includes(Key) ? "Generator" : "Value"; }
        else if (Kind === "paint") { Modes[Key] = "Texture"; }
        else                       { Modes[Key] = "Value"; }
    }
    return Modes;
}

// Which channels a newly created layer of this kind should have enabled.
export function DefaultChannels(Kind, Preset, Generator)
{
    if (Kind === "material" && MATERIAL_PRESETS[Preset]) { return [...MATERIAL_PRESETS[Preset].Channels]; }
    if (Kind === "generator" && GENERATOR_RECIPES[Generator]) { return [...GENERATOR_RECIPES[Generator].Drives]; }
    if (Kind === "fill") { return ["baseColour", "metallic", "roughness"]; }
    return ["baseColour"];
}
