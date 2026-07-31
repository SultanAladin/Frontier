// ============================================================================================================================================
//                                                          SELECTIONOUTLINE.FRAG
// ============================================================================================================================================
// 🧩 Fragment stage of the object-selection outline: a fullscreen composite that reads the R32_UINT visibility buffer and draws an outline exactly
//    where the SELECTED partition ordinal borders a pixel that is NOT that ordinal. Nothing else is read — no depth texture, no second geometry
//    pass, no stencil. The id buffer was written under hardware depth test, so each pixel already names the partition NEAREST the camera there;
//    an outline derived from it is therefore depth-correct by construction.
//
// 💡 THE DEPTH BUG THIS AVOIDS. The naive outline re-draws the selected object with front-face cull / a scaled hull / a jump-flood over its
//    silhouette, then composites that ring over the frame. That ring is built from the object's OWN extent with no knowledge of what occludes it,
//    so an unselected object standing in front of the selected one gets the ring painted ON TOP of it — the selection reads as being in front of
//    geometry that actually occludes it. Sorting the draws does not fix it (the ring is not the object), and testing the ring against depth only
//    trades it for a second artefact: the ring vanishes wherever it touches the silhouette's own depth discontinuity.
//    Reading the resolved id buffer removes the failure at the source. A pixel belongs to the selected object here ONLY if the selected object won
//    the depth test at this pixel. An occluder in front simply owns those pixels, so no border is ever detected there and the outline is silently,
//    correctly clipped by the occluder — the same rule Blender's overlay engine relies on.
//
// 📝 Border rule: the pixel is INSIDE when its partition == SelectedPartition. It is an outline pixel when it is inside and at least one neighbour
//    within OutlineThickness is NOT inside (an INNER outline: the ring lands on the object's own pixels, never on its neighbours'). An inner ring is
//    what keeps the outline honest — an outer ring would have to paint pixels owned by whatever is behind/beside the object, which is exactly the
//    occlusion lie above. Empty pixels (the all-ones sentinel) are never inside, so an object silhouetted against sky outlines against the sky.
//
// ⚠️ THE ID BUFFER ALONE IS NOT ENOUGH, and this is the bug the depth tap below fixes. "My neighbour is a different ordinal" is TWO different
//    situations wearing one signal:
//      • the neighbour is sky or sits BEHIND me  → a true silhouette edge, outline it;
//      • the neighbour sits IN FRONT of me       → an OCCLUSION CONTOUR, and outlining it traces the occluder's shape onto my surface.
//    Selecting a large background object (the floor) made that unmistakable: every head standing on it got ringed, because the floor pixels hugging
//    each head satisfy "inside AND neighbour differs". Those really are floor pixels — nothing was painted over an occluder, the earlier inner-ring
//    rule held — but an outline is meant to say "here is my outer boundary", not to trace every hole punched in me. Depth is the only thing that
//    separates the two cases, so the neighbour test now also compares depth.
#version 450

layout(location = 0) in  vec2 FragTexCoord;
layout(location = 0) out vec4 OutColour;

// 📝 The visibility buffer as an unfiltered integer texture. usampler2D + nearest sampling: a filtered id is a meaningless average of two
//    unrelated ordinals, so this MUST stay point-sampled (the inscription's PointSampler supplies that).
layout(set = 0, binding = 0) uniform usampler2D IdentityImage;

// 📝 The scene depth the SAME raster wrote, so an id texel and a depth texel at one coordinate describe one surface. Point-sampled for the same
//    reason as the id: an averaged depth across a silhouette is a value no surface ever had.
// ⚠️ STANDARD Z, NOT REVERSE-Z. The raster uses VK_COMPARE_OP_LESS_OR_EQUAL and clears to 1.0 (VisibilityRasterization.cpp), so SMALLER depth is
//    NEARER and the cleared far plane is 1.0. Every comparison below depends on that direction; flipping the raster's compare op flips these too.
layout(set = 0, binding = 1) uniform sampler2D SceneDepthImage;

