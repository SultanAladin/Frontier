// Retained browser distribution avoids preview access to ignored dependency directories.
// Rebuild after source changes; the pinned package remains the only Jolt source of truth.
import { build } from 'esbuild';
import { fileURLToPath } from 'node:url';
import fs from 'node:fs';
import { createHash } from 'node:crypto';
const Root = fileURLToPath(new URL('../', import.meta.url));
await build({
    absWorkingDir: Root, entryPoints: ['Source/IlluminationHost.js'],
    outfile: 'Archive/Application.js', bundle: true, format: 'esm', platform: 'browser',
    target: 'es2022', minify: true, legalComments: 'eof',
    external: ['node:*', 'fs', 'path', 'url'],
});
fs.copyFileSync(`${Root}node_modules/jolt-physics/LICENSE`, `${Root}Archive/Application-Jolt-LICENSE.txt`);
const Sources = fs.readdirSync(`${Root}Source`).filter(Name => Name.endsWith('.js') && Name !== 'Bootstrap.js')
    .sort().map(Name => `Source/${Name}`);
Sources.push('package-lock.json', 'Tools/package-lock.json', 'Tools/BuildBrowser.mjs');
const Digest = createHash('sha256');
for (const File of Sources) Digest.update(File).update('\0').update(fs.readFileSync(Root + File));
const Bundle = fs.readFileSync(`${Root}Archive/Application.js`);
fs.writeFileSync(`${Root}Archive/ApplicationBuild.json`, JSON.stringify({
    SourceSha256: Digest.digest('hex'), BundleSha256: createHash('sha256').update(Bundle).digest('hex'),
    Bytes: Bundle.length, Jolt: '1.1.0', Esbuild: '0.25.12', Sources,
}, null, 2) + '\n');
console.log('Built public browser application, including locked Jolt WASM.');
