/*==========================================================================================================================================
                                                              BAKEMESH.CPP
==========================================================================================================================================*/
#include "Engine/ContentInterchange/BakeMesh.h"
#include "Engine/ContentInterchange/TeapotArchiveData.inl"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Frontier
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;

void Cross(const float A[3], const float B[3], float Out[3]) noexcept
{
    Out[0] = A[1] * B[2] - A[2] * B[1];
    Out[1] = A[2] * B[0] - A[0] * B[2];
    Out[2] = A[0] * B[1] - A[1] * B[0];
}
float Dot(const float A[3], const float B[3]) noexcept { return A[0] * B[0] + A[1] * B[1] + A[2] * B[2]; }
void Normalize(float V[3]) noexcept
{
    const float Length = std::sqrt(Dot(V, V));
    if (Length > 1e-20f) { V[0] /= Length; V[1] /= Length; V[2] /= Length; }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     CUBE / SPHERE DEMO MESHES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Port of bakeMeshes.js packedBox(): a 6-face box, each face its own UV island tiled 3 columns x 2 rows
// with a 0.06-unit gutter, matching the web reference's layout and chart ids exactly.
BakePart BuildPackedBox(const char *Name)
{
    BakePart Part;
    Part.Name = Name;
    struct Face { float Normal[3]; float U[3]; float V[3]; };
    const Face Faces[6] = {
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},   // +X
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},   // -X
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},   // +Y
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},   // -Y
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},    // +Z
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},  // -Z
    };
    for (uint32_t Face = 0; Face < 6; ++Face)
    {
        const auto &F = Faces[Face];
        const uint32_t Base = uint32_t(Part.Vertices.size());
        const uint32_t Column = Face % 3, Row = Face / 3;
        for (uint32_t Corner = 0; Corner < 4; ++Corner)
        {
            const float Su = (Corner == 1 || Corner == 2) ? 1.f : -1.f;
            const float Sv = (Corner == 2 || Corner == 3) ? 1.f : -1.f;
            BakeVertex Vertex;
            for (int Axis = 0; Axis < 3; ++Axis)
                Vertex.Position[Axis] = F.Normal[Axis] + Su * F.U[Axis] + Sv * F.V[Axis];
            Vertex.Normal[0] = F.Normal[0]; Vertex.Normal[1] = F.Normal[1]; Vertex.Normal[2] = F.Normal[2];
            const float Uu = (Su + 1.f) * 0.5f, Vv = (Sv + 1.f) * 0.5f;
            Vertex.Uv[0] = (Column + 0.06f + Uu * 0.88f) / 3.f;
            Vertex.Uv[1] = (Row + 0.06f + Vv * 0.88f) / 2.f;
            Vertex.ChartId = Face + 1;
            Part.Vertices.push_back(Vertex);
        }
        Part.Indices.insert(Part.Indices.end(), {Base, Base + 1, Base + 2, Base, Base + 2, Base + 3});
    }
    return Part;
}

