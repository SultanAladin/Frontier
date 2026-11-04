//============================================================================================================================================
//                                                        AUDIOKERNEL.JS
//============================================================================================================================================
// 🧩 The real-time seam between the synthesiser and the device: a lock-free ring of interleaved stereo samples, the drain algorithm both
//    the AudioWorklet and the ScriptProcessor fallback use, and the shared control words. Nothing here synthesises; it moves finished audio.

// 📝 Why a ring and not "render inside the device callback": a browser audio callback must return in a few hundred microseconds and must never
//    allocate, lock, or touch the simulation. Synthesis happens on a render thread that owns the model; the device only copies samples out of
//    the ring and fades to silence if the producer falls behind. That separation is what makes the sound immune to a slow picture.

export const RingLayout = Object.freeze({
    SampleCapacity:  65536,     // [sample] - per channel, ~1.37 s at 48 kHz
    ControlWords:    32,        // [idx]    - Int32 header before the sample plane
    WriteIndexWord:  0,
    ReadIndexWord:   1,
    UnderrunWord:    2,
    OverrunWord:     3,
    GenerationWord:  4,
    RequestWord:     5,
    ChannelCount:    2
});

// 📝 The AudioWorklet processor source is a string because a worklet module must be a separate script; keeping it here means one file owns the
//    protocol and there is no second copy of it to drift. It references no imports, so it cannot be invalidated by module resolution.
export function ComposeWorkletSource()
{
    return `
class VehicleAcousticsProcessor extends AudioWorkletProcessor
{
    constructor(Options)
    {
        super();
        const Configuration = Options.processorOptions || {};
        this.SampleCapacity = Configuration.SampleCapacity || 65536;
        this.ChannelCount = Configuration.ChannelCount || 2;
        // 📝 Atomics is only legal on a SharedArrayBuffer. When a host refuses cross-origin isolation the buffers arrive as plain ArrayBuffers,
        //    and this flag is what keeps the processor on the message-queue path instead of throwing inside every audio callback.
        this.Control = new Int32Array(Configuration.ControlBuffer || new ArrayBuffer(128));
        this.SamplePlane = new Float32Array(Configuration.SampleBuffer || new ArrayBuffer(4));
        this.RingMode = Configuration.RingMode === true && typeof SharedArrayBuffer !== "undefined" &&
                        this.Control.buffer instanceof SharedArrayBuffer && this.SamplePlane.length > 1;
        this.DrainGain = 1.0;
        this.SilenceRamp = 0.0;
        this.Messages = [];
        this.FallbackQueue = [];
        this.ExpectedWrite = 0;
        this.port.onmessage = (Event) => this.AcceptMessage(Event.data);
        this.port.postMessage({ Type: "ready", SampleRate: sampleRate });
    }

    AcceptMessage(Message)
    {
        if (!Message) return;
        if (Message.Type === "block" && Message.Left)
        {
            this.FallbackQueue.push({ Left: Message.Left, Right: Message.Right, Count: Message.Count });
            if (this.FallbackQueue.length > 64) this.FallbackQueue.shift();
        }
        if (Message.Type === "gain") this.DrainGain = Message.Value;
        if (Message.Type === "reset") { this.ExpectedWrite = 0; this.FallbackQueue.length = 0; }
    }

    DrainFromRing(Left, Right, Count)
    {
        const WriteIndex = Atomics.load(this.Control, 0);
        const ReadIndex = Atomics.load(this.Control, 1);
        let Available = WriteIndex - ReadIndex;
        if (Available < 0) Available += this.SampleCapacity;
        const Usable = Math.min(Count, Available);
        let Cursor = ReadIndex;
        for (let Index = 0; Index < Usable; ++Index)
        {
            const SampleIndex = Cursor * 2;
            Left[Index] += this.SamplePlane[SampleIndex] * this.DrainGain;
            Right[Index] += this.SamplePlane[SampleIndex + 1] * this.DrainGain;
            Cursor += 1;
            if (Cursor >= this.SampleCapacity) Cursor = 0;
        }
        Atomics.store(this.Control, 1, Cursor);
        if (Usable < Count)
        {
            Atomics.add(this.Control, 2, Count - Usable);
            return false;
        }
        return true;
    }

    DrainFromQueue(Left, Right, Count)
    {
        let Written = 0;
        while (Written < Count && this.FallbackQueue.length > 0)
        {
            const Block = this.FallbackQueue[0];
            const Take = Math.min(Count - Written, Block.Count);
            for (let Index = 0; Index < Take; ++Index)
            {
                Left[Written + Index] += Block.Left[Index] * this.DrainGain;
                Right[Written + Index] += Block.Right[Index] * this.DrainGain;
            }
            Block.Left = Block.Left.subarray(Take);
            Block.Right = Block.Right.subarray(Take);
            Block.Count -= Take;
            Written += Take;
            if (Block.Count <= 0) this.FallbackQueue.shift();
        }
        if (Written < Count) Atomics.add(this.Control, 2, Count - Written);
        return Written >= Count;
    }

    process(Inputs, Outputs)
    {
        const Output = Outputs[0];
        const Left = Output[0];
        const Right = Output.length > 1 ? Output[1] : Output[0];
        const Count = Left.length;
        const Fed = this.RingMode ? this.DrainFromRing(Left, Right, Count) : this.DrainFromQueue(Left, Right, Count);
        // 📝 A short fade instead of a hard cut: an underrun becomes a dip, not a click. The ramp advances PER SAMPLE — a per-block step would
        //    turn a 2.7 ms recovery into a five-second swell — and it is asymmetric, decaying fast and recovering slowly.
        const Target = Fed ? 1.0 : 0.0;
        for (let Index = 0; Index < Count; ++Index)
        {
            if (this.SilenceRamp < Target) this.SilenceRamp = Math.min(Target, this.SilenceRamp + 1.0 / 256.0);
            else if (this.SilenceRamp > Target) this.SilenceRamp = Math.max(Target, this.SilenceRamp - 1.0 / 64.0);
            if (this.SilenceRamp < 0.9999)
            {
                Left[Index] *= this.SilenceRamp;
                Right[Index] *= this.SilenceRamp;
            }
        }
        if (typeof currentFrame === "number" && (currentFrame & 1023) === 0)
        {
            const Underrun = this.RingMode ? Atomics.load(this.Control, 2) : 0;
            this.port.postMessage({ Type: "status", Frame: currentFrame, Underrun });
        }
        return true;
    }
}
registerProcessor("vehicle-acoustics", VehicleAcousticsProcessor);
`;
}

