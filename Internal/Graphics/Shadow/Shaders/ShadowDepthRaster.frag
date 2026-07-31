/*==============================================================================================================================================
                                                          SHADOWDEPTHRASTER.FRAG
==============================================================================================================================================*/
// 🧩 S7 fragment stage — resolves one caster fragment into one atlas texel and keeps the nearest depth with `imageAtomicMin`. This is the pass that
//    finally puts real depth in the page atlas: everything before it decided WHICH pages exist (S1/S3/S5) and PRIMED them to the identity (S6).
//
// 🔴 THE ATOMIC IS THE DEPTH TEST. There is no depth attachment (the atlas is R32_UINT STORAGE|SAMPLED and cannot be one), so nothing orders the
//    fragments that land in a texel. `imageAtomicMin` makes that order IRRELEVANT rather than merely tolerable: min is commutative and associative,
//    so any interleaving of any number of casters converges on the same nearest depth. A plain imageStore here would keep whichever fragment
//    happened to land LAST — a shadow that flickers between casters as the driver reschedules, which reads as a synchronization bug rather than a
//    missing atomic.
//
// 🔴 DEPTH IS ENCODED MONOTONICALLY INCREASING WITH DISTANCE FROM THE SUN, AND THAT DIRECTION IS THE ENTIRE CORRECTNESS OF THE MIN. The light basis
//    points ForwardAxis ALONG the light's travel (SunShadowClipmap.h:57), so a larger light-space z is FARTHER from the sun. atomicMin therefore
//    keeps the closest caster, which is the occluder. Any sign flip in the encoding silently inverts the test into a max — keeping the FARTHEST
//    surface — and the resulting image is not blank but *inside-out*: objects lit through themselves and shadows cast by back faces.
//
// 🔴 THE TEXEL ADDRESS IS RESOLVED THROUGH THE MAPPING TABLE, NOT FROM gl_FragCoord. gl_FragCoord is a position in this level's 32x32 tile viewport;
//    the atlas texel depends on which PHYSICAL PAGE the allocator gave this tile, which is arbitrary. The chain is: light position -> light tile ->
//    toroidal slot -> page index (mapping SSBO) -> page origin in the atlas -> texel within the page.
//
// ⚠️ A FRAGMENT WHOSE TILE HAS NO PAGE MUST DISCARD, NOT CLAMP. An unmapped tile's depth does not exist, and writing it anywhere at all corrupts a
//    page belonging to a different region of the world — a shadow appearing in empty space far from any caster.

#version 450

// 🔴 DELIBERATELY INCLUDES NOTHING, and the reason is a DESCRIPTOR COLLISION THAT COMPILES CLEAN. ShadowTileStore.glsl declares the tile table as a
//    storage buffer at set 0 binding 0, which is where this pass binds the atlas storage IMAGE. The first draft did include it: glslc returned 0
//    because the unused block is dead-stripped from the name table, but `spirv-dis` showed `OpDecorate %ShadowTileTable Binding 0` still emitted
//    alongside `OpDecorate %ShadowAtlas Binding 0` — two different descriptor types at one binding, which is undefined behaviour the compiler will
//    never report. Relying on dead-stripping would also mean that merely CALLING one of that header's helpers silently reintroduces the collision.
//    S7 needs only the tile addressing, which is small enough to carry here; every quantity else arrives by push constant.
//
// ⚠️ ShadowTilemapResolution is mirrored from ShadowTileStore.glsl / ShadowTileStore.h for the same reason, and is likewise unchecked by any compiler.
// ⚠️ The two helpers below MIRROR ShadowTileStore.glsl and no compiler cross-checks them. WrapTileCoordinate must match its floor-modulo, and the
//    origin must be ADDED (the origin is stored in ADD form) — see that header's note; the sign of that one operator is the whole CPU/GPU contract.

#define ShadowTilemapResolution 32

// Mirrors ShadowTileStore.glsl. 🔴 GLSL's `%` follows the dividend's sign, so a bare `%` returns negative for a negative light tile — and light tiles
// are routinely negative, because the lattice is centred on the observer rather than the world origin.
int WrapTileCoordinate(int Value, int Modulus)
{
    const int Remainder = Value % Modulus;
    return (Remainder < 0) ? (Remainder + Modulus) : Remainder;
}

// Mirrors ShadowTileStore.glsl. 🔴 The origin is ADDED, not subtracted.
uint ResolveShadowTileWordIndex(uint Level, ivec2 LightTile, ivec2 ToroidalOrigin)
{
    const ivec2 Shifted     = LightTile + ToroidalOrigin;
    const int   WrappedX    = WrapTileCoordinate(Shifted.x, ShadowTilemapResolution);
    const int   WrappedY    = WrapTileCoordinate(Shifted.y, ShadowTilemapResolution);
    const uint  WithinLevel = uint(WrappedY * ShadowTilemapResolution + WrappedX);

    return Level * uint(ShadowTilemapResolution * ShadowTilemapResolution) + WithinLevel;
}

