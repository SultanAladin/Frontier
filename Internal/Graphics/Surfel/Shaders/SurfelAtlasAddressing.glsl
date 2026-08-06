/*==============================================================================================================================================
                                                         SURFELATLASADDRESSING.GLSL
==============================================================================================================================================*/
// 🧩 Where one surfel's 7x7 tile lives in the atlas, and which texel inside it a given direction lands on. Both atlases — the irradiance map the ray
//    guiding writes and the depth-moment map the occlusion test reads — share this addressing exactly, because upstream sizes them identically
//    (kIrradianceMapUnit == kSurfelDepthTextureUnit == 7x7) and any divergence would need two copies of every offset calculation. Ported from
//    W298/SurfelGI's SurfelIntegratePass / SurfelUtils::getSurfelDepthUV. No bindings — pure addressing over a surfel ordinal and a local direction.
//
// 🔴 UPSTREAM'S TILE ROW CALCULATION IS A BUG THIS PORT DOES NOT REPRODUCE. It writes
//        irrMapLT = uint2(surfelIndex % (Res.x / Unit.x), surfelIndex / (Res.x / Unit.y))
//    — note `.x` in the modulus and `.y` in the DIVISION. That is only correct because its tile is SQUARE (7x7), so Unit.x == Unit.y and the two agree
//    by accident. Spelled with one edge here, the accident cannot become a defect if a future tile is ever non-square. Same numbers, no latent trap.
//
// 🔴 THE 7x7 TILE IS A 5x5 PAYLOAD PLUS A ONE-TEXEL BORDER, AND THE BORDER IS NOT PADDING. The shade samples the depth atlas BILINEARLY through the
//    tile, and a bilinear tap at the payload's edge reaches one texel outside it. Without a border that tap reads the NEIGHBOURING SURFEL's tile — a
//    surfel's occlusion would then be partly another surfel's, which reads as light leaking along tile boundaries in a pattern that looks like noise.
//    The integrate copies the payload edge outward into the border every frame (upstream's "write border" block); this file supplies the addressing that
//    block walks.
//
// ⚠️ EVERY COORDINATE HERE IS A TEXEL, NOT A UV. The integrate writes through imageStore, which is texel-addressed; only the Phase-8 shade samples with
//    a normalized UV, and it gets its own helper at the foot of this file. Mixing the two silently reads texel (0,0) for every surfel.

#ifndef FRONTIER_SURFEL_ATLAS_ADDRESSING_GLSL
#define FRONTIER_SURFEL_ATLAS_ADDRESSING_GLSL

#include "SurfelTypes.glsl"
#include "SurfelSampling.glsl"

//------------------------------------------------------------------------------------------------------------------------
//                                                          TILE GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

// The payload is the tile minus its one-texel border on each side: 7 - 2 = 5.
const int SurfelAtlasPayloadEdge = int(SurfelAtlasTileEdge) - 2;

// 📝 The tile's centre texel, in tile-local coordinates. Upstream's kIrradianceMapHalfUnit — 7/2 = 3 by integer division, which is the middle of a
//    seven-wide run. A direction along the surfel's normal (local +Z, which octahedral-encodes to (0,0)) lands exactly here.
const int SurfelAtlasHalfUnit = int(SurfelAtlasTileEdge) / 2;

//------------------------------------------------------------------------------------------------------------------------
//                                                        TILE ADDRESSING
//------------------------------------------------------------------------------------------------------------------------

// The top-left texel of SurfelOrdinal's tile. 📝 Row-major over SurfelAtlasTilesPerRow tiles per row; the limit divides the row count exactly
//    (asserted in SurfelTypes.h) so there is no partial last row to special-case.
ivec2 ResolveSurfelTileOrigin(uint SurfelOrdinal)
{
    const uint TileColumn = SurfelOrdinal % SurfelAtlasTilesPerRow;
    const uint TileRow    = SurfelOrdinal / SurfelAtlasTilesPerRow;
    return ivec2(TileColumn, TileRow) * int(SurfelAtlasTileEdge);
}

