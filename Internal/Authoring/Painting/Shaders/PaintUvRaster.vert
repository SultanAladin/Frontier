/*==============================================================================================================================================
                                                              PAINTUVRASTER.VERT
==============================================================================================================================================*/
// 🧩 UV-space paint-stamp vertex shader. Rasterizes the model into its OWN texture space: each vertex is positioned at its UV
// 📝 (mapped UV [0,1] -> clip [-1,1]) so the triangle covers exactly the texels its surface owns. The world-space clip position
//    of the same vertex is carried to the fragment stage (as clip xy + w) so the fragment can reproject to the on-screen brush
//    circle and decide whether this texel is under the brush. Reuses the RenderVertex input contract (position @0, normal @12,
//    texcoord @24, stride 32); all brush inputs ride the push-constant block. No descriptor sets — the target IS the render pass.

#version 450

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUSH CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

layout(push_constant) uniform PaintStampBlock
{
    mat4  ModelViewProjection;   // [-]  - Projection . View . Model (world -> clip) for the fragment reprojection
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

layout(location = 0) in vec3 Position;   // [cm] - object-space position
layout(location = 1) in vec3 Normal;     // [-]  - object-space normal (unused this slice; kept for the shared vertex contract)
layout(location = 2) in vec2 TexCoord;   // [-]  - uv; drives the clip-space placement AND the texel this fragment paints

layout(location = 0) out vec3 OutClipProbe;   // [-] - world clip xy + w carried for the fragment reprojection (xy/w -> NDC)

//------------------------------------------------------------------------------------------------------------------------
//                                                             MAIN
//------------------------------------------------------------------------------------------------------------------------

void main()
{
    // 📝 Carry the world-space clip position so the fragment can divide xy by w to reach NDC, then map to canvas pixels and
    //    test the brush circle. Storing xy + w (not the full vec4) is enough — z is not read by the screen reprojection.
    vec4 WorldClip = PaintStamp.ModelViewProjection * vec4(Position, 1.0);
    OutClipProbe   = vec3(WorldClip.xy, WorldClip.w);

    // 📝 Place the vertex at its UV in clip space so the rasterizer fills exactly the texels this surface owns. This is the
    //    defining move of UV-space painting: the render target is the layer texture, not the screen.
    vec2 UvClip = TexCoord * 2.0 - 1.0;
    gl_Position = vec4(UvClip.x, UvClip.y, 0.0, 1.0);
}