// 📝 Port of three.js SphereGeometry(radius, widthSegments, heightSegments) position/normal/uv generation,
// with the low target's pole-clamped UV fix from bakeMeshes.js createDemoBakeAssets.
BakePart BuildSphere(const char *Name, float Radius, uint32_t WidthSegments, uint32_t HeightSegments, bool ClampUv)
{
    BakePart Part;
    Part.Name = Name;
    std::vector<std::vector<uint32_t>> Grid(HeightSegments + 1, std::vector<uint32_t>(WidthSegments + 1));
    for (uint32_t Y = 0; Y <= HeightSegments; ++Y)
    {
        const float V = float(Y) / float(HeightSegments);
        float UOffset = 0.f;
        if (Y == 0) UOffset = 0.5f / float(WidthSegments);
        else if (Y == HeightSegments) UOffset = -0.5f / float(WidthSegments);
        for (uint32_t X = 0; X <= WidthSegments; ++X)
        {
            const float U = float(X) / float(WidthSegments);
            BakeVertex Vertex;
            Vertex.Position[0] = -Radius * std::cos(U * 2.f * kPi) * std::sin(V * kPi);
            Vertex.Position[1] = Radius * std::cos(V * kPi);
            Vertex.Position[2] = Radius * std::sin(U * 2.f * kPi) * std::sin(V * kPi);
            float N[3]{Vertex.Position[0], Vertex.Position[1], Vertex.Position[2]};
            Normalize(N);
            Vertex.Normal[0] = N[0]; Vertex.Normal[1] = N[1]; Vertex.Normal[2] = N[2];
            float Uv0 = U + UOffset;
            if (ClampUv) Uv0 = std::max(0.f, std::min(1.f, Uv0));
            Vertex.Uv[0] = Uv0; Vertex.Uv[1] = 1.f - V;
            Vertex.ChartId = 1;
            Grid[Y][X] = uint32_t(Part.Vertices.size());
            Part.Vertices.push_back(Vertex);
        }
    }
    for (uint32_t Y = 0; Y < HeightSegments; ++Y)
        for (uint32_t X = 0; X < WidthSegments; ++X)
        {
            const uint32_t A = Grid[Y][X + 1], B = Grid[Y][X], C = Grid[Y + 1][X], D = Grid[Y + 1][X + 1];
            if (Y != 0) Part.Indices.insert(Part.Indices.end(), {A, B, D});
            if (Y != HeightSegments - 1) Part.Indices.insert(Part.Indices.end(), {B, C, D});
        }
    return Part;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     TEAPOT DEMO MESH
//------------------------------------------------------------------------------------------------------------------------

// 📝 Faithful re-derivation of three.js TeapotGeometry's bicubic Bezier evaluation (see
// Engine/ContentInterchange/TeapotArchiveData.inl for provenance of the patch data). The matrix-form "M*G*M"
// evaluation that file performs is mathematically identical to summing the standard cubic Bernstein basis over
// each patch's 4x4 control grid, which is what this function does directly.
BakePart BuildTeapot(const char *Name, float Size, uint32_t Segments)
{
    Segments = std::max(2u, Segments);
    constexpr float BlinnScale = 1.3f;
    const float MaxHeight = 3.15f;
    const float MaxHeight2 = MaxHeight / 2.f;
    const float TrueSize = Size / MaxHeight2;
    const uint32_t VertPerRow = Segments + 1;

    const auto Bernstein = [](float U, float Out[4]) noexcept
    {
        const float Inverse = 1.f - U;
        Out[0] = Inverse * Inverse * Inverse;
        Out[1] = 3.f * U * Inverse * Inverse;
        Out[2] = 3.f * U * U * Inverse;
        Out[3] = U * U * U;
    };
    const auto BernsteinDerivative = [](float U, float Out[4]) noexcept
    {
        const float Inverse = 1.f - U;
        Out[0] = -3.f * Inverse * Inverse;
        Out[1] = 3.f * Inverse * (Inverse - 2.f * U);
        Out[2] = 3.f * U * (2.f * Inverse - U);
        Out[3] = 3.f * U * U;
    };
    const auto ControlPoint = [](uint32_t PatchIndex, uint32_t Row, uint32_t Column, float Out[3]) noexcept
    {
        const uint32_t Vertex = TeapotPatchIndices[PatchIndex * 16 + Row * 4 + Column];
        Out[0] = TeapotControlPoints[Vertex * 3 + 0];
        Out[1] = TeapotControlPoints[Vertex * 3 + 1];
        Out[2] = TeapotControlPoints[Vertex * 3 + 2];
    };

    BakePart Part;
    Part.Name = Name;
    for (uint32_t Patch = 0; Patch < 32; ++Patch)
    {
        const uint32_t Base = uint32_t(Part.Vertices.size());
        for (uint32_t Ss = 0; Ss <= Segments; ++Ss)
        {
            const float S = float(Ss) / float(Segments);
            float Bs[4], Dbs[4];
            Bernstein(S, Bs);
            BernsteinDerivative(S, Dbs);
            for (uint32_t Ts = 0; Ts <= Segments; ++Ts)
            {
                const float T = float(Ts) / float(Segments);
                float Bt[4], Dbt[4];
                Bernstein(T, Bt);
                BernsteinDerivative(T, Dbt);
                float Position[3]{0, 0, 0}, SDir[3]{0, 0, 0}, TDir[3]{0, 0, 0};
                for (uint32_t Row = 0; Row < 4; ++Row)
                    for (uint32_t Column = 0; Column < 4; ++Column)
                    {
                        float Control[3];
                        ControlPoint(Patch, Row, Column, Control);
                        const float Weight = Bs[Row] * Bt[Column];
                        const float DWeightS = Dbs[Row] * Bt[Column];
                        const float DWeightT = Bs[Row] * Dbt[Column];
                        for (int Axis = 0; Axis < 3; ++Axis)
                        {
                            Position[Axis] += Weight * Control[Axis];
                            SDir[Axis] += DWeightS * Control[Axis];
                            TDir[Axis] += DWeightT * Control[Axis];
                        }
                    }
                float Normal[3];
                Cross(TDir, SDir, Normal);
                Normalize(Normal);
                float NormalOut[3];
                if (Position[0] == 0.f && Position[1] == 0.f)
                    { NormalOut[0] = 0.f; NormalOut[1] = Position[2] > MaxHeight2 ? 1.f : -1.f; NormalOut[2] = 0.f; }
                else
                    { NormalOut[0] = Normal[0]; NormalOut[1] = Normal[2]; NormalOut[2] = -Normal[1]; }

                BakeVertex Vertex;
                Vertex.Position[0] = TrueSize * Position[0];
                Vertex.Position[1] = TrueSize * (Position[2] - MaxHeight2);
                Vertex.Position[2] = -TrueSize * Position[1];
                Vertex.Normal[0] = NormalOut[0]; Vertex.Normal[1] = NormalOut[1]; Vertex.Normal[2] = NormalOut[2];
                // 📝 packTeapotUVs (bakeMeshes.js): tile each of the 32 patches into an 8x4 grid, inset by a
                // small gutter, and record its 1-based chart id — this is the low mesh's non-overlapping layout.
                constexpr float Gutter = 0.045f;
                const uint32_t Column = Patch % 8, Row = Patch / 8;
                Vertex.Uv[0] = (Column + Gutter + (1.f - T) * (1.f - 2.f * Gutter)) / 8.f;
                Vertex.Uv[1] = (Row + Gutter + (1.f - S) * (1.f - 2.f * Gutter)) / 4.f;
                Vertex.ChartId = Patch + 1;
                Part.Vertices.push_back(Vertex);
            }
        }
        for (uint32_t Ss = 0; Ss < Segments; ++Ss)
            for (uint32_t Ts = 0; Ts < Segments; ++Ts)
            {
                const uint32_t V1 = Base + Ss * VertPerRow + Ts, V2 = V1 + 1, V3 = V2 + VertPerRow, V4 = V1 + VertPerRow;
                const auto SamePosition = [&](uint32_t A, uint32_t B)
                {
                    return Part.Vertices[A].Position[0] == Part.Vertices[B].Position[0] &&
                        Part.Vertices[A].Position[1] == Part.Vertices[B].Position[1] &&
                        Part.Vertices[A].Position[2] == Part.Vertices[B].Position[2];
                };
                const auto Degenerate = [&](uint32_t X, uint32_t Y, uint32_t Z)
                { return SamePosition(X, Y) || SamePosition(X, Z) || SamePosition(Y, Z); };
                if (!Degenerate(V1, V2, V3)) Part.Indices.insert(Part.Indices.end(), {V1, V2, V3});
                if (!Degenerate(V1, V3, V4)) Part.Indices.insert(Part.Indices.end(), {V1, V3, V4});
            }
    }
    return Part;
}
} // namespace

