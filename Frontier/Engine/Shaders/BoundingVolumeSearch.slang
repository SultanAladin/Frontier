/*==========================================================================================================================================
                                             BOUNDING VOLUME SEARCH (SLANG)
                                     ------------------------------------------------------------------
             GTX-first two-level instanced ray-acceleration search (binary SAH instance TLAS + quantized
             8-wide compressed bounding-volume BLAS), octahedral smooth-normal interpolation, exact
             unpolarized Fresnel / total-internal-reflection curved dielectric refraction, Beer-Lambert
             glass shadow transmittance, and flux-weighted emissive facet CDF sampling.
==========================================================================================================================================*/
// 🧩 Two-level instanced 8-wide CWBVH ray search, curved dielectric Snell/TIR peel, & emissive facet CDF sampler.

#pragma once

static const uint InteriorEnclosureBit = 0x80000000u;
static const uint WideTrianglesStride  = 6u;
static const uint WideCellsStride      = 5u;
static const uint InstanceRecordStride = 12u;

struct BoundingRayHit
{
    bool   Hit;
    float  DistanceMeters;
    uint   TriangleIndex;
    uint   InstanceIndex;
    uint   ClassificationId;
    bool   TransmissiveGlass;
    float  RefractiveIndex;
    float3 WorldPosition;
    float3 PreviousWorldPosition;
    float3 GeometricNormal;
    float3 ShadingNormal;
    float2 TextureCoord;
    float3 AbsorptionPerM;
};

// 📝 Degenerate facets (zero-area triangles from the quantised .facet archive) make normalize() produce NaN,
// which then propagates through the interpolated shading normal into the whole G-buffer for those pixels.
float3 SafeNormalizeVec3(float3 V)
{
    float LenSq = dot(V, V);
    return (LenSq > 1.0e-20f) ? (V * rsqrt(LenSq)) : float3(0.0f, 1.0f, 0.0f);
}

float3 SafeReciprocalRayDir(float3 Direction)
{
    float3 Adjusted = float3(
        abs(Direction.x) > 1.0e-8f ? Direction.x : (Direction.x >= 0.0f ? 1.0e-8f : -1.0e-8f),
        abs(Direction.y) > 1.0e-8f ? Direction.y : (Direction.y >= 0.0f ? 1.0e-8f : -1.0e-8f),
        abs(Direction.z) > 1.0e-8f ? Direction.z : (Direction.z >= 0.0f ? 1.0e-8f : -1.0e-8f));
    return 1.0f / Adjusted;
}

float3 UnpackOctahedralNormal(uint PackedWord)
{
    if (PackedWord == 0u)
    {
        return float3(0.0f, 1.0f, 0.0f);
    }
    int   RawX = int(PackedWord & 0xFFFFu);
    int   RawY = int((PackedWord >> 16u) & 0xFFFFu);
    if (RawX >= 32768) RawX -= 65536;
    if (RawY >= 32768) RawY -= 65536;
    float Ox = float(RawX) * (1.0f / 32767.0f);
    float Oy = float(RawY) * (1.0f / 32767.0f);
    float Nx = Ox;
    float Ny = Oy;
    float Nz = 1.0f - abs(Ox) - abs(Oy);
    if (Nz < 0.0f)
    {
        float Fold = -Nz;
        Nx += (Nx >= 0.0f) ? -Fold : Fold;
        Ny += (Ny >= 0.0f) ? -Fold : Fold;
    }
    return SafeNormalizeVec3(float3(Nx, Ny, Nz));
}

uint ExtractWideChildByte(uint LowWord, uint HighWord, uint ChildSlot)
{
    uint Word  = (ChildSlot < 4u) ? LowWord : HighWord;
    uint Shift = (ChildSlot & 3u) * 8u;
    return (Word >> Shift) & 0xFFu;
}

bool IntersectBoxSlab(float3 Lower, float3 Upper, float3 Origin, float3 InvDir, float MinT, float MaxT, out float OutEntryT)
{
    float3 T0    = (Lower - Origin) * InvDir;
    float3 T1    = (Upper - Origin) * InvDir;
    float3 TNear = min(T0, T1);
    float3 TFar  = max(T0, T1);
    float  Enter = max(MinT, max(TNear.x, max(TNear.y, TNear.z)));
    float  Exit  = min(MaxT, min(TFar.x, min(TFar.y, TFar.z)));
    OutEntryT    = Enter;
    return Exit >= Enter;
}

