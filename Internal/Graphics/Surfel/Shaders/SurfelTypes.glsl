/*==============================================================================================================================================
                                                             SURFELTYPES.GLSL
==============================================================================================================================================*/
// 🧩 The GPU half of the surfel record declarations. This file and Surfel/SurfelTypes.h are ONE declaration written twice; the byte offsets in the
//    comments below are the SAME numbers the host header's static_asserts pin. Ported 1:1 from W298/SurfelGI (SurfelTypes.slang).
//
// 🔴 EVERY MEMBER IS A SCALAR, AND THAT IS LOAD-BEARING. Upstream is Slang/HLSL where a structured-buffer `float3` is 12 tight bytes. In GLSL std430 a
//    `vec3` carries 16-BYTE ALIGNMENT, so declaring `vec3 Position; vec3 Normal;` would place Normal at offset 16 where the host writes offset 12, and
//    every later field would follow the drift. Nothing crashes — the surfels merely land in wrong-but-plausible places, which is the most expensive
//    failure this port can produce.
//
//    A std430 struct built only from float/uint has 4-byte alignment and no interior padding, so it matches the C++ struct exactly.
//    ⚠️ NEVER change a member here to vec3/vec4, and never reorder one, without changing SurfelTypes.h and its asserts in the same edit.
//
//    📝 Read/write vec3 fields through the Fetch*/Store* helpers at the foot of this file rather than touching components ad hoc — they keep the
//       scalar layout an implementation detail at every call site instead of spreading it across every pass.

#ifndef FRONTIER_SURFEL_TYPES_GLSL
#define FRONTIER_SURFEL_TYPES_GLSL

//------------------------------------------------------------------------------------------------------------------------
//                                                        TRANSCRIBED LIMITS
//------------------------------------------------------------------------------------------------------------------------

// ⚠️ Every number in this block is pinned by SurfelTypes.h and re-verified by SurfelLayoutProbe. Changing one here alone desynchronizes the store's
//    allocation from the shader's bounds checks — the host sizes the buffers from its copy, the shaders index against this one.
const uint SurfelTileEdge             = 16u;       // [px]     - screen tile edge for generation (kTileSize 16x16)
const uint SurfelTotalLimit           = 65536u;    // [-]      - hard live-surfel cap (CALIBRATED; upstream kTotalSurfelLimit = 150000)
const uint SurfelReferenceThreshold   = 32u;       // [-]      - coverage at which a cell stops accepting surfels (kRefCountThreshold)
const uint SurfelMaximumLife          = 240u;      // [frames] - life ceiling (kMaxLife)
const uint SurfelSleepingMaximumLife  = 60u;       // [frames] - ceiling once asleep (kSleepingMaxLife = kMaxLife / 4)

// 🔴 The frame's ray POOL, not a per-surfel quota — 8 rays per surfel against upstream's 64, because Frontier traces a software BVH (see SurfelTypes.h).
//    SurfelCellCensus claims a run atomically and MUST keep upstream's `RayOffset < SurfelRayBudget` guard: a surfel whose claim lands past the budget
//    traces nothing this frame. Dropping that guard writes past the outcome buffer.
const uint SurfelRaysPerSurfel        = 8u;        // [-]      - budget multiplier
const uint SurfelRayBudget            = SurfelTotalLimit * SurfelRaysPerSurfel;   // [-] - rays the outcome buffer holds

// 📝 The 5³ neighbourhood the census and scatter walk. The cell-to-surfel table is sized SurfelTotalLimit * this — one entry per (surfel, cell) pair —
//    NOT cellCount * PerCellLimit. ⚠️ PerCellLimit is a spawn gate only and never bounds this table.
const uint SurfelNeighbourCellCount   = 125u;      // [-]      - the 5x5x5 offset table's length
const uint SurfelCellListCapacity     = SurfelTotalLimit * SurfelNeighbourCellCount;   // [idx] - entries in the cell-to-surfel table

