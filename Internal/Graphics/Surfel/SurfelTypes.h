/*==============================================================================================================================================
                                                              SURFELTYPES.H
==============================================================================================================================================*/
// 🧩 The host mirror of every GPU surfel record, ported 1:1 from W298/SurfelGI (SurfelTypes.slang + MultiscaleMeanEstimator.slang). This header and
//    Shaders/SurfelTypes.glsl are ONE declaration expressed twice, and they must agree byte for byte — every buffer in the surfel chain is written by
//    one side and read by the other, with no runtime diagnostic when they disagree.
//
// 🔴 WHY EVERY FIELD IS A SCALAR. Upstream is Slang/HLSL, where `float3` in a structured buffer is 12 bytes, tightly packed. In GLSL std430 a `vec3`
//    has 16-BYTE ALIGNMENT, so the "obvious" transcription
//        struct Surfel { vec3 Position; vec3 Normal; float Radius; ... }
//    pads Position to 16, lands Normal at 16 instead of 12, and every field after it reads the wrong bytes. The result is not a crash: it is a field
//    of surfels at plausible-looking wrong positions, which is the single most expensive class of bug in this port to diagnose.
//    So both sides declare ONLY scalar float/uint members. A std430 struct of scalars has 4-byte alignment and NO interior padding, which is exactly
//    what a C++ struct of floats does — the two layouts coincide by construction rather than by luck, and the asserts below prove it.
//
// 🔴 The asserts are the contract. They pin the offset of every field, not merely the total size: two fields could swap and leave sizeof() unchanged.
//
//    📝 Deliberate deviations from upstream, each one deviating to REMOVE a hazard rather than to save work:
//       - `bool hasHole` -> `uint HoleCondition`. A Slang `bool` is 4 bytes in a structured buffer; a C++ `bool` is 1. Spelling it uint makes the
//         4 bytes explicit on both sides instead of depending on a compiler's bool width.
//       - `uint16_t life/frame/status` -> three `uint32_t`. 16-bit members in a storage buffer need VK_KHR_16bit_storage, and a 6-byte stride is
//         pathological to index. The cost is 6 bytes x 150k surfels = ~0.9 MB, which is not worth an extension dependency.

#pragma once
#ifndef FRONTIER_GRAPHICS_SURFEL_SURFELTYPES_H
#define FRONTIER_GRAPHICS_SURFEL_SURFELTYPES_H

#include <cstddef>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        TRANSCRIBED LIMITS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Upstream's own numbers, kept verbatim so a reader can diff this against SurfelTypes.slang. The GRID constants are NOT here — upstream drives
//    those through -D defines and its values do not survive the move to a software BVH; see SurfelGridProportions below.
constexpr uint32_t SurfelTileEdge          = 16u;       // [px]  - screen tile edge the generation pass works over (kTileSize is 16x16)
constexpr uint32_t SurfelReferenceThreshold = 32u;      // [-]   - coverage above which a cell stops accepting new surfels (kRefCountThreshold)
constexpr uint32_t SurfelMaximumLife       = 240u;      // [frames] - a surfel's life ceiling (kMaxLife)
constexpr uint32_t SurfelSleepingMaximumLife = SurfelMaximumLife / 4u;   // [frames] - ceiling once asleep (kSleepingMaxLife)

// 🔴 CALIBRATED, not transcribed (authorized budget, 2026-08-05). Upstream's kTotalSurfelLimit is 150000; the two buffers that scale with it —
//    SurfelCellListBuffer at 125 entries PER SURFEL and SurfelRayOutcomeBuffer — dominate the store, so halving the limit halves the store. 65536 is
//    chosen over a round 75000 because it is a power of two: the irradiance/depth atlases tile as 1024 x 64 surfels exactly, with no partial last row
//    to special-case in the atlas addressing (see SurfelAtlasTileEdge below).
// 💡 The limit is not a coverage constraint. At 1080p with SurfelGridProportions::TargetArea = 40000 px², covering the screen once takes ~52 surfels;
//    everything above that buys HISTORY — off-screen and behind-camera surfels retained so the GI does not re-converge when the camera turns.
constexpr uint32_t SurfelTotalLimit        = 65536u;    // [-]   - hard cap on live surfels (upstream kTotalSurfelLimit = 150000)

