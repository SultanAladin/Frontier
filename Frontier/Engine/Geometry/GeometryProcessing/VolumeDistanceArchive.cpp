/*==========================================================================================================================================
                                            VOLUME DISTANCE ARCHIVE DEFINITION
==========================================================================================================================================*/
// 🧩 Headless .facet -> .sdfspace baker, IEEE-754 Float16 serializer, standalone content resolver, & world SDF union composer.

#include "Engine/Geometry/GeometryProcessing/VolumeDistanceArchive.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <tuple>

namespace Frontier
{
namespace
{

struct SdfSpaceHeaderV1
{
    char     Magic[4]        = {'S', 'D', 'F', '1'};
    uint32_t Version         = 1u;
    uint32_t DimX            = 0u;
    uint32_t DimY            = 0u;
    uint32_t DimZ            = 0u;
    uint32_t Flags           = 0u; // bit 0 = Signed, bit 1 = ClosedManifold, bit 2 = Float16Payload
    float    Minimum[3]      = {0.0f, 0.0f, 0.0f};
    float    Maximum[3]      = {0.0f, 0.0f, 0.0f};
    float    Step[3]         = {0.0f, 0.0f, 0.0f};
    float    MinimumDistance = 0.0f;
    float    MaximumDistance = 0.0f;
};
static_assert(sizeof(SdfSpaceHeaderV1) == 68u);

uint16_t EncodeFloat16(float Value) noexcept
{
    const uint32_t Bits  = std::bit_cast<uint32_t>(Value);
    const uint32_t Sign  = (Bits >> 16u) & 0x8000u;
    const int32_t  Exp32 = static_cast<int32_t>((Bits >> 23u) & 0xFFu) - 127 + 15;
    uint32_t       Mant  = Bits & 0x007FFFFFu;

    if (Exp32 <= 0)
    {
        if (Exp32 < -10)
        {
            return static_cast<uint16_t>(Sign);
        }
        Mant = (Mant | 0x00800000u) >> static_cast<uint32_t>(1 - Exp32);
        return static_cast<uint16_t>(Sign | ((Mant + 0x00001000u) >> 13u));
    }
    if (Exp32 >= 31)
    {
        return static_cast<uint16_t>(Sign | 0x7BFFu);
    }
    const uint32_t Rounded = Mant + 0x00001000u;
    if ((Rounded & 0x00800000u) != 0u)
    {
        const int32_t NextExp = Exp32 + 1;
        return (NextExp >= 31) ? static_cast<uint16_t>(Sign | 0x7BFFu)
                               : static_cast<uint16_t>(Sign | (static_cast<uint32_t>(NextExp) << 10u));
    }
    return static_cast<uint16_t>(Sign | (static_cast<uint32_t>(Exp32) << 10u) | (Rounded >> 13u));
}

float DecodeFloat16(uint16_t HalfBits) noexcept
{
    const uint32_t Sign = static_cast<uint32_t>(HalfBits & 0x8000u) << 16u;
    const uint32_t Exp  = (HalfBits >> 10u) & 0x1Fu;
    const uint32_t Mant = HalfBits & 0x03FFu;

    if (Exp == 0u)
    {
        if (Mant == 0u)
        {
            return std::bit_cast<float>(Sign);
        }
        uint32_t NormMant = Mant;
        int32_t  Exp32    = 127 - 15 + 1;
        while ((NormMant & 0x0400u) == 0u)
        {
            NormMant <<= 1u;
            --Exp32;
        }
        NormMant &= 0x03FFu;
        const uint32_t OutBits = Sign | (static_cast<uint32_t>(Exp32) << 23u) | (NormMant << 13u);
        return std::bit_cast<float>(OutBits);
    }
    if (Exp == 31u)
    {
        const uint32_t OutBits = Sign | 0x7F800000u | (Mant << 13u);
        return std::bit_cast<float>(OutBits);
    }
    const uint32_t OutBits = Sign | ((Exp + 127u - 15u) << 23u) | (Mant << 13u);
    return std::bit_cast<float>(OutBits);
}

std::filesystem::path ResolveSdfArchivePath(const std::string& PathOrContentRel)
{
    namespace fs = std::filesystem;
    if (PathOrContentRel.empty())
    {
        return {};
    }
    const fs::path Direct(PathOrContentRel);
    std::error_code Ec;
    if (fs::is_regular_file(Direct, Ec))
    {
        return Direct;
    }

    auto StripPrefix = [](std::string S) {
        const std::string Prefix = "EngineContent/";
        if (S.rfind(Prefix, 0) == 0)
        {
            S.erase(0, Prefix.size());
        }
        return S;
    };
    const fs::path Rel = StripPrefix(PathOrContentRel);

    if (const char* EnvProj = std::getenv("FRONTIER_PROJECT_CONTENT_DIR"); EnvProj != nullptr && EnvProj[0] != '\0')
    {
        const fs::path Candidate = fs::path(EnvProj) / Rel;
        if (fs::is_regular_file(Candidate, Ec))
        {
            return Candidate;
        }
    }

    fs::path Cursor = fs::current_path(Ec);
    for (int Depth = 0; Depth < 8 && !Cursor.empty(); ++Depth)
    {
        const fs::path StagedEverest = Cursor / "Projects" / "Everest" / "Content" / Rel;
        if (fs::is_regular_file(StagedEverest, Ec))
        {
            return StagedEverest;
        }
        const fs::path LocalContent = Cursor / "Content" / Rel;
        if (fs::is_regular_file(LocalContent, Ec))
        {
            return LocalContent;
        }
        const fs::path RepoEngine = Cursor / "EngineContent" / Rel;
        if (fs::is_regular_file(RepoEngine, Ec))
        {
            return RepoEngine;
        }
        const fs::path Parent = Cursor.parent_path();
        if (Parent == Cursor)
        {
            break;
        }
        Cursor = Parent;
    }
    return {};
}

} // namespace

VolumeDistanceField BakeFacetFieldDistanceVolume(const FacetField& SourceFacets, const HeadlessSdfBakeOptions& Options)
{
    const size_t TriCount = SourceFacets.Corners.size() / 3u;
    BakePart     Part{};
    Part.Name = "FacetArchiveSurface";
    Part.Indices.reserve(TriCount * 3u);

    std::map<std::tuple<int64_t, int64_t, int64_t>, uint32_t> WeldedMap;

    for (size_t TriIdx = 0u; TriIdx < TriCount; ++TriIdx)
    {
        const uint32_t ClassId =
            (TriIdx < SourceFacets.Classification.size()) ? SourceFacets.Classification[TriIdx] : 0u;
        if (Options.SkipGlass && (ClassId == 7u || ClassId == 12u))
        {
            continue;
        }

        for (size_t Corner = 0u; Corner < 3u; ++Corner)
        {
            const FacetPoint3D& Pt = SourceFacets.Corners[TriIdx * 3u + Corner];
            const auto          Key = std::make_tuple(static_cast<int64_t>(std::llround(Pt.X * 1.0e6)),
                                                      static_cast<int64_t>(std::llround(Pt.Y * 1.0e6)),
                                                      static_cast<int64_t>(std::llround(Pt.Z * 1.0e6)));
            auto [It, Inserted] = WeldedMap.emplace(Key, static_cast<uint32_t>(Part.Vertices.size()));
            if (Inserted)
            {
                BakeVertex V{};
                V.Position[0] = static_cast<float>(Pt.X);
                V.Position[1] = static_cast<float>(Pt.Y);
                V.Position[2] = static_cast<float>(Pt.Z);
                Part.Vertices.push_back(V);
            }
            Part.Indices.push_back(It->second);
        }
    }

    BakeMeshSet MeshSet{};
    MeshSet.Label  = "HeadlessFacetBake";
    MeshSet.Source = "FacetField";
    MeshSet.Parts.push_back(std::move(Part));

    return BakeVolumeDistanceField(
        MeshSet, Options.Dimensions, Options.PaddingRatio, Options.SignedField, Options.NarrowBandM);
}

bool SaveVolumeDistanceSpace(const VolumeDistanceField&   Field,
                             const std::filesystem::path& OutputPath,
                             std::string*                 OutDiagnostic)
{
    const size_t VoxelCount =
        static_cast<size_t>(Field.Dimensions[0]) * Field.Dimensions[1] * Field.Dimensions[2];
    if (VoxelCount == 0u || Field.Distances.size() != VoxelCount)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "SaveVolumeDistanceSpace: invalid field dimensions or distance count.";
        }
        return false;
    }

    std::error_code Ec;
    if (OutputPath.has_parent_path())
    {
        std::filesystem::create_directories(OutputPath.parent_path(), Ec);
    }

    SdfSpaceHeaderV1 Header{};
    Header.DimX            = Field.Dimensions[0];
    Header.DimY            = Field.Dimensions[1];
    Header.DimZ            = Field.Dimensions[2];
    Header.Flags           = (Field.Signed ? 1u : 0u) | (Field.ClosedManifold ? 2u : 0u) | 4u;
    Header.Minimum[0]      = Field.Minimum[0];
    Header.Minimum[1]      = Field.Minimum[1];
    Header.Minimum[2]      = Field.Minimum[2];
    Header.Maximum[0]      = Field.Maximum[0];
    Header.Maximum[1]      = Field.Maximum[1];
    Header.Maximum[2]      = Field.Maximum[2];
    Header.Step[0]         = Field.Step[0];
    Header.Step[1]         = Field.Step[1];
    Header.Step[2]         = Field.Step[2];
    Header.MinimumDistance = Field.MinimumDistance;
    Header.MaximumDistance = Field.MaximumDistance;

    std::vector<uint16_t> HalfPayload(VoxelCount, 0u);
    for (size_t Idx = 0u; Idx < VoxelCount; ++Idx)
    {
        HalfPayload[Idx] = EncodeFloat16(Field.Distances[Idx]);
    }

    std::ofstream File(OutputPath, std::ios::binary | std::ios::trunc);
    if (!File)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "SaveVolumeDistanceSpace: failed to open output path " + OutputPath.string();
        }
        return false;
    }
    File.write(reinterpret_cast<const char*>(&Header), sizeof(Header));
    File.write(reinterpret_cast<const char*>(HalfPayload.data()),
               static_cast<std::streamsize>(HalfPayload.size() * sizeof(uint16_t)));
    File.flush();
    return static_cast<bool>(File);
}