// 📝 The atlas as a storage image. ⚠️ NOT `writeonly`: imageAtomicMin both reads and writes, and the qualifier makes it fail to compile — the same
//    trap ShadowTileStore.glsl documents for the atomicOr'd tile table.
layout(set = 0, binding = 0, r32ui) uniform restrict uimage2D ShadowAtlas;

// 📝 The GPU mirror of ShadowPageAtlas::TilePageMapping, uploaded per image by UploadShadowPageMapping: one page index per addressable tile across
//    every level, or ShadowPageUnmapped. This is the indirection that makes the pool virtual.
layout(set = 0, binding = 1, std430) readonly restrict buffer ShadowPageMappingBuffer
{
    uint TilePage[];
} PageMapping;

// 📝 One word per PHYSICAL page, zeroed by ClearShadowPageCoverage at the top of every image and raised here. This is the tracer's answer to "was
//    anything actually drawn into this page", recorded as fact by the same fragment that writes the depth.
// 🔴 NOT `readonly`, and NOT `writeonly` — atomicOr is a read-modify-write and either qualifier fails to compile, the same trap the atlas image above
//    documents.
layout(set = 0, binding = 3, std430) restrict buffer ShadowPageCoverageBuffer
{
    uint PageDrawn[];
} PageCoverage;

// 📝 One word per PHYSICAL page carrying the CPU's ShadowPageNeedsRender verdict: 1 means S6 primed this page to the clear identity this image, 0 means
//    the page holds valid cached depth. Uploaded by UploadShadowPageRenderMask and read-only here.
// 🔴 THIS PASS DRAWS WHOLE TILE WINDOWS, SO WITHOUT THIS GATE ITS WRITE SET IS EVERY MAPPED PAGE WHILE S6 PRIMED ONLY THE STALE ONES. Measured 1024
//    written against 12 primed: the other 1012 took this image's imageAtomicMin on top of depth they already held, and because min never releases, the
//    old caster's silhouette survives permanently. On screen that is a duplicate shadow standing where the object no longer is, over a region shaped
//    like the allocated span of the atlas — a packed grid of silhouettes in open ground.
// 🔴 `readonly` IS CORRECT HERE AND IS THE DIFFERENCE FROM THE COVERAGE BUFFER ABOVE. Nothing in this pass writes the mask, which is exactly what makes
//    it a sound gate: it is constant for the whole pass. Coverage cannot serve this role even though it has the same shape, because S7 RAISES coverage
//    as it runs — gating on it would let the first caster fragment to reach a page silence every fragment after it, keeping one caster per page.
layout(set = 0, binding = 4, std430) readonly restrict buffer ShadowPageRenderMaskBuffer
{
    uint PageAuthorized[];
} PageRenderMask;

layout(push_constant) uniform ShadowDepthConstants
{
    vec4  LightRightAxis;
    vec4  LightUpAxis;
    vec4  LightForwardAxis;
    vec2  WindowCentreTile;
    ivec2 ToroidalOrigin;
    float BaseTileMetres;
    uint  Level;
    float DepthRangeMetres;
    float DepthOriginMetres;
    uint  PageResolution;
    uint  AtlasPageEdge;
    uint  ClearValue;
    uint  Padding;
} Constants;

layout(location = 0) in vec3 InLightPosition;
layout(location = 1) flat in uint InLevel;

// 📝 Arithmetic floor, needed because a light tile left of or below the lattice origin is the common case, not an edge case — the window is centred
//    on the observer, not the world origin. `int(floor(x))` rather than a cast, which truncates toward zero and folds -0.5 and +0.5 both onto 0.
int FloorToInt(float Value)
{
    return int(floor(Value));
}

