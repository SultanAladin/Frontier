/*==========================================================================================================================================
                                                        VOLUMERASTERIZATION.JS
==========================================================================================================================================*/
// 🧩 GPU triangle/depth rasterization of bounded XYZ surfaces; procedural terrain never executes in a camera shader.

import { ProjectVolumeCamera } from './VolumeRayField.js';
import { OceanVertexShader, PrepareOceanSpectrum, OceanPhaseParameters, OceanDetailShader } from './OceanSurface.js';
import { PreviewExtent, ConstructPreviewDensity } from './PreviewTiling.js';
import { MaximumSurfaceTriangles } from './VolumeSurface.js';

export const MaximumGpuFenceMilliseconds = 4000;
export const MaximumGpuBatchVertices = 4095;
export const OceanGridResolution = 64;
export const OceanWorldExtent = 8000;

//------------------------------------------------------------------------------------------------------------------------
//                                                        SMALL GRAPHICS PROGRAMS
//------------------------------------------------------------------------------------------------------------------------

const ProjectionSource = `
uniform vec3 Eye, Forward, Right, Up, Origin;
uniform vec2 Pixels, Clip;
vec4 Project(vec3 Position)
{
    vec3 Relative = Position - Eye;
    float Depth = dot(Relative, Forward);
    return vec4(dot(Relative, Right) * 1.65 * Pixels.y / Pixels.x, dot(Relative, Up) * 1.65,
        (Clip.y + Clip.x) / (Clip.x - Clip.y) * Depth + 2. * Clip.x * Clip.y / (Clip.y - Clip.x), Depth);
}
`;

const SkySource = `
vec3 Sky(vec3 Ray)
{
    float Elevation = max(Ray.y, 0.);
    return mix(vec3(.60, .66, .69), vec3(.18, .27, .34), pow(Elevation, .45));
}
`;

const DistantOceanSource = `
vec3 DistantOcean(vec3 Ray)
{
    float Grazing = 1. - max(-Ray.y, 0.);
    float Fresnel = .025 + .975 * Grazing * Grazing * Grazing * Grazing * Grazing;
    vec3 Colour = mix(vec3(.055, .14, .17), Sky(vec3(Ray.x, -Ray.y, Ray.z)), Fresnel * .72);
    return mix(Colour, Sky(Ray), .3);
}
`;

export const SurfaceVertexSource = `#version 300 es
precision highp float;
layout(location = 0) in vec3 Position;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec3 Colour;
out vec3 SurfacePosition, SurfaceNormal, SurfaceColour;
${ProjectionSource}
void main()
{
    SurfacePosition = Position;
    SurfaceNormal = Normal;
    SurfaceColour = Colour;
    gl_Position = Project(Position);
}
`;

export const SurfaceFragmentSource = `#version 300 es
precision highp float;
in vec3 SurfacePosition, SurfaceNormal, SurfaceColour;
uniform vec3 Eye, DetailMinimum, DetailMaximum;
uniform bool ExcludeDetail;
out vec4 Pixel;
${SkySource}
void main()
{
    if (ExcludeDetail && all(greaterThan(SurfacePosition, DetailMinimum)) && all(lessThan(SurfacePosition, DetailMaximum))) discard;
    vec3 Normal = normalize(SurfaceNormal);
    vec3 Ray = normalize(SurfacePosition - Eye);
    float Diffuse = max(dot(Normal, normalize(vec3(-.45, .78, .5))), 0.);
    vec3 Sand = vec3(.59, .53, .40);
    vec3 Colour = mix(Sand, SurfaceColour * .82, smoothstep(1., 24., SurfacePosition.y));
    Colour *= .36 + .64 * Diffuse;
    Colour *= .91 + .09 * max(Normal.y, 0.);
    Colour = mix(Colour, Sky(Ray), 1. - exp(-length(SurfacePosition - Eye) * .000045));
    Pixel = vec4(pow(max(Colour, 0.), vec3(.88)), 1.);
}
`;

const SkyVertexSource = `#version 300 es
precision highp float;
layout(location = 0) in vec2 Position;
out vec2 Screen;
void main() { Screen = Position; gl_Position = vec4(Position, 1., 1.); }
`;

const SkyFragmentSource = `#version 300 es
precision highp float;
in vec2 Screen;
uniform vec3 Forward, Right, Up;
uniform vec2 Pixels;
uniform bool Ocean;
out vec4 Pixel;
${SkySource}
${DistantOceanSource}
void main()
{
    vec3 Ray = normalize(Forward * 1.65 + Right * Screen.x * Pixels.x / Pixels.y + Up * Screen.y);
    vec3 Colour = Ocean && Ray.y < 0. ? DistantOcean(Ray) : Sky(Ray);
    Pixel = vec4(pow(Colour, vec3(.88)), 1.);
}
`;

