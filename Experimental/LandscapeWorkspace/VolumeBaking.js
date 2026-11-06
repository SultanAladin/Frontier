/*==========================================================================================================================================
                                                            VOLUMEBAKING.JS
==========================================================================================================================================*/
// 🧩 Background sparse-volume sampling with streaming gzip compression.

import { VolumeSpecification, EncodeSparseVolume } from './SparseVolume.js';
import { ValidateDocument } from './DistanceField.js';

self.onmessage = async Event =>
{
    try
    {
        const { Layer, RegionIndex } = Event.data;
        ValidateDocument({ Format: 'FrontierLandscape/1', Layers: [Layer] });
        if (!Layer.Visible || !Layer.Strokes.length || !Layer.Regions[RegionIndex]) throw Error('Choose a painted region on a visible layer.');
        const Specification = VolumeSpecification(Layer, Layer.Regions[RegionIndex]);
        const Compression = new CompressionStream('gzip');
        const Writer = Compression.writable.getWriter();
        let CompressedBytes = 0;
        const Drain = (async () =>
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
        let Progress;
        let Batch = new Uint8Array(1024 * 1024);
        let Offset = 0;
        let LastUpdate = 0;
        for (const Entry of EncodeSparseVolume(Specification))
        {
            if (Offset + Entry.Bytes.byteLength > Batch.byteLength)
            {
                await Writer.write(Batch.subarray(0, Offset));
                Batch = new Uint8Array(1024 * 1024);
                Offset = 0;
            }
            Batch.set(Entry.Bytes, Offset);
            Offset += Entry.Bytes.byteLength;
            Progress = Entry.Progress;
            if (performance.now() - LastUpdate > 150)
            {
                self.postMessage({ Category: 'progress', Progress });
                LastUpdate = performance.now();
            }
        }
        if (Offset) await Writer.write(Batch.subarray(0, Offset));
        await Writer.close();
        await Drain;
        self.postMessage({ Category: 'complete', Progress, Dimensions: Specification.Dimensions, CompressedBytes });
    }
    catch (Failure)
    {
        self.postMessage({ Category: 'failure', Message: Failure.message });
    }
};
