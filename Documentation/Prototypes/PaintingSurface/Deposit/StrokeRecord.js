/*====================================================================================================================================
                                                      STROKERECORD.JS
====================================================================================================================================*/
// 🧩 Persist strokes as object-space dabs with raw pressure, so a stroke can be replayed at any resolution

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// Provenance of a recorded sample. 📝 Predicted samples must never be committed to the record —
//    they are speculative positions that the real input stream will contradict.
export const SampleProvenance = {
    Measured:  "measured",
    Predicted: "predicted",
    Replayed:  "replayed"
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// A single stroke: the brush it was laid with, and the dabs it emitted.
//
// 🔴 Two rules from the research govern this structure, and both are easy to violate silently:
//    1. Everything spatial is in OBJECT space, never device pixels. A pixel-space record cannot be
//       replayed at another viewport size, zoom, or export resolution.
//    2. Pressure is stored RAW. The response curve is presentational and is applied at render time,
//       so baking it in makes the stroke unreplayable under a different curve.
export class StrokeRecord
{
    constructor(Brush, Ordinal)
    {
        this.Ordinal = Ordinal;

        // A copy, not a reference — the live brush keeps changing after the stroke is committed.
        this.Brush = {
            Radius:       Brush.Radius,
            Hardness:     Brush.Hardness,
            Flow:         Brush.Flow,
            Spacing:      Brush.Spacing,
            NormalCutoff: Brush.NormalCutoff,
            Ink:          [Brush.Ink[0], Brush.Ink[1], Brush.Ink[2]],
            Erase:        Brush.Erase === true
        };

        this.Dabs      = [];
        this.Provenance = SampleProvenance.Measured;
    }

    Append(Dab)
    {
        this.Dabs.push({
            Position:   [Dab.Position[0], Dab.Position[1], Dab.Position[2]],
            Normal:     [Dab.Normal[0],   Dab.Normal[1],   Dab.Normal[2]],
            Coordinate: [Dab.Coordinate[0], Dab.Coordinate[1]],
            Pressure:   Dab.Pressure ?? 1.0
        });
    }

    get DabCount() { return this.Dabs.length; }
}

// The whole painting session: an ordered run of strokes, plus a cursor for undo and redo.
//
// 📝 Undo moves the cursor rather than deleting, so redo is a cursor move too. Replay from the base
//    atlas is what actually reconstitutes the image — see ReplayInto below.
export class StrokeLedger
{
    constructor()
    {
        this.Strokes = [];
        this.Cursor  = 0;          // How many strokes are currently applied.
        this.Sequence = 0;
    }

    Begin(Brush)
    {
        // 🔴 Recording behind the cursor discards the redo tail. Without this, an undo followed by a
        //    new stroke would leave orphaned strokes that replay would resurrect.
        if (this.Cursor < this.Strokes.length) { this.Strokes.length = this.Cursor; }

        this.Sequence += 1;
        const Stroke = new StrokeRecord(Brush, this.Sequence);

        this.Strokes.push(Stroke);
        this.Cursor = this.Strokes.length;

        return Stroke;
    }

    Undo()
    {
        if (this.Cursor <= 0) { return false; }
        this.Cursor -= 1;
        return true;
    }

    Redo()
    {
        if (this.Cursor >= this.Strokes.length) { return false; }
        this.Cursor += 1;
        return true;
    }

    // The strokes that are currently live, in the order they were laid.
    ActiveStrokes()
    {
        return this.Strokes.slice(0, this.Cursor);
    }

    get StrokeCount()   { return this.Strokes.length; }
    get ActiveCount()   { return this.Cursor; }
    get TotalDabCount() { return this.Strokes.reduce((Sum, Stroke) => Sum + Stroke.DabCount, 0); }

    // Serialize to a plain object. 📝 Nothing here is resolution-bound, which is the whole point of
    //    the record: the same document replays into a 512² or an 8K atlas.
    Describe()
    {
        return {
            Sequence: this.Sequence,
            Cursor:   this.Cursor,
            Strokes:  this.Strokes.map(Stroke => ({
                Ordinal: Stroke.Ordinal,
                Brush:   Stroke.Brush,
                Dabs:    Stroke.Dabs
            }))
        };
    }
}

// Replay the live strokes into the atlas through a paint pass.
//
// 📝 Replay is what makes undo correct: rather than storing pixels, the atlas is reset to its base
//    and every surviving stroke is re-laid. The cost is bounded by stroke count, and the result is
//    exact rather than an approximation of the pixels that were overwritten.
export function ReplayInto(Device, Pass, AtlasView, Surface, Ledger)
{
    const Strokes = Ledger.ActiveStrokes();
    let   Laid    = 0;

    for (const Stroke of Strokes)
    {
        for (let Start = 0; Start < Stroke.Dabs.length; Start += 256)
        {
            const Batch   = Stroke.Dabs.slice(Start, Start + 256);
            const Count   = Pass.StageDabs(Batch, Stroke.Brush);
            const Encoder = Device.createCommandEncoder({ label: "StrokeReplay" });

            Pass.Encode(Encoder, AtlasView, Surface, Count);
            Device.queue.submit([Encoder.finish()]);

            Laid += Count;
        }
    }

    return Laid;
}
