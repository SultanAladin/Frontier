/*==============================================================================================================================================
                                                            ANALYTICGROUNDPLANE.VERT
==============================================================================================================================================*/
// 🧩 Fullscreen-triangle vertex stage for the analytic ground plane. Emits three clip-space vertices that cover the screen and, per vertex,
//    unprojects the near and far clip points into world space through the inverse view-projection. The fragment stage intersects the ray
//    between them against the y=0 plane, so no ground geometry is uploaded — the grid is reconstructed entirely from the camera matrices.

#version 450

layout(push_constant) uniform AnalyticGroundPlaneConstants
{
    mat4  InverseViewProjection;   // Clip → world
    vec4  CameraPosition;          // World-space eye (xyz); w unused
    vec4  MinorLineColour;         // rgb + a intensity
    vec4  MajorLineColour;         // rgb + a intensity
    vec4  DotColour;               // rgb + a intensity
    vec4  AxisLineColourX;         // +X axis rgb + intensity (red)
    vec4  AxisLineColourY;         // +Y axis rgb + intensity (green)
    vec4  AxisLineColourZ;         // +Z axis rgb + intensity (blue)
    float MinorSpacing;            // World units between minor lines
    float MajorSpacing;            // World units between major lines (dots ride its intersections)
    float GridExtent;              // Fade-out radius (world units) — the "grow to the extent we want" knob
    float LineThickness;           // Line half-width factor (pixels-ish, derivative-scaled)
    float DotPixelRadius;          // Dot radius in screen pixels (constant with distance)
    float AxisLineThickness;       // Origin-axis line half-width factor
    float LineLayerEnabled;        // 1 = draw the line grid
    float DotLayerEnabled;         // 1 = draw the dotted grid
    float AxisLayerEnabled;        // 1 = draw the origin axes
} Grid;

layout(location = 0) out vec3 EyeWorld;    // Ray origin: the world-space eye (exact push data), flat across the triangle
layout(location = 1) out vec3 NearPlaneWorld; // Per-pixel near-plane world point (w~1 unproject) — INTERPOLATED, gives the true per-pixel ray

// Unproject a clip-space point (z in [0,1] for Vulkan) into world space. Used ONLY for the near plane, where the homogeneous
// w is ~1 so the perspective divide keeps full float precision.
vec3 Unproject(vec2 Ndc, float Depth)
{
    vec4 Clip  = vec4(Ndc, Depth, 1.0);
    vec4 World = Grid.InverseViewProjection * Clip;
    return World.xyz / World.w;
}

void main()
{
    // Fullscreen triangle: three vertices whose NDC covers the whole screen with no vertex buffer.
    vec2 Corner = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 Ndc    = Corner * 2.0 - 1.0;

    // 🔴 Precision AND perspective: hand the fragment stage the exact eye plus the PER-PIXEL near-plane world point, and let it
    //    build the ray there. Two failure modes are avoided at once:
    //      • Far-clip divide — unprojecting the far clip point (Depth 1) divides by a huge FarClip.w (large far/near ratio), so
    //        the quotient keeps only a few significant bits and SNAPS as the camera rotates (the /\/\ shimmer). The near point
    //        has w ~1, so unprojecting it keeps full float precision.
    //      • Flattened (fake-ortho) ray — a normalized direction computed here and interpolated across only three fullscreen
    //        vertices does NOT reconstruct the true per-pixel ray (normalize is nonlinear), so the rays come out too parallel and
    //        the grid reads as orthographic. The near-plane WORLD point, by contrast, is a genuine unprojection of a linearly
    //        varying clip point, so it interpolates correctly — the fragment then takes Eye → this point as the true perspective
    //        ray. No normalize here, no far-clip divide anywhere. (SkyDome.frag reconstructs its ray per-pixel for the same reason.)
    EyeWorld       = Grid.CameraPosition.xyz;
    NearPlaneWorld = Unproject(Ndc, 0.0);

    gl_Position = vec4(Ndc, 0.0, 1.0);
}
