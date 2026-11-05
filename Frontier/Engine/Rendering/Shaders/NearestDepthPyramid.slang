/*==========================================================================================================================================
                                                      NEARESTDEPTHPYRAMID.SLANG
                                     ------------------------------------------------------------------
                    The nearest-depth pyramid over the screen: level 0 transferred from the
                    depth attachment, then every coarser level reduced from the one beneath.
==========================================================================================================================================*/

// 🧩 The nearest-depth pyramid: the Slang form of the demo's NearestDepthTransfer.wgsl and
//    NearestDepthReduction.wgsl, 1:1.
//
// 📝 A depth attachment can be neither copied into a colour texture nor written as storage, so level 0
//    is transferred texel for texel here; every coarser level is then built from the one beneath it.
//
// 📝 Reversed depth stores larger values nearer the eye, so "nearest" is a maximum, and the clear value
//    0 (nothing rastered) never wins over a surface. A level-L texel therefore bounds every pixel of
//    its 2^L × 2^L tile from in front: a ray in front of that value is in front of the whole tile and
//    may cross it in a single step.
//
//    Level extents follow max(1, ⌊extent / 2⌋). Where the source extent is odd its last row or column
//    has no partner, so the final target texel takes three source texels on that axis instead of two —
//    no pixel is ever left unbounded.

//------------------------------------------------------------------------------------------------------------------------
//                                                    LEVEL TRANSFER
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 0)]] // 📝 The reversed depth attachment. Slang's HLSL front end carries no Depth2D on the SPIR-V target, so the
//    attachment is bound as a sampled single-channel texture (the host's depth view is a depth-only
//    image either way; only the SPIR-V image descriptor's depth flag differs, and the loader never
//    samples it).
public Texture2D<float> SurfaceDepth;
[[vk::binding(1, 0)]] public RWTexture2D<float> LevelTarget;

[shader("compute")]
[numthreads(8, 8, 1)]
public void TransferNearestDepth(uint3 Invocation : SV_DispatchThreadID)
{
    uint Width = 0u;
    uint Height = 0u;
    LevelTarget.GetDimensions(Width, Height);
    int2 Pixel = int2(Invocation.xy);
    if (any(Pixel >= int2(int(Width), int(Height))))
        return;
    LevelTarget[Pixel] = SurfaceDepth.Load(int3(Pixel, 0));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   LEVEL REDUCTION
//------------------------------------------------------------------------------------------------------------------------

[[vk::binding(0, 1)]] public Texture2D<float> LevelSource;
[[vk::binding(1, 1)]] public RWTexture2D<float> CoarseTarget;

[shader("compute")]
[numthreads(8, 8, 1)]
public void ReduceNearestDepth(uint3 Invocation : SV_DispatchThreadID)
{
    uint TargetWidth = 0u;
    uint TargetHeight = 0u;
    CoarseTarget.GetDimensions(TargetWidth, TargetHeight);
    int2 TargetExtent = int2(int(TargetWidth), int(TargetHeight));
    int2 Pixel = int2(Invocation.xy);
    if (any(Pixel >= TargetExtent))
        return;

    uint SourceWidth = 0u;
    uint SourceHeight = 0u;
    LevelSource.GetDimensions(SourceWidth, SourceHeight);
    int2 SourceExtent = int2(int(SourceWidth), int(SourceHeight));

    // 📝 Where the source extent is odd, the last texel of that axis takes three source texels.
    int2 Unpartnered = int2(((SourceExtent & int2(1, 1)) == int2(1, 1)) & (Pixel == TargetExtent - int2(1, 1)));
    int2 Span = int2(2, 2) + Unpartnered;

    float Nearest = 0.0;
    for (int Row = 0; Row < Span.y; ++Row)
        for (int Column = 0; Column < Span.x; ++Column)
        {
            int2 Texel = min(Pixel * 2 + int2(Column, Row), SourceExtent - int2(1, 1));
            Nearest = max(Nearest, LevelSource.Load(int3(Texel, 0)));
        }
    CoarseTarget[Pixel] = Nearest;
}
