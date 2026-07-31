/*==============================================================================================================================================
                                                            ANALYTICGROUNDPLANE.FRAG
==============================================================================================================================================*/
// 🧩 The analytic infinite ground plane (Blender-style), on the right-handed Z-up world frame (CoordinateSpace.h). Intersects the per-pixel camera
//    ray (near → far, from the vertex stage) against the z = 0 ground plane, then derives crisp anti-aliased minor + major lines and a dotted layer
//    locked to the major-line intersections — all from the plane coordinates using screen-space derivatives, so lines and dots stay a constant pixel
//    size at any distance. The grid fades to nothing past GridExtent, so it grows exactly as far as the UI-driven extent asks and never shows a hard
//    edge. Every visual knob arrives as push data — nothing is hardcoded.
//
//    The three ORIGIN AXES (X red, Y green, Z blue) are measured against the pixel's ray instead of the ground plane, so all three exist as true 3D
//    lines — including the vertical +Z, which a ground-plane formulation cannot express at all — and they survive the orthographic side views where
//    the plane intersection is degenerate for every pixel. They composite on top without depth (this pass owns no depth attachment), reading as a
//    gizmo overlay, and run unbounded under a parallel projection while following the grid's own dissolve under perspective.
//
// 🐞 OCCLUSION (fixed): this pass owns no depth ATTACHMENT, so for its whole life it composited over everything — an infinite ground plane painted
//    on top of the objects standing on it, which read as the grid drawing through solid geometry. The fix is not a depth attachment (there is
//    nothing to write, and the scene depth is already resolved upstream) but a depth TEST done by hand: sample the renderer-owned scene depth the
//    visibility raster wrote, and reject the analytic hit wherever real geometry stands nearer along the same ray.
//
//    The compare runs in LINEAR VIEW METRES, not window depth, for the reason SelectionOutline.frag documents at length: with a near=0.1/far=1000
//    lens, everything past 5 m is crushed into the last 0.002 of the [0,1] window range, so any tolerance expressed against z_win is orders of
//    magnitude larger than the signal it is meant to resolve. Metres are uniform and mean what they say.

#version 450

layout(push_constant) uniform AnalyticGroundPlaneConstants
{
    mat4  InverseViewProjection;
    vec4  CameraPosition;
    vec4  FocalCentre;
    vec4  MinorLineColour;
    vec4  MajorLineColour;
    vec4  DotColour;
    vec4  AxisLineColourX;
    vec4  AxisLineColourY;
    vec4  AxisLineColourZ;
    float MinorSpacing;
    float MajorSpacing;
    float GridExtent;
    float FadeSharpness;
    float LineThickness;
    float DotPixelRadius;
    float AxisLineThickness;
    float LineLayerEnabled;
    float DotLayerEnabled;
    float AxisLayerEnabled;
    float OrthographicEnabled;
    float DepthNearPlane;           // [m] - near plane the frame's projection was built from; linearizes the sampled depth
    float DepthTestEnabled;         // [-] - 1 tests the scene depth, 0 restores the old draw-over-everything behaviour
} Grid;

// 📝 The renderer-owned scene depth (D32_SFLOAT), left in SHADER_READ_ONLY by the preamble. Read with texelFetch — a filtered depth is a blend of
//    two DISTANCES, which is meaningless at a silhouette edge (it would interpolate between an object and the sky behind it and place the surface
//    somewhere neither of them is), so this samples exactly one texel and never interpolates.
layout(set = 0, binding = 0) uniform sampler2D SceneDepthImage;

layout(location = 0) in  vec3 RayOrigin;      // Per-pixel ray origin (persp: the eye; ortho: this pixel's near-plane point)
layout(location = 1) in  vec3 RayTowardScene; // Per-pixel second ray point — Origin → here is the true ray in either lens

layout(location = 0) out vec4 FragmentColour;

// Linear view distance in metres from standard-Z window depth: z_view = near / (1 − z_win). The cleared far plane (1.0) would divide by zero, so
// clamp just below it — that reads as "very distant", which is exactly what an empty (sky) pixel is, and sky must never occlude the grid.
float LinearizeSceneDepth(float WindowDepth)
{
    float Bounded = min(WindowDepth, 0.9999999);
    return Grid.DepthNearPlane / max(1.0 - Bounded, 1e-9);
}

