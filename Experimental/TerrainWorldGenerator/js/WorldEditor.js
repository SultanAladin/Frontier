/*====================================================================================================
                                      WORLDEDITOR.JS
====================================================================================================*/
// 🧩 Separate editor pane for blocking out the world: sculpt brushes, SDF continent /
// gulf stamps, sea level, city base placement, and water / camera settings. Docked as a
// fixed panel beside the viewport so the world itself stays unobstructed.

function $(Id) { return document.getElementById(Id); }

function CreateElement(Class, Html)
{
    const Element = document.createElement('div');
    if (Class) Element.className = Class;
    if (Html) Element.innerHTML = Html;
    return Element;
}

function Slider(Parent, Label, Min, Max, Value, Step, Unit, OnInput)
{
    const Row = CreateElement('control-row');
    const LabelEl = CreateElement('control-label', Label);
    const ValueEl = CreateElement('control-value', Value + Unit);
    const Input = document.createElement('input');
    Input.type = 'range';
    Input.min = Min;
    Input.max = Max;
    Input.step = Step;
    Input.value = Value;
    Input.addEventListener('input', () =>
    {
        ValueEl.textContent = Input.value + Unit;
        OnInput(parseFloat(Input.value));
    });
    Row.appendChild(LabelEl);
    Row.appendChild(Input);
    Row.appendChild(ValueEl);
    Parent.appendChild(Row);
    return { Element: Input, ValueEl };
}

function Checkbox(Parent, Label, Checked, OnChange)
{
    const Row = CreateElement('control-row');
    const Input = document.createElement('input');
    Input.type = 'checkbox';
    Input.checked = Checked;
    Input.addEventListener('change', () => OnChange(Input.checked));
    const LabelEl = CreateElement('control-label', Label);
    LabelEl.style.marginLeft = '8px';
    Row.appendChild(Input);
    Row.appendChild(LabelEl);
    Parent.appendChild(Row);
    return Input;
}

function Button(Parent, Label, OnClick)
{
    const Input = document.createElement('button');
    Input.type = 'button';
    Input.textContent = Label;
    Input.className = 'control-button';
    Input.addEventListener('click', OnClick);
    Parent.appendChild(RowOrParent(Parent), Input);
    return Input;
}

function RowOrParent(Parent)
{
    // Buttons get their own row; other controls use the panel directly.
    return Parent;
}

class WorldEditor
{
    constructor(App)
    {
        this.App = App;
        this.Container = $('Editor');
        this.ActiveTab = 'Sculpt';
        this.MiniPreviewDirty = true;
        this.MiniCanvas = null;
        this.BaseList = null;
        this.Build();
    }

    Build()
    {
        this.Container.innerHTML = '';
        this.Container.className = 'editor';

        const Header = CreateElement('editor-header');
        Header.innerHTML = '<div class="editor-title">World Editor — Blockout</div>' +
            '<div class="editor-subtitle">Flat continent, 8 km × 8 km, surrounded by ocean</div>';
        this.Container.appendChild(Header);

        const Tabs = CreateElement('editor-tabs');
        const TabSpec = [
            ['Sculpt', 'Sculpt'],
            ['World', 'World'],
            ['Bases', 'City Bases'],
            ['Water', 'Water'],
            ['Camera', 'Camera']
        ];
        for (const [Id, Label] of TabSpec)
        {
            const Button = document.createElement('button');
            Button.type = 'button';
            Button.className = 'tab-button';
            Button.textContent = Label;
            Button.addEventListener('click', () => this.ShowTab(Id));
            Tabs.appendChild(Button);
        }
        this.Container.appendChild(Tabs);

        const Panels = CreateElement('editor-panels');
        Panels.appendChild(this.PanelSculpt());
        Panels.appendChild(this.PanelWorld());
        Panels.appendChild(this.PanelBases());
        Panels.appendChild(this.PanelWater());
        Panels.appendChild(this.PanelCamera());
        this.Container.appendChild(Panels);

        this.ShowTab('Sculpt');
    }

