/*====================================================================================================================================
                                                      STROKEDRIVER.JS
====================================================================================================================================*/
// 🧩 Turn a pointer drag into spaced dabs on the surface, laying them into the atlas as the stroke runs

import { PickFromPointer, ResolveCoordinate } from "./SurfacePick.js";
import { DecomposeSegment, DefaultSpacing }   from "../Deposit/DabFootprint.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Drives one painting session against a surface, a paint pass and a stroke ledger.
//
// 📝 The driver owns only stroke continuity — the carry distance and the previous anchor. Everything
//    persistent lives in the ledger, and everything about brush appearance lives in the brush the
//    caller hands in, so changing a slider mid-session needs no driver state at all.
export class StrokeDriver
{
    constructor(Device, Pass, Ledger)
    {
        this.Device = Device;
        this.Pass   = Pass;
        this.Ledger = Ledger;

        this.Stroke      = null;
        this.PriorAnchor = null;
        this.Carry       = 0;

        // Dabs awaiting a flush to the GPU this frame.
        this.Pending = [];
    }

    get Painting() { return this.Stroke !== null; }

    // Open a stroke at a pointer position. Returns false when the pointer missed the surface.
    Begin(Context, PointerX, PointerY, Pressure)
    {
        const Hit = this.Probe(Context, PointerX, PointerY);
        if (Hit === null) { return false; }

        this.Stroke      = this.Ledger.Begin(Context.Brush);
        this.Carry       = 0;
        this.PriorAnchor = AnchorFromHit(Hit, Pressure);

        // 📝 The opening dab is stamped unconditionally. Without it a click that never moves leaves
        //    no mark at all, because DecomposeSegment only emits dabs strictly along a segment.
        this.Commit([this.PriorAnchor]);
        return true;
    }

    // Extend the open stroke to a new pointer position.
    Extend(Context, PointerX, PointerY, Pressure)
    {
        if (this.Stroke === null) { return 0; }

        const Hit = this.Probe(Context, PointerX, PointerY);

        // 📝 Dragging off the silhouette breaks stroke continuity rather than bridging across the gap.
        //    Bridging would draw a straight line through empty space and reattach on the far side of
        //    the model, which reads as the brush "jumping" to the other cheek.
        if (Hit === null) { this.PriorAnchor = null; this.Carry = 0; return 0; }

        const Anchor = AnchorFromHit(Hit, Pressure);

        if (this.PriorAnchor === null)
        {
            this.PriorAnchor = Anchor;
            this.Commit([Anchor]);
            return 1;
        }

        const Walk = DecomposeSegment(
            this.PriorAnchor, Anchor,
            Context.Brush.Radius,
            Context.Brush.Spacing ?? DefaultSpacing,
            this.Carry);

        this.Carry       = Walk.Carry;
        this.PriorAnchor = Anchor;

        // 📝 Any dab whose endpoints sat on different triangles carries a provisional coordinate that
        //    may have been mixed across a UV seam. Replace those with the surface's own UV. Only the
        //    flagged dabs pay the mesh sweep; a stroke that stays on one triangle pays nothing.
        for (const Dab of Walk.Dabs)
        {
            if (Dab.CoordinateExact) { continue; }

            const Resolved = ResolveCoordinate(Context.Decoded, Dab.Position);
            if (Resolved !== null) { Dab.Coordinate = Resolved; }
        }

        this.Commit(Walk.Dabs);
        return Walk.Dabs.length;
    }

    // Close the stroke. The record keeps it; the ledger cursor already includes it.
    Finish()
    {
        const Closed = this.Stroke;
        this.Stroke      = null;
        this.PriorAnchor = null;
        this.Carry       = 0;
        return Closed;
    }

    // Cast the pointer at the surface.
    Probe(Context, PointerX, PointerY)
    {
        return PickFromPointer(
            Context.Decoded, Context.Resolved,
            PointerX, PointerY,
            Context.SurfaceWidth, Context.SurfaceHeight);
    }

    // Record dabs and queue them for the GPU.
    Commit(Dabs)
    {
        if (Dabs.length === 0) { return; }

        for (const Dab of Dabs) { this.Stroke.Append(Dab); }
        this.Pending.push(...Dabs);
    }

