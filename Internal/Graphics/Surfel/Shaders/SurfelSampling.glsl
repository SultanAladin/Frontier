/*==============================================================================================================================================
                                                            SURFELSAMPLING.GLSL
==============================================================================================================================================*/
// 🧩 The random and direction machinery every surfel pass that fires or decodes a ray needs: a small xoroshiro-style generator, the tangent frame a
//    surfel's local hemisphere is expressed in, uniform and cosine hemisphere sampling, and the hemispherical octahedral encode/decode the irradiance
//    atlas is addressed by. Ported 1:1 from W298/SurfelGI (Random.slang + the sampling half of SurfelUtils.slang). No bindings, no storage — pure
//    functions, so the trace, the integrate and the spawn all include it without agreeing on a descriptor layout.
//
// 🔴 THE GENERATOR IS TRANSCRIBED EXACTLY, INCLUDING ITS SEEDING, AND THAT IS NOT COSMETIC FIDELITY. The ray directions a surfel fires are the sample
//    distribution the estimator's variance is computed against, and the atlas's ray-guiding CDF is built from the same directions. Substituting a
//    "better" hash changes the distribution, which changes the variance, which changes how many rays the census's ladder grants — so a hash swap moves
//    the field's whole convergence behaviour and is not comparable against upstream any more. ⚠️ If the noise is ever changed, it must be changed
//    knowingly and re-measured, not tidied.
//
// 📝 UPSTREAM'S HLSL `mul(rowVector, float3x3)` IS A ROW-VECTOR PRODUCT. `mul(dirLocal, get_tangentspace(normal))` treats dirLocal as a ROW and the
//    matrix's three float3 constructor arguments as ROWS — so the result is dirLocal.x*tangent + dirLocal.y*binormal + dirLocal.z*normal. GLSL's mat3
//    constructor takes COLUMNS and `Matrix * Vector` is a column-vector product, so ResolveTangentFrame below builds the frame with tangent / binormal /
//    normal as its COLUMNS and `Frame * Local` gives the identical sum. ⚠️ Writing `Local * Frame` in GLSL, or building the frame from rows, silently
//    transposes it: local +Z stops meaning "along the normal" and every ray leaves at the wrong angle while still looking like a plausible hemisphere.

#ifndef FRONTIER_SURFEL_SAMPLING_GLSL
#define FRONTIER_SURFEL_SAMPLING_GLSL

const float SurfelPi = 3.14159265358979323846;

//------------------------------------------------------------------------------------------------------------------------
//                                                          THE GENERATOR
//------------------------------------------------------------------------------------------------------------------------

// 📝 Upstream's RNG is a struct with mutating methods; GLSL has neither, so the two-word state is carried in a struct passed as `inout`. Same words,
//    same advance, same output — only the calling convention differs.
struct SurfelRandomState
{
    uint Low;    // upstream s.x
    uint High;   // upstream s.y
};

uint SurfelRotateLeft(uint Word, uint Places)
{
    return (Word << Places) | (Word >> (32u - Places));
}

// One step of the generator. ⚠️ The RESULT is computed from the state BEFORE the advance (upstream returns `s.x * 0x9e3779bb` and only then mixes), so
// the two halves of this function cannot be reordered.
uint SurfelRandomNext(inout SurfelRandomState State)
{
    const uint Result = State.Low * 0x9e3779bbu;

    State.High ^= State.Low;
    State.Low   = SurfelRotateLeft(State.Low, 26u) ^ State.High ^ (State.High << 9u);
    State.High  = SurfelRotateLeft(State.High, 13u);

    return Result;
}

// Upstream's integer finalizer, applied to each seed word before the state is used.
uint SurfelRandomHash(uint Seed)
{
    Seed = (Seed ^ 61u) ^ (Seed >> 16u);
    Seed *= 9u;
    Seed = Seed ^ (Seed >> 4u);
    Seed *= 0x27d4eb2du;
    Seed = Seed ^ (Seed >> 15u);
    return Seed;
}

// Seed from a two-component identity plus the frame ordinal. 🔴 THE DISCARDED FIRST DRAW IS PART OF THE SEEDING (upstream's init() ends with a bare
// `next()`): without it the first value out of the generator is a direct function of the hashed seed, which correlates ray 0 across surfels — visible
// as a faint structured pattern in the first bounce rather than as noise.
SurfelRandomState SeedSurfelRandom(uvec2 Identity, uint FrameOrdinal)
{
    Identity += uvec2(FrameOrdinal, FrameOrdinal);

    SurfelRandomState State;
    State.Low  = SurfelRandomHash((Identity.x << 16u) | Identity.y);
    State.High = SurfelRandomHash(FrameOrdinal);

    SurfelRandomNext(State);   // 🔴 not redundant — see above
    return State;
}

