/*==========================================================================================================================================
                                                            VOLUMEBAKING.JS
==========================================================================================================================================*/
// 🧩 Background conservative/metric sparse-volume sampling, convergent 3D redistancing and streaming gzip export.

import { VolumeSpecification, EncodeSparseVolume, SampleSparseVolume, EncodeSampledVolume } from './SparseVolume.js';
import { ValidateDocument, VolumeDistance } from './DistanceField.js';
import { ReinitializeSparseVolume } from './DistanceReinitialization.js';

self.onmessage = async Event =>
{
    let Writer = null, Drain = null;
    try
    {
        const { Layer, RegionIndex } = Event.data;
        ValidateDocument({ Format: 'FrontierLandscape/1', Layers: [Layer] });
        if (!Layer.Visible || !Layer.Strokes.length || !Layer.Regions[RegionIndex]) throw Error('Choose a painted region on a visible layer.');
        let Specification = VolumeSpecification(Layer, Layer.Regions[RegionIndex]);
        let Progress, Encoding;
        let LastUpdate = 0;
        const BroadcastProgress = (Next, Force = false) =>
        {
            Progress = Next;
            if (!Force && performance.now() - LastUpdate < 150) return;
            self.postMessage({ Category: 'progress', Progress });
            LastUpdate = performance.now();
        };
        if (Layer.DistanceMode === 'metric')
        {
            const Volume = { Specification, Leaves: [] };
            for (const Entry of SampleSparseVolume(Specification))
            {
                if (Entry.Leaf) Volume.Leaves.push(Entry.Leaf);
                BroadcastProgress({ ...Entry.Progress, Completion: Entry.Progress.Fraction * .4 });
            }
            const Sampling = Progress;
            const Exterior = (X, Y, Z) => VolumeDistance(Layer, X, Y, Z, Layer.Regions[RegionIndex]);
            const Surface = (X, Y, Z) => VolumeDistance(Layer, X, Y, Z, Layer.Regions[RegionIndex], false);
            for (const Metric of ReinitializeSparseVolume(Volume, { Exterior, Surface }))
            {
                const Completion = Metric.Phase === 'seeding' ? .4 + Metric.Fraction * .1 :
                    Metric.Phase === 'sweeping' ? .5 + .4 * (Metric.Cycle - 1 + Metric.Fraction) / Metric.MaximumCycles : .9;
                BroadcastProgress({ ...Sampling, ...Metric, Completion }, Metric.Phase === 'redistanced');
            }
            Specification = Volume.Specification;
            Encoding = EncodeSampledVolume(Volume);
        }
        else Encoding = EncodeSparseVolume(Specification);

        // 📝 A metric archive starts only after convergence and sign preservation succeed. Failures never export a partial result.
        const Compression = new CompressionStream('gzip');
        Writer = Compression.writable.getWriter();
        let CompressedBytes = 0;
        Drain = (async () =>
        {
            const Reader = Compression.readable.getReader();
            while (true)
            {
                const Read = await Reader.read();
                if (Read.done) break;
                const Chunk = Read.value;
                CompressedBytes += Chunk.byteLength;
                self.postMessage({ Category: 'chunk', Chunk }, [Chunk.buffer]);
            }
        })();
        // Attach a rejection consumer immediately; the awaited original promise still reports compression failure.
        Drain.catch(() => {});
        let Batch = new Uint8Array(1024 * 1024);
        let Offset = 0;
        for (const Entry of Encoding)
        {
            if (Offset + Entry.Bytes.byteLength > Batch.byteLength)
            {
                await Writer.write(Batch.subarray(0, Offset));
                Batch = new Uint8Array(1024 * 1024);
                Offset = 0;
            }
            Batch.set(Entry.Bytes, Offset);
            Offset += Entry.Bytes.byteLength;
            const Completion = Layer.DistanceMode === 'metric' ? .9 + Entry.Progress.Fraction * .1 : Entry.Progress.Fraction;
            BroadcastProgress({ ...Entry.Progress, Completion });
        }
        if (Offset) await Writer.write(Batch.subarray(0, Offset));
        await Writer.close();
        await Drain;
        self.postMessage({
            Category: 'complete', Progress, Dimensions: Specification.Dimensions, CompressedBytes,
            Format: Specification.Format, Redistance: Specification.Redistance || null
        });
    }
    catch (Failure)
    {
        if (Writer) await Writer.abort(Failure).catch(() => {});
        if (Drain) await Drain.catch(() => {});
        self.postMessage({ Category: 'failure', Message: Failure.message });
    }
};
