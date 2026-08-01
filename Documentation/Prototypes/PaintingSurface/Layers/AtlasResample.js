/*====================================================================================================================================
                                                     ATLASRESAMPLE.JS
====================================================================================================================================*/
// 🧩 Resample one layer atlas to a new extent, preserving painted content across a resolution change

import { CHANNEL_ATLASES, ChannelAtlasFormat } from "./ChannelSet.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 The box filter's tap count per axis is CAPPED. An 8K -> 512 reduction has a 16x16 source footprint per
//    destination texel, which is 256 taps — fine — but the same arithmetic on a hypothetical larger pair grows
//    quadratically and would stall the queue for seconds with no feedback. 8 per axis (64 taps) is past the
//    point where more taps change the result visibly, because a box filter converges quickly.
const MaximumTapsPerAxis = 8;

//------------------------------------------------------------------------------------------------------------------------
//                                                      SHADER SOURCE
//------------------------------------------------------------------------------------------------------------------------

// 🔴 A DRAW, not copyTextureToTexture. The copy requires identical sizes, which is exactly what a resolution
//    change is not — so the content has to be re-filtered through the rasterizer.
//
// 🔴 The filter is a PREMULTIPLIED box, and the premultiply is the whole difficulty. The atlases store
//    coverage as STRAIGHT alpha (the compositor does mix(Below, Above, Layer.a), which is only correct for
//    straight), and averaging straight RGBA averages colour and coverage independently — which pulls the
//    clear colour of UNPAINTED texels into the mean. A sparse red stroke on a mid-grey transparent atlas
//    would drift toward grey-pink at every resolution change, so the paint would fade a little each time the
//    user touched the dropdown. Weight by coverage, average, then divide back out.
const ResampleShaderSource = `
struct ResampleUniform
{
    // xy = source extent in texels, z = taps per axis, w = unused
    Control : vec4f,
    // The atlas's documented clear value, returned where the footprint carries no coverage at all.
    Clear   : vec4f,
};

@group(0) @binding(0) var<uniform> Resample : ResampleUniform;
@group(0) @binding(1) var SourceStore : texture_2d<f32>;
@group(0) @binding(2) var SourceSampler : sampler;

struct ResampleVarying
{
    @builtin(position) ClipPosition : vec4f,
    @location(0)       Coordinate   : vec2f,
};

@vertex
fn ResampleVertex(@builtin(vertex_index) Index : u32) -> ResampleVarying
{
    // One oversized triangle covering the target, matching the compositor's and the preview's idiom.
    var Corners = array<vec2f, 3>(vec2f(-1.0, -5.0), vec2f(-1.0, 1.0), vec2f(5.0, 1.0));
    let Corner  = Corners[Index];

    var Out : ResampleVarying;
    Out.ClipPosition = vec4f(Corner, 0.0, 1.0);
    // Clip y runs up, texture v runs down.
    Out.Coordinate   = vec2f((Corner.x + 1.0) * 0.5, (1.0 - Corner.y) * 0.5);
    return Out;
}

@fragment
fn ResampleFragment(In : ResampleVarying) -> @location(0) vec4f
{
    let SourceExtent = Resample.Control.xy;
    let Taps         = i32(Resample.Control.z);
    let Span         = 1.0 / SourceExtent;

    // 🔴 The taps span one DESTINATION texel expressed in source space, which is what makes this correct in
    //    BOTH directions. Downscaling gives a footprint many source texels wide and the box averages them (a
    //    single bilinear tap would read 4 of 256 and drop most of a sparse stroke); upscaling gives a
    //    sub-texel footprint where every tap lands in the same neighbourhood, so the result is the bilinear
    //    interpolation the sampler would have given anyway.
    //
    // 📝 Taps is set to round(From/To) for a reduction, so Span times Taps IS the destination texel's width
    //    in source units — no separate destination extent needs to be passed in.
    //
    // 🔴 No backticks in here. This whole shader is a JS template literal, so a backtick in a WGSL comment
    //    terminates the literal at that point: every module that imports this one dies with a parse error
    //    pointing at the next identifier, which reads as a JS fault a long way from the actual cause.
    var Colour = vec3f(0.0);
    var Cover  = 0.0;
    var Count  = 0.0;

    for (var Y = 0; Y < Taps; Y += 1)
    {
        for (var X = 0; X < Taps; X += 1)
        {
            // Taps spread evenly across the destination texel's own width, centred on it.
            let Jitter = (vec2f(f32(X), f32(Y)) + 0.5) / f32(Taps) - 0.5;
            let At     = In.Coordinate + Jitter * Span * f32(Taps);
            let S      = textureSampleLevel(SourceStore, SourceSampler, At, 0.0);
            Colour += S.rgb * S.a;
            Cover  += S.a;
            Count  += 1.0;
        }
    }

    // 🔴 A fully transparent footprint returns the atlas's CLEAR value, not zero. Dividing by a zero coverage
    //    sum is NaN, and falling back to black instead would put 0 in the height component — which reads as a
    //    maximum depression rather than the undisplaced surface, exactly the fault EnsureAtlas's clear exists
    //    to avoid. The gutters between UV islands make an all-transparent footprint common, not exotic.
    if (Cover <= 0.0) { return vec4f(Resample.Clear.rgb, 0.0); }

    // Un-premultiplied on the way out, because straight alpha is what the atlas stores.
    return vec4f(Colour / Cover, Cover / Count);
}`;

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 One pipeline per DEVICE, cached on the device object itself rather than in a module-level variable. A
//    module singleton would be captured by the first device it saw and then hand stale GPU objects to a second
//    one — which is what happens across a device loss and reacquire, and the validation error names the
//    pipeline rather than the caching, so it reads as a shader fault.
function ResolveResamplePipeline(Device)
{
    if (Device.__AtlasResample) { return Device.__AtlasResample; }

    const ShaderModule = Device.createShaderModule({
        label: "AtlasResample",
        code:  ResampleShaderSource
    });

    const Entry = {
        Pipeline: Device.createRenderPipeline({
            label:  "AtlasResample",
            layout: "auto",
            vertex:   { module: ShaderModule, entryPoint: "ResampleVertex" },
            fragment: { module: ShaderModule, entryPoint: "ResampleFragment",
                        targets: [{ format: ChannelAtlasFormat }] },
            primitive: { topology: "triangle-list" }
        }),
        // Linear, so an UPSCALE interpolates rather than blocking up into visible source texels.
        Sampler: Device.createSampler({
            label: "AtlasResampleSampler",
            magFilter: "linear", minFilter: "linear",
            addressModeU: "clamp-to-edge", addressModeV: "clamp-to-edge"
        }),
        Uniform: Device.createBuffer({
            label: "AtlasResampleUniform",
            size:  32,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
        })
    };

    Device.__AtlasResample = Entry;
    return Entry;
}

