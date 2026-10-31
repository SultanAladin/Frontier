/*============================================================================================================================================
                                                    GEOMETRYDETAILVALIDATION.CJS
============================================================================================================================================*/
// 🧩 Browser checks for editor switching, simulated polygon detail and camera damping.
'use strict';
const assert = require('node:assert/strict');
const path = require('node:path');
const fs = require('node:fs');
const {chromium} = require('playwright-core');
const Chromium = require('@sparticuz/chromium').default;
const Output = path.join(__dirname, 'Images');
const Url = process.env.ATMOSPHERE_URL || 'http://127.0.0.1:4173/';

async function Slider(Page, Id, Amount)
{
    await Page.locator('#' + Id).evaluate((Input, Amount) => {
        Input.value = String(Amount);
        Input.dispatchEvent(new Event('input', {bubbles: true}));
    }, Amount);
}
async function GeometrySample(Page)
{
    await Page.waitForTimeout(200);
    return Page.evaluate(() => {
        const Pixels = new Uint8Array(GeometryCanvas.width * GeometryCanvas.height * 4);
        GeometryGl.readPixels(0,0,GeometryCanvas.width,GeometryCanvas.height,GeometryGl.RGBA,GeometryGl.UNSIGNED_BYTE,Pixels);
        const Samples=[];
        for (let Y=1;Y<50;++Y) for(let X=1;X<50;++X) {
            const Index=(Math.floor(Y*GeometryCanvas.height/50)*GeometryCanvas.width+Math.floor(X*GeometryCanvas.width/50))*4;
            Samples.push(Pixels[Index],Pixels[Index+1],Pixels[Index+2],Pixels[Index+3]);
        }
        return {Samples, Triangles:GeometryTriangleCount, Max:GeometryMaximumTriangles, Error:GeometryGl.getError()};
    });
}
function Difference(A,B) { return A.Samples.reduce((Sum,Channel,Index)=>Sum+Math.abs(Channel-B.Samples[Index]),0); }
(async()=>{
    const Browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||await Chromium.executablePath(),
        args:[...Chromium.args.filter(Arg=>!['--single-process','--in-process-gpu'].includes(Arg)),
              '--enable-unsafe-swiftshader','--use-gl=angle','--use-angle=swiftshader']});
    const Page=await Browser.newPage({viewport:{width:1440,height:900},deviceScaleFactor:1});
    const Errors=[];Page.on('pageerror',Error=>Errors.push(Error.message));
    try {
        const Response=await Page.goto(Url,{waitUntil:'networkidle',timeout:60000});
        assert.match(Response.headers()['cache-control'],/no-store/,'server should not cache stale UI');
        await Page.locator('#AtmosphereCanvas[data-rendered="true"]').waitFor({timeout:60000});
        assert.equal(await Page.locator('.atmosphere-card:visible').count(),1);
        await Page.locator('#PlanetPreset').selectOption('Mars');
        await Page.getByRole('button',{name:'Next editor: Geometry detail'}).click();
        await Page.locator('#GeometryCanvas[data-rendered="true"]').waitFor({timeout:60000});
        assert.equal(await Page.locator('#AtmosphereCard').isHidden(),true);
        assert.equal(await Page.locator('#GeometryCard').isVisible(),true);
        assert.equal(await Page.locator('#CurrentEditorName').textContent(),'Geometry detail');
        assert.ok((await Page.locator('#GeometryCurve').getAttribute('d')).length>50,'quality infographic missing');
        const Dock=await Page.locator('#GeometryCard').boundingBox(), View=await Page.locator('#GeometryViewport').boundingBox();
        assert.ok(Math.abs(Dock.x+Dock.width-1440)<1 && Math.abs(Dock.y)<1 && Math.abs(Dock.height-900)<1 && Math.abs(View.width-Dock.x)<1,'geometry dock is not split');
        const Default=await GeometrySample(Page);
        assert.equal(Default.Error,0,'geometry WebGL error');
        assert.ok(Default.Triangles>1000 && Default.Samples.some(V=>V>100),'geometry preview is blank');
        await Page.screenshot({path:path.join(Output,'geometry-shaded.png')});
        const DistanceInput=Page.locator('#GeometryDistance');
        const DistanceGraphic=DistanceInput.locator('..');
        const BeforeMarker=await DistanceGraphic.locator('.control-marker').getAttribute('cx');
        const BeforeCurve=await Page.locator('#GeometryNear').locator('..').locator('.control-line').getAttribute('d');
        await DistanceInput.scrollIntoViewIfNeeded();
        const InputBounds=await DistanceInput.boundingBox();
        assert.equal(await Page.getByRole('slider',{name:/Preview distance/}).count(),1,'chart input is missing from the accessibility tree');
        await Page.mouse.move(InputBounds.x+InputBounds.width*.2,InputBounds.y+InputBounds.height/2);
        await Page.mouse.down();await Page.mouse.move(InputBounds.x+InputBounds.width*.9,InputBounds.y+InputBounds.height/2,{steps:5});await Page.mouse.up();
        assert.ok(Number(await DistanceInput.inputValue())>120,'chart pointer input did not update distance');
        assert.notEqual(await DistanceGraphic.locator('.control-marker').getAttribute('cx'),BeforeMarker,'chart marker did not move with pointer');
        assert.notEqual(await Page.locator('#GeometryNear').locator('..').locator('.control-line').getAttribute('d'),BeforeCurve,'quality response charts did not follow distance');
        await DistanceInput.focus();await Page.keyboard.press('Home');await Page.keyboard.press('ArrowRight');
        assert.equal(await DistanceInput.inputValue(),'3','chart did not retain keyboard control');
        await Page.locator('#ResetGeometry').click();
        assert.equal(await DistanceInput.inputValue(),'35','chart reset failed');
        await Page.waitForFunction(()=>Math.abs(Number(document.querySelector('#GeometryDistance').parentNode.querySelector('.control-marker').getAttribute('cx')) - (5+150*(35-2)/(150-2))) < .1);
        await GeometrySample(Page);
        await Page.getByRole('button',{name:'Topology'}).click();
        const Topology=await GeometrySample(Page);
        assert.ok(Difference(Default,Topology)>1000,'topology overlay did not change pixels');
        assert.equal(Topology.Triangles,Default.Triangles,'topology overlay changed geometry');
        await Page.screenshot({path:path.join(Output,'geometry-topology.png')});
        await Slider(Page,'GeometryDistance',5);
        await Page.waitForFunction(()=>Math.abs(GeometryFocusDistance-GeometryParameters.GeometryDistance)<.05,null,{timeout:15000});
        const Near=await GeometrySample(Page);
        assert.equal(Near.Triangles,Near.Max,'near distance does not draw full detail');
        const DistanceLag=await Page.evaluate(()=>{
            const Input=document.getElementById('GeometryDistance');Input.value='140';Input.dispatchEvent(new Event('input',{bubbles:true}));
            return {Target:GeometryParameters.GeometryDistance,Current:GeometryFocusDistance};
        });
        assert.ok(DistanceLag.Target-DistanceLag.Current>100,'distance camera did not lag');
        await Page.waitForFunction(()=>Math.abs(GeometryFocusDistance-GeometryParameters.GeometryDistance)<.05,null,{timeout:15000});
        const Far=await GeometrySample(Page);
        assert.ok(Far.Triangles<Near.Triangles*.16 && Far.Triangles>0,'far triangles did not match the reduction');
        assert.equal(Number((await Page.locator('#GeometryTriangles').textContent()).replaceAll(',','')),Far.Triangles,'triangle readout is not the drawn count');
        assert.ok(Difference(Near,Far)>1000,'distance and geometry did not affect the image');
        await Page.screenshot({path:path.join(Output,'geometry-far.png')});
        await Slider(Page,'GeometryRetention',50);
        const Retained=await GeometrySample(Page);
        assert.ok(Retained.Triangles>Far.Triangles*2,'far retention did not increase drawn triangles');
        const BeforeMaterial=Retained.Triangles;
        await Page.getByRole('button',{name:'Shaded'}).click();
        await Slider(Page,'GeometryMaterial',0);const MaterialLow=await GeometrySample(Page);
        await Slider(Page,'GeometryMaterial',100);const MaterialHigh=await GeometrySample(Page);
        assert.ok(Difference(MaterialLow,MaterialHigh)>50,'material detail did not affect preview shading');
        assert.equal(MaterialHigh.Triangles,BeforeMaterial,'material detail changed triangle count');
        await Slider(Page,'GeometryNear',100);await Slider(Page,'GeometryFar',3);
        assert.ok(Number(await Page.locator('#GeometryNear').inputValue())<Number(await Page.locator('#GeometryFar').inputValue()),'distance constraints are invalid');
        await Page.locator('#ResetGeometry').click();await GeometrySample(Page);
        assert.equal(await Page.locator('#GeometryDistance').inputValue(),'35','reset did not restore preview distance');
        assert.equal(await Page.locator('#GeometryRetention').inputValue(),'12','reset did not restore retention');
        assert.equal(await Page.locator('#GeometryMaterial').inputValue(),'80','reset did not restore material detail');
        const Canvas=Page.locator('#GeometryCanvas');await Canvas.hover({position:{x:700,y:400}});await Page.mouse.down();
        const OrbitLag=await Page.evaluate(()=>{
            const R=GeometryCanvas.getBoundingClientRect();GeometryCanvas.dispatchEvent(new PointerEvent('pointermove',{clientX:R.left+820,clientY:R.top+440,pointerId:1,bubbles:true}));
            return {Target:GeometryAim.Yaw,Current:GeometryParameters.Yaw};
        });
        assert.ok(Math.abs(OrbitLag.Target-OrbitLag.Current)>.2,'orbit camera did not lag');await Page.mouse.up();
        await Page.waitForFunction(()=>Math.abs(GeometryAim.Yaw-GeometryParameters.Yaw)<.001,null,{timeout:15000});
        await Slider(Page,'GeometryRetention',67);
        await Page.getByRole('button',{name:'Previous editor: Atmosphere'}).click();
        assert.equal(await Page.locator('#PlanetPreset').inputValue(),'Mars','switching lost atmosphere settings');
        await Page.getByRole('button',{name:'Next editor: Geometry detail'}).click();
        assert.equal(await Page.locator('#GeometryCard').isVisible(),true,'switching back to geometry failed');
        assert.equal(await Page.locator('#GeometryRetention').inputValue(),'67','switching lost geometry settings');
        await Page.locator('#ResetGeometry').click();
        await Page.setViewportSize({width:758,height:548});await Page.waitForTimeout(300);
        const CompactDock=await Page.locator('#GeometryCard').boundingBox(),CompactView=await Page.locator('#GeometryViewport').boundingBox();
        assert.ok(Math.abs(CompactDock.x+CompactDock.width-758)<1 && Math.abs(CompactDock.y)<1 && Math.abs(CompactDock.height-548)<1 && Math.abs(CompactView.width-CompactDock.x)<1,'compact preview did not remain split');
        assert.equal(await Page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth && document.documentElement.scrollHeight<=innerHeight),true,'compact shell overflows');
        await Page.screenshot({path:path.join(Output,'geometry-compact.png')});
        await Page.getByRole('button',{name:'Previous editor: Atmosphere'}).click();await Page.waitForTimeout(300);
        const CompactAtmos=await Page.locator('#AtmosphereCard').boundingBox();assert.ok(Math.abs(CompactAtmos.x+CompactAtmos.width-758)<1 && CompactAtmos.y===0 && Math.abs(CompactAtmos.height-548)<1,'atmosphere compact dock not split');
        assert.ok((await Page.locator('#DensityCurve').getAttribute('d')).length>50,'atmosphere infographic missing');
        await Page.screenshot({path:path.join(Output,'atmosphere-compact.png')});
        await Page.setViewportSize({width:390,height:844});await Page.waitForTimeout(300);
        const MobileDock=await Page.locator('#AtmosphereCard').boundingBox(),MobileView=await Page.locator('#AtmosphereViewport').boundingBox();
        assert.ok(Math.abs(MobileDock.x+MobileDock.width-390)<1 && MobileDock.y===0 && Math.abs(MobileDock.height-844)<1 && Math.abs(MobileView.width-MobileDock.x)<1,'mobile dock not split');
        assert.equal(await Page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth && document.documentElement.scrollHeight<=innerHeight),true,'mobile shell overflows');
        await Page.getByRole('button',{name:'Next editor: Geometry detail'}).click();await Page.waitForTimeout(200);
        assert.equal(await Page.locator('#GeometryCard').isVisible(),true,'mobile editor navigation failed');
        await Page.screenshot({path:path.join(Output,'geometry-mobile.png')});
        const MobileControl=Page.locator('#GeometryDistance');await MobileControl.scrollIntoViewIfNeeded();
        const MobileBounds=await MobileControl.boundingBox();
        await Page.mouse.click(MobileBounds.x+MobileBounds.width*.8,MobileBounds.y+MobileBounds.height/2);
        assert.ok(Number(await MobileControl.inputValue())>100,'mobile-size chart input failed');
        assert.deepEqual(Errors,[],'uncaught browser exceptions');
        const Results={checks:'passed',renderer:'Chromium WebGL2 / SwiftShader',nearTriangles:Near.Triangles,farTriangles:Far.Triangles,fullTriangles:Near.Max,
            higherRetentionTriangles:Retained.Triangles,materialPixelDifference:Difference(MaterialLow,MaterialHigh),topologyPixelDifference:Difference(Default,Topology),
            distanceLag:DistanceLag,orbitLag:OrbitLag,compact:{width:758,height:548,dockX:CompactDock.x,dockWidth:CompactDock.width},browserErrors:Errors};
        fs.writeFileSync(path.join(__dirname,'Results.json'),JSON.stringify(Results,null,2)+'\n');console.log(JSON.stringify(Results,null,2));
    } finally {await Browser.close();}
})().catch(Error=>{console.error(Error);process.exitCode=1;});
