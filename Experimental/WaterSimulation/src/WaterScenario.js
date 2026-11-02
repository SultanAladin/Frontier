/*====================================================================================================================================
                                                        WATER SCENARIO.JS
====================================================================================================================================*/
// 🧩 Frame orchestration: simulation ordering, mirrored reflection pass, deferred scene, and post resolve

import { OrbitCamera } from './OrbitCamera.js';
import { GeometryBuffer, BuildRadialSurfaceGrid, BuildTerrainGrid, BuildOverlayLineGrid, BuildVesselHull } from './GeometryAssembly.js';
import { TerrainAtlas } from './TerrainAtlas.js';
import { SpectralOceanField } from './SpectralOceanField.js';
import { WaveFieldSolver } from './WaveFieldSolver.js';
import { FoamField } from './FoamField.js';
import { WaterHeightAtlas } from './WaterHeightAtlas.js';
import { SprayParticles } from './SprayParticles.js';
import { GerstnerWaveSet } from './GerstnerWaveSet.js';
import { WaveUniformBinder, AtmosphereUniformBinder } from './UniformBinders.js';
import { PostProcessSequence } from './PostProcessSequence.js';
import { VesselBody } from './VesselBody.js';
import { DomainExtent, QualityProfiles } from './ScenarioSettings.js';
import { Mat4Perspective, Mat4LookAt, Mat4Multiply } from './LinearAlgebra.js';
import { InvertMatrix4 } from './OrbitCamera.js';

const AbsorptionCoefficient = [0.42, 0.115, 0.072];

export class WaterScenario
{
    constructor(Canvas, Device, Programs, Settings, Report)
    {
        this.Canvas = Canvas;
        this.Device = Device;
        this.Programs = Programs;
        this.Settings = Settings;
        this.Report = Report;

        this.SimulationTime = 0.0;
        this.WaveTime = 0.0;
        this.FrameIndex = 0;
        this.SpectralSignature = '';
        this.GerstnerSignature = '';
        this.ReflectionViewProjection = new Float32Array(16);
        this.ReflectionInverseViewProjection = new Float32Array(16);

        this.Spectral = new SpectralOceanField(Device, Programs, { Resolution: 256 });
        this.Gerstner = new GerstnerWaveSet();
        this.ShallowField = new WaveFieldSolver(Device, Programs, {
            Label:         'ShallowSea',
            Domain:        DomainExtent,
            Resolution:    512,
            CelerityScale: 1.0,
            Damping:       0.9985,
            WindForcing:   1.0,
            MaxHeightRatio: 0.5
        });
        this.RippleField = new WaveFieldSolver(Device, Programs, {
            Label:         'RippleField',
            Domain:        DomainExtent,
            Resolution:    1024,
            CelerityScale: 0.42,
            Damping:       0.962,
            WindForcing:   0.0,
            MaxHeightRatio: 0.9
        });
        this.Foam = new FoamField(Device, Programs, { Domain: DomainExtent, Resolution: 1024 });
        this.Terrain = new TerrainAtlas(Device, Programs, { Domain: 900, Resolution: 1024 });
        this.HeightAtlas = new WaterHeightAtlas(Device, Programs, { Domain: DomainExtent, Resolution: 256 });
        this.Spray = new SprayParticles(Device, Programs, { Capacity: 65536 });
        this.Binder = new WaveUniformBinder(this.Spectral, this.Gerstner, this.ShallowField, this.RippleField);
        this.Atmosphere = new AtmosphereUniformBinder().Configure(Settings);
        this.Vessel = new VesselBody({ CourseCenter: [0, -6], CourseRadius: 36 });

        this.Camera = new OrbitCamera(Canvas, {
            Yaw:      0.42,
            Pitch:    0.135,
            Distance: 92,
            Target:   [0, 0.8, 12]
        });

        this.Post = new PostProcessSequence(Device, Programs);

        this.OceanMesh = new GeometryBuffer(Device, BuildRadialSurfaceGrid(190, 384, 2.4));
        this.TerrainMesh = new GeometryBuffer(Device, BuildTerrainGrid(512));
        this.OverlayMesh = new GeometryBuffer(Device, BuildOverlayLineGrid(DomainExtent, 25, 48));
        this.VesselMesh = new GeometryBuffer(Device, BuildVesselHull());

        this.CreateTargets();
        this.SeedWaveFields();
        this.Resize();

        if (this.Report)
        {
            this.Report('Simulation', `FFT ${this.Spectral.Resolution}² × 3 cascades · grids 512² / 1024²`);
        }
    }

