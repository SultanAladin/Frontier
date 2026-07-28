#version 450

// 🧩 Fragment stage of the visibility inscription: read one packed identity from the R32_UINT visibility buffer at this pixel,
//    turn it into a stable debug colour, and composite it over whatever the colour scope already holds (sky + grid). The empty
//    sentinel (all-ones) is DISCARDED so uncovered pixels keep the background — the Suzanne heads read as flat-shaded coloured
//    silhouettes against the live sky/grid, which is the on-screen confirmation the raster wrote correct ids. The pack layout
//    matches VisibilityInscription.vert's producer (VisibilityRaster.frag): partition in the high 12 bits, primitive in the low
//    20. A tiny lambert-ish term from screen-space derivatives of the primitive hash gives the heads visible relief without any
//    normal / material read (this is a debug composite, not the deferred shade — that arrives with P4/P5).

layout(location = 0) in  vec2 FragTexCoord;
layout(location = 0) out vec4 OutColour;

layout(set = 0, binding = 0) uniform usampler2D VisibilityBuffer;

layout(push_constant) uniform InscriptionConstants
{
    uint ColourByPrimitive;   // [-] - 0 → colour by partition (per-head), 1 → colour by primitive (per-triangle)
    uint Pad0;
    uint Pad1;
    uint Pad2;
} Constants;

const uint PrimitiveBits     = 20u;
const uint PrimitiveMask     = (1u << PrimitiveBits) - 1u;
const uint VisibilitySentinel = 0xFFFFFFFFu;

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
    uint Primitive = Identity & PrimitiveMask;
    uint Seed      = (Constants.ColourByPrimitive != 0u) ? Identity : Partition;

    vec3 Base = HashToColour(Seed);

    // Fake relief so the silhouettes are not flat: shade by how fast the primitive index changes across the screen (triangle
    // edges become subtle creases). Pure debug cue — carries no lighting meaning.
    float Edge  = fwidth(float(Primitive));
    float Shade = clamp(1.0 - Edge * 0.02, 0.35, 1.0);

    OutColour = vec4(Base * Shade, 1.0);
}
