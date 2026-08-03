#version 450

// 🧩 Fragment stage of the surface shade — the deferred half of deferred texturing. Read one packed identity from the R32_UINT
//    visibility buffer, reconstruct everything a BRDF needs from it alone, shade, tonemap, composite. No G-buffer: the raster
//    stored 32 bits of identity per pixel and this stage recovers position, normal, and material from that (Burns & Hunt 2013).
//
//    The reconstruction chain, per pixel:
//      identity -> partition (high 12) + primitive (low 20)
//      partition -> Instances[] -> model matrix, normal basis, MaterialId
//      primitive -> Indices[]/Vertices[] -> the triangle's three corners
//      pixel NDC + InverseViewProjection -> a world-space view ray
//      ray x triangle -> BARYCENTRIC weights -> world position
//      triangle plane -> GEOMETRIC (flat) normal
//      MaterialId -> Materials[] -> shading model + feature mask + channels
//
//    Barycentrics come from a ray-triangle intersection rather than screen-space derivatives (Schied & Dachsbacher): at a
//    silhouette or an id discontinuity a derivative-based reconstruction straddles two triangles and produces garbage weights,
//    whereas the analytic intersection is exact per pixel and independent of its neighbours. It costs one cross product more.
//
//    The normal is the triangle's own PLANE normal, not an interpolation of the vertex stream. The reference geometry is authored
//    flat (Suzzane.obj: `s 0`, 499 face normals for 500 faces) and the stride-32 stream cannot carry face-varying normals at all,
//    so there is nothing to interpolate — the bake writes `normals = []` and interpolating would read (0,0,0) -> NaN -> an
//    entirely unlit surface. Faceted-but-lit is the intended read here; smooth-highlight materials (Chrome, Clearcoat) will
//    therefore show per-facet lobes until a corner-normal channel or a smooth-normal derivation lands.
//
//    The BRDF core follows Filament / glTF (D_GGX, V_SmithGGXCorrelated, F_Schlick, D_Charlie, V_Kelemen) and every lobe is gated
//    by a FEATURE-MASK bit, not by a zeroed weight — Filament notes a clear-coat lobe roughly doubles specular cost, so a zeroed
//    weight would still pay for the work it was meant to skip. That gating is what makes the twelve presets data over one shared
//    path, and the thirteenth (Composite, every channel live, mask pushed per-frame) nearly free.

// ---- Identity pack (must match VisibilityRaster.frag / SoftwareRasterization.comp) ----
const uint PrimitiveBits      = 20u;
const uint PrimitiveMask      = (1u << PrimitiveBits) - 1u;
const uint VisibilitySentinel = 0xFFFFFFFFu;

// ---- Shading models (must match SurfaceShadingModel in SurfacePresetTable.h) ----
const uint ModelStandard   = 0u;
const uint ModelMetal      = 1u;
const uint ModelClearcoat  = 2u;
const uint ModelCloth      = 3u;
const uint ModelGlass      = 4u;
const uint ModelSubsurface = 5u;
const uint ModelIridescent = 6u;
const uint ModelEmissive   = 7u;
const uint ModelMatcap     = 8u;
const uint ModelComposite  = 9u;

// ---- Feature bits (must match SurfaceFeatureBit in SurfacePresetTable.h) ----
const uint FeatureDiffuse      = 1u << 0;
const uint FeatureSpecular     = 1u << 1;
const uint FeatureCoat         = 1u << 2;
const uint FeatureSheen        = 1u << 3;
const uint FeatureIridescence  = 1u << 4;
const uint FeatureEmissive     = 1u << 5;
const uint FeatureSubsurface   = 1u << 6;
const uint FeatureTransmission = 1u << 7;
const uint FeatureMatcap       = 1u << 8;

const float Pi = 3.14159265359;

// Filament's minimum roughness. Below this a mirror-smooth surface drives D_GGX's denominator toward zero and the highlight
// becomes NaN / fireflies — Chrome at Roughness 0.05 sits close enough to the floor that this clamp is what keeps it finite.
const float MinimumRoughness = 0.089;

layout(location = 0) in  vec2 FragTexCoord;
layout(location = 0) out vec4 OutColour;

layout(set = 0, binding = 0) uniform usampler2D VisibilityBuffer;

// One RenderVertex, stride-32: position @0, normal @12, texcoord @24. Only POSITION is read (the normal is derived geometrically
// from the triangle plane — see the shade body), but the full layout is declared so the std430 stride stays 32 and indexing is correct.
struct RenderVertex
{
    float PositionX; float PositionY; float PositionZ; // @0
    float NormalX;   float NormalY;   float NormalZ;   // @12
    float TexU;      float TexV;                       // @24
};

layout(std430, set = 0, binding = 1) readonly buffer VertexBlock
{
    RenderVertex Vertices[];
};

layout(std430, set = 0, binding = 2) readonly buffer IndexBlock
{
    uint Indices[];
};

// Mirrors SuzanneSceneInstance / VisibilityRaster.vert's SceneInstance (std140).
// 🔴 MIRRORS SuzanneSceneInstance (Graphics/Scene/SuzanneScene.h) FIELD FOR FIELD — 208 bytes. Three other shaders carry the same hand-written copy
//    (VisibilityRaster.vert, SoftwareRasterization.comp, ComponentOverlay.frag). A copy that falls behind the C++ struct still compiles and still
//    validates; it just strides by the wrong size, so instance N reads the tail of instance N-1. The host static_assert is what catches it.
struct SceneInstance
{
    mat4 Model;
    vec4 NormalBasis[3];
    vec4 Tint;
    uint PartitionId;
    uint MaterialId;
    uint MeshOrdinal;    // bottom-level tree slice — ray tracing only, unused here
    uint Pad0;
    mat4 InverseModel;   // world -> local, for the ray trace — unused here
};

layout(std140, set = 0, binding = 3) readonly buffer InstanceBlock
{
    SceneInstance Instances[];
};

// Mirrors SurfacePresetParameters (96 B as six 16-byte std140 slots).
struct SurfacePreset
{
    vec4 BaseColour;           // .w = alpha
    vec4 EmissiveColour;       // .w = strength
    vec4 RoughnessMetallic;    // x Roughness, y Metallic, z Reflectance, w pad
    vec4 SheenColourRoughness; // xyz sheen tint, w sheen roughness
    vec4 CoatIridescence;      // x coat weight, y coat roughness, z iridescence IOR, w iridescence thickness
    uvec4 ModelFeature;        // x ShadingModelId, y FeatureMask, zw pad
};