// A float in [0, 1). 📝 Built by pasting 23 random bits into an exponent-1 float and subtracting one, rather than by dividing — the division form loses
// the low bits to rounding at the top of the range.
float SurfelRandomFloat(inout SurfelRandomState State)
{
    const uint Bits = 0x3f800000u | (SurfelRandomNext(State) >> 9u);
    return uintBitsToFloat(Bits) - 1.0;
}

vec2 SurfelRandomFloat2(inout SurfelRandomState State)
{
    const float First  = SurfelRandomFloat(State);
    const float Second = SurfelRandomFloat(State);
    return vec2(First, Second);   // ⚠️ two named draws, in order; vec2(f(s), f(s)) has no defined evaluation order
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE TANGENT FRAME
//------------------------------------------------------------------------------------------------------------------------

// The orthonormal frame whose +Z is Normal, as COLUMNS — so `ResolveTangentFrame(N) * Local` takes a local hemisphere direction to world. Upstream
// get_tangentspace; see the transposition note in the file header.
// 📝 The 0.99 helper switch is upstream's, and it is what stops the cross product degenerating: a normal within 8° of ±X would give cross(N, X) ≈ 0 and
//    a tangent of pure noise, so those normals take Z as the helper instead.
mat3 ResolveTangentFrame(vec3 Normal)
{
    const vec3 Helper = abs(Normal.x) > 0.99 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);

    const vec3 Tangent  = normalize(cross(Normal, Helper));
    const vec3 Binormal = normalize(cross(Normal, Tangent));
    return mat3(Tangent, Binormal, Normal);   // COLUMNS
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      HEMISPHERE SAMPLING
//------------------------------------------------------------------------------------------------------------------------

// A direction in the +Z hemisphere, UNIFORM in solid angle. Upstream hemispherepoint_uniform.
// ⚠️ Uniform, not cosine-weighted — and upstream pairs it with a pdf of 1/(16π) rather than the 1/(2π) a uniform hemisphere actually carries. That
//    constant is preserved verbatim in the trace because the integrate divides by the SAME constant when it reweights, so the pair cancels; changing
//    one without the other rescales every surfel's radiance.
vec3 ResolveUniformHemisphereDirection(float First, float Second)
{
    const float Azimuth   = Second * 2.0 * SurfelPi;
    const float CosPolar  = 1.0 - First;
    const float SinPolar  = sqrt(max(0.0, 1.0 - CosPolar * CosPolar));
    return vec3(cos(Azimuth) * SinPolar, sin(Azimuth) * SinPolar, CosPolar);
}

// A direction in the +Z hemisphere, COSINE-weighted. Upstream hemispherepoint_cos — carried for the spawn and the integrate; the trace uses the uniform
// form because that is what upstream's raygen fires.
vec3 ResolveCosineHemisphereDirection(float First, float Second)
{
    const float Azimuth  = Second * 2.0 * SurfelPi;
    const float CosPolar = sqrt(max(0.0, 1.0 - First));
    const float SinPolar = sqrt(max(0.0, 1.0 - CosPolar * CosPolar));
    return vec3(cos(Azimuth) * SinPolar, sin(Azimuth) * SinPolar, CosPolar);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     OCTAHEDRAL ENCODING
//------------------------------------------------------------------------------------------------------------------------

// 🧩 Hemispherical octahedral mapping (Cigolle et al., JCGT 2014, modified by upstream for a hemisphere rather than a sphere): a direction in the +Z
//    hemisphere becomes a point in the rotated unit square, which is how one surfel's 5x5 irradiance tile addresses direction. The rotation by 45°
//    (the x-y / x+y pair) is what makes the HEMISPHERE fill the square instead of only half of it — so every texel of the tile carries signal.
//
// ⚠️ SurfelEncodeOctahedral IS NOT NORMALIZED-INPUT-SAFE IN ONE DIRECTION: at the equator (z = 0) the L1 norm is |x|+|y| and the map is exact, but a
//    direction with a NEGATIVE z folds onto the same square point as its mirror. Every caller passes a local direction from the +Z hemisphere, which is
//    the mapping's domain; passing a full-sphere direction silently aliases the lower hemisphere onto the upper.
vec2 SurfelEncodeOctahedral(vec3 Direction)
{
    const float L1Norm = abs(Direction.x) + abs(Direction.y) + abs(Direction.z);
    const vec2  Folded = Direction.xy * (1.0 / max(L1Norm, 1e-12));
    return vec2(Folded.x - Folded.y, Folded.x + Folded.y);
}

// The inverse. Returns a unit direction in the +Z hemisphere.
vec3 SurfelDecodeOctahedral(vec2 Encoded)
{
    const vec2 Unrotated = vec2((Encoded.x + Encoded.y) * 0.5, (Encoded.y - Encoded.x) * 0.5);
    const vec3 Direction = vec3(Unrotated.x, Unrotated.y, 1.0 - abs(Unrotated.x) - abs(Unrotated.y));
    return normalize(Direction);
}

#endif
