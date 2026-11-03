/*====================================================================================================
                                      WORLDGENERATORAPP.JS
====================================================================================================*/
// 🧩 Main application: glues the SDF terrain field, the 2.5D water simulation, the
// adaptive-tessellation ocean renderer, the Unreal-style fly camera, and the separate
// world-editor pane into one block-out viewport. Served over plain HTTP; no build step.

import { TerrainField, HeightMapSize, HeightMapTexelM } from './TerrainField.js';
import { WaterSimulation } from './WaterSimulation.js';
import { FlyCamera, Multiply, Inverse4 } from './FlyCamera.js';
import { TerrainRenderer } from './TerrainRenderer.js';
import { OceanRenderer } from './OceanRenderer.js';
import { WorldEditor } from './WorldEditor.js';

// [m] - Viewport fog range.
const FogNearM = 600.0;
const FogFarM = 7000.0;
// [-] - Sun direction (normalised) and colour.
const SunDir = [0.55, -0.75, -0.35];
const SunColour = [1.0, 0.94, 0.82];
// [-] - Fog / clear colour.
const FogColour = [0.52, 0.66, 0.82];
// [m] - Rain splash cadence.
const RainIntervalSec = 0.4;

class WorldGeneratorApp
{
    constructor()
    {
        this.Canvas = document.getElementById('Viewport');
        this.Overlay = document.getElementById('Overlay');
        this.Gl = this.Canvas.getContext('webgl2', { alpha: false, depth: true });
        if (!this.Gl) throw new Error('WebGL2 is required');

        this.Settings = {
            SculptMode: 'Raise',
            SculptRadiusM: 140.0,
            SculptStrengthM: 2.0,
            SimulationEnabled: true,
            RainEnabled: true,
            SplashStrengthM: 2.0,
            SplashRadiusM: 140.0,
            NearRadiusM: 700.0,
            FarDistanceM: 4000.0,
            WireframeEnabled: false,
            LodColourEnabled: true
        };

        this.Field = new TerrainField();
        this.Field.Regenerate();
        this.Simulation = new WaterSimulation();
        this.Camera = new FlyCamera();
        this.Camera.ResetOver(0, 0);
        this.Bases = [];
        this.PointerLocked = false;
        this.IsPainting = false;
        this.IsOrbiting = false;
        this.LastMouse = [0, 0];
        this.LastTime = performance.now();
        this.RainAccumulatorSec = 0.0;
        this.FrameCount = 0;
        this.FpsTime = 0.0;
        this.Fps = 0;
        this.Editor = new WorldEditor(this);
        this.TerrainRenderer = new TerrainRenderer(this.Gl);
        this.OceanRenderer = new OceanRenderer(this.Gl);

        this.TerrainRenderer.SetHeightMap(this.Field.Heights, this.Field.Size);
        this.Resize();
        this.BindInput();
        window.addEventListener('resize', () => this.Resize());
        this.Loop();
    }

    Resize()
    {
        const PixelRatio = Math.min(window.devicePixelRatio || 1, 2);
        const Width = Math.floor(this.Canvas.clientWidth * PixelRatio);
        const Height = Math.floor(this.Canvas.clientHeight * PixelRatio);
        if (this.Canvas.width !== Width || this.Canvas.height !== Height)
        {
            this.Canvas.width = Width;
            this.Canvas.height = Height;
            this.Overlay.width = Width;
            this.Overlay.height = Height;
        }
        this.Gl.viewport(0, 0, this.Canvas.width, this.Canvas.height);
    }

    BindInput()
    {
        const PointerLockChange = () =>
        {
            this.PointerLocked = document.pointerLockElement === this.Canvas;
        };
        document.addEventListener('pointerlockchange', PointerLockChange);
        document.addEventListener('pointerlockerror', () =>
        {
            // Pointer lock unavailable on this host: orbit fallback stays active.
        });
        this.Canvas.addEventListener('mousedown', Event => this.OnMouseDown(Event));
        this.Canvas.addEventListener('mouseup', Event => this.OnMouseUp(Event));
        this.Canvas.addEventListener('mousemove', Event => this.OnMouseMove(Event));
        this.Canvas.addEventListener('wheel', Event => this.OnWheel(Event), { passive: false });
        window.addEventListener('keydown', Event => this.OnKeyDown(Event));
        window.addEventListener('keyup', Event => this.OnKeyUp(Event));
        this.Canvas.addEventListener('click', () =>
        {
            if (!this.PointerLocked) this.Canvas.requestPointerLock();
        });
    }

