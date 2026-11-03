/*============================================================================================================================================
                                                      VIEWPORTRENDERER.JS
============================================================================================================================================*/
// 🧩 Frame composition: atmospheric sky, crust clipmaps, simulated ocean tiers, spectral shell, planet projection and editor overlays

import {
    BindLineIndices, BindTriangleIndices, CreateElevationTexture, CreateGridGeometry, CreateOverlayGeometry,
    CreatePolygonSurfaceGeometry, CreateProgramBundle, RefreshElevationRegion, UploadOverlayLines
} from './GlResources.js';
import {
    FullscreenVertexShader, OverlayFragmentShader, OverlayVertexShader, SkyFragmentShader,
    SurfaceFragmentShader, SurfaceVertexShader, TerrainFragmentShader, TerrainVertexShader,
    WaterFragmentShader, WaterVertexShader
} from './Shaders.js';

const ViewModeCodes = Object.freeze({
    beauty: { Terrain: 0, Water: 0 },
    bathymetry: { Terrain: 1, Water: 0 },
    lod: { Terrain: 2, Water: 4 },
    eta: { Terrain: 1, Water: 2 },
    velocity: { Terrain: 1, Water: 3 }
});

function Normalize(Vector)
{
    const Length = Math.hypot(Vector[0], Vector[1], Vector[2]) || 1;
    return [Vector[0] / Length, Vector[1] / Length, Vector[2] / Length];
}

function CrossProduct(First, Second)
{
    return [
        First[1] * Second[2] - First[2] * Second[1],
        First[2] * Second[0] - First[0] * Second[2],
        First[0] * Second[1] - First[1] * Second[0]
    ];
}

export class ViewportRenderer
{
    constructor(Canvas, Capabilities, Heightfield, WaterConfiguration, TerrainClipmap)
    {
        this.Canvas = Canvas;
        this.Context = Capabilities.Context;
        this.Capabilities = Capabilities;
        this.Heightfield = Heightfield;
        this.WaterConfiguration = WaterConfiguration;
        this.TerrainClipmap = TerrainClipmap;
        this.PlanetRadius = 6371000;
        this.VerticalExaggeration = 26;
        this.ElevationTexture = CreateElevationTexture(this.Context, Heightfield.Resolution, Heightfield.Elevations);
        this.Programs = {
            Sky: CreateProgramBundle(this.Context, FullscreenVertexShader, SkyFragmentShader, 'Atmosphere'),
            Terrain: CreateProgramBundle(this.Context, TerrainVertexShader, TerrainFragmentShader, 'CrustClipmap'),
            Water: CreateProgramBundle(this.Context, WaterVertexShader, WaterFragmentShader, 'OceanSurface'),
            Surface: CreateProgramBundle(this.Context, SurfaceVertexShader, SurfaceFragmentShader, 'ConvertedSurface'),
            Overlay: CreateProgramBundle(this.Context, OverlayVertexShader, OverlayFragmentShader, 'EditorOverlay')
        };
        this.Geometry = {
            Fullscreen: this.Context.createVertexArray(),
            Terrain: CreateGridGeometry(this.Context, TerrainClipmap.GridSegments),
            Water: CreateGridGeometry(this.Context, 128),
            Shell: CreateGridGeometry(this.Context, 96),
            Sphere: CreateGridGeometry(this.Context, 192),
            Overlay: CreateOverlayGeometry(this.Context)
        };
        this.SurfaceGeometry = null;
        this.FrameIndex = 0;
        this.LastDrawCalls = 0;
        this.LastTriangleCount = 0;
        this.ConfigureViewport();
    }

    ConfigureViewport()
    {
        const Ratio = Math.min(window.devicePixelRatio || 1, 1.75);
        const Width = Math.max(1, Math.floor(this.Canvas.clientWidth * Ratio));
        const Height = Math.max(1, Math.floor(this.Canvas.clientHeight * Ratio));
        if (this.Canvas.width !== Width || this.Canvas.height !== Height)
        {
            this.Canvas.width = Width;
            this.Canvas.height = Height;
        }
        return { Width, Height, Aspect: Width / Height };
    }

