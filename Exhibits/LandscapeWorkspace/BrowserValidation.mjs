/*==========================================================================================================================================
                                                        BROWSERVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Browser interaction checks and retained captures for the landscape prototype.
import { createRequire } from 'node:module';
import Assert from 'node:assert/strict';
import { writeFile, readFile } from 'node:fs/promises';
import { gunzipSync } from 'node:zlib';
const Require = createRequire(new URL('../../Experimental/LandscapeWorkspace/package.json', import.meta.url));
const { chromium: BrowserType } = Require('playwright');
const Chromium = Require('@sparticuz/chromium').default;
const Browser = await BrowserType.launch({ executablePath:await Chromium.executablePath(), args:[...Chromium.args,'--enable-webgl','--ignore-gpu-blocklist'], headless:true });
const Page = await Browser.newPage({viewport:{width:1440,height:1000},deviceScaleFactor:1});
const Errors=[];
Page.on('pageerror',Failure=>Errors.push(Failure.message));
const Results=[];
async function Check(Name, Evaluation)
{
    await Evaluation();Results.push({Check:Name,Result:'Passed'});console.log('Passed:',Name);
}
try
{
    await Page.goto(process.env.LANDSCAPE_URL || 'http://localhost:5173');
    await Page.waitForSelector('#tp-layer-list .tp-layer-row');await Page.click('#ToggleMaskScopes');await Page.click('#MaskTab');
    await Check('Three example vector layers',async()=>Assert.equal(await Page.locator('.layer').count(),3));
    await Page.screenshot({path:new URL('./MaskCanvas.png',import.meta.url).pathname});
    await Check('Paint stroke and undo / redo',async()=>
    {
        const Before=await Page.locator('#StrokeCount').textContent();
        await Page.mouse.move(700,500);await Page.mouse.down();await Page.mouse.move(780,550,{steps:8});await Page.mouse.up();
        Assert.notEqual(await Page.locator('#StrokeCount').textContent(),Before);
        await Page.click('#Undo');Assert.equal(await Page.locator('#StrokeCount').textContent(),Before);
        await Page.click('#Redo');Assert.notEqual(await Page.locator('#StrokeCount').textContent(),Before);
    });
    await Check('Box select and attach region',async()=>
    {
        await Page.click('[data-tool="select"]');await Page.mouse.move(620,400);await Page.mouse.down();await Page.mouse.move(830,650,{steps:5});await Page.mouse.up();
        await Page.click('#KeepRegion');Assert.equal(await Page.locator('#RegionList .region').count(),2);
    });
    await Check('Zoom and pan canvas',async()=>
    {
        const Before=await Page.locator('#Zoom').textContent();await Page.mouse.move(700,500);await Page.mouse.wheel(0,-200);await Page.waitForTimeout(150);
        Assert.notEqual(await Page.locator('#Zoom').textContent(),Before);
        await Page.click('[data-tool="pan"]');await Page.mouse.move(700,500);await Page.mouse.down();await Page.mouse.move(740,520);await Page.mouse.up();await Page.click('#Fit');
    });
    await Check('Build WebGL2 volumetric landscape',async()=>
    {
        await Page.click('#Build');await Page.waitForTimeout(1800);
        Assert.equal(await Page.locator('#Volume').isVisible(),true);
        Assert.match(await Page.locator('#Status').textContent(),/Volumetric terrain/);
        const Pixels=await Page.evaluate(()=>
        {
            const Canvas=document.querySelector('#Volume'),GL=Canvas.getContext('webgl2'),Bytes=new Uint8Array(4*20);
            const Samples=[];
            for(let Index=0;Index<20;Index++){GL.readPixels(Math.floor(Canvas.width*(Index+1)/21),Math.floor(Canvas.height*.5),1,1,GL.RGBA,GL.UNSIGNED_BYTE,Bytes);Samples.push([...Bytes.slice(0,4)]);}
            return {Error:GL.getError(),Samples};
        });
        Assert.equal(Pixels.Error,0);Assert.ok(new Set(Pixels.Samples.map(Pixel=>Pixel.join(','))).size>4);
    });
    await Check('GPU field signs: interior, below solid, outside region',async()=>
    {
        const Signs=await Page.evaluate(()=>
        {
            const GL=document.querySelector('#Volume').getContext('webgl2'), Program=GL.getParameter(GL.CURRENT_PROGRAM);
            GL.uniform1i(GL.getUniformLocation(Program,'Probe'),1);
            const Signs=[];
            for(const Point of [[-250,0,0],[-250,-200,0],[900,0,900]])
            {
                GL.uniform3fv(GL.getUniformLocation(Program,'ProbePoint'),Point);GL.drawArrays(GL.TRIANGLES,0,3);
                const Pixel=new Uint8Array(4);GL.readPixels(0,0,1,1,GL.RGBA,GL.UNSIGNED_BYTE,Pixel);Signs.push(Pixel[0]>128?'inside':'outside');
            }
            GL.uniform1i(GL.getUniformLocation(Program,'Probe'),0);return Signs;
        });
        Assert.deepEqual(Signs,['inside','outside','outside']);
    });
    await Page.waitForTimeout(1200);
    await Page.screenshot({path:new URL('./Landscape.png',import.meta.url).pathname});
    await Check('Orbit, cave toggle, ocean toggle and waves',async()=>
    {
        await Page.mouse.move(700,450);await Page.mouse.down();await Page.mouse.move(780,490,{steps:4});await Page.mouse.up();
        await Page.check('#Caves');await Page.uncheck('#Ocean');await Page.waitForTimeout(300);await Page.check('#Ocean');
        await Page.locator('#Wave').fill('4');await Page.locator('#Wave').dispatchEvent('input');Assert.equal(await Page.locator('#WaveRead').textContent(),'4 m');
    });
    await Check('Vector project export and import',async()=>
    {
        const [Download]=await Promise.all([Page.waitForEvent('download'),Page.click('#Export')]);
        const Filename=await Download.path();await Page.locator('#File').setInputFiles(Filename);await Page.waitForTimeout(150);
        Assert.match(await Page.locator('#Status').textContent(),/Vector document opened/);
    });
    await Check('Sparse signed-distance voxel export',async()=>
    {
        const [Download]=await Promise.all([Page.waitForEvent('download'),Page.click('#PreviewBake')]);
        Assert.match(Download.suggestedFilename(),/sdf-preview/);Assert.match(await Page.locator('#Status').textContent(),/occupied SDF bricks/);
    });
    await Check('Empty world has no authored volume; undo restores',async()=>
    {
        Page.once('dialog',Dialog=>Dialog.accept());await Page.click('#Empty');Assert.equal(await Page.locator('#StrokeCount').textContent(),'0 / 512 capsule samples');
        Assert.equal(await Page.locator('#RegionList .region').count(),0);await Page.click('#Undo');Assert.equal(await Page.locator('.layer').count(),3);
    });
    await Page.click('#MaskTab');
    const Fixture=JSON.parse(await readFile(new URL('./BakeResults.json',import.meta.url),'utf8')).Fixture;
    await Page.locator('#File').setInputFiles({name:'BakeFixture.json',mimeType:'application/json',buffer:Buffer.from(JSON.stringify({Format:'FrontierLandscape/1',Layers:[Fixture]}))});
    await Check('Background 4096 bake cancellation keeps UI responsive',async()=>
    {
        await Page.selectOption('#Resolution','4096');await Page.click('#Bake');
        await Page.waitForFunction(()=>document.querySelector('#BakeMeter').value>0);
        await Page.locator('#BakeRead').scrollIntoViewIfNeeded();
        await Page.screenshot({path:new URL('./BakeProgress.png',import.meta.url).pathname});
        await Page.click('#CancelBake');
        Assert.match(await Page.locator('#Status').textContent(),/cancelled/);
        Assert.equal(await Page.locator('#BakeProgress').isVisible(),false);
        Assert.equal(await Page.locator('#Bake').isEnabled(),true);
    });
    await Check('Full 1024 × 128 × 1024 browser bake downloads complete gzip volume',async()=>
    {
        await Page.selectOption('#Resolution','1024');
        const DownloadPromise=Page.waitForEvent('download',{timeout:120000});
        await Page.click('#Bake');
        await Page.click('#AddMaskLayer');
        Assert.equal(await Page.locator('.layer').count(),2);
        const Download=await DownloadPromise;
        Assert.equal(Download.suggestedFilename(),'Landscape.sdf.gz');
        const Bytes=gunzipSync(await readFile(await Download.path()));
        const Newline=Bytes.indexOf(10), Specification=JSON.parse(Bytes.subarray(0,Newline).toString());
        Assert.deepEqual(Specification.Dimensions,[1024,128,1024]);
        Assert.equal(Specification.Layer.Name,Fixture.Name);
        let Offset=Newline+1,Covered=0,Sampled=0;
        while(Offset<Bytes.length)
        {
            const Category=Bytes.readUInt32LE(Offset),Width=Bytes.readUInt32LE(Offset+16);
            Assert.ok(Category===1||Category===2);Covered+=Width**3;
            Offset+=20+(Category===1?4:Width**3*4);if(Category===2)Sampled++;
        }
        Assert.equal(Offset,Bytes.length);Assert.equal(Covered,1024*128*1024);Assert.ok(Sampled>0);
        Assert.match(await Page.locator('#Status').textContent(),/1024 × 128 × 1024 cells/);
        await Page.locator('.layer').first().click();
        await Page.screenshot({path:new URL('./BakeComplete.png',import.meta.url).pathname});
    });
    Assert.deepEqual(Errors,[]);
    await writeFile(new URL('./BrowserResults.json',import.meta.url),JSON.stringify({URL:process.env.LANDSCAPE_URL || 'http://localhost:5173',Results,Errors},null,2));
}
finally{await Browser.close();}