DemoBakeAssets BuildDemoBakeAssets(const std::string &Shape)
{
    DemoBakeAssets Assets;
    if (Shape == "Cube")
    {
        Assets.Low.Parts.push_back(BuildPackedBox("Cube"));
        Assets.High.Parts.push_back(BuildPackedBox("Cube"));
    }
    else if (Shape == "Sphere")
    {
        Assets.Low.Parts.push_back(BuildSphere("Sphere", 1.05f, 16, 12, true));
        Assets.High.Parts.push_back(BuildSphere("Sphere", 1.05f, 64, 48, false));
    }
    else
    {
        Assets.Low.Parts.push_back(BuildTeapot("Teapot", 1.05f, 8));
        Assets.High.Parts.push_back(BuildTeapot("Teapot", 1.05f, 24));
    }
    Assets.Low.Label = Shape + " study";
    Assets.High.Label = Shape + " study";
    Assets.Low.Source = Assets.High.Source = "Built-in geometry";
    return Assets;
}

MeshSummary SummarizeMesh(const BakeMeshSet &Set)
{
    MeshSummary Summary;
    Summary.Objects = uint32_t(Set.Parts.size());
    for (const auto &Part : Set.Parts)
    {
        Summary.Triangles += Part.TriangleCount();
        Summary.Vertices += Part.Vertices.size();
    }
    return Summary;
}