// Coverage of a grid line at ground position Coordinate for the given spacing, on the same phone-wire rule the dots use: the
// distance to the nearest line is measured in PIXELS (cell distance over the cell derivative), so the line holds Thickness pixels
// at any zoom. What the earlier build lacked was the other half of the rule — once cells pack below a pixel the line cannot get
// thinner without aliasing, so here the pixel spacing between lines also drives a dissolve. Without it the far field degenerates
// into shimmering moiré and eventually a solid wash, since every pixel lands on some line.
//
// Per-axis length(vec2(dFdx, dFdy)) rather than fwidth() for the same reason as the dots: fwidth over-estimates on diagonals.
float LineCoverage(vec2 Coordinate, float Spacing, float Thickness)
{
    vec2 Cell = Coordinate / Spacing;

    vec2 Derivative = max(vec2(length(vec2(dFdx(Cell.x), dFdy(Cell.x))),
                               length(vec2(dFdx(Cell.y), dFdy(Cell.y)))), vec2(1e-8));

    vec2  Distance = abs(fract(Cell - 0.5) - 0.5) / Derivative;    // pixel distance to the nearest line, per axis
    float Nearest  = min(Distance.x, Distance.y);
    float Coverage = 1.0 - clamp(Nearest - (Thickness - 1.0), 0.0, 1.0);

    // Pixel spacing between neighbouring lines on the narrow (limiting) axis — the same Nyquist gate the dots apply.
    float NarrowPitch = 1.0 / max(max(Derivative.x, Derivative.y), 1e-8);
    Coverage *= smoothstep(1.5, 6.0, NarrowPitch);

    return Coverage;
}

// Coverage of a round dot at the NEAREST major-line intersection (grid corner), built on PHONE-WIRE anti-aliasing (Emil Persson,
// popularised for grids by Ben Golus' "The Best Darn Grid Shader (Yet)"). The governing rule is: never draw a feature thinner
// than one pixel. Instead, floor the drawn radius at the pixel footprint and fade opacity by wanted/drawn — coverage is then
// conserved (a dot drawn 4x too wide at 1/4 opacity integrates to the same energy as the true sub-pixel dot), so the grid keeps
// its perspective falloff without ever handing the rasteriser a feature it cannot resolve.
//
// 🔴 Three earlier builds failed here, each by measuring the wrong thing:
//      • Deriving the screen radius from the RADIAL distance field, length(vec2(dFdx, dFdy)) of |Coordinate − Corner|. At glancing
//        angles that scalar changes fast along the view ray, so its derivative blew up and INFLATED far dots.
//      • Shrinking the radius by a WORLD-radius fade (DistanceShrink = 0.34 + 0.66·ExtentFade). World radius is decorrelated from
//        screen density: at a grazing angle the radius grows slowly while the on-screen dot PITCH collapses, so dots kept a large
//        radius exactly where they were packed tightest and merged into blobs and bands — read as "the grid expands into the
//        distance". A steep fade curve made it worse, holding near-full radius far out.
//      • Dividing the world offset PER AXIS by a per-axis world-per-pixel, then taking length(). That treats the pixel footprint as
//        an axis-aligned rectangle, so the "circle" it measures is an ELLIPSE in screen space — dots rendered as dashes smeared
//        along whichever axis was the more compressed. A ground plane seen at a grazing angle is exactly the anisotropic case:
//        the depth axis can be tens of times more compressed than the horizontal one.
//
// The correct measure transforms the offset through the full 2x2 JACOBIAN of screen-space-per-world, which is what actually maps a
// world displacement to a screen displacement, shear and all. Its inverse turns "world offset to the corner" into a genuine PIXEL
// vector, so length() of that vector is a true screen radius and the dot is round at every angle. The same Jacobian's column norms
// give the on-screen pitch used for the density clamps below.
float DotCoverage(vec2 Coordinate, float Spacing, float PixelRadius)
{
    vec2 Corner      = round(Coordinate / Spacing) * Spacing;
    vec2 WorldOffset = Coordinate - Corner;                        // world offset to the nearest intersection

    // Jacobian of the plane coordinate w.r.t. screen pixels: columns are how far the world moves per pixel of screen x / y.
    vec2 WorldPerPixelX = vec2(dFdx(Coordinate.x), dFdx(Coordinate.y));   // d(world)/d(screen x)
    vec2 WorldPerPixelY = vec2(dFdy(Coordinate.x), dFdy(Coordinate.y));   // d(world)/d(screen y)

    // Invert it to go world -> pixels. A grazing ground plane can make this near-singular, so guard the determinant.
    float Determinant = WorldPerPixelX.x * WorldPerPixelY.y - WorldPerPixelY.x * WorldPerPixelX.y;
    if (abs(Determinant) < 1e-12)
        return 0.0;

    // Inverse 2x2 applied to the world offset — the offset expressed in true screen pixels, shear included.
    vec2 PixelOffset = vec2(WorldPerPixelY.y * WorldOffset.x - WorldPerPixelY.x * WorldOffset.y,
                            WorldPerPixelX.x * WorldOffset.y - WorldPerPixelX.y * WorldOffset.x) / Determinant;

    float PixelRadial = length(PixelOffset);                       // isotropic: a circle on screen, not an ellipse

    // On-screen pitch between neighbouring dots. One world unit along plane-x maps to the screen vector that is column 0 of the
    // INVERSE Jacobian (world -> pixels), so its length is that axis' pixels-per-world; likewise column 1 for plane-y. Reusing the
    // inverse already computed above keeps this consistent with PixelOffset. The narrow axis limits how dense dots may get.
    vec2 PixelsPerWorldX = vec2( WorldPerPixelY.y, -WorldPerPixelX.y) / Determinant;   // screen delta per world x
    vec2 PixelsPerWorldY = vec2(-WorldPerPixelY.x,  WorldPerPixelX.x) / Determinant;   // screen delta per world y

    vec2  PitchPixels = vec2(Spacing) * vec2(length(PixelsPerWorldX), length(PixelsPerWorldY));
    float NarrowPitch = min(PitchPixels.x, PitchPixels.y);

    // Phone-wire: floor the drawn radius at ~half a pixel so a dot is never sub-pixel, and additionally cap it well under the
    // pitch so neighbours cannot touch however dense they get. Both clamps are screen-space, so neither can be defeated by angle.
    float WantedRadius = max(PixelRadius, 0.0);
    float DrawRadius   = max(min(WantedRadius, NarrowPitch * 0.28), 0.5);

    float EdgeBand = 1.5;                                          // smoothstep steepens the ramp; 1.5 px ~ a 1 px linear band
    float Coverage = smoothstep(DrawRadius + EdgeBand, DrawRadius - EdgeBand, PixelRadial);

    // Conserve coverage for whatever the floor widened: wanted/drawn, exactly Persson's fade.
    Coverage *= clamp(WantedRadius / DrawRadius, 0.0, 1.0);

    // Below Nyquist there is no detail left to resolve, so dissolve rather than alias. Fully out by a ~1.5 px pitch, fully
    // present by ~6 px; between those the far field thins smoothly to empty space instead of moiréing into a solid wash.
    Coverage *= smoothstep(1.5, 6.0, NarrowPitch);

    return Coverage;
}

