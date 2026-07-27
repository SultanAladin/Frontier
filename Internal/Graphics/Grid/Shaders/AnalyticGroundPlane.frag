/*==============================================================================================================================================
                                                            ANALYTICGROUNDPLANE.FRAG
==============================================================================================================================================*/
// 🧩 The analytic infinite ground plane (Blender-style), on the right-handed Z-up world frame (CoordinateSpace.h). Intersects the per-pixel camera
//    ray (near → far, from the vertex stage) against the z = 0 ground plane, then derives crisp anti-aliased minor + major lines, a dotted layer
//    locked to the major-line intersections, and the origin axes (X red, Y green) — all from the plane coordinates using screen-space derivatives
//    (fwidth), so lines and dots stay a constant pixel size at any distance. The grid fades to nothing past GridExtent, so it grows exactly as far
//    as the UI-driven extent asks and never shows a hard edge. Every visual knob arrives as push data — nothing is hardcoded.

#version 450

layout(push_constant) uniform AnalyticGroundPlaneConstants
{
    mat4  InverseViewProjection;
    vec4  CameraPosition;
    vec4  MinorLineColour;
    vec4  MajorLineColour;
    vec4  DotColour;
    vec4  AxisLineColourX;
    vec4  AxisLineColourY;
    vec4  AxisLineColourZ;
    float MinorSpacing;
    float MajorSpacing;
    float GridExtent;
    float LineThickness;
    float DotPixelRadius;
    float AxisLineThickness;
    float LineLayerEnabled;
    float DotLayerEnabled;
    float AxisLayerEnabled;
} Grid;

layout(location = 0) in  vec3 EyeWorld;        // Ray origin (flat: the exact eye)
layout(location = 1) in  vec3 NearPlaneWorld;  // Per-pixel near-plane world point (interpolated) — Eye → here is the true ray

layout(location = 0) out vec4 FragmentColour;

// Coverage of a grid line at ground position Coordinate for the given spacing. Distance to the nearest grid line in cells,
// divided by the derivative (line stays ~Thickness pixels wide regardless of zoom). Returns 0..1 coverage.
float LineCoverage(vec2 Coordinate, float Spacing, float Thickness)
{
    vec2 Cell       = Coordinate / Spacing;
    vec2 Derivative = fwidth(Cell);
    vec2 Distance   = abs(fract(Cell - 0.5) - 0.5) / max(Derivative, vec2(1e-8));
    float Nearest   = min(Distance.x, Distance.y);
    return 1.0 - clamp(Nearest - (Thickness - 1.0), 0.0, 1.0);
}

// Coverage of a round dot at the NEAREST major-line intersection (grid corner), holding a constant screen-pixel radius that
// gently SHRINKS into the distance — the look of the retired CPU painter, which projected each intersection and drew a fixed
// pixel circle scaled down by distance (GroundGrid.cpp, Shrink = 0.34 + 0.66·Fade). The earlier "phone-wire" build took the
// screen derivative of the RADIAL distance field, length(vec2(dFdx, dFdy)) of |Coordinate − Corner|; at glancing angles that
// scalar changes fast along the view ray, so its derivative blew up and INFLATED the dot the further it sat from the eye —
// the exact "far dots grow" artefact. The fix measures the offset-to-corner in PIXELS directly, per plane axis, using the
// well-conditioned Jacobian of the plane coordinate itself (fwidth(Coordinate) = world units per pixel on each axis). That
// pixel-space offset gives a true screen radius that stays put at any viewing angle; DistanceShrink then trims far dots so
// the distance reads as thinner, never thicker.
float DotCoverage(vec2 Coordinate, float Spacing, float PixelRadius, float DistanceShrink)
{
    vec2 Corner        = round(Coordinate / Spacing) * Spacing;
    vec2 WorldOffset   = Coordinate - Corner;                      // world offset to the nearest intersection, per axis

    // Per-axis world-units-per-pixel of the PLANE coordinate — stable at every angle (unlike the radial-field derivative).
    vec2 WorldPerPixel = max(fwidth(Coordinate), vec2(1e-8));
    vec2 PixelOffset   = WorldOffset / WorldPerPixel;             // offset to the corner, now in screen pixels
    float PixelRadial  = length(PixelOffset);                     // screen-pixel distance to the intersection

    float WantedRadius = max(PixelRadius * DistanceShrink, 0.0);  // far dots read thinner (retired painter's shrink)
    float EdgeBand     = 1.0;                                     // one-pixel anti-alias band, in pixels
    return smoothstep(WantedRadius + EdgeBand, WantedRadius - EdgeBand, PixelRadial);
}