// How many taps per axis this reduction needs. 1:1 and upscales need no box at all.
function ResolveTapCount(FromExtent, ToExtent)
{
    if (ToExtent >= FromExtent) { return 1; }
    return Math.min(MaximumTapsPerAxis, Math.max(1, Math.round(FromExtent / ToExtent)));
}

// Re-filter one layer's atlases into fresh textures at `ToExtent`, returning the new texture/view pair.
//
// 🔴 The OLD textures are destroyed by the caller, not here, and only after the new ones exist. Destroying
//    first would leave the layer with no storage if allocation then failed (an 8K triple is 768 MiB and can
//    genuinely fail), turning a refused resize into silent loss of the user's paint.
export function ResampleLayerAtlases(Device, Layer, ToExtent)
{
    const Entry = ResolveResamplePipeline(Device);

    const Atlas = {};
    const View  = {};
    let   Any   = false;

    for (const Descriptor of CHANNEL_ATLASES)
    {
        // 📝 Only ALLOCATED atlases are resampled. An unallocated channel has no content to preserve, and
        //    allocating one here would cost 4 MiB to store the clear value it already reads as.
        const Source = Layer.Atlas?.[Descriptor.Key];
        if (!Source) { continue; }

        const Texture = Device.createTexture({
            label:  `Layer${Layer.Token}${Descriptor.Key}`,
            size:   [ToExtent, ToExtent],
            format: ChannelAtlasFormat,
            usage:  GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING |
                    GPUTextureUsage.COPY_SRC          | GPUTextureUsage.COPY_DST
        });
        const Target = Texture.createView();

        const Taps = ResolveTapCount(Layer.Extent, ToExtent);
        const [R, G, B] = Descriptor.Clear;

        // 🔴 ONE ENCODER AND ONE SUBMIT PER ATLAS, because the three atlases share a single uniform buffer and
        //    each pass needs its own clear value in it. queue.writeBuffer is ordered against the SUBMIT, not
        //    against encoding — so batching all three passes into one encoder and writing the uniform between
        //    them gives every pass the LAST value written, and the colour atlas would fall back to the
        //    emissive black in its gutters. Pairing each write with its own submit is what keeps them
        //    separate; the alternative is three uniform buffers, which is more GPU objects for no gain here.
        Device.queue.writeBuffer(Entry.Uniform, 0, new Float32Array(
            [Layer.Extent, Layer.Extent, Taps, 0,
             R, G, B, 0]));

        const Encoder = Device.createCommandEncoder({
            label: `AtlasResample${Layer.Token}${Descriptor.Key}` });

        const Pass = Encoder.beginRenderPass({
            label: `AtlasResample${Layer.Token}${Descriptor.Key}`,
            colorAttachments: [{
                view: Target, loadOp: "clear", storeOp: "store",
                clearValue: { r: R, g: G, b: B, a: 0 }
            }]
        });
        Pass.setPipeline(Entry.Pipeline);
        Pass.setBindGroup(0, Device.createBindGroup({
            layout: Entry.Pipeline.getBindGroupLayout(0),
            entries: [
                { binding: 0, resource: { buffer: Entry.Uniform } },
                { binding: 1, resource: Source.createView() },
                { binding: 2, resource: Entry.Sampler }
            ]
        }));
        Pass.draw(3);
        Pass.end();

        Device.queue.submit([Encoder.finish()]);

        Atlas[Descriptor.Key] = Texture;
        View[Descriptor.Key]  = Target;
        Any = true;
    }

    return { Atlas, View, Any };
}