layout(std140, set = 0, binding = 4) uniform MaterialBlock
{
    SurfacePreset Materials[14];   // SurfacePresetCount
};

// The checkered floor's own geometry (P6.3a). A SECOND raster draws the floor into the SAME visibility buffer from these buffers, based at
// FloorPartitionBase so the two identity ranges stay disjoint — which is exactly why its triangles cannot be fetched from b1/b2 above.
// ⚠️ When no floor document loaded these three are ALIASED onto b1/b2/b3 rather than left unbound, so their lengths look healthy while holding
//    head data. FloorShadeEnabled, not a length test, is what says whether they are real.
layout(std430, set = 0, binding = 5) readonly buffer FloorVertexBlock
{
    RenderVertex FloorVertices[];
};

layout(std430, set = 0, binding = 6) readonly buffer FloorIndexBlock
{
    uint FloorIndices[];
};

layout(std140, set = 0, binding = 7) readonly buffer FloorInstanceBlock
{
    SceneInstance FloorInstances[];
};

//------------------------------------------------------------------------------------------------------------------------
//                                            SET 1 — THE SURFEL STATE (Phase 3 GI gather)
//------------------------------------------------------------------------------------------------------------------------

// 🧩 Phase 3: the deferred shade reads the surfel irradiance cache to REPLACE the flat ambient fill with a real one-bounce GI gather. This second
//    descriptor set mirrors SurfelIntegrate.comp's set 1 spelling EXACTLY so host and shader agree on layout — the gather module reads Offsets/List/
//    Surfels/Moments and atomicMax'es Touched, so those five carry the exact qualifiers the integrate uses.
//
// 🔴 GUIDING (b4) AND DEPTH (b5) MUST BE NON-readonly even though the gather never writes them: SurfelGuiding.glsl carries SurfelUpdateFromSample
//    (writes SurfelGuidingBuffer) and SurfelRadialDepth.glsl carries update_surfel_depth2 (writes SurfelDepthBuffer); both compile into this frag
//    through the #includes below even though the gather calls neither, and glslc rejects a write to a readonly block. Match the integrate: writable.
//    TOUCHED (b6) is written by the gather's atomicMax, so it is writable too.

// The moments struct: five vec4 rows (20 floats), one entry per surfel per ping-pong half. The shade reads the POST-SWAP read half — Moments[id +
// ReadOffsetElements] — which holds the fresh integrate output (the swap at RenderExtension already ran before this pass records). Field order is exact.
struct SurfelMomentEntry
{
    vec4 Irradiance;   // xyz = mean irradiance, w = totalCount
    vec4 MsmeData0;    // xyz = shortMean,       w = vbbr
    vec4 MsmeData1;    // xyz = variance,        w = inconsistency
    vec4 Hit;          // xyz = first hit point, w = debug flag
    vec4 Guiding;      // xyz = mean world dir,  w = slgMass
};

// SurfelRecord's layout must exist before the set-1 buffer block below names it, so pull the record module in here (ahead of the grouped includes after
// the push block). Its include guard makes the later #include a no-op.
#include "SurfelRecord.glsl"

layout(std430, set = 1, binding = 0) readonly  buffer SurfelBuffer   { SurfelRecord      Surfels[]; };
layout(std430, set = 1, binding = 1)           buffer MomentsBuffer  { SurfelMomentEntry SurfelMoments[]; };
layout(std430, set = 1, binding = 2) readonly  buffer OffsetsBuffer  { int  SurfelOffsets[]; };   // F6 split: per-cell [start,end)
layout(std430, set = 1, binding = 3) readonly  buffer ListBuffer     { int  SurfelList[]; };      // F6 split: packed per-cell surfel indices (no base)
layout(std430, set = 1, binding = 4)           buffer GuidingBuffer  { float SurfelGuidingBuffer[]; };   // 72 floats/surfel (SLG) — writable, see 🔴 above
layout(std430, set = 1, binding = 5)           buffer DepthBuffer    { vec4  SurfelDepthBuffer[]; };     // 16 vec4/surfel (MSM radial depth) — writable
layout(std430, set = 1, binding = 6)           buffer TouchedBuffer  { int   SurfelTouched[]; };         // per-surfel importance (atomicMax)

layout(push_constant) uniform ShadeConstants
{
    mat4 InverseViewProjection;   // [-] - clip -> world
    vec4 CameraPosition;          // [-] - world-space eye (.w unused)
    vec4 LightDirection;          // [-] - world-space direction TOWARD the light (.w unused)
    vec4 SunRadiance;             // [-] - rgb: key-light radiance (colour x intensity, PREMULTIPLIED on the host from the F10 Sun card); .w unused
    uint CompositeFeatureMask;    // [-] - overrides the Composite record's own mask only
    uint FloorPartitionBase;      // [-] - partition ordinals >= this belong to the floor mesh
    uint FloorShadeEnabled;       // [-] - 1 shades the floor from b5-b7, 0 discards it (the b5-b7 alias is not real floor data)
    uint FloorIndexBase;          // [-] - first index of the floor's run in b6; gl_PrimitiveID restarts per draw, b6 is a merged buffer

    // ---- Phase 3 surfel GI gather (std430 tail, vec4-padded rows first, then the uint scalars) ----
    vec4 GridOrigin;              // [-] - snapped grid origin, SAME source as the slotting/integrate build; .w unused
    vec4 OcclusionParams;         // [-] - (shadowStrength, bleedReduction, grazingBiasScale, varianceBleedScale) for the radial-depth gate
    uint SurfelReadOffsetElements;// [-] - moments read-half ELEMENT base = MomentsParity*Capacity (post-swap); NOT the byte offset
    uint SurfelGiEnabled;         // [-] - 1 gathers surfel GI, 0 falls back to the flat AmbientColour (A/B toggle)
    uint SurfelCapacity;          // [-] - pool capacity (unused by the gather math; carried for parity + future bounds)
    float TuneCellDiameter;       // [m] - live base cell edge (F10 window); the gather's cell must match spawn/slotting/integrate
    float TuneBaseRadius;         // [m] - live cascade-0 disc radius (F10 window)
    float TuneNearFieldBias;      // [-] - live near-field bias (F10 window; layout parity, unused by the gather)
    uint PushPad0;                // [-] - keep the block 16-byte aligned

    // ---- Primary sun shadow (area-sampled BVH ray; set 2) — six scalars, byte-matched by SurfaceShadeConstants ----
    float SunAngularRadius;       // [rad] - half-angle of the sun disc; 0 gives a hard shadow, ~0.0047 is the real sun (soft penumbra)
    uint  ShadowSampleCount;      // [-] - jittered rays across the disc per pixel; 1 = hard, more = smoother penumbra (denoise wants temporal at low N)
    uint  ShadowEnabled;          // [-] - 1 traces the sun-visibility gate, 0 leaves LightEnergy unshadowed (forced 0 when set 2 is not ready)
    uint  ShadowFrame;            // [-] - frame index; rotates the per-pixel sample jitter so a temporal/denoise pass can average
    uint  ShadowInstanceCount;    // [-] - TLAS leaves (TraceInstanceCount for the shadow trace)
    uint  ShadowSliceCount;       // [-] - slice table entries (TraceSliceCount for the shadow trace)
} Constants;