    // Lay every queued dab into the atlas. Returns how many were drawn.
    //
    // 📝 Flushed once per frame rather than per pointer event: a 240 Hz stylus would otherwise submit
    //    several command buffers per frame for no benefit.
    Flush(AtlasView, Surface, Brush)
    {
        if (this.Pending.length === 0) { return 0; }

        let Drawn = 0;

        while (this.Pending.length > 0)
        {
            const Batch   = this.Pending.splice(0, 256);
            const Count   = this.Pass.StageDabs(Batch, Brush);
            const Encoder = this.Device.createCommandEncoder({ label: "StrokeFlush" });

            this.Pass.Encode(Encoder, AtlasView, Surface, Count);
            this.Device.queue.submit([Encoder.finish()]);

            Drawn += Count;
        }

        return Drawn;
    }

    // Lay every queued dab into a LAYER, fanning across the channel atlases the layer paints.
    //
    // 🔴 The dabs are staged once per atlas, not once per stroke. Each atlas needs a different deposited
    //    value and a different write mask, and both live in the uniform block — so the same batch is
    //    re-staged with a different Write before each atlas's draw. Staging once and reusing it would
    //    paint every atlas with whichever channel happened to be staged last.
    //
    // 🔴 The batch is spliced out ONCE and reused across the atlases. Splicing inside the atlas loop
    //    would hand each atlas a different slice of the stroke — colour would get the first 256 dabs and
    //    roughness the next 256, so the two channels would be painted along disjoint parts of the line.
    FlushToLayer(Layer, Surface, Brush, ResolveAtlasWrite, ChannelAtlases)
    {
        if (this.Pending.length === 0) { return 0; }
        if (!Layer)                    { this.Pending.length = 0; return 0; }

        // 🔴 The MASK branch comes FIRST, ahead of the Paintable guard, and the order is the whole point.
        //    Paintable describes the layer's CHANNEL atlases — a fill or material layer owns its content
        //    and must not take brush ink into it. Its mask is a different surface with the opposite rule:
        //    masking a flat fill or a material preset is the single most common thing anyone does with a
        //    mask. Guarding first would drop exactly those strokes, and the refusal counter would report
        //    the drop as if the user had aimed at an unpaintable channel.
        //
        // 🔴 A mask stroke also goes nowhere near ResolveAtlasWrite. The mask is one greyscale target, so
        //    it takes a single pass with an all-components write rather than the per-atlas loop below, and
        //    the layer's Enabled channel set is IRRELEVANT to it. Routing it through the channel writer
        //    would silently drop it on any layer that does not paint colour — most generator and material
        //    layers — so the mask would appear dead on precisely the layers people most want to mask.
        if (Layer.Mask?.Target === true)
        {
            return this.FlushToMask(Layer, Surface, Brush);
        }

        // 🔴 Only a PAINT layer takes a stroke into its channels. A fill, material or generator layer's
        //    content is its authored value or its procedural pass, and letting a dab land on one would
        //    silently overwrite a patch of it with brush ink — the layer would then be neither the preset
        //    nor hand-painted, and nothing in the UI would explain why. The pending dabs are DROPPED
        //    rather than redirected onto some other layer, because silently painting somewhere the user
        //    did not aim is worse than painting nowhere. The caller reports the refusal.
        if (Layer.Paintable === false)
        {
            this.Pending.length = 0;
            this.Refused = (this.Refused ?? 0) + 1;
            return 0;
        }

        let Drawn = 0;

        while (this.Pending.length > 0)
        {
            const Batch = this.Pending.splice(0, 256);

            for (const Descriptor of ChannelAtlases)
            {
                const Write = ResolveAtlasWrite(Descriptor.Key, Layer.Enabled, Layer.Values, Brush.Ink);
                if (Write === null) { continue; }

                // 📝 THIS is where a paint layer earns its storage. Atlases are allocated lazily, so a
                //    layer that has never been stroked holds no textures at all; the first dab into a
                //    channel is what brings its 4 MiB into existence.
                const View = Layer.EnsureAtlas
                    ? Layer.EnsureAtlas(Descriptor.Key)
                    : Layer.AtlasView[Descriptor.Key];
                if (!View) { continue; }

                const Count   = this.Pass.StageDabs(Batch, Brush, Write);
                const Encoder = this.Device.createCommandEncoder({ label: `StrokeFlush${Descriptor.Key}` });

                this.Pass.Encode(Encoder, View, Surface, Count, Write);
                this.Device.queue.submit([Encoder.finish()]);

                // 📝 Counted once per dab, not once per (dab × atlas). The caller uses this to decide
                //    whether to redraw, and inflating it by the atlas count would misreport the stroke.
                if (Descriptor === ChannelAtlases[0]) { Drawn += Count; }
            }

            // A layer whose first atlas paints nothing still has to count its dabs, or a roughness-only
            // stroke reports zero and the viewport never redraws.
            if (Drawn === 0) { Drawn += Batch.length; }
        }

        return Drawn;
    }

