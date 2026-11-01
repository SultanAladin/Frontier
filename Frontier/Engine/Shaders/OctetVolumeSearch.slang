//==========================================================================================================================================
//                                                  OCTET VOLUME SEARCH
//==========================================================================================================================================
// 🧩 Two-level instanced 8-wide quantized compressed BVH (TLAS + 8-wide Octet BLAS) Slang compute ray queries for GTX GPUs:
//    - 4-wide float4 SIMD quantized child slab tests (Ylitie-Karras-Laine power-of-two exponent encoding)
//    - Ordered near-to-far child stack push & affine World<->Object ray/normal/tangent transforms
//    - Two-interface curved & solid dielectric refraction peel (front + back normals for Snell & Total Internal Reflection)
//    - Colored Beer-Lambert transmissive shadow evaluation

#pragma once

static const uint OctetInteriorFlagBit = 0x80000000u;
static const uint OctetInstanceStride  = 9u;
static const uint OctetBlockStride     = 6u;
static const uint OctetTriangleStride  = 5u;

struct GpuOctetSurfaceHit
{
    bool   Hit;
    float  DistanceM;
    uint   ClassId;
    uint   InstanceIndex;
    uint   VisitedBlocks;
    float3 WorldPos;
    float3 GeoNormal;
    float3 WorldTangent;
    float2 TexCoord;
    float3 InstanceTint;
    float  InstanceSelfRadiance;
};

struct GpuOctetGlassPeel
{
    bool   HitGlass;
    float  FrontT;
    float  ThicknessM;
    float  Ior;
    float3 FrontNormal;
    float3 BackNormal;
    float3 AbsorptionPerM;
};

float4 UnpackOctetBytes4(uint Packed)
{
    return float4(float(Packed & 0xFFu),
                  float((Packed >> 8u) & 0xFFu),
                  float((Packed >> 16u) & 0xFFu),
                  float((Packed >> 24u) & 0xFFu));
}

float3 SafeReciprocalRayDir(float3 Dir)
{
    float3 SafeD = select(abs(Dir) > 1.0e-8f, Dir, select(Dir >= 0.0f, float3(1.0e-8f), float3(-1.0e-8f)));
    return 1.0f / SafeD;
}

bool IntersectOctetAabb(float3 Lo, float3 Hi, float3 Origin, float3 InvDir, float MinT, float MaxT, out float OutEntryT)
{
    float3 T0v = (Lo - Origin) * InvDir;
    float3 T1v = (Hi - Origin) * InvDir;
    float3 TLo = min(T0v, T1v);
    float3 THi = max(T0v, T1v);
    float  T0  = max(MinT, max(TLo.x, max(TLo.y, TLo.z)));
    float  T1  = min(MaxT, min(THi.x, min(THi.y, THi.z)));
    OutEntryT  = T0;
    return T1 >= T0;
}

float3 TransformWorldPointToInstance(float4 Inv0, float4 Inv1, float4 Inv2, float4 Inv3, float3 WorldP)
{
    return float3(dot(float3(Inv0.x, Inv1.x, Inv2.x), WorldP) + Inv3.x,
                  dot(float3(Inv0.y, Inv1.y, Inv2.y), WorldP) + Inv3.y,
                  dot(float3(Inv0.z, Inv1.z, Inv2.z), WorldP) + Inv3.z);
}

float3 TransformWorldDirToInstance(float4 Inv0, float4 Inv1, float4 Inv2, float3 WorldD)
{
    return float3(dot(float3(Inv0.x, Inv1.x, Inv2.x), WorldD),
                  dot(float3(Inv0.y, Inv1.y, Inv2.y), WorldD),
                  dot(float3(Inv0.z, Inv1.z, Inv2.z), WorldD));
}

float3 TransformInstanceNormalToWorld(float4 Inv0, float4 Inv1, float4 Inv2, float3 LocalN)
{
    float3 Raw = float3(dot(Inv0.xyz, LocalN), dot(Inv1.xyz, LocalN), dot(Inv2.xyz, LocalN));
    float  Len = length(Raw);
    return (Len > 1.0e-8f) ? (Raw / Len) : float3(0.0f, 1.0f, 0.0f);
}