// 🔴 Upstream's kRayBudget is kTotalSurfelLimit * 64 = 9,600,000 rays per frame, chosen against HARDWARE DXR. Frontier traces the software two-level
//    BVH, where that is not payable, so the budget is 8 rays per surfel rather than 64. This is a POOL, not a per-surfel quota: SurfelCellCensus
//    atomically claims a run of it per surfel (RequestedRay), and a surfel whose claimed offset lands past the budget simply traces nothing this frame
//    (upstream's own `rayOffset < kRayBudget` guard). So a heavy frame degrades by starving late surfels, never by overrunning the buffer.
// ⚠️ SurfelTuningState's MaximumRayCount can still ask for 64 rays for ONE surfel — the ladder is per-surfel and this is the frame total. Phase 6
//    raises the multiplier against measured GPU ms; it is a number to tune, not a structural limit.
constexpr uint32_t SurfelRaysPerSurfel     = 8u;                                    // [-] - budget multiplier (upstream 64, against hardware DXR)
constexpr uint32_t SurfelRayBudget         = SurfelTotalLimit * SurfelRaysPerSurfel;// [-] - rays the outcome buffer is sized for, per frame
constexpr uint32_t SurfelUpstreamRayBudget = 150000u * 64u;                         // [-] - kRayBudget verbatim, for the record; NOT dispatched

// 🔴 THE CELL LIST IS SIZED PER SURFEL, NOT PER CELL — and getting this backwards is the memory trap in this port. The plan's first reading was
//    `cellCount * PerCellLimit` (2.1M x 64 = 537 MB, unpayable). Upstream (SurfelGI.cpp:632) allocates `kTotalSurfelLimit * 125`: one entry for each
//    surfel in each of the 125 neighbour cells it can touch, which is the true worst case because SurfelCellScatter writes exactly one entry per
//    (surfel, intersected cell) pair and the neighbour loop runs 125 times.
// ⚠️ PerCellLimit is therefore NOT an allocation bound. It is only a SPAWN GATE — SurfelSpawnRasterization refuses to seed into a cell already holding
//    that many (upstream SurfelGenerationPass.cs.slang:228). Raising it costs no memory; lowering it does not shrink this buffer.
constexpr uint32_t SurfelNeighbourCellCount = 125u;                                            // [-] - the 5³ neighbourhood the census/scatter walks
constexpr uint32_t SurfelCellListCapacity   = SurfelTotalLimit * SurfelNeighbourCellCount;     // [idx] - entries in the cell-to-surfel table

// 📝 Both atlases give every surfel one 7x7 texel tile (upstream kIrradianceMapUnit / kSurfelDepthTextureUnit), a 5x5 payload with a one-texel border
//    for bilinear taps. Upstream hardcodes 3840x2160 because that holds exactly 150000 tiles; these are DERIVED from the limit instead, so the two can
//    never drift. 1024 tiles per row x 64 rows = 65536 tiles = SurfelTotalLimit, with no partial row.
constexpr uint32_t SurfelAtlasTileEdge     = 7u;                                        // [px] - edge of one surfel's tile (5x5 payload + border)
constexpr uint32_t SurfelAtlasTilesPerRow  = 1024u;                                      // [-]  - tiles across; 1024 x 64 tiles covers the limit exactly
constexpr uint32_t SurfelAtlasWidth        = SurfelAtlasTilesPerRow * SurfelAtlasTileEdge;                       // [px] - 7168
constexpr uint32_t SurfelAtlasHeight       = (SurfelTotalLimit / SurfelAtlasTilesPerRow) * SurfelAtlasTileEdge;   // [px] - 448

static_assert(SurfelTotalLimit % SurfelAtlasTilesPerRow == 0,
              "Atlas rows must divide the surfel limit exactly, or the last row holds partial tiles the addressing does not handle.");