// The texel inside a tile that a LOCAL-SPACE direction addresses. 📝 The direction is octahedral-encoded to the rotated unit square, then scaled onto
//    the payload's half-width and re-centred. `Inset` is 0 for the irradiance map (upstream scales by HalfUnit) and 1 for the depth map (upstream
//    scales by HalfUnit - 1, keeping the whole payload strictly inside the border so the border copy has something to copy).
// ⚠️ sign() x round(abs()) rather than a plain round: it keeps the mapping SYMMETRIC about the centre texel. A plain round() biases one side by half a
//    texel, which tilts every surfel's guiding distribution the same way — a systematic error, not a noisy one.
ivec2 ResolveSurfelTileTexel(vec3 LocalDirection, int Inset)
{
    const vec2  Encoded = SurfelEncodeOctahedral(normalize(LocalDirection));
    const float Extent  = float(SurfelAtlasHalfUnit - Inset);

    const ivec2 Offset = ivec2(int(sign(Encoded.x) * round(abs(Encoded.x * Extent))),
                               int(sign(Encoded.y) * round(abs(Encoded.y * Extent))));

    return ivec2(SurfelAtlasHalfUnit) + Offset;
}

// The inverse of the above: the local direction a tile texel STANDS FOR. Used by the depth write to weight a moment by how well the ray's direction
// matches the texel it is landing in — a ray at the edge of a texel's cone should not fully own that texel.
vec3 ResolveSurfelTexelDirection(ivec2 TileTexel, int Inset)
{
    const vec2 Signed = vec2(TileTexel - ivec2(SurfelAtlasHalfUnit)) / float(SurfelAtlasHalfUnit - Inset);
    return SurfelDecodeOctahedral(Signed);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       SAMPLED ADDRESSING
//------------------------------------------------------------------------------------------------------------------------

// 🧩 The normalized UV a BILINEAR sampler should read for a WORLD direction about a surfel's normal. Upstream getSurfelDepthUV, and the one function
//    in this file the Phase-8 shade needs. 📝 The +1 inset and the (TileEdge - 2) span are what keep the sampled footprint inside the payload: the
//    border exists to be TAPPED by the filter, never to be addressed directly.
// 🔴 THE WORLD DIRECTION IS TAKEN INTO THE SURFEL'S LOCAL FRAME BY THE TRANSPOSE, NOT THE FRAME ITSELF. ResolveTangentFrame returns the local->world
//    matrix as COLUMNS; world->local is its transpose (the frame is orthonormal). Upstream's `mul(get_tangentspace(n), dirW)` is a row-vector product
//    against the same matrix, which IS the transpose — so this transcription needs the explicit transpose() to mean the same thing. ⚠️ Dropping it
//    gives a direction that is still unit-length and still plausible, addressing the wrong texel for every off-axis direction.
vec2 ResolveSurfelAtlasSampleUv(uint SurfelOrdinal, vec3 WorldDirection, vec3 SurfelNormal)
{
    const ivec2 TileOrigin = ResolveSurfelTileOrigin(SurfelOrdinal) + ivec2(1, 1);
    const vec2  OriginUv   = vec2(TileOrigin) / vec2(float(SurfelAtlasWidth), float(SurfelAtlasHeight));

    const vec3 LocalDirection = transpose(ResolveTangentFrame(SurfelNormal)) * WorldDirection;

    const vec2 SignedUv   = SurfelEncodeOctahedral(normalize(LocalDirection));
    const vec2 UnsignedUv = (SignedUv + vec2(1.0)) * 0.5;

    const vec2 PayloadSpan = vec2(float(int(SurfelAtlasTileEdge) - 2)) / vec2(float(SurfelAtlasWidth), float(SurfelAtlasHeight));
    return OriginUv + UnsignedUv * PayloadSpan;
}

#endif
