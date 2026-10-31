// Capture the upstream VehicleHMI reference; this does not exercise native C++.
const { chromium } = require('playwright-core');
const path = require('node:path');
const Chromium = require('@sparticuz/chromium').default;

(async () => {
    const browser = await chromium.launch({
        executablePath: process.env.CHROMIUM_PATH || await Chromium.executablePath(),
        headless: true,
        args: [...Chromium.args.filter(a => !['--single-process', '--in-process-gpu', '--disable-web-security'].includes(a)), '--enable-unsafe-webgpu', '--use-webgpu-adapter=swiftshader',
               '--enable-features=Vulkan', '--use-vulkan=swiftshader', '--disable-vulkan-surface',
               '--enable-unsafe-swiftshader'],
    });
    try {
        const page = await browser.newPage({ viewport: { width: 1440, height: 900 }, deviceScaleFactor: 1 });
        page.on('pageerror', error => console.error('Page error:', error.message));
        // Stop after the first rendered frame so software WebGPU remains responsive during capture.
        await page.addInitScript(() => {
            const request = requestAnimationFrame.bind(window);
            window.requestAnimationFrame = callback => request(time => {
                if (window.IlluminationExhibit?.FrameIndex >= 1) return;
                callback(time);
            });
        });
        await page.goto((process.env.REFERENCE_URL || 'http://127.0.0.1:8080/') +
                        '?quality=low&scale=.25&stresspool=0&inspector=sun', { waitUntil: 'domcontentloaded' });
        await page.locator('.sun-observatory').waitFor({ timeout: 300000 });
        await page.waitForFunction(() => window.IlluminationExhibit?.FrameIndex >= 1, null, { timeout: 300000, polling: 100 });
        const output = path.join(__dirname, 'Images');
        for (const [tab, name] of [['Overview', 'overview'], ['Daylight', 'daylight'],
                                   ['Light & colour', 'light-colour'], ['Disc & optics', 'disc-optics']]) {
            await page.getByRole('tab', { name: tab, exact: true }).click();
            await page.mouse.move(700, 20);
            await page.screenshot({ path: path.join(output, `sun-${name}.png`) });
        }
        console.log('Reference captures complete; browser:', browser.version());
    } finally {
        await browser.close();
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
