/*====================================================================================================
                              WATERSIMULATIONVALIDATION.MJS
====================================================================================================*/
// 🧩 Numeric validation of the 2.5D water simulation: a single splash must produce a
// propagating ripple (energy reaches neighbouring cells) while the surface settles
// back under damping. Runs the same WaterSimulation class used by the browser app.

import { WaterSimulation } from '../../../Experimental/TerrainWorldGenerator/js/WaterSimulation.js';
import { writeFileSync } from 'fs';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';
const ResultsDir = join(dirname(fileURLToPath(import.meta.url)), '..', 'Results');

const Sim = new WaterSimulation(64, 8.0); // 512 m span, cheap for validation
Sim.AddSplash(0, 0, 24, 4.0); // [m] peak 4 m at centre

const Centre = 32 * 64 + 32;
const Peak0 = Sim.CurrentHeight[Centre];
if (!(Peak0 > 3.5)) throw new Error('splash peak not registered');

let MaxNeighbour = 0.0;
let MaxMass = 0.0;
let Steps = 0;
for (let S = 0; S < 400; S++)
{
    Sim.Step(0.02);
    Steps++;
    const Ring = [
        Sim.CurrentHeight[(32 - 3) * 64 + 32],
        Sim.CurrentHeight[(32 + 3) * 64 + 32],
        Sim.CurrentHeight[32 * 64 + (32 - 3)],
        Sim.CurrentHeight[32 * 64 + (32 + 3)]
    ];
    const RingMax = Math.max(...Ring.map(Math.abs));
    if (RingMax > MaxNeighbour) MaxNeighbour = RingMax;
    const Mass = Math.abs(Sim.CurrentHeight.reduce((A, B) => A + B, 0));
    if (Mass > MaxMass) MaxMass = Mass;
    if (Math.abs(Sim.CurrentHeight[Centre]) < 0.05 && RingMax < 0.02) break;
}

const Settled = Math.abs(Sim.CurrentHeight[Centre]);
const InitialMass = 4.0 * 2 * Math.PI * (24 / 8) ** 2; // approx splash mass [m * cells]
const Result =
{
    Cells: 64,
    CellSizeM: 8.0,
    Peak0M: Peak0,
    Steps: Steps,
    SimTimeS: Steps * 0.02,
    MaxNeighbourM: MaxNeighbour,
    SettledM: Settled,
    InitialMassM: InitialMass,
    MaxMassM: MaxMass,
    RippleReachedNeighbours: MaxNeighbour > 0.15,
    DampedToRest: Settled < 0.05,
    MassBounded: MaxMass <= InitialMass * 2.0
};
Result.Pass = Result.RippleReachedNeighbours && Result.DampedToRest && Result.MassBounded;

writeFileSync(join(ResultsDir, 'WaterValidation.json'), JSON.stringify(Result, null, 2));
console.log(JSON.stringify(Result, null, 2));
process.exit(Result.Pass ? 0 : 1);