bool LoadVolumeDistanceSpaceFromMemory(std::span<const uint8_t> Bytes,
                                       VolumeDistanceField&     OutField,
                                       std::string*             OutDiagnostic)
{
    if (Bytes.size() < sizeof(SdfSpaceHeaderV1))
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "LoadVolumeDistanceSpace: truncated header.";
        }
        return false;
    }
    SdfSpaceHeaderV1 Header{};
    std::memcpy(&Header, Bytes.data(), sizeof(Header));
    if (std::memcmp(Header.Magic, "SDF1", 4u) != 0 || Header.Version != 1u ||
        Header.DimX == 0u || Header.DimY == 0u || Header.DimZ == 0u)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "LoadVolumeDistanceSpace: invalid SDF1 header.";
        }
        return false;
    }

    const size_t VoxelCount  = static_cast<size_t>(Header.DimX) * Header.DimY * Header.DimZ;
    const bool   IsHalf      = (Header.Flags & 4u) != 0u;
    const size_t PayloadBytes = VoxelCount * (IsHalf ? sizeof(uint16_t) : sizeof(float));
    if (Bytes.size() < sizeof(SdfSpaceHeaderV1) + PayloadBytes)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "LoadVolumeDistanceSpace: truncated voxel payload.";
        }
        return false;
    }

    OutField                 = {};
    OutField.Dimensions      = {Header.DimX, Header.DimY, Header.DimZ};
    OutField.Minimum         = {Header.Minimum[0], Header.Minimum[1], Header.Minimum[2]};
    OutField.Maximum         = {Header.Maximum[0], Header.Maximum[1], Header.Maximum[2]};
    OutField.Step            = {Header.Step[0], Header.Step[1], Header.Step[2]};
    OutField.MinimumDistance = Header.MinimumDistance;
    OutField.MaximumDistance = Header.MaximumDistance;
    OutField.Signed          = (Header.Flags & 1u) != 0u;
    OutField.ClosedManifold  = (Header.Flags & 2u) != 0u;
    OutField.Distances.resize(VoxelCount);
    OutField.Confidence.assign(VoxelCount, 1.0f);

    const uint8_t* PayloadPtr = Bytes.data() + sizeof(SdfSpaceHeaderV1);
    if (IsHalf)
    {
        for (size_t Idx = 0u; Idx < VoxelCount; ++Idx)
        {
            uint16_t HalfWord = 0u;
            std::memcpy(&HalfWord, PayloadPtr + Idx * sizeof(uint16_t), sizeof(uint16_t));
            OutField.Distances[Idx] = DecodeFloat16(HalfWord);
        }
    }
    else
    {
        std::memcpy(OutField.Distances.data(), PayloadPtr, PayloadBytes);
    }
    return true;
}

