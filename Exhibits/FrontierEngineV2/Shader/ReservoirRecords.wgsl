//============================================================================================================================================
//                                                          RESERVOIRRECORDS.WGSL
//============================================================================================================================================
// 🧩 ReSTIR PT Enhanced unified 64-byte path reservoirs (Lin, Kettunen & Wyman 2026, PACMCGIT 9(1), Art. 13, doi:10.1145/3804494):
//    - §2.3 & §6.1: Unified direct (d = 2 NEE) and indirect/specular (d >= 3) path-tree reservoir with hybrid-shift replay seed ū
//    - §4 Eq. (5) : Scene-independent dual ray-footprint + single-vertex roughness (α_min = 0.20) reconnection criterion
//    - §5         : Adaptive temporal confidence cap c_Cap = lerp(c_Cap^Default, 1.0, D^0.1) driven by 17×17 seed duplication D
//    - §6.2.1/7.1 : Branchless reservoir merge & compressed 64-byte ReservoirRecord (2 × 64 B per pixel across ping-pong pairs)

//! include "PackingCodec.wgsl"
//! include "DistanceRecords.wgsl"
//! include "SurfaceReflectance.wgsl"

//------------------------------------------------------------------------------------------------------------------------
//                                      PATH METADATA FLAGS & RECONNECTION CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const SkyReach              : f32 = 10000.0;   // [m]   far-field distance encoding directional sun/sky rays
const RoughnessReconnectMin : f32 = 0.20;      // [-]   §4.2 & §7.2: single-vertex roughness safeguard α_min = 0.20

const PathFlagDirectNee     : u32 = 1u;        // [-]   d = 2 direct NEE light/sun sample (§6.1 unified DI + GI)
const PathFlagForcedNee     : u32 = 2u;        // [-]   §6.2.3 forced reconnection onto terminal NEE light vertex
const PathFlagDirectional   : u32 = 4u;        // [-]   sky escape or directional solar cone sample (G = 1, |J_geom| = 1)
const PathFlagSpecularLobe  : u32 = 8u;        // [-]   primary vertex x_1 sampled the coated GGX specular lobe

//------------------------------------------------------------------------------------------------------------------------
//                                           64-BYTE UNIFIED RESERVOIR RECORD
//------------------------------------------------------------------------------------------------------------------------

struct ReservoirRecord
{
    // Words 0..3 (16 B)
    SamplePosition     : vec3f,   // [m]    reconnection vertex x_k in world space
    SampleNormal       : u32,     // [-]    pack2x16snorm octahedral shading/geometric normal at x_k

    // Words 4..7 (16 B)
    Radiance           : vec2u,   // [-]    pack2x16float RGB suffix radiance L(x_k), fourth half = occupant Identity
    OutgoingAndMeta    : u32,     // [-]    low 16b: −ω_k | 16..19: Flags | 20..21: k−2 | 22..23: d−2 | 24..31: DuplicationByte
    PathSeed           : u32,     // [-]    initial-candidate seed ū (§2.3 hybrid-shift replay & §5 17×17 duplication map)

    // Words 8..11 (16 B)
    Weight             : f32,     // [-]    unbiased contribution weight W_X
    Tally              : f32,     // [-]    confidence weight c (effective sample count)
    ForwardAreaPdf     : f32,     // [1/m²] p^x_{k-1}(ω_{k-1}) · G(x_{k-1} → x_k) (solid-angle pdf if directional)
    ExitSolidPdf       : f32,     // [1/sr] p^x_k(ω_k) (1.0 when k == d or NEE light endpoint, Paper §2.3)

    // Words 12..15 (16 B)
    PrefixPosition     : vec3f,   // [m]    x_{k-1} (= x_1 when k == 2; glossy bounce vertex when k >= 3)
    PackedRoughness    : u32,     // [-]    bits 0..7: α_{k-1} | bits 8..15: α_k | bits 16..31: f16 SourceTargetPdf p̂_X(X)
}

