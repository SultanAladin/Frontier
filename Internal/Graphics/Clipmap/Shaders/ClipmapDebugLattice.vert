// ==============================================================================================================================================
//                                                        CLIPMAPDEBUGLATTICE.VERT
// ==============================================================================================================================================
// Development-only clipmap lattice: one instanced wire-cube per clipmap cell. No vertex buffer — the 12 edges are synthesized from
// gl_VertexIndex against a unit-cube corner table. Each edge is expanded into a screen-space QUAD (two triangles, 6 vertices) whose width in
// pixels scales with the cell's level, so a fine near cell draws thin lines and a coarse far cell draws thick ones — legible thickness with NO
// wideLines device feature (line-list rendering ignores lineWidth on most drivers). 12 edges * 6 vertices = 72 vertices per instance. The cell
// record comes from the storage buffer indexed by gl_InstanceIndex. Colour is chosen by category (vacant / resident / occupied), tinted per
// level so the near/far resolution ladder is readable.

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

// Per-level width MULTIPLIER on LatticeBaseWidth: level 0 (fine, near) thin, each coarser level toward the larger boxes thicker. A level past
// the table clamps to the last stride so a deep ladder never runs off the end.
const float LevelWidthScale[4] = float[4](1.0, 2.2, 3.6, 5.0);

// The 8 corners of a unit cube centred on the origin, edge 1.
vec3 ResolveCubeCorner(uint CornerIndex)
{
    return vec3(
        (CornerIndex & 1u) != 0u ?  0.5 : -0.5,
        (CornerIndex & 2u) != 0u ?  0.5 : -0.5,
        (CornerIndex & 4u) != 0u ?  0.5 : -0.5);
}

// The 12 edges as corner-index pairs (24 entries, 2 per edge).
const uint EdgeCorners[24] = uint[24](
    0u,1u, 2u,3u, 4u,5u, 6u,7u,     // edges along X
    0u,2u, 1u,3u, 4u,6u, 5u,7u,     // edges along Y
    0u,4u, 1u,5u, 2u,6u, 3u,7u);    // edges along Z

void main()
{
    // -- Decode this instance's cell record ------------------------------------------------------------------------------
    const uint RecordBase = uint(gl_InstanceIndex) * 2u;
    const vec4 CentreRow  = Cells.CellRows[RecordBase];
    const vec4 PayloadRow = Cells.CellRows[RecordBase + 1u];

    const vec3  CellCentre = CentreRow.xyz;
    const float CellEdge   = CentreRow.w;
    const uint  Category   = floatBitsToUint(PayloadRow.x);
    const uint  LevelIndex = floatBitsToUint(PayloadRow.y);

    // -- Which edge + which of the 6 quad vertices this invocation is --------------------------------------------------
    const uint EdgeIndex   = uint(gl_VertexIndex) / 6u;
    const uint QuadVertex  = uint(gl_VertexIndex) % 6u;

    const vec3 CornerA = ResolveCubeCorner(EdgeCorners[EdgeIndex * 2u]);
    const vec3 CornerB = ResolveCubeCorner(EdgeCorners[EdgeIndex * 2u + 1u]);

    const vec3 WorldA = CellCentre + CornerA * CellEdge;
    const vec3 WorldB = CellCentre + CornerB * CellEdge;

    // -- Project both endpoints, perspective-divide to NDC -------------------------------------------------------------
    const vec4 ClipA = Constants.ViewProjection * vec4(WorldA, 1.0);
    const vec4 ClipB = Constants.ViewProjection * vec4(WorldB, 1.0);
    const vec3 NdcA  = ClipA.xyz / max(ClipA.w, 0.0001);
    const vec3 NdcB  = ClipB.xyz / max(ClipB.w, 0.0001);

    // -- Screen-space edge direction + perpendicular (aspect-corrected so pixels are square) --------------------------
    const vec2 Aspect  = vec2(Constants.ViewportWidth, Constants.ViewportHeight);
    vec2 ScreenDir = (NdcB.xy - NdcA.xy) * Aspect;
    const float DirLength = length(ScreenDir);
    ScreenDir = (DirLength > 1e-5) ? ScreenDir / DirLength : vec2(1.0, 0.0);
    const vec2 ScreenPerp = vec2(-ScreenDir.y, ScreenDir.x);

    // Half-width in NDC: a pixel width mapped back through the viewport (NDC spans 2 across the full extent, so px → NDC is 2/extent).
    // The per-level table thins fine levels on the assumption they are the dense camera grid, but an OCCUPIED cage is the thing being inspected and
    // may sit on any level — voxelizing at level 0 for a legible per-surface shell would otherwise draw it at the table's thinnest stroke. So the
    // occupied cage takes a fixed stroke of its own instead of the level's, keeping it readable wherever the occupancy level is set.
    const uint  LevelSlot = min(LevelIndex, 3u);
    const float OccupiedWidthScale = 2.6;
    const float WidthScale = (Category == 2u) ? OccupiedWidthScale : LevelWidthScale[LevelSlot];
    const float HalfWidthPixels = 0.5 * Constants.LatticeBaseWidth * WidthScale;
    const vec2 NdcHalfWidth = ScreenPerp * (HalfWidthPixels * 2.0) / Aspect;

    // -- The 6 quad vertices: (A+, A-, B+) and (A-, B-, B+) ------------------------------------------------------------
    vec3  BaseNdc;
    float Sign;
    if (QuadVertex == 0u)      { BaseNdc = NdcA; Sign =  1.0; }
    else if (QuadVertex == 1u) { BaseNdc = NdcA; Sign = -1.0; }
    else if (QuadVertex == 2u) { BaseNdc = NdcB; Sign =  1.0; }
    else if (QuadVertex == 3u) { BaseNdc = NdcA; Sign = -1.0; }
    else if (QuadVertex == 4u) { BaseNdc = NdcB; Sign = -1.0; }
    else                       { BaseNdc = NdcB; Sign =  1.0; }

    const vec2 OffsetNdc = BaseNdc.xy + NdcHalfWidth * Sign;
    gl_Position = vec4(OffsetNdc, BaseNdc.z, 1.0);

    // -- Colour by category, tinted by level -----------------------------------------------------------------------------
    //    Vacant  : bright violet    (scrolled in, awaiting fill)
    //    Resident: bright royal blue (cached and valid)
    //    Occupied: bright orange    (resident AND holding scene geometry)
    //    The three hues are picked far apart on the wheel so the category of any cell reads at a glance against the scene.
    vec3  CellColour = vec3(0.62, 0.28, 1.00);
    float CellAlpha  = Constants.LineOpacity * Constants.VacantOpacity;
    bool  OccupiedCell = false;
    if (Category == 1u)
    {
        CellColour = vec3(0.15, 0.45, 1.00);
        CellAlpha  = Constants.LineOpacity;
    }
    else if (Category == 2u)
    {
        CellColour = vec3(1.00, 0.68, 0.12);   // bright orange — the mesh-holding cage, kept vivid and fully opaque
        CellAlpha  = 1.0;
        OccupiedCell = true;
    }

    // Coarser levels fade back so the fine camera grid stays dominant — but the occupied cage is exempt so it always reads bright, even though
    // it draws on a coarse occupancy level.
    const float LevelFade = OccupiedCell ? 1.0 : 1.0 / (1.0 + 0.35 * float(LevelIndex));
    FragmentColour = vec4(CellColour * LevelFade, CellAlpha * LevelFade);
}
