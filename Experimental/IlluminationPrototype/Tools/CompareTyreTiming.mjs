// Paired deterministic CPU physics timing; no claim about GPU or target-hardware FPS.
import { VehicleSolver } from '../Source/VehicleSolver.js';
import { SummarizeDurations } from '../Source/TimingComparison.js';
import os from 'node:os';
import fs from 'node:fs';
const Output = process.argv[2];
const Samples = { Point: [], XPBD: [] };
const Steps = 2400;
for (let Repetition = -1; Repetition < 9; ++Repetition) {
    // Alternate order to reduce a one-sided JIT/temperature advantage.
    for (const Mode of Repetition % 2 ? ['XPBD', 'Point'] : ['Point', 'XPBD']) {
        const Vehicle = new VehicleSolver([]);
        Vehicle.XpbdEnabled = Mode === 'XPBD';
        const Start = performance.now();
        for (let Tick = 0; Tick < Steps; ++Tick)
            Vehicle.Advance({ Throttle: 0.25, Brake: 0, Steer: 0.12 * Math.sin(Tick / 120), Handbrake: false }, 1 / 120);
        const Duration = performance.now() - Start;
        if (Repetition >= 0) Samples[Mode].push(Duration);
    }
}
const Report = { Platform: process.platform, Architecture: process.arch, Cpu: os.cpus()[0]?.model,
    Node: process.version, OuterStep: 1 / 120, InternalStep: 1 / 480, Steps, Repetitions: 9,
    Note: 'Total CPU time for 20 simulated seconds per repetition. Different tyre physics, not a rendering-quality comparison. XPBD curves prewarmed.',
    Results: Object.fromEntries(Object.entries(Samples).map(([Mode, Entries]) => [Mode, { Milliseconds: SummarizeDurations(Entries), RawMilliseconds: Entries }])) };
console.log(JSON.stringify(Report, null, 2));
if (Output) fs.writeFileSync(Output, JSON.stringify(Report, null, 2));