layout(push_constant) uniform OutlineConstants
{
    uint  SelectedPartition;    // [-]  - partition ordinal to outline; NoSelectionSentinel when nothing is selected
    uint  OutlineThickness;     // [px] - ring half-width in pixels (1..4 sane)
    uint  HoveredPartition;     // [-]  - partition ordinal under the cursor; NoSelectionSentinel when none
    uint  OccludedStyleEnabled; // [-]  - 1 = draw the dashed X-ray hint where the selection is hidden; 0 = hide it outright
    vec4  OutlineColour;        // [-]  - selected-outline RGBA
    vec4  HoverColour;          // [-]  - hovered-outline RGBA (drawn thinner, under the selection)
    vec4  OccludedColour;       // [-]  - dashed X-ray RGBA for the hidden stretch of the selected silhouette
    float DepthNearPlane;       // [m]  - camera near plane, for the window->view depth unproject (see LinearizeDepth)
    float DepthSlackMetres;     // [m]  - slack on the occlusion compare, in LINEAR view metres (see NeighbourOccludes)
    float DepthSlackRelative;   // [-]  - additional slack as a fraction of view distance (perspective-proportional)
    float DashPeriod;           // [px] - dash cycle length along the silhouette
    float DashDutyCycle;        // [-]  - lit fraction of each dash cycle (0..1)
    float Pad0;                 // [-]  - std430 tail pad
} Constants;

// 📝 Must match VisibilityRaster.frag / SoftwareRasterization.comp: primitive ordinal in the low 20 bits, partition ordinal in the high 12.
//    Only the partition half is read here — object selection is deliberately blind to which triangle was hit.
const uint PrimitiveBits = 20u;

// 📝 The "nothing selected" ordinal. Not a reachable partition (the raster packs 12 bits, so 0xFFFFFFFF cannot occur) and distinct from the
//    visibility buffer's own empty sentinel, which is a whole-word value rather than a partition ordinal.
const uint NoSelectionSentinel = 0xFFFFFFFFu;

// 📝 The cleared-pixel word. Unpacking it would yield partition 0xFFF, which IS a reachable ordinal, so an empty pixel must be rejected on the
//    whole word BEFORE the shift — otherwise the 4095th object would outline against the sky.
const uint VisibilityEmptySentinel = 0xFFFFFFFFu;

// Partition ordinal at a texel, or NoSelectionSentinel where the pixel is empty / out of bounds.
uint RetrievePartition(ivec2 Texel, ivec2 Bounds)
{
    if (Texel.x < 0 || Texel.y < 0 || Texel.x >= Bounds.x || Texel.y >= Bounds.y)
        return NoSelectionSentinel;

    uint Identity = texelFetch(IdentityImage, Texel, 0).r;
    if (Identity == VisibilityEmptySentinel)
        return NoSelectionSentinel;

    return Identity >> PrimitiveBits;
}

// True when Texel is inside Target's region — i.e. Target owns the nearest surface at that pixel.
bool InsideRegion(ivec2 Texel, ivec2 Bounds, uint Target)
{
    return RetrievePartition(Texel, Bounds) == Target;
}

// ⚠️ WINDOW DEPTH IS UNUSABLE FOR THIS COMPARISON — the bug that made the first version of this shader a no-op. With the engine's near=0.01 /
//    far=1000 lens, standard-Z window depth is z_win ≈ 1 − near/z_view, so the ENTIRE range from 5 m to infinity is crushed into the last 0.002 of
//    the [0,1] span (5 m → 0.998010, 10 m → 0.999010, 50 m → 0.999810). A genuine two-metre occlusion at viewing distance moves z_win by only
//    0.000167. Any slack expressed as a fraction of z_win is therefore a fraction of ~1.0 — the original 0.002 relative slack was TWELVE TIMES the
//    signal it was meant to tolerate, so no neighbour ever tested as nearer and every boundary fell through to "visible". Compare in LINEAR view
//    metres instead, where a slack in metres means what it says and the precision is uniform in the only unit the scene is authored in.
//
//    z_win = far/(far−near) · (1 − near/z_view)  ⇒  z_view = near / (1 − z_win)   (exact inverse, one divide)
float LinearizeDepth(float WindowDepth)
{
    // The cleared far plane (1.0) would divide by zero; clamp just below it and let it read as "very distant", which is what sky is.
    float Bounded = min(WindowDepth, 0.9999999);
    return Constants.DepthNearPlane / max(1.0 - Bounded, 1e-9);
}