std::string MeshSummaryText(const BakeMeshSet &Set)
{
    const auto Summary = SummarizeMesh(Set);
    return std::to_string(Summary.Objects) + " objects, " + std::to_string(Summary.Triangles) + " triangles";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        TRIANGLE BVH
//------------------------------------------------------------------------------------------------------------------------

void BakeTriangleBvh::Build(const BakeMeshSet &High)
{
    Triangles.clear();
    Nodes.clear();
    Order.clear();
    for (uint32_t PartIndex = 0; PartIndex < High.Parts.size(); ++PartIndex)
    {
        const auto &Part = High.Parts[PartIndex];
        for (size_t Index = 0; Index + 2 < Part.Indices.size(); Index += 3)
        {
            const auto &VA = Part.Vertices[Part.Indices[Index + 0]];
            const auto &VB = Part.Vertices[Part.Indices[Index + 1]];
            const auto &VC = Part.Vertices[Part.Indices[Index + 2]];
            Triangle Triangle_;
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                Triangle_.A[Axis] = VA.Position[Axis];
                Triangle_.B[Axis] = VB.Position[Axis];
                Triangle_.C[Axis] = VC.Position[Axis];
                Triangle_.NormalA[Axis] = VA.Normal[Axis];
                Triangle_.NormalB[Axis] = VB.Normal[Axis];
                Triangle_.NormalC[Axis] = VC.Normal[Axis];
            }
            Triangle_.PartIndex = PartIndex;
            Triangles.push_back(Triangle_);
        }
    }
    Order.resize(Triangles.size());
    for (uint32_t Index = 0; Index < Order.size(); ++Index) Order[Index] = Index;
    if (!Triangles.empty()) BuildRange(0, uint32_t(Order.size()));
}