// 📝 Atlas geometry: one 7x7 tile per surfel (5x5 payload + a one-texel border for bilinear taps), 1024 tiles per row x 64 rows = the limit exactly.
const uint SurfelAtlasTileEdge        = 7u;        // [px]     - one surfel's tile edge
const uint SurfelAtlasTilesPerRow     = 1024u;     // [-]      - tiles across the atlas
const uint SurfelAtlasWidth           = SurfelAtlasTilesPerRow * SurfelAtlasTileEdge;                        // [px] - 7168
const uint SurfelAtlasHeight          = (SurfelTotalLimit / SurfelAtlasTilesPerRow) * SurfelAtlasTileEdge;   // [px] - 448

// Recycle status bits (upstream's documented 0x0001 / 0x0002).
const uint SurfelStatusSleeping = 0x0001u;
const uint SurfelStatusLastSeen = 0x0002u;

// Counter buffer slots as ELEMENT indices. ⚠️ The host enum SurfelCounterOffset spells these as BYTE offsets (0,4,8,...) because upstream indexes a
//    byte-addressed buffer; here the counter is a uint array, so the same slots are 0,1,2,... Do not copy a host value into a shader index.
const uint SurfelCounterValidSurfel  = 0u;
const uint SurfelCounterDirtySurfel  = 1u;
const uint SurfelCounterFreeSurfel   = 2u;
const uint SurfelCounterCell         = 3u;
const uint SurfelCounterRequestedRay = 4u;
const uint SurfelCounterMissBounce   = 5u;

//------------------------------------------------------------------------------------------------------------------------
//                                                            RECORDS
//------------------------------------------------------------------------------------------------------------------------

// 📝 MSME carried state (RT Gems Ch.25). 44 bytes, offsets 0/12/24/28/40.
// 🔴 Inconsistency must SEED TO 1.0, not 0 — MSME divides by it, so a zero-filled buffer is not a valid estimator. SeedSurfelMeanEstimator() below is
//    the only correct initializer; never rely on a cleared buffer.
struct SurfelMeanEstimator
{
    float MeanX, MeanY, MeanZ;                 // +0
    float ShortMeanX, ShortMeanY, ShortMeanZ;  // +12
    float VarianceBlendReduction;              // +24  (upstream `vbbr`)
    float VarianceX, VarianceY, VarianceZ;     // +28
    float Inconsistency;                       // +40
};

// 📝 One surfel. 100 bytes total.
struct Surfel
{
    float PositionX, PositionY, PositionZ;     // +0   [m]
    float NormalX, NormalY, NormalZ;           // +12  [-]        🔴 +12, NOT +16
    float Radius;                              // +24  [m]        0 = unused slot
    float RadianceX, RadianceY, RadianceZ;     // +28  [W/m²/sr]
    float SumLuminance;                        // +40  [-]
    uint  HoleCondition;                       // +44  [-]        upstream `hasHole` (a Slang bool is 4 B here)
    SurfelMeanEstimator MeanEstimator;         // +48
    uint  RayOffset;                           // +92  [idx]
    uint  RayCount;                            // +96  [-]
};                                             // = 100

// 📝 One cell's occupancy. Upstream `CellInfo`. 8 bytes.
struct SurfelCellSpan
{
    uint SurfelCount;   // +0
    uint TableOffset;   // +4
};

// 📝 One ray's outcome. Upstream `SurfelRayResult`. 48 bytes.
struct SurfelRayOutcome
{
    float LocalDirectionX, LocalDirectionY, LocalDirectionZ;   // +0   [-]
    float WorldDirectionX, WorldDirectionY, WorldDirectionZ;   // +12  [-]
    float ProbabilityDensity;                                  // +24  [1/sr]
    float FirstRayLength;                                      // +28  [m]
    float RadianceX, RadianceY, RadianceZ;                     // +32  [W/m²/sr]
    uint  SurfelOrdinal;                                       // +44  [idx]
};                                                             // = 48

