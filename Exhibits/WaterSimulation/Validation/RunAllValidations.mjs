/*====================================================================================================================================
                                            RUN ALL VALIDATIONS.MJS
====================================================================================================================================*/
// 🧩 Runs every water validation in order, writes each transcript to Results/, and exits non-zero on any failure

import fs from 'fs';
import path from 'path';
import { spawnSync } from 'child_process';
import { fileURLToPath } from 'url';

const Here = path.dirname(fileURLToPath(import.meta.url));
const Results = path.resolve(Here, '../Results');

const Sequence =
[
    { Script: 'ShaderStageValidation.mjs',         Output: 'ShaderStageValidation.txt' },
    { Script: 'ShaderUniformAudit.mjs',            Output: 'ShaderUniformAudit.txt' },
    { Script: 'InterfaceConsistencyValidation.mjs', Output: 'InterfaceConsistencyValidation.txt' },
    { Script: 'ScenarioRuntimeValidation.mjs',     Output: 'ScenarioRuntimeValidation.txt' },
    { Script: 'WaveMathValidation.mjs',            Output: 'WaveMathValidation.txt' },
    { Script: 'VesselKinematicsValidation.mjs',    Output: 'VesselKinematicsValidation.txt' },
    { Script: 'SpectralReferenceRender.mjs',       Output: 'SpectralReferenceRender.txt' }
];

fs.mkdirSync(Results, { recursive: true });

const Summary = [];
let Failures = 0;

for (const Entry of Sequence)
{
    const Run = spawnSync(process.execPath, [path.join(Here, Entry.Script)], { encoding: 'utf8', cwd: Here });
    const Transcript = `${Run.stdout ?? ''}${Run.stderr ?? ''}`;
    fs.writeFileSync(path.join(Results, Entry.Output), Transcript);

    const Blocked = Run.status === 2;
    const Passed = Run.status === 0;
    if (!Passed && !Blocked) Failures++;

    Summary.push({
        Script: Entry.Script,
        Status: Passed ? 'PASS' : (Blocked ? 'BLOCKED' : 'FAIL'),
        Detail: Transcript.trim().split('\n').slice(-2).join(' | ')
    });

    console.log(`${Summary.at(-1).Status.padEnd(8)} ${Entry.Script}`);
}

console.log('--- water validation summary ---');

for (const Entry of Summary)
{
    console.log(`${Entry.Status.padEnd(8)} ${Entry.Script.padEnd(38)} ${Entry.Detail}`);
}

console.log('overall:', Failures === 0 ? 'PASS' : `${Failures} FAILING SCRIPT(S)`);
process.exitCode = Failures === 0 ? 0 : 1;
