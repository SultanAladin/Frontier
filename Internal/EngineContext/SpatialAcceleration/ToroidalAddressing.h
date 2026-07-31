/*==============================================================================================================================================
                                                          TOROIDALADDRESSING.H
==============================================================================================================================================*/
// 🧩 The integer arithmetic a scrolling window is made of, hoisted so more than one window can stand on it: positive modulo (the toroidal wrap
//    itself), signed floor-to-cell (metres -> the lattice cell containing them), and the per-axis exposed-strip solve that turns "the window
//    moved by N cells" into "these cells were NOT in the old window". Pure integer / scalar math — no lattice type, no dimensionality, no
//    Vulkan, and deliberately no notion of what a cell CONTAINS.
//
//    Two windows share this and nothing else. ToroidalClipmapField is a 3D WORLD-space voxel lattice for GI probes; SunShadowClipmap is a 2D
//    LIGHT-space tile window whose basis rotates with the sun. That difference is why they are separate structures rather than one generic
//    lattice — see the ruling below — and why what is shared here is arithmetic rather than storage.
//
// 🔴 HEADER-ONLY, and it must stay that way. Executables/Validation/{ClipmapFieldValidation,TriangleCellOverlapValidation}/Build.bat each
//    compile ToroidalClipmapField.cpp standalone, listing their sources explicitly. A sibling .cpp in this folder would not be added to those
//    lists, so every function it defined would link-fail in exactly the two targets that gate this math. Everything here is `inline` for that
//    reason, not for speed.
//
// 📝 🔴 The "shared spine" the older notes promise does not exist. REFERENCE-ToroidalClipmapGI.md and PLAN-VisibilityRenderer.md §7.3/§8.3 say
//    one toroidal structure serves both the sun shadows and the GI probes. It cannot: shadow pages are addressed in light space, which ROTATES
//    with the sun, while probe voxels are addressed in world space and are rigidly axis-aligned. There is no assignment of meanings to X/Y/Z
//    that makes a world-aligned lattice light-aligned — unifying them would need a basis-rotation member on the lattice, which poisons every
//    world-aligned consumer. The honest scope of sharing is this file. (PLAN-SunShadowClipmap.md §3.)

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_SPATIALACCELERATION_TOROIDALADDRESSING_H
#define FRONTIER_ENGINECONTEXT_SPATIALACCELERATION_TOROIDALADDRESSING_H

#include <cmath>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        ADDRESSING
//------------------------------------------------------------------------------------------------------------------------

// Positive modulo: the result lands in [0, Modulus) even when Value is negative. Modulus must be > 0.
// 🔴 Not interchangeable with the built-in `%`. C++ truncates toward zero, so -1 % 32 is -1, and a window whose origin has scrolled negative
//    would index one cell BEFORE its own storage — an out-of-bounds read that reports as corrupt cell contents rather than as a crash. The
//    whole point of a toroidal window is that world cells left of the origin wrap to the far side, which is what this branch restores.
[[nodiscard]] inline int32_t WrapToroidalIndex(int32_t Value, int32_t Modulus)
{
    const int32_t Remainder = Value % Modulus;
    return (Remainder < 0) ? Remainder + Modulus : Remainder;
}

// Signed floor-divide of a metric coordinate by a cell edge -> the lattice cell containing it.
// 🔴 Floors toward NEGATIVE INFINITY, which is what makes the cell lattice uniform across the origin. A C++ integer cast truncates toward zero
//    instead, so both -0.5 and +0.5 would land in cell 0 and that one cell would be twice as wide as every other — a seam through the origin
//    that shifts every downstream address on one side of the world.
[[nodiscard]] inline int32_t FloorToLatticeCell(float Coordinate, float CellMetres)
{
    return (int32_t)std::floor(Coordinate / CellMetres);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       EXPOSED STRIPS
//------------------------------------------------------------------------------------------------------------------------

// One axis's newly-exposed strip, expressed as a half-open cell range [MinimumCell, MaximumCell) along the axis that moved. The caller holds
// the other axes at the window's full extent, so this type is dimension-agnostic: a 2D window uses two of them, a 3D window three.
struct ToroidalStrip
{
    int32_t Axis        = 0;   // [-]    - which axis moved (caller's own numbering)
    int32_t MinimumCell = 0;   // [cell] - inclusive lower bound along Axis
    int32_t MaximumCell = 0;   // [cell] - exclusive upper bound along Axis
};

// Solve the strip exposed along ONE axis by a scroll of CornerDelta cells, for a window now spanning [WindowMinimum, WindowMinimum + Resolution).
// Returns false when the axis did not move, in which case no strip is written.
//
// 📝 Moving +N exposes the TOP |N| cells of the new window; moving -N exposes the BOTTOM |N|. Both are clamped to Resolution, which is what
//    makes a teleport degrade gracefully: a jump farther than the window's own width exposes the whole window rather than a nonsense range
//    wider than the storage.
// ⚠️ Per-axis strips OVERLAP at the corners when two axes move in the same image. That is intended and must not be "optimized" away by
//    subtracting the intersection — re-marking a corner cell is idempotent, whereas a subtraction that is even slightly wrong leaves a corner
//    stale, and a stale corner is a cell holding another region's contents.
[[nodiscard]] inline bool SolveToroidalStrip(int32_t        Axis,
                                             int32_t        CornerDelta,
                                             int32_t        WindowMinimum,
                                             int32_t        Resolution,
                                             ToroidalStrip& OutStrip)
{
    if (CornerDelta == 0)
        return false;

    const int32_t Magnitude   = (CornerDelta < 0 ? -CornerDelta : CornerDelta);
    const int32_t Clamped     = (Magnitude < Resolution) ? Magnitude : Resolution;
    const int32_t WindowLimit = WindowMinimum + Resolution;

    OutStrip.Axis = Axis;
    if (CornerDelta > 0)
    {
        OutStrip.MinimumCell = WindowLimit - Clamped;
        OutStrip.MaximumCell = WindowLimit;
    }
    else
    {
        OutStrip.MinimumCell = WindowMinimum;
        OutStrip.MaximumCell = WindowMinimum + Clamped;
    }
    return true;
}

} // namespace Frontier

#endif