//------------------------------------------------------------------------------------------------------------------------
//                                                       GRID PROPORTIONS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 RECALIBRATED, not transcribed. Upstream runs cellUnit 0.05 m x cellDim 250, which is 15,625,000 cells spanning just 12.5 m — a grid built for a
//    Falcor test scene held entirely in front of the camera. Two things break if it is copied:
//       1. The span is too small. Frontier's clipmap level 0 alone covers 32 m, so a 12.5 m field would stop lighting well inside the visible scene.
//       2. The cell table costs 15.6M x 8 B = 125 MB before a single surfel exists, on a GTX-1650-class floor.
//    0.25 m x 128 spans exactly 32 m — level 0's footprint, so the GI reach matches a structure the renderer already maintains — for 2.1M cells and
//    16 MB. Coarser cells hold more surfels each, which the per-cell limit absorbs.
// ⚠️ CellEdge is not only a grid number — upstream's calcSurfelRadius CLAMPS every surfel radius to `cellUnit * 2` and floors a sleeping surfel's
//    radius at `cellUnit * 0.5`. So CellEdge 0.25 m sets a 0.5 m radius ceiling on any surfel, which is what keeps the 125-cell neighbourhood a
//    sufficient search: a disc can never reach beyond ±2 cells. 🔴 Changing CellEdge silently rescales every surfel disc; the two are one decision.
struct SurfelGridProportions
{
    float    CellEdge      = 0.25f;      // [m] - world edge of one cell (upstream kCellUnit = 0.05); also caps surfel radius at 2x this
    uint32_t CellDimension = 128u;       // [-] - cells per axis (upstream kCellDimension = 250); 128 x 0.25 m = a 32 m reach, matching clipmap level 0
    uint32_t PerCellLimit  = 64u;        // [-] - SPAWN GATE only, costs no memory (upstream kPerCellSurfelLimit = 1024, sized for its 0.05 m cells)
    uint32_t TargetArea    = 40000u;     // [px²] - screen area one surfel aims to cover (upstream kSurfelTargetArea, unchanged)
    uint32_t MaximumPerStep = 10u;       // [-] - new surfels admitted per frame (upstream kMaxSurfelForStep, unchanged)
};

