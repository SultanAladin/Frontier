// Chromium renders SVG filters unsupported by the pinned ThorVG. The PNG is supplied for later integration into the editor icon atlas.
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('playwright-core');
const Chromium = require('@sparticuz/chromium').default;
(async () => {
    const source = process.argv[2] || 'Experimental/EnvironmentInspectors/Icons/SolarReference.svg';
    const destination = process.argv[3] || 'Experimental/EnvironmentInspectors/Icons/SolarReference.png';
    const browser = await chromium.launch({ executablePath: await Chromium.executablePath(), args: Chromium.args });
    try {
        const page = await browser.newPage({ viewport: { width: 96, height: 96 }, deviceScaleFactor: 1 });
        const svg = fs.readFileSync(source).toString('base64');
        await page.setContent(`<style>html,body{margin:0;background:transparent} img{width:96px;height:96px}</style><img src="data:image/svg+xml;base64,${svg}">`);
        await page.evaluate(() => document.querySelector('img').decode());
        await page.screenshot({ path: path.resolve(destination), omitBackground: true });
    } finally {
        await browser.close();
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
