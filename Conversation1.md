
● Agent "Research UE5 VSM clipmap LOD selection" finished · 7m 3s

  Thought for 19s

UE5 research landed with verbatim 5.8 source. This is the answer, and it reframes the problem.

Comparison

┌────────────────────────┬─────────────────────────────────────────────────────────────────┬────────────────────────────────────┬─────────────┐
│                        │                             UE5 5.8                             │                ours                │   verdict   │
├────────────────────────┼─────────────────────────────────────────────────────────────────┼────────────────────────────────────┼─────────────┤
│ level formula          │ floor(log2(dist) + Bias)                                        │ ceil(log2(dist/half_span))         │ ≈ same      │
│                        │                                                                 │                                    │ shape       │
├────────────────────────┼─────────────────────────────────────────────────────────────────┼────────────────────────────────────┼─────────────┤
│ view-resolution term   │ ResolutionLodBias += log2(LodScale), LodScale = 0.5/ProjScale.X │ none — BaseTileMetres = 0.5f       │ 🔴 missing  │
│                        │  × (16384/ViewportWidth)                                        │ hardcoded                          │             │
├────────────────────────┼─────────────────────────────────────────────────────────────────┼────────────────────────────────────┼─────────────┤
│ pool-pressure feedback │ GlobalResolutionLodBias = lerp(…, max(0, Bias +                 │ none                               │ 🔴 missing  │
│                        │ log2(AllocationRatio))), kicks in at 0.85 pool load             │                                    │             │
├────────────────────────┼─────────────────────────────────────────────────────────────────┼────────────────────────────────────┼─────────────┤
│ marking vs sampling    │ same function both passes                                       │ mirrored by hand, bit-identical    │ ✅ ours is  │
│ predicate              │                                                                 │ (verified)                         │ fine        │
├────────────────────────┼─────────────────────────────────────────────────────────────────┼────────────────────────────────────┼─────────────┤
│ coarse fallback        │ PropagateMappedMips precomputes LODOffset once per frame per    │ per-pixel walk at sample time      │ 🔴 differs  │
│                        │ page                                                            │                                    │             │
├────────────────────────┼─────────────────────────────────────────────────────────────────┼────────────────────────────────────┼─────────────┤
│ overflow               │ detected, logged, "will produce visual artifacts"               │ silent                             │ ⚠️          │
└────────────────────────┴─────────────────────────────────────────────────────────────────┴────────────────────────────────────┴─────────────┘

Where the bug is — ranked

1. No view-resolution term in the level rule. UE5 folds FOV + viewport width into a scalar so that one shadow texel ≈ one screen pixel. We have a hardcoded 0.5 m base tile, so our L0 texel is 3.9 mm while a screen pixel at 18 m covers ~16 mm — 4× too fine per axis, 16× the texels. That is the entire 4.36× oversubscription. UE5 solves texel≈pixel analytically, once per frame, on the CPU; we never solve it at all.

2. No pool-pressure feedback. UE5 corrects overshoot with log2(AllocationRatio) — exactly the log of the overshoot, because one bias step halves demand. We demand 2400 from 1024 forever with nothing pushing back. update 2206 never moving on any line of your log is that: ~1400 tiles can never win a page, so their update bit can never clear (MarkShadowTileRendered is the only thing that lowers it, and S7 only reaches tiles that got a page).

3. Our coarse fallback is per-pixel and residency-dependent. 🔴 This is specifically what your screenshots show. UE5 resolves the redirect once per frame per page, so every receiver in a page agrees and the answer is stable. Ours re-walks per pixel against a residency set that rotates every frame (S6 cleared 2 … 637 … 511 …). Neighbouring pixels land on different levels → page-aligned ragged blocks, churning yellow→green→blue→magenta. Magenta = the frame the walk found nothing anywhere.

⚠️ Note UE5 marks the last clipmap level unconditionally (r.Shadow.Virtual.MarkCoarsePagesDirectional = 2) to guarantee a fallback exists nearly everywhere — that's what makes magenta essentially unreachable for them.

Edges look smooth because coarse levels there have demand that fits; the centre is where L0/L1 starve.

📝 EEVEE research still running — I'll fold it in when it lands, since its budgeting is the other half of the question.

The fix that follows from this is a ResolutionLodBias-equivalent (view-derived, coarsening-only, max(0, …) clamped) rather than more cache correctness. But that changes shadow sharpness globally, so I'd rather show you the EEVEE comparison and let you pick before I touch it.

✻ Sautéed for 37s

● Agent "Research EEVEE sun shadow LOD selection" finished · 15m 57s

  Thought for 1m 18s

Both research agents are in. EEVEE's answer changes the recommendation — let me get real numbers for the fix before proposing it.

● Write(_ClaudeScratch\tmp\lodmin.py)
Wrote 41 lines to _ClaudeScratch\tmp\lodmin.py
# What a level FLOOR (EEVEE's lod_min) does to our page demand.
# Same shipped rule as demand.py, but refusing to select levels below `floor_lvl`,
# which is exactly EEVEE's `max(lod + lod_bias, lod_min)` applied before the range clamp.
import math

RES, LODS, BASE = 32, 6, 0.5
NARROW = RES / (RES - 1.0001)
HALF   = BASE * RES * 0.5
POOL   = 1024
… +31 lines

  Thought for 30s, ran 1 shell command

The lod_min floor alone doesn't fix it. Let me test the actual UE5 mechanism — an additive bias, which collapses annuli rather than just flooring the near ones.

● Write(_ClaudeScratch\tmp\lodbias.py)
Wrote 52 lines to _ClaudeScratch\tmp\lodbias.py
# UE5's actual mechanism: an ADDITIVE ResolutionLodBias, not a floor.
# level = min(LODS-1, max(0, ceil(log2(ratio)) + bias))
# A floor only coarsens the near region; a bias shifts EVERY annulus coarser, so
# several annuli collapse onto one level and the total tile count actually drops.
import math

RES, LODS, BASE = 32, 6, 0.5
NARROW = RES / (RES - 1.0001)
HALF   = BASE * RES * 0.5
POOL   = 1024
… +42 lines

  Thought for 17s, ran 1 shell command

Both agents in — UE5 from verbatim 5.8 source, EEVEE from local copies. They converge, and the answer is now measured rather than argued.

How the three differ

