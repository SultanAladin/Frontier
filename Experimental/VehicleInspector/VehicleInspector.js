/*==========================================================================================================================================
                                                       VEHICLEINSPECTOR.JS
==========================================================================================================================================*/
// 🧩 The vehicle inspector prototype: the scene outliner, the tyre layout, the vehicle record and its editable curves.
'use strict';

(() =>
{
    const C       = InspectorControls;
    const Catalog = VehicleCatalogue;
    // 📝 The live dock is a companion script. The inspector keeps working if it is missing, so a build never depends on it.
    const Dock    = typeof VehicleTelemetry !== 'undefined' ? VehicleTelemetry : { Detach() {}, Construct() {} };

    const Specification = Catalog.ConfigureVehicleSpecification();
    const Profiles      = Catalog.ConfigureCurveProfiles();
    const Tyres         = Catalog.ConfigureTyreRecords();

    // 📝 The baseline is kept for Reset; the live objects above are what every control reads and writes.
    const Baseline = JSON.parse(JSON.stringify({ Specification, Profiles, Tyres }));

    let UnitSystem      = 'Metric';
    let InspectorMode   = 'Inspector';
    let SelectedRecord  = 'Vehicle';
    let SelectedCurveKey = 'EngineTorque';
    let SelectedTyreId  = 'TyreFrontLeft';
    const PointSelections = { EngineTorque: 4, BrakeTorque: 1, CoolantTemperature: 2, OilTemperature: 3, TyreSurfaceTemperature: 3 };
    let InspectorScroller = null;
    const Sections = [];
    const LogEntries = [];
    let RevisionCount = 0;
    let LastRevisionSignature = JSON.stringify([Specification, Profiles, Tyres]);

    //------------------------------------------------------------------------------------------------------------------------
    //                                                       HELPERS
    //------------------------------------------------------------------------------------------------------------------------

    const ResolveProfile = Key => Profiles.find(Profile => Profile.Key === Key);
    const ResolveTyre    = Id => Tyres.find(Tyre => Tyre.Id === Id);
    const EnclosedTyres  = () => Tyres.filter(Tyre => Tyre.Enclosed);
    const DetachedTyres  = () => Tyres.filter(Tyre => !Tyre.Enclosed);
    const Format         = (Quantity, UnitKey, Digits) => Catalog.FormatQuantity(Quantity, Digits, UnitKey, UnitSystem);
    const UnitSymbol     = UnitKey => Catalog.ResolveUnitSymbol(UnitKey, UnitSystem);
    const DisplayText    = (Quantity, UnitKey, Digits) => Format(Quantity, UnitKey, Digits).Text;

    // 📝 UnitKey, bounds and step may be functions: the point fields follow whichever curve is selected.
    const Bound = Quantity => (typeof Quantity === 'function' ? Quantity() : Quantity);

    function QuantityRow(Parent, Binding)
    {
        const UnitKey = () => Bound(Binding.UnitKey);
        C.SliderRow(Parent, {
            Label: Binding.Label, Tags: Binding.Tags, Digits: Binding.Digits ?? 0, Quantity: UnitKey,
            Minimum: () => Bound(Binding.Minimum), Maximum: () => Bound(Binding.Maximum), Step: () => Bound(Binding.Step),
            Unit: () => Catalog.ResolveUnitSymbol(UnitKey(), UnitSystem),
            Convert: Quantity => (Binding.Convert ? Binding.Convert(Quantity) : Catalog.ConvertQuantity(Quantity, UnitKey(), UnitSystem)),
            Decode: Quantity => (Binding.Decode ? Binding.Decode(Quantity) : Catalog.DecodeQuantity(Quantity, UnitKey(), UnitSystem)),
            Read: Binding.Read, Write: Binding.Write, Labels: Binding.Labels, Availability: Binding.Availability
        });
    }

    function QuantityTile(Grid, Binding)
    {
        C.ReadoutTile(Grid, {
            Label: Binding.Label, Glyph: Binding.Glyph, Quantity: Binding.UnitKey,
            Resolve: () =>
            {
                const Reading = Format(Binding.Read(), Binding.UnitKey, Binding.Digits ?? 0);
                return [Reading.Text, Reading.Unit, Binding.Tone ? Binding.Tone() : null];
            }
        });
    }

    function QuantityReadout(Parent, Binding)
    {
        const Value    = C.Construct('div', 'stat-cell-value');
        const Label    = C.Construct('span', 'stat-cell-label');
        const Cell     = C.Construct('div', 'stat-cell', [Value, Label]);
        Parent.append(Cell);
        C.Register(() =>
        {
            const UnitKey = Bound(Binding.UnitKey);
            const Reading = Format(Binding.Read(), UnitKey, Binding.Digits ?? 0);
            Value.textContent = '';
            Value.append(document.createTextNode(Reading.Text), C.Construct('small', null, Reading.Unit));
            Label.textContent = Binding.Label;
            Cell.dataset.quantity = UnitKey;
            Value.className = 'stat-cell-value' + (Binding.Tone && Binding.Tone() ? ' tone-' + Binding.Tone() : '');
        });
        return Cell;
    }

    function Bind(Container, Key, Extra)
    {
        return Object.assign({
            Read: () => Container[Key],
            Write: Quantity => { Container[Key] = Quantity; }
        }, Extra);
    }

    function SampleProfile(Profile, Count)
    {
        const Output = [];
        for (let Index = 0; Index <= Count; ++Index)
        {
            const X = Profile.X.Minimum + (Profile.X.Maximum - Profile.X.Minimum) * Index / Count;
            Output.push([X, Catalog.EvaluateCurve(Profile, X, Specification, Profiles)]);
        }
        return Output;
    }

    // 📝 One performance estimate per state; the launch integration is the expensive readout and every card asks for it.
    let RetainedPerformance = null, RetainedSignature = '';
    function ResolvePerformance()
    {
        const Signature = JSON.stringify([Specification, Profiles, Tyres.map(Tyre => [Tyre.Compound, Tyre.Wear, Tyre.Enclosed])]);
        if (Signature !== RetainedSignature)
        {
            RetainedSignature = Signature;
            RetainedPerformance = Catalog.EstimatePerformance(Specification, Profiles, Tyres);
        }
        return RetainedPerformance;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    LOG STREAM
    //------------------------------------------------------------------------------------------------------------------------

    function AppendLog(Channel, Message, Detail, Severity, Key)
    {
        LogEntries.push({ Channel, Message, Detail, Severity: Severity || 'live', Key, When: new Date().toLocaleTimeString('en-GB', { hour12: false }) });
        while (LogEntries.length > 200) LogEntries.shift();
        RefreshLog();
    }

    function RefreshLog()
    {
        const Content = document.getElementById('LogContent');
        if (!Content) return;
        Content.textContent = '';
        for (const Entry of LogEntries.slice().reverse())
        {
            const Severity = C.Construct('span', 'severity' + (Entry.Severity === 'warm' ? ' warm' : ''));
            Content.append(C.Construct('div', 'log-row', [
                Severity, C.Construct('span', 'when', Entry.When), C.Construct('span', 'channel', Entry.Channel),
                C.Construct('div', 'message', [C.Construct('b', null, Entry.Message + ' '), C.Construct('span', null, Entry.Detail || '')])
            ]));
        }
        const Count = document.getElementById('LogCount');
        if (Count) Count.textContent = `${LogEntries.length} entries`;
    }

    function ConstructLogStream()
    {
        const Head = document.getElementById('LogHead');
        const Pause = C.Construct('div', 'round-button', C.ConstructGlyph('Eye'));
        const Clear = C.Construct('div', 'round-button', C.ConstructGlyph('Close'));
        Pause.title = 'Pause';
        Clear.title = 'Clear';
        Clear.addEventListener('click', () => { LogEntries.length = 0; RefreshLog(); });
        Head.append(C.Construct('div', 'log-title', 'Log Stream'), C.Construct('span', 'live-pill', [C.Construct('i'), C.Construct('span', null, 'LIVE')]),
                    C.Construct('span', 'spacer'), C.Construct('span', 'log-count'), Pause, Clear);
        C.SmoothScroll(document.getElementById('LogScroll'), document.getElementById('LogContent'));
        AppendLog('SCENE', "Loaded 'Default.levelspace'", '10 records · vehicle added', 'live');
        AppendLog('VEHICLE', 'GT-04 placed in the scene', 'chassis only: four tyres, no other geometry', 'live');
        AppendLog('CURVES', 'Six curve profiles read', 'five editable, engine power derived', 'live');
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                       OUTLINER
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 The scene as the owner described it: the environment records stay, one Vehicle record arrives, and its four tyre
    //    records sit beneath it only while the vehicle encloses them. A detached tyre drops into the pit lane group.
    const EnvironmentRows = Catalog.EnvironmentRows;

    function ConstructOutliner(Content)
    {
        const Outliner = C.Construct('div', 'outliner');
        const RecordCount = C.Construct('b', null, '10');
        const EnclosedCount = C.Construct('b', null, '4');
        const DetachedCount = C.Construct('b', null, '0');
        Outliner.append(
            C.Construct('div', 'outliner-heading', [
                C.Construct('div', null, [C.Construct('h1', null, 'Outliner'), C.Construct('div', 'muted', 'Default.levelspace · 10 records')]),
                C.Construct('div', 'spacer'), C.Construct('div', 'round-button', C.ConstructGlyph('Sliders'))]),
            C.Construct('div', 'count-cards', [
                C.Construct('div', 'count-card', [C.Construct('div', 'chip-glyph live', C.ConstructGlyph('Vehicle')), C.Construct('div', 'label', 'Vehicles'),
                                                 C.Construct('div', 'number', '1')]),
                C.Construct('div', 'count-card', [C.Construct('div', 'chip-glyph dim', C.ConstructGlyph('Tyre')), C.Construct('div', 'label', 'Tyres'),
                                                 C.Construct('div', 'number', '4')])]),
            C.Construct('div', 'search-row', [
                C.Construct('div', 'search-pill', [C.ConstructGlyph('Search'), C.Construct('span', null, 'Search'), C.Construct('kbd', null, 'Ctrl+F')]),
                C.Construct('div', 'filter-pill', [C.ConstructGlyph('Sliders'), C.Construct('span', null, 'Filter')])]));

        const Group = (Title, Count) =>
        {
            const Section = C.Construct('div', 'tree-group');
            const CountElement = C.Construct('span', 'count', Count);
            Section.append(C.Construct('div', 'tree-group-title', [C.ConstructGlyph('Chevron'), C.Construct('span', null, Title), CountElement]));
            Outliner.append(Section);
            return { Section, Count: CountElement };
        };

        const Row = (Options) =>
        {
            const Meta = C.Construct('div', 'row-meta');
            const Badge = C.Construct('div', 'row-state');
            const Glyph = C.Construct('div', 'row-glyph', C.ConstructGlyph(Options.Glyph));
            Glyph.style.color = Options.Colour || '#bcbcbc';
            Glyph.style.background = (Options.Colour || '#bcbcbc') + '1f';
            const Element = C.Construct('div', 'tree-row' + (Options.Nested ? ' nested' : ''), [
                Glyph, C.Construct('div', 'row-text', [C.Construct('div', 'row-name', Options.Name), Meta]), Badge
            ]);
            Element.addEventListener('click', () => SelectRecord(Options.Id));
            C.Register(() =>
            {
                Meta.textContent = Options.Meta ? Options.Meta() : '';
                Element.classList.toggle('selected', SelectedRecord === Options.Id);
                Badge.textContent = '';
                Badge.append(C.ConstructGlyph(Options.BadgeGlyph ? Options.BadgeGlyph() : 'Check'));
                Badge.className = 'row-state' + (Options.BadgeTone && Options.BadgeTone() === 'warm' ? ' warm' : '');
            });
            return Element;
        };

        // ① Rendering
        const Rendering = Group('Rendering', '1');
        Rendering.Section.append(Row({ Id: 'RenderSettings', Name: 'Render settings', Glyph: 'Monitor', Colour: '#9fb4d6',
                                      Meta: () => 'RTX · 1512×945', Nested: false }));

        // ② Environment
        const Environment = Group('Environment', String(EnvironmentRows.length));
        for (const Entry of EnvironmentRows)
            Environment.Section.append(Row({ Id: Entry.Id, Name: Entry.Name, Glyph: Entry.Glyph, Colour: Entry.Colour, Meta: () => Entry.Meta }));

        // ③ Vehicle and its enclosed tyres
        const VehicleGroup = Group('Vehicle', '1 + 4');
        VehicleGroup.Section.append(Row({
            Id: 'Vehicle', Name: 'Vehicle · GT-04', Glyph: 'Vehicle', Colour: '#9fb4d6',
            Meta: () => `${DisplayText(Specification.Identity.KerbMass, 'Mass', 0)} ${UnitSymbol('Mass')} · `
                + `${EnclosedTyres().length} tyres enclosed`,
            BadgeGlyph: () => 'Check'
        }));

        // ④ Pit lane, which only exists while a tyre is detached
        const PitGroup = Group('Pit lane', '0');
        PitGroup.Section.classList.add('pit-group');
        const PitNote = C.Construct('div', 'merge-note', [
            C.Construct('b', null, 'Pit stop. '),
            document.createTextNode('A detached tyre keeps its record and leaves the vehicle enclosure; fitting a set writes it back.')
        ]);
        PitGroup.Section.append(PitNote);

        const TyreRows = new Map();
        for (const Tyre of Tyres)
        {
            const Element = Row({
                Id: Tyre.Id, Nested: true,
                Name: `Tyre · ${Tyre.Code}`,
                Glyph: 'Tyre',
                Colour: () => Catalog.ResolveCompound(Tyre.Compound).Colour,
                Meta: () => `${Tyre.Compound} · ${DisplayText(Tyre.Pressure, 'Pressure', 0)} ${UnitSymbol('Pressure')} · `
                    + `${DisplayText(Tyre.SurfaceTemperature, 'Temperature', 0)} ${UnitSymbol('Temperature')}`,
                BadgeGlyph: () => Tyre.Enclosed ? 'Check' : 'Flag',
                BadgeTone: () => Tyre.Enclosed ? 'live' : 'warm'
            });
            Element.querySelector('.row-glyph').style.color = Catalog.ResolveCompound(Tyre.Compound).Colour;
            TyreRows.set(Tyre.Id, Element);
        }

        // 📝 Rows move between the two groups on refresh; nothing is rebuilt, so registration and scroll survive.
        C.Register(() =>
        {
            for (const Tyre of Tyres)
            {
                const Element = TyreRows.get(Tyre.Id);
                const Target = Tyre.Enclosed ? VehicleGroup.Section : PitGroup.Section;
                if (Element.parentElement !== Target) Target.insertBefore(Element, Target.querySelector('.merge-note'));
                Element.querySelector('.row-glyph').style.background = Catalog.ResolveCompound(Tyre.Compound).Colour + '1f';
            }
            RecordCount.textContent = String(6 + Tyres.length);
            EnclosedCount.textContent = String(EnclosedTyres().length);
            DetachedCount.textContent = String(DetachedTyres().length);
            VehicleGroup.Count.textContent = `1 + ${EnclosedTyres().length}`;
            PitGroup.Count.textContent = String(DetachedTyres().length);
            PitGroup.Section.classList.toggle('empty', DetachedTyres().length === 0);
        });

        Content.append(Outliner);
        C.SmoothScroll(document.getElementById('OutlinerScroll'), Content);

        const StatusRecords = C.Construct('b', null, '10');
        C.Register(() => { StatusRecords.textContent = String(6 + Tyres.length); });
        const Status = document.getElementById('OutlinerStatus');
        Status.append(
            C.Construct('div', 'status-cell', [C.Construct('span', null, 'Records'), StatusRecords]),
            C.Construct('div', 'status-cell', [C.Construct('span', null, 'Enclosed'), EnclosedCount]),
            C.Construct('div', 'status-cell', [C.Construct('span', null, 'Detached'), DetachedCount]),
            C.Construct('span', 'spacer'),
            C.Construct('div', 'status-cell', [C.Construct('span', null, 'Scene'), C.Construct('b', null, 'Default.levelspace')]));
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                   SCENE SCHEMATIC
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 There is no C++ capture of a vehicle yet, so the viewport draws the authored layout instead of pretending to render
    //    one: wheelbase, track, tyre width and compound come from the same records the inspector edits.
    const TyreHits = [];

    function RasterizeScene()
    {
        const Canvas = document.getElementById('ViewportCanvas');
        if (!Canvas) return;
        const Surface = Canvas.parentElement;
        const Width = Math.max(240, Surface.clientWidth), Height = Math.max(200, Surface.clientHeight);
        const Ratio = window.devicePixelRatio || 1;
        if (Canvas.width !== Math.round(Width * Ratio) || Canvas.height !== Math.round(Height * Ratio))
        {
            Canvas.width = Math.round(Width * Ratio);
            Canvas.height = Math.round(Height * Ratio);
        }
        const Context = Canvas.getContext('2d');
        Context.setTransform(Ratio, 0, 0, Ratio, 0, 0);
        Context.clearRect(0, 0, Width, Height);
        const RoundedRect = (X, Y, BoxWidth, BoxHeight, Radius) =>
        {
            Context.beginPath();
            if (Context.roundRect) Context.roundRect(X, Y, BoxWidth, BoxHeight, Radius);
            else Context.rect(X, Y, BoxWidth, BoxHeight);
        };

        const Identity = Specification.Identity, Drivetrain = Specification.Drivetrain;
        const PitWidth = Math.max(104, Width * 0.22);
        const CarWidth = Width - PitWidth;
        const OverallLength = Identity.Wheelbase + 1500;                    // [mm] - nose and tail overhang
        const OverallWidth  = Math.max(Identity.TrackFront, Identity.TrackRear) + 520;
        const Scale = Math.min((CarWidth - 54) / OverallWidth, (Height - 46) / OverallLength);
        const CentreX = CarWidth / 2;
        const CentreY = Height / 2;
        const FrontY  = CentreY - Identity.Wheelbase * Scale / 2;
        const RearY   = CentreY + Identity.Wheelbase * Scale / 2;

        // ① ground grid
        Context.strokeStyle = 'rgba(255,255,255,0.045)';
        Context.lineWidth = 1;
        for (let X = 0; X < Width; X += 34) { Context.beginPath(); Context.moveTo(X, 0); Context.lineTo(X, Height); Context.stroke(); }
        for (let Y = 0; Y < Height; Y += 34) { Context.beginPath(); Context.moveTo(0, Y); Context.lineTo(Width, Y); Context.stroke(); }

        // ② pit lane
        const PitX = Width - PitWidth + 10;
        Context.setLineDash([6, 6]);
        Context.strokeStyle = DetachedTyres().length ? 'rgba(232,168,74,0.5)' : 'rgba(255,255,255,0.14)';
        Context.lineWidth = 1.2;
        RoundedRect(PitX, 16, PitWidth - 24, Height - 46, 14);
        Context.stroke();
        Context.setLineDash([]);
        Context.fillStyle = DetachedTyres().length ? '#e3b46c' : '#5a5a5a';
        Context.font = '500 9.5px "DM Sans", sans-serif';
        Context.fillText('PIT LANE', PitX + 12, 32);

        // ③ chassis
        const BodyWidth = OverallWidth * Scale, BodyLength = OverallLength * Scale;
        Context.fillStyle = 'rgba(159,180,214,0.10)';
        Context.strokeStyle = 'rgba(159,180,214,0.55)';
        Context.lineWidth = 1.4;
        RoundedRect(CentreX - BodyWidth / 2, CentreY - BodyLength / 2, BodyWidth, BodyLength, Math.min(22, BodyWidth * 0.18));
        Context.fill();
        Context.stroke();
        // ④ cabin and axle lines
        Context.strokeStyle = 'rgba(255,255,255,0.16)';
        Context.lineWidth = 1;
        Context.beginPath();
        Context.moveTo(CentreX - BodyWidth * 0.34, FrontY + 16);
        Context.lineTo(CentreX + BodyWidth * 0.34, FrontY + 16);
        Context.lineTo(CentreX + BodyWidth * 0.30, RearY - 30);
        Context.lineTo(CentreX - BodyWidth * 0.30, RearY - 30);
        Context.closePath();
        Context.stroke();
        for (const AxleY of [FrontY, RearY])
        {
            Context.beginPath();
            Context.moveTo(CentreX - BodyWidth / 2, AxleY);
            Context.lineTo(CentreX + BodyWidth / 2, AxleY);
            Context.stroke();
        }
        // ⑤ centre of gravity: the static rear share places it along the wheelbase
        const GravityY = FrontY + Identity.Wheelbase * Scale * (1 - Drivetrain.RearMassShare / 100);
        Context.strokeStyle = '#dbe5f4';
        Context.lineWidth = 1.2;
        Context.beginPath();
        Context.arc(CentreX, GravityY, 6, 0, Math.PI * 2);
        Context.moveTo(CentreX - 10, GravityY);
        Context.lineTo(CentreX + 10, GravityY);
        Context.moveTo(CentreX, GravityY - 10);
        Context.lineTo(CentreX, GravityY + 10);
        Context.stroke();
        Context.fillStyle = '#6a6a6a';
        Context.font = '9px "DM Sans", sans-serif';
        Context.fillText('CG', CentreX + 13, GravityY + 3);

        // ⑥ tyres, enclosed or in the pit lane
        TyreHits.length = 0;
        const CornerGeometry = (Tyre) =>
        {
            const Front = Tyre.Code[0] === 'F';
            const Left  = Tyre.Code[1] === 'L';
            const Track = Front ? Identity.TrackFront : Identity.TrackRear;
            const AxleY = Front ? FrontY : RearY;
            const WheelLength = Tyre.Radius * 2 * Scale;
            const WheelWidth  = Math.max(7, Tyre.Width * Scale);
            const WheelX = CentreX + (Left ? -1 : 1) * (Track * Scale / 2) - WheelWidth / 2;
            return { X: WheelX, Y: AxleY - WheelLength / 2, Length: WheelLength, Width: WheelWidth, AxleY, Track, Front, Left };
        };
        let PitSlot = 0;
        for (const Tyre of Tyres)
        {
            const Geometry = CornerGeometry(Tyre);
            const Compound = Catalog.ResolveCompound(Tyre.Compound);
            const Selected = SelectedTyreId === Tyre.Id;
            if (Tyre.Enclosed)
            {
                Context.fillStyle = Compound.Colour + '38';
                Context.strokeStyle = Selected ? '#eef3fa' : Compound.Colour;
                Context.lineWidth = Selected ? 2 : 1.2;
                RoundedRect(Geometry.X, Geometry.Y, Geometry.Width, Geometry.Length, 5);
                Context.fill();
                Context.stroke();
                Context.fillStyle = Selected ? '#ffffff' : '#c8c8c8';
                Context.font = `${Selected ? '500 ' : ''}9.5px "DM Sans", sans-serif`;
                Context.fillText(Tyre.Code, Geometry.Left ? Geometry.X - 20 : Geometry.X + Geometry.Width + 5, Geometry.AxleY + 3);
                TyreHits.push({ Id: Tyre.Id, X: Geometry.X - 6, Y: Geometry.Y - 4, Width: Geometry.Width + 12, Height: Geometry.Length + 8 });
            }
            else
            {
                // 📝 The detached tyre sits in the pit lane with a dashed link back to the axle it left.
                const SlotY = 52 + PitSlot * 66;
                const SlotX = PitX + (PitWidth - 24) / 2;
                ++PitSlot;
                Context.setLineDash([3, 5]);
                Context.strokeStyle = 'rgba(232,168,74,0.45)';
                Context.lineWidth = 1;
                Context.beginPath();
                Context.moveTo(Geometry.X + Geometry.Width / 2, Geometry.AxleY);
                Context.lineTo(SlotX, SlotY + 20);
                Context.stroke();
                Context.setLineDash([]);
                Context.fillStyle = Compound.Colour + '30';
                Context.strokeStyle = Selected ? '#eef3fa' : 'rgba(232,168,74,0.8)';
                Context.lineWidth = Selected ? 2 : 1.2;
                RoundedRect(SlotX - 26, SlotY - 2, 52, 40, 8);
                Context.fill();
                Context.stroke();
                Context.fillStyle = '#e8c8a0';
                Context.font = '500 10px "DM Sans", sans-serif';
                Context.fillText(Tyre.Code, SlotX - 26, SlotY + 15);
                Context.fillStyle = '#8a8a8a';
                Context.font = '8px "DM Sans", sans-serif';
                Context.fillText('detached', SlotX - 24, SlotY + 28);
                TyreHits.push({ Id: Tyre.Id, X: SlotX - 30, Y: SlotY - 6, Width: 60, Height: 48 });
            }
        }

        // ⑦ scale bar
        const Metre = 1000 * Scale;
        Context.strokeStyle = 'rgba(255,255,255,0.3)';
        Context.lineWidth = 1;
        Context.beginPath();
        Context.moveTo(16, Height - 16);
        Context.lineTo(16 + Metre, Height - 16);
        Context.moveTo(16, Height - 20);
        Context.lineTo(16, Height - 12);
        Context.moveTo(16 + Metre, Height - 20);
        Context.lineTo(16 + Metre, Height - 12);
        Context.stroke();
        Context.fillStyle = '#5a5a5a';
        Context.font = '9px "DM Sans", sans-serif';
        Context.fillText('1 m', 16 + Metre + 6, Height - 13);
    }

    function ConstructViewport()
    {
        const Bar = document.getElementById('ViewportBar');
        const Hud = document.getElementById('ViewportHud');
        const Readout = document.getElementById('ViewportReadout');
        const Caption = document.getElementById('ViewportCaption');
        Bar.append(
            C.Construct('div', 'scene-glyph', C.ConstructGlyph('Folder')),
            C.Construct('div', null, [C.Construct('div', 'scene-title', 'Default.levelspace'),
                                      C.Construct('div', 'scene-meta', 'Vehicle GT-04 placed · chassis and four tyres only')]),
            C.Construct('span', 'spacer'),
            C.Construct('span', 'renderer-chip', [C.Construct('i'), C.Construct('span', null, 'VEHICLE ADDED')]),
            C.Construct('div', 'view-select', [C.ConstructGlyph('Scan'), C.Construct('span', null, 'Top · layout'), C.ConstructGlyph('Chevron')]));
        Caption.textContent = 'Schematic · authored wheelbase, track and tyres — no C++ capture of a vehicle exists yet';
        const Mass = C.Construct('span');
        const Layout = C.Construct('span');
        Hud.append(Mass, Layout);
        const Metric = C.Construct('div');
        const Wheelbase = C.Construct('span');
        Readout.append(Metric, Wheelbase);
        C.Register(() =>
        {
            Mass.textContent = `${Specification.Identity.Name} · ${DisplayText(Specification.Identity.KerbMass, 'Mass', 0)} ${UnitSymbol('Mass')}`;
            Layout.textContent = `${EnclosedTyres().length}/4 tyres enclosed`;
            Metric.textContent = `mass ${DisplayText(Specification.Identity.KerbMass, 'Mass', 0)} ${UnitSymbol('Mass')}`;
            Wheelbase.textContent = `wheelbase ${DisplayText(Specification.Identity.Wheelbase, 'Length', 0)} ${UnitSymbol('Length')} · `
                + `track ${DisplayText(Specification.Identity.TrackFront, 'Length', 0)} / `
                + `${DisplayText(Specification.Identity.TrackRear, 'Length', 0)} ${UnitSymbol('Length')}`;
            RasterizeScene();
        });
        const Canvas = document.getElementById('ViewportCanvas');
        new ResizeObserver(() => RasterizeScene()).observe(Canvas.parentElement);
        Canvas.addEventListener('pointerdown', Event =>
        {
            const Bounds = Canvas.getBoundingClientRect();
            const X = Event.clientX - Bounds.left, Y = Event.clientY - Bounds.top;
            const Hit = TyreHits.find(Candidate => X >= Candidate.X && X <= Candidate.X + Candidate.Width
                && Y >= Candidate.Y && Y <= Candidate.Y + Candidate.Height);
            SelectRecord(Hit ? Hit.Id : 'Vehicle');
        });
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    HERO AND CHIPS
    //------------------------------------------------------------------------------------------------------------------------

    function ConstructHero(Page)
    {
        const TorquePoints = [];
        const PowerPoints  = [];
        const TorqueAxis = { Minimum: 0, Maximum: 800, MajorStep: 200, Label: 'Torque', Unit: 'Torque' };
        const PowerAxis  = { Minimum: 0, Maximum: 500, MajorStep: 100, Label: 'Power', Unit: 'Power' };
        const SpeedAxis  = { Minimum: 800, Maximum: 8000, MajorStep: 1000, Label: 'Engine speed', Unit: 'Rpm' };
        const Mark   = C.Construct('div', 'mark');
        const MarkText = C.Construct('span');
        const Metric = C.Construct('div', 'metric-big');
        const Rate   = C.Construct('div', 'rate');
        const Low    = C.Construct('div', 'low');
        const TargetRow = C.Construct('div', 'target-row');
        const TargetLabel = C.Construct('span', null, 'Rated torque');
        const EditPill = C.Construct('button', 'pill-button accent small', [C.ConstructGlyph('Activity'), C.Construct('span', null, 'Edit curve')]);
        EditPill.type = 'button';
        EditPill.addEventListener('click', () => RevealSection('Curves'));
        TargetRow.append(C.ConstructGlyph('Target'), TargetLabel, C.Construct('span', 'spacer'), EditPill);
        const Card = C.Construct('div', 'hero-card', [
            C.Construct('div', 'hero-top', [C.Construct('div', 'budget-chip', [Mark, MarkText]), C.Construct('div', 'estimate-tag', 'Prototype · model')]),
            C.Construct('div', 'hero-title', 'Dyno sheet · engine speed against torque and power'),
            C.Construct('div', 'hero-metrics', [Metric, C.Construct('div', 'metric-side', [Rate, Low])]),
            TargetRow
        ]);
        Page.append(Card);
        // 📝 The series update before the chart draws: the updater is registered first, so the chart's own refresher sees it.
        C.Register(() =>
        {
            TorquePoints.length = 0;
            PowerPoints.length  = 0;
            for (const Point of SampleProfile(ResolveProfile('EngineTorque'), 72)) TorquePoints.push(Point);
            for (const Point of SampleProfile(ResolveProfile('EnginePower'), 72)) PowerPoints.push(Point);
            const Peak = ResolvePerformance().PeakPower;
            PowerAxis.Maximum = Math.max(200, Math.ceil(Peak / 100) * 100);
        });
        const Series = C.SeriesChart(Card, {
            Height: 158, UnitSystem: () => UnitSystem,
            XAxis: SpeedAxis,
            Series: [
                { Id: 'Torque', Points: TorquePoints, Axis: TorqueAxis, Colour: '#e8a84a', Area: true, Width: 1.6 },
                { Id: 'Power', Points: PowerPoints, Axis: PowerAxis, Colour: '#9fb4d6', Dash: '5 4', Width: 1.3 }
            ],
            CaptionText: 'Solid — torque. Dashed — power, derived as torque × engine speed. Both follow the authored curve.'
        });
        Card.append(C.Construct('div', 'hero-note', 'This card, the launch estimate, the top speed and the exported record all read the '
                                                    + 'same editable torque curve. Move one control point and every one of them follows.'));
        const Live = () =>
        {
            const Performance = ResolvePerformance();
            C.ConstructMetric(Metric, DisplayText(Performance.PeakTorque, 'Torque', 0), UnitSymbol('Torque'));
            C.ConstructMetric(Rate, DisplayText(Performance.PeakPower, 'Power', 0), UnitSymbol('Power'));
            Rate.append(C.Construct('small', null, `@ ${DisplayText(Performance.PeakPowerSpeed, 'Rpm', 0)} rpm`));
            Low.textContent = `peak torque at ${DisplayText(Performance.PeakTorqueSpeed, 'Rpm', 0)} rpm`;
            const Rated = Specification.Engine.RatedTorque;
            const Within = Performance.PeakTorque <= Rated + 1e-6;
            const Close  = Performance.PeakTorque <= Rated * 1.06;
            Mark.className = 'mark ' + (Within ? 'live' : Close ? 'warm' : 'danger');
            Mark.textContent = '';
            Mark.append(C.ConstructGlyph(Within ? 'Check' : 'Alert'));
            MarkText.textContent = Within ? 'Within the rated torque' : Close ? 'At the rated torque' : 'Above the rated torque';
            TargetLabel.textContent = `Rated ${DisplayText(Rated, 'Torque', 0)} ${UnitSymbol('Torque')}`;
            TorqueAxis.Maximum = Math.max(400, Math.ceil((Performance.PeakTorque * 1.25) / 100) * 100);
            Series.Refresh();
        };
        C.Register(Live);
        return Card;
    }

    function ConstructChips(Page)
    {
        const Grid = C.Construct('div', 'tile-grid four');
        QuantityTile(Grid, { Label: 'Kerb mass', Glyph: 'Weight', UnitKey: 'Mass', Digits: 0, Read: () => Specification.Identity.KerbMass });
        QuantityTile(Grid, { Label: 'Power to weight', Glyph: 'Gauge', UnitKey: 'SpecificPower', Digits: 0,
                             Read: () => ResolvePerformance().PowerToWeight,
                             Tone: () => ResolvePerformance().PowerToWeight >= 280 ? 'live' : null });
        QuantityTile(Grid, { Label: '0–100 km/h', Glyph: 'Clock', UnitKey: 'Seconds', Digits: 2,
                             Read: () => ResolvePerformance().ZeroToHundred,
                             Tone: () => ResolvePerformance().ZeroToHundred <= 3.5 ? 'live' : 'warm' });
        QuantityTile(Grid, { Label: 'Top speed', Glyph: 'Road', UnitKey: 'Speed', Digits: 0, Read: () => ResolvePerformance().TopSpeed });
        Page.append(Grid);
        Page.append(C.Construct('div', 'properties-head', [C.Construct('div', 'title', 'Quick controls'), C.Construct('div', 'caption', 'Vehicle aids')]));
        const Tiles = C.Construct('div', 'toggle-grid');
        C.ToggleTile(Tiles, Bind(Specification.Aids, 'TractionControl', { Label: 'Traction control', Glyph: 'Tyre' }));
        C.ToggleTile(Tiles, Bind(Specification.Aids, 'StabilityControl', { Label: 'ESC stability', Glyph: 'Activity' }));
        C.ToggleTile(Tiles, Bind(Specification.Aids, 'AntiLockBrakes', { Label: 'ABS', Glyph: 'Brake' }));
        C.ToggleTile(Tiles, Bind(Specification.Aids, 'LaunchControl', { Label: 'Launch control', Glyph: 'Zap' }));
        C.ToggleTile(Tiles, Bind(Specification.Aids, 'DrsEnabled', { Label: 'DRS', Glyph: 'Wind' }));
        C.ToggleTile(Tiles, Bind(Specification.Aids, 'RevLimiter', { Label: 'Rev limiter', Glyph: 'Gauge' }));
        C.ToggleTile(Tiles, Bind(Specification.Aids, 'Telemetry', { Label: 'Telemetry', Glyph: 'Activity' }));
        Page.append(Tiles);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                       SECTIONS
    //------------------------------------------------------------------------------------------------------------------------

    function DeclareSection(Page, Options)
    {
        const Section = C.SectionCard(Page, Options);
        Sections.push({ Id: Options.Id, Tab: Options.Tab || Options.Title, Card: Section.Card, Inner: Section.Inner });
        return Section;
    }

    function ConstructIdentity(Page)
    {
        const Section = DeclareSection(Page, {
            Id: 'Identity', Title: 'Chassis and identity', Tab: 'Chassis', Glyph: 'Vehicle', Colour: '#9fb4d6',
            Status: () => `${DisplayText(Specification.Identity.KerbMass, 'Mass', 0)} ${UnitSymbol('Mass')}`
        });
        const Body = Section.Inner;
        Body.append(C.Construct('p', 'section-caption', 'The record the scene gains. Nothing else changes: no body geometry, no '
            + 'accessories, only the chassis frame and the four tyres it encloses.'));
        const Grid = C.Construct('div', 'tile-grid four');
        QuantityTile(Grid, { Label: 'Wheelbase', Glyph: 'Road', UnitKey: 'Length', Digits: 0, Read: () => Specification.Identity.Wheelbase });
        QuantityTile(Grid, { Label: 'Centre of gravity', Glyph: 'Target', UnitKey: 'Length', Digits: 0, Read: () => Specification.Identity.CentreOfGravityHeight });
        QuantityTile(Grid, { Label: 'Fuel capacity', Glyph: 'Gauge', UnitKey: 'Volume', Digits: 0, Read: () => Specification.Identity.FuelCapacity });
        QuantityTile(Grid, { Label: 'Tyre count', Glyph: 'Tyre', UnitKey: 'Ratio', Digits: 0, Read: () => EnclosedTyres().length });
        Body.append(Grid);
        Body.append(C.Construct('div', 'sub-heading', 'Mass and dimensions'));
        QuantityRow(Body, { Label: 'Kerb mass', UnitKey: 'Mass', Minimum: 900, Maximum: 1800, Step: 1, Read: () => Specification.Identity.KerbMass,
                            Write: Quantity => { Specification.Identity.KerbMass = Quantity; } });
        QuantityRow(Body, { Label: 'Wheelbase', UnitKey: 'Length', Minimum: 2200, Maximum: 3200, Step: 5,
                            Read: () => Specification.Identity.Wheelbase, Write: Quantity => { Specification.Identity.Wheelbase = Quantity; } });
        QuantityRow(Body, { Label: 'Front track', UnitKey: 'Length', Minimum: 1400, Maximum: 1800, Step: 5,
                            Read: () => Specification.Identity.TrackFront, Write: Quantity => { Specification.Identity.TrackFront = Quantity; } });
        QuantityRow(Body, { Label: 'Rear track', UnitKey: 'Length', Minimum: 1400, Maximum: 1800, Step: 5,
                            Read: () => Specification.Identity.TrackRear, Write: Quantity => { Specification.Identity.TrackRear = Quantity; } });
        QuantityRow(Body, { Label: 'Centre of gravity height', UnitKey: 'Length', Minimum: 260, Maximum: 620, Step: 2,
                            Read: () => Specification.Identity.CentreOfGravityHeight,
                            Write: Quantity => { Specification.Identity.CentreOfGravityHeight = Quantity; } });
        QuantityRow(Body, { Label: 'Ride height', UnitKey: 'Length', Minimum: 60, Maximum: 220, Step: 2,
                            Read: () => Specification.Identity.RideHeight, Write: Quantity => { Specification.Identity.RideHeight = Quantity; } });
        QuantityRow(Body, { Label: 'Fuel capacity', UnitKey: 'Volume', Minimum: 30, Maximum: 120, Step: 1,
                            Read: () => Specification.Identity.FuelCapacity, Write: Quantity => { Specification.Identity.FuelCapacity = Quantity; } });
        QuantityRow(Body, { Label: 'Loaded fuel', UnitKey: 'Volume', Minimum: 0, Maximum: 120, Step: 0.5, Digits: 1,
                            Tags: [['TELEMETRY', 'live']],
                            Read: () => Specification.Fuel.Load, Write: Quantity => { Specification.Fuel.Load = Quantity; } });
        C.SegmentedRow(Body, {
            Label: 'Drive layout',
            Options: [['Front', 'Fwd'], ['Rear', 'Rwd'], ['All four', 'All']],
            Read: () => Specification.Drivetrain.Layout,
            Write: Layout => { Specification.Drivetrain.Layout = Layout; }
        });
        QuantityRow(Body, { Label: 'Static rear mass share', UnitKey: 'Percent', Minimum: 40, Maximum: 65, Step: 1,
                            Read: () => Specification.Drivetrain.RearMassShare,
                            Write: Quantity => { Specification.Drivetrain.RearMassShare = Quantity; } });
    }

    function ConstructEngine(Page)
    {
        const Section = DeclareSection(Page, {
            Id: 'Engine', Title: 'Engine and drivetrain', Tab: 'Engine', Glyph: 'Zap', Colour: '#e8a84a',
            Status: () => `${DisplayText(Specification.Engine.Redline, 'Rpm', 0)} rpm limiter`
        });
        const Body = Section.Inner;
        const Grid = C.Construct('div', 'stat-grid');
        QuantityReadout(Grid, { Label: 'Specific torque', UnitKey: 'Torque', Digits: 0,
                                Read: () => ResolvePerformance().SpecificTorque });
        QuantityReadout(Grid, { Label: 'Peak power speed', UnitKey: 'Rpm', Digits: 0, Read: () => ResolvePerformance().PeakPowerSpeed });
        QuantityReadout(Grid, { Label: 'Shifts to 100', UnitKey: 'Ratio', Digits: 0, Read: () => ResolvePerformance().ShiftCount });
        Body.append(Grid);
        Body.append(C.Construct('div', 'sub-heading', 'Engine'));
        QuantityRow(Body, { Label: 'Displacement', UnitKey: 'Litres', Minimum: 1.0, Maximum: 6.5, Step: 0.1, Digits: 1,
                            Read: () => Specification.Engine.Displacement, Write: Quantity => { Specification.Engine.Displacement = Quantity; } });
        QuantityRow(Body, { Label: 'Idle speed', UnitKey: 'Rpm', Minimum: 600, Maximum: 1600, Step: 25,
                            Read: () => Specification.Engine.IdleSpeed, Write: Quantity => { Specification.Engine.IdleSpeed = Quantity; } });
        QuantityRow(Body, { Label: 'Rev limiter', UnitKey: 'Rpm', Minimum: 5000, Maximum: 9500, Step: 50,
                            Tags: [['LIMITER', 'warm']],
                            Read: () => Specification.Engine.Redline, Write: Quantity => { Specification.Engine.Redline = Quantity; } });
        QuantityRow(Body, { Label: 'Rated torque', UnitKey: 'Torque', Minimum: 200, Maximum: 900, Step: 5,
                            Read: () => Specification.Engine.RatedTorque, Write: Quantity => { Specification.Engine.RatedTorque = Quantity; } });
        QuantityRow(Body, { Label: 'Turbo boost', UnitKey: 'Boost', Minimum: 0, Maximum: 2.5, Step: 0.05, Digits: 2,
                            Read: () => Specification.Engine.TurboBoost, Write: Quantity => { Specification.Engine.TurboBoost = Quantity; } });
        QuantityRow(Body, { Label: 'Engine inertia', UnitKey: 'Inertia', Minimum: 0.08, Maximum: 0.8, Step: 0.01, Digits: 2,
                            Read: () => Specification.Engine.EngineInertia, Write: Quantity => { Specification.Engine.EngineInertia = Quantity; } });
        QuantityRow(Body, { Label: 'Full-load fuel burn', UnitKey: 'Flow', Minimum: 20, Maximum: 140, Step: 1,
                            Read: () => Specification.Engine.FuelBurn, Write: Quantity => { Specification.Engine.FuelBurn = Quantity; } });
        Body.append(C.Construct('div', 'sub-heading', 'Gear train'));
        QuantityRow(Body, { Label: 'Forward gears', UnitKey: 'Ratio', Minimum: 4, Maximum: 8, Step: 1,
                            Read: () => Specification.Drivetrain.GearCount, Write: Quantity => { Specification.Drivetrain.GearCount = Quantity; } });
        QuantityRow(Body, { Label: 'Final drive', UnitKey: 'Ratio', Minimum: 2.5, Maximum: 4.6, Step: 0.01, Digits: 2,
                            Read: () => Specification.Drivetrain.FinalDrive, Write: Quantity => { Specification.Drivetrain.FinalDrive = Quantity; } });
        QuantityRow(Body, { Label: 'Driveline efficiency', UnitKey: 'Percent', Minimum: 70, Maximum: 100, Step: 1,
                            Read: () => Specification.Drivetrain.DrivelineEfficiency * 100,
                            Write: Quantity => { Specification.Drivetrain.DrivelineEfficiency = Quantity / 100; } });
        QuantityRow(Body, { Label: 'Loaded wheel radius', UnitKey: 'Length', Minimum: 280, Maximum: 400, Step: 1,
                            Read: () => Specification.Drivetrain.WheelRadius, Write: Quantity => { Specification.Drivetrain.WheelRadius = Quantity; } });
        QuantityRow(Body, { Label: 'Peak tyre friction', UnitKey: 'Ratio', Minimum: 0.7, Maximum: 1.8, Step: 0.01, Digits: 2,
                            Read: () => Specification.Drivetrain.TyreFriction, Write: Quantity => { Specification.Drivetrain.TyreFriction = Quantity; } });
        Body.append(C.Construct('div', 'sub-heading', 'Gear ratios'));
        const Ratios = C.Construct('div', 'ratio-strip');
        Body.append(Ratios);
        C.Register(() =>
        {
            Ratios.textContent = '';
            const Count = Math.max(1, Math.min(Specification.Drivetrain.GearCount, Specification.Drivetrain.GearRatios.length));
            for (let Index = 0; Index < Count; ++Index)
            {
                const Cell = C.Construct('div', 'ratio-cell', [
                    C.Construct('span', 'ratio-name', String(Index + 1)),
                    C.Construct('span', 'ratio-value', Specification.Drivetrain.GearRatios[Index].toFixed(2))
                ]);
                Ratios.append(Cell);
            }
        });
        Body.append(C.Construct('div', 'sub-heading', 'Forced induction and supercharger'));
        C.SegmentedRow(Body, {
            Label: 'Induction architecture',
            Options: [['NA', 'NaturallyAspirated'], ['Turbo', 'Turbocharged'], ['Supercharged', 'Supercharged']],
            Read: () => Specification.Induction.Mode,
            Write: Mode => { Specification.Induction.Mode = Mode; }
        });
        C.SwitchRow(Body, {
            Label: 'Race tune (+33 % boost & aggressive timing)',
            Detail: 'Raises peak compressor or wastegate boost target and advances wide-open-throttle spark.',
            Read: () => Specification.Induction.RaceTune,
            Write: Enabled => { Specification.Induction.RaceTune = Enabled; }
        });
        C.SegmentedRow(Body, {
            Label: 'Supercharger compressor',
            Options: [['Roots', 'Roots'], ['Twin-screw', 'TwinScrew'], ['Centrifugal', 'Centrifugal']],
            Read: () => Specification.Induction.CompressorType,
            Write: Kind => { Specification.Induction.CompressorType = Kind; }
        });
        QuantityRow(Body, { Label: 'Pulley drive ratio', UnitKey: 'Ratio', Minimum: 1.0, Maximum: 3.8, Step: 0.05, Digits: 2,
                            Read: () => Specification.Induction.DriveRatio, Write: Quantity => { Specification.Induction.DriveRatio = Quantity; } });
        QuantityRow(Body, { Label: 'Compressor displacement', UnitKey: 'DisplacementCc', Minimum: 600, Maximum: 3400, Step: 25,
                            Read: () => Specification.Induction.DisplacementCc, Write: Quantity => { Specification.Induction.DisplacementCc = Quantity; } });
        QuantityRow(Body, { Label: 'Supercharger peak boost', UnitKey: 'Boost', Minimum: 0.1, Maximum: 2.2, Step: 0.05, Digits: 2,
                            Read: () => Specification.Induction.SuperchargerMaxBoost,
                            Write: Quantity => { Specification.Induction.SuperchargerMaxBoost = Quantity; } });
        QuantityRow(Body, { Label: 'WOT parasitic drag', UnitKey: 'Torque', Minimum: 4, Maximum: 110, Step: 1,
                            Read: () => Specification.Induction.PeakParasiticTorque,
                            Write: Quantity => { Specification.Induction.PeakParasiticTorque = Quantity; } });
        QuantityRow(Body, { Label: 'Bypass parasitic share', UnitKey: 'Percent', Minimum: 0, Maximum: 40, Step: 1,
                            Read: () => Specification.Induction.BypassParasiticPercent,
                            Write: Quantity => { Specification.Induction.BypassParasiticPercent = Quantity; } });
        QuantityRow(Body, { Label: 'Bypass valve threshold', UnitKey: 'Percent', Minimum: 2, Maximum: 50, Step: 1,
                            Read: () => Specification.Induction.BypassThrottlePercent,
                            Write: Quantity => { Specification.Induction.BypassThrottlePercent = Quantity; } });
        QuantityRow(Body, { Label: 'Turbo spool-up rate', UnitKey: 'Frequency', Minimum: 0.5, Maximum: 12.0, Step: 0.1, Digits: 1,
                            Read: () => Specification.Induction.TurboSpoolUpRate,
                            Write: Quantity => { Specification.Induction.TurboSpoolUpRate = Quantity; } });
        QuantityRow(Body, { Label: 'Turbo blow-off rate', UnitKey: 'Frequency', Minimum: 1.0, Maximum: 16.0, Step: 0.1, Digits: 1,
                            Read: () => Specification.Induction.TurboSpoolDownRate,
                            Write: Quantity => { Specification.Induction.TurboSpoolDownRate = Quantity; } });
        QuantityRow(Body, { Label: 'Torque gain per bar', UnitKey: 'Ratio', Minimum: 0.15, Maximum: 1.10, Step: 0.01, Digits: 2,
                            Read: () => Specification.Induction.TorqueGainPerBar,
                            Write: Quantity => { Specification.Induction.TorqueGainPerBar = Quantity; } });

        Body.append(C.Construct('div', 'sub-heading', 'Differential and clutch'));
        C.SegmentedRow(Body, {
            Label: 'Differential mechanism',
            Options: [['Open', 'Open'], ['Locked', 'Locked'], ['1.5-way LSD', 'ClutchPack15Way'], ['Torsen', 'Torsen']],
            Read: () => Specification.Differential.Type,
            Write: Kind => { Specification.Differential.Type = Kind; }
        });
        QuantityRow(Body, { Label: 'LSD preload torque', UnitKey: 'Torque', Minimum: 0, Maximum: 320, Step: 5,
                            Read: () => Specification.Differential.PreloadTorque,
                            Write: Quantity => { Specification.Differential.PreloadTorque = Quantity; } });
        QuantityRow(Body, { Label: 'Power ramp lock', UnitKey: 'Percent', Minimum: 0, Maximum: 100, Step: 1,
                            Read: () => Specification.Differential.PowerLockPercent,
                            Write: Quantity => { Specification.Differential.PowerLockPercent = Quantity; } });
        QuantityRow(Body, { Label: 'Coast ramp lock', UnitKey: 'Percent', Minimum: 0, Maximum: 100, Step: 1,
                            Read: () => Specification.Differential.CoastLockPercent,
                            Write: Quantity => { Specification.Differential.CoastLockPercent = Quantity; } });
        QuantityRow(Body, { Label: 'Torsen bias ratio', UnitKey: 'Ratio', Minimum: 1.2, Maximum: 5.5, Step: 0.1, Digits: 1,
                            Read: () => Specification.Differential.TorsenBiasRatio,
                            Write: Quantity => { Specification.Differential.TorsenBiasRatio = Quantity; } });
        QuantityRow(Body, { Label: 'AWD front torque split', UnitKey: 'Percent', Minimum: 0, Maximum: 60, Step: 1,
                            Read: () => Specification.Differential.FrontTorqueSplitPercent,
                            Write: Quantity => { Specification.Differential.FrontTorqueSplitPercent = Quantity; } });
        QuantityRow(Body, { Label: 'Shift cut duration', UnitKey: 'Milliseconds', Minimum: 25, Maximum: 350, Step: 5,
                            Read: () => Specification.Differential.ShiftDurationMs,
                            Write: Quantity => { Specification.Differential.ShiftDurationMs = Quantity; } });
        QuantityRow(Body, { Label: 'Clutch torque capacity', UnitKey: 'Torque', Minimum: 350, Maximum: 1800, Step: 10,
                            Read: () => Specification.Differential.ClutchCapacity,
                            Write: Quantity => { Specification.Differential.ClutchCapacity = Quantity; } });

        Body.append(C.Construct('p', 'footer-note', 'Ratios are fixed in this prototype; the curve section edits torque, and the '
            + 'launch estimate uses these ratios with the traction circle below.'));
    }

    function ConstructCurves(Page)
    {
        const Section = DeclareSection(Page, {
            Id: 'Curves', Title: 'Curves', Tab: 'Curves', Glyph: 'Activity', Colour: '#9fb4d6',
            Status: () => `${Profiles.filter(Profile => Profile.Editable).length} editable`
        });
        const Body = Section.Inner;
        Body.append(C.Construct('p', 'section-caption', 'Engine torque, brake torque and the temperature curves are authored here. '
            + 'Every other readout is derived, so a custom vehicle is this list plus the specification above.'));

        // ① the library
        const Library = C.Construct('div', 'curve-library');
        Body.append(Library);
        let BuildEditor = null;
        for (const Profile of Profiles)
        {
            const Dot = C.Construct('span', 'curve-dot');
            Dot.style.background = Profile.Colour;
            const Value = C.Construct('span', 'curve-value');
            const Element = C.Construct('div', 'curve-row',
                [Dot, C.Construct('span', 'curve-name', Profile.Title), C.Construct('span', 'spacer'),
                 C.Construct('span', 'tag ' + (Profile.Editable ? 'accent' : 'muted'), Profile.Editable ? 'EDITABLE' : 'DERIVED'), Value]);
            Element.tabIndex = 0;
            const Choose = () =>
            {
                SelectedCurveKey = Profile.Key;
                if (BuildEditor) BuildEditor();
                C.Broadcast();
            };
            Element.addEventListener('click', Choose);
            Element.addEventListener('keydown', Event =>
            {
                if (Event.key === 'Enter' || Event.key === ' ') { Choose(); Event.preventDefault(); }
            });
            C.Register(() =>
            {
                Element.classList.toggle('selected', SelectedCurveKey === Profile.Key);
                const Peak = Catalog.ResolveCurvePeak(Profile, Specification, Profiles);
                const Reading = Format(Peak.Y, Profile.Y.Unit, 0);
                Value.textContent = `${Reading.Text} ${Reading.Unit} @ ${DisplayText(Peak.X, Profile.X.Unit, 0)} ${UnitSymbol(Profile.X.Unit)}`;
            });
            Library.append(Element);
        }

        // ② the editor, rebuilt only when the selected profile changes; the editor it replaces releases its refreshers
        const EditorHost = C.Construct('div', 'curve-editor-host');
        Body.append(EditorHost);
        BuildEditor = () =>
        {
            C.ReleaseScope('CurveEditor');
            C.OpenScope('CurveEditor');
            EditorHost.textContent = '';
            const Profile = ResolveProfile(SelectedCurveKey);
            C.CurveEditor(EditorHost, {
                Profile, Height: 232,
                Specification, Profiles,
                UnitSystem: () => UnitSystem,
                Selection: { Read: () => PointSelections[Profile.Key] ?? 0, Write: Index => { PointSelections[Profile.Key] = Index; } },
                Target: Profile.Key === 'EngineTorque' ? { Y: Specification.Engine.RatedTorque, Label: 'Rated torque' } : null,
                OnEdit: (Action, Index) =>
                {
                    const Edited = ResolveProfile(SelectedCurveKey);
                    const Point = Edited.Points[Math.min(Index, Edited.Points.length - 1)];
                    const Reading = Format(Point[1], Edited.Y.Unit, 1);
                    const Speed = Format(Point[0], Edited.X.Unit, 0);
                    AppendLog('CURVES', `${Edited.Title} ${Action === 'insert' ? 'point added' : Action === 'remove' ? 'point removed' : 'point moved'}`,
                              `${Speed.Text} ${Speed.Unit} → ${Reading.Text} ${Reading.Unit}`, Action === 'remove' ? 'warm' : 'live');
                }
            });
            C.CloseScope();
        };
        BuildEditor();

        // ③ the selected control point, edited numerically with units
        Body.append(C.Construct('div', 'sub-heading', 'Selected control point'));
        const PointLabel = C.Construct('div', 'point-caption');
        Body.append(PointLabel);
        C.Register(() =>
        {
            const Profile = ResolveProfile(SelectedCurveKey);
            const Index = PointSelections[Profile.Key] ?? 0;
            PointLabel.textContent = Profile.Editable
                ? `Point ${Math.min(Index + 1, Profile.Points.length)} of ${Profile.Points.length} — drag it on the chart, or type here.`
                : 'Derived curve: sample it, or edit the source curve it follows.';
        });
        const PointRow = C.Construct('div', 'point-fields');
        Body.append(PointRow);
        const PointProfile = () => ResolveProfile(SelectedCurveKey);
        const PointIndex = () => Math.min(PointSelections[SelectedCurveKey] ?? 0, PointProfile().Points.length - 1);
        const Availability = () =>
        {
            const Profile = PointProfile();
            return Profile.Editable ? { Enabled: true, Reason: '' } : { Enabled: false, Reason: 'derived curve' };
        };
        // 📝 The pills read and write canonical coordinates; each writes through RelocateCurvePoint, which keeps the point
        //    between its neighbours and inside the axis bounds the track shows.
        QuantityRow(PointRow, {
            Label: 'Point X', Digits: 0, Tags: [['X', 'muted']], Availability,
            UnitKey: () => PointProfile().X.Unit,
            Minimum: () => PointProfile().X.Minimum, Maximum: () => PointProfile().X.Maximum,
            Step: () => (PointProfile().X.Maximum - PointProfile().X.Minimum) / 100,
            Read: () => PointProfile().Points[PointIndex()][0],
            Write: Quantity =>
            {
                const Profile = PointProfile();
                Catalog.RelocateCurvePoint(Profile, PointIndex(), Quantity, Profile.Points[PointIndex()][1]);
            }
        });
        QuantityRow(PointRow, {
            Label: 'Point Y', Digits: 1, Tags: [['Y', 'muted']], Availability,
            UnitKey: () => PointProfile().Y.Unit,
            Minimum: () => PointProfile().Y.Minimum, Maximum: () => PointProfile().Y.Maximum,
            Step: () => (PointProfile().Y.Maximum - PointProfile().Y.Minimum) / 200,
            Read: () => PointProfile().Points[PointIndex()][1],
            Write: Quantity =>
            {
                const Profile = PointProfile();
                Catalog.RelocateCurvePoint(Profile, PointIndex(), Profile.Points[PointIndex()][0], Quantity);
            }
        });

        // ④ actions
        const Add = C.Construct('button', 'pill-button', [C.ConstructGlyph('Plus'), C.Construct('span', null, 'Add point')]);
        const Remove = C.Construct('button', 'pill-button', [C.ConstructGlyph('Close'), C.Construct('span', null, 'Remove point')]);
        const Reset = C.Construct('button', 'pill-button', [C.ConstructGlyph('Reset'), C.Construct('span', null, 'Reset curve')]);
        const Copy = C.Construct('button', 'pill-button accent', [C.ConstructGlyph('Copy'), C.Construct('span', null, 'Copy CSV')]);
        for (const Button of [Add, Remove, Reset, Copy]) Button.type = 'button';
        Add.addEventListener('click', () =>
        {
            const Profile = PointProfile();
            if (!Profile.Editable) { C.Toast('Derived curve — edit the torque curve'); return; }
            const Points = Profile.Points;
            let Largest = 0, InsertX = (Profile.X.Minimum + Profile.X.Maximum) / 2;
            for (let Index = 0; Index < Points.length - 1; ++Index)
            {
                const Gap = Points[Index + 1][0] - Points[Index][0];
                if (Gap > Largest) { Largest = Gap; InsertX = (Points[Index][0] + Points[Index + 1][0]) / 2; }
            }
            const Inserted = Catalog.InsertCurvePoint(Profile, InsertX, Catalog.EvaluateCurve(Profile, InsertX, Specification, Profiles));
            if (Inserted < 0) { C.Toast('No room for another point there'); return; }
            PointSelections[Profile.Key] = Inserted;
            AppendLog('CURVES', `${Profile.Title} point added`, `${DisplayText(InsertX, Profile.X.Unit, 0)} ${UnitSymbol(Profile.X.Unit)}`, 'live');
            C.Broadcast();
        });
        Remove.addEventListener('click', () =>
        {
            const Profile = PointProfile();
            if (!Profile.Editable) { C.Toast('Derived curve — edit the torque curve'); return; }
            const RemovedIndex = PointIndex();
            if (!Catalog.RemoveCurvePoint(Profile, RemovedIndex)) { C.Toast('A curve keeps at least two points'); return; }
            PointSelections[Profile.Key] = Math.max(0, Math.min(RemovedIndex, Profile.Points.length - 1));
            AppendLog('CURVES', `${Profile.Title} point removed`, `${Profile.Points.length} points remain`, 'warm');
            C.Broadcast();
        });
        Reset.addEventListener('click', () =>
        {
            const Profile = PointProfile();
            const Index = Profiles.findIndex(Candidate => Candidate.Key === Profile.Key);
            if (Index >= 0 && Baseline.Profiles[Index])
            {
                Profile.Points = JSON.parse(JSON.stringify(Baseline.Profiles[Index].Points));
                Profile.Interpolation = Baseline.Profiles[Index].Interpolation;
            }
            PointSelections[Profile.Key] = 0;
            AppendLog('CURVES', `${Profile.Title} reset`, 'baseline points restored', 'warm');
            C.Broadcast();
        });
        Copy.addEventListener('click', () =>
        {
            const Text = Catalog.EncodeCurveCsv(PointProfile(), UnitSystem);
            (navigator.clipboard ? navigator.clipboard.writeText(Text) : Promise.reject())
                .then(() => C.Toast('Curve copied as CSV'), () => C.Toast('Copy unavailable here'));
        });
        Body.append(C.Construct('div', 'action-row wrap', [Add, Remove, Reset, Copy]));
        C.SegmentedRow(Body, {
            Label: 'Interpolation', Tags: [['SHAPE', 'muted']],
            Options: [['Smooth · monotone', 'Smooth'], ['Straight segments', 'Linear']],
            Read: () => PointProfile().Interpolation,
            Write: Mode => { PointProfile().Interpolation = Mode; }
        });
        Body.append(C.Construct('p', 'footer-note', 'Drag a control point to move it; shift-click or double-click empty space to add one; '
            + 'right-click or Delete removes it; the arrow keys nudge the selected point. Smooth interpolation is monotone: '
            + 'the curve never overshoots between the points you authored.'));
    }

    function ConstructThermal(Page)
    {
        const Section = DeclareSection(Page, {
            Id: 'Thermal', Title: 'Thermal', Tab: 'Thermal', Glyph: 'Thermometer', Colour: '#d95656',
            Status: () => `${DisplayText(Specification.Thermal.CoolantTarget, 'Temperature', 0)} ${UnitSymbol('Temperature')} coolant`
        });
        const Body = Section.Inner;
        Body.append(C.Construct('p', 'section-caption', 'Temperatures are judged against the compound window and the targets: green inside, '
            + 'amber at the edge, red beyond. The coolant curve is the same record the Curves section edits.'));
        const Grid = C.Construct('div', 'instrument-grid');
        const Instrument = (Label, Resolve) => C.InstrumentCard(Grid, { Label, Resolve, Quantity: 'Temperature' });
        Instrument('Coolant', () =>
        {
            const Value = Catalog.EvaluateCurve(ResolveProfile('CoolantTemperature'), 80, Specification, Profiles);
            const Reading = Format(Value, 'Temperature', 0);
            const Over = Value > Specification.Thermal.CoolantTarget + 6;
            const ValueTone = C.TemperatureTone(Value, Specification.Thermal.CoolantTarget - 12, Specification.Thermal.CoolantTarget + 6);
            return { Text: Reading.Text, Unit: Reading.Unit, ValueTone, Status: Over ? 'Above target' : 'At target',
                     Tone: Over ? 'danger' : 'live', Colour: '#e8a84a' };
        });
        Instrument('Oil', () =>
        {
            const Value = Catalog.EvaluateCurve(ResolveProfile('OilTemperature'), 6500, Specification, Profiles);
            const Reading = Format(Value, 'Temperature', 0);
            const Over = Value > Specification.Thermal.OilTarget + 6;
            const ValueTone = C.TemperatureTone(Value, Specification.Thermal.OilTarget - 12, Specification.Thermal.OilTarget + 6);
            return { Text: Reading.Text, Unit: Reading.Unit, ValueTone, Status: Over ? 'Above target' : 'At target',
                     Tone: Over ? 'danger' : 'live', Colour: '#d95656' };
        });
        Instrument('Brake disc', () =>
        {
            const Reading = Format(Specification.Thermal.BrakeTarget, 'Temperature', 0);
            const Over = Specification.Thermal.BrakeTarget > 680;
            const ValueTone = C.TemperatureTone(Specification.Thermal.BrakeTarget, 380, 680);
            return { Text: Reading.Text, Unit: Reading.Unit, ValueTone,
                     Status: Over ? 'Over the disc limit' : 'Within the disc limit',
                     Tone: Over ? 'danger' : 'live', Colour: '#c98f5a' };
        });
        Instrument('Tyre surface', () =>
        {
            const Tyre = ResolveTyre(SelectedTyreId) || Tyres[0];
            const Reading = Format(Tyre.SurfaceTemperature, 'Temperature', 0);
            const Window = Catalog.ResolveTemperatureWindow(Tyre.Compound, Tyre.SurfaceTemperature);
            const Tone = Window === 'Optimal' ? 'live' : Window === 'Cold' ? 'cold' : 'danger';
            const Bounds = Catalog.ResolveCompound(Tyre.Compound);
            const ValueTone = C.TemperatureTone(Tyre.SurfaceTemperature, Bounds.WindowLow, Bounds.WindowHigh);
            return { Text: Reading.Text, Unit: Reading.Unit, ValueTone, Status: `${Window} window · ${Tyre.Compound}`, Tone,
                     Colour: Catalog.ResolveCompound(Tyre.Compound).Colour };
        });
        Body.append(Grid);
        Body.append(C.Construct('div', 'sub-heading', 'Targets'));
        QuantityRow(Body, { Label: 'Coolant target', UnitKey: 'Temperature', Minimum: 70, Maximum: 120, Step: 1,
                            Read: () => Specification.Thermal.CoolantTarget, Write: Quantity => { Specification.Thermal.CoolantTarget = Quantity; } });
        QuantityRow(Body, { Label: 'Oil target', UnitKey: 'Temperature', Minimum: 80, Maximum: 150, Step: 1,
                            Read: () => Specification.Thermal.OilTarget, Write: Quantity => { Specification.Thermal.OilTarget = Quantity; } });
        QuantityRow(Body, { Label: 'Brake target', UnitKey: 'Temperature', Minimum: 300, Maximum: 780, Step: 5,
                            Read: () => Specification.Thermal.BrakeTarget, Write: Quantity => { Specification.Thermal.BrakeTarget = Quantity; } });
        QuantityRow(Body, { Label: 'Ambient', UnitKey: 'Temperature', Minimum: -10, Maximum: 48, Step: 1,
                            Read: () => Specification.Thermal.AmbientTemperature,
                            Write: Quantity => { Specification.Thermal.AmbientTemperature = Quantity; } });
        QuantityRow(Body, { Label: 'Cooling capacity', UnitKey: 'Power', Minimum: 10, Maximum: 140, Step: 1,
                            Read: () => Specification.Thermal.CoolingCapacity,
                            Write: Quantity => { Specification.Thermal.CoolingCapacity = Quantity; } });
        QuantityRow(Body, { Label: 'Thermostat opening', UnitKey: 'Temperature', Minimum: 60, Maximum: 100, Step: 1,
                            Read: () => Specification.Engine.ThermostatOpening,
                            Write: Quantity => { Specification.Engine.ThermostatOpening = Quantity; } });
        Body.append(C.Construct('div', 'sub-heading', 'Engine fluids and viscosity'));
        C.SegmentedRow(Body, {
            Label: 'Oil viscosity grade',
            Options: [['0W-20', '0W-20'], ['5W-30', '5W-30'], ['5W-40', '5W-40'], ['10W-60', '10W-60']],
            Read: () => Specification.Fluids.OilGrade,
            Write: Grade => { Specification.Fluids.OilGrade = Grade; }
        });
        QuantityRow(Body, { Label: 'Coolant heat capacity', UnitKey: 'Ratio', Minimum: 8, Maximum: 60, Step: 0.5, Digits: 1,
                            Read: () => Specification.Fluids.CoolantHeatCapacityKjK,
                            Write: Quantity => { Specification.Fluids.CoolantHeatCapacityKjK = Quantity; } });
        QuantityRow(Body, { Label: 'Radiator ram-air gain', UnitKey: 'Ratio', Minimum: 0.1, Maximum: 1.5, Step: 0.02, Digits: 2,
                            Read: () => Specification.Fluids.RadiatorAirflowGain,
                            Write: Quantity => { Specification.Fluids.RadiatorAirflowGain = Quantity; } });
        QuantityRow(Body, { Label: 'Electric fan cut-in', UnitKey: 'Temperature', Minimum: 78, Maximum: 112, Step: 1,
                            Read: () => Specification.Fluids.RadiatorFanThreshold,
                            Write: Quantity => { Specification.Fluids.RadiatorFanThreshold = Quantity; } });
        QuantityRow(Body, { Label: 'Overheat limp derate', UnitKey: 'Temperature', Minimum: 102, Maximum: 138, Step: 1,
                            Read: () => Specification.Fluids.OverheatDerateStart,
                            Write: Quantity => { Specification.Fluids.OverheatDerateStart = Quantity; } });
        QuantityRow(Body, { Label: 'Brake-specific fuel burn', UnitKey: 'Ratio', Minimum: 185, Maximum: 360, Step: 1,
                            Read: () => Specification.Fluids.SpecificFuelBurnGkwh,
                            Write: Quantity => { Specification.Fluids.SpecificFuelBurnGkwh = Quantity; } });

        Body.append(C.Construct('div', 'sub-heading', 'Coolant curve'));
        const Host = C.Construct('div', 'curve-editor-host');
        Body.append(Host);
        C.CurveEditor(Host, {
            Profile: ResolveProfile('CoolantTemperature'), Height: 172,
            Specification, Profiles, UnitSystem: () => UnitSystem,
            Selection: { Read: () => PointSelections.CoolantTemperature ?? 0, Write: Index => { PointSelections.CoolantTemperature = Index; } },
            OnEdit: () => { AppendLog('THERMAL', 'Coolant curve edited', 'thermal targets unchanged', 'live'); }
        });
    }

    function ConstructTyres(Page)
    {
        const Section = DeclareSection(Page, {
            Id: 'Tyres', Title: 'Tyres', Tab: 'Tyres', Glyph: 'Tyre', Colour: '#8fc7b8',
            Status: () => `${EnclosedTyres().length} enclosed · ${DetachedTyres().length} detached`
        });
        const Body = Section.Inner;
        Body.append(C.Construct('p', 'section-caption', 'Each tyre is its own record enclosed by the vehicle. Detaching one clears the '
            + 'enclosure and parks it in the pit lane; fitting a set replaces the profiles and writes them back.'));

        const Cards = C.Construct('div', 'tyre-grid');
        Body.append(Cards);
        for (const Tyre of Tyres)
        {
            C.InstrumentCard(Cards, {
                Label: `${Tyre.Code} · ${Tyre.Compound}`, Quantity: 'Pressure',
                onSelect: () => { SelectedTyreId = Tyre.Id; C.Broadcast(); },
                Resolve: () =>
                {
                    const Pressure = Format(Tyre.Pressure, 'Pressure', 0);
                    const Window = Catalog.ResolveTemperatureWindow(Tyre.Compound, Tyre.SurfaceTemperature);
                    const WindowBounds = Catalog.ResolveCompound(Tyre.Compound);
                    const Tone = Tyre.Enclosed ? (Window === 'Optimal' ? 'live' : Window === 'Cold' ? 'cold' : 'danger') : 'muted';
                    // 📝 The pressure is the card's number; its colour follows the tyre's surface temperature, not the pressure.
                    const ValueTone = Tyre.Enclosed ? C.TemperatureTone(Tyre.SurfaceTemperature, WindowBounds.WindowLow, WindowBounds.WindowHigh) : null;
                    return {
                        Name: `${Tyre.Code} · ${Tyre.Compound}`,
                        Text: Pressure.Text, Unit: Pressure.Unit, ValueTone,
                        Status: Tyre.Enclosed ? `${DisplayText(Tyre.SurfaceTemperature, 'Temperature', 0)} ${UnitSymbol('Temperature')} · `
                            + `${DisplayText(Tyre.Wear, 'Percent', 0)} % wear` : 'Detached · in the pit lane',
                        Tone, Colour: Catalog.ResolveCompound(Tyre.Compound).Colour, Selected: SelectedTyreId === Tyre.Id
                    };
                }
            });
        }

        const FitA = C.Construct('button', 'pill-button accent', [C.ConstructGlyph('Tyre'), C.Construct('span', null, 'Fit set A · medium')]);
        const FitB = C.Construct('button', 'pill-button', [C.ConstructGlyph('Tyre'), C.Construct('span', null, 'Fit set B · soft')]);
        const FitWet = C.Construct('button', 'pill-button', [C.ConstructGlyph('Tyre'), C.Construct('span', null, 'Fit set C · wet')]);
        const DetachAll = C.Construct('button', 'pill-button', [C.ConstructGlyph('Flag'), C.Construct('span', null, 'Detach all four')]);
        const Refit = C.Construct('button', 'pill-button', [C.ConstructGlyph('Check'), C.Construct('span', null, 'Re-attach detached')]);
        for (const Button of [FitA, FitB, FitWet, DetachAll, Refit]) Button.type = 'button';
        const Fit = (SetId, Compound, Label) =>
        {
            Catalog.FitTyreSet(Tyres, SetId, Compound);
            SelectedTyreId = Tyres[0].Id;
            AppendLog('PIT', `${Label} fitted`, 'four tyres enclosed · wear reset to 0 %', 'live');
            C.Broadcast();
        };
        FitA.addEventListener('click', () => Fit('SetA', 'Medium', 'Set A · medium'));
        FitB.addEventListener('click', () => Fit('SetB', 'Soft', 'Set B · soft'));
        FitWet.addEventListener('click', () => Fit('SetC', 'Wet', 'Set C · wet'));
        DetachAll.addEventListener('click', () =>
        {
            for (const Tyre of Tyres) Catalog.DetachTyre(Tyre);
            AppendLog('PIT', 'All four tyres detached', 'records move to the pit lane, enclosure cleared', 'warm');
            C.Broadcast();
        });
        Refit.addEventListener('click', () =>
        {
            const Detached = DetachedTyres();
            if (!Detached.length) { C.Toast('Nothing is detached'); return; }
            for (const Tyre of Detached) Catalog.AttachTyre(Tyre);
            AppendLog('PIT', `${Detached.length} tyres re-attached`, 'enclosure restored', 'live');
            C.Broadcast();
        });
        Body.append(C.Construct('div', 'action-row wrap', [FitA, FitB, FitWet, DetachAll, Refit]));

        Body.append(C.Construct('div', 'sub-heading', 'Selected tyre'));
        const Selected = () => ResolveTyre(SelectedTyreId) || Tyres[0];
        const SelectedName = C.Construct('div', 'point-caption');
        Body.append(SelectedName);
        C.Register(() =>
        {
            const Tyre = Selected();
            SelectedName.textContent = `${Tyre.Position} · ${Tyre.Compound} · ${Tyre.Enclosed ? 'enclosed by GT-04' : 'detached, in the pit lane'}`;
        });
        C.SwitchRow(Body, {
            Label: 'Enclosed by the vehicle',
            Detail: 'Tyres are records; the enclosure is what makes one part of the vehicle.',
            Read: () => Selected().Enclosed,
            Write: Enabled =>
            {
                const Tyre = Selected();
                if (Enabled) Catalog.AttachTyre(Tyre); else Catalog.DetachTyre(Tyre);
                AppendLog('PIT', `Tyre ${Tyre.Code} ${Enabled ? 'enclosed' : 'detached'}`, Enabled ? 'part of the vehicle again' : 'parked in the pit lane',
                          Enabled ? 'live' : 'warm');
            }
        });
        C.SegmentedRow(Body, {
            Label: 'Compound',
            Options: [['Soft', 'Soft'], ['Medium', 'Medium'], ['Hard', 'Hard'], ['Inter', 'Intermediate'], ['Wet', 'Wet']],
            Read: () => Selected().Compound,
            Write: Compound => { Selected().Compound = Compound; }
        });
        QuantityRow(Body, { Label: 'Cold pressure', UnitKey: 'Pressure', Minimum: 120, Maximum: 260, Step: 1,
                            Read: () => Selected().Pressure, Write: Quantity => { Selected().Pressure = Quantity; } });
        QuantityRow(Body, { Label: 'Surface temperature', UnitKey: 'Temperature', Minimum: 20, Maximum: 160, Step: 1,
                            Read: () => Selected().SurfaceTemperature, Write: Quantity => { Selected().SurfaceTemperature = Quantity; } });
        QuantityRow(Body, { Label: 'Wear', UnitKey: 'Percent', Minimum: 0, Maximum: 100, Step: 1,
                            Read: () => Selected().Wear, Write: Quantity => { Selected().Wear = Quantity; } });
        const Open = C.Construct('button', 'pill-button accent', [C.ConstructGlyph('Tyre'), C.Construct('span', null, 'Open the tyre inspector')]);
        Open.type = 'button';
        Open.addEventListener('click', () => SelectRecord(Selected().Id));
        Body.append(C.Construct('div', 'action-row', [Open]));
        Body.append(C.Construct('p', 'footer-note', 'This prototype ends at the vehicle record. The tyre editor — carcass, thermal '
            + 'windows, wear curves per compound and the pit-stop history — is the next step, after this inspector is approved.'));
    }

    function ConstructAero(Page)
    {
        const Section = DeclareSection(Page, {
            Id: 'Aero', Title: 'Aero and brakes', Tab: 'Aero', Glyph: 'Wind', Colour: '#8fc7b8',
            Status: () => `Cd ${Specification.Drivetrain.DragCoefficient.toFixed(2)}`
        });
        const Body = Section.Inner;
        const Grid = C.Construct('div', 'stat-grid');
        QuantityReadout(Grid, { Label: 'Downforce at 200', UnitKey: 'Force', Digits: 0, Read: () => Specification.Drivetrain.DownforceAt200 });
        QuantityReadout(Grid, { Label: 'Brake torque', UnitKey: 'Torque', Digits: 0, Read: () => Specification.Drivetrain.BrakeTorque });
        QuantityReadout(Grid, { Label: '100–0 distance', UnitKey: 'Distance', Digits: 1, Read: () => ResolvePerformance().BrakingDistance });
        Body.append(Grid);
        QuantityRow(Body, { Label: 'Drag coefficient', UnitKey: 'Ratio', Minimum: 0.18, Maximum: 0.60, Step: 0.01, Digits: 2,
                            Read: () => Specification.Drivetrain.DragCoefficient,
                            Write: Quantity => { Specification.Drivetrain.DragCoefficient = Quantity; } });
        QuantityRow(Body, { Label: 'Frontal area', UnitKey: 'Area', Minimum: 1.2, Maximum: 3.2, Step: 0.01, Digits: 2,
                            Read: () => Specification.Drivetrain.FrontalArea, Write: Quantity => { Specification.Drivetrain.FrontalArea = Quantity; } });
        QuantityRow(Body, { Label: 'Downforce at 200 km/h', UnitKey: 'Force', Minimum: 0, Maximum: 2000, Step: 10,
                            Read: () => Specification.Drivetrain.DownforceAt200,
                            Write: Quantity => { Specification.Drivetrain.DownforceAt200 = Quantity; } });
        QuantityRow(Body, { Label: 'Total brake torque', UnitKey: 'Torque', Minimum: 800, Maximum: 5000, Step: 25,
                            Read: () => Specification.Drivetrain.BrakeTorque, Write: Quantity => { Specification.Drivetrain.BrakeTorque = Quantity; } });
        QuantityRow(Body, { Label: 'Brake balance to the front', UnitKey: 'Percent', Minimum: 45, Maximum: 75, Step: 1,
                            Read: () => Specification.Drivetrain.BrakeBalance,
                            Write: Quantity => { Specification.Drivetrain.BrakeBalance = Quantity; } });
        QuantityRow(Body, { Label: 'Rolling resistance', UnitKey: 'Ratio', Minimum: 0.006, Maximum: 0.030, Step: 0.001, Digits: 3,
                            Read: () => Specification.Drivetrain.RollingResistance,
                            Write: Quantity => { Specification.Drivetrain.RollingResistance = Quantity; } });
        QuantityRow(Body, { Label: 'Wing trim', UnitKey: 'Ratio', Minimum: 0, Maximum: 1, Step: 0.05, Digits: 2,
                            Tags: [['TRIM', 'muted']],
                            Read: () => Math.min(1, Specification.Drivetrain.DownforceAt200 / 1600),
                            Write: Quantity => { Specification.Drivetrain.DownforceAt200 = Quantity * 1600; } });

        Body.append(C.Construct('div', 'sub-heading', 'Active aero and ground effect'));
        QuantityRow(Body, { Label: 'Front aero balance (CoP)', UnitKey: 'Percent', Minimum: 30, Maximum: 65, Step: 1,
                            Read: () => Specification.AeroActive.CenterOfPressureFrontPercent,
                            Write: Quantity => { Specification.AeroActive.CenterOfPressureFrontPercent = Quantity; } });
        QuantityRow(Body, { Label: 'Pitch CoP sensitivity', UnitKey: 'Ratio', Minimum: 0.0, Maximum: 3.0, Step: 0.05, Digits: 2,
                            Read: () => Specification.AeroActive.PitchSensitivity,
                            Write: Quantity => { Specification.AeroActive.PitchSensitivity = Quantity; } });
        QuantityRow(Body, { Label: 'DRS drag reduction', UnitKey: 'Percent', Minimum: 0, Maximum: 45, Step: 1,
                            Read: () => Specification.AeroActive.DrsDragReductionPercent,
                            Write: Quantity => { Specification.AeroActive.DrsDragReductionPercent = Quantity; } });
        QuantityRow(Body, { Label: 'DRS downforce loss', UnitKey: 'Percent', Minimum: 0, Maximum: 60, Step: 1,
                            Read: () => Specification.AeroActive.DrsDownforceLossPercent,
                            Write: Quantity => { Specification.AeroActive.DrsDownforceLossPercent = Quantity; } });
        QuantityRow(Body, { Label: 'DRS minimum speed', UnitKey: 'Speed', Minimum: 60, Maximum: 220, Step: 5,
                            Read: () => Specification.AeroActive.DrsActivationSpeed,
                            Write: Quantity => { Specification.AeroActive.DrsActivationSpeed = Quantity; } });
        QuantityRow(Body, { Label: 'Airbrake extra Cd', UnitKey: 'Ratio', Minimum: 0.0, Maximum: 0.65, Step: 0.01, Digits: 2,
                            Read: () => Specification.AeroActive.AirbrakeDragAddition,
                            Write: Quantity => { Specification.AeroActive.AirbrakeDragAddition = Quantity; } });
        QuantityRow(Body, { Label: 'Airbrake downforce at 200', UnitKey: 'Force', Minimum: 0, Maximum: 900, Step: 10,
                            Read: () => Specification.AeroActive.AirbrakeDownforceGain,
                            Write: Quantity => { Specification.AeroActive.AirbrakeDownforceGain = Quantity; } });
        QuantityRow(Body, { Label: 'Venturi optimal height', UnitKey: 'Length', Minimum: 35, Maximum: 140, Step: 1,
                            Read: () => Specification.AeroActive.GroundEffectOptimalHeight,
                            Write: Quantity => { Specification.AeroActive.GroundEffectOptimalHeight = Quantity; } });
        QuantityRow(Body, { Label: 'Venturi peak suction gain', UnitKey: 'Ratio', Minimum: 1.0, Maximum: 2.2, Step: 0.02, Digits: 2,
                            Read: () => Specification.AeroActive.GroundEffectPeakGain,
                            Write: Quantity => { Specification.AeroActive.GroundEffectPeakGain = Quantity; } });
        QuantityRow(Body, { Label: 'Diffuser choke height', UnitKey: 'Length', Minimum: 10, Maximum: 55, Step: 1,
                            Read: () => Specification.AeroActive.GroundEffectChokeHeight,
                            Write: Quantity => { Specification.AeroActive.GroundEffectChokeHeight = Quantity; } });

        Body.append(C.Construct('div', 'sub-heading', 'Brake fade and staging'));
        QuantityRow(Body, { Label: 'Rear handbrake torque', UnitKey: 'Torque', Minimum: 400, Maximum: 3800, Step: 25,
                            Read: () => Specification.AeroActive.HandbrakeTorque,
                            Write: Quantity => { Specification.AeroActive.HandbrakeTorque = Quantity; } });
        QuantityRow(Body, { Label: 'Pad fade onset', UnitKey: 'Temperature', Minimum: 380, Maximum: 820, Step: 5,
                            Read: () => Specification.AeroActive.BrakeFadeStart,
                            Write: Quantity => { Specification.AeroActive.BrakeFadeStart = Quantity; } });
        QuantityRow(Body, { Label: 'Peak thermal fade loss', UnitKey: 'Percent', Minimum: 0, Maximum: 80, Step: 1,
                            Read: () => Specification.AeroActive.MaxBrakeFadePercent,
                            Write: Quantity => { Specification.AeroActive.MaxBrakeFadePercent = Quantity; } });
        C.SwitchRow(Body, {
            Label: 'Front line-lock staging',
            Detail: 'Holds front brake callipers while leaving rear axle unbraked for tyre warm-up.',
            Read: () => Specification.AeroActive.LineLockFrontOnly,
            Write: Enabled => { Specification.AeroActive.LineLockFrontOnly = Enabled; }
        });
    }

    function ConstructSuspension(Page)
    {
        const Section = DeclareSection(Page, {
            Id: 'Suspension', Title: 'Suspension and steering', Tab: 'Suspension', Glyph: 'Sliders', Colour: '#e8a84a',
            Status: () => `${DisplayText(Specification.Suspension.FrontSpringRate, 'Stiffness', 0)} / `
                + `${DisplayText(Specification.Suspension.RearSpringRate, 'Stiffness', 0)} ${UnitSymbol('Stiffness')}`
        });
        const Body = Section.Inner;
        Body.append(C.Construct('p', 'section-caption', 'Progressive coilovers, digressive two-slope dampers, anti-roll bars, '
            + 'Ackermann rack geometry and closed-loop yaw/slip thresholds.'));

        Body.append(C.Construct('div', 'sub-heading', 'Coilover springs and travel'));
        QuantityRow(Body, { Label: 'Front spring stiffness', UnitKey: 'Stiffness', Minimum: 15, Maximum: 140, Step: 1,
                            Read: () => Specification.Suspension.FrontSpringRate,
                            Write: Quantity => { Specification.Suspension.FrontSpringRate = Quantity; } });
        QuantityRow(Body, { Label: 'Rear spring stiffness', UnitKey: 'Stiffness', Minimum: 15, Maximum: 140, Step: 1,
                            Read: () => Specification.Suspension.RearSpringRate,
                            Write: Quantity => { Specification.Suspension.RearSpringRate = Quantity; } });
        QuantityRow(Body, { Label: 'Progressive hardening', UnitKey: 'Ratio', Minimum: 0.0, Maximum: 1.5, Step: 0.02, Digits: 2,
                            Read: () => Specification.Suspension.ProgressiveFactor,
                            Write: Quantity => { Specification.Suspension.ProgressiveFactor = Quantity; } });
        QuantityRow(Body, { Label: 'Front unloaded strut', UnitKey: 'Length', Minimum: 240, Maximum: 460, Step: 2,
                            Read: () => Specification.Suspension.FrontRestLength,
                            Write: Quantity => { Specification.Suspension.FrontRestLength = Quantity; } });
        QuantityRow(Body, { Label: 'Rear unloaded strut', UnitKey: 'Length', Minimum: 240, Maximum: 460, Step: 2,
                            Read: () => Specification.Suspension.RearRestLength,
                            Write: Quantity => { Specification.Suspension.RearRestLength = Quantity; } });
        QuantityRow(Body, { Label: 'Bump-stop stiffness', UnitKey: 'Stiffness', Minimum: 60, Maximum: 400, Step: 5,
                            Read: () => Specification.Suspension.BumpStopStiffness,
                            Write: Quantity => { Specification.Suspension.BumpStopStiffness = Quantity; } });

        Body.append(C.Construct('div', 'sub-heading', 'Digressive dampers and anti-roll'));
        QuantityRow(Body, { Label: 'Front bump damping', UnitKey: 'Damping', Minimum: 1200, Maximum: 12000, Step: 100,
                            Read: () => Specification.Suspension.FrontBumpDamping,
                            Write: Quantity => { Specification.Suspension.FrontBumpDamping = Quantity; } });
        QuantityRow(Body, { Label: 'Front rebound damping', UnitKey: 'Damping', Minimum: 1500, Maximum: 15000, Step: 100,
                            Read: () => Specification.Suspension.FrontReboundDamping,
                            Write: Quantity => { Specification.Suspension.FrontReboundDamping = Quantity; } });
        QuantityRow(Body, { Label: 'Rear bump damping', UnitKey: 'Damping', Minimum: 1200, Maximum: 12000, Step: 100,
                            Read: () => Specification.Suspension.RearBumpDamping,
                            Write: Quantity => { Specification.Suspension.RearBumpDamping = Quantity; } });
        QuantityRow(Body, { Label: 'Rear rebound damping', UnitKey: 'Damping', Minimum: 1500, Maximum: 15000, Step: 100,
                            Read: () => Specification.Suspension.RearReboundDamping,
                            Write: Quantity => { Specification.Suspension.RearReboundDamping = Quantity; } });
        QuantityRow(Body, { Label: 'High-speed blow-off ratio', UnitKey: 'Ratio', Minimum: 0.15, Maximum: 1.0, Step: 0.02, Digits: 2,
                            Read: () => Specification.Suspension.HighSpeedDampingRatio,
                            Write: Quantity => { Specification.Suspension.HighSpeedDampingRatio = Quantity; } });
        QuantityRow(Body, { Label: 'Front anti-roll bar', UnitKey: 'Stiffness', Minimum: 0, Maximum: 65, Step: 1,
                            Read: () => Specification.Suspension.FrontAntiRollRate,
                            Write: Quantity => { Specification.Suspension.FrontAntiRollRate = Quantity; } });
        QuantityRow(Body, { Label: 'Rear anti-roll bar', UnitKey: 'Stiffness', Minimum: 0, Maximum: 65, Step: 1,
                            Read: () => Specification.Suspension.RearAntiRollRate,
                            Write: Quantity => { Specification.Suspension.RearAntiRollRate = Quantity; } });
        QuantityRow(Body, { Label: 'Front static camber', UnitKey: 'Degree', Minimum: -5.0, Maximum: 1.0, Step: 0.1, Digits: 1,
                            Read: () => Specification.Suspension.FrontStaticCamber,
                            Write: Quantity => { Specification.Suspension.FrontStaticCamber = Quantity; } });
        QuantityRow(Body, { Label: 'Rear static camber', UnitKey: 'Degree', Minimum: -5.0, Maximum: 1.0, Step: 0.1, Digits: 1,
                            Read: () => Specification.Suspension.RearStaticCamber,
                            Write: Quantity => { Specification.Suspension.RearStaticCamber = Quantity; } });

        Body.append(C.Construct('div', 'sub-heading', 'Ackermann steering and active stability'));
        QuantityRow(Body, { Label: 'Max inner wheel lock', UnitKey: 'Degree', Minimum: 20, Maximum: 52, Step: 1,
                            Read: () => Specification.Steering.MaxInnerAngle,
                            Write: Quantity => { Specification.Steering.MaxInnerAngle = Quantity; } });
        QuantityRow(Body, { Label: 'Steering rack ratio', UnitKey: 'Ratio', Minimum: 8.5, Maximum: 20.0, Step: 0.1, Digits: 1,
                            Read: () => Specification.Steering.SteeringRatio,
                            Write: Quantity => { Specification.Steering.SteeringRatio = Quantity; } });
        QuantityRow(Body, { Label: 'Ackermann geometry', UnitKey: 'Percent', Minimum: 0, Maximum: 120, Step: 1,
                            Read: () => Specification.Steering.AckermannPercent,
                            Write: Quantity => { Specification.Steering.AckermannPercent = Quantity; } });
        QuantityRow(Body, { Label: 'High-speed steer floor', UnitKey: 'Percent', Minimum: 15, Maximum: 100, Step: 1,
                            Read: () => Specification.Steering.SpeedSensitiveFloorPercent,
                            Write: Quantity => { Specification.Steering.SpeedSensitiveFloorPercent = Quantity; } });
        QuantityRow(Body, { Label: 'Caster mechanical trail', UnitKey: 'Length', Minimum: 5, Maximum: 65, Step: 1,
                            Read: () => Specification.Steering.MechanicalTrail,
                            Write: Quantity => { Specification.Steering.MechanicalTrail = Quantity; } });
        QuantityRow(Body, { Label: 'ESC yaw-rate gain', UnitKey: 'Ratio', Minimum: 0.0, Maximum: 2.0, Step: 0.05, Digits: 2,
                            Read: () => Specification.Steering.EscYawGain,
                            Write: Quantity => { Specification.Steering.EscYawGain = Quantity; } });
        QuantityRow(Body, { Label: 'TCS target slip', UnitKey: 'Percent', Minimum: 4, Maximum: 28, Step: 1,
                            Read: () => Specification.Steering.TcsSlipThresholdPercent,
                            Write: Quantity => { Specification.Steering.TcsSlipThresholdPercent = Quantity; } });
        QuantityRow(Body, { Label: 'ABS target slip', UnitKey: 'Percent', Minimum: 6, Maximum: 30, Step: 1,
                            Read: () => Specification.Steering.AbsSlipThresholdPercent,
                            Write: Quantity => { Specification.Steering.AbsSlipThresholdPercent = Quantity; } });
    }

    function ConstructSoftBody(Page)
    {
        const Section = DeclareSection(Page, {
            Id: 'SoftBody', Title: 'Soft-body tyre and slip', Tab: 'Soft-body', Glyph: 'Tyre', Colour: '#8fc7b8',
            Status: () => `${Specification.SoftBody.Formulation} · ${Specification.SoftBody.CircumferentialNodes}×${Specification.SoftBody.LateralRings}`
        });
        const Body = Section.Inner;
        Body.append(C.Construct('p', 'section-caption', 'Selects the 3D deformable tyre formulation (Radial Spring, XPBD continuum or '
            + 'Hex8 Neo-Hookean 3D FEM) and the transient low-speed slip solver coupled to Pacejka MF6.1.'));

        C.SegmentedRow(Body, {
            Label: 'Soft-body tyre formulation',
            Options: [['Spring', 'Spring'], ['XPBD', 'Xpbd'], ['3D Hex8 FEM', 'Fem3D']],
            Read: () => Specification.SoftBody.Formulation,
            Write: Mode => { Specification.SoftBody.Formulation = Mode; }
        });
        C.SegmentedRow(Body, {
            Label: 'Transient slip solver',
            Options: [['GRIT implicit', 'GritImplicitNewton'], ['Relaxation', 'RelaxationLength'], ['Quasi-static', 'QuasiStaticNewton']],
            Read: () => Specification.SoftBody.SlipMode,
            Write: Mode => { Specification.SoftBody.SlipMode = Mode; }
        });

        Body.append(C.Construct('div', 'sub-heading', 'Carcass discretization and XPBD'));
        QuantityRow(Body, { Label: 'Circumferential nodes', UnitKey: 'Ratio', Minimum: 12, Maximum: 32, Step: 2,
                            Read: () => Specification.SoftBody.CircumferentialNodes,
                            Write: Quantity => { Specification.SoftBody.CircumferentialNodes = Quantity; } });
        QuantityRow(Body, { Label: 'Lateral tread rings', UnitKey: 'Ratio', Minimum: 2, Maximum: 5, Step: 1,
                            Read: () => Specification.SoftBody.LateralRings,
                            Write: Quantity => { Specification.SoftBody.LateralRings = Quantity; } });
        QuantityRow(Body, { Label: 'Reference radial stiffness', UnitKey: 'Stiffness', Minimum: 90, Maximum: 380, Step: 5,
                            Read: () => Specification.SoftBody.VerticalStiffness,
                            Write: Quantity => { Specification.SoftBody.VerticalStiffness = Quantity; } });
        QuantityRow(Body, { Label: 'Pneumatic stiffness gain', UnitKey: 'Ratio', Minimum: 0.2, Maximum: 2.0, Step: 0.05, Digits: 2,
                            Read: () => Specification.SoftBody.PressureStiffnessGain,
                            Write: Quantity => { Specification.SoftBody.PressureStiffnessGain = Quantity; } });
        QuantityRow(Body, { Label: 'Pneumatic volume preservation', UnitKey: 'Percent', Minimum: 0, Maximum: 100, Step: 1,
                            Read: () => Specification.SoftBody.PneumaticVolumeWeight,
                            Write: Quantity => { Specification.SoftBody.PneumaticVolumeWeight = Quantity; } });
        QuantityRow(Body, { Label: 'XPBD substeps', UnitKey: 'Ratio', Minimum: 1, Maximum: 12, Step: 1,
                            Read: () => Specification.SoftBody.XpbdSubsteps,
                            Write: Quantity => { Specification.SoftBody.XpbdSubsteps = Quantity; } });
        QuantityRow(Body, { Label: 'XPBD constraint iterations', UnitKey: 'Ratio', Minimum: 1, Maximum: 12, Step: 1,
                            Read: () => Specification.SoftBody.XpbdIterations,
                            Write: Quantity => { Specification.SoftBody.XpbdIterations = Quantity; } });
        QuantityRow(Body, { Label: 'Sidewall contact bulge', UnitKey: 'Percent', Minimum: 0, Maximum: 45, Step: 1,
                            Read: () => Specification.SoftBody.MaxContactBulgePercent,
                            Write: Quantity => { Specification.SoftBody.MaxContactBulgePercent = Quantity; } });

        Body.append(C.Construct('div', 'sub-heading', '3D Hex8 Neo-Hookean finite elements'));
        QuantityRow(Body, { Label: 'FEM explicit substeps', UnitKey: 'Ratio', Minimum: 4, Maximum: 32, Step: 1,
                            Read: () => Specification.SoftBody.FemSubsteps,
                            Write: Quantity => { Specification.SoftBody.FemSubsteps = Quantity; } });
        QuantityRow(Body, { Label: 'Carcass Young modulus', UnitKey: 'Modulus', Minimum: 0.8, Maximum: 8.0, Step: 0.1, Digits: 1,
                            Read: () => Specification.SoftBody.FemYoungsModulusMpa,
                            Write: Quantity => { Specification.SoftBody.FemYoungsModulusMpa = Quantity; } });
        QuantityRow(Body, { Label: 'Rubber Poisson ratio', UnitKey: 'Ratio', Minimum: 0.30, Maximum: 0.47, Step: 0.005, Digits: 3,
                            Read: () => Specification.SoftBody.FemPoissonRatio,
                            Write: Quantity => { Specification.SoftBody.FemPoissonRatio = Quantity; } });
        QuantityRow(Body, { Label: 'FEM lumped vertex mass', UnitKey: 'Mass', Minimum: 0.05, Maximum: 0.45, Step: 0.01, Digits: 2,
                            Read: () => Specification.SoftBody.FemNodeMassKg,
                            Write: Quantity => { Specification.SoftBody.FemNodeMassKg = Quantity; } });
        QuantityRow(Body, { Label: 'Rayleigh damping rate', UnitKey: 'Frequency', Minimum: 2, Maximum: 45, Step: 1,
                            Read: () => Specification.SoftBody.FemDampingRate,
                            Write: Quantity => { Specification.SoftBody.FemDampingRate = Quantity; } });
    }

    function ConstructSimulation(Page)
    {
        const Section = DeclareSection(Page, {
            Id: 'Simulation', Title: 'Jolt adapter and physics thread', Tab: 'Physics', Glyph: 'Activity', Colour: '#9fb4d6',
            Status: () => `${Specification.Simulation.OuterTickHz * Specification.Simulation.SubstepsPerOuterTick} Hz inner`
        });
        const Body = Section.Inner;
        Body.append(C.Construct('p', 'section-caption', 'Configures PhysicsExecutionThread and JoltAdapter: outer fixed-step accumulator, '
            + 'deterministic N-way substepping, SLERP pose interpolation and world force-zone coupling.'));
        const Stats = C.Construct('div', 'stat-grid');
        QuantityReadout(Stats, { Label: 'Outer rate', UnitKey: 'Frequency', Digits: 0, Read: () => Specification.Simulation.OuterTickHz });
        QuantityReadout(Stats, { Label: 'Inner rate', UnitKey: 'Frequency', Digits: 0,
                                 Read: () => Specification.Simulation.OuterTickHz * Specification.Simulation.SubstepsPerOuterTick });
        QuantityReadout(Stats, { Label: 'Inner dt', UnitKey: 'Milliseconds', Digits: 2,
                                 Read: () => 1000 / (Specification.Simulation.OuterTickHz * Specification.Simulation.SubstepsPerOuterTick) });
        Body.append(Stats);
        QuantityRow(Body, { Label: 'Outer fixed tick rate', UnitKey: 'Frequency', Minimum: 30, Maximum: 240, Step: 10,
                            Read: () => Specification.Simulation.OuterTickHz,
                            Write: Quantity => { Specification.Simulation.OuterTickHz = Quantity; } });
        QuantityRow(Body, { Label: 'Substeps per outer tick', UnitKey: 'Ratio', Minimum: 1, Maximum: 8, Step: 1,
                            Read: () => Specification.Simulation.SubstepsPerOuterTick,
                            Write: Quantity => { Specification.Simulation.SubstepsPerOuterTick = Quantity; } });
        QuantityRow(Body, { Label: 'Max catch-up outer steps', UnitKey: 'Ratio', Minimum: 1, Maximum: 16, Step: 1,
                            Read: () => Specification.Simulation.MaximumCatchUpSteps,
                            Write: Quantity => { Specification.Simulation.MaximumCatchUpSteps = Quantity; } });
        C.SwitchRow(Body, {
            Label: 'Coupled Coulomb-cone slope hold',
            Detail: 'Engages static PGS hold on grades when speed is near zero and brake + friction hold gravity.',
            Read: () => Specification.Simulation.StationarityHold,
            Write: Enabled => { Specification.Simulation.StationarityHold = Enabled; }
        });
        C.SwitchRow(Body, {
            Label: 'World force-zone aerodynamic coupling',
            Detail: 'Couples UniformWind, VortexFunnel and RadialImpulse zones across all occupants and vehicles.',
            Read: () => Specification.Simulation.ForceZoneCoupling,
            Write: Enabled => { Specification.Simulation.ForceZoneCoupling = Enabled; }
        });
    }

    function ConstructPerformance(Page)
    {
        const Section = DeclareSection(Page, {
            Id: 'Performance', Title: 'Performance estimate', Tab: 'Performance', Glyph: 'Gauge', Colour: '#c9b8f0',
            Status: () => `${DisplayText(ResolvePerformance().ZeroToHundred, 'Seconds', 2)} s`
        });
        const Body = Section.Inner;
        Body.append(C.Construct('p', 'section-caption', 'A longitudinal model of the record: torque curve, gearing, traction circle with '
            + 'weight transfer, drag and rolling resistance. It is a prototype estimate, not the C++ tyre solver.'));
        const Grid = C.Construct('div', 'tile-grid four');
        QuantityTile(Grid, { Label: '0–100 km/h', Glyph: 'Clock', UnitKey: 'Seconds', Digits: 2,
                             Read: () => ResolvePerformance().ZeroToHundred });
        QuantityTile(Grid, { Label: 'Top speed', Glyph: 'Road', UnitKey: 'Speed', Digits: 0, Read: () => ResolvePerformance().TopSpeed });
        QuantityTile(Grid, { Label: '100–0 braking', Glyph: 'Brake', UnitKey: 'Distance', Digits: 1,
                             Read: () => ResolvePerformance().BrakingDistance });
        QuantityTile(Grid, { Label: 'Peak power', Glyph: 'Zap', UnitKey: 'Power', Digits: 0, Read: () => ResolvePerformance().PeakPower });
        Body.append(Grid);
        const Trace = [];
        const TimeAxis = { Minimum: 0, Maximum: 4, MajorStep: 1, Label: 'Elapsed', Unit: 'Seconds' };
        const SpeedAxis = { Minimum: 0, Maximum: 120, MajorStep: 30, Label: 'Road speed', Unit: 'Speed' };
        C.Register(() =>
        {
            const Performance = ResolvePerformance();
            Trace.length = 0;
            for (const Point of Performance.LaunchTrace) Trace.push(Point);
            TimeAxis.Maximum = Math.max(1, Math.ceil(Performance.ZeroToHundred * 1.2));
        });
        C.SeriesChart(Body, {
            Height: 168, UnitSystem: () => UnitSystem,
            XAxis: TimeAxis,
            Series: [{ Id: 'Launch', Points: Trace, Axis: SpeedAxis, Colour: '#c9b8f0', Area: true, Width: 1.6 }],
            Target: { Y: 100, Label: '100 km/h' },
            CaptionText: 'Launch trace from standstill in first gear; the estimate shifts at the limiter and stops at 100 km/h.'
        });
        const Stats = C.Construct('div', 'stat-grid');
        QuantityReadout(Stats, { Label: 'Distance to 100', UnitKey: 'Distance', Digits: 1, Read: () => ResolvePerformance().LaunchDistance });
        QuantityReadout(Stats, { Label: 'Shifts', UnitKey: 'Ratio', Digits: 0, Read: () => ResolvePerformance().ShiftCount });
        QuantityReadout(Stats, { Label: 'Power to weight', UnitKey: 'SpecificPower', Digits: 0, Read: () => ResolvePerformance().PowerToWeight });
        Body.append(Stats);
    }

    function ConstructRecord(Page)
    {
        const Section = DeclareSection(Page, {
            Id: 'Record', Title: 'Vehicle record', Tab: 'Record', Glyph: 'Table', Colour: '#9fb4d6',
            Status: () => 'TOML'
        });
        const Body = Section.Inner;
        Body.append(C.Construct('p', 'section-caption', 'The record the scene writes. Curves travel as point arrays, so a custom '
            + 'vehicle is a text file — the same shape the C++ VehicleSpecification will read.'));
        const Block = C.Construct('pre', 'levelspace');
        const Reset = C.Construct('button', 'pill-button', [C.ConstructGlyph('Reset'), C.Construct('span', null, 'Reset to the baseline')]);
        const Copy = C.Construct('button', 'pill-button accent', [C.ConstructGlyph('Copy'), C.Construct('span', null, 'Copy record')]);
        const Csv = C.Construct('button', 'pill-button', [C.ConstructGlyph('Activity'), C.Construct('span', null, 'Copy torque CSV')]);
        for (const Button of [Reset, Copy, Csv]) Button.type = 'button';
        Reset.addEventListener('click', () =>
        {
            Object.assign(Specification.Identity, JSON.parse(JSON.stringify(Baseline.Specification.Identity)));
            Object.assign(Specification.Drivetrain, JSON.parse(JSON.stringify(Baseline.Specification.Drivetrain)));
            Object.assign(Specification.Engine, JSON.parse(JSON.stringify(Baseline.Specification.Engine)));
            Object.assign(Specification.Thermal, JSON.parse(JSON.stringify(Baseline.Specification.Thermal)));
            Object.assign(Specification.Aids, JSON.parse(JSON.stringify(Baseline.Specification.Aids)));
            Object.assign(Specification.Induction, JSON.parse(JSON.stringify(Baseline.Specification.Induction)));
            Object.assign(Specification.Differential, JSON.parse(JSON.stringify(Baseline.Specification.Differential)));
            Object.assign(Specification.Suspension, JSON.parse(JSON.stringify(Baseline.Specification.Suspension)));
            Object.assign(Specification.Steering, JSON.parse(JSON.stringify(Baseline.Specification.Steering)));
            Object.assign(Specification.SoftBody, JSON.parse(JSON.stringify(Baseline.Specification.SoftBody)));
            Object.assign(Specification.AeroActive, JSON.parse(JSON.stringify(Baseline.Specification.AeroActive)));
            Object.assign(Specification.Fluids, JSON.parse(JSON.stringify(Baseline.Specification.Fluids)));
            Object.assign(Specification.Simulation, JSON.parse(JSON.stringify(Baseline.Specification.Simulation)));
            Profiles.forEach((Profile, Index) =>
            {
                Profile.Points = JSON.parse(JSON.stringify(Baseline.Profiles[Index].Points));
                Profile.Interpolation = Baseline.Profiles[Index].Interpolation;
            });
            AppendLog('VEHICLE', 'Baseline restored', 'every authored value returned to GT-04', 'warm');
            C.Broadcast();
            C.Toast('Baseline restored');
        });
        Copy.addEventListener('click', () =>
        {
            const Text = Catalog.EncodeVehicleRecord(Specification, Profiles, Tyres, DetachedTyres()).join('\n');
            (navigator.clipboard ? navigator.clipboard.writeText(Text) : Promise.reject())
                .then(() => C.Toast('Vehicle record copied'), () => C.Toast('Copy unavailable here'));
        });
        Csv.addEventListener('click', () =>
        {
            const Text = Catalog.EncodeCurveCsv(ResolveProfile('EngineTorque'), UnitSystem);
            (navigator.clipboard ? navigator.clipboard.writeText(Text) : Promise.reject())
                .then(() => C.Toast('Torque curve copied as CSV'), () => C.Toast('Copy unavailable here'));
        });
        Body.append(C.Construct('div', 'action-row wrap', [Reset, Copy, Csv]), Block);
        C.Register(() =>
        {
            Block.textContent = '';
            for (const Line of Catalog.EncodeVehicleRecord(Specification, Profiles, Tyres, DetachedTyres()))
            {
                const Split = Line.indexOf(' = ');
                if (Split < 0) { Block.append(Line + '\n'); continue; }
                Block.append(C.Construct('span', 'key', Line.slice(0, Split)), Line.slice(Split));
                const Comment = Line.indexOf('   # ');
                if (Comment >= 0) Block.append(C.Construct('span', 'comment', '   # proposed: new record'));
                Block.append('\n');
            }
        });
        Body.append(C.Construct('p', 'footer-note', 'The vehicle record is new: nothing in the scene reads it yet, so every key is marked '
            + 'proposed. The tyres keep their four records and gain an `enclosed` key; the pit lane lists what is detached.'));
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                     TYRE INSPECTOR
    //------------------------------------------------------------------------------------------------------------------------

    function ConstructTyrePage(Page, Tyre)
    {
        const Compound = Catalog.ResolveCompound(Tyre.Compound);
        Page.append(C.Construct('div', 'page-title', [
            C.Construct('h2', null, `Tyre · ${Tyre.Code}`), C.Construct('span', 'live-pill', [C.Construct('i'), C.Construct('span', null, Tyre.Enclosed ? 'ENCLOSED' : 'DETACHED')])
        ]),
            C.Construct('div', 'page-subtitle', `${Tyre.Position} of the GT-04. A record in its own right, enclosed by the vehicle and '
                + 'replaceable at a pit stop.`));
        const Grid = C.Construct('div', 'instrument-grid');
        C.InstrumentCard(Grid, {
            Label: 'Pressure', Resolve: () =>
            {
                const Reading = Format(Tyre.Pressure, 'Pressure', 0);
                const Hot = Tyre.Pressure > 230;
                return { Text: Reading.Text, Unit: Reading.Unit, Status: Hot ? 'Above the cold window' : 'Inside the window',
                         Tone: Hot ? 'warm' : 'live', Colour: '#9fb4d6' };
            }
        });
        C.InstrumentCard(Grid, {
            Label: 'Surface', Resolve: () =>
            {
                const Reading = Format(Tyre.SurfaceTemperature, 'Temperature', 0);
                const Window = Catalog.ResolveTemperatureWindow(Tyre.Compound, Tyre.SurfaceTemperature);
                return { Text: Reading.Text, Unit: Reading.Unit, Status: `${Window} window`,
                         Tone: Window === 'Optimal' ? 'live' : Window === 'Cold' ? 'muted' : 'danger', Colour: Compound.Colour };
            }
        });
        C.InstrumentCard(Grid, {
            Label: 'Wear', Resolve: () =>
            {
                const Reading = Format(Tyre.Wear, 'Percent', 0);
                const Status = Catalog.ResolveWearStatus(Tyre.Wear);
                return { Text: Reading.Text, Unit: Reading.Unit, Status,
                         Tone: Status === 'Fresh' ? 'live' : Status === 'Degraded' ? 'warm' : 'danger', Colour: '#8fc7b8' };
            }
        });
        C.InstrumentCard(Grid, {
            Label: 'Stint', Resolve: () =>
            {
                const Reading = Format(Tyre.StintLaps, 'Ratio', 0);
                return { Text: Reading.Text, Unit: 'laps', Status: Tyre.SetId, Tone: 'muted', Colour: '#bcbcbc' };
            }
        });
        Page.append(Grid);
        const Section = C.SectionCard(Page, {
            Id: 'TyreDetail', Title: 'Tyre record', Glyph: 'Tyre', Colour: Compound.Colour,
            Status: () => `${Compound.Name} · ${DisplayText(Tyre.Pressure, 'Pressure', 0)} ${UnitSymbol('Pressure')}`
        });
        const Body = Section.Inner;
        Body.append(C.Construct('p', 'section-caption', 'The fields this prototype edits. Compound windows, carcass pressure and the '
            + 'thermal model belong to the tyre editor that follows.'));
        C.SegmentedRow(Body, {
            Label: 'Compound',
            Options: [['Soft', 'Soft'], ['Medium', 'Medium'], ['Hard', 'Hard'], ['Inter', 'Intermediate'], ['Wet', 'Wet']],
            Read: () => Tyre.Compound,
            Write: Key => { Tyre.Compound = Key; }
        });
        QuantityRow(Body, { Label: 'Cold pressure', UnitKey: 'Pressure', Minimum: 120, Maximum: 260, Step: 1,
                            Read: () => Tyre.Pressure, Write: Quantity => { Tyre.Pressure = Quantity; } });
        QuantityRow(Body, { Label: 'Hot pressure', UnitKey: 'Pressure', Minimum: 140, Maximum: 300, Step: 1,
                            Read: () => Tyre.CarcassPressure, Write: Quantity => { Tyre.CarcassPressure = Quantity; } });
        QuantityRow(Body, { Label: 'Surface temperature', UnitKey: 'Temperature', Minimum: 20, Maximum: 160, Step: 1,
                            Read: () => Tyre.SurfaceTemperature, Write: Quantity => { Tyre.SurfaceTemperature = Quantity; } });
        QuantityRow(Body, { Label: 'Core temperature', UnitKey: 'Temperature', Minimum: 20, Maximum: 160, Step: 1,
                            Read: () => Tyre.CoreTemperature, Write: Quantity => { Tyre.CoreTemperature = Quantity; } });
        QuantityRow(Body, { Label: 'Wear', UnitKey: 'Percent', Minimum: 0, Maximum: 100, Step: 1,
                            Read: () => Tyre.Wear, Write: Quantity => { Tyre.Wear = Quantity; } });
        QuantityRow(Body, { Label: 'Tread depth', UnitKey: 'Length', Minimum: 1.5, Maximum: 9, Step: 0.1, Digits: 1,
                            Read: () => Tyre.TreadDepth, Write: Quantity => { Tyre.TreadDepth = Quantity; } });
        QuantityRow(Body, { Label: 'Loaded radius', UnitKey: 'Length', Minimum: 280, Maximum: 400, Step: 1,
                            Read: () => Tyre.Radius, Write: Quantity => { Tyre.Radius = Quantity; } });
        QuantityRow(Body, { Label: 'Section width', UnitKey: 'Length', Minimum: 180, Maximum: 360, Step: 1,
                            Read: () => Tyre.Width, Write: Quantity => { Tyre.Width = Quantity; } });
        QuantityRow(Body, { Label: 'Stint laps', UnitKey: 'Ratio', Minimum: 0, Maximum: 60, Step: 1,
                            Read: () => Tyre.StintLaps, Write: Quantity => { Tyre.StintLaps = Quantity; } });
        QuantityRow(Body, { Label: 'Contact patch load', UnitKey: 'Force', Minimum: 0.5, Maximum: 8, Step: 0.1, Digits: 2,
                            Read: () => Tyre.ContactPatchLoad * 1000, Write: Quantity => { Tyre.ContactPatchLoad = Quantity / 1000; } });
        Body.append(C.Construct('div', 'sub-heading', 'Three-node carcass gas and Pacejka MF6.1'));
        QuantityRow(Body, { Label: 'Belt intermediate temperature', UnitKey: 'Temperature', Minimum: 20, Maximum: 160, Step: 1,
                            Read: () => Tyre.BeltTemperature, Write: Quantity => { Tyre.BeltTemperature = Quantity; } });
        QuantityRow(Body, { Label: 'Internal gas temperature', UnitKey: 'Temperature', Minimum: 20, Maximum: 150, Step: 1,
                            Read: () => Tyre.GasTemperature, Write: Quantity => { Tyre.GasTemperature = Quantity; } });
        QuantityRow(Body, { Label: 'Pacejka longitudinal shape', UnitKey: 'Ratio', Minimum: 1.0, Maximum: 2.3, Step: 0.02, Digits: 2,
                            Read: () => Tyre.PacejkaLongitudinalShape,
                            Write: Quantity => { Tyre.PacejkaLongitudinalShape = Quantity; } });
        QuantityRow(Body, { Label: 'Pacejka lateral shape', UnitKey: 'Ratio', Minimum: 0.9, Maximum: 2.0, Step: 0.02, Digits: 2,
                            Read: () => Tyre.PacejkaLateralShape,
                            Write: Quantity => { Tyre.PacejkaLateralShape = Quantity; } });
        QuantityRow(Body, { Label: 'Load friction sensitivity', UnitKey: 'Ratio', Minimum: 0.0, Maximum: 0.45, Step: 0.01, Digits: 2,
                            Read: () => Tyre.LoadSensitivity, Write: Quantity => { Tyre.LoadSensitivity = Quantity; } });
        C.SwitchRow(Body, {
            Label: 'Punctured carcass (rapid deflation)',
            Detail: 'Vents pneumatic cavity pressure toward atmospheric and collapses radial carcass support.',
            Read: () => Tyre.Punctured,
            Write: PuncturedState => { Tyre.Punctured = PuncturedState; }
        });
        C.SwitchRow(Body, {
            Label: 'Enclosed by the vehicle',
            Detail: 'Clearing it parks the tyre in the pit lane; the record survives either way.',
            Read: () => Tyre.Enclosed,
            Write: Enabled =>
            {
                if (Enabled) Catalog.AttachTyre(Tyre); else Catalog.DetachTyre(Tyre);
                AppendLog('PIT', `Tyre ${Tyre.Code} ${Enabled ? 'enclosed' : 'detached'}`, Enabled ? 'written back to GT-04' : 'parked in the pit lane',
                          Enabled ? 'live' : 'warm');
            }
        });
        Body.append(C.Construct('div', 'sub-heading', 'Compound window'));
        const Window = C.Construct('div', 'window-strip');
        Body.append(Window);
        C.Register(() =>
        {
            Window.textContent = '';
            const Current = Catalog.ResolveCompound(Tyre.Compound);
            const Low = Format(Current.WindowLow, 'Temperature', 0), High = Format(Current.WindowHigh, 'Temperature', 0);
            const Surface = Format(Tyre.SurfaceTemperature, 'Temperature', 0);
            Window.append(
                C.Construct('span', 'window-cell', [C.Construct('span', null, 'Window'), C.Construct('b', null, `${Low.Text}–${High.Text} ${Low.Unit}`)]),
                C.Construct('span', 'window-cell', [C.Construct('span', null, 'Surface'), C.Construct('b', null, `${Surface.Text} ${Surface.Unit}`)]),
                C.Construct('span', 'window-cell', [C.Construct('span', null, 'Grip scale'), C.Construct('b', null, Current.GripScale.toFixed(2))]),
                C.Construct('span', 'window-cell', [C.Construct('span', null, 'Wear rate'), C.Construct('b', null, Current.WearRate.toFixed(2))]));
        });
        const Back = C.Construct('button', 'pill-button accent', [C.ConstructGlyph('Vehicle'), C.Construct('span', null, 'Back to the vehicle')]);
        Back.type = 'button';
        Back.addEventListener('click', () => SelectRecord('Vehicle'));
        Body.append(C.Construct('div', 'action-row', [Back]));
        Body.append(C.Construct('p', 'footer-note', 'Next step: the tyre editor — per-compound wear and thermal curves, the carcass '
            + 'continuum and pit-stop history.'));
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    INSPECTOR ASSEMBLY
    //------------------------------------------------------------------------------------------------------------------------

    function RevealSection(Id)
    {
        const Entry = Sections.find(Section => Section.Id === Id);
        if (!Entry || !InspectorScroller) return;
        Entry.Card.classList.remove('folded');
        InspectorScroller.ScrollTo(Entry.Card.offsetTop - 52);
    }

    let ActiveSpy = null;

    function ConstructSectionTabs(Page)
    {
        const Bar = C.Construct('div', 'section-tabs');
        Page.append(Bar);
        return () =>
        {
            for (const Entry of Sections)
            {
                const Tab = C.Construct('button', 'section-tab', Entry.Tab);
                Tab.type = 'button';
                Tab.dataset.section = Entry.Id;
                Tab.addEventListener('click', () => RevealSection(Entry.Id));
                Bar.append(Tab);
            }
            const Scroller = document.getElementById('InspectorScroll');
            const Spy = () =>
            {
                let Current = Sections[0]?.Id;
                for (const Entry of Sections) if (Entry.Card.offsetTop - 70 <= Scroller.scrollTop) Current = Entry.Id;
                Bar.querySelectorAll('.section-tab').forEach(Tab =>
                {
                    const Active = Tab.dataset.section === Current;
                    if (Active && !Tab.classList.contains('active'))
                        Bar.scrollTo({ left: Tab.offsetLeft - (Bar.clientWidth - Tab.clientWidth) / 2, behavior: 'smooth' });
                    Tab.classList.toggle('active', Active);
                });
            };
            if (ActiveSpy) Scroller.removeEventListener('scroll', ActiveSpy);
            ActiveSpy = Spy;
            Scroller.addEventListener('scroll', Spy);
            Spy();
        };
    }

    function ConstructInspectorPage()
    {
        const Content = document.getElementById('InspectorContent');
        Dock.Detach();
        C.ReleaseScope('Page');
        C.OpenScope('Page');
        Content.textContent = '';
        Sections.length = 0;
        RefreshModeTabs();
        const Page = C.Construct('div', 'inspector');
        Content.append(Page);
        if (InspectorMode === 'Telemetry') { ConstructTelemetryPage(Page); C.CloseScope(); return; }
        const Tyre = Tyres.find(Candidate => Candidate.Id === SelectedRecord);
        const NotesPill = C.Construct('button', 'notes-pill', [C.ConstructGlyph('Plus'), C.Construct('span', null, 'Add notes')]);
        NotesPill.type = 'button';
        NotesPill.addEventListener('click', () => C.Toast('Notes are not stored by this prototype'));
        const Breadcrumb = C.Construct('div', 'breadcrumb-row', [
            C.Construct('div', 'breadcrumb', [document.createTextNode('Inspector / '), C.Construct('b', null, Tyre ? `Vehicle / Tyre ${Tyre.Code}` : 'Vehicle')]),
            C.Construct('div', 'notes-actions', [NotesPill])
        ]);
        Page.append(Breadcrumb);

        // 📝 Units belong to the inspector, not the record: one switch converts every card, slider and chart.
        C.PillTabs(Page, {
            Options: [['Metric · kg · kW · Nm', 'Metric'], ['Imperial · lb · hp · lb·ft', 'Imperial']],
            Read: () => UnitSystem, Write: System => { UnitSystem = System; }
        });
        // 📝 Colour is a reading aid, not a record field: it survives every rebuild because the kit holds the mode.
        C.ColourPill(Page, { Caption: 'Text colour · one hue per quantity' });

        if (Tyre) { ConstructTyrePage(Page, Tyre); C.CloseScope(); return; }
        if (SelectedRecord !== 'Vehicle')
        {
            const Environment = Catalog.EnvironmentRows.find(Entry => Entry.Id === SelectedRecord);
            Page.append(C.Construct('div', 'page-title', [C.Construct('h2', null, Environment ? Environment.Name : 'Render settings')]),
                C.Construct('div', 'page-subtitle', 'The record the scene already had. This prototype is the vehicle inspector; the '
                    + 'existing inspector is unchanged.'));
            Page.append(C.Construct('div', 'empty-inspector', [
                C.Construct('h3', null, 'Outside this prototype'),
                document.createTextNode('Select Vehicle · GT-04 in the Outliner, or one of its tyres, to see the new inspector.')
            ]));
            C.CloseScope();
            return;
        }

        Page.append(C.Construct('div', 'page-title', [C.Construct('h2', null, 'Vehicle · GT-04'),
                                                      C.Construct('span', 'live-pill', [C.Construct('i'), C.Construct('span', null, 'NEW RECORD')])]),
            C.Construct('div', 'page-subtitle', 'One vehicle record in the scene, with the four tyres it encloses and the curves that '
                + 'make it a custom vehicle. No other geometry is added.'));
        ConstructHero(Page);
        ConstructChips(Page);
        const ConstructTabs = ConstructSectionTabs(Page);
        ConstructIdentity(Page);
        ConstructEngine(Page);
        ConstructSuspension(Page);
        ConstructSoftBody(Page);
        ConstructCurves(Page);
        ConstructThermal(Page);
        ConstructTyres(Page);
        ConstructAero(Page);
        ConstructSimulation(Page);
        ConstructPerformance(Page);
        ConstructRecord(Page);
        ConstructTabs();
        C.CloseScope();
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                      TELEMETRY DOCK
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 The live dock is the vehicle's own component: it is parented to the record, reads it, and is shown only while the
    //    record has telemetry enabled. Turning the aid off in the inspector detaches it, which is the debug-only contract.
    function ConstructTelemetryPage(Page)
    {
        if (!Specification.Aids.Telemetry)
        {
            Page.append(
                C.Construct('div', 'telemetry-head', [
                    C.Construct('div', 'telemetry-identity', [C.Construct('i', 'off'), C.Construct('span', 'telemetry-name',
                        `${Specification.Identity.Name} · telemetry off`), C.Construct('span', 'telemetry-tag', 'DEBUG · OFF')])
                ]),
                C.Construct('div', 'empty-inspector', [
                    C.Construct('h3', null, 'The live dock is detached'),
                    document.createTextNode('Vehicle · GT-04 has telemetry disabled, so nothing is sampled at runtime. '
                        + 'Enable it to attach the read-only dock; the record itself is unchanged by watching it.')
                ]));
            const Enable = C.Construct('button', 'pill-button accent small', [C.ConstructGlyph('Activity'),
                                                                              C.Construct('span', null, 'Enable telemetry')]);
            Enable.type = 'button';
            Enable.addEventListener('click', () =>
            {
                Specification.Aids.Telemetry = true;
                AppendLog('INSPECTOR', 'Telemetry enabled', 'the live dock attaches to the vehicle', 'live');
                ConstructInspectorPage();
                C.Broadcast();
            });
            Page.append(C.Construct('div', 'telemetry-foot', [C.Construct('span', 'telemetry-foot-note',
                'Debug instrumentation only.'), Enable]));
            return;
        }
        Dock.Construct(Page, {
            Specification, Profiles, Tyres,
            UnitSystem: () => UnitSystem,
            OnMode: Mode => SwitchMode(Mode),
            OnEvent: Event => AppendLog(Event.Channel, Event.Message, Event.Detail, Event.Tone || 'live')
        });
    }

    function SwitchMode(Mode)
    {
        if (InspectorMode === Mode) return;
        InspectorMode = Mode;
        Dock.Detach();
        ConstructInspectorPage();
        C.Broadcast();
        AppendLog('INSPECTOR', Mode === 'Telemetry' ? 'Live telemetry attached' : 'Vehicle inspector attached',
                  Mode === 'Telemetry' ? 'read-only dock · the record is untouched' : 'authoring view · edits write the record',
                  Mode === 'Telemetry' ? 'live' : 'muted');
    }

    let ModeTabs = [];

    function RefreshModeTabs()
    {
        for (const Tab of ModeTabs) Tab.Element.classList.toggle('active', Tab.Mode === InspectorMode);
    }

    function ConstructModeTabs()
    {
        const Bar = document.getElementById('InspectorDockTabs');
        Bar.textContent = '';
        const Entries = [['Inspector', 'Inspector', 'Sliders'], ['Telemetry', 'Telemetry', 'Activity']];
        ModeTabs = Entries.map(([Mode, Label, Glyph]) =>
        {
            const Element = C.Construct('button', 'dock-tab', [C.ConstructGlyph(Glyph), C.Construct('span', null, Label)]);
            Element.type = 'button';
            Element.dataset.mode = Mode;
            if (Mode === 'Telemetry') Element.append(C.Construct('i', 'tab-live-dot'));
            Element.addEventListener('click', () => SwitchMode(Mode));
            Bar.append(Element);
            return { Mode, Element };
        });
        RefreshModeTabs();
    }

    function SelectRecord(Id)
    {
        SelectedRecord = Id;
        if (Tyres.some(Tyre => Tyre.Id === Id)) SelectedTyreId = Id;
        ConstructInspectorPage();
        C.Broadcast();
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                     INITIALIZATION
    //------------------------------------------------------------------------------------------------------------------------

    function Initialize()
    {
        C.SetColourMode(C.ReadColourMode());
        ConstructModeTabs();
        ConstructOutliner(document.getElementById('OutlinerContent'));
        InspectorScroller = C.SmoothScroll(document.getElementById('InspectorScroll'), document.getElementById('InspectorContent'));
        ConstructViewport();
        ConstructInspectorPage();
        ConstructLogStream();

        const InspectorStatus = document.getElementById('InspectorStatus');
        const Revision = C.Construct('b', null, '0');
        const CurveCount = C.Construct('b', null, '6');
        const ModeCell = C.Construct('b', null, 'Inspector');
        const RecordCell = C.Construct('b', null, 'Vehicle');
        InspectorStatus.append(
            C.Construct('div', 'status-cell', [C.Construct('span', null, 'Mode'), ModeCell]),
            C.Construct('div', 'status-cell', [C.Construct('span', null, 'Record'), RecordCell]),
            C.Construct('div', 'status-cell', [C.Construct('span', null, 'Curves'), CurveCount]),
            C.Construct('span', 'spacer'),
            C.Construct('div', 'status-cell', [C.Construct('span', null, 'Revision'), Revision]));

        // 📝 One revision per authored change, as RenderPipelineConfiguration::AdvanceRevision does in C++.
        C.Register(() =>
        {
            const Signature = JSON.stringify([Specification, Profiles, Tyres]);
            if (Signature !== LastRevisionSignature)
            {
                LastRevisionSignature = Signature;
                Revision.textContent = String(++RevisionCount);
            }
            CurveCount.textContent = String(Profiles.length);
            ModeCell.textContent = InspectorMode === 'Telemetry' ? 'Telemetry · live' : 'Inspector';
            const Detached = Tyres.find(Candidate => Candidate.Id === SelectedRecord);
            RecordCell.textContent = Detached ? `Tyre ${Detached.Code}` : 'Vehicle';
        });

        document.addEventListener('keydown', Event =>
        {
            if (Event.target instanceof HTMLInputElement) return;
            if (Event.key === 'c' || Event.key === 'C')
            {
                const Order = ['labels', 'units', 'none'];
                const Next = Order[(Order.indexOf(C.ReadColourMode()) + 1) % Order.length];
                C.SetColourMode(Next);
                AppendLog('INSPECTOR', `Text colour: ${Next === 'none' ? 'plain' : Next}`,
                          Next === 'labels' ? 'labels wear the quantity colour' : Next === 'units' ? 'only units are coloured'
                                                                                              : 'no colour coding', 'live');
                C.Broadcast();
                return;
            }
            if (Event.key === 't' || Event.key === 'T')
            {
                SwitchMode(InspectorMode === 'Telemetry' ? 'Inspector' : 'Telemetry');
                return;
            }
            if (Event.key === 'u' || Event.key === 'U')
            {
                UnitSystem = UnitSystem === 'Metric' ? 'Imperial' : 'Metric';
                AppendLog('INSPECTOR', `Units: ${UnitSystem}`, 'every card, slider and chart converts', 'live');
                C.Broadcast();
            }
        });
        C.Broadcast();
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', Initialize);
    else Initialize();
})();
