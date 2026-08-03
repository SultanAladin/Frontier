/*==============================================================================================================================================
                                                            SURFELDEBUGSPLAT.FRAG
==============================================================================================================================================*/
// 🧩 The fragment half of the surfel DEBUG splat. Each point sprite is shaded as a soft round disc: gl_PointCoord runs [0,1] across the sprite, so
//    the distance from its centre carves a circle out of the square point and anti-aliases its rim. The colour arrives already resolved by mode from
//    the vertex stage (age / cascade / identity / occupancy) — this stage only shapes the disc and applies the alpha, then alpha-over blends onto the
//    radiance target. A culled slot arrives with SurfelFade == 0 and is discarded outright.
//
//    🔴 DEBUG-ONLY. No depth test (the splat owns no depth attachment, same as GroundGridPass): surfels draw over the shaded scene as an overlay so
//       coverage is legible even where a surfel sits just behind a surface. This is intentional for a diagnostic view and is why it is gated off by
//       default (mode 0) — it is never part of the shaded image Phase 2/3 build on.

#version 450

layout(location = 0) in flat vec4  SurfelColour;   // disc colour resolved per mode in the vertex stage
layout(location = 1) in flat float SurfelFade;     // 1 live, 0 culled

layout(location = 0) out vec4 FragmentColour;

void main()
{
    if (SurfelFade <= 0.0)
        discard;

    // Round the square point sprite into a disc. Distance from the sprite centre in [0, ~0.707]; fade the last texels for a soft anti-aliased rim.
    vec2  Offset   = gl_PointCoord - vec2(0.5);
    float Radial   = length(Offset) * 2.0;                  // 0 at centre, 1 at the sprite edge
    float Edge     = fwidth(Radial) * 1.5 + 1e-4;           // rim band ~ one pixel wide
    float Coverage = 1.0 - smoothstep(1.0 - Edge, 1.0, Radial);
    if (Coverage <= 0.0)
        discard;

    // A faint centre-bright falloff so overlapping discs still read as individual dots rather than a flat wash.
    float CentreLift = mix(0.75, 1.0, 1.0 - Radial);

    FragmentColour = vec4(SurfelColour.rgb * CentreLift, SurfelColour.a * Coverage);
}