bool IntersectMollerFacet(float3 V0,
                          float3 E1,
                          float3 E2,
                          float3 Origin,
                          float3 Direction,
                          float  MinT,
                          float  MaxT,
                          out float OutT,
                          out float OutU,
                          out float OutV)
{
    OutT        = MaxT;
    OutU        = 0.0f;
    OutV        = 0.0f;
    float3 Pvec = cross(Direction, E2);
    float  Det  = dot(E1, Pvec);
    if (abs(Det) <= 1.0e-9f)
    {
        return false;
    }
    float  InvDet = 1.0f / Det;
    float3 Tvec   = Origin - V0;
    float  U      = dot(Tvec, Pvec) * InvDet;
    if (U < 0.0f || U > 1.0f)
    {
        return false;
    }
    float3 Qvec = cross(Tvec, E1);
    float  V    = dot(Direction, Qvec) * InvDet;
    if (V < 0.0f || (U + V) > 1.0f)
    {
        return false;
    }
    float HitT = dot(E2, Qvec) * InvDet;
    if (HitT < MinT || HitT > MaxT)
    {
        return false;
    }
    OutT = HitT;
    OutU = U;
    OutV = V;
    return true;
}

float3 TransformAffinePoint3x4(float4 Row0, float4 Row1, float4 Row2, float3 P)
{
    return float3(dot(Row0.xyz, P) + Row0.w,
                  dot(Row1.xyz, P) + Row1.w,
                  dot(Row2.xyz, P) + Row2.w);
}

float3 TransformAffineVector3x4(float4 Row0, float4 Row1, float4 Row2, float3 V)
{
    return float3(dot(Row0.xyz, V), dot(Row1.xyz, V), dot(Row2.xyz, V));
}

//------------------------------------------------------------------------------------------------------------------------
//                                  EXACT DIELECTRIC BOUNDARY & SNELL / TIR OPTICS
//------------------------------------------------------------------------------------------------------------------------

void EvaluateDielectricBoundary(float3    IncidentUnit,
                                float3    BoundaryNormalUnit,
                                float     EtaIncidentOverTransmitted,
                                out float FresnelReflectance,
                                out bool  TotalInternalReflection,
                                out float3 ReflectedDirection,
                                out float3 RefractedDirection)
{
    float3 I = normalize(IncidentUnit);
    float3 N = normalize(BoundaryNormalUnit);
    if (dot(I, N) > 0.0f)
    {
        N = -N;
    }

    float CosThetaI    = clamp(-dot(I, N), 0.0f, 1.0f);
    ReflectedDirection = normalize(I + 2.0f * CosThetaI * N);

    float Eta        = max(EtaIncidentOverTransmitted, 1.0e-4f);
    float Sin2ThetaI = max(0.0f, 1.0f - CosThetaI * CosThetaI);
    float Sin2ThetaT = Eta * Eta * Sin2ThetaI;
    if (Sin2ThetaT >= 1.0f)
    {
        TotalInternalReflection = true;
        FresnelReflectance      = 1.0f;
        RefractedDirection      = ReflectedDirection;
        return;
    }

    float CosThetaT = sqrt(max(0.0f, 1.0f - Sin2ThetaT));
    float Rs        = (Eta * CosThetaI - CosThetaT) / max(Eta * CosThetaI + CosThetaT, 1.0e-6f);
    float Rp        = (CosThetaI - Eta * CosThetaT) / max(CosThetaI + Eta * CosThetaT, 1.0e-6f);

    TotalInternalReflection = false;
    FresnelReflectance      = saturate(0.5f * (Rs * Rs + Rp * Rp));
    RefractedDirection      = normalize(Eta * I + (Eta * CosThetaI - CosThetaT) * N);
}

//------------------------------------------------------------------------------------------------------------------------
//                              BOTTOM-LEVEL 8-WIDE QUANTIZED CWBVH LOCAL RAY SEARCH
//------------------------------------------------------------------------------------------------------------------------

