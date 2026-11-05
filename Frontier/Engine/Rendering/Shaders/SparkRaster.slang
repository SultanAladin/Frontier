/*==========================================================================================================================================
                                                          SPARKRASTER.SLANG
                                     ------------------------------------------------------------------
                   Sparks as streaks: each one a screen-aligned quad from where the camera
                             saw it a frame ago to where it sees it now, additive.
==========================================================================================================================================*/

// 🧩 Sparks as streaks: the Slang form of the demo's SparkRaster.wgsl, 1:1.
//
// 📝 Per instance (SparkSequence): the head (now) and the tail (a frame ago) in world space, the
//    spark's width and its blackbody radiance. The head projects through this frame's camera and the
//    tail through last frame's, so the streak is the spark's motion as the camera saw it over the
//    frame — a shutter's blur: sparks thrown off a wheel stream back from a car the camera follows.
//    The quad spans head to tail in screen space, at least a pixel wide so a distant spark stays
//    visible and never longer than a fifth of the screen (a camera cut), with a soft profile across
//    and a brighter head; drawn over the resolved image (unjittered), depth-tested against the opaque
//    surface, added.

public struct SparkFrameRecord
{
    public float4x4 UnjitteredViewProjection;
    public float4x4 PreviousUnjitteredViewProjection;
    public float4x4 View;
    public float2  ScreenExtent;
    public float   EmissionScale;
    public float   Reserved;
};

[[vk::binding(0, 0)]] public ConstantBuffer<SparkFrameRecord> SparkFrame;

public struct SparkVarying
{
    public float4 Position : SV_Position;
    public float3 Radiance : TEXCOORD0;
    public float2 Shape : TEXCOORD2;      // x pixel-width factor, y shutter fraction
    public float2 Profile : TEXCOORD1;    // x across [-1, 1], y along [0 tail, 1 head]
};

float2 ProjectPixels(float4 Clip)
{
    return Clip.xy / Clip.w * 0.5 * SparkFrame.ScreenExtent;
}

[shader("vertex")]
public SparkVarying SparkVertex(uint VertexIndex : SV_VertexID,
                                float4 Head : TEXCOORD0,
                                float4 Tail : TEXCOORD1,
                                float4 Radiance : TEXCOORD2)
{
    SparkVarying Varying;
    Varying.Radiance = float3(0.0);
    Varying.Shape = float2(1.0, 1.0);
    Varying.Profile = float2(0.0);

    const float4 HeadClip = mul(SparkFrame.UnjitteredViewProjection, float4(Head.xyz, 1.0));
    float4 TailClip = mul(SparkFrame.PreviousUnjitteredViewProjection, float4(Tail.xyz, 1.0));
    if (HeadClip.w < 0.05)
    {
        Varying.Position = float4(0.0, 0.0, -2.0, 1.0);
        return Varying;
    }
    if (TailClip.w < 0.05)
        TailClip = HeadClip;

    const float3 Up = float3(SparkFrame.View[0].y, SparkFrame.View[1].y, SparkFrame.View[2].y);
    const float2 HeadPixels = ProjectPixels(HeadClip);
    float2 TailPixels = ProjectPixels(TailClip);

    // 📝 Bound the raster footprint: close-up glowing chips must not become large billboards.
    const float Width = clamp(length(ProjectPixels(mul(SparkFrame.UnjitteredViewProjection, float4(Head.xyz + Up * Head.w, 1.0)))
                                     - HeadPixels), 0.65, 1.35);
    const float PixelWidth = select(Tail.w > 0.0, clamp(Tail.w, 0.35, 1.0), 1.0);
    const float Shutter = select(Radiance.w > 0.0, clamp(Radiance.w, 0.15, 1.0), 1.0);
    TailPixels = lerp(HeadPixels, TailPixels, Shutter);

    float2 Along = HeadPixels - TailPixels;
    const float Longest = 0.2 * SparkFrame.ScreenExtent.y;
    const float Span = length(Along);
    if (Span > Longest)
    {
        TailPixels = HeadPixels - Along * (Longest / Span);
        Along = HeadPixels - TailPixels;
    }
    const float Length = length(Along);
    const float2 Direction = select(Length > 1.0e-3, Along / max(Length, 1.0e-4), float2(1.0, 0.0));
    const float2 Across = float2(-Direction.y, Direction.x);

    // 📝 Corners: (along, across) — tail end pushed back and head end forward by half a width so a
    //    still spark is a dot.
    float2 Corners[6] = { float2(0.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0),
                          float2(0.0, -1.0), float2(1.0, 1.0), float2(0.0, 1.0) };
    const float2 Corner = Corners[VertexIndex % 6u];

    // 📝 Both ends take the head's depth: the tail's clip position belongs to last frame's camera, and
    //    a streak a few centimetres long sits at one depth for the test anyway.
    const float2 BasePixels = select(Corner.x > 0.5, HeadPixels, TailPixels);
    const float2 Offset = Across * Corner.y * Width * PixelWidth * lerp(0.22, 1.0, Corner.x) * 0.5
                        + Direction * (Corner.x - 0.5) * Width * PixelWidth;
    const float2 Pixels = BasePixels + Offset;
    Varying.Position = float4(Pixels / (0.5 * SparkFrame.ScreenExtent) * HeadClip.w, HeadClip.z, HeadClip.w);

    // 📝 Energy: a spark's light spread over a streak longer than its width is shared out along it.
    const float Spread = Width / max(Length + Width, Width);
    Varying.Radiance = Radiance.rgb * lerp(0.35, 1.0, Spread) * SparkFrame.EmissionScale;
    Varying.Profile = float2(Corner.y, Corner.x);
    Varying.Shape = float2(PixelWidth, Shutter);
    return Varying;
}

[shader("fragment")]
public float4 SparkFragment(SparkVarying Varying) : SV_Target0
{
    const float Across = pow(max(0.0, 1.0 - Varying.Profile.x * Varying.Profile.x), 2.0);
    const float Head = lerp(lerp(0.08, 0.35, Varying.Shape.y), 1.0, pow(Varying.Profile.y, lerp(0.7, 1.8, Varying.Shape.x)));
    return float4(Varying.Radiance * Across * Head, 0.0);
}
