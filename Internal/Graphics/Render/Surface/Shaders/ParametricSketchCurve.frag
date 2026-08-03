// ============================================================================================================================================
//                                                        PARAMETRICSKETCHCURVE.FRAG
// ============================================================================================================================================
// 🧩 Fragment stage of the parametric-sketch thick-line outline rasterization. Shades one ribbon fragment with the per-body stroke colour carried
//    on the push block. The interpolated arc length (mm along the polyline) is consumed here so Phase 4 can add dashed / centerline linetypes as a
//    SHADER-ONLY edit (a fract() discard on ArcLength) without touching the pipeline or the C++ — for Phase 3 every fragment is solid.
#version 450

layout(push_constant) uniform StrokeConstants
{
    float HalfWidthPixels;
    float ViewportWidth;
    float ViewportHeight;
    float Padding;
    vec4  StrokeColour;    // [-] - linear RGBA of this body (from the shape TintIndex)
    float DashPeriodMm;    // [mm] - Phase 4: dash cycle length; 0 disables (solid) — read now so the block matches the C++ struct
    float DashDutyCycle;   // [-]  - Phase 4: lit fraction of a dash cycle (0..1)
    float LineStyle;       // [-]  - Phase 4: 0 solid / 1 dashed / 2 centerline (float so the push block stays 16-aligned)
    float Reserved;        // [-]  - keep the tail on a 16-byte boundary
} Stroke;

layout(location = 0) in float FragArcLength;   // running arc length (mm) along the polyline

layout(location = 0) out vec4 OutColour;

// 📝 Fold the linetype off the interpolated arc length: normalise the mm distance to a 0..1 phase within one dash cycle, then discard the fragment
//    in the GAP portion (phase >= DutyCycle). A period of 0 (LineStyle 0, solid) skips the discard entirely — every fragment survives. This is the
//    ONLY Phase-4 edit; the CPU sequence maps LineStyle -> DashPeriodMm / DashDutyCycle, so a linetype tweak never touches this shader again.
//    ArcLength is in world mm (the CPU accumulates it PRE-scale), so a period expressed in mm reads true regardless of the cm render scale.
void main()
{
    if (Stroke.DashPeriodMm > 0.0)
    {
        float Phase = fract(FragArcLength / Stroke.DashPeriodMm);   // 0..1 position within one dash cycle
        if (Phase >= Stroke.DashDutyCycle)
            discard;                                                // the unlit gap of the dash / centerline cycle
    }
    OutColour = Stroke.StrokeColour;
}
