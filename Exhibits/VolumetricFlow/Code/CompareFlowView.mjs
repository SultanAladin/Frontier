// Compare native view staging with the retained JavaScript orbit and uniform transcription.
import assert from "node:assert/strict";
import fs from "node:fs";
import vm from "node:vm";
import { spawnSync } from "node:child_process";

const Executable = process.argv[2];
assert.ok(Executable, "Expected the FlowViewValidation executable");
const Html = fs.readFileSync("Documentation/Plans/References/InheritedEngine/Prototypes/VolumetricFlowSolver.html", "utf8");
function Slice(Start, End)
{
    const From = Html.indexOf(Start), To = Html.indexOf(End, From);
    assert.ok(From >= 0 && To > From, `Reference section not found: ${Start}`);
    return Html.slice(From, To);
}
const Reference = Slice("const OrbitCamera =", "const ViewByteCount =") +
    Slice("function TranscribeViewParameterization()", "async function ConstructIrradianceUnit()");
const Cases = [
    [1280,720, 0.62,0.24,2.05,0.62, 0,0.55,46,0.85,0,128,4,96],
    [512,512, -0.9,-0.7,3.2,0.85, 10.5,0,4.5,0,1,16,2,8],
    [1024,400, 2.2,1.1,1.1,0.4, 1.75,0.65,7.25,1,0,10,2,8],
    [640,0, 0,0,0,0.62, 0,0,0,0,0,32,0,16],
    [1,1, -3.14,1.45,10,1.2, -0.5,1.2,100,0.5,1,128,4,96]
];
const Native = spawnSync(Executable, [], { encoding: "utf8", input: Cases.map(Case => Case.join(" ")).join("\n") + "\n" });
assert.equal(Native.status, 0, Native.stderr);
const Outputs = Native.stdout.trim().split("\n").map(Line => JSON.parse(Line));
assert.equal(Outputs.length, Cases.length);
let Maximum = 0;
for (let Index = 0; Index < Cases.length; ++Index)
{
    const Case = Cases[Index], Context = vm.createContext({ Case });
    vm.runInContext(`${Reference}\n
        const [Width, Height, Azimuth, Pitch, Radius, Field, Elapsed, Turbulence, Density, Palette, Selector, March, Shadow, Detail] = Case;
        Object.assign(OrbitCamera, { Azimuth, Pitch, OrbitRadius: Radius, FieldFactor: Field });
        Object.assign(ViewParameterization, { TurbulenceStrength: Turbulence, DensityScale: Density, PaletteBlend: Palette,
            ChannelSelector: Selector, MarchStepCount: March, ShadowStepCount: Shadow, TurbulenceDetail: Detail });
        const ViewStaging = new Float32Array(52), ShadowDirection = [-0.42, 0.80, 0.43];
        const SurfaceExtent = { PixelWidth: Width, PixelHeight: Height }, SolverAssembly = { ElapsedInterval: Elapsed };
        const RenderAssembly = { ViewBuffer: {} }, DeviceProfile = { Device: { queue: { writeBuffer() {} } } };
        TranscribeViewParameterization(); globalThis.Result = Array.from(ViewStaging);
    `, Context);
    for (let Slot = 0; Slot < 52; ++Slot)
    {
        const Delta = Math.abs(Outputs[Index][Slot] - Context.Result[Slot]);
        assert.ok(Delta <= 1e-6 * Math.max(1, Math.abs(Context.Result[Slot])), `View case ${Index} slot ${Slot}: ${Outputs[Index][Slot]} vs ${Context.Result[Slot]}`);
        Maximum = Math.max(Maximum, Delta);
    }
}
console.log(`Flow view parity: ${Cases.length} cameras, ${Cases.length * 52} float slots, max |Δ| ${Maximum}.`);