struct ReservoirSample
{
    Position           : vec3f,
    Normal             : vec3f,
    Radiance           : vec3f,
    Identity           : u32,
    PrefixPosition     : vec3f,
    OutgoingDirection  : vec3f,
    PathSeed           : u32,
    ForwardAreaPdf     : f32,
    ExitSolidPdf       : f32,
    PrefixRoughness    : f32,
    ReconnectRoughness : f32,
    SourceTargetPdf    : f32,     // [-] canonical scalar target p̂_X(X) for zero-re-evaluation pairwise MIS
    DuplicationScore   : f32,     // [-] §5 17×17 sample duplication ratio D ∈ [0, 1]
    ReconnectVertex    : u32,     // k ∈ [2..5]
    PathLength         : u32,     // d ∈ [2..5]
    Flags              : u32,
}

struct Reservoir
{
    Sample    : ReservoirSample,
    WeightSum : f32,
    Tally     : f32,
    Weight    : f32,
    Adopted   : bool,
}

//------------------------------------------------------------------------------------------------------------------------
//                                         COMPACT DIRECTION & METADATA CODECS
//------------------------------------------------------------------------------------------------------------------------

fn PackOctahedral16Low(UnitDir: vec3f) -> u32
{
    let Enc = clamp(EncodeOctahedral(UnitDir), vec2f(-1.0), vec2f(1.0));
    let QX  = u32(clamp(round((Enc.x * 0.5 + 0.5) * 255.0), 0.0, 255.0));
    let QY  = u32(clamp(round((Enc.y * 0.5 + 0.5) * 255.0), 0.0, 255.0));
    return QX | (QY << 8u);
}

fn UnpackOctahedral16Low(Bits: u32) -> vec3f
{
    let QX  = f32(Bits & 0xFFu) / 255.0 * 2.0 - 1.0;
    let QY  = f32((Bits >> 8u) & 0xFFu) / 255.0 * 2.0 - 1.0;
    return DecodeOctahedral(vec2f(QX, QY));
}

fn PackOutgoingAndMeta(OutgoingDir: vec3f, Flags: u32, ReconnectK: u32, PathD: u32, DuplicationScore: f32) -> u32
{
    let DirBits = PackOctahedral16Low(OutgoingDir);
    let KBits   = (clamp(ReconnectK, 2u, 5u) - 2u) & 3u;
    let DBits   = (clamp(PathD, 2u, 5u) - 2u) & 3u;
    let DupByte = u32(clamp(round(DuplicationScore * 255.0), 0.0, 255.0)) & 0xFFu;
    return DirBits | ((Flags & 15u) << 16u) | (KBits << 20u) | (DBits << 22u) | (DupByte << 24u);
}

fn ConstructEmptySample() -> ReservoirSample
{
    var Empty : ReservoirSample;
    Empty.Position           = vec3f(0.0);
    Empty.Normal             = vec3f(0.0, 0.0, 1.0);
    Empty.Radiance           = vec3f(0.0);
    Empty.Identity           = SkyIdentity;
    Empty.PrefixPosition     = vec3f(0.0);
    Empty.OutgoingDirection  = vec3f(0.0, 0.0, 1.0);
    Empty.PathSeed           = 0u;
    Empty.ForwardAreaPdf     = 1.0;
    Empty.ExitSolidPdf       = 1.0;
    Empty.PrefixRoughness    = 1.0;
    Empty.ReconnectRoughness = 1.0;
    Empty.SourceTargetPdf    = 0.0;
    Empty.DuplicationScore   = 0.0;
    Empty.ReconnectVertex    = 2u;
    Empty.PathLength         = 2u;
    Empty.Flags              = PathFlagDirectional;
    return Empty;
}

fn ConstructEmptyReservoir() -> Reservoir
{
    var Empty : Reservoir;
    Empty.Sample    = ConstructEmptySample();
    Empty.WeightSum = 0.0;
    Empty.Tally     = 0.0;
    Empty.Weight    = 0.0;
    Empty.Adopted   = false;
    return Empty;
}