bool SearchLocalWideBlas(StructuredBuffer<float4> WideCells,
                         StructuredBuffer<float4> WideTriangles,
                         uint                     WideRootIndex,
                         float3                   LocalOrigin,
                         float3                   LocalDirUnit,
                         float                    LocalMinT,
                         float                    LocalMaxT,
                         bool                     IncludeGlass,
                         out float                OutLocalT,
                         out float                OutBaryU,
                         out float                OutBaryV,
                         out uint                 OutTriIdx)
{
    OutLocalT = LocalMaxT;
    OutBaryU  = 0.0f;
    OutBaryV  = 0.0f;
    OutTriIdx = 0xFFFFFFFFu;

    float3 InvDir = SafeReciprocalRayDir(LocalDirUnit);
    float  BestT  = LocalMaxT;

    uint WideStack[20];
    [unroll]
    for (uint InitIdx = 0u; InitIdx < 20u; ++InitIdx)
    {
        WideStack[InitIdx] = 0u;
    }
    uint StackTop       = 0u;
    WideStack[StackTop++] = WideRootIndex;

    [loop]
    while (StackTop > 0u)
    {
        uint   CellIdx = WideStack[--StackTop];
        uint   Base    = CellIdx * WideCellsStride;
        float4 Slot0   = WideCells[Base + 0u];
        uint4  Slot1   = asuint(WideCells[Base + 1u]);
        uint4  Slot2   = asuint(WideCells[Base + 2u]);
        uint4  Slot3   = asuint(WideCells[Base + 3u]);
        uint4  Slot4   = asuint(WideCells[Base + 4u]);

        uint   PackedExp  = asuint(Slot0.w);
        float  StepX      = asfloat((PackedExp & 0xFFu) << 23u);
        float  StepY      = asfloat(((PackedExp >> 8u) & 0xFFu) << 23u);
        float  StepZ      = asfloat(((PackedExp >> 16u) & 0xFFu) << 23u);
        float3 OriginBias = (Slot0.xyz - LocalOrigin) * InvDir;
        float3 StepScaled = float3(StepX, StepY, StepZ) * InvDir;

        uint  HitChildIndices[8];
        float HitChildEntries[8];
        [unroll]
        for (uint ClearIdx = 0u; ClearIdx < 8u; ++ClearIdx)
        {
            HitChildIndices[ClearIdx] = 0u;
            HitChildEntries[ClearIdx] = 0.0f;
        }
        uint InteriorCount = 0u;

        for (uint C = 0u; C < 8u; ++C)
        {
            uint Meta = ExtractWideChildByte(Slot1.z, Slot1.w, C);
            if (Meta == 0u)
            {
                continue;
            }

            float3 QLo = float3(float(ExtractWideChildByte(Slot2.x, Slot2.y, C)),
                                float(ExtractWideChildByte(Slot3.x, Slot3.y, C)),
                                float(ExtractWideChildByte(Slot4.x, Slot4.y, C)));
            float3 QHi = float3(float(ExtractWideChildByte(Slot2.z, Slot2.w, C)),
                                float(ExtractWideChildByte(Slot3.z, Slot3.w, C)),
                                float(ExtractWideChildByte(Slot4.z, Slot4.w, C)));

            float3 T0    = OriginBias + QLo * StepScaled;
            float3 T1    = OriginBias + QHi * StepScaled;
            float3 TNear = min(T0, T1);
            float3 TFar  = max(T0, T1);
            float  Enter = max(LocalMinT, max(TNear.x, max(TNear.y, TNear.z)));
            float  Exit  = min(BestT, min(TFar.x, min(TFar.y, TFar.z)));
            if (Exit < Enter)
            {
                continue;
            }

            if ((Meta & 0x80u) != 0u)
            {
                HitChildIndices[InteriorCount] = Slot1.x + (Meta & 0x07u);
                HitChildEntries[InteriorCount] = Enter;
                ++InteriorCount;
            }
            else
            {
                uint TriCount   = (Meta >> 5u) & 0x03u;
                uint FirstFacet = Slot1.y + (Meta & 0x1Fu);
                for (uint T = 0u; T < TriCount; ++T)
                {
                    uint   FacetIdx  = FirstFacet + T;
                    uint   TriBase   = FacetIdx * WideTrianglesStride;
                    float4 Tri2      = WideTriangles[TriBase + 2u];
                    uint   ClassWord = asuint(Tri2.w);
                    bool   TriGlass  = (ClassWord & 0x80000000u) != 0u;
                    if (!IncludeGlass && TriGlass)
                    {
                        continue;
                    }
                    float4 Tri0 = WideTriangles[TriBase + 0u];
                    float4 Tri1 = WideTriangles[TriBase + 1u];
                    float  HitT, U, V;
                    if (IntersectMollerFacet(Tri0.xyz, Tri1.xyz, Tri2.xyz, LocalOrigin, LocalDirUnit, LocalMinT, BestT, HitT, U, V))
                    {
                        BestT     = HitT;
                        OutLocalT = HitT;
                        OutBaryU  = U;
                        OutBaryV  = V;
                        OutTriIdx = FacetIdx;
                    }
                }
            }
        }

        // Order interior children far-to-near so closest pops first.
        for (uint I = 1u; I < InteriorCount; ++I)
        {
            uint  KeyIdx = HitChildIndices[I];
            float KeyT   = HitChildEntries[I];
            int   J      = int(I) - 1;
            while (J >= 0 && HitChildEntries[J] < KeyT)
            {
                HitChildIndices[J + 1] = HitChildIndices[J];
                HitChildEntries[J + 1] = HitChildEntries[J];
                --J;
            }
            HitChildIndices[J + 1] = KeyIdx;
            HitChildEntries[J + 1] = KeyT;
        }
        for (uint I = 0u; I < InteriorCount; ++I)
        {
            if (HitChildEntries[I] <= BestT && StackTop < 20u)
            {
                WideStack[StackTop++] = HitChildIndices[I];
            }
        }
    }

    return OutTriIdx != 0xFFFFFFFFu;
}

