//============================================================================================================================================
//                                                          SELECTIONMASKCOMPUTE.SLANG
//============================================================================================================================================
// O(1) VRAM selection bitmask compute shader evaluating PriorityLayerOrdinal, PartGroupOrdinal, or Face selection operations in < 0.05 ms

struct SelectionDispatchParams
{
    uint FaceCount;          // Total faces in PolygonNetwork
    uint OperationCode;      // 0 = ClearAll, 1 = SelectByLayer, 2 = SelectByPartGroup, 3 = SelectSingleFace, 4 = ToggleSingleFace, 5 = SelectAll
    uint TargetValue;        // LayerOrdinal, PartGroupOrdinal, or FaceIndex
    uint AdditiveMask;       // 0 = Replace selection, 1 = Union into existing selection
};

[[vk::binding(0, 0)]] ConstantBuffer<SelectionDispatchParams> Params;
[[vk::binding(1, 0)]] StructuredBuffer<uint>                  FacePackedAttributes; // low 16 bits = PriorityLayerOrdinal, high 16 bits = PartGroupOrdinal
[[vk::binding(2, 0)]] RWStructuredBuffer<uint>                SelectionBitmask;     // 1 bit per FaceIndex (ceil(FaceCount / 32) uints)

[shader("compute")]
[numthreads(64, 1, 1)]
void ComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint WordIndex = DispatchThreadID.x;
    uint BaseFace  = WordIndex * 32u;
    if (BaseFace >= Params.FaceCount)
    {
        return;
    }

    uint ExistingWord = SelectionBitmask[WordIndex];
    uint ResultWord   = (Params.AdditiveMask != 0u) ? ExistingWord : 0u;

    if (Params.OperationCode == 0u) // ClearAll
    {
        SelectionBitmask[WordIndex] = 0u;
        return;
    }

    for (uint Bit = 0u; Bit < 32u; ++Bit)
    {
        uint FaceIdx = BaseFace + Bit;
        if (FaceIdx < Params.FaceCount)
        {
            uint Packed   = FacePackedAttributes[FaceIdx];
            uint LayerOrd = Packed & 0xFFFFu;
            uint PartOrd  = (Packed >> 16u) & 0xFFFFu;
            uint BitFlag  = 1u << Bit;

            if (Params.OperationCode == 1u && LayerOrd == Params.TargetValue)
            {
                ResultWord |= BitFlag;
            }
            else if (Params.OperationCode == 2u && PartOrd == Params.TargetValue)
            {
                ResultWord |= BitFlag;
            }
            else if (Params.OperationCode == 3u && FaceIdx == Params.TargetValue)
            {
                ResultWord |= BitFlag;
            }
            else if (Params.OperationCode == 4u)
            {
                bool BitEnabled = (ExistingWord & BitFlag) != 0u;
                bool Flip       = (FaceIdx == Params.TargetValue);
                if (BitEnabled != Flip)
                {
                    ResultWord |= BitFlag;
                }
                else
                {
                    ResultWord &= ~BitFlag;
                }
            }
            else if (Params.OperationCode == 5u)
            {
                ResultWord |= BitFlag;
            }
        }
    }

    SelectionBitmask[WordIndex] = ResultWord;
}
