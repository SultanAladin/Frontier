#version 450

// 🧩 Fragment stage of the visibility inscription: read one packed identity from the R32_UINT visibility buffer at this pixel,
//    turn it into a stable debug colour, and composite it over whatever the colour scope already holds (sky + grid). The empty
//    sentinel (all-ones) is DISCARDED so uncovered pixels keep the background — the Suzanne heads read as flat-shaded coloured
//    silhouettes against the live sky/grid, which is the on-screen confirmation the raster wrote correct ids. The pack layout
//    matches VisibilityInscription.vert's producer (VisibilityRaster.frag): partition in the high 12 bits, primitive in the low
//    20. A crisp triangle-boundary line is drawn wherever a neighbouring pixel carries a DIFFERENT id — a uniform one-pixel edge,
//    independent of the numeric id distance (this is a debug composite, not the deferred shade — that arrives with P4/P5).

layout(location = 0) in  vec2 FragTexCoord;
layout(location = 0) out vec4 OutColour;

layout(set = 0, binding = 0) uniform usampler2D VisibilityBuffer;

// The primitive-ordinal → authored-source-face table (one entry per emitted triangle, filled by UploadInscriptionSourceFaces).
// Topology mode reads SourceFace[primitive] to collapse a fan-triangulated ngon/quad back to its authored face: two triangles
// from the SAME face share a source-face ordinal, so the internal diagonal between them is NOT an edge. Empty (length 0) when a
// scene carried no provenance — the edge-key builder then falls back to the full identity, i.e. the per-triangle wireframe.
layout(set = 0, binding = 1) readonly buffer SourceFaceTable
{
    uint SourceFace[];
};

layout(push_constant) uniform InscriptionConstants
{
    uint ColourByPrimitive;   // [-] - 0 → colour by partition (per-head), 1 → colour by primitive (per-triangle)
    uint WireframeMode;       // [-] - 0 → per-triangle wireframe, 1 → per-source-face (authored ngon/quad/tri) wireframe
    uint Pad1;
    uint Pad2;
} Constants;

const uint PrimitiveBits     = 20u;
const uint PrimitiveMask     = (1u << PrimitiveBits) - 1u;
const uint VisibilitySentinel = 0xFFFFFFFFu;

// The comparison key a wireframe edge is drawn between: two adjacent pixels lie on a line when their keys differ. Triangle mode
// keys on the whole packed identity (partition + triangle ordinal), so every triangle boundary shows. Topology mode re-keys the
// triangle ordinal to its authored source face — internal triangulation diagonals (same face both sides) collapse away and only
// real ngon/quad/tri boundaries survive. The sentinel keeps its all-ones key so background stays distinct and the silhouette
// outlines. A key is always partition-tagged so two different heads never merge across a shared screen-space border.
uint EdgeKey(uint Identity)
{
    if (Identity == VisibilitySentinel)
        return VisibilitySentinel;
    uint Partition = Identity >> PrimitiveBits;
    if (Constants.WireframeMode == 0u || SourceFace.length() == 0)
        return Identity;
    uint Primitive = Identity & PrimitiveMask;
    if (Primitive >= uint(SourceFace.length()))
        return Identity;   // ordinal outside the provenance table — treat as its own triangle
    return (Partition << PrimitiveBits) | (SourceFace[Primitive] & PrimitiveMask);
}

// A cheap integer hash → bright, well-separated hue. Keeps neighbouring ids visually distinct so partitions / triangles read
// apart at a glance. Standard Wang-style bit-mix, then split into three channels.
vec3 HashToColour(uint Seed)
{
    Seed = (Seed ^ 61u) ^ (Seed >> 16u);
    Seed *= 9u;
    Seed  = Seed ^ (Seed >> 4u);
    Seed *= 0x27d4eb2du;
    Seed  = Seed ^ (Seed >> 15u);
    float R = float((Seed        ) & 255u) / 255.0;
    float G = float((Seed >>  8u ) & 255u) / 255.0;
    float B = float((Seed >> 16u ) & 255u) / 255.0;
    // Lift the darkest ids so nothing lands near-black against the sky.
    return 0.25 + 0.75 * vec3(R, G, B);
}

void main()
{
    ivec2 Texel    = ivec2(gl_FragCoord.xy);
    uint  Identity = texelFetch(VisibilityBuffer, Texel, 0).r;
    if (Identity == VisibilitySentinel)
        discard;   // no surface here — keep the sky / grid behind us

    uint Partition = Identity >> PrimitiveBits;
    uint Seed      = (Constants.ColourByPrimitive != 0u) ? Identity : Partition;

    vec3 Base = HashToColour(Seed);

    // Crisp wireframe: this pixel sits on a line when ANY 4-neighbour carries a different EDGE KEY from ours. A pure equality test
    // (not a derivative of the id NUMBER) draws a uniform one-pixel line — no thick/uneven strokes, no dashing — regardless of how
    // far apart the two ordinals happen to be. The key is per WireframeMode: the full identity (per-triangle) or the authored
    // source face (per-ngon/quad/tri, so internal triangulation diagonals vanish). Sentinel neighbours (background) key all-ones
    // and count as "different" so the outer silhouette is outlined. Edge pixels darken toward the base colour; interior stay full.
    ivec2 BufferSize = textureSize(VisibilityBuffer, 0);
    uint  OwnKey     = EdgeKey(Identity);
    bool  OnEdge     = false;
    // Thinner stroke: a boundary between two keys A|B otherwise darkens BOTH bordering pixels, so the visible line reads ~2px
    // wide. Draw the edge only on the LOWER-keyed side (OwnKey < NeighbourKey) — the higher side stays interior — so exactly one
    // pixel row/column lights per boundary and the wireframe is a true 1px line. Sentinel neighbours (all-ones, the largest key)
    // always sit ABOVE any surface key, so the outer silhouette still draws on the surface side and stays outlined.
    const ivec2 Offsets[4] = ivec2[4](ivec2(1, 0), ivec2(-1, 0), ivec2(0, 1), ivec2(0, -1));
    for (int Step = 0; Step < 4; ++Step)
    {
        ivec2 Sample       = clamp(Texel + Offsets[Step], ivec2(0), BufferSize - ivec2(1));
        uint  NeighbourKey = EdgeKey(texelFetch(VisibilityBuffer, Sample, 0).r);
        if (NeighbourKey != OwnKey && OwnKey < NeighbourKey)
        {
            OnEdge = true;
            break;
        }
    }

    float Shade = OnEdge ? 0.45 : 1.0;
    OutColour   = vec4(Base * Shade, 1.0);
}