//------------------------------------------------------------------------------------------------------------------------
//                           TWO-LEVEL INSTANCED TLAS + 8-WIDE CWBVH WORLD RAY QUERIES
//------------------------------------------------------------------------------------------------------------------------

BoundingRayHit SearchInstancedClosestRay(StructuredBuffer<float4> TlasCells,
                                         uint                     TlasCellCount,
                                         StructuredBuffer<float4> Instances,
                                         StructuredBuffer<float4> WideCells,
                                         StructuredBuffer<float4> WideTriangles,
                                         float3                   Origin,
                                         float3                   Direction,
                                         float                    MinT,
                                         float                    MaxT,
                                         bool                     IncludeGlass)
{
    BoundingRayHit Result;
    Result.Hit                   = false;
    Result.DistanceMeters        = MaxT;
    Result.TriangleIndex         = 0xFFFFFFFFu;
    Result.InstanceIndex         = 0u;
    Result.ClassificationId      = 0u;
    Result.TransmissiveGlass     = false;
    Result.RefractiveIndex       = 1.52f;
    Result.WorldPosition         = Origin;
    Result.PreviousWorldPosition = Origin;
    Result.GeometricNormal       = float3(0.0f, 1.0f, 0.0f);
    Result.ShadingNormal         = float3(0.0f, 1.0f, 0.0f);
    Result.TextureCoord          = float2(0.0f, 0.0f);
    Result.AbsorptionPerM        = float3(0.0f, 0.0f, 0.0f);

    if (TlasCellCount == 0u)
    {
        return Result;
    }

    float3 InvDir = SafeReciprocalRayDir(Direction);
    float  BestT  = MaxT;

    uint TlasStack[16];
    [unroll]
    for (uint S = 0u; S < 16u; ++S)
    {
        TlasStack[S] = 0u;
    }
    uint  StackTop  = 0u;
    float RootEntry = 0.0f;
    if (IntersectBoxSlab(TlasCells[0].xyz, TlasCells[1].xyz, Origin, InvDir, MinT, BestT, RootEntry))
    {
        TlasStack[StackTop++] = 0u;
    }

    uint   BestInstIdx = 0u;
    uint   BestTriIdx  = 0xFFFFFFFFu;
    float  BestBaryU   = 0.0f;
    float  BestBaryV   = 0.0f;
    float3 BestLocalP  = float3(0.0f, 0.0f, 0.0f);

    [loop]
    while (StackTop > 0u)
    {
        uint   CellIdx      = TlasStack[--StackTop];
        float4 CellLo       = TlasCells[CellIdx * 2u + 0u];
        float4 CellHi       = TlasCells[CellIdx * 2u + 1u];
        uint   LeftOrFirst  = asuint(CellLo.w);
        uint   RightOrCount = asuint(CellHi.w);

        if ((RightOrCount & InteriorEnclosureBit) != 0u)
        {
            uint  LeftIdx  = LeftOrFirst;
            uint  RightIdx = RightOrCount & ~InteriorEnclosureBit;
            float EntryL = 0.0f, EntryR = 0.0f;
            bool  HitL = IntersectBoxSlab(TlasCells[LeftIdx * 2u].xyz, TlasCells[LeftIdx * 2u + 1u].xyz, Origin, InvDir, MinT, BestT, EntryL);
            bool  HitR = IntersectBoxSlab(TlasCells[RightIdx * 2u].xyz, TlasCells[RightIdx * 2u + 1u].xyz, Origin, InvDir, MinT, BestT, EntryR);
            if (HitL && HitR)
            {
                if (EntryL < EntryR)
                {
                    if (StackTop + 2u <= 16u)
                    {
                        TlasStack[StackTop++] = RightIdx;
                        TlasStack[StackTop++] = LeftIdx;
                    }
                }
                else
                {
                    if (StackTop + 2u <= 16u)
                    {
                        TlasStack[StackTop++] = LeftIdx;
                        TlasStack[StackTop++] = RightIdx;
                    }
                }
            }
            else if (HitL && StackTop < 16u)
            {
                TlasStack[StackTop++] = LeftIdx;
            }
            else if (HitR && StackTop < 16u)
            {
                TlasStack[StackTop++] = RightIdx;
            }
        }
        else
        {
            uint   InstIdx     = LeftOrFirst;
            uint   InstBase    = InstIdx * InstanceRecordStride;
            uint   PackedFlags = asuint(Instances[InstBase + 10u].w);
            bool   ForceGlass  = (PackedFlags & 0x80000000u) != 0u;
            if (!IncludeGlass && ForceGlass)
            {
                continue;
            }

            float4 W2O0 = Instances[InstBase + 0u];
            float4 W2O1 = Instances[InstBase + 1u];
            float4 W2O2 = Instances[InstBase + 2u];

            float3 LocalO      = TransformAffinePoint3x4(W2O0, W2O1, W2O2, Origin);
            float3 LocalD      = TransformAffineVector3x4(W2O0, W2O1, W2O2, Direction);
            float  DirScale    = sqrt(max(dot(LocalD, LocalD), 1.0e-12f));
            float3 LocalDUnit  = LocalD / DirScale;
            uint   WideRootIdx = asuint(Instances[InstBase + 9u].w);

            float LocalT, U, V;
            uint  TriIdx;
            if (SearchLocalWideBlas(WideCells, WideTriangles, WideRootIdx, LocalO, LocalDUnit,
                                    MinT * DirScale, BestT * DirScale, IncludeGlass, LocalT, U, V, TriIdx))
            {
                float WorldT = LocalT / DirScale;
                if (WorldT >= MinT && WorldT < BestT)
                {
                    BestT       = WorldT;
                    BestInstIdx = InstIdx;
                    BestTriIdx  = TriIdx;
                    BestBaryU   = U;
                    BestBaryV   = V;
                    BestLocalP  = LocalO + LocalDUnit * LocalT;
                }
            }
        }
    }

    if (BestTriIdx == 0xFFFFFFFFu)
    {
        return Result;
    }

    uint   InstBase    = BestInstIdx * InstanceRecordStride;
    float4 W2O0        = Instances[InstBase + 0u];
    float4 W2O1        = Instances[InstBase + 1u];
    float4 W2O2        = Instances[InstBase + 2u];
    float4 O2W0        = Instances[InstBase + 3u];
    float4 O2W1        = Instances[InstBase + 4u];
    float4 O2W2        = Instances[InstBase + 5u];
    float4 PrevO2W0    = Instances[InstBase + 6u];
    float4 PrevO2W1    = Instances[InstBase + 7u];
    float4 PrevO2W2    = Instances[InstBase + 8u];
    uint   PackedFlags = asuint(Instances[InstBase + 10u].w);
    uint   OverrideId  = PackedFlags & 0x7FFFFFFFu;
    bool   ForceGlass  = (PackedFlags & 0x80000000u) != 0u;
    float4 InstGlass   = Instances[InstBase + 11u];

    uint   TriBase   = BestTriIdx * WideTrianglesStride;
    float4 Tri0      = WideTriangles[TriBase + 0u];
    float4 Tri1      = WideTriangles[TriBase + 1u];
    float4 Tri2      = WideTriangles[TriBase + 2u];
    float4 Tri3      = WideTriangles[TriBase + 3u];
    float4 Tri4      = WideTriangles[TriBase + 4u];
    uint4  Tri5Words = asuint(WideTriangles[TriBase + 5u]);

    float  BaryW      = 1.0f - BestBaryU - BestBaryV;
    float3 LocalGeomN = SafeNormalizeVec3(cross(Tri1.xyz, Tri2.xyz));
    float3 Vn0        = UnpackOctahedralNormal(Tri5Words.x);
    float3 Vn1        = UnpackOctahedralNormal(Tri5Words.y);
    float3 Vn2        = UnpackOctahedralNormal(Tri5Words.z);
    float3 LocalShade = SafeNormalizeVec3(BaryW * Vn0 + BestBaryU * Vn1 + BestBaryV * Vn2);
    if (dot(LocalShade, LocalGeomN) < 0.0f)
    {
        LocalShade = -LocalShade;
    }

    float3 WorldGeomN = SafeNormalizeVec3(float3(
        W2O0.x * LocalGeomN.x + W2O1.x * LocalGeomN.y + W2O2.x * LocalGeomN.z,
        W2O0.y * LocalGeomN.x + W2O1.y * LocalGeomN.y + W2O2.y * LocalGeomN.z,
        W2O0.z * LocalGeomN.x + W2O1.z * LocalGeomN.y + W2O2.z * LocalGeomN.z));
    float3 WorldShadeN = SafeNormalizeVec3(float3(
        W2O0.x * LocalShade.x + W2O1.x * LocalShade.y + W2O2.x * LocalShade.z,
        W2O0.y * LocalShade.x + W2O1.y * LocalShade.y + W2O2.y * LocalShade.z,
        W2O0.z * LocalShade.x + W2O1.z * LocalShade.y + W2O2.z * LocalShade.z));

    if (dot(WorldGeomN, Direction) > 0.0f)
    {
        WorldGeomN  = -WorldGeomN;
        WorldShadeN = -WorldShadeN;
    }

    uint TriClassWord = asuint(Tri2.w);
    uint FinalClass   = (OverrideId != 0x7FFFFFFFu) ? OverrideId : (TriClassWord & 0x7FFFFFFFu);
    bool FinalGlass   = ForceGlass || ((TriClassWord & 0x80000000u) != 0u);

    Result.Hit                   = true;
    Result.DistanceMeters        = BestT;
    Result.TriangleIndex         = BestTriIdx;
    Result.InstanceIndex         = BestInstIdx;
    Result.ClassificationId      = FinalClass;
    Result.TransmissiveGlass     = FinalGlass;
    Result.RefractiveIndex       = ForceGlass ? InstGlass.w : Tri4.w;
    Result.AbsorptionPerM        = ForceGlass ? InstGlass.xyz : Tri4.xyz;
    Result.WorldPosition         = TransformAffinePoint3x4(O2W0, O2W1, O2W2, BestLocalP);
    Result.PreviousWorldPosition = TransformAffinePoint3x4(PrevO2W0, PrevO2W1, PrevO2W2, BestLocalP);
    Result.GeometricNormal       = WorldGeomN;
    Result.ShadingNormal         = WorldShadeN;
    Result.TextureCoord          = BaryW * float2(Tri0.w, Tri1.w) + BestBaryU * Tri3.xy + BestBaryV * Tri3.zw;
    return Result;
}