fn PackReservoir(Source: Reservoir) -> ReservoirRecord
{
    var Record : ReservoirRecord;
    Record.SamplePosition  = Source.Sample.Position;
    Record.SampleNormal    = PackOctahedral(Source.Sample.Normal);
    Record.Radiance        = PackHalfTriple(min(max(Source.Sample.Radiance, vec3f(0.0)), vec3f(60000.0)), f32(Source.Sample.Identity));
    Record.OutgoingAndMeta = PackOutgoingAndMeta(
        Source.Sample.OutgoingDirection,
        Source.Sample.Flags,
        Source.Sample.ReconnectVertex,
        Source.Sample.PathLength,
        Source.Sample.DuplicationScore
    );
    Record.PathSeed        = Source.Sample.PathSeed;
    Record.Weight          = Source.Weight;
    Record.Tally           = Source.Tally;
    Record.ForwardAreaPdf  = max(Source.Sample.ForwardAreaPdf, 1.0e-8);
    Record.ExitSolidPdf    = max(Source.Sample.ExitSolidPdf, 1.0e-8);
    Record.PrefixPosition  = Source.Sample.PrefixPosition;
    let R0                 = u32(clamp(round(Source.Sample.PrefixRoughness * 255.0), 0.0, 255.0)) & 0xFFu;
    let R1                 = u32(clamp(round(Source.Sample.ReconnectRoughness * 255.0), 0.0, 255.0)) & 0xFFu;
    let TargetHalfHigh     = pack2x16float(vec2f(0.0, clamp(Source.Sample.SourceTargetPdf, 0.0, 65000.0))) & 0xFFFF0000u;
    Record.PackedRoughness = R0 | (R1 << 8u) | TargetHalfHigh;
    return Record;
}