const ShoreSource = `
uniform highp sampler2D ShoreTexture;
uniform vec2 ShoreMinimum, ShoreSpan;
float ShoreDistance(vec2 Position)
{
    vec2 UV = (Position - ShoreMinimum) / ShoreSpan;
    if (any(lessThan(UV, vec2(0.))) || any(greaterThan(UV, vec2(1.)))) return 10000.;
    ivec2 Extent = textureSize(ShoreTexture, 0);
    vec2 Coordinate = clamp(UV * vec2(Extent) - .5, vec2(0.), vec2(Extent - 1));
    ivec2 Lower = ivec2(floor(Coordinate)), Upper = min(Lower + 1, Extent - 1);
    vec2 Fraction = fract(Coordinate);
    return mix(mix(texelFetch(ShoreTexture, Lower, 0).r, texelFetch(ShoreTexture, ivec2(Upper.x, Lower.y), 0).r, Fraction.x),
        mix(texelFetch(ShoreTexture, ivec2(Lower.x, Upper.y), 0).r, texelFetch(ShoreTexture, Upper, 0).r, Fraction.x), Fraction.y);
}
`;

export const OceanVertexSource = `#version 300 es
precision highp float;
layout(location = 0) in vec2 Position;
uniform vec2 OceanCentre;
uniform float OceanExtent, OceanTime, Wave;
out vec3 WaterPosition;
out vec2 WaterSlope;
${ProjectionSource}
${ShoreSource}
${OceanVertexShader}
${OceanDetailShader}
void main()
{
    vec2 Horizontal = OceanCentre + Position * Position * Position * OceanExtent;
    vec2 World = Horizontal + Origin.xz;
    float Shore = ShoreDistance(World);
    float Damping = smoothstep(-6., 28., Shore);
    vec3 WaveEvaluation = OceanVertexEvaluation(World, OceanTime);
    WaterPosition = vec3(Horizontal.x, -.09 + WaveEvaluation.x * Damping, Horizontal.y);
    WaterSlope = (WaveEvaluation.yz - OceanDetailSlope(World, OceanTime, Wave, 0.)) * Damping;
    gl_Position = Project(WaterPosition);
}
`;