    PanelSculpt()
    {
        const Panel = CreateElement('editor-panel tab-panel', '');
        Panel.dataset.tab = 'Sculpt';
        Panel.innerHTML =
            '<div class="panel-title">Sculpt brush</div>' +
            '<div class="hint">Drag on the terrain to paint. Works below sea level too.</div>';
        const Modes = ['Raise', 'Lower', 'Flatten', 'Smooth'];
        this.SculptMode = 'Raise';
        for (const Mode of Modes)
        {
            const B = document.createElement('button');
            B.type = 'button';
            B.className = 'mode-button' + (Mode === this.SculptMode ? ' active' : '');
            B.textContent = Mode;
            B.addEventListener('click', () =>
            {
                this.SculptMode = Mode;
                this.App.Settings.SculptMode = Mode;
                this.UpdateModeButtons();
            });
            Panel.appendChild(B);
        }
        Slider(Panel, 'Radius [m]', 10, 500, this.App.Settings.SculptRadiusM, 10, ' m', V => this.App.Settings.SculptRadiusM = V);
        Slider(Panel, 'Strength [m/stamp]', 0.1, 40, this.App.Settings.SculptStrengthM, 0.1, ' m', V => this.App.Settings.SculptStrengthM = V);
        const Reset = document.createElement('button');
        Reset.type = 'button';
        Reset.className = 'control-button';
        Reset.textContent = 'Reset height map to flat sea level';
        Reset.addEventListener('click', () =>
        {
            this.App.Field.Heights.fill(this.App.Field.SeaLevelM);
            this.App.Field.Dirty = true;
            this.App.TerrainRenderer.SetHeightMap(this.App.Field.Heights, this.App.Field.Size);
            this.MiniPreviewDirty = true;
        });
        Panel.appendChild(Reset);
        return Panel;
    }

    UpdateModeButtons()
    {
        const Buttons = this.Container.querySelectorAll('[data-tab="Sculpt"] > button');
        for (const B of Buttons)
        {
            B.className = 'mode-button' + (B.textContent === this.SculptMode ? ' active' : '');
        }
    }

    PanelWorld()
    {
        const Panel = CreateElement('editor-panel tab-panel', '');
        Panel.dataset.tab = 'World';
        Panel.innerHTML =
            '<div class="panel-title">World (SDF → height map → mesh)</div>' +
            '<div class="hint">Continents and gulfs are signed-distance stamps on a fractal plate mask.</div>';
        Slider(Panel, 'Sea level [m]', -60, 220, this.App.Field.SeaLevelM, 1, ' m', V => this.App.Field.SeaLevelM = V);
        const Regenerate = document.createElement('button');
        Regenerate.type = 'button';
        Regenerate.className = 'control-button';
        Regenerate.textContent = 'Regenerate continent layout (new seed)';
        Regenerate.addEventListener('click', () =>
        {
            const Seed = parseInt($('WorldSeed').value, 10) || 0xC0FFEE;
            this.App.Field.Regenerate(Seed);
            this.App.TerrainRenderer.SetHeightMap(this.App.Field.Heights, this.App.Field.Size);
            this.App.WaterSimulation.Reset();
            this.MiniPreviewDirty = true;
        });
        Panel.appendChild(Regenerate);
        const SeedRow = CreateElement('control-row');
        SeedRow.innerHTML = '<label class="control-label">Seed</label>' +
            '<input id="WorldSeed" type="number" value="0xC0FFEE" style="width:96px;padding:4px;">';
        Panel.appendChild(SeedRow);
        const ResetWorld = document.createElement('button');
        ResetWorld.type = 'button';
        ResetWorld.className = 'control-button';
        ResetWorld.textContent = 'Reset sculpts (keep layout)';
        ResetWorld.addEventListener('click', () =>
        {
            this.App.Field.Regenerate(this.App.Field.NoiseSeed || 0xC0FFEE);
            this.App.TerrainRenderer.SetHeightMap(this.App.Field.Heights, this.App.Field.Size);
            this.MiniPreviewDirty = true;
        });
        Panel.appendChild(ResetWorld);
        const Stamps = CreateElement('panel-title-stamp');
        Stamps.innerHTML = '<div class="panel-title">Stamps</div>';
        Panel.appendChild(Stamps);
        const AddContinent = document.createElement('button');
        AddContinent.type = 'button';
        AddContinent.className = 'control-button';
        AddContinent.textContent = 'Stamp Africa-like continent at crosshair';
        AddContinent.addEventListener('click', () => this.ApplyStamp('continent'));
        Panel.appendChild(AddContinent);
        const AddGulf = document.createElement('button');
        AddGulf.type = 'button';
        AddGulf.className = 'control-button';
        AddGulf.textContent = 'Cut gulf at crosshair';
        AddGulf.addEventListener('click', () => this.ApplyStamp('gulf'));
        Panel.appendChild(AddGulf);

        const PreviewWrap = CreateElement('preview-wrap');
        PreviewWrap.innerHTML = '<div class="preview-label">Height map preview</div>';
        this.MiniCanvas = document.createElement('canvas');
        this.MiniCanvas.width = 160;
        this.MiniCanvas.height = 160;
        this.MiniCanvas.className = 'mini-preview';
        PreviewWrap.appendChild(this.MiniCanvas);
        Panel.appendChild(PreviewWrap);
        return Panel;
    }

