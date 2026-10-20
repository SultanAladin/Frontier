// Real Chromium/WebGPU execution and screenshots. Requires a running local preview server.
import { chromium as Playwright } from "playwright-core";
import Chromium from "@sparticuz/chromium";
import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";

const Output = path.resolve(process.env.EVIDENCE || "../../../Exhibits/IlluminationConsolidation/Results/ActualLights");
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
    await Page.getByRole("button", {name:"Show lights at night",exact:true}).click();
    const Start = await Page.evaluate(() => globalThis.IlluminationExhibit.FrameIndex);
    await Page.waitForFunction(Start => globalThis.IlluminationExhibit.FrameIndex >= Start+8, Start, {timeout:180000});
    const State = await Page.evaluate(() => {
        const H = globalThis.IlluminationExhibit;
        return {Lights:H.Lighting.Lights, Clustered:H.Settings.ClusteredLights, Emission:H.Settings.EmissionScale,
            Demo:H.Settings.DemonstrationLights, Errors:H.Exchange.ReportedErrors};
    });
    assert.equal(State.Demo,true); assert.equal(State.Emission,0);
    assert.equal(State.Lights.filter(L=>L.Intensity===120 && L.Reach===12).length,4);
    await Page.screenshot({path:path.join(Output,"ActualLights.png")});
    await Page.getByRole("switch",{name:"Cull actual lights",exact:true}).click();
    assert.equal(await Page.evaluate(()=>globalThis.IlluminationExhibit.Settings.ClusteredLights),false);
    const Next = await Page.evaluate(()=>globalThis.IlluminationExhibit.FrameIndex);
    await Page.waitForFunction(Start => globalThis.IlluminationExhibit.FrameIndex >= Start+2, Next, {timeout:120000});
    assert.equal(Errors.length,0,Errors.join("\n"));
    fs.writeFileSync(path.join(Output,"ActualLights.json"),JSON.stringify({State,Errors,Console},null,2));
    console.log("Actual light controls, rendering and culling toggle passed");
}
finally { await Browser.close(); }
