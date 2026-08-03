/*==============================================================================================================================================
                                                            SURFELDEBUGSPLAT.FRAG
==============================================================================================================================================*/
// 🧩 The fragment half of the surfel DEBUG splat. Each point sprite is shaded as a soft round disc: gl_PointCoord runs [0,1] across the sprite, so
//    the distance from its centre carves a circle out of the square point and anti-aliases its rim. The colour arrives already resolved by mode from
//    the vertex stage (age / cascade / identity / occupancy) — this stage only shapes the disc and applies the alpha, then alpha-over blends onto the
//    radiance target. A culled slot arrives with SurfelFade == 0 and is discarded outright.
//
//    🔴 DEPTH-REJECT (not a depth attachment). The splat still owns no depth attachment — it samples the scene's D32 depth (SHADER_READ_ONLY at
//       radiance time) at gl_FragCoord and DISCARDS any surfel fragment behind the scene surface. So a near surface occludes the surfels behind it
//       and the field reads front-to-back with real parallax instead of a flat wash of every surfel in the frustum. This is what turns "a cloud of
//       same-size spheres" into a legible view. The compare is window-space depth vs the surfel centre's NDC z (a small bias absorbs the point-sprite
//       spread). A surfel IN FRONT of the surface (or where the depth buffer is the cleared far plane) always passes.

#version 450

layout(location = 0) in flat vec4  SurfelColour;   // disc colour resolved per mode in the vertex stage
layout(location = 1) in flat float SurfelFade;     // 1 live, 0 culled
layout(location = 2) in flat float SurfelDepthNdc; // [0,1] window-space depth of the surfel centre (from the vertex stage)

// The scene depth target (D32_SFLOAT), SHADER_READ_ONLY at radiance time. Sampled at the fragment's own pixel to depth-reject occluded surfels.
layout(set = 0, binding = 3) uniform sampler2D SceneDepth;

layout(location = 0) out vec4 FragmentColour;

void main()
{
    if (SurfelFade <= 0.0)
        discard;

    // Depth-reject: read the scene depth at THIS pixel and drop the fragment if the surfel sits behind the surface there. texelFetch by integer pixel
    // (gl_FragCoord.xy is pixel-centered) reads the exact stored depth with no filtering. The bias tolerates the point-sprite's screen spread so a
    // surfel glued to a surface is not self-occluded by its own disc edge. A cleared far-plane depth (1.0) never rejects, so open sky keeps its surfels.
    float SceneZ = texelFetch(SceneDepth, ivec2(gl_FragCoord.xy), 0).r;
    if (SurfelDepthNdc > SceneZ + 1e-4)
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