    PanelBases()
    {
        const Panel = CreateElement('editor-panel tab-panel', '');
        Panel.dataset.tab = 'Bases';
        Panel.innerHTML =
            '<div class="panel-title">City bases</div>' +
            '<div class="hint">First the base, then the city. Click "Place at crosshair" to drop a base block on the surface.</div>';
        const Place = document.createElement('button');
        Place.type = 'button';
        Place.className = 'control-button';
        Place.textContent = 'Place base at crosshair';
        Place.addEventListener('click', () =>
        {
            const Hit = this.App.IntersectCenter();
            if (Hit) this.App.AddBase(Hit[0], Hit[1], Hit[2]);
        });
        Panel.appendChild(Place);
        this.BaseList = CreateElement('base-list');
        Panel.appendChild(this.BaseList);
        return Panel;
    }

    RefreshBases()
    {
        if (!this.BaseList) return;
        this.BaseList.innerHTML = '';
        for (const Base of this.App.Bases)
        {
            const Row = CreateElement('base-row');
            Row.textContent = Base.Name + '  (' + Base.Position[0].toFixed(0) + ', ' + Base.Position[2].toFixed(0) + ')';
            const Zoom = document.createElement('button');
            Zoom.type = 'button';
            Zoom.textContent = 'zoom';
            Zoom.className = 'base-button';
            Zoom.addEventListener('click', () =>
            {
                this.App.Camera.ResetOver(Base.Position[0], Base.Position[2]);
            });
            const Remove = document.createElement('button');
            Remove.type = 'button';
            Remove.textContent = 'remove';
            Remove.className = 'base-button';
            Remove.addEventListener('click', () =>
            {
                const Index = this.App.Bases.indexOf(Base);
                if (Index >= 0) this.App.Bases.splice(Index, 1);
            });
            Row.appendChild(Zoom);
            Row.appendChild(Remove);
            this.BaseList.appendChild(Row);
        }
    }

    PanelWater()
    {
        const Panel = CreateElement('editor-panel tab-panel', '');
        Panel.dataset.tab = 'Water';
        Panel.innerHTML =
            '<div class="panel-title">Ocean water</div>' +
            '<div class="hint">2.5D height-field simulation nearby; tessellated mesh ocean farther out.</div>';
        Checkbox(Panel, 'Simulation enabled', this.App.Settings.SimulationEnabled, V => this.App.Settings.SimulationEnabled = V);
        Checkbox(Panel, 'Rain (auto splashes)', this.App.Settings.RainEnabled, V => this.App.Settings.RainEnabled = V);
        Slider(Panel, 'Splash strength [m]', 0.1, 6.0, this.App.Settings.SplashStrengthM, 0.1, ' m', V => this.App.Settings.SplashStrengthM = V);
        Slider(Panel, 'Splash radius [m]', 20, 300, this.App.Settings.SplashRadiusM, 10, ' m', V => this.App.Settings.SplashRadiusM = V);
        Slider(Panel, 'Near ring radius [m]', 300, 1500, this.App.Settings.NearRadiusM, 100, ' m', V => this.App.Settings.NearRadiusM = V);
        Slider(Panel, 'Far distance [m]', 1500, 5000, this.App.Settings.FarDistanceM, 250, ' m', V => this.App.Settings.FarDistanceM = V);
        Checkbox(Panel, 'Wireframe (tessellation view)', this.App.Settings.WireframeEnabled, V => this.App.Settings.WireframeEnabled = V);
        Checkbox(Panel, 'LOD colour bands', this.App.Settings.LodColourEnabled, V => this.App.Settings.LodColourEnabled = V);
        return Panel;
    }