int32_t BakeTriangleBvh::BuildRange(uint32_t Start, uint32_t Count)
{
    Node Node_;
    Node_.Minimum[0] = Node_.Minimum[1] = Node_.Minimum[2] = 3.402823e38f;
    Node_.Maximum[0] = Node_.Maximum[1] = Node_.Maximum[2] = -3.402823e38f;
    for (uint32_t Index = 0; Index < Count; ++Index)
    {
        const auto &Triangle_ = Triangles[Order[Start + Index]];
        for (const float *Vertex : {Triangle_.A, Triangle_.B, Triangle_.C})
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                Node_.Minimum[Axis] = std::min(Node_.Minimum[Axis], Vertex[Axis]);
                Node_.Maximum[Axis] = std::max(Node_.Maximum[Axis], Vertex[Axis]);
            }
    }
    const int32_t NodeIndex = int32_t(Nodes.size());
    Nodes.push_back(Node_);
    constexpr uint32_t LeafTriangles = 4;
    if (Count <= LeafTriangles)
    {
        Nodes[NodeIndex].Start = Start;
        Nodes[NodeIndex].Count = Count;
        return NodeIndex;
    }
    const float Extent[3]{Nodes[NodeIndex].Maximum[0] - Nodes[NodeIndex].Minimum[0],
        Nodes[NodeIndex].Maximum[1] - Nodes[NodeIndex].Minimum[1], Nodes[NodeIndex].Maximum[2] - Nodes[NodeIndex].Minimum[2]};
    const int Axis = Extent[0] > Extent[1] ? (Extent[0] > Extent[2] ? 0 : 2) : (Extent[1] > Extent[2] ? 1 : 2);
    std::sort(Order.begin() + Start, Order.begin() + Start + Count, [&](uint32_t A, uint32_t B)
    {
        const auto Centroid = [&](uint32_t TriangleIndex)
        {
            const auto &Triangle_ = Triangles[TriangleIndex];
            return (Triangle_.A[Axis] + Triangle_.B[Axis] + Triangle_.C[Axis]) / 3.f;
        };
        return Centroid(A) < Centroid(B);
    });
    const uint32_t Mid = Count / 2;
    const int32_t Left = BuildRange(Start, Mid);
    const int32_t Right = BuildRange(Start + Mid, Count - Mid);
    Nodes[NodeIndex].Left = Left;
    Nodes[NodeIndex].Right = Right;
    return NodeIndex;
}

namespace
{
bool RayBox(const float Origin[3], const float InvDirection[3], const float Minimum[3], const float Maximum[3], float MinDistance,
    float MaxDistance) noexcept
{
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        float T0 = (Minimum[Axis] - Origin[Axis]) * InvDirection[Axis];
        float T1 = (Maximum[Axis] - Origin[Axis]) * InvDirection[Axis];
        if (T0 > T1) std::swap(T0, T1);
        MinDistance = std::max(MinDistance, T0);
        MaxDistance = std::min(MaxDistance, T1);
        if (MaxDistance < MinDistance) return false;
    }
    return true;
}
// 📝 Möller–Trumbore ray-triangle intersection, returns the barycentric-interpolated normal on hit.
bool RayTriangle(const float Origin[3], const float Direction[3], const float A[3], const float B[3], const float C[3],
    float MinDistance, float MaxDistance, float &OutDistance, float OutBarycentric[3]) noexcept
{
    const float Edge1[3]{B[0] - A[0], B[1] - A[1], B[2] - A[2]};
    const float Edge2[3]{C[0] - A[0], C[1] - A[1], C[2] - A[2]};
    float PVector[3];
    Cross(Direction, Edge2, PVector);
    const float Determinant = Dot(Edge1, PVector);
    if (std::fabs(Determinant) < 1e-12f) return false;
    const float InverseDeterminant = 1.f / Determinant;
    const float TVector[3]{Origin[0] - A[0], Origin[1] - A[1], Origin[2] - A[2]};
    const float U = Dot(TVector, PVector) * InverseDeterminant;
    if (U < -1e-5f || U > 1.f + 1e-5f) return false;
    float QVector[3];
    Cross(TVector, Edge1, QVector);
    const float V = Dot(Direction, QVector) * InverseDeterminant;
    if (V < -1e-5f || U + V > 1.f + 1e-5f) return false;
    const float Distance = Dot(Edge2, QVector) * InverseDeterminant;
    if (Distance < MinDistance || Distance > MaxDistance) return false;
    OutDistance = Distance;
    OutBarycentric[0] = 1.f - U - V;
    OutBarycentric[1] = U;
    OutBarycentric[2] = V;
    return true;
}
} // namespace

