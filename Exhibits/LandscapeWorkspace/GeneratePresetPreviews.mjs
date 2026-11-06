/*==========================================================================================================================================
                                                       GENERATEPRESETPREVIEWS.MJS
==========================================================================================================================================*/
// 🧩 Capture actual WebGL2 preset volume renders for the editor's example library.

import { createRequire } from 'node:module';
import { writeFile } from 'node:fs/promises';
import { LandformPresets } from '../../Experimental/LandscapeWorkspace/LandformPresets.js';
const Require=createRequire(new URL('../../Experimental/LandscapeWorkspace/package.json',import.meta.url));
const {chromium:BrowserType}=Require('playwright');const Chromium=Require('@sparticuz/chromium').default;
const Browser=await BrowserType.launch({executablePath:await Chromium.executablePath(),args:[...Chromium.args,'--enable-webgl','--ignore-gpu-blocklist'],headless:true});
const Captures=[];
try
{
    const Page=await Browser.newPage({viewport:{width:480,height:320},deviceScaleFactor:1});
    await Page.goto((process.env.LANDSCAPE_URL||'http://localhost:5173')+'/PresetExamples.html');
    await Page.waitForFunction(()=>typeof window.RenderPreset==='function');
    for(const Preset of LandformPresets)
    {
        const Image=await Page.evaluate(Token=>{window.RenderPreset(Token);return document.querySelector('#Example').toDataURL('image/png');},Preset.Token);
        Captures.push({Token:Preset.Token,Bytes:Buffer.from(Image.split(',')[1],'base64')});
        console.log('Captured actual SDF preset:',Preset.Name);
    }
}
finally{await Browser.close();}

for(const Capture of Captures)await writeFile(new URL(`../../Experimental/LandscapeWorkspace/PresetPreviews/${Capture.Token}.png`,import.meta.url),Capture.Bytes);