void EvaluateFourQuantizedSlabs(float3   BlockOrigin,
                                float3   StepScale,
                                uint     PackedLoX,
                                uint     PackedHiX,
                                uint     PackedLoY,
                                uint     PackedHiY,
                                uint     PackedLoZ,
                                uint     PackedHiZ,
                                float3   Origin,
                                float3   InvDir,
                                float    MinT,
                                float    MaxT,
                                out bool4  OutHit,
                                out float4 OutEntryT)
{
    float4 LoX = BlockOrigin.x + UnpackOctetBytes4(PackedLoX) * StepScale.x;
    float4 HiX = BlockOrigin.x + UnpackOctetBytes4(PackedHiX) * StepScale.x;
    float4 LoY = BlockOrigin.y + UnpackOctetBytes4(PackedLoY) * StepScale.y;
    float4 HiY = BlockOrigin.y + UnpackOctetBytes4(PackedHiY) * StepScale.y;
    float4 LoZ = BlockOrigin.z + UnpackOctetBytes4(PackedLoZ) * StepScale.z;
    float4 HiZ = BlockOrigin.z + UnpackOctetBytes4(PackedHiZ) * StepScale.z;

    float4 Tx0 = (LoX - Origin.x) * InvDir.x;
    float4 Tx1 = (HiX - Origin.x) * InvDir.x;
    float4 Ty0 = (LoY - Origin.y) * InvDir.y;
    float4 Ty1 = (HiY - Origin.y) * InvDir.y;
    float4 Tz0 = (LoZ - Origin.z) * InvDir.z;
    float4 Tz1 = (HiZ - Origin.z) * InvDir.z;

    float4 TMin = max(float4(MinT), max(min(Tx0, Tx1), max(min(Ty0, Ty1), min(Tz0, Tz1))));
    float4 TMax = min(float4(MaxT), min(max(Tx0, Tx1), min(max(Ty0, Ty1), max(Tz0, Tz1))));

    OutEntryT = TMin;
    OutHit    = (TMax >= TMin);
}

