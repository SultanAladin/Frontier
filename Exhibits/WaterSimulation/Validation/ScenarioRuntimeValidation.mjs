/*====================================================================================================================================
                                            SCENARIO RUNTIME VALIDATION.MJS
====================================================================================================================================*/
// 🧩 Boots the real prototype modules against a recording WebGL2 stand-in and runs live frames in all three models

import fs from 'fs';
import path from 'path';
import { fileURLToPath, pathToFileURL } from 'url';

const Here = path.dirname(fileURLToPath(import.meta.url));
const SourceRoot = path.resolve(Here, '../../../Experimental/WaterSimulation/src');
const Module = (Name) => pathToFileURL(path.join(SourceRoot, Name)).href;

/*------------------------------------------------------------------------------------------------------------------------------------
                                                    RECORDING WEBGL2 STAND-IN
------------------------------------------------------------------------------------------------------------------------------------*/

const CallCounts = new Map();
const DrawCalls = { DrawArrays: 0, DrawElements: 0 };
const AttributeBindings = new Set();

// 💡 Sampler auditing: every program that draws must leave distinct, bound textures on each sampler unit
const SamplersByProgram = new Map();
const SamplerUnitsByProgram = new Map();
const UnitTextures = new Map();
const SamplerProblems = [];
const AttachedStages = new Map();
let CurrentProgram = null;
let CurrentLabel = null;
const ProgramLabelByHandle = new Map();
let ActiveTextureUnit = 0;
let DrawsAudited = 0;

