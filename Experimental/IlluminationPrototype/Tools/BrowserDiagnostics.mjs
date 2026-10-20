// Real Chromium/WebGPU execution and screenshots. Requires a running local preview server.
import { chromium as Playwright } from "playwright-core";
import Chromium from "@sparticuz/chromium";
import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";

const Output = path.resolve(process.env.EVIDENCE || "../../../Exhibits/IlluminationConsolidation/Results/Diagnostics");
fs.mkdirSync(Output, { recursive: true });
const Executable = process.env.CHROMIUM_EXECUTABLE || await Chromium.executablePath();
const Browser = await Playwright.launch({ executablePath: Executable, headless: true,
    args: [...Chromium.args, "--enable-unsafe-webgpu", "--use-angle=swiftshader", "--enable-features=Vulkan",
           "--use-vulkan=swiftshader", "--disable-vulkan-surface"] });
const Errors = [];
const Console = [];
try
{
    const Broken = await Browser.newPage({ viewport: {width:1280,height:720} });
    await Broken.route("**/Archive/Application.js*", Route => Route.fulfill({status:404,contentType:"text/javascript",body:"missing"}));
    await Broken.goto(process.env.URL || "http://127.0.0.1:8080/");
    await Broken.waitForFunction(() => document.getElementById("failure").textContent.includes("Application modules could not load"));
    assert.equal(await Broken.locator("#bring-up").isVisible(),false);
    assert.match(await Broken.locator("#failure").textContent(), /Application.js: HTTP 404/);
    await Broken.screenshot({path:path.join(Output,"MissingModule.png")});
    await Broken.unroute("**/Archive/Application.js*");
    const Page = Broken;
    await Page.route("**/node_modules/**", Route => Route.abort());
    await Page.route("**/Runtime/**", Route => Route.abort());
    await Page.route("**/Source/IlluminationHost.js*", Route => Route.abort());
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
    await Page.evaluate(() => {
        Object.defineProperty(navigator, 'clipboard', {configurable:true,value:{writeText:async Text => {globalThis.CopiedDiagnosticLog=Text;}}});
    });
    await Page.getByRole('button',{name:'Copy diagnostic log',exact:true}).click();
    const Copied = await Page.evaluate(()=>JSON.parse(globalThis.CopiedDiagnosticLog));
    assert.equal(Copied.Settings.RayTracing,true);
    assert.ok(Copied.FrameTiming.SmoothedFps > 0);
    await Page.evaluate(() => {navigator.clipboard.writeText=async()=>{throw new Error('denied');};});
    await Page.getByRole('button',{name:'Copy diagnostic log',exact:true}).click();
    const Field = Page.getByRole('textbox',{name:'Diagnostic log text',exact:true});
    await Field.waitFor();
    assert.equal(JSON.parse(await Field.inputValue()).Format,'Frontier rendering diagnostics v1');
    await Page.screenshot({path:path.join(Output,"CopyLog.png")});
    fs.writeFileSync(path.join(Output,'CopiedLog.json'),JSON.stringify(Copied,null,2));
    await Page.getByRole('button',{name:'Close',exact:true}).click();
    await Page.screenshot({path:path.join(Output,"Resumed.png")});
    await Page.getByRole("switch",{name:"Cull actual lights",exact:true}).click();
    assert.equal(await Page.evaluate(()=>globalThis.IlluminationExhibit.Settings.ClusteredLights),false);
    const Next = await Page.evaluate(()=>globalThis.IlluminationExhibit.FrameIndex);
    await Page.waitForFunction(Start => globalThis.IlluminationExhibit.FrameIndex >= Start+2, Next, {timeout:120000});
    assert.equal(Errors.length,0,Errors.join("\n"));
    fs.writeFileSync(path.join(Output,"Startup.json"),JSON.stringify({State,Errors,Console},null,2));
    console.log("Startup failure display, resumed rendering and actual light controls passed");
}
finally { await Browser.close(); }