GpuOctetSurfaceHit SearchOctetInstancesOpaque(StructuredBuffer<float4> TopCells,
                                              StructuredBuffer<float4> Instances,
                                              StructuredBuffer<float4> OctetBlocks,
                                              StructuredBuffer<float4> Triangles,
                                              uint                     TopCellCount,
                                              float3                   Origin,
                                              float3                   RayDir,
                                              float                    MinT,
                                              float                    MaxT)
{
    GpuOctetSurfaceHit Out;
    Out.Hit                  = false;
    Out.DistanceM            = MaxT;
    Out.ClassId              = 0u;
    Out.InstanceIndex        = 0xFFFFFFFFu;
    Out.VisitedBlocks        = 0u;
    Out.WorldPos             = Origin;
    Out.GeoNormal            = float3(0.0f, 1.0f, 0.0f);
    Out.WorldTangent         = float3(1.0f, 0.0f, 0.0f);
    Out.TexCoord             = float2(0.0f);
    Out.InstanceTint         = float3(1.0f);
    Out.InstanceSelfRadiance = 0.0f;

    if (TopCellCount == 0u)
    {
        return Out;
    }

    float3 WorldInvDir = SafeReciprocalRayDir(RayDir);
    uint   TopStack[32] = {};
    int    TopPtr       = 0;
    float  RootEntry    = 0.0f;
    if (IntersectOctetAabb(TopCells[0].xyz, TopCells[1].xyz, Origin, WorldInvDir, MinT, Out.DistanceM, RootEntry))
    {
        TopStack[TopPtr++] = 0u;
    }

    uint  BestGlobalTri = 0xFFFFFFFFu;
    float BestU         = 0.0f;
    float BestV         = 0.0f;

    while (TopPtr > 0)
    {
        Out.VisitedBlocks += 1u;
        uint   TopIdx  = TopStack[--TopPtr];
        float4 TC0     = TopCells[TopIdx * 2u + 0u];
        float4 TC1     = TopCells[TopIdx * 2u + 1u];
        uint   LeftOrF = asuint(TC0.w);
        uint   RightOr = asuint(TC1.w);

        if ((RightOr & OctetInteriorFlagBit) != 0u)
        {
            uint  LeftIdx  = LeftOrF;
            uint  RightIdx = RightOr & ~OctetInteriorFlagBit;
            float EntryL = 0.0f, EntryR = 0.0f;
            bool  HitL = IntersectOctetAabb(TopCells[LeftIdx * 2u].xyz, TopCells[LeftIdx * 2u + 1u].xyz, Origin, WorldInvDir, MinT, Out.DistanceM, EntryL);
            bool  HitR = IntersectOctetAabb(TopCells[RightIdx * 2u].xyz, TopCells[RightIdx * 2u + 1u].xyz, Origin, WorldInvDir, MinT, Out.DistanceM, EntryR);
            if (HitL && HitR && TopPtr + 2 <= 32)
            {
                if (EntryL < EntryR)
                {
                    TopStack[TopPtr++] = RightIdx;
                    TopStack[TopPtr++] = LeftIdx;
                }
                else
                {
                    TopStack[TopPtr++] = LeftIdx;
                    TopStack[TopPtr++] = RightIdx;
                }
            }
            else if (HitL && TopPtr + 1 <= 32)
            {
                TopStack[TopPtr++] = LeftIdx;
            }
            else if (HitR && TopPtr + 1 <= 32)
            {
                TopStack[TopPtr++] = RightIdx;
            }
            continue;
        }

        for (uint LeafOff = 0u; LeafOff < RightOr; ++LeafOff)
        {
            uint   InstIdx   = LeftOrF + LeafOff;
            uint   InstBase  = InstIdx * OctetInstanceStride;
            float4 WorldLo   = Instances[InstBase + 4u];
            float4 WorldHi   = Instances[InstBase + 5u];
            float  InstEntry = 0.0f;
            if (!IntersectOctetAabb(WorldLo.xyz, WorldHi.xyz, Origin, WorldInvDir, MinT, Out.DistanceM, InstEntry))
            {
                continue;
            }

            float4 Inv0    = Instances[InstBase + 0u];
            float4 Inv1    = Instances[InstBase + 1u];
            float4 Inv2    = Instances[InstBase + 2u];
            float4 Inv3    = Instances[InstBase + 3u];
            float4 Meta    = Instances[InstBase + 7u];
            uint   BlkBase = asuint(Meta.x);
            uint   TriBase = asuint(Meta.y);

            float3 ObjOrigin = TransformWorldPointToInstance(Inv0, Inv1, Inv2, Inv3, Origin);
            float3 ObjDir    = TransformWorldDirToInstance(Inv0, Inv1, Inv2, RayDir);
            float3 ObjInvDir = SafeReciprocalRayDir(ObjDir);

            uint BlasStack[24] = {};
            int  BlasPtr       = 0;
            BlasStack[BlasPtr++] = 0u;

            while (BlasPtr > 0)
            {
                Out.VisitedBlocks += 1u;
                uint   LocalBlk = BlasStack[--BlasPtr];
                uint   BAddr    = (BlkBase + LocalBlk) * OctetBlockStride;
                float4 B0       = OctetBlocks[BAddr + 0u];
                float4 B1       = OctetBlocks[BAddr + 1u];
                float4 B2       = OctetBlocks[BAddr + 2u];
                float4 B3       = OctetBlocks[BAddr + 3u];
                float4 B4       = OctetBlocks[BAddr + 4u];
                float4 B5       = OctetBlocks[BAddr + 5u];

                uint   ExpMask = asuint(B0.w);
                int    ExpX    = int(ExpMask & 0xFFu) - 128;
                int    ExpY    = int((ExpMask >> 8u) & 0xFFu) - 128;
                int    ExpZ    = int((ExpMask >> 16u) & 0xFFu) - 128;
                uint   Mask    = (ExpMask >> 24u) & 0xFFu;
                float3 StepS   = exp2(float3(float(ExpX), float(ExpY), float(ExpZ)));

                bool4  Hit03, Hit47;
                float4 Ent03, Ent47;
                EvaluateFourQuantizedSlabs(B0.xyz, StepS, asuint(B3.x), asuint(B3.z), asuint(B4.x), asuint(B4.z), asuint(B5.x), asuint(B5.z),
                                           ObjOrigin, ObjInvDir, MinT, Out.DistanceM, Hit03, Ent03);
                EvaluateFourQuantizedSlabs(B0.xyz, StepS, asuint(B3.y), asuint(B3.w), asuint(B4.y), asuint(B4.w), asuint(B5.y), asuint(B5.w),
                                           ObjOrigin, ObjInvDir, MinT, Out.DistanceM, Hit47, Ent47);

                uint  ChildLinks[8] = {asuint(B1.x), asuint(B1.y), asuint(B1.z), asuint(B1.w),
                                       asuint(B2.x), asuint(B2.y), asuint(B2.z), asuint(B2.w)};
                bool  ChildHit[8]   = {Hit03.x, Hit03.y, Hit03.z, Hit03.w, Hit47.x, Hit47.y, Hit47.z, Hit47.w};
                float ChildEnt[8]   = {Ent03.x, Ent03.y, Ent03.z, Ent03.w, Ent47.x, Ent47.y, Ent47.z, Ent47.w};

                uint  InternalIds[8] = {};
                float InternalEnt[8] = {};
                int   InternalCount  = 0;

                [unroll]
                for (uint Slot = 0u; Slot < 8u; ++Slot)
                {
                    if (((Mask & (1u << Slot)) != 0u) && ChildHit[Slot])
                    {
                        uint Link = ChildLinks[Slot];
                        if ((Link & OctetInteriorFlagBit) != 0u)
                        {
                            InternalIds[InternalCount] = Link & ~OctetInteriorFlagBit;
                            InternalEnt[InternalCount] = ChildEnt[Slot];
                            InternalCount += 1;
                        }
                        else
                        {
                            uint SpanCount = (Link >> 24u) & 0x7Fu;
                            uint SpanStart = TriBase + (Link & 0x00FFFFFFu);
                            for (uint TriOff = 0u; TriOff < SpanCount; ++TriOff)
                            {
                                uint   GlobalTri = SpanStart + TriOff;
                                uint   TAddr     = GlobalTri * OctetTriangleStride;
                                float4 T2        = Triangles[TAddr + 2u];
                                uint   ClassPack = asuint(T2.w);
                                if ((ClassPack & 0x80000000u) != 0u)
                                {
                                    continue;
                                }
                                float3 V0   = Triangles[TAddr + 0u].xyz;
                                float3 E1   = Triangles[TAddr + 1u].xyz;
                                float3 E2   = T2.xyz;
                                float3 Pvec = cross(ObjDir, E2);
                                float  Det  = dot(E1, Pvec);
                                if (abs(Det) > 1.0e-9f)
                                {
                                    float  InvDet = 1.0f / Det;
                                    float3 Tvec   = ObjOrigin - V0;
                                    float  U      = dot(Tvec, Pvec) * InvDet;
                                    if (U >= 0.0f && U <= 1.0f)
                                    {
                                        float3 Qvec = cross(Tvec, E1);
                                        float  V    = dot(ObjDir, Qvec) * InvDet;
                                        if (V >= 0.0f && (U + V) <= 1.0f)
                                        {
                                            float HitT = dot(E2, Qvec) * InvDet;
                                            if (HitT >= MinT && HitT < Out.DistanceM)
                                            {
                                                Out.Hit           = true;
                                                Out.DistanceM     = HitT;
                                                Out.InstanceIndex = InstIdx;
                                                BestGlobalTri     = GlobalTri;
                                                BestU             = U;
                                                BestV             = V;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Order internal children far-to-near so nearest pops first
                for (int I = 1; I < InternalCount; ++I)
                {
                    uint  KeyId  = InternalIds[I];
                    float KeyEnt = InternalEnt[I];
                    int   J      = I - 1;
                    while (J >= 0 && InternalEnt[J] < KeyEnt)
                    {
                        InternalIds[J + 1] = InternalIds[J];
                        InternalEnt[J + 1] = InternalEnt[J];
                        J -= 1;
                    }
                    InternalIds[J + 1] = KeyId;
                    InternalEnt[J + 1] = KeyEnt;
                }
                for (int I = 0; I < InternalCount && BlasPtr < 24; ++I)
                {
                    if (InternalEnt[I] <= Out.DistanceM)
                    {
                        BlasStack[BlasPtr++] = InternalIds[I];
                    }
                }
            }
        }
    }

    if (Out.Hit && BestGlobalTri != 0xFFFFFFFFu)
    {
        uint   InstBase = Out.InstanceIndex * OctetInstanceStride;
        float4 Inv0     = Instances[InstBase + 0u];
        float4 Inv1     = Instances[InstBase + 1u];
        float4 Inv2     = Instances[InstBase + 2u];
        float4 Fwd0     = Instances[InstBase + 6u];
        float4 Meta     = Instances[InstBase + 7u];
        float4 Optics   = Instances[InstBase + 8u];

        uint   TAddr     = BestGlobalTri * OctetTriangleStride;
        float4 T0        = Triangles[TAddr + 0u];
        float4 T1        = Triangles[TAddr + 1u];
        float4 T2        = Triangles[TAddr + 2u];
        float4 T3        = Triangles[TAddr + 3u];
        uint   ClassBase = asuint(T2.w) & 0x7FFFFFFFu;
        uint   ClassOver = asuint(Meta.z);

        Out.ClassId   = (ClassOver != 0xFFFFFFFFu) ? ClassOver : ClassBase;
        Out.WorldPos  = Origin + RayDir * Out.DistanceM;
        float3 LocalN = normalize(cross(T1.xyz, T2.xyz));
        Out.GeoNormal = TransformInstanceNormalToWorld(Inv0, Inv1, Inv2, LocalN);

        // Derive tangent from UV gradient or instance forward column 0
        float2 Uv0 = float2(T0.w, T1.w);
        float2 Du1 = T3.xy - Uv0;
        float2 Du2 = T3.zw - Uv0;
        float  Det = Du1.x * Du2.y - Du2.x * Du1.y;
        float3 RawT = Fwd0.xyz;
        if (abs(Det) > 1.0e-7f)
        {
            float3 LocalT = (Du2.y * T1.xyz - Du1.y * T2.xyz) / Det;
            // Transform local tangent direction via inverse(WorldToObject) approximated by normal basis + Gram-Schmidt
            RawT = TransformInstanceNormalToWorld(Inv0, Inv1, Inv2, normalize(LocalT));
        }
        float3 OrthoT = RawT - Out.GeoNormal * dot(Out.GeoNormal, RawT);
        Out.WorldTangent = (length(OrthoT) > 1.0e-6f) ? normalize(OrthoT) : normalize(cross(abs(Out.GeoNormal.y) < 0.99f ? float3(0, 1, 0) : float3(1, 0, 0), Out.GeoNormal));

        float W0     = 1.0f - BestU - BestV;
        Out.TexCoord = W0 * Uv0 + BestU * T3.xy + BestV * T3.zw;

        if (asuint(Meta.w) != 0u)
        {
            Out.InstanceTint         = Optics.xyz;
            Out.InstanceSelfRadiance = Optics.w;
        }
    }
    return Out;
}

GpuOctetGlassPeel SearchOctetInstancesGlassPeel(StructuredBuffer<float4> TopCells,
                                                StructuredBuffer<float4> Instances,
                                                StructuredBuffer<float4> OctetBlocks,
                                                StructuredBuffer<float4> Triangles,
                                                uint                     TopCellCount,
                                                float3                   Origin,
                                                float3                   RayDir,
                                                float                    MaxT)
{
    GpuOctetGlassPeel Out;
    Out.HitGlass       = false;
    Out.FrontT         = MaxT;
    Out.ThicknessM     = 0.014f;
    Out.Ior            = 1.52f;
    Out.FrontNormal    = -RayDir;
    Out.BackNormal     = RayDir;
    Out.AbsorptionPerM = float3(0.0f);

    if (TopCellCount == 0u)
    {
        return Out;
    }

    float3 WorldInvDir = SafeReciprocalRayDir(RayDir);
    uint   TopStack[32] = {};
    int    TopPtr       = 0;
    float  RootEntry    = 0.0f;
    if (IntersectOctetAabb(TopCells[0].xyz, TopCells[1].xyz, Origin, WorldInvDir, 1.0e-3f, MaxT, RootEntry))
    {
        TopStack[TopPtr++] = 0u;
    }

    float FirstT   = MaxT;
    float SecondT  = 0.0f;
    uint  HitInst  = 0xFFFFFFFFu;
    uint  HitTri   = 0xFFFFFFFFu;
    uint  BackInst = 0xFFFFFFFFu;
    uint  BackTri  = 0xFFFFFFFFu;

    while (TopPtr > 0)
    {
        uint   TopIdx  = TopStack[--TopPtr];
        float4 TC0     = TopCells[TopIdx * 2u + 0u];
        float4 TC1     = TopCells[TopIdx * 2u + 1u];
        uint   LeftOrF = asuint(TC0.w);
        uint   RightOr = asuint(TC1.w);

        if ((RightOr & OctetInteriorFlagBit) != 0u)
        {
            uint  L = LeftOrF;
            uint  R = RightOr & ~OctetInteriorFlagBit;
            float EL = 0.0f, ER = 0.0f;
            if (IntersectOctetAabb(TopCells[L * 2u].xyz, TopCells[L * 2u + 1u].xyz, Origin, WorldInvDir, 1.0e-3f, MaxT, EL) && TopPtr < 32)
            {
                TopStack[TopPtr++] = L;
            }
            if (IntersectOctetAabb(TopCells[R * 2u].xyz, TopCells[R * 2u + 1u].xyz, Origin, WorldInvDir, 1.0e-3f, MaxT, ER) && TopPtr < 32)
            {
                TopStack[TopPtr++] = R;
            }
            continue;
        }

        for (uint LeafOff = 0u; LeafOff < RightOr; ++LeafOff)
        {
            uint   InstIdx   = LeftOrF + LeafOff;
            uint   InstBase  = InstIdx * OctetInstanceStride;
            float  InstEntry = 0.0f;
            if (!IntersectOctetAabb(Instances[InstBase + 4u].xyz, Instances[InstBase + 5u].xyz, Origin, WorldInvDir, 1.0e-3f, MaxT, InstEntry))
            {
                continue;
            }

            float4 Inv0      = Instances[InstBase + 0u];
            float4 Inv1      = Instances[InstBase + 1u];
            float4 Inv2      = Instances[InstBase + 2u];
            float4 Inv3      = Instances[InstBase + 3u];
            float4 Meta      = Instances[InstBase + 7u];
            uint   BlkBase   = asuint(Meta.x);
            uint   TriBase   = asuint(Meta.y);
            float3 ObjOrigin = TransformWorldPointToInstance(Inv0, Inv1, Inv2, Inv3, Origin);
            float3 ObjDir    = TransformWorldDirToInstance(Inv0, Inv1, Inv2, RayDir);
            float3 ObjInvDir = SafeReciprocalRayDir(ObjDir);

            uint BlasStack[24] = {};
            int  BlasPtr       = 0;
            BlasStack[BlasPtr++] = 0u;

            while (BlasPtr > 0)
            {
                uint   BAddr   = (BlkBase + BlasStack[--BlasPtr]) * OctetBlockStride;
                float4 B0      = OctetBlocks[BAddr + 0u];
                float4 B1      = OctetBlocks[BAddr + 1u];
                float4 B2      = OctetBlocks[BAddr + 2u];
                float4 B3      = OctetBlocks[BAddr + 3u];
                float4 B4      = OctetBlocks[BAddr + 4u];
                float4 B5      = OctetBlocks[BAddr + 5u];
                uint   ExpMask = asuint(B0.w);
                float3 StepS   = exp2(float3(float(int(ExpMask & 0xFFu) - 128),
                                             float(int((ExpMask >> 8u) & 0xFFu) - 128),
                                             float(int((ExpMask >> 16u) & 0xFFu) - 128)));
                uint   Mask    = (ExpMask >> 24u) & 0xFFu;

                bool4  Hit03, Hit47;
                float4 Ent03, Ent47;
                EvaluateFourQuantizedSlabs(B0.xyz, StepS, asuint(B3.x), asuint(B3.z), asuint(B4.x), asuint(B4.z), asuint(B5.x), asuint(B5.z),
                                           ObjOrigin, ObjInvDir, 1.0e-3f, MaxT, Hit03, Ent03);
                EvaluateFourQuantizedSlabs(B0.xyz, StepS, asuint(B3.y), asuint(B3.w), asuint(B4.y), asuint(B4.w), asuint(B5.y), asuint(B5.w),
                                           ObjOrigin, ObjInvDir, 1.0e-3f, MaxT, Hit47, Ent47);

                uint ChildLinks[8] = {asuint(B1.x), asuint(B1.y), asuint(B1.z), asuint(B1.w),
                                      asuint(B2.x), asuint(B2.y), asuint(B2.z), asuint(B2.w)};
                bool ChildHit[8]   = {Hit03.x, Hit03.y, Hit03.z, Hit03.w, Hit47.x, Hit47.y, Hit47.z, Hit47.w};

                [unroll]
                for (uint Slot = 0u; Slot < 8u; ++Slot)
                {
                    if (((Mask & (1u << Slot)) != 0u) && ChildHit[Slot])
                    {
                        uint Link = ChildLinks[Slot];
                        if ((Link & OctetInteriorFlagBit) != 0u)
                        {
                            if (BlasPtr < 24)
                            {
                                BlasStack[BlasPtr++] = Link & ~OctetInteriorFlagBit;
                            }
                        }
                        else
                        {
                            uint SpanCount = (Link >> 24u) & 0x7Fu;
                            uint SpanStart = TriBase + (Link & 0x00FFFFFFu);
                            for (uint TriOff = 0u; TriOff < SpanCount; ++TriOff)
                            {
                                uint   GlobalTri = SpanStart + TriOff;
                                uint   TAddr     = GlobalTri * OctetTriangleStride;
                                float4 T2        = Triangles[TAddr + 2u];
                                if ((asuint(T2.w) & 0x80000000u) == 0u)
                                {
                                    continue;
                                }
                                float3 V0   = Triangles[TAddr + 0u].xyz;
                                float3 E1   = Triangles[TAddr + 1u].xyz;
                                float3 E2   = T2.xyz;
                                float3 Pvec = cross(ObjDir, E2);
                                float  Det  = dot(E1, Pvec);
                                if (abs(Det) > 1.0e-9f)
                                {
                                    float  InvDet = 1.0f / Det;
                                    float3 Tvec   = ObjOrigin - V0;
                                    float  U      = dot(Tvec, Pvec) * InvDet;
                                    if (U >= 0.0f && U <= 1.0f)
                                    {
                                        float3 Qvec = cross(Tvec, E1);
                                        float  V    = dot(ObjDir, Qvec) * InvDet;
                                        if (V >= 0.0f && (U + V) <= 1.0f)
                                        {
                                            float T = dot(E2, Qvec) * InvDet;
                                            if (T >= 1.0e-3f && T < MaxT)
                                            {
                                                if (T < FirstT)
                                                {
                                                    if (HitTri != 0xFFFFFFFFu && (T + 0.002f) < FirstT)
                                                    {
                                                        SecondT  = FirstT;
                                                        BackInst = HitInst;
                                                        BackTri  = HitTri;
                                                    }
                                                    FirstT  = T;
                                                    HitInst = InstIdx;
                                                    HitTri  = GlobalTri;
                                                }
                                                else if (T > FirstT + 0.002f && (SecondT == 0.0f || T < SecondT))
                                                {
                                                    SecondT  = T;
                                                    BackInst = InstIdx;
                                                    BackTri  = GlobalTri;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (HitTri != 0xFFFFFFFFu)
    {
        uint   IBase = HitInst * OctetInstanceStride;
        uint   TAddr = HitTri * OctetTriangleStride;
        float4 T4    = Triangles[TAddr + 4u];
        float3 LocN  = normalize(cross(Triangles[TAddr + 1u].xyz, Triangles[TAddr + 2u].xyz));
        float3 WN    = TransformInstanceNormalToWorld(Instances[IBase + 0u], Instances[IBase + 1u], Instances[IBase + 2u], LocN);
        if (dot(WN, RayDir) > 0.0f)
        {
            WN = -WN;
        }
        Out.HitGlass       = true;
        Out.FrontT         = FirstT;
        Out.ThicknessM     = (SecondT > FirstT) ? min(0.24f, SecondT - FirstT) : 0.014f;
        Out.FrontNormal    = WN;
        Out.BackNormal     = -WN;
        Out.AbsorptionPerM = T4.xyz;
        Out.Ior            = max(1.01f, T4.w);

        if (BackTri != 0xFFFFFFFFu)
        {
            uint   BBase = BackInst * OctetInstanceStride;
            uint   BAddr = BackTri * OctetTriangleStride;
            float3 BLocN = normalize(cross(Triangles[BAddr + 1u].xyz, Triangles[BAddr + 2u].xyz));
            float3 BWN   = TransformInstanceNormalToWorld(Instances[BBase + 0u], Instances[BBase + 1u], Instances[BBase + 2u], BLocN);
            if (dot(BWN, RayDir) > 0.0f)
            {
                BWN = -BWN;
            }
            Out.BackNormal = BWN;
        }
    }
    return Out;
}

float3 SearchOctetInstancesShadow(StructuredBuffer<float4> TopCells,
                                  StructuredBuffer<float4> Instances,
                                  StructuredBuffer<float4> OctetBlocks,
                                  StructuredBuffer<float4> Triangles,
                                  uint                     TopCellCount,
                                  float3                   Origin,
                                  float3                   RayDir,
                                  float                    MaxT)
{
    if (TopCellCount == 0u)
    {
        return float3(1.0f);
    }

    float3 WorldInvDir = SafeReciprocalRayDir(RayDir);
    float3 Trans       = float3(1.0f);
    uint   TopStack[32] = {};
    int    TopPtr       = 0;
    float  RootEntry    = 0.0f;
    if (IntersectOctetAabb(TopCells[0].xyz, TopCells[1].xyz, Origin, WorldInvDir, 1.5e-3f, MaxT, RootEntry))
    {
        TopStack[TopPtr++] = 0u;
    }

    while (TopPtr > 0)
    {
        uint   TopIdx  = TopStack[--TopPtr];
        float4 TC0     = TopCells[TopIdx * 2u + 0u];
        float4 TC1     = TopCells[TopIdx * 2u + 1u];
        uint   LeftOrF = asuint(TC0.w);
        uint   RightOr = asuint(TC1.w);

        if ((RightOr & OctetInteriorFlagBit) != 0u)
        {
            uint  L = LeftOrF;
            uint  R = RightOr & ~OctetInteriorFlagBit;
            float EL = 0.0f, ER = 0.0f;
            if (IntersectOctetAabb(TopCells[L * 2u].xyz, TopCells[L * 2u + 1u].xyz, Origin, WorldInvDir, 1.5e-3f, MaxT, EL) && TopPtr < 32)
            {
                TopStack[TopPtr++] = L;
            }
            if (IntersectOctetAabb(TopCells[R * 2u].xyz, TopCells[R * 2u + 1u].xyz, Origin, WorldInvDir, 1.5e-3f, MaxT, ER) && TopPtr < 32)
            {
                TopStack[TopPtr++] = R;
            }
            continue;
        }

        for (uint LeafOff = 0u; LeafOff < RightOr; ++LeafOff)
        {
            uint   InstIdx   = LeftOrF + LeafOff;
            uint   InstBase  = InstIdx * OctetInstanceStride;
            float  InstEntry = 0.0f;
            if (!IntersectOctetAabb(Instances[InstBase + 4u].xyz, Instances[InstBase + 5u].xyz, Origin, WorldInvDir, 1.5e-3f, MaxT, InstEntry))
            {
                continue;
            }

            float4 Inv0      = Instances[InstBase + 0u];
            float4 Inv1      = Instances[InstBase + 1u];
            float4 Inv2      = Instances[InstBase + 2u];
            float4 Inv3      = Instances[InstBase + 3u];
            float4 Meta      = Instances[InstBase + 7u];
            uint   BlkBase   = asuint(Meta.x);
            uint   TriBase   = asuint(Meta.y);
            float3 ObjOrigin = TransformWorldPointToInstance(Inv0, Inv1, Inv2, Inv3, Origin);
            float3 ObjDir    = TransformWorldDirToInstance(Inv0, Inv1, Inv2, RayDir);
            float3 ObjInvDir = SafeReciprocalRayDir(ObjDir);

            uint BlasStack[24] = {};
            int  BlasPtr       = 0;
            BlasStack[BlasPtr++] = 0u;

            while (BlasPtr > 0)
            {
                uint   BAddr   = (BlkBase + BlasStack[--BlasPtr]) * OctetBlockStride;
                float4 B0      = OctetBlocks[BAddr + 0u];
                float4 B1      = OctetBlocks[BAddr + 1u];
                float4 B2      = OctetBlocks[BAddr + 2u];
                float4 B3      = OctetBlocks[BAddr + 3u];
                float4 B4      = OctetBlocks[BAddr + 4u];
                float4 B5      = OctetBlocks[BAddr + 5u];
                uint   ExpMask = asuint(B0.w);
                float3 StepS   = exp2(float3(float(int(ExpMask & 0xFFu) - 128),
                                             float(int((ExpMask >> 8u) & 0xFFu) - 128),
                                             float(int((ExpMask >> 16u) & 0xFFu) - 128)));
                uint   Mask    = (ExpMask >> 24u) & 0xFFu;

                bool4  Hit03, Hit47;
                float4 Ent03, Ent47;
                EvaluateFourQuantizedSlabs(B0.xyz, StepS, asuint(B3.x), asuint(B3.z), asuint(B4.x), asuint(B4.z), asuint(B5.x), asuint(B5.z),
                                           ObjOrigin, ObjInvDir, 1.5e-3f, MaxT, Hit03, Ent03);
                EvaluateFourQuantizedSlabs(B0.xyz, StepS, asuint(B3.y), asuint(B3.w), asuint(B4.y), asuint(B4.w), asuint(B5.y), asuint(B5.w),
                                           ObjOrigin, ObjInvDir, 1.5e-3f, MaxT, Hit47, Ent47);

                uint ChildLinks[8] = {asuint(B1.x), asuint(B1.y), asuint(B1.z), asuint(B1.w),
                                      asuint(B2.x), asuint(B2.y), asuint(B2.z), asuint(B2.w)};
                bool ChildHit[8]   = {Hit03.x, Hit03.y, Hit03.z, Hit03.w, Hit47.x, Hit47.y, Hit47.z, Hit47.w};

                [unroll]
                for (uint Slot = 0u; Slot < 8u; ++Slot)
                {
                    if (((Mask & (1u << Slot)) != 0u) && ChildHit[Slot])
                    {
                        uint Link = ChildLinks[Slot];
                        if ((Link & OctetInteriorFlagBit) != 0u)
                        {
                            if (BlasPtr < 24)
                            {
                                BlasStack[BlasPtr++] = Link & ~OctetInteriorFlagBit;
                            }
                        }
                        else
                        {
                            uint SpanCount = (Link >> 24u) & 0x7Fu;
                            uint SpanStart = TriBase + (Link & 0x00FFFFFFu);
                            for (uint TriOff = 0u; TriOff < SpanCount; ++TriOff)
                            {
                                uint   TAddr = (SpanStart + TriOff) * OctetTriangleStride;
                                float3 V0    = Triangles[TAddr + 0u].xyz;
                                float3 E1    = Triangles[TAddr + 1u].xyz;
                                float4 T2    = Triangles[TAddr + 2u];
                                float3 Pvec  = cross(ObjDir, T2.xyz);
                                float  Det   = dot(E1, Pvec);
                                if (abs(Det) > 1.0e-9f)
                                {
                                    float  InvDet = 1.0f / Det;
                                    float3 Tvec   = ObjOrigin - V0;
                                    float  U      = dot(Tvec, Pvec) * InvDet;
                                    if (U >= 0.0f && U <= 1.0f)
                                    {
                                        float3 Qvec = cross(Tvec, E1);
                                        float  V    = dot(ObjDir, Qvec) * InvDet;
                                        if (V >= 0.0f && (U + V) <= 1.0f)
                                        {
                                            float T = dot(T2.xyz, Qvec) * InvDet;
                                            if (T >= 1.5e-3f && T < MaxT)
                                            {
                                                if ((asuint(T2.w) & 0x80000000u) == 0u)
                                                {
                                                    return float3(0.0f);
                                                }
                                                float4 T4 = Triangles[TAddr + 4u];
                                                float  Ior = max(1.01f, T4.w);
                                                float  F0  = ((Ior - 1.0f) / (Ior + 1.0f));
                                                F0         = F0 * F0;
                                                Trans *= (1.0f - F0) * exp(-T4.xyz * 0.015f);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return Trans;
}
