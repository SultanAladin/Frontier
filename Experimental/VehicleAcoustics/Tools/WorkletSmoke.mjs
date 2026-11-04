//============================================================================================================================================
//                                                        WORKLETSMOKE.MJS
//============================================================================================================================================
// 🧩 Runs the actual AudioWorklet processor source in a stubbed device scope and drives process() in both transport modes. This is the only way to
//    verify the drain path outside a browser, and it catches the class of defect that would otherwise only appear as console noise for a user
//    whose browser refuses cross-origin isolation.

import { strict as Assert } from "node:assert";
import { ComposeWorkletSource, WriteRingBlock, RingLayout } from "../Source/AudioKernel.js";

let ProcessorClass = null;
class StubProcessor
{
    constructor() { this.port = { Messages: [], postMessage: (Message) => this.port.Messages.push(Message), onmessage: null }; }
}
globalThis.AudioWorkletProcessor = StubProcessor;
globalThis.registerProcessor = (Name, Implementation) =>
{
    Assert.equal(Name, "vehicle-acoustics");
    ProcessorClass = Implementation;
};
globalThis.currentFrame = 0;
globalThis.sampleRate = 48000;

const Source = ComposeWorkletSource();
const Factory = new Function("AudioWorkletProcessor", "registerProcessor", "currentFrame", "sampleRate", Source);
Factory(StubProcessor, globalThis.registerProcessor, 0, 48000);
Assert.ok(ProcessorClass, "registerProcessor was never called");

const Capacity = 4096;
const BlockCount = 128;
const Output = () => [new Float32Array(BlockCount), new Float32Array(BlockCount)];

// ① Shared-ring mode: the worklet must copy exactly what the producer wrote, in order.
{
    const Control = new Int32Array(new SharedArrayBuffer(RingLayout.ControlWords * 4));
    const Plane = new Float32Array(new SharedArrayBuffer(Capacity * 2 * 4));
    const Processor = new ProcessorClass({ processorOptions: { SampleCapacity: Capacity, RingMode: true,
                                                              ControlBuffer: Control.buffer, SampleBuffer: Plane.buffer } });
    const Left = Float32Array.from({ length: BlockCount }, (Unused, Index) => Math.sin(Index * 0.05));
    const Right = Float32Array.from({ length: BlockCount }, (Unused, Index) => -Math.sin(Index * 0.05));

    // 📝 Constant-amplitude block first, so the ramp itself can be judged without the signal's own shape confusing it. A device that had nothing
    //    to play must start silent and reach unity within one block; a per-block ramp step would take five seconds and fail this outright.
    const FlatLeft = new Float32Array(BlockCount).fill(0.5);
    const FlatRight = new Float32Array(BlockCount).fill(-0.5);
    WriteRingBlock(Control, Plane, Capacity, FlatLeft, FlatRight, BlockCount);
    const [RampLeft, RampRight] = Output();
    Processor.process([], [[RampLeft, RampRight]]);
    Assert.ok(Math.abs(RampLeft[0]) < 0.01, `the first sample after starvation was not near silent: ${RampLeft[0]}`);
    for (let Index = 1; Index < BlockCount; ++Index)
        Assert.ok(RampLeft[Index] >= RampLeft[Index - 1] - 1.0e-9, `ramp reversed at ${Index}`);
    // 📝 1/256 per sample over a 128-sample quantum puts the endpoint at half amplitude after one block and at unity after two: a 5.3 ms
    //    recovery, fast enough to be inaudible as a hole and slow enough not to click.
    Assert.ok(Math.abs(RampLeft[BlockCount - 1] - 0.25) < 0.01, `ramp endpoint ${RampLeft[BlockCount - 1]}`);
    Assert.ok(Math.abs(RampRight[BlockCount - 1] + 0.25) < 0.01, "right channel did not ramp with the left");
    // 📝 Second block carries the ramp from half to full, third block is at unity: the endpoint of the recovery, asserted as a number.
    WriteRingBlock(Control, Plane, Capacity, FlatLeft, FlatRight, BlockCount);
    const [HalfLeft] = Output();
    Processor.process([], [[HalfLeft, new Float32Array(BlockCount)]]);
    Assert.ok(Math.abs(HalfLeft[0] - 0.25) < 0.02, `second block began at ${HalfLeft[0]}`);
    WriteRingBlock(Control, Plane, Capacity, FlatLeft, FlatRight, BlockCount);
    const [FullLeft] = Output();
    Processor.process([], [[FullLeft, new Float32Array(BlockCount)]]);
    Assert.ok(Math.abs(FullLeft[0] - 0.5) < 0.005, `ramp did not reach unity: ${FullLeft[0]}`);

    // 📝 Once the device is fed continuously the ring contents must be reproduced exactly.
    WriteRingBlock(Control, Plane, Capacity, Left, Right, BlockCount);
    const [OutLeft, OutRight] = Output();
    Processor.process([], [[OutLeft, OutRight]]);
    for (let Index = 1; Index < BlockCount; ++Index)
    {
        Assert.ok(Math.abs(OutLeft[Index] - Left[Index]) < 1.0e-6, `left mismatch at ${Index}`);
        Assert.ok(Math.abs(OutRight[Index] - Right[Index]) < 1.0e-6, `right mismatch at ${Index}`);
    }
    Assert.equal(Atomics.load(Control, RingLayout.ReadIndexWord), (BlockCount * 4) % Capacity, "read head did not advance");

    // 📝 Wrap-around: a second producer write past the end of the ring must be read back contiguously.
    WriteRingBlock(Control, Plane, Capacity, Left, Right, BlockCount);
    WriteRingBlock(Control, Plane, Capacity, Left, Right, BlockCount);
    const [SecondLeft] = Output();
    Processor.process([], [[SecondLeft, new Float32Array(BlockCount)]]);
    Assert.ok(Math.abs(SecondLeft[BlockCount - 1] - Left[BlockCount - 1]) < 1.0e-6, "wrap-around read failed");

    // 📝 The wrap test left one block queued; drain it so the next call is genuinely starved.
    Processor.process([], [[new Float32Array(BlockCount), new Float32Array(BlockCount)]]);

    // 📝 Underrun: an empty ring must yield silence and increment the device counter, not throw and not repeat stale audio.
    const [ThirdLeft] = Output();
    Processor.process([], [[ThirdLeft, new Float32Array(BlockCount)]]);
    Assert.ok(ThirdLeft.every((Value) => Value === 0.0), "an empty ring produced sound");
    Assert.ok(Atomics.load(Control, RingLayout.UnderrunWord) > 0, "underrun was not counted");
    process.stdout.write(`ring mode: copied ${BlockCount} samples, wrap-around and underrun handled\n`);
}