// 📝 The producer side in the worker: interleaved write with wrap. Lock-free because there is exactly one producer and one consumer, both
//    advancing monotonically; the only shared mutation is the write index, published with a store-release via Atomics.
export function WriteRingBlock(Control, SamplePlane, Capacity, Left, Right, Count)
{
    let WriteIndex = Atomics.load(Control, RingLayout.WriteIndexWord);
    const ReadIndex = Atomics.load(Control, RingLayout.ReadIndexWord);
    let Free = Capacity - 1 - ((WriteIndex - ReadIndex + Capacity) % Capacity);
    if (Count > Free)
    {
        Atomics.add(Control, RingLayout.OverrunWord, Count - Free);
        Count = Free;
    }
    let Cursor = WriteIndex;
    for (let Index = 0; Index < Count; ++Index)
    {
        SamplePlane[Cursor * 2] = Left[Index];
        SamplePlane[Cursor * 2 + 1] = Right[Index];
        Cursor += 1;
        if (Cursor >= Capacity) Cursor = 0;
    }
    Atomics.store(Control, RingLayout.WriteIndexWord, Cursor);
    return Count;
}

// 📝 Buffer sizing for the producer: how many samples are queued ahead of the read head. The page displays this, and it is the number that
//    proves the pipeline is decoupled from the frame rate.
export function QueryRingFill(Control, Capacity)
{
    const WriteIndex = Atomics.load(Control, RingLayout.WriteIndexWord);
    const ReadIndex = Atomics.load(Control, RingLayout.ReadIndexWord);
    return ((WriteIndex - ReadIndex) + Capacity) % Capacity;
}