    OnKeyDown(Event)
    {
        this.Camera.Keys[Event.key.toLowerCase()] = true;
        if (Event.key === 'Escape' && this.PointerLocked) document.exitPointerLock();
    }

    OnKeyUp(Event)
    {
        this.Camera.Keys[Event.key.toLowerCase()] = false;
    }

    OnMouseDown(Event)
    {
        if (Event.button !== 0) return;
        if (this.PointerLocked)
        {
            // Fire a splash into the ocean at the crosshair (proves the simulation is
            // stateful: ripples propagate from the impact point).
            const Hit = this.IntersectCenter();
            if (Hit) this.SplashAt(Hit[0], Hit[2]);
        }
        else if (this.Editor.ActiveTab === 'Sculpt')
        {
            this.IsPainting = true;
            this.PaintAt(Event.clientX, Event.clientY);
        }
        else
        {
            this.IsOrbiting = true;
        }
        this.LastMouse = [Event.clientX, Event.clientY];
    }

    OnMouseUp(Event)
    {
        if (Event.button === 0) { this.IsPainting = false; this.IsOrbiting = false; }
    }

    OnMouseMove(Event)
    {
        const Dx = Event.clientX - this.LastMouse[0];
        const Dy = Event.clientY - this.LastMouse[1];
        this.LastMouse = [Event.clientX, Event.clientY];
        if (this.PointerLocked)
        {
            this.Camera.Rotate(Event.movementX, Event.movementY);
        }
        else if (this.IsPainting)
        {
            this.PaintAt(Event.clientX, Event.clientY);
        }
        else if (this.IsOrbiting)
        {
            this.Camera.Orbit(Dx, Dy);
        }
        this.UpdateReadout();
    }

    OnWheel(Event)
    {
        Event.preventDefault();
        if (this.Editor.ActiveTab === 'Sculpt' && !this.PointerLocked)
        {
            const Delta = -Event.deltaY * 0.001;
            this.Settings.SculptRadiusM = Math.min(600, Math.max(20, this.Settings.SculptRadiusM * (1.0 + Delta)));
        }
        else
        {
            this.Camera.OrbitZoom(-Event.deltaY * 0.001);
        }
    }

    // [m] - Ray from screen coordinates against the height field (fixed-point iteration
    // against the sampled surface). Returns [x, y, z] or null.
    IntersectScreen(ScreenX, ScreenY)
    {
        const Gl = this.Gl;
        const Width = Gl.canvas.width;
        const Height = Gl.canvas.height;
        const NdcX = (ScreenX / Width) * 2.0 - 1.0;
        const NdcY = 1.0 - (ScreenY / Height) * 2.0;
        const Proj = this.Camera.ProjectionMatrix(Width / Height);
        const View = this.PointerLocked ? this.Camera.ViewMatrix() : this.Camera.OrbitViewMatrix();
        const InverseProjection = Inverse4(Multiply(Proj, View));
        const Eye = TransformPoint4(InverseProjection, [0.0, 0.0, 0.0, 1.0]);
        const Clip = TransformPoint4(InverseProjection, [NdcX, NdcY, -1.0, 1.0]);
        const Direction = Normalize([Clip[0] - Eye[0], Clip[1] - Eye[1], Clip[2] - Eye[2]]);
        if (Direction[1] > -0.001) return null;
        let T = (this.Field.SeaLevelM - Eye[1]) / Direction[1];
        let Position = [Eye[0] + Direction[0] * T, Eye[1] + Direction[1] * T, Eye[2] + Direction[2] * T];
        for (let I = 0; I < 8; I++)
        {
            const Surface = this.Field.SampleBilinear(Position[0], Position[2]);
            T = (Surface - Eye[1]) / Direction[1];
            Position = [Eye[0] + Direction[0] * T, Eye[1] + Direction[1] * T, Eye[2] + Direction[2] * T];
        }
        if (!IsFinite(Position[0]) || !IsFinite(Position[2])) return null;
        const Range = this.Field.WorldHalfSideM;
        if (Math.abs(Position[0]) > Range || Math.abs(Position[2]) > Range) return null;
        return Position;
    }

