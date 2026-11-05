//============================================================================================================================================
//                                                            VEHICLESCAN.WGSL
//============================================================================================================================================
// 🧩 Vehicle scan overlay, including reference-derived hologram, curtain and particle equations.
//    The deferred solid transition and complete visual result are not yet equivalent to the reference renderer.

//! include "FrameDescriptor.wgsl"
//! include "SurfaceReflectance.wgsl"
//! include "VehicleVisual.wgsl"

struct ScanEffectRecord
{
    State0 : vec4f,   // x: ScanX, y: HoloAmount, z: Clock, w: EffectiveMode
    State1 : vec4f,   // x: EdgeOpacity, y: CurtainAlpha0, z: CurtainAlpha1, w: SparkleAlpha
    Ghost0 : vec4f,   // xyz: GhostBaseLinear (#a9dcec), w: ClipOffset (0.05 m)
    Ghost1 : vec4f,   // xyz: GhostAccentLinear (#38bdf8), w: LineWidthPixels
    Edge0  : vec4f,   // xyz: ScanEdgeLinear (#8fd8f2), w: body-relative curtain height
    Planes : vec4f,   // xy: Curtain0 (2.4, 2.5), zw: Curtain1 (0.55, 2.5)
    Viewport : vec4f, // x: drawing-buffer pixel ratio
}

struct DihedralEdgeRecord
{
    PointA : vec3f,
    MetaA  : u32,
    PointB : vec3f,
    MetaB  : u32,
}

struct ScanSparkleRecord
{
    BasePosition : vec3f,
    Size         : f32,
    Noise        : vec3f,
    Speed        : f32,
}

@group(0) @binding(1) var<storage, read> Occupants    : array<OccupantRecord>;
@group(0) @binding(2) var<storage, read> Materials    : array<MaterialRecord>;
@group(0) @binding(3) var<storage, read> VehicleState : VehicleRecord;
@group(0) @binding(4) var<uniform> ScanEffect         : ScanEffectRecord;
@group(0) @binding(5) var<storage, read> Edges        : array<DihedralEdgeRecord>;
@group(0) @binding(6) var<storage, read> Sparkles     : array<ScanSparkleRecord>;

const VehicleOccupantCount : u32 = 5u;
const HeadlightFamily      : u32 = 13u;
const TaillightFamily      : u32 = 14u;

fn ResolveScanSurface(Family: u32, WorldPosition: vec3f) -> SurfaceSample
{
    if (Family == BodyCoatMaterialFamily && arrayLength(&Materials) > RippleTransformSlot)
    {
        return ProjectRippledSurfaceSample(Materials[Family], Materials[RipplePreviousSlot], Materials[RippleStateSlot],
                                           Materials[RippleTransformSlot], WorldPosition);
    }
    return ProjectSurfaceSample(Materials[Family]);
}

