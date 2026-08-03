// ============================================================================================================================================
//                                                        PARAMETRICSKETCHCURVE.VERT
// ============================================================================================================================================
// 🧩 Vertex stage of the parametric-sketch thick-line outline rasterization. Each polyline segment is expanded on the CPU into a quad of FOUR
//    stride-32 RenderVertices; this shader turns that quad into a screen-constant-width ribbon. The packing REUSES the engine's one vertex
//    contract (position @0, normal @12, texcoord @24) with a bespoke meaning so no new upload path is needed:
//        InPosition  = THIS corner's segment endpoint A  (world cm)
//        InNormal    = the segment's OTHER endpoint B     (world cm) — the direction partner, so we can build the perpendicular
//        InTexCoord  = (SideSign, ArcLength): SideSign is ±1 (which edge of the ribbon this corner sits on); ArcLength is the running
//                      distance in mm along the polyline for the fragment linetype (dashed / centerline in Phase 4).
//    Both endpoints project through the SAME camera block the matcap uses, so outlines register 1:1 with the solid + grid. The offset is applied
//    in NDC as (HalfWidthPixels / ViewportSize) so the ribbon is CONSTANT PIXEL WIDTH at every zoom — the CAD requirement.
#version 450

layout(set = 0, binding = 0) uniform CameraBlock
{
    mat4 ViewProjection;   // world -> clip (orbit / ortho camera) — identical to the matcap block
    mat4 ViewMatrix;       // world -> camera (unused here; kept so the block matches ParametricSketchSurfaceCameraBlock byte-for-byte)
} Camera;

// 📝 Per-record push block: the half stroke width in PIXELS, the target's pixel extent, and the body colour + linetype. It is ONE block shared by
//    both stages, so the vertex + fragment declarations must be byte-for-byte identical (Vulkan requires it); each stage reads only the fields it
//    needs. The vertex stage uses HalfWidthPixels + the viewport extent; the fragment uses StrokeColour + the linetype fields (Phase 4).
layout(push_constant) uniform StrokeConstants
{
    float HalfWidthPixels;   // [px] - half the stroke thickness; the ribbon spans 2·this across the screen at any zoom
    float ViewportWidth;     // [px] - target width  (NDC->pixel scale x)
    float ViewportHeight;    // [px] - target height (NDC->pixel scale y)
    float Padding;           // [-]  - align StrokeColour to a 16-byte boundary
    vec4  StrokeColour;      // [-]  - linear RGBA of this body (fragment stage)
    float DashPeriodMm;      // [mm] - Phase 4 linetype (fragment stage)
    float DashDutyCycle;     // [-]  - Phase 4 linetype (fragment stage)
    float LineStyle;         // [-]  - Phase 4 linetype (fragment stage)
    float Reserved;          // [-]  - keep the tail 16-byte aligned
} Stroke;

layout(location = 0) in vec3 InPosition;   // segment endpoint A (this corner), world cm
layout(location = 1) in vec3 InNormal;     // segment endpoint B (partner),     world cm
layout(location = 2) in vec2 InTexCoord;   // (SideSign ±1, ArcLength mm)

layout(location = 0) out float FragArcLength;   // running arc length (mm) for the fragment linetype

void main()
{
    vec4 ClipA = Camera.ViewProjection * vec4(InPosition, 1.0);
    vec4 ClipB = Camera.ViewProjection * vec4(InNormal,   1.0);

    // 📝 Project both endpoints to NDC (perspective divide), guarding a zero/near-zero w so a segment straddling the camera plane does not
    //    explode. The screen-space segment direction is measured in PIXELS so the perpendicular offset is a true pixel width regardless of aspect.
    float WvalueA = max(abs(ClipA.w), 1e-6);
    float WvalueB = max(abs(ClipB.w), 1e-6);
    vec2 NdcA = ClipA.xy / WvalueA;
    vec2 NdcB = ClipB.xy / WvalueB;

    vec2 ViewportSize   = vec2(Stroke.ViewportWidth, Stroke.ViewportHeight);
    vec2 PixelDirection = (NdcB - NdcA) * 0.5 * ViewportSize;   // NDC [-1,1] spans the full extent -> ·0.5·size = pixels
    float PixelLength   = length(PixelDirection);
    // A degenerate (zero-length) segment has no direction; fall back to +X so the quad has some non-zero area instead of collapsing to a line.
    vec2 PixelTangent   = (PixelLength > 1e-5) ? (PixelDirection / PixelLength) : vec2(1.0, 0.0);
    vec2 PixelNormal    = vec2(-PixelTangent.y, PixelTangent.x);   // left-hand perpendicular in pixel space

    // 📝 Offset THIS corner (endpoint A) sideways by ±HalfWidthPixels, converted pixels -> NDC (·2/size), then re-attach A's own clip w so the
    //    perspective divide the rasterizer applies lands the ribbon at the intended screen width.
    float SideSign     = InTexCoord.x;
    vec2  PixelOffset  = PixelNormal * (SideSign * Stroke.HalfWidthPixels);
    vec2  NdcOffset    = PixelOffset * (2.0 / ViewportSize);
    vec2  NdcExpanded  = NdcA + NdcOffset;

    FragArcLength = InTexCoord.y;
    gl_Position   = vec4(NdcExpanded * WvalueA, ClipA.z, ClipA.w);
}