BakeTriangleBvh::Hit BakeTriangleBvh::CastRay(const float Origin[3], const float Direction[3], float MinDistance,
    float MaxDistance) const
{
    Hit Result;
    if (Nodes.empty()) return Result;
    const float InvDirection[3]{1.f / Direction[0], 1.f / Direction[1], 1.f / Direction[2]};
    float BestDistance = MaxDistance;
    std::vector<int32_t> Stack{0};
    while (!Stack.empty())
    {
        const int32_t NodeIndex = Stack.back();
        Stack.pop_back();
        const auto &Node_ = Nodes[NodeIndex];
        if (!RayBox(Origin, InvDirection, Node_.Minimum, Node_.Maximum, MinDistance, BestDistance)) continue;
        if (Node_.Left < 0)
        {
            for (uint32_t Index = 0; Index < Node_.Count; ++Index)
            {
                const auto &Triangle_ = Triangles[Order[Node_.Start + Index]];
                float Distance, Barycentric[3];
                if (RayTriangle(Origin, Direction, Triangle_.A, Triangle_.B, Triangle_.C, MinDistance, BestDistance, Distance,
                        Barycentric))
                {
                    BestDistance = Distance;
                    Result.Found = true;
                    Result.Distance = Distance;
                    Result.PartIndex = Triangle_.PartIndex;
                    for (int Axis = 0; Axis < 3; ++Axis)
                    {
                        Result.Position[Axis] = Origin[Axis] + Direction[Axis] * Distance;
                        Result.Normal[Axis] = Barycentric[0] * Triangle_.NormalA[Axis] + Barycentric[1] * Triangle_.NormalB[Axis] +
                            Barycentric[2] * Triangle_.NormalC[Axis];
                    }
                    Normalize(Result.Normal);
                }
            }
        }
        else
        {
            Stack.push_back(Node_.Left);
            Stack.push_back(Node_.Right);
        }
    }
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        MESH MAP EVALUATION
//------------------------------------------------------------------------------------------------------------------------

const std::vector<std::string> &ImplementedMeshMapKeys()
{
    static const std::vector<std::string> Keys{"normal", "world-normal", "coverage", "alpha"};
    return Keys;
}
bool IsMeshMapImplemented(const std::string &Key)
{
    const auto &Keys = ImplementedMeshMapKeys();
    return std::find(Keys.begin(), Keys.end(), Key) != Keys.end();
}

namespace
{
void Transform(const std::array<float, 16> &Matrix, const float In[3], float Out[3]) noexcept
{
    for (int Row = 0; Row < 3; ++Row)
        Out[Row] = Matrix[Row * 4 + 0] * In[0] + Matrix[Row * 4 + 1] * In[1] + Matrix[Row * 4 + 2] * In[2] + Matrix[Row * 4 + 3];
}
uint8_t ToByte(float Value) noexcept { return uint8_t(std::max(0.f, std::min(1.f, Value)) * 255.f + 0.5f); }

// 📝 Barycentric interpolation across a low-mesh triangle for position/normal/uv, used both to rasterize UV
// space and to build the per-texel tangent frame for the tangent-normal map.
struct Interpolated
{
    float Position[3];
    float Normal[3];
};
// 📝 WeightA/WeightB/WeightC are the barycentric weights of A/B/C directly (summing to ~1), not the
// "W=1-u-v" convention — callers must supply all three explicitly to avoid swapping vertices.
Interpolated BarycentricLerp(const BakeVertex &A, const BakeVertex &B, const BakeVertex &C, float WeightA, float WeightB,
    float WeightC) noexcept
{
    Interpolated Out{};
    for (int Axis = 0; Axis < 3; ++Axis)
        Out.Position[Axis] = WeightA * A.Position[Axis] + WeightB * B.Position[Axis] + WeightC * C.Position[Axis];
    for (int Axis = 0; Axis < 3; ++Axis)
        Out.Normal[Axis] = WeightA * A.Normal[Axis] + WeightB * B.Normal[Axis] + WeightC * C.Normal[Axis];
    Normalize(Out.Normal);
    return Out;
}
void TangentBasis(const float Normal[3], float Tangent[3], float Bitangent[3]) noexcept
{
    const float UpReference[3]{0, 1, 0}, SideReference[3]{1, 0, 0};
    const bool UseUp = std::fabs(Normal[1]) < 0.99f;
    Cross(UseUp ? UpReference : SideReference, Normal, Tangent);
    Normalize(Tangent);
    Cross(Normal, Tangent, Bitangent);
    Normalize(Bitangent);
}
} // namespace

std::vector<BakePartResult> RunMeshBake(const std::vector<const BakePart *> &Targets, const BakeMeshSet &High,
    const BakeSettings &Settings, const std::function<void(const BakeProgress &)> &OnProgress)
{
    std::vector<std::string> Channels;
    for (const auto &Key : Settings.Channels)
        if (IsMeshMapImplemented(Key)) Channels.push_back(Key);
    std::vector<BakePartResult> Results;
    if (Targets.empty() || Channels.empty()) return Results;

    BakeTriangleBvh Bvh;
    Bvh.Build(High);
    const uint32_t Resolution = std::max(8u, std::min(1024u, Settings.Resolution));

    for (uint32_t TargetIndex = 0; TargetIndex < Targets.size(); ++TargetIndex)
    {
        const BakePart &Part = *Targets[TargetIndex];
        BakePartResult Result;
        Result.Name = Part.Name;
        Result.Resolution = Resolution;
        for (const auto &Key : Channels) Result.Maps.push_back(BakeMapImage{Key, std::vector<uint8_t>(size_t(Resolution) * Resolution * 4, 0)});

        uint32_t ChartMaximum = 0;
        for (const auto &Vertex : Part.Vertices) ChartMaximum = std::max(ChartMaximum, Vertex.ChartId);
        Result.Stats.Charts = ChartMaximum;

        const uint64_t TotalTexels = uint64_t(Resolution) * Resolution;
        uint64_t Done = 0;
        for (uint32_t Y = 0; Y < Resolution; ++Y)
        {
            for (uint32_t X = 0; X < Resolution; ++X)
            {
                const size_t Pixel = (size_t(Y) * Resolution + X) * 4;
                const float PixelU = (float(X) + 0.5f) / float(Resolution);
                const float PixelV = 1.f - (float(Y) + 0.5f) / float(Resolution); // texture v grows upward

                bool Covered = false;
                Interpolated Sample{};
                for (size_t Index = 0; Index + 2 < Part.Indices.size() && !Covered; Index += 3)
                {
                    const auto &A = Part.Vertices[Part.Indices[Index + 0]];
                    const auto &B = Part.Vertices[Part.Indices[Index + 1]];
                    const auto &C = Part.Vertices[Part.Indices[Index + 2]];
                    const float Denominator = (B.Uv[1] - C.Uv[1]) * (A.Uv[0] - C.Uv[0]) + (C.Uv[0] - B.Uv[0]) * (A.Uv[1] - C.Uv[1]);
                    if (std::fabs(Denominator) < 1e-12f) continue;
                    // 📝 Standard barycentric weights: WeightA is A's weight, WeightB is B's, WeightC is C's.
                    const float WeightA = ((B.Uv[1] - C.Uv[1]) * (PixelU - C.Uv[0]) + (C.Uv[0] - B.Uv[0]) * (PixelV - C.Uv[1])) / Denominator;
                    const float WeightB = ((C.Uv[1] - A.Uv[1]) * (PixelU - C.Uv[0]) + (A.Uv[0] - C.Uv[0]) * (PixelV - C.Uv[1])) / Denominator;
                    const float WeightC = 1.f - WeightA - WeightB;
                    if (WeightA < -1e-4f || WeightB < -1e-4f || WeightC < -1e-4f) continue;
                    Sample = BarycentricLerp(A, B, C, WeightA, WeightB, WeightC);
                    Covered = true;
                }

                for (auto &Map : Result.Maps)
                {
                    uint8_t Out[4]{0, 0, 0, 0};
                    if (Map.Key == "coverage")
                    {
                        Out[0] = Out[1] = Out[2] = Covered ? 255 : 0;
                        Out[3] = Covered ? 255 : 0;
                    }
                    else if (Covered && (Map.Key == "normal" || Map.Key == "world-normal" || Map.Key == "alpha"))
                    {
                        float WorldPosition[3], WorldNormal[3];
                        Transform(Part.ObjectToWorld, Sample.Position, WorldPosition);
                        for (int Axis = 0; Axis < 3; ++Axis) WorldNormal[Axis] = Sample.Normal[Axis];
                        Normalize(WorldNormal);
                        // 📝 "Ray front/back limits act as a uniform projection envelope" (baking-studio.md): a
                        // single ray spans from Back behind the low surface to Front in front of it along the
                        // normal, taking the nearest high-surface hit anywhere in that envelope. This also makes
                        // an identical low==high demo mesh self-intersect correctly (hit at t ≈ Back), instead of
                        // two directionally-exclusive half-searches that can straddle a flat surface and miss it.
                        const float EnvelopeLength = Settings.Back + Settings.Front;
                        float RayOrigin[3]{WorldPosition[0] - WorldNormal[0] * Settings.Back, WorldPosition[1] - WorldNormal[1] * Settings.Back,
                            WorldPosition[2] - WorldNormal[2] * Settings.Back};
                        auto Hit = Bvh.CastRay(RayOrigin, WorldNormal, 0.f, EnvelopeLength);
                        if (Hit.Found)
                        {
                            ++Result.Stats.Hits;
                            if (Map.Key == "alpha") { Out[0] = Out[1] = Out[2] = Out[3] = 255; }
                            else if (Map.Key == "world-normal")
                            {
                                float Signed = Settings.NormalPlusY ? Hit.Normal[1] : -Hit.Normal[1];
                                Out[0] = ToByte(Hit.Normal[0] * 0.5f + 0.5f);
                                Out[1] = ToByte(Signed * 0.5f + 0.5f);
                                Out[2] = ToByte(Hit.Normal[2] * 0.5f + 0.5f);
                                Out[3] = 255;
                            }
                            else // tangent normal
                            {
                                float Tangent[3], Bitangent[3];
                                TangentBasis(WorldNormal, Tangent, Bitangent);
                                float Tx = Dot(Hit.Normal, Tangent), Ty = Dot(Hit.Normal, Bitangent), Tz = Dot(Hit.Normal, WorldNormal);
                                if (!Settings.NormalPlusY) Ty = -Ty;
                                Out[0] = ToByte(Tx * 0.5f + 0.5f);
                                Out[1] = ToByte(Ty * 0.5f + 0.5f);
                                Out[2] = ToByte(Tz * 0.5f + 0.5f);
                                Out[3] = 255;
                            }
                        }
                        else ++Result.Stats.Misses;
                    }
                    Map.Rgba[Pixel + 0] = Out[0];
                    Map.Rgba[Pixel + 1] = Out[1];
                    Map.Rgba[Pixel + 2] = Out[2];
                    Map.Rgba[Pixel + 3] = Out[3];
                }
                ++Done;
            }
            if (OnProgress)
                OnProgress(BakeProgress{Part.Name, TargetIndex, uint32_t(Targets.size()), Done, TotalTexels});
        }
        Results.push_back(std::move(Result));
    }
    return Results;
}
} // namespace Frontier
