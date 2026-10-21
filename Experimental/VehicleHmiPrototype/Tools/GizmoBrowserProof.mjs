// Optional real-browser proof: npm install --no-save playwright @sparticuz/chromium
import {chromium} from 'playwright';
import Chromium from '@sparticuz/chromium';
import {inflate} from './node_modules/@sparticuz/chromium/build/lambdafs.js';
import path from 'node:path';
import fs from 'node:fs';
const ProofFrames=Number(process.env.PROOF_FRAMES??1);
const Output=process.env.OUT_DIR??'/home/user/native-proof';fs.mkdirSync(Output,{recursive:true});
const Executable=await Chromium.executablePath();await inflate(path.resolve('Experimental/VehicleHmiPrototype/Tools/node_modules/@sparticuz/chromium/bin/al2023.tar.br'));
const Browser=await chromium.launch({executablePath:Executable,env:{...process.env,LD_LIBRARY_PATH:'/tmp/al2023/lib',VK_ICD_FILENAMES:'/tmp/vk_swiftshader_icd.json'},args:[...Chromium.args.filter(A=>!['--single-process','--in-process-gpu'].includes(A)),'--enable-unsafe-webgpu','--use-webgpu-adapter=swiftshader','--enable-features=Vulkan','--use-vulkan=swiftshader','--disable-vulkan-surface'],headless:true});
const Page=await Browser.newPage({viewport:{width:1600,height:1100},deviceScaleFactor:1});let Closing=false;
const Errors=[];Page.on('pageerror',E=>{if(!Closing){Errors.push(String(E));console.log('PAGE ERROR',String(E));}});
await Page.addInitScript(Count=>{const RAF=window.requestAnimationFrame.bind(window);window.__ProofRAF=RAF;window.requestAnimationFrame=Callback=>RAF(Time=>{if(window.IlluminationExhibit?.FrameIndex>=Count)return;Callback(Time);});},ProofFrames);
if(process.env.CLOUD_COVERAGE)await Page.addInitScript(Coverage=>{try{localStorage.setItem('Frontier.Environment.v1',JSON.stringify({CloudCoverage:Coverage}));}catch{}},Number(process.env.CLOUD_COVERAGE));
async function Shot(Options){const Box=await Page.locator('#inspector').boundingBox();return Page.screenshot({...Options,clip:Box,timeout:90000});}
await Page.goto('http://127.0.0.1:8080/?quality=low&stresspool=0&inspector=atmosphere',{waitUntil:'domcontentloaded'});
await Page.waitForSelector('.native-environment h1',{timeout:90000});
await Page.waitForFunction(N=>window.IlluminationExhibit?.FrameIndex>=N,ProofFrames,{timeout:150000,polling:100});
await Page.evaluate(()=>window.IlluminationExhibit.Exchange.Device.queue.onSubmittedWorkDone());


await Page.evaluate(()=>{const H=window.IlluminationExhibit;H.CameraState={Position:[0,0,3],Forward:[.7,.7,-.14],Right:[-.707,.707,0],Up:[.1,.1,.99],TanY:.6,Aspect:1};const C=H.CameraState;H.Settings.LocalFogX=C.Position[0]+C.Forward[0]*30;H.Settings.LocalFogY=C.Position[1]+C.Forward[1]*30;H.Settings.LocalFogZ=C.Position[2]+C.Forward[2]*30;H.Shell.Select('local-fog');H.Shell.Gizmo.Draw();});
const Before=await Page.evaluate(()=>window.IlluminationExhibit.Settings.LocalFogX);
const Axis=Page.locator('svg line[data-axis="0"]');const Hit=await Axis.evaluate(E=>{const B=E.ownerSVGElement.getBoundingClientRect();return {dx:(+E.getAttribute('x2'))-(+E.getAttribute('x1')),dy:(+E.getAttribute('y2'))-(+E.getAttribute('y1')),x:B.x+(+E.getAttribute('x1'))*.25+(+E.getAttribute('x2'))*.75,y:B.y+(+E.getAttribute('y1'))*.25+(+E.getAttribute('y2'))*.75};});const X=Hit.x,Y=Hit.y;
console.log(await Page.evaluate(({X,Y})=>({hit:document.elementFromPoint(X,Y)?.outerHTML,x:X,y:Y}),{X,Y}));await Page.mouse.move(X,Y);await Page.mouse.down();console.log(await Page.evaluate(()=>({drag:!!window.IlluminationExhibit.Shell.Gizmo.Drag})));await Page.mouse.move(X+Hit.dx/Math.hypot(Hit.dx,Hit.dy)*40,Y+Hit.dy/Math.hypot(Hit.dx,Hit.dy)*40,{steps:4});await Page.mouse.up();
const After=await Page.evaluate(()=>window.IlluminationExhibit.Settings.LocalFogX);if(Math.abs(After-Before)<.01)throw new Error('Axis drag did not move actual fog setting');
await Page.keyboard.press('g');await Page.mouse.move(X+80,Y+30);await Page.keyboard.press('Escape');
if(Math.abs(await Page.evaluate(()=>window.IlluminationExhibit.Settings.LocalFogX)-After)>.0001)throw new Error('Escape did not restore transform');
await Page.screenshot({path:path.join(Output,'gizmo.png'),timeout:90000});console.log({before:Before,after:After,axisDrag:'passed',escapeCancel:'passed'});Closing=true;await Browser.close();if(Errors.length)process.exitCode=1;