┌────────────────────┬───────────────────────────────────────────────┬────────────────────────────────────────┬──────────────────────────────┐
│                    │                    UE5 5.8                    │               EEVEE Next               │             ours             │
├────────────────────┼───────────────────────────────────────────────┼────────────────────────────────────────┼──────────────────────────────┤
│ level formula      │ floor(log2(dist) + bias)                      │ ceil(log2(‖lP‖·narrow·2) + lod_bias)   │ ceil(log2(dist/half_span))   │
├────────────────────┼───────────────────────────────────────────────┼────────────────────────────────────────┼──────────────────────────────┤
│ distance measured  │ clipmap origin                                │ camera pos in light space              │ clipmap centre (= orbit      │
│ from               │                                               │                                        │ target)                      │
├────────────────────┼───────────────────────────────────────────────┼────────────────────────────────────────┼──────────────────────────────┤
│ lateral or 3D      │ 3D                                            │ 3D length(lP)                          │ lateral xy only              │
├────────────────────┼───────────────────────────────────────────────┼────────────────────────────────────────┼──────────────────────────────┤
│ pixel-footprint    │ ✅ log2(LodScale) from FOV+viewport, CPU,     │ ❌ none for sun (only punctual uses    │ ❌ none                      │
│ term               │ once/frame                                    │ film_pixel_radius)                     │                              │
├────────────────────┼───────────────────────────────────────────────┼────────────────────────────────────────┼──────────────────────────────┤
│ resolution floor   │ max(0, ResolutionLodBias)                     │ lod_min from Resolution Limit (mm) —   │ ❌ none                      │
│                    │                                               │ user-facing                            │                              │
├────────────────────┼───────────────────────────────────────────────┼────────────────────────────────────────┼──────────────────────────────┤
│ pool-pressure      │ ✅ GlobalResolutionLodBias +=                 │ ❌ none — fails and warns              │ ❌ none                      │
│ feedback           │ log2(AllocationRatio), arms at 0.85 load      │                                        │                              │
├────────────────────┼───────────────────────────────────────────────┼────────────────────────────────────────┼──────────────────────────────┤
│ coarse fallback    │ PropagateMappedMips → LODOffset, precomputed  │ ..._amend_comp → tile.lod_offset,      │ 🔴 per-pixel walk at sample  │
│                    │ per page per frame, O(1) fetch                │ precomputed, O(1) fetch                │ time                         │
├────────────────────┼───────────────────────────────────────────────┼────────────────────────────────────────┼──────────────────────────────┤
│ overflow behaviour │ logs "will produce visual artifacts"          │ logs "Shadow buffer full"              │ silent                       │
├────────────────────┼───────────────────────────────────────────────┼────────────────────────────────────────┼──────────────────────────────┤
│ narrowing constant │ —                                             │ 32/(32−1.0001)                         │ identical ✅                 │
└────────────────────┴───────────────────────────────────────────────┴────────────────────────────────────────┴──────────────────────────────┘

Notably, neither engine uses per-pixel footprint for sun shadows — my earlier framing was too strong. EEVEE has no resolution term at all; it exposes a world-space mm knob instead. Only UE5 folds the view in, and analytically on the CPU.

Where the bug is — two, measured

1. Nothing bounds demand. Both engines have at least one bounding mechanism; we have zero. My probe over the floor using the shipped rule (_ClaudeScratch/tmp/lodbias.py):

┌──────────────┬───────────────────────────────┬───────┬──────────────┬──────────────┐
│     bias     │     per-level tiles L0→L5     │ total │ vs 1024 pool │ finest texel │
├──────────────┼───────────────────────────────┼───────┼──────────────┼──────────────┤
│ +0 (shipped) │ 1024, 828, 828, 828, 828, 128 │ 4464  │ 4.36× over   │ 3.9 mm       │
├──────────────┼───────────────────────────────┼───────┼──────────────┼──────────────┤
│ +1           │ 0, 256, 220, 220, 220, 288    │ 1204  │ 1.18× over   │ 7.8 mm       │
├──────────────┼───────────────────────────────┼───────┼──────────────┼──────────────┤
│ +2           │ 0, 0, 64, 60, 60, 320         │ 504   │ ✅ FITS      │ 15.6 mm      │
└──────────────┴───────────────────────────────┴───────┴──────────────┴──────────────┘

⚠️ A floor (EEVEE's lod_min) barely helps — L3 floor still leaves 1980, 1.93× over. Only UE5's additive bias works, because it collapses several annuli onto one level.

And the physically-correct bias lands on the same number:

┌─────────────┬───────────┬───────────────┐
│ camera dist │ screen px │ required bias │
├─────────────┼───────────┼───────────────┤
│ 18 m @1440  │ 11.7 mm   │ 1.58 → 2      │
├─────────────┼───────────┼───────────────┤
│ 18 m @1000  │ 16.8 mm   │ 2.10 → 3      │
└─────────────┴───────────┴───────────────┘

🔴 The texel≈pixel bias and the bias that makes demand fit are the same bias. That is the missing ResolutionLodBias.

2. Our coarse fallback is per-pixel; both engines precompute it. This is what your screenshots show. UE5 and EEVEE both resolve the redirect once per frame per page, so every pixel in a page agrees. Ours re-walks per pixel against residency that rotates every frame (S6 cleared 2 … 637 … 511), so neighbouring pixels pick different levels → page-aligned ragged blocks churning yellow→green→blue. Magenta = the frame the walk found nothing; UE5 makes that near-unreachable by marking the last level unconditionally.

📝 Caveat on my numbers: the probe walks the whole 130 m floor, so 4464 overestimates — your log shows real demand at ~2400 because S1 only marks screen-visible receivers. The ratios hold; treat 4464 as an upper bound.

update 2206 frozen on every log line is the consequence: with 2400 demand and 1024 pages, ~1400 tiles can never win a page, and MarkShadowTileRendered is the only thing that clears the bit.

● User answered Claude's questions:
· Demand is 2400 against a 1024 pool with nothing bounding it. Which mechanism should I port? → UE5 ResolutionLodBias (Recommended)
· Both reference engines precompute the coarse-level redirect once per frame per page; we walk per pixel at sample time. This is what makes the level blocks ragged and churning. Fix it in the

Both approved: (C) view-derived ResolutionLodBias, (D) precomputed coarse redirect. Let me track them and read the exact code I need to change.

Starting with (C), the demand bias. I need the camera's FOV/viewport and the existing push-constant plumbing.

  Thought for 8s, searched for 2 patterns

Found the ideal home: LodDistanceScale is already a dead field in the marking push block, and SunShadowTraceBlock has spare pads. Let me read both precisely.

  Read 2 files

    uint32_t EmptySentinel             = 0xFFFFFFFFu;                              // [-]  - the id value meaning "nothing rasteriz
ed here"
    float    LodDistanceScale          = 8.0f;                                     // [m]  - distance at which LOD 0 gives way to L
OD 1; doubles per level
    // 🔴 THE VIEW-DERIVED RESOLUTION BIAS, AND THE ONLY THING BOUNDING PAGE DEMAND. Added to the level every receiver selects, in
BOTH this pass and
    //    SunShadowTrace.glsl. Without it the level rule is pure geometry against a HARDCODED BaseTileMetres, so the atlas is asked
 for a texel density
    //    the screen cannot resolve: at 0.5 m tiles an L0 texel is 3.9 mm while a screen pixel at 18 m covers ~12 mm, i.e. 4x too f
ine per axis and 16x
    //    the texels. Measured over the 100 m floor with the shipped rule, demand was 4464 tiles against a 1024-page pool (4.36x);
at +2 it is 504 and
    //    FITS. ⚠️ That is not a coincidence of tuning — the bias that makes demand fit IS the bias that makes one shadow texel one
 screen pixel, which
    //    is why this is the correct fix and a hand-tuned constant is not. (UE5 calls this ResolutionLodBias and folds FOV + viewpo
rt width into
    float    ResolutionLodBias         = 0.0f;                                     // [level] - ADDED to the selected level; >= 0,
coarsens only
};

