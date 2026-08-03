/*====================================================================================================================================
                                                      STROKEDRIVER.JS
====================================================================================================================================*/
// 🧩 Turn a pointer drag into spaced dabs on the surface, laying them into the atlas as the stroke runs

import { PickFromPointer, ResolveCoordinate } from "./SurfacePick.js";
import { DecomposeSegment, DefaultSpacing }   from "../Deposit/DabFootprint.js";
import { ResolveMaskPaintTarget }             from "../Layers/LayerMask.js";

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

        // 🔴 A focused PAINT mask component takes the stroke BEFORE the paintable check, and takes it even
        //    on a fill / material / generator layer. Painting the mask is how the user carves where the
        //    whole layer applies, so a layer whose CONTENT cannot be hand-painted still accepts strokes
        //    into its mask. The dabs land in the component's own greyscale atlas — one texture, not the
        //    channel fan-out — as a flat scalar the mask sequence reads back from red and weights by
        //    coverage. Ink is ignored: a mask has no colour, only where-it-applies.
        const MaskTarget = ResolveMaskPaintTarget(Layer);
        if (MaskTarget)
        {
            const MaskWrite = { Value: [1, 1, 1], Mask: [1, 1, 1] };
            let   MaskDrawn = 0;

            while (this.Pending.length > 0)
            {
                const Batch = this.Pending.splice(0, 256);

                // First dab into this component is what brings its atlas into existence.
                const View = Layer.EnsureMaskComponentAtlas
                    ? Layer.EnsureMaskComponentAtlas(MaskTarget)
                    : MaskTarget.AtlasView;
                if (!View) { continue; }

                const Count   = this.Pass.StageDabs(Batch, Brush, MaskWrite);
                const Encoder = this.Device.createCommandEncoder({ label: `MaskStrokeFlush${MaskTarget.Token}` });

                this.Pass.Encode(Encoder, View, Surface, Count, MaskWrite);
                this.Device.queue.submit([Encoder.finish()]);

                MaskDrawn += Count;
            }

            return MaskDrawn;
        }

        // 🔴 Only a PAINT layer takes a stroke. A fill, material or generator layer's content is its
        //    authored value or its procedural pass, and letting a dab land on one would silently overwrite
        //    a patch of it with brush ink — the layer would then be neither the preset nor hand-painted,
        //    and nothing in the UI would explain why. The pending dabs are DROPPED rather than redirected
        //    onto some other layer, because silently painting somewhere the user did not aim is worse than
        //    painting nowhere. The caller reports the refusal.
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
