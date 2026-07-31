/*==============================================================================================================================================
                                                       SHADOWTILETAGINSCRIPTION.FRAG
==============================================================================================================================================*/
// 🧩 S2 fragment stage — one invocation per tilemap texel the caster's footprint covered, raising `Update` on that tile: its stored depth predates
//    the caster's current position and must be redrawn. Raises NOTHING else. `Used` is S1's answer (a receiver asked for the tile) and `Coarse` is
//    S3's (a finer level inherited into it); a caster moving through a region nothing looks at must not conjure pages there, and raising `Used` here
//    would do exactly that — the pool would fill with tiles no pixel ever samples.
//
// 🔴 THE TILE COMES FROM gl_FragCoord, NOT FROM AN INTERPOLATED LIGHT-SPACE COORDINATE. The render target is one texel per tile, so the texel the
//    rasterizer chose IS the tile — an integer, already wrapped into the window by the viewport. Re-deriving it from an interpolated float would
//    reintroduce a floor() whose rounding can disagree with the rasterizer's own sample coverage at a tile boundary, tagging a neighbour instead of
//    the tile that was actually covered.
//
// 🔴 THE PHYSICAL SLOT NEEDS NO TOROIDAL WRAP HERE, AND APPLYING ONE WOULD DOUBLE-WRAP. The vertex stage already mapped light tiles into the window's
//    NDC, so gl_FragCoord.xy is a PHYSICAL slot in [0, 32) — the post-wrap coordinate. ResolveShadowTileWordIndex expects a LIGHT tile and wraps it
//    itself, so it must not be called with this. The flat index is formed directly instead.
//
// ⚠️ No colour attachment is written and none should be attached: the only output is the atomicOr into the tile table. The pipeline needs a viewport
//    of ShadowTilemapResolution² with depth test AND depth write disabled, and `rasterizerDiscardEnable` must stay FALSE (the side effect IS the work).

#version 450

#extension GL_GOOGLE_include_directive : require
#include "ShadowTileStore.glsl"

layout(location = 0) flat in uint InLevel;

void main()
{
    // The physical slot this fragment landed on. Already inside [0, ShadowTilemapResolution) by construction of the viewport.
    const ivec2 Slot = ivec2(gl_FragCoord.xy);

    // ⚠️ Defensive, not decorative: a viewport or scissor wider than the tilemap would let a fragment past the window and the flat index below would
    //    silently spill into the NEXT level's word range, tagging an unrelated tile at a coarser LOD.
    if (Slot.x < 0 || Slot.x >= ShadowTilemapResolution ||
        Slot.y < 0 || Slot.y >= ShadowTilemapResolution)
        return;

    if (InLevel >= uint(ShadowTilemapLodCount))
        return;

    const uint WithinLevel = uint(Slot.y * ShadowTilemapResolution + Slot.x);
    const uint WordIndex   = InLevel * uint(ShadowTilemapResolution * ShadowTilemapResolution) + WithinLevel;

    // 🔴 Update ONLY — see the banner. This bit describes page CONTENT, so it deliberately outlives the per-image demand reset: ResetShadowTileDemand
    //    clears Used/Direct/Coarse/Masked and preserves Update, or a moved caster's tile would read clean next image with nothing having redrawn it.
    RaiseShadowTileBits(WordIndex, ShadowTileUpdateBit);
}
