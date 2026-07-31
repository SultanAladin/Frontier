// ==============================================================================================================================================
//                                                         CLIPMAPDEBUGPROBE.FRAG
// ==============================================================================================================================================
// Development-only probe-marker fragment step: rounds the rasterized point sprite into a disc. gl_PointCoord runs 0..1 across the sprite, so
// the distance from its centre discards the corners and feathers the rim — a square marker would read as a solid block at these sizes.

#version 450

layout(location = 0) in  vec4 FragmentColour;
layout(location = 0) out vec4 SurfaceColour;

void main()
{
    const float CentreDistance = length(gl_PointCoord - vec2(0.5));
    if (CentreDistance > 0.5)
        discard;

    // Feather the outer rim so the disc does not alias hard against the scene.
    const float RimFade = 1.0 - smoothstep(0.35, 0.5, CentreDistance);
    SurfaceColour = vec4(FragmentColour.rgb, FragmentColour.a * RimFade);
}