// Linear view distance at a texel, in metres. Out-of-bounds reads the far plane so the frame border behaves like sky, not like an occluder.
float RetrieveViewDistance(ivec2 Texel, ivec2 Bounds)
{
    if (Texel.x < 0 || Texel.y < 0 || Texel.x >= Bounds.x || Texel.y >= Bounds.y)
        return LinearizeDepth(1.0);
    return LinearizeDepth(texelFetch(SceneDepthImage, Texel, 0).r);
}

// 📝 Does the neighbour at Texel OCCLUDE the centre — i.e. does it stand measurably NEARER the camera? Both distances are linear metres here, so
//    "measurably" is a real tolerance rather than a floating-point accident.
// 📝 The slack has two terms because the two error sources scale differently. A fixed floor (DepthSlackMetres) covers depth-buffer quantization and
//    the sub-pixel disagreement between an id texel and its depth texel at a silhouette. A proportional term (DepthSlackRelative · distance) covers
//    perspective foreshortening: at 200 m one pixel of screen space spans far more depth than at 2 m, so a grazing floor needs proportionally more
//    tolerance out there to avoid flickering between "occluded" and "silhouette" as the camera moves.
bool NeighbourOccludes(ivec2 Texel, ivec2 Bounds, float CentreDistance)
{
    float NeighbourDistance = RetrieveViewDistance(Texel, Bounds);
    float Slack             = Constants.DepthSlackMetres + Constants.DepthSlackRelative * CentreDistance;
    return NeighbourDistance < CentreDistance - Slack;
}

// 📝 The mirror test: does the neighbour sit measurably FARTHER, so our boundary faces open space (or sky) in that direction? Deliberately NOT the
//    negation of NeighbourOccludes — the band within ±Slack satisfies neither, and that gap is the point. It is the contact seam where a resting
//    object meets the surface under it, and letting it fall through to "visible" is precisely what painted a solid ring around every head.
bool NeighbourRecedes(ivec2 Texel, ivec2 Bounds, float CentreDistance)
{
    float NeighbourDistance = RetrieveViewDistance(Texel, Bounds);
    float Slack             = Constants.DepthSlackMetres + Constants.DepthSlackRelative * CentreDistance;
    return NeighbourDistance > CentreDistance + Slack;
}

// 📝 Inner-ring test, now depth-aware, classifying the edge into the two kinds that matter. Chebyshev (square) neighbourhood — a square ring reads
//    as uniform thickness on straight silhouette edges, where a cross/plus kernel visibly thins on diagonals.
//
//    For each differing neighbour we ask whether it is NEARER than us:
//      • nearer  → it occludes us; this stretch of our boundary is HIDDEN behind it   → OutOccluded
//      • farther → it is sky or sits behind us; this is our true silhouette           → OutVisible
//
// ⚠️ THE DECISION IS PER-NEIGHBOUR, NEVER PER-KERNEL, and both blunt versions of this rule are broken in opposite directions:
//      • "visible wins if ANY neighbour is not-nearer" → the original bug. Where a head stands ON the selected floor, the contact pixels sit at nearly
//        the same distance as the floor beneath them, land inside the slack, and count as not-nearer — so one contact pixel re-lights the whole
//        occlusion contour and every head gets a solid ring.
//      • "occluded wins if ANY neighbour is nearer"    → the opposite failure. On a curved silhouette almost every boundary pixel has SOME nearer
//        differing neighbour within the radius, so the solid ring collapses to dashed everywhere and the selection highlight disappears.
//    What actually matters is the DIRECTION each differing neighbour lies in. A neighbour that is sky or sits behind us proves our boundary faces open
//    space there → that stretch is genuinely visible. A neighbour that is nearer proves only that THAT direction is blocked. So the two flags are
//    accumulated independently over the kernel and both may end true; the blend site then prefers solid, which is now correct because OutVisible means
//    "at least one direction really is open" rather than "some direction was not measurably nearer".
void ClassifyBorder(ivec2 Centre, ivec2 Bounds, uint Target, int Radius, out bool OutVisible, out bool OutOccluded)
{
    OutVisible  = false;
    OutOccluded = false;

    if (Target == NoSelectionSentinel)          return;
    if (!InsideRegion(Centre, Bounds, Target))  return;   // inner ring: only the object's own pixels can carry it

    float CentreDistance = RetrieveViewDistance(Centre, Bounds);

    for (int OffsetY = -Radius; OffsetY <= Radius; ++OffsetY)
        for (int OffsetX = -Radius; OffsetX <= Radius; ++OffsetX)
        {
            if (OffsetX == 0 && OffsetY == 0) continue;
            ivec2 Neighbour = Centre + ivec2(OffsetX, OffsetY);
            if (InsideRegion(Neighbour, Bounds, Target)) continue;

            // 📝 Three outcomes, not two. Only a neighbour measurably FARTHER (or sky) proves the boundary faces open space; a neighbour measurably
            //    NEARER proves that direction is occluded. A neighbour inside the slack in EITHER direction is the contact seam where two surfaces
            //    meet — it proves nothing and must vote for neither, which is exactly what the original version got wrong by counting it as visible.
            if (NeighbourOccludes(Neighbour, Bounds, CentreDistance))
                OutOccluded = true;
            else if (NeighbourRecedes(Neighbour, Bounds, CentreDistance))
                OutVisible  = true;
        }
}

