/*==========================================================================================================================================
                                                           LAYERSTACKPANEL.JS
==========================================================================================================================================*/
// 🧩 Explicit mask and boundary ownership, direct generator insertion, ordered operations, and focused property inspection.

import { LandformCatalogue, CreateLandform, InvalidateLandforms, CompileRegion } from './LandformStack.js';
import { ValidateDocument } from './DistanceField.js';
import { TerrainGeneratorCodes, TerrainInfluenceDefaults, TerrainProfile } from './TerrainInfluence.js';
import { StackGlyph, StackAppearance } from './StackGlyphs.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                        GENERATOR DESCRIPTIONS
//------------------------------------------------------------------------------------------------------------------------

const GeneratorDescriptions = {
    fractal: 'Additive octaves · rolling landforms',
    multifractal: 'Multiplicative octaves · varied relief',
    'ridged-multifractal': 'Sharp crests · connected mountain ranges',
    badlands: 'Dense branching gullies and eroded ridges',
    'ridge-noise': 'Single-scale crests and valleys',
    'fractal-ridges': 'Multi-scale ridges · fine branching detail',
    'sand-dunes': 'Directionally warped aeolian dune fields',
    mesa: 'Terraced plateaus with softened shoulders',
    fault: 'Directional escarpment and uplift',
    craters: 'Subtract impact depressions from the volume',
    tunnel: 'Subtract a three-dimensional passage',
    arch: 'Add a solid span with empty space beneath',
    stacks: 'Add bounded rock columns',
    strata: 'Add stratigraphic ledges'
};
const VolumeTypes = new Set(['tunnel', 'arch', 'stacks', 'strata']);
const Find = Selector => document.querySelector(Selector);

function Caption(Tag, Class, Text)
{
    const Control = document.createElement(Tag);
    if (Class) Control.className = Class;
    if (Text !== undefined) Control.textContent = Text;
    return Control;
}

function Button(Text, Title, Glyph, Action, Class = 'icon-button')
{
    const Control = Caption('button', Class, Text || undefined);
    Control.type = 'button';
    Control.title = Title;
    Control.setAttribute('aria-label', Title);
    if (Glyph) Control.prepend(StackGlyph(Glyph));
    Control.onclick = Action;
    return Control;
}

