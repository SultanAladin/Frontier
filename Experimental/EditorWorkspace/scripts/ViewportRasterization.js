/*====================================================================================================================================
                                                 VIEWPORTRASTERIZATION.JS
====================================================================================================================================*/
// 🧩 WebGL2 scene rasterization: holographic surface shading, additive ground grid, selection axes, and orbit framing.

import {
    MatrixIdentity,
    MatrixInverse,
    MatrixModelPlacement,
    MatrixMultiply,
    MatrixNormalRecovery,
    MatrixOrthographic,
    MatrixPerspective,
    MatrixViewOrientation,
    ProjectPoint,
    RayFromScreen,
    VecAdd,
    VecCross,
    VecDot,
    VecNormalize,
    VecScale,
    VecSubtract
} from "./SpatialMathematics.js";

import { ConstructPrimitiveSurface } from "./PolygonSurfaceConstruction.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                    SHADER SOURCES
//------------------------------------------------------------------------------------------------------------------------

const SurfaceVertexSource = `#version 300 es
precision highp float;

layout(location = 0) in vec3 VertexPosition;
layout(location = 1) in vec3 VertexNormal;
layout(location = 2) in vec3 VertexBarycentric;

uniform mat4 ModelMatrix;
uniform mat4 ViewProjectionMatrix;
uniform mat3 NormalMatrix;

out vec3 SurfacePosition;
out vec3 SurfaceNormal;
out vec3 BarycentricCoordinates;

void main()
{
    vec4 PlacedPosition = ModelMatrix * vec4(VertexPosition, 1.0);
    SurfacePosition = PlacedPosition.xyz;
    SurfaceNormal = normalize(NormalMatrix * VertexNormal);
    BarycentricCoordinates = VertexBarycentric;
    gl_Position = ViewProjectionMatrix * PlacedPosition;
}`;

const SurfaceFragmentSource = `#version 300 es
precision highp float;

in vec3 SurfacePosition;
in vec3 SurfaceNormal;
in vec3 BarycentricCoordinates;

uniform vec3 CameraPosition;
uniform vec3 BaseColor;
uniform vec3 RimColor;
uniform float RimStrength;
uniform float WireStrength;
uniform float SweepStrength;
uniform float SweepHeight;
uniform float HighlightStrength;
uniform float ElapsedSeconds;

out vec4 FragmentColor;

void main()
{
    vec3 NormalDirection = normalize(SurfaceNormal);

    vec3 ViewDirection = normalize(CameraPosition - SurfacePosition);
    float Facing = clamp(dot(NormalDirection, ViewDirection), 0.0, 1.0);
    float Rim = pow(1.0 - Facing, 2.6);

    vec3 KeyDirection = normalize(vec3(0.42, 0.86, 0.38));
    vec3 FillDirection = normalize(vec3(-0.62, 0.14, -0.52));
    float Key = max(dot(NormalDirection, KeyDirection), 0.0);
    float Fill = max(dot(NormalDirection, FillDirection), 0.0);

    vec3 Radiance = BaseColor * (0.16 + 0.72 * Key + 0.20 * Fill);
    Radiance += RimColor * 0.05 * pow(Key, 8.0);

    // 📝 Travelling scan band; the editor timeline drives SweepHeight from the record's own vertical bounds.
    float Band = exp(-pow((SurfacePosition.y - SweepHeight) * 24.0, 2.0));
    Radiance += RimColor * (Rim * RimStrength + Band * SweepStrength);

    float Striation = smoothstep(0.82, 1.0, sin((SurfacePosition.y + ElapsedSeconds * 0.06) * 132.0));
    Radiance += RimColor * Striation * 0.03;

    // 📝 Barycentric distance to the nearest triangle edge gives a uniform-width topology wireframe.
    vec3 EdgeDerivative = fwidth(BarycentricCoordinates);
    vec3 EdgeDistance = smoothstep(vec3(0.0), EdgeDerivative * 1.25, BarycentricCoordinates);
    float EdgeMask = 1.0 - min(min(EdgeDistance.x, EdgeDistance.y), EdgeDistance.z);

    // 📝 Dense tessellation fades its own wireframe, otherwise the topology lines collapse into glare.
    float TessellationWeight = max(max(EdgeDerivative.x, EdgeDerivative.y), EdgeDerivative.z);
    float DensityFade = 1.0 - smoothstep(0.040, 0.135, TessellationWeight);

    Radiance = mix(Radiance, RimColor * 1.35, clamp(EdgeMask, 0.0, 1.0) * WireStrength * DensityFade);

    Radiance += RimColor * HighlightStrength * (0.12 + Rim * 0.85);

    FragmentColor = vec4(Radiance, 1.0);
}`;

