#!/usr/bin/env python3
"""Rewrite the disputed Alloy families (solar, golf, stucco, scratched metals,
brake, scratch study, woven/knit fabrics) as runtime procedural recipes.

Source of truth: the complete normalized Alloy SourceRecipe per material in
Library.toml. Every channel param is passed through verbatim except two
documented transforms:
  * scratchDensity 6 -> 0 on metals: shipped metals are polished; cuts are an
    opt-in overlay and the Alloy defaults stay staged for the toggle.
  * scratchAngle/weaveAngle -> canonical "angle": the pool stores one angle so
    the construction direction and the independent scratch direction never
    collide (see ResolveProceduralParamKey).
Sheen carriers (sheen/sheenRoughness/fuzzLength) ride the weave/knit channel
params so the retained Alloy finish sliders resolve; the native shader has no
sheen term, so only the roughness/fuzz targets take visible effect.

UvScale derivation (equator density match): the Alloy specimen ball carries the
1.43-profile, so one circumference spans 2*pi*1.43 ~= 8.99 object-space units
and pole-to-pole pi*1.43 ~= 4.49. The native ball wraps UV [0,1] around the
same profile, so UvScale [9, 4.5] reproduces the Alloy thread/cell count across
the visible face while keeping texels square on the surface.

Regenerate with: python Exhibits/TextureStudio/Code/ConvertDisputedMaterials.py
then bake the committed load cache with MaterialProceduralBake. The validation
test fails closed when channel params drift from the SourceRecipe or committed
PNG bytes drift from a fresh kernel bake.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
ARCHIVE = ROOT / "EngineContent/MaterialArchives/TextureStudio"

FABRIC_OPS = {
    "woven": "weave", "cotton": "weave", "denim": "weave", "silk": "weave",
    "linen": "weave", "wool": "weave", "suede": "weave", "velvet": "weave",
    "jersey": "knit",
}

TARGETS = {
    # stem: op
    "monocrystalline-solar": "solar", "polycrystalline-solar": "solar",
    "black-solar-module": "solar",
    "tour-golf-ball": "dimples", "practice-golf-ball": "dimples",
    "lime-stucco": "stucco",
    "liquid-silver": "scratchedMetal", "brushed-titanium": "scratchedMetal",
    "pure-aluminium": "scratchedMetal", "24k-gold": "scratchedMetal",
    "18k-yellow-gold": "scratchedMetal", "14k-yellow-gold": "scratchedMetal",
    "18k-rose-gold": "scratchedMetal", "18k-white-gold": "scratchedMetal",
    "pure-copper": "scratchedMetal", "maraging-steel": "scratchedMetal",
    "cast-steel": "scratchedMetal", "stainless-steel": "scratchedMetal",
    "pure-iron": "scratchedMetal", "chromium": "scratchedMetal",
    "bronze-alloy": "scratchedMetal", "brass-alloy": "scratchedMetal",
    "pure-nickel": "scratchedMetal", "titanium-metal": "scratchedMetal",
    "pure-zinc": "scratchedMetal", "pure-silver": "scratchedMetal",
    "pure-tin": "scratchedMetal",
    "brake-disc": "latheMetal",
    "scratches": "scratches",
    "woven-fabric": "weave", "suede-microfiber": "weave",
    "midnight-velvet": "weave", "natural-cotton": "weave",
    "indigo-denim": "weave", "champagne-silk": "weave",
    "midnight-satin": "weave", "plain-linen": "weave",
    "basket-cotton": "weave", "ribbed-cotton": "weave",
    "oxford-weave": "weave", "herringbone-wool": "weave",
    "houndstooth-wool": "weave", "raw-selvedge-denim": "weave",
    "washed-indigo-denim": "weave", "black-selvedge-denim": "weave",
    "cotton-jersey-knit": "knit", "melange-jersey-knit": "knit",
    "terracotta-clay": "clay", "wet-potter-clay": "clay",
    "kaolin-clay": "clay", "sculpting-clay": "clay",
    "led-pixel-matrix": "led", "warm-cob-led": "led",
    "apple-skin": "fruit",
}

OP_CHANNELS = {
    "solar": ("Albedo", "Orm", "Normal"),
    "scratchedMetal": ("Albedo", "Orm", "Normal"),
    "latheMetal": ("Albedo", "Orm", "Normal"),
    "scratches": ("Orm", "Normal"),
    "dimples": ("Albedo", "Normal"),
    "stucco": ("Albedo", "Normal"),
    "weave": ("Albedo", "Normal"),
    "knit": ("Albedo", "Normal"),
    "clay": ("Albedo", "Normal"),
    "led": ("Albedo", "Normal", "Radiance"),
    "fruit": ("Albedo", "Normal"),
}

# Bake-driving scalars passed through verbatim from the SourceRecipe.
OP_SCALARS = {
    "solar": ("detailScale", "roughness", "busbarWidth", "surfaceSeed", "panelMode"),
    "dimples": ("detailScale", "dimpleDepth", "surfaceSeed"),
    "stucco": ("detailScale", "grain", "surfaceSeed"),
    "clay": ("detailScale", "grain", "moisture", "surfaceSeed"),
    "led": ("detailScale", "pixelFill", "lensDome", "packageDepth", "surfaceSeed"),
    "fruit": ("detailScale", "skinMode", "spotDensity", "colorGradient", "grain", "surfaceSeed"),
    "weave": ("detailScale", "weaveAngle", "weaveRelief", "fiberDetail", "fuzz",
              "slub", "denimFade", "grain", "fabricMode", "surfaceSeed"),
    "knit": ("detailScale", "grain", "fuzz", "weaveAngle", "surfaceSeed"),
    "scratchedMetal": ("grain", "detailScale", "roughness", "scratchLength",
                       "scratchWidth", "scratchDepth", "scratchSpread",
                       "scratchBend", "scratchScale", "surfaceSeed"),
    "latheMetal": ("depth", "detailScale", "roughness", "scratchLength",
                   "scratchWidth", "scratchDepth", "scratchSpread",
                   "scratchBend", "scratchScale", "surfaceSeed"),
    "scratches": ("scratchDensity", "scratchLength", "scratchWidth",
                  "scratchDepth", "scratchSpread", "scratchBend",
                  "scratchScale", "surfaceSeed", "roughness"),
}

# Inert carriers so retained Alloy sliders resolve against the pool.
OP_CARRIERS = {
    "weave": ("sheen", "sheenRoughness", "fuzzLength"),
    "knit": ("sheen",),
}

OP_TEXTS = {
    "solar": ("color", "secondaryColor"),
    "weave": ("weavePattern", "warpColor", "weftColor"),
    "knit": ("color", "secondaryColor"),
    "led": ("color",),
    "fruit": ("color", "secondaryColor", "tertiaryColor"),
}

LIFT = {"solar": 0.02, "scratchedMetal": 0.3, "latheMetal": 0.3, "scratches": 0.3}


def parse_library():
    text = (ARCHIVE / "Library.toml").read_text()
    recipes = {}
    for block in re.split(r"\n\[\[Materials\]\]\n", text)[1:]:
        path = re.search(r'Path = "([^"]+)"', block).group(1)
        src = re.search(r"SourceRecipe = \{(.*?)\}\n", block, re.S).group(1)
        values = dict(re.findall(r'"([^"]+)" = ("[^"]*"|[^,}]+)', src))
        recipes[path[:-9]] = {k: v.strip() for k, v in values.items()}
    return recipes


def clean_number(value):
    text = ("%.10g" % value).replace("-0", "0")
    return "0" if text in ("-0",) else text


def decode_spectrum(hex_text):
    hex_text = hex_text.strip('"')
    out = []
    for offset in (1, 3, 5):
        channel = int(hex_text[offset:offset + 2], 16) / 255
        out.append(channel / 12.92 if channel <= 0.04045 else ((channel + 0.055) / 1.055) ** 2.4)
    return out


def build_material(stem, op, recipe):
    current = (ARCHIVE / f"{stem}.material").read_text()
    name = re.search(r'Name = "(.*)"', current).group(1)
    rough = float(recipe["roughness"])
    if op in LIFT:
        rough = min(1.0, rough + LIFT[op])
    metallic = 1.0 if op == "solar" else float(recipe["metalness"])
    bakes_color = op in ("solar", "knit", "led", "fruit") or (op == "weave" and recipe["fabricMode"] in ("0", "3", "4"))
    albedo = [1.0, 1.0, 1.0] if bakes_color else decode_spectrum(recipe["color"])
    mask = {"solar": 7, "scratchedMetal": 7, "latheMetal": 7, "scratches": 6, "led": 13}.get(op, 5)
    radiance = None
    if op == "led":
        strength = float(recipe["emissionStrength"])
        radiance = [clean_number(c * strength) for c in decode_spectrum(recipe["emissionColor"])]

    lines = ["Format = 1", f'Name = "{name}"', "", "[[Strata]]",
             'Category = "Opaque"',
             "Albedo = [%s]" % ", ".join(repr(c) for c in albedo),
             f"Roughness = {clean_number(rough)}",
             f"Metallic = {clean_number(metallic)}",
             f"Ior = {clean_number(float(recipe['ior']))}",
             "Coverage = 1.0",
             f"Anisotropy = {clean_number(float(recipe['anisotropy']))}",
             f"TextureMask = {mask}",
             "UvScaleOffset = [9, 4.5, 0, 0]",
             "",
             "[Strata.Textures]"]
    if radiance is not None:
        lines.insert(2, "Radiance = [%s]" % ", ".join(radiance))
    for channel in OP_CHANNELS[op]:
        lines.append(f'{channel} = "Textures/{stem}-{channel.lower()}.png"')

    params = []
    for key in OP_SCALARS[op]:
        params.append(f"{key} = {recipe[key]}")
    if op in ("scratchedMetal", "latheMetal"):
        # Shipped metals are polished; Alloy scratch defaults stay staged.
        params.append("scratchDensity = 0")
        params.append(f"angle = {recipe['scratchAngle']}")
    if op == "scratches":
        params.append(f"angle = {recipe['weaveAngle']}")
    for key in OP_CARRIERS.get(op, ()):
        params.append(f"{key} = {recipe[key]}")
    for key in OP_TEXTS.get(op, ()):
        params.append(f'{key} = {recipe[key]}')

    for channel in OP_CHANNELS[op]:
        lines += ["", "", "[[Strata.Channels]]", f'Name = "{channel}"',
                  f'Op = "{op}"', 'Link = "shared"',
                  "[Strata.Channels.Params]", *params]

    coat = float(recipe["coat"])
    existing_coat = re.search(r'\[\[Strata\]\]\nCategory = "Coating"(.*?)(?=\n\[\[|\Z)',
                              current, re.S)
    if coat > 0:
        if not existing_coat:
            raise SystemExit(f"{stem}: recipe needs coat {coat} but no coating stratum found")
        lines += ["", "", '[[Strata]]', 'Category = "Coating"',
                  "Albedo = [1.0, 1.0, 1.0]",
                  f"Roughness = {clean_number(float(recipe['coatRoughness']))}",
                  "Metallic = 0.0",
                  f"Ior = {clean_number(float(recipe['coatIor']))}",
                  f"Coverage = {clean_number(coat)}"]
    elif existing_coat:
        raise SystemExit(f"{stem}: unexpected coating stratum for coat 0")
    lines.append("")
    return "\n".join(lines)


def main():
    recipes = parse_library()
    missing = [s for s in TARGETS if s not in recipes]
    if missing:
        raise SystemExit(f"missing SourceRecipe: {missing}")
    for stem, op in sorted(TARGETS.items()):
        recipe = recipes[stem]
        text = build_material(stem, op, recipe)
        (ARCHIVE / f"{stem}.material").write_text(text)
    print(f"converted {len(TARGETS)} materials")


if __name__ == "__main__":
    sys.exit(main())
