/*==============================================================================================================================================
                                                           SURFELRECORD.GLSL
==============================================================================================================================================*/
// 🧩 The std430 memory image of one surfel in the pool's surfel buffer, shared by every stage that reads or writes a surfel (count, slot, spawn, age,
//    debug). 🔴 FIELD ORDER IS EXACT and matches the host SurfelRecord (SurfelPool.h §14): posb (vec4, xyz world position + w spare) at byte 0, normal
//    (vec3) at byte 16, age (int) at byte 28 — 32-byte stride. GLSL std430 packs a trailing vec3 + int into the same 16-byte slot, so the struct is a
//    tight 32 bytes with no padding surprises. Any consumer that re-declares this layout differently mis-strides the whole array.
//
//    🔴 THIS IS AN #include MODULE — NO main, NO bindings. A consumer declares its own `buffer SurfelBuffer { SurfelRecord Surfels[]; }` at whatever
//       binding it needs; this only defines the record type and the small read helpers so the layout lives in exactly one place.

#ifndef FRONTIER_SURFEL_SURFELRECORD_GLSL
#define FRONTIER_SURFEL_SURFELRECORD_GLSL

struct SurfelRecord
{
    vec4 PositionAndSpare;   // @0  - xyz world position, w spare (webgiya posb)
    vec3 Normal;             // @16 - surface normal
    int  Age;                // @28 - lifecycle age; >= SURFEL_TTL is dead, SURFEL_LIFE_RECYCLED is the recycled sentinel
};

#endif
