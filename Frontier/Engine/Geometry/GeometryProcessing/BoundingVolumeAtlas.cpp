/*==========================================================================================================================================
                                             BOUNDING VOLUME ATLAS DEFINITION
==========================================================================================================================================*/
// 🧩 16-bin SAH + quantized 8-wide CWBVH builder, crease-smooth octahedral normals, dielectric Snell/TIR optics, & TLAS/CDF.

#include "Engine/Geometry/GeometryProcessing/BoundingVolumeAtlas.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace Frontier
{
namespace
{

constexpr uint32_t InteriorCellFlagBit      = 0x80000000u;
constexpr uint32_t SurfaceHeuristicBinCount = 16u;
constexpr uint32_t MaximumBinaryLeafFacets  = 3u;
constexpr uint32_t WideChildCapacity        = 8u;
constexpr float    PiConstant               = 3.14159265358979323846f;

[[nodiscard]] inline std::array<float, 3> NormalizeVec3(std::array<float, 3> V) noexcept
{
    const float LenSq = V[0] * V[0] + V[1] * V[1] + V[2] * V[2];
    if (LenSq <= 1.0e-18f)
    {
        return {0.0f, 1.0f, 0.0f};
    }
    const float Inv = 1.0f / std::sqrt(LenSq);
    return {V[0] * Inv, V[1] * Inv, V[2] * Inv};
}

[[nodiscard]] inline float DotVec3(std::array<float, 3> A, std::array<float, 3> B) noexcept
{
    return A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
}

[[nodiscard]] inline std::array<float, 3> CrossVec3(std::array<float, 3> A, std::array<float, 3> B) noexcept
{
    return {
        A[1] * B[2] - A[2] * B[1],
        A[2] * B[0] - A[0] * B[2],
        A[0] * B[1] - A[1] * B[0],
    };
}

[[nodiscard]] inline float SurfaceAreaBox(const std::array<float, 3>& Lo, const std::array<float, 3>& Hi) noexcept
{
    const float Dx = std::max(0.0f, Hi[0] - Lo[0]);
    const float Dy = std::max(0.0f, Hi[1] - Lo[1]);
    const float Dz = std::max(0.0f, Hi[2] - Lo[2]);
    return 2.0f * (Dx * Dy + Dy * Dz + Dz * Dx);
}

[[nodiscard]] inline std::array<float, 3> SafeReciprocalDirection(std::array<float, 3> Dir) noexcept
{
    return {
        1.0f / (std::abs(Dir[0]) > 1.0e-8f ? Dir[0] : (Dir[0] >= 0.0f ? 1.0e-8f : -1.0e-8f)),
        1.0f / (std::abs(Dir[1]) > 1.0e-8f ? Dir[1] : (Dir[1] >= 0.0f ? 1.0e-8f : -1.0e-8f)),
        1.0f / (std::abs(Dir[2]) > 1.0e-8f ? Dir[2] : (Dir[2] >= 0.0f ? 1.0e-8f : -1.0e-8f)),
    };
}

[[nodiscard]] inline bool IntersectSlabAabb(const std::array<float, 3>& Lo,
                                            const std::array<float, 3>& Hi,
                                            const std::array<float, 3>& Origin,
                                            const std::array<float, 3>& InvDir,
                                            float                       MinT,
                                            float                       MaxT,
                                            float&                      OutEntryT) noexcept
{
    float T0 = MinT;
    float T1 = MaxT;
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        const float A = (Lo[Axis] - Origin[Axis]) * InvDir[Axis];
        const float B = (Hi[Axis] - Origin[Axis]) * InvDir[Axis];
        T0            = std::max(T0, std::min(A, B));
        T1            = std::min(T1, std::max(A, B));
        if (T1 < T0)
        {
            return false;
        }
    }
    OutEntryT = T0;
    return true;
}

[[nodiscard]] inline bool IntersectRayTriangleMoller(const PackedRayTriangle&    Tri,
                                                     const std::array<float, 3>& Origin,
                                                     const std::array<float, 3>& Dir,
                                                     float                       MinT,
                                                     float                       MaxT,
                                                     float&                      OutT,
                                                     float&                      OutU,
                                                     float&                      OutV) noexcept
{
    const std::array<float, 3> Pvec = CrossVec3(Dir, Tri.Edge2);
    const float                Det  = DotVec3(Tri.Edge1, Pvec);
    if (std::abs(Det) <= 1.0e-9f)
    {
        return false;
    }
    const float                InvDet = 1.0f / Det;
    const std::array<float, 3> Tvec   = {
        Origin[0] - Tri.Vertex0[0],
        Origin[1] - Tri.Vertex0[1],
        Origin[2] - Tri.Vertex0[2],
    };
    const float U = DotVec3(Tvec, Pvec) * InvDet;
    if (U < 0.0f || U > 1.0f)
    {
        return false;
    }
    const std::array<float, 3> Qvec = CrossVec3(Tvec, Tri.Edge1);
    const float                V    = DotVec3(Dir, Qvec) * InvDet;
    if (V < 0.0f || (U + V) > 1.0f)
    {
        return false;
    }
    const float HitT = DotVec3(Tri.Edge2, Qvec) * InvDet;
    if (HitT < MinT || HitT > MaxT)
    {
        return false;
    }
    OutT = HitT;
    OutU = U;
    OutV = V;
    return true;
}

[[nodiscard]] inline uint32_t ExtractByte(uint32_t LowWord, uint32_t HighWord, uint32_t ChildSlot) noexcept
{
    const uint32_t Word  = (ChildSlot < 4u) ? LowWord : HighWord;
    const uint32_t Shift = (ChildSlot & 3u) * 8u;
    return (Word >> Shift) & 0xFFu;
}

inline void SetByteInPair(uint32_t& LowWord, uint32_t& HighWord, uint32_t ChildSlot, uint32_t ByteVal) noexcept
{
    const uint32_t Clamped = ByteVal & 0xFFu;
    const uint32_t Shift   = (ChildSlot & 3u) * 8u;
    if (ChildSlot < 4u)
    {
        LowWord = (LowWord & ~(0xFFu << Shift)) | (Clamped << Shift);
    }
    else
    {
        HighWord = (HighWord & ~(0xFFu << Shift)) | (Clamped << Shift);
    }
}

struct WeldedPositionKey
{
    int32_t  Qx          = 0;
    int32_t  Qy          = 0;
    int32_t  Qz          = 0;
    uint32_t ClassBucket = 0u;

    bool operator==(const WeldedPositionKey& Other) const noexcept
    {
        return Qx == Other.Qx && Qy == Other.Qy && Qz == Other.Qz && ClassBucket == Other.ClassBucket;
    }
};

struct WeldedPositionHasher
{
    size_t operator()(const WeldedPositionKey& K) const noexcept
    {
        uint64_t H = static_cast<uint64_t>(static_cast<uint32_t>(K.Qx)) * 73856093ull;
        H ^= static_cast<uint64_t>(static_cast<uint32_t>(K.Qy)) * 19349663ull;
        H ^= static_cast<uint64_t>(static_cast<uint32_t>(K.Qz)) * 83492791ull;
        H ^= static_cast<uint64_t>(K.ClassBucket) * 2654435761ull;
        return static_cast<size_t>(H);
    }
};

void PopulateSmoothVertexNormals(std::vector<PackedRayTriangle>& RawTris)
{
    if (RawTris.empty())
    {
        return;
    }

    struct NormalCluster
    {
        std::array<float, 3> ReferenceNormal = {};
        std::array<float, 3> Accumulated     = {};
    };

    std::unordered_map<WeldedPositionKey, std::vector<NormalCluster>, WeldedPositionHasher> Adjacency;
    Adjacency.reserve(RawTris.size() * 2u);

    std::vector<std::array<float, 3>> FaceWeightedNormals(RawTris.size());
    std::vector<std::array<float, 3>> FaceUnitNormals(RawTris.size());

    auto MakeKey = [](std::array<float, 3> P, uint32_t ClassId) -> WeldedPositionKey {
        return WeldedPositionKey{
            static_cast<int32_t>(std::llround(P[0] * 5000.0f)),
            static_cast<int32_t>(std::llround(P[1] * 5000.0f)),
            static_cast<int32_t>(std::llround(P[2] * 5000.0f)),
            ClassId,
        };
    };

    constexpr float CreaseCosineThreshold = 0.52f; // ~58.7 degrees preserves hard body creases while smoothing panels/rims/glass

    for (size_t TriIdx = 0u; TriIdx < RawTris.size(); ++TriIdx)
    {
        PackedRayTriangle&         Tri       = RawTris[TriIdx];
        const std::array<float, 3> CrossProd = CrossVec3(Tri.Edge1, Tri.Edge2);
        const float                CrossLen  = std::sqrt(DotVec3(CrossProd, CrossProd));
        Tri.SurfaceAreaM2                    = 0.5f * CrossLen;
        const std::array<float, 3> UnitN     = (CrossLen > 1.0e-10f)
                                                   ? std::array<float, 3>{CrossProd[0] / CrossLen, CrossProd[1] / CrossLen, CrossProd[2] / CrossLen}
                                                   : std::array<float, 3>{0.0f, 1.0f, 0.0f};
        FaceWeightedNormals[TriIdx]          = CrossProd;
        FaceUnitNormals[TriIdx]              = UnitN;

        const std::array<std::array<float, 3>, 3> Corners = {{
            Tri.Vertex0,
            {Tri.Vertex0[0] + Tri.Edge1[0], Tri.Vertex0[1] + Tri.Edge1[1], Tri.Vertex0[2] + Tri.Edge1[2]},
            {Tri.Vertex0[0] + Tri.Edge2[0], Tri.Vertex0[1] + Tri.Edge2[1], Tri.Vertex0[2] + Tri.Edge2[2]},
        }};

        for (int C = 0; C < 3; ++C)
        {
            auto& Clusters = Adjacency[MakeKey(Corners[C], Tri.ClassificationId)];
            bool  Merged   = false;
            for (NormalCluster& Group : Clusters)
            {
                if (DotVec3(Group.ReferenceNormal, UnitN) >= CreaseCosineThreshold)
                {
                    Group.Accumulated[0] += CrossProd[0];
                    Group.Accumulated[1] += CrossProd[1];
                    Group.Accumulated[2] += CrossProd[2];
                    Merged = true;
                }
            }
            if (!Merged)
            {
                Clusters.push_back(NormalCluster{UnitN, CrossProd});
            }
        }
    }

    for (size_t TriIdx = 0u; TriIdx < RawTris.size(); ++TriIdx)
    {
        PackedRayTriangle&         Tri   = RawTris[TriIdx];
        const std::array<float, 3> UnitN = FaceUnitNormals[TriIdx];

        const std::array<std::array<float, 3>, 3> Corners = {{
            Tri.Vertex0,
            {Tri.Vertex0[0] + Tri.Edge1[0], Tri.Vertex0[1] + Tri.Edge1[1], Tri.Vertex0[2] + Tri.Edge1[2]},
            {Tri.Vertex0[0] + Tri.Edge2[0], Tri.Vertex0[1] + Tri.Edge2[1], Tri.Vertex0[2] + Tri.Edge2[2]},
        }};

        for (int C = 0; C < 3; ++C)
        {
            std::array<float, 3> Smooth = FaceWeightedNormals[TriIdx];
            const auto           It     = Adjacency.find(MakeKey(Corners[C], Tri.ClassificationId));
            if (It != Adjacency.end())
            {
                std::array<float, 3> Sum = {0.0f, 0.0f, 0.0f};
                for (const NormalCluster& Group : It->second)
                {
                    if (DotVec3(Group.ReferenceNormal, UnitN) >= CreaseCosineThreshold)
                    {
                        Sum[0] += Group.Accumulated[0];
                        Sum[1] += Group.Accumulated[1];
                        Sum[2] += Group.Accumulated[2];
                    }
                }
                if (DotVec3(Sum, Sum) > 1.0e-16f)
                {
                    Smooth = Sum;
                }
            }
            Tri.OctVertexNormals[C] = PackOctahedralNormal(NormalizeVec3(Smooth));
        }
    }
}

void FinalizeHitNormalsAndUv(const PackedRayTriangle&    HitTri,
                             const std::array<float, 3>& RayOrigin,
                             const std::array<float, 3>& RayDirection,
                             float                       HitDistance,
                             float                       BaryU,
                             float                       BaryV,
                             SoftwareRayHit&             OutHit) noexcept
{
    const float                BaryW  = 1.0f - BaryU - BaryV;
    std::array<float, 3>       GeomN  = NormalizeVec3(CrossVec3(HitTri.Edge1, HitTri.Edge2));
    const std::array<float, 3> N0     = UnpackOctahedralNormal(HitTri.OctVertexNormals[0]);
    const std::array<float, 3> N1     = UnpackOctahedralNormal(HitTri.OctVertexNormals[1]);
    const std::array<float, 3> N2     = UnpackOctahedralNormal(HitTri.OctVertexNormals[2]);
    std::array<float, 3>       ShadeN = NormalizeVec3({
        BaryW * N0[0] + BaryU * N1[0] + BaryV * N2[0],
        BaryW * N0[1] + BaryU * N1[1] + BaryV * N2[1],
        BaryW * N0[2] + BaryU * N1[2] + BaryV * N2[2],
    });
    if (DotVec3(ShadeN, GeomN) < 0.0f)
    {
        ShadeN = {-ShadeN[0], -ShadeN[1], -ShadeN[2]};
    }
    if (DotVec3(GeomN, RayDirection) > 0.0f)
    {
        GeomN  = {-GeomN[0], -GeomN[1], -GeomN[2]};
        ShadeN = {-ShadeN[0], -ShadeN[1], -ShadeN[2]};
    }

    OutHit.Hit               = true;
    OutHit.DistanceMeters    = HitDistance;
    OutHit.ClassificationId  = HitTri.ClassificationId;
    OutHit.TransmissiveGlass = HitTri.TransmissiveGlass;
    OutHit.RefractiveIndex   = HitTri.RefractiveIndex;
    OutHit.AbsorptionPerM    = HitTri.AbsorptionPerM;
    OutHit.WorldPosition     = {
        RayOrigin[0] + RayDirection[0] * HitDistance,
        RayOrigin[1] + RayDirection[1] * HitDistance,
        RayOrigin[2] + RayDirection[2] * HitDistance,
    };
    OutHit.PreviousWorldPosition = OutHit.WorldPosition;
    OutHit.GeometricNormal       = GeomN;
    OutHit.ShadingNormal         = ShadeN;
    OutHit.TextureCoord          = {
        BaryW * HitTri.Uv0U + BaryU * HitTri.Uv1[0] + BaryV * HitTri.Uv2[0],
        BaryW * HitTri.Uv0V + BaryU * HitTri.Uv1[1] + BaryV * HitTri.Uv2[1],
    };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                      OCTAHEDRAL NORMALS & DIELECTRIC OPTICS
//------------------------------------------------------------------------------------------------------------------------

uint32_t PackOctahedralNormal(std::array<float, 3> UnitNormal) noexcept
{
    const std::array<float, 3> N      = NormalizeVec3(UnitNormal);
    const float                InvL1  = 1.0f / std::max(std::abs(N[0]) + std::abs(N[1]) + std::abs(N[2]), 1.0e-8f);
    float                      Ox     = N[0] * InvL1;
    float                      Oy     = N[1] * InvL1;
    if (N[2] < 0.0f)
    {
        const float WrapX = (1.0f - std::abs(Oy)) * (Ox >= 0.0f ? 1.0f : -1.0f);
        const float WrapY = (1.0f - std::abs(Ox)) * (Oy >= 0.0f ? 1.0f : -1.0f);
        Ox                = WrapX;
        Oy                = WrapY;
    }
    const int32_t Qx = std::clamp(static_cast<int32_t>(std::llround(Ox * 32767.0f)), -32767, 32767);
    const int32_t Qy = std::clamp(static_cast<int32_t>(std::llround(Oy * 32767.0f)), -32767, 32767);
    return (static_cast<uint32_t>(static_cast<uint16_t>(static_cast<int16_t>(Qx)))) |
           (static_cast<uint32_t>(static_cast<uint16_t>(static_cast<int16_t>(Qy))) << 16u);
}

std::array<float, 3> UnpackOctahedralNormal(uint32_t PackedWord) noexcept
{
    if (PackedWord == 0u)
    {
        return {0.0f, 1.0f, 0.0f};
    }
    const float Ox = static_cast<float>(static_cast<int16_t>(PackedWord & 0xFFFFu)) / 32767.0f;
    const float Oy = static_cast<float>(static_cast<int16_t>((PackedWord >> 16u) & 0xFFFFu)) / 32767.0f;
    float       Nx = Ox;
    float       Ny = Oy;
    float       Nz = 1.0f - std::abs(Ox) - std::abs(Oy);
    if (Nz < 0.0f)
    {
        const float Fold = -Nz;
        Nx += (Nx >= 0.0f) ? -Fold : Fold;
        Ny += (Ny >= 0.0f) ? -Fold : Fold;
    }
    return NormalizeVec3({Nx, Ny, Nz});
}

DielectricBoundaryResult EvaluateDielectricBoundary(std::array<float, 3> IncidentUnit,
                                                    std::array<float, 3> BoundaryNormalUnit,
                                                    float                EtaIncidentOverTransmitted) noexcept
{
    DielectricBoundaryResult Out{};
    const std::array<float, 3> I = NormalizeVec3(IncidentUnit);
    std::array<float, 3>       N = NormalizeVec3(BoundaryNormalUnit);
    if (DotVec3(I, N) > 0.0f)
    {
        N = {-N[0], -N[1], -N[2]};
    }

    const float CosThetaI   = std::clamp(-DotVec3(I, N), 0.0f, 1.0f);
    Out.ReflectedDirection  = NormalizeVec3({
        I[0] + 2.0f * CosThetaI * N[0],
        I[1] + 2.0f * CosThetaI * N[1],
        I[2] + 2.0f * CosThetaI * N[2],
    });

    const float Eta        = std::max(EtaIncidentOverTransmitted, 1.0e-4f);
    const float Sin2ThetaI = std::max(0.0f, 1.0f - CosThetaI * CosThetaI);
    const float Sin2ThetaT = Eta * Eta * Sin2ThetaI;
    if (Sin2ThetaT >= 1.0f)
    {
        Out.TotalInternalReflection = true;
        Out.FresnelReflectance      = 1.0f;
        Out.RefractedDirection      = Out.ReflectedDirection;
        return Out;
    }

    const float CosThetaT = std::sqrt(std::max(0.0f, 1.0f - Sin2ThetaT));
    // Exact unpolarized Fresnel equations: η_i / η_t = Eta  =>  Rs = (Eta*cosθ_i - cosθ_t)/(Eta*cosθ_i + cosθ_t)
    const float Rs = (Eta * CosThetaI - CosThetaT) / std::max(Eta * CosThetaI + CosThetaT, 1.0e-6f);
    const float Rp = (CosThetaI - Eta * CosThetaT) / std::max(CosThetaI + Eta * CosThetaT, 1.0e-6f);

    Out.TotalInternalReflection = false;
    Out.FresnelReflectance      = std::clamp(0.5f * (Rs * Rs + Rp * Rp), 0.0f, 1.0f);
    const float ScaleN          = Eta * CosThetaI - CosThetaT;
    Out.RefractedDirection      = NormalizeVec3({
        Eta * I[0] + ScaleN * N[0],
        Eta * I[1] + ScaleN * N[1],
        Eta * I[2] + ScaleN * N[2],
    });
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      BINARY SAH & 8-WIDE CWBVH CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

uint32_t BoundingVolumeAtlas::BuildRecursive(std::vector<uint32_t>&             Indices,
                                             std::vector<std::array<float, 3>>& Centroids,
                                             std::vector<std::array<float, 3>>& TriBoundsMin,
                                             std::vector<std::array<float, 3>>& TriBoundsMax,
                                             uint32_t                           Begin,
                                             uint32_t                           Count)
{
    EnclosureCell Cell{};
    Cell.LowerBounds = {1.0e30f, 1.0e30f, 1.0e30f};
    Cell.UpperBounds = {-1.0e30f, -1.0e30f, -1.0e30f};

    std::array<float, 3> CentMin = {1.0e30f, 1.0e30f, 1.0e30f};
    std::array<float, 3> CentMax = {-1.0e30f, -1.0e30f, -1.0e30f};

    for (uint32_t I = Begin; I < Begin + Count; ++I)
    {
        const uint32_t TriIdx = Indices[I];
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            Cell.LowerBounds[Axis] = std::min(Cell.LowerBounds[Axis], TriBoundsMin[TriIdx][Axis] - 1.0e-4f);
            Cell.UpperBounds[Axis] = std::max(Cell.UpperBounds[Axis], TriBoundsMax[TriIdx][Axis] + 1.0e-4f);
            CentMin[Axis]          = std::min(CentMin[Axis], Centroids[TriIdx][Axis]);
            CentMax[Axis]          = std::max(CentMax[Axis], Centroids[TriIdx][Axis]);
        }
    }

    const uint32_t CellIndex = static_cast<uint32_t>(Cells_.size());
    Cells_.push_back(Cell);

    if (Count <= MaximumBinaryLeafFacets)
    {
        Cells_[CellIndex].LeftOrFirst  = Begin;
        Cells_[CellIndex].RightOrCount = Count;
        return CellIndex;
    }

    const float ParentArea = std::max(SurfaceAreaBox(Cell.LowerBounds, Cell.UpperBounds), 1.0e-8f);
    float       BestCost   = static_cast<float>(Count);
    int         BestAxis   = -1;
    uint32_t    BestSplit  = 0u;

    struct HeuristicBin
    {
        std::array<float, 3> Lo    = {1.0e30f, 1.0e30f, 1.0e30f};
        std::array<float, 3> Hi    = {-1.0e30f, -1.0e30f, -1.0e30f};
        uint32_t             Count = 0u;
    };

    for (int Axis = 0; Axis < 3; ++Axis)
    {
        const float Span = CentMax[Axis] - CentMin[Axis];
        if (Span <= 1.0e-6f)
        {
            continue;
        }
        std::array<HeuristicBin, SurfaceHeuristicBinCount> Bins{};
        const float Scale = static_cast<float>(SurfaceHeuristicBinCount) / Span;
        for (uint32_t I = Begin; I < Begin + Count; ++I)
        {
            const uint32_t TriIdx = Indices[I];
            const uint32_t BinIdx = std::min(
                SurfaceHeuristicBinCount - 1u,
                static_cast<uint32_t>(std::max(0.0f, (Centroids[TriIdx][Axis] - CentMin[Axis]) * Scale)));
            ++Bins[BinIdx].Count;
            for (int A = 0; A < 3; ++A)
            {
                Bins[BinIdx].Lo[A] = std::min(Bins[BinIdx].Lo[A], TriBoundsMin[TriIdx][A]);
                Bins[BinIdx].Hi[A] = std::max(Bins[BinIdx].Hi[A], TriBoundsMax[TriIdx][A]);
            }
        }

        std::array<float, SurfaceHeuristicBinCount - 1u>    LeftArea{};
        std::array<uint32_t, SurfaceHeuristicBinCount - 1u> LeftCount{};
        HeuristicBin                                        RunLeft{};
        for (uint32_t S = 0u; S < SurfaceHeuristicBinCount - 1u; ++S)
        {
            RunLeft.Count += Bins[S].Count;
            for (int A = 0; A < 3; ++A)
            {
                RunLeft.Lo[A] = std::min(RunLeft.Lo[A], Bins[S].Lo[A]);
                RunLeft.Hi[A] = std::max(RunLeft.Hi[A], Bins[S].Hi[A]);
            }
            LeftCount[S] = RunLeft.Count;
            LeftArea[S]  = (RunLeft.Count > 0u) ? SurfaceAreaBox(RunLeft.Lo, RunLeft.Hi) : 0.0f;
        }

        HeuristicBin RunRight{};
        for (int S = static_cast<int>(SurfaceHeuristicBinCount) - 2; S >= 0; --S)
        {
            const uint32_t RightIdx = static_cast<uint32_t>(S + 1);
            RunRight.Count += Bins[RightIdx].Count;
            for (int A = 0; A < 3; ++A)
            {
                RunRight.Lo[A] = std::min(RunRight.Lo[A], Bins[RightIdx].Lo[A]);
                RunRight.Hi[A] = std::max(RunRight.Hi[A], Bins[RightIdx].Hi[A]);
            }
            if (LeftCount[S] == 0u || RunRight.Count == 0u)
            {
                continue;
            }
            const float RightArea = SurfaceAreaBox(RunRight.Lo, RunRight.Hi);
            const float Cost      = 0.5f + (LeftArea[S] * static_cast<float>(LeftCount[S]) +
                                       RightArea * static_cast<float>(RunRight.Count)) /
                                          ParentArea;
            if (Cost < BestCost)
            {
                BestCost  = Cost;
                BestAxis  = Axis;
                BestSplit = static_cast<uint32_t>(S + 1);
            }
        }
    }

    uint32_t Mid = Begin + Count / 2u;
    if (BestAxis >= 0)
    {
        const float Span    = CentMax[BestAxis] - CentMin[BestAxis];
        const float Scale   = static_cast<float>(SurfaceHeuristicBinCount) / Span;
        auto        SplitIt = std::partition(
            Indices.begin() + Begin,
            Indices.begin() + Begin + Count,
            [&](uint32_t TriIdx) {
                const uint32_t BinIdx = std::min(
                    SurfaceHeuristicBinCount - 1u,
                    static_cast<uint32_t>(std::max(0.0f, (Centroids[TriIdx][BestAxis] - CentMin[BestAxis]) * Scale)));
                return BinIdx < BestSplit;
            });
        Mid = static_cast<uint32_t>(SplitIt - Indices.begin());
        if (Mid == Begin || Mid == Begin + Count)
        {
            Mid = Begin + Count / 2u;
            std::nth_element(Indices.begin() + Begin,
                             Indices.begin() + Mid,
                             Indices.begin() + Begin + Count,
                             [&](uint32_t A, uint32_t B) {
                                 return Centroids[A][BestAxis] < Centroids[B][BestAxis];
                             });
        }
    }
    else
    {
        int WideAxis = 0;
        for (int A = 1; A < 3; ++A)
        {
            if ((CentMax[A] - CentMin[A]) > (CentMax[WideAxis] - CentMin[WideAxis]))
            {
                WideAxis = A;
            }
        }
        std::nth_element(Indices.begin() + Begin,
                         Indices.begin() + Mid,
                         Indices.begin() + Begin + Count,
                         [&](uint32_t A, uint32_t B) {
                             return Centroids[A][WideAxis] < Centroids[B][WideAxis];
                         });
    }

    const uint32_t LeftChild       = BuildRecursive(Indices, Centroids, TriBoundsMin, TriBoundsMax, Begin, Mid - Begin);
    const uint32_t RightChild      = BuildRecursive(Indices, Centroids, TriBoundsMin, TriBoundsMax, Mid, Begin + Count - Mid);
    Cells_[CellIndex].LeftOrFirst  = LeftChild;
    Cells_[CellIndex].RightOrCount = RightChild | InteriorCellFlagBit;
    return CellIndex;
}

uint32_t BoundingVolumeAtlas::CollapseWideSubtree(uint32_t BinaryCellIndex)
{
    const uint32_t TargetWideSlot = static_cast<uint32_t>(WideCells_.size());
    WideCells_.push_back(CompressedWideCell{});

    struct CandidateSlot
    {
        uint32_t BinaryIdx    = 0u;
        float    SurfaceArea  = 0.0f;
        bool     InteriorCell = false;
    };

    std::array<CandidateSlot, WideChildCapacity> Slots{};
    uint32_t                                     SlotCount = 0u;

    const EnclosureCell& RootBin = Cells_[BinaryCellIndex];
    if ((RootBin.RightOrCount & InteriorCellFlagBit) == 0u)
    {
        Slots[SlotCount++] = CandidateSlot{BinaryCellIndex, SurfaceAreaBox(RootBin.LowerBounds, RootBin.UpperBounds), false};
    }
    else
    {
        const uint32_t L = RootBin.LeftOrFirst;
        const uint32_t R = RootBin.RightOrCount & ~InteriorCellFlagBit;
        Slots[SlotCount++] = CandidateSlot{L, SurfaceAreaBox(Cells_[L].LowerBounds, Cells_[L].UpperBounds),
                                           (Cells_[L].RightOrCount & InteriorCellFlagBit) != 0u};
        Slots[SlotCount++] = CandidateSlot{R, SurfaceAreaBox(Cells_[R].LowerBounds, Cells_[R].UpperBounds),
                                           (Cells_[R].RightOrCount & InteriorCellFlagBit) != 0u};

        while (SlotCount < WideChildCapacity)
        {
            int   BestSlot = -1;
            float BestArea = -1.0f;
            for (uint32_t C = 0u; C < SlotCount; ++C)
            {
                if (Slots[C].InteriorCell && Slots[C].SurfaceArea > BestArea)
                {
                    BestArea = Slots[C].SurfaceArea;
                    BestSlot = static_cast<int>(C);
                }
            }
            if (BestSlot < 0)
            {
                break;
            }
            const EnclosureCell& ExpandCell = Cells_[Slots[BestSlot].BinaryIdx];
            const uint32_t       ChildL     = ExpandCell.LeftOrFirst;
            const uint32_t       ChildR     = ExpandCell.RightOrCount & ~InteriorCellFlagBit;
            Slots[BestSlot] = CandidateSlot{ChildL, SurfaceAreaBox(Cells_[ChildL].LowerBounds, Cells_[ChildL].UpperBounds),
                                            (Cells_[ChildL].RightOrCount & InteriorCellFlagBit) != 0u};
            Slots[SlotCount++] = CandidateSlot{ChildR, SurfaceAreaBox(Cells_[ChildR].LowerBounds, Cells_[ChildR].UpperBounds),
                                               (Cells_[ChildR].RightOrCount & InteriorCellFlagBit) != 0u};
        }
    }

    std::array<float, 3> ParentMin = {1.0e30f, 1.0e30f, 1.0e30f};
    std::array<float, 3> ParentMax = {-1.0e30f, -1.0e30f, -1.0e30f};
    for (uint32_t C = 0u; C < SlotCount; ++C)
    {
        const EnclosureCell& Sub = Cells_[Slots[C].BinaryIdx];
        for (int A = 0; A < 3; ++A)
        {
            ParentMin[A] = std::min(ParentMin[A], Sub.LowerBounds[A]);
            ParentMax[A] = std::max(ParentMax[A], Sub.UpperBounds[A]);
        }
    }

    std::array<int, 3>   Exponents = {0, 0, 0};
    std::array<float, 3> InvSteps  = {1.0f, 1.0f, 1.0f};
    for (int A = 0; A < 3; ++A)
    {
        const float Span = std::max(ParentMax[A] - ParentMin[A], 1.0e-5f);
        Exponents[A]     = std::clamp(static_cast<int>(std::ceil(std::log2(Span / 255.0f))), -126, 126);
        InvSteps[A]      = std::ldexp(1.0f, -Exponents[A]);
    }

    CompressedWideCell Built{};
    Built.ParentOrigin       = ParentMin;
    Built.PrimitiveBaseIndex = static_cast<uint32_t>(WideTriangles_.size());

    uint32_t InteriorMask      = 0u;
    uint32_t InteriorChildRuns = 0u;
    uint32_t LocalPrimOffset   = 0u;

    std::array<int, WideChildCapacity> InteriorBinaryTargets{};
    InteriorBinaryTargets.fill(-1);

    for (uint32_t C = 0u; C < SlotCount; ++C)
    {
        const EnclosureCell& Sub = Cells_[Slots[C].BinaryIdx];

        const uint32_t QxLo = static_cast<uint32_t>(std::clamp(static_cast<int>(std::floor(std::max(0.0f, (Sub.LowerBounds[0] - ParentMin[0]) * InvSteps[0] - 1.0e-4f))), 0, 255));
        const uint32_t QxHi = std::max(QxLo, static_cast<uint32_t>(std::clamp(static_cast<int>(std::ceil((Sub.UpperBounds[0] - ParentMin[0]) * InvSteps[0] + 1.0e-4f)), 0, 255)));
        const uint32_t QyLo = static_cast<uint32_t>(std::clamp(static_cast<int>(std::floor(std::max(0.0f, (Sub.LowerBounds[1] - ParentMin[1]) * InvSteps[1] - 1.0e-4f))), 0, 255));
        const uint32_t QyHi = std::max(QyLo, static_cast<uint32_t>(std::clamp(static_cast<int>(std::ceil((Sub.UpperBounds[1] - ParentMin[1]) * InvSteps[1] + 1.0e-4f)), 0, 255)));
        const uint32_t QzLo = static_cast<uint32_t>(std::clamp(static_cast<int>(std::floor(std::max(0.0f, (Sub.LowerBounds[2] - ParentMin[2]) * InvSteps[2] - 1.0e-4f))), 0, 255));
        const uint32_t QzHi = std::max(QzLo, static_cast<uint32_t>(std::clamp(static_cast<int>(std::ceil((Sub.UpperBounds[2] - ParentMin[2]) * InvSteps[2] + 1.0e-4f)), 0, 255)));

        SetByteInPair(Built.QuantMinXLow, Built.QuantMinXHigh, C, QxLo);
        SetByteInPair(Built.QuantMaxXLow, Built.QuantMaxXHigh, C, QxHi);
        SetByteInPair(Built.QuantMinYLow, Built.QuantMinYHigh, C, QyLo);
        SetByteInPair(Built.QuantMaxYLow, Built.QuantMaxYHigh, C, QyHi);
        SetByteInPair(Built.QuantMinZLow, Built.QuantMinZHigh, C, QzLo);
        SetByteInPair(Built.QuantMaxZLow, Built.QuantMaxZHigh, C, QzHi);

        if (Slots[C].InteriorCell)
        {
            const uint32_t LocalChildIdx = InteriorChildRuns++;
            InteriorMask |= (1u << C);
            SetByteInPair(Built.ChildMetaLow, Built.ChildMetaHigh, C, 0x80u | (LocalChildIdx & 0x07u));
            InteriorBinaryTargets[LocalChildIdx] = static_cast<int>(Slots[C].BinaryIdx);
        }
        else
        {
            const uint32_t TriCount = std::min<uint32_t>(Sub.RightOrCount, 3u);
            if (TriCount > 0u)
            {
                const uint32_t MetaByte = ((TriCount & 0x03u) << 5u) | (LocalPrimOffset & 0x1Fu);
                SetByteInPair(Built.ChildMetaLow, Built.ChildMetaHigh, C, MetaByte);
                for (uint32_t T = 0u; T < TriCount; ++T)
                {
                    WideTriangles_.push_back(Triangles_[Sub.LeftOrFirst + T]);
                }
                LocalPrimOffset += TriCount;
            }
        }
    }

    Built.PackedExponentsMask = (static_cast<uint32_t>(Exponents[0] + 127) & 0xFFu) |
                                ((static_cast<uint32_t>(Exponents[1] + 127) & 0xFFu) << 8u) |
                                ((static_cast<uint32_t>(Exponents[2] + 127) & 0xFFu) << 16u) |
                                ((InteriorMask & 0xFFu) << 24u);

    // Reserve contiguous slice for interior wide children so ChildBaseIndex + LocalChildIdx addresses them directly.
    Built.ChildBaseIndex = static_cast<uint32_t>(WideCells_.size());
    WideCells_[TargetWideSlot] = Built;

    if (InteriorChildRuns > 0u)
    {
        const uint32_t ReservedStart = Built.ChildBaseIndex;
        WideCells_.resize(ReservedStart + InteriorChildRuns);

        for (uint32_t LocalIdx = 0u; LocalIdx < InteriorChildRuns; ++LocalIdx)
        {
            // Temporarily collapse into end, then move root into reserved contiguous slot?
            // Even cleaner: collapse directly into ReservedStart + LocalIdx!
            // Let's inline a direct slot collapse helper by swapping the newly created root into ReservedStart + LocalIdx!
            const uint32_t SubBinaryIdx = static_cast<uint32_t>(InteriorBinaryTargets[LocalIdx]);
            const uint32_t TempRoot     = CollapseWideSubtree(SubBinaryIdx);
            WideCells_[ReservedStart + LocalIdx] = WideCells_[TempRoot];
            // Pop TempRoot ONLY if TempRoot was the last element (i.e. leaf-only wide node); otherwise TempRoot's children
            // already reference indices > TempRoot, never TempRoot itself, so leaving the 1 unused slot is harmless and O(1)!
            if (TempRoot + 1u == WideCells_.size())
            {
                WideCells_.pop_back();
            }
        }
    }

    return TargetWideSlot;
}

void BoundingVolumeAtlas::CollapseIntoCompressedWideHierarchy()
{
    WideCells_.clear();
    WideTriangles_.clear();
    if (Cells_.empty() || Triangles_.empty())
    {
        return;
    }
    WideCells_.reserve(std::max<size_t>(4u, Cells_.size() / 2u));
    WideTriangles_.reserve(Triangles_.size());
    (void)CollapseWideSubtree(0u);
}

bool BoundingVolumeAtlas::BuildFromFacetField(const FacetField&                   SourceFacets,
                                              const ScreenSpaceClusterRefinement* MaterialSource)
{
    Cells_.clear();
    Triangles_.clear();
    WideCells_.clear();
    WideTriangles_.clear();

    const size_t TriCount = SourceFacets.Corners.size() / 3u;
    if (TriCount == 0u)
    {
        return false;
    }

    std::vector<PackedRayTriangle>    RawTris;
    std::vector<std::array<float, 3>> Centroids;
    std::vector<std::array<float, 3>> BoundsMin;
    std::vector<std::array<float, 3>> BoundsMax;
    RawTris.reserve(TriCount);
    Centroids.reserve(TriCount);
    BoundsMin.reserve(TriCount);
    BoundsMax.reserve(TriCount);

    for (size_t TriIdx = 0u; TriIdx < TriCount; ++TriIdx)
    {
        const FacetPoint3D& C0      = SourceFacets.Corners[TriIdx * 3u + 0u];
        const FacetPoint3D& C1      = SourceFacets.Corners[TriIdx * 3u + 1u];
        const FacetPoint3D& C2      = SourceFacets.Corners[TriIdx * 3u + 2u];
        const uint32_t      ClassId =
            (TriIdx < SourceFacets.Classification.size()) ? SourceFacets.Classification[TriIdx] : 0u;

        PackedRayTriangle Packed{};
        Packed.Vertex0               = {static_cast<float>(C0.X), static_cast<float>(C0.Y), static_cast<float>(C0.Z)};
        const std::array<float, 3> V1 = {static_cast<float>(C1.X), static_cast<float>(C1.Y), static_cast<float>(C1.Z)};
        const std::array<float, 3> V2 = {static_cast<float>(C2.X), static_cast<float>(C2.Y), static_cast<float>(C2.Z)};
        Packed.Edge1                 = {V1[0] - Packed.Vertex0[0], V1[1] - Packed.Vertex0[1], V1[2] - Packed.Vertex0[2]};
        Packed.Edge2                 = {V2[0] - Packed.Vertex0[0], V2[1] - Packed.Vertex0[1], V2[2] - Packed.Vertex0[2]};

        if (TriIdx * 3u + 2u < SourceFacets.CornerUVs.size())
        {
            Packed.Uv0U = static_cast<float>(SourceFacets.CornerUVs[TriIdx * 3u + 0u][0]);
            Packed.Uv0V = static_cast<float>(SourceFacets.CornerUVs[TriIdx * 3u + 0u][1]);
            Packed.Uv1  = {static_cast<float>(SourceFacets.CornerUVs[TriIdx * 3u + 1u][0]),
                           static_cast<float>(SourceFacets.CornerUVs[TriIdx * 3u + 1u][1])};
            Packed.Uv2  = {static_cast<float>(SourceFacets.CornerUVs[TriIdx * 3u + 2u][0]),
                           static_cast<float>(SourceFacets.CornerUVs[TriIdx * 3u + 2u][1])};
        }

        Packed.ClassificationId = ClassId;
        if (MaterialSource != nullptr)
        {
            const ClusterMaterialProfile& Mat = MaterialSource->FindMaterial(ClassId);
            Packed.TransmissiveGlass          = Mat.TransmissiveGlass;
            Packed.RefractiveIndex            = Mat.RefractiveIndexIor;
            Packed.AbsorptionPerM             = {Mat.SpectralAbsorptionPerMeter.X,
                                                 Mat.SpectralAbsorptionPerMeter.Y,
                                                 Mat.SpectralAbsorptionPerMeter.Z};
        }
        else if (ClassId == 7u || ClassId == 12u)
        {
            Packed.TransmissiveGlass = true;
            Packed.RefractiveIndex   = (ClassId == 12u) ? 1.58f : 1.52f;
        }

        RawTris.push_back(Packed);
        Centroids.push_back({
            (Packed.Vertex0[0] + V1[0] + V2[0]) * (1.0f / 3.0f),
            (Packed.Vertex0[1] + V1[1] + V2[1]) * (1.0f / 3.0f),
            (Packed.Vertex0[2] + V1[2] + V2[2]) * (1.0f / 3.0f),
        });
        BoundsMin.push_back({
            std::min({Packed.Vertex0[0], V1[0], V2[0]}),
            std::min({Packed.Vertex0[1], V1[1], V2[1]}),
            std::min({Packed.Vertex0[2], V1[2], V2[2]}),
        });
        BoundsMax.push_back({
            std::max({Packed.Vertex0[0], V1[0], V2[0]}),
            std::max({Packed.Vertex0[1], V1[1], V2[1]}),
            std::max({Packed.Vertex0[2], V1[2], V2[2]}),
        });
    }

    PopulateSmoothVertexNormals(RawTris);

    std::vector<uint32_t> Order(RawTris.size());
    std::iota(Order.begin(), Order.end(), 0u);
    Cells_.reserve(RawTris.size() * 2u);
    (void)BuildRecursive(Order, Centroids, BoundsMin, BoundsMax, 0u, static_cast<uint32_t>(Order.size()));

    Triangles_.resize(Order.size());
    for (size_t NewIdx = 0u; NewIdx < Order.size(); ++NewIdx)
    {
        Triangles_[NewIdx] = RawTris[Order[NewIdx]];
    }

    CollapseIntoCompressedWideHierarchy();
    return !Cells_.empty() && !WideCells_.empty();
}

bool BoundingVolumeAtlas::BuildFromRefinementCut(const ScreenSpaceClusterRefinement& Refinement,
                                                 const ClusterRuntimeFrameReport&    CutReport)
{
    FacetField  CutFacets{};
    const auto& Sequence = Refinement.TierSequence();
    const auto& Records  = Refinement.ClusterRecords();

    auto AppendGlobals = [&](const std::vector<uint32_t>& Globals) {
        for (uint32_t Gid : Globals)
        {
            if (Gid >= Records.size())
            {
                continue;
            }
            const ClusterRefinementRecord& Rec  = Records[Gid];
            const ClusterTier&             Tier = Sequence.Tiers[Rec.TierIndex];
            const FacetCluster&            Fc   = Tier.Clusters[Rec.ClusterIndexInTier];
            for (uint32_t Tri : Fc.Triangles)
            {
                CutFacets.Corners.push_back(Tier.Complex.Positions[Tier.Complex.Triangles[Tri * 3u + 0u]]);
                CutFacets.Corners.push_back(Tier.Complex.Positions[Tier.Complex.Triangles[Tri * 3u + 1u]]);
                CutFacets.Corners.push_back(Tier.Complex.Positions[Tier.Complex.Triangles[Tri * 3u + 2u]]);
                CutFacets.Classification.push_back(Rec.ClassificationId);
                if (Tier.Complex.CarriesUVs && Tri * 3u + 2u < Tier.Complex.CornerUVs.size())
                {
                    CutFacets.CornerUVs.push_back(Tier.Complex.CornerUVs[Tri * 3u + 0u]);
                    CutFacets.CornerUVs.push_back(Tier.Complex.CornerUVs[Tri * 3u + 1u]);
                    CutFacets.CornerUVs.push_back(Tier.Complex.CornerUVs[Tri * 3u + 2u]);
                }
                else
                {
                    CutFacets.CornerUVs.push_back({0.0, 0.0});
                    CutFacets.CornerUVs.push_back({0.0, 0.0});
                    CutFacets.CornerUVs.push_back({0.0, 0.0});
                }
            }
        }
    };

    AppendGlobals(CutReport.SelectedOpaqueGlobals);
    AppendGlobals(CutReport.SelectedGlassGlobals);
    return BuildFromFacetField(CutFacets, &Refinement);
}

std::array<float, 3> BoundingVolumeAtlas::LocalLowerBounds() const noexcept
{
    return Cells_.empty() ? std::array<float, 3>{0.0f, 0.0f, 0.0f} : Cells_[0].LowerBounds;
}

std::array<float, 3> BoundingVolumeAtlas::LocalUpperBounds() const noexcept
{
    return Cells_.empty() ? std::array<float, 3>{0.0f, 0.0f, 0.0f} : Cells_[0].UpperBounds;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      CPU BINARY & 8-WIDE CWBVH RAY ORACLES
//------------------------------------------------------------------------------------------------------------------------

SoftwareRayHit BoundingVolumeAtlas::TraceClosestRay(std::array<float, 3> OriginMeters,
                                                    std::array<float, 3> DirectionUnit,
                                                    float                MinDistanceMeters,
                                                    float                MaxDistanceMeters,
                                                    bool                 IncludeGlass) const noexcept
{
    SoftwareRayHit Result{};
    if (Cells_.empty() || Triangles_.empty())
    {
        return Result;
    }

    const std::array<float, 3> InvDir = SafeReciprocalDirection(DirectionUnit);

    float                    BestT     = MaxDistanceMeters;
    float                    BestU     = 0.0f;
    float                    BestV     = 0.0f;
    uint32_t                 BestIdx   = UINT32_MAX;
    std::array<uint32_t, 64> Stack{};
    uint32_t                 StackTop  = 0u;
    float                    RootEntry = 0.0f;
    if (IntersectSlabAabb(Cells_[0].LowerBounds, Cells_[0].UpperBounds, OriginMeters, InvDir, MinDistanceMeters, BestT, RootEntry))
    {
        Stack[StackTop++] = 0u;
    }

    while (StackTop > 0u)
    {
        const EnclosureCell& Cell = Cells_[Stack[--StackTop]];
        if ((Cell.RightOrCount & InteriorCellFlagBit) != 0u)
        {
            const uint32_t LeftIdx  = Cell.LeftOrFirst;
            const uint32_t RightIdx = Cell.RightOrCount & ~InteriorCellFlagBit;
            float          EntryL = 0.0f, EntryR = 0.0f;
            const bool     HitL = IntersectSlabAabb(Cells_[LeftIdx].LowerBounds, Cells_[LeftIdx].UpperBounds, OriginMeters, InvDir, MinDistanceMeters, BestT, EntryL);
            const bool     HitR = IntersectSlabAabb(Cells_[RightIdx].LowerBounds, Cells_[RightIdx].UpperBounds, OriginMeters, InvDir, MinDistanceMeters, BestT, EntryR);
            if (HitL && HitR)
            {
                if (EntryL < EntryR)
                {
                    if (StackTop + 2u <= Stack.size())
                    {
                        Stack[StackTop++] = RightIdx;
                        Stack[StackTop++] = LeftIdx;
                    }
                }
                else
                {
                    if (StackTop + 2u <= Stack.size())
                    {
                        Stack[StackTop++] = LeftIdx;
                        Stack[StackTop++] = RightIdx;
                    }
                }
            }
            else if (HitL && StackTop + 1u <= Stack.size())
            {
                Stack[StackTop++] = LeftIdx;
            }
            else if (HitR && StackTop + 1u <= Stack.size())
            {
                Stack[StackTop++] = RightIdx;
            }
        }
        else
        {
            for (uint32_t Offset = 0u; Offset < Cell.RightOrCount; ++Offset)
            {
                const uint32_t           TriIdx = Cell.LeftOrFirst + Offset;
                const PackedRayTriangle& Tri    = Triangles_[TriIdx];
                if (!IncludeGlass && Tri.TransmissiveGlass)
                {
                    continue;
                }
                float T = 0.0f, U = 0.0f, V = 0.0f;
                if (IntersectRayTriangleMoller(Tri, OriginMeters, DirectionUnit, MinDistanceMeters, BestT, T, U, V))
                {
                    BestT   = T;
                    BestU   = U;
                    BestV   = V;
                    BestIdx = TriIdx;
                }
            }
        }
    }

    if (BestIdx == UINT32_MAX)
    {
        return Result;
    }

    Result.TriangleIndex = BestIdx;
    FinalizeHitNormalsAndUv(Triangles_[BestIdx], OriginMeters, DirectionUnit, BestT, BestU, BestV, Result);
    return Result;
}

SoftwareRayHit BoundingVolumeAtlas::TraceWideClosestRay(std::array<float, 3> OriginMeters,
                                                        std::array<float, 3> DirectionUnit,
                                                        float                MinDistanceMeters,
                                                        float                MaxDistanceMeters,
                                                        bool                 IncludeGlass) const noexcept
{
    SoftwareRayHit Result{};
    if (WideCells_.empty() || WideTriangles_.empty())
    {
        return Result;
    }

    const std::array<float, 3> InvDir = SafeReciprocalDirection(DirectionUnit);

    float                    BestT    = MaxDistanceMeters;
    float                    BestU    = 0.0f;
    float                    BestV    = 0.0f;
    uint32_t                 BestIdx  = UINT32_MAX;
    std::array<uint32_t, 64> Stack{};
    uint32_t                 StackTop = 0u;
    Stack[StackTop++]                 = 0u;

    struct OrderedChildHit
    {
        uint32_t CellIndex = 0u;
        float    EntryT    = 0.0f;
    };

    while (StackTop > 0u)
    {
        const CompressedWideCell& Wide = WideCells_[Stack[--StackTop]];
        const int                 ExpX = static_cast<int>(Wide.PackedExponentsMask & 0xFFu) - 127;
        const int                 ExpY = static_cast<int>((Wide.PackedExponentsMask >> 8u) & 0xFFu) - 127;
        const int                 ExpZ = static_cast<int>((Wide.PackedExponentsMask >> 16u) & 0xFFu) - 127;
        const float               StepX = std::ldexp(1.0f, ExpX);
        const float               StepY = std::ldexp(1.0f, ExpY);
        const float               StepZ = std::ldexp(1.0f, ExpZ);

        std::array<OrderedChildHit, WideChildCapacity> InteriorHits{};
        uint32_t                                       InteriorHitCount = 0u;

        for (uint32_t C = 0u; C < WideChildCapacity; ++C)
        {
            const uint32_t Meta = ExtractByte(Wide.ChildMetaLow, Wide.ChildMetaHigh, C);
            if (Meta == 0u)
            {
                continue;
            }

            const std::array<float, 3> Lo = {
                Wide.ParentOrigin[0] + static_cast<float>(ExtractByte(Wide.QuantMinXLow, Wide.QuantMinXHigh, C)) * StepX,
                Wide.ParentOrigin[1] + static_cast<float>(ExtractByte(Wide.QuantMinYLow, Wide.QuantMinYHigh, C)) * StepY,
                Wide.ParentOrigin[2] + static_cast<float>(ExtractByte(Wide.QuantMinZLow, Wide.QuantMinZHigh, C)) * StepZ,
            };
            const std::array<float, 3> Hi = {
                Wide.ParentOrigin[0] + static_cast<float>(ExtractByte(Wide.QuantMaxXLow, Wide.QuantMaxXHigh, C)) * StepX,
                Wide.ParentOrigin[1] + static_cast<float>(ExtractByte(Wide.QuantMaxYLow, Wide.QuantMaxYHigh, C)) * StepY,
                Wide.ParentOrigin[2] + static_cast<float>(ExtractByte(Wide.QuantMaxZLow, Wide.QuantMaxZHigh, C)) * StepZ,
            };

            float EntryT = 0.0f;
            if (!IntersectSlabAabb(Lo, Hi, OriginMeters, InvDir, MinDistanceMeters, BestT, EntryT))
            {
                continue;
            }

            if ((Meta & 0x80u) != 0u)
            {
                const uint32_t ChildIdx                = Wide.ChildBaseIndex + (Meta & 0x07u);
                InteriorHits[InteriorHitCount++] = OrderedChildHit{ChildIdx, EntryT};
            }
            else
            {
                const uint32_t TriCount   = (Meta >> 5u) & 0x03u;
                const uint32_t FirstFacet = Wide.PrimitiveBaseIndex + (Meta & 0x1Fu);
                for (uint32_t T = 0u; T < TriCount; ++T)
                {
                    const uint32_t           FacetIdx = FirstFacet + T;
                    const PackedRayTriangle& Tri      = WideTriangles_[FacetIdx];
                    if (!IncludeGlass && Tri.TransmissiveGlass)
                    {
                        continue;
                    }
                    float HitT = 0.0f, U = 0.0f, V = 0.0f;
                    if (IntersectRayTriangleMoller(Tri, OriginMeters, DirectionUnit, MinDistanceMeters, BestT, HitT, U, V))
                    {
                        BestT   = HitT;
                        BestU   = U;
                        BestV   = V;
                        BestIdx = FacetIdx;
                    }
                }
            }
        }

        // Push interior children in descending EntryT order so the nearest child pops first.
        for (uint32_t I = 1u; I < InteriorHitCount; ++I)
        {
            OrderedChildHit Key = InteriorHits[I];
            int             J   = static_cast<int>(I) - 1;
            while (J >= 0 && InteriorHits[J].EntryT < Key.EntryT)
            {
                InteriorHits[J + 1] = InteriorHits[J];
                --J;
            }
            InteriorHits[J + 1] = Key;
        }
        for (uint32_t I = 0u; I < InteriorHitCount && StackTop < Stack.size(); ++I)
        {
            Stack[StackTop++] = InteriorHits[I].CellIndex;
        }
    }

    if (BestIdx == UINT32_MAX)
    {
        return Result;
    }

    Result.TriangleIndex = BestIdx;
    FinalizeHitNormalsAndUv(WideTriangles_[BestIdx], OriginMeters, DirectionUnit, BestT, BestU, BestV, Result);
    return Result;
}

std::array<float, 3> BoundingVolumeAtlas::TraceShadowTransmittance(std::array<float, 3> OriginMeters,
                                                                   std::array<float, 3> DirectionUnit,
                                                                   float                MaxDistanceMeters) const noexcept
{
    if (Cells_.empty() || Triangles_.empty())
    {
        return {1.0f, 1.0f, 1.0f};
    }

    const std::array<float, 3> InvDir = SafeReciprocalDirection(DirectionUnit);

    std::array<float, 3>     Trans{1.0f, 1.0f, 1.0f};
    std::array<uint32_t, 64> Stack{};
    uint32_t                 StackTop  = 0u;
    float                    RootEntry = 0.0f;
    if (IntersectSlabAabb(Cells_[0].LowerBounds, Cells_[0].UpperBounds, OriginMeters, InvDir, 1.5e-3f, MaxDistanceMeters, RootEntry))
    {
        Stack[StackTop++] = 0u;
    }

    while (StackTop > 0u)
    {
        const EnclosureCell& Cell = Cells_[Stack[--StackTop]];
        if ((Cell.RightOrCount & InteriorCellFlagBit) != 0u)
        {
            const uint32_t LeftIdx  = Cell.LeftOrFirst;
            const uint32_t RightIdx = Cell.RightOrCount & ~InteriorCellFlagBit;
            float          EntryL = 0.0f, EntryR = 0.0f;
            if (IntersectSlabAabb(Cells_[LeftIdx].LowerBounds, Cells_[LeftIdx].UpperBounds, OriginMeters, InvDir, 1.5e-3f, MaxDistanceMeters, EntryL) &&
                StackTop < Stack.size())
            {
                Stack[StackTop++] = LeftIdx;
            }
            if (IntersectSlabAabb(Cells_[RightIdx].LowerBounds, Cells_[RightIdx].UpperBounds, OriginMeters, InvDir, 1.5e-3f, MaxDistanceMeters, EntryR) &&
                StackTop < Stack.size())
            {
                Stack[StackTop++] = RightIdx;
            }
        }
        else
        {
            for (uint32_t Offset = 0u; Offset < Cell.RightOrCount; ++Offset)
            {
                const PackedRayTriangle& Tri = Triangles_[Cell.LeftOrFirst + Offset];
                float                    T = 0.0f, U = 0.0f, V = 0.0f;
                if (IntersectRayTriangleMoller(Tri, OriginMeters, DirectionUnit, 1.5e-3f, MaxDistanceMeters, T, U, V))
                {
                    if (!Tri.TransmissiveGlass)
                    {
                        return {0.0f, 0.0f, 0.0f};
                    }
                    const float F0 = std::pow((Tri.RefractiveIndex - 1.0f) / (Tri.RefractiveIndex + 1.0f), 2.0f);
                    const float Tr = (1.0f - F0);
                    Trans[0] *= Tr * std::exp(-Tri.AbsorptionPerM[0] * 0.015f);
                    Trans[1] *= Tr * std::exp(-Tri.AbsorptionPerM[1] * 0.015f);
                    Trans[2] *= Tr * std::exp(-Tri.AbsorptionPerM[2] * 0.015f);
                }
            }
        }
    }
    return Trans;
}

void BoundingVolumeAtlas::PackForGpu(std::vector<std::array<float, 4>>& OutCellFloat4Table,
                                     std::vector<std::array<float, 4>>& OutTriangleFloat4Table) const
{
    OutCellFloat4Table.clear();
    OutTriangleFloat4Table.clear();

    if (Cells_.empty() || Triangles_.empty())
    {
        OutCellFloat4Table.push_back({0.0f, 0.0f, 0.0f, 0.0f});
        OutCellFloat4Table.push_back({0.0f, 0.0f, 0.0f, 0.0f});
        OutTriangleFloat4Table.resize(5u, {0.0f, 0.0f, 0.0f, 0.0f});
        return;
    }

    OutCellFloat4Table.reserve(Cells_.size() * 2u);
    for (const EnclosureCell& C : Cells_)
    {
        OutCellFloat4Table.push_back({C.LowerBounds[0], C.LowerBounds[1], C.LowerBounds[2], std::bit_cast<float>(C.LeftOrFirst)});
        OutCellFloat4Table.push_back({C.UpperBounds[0], C.UpperBounds[1], C.UpperBounds[2], std::bit_cast<float>(C.RightOrCount)});
    }

    OutTriangleFloat4Table.reserve(Triangles_.size() * 5u);
    for (const PackedRayTriangle& T : Triangles_)
    {
        const uint32_t PackedClassAndGlass = (T.ClassificationId & 0x7FFFFFFFu) | (T.TransmissiveGlass ? 0x80000000u : 0u);
        OutTriangleFloat4Table.push_back({T.Vertex0[0], T.Vertex0[1], T.Vertex0[2], T.Uv0U});
        OutTriangleFloat4Table.push_back({T.Edge1[0], T.Edge1[1], T.Edge1[2], T.Uv0V});
        OutTriangleFloat4Table.push_back({T.Edge2[0], T.Edge2[1], T.Edge2[2], std::bit_cast<float>(PackedClassAndGlass)});
        OutTriangleFloat4Table.push_back({T.Uv1[0], T.Uv1[1], T.Uv2[0], T.Uv2[1]});
        OutTriangleFloat4Table.push_back({T.AbsorptionPerM[0], T.AbsorptionPerM[1], T.AbsorptionPerM[2], T.RefractiveIndex});
    }
}

void BoundingVolumeAtlas::PackWideForGpu(std::vector<std::array<float, 4>>& OutWideCellFloat4Table,
                                         std::vector<std::array<float, 4>>& OutWideTriangleFloat4Table) const
{
    OutWideCellFloat4Table.clear();
    OutWideTriangleFloat4Table.clear();

    if (WideCells_.empty() || WideTriangles_.empty())
    {
        OutWideCellFloat4Table.resize(5u, {0.0f, 0.0f, 0.0f, 0.0f});
        OutWideTriangleFloat4Table.resize(6u, {0.0f, 0.0f, 0.0f, 0.0f});
        return;
    }

    OutWideCellFloat4Table.reserve(WideCells_.size() * 5u);
    for (const CompressedWideCell& W : WideCells_)
    {
        OutWideCellFloat4Table.push_back({W.ParentOrigin[0], W.ParentOrigin[1], W.ParentOrigin[2], std::bit_cast<float>(W.PackedExponentsMask)});
        OutWideCellFloat4Table.push_back({std::bit_cast<float>(W.ChildBaseIndex), std::bit_cast<float>(W.PrimitiveBaseIndex),
                                          std::bit_cast<float>(W.ChildMetaLow), std::bit_cast<float>(W.ChildMetaHigh)});
        OutWideCellFloat4Table.push_back({std::bit_cast<float>(W.QuantMinXLow), std::bit_cast<float>(W.QuantMinXHigh),
                                          std::bit_cast<float>(W.QuantMaxXLow), std::bit_cast<float>(W.QuantMaxXHigh)});
        OutWideCellFloat4Table.push_back({std::bit_cast<float>(W.QuantMinYLow), std::bit_cast<float>(W.QuantMinYHigh),
                                          std::bit_cast<float>(W.QuantMaxYLow), std::bit_cast<float>(W.QuantMaxYHigh)});
        OutWideCellFloat4Table.push_back({std::bit_cast<float>(W.QuantMinZLow), std::bit_cast<float>(W.QuantMinZHigh),
                                          std::bit_cast<float>(W.QuantMaxZLow), std::bit_cast<float>(W.QuantMaxZHigh)});
    }

    OutWideTriangleFloat4Table.reserve(WideTriangles_.size() * 6u);
    for (const PackedRayTriangle& T : WideTriangles_)
    {
        const uint32_t PackedClassAndGlass = (T.ClassificationId & 0x7FFFFFFFu) | (T.TransmissiveGlass ? 0x80000000u : 0u);
        OutWideTriangleFloat4Table.push_back({T.Vertex0[0], T.Vertex0[1], T.Vertex0[2], T.Uv0U});
        OutWideTriangleFloat4Table.push_back({T.Edge1[0], T.Edge1[1], T.Edge1[2], T.Uv0V});
        OutWideTriangleFloat4Table.push_back({T.Edge2[0], T.Edge2[1], T.Edge2[2], std::bit_cast<float>(PackedClassAndGlass)});
        OutWideTriangleFloat4Table.push_back({T.Uv1[0], T.Uv1[1], T.Uv2[0], T.Uv2[1]});
        OutWideTriangleFloat4Table.push_back({T.AbsorptionPerM[0], T.AbsorptionPerM[1], T.AbsorptionPerM[2], T.RefractiveIndex});
        OutWideTriangleFloat4Table.push_back({std::bit_cast<float>(T.OctVertexNormals[0]),
                                              std::bit_cast<float>(T.OctVertexNormals[1]),
                                              std::bit_cast<float>(T.OctVertexNormals[2]),
                                              T.SurfaceAreaM2});
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                  AFFINE TRANSFORMS & TWO-LEVEL INSTANCED SCENE
//------------------------------------------------------------------------------------------------------------------------

AffineTransform3x4 AffineTransform3x4::ComposeTrs(std::array<float, 3> TranslationMeters,
                                                  std::array<float, 3> EulerDegreesXyz,
                                                  std::array<float, 3> ScaleFactors) noexcept
{
    const float Rx = EulerDegreesXyz[0] * (PiConstant / 180.0f);
    const float Ry = EulerDegreesXyz[1] * (PiConstant / 180.0f);
    const float Rz = EulerDegreesXyz[2] * (PiConstant / 180.0f);
    const float Cx = std::cos(Rx), Sx = std::sin(Rx);
    const float Cy = std::cos(Ry), Sy = std::sin(Ry);
    const float Cz = std::cos(Rz), Sz = std::sin(Rz);

    // Intrinsic Y * X * Z rotation (standard Yaw -> Pitch -> Roll)
    const float M00 = (Cy * Cz + Sy * Sx * Sz) * ScaleFactors[0];
    const float M01 = (-Cy * Sz + Sy * Sx * Cz) * ScaleFactors[1];
    const float M02 = (Sy * Cx) * ScaleFactors[2];
    const float M10 = (Cx * Sz) * ScaleFactors[0];
    const float M11 = (Cx * Cz) * ScaleFactors[1];
    const float M12 = (-Sx) * ScaleFactors[2];
    const float M20 = (-Sy * Cz + Cy * Sx * Sz) * ScaleFactors[0];
    const float M21 = (Sy * Sz + Cy * Sx * Cz) * ScaleFactors[1];
    const float M22 = (Cy * Cx) * ScaleFactors[2];

    AffineTransform3x4 Out{};
    Out.Row0 = {M00, M01, M02, TranslationMeters[0]};
    Out.Row1 = {M10, M11, M12, TranslationMeters[1]};
    Out.Row2 = {M20, M21, M22, TranslationMeters[2]};
    return Out;
}

AffineTransform3x4 InvertAffineTransform3x4(const AffineTransform3x4& Source) noexcept
{
    const float A00 = Source.Row0[0], A01 = Source.Row0[1], A02 = Source.Row0[2];
    const float A10 = Source.Row1[0], A11 = Source.Row1[1], A12 = Source.Row1[2];
    const float A20 = Source.Row2[0], A21 = Source.Row2[1], A22 = Source.Row2[2];

    const float C00 = A11 * A22 - A12 * A21;
    const float C01 = A02 * A21 - A01 * A22;
    const float C02 = A01 * A12 - A02 * A11;
    const float C10 = A12 * A20 - A10 * A22;
    const float C11 = A00 * A22 - A02 * A20;
    const float C12 = A02 * A10 - A00 * A12;
    const float C20 = A10 * A21 - A11 * A20;
    const float C21 = A01 * A20 - A00 * A21;
    const float C22 = A00 * A11 - A01 * A10;

    const float Det = A00 * C00 + A01 * C10 + A02 * C20;
    if (std::abs(Det) <= 1.0e-12f)
    {
        return AffineTransform3x4::Identity();
    }
    const float InvDet = 1.0f / Det;

    AffineTransform3x4 Inv{};
    Inv.Row0[0] = C00 * InvDet;
    Inv.Row0[1] = C01 * InvDet;
    Inv.Row0[2] = C02 * InvDet;
    Inv.Row1[0] = C10 * InvDet;
    Inv.Row1[1] = C11 * InvDet;
    Inv.Row1[2] = C12 * InvDet;
    Inv.Row2[0] = C20 * InvDet;
    Inv.Row2[1] = C21 * InvDet;
    Inv.Row2[2] = C22 * InvDet;

    const float Tx = Source.Row0[3], Ty = Source.Row1[3], Tz = Source.Row2[3];
    Inv.Row0[3] = -(Inv.Row0[0] * Tx + Inv.Row0[1] * Ty + Inv.Row0[2] * Tz);
    Inv.Row1[3] = -(Inv.Row1[0] * Tx + Inv.Row1[1] * Ty + Inv.Row1[2] * Tz);
    Inv.Row2[3] = -(Inv.Row2[0] * Tx + Inv.Row2[1] * Ty + Inv.Row2[2] * Tz);
    return Inv;
}

std::array<float, 3> TransformAffinePoint(const AffineTransform3x4& M, std::array<float, 3> P) noexcept
{
    return {
        M.Row0[0] * P[0] + M.Row0[1] * P[1] + M.Row0[2] * P[2] + M.Row0[3],
        M.Row1[0] * P[0] + M.Row1[1] * P[1] + M.Row1[2] * P[2] + M.Row1[3],
        M.Row2[0] * P[0] + M.Row2[1] * P[1] + M.Row2[2] * P[2] + M.Row2[3],
    };
}

std::array<float, 3> TransformAffineVector(const AffineTransform3x4& M, std::array<float, 3> V) noexcept
{
    return {
        M.Row0[0] * V[0] + M.Row0[1] * V[1] + M.Row0[2] * V[2],
        M.Row1[0] * V[0] + M.Row1[1] * V[1] + M.Row1[2] * V[2],
        M.Row2[0] * V[0] + M.Row2[1] * V[1] + M.Row2[2] * V[2],
    };
}

void InstancedBoundingScene::Clear() noexcept
{
    Prototypes_.clear();
    Instances_.clear();
    ActivePlacements_.clear();
    TlasCells_.clear();
    EmissiveFacets_.clear();
    PublicEmissiveFacets_.clear();
    PackedWideCellTable_.clear();
    PackedWideTriTable_.clear();
    TotalWideCellCount_ = 0u;
    TotalTriangleCount_ = 0u;
    StaticTablesDirty_  = true;
}

uint32_t InstancedBoundingScene::RegisterPrototype(BoundingVolumeAtlas PrototypeAtlas)
{
    const uint32_t Index = static_cast<uint32_t>(Prototypes_.size());
    PrototypeSlice Slice{};
    Slice.GlobalWideCellOffset = TotalWideCellCount_;
    Slice.GlobalTriangleOffset = TotalTriangleCount_;
    TotalWideCellCount_ += PrototypeAtlas.WideCellCount();
    TotalTriangleCount_ += PrototypeAtlas.WideTriangleCount();
    Slice.Atlas = std::move(PrototypeAtlas);
    Prototypes_.push_back(std::move(Slice));
    StaticTablesDirty_ = true;
    return Index;
}

uint32_t InstancedBoundingScene::BuildTlasRecursive(std::vector<uint32_t>& Indices, uint32_t Begin, uint32_t Count)
{
    EnclosureCell Cell{};
    Cell.LowerBounds = {1.0e30f, 1.0e30f, 1.0e30f};
    Cell.UpperBounds = {-1.0e30f, -1.0e30f, -1.0e30f};

    std::array<float, 3> CentMin = {1.0e30f, 1.0e30f, 1.0e30f};
    std::array<float, 3> CentMax = {-1.0e30f, -1.0e30f, -1.0e30f};

    for (uint32_t I = Begin; I < Begin + Count; ++I)
    {
        const ResolvedInstance& Inst = Instances_[Indices[I]];
        for (int A = 0; A < 3; ++A)
        {
            Cell.LowerBounds[A] = std::min(Cell.LowerBounds[A], Inst.WorldLower[A] - 1.0e-3f);
            Cell.UpperBounds[A] = std::max(Cell.UpperBounds[A], Inst.WorldUpper[A] + 1.0e-3f);
            const float Cent    = 0.5f * (Inst.WorldLower[A] + Inst.WorldUpper[A]);
            CentMin[A]          = std::min(CentMin[A], Cent);
            CentMax[A]          = std::max(CentMax[A], Cent);
        }
    }

    const uint32_t CellIndex = static_cast<uint32_t>(TlasCells_.size());
    TlasCells_.push_back(Cell);

    if (Count <= 1u)
    {
        TlasCells_[CellIndex].LeftOrFirst  = Indices[Begin];
        TlasCells_[CellIndex].RightOrCount = 1u;
        return CellIndex;
    }

    int Axis = 0;
    for (int A = 1; A < 3; ++A)
    {
        if ((CentMax[A] - CentMin[A]) > (CentMax[Axis] - CentMin[Axis]))
        {
            Axis = A;
        }
    }

    const uint32_t Mid = Begin + Count / 2u;
    std::nth_element(Indices.begin() + Begin,
                     Indices.begin() + Mid,
                     Indices.begin() + Begin + Count,
                     [&](uint32_t Lhs, uint32_t Rhs) {
                         const float CL = Instances_[Lhs].WorldLower[Axis] + Instances_[Lhs].WorldUpper[Axis];
                         const float CR = Instances_[Rhs].WorldLower[Axis] + Instances_[Rhs].WorldUpper[Axis];
                         return CL < CR;
                     });

    const uint32_t LeftChild           = BuildTlasRecursive(Indices, Begin, Mid - Begin);
    const uint32_t RightChild          = BuildTlasRecursive(Indices, Mid, Begin + Count - Mid);
    TlasCells_[CellIndex].LeftOrFirst  = LeftChild;
    TlasCells_[CellIndex].RightOrCount = RightChild | InteriorCellFlagBit;
    return CellIndex;
}

bool InstancedBoundingScene::RebuildInstanceHierarchy(std::span<const VolumeInstancePlacement> Placements)
{
    Instances_.clear();
    TlasCells_.clear();
    ActivePlacements_.clear();
    if (Prototypes_.empty() || Placements.empty())
    {
        return false;
    }

    ActivePlacements_.assign(Placements.begin(), Placements.end());
    Instances_.reserve(Placements.size());
    for (const VolumeInstancePlacement& Spec : Placements)
    {
        if (Spec.PrototypeIndex >= Prototypes_.size())
        {
            continue;
        }
        const PrototypeSlice& Proto = Prototypes_[Spec.PrototypeIndex];
        if (Proto.Atlas.Empty())
        {
            continue;
        }

        ResolvedInstance Inst{};
        Inst.Placement     = Spec;
        Inst.WorldToObject = InvertAffineTransform3x4(Spec.ObjectToWorld);
        Inst.WideRoot      = Proto.GlobalWideCellOffset;

        const std::array<float, 3> Lo = Proto.Atlas.LocalLowerBounds();
        const std::array<float, 3> Hi = Proto.Atlas.LocalUpperBounds();
        Inst.WorldLower               = {1.0e30f, 1.0e30f, 1.0e30f};
        Inst.WorldUpper               = {-1.0e30f, -1.0e30f, -1.0e30f};
        for (int Mask = 0; Mask < 8; ++Mask)
        {
            const std::array<float, 3> Corner = {
                (Mask & 1) ? Hi[0] : Lo[0],
                (Mask & 2) ? Hi[1] : Lo[1],
                (Mask & 4) ? Hi[2] : Lo[2],
            };
            const std::array<float, 3> W = TransformAffinePoint(Spec.ObjectToWorld, Corner);
            for (int A = 0; A < 3; ++A)
            {
                Inst.WorldLower[A] = std::min(Inst.WorldLower[A], W[A]);
                Inst.WorldUpper[A] = std::max(Inst.WorldUpper[A], W[A]);
            }
        }
        Instances_.push_back(Inst);
    }

    if (Instances_.empty())
    {
        return false;
    }

    std::vector<uint32_t> Order(Instances_.size());
    std::iota(Order.begin(), Order.end(), 0u);
    TlasCells_.reserve(Instances_.size() * 2u);
    (void)BuildTlasRecursive(Order, 0u, static_cast<uint32_t>(Order.size()));
    return !TlasCells_.empty();
}

bool InstancedBoundingScene::BuildSinglePrototypeScene(const BoundingVolumeAtlas& SingleAtlas)
{
    Clear();
    if (SingleAtlas.Empty())
    {
        return false;
    }
    const uint32_t          ProtoIdx = RegisterPrototype(SingleAtlas);
    VolumeInstancePlacement IdentityPlacement{};
    IdentityPlacement.PrototypeIndex = ProtoIdx;
    return RebuildInstanceHierarchy(std::span<const VolumeInstancePlacement>(&IdentityPlacement, 1u));
}

void InstancedBoundingScene::PackSceneForGpu(std::vector<std::array<float, 4>>& OutTlasCellFloat4Table,
                                             std::vector<std::array<float, 4>>& OutInstanceFloat4Table,
                                             std::vector<std::array<float, 4>>& OutWideCellFloat4Table,
                                             std::vector<std::array<float, 4>>& OutWideTriangleFloat4Table) const
{
    OutTlasCellFloat4Table.clear();
    OutInstanceFloat4Table.clear();

    if (TlasCells_.empty() || Instances_.empty())
    {
        OutTlasCellFloat4Table.resize(2u, {0.0f, 0.0f, 0.0f, 0.0f});
        OutInstanceFloat4Table.resize(12u, {0.0f, 0.0f, 0.0f, 0.0f});
        OutWideCellFloat4Table.resize(5u, {0.0f, 0.0f, 0.0f, 0.0f});
        OutWideTriangleFloat4Table.resize(6u, {0.0f, 0.0f, 0.0f, 0.0f});
        return;
    }

    OutTlasCellFloat4Table.reserve(TlasCells_.size() * 2u);
    for (const EnclosureCell& C : TlasCells_)
    {
        OutTlasCellFloat4Table.push_back({C.LowerBounds[0], C.LowerBounds[1], C.LowerBounds[2], std::bit_cast<float>(C.LeftOrFirst)});
        OutTlasCellFloat4Table.push_back({C.UpperBounds[0], C.UpperBounds[1], C.UpperBounds[2], std::bit_cast<float>(C.RightOrCount)});
    }

    // 12 × float4 per instance: WorldToObject[3], ObjectToWorld[3], PrevObjectToWorld[3], BoundsMin+Root, BoundsMax+Override, GlassOptics
    OutInstanceFloat4Table.reserve(Instances_.size() * 12u);
    for (const ResolvedInstance& Inst : Instances_)
    {
        OutInstanceFloat4Table.push_back(Inst.WorldToObject.Row0);
        OutInstanceFloat4Table.push_back(Inst.WorldToObject.Row1);
        OutInstanceFloat4Table.push_back(Inst.WorldToObject.Row2);
        OutInstanceFloat4Table.push_back(Inst.Placement.ObjectToWorld.Row0);
        OutInstanceFloat4Table.push_back(Inst.Placement.ObjectToWorld.Row1);
        OutInstanceFloat4Table.push_back(Inst.Placement.ObjectToWorld.Row2);
        OutInstanceFloat4Table.push_back(Inst.Placement.PreviousObjectToWorld.Row0);
        OutInstanceFloat4Table.push_back(Inst.Placement.PreviousObjectToWorld.Row1);
        OutInstanceFloat4Table.push_back(Inst.Placement.PreviousObjectToWorld.Row2);
        OutInstanceFloat4Table.push_back({Inst.WorldLower[0], Inst.WorldLower[1], Inst.WorldLower[2],
                                          std::bit_cast<float>(Inst.WideRoot)});

        const uint32_t OverrideCode = (Inst.Placement.ClassificationOverride == UINT32_MAX)
                                          ? 0x7FFFFFFFu
                                          : (Inst.Placement.ClassificationOverride & 0x7FFFFFFFu);
        const uint32_t PackedFlags  = OverrideCode | (Inst.Placement.ForceTransmissiveGlass ? 0x80000000u : 0u);
        OutInstanceFloat4Table.push_back({Inst.WorldUpper[0], Inst.WorldUpper[1], Inst.WorldUpper[2],
                                          std::bit_cast<float>(PackedFlags)});
        OutInstanceFloat4Table.push_back({Inst.Placement.GlassAbsorptionPerM[0],
                                          Inst.Placement.GlassAbsorptionPerM[1],
                                          Inst.Placement.GlassAbsorptionPerM[2],
                                          Inst.Placement.GlassRefractiveIndex});
    }

    if (StaticTablesDirty_)
    {
        PackedWideCellTable_.clear();
        PackedWideTriTable_.clear();
        PackedWideCellTable_.reserve(std::max<size_t>(5u, TotalWideCellCount_ * 5u));
        PackedWideTriTable_.reserve(std::max<size_t>(6u, TotalTriangleCount_ * 6u));

        for (const PrototypeSlice& Slice : Prototypes_)
        {
            for (const CompressedWideCell& W : Slice.Atlas.WideCells())
            {
                const uint32_t GlobalChildBase = W.ChildBaseIndex + Slice.GlobalWideCellOffset;
                const uint32_t GlobalPrimBase  = W.PrimitiveBaseIndex + Slice.GlobalTriangleOffset;
                PackedWideCellTable_.push_back({W.ParentOrigin[0], W.ParentOrigin[1], W.ParentOrigin[2],
                                                std::bit_cast<float>(W.PackedExponentsMask)});
                PackedWideCellTable_.push_back({std::bit_cast<float>(GlobalChildBase),
                                                std::bit_cast<float>(GlobalPrimBase),
                                                std::bit_cast<float>(W.ChildMetaLow),
                                                std::bit_cast<float>(W.ChildMetaHigh)});
                PackedWideCellTable_.push_back({std::bit_cast<float>(W.QuantMinXLow), std::bit_cast<float>(W.QuantMinXHigh),
                                                std::bit_cast<float>(W.QuantMaxXLow), std::bit_cast<float>(W.QuantMaxXHigh)});
                PackedWideCellTable_.push_back({std::bit_cast<float>(W.QuantMinYLow), std::bit_cast<float>(W.QuantMinYHigh),
                                                std::bit_cast<float>(W.QuantMaxYLow), std::bit_cast<float>(W.QuantMaxYHigh)});
                PackedWideCellTable_.push_back({std::bit_cast<float>(W.QuantMinZLow), std::bit_cast<float>(W.QuantMinZHigh),
                                                std::bit_cast<float>(W.QuantMaxZLow), std::bit_cast<float>(W.QuantMaxZHigh)});
            }

            for (const PackedRayTriangle& T : Slice.Atlas.WideTriangles())
            {
                const uint32_t PackedClassAndGlass = (T.ClassificationId & 0x7FFFFFFFu) | (T.TransmissiveGlass ? 0x80000000u : 0u);
                PackedWideTriTable_.push_back({T.Vertex0[0], T.Vertex0[1], T.Vertex0[2], T.Uv0U});
                PackedWideTriTable_.push_back({T.Edge1[0], T.Edge1[1], T.Edge1[2], T.Uv0V});
                PackedWideTriTable_.push_back({T.Edge2[0], T.Edge2[1], T.Edge2[2], std::bit_cast<float>(PackedClassAndGlass)});
                PackedWideTriTable_.push_back({T.Uv1[0], T.Uv1[1], T.Uv2[0], T.Uv2[1]});
                PackedWideTriTable_.push_back({T.AbsorptionPerM[0], T.AbsorptionPerM[1], T.AbsorptionPerM[2], T.RefractiveIndex});
                PackedWideTriTable_.push_back({std::bit_cast<float>(T.OctVertexNormals[0]),
                                               std::bit_cast<float>(T.OctVertexNormals[1]),
                                               std::bit_cast<float>(T.OctVertexNormals[2]),
                                               T.SurfaceAreaM2});
            }
        }
        StaticTablesDirty_ = false;
    }

    OutWideCellFloat4Table     = PackedWideCellTable_;
    OutWideTriangleFloat4Table = PackedWideTriTable_;
}

void InstancedBoundingScene::BuildEmissiveFacetDistribution(std::span<const EmissiveSlotRadiance> SlotEmissions,
                                                            std::vector<std::array<float, 4>>&    OutEmissiveFacetFloat4Table,
                                                            uint32_t&                             OutEmissiveFacetCount,
                                                            float&                                OutTotalFluxWatts)
{
    EmissiveFacets_.clear();
    PublicEmissiveFacets_.clear();
    OutEmissiveFacetFloat4Table.clear();
    OutEmissiveFacetCount = 0u;
    OutTotalFluxWatts     = 0.0f;

    std::vector<float> Powers;
    double             FluxSum = 0.0;

    for (const ResolvedInstance& Inst : Instances_)
    {
        const PrototypeSlice& Proto = Prototypes_[Inst.Placement.PrototypeIndex];
        for (const PackedRayTriangle& T : Proto.Atlas.WideTriangles())
        {
            const uint32_t ClassId = (Inst.Placement.ClassificationOverride != UINT32_MAX)
                                         ? Inst.Placement.ClassificationOverride
                                         : T.ClassificationId;
            if (ClassId >= SlotEmissions.size())
            {
                continue;
            }
            const std::array<float, 3>& Rad = SlotEmissions[ClassId].RadianceRgb;
            const float                 Lum = 0.2126f * Rad[0] + 0.7152f * Rad[1] + 0.0722f * Rad[2];
            if (Lum <= 1.0e-3f)
            {
                continue;
            }

            const std::array<float, 3> W0 = TransformAffinePoint(Inst.Placement.ObjectToWorld, T.Vertex0);
            const std::array<float, 3> W1 = TransformAffinePoint(
                Inst.Placement.ObjectToWorld,
                {T.Vertex0[0] + T.Edge1[0], T.Vertex0[1] + T.Edge1[1], T.Vertex0[2] + T.Edge1[2]});
            const std::array<float, 3> W2 = TransformAffinePoint(
                Inst.Placement.ObjectToWorld,
                {T.Vertex0[0] + T.Edge2[0], T.Vertex0[1] + T.Edge2[1], T.Vertex0[2] + T.Edge2[2]});

            const std::array<float, 3> WE1      = {W1[0] - W0[0], W1[1] - W0[1], W1[2] - W0[2]};
            const std::array<float, 3> WE2      = {W2[0] - W0[0], W2[1] - W0[1], W2[2] - W0[2]};
            const std::array<float, 3> CrossPrd = CrossVec3(WE1, WE2);
            const float                Area     = 0.5f * std::sqrt(DotVec3(CrossPrd, CrossPrd));
            if (Area <= 1.0e-7f)
            {
                continue;
            }

            const float PowerWatts = PiConstant * Area * Lum;
            FluxSum += static_cast<double>(PowerWatts);

            EmissiveFacetEntry Entry{};
            Entry.WorldV0  = W0;
            Entry.WorldE1  = WE1;
            Entry.WorldE2  = WE2;
            Entry.Radiance = Rad;
            Entry.AreaM2   = Area;
            EmissiveFacets_.push_back(Entry);
            Powers.push_back(PowerWatts);
        }
    }

    if (EmissiveFacets_.empty() || FluxSum <= 1.0e-9)
    {
        EmissiveFacets_.clear();
        OutEmissiveFacetFloat4Table.resize(4u, {0.0f, 0.0f, 0.0f, 0.0f});
        return;
    }

    OutTotalFluxWatts  = static_cast<float>(FluxSum);
    double RunningFlux = 0.0;
    OutEmissiveFacetFloat4Table.reserve(EmissiveFacets_.size() * 4u);

    for (size_t I = 0u; I < EmissiveFacets_.size(); ++I)
    {
        EmissiveFacetEntry& E = EmissiveFacets_[I];
        const double        P = static_cast<double>(Powers[I]) / FluxSum;
        E.CdfLower            = static_cast<float>(RunningFlux);
        RunningFlux           = (I + 1u == EmissiveFacets_.size()) ? 1.0 : std::min(1.0, RunningFlux + P);
        E.CdfUpper            = static_cast<float>(RunningFlux);
        E.SelectionProb       = static_cast<float>(P);

        EmissiveFacetRecord Public{};
        Public.WorldVertex0  = E.WorldV0;
        Public.WorldEdge1    = E.WorldE1;
        Public.WorldEdge2    = E.WorldE2;
        Public.RadianceRgb   = E.Radiance;
        Public.AreaM2        = E.AreaM2;
        Public.SelectionProb = E.SelectionProb;
        Public.CdfUpper      = E.CdfUpper;
        PublicEmissiveFacets_.push_back(Public);

        OutEmissiveFacetFloat4Table.push_back({E.WorldV0[0], E.WorldV0[1], E.WorldV0[2], E.AreaM2});
        OutEmissiveFacetFloat4Table.push_back({E.WorldE1[0], E.WorldE1[1], E.WorldE1[2], E.CdfLower});
        OutEmissiveFacetFloat4Table.push_back({E.WorldE2[0], E.WorldE2[1], E.WorldE2[2], E.CdfUpper});
        OutEmissiveFacetFloat4Table.push_back({E.Radiance[0], E.Radiance[1], E.Radiance[2], E.SelectionProb});
    }

    OutEmissiveFacetCount = static_cast<uint32_t>(EmissiveFacets_.size());
}

SampledEmissiveSurfacePoint InstancedBoundingScene::SampleEmissiveSurface(float SelectorU, float BaryU, float BaryV) const noexcept
{
    SampledEmissiveSurfacePoint Out{};
    if (EmissiveFacets_.empty())
    {
        return Out;
    }

    const float U       = std::clamp(SelectorU, 0.0f, 0.9999999f);
    size_t      Lo      = 0u;
    size_t      Hi      = EmissiveFacets_.size() - 1u;
    while (Lo < Hi)
    {
        const size_t Mid = Lo + (Hi - Lo) / 2u;
        if (U >= EmissiveFacets_[Mid].CdfUpper)
        {
            Lo = Mid + 1u;
        }
        else
        {
            Hi = Mid;
        }
    }

    const EmissiveFacetEntry& E     = EmissiveFacets_[Lo];
    const float               RootU = std::sqrt(std::clamp(BaryU, 0.0f, 1.0f));
    const float               TriU  = 1.0f - RootU;
    const float               TriV  = std::clamp(BaryV, 0.0f, 1.0f) * RootU;

    Out.Valid              = true;
    Out.SelectedFacetIndex = static_cast<uint32_t>(Lo);
    Out.WorldPosition      = {
        E.WorldV0[0] + TriU * E.WorldE1[0] + TriV * E.WorldE2[0],
        E.WorldV0[1] + TriU * E.WorldE1[1] + TriV * E.WorldE2[1],
        E.WorldV0[2] + TriU * E.WorldE1[2] + TriV * E.WorldE2[2],
    };
    Out.WorldNormal       = NormalizeVec3(CrossVec3(E.WorldE1, E.WorldE2));
    Out.EmittedRadiance   = E.Radiance;
    Out.AreaPdfPerSquareM = E.SelectionProb / std::max(E.AreaM2, 1.0e-7f);
    return Out;
}

SoftwareRayHit InstancedBoundingScene::TraceClosestRay(std::array<float, 3> OriginMeters,
                                                       std::array<float, 3> DirectionUnit,
                                                       float                MinDistanceMeters,
                                                       float                MaxDistanceMeters,
                                                       bool                 IncludeGlass) const noexcept
{
    SoftwareRayHit BestHit{};
    if (TlasCells_.empty() || Instances_.empty())
    {
        return BestHit;
    }

    const std::array<float, 3> InvDir = SafeReciprocalDirection(DirectionUnit);
    float                      BestT  = MaxDistanceMeters;

    std::array<uint32_t, 64> Stack{};
    uint32_t                 StackTop  = 0u;
    float                    RootEntry = 0.0f;
    if (IntersectSlabAabb(TlasCells_[0].LowerBounds, TlasCells_[0].UpperBounds, OriginMeters, InvDir, MinDistanceMeters, BestT, RootEntry))
    {
        Stack[StackTop++] = 0u;
    }

    while (StackTop > 0u)
    {
        const EnclosureCell& Cell = TlasCells_[Stack[--StackTop]];
        if ((Cell.RightOrCount & InteriorCellFlagBit) != 0u)
        {
            const uint32_t LeftIdx  = Cell.LeftOrFirst;
            const uint32_t RightIdx = Cell.RightOrCount & ~InteriorCellFlagBit;
            float          EntryL = 0.0f, EntryR = 0.0f;
            const bool     HitL = IntersectSlabAabb(TlasCells_[LeftIdx].LowerBounds, TlasCells_[LeftIdx].UpperBounds, OriginMeters, InvDir, MinDistanceMeters, BestT, EntryL);
            const bool     HitR = IntersectSlabAabb(TlasCells_[RightIdx].LowerBounds, TlasCells_[RightIdx].UpperBounds, OriginMeters, InvDir, MinDistanceMeters, BestT, EntryR);
            if (HitL && HitR)
            {
                if (EntryL < EntryR)
                {
                    if (StackTop + 2u <= Stack.size())
                    {
                        Stack[StackTop++] = RightIdx;
                        Stack[StackTop++] = LeftIdx;
                    }
                }
                else
                {
                    if (StackTop + 2u <= Stack.size())
                    {
                        Stack[StackTop++] = LeftIdx;
                        Stack[StackTop++] = RightIdx;
                    }
                }
            }
            else if (HitL && StackTop + 1u <= Stack.size())
            {
                Stack[StackTop++] = LeftIdx;
            }
            else if (HitR && StackTop + 1u <= Stack.size())
            {
                Stack[StackTop++] = RightIdx;
            }
        }
        else
        {
            const uint32_t          InstIdx = Cell.LeftOrFirst;
            const ResolvedInstance& Inst    = Instances_[InstIdx];
            if (!IncludeGlass && Inst.Placement.ForceTransmissiveGlass)
            {
                continue;
            }
            const PrototypeSlice&      Proto      = Prototypes_[Inst.Placement.PrototypeIndex];
            const std::array<float, 3> LocalO     = TransformAffinePoint(Inst.WorldToObject, OriginMeters);
            const std::array<float, 3> LocalD     = TransformAffineVector(Inst.WorldToObject, DirectionUnit);
            const float                DirScale   = std::sqrt(std::max(DotVec3(LocalD, LocalD), 1.0e-12f));
            const std::array<float, 3> LocalDUnit = {LocalD[0] / DirScale, LocalD[1] / DirScale, LocalD[2] / DirScale};

            const SoftwareRayHit LocalHit = Proto.Atlas.TraceWideClosestRay(
                LocalO, LocalDUnit, MinDistanceMeters * DirScale, BestT * DirScale, IncludeGlass);

            if (LocalHit.Hit)
            {
                const bool FinalGlass = Inst.Placement.ForceTransmissiveGlass || LocalHit.TransmissiveGlass;
                if (!IncludeGlass && FinalGlass)
                {
                    continue;
                }
                const float WorldT = LocalHit.DistanceMeters / DirScale;
                if (WorldT >= MinDistanceMeters && WorldT < BestT)
                {
                    BestT                   = WorldT;
                    BestHit                 = LocalHit;
                    BestHit.DistanceMeters  = WorldT;
                    BestHit.InstanceIndex   = InstIdx;
                    BestHit.TriangleIndex   = Proto.GlobalTriangleOffset + LocalHit.TriangleIndex;
                    BestHit.WorldPosition   = TransformAffinePoint(Inst.Placement.ObjectToWorld, LocalHit.WorldPosition);
                    BestHit.PreviousWorldPosition = TransformAffinePoint(Inst.Placement.PreviousObjectToWorld, LocalHit.WorldPosition);

                    // Inverse-transpose normal transform: (M^-1)^T * N_local
                    const auto XformNormal = [&](std::array<float, 3> N) -> std::array<float, 3> {
                        std::array<float, 3> Wn = NormalizeVec3({
                            Inst.WorldToObject.Row0[0] * N[0] + Inst.WorldToObject.Row1[0] * N[1] + Inst.WorldToObject.Row2[0] * N[2],
                            Inst.WorldToObject.Row0[1] * N[0] + Inst.WorldToObject.Row1[1] * N[1] + Inst.WorldToObject.Row2[1] * N[2],
                            Inst.WorldToObject.Row0[2] * N[0] + Inst.WorldToObject.Row1[2] * N[1] + Inst.WorldToObject.Row2[2] * N[2],
                        });
                        if (DotVec3(Wn, DirectionUnit) > 0.0f)
                        {
                            Wn = {-Wn[0], -Wn[1], -Wn[2]};
                        }
                        return Wn;
                    };
                    BestHit.GeometricNormal = XformNormal(LocalHit.GeometricNormal);
                    BestHit.ShadingNormal   = XformNormal(LocalHit.ShadingNormal);
                    if (Inst.Placement.ClassificationOverride != UINT32_MAX)
                    {
                        BestHit.ClassificationId = Inst.Placement.ClassificationOverride;
                    }
                    BestHit.TransmissiveGlass = FinalGlass;
                    if (Inst.Placement.ForceTransmissiveGlass)
                    {
                        BestHit.RefractiveIndex = Inst.Placement.GlassRefractiveIndex;
                        BestHit.AbsorptionPerM  = Inst.Placement.GlassAbsorptionPerM;
                    }
                }
            }
        }
    }

    return BestHit;
}

std::array<float, 3> InstancedBoundingScene::TraceShadowTransmittance(std::array<float, 3> OriginMeters,
                                                                      std::array<float, 3> DirectionUnit,
                                                                      float                MaxDistanceMeters) const noexcept
{
    if (TlasCells_.empty() || Instances_.empty())
    {
        return {1.0f, 1.0f, 1.0f};
    }

    const std::array<float, 3> InvDir = SafeReciprocalDirection(DirectionUnit);
    std::array<float, 3>       Trans  = {1.0f, 1.0f, 1.0f};

    std::array<uint32_t, 64> Stack{};
    uint32_t                 StackTop  = 0u;
    float                    RootEntry = 0.0f;
    if (IntersectSlabAabb(TlasCells_[0].LowerBounds, TlasCells_[0].UpperBounds, OriginMeters, InvDir, 1.5e-3f, MaxDistanceMeters, RootEntry))
    {
        Stack[StackTop++] = 0u;
    }

    while (StackTop > 0u)
    {
        const EnclosureCell& Cell = TlasCells_[Stack[--StackTop]];
        if ((Cell.RightOrCount & InteriorCellFlagBit) != 0u)
        {
            const uint32_t LeftIdx  = Cell.LeftOrFirst;
            const uint32_t RightIdx = Cell.RightOrCount & ~InteriorCellFlagBit;
            float          EntryL = 0.0f, EntryR = 0.0f;
            if (IntersectSlabAabb(TlasCells_[LeftIdx].LowerBounds, TlasCells_[LeftIdx].UpperBounds, OriginMeters, InvDir, 1.5e-3f, MaxDistanceMeters, EntryL) &&
                StackTop < Stack.size())
            {
                Stack[StackTop++] = LeftIdx;
            }
            if (IntersectSlabAabb(TlasCells_[RightIdx].LowerBounds, TlasCells_[RightIdx].UpperBounds, OriginMeters, InvDir, 1.5e-3f, MaxDistanceMeters, EntryR) &&
                StackTop < Stack.size())
            {
                Stack[StackTop++] = RightIdx;
            }
        }
        else
        {
            const ResolvedInstance&    Inst       = Instances_[Cell.LeftOrFirst];
            const PrototypeSlice&      Proto      = Prototypes_[Inst.Placement.PrototypeIndex];
            const std::array<float, 3> LocalO     = TransformAffinePoint(Inst.WorldToObject, OriginMeters);
            const std::array<float, 3> LocalD     = TransformAffineVector(Inst.WorldToObject, DirectionUnit);
            const float                DirScale   = std::sqrt(std::max(DotVec3(LocalD, LocalD), 1.0e-12f));
            const std::array<float, 3> LocalDUnit = {LocalD[0] / DirScale, LocalD[1] / DirScale, LocalD[2] / DirScale};
            if (Inst.Placement.ForceTransmissiveGlass)
            {
                const SoftwareRayHit GlassHit = Proto.Atlas.TraceWideClosestRay(
                    LocalO, LocalDUnit, 1.5e-3f * DirScale, MaxDistanceMeters * DirScale, true);
                if (GlassHit.Hit)
                {
                    const float F0 = std::pow((Inst.Placement.GlassRefractiveIndex - 1.0f) /
                                              (Inst.Placement.GlassRefractiveIndex + 1.0f), 2.0f);
                    const float Tr = (1.0f - F0);
                    Trans[0] *= Tr * std::exp(-Inst.Placement.GlassAbsorptionPerM[0] * 0.015f);
                    Trans[1] *= Tr * std::exp(-Inst.Placement.GlassAbsorptionPerM[1] * 0.015f);
                    Trans[2] *= Tr * std::exp(-Inst.Placement.GlassAbsorptionPerM[2] * 0.015f);
                }
            }
            else
            {
                const std::array<float, 3> SubTrans = Proto.Atlas.TraceShadowTransmittance(LocalO, LocalDUnit, MaxDistanceMeters * DirScale);
                Trans[0] *= SubTrans[0];
                Trans[1] *= SubTrans[1];
                Trans[2] *= SubTrans[2];
            }
            if (Trans[0] <= 1.0e-4f && Trans[1] <= 1.0e-4f && Trans[2] <= 1.0e-4f)
            {
                return {0.0f, 0.0f, 0.0f};
            }
        }
    }
    return Trans;
}

} // namespace Frontier