float3 SearchInstancedShadowTransmittance(StructuredBuffer<float4> TlasCells,
                                          uint                     TlasCellCount,
                                          StructuredBuffer<float4> Instances,
                                          StructuredBuffer<float4> WideCells,
                                          StructuredBuffer<float4> WideTriangles,
                                          float3                   Origin,
                                          float3                   Direction,
                                          float                    MaxT)
{
    if (TlasCellCount == 0u)
    {
        return float3(1.0f, 1.0f, 1.0f);
    }

    float3 InvDir        = SafeReciprocalRayDir(Direction);
    float3 Transmittance = float3(1.0f, 1.0f, 1.0f);

    uint TlasStack[16];
    [unroll]
    for (uint S = 0u; S < 16u; ++S)
    {
        TlasStack[S] = 0u;
    }
    uint  StackTop  = 0u;
    float RootEntry = 0.0f;
    if (IntersectBoxSlab(TlasCells[0].xyz, TlasCells[1].xyz, Origin, InvDir, 1.5e-3f, MaxT, RootEntry))
    {
        TlasStack[StackTop++] = 0u;
    }

    [loop]
    while (StackTop > 0u)
    {
        uint   CellIdx      = TlasStack[--StackTop];
        float4 CellLo       = TlasCells[CellIdx * 2u + 0u];
        float4 CellHi       = TlasCells[CellIdx * 2u + 1u];
        uint   LeftOrFirst  = asuint(CellLo.w);
        uint   RightOrCount = asuint(CellHi.w);

        if ((RightOrCount & InteriorEnclosureBit) != 0u)
        {
            uint  LeftIdx  = LeftOrFirst;
            uint  RightIdx = RightOrCount & ~InteriorEnclosureBit;
            float EntryL = 0.0f, EntryR = 0.0f;
            if (IntersectBoxSlab(TlasCells[LeftIdx * 2u].xyz, TlasCells[LeftIdx * 2u + 1u].xyz, Origin, InvDir, 1.5e-3f, MaxT, EntryL) && StackTop < 16u)
            {
                TlasStack[StackTop++] = LeftIdx;
            }
            if (IntersectBoxSlab(TlasCells[RightIdx * 2u].xyz, TlasCells[RightIdx * 2u + 1u].xyz, Origin, InvDir, 1.5e-3f, MaxT, EntryR) && StackTop < 16u)
            {
                TlasStack[StackTop++] = RightIdx;
            }
        }
        else
        {
            uint   InstIdx     = LeftOrFirst;
            uint   InstBase    = InstIdx * InstanceRecordStride;
            float4 W2O0        = Instances[InstBase + 0u];
            float4 W2O1        = Instances[InstBase + 1u];
            float4 W2O2        = Instances[InstBase + 2u];
            uint   WideRootIdx = asuint(Instances[InstBase + 9u].w);
            uint   PackedFlags = asuint(Instances[InstBase + 10u].w);
            bool   ForceGlass  = (PackedFlags & 0x80000000u) != 0u;
            float4 InstGlass   = Instances[InstBase + 11u];

            float3 LocalO     = TransformAffinePoint3x4(W2O0, W2O1, W2O2, Origin);
            float3 LocalD     = TransformAffineVector3x4(W2O0, W2O1, W2O2, Direction);
            float  DirScale   = sqrt(max(dot(LocalD, LocalD), 1.0e-12f));
            float3 LocalDUnit = LocalD / DirScale;

            float LocalT, U, V;
            uint  TriIdx;
            if (SearchLocalWideBlas(WideCells, WideTriangles, WideRootIdx, LocalO, LocalDUnit,
                                    1.5e-3f * DirScale, MaxT * DirScale, true, LocalT, U, V, TriIdx))
            {
                uint   TriBase  = TriIdx * WideTrianglesStride;
                uint   TriWords = asuint(WideTriangles[TriBase + 2u].w);
                bool   TriGlass = ForceGlass || ((TriWords & 0x80000000u) != 0u);
                if (!TriGlass)
                {
                    return float3(0.0f, 0.0f, 0.0f);
                }
                float4 Tri4 = WideTriangles[TriBase + 4u];
                float  Ior  = ForceGlass ? InstGlass.w : max(Tri4.w, 1.01f);
                float3 AbsM = ForceGlass ? InstGlass.xyz : Tri4.xyz;
                float  F0   = pow((Ior - 1.0f) / (Ior + 1.0f), 2.0f);
                Transmittance *= (1.0f - F0) * exp(-AbsM * 0.015f);
                if (max(Transmittance.r, max(Transmittance.g, Transmittance.b)) < 1.0e-3f)
                {
                    return float3(0.0f, 0.0f, 0.0f);
                }
            }
        }
    }

    return Transmittance;
}

