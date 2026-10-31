/*============================================================================================================================================
                                                    VISUALCONTROLSVALIDATION.CJS
============================================================================================================================================*/
// 🧩 Verify every distinct infographic is interactive, accessible and restored on reset.
'use strict';
const assert = require('node:assert/strict');
const path = require('node:path');
const fs = require('node:fs');
const {chromium} = require('playwright-core');
const Chromium = require('@sparticuz/chromium').default;
const Url = process.env.ATMOSPHERE_URL || 'http://127.0.0.1:4173/';
const Groups = [
    {Name:'atmosphere', Inputs:['SunElevation','Rayleigh','Mie','Ozone','Exposure','CloudCoverage'], Reset:'ResetAtmosphere'},
    {Name:'geometry', Inputs:['GeometryDistance','GeometryNear','GeometryFar','GeometryRetention','GeometryCurvePower','GeometryMaterial'], Reset:'ResetGeometry'}
];
(async()=>{
    const Browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||await Chromium.executablePath(),
        args:[...Chromium.args.filter(Arg=>!['--single-process','--in-process-gpu'].includes(Arg)),
            '--enable-unsafe-swiftshader','--use-gl=angle','--use-angle=swiftshader']});
    const Page=await Browser.newPage({viewport:{width:1440,height:1250},deviceScaleFactor:1});
    const Errors=[];Page.on('pageerror',Error=>Errors.push(Error.message));
    try {
        await Page.goto(Url,{waitUntil:'networkidle',timeout:60000});
        await Page.locator('#AtmosphereCanvas[data-rendered="true"]').waitFor({timeout:60000});
        const Visuals=await Page.locator('.visual-parameter').evaluateAll(Items=>Items.map(Item=>({
            kind:Item.dataset.visual, color:getComputedStyle(Item).getPropertyValue('--accent').trim()
        })));
        assert.equal(Visuals.length,12);assert.equal(new Set(Visuals.map(Item=>Item.kind)).size,12,'repeated infographic visual');
        assert.ok(new Set(Visuals.map(Item=>Item.color)).size>=10,'missing distinct colours');
        const Tested=[];
        for (const Group of Groups) {
            if(Group.Name==='geometry') {
                await Page.getByRole('button',{name:'Next editor: Geometry detail'}).click();
                await Page.locator('#GeometryCanvas[data-rendered="true"]').waitFor({timeout:60000});
            }
            await Page.screenshot({path:path.join(__dirname,'Images',Group.Name+'-all-controls.png')});
            await Page.locator(Group.Name==='atmosphere'?'#AtmosphereCard':'#GeometryCard').screenshot({path:path.join(__dirname,'Images',Group.Name+'-dock.png')});
            const Defaults={};
            for (const Id of Group.Inputs) Defaults[Id]=await Page.locator('#'+Id).inputValue();
            for (const Id of Group.Inputs) {
                const Input=Page.locator('#'+Id), Container=Input.locator('..');
                assert.equal(await Input.getAttribute('type'),'range');
                assert.ok(await Input.evaluate(Element=>!!Element.labels.length),'unlabelled control '+Id);
                assert.equal(await Page.locator('input[type=range]').evaluateAll((Items,Id)=>{
                    const Node=Items.find(Item=>Item.id===Id);return Node.closest('[aria-hidden="true"]')===null;
                },Id),true,'hidden from accessibility tree: '+Id);
                await Input.focus();await Page.keyboard.press('Home');
                const Low=Number(await Input.inputValue()), LowMarker=Number(await Container.locator('.control-marker').getAttribute('cx'));
                await Page.keyboard.press('End');
                const High=Number(await Input.inputValue()), HighMarker=Number(await Container.locator('.control-marker').getAttribute('cx'));
                assert.ok(High>Low,Id+' did not respond to keyboard');
                assert.ok(HighMarker>LowMarker+140,Id+' marker did not track keyboard values');
                await Input.scrollIntoViewIfNeeded();
                const Bounds=await Input.boundingBox();
                await Page.mouse.move(Bounds.x+Bounds.width*.2,Bounds.y+Bounds.height/2);
                await Page.mouse.down();await Page.mouse.move(Bounds.x+Bounds.width*.7,Bounds.y+Bounds.height/2,{steps:4});await Page.mouse.up();
                const Pointer=Number(await Input.inputValue());
                assert.ok(Pointer>Low && Pointer<High,Id+' pointer drag did not adjust the value');
                Tested.push(Id);
            }
            await Page.locator('#'+Group.Reset).click();
            for (const Id of Group.Inputs) {
                const Input=Page.locator('#'+Id);assert.equal(await Input.inputValue(),Defaults[Id],Id+' reset failed');
                const Marker=Number(await Input.locator('..').locator('.control-marker').getAttribute('cx'));
                const Expected=await Input.evaluate(Node=>5+150*(Number(Node.value)-Number(Node.min))/(Number(Node.max)-Number(Node.min)));
                assert.ok(Math.abs(Marker-Expected)<.1,Id+' reset marker is stale');
            }
        }
        assert.deepEqual(Errors,[]);
        const Results={checks:'passed',browser:'Chromium / WebGL2 / SwiftShader',tested:Tested,distinctVisuals:Visuals,browserErrors:Errors};
        fs.writeFileSync(path.join(__dirname,'VisualControlsResults.json'),JSON.stringify(Results,null,2)+'\n');
        console.log(JSON.stringify(Results,null,2));
    } finally {await Browser.close();}
})().catch(Error=>{console.error(Error);process.exitCode=1;});