bool LoadVolumeDistanceSpace(const std::string&   PathOrContentRel,
                             VolumeDistanceField& OutField,
                             std::string*         OutDiagnostic)
{
    const std::filesystem::path Resolved = ResolveSdfArchivePath(PathOrContentRel);
    if (Resolved.empty())
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "LoadVolumeDistanceSpace: could not locate " + PathOrContentRel;
        }
        return false;
    }
    std::ifstream File(Resolved, std::ios::binary | std::ios::ate);
    if (!File)
    {
        if (OutDiagnostic)
        {
            *OutDiagnostic = "LoadVolumeDistanceSpace: failed to read " + Resolved.string();
        }
        return false;
    }
    const auto ByteCount = File.tellg();
    if (ByteCount <= 0)
    {
        return false;
    }
    std::vector<uint8_t> Bytes(static_cast<size_t>(ByteCount));
    File.seekg(0, std::ios::beg);
    File.read(reinterpret_cast<char*>(Bytes.data()), ByteCount);
    return LoadVolumeDistanceSpaceFromMemory(Bytes, OutField, OutDiagnostic);
}

float SampleVolumeDistanceMeters(const VolumeDistanceField& Field,
                                 float                      WorldX,
                                 float                      WorldY,
                                 float                      WorldZ) noexcept
{
    const uint32_t NX = Field.Dimensions[0];
    const uint32_t NY = Field.Dimensions[1];
    const uint32_t NZ = Field.Dimensions[2];
    if (NX == 0u || NY == 0u || NZ == 0u || Field.Distances.empty())
    {
        return 1.0e6f;
    }

    // Exact Euclidean exterior distance outside the grid AABB + trilinear grid value at nearest boundary point
    const float ClampedX = std::clamp(WorldX, Field.Minimum[0], Field.Maximum[0]);
    const float ClampedY = std::clamp(WorldY, Field.Minimum[1], Field.Maximum[1]);
    const float ClampedZ = std::clamp(WorldZ, Field.Minimum[2], Field.Maximum[2]);
    const float ExX      = WorldX - ClampedX;
    const float ExY      = WorldY - ClampedY;
    const float ExZ      = WorldZ - ClampedZ;
    const float Exterior = std::sqrt(ExX * ExX + ExY * ExY + ExZ * ExZ);

    const float Gx = std::clamp((ClampedX - Field.Minimum[0]) / std::max(Field.Step[0], 1.0e-6f) - 0.5f,
                                0.0f,
                                static_cast<float>(NX - 1u));
    const float Gy = std::clamp((ClampedY - Field.Minimum[1]) / std::max(Field.Step[1], 1.0e-6f) - 0.5f,
                                0.0f,
                                static_cast<float>(NY - 1u));
    const float Gz = std::clamp((ClampedZ - Field.Minimum[2]) / std::max(Field.Step[2], 1.0e-6f) - 0.5f,
                                0.0f,
                                static_cast<float>(NZ - 1u));

    const uint32_t X0 = static_cast<uint32_t>(Gx);
    const uint32_t Y0 = static_cast<uint32_t>(Gy);
    const uint32_t Z0 = static_cast<uint32_t>(Gz);
    const uint32_t X1 = std::min(X0 + 1u, NX - 1u);
    const uint32_t Y1 = std::min(Y0 + 1u, NY - 1u);
    const uint32_t Z1 = std::min(Z0 + 1u, NZ - 1u);

    const float Tx = Gx - static_cast<float>(X0);
    const float Ty = Gy - static_cast<float>(Y0);
    const float Tz = Gz - static_cast<float>(Z0);

    auto At = [&](uint32_t X, uint32_t Y, uint32_t Z) noexcept -> float {
        return Field.Distances[(static_cast<size_t>(Z) * NY + Y) * NX + X];
    };

    const float C00 = At(X0, Y0, Z0) * (1.0f - Tx) + At(X1, Y0, Z0) * Tx;
    const float C10 = At(X0, Y1, Z0) * (1.0f - Tx) + At(X1, Y1, Z0) * Tx;
    const float C01 = At(X0, Y0, Z1) * (1.0f - Tx) + At(X1, Y0, Z1) * Tx;
    const float C11 = At(X0, Y1, Z1) * (1.0f - Tx) + At(X1, Y1, Z1) * Tx;
    const float C0  = C00 * (1.0f - Ty) + C10 * Ty;
    const float C1  = C01 * (1.0f - Ty) + C11 * Ty;
    return (C0 * (1.0f - Tz) + C1 * Tz) + Exterior;
}