    IntersectCenter()
    {
        const Rect = this.Canvas.getBoundingClientRect();
        const ScaleX = this.Canvas.width / Rect.width;
        const ScaleY = this.Canvas.height / Rect.height;
        return this.IntersectScreen(
            (this.Canvas.width / 2) * ScaleX,
            (this.Canvas.height / 2) * ScaleY
        );
    }

    PaintAt(ScreenX, ScreenY)
    {
        const Hit = this.IntersectScreen(ScreenX, ScreenY);
        if (!Hit) return;
        const Field = this.Field;
        Field.ApplySculptBrush(
            Hit[0], Hit[2],
            this.Settings.SculptRadiusM,
            this.Settings.SculptStrengthM,
            this.Settings.SculptMode
        );
        this.TerrainRenderer.SetHeightMap(Field.Heights, Field.Size);
        this.Editor.MiniPreviewDirty = true;
    }

    SplashAt(WorldX, WorldZ)
    {
        this.Simulation.AddSplash(
            WorldX, WorldZ,
            this.Settings.SplashRadiusM,
            this.Settings.SplashStrengthM
        );
    }

    AddBase(WorldX, WorldY, WorldZ)
    {
        this.Bases.push({
            Name: 'Base-' + (this.Bases.length + 1),
            Position: [WorldX, WorldY, WorldZ]
        });
        this.Editor.RefreshBases();
    }

    Update(DeltaTauSec)
    {
        this.Camera.Update(DeltaTauSec);
        const Snap = 64.0;
        const CentreX = Math.round(this.Camera.Position[0] / Snap) * Snap;
        const CentreZ = Math.round(this.Camera.Position[2] / Snap) * Snap;
        this.OceanRenderer.SetCenter(CentreX, CentreZ);

        if (this.Settings.SimulationEnabled)
        {
            this.Simulation.Step(DeltaTauSec);
            this.OceanRenderer.UpdateSimTexture(this.Simulation.HeightData());
        }

        this.RainAccumulatorSec += DeltaTauSec;
        if (this.Settings.SimulationEnabled && this.Settings.RainEnabled && this.RainAccumulatorSec >= RainIntervalSec)
        {
            this.RainAccumulatorSec -= RainIntervalSec;
            const OffsetX = (Math.random() - 0.5) * this.Settings.NearRadiusM * 1.4;
            const OffsetZ = (Math.random() - 0.5) * this.Settings.NearRadiusM * 1.4;
            this.Simulation.AddSplash(
                this.Camera.Position[0] + OffsetX,
                this.Camera.Position[2] + OffsetZ,
                this.Settings.SplashRadiusM * 0.5,
                this.Settings.SplashStrengthM * 0.6
            );
        }
    }

    UpdateReadout()
    {
        const Hit = this.IntersectCenter();
        if (Hit)
        {
            this.Editor.UpdateReadout(Hit[0], Hit[2], Hit[1]);
        }
    }

    Render()
    {
        const Gl = this.Gl;
        const Width = Gl.canvas.width;
        const Height = Gl.canvas.height;
        const Aspect = Width / Height;

        Gl.clearColor(FogColour[0], FogColour[1], FogColour[2], 1.0);
        Gl.clear(Gl.COLOR_BUFFER_BIT | Gl.DEPTH_BUFFER_BIT);
        Gl.enable(Gl.DEPTH_TEST);

        const ViewProjection = this.PointerLocked
            ? this.Camera.ViewProjectionMatrix(Aspect)
            : Multiply(this.Camera.ProjectionMatrix(Aspect), this.Camera.OrbitViewMatrix());

        this.TerrainRenderer.Draw(
            ViewProjection, this.Camera.Position,
            SunDir, SunColour, FogColour, FogNearM, FogFarM,
            this.Field.SeaLevelM, this.Field.Size, HeightMapTexelM, 1.0
        );

        // Draw far -> near so transparent water blends correctly across annuli.
        this.OceanRenderer.DrawRings(
            ViewProjection, this.Camera.Position,
            SunDir, SunColour, FogColour, FogNearM, FogFarM,
            this.Field.SeaLevelM,
            this.TerrainRenderer.GetHeightMapTexture(),
            this.Field.Size, HeightMapTexelM, 1.0,
            this.Settings,
            this.Settings.WireframeEnabled, this.Settings.LodColourEnabled
        );

        const Markers = this.Bases.map(Base =>
        ({
            Position: [Base.Position[0], Math.max(Base.Position[1], this.Field.SeaLevelM) + 1.5, Base.Position[2]],
            Colour: [1.0, 0.6, 0.15, 1.0]
        }));
        this.OceanRenderer.DrawMarkers(ViewProjection, Markers);

        this.DrawOverlay(ViewProjection, Aspect);
        this.Editor.UpdateMiniPreview();
        this.UpdateReadout();
    }