export const OceanFragmentSource = `#version 300 es
precision highp float;
in vec3 WaterPosition;
in vec2 WaterSlope;
uniform vec3 Eye, Origin;
uniform float OceanTime, Wave;
out vec4 Pixel;
${ShoreSource}
${SkySource}
${DistantOceanSource}
${OceanDetailShader}
void main()
{
    vec2 World = WaterPosition.xz + Origin.xz;
    float Footprint = max(length(dFdx(World)), length(dFdy(World)));
    float Shore = ShoreDistance(World);
    vec2 Slope = WaterSlope + OceanDetailSlope(World, OceanTime, Wave, Footprint) * smoothstep(-6., 28., Shore);
    vec3 Ray = normalize(WaterPosition - Eye);
    vec3 Normal = normalize(vec3(-Slope.x, 1., -Slope.y));
    if (dot(Normal, -Ray) < 0.) Normal = -Normal;
    float Fresnel = .025 + .975 * pow(1. - max(dot(-Ray, Normal), 0.), 5.);
    vec3 Colour = mix(vec3(.055, .14, .17), vec3(.12, .29, .29), exp(-max(Shore, 0.) * .035));
    Colour = mix(Colour, Sky(reflect(Ray, Normal)), Fresnel * .72);
    vec3 Sun = normalize(vec3(-.45, .78, .5));
    Colour += vec3(.60, .56, .43) * pow(max(dot(reflect(-Sun, Normal), -Ray), 0.), 128.) * .5;
    float Breakup = .5 + .5 * sin(WaterPosition.x * .21 + sin(WaterPosition.z * .17) + OceanTime);
    float Foam = (1. - smoothstep(.5, 5., abs(Shore))) * smoothstep(.3, .8, Breakup);
    Colour = mix(Colour, vec3(.68, .78, .75), Foam * .38);
    Colour = mix(Colour, Sky(Ray), 1. - exp(-length(WaterPosition - Eye) * .000045));
    Colour = mix(Colour, DistantOcean(Ray), smoothstep(3500., 6500., length(WaterPosition - Eye)));
    Pixel = vec4(pow(max(Colour, 0.), vec3(.88)), 1.);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                                    ALLOCATION AND PROGRAM COMPILATION
//------------------------------------------------------------------------------------------------------------------------

function ConstructProgram(GL, VertexSource, FragmentSource)
{
    const Program = GL.createProgram(), Shaders = [];
    for (const [Category, Source] of [[GL.VERTEX_SHADER, VertexSource], [GL.FRAGMENT_SHADER, FragmentSource]])
    {
        const Shader = GL.createShader(Category);
        Shaders.push(Shader);
        GL.shaderSource(Shader, Source);
        GL.compileShader(Shader);
        GL.attachShader(Program, Shader);
    }
    GL.linkProgram(Program);
    return { Program, Shaders, Locations: new Map() };
}

function ValidateProgram(GL, Compiled)
{
    if (!GL.getProgramParameter(Compiled.Program, GL.LINK_STATUS))
        throw Error('GPU surface shader: ' + GL.getProgramInfoLog(Compiled.Program));
    for (const Shader of Compiled.Shaders) GL.deleteShader(Shader);
    Compiled.Shaders = [];
}

function ConstructVertexArray(GL, Vertices, Width)
{
    const VertexArray = GL.createVertexArray(), Buffer = GL.createBuffer();
    GL.bindVertexArray(VertexArray);
    GL.bindBuffer(GL.ARRAY_BUFFER, Buffer);
    GL.bufferData(GL.ARRAY_BUFFER, Vertices, GL.STATIC_DRAW);
    for (let Slot = 0; Slot < (Width === 9 ? 3 : 1); Slot++)
    {
        GL.enableVertexAttribArray(Slot);
        GL.vertexAttribPointer(Slot, Width === 9 ? 3 : 2, GL.FLOAT, false, Width * 4, Slot * 12);
    }
    return { VertexArray, Buffer, Count: Vertices.length / Width };
}

function ConstructOceanGrid(GL)
{
    const Side = OceanGridResolution + 1;
    const Coordinates = new Float32Array(Side * Side * 2);
    const Indices = new Uint16Array(OceanGridResolution * OceanGridResolution * 6);
    for (let Z = 0; Z < Side; Z++)
    for (let X = 0; X < Side; X++)
    {
        Coordinates[(Z * Side + X) * 2] = X * 2 / OceanGridResolution - 1;
        Coordinates[(Z * Side + X) * 2 + 1] = Z * 2 / OceanGridResolution - 1;
    }
    let Offset = 0;
    for (let Z = 0; Z < OceanGridResolution; Z++)
    for (let X = 0; X < OceanGridResolution; X++)
    for (const [DX, DZ] of [[0, 0], [1, 0], [1, 1], [0, 0], [1, 1], [0, 1]])
        Indices[Offset++] = (Z + DZ) * Side + X + DX;
    const Vertices = ConstructVertexArray(GL, Coordinates, 2);
    const IndexBuffer = GL.createBuffer();
    GL.bindVertexArray(Vertices.VertexArray);
    GL.bindBuffer(GL.ELEMENT_ARRAY_BUFFER, IndexBuffer);
    GL.bufferData(GL.ELEMENT_ARRAY_BUFFER, Indices, GL.STATIC_DRAW);
    return { ...Vertices, IndexBuffer, IndexCount: Indices.length };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ONE IN-FLIGHT GPU IMAGE
//------------------------------------------------------------------------------------------------------------------------

export function ConstructVisibleGpuInterval(InspectTime = () => performance.now())
{
    let Started = InspectTime(), PausedAt = null, PausedDuration = 0;
    return {
        Reset() { Started = InspectTime(); PausedAt = null; PausedDuration = 0; },
        Suspend() { if (PausedAt === null) PausedAt = InspectTime(); },
        Resume()
        {
            if (PausedAt === null) return;
            PausedDuration += InspectTime() - PausedAt;
            PausedAt = null;
        },
        Elapsed() { return (PausedAt === null ? InspectTime() : PausedAt) - Started - PausedDuration; }
    };
}

export function CreateVolumeRasterization(Canvas, Options = {})
{
    const Start = performance.now();
    const GL = Canvas.getContext('webgl2', {
        antialias: false, preserveDrawingBuffer: true, depth: false, powerPreference: 'default', alpha: false
    });
    if (!GL || GL.isContextLost()) throw Error('WebGL2 could not initialize. GPU preview stopped; Mask and save remain available.');
    const Parallel = GL.getExtension('KHR_parallel_shader_compile');
    const GpuTimer = GL.getExtension('EXT_disjoint_timer_query_webgl2');
    const ProgramSurface = ConstructProgram(GL, SurfaceVertexSource, SurfaceFragmentSource);
    const ProgramSky = ConstructProgram(GL, SkyVertexSource, SkyFragmentSource);
    const ProgramOcean = ConstructProgram(GL, OceanVertexSource, OceanFragmentSource);
    const Programs = [ProgramSurface, ProgramSky, ProgramOcean];
    const SkyVertices = ConstructVertexArray(GL, new Float32Array([-1, -1, 3, -1, -1, 3]), 2);
    const WaterVertices = ConstructOceanGrid(GL);
    const ImageFramebuffer = GL.createFramebuffer();
    const ImageColour = GL.createRenderbuffer(), ImageDepth = GL.createRenderbuffer();
    let ImageWidth = 0, ImageHeight = 0;
    const ShoreTexture = GL.createTexture();
    GL.bindTexture(GL.TEXTURE_2D, ShoreTexture);
    GL.texParameteri(GL.TEXTURE_2D, GL.TEXTURE_MIN_FILTER, GL.NEAREST);
    GL.texParameteri(GL.TEXTURE_2D, GL.TEXTURE_MAG_FILTER, GL.NEAREST);
    GL.texParameteri(GL.TEXTURE_2D, GL.TEXTURE_WRAP_S, GL.CLAMP_TO_EDGE);
    GL.texParameteri(GL.TEXTURE_2D, GL.TEXTURE_WRAP_T, GL.CLAMP_TO_EDGE);
    GL.texImage2D(GL.TEXTURE_2D, 0, GL.R32F, 1, 1, 0, GL.RED, GL.FLOAT, new Float32Array([10000]));
    const Debug = GL.getExtension('WEBGL_debug_renderer_info');
    Object.assign(Canvas.dataset, {
        renderMode: 'webgl2', renderTechnique: 'xyz-surface', graphicsContexts: '1', contextEpoch: '1',
        renderer: Debug ? GL.getParameter(Debug.UNMASKED_RENDERER_WEBGL) : GL.getParameter(GL.RENDERER),
        pendingGpuImages: '0', maximumPendingGpuImages: '1', shaderTerrainFieldEvaluations: '0', depthFormat: 'reverse-float32',
        terrainRenders: '0', oceanRenders: '0', surfaceBytes: '0'
    });
    let Disposed = false, Suspended = false, Stopped = false, Compiled = false, InspectionPending = false;
    let Revision = 0, Surface = null, PendingSurface = null, Shore = null, PendingShore = null, Batches = [];
    let LatestView = null, PendingFence = null, PendingTiming = null, SubmittedAt = 0, PendingRevision = 0, PendingQuality = 0;
    let PendingDetailKey = '', PendingPreparationEpoch = 0, PendingSampling = null;
    let LastSubmission = 0, LastSignature = '', LastCompletedSignature = '', MaximumFenceMilliseconds = 0;
    let Timer = null;
    const VisibleGpuInterval = ConstructVisibleGpuInterval();
    const PreviewDensity = ConstructPreviewDensity();

    function Status(Category, Message)
    {
        Canvas.dataset.renderStatus = Category;
        Canvas.dataset.renderMessage = Message || '';
        Options.Status?.(Category, Message);
    }

    function Suspend()
    {
        Suspended = true;
        VisibleGpuInterval.Suspend();
        Canvas.dataset.previewSuspended = 'true';
    }

    function Failed(Failure)
    {
        if (Stopped || Disposed) return;
        Stopped = true;
        clearTimeout(Timer);
        Status('failed', Failure.message || String(Failure));
    }

    function Lost(Event)
    {
        Event.preventDefault();
        if (Stopped || Disposed) return;
        Stopped = true;
        clearTimeout(Timer);
        Canvas.dataset.contextLosses = String(Number(Canvas.dataset.contextLosses || 0) + 1);
        Status('lost', 'The GPU context was lost. Rendering is stopped, not switched to software. Mask and project save remain available.');
    }
    Canvas.addEventListener('webglcontextlost', Lost);

    function Location(CompiledProgram, Name)
    {
        if (!CompiledProgram.Locations.has(Name))
            CompiledProgram.Locations.set(Name, GL.getUniformLocation(CompiledProgram.Program, Name));
        return CompiledProgram.Locations.get(Name);
    }

    function ReclaimBatches()
    {
        for (const Batch of Batches) { GL.deleteBuffer(Batch.Buffer); GL.deleteVertexArray(Batch.VertexArray); }
        Batches = [];
    }

    function UploadSurface(Prepared, NextRevision)
    {
        if (Disposed || Stopped || NextRevision < Revision) return;
        if (Prepared.Triangles > MaximumSurfaceTriangles || Prepared.Chunks.some(Chunk => Chunk.length / 9 > MaximumGpuBatchVertices))
            throw Error('GPU surface submission exceeded the geometry budget.');
        Revision = NextRevision;
        Canvas.dataset.volumeRevision = String(Revision);
        PendingSurface = { Prepared, Revision };
    }

    function UploadShore(Prepared, NextRevision)
    {
        if (Disposed || Stopped || NextRevision < Revision) return;
        PendingShore = { Prepared, Revision: NextRevision };
    }

    function ConfigureView(CompiledProgram, View, Origin)
    {
        GL.useProgram(CompiledProgram.Program);
        const Projection = ProjectVolumeCamera(View.Camera);
        for (const Name of ['Forward', 'Right', 'Up']) GL.uniform3fv(Location(CompiledProgram, Name), Projection[Name]);
        GL.uniform3fv(Location(CompiledProgram, 'Eye'), Projection.Eye.map((Coordinate, Axis) => Coordinate - Origin[Axis]));
        GL.uniform3fv(Location(CompiledProgram, 'Origin'), Origin);
        GL.uniform2f(Location(CompiledProgram, 'Pixels'), Canvas.width, Canvas.height);
        GL.uniform2f(Location(CompiledProgram, 'Clip'), Math.max(.1, View.Camera.Orbit * .0001), Math.max(1000, View.Camera.Orbit * 12));
    }

    function ConfigureShore(CompiledProgram)
    {
        GL.activeTexture(GL.TEXTURE0);
        GL.bindTexture(GL.TEXTURE_2D, ShoreTexture);
        GL.uniform1i(Location(CompiledProgram, 'ShoreTexture'), 0);
        GL.uniform2fv(Location(CompiledProgram, 'ShoreMinimum'), Shore?.Minimum || [0, 0]);
        GL.uniform2fv(Location(CompiledProgram, 'ShoreSpan'), Shore?.Span || [1, 1]);
    }

    function InspectPreviewExtent()
    {
        const Display = Options.Display();
        const Requested = Number(Canvas.dataset.previewWidth) || Math.min(1440, Display.Width * Math.min(Display.PixelRatio, 1.5));
        return PreviewExtent(Display.Width, Display.Height, Display.PixelRatio, Requested, PreviewDensity.Pixels(LatestView.Interacting));
    }

    function SubmitImage()
    {
        if (!LatestView || !Surface || !Options.Display().Visible || Suspended || InspectionPending) return;
        const PixelBudget = PreviewDensity.Pixels(LatestView.Interacting);
        const Extent = InspectPreviewExtent();
        if (!Extent) return;
        const Signature = JSON.stringify([LatestView.Camera, LatestView.Wave, LatestView.Ocean, LatestView.OceanTime, Extent, Revision, Surface.Quality, Surface.DetailKey || '', Surface.PreparationEpoch || 0]);
        if (Signature === LastCompletedSignature || performance.now() - LastSubmission < 33) return;
        LastSubmission = performance.now();
        if (Canvas.width !== Extent[0] || Canvas.height !== Extent[1]) [Canvas.width, Canvas.height] = Extent;
        GL.bindFramebuffer(GL.FRAMEBUFFER, ImageFramebuffer);
        if (ImageWidth !== Extent[0] || ImageHeight !== Extent[1])
        {
            [ImageWidth, ImageHeight] = Extent;
            GL.bindRenderbuffer(GL.RENDERBUFFER, ImageColour);
            GL.renderbufferStorage(GL.RENDERBUFFER, GL.RGBA8, ...Extent);
            GL.framebufferRenderbuffer(GL.FRAMEBUFFER, GL.COLOR_ATTACHMENT0, GL.RENDERBUFFER, ImageColour);
            GL.bindRenderbuffer(GL.RENDERBUFFER, ImageDepth);
            GL.renderbufferStorage(GL.RENDERBUFFER, GL.DEPTH_COMPONENT32F, ...Extent);
            GL.framebufferRenderbuffer(GL.FRAMEBUFFER, GL.DEPTH_ATTACHMENT, GL.RENDERBUFFER, ImageDepth);
            if (GL.checkFramebufferStatus(GL.FRAMEBUFFER) !== GL.FRAMEBUFFER_COMPLETE)
                throw Error('Floating-point GPU depth is unavailable. Preview stopped without a software fallback.');
        }
        GL.viewport(0, 0, ...Extent);
        if (GpuTimer)
        {
            PendingTiming = GL.createQuery();
            GL.beginQuery(GpuTimer.TIME_ELAPSED_EXT, PendingTiming);
        }
        GL.clearColor(.3, .35, .4, 1);
        GL.clearDepth(0);
        GL.depthMask(true);
        GL.clear(GL.COLOR_BUFFER_BIT | GL.DEPTH_BUFFER_BIT);
        GL.disable(GL.BLEND);
        GL.disable(GL.CULL_FACE);
        GL.disable(GL.DEPTH_TEST);
        GL.depthMask(false);
        const OceanOrigin = [Math.round(LatestView.Camera.Target[0] / 1000) * 1000, 0,
            Math.round(LatestView.Camera.Target[2] / 1000) * 1000];
        ConfigureView(ProgramSky, LatestView, OceanOrigin);
        GL.uniform1i(Location(ProgramSky, 'Ocean'), LatestView.Ocean ? 1 : 0);
        GL.bindVertexArray(SkyVertices.VertexArray);
        GL.drawArrays(GL.TRIANGLES, 0, 3);
        GL.enable(GL.DEPTH_TEST);
        GL.depthMask(true);
        GL.depthFunc(GL.GREATER);
        let Draws = 1, PreviousOrigin = null;
        for (const Batch of Batches)
        {
            if (PreviousOrigin !== Batch.Origin) ConfigureView(ProgramSurface, LatestView, Batch.Origin);
            PreviousOrigin = Batch.Origin;
            GL.uniform1i(Location(ProgramSurface, 'ExcludeDetail'), Surface.DetailMinimum && Batch.Section === 0 ? 1 : 0);
            if (Surface.DetailMinimum && Batch.Section === 0)
            {
                GL.uniform3fv(Location(ProgramSurface, 'DetailMinimum'), Surface.DetailMinimum.map((Coordinate, Axis) => Coordinate - Batch.Origin[Axis]));
                GL.uniform3fv(Location(ProgramSurface, 'DetailMaximum'), Surface.DetailMaximum.map((Coordinate, Axis) => Coordinate - Batch.Origin[Axis]));
            }
            GL.bindVertexArray(Batch.VertexArray);
            GL.drawArrays(GL.TRIANGLES, 0, Batch.Count);
            Draws++;
        }
        if (LatestView.Ocean)
        {
            ConfigureView(ProgramOcean, LatestView, OceanOrigin);
            ConfigureShore(ProgramOcean);
            GL.uniform2f(Location(ProgramOcean, 'OceanCentre'),
                Math.round(LatestView.Camera.Target[0] / 64) * 64 - OceanOrigin[0],
                Math.round(LatestView.Camera.Target[2] / 64) * 64 - OceanOrigin[2]);
            GL.uniform1f(Location(ProgramOcean, 'OceanExtent'), OceanWorldExtent);
            // 📝 Dolly changes projection, never wavelength, amplitude, phase, or the water's world-space grid extent.
            GL.uniform4fv(Location(ProgramOcean, 'OceanVectors[0]'), PrepareOceanSpectrum(LatestView.Wave, 0));
            GL.uniform2fv(Location(ProgramOcean, 'OceanPhases[0]'), OceanPhaseParameters);
            GL.uniform1f(Location(ProgramOcean, 'OceanTime'), LatestView.OceanTime);
            GL.uniform1f(Location(ProgramOcean, 'Wave'), LatestView.Wave);
            GL.bindVertexArray(WaterVertices.VertexArray);
            GL.drawElements(GL.TRIANGLES, WaterVertices.IndexCount, GL.UNSIGNED_SHORT, 0);
            Draws++;
            Canvas.dataset.oceanRenders = String(Number(Canvas.dataset.oceanRenders) + 1);
        }
        GL.bindFramebuffer(GL.READ_FRAMEBUFFER, ImageFramebuffer);
        GL.bindFramebuffer(GL.DRAW_FRAMEBUFFER, null);
        GL.blitFramebuffer(0, 0, ...Extent, 0, 0, ...Extent, GL.COLOR_BUFFER_BIT, GL.NEAREST);
        GL.bindFramebuffer(GL.FRAMEBUFFER, null);
        if (PendingTiming) GL.endQuery(GpuTimer.TIME_ELAPSED_EXT);
        PendingFence = GL.fenceSync(GL.SYNC_GPU_COMMANDS_COMPLETE, 0);
        if (!PendingFence) throw Error('The GPU could not allocate its image fence. Rendering stopped.');
        GL.flush();
        SubmittedAt = performance.now();
        VisibleGpuInterval.Reset();
        PendingRevision = Revision;
        PendingQuality = Surface.Quality;
        PendingDetailKey = Surface.DetailKey || '';
        PendingPreparationEpoch = Surface.PreparationEpoch || 0;
        PendingSampling = { Pitch: Surface.Pitch, Triangles: Surface.Triangles, Working: Surface.Working || null, Refinement: Surface.Refinement || null, Width: Extent[0], Height: Extent[1] };
        LastSignature = Signature;
        Object.assign(Canvas.dataset, {
            pendingGpuImages: '1', bufferWidth: String(Extent[0]), bufferHeight: String(Extent[1]),
            drawCalls: String(Draws), oceanVertices: String(LatestView.Ocean ? WaterVertices.Count : 0),
            lastSubmitMilliseconds: String(SubmittedAt - LastSubmission), submittedComposition: Signature,
            previewPixelBudget: String(PixelBudget), previewPixelCeiling: String(PreviewDensity.Ceiling()),
            oceanWorldExtent: String(OceanWorldExtent), oceanSpectrumFootprint: '0'
        });
    }

    function Advance()
    {
        if (Disposed || Stopped) return;
        // 📝 Hidden OffscreenCanvas contexts may be descheduled by the browser. Do not poll GL or spend their visible wait budget.
        if (Suspended) { Timer = setTimeout(Advance, 100); return; }
        try
        {
            if (!Compiled)
            {
                if (Parallel && Programs.some(Program => !GL.getProgramParameter(Program.Program, Parallel.COMPLETION_STATUS_KHR)))
                { Timer = setTimeout(Advance, 8); return; }
                for (const Program of Programs) ValidateProgram(GL, Program);
                Compiled = true;
                Canvas.dataset.shaderInitializationMilliseconds = String(performance.now() - Start);
                Status('geometry', 'GPU programs ready. Preparing the bounded XYZ surface.');
            }
            if (PendingFence)
            {
                const Completion = GL.clientWaitSync(PendingFence, 0, 0);
                const Elapsed = VisibleGpuInterval.Elapsed();
                if (Completion === GL.WAIT_FAILED) throw Error('The GPU image fence failed. Rendering stopped.');
                if (Completion === GL.TIMEOUT_EXPIRED)
                {
                    if (Elapsed > MaximumGpuFenceMilliseconds)
                        throw Error('The GPU did not finish a bounded image within four seconds. Further submissions are stopped; no software fallback.');
                    Timer = setTimeout(Advance, 8);
                    return;
                }
                GL.deleteSync(PendingFence);
                PendingFence = null;
                let ImageMilliseconds = Elapsed;
                if (PendingTiming)
                {
                    if (GL.getQueryParameter(PendingTiming, GL.QUERY_RESULT_AVAILABLE) && !GL.getParameter(GpuTimer.GPU_DISJOINT_EXT))
                    {
                        ImageMilliseconds = GL.getQueryParameter(PendingTiming, GL.QUERY_RESULT) / 1e6;
                        Canvas.dataset.gpuImageMilliseconds = String(ImageMilliseconds);
                    }
                    GL.deleteQuery(PendingTiming);
                    PendingTiming = null;
                }
                if ((PendingQuality >= 48 || PendingDetailKey) && !LatestView.Interacting && Canvas.dataset.geometryCompletedRevision === String(PendingRevision))
                    PreviewDensity.Observe(Canvas.width * Canvas.height, ImageMilliseconds);
                const NextExtent = InspectPreviewExtent();
                Canvas.dataset.previewDensitySettled = String(Boolean(NextExtent && NextExtent[0] === Canvas.width && NextExtent[1] === Canvas.height));
                MaximumFenceMilliseconds = Math.max(MaximumFenceMilliseconds, Elapsed);
                LastCompletedSignature = LastSignature;
                Object.assign(Canvas.dataset, {
                    pendingGpuImages: '0', completedRevision: String(PendingRevision), completedQuality: String(PendingQuality),
                    completedDetailKey: PendingDetailKey, completedPreparationEpoch: String(PendingPreparationEpoch),
                    displayedSurfacePitch: String(PendingSampling.Pitch), displayedSurfaceTriangles: String(PendingSampling.Triangles),
                    displayedWorkingLattice: JSON.stringify(PendingSampling.Working),
                    displayedSurfaceRefinement: JSON.stringify(PendingSampling.Refinement),
                    displayedWidth: String(PendingSampling.Width), displayedHeight: String(PendingSampling.Height),
                    completedComposition: LastSignature,
                    maximumFenceMilliseconds: String(MaximumFenceMilliseconds), lastFenceMilliseconds: String(Elapsed),
                    terrainRenders: String(Number(Canvas.dataset.terrainRenders) + 1)
                });
                Status('ready', 'GPU surface preview ready. Camera frames reuse the prepared XYZ geometry.');
                Options.Progress?.(1);
            }
            if (!Suspended && PendingSurface)
            {
                const Next = PendingSurface;
                PendingSurface = null;
                ReclaimBatches();
                Surface = Next.Prepared;
                Revision = Next.Revision;
                Batches = Surface.Chunks.map((Chunk, Index) => ({
                    ...ConstructVertexArray(GL, Chunk, 9), Origin: Surface.Origins[Index] || Surface.Origin, Section: Surface.Sections?.[Index] || 0
                }));
                Surface.Chunks = [];
                Surface.Origins = [];
                LastCompletedSignature = '';
                Object.assign(Canvas.dataset, {
                    volumeRevision: String(Revision), surfaceQuality: String(Surface.Quality),
                    surfaceDetailKey: Surface.DetailKey || '', surfacePreparationEpoch: String(Surface.PreparationEpoch || 0), workingLattice: JSON.stringify(Surface.Working || null),
                    contextTriangles: String(Surface.ContextTriangles || 0), workingTriangles: String(Surface.DetailTriangles || 0),
                    workingContextReused: String(Surface.ContextReused || false), workingContextExpansion: String(Surface.ContextExpansion || 1),
                    surfacePitch: String(Surface.Pitch), surfaceTriangles: String(Surface.Triangles), surfaceBytes: String(Surface.Bytes),
                    surfaceSamples: String(Surface.Samples), surfaceEvaluations: String(Surface.Evaluations),
                    surfacePreparationMilliseconds: String(Surface.PreparationMilliseconds),
                    surfaceMaximumSliceMilliseconds: String(Surface.MaximumSliceMilliseconds)
                });
            }
            if (!Suspended && PendingShore)
            {
                Shore = PendingShore.Prepared;
                PendingShore = null;
                GL.bindTexture(GL.TEXTURE_2D, ShoreTexture);
                GL.texImage2D(GL.TEXTURE_2D, 0, GL.R32F, Shore.Size, Shore.Size, 0, GL.RED, GL.FLOAT, Shore.Distances);
                LastCompletedSignature = '';
            }
            SubmitImage();
        }
        catch (Failure) { Failed(Failure); return; }
        Timer = setTimeout(Advance, Suspended ? 100 : 8);
    }

    function Rasterize(Camera, Seconds, Wave, Ocean, OceanTime = Seconds, Interacting = false)
    {
        if (Disposed || Stopped) return;
        LatestView = { Camera, Wave, Ocean, OceanTime: Ocean ? OceanTime : 0, Interacting };
    }

    function Resume()
    {
        Suspended = false;
        VisibleGpuInterval.Resume();
        Canvas.dataset.previewSuspended = 'false';
    }

    async function Inspect(Category = 'status')
    {
        if (Category === 'lose')
        {
            const Extension = GL.getExtension('WEBGL_lose_context');
            setTimeout(() => Extension?.loseContext(), 0);
            return { Requested: Boolean(Extension) };
        }
        if (Category === 'detail') return { Working: Surface?.Working || null,
            ContextTriangles: Surface?.ContextTriangles || 0, DetailTriangles: Surface?.DetailTriangles || 0,
            ContextBatches: Batches.filter(Batch => Batch.Section === 0).length,
            DetailBatches: Batches.filter(Batch => Batch.Section === 1).length, Error: GL.getError() };
        if (!['pixels', 'ocean'].includes(Category)) return { ...Canvas.dataset };
        InspectionPending = true;
        try
        {
            while (PendingFence && !Disposed && !Stopped) await new Promise(Resolve => setTimeout(Resolve, 10));
            if (Disposed || Stopped) throw Error('GPU preview is stopped.');
            if (Category === 'ocean')
            {
                return {
                    Extent: GL.getUniform(ProgramOcean.Program, Location(ProgramOcean, 'OceanExtent')),
                    Centre: Array.from(GL.getUniform(ProgramOcean.Program, Location(ProgramOcean, 'OceanCentre'))),
                    Time: GL.getUniform(ProgramOcean.Program, Location(ProgramOcean, 'OceanTime')),
                    Spectrum: Array.from({ length: 24 }, (_, Index) => Array.from(GL.getUniform(ProgramOcean.Program,
                        Location(ProgramOcean, `OceanVectors[${Index}]`)))).flat(),
                    Phases: Array.from({ length: 24 }, (_, Index) => Array.from(GL.getUniform(ProgramOcean.Program,
                        Location(ProgramOcean, `OceanPhases[${Index}]`)))).flat(),
                    Error: GL.getError()
                };
            }
            const Pixels = new Uint8Array(Canvas.width * Canvas.height * 4);
            GL.readPixels(0, 0, Canvas.width, Canvas.height, GL.RGBA, GL.UNSIGNED_BYTE, Pixels);
            return { Pixels, Width: Canvas.width, Height: Canvas.height, Error: GL.getError(), Telemetry: { ...Canvas.dataset } };
        }
        finally { InspectionPending = false; }
    }

    function Dispose()
    {
        if (Disposed) return;
        Disposed = true;
        clearTimeout(Timer);
        Canvas.removeEventListener('webglcontextlost', Lost);
        if (PendingFence) GL.deleteSync(PendingFence);
        if (PendingTiming) GL.deleteQuery(PendingTiming);
        ReclaimBatches();
        for (const Vertices of [SkyVertices, WaterVertices])
        { GL.deleteBuffer(Vertices.Buffer); GL.deleteVertexArray(Vertices.VertexArray); }
        GL.deleteRenderbuffer(ImageColour);
        GL.deleteRenderbuffer(ImageDepth);
        GL.deleteFramebuffer(ImageFramebuffer);
        GL.deleteBuffer(WaterVertices.IndexBuffer);
        GL.deleteTexture(ShoreTexture);
        for (const CompiledProgram of Programs)
        {
            for (const Shader of CompiledProgram.Shaders) GL.deleteShader(Shader);
            GL.deleteProgram(CompiledProgram.Program);
        }
    }

    Status('compiling', 'Preparing small GPU surface and ocean programs. No procedural SDF pixel shader.');
    Timer = setTimeout(Advance, 0);
    return { UploadSurface, UploadShore, Rasterize, Suspend, Resume, Failed, Inspect, Dispose };
}