function ProfileSVG(Type)
{
    const Region = { X0: -300, Z0: -300, X1: 300, Z1: 300, Edge: 0 };
    const Layer = { Depth: 120, Landforms: [{ ...CreateLandform(Type), Blend: 0 }] };
    const Primitive = CompileRegion(Layer, Region).Steps[0]?.Primitives[0];
    if (!Primitive || Primitive.Category < 26) return StackGlyph(StackAppearance(Type).Glyph);
    const Points = Array.from({ length: 60 }, (_, Index) =>
    {
        const Fraction = Index / 59;
        const Elevation = TerrainProfile(Primitive, Fraction * 600 - 300, 0, 47);
        return `${(Fraction * 46).toFixed(2)},${(25 - Elevation * 22).toFixed(2)}`;
    });
    const SVG = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    SVG.setAttribute('viewBox', '0 0 46 29');
    SVG.innerHTML = `<path d="M${Points.join(' L')}"/><path d="M0 28H46" opacity=".2"/>`;
    return SVG;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        OWNERSHIP AND EDITING
//------------------------------------------------------------------------------------------------------------------------

export function InitializeLayerStackPanel(API)
{
    const ScopeSelection = new Map(), OperationSelection = new Map(), BoundarySelection = new Map();
    let Category = 'landform', Dragged = null, BatchAddition = false;
    let MenuInitiator = '#AddGenerator';
    const RetrieveLayer = () => API.RetrieveLayers()[API.RetrieveActive()];
    const RetrieveBoundary = () => Math.min(BoundarySelection.get(API.RetrieveActive()) || 0, Math.max(0, RetrieveLayer().Regions.length - 1));
    const RetrieveScope = () =>
    {
        const Index = ScopeSelection.get(API.RetrieveActive()) ?? -1;
        return Index >= 0 && Index < RetrieveLayer().Regions.length ? Index : -1;
    };
    const RetrieveOwner = () => RetrieveScope() < 0 ? RetrieveLayer() : RetrieveLayer().Regions[RetrieveScope()];
    const ScopeToken = () => `${API.RetrieveActive()}:${RetrieveScope()}`;
    const RetrieveOperations = () => RetrieveOwner().Landforms || [];
    const RetrieveOperation = () => Math.min(OperationSelection.get(ScopeToken()) ?? RetrieveOperations().length - 1, RetrieveOperations().length - 1);
    const OperationTitle = Operation => Operation.Name || LandformCatalogue.find(Entry => Entry.Type === Operation.Type).Name;
    const ScopeCaption = () => `${RetrieveLayer().Name}${RetrieveScope() < 0 ? ' · Entire mask' : ` · Region ${RetrieveScope() + 1}`}`;

    function Modify(Mutation, GeometryChanged = true)
    {
        const Original = structuredClone(API.RetrieveLayers());
        try
        {
            Mutation();
            ValidateDocument({ Format: 'FrontierLandscape/1', Layers: API.RetrieveLayers() });
        }
        catch (Failure)
        {
            API.ReplaceLayers(Original);
            InvalidateLandforms();
            API.Report(Failure.message);
            API.Refresh();
            return false;
        }
        const Edited = structuredClone(API.RetrieveLayers());
        if (JSON.stringify(Original) === JSON.stringify(Edited)) return true;
        API.ReplaceLayers(Original);
        API.Remember();
        API.ReplaceLayers(Edited);
        API.Changed(GeometryChanged);
        return true;
    }

    function SelectOperation(Index)
    {
        OperationSelection.set(ScopeToken(), Index);
        API.ActivateInspector('shape');
        API.Refresh();
    }

    function ModifyOperation(Index, Property, Setting)
    {
        return Modify(() => RetrieveOwner().Landforms[Index][Property] = Setting);
    }

    function Reorder(From, To)
    {
        if (To < 0 || To >= RetrieveOperations().length) return;
        if (Modify(() =>
        {
            const Operations = RetrieveOwner().Landforms;
            const [Operation] = Operations.splice(From, 1);
            Operations.splice(To, 0, Operation);
        })) SelectOperation(To);
    }

    function AddGenerator(Type, Batch = false)
    {
        const Targets = Batch ? [...API.RetrieveSelected()].map(Index => API.RetrieveLayers()[Index]) : [RetrieveOwner()];
        if (!Targets.length) return;
        const Applied = Modify(() =>
        {
            for (const Target of Targets)
            {
                Target.Landforms ||= [];
                Target.Landforms.push({ ...CreateLandform(Type), Blend: 0, Seed: 4029 + Target.Landforms.length * 149 });
            }
        });
        if (!Applied) return;
        if (Batch) ScopeSelection.set(API.RetrieveActive(), -1);
        CloseMenu();
        SelectOperation(RetrieveOperations().length - 1);
        API.Report(`${LandformCatalogue.find(Entry => Entry.Type === Type).Name} applied to ${Batch ? `${Targets.length} selected mask stacks` : ScopeCaption()}.`);
    }

//------------------------------------------------------------------------------------------------------------------------
//                                                        STACK ROWS AND CATALOGUE
//------------------------------------------------------------------------------------------------------------------------

    function RenderRows()
    {
        const Container = Find('#LandformRows');
        Container.replaceChildren();
        const Operations = RetrieveOperations();
        const Query = Find('#LayerSearch').value.trim().toLowerCase();
        Find('#OperationCount').textContent = Operations.length;
        Find('#StackOwner').textContent = RetrieveLayer().Name;
        Find('#StackOwnerColour').style.background = RetrieveLayer().Color;
        Find('#BatchLandform').hidden = API.RetrieveSelected().size < 2;
        Find('#BatchLandform').textContent = `Add to ${API.RetrieveSelected().size} selected masks…`;
        const Scope = Find('#StackScope');
        Scope.replaceChildren(new Option('Entire mask', '-1'));
        RetrieveLayer().Regions.forEach((Region, Index) => Scope.add(new Option(`Region ${String(Index + 1).padStart(2, '0')} only`, String(Index))));
        Scope.value = String(RetrieveScope());
        for (let Index = Operations.length - 1; Index >= -1; Index--)
        {
            const Operation = Operations[Index];
            const Title = Operation ? OperationTitle(Operation) : RetrieveScope() < 0 ? 'Sea-level foundation' : 'Inherited mask stack';
            if (Query && !`${Title} ${Operation?.Type || ''}`.toLowerCase().includes(Query)) continue;
            const Row = Caption('div', `landform-row ${Index === RetrieveOperation() ? 'selected' : ''} ${Operation && !Operation.Enabled ? 'muted' : ''} ${Operation ? '' : 'foundation'}`);
            Row.tabIndex = 0;
            Row.dataset.operationIndex = Index;
            Row.setAttribute('role', 'option');
            Row.setAttribute('aria-label', Title);
            Row.setAttribute('aria-selected', String(Index === RetrieveOperation()));
            const Icon = Caption('span', 'landform-icon');
            Icon.append(StackGlyph(Operation ? StackAppearance(Operation.Type).Glyph : 'layers'));
            const Copy = Caption('span', 'landform-copy');
            Copy.append(Caption('strong', '', Title), Caption('small', '', Operation ? `${Index + 1}  /  ${Operation.Mode === 'union' ? 'Union' : Operation.Mode} · ${Operation.Amount} m relief` : RetrieveScope() < 0 ? 'Y = 0 m · thickness below sea' : `${(RetrieveLayer().Landforms || []).length} operations · evaluated first`));
            Row.append(Icon, Copy);
            if (Operation)
            {
                const Eye = Button('', Operation.Enabled ? 'Mute generator' : 'Enable generator', Operation.Enabled ? 'eye' : 'eye-off', () => ModifyOperation(Index, 'Enabled', !Operation.Enabled));
                Eye.setAttribute('aria-pressed', String(Operation.Enabled));
                Eye.onclick = Event => { Event.stopPropagation(); ModifyOperation(Index, 'Enabled', !Operation.Enabled); };
                Row.append(Eye);
                Row.draggable = true;
                Row.ondragstart = Event =>
                {
                    Dragged = { Token: ScopeToken(), Index };
                    Event.dataTransfer.setData('text/plain', String(Index));
                    Event.dataTransfer.effectAllowed = 'move';
                    Row.classList.add('dragging');
                };
                Row.ondragover = Event =>
                {
                    if (Dragged?.Token !== ScopeToken()) return;
                    Event.preventDefault();
                    Row.classList.add('drop-target');
                };
                Row.ondragleave = () => Row.classList.remove('drop-target');
                Row.ondrop = Event =>
                {
                    Event.preventDefault();
                    if (Dragged?.Token === ScopeToken()) Reorder(Dragged.Index, Index);
                    Dragged = null;
                };
                Row.ondragend = () =>
                {
                    Dragged = null;
                    document.querySelectorAll('.dragging,.drop-target').forEach(Entry => Entry.classList.remove('dragging', 'drop-target'));
                };
            }
            else Row.append(StackGlyph('lock'));
            Row.onclick = () => SelectOperation(Index);
            Row.onkeydown = Event =>
            {
                if (Event.target !== Row || !['Enter', ' '].includes(Event.key)) return;
                Event.preventDefault();
                SelectOperation(Index);
            };
            Container.append(Row);
        }
        if (!Operations.length && !Query) Container.prepend(Caption('p', 'empty-stack', 'A flat volume, ready for local relief. Add a generator to begin.'));
    }

    function RenderChoices()
    {
        const Query = Find('#GeneratorSearch').value.trim().toLowerCase();
        const Container = Find('#GeneratorChoices');
        Container.replaceChildren();
        for (const Entry of LandformCatalogue)
        {
            if (!GeneratorDescriptions[Entry.Type]) continue;
            if (VolumeTypes.has(Entry.Type) !== (Category === 'volume')) continue;
            if (Query && !`${Entry.Name} ${GeneratorDescriptions[Entry.Type]}`.toLowerCase().includes(Query)) continue;
            const Choice = Button('', `Apply ${Entry.Name}`, '', () => AddGenerator(Entry.Type, BatchAddition), 'generator-choice');
            Choice.dataset.generator = Entry.Type;
            const Preview = Caption('span', 'generator-profile');
            Preview.append(ProfileSVG(Entry.Type));
            const Copy = Caption('span', 'generator-choice-copy');
            Copy.append(Caption('strong', '', Entry.Name), Caption('small', '', GeneratorDescriptions[Entry.Type]));
            Choice.append(Preview, Copy, StackGlyph('plus'));
            Container.append(Choice);
        }
        if (!Container.children.length) Container.append(Caption('p', 'empty-stack', 'No matching generators.'));
    }

    function OpenMenu(Batch = false)
    {
        BatchAddition = Batch === true;
        MenuInitiator = BatchAddition ? '#BatchLandform' : '#AddGenerator';
        Find('#GeneratorTarget').textContent = BatchAddition ? `${API.RetrieveSelected().size} selected masks · mask-wide` : ScopeCaption();
        Find('#GeneratorMenuCaption').textContent = BatchAddition ? 'ADD TO SELECTED MASK STACKS' : 'ADD TO ACTIVE STACK';
        Find('#GeneratorMenu').hidden = false;
        Find(MenuInitiator).setAttribute('aria-expanded', 'true');
        RenderChoices();
        Find('#GeneratorSearch').focus();
    }

    function CloseMenu()
    {
        Find('#GeneratorMenu').hidden = true;
        Find('#AddGenerator').setAttribute('aria-expanded', 'false');
        Find('#BatchLandform').setAttribute('aria-expanded', 'false');
    }

//------------------------------------------------------------------------------------------------------------------------
//                                                        PROPERTY CONTROLS
//------------------------------------------------------------------------------------------------------------------------

    function NumericControl(Container, Index, Operation, Property, Label, Minimum, Maximum, Step = 1, Units = '')
    {
        const Group = Caption('div', 'control-group');
        const Heading = Caption('label', 'field-caption', Label);
        const Readout = Caption('output', '', `${Operation[Property]}${Units}`);
        const Input = Caption('input');
        Input.type = 'range';
        Input.min = Minimum;
        Input.max = Maximum;
        Input.step = Step;
        Input.value = Operation[Property];
        Input.dataset.property = Property;
        Input.setAttribute('aria-label', Label);
        Input.style.setProperty('--progress', `${(Operation[Property] - Minimum) / (Maximum - Minimum) * 100}%`);
        Heading.append(Readout);
        Input.oninput = () =>
        {
            Readout.textContent = `${Input.value}${Units}`;
            Input.style.setProperty('--progress', `${(Input.value - Minimum) / (Maximum - Minimum) * 100}%`);
        };
        Input.onchange = () => ModifyOperation(Index, Property, Number(Input.value));
        Group.append(Heading, Input);
        Container.append(Group);
    }

    function InfluencePreview(Operation, Index)
    {
        const Settings = { ...TerrainInfluenceDefaults, ...Operation };
        const SVG = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
        SVG.setAttribute('viewBox', '0 0 220 140');
        SVG.classList.add('influence-preview');
        SVG.setAttribute('role', 'img');
        SVG.setAttribute('aria-label', 'Radial influence. Drag to move the centre.');
        const X = 110 + Settings.InfluenceX * 82, Z = 70 + Settings.InfluenceZ * 52;
        const Radius = Settings.InfluenceRadius * 54;
        SVG.innerHTML = `<defs><radialGradient id="InfluenceGradient"><stop offset="${Math.max(0, (1 - Settings.InfluenceFalloff) * 100)}%" stop-color="#c8c8c8" stop-opacity="${Settings.Influence * .35}"/><stop offset="100%" stop-color="#c8c8c8" stop-opacity="0"/></radialGradient></defs>
            <path class="guide" d="M0 35H220M0 70H220M0 105H220M55 0V140M110 0V140M165 0V140"/>
            <rect x="28" y="18" width="164" height="104" fill="none" stroke="#606060" stroke-width=".5" stroke-dasharray="3 3"/>
            <circle cx="${X}" cy="${Z}" r="${Radius}" fill="url(#InfluenceGradient)"/>
            <circle class="contour" opacity=".22" cx="${X}" cy="${Z}" r="${Radius}"/>
            <circle class="contour" opacity=".32" cx="${X}" cy="${Z}" r="${Radius * .62}"/>
            <path stroke="#bdbdbd" stroke-width=".6" d="M${X - 7} ${Z}h14M${X} ${Z - 7}v14"/>
            <circle class="centre" cx="${X}" cy="${Z}" r="2.8"/><text x="33" y="116">MASK REGION</text>`;
        let Centre = null;
        SVG.onpointerdown = Event =>
        {
            SVG.setPointerCapture(Event.pointerId);
            SVG.onpointermove(Event);
        };
        SVG.onpointermove = Event =>
        {
            if (!SVG.hasPointerCapture(Event.pointerId)) return;
            const Bounds = SVG.getBoundingClientRect();
            const CoordinateX = (Event.clientX - Bounds.left) / Bounds.width * 220;
            const CoordinateZ = (Event.clientY - Bounds.top) / Bounds.height * 140;
            Centre = [Math.max(-1, Math.min(1, (CoordinateX - 110) / 82)), Math.max(-1, Math.min(1, (CoordinateZ - 70) / 52))];
            SVG.querySelector('.centre').setAttribute('cx', 110 + Centre[0] * 82);
            SVG.querySelector('.centre').setAttribute('cy', 70 + Centre[1] * 52);
        };
        SVG.onpointerup = () =>
        {
            if (!Centre) return;
            Modify(() =>
            {
                RetrieveOwner().Landforms[Index].InfluenceX = Math.round(Centre[0] * 100) / 100;
                RetrieveOwner().Landforms[Index].InfluenceZ = Math.round(Centre[1] * 100) / 100;
            });
        };
        SVG.onpointercancel = () => { Centre = null; API.Refresh(); };
        return SVG;
    }

    function RenderInspector()
    {
        const Index = RetrieveOperation(), Operation = RetrieveOperations()[Index];
        const MaskWorkspace = API.RetrieveWorkspace() === 'mask';
        const Ocean = API.RetrieveInspector() === 'ocean';
        const Type = Ocean ? 'environment' : MaskWorkspace ? 'mask' : Operation?.Type || 'foundation';
        Find('#LayerTitle').textContent = RetrieveLayer().Name;
        Find('#ActiveMaskColour').style.background = RetrieveLayer().Color;
        Find('#StackIdentityGlyph').replaceChildren(StackGlyph(MaskWorkspace ? 'mask' : StackAppearance(Type).Glyph));
        Find('#InspectorEyebrow').textContent = Ocean ? 'WORLD ENVIRONMENT' : MaskWorkspace ? 'VECTOR MASK' : Operation ? 'TERRAIN GENERATOR' : 'SEA-LEVEL FOUNDATION';
        Find('#OperationName').value = Ocean ? 'Open water' : MaskWorkspace ? RetrieveLayer().Name : Operation ? OperationTitle(Operation) : 'Flat foundation';
        Find('#OperationName').disabled = Ocean || (!MaskWorkspace && !Operation);
        Find('#OperationName').onchange = Event =>
        {
            const Name = Event.target.value.trim();
            if (!Name) { API.Refresh(); return; }
            if (MaskWorkspace) Modify(() => RetrieveLayer().Name = Name);
            else ModifyOperation(Index, 'Name', Name);
        };
        Find('#OperationSubtitle').textContent = Ocean ? 'Textureless · world-space spectrum' : MaskWorkspace ? `${RetrieveLayer().Strokes.length} strokes · resolution independent` : RetrieveScope() < 0 ? 'Entire mask · local influence' : `Region ${RetrieveScope() + 1} only · isolated volume`;
        Find('#InspectorScope').textContent = Ocean ? 'World environment' : MaskWorkspace ? 'Active mask' : 'Active landform';
        const Container = Find('#OperationInspector');
        Container.replaceChildren();
        if (!Operation)
        {
            const Card = Caption('section', 'property-card');
            Card.append(Caption('h3', '', RetrieveScope() < 0 ? 'Flat, not elevated' : 'Region-scoped modifiers'));
            const Metric = Caption('div', 'metric', '0');
            Metric.append(Caption('small', '', 'm'));
            Card.append(Metric, Caption('p', 'card-note', RetrieveScope() < 0 ? 'The foundation meets mean sea level. Depth extends downwards. Add a generator for local relief, with no platform underneath.' : 'This boundary inherits the mask stack. Generators added here affect only this region.'));
            Card.append(Button('Add generator', 'Add terrain generator', 'plus', OpenMenu, 'border-button full-width'));
            Container.append(Card);
            return;
        }
        const Card = Caption('section', 'property-card');
        const Heading = Caption('h3', '', 'Relief & character');
        Heading.prepend(StackGlyph(StackAppearance(Operation.Type).Glyph));
        const Enabled = Caption('label', 'switch');
        const Toggle = Caption('input');
        Toggle.type = 'checkbox';
        Toggle.checked = Operation.Enabled;
        Toggle.setAttribute('aria-label', 'Enable active generator');
        Toggle.onchange = () => ModifyOperation(Index, 'Enabled', Toggle.checked);
        Enabled.append(Toggle, Caption('span'));
        Heading.append(Enabled);
        const Metric = Caption('div', 'metric');
        const Amplitude = Caption('input');
        Amplitude.type = 'number';
        Amplitude.min = 1;
        Amplitude.max = 500;
        Amplitude.value = Operation.Amount;
        Amplitude.setAttribute('aria-label', 'Relief in metres');
        Amplitude.onchange = () => ModifyOperation(Index, 'Amount', Number(Amplitude.value));
        Metric.append(Amplitude, Caption('small', '', 'm'));
        Card.append(Heading, Metric, Caption('p', 'muted', 'Relief above the sea-level foundation'));
        NumericControl(Card, Index, Operation, 'Amount', 'Amplitude', 1, 500, 1, ' m');
        NumericControl(Card, Index, Operation, 'Scale', 'Frequency', .5, 5, .05);
        NumericControl(Card, Index, Operation, 'Sharpness', 'Roughness', 0, 1, .01);
        NumericControl(Card, Index, Operation, 'Azimuth', 'Azimuth', -180, 180, 1, '°');
        Container.append(Card);

        if (TerrainGeneratorCodes[Operation.Type] !== undefined)
        {
            const InfluenceCard = Caption('section', 'property-card');
            InfluenceCard.append(Caption('h3', '', 'Spatial influence'), InfluencePreview(Operation, Index));
            const PreviewCaption = Caption('p', 'influence-caption', 'Radial falloff');
            PreviewCaption.append(Caption('span', '', 'Drag to position'));
            InfluenceCard.append(PreviewCaption);
            const Settings = { ...TerrainInfluenceDefaults, ...Operation };
            NumericControl(InfluenceCard, Index, Settings, 'Influence', 'Strength', 0, 1, .01);
            NumericControl(InfluenceCard, Index, Settings, 'InfluenceRadius', 'Radius', .05, 2, .01);
            NumericControl(InfluenceCard, Index, Settings, 'InfluenceFalloff', 'Falloff', .05, 1, .01);
            NumericControl(InfluenceCard, Index, Settings, 'InfluenceX', 'Centre X', -1, 1, .01);
            NumericControl(InfluenceCard, Index, Settings, 'InfluenceZ', 'Centre Z', -1, 1, .01);
            InfluenceCard.append(Caption('p', 'card-note', 'Outside the influence the volume stays flat at sea level. Coast feather also fades relief at the painted shoreline.'));
            Container.append(InfluenceCard);
        }
        const Combination = Caption('section', 'property-card');
        Combination.append(Caption('h3', '', 'Stack evaluation'));
        const CombinationLabel = Caption('label', 'field-row', 'Combine');
        const Mode = Caption('select');
        Mode.setAttribute('aria-label', 'Generator combine mode');
        for (const [Token, Text] of [['union', 'Maximum · union'], ['subtract', 'Carve · subtract'], ['intersect', 'Minimum · intersect'], ['blend', 'Average · blend']]) Mode.add(new Option(Text, Token));
        Mode.value = Operation.Mode;
        Mode.onchange = () => ModifyOperation(Index, 'Mode', Mode.value);
        CombinationLabel.append(Mode);
        Combination.append(CombinationLabel, Caption('p', 'layer-combination-note', 'Generators share the same sea-level origin. Union keeps the larger relief; it does not stack raised slabs.'));
        NumericControl(Combination, Index, Operation, 'Blend', 'Smooth union width', 0, 100, 1, ' m');
        const SeedLabel = Caption('label', 'field-row', 'Seed');
        const Seed = Caption('input');
        Seed.type = 'number';
        Seed.min = 0;
        Seed.max = 1000000;
        Seed.value = Operation.Seed;
        Seed.setAttribute('aria-label', 'Generator seed');
        Seed.onchange = () => ModifyOperation(Index, 'Seed', Number(Seed.value));
        SeedLabel.append(Seed, Button('', 'Choose a different seed', 'reset', () => ModifyOperation(Index, 'Seed', (Operation.Seed + 7919) % 1000001)));
        Combination.append(SeedLabel);
        const Actions = Caption('div', 'operation-actions');
        const Up = Button('', 'Move generator later', 'arrow-up', () => Reorder(Index, Index + 1));
        const Down = Button('', 'Move generator earlier', 'arrow-down', () => Reorder(Index, Index - 1));
        Up.disabled = Index === RetrieveOperations().length - 1;
        Down.disabled = Index === 0;
        const Duplicate = Button('Duplicate', 'Duplicate generator', 'copy', () =>
        {
            if (Modify(() =>
            {
                const Copy = structuredClone(RetrieveOwner().Landforms[Index]);
                Copy.Name = `${OperationTitle(Copy).slice(0, 90)} copy`;
                RetrieveOwner().Landforms.splice(Index + 1, 0, Copy);
            })) SelectOperation(Index + 1);
        }, 'quiet-button');
        const Remove = Button('Delete', 'Delete generator', 'trash', () =>
        {
            if (Modify(() => RetrieveOwner().Landforms.splice(Index, 1))) SelectOperation(Math.max(-1, Index - 1));
        }, 'quiet-button danger');
        Actions.append(Up, Down, Duplicate, Remove);
        Combination.append(Actions);
        Container.append(Combination);
    }

//------------------------------------------------------------------------------------------------------------------------
//                                                        CONTROL ROUTING
//------------------------------------------------------------------------------------------------------------------------

    Find('#AddGenerator').onclick = () => OpenMenu();
    Find('#CloseGeneratorMenu').onclick = () => { CloseMenu(); Find(MenuInitiator).focus(); };
    Find('#LayerSearch').oninput = RenderRows;
    Find('#GeneratorSearch').oninput = RenderChoices;
    Find('#BatchLandform').onclick = () => OpenMenu(true);
    Find('#StackScope').onchange = Event =>
    {
        ScopeSelection.set(API.RetrieveActive(), Number(Event.target.value));
        if (RetrieveScope() >= 0) BoundarySelection.set(API.RetrieveActive(), RetrieveScope());
        CloseMenu();
        API.Refresh();
    };
    document.querySelectorAll('[data-generator-category]').forEach(Control => Control.onclick = () =>
    {
        Category = Control.dataset.generatorCategory;
        document.querySelectorAll('[data-generator-category]').forEach(Entry => Entry.classList.toggle('active', Entry === Control));
        RenderChoices();
    });
    document.addEventListener('pointerdown', Event =>
    {
        if (!Find('#GeneratorMenu').hidden && !Event.target.closest('#GeneratorMenu,#AddGenerator,#BatchLandform')) CloseMenu();
    });
    document.addEventListener('keydown', Event =>
    {
        if (Event.key === 'Escape' && !Find('#GeneratorMenu').hidden)
        {
            Event.preventDefault();
            Event.stopImmediatePropagation();
            CloseMenu();
            Find(MenuInitiator).focus();
        }
    }, true);

    return {
        Render: () => { RenderRows(); RenderInspector(); },
        RetrieveBoundary,
        RetrieveSelection: () => ({ Scope: [...ScopeSelection], Operation: [...OperationSelection], Boundary: [...BoundarySelection] }),
        RestoreSelection: Selection =>
        {
            for (const [Destination, Entries] of [[ScopeSelection, Selection.Scope], [OperationSelection, Selection.Operation], [BoundarySelection, Selection.Boundary]])
            {
                Destination.clear();
                for (const [Token, Index] of Entries) Destination.set(Token, Index);
            }
            CloseMenu();
        },
        SelectBoundary: Index => { BoundarySelection.set(API.RetrieveActive(), Index); API.Refresh(); },
        ResetSelection: () => { ScopeSelection.clear(); BoundarySelection.clear(); OperationSelection.clear(); CloseMenu(); },
        ResetBoundary: () => { ScopeSelection.set(API.RetrieveActive(), -1); BoundarySelection.set(API.RetrieveActive(), 0); },
        OpenMenu,
        CloseMenu,
        Modify
    };
}
