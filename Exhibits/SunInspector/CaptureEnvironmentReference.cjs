// Browser reference evidence, not C++ output. Run with the VehicleHMI demo server running.
const path = require('node:path');
const { chromium } = require('playwright-core');
const Chromium = require('@sparticuz/chromium').default;

(async () => {
    const browser = await chromium.launch({
        executablePath: process.env.CHROMIUM_PATH || await Chromium.executablePath(),
        args: [...Chromium.args.filter(a => !['--single-process', '--in-process-gpu', '--disable-web-security'].includes(a)),
               '--enable-unsafe-webgpu', '--use-webgpu-adapter=swiftshader', '--enable-features=Vulkan',
               '--use-vulkan=swiftshader', '--disable-vulkan-surface', '--enable-unsafe-swiftshader'],
    });
    try {
        const page = await browser.newPage({ viewport: { width: 1440, height: 900 }, deviceScaleFactor: 1 });
        await page.addInitScript(() => {
            const request = requestAnimationFrame.bind(window);
            window.requestAnimationFrame = callback => request(time => {
                if (window.IlluminationExhibit?.FrameIndex >= 1) return;
                callback(time);
            });
        });
        const url = (process.env.REFERENCE_URL || 'http://127.0.0.1:8080/') +
                    '?quality=low&scale=.25&stresspool=0&inspector=sun';
        await page.goto(url, { waitUntil: 'domcontentloaded' });
        await page.waitForFunction(() => window.IlluminationExhibit?.FrameIndex >= 1, null,
                                   { polling: 100, timeout: 300000 });
        for (const id of ['clouds', 'atmosphere', 'flare']) {
            await page.evaluate(name => window.IlluminationExhibit.Shell.Select(name), id);
            const tabs = page.getByRole('tab');
            const count = await tabs.count();
            for (let i = 0; i < count; ++i) {
                await tabs.nth(i).click();
                await page.mouse.move(700, 20);
                await page.screenshot({ path: path.join(__dirname, 'Images', `reference-${id}-${i}.png`) });
            }
        }
        console.log('Environment reference captures complete; browser:', browser.version());
    } finally {
        await browser.close();
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
