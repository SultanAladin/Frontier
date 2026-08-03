/*==============================================================================================================================================
                                                            SURFELRADIALDEPTH.GLSL
==============================================================================================================================================*/
// 🧩 Per-surfel radial occlusion — the reason a surfel behind a wall does not leak light onto the surfel in front of it. Each surfel owns a tiny 4x4
//    tile of MSM4 moments (E[z], E[z^2], E[z^3], E[z^4]) indexed by the hemi-oct-square uv of a direction: a compact directional depth map of how far
//    the surfel can "see" before it hits geometry, in each hemisphere direction. The integrate seeds and EMA-updates this tile from its own trace hits
//    (update_surfel_depth2), and the gather queries it (surfel_radial_occlusion_rw) to decide whether a neighbouring surfel is actually visible from
//    the shade point or hidden behind an occluder — the four-moment shadow map (Peters & Klein "Moment Shadow Mapping", RTG-style Hamburger 4MSM)
//    turns those four moments into a smooth visibility estimate without the ringing a two-moment (VSM) map would show.
//
//    🔴 THIS IS AN #include MODULE WITH NO main(). It reads and writes ONE SSBO, the radial-depth tile buffer, by the bare array name
//       `SurfelDepthBuffer[]` (an array of vec4, 16 per surfel) — the includer MUST declare that `buffer` block before #include'ing this file, exactly
//       as webgiya's surfelDepth.value[] is a closed-over global. It also calls SurfelHemiOctSquareEncode/Decode from SurfelGuiding.glsl, so that module
//       MUST be #include'd first. Compile through the .comp with glslc -I, never standalone.
//
//    🔴 PORTED 1:1 from surfelRadialDepth.ts. The uninitialised sentinel is `m.w == 0.0` ⇒ VISIBLE (return 1.0) — this is why F7 zero-fills the depth
//       buffer and why a never-probed surfel does not spuriously self-occlude. The Hamburger 4MSM (LDLᵀ solve, alpha=2e-6 moment bias, sqrt(m2) scale
//       normalisation), the EMA rates (kHit=0.05, kMiss=0.01), and the monotone moment enforcement (m2>=m1², m4>=m2²) are all exact.

#ifndef FRONTIER_SURFEL_SURFELRADIALDEPTH_GLSL
#define FRONTIER_SURFEL_SURFELRADIALDEPTH_GLSL

// The per-surfel tile edge (4x4 = 16 texels of vec4 moments). Structural — ported 1:1 from webgiya constants.ts SURFEL_DEPTH_TEXELS = 4.
const uint SURFEL_DEPTH_TEXELS = 4u;