std::array<float, 3> SampleVolumeGradientNormal(const VolumeDistanceField& Field,
                                                float                      WorldX,
                                                float                      WorldY,
                                                float                      WorldZ) noexcept
{
    const float Eps = std::max(0.015f, 0.5f * std::min({Field.Step[0], Field.Step[1], Field.Step[2]}));
    const float Dxp = SampleVolumeDistanceMeters(Field, WorldX + Eps, WorldY - Eps, WorldZ - Eps);
    const float Dxm = SampleVolumeDistanceMeters(Field, WorldX - Eps, WorldY - Eps, WorldZ + Eps);
    const float Dyp = SampleVolumeDistanceMeters(Field, WorldX - Eps, WorldY + Eps, WorldZ - Eps);
    const float Dzp = SampleVolumeDistanceMeters(Field, WorldX + Eps, WorldY + Eps, WorldZ + Eps);

    const float Nx  = Dxp - Dxm - Dyp + Dzp;
    const float Ny  = -Dxp - Dxm + Dyp + Dzp;
    const float Nz  = -Dxp + Dxm - Dyp + Dzp;
    const float Len = std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz);
    if (Len <= 1.0e-8f)
    {
        return {0.0f, 1.0f, 0.0f};
    }
    return {Nx / Len, Ny / Len, Nz / Len};
}

float TraceVolumeConeVisibility(const VolumeDistanceField& Field,
                                std::array<float, 3>       OriginMeters,
                                std::array<float, 3>       DirectionUnit,
                                float                      MaxDistanceMeters,
                                float                      ConeSharpness,
                                uint32_t                   MaxSteps) noexcept
{
    const float VoxelBias = std::max(0.04f, 0.85f * std::max({Field.Step[0], Field.Step[1], Field.Step[2]}));
    float       Travel    = VoxelBias * 1.35f;
    float       Vis       = 1.0f;

    for (uint32_t Step = 0u; Step < MaxSteps && Travel < MaxDistanceMeters; ++Step)
    {
        const float Px   = OriginMeters[0] + DirectionUnit[0] * Travel;
        const float Py   = OriginMeters[1] + DirectionUnit[1] * Travel;
        const float Pz   = OriginMeters[2] + DirectionUnit[2] * Travel;
        const float Dist = SampleVolumeDistanceMeters(Field, Px, Py, Pz);
        if (Dist <= 0.002f)
        {
            return 0.0f;
        }
        Vis    = std::min(Vis, std::clamp(ConeSharpness * Dist / std::max(Travel, 0.05f), 0.0f, 1.0f));
        Travel += std::max(Dist * 0.85f, VoxelBias * 0.55f);
    }
    return Vis;
}

VolumeDistanceField ComposeWorldDistanceVolume(std::span<const PlacedDistanceInstance> Instances,
                                               std::array<float, 3>                    WorldBoundsMin,
                                               std::array<float, 3>                    WorldBoundsMax,
                                               std::array<uint32_t, 3>                 Dimensions,
                                               bool                                    IncludeGroundHalfSpace,
                                               float                                   GroundPlaneY)
{
    VolumeDistanceField Out{};
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        Out.Dimensions[Axis] = std::clamp(Dimensions[Axis], 8u, 256u);
        Out.Minimum[Axis]    = WorldBoundsMin[Axis];
        Out.Maximum[Axis]    = WorldBoundsMax[Axis];
        Out.Step[Axis]       = (Out.Maximum[Axis] - Out.Minimum[Axis]) / static_cast<float>(Out.Dimensions[Axis]);
    }
    Out.Signed         = true;
    Out.ClosedManifold = true;

    const uint32_t NX    = Out.Dimensions[0];
    const uint32_t NY    = Out.Dimensions[1];
    const uint32_t NZ    = Out.Dimensions[2];
    const size_t   Count = static_cast<size_t>(NX) * NY * NZ;
    Out.Distances.assign(Count, 1.0e6f);
    Out.Confidence.assign(Count, 1.0f);
    Out.MinimumDistance = 1.0e6f;
    Out.MaximumDistance = -1.0e6f;

    struct PrecomputedInstance
    {
        const VolumeDistanceField* Archive  = nullptr;
        float                      Tx       = 0.0f;
        float                      Ty       = 0.0f;
        float                      Tz       = 0.0f;
        float                      CosNegψ  = 1.0f;
        float                      SinNegψ  = 0.0f;
        float                      Scale    = 1.0f;
        float                      InvScale = 1.0f;
    };
    std::vector<PrecomputedInstance> Prepared;
    Prepared.reserve(Instances.size());
    for (const PlacedDistanceInstance& Inst : Instances)
    {
        if (Inst.Archive == nullptr || Inst.Archive->Distances.empty())
        {
            continue;
        }
        const float Scale = static_cast<float>(std::max(1.0e-4, Inst.UniformScale));
        Prepared.push_back({
            Inst.Archive,
            static_cast<float>(Inst.TranslationMeters.X),
            static_cast<float>(Inst.TranslationMeters.Y),
            static_cast<float>(Inst.TranslationMeters.Z),
            static_cast<float>(std::cos(Inst.YawRadians)),
            static_cast<float>(std::sin(Inst.YawRadians)),
            Scale,
            1.0f / Scale,
        });
    }

    for (uint32_t Z = 0u; Z < NZ; ++Z)
    {
        const float Wz = Out.Minimum[2] + (static_cast<float>(Z) + 0.5f) * Out.Step[2];
        for (uint32_t Y = 0u; Y < NY; ++Y)
        {
            const float Wy = Out.Minimum[1] + (static_cast<float>(Y) + 0.5f) * Out.Step[1];
            for (uint32_t X = 0u; X < NX; ++X)
            {
                const float Wx   = Out.Minimum[0] + (static_cast<float>(X) + 0.5f) * Out.Step[0];
                float       Best = IncludeGroundHalfSpace ? (Wy - GroundPlaneY) : 1.0e6f;

                for (const PrecomputedInstance& P : Prepared)
                {
                    const float Rx = Wx - P.Tx;
                    const float Ry = Wy - P.Ty;
                    const float Rz = Wz - P.Tz;
                    const float Lx = (Rx * P.CosNegψ - Rz * P.SinNegψ) * P.InvScale;
                    const float Ly = Ry * P.InvScale;
                    const float Lz = (Rx * P.SinNegψ + Rz * P.CosNegψ) * P.InvScale;
                    const float D  = SampleVolumeDistanceMeters(*P.Archive, Lx, Ly, Lz) * P.Scale;
                    Best           = std::min(Best, D);
                }

                const size_t Slot       = (static_cast<size_t>(Z) * NY + Y) * NX + X;
                Out.Distances[Slot]     = Best;
                Out.MinimumDistance     = std::min(Out.MinimumDistance, Best);
                Out.MaximumDistance     = std::max(Out.MaximumDistance, Best);
            }
        }
    }
    return Out;
}