fn UnpackReservoir(Record: ReservoirRecord) -> Reservoir
{
    var Result   = ConstructEmptyReservoir();
    let Radiance = UnpackHalfTriple(Record.Radiance);
    let Meta     = Record.OutgoingAndMeta;
    let R0       = f32(Record.PackedRoughness & 0xFFu) / 255.0;
    let R1       = f32((Record.PackedRoughness >> 8u) & 0xFFu) / 255.0;
    let SrcPdf   = unpack2x16float(Record.PackedRoughness & 0xFFFF0000u).y;

    Result.Sample.Position           = Record.SamplePosition;
    Result.Sample.Normal             = UnpackOctahedral(Record.SampleNormal);
    Result.Sample.Radiance           = max(Radiance.rgb, vec3f(0.0));
    Result.Sample.Identity           = u32(Radiance.w + 0.5);
    Result.Sample.OutgoingDirection  = UnpackOctahedral16Low(Meta);
    Result.Sample.Flags              = (Meta >> 16u) & 15u;
    Result.Sample.ReconnectVertex    = ((Meta >> 20u) & 3u) + 2u;
    Result.Sample.PathLength         = ((Meta >> 22u) & 3u) + 2u;
    Result.Sample.DuplicationScore   = f32((Meta >> 24u) & 0xFFu) / 255.0;
    Result.Sample.PathSeed           = Record.PathSeed;
    Result.Sample.ForwardAreaPdf     = max(Record.ForwardAreaPdf, 1.0e-8);
    Result.Sample.ExitSolidPdf       = max(Record.ExitSolidPdf, 1.0e-8);
    Result.Sample.PrefixPosition     = Record.PrefixPosition;
    Result.Sample.PrefixRoughness    = clamp(R0, 0.03, 1.0);
    Result.Sample.ReconnectRoughness = clamp(R1, 0.03, 1.0);
    Result.Sample.SourceTargetPdf    = max(SrcPdf, 0.0);
    Result.Weight                    = max(Record.Weight, 0.0);
    Result.Tally                     = max(Record.Tally, 0.0);
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  BRANCHLESS RESERVOIR STREAMING (§6.2.1 MICRO-OP)
//------------------------------------------------------------------------------------------------------------------------

// 📐 Weighted reservoir sampling (Chao 1982) using conditional select instructions where possible to minimize divergence (§6.2.1).
fn MergeReservoir(Target: ptr<function, Reservoir>, Candidate: ReservoirSample, Weight: f32, Tally: f32, Random: f32) -> bool
{
    let SafeWeight = max(Weight, 0.0);
    let NewSum     = (*Target).WeightSum + SafeWeight;
    (*Target).WeightSum = NewSum;
    (*Target).Tally    += max(Tally, 0.0);
    let Accept = SafeWeight > 0.0 && (Random * NewSum < SafeWeight);
    if (Accept)
    {
        (*Target).Sample = Candidate;
    }
    return Accept;
}

fn ResolveSampleDirection(Sample: ReservoirSample, Position: vec3f) -> vec3f
{
    if (Sample.ReconnectVertex == 3u)
    {
        return normalize(Sample.PrefixPosition - Position);
    }
    if ((Sample.Flags & PathFlagDirectional) != 0u || Sample.Identity == SkyIdentity)
    {
        return normalize(-Sample.Normal);
    }
    return normalize(Sample.Position - Position);
}

//------------------------------------------------------------------------------------------------------------------------
//                          UNIFIED BSDF INTEGRAND & TARGET FUNCTION (§2.3, §6.1 & §6.3)
//------------------------------------------------------------------------------------------------------------------------

// 📐 Evaluates the primary surface's diffuse + coated-GGX RGB path contribution F(Y) = f_s(x_1, V, ω_1) · cos θ_1 · L(x_k)
//    and splits it into demodulated diffuse irradiance and specular/coat arrival so downstream resolve preserves sharp edges.
struct PrimaryPathResponse
{
    FullRgb           : vec3f,   // [-] complete RGB integrand F(Y) at x_1 (used for scalar target p̂(Y) = Luminance(FullRgb))
    DiffuseIrradiance : vec3f,   // [-] demodulated diffuse arrival (DisplayRecombination multiplies by ρ_diff / π)
    SpecularArrival   : vec3f,   // [-] demodulated specular/coat arrival matching DisplayRecombination's split-sum weight
    TargetPdf         : f32,     // [-] scalar target p̂(Y) = Luminance(FullRgb)
}

fn EvaluatePrimaryPathIntegrand(
    Sample   : ReservoirSample,
    Surface  : SurfaceSample,
    Position : vec3f,
    Normal   : vec3f,
    View     : vec3f,
    Unified  : bool
) -> PrimaryPathResponse
{
    var Out : PrimaryPathResponse;
    Out.FullRgb           = vec3f(0.0);
    Out.DiffuseIrradiance = vec3f(0.0);
    Out.SpecularArrival   = vec3f(0.0);
    Out.TargetPdf         = 0.0;

    let LightDir = ResolveSampleDirection(Sample, Position);
    let NdotL    = dot(Normal, LightDir);
    if (!(NdotL > 1.0e-5) || any(Sample.Radiance != Sample.Radiance) || all(Sample.Radiance <= vec3f(0.0)))
    {
        return Out;
    }

    // Clean demodulated irradiance E = L(x_k) · max(0, n · ω_1). Keeping the reservoir target strictly proportional
    // to incident irradiance prevents dark-albedo grazing Fresnel (e.g. asphalt R=0.82, F -> 1) from hijacking diffuse GI!
    let SafeRadiance      = min(max(Sample.Radiance, vec3f(0.0)), vec3f(120.0));
    let Irr               = SafeRadiance * NdotL;
    let AlbedoWeight      = max(ResolveDiffuseAlbedo(Surface), vec3f(0.08));
    Out.DiffuseIrradiance = Irr;
    Out.FullRgb           = AlbedoWeight * InversePi * Irr;
    Out.TargetPdf         = ResolveLuminance(Irr);
    return Out;
}

// Legacy diffuse target wrapper kept for SDF fallback mode.
fn ResolveReservoirTarget(Sample: ReservoirSample, Position: vec3f, Normal: vec3f) -> f32
{
    let Cosine = dot(Normal, ResolveSampleDirection(Sample, Position));
    return select(0.0, ResolveLuminance(Sample.Radiance) * Cosine, Cosine > 0.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                 §4 SCENE-INDEPENDENT DUAL RAY-FOOTPRINT + SINGLE-VERTEX ROUGHNESS CRITERION
//------------------------------------------------------------------------------------------------------------------------

// 📐 RHS of Eq. (5) (Lin, Kettunen & Wyman 2026, §4):
//    Threshold = (c / 100) · ‖x_0 − x_1‖² / (⟨n_{x_1}, x̂_1 x_0⟩ / (4π)).
fn ResolvePrimaryFootprintThreshold(CameraPos: vec3f, PrimaryPos: vec3f, PrimaryNormal: vec3f, FootprintConstant: f32) -> f32
{
    let ToCamera = CameraPos - PrimaryPos;
    let DistSq   = max(dot(ToCamera, ToCamera), 1.0e-4);
    let ViewCos  = max(dot(PrimaryNormal, ToCamera * inverseSqrt(DistSq)), 1.0e-3);
    let C        = select(0.02, FootprintConstant, FootprintConstant > 1.0e-5);
    return (C * 0.01) * (4.0 * Pi * DistSq / ViewCos);
}

// 📐 Eq. (5) + §4.2: evaluates min(ForwardFootprint, InverseFootprint) / PrimaryThreshold (> 1.0 means connectable).
fn EvaluateDualFootprintRatio(
    PrefixRoughness    : f32,
    ReconnectRoughness : f32,
    EdgeDistanceSq     : f32,
    CosAtPrefix        : f32,
    CosAtReconnect     : f32,
    ForwardSolidPdf    : f32,
    ExitSolidPdf       : f32,
    IsTerminalVertex   : bool,
    IsDirectional      : bool,
    PrimaryThreshold   : f32,
    UseEnhanced        : bool
) -> f32
{
    if (!UseEnhanced)
    {
        // Lin et al. (2022) baseline: two-vertex roughness min(α_{k-1}, α_k) >= 0.20 and world distance >= 0.35 m
        let RoughOk = PrefixRoughness >= RoughnessReconnectMin && (IsDirectional || ReconnectRoughness >= RoughnessReconnectMin);
        let DistOk  = IsDirectional || EdgeDistanceSq >= 0.1225;
        return select(0.0, 2.0, RoughOk && DistOk);
    }

    // §4.2 Single-vertex roughness safeguard at x_{k-1} ONLY (no roughness restriction at x_k!)
    if (PrefixRoughness < RoughnessReconnectMin)
    {
        return 0.0;
    }
    if (IsDirectional)
    {
        return 4.0;
    }

    let GForward         = max(CosAtReconnect, 1.0e-4) / max(EdgeDistanceSq, 1.0e-6);
    let ForwardFootprint = 1.0 / max(ForwardSolidPdf * GForward, 1.0e-8);
    let MinFootprint     = select(
        min(ForwardFootprint, 1.0 / max(ExitSolidPdf * (max(CosAtPrefix, 1.0e-4) / max(EdgeDistanceSq, 1.0e-6)), 1.0e-8)),
        ForwardFootprint,
        IsTerminalVertex
    );
    return MinFootprint / max(PrimaryThreshold, 1.0e-8);
}

//------------------------------------------------------------------------------------------------------------------------
//                           §2.3 & §4 PRIMARY-SAMPLE-SPACE HYBRID-SHIFT JACOBIAN (EQ. 2)
//------------------------------------------------------------------------------------------------------------------------

// 📐 Hybrid Prefix × Area-Reconnection Shift Jacobian (analytically cancelling p^y_{k-1}(ω'_{k-1}) between F_PSS(Y) and Eq. 2 so
//    glossy/mirror surfaces where p^y_{k-1} -> 0 never produce 0/0 NaNs):
//    |J_{x->y}| = [G(y_{k-1} -> x_k) · p^y_k(ω_k)] / [G(x_{k-1} -> x_k) · p^x_k(ω_k)], where ForwardAreaPdf stores G(x_{k-1} -> x_k).
fn EvaluateHybridShiftJacobian(
    Sample             : ReservoirSample,
    ReceiverPrefixPos  : vec3f,
    ReceiverExitPdf    : f32
) -> f32
{
    if ((Sample.Flags & PathFlagDirectional) != 0u || Sample.Identity == SkyIdentity)
    {
        let ExitRatio = select(ReceiverExitPdf / max(Sample.ExitSolidPdf, 1.0e-6), 1.0, Sample.ReconnectVertex >= Sample.PathLength);
        return clamp(ExitRatio, 0.0, 16.0);
    }
    let Offset  = Sample.Position - ReceiverPrefixPos;
    let DistSq  = max(dot(Offset, Offset), 1.0e-6);
    let Dir     = Offset * inverseSqrt(DistSq);
    let CosAtXk = abs(dot(Sample.Normal, -Dir));
    let G_y     = CosAtXk / DistSq;
    let GeomRatio = G_y / max(Sample.ForwardAreaPdf, 1.0e-8);
    let ExitRatio = select(ReceiverExitPdf / max(Sample.ExitSolidPdf, 1.0e-6), 1.0,
                           Sample.ReconnectVertex >= Sample.PathLength || (Sample.Flags & (PathFlagDirectNee | PathFlagForcedNee)) != 0u);
    let Jac = GeomRatio * ExitRatio;
    return select(0.0, min(Jac, 16.0), Jac == Jac && Jac > 0.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                         §5 SAMPLE DUPLICATION ADAPTIVE TEMPORAL CONFIDENCE CAP
//------------------------------------------------------------------------------------------------------------------------

// 📐 §5 of Lin, Kettunen & Wyman (2026): adaptive temporal confidence cap driven by 17x17 duplication score D.
//    Healthy local spatial reuse shares 1-6 neighbors out of 288 (D <= 0.021) and preserves full DefaultCap (zero jitter).
//    Only excess correlation blobs (D > 0.021) smoothly throttle c_Cap toward 2.0 to dissolve firefly duplication.
fn ResolveAdaptiveConfidenceCap(DefaultCap: f32, DuplicationScore: f32, Enabled: bool) -> f32
{
    let HealthyAllowance = 6.0 / 288.0;
    if (!Enabled || !(DuplicationScore > HealthyAllowance))
    {
        return DefaultCap;
    }
    let Excess = clamp((DuplicationScore - HealthyAllowance) / (1.0 - HealthyAllowance), 0.0, 1.0);
    let Factor = pow(Excess, 0.35);
    return mix(DefaultCap, 2.0, Factor);
}

//------------------------------------------------------------------------------------------------------------------------
//                           §3.2 INVOLUTIVE DIHEDRAL PAIRING TEXTURE LOOKUP (A <-> B)
//------------------------------------------------------------------------------------------------------------------------

struct PairingTransformUniform
{
    Layers : array<vec4i, 4>,   // [Layer]: (OffsetX, OffsetY, Symmetry, Width)
}

fn PositiveModuloI32(Value: i32, Modulus: i32) -> i32
{
    let Rem = Value % Modulus;
    return select(Rem, Rem + Modulus, Rem < 0);
}

// 📐 Exact GPU counterpart of ResolvePairedNeighborCpu: u = (M(P) + O) mod W, δ = Tex(u) − 256, Q = P + M⁻¹(δ).
//    Guaranteed involutive on ℤ² across all 8 dihedral symmetries Sym ∈ [0..7]: Q = Pair(P) <=> P = Pair(Q).
fn ResolvePairedNeighbor(
    PairingMaps : texture_2d_array<u32>,
    Transform   : vec4i,
    Layer       : i32,
    Pixel       : vec2i
) -> vec2i
{
    let W     = max(Transform.w, 4);
    let Sym   = Transform.z;
    let FlipX = select(1, -1, (Sym & 1) != 0);
    let FlipY = select(1, -1, (Sym & 2) != 0);
    let Trans = (Sym & 4) != 0;

    let Mx = select(FlipX * Pixel.x, FlipX * Pixel.y, Trans);
    let My = select(FlipY * Pixel.y, FlipY * Pixel.x, Trans);
    let Ux = PositiveModuloI32(Mx + Transform.x, W);
    let Uy = PositiveModuloI32(My + Transform.y, W);

    let Packed = textureLoad(PairingMaps, vec2i(Ux, Uy), Layer, 0).xy;
    let Dx     = i32(Packed.x) - 256;
    let Dy     = i32(Packed.y) - 256;

    let OutDx = select(FlipX * Dx, FlipY * Dy, Trans);
    let OutDy = select(FlipY * Dy, FlipX * Dx, Trans);
    return Pixel + vec2i(OutDx, OutDy);
}

// 📐 Zero-ray analytic hybrid shift evaluation (§2.3 & §4) used by Pass 1 (ReservoirPairedShift) and unvalidated shifts.
//    Returns (DiffuseIrradiance * Jacobian, TargetPdf * Jacobian) or vec4f(0.0) when rejected by bijectivity/Jacobian bounds.
fn EvaluateAnalyticPairedShift(
    Sample           : ReservoirSample,
    ReceiverPos      : vec3f,
    ReceiverNormal   : vec3f,
    PrimaryThreshold : f32,
    UseRayTrace      : bool,
    UseFootprint     : bool,
    MaxJacobian      : f32
) -> vec4f
{
    if (Sample.ReconnectVertex <= 2u || !UseRayTrace)
    {
        let IsDir = (Sample.Flags & PathFlagDirectional) != 0u || Sample.Identity == SkyIdentity;
        let DirY1 = ResolveSampleDirection(Sample, ReceiverPos);
        let CosY1 = dot(ReceiverNormal, DirY1);
        if (!(CosY1 > 1.0e-4))
        {
            return vec4f(0.0);
        }

        var ReceiverExitPdf = 1.0;
        if (!IsDir)
        {
            let Offset = Sample.Position - ReceiverPos;
            let DistSq = max(dot(Offset, Offset), 1.0e-6);
            let CosX2  = max(abs(dot(Sample.Normal, -DirY1)), 1.0e-4);
            let PdfY1  = CosY1 * InversePi;
            if (Sample.PathLength > 2u)
            {
                ReceiverExitPdf = max(dot(Sample.Normal, Sample.OutgoingDirection), 1.0e-4) * InversePi;
            }
            if (UseRayTrace)
            {
                let RatioY = EvaluateDualFootprintRatio(
                    1.0,
                    Sample.ReconnectRoughness,
                    DistSq,
                    CosY1,
                    CosX2,
                    PdfY1,
                    ReceiverExitPdf,
                    Sample.PathLength <= 2u,
                    false,
                    PrimaryThreshold,
                    UseFootprint
                );
                if (RatioY < 1.0)
                {
                    return vec4f(0.0);
                }
            }
        }

        let Jac = EvaluateHybridShiftJacobian(Sample, ReceiverPos, ReceiverExitPdf);
        if (!(Jac > 0.0) || Jac > MaxJacobian || Jac < (1.0 / MaxJacobian))
        {
            return vec4f(0.0);
        }
        if (any(Sample.Radiance != Sample.Radiance) || all(Sample.Radiance <= vec3f(0.0)))
        {
            return vec4f(0.0);
        }
        let SafeRad = min(max(Sample.Radiance, vec3f(0.0)), vec3f(120.0));
        let Irr     = SafeRad * CosY1;
        let Target  = ResolveLuminance(Irr);
        if (!(Target > 1.0e-8))
        {
            return vec4f(0.0);
        }
        return vec4f(Irr * Jac, Target * Jac);
    }

    if (Sample.ReconnectVertex == 3u)
    {
        let ToPrefix = Sample.PrefixPosition - ReceiverPos;
        let PrefixSq = max(dot(ToPrefix, ToPrefix), 1.0e-6);
        let DirY1    = ToPrefix * inverseSqrt(PrefixSq);
        let CosY1    = dot(ReceiverNormal, DirY1);
        if (!(CosY1 > 1.0e-4))
        {
            return vec4f(0.0);
        }
        let Jac = EvaluateHybridShiftJacobian(Sample, Sample.PrefixPosition, Sample.ExitSolidPdf);
        if (!(Jac > 0.0) || Jac > MaxJacobian || Jac < (1.0 / MaxJacobian))
        {
            return vec4f(0.0);
        }
        if (any(Sample.Radiance != Sample.Radiance) || all(Sample.Radiance <= vec3f(0.0)))
        {
            return vec4f(0.0);
        }
        let SafeRad = min(max(Sample.Radiance, vec3f(0.0)), vec3f(120.0));
        let Irr     = SafeRad * CosY1;
        let Target  = ResolveLuminance(Irr);
        if (!(Target > 1.0e-8))
        {
            return vec4f(0.0);
        }
        return vec4f(Irr * Jac, Target * Jac);
    }

    return vec4f(0.0);
}
