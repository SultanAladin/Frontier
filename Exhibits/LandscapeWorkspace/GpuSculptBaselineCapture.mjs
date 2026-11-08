/*==========================================================================================================================================
                                                          GPUSCULPTBASELINECAPTURE.MJS
==========================================================================================================================================*/
// 🧩 Actual preceding built application's 64-step coast before rebuilding, with its original manifest rather than new-source hashes.

import Assert from 'node:assert/strict';
import { readFile, writeFile } from 'node:fs/promises';
import { chromium } from '../../Experimental/LandscapeWorkspace/node_modules/playwright-core/index.mjs';
import { GraphicsArguments, ObserveGpuBrowser, WaitForGpuImage, ReadGpuPixels } from './GpuValidationEnvironment.mjs';

const Origin = process.env.LANDSCAPE_URL || 'http://127.0.0.1:4173';
const Previous = JSON.parse(await readFile(new URL('./Results/ProductionGpuPreviewSampling.json', import.meta.url), 'utf8'));
const Built = JSON.parse(await readFile(new URL('./Results/ProductionValidation.json', import.meta.url), 'utf8'));
const Browser = await chromium.launch({ executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH || undefined, headless: true, args: GraphicsArguments });
const Page = await Browser.newPage({ viewport: { width: 1600, height: 1000 }, deviceScaleFactor: 1 });
await ObserveGpuBrowser(Page);
try
{
    await Page.goto(Origin, { waitUntil: 'networkidle' });
    await Page.locator('#LandscapeTab').click();
    const Telemetry = await WaitForGpuImage(Page);
    Assert.equal(Telemetry.completedQuality, '64', 'Capture must run against the preceding build, not the new high-detail application.');
    Assert.equal(Telemetry.displayedSurfaceTriangles, '96798');
    const Pixels = await ReadGpuPixels(Page);
    await Page.screenshot({ path: new URL('./Images/GpuSculptBeforeHigh.png', import.meta.url).pathname });
    await writeFile(new URL('./Results/GpuSculptBeforeHigh.json', import.meta.url), JSON.stringify({
        Status: 'observed-before-always-high-sculpt', Date: new Date().toISOString(), Origin,
        Revision: Previous.Revision, Branch: Previous.Branch, SourceSHA256: Previous.SourceSHA256, BuiltAssets: Built.Assets,
        Conditions: { Graphics: 'Chromium / ANGLE SwiftShader lab driver; not physical GPU verification.',
            Source: 'Preceding built application captured before rebuilding; fingerprints inherited from its matching production reports.',
            ApplicationSourceInterception: false, DeadlineOverride: false, Viewport: [1600, 1000] },
        Telemetry, Pixels, Images: ['GpuSculptBeforeHigh.png']
    }, null, 2) + '\n');
}
finally { await Browser.close(); }