// 🔴 The three origin axes are measured against the PIXEL'S RAY, not against the ground plane — the only formulation that can
//    draw all three. The ground-plane approach could not, for two independent reasons:
//      • No +Z axis is expressible. Every shaded pixel there IS a z = 0 intersection, so the geometry available to the pass
//        contains only the ground plane; a vertical line has no representation in it and blue could merely tint the origin.
//      • Side / front views under a PARALLEL projection lose all three. Those rays run parallel to the ground, so the plane
//        intersection is degenerate for EVERY pixel at once (not just along a horizon), the pass discards wholesale, and the
//        axes disappear with the grid — exactly where an axis reference matters most.
//    Closest approach between two 3D lines needs no projection matrix (which would not fit the 256-byte push budget anyway) and
//    behaves identically in both lenses, since the vertex stage already handed us a correct per-pixel ray for each.
//
// Returns the perpendicular distance from the ray to the axis, plus the along-axis coordinate of the closest point (so the
// caller can fade by distance from the origin) and the ray parameter (to reject the half-space behind the camera).
struct AxisApproach
{
    float Distance;      // [m] - shortest world distance between the pixel ray and the axis line
    float AlongAxis;     // [m] - signed coordinate along the axis at closest approach
    float RayParameter;  // [-] - position along the ray; negative means behind the eye
};

