// Real Chromium/WebGPU execution and screenshots. Requires a running local preview server.
import { chromium as Playwright } from "playwright-core";
import Chromium from "@sparticuz/chromium";
import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";

const Output = path.resolve(process.env.EVIDENCE || "../../../Exhibits/IlluminationConsolidation/Results/Browser");
fs.mkdirSync(Output, { recursive: true });
const Executable = process.env.CHROMIUM_EXECUTABLE || await Chromium.executablePath();
const Browser = await Playwright.launch({ executablePath: Executable, headless: true,
    args: [...Chromium.args, "--enable-unsafe-webgpu", "--use-angle=swiftshader", "--enable-features=Vulkan",
           "--use-vulkan=swiftshader", "--disable-vulkan-surface"] });
const Errors = [];
const Console = [];
try
{
    const Page = await Browser.newPage({ viewport: { width: 1280, height: 720 } });
    Page.on("console", Message => { Console.push(`${Message.type()}: ${Message.text()}`); if (Message.type() === "error") Errors.push(Message.text()); });
    Page.on("pageerror", Failure => Errors.push(Failure.message));
    await Page.goto((process.env.URL || "http://127.0.0.1:8080/") + "?capture=1&quality=low&renderer=bvh&scale=0.25&stresspool=16&bounces=1&reflbounces=1&candidates=1");
    await Page.waitForFunction(() => globalThis.IlluminationExhibit?.Ready, null, { timeout: 180000 });
    await Page.waitForFunction(() => globalThis.IlluminationExhibit.FrameIndex >= 2, null, { timeout: 120000 });
    await Page.evaluate(() => globalThis.IlluminationExhibit.Shell.Select("render"));
    await Page.screenshot({ path: path.join(Output, "CombinedBrowser.png") });
    // Exercise the real DOM controls, not just shader source matching.
    // Programmatic mode changes exercise command/resource paths; DOM click below also verifies switch interaction.
    const States = [];
    for (const Mode of [false, true])
    {
        const Start = await Page.evaluate(Mode =>
        {
            const Host = globalThis.IlluminationExhibit;
            Object.assign(Host.Settings, { ScreenTraversal: Mode, ClusteredLights: Mode, PhotometricLights: Mode,
                XpbdTyres: Mode, RayTracing: Mode });
            Host.ApplySetting("RayTracing");
            return Host.FrameIndex;
        }, Mode);
        await Page.waitForFunction(Start => globalThis.IlluminationExhibit.FrameIndex >= Start + 2, Start, { timeout: 120000 });
        States.push(await Page.evaluate(() =>
        {
            const Host = globalThis.IlluminationExhibit;
            return { Frame: Host.FrameIndex, Screen: Host.Settings.ScreenTraversal, BVH: Host.Settings.RayTracing,
                Xpbd: Host.Vehicle.Wheels.every(Wheel => Wheel.CarcassActive),
                Photometry: Host.Lighting.Profile.Description, Errors: Host.Exchange.ReportedErrors };
        }));
    }
    // Import the real bundled file through the browser file picker too.
    await Page.evaluate(() => globalThis.IlluminationExhibit.Shell.Select("render"));
    const Choosing = Page.waitForEvent("filechooser");
    await Page.getByRole("button", { name: "Import .ies", exact: true }).click();
    await (await Choosing).setFiles(path.resolve("../Archive/Photometry/BEGA-50975.ies"));
    // Click the actual custom switch: its label has a click listener on the enclosing button.
    await Page.evaluate(() => globalThis.IlluminationExhibit.Shell.Select("render"));
    await Page.getByRole("switch", { name: "Screen-first tracing", exact: true }).click();
    assert.equal(await Page.evaluate(() => globalThis.IlluminationExhibit.Settings.ScreenTraversal), false);
    await Page.getByRole("switch", { name: "Screen-first tracing", exact: true }).click();
    assert.equal(await Page.evaluate(() => globalThis.IlluminationExhibit.Settings.ScreenTraversal), true);
    assert.equal(States[0].Xpbd, false);
    assert.equal(States[1].Xpbd, true);
    const Benchmarks = [];
    if (process.env.BENCHMARK === "1")
    {
        for (const Key of ["ClusteredLights", "ScreenTraversal"])
        {
            await Page.evaluate(Key => globalThis.IlluminationExhibit.Comparison.Start(Key, 2, 6), Key);
            await Page.waitForFunction(() => !globalThis.IlluminationExhibit.Comparison.Active, null, { timeout: 180000 });
            Benchmarks.push(await Page.evaluate(() => globalThis.IlluminationExhibit.Comparison.Report));
        }
    }
    const Adapter = await Page.evaluate(() => globalThis.IlluminationExhibit.Exchange.QueryAdapterDescription());
    await Page.screenshot({ path: path.join(Output, "CombinedControls.png") });
    const Report = { Browser: Browser.version(), Adapter, States, Benchmarks, Errors, Viewport: [1280, 720],
        Note: "SwiftShader software GPU, BVH and SDF routes with screen traces. BVH mode validated separately with the dedicated Dawn fixture. Short fixed-view paired timings per toggle; no universal speed claim." };
    fs.writeFileSync(path.join(Output, "BrowserValidation.json"), JSON.stringify(Report, null, 2));
    assert.deepEqual(Errors, []);
    assert.ok(States.every(State => State.Errors.length === 0));
    console.log(JSON.stringify(Report, null, 2));
}
finally
{
    fs.writeFileSync(path.join(Output, "Console.txt"), Console.join("\n"));
    await Browser.close();
}