//------------------------------------------------------------------------------------------------------------------------
//                      CURVED MULTI-INTERFACE DIELECTRIC SNELL REFRACTION & TIR PEEL
//------------------------------------------------------------------------------------------------------------------------

struct DielectricGlassPeel
{
    bool   EncounteredGlass;
    bool   TotalInternalReflection;
    float  EntryFresnelReflectance;
    float3 EntryWorldPosition;
    float3 EntryNormal;
    float3 EntryReflectedRay;
    float3 TransmittedRayOrigin;
    float3 TransmittedRayDirection;
    float3 BeerLambertThroughput;
};

DielectricGlassPeel PeelInstancedDielectricGlass(StructuredBuffer<float4> TlasCells,
                                                 uint                     TlasCellCount,
                                                 StructuredBuffer<float4> Instances,
                                                 StructuredBuffer<float4> WideCells,
                                                 StructuredBuffer<float4> WideTriangles,
                                                 BoundingRayHit           FirstHit,
                                                 float3                   IncidentDirection)
{
    DielectricGlassPeel Peel;
    Peel.EncounteredGlass        = FirstHit.Hit && FirstHit.TransmissiveGlass;
    Peel.TotalInternalReflection = false;
    Peel.EntryFresnelReflectance = 0.04f;
    Peel.EntryWorldPosition      = FirstHit.WorldPosition;
    Peel.EntryNormal             = FirstHit.ShadingNormal;
    Peel.EntryReflectedRay       = reflect(IncidentDirection, FirstHit.ShadingNormal);
    Peel.TransmittedRayOrigin    = FirstHit.WorldPosition;
    Peel.TransmittedRayDirection = IncidentDirection;
    Peel.BeerLambertThroughput   = float3(1.0f, 1.0f, 1.0f);

    if (!Peel.EncounteredGlass)
    {
        return Peel;
    }

    float  Ior = max(FirstHit.RefractiveIndex, 1.02f);
    float  F1  = 0.04f;
    bool   Tir1 = false;
    float3 Refl1, Refr1;
    EvaluateDielectricBoundary(IncidentDirection, FirstHit.ShadingNormal, 1.0f / Ior, F1, Tir1, Refl1, Refr1);

    Peel.EntryFresnelReflectance = F1;
    Peel.EntryReflectedRay       = Refl1;

    float3 InteriorOrigin = FirstHit.WorldPosition - FirstHit.GeometricNormal * 1.5e-3f;
    BoundingRayHit SecondHit = SearchInstancedClosestRay(
        TlasCells, TlasCellCount, Instances, WideCells, WideTriangles,
        InteriorOrigin, Refr1, 1.5e-3f, 0.35f, true);

    if (SecondHit.Hit && SecondHit.TransmissiveGlass)
    {
        float  PaneThickness = clamp(SecondHit.DistanceMeters, 0.004f, 0.25f);
        float  F2            = 0.04f;
        bool   Tir2          = false;
        float3 Refl2, Refr2;
        EvaluateDielectricBoundary(Refr1, SecondHit.ShadingNormal, Ior, F2, Tir2, Refl2, Refr2);

        Peel.BeerLambertThroughput = (1.0f - F1) * exp(-FirstHit.AbsorptionPerM * PaneThickness);
        if (Tir2)
        {
            // Total internal reflection inside curved solid dielectric: bounce internally and exit
            Peel.TotalInternalReflection = true;
            Peel.TransmittedRayOrigin    = SecondHit.WorldPosition + SecondHit.GeometricNormal * 1.8e-3f;
            Peel.TransmittedRayDirection = Refl2;
        }
        else
        {
            Peel.BeerLambertThroughput *= (1.0f - F2 * 0.5f);
            Peel.TransmittedRayOrigin    = SecondHit.WorldPosition - SecondHit.GeometricNormal * 1.8e-3f;
            Peel.TransmittedRayDirection = Refr2;
        }
    }
    else
    {
        // Thin curved automotive laminated glass sheet (~6 mm optical path with second-surface exit bending recovery)
        float  SheetThickness = 0.0065f;
        float  F2             = 0.04f;
        bool   Tir2           = false;
        float3 Refl2, Refr2;
        // Curved windshield slight normal convergence bends exiting ray realistically by ~1.5 deg while restoring medium
        float3 ExitNormal = normalize(-FirstHit.ShadingNormal + IncidentDirection * 0.035f);
        EvaluateDielectricBoundary(Refr1, ExitNormal, Ior, F2, Tir2, Refl2, Refr2);

        Peel.TotalInternalReflection = Tir2;
        Peel.BeerLambertThroughput   = (1.0f - F1) * exp(-FirstHit.AbsorptionPerM * SheetThickness);
        Peel.TransmittedRayOrigin    = FirstHit.WorldPosition + Refr1 * SheetThickness;
        Peel.TransmittedRayDirection = Tir2 ? Refl2 : Refr2;
    }

    return Peel;
}