    DrawOverlay(ViewProjection, Aspect)
    {
        const Context = this.Overlay.getContext('2d');
        Context.clearRect(0, 0, this.Overlay.width, this.Overlay.height);
        Context.lineWidth = 2;
        Context.strokeStyle = '#00d9ff';
        Context.beginPath();
        Context.moveTo(this.Overlay.width / 2 - 10, this.Overlay.height / 2);
        Context.lineTo(this.Overlay.width / 2 + 10, this.Overlay.height / 2);
        Context.moveTo(this.Overlay.width / 2, this.Overlay.height / 2 - 10);
        Context.lineTo(this.Overlay.width / 2, this.Overlay.height / 2 + 10);
        Context.stroke();

        Context.font = '12px ui-monospace, monospace';
        Context.fillStyle = '#ff9b44';
        for (const Base of this.Bases)
        {
            const Screen = ProjectWorldToScreen(ViewProjection, Base.Position, this.Overlay.width, this.Overlay.height);
            if (!Screen) continue;
            Context.fillText(Base.Name, Screen[0] + 10, Screen[1] - 10);
        }

        Context.fillStyle = '#cfdce4';
        Context.font = '12px ui-monospace, monospace';
        Context.fillText('FPS ' + this.Fps.toFixed(0), 12, 24);
        Context.fillText('Cam ' + this.Camera.Position[0].toFixed(0) + ',' + this.Camera.Position[2].toFixed(0) + ' m', 12, 42);
        Context.fillText('Sea ' + this.Field.SeaLevelM.toFixed(0) + ' m  |  ' + (this.PointerLocked ? 'FLY' : 'ORBIT'), 12, 60);
    }

    Loop()
    {
        const Now = performance.now();
        let Delta = (Now - this.LastTime) / 1000.0;
        this.LastTime = Now;
        if (Delta > 0.05) Delta = 0.05;

        this.FrameCount++;
        this.FpsTime += Delta;
        if (this.FpsTime >= 1.0)
        {
            this.Fps = this.FrameCount;
            this.FrameCount = 0;
            this.FpsTime = 0.0;
        }

        this.Update(Delta);
        this.Render();
        requestAnimationFrame(() => this.Loop());
    }
}

// [clip] - Four-component transform helper for ray math.
function TransformPoint4(Matrix, Point)
{
    return [
        Matrix[0] * Point[0] + Matrix[4] * Point[1] + Matrix[8] * Point[2] + Matrix[12] * Point[3],
        Matrix[1] * Point[0] + Matrix[5] * Point[1] + Matrix[9] * Point[2] + Matrix[13] * Point[3],
        Matrix[2] * Point[0] + Matrix[6] * Point[1] + Matrix[10] * Point[2] + Matrix[14] * Point[3],
        Matrix[3] * Point[0] + Matrix[7] * Point[1] + Matrix[11] * Point[2] + Matrix[15] * Point[3]
    ];
}

function ProjectWorldToScreen(ViewProjection, Position, Width, Height)
{
    const Clip = TransformPoint4(ViewProjection, [Position[0], Position[1], Position[2], 1.0]);
    if (Clip[3] <= 0.0) return null;
    const NdcX = Clip[0] / Clip[3];
    const NdcY = Clip[1] / Clip[3];
    const NdcZ = Clip[2] / Clip[3];
    if (NdcZ < 0.0 || NdcZ > 1.0) return null;
    return [
        (NdcX + 1.0) * 0.5 * Width,
        (1.0 - NdcY) * 0.5 * Height
    ];
}

new WorldGeneratorApp();