// 📝 The dash gate for the X-ray hint. Screen-space diagonal stripes: (x + y) marches along the boundary whichever way it runs, so a dash cadence
//    survives on horizontal, vertical, and diagonal stretches alike — keying off x alone would leave horizontal runs unbroken and vertical runs
//    solid. Screen-space (not arc-length) is deliberate: measuring true arc length needs a boundary walk this pass has no structure for, and the
//    stripe reads as "hidden" regardless, which is the whole job of the hint.
bool DashLit(ivec2 Texel)
{
    float Period = max(Constants.DashPeriod, 2.0);
    float Phase  = fract(float(Texel.x + Texel.y) / Period);
    return Phase < clamp(Constants.DashDutyCycle, 0.05, 0.95);
}

void main()
{
    ivec2 Bounds = textureSize(IdentityImage, 0);
    ivec2 Texel  = ivec2(FragTexCoord * vec2(Bounds));

    int  SelectedRadius = int(max(Constants.OutlineThickness, 1u));
    bool SelectedVisible, SelectedOccluded;
    ClassifyBorder(Texel, Bounds, Constants.SelectedPartition, SelectedRadius, SelectedVisible, SelectedOccluded);

    // 📝 Hover is drawn one pixel thinner and only where the selection outline is not already drawn, so selecting the hovered object does not
    //    double-draw the same ring. Hover on the ALREADY-selected object is suppressed entirely (the selection ring is the stronger signal).
    //    Hover takes only the VISIBLE half — a dashed hover ring on top of a dashed selection ring is noise, and hover is a transient cue anyway.
    bool HoverDistinct = Constants.HoveredPartition != Constants.SelectedPartition;
    bool HoverVisible = false, HoverOccluded = false;
    if (HoverDistinct)
        ClassifyBorder(Texel, Bounds, Constants.HoveredPartition, 1, HoverVisible, HoverOccluded);

    // 💡 Priority: a genuinely visible selection edge is the strongest signal, then the dashed hidden hint, then hover. The occluded ring is
    //    suppressed wherever the visible one already drew, so the two never fight over one pixel.
    if (SelectedVisible)
        OutColour = Constants.OutlineColour;
    else if (SelectedOccluded && Constants.OccludedStyleEnabled != 0u && DashLit(Texel))
        OutColour = Constants.OccludedColour;
    else if (HoverVisible)
        OutColour = Constants.HoverColour;
    else
        discard;   // 💡 discard, not a zero-alpha write: the composite blends over the shaded frame, so untouched pixels must keep whatever the
                   //    forward view already put there. Also the gap between dashes — which must show the scene, not a dark band.
}
