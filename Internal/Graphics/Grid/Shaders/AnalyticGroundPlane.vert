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
    vec4  FocalCentre;             // World point the view is centred on (orbit Target, xyz) — the radial fade origin; w unused
    vec4  MinorLineColour;         // rgb + a intensity
    vec4  MajorLineColour;         // rgb + a intensity
    vec4  DotColour;               // rgb + a intensity
    vec4  AxisLineColourX;         // +X axis rgb + intensity (red)
    vec4  AxisLineColourY;         // +Y axis rgb + intensity (green)
    vec4  AxisLineColourZ;         // +Z axis rgb + intensity (blue)
    float MinorSpacing;            // World units between minor lines
    float MajorSpacing;            // World units between major lines (dots ride its intersections)
    float GridExtent;              // Fade-out radius (world units) — the "grow to the extent we want" knob
    float FadeSharpness;           // Radial falloff exponent — 1 linear, higher dissolves the grid closer in
    float LineThickness;           // Line half-width factor (pixels-ish, derivative-scaled)
    float DotPixelRadius;          // Dot radius in screen pixels (constant with distance)
    float AxisLineThickness;       // Origin-axis line half-width factor
    float LineLayerEnabled;        // 1 = draw the line grid
    float DotLayerEnabled;         // 1 = draw the dotted grid
    float AxisLayerEnabled;        // 1 = draw the origin axes
    float OrthographicEnabled;     // 1 = parallel projection (rays share one direction, origins spread across the near plane)
} Grid;

layout(location = 0) out vec3 RayOrigin;     // Per-pixel ray origin  (persp: the eye, flat; ortho: the near-plane point)
layout(location = 1) out vec3 RayTowardScene; // Per-pixel ray target (persp: the near-plane point; ortho: the far-plane point)

// Unproject a clip-space point (z in [0,1] for Vulkan) into world space. Under PERSPECTIVE, call this for the near plane only:
// there w is ~1 so the divide keeps full float precision, whereas the far plane divides by a huge w and loses most of its bits.
// Under a PARALLEL projection that restriction does not apply — w is a constant 1 at every depth — so the ortho path below
// unprojects both planes safely.
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

    // 🔴 Precision AND perspective: hand the fragment stage two world points per pixel and let it build the ray there. Two
    //    failure modes are avoided at once:
    //      • Far-clip divide — unprojecting the far clip point (Depth 1) divides by a huge FarClip.w (large far/near ratio), so
    //        the quotient keeps only a few significant bits and SNAPS as the camera rotates (the /\/\ shimmer). The near point
    //        has w ~1, so unprojecting it keeps full float precision.
    //      • Flattened (fake-ortho) ray — a normalized direction computed here and interpolated across only three fullscreen
    //        vertices does NOT reconstruct the true per-pixel ray (normalize is nonlinear), so the rays come out too parallel and
    //        the grid reads as orthographic. A near-plane WORLD point, by contrast, is a genuine unprojection of a linearly
    //        varying clip point, so it interpolates correctly.
    //
    // 🐞 The two lenses need GENUINELY different rays, and forcing the perspective construction on both is what stretched the
    //    orthographic grid. Under perspective every ray fans out from one shared eye. Under a PARALLEL projection there is no
    //    such point: all rays share one DIRECTION and their origins spread across the near plane. CameraPosition is still a
    //    single orbit-derived point there, so building Eye → nearPlane made the rays fan from it anyway — a perspective ray set
    //    under an orthographic matrix. The plane intersection then walked the wrong distances and the grid sheared/stretched
    //    (worst at a shallow pitch, where the false convergence is most visible).
    //
    //    Ortho takes near → far instead: both endpoints unproject from the same NDC, so their difference is the true constant
    //    view direction and each pixel keeps its own parallel ray. The far-clip divide is harmless in this lens precisely
    //    because a parallel projection's w is a constant 1 — the precision trap that rules it out under perspective is absent.
    if (Grid.OrthographicEnabled > 0.5)
    {
        RayOrigin      = Unproject(Ndc, 0.0);
        RayTowardScene = Unproject(Ndc, 1.0);
    }
    else
    {
        RayOrigin      = Grid.CameraPosition.xyz;
        RayTowardScene = Unproject(Ndc, 0.0);
    }

    gl_Position = vec4(Ndc, 0.0, 1.0);
}