    // Lay the pending dabs into the layer's greyscale MASK instead of its channel atlases.
    //
    // 📝 White paints the layer in, black paints it out — the compositor multiplies this into the layer's
    //    coverage, so black there means "show whatever is beneath". Nothing destructive happens: the
    //    channel atlases are never touched by a mask stroke, which is the entire reason to mask rather
    //    than erase.
    FlushToMask(Layer, Surface, Brush)
    {
        // 🔴 EnsureMaskAtlas, so painting a mask allocates it exactly like a first stroke allocates a
        //    channel. Requiring the user to "add" a mask before the brush would work is a state the UI
        //    would have to explain; this way the mask exists the moment it is painted.
        const View = Layer.EnsureMaskAtlas ? Layer.EnsureMaskAtlas() : Layer.MaskView;
        if (!View) { this.Pending.length = 0; return 0; }

        // 🔴 A mask stroke's INK IS ITS VALUE, and it is greyscale by construction rather than by
        //    convention. The brush's colour is meaningless in a mask — only lightness matters — so
        //    Erase paints black and paint paints white, and the brush's own hue is deliberately ignored.
        //    Passing Brush.Ink through unchanged would let a red brush write (1,0,0), whose .r reads as
        //    fully-shown while it clearly looks red in any mask thumbnail: the mask would then disagree
        //    with its own preview.
        const Level = Brush.Erase ? 0.0 : 1.0;

        // 🔴 All three components written, and alpha with them. The compositor samples .r, but the
        //    thumbnail and any future filter read the whole texel; leaving g/b at the clear value would
        //    make the mask read as coloured everywhere it was painted.
        //
        // 🔴 The greyscale level rides in Write.Value, NOT in the brush's ink, because StageDabs prefers
        //    Write.Value over Brush.Ink whenever a Write is supplied. Substituting a greyscale ink on a
        //    copied brush looks like the natural way to express this and is dead code — the ink would
        //    never be read, and the mask would silently take its value from whatever Write said instead.
        const Write = { Value: [Level, Level, Level], Mask: [1, 1, 1], Clear: [Level, Level, Level, 1] };

        // 📝 Brush passed through UNMODIFIED: the mask stroke borrows its geometry (radius, hardness,
        //    falloff, flow, pressure) so it feels identical under the hand, and only the deposited value
        //    differs. Brush.Erase is staged but the paint shader does not branch on it — erase there is
        //    already "paint a different colour" — so black-as-erase needs no special handling.
        let Drawn = 0;

        while (this.Pending.length > 0)
        {
            const Batch   = this.Pending.splice(0, 256);
            const Count   = this.Pass.StageDabs(Batch, Brush, Write);
            const Encoder = this.Device.createCommandEncoder({ label: "StrokeFlushMask" });

            this.Pass.Encode(Encoder, View, Surface, Count, Write);
            this.Device.queue.submit([Encoder.finish()]);

            Drawn += Count > 0 ? Count : Batch.length;
        }

        return Drawn;
    }
}

// Build a dab anchor from a surface hit.
function AnchorFromHit(Hit, Pressure)
{
    return {
        Position:   Hit.Position,
        Normal:     Hit.Normal,
        Coordinate: Hit.Coordinate,
        // 📝 Carried so DecomposeSegment can tell a within-triangle blend from one that may cross a UV
        //    seam. Two anchors on the same triangle cannot straddle a seam, so their coordinates
        //    interpolate exactly; across triangles the blend has to snap instead of mixing.
        Triangle:   Hit.Triangle,
        // 📝 Raw pressure, uncurved. The response curve is presentational and is applied at render
        //    time so a recorded stroke can be replayed under a different curve.
        Pressure:   Pressure ?? 1.0
    };
}
