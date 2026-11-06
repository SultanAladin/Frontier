/*==========================================================================================================================================
                                                     REFERENCESTACKBROWSERVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Verify the actual source row template/styles against the working mask-owned stack and its interactions.

import { createRequire } from 'node:module';
import Assert from 'node:assert/strict';
import { readFile,writeFile } from 'node:fs/promises';
import { CreateLandform } from '../../Experimental/LandscapeWorkspace/LandformStack.js';
const Require=createRequire(new URL('../../Experimental/LandscapeWorkspace/package.json',import.meta.url));
const {chromium:BrowserType}=Require('playwright'),Chromium=Require('@sparticuz/chromium').default;
const Icons=await import(Require.resolve('lucide'));
const SVG=Glyph=>`<svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${Glyph.map(([Tag,Attributes])=>`<${Tag} ${Object.entries(Attributes).map(([Name,Setting])=>`${Name}="${Setting}"`).join(' ')}/>`).join('')}</svg>`;
const Browser=await BrowserType.launch({executablePath:await Chromium.executablePath(),args:[...Chromium.args,'--enable-webgl','--ignore-gpu-blocklist'],headless:true});
const Context=await Browser.newContext({viewport:{width:1600,height:1100},deviceScaleFactor:1});const Page=await Context.newPage();
const Errors=[],Results=[];Page.on('pageerror',Failure=>Errors.push(Failure.message));
const Layer=(Name,X)=>({Name,Color:'#a99778',Visible:true,Depth:100,Caves:false,Resolution:256,Vertical:64,Strokes:[{Points:[[X,0]],Radius:180,Erase:false}],Regions:[{X0:X-230,Z0:-230,X1:X+230,Z1:230,Edge:12}],Landforms:[]});
const Fixture={Format:'FrontierLandscape/1',Layers:[Layer('Desert coast',0),Layer('Second mask',650)]};
Fixture.Layers[0].Landforms=['mesa','ridges','tunnel'].map(Type=>({...CreateLandform(Type),Scale:1,Blend:0}));Fixture.Layers[1].Landforms=[CreateLandform('dunes')];
await Page.addInitScript(Document=>localStorage.setItem('FrontierLandscape',JSON.stringify(Document)),Fixture);
async function Check(Name,Evaluation){await Evaluation();Results.push({Check:Name,Result:'Passed'});console.log('Passed:',Name);}
async function Export()
{
    const [Download]=await Promise.all([Page.waitForEvent('download'),Page.click('#Export')]);return JSON.parse(await readFile(await Download.path(),'utf8'));
}
async function Import(Document)
{
    await Page.locator('#File').setInputFiles({name:'ReferenceStack.json',mimeType:'application/json',buffer:Buffer.from(JSON.stringify(Document))});await Page.waitForTimeout(80);
}
const Operations=()=>Page.locator('#tp-layer-list [data-operation-index]');
async function Styles(Locator)
{
    return Locator.evaluate(Row=>
    {
        const Retrieve=(Selector,Properties)=>{const Target=Selector?Row.querySelector(Selector):Row,Style=getComputedStyle(Target);return Object.fromEntries(Properties.map(Property=>[Property,Style[Property]]));};
        return {Row:Retrieve(null,['height','minHeight','paddingTop','paddingLeft','backgroundColor','borderTopColor','borderRadius','gap']),Icon:Retrieve('.tp-layer-icon',['width','height','backgroundColor','borderTopWidth','color']),Name:Retrieve('.tp-layer-name',['fontSize','fontWeight','color']),Step:Retrieve('.tp-step-pill',['borderRadius','fontSize']),Eye:Retrieve('.tp-icon-btn',['width','height','borderRadius']),EyeGlyph:Retrieve('.tp-icon-btn svg',['width','height']),Selection:getComputedStyle(Row,'::before').backgroundColor};
    });
}
try
{
    await Page.goto(process.env.LANDSCAPE_URL||'http://localhost:5174');await Page.waitForSelector('#tp-layer-list .tp-layer-row');
    await Check('Reference outliner is the primary active-mask stack, not tiny nested custom cards',async()=>
    {
        Assert.equal(await Operations().count(),3);Assert.equal(await Page.locator('#tp-layer-list .tp-layer-row').count(),5);Assert.equal(await Page.locator('.operation-outline').count(),0);
        Assert.equal(await Page.locator('#tp-layer-count').textContent(),'5');Assert.equal(await Page.locator('#VisibleMaskCount').textContent(),'5');
        Assert.deepEqual(await Page.locator('#tp-layer-list .tp-step-pill').allTextContents(),['#4','#3','#2','#1','LAST']);
        Assert.equal(await Page.locator('#tp-layer-list .tp-layer-actions').count(),1);
    });
    await Check('Source add menu adds directly to the active mask and closes on selection/Escape/outside click',async()=>
    {
        await Page.click('#tp-btn-add-layer');Assert.equal(await Page.locator('.tp-menu-item').count(),16);Assert.equal(await Page.locator('#tp-add-layer-menu').isVisible(),true);
        await Page.click('[data-add-type="fractal"]');Assert.equal(await Operations().count(),4);Assert.equal(await Page.locator('#tp-add-layer-menu').isVisible(),false);
        const Document=await Export();Assert.equal(Document.Layers[0].Landforms.at(-1).Type,'fractal');Assert.deepEqual(Document.Layers[1],Fixture.Layers[1]);
        await Page.click('#tp-btn-add-layer');await Page.keyboard.press('Escape');Assert.equal(await Page.locator('#tp-add-layer-menu').isVisible(),false);
        await Page.click('#tp-btn-add-layer');await Page.click('#LayerTitle');Assert.equal(await Page.locator('#tp-add-layer-menu').isVisible(),false);await Page.click('#HistoryUndo');
    });
    await Check('Source search filters names and operation summaries without altering stack data',async()=>
    {
        const Before=await Export();await Page.fill('#tp-layer-search-input','mesa');Assert.equal(await Operations().count(),1);
        await Page.fill('#tp-layer-search-input','not a layer');Assert.match(await Page.locator('#tp-layer-list').textContent(),/No matching/);
        await Page.fill('#tp-layer-search-input','');Assert.deepEqual(await Export(),Before);
    });
    await Check('Click selection exposes only that row’s Up/Down/Duplicate/Delete strip and inspector',async()=>
    {
        await Page.locator('[data-operation-index="0"] .tp-layer-row').click();Assert.equal(await Page.locator('#OperationName').inputValue(),'Mesa / plateau');
        Assert.equal(await Page.locator('[data-operation-index="0"] .tp-layer-actions').count(),1);Assert.equal(await Page.locator('[data-operation-index="2"] .tp-layer-actions').count(),0);
        Assert.equal(await Page.locator('.tp-amount').count(),10);Assert.equal(await Page.locator('.tp-identity-badge svg').count(),1);
    });
    await Check('Eye controls mute operations; status, muted count, volume data and undo stay synchronized',async()=>
    {
        await Page.locator('[data-operation-index="0"]').getByTitle('Mute layer',{exact:true}).click();Assert.equal(await Page.locator('#MutedMaskCount').textContent(),'1');
        Assert.equal((await Export()).Layers[0].Landforms[0].Enabled,false);Assert.match(await Page.locator('[data-operation-index="0"] .tp-layer-row').getAttribute('class'),/muted/);
        await Page.click('#HistoryUndo');Assert.equal(await Page.locator('#MutedMaskCount').textContent(),'0');
    });
    await Check('Layer names are editable, escaped and round-trip without changing geometry parameters',async()=>
    {
        await Page.fill('#OperationName','Mesa <edited>');await Page.locator('#OperationName').dispatchEvent('change');
        Assert.equal(await Page.locator('[data-operation-index="0"] .tp-layer-name').textContent(),'Mesa <edited>');Assert.equal(await Page.locator('#tp-layer-list edited').count(),0);
        const Document=await Export();Assert.equal(Document.Layers[0].Landforms[0].Name,'Mesa <edited>');await Import(Document);Assert.equal((await Export()).Layers[0].Landforms[0].Name,'Mesa <edited>');
    });
    await Check('Duplicate creates an independent editable operation on this mask; delete and undo work',async()=>
    {
        await Page.locator('[data-operation-index="0"] .tp-layer-row').click();await Page.getByTitle('Duplicate layer',{exact:true}).click();Assert.equal(await Operations().count(),4);
        await Page.locator('[data-property="Amount"]').fill('180');await Page.locator('[data-property="Amount"]').dispatchEvent('change');const Document=await Export();
        Assert.equal(Document.Layers[0].Landforms[1].Amount,180);Assert.equal(Document.Layers[0].Landforms[0].Amount,48);Assert.deepEqual(Document.Layers[1],Fixture.Layers[1]);
        await Page.getByTitle('Delete layer',{exact:true}).click();Assert.equal(await Operations().count(),3);await Page.click('#HistoryUndo');Assert.equal(await Operations().count(),4);
    });
    await Check('Source arrow buttons and real drag/drop reorder persisted operations within the active mask',async()=>
    {
        await Import(Fixture);await Page.locator('[data-operation-index="0"] .tp-layer-row').click();await Page.getByTitle('Move later in pipeline (up)',{exact:true}).click();Assert.equal((await Export()).Layers[0].Landforms[1].Type,'mesa');
        await Page.locator('[data-operation-index="2"] .tp-layer-row').dragTo(Page.locator('[data-operation-index="0"] .tp-layer-row'));Assert.equal((await Export()).Layers[0].Landforms[0].Type,'tunnel');
        Assert.deepEqual((await Export()).Layers[1],Fixture.Layers[1]);
    });
    await Check('Switching active masks restores each independent stack without mixing operation selections',async()=>
    {
        await Page.selectOption('#ActiveMask','1');Assert.equal(await Operations().count(),1);Assert.equal(await Page.locator('[data-property="Type"]').inputValue(),'dunes');
        await Page.selectOption('#ActiveMask','0');Assert.equal(await Operations().count(),3);
    });
    await Check('Foundation and Environment have locked positions and real contextual inspector identities',async()=>
    {
        for(const Index of [-1,-2]){await Page.locator(`.tp-layer-row[data-index="${Index}"]`).click();Assert.equal(await Page.locator(`.tp-layer-row[data-index="${Index}"]`).getAttribute('draggable'),'false');Assert.equal(await Page.getByTitle('Delete layer',{exact:true}).count(),0);}
        Assert.equal(await Page.locator('#OperationName').inputValue(),'Environment');Assert.equal(await Page.locator('.tp-io-pill.write').textContent(),'WRITES · SHADING');
    });
    await Check('Source extracted CSS reproduces the original row template’s geometry, glyph styling and selection',async()=>
    {
        await Import(Fixture);await Page.locator('[data-operation-index="0"] .tp-layer-row').click();
        const Row=Page.locator('[data-operation-index="0"] .tp-layer-row'),Current=await Styles(Row),Width=(await Row.boundingBox()).width;
        const Source=await readFile(new URL('../../Experimental/LandscapeWorkspace/ReferenceSnapshot/layer-stack-ui.js',import.meta.url),'utf8');
        const Start=Source.indexOf('      html += `',Source.indexOf('  renderStackList() {'))+'      html += `'.length,End=Source.indexOf('      `;',Start),Template=Source.slice(Start,End);
        const Render=new Function('layer','idx','isSelected','environmentLayer','positionLocked','stepLabel','evalOrderNum','iconSvg','summary','hasMask','supportsMask','canMoveDown','cat','layerIconColor','SVG_ICONS','return `'+Template+'`;');
        const Glyphs={mountain:SVG(Icons.Mountain),check:SVG(Icons.Check),mask:SVG(Icons.Contrast),eyeOpen:SVG(Icons.Eye),eyeClosed:SVG(Icons.EyeOff),lock:SVG(Icons.Lock)};
        const Escape=Text=>String(Text).replace(/[&<>"']/g,Character=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[Character]));
        const HTML=Render.call({_esc:Escape,pipeline:{shadingMode:0}},{id:'reference',name:'Mesa / plateau',type:'mesa',enabled:true,locked:false,mask:{enabled:true,mode:'inherited',min:0,max:1}},0,true,false,false,'#2',2,Glyphs.mountain,'UNION · 48 m · seed 4029',true,true,false,{},()=> '#ff8a5c',Glyphs);
        const Reference=await Page.context().newPage();await Reference.setViewportSize({width:Math.ceil(Width)+40,height:300});
        const CSS=await readFile(new URL('../../Experimental/LandscapeWorkspace/ReferenceSnapshot/style.css',import.meta.url),'utf8');
        await Reference.setContent(`<style>${CSS}</style><div style="width:${Width}px;margin:20px">${HTML}</div>`);
        const Original=await Styles(Reference.locator('.tp-layer-row'));Assert.deepEqual(Current,Original);
        Results.push({Measurement:'Original source row vs applied row computed styling',Observed:Current});
        await Row.screenshot({path:new URL('./ReferenceStackAppliedRow.png',import.meta.url).pathname});await Reference.locator('.tp-layer-row').screenshot({path:new URL('./ReferenceStackOriginalRow.png',import.meta.url).pathname});await Reference.close();await Page.bringToFront();
    });
    await Page.locator('[data-operation-index="1"] .tp-layer-row').click();await Page.click('#FocusMask');
    await Page.waitForFunction(()=>{const Canvas=document.querySelector('#Volume');return Canvas.dataset.completedTarget===Canvas.dataset.cameraTarget&&Canvas.dataset.completedRevision===Canvas.dataset.volumeRevision;},{},{timeout:90000});
    await Page.screenshot({path:new URL('./ReferenceLayerStack.png',import.meta.url).pathname,timeout:90000});
    await Page.click('#tp-btn-add-layer');await Page.screenshot({path:new URL('./ReferenceLayerAddMenu.png',import.meta.url).pathname,timeout:90000});
    Assert.deepEqual(Errors,[]);await writeFile(new URL('./ReferenceStackBrowserResults.json',import.meta.url),JSON.stringify({URL:process.env.LANDSCAPE_URL||'http://localhost:5174',SourceRevision:'09b7064f282f4df5c5352f2bc97c82fb1c06a098',Results,Errors},null,2));
}
finally{await Browser.close();}