    RefreshElevation(Region = null)
    {
        const Context = this.Context;
        if (Region && Region.Width > 0 && Region.Height > 0)
        {
            RefreshElevationRegion(Context, this.ElevationTexture, this.Heightfield.Resolution, {
                ...Region,
                Samples: this.Heightfield.ExtractRegion(Region.FirstColumn, Region.FirstRow, Region.Width, Region.Height)
            });
        }
        else
        {
            Context.bindTexture(Context.TEXTURE_2D, this.ElevationTexture);
            Context.pixelStorei(Context.UNPACK_ALIGNMENT, 1);
            Context.texSubImage2D(Context.TEXTURE_2D, 0, 0, 0, this.Heightfield.Resolution, this.Heightfield.Resolution,
                Context.RED, Context.FLOAT, this.Heightfield.Elevations);
        }
    }

    AttachPolygonSurface(Surface)
    {
        this.SurfaceGeometry = CreatePolygonSurfaceGeometry(this.Context, Surface);
    }

    BindTexture(Unit, Texture)
    {
        const Context = this.Context;
        Context.activeTexture(Context.TEXTURE0 + Unit);
        Context.bindTexture(Context.TEXTURE_2D, Texture);
    }

    ApplyCameraUniforms(Program, FrameSpecification, Basis, CameraAbsolute, Aspect)
    {
        const Camera = FrameSpecification.Camera;
        const Tangent = Math.tan(Camera.FieldOfViewDegrees * Math.PI / 360);
        Program.SetVec3('CameraRight', Basis.Right[0], Basis.Right[1], Basis.Right[2]);
        Program.SetVec3('CameraUp', Basis.Up[0], Basis.Up[1], Basis.Up[2]);
        Program.SetVec3('CameraForward', Basis.Forward[0], Basis.Forward[1], Basis.Forward[2]);
        Program.SetVec3('CameraAbsolute', CameraAbsolute[0], CameraAbsolute[1], CameraAbsolute[2]);
        Program.SetVec2('ProjectionScale', 1 / (Tangent * Aspect), 1 / Tangent);
        Program.SetFloat('NearPlane', Camera.NearPlane);
        Program.SetFloat('FarPlane', Camera.FarPlane);
        Program.SetFloat('LogFarPlane', Math.log2(1 + Camera.FarPlane));
        Program.SetVec3('SunDirection', FrameSpecification.SunDirection[0], FrameSpecification.SunDirection[1], FrameSpecification.SunDirection[2]);
        Program.SetFloat('HazeDensity', FrameSpecification.HazeDensity);
    }

    ResolvePlanetBasis(Planet)
    {
        const CosinePitch = Math.cos(Planet.Pitch);
        const Position = [
            Planet.Distance * Math.cos(Planet.Pitch) * Math.sin(Planet.Yaw),
            Planet.Distance * Math.sin(Planet.Pitch),
            Planet.Distance * Math.cos(Planet.Pitch) * Math.cos(Planet.Yaw)
        ];
        const Forward = Normalize([-Position[0], -Position[1], -Position[2]]);
        const Right = Normalize(CrossProduct(Forward, [0, 1, 0]));
        const Up = Normalize(CrossProduct(Right, Forward));
        return { Position, Forward, Right, Up };
    }

