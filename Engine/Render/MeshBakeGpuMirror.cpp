/*==========================================================================================================================================
                                                           MESHBAKEGPUMIRROR.CPP
==========================================================================================================================================*/
// 🧩 See MeshBakeGpuMirror.h - every function below corresponds to a same-named function in
// Engine/Render/Shader/MeshBakeCompute.slang; keep them structurally identical when either changes.
#include "MeshBakeGpuMirror.h"
#include <algorithm>
#include <cmath>

namespace Frontier
{
namespace
{
// 📝 A tiny HLSL/Slang-flavoured vector type so this file's math reads the same as the .slang source it mirrors.
struct Vec3
{
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}
    explicit Vec3(const GpuVec4 &V) : x(V.X), y(V.Y), z(V.Z) {}
    Vec3 operator+(const Vec3 &O) const { return {x + O.x, y + O.y, z + O.z}; }
    Vec3 operator-(const Vec3 &O) const { return {x - O.x, y - O.y, z - O.z}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 operator*(float S) const { return {x * S, y * S, z * S}; }
    Vec3 operator*(const Vec3 &O) const { return {x * O.x, y * O.y, z * O.z}; }
    Vec3 &operator+=(const Vec3 &O) { x += O.x; y += O.y; z += O.z; return *this; }
};
Vec3 operator*(float S, const Vec3 &V) { return V * S; }
float Dot(const Vec3 &A, const Vec3 &B) { return A.x * B.x + A.y * B.y + A.z * B.z; }
Vec3 Cross(const Vec3 &A, const Vec3 &B) { return {A.y * B.z - A.z * B.y, A.z * B.x - A.x * B.z, A.x * B.y - A.y * B.x}; }
float Length(const Vec3 &A) { return std::sqrt(Dot(A, A)); }
Vec3 Normalize(const Vec3 &A) { const float L = Length(A); return L > 1e-20f ? A * (1.f / L) : A; }
Vec3 Min3(const Vec3 &A, const Vec3 &B) { return {std::min(A.x, B.x), std::min(A.y, B.y), std::min(A.z, B.z)}; }
Vec3 Max3(const Vec3 &A, const Vec3 &B) { return {std::max(A.x, B.x), std::max(A.y, B.y), std::max(A.z, B.z)}; }
float Saturate(float V) { return std::clamp(V, 0.f, 1.f); }
Vec3 Saturate(const Vec3 &V) { return {Saturate(V.x), Saturate(V.y), Saturate(V.z)}; }
float Frac(float V) { return V - std::floor(V); }

constexpr uint32_t kBvhStackDepth = 64;

bool RayBoxTest(Vec3 Origin, Vec3 InvDirection, Vec3 Minimum, Vec3 Maximum, float MinDistance, float MaxDistance)
{
    Vec3 T0{(Minimum.x - Origin.x) * InvDirection.x, (Minimum.y - Origin.y) * InvDirection.y, (Minimum.z - Origin.z) * InvDirection.z};
    Vec3 T1{(Maximum.x - Origin.x) * InvDirection.x, (Maximum.y - Origin.y) * InvDirection.y, (Maximum.z - Origin.z) * InvDirection.z};
    Vec3 Lower = Min3(T0, T1), Upper = Max3(T0, T1);
    MinDistance = std::max(MinDistance, std::max(Lower.x, std::max(Lower.y, Lower.z)));
    MaxDistance = std::min(MaxDistance, std::min(Upper.x, std::min(Upper.y, Upper.z)));
    return MaxDistance >= MinDistance;
}
bool RayTriangleTest(Vec3 Origin, Vec3 Direction, Vec3 A, Vec3 B, Vec3 C, float MinDistance, float MaxDistance, float &OutDistance,
    Vec3 &OutBarycentric)
{
    Vec3 Edge1 = B - A, Edge2 = C - A;
    Vec3 PVector = Cross(Direction, Edge2);
    float Determinant = Dot(Edge1, PVector);
    if (std::fabs(Determinant) < 1e-12f) return false;
    float InverseDeterminant = 1.f / Determinant;
    Vec3 TVector = Origin - A;
    float U = Dot(TVector, PVector) * InverseDeterminant;
    if (U < -1e-5f || U > 1.f + 1e-5f) return false;
    Vec3 QVector = Cross(TVector, Edge1);
    float V = Dot(Direction, QVector) * InverseDeterminant;
    if (V < -1e-5f || U + V > 1.f + 1e-5f) return false;
    float Distance = Dot(Edge2, QVector) * InverseDeterminant;
    if (Distance < MinDistance || Distance > MaxDistance) return false;
    OutDistance = Distance;
    OutBarycentric = {1.f - U - V, U, V};
    return true;
}
struct BvhHit { bool Found = false; float Distance = 0; uint32_t PartIndex = 0; Vec3 Position, Normal; };
BvhHit BvhCastRay(const MeshBakeGpuPlan &Plan, Vec3 Origin, Vec3 Direction, float MinDistance, float MaxDistance)
{
    BvhHit Result;
    Vec3 InvDirection{1.f / Direction.x, 1.f / Direction.y, 1.f / Direction.z};
    float BestDistance = MaxDistance;
    int32_t Stack[kBvhStackDepth]; uint32_t StackPointer = 0; Stack[StackPointer++] = 0;
    while (StackPointer > 0)
    {
        int32_t NodeIndex = Stack[--StackPointer];
        const auto &Node = Plan.BvhNodes[size_t(NodeIndex)];
        if (!RayBoxTest(Origin, InvDirection, Vec3(Node.Minimum), Vec3(Node.Maximum), MinDistance, BestDistance)) continue;
        if (Node.LeftRightStartCount[0] < 0)
        {
            int32_t Start = Node.LeftRightStartCount[2], Count = Node.LeftRightStartCount[3];
            for (int32_t Index = 0; Index < Count; ++Index)
            {
                const auto &Tri = Plan.HighTriangles[Plan.BvhOrder[size_t(Start + Index)]];
                float Distance; Vec3 Barycentric;
                if (RayTriangleTest(Origin, Direction, Vec3(Tri.A), Vec3(Tri.B), Vec3(Tri.C), MinDistance, BestDistance, Distance,
                        Barycentric))
                {
                    BestDistance = Distance;
                    Result.Found = true; Result.Distance = Distance; Result.PartIndex = Tri.Meta[0];
                    Result.Position = Origin + Direction * Distance;
                    Result.Normal = Normalize(Barycentric.x * Vec3(Tri.NormalA) + Barycentric.y * Vec3(Tri.NormalB) + Barycentric.z * Vec3(Tri.NormalC));
                }
            }
        }
        else if (StackPointer + 2 <= kBvhStackDepth)
        {
            Stack[StackPointer++] = Node.LeftRightStartCount[0];
            Stack[StackPointer++] = Node.LeftRightStartCount[1];
        }
    }
    return Result;
}
float BoxDistanceSquared(Vec3 Point, Vec3 Minimum, Vec3 Maximum)
{
    Vec3 Delta = Max3(Minimum - Point, Max3(Point - Maximum, Vec3{0, 0, 0}));
    return Dot(Delta, Delta);
}
Vec3 ClosestPointOnTriangle(Vec3 Point, Vec3 A, Vec3 B, Vec3 C)
{
    Vec3 Ab = B - A, Ac = C - A, Ap = Point - A;
    float D1 = Dot(Ab, Ap), D2 = Dot(Ac, Ap);
    if (D1 <= 0 && D2 <= 0) return A;
    Vec3 Bp = Point - B;
    float D3 = Dot(Ab, Bp), D4 = Dot(Ac, Bp);
    if (D3 >= 0 && D4 <= D3) return B;
    float Vc = D1 * D4 - D3 * D2;
    if (Vc <= 0 && D1 >= 0 && D3 <= 0) return A + Ab * (D1 / (D1 - D3));
    Vec3 Cp = Point - C;
    float D5 = Dot(Ab, Cp), D6 = Dot(Ac, Cp);
    if (D6 >= 0 && D5 <= D6) return C;
    float Vb = D5 * D2 - D1 * D6;
    if (Vb <= 0 && D2 >= 0 && D6 <= 0) return A + Ac * (D2 / (D2 - D6));
    float Va = D3 * D6 - D5 * D4;
    if (Va <= 0 && (D4 - D3) >= 0 && (D5 - D6) >= 0) return B + (C - B) * ((D4 - D3) / ((D4 - D3) + (D5 - D6)));
    float Denominator = 1.f / (Va + Vb + Vc);
    return A + Ab * (Vb * Denominator) + Ac * (Vc * Denominator);
}
Vec3 PlaneBarycentric(Vec3 Point, Vec3 A, Vec3 B, Vec3 C)
{
    Vec3 V0 = B - A, V1 = C - A, V2 = Point - A;
    float D00 = Dot(V0, V0), D01 = Dot(V0, V1), D11 = Dot(V1, V1), D20 = Dot(V2, V0), D21 = Dot(V2, V1);
    float Denominator = D00 * D11 - D01 * D01;
    if (std::fabs(Denominator) < 1e-20f) return {1, 0, 0};
    float V = (D11 * D20 - D01 * D21) / Denominator, W = (D00 * D21 - D01 * D20) / Denominator;
    return {1.f - V - W, V, W};
}
struct BvhClosest { bool Found = false; float Distance = 0; uint32_t PartIndex = 0; Vec3 Point, Normal; };
BvhClosest BvhClosestPoint(const MeshBakeGpuPlan &Plan, Vec3 Point, float MaxDistance)
{
    BvhClosest Result;
    float BestDistanceSquared = MaxDistance * MaxDistance;
    int32_t Stack[kBvhStackDepth]; uint32_t StackPointer = 0; Stack[StackPointer++] = 0;
    while (StackPointer > 0)
    {
        int32_t NodeIndex = Stack[--StackPointer];
        const auto &Node = Plan.BvhNodes[size_t(NodeIndex)];
        if (BoxDistanceSquared(Point, Vec3(Node.Minimum), Vec3(Node.Maximum)) > BestDistanceSquared) continue;
        if (Node.LeftRightStartCount[0] < 0)
        {
            int32_t Start = Node.LeftRightStartCount[2], Count = Node.LeftRightStartCount[3];
            for (int32_t Index = 0; Index < Count; ++Index)
            {
                const auto &Tri = Plan.HighTriangles[Plan.BvhOrder[size_t(Start + Index)]];
                Vec3 Closest = ClosestPointOnTriangle(Point, Vec3(Tri.A), Vec3(Tri.B), Vec3(Tri.C));
                Vec3 Delta = Closest - Point;
                float DistanceSquared = Dot(Delta, Delta);
                if (DistanceSquared < BestDistanceSquared)
                {
                    BestDistanceSquared = DistanceSquared;
                    Result.Found = true; Result.Distance = std::sqrt(DistanceSquared); Result.PartIndex = Tri.Meta[0]; Result.Point = Closest;
                    Vec3 Weights = PlaneBarycentric(Closest, Vec3(Tri.A), Vec3(Tri.B), Vec3(Tri.C));
                    Result.Normal = Normalize(Weights.x * Vec3(Tri.NormalA) + Weights.y * Vec3(Tri.NormalB) + Weights.z * Vec3(Tri.NormalC));
                }
            }
        }
        else if (StackPointer + 2 <= kBvhStackDepth)
        {
            int32_t Left = Node.LeftRightStartCount[0], Right = Node.LeftRightStartCount[1];
            float LeftDistance = BoxDistanceSquared(Point, Vec3(Plan.BvhNodes[size_t(Left)].Minimum), Vec3(Plan.BvhNodes[size_t(Left)].Maximum));
            float RightDistance = BoxDistanceSquared(Point, Vec3(Plan.BvhNodes[size_t(Right)].Minimum), Vec3(Plan.BvhNodes[size_t(Right)].Maximum));
            if (LeftDistance <= RightDistance) { Stack[StackPointer++] = Right; Stack[StackPointer++] = Left; }
            else { Stack[StackPointer++] = Left; Stack[StackPointer++] = Right; }
        }
    }
    return Result;
}
void TangentBasis(Vec3 Normal, Vec3 &Tangent, Vec3 &Bitangent)
{
    Vec3 UpReference{0, 1, 0}, SideReference{1, 0, 0};
    Tangent = Normalize(Cross(std::fabs(Normal.y) < 0.99f ? UpReference : SideReference, Normal));
    Bitangent = Normalize(Cross(Normal, Tangent));
}
void Hemisphere(Vec3 Normal, Vec3 &Tangent, Vec3 &Bitangent)
{
    Vec3 ZReference{0, 0, 1}, YReference{0, 1, 0};
    Tangent = Normalize(Cross(std::fabs(Normal.z) < 0.9f ? ZReference : YReference, Normal));
    Bitangent = Cross(Normal, Tangent);
}
Vec3 TransformLocalNormal(const GpuVec4 &Row0, const GpuVec4 &Row1, const GpuVec4 &Row2, Vec3 World)
{
    Vec3 Out;
    Out.x = Row0.X * World.x + Row1.X * World.y + Row2.X * World.z;
    Out.y = Row0.Y * World.x + Row1.Y * World.y + Row2.Y * World.z;
    Out.z = Row0.Z * World.x + Row1.Z * World.y + Row2.Z * World.z;
    return Normalize(Out);
}
Vec3 TransformPoint(const GpuVec4 &Row0, const GpuVec4 &Row1, const GpuVec4 &Row2, Vec3 Local)
{
    return {Row0.X * Local.x + Row0.Y * Local.y + Row0.Z * Local.z + Row0.W, Row1.X * Local.x + Row1.Y * Local.y + Row1.Z * Local.z + Row1.W,
        Row2.X * Local.x + Row2.Y * Local.y + Row2.Z * Local.z + Row2.W};
}
Vec3 UvIslandColor(uint32_t Id)
{
    const uint32_t Code = (Id * 0x9e3779u) & 0xffffffu;
    return Vec3(float(Code & 255u), float((Code >> 8) & 255u), float((Code >> 16) & 255u)) * (1.f / 255.f);
}
Vec3 AverageNormal(const MeshBakeGpuPlan &Plan, Vec3 Point, Vec3 Normal, Vec3 Facing, float Radius, uint32_t Samples, float Epsilon)
{
    if (Radius <= Epsilon) return Normal;
    Vec3 Tangent, Bitangent;
    Hemisphere(Normal, Tangent, Bitangent);
    Vec3 Sum = Normal * 2.f;
    float Weight = 2.f;
    float SearchRadius = Radius * 1.8f;
    uint32_t EffectiveSamples = std::max(1u, Samples);
    for (uint32_t K = 0; K < Samples; ++K)
    {
        float Angle = (float(K) / float(EffectiveSamples)) * 6.283185307179586f;
        Vec3 Query = Point + Tangent * std::cos(Angle) * Radius + Bitangent * std::sin(Angle) * Radius - Normal * Radius * 0.3f;
        BvhClosest Near = BvhClosestPoint(Plan, Query, SearchRadius);
        if (!Near.Found || Near.Distance > SearchRadius) continue;
        if (Dot(Near.Normal, Normal) < -0.1f || Dot(Near.Normal, Facing) < -1e-6f) continue;
        float SampleWeight = 1.f - Saturate(Near.Distance / SearchRadius);
        Sum += Near.Normal * SampleWeight;
        Weight += SampleWeight;
    }
    return Normalize(Sum * (1.f / Weight));
}
uint8_t ToByte(float Value) noexcept { return uint8_t(Saturate(Value) * 255.f + 0.5f); }
} // namespace

std::vector<BakeMapImage> RunMeshBakeGpuMirror(const MeshBakeGpuPlan &Plan)
{
    std::vector<BakeMapImage> Results;
    if (Plan.RequestedChannelKeys.empty()) return Results;
    const auto &S = Plan.Settings;
    const uint32_t Resolution = S.SampleCounts[2];
    for (const auto &Key : Plan.RequestedChannelKeys)
        Results.push_back(BakeMapImage{Key, std::vector<uint8_t>(size_t(Resolution) * Resolution * 4, 0)});

    const uint32_t NeedsMask = S.Flags[0];
    const bool NormalPlusY = S.Flags[1] != 0;
    const float Epsilon = S.DiagonalEpsilon.W;
    const Vec3 Diagonal(S.DiagonalEpsilon);
    const float Front = S.FrontBackAo.X, Back = S.FrontBackAo.Y, AoDistance = S.FrontBackAo.Z, SecondaryAoDistance = S.FrontBackAo.W;
    const float ThicknessRange = S.ThicknessCurvWireMask.X, CurvatureRadius = S.ThicknessCurvWireMask.Y;
    const float WireWidth = S.ThicknessCurvWireMask.Z, MaskStrength = S.ThicknessCurvWireMask.W;
    const float BevelRadius = S.BevelRadiusStrength.X, BevelStrength = S.BevelRadiusStrength.Y;
    const Vec3 DustUp(S.DustUpFalloff); const float DustFalloff = S.DustUpFalloff.W;
    const uint32_t Samples = std::max(1u, S.SampleCounts[0]), BevelSamples = std::max(1u, S.SampleCounts[1]);
    const uint32_t LowIndexCount = S.SampleCounts[3];
    const bool NeedHemisphere = (NeedsMask & MeshBakeGpuNeedHemisphereSampling) != 0;
    const bool NeedCurvatureProbe = (NeedsMask & MeshBakeGpuNeedCurvatureSampling) != 0;
    const float MaxFrontBack = std::max(1e-6f, std::max(Front, Back));

    for (uint32_t Y = 0; Y < Resolution; ++Y)
    {
        for (uint32_t X = 0; X < Resolution; ++X)
        {
            const float PixelU = (float(X) + 0.5f) / float(Resolution);
            const float PixelV = 1.f - (float(Y) + 0.5f) / float(Resolution);

            bool Covered = false;
            Vec3 SamplePosition, SampleNormal{0, 0, 1};
            int32_t WinningA = -1, WinningB = -1, WinningC = -1;
            float WinningWeightA = 0, WinningWeightB = 0, WinningWeightC = 0;
            for (uint32_t Index = 0; Index + 2 < LowIndexCount && !Covered; Index += 3)
            {
                const uint32_t Ia = Plan.LowIndices[Index + 0], Ib = Plan.LowIndices[Index + 1], Ic = Plan.LowIndices[Index + 2];
                const auto &A = Plan.LowVertices[Ia]; const auto &B = Plan.LowVertices[Ib]; const auto &C = Plan.LowVertices[Ic];
                const float Denominator = (B.UvChart.Y - C.UvChart.Y) * (A.UvChart.X - C.UvChart.X) + (C.UvChart.X - B.UvChart.X) * (A.UvChart.Y - C.UvChart.Y);
                if (std::fabs(Denominator) < 1e-12f) continue;
                const float WeightA = ((B.UvChart.Y - C.UvChart.Y) * (PixelU - C.UvChart.X) + (C.UvChart.X - B.UvChart.X) * (PixelV - C.UvChart.Y)) / Denominator;
                const float WeightB = ((C.UvChart.Y - A.UvChart.Y) * (PixelU - C.UvChart.X) + (A.UvChart.X - C.UvChart.X) * (PixelV - C.UvChart.Y)) / Denominator;
                const float WeightC = 1.f - WeightA - WeightB;
                if (WeightA < -1e-4f || WeightB < -1e-4f || WeightC < -1e-4f) continue;
                SamplePosition = WeightA * Vec3(A.Position) + WeightB * Vec3(B.Position) + WeightC * Vec3(C.Position);
                SampleNormal = Normalize(WeightA * Vec3(A.Normal) + WeightB * Vec3(B.Normal) + WeightC * Vec3(C.Normal));
                WinningA = int32_t(Ia); WinningB = int32_t(Ib); WinningC = int32_t(Ic);
                WinningWeightA = WeightA; WinningWeightB = WeightB; WinningWeightC = WeightC;
                Covered = true;
            }

            bool HasHit = false;
            BvhHit Hit;
            Vec3 WorldNormal{0, 0, 1}, WorldPosition{0, 0, 0};
            if (Covered)
            {
                WorldPosition = TransformPoint(S.ObjectToWorldRow0, S.ObjectToWorldRow1, S.ObjectToWorldRow2, SamplePosition);
                WorldNormal = Normalize(SampleNormal);
                const float EnvelopeLength = Back + Front;
                const Vec3 RayOrigin = WorldPosition - WorldNormal * Back;
                Hit = BvhCastRay(Plan, RayOrigin, WorldNormal, 0.f, EnvelopeLength);
                HasHit = Hit.Found;
            }
            const Vec3 NH = HasHit ? Hit.Normal : WorldNormal;
            const Vec3 Q = HasHit ? Hit.Position : WorldPosition;

            float Visibility = 1.f, SecondaryVisibility = 1.f;
            Vec3 Bent = NH;
            if (HasHit && NeedHemisphere)
            {
                Vec3 Tangent, Bitangent;
                Hemisphere(Hit.Normal, Tangent, Bitangent);
                const Vec3 Origin = Hit.Position + Hit.Normal * (Epsilon * 3.f);
                const float SampleDistance = std::max(AoDistance, SecondaryAoDistance);
                uint32_t Blocked = 0, SecondaryBlocked = 0;
                Vec3 BentSum{0, 0, 0};
                const float Seed = Frac(Hit.Position.x * 0.754877666f + Hit.Position.y * 0.569840296f + Hit.Position.z * 0.438579021f);
                for (uint32_t SampleIndex = 0; SampleIndex < Samples; ++SampleIndex)
                {
                    const float U = (float(SampleIndex) + 0.5f) / float(Samples);
                    const float Z = std::sqrt(std::max(0.f, 1.f - U)), R = std::sqrt(U);
                    const float Angle = float(SampleIndex) * 2.399963229728653f + Seed * 6.283185307179586f;
                    const Vec3 Direction = Hit.Normal * Z + Tangent * std::cos(Angle) * R + Bitangent * std::sin(Angle) * R;
                    const BvhHit Obstruction = BvhCastRay(Plan, Origin, Direction, Epsilon, SampleDistance);
                    const bool Occluded = Obstruction.Found && Obstruction.Distance <= AoDistance;
                    if (Occluded) ++Blocked; else BentSum += Direction;
                    if (Obstruction.Found && Obstruction.Distance <= SecondaryAoDistance) ++SecondaryBlocked;
                }
                Visibility = 1.f - float(Blocked) / float(Samples);
                SecondaryVisibility = 1.f - float(SecondaryBlocked) / float(Samples);
                Bent = Dot(BentSum, BentSum) < 1e-12f ? Hit.Normal : Normalize(BentSum);
            }

            float SignedCurvature = 0.f;
            if (HasHit && NeedCurvatureProbe)
            {
                Vec3 Tangent, Bitangent;
                Hemisphere(Hit.Normal, Tangent, Bitangent);
                float Sum = 0.f; uint32_t Count = 0;
                for (uint32_t ProbeIndex = 0; ProbeIndex < 4; ++ProbeIndex)
                {
                    const float Angle = float(ProbeIndex) * 1.5707963267948966f;
                    const Vec3 Direction = Tangent * std::cos(Angle) + Bitangent * std::sin(Angle);
                    const Vec3 Query = Hit.Position + Direction * CurvatureRadius;
                    const BvhClosest Near = BvhClosestPoint(Plan, Query, CurvatureRadius * 2.f);
                    if (Near.Found) { Sum += Dot(Near.Normal - Hit.Normal, Direction); ++Count; }
                }
                SignedCurvature = Count > 0 ? Sum / float(Count) : 0.f;
            }
            const float Convexity = Saturate(SignedCurvature * MaskStrength);
            const float Cavity = Saturate(-SignedCurvature * MaskStrength);

            float ThicknessValue = 0.f;
            if (HasHit && (NeedsMask & MeshBakeGpuNeedThickness) != 0)
            {
                const Vec3 InwardOrigin = Q - Hit.Normal * (Epsilon * 3.f);
                const Vec3 InwardDirection = -Hit.Normal;
                const BvhHit Inward = BvhCastRay(Plan, InwardOrigin, InwardDirection, Epsilon, 3.402823e38f);
                ThicknessValue = Inward.Found ? Saturate((Inward.Distance + Epsilon * 3.f) / std::max(1e-6f, ThicknessRange)) : 1.f;
            }

            Vec3 BevelNormal = NH;
            float BevelMaskValue = 0.f;
            if (HasHit && (NeedsMask & MeshBakeGpuNeedBevel) != 0)
            {
                const Vec3 Averaged = AverageNormal(Plan, Q, Hit.Normal, WorldNormal, BevelRadius, BevelSamples, Epsilon);
                const float InverseStrength = 1.f - Saturate(BevelStrength);
                BevelNormal = Normalize(Averaged * BevelStrength + Hit.Normal * InverseStrength);
                BevelMaskValue = Saturate((1.f - Dot(Hit.Normal, BevelNormal)) * 8.f);
            }

            Vec3 TangentBase, BitangentBase;
            TangentBasis(WorldNormal, TangentBase, BitangentBase);

            for (uint32_t Channel = 0; Channel < MeshBakeGpuChannelCount; ++Channel)
            {
                const uint32_t Slot = Plan.ChannelSlots[Channel];
                if (Slot == MeshBakeGpuInvalidSlot) continue;
                float OutR = 0, OutG = 0, OutB = 0, OutA = 0;
                const auto Encode = [&](Vec3 Direction)
                {
                    float Tx = Dot(Direction, TangentBase), Ty = Dot(Direction, BitangentBase), Tz = Dot(Direction, WorldNormal);
                    if (!NormalPlusY) Ty = -Ty;
                    OutR = Tx * 0.5f + 0.5f; OutG = Ty * 0.5f + 0.5f; OutB = Tz * 0.5f + 0.5f; OutA = 1.f;
                };
                const auto Channel_ = MeshBakeGpuChannelIndex(Channel);
                if (Channel_ == MeshBakeGpuChannelIndex::Normal) { if (HasHit) Encode(Hit.Normal); }
                else if (Channel_ == MeshBakeGpuChannelIndex::WorldNormal)
                {
                    if (HasHit)
                    {
                        const float Signed = NormalPlusY ? Hit.Normal.y : -Hit.Normal.y;
                        OutR = Hit.Normal.x * 0.5f + 0.5f; OutG = Signed * 0.5f + 0.5f; OutB = Hit.Normal.z * 0.5f + 0.5f; OutA = 1.f;
                    }
                }
                else if (Channel_ == MeshBakeGpuChannelIndex::ObjectNormal)
                {
                    const Vec3 LocalNormal = TransformLocalNormal(S.ObjectToWorldRow0, S.ObjectToWorldRow1, S.ObjectToWorldRow2, NH);
                    OutR = LocalNormal.x * 0.5f + 0.5f; OutG = LocalNormal.y * 0.5f + 0.5f; OutB = LocalNormal.z * 0.5f + 0.5f; OutA = 1.f;
                }
                else if (Channel_ == MeshBakeGpuChannelIndex::BentNormal) Encode(Bent);
                else if (Channel_ == MeshBakeGpuChannelIndex::BentWorldNormal)
                    { OutR = Bent.x * 0.5f + 0.5f; OutG = Bent.y * 0.5f + 0.5f; OutB = Bent.z * 0.5f + 0.5f; OutA = 1.f; }
                else if (Channel_ == MeshBakeGpuChannelIndex::BentObjectNormal)
                {
                    const Vec3 LocalBent = TransformLocalNormal(S.ObjectToWorldRow0, S.ObjectToWorldRow1, S.ObjectToWorldRow2, Bent);
                    OutR = LocalBent.x * 0.5f + 0.5f; OutG = LocalBent.y * 0.5f + 0.5f; OutB = LocalBent.z * 0.5f + 0.5f; OutA = 1.f;
                }
                else if (Channel_ == MeshBakeGpuChannelIndex::BevelNormal) Encode(BevelNormal);
                else if (Channel_ == MeshBakeGpuChannelIndex::Occlusion) { OutR = OutG = OutB = Visibility; OutA = 1.f; }
                else if (Channel_ == MeshBakeGpuChannelIndex::OcclusionSecondary) { OutR = OutG = OutB = SecondaryVisibility; OutA = 1.f; }
                else if (Channel_ == MeshBakeGpuChannelIndex::Curvature) { OutR = OutG = OutB = 0.5f + SignedCurvature * 0.5f; OutA = 1.f; }
                else if (Channel_ == MeshBakeGpuChannelIndex::Convexity) { OutR = OutG = OutB = Convexity; OutA = 1.f; }
                else if (Channel_ == MeshBakeGpuChannelIndex::Cavity) { OutR = OutG = OutB = Cavity; OutA = 1.f; }
                else if (Channel_ == MeshBakeGpuChannelIndex::Thickness) { OutR = OutG = OutB = ThicknessValue; OutA = 1.f; }
                else if (Channel_ == MeshBakeGpuChannelIndex::Height)
                {
                    const Vec3 Delta = Q - WorldPosition;
                    const float Signed = HasHit ? Dot(Delta, WorldNormal) : 0.f;
                    OutR = OutG = OutB = 0.5f + Signed / (2.f * MaxFrontBack); OutA = 1.f;
                }
                else if (Channel_ == MeshBakeGpuChannelIndex::VectorDisplacement)
                {
                    const Vec3 Delta = Q - WorldPosition;
                    OutR = 0.5f + Delta.x / (2.f * MaxFrontBack); OutG = 0.5f + Delta.y / (2.f * MaxFrontBack);
                    OutB = 0.5f + Delta.z / (2.f * MaxFrontBack); OutA = 1.f;
                }
                else if (Channel_ == MeshBakeGpuChannelIndex::Position)
                {
                    OutR = Diagonal.x > 1e-9f ? (Q.x - S.BoundsMin.X) / Diagonal.x : 0.5f;
                    OutG = Diagonal.y > 1e-9f ? (Q.y - S.BoundsMin.Y) / Diagonal.y : 0.5f;
                    OutB = Diagonal.z > 1e-9f ? (Q.z - S.BoundsMin.Z) / Diagonal.z : 0.5f; OutA = 1.f;
                }
                else if (Channel_ == MeshBakeGpuChannelIndex::Id)
                {
                    const uint32_t Id = HasHit ? Hit.PartIndex + 1u : 0u;
                    OutR = float(Id & 255u) / 255.f; OutG = float((Id >> 8) & 255u) / 255.f; OutB = float((Id >> 16) & 255u) / 255.f; OutA = 1.f;
                }
                else if (Channel_ == MeshBakeGpuChannelIndex::UvIsland && Covered && WinningA >= 0)
                {
                    const Vec3 Colour = UvIslandColor(uint32_t(std::max(0.f, Plan.LowVertices[size_t(WinningA)].UvChart.Z)));
                    OutR = Colour.x; OutG = Colour.y; OutB = Colour.z; OutA = 1.f;
                }
                else if (Channel_ == MeshBakeGpuChannelIndex::Uv && Covered) { OutR = PixelU; OutG = PixelV; OutB = 1.f; OutA = 1.f; }
                else if (Channel_ == MeshBakeGpuChannelIndex::Wireframe && Covered && WinningA >= 0)
                {
                    const auto &A = Plan.LowVertices[size_t(WinningA)]; const auto &B = Plan.LowVertices[size_t(WinningB)];
                    const auto &C = Plan.LowVertices[size_t(WinningC)];
                    const float Ua[2]{A.UvChart.X * float(Resolution), A.UvChart.Y * float(Resolution)};
                    const float Ub[2]{B.UvChart.X * float(Resolution), B.UvChart.Y * float(Resolution)};
                    const float Uc[2]{C.UvChart.X * float(Resolution), C.UvChart.Y * float(Resolution)};
                    const float Area = std::fabs((Ub[0] - Ua[0]) * (Uc[1] - Ua[1]) - (Ub[1] - Ua[1]) * (Uc[0] - Ua[0]));
                    const auto EdgeLength = [](const float P0[2], const float P1[2])
                    { return std::max(1e-9f, std::sqrt((P0[0] - P1[0]) * (P0[0] - P1[0]) + (P0[1] - P1[1]) * (P0[1] - P1[1]))); };
                    const float DistanceA = std::fabs(WinningWeightA) * Area / EdgeLength(Ub, Uc);
                    const float DistanceB = std::fabs(WinningWeightB) * Area / EdgeLength(Uc, Ua);
                    const float DistanceC = std::fabs(WinningWeightC) * Area / EdgeLength(Ua, Ub);
                    const float Distance = std::min({DistanceA, DistanceB, DistanceC});
                    OutR = OutG = OutB = WireWidth + 0.5f - Distance; OutA = 1.f;
                }
                else if (Channel_ == MeshBakeGpuChannelIndex::Coverage) { OutR = OutG = OutB = OutA = Covered ? 1.f : 0.f; }
                else if (Channel_ == MeshBakeGpuChannelIndex::Alpha) { if (HasHit) OutR = OutG = OutB = OutA = 1.f; }
                else if (Channel_ == MeshBakeGpuChannelIndex::BevelMask) { OutR = OutG = OutB = BevelMaskValue; OutA = 1.f; }
                else if (Channel_ == MeshBakeGpuChannelIndex::Dust)
                {
                    OutA = 1.f;
                    if (HasHit)
                    {
                        const float Facing = std::max(0.f, Dot(NH, DustUp));
                        const float Value = std::pow(Facing, std::max(1e-3f, DustFalloff)) * (0.65f + 0.35f * (1.f - Visibility));
                        OutR = OutG = OutB = Value;
                    }
                }
                else if (Channel_ == MeshBakeGpuChannelIndex::Dirt)
                {
                    OutA = 1.f;
                    if (HasHit) OutR = OutG = OutB = Saturate((1.f - Visibility) * MaskStrength + Cavity * 0.5f);
                }
                else if (Channel_ == MeshBakeGpuChannelIndex::EdgeWear)
                {
                    OutA = 1.f;
                    if (HasHit) OutR = OutG = OutB = Convexity * Visibility;
                }
                else if (Channel_ == MeshBakeGpuChannelIndex::BaseColor && Covered) OutR = OutG = OutB = OutA = 1.f;

                const size_t Pixel = (size_t(Y) * Resolution + X) * 4;
                Results[Slot].Rgba[Pixel + 0] = ToByte(OutR);
                Results[Slot].Rgba[Pixel + 1] = ToByte(OutG);
                Results[Slot].Rgba[Pixel + 2] = ToByte(OutB);
                Results[Slot].Rgba[Pixel + 3] = ToByte(OutA);
            }
        }
    }
    return Results;
}
} // namespace Frontier