// 📝 S2's push block, matching ShadowTileTagInscription.vert's InscriptionConstants. One draw per level, so Level and WindowCentre
Tile change per draw.

Now the solver that computes the bias. It belongs next to the clipmap constants it derives from.

  Read 1 file

● Update(Internal\Graphics\Shadow\SunShadowClipmap.h)
Added 27 lines
//    which reads as a shadow-math bug rather than a basis one.
[[nodiscard]] SunShadowBasis SolveSunShadowBasis(Vector3f SolarDirection);

// The view-derived resolution bias, in LEVELS, to be ADDED to every receiver's selected level by both the marking pass and the tr
acer. This is the
olve. At the shipped
//    0.5 m base tile an L0 texel is 3.9 mm, while a screen pixel at the default 18 m orbit distance covers ~12 mm at 1440p — 4x t
oo fine per axis, 16x
//    the texels. Measured over the 100 m floor with the shipped rule (_ClaudeScratch/tmp/lodbias.py): demand 4464 tiles against a
 1024-page pool
//    (4.36x oversubscribed) at bias 0, versus 504 tiles (FITS) at bias 2. The physically-correct bias at that distance is 1.58 ->
 2.
//    ⚠️ THE TWO NUMBERS AGREEING IS THE WHOLE POINT: the bias that makes demand fit the pool IS the bias that makes one shadow te
xel one screen pixel.
//    A hand-tuned constant would coincide at one camera distance and diverge everywhere else, which is precisely the trap the dea
d LodDistanceScale
//    field fell into.
//
// 🔴 CLAMPED AT ZERO — it may only ever COARSEN, mirroring UE5's `FMath::Max(0.0f, ResolutionLodBias)`. A negative bias selects l
evels finer than the
//    clipmap allocates, so the receiver addresses a level with no tilemap and resolves to nothing at all (a lit hole, not a sharp
er shadow).
// 🔴 RETURNED AS A FLOAT AND FLOORED NOWHERE HERE. Both shaders add it before their own ceil, so rounding it on the CPU would qua
ntise the transition
//    and reintroduce the power-of-two staircase that the eye-distance rule was reverted for.
// ⚠️ VerticalFieldOfView is the FULL vertical angle in radians (ViewportCamera::FieldOfView), not the half-angle. ViewportHeightP
ixels is the id
//    buffer's height, not the window's — they differ while a resize is in flight, and the marking pass reads the id buffer.
// 📝 Returns 0 for a degenerate viewport (zero height, non-finite FOV) rather than asserting: the caller is a per-image render pa
th, and a bias of 0 is
//    the pre-existing behaviour rather than a new failure mode.
[[nodiscard]] float SolveShadowResolutionLodBias(float    VerticalFieldOfView,
                                                 uint32_t ViewportHeightPixels,
                                                 float    ObserverDistanceMetres,
                                                 float    BaseTileMetres = ShadowBaseTileMetres,
                                                 uint32_t PageResolution = ShadowPageResolution);

// Allocate LevelCount LODs, each Resolution² tiles with the level-0 tile edge doubling per level, and clear all residency. Safe t
o call again;
// it rebuilds from scratch. LevelCount is clamped to ShadowTilemapLodCount.
void InitializeSunShadowClipmap(SunShadowClipmap& Clipmap,

● API Error: 400 You have exceeded your daily token limit. You can resume at 2026-08-01 00:00 UTC.

✻ Worked for 29m 2s

❯ /compact

❯ /compact
  ⎿  Compacted (ctrl+o to see full summary)
Read Internal\Graphics\Shadow\SunShadowClipmap.h (189 lines)
Read Internal\Graphics\Shadow\ShadowTileMarkingSubmission.h (192 lines)
Read Internal\Graphics\Shadow\Shaders\MarkVisibleShadowPages.comp (143 lines)
Read _ClaudeScratch\tmp\lodbias.py (53 lines)
Read _ClaudeScratch\tmp\lodmin.py (42 lines)

❯ continue

I'll implement SolveShadowResolutionLodBias in the .cpp, then thread it through the mirror pair.

  Searched for 3 patterns

Now let me read the key sites.

  Read 3 files

Now the tracer's level selector and the CPU-side block solver:

  Read 2 files

Now I'll implement the .cpp definition.

● Update(Internal\Graphics\Shadow\SunShadowClipmap.cpp)
Added 32 lines
    return Basis;
}