    CreateTargets()
    {
        const Device = this.Device;

        this.SceneTarget = Device.CreateTarget({
            Label:  'SceneTarget',
            Width:  2,
            Height: 2,
            Format: 'rgba16f',
            Filter: 'linear',
            Depth:  'renderbuffer'
        });

        this.ColorCopy = Device.CreateTarget({
            Label:  'SceneColorCopy',
            Width:  2,
            Height: 2,
            Format: 'rgba16f',
            Filter: 'linear'
        });

        this.DepthCopy = Device.CreateTarget({
            Label:  'SceneDepthCopy',
            Width:  2,
            Height: 2,
            Format: 'rgba8',
            Filter: 'nearest',
            Depth:  'texture'
        });

        this.ReflectionTarget = Device.CreateTarget({
            Label:  'ReflectionTarget',
            Width:  2,
            Height: 2,
            Format: 'rgba16f',
            Filter: 'linear',
            Depth:  'renderbuffer'
        });
    }

    // 💡 Seeding the grid solves means the third model is already alive when it is selected
    SeedWaveFields()
    {
        for (let Index = 0; Index < 220; Index++)
        {
            const Angle = Math.random() * Math.PI * 2.0;
            const Radius = 12.0 + Math.random() * 150.0;
            const Strength = (Math.random() - 0.5) * 2.4;

            this.ShallowField.QueueSplat(Math.cos(Angle) * Radius, Math.sin(Angle) * Radius - 20.0, Strength,
                                         18.0 + Math.random() * 34.0);
        }
    }

    Resize()
    {
        const Width = this.Canvas.width;
        const Height = this.Canvas.height;

        this.SceneTarget.Resize(Width, Height);
        this.ColorCopy.Resize(Width, Height);
        this.DepthCopy.Resize(Width, Height);
        this.ReflectionTarget.Resize(Math.max(2, Width >> 1), Math.max(2, Height >> 1));
        this.Post.Resize(Width, Height);
    }

    /*--------------------------------------------------------------------------------------------------------------------------------
                                                        SIMULATION
    --------------------------------------------------------------------------------------------------------------------------------*/