bool ComposeControlVehicleDistanceVolume(VolumeDistanceField&           OutWorldField,
                                         const ControlVehiclePlacement& Placement,
                                         std::array<uint32_t, 3>        WorldDimensions,
                                         bool                           IncludeGroundHalf,
                                         std::string*                   OutDiagnostic)
{
    VolumeDistanceField BodySdf{};
    VolumeDistanceField RimSdf{};
    VolumeDistanceField TyreSdf{};

    if (!LoadVolumeDistanceSpace(Placement.ArchiveDirectory + "/ControlVehicleBody.sdfspace", BodySdf, OutDiagnostic) ||
        !LoadVolumeDistanceSpace(Placement.ArchiveDirectory + "/ControlVehicleRim.sdfspace", RimSdf, OutDiagnostic) ||
        !LoadVolumeDistanceSpace(Placement.ArchiveDirectory + "/ControlVehicleTyre.sdfspace", TyreSdf, OutDiagnostic))
    {
        return false;
    }

    constexpr double TyreOuterRadiusM = 0.50855;
    constexpr double FrontAxleZM      = 1.665;
    constexpr double RearAxleZM       = -1.705;
    constexpr double TrackHalfXM      = 1.045;
    constexpr double π                = 3.14159265358979323846;

    const double cosψ = std::cos(Placement.HeadingYawRadians);
    const double sinψ = std::sin(Placement.HeadingYawRadians);
    auto ToWorld      = [&](double Lx, double Ly, double Lz) -> FacetPoint3D {
        return {
            Placement.WorldOriginMeters.X + (Lx * cosψ + Lz * sinψ),
            Placement.WorldOriginMeters.Y + Ly,
            Placement.WorldOriginMeters.Z + (-Lx * sinψ + Lz * cosψ),
        };
    };

    std::vector<PlacedDistanceInstance> Instances;
    Instances.reserve(9u);
    Instances.push_back({&BodySdf, ToWorld(0.0, TyreOuterRadiusM, 0.0), Placement.HeadingYawRadians, 1.0});

    struct WheelDesc
    {
        double Lx;
        double Lz;
        bool   Front;
        bool   RightSide;
    };
    const std::array<WheelDesc, 4> Wheels = {{
        {-TrackHalfXM, FrontAxleZM, true, false},
        {TrackHalfXM, FrontAxleZM, true, true},
        {-TrackHalfXM, RearAxleZM, false, false},
        {TrackHalfXM, RearAxleZM, false, true},
    }};
    for (const WheelDesc& W : Wheels)
    {
        const double Steer = W.Front ? Placement.FrontSteerRadians : 0.0;
        const double Flip  = W.RightSide ? π : 0.0;
        const auto   Pos   = ToWorld(W.Lx, TyreOuterRadiusM, W.Lz);
        Instances.push_back({&RimSdf, Pos, Placement.HeadingYawRadians + Steer + Flip, 1.0});
        Instances.push_back({&TyreSdf, Pos, Placement.HeadingYawRadians + Steer + Flip, 1.0});
    }

    const float Ox = static_cast<float>(Placement.WorldOriginMeters.X);
    const float Oy = static_cast<float>(Placement.WorldOriginMeters.Y);
    const float Oz = static_cast<float>(Placement.WorldOriginMeters.Z);
    OutWorldField  = ComposeWorldDistanceVolume(Instances,
                                                {Ox - 1.95f, Oy - 0.15f, Oz - 3.35f},
                                                {Ox + 1.95f, Oy + 2.05f, Oz + 3.35f},
                                                WorldDimensions,
                                                IncludeGroundHalf,
                                                Oy);
    return true;
}

