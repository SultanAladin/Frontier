/*============================================================================================================================================
                                                   ATMOSPHEREEDITORVALIDATION.CJS
============================================================================================================================================*/
// 🧩 Browser interaction and WebGL pixel validation for the atmosphere prototype.
'use strict';
const assert = require('node:assert/strict');
const path = require('node:path');
const fs = require('node:fs');
const {chromium} = require('playwright-core');
const Chromium = require('@sparticuz/chromium').default;
const Output = path.join(__dirname, 'Images');
const Url = process.env.ATMOSPHERE_URL || 'http://127.0.0.1:4173/';

async function Sample(Page)
{
    await Page.waitForTimeout(180);
    return Page.evaluate(() => {
        const Canvas = document.getElementById('AtmosphereCanvas');
        const Gl = Canvas.getContext('webgl2');
        const Pixels = new Uint8Array(Canvas.width * Canvas.height * 4);
        Gl.readPixels(0, 0, Canvas.width, Canvas.height, Gl.RGBA, Gl.UNSIGNED_BYTE, Pixels);
        const Samples = [];
        for (let Y = 1; Y < 80; ++Y) for (let X = 1; X < 80; ++X) {
            const Index = (Math.floor(Y * Canvas.height / 80) * Canvas.width + Math.floor(X * Canvas.width / 80)) * 4;
            Samples.push(Pixels[Index], Pixels[Index + 1], Pixels[Index + 2]);
        }
        return {Samples, Error: Gl.getError(), Width: Canvas.width, Height: Canvas.height};
    });
}
function Difference(First, Second)
{
    return First.Samples.reduce((Total, Sample, Index) => Total + Math.abs(Sample - Second.Samples[Index]), 0);
}
async function SetSlider(Page, Name, Amount)
{
    await Page.locator('#' + Name).evaluate((Input, Amount) => {
        Input.value = String(Amount);
        Input.dispatchEvent(new Event('input', {bubbles: true}));
    }, Amount);
}
(async () => {
    const Browser = await chromium.launch({executablePath: process.env.CHROMIUM_PATH || await Chromium.executablePath(),
        args: [...Chromium.args.filter(Arg => !['--single-process', '--in-process-gpu'].includes(Arg)),
               '--enable-unsafe-swiftshader', '--use-gl=angle', '--use-angle=swiftshader']});
    const Page = await Browser.newPage({viewport: {width: 1440, height: 900}, deviceScaleFactor: 1});
    const Errors = [];
    Page.on('pageerror', Error => Errors.push(Error.message));
    try {
        await Page.goto(Url, {waitUntil: 'networkidle', timeout: 60000});
        await Page.locator('canvas[data-rendered="true"]').waitFor({timeout: 60000});
        assert.equal(await Page.locator('#RenderNotice').isHidden(), true, 'WebGL preview is unavailable');
        assert.equal(await Page.locator('.atmosphere-card:visible').count(), 1, 'expected exactly one visible editor card');
        assert.equal(await Page.getByRole('button', {name: 'Next editor: Geometry detail'}).isEnabled(), true, 'editor navigation unavailable');
        const Dock = await Page.locator('#AtmosphereCard').boundingBox();
        const Viewport = await Page.locator('#AtmosphereViewport').boundingBox();
        assert.ok(Math.abs(Dock.x + Dock.width - 1440) < 1 && Math.abs(Dock.y) < 1 && Math.abs(Dock.height - 900) < 1, 'card is not docked to the right');
        assert.ok(Math.abs(Viewport.width - Dock.x) < 1, 'docked card overlaps the preview');
        const Default = await Sample(Page);
        // Compare the shader's upward solar transmission against the analytic integrals of
        // the exponential 8 km / 1.2 km profiles and 10–40 km triangular ozone layer.
        const PhysicalProbe = await Page.evaluate(() => {
            const Pixel = new Uint8Array(4);
            Gl.uniform1f(Locations.ValidationMode, 1); Gl.drawArrays(Gl.TRIANGLES, 0, 3);
            Gl.readPixels(0, 0, 1, 1, Gl.RGBA, Gl.UNSIGNED_BYTE, Pixel);
            const Transmission = Array.from(Pixel.slice(0, 3), Amount => Amount / 255);
            Gl.uniform1f(Locations.ValidationMode, 2); Gl.drawArrays(Gl.TRIANGLES, 0, 3);
            Gl.readPixels(0, 0, 1, 1, Gl.RGBA, Gl.UNSIGNED_BYTE, Pixel);
            const Shadowed = Pixel[0] > 250;
            RenderAtmosphere();
            return {Transmission, Shadowed};
        });
        const RayleighDepth = 8 * Math.exp(-.1 / 8);
        const MieDepth = 1.2 * Math.exp(-.1 / 1.2);
        const Expected = [.005802, .013558, .0331].map((Beta, Index) =>
            Math.exp(-Beta * RayleighDepth - .0044 * MieDepth - [.000650, .001881, .000085][Index] * 15));
        assert.ok(PhysicalProbe.Transmission.every((Amount, Index) => Math.abs(Amount - Expected[Index]) < .025),
            'solar optical depth differs from the analytic atmosphere');
        assert.equal(PhysicalProbe.Shadowed, true, 'planet should occlude downward sunlight');
        assert.equal(Default.Error, 0, 'WebGL error in initial render');
        assert.ok(Default.Samples.some(Channel => Channel > 80), 'preview appears blank');
        await Page.screenshot({path: path.join(Output, 'orbit.png')});
        const Differences = {};
        for (const [Name, Amount] of [['SunElevation', -6], ['Rayleigh', 2.5], ['Mie', 4], ['Ozone', 3], ['Exposure', 2]]) {
            await Page.locator('#ResetAtmosphere').click();
            await SetSlider(Page, Name, Amount);
            const Result = await Sample(Page);
            Differences[Name] = Difference(Default, Result);
            assert.equal(Result.Error, 0, Name + ' produced a WebGL error');
            assert.ok(Differences[Name] > 30, Name + ' did not change the rendered atmosphere');
            assert.ok((await Page.locator('#' + Name + 'Readout').textContent()).includes(Name === 'SunElevation' ? '-6' : Amount.toFixed(2)));
        }
        assert.equal(await Page.locator('.visual-parameter').count(), 12, 'expected a chart control for every range');
        assert.equal(await Page.getByRole('slider', {name: /Air molecules/}).count(), 1, 'chart input is missing from the accessibility tree');
        const BeforeMarker = await Page.locator('#Rayleigh').locator('..').locator('.control-marker').getAttribute('cx');
        const Rail = await Page.locator('#Rayleigh').boundingBox();
        await Page.mouse.click(Rail.x + Rail.width * .9, Rail.y + Rail.height / 2);
        assert.ok(Number(await Page.locator('#Rayleigh').inputValue()) > 2.5, 'pointer did not move the infographic control');
        assert.notEqual(await Page.locator('#Rayleigh').locator('..').locator('.control-marker').getAttribute('cx'), BeforeMarker, 'chart marker did not follow the pointer');
        await Page.locator('#SunElevation').focus();
        await Page.keyboard.press('Home');
        assert.equal(await Page.locator('#SunElevation').inputValue(), '-12', 'keyboard range Home failed');
        await Page.keyboard.press('ArrowRight');
        assert.equal(await Page.locator('#SunElevation').inputValue(), '-11', 'keyboard range arrow failed');
        assert.ok(Number(await Page.locator('#SunElevation').locator('..').locator('.control-marker').getAttribute('cx')) < 10, 'chart marker did not follow keyboard input');
        await Page.locator('#ResetAtmosphere').click();
        const Reset = await Sample(Page);
        assert.equal(Difference(Default, Reset), 0, 'reset did not restore the default rendering');
        await Page.locator('#AtmosphereEnabled').uncheck();
        const Disabled = await Sample(Page);
        Differences.Disabled = Difference(Default, Disabled);
        assert.ok(Differences.Disabled > 100, 'atmosphere switch did not change the preview');
        await Page.locator('#ResetAtmosphere').click();
        await Page.locator('#CloudsEnabled').uncheck();
        const Clear = await Sample(Page);
        Differences.CloudLayer = Difference(Default, Clear);
        assert.ok(Differences.CloudLayer > 1000, 'the cloud switch did not change the planet');
        await Page.locator('#CloudsEnabled').check();
        await SetSlider(Page, 'CloudCoverage', 0);
        Differences.CloudCoverage = Difference(Default, await Sample(Page));
        assert.ok(Differences.CloudCoverage > 1000, 'cloud coverage did not affect the preview');
        await Page.locator('#ResetAtmosphere').click();
        await Page.locator('#LimbEnabled').uncheck();
        Differences.Limb = Difference(Default, await Sample(Page));
        assert.ok(Differences.Limb > 10, 'rim enhancement did not alter the atmosphere');
        assert.equal(await Page.locator('#LimbReadout').textContent(), 'PHYSICAL RIM');
        await Page.locator('#ResetAtmosphere').click();
        const Canvas = Page.locator('#AtmosphereCanvas');
        await Canvas.hover({position: {x: 700, y: 400}});
        await Page.mouse.down();
        const Lag = await Page.evaluate(() => {
            const R = AtmosphereCanvas.getBoundingClientRect();
            AtmosphereCanvas.dispatchEvent(new PointerEvent('pointermove', {clientX: R.left + 820, clientY: R.top + 440, pointerId: 1, bubbles: true}));
            return {Target: OrbitAim.Yaw, Current: Parameters.Yaw};
        });
        assert.ok(Math.abs(Lag.Target - Lag.Current) > .2, 'camera moved immediately instead of lagging');
        await Page.mouse.up();
        await Page.waitForFunction(() => Math.abs(OrbitAim.Yaw - Parameters.Yaw) < .001, null, {timeout: 15000});
        const Orbited = await Sample(Page);
        Differences.Orbit = Difference(Default, Orbited);
        assert.ok(Differences.Orbit > 100, 'dragging did not orbit the planet');
        await Page.locator('#PlanetPreset').selectOption('Mars');
        assert.equal(await Page.locator('#PlanetReadout').textContent(), 'MARS');
        assert.equal(await Page.locator('#CloudsEnabled').isChecked(), false, 'Mars should start without the cloud overlay');
        assert.ok((await Page.locator('#HeightReadout').textContent()).includes('11.1'));
        const Mars = await Sample(Page);
        Differences.Mars = Difference(Default, Mars);
        assert.ok(Differences.Mars > 1000, 'Mars preset did not change the preview');
        await SetSlider(Page, 'Ozone', 2);
        Differences.MarsOzone = Difference(Mars, await Sample(Page));
        assert.ok(Differences.MarsOzone > 30, 'Mars ozone control did not affect the preview');
        await Page.locator('#ResetAtmosphere').click();
        await Page.locator('#AtmosphereCard').evaluate(Dock => { Dock.scrollTop = 0; });
        await Page.screenshot({path: path.join(Output, 'mars.png')});
        await SetSlider(Page, 'Rayleigh', 2);
        await Page.locator('#ResetAtmosphere').click();
        assert.equal(await Page.locator('#PlanetPreset').inputValue(), 'Mars', 'reset changed the selected planet');
        assert.equal(await Page.locator('#Rayleigh').inputValue(), '1', 'reset did not restore the selected planet');
        await Page.locator('#PlanetPreset').selectOption('Venus');
        assert.equal(await Page.locator('#CloudsEnabled').isChecked(), true, 'Venus should start with clouds');
        const Venus = await Sample(Page);
        Differences.Venus = Difference(Mars, Venus);
        assert.ok(Differences.Venus > 1000, 'Venus preset did not change the preview');
        await Page.locator('#AtmosphereCard').evaluate(Dock => { Dock.scrollTop = 0; });
        await Page.screenshot({path: path.join(Output, 'venus.png')});
        await Page.locator('#PlanetPreset').selectOption('Earth');
        await Page.getByRole('button', {name: 'Surface view'}).click();
        assert.equal(await Page.getByRole('button', {name: 'Surface view'}).getAttribute('aria-pressed'), 'true');
        const Surface = await Sample(Page);
        Differences.Surface = Difference(Default, Surface);
        assert.ok(Differences.Surface > 1000, 'surface view did not alter the rendered camera');
        await Page.screenshot({path: path.join(Output, 'surface.png')});
        await SetSlider(Page, 'Mie', 5);
        Differences.SurfaceMie = Difference(Surface, await Sample(Page));
        assert.ok(Differences.SurfaceMie > 100, 'aerosols did not change the surface sky');
        await Page.locator('#ResetAtmosphere').click();
        await SetSlider(Page, 'SunElevation', -4);
        const Twilight = await Sample(Page);
        Differences.Twilight = Difference(Surface, Twilight);
        assert.ok(Differences.Twilight > 100, 'sun elevation did not change the surface sky');
        await Page.screenshot({path: path.join(Output, 'surface-twilight.png')});
        await Page.setViewportSize({width: 390, height: 844});
        await Page.getByRole('button', {name: 'Orbital view'}).click();
        await Page.locator('#ResetAtmosphere').click();
        await Sample(Page);
        assert.equal(await Page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true, 'mobile layout overflows horizontally');
        assert.ok(await Page.locator('#Exposure').isVisible(), 'mobile card controls are unavailable');
        await Page.screenshot({path: path.join(Output, 'mobile.png'), fullPage: true});
        assert.deepEqual(Errors, [], 'uncaught browser exceptions');
        const Results = {renderer: 'Chromium WebGL2 / SwiftShader', width: Default.Width, height: Default.Height,
            checks: 'passed', cameraLag: Lag, physicalProbe: PhysicalProbe, analyticTransmission: Expected, pixelDifferences: Differences, browserErrors: Errors};
        fs.writeFileSync(path.join(__dirname, 'Results.json'), JSON.stringify(Results, null, 2) + '\n');
        console.log(JSON.stringify(Results, null, 2));
    } finally { await Browser.close(); }
})().catch(Error => { console.error(Error); process.exitCode = 1; });