    PanelCamera()
    {
        const Panel = CreateElement('editor-panel tab-panel', '');
        Panel.dataset.tab = 'Camera';
        Panel.innerHTML =
            '<div class="panel-title">Camera</div>' +
            '<div class="hint">Click viewport to fly (WASD + mouse look, ESC to release). Drag to orbit without pointer lock.</div>';
        Slider(Panel, 'Walk speed [m/s]', 2, 40, this.App.Camera.WalkSpeedMps, 1, ' m/s', V => this.App.Camera.WalkSpeedMps = V);
        Slider(Panel, 'Sprint speed [m/s]', 10, 120, this.App.Camera.SprintSpeedMps, 5, ' m/s', V => this.App.Camera.SprintSpeedMps = V);
        Slider(Panel, 'Look sensitivity', 0.5, 8, this.App.Camera.SensitivityRadPerPx * 1000, 0.5, ' ×1e-3', V => this.App.Camera.SensitivityRadPerPx = V / 1000);
        const ResetCam = document.createElement('button');
        ResetCam.type = 'button';
        ResetCam.className = 'control-button';
        ResetCam.textContent = 'Reset camera over world centre';
        ResetCam.addEventListener('click', () => this.App.Camera.ResetOver(0, 0));
        Panel.appendChild(ResetCam);
        return Panel;
    }

    ShowTab(Id)
    {
        this.ActiveTab = Id;
        const Panels = this.Container.querySelectorAll('.editor-panel');
        for (const Panel of Panels) Panel.style.display = Panel.dataset.tab === Id ? 'block' : 'none';
        const Buttons = this.Container.querySelectorAll('.tab-button');
        for (const B of Buttons) B.className = 'tab-button' + (B.textContent === Id ? ' active' : '');
        if (Id === 'World') this.MiniPreviewDirty = true;
        if (Id === 'Bases') this.RefreshBases();
    }

    // Draw the height map colour preview in the World tab.
    UpdateMiniPreview()
    {
        if (!this.MiniPreviewDirty || !this.MiniCanvas) return;
        const Width = this.MiniCanvas.width;
        const Height = this.MiniCanvas.height;
        const Pixels = this.App.Field.BuildColorPreview(Width, Height);
        const Context = this.MiniCanvas.getContext('2d');
        const Image = Context.createImageData(Width, Height);
        Image.data.set(Pixels);
        Context.putImageData(Image, 0, 0);
        this.MiniPreviewDirty = false;
    }

    // Coordinate readout under the crosshair.
    UpdateReadout(WorldX, WorldZ, Height)
    {
        let El = $('CoordReadout');
        if (!El)
        {
            El = CreateElement('coord-readout');
            El.id = 'CoordReadout';
            this.Container.appendChild(El);
        }
        El.textContent = 'Crosshair: x ' + WorldX.toFixed(0) + ' m,  z ' + WorldZ.toFixed(0) + ' m,  h ' + Height.toFixed(1) + ' m';
    }

    ApplyStamp(Kind)
    {
        const Hit = this.App.IntersectCenter();
        if (!Hit) return;
        const WorldX = Hit[0];
        const WorldZ = Hit[1];
        const Field = this.App.Field;
        const RadiusM = this.App.Settings.SculptRadiusM;
        const StrengthM = this.App.Settings.SculptStrengthM;
        if (Kind === 'continent')
        {
            // Stamp a broad rise (continent nucleus) then erode the edges into a shelf.
            Field.ApplySculptBrush(WorldX, WorldZ, RadiusM * 2.5, StrengthM * 1.5, 'Raise');
        }
        else if (Kind === 'gulf')
        {
            Field.ApplySculptBrush(WorldX, WorldZ, RadiusM * 1.8, StrengthM * 2.0, 'Lower');
        }
        Field.Dirty = true;
        this.App.TerrainRenderer.SetHeightMap(Field.Heights, Field.Size);
        this.MiniPreviewDirty = true;
    }
}

export { WorldEditor };