const GridVertexSource = `#version 300 es
precision highp float;

layout(location = 0) in vec3 VertexPosition;

uniform mat4 ViewProjectionMatrix;

out vec2 GroundCoordinate;

void main()
{
    GroundCoordinate = VertexPosition.xz;
    gl_Position = ViewProjectionMatrix * vec4(VertexPosition, 1.0);
}`;

const GridFragmentSource = `#version 300 es
precision highp float;

in vec2 GroundCoordinate;

uniform float GridIntensity;
uniform vec3 GridColor;
uniform vec3 AccentColor;

out vec4 FragmentColor;

float EvaluateGridLine(vec2 Coordinate, float Spacing, float Width)
{
    vec2 Cell = abs(fract(Coordinate / Spacing - 0.5) - 0.5) * Spacing;
    vec2 Thickness = fwidth(Coordinate) * Width;
    vec2 Line = smoothstep(Thickness, vec2(0.0), Cell);
    return max(Line.x, Line.y);
}

void main()
{
    float Distance = length(GroundCoordinate);
    float RadialFade = 1.0 - smoothstep(2.6, 13.5, Distance);

    float Minor = EvaluateGridLine(GroundCoordinate, 0.5, 1.0) * 0.16;
    float Major = EvaluateGridLine(GroundCoordinate, 2.5, 1.15) * 0.34;
    float AxisHighlight = EvaluateGridLine(GroundCoordinate, 0.02, 1.6) * 0.30;

    vec3 Radiance = mix(GridColor, AccentColor, AxisHighlight * 2.2);
    float Alpha = (Minor + Major + AxisHighlight) * RadialFade * GridIntensity;

    FragmentColor = vec4(Radiance, Alpha);
}`;

const AxisVertexSource = `#version 300 es
precision highp float;

layout(location = 0) in vec3 VertexPosition;
layout(location = 1) in vec3 VertexColor;

uniform mat4 ViewProjectionMatrix;
uniform mat4 ModelMatrix;

out vec3 AxisColor;

void main()
{
    AxisColor = VertexColor;
    gl_Position = ViewProjectionMatrix * ModelMatrix * vec4(VertexPosition, 1.0);
}`;

const AxisFragmentSource = `#version 300 es
precision highp float;

in vec3 AxisColor;
out vec4 FragmentColor;

void main()
{
    FragmentColor = vec4(AxisColor, 0.55);
}`;

//------------------------------------------------------------------------------------------------------------------------
//                                                    SHADER COMPILATION
//------------------------------------------------------------------------------------------------------------------------

function CompileShaderStage(Context, StageType, Source, Diagnostics, Label)
{
    const Shader = Context.createShader(StageType);
    Context.shaderSource(Shader, Source);
    Context.compileShader(Shader);

    if (!Context.getShaderParameter(Shader, Context.COMPILE_STATUS))
    {
        const Report = Context.getShaderInfoLog(Shader) || "unknown compile failure";
        Diagnostics.push(`${Label} compile failure: ${Report}`);
        console.error(`${Label} compile failure`, Report, Source);
        Context.deleteShader(Shader);
        return null;
    }
    return Shader;
}

