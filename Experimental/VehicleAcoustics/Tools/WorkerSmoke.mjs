//============================================================================================================================================
//                                                        WORKERSMOKE.MJS
//============================================================================================================================================
// 🧩 Runs the real render worker in a stubbed worker scope and pulls the shared ring exactly as the device would. This exercises the audio path
//    that the browser uses — worker boot, vehicle configuration, ring writes, telemetry — without a sound card.

import { strict as Assert } from "node:assert";
import { RingLayout, ComposeWorkletSource } from "../Source/AudioKernel.js";

const Messages = [];
const Scope = {
    postMessage: (Message) => Messages.push(Message),
    onmessage: null
};
globalThis.self = Scope;
globalThis.importScripts = () => {};

// 📝 The worker module assigns self.onmessage at import time, so it must be imported after the scope exists.
await import("../Source/RenderWorker.js");
Assert.equal(typeof Scope.onmessage, "function", "the worker did not install a message handler");

const Capacity = RingLayout.SampleCapacity;
const ControlBuffer = new SharedArrayBuffer(RingLayout.ControlWords * 4);
const SampleBuffer = new SharedArrayBuffer(Capacity * 2 * 4);
const Control = new Int32Array(ControlBuffer);
const SamplePlane = new Float32Array(SampleBuffer);

Scope.onmessage({ data: { Type: "configure", SampleRate: 48000, Identifier: "gt-rNismoGt3", Position: "exterior",
                          Gain: 1.0, AutoShift: true, Capacity, ControlBuffer, SampleBuffer } });
const Configured = Messages.find((Entry) => Entry.Type === "configured");
Assert.ok(Configured, "no configured message after boot");
Assert.ok(Configured.Report.includes("GT-R"), `unexpected report: ${Configured.Report}`);
Assert.ok(Configured.Ceiling.NyquistHz === 24000);

// 📝 Act as the device: pull a quantum every 2.7 ms so the producer has somewhere to write. Without a consumer the ring is meant to stall, which
//    is why this happens before start rather than after.
let Drained = 0;
const DeviceTimer = setInterval(() => { Drained += DrainQuantum(); }, 2.7);
function DrainQuantum()
{
    const Write = Atomics.load(Control, RingLayout.WriteIndexWord);
    const Read = Atomics.load(Control, RingLayout.ReadIndexWord);
    const Available = ((Write - Read) + Capacity) % Capacity;
    const Take = Math.min(Available, 128);
    Atomics.store(Control, RingLayout.ReadIndexWord, (Read + Take) % Capacity);
    return Take;
}

Scope.onmessage({ data: { Type: "input", Input: { Throttle: 1.0 } } });
Scope.onmessage({ data: { Type: "start" } });

// 📝 Let the producer run for a moment of wall time, exactly as the device would.
await new Promise((Resolve) => setTimeout(Resolve, 260));
Scope.onmessage({ data: { Type: "stop" } });
clearInterval(DeviceTimer);

const WriteIndex = Atomics.load(Control, RingLayout.WriteIndexWord);
Assert.ok(WriteIndex > 0, `the producer wrote nothing (${WriteIndex})`);
Assert.ok(Drained > 0, "the device never drained a quantum");
const Telemetry = Messages.filter((Entry) => Entry.Type === "telemetry");
Assert.ok(Telemetry.length >= 1, `expected telemetry, saw ${Telemetry.length}`);
const Latest = Telemetry.at(-1);
Assert.ok(Latest.Vehicle.Identifier === "gt-rNismoGt3");
Assert.ok(Latest.State.RPM > 0, `engine never turned: ${Latest.State.RPM}`);

// 📝 Audio content: the written ring must contain nonzero samples, and no non-finite values.
let Peak = 0.0;
let NonFinite = 0;
for (let Index = 0; Index < Capacity; ++Index)
{
    const Left = SamplePlane[Index * 2];
    const Right = SamplePlane[Index * 2 + 1];
    if (!Number.isFinite(Left) || !Number.isFinite(Right)) ++NonFinite;
    Peak = Math.max(Peak, Math.abs(Left), Math.abs(Right));
}
Assert.equal(NonFinite, 0, "the ring contains non-finite samples");
Assert.ok(Peak > 0.02, `the ring is silent (peak ${Peak})`);
Assert.ok(Peak <= 1.0, `the ring exceeds full scale (peak ${Peak})`);
Assert.equal(Latest.Overrun, 0, `ring overran by ${Latest.Overrun}`);

// 📝 Vehicle switching and the held operating point through the same seam.
Scope.onmessage({ data: { Type: "select", Identifier: "taycanTurbo", Position: "cabin" } });
Scope.onmessage({ data: { Type: "hold", Point: { RPM: 7000, Throttle: 1.0 } } });
const HoldTimer = setInterval(() => { DrainQuantum(); }, 2.7);
Scope.onmessage({ data: { Type: "start" } });
const Before = Atomics.load(Control, RingLayout.WriteIndexWord);
await new Promise((Resolve) => setTimeout(Resolve, 160));
Scope.onmessage({ data: { Type: "stop" } });
clearInterval(HoldTimer);
const After = Atomics.load(Control, RingLayout.WriteIndexWord);
Assert.ok(After !== Before, "the hold path produced no audio");

// 📝 The worklet source must be valid JavaScript and register the processor the host looks for.
const Source = ComposeWorkletSource();
Assert.ok(Source.includes("registerProcessor(\"vehicle-acoustics\""));
Assert.ok(Source.includes("class VehicleAcousticsProcessor"));
new Function("AudioWorkletProcessor", "registerProcessor", "currentFrame", "sampleRate", Source);
Assert.ok(!Source.includes("import "), "the worklet source must be self-contained");

process.stdout.write(`worker smoke: ${Drained} samples drained, peak ${Peak.toFixed(3)}, ` +
                     `${Telemetry.length} telemetry frames, ${Latest.State.RPM.toFixed(0)} rpm, ` +
                     `hold path produced ${After - Before} samples\n`);
process.exit(0);