    UpdateSimulation(DeltaSeconds, WindDirection, WindBearing)
    {
        const Settings = this.Settings;
        const Quality = QualityProfiles[Settings.QualityCategory] ?? QualityProfiles[2];
        const Frozen = Settings.SimulationPaused;
        const Step = Frozen ? 0.0 : DeltaSeconds;

        if (!Frozen)
        {
            this.SimulationTime += DeltaSeconds;
            this.WaveTime += DeltaSeconds * Settings.WaveSpeed;
        }

        this.Spectral.ActiveCascadeCount = Quality.CascadeCount;
        this.Spray.ActiveRatio = Settings.SprayEnabled ? Quality.SprayRatio * Math.min(1.0, Settings.SprayDensity) : 0.0;
        this.Atmosphere.Configure(Settings);

        const WindSignature = `${Settings.WindSpeed.toFixed(2)}:${WindBearing.toFixed(3)}:${Settings.WaveHeight.toFixed(3)}`;

        if (Settings.SimulationCategory === 0)
        {
            if (WindSignature !== this.SpectralSignature)
            {
                this.SpectralSignature = WindSignature;
                this.Spectral.Configure(Settings, WindBearing);
            }

            this.Spectral.Choppiness = Settings.Choppiness;
            this.Spectral.Update(this.WaveTime);
        }
        else
        {
            this.SpectralSignature = '';
        }

        if (Settings.SimulationCategory === 1 && WindSignature !== this.GerstnerSignature)
        {
            this.GerstnerSignature = WindSignature;
            this.Gerstner.Configure(Settings.WaveHeight, WindBearing, Settings.Choppiness);
        }

        if (Settings.SprayEnabled)
        {
            this.ConfigureVesselEmitters(WindDirection);
            this.Spray.Update(Step, this.HeightAtlas.Texture, this.HeightAtlas.Domain, WindDirection);
            this.Spray.Deposit(this.RippleField.Targets[this.RippleField.Current], DomainExtent, 0.0, 1.0);

            if (Settings.FoamEnabled)
            {
                this.Spray.Deposit(this.Foam.Targets[this.Foam.Current], DomainExtent, 1.0, 0.0);
            }
        }

        // 📝 Both grids run every frame while the prototype is live, so switching models is instant
        const RippleSkip = Quality.RippleDivisor > 1 && (this.FrameIndex % Quality.RippleDivisor !== 0);
        const RippleStep = RippleSkip ? 0.0 : Step;
        this.ShallowField.Step(Step, this.SimulationTime, WindDirection, Settings, this.Terrain);
        this.RippleField.Step(RippleStep, this.SimulationTime, WindDirection, Settings, this.Terrain);

        if (Settings.FoamEnabled)
        {
            const HeightSource = Settings.SimulationCategory === 2 ? this.ShallowField.Texture : this.RippleField.Texture;
            this.Foam.Step(Step, HeightSource, Settings, this.Terrain, WindDirection);
        }

        this.HeightAtlas.Update(this.Binder, Settings, this.SimulationTime, this.Terrain);

        if (Settings.VesselEnabled)
        {
            if (this.FrameIndex % 2 === 0)
            {
                this.HeightAtlas.RefreshWindow(this.Vessel.Position[0], this.Vessel.Position[2]);
            }

            const Sampler = (WorldX, WorldZ) => this.HeightAtlas.SampleHeight(WorldX, WorldZ);
            this.Vessel.Update(Step, Sampler, Settings, this.SimulationTime);
            this.Vessel.QueueWake(Settings.RippleEnabled ? this.RippleField : null,
                                  Settings.FoamEnabled ? this.Foam : null,
                                  Settings, Step);
        }

        this.FrameIndex++;
    }

    ConfigureVesselEmitters(WindDirection)
    {
        const Settings = this.Settings;
        this.Spray.ClearEmitters();

        // Shore break always contributes spray so the surf line reads even without the vessel
        this.Spray.SetEmitter(2, {
            Position:  [16.0, 0.0, 36.0],
            Direction: [WindDirection[0] * 0.4, 0.75, WindDirection[1] * 0.4],
            Spread:    0.95,
            Radius:    8.0,
            Speed:     0.8 + Settings.WaveHeight * 0.6,
            Weight:    0.2 + Settings.WaveHeight * 0.25,
            Category:  1.0
        });

        if (!Settings.VesselEnabled)
        {
            return;
        }

        const Cosine = Math.cos(this.Vessel.Heading);
        const Sine = Math.sin(this.Vessel.Heading);
        const Throttle = Settings.VesselThrottle;
        const Stern = [this.Vessel.Position[0] + Sine * 3.1, this.Vessel.Position[1] + 0.1,
                       this.Vessel.Position[2] + Cosine * 3.1];
        const Bow = [this.Vessel.Position[0] - Sine * 2.9, this.Vessel.Position[1] + 0.12,
                     this.Vessel.Position[2] - Cosine * 2.9];

        this.Spray.SetEmitter(0, {
            Position:  Stern,
            Direction: [Sine * 0.45, 0.55, Cosine * 0.45],
            Spread:    0.55,
            Radius:    0.45,
            Speed:     1.1 + this.Vessel.Speed * 0.55,
            Weight:    0.25 + Throttle * 1.1,
            Category:  0.0
        });

        this.Spray.SetEmitter(1, {
            Position:  Bow,
            Direction: [-Sine * 0.15, 0.85, -Cosine * 0.15],
            Spread:    0.75,
            Radius:    0.6,
            Speed:     0.9 + this.Vessel.Speed * 0.45,
            Weight:    0.12 + this.Vessel.SplashBurst * 0.9,
            Category:  0.0
        });
    }