//------------------------------------------------------------------------------------------------------------------------
//                           FLUX-WEIGHTED EMISSIVE FACET CDF SAMPLING
//------------------------------------------------------------------------------------------------------------------------

struct SampledEmissiveFacet
{
    bool   Valid;
    float3 WorldPosition;
    float3 WorldNormal;
    float3 EmittedRadiance;
    float  SolidAnglePdf;
    float  DistanceMeters;
    float3 DirectionToLight;
};

SampledEmissiveFacet SampleEmissiveFacetCdf(StructuredBuffer<float4> EmissiveFacets,
                                            uint                     EmissiveFacetCount,
                                            float3                   ShadingPoint,
                                            float                    SelectorU,
                                            float                    BarySampleU,
                                            float                    BarySampleV)
{
    SampledEmissiveFacet Out;
    Out.Valid            = false;
    Out.WorldPosition    = ShadingPoint;
    Out.WorldNormal      = float3(0.0f, 1.0f, 0.0f);
    Out.EmittedRadiance  = float3(0.0f, 0.0f, 0.0f);
    Out.SolidAnglePdf    = 0.0f;
    Out.DistanceMeters   = 0.0f;
    Out.DirectionToLight = float3(0.0f, 1.0f, 0.0f);

    if (EmissiveFacetCount == 0u)
    {
        return Out;
    }

    float U  = clamp(SelectorU, 0.0f, 0.9999999f);
    uint  Lo = 0u;
    uint  Hi = EmissiveFacetCount - 1u;
    [loop]
    while (Lo < Hi)
    {
        uint  Mid      = Lo + ((Hi - Lo) >> 1u);
        float CdfUpper = EmissiveFacets[Mid * 4u + 2u].w;
        if (U >= CdfUpper)
        {
            Lo = Mid + 1u;
        }
        else
        {
            Hi = Mid;
        }
    }

    uint   Base  = Lo * 4u;
    float4 Row0  = EmissiveFacets[Base + 0u]; // WorldV0.xyz, AreaM2
    float4 Row1  = EmissiveFacets[Base + 1u]; // WorldE1.xyz, CdfLower
    float4 Row2  = EmissiveFacets[Base + 2u]; // WorldE2.xyz, CdfUpper
    float4 Row3  = EmissiveFacets[Base + 3u]; // Radiance.rgb, SelectionProb

    float  RootU = sqrt( saturate(BarySampleU) );
    float  TriU  = 1.0f - RootU;
    float  TriV  = saturate(BarySampleV) * RootU;
    float3 Pos   = Row0.xyz + TriU * Row1.xyz + TriV * Row2.xyz;
    float3 Norm  = normalize(cross(Row1.xyz, Row2.xyz));

    float3 ToLight = Pos - ShadingPoint;
    float  DistSq  = max(dot(ToLight, ToLight), 1.0e-4f);
    float  Dist    = sqrt(DistSq);
    float3 L       = ToLight / Dist;

    // Two-sided architectural / vehicle luminaire cosine factor
    float  CosLight = max(abs(dot(Norm, -L)), 1.0e-3f);
    float  AreaPdf  = Row3.w / max(Row0.w, 1.0e-6f);
    float  OmegaPdf = AreaPdf * DistSq / CosLight;

    Out.Valid            = (OmegaPdf > 1.0e-7f);
    Out.WorldPosition    = Pos;
    Out.WorldNormal      = Norm;
    Out.EmittedRadiance  = Row3.xyz;
    Out.SolidAnglePdf    = OmegaPdf;
    Out.DistanceMeters   = Dist;
    Out.DirectionToLight = L;
    return Out;
}
