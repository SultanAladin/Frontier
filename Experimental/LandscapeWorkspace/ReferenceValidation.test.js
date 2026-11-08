/*==========================================================================================================================================
                                                       REFERENCEVALIDATION.TEST.JS
==========================================================================================================================================*/
// 🧩 Immutable ProjectZero UI fingerprints and separate mask/terrain ownership markup checks.

import { test } from 'node:test';
import Assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';

test('ProjectZero UI source snapshots match the inspected immutable Git blobs', () =>
{
    const Pin = JSON.parse(readFileSync(new URL('./ReferenceSnapshot/ProjectZeroPin.json', import.meta.url)));
    Assert.equal(Pin.Commit, '28ec0657a23ed6bbf680f17cc06b464a53639dae');
    for (const Entry of Pin.Files)
    {
        const Bytes = readFileSync(new URL('./ReferenceSnapshot/' + Entry.Snapshot, import.meta.url));
        const Fingerprint = createHash('sha1').update(`blob ${Bytes.length}\0`).update(Bytes).digest('hex');
        Assert.equal(Fingerprint, Entry.GitBlob);
        Assert.equal(Bytes.length, Entry.Bytes);
    }
});

test('editor uses ProjectZero graphite tokens rather than the erosion-reference CSS cascade', () =>
{
    const HTML = readFileSync(new URL('./index.html', import.meta.url), 'utf8');
    const CSS = readFileSync(new URL('./Workspace.css', import.meta.url), 'utf8');
    Assert.match(HTML, /id="Layers"/);
    Assert.match(HTML, /id="LandformRows"/);
    Assert.match(HTML, /id="StackScope"/);
    Assert.match(CSS, /"DM Sans"/);
    Assert.match(CSS, /\.outliner[^\n]*background: #151515/);
    Assert.match(CSS, /\.property-card[^\n]*border-radius: 18px/);
    Assert.doesNotMatch(HTML, /ReferenceStack\.css|StackDock\.css|PresetCards/);
});

test('the user-requested SDF branch and immutable terrain/UI pins remain in CLAUDE.md', () =>
{
    const Instructions = readFileSync(new URL('../../CLAUDE.md', import.meta.url), 'utf8');
    Assert.ok(Instructions.includes('https://github.com/c7egoist/Frontier/tree/arena%2F68c84c0d-frontier'));
    Assert.ok(Instructions.includes('09b7064f282f4df5c5352f2bc97c82fb1c06a098'));
    Assert.ok(Instructions.includes('28ec0657a23ed6bbf680f17cc06b464a53639dae'));
});