// Re-filter ONE texture into a fresh one at `ToExtent`. Used for the layer mask, which lives outside
// CHANNEL_ATLASES and so is not reached by the loop above.
//
// 🔴 Shares the same pipeline and box-filter path as the channel resample rather than reimplementing it.
//    A separate simplified copy here would drift from the real filter, and a mask resampled by a different
//    kernel than the paint it gates produces a halo along every mask edge on reduction — a defect that
//    only appears at one specific resolution change and looks like a compositing bug, not a resample one.
//
// 🔴 Returns null rather than throwing when there is nothing to carry, so the caller's "no mask" and
//    "mask resample failed" paths stay distinguishable.
export function ResampleSingleAtlas(Device, Source, FromExtent, ToExtent, Label)
{
    if (!Source) { return null; }

    const Entry = ResolveResamplePipeline(Device);

    const Texture = Device.createTexture({
        label:  Label ?? "ResampledAtlas",
        size:   [ToExtent, ToExtent],
        format: ChannelAtlasFormat,
        usage:  GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING |
                GPUTextureUsage.COPY_SRC          | GPUTextureUsage.COPY_DST
    });
    const Target = Texture.createView();

    // 🔴 The uniform is EIGHT floats — [FromExtent, FromExtent, Taps, pad, ClearR, ClearG, ClearB, pad] —
    //    and all eight must be written. Writing only the first four leaves the clear colour holding
    //    whatever the previous caller put there (the emissive atlas's black, in practice), which the
    //    shader then drags into this mask's gutters. The failure is invisible on an upscale, where the
    //    gutter is never sampled, and shows as dark fringing along island edges on a reduction.
    //
    // 🔴 Both extents are the SOURCE extent, matching ResampleLayerAtlases above. The field pair reads
    //    like [From, To] and is not — it is the source dimensions twice, and the destination size comes
    //    from the render target. Passing ToExtent in the second slot scales the sample grid wrongly and
    //    the mask lands offset by half its resolution difference.
    const Taps = ResolveTapCount(FromExtent, ToExtent);
    Device.queue.writeBuffer(Entry.Uniform, 0, new Float32Array(
        [FromExtent, FromExtent, Taps, 0,
         1, 1, 1, 0]));

    const Encoder = Device.createCommandEncoder({ label: Label ?? "ResampleSingle" });
    const Pass = Encoder.beginRenderPass({
        colorAttachments: [{ view: Target, loadOp: "clear",
                             clearValue: { r: 1, g: 1, b: 1, a: 1 }, storeOp: "store" }]
    });
    Pass.setPipeline(Entry.Pipeline);
    Pass.setBindGroup(0, Device.createBindGroup({
        // 🔴 Taken from the pipeline, matching ResampleLayerAtlases. The cached entry exposes no standalone
        //    layout field, and naming one that does not exist yields `undefined` — which createBindGroup
        //    rejects at the call rather than at validation, so the resize throws from inside Resize() with
        //    a message about a missing descriptor member rather than about the mask.
        layout: Entry.Pipeline.getBindGroupLayout(0),
        entries: [
            { binding: 0, resource: { buffer: Entry.Uniform } },
            { binding: 1, resource: Source.createView() },
            { binding: 2, resource: Entry.Sampler }
        ]
    }));
    Pass.draw(3);
    Pass.end();
    Device.queue.submit([Encoder.finish()]);

    return { Texture, View: Target };
}

// The resolutions this device can actually allocate, as {Value, Label} for the dropdown.
//
// 🔴 Filtered against maxTextureDimension2D rather than hard-coded. The reference offers up to 8K, which is
//    exactly the default WebGPU limit — so it is allowable but only just, and a device reporting less would
//    fail validation inside createTexture with an error naming the texture, not the menu that asked for it.
export function ResolveResolutionOptions(Device)
{
    const Ceiling = Device?.limits?.maxTextureDimension2D ?? 8192;
    return [
        { Value: 512,  Label: "512" },
        { Value: 1024, Label: "1K"  },
        { Value: 2048, Label: "2K"  },
        { Value: 4096, Label: "4K"  },
        { Value: 8192, Label: "8K"  }
    ].filter(O => O.Value <= Ceiling);
}