    /*--------------------------------------------------------------------------------------------------------------------------------
                                                        RENDERING
    --------------------------------------------------------------------------------------------------------------------------------*/

    UpdateReflectionMatrices()
    {
        const Eye = this.Camera.EyePosition;
        const Target = this.Camera.Target;
        const MirrorEye = [Eye[0], -Eye[1], Eye[2]];
        this.MirrorEye = MirrorEye;
        const MirrorTarget = [Target[0], -Target[1], Target[2]];
        const MirrorView = new Float32Array(16);
        const MirrorProjection = new Float32Array(16);

        Mat4LookAt(MirrorView, MirrorEye, MirrorTarget, [0, 1, 0]);
        Mat4Perspective(MirrorProjection, this.Camera.FieldOfView,
                        this.Canvas.width / this.Canvas.height, this.Camera.NearPlane, this.Camera.FarPlane);

        const ViewProjection = new Float32Array(16);
        Mat4Multiply(ViewProjection, MirrorProjection, MirrorView);
        this.ReflectionViewProjection.set(ViewProjection);
        InvertMatrix4(this.ReflectionInverseViewProjection, ViewProjection);
    }

    RenderReflection()
    {
        const Gl = this.Device.Gl;
        const Settings = this.Settings;
        const Device = this.Device;

        if (!Settings.ReflectionEnabled)
        {
            return;
        }

        this.UpdateReflectionMatrices();
        this.ReflectionTarget.Bind();
        Gl.clearColor(0.02, 0.04, 0.07, 1);
        Gl.depthMask(true);
        Gl.clear(Gl.COLOR_BUFFER_BIT | Gl.DEPTH_BUFFER_BIT);

        Gl.disable(Gl.DEPTH_TEST);
        Gl.depthMask(false);
        const Sky = this.Programs.get('SkyRadiance').Use();
        Sky.Uniforms.SetMatrix4('uInverseViewProjection', this.ReflectionInverseViewProjection);
        Sky.Uniforms.SetVec3('uCameraPosition', this.MirrorEye[0], this.MirrorEye[1], this.MirrorEye[2]);
        this.Atmosphere.Apply(Sky.Uniforms, Settings, this.SimulationTime);
        Device.DrawFullscreen();

        Gl.enable(Gl.DEPTH_TEST);
        Gl.depthFunc(Gl.LEQUAL);
        Gl.depthMask(true);

        const Terrain = this.Programs.get('ShoreTerrain').Use();
        this.ApplyTerrainUniforms(Terrain, this.ReflectionViewProjection, 1);
        this.TerrainMesh.Draw(Terrain);

        if (Settings.VesselEnabled)
        {
            const Vessel = this.Programs.get('VesselHull').Use();
            Vessel.Uniforms.SetMatrix4('uVesselTransform', this.Vessel.Transform);
            Vessel.Uniforms.SetMatrix4('uViewProjection', this.ReflectionViewProjection);
            Vessel.Uniforms.SetVec3('uCameraPosition', this.MirrorEye[0], this.MirrorEye[1], this.MirrorEye[2]);
            Vessel.Uniforms.SetInt('uReflectionPass', 1);
            this.Atmosphere.Apply(Vessel.Uniforms, Settings, this.SimulationTime);
            this.VesselMesh.Draw(Vessel);
        }

        Gl.bindFramebuffer(Gl.FRAMEBUFFER, null);
    }

