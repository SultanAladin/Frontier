'use strict';
//----------------------------------------------------------------------------------------------------------------------------
//                                          Frontier · World Blockout · WorldBlockout
//----------------------------------------------------------------------------------------------------------------------------
// 📝 What this file is: the editor itself. A separate editor from the vehicle inspector on purpose — blocking out a world is
//    its own job, and a city is placed on ground that has already been settled.
//
//    Three columns:
//      ① tools      sculpt brushes over the SDF, continent and gulf stamps, city pads, undo
//      ② stage      the planet map, with the cursor, the brush ring and the placed pads
//      ③ ocean      the 2.5D patch readout, the adaptive resolution map and the live cross-section
//
//    Everything visual comes from Scenes.js, so what the owner sees and what the evidence scripts render are one drawing.
//----------------------------------------------------------------------------------------------------------------------------

const WorldBlockout = (function ()
{
    // 📝 Loaded as scripts in the page, required in Node: the typeof guards keep one source file working in both worlds.
    const TerrainApi = typeof TerrainField !== 'undefined' ? TerrainField : require('./TerrainField.js');
    const OceanApi = typeof OceanField !== 'undefined' ? OceanField : require('./OceanField.js');
    const MeshApi = typeof OceanMesh !== 'undefined' ? OceanMesh : require('./OceanMesh.js');
    const SceneApi = typeof Scenes !== 'undefined' ? Scenes : require('./Scenes.js');
    const RendererApi = typeof Renderer3D !== 'undefined' ? Renderer3D : require('./Renderer3D.js');

    const TOOLS = [
        { Key: 'Raise', Label: 'Raise', Glyph: '▲', Hint: 'lift the ground' },
        { Key: 'Lower', Label: 'Lower', Glyph: '▼', Hint: 'dig it down' },
        { Key: 'Plateau', Label: 'Plateau', Glyph: '▬', Hint: 'flatten to a shelf' },
        { Key: 'Smooth', Label: 'Smooth', Glyph: '≈', Hint: 'average the neighbours' },
        { Key: 'City', Label: 'City pad', Glyph: '◎', Hint: 'sink a level pad' }
    ];

    const CONTINENTS = [
        { Key: 'Africa', Plateau: 680 },
        { Key: 'Eurasia', Plateau: 780 },
        { Key: 'SouthAmerica', Plateau: 700 },
        { Key: 'Australia', Plateau: 520 }
    ];

    const GULFS = [
        { Key: 'PersianGulf', RampKm: 200 },
        { Key: 'RedSea', RampKm: 120 },
        { Key: 'GulfOfGuinea', RampKm: 150 }
    ];

    const State = {
        Field: null, Ocean: null, Patch: null, Plan: null, Mesh: null,
        Tool: 'Raise', RadiusKm: 900, Strength: 0.6, CityRadiusKm: 60,
        Cursor: { Lon: 20, Lat: 6 },
        CameraHeightKm: 2, FarOnly: false, Running: true,
        MapDirty: true, OceanDirty: true, MapWidth: 800, MapHeight: 400,
        Frames: 0, Strokes: 0, Dragging: false, Ticker: null,
        View: 'map', Camera3D: null, ViewDirty: true, View3DStats: null, Paint3D: false, Orbiting: false
    };

    const Elements = {};
    const Bridge = {};

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    SMALL DOM HELPERS
    //------------------------------------------------------------------------------------------------------------------------

    const Make = (Tag, ClassName, Text) =>
    {
        const Element = document.createElement(Tag);
        if (ClassName) Element.className = ClassName;
        if (Text !== undefined && Text !== null) Element.textContent = Text;
        return Element;
    };

    function Card(Title, Note)
    {
        const Shell = Make('section', 'card');
        const Head = Make('div', 'card-head');
        Head.append(Make('h2', 'card-title', Title));
        if (Note) Head.append(Make('span', 'card-note', Note));
        Shell.append(Head);
        return Shell;
    }

    function Readout(Label, Value, Unit)
    {
        const Shell = Make('div', 'readout');
        Shell.append(Make('span', 'readout-label', Label));
        const Line = Make('span', 'readout-value', Value);
        if (Unit) Line.append(Make('span', 'readout-unit', Unit));
        Shell.append(Line);
        return Shell;
    }

    function Vital(Label, Value, Unit, Colour)
    {
        const Shell = Make('div', 'vital');
        Shell.append(Make('span', 'vital-label', Label));
        const Line = Make('span', 'vital-value', Value);
        if (Colour) Line.style.color = Colour;
        if (Unit) Line.append(Make('span', 'vital-unit', Unit));
        Shell.append(Line);
        return Shell;
    }

    function Slider(Label, Minimum, Maximum, Step, Value, Format, OnInput)
    {
        const Field = Make('label', 'field');
        Field.append(Make('span', 'field-label', Label));
        const Display = Make('span', 'field-value', Format(Value));
        const Input = Make('input');
        Input.type = 'range';
        Input.min = String(Minimum);
        Input.max = String(Maximum);
        Input.step = String(Step);
        Input.value = String(Value);
        Input.setAttribute('aria-label', Label);
        Input.addEventListener('input', () =>
        {
            Display.textContent = Format(Number(Input.value));
            OnInput(Number(Input.value));
        });
        Field.append(Display, Input);
        return Field;
    }

    function Chip(Label, Kind, OnClick)
    {
        const Button = Make('button', 'chip', Label);
        Button.type = 'button';
        Button.dataset.kind = Kind;
        Button.addEventListener('click', OnClick);
        return Button;
    }

    function Action(Label, Accent, OnClick)
    {
        const Button = Make('button', Accent ? 'action action-accent' : 'action', Label);
        Button.type = 'button';
        Button.addEventListener('click', OnClick);
        return Button;
    }

    function Status(Text, Tone)
    {
        if (!Elements.Status) return;
        Elements.Status.textContent = Text;
        if (Tone) Elements.Status.dataset.tone = Tone;
        else delete Elements.Status.dataset.tone;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    THE WORLD
    //------------------------------------------------------------------------------------------------------------------------

    function CreateWorld(Preset)
    {
        const Field = TerrainApi.CreateField(TerrainApi.ConfigurePlanet());
        State.Field = Field;
        State.MapDirty = true;
        if (Preset === 'Bare')
        {
            State.Cursor = { Lon: 0, Lat: 0 };
            return Field;
        }
        for (const Continent of CONTINENTS)
        {
            TerrainApi.StampContinent(Field, {
                Name: Continent.Key, Polygon: TerrainApi.ContinentOutlines()[Continent.Key],
                Plateau: Continent.Plateau, RampKm: 420
            });
        }
        for (const Gulf of GULFS)
        {
            TerrainApi.CarveGulf(Field, {
                Name: Gulf.Key, Polygon: TerrainApi.GulfOutlines()[Gulf.Key], RampKm: Gulf.RampKm
            });
        }
        TerrainApi.StampArchipelago(Field, { Name: 'Sunda', Lon: 112, Lat: -4, Count: 20, Radius: 1500 });
        // 📝 Two pads to start with, because a city needs its base before anyone talks about the city.
        for (const Site of [{ Name: 'Kinshasa', Lon: 15.3, Lat: -4.3, RadiusKm: 60 },
            { Name: 'Punjab', Lon: 74.0, Lat: 31.0, RadiusKm: 55 }])
        {
            // 📝 PlaceCityPad records the pad itself and pushes an undo snapshot with it.
            TerrainApi.PlaceCityPad(Field, Site);
        }
        State.Cursor = { Lon: 15.3, Lat: -4.3 };
        Field.Cursor = State.Cursor;
        return Field;
    }

    function StampWorld(Key)
    {
        const Field = State.Field;
        const Continent = CONTINENTS.find(Entry => Entry.Key === Key);
        if (Continent)
        {
            const Record = TerrainApi.StampContinent(Field, {
                Name: Continent.Key, Polygon: TerrainApi.ContinentOutlines()[Key],
                Plateau: Continent.Plateau, RampKm: 420
            });
            Status(`Stamped ${Record.Name}: ${Record.Touched} cells, peak ${Math.round(Record.Peak)} m.`, 'live');
        }
        else
        {
            const Gulf = GULFS.find(Entry => Entry.Key === Key);
            if (!Gulf) return;
            const Record = TerrainApi.CarveGulf(Field, {
                Name: Gulf.Key, Polygon: TerrainApi.GulfOutlines()[Key], RampKm: Gulf.RampKm
            });
            Status(`Carved ${Record.Name}: ${Record.Touched} cells back under water.`, 'warm');
        }
        State.MapDirty = true;
        State.OceanDirty = true;
        RefreshMap();
        RenderStageBar();
        RenderVitals();
    }

    function StampIsles()
    {
        const Record = TerrainApi.StampArchipelago(State.Field, {
            Name: 'Sunda', Lon: 112, Lat: -4, Count: 20, Radius: 1500
        });
        Status(`Sowed ${Record.Islands.length} islands in the Sunda strait.`, 'live');
        State.MapDirty = true;
        RefreshMap();
        RenderVitals();
    }

    function SetTool(Key)
    {
        if (!TOOLS.some(Tool => Tool.Key === Key)) return;
        State.Tool = Key;
        for (const Button of Elements.ToolRail?.querySelectorAll('.tool') ?? [])
        {
            Button.setAttribute('aria-pressed', Button.dataset.tool === Key ? 'true' : 'false');
        }
        const Tool = TOOLS.find(Entry => Entry.Key === Key);
        Status(`${Tool.Label}: ${Tool.Hint}.`, null);
        RenderStageBar();
    }

    // 📝 One pointer position, two jobs: the cursor for every readout, and the centre of the brush if the drag is live.
    // 📝 The map is a 2:1 equirectangular image inside a box that is rarely 2:1, so the SVG letterboxes. The pointer is
    //    mapped through the *rendered image*, never the box, or a cursor at the edge of the Atlantic lands in the Pacific.
    function ResolveCursorFromPointer(Pointer)
    {
        const Bounds = Elements.MapHit.getBoundingClientRect();
        const Aspect = State.MapWidth / State.MapHeight;
        let Width = Bounds.width || State.MapWidth;
        let Height = Width / Aspect;
        if (Bounds.height && Height > Bounds.height) { Height = Bounds.height; Width = Height * Aspect; }
        const Left = Bounds.left + Math.max(0, (Bounds.width - Width) / 2);
        const Top = Bounds.top + Math.max(0, (Bounds.height - Height) / 2);
        const X = Math.max(0, Math.min(Width, Pointer.clientX - Left));
        const Y = Math.max(0, Math.min(Height, Pointer.clientY - Top));
        State.Cursor = { Lon: (X / Width) * 360 - 180, Lat: 90 - (Y / Height) * 180 };
        State.Field.Cursor = State.Cursor;
        return State.Cursor;
    }

    function Sculpt(Lon, Lat)
    {
        if (State.Tool === 'City')
        {
            const Result = TerrainApi.PlaceCityPad(State.Field, {
                Name: `Pad ${State.Field.Cities.length + 1}`, Lon, Lat, RadiusKm: State.CityRadiusKm
            });
            if (!Result.Valid)
            {
                Status(`No pad here: ${Result.Reason.toLowerCase()}.`, 'danger');
                return Result;
            }
            Status(`Pad for ${Result.City.Name} levelled at ${Math.round(Result.City.Height)} m `
                + `over ${Math.round(Result.City.RadiusKm)} km.`, 'live');
            ++State.Strokes;
            State.MapDirty = true;
            RefreshMap();
            RenderVitals();
            return Result;
        }
        const Changed = TerrainApi.ApplyBrush(State.Field, {
            Tool: State.Tool, Lon, Lat, RadiusKm: State.RadiusKm, Strength: State.Strength
        });
        if (Changed > 0)
        {
            ++State.Strokes;
            State.MapDirty = true;
            State.OceanDirty = true;
            RefreshMap();
            RenderVitals();
            RenderStageBar();
        }
        return Changed;
    }

    function Undo()
    {
        const Cells = TerrainApi.UndoStroke(State.Field);
        State.MapDirty = true;
        State.OceanDirty = true;
        RefreshMap();
        Status(Cells ? `Undid one stroke (${Cells} cells).` : 'Nothing to undo.', Cells ? 'warm' : null);
        RenderVitals();
        RenderStageBar();
        return Cells;
    }

    function ResolveCoast()
    {
        // 📝 The patch wants a coast, not open water: walk west from the cursor until the SDF changes sign.
        const Field = State.Field;
        const Lat = Math.max(-70, Math.min(70, State.Cursor.Lat));
        let Lon = State.Cursor.Lon;
        for (let Index = 0; Index < 220; ++Index)
        {
            if (Field.Sample.Distance(Lon, Lat) < 0) return { Lon, Lat };
            Lon -= 0.25;
            if (Lon < -180) break;
        }
        return { Lon: State.Cursor.Lon, Lat };
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    THE OCEAN
    //------------------------------------------------------------------------------------------------------------------------

    function BuildOcean()
    {
        if (!State.Ocean)
        {
            State.Ocean = OceanApi.CreateOcean(State.Field, {});
        }
        State.Ocean.Terrain = State.Field;
        const Anchor = ResolveCoast();
        // 📝 Nudge the focus a few hundred metres inland of the waterline so the patch holds both beach and open water.
        Anchor.Lon += 0.0035;
        const Patch = OceanApi.CreatePatch(State.Ocean, {
            FocusLon: Anchor.Lon, FocusLat: Anchor.Lat, Cells: 112, ExtentKm: 1.6
        });
        State.Ocean.Patch = Patch;
        State.Patch = Patch;
        State.Patch.Probes.length = 0;
        OceanApi.AddProbe(Patch, { Name: 'Surf', Lon: Anchor.Lon + 0.0016, Lat: Anchor.Lat });
        OceanApi.AddProbe(Patch, { Name: 'Shelf', Lon: Anchor.Lon - 0.0012, Lat: Anchor.Lat });
        OceanApi.AddProbe(Patch, { Name: 'Off', Lon: Anchor.Lon - 0.0046, Lat: Anchor.Lat });
        OceanApi.SettlePatch(State.Ocean, Patch, 200);
        BuildPlan();
        State.OceanDirty = true;
        Status(`Ocean patch settled at ${Anchor.Lon.toFixed(1)}°, ${Anchor.Lat.toFixed(1)}° — `
            + `${Patch.WetCells} wet cells, celerity ${Patch.MaxSpeed.toFixed(0)} m/s.`, 'live');
        RenderOcean();
        return Patch;
    }

    function BuildPlan()
    {
        State.Plan = MeshApi.CreatePlan(State.Ocean, {
            FocusLon: State.Patch?.FocusLon ?? 0, FocusLat: State.Patch?.FocusLat ?? 0,
            CameraHeightKm: State.CameraHeightKm, MaxTriangles: 120000, PixelBudget: 2.5
        });
        State.Mesh = MeshApi.BuildMesh(State.Ocean, State.Plan);
        State.OceanDirty = true;
        return State.Plan;
    }

    function SetFarOnly(On)
    {
        State.FarOnly = !!On;
        if (!State.Ocean) return;
        if (State.FarOnly)
        {
            State.Ocean.SavedBlend = State.Ocean.SavedBlend
                || { Near: State.Ocean.NearBlendKm, Far: State.Ocean.FarBlendKm };
            State.Ocean.NearBlendKm = -1;
            State.Ocean.FarBlendKm = 0.0001;
            Status('Far field only: the patch is still solving, but nothing on screen uses it.', 'warm');
        }
        else if (State.Ocean.SavedBlend)
        {
            State.Ocean.NearBlendKm = State.Ocean.SavedBlend.Near;
            State.Ocean.FarBlendKm = State.Ocean.SavedBlend.Far;
            Status('Near field back in the blend.', 'live');
        }
        // 📝 The toggle changes which field the surface reads, so the mesh has to be re-evaluated the moment it flips.
        if (State.Mesh) MeshApi.RefreshMesh(State.Ocean, State.Mesh, State.Patch.Time);
        State.OceanDirty = true;
        RenderOcean();
    }

    // 📝 One frame: the solve advances, the surface follows, and only the figures that changed are redrawn.
    function Step(Seconds)
    {
        if (!State.Ocean || !State.Patch) return null;
        const Result = OceanApi.StepPatch(State.Ocean, State.Patch, Seconds);
        if (State.Mesh) MeshApi.RefreshMesh(State.Ocean, State.Mesh, State.Patch.Time);
        ++State.Frames;
        RenderSlice();
        RenderOceanNumbers();
        return Result;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    RENDERING
    //------------------------------------------------------------------------------------------------------------------------

    function RenderVitals()
    {
        if (!Elements.Vitals || !State.Field) return;
        const Field = State.Field;
        const Height = Field.Sample.Height(State.Cursor.Lon, State.Cursor.Lat);
        const Distance = Field.Sample.Distance(State.Cursor.Lon, State.Cursor.Lat);
        const Slope = Field.Sample.Slope(State.Cursor.Lon, State.Cursor.Lat);
        const Band = Field.Sample.Band(Height);
        const Coverage = TerrainApi.ResolveCoverage(Field);
        const Water = Height < 0;
        Elements.Vitals.replaceChildren(
            Vital('Cursor', `${State.Cursor.Lon.toFixed(1)}°`, `${State.Cursor.Lat.toFixed(1)}°`, 'var(--text-primary)'),
            Vital('Elevation', Math.round(Height).toLocaleString('en-GB'), 'm',
                Water ? 'var(--tone-cold)' : 'var(--tone-live)'),
            Vital('SDF', Math.round(Distance).toLocaleString('en-GB'), 'm',
                Distance < 0 ? 'var(--tone-cold)' : 'var(--tone-warm)'),
            Vital('Band', Band, '', Water ? 'var(--deep)' : 'var(--accent)'),
            Vital('Slope', Slope.toFixed(1), '°', Slope > 9 ? 'var(--tone-danger)' : 'var(--text-secondary)'),
            Vital('Land', (Coverage.LandShare * 100).toFixed(1), '%', 'var(--water)')
        );
    }

    function RenderStageBar()
    {
        if (!Elements.StageBar || !State.Field) return;
        const Tool = TOOLS.find(Entry => Entry.Key === State.Tool);
        const MeshLine = State.Mesh
            ? `${State.Mesh.Triangles.toLocaleString('en-GB')} ocean triangles`
            : 'ocean not built';
        const Children = [
            Make('strong', null, `${Tool.Label}`),
            Make('span', null, `${State.RadiusKm.toFixed(0)} km brush`),
            Make('span', null, `rev ${State.Field.Revision}`),
            Make('span', null, `${State.Field.Cities.length} pads`),
            Make('span', 'spacer')
        ];
        // 📝 A switch, then whatever the current view needs: the map has nothing to offer here, and the 3D view wants its
        //    three presets and its LOD paint within reach.
        const MapButton = Make('button', 'view-tab' + (State.View === 'map' ? ' is-on' : ''), 'Map');
        MapButton.type = 'button';
        MapButton.addEventListener('click', () => SetView('map'));
        // 📝 The 3D switch is the one control a first-time reader has to find, so it is the only pill in the bar that is
        //    allowed to shout: accent outline when the map is showing, solid when the 3D view is.
        const ThreeButton = Make('button', 'view-tab view-tab-strong' + (State.View === '3d' ? ' is-on' : ''), '3D view');
        ThreeButton.type = 'button';
        ThreeButton.addEventListener('click', () => SetView('3d'));
        Children.push(MapButton, ThreeButton);
        if (State.View === '3d')
        {
            for (const Name of Object.keys(VIEW_PRESETS))
            {
                const PresetButton = Make('button', 'view-chip' + (State.Preset3D === Name ? ' is-on' : ''), VIEW_PRESETS[Name].Label);
                PresetButton.type = 'button';
                PresetButton.addEventListener('click', () => UsePreset(Name));
                Children.push(PresetButton);
            }
            const PaintButton = Make('button', 'view-chip' + (State.Paint3D ? ' is-on' : ''), 'LOD paint');
            PaintButton.type = 'button';
            PaintButton.addEventListener('click', TogglePaint3D);
            Children.push(PaintButton, Make('span', null, MeshLine));
        }
        else Children.push(Make('span', null, MeshLine));
        Elements.StageBar.replaceChildren(...Children);
    }

    function RefreshMap()
    {
        if (!Elements.MapFrame || !State.Field) return;
        State.MapDirty = false;
        Elements.MapFrame.innerHTML = SceneApi.RenderPlanetMap(State.Field, {
            Width: State.MapWidth, Height: State.MapHeight, Cursor: true
        });
        if (Elements.BrushRing)
        {
            const Reach = Math.max(6, (State.RadiusKm / (2 * Math.PI * 6371)) * State.MapWidth);
            const Left = ((State.Cursor.Lon + 180) / 360) * 100;
            const Top = ((90 - State.Cursor.Lat) / 180) * 100;
            Elements.BrushRing.setAttribute('cx', `${Left}%`);
            Elements.BrushRing.setAttribute('cy', `${Top}%`);
            Elements.BrushRing.setAttribute('r', String(Reach));
        }
    }

    function RenderMapIfDirty()
    {
        if (State.MapDirty) RefreshMap();
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                     THE 3D VIEW
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Three heights are worth having and they are not the same height. A globe is a disc that has to be framed from
    //    outside it; a coast is a few hundred kilometres of shelf and shore; the wave patch is three kilometres of water
    //    with a two-metre swell. The presets are those three questions, and orbit and zoom are free between them.
    const VIEW_PRESETS = {
        Globe: { Label: 'Globe', Camera: { AltitudeKm: 9000, DistanceKm: 0, TargetLon: 16, TargetLat: 6, Relief: 6, WaveScale: 8 } },
        Coast: { Label: 'Coast', Camera: { AltitudeKm: 0, DistanceKm: 520, ElevationDeg: 30, AzimuthDeg: 248, TargetLon: 9, TargetLat: -4, Relief: 6, WaveScale: 24 } },
        Waves: { Label: 'Waves', Camera: { AltitudeKm: 0, DistanceKm: 2.4, ElevationDeg: 34, AzimuthDeg: 290, Relief: 6, WaveScale: 30 } }
    };

    function UsePreset(Name)
    {
        const Preset = VIEW_PRESETS[Name];
        if (!Preset) return null;
        const Camera = State.Camera3D || RendererApi.CreateCamera({});
        const Settings = Object.assign({}, Preset.Camera);
        if (Name === 'Waves' && State.Patch)
        {
            Settings.TargetLon = State.Patch.FocusLon;
            Settings.TargetLat = State.Patch.FocusLat;
        }
        Object.assign(Camera, Settings, { Width: Camera.Width, Height: Camera.Height, FieldOfViewDeg: Camera.FieldOfViewDeg,
            LightAzimuthDeg: Camera.LightAzimuthDeg, LightElevationDeg: Camera.LightElevationDeg });
        State.Camera3D = Camera;
        State.Preset3D = Name;
        State.ViewDirty = true;
        RenderView3D();
        RenderStageBar();
        return Camera;
    }

    function ResizeView3D()
    {
        const Camera = State.Camera3D;
        if (!Camera || !Elements.View3D) return Camera;
        const Width = 720;
        const Aspect = (State.MapWidth / State.MapHeight) || 2;
        Camera.Width = Width;
        Camera.Height = Math.max(240, Math.round(Width / Aspect));
        // 📝 Only when there is a canvas to draw on. In a headless test there is not, and the render still has to happen so
        //    the numbers can be checked.
        if (Elements.View3D.width !== Camera.Width) Elements.View3D.width = Camera.Width;
        if (Elements.View3D.height !== Camera.Height) Elements.View3D.height = Camera.Height;
        return Camera;
    }

    function RenderView3D(Options)
    {
        Options = Options || {};
        if (State.View !== '3d' || !State.Field) return null;
        ResizeView3D();
        const Camera = State.Camera3D;
        const Layer = {};
        if (Options.Water !== false && State.Ocean)
        {
            Layer.Water = RendererApi.BuildWater(State.Ocean, Camera, { Time: State.Ocean.Clock });
        }
        Layer.Terrain = RendererApi.BuildTerrain(State.Field, Camera);
        const Stats = RendererApi.Render(Camera, State.Field.Planet.RadiusKm, Layer, { PaintTiers: State.Paint3D });
        State.ViewDirty = false;
        State.View3DStats = {
            TerrainTriangles: Stats.TerrainTriangles, WaterTriangles: Stats.WaterTriangles,
            Milliseconds: Stats.Milliseconds, MaxKm: Layer.Terrain.MaxKm
        };
        // 📝 Straight into the canvas: the raster is already the pixels, and encoding a PNG only to decode it again would
        //    cost more than the frame did.
        if (Elements.View3D && typeof Elements.View3D.getContext === 'function')
        {
            // 📝 A headless document has a canvas element and no 2D context at all — and asking for one throws rather than
            //    returning null. The render is still the deliverable the tests read, so a canvas that cannot be painted is a
            //    skipped paint, not a failure.
            let Context = null;
            // 📝 Feature-detect the context class before asking for one: jsdom has the element, no implementation, and a
            //    console complaint for every asked-for context.
            const CanPaint = typeof CanvasRenderingContext2D !== 'undefined';
            if (CanPaint) { try { Context = Elements.View3D.getContext('2d'); } catch (Error) { Context = null; } }
            if (Context && typeof Context.createImageData === 'function' && typeof Context.putImageData === 'function')
            {
                try
                {
                    const Image = Context.createImageData(Camera.Width, Camera.Height);
                    Image.data.set(Stats.Raster.Pixels);
                    Context.putImageData(Image, 0, 0);
                }
                catch (Error)
                {
                    State.View3DStats.CanvasError = String(Error && Error.message);
                }
            }
        }
        if (!Options.Quiet)
        {
            Status(`3D · ${Stats.TerrainTriangles.toLocaleString('en-GB')} land triangles · ` +
                `${Stats.WaterTriangles.toLocaleString('en-GB')} water · ${Stats.Milliseconds} ms`, 'live');
        }
        return Stats;
    }

    function SetView(View)
    {
        State.View = View === '3d' ? '3d' : 'map';
        const Stage = Elements.View3D ? Elements.View3D.closest('.stage') : null;
        if (Stage) Stage.classList.toggle('is-3d', State.View === '3d');
        if (State.View === '3d')
        {
            if (!State.Camera3D) UsePreset('Globe');
            else RenderView3D();
            Status('3D view · drag to orbit · wheel to zoom', 'live');
        }
        else
        {
            RefreshMap();
            Status('Map view.', null);
        }
        RenderStageBar();
        return State.View;
    }

    // 📝 Orbit is a pure function of the pointer delta, so a test can call it without a browser and the drag and the test
    //    move the camera the same way.
    function Orbit3D(DeltaX, DeltaY)
    {
        const Camera = State.Camera3D;
        if (!Camera) return null;
        if (Camera.AltitudeKm > 0)
        {
            // 📝 Over a globe, dragging turns the planet: there is no horizon to tilt.
            Camera.TargetLon = ((Camera.TargetLon + DeltaX * 0.22 + 180) % 360 + 360) % 360 - 180;
            Camera.TargetLat = Math.max(-85, Math.min(85, Camera.TargetLat - DeltaY * 0.18));
        }
        else
        {
            RendererApi.Orbit(Camera, DeltaX * 0.3, -DeltaY * 0.24);
        }
        State.ViewDirty = true;
        return Camera;
    }

    function Zoom3D(Notches)
    {
        const Camera = State.Camera3D;
        if (!Camera) return null;
        const Factor = Math.pow(1.18, Notches);
        if (Camera.AltitudeKm > 0)
        {
            Camera.AltitudeKm = Math.max(600, Math.min(40000, Camera.AltitudeKm * Factor));
        }
        else
        {
            Camera.DistanceKm = Math.max(1.5, Math.min(12000, Camera.DistanceKm * Factor));
            Camera.ElevationDeg = Math.max(4, Math.min(84, Camera.ElevationDeg));
        }
        State.ViewDirty = true;
        return Camera;
    }

    function ViewPointerDown(Event)
    {
        if (State.View !== '3d') return;
        State.Orbiting = { X: Event.clientX, Y: Event.clientY };
        if (Event.preventDefault) Event.preventDefault();
    }

    function ViewPointerMove(Event)
    {
        if (State.View !== '3d' || !State.Orbiting) return;
        const DeltaX = Event.clientX - State.Orbiting.X;
        const DeltaY = Event.clientY - State.Orbiting.Y;
        State.Orbiting = { X: Event.clientX, Y: Event.clientY };
        Orbit3D(DeltaX, DeltaY);
        RenderView3D({ Quiet: true });
    }

    function ViewPointerUp()
    {
        State.Orbiting = false;
        if (State.View === '3d') RenderView3D();
    }

    function ViewWheel(Event)
    {
        if (State.View !== '3d') return;
        if (Event.preventDefault) Event.preventDefault();
        Zoom3D(Math.sign(Event.deltaY || 0));
        RenderView3D();
    }

    function TogglePaint3D()
    {
        State.Paint3D = !State.Paint3D;
        RenderView3D();
        RenderStageBar();
        return State.Paint3D;
    }

    function RenderOceanNumbers()
    {
        if (!Elements.OceanNumbers || !State.Patch) return;
        const Patch = State.Patch;
        const Plan = State.Plan;
        if (!Plan) return;
        const Summary = MeshApi.Summarize(Plan, State.Mesh);
        Elements.OceanNumbers.replaceChildren(
            Readout('Substeps', Patch.Substeps.toFixed(0), ''),
            Readout('Celerity', Patch.MaxSpeed.toFixed(0), 'm/s'),
            Readout('Peak η', Patch.PeakEta.toFixed(2), 'm'),
            Readout('Energy', Patch.Energy.toFixed(0), 'MJ')
        );
        if (Elements.Triangles)
        {
            const Used = Math.min(1, Summary.Triangles / Summary.Budget);
            Elements.Triangles.style.width = `${(Used * 100).toFixed(1)}%`;
            Elements.Triangles.dataset.state = Summary.WithinBudget ? 'ok' : 'over';
        }
        if (Elements.TriangleNote)
        {
            Elements.TriangleNote.textContent = `${Summary.Triangles.toLocaleString('en-GB')} / `
                + `${Summary.Budget.toLocaleString('en-GB')} · worst ${Summary.WorstErrorPx.toFixed(1)} px · `
                + `${Summary.Ratio.toFixed(0)}× cheaper than uniform`;
        }
        if (Elements.Clock)
        {
            Elements.Clock.textContent = `t ${Patch.Time.toFixed(1)} s · ${Patch.Cells}² cells · `
                + `${(Patch.CellKm * 1000).toFixed(0)} m`;
        }
    }

    function RenderOcean()
    {
        if (!State.Ocean || !State.Patch) return;
        RenderOceanNumbers();
        RenderSlice();
        if (Elements.ResolutionFigure && State.OceanDirty)
        {
            State.OceanDirty = false;
            Elements.ResolutionFigure.innerHTML = SceneApi.RenderOceanPlan(State.Ocean, State.Plan, State.Mesh,
                { Width: 360, Height: 360, SpanKm: Math.max(2, State.Plan.Settings.AdaptiveKm) });
        }
        if (Elements.PlanCaption && State.Plan)
        {
            const Summary = MeshApi.Summarize(State.Plan, State.Mesh);
            Elements.PlanCaption.textContent = `${Summary.Tiles.toLocaleString('en-GB')} tiles · `
                + `${(Summary.MinCellKm * 1000).toFixed(0)} m near · ${(Summary.MaxCellKm * 1000).toFixed(0)} m far · `
                + `${Summary.FarTriangles} fan triangles`;
        }
    }

    function RenderSlice()
    {
        if (!Elements.SliceFigure || !State.Ocean) return;
        Elements.SliceFigure.innerHTML = SceneApi.RenderSurfaceSlice(State.Ocean, { Width: 360, Height: 200 });
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    PANELS
    //------------------------------------------------------------------------------------------------------------------------

    function BuildToolRail()
    {
        const Rail = Elements.ToolRail;

        const SculptCard = Card('Sculpt', 'SDF → mesh');
        const Grid = Make('div', 'tool-grid');
        TOOLS.forEach((Tool, Index) =>
        {
            const Button = Make('button', 'tool');
            Button.type = 'button';
            Button.dataset.tool = Tool.Key;
            Button.setAttribute('aria-pressed', Tool.Key === State.Tool ? 'true' : 'false');
            Button.title = Tool.Hint;
            Button.append(Make('span', 'tool-glyph', Tool.Glyph), Make('span', null, Tool.Label),
                Make('span', 'tool-key', String(Index + 1)));
            Button.addEventListener('click', () => SetTool(Tool.Key));
            Grid.append(Button);
        });
        SculptCard.append(Grid);

        SculptCard.append(Slider('Brush radius', 90, 2400, 10, State.RadiusKm,
            Value => `${(Value / 1000).toFixed(2)} Mm`, Value => { State.RadiusKm = Value; RenderStageBar(); }));
        SculptCard.append(Slider('Strength', 0.05, 1, 0.05, State.Strength,
            Value => Value.toFixed(2), Value => { State.Strength = Value; }));
        SculptCard.append(Slider('City pad', 20, 200, 5, State.CityRadiusKm,
            Value => `${Value} km`, Value => { State.CityRadiusKm = Value; }));

        const Actions = Make('div', 'actions');
        Actions.append(Action('Undo', false, Undo), Action('Export', true, Export));
        SculptCard.append(Actions);
        Rail.append(SculptCard);

        const StampCard = Card('Stamps', 'authored outlines');
        const Continents = Make('div', 'chips');
        for (const Continent of CONTINENTS)
        {
            Continents.append(Chip(Label(Continent.Key), 'continent', () => StampWorld(Continent.Key)));
        }
        const Gulfs = Make('div', 'chips');
        Gulfs.style.marginTop = '7px';
        for (const Gulf of GULFS)
        {
            Gulfs.append(Chip(Label(Gulf.Key), 'gulf', () => StampWorld(Gulf.Key)));
        }
        Gulfs.append(Chip('Sunda isles', 'isles', StampIsles));
        StampCard.append(Continents, Gulfs);
        StampCard.append(Make('p', 'figure-caption',
            'Stamps blend with max, so a second continent never digs a hole in the first.'));
        Rail.append(StampCard);

        const CityCard = Card('City pads', 'base before content');
        const CityNote = Make('p', 'figure-caption',
            'Pick the city tool and click open ground: the pad is rejected below the waterline, on ground steeper than nine '
            + 'degrees, or where the rim would reach water.');
        CityCard.append(CityNote);
        Elements.CitySheet = Make('div', 'figure');
        CityCard.append(Elements.CitySheet);
        Rail.append(CityCard);
    }

    function Label(Key)
    {
        return Key.replace(/([a-z])([A-Z])/g, '$1 $2');
    }

    function BuildOceanRail()
    {
        const Rail = Elements.OceanRail;

        const OceanCard = Card('Ocean', '2.5D solve');
        Elements.Clock = Make('span', 'card-note', 'patch idle');
        OceanCard.querySelector('.card-head').append(Elements.Clock);
        Elements.OceanNumbers = Make('div', 'readouts');
        OceanCard.append(Elements.OceanNumbers);

        const Toggles = Make('div', 'toggle-row');
        Toggles.append(Make('span', 'field-label', 'Far field only (reject the near solve)'));
        const Toggle = Make('button', 'toggle');
        Toggle.type = 'button';
        Toggle.setAttribute('aria-pressed', 'false');
        Toggle.setAttribute('aria-label', 'Far field only');
        Toggle.addEventListener('click', () =>
        {
            const Next = Toggle.getAttribute('aria-pressed') !== 'true';
            Toggle.setAttribute('aria-pressed', Next ? 'true' : 'false');
            SetFarOnly(Next);
        });
        Toggles.append(Toggle);
        OceanCard.append(Toggles);

        OceanCard.append(Slider('Camera altitude', 0.05, 60, 0.05, State.CameraHeightKm,
            Value => `${Value < 1 ? (Value * 1000).toFixed(0) + ' m' : Value.toFixed(1) + ' km'}`,
            Value =>
            {
                State.CameraHeightKm = Value;
                if (State.Patch) BuildPlan();
                RenderOcean();
            }));

        const Buttons = Make('div', 'actions');
        Buttons.append(Action('Rebuild patch', false, () => BuildOcean()),
            Action('Pause', false, function ToggleRun()
            {
                State.Running = !State.Running;
                this.textContent = State.Running ? 'Pause' : 'Resume';
            }));
        OceanCard.append(Buttons);
        Rail.append(OceanCard);

        const ResolutionCard = Card('Resolution map', 'budget, made visible');
        Elements.ResolutionFigure = Make('div', 'figure');
        Elements.PlanCaption = Make('p', 'figure-caption', 'build the ocean to see the plan');
        ResolutionCard.append(Elements.ResolutionFigure, Elements.PlanCaption);
        const Meter = Make('div', 'meter');
        Meter.append(Make('div', 'meter-head'));
        const MeterTrack = Make('div', 'meter-track');
        Elements.Triangles = Make('div', 'meter-fill');
        MeterTrack.append(Elements.Triangles);
        Meter.append(MeterTrack);
        Elements.TriangleNote = Make('p', 'figure-caption', '');
        ResolutionCard.append(Meter, Elements.TriangleNote);
        Rail.append(ResolutionCard);

        const SliceCard = Card('Cross-section', 'solve vs spectrum');
        Elements.SliceFigure = Make('div', 'figure');
        SliceCard.append(Elements.SliceFigure);
        Rail.append(SliceCard);

        const RecordCard = Card('Record', 'TOML');
        Elements.ExportText = Make('pre', 'export hidden');
        RecordCard.append(Action('Show record', false, ShowRecord), Elements.ExportText);
        Rail.append(RecordCard);
    }

    function ShowRecord()
    {
        if (!Elements.ExportText) return;
        const Hidden = Elements.ExportText.classList.toggle('hidden');
        if (!Hidden) Elements.ExportText.textContent = Export(false);
    }

    function Export(Write)
    {
        const TerrainRecord = TerrainApi.EncodeRecord(State.Field);
        const OceanRecord = State.Ocean ? `\n\n${OceanApi.EncodeRecord(State.Ocean)}` : '';
        const Text = TerrainRecord + OceanRecord;
        if (Elements.ExportText)
        {
            Elements.ExportText.classList.remove('hidden');
            Elements.ExportText.textContent = Text;
        }
        Status(Write ? 'Record written to the panel — copy it into the asset record.' : 'Record shown.', 'warm');
        return Text;
    }

    function RenderCitySheet()
    {
        if (!Elements.CitySheet) return;
        Elements.CitySheet.innerHTML = State.Field.Cities.length
            ? SceneApi.RenderCityPads(State.Field, { Width: 220, Height: 96 })
            : '<p class="figure-caption" style="padding:10px">No pads yet.</p>';
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    WIRING
    //------------------------------------------------------------------------------------------------------------------------

    function PointerDown(Event)
    {
        if (!State.Field) return;
        Elements.MapHit.setPointerCapture?.(Event.pointerId);
        State.Dragging = true;
        ResolveCursorFromPointer(Event);
        RenderVitals();
        RenderStageBar();
        Sculpt(State.Cursor.Lon, State.Cursor.Lat);
    }

    function PointerMove(Event)
    {
        if (!State.Field) return;
        ResolveCursorFromPointer(Event);
        RenderVitals();
        if (State.Dragging) Sculpt(State.Cursor.Lon, State.Cursor.Lat);
    }

    function PointerUp(Event)
    {
        if (!State.Dragging) return;
        State.Dragging = false;
        Elements.MapHit.releasePointerCapture?.(Event.pointerId);
        State.MapDirty = true;
        RefreshMap();
        RenderCitySheet();
        RenderStageBar();
    }

    function KeyDown(Event)
    {
        if (Event.target && /input|textarea|select/i.test(Event.target.tagName ?? '')) return;
        const Index = Number(Event.key);
        if (Number.isInteger(Index) && Index >= 1 && Index <= TOOLS.length)
        {
            SetTool(TOOLS[Index - 1].Key);
            Event.preventDefault();
            return;
        }
        if (Event.key === 'z' || Event.key === 'Z') { Undo(); Event.preventDefault(); }
        if (Event.key === 'c' || Event.key === 'C')
        {
            const Toggle = Elements.OceanRail?.querySelector('.toggle');
            Toggle?.click();
            Event.preventDefault();
        }
    }

    function Boot(Root)
    {
        const Scope = Root ?? document;
        Elements.Vitals = Scope.getElementById('vitals');
        Elements.ToolRail = Scope.getElementById('tool-rail');
        Elements.OceanRail = Scope.getElementById('ocean-rail');
        Elements.StageBar = Scope.getElementById('stage-bar');
        Elements.MapFrame = Scope.getElementById('map-frame');
        Elements.MapHit = Scope.getElementById('map-hit');
        Elements.Status = Scope.getElementById('status');
        Elements.BrushRing = Scope.getElementById('brush-ring');
        Elements.View3D = Scope.getElementById('view-3d');

        CreateWorld('Terra');
        BuildToolRail();
        BuildOceanRail();
        RenderVitals();
        RenderStageBar();
        RefreshMap();
        RenderCitySheet();
        BuildOcean();
        RenderOcean();
        RenderCitySheet();

        Elements.MapHit.addEventListener('pointerdown', PointerDown);
        Elements.MapHit.addEventListener('pointermove', PointerMove);
        Elements.MapHit.addEventListener('pointerup', PointerUp);
        Elements.MapHit.addEventListener('pointercancel', PointerUp);
        if (Elements.View3D)
        {
            Elements.View3D.addEventListener('pointerdown', ViewPointerDown);
            Elements.View3D.addEventListener('pointermove', ViewPointerMove);
            Elements.View3D.addEventListener('pointerup', ViewPointerUp);
            Elements.View3D.addEventListener('pointerleave', ViewPointerUp);
            Elements.View3D.addEventListener('wheel', ViewWheel, { passive: false });
        }
        document.addEventListener('keydown', KeyDown);
        StartTicker();
        Status('Terra-01 blocked out: four continents, three gulfs, one settled ocean patch.', 'live');
        return Bridge;
    }

    // 📝 The page runs the solve on a timer; the evidence scripts drive Step() themselves and call Detach() so a jsdom
    //    process can actually exit.
    function StartTicker()
    {
        if (State.Ticker || typeof setInterval !== 'function') return State.Ticker;
        State.Ticker = setInterval(() =>
        {
            if (State.Running && State.Patch) Step(0.12);
        }, 100);
        return State.Ticker;
    }

    function Detach()
    {
        if (State.Ticker) { clearInterval(State.Ticker); State.Ticker = null; }
        State.Running = false;
        return true;
    }

    function Snapshot()
    {
        const Field = State.Field;
        const Height = Field ? Field.Sample.Height(State.Cursor.Lon, State.Cursor.Lat) : 0;
        const Coverage = Field ? TerrainApi.ResolveCoverage(Field) : null;
        const Summary = State.Plan ? MeshApi.Summarize(State.Plan, State.Mesh) : null;
        return {
            Tool: State.Tool, RadiusKm: State.RadiusKm, Strength: State.Strength,
            Cursor: { ...State.Cursor },
            Height: Math.round(Height), Band: Field ? Field.Sample.Band(Height) : '—',
            Slope: Field ? Field.Sample.Slope(State.Cursor.Lon, State.Cursor.Lat) : 0,
            Revision: Field ? Field.Revision : 0,
            Cities: Field ? Field.Cities.length : 0,
            LandShare: Coverage ? Coverage.LandShare : 0,
            Strokes: State.Strokes, Frames: State.Frames,
            Patch: State.Patch ? {
                Cells: State.Patch.Cells, CellKm: State.Patch.CellKm,
                WetCells: State.Patch.WetCells, Time: State.Patch.Time,
                PeakEta: State.Patch.PeakEta, Energy: State.Patch.Energy,
                MaxSpeed: State.Patch.MaxSpeed, Substeps: State.Patch.Substeps
            } : null,
            Summary,
            FarOnly: State.FarOnly,
            Mesh: State.Mesh ? {
                Triangles: State.Mesh.Triangles, Vertices: State.Mesh.Vertices,
                NearTriangles: State.Mesh.NearTriangles, MidTriangles: State.Mesh.MidTriangles,
                FarTriangles: State.Mesh.FarTriangles
            } : null
        };
    }

    Object.assign(Bridge, {
        Boot, Step, Snapshot, Detach, StartTicker, SetTool, SetFarOnly, Undo, Export,
        SetView, UsePreset, Orbit3D, Zoom3D, RenderView3D, ResizeView3D, TogglePaint3D, VIEW_PRESETS,
        Sculpt, StampWorld, StampIsles, BuildOcean, BuildPlan,
        CreateWorld, Refresh: () => { RefreshMap(); RenderVitals(); RenderStageBar(); RenderOcean(); RenderCitySheet(); },
        State, TOOLS, CONTINENTS, GULFS,
        Settle: Seconds => (State.Ocean ? OceanApi.SettlePatch(State.Ocean, State.Patch, Seconds) : null),
        SculptAt: (Lon, Lat) => { State.Cursor = { Lon, Lat }; State.Field.Cursor = State.Cursor; RenderVitals(); return Sculpt(Lon, Lat); }
    });

    if (typeof module !== 'undefined' && module.exports) module.exports = Bridge;
    if (typeof document !== 'undefined')
    {
        const Start = () => Boot(document);
        if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', Start);
        else Start();
    }
    return Bridge;
})();
