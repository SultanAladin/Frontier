/*====================================================================================================================================
                                            SHADER STAGE VALIDATION.MJS
====================================================================================================================================*/
// 🧩 Resolves the include graph and validates every stage as GLSL ES 3.00 with glslangValidator

import fs from 'fs';
import os from 'os';
import path from 'path';
import { execFileSync } from 'child_process';
import { fileURLToPath, pathToFileURL } from 'url';

const Here = path.dirname(fileURLToPath(import.meta.url));
const SourceRoot = path.resolve(Here, '../../../Experimental/WaterSimulation/src');
const ShaderRoot = path.join(SourceRoot, 'shaders');
const ScratchRoot = path.join(os.tmpdir(), 'frontier-water-shader-validation');

const Hosts = [
    process.env.GLSLANG_VALIDATOR,
    '/tmp/sdl/third_party/glslang/StandAlone/glslangValidator',
    '/usr/bin/glslangValidator',
    '/usr/local/bin/glslangValidator'
].filter(Boolean);

const Validator = Hosts.find((Candidate) => fs.existsSync(Candidate));

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

function ResolveIncludes(Source, Label, Depth = 0, Included = new Set())
{
    if (Depth > 12)
    {
        throw new Error(`${Label}: include nesting exceeds 12 tiers`);
    }

    return Source.replace(/^[ \t]*#include[ \t]+"([^"]+)"[ \t]*$/gm, (Match, Name) =>
    {
        if (!Chunks.has(Name))
        {
            throw new Error(`${Label}: missing shader chunk "${Name}"`);
        }

        if (Included.has(Name))
        {
            return '';
        }

        Included.add(Name);
        return ResolveIncludes(Chunks.get(Name), Label, Depth + 1, Included);
    });
}

const Library = fs.readFileSync(path.join(SourceRoot, 'ShaderLibrary.js'), 'utf8');
const Descriptors = [...Library.matchAll(
    /\{ Label: '([^']+)', Vertex: '([^']+)'(?:, Fragment: '([^']+)')?(?:, TransformVaryings: \[[^\]]*\])? \}/g
)].map((Match) => ({ Label: Match[1], Vertex: Match[2], Fragment: Match[3] }));

const Cache = new Map();
const Stages = [];

for (const Descriptor of Descriptors)
{
    for (const [Category, FileName] of [['vert', Descriptor.Vertex], ['frag', Descriptor.Fragment]])
    {
        if (!FileName)
        {
            continue;
        }

        if (!Cache.has(FileName))
        {
            Cache.set(FileName, fs.readFileSync(path.join(ShaderRoot, 'programs', FileName), 'utf8'));
        }

        Stages.push({
            Key:     `${Descriptor.Label}.${Category === 'vert' ? 'vertex' : 'fragment'}`,
            Category: Category,
            Source:  ResolveIncludes(Cache.get(FileName), `${Descriptor.Label}.${Category}`)
        });
    }
}

fs.mkdirSync(ScratchRoot, { recursive: true });

console.log('--- shader stage validation ---');
console.log('programs   :', Descriptors.length);
console.log('stages     :', Stages.length);

if (!Validator)
{
    console.log('validator  : NOT FOUND');
    console.log('prerequisite: set GLSLANG_VALIDATOR to a glslangValidator binary (see Exhibits/WaterSimulation/README.md).');
    console.log('result     : BLOCKED — nothing is claimed as validated');
    process.exitCode = 2;
}
else
{
    console.log('validator  :', Validator);
    let Failures = 0;

    for (const Stage of Stages)
    {
        const File = path.join(ScratchRoot, `stage_${Stage.Key.replace(/\./g, '_')}.${Stage.Category}`);
        fs.writeFileSync(File, Stage.Source);

        try
        {
            execFileSync(Validator, ['-S', Stage.Category, File], { stdio: ['ignore', 'pipe', 'pipe'] });
            console.log(`OK    ${Stage.Key}`);
        }
        catch (Failure)
        {
            Failures++;
            const Output = `${Failure.stdout?.toString() ?? ''}${Failure.stderr?.toString() ?? ''}`;
            console.log(`FAIL  ${Stage.Key}`);
            console.log(Output.split('\n').filter(Boolean).map((Line) => `      ${Line}`).join('\n'));
        }
    }

    console.log('result     :', Failures === 0 ? 'PASS' : `${Failures} FAILURES`);
    process.exitCode = Failures === 0 ? 0 : 1;
}
