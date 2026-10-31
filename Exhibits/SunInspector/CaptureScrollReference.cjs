// Capture lower portions of the real, scrollable VehicleHMI inspectors.
const fs = require('node:fs');
const path = require('node:path');
const {chromium} = require('playwright-core');
const Chromium = require('@sparticuz/chromium').default;
(async () => {
    const browser = await chromium.launch({executablePath: process.env.CHROMIUM_PATH || await Chromium.executablePath(),
        args: [...Chromium.args.filter(a => !['--single-process', '--in-process-gpu', '--disable-web-security'].includes(a)),
            '--enable-unsafe-webgpu', '--use-webgpu-adapter=swiftshader', '--enable-features=Vulkan',
            '--use-vulkan=swiftshader', '--disable-vulkan-surface', '--enable-unsafe-swiftshader']});
    try {
        const page = await browser.newPage({viewport: {width: 1440, height: 900}, deviceScaleFactor: 1});
        await page.addInitScript(() => {
            const request = requestAnimationFrame.bind(window);
            window.requestAnimationFrame = callback => request(time => {
                if (window.IlluminationExhibit?.FrameIndex >= 1) return;
                callback(time);
            });
        });
        const url = (process.env.REFERENCE_URL || 'http://127.0.0.1:8080/') +
                    '?quality=low&scale=.25&stresspool=0&inspector=sun';
        await page.goto(url, {waitUntil: 'domcontentloaded'});
        await page.waitForFunction(() => window.IlluminationExhibit?.FrameIndex >= 1, null,
                                   {polling: 100, timeout: 300000});
        const output = path.join(__dirname, 'Images', 'ScrollReference');
        fs.mkdirSync(output, {recursive: true});
        for (const id of ['sun', 'clouds', 'atmosphere', 'flare']) {
            await page.evaluate(name => window.IlluminationExhibit.Shell.Select(name), id);
            const selector = {sun: '.sun-observatory', clouds: '.cloud-observatory',
                              atmosphere: '.atmosphere-observatory', flare: '.lens-optics'}[id];
            await page.waitForFunction(s => !!document.querySelector(s), selector, {timeout: 30000, polling: 100});
            const root = page.locator(selector);
            await root.waitFor({state: 'visible', timeout: 30000});
            const scroll = page.locator('.inspector-content');
            const tabs = root.getByRole('tab');
            const count = await tabs.count();
            for (let i = 0; i < count; ++i) {
                await scroll.evaluate(e => {e.scrollTop = 0;});
                const label = await tabs.nth(i).textContent();
                await tabs.nth(i).click();
                await page.waitForFunction(({selector, index}) =>
                    document.querySelectorAll(selector + ' [role="tab"]')[index]?.getAttribute('aria-selected') === 'true',
                    {selector, index: i}, {timeout: 30000, polling: 100});
                const metrics = await scroll.evaluate(e => ({max: e.scrollHeight - e.clientHeight}));
                const offsets = [0];
                for (let offset = 600; offset < metrics.max - 100; offset += 600) offsets.push(offset);
                if (metrics.max > 0) offsets.push(metrics.max);
                for (const offset of offsets) {
                    await scroll.evaluate((e, y) => {e.scrollTop = y;}, offset);
                    await page.waitForTimeout(30);
                    await scroll.screenshot({path: path.join(output, `${id}-${i}-${offset}.png`)});
                }
                console.log(`${id} tab ${i} ${label}: scroll max ${metrics.max}; captured ${offsets.join(',')}`);
            }
        }
    } finally { await browser.close(); }
})().catch(error => {console.error(error); process.exitCode = 1;});
