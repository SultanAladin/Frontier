/*====================================================================================================================================
                                            SHADER UNIFORM AUDIT.MJS
====================================================================================================================================*/
// 🧩 Cross-checks every JS uniform write against shader declarations, and varyings against their vertex outputs

import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const Here = path.dirname(fileURLToPath(import.meta.url));
const SourceRoot = path.resolve(Here, '../../../Experimental/WaterSimulation/src');
const ShaderRoot = path.join(SourceRoot, 'shaders');

function CollectFiles(Directory, Suffix)
{
    return fs.readdirSync(Directory, { withFileTypes: true }).flatMap((Entry) =>
    {
        const Full = path.join(Directory, Entry.name);
        return Entry.isDirectory() ? CollectFiles(Full, Suffix) : (Entry.name.endsWith(Suffix) ? [Full] : []);
    });
}

const Chunks = new Map();
for (const File of CollectFiles(path.join(ShaderRoot, 'chunks'), '.glsl'))
{
    Chunks.set(path.basename(File), fs.readFileSync(File, 'utf8'));
}

function Resolve(Source, Label, Depth = 0, Included = new Set())
{
    if (Depth > 12) throw new Error(`${Label}: include nesting`);

    return Source.replace(/^[ \t]*#include[ \t]+"([^"]+)"[ \t]*$/gm, (Match, Name) =>
    {
        if (!Chunks.has(Name)) throw new Error(`${Label}: missing ${Name}`);
        if (Included.has(Name)) return '';
        Included.add(Name);
        return Resolve(Chunks.get(Name), Label, Depth + 1, Included);
    });
}

const Library = fs.readFileSync(path.join(SourceRoot, 'ShaderLibrary.js'), 'utf8');
const Descriptors = [...Library.matchAll(
    /\{ Label: '([^']+)', Vertex: '([^']+)'(?:, Fragment: '([^']+)')?/g
)].map((Match) => ({ Label: Match[1], Vertex: Match[2], Fragment: Match[3] }));

const ScriptFiles = CollectFiles(SourceRoot, '.js');
const WrittenUniforms = new Set();
const DynamicallyIndexed = ['uSpectralDisplacement0', 'uSpectralDisplacement1', 'uSpectralDisplacement2',
                            'uSpectralSlope0', 'uSpectralSlope1', 'uSpectralSlope2',
                            'uCascadeDrift0', 'uCascadeDrift1', 'uCascadeDrift2'];

for (const File of ScriptFiles)
{
    const Text = fs.readFileSync(File, 'utf8');
    for (const Match of Text.matchAll(/Set(?:Float|Int|Vec2|Vec3|Vec4|Matrix4|Array1|Array3|Array4|Texture)\(\s*'([A-Za-z0-9_]+)'/g))
    {
        WrittenUniforms.add(Match[1]);
    }
    for (const Match of Text.matchAll(/Set(?:Float|Int|Vec2|Vec3|Vec4|Matrix4|Texture)\(\s*`([A-Za-z0-9_]+)\$\{/g))
    {
        WrittenUniforms.add(Match[1]);
    }
}

// Templated writes expand to per-cascade names, so the base template is dropped from the audit set
const TemplateBases = new Set(['uSpectralDisplacement', 'uSpectralSlope', 'uCascadeDrift']);

for (const Name of DynamicallyIndexed)
{
    WrittenUniforms.add(Name);
}

for (const Base of TemplateBases)
{
    WrittenUniforms.delete(Base);
}

function Declarations(Source)
{
    const Uniforms = [];
    const Outputs = [];
    const Inputs = [];

    for (const Match of Source.matchAll(/^\s*uniform\s+(?:highp\s+|mediump\s+|lowp\s+)?([A-Za-z0-9_]+)\s+([A-Za-z0-9_]+)(\[[^\]]*\])?\s*;/gm))
    {
        Uniforms.push({ Name: Match[2] });
    }
    for (const Match of Source.matchAll(/^\s*(?:layout\(location\s*=\s*\d+\)\s*)?out\s+([A-Za-z0-9_]+)\s+([A-Za-z0-9_]+)\s*;/gm))
    {
        Outputs.push({ Type: Match[1], Name: Match[2] });
    }
    for (const Match of Source.matchAll(/^\s*in\s+([A-Za-z0-9_]+)\s+([A-Za-z0-9_]+)\s*;/gm))
    {
        Inputs.push({ Type: Match[1], Name: Match[2] });
    }

    return { Uniforms, Outputs, Inputs };
}

const ProgramStages = new Map();
const DeclaredUniforms = new Set();

for (const Descriptor of Descriptors)
{
    for (const [Category, FileName] of [['vertex', Descriptor.Vertex], ['fragment', Descriptor.Fragment]])
    {
        if (!FileName) continue;

        const Source = Resolve(fs.readFileSync(path.join(ShaderRoot, 'programs', FileName), 'utf8'),
                               `${Descriptor.Label}.${Category}`);
        const Parsed = Declarations(Source);

        for (const Uniform of Parsed.Uniforms)
        {
            DeclaredUniforms.add(Uniform.Name);
        }

        ProgramStages.set(`${Descriptor.Label}.${Category}`, Parsed);
    }
}

const Problems = [];

for (const Descriptor of Descriptors)
{
    const Vertex = ProgramStages.get(`${Descriptor.Label}.vertex`);
    const Fragment = ProgramStages.get(`${Descriptor.Label}.fragment`);

    if (!Fragment)
    {
        continue;
    }

    const VertexOutputs = new Map(Vertex.Outputs.map((Entry) => [Entry.Name, Entry.Type]));

    for (const Entry of Fragment.Inputs)
    {
        if (Entry.Name === 'gl_FragCoord' || Entry.Name === 'gl_PointCoord')
        {
            continue;
        }

        if (!VertexOutputs.has(Entry.Name))
        {
            Problems.push(`LINK  ${Descriptor.Label}: fragment input ${Entry.Name} has no vertex output`);
            continue;
        }

        if (VertexOutputs.get(Entry.Name) !== Entry.Type)
        {
            Problems.push(`TYPE  ${Descriptor.Label}: ${Entry.Name} is ${VertexOutputs.get(Entry.Name)} in vertex and ${Entry.Type} in fragment`);
        }
    }
}

for (const [Key, Parsed] of ProgramStages)
{
    for (const Uniform of Parsed.Uniforms)
    {
        if (!WrittenUniforms.has(Uniform.Name))
        {
            Problems.push(`UNSET ${Key}: declares ${Uniform.Name} but no JS path sets it`);
        }
    }
}

for (const Name of WrittenUniforms)
{
    if (!DeclaredUniforms.has(Name))
    {
        Problems.push(`TYPO  JS sets ${Name} but no shader declares it`);
    }
}

console.log('--- shader uniform and varying audit ---');
console.log('uniform names written from JS :', WrittenUniforms.size);
console.log('uniform names declared in GLSL :', DeclaredUniforms.size);
console.log('program stages inspected       :', ProgramStages.size);
console.log(Problems.length === 0 ? 'no cross-check problems' : Problems.join('\n'));
console.log('result:', Problems.length === 0 ? 'PASS' : `${Problems.length} PROBLEMS`);
process.exitCode = Problems.length === 0 ? 0 : 1;
