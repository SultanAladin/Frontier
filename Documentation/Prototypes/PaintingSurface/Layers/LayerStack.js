/*====================================================================================================================================
                                                       LAYERSTACK.JS
====================================================================================================================================*/
// 🧩 The ordered stack of paint layers: each owns three channel atlases, a blend mode, an opacity and a mask

import { CHANNEL_ATLASES, CHANNEL_SLOTS, CHANNEL_ORDER, ChannelAtlasFormat,
         ResolveAtlasWrite } from "./ChannelSet.js";
import { LAYER_KINDS, IsPaintable, DefaultChannelModes, DefaultChannels,
         MATERIAL_PRESETS, GENERATOR_RECIPES } from "./LayerKinds.js";
import { ResampleLayerAtlases, ResampleSingleAtlas, ResolveResolutionOptions } from "./AtlasResample.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// Ported verbatim from TexturePaintInspector so the UI and the engine agree on the vocabulary.
export const CLASSIFICATION_LABEL = {
    material:  "Material",
    generator: "Generator",
    brushwork: "Brushwork",
    flood:     "Flood"
};

export const CLASSIFICATION_ORDER = ["material", "generator", "brushwork", "flood"];

//------------------------------------------------------------------------------------------------------------------------
//                                                      LAYER MASK
//------------------------------------------------------------------------------------------------------------------------

// Ported from Studio-standalone.html's mask model.
//
// 📝 A mask is a coverage field over the layer, built from an ordered list of components (painted strokes, a
//    flat fill, a procedural generator, a levels remap) on top of a base fill of white or black.
export const MASK_COMPONENT_TYPES = {
    Paint:     { Glyph: "brush",   Note: "Hand-painted mask strokes." },
    Fill:      { Glyph: "bucket",  Note: "Uniform fill region." },
    Generator: { Glyph: "sparkle", Note: "Procedural (AO / curvature / dirt)." },
    Levels:    { Glyph: "sliders", Note: "Remap mask contrast & range." }
};

let MaskComponentSequence = 0;

export function MakeMaskComponent(Type, Name)
{
    return { Token: `M${(MaskComponentSequence += 1).toString().padStart(3, "0")}`,
             Type, Name: Name ?? Type };
}

// 🔴 `Enabled` starts FALSE, and that is what makes the "Add Mask" call-to-action honest. Every layer carries
//    a mask object so nothing has to null-check it, but a layer the user never masked must composite as if it
//    has no mask at all — defaulting this to true would silently clip every layer to its unpainted mask.
// 🔴 `Target` is the paint router's switch: true means the brush deposits into this mask instead of the
//    layer's channel atlases. It lives on the mask rather than on the stack so that focusing a different
//    layer cannot silently leave the brush pointed at a mask the user can no longer see.
export function MakeMask(Over)
{
    return { Enabled: false, Fill: "white", Invert: false, Opacity: 100,
             Target: false, Components: [], ...(Over ?? {}) };
}

export const CLASSIFICATION_TINT = {
    material:  "#5b8cff",
    generator: "#c98b3a",
    brushwork: "#4fb286",
    flood:     "#b45fd0"
};

// 🔴 The order here IS the shader's enum. BlendShaderIndex returns the position in this array and the
//    composite shader switches on that integer, so reordering this list silently remaps every layer's
//    blend mode in every existing document.
export const BLEND_MODES = ["Normal", "Multiply", "Screen", "Overlay", "Add", "Darken", "Linear Dodge"];

export const RESOLUTIONS = { "512": 512, "1K": 1024, "2K": 2048, "4K": 4096, "8K": 8192 };

// 📝 Layers are capped because each costs three full atlases. At 1024² that is 12 MiB per layer, so
//    twelve layers is 144 MiB of paint — already past what a modest integrated adapter will hand out
//    without complaint. The cap is a guard rail with a clear message, not a silent allocation failure.
export const LayerCapacity = 12;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

let TokenSequence = 0;
const NextToken = () => `L${(TokenSequence += 1).toString().padStart(3, "0")}`;

export function BlendShaderIndex(Mode)
{
    const Index = BLEND_MODES.indexOf(Mode);
    return Index < 0 ? 0 : Index;
}