    ApplyTerrainUniforms(Program, ViewProjection, ClipBelowWater)
    {
        const Settings = this.Settings;
        const Uniforms = Program.Uniforms;

        Uniforms.SetMatrix4('uViewProjection', ViewProjection);
        Uniforms.SetFloat('uTerrainExtent', 900);
        Uniforms.SetVec3('uCameraPosition', this.Camera.EyePosition[0], this.Camera.EyePosition[1], this.Camera.EyePosition[2]);
        Uniforms.SetInt('uClipBelowWater', ClipBelowWater);
        Uniforms.SetVec3('uAbsorption', AbsorptionCoefficient[0], AbsorptionCoefficient[1], AbsorptionCoefficient[2]);
        Uniforms.SetFloat('uWaterClarity', Settings.WaterClarity);
        Uniforms.SetInt('uSubsurfaceEnabled', Settings.SubsurfaceEnabled ? 1 : 0);
        Uniforms.SetFloat('uSimulationTime', this.SimulationTime);
        Uniforms.SetFloat('uFoamIntensity', Settings.FoamIntensity);
        Uniforms.SetFloat('uMicroDetail', Settings.MicroDetail);
        Uniforms.SetInt('uRippleEnabled', 0);
        this.Terrain.Bind(Uniforms);
        this.Atmosphere.Apply(Uniforms, Settings, this.SimulationTime);
    }

    ApplyWaterUniforms(Program)
    {
        const Settings = this.Settings;
        const Uniforms = Program.Uniforms;
        const Camera = this.Camera;

        this.Binder.Apply(Uniforms, Settings, this.SimulationTime);
        this.Terrain.Bind(Uniforms);
        this.Atmosphere.Apply(Uniforms, Settings, this.SimulationTime);

        Uniforms.SetMatrix4('uViewProjection', Camera.ViewProjection);
        Uniforms.SetMatrix4('uReflectionViewProjection', this.ReflectionViewProjection);
        Uniforms.SetVec3('uCameraPosition', Camera.EyePosition[0], Camera.EyePosition[1], Camera.EyePosition[2]);
        Uniforms.SetVec2('uGridCenter', Camera.EyePosition[0], Camera.EyePosition[2]);
        Uniforms.SetFloat('uGridRadius', Math.max(320, Camera.Distance * 26));

        Uniforms.SetTexture('uReflectionTexture', this.ReflectionTarget.ColorTexture, 9);
        Uniforms.SetTexture('uSceneColor', this.ColorCopy.ColorTexture, 10);
        Uniforms.SetTexture('uSceneDepth', this.DepthCopy.DepthTexture, 11);
        Uniforms.SetTexture('uFoamField', this.Foam.Texture, 2);

        Uniforms.SetFloat('uDomain', DomainExtent);
        Uniforms.SetFloat('uNearPlane', Camera.NearPlane);
        Uniforms.SetFloat('uFarPlane', Camera.FarPlane);
        Uniforms.SetVec2('uViewport', this.Canvas.width, this.Canvas.height);
        Uniforms.SetInt('uReflectionEnabled', Settings.ReflectionEnabled ? 1 : 0);
        Uniforms.SetInt('uRefractionEnabled', Settings.RefractionEnabled ? 1 : 0);
        Uniforms.SetInt('uSubsurfaceEnabled', Settings.SubsurfaceEnabled ? 1 : 0);
        Uniforms.SetInt('uFoamEnabled', Settings.FoamEnabled ? 1 : 0);
        Uniforms.SetInt('uFieldViewMode', Settings.FieldView | 0);
        Uniforms.SetFloat('uWaterClarity', Settings.WaterClarity);
        Uniforms.SetVec3('uAbsorption', AbsorptionCoefficient[0], AbsorptionCoefficient[1], AbsorptionCoefficient[2]);
        Uniforms.SetFloat('uShoreFoamWidth', 2.8);
        Uniforms.SetFloat('uFoamIntensity', Settings.FoamIntensity);
        Uniforms.SetFloat('uMicroDetail', Settings.MicroDetail);
        Uniforms.SetFloat('uSimulationTime', this.SimulationTime);
    }

