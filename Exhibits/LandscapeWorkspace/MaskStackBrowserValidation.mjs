/*==========================================================================================================================================
                                                       MASKSTACKBROWSERVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Active mask-layer ownership, preset gallery, independent batch stacks, and actual signed-volume validation.

import { createRequire } from 'node:module';
import Assert from 'node:assert/strict';
import { readFile,writeFile } from 'node:fs/promises';
import { gunzipSync } from 'node:zlib';
import { VolumeDistance } from '../../Experimental/LandscapeWorkspace/DistanceField.js';
import { CreateLandform } from '../../Experimental/LandscapeWorkspace/LandformStack.js';
import { CreatePresetOperations } from '../../Experimental/LandscapeWorkspace/LandformPresets.js';
const Require=createRequire(new URL('../../Experimental/LandscapeWorkspace/package.json',import.meta.url));
const {chromium:BrowserType}=Require('playwright'),Chromium=Require('@sparticuz/chromium').default;
const Browser=await BrowserType.launch({executablePath:await Chromium.executablePath(),args:[...Chromium.args,'--enable-webgl','--ignore-gpu-blocklist'],headless:true});
const Page=await Browser.newPage({viewport:{width:1600,height:1100},deviceScaleFactor:1});
const Errors=[],Results=[];Page.on('pageerror',Failure=>Errors.push(Failure.message));
const Layer=(Name,X)=>({Name,Color:'#b4a17d',Visible:true,Depth:100,Caves:false,Resolution:256,Vertical:64,Landforms:[],Strokes:[{Points:[[X,0]],Radius:175,Erase:false}],Regions:[{X0:X-210,Z0:-210,X1:X+210,Z1:210,Edge:15}]});
const Fixture={Format:'FrontierLandscape/1',Layers:[Layer('Desert coast',-450),Layer('Rocky islands',0),Layer('Ice region',450)]};
Fixture.Layers[0].Strokes.push({Points:[[-450,600]],Radius:150,Erase:false});Fixture.Layers[0].Regions.push({X0:-650,Z0:400,X1:-250,Z1:800,Edge:25});
async function Verify(Name,Evaluation){await Evaluation();Results.push({Check:Name,Result:'Passed'});console.log('Passed:',Name);}
async function Import(Document)
{
    await Page.locator('#File').setInputFiles({name:'MaskStacks.json',mimeType:'application/json',buffer:Buffer.from(JSON.stringify(Document))});
    await Page.waitForTimeout(100);
    if(!await Page.locator('#Layers').isVisible())await Page.click('#ToggleMaskScopes');
    if(await Page.locator('.stack-secondary-controls').getAttribute('open')===null)await Page.locator('.stack-secondary-controls > summary').click();
}
async function Export()
{
    const [Download]=await Promise.all([Page.waitForEvent('download'),Page.click('#Export')]);return JSON.parse(await readFile(await Download.path(),'utf8'));
}
async function Configure(Selector,Setting){await Page.locator(Selector).fill(String(Setting));await Page.locator(Selector).dispatchEvent('change');}
async function Probe(Points)
{
    return Page.evaluate(Points=>
    {
        const GL=document.querySelector('#Volume').getContext('webgl2'),Program=GL.getParameter(GL.CURRENT_PROGRAM);GL.disable(GL.DITHER);const Viewport=GL.getParameter(GL.VIEWPORT);GL.viewport(0,0,1,1);
        GL.uniform1i(GL.getUniformLocation(Program,'Probe'),1);GL.uniform1i(GL.getUniformLocation(Program,'ProbeDistance'),1);
        const Distances=[];
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
    await Page.goto(process.env.LANDSCAPE_URL||'http://localhost:5173');await Page.waitForSelector('#tp-layer-list .tp-layer-row');await Page.click('#LandscapeTab');
    await Verify('Six preset examples load actual rendered thumbnails in the production gallery',async()=>
    {
        await Page.click('#TogglePresetLibrary');Assert.equal(await Page.locator('.preset-card').count(),6);
        await Page.waitForFunction(()=>[...document.querySelectorAll('.preset-card img')].every(Image=>Image.complete&&Image.naturalWidth>100));
        await Page.locator('[data-preset="coastal-arches"]').dblclick();Assert.equal(await Page.locator('#PresetDialog').isVisible(),true);
        Assert.equal(await Page.locator('#PresetDialogTitle').textContent(),'Coastal arches');await Page.click('#ClosePresetDialog');
    });
    await Import(Fixture);
    await Verify('Add landforms to the active mask stack, not a boundary or another mask',async()=>
    {
        await Page.selectOption('#LandformType','mesa');await Page.click('#AddLandform');let Document=await Export();
        Assert.equal(Document.Layers[0].Landforms[0].Type,'mesa');Assert.equal(Document.Layers[0].Regions[0].Landforms,undefined);Assert.equal(Document.Layers[0].Regions[1].Landforms,undefined);Assert.equal(Document.Layers[1].Landforms.length,0);
        Assert.equal(await Page.locator('[data-operation-index]').count(),1);
        await Page.selectOption('#StackRegion','1');await Configure('[data-property="Amount"]',120);
        Document=await Export();Assert.equal(Document.Layers[0].Landforms[0].Amount,120);Assert.equal(await Page.locator('[data-operation-index]').count(),1);
    });
    await Verify('Switching masks restores each mask’s own stack and selected operation',async()=>
    {
        await Page.locator('.layer').nth(1).click();Assert.equal(await Page.locator('[data-operation-index]').count(),0);
        await Page.selectOption('#LandformType','dunes');await Page.click('#AddLandform');await Page.locator('.layer').first().click();
        Assert.equal(await Page.locator('#tp-layer-list .tp-layer-name').first().textContent(),'Mesa / plateau');Assert.equal(await Page.locator('[data-property="Amount"]').inputValue(),'120');
    });
    await Verify('Layer toggles, parameters, reorder, deletion, undo and redo change the persisted mask stack',async()=>
    {
        await Page.selectOption('#LandformType','tunnel');await Page.click('#AddLandform');await Page.locator('[data-operation-index]').first().getByTitle('Mute layer',{exact:true}).click();
        let Document=await Export();Assert.equal(Document.Layers[0].Landforms[1].Enabled,false);
        await Page.locator('[data-operation-index]').first().getByTitle('Move earlier in pipeline (down)').click();Document=await Export();Assert.equal(Document.Layers[0].Landforms[0].Type,'tunnel');
        await Page.getByTitle('Delete layer',{exact:true}).click();Assert.equal(await Page.locator('[data-operation-index]').count(),1);
        await Page.click('#HistoryUndo');Assert.equal(await Page.locator('[data-operation-index]').count(),2);await Page.click('#HistoryRedo');Assert.equal(await Page.locator('[data-operation-index]').count(),1);
    });
    await Verify('Applying a preset targets the active mask only, even with several masks selected',async()=>
    {
        await Page.getByLabel('Select Ice region for batch',{exact:true}).check();const Before=await Export();
        await Page.click('[data-preset="desert-mesas"]');await Page.click('#ApplyPreset');const After=await Export();
        Assert.deepEqual(After.Layers[0].Landforms,CreatePresetOperations('desert-mesas'));Assert.deepEqual(After.Layers[0].Strokes,Before.Layers[0].Strokes);Assert.deepEqual(After.Layers[0].Regions,Before.Layers[0].Regions);Assert.deepEqual(After.Layers[0].Color,Before.Layers[0].Color);
        Assert.deepEqual(After.Layers[1],Before.Layers[1]);Assert.deepEqual(After.Layers[2],Before.Layers[2]);
    });
    await Verify('Explicit multi-mask presets create independent stacks and append does not replace existing layers',async()=>
    {
        await Page.click('[data-preset="alpine-ridges"]');await Page.click('#BatchPreset');let Document=await Export();
        Assert.deepEqual(Document.Layers[0].Landforms,CreatePresetOperations('alpine-ridges'));Assert.deepEqual(Document.Layers[2].Landforms,CreatePresetOperations('alpine-ridges'));Assert.equal(Document.Layers[1].Landforms[0].Type,'dunes');
        await Configure('[data-property="Amount"]',220);Document=await Export();Assert.notEqual(Document.Layers[2].Landforms.at(-1).Amount,220);
        await Page.click('[data-preset="coastal-arches"]');await Page.click('#AppendPreset');Document=await Export();Assert.equal(Document.Layers[0].Landforms.length,4);Assert.equal(Document.Layers[2].Landforms.length,2);
    });
    await Verify('Boundary edge settings do not change mask-stack ownership',async()=>
    {
        const Before=await Export();await Page.selectOption('#StackRegion','1');await Configure('#EdgeWidth',45);const After=await Export();
        Assert.equal(After.Layers[0].Regions[1].Edge,45);Assert.equal(After.Layers[0].Regions[0].Edge,15);Assert.deepEqual(After.Layers[0].Landforms,Before.Layers[0].Landforms);
    });
    await Verify('Mask stacks round-trip and autosave without flattening into per-boundary copies',async()=>
    {
        const Document=await Export();await Import(Document);Assert.deepEqual(await Export(),Document);await Page.waitForTimeout(400);await Page.reload();await Page.waitForSelector('#tp-layer-list .tp-layer-row');Assert.deepEqual(await Export(),Document);
    });
    await Verify('Mask stacks can be authored before regions exist',async()=>
    {
        const Empty=Layer('Empty mask',0);Empty.Regions=[];Empty.Strokes=[];await Import({Format:'FrontierLandscape/1',Layers:[Empty]});await Page.selectOption('#LandformType','arch');await Page.click('#AddLandform');
        const Document=await Export();Assert.equal(Document.Layers[0].Landforms[0].Type,'arch');Assert.equal(Document.Layers[0].Regions.length,0);
    });
    await Verify('Legacy boundary modifiers remain intact and are explicitly removable without losing the mask stack',async()=>
    {
        const Legacy=Layer('Legacy',0);Legacy.Landforms=[CreateLandform('mesa')];Legacy.Regions[0].Landforms=[CreateLandform('tunnel')];await Import({Format:'FrontierLandscape/1',Layers:[Legacy]});
        Assert.equal((await Export()).Layers[0].Regions[0].Landforms[0].Type,'tunnel');Assert.match(await Page.locator('#LegacyBoundaryOps').textContent(),/Imported Region/);
        await Page.getByText('Clear imported boundary modifiers',{exact:true}).click();const Document=await Export();Assert.equal(Document.Layers[0].Regions[0].Landforms,undefined);Assert.equal(Document.Layers[0].Landforms[0].Type,'mesa');
    });
    await Verify('Over-limit batch additions fail atomically on all mask stacks',async()=>
    {
        const Guard=structuredClone(Fixture);Guard.Layers[0].Landforms=Array.from({length:8},()=>CreateLandform('mesa'));await Import(Guard);
        await Page.getByLabel('Select Ice region for batch',{exact:true}).check();await Page.click('#BatchLandform');Assert.deepEqual(await Export(),Guard);Assert.match(await Page.locator('#Status').textContent(),/eight/);
    });
    await Verify('Mask-owned landforms agree between GPU preview and CPU field evaluation',async()=>
    {
        let MaximumError=0,Count=0;
        for(const Type of ['ridges','mesa','fault','dunes','craters','stacks','strata','tunnel','arch'])
        {
            const Document={Format:'FrontierLandscape/1',Layers:[Layer(Type,0)]};Document.Layers[0].Landforms=[{...CreateLandform(Type),Amount:120,Scale:1}];
            await Import(Document);await Page.click('#Build');const Points=Array.from({length:24},(_,Index)=>[Math.sin(Index*1.3)*210,Math.cos(Index*.9)*170,Math.sin(Index*.7)*210]);const Observed=await Probe(Points);
            Points.forEach((Point,Index)=>{const Error=Math.abs(VolumeDistance(Document.Layers[0],...Point)-Observed[Index]);MaximumError=Math.max(MaximumError,Error);Assert.ok(Error<.003,`${Type}: ${Error}`);Count++;});
        }
        Results.push({Measurement:'Mask-stack CPU/GPU agreement',Samples:Count,MaximumAbsoluteError:MaximumError});
    });
    await Verify('Background export contains the parent mask-layer stack and untouched boundary geometry',async()=>
    {
        const Document={Format:'FrontierLandscape/1',Layers:[Layer('Arch bake',0)]};Document.Layers[0].Landforms=CreatePresetOperations('coastal-arches');await Import(Document);
        const [Download]=await Promise.all([Page.waitForEvent('download',{timeout:90000}),Page.click('#Bake')]);const Bytes=gunzipSync(await readFile(await Download.path())),Header=JSON.parse(Bytes.subarray(0,Bytes.indexOf(10)).toString());
        Assert.equal(Header.Layer.Landforms[0].Type,'arch');Assert.equal(Header.Region.Landforms,undefined);Assert.equal(Header.Region.Edge,15);
    });
    await Verify('Six-mask example world loads independent visible presets and can be undone',async()=>
    {
        await Page.click('#LandscapeTab');const Before=await Export();Page.once('dialog',Dialog=>Dialog.accept());await Page.click('#LoadPresetWorld');Assert.equal(await Page.locator('.layer').count(),6);
        const Document=await Export();Assert.equal(Document.Layers[0].Landforms.length,3);Assert.equal(Document.Layers[5].Landforms[0].Type,'arch');
        await Page.click('#HistoryUndo');Assert.deepEqual(await Export(),Before);Page.once('dialog',Dialog=>Dialog.accept());await Page.click('#LoadPresetWorld');
    });
    await Verify('Focus active mask renders the selected extent and preserves a usable canvas zoom',async()=>
    {
        await Page.locator('.layer').first().click();await Page.selectOption('#RenderQuality','360');await Page.click('#FocusMask');
        await Page.waitForFunction(()=>document.querySelector('#Volume').dataset.completedOrbit===String(420*1.2),{},{timeout:60000});
        Assert.ok(parseInt(await Page.locator('#Zoom').textContent(),10)>5);
        await Page.evaluate(()=>document.querySelector('.inspector').scrollTop=0);
    });
    if(!await Page.locator('#PresetCards').isVisible())await Page.click('#TogglePresetLibrary');
    await Page.screenshot({path:new URL('./MaskLayerStacks.png',import.meta.url).pathname,timeout:90000});
    await Page.locator('[data-preset="coastal-arches"]').dblclick();await Page.screenshot({path:new URL('./PresetLibrary.png',import.meta.url).pathname,timeout:90000});
    Assert.deepEqual(Errors,[]);await writeFile(new URL('./MaskStackBrowserResults.json',import.meta.url),JSON.stringify({URL:process.env.LANDSCAPE_URL||'http://localhost:5173',Results,Errors},null,2));
}
finally{await Browser.close();}
