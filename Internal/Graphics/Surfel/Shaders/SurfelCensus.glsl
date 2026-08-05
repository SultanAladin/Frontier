/*==============================================================================================================================================
                                                            SURFELCENSUS.GLSL
==============================================================================================================================================*/
// 🧩 The per-frame population census counters, shared by every lifecycle pass that births or kills a surfel. Include this and call the
//    SurfelCensus* helpers at each such site; the host reads the tallies back one frame late and appends a CSV row (SurfelCensusTrace.h).
//
//    🔴 THE SLOT INDICES MIRROR SurfelCensusSlot IN SurfelCensusTrace.h — EDIT BOTH IN ONE EDIT. A slot added here alone makes the host read one
//       counter as another, and the CSV columns silently swap meaning. That failure has no compile error and no visual symptom; the numbers just
//       become wrong in a way that still looks like a measurement.
//
//    📝 The counters are a DIAGNOSTIC: they are gated on the buffer being bound, cost one atomicAdd on an already-taken branch, and no pass reads
//       them back. Nothing about the lifecycle's behaviour may depend on them — the field must evolve identically whether the trace records or not.

#ifndef SURFEL_CENSUS_GLSL
#define SURFEL_CENSUS_GLSL

// 🔴 Keep in lockstep with enum SurfelCensusSlot (SurfelCensusTrace.h).
const int SURFEL_CENSUS_SPAWNED      = 0;   // successful allocations this frame
const int SURFEL_CENSUS_SPAWN_FAILED = 1;   // allocations refused at pool capacity
const int SURFEL_CENSUS_DIED_TTL     = 2;   // deaths by reaching SURFEL_TTL
const int SURFEL_CENSUS_DIED_POLICE  = 3;   // deaths by the despawn vote's kill signal
const int SURFEL_CENSUS_KEEP_ALIVE   = 4;   // surfels paid nonzero keep-alive income
const int SURFEL_CENSUS_SLOT_COUNT   = 5;

// 📝 The counters SSBO. Each including pass declares it at ITS OWN free binding (the lifecycle pool set uses binding 9) by defining
//    SURFEL_CENSUS_BINDING before the include — the block name is shared so the helpers below resolve in every pass.
#ifdef SURFEL_CENSUS_BINDING
layout(std430, binding = SURFEL_CENSUS_BINDING) buffer SurfelCensusBuffer { int SurfelCensusCounters[]; };

// Tally one event into a census slot. One atomicAdd on a branch the pass was already taking.
void SurfelCensusAdd(int Slot, int Amount)
{
    atomicAdd(SurfelCensusCounters[Slot], Amount);
}

void SurfelCensusTally(int Slot)
{
    atomicAdd(SurfelCensusCounters[Slot], 1);
}
#else
// 📝 No binding defined — the helpers compile to nothing, so a pass can include this header without carrying the buffer.
void SurfelCensusAdd(int Slot, int Amount) {}
void SurfelCensusTally(int Slot) {}
#endif

#endif