fn DeformScanVertex(Index: u32, Family: u32, Position: vec3f, Normal: vec3f) -> DeformedVertex
{
    var Moved: DeformedVertex;
    Moved.Position = Position;
    Moved.Normal   = Normal;
    if (Index >= 1u && Index < VehicleOccupantCount)
    {
        return ResolveWheelVertex(Index - 1u, Family, Position, Normal);
    }
    if (Index == 0u)
    {
        return DeformBodyVertex(Family, Position, Normal);
    }
    return Moved;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      1 & 2. TRANSITION SOLID + HOLOGRAM GHOST MESH
//------------------------------------------------------------------------------------------------------------------------

struct ScanMeshVarying
{
    @builtin(position) Position            : vec4f,
    @location(0) WorldPosition             : vec3f,
    @location(1) WorldNormal               : vec3f,
    @location(2) BodyLocal                 : vec3f,
    @location(3) @interpolate(flat) Family : u32,
}

@vertex
fn ScanMeshVertex(
    @location(0) Position: vec3f,
    @location(1) Family: u32,
    @location(2) Normal: vec3f,
    @location(3) OccupantOffset: u32,
    @builtin(instance_index) OccupantIndex: u32
) -> ScanMeshVarying
{
    let Index    = OccupantIndex + OccupantOffset;
    let Occupant = Occupants[Index];
    let Moved    = DeformScanVertex(Index, Family, Position, Normal);
    let World    = Occupant.LocalToWorld * vec4f(Moved.Position, 1.0);
    var Out: ScanMeshVarying;
    Out.Position      = Frame.ViewProjection * World;
    Out.WorldPosition = World.xyz;
    Out.WorldNormal   = normalize((Occupant.LocalToWorld * vec4f(Moved.Normal, 0.0)).xyz);
    Out.BodyLocal     = (Occupants[0].WorldToLocal * World).xyz;
    Out.Family        = select(Family, Occupant.MaterialOverride, Occupant.MaterialOverride != NoMaterialOverride);
    return Out;
}

// 📝 Smooth continuous alpha dissolve across the [-0.9, -0.05] scan front where SurfaceRaster yielded the opaque G-buffer
//    to the studio floor behind the car. Matches diffuseColor.a *= 1.0 - sf * uHoloAmt * 0.965 in reference Truck.tsx.
@fragment
fn TransitionSolidFragment(Varying: ScanMeshVarying) -> @location(0) vec4f
{
    if (Varying.Family == HeadlightFamily || Varying.Family == TaillightFamily || Varying.Family == VehicleGlassFamily)
    {
        discard;
    }
    let ScanD  = Varying.BodyLocal.x - ScanEffect.State0.x;
    let Reveal = smoothstep(-0.9, -0.05, ScanD);
    if (Reveal <= 0.002)
    {
        discard;
    }
    let Mode       = u32(ScanEffect.State0.w + 0.5);
    let Dissolve   = select(0.965, 1.0, Mode == 2u);
    let SolidAlpha = 1.0 - Reveal * ScanEffect.State0.y * Dissolve;
    if (SolidAlpha <= 0.008)
    {
        discard;
    }

    let Surface = ResolveScanSurface(Varying.Family, Varying.WorldPosition);
    let Normal  = normalize(Varying.WorldNormal);
    let View    = normalize(Frame.CameraPosition - Varying.WorldPosition);
    let NdotV   = abs(dot(Normal, View));

    // Studio & outdoor lighting approximation matching the primary PBR tone across the 85 cm dissolve band.
    let SunTerm   = Frame.SunIlluminance * ResolveDirectReflectance(Surface, Normal, View, Frame.SunDirection);
    let Overhead  = vec3f(0.0, 0.0, 1.0);
    let StudioKey = ResolveDirectReflectance(Surface, Normal, View, Overhead) * (1.35 + 1.85 * Frame.SkyScale);
    let Fresnel   = ResolveFresnel(ResolveSpecularColour(Surface), NdotV);
    let Hemis     = ResolveDiffuseAlbedo(Surface) * (0.14 + 0.16 * saturate(Normal.z)) + Fresnel * 0.22;
    let Emissive  = Surface.Emission * Frame.EmissionScale;
    let Radiance  = SunTerm + StudioKey + Hemis + Emissive;

    return vec4f(Radiance, SolidAlpha);
}

// 📝 Hologram equations from holoVert / holoFrag in reference Truck.tsx:
//    fres  = pow(1.0 - abs(dot(N, V)), 2.3)
//    lines = smoothstep(0.72, 1.0, sin(height * 85.0) * 0.5 + 0.5)
//    edge  = exp(-pow((scanD + 0.18) * 4.2, 2.0))
//    a     = (0.028 + fres * 0.50 + lines * 0.040 + edge * 0.30) * vis
//    col   = uColor + uAccent * edge * 1.15
@fragment
fn GhostShellFragment(Varying: ScanMeshVarying) -> @location(0) vec4f
{
    if (Varying.Family == HeadlightFamily || Varying.Family == TaillightFamily)
    {
        discard;
    }
    let ScanD  = Varying.BodyLocal.x - ScanEffect.State0.x;
    let Region = smoothstep(-0.9, -0.05, ScanD);
    let Vis    = Region * ScanEffect.State0.y;
    if (Vis < 0.003)
    {
        discard;
    }

    let Normal = normalize(Varying.WorldNormal);
    let View   = normalize(Frame.CameraPosition - Varying.WorldPosition);
    let Fres   = pow(clamp(1.0 - abs(dot(Normal, View)), 0.0, 1.0), 2.3);
    // The reference floor is at Y=-0.63; this renderer's Z-up floor is at zero.
    let Lines  = smoothstep(0.72, 1.0, sin((Varying.WorldPosition.z - 0.63) * 85.0) * 0.5 + 0.5);
    let Edge   = exp(-pow((ScanD + 0.18) * 4.2, 2.0));

    let Alpha  = (0.028 + Fres * 0.50 + Lines * 0.040 + Edge * 0.30) * Vis;
    let Colour = ScanEffect.Ghost0.xyz + ScanEffect.Ghost1.xyz * (Edge * 1.15);
    return vec4f(Colour, clamp(Alpha, 0.0, 1.0));
}

//------------------------------------------------------------------------------------------------------------------------
//                                  3. QUAD-EXPANDED 14°/28° DIHEDRAL FEATURE WIREFRAME
//------------------------------------------------------------------------------------------------------------------------

struct WireVarying
{
    @builtin(position) Position : vec4f,
    @location(0) BodyLocal      : vec3f,
    @location(1) EdgeCoord      : f32,
}

@vertex
fn WireEdgeVertex(@builtin(vertex_index) VertexIndex: u32, @builtin(instance_index) EdgeIndex: u32) -> WireVarying
{
    var Out: WireVarying;
    let Edge = Edges[EdgeIndex];
    let SlotA   = Edge.MetaA & 0xFFu;
    let FamilyA = (Edge.MetaA >> 8u) & 0xFFFFu;
    let SlotB   = Edge.MetaB & 0xFFu;
    let FamilyB = (Edge.MetaB >> 8u) & 0xFFFFu;

    let MovedA = DeformScanVertex(SlotA, FamilyA, Edge.PointA, vec3f(0.0, 0.0, 1.0));
    let MovedB = DeformScanVertex(SlotB, FamilyB, Edge.PointB, vec3f(0.0, 0.0, 1.0));

    // Collapse shredded tyre rubber edges cleanly onto the axle.
    if (FamilyA == TyreFamily && SlotA >= 1u && SlotA < VehicleOccupantCount && VehicleState.Wheels[SlotA - 1u].x > 0.5)
    {
        Out.Position  = vec4f(0.0, 0.0, -2.0, 1.0);
        Out.BodyLocal = vec3f(-999.0);
        Out.EdgeCoord = 0.0;
        return Out;
    }

    let WorldA = Occupants[SlotA].LocalToWorld * vec4f(MovedA.Position, 1.0);
    let WorldB = Occupants[SlotB].LocalToWorld * vec4f(MovedB.Position, 1.0);
    let LocalA = (Occupants[0].WorldToLocal * WorldA).xyz;
    let LocalB = (Occupants[0].WorldToLocal * WorldB).xyz;

    let ClipA = Frame.ViewProjection * WorldA;
    let ClipB = Frame.ViewProjection * WorldB;
    if (ClipA.w <= 0.02 || ClipB.w <= 0.02)
    {
        Out.Position  = vec4f(0.0, 0.0, -2.0, 1.0);
        Out.BodyLocal = vec3f(-999.0);
        Out.EdgeCoord = 0.0;
        return Out;
    }

    let ScreenA = (ClipA.xy / ClipA.w) * 0.5 * Frame.ScreenExtent;
    let ScreenB = (ClipB.xy / ClipB.w) * 0.5 * Frame.ScreenExtent;
    let Delta   = ScreenB - ScreenA;
    let Len     = length(Delta);
    let Dir     = select(vec2f(1.0, 0.0), Delta / max(Len, 1.0e-4), Len > 1.0e-4);
    let Perp    = vec2f(-Dir.y, Dir.x);

    // 2-triangle quad corners: (Along ∈ {0, 1}, Across ∈ {-1, +1})
    var Along  = 0.0;
    var Across = -1.0;
    switch (VertexIndex)
    {
        case 0u: { Along = 0.0; Across = -1.0; }
        case 1u: { Along = 1.0; Across = -1.0; }
        case 2u: { Along = 1.0; Across =  1.0; }
        case 3u: { Along = 0.0; Across = -1.0; }
        case 4u: { Along = 1.0; Across =  1.0; }
        default: { Along = 0.0; Across =  1.0; }
    }

    var Clip = mix(ClipA, ClipB, Along);
    let HalfWidthPixels = ScanEffect.Ghost1.w * 0.5;
    let NdcOffset = Perp * (Across * HalfWidthPixels * 2.0) * Frame.InverseScreenExtent;
    Clip.x += NdcOffset.x * Clip.w;
    Clip.y += NdcOffset.y * Clip.w;
    // Slight forward depth bias in reversed-Z so edges sit cleanly over the ghost shell without z-fighting.
    Clip.z += 2.5e-4 * Clip.w;

    Out.Position  = Clip;
    Out.BodyLocal = mix(LocalA, LocalB, Along);
    Out.EdgeCoord = Across;
    return Out;
}

@fragment
fn WireEdgeFragment(Varying: WireVarying) -> @location(0) vec4f
{
    // Reference Truck.tsx clipPlane: normal (1, 0, 0), constant -(uScanX + 0.05) -> keeps BodyLocal.x >= uScanX + 0.05.
    let ClipLimit = ScanEffect.State0.x + ScanEffect.Ghost0.w;
    if (Varying.BodyLocal.x < ClipLimit)
    {
        discard;
    }
    let Coverage = 1.0 - smoothstep(0.45, 1.0, abs(Varying.EdgeCoord));
    let Alpha    = ScanEffect.State1.x * Coverage;
    if (Alpha <= 0.003)
    {
        discard;
    }
    return vec4f(ScanEffect.Edge0.xyz, clamp(Alpha, 0.0, 1.0));
}

//------------------------------------------------------------------------------------------------------------------------
//                                    4. DUAL SCAN WAVEFRONT CURTAIN PLANES
//------------------------------------------------------------------------------------------------------------------------

struct CurtainVarying
{
    @builtin(position) Position          : vec4f,
    @location(0) Uv                      : vec2f,
    @location(1) @interpolate(flat) Slot : u32,
}

@vertex
fn CurtainPlaneVertex(@builtin(vertex_index) VertexIndex: u32, @builtin(instance_index) PlaneIndex: u32) -> CurtainVarying
{
    let SizeY = select(ScanEffect.Planes.x, ScanEffect.Planes.z, PlaneIndex == 1u);
    let SizeZ = select(ScanEffect.Planes.y, ScanEffect.Planes.w, PlaneIndex == 1u);

    var Corner = vec2f(-0.5, -0.5);
    switch (VertexIndex)
    {
        case 0u: { Corner = vec2f(-0.5, -0.5); }
        case 1u: { Corner = vec2f( 0.5, -0.5); }
        case 2u: { Corner = vec2f( 0.5,  0.5); }
        case 3u: { Corner = vec2f(-0.5, -0.5); }
        case 4u: { Corner = vec2f( 0.5,  0.5); }
        default: { Corner = vec2f(-0.5,  0.5); }
    }

    // Plane normal points along X, spanning Y (width) × Z (height) at BodyLocal.x = uScanX.
    let LocalPos = vec3f(ScanEffect.State0.x, Corner.x * SizeY, ScanEffect.Edge0.w + Corner.y * SizeZ);
    let World    = Occupants[0].LocalToWorld * vec4f(LocalPos, 1.0);

    var Out: CurtainVarying;
    Out.Position = Frame.ViewProjection * World;
    Out.Uv       = Corner + vec2f(0.5);
    Out.Slot     = PlaneIndex;
    return Out;
}

@fragment
fn CurtainPlaneFragment(Varying: CurtainVarying) -> @location(0) vec4f
{
    let BaseAlpha = select(ScanEffect.State1.y, ScanEffect.State1.z, Varying.Slot == 1u);
    if (BaseAlpha <= 0.001)
    {
        discard;
    }
    // Truck.tsx uses an untextured, constant-opacity MeshBasicMaterial with additive blending.
    return vec4f(ScanEffect.Ghost1.xyz, clamp(BaseAlpha, 0.0, 1.0));
}

//------------------------------------------------------------------------------------------------------------------------
//                                  5. 130 FLOATING #8FD8F2 HOLOGRAPHIC SPARKLES
//------------------------------------------------------------------------------------------------------------------------

// Drei evaluates its motion after modelMatrix, in world coordinates. The reference uses Y-up;
// the renderer uses Z-up with (reference x, y, z) mapped to (x, -z, y).
fn ProjectSparkleWorld(BaseWorld: vec3f, Time: f32, Speed: f32, Noise: vec3f) -> vec3f
{
    let T = Time * Speed;
    let OriginalX = BaseWorld.x;
    return BaseWorld + vec3f(
        cos(T + OriginalX * Noise.z * 100.0) * 0.2,
       -cos(T + OriginalX * Noise.y * 100.0) * 0.2,
        sin(T + OriginalX * Noise.x * 100.0) * 0.2
    );
}

fn ResolveSparklePixelSize(Size: f32, PixelRatio: f32, ViewDepth: f32) -> f32
{
    // WebGL rasterizes at least one pixel; its device-specific maximum is not known to this renderer.
    return max(1.0, Size * 25.0 * PixelRatio / max(ViewDepth, 1.0e-6));
}

fn ResolveSparkleAlpha(Corner: vec2f, Opacity: f32) -> f32
{
    // Corner spans [-1, 1], whereas gl_PointCoord spans [0, 1]. Avoid the centre singularity.
    let Distance = length(Corner) * 0.5;
    let Strength = 0.05 / max(Distance, 1.0e-6) - 0.1;
    return clamp(Strength * Opacity, 0.0, 1.0);
}

struct SparkleVarying
{
    @builtin(position) Position : vec4f,
    @location(0) Corner         : vec2f,
}

@vertex
fn SparkleQuadVertex(@builtin(vertex_index) VertexIndex: u32, @builtin(instance_index) SparkleIndex: u32) -> SparkleVarying
{
    let Particle = Sparkles[SparkleIndex];
    let BaseWorld = (Occupants[0].LocalToWorld * vec4f(Particle.BasePosition, 1.0)).xyz;
    let World = ProjectSparkleWorld(BaseWorld, ScanEffect.State0.z, Particle.Speed, Particle.Noise);
    let ViewDepth = -(Frame.View * vec4f(World, 1.0)).z;
    var Clip = Frame.ViewProjection * vec4f(World, 1.0);

    var Quad = vec2f(-1.0, -1.0);
    switch (VertexIndex)
    {
        case 0u: { Quad = vec2f(-1.0, -1.0); }
        case 1u: { Quad = vec2f( 1.0, -1.0); }
        case 2u: { Quad = vec2f( 1.0,  1.0); }
        case 3u: { Quad = vec2f(-1.0, -1.0); }
        case 4u: { Quad = vec2f( 1.0,  1.0); }
        default: { Quad = vec2f(-1.0,  1.0); }
    }

    var Out: SparkleVarying;
    Out.Corner = Quad;
    if (ViewDepth <= 0.0 || Clip.w <= 0.0)
    {
        Out.Position = vec4f(0.0, 0.0, -2.0, 1.0);
        return Out;
    }

    let PixelSize = ResolveSparklePixelSize(Particle.Size, ScanEffect.Viewport.x, ViewDepth);
    let NdcOffset = Quad * PixelSize * Frame.InverseScreenExtent;
    Clip.x += NdcOffset.x * Clip.w;
    Clip.y += NdcOffset.y * Clip.w;
    Out.Position = Clip;
    return Out;
}

@fragment
fn SparkleQuadFragment(Varying: SparkleVarying) -> @location(0) vec4f
{
    let Alpha = ResolveSparkleAlpha(Varying.Corner, ScanEffect.State1.w);
    if (Alpha <= 0.0)
    {
        discard;
    }
    return vec4f(ScanEffect.Edge0.xyz, Alpha);
}