// The surfel gather chain — dependency order (each is a guarded #include MODULE with no main(), reading the set-1 buffers by bare name declared above):
//   SurfelGrid.glsl        cell hash + radius-for-position (the F1 signed-shift site)
//   SurfelGuiding.glsl     hemi-oct encode/decode the radial-depth module calls
//   SurfelRadialDepth.glsl the 4MSM occlusion gate
//   SurfelGather.glsl      SurfelLookupGI itself
// SurfelRecord.glsl is already pulled in above (it must precede the set-1 block). These compile INTO SurfaceShade.frag.spv via glslc -I.
#include "SurfelGrid.glsl"
#include "SurfelGuiding.glsl"
#include "SurfelRadialDepth.glsl"
#include "SurfelGather.glsl"

//------------------------------------------------------------------------------------------------------------------------
//                                     SET 2 — THE BVH (area-sampled sun shadow ray)
//------------------------------------------------------------------------------------------------------------------------

// 🧩 The primary/direct sun shadow. The reference composites a CSM-shadowed scene pass (three.js CSM, PCF-filtered) over the surfel
//    GI; the surfel integrate already carries its OWN secondary sun-shadow ray, but nothing here gated the DIRECT sun in the shade —
//    so the port rendered unshadowed. Rather than rebuild a map-atlas shadow system, this traces N jittered rays across the sun's
//    angular disc through the SAME two-level BVH the surfel integrate uses; the averaged visibility ∈ [0,1] gives a true penumbra
//    (physically-accurate contact hardening, better than a PCF blur) and multiplies LightEnergy below.
//
// 🔴 A SELF-CONTAINED THIRD SET, spelled to satisfy TwoLevelTrace.glsl's includer contract. Set 0 already carries the instance SSBO
//    (b3, std140) and the merged vertex/index streams (b1/b2) the BVH was built over, so those are REUSED — the trace's Instances[]
//    and MeshIndices[] resolve to set 0's Instances/Indices via the #defines, and PositionForVertex reads set 0's Vertices. Only the
//    five acceleration buffers the shade never had — Slices / ArenaNodeWords / ArenaPrimitives / TreeNodeWords — live in set 2.
//    All BORROWED (GeometryArena + InstanceTree); best-effort on the host, so a set-2 build failure forces ShadowEnabled=0 and the
//    shade still runs unshadowed rather than reading undefined memory.

// Mirrors GeometryArenaSlice (Acceleration/GeometryArenaSubmission.h), pinned at 32 B — same as SurfelIntegrate.comp's set 0 b1.
struct GeometryArenaSlice
{
    uint NodeOffset;
    uint NodeCount;
    uint PrimitiveOffset;
    uint PrimitiveCount;
    uint VertexOffset;
    uint IndexOffset;
    uint ParentOffset;
    uint Padding;
};

layout(std430, set = 2, binding = 0) readonly buffer TraceSliceBlock     { GeometryArenaSlice TraceSlices[]; };
layout(std430, set = 2, binding = 1) readonly buffer TraceArenaNodeBlock { uint TraceArenaNodeWords[]; };
layout(std430, set = 2, binding = 2) readonly buffer TraceArenaPrimBlock { uint TraceArenaPrimitives[]; };
layout(std430, set = 2, binding = 3) readonly buffer TraceTreeNodeBlock  { uint TraceTreeNodeWords[]; };

// TwoLevelTrace.glsl reads bare names; alias its contract onto the buffers above and the set-0 streams it shares with the raster.
#define Slices          TraceSlices
#define ArenaNodeWords  TraceArenaNodeWords
#define ArenaPrimitives TraceArenaPrimitives
#define TreeNodeWords   TraceTreeNodeWords
#define MeshIndices     Indices
#define TraceInstanceCount Constants.ShadowInstanceCount
#define TraceSliceCount    Constants.ShadowSliceCount

//------------------------------------------------------------------------------------------------------------------------
//                                                     RECONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

vec3 PositionForVertex(uint VertexIndex)
{
    RenderVertex Vertex = Vertices[VertexIndex];
    return vec3(Vertex.PositionX, Vertex.PositionY, Vertex.PositionZ);
}

// The two-level ray walk — pulled in AFTER its includer contract (set-2 buffers + the #define aliases + PositionForVertex + the two
// count aliases) is all in scope. Compiles into SurfaceShade.frag.spv via glslc -I, same as the surfel gather modules above.
#include "TwoLevelTrace.glsl"

// The floor's counterpart. A separate reader rather than a buffer parameter because GLSL cannot pass an SSBO block as an argument — the two
// otherwise-identical bodies are the language's price for two storage blocks, not duplication that could be factored away.
vec3 FloorPositionForVertex(uint VertexIndex)
{
    RenderVertex Vertex = FloorVertices[VertexIndex];
    return vec3(Vertex.PositionX, Vertex.PositionY, Vertex.PositionZ);
}