// ⚠️ Branch-FREE by construction. The caller feeds this result into screen-space derivatives, and a derivative taken in
//    divergent control flow is undefined — if one pixel of a 2x2 quad took a different path than its neighbours the axis width
//    would be garbage along exactly the near-degenerate lines this guard exists for. So the parallel case is handled by mixing
//    both answers rather than returning early: the singular branch is a ray aimed straight down an axis (looking along +Z from
//    directly overhead), where the closest-approach system loses rank and the plain point-line distance is the exact answer.
AxisApproach ApproachAxis(vec3 Origin, vec3 Direction, vec3 AxisDirection)
{
    // Standard skew-line closest approach. Origin is the ray start, the axis passes through the world origin.
    float DirectionDotAxis   = dot(Direction, AxisDirection);
    float Denominator        = 1.0 - DirectionDotAxis * DirectionDotAxis;  // Direction and AxisDirection are unit length
    float OriginDotDirection = dot(Origin, Direction);
    float OriginDotAxis      = dot(Origin, AxisDirection);

    float SafeDenominator = max(Denominator, 1e-7);

    // Skew (general) solution.
    float SkewRayParameter = (-OriginDotDirection + DirectionDotAxis * OriginDotAxis) / SafeDenominator;
    float SkewAlongAxis    = ( OriginDotAxis      - DirectionDotAxis * OriginDotDirection) / SafeDenominator;
    vec3  SkewOffset       = (Origin + Direction * SkewRayParameter) - AxisDirection * SkewAlongAxis;

    // Parallel solution: perpendicular component of the origin, which is constant along a parallel line.
    float ParallelAlongAxis = OriginDotAxis;
    vec3  ParallelOffset    = Origin - AxisDirection * ParallelAlongAxis;

    float ParallelWeight = 1.0 - smoothstep(1e-7, 1e-5, Denominator);      // 1 when parallel, 0 when well-conditioned

    AxisApproach Result;
    Result.Distance     = mix(length(SkewOffset), length(ParallelOffset), ParallelWeight);
    Result.AlongAxis    = mix(SkewAlongAxis,      ParallelAlongAxis,      ParallelWeight);
    // A parallel ray never passes "behind" the axis, so keep it accepted by reporting a non-negative parameter.
    Result.RayParameter = mix(SkewRayParameter,   0.0,                    ParallelWeight);
    return Result;
}

// Coverage of one origin axis, anti-aliased to a constant screen width. The world distance is converted to pixels through its
// own screen-space derivative, the same angle-correct measure the dots and lines use, so an axis running away into the distance
// keeps its thickness instead of tapering.
float AxisCoverage(float PerpendicularDistance, float Thickness)
{
    float WorldPerPixel = max(length(vec2(dFdx(PerpendicularDistance), dFdy(PerpendicularDistance))), 1e-8);
    float PixelDistance = PerpendicularDistance / WorldPerPixel;
    return 1.0 - clamp(PixelDistance - Thickness, 0.0, 1.0);
}