function ResolveProgramSources()
{
    const ShaderRoot = path.join(SourceRoot, 'shaders');
    const Chunks = new Map();

    for (const FileName of fs.readdirSync(path.join(ShaderRoot, 'chunks')))
    {
        Chunks.set(FileName, fs.readFileSync(path.join(ShaderRoot, 'chunks', FileName), 'utf8'));
    }

    const Resolve = (Source, Depth = 0, Included = new Set()) =>
    {
        if (Depth > 12) return Source;
        return Source.replace(/^[ \t]*#include[ \t]+"([^"]+)"[ \t]*$/gm, (Match, Name) =>
        {
            if (!Chunks.has(Name) || Included.has(Name)) return '';
            Included.add(Name);
            return Resolve(Chunks.get(Name), Depth + 1, Included);
        });
    };

    const Library = fs.readFileSync(path.join(SourceRoot, 'ShaderLibrary.js'), 'utf8');
    const Entries = [...Library.matchAll(
        /\{ Label: '([^']+)', Vertex: '([^']+)'(?:, Fragment: '([^']+)')?/g)];

    for (const Entry of Entries)
    {
        const Label = Entry[1];
        const Names = new Set();

        for (const FileName of [Entry[2], Entry[3]])
        {
            if (!FileName) continue;
            const Source = Resolve(fs.readFileSync(path.join(ShaderRoot, 'programs', FileName), 'utf8'));
            for (const Match of Source.matchAll(/uniform\s+sampler2D\s+([A-Za-z0-9_]+)\s*;/g))
            {
                Names.add(Match[1]);
            }
        }

        SamplersByProgram.set(Label, Names);
    }
}

ResolveProgramSources();

function AuditDraw(ProgramLabel)
{
    if (!ProgramLabel || !SamplersByProgram.has(ProgramLabel)) return;
    DrawsAudited++;
    const Samplers = SamplersByProgram.get(ProgramLabel);
    const Units = SamplerUnitsByProgram.get(ProgramLabel) ?? new Map();
    const Used = new Map();

    for (const Sampler of Samplers)
    {
        if (!Units.has(Sampler))
        {
            SamplerProblems.push(`${ProgramLabel}: ${Sampler} has no assigned texture unit`);
            continue;
        }

        const Unit = Units.get(Sampler);

        if (!UnitTextures.has(Unit))
        {
            SamplerProblems.push(`${ProgramLabel}: ${Sampler} reads unit ${Unit} with no bound texture`);
        }

        if (Used.has(Unit))
        {
            SamplerProblems.push(`${ProgramLabel}: ${Sampler} and ${Used.get(Unit)} both read unit ${Unit}`);
        }
        else
        {
            Used.set(Unit, Sampler);
        }
    }
}

const Overrides =
{
    getExtension: (Name) => ({ Name: Name }),
    getParameter: () => 16,
    getShaderParameter: () => true,
    getProgramParameter: (Program, Parameter) =>
    {
        const Stages = AttachedStages.get(Program) ?? new Set();

        if (Parameter === ConstantFor('LINK_STATUS'))
        {
            // 📝 WebGL2 requires both stages; reproduce that rule rather than always reporting success
            return Stages.has('vertex') && Stages.has('fragment');
        }

        if (Parameter === ConstantFor('VALIDATE_STATUS'))
        {
            return true;
        }

        return 0;
    },
    attachShader: (Program, Shader) =>
    {
        if (!AttachedStages.has(Program)) AttachedStages.set(Program, new Set());
        AttachedStages.get(Program).add(Shader.Kind ?? 'unknown');
    },
    getShaderInfoLog: () => '',
    getProgramInfoLog: () => '',
    checkFramebufferStatus: () => 0x8CD5,
    FRAMEBUFFER_COMPLETE: 0x8CD5,
    getError: () => 0,
    createTexture: () => ({ Kind: 'Texture' }),
    createFramebuffer: () => ({ Kind: 'Framebuffer' }),
    createRenderbuffer: () => ({ Kind: 'Renderbuffer' }),
    createBuffer: () => ({ Kind: 'Buffer' }),
    createVertexArray: () => ({ Kind: 'VertexArray' }),
    createShader: (Category) => ({ Kind: Category === ConstantFor('VERTEX_SHADER') ? 'vertex' : 'fragment' }),
    createProgram: () => ({ Kind: 'Program' }),
    createTransformFeedback: () => ({ Kind: 'TransformFeedback' }),
    getAttribLocation: (Program, Name) =>
    {
        const Index = ['a_Position', 'a_Velocity', 'a_Spray', 'a_GridPosition', 'a_Normal', 'a_Color', 'a_Splat']
            .indexOf(Name);
        return Index;
    },
    getUniformLocation: (Program, Name) => ({ Name: Name }),
    readPixels: (X, Y, Width, Height, Format, Type, Destination) =>
    {
        for (let Index = 0; Index < Destination.length; Index += 4)
        {
            const Column = (Index / 4) % Width;
            const Row = Math.floor(Index / 4 / Width);
            Destination[Index + 0] = 0.35 + 0.05 * Math.sin(Column * 0.4) + 0.04 * Math.cos(Row * 0.3);
            Destination[Index + 1] = -3.2;
            Destination[Index + 2] = 0.1;
            Destination[Index + 3] = 6.0;
        }
    },
    useProgram: (Program) => { CurrentProgram = Program; CurrentLabel = ProgramLabelByHandle.get(Program) ?? null; },
    activeTexture: (Unit) => { ActiveTextureUnit = Unit - ConstantFor('TEXTURE0'); },
    bindTexture: (Target, Texture) => { UnitTextures.set(ActiveTextureUnit, Texture); },
    uniform1i: (Location, Value) =>
    {
        if (!CurrentProgram || !Location || typeof Location.Name !== 'string') return;
        const Samplers = SamplersByProgram.get(CurrentLabel ?? '');
        if (!Samplers || !Samplers.has(Location.Name)) return;
        if (!SamplerUnitsByProgram.has(CurrentLabel)) SamplerUnitsByProgram.set(CurrentLabel, new Map());
        SamplerUnitsByProgram.get(CurrentLabel).set(Location.Name, Value);
    },
    drawArrays: () => { DrawCalls.DrawArrays++; AuditDraw(CurrentLabel); },
    drawElements: () => { DrawCalls.DrawElements++; AuditDraw(CurrentLabel); },
    enableVertexAttribArray: (Index) => AttributeBindings.add(Index),
    disableVertexAttribArray: (Index) => AttributeBindings.delete(Index)
};

const ConstantLookup = new Map();
let ConstantCursor = 0x1000;

function ConstantFor(Name)
{
    if (!ConstantLookup.has(Name))
    {
        ConstantCursor += 1;
        ConstantLookup.set(Name, ConstantCursor);
    }

    return ConstantLookup.get(Name);
}

const StandInGl = new Proxy({}, {
    get(Target, Property)
    {
        if (typeof Property !== 'string')
        {
            return undefined;
        }

        if (Object.hasOwn(Overrides, Property))
        {
            return Overrides[Property];
        }

        if (Property in Target)
        {
            return Target[Property];
        }

        if (/^[A-Z0-9_]+$/.test(Property))
        {
            return ConstantFor(Property);
        }

        const Recorder = () =>
        {
            CallCounts.set(Property, (CallCounts.get(Property) ?? 0) + 1);
            return undefined;
        };

        return Recorder;
    }
});

class StandInCanvas
{
    constructor()
    {
        this.width = 1280;
        this.height = 720;
        this.classList = { add: () => {}, remove: () => {}, toggle: () => {} };
    }

    getContext(Category)
    {
        return Category === 'webgl2' ? StandInGl : null;
    }

    addEventListener() {}
    hasPointerCapture() { return false; }
    releasePointerCapture() {}
    setPointerCapture() {}
}

/*------------------------------------------------------------------------------------------------------------------------------------
                                                    FILE-BACKED FETCH
------------------------------------------------------------------------------------------------------------------------------------*/

globalThis.fetch = async (Resource) =>
{
    const ResourcePath = Resource instanceof URL ? fileURLToPath(Resource) : String(Resource);
    const Text = fs.readFileSync(ResourcePath, 'utf8');
    return { ok: true, status: 200, text: async () => Text };
};

/*------------------------------------------------------------------------------------------------------------------------------------
                                                    RUN
------------------------------------------------------------------------------------------------------------------------------------*/

const { GraphicsDevice } = await import(Module('GraphicsDevice.js'));
const { LoadShaderLibrary } = await import(Module('ShaderLibrary.js'));
const { WaterScenario } = await import(Module('WaterScenario.js'));
const { DefaultSettings, QualityProfiles } = await import(Module('ScenarioSettings.js'));

const Canvas = new StandInCanvas();
const Device = new GraphicsDevice(Canvas);
const Programs = await LoadShaderLibrary(Device, () => {});
const Settings = { ...DefaultSettings };
const Notes = [];
const Scenario = new WaterScenario(Canvas, Device, Programs, Settings, (Label, Detail) => Notes.push(`${Label}: ${Detail}`));

for (const [Label, Compiled] of Device.ProgramCache)
{
    ProgramLabelByHandle.set(Compiled.Program, Label);
}

function Frames(Count, DeltaSeconds)
{
    for (let Index = 0; Index < Count; Index++)
    {
        Scenario.Frame(DeltaSeconds);
    }
}

function FiniteCheck(Label, Values)
{
    let NonFinite = 0;

    for (const Value of Values)
    {
        if (!Number.isFinite(Value))
        {
            NonFinite++;
        }
    }

    return { Label: Label, NonFinite: NonFinite, Length: Values.length };
}

const Results = [];
const DrawsPerFrame = [];

for (const Category of [0, 1, 2])
{
    Settings.SimulationCategory = Category;
    Scenario.SpectralSignature = '';
    Scenario.GerstnerSignature = '';
    const Before = DrawCalls.DrawArrays + DrawCalls.DrawElements;
    Frames(40, 1.0 / 60.0);
    DrawsPerFrame.push((DrawCalls.DrawArrays + DrawCalls.DrawElements - Before) / 40);
    Results.push(FiniteCheck(`model ${Category + 1} vessel transform`, Scenario.Vessel.Transform));
    Results.push(FiniteCheck(`model ${Category + 1} gerstner table`, Scenario.Gerstner.Shape));
}

// Toggles must not disturb the frame path
Settings.FoamEnabled = false;
Settings.SprayEnabled = false;
Settings.VesselEnabled = false;
Settings.GridOverlay = true;
Settings.FieldView = 2;
Frames(10, 1.0 / 60.0);

Settings.FoamEnabled = true;
Settings.SprayEnabled = true;
Settings.VesselEnabled = true;
Settings.GridOverlay = false;
Settings.FieldView = 0;
Settings.ReflectionEnabled = false;
Settings.RefractionEnabled = false;
Frames(10, 1.0 / 60.0);
Settings.ReflectionEnabled = true;
Settings.RefractionEnabled = true;

// Quality profiles and pause must all survive
for (const Quality of [0, 3, 2])
{
    Settings.QualityCategory = Quality;
    Frames(6, 1.0 / 60.0);
}

Settings.SimulationPaused = true;
Frames(4, 1.0 / 60.0);
Settings.SimulationPaused = false;

const SpectrumScales = Scenario.Spectral.Cascades.map((Cascade) => Cascade.SpectrumScale);
const VesselTrace = Scenario.Vessel.Position.map((Value) => Number(Value.toFixed(2)));

console.log('--- runtime validation ---');
console.log('programs compiled        :', Programs.size);
console.log('frames executed          : 40 x 3 models + 30 toggle frames');
console.log('draw calls per frame     :', DrawsPerFrame.map((Value) => Value.toFixed(1)).join(', '));
console.log('distinct gl entry points :', CallCounts.size);
console.log('draws sampled for texture binding:', DrawsAudited);
console.log('sampler binding problems :', SamplerProblems.length === 0 ? 'none' : SamplerProblems.slice(0, 8).join(' | '));
console.log('spectrum scales          :', SpectrumScales.map((Value) => Value.toExponential(3)).join(', '));
console.log('vessel position          :', VesselTrace.join(', '));
console.log('emitters configured      :', Scenario.Spray.EmitterCount);
console.log('report notes             :', Notes.join(' | '));

let Failures = 0;
for (const Result of Results)
{
    const Status = Result.NonFinite === 0 ? 'finite' : `${Result.NonFinite} non-finite`;
    if (Result.NonFinite !== 0) Failures++;
    console.log(`check ${Result.Label.padEnd(32)}: ${Status} (${Result.Length} values)`);
}

if (SamplerProblems.length > 0) Failures++;
const SpectrumScaleFailure = SpectrumScales.some((Value) => !Number.isFinite(Value) || Value <= 0);
if (SpectrumScaleFailure) Failures++;
console.log('spectrum scale check     :', SpectrumScaleFailure ? 'FAILED' : 'positive and finite');
console.log('result                   :', Failures === 0 ? 'PASS' : `${Failures} FAILURES`);
process.exitCode = Failures === 0 ? 0 : 1;