void main()
{
    const float TileMetres = Constants.BaseTileMetres * float(1u << Constants.Level);

    // ---- light position -> light tile, and the position WITHIN that tile ------------------------------------------------
    const vec2 LateralTile   = InLightPosition.xy / TileMetres;
    const ivec2 LightTile    = ivec2(FloorToInt(LateralTile.x), FloorToInt(LateralTile.y));

    // The fractional part is where in the tile this fragment sits, in [0,1). 📝 Taken as `LateralTile - LightTile` rather than fract() so it is
    //    consistent with the floor above by construction for negative coordinates.
    const vec2 WithinTile = LateralTile - vec2(LightTile);

    // ---- light tile -> toroidal slot -> physical page -------------------------------------------------------------------
    const uint SlotIndex = ResolveShadowTileWordIndex(Constants.Level, LightTile, Constants.ToroidalOrigin);
    if (SlotIndex >= PageMapping.TilePage.length())
        discard;

    const uint PageIndex = PageMapping.TilePage[SlotIndex];

    // 🔴 The sentinel test is the containment gate, and it must come BEFORE any arithmetic on PageIndex. ShadowPageUnmapped is 0xFFFFFFFF, so a page
    //    origin computed from it would wrap to a plausible-looking in-range texel and quietly scribble on page 15.
    const uint PageCapacity = Constants.AtlasPageEdge * Constants.AtlasPageEdge;
    if (PageIndex >= PageCapacity)
        discard;

    // 🔴 THE CACHE GATE, AND IT MUST PRECEDE BOTH THE ATOMIC AND THE COVERAGE WRITE. A page S6 did not prime still holds correct depth from the image
    //    that drew it; rasterizing into it resolves imageAtomicMin against that retained depth instead of against the clear identity, and min keeps the
    //    nearer of the two forever. Discarding is the whole point rather than an optimization — this is the pass-side half of the cache whose CPU half
    //    is ShadowPageNeedsRender, and the two sets must be identical or the atlas accumulates depth from images whose casters have since moved.
    // ⚠️ Bounds-tested separately from PageCapacity: the mask is sized to ShadowPageCapacity, which is not derived from the push-constant page edge.
    if (PageIndex >= PageRenderMask.PageAuthorized.length())
        discard;
    if (PageRenderMask.PageAuthorized[PageIndex] == 0u)
        discard;

    // ---- page -> atlas texel -------------------------------------------------------------------------------------------
    const uvec2 PageOrigin = uvec2((PageIndex % Constants.AtlasPageEdge) * Constants.PageResolution,
                                   (PageIndex / Constants.AtlasPageEdge) * Constants.PageResolution);

    // The texel within the page. 📝 Clamped to the last texel rather than discarded: WithinTile can reach exactly 1.0 through floating-point rounding
    //    at a tile boundary, and discarding there would punch a one-texel seam along every tile edge — a grid of light leaks across the shadow.
    const uvec2 WithinPage = min(uvec2(WithinTile * float(Constants.PageResolution)),
                                 uvec2(Constants.PageResolution - 1u));

    const ivec2 AtlasTexel = ivec2(PageOrigin + WithinPage);

    // ---- encode depth and resolve the nearest caster --------------------------------------------------------------------
    // Normalize light-space depth onto [0,1] across the configured range, then to the full uint span.
    const float DepthNormalized = (InLightPosition.z - Constants.DepthOriginMetres) / Constants.DepthRangeMetres;

    // ⚠️ A caster outside the light's depth range is CLAMPED, not discarded. Discarding would make an over-range caster stop occluding entirely — its
    //    shadow would vanish, which is far worse than the depth being saturated at the range limit.
    const float DepthClamped = clamp(DepthNormalized, 0.0, 1.0);

    // 🔴 THE SCALE IS AN EXACTLY-REPRESENTABLE CONSTANT, NOT `float(ClearValue - 1)`, AND THAT IS A CORRECTNESS FIX RATHER THAN A TIDINESS ONE.
    //    float carries a 24-bit mantissa, so 0xFFFFFFFE is NOT representable and rounds UP to exactly 2^32; a fragment at depth 1.0 then converts a
    //    value one past the uint32 maximum, which is UNDEFINED. Measured on the reference compiler it lands on 0x00000000 — the NEAREST depth — so the
    //    farthest caster in the slab would occlude everything in its page. Worst possible failure direction, and it only ever shows on distant casters.
    //    0xFFFFFF00 has eight low zero bits, needs only 24 significant bits, and is therefore exact in float; it is also safely below the identity, so
    //    the "nothing here" code stays unreachable. 24 bits of depth precision is far more than a 256-texel page can resolve.
    //    (Verified in _ClaudeScratch/tmp/EncodeRangeProbe.cpp: exact, in range, and monotonic across the whole span.)
    const float DepthEncodeScale = 4294967040.0;   // 0xFFFFFF00
    const uint  DepthEncoded     = uint(DepthClamped * DepthEncodeScale);

    imageAtomicMin(ShadowAtlas, AtlasTexel, DepthEncoded);

    // 🔴 RECORD THE COVERAGE AFTER EVERY DISCARD GATE AND ALONGSIDE THE DEPTH WRITE, never earlier. The bit's whole meaning is "a caster fragment
    //    reached this page's depth", so raising it on a path that then discards (unmapped tile, out-of-range slot) would assert coverage for a page
    //    this draw never touched — and the tracer would sample the clear identity from a page it believes is authoritative, reading as a surface lit
    //    through an occluder rather than as a bookkeeping mistake.
    // 📝 One atomicOr per fragment onto a 4 KiB buffer is heavily contended but uncontested in cost terms: the value never changes after the first
    //    write, so every later atomic hits an already-set line and the hardware coalesces them.
    if (PageIndex < PageCoverage.PageDrawn.length())
        atomicOr(PageCoverage.PageDrawn[PageIndex], 1u);
}