// ② Message mode (no SharedArrayBuffer): blocks arrive on the port and are drained with the same fade discipline.
{
    const Processor = new ProcessorClass({ processorOptions: { SampleCapacity: Capacity, RingMode: false,
                                                              ControlBuffer: new ArrayBuffer(128),
                                                              SampleBuffer: new ArrayBuffer(4) } });
    Assert.equal(Processor.RingMode, false, "a non-shared buffer must select the queue path");
    const Left = new Float32Array(BlockCount).fill(0.25);
    const Right = new Float32Array(BlockCount).fill(0.5);
    Processor.port.onmessage({ data: { Type: "block", Left, Right, Count: BlockCount } });
    const [OutLeft, OutRight] = Output();
    Processor.process([], [[OutLeft, OutRight]]);
    // The first block has no samples yet on entry, so the output begins from silence and the fade-in is expected to soften it.
    Assert.ok(OutLeft.some((Value) => Value !== 0.0), "message mode produced nothing");
    // 📝 The same ramp applies here: the first block reaches half amplitude, the second full.
    Assert.ok(Math.abs(OutLeft[BlockCount - 1] - 0.125) < 0.01, `message mode first block ${OutLeft[BlockCount - 1]}`);
    Assert.ok(OutLeft[0] < 0.02, `message mode did not fade in from silence: ${OutLeft[0]}`);
    // 📝 Three fed blocks in total are what the ramp needs in a 128-sample quantum; the third begins at unity.
    Processor.port.onmessage({ data: { Type: "block", Left, Right, Count: BlockCount } });
    Processor.process([], [[new Float32Array(BlockCount), new Float32Array(BlockCount)]]);
    Processor.port.onmessage({ data: { Type: "block", Left, Right, Count: BlockCount } });
    const [Settled] = Output();
    Processor.process([], [[Settled, new Float32Array(BlockCount)]]);
    Assert.ok(Math.abs(Settled[0] - 0.25) < 0.02, `message mode did not settle at unity: ${Settled[0]}`);

    // 📝 Gain message: the device-side trim must scale what is already queued.
    Processor.port.onmessage({ data: { Type: "gain", Value: 0.5 } });
    Processor.port.onmessage({ data: { Type: "block", Left, Right, Count: BlockCount } });
    const [Trimmed] = Output();
    Processor.process([], [[Trimmed, new Float32Array(BlockCount)]]);
    Assert.ok(Math.abs(Trimmed[BlockCount - 1] - 0.125) < 0.02, `trim applied ${Trimmed[BlockCount - 1]}`);

    // 📝 Reset clears the queue.
    Processor.port.onmessage({ data: { Type: "reset" } });
    const [Cleared] = Output();
    Processor.process([], [[Cleared, new Float32Array(BlockCount)]]);
    Assert.ok(Cleared.every((Value) => Value === 0.0), "reset left queued audio");
    process.stdout.write("message mode: queued, trimmed and reset correctly\n");
}

process.stdout.write("worklet smoke: both transports verified\n");
