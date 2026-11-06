/*==========================================================================================================================================
                                                        TERRAINBROWSERVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Actual applied noise terrain, camera panning/keyboard input, source-aligned controls, and CPU/GPU agreement.

import { createRequire } from 'node:module';
import Assert from 'node:assert/strict';
import { writeFile } from 'node:fs/promises';
import { TerrainCategories } from '../../Experimental/LandscapeWorkspace/TerrainNoise.js';
import { CreateTerrainWorld } from '../../Experimental/LandscapeWorkspace/TerrainExamples.js';
import { CreateLandform } from '../../Experimental/LandscapeWorkspace/LandformStack.js';
import { VolumeDistance } from '../../Experimental/LandscapeWorkspace/DistanceField.js';
const Require=createRequire(new URL('../../Experimental/LandscapeWorkspace/package.json',import.meta.url));
const {chromium:BrowserType}=Require('playwright'),Chromium=Require('@sparticuz/chromium').default;
const Browser=await BrowserType.launch({executablePath:await Chromium.executablePath(),args:[...Chromium.args,'--enable-webgl','--ignore-gpu-blocklist'],headless:true});
const Page=await Browser.newPage({viewport:{width:1600,height:1100},deviceScaleFactor:1}),Results=[],Errors=[];
Page.on('pageerror',Failure=>Errors.push(Failure.message));
async function Check(Name,Evaluation){await Evaluation();Results.push({Check:Name,Result:'Passed'});console.log('Passed:',Name);}
const Pose=()=>Page.locator('#Volume').getAttribute('data-camera-target');
async function Drag(Options={})
{
    const Bounds=await Page.locator('#Volume').boundingBox();await Page.mouse.move(Bounds.x+Bounds.width*.5,Bounds.y+Bounds.height*.6);
    await Page.mouse.down(Options);await Page.mouse.move(Bounds.x+Bounds.width*.5+70,Bounds.y+Bounds.height*.6+35,{steps:5});await Page.mouse.up(Options);await Page.waitForTimeout(100);
}
async function Import(Layers)
{
    await Page.locator('#File').setInputFiles({name:'Terrain.json',mimeType:'application/json',buffer:Buffer.from(JSON.stringify({Format:'FrontierLandscape/1',Layers}))});await Page.waitForTimeout(80);
}
async function Probe(Points)
{
    return Page.evaluate(Points=>
    {
        const GL=document.querySelector('#Volume').getContext('webgl2'),Program=GL.getParameter(GL.CURRENT_PROGRAM);GL.disable(GL.DITHER);const Viewport=GL.getParameter(GL.VIEWPORT);GL.viewport(0,0,1,1);
        GL.uniform1i(GL.getUniformLocation(Program,'Probe'),1);GL.uniform1i(GL.getUniformLocation(Program,'ProbeDistance'),1);const Distances=[];
        for(const Point of Points)
        {
            GL.uniform3fv(GL.getUniformLocation(Program,'ProbePoint'),Point);GL.drawArrays(GL.TRIANGLES,0,3);
            const Pixel=new Uint8Array(4);GL.readPixels(0,0,1,1,GL.RGBA,GL.UNSIGNED_BYTE,Pixel);Distances.push(new DataView(Pixel.buffer).getFloat32(0,true));
        }
        GL.uniform1i(GL.getUniformLocation(Program,'Probe'),0);GL.uniform1i(GL.getUniformLocation(Program,'ProbeDistance'),0);GL.viewport(...Viewport);return Distances;
    },Points);
}
try
{
    await Page.goto(process.env.LANDSCAPE_URL||'http://localhost:5174');await Page.waitForSelector('#tp-layer-list .tp-layer-row');
    await Check('Fresh landscape already contains applied fractal terrain, not just preset cards',async()=>
    {
        Assert.equal(await Page.locator('#Volume').isVisible(),true);Assert.equal(await Page.locator('.layer').count(),3);
        Assert.match(await Page.locator('.tp-layer-name').first().textContent(),/Fractal/);Assert.equal(await Page.locator('#PresetCards').isVisible(),false);
        Assert.equal(await Page.locator('#KeyboardCamera').isChecked(),false);
    });
    await Check('Source-aligned charcoal outliner, neutral docks and cyan selected rows',async()=>
    {
        Assert.match(await Page.locator('.tp-document-tab').first().textContent(),/Landform layers/);
        Assert.equal(await Page.locator('.stack').evaluate(Panel=>getComputedStyle(Panel).backgroundColor),'rgb(14, 14, 16)');
        Assert.match(await Page.locator('#tp-layer-list .tp-layer-row.selected').evaluate(Row=>getComputedStyle(Row,'::before').backgroundColor),/90, 200, 250/);
        Assert.equal(await Page.locator('#LandformType optgroup[label="Landform generators"] option[value="arch"]').count(),0);
        for(const Type of Object.keys(TerrainCategories))Assert.equal(await Page.locator(`#LandformType optgroup[label="Landform generators"] option[value="${Type}"]`).count(),1);
    });
    await Check('Explicit Pan tool translates the camera without orbiting',async()=>
    {
        await Page.click('#PanCamera');const Before=await Pose(),Yaw=await Page.locator('#Volume').getAttribute('data-camera-yaw');await Drag();Assert.notEqual(await Pose(),Before);Assert.equal(await Page.locator('#Volume').getAttribute('data-camera-yaw'),Yaw);
    });
    await Check('Middle-mouse and Shift-drag always pan, even in Orbit mode',async()=>
    {
        await Page.click('#OrbitCamera');let Before=await Pose();await Drag({button:'middle'});Assert.notEqual(await Pose(),Before);
        Before=await Pose();await Page.keyboard.down('Shift');await Drag();await Page.keyboard.up('Shift');Assert.notEqual(await Pose(),Before);
    });
    await Check('WASD is opt-in and moves only the camera when enabled',async()=>
    {
        let Before=await Pose();await Page.keyboard.down('w');await Page.waitForTimeout(200);await Page.keyboard.up('w');Assert.equal(await Pose(),Before);
        await Page.check('#KeyboardCamera');Before=await Pose();await Page.keyboard.down('w');await Page.waitForFunction(Before=>document.querySelector('#Volume').dataset.cameraTarget!==Before,Before,{timeout:15000});await Page.keyboard.up('w');Assert.notEqual(await Pose(),Before);
        const Stopped=await Pose();await Page.waitForTimeout(150);Assert.equal(await Pose(),Stopped);
    });
    await Check('Typing, blur and disabling keyboard movement prevent stuck camera motion',async()=>
    {
        await Page.locator('#CameraSpeed').focus();let Before=await Pose();await Page.keyboard.down('w');await Page.waitForTimeout(200);await Page.keyboard.up('w');Assert.equal(await Pose(),Before);
        await Page.locator('#OrbitCamera').focus();await Page.keyboard.down('d');await Page.waitForTimeout(100);await Page.evaluate(()=>window.dispatchEvent(new Event('blur')));await Page.waitForTimeout(100);Before=await Pose();await Page.waitForTimeout(150);Assert.equal(await Pose(),Before);await Page.keyboard.up('d');await Page.uncheck('#KeyboardCamera');
    });
    await Check('Eight generator examples are already applied on independent mask terrains and replacement is undoable',async()=>
    {
        Page.once('dialog',Dialog=>Dialog.accept());await Page.click('#LoadTerrainExamples');Assert.equal(await Page.locator('.layer').count(),8);
        const [Download]=await Promise.all([Page.waitForEvent('download'),Page.click('#Export')]);const Document=JSON.parse(await (await Download.createReadStream()).toArray().then(Chunks=>Buffer.concat(Chunks).toString()));
        Assert.deepEqual(Document.Layers.map(Layer=>Layer.Landforms.at(-1).Type),[...Object.keys(TerrainCategories),'craters']);
        await Page.click('#HistoryUndo');Assert.equal(await Page.locator('.layer').count(),3);
    });
    await Check('All seven new volumetric noise generators agree between CPU and GPU, including negative coordinates',async()=>
    {
        let MaximumError=0,Samples=0;
        for(const Type of Object.keys(TerrainCategories))
        {
            const Layer=CreateTerrainWorld()[0];Layer.Landforms=[{...CreateLandform(Type),Blend:0}];await Import([Layer]);await Page.click('#Build');
            const Region=Layer.Regions[0],X=(Region.X0+Region.X1)*.5,Z=(Region.Z0+Region.Z1)*.5;
            const Points=Array.from({length:40},(_,Index)=>[X+Math.sin(Index*1.7)*300,Math.sin(Index*.6)*160,Z+Math.cos(Index*.93)*280]);
            const Observed=await Probe(Points);Points.forEach((Point,Index)=>{const Error=Math.abs(VolumeDistance(Layer,...Point)-Observed[Index]);MaximumError=Math.max(MaximumError,Error);Assert.ok(Error<.003,`${Type} point ${Index}: ${Error}`);Samples++;});
        }
        Results.push({Measurement:'New noise generator CPU/GPU agreement',Samples,MaximumAbsoluteError:MaximumError});
    });
    await Check('Applied terrain controls edit frequency, roughness, amplitude and seed on the active mask',async()=>
    {
        const Layer=CreateTerrainWorld()[0];await Import([Layer]);await Page.locator('[data-property="Amount"]').fill('220');await Page.locator('[data-property="Amount"]').dispatchEvent('change');
        Assert.equal(await Page.locator('[data-property="Amount"]').inputValue(),'220');Assert.equal(await Page.locator('[data-property="Scale"]').getAttribute('aria-label'),'Frequency / density');
        await Page.selectOption('[data-property="Type"]','multifractal');Assert.equal(await Page.locator('[data-property="Type"]').inputValue(),'multifractal');await Page.click('#HistoryUndo');Assert.equal(await Page.locator('[data-property="Type"]').inputValue(),'fractal');
    });
    Page.once('dialog',Dialog=>Dialog.accept());await Page.click('#LoadTerrainExamples');await Page.selectOption('#RenderQuality','360');
    await Page.evaluate(()=>document.querySelector('.inspector').scrollTop=0);await Page.click('#FocusMask');
    await Page.waitForFunction(()=>document.querySelector('#Volume').dataset.completedOrbit===String(720*1.2)&&document.querySelector('#Volume').dataset.completedTarget===document.querySelector('#Volume').dataset.cameraTarget&&document.querySelector('#Volume').dataset.completedRevision===document.querySelector('#Volume').dataset.volumeRevision,{},{timeout:90000});await Page.waitForTimeout(1500);
    await Page.screenshot({path:new URL('./AppliedTerrainGenerators.png',import.meta.url).pathname,timeout:90000});
    await Page.selectOption('#ActiveMask','2');await Page.click('#FocusMask');await Page.waitForFunction(()=>document.querySelector('#Volume').dataset.completedTarget===document.querySelector('#Volume').dataset.cameraTarget&&document.querySelector('#Volume').dataset.completedRevision===document.querySelector('#Volume').dataset.volumeRevision,{},{timeout:90000});
    await Page.screenshot({path:new URL('./AppliedRidgedMultifractal.png',import.meta.url).pathname,timeout:90000});
    Assert.deepEqual(Errors,[]);await writeFile(new URL('./TerrainBrowserResults.json',import.meta.url),JSON.stringify({URL:process.env.LANDSCAPE_URL||'http://localhost:5174',Results,Errors},null,2));
}
finally{await Browser.close();}
