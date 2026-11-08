/*==========================================================================================================================================
                                                      GPUPREVIEWSAMPLINGREPRODUCTION.MJS
==========================================================================================================================================*/
// 🧩 Portable form of the original 1050 × 720 capture: a fresh 2048/512 target still completes the coarse overview.

import { mkdir, writeFile } from 'node:fs/promises';
import { chromium } from '../../Experimental/LandscapeWorkspace/node_modules/playwright-core/index.mjs';
import { GraphicsArguments, ConstructGpuReport, ObserveGpuBrowser, WaitForGpuImage } from './GpuValidationEnvironment.mjs';

const Origin = process.env.LANDSCAPE_URL || 'http://127.0.0.1:5173';
const Prefix = process.env.LANDSCAPE_REPORT_PREFIX || 'GpuPreviewSamplingReproduction';
const Report = await ConstructGpuReport(Origin);
const Images = new URL('./Images/', import.meta.url), Results = new URL('./Results/', import.meta.url);
await mkdir(Images, { recursive: true });
await mkdir(Results, { recursive: true });
const Browser = await chromium.launch({ executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH || undefined, headless: true, args: GraphicsArguments });
const Page = await Browser.newPage({ viewport: { width: 1050, height: 720 } });
await ObserveGpuBrowser(Page);
try
{
    await Page.goto(Origin, { waitUntil: 'networkidle' });
    await Page.locator('#LandscapeTab').click();
    Report.Before = await WaitForGpuImage(Page);
    await Page.locator('[data-inspector="region"]').click();
    await Page.locator('#Resolution').selectOption('2048');
    await Page.locator('#Vertical').selectOption('512');
    Report.After = await WaitForGpuImage(Page);
    Report.WorkingPanelVisible = await Page.locator('#WorkingReadout').isVisible();
    Report.Target = await Page.locator('#LatticePitch').textContent();
    await Page.locator('#LatticePitch').scrollIntoViewIfNeeded();
    await Page.screenshot({ path: new URL(Prefix + '.png', Images).pathname });
    Report.Images.push(Prefix + '.png');
    Report.Status = 'observed';
}
finally
{
    await Browser.close();
    await writeFile(new URL(Prefix + '.json', Results), JSON.stringify(Report, null, 2) + '\n');
}
