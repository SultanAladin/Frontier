/*==============================================================================================================================================
                                                          SHADOWTILESTORE.GLSL
==============================================================================================================================================*/
// 🧩 The GPU half of the sun shadows' virtual tile table: the bit vocabulary, the toroidal tile addressing, and the atomicOr helpers that S1/S2/S3
//    raise demand with. Included by MarkVisibleShadowPages.comp, ShadowTileTagInscription.frag, and ShadowTileLevelPropagate.comp.
//
// 🔴 THIS FILE IS AN #include BODY, NOT A COMPILE TARGET, and the .glsl extension is load-bearing. ShaderPlan.ps1 discovers only
//    *.vert / *.frag / *.comp, so a rename to .comp would make the build try to compile a fragment with no entry point.
//
// 🔴 EVERY CONSTANT BELOW IS DUPLICATED IN ShadowTileStore.h AND NO COMPILER CROSS-CHECKS THEM. Drift here does not fail the build — it produces
//    shadows that are subtly wrong in a way that reads as a marking bug. ValidateShadowTileStoreLayout() in the .cpp compares these exact `#define`
//    lines as literal text, so 🔴 IF YOU EDIT A VALUE HERE YOU MUST EDIT BOTH THE HEADER AND THAT VALIDATOR'S EXPECTATION TABLE.
//
// ⚠️ The define spellings are whitespace-sensitive to that validator. Keep the alignment as written.

#ifndef FRONTIER_SHADOW_TILESTORE_GLSL
#define FRONTIER_SHADOW_TILESTORE_GLSL

//------------------------------------------------------------------------------------------------------------------------
//                                                       BIT VOCABULARY
//------------------------------------------------------------------------------------------------------------------------

// 🔴 Two INDEPENDENT bits, not one state. Used (S1, receiver-driven from the id buffer) says the tile must EXIST; Update (S2, caster-driven from
//    moving bounds) says its depth is WRONG. Neither implies the other, and a page rasterizes only on the conjunction.
//
// 🔴 Direct and Coarse COEXIST. A distant receiver can sample a coarse tile itself (Direct) while nearer geometry propagates into the same tile
//    (Coarse). Masking must therefore test `Coarse && !Direct` — testing Coarse alone drops a page a receiver is actively reading.
#define ShadowTileUsedBit    1u
#define ShadowTileDirectBit  2u
#define ShadowTileUpdateBit  4u
#define ShadowTileCoarseBit  8u
#define ShadowTileMaskedBit  16u

// 🔴 Bits 0..7 are the ONLY bits a marking pass may atomicOr. Bits 8..31 hold the allocator's packed page index, and an atomicOr that reached into
//    them would corrupt a live page mapping — a tile would then read depth belonging to a different region entirely.
#define ShadowTileFlagMask   0xFFu
#define ShadowTilePageShift  8

#define ShadowTilemapResolution 32
#define ShadowTilemapLodCount   6

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE TABLE
//------------------------------------------------------------------------------------------------------------------------

// 📝 One word per addressable tile, 32² x 6 = 6144 words. Declared `restrict` — no other binding aliases it.
// ⚠️ NOT `readonly` and NOT `writeonly`: an atomicOr is both a read and a write, and either qualifier makes it fail to compile.
layout(set = 0, binding = 0, std430) buffer restrict ShadowTileTableBuffer
{
    uint TileWords[];
} ShadowTileTable;

//------------------------------------------------------------------------------------------------------------------------
//                                                       ADDRESSING
//------------------------------------------------------------------------------------------------------------------------

// 📝 Arithmetic floor-modulo into [0, Modulus). 🔴 GLSL's `%` follows the sign of the dividend, so a bare `Value % Modulus` returns negative for a
//    negative light tile and the index goes out of range or wraps to the wrong slot. Light tiles are routinely negative — the lattice is centred on
//    the observer, not the world origin — so this is the common path, not an edge case. Must match ToroidalAddressing.h's wrap exactly.
int WrapTileCoordinate(int Value, int Modulus)
{
    const int Remainder = Value % Modulus;
    return (Remainder < 0) ? (Remainder + Modulus) : Remainder;
}

