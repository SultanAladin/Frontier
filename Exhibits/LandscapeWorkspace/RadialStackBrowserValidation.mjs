/*==========================================================================================================================================
                                                     RADIALSTACKBROWSERVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Separate workspace sequences, persistent radial controls and actual CPU/GPU radial volume verification.

import { createRequire } from 'node:module';
import Assert from 'node:assert/strict';
import { readFile,writeFile } from 'node:fs/promises';
import { CreateLandform } from '../../Experimental/LandscapeWorkspace/LandformStack.js';
import { TerrainGeneratorCodes } from '../../Experimental/LandscapeWorkspace/TerrainInfluence.js';
import { VolumeDistance } from '../../Experimental/LandscapeWorkspace/DistanceField.js';
const Require=createRequire(new URL('../../Experimental/LandscapeWorkspace/package.json',import.meta.url));
const {chromium:BrowserType}=Require('playwright'),Chromium=Require('@sparticuz/chromium').default;
const Browser=await BrowserType.launch({executablePath:await Chromium.executablePath(),args:[...Chromium.args,'--enable-webgl','--ignore-gpu-blocklist'],headless:true});
const Page=await Browser.newPage({viewport:{width:1600,height:1100},deviceScaleFactor:1}),Results=[],Errors=[];
Page.on('pageerror',Failure=>Errors.push(Failure.message));
const Layer=(Name,X)=>({Name,Color:'#c6ad76',Visible:true,Depth:100,Caves:false,Resolution:256,Vertical:64,Strokes:[{Points:[[X,0]],Radius:300,Erase:false}],Regions:[{X0:X-320,Z0:-320,X1:X+320,Z1:320,Edge:0}],Landforms:[]});
const Fixture={Format:'FrontierLandscape/1',Layers:[Layer('Radial dunes',0),Layer('Second mask',850)]};
Fixture.Layers[0].Landforms=[-.16,.16].map((InfluenceX,Index)=>({...CreateLandform('dunes'),Amount:90-Index*15,Scale:2.5,InfluenceX,InfluenceRadius:.65,Seed:4029+Index}));
Fixture.Layers[1].Landforms=[CreateLandform('fractal')];
await Page.addInitScript(Document=>localStorage.setItem('FrontierLandscape',JSON.stringify(Document)),Fixture);
async function Check(Name,Evaluation){await Evaluation();Results.push({Check:Name,Result:'Passed'});console.log('Passed:',Name);}
async function Export(){const [Download]=await Promise.all([Page.waitForEvent('download'),Page.click('#Export')]);return JSON.parse(await readFile(await Download.path(),'utf8'));}
async function Import(Document){await Page.locator('#File').setInputFiles({name:'Radial.json',mimeType:'application/json',buffer:Buffer.from(JSON.stringify(Document))});await Page.waitForTimeout(80);}
async function Configure(Property,Amount){await Page.locator(`[data-property="${Property}"]`).fill(String(Amount));await Page.locator(`[data-property="${Property}"]`).dispatchEvent('change');}
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
    await Check('Landscape has only the active mask’s landform outliner and radial inspector',async()=>
    {
        Assert.equal(await Page.locator('#left-erosion-panels').isVisible(),true);Assert.equal(await Page.locator('#MaskLayerPane').isVisible(),false);
        Assert.equal(await Page.locator('#tp-layer-list [data-operation-index]').count(),2);Assert.equal(await Page.locator('.radial-influence-card').isVisible(),true);
        Assert.equal(await Page.locator('#BrushPanel').isVisible(),false);
    });
    await Check('Mask editor has a separate mask-document stack, not terrain operations',async()=>
    {
        await Page.click('#MaskTab');Assert.equal(await Page.locator('#MaskLayerPane').isVisible(),true);Assert.equal(await Page.locator('#left-erosion-panels').isVisible(),false);
        Assert.equal(await Page.locator('#MaskRowsContainer [data-mask-index]').count(),2);Assert.equal(await Page.locator('#LandformPanel').isVisible(),false);
        Assert.equal(await Page.locator('#BrushPanel').isVisible(),true);Assert.equal(await Page.locator('#Paint').isVisible(),true);
        Assert.equal(await Page.locator('#WorkspaceStackTitle').textContent(),'Mask layers');
    });
    await Check('Selecting a painted mask changes only the active mask’s independent Landscape stack',async()=>
    {
        const Before=await Export();await Page.locator('#MaskRowsContainer [data-mask-index="1"]').click();await Page.click('#LandscapeTab');
        Assert.equal(await Page.locator('#tp-layer-list [data-operation-index]').count(),1);Assert.equal(await Page.locator('[data-property="Type"]').inputValue(),'fractal');
        Assert.deepEqual(await Export(),Before);await Page.click('#MaskTab');await Page.locator('#MaskRowsContainer [data-mask-index="0"]').click();await Page.click('#LandscapeTab');
        Assert.equal(await Page.locator('#tp-layer-list [data-operation-index]').count(),2);Assert.equal(await Page.locator('[data-property="Type"]').inputValue(),'dunes');
    });
    await Check('The mask-stack plus button creates a mask, not a landform operation',async()=>
    {
        const Before=await Export();await Page.click('#MaskTab');await Page.click('#AddMaskLayer');const After=await Export();
        Assert.equal(After.Layers.length,3);Assert.deepEqual(After.Layers[0],Before.Layers[0]);Assert.equal(After.Layers[2].Landforms.length,0);
        await Page.click('#HistoryUndo');Assert.deepEqual(await Export(),Before);await Page.click('#LandscapeTab');
    });
    await Check('All five radial controls persist, undo independently and affect only the active mask',async()=>
    {
        await Import(Fixture);const Before=await Export();
        for(const [Property,Amount] of [['Influence',.65],['InfluenceRadius',.55],['InfluenceFalloff',.7],['InfluenceX',.1],['InfluenceZ',-.2]])
        {
            await Configure(Property,Amount);const Edited=await Export();Assert.equal(Edited.Layers[0].Landforms[1][Property],Amount);Assert.deepEqual(Edited.Layers[1],Before.Layers[1]);
            await Page.click('#HistoryUndo');Assert.deepEqual(await Export(),Before);
        }
    });
    await Check('Export and reopen retain radial position, radius, falloff and independent stacks',async()=>
    {
        await Configure('InfluenceRadius',.45);await Configure('InfluenceX',-.3);const Edited=await Export();await Import(Fixture);await Import(Edited);
        Assert.equal(await Page.locator('[data-property="InfluenceRadius"]').inputValue(),'0.45');Assert.equal(await Page.locator('[data-property="InfluenceX"]').inputValue(),'-0.3');Assert.deepEqual(await Export(),Edited);
    });
    await Check('All twelve terrain profiles, four combine modes and mixed structures agree between CPU and GPU',async()=>
    {
        let MaximumError=0,Samples=0;
        for(const Type of Object.keys(TerrainGeneratorCodes))
        {
            const Document=structuredClone(Fixture);Document.Layers=Document.Layers.slice(0,1);
            Document.Layers[0].Landforms=[{...CreateLandform('dunes'),InfluenceX:-.15,InfluenceRadius:.55},...['union','subtract','intersect','blend'].map((Mode,Index)=>({...CreateLandform(Type),Mode,Influence:1-Index*.15,InfluenceX:(Index-1.5)*.2,InfluenceRadius:.5,InfluenceFalloff:Index===0?.05:.7,Seed:4029+Index}))];
            if(Type==='badlands')Document.Layers[0].Landforms.splice(2,0,CreateLandform('arch'));
            await Import(Document);
            const Points=Array.from({length:40},(_,Index)=>[Math.sin(Index*1.7)*295,Math.sin(Index*.6)*160,Math.cos(Index*.93)*280]);
            Points.push([260,0,0],[260,.5,0],[260,-.5,0],[0,0,260],[-260,0,0]);
            const Observed=await Probe(Points);Points.forEach((Point,Index)=>{const Error=Math.abs(VolumeDistance(Document.Layers[0],...Point)-Observed[Index]);MaximumError=Math.max(MaximumError,Error);Assert.ok(Error<.003,`${Type} point ${Index}: ${Error}`);Samples++;});
        }
        Results.push({Measurement:'Radial volume CPU/GPU agreement',Samples,MaximumAbsoluteError:MaximumError});
    });
    await Check('Zero strength bypasses relief; explicit cave/arch generators do not acquire terrain influence controls',async()=>
    {
        const Document=structuredClone(Fixture);Document.Layers=Document.Layers.slice(0,1);Document.Layers[0].Landforms=[{...CreateLandform('dunes'),Influence:0}];await Import(Document);
        const Observed=await Probe([[0,0,0],[0,1,0],[0,-1,0]]);Assert.ok(Math.abs(Observed[0])<1e-5);Assert.ok(Observed[1]>0);Assert.ok(Observed[2]<0);
        await Page.selectOption('[data-property="Type"]','arch');Assert.equal(await Page.locator('.radial-influence-card').count(),0);
        await Page.selectOption('[data-property="Type"]','tunnel');Assert.equal(await Page.locator('.radial-influence-card').count(),0);
    });
    await Check('Foundation is exactly at ocean Y=0; increasing underwater depth does not raise terrain',async()=>
    {
        const Document=structuredClone(Fixture);Document.Layers=Document.Layers.slice(0,1);await Import(Document);
        const Points=[[270,0,0],[270,.1,0],[270,-.1,0],[0,40,0]];
        const Before=await Probe(Points);Assert.ok(Math.abs(Before[0])<1e-5);Assert.ok(Before[1]>0);Assert.ok(Before[2]<0);
        for(const Depth of [30,300])
        {
            await Page.locator('#Depth').fill(String(Depth));await Page.locator('#Depth').dispatchEvent('change');
            const Observed=await Probe(Points);Observed.forEach((Distance,Index)=>Assert.ok(Math.abs(Distance-Before[Index])<1e-5));
            Assert.equal((await Export()).Layers[0].Depth,Depth);
        }
    });
    await Import(Fixture);await Page.selectOption('#RenderQuality','720');await Page.click('#FocusMask');
    await Page.waitForFunction(()=>document.querySelector('#Volume').dataset.completedTarget===document.querySelector('#Volume').dataset.cameraTarget&&document.querySelector('#Volume').dataset.completedRevision===document.querySelector('#Volume').dataset.volumeRevision,{},{timeout:120000});
    await Page.evaluate(()=>document.querySelector('.inspector').scrollTop=0);await Page.screenshot({path:new URL('./SeaLevelDuneInfluence.png',import.meta.url).pathname,timeout:120000});
    const Flat=structuredClone(Fixture);Flat.Layers=Flat.Layers.slice(0,1);Flat.Layers[0].Landforms.forEach(Operation=>Operation.Influence=0);await Import(Flat);await Page.locator('#Wave').fill('0');await Page.locator('#Wave').dispatchEvent('input');await Page.click('#FocusMask');
    await Page.waitForFunction(()=>document.querySelector('#Volume').dataset.completedRevision===document.querySelector('#Volume').dataset.volumeRevision,{},{timeout:120000});await Page.screenshot({path:new URL('./SeaLevelFlatFoundation.png',import.meta.url).pathname,timeout:120000});
    await Import(Fixture);await Page.click('#MaskTab');await Page.screenshot({path:new URL('./SeaLevelMaskStack.png',import.meta.url).pathname});
    Assert.deepEqual(Errors,[]);await writeFile(new URL('./RadialStackBrowserResults.json',import.meta.url),JSON.stringify({URL:process.env.LANDSCAPE_URL||'http://localhost:5174',Results,Errors},null,2));
}
finally{await Browser.close();}