// Coverage of a single axis line where the given ground coordinate crosses zero (the axis runs along the other coordinate).
// Screen-space width: the perpendicular world distance to the axis over the world-per-pixel scale gives a pixel distance.
float AxisCoverage(float PerpendicularCoordinate, float Thickness)
{
    // Angle-correct per-pixel scale of the perpendicular field (matches the dot method), so the axis line holds a constant
    // screen width even where it runs diagonally into the distance.
    float WorldPerPixel = max(length(vec2(dFdx(PerpendicularCoordinate), dFdy(PerpendicularCoordinate))), 1e-8);
    float PixelDistance = abs(PerpendicularCoordinate) / WorldPerPixel;
    return 1.0 - clamp(PixelDistance - Thickness, 0.0, 1.0);
}

void main()
{
    // Intersect the camera ray with the ground plane z = 0 (Z-up world). The ray is Eye → the per-pixel near-plane point, which
    // carries true perspective foreshortening (the near-plane point is a genuine per-pixel unprojection, not a flattened
    // per-vertex direction). Behind or parallel → nothing to draw.
    vec3  Direction = NearPlaneWorld - EyeWorld;
    float Parameter = -EyeWorld.z / Direction.z;
    if (Parameter <= 0.0)
        discard;

    vec3 Ground = EyeWorld + Parameter * Direction;
    vec2 Plane  = Ground.xy;   // the two horizontal (ground-plane) axes: X = right, Y = forward

    // Radial fade so the grid grows out to GridExtent and dissolves — no hard boundary.
    float Radius     = length(Plane - Grid.CameraPosition.xy);
    float ExtentFade = 1.0 - clamp(Radius / max(Grid.GridExtent, 1e-3), 0.0, 1.0);
    ExtentFade       = ExtentFade * ExtentFade;
    if (ExtentFade <= 0.0)
        discard;

    vec4 Accumulated = vec4(0.0);

    if (Grid.LineLayerEnabled > 0.5)
    {
        float MinorCover = LineCoverage(Plane, Grid.MinorSpacing, Grid.LineThickness);
        float MajorCover = LineCoverage(Plane, Grid.MajorSpacing, Grid.LineThickness);

        // Major lines dominate where they coincide with minor lines.
        vec4 MinorContribution = vec4(Grid.MinorLineColour.rgb, Grid.MinorLineColour.a * MinorCover);
        vec4 MajorContribution = vec4(Grid.MajorLineColour.rgb, Grid.MajorLineColour.a * MajorCover);
        Accumulated = mix(Accumulated, MinorContribution, MinorContribution.a);
        Accumulated = mix(Accumulated, MajorContribution, MajorContribution.a);
    }

    if (Grid.DotLayerEnabled > 0.5)
    {
        // Dots sit on the major-line intersections at a near-constant screen-pixel radius that trims down with distance,
        // mirroring the retired painter's Shrink = 0.34 + 0.66·Fade so far rows read thinner instead of swelling.
        float DistanceShrink = 0.34 + 0.66 * ExtentFade;
        float DotCover        = DotCoverage(Plane, Grid.MajorSpacing, Grid.DotPixelRadius, DistanceShrink);
        vec4  DotContribution = vec4(Grid.DotColour.rgb, Grid.DotColour.a * DotCover);
        Accumulated = mix(Accumulated, DotContribution, DotContribution.a);
    }

    if (Grid.AxisLayerEnabled > 0.5)
    {
        // The +X axis runs where Y = 0; the +Y axis runs where X = 0. (The +Z axis is vertical and cannot be drawn by a
        // ground-plane pass; its blue tints the shared origin crossing so all three colours read at the origin.)
        float AxisXCover = AxisCoverage(Plane.y, Grid.AxisLineThickness);   // red line along X
        float AxisYCover = AxisCoverage(Plane.x, Grid.AxisLineThickness);   // green line along Y

        vec4 AxisXContribution = vec4(Grid.AxisLineColourX.rgb, Grid.AxisLineColourX.a * AxisXCover);
        vec4 AxisYContribution = vec4(Grid.AxisLineColourY.rgb, Grid.AxisLineColourY.a * AxisYCover);
        Accumulated = mix(Accumulated, AxisXContribution, AxisXContribution.a);
        Accumulated = mix(Accumulated, AxisYContribution, AxisYContribution.a);

        // Origin marker: where both axes cross, blend the +Z (blue) colour so the origin shows all three axis colours.
        float OriginCover = AxisXCover * AxisYCover;
        vec4  OriginContribution = vec4(Grid.AxisLineColourZ.rgb, Grid.AxisLineColourZ.a * OriginCover);
        Accumulated = mix(Accumulated, OriginContribution, OriginContribution.a);
    }

    Accumulated.a *= ExtentFade;
    if (Accumulated.a <= 0.0)
        discard;

    FragmentColour = Accumulated;
}