// This pixel's world-space position on the near plane, unprojected through the inverse view-projection. Subtracting the eye gives
// the view ray. Vulkan NDC is x,y in [-1,1] with y DOWN and z in [0,1]; the fullscreen triangle's [0,2] UV maps straight to it.
vec3 UnprojectPixel(vec2 Ndc, float NdcDepth)
{
    vec4 Clip  = Constants.InverseViewProjection * vec4(Ndc, NdcDepth, 1.0);
    return Clip.xyz / Clip.w;
}

// Barycentric weights of the point where the ray (Origin, Direction) meets the triangle — the standard Möller-Trumbore solve,
// returning (w0, w1, w2) that sum to 1. Degenerate / parallel cases fall back to the first corner, which reads as a face normal
// for that pixel rather than a NaN.
vec3 RayTriangleBarycentrics(vec3 Origin, vec3 Direction, vec3 Corner0, vec3 Corner1, vec3 Corner2)
{
    vec3  Edge1  = Corner1 - Corner0;
    vec3  Edge2  = Corner2 - Corner0;
    vec3  PVec   = cross(Direction, Edge2);
    float Determinant = dot(Edge1, PVec);
    if (abs(Determinant) < 1e-12)
        return vec3(1.0, 0.0, 0.0);

    float InverseDeterminant = 1.0 / Determinant;
    vec3  TVec = Origin - Corner0;
    float Bary1 = dot(TVec, PVec) * InverseDeterminant;
    vec3  QVec  = cross(TVec, Edge1);
    float Bary2 = dot(Direction, QVec) * InverseDeterminant;
    return vec3(1.0 - Bary1 - Bary2, Bary1, Bary2);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        BRDF CORE
//------------------------------------------------------------------------------------------------------------------------

// GGX / Trowbridge-Reitz normal distribution (Filament's form, written to stay finite at alpha -> 0).
float DistributionGgx(float NoH, float Alpha)
{
    float A = NoH * Alpha;
    float K = Alpha / max(1.0 - NoH * NoH + A * A, 1e-8);
    return K * K * (1.0 / Pi);
}

// Height-correlated Smith visibility — the V term with the 1/(4 NoL NoV) already folded in.
float VisibilitySmithGgxCorrelated(float NoV, float NoL, float Alpha)
{
    float A2 = Alpha * Alpha;
    float LambdaV = NoL * sqrt(NoV * NoV * (1.0 - A2) + A2);
    float LambdaL = NoV * sqrt(NoL * NoL * (1.0 - A2) + A2);
    return 0.5 / max(LambdaV + LambdaL, 1e-8);
}

vec3 FresnelSchlick(vec3 F0, float VoH)
{
    float Fc = pow(clamp(1.0 - VoH, 0.0, 1.0), 5.0);
    return F0 + (vec3(1.0) - F0) * Fc;
}

float FresnelSchlickScalar(float F0, float VoH)
{
    float Fc = pow(clamp(1.0 - VoH, 0.0, 1.0), 5.0);
    return F0 + (1.0 - F0) * Fc;
}

// Estevez & Kulla's Charlie distribution — the cloth sheen lobe. Its inverted exponent gives the bright grazing rim that GGX
// cannot produce, which is the entire visual signature of velvet.
float DistributionCharlie(float NoH, float SheenRoughness)
{
    float Alpha    = max(SheenRoughness, MinimumRoughness);
    float InverseAlpha = 1.0 / Alpha;
    float Cos2     = NoH * NoH;
    float Sin2     = max(1.0 - Cos2, 1e-4);
    return (2.0 + InverseAlpha) * pow(Sin2, InverseAlpha * 0.5) / (2.0 * Pi);
}

// Kelemen visibility — the cheap V term Filament pairs with the clear-coat lobe.
float VisibilityKelemen(float LoH)
{
    return 0.25 / max(LoH * LoH, 1e-4);
}

// A cheap thin-film hue sweep standing in for true iridescence: the ramp REPLACES the Fresnel term rather than tinting it, so the
// hue travels with view angle the way a soap film does. Real thin-film interference needs a spectral integral; this keeps the
// characteristic motion at a fraction of the cost, which is the documented approximation for this slice.
vec3 IridescenceRamp(float NoV, float FilmIor, float ThicknessNanometres)
{
    // Optical path difference, normalized into a hue phase. Thickness is in nm; the 550 divisor centres the sweep on green.
    float Phase = (2.0 * FilmIor * ThicknessNanometres * max(NoV, 1e-3)) / 550.0;
    vec3  Hue   = 0.5 + 0.5 * cos(6.28318 * Phase + vec3(0.0, 2.0944, 4.1888));
    // Grazing angles push toward white, as a real film's higher-order fringes wash out.
    float Grazing = pow(1.0 - clamp(NoV, 0.0, 1.0), 3.0);
    return mix(Hue, vec3(1.0), Grazing);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TONEMAP + TRANSFER
//------------------------------------------------------------------------------------------------------------------------

// 🔴 THERE IS NO TONEMAP IN THIS SHADER, AND NONE MAY BE ADDED. This pass writes LINEAR SCENE RADIANCE, unbounded above 1.0, into the
//    linear HDR radiance target; `RadianceResolve.frag` is the single site that applies exposure and the Khronos PBR Neutral curve.
//    The operator that used to close this function was removed in the P5.9b completion pass — it was still running against the
//    radiance target, so every scene pixel was mapped TWICE (here, then again at the resolve) and the shader's own header already
//    claimed otherwise. A doubly-compressed signal cannot be recovered downstream: the second curve compresses an already-compressed
//    peak, which flattens and desaturates exactly the metal / clearcoat highlights the first curve existed to protect.
// ⚠️ Do not "fix" a clipped or washed highlight by reintroducing a curve here. If highlights clip, the fault is upstream (exposure,
//    light intensity) or downstream (the resolve) — never a second operator in this shader.
//
// 📝 No sRGB encode here either. The swapchain is _SRGB, so the presentation hardware applies the exact piecewise sRGB OETF on write.
//    The removed pow(x, 1/2.2) was a pure-power approximation that missed the real curve's linear toe by up to ~6% in the low end,
//    exactly where AmbientColour (0.10–0.16) sits.
// ⚠️ Do not reintroduce a gamma encode. Against an _SRGB target it double-encodes and the whole image washes out.

// 🔴 THERE IS NO TONE-MAP INVERSE IN THIS SHADER EITHER, AND NONE MAY BE ADDED. An `InvertPbrNeutral` used to live here, called only by the
//    matcap branch, to pre-cancel the resolve's forward curve so a display-referred matcap reached the screen unaltered. It was REMOVED on
//    2026-07-30 because it is not merely approximate — it fails catastrophically, and the failure is structural rather than a tuning problem.
//
//    🔴 THE DEFECT: PBR Neutral's display peak APPROACHES 1.0 ASYMPTOTICALLY AND NEVER ATTAINS IT.
//         forward: NewPeak = 1 - D²/(Peak + D - StartCompression)  ->  Peak=1 gives 0.880, Peak=5 gives 0.987, Peak=1e6 gives 0.99999994
//       So a display value near 1.0 has no finite linear pre-image, and the inverse's `D²/(1 - NewPeak)` term explodes: display peak 0.999
//       demands linear 58, and display peak 1.0 demands ~5760. The `max(1.0 - NewPeak, 1e-5)` guard bounds the DIVISOR but not the RESULT,
//       so every channel saturates. Measured over a 9261-sample sweep of the unit cube: a saturated display (1.0, 0.5, 0.0) round-tripped to
//       (1.0, 1.0, 1.0) — PURE WHITE, 255 code values of error. A saturated matcap rendered white.
//
//    📝 Measured round-trip error by display peak (forward(inverse(x)) vs x, worst channel, 8-bit codes):
//         0.04..0.75  ->  0.000   (exact: the curve is a flat -0.04 offset there, so the inverse is a flat +0.04)
//         0.775       ->  2.6     (past the knee, diverging)
//         0.90        ->  7.6
//         0.95        ->  45.8
//         1.00        ->  255.0   (total failure)
//       Clamping the input to the knee was evaluated and rejected: it caps a matcap at 0.76 display (white reads ~61 codes dark) and still
//       drifts hue by 0.167 on out-of-range colours. A per-pixel display-referred flag was also rejected for now — OutColour.a is NOT free
//       (the shade pass runs blendEnable=TRUE with SRC_ALPHA and the glass path writes a real alpha at the bottom of main), so it would need
//       a second attachment or a stencil bit.
//
//    ✔️ THE FIX IS TO STOP FIGHTING THE TONE MAP: the matcap is now authored as LINEAR RADIANCE like every other surface and is tone-mapped
//       by the resolve along with them. Zero round-trip error because there is no round trip, no blow-up, no special case, and less ALU. The
//       trade is that a matcap now reads as a lit material rather than an exactly-preserved authored image — a LOOK change, accepted
//       deliberately (user ruling, 2026-07-30), and the correct trade against rendering saturated colours as white.

//------------------------------------------------------------------------------------------------------------------------
//                                                          SHADE
//------------------------------------------------------------------------------------------------------------------------

// One analytic key light plus a constant ambient fill. A single light is enough to read every material's signature, and the fill
// keeps the shadowed side from going pure black on an LDR target where crushed blacks band badly.
// 📝 The key light's radiance now rides Constants.SunRadiance (colour x intensity, premultiplied host-side from the F10 Sun card), so the
//    hardcoded pair is retired. These two survive ONLY as the fallback the body picks when the host pushes a zero radiance (older constant path).
const vec3  LightColourDefault    = vec3(1.0, 0.98, 0.95);
const float LightIntensityDefault = 3.0;
const vec3  AmbientColour  = vec3(0.10, 0.12, 0.16);

//------------------------------------------------------------------------------------------------------------------------
//                                              AREA-SAMPLED SUN SHADOW
//------------------------------------------------------------------------------------------------------------------------

// A cheap per-pixel hash for the sample jitter — the same integer-scramble the surfel passes lean on, so no blue-noise texture
// is needed. Feeds a rotated Fibonacci disc below; the frame index folds in so a temporal pass sees a fresh sequence each frame.
float ShadowHash(uint Seed)
{
    Seed ^= Seed >> 17u;
    Seed *= 0xED5AD4BBu;
    Seed ^= Seed >> 11u;
    Seed *= 0xAC4C1B51u;
    Seed ^= Seed >> 15u;
    Seed *= 0x31848BABu;
    Seed ^= Seed >> 14u;
    return float(Seed) * (1.0 / 4294967296.0);
}

// Build an orthonormal basis around a direction — Duff et al.'s branchless frisvad, for spreading the shadow taps across the disc.
void ShadowBasis(vec3 N, out vec3 T, out vec3 B)
{
    float Sign = N.z >= 0.0 ? 1.0 : -1.0;
    float A = -1.0 / (Sign + N.z);
    float C = N.x * N.y * A;
    T = vec3(1.0 + Sign * N.x * N.x * A, Sign * C, -Sign * N.x);
    B = vec3(C, Sign + N.y * N.y * A, -N.y);
}

// Averaged sun visibility ∈ [0,1] at a surface point: ShadowSampleCount rays spread across the sun's angular disc, each traced
// through the two-level BVH; the fraction that reach the sky is the soft-shadow term. SunAngularRadius = 0 collapses to one hard
// ray. The origin is nudged along the geometric normal by a scale-relative epsilon so a grazing ray does not self-intersect.
float SunVisibility(vec3 SurfacePoint, vec3 GeometricNormal, vec3 SunDirection)
{
    uint  Samples = max(Constants.ShadowSampleCount, 1u);
    float Epsilon = 1e-3 * (1.0 + length(SurfacePoint));
    vec3  Origin  = SurfacePoint + GeometricNormal * Epsilon;

    vec3 Tangent;
    vec3 Bitangent;
    ShadowBasis(SunDirection, Tangent, Bitangent);

    // A per-pixel rotation so neighbouring pixels sample different disc points — the Fibonacci spiral is otherwise identical per pixel.
    uint  PixelSeed = uint(gl_FragCoord.x) * 1973u + uint(gl_FragCoord.y) * 9277u + Constants.ShadowFrame * 26699u;
    float Rotation  = ShadowHash(PixelSeed) * 6.28318530718;

    const float GoldenAngle = 2.39996322973;
    float Visible = 0.0;
    for (uint Index = 0u; Index < Samples; ++Index)
    {
        // Concentric Fibonacci disc: radius = sqrt(stratified index), angle = golden angle * index + per-pixel rotation.
        float Fraction = (float(Index) + 0.5) / float(Samples);
        float Radius   = sqrt(Fraction) * Constants.SunAngularRadius;
        float Angle    = float(Index) * GoldenAngle + Rotation;
        vec3  Direction = normalize(SunDirection + (cos(Angle) * Tangent + sin(Angle) * Bitangent) * Radius);

        TraceHit Hit = TraceTwoLevel(Origin, Direction, 1e-4, 1e4);
        if (!Hit.HitCondition)
            Visible += 1.0;
    }
    return Visible / float(Samples);
}

void main()
{
    ivec2 Texel    = ivec2(gl_FragCoord.xy);
    uint  Identity = texelFetch(VisibilityBuffer, Texel, 0).r;
    if (Identity == VisibilitySentinel)
        discard;   // no surface here — keep the sky / grid behind us

    uint Partition = Identity >> PrimitiveBits;
    uint Primitive = Identity & PrimitiveMask;

    // The floor mesh writes into the SAME visibility buffer from a partition range based high (FloorPartitionBase), because a
    // shared buffer needs disjoint ranges or "partition 0" is ambiguous. Its geometry lives in its own vertex / index / instance
    // buffers (b5-b7), so the fetch below picks the pair by range rather than assuming the heads'.
    // 🔴 The partition ordinal must be REBASED for the floor: identities were written at FloorPartitionBase + local ordinal, so
    //    indexing FloorInstances by the raw value would run far off the end of a one-element array.
    const bool FloorSurface = Partition >= Constants.FloorPartitionBase;

    // ⚠️ Discarding an unshadeable floor pixel (rather than shading it wrong) is what keeps the pass honest when the floor buffers
    //    are the head-buffer alias: the sky stays visible there, exactly as before this pass learned to shade the floor at all.
    if (FloorSurface && Constants.FloorShadeEnabled == 0u)
        discard;

    SceneInstance Instance;
    vec3 World0;
    vec3 World1;
    vec3 World2;

    if (FloorSurface)
    {
        uint FloorPartition = Partition - Constants.FloorPartitionBase;
        if (FloorPartition >= uint(FloorInstances.length()))
            discard;
        Instance = FloorInstances[FloorPartition];

        // 🔴 The floor's primitive ordinal is DRAW-LOCAL (gl_PrimitiveID restarts at 0 for the floor's own vkCmdDrawIndexed) while b6 is the merged
        //    index buffer the heads also live in. FloorIndexBase converts one to the other. Without it every floor pixel reconstructs from the heads'
        //    opening triangles and shades a real surface with a borrowed normal.
        uint IndexBase = Constants.FloorIndexBase + Primitive * 3u;
        if (IndexBase + 2u >= uint(FloorIndices.length()))
            discard;
        vec3 Local0 = FloorPositionForVertex(FloorIndices[IndexBase + 0u]);
        vec3 Local1 = FloorPositionForVertex(FloorIndices[IndexBase + 1u]);
        vec3 Local2 = FloorPositionForVertex(FloorIndices[IndexBase + 2u]);
        World0 = (Instance.Model * vec4(Local0, 1.0)).xyz;
        World1 = (Instance.Model * vec4(Local1, 1.0)).xyz;
        World2 = (Instance.Model * vec4(Local2, 1.0)).xyz;
    }
    else
    {
        if (Partition >= uint(Instances.length()))
            discard;
        Instance = Instances[Partition];

        // ---- Fetch the triangle and recover barycentrics from this pixel's view ray ----
        uint IndexBase = Primitive * 3u;
        if (IndexBase + 2u >= uint(Indices.length()))
            discard;
        vec3 Local0 = PositionForVertex(Indices[IndexBase + 0u]);
        vec3 Local1 = PositionForVertex(Indices[IndexBase + 1u]);
        vec3 Local2 = PositionForVertex(Indices[IndexBase + 2u]);
        World0 = (Instance.Model * vec4(Local0, 1.0)).xyz;
        World1 = (Instance.Model * vec4(Local1, 1.0)).xyz;
        World2 = (Instance.Model * vec4(Local2, 1.0)).xyz;
    }

    // Pixel centre -> NDC. gl_FragCoord.xy ALREADY sits at the pixel centre (the .5 is built in), so adding another half pixel
    // would skew the ray by half a pixel and bias every barycentric fetch. y needs no flip: the fullscreen triangle writes the
    // same NDC it derives to gl_Position (see AnalyticGroundPlane.vert), so screen y and NDC y run the same direction here.
    vec2 BufferSize = vec2(textureSize(VisibilityBuffer, 0));
    vec2 Ndc        = (gl_FragCoord.xy / BufferSize) * 2.0 - 1.0;
    vec3 RayOrigin  = Constants.CameraPosition.xyz;
    vec3 NearPoint  = UnprojectPixel(Ndc, 0.0);
    vec3 RayDirection = normalize(NearPoint - RayOrigin);

    vec3 Bary = RayTriangleBarycentrics(RayOrigin, RayDirection, World0, World1, World2);

    vec3 WorldPosition = Bary.x * World0 + Bary.y * World1 + Bary.z * World2;

    // 🔴 GEOMETRIC (flat) normal — derived from the triangle's own plane, NOT interpolated from the vertex stream.
    //    Two reasons this is the derivation rather than a fallback:
    //      ① The authored geometry is flat by construction. Suzzane.obj carries `s 0` with 499 `vn` for 500 faces — one normal
    //         per FACE, all four corners of a face indexing the same `vn`. There is no per-vertex normal to interpolate; the
    //         faceted read IS the intended surface.
    //      ② The stride-32 vertex stream cannot carry them anyway. VertexField.Normal is indexed PER-VERTEX while OBJ normals
    //         are face-varying (v/vt/vn), so the corner->normal indirection has nowhere to live and the bake drops them —
    //         SuzanneMaterialRings.wsdoc records `normals = []`. Reading the stream's normal here would therefore yield
    //         (0,0,0), which normalizes to NaN and kills the entire light loop: every lobe collapses to the ambient fill,
    //         which reads as unlit flat colour rather than as flat SHADING.
    //    The cross product costs nothing new: World0..World2 are already resolved above for the barycentric solve.
    // ⚠️ Winding sets the sign, so the result is normalized but NOT yet oriented — the two-sided flip below settles that. Uses
    //    the WORLD corners, so the instance transform is already applied and NormalBasis must NOT be applied again.
    vec3 Normal = normalize(cross(World1 - World0, World2 - World0));

    vec3 ViewVector = normalize(RayOrigin - WorldPosition);
    // Two-sided: the reference mesh has open boundaries, so a back-facing normal would otherwise shade pure black.
    if (dot(Normal, ViewVector) < 0.0)
        Normal = -Normal;

    // ---- Resolve the material ----
    uint MaterialId = min(Instance.MaterialId, 13u);
    SurfacePreset Preset = Materials[MaterialId];

    uint ShadingModel = Preset.ModelFeature.x;
    uint FeatureMask  = Preset.ModelFeature.y;
    // Composite reads its mask from the push constant so lobes toggle live with no table re-upload.
    if (ShadingModel == ModelComposite)
        FeatureMask = Constants.CompositeFeatureMask;

    vec3  BaseColour  = Preset.BaseColour.rgb;
    float Alpha       = Preset.BaseColour.a;
    float Roughness   = max(Preset.RoughnessMetallic.x, MinimumRoughness);
    float Metallic    = Preset.RoughnessMetallic.y;
    float Reflectance = Preset.RoughnessMetallic.z;

    // ---- Matcap bypasses the light loop entirely ----
    // A matcap skips the BRDF: its ramp IS its shading model, so there is no light loop to run. What it does NOT do any more is skip the
    // tone map.
    // 📝 The studio ramp is authored as LINEAR RADIANCE and written straight into the linear target, so the resolve maps it exactly as it
    //    maps every other surface. That is why this branch is now three lines: an earlier version treated the ramp as a display value and
    //    pre-inverted the resolve's curve to cancel it, which rendered saturated matcaps PURE WHITE (255 codes of error). See the 🔴 block
    //    where that inverse used to live for the measurement and for why no inverse belongs in this shader.
    // ⚠️ Do NOT clamp the ramp to 0..1 here. It is radiance now, not a display value — values above 1.0 are legitimate and the resolve's
    //    shoulder is what rolls them off. The old clamp existed only to keep the inverse inside its (very narrow) valid domain.
    if ((FeatureMask & FeatureMatcap) != 0u)
    {
        // No sphere texture is bound yet, so the UV lookup a real matcap would do is not computed here — the studio ramp below
        // stands in for it. When the texture lands, the normal must first be taken to VIEW space (a matcap is indexed by the
        // view-space normal, which is what makes the lighting stick to the camera); world space would rotate the highlight.
        float Key   = pow(clamp(dot(Normal, normalize(vec3(0.4, 0.3, 0.9))), 0.0, 1.0), 2.0);
        float Rim   = pow(1.0 - clamp(dot(Normal, ViewVector), 0.0, 1.0), 2.5);
        vec3  Studio = BaseColour * (0.35 + 0.75 * Key) + vec3(0.9) * Rim * 0.35;
        OutColour = vec4(max(Studio, vec3(0.0)), 1.0);
        return;
    }

    // ---- f0 / diffuse split ----
    // Dielectrics take a monochrome f0 from Reflectance (f0 = 0.16 * r^2, the canonical 4% at r = 0.5); conductors take f0 from
    // the base colour and have NO diffuse lobe at all. Metallic selects between them.
    vec3 DielectricF0 = vec3(0.16 * Reflectance * Reflectance);
    vec3 F0           = mix(DielectricF0, BaseColour, Metallic);
    vec3 DiffuseColour = BaseColour * (1.0 - Metallic);

    vec3  LightVector = normalize(Constants.LightDirection.xyz);
    vec3  HalfVector  = normalize(LightVector + ViewVector);
    float NoV = clamp(abs(dot(Normal, ViewVector)) + 1e-5, 0.0, 1.0);
    float NoL = clamp(dot(Normal, LightVector), 0.0, 1.0);
    float NoH = clamp(dot(Normal, HalfVector), 0.0, 1.0);
    float VoH = clamp(dot(ViewVector, HalfVector), 0.0, 1.0);
    float LoH = clamp(dot(LightVector, HalfVector), 0.0, 1.0);

    float Alpha2 = Roughness * Roughness;
    vec3  Radiance = vec3(0.0);

    // ---- Key light energy ----
    // Every sun-driven lobe below — diffuse, wrapped subsurface, specular, sheen, coat — is scaled by LightEnergy. The ambient fill uses AmbientColour
    // instead, so it stands in for sky and bounce and is deliberately left untouched by the key light's direction.
    // 📝 LightEnergy is the host-pushed key radiance (F10 Sun card, colour x intensity premultiplied). A zero push (older constant path / uninitialised)
    //    falls back to the retired hardcoded pair so the shade never goes black on a caller that has not wired the Sun card yet.
    vec3  LightEnergy = (dot(Constants.SunRadiance.rgb, vec3(1.0)) > 0.0)
                        ? Constants.SunRadiance.rgb
                        : LightColourDefault * LightIntensityDefault;

    // ---- Primary sun shadow (area-sampled BVH) ----
    // The DIRECT sun's visibility gate — the piece the port was missing (the surfel integrate already self-shadows the indirect bounce). Averaged
    // across the sun's angular disc it is a real penumbra, so it folds into LightEnergy BEFORE the lobes and every direct term darkens together. Skip
    // the trace entirely when the ray would face away (NoL == 0) — a back-facing point is already unlit, so its N taps are pure cost. Set 2 not ready
    // ⇒ the host forces ShadowEnabled=0 and this whole block is a no-op, leaving the pre-shadow look intact.
    if (Constants.ShadowEnabled != 0u && NoL > 0.0)
        LightEnergy *= SunVisibility(WorldPosition, Normal, LightVector);

    // ---- Diffuse ----
    if ((FeatureMask & FeatureDiffuse) != 0u)
    {
        if ((FeatureMask & FeatureSubsurface) != 0u)
        {
            // Wrapped diffuse: shift the lambert term so light bleeds past the terminator, standing in for subsurface scatter.
            // The wrap ALREADY carries the cosine falloff, so it must NOT be multiplied by NoL again — doing so reintroduces the
            // hard terminator the wrap exists to remove.
            const float WrapAmount = 0.5;
            float Wrapped = clamp((dot(Normal, LightVector) + WrapAmount) / ((1.0 + WrapAmount) * (1.0 + WrapAmount)), 0.0, 1.0);
            // Tint the scattered light warm — the shallow red bleed that makes skin read as flesh rather than painted plastic.
            vec3 ScatterTint = mix(vec3(1.0), vec3(1.0, 0.45, 0.35), 0.6);
            Radiance += DiffuseColour * ScatterTint * Wrapped * LightEnergy * (1.0 / Pi);
        }
        else
        {
            Radiance += DiffuseColour * (1.0 / Pi) * NoL * LightEnergy;
        }
    }

    // ---- Specular ----
    if ((FeatureMask & FeatureSpecular) != 0u)
    {
        float D = DistributionGgx(NoH, Alpha2);
        float V = VisibilitySmithGgxCorrelated(NoV, NoL, Alpha2);
        vec3  F;
        if ((FeatureMask & FeatureIridescence) != 0u)
        {
            // The thin-film ramp REPLACES Fresnel (it is not layered over it), then is scaled by the surface's own f0 so a metal
            // base still reads as metal. Filament's note applies: an iridescent metal falls back to Schlick-IOR behaviour.
            vec3 Film = IridescenceRamp(NoV, Preset.CoatIridescence.z, Preset.CoatIridescence.w);
            F = Film * FresnelSchlick(F0, VoH);
        }
        else
        {
            F = FresnelSchlick(F0, VoH);
        }
        Radiance += D * V * F * NoL * LightEnergy;
    }

    // ---- Cloth sheen ----
    if ((FeatureMask & FeatureSheen) != 0u)
    {
        // Charlie + a Fresnel-FREE visibility: Filament removes Fresnel from cloth entirely, and the sheen tint takes its place.
        float D = DistributionCharlie(NoH, Preset.SheenColourRoughness.w);
        float V = 1.0 / (4.0 * (NoL + NoV - NoL * NoV) + 1e-4);
        Radiance += Preset.SheenColourRoughness.rgb * D * V * NoL * LightEnergy;
    }

    // ---- Clear coat ----
    // A second, tighter GGX lobe over the base, plus the energy the coat takes away from what is underneath. The coat's own
    // Fresnel is fixed at the 4% of a polyurethane layer (f0 = 0.04), independent of the base material.
    if ((FeatureMask & FeatureCoat) != 0u)
    {
        float CoatWeight    = Preset.CoatIridescence.x;
        float CoatRoughness = max(Preset.CoatIridescence.y, MinimumRoughness);
        float CoatAlpha     = CoatRoughness * CoatRoughness;
        float Dc = DistributionGgx(NoH, CoatAlpha);
        float Vc = VisibilityKelemen(LoH);
        float Fc = FresnelSchlickScalar(0.04, LoH) * CoatWeight;
        // Attenuate what is already there by the light the coat reflected away, then add the coat's own lobe.
        Radiance *= (1.0 - Fc);
        Radiance += Dc * Vc * Fc * NoL * LightEnergy;
    }

    // ---- Ambient fill / surfel GI ----
    // The indirect term catches on the same albedo either way: metals take it tinted by f0 (they have no diffuse albedo to catch it
    // with), dielectrics take it on their diffuse colour.
    vec3 AmbientAlbedo = mix(DiffuseColour, F0, Metallic);
    // 🧩 Phase 3 seam: FULLY REPLACE the flat ambient with a one-bounce surfel-cache gather when GI is on (user ruling). SurfelLookupGI walks the
    //    grid cell at WorldPosition, blends nearby surfels' stored irradiance by radius/normal/occlusion weight, and returns the indirect radiance.
    //    The toggle keeps the pre-Phase-3 flat-ambient look one keypress away for a direct A/B. ReadOffsetElements is the POST-SWAP read half.
    if (Constants.SurfelGiEnabled != 0u)
    {
        // Seat the live world-scale globals before the gather (whole-pipeline reach) — the gather hashes WorldPosition into the SAME cells the
        // spawn+slotting build scattered surfels into, so its cell diameter + radius must track the F10 sliders in lockstep.
        SurfelSetTuning(Constants.TuneCellDiameter, Constants.TuneBaseRadius, Constants.TuneNearFieldBias);
        vec3 Gi = SurfelLookupGI(WorldPosition, Normal, Constants.CameraPosition.xyz, Constants.GridOrigin.xyz,
                                 Constants.SurfelReadOffsetElements, Constants.OcclusionParams);
        Radiance += Gi * AmbientAlbedo;
    }
    else
    {
        Radiance += AmbientColour * AmbientAlbedo;
    }

    // ---- Emissive ----
    // Enters BELOW the coat (an LED under a lacquer layer is dimmed by it), which is why this sits after the coat attenuation.
    if ((FeatureMask & FeatureEmissive) != 0u)
        Radiance += Preset.EmissiveColour.rgb * Preset.EmissiveColour.a;

    // ---- Glass ----
    // No refraction this slice: with no environment map and no scene colour copy, IBL-only refraction reads worse than a plain
    // Fresnel rim over alpha blending (Khronos is explicit about this). Alpha carries the transparency; the rim brightens toward
    // grazing angles, and the alpha rises with it so the silhouette stays visible where the facing surface is nearly clear.
    float OutputAlpha = 1.0;
    if ((FeatureMask & FeatureTransmission) != 0u)
    {
        // Rim tint tracks the key light's HUE (normalised so intensity does not double-count with the rim's own 0.6 weight); falls back to the
        // default warm-white when the host pushes no radiance.
        float Rim = pow(1.0 - NoV, 3.0);
        float SunLuma = max(dot(Constants.SunRadiance.rgb, vec3(0.2126, 0.7152, 0.0722)), 1e-4);
        vec3  SunHue  = (dot(Constants.SunRadiance.rgb, vec3(1.0)) > 0.0) ? Constants.SunRadiance.rgb / SunLuma : LightColourDefault;
        Radiance += vec3(Rim) * 0.6 * SunHue;
        OutputAlpha = clamp(Alpha + Rim, 0.0, 1.0);
    }

    // Linear radiance out, unbounded above 1.0 — the resolve owns exposure + the tone curve, the _SRGB swapchain owns the transfer
    // function. See the 🔴 note where the operator used to live for why no curve may be applied here.
    OutColour = vec4(Radiance, OutputAlpha);
}
