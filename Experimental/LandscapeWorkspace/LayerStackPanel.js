/*==========================================================================================================================================
                                                            LAYERSTACKPANEL.JS
==========================================================================================================================================*/
// 🧩 Mask-owned operation stacks, selected-layer authoring, and rendered preset examples.

import { LandformCatalogue, CreateLandform, CombineModes, InvalidateLandforms } from './LandformStack.js';
import { LandformPresets, RetrievePreset, CreatePresetOperations } from './LandformPresets.js';
import { ValidateDocument } from './DistanceField.js';
import { TerrainGeneratorCodes,TerrainInfluenceDefaults } from './TerrainInfluence.js';
import { StackGlyph,StackAppearance } from './StackGlyphs.js';

export function InitializeLayerStackPanel(API)
{
    const Find=Selector=>document.querySelector(Selector);
    const BoundarySelection=new Map();
    const OperationSelection=new Map();
    let SelectedPreset='desert-mesas';
    let DraggedOperation=null;
    const OperationTitle=Operation=>Operation.Name||LandformCatalogue.find(Entry=>Entry.Type===Operation.Type).Name;
    const RetrieveLayer=()=>API.RetrieveLayers()[API.RetrieveActive()];
    const RetrieveBoundary=()=>Math.min(BoundarySelection.get(API.RetrieveActive())||0,Math.max(0,RetrieveLayer().Regions.length-1));
    const RetrieveRegion=()=>RetrieveLayer().Regions[RetrieveBoundary()];
    const RetrieveOperation=()=>{const Count=(RetrieveLayer().Landforms||[]).length,Index=OperationSelection.get(API.RetrieveActive())??(Count?Count-1:-1);return Index<0?Index:Count?Math.min(Index,Count-1):-1;};
    const PreviewURL=Token=>new URL(`./PresetPreviews/${Token}.png`,import.meta.url).href;
    function Button(Caption,Title,Click)
    {
        const Control=document.createElement('button');Control.textContent=Caption;Control.title=Title;Control.onclick=Click;return Control;
    }
    function PopulateGenerators(Select)
    {
        for(const [Caption,Shapes] of [['Landform generators',false],['Optional SDF structures / cuts',true]])
        {
            const Group=document.createElement('optgroup');Group.label=Caption;
            for(const Entry of [...LandformCatalogue].sort((A,B)=>Number(B.Reference.startsWith('Local'))-Number(A.Reference.startsWith('Local'))).filter(Entry=>['arch','tunnel','stacks','strata'].includes(Entry.Type)===Shapes))
            {const Option=document.createElement('option');Option.value=Entry.Type;Option.textContent=Entry.Name;Group.append(Option);}
            Select.append(Group);
        }
    }
    function Restore(Target,Snapshot)
    {
        for(const Key of Object.keys(Target))delete Target[Key];Object.assign(Target,structuredClone(Snapshot));
    }
    function Modify(Targets,Mutation)
    {
        const Original=Targets.map(Target=>structuredClone(Target));
        let Edited;
        try
        {
            Targets.forEach(Mutation);
            ValidateDocument({Format:'FrontierLandscape/1',Layers:API.RetrieveLayers()});
            Edited=Targets.map(Target=>structuredClone(Target));
            if(JSON.stringify(Original)===JSON.stringify(Edited))return true;
        }
        catch(Failure)
        {
            Targets.forEach((Target,Index)=>Restore(Target,Original[Index]));InvalidateLandforms();API.Report(Failure.message);return false;
        }
        Targets.forEach((Target,Index)=>Restore(Target,Original[Index]));API.Remember();
        Targets.forEach((Target,Index)=>Restore(Target,Edited[Index]));InvalidateLandforms();API.Changed();return true;
    }
    function SelectRegion(Index)
    {
        BoundarySelection.set(API.RetrieveActive(),Index);API.Refresh();
    }
    function SelectOperation(Index)
    {
        OperationSelection.set(API.RetrieveActive(),Index);API.Refresh();
    }
    function ModifyOperation(Index,Property,Setting)
    {
        Modify([RetrieveLayer()],Layer=>Layer.Landforms[Index][Property]=Setting);
    }
    function Reorder(Index,Direction)
    {
        const Next=Index+Direction;
        const Applied=Modify([RetrieveLayer()],Layer=>{[Layer.Landforms[Index],Layer.Landforms[Next]]=[Layer.Landforms[Next],Layer.Landforms[Index]];});
        if(Applied)SelectOperation(Next);
    }
    function RemoveOperation(Index)
    {
        const Applied=Modify([RetrieveLayer()],Layer=>Layer.Landforms.splice(Index,1));
        if(Applied)SelectOperation(Math.max(0,Index-1));
    }
    function MoveOperation(From,To)
    {
        const Applied=Modify([RetrieveLayer()],Layer=>{const [Operation]=Layer.Landforms.splice(From,1);Layer.Landforms.splice(To,0,Operation);});
        if(Applied)SelectOperation(To);
    }
    function DuplicateOperation(Index)
    {
        const Applied=Modify([RetrieveLayer()],Layer=>{const Copy=structuredClone(Layer.Landforms[Index]);Copy.Name=OperationTitle(Copy).slice(0,91)+' copy';Layer.Landforms.splice(Index+1,0,Copy);});
        if(Applied)SelectOperation(Index+1);
    }
    function Span(Class,Text)
    {
        const Caption=document.createElement('span');Caption.className=Class;if(Text!==undefined)Caption.textContent=Text;return Caption;
    }
    function CloseAddMenu()
    {
        Find('#tp-add-layer-menu').hidden=true;Find('#tp-btn-add-layer').setAttribute('aria-expanded','false');
    }
    function RenderOutline(Layer,Container)
    {
        Container.replaceChildren();Container.setAttribute('aria-label',`${Layer.Name} layer stack`);
        const Operations=Layer.Landforms||[],Query=Find('#tp-layer-search-input').value.trim().toLowerCase();
        Find('#tp-layer-count').textContent=Operations.length+2;
        Find('#VisibleMaskCount').textContent=Operations.filter(Operation=>Operation.Enabled).length+2;
        Find('#MutedMaskCount').textContent=Operations.filter(Operation=>!Operation.Enabled).length;
        Find('#StackMaskCaption').textContent=`${Layer.Name} · bottom → top`;
        const Entries=[...Operations.keys()].reverse().map(Index=>({Index,Operation:Operations[Index],Type:Operations[Index].Type,Name:OperationTitle(Operations[Index]),Step:'#'+(Index+2)}));
        Entries.push({Index:-1,Type:'foundation',Name:'Base landform',Step:'#1'}, {Index:-2,Type:'environment',Name:'Environment',Step:'LAST'});
        for(const Entry of Entries)
        {
            const {Index,Operation,Type,Name,Step}=Entry,Environment=Type==='environment',Locked=Index<0,Selected=RetrieveOperation()===Index;
            const Summary=Operation?`${Operation.Mode.toUpperCase()} · ${Operation.Amount} m · seed ${Operation.Seed}`:Environment?'Procedural ocean · global lighting':`Painted mask · ${Layer.Regions.length} regions · ${Layer.Depth} m volume`;
            if(Query&&!`${Name} ${Type} ${Summary}`.toLowerCase().includes(Query))continue;
            const Appearance=StackAppearance(Type),Wrap=document.createElement('div');Wrap.className='tp-layer-item-wrap';Wrap.dataset.index=Index;if(Operation)Wrap.dataset.operationIndex=Index;
            const Row=document.createElement('div');Row.className=`tp-layer-row ${Selected?'selected':''} ${Operation&&!Operation.Enabled?'muted':''} ${Environment?'environment locked-last':''}`;Row.dataset.index=Index;Row.tabIndex=0;Row.draggable=!Locked;
            Row.setAttribute('aria-label',Name);Row.setAttribute('aria-selected',String(Selected));
            const Grip=Span(`tp-drag-grip ${Locked?'locked':''}`);if(Locked)Grip.append(StackGlyph('lock'));else Grip.textContent='⋮⋮';Grip.title=Locked?'Locked pipeline position':'Drag to reorder pipeline (Bottom → Top)';
            const Icon=Span('tp-layer-icon');Icon.dataset.layerType=Type;Icon.style.setProperty('--cat-accent',Appearance.Color);Icon.append(StackGlyph(Appearance.Glyph));
            const Meta=document.createElement('div');Meta.className='tp-layer-meta';const Title=document.createElement('div');Title.className='tp-layer-title-line';
            const NameCaption=Span('tp-layer-name',Name),Number=Span(`tp-step-pill ${Environment?'locked-last':''}`,Step);Number.title=Environment?'Locked last layer — lighting only':`Evaluated at step ${Step} from bottom`;Title.append(NameCaption,Number);
            Meta.append(Title,Span('tp-layer-sub',Summary));
            const Status=Span(`tp-layer-status ${Locked?'locked':Operation.Enabled?'on':'off'}`);Status.append(StackGlyph(Locked?'lock':'check'));Status.title=Locked?'Locked pipeline position':Operation.Enabled?'Layer active':'Layer muted';
            const Controls=document.createElement('div');Controls.className='tp-layer-controls';
            if(Environment)
            {
                const Lock=Span('tp-lock-chip');Lock.append(StackGlyph('lock'),Span('','Locked'));Controls.append(Lock);
            }
            else if(Operation)
            {
                const Mask=Button('','Edit inherited mask',()=>Find('#MaskTab').click());Mask.className='tp-icon-btn mask-on';Mask.append(StackGlyph('mask'));
                const Eye=Button('',Operation.Enabled?'Mute layer':'Enable layer',()=>ModifyOperation(Index,'Enabled',!Operation.Enabled));Eye.className=`tp-icon-btn ${Operation.Enabled?'vis-on':'vis-off'}`;Eye.setAttribute('aria-pressed',String(Operation.Enabled));Eye.append(StackGlyph(Operation.Enabled?'eye':'eye-off'));Controls.append(Mask,Eye);
            }
            Row.append(Grip,Icon,Meta,Status,Controls);Row.onclick=Event=>{if(!Event.target.closest('button'))SelectOperation(Index);};Row.onkeydown=Event=>{if(Event.target===Row&&(Event.key==='Enter'||Event.key===' ')){Event.preventDefault();SelectOperation(Index);}};
            if(Operation)
            {
                Row.ondragstart=Event=>{DraggedOperation={Layer:API.RetrieveActive(),Index};Event.dataTransfer.setData('text/plain',String(Index));Event.dataTransfer.effectAllowed='move';Row.classList.add('dragging');};
                Row.ondragover=Event=>{if(DraggedOperation?.Layer===API.RetrieveActive()){Event.preventDefault();Row.classList.add('drop-target');}};
                Row.ondragleave=()=>Row.classList.remove('drop-target');
                Row.ondragend=()=>{DraggedOperation=null;document.querySelectorAll('.drop-target,.dragging').forEach(Entry=>Entry.classList.remove('drop-target','dragging'));};
                Row.ondrop=Event=>{Event.preventDefault();const From=DraggedOperation;DraggedOperation=null;Row.classList.remove('drop-target');if(From?.Layer===API.RetrieveActive()&&From.Index!==Index)MoveOperation(From.Index,Index);};
            }
            Wrap.append(Row);
            if(Operation)
            {
                const Mask=document.createElement('div');Mask.className=`tp-mask-row ${Selected?'selected':''}`;const Edit=Button('Edit','Edit this mask in the canvas',()=>Find('#MaskTab').click());Edit.className='tp-mask-eye';
                Mask.append(Span('tp-mask-chip','B&W MASK'),Span('tp-mask-desc',`INHERITED · ${Layer.Strokes.length} strokes · ${Layer.Regions.length} bounds`),Edit);Wrap.append(Mask);
            }
            if(Selected)
            {
                const Actions=document.createElement('div');Actions.className=`tp-layer-actions ${Environment?'environment-locked':''}`;
                if(Operation)
                {
                    const Up=Button('↑ Up','Move later in pipeline (up)',()=>Reorder(Index,1));Up.disabled=Index===Operations.length-1;
                    const Down=Button('↓ Down','Move earlier in pipeline (down)',()=>Reorder(Index,-1));Down.disabled=Index===0;
                    const Duplicate=Button('Duplicate','Duplicate layer',()=>DuplicateOperation(Index));Duplicate.disabled=Operations.length>=8;
                    const Remove=Button('Delete','Delete layer',()=>RemoveOperation(Index));Remove.className='danger';Actions.append(Up,Down,Duplicate,Remove);
                }
                else Actions.append(Span('tp-locked-tag',Environment?'Locked · Last layer':'Foundation · Painted mask'));
                Wrap.append(Actions);
            }
            Container.append(Wrap);
        }
        if(!Container.children.length){const Empty=document.createElement('p');Empty.className='tp-card-note';Empty.textContent='No matching layers.';Container.append(Empty);}
    }
    function NumericControl(Container,Index,Operation,Property,Caption,Minimum,Maximum,Step)
    {
        const Control=document.createElement('div');Control.className='tp-control-row';
        const Label=document.createElement('div');Label.className='tp-control-label';Label.textContent=Caption;
        const Amount=document.createElement('div');Amount.className=Property==='Seed'?'tp-seed-bar':'tp-amount';
        const Readout=document.createElement('output');Readout.textContent=Operation[Property]+(['Amount','Blend'].includes(Property)?' m':'');
        const Input=document.createElement('input');Input.type=Property==='Seed'?'number':'range';Input.className=Property==='Seed'?'tp-seed-input':'';Input.min=Minimum;Input.max=Maximum;Input.step=Step;Input.value=Operation[Property];Input.dataset.property=Property;Input.setAttribute('aria-label',Caption);
        const Update=()=>{Readout.textContent=Input.value+(['Amount','Blend'].includes(Property)?' m':'');Amount.style.setProperty('--fill',`${(Input.value-Minimum)/(Maximum-Minimum)*100}%`);};Update();
        Input.oninput=Update;Input.onchange=()=>ModifyOperation(Index,Property,Number(Input.value));Amount.append(Input,Readout);Control.append(Label,Amount);Container.append(Control);
    }
    function RenderInspector()
    {
        const Layer=RetrieveLayer(),Operations=Layer.Landforms||[],Index=RetrieveOperation(),Operation=Operations[Index];
        Find('#ActiveStackName').textContent=Layer.Name;
        Find('#StackScope').textContent=`MASK LAYER STACK · ${Operations.length} operations · ${Layer.Regions.length} boundaries`;
        const Type=Operation?.Type||(Index===-2?'environment':'foundation'),Appearance=StackAppearance(Type);
        Find('#OperationName').value=Operation?OperationTitle(Operation):Index===-2?'Environment':'Base landform';Find('#OperationName').disabled=!Operation;
        Find('#OperationName').onchange=Event=>{const Name=Event.target.value.trim();if(Operation&&Name)ModifyOperation(Index,'Name',Name);else API.Refresh();};
        Find('#StackIdentityGlyph').replaceChildren(StackGlyph(Appearance.Glyph));Find('#StackIdentityGlyph').style.setProperty('--cat-accent',Appearance.Color);
        Find('#InspectorEyebrow').textContent=Operation?'LANDFORM LAYER':Index===-2?'ENVIRONMENT · LOCKED LAST':'BASE LANDFORM · FOUNDATION';
        Find('#InspectorCategory').textContent=Operation?'Landform':Index===-2?'Environment':'Foundation';
        Find('.tp-io-pill.write').textContent=Index===-2?'WRITES · SHADING':'WRITES · SIGNED VOLUME';
        Find('#LayerStackEmpty').hidden=!!Operation;Find('#LayerStackEmpty').textContent=Index===-2?'Global procedural ocean and sky. The environment stays last; edit its controls below.':'Flat foundation at sea level (Y = 0). Depth extends underwater, not above the ocean. Edit strokes and bounds below.';
        Find('#OperationInspector').replaceChildren();
        if(Operation)
        {
            const Container=document.createElement('section');Container.className='tp-property-card';Find('#OperationInspector').append(Container);
            const Heading=document.createElement('h2');Heading.className='selected-operation-heading';
            Heading.textContent=LandformCatalogue.find(Entry=>Entry.Type===Operation.Type).Name;Container.append(Heading);
            const GeneratorLabel=document.createElement('label');GeneratorLabel.className='tp-field-row';GeneratorLabel.textContent='Generator';
            const Generator=document.createElement('select');Generator.className='tp-select';Generator.dataset.property='Type';Generator.setAttribute('aria-label','Active generator');PopulateGenerators(Generator);Generator.value=Operation.Type;
            Generator.onchange=()=>Modify([RetrieveLayer()],Layer=>{Layer.Landforms[Index].Type=Generator.value;Layer.Landforms[Index].Mode=CreateLandform(Generator.value).Mode;});
            GeneratorLabel.append(Generator);Container.append(GeneratorLabel);
            const EnabledLabel=document.createElement('label');EnabledLabel.className='check';
            const Enabled=document.createElement('input');Enabled.type='checkbox';Enabled.checked=Operation.Enabled;Enabled.setAttribute('aria-label','Enable active operation');
            Enabled.onchange=()=>ModifyOperation(Index,'Enabled',Enabled.checked);EnabledLabel.append(Enabled,document.createTextNode('Operation enabled'));Container.append(EnabledLabel);
            const CombineLabel=document.createElement('label');CombineLabel.className='tp-field-row';CombineLabel.textContent='Combine';const Combine=document.createElement('select');Combine.className='tp-select';Combine.setAttribute('aria-label','Combine operation');
            for(const Mode of CombineModes){const Option=document.createElement('option');Option.value=Mode;Option.textContent={union:'Union / add rock',subtract:'Subtract / excavate',intersect:'Intersect / clip',blend:'Blend fields'}[Mode];Combine.append(Option);}
            Combine.value=Operation.Mode;Combine.onchange=()=>ModifyOperation(Index,'Mode',Combine.value);CombineLabel.append(Combine);Container.append(CombineLabel);
            NumericControl(Container,Index,Operation,'Amount','Amplitude / relief',1,500,1);
            NumericControl(Container,Index,Operation,'Scale','Frequency / density',.5,5,.1);
            NumericControl(Container,Index,Operation,'Sharpness','Roughness / detail',0,1,.01);
            NumericControl(Container,Index,Operation,'Azimuth','Azimuth',-180,180,1);
            NumericControl(Container,Index,Operation,'Blend','CSG blending',0,100,1);
            NumericControl(Container,Index,Operation,'Seed','Deterministic seed',0,1000000,1);
            if(TerrainGeneratorCodes[Operation.Type]!==undefined)
            {
                const Radial=document.createElement('section');Radial.className='tp-property-card radial-influence-card';
                const Caption=document.createElement('h2');Caption.textContent='Radial influence';Radial.append(Caption);
                const Settings={...TerrainInfluenceDefaults,...Operation},Preview=document.createElement('div');Preview.className='radial-influence-preview';Preview.setAttribute('role','img');Preview.setAttribute('aria-label','Radial gradient: flat beyond the outer radius');
                Preview.style.setProperty('--influence-opacity',Settings.Influence);Preview.style.setProperty('--influence-radius',`${Settings.InfluenceRadius*45}%`);Preview.style.setProperty('--influence-inner',`${Settings.InfluenceRadius*45*(1-Settings.InfluenceFalloff)}%`);Preview.style.setProperty('--influence-x',`${50+Settings.InfluenceX*45}%`);Preview.style.setProperty('--influence-z',`${50+Settings.InfluenceZ*45}%`);Radial.append(Preview);
                NumericControl(Radial,Index,Settings,'Influence','Influence strength',0,1,.01);
                NumericControl(Radial,Index,Settings,'InfluenceRadius','Radius / boundary half-span',.05,2,.01);
                NumericControl(Radial,Index,Settings,'InfluenceFalloff','Radial edge falloff',.05,1,.01);
                NumericControl(Radial,Index,Settings,'InfluenceX','Centre X offset',-1,1,.01);
                NumericControl(Radial,Index,Settings,'InfluenceZ','Centre Z offset',-1,1,.01);
                const Explanation=document.createElement('p');Explanation.className='tp-card-note';Explanation.textContent='Sea level (Y = 0) → generated relief → smooth radial fade back to sea level. Radius is relative to the shorter boundary half-span; offsets are relative to each boundary. Zero strength bypasses this generator. Caves and explicit SDF structures keep their own CSG.';Radial.append(Explanation);Find('#OperationInspector').append(Radial);
            }
        }
        Find('#AddLandform').disabled=Operations.length>=8;
        const Selected=API.RetrieveLayers().filter((Entry,Ordinal)=>API.RetrieveSelected().has(Ordinal));
        Find('#SelectedScopes').textContent=`Active: ${Layer.Name} · ${Selected.length} selected masks / ${Selected.reduce((Count,Entry)=>Count+Entry.Regions.length,0)} boundaries`;
        Find('#BatchLandform').disabled=!Selected.length;Find('#BatchPreset').disabled=!Selected.length;
        Find('#StackRegion').replaceChildren();
        Layer.Regions.forEach((Region,Ordinal)=>{const Option=document.createElement('option');Option.value=Ordinal;Option.textContent=`Region ${String(Ordinal+1).padStart(2,'0')}`;Find('#StackRegion').append(Option);});
        Find('#StackRegion').value=RetrieveBoundary();Find('#BoundaryEdgeControls').hidden=!RetrieveRegion();
        Find('#EdgeWidth').value=RetrieveRegion()?.Edge||0;Find('#EdgeRead').textContent=(RetrieveRegion()?.Edge||0)+' m';
        Find('#LegacyBoundaryOps').replaceChildren();
        Layer.Regions.forEach((Region,Ordinal)=>
        {
            if(!Region.Landforms?.length)return;
            const Row=document.createElement('div');Row.className='legacy-region';
            const Description=document.createElement('p');Description.textContent=`Imported Region ${Ordinal+1}: ${Region.Landforms.map(Entry=>LandformCatalogue.find(Candidate=>Candidate.Type===Entry.Type).Name).join(', ')}. Preserved after the mask stack.`;
            const Clear=Button('Clear imported boundary modifiers','Remove only these legacy modifiers',()=>Modify([Layer],Target=>{delete Target.Regions[Ordinal].Landforms;}));Row.append(Description,Clear);Find('#LegacyBoundaryOps').append(Row);
        });
        RenderPresetSummary();
    }
    function SelectPreset(Token)
    {
        SelectedPreset=Token;RenderPresetSummary();
    }
    function RenderPresetSummary()
    {
        const Preset=RetrievePreset(SelectedPreset);
        Find('#PresetName').textContent=Preset.Name;Find('#PresetDescription').textContent=Preset.Description;
        Find('#PresetTarget').textContent=`Target: ${RetrieveLayer().Name} only`;
        document.querySelectorAll('[data-preset]').forEach(Card=>Card.classList.toggle('active',Card.dataset.preset===SelectedPreset));
        Find('#PresetDialogTitle').textContent=Preset.Name;Find('#PresetDialogDescription').textContent=Preset.Description;
        Find('#PresetDialogImage').src=PreviewURL(Preset.Token);Find('#PresetDialogImage').alt=Preset.Name+' — actual signed-volume render';
        Find('#PresetDialogScope').textContent=`Apply to active mask: ${RetrieveLayer().Name}. Paint, bounds and edge widths are preserved.`;
        Find('#PresetDialogOperations').replaceChildren();
        [...Preset.Operations].reverse().forEach(Operation=>{const Row=document.createElement('li');Row.textContent=`${LandformCatalogue.find(Entry=>Entry.Type===Operation.Type).Name} · ${Operation.Mode} · ${Operation.Amount} m`;Find('#PresetDialogOperations').append(Row);});
    }
    function ApplyPreset(Append=false,Batch=false)
    {
        const Targets=Batch?API.RetrieveLayers().filter((Layer,Index)=>API.RetrieveSelected().has(Index)):[RetrieveLayer()];
        if(!Targets.length)return;
        const Applied=Modify(Targets,Layer=>{Layer.Landforms=Append?[...(Layer.Landforms||[]),...CreatePresetOperations(SelectedPreset)]:CreatePresetOperations(SelectedPreset);});
        if(Applied)
        {
            OperationSelection.set(API.RetrieveActive(),Math.max(0,(RetrieveLayer().Landforms||[]).length-1));API.Refresh();
            API.Report(`${RetrievePreset(SelectedPreset).Name} ${Append?'appended':'applied'} to ${Batch?Targets.length+' selected mask stacks':RetrieveLayer().Name+' only'}. Paint and boundaries unchanged.`);
        }
    }
    const AddMenu=Find('#tp-add-layer-menu');
    for(const [Caption,Structures] of [['1. Landform & 3D SDF Geometry',false],['2. Optional SDF structures',true]])
    {
        const Group=document.createElement('div');Group.className='tp-menu-group';const Heading=document.createElement('div');Heading.className='tp-menu-heading';Heading.textContent=Caption;Group.append(Heading);
        for(const Entry of LandformCatalogue.filter(Entry=>['arch','tunnel','stacks','strata'].includes(Entry.Type)===Structures))
        {
            const Appearance=StackAppearance(Entry.Type),Add=Button('','Add '+Entry.Name,()=>{Find('#LandformType').value=Entry.Type;CloseAddMenu();Find('#AddLandform').click();});Add.className='tp-menu-item';Add.dataset.addType=Entry.Type;
            const Icon=Span('tp-menu-dot');Icon.style.setProperty('--cat-accent',Appearance.Color);Icon.append(StackGlyph(Appearance.Glyph));const Caption=document.createElement('span');Caption.className='tp-menu-item-body';
            const Title=document.createElement('strong');Title.textContent=Entry.Name;const Description=document.createElement('small');Description.textContent=Structures?'Signed 3D structure / cut':'Procedural mask-bounded signed volume';Caption.append(Title,Description);Add.append(Icon,Caption);Group.append(Add);
        }
        AddMenu.append(Group);
    }
    Find('#tp-btn-add-layer').onclick=()=>{AddMenu.hidden=!AddMenu.hidden;Find('#tp-btn-add-layer').setAttribute('aria-expanded',String(!AddMenu.hidden));};
    document.addEventListener('pointerdown',Event=>{if(!Event.target.closest('.tp-add-menu-anchor'))CloseAddMenu();});
    document.addEventListener('keydown',Event=>{if(Event.key==='Escape')CloseAddMenu();});
    Find('#tp-layer-search-input').oninput=()=>RenderOutline(RetrieveLayer(),Find('#tp-layer-list'));
    PopulateGenerators(Find('#LandformType'));
    for(const Preset of LandformPresets)
    {
        const Card=Button('','Select '+Preset.Name,()=>SelectPreset(Preset.Token));Card.className='preset-card';Card.dataset.preset=Preset.Token;
        const Image=document.createElement('img');Image.src=PreviewURL(Preset.Token);Image.alt=Preset.Name+' volumetric preset';Image.width=144;Image.height=80;
        const Name=document.createElement('strong');Name.textContent=Preset.Name;const Count=document.createElement('small');Count.textContent=Preset.Operations.length+' layers · SDF';Card.append(Image,Name,Count);Card.ondblclick=()=>{SelectPreset(Preset.Token);Find('#PresetDialog').showModal();};Find('#PresetCards').append(Card);
    }
    Find('#AddLandform').onclick=()=>
    {
        const Layer=RetrieveLayer();const Applied=Modify([Layer],Target=>{Target.Landforms||=[];Target.Landforms.push(CreateLandform(Find('#LandformType').value));});
        if(Applied){SelectOperation(Layer.Landforms.length-1);API.Report(`Landform added to ${Layer.Name} mask stack; applies to all ${Layer.Regions.length} boundaries.`);}
    };
    Find('#BatchLandform').onclick=()=>
    {
        const Targets=API.RetrieveLayers().filter((Layer,Index)=>API.RetrieveSelected().has(Index));if(!Targets.length)return;
        const Applied=Modify(Targets,Layer=>{Layer.Landforms||=[];Layer.Landforms.push(CreateLandform(Find('#LandformType').value));});
        if(Applied)API.Report(`Added independent landform layers to ${Targets.length} selected mask stacks.`);
    };
    Find('#StackRegion').onchange=Event=>SelectRegion(Number(Event.target.value));
    Find('#EdgeWidth').oninput=Event=>Find('#EdgeRead').textContent=Event.target.value+' m';
    Find('#EdgeWidth').onchange=Event=>{const Region=RetrieveRegion();if(Region)Modify([Region],Target=>Target.Edge=Number(Event.target.value));};
    Find('#ApplyPreset').onclick=()=>ApplyPreset();Find('#AppendPreset').onclick=()=>ApplyPreset(true);Find('#BatchPreset').onclick=()=>ApplyPreset(false,true);
    Find('#PreviewPreset').onclick=()=>Find('#PresetDialog').showModal();Find('#ClosePresetDialog').onclick=()=>Find('#PresetDialog').close();
    Find('#ApplyDialogPreset').onclick=()=>{ApplyPreset();Find('#PresetDialog').close();};
    Find('#LoadPresetWorld').onclick=()=>API.LoadExamples();
    Find('#TogglePresetLibrary').onclick=()=>{Find('#PresetCards').hidden=!Find('#PresetCards').hidden;Find('#TogglePresetLibrary').textContent=Find('#PresetCards').hidden?'Show saved stacks':'Hide saved stacks';};
    return {Render:RenderInspector,RenderOutline,SelectRegion,RetrieveBoundary,SelectOperation,ResetSelection:()=>{BoundarySelection.clear();OperationSelection.clear();}};
}
