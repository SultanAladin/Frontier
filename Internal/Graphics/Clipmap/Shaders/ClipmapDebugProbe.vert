// ==============================================================================================================================================
//                                                         CLIPMAPDEBUGPROBE.VERT
// ==============================================================================================================================================
// Development-only probe markers: one screen-space point per clipmap cell, placed at the cell centre. Only OCCUPIED cells (holding scene
// geometry) emit a marker — vacant and plain-resident cells collapse to a degenerate point (size 0) so the markers do not flood the view; the
// lattice alone carries vacant/resident residency. gl_PointSize is a small fixed pixel size per level (fine thin, coarse a little thicker).
// Colour tracks the relight-ramp STUB: a cell that just scrolled in reads violet (0) and climbs to orange (1) as it relights, which is what
// makes the streaming path visible.

#version 450

layout(std430, binding = 0) readonly buffer ClipmapCellBlock
{
    // Matches ClipmapCellRecord (32 bytes): centre.xyz, edge, category, level, ramp, pad.
    vec4 CellRows[];
} Cells;

layout(push_constant) uniform ClipmapInspectionConstants
{
    mat4  ViewProjection;
    float LineOpacity;
    float ProbePixelRadius;
    float ProbeOpacity;
    float VacantOpacity;
    float LatticeBaseWidth;
    float ViewportWidth;
    float ViewportHeight;
    float ConstantsPad0;
} Constants;

layout(location = 0) out vec4 FragmentColour;

// Per-level marker width in pixels — kept small so the occupied markers read as dots on the meshes, not blobs. A level past the table clamps
// to the last stride so a deep ladder never runs off the end.
const float ProbeLevelPixels[4] = float[4](5.0, 7.0, 9.0, 11.0);
const float ProbeMaximumPixels  = 14.0;

void main()
{
    const uint RecordBase = uint(gl_InstanceIndex) * 2u;
    const vec4 CentreRow  = Cells.CellRows[RecordBase];
    const vec4 PayloadRow = Cells.CellRows[RecordBase + 1u];

    const vec3  CellCentre  = CentreRow.xyz;
    const uint  Category    = floatBitsToUint(PayloadRow.x);
    const uint  LevelIndex  = floatBitsToUint(PayloadRow.y);
    const float RelightRamp = PayloadRow.z;

    gl_Position = Constants.ViewProjection * vec4(CellCentre, 1.0);

    // 📝 Marker width scales with the cell's LEVEL — level 0 (fine, near) is thin, each coarser level toward the larger boxes reads thicker,
    //    so the resolution ladder is legible from the probe width alone. The per-level table is the FINAL pixel width (the ProbePixelRadius
    //    knob is intentionally not multiplied in here — it stays the lattice/probe-opacity tuning), capped so the coarsest level cannot swallow
    //    the view. A vacant cell carries no payload — collapse its marker away.
    // Only occupied cells (category 2) show a marker; everything else collapses away so the yellow markers do not flood the field.
    const uint  LevelSlot    = min(LevelIndex, 3u);
    const float MarkerPixels = min(ProbeLevelPixels[LevelSlot], ProbeMaximumPixels);
    gl_PointSize = (Category == 2u) ? MarkerPixels : 0.0;

    // 📝 The ramp drives colour, not just brightness, so the relight sweep is unmistakable: an occupied cell reads bright violet the instant it
    //    scrolls in (ramp 0) and climbs to bright orange (ramp 1) as it relights — the same orange as the lattice occupied cage, so marker and
    //    cage agree. Violet -> orange is a full hue sweep, far easier to read than a brightness-only ramp.
    const vec3 UnlitColour = vec3(0.55, 0.15, 0.95);
    const vec3 LitColour   = vec3(1.00, 0.45, 0.00);
    const vec3 ProbeColour = mix(UnlitColour, LitColour, clamp(RelightRamp, 0.0, 1.0));

    const float LevelFade = 1.0 / (1.0 + 0.35 * float(LevelIndex));
    FragmentColour = vec4(ProbeColour, Constants.ProbeOpacity * LevelFade);
}