// Total cells for a proportion set. 128³ = 2,097,152.
constexpr uint32_t ResolveSurfelCellCount(uint32_t CellDimension)
{
    return CellDimension * CellDimension * CellDimension;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         COUNTER SLOTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Upstream's SurfelCounterOffset is a BYTE offset enum into one shared counter buffer (0, 4, 8, ...), so the values below are byte offsets too, not
//    element indices. kInitialStatus seeds them: FreeSurfel starts at the full limit because every slot begins free.
enum class SurfelCounterOffset : uint32_t
{
    ValidSurfel  = 0u,    // [B] - live surfel count
    DirtySurfel  = 4u,    // [B] - surfels awaiting recycle
    FreeSurfel   = 8u,    // [B] - vacancies remaining (seeded to SurfelTotalLimit)
    Cell         = 12u,   // [B] - occupied cell count
    RequestedRay = 16u,   // [B] - rays the trace was asked for
    MissBounce   = 20u,   // [B] - rays that left the scene
};

constexpr uint32_t SurfelCounterSlotCount = 6u;                                   // [-] - kInitialStatus's length
constexpr uint32_t SurfelCounterBytes     = SurfelCounterSlotCount * 4u;          // [B] - the whole counter buffer

// 🔴 The counter's ONE correct initial state, upstream's kInitialStatus = { 0, 0, kTotalSurfelLimit, 0, 0, 0 }. FreeSurfel seeds to the FULL LIMIT
//    because every slot begins vacant; a zero-filled counter reads as "no free slots" and NOTHING EVER SPAWNS — a silent dead field, not a crash.
//    ⚠️ Order matches SurfelCounterOffset exactly; index this array with the offset value / 4.
constexpr uint32_t SurfelCounterSeed[SurfelCounterSlotCount] = { 0u, 0u, SurfelTotalLimit, 0u, 0u, 0u };

//------------------------------------------------------------------------------------------------------------------------
//                                                            RECORDS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The Multiscale Mean Estimator's carried state (RT Gems Ch.25). Scalars, per this file's header note.
// 🔴 Inconsistency seeds to 1.0, NOT zero — MSME() divides by it. A zero-filled buffer is NOT a valid initial estimator.
struct SurfelMeanEstimator
{
    float MeanX = 0.0f, MeanY = 0.0f, MeanZ = 0.0f;                 // [W/m²/sr] - the long-window mean this estimator exists to produce
    float ShortMeanX = 0.0f, ShortMeanY = 0.0f, ShortMeanZ = 0.0f;  // [W/m²/sr] - fast-following mean, used to detect real change vs noise
    float VarianceBlendReduction = 0.0f;                            // [-] - upstream `vbbr`: damps the catch-up blend where variance is high
    float VarianceX = 0.0f, VarianceY = 0.0f, VarianceZ = 0.0f;     // [-] - per-channel running variance
    float Inconsistency = 1.0f;                                     // [-] - how much mean and shortMean disagree; SEEDS TO 1
};

// 📝 One surfel. 100 bytes, matching upstream's Slang layout exactly (see the asserts).
struct Surfel
{
    float               PositionX = 0.0f, PositionY = 0.0f, PositionZ = 0.0f;   // [m] - world centre
    float               NormalX = 0.0f, NormalY = 0.0f, NormalZ = 0.0f;         // [-] - world unit normal (the disc's facing)
    float               Radius = 0.0f;                                          // [m] - disc radius; 0 marks an unused slot
    float               RadianceX = 0.0f, RadianceY = 0.0f, RadianceZ = 0.0f;   // [W/m²/sr] - the gathered radiance the shade reads
    float               SumLuminance = 0.0f;                                    // [-] - accumulated luminance, drives ray guiding
    uint32_t            HoleCondition = 0u;                                     // [-] - non-zero when coverage found a gap (upstream `hasHole`)
    SurfelMeanEstimator MeanEstimator = {};                                     // [-] - carried MSME state
    uint32_t            RayOffset = 0u;                                         // [idx] - this surfel's first ray in the shared ray buffer
    uint32_t            RayCount = 0u;                                          // [-] - rays allocated to it this frame
};

// 📝 One cell's occupancy: how many surfels it holds and where its slice of the cell-to-surfel table starts. Upstream `CellInfo`.
struct SurfelCellSpan
{
    uint32_t SurfelCount = 0u;   // [-]   - surfels currently in this cell
    uint32_t TableOffset = 0u;   // [idx] - start of this cell's run in the cell-to-surfel index table
};

// 📝 One traced ray's outcome. Upstream `SurfelRayResult`.
struct SurfelRayOutcome
{
    float    LocalDirectionX = 0.0f, LocalDirectionY = 0.0f, LocalDirectionZ = 0.0f;   // [-] - direction in the surfel's tangent frame (ray guiding)
    float    WorldDirectionX = 0.0f, WorldDirectionY = 0.0f, WorldDirectionZ = 0.0f;   // [-] - the world direction actually traced
    float    ProbabilityDensity = 0.0f;                                                // [1/sr] - sampling pdf, divided out on integrate
    float    FirstRayLength = 0.0f;                                                     // [m] - distance to the first hit (drives surfel depth)
    float    RadianceX = 0.0f, RadianceY = 0.0f, RadianceZ = 0.0f;                      // [W/m²/sr] - radiance the ray brought back
    uint32_t SurfelOrdinal = 0u;                                                        // [idx] - the surfel this ray belongs to
};

// 📝 The recycle bookkeeping. Status is a BIT FIELD, not a count — see the masks below.
// ⚠️ Widened from upstream's three uint16_t to uint32_t (12 B, not 6): see this file's header note on 16-bit storage.
struct SurfelRecycleRecord
{
    uint32_t Life   = 0u;   // [frames] - remaining life; reaching 0 frees the slot
    uint32_t Frame  = 0u;   // [-]      - frame ordinal this surfel was last seen
    uint32_t Status = 0u;   // [-]      - bit field (see SurfelStatus* below)
};

constexpr uint32_t SurfelStatusSleeping = 0x0001u;   // [-] - the surfel is asleep and ages against SurfelSleepingMaximumLife
constexpr uint32_t SurfelStatusLastSeen = 0x0002u;   // [-] - it was touched this frame

//------------------------------------------------------------------------------------------------------------------------
//                                                       LAYOUT ASSERTS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 These are the whole point of the file. Each pins one field's byte offset so a reordering, an inserted member, or a compiler packing difference
//    fails the BUILD instead of producing a field of wrong-but-plausible surfels. Shaders/SurfelTypes.glsl repeats the same numbers in its own
//    comments, and SurfelLayoutProbe round-trips a known pattern through the GPU to prove the two agree in fact and not just on paper.

static_assert(sizeof(float) == 4 && sizeof(uint32_t) == 4, "Surfel layout assumes 4-byte float and uint32_t.");

// -- SurfelMeanEstimator: 44 bytes, offsets 0..40 ------------------------------------------------------------------------
static_assert(offsetof(SurfelMeanEstimator, MeanX)                  ==  0, "MSME MeanX must sit at 0");
static_assert(offsetof(SurfelMeanEstimator, ShortMeanX)             == 12, "MSME ShortMeanX must sit at 12");
static_assert(offsetof(SurfelMeanEstimator, VarianceBlendReduction) == 24, "MSME vbbr must sit at 24");
static_assert(offsetof(SurfelMeanEstimator, VarianceX)              == 28, "MSME VarianceX must sit at 28");
static_assert(offsetof(SurfelMeanEstimator, Inconsistency)          == 40, "MSME Inconsistency must sit at 40");
static_assert(sizeof(SurfelMeanEstimator)                           == 44, "MSMEData is 44 bytes in Slang; a padded mirror desynchronizes every surfel");

// -- Surfel: 100 bytes ---------------------------------------------------------------------------------------------------
static_assert(offsetof(Surfel, PositionX)     ==  0, "Surfel PositionX must sit at 0");
static_assert(offsetof(Surfel, NormalX)       == 12, "Surfel NormalX must sit at 12 (NOT 16 — this is the vec3-padding trap)");
static_assert(offsetof(Surfel, Radius)        == 24, "Surfel Radius must sit at 24");
static_assert(offsetof(Surfel, RadianceX)     == 28, "Surfel RadianceX must sit at 28");
static_assert(offsetof(Surfel, SumLuminance)  == 40, "Surfel SumLuminance must sit at 40");
static_assert(offsetof(Surfel, HoleCondition) == 44, "Surfel HoleCondition must sit at 44 (Slang bool is 4 bytes here)");
static_assert(offsetof(Surfel, MeanEstimator) == 48, "Surfel MeanEstimator must sit at 48");
static_assert(offsetof(Surfel, RayOffset)     == 92, "Surfel RayOffset must sit at 92");
static_assert(offsetof(Surfel, RayCount)      == 96, "Surfel RayCount must sit at 96");
static_assert(sizeof(Surfel)                  == 100, "Surfel is 100 bytes in Slang; the GLSL stride must match exactly");

// -- The small records ---------------------------------------------------------------------------------------------------
static_assert(offsetof(SurfelCellSpan, TableOffset) == 4 && sizeof(SurfelCellSpan) == 8, "SurfelCellSpan must be two tight uints");

static_assert(offsetof(SurfelRayOutcome, WorldDirectionX)    == 12, "RayOutcome WorldDirectionX must sit at 12");
static_assert(offsetof(SurfelRayOutcome, ProbabilityDensity) == 24, "RayOutcome pdf must sit at 24");
static_assert(offsetof(SurfelRayOutcome, FirstRayLength)     == 28, "RayOutcome FirstRayLength must sit at 28");
static_assert(offsetof(SurfelRayOutcome, RadianceX)          == 32, "RayOutcome RadianceX must sit at 32");
static_assert(offsetof(SurfelRayOutcome, SurfelOrdinal)      == 44, "RayOutcome SurfelOrdinal must sit at 44");
static_assert(sizeof(SurfelRayOutcome)                       == 48, "SurfelRayResult is 48 bytes in Slang");

static_assert(sizeof(SurfelRecycleRecord) == 12, "SurfelRecycleRecord is the deliberate 3x uint32 widening (upstream packs 3x uint16 = 6 B)");

} // namespace Frontier

#endif
