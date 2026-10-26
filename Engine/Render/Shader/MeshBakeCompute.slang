/*============================================================================================================================================
                                                         MESHBAKECOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 GPU port of Engine/ContentInterchange/BakeMesh.cpp's RunMeshBake per-texel kernel: one compute invocation per
// output texel, evaluating every requested mesh map from a shared UV-space low-mesh rasterization and a shared
// high-mesh BVH ray cast, exactly mirroring the CPU reference's control flow, gating and formulas line for line.
// 📝 See Engine/Render/MeshBakeGpuMirror.h for a Vulkan-free C++ transcription of this same kernel, regression
// tested against RunMeshBake's trusted CPU output (Exhibits/Renderer/Code/MeshBakeGpuMirrorValidation.cpp) —
// this shader is a near-verbatim structural copy of that mirror, so the mirror's correctness carries over here.

// 📝 Canonical channel indices: must match Engine/Render/MeshBakeGpu.h's MeshBakeGpuChannelIndex enum exactly.
static const uint kChannelNormal = 0, kChannelWorldNormal = 1, kChannelObjectNormal = 2, kChannelBentNormal = 3,
    kChannelBentWorldNormal = 4, kChannelBentObjectNormal = 5, kChannelBevelNormal = 6, kChannelOcclusion = 7,
    kChannelOcclusionSecondary = 8, kChannelCurvature = 9, kChannelConvexity = 10, kChannelCavity = 11,
    kChannelThickness = 12, kChannelHeight = 13, kChannelVectorDisplacement = 14, kChannelPosition = 15,
    kChannelId = 16, kChannelUvIsland = 17, kChannelUv = 18, kChannelWireframe = 19, kChannelCoverage = 20,
    kChannelAlpha = 21, kChannelBevelMask = 22, kChannelDust = 23, kChannelDirt = 24, kChannelEdgeWear = 25,
    kChannelBaseColor = 26, kChannelCount = 27;
static const uint kInvalidSlot = 0xffffffffu;
static const uint kBvhStackDepth = 64;

struct GpuLowVertex { float4 Position; float4 Normal; float4 UvChart; };
struct GpuTriangle { float4 A; float4 B; float4 C; float4 NormalA; float4 NormalB; float4 NormalC; uint4 Meta; };
struct GpuBvhNode { float4 Minimum; float4 Maximum; int4 LeftRightStartCount; };
struct GpuBakeSettings
{
    // 📝 Explicit affine rows instead of a float4x4: Slang/HLSL's default row-major-vs-column-major matrix
    // packing is ambiguous without an explicit modifier, and that ambiguity cannot be caught without actually
    // running the shader. Three explicit rows (row 3 is always (0,0,0,1) and is never read, exactly like
    // BakeMesh.cpp's Transform()/TransformLocalNormal() which also never read ObjectToWorld's last row) removes
    // the ambiguity entirely: both this shader and MeshBakeGpu.cpp's upload agree on RowN.{x,y,z,w} = M(N,0..3).
    float4 ObjectToWorldRow0;
    float4 ObjectToWorldRow1;
    float4 ObjectToWorldRow2;
    float4 BoundsMin;               // xyz
    float4 BoundsMax;               // xyz
    float4 DiagonalEpsilon;         // xyz = Diagonal, w = Epsilon
    float4 FrontBackAo;             // x=Front, y=Back, z=AoDistance, w=SecondaryAoDistance
    float4 ThicknessCurvWireMask;   // x=ThicknessRange, y=CurvatureRadius, z=WireWidth, w=MaskStrength
    float4 BevelRadiusStrength;     // x=BevelRadius, y=BevelStrength, zw=unused
    float4 DustUpFalloff;           // xyz = DustUp, w = DustFalloff
    uint4  SampleCounts;            // x=Samples, y=BevelSamples, z=Resolution, w=LowIndexCount
    uint4  Flags;                   // x=NeedsMask, y=NormalPlusY(0/1), zw=unused
};
// 📝 Flags.x bit layout - must match Engine/Render/MeshBakeGpu.h's MeshBakeGpuNeedsFlag enum exactly.
static const uint kNeedHemisphereSampling = 1u, kNeedCurvatureSampling = 2u, kNeedThickness = 4u, kNeedBevel = 8u;

[[vk::binding(0, 0)]] StructuredBuffer<GpuLowVertex> LowVertices;
[[vk::binding(1, 0)]] StructuredBuffer<uint> LowIndices;
[[vk::binding(2, 0)]] StructuredBuffer<GpuTriangle> HighTriangles;
[[vk::binding(3, 0)]] StructuredBuffer<GpuBvhNode> BvhNodes;
[[vk::binding(4, 0)]] StructuredBuffer<uint> BvhOrder;
[[vk::binding(5, 0)]] StructuredBuffer<GpuBakeSettings> Settings;
[[vk::binding(6, 0)]] StructuredBuffer<uint> ChannelSlots; // size kChannelCount; kInvalidSlot when not requested
[[vk::binding(7, 0)]] RWStructuredBuffer<uint> Output;     // size Resolution*Resolution*requestedChannelCount

//------------------------------------------------------------------------------------------------------------------------
//                                                     TRIANGLE BVH (mirrors BakeMesh.cpp's BakeTriangleBvh)
//------------------------------------------------------------------------------------------------------------------------

bool RayBoxTest(float3 Origin, float3 InvDirection, float3 Minimum, float3 Maximum, float MinDistance, float MaxDistance)
{
    float3 T0 = (Minimum - Origin) * InvDirection;
    float3 T1 = (Maximum - Origin) * InvDirection;
    float3 Lower = min(T0, T1), Upper = max(T0, T1);
    MinDistance = max(MinDistance, max(Lower.x, max(Lower.y, Lower.z)));
    MaxDistance = min(MaxDistance, min(Upper.x, min(Upper.y, Upper.z)));
    return MaxDistance >= MinDistance;
}
// 📝 Möller–Trumbore; returns the barycentric-interpolated normal on hit, same tolerances as RayTriangle in BakeMesh.cpp.
bool RayTriangleTest(float3 Origin, float3 Direction, float3 A, float3 B, float3 C, float MinDistance, float MaxDistance,
    out float OutDistance, out float3 OutBarycentric)
{
    OutDistance = 0; OutBarycentric = float3(0, 0, 0);
    float3 Edge1 = B - A, Edge2 = C - A;
    float3 PVector = cross(Direction, Edge2);
    float Determinant = dot(Edge1, PVector);
    if (abs(Determinant) < 1e-12) return false;
    float InverseDeterminant = 1.0 / Determinant;
    float3 TVector = Origin - A;
    float U = dot(TVector, PVector) * InverseDeterminant;
    if (U < -1e-5 || U > 1.0 + 1e-5) return false;
    float3 QVector = cross(TVector, Edge1);
    float V = dot(Direction, QVector) * InverseDeterminant;
    if (V < -1e-5 || U + V > 1.0 + 1e-5) return false;
    float Distance = dot(Edge2, QVector) * InverseDeterminant;
    if (Distance < MinDistance || Distance > MaxDistance) return false;
    OutDistance = Distance;
    OutBarycentric = float3(1.0 - U - V, U, V);
    return true;
}
struct BvhHit { bool Found; float Distance; uint PartIndex; float3 Position; float3 Normal; };
BvhHit BvhCastRay(float3 Origin, float3 Direction, float MinDistance, float MaxDistance)
{
    BvhHit Result; Result.Found = false; Result.Distance = 0; Result.PartIndex = 0;
    Result.Position = float3(0, 0, 0); Result.Normal = float3(0, 0, 0);
    float3 InvDirection = 1.0 / Direction;
    float BestDistance = MaxDistance;
    int Stack[kBvhStackDepth]; uint StackPointer = 0; Stack[StackPointer++] = 0;
    while (StackPointer > 0)
    {
        int NodeIndex = Stack[--StackPointer];
        GpuBvhNode Node = BvhNodes[NodeIndex];
        if (!RayBoxTest(Origin, InvDirection, Node.Minimum.xyz, Node.Maximum.xyz, MinDistance, BestDistance)) continue;
        if (Node.LeftRightStartCount.x < 0)
        {
            int Start = Node.LeftRightStartCount.z, Count = Node.LeftRightStartCount.w;
            for (int Index = 0; Index < Count; ++Index)
            {
                GpuTriangle Tri = HighTriangles[BvhOrder[Start + Index]];
                float Distance; float3 Barycentric;
                if (RayTriangleTest(Origin, Direction, Tri.A.xyz, Tri.B.xyz, Tri.C.xyz, MinDistance, BestDistance, Distance, Barycentric))
                {
                    BestDistance = Distance;
                    Result.Found = true; Result.Distance = Distance; Result.PartIndex = Tri.Meta.x;
                    Result.Position = Origin + Direction * Distance;
                    Result.Normal = normalize(Barycentric.x * Tri.NormalA.xyz + Barycentric.y * Tri.NormalB.xyz + Barycentric.z * Tri.NormalC.xyz);
                }
            }
        }
        else if (StackPointer + 2 <= kBvhStackDepth)
        {
            Stack[StackPointer++] = Node.LeftRightStartCount.x;
            Stack[StackPointer++] = Node.LeftRightStartCount.y;
        }
    }
    return Result;
}
float BoxDistanceSquared(float3 Point, float3 Minimum, float3 Maximum)
{
    float3 Delta = max(Minimum - Point, max(Point - Maximum, float3(0, 0, 0)));
    return dot(Delta, Delta);
}
// 📝 Ericson, "Real-Time Collision Detection" 5.1.5 — identical branch structure to ClosestPointOnTriangle in BakeMesh.cpp.
float3 ClosestPointOnTriangle(float3 Point, float3 A, float3 B, float3 C)
{
    float3 Ab = B - A, Ac = C - A, Ap = Point - A;
    float D1 = dot(Ab, Ap), D2 = dot(Ac, Ap);
    if (D1 <= 0 && D2 <= 0) return A;
    float3 Bp = Point - B;
    float D3 = dot(Ab, Bp), D4 = dot(Ac, Bp);
    if (D3 >= 0 && D4 <= D3) return B;
    float Vc = D1 * D4 - D3 * D2;
    if (Vc <= 0 && D1 >= 0 && D3 <= 0) return A + Ab * (D1 / (D1 - D3));
    float3 Cp = Point - C;
    float D5 = dot(Ab, Cp), D6 = dot(Ac, Cp);
    if (D6 >= 0 && D5 <= D6) return C;
    float Vb = D5 * D2 - D1 * D6;
    if (Vb <= 0 && D2 >= 0 && D6 <= 0) return A + Ac * (D2 / (D2 - D6));
    float Va = D3 * D6 - D5 * D4;
    if (Va <= 0 && (D4 - D3) >= 0 && (D5 - D6) >= 0) return B + (C - B) * ((D4 - D3) / ((D4 - D3) + (D5 - D6)));
    float Denominator = 1.0 / (Va + Vb + Vc);
    return A + Ab * (Vb * Denominator) + Ac * (Vc * Denominator);
}
float3 PlaneBarycentric(float3 Point, float3 A, float3 B, float3 C)
{
    float3 V0 = B - A, V1 = C - A, V2 = Point - A;
    float D00 = dot(V0, V0), D01 = dot(V0, V1), D11 = dot(V1, V1), D20 = dot(V2, V0), D21 = dot(V2, V1);
    float Denominator = D00 * D11 - D01 * D01;
    if (abs(Denominator) < 1e-20) return float3(1, 0, 0);
    float V = (D11 * D20 - D01 * D21) / Denominator, W = (D00 * D21 - D01 * D20) / Denominator;
    return float3(1.0 - V - W, V, W);
}
struct BvhClosest { bool Found; float Distance; uint PartIndex; float3 Point; float3 Normal; };
BvhClosest BvhClosestPoint(float3 Point, float MaxDistance)
{
    BvhClosest Result; Result.Found = false; Result.Distance = 0; Result.PartIndex = 0;
    Result.Point = float3(0, 0, 0); Result.Normal = float3(0, 0, 0);
    float BestDistanceSquared = MaxDistance * MaxDistance;
    int Stack[kBvhStackDepth]; uint StackPointer = 0; Stack[StackPointer++] = 0;
    while (StackPointer > 0)
    {
        int NodeIndex = Stack[--StackPointer];
        GpuBvhNode Node = BvhNodes[NodeIndex];
        if (BoxDistanceSquared(Point, Node.Minimum.xyz, Node.Maximum.xyz) > BestDistanceSquared) continue;
        if (Node.LeftRightStartCount.x < 0)
        {
            int Start = Node.LeftRightStartCount.z, Count = Node.LeftRightStartCount.w;
            for (int Index = 0; Index < Count; ++Index)
            {
                GpuTriangle Tri = HighTriangles[BvhOrder[Start + Index]];
                float3 Closest = ClosestPointOnTriangle(Point, Tri.A.xyz, Tri.B.xyz, Tri.C.xyz);
                float3 Delta = Closest - Point;
                float DistanceSquared = dot(Delta, Delta);
                if (DistanceSquared < BestDistanceSquared)
                {
                    BestDistanceSquared = DistanceSquared;
                    Result.Found = true; Result.Distance = sqrt(DistanceSquared); Result.PartIndex = Tri.Meta.x; Result.Point = Closest;
                    float3 Weights = PlaneBarycentric(Closest, Tri.A.xyz, Tri.B.xyz, Tri.C.xyz);
                    Result.Normal = normalize(Weights.x * Tri.NormalA.xyz + Weights.y * Tri.NormalB.xyz + Weights.z * Tri.NormalC.xyz);
                }
            }
        }
        else if (StackPointer + 2 <= kBvhStackDepth)
        {
            int Left = Node.LeftRightStartCount.x, Right = Node.LeftRightStartCount.y;
            float LeftDistance = BoxDistanceSquared(Point, BvhNodes[Left].Minimum.xyz, BvhNodes[Left].Maximum.xyz);
            float RightDistance = BoxDistanceSquared(Point, BvhNodes[Right].Minimum.xyz, BvhNodes[Right].Maximum.xyz);
            // 📝 Push the farther child first so the nearer one pops first, same ordering as BakeTriangleBvh::ClosestPoint.
            if (LeftDistance <= RightDistance) { Stack[StackPointer++] = Right; Stack[StackPointer++] = Left; }
            else { Stack[StackPointer++] = Left; Stack[StackPointer++] = Right; }
        }
    }
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SHARED HELPERS (mirror BakeMesh.cpp's anonymous namespace)
//------------------------------------------------------------------------------------------------------------------------

void TangentBasis(float3 Normal, out float3 Tangent, out float3 Bitangent)
{
    float3 UpReference = float3(0, 1, 0), SideReference = float3(1, 0, 0);
    Tangent = normalize(cross(abs(Normal.y) < 0.99 ? UpReference : SideReference, Normal));
    Bitangent = normalize(cross(Normal, Tangent));
}
// 📝 1:1 with meshBakeCore.js's hemisphere(normal) via BakeMesh.cpp's Hemisphere().
void Hemisphere(float3 Normal, out float3 Tangent, out float3 Bitangent)
{
    float3 ZReference = float3(0, 0, 1), YReference = float3(0, 1, 0);
    Tangent = normalize(cross(abs(Normal.z) < 0.9 ? ZReference : YReference, Normal));
    Bitangent = cross(Normal, Tangent);
}
// 📝 Row0/Row1/Row2 are the object-to-world matrix's first three rows (row 3 is always (0,0,0,1) and unused),
// matching BakeMesh.cpp's TransformLocalNormal() exactly: the plain transpose of the upper 3x3, not a true
// inverse-transpose (only exact for orthonormal object-to-world matrices, same documented limitation as the CPU).
float3 TransformLocalNormal(float4 Row0, float4 Row1, float4 Row2, float3 World)
{
    float3 Out;
    Out.x = Row0.x * World.x + Row1.x * World.y + Row2.x * World.z;
    Out.y = Row0.y * World.x + Row1.y * World.y + Row2.y * World.z;
    Out.z = Row0.z * World.x + Row1.z * World.y + Row2.z * World.z;
    return normalize(Out);
}
float3 TransformPoint(float4 Row0, float4 Row1, float4 Row2, float3 Local)
{
    float4 Local4 = float4(Local, 1.0);
    return float3(dot(Row0, Local4), dot(Row1, Local4), dot(Row2, Local4));
}
float3 UvIslandColor(uint Id)
{
    uint Code = (Id * 0x9e3779u) & 0xffffffu;
    return float3(float(Code & 255u), float((Code >> 8) & 255u), float((Code >> 16) & 255u)) / 255.0;
}
// 📝 1:1 with BakeMesh.cpp's AverageNormal: a local high-normal average around Point within Radius, biased
// slightly inward, weighted by distance falloff and rejecting samples facing sharply away.
float3 AverageNormal(float3 Point, float3 Normal, float3 Facing, float Radius, uint Samples, float Epsilon)
{
    if (Radius <= Epsilon) return Normal;
    float3 Tangent, Bitangent;
    Hemisphere(Normal, Tangent, Bitangent);
    float3 Sum = Normal * 2.0;
    float Weight = 2.0;
    float SearchRadius = Radius * 1.8;
    uint EffectiveSamples = max(1u, Samples);
    for (uint K = 0; K < Samples; ++K)
    {
        float Angle = (float(K) / float(EffectiveSamples)) * 6.283185307179586;
        float3 Query = Point + Tangent * cos(Angle) * Radius + Bitangent * sin(Angle) * Radius - Normal * Radius * 0.3;
        BvhClosest Near = BvhClosestPoint(Query, SearchRadius);
        if (!Near.Found || Near.Distance > SearchRadius) continue;
        if (dot(Near.Normal, Normal) < -0.1 || dot(Near.Normal, Facing) < -1e-6) continue;
        float SampleWeight = 1.0 - saturate(Near.Distance / SearchRadius);
        Sum += Near.Normal * SampleWeight;
        Weight += SampleWeight;
    }
    return normalize(Sum / Weight);
}
uint PackRgba8(float4 Colour)
{
    uint4 Bytes = uint4(clamp(Colour, 0.0, 1.0) * 255.0 + 0.5);
    return Bytes.x | (Bytes.y << 8) | (Bytes.z << 16) | (Bytes.w << 24);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     MAIN PER-TEXEL KERNEL
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 1)]
void MeshBakeCompute(uint3 Pixel : SV_DispatchThreadID)
{
    GpuBakeSettings S = Settings[0];
    uint Resolution = S.SampleCounts.z;
    if (Pixel.x >= Resolution || Pixel.y >= Resolution) return;
    uint X = Pixel.x, Y = Pixel.y;
    float PixelU = (float(X) + 0.5) / float(Resolution);
    float PixelV = 1.0 - (float(Y) + 0.5) / float(Resolution); // texture v grows upward, matches RunMeshBake

    uint NeedsMask = S.Flags.x;
    bool NormalPlusY = S.Flags.y != 0;
    float Epsilon = S.DiagonalEpsilon.w;
    float3 Diagonal = S.DiagonalEpsilon.xyz;
    float Front = S.FrontBackAo.x, Back = S.FrontBackAo.y, AoDistance = S.FrontBackAo.z, SecondaryAoDistance = S.FrontBackAo.w;
    float ThicknessRange = S.ThicknessCurvWireMask.x, CurvatureRadius = S.ThicknessCurvWireMask.y;
    float WireWidth = S.ThicknessCurvWireMask.z, MaskStrength = S.ThicknessCurvWireMask.w;
    float BevelRadius = S.BevelRadiusStrength.x, BevelStrength = S.BevelRadiusStrength.y;
    float3 DustUp = S.DustUpFalloff.xyz; float DustFalloff = S.DustUpFalloff.w;
    uint Samples = max(1u, S.SampleCounts.x), BevelSamples = max(1u, S.SampleCounts.y);
    uint LowIndexCount = S.SampleCounts.w;

    // 📝 Brute-force UV-space rasterization over the low mesh's triangle soup, first-match-wins — identical
    // semantics to RunMeshBake's own winning-triangle loop (it also never looks past the first covering hit).
    bool Covered = false;
    float3 SamplePosition = float3(0, 0, 0), SampleNormal = float3(0, 0, 1);
    int WinningA = -1, WinningB = -1, WinningC = -1;
    float WinningWeightA = 0, WinningWeightB = 0, WinningWeightC = 0;
    for (uint Index = 0; Index + 2 < LowIndexCount && !Covered; Index += 3)
    {
        uint Ia = LowIndices[Index + 0], Ib = LowIndices[Index + 1], Ic = LowIndices[Index + 2];
        GpuLowVertex A = LowVertices[Ia], B = LowVertices[Ib], C = LowVertices[Ic];
        float Denominator = (B.UvChart.y - C.UvChart.y) * (A.UvChart.x - C.UvChart.x) + (C.UvChart.x - B.UvChart.x) * (A.UvChart.y - C.UvChart.y);
        if (abs(Denominator) < 1e-12) continue;
        float WeightA = ((B.UvChart.y - C.UvChart.y) * (PixelU - C.UvChart.x) + (C.UvChart.x - B.UvChart.x) * (PixelV - C.UvChart.y)) / Denominator;
        float WeightB = ((C.UvChart.y - A.UvChart.y) * (PixelU - C.UvChart.x) + (A.UvChart.x - C.UvChart.x) * (PixelV - C.UvChart.y)) / Denominator;
        float WeightC = 1.0 - WeightA - WeightB;
        if (WeightA < -1e-4 || WeightB < -1e-4 || WeightC < -1e-4) continue;
        SamplePosition = WeightA * A.Position.xyz + WeightB * B.Position.xyz + WeightC * C.Position.xyz;
        SampleNormal = normalize(WeightA * A.Normal.xyz + WeightB * B.Normal.xyz + WeightC * C.Normal.xyz);
        WinningA = int(Ia); WinningB = int(Ib); WinningC = int(Ic);
        WinningWeightA = WeightA; WinningWeightB = WeightB; WinningWeightC = WeightC;
        Covered = true;
    }

    // 📝 One shared high-surface hit per texel, exactly like RunMeshBake's Hit/HasHit/NH/Q.
    bool HasHit = false;
    BvhHit Hit; Hit.Found = false; Hit.Distance = 0; Hit.PartIndex = 0; Hit.Position = float3(0, 0, 0); Hit.Normal = float3(0, 0, 0);
    float3 WorldNormal = float3(0, 0, 1), WorldPosition = float3(0, 0, 0);
    if (Covered)
    {
        WorldPosition = TransformPoint(S.ObjectToWorldRow0, S.ObjectToWorldRow1, S.ObjectToWorldRow2, SamplePosition);
        WorldNormal = normalize(SampleNormal);
        float EnvelopeLength = Back + Front;
        float3 RayOrigin = WorldPosition - WorldNormal * Back;
        Hit = BvhCastRay(RayOrigin, WorldNormal, 0.0, EnvelopeLength);
        HasHit = Hit.Found;
    }
    float3 NH = HasHit ? Hit.Normal : WorldNormal;
    float3 Q = HasHit ? Hit.Position : WorldPosition;

    // 📝 Shared hemisphere sampling: ambient occlusion, bent normal and secondary occlusion in one pass.
    float Visibility = 1.0, SecondaryVisibility = 1.0;
    float3 Bent = NH;
    bool NeedHemisphere = (NeedsMask & kNeedHemisphereSampling) != 0;
    bool NeedCurvatureProbe = (NeedsMask & kNeedCurvatureSampling) != 0;
    if (HasHit && (NeedHemisphere || NeedCurvatureProbe) && NeedHemisphere)
    {
        float3 Tangent, Bitangent;
        Hemisphere(Hit.Normal, Tangent, Bitangent);
        float3 Origin = Hit.Position + Hit.Normal * Epsilon * 3.0;
        float SampleDistance = max(AoDistance, SecondaryAoDistance);
        uint Blocked = 0, SecondaryBlocked = 0;
        float3 BentSum = float3(0, 0, 0);
        float Seed = frac(Hit.Position.x * 0.754877666 + Hit.Position.y * 0.569840296 + Hit.Position.z * 0.438579021);
        for (uint SampleIndex = 0; SampleIndex < Samples; ++SampleIndex)
        {
            float U = (float(SampleIndex) + 0.5) / float(Samples);
            float Z = sqrt(max(0.0, 1.0 - U)), R = sqrt(U);
            float Angle = float(SampleIndex) * 2.399963229728653 + Seed * 6.283185307179586;
            float3 Direction = Hit.Normal * Z + Tangent * cos(Angle) * R + Bitangent * sin(Angle) * R;
            BvhHit Obstruction = BvhCastRay(Origin, Direction, Epsilon, SampleDistance);
            bool Occluded = Obstruction.Found && Obstruction.Distance <= AoDistance;
            if (Occluded) ++Blocked; else BentSum += Direction;
            if (Obstruction.Found && Obstruction.Distance <= SecondaryAoDistance) ++SecondaryBlocked;
        }
        Visibility = 1.0 - float(Blocked) / float(Samples);
        SecondaryVisibility = 1.0 - float(SecondaryBlocked) / float(Samples);
        Bent = dot(BentSum, BentSum) < 1e-12 ? Hit.Normal : normalize(BentSum);
    }

    // 📝 Curvature: four cardinal tangent-plane probes against the nearest high-surface point.
    float SignedCurvature = 0.0;
    if (HasHit && NeedCurvatureProbe)
    {
        float3 Tangent, Bitangent;
        Hemisphere(Hit.Normal, Tangent, Bitangent);
        float Sum = 0.0; uint Count = 0;
        for (uint ProbeIndex = 0; ProbeIndex < 4; ++ProbeIndex)
        {
            float Angle = float(ProbeIndex) * 1.5707963267948966;
            float3 Direction = Tangent * cos(Angle) + Bitangent * sin(Angle);
            float3 Query = Hit.Position + Direction * CurvatureRadius;
            BvhClosest Near = BvhClosestPoint(Query, CurvatureRadius * 2.0);
            if (Near.Found)
            {
                float3 Delta = Near.Normal - Hit.Normal;
                Sum += dot(Delta, Direction);
                ++Count;
            }
        }
        SignedCurvature = Count > 0 ? Sum / float(Count) : 0.0;
    }
    float Convexity = saturate(SignedCurvature * MaskStrength);
    float Cavity = saturate(-SignedCurvature * MaskStrength);

    // 📝 Thickness: inward ray from just past the surface to the nearest backside hit.
    float ThicknessValue = 0.0;
    if (HasHit && (NeedsMask & kNeedThickness) != 0)
    {
        float3 InwardOrigin = Q - Hit.Normal * Epsilon * 3.0;
        float3 InwardDirection = -Hit.Normal;
        BvhHit Inward = BvhCastRay(InwardOrigin, InwardDirection, Epsilon, 3.402823e38);
        ThicknessValue = Inward.Found ? saturate((Inward.Distance + Epsilon * 3.0) / max(1e-6, ThicknessRange)) : 1.0;
    }

    // 📝 Bevel normal/mask: a local high-normal average within BevelRadius, blended with NH by BevelStrength.
    float3 BevelNormal = NH;
    float BevelMaskValue = 0.0;
    if (HasHit && (NeedsMask & kNeedBevel) != 0)
    {
        float3 Averaged = AverageNormal(Q, Hit.Normal, WorldNormal, BevelRadius, BevelSamples, Epsilon);
        float InverseStrength = 1.0 - saturate(BevelStrength);
        BevelNormal = normalize(Averaged * BevelStrength + Hit.Normal * InverseStrength);
        BevelMaskValue = saturate((1.0 - dot(Hit.Normal, BevelNormal)) * 8.0);
    }

    // 📝 Shared tangent-space encoder for normal/bent-normal/bevel-normal, built from WorldNormal (not Hit.Normal).
    float3 TangentBase, BitangentBase;
    TangentBasis(WorldNormal, TangentBase, BitangentBase);
    float MaxFrontBack = max(1e-6, max(Front, Back));

    // 📝 Encode every requested canonical channel exactly as RunMeshBake's per-Map loop does, including its
    // per-channel gating (HasHit-only, Covered-only or always-written with a documented fallback value).
    for (uint Channel = 0; Channel < kChannelCount; ++Channel)
    {
        uint Slot = ChannelSlots[Channel];
        if (Slot == kInvalidSlot) continue;
        float4 OutColour = float4(0, 0, 0, 0);
        if (Channel == kChannelNormal)
        {
            if (HasHit)
            {
                float Tx = dot(Hit.Normal, TangentBase), Ty = dot(Hit.Normal, BitangentBase), Tz = dot(Hit.Normal, WorldNormal);
                if (!NormalPlusY) Ty = -Ty;
                OutColour = float4(Tx * 0.5 + 0.5, Ty * 0.5 + 0.5, Tz * 0.5 + 0.5, 1.0);
            }
        }
        else if (Channel == kChannelWorldNormal)
        {
            if (HasHit)
            {
                float Signed = NormalPlusY ? Hit.Normal.y : -Hit.Normal.y;
                OutColour = float4(Hit.Normal.x * 0.5 + 0.5, Signed * 0.5 + 0.5, Hit.Normal.z * 0.5 + 0.5, 1.0);
            }
        }
        else if (Channel == kChannelObjectNormal)
        {
            float3 LocalNormal = TransformLocalNormal(S.ObjectToWorldRow0, S.ObjectToWorldRow1, S.ObjectToWorldRow2, NH);
            OutColour = float4(LocalNormal * 0.5 + 0.5, 1.0);
        }
        else if (Channel == kChannelBentNormal)
        {
            float Tx = dot(Bent, TangentBase), Ty = dot(Bent, BitangentBase), Tz = dot(Bent, WorldNormal);
            if (!NormalPlusY) Ty = -Ty;
            OutColour = float4(Tx * 0.5 + 0.5, Ty * 0.5 + 0.5, Tz * 0.5 + 0.5, 1.0);
        }
        else if (Channel == kChannelBentWorldNormal) OutColour = float4(Bent * 0.5 + 0.5, 1.0);
        else if (Channel == kChannelBentObjectNormal) OutColour = float4(TransformLocalNormal(S.ObjectToWorldRow0, S.ObjectToWorldRow1, S.ObjectToWorldRow2, Bent) * 0.5 + 0.5, 1.0);
        else if (Channel == kChannelBevelNormal)
        {
            float Tx = dot(BevelNormal, TangentBase), Ty = dot(BevelNormal, BitangentBase), Tz = dot(BevelNormal, WorldNormal);
            if (!NormalPlusY) Ty = -Ty;
            OutColour = float4(Tx * 0.5 + 0.5, Ty * 0.5 + 0.5, Tz * 0.5 + 0.5, 1.0);
        }
        else if (Channel == kChannelOcclusion) OutColour = float4(Visibility, Visibility, Visibility, 1.0);
        else if (Channel == kChannelOcclusionSecondary) OutColour = float4(SecondaryVisibility, SecondaryVisibility, SecondaryVisibility, 1.0);
        else if (Channel == kChannelCurvature) { float V = 0.5 + SignedCurvature * 0.5; OutColour = float4(V, V, V, 1.0); }
        else if (Channel == kChannelConvexity) OutColour = float4(Convexity, Convexity, Convexity, 1.0);
        else if (Channel == kChannelCavity) OutColour = float4(Cavity, Cavity, Cavity, 1.0);
        else if (Channel == kChannelThickness) OutColour = float4(ThicknessValue, ThicknessValue, ThicknessValue, 1.0);
        else if (Channel == kChannelHeight)
        {
            float3 Delta = Q - WorldPosition;
            float Signed = HasHit ? dot(Delta, WorldNormal) : 0.0;
            float V = 0.5 + Signed / (2.0 * MaxFrontBack);
            OutColour = float4(V, V, V, 1.0);
        }
        else if (Channel == kChannelVectorDisplacement)
        {
            float3 Delta = Q - WorldPosition;
            OutColour = float4(0.5 + Delta / (2.0 * MaxFrontBack), 1.0);
        }
        else if (Channel == kChannelPosition)
        {
            float3 Out3;
            Out3.x = Diagonal.x > 1e-9 ? (Q.x - S.BoundsMin.x) / Diagonal.x : 0.5;
            Out3.y = Diagonal.y > 1e-9 ? (Q.y - S.BoundsMin.y) / Diagonal.y : 0.5;
            Out3.z = Diagonal.z > 1e-9 ? (Q.z - S.BoundsMin.z) / Diagonal.z : 0.5;
            OutColour = float4(Out3, 1.0);
        }
        else if (Channel == kChannelId)
        {
            uint Id = HasHit ? Hit.PartIndex + 1u : 0u;
            OutColour = float4(float(Id & 255u), float((Id >> 8) & 255u), float((Id >> 16) & 255u), 255.0) / 255.0;
        }
        else if (Channel == kChannelUvIsland && Covered && WinningA >= 0) OutColour = float4(UvIslandColor(LowVertices[uint(WinningA)].UvChart.z > 0 ? uint(LowVertices[uint(WinningA)].UvChart.z) : 0), 1.0);
        else if (Channel == kChannelUv && Covered) OutColour = float4(PixelU, PixelV, 1.0, 1.0);
        else if (Channel == kChannelWireframe && Covered && WinningA >= 0)
        {
            GpuLowVertex A = LowVertices[uint(WinningA)], B = LowVertices[uint(WinningB)], C = LowVertices[uint(WinningC)];
            float2 Ua = A.UvChart.xy * float(Resolution), Ub = B.UvChart.xy * float(Resolution), Uc = C.UvChart.xy * float(Resolution);
            float Area = abs((Ub.x - Ua.x) * (Uc.y - Ua.y) - (Ub.y - Ua.y) * (Uc.x - Ua.x));
            float EdgeAB = max(1e-9, length(Ua - Ub)), EdgeBC = max(1e-9, length(Ub - Uc)), EdgeCA = max(1e-9, length(Uc - Ua));
            float DistanceA = abs(WinningWeightA) * Area / EdgeBC;
            float DistanceB = abs(WinningWeightB) * Area / EdgeCA;
            float DistanceC = abs(WinningWeightC) * Area / EdgeAB;
            float Distance = min(DistanceA, min(DistanceB, DistanceC));
            float V = WireWidth + 0.5 - Distance;
            OutColour = float4(V, V, V, 1.0);
        }
        else if (Channel == kChannelCoverage) OutColour = Covered ? float4(1, 1, 1, 1) : float4(0, 0, 0, 0);
        else if (Channel == kChannelAlpha) { if (HasHit) OutColour = float4(1, 1, 1, 1); }
        else if (Channel == kChannelBevelMask) OutColour = float4(BevelMaskValue, BevelMaskValue, BevelMaskValue, 1.0);
        else if (Channel == kChannelDust)
        {
            OutColour.a = 1.0;
            if (HasHit)
            {
                float Facing = max(0.0, dot(NH, DustUp));
                float Value = pow(Facing, max(1e-3, DustFalloff)) * (0.65 + 0.35 * (1.0 - Visibility));
                OutColour.rgb = float3(Value, Value, Value);
            }
        }
        else if (Channel == kChannelDirt)
        {
            OutColour.a = 1.0;
            if (HasHit) { float V = saturate((1.0 - Visibility) * MaskStrength + Cavity * 0.5); OutColour.rgb = float3(V, V, V); }
        }
        else if (Channel == kChannelEdgeWear)
        {
            OutColour.a = 1.0;
            if (HasHit) { float V = Convexity * Visibility; OutColour.rgb = float3(V, V, V); }
        }
        else if (Channel == kChannelBaseColor && Covered) OutColour = float4(1, 1, 1, 1);
        Output[Slot * Resolution * Resolution + Y * Resolution + X] = PackRgba8(OutColour);
    }
}