    Rasterize(FrameSpecification)
    {
        const Context = this.Context;
        const { Width, Height, Aspect } = this.ConfigureViewport();
        const PlanetMode = FrameSpecification.Mode === 'Planet';
        const PlanetBasis = PlanetMode ? this.ResolvePlanetBasis(FrameSpecification.Planet) : null;
        const Basis = PlanetMode ? PlanetBasis : FrameSpecification.Camera.ResolveBasis();
        const CameraAbsolute = PlanetMode ? PlanetBasis.Position : FrameSpecification.Camera.Position;
        const CameraX = PlanetMode ? 0 : FrameSpecification.Camera.Position[0];
        const CameraY = PlanetMode ? 0 : FrameSpecification.Camera.Position[1];
        const CameraZ = PlanetMode ? 0 : FrameSpecification.Camera.Position[2];
        const Codes = ViewModeCodes[FrameSpecification.ViewMode] || ViewModeCodes.beauty;
        const DomainExtent = this.Heightfield.Extent;
        let DrawCalls = 0;
        let TriangleCount = 0;

        Context.viewport(0, 0, Width, Height);
        Context.clearColor(0.05, 0.09, 0.16, 1);
        Context.clear(Context.COLOR_BUFFER_BIT | Context.DEPTH_BUFFER_BIT);
        Context.disable(Context.BLEND);
        Context.disable(Context.CULL_FACE);
        Context.disable(Context.DEPTH_TEST);
        Context.depthMask(false);

        // ① Atmosphere.
        Context.bindVertexArray(this.Geometry.Fullscreen);
        const Sky = this.Programs.Sky;
        Sky.Activate();
        this.ApplyCameraUniforms(Sky, FrameSpecification, Basis, CameraAbsolute, Aspect);
        Context.drawArrays(Context.TRIANGLES, 0, 3);
        ++DrawCalls;

        Context.depthMask(true);
        Context.enable(Context.DEPTH_TEST);
        Context.depthFunc(Context.LEQUAL);
        Context.enable(Context.POLYGON_OFFSET_FILL);

        // ② Crust: nested tessellation windows, or the same field wrapped onto a planet.
        const Terrain = this.Programs.Terrain;
        Terrain.Activate();
        this.ApplyCameraUniforms(Terrain, FrameSpecification, Basis, CameraAbsolute, Aspect);
        this.BindTexture(0, this.ElevationTexture);
        Terrain.SetInteger('ElevationField', 0);
        Terrain.SetInteger('ElevationResolution', this.Heightfield.Resolution);
        Terrain.SetFloat('DomainExtent', DomainExtent);
        Terrain.SetVec2('CameraOffset', CameraX, CameraZ);
        Terrain.SetFloat('CameraElevation', CameraY);
        Terrain.SetFloat('SeaLevel', FrameSpecification.SeaLevel);
        Terrain.SetFloat('ElapsedSeconds', FrameSpecification.ElapsedSeconds);
        Terrain.SetFloat('ContourInterval', 250);
        Terrain.SetFloat('DetailStrength', 0);
        Terrain.SetInteger('ViewMode', Codes.Terrain);
        Terrain.SetFloat('PlanetRadius', this.PlanetRadius);
        Terrain.SetFloat('VerticalExaggeration', this.VerticalExaggeration);

        if (PlanetMode)
        {
            Context.bindVertexArray(this.Geometry.Sphere.VertexArray);
            Terrain.SetInteger('GeometryMode', 1);
            Terrain.SetVec3('PatchOrigin', 0, 0, 0);
            Terrain.SetFloat('PatchExtent', 0);
            Terrain.SetVec3('TintColour', 0.3, 0.5, 0.8);
            Context.polygonOffset(0, 0);
            Context.drawElements(Context.TRIANGLES, this.Geometry.Sphere.IndexCount, Context.UNSIGNED_INT, 0);
            ++DrawCalls;
            TriangleCount += this.Geometry.Sphere.IndexCount / 3;
        }
        else
        {
            Terrain.SetInteger('GeometryMode', 0);
            Context.bindVertexArray(this.Geometry.Terrain.VertexArray);
            const Windows = FrameSpecification.TerrainWindows;
            for (let Index = Windows.length - 1; Index >= 0; --Index)
            {
                const Window = Windows[Index];
                Terrain.SetVec3('PatchOrigin', Window.OriginX - CameraX, 0, Window.OriginZ - CameraZ);
                Terrain.SetFloat('PatchExtent', Window.Extent);
                Terrain.SetVec3('TintColour', Window.Tint[0], Window.Tint[1], Window.Tint[2]);
                Context.polygonOffset(-0.35 * Window.Level, -0.35 * Window.Level);
                Context.drawElements(Context.TRIANGLES, this.Geometry.Terrain.IndexCount, Context.UNSIGNED_INT, 0);
                ++DrawCalls;
                TriangleCount += this.Geometry.Terrain.IndexCount / 3;
            }
        }

        // ③ Converted SDF polygon surface.
        if (!PlanetMode && FrameSpecification.ShowConvertedSurface && this.SurfaceGeometry)
        {
            const Surface = this.Programs.Surface;
            Surface.Activate();
            this.ApplyCameraUniforms(Surface, FrameSpecification, Basis, CameraAbsolute, Aspect);
            Context.bindVertexArray(this.SurfaceGeometry.VertexArray);
            BindTriangleIndices(Context, this.SurfaceGeometry);
            Context.polygonOffset(-1, -1);
            Context.drawElements(Context.TRIANGLES, this.SurfaceGeometry.IndexCount, Context.UNSIGNED_INT, 0);
            ++DrawCalls;
            TriangleCount += this.SurfaceGeometry.IndexCount / 3;
            if (FrameSpecification.ShowSurfaceWireframe)
            {
                BindLineIndices(Context, this.SurfaceGeometry);
                Context.polygonOffset(-2, -2);
                Context.drawElements(Context.LINES, this.SurfaceGeometry.LineIndexCount, Context.UNSIGNED_INT, 0);
                ++DrawCalls;
            }
        }

        // ④ Ocean: spectral tessellated shell beyond the outermost tier, then simulated nested tiers.
        const Water = this.Programs.Water;
        Water.Activate();
        this.ApplyCameraUniforms(Water, FrameSpecification, Basis, CameraAbsolute, Aspect);
        this.BindTexture(0, this.ElevationTexture);
        Water.SetInteger('ElevationField', 0);
        Water.SetInteger('StateField', 1);
        Water.SetInteger('GridResolution', this.WaterConfiguration.GridResolution);
        Water.SetInteger('ViewMode', Codes.Water);
        Water.SetFloat('SeaLevel', FrameSpecification.SeaLevel);
        Water.SetFloat('CameraElevation', CameraY);
        Water.SetVec2('CameraOffset', CameraX, CameraZ);
        Water.SetFloat('DomainExtent', DomainExtent);
        Water.SetInteger('ElevationResolution', this.Heightfield.Resolution);
        Water.SetFloat('ElapsedSeconds', FrameSpecification.ElapsedSeconds);
        Water.SetFloat('SwellAmplitude', FrameSpecification.Spectral.Amplitude);
        Water.SetFloat('SwellWavelength', FrameSpecification.Spectral.Wavelength);
        Water.SetVec2('SwellDirection', FrameSpecification.Spectral.Direction[0], FrameSpecification.Spectral.Direction[1]);
        Water.SetFloat('SpectralSteepness', FrameSpecification.Spectral.Steepness);
        Water.SetFloat('PlanetRadius', this.PlanetRadius);
        this.BindTexture(1, FrameSpecification.Water.ReadTarget(0).Texture);

        if (PlanetMode)
        {
            Context.bindVertexArray(this.Geometry.Sphere.VertexArray);
            Water.SetInteger('DisplacementSource', 2);
            Water.SetInteger('GeometryMode', 1);
            Water.SetVec2('WindowOriginRelative', 0, 0);
            Water.SetFloat('WindowExtent', 20000);
            Water.SetVec3('TintColour', 0.2, 0.45, 0.8);
            Context.polygonOffset(-0.5, -0.5);
            Context.drawElements(Context.TRIANGLES, this.Geometry.Sphere.IndexCount, Context.UNSIGNED_INT, 0);
            ++DrawCalls;
            TriangleCount += this.Geometry.Sphere.IndexCount / 3;
        }
        else
        {
            const ShellExtent = FrameSpecification.WaterWindows[FrameSpecification.WaterWindows.length - 1].Extent * 8;
            const ShellSpacing = ShellExtent / 96;
            const ShellOriginX = Math.floor(CameraX / ShellSpacing) * ShellSpacing - ShellExtent * 0.5;
            const ShellOriginZ = Math.floor(CameraZ / ShellSpacing) * ShellSpacing - ShellExtent * 0.5;
            Context.bindVertexArray(this.Geometry.Shell.VertexArray);
            Water.SetInteger('DisplacementSource', 1);
            Water.SetInteger('GeometryMode', 0);
            Water.SetVec2('WindowOriginRelative', ShellOriginX - CameraX, ShellOriginZ - CameraZ);
            Water.SetFloat('WindowExtent', ShellExtent);
            Water.SetVec3('TintColour', 0.12, 0.28, 0.5);
            Context.polygonOffset(0.4, 0.4);
            Context.drawElements(Context.TRIANGLES, this.Geometry.Shell.IndexCount, Context.UNSIGNED_INT, 0);
            ++DrawCalls;
            TriangleCount += this.Geometry.Shell.IndexCount / 3;

            Water.SetInteger('DisplacementSource', 0);
            Context.bindVertexArray(this.Geometry.Water.VertexArray);
            for (let Index = FrameSpecification.WaterWindows.length - 1; Index >= 0; --Index)
            {
                const Window = FrameSpecification.WaterWindows[Index];
                this.BindTexture(1, FrameSpecification.Water.ReadTarget(Index).Texture);
                Water.SetVec2('WindowOriginRelative', Window.WorldOriginX - CameraX, Window.WorldOriginZ - CameraZ);
                Water.SetFloat('WindowExtent', Window.Extent);
                Water.SetFloat('CellSpacing', Window.CellSpacing);
                Water.SetIVec2('Shift', Window.ShiftX, Window.ShiftZ);
                Water.SetVec3('TintColour', Window.Level % 2 === 0 ? 0.12 : 0.72, 0.55 + Window.Level * 0.03, 0.94 - Window.Level * 0.05);
                Context.polygonOffset(-0.5 - 0.3 * (FrameSpecification.WaterWindows.length - Index), -0.5 - 0.3 * (FrameSpecification.WaterWindows.length - Index));
                Context.drawElements(Context.TRIANGLES, this.Geometry.Water.IndexCount, Context.UNSIGNED_INT, 0);
                ++DrawCalls;
                TriangleCount += this.Geometry.Water.IndexCount / 3;
            }
        }
        Context.disable(Context.POLYGON_OFFSET_FILL);

        // ⑤ Editor overlays: brush footprint, simulation cells and LOD window bounds.
        if (!PlanetMode && FrameSpecification.OverlayLines && FrameSpecification.OverlayLines.length > 0)
        {
            const Overlay = this.Programs.Overlay;
            Overlay.Activate();
            this.ApplyCameraUniforms(Overlay, FrameSpecification, Basis, CameraAbsolute, Aspect);
            Context.bindVertexArray(this.Geometry.Overlay.VertexArray);
            UploadOverlayLines(Context, this.Geometry.Overlay, FrameSpecification.OverlayLines);
            Overlay.SetVec4('OverlayColour', 1.0, 0.78, 0.22, 1.0);
            Context.disable(Context.DEPTH_TEST);
            Context.drawArrays(Context.LINES, 0, FrameSpecification.OverlayLines.length / 3);
            Context.enable(Context.DEPTH_TEST);
            ++DrawCalls;
        }

        Context.bindVertexArray(null);
        Context.bindFramebuffer(Context.FRAMEBUFFER, null);
        ++this.FrameIndex;
        this.LastDrawCalls = DrawCalls;
        this.LastTriangleCount = TriangleCount;
        return { DrawCalls, TriangleCount, Width, Height };
    }
}