void main()
{
    // Intersect the camera ray with the ground plane z = 0 (Z-up world). The vertex stage already picked the right two points for
    // the active lens (eye → near plane for perspective, near → far for a parallel projection), so the same intersection serves
    // both: rays that genuinely converge under perspective and genuinely parallel ones under ortho.
    vec3 Direction = RayTowardScene - RayOrigin;

    // 🔴 The AXES are resolved from the ray FIRST, before any ground-plane test can discard this pixel. That ordering is the
    //    whole fix for the vanishing side views: the plane intersection below is legitimately degenerate for every pixel of an
    //    orthographic front/side view, so anything computed after it inherits that failure. The axes do not need the plane.
    vec3  AxisRayDirection = normalize(Direction);
    vec4  AxisAccumulated  = vec4(0.0);

    if (Grid.AxisLayerEnabled > 0.5)
    {
        AxisApproach ApproachX = ApproachAxis(RayOrigin, AxisRayDirection, vec3(1.0, 0.0, 0.0));
        AxisApproach ApproachY = ApproachAxis(RayOrigin, AxisRayDirection, vec3(0.0, 1.0, 0.0));
        AxisApproach ApproachZ = ApproachAxis(RayOrigin, AxisRayDirection, vec3(0.0, 0.0, 1.0));

        float CoverX = AxisCoverage(ApproachX.Distance, Grid.AxisLineThickness);
        float CoverY = AxisCoverage(ApproachY.Distance, Grid.AxisLineThickness);
        float CoverZ = AxisCoverage(ApproachZ.Distance, Grid.AxisLineThickness);

        // Reject the part of each axis that lies behind the eye, or the line would also be drawn mirrored into the pixels
        // behind the camera. Under a parallel projection RayOrigin sits on the near plane, so this stays correct there too.
        if (ApproachX.RayParameter < 0.0) { CoverX = 0.0; }
        if (ApproachY.RayParameter < 0.0) { CoverY = 0.0; }
        if (ApproachZ.RayParameter < 0.0) { CoverZ = 0.0; }

        // 🐞 Occlude each axis independently, on the same linear-metre compare the ground uses. Per-axis rather than once for the
        //    pixel because the three axes cross this ray at genuinely different distances — one can pass in front of an object
        //    while another passes behind it, and a single shared test would wrongly hide or reveal all three together.
        // ⚠️ Computed OUTSIDE the earlier control flow but applied here, so no screen-space derivative is taken in divergent flow
        //    (AxisCoverage above already consumed its derivatives). Zeroing coverage after the fact cannot corrupt a derivative.
        if (Grid.DepthTestEnabled > 0.5)
        {
            float SceneDistance = LinearizeSceneDepth(texelFetch(SceneDepthImage, ivec2(gl_FragCoord.xy), 0).r);
            if (SceneDistance < ApproachX.RayParameter * Grid.DepthNearPlane * 0.999) { CoverX = 0.0; }
            if (SceneDistance < ApproachY.RayParameter * Grid.DepthNearPlane * 0.999) { CoverY = 0.0; }
            if (SceneDistance < ApproachZ.RayParameter * Grid.DepthNearPlane * 0.999) { CoverZ = 0.0; }
        }

        // 📝 Reach: INFINITE under a parallel projection, grid-matched under perspective. In ortho a side view has no visible
        //    ground plane at all, so a radial fade would dissolve the axes into the same blank frame the grid leaves behind —
        //    unbounded lines are the only thing that actually reads there. Under perspective the grid IS present, so the axes
        //    ride its own dissolve curve and stay visually attached to it rather than streaking past its edge.
        if (Grid.OrthographicEnabled <= 0.5)
        {
            float ReachX = 1.0 - clamp(abs(ApproachX.AlongAxis) / max(Grid.GridExtent, 1e-3), 0.0, 1.0);
            float ReachY = 1.0 - clamp(abs(ApproachY.AlongAxis) / max(Grid.GridExtent, 1e-3), 0.0, 1.0);
            float ReachZ = 1.0 - clamp(abs(ApproachZ.AlongAxis) / max(Grid.GridExtent, 1e-3), 0.0, 1.0);
            float Sharpness = max(Grid.FadeSharpness, 0.01);
            CoverX *= pow(ReachX, Sharpness);
            CoverY *= pow(ReachY, Sharpness);
            CoverZ *= pow(ReachZ, Sharpness);
        }

        // Painter order by distance so the nearer axis wins where two cross (at the origin all three meet).
        vec4 ContributionX = vec4(Grid.AxisLineColourX.rgb, Grid.AxisLineColourX.a * CoverX);
        vec4 ContributionY = vec4(Grid.AxisLineColourY.rgb, Grid.AxisLineColourY.a * CoverY);
        vec4 ContributionZ = vec4(Grid.AxisLineColourZ.rgb, Grid.AxisLineColourZ.a * CoverZ);

        AxisAccumulated = mix(AxisAccumulated, ContributionX, ContributionX.a);
        AxisAccumulated = mix(AxisAccumulated, ContributionY, ContributionY.a);
        AxisAccumulated = mix(AxisAccumulated, ContributionZ, ContributionZ.a);
    }

    // ⚠️ A ray running along the ground plane never meets it. Guard the near-zero divide explicitly — under ortho a front/side
    //    view drives Direction.z to zero for EVERY pixel at once (not just a horizon line), so this branch is genuinely reached
    //    there. Only the GRID depends on the intersection; the axes above are already resolved, so emit them and stop.
    bool  GroundResolved = true;
    vec2  Plane          = vec2(0.0);
    float ExtentFade     = 0.0;

    if (abs(Direction.z) < 1e-9)
    {
        GroundResolved = false;
    }
    else
    {
        float Parameter = -RayOrigin.z / Direction.z;
        if (Parameter <= 0.0)
        {
            GroundResolved = false;
        }
        else
        {
            vec3 Ground = RayOrigin + Parameter * Direction;
            Plane       = Ground.xy;   // the two horizontal (ground-plane) axes: X = right, Y = forward

            // 🐞 Reject the ground hit when real geometry stands nearer on this pixel's ray — the occlusion fix. Note what is compared:
            //    Parameter is the ray parameter at the plane hit, and because Direction is exactly (RayTowardScene − RayOrigin) — the
            //    near-plane point minus the eye — Parameter is already expressed in units of "one near-plane distance". Multiplying by
            //    DepthNearPlane converts it to the SAME linear view metres LinearizeSceneDepth returns, so the two are directly comparable
            //    without reconstructing a view matrix or spending push budget on one.
            // ⚠️ Under ortho Direction spans near → far instead, so that identity does not hold and this scale would be wrong. The host
            //    leaves DepthTestEnabled at 0 for a parallel projection, which is also the honest choice for a lens whose whole purpose is
            //    seeing through the scene — see the note in GroundGridPass.h.
            if (Grid.DepthTestEnabled > 0.5)
            {
                float GroundDistance = Parameter * Grid.DepthNearPlane;
                float SceneDistance  = LinearizeSceneDepth(texelFetch(SceneDepthImage, ivec2(gl_FragCoord.xy), 0).r);

                // A relative slack, not a fixed one: depth precision degrades with distance under standard Z, so a constant metre
                // tolerance that is invisible at 1 m is far too tight at 200 m and would let the grid flicker through the floor mesh.
                if (SceneDistance < GroundDistance * 0.999)
                    GroundResolved = false;
            }
        }
    }

    // Radial fade so the grid grows out to GridExtent and dissolves — no hard boundary. FadeSharpness is the falloff exponent
    // rather than a hardcoded square: 1 fades perfectly linearly out to the extent (the grid reaches nearly its full radius
    // before thinning), while larger values pull the dissolve inward. Exposing it lets a UI trade reach against softness
    // without also moving the extent, which would change the grid's apparent scale.
    //
    // 📝 Measured from the view CENTRE (the orbit Target), not the eye. The two coincide closely enough under perspective, but a
    //    parallel projection's eye is an orbit-derived point that can sit arbitrarily far out along the view axis while the
    //    framing does not change — keying the fade to it would dissolve the grid the moment the camera pulled back, and a
    //    top-down ortho view (eye directly overhead) would fade radially from a point that is not where the user is looking.
    //    The Target is what the viewport is actually centred on in both lenses, so the dissolve stays concentric with the view.
    if (GroundResolved)
    {
        float Radius = length(Plane - Grid.FocalCentre.xy);
        ExtentFade   = 1.0 - clamp(Radius / max(Grid.GridExtent, 1e-3), 0.0, 1.0);
        ExtentFade   = pow(ExtentFade, max(Grid.FadeSharpness, 0.01));
        if (ExtentFade <= 0.0)
        {
            GroundResolved = false;   // past the grid's reach — the axes may still cover this pixel
        }
    }

    vec4 Accumulated = vec4(0.0);

    if (GroundResolved && Grid.LineLayerEnabled > 0.5)
    {
        float MinorCover = LineCoverage(Plane, Grid.MinorSpacing, Grid.LineThickness);
        float MajorCover = LineCoverage(Plane, Grid.MajorSpacing, Grid.LineThickness);

        // Major lines dominate where they coincide with minor lines.
        vec4 MinorContribution = vec4(Grid.MinorLineColour.rgb, Grid.MinorLineColour.a * MinorCover);
        vec4 MajorContribution = vec4(Grid.MajorLineColour.rgb, Grid.MajorLineColour.a * MajorCover);
        Accumulated = mix(Accumulated, MinorContribution, MinorContribution.a);
        Accumulated = mix(Accumulated, MajorContribution, MajorContribution.a);
    }

    if (GroundResolved && Grid.DotLayerEnabled > 0.5)
    {
        // Dots sit on the major-line intersections at a constant screen-pixel radius. Nothing world-space trims them: the dot
        // sizes itself against its own on-screen PITCH (see DotCoverage), which is what keeps far rows distinct instead of
        // merging. The radial ExtentFade below still dissolves the whole grid toward the extent — that is reach, not density.
        float DotCover        = DotCoverage(Plane, Grid.MajorSpacing, Grid.DotPixelRadius);
        vec4  DotContribution = vec4(Grid.DotColour.rgb, Grid.DotColour.a * DotCover);
        Accumulated = mix(Accumulated, DotContribution, DotContribution.a);
    }

    // The grid layers fade with reach; the axes carry their own (see above) and must NOT be scaled by the ground fade, which is
    // zero wherever the plane was never resolved.
    Accumulated.a *= ExtentFade;

    // Axes composite last so they read on top of the lines and dots they cross.
    Accumulated = mix(Accumulated, AxisAccumulated, AxisAccumulated.a);

    if (Accumulated.a <= 0.0)
        discard;

    FragmentColour = Accumulated;
}