function LinkProgram(Context, VertexSource, FragmentSource, Diagnostics, Label)
{
    const VertexStage = CompileShaderStage(Context, Context.VERTEX_SHADER, VertexSource, Diagnostics, `${Label} vertex`);
    const FragmentStage = CompileShaderStage(Context, Context.FRAGMENT_SHADER, FragmentSource, Diagnostics, `${Label} fragment`);
    if (!VertexStage || !FragmentStage)
    {
        return null;
    }

    const Program = Context.createProgram();
    Context.attachShader(Program, VertexStage);
    Context.attachShader(Program, FragmentStage);
    Context.linkProgram(Program);
    Context.deleteShader(VertexStage);
    Context.deleteShader(FragmentStage);

    if (!Context.getProgramParameter(Program, Context.LINK_STATUS))
    {
        Diagnostics.push(`${Label} link failure: ${Context.getProgramInfoLog(Program)}`);
        Context.deleteProgram(Program);
        return null;
    }
    return Program;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    VIEWPORT RASTERIZATION
//------------------------------------------------------------------------------------------------------------------------

export class ViewportRasterization
{
    constructor(Canvas, SceneEntries, Notifications = {})
    {
        this.Canvas = Canvas;
        this.SceneEntries = SceneEntries;
        this.Notifications = Notifications;
        this.Diagnostics = [];

        this.Context = Canvas.getContext("webgl2", {
            antialias: true,
            alpha: true,
            premultipliedAlpha: false,
            powerPreference: "high-performance"
        });

        this.Camera = {
            Yaw: 0.68,
            Pitch: 0.24,
            Distance: 9.8,
            Target: [ 0.0, 0.55, 0.0 ],
            FieldOfView: 34.0
        };
        this.SmoothedCamera = {
            Yaw: this.Camera.Yaw,
            Pitch: this.Camera.Pitch,
            Distance: this.Camera.Distance,
            Target: [ ...this.Camera.Target ]
        };

        this.SelectedToken = null;
        this.HoveredToken = null;
        this.GridEnabled = true;
        this.SelectionAxesEnabled = true;
        this.ProjectionMode = "perspective";
        this.SweepPlaying = true;
        this.SweepRate = 1.0;
        this.SweepPhase = 0.0;
        this.ElapsedSeconds = 0.0;

        this.GeometryAtlas = new Map();
        this.SurfaceProgram = null;
        this.GridProgram = null;
        this.AxisProgram = null;
        this.GridSurface = null;
        this.AxisSurface = null;
        this.ProgramUniforms = new WeakMap();

        this.SelectionRect = null;
        this.FrameRateEstimate = 0.0;

        this.PreviousTimestamp = 0.0;
        this.LastFrameRateSampleTime = 0.0;
        this.FrameCounter = 0;

        this.ViewProjectionMatrix = MatrixIdentity();
        this.EyePosition = [ 0.0, 0.0, 6.0 ];

        this.RenderFrame = this.RenderFrame.bind(this);

        if (!this.Context)
        {
            this.Diagnostics.push("WebGL2 context unavailable in this browser.");
            if (this.Notifications.OnDiagnostics) this.Notifications.OnDiagnostics(this.Diagnostics);
            return;
        }

        this.InitializeResources();
        this.ResizeTarget();

        if (this.Notifications.OnDiagnostics) this.Notifications.OnDiagnostics(this.Diagnostics);
        this.AnimationHandle = requestAnimationFrame(this.RenderFrame);
    }

    ConcludeRasterization()
    {
        if (this.AnimationHandle)
        {
            cancelAnimationFrame(this.AnimationHandle);
            this.AnimationHandle = 0;
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                    RESOURCE INITIALIZATION
    //--------------------------------------------------------------------------------------------------------------------

    InitializeResources()
    {
        const Context = this.Context;

        this.SurfaceProgram = LinkProgram(Context, SurfaceVertexSource, SurfaceFragmentSource, this.Diagnostics, "Surface");
        this.GridProgram = LinkProgram(Context, GridVertexSource, GridFragmentSource, this.Diagnostics, "GroundGrid");
        this.AxisProgram = LinkProgram(Context, AxisVertexSource, AxisFragmentSource, this.Diagnostics, "AxisLines");

        const GroundExtent = 26.0;
        const GroundLevel = -1.62;
        const GroundPlane = new Float32Array([
            -GroundExtent, GroundLevel, -GroundExtent,
             GroundExtent, GroundLevel, -GroundExtent,
             GroundExtent, GroundLevel,  GroundExtent,
            -GroundExtent, GroundLevel, -GroundExtent,
             GroundExtent, GroundLevel,  GroundExtent,
            -GroundExtent, GroundLevel,  GroundExtent
        ]);
        this.GridSurface = this.UploadAttributeSurface(GroundPlane, 3, [ 0 ]);

        const AxisLines = new Float32Array([
            0.0, 0.0, 0.0,  0.55, 0.98, 0.74,
            1.0, 0.0, 0.0,  0.55, 0.98, 0.74,
            0.0, 0.0, 0.0,  0.98, 0.66, 0.42,
            0.0, 1.0, 0.0,  0.98, 0.66, 0.42,
            0.0, 0.0, 0.0,  0.52, 0.72, 1.00,
            0.0, 0.0, 1.0,  0.52, 0.72, 1.00
        ]);
        this.AxisSurface = this.UploadAttributeSurface(AxisLines, 6, [ 0, 1 ], 6);

        Context.enable(Context.DEPTH_TEST);
        Context.depthFunc(Context.LEQUAL);
        Context.disable(Context.CULL_FACE);
        Context.clearColor(0.0, 0.0, 0.0, 0.0);
    }

    UploadAttributeSurface(InterleavedData, Stride, Layout, VertexTotal = 0)
    {
        const Context = this.Context;
        const VertexArray = Context.createVertexArray();
        Context.bindVertexArray(VertexArray);

        const VertexStore = Context.createBuffer();
        Context.bindBuffer(Context.ARRAY_BUFFER, VertexStore);
        Context.bufferData(Context.ARRAY_BUFFER, InterleavedData, Context.STATIC_DRAW);

        for (const Attribute of Layout)
        {
            Context.enableVertexAttribArray(Attribute);
            Context.vertexAttribPointer(Attribute, 3, Context.FLOAT, false, Stride * 4, Attribute * 3 * 4);
        }

        Context.bindVertexArray(null);
        return {
            VertexArray,
            VertexStore,
            VertexTotal: VertexTotal || InterleavedData.length / Stride
        };
    }

    RetrieveGeometry(PrimitiveIdentity)
    {
        if (this.GeometryAtlas.has(PrimitiveIdentity))
        {
            return this.GeometryAtlas.get(PrimitiveIdentity);
        }

        const Context = this.Context;
        const Surface = ConstructPrimitiveSurface(PrimitiveIdentity);
        const VertexArray = Context.createVertexArray();
        Context.bindVertexArray(VertexArray);

        const PositionStore = Context.createBuffer();
        Context.bindBuffer(Context.ARRAY_BUFFER, PositionStore);
        Context.bufferData(Context.ARRAY_BUFFER, Surface.Positions, Context.STATIC_DRAW);
        Context.enableVertexAttribArray(0);
        Context.vertexAttribPointer(0, 3, Context.FLOAT, false, 0, 0);

        const NormalStore = Context.createBuffer();
        Context.bindBuffer(Context.ARRAY_BUFFER, NormalStore);
        Context.bufferData(Context.ARRAY_BUFFER, Surface.Normals, Context.STATIC_DRAW);
        Context.enableVertexAttribArray(1);
        Context.vertexAttribPointer(1, 3, Context.FLOAT, false, 0, 0);

        const BarycentricStore = Context.createBuffer();
        Context.bindBuffer(Context.ARRAY_BUFFER, BarycentricStore);
        Context.bufferData(Context.ARRAY_BUFFER, Surface.Barycentrics, Context.STATIC_DRAW);
        Context.enableVertexAttribArray(2);
        Context.vertexAttribPointer(2, 3, Context.FLOAT, false, 0, 0);

        Context.bindVertexArray(null);

        const Record = {
            VertexArray,
            PositionStore,
            NormalStore,
            BarycentricStore,
            VertexTotal: Surface.VertexCount,
            TriangleTotal: Surface.TriangleCount,
            MinimumBoundary: Surface.MinimumBoundary,
            MaximumBoundary: Surface.MaximumBoundary,
            CorrectedWindings: Surface.CorrectedWindings
        };
        this.GeometryAtlas.set(PrimitiveIdentity, Record);
        return Record;
    }

    RetrieveUniforms(Program)
    {
        if (this.ProgramUniforms.has(Program))
        {
            return this.ProgramUniforms.get(Program);
        }

        const Context = this.Context;
        const UniformTable = {};
        const UniformTotal = Context.getProgramParameter(Program, Context.ACTIVE_UNIFORMS);
        for (let Index = 0; Index < UniformTotal; Index += 1)
        {
            const Description = Context.getActiveUniform(Program, Index);
            UniformTable[Description.name] = Context.getUniformLocation(Program, Description.name);
        }
        this.ProgramUniforms.set(Program, UniformTable);
        return UniformTable;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                    VIEWPORT RESIZING
    //--------------------------------------------------------------------------------------------------------------------

    ResizeTarget()
    {
        if (!this.Context)
        {
            return;
        }
        const Bounds = this.Canvas.getBoundingClientRect();
        const PixelRatio = Math.min(window.devicePixelRatio || 1.0, 2.0);
        const Width = Math.max(1, Math.round(Bounds.width * PixelRatio));
        const Height = Math.max(1, Math.round(Bounds.height * PixelRatio));

        if (this.Canvas.width !== Width || this.Canvas.height !== Height)
        {
            this.Canvas.width = Width;
            this.Canvas.height = Height;
        }
        this.Context.viewport(0, 0, Width, Height);
        this.PixelRatio = PixelRatio;
        this.ViewportWidth = Bounds.width;
        this.ViewportHeight = Bounds.height;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                    CAMERA CONTROL
    //--------------------------------------------------------------------------------------------------------------------

    OrbitCamera(LateralDelta, ElevationDelta)
    {
        this.Camera.Yaw -= LateralDelta * 0.0062;
        this.Camera.Pitch = Math.max(-1.35, Math.min(1.35, this.Camera.Pitch + ElevationDelta * 0.0052));
    }

    DollyCamera(WheelDelta)
    {
        const Factor = Math.exp(WheelDelta * 0.0012);
        this.Camera.Distance = Math.max(1.9, Math.min(24.0, this.Camera.Distance * Factor));
    }

    PanCamera(LateralDelta, ElevationDelta)
    {
        const Forward = this.RetrieveCameraForward();
        const Right = VecNormalize(VecCross(Forward, [ 0.0, 1.0, 0.0 ]));
        const Up = VecCross(Right, Forward);

        const Scalar = this.Camera.Distance * 0.0016;
        const Offset = VecAdd(VecScale(Right, -LateralDelta * Scalar), VecScale(Up, ElevationDelta * Scalar));
        this.Camera.Target = VecAdd(this.Camera.Target, Offset);
    }

    RetrieveCameraForward()
    {
        const Forward = VecSubtract(this.Camera.Target, this.RetrieveCameraEye());
        return VecNormalize(Forward);
    }

    RetrieveCameraEye()
    {
        const CosinePitch = Math.cos(this.SmoothedCamera.Pitch);
        return [
            this.SmoothedCamera.Target[0] + this.SmoothedCamera.Distance * CosinePitch * Math.sin(this.SmoothedCamera.Yaw),
            this.SmoothedCamera.Target[1] + this.SmoothedCamera.Distance * Math.sin(this.SmoothedCamera.Pitch),
            this.SmoothedCamera.Target[2] + this.SmoothedCamera.Distance * CosinePitch * Math.cos(this.SmoothedCamera.Yaw)
        ];
    }

    FrameSelection()
    {
        if (!this.Context)
        {
            return;
        }

        const Entry = this.RetrieveSelectedEntry();
        if (!Entry || Entry.Category !== "geometry")
        {
            this.Camera.Target = [ 0.0, 0.35, 0.0 ];
            this.Camera.Distance = 7.1;
            return;
        }

        const Geometry = this.RetrieveGeometry(Entry.Primitive);
        const Placement = MatrixModelPlacement(Entry.Placement.Position, Entry.Placement.Rotation, Entry.Placement.Scaling);

        const MinimumBoundary = [ Infinity, Infinity, Infinity ];
        const MaximumBoundary = [ -Infinity, -Infinity, -Infinity ];
        for (let Corner = 0; Corner < 8; Corner += 1)
        {
            const LocalCorner = [
                (Corner & 1) ? Geometry.MaximumBoundary[0] : Geometry.MinimumBoundary[0],
                (Corner & 2) ? Geometry.MaximumBoundary[1] : Geometry.MinimumBoundary[1],
                (Corner & 4) ? Geometry.MaximumBoundary[2] : Geometry.MinimumBoundary[2]
            ];
            const WorldCorner = ProjectPoint(Placement, LocalCorner);
            for (let Axis = 0; Axis < 3; Axis += 1)
            {
                MinimumBoundary[Axis] = Math.min(MinimumBoundary[Axis], WorldCorner[Axis]);
                MaximumBoundary[Axis] = Math.max(MaximumBoundary[Axis], WorldCorner[Axis]);
            }
        }

        const Center = [ 0, 1, 2 ].map((Axis) => (MinimumBoundary[Axis] + MaximumBoundary[Axis]) * 0.5);
        const Extent = Math.max(
            MaximumBoundary[0] - MinimumBoundary[0],
            MaximumBoundary[1] - MinimumBoundary[1],
            MaximumBoundary[2] - MinimumBoundary[2]
        );

        // 📝 Fitted against the reference framing: the record occupies roughly 85% of the stage height.
        const FramingScale = 2.72;
        const FieldCompensation = Math.tan(34.0 * Math.PI / 360.0) / Math.tan(this.Camera.FieldOfView * Math.PI / 360.0);

        this.Camera.Target = Center;
        this.Camera.Distance = Math.max(2.4, Extent * FramingScale * FieldCompensation);
    }

    ResetView()
    {
        this.Camera.Yaw = 0.68;
        this.Camera.Pitch = 0.24;
        this.Camera.Target = [ 0.0, 0.55, 0.0 ];
        this.Camera.Distance = 9.8;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                    ENTRY QUERIES
    //--------------------------------------------------------------------------------------------------------------------

    RetrieveRecordStatistics(PrimitiveIdentity)
    {
        if (!this.Context)
        {
            return { VertexTotal: 0, TriangleTotal: 0, CorrectedWindings: 0 };
        }
        const Geometry = this.RetrieveGeometry(PrimitiveIdentity);
        return { VertexTotal: Geometry.VertexTotal, TriangleTotal: Geometry.TriangleTotal, CorrectedWindings: Geometry.CorrectedWindings };
    }

    RetrieveSceneStatistics()
    {
        let VertexTotal = 0;
        let SurfaceTotal = 0;
        if (!this.Context)
        {
            return { VertexTotal, SurfaceTotal };
        }

        for (const Entry of this.SceneEntries)
        {
            if (Entry.Category !== "geometry" || !Entry.VisibilityEnabled)
            {
                continue;
            }
            VertexTotal += this.RetrieveGeometry(Entry.Primitive).VertexTotal;
            SurfaceTotal += 1;
        }
        return { VertexTotal, SurfaceTotal };
    }

    RetrieveCameraBasis()
    {
        const Forward = this.RetrieveCameraForward();
        const Right = VecNormalize(VecCross(Forward, [ 0.0, 1.0, 0.0 ]));
        const Up = VecCross(Right, Forward);
        return { Forward, Right, Up };
    }

    RetrieveSelectedEntry()
    {
        if (this.SelectedToken === null)
        {
            return null;
        }
        return this.SceneEntries.find((Entry) => Entry.Token === this.SelectedToken) || null;
    }

    RetrieveWorldBounds(Entry)
    {
        const Geometry = this.RetrieveGeometry(Entry.Primitive);
        const Placement = MatrixModelPlacement(Entry.Placement.Position, Entry.Placement.Rotation, Entry.Placement.Scaling);
        const MinimumBoundary = [ Infinity, Infinity, Infinity ];
        const MaximumBoundary = [ -Infinity, -Infinity, -Infinity ];

        for (let Corner = 0; Corner < 8; Corner += 1)
        {
            const LocalCorner = [
                (Corner & 1) ? Geometry.MaximumBoundary[0] : Geometry.MinimumBoundary[0],
                (Corner & 2) ? Geometry.MaximumBoundary[1] : Geometry.MinimumBoundary[1],
                (Corner & 4) ? Geometry.MaximumBoundary[2] : Geometry.MinimumBoundary[2]
            ];
            const WorldCorner = ProjectPoint(Placement, LocalCorner);
            for (let Axis = 0; Axis < 3; Axis += 1)
            {
                MinimumBoundary[Axis] = Math.min(MinimumBoundary[Axis], WorldCorner[Axis]);
                MaximumBoundary[Axis] = Math.max(MaximumBoundary[Axis], WorldCorner[Axis]);
            }
        }
        return { MinimumBoundary, MaximumBoundary, Placement };
    }

    SelectEntryAtScreen(ScreenX, ScreenY)
    {
        if (!this.Context)
        {
            return null;
        }

        const Ray = RayFromScreen(this.ViewProjectionMatrix, ScreenX, ScreenY, this.ViewportWidth, this.ViewportHeight);
        let NearestEntry = null;
        let NearestDistance = Infinity;

        for (const Entry of this.SceneEntries)
        {
            if (Entry.Category !== "geometry" || !Entry.VisibilityEnabled)
            {
                continue;
            }

            const Bounds = this.RetrieveWorldBounds(Entry);
            const Center = [ 0, 1, 2 ].map((Axis) => (Bounds.MinimumBoundary[Axis] + Bounds.MaximumBoundary[Axis]) * 0.5);
            const Radius = Math.max(0.35, Math.hypot(
                Bounds.MaximumBoundary[0] - Bounds.MinimumBoundary[0],
                Bounds.MaximumBoundary[1] - Bounds.MinimumBoundary[1],
                Bounds.MaximumBoundary[2] - Bounds.MinimumBoundary[2]
            ) * 0.5);

            const ToCenter = VecSubtract(Center, Ray.Origin);
            const AlongRay = VecDot(ToCenter, Ray.Direction);
            const PerpendicularSquared = VecDot(ToCenter, ToCenter) - AlongRay * AlongRay;
            if (AlongRay <= 0.0 || PerpendicularSquared > Radius * Radius)
            {
                continue;
            }
            const EntryDistance = AlongRay - Math.sqrt(Math.max(0.0, Radius * Radius - PerpendicularSquared));
            if (EntryDistance < NearestDistance)
            {
                NearestDistance = EntryDistance;
                NearestEntry = Entry;
            }
        }
        return NearestEntry;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                    FRAME EVALUATION
    //--------------------------------------------------------------------------------------------------------------------

    EvaluateSelectionRect()
    {
        const Entry = this.RetrieveSelectedEntry();
        if (!Entry || Entry.Category !== "geometry" || !Entry.VisibilityEnabled || !this.ViewportWidth)
        {
            this.SelectionRect = null;
            return;
        }

        const Bounds = this.RetrieveWorldBounds(Entry);
        let MinimumScreenX = Infinity, MinimumScreenY = Infinity;
        let MaximumScreenX = -Infinity, MaximumScreenY = -Infinity;

        for (let Corner = 0; Corner < 8; Corner += 1)
        {
            const WorldCorner = [
                (Corner & 1) ? Bounds.MaximumBoundary[0] : Bounds.MinimumBoundary[0],
                (Corner & 2) ? Bounds.MaximumBoundary[1] : Bounds.MinimumBoundary[1],
                (Corner & 4) ? Bounds.MaximumBoundary[2] : Bounds.MinimumBoundary[2]
            ];
            const Projected = ProjectPoint(this.ViewProjectionMatrix, WorldCorner);
            if (Projected[3] <= 0.0)
            {
                this.SelectionRect = null;
                return;
            }
            const ScreenX = (Projected[0] * 0.5 + 0.5) * this.ViewportWidth;
            const ScreenY = (-Projected[1] * 0.5 + 0.5) * this.ViewportHeight;
            MinimumScreenX = Math.min(MinimumScreenX, ScreenX);
            MinimumScreenY = Math.min(MinimumScreenY, ScreenY);
            MaximumScreenX = Math.max(MaximumScreenX, ScreenX);
            MaximumScreenY = Math.max(MaximumScreenY, ScreenY);
        }

        this.SelectionRect = {
            Left: MinimumScreenX,
            Top: MinimumScreenY,
            Width: MaximumScreenX - MinimumScreenX,
            Height: MaximumScreenY - MinimumScreenY,
            AnchorX: MaximumScreenX,
            AnchorY: MinimumScreenY
        };
    }

    RenderFrame(Timestamp)
    {
        if (!this.Context)
        {
            return;
        }

        const Elapsed = this.PreviousTimestamp ? Math.min(0.05, (Timestamp - this.PreviousTimestamp) / 1000.0) : 0.016;
        this.PreviousTimestamp = Timestamp;
        this.ElapsedSeconds += Elapsed;

        if (this.SweepPlaying)
        {
            this.SweepPhase = (this.SweepPhase + Elapsed * 0.32 * this.SweepRate) % 1.0;
        }

        const BlendFactor = 1.0 - Math.exp(-Elapsed * 11.0);
        this.SmoothedCamera.Yaw += (this.Camera.Yaw - this.SmoothedCamera.Yaw) * BlendFactor;
        this.SmoothedCamera.Pitch += (this.Camera.Pitch - this.SmoothedCamera.Pitch) * BlendFactor;
        this.SmoothedCamera.Distance += (this.Camera.Distance - this.SmoothedCamera.Distance) * BlendFactor;
        for (let Axis = 0; Axis < 3; Axis += 1)
        {
            this.SmoothedCamera.Target[Axis] += (this.Camera.Target[Axis] - this.SmoothedCamera.Target[Axis]) * BlendFactor;
        }

        this.ResizeTarget();

        const Context = this.Context;
        const EyePosition = this.RetrieveCameraEye();
        const ViewMatrix = MatrixViewOrientation(EyePosition, this.SmoothedCamera.Target, [ 0.0, 1.0, 0.0 ]);
        const AspectRatio = Math.max(0.2, this.ViewportWidth / Math.max(1.0, this.ViewportHeight));
        const HalfField = this.Camera.FieldOfView * Math.PI / 360.0;
        const ProjectionMatrix = this.ProjectionMode === "orthographic"
            ? MatrixOrthographic(
                this.SmoothedCamera.Distance * Math.tan(HalfField) * AspectRatio,
                this.SmoothedCamera.Distance * Math.tan(HalfField),
                0.08,
                180.0
            )
            : MatrixPerspective(this.Camera.FieldOfView * Math.PI / 180.0, AspectRatio, 0.08, 90.0);
        this.ViewProjectionMatrix = MatrixMultiply(ProjectionMatrix, ViewMatrix);
        this.EyePosition = EyePosition;

        Context.clear(Context.COLOR_BUFFER_BIT | Context.DEPTH_BUFFER_BIT);

        this.RasterizeSurfaces();
        this.RasterizeGroundGrid();
        this.RasterizeSelectionAxes();
        this.EvaluateSelectionRect();

        this.FrameCounter += 1;
        const SampleWindow = Timestamp - this.LastFrameRateSampleTime;
        if (SampleWindow > 420.0)
        {
            this.FrameRateEstimate = (this.FrameCounter * 1000.0) / SampleWindow;
            this.FrameCounter = 0;
            this.LastFrameRateSampleTime = Timestamp;
            if (this.Notifications.OnFrameRate) this.Notifications.OnFrameRate(this.FrameRateEstimate);
        }

        if (this.Notifications.OnSelectionRect) this.Notifications.OnSelectionRect(this.SelectionRect);

        this.AnimationHandle = requestAnimationFrame(this.RenderFrame);
    }

    RasterizeSurfaces()
    {
        const Context = this.Context;
        if (!this.SurfaceProgram)
        {
            return;
        }

        Context.useProgram(this.SurfaceProgram);
        const Uniforms = this.RetrieveUniforms(this.SurfaceProgram);
        Context.uniformMatrix4fv(Uniforms.ViewProjectionMatrix, false, this.ViewProjectionMatrix);
        Context.uniform3fv(Uniforms.CameraPosition, this.EyePosition);
        Context.uniform1f(Uniforms.ElapsedSeconds, this.ElapsedSeconds);

        for (const Entry of this.SceneEntries)
        {
            if (Entry.Category !== "geometry" || !Entry.VisibilityEnabled)
            {
                continue;
            }

            const Geometry = this.RetrieveGeometry(Entry.Primitive);
            const Placement = MatrixModelPlacement(Entry.Placement.Position, Entry.Placement.Rotation, Entry.Placement.Scaling);
            const NormalRecovery = MatrixNormalRecovery(Placement);
            const SurfaceBounds = this.RetrieveWorldBounds(Entry);

            let SweepHeight = SurfaceBounds.MinimumBoundary[1];
            const SweepSpan = Math.max(0.001, SurfaceBounds.MaximumBoundary[1] - SurfaceBounds.MinimumBoundary[1]);
            const SweepExtent = (0.35 + this.SweepPhase * 0.9) * SweepSpan;
            SweepHeight = SurfaceBounds.MinimumBoundary[1] + SweepExtent;

            Context.uniformMatrix4fv(Uniforms.ModelMatrix, false, Placement);
            Context.uniformMatrix3fv(Uniforms.NormalMatrix, false, NormalRecovery);
            Context.uniform3fv(Uniforms.BaseColor, Entry.Surface.BaseColor);
            Context.uniform3fv(Uniforms.RimColor, Entry.Surface.RimColor);
            Context.uniform1f(Uniforms.RimStrength, Entry.Surface.RimStrength);
            Context.uniform1f(Uniforms.WireStrength, Entry.Surface.WireStrength);
            Context.uniform1f(Uniforms.SweepStrength, Entry.Surface.SweepStrength);
            Context.uniform1f(Uniforms.SweepHeight, SweepHeight);
            Context.uniform1f(Uniforms.HighlightStrength, Entry.Token === this.SelectedToken ? 0.26 : 0.0);

            Context.bindVertexArray(Geometry.VertexArray);
            Context.drawArrays(Context.TRIANGLES, 0, Geometry.VertexTotal);
        }

        Context.bindVertexArray(null);
    }

    RasterizeGroundGrid()
    {
        if (!this.GridEnabled || !this.GridProgram || !this.GridSurface)
        {
            return;
        }

        const Context = this.Context;
        Context.enable(Context.BLEND);
        Context.blendFunc(Context.SRC_ALPHA, Context.ONE);
        Context.depthMask(false);

        Context.useProgram(this.GridProgram);
        const Uniforms = this.RetrieveUniforms(this.GridProgram);
        Context.uniformMatrix4fv(Uniforms.ViewProjectionMatrix, false, this.ViewProjectionMatrix);
        Context.uniform1f(Uniforms.GridIntensity, 0.9);
        Context.uniform3f(Uniforms.GridColor, 0.58, 0.82, 0.68);
        Context.uniform3f(Uniforms.AccentColor, 0.55, 0.98, 0.74);

        Context.bindVertexArray(this.GridSurface.VertexArray);
        Context.drawArrays(Context.TRIANGLES, 0, this.GridSurface.VertexTotal);
        Context.bindVertexArray(null);

        Context.disable(Context.BLEND);
        Context.depthMask(true);
    }

    RasterizeSelectionAxes()
    {
        const Entry = this.RetrieveSelectedEntry();
        if (!this.SelectionAxesEnabled || !Entry || Entry.Category !== "geometry" || !this.AxisProgram || !this.AxisSurface)
        {
            return;
        }

        const Context = this.Context;
        const AxisLength = Math.max(0.85, this.SmoothedCamera.Distance * 0.16);
        const Origin = Entry.Placement.Position;
        const Placement = new Float32Array([
            AxisLength, 0.0, 0.0, 0.0,
            0.0, AxisLength, 0.0, 0.0,
            0.0, 0.0, AxisLength, 0.0,
            Origin[0], Origin[1], Origin[2], 1.0
        ]);

        Context.enable(Context.BLEND);
        Context.blendFunc(Context.SRC_ALPHA, Context.ONE);
        Context.depthMask(false);
        Context.disable(Context.DEPTH_TEST);

        Context.useProgram(this.AxisProgram);
        const Uniforms = this.RetrieveUniforms(this.AxisProgram);
        Context.uniformMatrix4fv(Uniforms.ViewProjectionMatrix, false, this.ViewProjectionMatrix);
        Context.uniformMatrix4fv(Uniforms.ModelMatrix, false, Placement);
        Context.bindVertexArray(this.AxisSurface.VertexArray);
        Context.drawArrays(Context.LINES, 0, this.AxisSurface.VertexTotal);
        Context.bindVertexArray(null);

        Context.enable(Context.DEPTH_TEST);
        Context.depthMask(true);
        Context.disable(Context.BLEND);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                    ENTRY SELECTION
    //--------------------------------------------------------------------------------------------------------------------

    ConfigureSelection(Token)
    {
        this.SelectedToken = Token;
    }

    ConfigureGridVisibility(VisibilityEnabled)
    {
        this.GridEnabled = VisibilityEnabled;
    }

    ConfigureSweepPlaying(Playing)
    {
        this.SweepPlaying = Playing;
    }

    ConfigureSweepRate(Rate)
    {
        this.SweepRate = Math.max(0.1, Rate);
    }

    ConfigureSelectionAxes(Enabled)
    {
        this.SelectionAxesEnabled = Enabled;
    }

    ConfigureProjectionMode(ModeIdentity)
    {
        this.ProjectionMode = ModeIdentity === "orthographic" ? "orthographic" : "perspective";
    }
}