    RenderScene()
    {
        const Gl = this.Device.Gl;
        const Device = this.Device;
        const Settings = this.Settings;

        this.SceneTarget.Bind();
        Gl.clearColor(0.015, 0.03, 0.05, 1);
        Gl.depthMask(true);
        Gl.clear(Gl.COLOR_BUFFER_BIT | Gl.DEPTH_BUFFER_BIT);

        Gl.disable(Gl.DEPTH_TEST);
        Gl.depthMask(false);
        const Sky = this.Programs.get('SkyRadiance').Use();
        Sky.Uniforms.SetMatrix4('uInverseViewProjection', this.Camera.InverseViewProjection);
        Sky.Uniforms.SetVec3('uCameraPosition', this.Camera.EyePosition[0], this.Camera.EyePosition[1],
                             this.Camera.EyePosition[2]);
        this.Atmosphere.Apply(Sky.Uniforms, Settings, this.SimulationTime);
        Device.DrawFullscreen();

        Gl.enable(Gl.DEPTH_TEST);
        Gl.depthFunc(Gl.LEQUAL);
        Gl.depthMask(true);

        const Terrain = this.Programs.get('ShoreTerrain').Use();
        this.ApplyTerrainUniforms(Terrain, this.Camera.ViewProjection, 0);
        this.TerrainMesh.Draw(Terrain);

        if (Settings.VesselEnabled)
        {
            const Vessel = this.Programs.get('VesselHull').Use();
            Vessel.Uniforms.SetMatrix4('uVesselTransform', this.Vessel.Transform);
            Vessel.Uniforms.SetMatrix4('uViewProjection', this.Camera.ViewProjection);
            Vessel.Uniforms.SetVec3('uCameraPosition', this.Camera.EyePosition[0], this.Camera.EyePosition[1],
                                    this.Camera.EyePosition[2]);
            Vessel.Uniforms.SetInt('uReflectionPass', 0);
            this.Atmosphere.Apply(Vessel.Uniforms, Settings, this.SimulationTime);
            this.VesselMesh.Draw(Vessel);
        }

        Device.BlitColor(this.SceneTarget, this.ColorCopy);
        Device.BlitDepth(this.SceneTarget, this.DepthCopy);

        this.SceneTarget.Bind();
        Gl.enable(Gl.DEPTH_TEST);
        Gl.depthMask(false);

        const Water = this.Programs.get('OceanSurface').Use();
        this.ApplyWaterUniforms(Water);
        this.OceanMesh.Draw(Water);

        if (Settings.SprayEnabled && this.Spray.EmitterCount > 0)
        {
            const SprayTint = [0.92, 0.96, 1.0];
            this.Spray.Rasterize(this.Camera.ViewProjection, this.Canvas.height * 0.6,
                                 this.DepthCopy.DepthTexture, [this.Canvas.width, this.Canvas.height],
                                 this.Camera.NearPlane, this.Camera.FarPlane, SprayTint);
        }

        if (Settings.GridOverlay)
        {
            Gl.enable(Gl.BLEND);
            Gl.blendFunc(Gl.SRC_ALPHA, Gl.ONE_MINUS_SRC_ALPHA);
            const Overlay = this.Programs.get('SimulationGridOverlay').Use();
            this.Binder.Apply(Overlay.Uniforms, Settings, this.SimulationTime);
            this.Terrain.Bind(Overlay.Uniforms);
            Overlay.Uniforms.SetMatrix4('uViewProjection', this.Camera.ViewProjection);
            Overlay.Uniforms.SetFloat('uOverlayLift', 0.06);
            Overlay.Uniforms.SetVec3('uOverlayColor', 0.36, 0.86, 1.0);
            Overlay.Uniforms.SetFloat('uOverlayOpacity', 0.42);
            this.OverlayMesh.Draw(Overlay);
            Gl.disable(Gl.BLEND);
        }

        Gl.depthMask(true);
        Gl.bindFramebuffer(Gl.FRAMEBUFFER, null);
    }

    Frame(DeltaSeconds)
    {
        const Settings = this.Settings;
        const WindBearing = (Settings.WindAngle * Math.PI) / 180.0;
        const WindDirection = [Math.cos(WindBearing), Math.sin(WindBearing)];

        this.Camera.AutoOrbitRate = Settings.AutoOrbit ? 0.055 : 0.0;
        this.Camera.Update(DeltaSeconds);

        this.UpdateSimulation(DeltaSeconds, WindDirection, WindBearing);

        this.RenderReflection();
        this.RenderScene();
        this.Post.Resolve(this.SceneTarget, Settings);
    }
}