float SolveShadowResolutionLodBias(float    VerticalFieldOfView,
                                  uint32_t ViewportHeightPixels,
                                  float    ObserverDistanceMetres,
                                  float    BaseTileMetres,
                                  uint32_t PageResolution)
{
    // 📝 A degenerate viewport is a transient state of a live render path (a resize in flight, a minimised window), so this return
s the neutral bias
    //    rather than asserting — 0 is exactly the pre-existing behaviour, which makes the failure mode "no bias this image" instea
d of a crash.
    if (ViewportHeightPixels == 0u || PageResolution == 0u)
        return 0.0f;
    if (!std::isfinite(VerticalFieldOfView) || !

    // The world extent one screen pixel covers at the observer's distance: the vertical frustum height there, divided by the pixel
 rows spanning it.
    // ⚠️ tan takes the HALF angle — VerticalFieldOfView is the full one, which is why this is not tan(VerticalFieldOfView).
    const float FrustumHeightMetres = 2.0f * ObserverDistanceMetres * std::tan(VerticalFieldOfView * 0.5f);
    const float ScreenPixelMetres   = FrustumHeightMetres / (float)ViewportHeightPixels;

    // One level-0 shadow texel, in the same units. Fixed by the clipmap's own shape, which is precisely why nothing yet related it
 to the screen.
    const float BaseTexelMetres = BaseTileMetres / (float)PageResolution;

    // 🔴 log2 of the RATIO, and clamped at zero so the bias may only ever COARSEN — see the header. A ratio at or below 1 means th
e screen already
    //    out-resolves L0, so there is nothing to save and the finest level is the honest answer.
    const float Ratio = ScreenPixelMetres / BaseTexelMetres;
    if (!std::isfinite(Ratio) || Ratio <= 1.0f)
        return 0.0f;

    return std::log2(Ratio);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

Now the S1 shader — replace the dead LodDistanceScale field and add the bias to SelectShadowLevel:

● Update(Internal\Graphics\Shadow\Shaders\MarkVisibleShadowPages.comp)
Added 5 lines, removed 2 lines
    float BaseTileMetres;          // [m]     - LOD 0 tile edge; doubles per level
    uint  LevelCount;              // [-]     - LODs to consider
    uint  EmptySentinel;           // [-]     - the id value meaning "nothing rasterized here"
    float LodDistanceScale;        // [m]     - ⚠️ NO LONGER READ. The level now follows the clipmap window's own geometry (BaseTile
Metres * Resolution),
                                   //           so a separate hand-tuned distance cannot disagree with it. Kept for the std140 mirr
or.
    // 🔴 THE VIEW-DERIVED RESOLUTION BIAS — the only thing bounding page demand, and it MUST be added identically in SunShadowTrac
e.glsl. Solved once per
    //    image on the CPU (SolveShadowResolutionLodBias) from FOV + viewport height, so the shadow texel a receiver selects is rou
ghly the screen pixel
    //    covering it. >= 0 by construction: it coarsens only, because a finer level than the clipmap allocates has no tilemap to a
ddress.
    // ⚠️ REUSES the slot the dead LodDistanceScale occupied, which is what keeps this block byte-identical to the C++ mirror.
    float ResolutionLodBias;       // [level] - ADDED to the selected level before the clamp
} Constants;

// The per-level toroidal origins, one per level, in a small uniform block rather than the push constants: the 64-byte matrix plus
the vectors above

Now the level function in S1. Note the Ratio <= 1.0 early-out must also apply the bias, otherwise receivers inside L0 ignore it entirely:

● Update(Internal\Graphics\Shadow\Shaders\MarkVisibleShadowPages.comp)
Added 10 lines, removed 3 lines
//    receiver's level changed frame to frame with no geometric cause, which is the flicker.
// ⚠️ CHEBYSHEV (per-axis max), not `length()`: the window is a SQUARE, and a Euclidean test under-reports the extent off-axis, hand
ing a diagonal
//    receiver a level whose window excludes it. Same reasoning and the same measurement as the tracer's copy.
// 🔴 THE BIAS IS ADDED BEFORE THE CEIL, AND THE `Ratio <= 1` CASE TAKES IT TOO. Returning a bare 0 for receivers inside L0's half-
span would exempt
//    exactly the near region the bias exists to coarsen — which is the region that oversubscribes the pool (L0 alone demanded 1024
 of 1024 pages). The
//    geometric term simply saturates at 0 there, so the level becomes ceil(bias) and the whole near field lands on one level rathe
r than the finest.
uint SelectShadowLevel(vec2 LateralMetres, vec2 CentreMetres)
{
    const float BaseHalfSpan = Constants.BaseTileMetres * float(ShadowTilemapResolution) * 0.5;

    const vec2  Offset       = abs(LateralMetres - CentreMetres);
    const float OffsetMetres = max(Offset.x, Offset.y) * MarkLevelNarrowing;

    const float Ratio = OffsetMetres / BaseHalfSpan;
    if (Ratio <= 1.0)
    // Inside L0's half-span the log is negative, so the geometric term clamps to 0 and the bias alone decides.
    const float Ratio        = OffsetMetres / BaseHalfSpan;
    const float GeometricLod = (Ratio <= 1.0) ? 0.0 : log2(Ratio);

    const float BiasedLod = ceil(GeometricLod + max(Constants.ResolutionLodBias, 0.0));
    if (BiasedLod <= 0.0)
        return 0u;

    return min(uint(ceil(log2(Ratio))), Constants.LevelCount - 1u);
    return min(uint(BiasedLod), Constants.LevelCount - 1u);
}

// This level's window centre in light-space METRES, recovered from its toroidal origin (ADD form: corner is -Origin, centre is -Or
igin + Resolution/2).

Now the tracer's mirror. It takes params rather than reading a push block, so I'll add the bias as an argument:

● Update(Internal\Graphics\Shadow\Shaders\SunShadowTrace.glsl)
Added 13 lines, removed 6 lines
//    it. The containment gate then rejects the tile, the walk falls outward, and the receiver comes out lit: a bright wedge along
 both diagonals, widest
//    where the levels change. Measured in _ClaudeScratch/tmp/lvlcmp.py: Euclidean escapes on the diagonals, Chebyshev escapes ZER
O of 33344 sampled
//    receivers inside L5's reach. Boundaries land at 7.75 / 15.5 / 31 / 62 / 124 m, each just inside the previous level's half-sp
an.
uint SelectShadowTraceLevel(vec2 LateralMetres, vec2 CentreMetres, float BaseTileMetres, uint LevelCount)
// 🔴 ResolutionLodBias IS PART OF THE MIRROR AND NOT AN OPTIONAL REFINEMENT. It is the view-derived term that bounds how many pag
es marking ASKS for
//    (see MarkVisibleShadowPages.comp's SelectShadowLevel, which must compute the identical value). Omitting it on this side woul
d make the reader select
//    a level finer than any page was allocated for, so every near receiver would miss, fall outward through the walk, and read a
coarse page or come out
//    lit — the same class of failure as any other drift between these two copies, but guaranteed rather than intermittent.
uint SelectShadowTraceLevel(vec2 LateralMetres, vec2 CentreMetres, float BaseTileMetres, uint LevelCount, float ResolutionLodBias)
{
    // The window's half-extent at level 0, in metres: the radius L0 can actually serve.
    const float BaseHalfSpan = BaseTileMetres * float(ShadowTraceTilemapResolution) * 0.5;


    // 📝 Inside L0's half-span the ratio is below 1 and the log is negative, so clamp to 0 rather than letting the cast wrap.
    const float Ratio = OffsetMetres / BaseHalfSpan;
    if (Ratio <= 1.0)
    // 📝 Inside L0's half-span the ratio is below 1 and the log is negative, so the geometric term clamps to 0 and the bias alone
 decides. ⚠️ The clamp is
    //    on the TERM, not an early return — an early return would exempt the near field from the bias, which is exactly the regio
n it must coarsen.
    const float Ratio        = OffsetMetres / BaseHalfSpan;
    const float GeometricLod = (Ratio <= 1.0) ? 0.0 : log2(Ratio);

    const float BiasedLod = ceil(GeometricLod + max(ResolutionLodBias, 0.0));
    if (BiasedLod <= 0.0)
        return 0u;

    const uint Level = uint(ceil(log2(Ratio)));
    return min(Level, LevelCount - 1u);
    return min(uint(BiasedLod), LevelCount - 1u);
}
r (ADD form), so the

