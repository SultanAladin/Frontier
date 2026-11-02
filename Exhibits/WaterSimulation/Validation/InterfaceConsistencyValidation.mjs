/*====================================================================================================================================
                                            INTERFACE CONSISTENCY VALIDATION.MJS
====================================================================================================================================*/
// 🧩 Static checks: shader and asset references resolve, DOM ids exist, CSS classes exist, and no banned vocabulary

import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const Here = path.dirname(fileURLToPath(import.meta.url));
const Root = path.resolve(Here, '../../../Experimental/WaterSimulation');
const SourceRoot = path.join(Root, 'src');

const Report = [];
let Failures = 0;

function Check(Label, Passed, Detail)
{
    if (!Passed) Failures++;
    Report.push({ Label: Label, Result: Passed ? 'PASS' : 'FAIL', Detail: Detail });
}

function Walk(Directory, Extension)
{
    const Entries = fs.readdirSync(Directory, { withFileTypes: true });
    const Files = [];

    for (const Entry of Entries)
    {
        const Full = path.join(Directory, Entry.name);

        if (Entry.isDirectory())
        {
            Files.push(...Walk(Full, Extension));
        }
        else if (Entry.name.endsWith(Extension))
        {
            Files.push(Full);
        }
    }

    return Files;
}

/*------------------------------------------------------------------------------------------------------------------------------------
                                                    1. SHADER REFERENCE GRAPH
------------------------------------------------------------------------------------------------------------------------------------*/

const Library = fs.readFileSync(path.join(SourceRoot, 'ShaderLibrary.js'), 'utf8');
const ProgramFiles = [...Library.matchAll(/'([A-Za-z0-9_]+\.(?:vert|frag))'/g)].map((Match) => Match[1]);
const ChunkFiles = [...Library.matchAll(/'([A-Za-z0-9_]+\.glsl)'/g)].map((Match) => Match[1]);

const MissingPrograms = ProgramFiles.filter((Name) => !fs.existsSync(path.join(SourceRoot, 'shaders/programs', Name)));
const MissingChunks = ChunkFiles.filter((Name) => !fs.existsSync(path.join(SourceRoot, 'shaders/chunks', Name)));
Check('Every referenced program file exists', MissingPrograms.length === 0, `${ProgramFiles.length} program files checked`);
Check('Every referenced chunk file exists', MissingChunks.length === 0, `${ChunkFiles.length} chunk files checked`);

// 🔴 WebGL2 cannot link a program that lacks either stage; every descriptor must declare a fragment
const ProgramDescriptors = [...Library.matchAll(
    /\{ Label: '([^']+)', Vertex: '([^']+)', Fragment: '([^']+)'/g)].map((Match) => Match[1]);
const AllLabels = [...Library.matchAll(/\{ Label: '([^']+)', Vertex: /g)].map((Match) => Match[1]);
const StageLess = AllLabels.filter((Label) => !ProgramDescriptors.includes(Label));
Check('Every program declares both a vertex and a fragment stage', StageLess.length === 0,
      `${AllLabels.length} programs${StageLess.length ? `, missing fragment: ${StageLess.join(', ')}` : ', all two-stage'}`);

const DeclaredChunks = new Set(ChunkFiles);
const IncludeFailures = [];
const ProgramSources = Walk(path.join(SourceRoot, 'shaders/programs'), '.');
for (const File of ProgramSources)
{
    const Text = fs.readFileSync(File, 'utf8');
    for (const Match of Text.matchAll(/#include\s+"([^"]+)"/g))
    {
        if (!DeclaredChunks.has(Match[1]))
        {
            IncludeFailures.push(`${path.basename(File)} -> ${Match[1]}`);
        }
    }
}
Check('Every #include resolves to a registered chunk', IncludeFailures.length === 0,
      IncludeFailures.length ? IncludeFailures.join(', ') : 'no dangling includes');

/*------------------------------------------------------------------------------------------------------------------------------------
                                                    2. DOM AND STYLE CONTRACTS
------------------------------------------------------------------------------------------------------------------------------------*/

const Html = fs.readFileSync(path.join(Root, 'index.html'), 'utf8');
const Css = fs.readFileSync(path.join(Root, 'Styles.css'), 'utf8');
const Main = fs.readFileSync(path.join(SourceRoot, 'Main.js'), 'utf8');

const HtmlIds = new Set([...Html.matchAll(/id="([^"]+)"/g)].map((Match) => Match[1]));
const QueryIds = new Set([...Main.matchAll(/getElementById\('([^']+)'\)/g)].map((Match) => Match[1]));
const MissingIds = [...QueryIds].filter((Identifier) => !HtmlIds.has(Identifier));
Check('Every queried DOM id exists in index.html', MissingIds.length === 0,
      `${QueryIds.size} ids queried${MissingIds.length ? `, missing ${MissingIds.join(', ')}` : ''}`);

const CssClasses = new Set([...Css.matchAll(/\.([A-Za-z][A-Za-z0-9_-]*)/g)].map((Match) => Match[1]));
const ScriptFiles = Walk(SourceRoot, '.js');
const ClassReferences = new Set();

for (const File of ScriptFiles)
{
    const Text = fs.readFileSync(File, 'utf8');
    for (const Match of Text.matchAll(/className\s*=\s*'([^']+)'/g))
    {
        for (const Token of Match[1].split(/\s+/)) ClassReferences.add(Token);
    }
    for (const Match of Text.matchAll(/classList\.(?:add|remove|toggle)\('([^']+)'/g)) ClassReferences.add(Match[1]);
}

const MissingClasses = [...ClassReferences].filter((Token) => Token && !CssClasses.has(Token));
Check('Every class used by the UI exists in Styles.css', MissingClasses.length === 0,
      `${ClassReferences.size} class tokens${MissingClasses.length ? `, missing ${MissingClasses.join(', ')}` : ''}`);

/*------------------------------------------------------------------------------------------------------------------------------------
                                                    3. NAMING AND ASSET HYGIENE
------------------------------------------------------------------------------------------------------------------------------------*/

const BannedWords = ['Manager', 'Handler', 'Dispatcher', 'Processor', 'Controller', 'Service', 'Utility',
                     'Helper', 'Mesh', 'Backend', 'Entity', 'Node', 'Harness', 'Proof'];

const BannedHits = [];

for (const File of [...ScriptFiles, ...ProgramSources])
{
    const Text = fs.readFileSync(File, 'utf8');
    for (const Word of BannedWords)
    {
        const Pattern = new RegExp(`\\b${Word}\\b`);
        if (Pattern.test(Text))
        {
            BannedHits.push(`${path.basename(File)}: ${Word}`);
        }
    }
}

Check('No banned vocabulary in prototype source', BannedHits.length === 0,
      BannedHits.length ? BannedHits.slice(0, 6).join(', ') : `${BannedWords.length} terms checked`);

const HardCodedPaths = [];
for (const File of ScriptFiles)
{
    const Text = fs.readFileSync(File, 'utf8');
    if (/[A-Za-z]:[\\/]/.test(Text))
    {
        HardCodedPaths.push(path.basename(File));
    }
}
Check('No hard-coded developer drive paths', HardCodedPaths.length === 0,
      HardCodedPaths.length ? HardCodedPaths.join(', ') : 'relative module and fetch paths only');

console.log('--- interface consistency validation ---');

for (const Entry of Report)
{
    console.log(`${Entry.Result.padEnd(5)} ${Entry.Label}`);
    console.log(`      ${Entry.Detail}`);
}

console.log('result:', Failures === 0 ? 'PASS' : `${Failures} FAILURES`);
process.exitCode = Failures === 0 ? 0 : 1;