void PackVolumeDistanceForGpu(const VolumeDistanceField&          Field,
                              std::vector<std::array<float, 4>>& OutGpuFloat4Table)
{
    OutGpuFloat4Table.clear();
    const uint32_t NX    = Field.Dimensions[0];
    const uint32_t NY    = Field.Dimensions[1];
    const uint32_t NZ    = Field.Dimensions[2];
    const size_t   Count = static_cast<size_t>(NX) * NY * NZ;
    if (Count == 0u || Field.Distances.size() != Count)
    {
        OutGpuFloat4Table.push_back({0.0f, 0.0f, 0.0f, 0.0f});
        OutGpuFloat4Table.push_back({1.0f, 1.0f, 1.0f, 0.0f});
        OutGpuFloat4Table.push_back({1.0f, 1.0f, 1.0f, 0.0f});
        OutGpuFloat4Table.push_back({1.0f, 1.0f, 1.0f, 0.0f});
        return;
    }

    const float InvSpanX = 1.0f / std::max(Field.Maximum[0] - Field.Minimum[0], 1.0e-5f);
    const float InvSpanY = 1.0f / std::max(Field.Maximum[1] - Field.Minimum[1], 1.0e-5f);
    const float InvSpanZ = 1.0f / std::max(Field.Maximum[2] - Field.Minimum[2], 1.0e-5f);

    OutGpuFloat4Table.reserve(4u + (Count + 3u) / 4u);
    OutGpuFloat4Table.push_back({Field.Minimum[0], Field.Minimum[1], Field.Minimum[2], static_cast<float>(NX)});
    OutGpuFloat4Table.push_back({Field.Maximum[0], Field.Maximum[1], Field.Maximum[2], static_cast<float>(NY)});
    OutGpuFloat4Table.push_back({Field.Step[0], Field.Step[1], Field.Step[2], static_cast<float>(NZ)});
    OutGpuFloat4Table.push_back({InvSpanX, InvSpanY, InvSpanZ, 1.0f});

    for (size_t Base = 0u; Base < Count; Base += 4u)
    {
        std::array<float, 4> Quad{1.0e6f, 1.0e6f, 1.0e6f, 1.0e6f};
        for (size_t Lane = 0u; Lane < 4u && Base + Lane < Count; ++Lane)
        {
            Quad[Lane] = Field.Distances[Base + Lane];
        }
        OutGpuFloat4Table.push_back(Quad);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                           FOUR-CASCADE CLIPMAP DISTANCE VOLUME & ANALYTICAL PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

float EvaluateAnalyticalPrimitiveDistance(const AnalyticalDistancePrimitive& Primitive,
                                          float                              WorldX,
                                          float                              WorldY,
                                          float                              WorldZ) noexcept
{
    const float Dx = WorldX - Primitive.CenterMeters[0];
    const float Dy = WorldY - Primitive.CenterMeters[1];
    const float Dz = WorldZ - Primitive.CenterMeters[2];

    if (Primitive.Category == AnalyticalDistanceCategory::Sphere)
    {
        const float Radius = std::max(Primitive.HalfExtentsMeters[0], 1.0e-4f);
        return std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz) - Radius;
    }
    if (Primitive.Category == AnalyticalDistanceCategory::Cylinder)
    {
        const float Radius     = std::max(Primitive.HalfExtentsMeters[0], 1.0e-4f);
        const float HalfHeight = std::max(Primitive.HalfExtentsMeters[1], 1.0e-4f);
        const float Radial     = std::sqrt(Dx * Dx + Dz * Dz) - Radius;
        const float Vertical   = std::abs(Dy) - HalfHeight;
        const float OutR       = std::max(Radial, 0.0f);
        const float OutV       = std::max(Vertical, 0.0f);
        return std::min(std::max(Radial, Vertical), 0.0f) + std::sqrt(OutR * OutR + OutV * OutV);
    }

    // Exact signed Axis-Aligned Box distance (Quilez)
    const float Qx   = std::abs(Dx) - std::max(Primitive.HalfExtentsMeters[0], 1.0e-4f);
    const float Qy   = std::abs(Dy) - std::max(Primitive.HalfExtentsMeters[1], 1.0e-4f);
    const float Qz   = std::abs(Dz) - std::max(Primitive.HalfExtentsMeters[2], 1.0e-4f);
    const float Ex   = std::max(Qx, 0.0f);
    const float Ey   = std::max(Qy, 0.0f);
    const float Ez   = std::max(Qz, 0.0f);
    const float Outs = std::sqrt(Ex * Ex + Ey * Ey + Ez * Ez);
    const float Ins  = std::min(std::max({Qx, Qy, Qz}), 0.0f);
    return Outs + Ins;
}

bool AssembleFourCascadeDistanceVolume(FourCascadeDistanceVolume&                   OutCascadedVolume,
                                       const ControlVehiclePlacement&               VehiclePlacement,
                                       std::span<const AnalyticalDistancePrimitive> ScenePrimitives,
                                       const FourCascadeDistanceParameters&         Parameters,
                                       std::string*                                 OutDiagnostic)
{
    VolumeDistanceField BodySdf{};
    VolumeDistanceField RimSdf{};
    VolumeDistanceField TyreSdf{};

    if (!LoadVolumeDistanceSpace(VehiclePlacement.ArchiveDirectory + "/ControlVehicleBody.sdfspace", BodySdf, OutDiagnostic) ||
        !LoadVolumeDistanceSpace(VehiclePlacement.ArchiveDirectory + "/ControlVehicleRim.sdfspace", RimSdf, OutDiagnostic) ||
        !LoadVolumeDistanceSpace(VehiclePlacement.ArchiveDirectory + "/ControlVehicleTyre.sdfspace", TyreSdf, OutDiagnostic))
    {
        return false;
    }

    constexpr double TyreOuterRadiusM = 0.50855;
    constexpr double FrontAxleZM      = 1.665;
    constexpr double RearAxleZM       = -1.705;
    constexpr double TrackHalfXM      = 1.045;
    constexpr double π                = 3.14159265358979323846;

    const double cosψ = std::cos(VehiclePlacement.HeadingYawRadians);
    const double sinψ = std::sin(VehiclePlacement.HeadingYawRadians);
    auto ToWorld      = [&](double Lx, double Ly, double Lz) -> FacetPoint3D {
        return {
            VehiclePlacement.WorldOriginMeters.X + (Lx * cosψ + Lz * sinψ),
            VehiclePlacement.WorldOriginMeters.Y + Ly,
            VehiclePlacement.WorldOriginMeters.Z + (-Lx * sinψ + Lz * cosψ),
        };
    };

    std::vector<PlacedDistanceInstance> ArchiveInstances;
    ArchiveInstances.reserve(9u);
    ArchiveInstances.push_back({&BodySdf, ToWorld(0.0, TyreOuterRadiusM, 0.0), VehiclePlacement.HeadingYawRadians, 1.0});

    struct WheelPlacementSpec
    {
        double Lx;
        double Lz;
        bool   Front;
        bool   RightSide;
    };
    const std::array<WheelPlacementSpec, 4> Wheels = {{
        {-TrackHalfXM, FrontAxleZM, true, false},
        {TrackHalfXM, FrontAxleZM, true, true},
        {-TrackHalfXM, RearAxleZM, false, false},
        {TrackHalfXM, RearAxleZM, false, true},
    }};
    for (const WheelPlacementSpec& W : Wheels)
    {
        const double Steer = W.Front ? VehiclePlacement.FrontSteerRadians : 0.0;
        const double Flip  = W.RightSide ? π : 0.0;
        const auto   Pos   = ToWorld(W.Lx, TyreOuterRadiusM, W.Lz);
        ArchiveInstances.push_back({&RimSdf, Pos, VehiclePlacement.HeadingYawRadians + Steer + Flip, 1.0});
        ArchiveInstances.push_back({&TyreSdf, Pos, VehiclePlacement.HeadingYawRadians + Steer + Flip, 1.0});
    }

    OutCascadedVolume            = FourCascadeDistanceVolume{};
    OutCascadedVolume.Parameters = Parameters;
    OutCascadedVolume.Primitives.assign(ScenePrimitives.begin(), ScenePrimitives.end());

    float PrevRadius = 1.0f;
    for (size_t CascadeIdx = 0u; CascadeIdx < 4u; ++CascadeIdx)
    {
        const float RadiusXZ = std::max(Parameters.CascadeRadiiMeters[CascadeIdx], PrevRadius + 1.0f);
        PrevRadius           = RadiusXZ;
        const float RadiusY  = std::max(2.5f, RadiusXZ * 0.45f);

        const std::array<float, 3> CascadeMin = {
            Parameters.FocusCenterMeters[0] - RadiusXZ,
            std::max(Parameters.GroundPlaneY - 0.8f, Parameters.FocusCenterMeters[1] - RadiusY),
            Parameters.FocusCenterMeters[2] - RadiusXZ,
        };
        const std::array<float, 3> CascadeMax = {
            Parameters.FocusCenterMeters[0] + RadiusXZ,
            Parameters.FocusCenterMeters[1] + RadiusY * 1.35f,
            Parameters.FocusCenterMeters[2] + RadiusXZ,
        };

        VolumeDistanceField Grid = ComposeWorldDistanceVolume(
            ArchiveInstances,
            CascadeMin,
            CascadeMax,
            Parameters.VoxelResolutionPerCascade,
            Parameters.IncludeGroundHalfSpace,
            Parameters.GroundPlaneY);

        // Bake static scene analytical primitives into coarse cascades as well so distant GI/sky cones see architecture
        if (!ScenePrimitives.empty())
        {
            const uint32_t NX = Grid.Dimensions[0];
            const uint32_t NY = Grid.Dimensions[1];
            const uint32_t NZ = Grid.Dimensions[2];
            for (uint32_t Z = 0u; Z < NZ; ++Z)
            {
                const float Wz = Grid.Minimum[2] + (static_cast<float>(Z) + 0.5f) * Grid.Step[2];
                for (uint32_t Y = 0u; Y < NY; ++Y)
                {
                    const float Wy = Grid.Minimum[1] + (static_cast<float>(Y) + 0.5f) * Grid.Step[1];
                    for (uint32_t X = 0u; X < NX; ++X)
                    {
                        const float  Wx   = Grid.Minimum[0] + (static_cast<float>(X) + 0.5f) * Grid.Step[0];
                        const size_t Slot = (static_cast<size_t>(Z) * NY + Y) * NX + X;
                        float        Best = Grid.Distances[Slot];
                        for (const AnalyticalDistancePrimitive& Prim : ScenePrimitives)
                        {
                            Best = std::min(Best, EvaluateAnalyticalPrimitiveDistance(Prim, Wx, Wy, Wz));
                        }
                        Grid.Distances[Slot] = Best;
                        Grid.MinimumDistance = std::min(Grid.MinimumDistance, Best);
                        Grid.MaximumDistance = std::max(Grid.MaximumDistance, Best);
                    }
                }
            }
        }

        OutCascadedVolume.Cascades[CascadeIdx] = std::move(Grid);
    }

    return true;
}

float SampleFourCascadeDistanceMeters(const FourCascadeDistanceVolume& Volume,
                                      float                            WorldX,
                                      float                            WorldY,
                                      float                            WorldZ) noexcept
{
    auto InsideCascade = [&](size_t Idx, float& OutNormChebyshev) noexcept -> bool {
        const VolumeDistanceField& C = Volume.Cascades[Idx];
        if (C.Distances.empty())
        {
            return false;
        }
        const float Cx = 0.5f * (C.Minimum[0] + C.Maximum[0]);
        const float Cy = 0.5f * (C.Minimum[1] + C.Maximum[1]);
        const float Cz = 0.5f * (C.Minimum[2] + C.Maximum[2]);
        const float Hx = std::max(0.5f * (C.Maximum[0] - C.Minimum[0]), 1.0e-4f);
        const float Hy = std::max(0.5f * (C.Maximum[1] - C.Minimum[1]), 1.0e-4f);
        const float Hz = std::max(0.5f * (C.Maximum[2] - C.Minimum[2]), 1.0e-4f);
        OutNormChebyshev = std::max({std::abs(WorldX - Cx) / Hx,
                                     std::abs(WorldY - Cy) / Hy,
                                     std::abs(WorldZ - Cz) / Hz});
        return OutNormChebyshev <= 1.0f;
    };

    float  GridDistance = 1.0e6f;
    size_t Selected     = 3u;
    float  NormRadius   = 1.0f;
    for (size_t C = 0u; C < 4u; ++C)
    {
        float R = 1.0f;
        if (InsideCascade(C, R))
        {
            Selected   = C;
            NormRadius = R;
            break;
        }
    }

    GridDistance = SampleVolumeDistanceMeters(Volume.Cascades[Selected], WorldX, WorldY, WorldZ);
    if (Volume.Parameters.TrilinearCascadeBlend && Selected < 3u && NormRadius > 0.78f &&
        !Volume.Cascades[Selected + 1u].Distances.empty())
    {
        const float T       = std::clamp((NormRadius - 0.78f) / 0.20f, 0.0f, 1.0f);
        const float SmoothT = T * T * (3.0f - 2.0f * T);
        const float CoarseD = SampleVolumeDistanceMeters(Volume.Cascades[Selected + 1u], WorldX, WorldY, WorldZ);
        GridDistance        = GridDistance * (1.0f - SmoothT) + CoarseD * SmoothT;
    }

    if (Volume.Parameters.IncludeGroundHalfSpace)
    {
        GridDistance = std::min(GridDistance, WorldY - Volume.Parameters.GroundPlaneY);
    }

    if (Volume.Parameters.AnalyticalPrimitives)
    {
        for (const AnalyticalDistancePrimitive& Prim : Volume.Primitives)
        {
            GridDistance = std::min(GridDistance, EvaluateAnalyticalPrimitiveDistance(Prim, WorldX, WorldY, WorldZ));
        }
    }
    return GridDistance;
}

void PackFourCascadeDistanceForGpu(const FourCascadeDistanceVolume&    Volume,
                                   std::vector<std::array<float, 4>>& OutGpuFloat4Table)
{
    OutGpuFloat4Table.clear();
    if (Volume.Cascades[0].Distances.empty())
    {
        PackVolumeDistanceForGpu(Volume.Cascades[0], OutGpuFloat4Table);
        return;
    }

    const VolumeDistanceField& C0       = Volume.Cascades[0];
    const float                InvSpanX = 1.0f / std::max(C0.Maximum[0] - C0.Minimum[0], 1.0e-5f);
    const float                InvSpanY = 1.0f / std::max(C0.Maximum[1] - C0.Minimum[1], 1.0e-5f);
    const float                InvSpanZ = 1.0f / std::max(C0.Maximum[2] - C0.Minimum[2], 1.0e-5f);

    // Slots 0..3: Primary header with Slot[3].w == 4.0f signaling 4-cascade clipmap + analytical primitives
    OutGpuFloat4Table.push_back({C0.Minimum[0], C0.Minimum[1], C0.Minimum[2], static_cast<float>(C0.Dimensions[0])});
    OutGpuFloat4Table.push_back({C0.Maximum[0], C0.Maximum[1], C0.Maximum[2], static_cast<float>(C0.Dimensions[1])});
    OutGpuFloat4Table.push_back({C0.Step[0], C0.Step[1], C0.Step[2], static_cast<float>(C0.Dimensions[2])});
    OutGpuFloat4Table.push_back({InvSpanX, InvSpanY, InvSpanZ, 4.0f});

    const uint32_t PrimCount = Volume.Parameters.AnalyticalPrimitives
                                   ? std::min<uint32_t>(static_cast<uint32_t>(Volume.Primitives.size()), 64u)
                                   : 0u;

    // Slots 4..19: 4 cascades × 4 float4 descriptors; Slot 20: global cascade + analytical control header;
    // Slots 21..(21 + 2*PrimCount - 1): analytical primitives; followed by quad-packed voxel payloads.
    uint32_t PayloadCursor = 21u + PrimCount * 2u;

    for (size_t Idx = 0u; Idx < 4u; ++Idx)
    {
        const VolumeDistanceField& C      = Volume.Cascades[Idx];
        const size_t               Voxels = static_cast<size_t>(C.Dimensions[0]) * C.Dimensions[1] * C.Dimensions[2];
        const float                Cx     = 0.5f * (C.Minimum[0] + C.Maximum[0]);
        const float                Cy     = 0.5f * (C.Minimum[1] + C.Maximum[1]);
        const float                Cz     = 0.5f * (C.Minimum[2] + C.Maximum[2]);

        OutGpuFloat4Table.push_back({C.Minimum[0], C.Minimum[1], C.Minimum[2], static_cast<float>(C.Dimensions[0])});
        OutGpuFloat4Table.push_back({C.Maximum[0], C.Maximum[1], C.Maximum[2], static_cast<float>(C.Dimensions[1])});
        OutGpuFloat4Table.push_back({C.Step[0], C.Step[1], C.Step[2], static_cast<float>(C.Dimensions[2])});
        OutGpuFloat4Table.push_back({Cx, Cy, Cz, std::bit_cast<float>(PayloadCursor)});

        PayloadCursor += static_cast<uint32_t>((Voxels + 3u) / 4u);
    }

    // Slot 20: (PrimCount, BlendEnabled ? 1 : 0, ConeStepScale, GroundPlaneFlagAndY)
    const float EncodedGround = Volume.Parameters.IncludeGroundHalfSpace ? Volume.Parameters.GroundPlaneY : -1.0e6f;
    OutGpuFloat4Table.push_back({
        static_cast<float>(PrimCount),
        Volume.Parameters.TrilinearCascadeBlend ? 1.0f : 0.0f,
        std::clamp(Volume.Parameters.ConeTraceStepScale, 0.35f, 1.25f),
        EncodedGround,
    });

    for (uint32_t P = 0u; P < PrimCount; ++P)
    {
        const AnalyticalDistancePrimitive& Prim = Volume.Primitives[P];
        float                              Code = 0.0f; // 0 = Box, 1 = Sphere, 2 = Cylinder
        if (Prim.Category == AnalyticalDistanceCategory::Sphere)
        {
            Code = 1.0f;
        }
        else if (Prim.Category == AnalyticalDistanceCategory::Cylinder)
        {
            Code = 2.0f;
        }
        OutGpuFloat4Table.push_back({Prim.CenterMeters[0], Prim.CenterMeters[1], Prim.CenterMeters[2], Code});
        OutGpuFloat4Table.push_back({Prim.HalfExtentsMeters[0],
                                     Prim.HalfExtentsMeters[1],
                                     Prim.HalfExtentsMeters[2],
                                     static_cast<float>(Volume.Parameters.MaxConeSteps)});
    }

    for (size_t Idx = 0u; Idx < 4u; ++Idx)
    {
        const VolumeDistanceField& C      = Volume.Cascades[Idx];
        const size_t               Voxels = static_cast<size_t>(C.Dimensions[0]) * C.Dimensions[1] * C.Dimensions[2];
        for (size_t Base = 0u; Base < Voxels; Base += 4u)
        {
            std::array<float, 4> Quad{1.0e6f, 1.0e6f, 1.0e6f, 1.0e6f};
            for (size_t Lane = 0u; Lane < 4u && Base + Lane < Voxels; ++Lane)
            {
                Quad[Lane] = C.Distances[Base + Lane];
            }
            OutGpuFloat4Table.push_back(Quad);
        }
    }
}

} // namespace Frontier
