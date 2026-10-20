// Actual Chromium/SwiftShader controls and images; requires Tools/Serve.py on port 8080.
import { chromium as Playwright } from "playwright-core";
import Chromium from "@sparticuz/chromium";
import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";

const Output = path.resolve(process.env.EVIDENCE || "../../../Exhibits/IlluminationConsolidation/Results/TyreModels");
fs.mkdirSync(Output, { recursive: true });
const Browser = await Playwright.launch({ executablePath: process.env.CHROMIUM_EXECUTABLE || await Chromium.executablePath(),
    headless: true, args: [...Chromium.args, "--enable-unsafe-webgpu", "--use-angle=swiftshader", "--enable-features=Vulkan",
        "--use-vulkan=swiftshader", "--disable-vulkan-surface"] });
const Errors = [], Console = [], Models = [];
try
{
    const Page = await Browser.newPage({ viewport: { width: 1280, height: 800 } });
    Page.on("console", Message => { Console.push(Message.type() + ": " + Message.text()); if (Message.type() === "error") Errors.push(Message.text()); });
    Page.on("pageerror", Failure => Errors.push(Failure.message));
    await Page.goto((process.env.URL || "http://127.0.0.1:8080/") +
        "?capture=1&quality=low&renderer=bvh&scale=0.25&inspection=9&stresspool=8&bounces=1&reflbounces=1&candidates=1");
    await Page.waitForFunction(() => globalThis.IlluminationExhibit?.Ready && globalThis.IlluminationExhibit.FrameIndex >= 2, null, { timeout: 180000 });
    await Page.evaluate(() => globalThis.IlluminationExhibit.Shell.Select("render"));
    for (const [Index, Model] of ["spring", "xpbd", "shell", "solid"].entries())
    {
        // Exercise the actual user-facing selector, then settle the identical nominal-pressure pose on the CPU.
        await Page.getByRole("combobox", { name: "Tyre model", exact: true }).selectOption(Model);
        const Start = await Page.evaluate(Model =>
        {
            const Host = globalThis.IlluminationExhibit;
            Host.Settings.TyreWear = false; Host.Settings.TyreFailure = false;
            Host.Vehicle.TyreModel = Model; Host.Vehicle.XpbdEnabled = true;
            Host.Vehicle.Frozen = false;
            Host.Vehicle.Respawn([-100, -34, 0.53], 0);
            for (let Tick = 0; Tick < 360; ++Tick)
                Host.Vehicle.Advance({ Throttle: 0, Brake: 0, Steer: 0, Handbrake: false }, 1 / 120);
            Host.Vehicle.Frozen = true;
            Host.AssignCameraMode("Tyre FL");
            Host.PriorValid = false;
            return Host.FrameIndex;
        }, Model);
        await Page.waitForFunction(Start => globalThis.IlluminationExhibit.FrameIndex >= Start + 3, Start, { timeout: 180000 });
        const Reading = await Page.evaluate(() =>
        {
            const Host = globalThis.IlluminationExhibit;
            const Encoded = Host.VehicleVisual.EncodeRecord({}, Host.Vehicle.Wheels, Host.Vehicle.Orientation);
            return { Model: Host.Settings.TyreModel, ShaderCode: Encoded[21], RecordBytes: Encoded.byteLength,
                Wheels: Host.Vehicle.Wheels.map(Wheel => ({ DeflectionM: Wheel.TyreDeflection, ForceN: Wheel.TyreForce,
                    VolumeM3: Wheel.CarcassActive ? Wheel.Carcass.Volume : 0.034,
                    FemTableSolveResidualN: Wheel.Carcass.Residual || 0, Category: Wheel.Carcass.Category || "xpbd" })),
                Errors: Host.Exchange.ReportedErrors, Frame: Host.FrameIndex };
        });
        assert.equal(Reading.Model, Model); assert.equal(Reading.ShaderCode, Index);
        assert.equal(Reading.RecordBytes, 7888); assert.deepEqual(Reading.Errors, []);
        Models.push(Reading);
        await Page.screenshot({ path: path.join(Output, Model + ".png") });
        console.log("Rendered tyre model:", Model, Reading.Wheels[0]);
    }
    // Smoke-test the new model-pair comparison and restoration, not a performance benchmark.
    await Page.evaluate(() => globalThis.IlluminationExhibit.Comparison.Start("TyreModel", 0, 2, ["shell", "solid"]));
    await Page.waitForFunction(() => !globalThis.IlluminationExhibit.Comparison.Active, null, { timeout: 180000 });
    const Comparison = await Page.evaluate(() => globalThis.IlluminationExhibit.Comparison.Report);
    assert.deepEqual(Comparison.Results.map(Result => Result.Model), ["shell", "solid"]);
    assert.equal(await Page.evaluate(() => globalThis.IlluminationExhibit.Settings.TyreModel), "solid");
    assert.deepEqual(Errors, []);
    const Report = { Browser: Browser.version(), Models, Errors, ComparisonSmokeTest: Comparison,
        Note: "Actual software-GPU screenshots in albedo inspection mode to show shape without GI/exposure noise, at low trace resolution. Each pose CPU-settled for 3 seconds, then frozen for capture. Comparison timings are only a control-path smoke test, not performance evidence." };
    fs.writeFileSync(path.join(Output, "BrowserTyreModels.json"), JSON.stringify(Report, null, 2) + "\n");
}
finally
{
    fs.writeFileSync(path.join(Output, "Console.txt"), Console.join("\n"));
    await Browser.close();
}
