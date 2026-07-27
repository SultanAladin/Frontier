/*==============================================================================================================================================
                                                              PAINTUVRASTER.FRAG
==============================================================================================================================================*/
// 🧩 UV-space paint-stamp fragment shader. Runs once per texel the model owns (the vertex stage placed the triangle at its UV).
// 📝 Each texel reprojects its world position to the on-screen canvas, measures its distance to the brush centre, and discards
//    unless it falls inside the brush circle. A smoothstep from Hardness*Radius to Radius gives the soft edge; the deposited
//    alpha folds Opacity, Flow, and that falloff. The colour attachment is the layer's Albedo image, blended src-alpha over —
//    so repeated stamps along a drag accumulate toward the brush colour. Behind-the-eye texels (clip w <= 0) discard.

#version 450

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUSH CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

layout(push_constant) uniform PaintStampBlock
{
    mat4  ModelViewProjection;   // [-]  - Projection . View . Model (world -> clip); the vertex stage carried the probe
    vec2  BrushCentre;           // [px] - stroke sample centre in canvas pixels
    float BrushRadius;           // [px] - brush footprint radius on screen
    float Hardness;              // [0-1]- inner solid fraction; the falloff runs from Hardness*Radius to Radius
    float Opacity;               // [0-1]- overall stroke strength
    float Flow;                  // [0-1]- per-stamp deposit fraction
    vec4  BrushColour;           // [-]  - straight albedo RGBA the stamp deposits
    vec2  TargetExtent;          // [px] - canvas width/height the reprojection maps NDC into
} PaintStamp;

//------------------------------------------------------------------------------------------------------------------------
//                                                            INPUTS
//------------------------------------------------------------------------------------------------------------------------

layout(location = 0) in vec3 InClipProbe;    // [-] - world clip xy + w from the vertex stage (xy/w -> NDC)

layout(location = 0) out vec4 OutColour;     // [-] - deposited albedo, src-alpha over the layer image

//------------------------------------------------------------------------------------------------------------------------
//                                                             MAIN
//------------------------------------------------------------------------------------------------------------------------

void main()
{
    if (InClipProbe.z <= 0.0) discard;                             // behind the eye (clip w) — not visible, do not paint

    // 📝 Perspective divide to NDC, then map to canvas pixels. The Y flip mirrors SurfaceForward.vert's clip-Y compensation
    //    so the reprojected pixel lands where the surface actually draws on screen (Vulkan clip is Y-down vs the GL projection).
    vec2 Ndc    = InClipProbe.xy / InClipProbe.z;
    vec2 Screen = vec2((Ndc.x * 0.5 + 0.5), (Ndc.y * -0.5 + 0.5)) * PaintStamp.TargetExtent;

    float Reach = length(Screen - PaintStamp.BrushCentre);
    if (Reach > PaintStamp.BrushRadius) discard;                  // outside the brush circle

    // 📝 Soft edge: solid inside Hardness*Radius, fading to zero at Radius. A hardness of 1 gives a crisp disc.
    float InnerEdge = PaintStamp.BrushRadius * PaintStamp.Hardness;
    float FallOff   = 1.0 - smoothstep(InnerEdge, PaintStamp.BrushRadius, Reach);
    float Strength  = PaintStamp.Opacity * PaintStamp.Flow * FallOff;

    OutColour = vec4(PaintStamp.BrushColour.rgb, PaintStamp.BrushColour.a * Strength);
}
