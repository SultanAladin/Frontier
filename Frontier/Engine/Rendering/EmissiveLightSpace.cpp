//==========================================================================================================================================
//                                                      EMISSIVELIGHTSPACE.CPP
//==========================================================================================================================================
//  Emissive surfaces as clustered lights: triangle table, cluster instances, and the per-cell lists of
//  the eight strongest clusters. Ported 1:1 from
//  Experimental/VehicleHmiPrototype/Source/EmissiveLightSpace.js.
//==========================================================================================================================================

#include "EmissiveLightSpace.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>

namespace Frontier
{

namespace
{

constexpr uint32_t CellCount = LightClusterCellCount;

[[nodiscard]] std::array<float, 3> CellSizeVector() noexcept
{
    return { LightClusterBounds::Extent[0] / float(LightClusterBounds::Resolution[0]),
             LightClusterBounds::Extent[1] / float(LightClusterBounds::Resolution[1]),
             LightClusterBounds::Extent[2] / float(LightClusterBounds::Resolution[2]) };
}

// 📝 Inserts one candidate into a cell's list, which is kept sorted (importance descending, slot
//    ascending) and at most Capacity long; returns the new length. Insertion keeps the exact order a
//    full sort of every candidate would give, so a list built candidate by candidate is the list built
//    all at once.
uint32_t InsertCandidate(std::vector<uint16_t>& Slots,
                         std::vector<double>& Importances,
                         uint32_t Base,
                         uint32_t Count,
                         uint32_t Capacity,
                         uint16_t Slot,
                         double Importance)
{
    uint32_t Position = Count;
    while (Position > 0u
           && (Importance > Importances[Base + Position - 1u]
               || (Importance == Importances[Base + Position - 1u] && Slot < Slots[Base + Position - 1u])))
        --Position;
    if (Position >= Capacity)
        return Count;                                     // not among the Capacity strongest
    for (uint32_t K = std::min(Count, Capacity - 1u); K > Position; --K)
    {
        Slots[Base + K] = Slots[Base + K - 1u];
        Importances[Base + K] = Importances[Base + K - 1u];
    }
    Slots[Base + Position] = Slot;
    Importances[Base + Position] = Importance;
    return std::min(Count + 1u, Capacity);
}

[[nodiscard]] double Hypot3(const std::array<float, 3>& Vector) noexcept
{
    return std::sqrt(double(Vector[0]) * Vector[0] + double(Vector[1]) * Vector[1] + double(Vector[2]) * Vector[2]);
}

} // namespace

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                        PARTITIONING
//--------------------------------------------------------------------------------------------------------------------------------------

std::vector<std::vector<uint32_t>> PartitionEmissiveTriangles(std::span<const std::array<float, 3>> Centroids,
                                                              uint32_t Limit)
{
    std::vector<uint32_t> All(static_cast<size_t>(Centroids.size()));
    std::iota(All.begin(), All.end(), 0u);
    if (All.size() <= Limit)
        return { All };

    std::array<double, 3> Low = { std::numeric_limits<double>::max(),
                                  std::numeric_limits<double>::max(),
                                  std::numeric_limits<double>::max() };
    std::array<double, 3> High = { -std::numeric_limits<double>::max(),
                                   -std::numeric_limits<double>::max(),
                                   -std::numeric_limits<double>::max() };
    for (const std::array<float, 3>& Centroid : Centroids)
        for (uint32_t Axis = 0; Axis < 3u; ++Axis)
        {
            Low[Axis] = std::min(Low[Axis], double(Centroid[Axis]));
            High[Axis] = std::max(High[Axis], double(Centroid[Axis]));
        }
    const std::array<double, 3> Extent = { High[0] - Low[0], High[1] - Low[1], High[2] - Low[2] };
    const uint32_t Axis = (Extent[0] >= Extent[1] && Extent[0] >= Extent[2]) ? 0u : (Extent[1] >= Extent[2] ? 1u : 2u);

    std::vector<uint32_t> Sorted = All;
    std::sort(Sorted.begin(), Sorted.end(), [&](uint32_t A, uint32_t B)
    {
        if (Centroids[A][Axis] != Centroids[B][Axis])
            return Centroids[A][Axis] < Centroids[B][Axis];
        return A < B;
    });
    const size_t Middle = Sorted.size() >> 1u;

    std::vector<std::array<float, 3>> LowerCentroids;
    std::vector<std::array<float, 3>> UpperCentroids;
    LowerCentroids.reserve(Middle);
    UpperCentroids.reserve(Sorted.size() - Middle);
    for (size_t Index = 0; Index < Middle; ++Index)
        LowerCentroids[Index] = Centroids[Sorted[Index]];
    for (size_t Index = Middle; Index < Sorted.size(); ++Index)
        UpperCentroids[Index - Middle] = Centroids[Sorted[Index]];

    auto Lower = PartitionEmissiveTriangles(LowerCentroids, Limit);
    auto Upper = PartitionEmissiveTriangles(UpperCentroids, Limit);
    // 📝 The runs are index sets into each half's own centroid table; map them back to the input.
    std::vector<std::vector<uint32_t>> Runs;
    Runs.reserve(Lower.size() + Upper.size());
    for (std::vector<uint32_t>& Run : Lower)
    {
        for (uint32_t& Index : Run)
            Index = Sorted[Index];
        Runs.push_back(std::move(Run));
    }
    for (std::vector<uint32_t>& Run : Upper)
    {
        for (uint32_t& Index : Run)
            Index = Sorted[Middle + Index];
        Runs.push_back(std::move(Run));
    }
    return Runs;
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTRUCTION
//--------------------------------------------------------------------------------------------------------------------------------------

EmissiveLightSpace::EmissiveLightSpace()
    : Triangles(size_t(EmissiveCapacity::Triangles) * EmissiveTriangleStride, 0u),
      Clusters(size_t(EmissiveCapacity::Clusters) * EmissiveClusterStride, 0u),
      Cells(size_t(CellCount) * 4u, 0u),
      StaticCells(size_t(CellCount) * 4u, 0u),
      Touched(size_t(CellCount), 0)
{
    Static.Slot.resize(size_t(CellCount) * EmissiveCapacity::Slots, EmissiveEmptySlot);
    Static.Importance.resize(size_t(CellCount) * EmissiveCapacity::Slots, 0.0);
    Static.Count.assign(size_t(CellCount), 0);
    Static.Total.assign(size_t(CellCount), 0u);
    Work.Slot.resize(size_t(CellCount) * EmissiveCapacity::Slots, EmissiveEmptySlot);
    Work.Importance.resize(size_t(CellCount) * EmissiveCapacity::Slots, 0.0);
    Work.Count.assign(size_t(CellCount), -1);
    Work.Total.assign(size_t(CellCount), 0u);
    StaticStats.EmptyCells = CellCount;
}

uint32_t EmissiveLightSpace::Construct(std::span<const EmissiveSetSource> Sets,
                                       std::span<const std::array<float, 3>> Materials)
{
    SetClusters.clear();
    SetTriangles.clear();
    SetClusters.resize(Sets.size());
    SetTriangles.resize(Sets.size());

    uint32_t Cursor = 0u;
    uint32_t Excluded = 0u;

    for (size_t SetIndex = 0; SetIndex < Sets.size(); ++SetIndex)
    {
        const EmissiveSetSource& Set = Sets[SetIndex];
        if (Set.Facets.empty())
            continue;

        std::vector<TriangleEntry> Entries;
        Entries.reserve(Set.Facets.size());
        for (uint32_t Index = 0; Index < Set.Facets.size(); ++Index)
        {
            const EmissiveTriangleSource& Facet = Set.Facets[Index];
            if (Facet.Family >= Materials.size())
                continue;
            if (EmissiveLuminance(Materials[Facet.Family]) < double(EmissiveFloor))
                continue;

            const std::array<float, 3> E1 = { Facet.B[0] - Facet.A[0], Facet.B[1] - Facet.A[1], Facet.B[2] - Facet.A[2] };
            const std::array<float, 3> E2 = { Facet.C[0] - Facet.A[0], Facet.C[1] - Facet.A[1], Facet.C[2] - Facet.A[2] };
            const std::array<float, 3> Cross = { E1[1] * E2[2] - E1[2] * E2[1],
                                                 E1[2] * E2[0] - E1[0] * E2[2],
                                                 E1[0] * E2[1] - E1[1] * E2[0] };
            const double Area = 0.5 * Hypot3(Cross);
            if (!(Area > 1.0e-9))
                continue;

            TriangleEntry Entry;
            Entry.A = Facet.A;
            Entry.E1 = E1;
            Entry.E2 = E2;
            Entry.Area = Area;
            Entry.Family = Facet.Family;
            Entry.Bvh = Facet.BvhSlot;
            Entry.Index = Index;
            for (uint32_t Axis = 0; Axis < 3u; ++Axis)
                Entry.Centroid[Axis] = (Facet.A[Axis] + Facet.B[Axis] + Facet.C[Axis]) / 3.0f;
            Entries.push_back(Entry);
        }
        if (Entries.empty())
            continue;
        if (Cursor + Entries.size() > EmissiveCapacity::Triangles)
        {
            ++Excluded;
            continue;
        }

        std::vector<std::array<float, 3>> Centroids;
        Centroids.reserve(Entries.size());
        for (const TriangleEntry& Entry : Entries)
            Centroids.push_back(Entry.Centroid);

        std::vector<ClusterEntry> Built;
        for (const std::vector<uint32_t>& Run : PartitionEmissiveTriangles(Centroids, EmissiveClusterSize))
        {
            const uint32_t First = Cursor;
            std::array<float, 3> Low = { std::numeric_limits<float>::max(),
                                         std::numeric_limits<float>::max(),
                                         std::numeric_limits<float>::max() };
            std::array<float, 3> High = { -std::numeric_limits<float>::max(),
                                          -std::numeric_limits<float>::max(),
                                          -std::numeric_limits<float>::max() };

            for (uint32_t Local : Run)
            {
                const TriangleEntry& Entry = Entries[Local];
                const size_t Base = size_t(Cursor) * 12u;
                const std::array<float, 12> Packed = {
                    Entry.A[0], Entry.A[1], Entry.A[2], 0.0f,
                    Entry.E1[0], Entry.E1[1], Entry.E1[2], float(Entry.Area),
                    Entry.E2[0], Entry.E2[1], Entry.E2[2], 0.0f
                };
                std::memcpy(Triangles.data() + Base * sizeof(float), Packed.data(), sizeof(Packed));
                uint32_t Bvh = Entry.Bvh;
                std::memcpy(Triangles.data() + (Base + 3u) * sizeof(float), &Bvh, sizeof(Bvh));   // A.w: BVH triangle index
                uint32_t Family = Entry.Family;
                std::memcpy(Triangles.data() + (Base + 11u) * sizeof(float), &Family, sizeof(Family));

                const std::array<std::array<float, 3>, 3> Corners = {
                    Entry.A,
                    { Entry.A[0] + Entry.E1[0], Entry.A[1] + Entry.E1[1], Entry.A[2] + Entry.E1[2] },
                    { Entry.A[0] + Entry.E2[0], Entry.A[1] + Entry.E2[1], Entry.A[2] + Entry.E2[2] }
                };
                for (const std::array<float, 3>& Corner : Corners)
                    for (uint32_t Axis = 0; Axis < 3u; ++Axis)
                    {
                        Low[Axis] = std::min(Low[Axis], Corner[Axis]);
                        High[Axis] = std::max(High[Axis], Corner[Axis]);
                    }
                ++Cursor;
            }

            ClusterEntry Cluster;
            Cluster.First = First;
            Cluster.TriangleCount = static_cast<uint32_t>(Run.size());
            for (uint32_t Axis = 0; Axis < 3u; ++Axis)
                Cluster.Centre[Axis] = 0.5f * (Low[Axis] + High[Axis]);
            Cluster.Radius = float(0.5 * Hypot3({ High[0] - Low[0], High[1] - Low[1], High[2] - Low[2] }));
            Cluster.Triangles = Run;
            Built.push_back(std::move(Cluster));
        }

        SetClusters[SetIndex] = std::move(Built);
        SetTriangles[SetIndex] = std::move(Entries);
    }

    TrianglesWritten = Cursor;
    Stats.Sets = 0u;
    for (const std::vector<ClusterEntry>& Entry : SetClusters)
        if (!Entry.empty())
            ++Stats.Sets;
    Stats.Triangles = Cursor;
    Stats.ExcludedSets = Excluded;
    Signature.clear();
    Constructed = true;
    return Stats.Sets;
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                          UPDATE
//--------------------------------------------------------------------------------------------------------------------------------------

EmissiveLightSpace::WorldCluster EmissiveLightSpace::InstanceOf(const ClusterEntry& Cluster,
                                                                uint32_t Set,
                                                                const std::array<float, 16>& Transform,
                                                                std::span<const std::array<float, 3>> Materials) const
{
    const double S0 = std::sqrt(double(Transform[0]) * Transform[0] + double(Transform[1]) * Transform[1] + double(Transform[2]) * Transform[2]);
    const double S1 = std::sqrt(double(Transform[4]) * Transform[4] + double(Transform[5]) * Transform[5] + double(Transform[6]) * Transform[6]);
    const double S2 = std::sqrt(double(Transform[8]) * Transform[8] + double(Transform[9]) * Transform[9] + double(Transform[10]) * Transform[10]);
    const double Scale = std::max(std::max(S0, S1), S2);
    const double Scale2 = S0 * S1;

    WorldCluster Result;
    Result.Centre = { Transform[0] * Cluster.Centre[0] + Transform[4] * Cluster.Centre[1] + Transform[8] * Cluster.Centre[2] + Transform[12],
                      Transform[1] * Cluster.Centre[0] + Transform[5] * Cluster.Centre[1] + Transform[9] * Cluster.Centre[2] + Transform[13],
                      Transform[2] * Cluster.Centre[0] + Transform[6] * Cluster.Centre[1] + Transform[10] * Cluster.Centre[2] + Transform[14] };
    Result.Radius = Cluster.Radius * float(Scale);

    double Power = 0.0;
    for (uint32_t Local : Cluster.Triangles)
    {
        const TriangleEntry& Entry = SetTriangles[Set][Local];
        Power += Entry.Area * (Entry.Family < Materials.size() ? EmissiveLuminance(Materials[Entry.Family]) : 0.0);
    }
    Result.Power = Power * Scale2;
    return Result;
}

bool EmissiveLightSpace::Update(std::span<EmissiveOccupantSource> Occupants,
                                std::span<const std::array<float, 3>> Materials,
                                float EmissionScale)
{
    if (!Constructed)
        return false;
    MembershipDirty = false;

    // 1. which occupants are represented: active, not carrying a material override (an override changes
    //    the family of every triangle), their set has emissive clusters, and they raster.
    std::vector<uint32_t> IncludedNow;
    for (size_t Index = 0; Index < Occupants.size(); ++Index)
    {
        const EmissiveOccupantSource& Occupant = Occupants[Index];
        if (Occupant.Inactive)
            continue;
        if (Occupant.FacetSet >= SetClusters.size() || SetClusters[Occupant.FacetSet].empty())
            continue;
        if (Occupant.MaterialOverride >= 0)
            continue;
        if ((Occupant.Roles & OccupantRole::Raster) == 0u)
            continue;
        IncludedNow.push_back(static_cast<uint32_t>(Index));
    }

    std::vector<InstanceEntry> InstancesNow;
    for (uint32_t Index : IncludedNow)
        for (size_t Cluster = 0; Cluster < SetClusters[Occupants[Index].FacetSet].size(); ++Cluster)
            InstancesNow.push_back(InstanceEntry{ Index,
                                                  Occupants[Index].FacetSet,
                                                  static_cast<uint32_t>(Cluster),
                                                  (Occupants[Index].Roles & OccupantRole::Dynamic) != 0u });

    bool Changed = false;
    bool StaticDirty = false;
    if (EmissionScale != LastScale || (std::isnan(LastScale) && !std::isnan(EmissionScale)))
    {
        LastScale = EmissionScale;
        DynamicMatrices.clear();
        DynamicMatrixValid.clear();
        Changed = true;
        StaticDirty = true;
    }

    const bool OverfullNow = InstancesNow.size() > EmissiveCapacity::Clusters;
    std::string SignatureNow;
    SignatureNow.reserve(InstancesNow.size() * 12u);
    for (const InstanceEntry& Instance : InstancesNow)
    {
        SignatureNow += std::to_string(Instance.Occupant);
        SignatureNow += ':';
        SignatureNow += std::to_string(SetClusters[Instance.Set][Instance.Cluster].First);
        SignatureNow += ':';
        SignatureNow += Instance.Dynamic ? '1' : '0';
        SignatureNow += ',';
    }

    if (SignatureNow != Signature)
    {
        const std::vector<uint32_t> Before = Included;
        Signature = SignatureNow;
        Included = OverfullNow ? std::vector<uint32_t>{} : IncludedNow;
        DynamicMatrices.clear();
        DynamicMatrixValid.clear();
        Changed = true;
        StaticDirty = true;
        MembershipDirty = true;

        // 📝 Mirror the membership into the occupants' roles; the host re-uploads the occupant records
        //    when MembershipChanged is set.
        for (uint32_t Index : Before)
            if (std::find(Included.begin(), Included.end(), Index) == Included.end())
                Occupants[Index].Roles &= ~OccupantRole::EmissiveCluster;
        for (uint32_t Index : Included)
            Occupants[Index].Roles |= OccupantRole::EmissiveCluster;
    }

    Instances = OverfullNow ? std::vector<InstanceEntry>{} : InstancesNow;
    Overfull = OverfullNow;

    std::vector<uint32_t> Moved;
    DynamicMatrices.resize(Instances.size());
    DynamicMatrixValid.resize(Instances.size(), false);
    for (size_t Slot = 0; Slot < Instances.size(); ++Slot)
    {
        const std::array<float, 16>& Transform = Occupants[Instances[Slot].Occupant].Transform;
        bool Same = DynamicMatrixValid[Slot];
        if (Same)
            for (uint32_t K = 0; K < 16u; ++K)
                if (DynamicMatrices[Slot][K] != Transform[K]) { Same = false; break; }
        if (!Same)
        {
            DynamicMatrices[Slot] = Transform;
            DynamicMatrixValid[Slot] = true;
            Moved.push_back(static_cast<uint32_t>(Slot));
            if (!Instances[Slot].Dynamic)
                StaticDirty = true;
        }
    }

    World.resize(Instances.size());
    for (uint32_t Slot : Moved)
    {
        const InstanceEntry& Instance = Instances[Slot];
        World[Slot] = InstanceOf(SetClusters[Instance.Set][Instance.Cluster],
                                 Instance.Set,
                                 Occupants[Instance.Occupant].Transform,
                                 Materials);
        const WorldCluster& W = World[Slot];
        const ClusterEntry& Cluster = SetClusters[Instance.Set][Instance.Cluster];
        const size_t Base = size_t(Slot) * 8u;
        std::array<float, 4> CentreReach = { W.Centre[0], W.Centre[1], W.Centre[2], W.Radius };
        std::memcpy(Clusters.data() + Base * sizeof(float), CentreReach.data(), sizeof(CentreReach));
        std::array<uint32_t, 3> Meta = { Instance.Occupant, Cluster.First, Cluster.TriangleCount };
        std::memcpy(Clusters.data() + (Base + 4u) * sizeof(float), Meta.data(), sizeof(Meta));
        const float Power = float(W.Power * double(EmissionScale));
        std::memcpy(Clusters.data() + (Base + 7u) * sizeof(float), &Power, sizeof(Power));
    }
    if (!Moved.empty())
        Changed = true;

    ClusterCount = static_cast<uint32_t>(Instances.size());
    Stats.Instances = ClusterCount;
    if (!Changed)
        return false;

    BinIntoGrid(World, EmissionScale, StaticDirty);
    return true;
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                        CELL BINNING
//--------------------------------------------------------------------------------------------------------------------------------------

void EmissiveLightSpace::Accumulate(uint32_t Slot,
                                    const WorldCluster& W,
                                    float EmissionScale,
                                    CellLists& Target,
                                    bool Lazy)
{
    const double Power = W.Power * double(EmissionScale);
    if (!(Power > 0.0))
        return;

    const uint32_t Slots = EmissiveCapacity::Slots;
    const auto& Minimum = LightClusterBounds::Minimum;
    const auto& Resolution = LightClusterBounds::Resolution;
    const std::array<float, 3> Size = CellSizeVector();

    const double Reach = std::sqrt(Power / EmissiveReachThreshold) + double(W.Radius);
    const std::array<float, 3>& Centre = W.Centre;
    const double Radius2 = double(W.Radius) * W.Radius + 1.0e-4;

    const auto Span = [&](uint32_t Axis)
    {
        const int64_t Low = std::max<int64_t>(0, int64_t(std::floor((double(Centre[Axis]) - Reach - double(Minimum[Axis])) / double(Size[Axis]))));
        const int64_t High = std::min<int64_t>(int64_t(Resolution[Axis]) - 1,
                                               int64_t(std::floor((double(Centre[Axis]) + Reach - double(Minimum[Axis])) / double(Size[Axis]))));
        return std::pair<int64_t, int64_t>{ Low, High };
    };
    const auto XSpan = Span(0);
    const auto YSpan = Span(1);
    const auto ZSpan = Span(2);

    for (int64_t Z = ZSpan.first; Z <= ZSpan.second; ++Z)
        for (int64_t Y = YSpan.first; Y <= YSpan.second; ++Y)
            for (int64_t X = XSpan.first; X <= XSpan.second; ++X)
            {
                // 📝 The same expressions, in the same order, as the independent recomputation in
                //    EmissiveHooks: equal to the last bit.
                const double CornerX = double(Minimum[0]) + double(X) * double(Size[0]);
                const double CornerY = double(Minimum[1]) + double(Y) * double(Size[1]);
                const double CornerZ = double(Minimum[2]) + double(Z) * double(Size[2]);

                double Nearest2 = 0.0;
                double Centre2 = 0.0;
                {
                    const double Delta = double(Centre[0]) - std::min(std::max(double(Centre[0]), CornerX), CornerX + double(Size[0]));
                    Nearest2 += Delta * Delta;
                    const double Mid = double(Centre[0]) - (CornerX + 0.5 * double(Size[0]));
                    Centre2 += Mid * Mid;
                }
                {
                    const double Delta = double(Centre[1]) - std::min(std::max(double(Centre[1]), CornerY), CornerY + double(Size[1]));
                    Nearest2 += Delta * Delta;
                    const double Mid = double(Centre[1]) - (CornerY + 0.5 * double(Size[1]));
                    Centre2 += Mid * Mid;
                }
                {
                    const double Delta = double(Centre[2]) - std::min(std::max(double(Centre[2]), CornerZ), CornerZ + double(Size[2]));
                    Nearest2 += Delta * Delta;
                    const double Mid = double(Centre[2]) - (CornerZ + 0.5 * double(Size[2]));
                    Centre2 += Mid * Mid;
                }
                if (Nearest2 >= Reach * Reach)
                    continue;

                const uint32_t Cell = uint32_t(X) + Resolution[0] * (uint32_t(Y) + Resolution[1] * uint32_t(Z));
                if (Lazy && Target.Count[Cell] < 0)
                {
                    const CellLists& Cached = Static;
                    const uint32_t Base = Cell * Slots;
                    const int32_t Count = Cached.Count[Cell];
                    for (int32_t K = 0; K < Count; ++K)
                    {
                        Target.Slot[Base + uint32_t(K)] = Cached.Slot[Base + uint32_t(K)];
                        Target.Importance[Base + uint32_t(K)] = Cached.Importance[Base + uint32_t(K)];
                    }
                    Target.Count[Cell] = Count;
                    Target.Total[Cell] = Cached.Total[Cell];
                    Touched[TouchedCount++] = int32_t(Cell);
                }
                ++Target.Total[Cell];
                Target.Count[Cell] = int32_t(InsertCandidate(Target.Slot, Target.Importance, Cell * Slots,
                                                             uint32_t(Target.Count[Cell]), Slots,
                                                             uint16_t(Slot), Power / (Centre2 + Radius2)));
            }
}

void EmissiveLightSpace::WriteCell(std::vector<uint32_t>& Words, uint32_t Cell, const CellLists& Lists) const
{
    const uint32_t Slots = EmissiveCapacity::Slots;
    const uint32_t Base = Cell * Slots;
    const int32_t Count = Lists.Count[Cell];
    const auto Id = [&](uint32_t K) -> uint16_t
    {
        return K < uint32_t(Count) ? Lists.Slot[Base + K] : EmissiveEmptySlot;
    };
    for (uint32_t Word = 0; Word < 4u; ++Word)
        Words[Cell * 4u + Word] = uint32_t(Id(2u * Word)) | (uint32_t(Id(2u * Word + 1u)) << 16u);
}

void EmissiveLightSpace::BinIntoGrid(const std::vector<WorldCluster>& WorldList, float EmissionScale, bool StaticDirty)
{
    const uint32_t Slots = EmissiveCapacity::Slots;
    std::vector<uint32_t> StaticSlots;
    std::vector<uint32_t> DynamicSlots;
    for (size_t Slot = 0; Slot < Instances.size(); ++Slot)
    {
        if (Slot >= WorldList.size())
            break;
        if (Instances[Slot].Dynamic)
            DynamicSlots.push_back(uint32_t(Slot));
        else
            StaticSlots.push_back(uint32_t(Slot));
    }

    // 📝 Incremental binning. The cell lists of the instances that never move (street lamps, the tunnel,
    //    the hangar: all but the car's lamps) are built once, and only rebuilt when those change. Each
    //    frame the few MOVING instances are merged into a copy of them. The eight strongest of a union
    //    are always among (the eight strongest of one part) ∪ (the other part), so the merged lists are
    //    exactly the lists a full rebuild would give: only the work differs.
    if (StaticDirty || !StaticBuilt)
    {
        std::fill(Static.Count.begin(), Static.Count.end(), 0);
        std::fill(Static.Total.begin(), Static.Total.end(), 0u);
        for (uint32_t Slot : StaticSlots)
            Accumulate(Slot, WorldList[Slot], EmissionScale, Static, false);

        uint32_t Truncated = 0u;
        uint32_t Empty = 0u;
        for (uint32_t Cell = 0; Cell < CellCount; ++Cell)
        {
            WriteCell(StaticCells, Cell, Static);
            if (Static.Total[Cell] > Slots)
                ++Truncated;
            else if (Static.Total[Cell] == 0u)
                ++Empty;
        }
        StaticStats.TruncatedCells = Truncated;
        StaticStats.EmptyCells = Empty;
        StaticBuilt = true;
    }

    Cells = StaticCells;
    uint32_t Truncated = StaticStats.TruncatedCells;
    uint32_t Empty = StaticStats.EmptyCells;
    TouchedCount = 0u;

    if (!DynamicSlots.empty())
    {
        std::fill(Work.Count.begin(), Work.Count.end(), -1);
        for (uint32_t Slot : DynamicSlots)
            Accumulate(Slot, WorldList[Slot], EmissionScale, Work, true);

        for (uint32_t Index = 0; Index < TouchedCount; ++Index)
        {
            const uint32_t Cell = uint32_t(Touched[Index]);
            const uint32_t Before = Static.Total[Cell];
            const uint32_t After = Work.Total[Cell];
            if (Before > Slots)
                --Truncated;
            else if (Before == 0u)
                --Empty;
            if (After > Slots)
                ++Truncated;
            else if (After == 0u)
                ++Empty;
            WriteCell(Cells, Cell, Work);
        }
    }

    Stats.TruncatedCells = Truncated;
    Stats.EmptyCells = Empty;
}

std::array<uint16_t, EmissiveCapacity::Slots> EmissiveLightSpace::CellSlots(uint32_t Cell) const noexcept
{
    std::array<uint16_t, EmissiveCapacity::Slots> Result{};
    Result.fill(EmissiveEmptySlot);
    if (Cell >= CellCount)
        return Result;
    for (uint32_t Word = 0; Word < 4u; ++Word)
    {
        const uint32_t Packed = Cells[Cell * 4u + Word];
        Result[2u * Word] = uint16_t(Packed & 0xFFFFu);
        Result[2u * Word + 1u] = uint16_t(Packed >> 16u);
    }
    return Result;
}

bool EmissiveLightSpace::Represents(uint32_t OccupantIndex) const noexcept
{
    return std::find(Included.begin(), Included.end(), OccupantIndex) != Included.end();
}

} // namespace Frontier