// surfel_depth_base_index (:88-96). The first tile texel index for a surfel — surfelIndex * (TEXELS * TEXELS).
uint SurfelDepthBaseIndex(uint SurfelIndex)
{
    uint Texels = SURFEL_DEPTH_TEXELS;
    return SurfelIndex * (Texels * Texels);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       LIGHT-BLEED REMAP
//------------------------------------------------------------------------------------------------------------------------

// reduce_light_bleeding (:80-86). Remap [amount..1] -> [0..1] and clamp below `amount` to 0 — the standard moment-shadow-map bleed cut that removes the
// soft grey halo a raw moment estimate leaves around a shadow edge.
float SurfelReduceLightBleeding(float Visibility, float Amount)
{
    float A = clamp(Amount, 0.0, 0.99);
    return clamp((Visibility - A) / (1.0 - A), 0.0, 1.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        HAMBURGER 4MSM
//------------------------------------------------------------------------------------------------------------------------

// compute_surfel_depth_weight (:109-289). Visibility in [0,1] for a query depth `Distance` against the four moments `Moments`. Returns 1.0 (visible)
// on the uninitialised sentinel (m.w==0). Scale-normalises by sqrt(m2) for fp32 conditioning, applies the alpha=2e-6 moment bias, solves the symmetric
// 3x3 moment system with LDLᵀ, then classifies the query into the near / middle / far branch of the Hamburger 4MSM. A variance- and grazing-adaptive
// bleed reduction and the shadow-strength multiply are applied last. 1:1 with the reference — every epsilon and coefficient matches.
float SurfelComputeDepthWeight(vec4 Moments, float Distance, float CosTheta,
                               float ShadowStrengthIn, float BleedReductionIn,
                               float GrazingBiasScaleIn, float VarianceBleedScaleIn)
{
    // Uninitialised => visible.
    if (Moments.w == 0.0) { return 1.0; }

    // Prevent self-shadowing acne / pathological tiny depths.
    float Zf = max(Distance, 1e-4);

    // Clamp moments to non-negative (depths are >=0 here, so this is a safe stabiliser vs filtering/precision oddities).
    float M1 = max(0.0, Moments.x);
    float M2 = max(0.0, Moments.y);
    float M3 = max(0.0, Moments.z);
    float M4 = max(0.0, Moments.w);

    // Scale normalisation by RMS depth ~ sqrt(E[z^2]) — a change of variable Z' = Z/s that preserves the CDF relation, for large-world conditioning.
    float S     = max(1e-4, sqrt(M2));
    float InvS  = 1.0 / S;
    float InvS2 = InvS * InvS;
    float InvS3 = InvS2 * InvS;
    float InvS4 = InvS2 * InvS2;

    float Z = Zf * InvS;

    vec4 B = vec4(M1 * InvS, M2 * InvS2, M3 * InvS3, M4 * InvS4);

    // Moment bias (Algorithm 3 recommends alpha ~ 2e-6 for fp32) — keeps the moment matrix positive definite under filtering.
    float Alpha = 2e-6;
    B = B * (1.0 - Alpha) + vec4(0.5) * Alpha;

    float B1 = B.x;
    float B2 = B.y;
    float B3 = B.z;
    float B4 = B.w;

    // Normalised variance for the adaptive bleed reduction.
    float VarN      = max(0.0, B2 - B1 * B1);
    float VarFactor = clamp(sqrt(VarN), 0.0, 1.0);

    // Grazing term in [0..1].
    float CosThetaC = clamp(CosTheta, 0.0, 1.0);
    float Grazing   = 1.0 - CosThetaC;

    // Effective bleed reduction: base + variance term + grazing term (the last two scaled by VarFactor so stable low-variance bins keep coverage).
    float Bleed = clamp(BleedReductionIn
                        + VarianceBleedScaleIn * VarFactor
                        + GrazingBiasScaleIn * Grazing * VarFactor,
                        0.0, 0.99);

    float Shadow;
    float Eps = 1e-6;

    // Solve B c = (1, z, z^2)^T with LDLᵀ for the symmetric 3x3 moment matrix.
    float L10 = B1;
    float L20 = B2;

    float D1  = max(Eps, B2 - B1 * B1);
    float L21 = (B3 - B2 * B1) / D1;
    float D2  = max(Eps, B4 - B2 * B2 - L21 * L21 * D1);

    // Forward solve L y = rhs.
    float Rhs0 = 1.0;
    float Rhs1 = Z;
    float Rhs2 = Z * Z;

    float Y0 = Rhs0;
    float Y1 = Rhs1 - L10 * Y0;
    float Y2 = Rhs2 - L20 * Y0 - L21 * Y1;

    // Diagonal solve D zV = y.
    float Z0  = Y0;        // D0 == 1
    float Z1  = Y1 / D1;
    float Z2v = Y2 / D2;

    // Back solve Lᵀ c = zV — c2*x^2 + c1*x + c0 = 0.
    float C2 = Z2v;
    float C1 = Z1 - L21 * C2;
    float C0 = Z0 - L10 * C1 - L20 * C2;

    // If the quadratic degenerates, fall back to a VSM-style bound on the first two moments.
    if (abs(C2) < 1e-6)
    {
        if (Z <= B1)
        {
            Shadow = 0.0;
        }
        else
        {
            float Diff = Z - B1;
            Shadow = 1.0 - D1 / (D1 + Diff * Diff);
        }
    }
    else
    {
        float Disc = max(0.0, C1 * C1 - 4.0 * C2 * C0);
        float Sd   = sqrt(Disc);

        float Z2r = (-C1 - Sd) / (2.0 * C2);
        float Z3r = (-C1 + Sd) / (2.0 * C2);

        // Sort roots so Z2r <= Z3r.
        if (Z2r > Z3r)
        {
            float T = Z2r;
            Z2r = Z3r;
            Z3r = T;
        }

        // If roots collapse, fall back (avoids rare div-by-small artifacts).
        if (abs(Z3r - Z2r) < 1e-6)
        {
            if (Z <= B1)
            {
                Shadow = 0.0;
            }
            else
            {
                float Diff = Z - B1;
                Shadow = 1.0 - D1 / (D1 + Diff * Diff);
            }
        }
        else if (Z <= Z2r)
        {
            Shadow = 0.0;
        }
        else if (Z <= Z3r)
        {
            // Middle branch.
            float Denom = (Z3r - Z2r) * max(Eps, (Z - Z2r));
            Shadow = (Z * Z3r - B1 * (Z + Z3r) + B2) / Denom;
        }
        else
        {
            // Far branch.
            float Denom = max(Eps, (Z - Z2r) * (Z - Z3r));
            Shadow = 1.0 - (Z2r * Z3r - B1 * (Z2r + Z3r) + B2) / Denom;
        }
    }

    Shadow = clamp(Shadow, 0.0, 1.0);

    // Post-processing: shadow strength, convert to visibility, bleed-reduction remap.
    float ShadowStrength = max(0.0, ShadowStrengthIn);
    Shadow = clamp(Shadow * ShadowStrength, 0.0, 1.0);

    float Visibility = 1.0 - Shadow;
    Visibility = SurfelReduceLightBleeding(Visibility, Bleed);

    return clamp(Visibility, 0.0, 1.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        TILE SAMPLE (RW)
//------------------------------------------------------------------------------------------------------------------------

// point_sample_radial_depth_rw (:474-495). Nearest-texel read of a surfel's moment tile at uv — the read-write variant the integrator uses (a plain
// SSBO fetch, no filtering). The gather uses this to query the tile written by earlier probe frames.
vec4 SurfelPointSampleRadialDepthRw(uint SurfelIndex, vec2 UvIn)
{
    vec2 Uv = clamp(UvIn, vec2(0.0), vec2(0.999));

    uint Texels = SURFEL_DEPTH_TEXELS;
    uint Base   = SurfelDepthBaseIndex(SurfelIndex);

    uvec2 Px = uvec2(floor(Uv * float(SURFEL_DEPTH_TEXELS)));
    uint  X  = min(Px.x, Texels - 1u);
    uint  Y  = min(Px.y, Texels - 1u);

    uint Index = Base + Y * Texels + X;
    return SurfelDepthBuffer[Index];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     OCCLUSION QUERY + UPDATE
//------------------------------------------------------------------------------------------------------------------------

// surfel_radial_occlusion_rw (:501-539). Visibility of a direction `DirectionWorld` at range `Distance` from a surfel with normal `NormalWorld`. Builds
// the surfel's tangent frame, projects the direction into the hemisphere, encodes to the tile uv, point-samples the moments, and runs the 4MSM. Returns
// 0 for directions behind the surfel hemisphere (dist<=1e-4 guard). `Params` = OcclusionParams = (shadowStrength, bleedReduction, grazingBiasScale, varianceBleedScale).
float SurfelRadialOcclusionRw(uint SurfelIndex, vec3 DirectionWorld, vec3 NormalWorld, float Distance, vec4 Params)
{
    if (Distance <= 0.0001) { return 0.0; }

    vec3 N  = normalize(NormalWorld);
    vec3 Up = (abs(N.z) < 0.999) ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 T  = normalize(cross(Up, N));
    vec3 Bt = cross(N, T);

    vec3 Hemi = vec3(dot(DirectionWorld, T), dot(DirectionWorld, Bt), dot(DirectionWorld, N));
    Hemi.z = max(0.0, Hemi.z);
    vec2 Uv = SurfelHemiOctSquareEncode(normalize(Hemi));

    vec4 Moments = SurfelPointSampleRadialDepthRw(SurfelIndex, Uv);

    Hemi.z = max(0.0, Hemi.z);
    float CosTheta = clamp(Hemi.z, 0.0, 1.0);

    return SurfelComputeDepthWeight(Moments, Distance, CosTheta, Params.x, Params.y, Params.z, Params.w);
}

// update_surfel_depth2 (:411-469). Fold one trace-hit distance into the surfel's moment tile at the texel nearest `UvIn`. On the uninitialised sentinel
// it SEEDS the texel with vec4(d, d², d³, d⁴); otherwise it EMA-blends (kHit=0.05 when the new sample is nearer, kMiss=0.01 when farther) toward the new
// moment vector, then enforces monotone moments (m2>=m1², m4>=m2²). `DirectionLocal` is the sample's local direction (the depthWeight term is left off, matching the reference).
void SurfelUpdateDepth2(uint SurfelIndex, vec2 UvIn, float Distance, vec3 DirectionLocal)
{
    vec2 Uv = clamp(UvIn, vec2(0.0), vec2(0.999));

    uint Texels = SURFEL_DEPTH_TEXELS;
    uint Base   = SurfelDepthBaseIndex(SurfelIndex);

    // Round to nearest texel centre.
    vec2  P        = Uv * float(SURFEL_DEPTH_TEXELS) - vec2(0.5);
    ivec2 PxOffsetI = ivec2(floor(P + vec2(0.5)));
    int   MaxI      = int(SURFEL_DEPTH_TEXELS - 1u);
    PxOffsetI = clamp(PxOffsetI, ivec2(0), ivec2(MaxI));
    uvec2 PxOffset = uvec2(uint(PxOffsetI.x), uint(PxOffsetI.y));

    uint Index = Base + PxOffset.y * Texels + PxOffset.x;

    vec4 Prev = SurfelDepthBuffer[Index];
    vec4 Next;

    if (Prev.w == 0.0)
    {
        float D  = max(Distance, 1e-4);
        float D2 = D * D;
        float D3 = D2 * D;
        float D4 = D2 * D2;
        Next = vec4(D, D2, D3, D4);
    }
    else
    {
        // depthWeight computed to mirror the reference (the alpha multiply by it is commented out there, so it is unused — kept for fidelity/readability).
        vec2  UvCentre  = vec2(float(PxOffset.x) + 0.5, float(PxOffset.y) + 0.5) / float(SURFEL_DEPTH_TEXELS);
        vec3  TexelDir  = SurfelHemiOctSquareDecode(UvCentre);
        float DepthWeight = clamp(dot(DirectionLocal, TexelDir), 0.0, 1.0);

        float D  = max(Distance, 1e-4);
        float D2 = D * D;
        float D3 = D2 * D;
        float D4 = D2 * D2;
        vec4  Sample = vec4(D, D2, D3, D4);

        float KHit  = 0.05;
        float KMiss = 0.01;
        float K2    = (Distance > Prev.x) ? KMiss : KHit;

        float AlphaBlend = K2;   // optionally * DepthWeight (the reference leaves it off)
        Next = Prev + AlphaBlend * (Sample - Prev);

        Next.y = max(Next.y, Next.x * Next.x);
        Next.w = max(Next.w, Next.y * Next.y);
    }

    SurfelDepthBuffer[Index] = Next;
}

#endif