// Every channel starts at its documented default, whether or not the layer paints it.
function DefaultChannelValues()
{
    const Values = {};
    for (const Key of CHANNEL_ORDER)
    {
        const Slot = CHANNEL_SLOTS[Key];
        if (Slot.Kind === "derived") { continue; }
        Values[Key] = Array.isArray(Slot.Default) ? Slot.Default.slice() : Slot.Default;
    }
    return Values;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       ONE LAYER
//------------------------------------------------------------------------------------------------------------------------

export class PaintLayer
{
    constructor(Device, Extent, Name, Kind, Enabled, Options)
    {
        const Setting = Options ?? {};

        this.Token   = NextToken();
        this.Name    = Name;
        this.Kind    = LAYER_KINDS[Kind] ? Kind : "paint";
        this.Shown   = true;
        this.Blend   = "Normal";
        this.Opacity = 100;              // [%]
        this.Extent  = Extent;
        this.Device  = Device;

        // 📝 Kept as an alias so anything still reading `Classification` keeps working while the UI and
        //    the serializer move over to `Kind`. Same string, one name for it going forward.
        this.Classification = this.Kind;

        // 🔴 The row's identification hue, and null by default rather than the kind's tint. Null means "no
        //    tag", which is what lets the row fall back to the kind tint; storing the tint here instead
        //    would make an untagged layer indistinguishable from one deliberately tagged its own colour,
        //    and re-tinting the kind later would leave every old layer stuck on the previous palette.
        this.Tag = Setting.Tag ?? null;

        // The layer's coverage mask. Present but disabled until the user adds one.
        this.Mask = MakeMask(Setting.Mask);

        // Which material preset or generator recipe this layer is an instance of, if any.
        this.Preset    = Setting.Preset    ?? null;
        this.Generator = Setting.Generator ?? null;

        // Which of the six the brush deposits into on this layer. A Set, because the inspector toggles
        // membership and every consumer asks "is this channel on" rather than iterating a list.
        this.Enabled = new Set(Enabled ?? DefaultChannels(this.Kind, this.Preset, this.Generator));

        // The authored value per channel — what a dab deposits where the brush ink does not apply.
        this.Values = DefaultChannelValues();

        // Per-channel source: Value | Texture | Generator.
        this.Modes = DefaultChannelModes(this.Kind, this.Generator);

        // Generator parameters, seeded from the recipe so the sliders open where the recipe intends.
        this.Params = { ...(GENERATOR_RECIPES[this.Generator]?.Params ?? {}) };

        // A material preset overwrites the channel defaults with its own authored values.
        const Preset = MATERIAL_PRESETS[this.Preset];
        if (Preset) { for (const [Key, Value] of Object.entries(Preset.Values))
                      { this.Values[Key] = Array.isArray(Value) ? Value.slice() : Value; } }

        // ---- GPU storage -----------------------------------------------------------------------------
        // 🔴 Allocation is LAZY — nothing is created here. Each atlas is 4 MiB at 1024², so eagerly
        //    allocating all three per layer costs 12 MiB for a layer that may never be written; twelve
        //    such layers is 144 MiB of paint the user never asked for. A channel's storage appears on its
        //    first actual write (stroke, flood or generator pass), and an atlas that was never written
        //    stays null and is skipped by the compositor — which is also exactly the right composite
        //    result, since an unwritten layer has zero coverage and must not affect anything beneath it.
        this.Atlas     = {};
        this.AtlasView = {};

        // The greyscale mask's storage, allocated by EnsureMaskAtlas on first use. Null means the layer
        // composites unmasked, which is the correct result for a layer nobody has masked.
        this.MaskAtlas = null;
        this.MaskView  = null;
    }

    // Allocate one atlas on demand and clear it to its documented value. Returns the view.
    //
    // 🔴 The clear is NOT optional and "zero-filled" is not good enough. A fresh WebGPU texture is zeroed,
    //    but height lives at 0.5 (the undisplaced surface) — so an unclear material atlas reads as a
    //    maximum dent at every texel and the derived normal explodes along every island edge.
    EnsureAtlas(AtlasKey)
    {
        if (this.AtlasView[AtlasKey]) { return this.AtlasView[AtlasKey]; }

        const Descriptor = CHANNEL_ATLASES.find(A => A.Key === AtlasKey);
        if (!Descriptor) { return null; }

        // 🔴 RENDER_ATTACHMENT is what lets the paint pass draw INTO the layer, TEXTURE_BINDING is what
        //    lets the composite pass read it back out, and COPY_SRC/COPY_DST carry snapshots for undo.
        //    Miss any one and the failure is a validation error at first use, not at creation.
        const Texture = this.Device.createTexture({
            label:  `Layer${this.Token}${AtlasKey}`,
            size:   [this.Extent, this.Extent],
            format: ChannelAtlasFormat,
            usage:  GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING |
                    GPUTextureUsage.COPY_SRC          | GPUTextureUsage.COPY_DST
        });

        this.Atlas[AtlasKey]     = Texture;
        this.AtlasView[AtlasKey] = Texture.createView();

        const [R, G, B, A] = Descriptor.Clear;
        const Encoder = this.Device.createCommandEncoder({ label: `LayerClear${this.Token}${AtlasKey}` });
        Encoder.beginRenderPass({
            colorAttachments: [{ view: this.AtlasView[AtlasKey],
                                 clearValue: { r: R, g: G, b: B, a: A },
                                 loadOp: "clear", storeOp: "store" }]
        }).end();
        this.Device.queue.submit([Encoder.finish()]);

        return this.AtlasView[AtlasKey];
    }

    // Allocate the layer's greyscale mask on demand, cleared to its base fill.
    //
    // 📝 The mask is a coverage field, not a colour: white shows this layer, black reveals whatever is
    //    resolved beneath it. The compositor multiplies it into the layer's coverage weight, so painting
    //    black is a non-destructive erase — the paint underneath is untouched and painting white back
    //    restores it exactly. That is the whole point of masking over erasing.
    //
    // 🔴 Cleared to the mask's OWN Fill, not to white. "Add black mask" means the layer starts hidden
    //    everywhere and the user paints white to reveal it; seeding white regardless would make the two
    //    menu entries behave identically, with the difference only visible in the metadata.
    //
    // 🔴 Stored in the same RGBA8 as every channel atlas rather than an r8unorm. The paint pass renders
    //    with one pipeline whose target format is fixed at creation, so an r8 mask would need a second
    //    pipeline, a second bind layout and a second shader variant to paint into. Greyscale is written
    //    to all three components and read from .r; the 4x memory is the price of one code path, and at
    //    1024² that is 4 MiB on a layer the user explicitly asked to mask.
    EnsureMaskAtlas()
    {
        if (this.MaskView) { return this.MaskView; }

        const Texture = this.Device.createTexture({
            label:  `Layer${this.Token}Mask`,
            size:   [this.Extent, this.Extent],
            format: ChannelAtlasFormat,
            usage:  GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING |
                    GPUTextureUsage.COPY_SRC          | GPUTextureUsage.COPY_DST
        });

        this.MaskAtlas = Texture;
        this.MaskView  = Texture.createView();

        // 🔴 Alpha is 1 in both cases. The mask carries its value in RGB and is sampled for .r only; a
        //    black mask cleared to alpha 0 would still read r = 0 and work by accident here, but the
        //    snapshot/undo path copies the texture wholesale and a zero alpha there reads as "never
        //    written". Keeping alpha at 1 means "this mask exists" is unambiguous everywhere.
        const Level = this.Mask?.Fill === "black" ? 0.0 : 1.0;
        const Encoder = this.Device.createCommandEncoder({ label: `LayerMaskClear${this.Token}` });
        Encoder.beginRenderPass({
            colorAttachments: [{ view: this.MaskView,
                                 clearValue: { r: Level, g: Level, b: Level, a: 1.0 },
                                 loadOp: "clear", storeOp: "store" }]
        }).end();
        this.Device.queue.submit([Encoder.finish()]);

        return this.MaskView;
    }

    // Is this layer's mask actually affecting the composite right now?
    //
    // 🔴 BOTH conditions, and the atlas one is not redundant. `Enabled` is metadata the UI flips before
    //    anything has been painted, so a mask enabled but never allocated has no texture to bind — and
    //    binding nothing while telling the shader a mask exists samples garbage. The compositor asks this,
    //    not `Mask.Enabled`.
    get MaskActive() { return this.Mask?.Enabled === true && !!this.MaskView; }

    // Has this layer ever been written? Drives the "allocated on first stroke" note in the UI.
    get Allocated() { return Object.keys(this.Atlas).length > 0; }

    get Paintable() { return IsPaintable(this.Kind); }

    // Wipe every channel atlas back to its documented clear value.
    //
    // 📝 Only ALLOCATED atlases are cleared. An unallocated one is already in the right state by
    //    definition — it has no storage, which the compositor reads as zero coverage, which is what a
    //    cleared layer means. Touching one here would allocate 4 MiB to write the value it already has.
    Clear(Device)
    {
        const Encoder = Device.createCommandEncoder({ label: `LayerClear${this.Token}` });

        for (const Descriptor of CHANNEL_ATLASES)
        {
            if (!this.AtlasView[Descriptor.Key]) { continue; }

            const [R, G, B, A] = Descriptor.Clear;
            Encoder.beginRenderPass({
                label: `LayerClear${this.Token}${Descriptor.Key}`,
                colorAttachments: [{
                    view:       this.AtlasView[Descriptor.Key],
                    clearValue: { r: R, g: G, b: B, a: A },
                    loadOp:     "clear",
                    storeOp:    "store"
                }]
            }).end();
        }

        Device.queue.submit([Encoder.finish()]);
    }

    // Fill every atlas this layer paints with its authored values at full coverage.
    //
    // 📝 A clear-to-value, not a draw. The paint pass rasterizes the mesh in UV space and so only reaches
    //    texels the model actually uses; a base material wants the whole atlas, including the gutters
    //    between UV islands. Filling the gutters is what stops bilinear sampling from dragging the clear
    //    colour in across every island edge.
    //
    // 🔴 Only the ENABLED channels are flooded, and unenabled components keep their clear value — the
    //    same contract the write mask enforces for a dab. A layer that paints roughness but not metallic
    //    must not flood metallic to 0 and call it authored; that is a different statement from "untouched".
    Flood(Device)
    {
        const Encoder = Device.createCommandEncoder({ label: `LayerFlood${this.Token}` });
        let   Any     = false;

        for (const Descriptor of CHANNEL_ATLASES)
        {
            const Write = ResolveAtlasWrite(Descriptor.Key, this.Enabled, this.Values, null);

            // Nothing enabled for this atlas: leave it transparent so the compositor passes the layers
            // beneath it through untouched.
            if (Write === null) { continue; }

            // A flood IS a write, so this is where the storage is earned.
            const View = this.EnsureAtlas(Descriptor.Key);
            if (!View) { continue; }

            const [R, G, B] = Descriptor.Clear;
            const Filled = [
                Write.Mask[0] ? Write.Value[0] : R,
                Write.Mask[1] ? Write.Value[1] : G,
                Write.Mask[2] ? Write.Value[2] : B
            ];

            Encoder.beginRenderPass({
                label: `LayerFlood${this.Token}${Descriptor.Key}`,
                colorAttachments: [{
                    view:       View,
                    clearValue: { r: Filled[0], g: Filled[1], b: Filled[2], a: 1.0 },
                    loadOp:     "clear",
                    storeOp:    "store"
                }]
            }).end();
            Any = true;
        }

        // 🔴 An encoder with no passes still has to be finished and submitted or it leaks, but submitting
        //    an empty command buffer is pointless work every time a non-flooding layer is created.
        if (Any) { Device.queue.submit([Encoder.finish()]); }
        else     { Encoder.finish(); }
    }

    // Change this layer's atlas resolution, carrying the painted content across.
    //
    // 🔴 The content is RESAMPLED, not discarded. Reallocating at the new size and clearing would be far
    //    simpler and is what the size change literally requires — but it would silently destroy every stroke
    //    the moment the user touched the resolution dropdown, which is the same one-way trap the Value/Texture
    //    switch had. A resolution change is a change of precision, not of content.
    //
    // 🔴 The OLD textures are destroyed only AFTER the new ones exist. An 8K triple is 768 MiB and allocation
    //    can genuinely fail; freeing first would turn a refused resize into lost paint.
    Resize(Device, ToExtent)
    {
        if (!Number.isFinite(ToExtent) || ToExtent <= 0)     { return false; }
        if (ToExtent === this.Extent)                        { return false; }

        // 🔴 The mask is carried FIRST and independently of the channel atlases, because `Allocated` counts
        //    only channel atlases and the early return below would otherwise skip it. A layer that has a
        //    mask but no paint — entirely reachable: add a mask, then change resolution before painting —
        //    would keep a mask at the OLD extent while the layer reports the new one. The compositor binds
        //    both to one draw, so the mismatch is a validation error at the next resolve, not a wrong
        //    picture, and it fires far from the resize that caused it.
        const FromExtent = this.Extent;
        if (this.MaskAtlas)
        {
            const NextMask = ResampleSingleAtlas(
                Device, this.MaskAtlas, FromExtent, ToExtent, `Layer${this.Token}Mask`);

            if (NextMask)
            {
                const StaleMask = this.MaskAtlas;
                this.MaskAtlas  = NextMask.Texture;
                this.MaskView   = NextMask.View;
                StaleMask.destroy();
            }
        }

        // Nothing allocated means nothing to carry: record the extent and let lazy allocation use it.
        if (!this.Allocated) { this.Extent = ToExtent; return true; }

        const Next = ResampleLayerAtlases(Device, this, ToExtent);
        if (!Next.Any) { this.Extent = ToExtent; return true; }

        const Stale = this.Atlas;

        this.Atlas     = Next.Atlas;
        this.AtlasView = Next.View;
        this.Extent    = ToExtent;

        for (const Descriptor of CHANNEL_ATLASES) { Stale[Descriptor.Key]?.destroy(); }
        return true;
    }

    Release()
    {
        // Only allocated atlases exist to destroy — a lazily-skipped one has no texture object.
        for (const Descriptor of CHANNEL_ATLASES) { this.Atlas[Descriptor.Key]?.destroy(); }
        this.Atlas     = {};
        this.AtlasView = {};

        // 🔴 The mask lives outside CHANNEL_ATLASES, so the loop above does not reach it. Left out, a
        //    masked layer leaks a full atlas every time one is deleted.
        this.MaskAtlas?.destroy();
        this.MaskAtlas = null;
        this.MaskView  = null;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE STACK
//------------------------------------------------------------------------------------------------------------------------

export class LayerStack
{
    // 🔴 Index 0 is the TOP of the stack, matching how the inspector lists layers and how every paint
    //    tool presents them. The composite pass therefore walks the array BACKWARD. Storing bottom-first
    //    instead would read more naturally in the compositor and would invert the stack in the UI, which
    //    is the more visible wrong.
    constructor(Device, Extent)
    {
        this.Device  = Device;
        this.Extent  = Extent;
        this.Layers  = [];
        this.FocusToken = null;
        this.Revision   = 0;        // Bumped on any change the compositor must react to.
    }

    get Count()        { return this.Layers.length; }
    get EnabledCount() { return this.Layers.filter(L => L.Shown).length; }

    // The atlas sizes this device can actually allocate, as {Value, Label}.
    //
    // 🔴 Answered from the STACK rather than imported straight into the inspector, because the honest answer
    //    depends on the adapter's maxTextureDimension2D and the inspector holds no device. Offering 8K on a
    //    device that caps at 4096 fails inside createTexture with an error naming the texture, so the menu that
    //    made the promise never appears in the message.
    get ResolutionOptions() { return ResolveResolutionOptions(this.Device); }

    Resolve(Token)     { return this.Layers.find(L => L.Token === Token) ?? null; }
    IndexOf(Token)     { return this.Layers.findIndex(L => L.Token === Token); }

    // The layer a stroke lands on. 🔴 Never falls back to "the top layer" when focus is unset — a
    //    stroke with no target must be refused, not silently redirected onto whatever sits on top.
    get Focus()        { return this.FocusToken ? this.Resolve(this.FocusToken) : null; }

    Touch() { this.Revision += 1; }

    Add(Name, Kind, Enabled, AtIndex, Options)
    {
        if (this.Layers.length >= LayerCapacity)
        {
            throw new Error(`Layer cap of ${LayerCapacity} reached — each layer costs up to three ${this.Extent}² atlases.`);
        }

        const Layer = new PaintLayer(this.Device, this.Extent, Name, Kind, Enabled, Options);
        const Where = AtIndex === undefined ? 0 : Math.max(0, Math.min(this.Layers.length, AtIndex));

        this.Layers.splice(Where, 0, Layer);
        this.FocusToken = Layer.Token;
        this.Touch();
        return Layer;
    }

    Remove(Token)
    {
        const Index = this.IndexOf(Token);
        if (Index < 0) { return false; }

        // 🔴 The bottom layer is the substrate every other layer composites over. Removing it leaves the
        //    stack with nothing opaque underneath and the resolve shows through to the clear colour, so
        //    it is refused rather than allowed to produce a confusing result.
        if (this.Layers.length <= 1) { return false; }

        this.Layers[Index].Release();
        this.Layers.splice(Index, 1);

        if (this.FocusToken === Token)
        {
            // Focus the layer that took its place, or the one above if it was the last.
            const Next = this.Layers[Math.min(Index, this.Layers.length - 1)];
            this.FocusToken = Next ? Next.Token : null;
        }

        this.Touch();
        return true;
    }

    // Move a layer by one position. Direction -1 raises it toward the top (index 0), +1 lowers it.
    Reorder(Token, Direction)
    {
        const Index  = this.IndexOf(Token);
        const Target = Index + Direction;
        if (Index < 0 || Target < 0 || Target >= this.Layers.length) { return false; }

        const [Moved] = this.Layers.splice(Index, 1);
        this.Layers.splice(Target, 0, Moved);
        this.Touch();
        return true;
    }

    Focus_Set(Token)
    {
        if (!this.Resolve(Token)) { return false; }
        this.FocusToken = Token;
        this.Touch();
        return true;
    }

    // Bottom-to-top, which is the order the compositor needs.
    *BottomUp()
    {
        for (let Ordinal = this.Layers.length - 1; Ordinal >= 0; Ordinal -= 1) { yield this.Layers[Ordinal]; }
    }

    Release()
    {
        for (const Layer of this.Layers) { Layer.Release(); }
        this.Layers = [];
        this.FocusToken = null;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SEEDED STACK
//------------------------------------------------------------------------------------------------------------------------

// The stack the prototype opens with: a blank white base, and one empty paint layer above it to receive
// the first stroke.
//
// 🔴 The document starts as a CLEAN WHITE SURFACE, not as a pre-dressed demo scene. A seeded car-paint
//    stack looks better in a screenshot but it is the wrong starting point for a painting tool: every
//    first stroke lands on top of somebody else's material, and the user cannot tell their own paint from
//    the seed. White also makes the brush colour honest — paint over grey and every colour reads dark.
export function SeedLayerStack(Device, Extent)
{
    const Stack = new LayerStack(Device, Extent);

    // 🔴 Added bottom-first because Add() inserts at index 0. Adding these in listed order would stand
    //    the stack on its head and the white base would cover the paint layer completely.
    //
    // 📝 The base is a FILL, not a paint layer: its content is its authored value across the whole
    //    surface, and it is flooded so it covers the UV gutters too. A base that stopped at the island
    //    edges would let the clear colour bleed in under bilinear sampling.
    const Base = Stack.Add("Base — White", "fill",
                           ["baseColour", "metallic", "roughness"]);
    Base.Values.baseColour = [1.0, 1.0, 1.0];
    Base.Values.metallic   = 0.0;
    Base.Values.roughness  = 0.55;      // a plain matte white, not a gloss
    Base.Flood(Device);

    // An empty paint layer on top, so the very first stroke has somewhere legal to land. It allocates no
    // atlases until that stroke actually happens.
    const Paint = Stack.Add("Paint 1", "paint", ["baseColour"]);

    Stack.Focus_Set(Paint.Token);
    return Stack;
}
