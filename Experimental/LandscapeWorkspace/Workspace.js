/*==========================================================================================================================================
                                                              WORKSPACE.JS
==========================================================================================================================================*/
// 🧩 Vector-mask authoring and volumetric landscape workspace.
import { SegmentCount, ValidateDocument, SampleSparseBricks } from './DistanceField.js';
import { CreateVolumeRasterization } from './VolumeRasterization.js';
import { InvalidateLandforms, CreateLandform, LayerVerticalBounds } from './LandformStack.js';
import { StackGlyph } from './StackGlyphs.js';
import { InitializeLayerStackPanel } from './LayerStackPanel.js';
import { CreatePresetWorld } from './LandformPresets.js';
import { CreateTerrainWorld } from './TerrainExamples.js';
import { InitializeCameraNavigation } from './CameraNavigation.js';
const Find = Selector => document.querySelector(Selector);
const Paint = Find('#Paint'), Volume = Find('#Volume'), Ink = Paint.getContext('2d');
const Palette = ['#b4a17d','#7e8c81','#b9cdd3'];
const CreateLayer = Index => ({Name:['Desert coast','Rocky islands','Ice region'][Index] || `Region ${Index+1}`,Color:Palette[Index%3],Visible:true,Depth:120,Caves:false,Resolution:1024,Vertical:128,Landforms:[],Strokes:[],Regions:[]});
let Layers = [CreateLayer(0),CreateLayer(1),CreateLayer(2)];
const Seed = (Index,Points,Radius) => Layers[Index].Strokes.push({Points,Radius,Erase:false});
Seed(0,[[-350,50],[-310,-90],[-180,-170],[-80,-110],[-50,-10],[-130,80],[-100,190],[-220,230],[-330,160]],110);
Seed(0,[[-140,-40],[-80,0],[30,-40],[110,-110]],72);
Seed(1,[[200,70],[280,20],[350,80],[300,170],[220,210]],62);
Seed(1,[[410,-60]],40);Seed(1,[[120,310],[200,350]],33);
Seed(2,[[-80,-390],[20,-370],[90,-310],[180,-360]],64);
Layers[0].Regions=[{X0:-500,Z0:-300,X1:150,Z1:380}];
Layers[1].Regions=[{X0:120,Z0:-140,X1:490,Z1:420}];
Layers[2].Regions=[{X0:-170,Z0:-490,X1:280,Z1:-230}];
for(const [Index,Type,Amount,Edge] of [[0,'fractal',150,12],[1,'ridged-multifractal',180,12],[2,'sand-dunes',85,12]])
{
    const Operation=CreateLandform(Type);Operation.Amount=Amount;
    Operation.Blend=0;Layers[Index].Landforms=[Operation];Layers[Index].Regions[0].Edge=Edge;
    Layers[Index].Name=['Fractal terrain','Ridged multifractal','Sand dunes'][Index];
}
let MaskScopesExpanded=false;
let SelectedLayers=new Set([0]);
let Active=0, Tool='paint', Workspace='mask', Projection={X:0,Z:0,Scale:.65};
let Selection=null, Gesture=null, Space=false, History=[], Future=[], Rasterization, SaveTimer;
const Camera={Yaw:.35,Pitch:.65,Orbit:1500,Target:[0,0,0]};
const Layer=()=>Layers[Active];
const Report=Message=>Find('#Status').textContent=Message;
const Document=()=>({Format:'FrontierLandscape/1',Layers});
try {const Stored=localStorage.getItem('FrontierLandscape');if(Stored)Layers=ValidateDocument(JSON.parse(Stored)).Layers;}
catch(Failure){Report(`Stored document could not be read: ${Failure.message}`);}
function Remember()
{
    History.push(JSON.stringify(Layers));if(History.length>60)History.shift();Future=[];
}
function Refresh()
{
    Find('#ActiveMask').replaceChildren();Layers.forEach((Entry,Index)=>{const Option=document.createElement('option');Option.value=Index;Option.textContent=Entry.Name;Find('#ActiveMask').append(Option);});Find('#ActiveMask').value=Active;
    Find('#Layers').replaceChildren();
    Layers.forEach((Entry,Index)=>
    {
        const Row=document.createElement('div');Row.className=`layer tp-layer-row ${Index===Active?'active selected':''} ${Entry.Visible?'':'muted'}`;Row.tabIndex=0;Row.dataset.maskIndex=Index;Row.setAttribute('aria-label',`Edit mask ${Entry.Name}`);Row.setAttribute('aria-selected',String(Index===Active));
        const Swatch=document.createElement('span');Swatch.className='tp-layer-icon';Swatch.style.setProperty('--cat-accent',Entry.Color);Swatch.append(StackGlyph('mask'));
        const Caption=document.createElement('div');Caption.className='tp-layer-meta';
        const Title=document.createElement('div');Title.className='tp-layer-title-line';
        const Name=document.createElement('span');Name.className='tp-layer-name';Name.textContent=Entry.Name;Title.append(Name);
        const Detail=document.createElement('span');Detail.className='tp-layer-sub';Detail.textContent=`${Entry.Strokes.length} strokes · ${Entry.Regions.length} bounds`;Caption.append(Title,Detail);
        const Eye=document.createElement('button');Eye.className=`tp-icon-btn ${Entry.Visible?'vis-on':'vis-off'}`;Eye.append(StackGlyph(Entry.Visible?'eye':'eye-off'));Eye.title='Toggle layer visibility';Eye.setAttribute('aria-label',`Toggle mask visibility: ${Entry.Name}`);
        Eye.onclick=Event=>{Event.stopPropagation();Remember();Entry.Visible=!Entry.Visible;Changed();};
        const Batch=document.createElement('input');Batch.type='checkbox';Batch.className='layer-scope';Batch.checked=SelectedLayers.has(Index);Batch.setAttribute('aria-label',`Select ${Entry.Name} for batch`);
        Batch.onclick=Event=>Event.stopPropagation();Batch.onchange=()=>{if(Batch.checked)SelectedLayers.add(Index);else SelectedLayers.delete(Index);Refresh();};
        Row.append(Batch,Swatch,Caption,Eye);Row.onclick=Event=>{Active=Index;if(!Event.ctrlKey&&!Event.metaKey)SelectedLayers=new Set([Index]);else if(SelectedLayers.has(Index))SelectedLayers.delete(Index);else SelectedLayers.add(Index);Selection=null;Refresh();Draw();};
        Row.onkeydown=Event=>{if(Event.key==='Enter')Row.click();};Find('#Layers').append(Row);
    });
    Find('#MaskLayerCount').textContent=Layers.length;
    Panel.RenderOutline(Layer(),Find('#tp-layer-list'));
    Find('#LayerTitle').textContent=Layer().Name;
    Find('#Depth').value=Layer().Depth;Find('#DepthRead').textContent=`${Layer().Depth} m`;
    Find('#Caves').checked=Layer().Caves;Find('#Resolution').value=Layer().Resolution;Find('#Vertical').value=Layer().Vertical;
    Find('#RegionCount').textContent=`${Layer().Regions.length} REGIONS`;Find('#RegionList').replaceChildren();
    Layer().Regions.forEach((Region,Index)=>
    {
        const Row=document.createElement('div');Row.className='region';Row.textContent=`▣ Region ${String(Index+1).padStart(2,'0')}`;
        const Remove=document.createElement('button');Remove.textContent='×';Remove.title='Remove region';
        Remove.onclick=()=>{Remember();Layer().Regions.splice(Index,1);Changed();};
        const Detail=document.createElement('small');Detail.textContent=`${Math.round(Region.X1-Region.X0)} × ${Math.round(Region.Z1-Region.Z0)} m`;
        Row.append(Remove,Detail);Find('#RegionList').append(Row);
    });
    const SelectedRegion=Find('#BakeRegion').value;
    Find('#BakeRegion').replaceChildren();
    Layer().Regions.forEach((Region,Index)=>
    {
        const Option=document.createElement('option');Option.value=Index;
        Option.textContent=`Region ${String(Index+1).padStart(2,'0')} · ${Math.round(Region.X1-Region.X0)} × ${Math.round(Region.Z1-Region.Z0)} m`;
        Find('#BakeRegion').append(Option);
    });
    if([...Find('#BakeRegion').options].some(Option=>Option.value===SelectedRegion))Find('#BakeRegion').value=SelectedRegion;
    Find('#VolumeNote').textContent=`${Layer().Resolution} × ${Layer().Vertical} × ${Layer().Resolution} cells per selected region. GPU preview stays analytic; full-resolution export samples the volume in the background.`;
    Find('#StrokeCount').textContent=`${SegmentCount(Layers)} / 512 capsule samples`;
    Find('#SelectionPrompt').hidden=!Selection||Workspace!=='mask';
    Panel.Render();
}
function Changed()
{
    InvalidateLandforms();
    SelectedLayers=new Set([...SelectedLayers].filter(Index=>Index<Layers.length));
    Refresh();Draw();if(Rasterization){try{Rasterization.Upload(Layers);}catch(Failure){Report(Failure.message);}}
    clearTimeout(SaveTimer);SaveTimer=setTimeout(()=>{try{localStorage.setItem('FrontierLandscape',JSON.stringify(Document()));}catch{Report('Local storage full. Export your project.');}},250);
}
function World(Event)
{
    const Bounds=Paint.getBoundingClientRect();
    return [(Event.clientX-Bounds.left-Bounds.width/2)/Projection.Scale+Projection.X,(Event.clientY-Bounds.top-Bounds.height/2)/Projection.Scale+Projection.Z];
}
function Draw()
{
    if(Workspace!=='mask')return;
    const Ratio=Math.min(devicePixelRatio,2), Width=Paint.clientWidth, Height=Paint.clientHeight;
    Paint.width=Math.floor(Width*Ratio);Paint.height=Math.floor(Height*Ratio);
    Ink.setTransform(Ratio,0,0,Ratio,0,0);Ink.fillStyle='#151a1c';Ink.fillRect(0,0,Width,Height);
    const Grid=100*Math.pow(10,Math.floor(Math.log10(1/Projection.Scale)));
    const Left=Projection.X-Width/2/Projection.Scale, Top=Projection.Z-Height/2/Projection.Scale;
    Ink.strokeStyle='#252e31';Ink.lineWidth=1;Ink.beginPath();
    for(let X=Math.floor(Left/Grid)*Grid;X<Left+Width/Projection.Scale;X+=Grid){const Pixel=(X-Projection.X)*Projection.Scale+Width/2;Ink.moveTo(Pixel,0);Ink.lineTo(Pixel,Height);}
    for(let Z=Math.floor(Top/Grid)*Grid;Z<Top+Height/Projection.Scale;Z+=Grid){const Pixel=(Z-Projection.Z)*Projection.Scale+Height/2;Ink.moveTo(0,Pixel);Ink.lineTo(Width,Pixel);}
    Ink.stroke();
    const Surface=document.createElement('canvas');Surface.width=Paint.width;Surface.height=Paint.height;const Brush=Surface.getContext('2d');
    for(const Entry of Layers)
    {
        if(!Entry.Visible)continue;
        Brush.setTransform(1,0,0,1,0,0);Brush.clearRect(0,0,Surface.width,Surface.height);
        Brush.setTransform(Ratio*Projection.Scale,0,0,Ratio*Projection.Scale,Ratio*(Width/2-Projection.X*Projection.Scale),Ratio*(Height/2-Projection.Z*Projection.Scale));
        for(const Stroke of Entry.Strokes)
        {
            Brush.globalCompositeOperation=Stroke.Erase?'destination-out':'source-over';Brush.fillStyle=Entry.Color;Brush.strokeStyle=Entry.Color;
            Brush.lineWidth=Stroke.Radius*2;Brush.lineCap='round';Brush.lineJoin='round';Brush.beginPath();Brush.moveTo(...Stroke.Points[0]);
            for(const Point of Stroke.Points)Brush.lineTo(...Point);
            Brush.stroke();Brush.beginPath();Brush.arc(...Stroke.Points[0],Stroke.Radius,0,Math.PI*2);Brush.fill();
        }
        Ink.globalAlpha=Entry===Layer()?.85:.45;Ink.drawImage(Surface,0,0,Width,Height);
    }
    Ink.globalAlpha=1;Ink.setTransform(Ratio*Projection.Scale,0,0,Ratio*Projection.Scale,Ratio*(Width/2-Projection.X*Projection.Scale),Ratio*(Height/2-Projection.Z*Projection.Scale));
    Layers.forEach((Entry,Index)=>
    {
        if(!Entry.Visible)return;
        Entry.Regions.forEach((Region,Offset)=>
        {
            Ink.strokeStyle=Index===Active?'#c7c1a1':'#66787b';Ink.lineWidth=1/Projection.Scale;Ink.setLineDash([6/Projection.Scale,5/Projection.Scale]);
            Ink.strokeRect(Region.X0,Region.Z0,Region.X1-Region.X0,Region.Z1-Region.Z0);
            Ink.fillStyle=Index===Active?'#c7c1a1':'#7f9294';Ink.font=`${9/Projection.Scale}px Segoe UI`;
            Ink.fillText(`${Entry.Name.toUpperCase()} / ${String(Offset+1).padStart(2,'0')}`,Region.X0,Region.Z0-10/Projection.Scale);
        });
    });
    if(Selection){Ink.setLineDash([]);Ink.strokeStyle='#e6d6a5';Ink.fillStyle='#e6d6a518';Ink.fillRect(Selection.X0,Selection.Z0,Selection.X1-Selection.X0,Selection.Z1-Selection.Z0);Ink.strokeRect(Selection.X0,Selection.Z0,Selection.X1-Selection.X0,Selection.Z1-Selection.Z0);}
    Ink.setLineDash([]);Find('#Zoom').textContent=`${Math.round(Projection.Scale*100)}%`;
}
function SelectTool(Next)
{
    Tool=Next;document.querySelectorAll('[data-tool]').forEach(Button=>Button.classList.toggle('active',Button.dataset.tool===Tool));Paint.style.cursor=Tool==='pan'?'grab':'crosshair';
}
function Fit(ActiveOnly=false)
{
    const Sources=ActiveOnly===true?[Layer()]:Layers.filter(Entry=>Entry.Visible);
    const Regions=Sources.flatMap(Entry=>Entry.Regions);
    if(!Regions.length){Projection={X:0,Z:0,Scale:.65};Draw();return;}
    const X0=Math.min(...Regions.map(Region=>Region.X0)), X1=Math.max(...Regions.map(Region=>Region.X1));
    const Z0=Math.min(...Regions.map(Region=>Region.Z0)), Z1=Math.max(...Regions.map(Region=>Region.Z1));
    Projection={X:(X0+X1)/2,Z:(Z0+Z1)/2,Scale:Math.max(.008,Math.min((Find('#Viewport').clientWidth-140)/(X1-X0),(Find('#Viewport').clientHeight-150)/(Z1-Z0)))};
    const Height=Math.max(...Sources.flatMap(Entry=>Entry.Regions.map(Region=>LayerVerticalBounds(Entry,Region)[1])));
    Camera.Target=[Projection.X,Height*.2,Projection.Z];Volume.dataset.cameraTarget=JSON.stringify(Camera.Target);Camera.Orbit=Math.max(X1-X0,Z1-Z0)*1.2;Draw();
}
function SwitchWorkspace(Next)
{
    if(Next==='landscape')
    {
        try{ValidateDocument(Document());if(!Rasterization)Rasterization=CreateVolumeRasterization(Volume);Rasterization.Upload(Layers);}
        catch(Failure){Report(Failure.message);return;}
        Report(Layers.some(Entry=>Entry.Visible&&Entry.Regions.length&&Entry.Strokes.length)?'Volumetric terrain · LMB orbit / Pan tool · MMB or Shift-drag pan · wheel dolly · optional WASD':'No authored terrain. Paint and add a region to a visible layer.');
    }
    Workspace=Next;
    Find('#WorkspaceStackTitle').textContent=Next==='mask'?'Mask layers':'Landform layers';
    Find('#MaskLayerPane').hidden=Next!=='mask';Find('#left-erosion-panels').hidden=Next!=='landscape';
    Find('#LandscapeMaskScope').hidden=Next!=='landscape';Find('#LandformPanel').hidden=Next!=='landscape';
    Find('#BrushPanel').hidden=Next!=='mask';Find('#OceanPanel').hidden=Next!=='landscape';Find('#PresetLibrary').hidden=Next!=='landscape';
    Find(Next==='mask'?'#MaskRowsContainer':'#MaskScopeContainer').append(Find('#Layers'));
    Find('#Layers').hidden=Next!=='mask'&&!MaskScopesExpanded;
    Navigation.Clear();Find('#CameraNavigation').hidden=Next!=='landscape';Paint.hidden=Next!=='mask';Volume.hidden=Next==='mask';
    Find('#MaskTab').classList.toggle('active',Next==='mask');Find('#LandscapeTab').classList.toggle('active',Next==='landscape');
    Find('#ViewTitle').textContent=Next==='mask'?'2D mask canvas':'SDF landscape';Find('#ViewSubtitle').textContent=Next==='mask'?' / Top orthographic':' / WebGL2 · analytic volume';
    Find('#ModeLabel').textContent=Next==='mask'?'MASK AUTHORING':'LANDSCAPE WORKSPACE';
    Find('#CanvasHint').textContent=Next==='mask'?'Paint a coastline. Select a region. Build your landscape.':'Drag to orbit · MMB / Shift-drag to pan · enable WASD for keyboard movement.';
    Find('.tools').hidden=Next!=='mask';Refresh();Draw();
}
Paint.onpointerdown=Event=>
{
    if(Event.button===2)return;Paint.setPointerCapture(Event.pointerId);const Point=World(Event), Mode=Space||Event.button===1?'pan':Tool;
    Gesture={Mode,Point,Client:[Event.clientX,Event.clientY]};
    if(Mode==='paint'||Mode==='erase')
    {
        if(SegmentCount(Layers)>=512){Gesture=null;Report('512-sample GPU prototype capacity reached. Undo or start another document.');return;}
        Remember();Layer().Strokes.push({Points:[Point],Radius:Number(Find('#Radius').value),Erase:Mode==='erase'});
    }
    if(Mode==='select'){Selection={X0:Point[0],Z0:Point[1],X1:Point[0],Z1:Point[1]};Refresh();}Draw();
};
Paint.onpointermove=Event=>
{
    const Point=World(Event);Find('#Coordinates').textContent=`X ${Math.round(Point[0])}   Z ${Math.round(Point[1])}`;if(!Gesture)return;
    if(Gesture.Mode==='pan'){Projection.X-=(Event.clientX-Gesture.Client[0])/Projection.Scale;Projection.Z-=(Event.clientY-Gesture.Client[1])/Projection.Scale;Gesture.Client=[Event.clientX,Event.clientY];}
    else if(Gesture.Mode==='select')Selection={X0:Math.min(Point[0],Gesture.Point[0]),Z0:Math.min(Point[1],Gesture.Point[1]),X1:Math.max(Point[0],Gesture.Point[0]),Z1:Math.max(Point[1],Gesture.Point[1])};
    else{const Points=Layer().Strokes.at(-1).Points, Previous=Points.at(-1);if(Math.hypot(Point[0]-Previous[0],Point[1]-Previous[1])>Math.max(5,Number(Find('#Radius').value)*.16)&&SegmentCount(Layers)<512)Points.push(Point);}
    Draw();
};
function FinishGesture(){if(!Gesture)return;if(Selection&&(Selection.X1-Selection.X0<2||Selection.Z1-Selection.Z0<2))Selection=null;Gesture=null;Changed();}
Paint.onpointerup=FinishGesture;Paint.onpointercancel=FinishGesture;
Paint.addEventListener('wheel',Event=>{Event.preventDefault();const Before=World(Event);Projection.Scale=Math.max(.008,Math.min(25,Projection.Scale*Math.exp(-Event.deltaY*.001)));const After=World(Event);Projection.X+=Before[0]-After[0];Projection.Z+=Before[1]-After[1];Draw();},{passive:false});
Volume.onpointerdown=Event=>{Volume.setPointerCapture(Event.pointerId);Gesture={Client:[Event.clientX,Event.clientY],Mode:Event.shiftKey||Space||Event.button===1||Event.button===2||Navigation.RetrieveMode()==='pan'?'pan3d':'orbit'};Event.preventDefault();};
Volume.onpointermove=Event=>
{
    if(!Gesture)return;const ΔX=Event.clientX-Gesture.Client[0], ΔY=Event.clientY-Gesture.Client[1];
    if(Gesture.Mode==='pan3d'){Camera.Target[0]-=(ΔX*Math.cos(Camera.Yaw)+ΔY*Math.sin(Camera.Yaw))*Camera.Orbit*.001;Camera.Target[2]+=(ΔX*Math.sin(Camera.Yaw)-ΔY*Math.cos(Camera.Yaw))*Camera.Orbit*.001;}
    else{Camera.Yaw-=ΔX*.007;Camera.Pitch=Math.max(.08,Math.min(1.5,Camera.Pitch+ΔY*.006));}Gesture.Client=[Event.clientX,Event.clientY];
};
Volume.oncontextmenu=Event=>Event.preventDefault();
Volume.onpointerup=()=>Gesture=null;Volume.onpointercancel=()=>Gesture=null;
Volume.addEventListener('wheel',Event=>{Event.preventDefault();Camera.Orbit=Math.max(80,Math.min(100000,Camera.Orbit*Math.exp(Event.deltaY*.001)));},{passive:false});
Find('#KeepRegion').onclick=()=>{if(!Selection)return;if(Layer().Regions.length>=16){Report('Maximum 16 regions per layer.');return;}Remember();Layer().Regions.push({...Selection});Selection=null;Changed();Report('Region added. Build landscape to evaluate its volumetric field.');};
Find('#ActiveMask').onchange=Event=>{Active=Number(Event.target.value);SelectedLayers=new Set([Active]);Selection=null;Refresh();Draw();};
Find('#ToggleMaskScopes').onclick=()=>{MaskScopesExpanded=!MaskScopesExpanded;Find('#Layers').hidden=!MaskScopesExpanded;Find('#ToggleMaskScopes').setAttribute('aria-expanded',String(MaskScopesExpanded));};
Find('#AddMaskLayer').onclick=()=>Find('#AddLayer').click();
Find('#EmptyMaskWorld').onclick=()=>Find('#Empty').click();
Find('#AddLayer').onclick=()=>{if(Layers.length>=16)return;Remember();Layers.push(CreateLayer(Layers.length));Active=Layers.length-1;SelectedLayers=new Set([Active]);Changed();};
Find('#Empty').onclick=()=>{if(!confirm('Start an empty world? Undo can restore the current document.'))return;Remember();Layers=[CreateLayer(0)];Active=0;SelectedLayers=new Set([0]);Panel.ResetSelection();Selection=null;Changed();SwitchWorkspace('mask');Report('Empty world. Paint a mask, then select its region with R.');};
Find('#Undo').onclick=()=>{if(!History.length)return;Future.push(JSON.stringify(Layers));Layers=JSON.parse(History.pop());Active=Math.min(Active,Layers.length-1);Selection=null;Changed();};
Find('#HistoryUndo').onclick=()=>Find('#Undo').click();Find('#HistoryRedo').onclick=()=>Find('#Redo').click();
Find('#Redo').onclick=()=>{if(!Future.length)return;History.push(JSON.stringify(Layers));Layers=JSON.parse(Future.pop());Active=Math.min(Active,Layers.length-1);Changed();};
Find('#RenderQuality').onchange=Event=>Volume.dataset.previewWidth=Event.target.value;
Find('#MaskTab').onclick=()=>SwitchWorkspace('mask');Find('#LandscapeTab').onclick=Find('#Build').onclick=()=>SwitchWorkspace('landscape');Find('#Fit').onclick=()=>Fit();Find('#FocusMask').onclick=()=>Fit(true);
document.querySelectorAll('[data-tool]').forEach(Button=>Button.onclick=()=>SelectTool(Button.dataset.tool));
for(const [Selector,Property] of [['#Depth','Depth'],['#Resolution','Resolution'],['#Vertical','Vertical'],['#Caves','Caves']])Find(Selector).onchange=Event=>{Remember();Layer()[Property]=Property==='Caves'?Event.target.checked:Number(Event.target.value);Changed();};
Find('#Depth').oninput=Event=>Find('#DepthRead').textContent=`${Event.target.value} m`;
Find('#Radius').oninput=Event=>Find('#RadiusRead').textContent=`${Event.target.value} m`;
Find('#Wave').oninput=Event=>Find('#WaveRead').textContent=`${Event.target.value} m`;
function Download(Filename,Contents)
{
    const URL=window.URL.createObjectURL(new Blob([JSON.stringify(Contents)],{type:'application/json'})), Anchor=document.createElement('a');Anchor.href=URL;Anchor.download=Filename;Anchor.click();setTimeout(()=>window.URL.revokeObjectURL(URL),1000);
}
Find('#Export').onclick=()=>Download('Landscape.frontier.json',Document());Find('#Import').onclick=()=>Find('#File').click();
Find('#File').onchange=async Event=>
{
    try{const File=Event.target.files[0];if(!File)return;if(File.size>5e6)throw Error('Document exceeds 5 MB');const Imported=ValidateDocument(JSON.parse(await File.text()));Remember();Layers=Imported.Layers;Active=0;SelectedLayers=new Set([0]);Panel.ResetSelection();Selection=null;Changed();Fit();Report('Vector document opened.');}
    catch(Failure){Report(Failure.message);}Event.target.value='';
};
Find('#PreviewBake').onclick=async()=>
{
    if(!Layer().Regions.length){Report('Select and add a region before sampling.');return;}
    const Button=Find('#PreviewBake');Button.disabled=true;Report('Sampling signed-distance bricks at 64 × 64 × 32…');await new Promise(Resolve=>setTimeout(Resolve,30));
    try{const Volumes=Layer().Regions.map(Region=>SampleSparseBricks(Layer(),Region));Download('Landscape.sdf-preview.json',{RequestedDimensions:[Layer().Resolution,Layer().Vertical,Layer().Resolution],Volumes});Report(`Exported ${Volumes.reduce((Total,Entry)=>Total+Entry.Bricks.length,0)} occupied SDF bricks. Preview resolution, not full requested resolution.`);}
    finally{Button.disabled=false;}
};
let Baking=null;
let BakingChunks=[];
function FinishBaking()
{
    if(Baking)Baking.terminate();
    Baking=null;BakingChunks=[];
    Find('#Bake').disabled=false;Find('#BakeProgress').hidden=true;
}
Find('#CancelBake').onclick=()=>{FinishBaking();Report('Volume bake cancelled. No partial file was exported.');};
Find('#Bake').onclick=()=>
{
    if(!Layer().Visible||!Layer().Strokes.length||!Layer().Regions.length){Report('Choose a painted region on a visible layer.');return;}
    if(!window.CompressionStream){Report('This browser does not support streaming gzip compression.');return;}
    FinishBaking();
    const Snapshot=structuredClone(Layer());
    const RegionIndex=Number(Find('#BakeRegion').value);
    const Caption=`${Snapshot.Name} / Region ${RegionIndex+1}`;
    Baking=new Worker(new URL('./VolumeBaking.js',import.meta.url),{type:'module'});
    Find('#Bake').disabled=true;Find('#BakeProgress').hidden=false;
    Find('#BakeMeter').value=0;Find('#BakeRead').textContent='Preparing full-resolution volume…';
    Report(`Baking snapshot: ${Caption}. You can keep editing; this bake uses the captured strokes.`);
    Baking.onmessage=Event=>
    {
        const Message=Event.data;
        if(Message.Category==='chunk')BakingChunks.push(Message.Chunk);
        if(Message.Category==='progress')
        {
            const Progress=Message.Progress;
            Find('#BakeMeter').value=Progress.Covered/Progress.Total;
            Find('#BakeRead').textContent=`${(Progress.Covered/Progress.Total*100).toFixed(1)}% · ${Progress.Bricks.toLocaleString()} sampled bricks · ${(Progress.Bytes/1048576).toFixed(1)} MiB raw`;
        }
        if(Message.Category==='failure'){FinishBaking();Report(`Bake failed: ${Message.Message}`);}
        if(Message.Category==='complete')
        {
            const BlobURL=URL.createObjectURL(new Blob(BakingChunks,{type:'application/gzip'}));
            const Anchor=document.createElement('a');Anchor.href=BlobURL;Anchor.download='Landscape.sdf.gz';Anchor.click();
            setTimeout(()=>URL.revokeObjectURL(BlobURL),10000);
            const Summary=`Baked ${Caption}: ${Message.Dimensions.join(' × ')} cells · ${Message.Progress.Bricks.toLocaleString()} sampled bricks · ${(Message.CompressedBytes/1048576).toFixed(2)} MiB gzip`;
            FinishBaking();Report(Summary);
        }
    };
    Baking.onerror=Event=>{FinishBaking();Report(`Background bake failed: ${Event.message}`);};
    Baking.postMessage({Layer:Snapshot,RegionIndex});
};
window.onkeydown=Event=>
{
    if(['INPUT','SELECT','TEXTAREA'].includes(Event.target.tagName))return;
    if(Event.code==='Space'){Space=true;Event.preventDefault();}
    if((Event.ctrlKey||Event.metaKey)&&Event.key.toLowerCase()==='z'){Event.preventDefault();Find(Event.shiftKey?'#Redo':'#Undo').click();return;}
    const Tools={b:'paint',e:'erase',r:'select',h:'pan'};if(Tools[Event.key])SelectTool(Tools[Event.key]);if(Event.key.toLowerCase()==='f')Fit();if(Event.key==='Escape'){Selection=null;Refresh();Draw();}
};
window.onkeyup=Event=>{if(Event.code==='Space')Space=false;};window.onblur=()=>{Space=false;FinishGesture();};
new ResizeObserver(()=>Draw()).observe(Find('#Viewport'));
function Animate(Seconds){Navigation.Step(Seconds);if(Workspace==='landscape'&&Rasterization)Rasterization.Draw(Camera,Seconds/1000,Number(Find('#Wave').value),Find('#Ocean').checked);requestAnimationFrame(Animate);}
function LoadExamples()
{
    if(!confirm('Replace the document with six preset mask layers? You can undo this. Export first if needed.'))return;
    Remember();Layers=CreatePresetWorld();Active=0;SelectedLayers=new Set([0]);Panel.ResetSelection();Selection=null;
    Changed();SwitchWorkspace('landscape');Fit();Report('Six independent preset mask stacks loaded. Select a mask on the left to inspect its layers; Focus active mask to explore it.');
}
function LoadTerrainExamples()
{
    if(!confirm('Replace this document with eight already-applied terrain generator examples? Undo restores the current document.'))return;
    Remember();Layers=CreateTerrainWorld();Active=0;SelectedLayers=new Set([0]);Panel.ResetSelection();Selection=null;
    Changed();SwitchWorkspace('landscape');Fit(true);Report('Eight applied terrain generators loaded. Select a mask, then Focus active mask to inspect its terrain.');
}
Find('#LoadTerrainExamples').onclick=LoadTerrainExamples;
const Navigation=InitializeCameraNavigation({Canvas:Volume,Camera,RetrieveWorkspace:()=>Workspace});
const Panel=InitializeLayerStackPanel({RetrieveLayers:()=>Layers,RetrieveActive:()=>Active,RetrieveSelected:()=>SelectedLayers,Remember,Changed,Refresh,Report,LoadExamples});
Refresh();Fit();SelectTool('paint');SwitchWorkspace('landscape');Fit(true);requestAnimationFrame(Animate);