// 📝 Flat index into TileWords for a light-space tile at one level, given that level's toroidal origin.
// 🔴 THE ORIGIN IS ADDED, NOT SUBTRACTED. ToroidalOrigin is stored in ADD form — `physical = wrap(LightTile + Origin)`, the origin being the NEGATED
//    window corner (SunShadowClipmap.h:22, .cpp:123). Subtracting here instead compiles, stays in range, and still maps distinct tiles to distinct
//    slots, so nothing about the addressing looks wrong; it simply disagrees with the CPU and the page atlas about WHICH tile owns which slot, and
//    the shadows land in the wrong place. The sign of this one operator is the entire GPU/CPU contract.
uint ResolveShadowTileWordIndex(uint Level, ivec2 LightTile, ivec2 ToroidalOrigin)
{
    const ivec2 Shifted     = LightTile + ToroidalOrigin;
    const int   WrappedX    = WrapTileCoordinate(Shifted.x, ShadowTilemapResolution);
    const int   WrappedY    = WrapTileCoordinate(Shifted.y, ShadowTilemapResolution);
    const uint  WithinLevel = uint(WrappedY * ShadowTilemapResolution + WrappedX);

    return Level * uint(ShadowTilemapResolution * ShadowTilemapResolution) + WithinLevel;
}

// 📝 The tile at one coarser level covering a fine tile. Each coarser level doubles the world extent per tile.
// 🔴 An arithmetic floor-halve, NOT `>> 1` on a signed value and NOT `/ 2`. Integer divide truncates toward zero, mapping both -1 and 0 onto 0, which
//    shifts every negative tile one tile high and mis-propagates the whole half of the window left of and below the origin.
ivec2 ResolveCoarserShadowTile(ivec2 FineTile)
{
    return ivec2(FineTile.x >= 0 ? FineTile.x / 2 : -((-FineTile.x + 1) / 2),
                 FineTile.y >= 0 ? FineTile.y / 2 : -((-FineTile.y + 1) / 2));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        MARKING
//------------------------------------------------------------------------------------------------------------------------

// 📝 Raise bits on one tile. The only mutation a marking pass performs, and it is idempotent — thousands of uncoordinated invocations landing in one
//    tile cost one bit each with no ordering requirement, which is why the whole marking chain needs no synchronization between invocations.
// 🔴 The mask is applied to the ARGUMENT, not merely documented, so a caller that passes a page-index bit by mistake cannot corrupt the mapping.
void RaiseShadowTileBits(uint WordIndex, uint Bits)
{
    if (WordIndex >= ShadowTileTable.TileWords.length())
        return;

    atomicOr(ShadowTileTable.TileWords[WordIndex], Bits & ShadowTileFlagMask);
}

// 📝 The word as it currently stands. ⚠️ Racy by nature during a marking pass — other invocations may raise bits between this read and any use of it.
//    Safe for the propagation pass, which runs after a barrier, and unsafe for anything that tries to make a decision mid-marking.
uint ReadShadowTileWord(uint WordIndex)
{
    if (WordIndex >= ShadowTileTable.TileWords.length())
        return 0u;

    return ShadowTileTable.TileWords[WordIndex];
}

// 📝 The packed page index the allocator stored, or ShadowTilePageUnmapped when the tile has none.
// 🔴 The sentinel is all-ones in the page field, NOT zero — page 0 is a perfectly valid physical page, so a zero sentinel would alias onto it and
//    every unmapped tile would silently sample the atlas's first page. Mirrors ShadowPageUnmapped on the CPU side.
#define ShadowTilePageUnmapped 0x00FFFFFFu

uint ResolveShadowTilePageIndex(uint WordIndex)
{
    return ReadShadowTileWord(WordIndex) >> ShadowTilePageShift;
}

// 📝 Whether a tile carries demand that survived masking — i.e. the allocator should back it with a page.
bool ShadowTileRequestsPage(uint Word)
{
    return (Word & ShadowTileUsedBit) != 0u && (Word & ShadowTileMaskedBit) == 0u;
}

// 📝 Whether a tile's demand is PURELY inherited, and so eligible for masking. 🔴 `Coarse && !Direct` — see the bit-vocabulary note above.
bool ShadowTileDemandIsInherited(uint Word)
{
    return (Word & ShadowTileCoarseBit) != 0u && (Word & ShadowTileDirectBit) == 0u;
}

// 📝 Whether this tile must be rasterized this image. 🔴 The conjunction: Used alone means no caching, Update alone redraws tiles nothing samples.
bool ShadowTileNeedsRender(uint Word)
{
    return ShadowTileRequestsPage(Word) && (Word & ShadowTileUpdateBit) != 0u;
}

#endif