// 📝 Recycle bookkeeping. 12 bytes — ⚠️ upstream packs three uint16_t (6 B); this port widens to uint32_t to avoid a VK_KHR_16bit_storage dependency.
struct SurfelRecycleRecord
{
    uint Life;     // +0  [frames]
    uint Frame;    // +4  [-]
    uint Status;   // +8  [-]  bit field
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        ACCESS HELPERS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Compose / decompose the scalar triples. Every pass goes through these so the scalar layout stays confined to this file — a pass that spells out
//    `Surfel.PositionX/Y/Z` inline is one edit away from disagreeing with the host.
vec3 FetchSurfelPosition(Surfel Record)  { return vec3(Record.PositionX, Record.PositionY, Record.PositionZ); }
vec3 FetchSurfelNormal(Surfel Record)    { return vec3(Record.NormalX,   Record.NormalY,   Record.NormalZ);   }
vec3 FetchSurfelRadiance(Surfel Record)  { return vec3(Record.RadianceX, Record.RadianceY, Record.RadianceZ); }

void StoreSurfelPosition(inout Surfel Record, vec3 Value) { Record.PositionX = Value.x; Record.PositionY = Value.y; Record.PositionZ = Value.z; }
void StoreSurfelNormal(inout Surfel Record,   vec3 Value) { Record.NormalX   = Value.x; Record.NormalY   = Value.y; Record.NormalZ   = Value.z; }
void StoreSurfelRadiance(inout Surfel Record, vec3 Value) { Record.RadianceX = Value.x; Record.RadianceY = Value.y; Record.RadianceZ = Value.z; }

vec3 FetchRayOutcomeWorldDirection(SurfelRayOutcome Outcome) { return vec3(Outcome.WorldDirectionX, Outcome.WorldDirectionY, Outcome.WorldDirectionZ); }
vec3 FetchRayOutcomeLocalDirection(SurfelRayOutcome Outcome) { return vec3(Outcome.LocalDirectionX, Outcome.LocalDirectionY, Outcome.LocalDirectionZ); }
vec3 FetchRayOutcomeRadiance(SurfelRayOutcome Outcome)       { return vec3(Outcome.RadianceX,       Outcome.RadianceY,       Outcome.RadianceZ);       }

void StoreRayOutcomeWorldDirection(inout SurfelRayOutcome Outcome, vec3 Value) { Outcome.WorldDirectionX = Value.x; Outcome.WorldDirectionY = Value.y; Outcome.WorldDirectionZ = Value.z; }
void StoreRayOutcomeLocalDirection(inout SurfelRayOutcome Outcome, vec3 Value) { Outcome.LocalDirectionX = Value.x; Outcome.LocalDirectionY = Value.y; Outcome.LocalDirectionZ = Value.z; }
void StoreRayOutcomeRadiance(inout SurfelRayOutcome Outcome, vec3 Value)       { Outcome.RadianceX       = Value.x; Outcome.RadianceY       = Value.y; Outcome.RadianceZ       = Value.z; }

// 🔴 The ONLY correct estimator seed. Inconsistency = 1.0; everything else zero.
SurfelMeanEstimator SeedSurfelMeanEstimator()
{
    SurfelMeanEstimator Estimator;
    Estimator.MeanX = 0.0; Estimator.MeanY = 0.0; Estimator.MeanZ = 0.0;
    Estimator.ShortMeanX = 0.0; Estimator.ShortMeanY = 0.0; Estimator.ShortMeanZ = 0.0;
    Estimator.VarianceBlendReduction = 0.0;
    Estimator.VarianceX = 0.0; Estimator.VarianceY = 0.0; Estimator.VarianceZ = 0.0;
    Estimator.Inconsistency = 1.0;
    return Estimator;
}

// Seed one surfel at a surface point. Mirrors upstream's Surfel::__init(position, normal, radius).
Surfel SeedSurfel(vec3 Position, vec3 Normal, float Radius)
{
    Surfel Record;
    StoreSurfelPosition(Record, Position);
    StoreSurfelNormal(Record, Normal);
    Record.Radius = Radius;
    StoreSurfelRadiance(Record, vec3(0.0));
    Record.SumLuminance  = 0.0;
    Record.HoleCondition = 0u;
    Record.MeanEstimator = SeedSurfelMeanEstimator();
    Record.RayOffset     = 0u;
    Record.RayCount      = 0u;
    return Record;
}

#endif