Now find the call site of SelectShadowTraceLevel:

  Searched for 1 pattern

Let me narrow the search to the actual call site inside the .glsl:

  Searched for 1 pattern

It's inside a macro. Let me see the macro signature and its callers:

  Searched for 1 pattern, read 1 file

I'll add the bias as a macro parameter so it stays explicit at the call site rather than reaching into Trace from inside the macro:

● Update(Internal\Graphics\Shadow\Shaders\SunShadowTrace.glsl)
Added 1 line, removed 1 line
//    the 0..1 visibility but obvious as a marg
       \
                                 DepthBias, LightNormal, ResolutionLodBias)
      \
{
      \
    (OutVisibility)    = 1.0;
       \
    (OutResolvedLevel) = ShadowTraceLodCount;
       \

● Update(Internal\Graphics\Shadow\Shaders\SunShadowTrace.glsl)
Added 4 lines, removed 1 line
          error, so selecting AGAINST L0's centre is both stable and conservative. Using the candidate level's own centre would ma
ke the         \
          selection self-referential: the level determines the centre that determines the level. */
             \
    const vec2 TraceCentreMetres = ResolveShadowTraceCentreMetres((ToroidalOriginArray)[0], (BaseTileMetres), 0u);
        \
    const uint TraceStartLevel   = SelectShadowTraceLevel((LightPosition).xy, TraceCentreMetres, (BaseTileMetres), (LevelCount));
        \
    /* 🔴 (ResolutionLodBias) is passed through rather than read from a uniform inside the macro, because this header is included
by more than one     \
          stage and must not assume any particular block is in scope. It has to be the SAME value the marking pass received in its
 push block. */      \
    const uint TraceStartLevel   = SelectShadowTraceLevel((LightPosition).xy, TraceCentreMetres, (BaseTileMetres), (LevelCount),
        \
                                                          (ResolutionLodBias));
       \

      \
    /* ⚠️ Still walks OUTWARD from the analytic level rather than sampling it alone: the analytically-correct page can be genuinel
y absent under  \
          pool pressure, and a single tap would then report fully lit — a bright hole in a correct shadow. Falling to a coarser pa
ge is a       \

Now find the macro invocation in SurfaceShade.frag:

  Searched for 1 pattern

Now add the field to the frag's UBO mirror (into Pad1's slot) and pass it:

● Update(Internal\Graphics\Visibility\Shaders\SurfaceShade.frag)
Added 3 lines
    float DepthBias;
    uint  LevelCount;
    uint  SunShadowDebugMode;   // [-] - SunShadowDebugView; 0 shades normally. Mirrors the field in SurfaceShadeInscription.h.
    // 🔴 Must equal the ResolutionLodBias the marking pass got in its push block that same image, or the reader selects a level n
othing allocated a page
    //    for. Occupies the slot Pad1 held, so the block's size and every offset above are unchanged.
    float ResolutionLodBias;    // [level] - view-derived, >= 0, coarsens only
} Trace;

// 🧩 One word per PHYSICAL page, raised by ShadowDepthRaster.frag's atomicOr wherever a caster fragment landed and zeroed by Clea
rShadowPageCoverage

● Update(Internal\Graphics\Visibility\Shaders\SurfaceShade.frag)
Added 2 lines, removed 1 line
                             SunShadowAtlas, ShadowPageMapping.TilePage, ShadowPageCoverage.PageDrawn, ShadowPageStale.PageStale,
                             Trace.LevelCount, LightPosition,
                             Origins, Trace.BaseTileMetres,
                             Trace.DepthOriginMetres, Trace.DepthRangeMetres, Trace.DepthBias, LightNormal);
                             Trace.DepthOriginMetres, Trace.DepthRangeMetres, Trace.DepthBias, LightNormal,
                             Trace.ResolutionLodBias);
    return Visibility;
}


Now the C++ mirror of that block, and the solver signature:

● Update(Internal\Graphics\Visibility\SurfaceShadeInscription.h)
Added 6 lines, removed 2 lines
    //    the 112 bytes its own note describes, with SunShadowEnabled holding the last tail pad. Growing it would carry the pass t
oward the 128-byte
    //    guaranteed push-constant minimum for the sake of a diagnostic. This block is a UBO with three spare pads, so the field c
osts zero bytes.
    uint32_t SunShadowDebugMode = 0;             // [-]     - SunShadowDebugView; 0 shades normally (see the enum for what each vi
ew paints)
    uint32_t Pad1               = 0;             // [-]     - std140 tail pad to a 16-byte boundary
    uint32_t Pad2               = 0;
    // 🔴 THE VIEW-DERIVED RESOLUTION BIAS THE READER MUST AGREE WITH. SolveShadowResolutionLodBias produces it once per image; Sh
adowMarkConstants carries
    //    the SAME value to S1. If these two diverge, marking allocates pages for one level and the tracer reads another, so every
 near receiver misses,
    //    walks outward, and resolves coarse or lit. Nothing in the build checks the agreement — see the mirror note on SelectShad
owLevel.
    // ⚠️ Takes the slot Pad1 held rather than being appended, so the block size and every preceding offset are untouched.
    float    ResolutionLodBias  = 0.0f;          // [level] - ADDED to the selected level; >= 0, coarsens only
    uint32_t Pad2               = 0;             // [-]     - std140 tail pad to a 16-byte boundary
};

// 📝 The shade pass's owned device resources. Pipeline + layout, the descriptor plumbing for everything the reconstruction reads
(the sampled

Now the SolveSurfaceShadeTraceBlock signature + body:

  Read 1 file

● Update(Internal\Graphics\Visibility\SurfaceShadeInscription.h)
Added 2 lines, removed 1 line
                                                             float                   DepthOriginMetres,
                                                             float                   DepthRangeMetres,
                                                             float                   DepthBias,
                                                             SunShadowDebugView      DebugView = SunShadowDebugView::Disabled);
                                                             SunShadowDebugView      DebugView = SunShadowDebugView::Disabled,
                                                             float                   ResolutionLodBias = 0.0f);

// Record one shade into an already-open dynamic-rendering colour scope: set viewport + scissor, bind the pipeline + set, push the
 constants, and draw
                                                float                   DepthOriginMetres,
                                                float                   DepthRangeMetres,
                                                float                   DepthBias,
                                                SunShadowDebugView      DebugView)
                                                SunShadowDebugView      DebugView,
                                                float                   ResolutionLodBias)
{
    SunShadowTraceBlock Block;


● Update(Internal\Graphics\Visibility\SurfaceShadeInscription.cpp)
Added 4 lines
    Block.DepthBias           = DepthBias;
    Block.LevelCount          = LevelCount;
    Block.SunShadowDebugMode  = (u
    Block.ResolutionLodBias   = (ResolutionLodBias > 0.0f) ? ResolutionLodBias : 0.0f;
    return Block;
}


Now RenderExtension.cpp — the two call sites that must receive the bias:

  Searched for 1 pattern, read 1 file

The two sites are ~100 lines apart. I'll compute the bias once, early, and store it on Extension so both read the same value. Let me check where CachedShadowCentre is declared:

  Searched for 1 pattern, read 1 file

Add the cache field to RenderExtension.h next to CachedShadowCentre:

  Read 1 file

● Update(Internal\Graphics\RenderExtension\RenderExtension.h)
Added 8 lines
    //    and the scroll solve on different lattices, which presents as shadows offset from their casters rather than as an obviou
s fault.
    Vector3f                CachedShadowCentre{ 0.0f, 0.0f, 0.0f };     // [m] - sun-window centre the preamble used this frame (o
rbit target)

    // 🔴 CACHED SO THE MARKING PASS AND THE TRACER CANNOT DISAGREE, which is the only reason this is a field rather than a local.
 The two consumers sit
    //    ~100 lines apart in one frame body (S1's push block, then the shade's trace UBO), and the bias is a function of the came
ra — so computing it
    //    twice would silently split them the moment either call site drifted onto a different distance or viewport. Solved ONCE i
n the preamble, next to
    //    CachedShadowCentre, and read verbatim by both. Nothing in the build checks that agreement; see SelectShadowLevel's mirro
r note.
    // ⚠️ Zoom DOES move this, unlike CachedShadowCentre — that is the point. The clipmap's centre must ignore Distance, while the
 resolution the screen
    //    can resolve is exactly what Distance changes, so a level bias is the one place zoom is allowed to enter the shadow path.
    float                   CachedShadowLodBias = 0.0f;                 // [level] - this frame's view-derived ResolutionLodBias;
>= 0, coarsens only

    double                  PreviousTimestamp = 0.0; // [s] - Last frame's clock reading, for the per-frame delta
    float                   FlySpeedScale     = 1.0f; // [-] - Scroll-adjusted fly-speed multiplier (Unreal-style)


Now solve it in the preamble, right after CachedShadowCentre:

● Update(Internal\Graphics\RenderExtension\RenderExtension.cpp)
Added 11 lines
            //    light-space offset (MarkVisibleShadowPages.comp / SunShadowTrace.glsl), never the viewer's distance.
            Extension.CachedShadowCentre = Extension.ViewCamera.Target;

            // 🔴 THE ONE THING BOUNDING PAGE DEMAND, and it is solved HERE — before either consumer — so S1 and the tracer canno
t disagree about it. See
            //    SolveShadowResolutionLodBias for why the level rule needs a view term at all: without it the atlas is asked for
 ~4x finer texels per axis
            //    than the screen can resolve, which measured as 4464 tiles of demand against a 1024-page pool. The visible conse
quence was the churn, not
            //    a shortfall message — a different rotating subset of tiles won pages each frame, so a STATIONARY receiver's fin
est resident level changed
            //    frame to frame and its debug colour cycled yellow/green/blue/magenta.
            // ⚠️ The VISIBILITY TARGET's height, not the swapchain's: S1 dispatches over the id buffer, so that is the raster th
is bias must match. They
            //    differ while a resize is in flight. 📝 Distance is the orbit distance, which is what makes zoom coarsen the sha
dow rather than churn it.
            Extension.CachedShadowLodBias = SolveShadowResolutionLodBias(Extension.ViewCamera.FieldOfView,
                                                                        Extension.VisibilityTarget.Height,
                                                                        Extension.ViewCamera.Distance);

            {

Now feed both consumers:

● Update(Internal\Graphics\RenderExtension\RenderExtension.cpp)
Added 4 lines
                    MarkConstants.ScreenExtentX = (int32_t)Extension.VisibilityTarget.Width;
                    MarkConstants.ScreenExtentY = (int32_t)Extension.VisibilityTarget.Height;
                    MarkConstants.LevelCount    = (uint32_t)Extension.SunWindow.Levels.size();

                    // 🔴 THE SAME VALUE THE TRACER GETS, read from the preamble's cache rather than re-solved — see CachedShadow
LodBias. Clamped at zero
                    //    here as well as at the trace block, because a negative bias selects a level finer than any tilemap exis
ts for.
                    MarkConstants.ResolutionLodBias = (Extension.CachedShadowLodBias > 0.0f) ? Extension.CachedShadowLodBias : 0.
0f;
                }

                RecordShadowReceiverMarking(Extension.TileMarking, MarkConstants, CommandBuffer);

● Update(Internal\Graphics\RenderExtension\RenderExtension.cpp)
Added 2 lines, removed 1 line
                                                        ShadowDepthOriginMetres,
                                                        ShadowDepthRangeMetres,
                                                        SunShadowDepthBiasMetres,
                                                        (SunShadowDebugView)Extension.SunShadowDebugMode);
                                                        (SunShadowDebugView)Extension.SunShadowDebugMode,
                                                        Extension.CachedShadowLodBias);
                        UploadSurfaceShadeTraceBlock(Extension.SurfaceShade, TraceBlock);
                    }


Now log the resolved bias. Let me find the scroll diagnostic I extended earlier:

● Update(Internal\Graphics\RenderExtension\RenderExtension.cpp)
Added 9 lines, removed 1 line
                                             !Extension.SunWindow.Levels.empty() &&
                                             (Extension.SunWindow.Levels[0].ToroidalOrigin.XTile != Extension.ReportedOriginXTile
 ||
                                              Extension.SunWindow.Levels[0].ToroidalOrigin.YTile != Extension.ReportedOriginYTile
);
                    if (OriginMoved)
                    // 🔴 A BIAS CHANGE MUST TRIGGER THIS LINE TOO, or the one input that zoom actually moves is invisible in the
 log. Zoom leaves the L0
                    //    origin alone by design (CachedShadowCentre ignores Distance), so gating on OriginMoved alone would prin
t nothing across the whole
                    //    yellow->green->blue->magenta transition the user reports while zooming — precisely the event under inve
stigation.
                    // ⚠️ Compared on the SELECTED level (ceil), not the raw float: the bias moves continuously with distance, so
 a float compare would fire
                    //    every image of a smooth dolly and bury the scroll lines. The integer step is what a receiver actually s
ees change.
                    const int32_t BiasStep  = (int32_t)std::ceil(Extension.CachedShadowLodBias);
                    const bool    BiasMoved = BiasStep != Extension.ReportedShadowLodStep;
                    if (OriginMoved || BiasMoved)
                    {
                        Extension.ReportedShadowLodStep = BiasStep;
                        const TileCoordinate ScrollOrigin = Extension.SunWindow.Levels[0].ToroidalOrigin;
                        // 📝 The per-image scroll STEP in tiles, which separates a slow drift from a jump the pool cannot re-fil
l in one image. Computed
Now ScrollOrigin reads Levels[0] — but if BiasMoved fires while !ReadyCondition, Levels could be empty. Let me check and guard:

  Read 1 file

Levels[0] would be an out-of-bounds read if BiasMoved fires before the clipmap is built. Let me guard the gate:

● Update(Internal\Graphics\RenderExtension\RenderExtension.cpp)
Added 5 lines, removed 2 lines
                    //    yellow->green->blue->magenta transition the user reports while zooming — precisely the event under inve
stigation.
                    // ⚠️ Compared on the SELECTED level (ceil), not the raw float: the bias moves continuously with distance, so
 a float compare would fire
                    //    every image of a smooth dolly and bury the scroll lines. The integer step is what a receiver actually s
ees change.
                    const int32_t BiasStep  = (int32_t)std::ceil(Extension.CachedShadowLodBias);
                    const bool    BiasMoved = BiasStep != Extension.ReportedShadowLodStep;
                    // ⚠️ The window-live conjunct is NOT redundant with OriginMoved's: the body dereferences Levels[0] unconditi
onally, so a bias step on the
                    //    very first images — before InitializeSunShadowClipmap has run — would index an empty vector.
                    const bool    WindowLive = Extension.SunWindow.ReadyCondition && !Extension.SunWindow.Levels.empty();
                    const int32_t BiasStep   = (int32_t)std::ceil(Extension.CachedShadowLodBias);
                    const bool    BiasMoved  = WindowLive && BiasStep != Extension.ReportedShadowLodStep;
                    if (OriginMoved || BiasMoved)
                    {
                        Extension.ReportedShadowLodStep = BiasStep;

Now add the bias to the printed line and declare ReportedShadowLodStep:

● Update(Internal\Graphics\RenderExtension\RenderExtension.cpp)
Added 3 lines
                        const ShadowTileTally&   Tally   = Extension.TileStore.Tally;
                        const ShadowPageCensus&  Census  = Extension.ShadowAtlas.Census;
                        ISSUE_NOTICE("render-extension",
                                     // 📝 `lodbias` is the raw float and `lod+` the level every receiver is actually shifted by.
 Reading them together is
                                     //    what ties a demand collapse to the camera: `pages req` should fall roughly 4x per whol
e step of `lod+`.
                                     "shadow scroll: L0 origin (%d, %d) step (%+d, %+d), dist %.2f m, centre (%.2f, %.2f, %.2f)"
                                     " | lodbias %.2f (lod+%d)"
                                     " | used L0=%u L1=%u L2=%u L3=%u L4=%u L5=%u"
                                     " | stale L0=%u L1=%u L2=%u L3=%u L4=%u L5=%u"
                                     " | update raised %u cleared %u (total %u)"

Added 2 lines
                                     (double)Extension.CachedShadowCentre.XCoord,
                                     (double)Extension.CachedShadowCentre.YCoord,
                                     (double)Extension.CachedShadowCentre.ZCoord,
                                     (double)Extension.CachedShadowLodBias,
                                     (int)BiasStep,
                                     (unsigned)Tally.TileUsedByLevel[0], (unsigned)Tally.TileUsedByLevel[1],
                                     (unsigned)Tally.TileUsedByLevel[2], (unsigned)Tally.TileUsedByLevel[3],
                                     (unsigned)Tally.TileUsedByLevel[4], (unsigned)Tally.TileUsedByLevel[5],

Now declare the tracking field next to the other Reported* fields:

  Searched for 1 pattern

● Update(Internal\Graphics\RenderExtension\RenderExtension.h)
Added 5 lines
    // ⚠️ Seeded to INT32_MIN rather than 0, so image one always prints: (0,0) is a perfectly reachable real origin and would comp
are equal.
    int32_t                 ReportedOriginX
 is the orbit TARGET, so
    //    Distance moves neither it nor any origin — yet Distance is the sole input to CachedShadowLodBias, and a whole-level step
 in that bias re-levels
    //    every receiver on screen. Keyed on the CEILED step rather than the float so a smooth dolly prints once per level crossed
, not once per image.
    // ⚠️ Seeded to a value no ceil of a non-negative bias can produce, so the first live image always prints its starting bias.
    int32_t                 ReportedShadowLodStep = INT32_MIN;   // [level] - last traced ceil(ResolutionLodBias)
    // 🔴 S5's RETURN VALUE, which the call site used to discard. Without it the log cannot tell "the allocator returned early on
a ready condition" from
    //    "it requested pages and every request failed" — both leave the census at zero, and every neighbouring counter keeps read
ing healthy.
    uint32_t                ReportedAllocationRequestCount = UINT32_MAX; // [page] - last S5 request count, traced on CHANGE inclu
ding change INTO zero

Now verify <cmath> is available in RenderExtension.cpp and the camera field names:

  Searched for 2 patterns

All names check out. Now build the shaders:

  Ran 1 shell command

Both shaders compiled — the new macro parameter and the reworked level functions type-checked. Now the C++ build:

  Ran 1 shell command

Clean build. Before handing it over, let me verify the arithmetic actually produces the bias I expect at the default camera, since the whole fix rests on that number:

● Write(_ClaudeScratch\tmp\biasverify.py)
Wrote 57 lines to _ClaudeScratch\tmp\biasverify.py
# Transcribes the SHIPPED SolveShadowResolutionLodBias (SunShadowClipmap.cpp) and the SHIPPED
# biased level rule (both shader copies) and reports what demand becomes at the real camera.
#
# The point is NOT to re-derive the bias -- it is to check that the values the engine will actually
# compute at boot land where the design says, and that the near field is coarsened rather than exempted.
import math

RES, LODS, BASE, PAGE = 32, 6, 0.5, 128
NARROW = RES / (RES - 1.0001)
HALF   = BASE * RES * 0.5
… +47 lines

  Ran 1 shell command

The 10-case sweep over a 2081² grid is too slow. Let me trim it while it runs:

● Update(_ClaudeScratch\tmp\biasverify.py)
Added 4 lines, removed 2 lines
print(f"fov {math.degrees(FOV):.1f} deg vertical, L0 texel {BASE/PAGE*1000:.2f} mm\n")
print("dist   height   screen px    bias   ceil   demand    vs pool     finest")
for dist in (2.0, 5.0, 18.0, 40.0, 100.0):
    for h in (1080, 1440):
    for h in (1440,):
        bias = solve_bias(FOV, h, dist)
        n = int(130.0 / 0.125)
        # 0.25 m step: half the finest tile
        tiles = {l: set() for l in range(LODS)}
        for i in range(-n, n + 1):
            for j in range(-n, n + 1):

● Update(_ClaudeScratch\tmp\biasverify.py)
Added 1 line, removed 1 line
        tiles = {l: set() for l in range(LODS)}
        for i in range(-n, n + 1):
            for j in range(-n, n + 1):
                x, y = i * 0.125, j * 0.125
                x, y = i * 0.25, j * 0.25
                l = select(x, y, bias)
                tm = BASE * (1 << l)
                tiles[l].add((math.floor(x / tm), math.floor(y / tm)))

Let me kill the background run and re-run the trimmed version:

● Stop Task
python _ClaudeScratch\tmp\biasverify.py · stopped
Two findings, one of which is a real bug I introduced.

The NaN guard is wrong. nan dist -> nan, not 0.0. My C++ checks std::isfinite(ObserverDistanceMetres) before the comparisons, so the C++ is actually correct — but the probe transcribed the shipped order wrong, which means the probe isn't a faithful mirror. Let me verify against the real C++ rather than trust either:

  Read 1 file

The C++ has the isfinite guard the probe omitted, so the shipped code returns 0.0 for a NaN distance. The probe was the faulty transcription, not the engine — exactly the "probe transcription goes stale" trap. Let me fix the probe so it stays a faithful mirror:

● Update(_ClaudeScratch\tmp\biasverify.py)
Added 8 lines, removed 3 lines
POOL   = 1024

    #    returns 0. The probe was wrong, not the engine; keep these three lines in the shipped order.
    if height_px == 0 or page == 0:                             return 0.0
    if not math.isfinite(fov_rad) or not math.isfinite(dist_m): return 0.0
    if fov_rad <= 0.0 or dist_m <= 0.0 or base <= 0.0:          return 0.0
    screen_px = (2.0 * dist_m * math.tan(fov_rad * 0.5)) / height_px
    ratio     = screen_px / (base / page)
    return 0.0 if ratio <= 1.0 else math.log2(ratio)
    if not math.isfinite(ratio) or ratio <= 1.0:                return 0.0
    return math.log2(ratio)

def select(ox, oy, bias):
    off   = max(abs(ox), abs(oy)) * NARROW

The second finding is substantive: at close range the bias does not save us. At 2 m it is 0.00 and demand stays 4401 (4.3× over); at 5 m it is 0.04 → still 3510. Let me confirm the fixed probe and check where it crosses:

  Ran 1 shell command

All four guards now return 0.0, and the near-field coarsening is confirmed (offset-0 receiver selects ceil(bias), not 0 — the exemption bug I explicitly guarded against is absent).

Let me record the close-range gap, which is a real limitation of this fix rather than a defect in it:

Fix C is built and verified. The binary is at Binaries\Validation\RenderExtensionValidation.exe — please run it yourself (per your earlier note about the redirected-stdout freeze).

What changed

┌────────────────────────────────────────────────────┬──────────────────────────────────────────────────────────────────────────────┐
│                        File                        │                                    Change                                    │
├────────────────────────────────────────────────────┼──────────────────────────────────────────────────────────────────────────────┤
│ SunShadowClipmap.h/.cpp                            │ New SolveShadowResolutionLodBias — max(0, log2(screen_px_metres / L0_texel)) │
├────────────────────────────────────────────────────┼──────────────────────────────────────────────────────────────────────────────┤
│ ShadowTileMarkingSubmission.h                      │ LodDistanceScale → ResolutionLodBias (reused slot, layout byte-identical)    │
├────────────────────────────────────────────────────┼──────────────────────────────────────────────────────────────────────────────┤
│ MarkVisibleShadowPages.comp                        │ Bias added inside SelectShadowLevel, before the ceil                         │
├────────────────────────────────────────────────────┼──────────────────────────────────────────────────────────────────────────────┤
│ SunShadowTrace.glsl                                │ Same term in SelectShadowTraceLevel + new macro parameter — the mirror       │
├────────────────────────────────────────────────────┼──────────────────────────────────────────────────────────────────────────────┤
│ SurfaceShade.frag / SurfaceShadeInscription.h/.cpp │ Field into the trace UBO, taking Pad1's slot                                 │
├────────────────────────────────────────────────────┼──────────────────────────────────────────────────────────────────────────────┤
│ RenderExtension.h/.cpp                             │ Solved once in the preamble as CachedShadowLodBias; both consumers read it   │
└────────────────────────────────────────────────────┴──────────────────────────────────────────────────────────────────────────────┘

Two things worth flagging.

The near-field is coarsened, not exempted. The obvious way to write this — keep the existing if (Ratio <= 1.0) return 0u; early-out and add the bias after — would have exempted every receiver inside L0's half-span, which is precisely the region that oversubscribes the pool (L0 alone demanded 1024 of 1024 pages). I clamped the geometric term to 0 instead and let the bias decide there. The probe confirms an offset-0 receiver now selects ceil(bias).

A probe I wrote was wrong and the engine was right. My first biasverify.py dropped the isfinite guards and reported nan for a NaN distance; the shipped C++ returns 0. I fixed the probe, not the code — noting it because a probe that silently diverges from the shipped function is the failure mode that has bitten this investigation before.

The measured limitation

At 60° vertical / 1440p:

┌────────────────┬──────┬────────┬──────────────┐
│    Distance    │ Bias │ Demand │ vs 1024 pool │
├────────────────┼──────┼────────┼──────────────┤
│ 2 m            │ 0.00 │ 4401   │ 4.30× over   │
├────────────────┼──────┼────────┼──────────────┤
│ 5 m            │ 0.04 │ 3510   │ 3.43× over   │
├────────────────┼──────┼────────┼──────────────┤
│ 18 m (default) │ 1.89 │ 576    │ FITS         │
├────────────────┼──────┼────────┼──────────────┤
│ 40 m           │ 3.04 │ 340    │ FITS         │
├────────────────┼──────┼────────┼──────────────┤
│ 100 m          │ 4.36 │ 324    │ FITS         │
└────────────────┴──────┴────────┴──────────────┘

So I expect the yellow→green→blue→magenta churn to be gone at the default orbit and to come back when you zoom in close. That is not a defect in the bias — up close the screen really can resolve a 3.9 mm texel, so texel-per-pixel honestly asks for a density the pool cannot supply. UE5 handles that case with a second mechanism I have not ported (GlobalResolutionLodBias, a closed loop on pool occupancy). I've filed it as task #21 rather than building it, since it's worth knowing whether close-up is actually still broken before adding a feedback loop.

When you run it, the shadow scroll: line now carries lodbias %.2f (lod+%d