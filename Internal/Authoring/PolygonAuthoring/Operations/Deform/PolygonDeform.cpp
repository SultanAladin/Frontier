/*============================================================================================================================================
                                                              POLYGONDEFORM.CPP
============================================================================================================================================*/
// 🧩 The vertex-deform kernel bodies. SmoothPolygonSelection double-buffers a Laplacian relaxation over the affected vertices'
//    one-ring. AccumulateDeformSnapshot records each affected vertex's arm-time position, unit normal and a deterministic per-vertex
//    random vector (splitmix64), so ShrinkOffsetAdvance / RandomizeOffsetAdvance can re-derive the live positions idempotently while
//    the viewport drives one scalar with the mouse. None of these change topology — only Attributes.Position entries are written.

#include "PolygonDeform.h"

#include "PolygonCluster.h"
#include "VertexField.h"
#include "AdjacencyIndex.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         FILE-LOCAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double DirectionEpsilon = 1.0e-12;   // [-] - below this squared length a normal / random vector is treated as degenerate

    // The per-vertex unit normal: average the base face normals of the faces incident to BaseVertex, normalize. Falls back to world
    //    +Z when the incident normals cancel or the vertex is unreferenced, so the deform axis is never zero. (Same construction as
    //    the extrude kernel's ResolveVertexExtrudeNormal — deforms slide / jitter along this axis.)
    Vector3d ResolveVertexNormal(const AdjacencyIndex& Adjacency, uint32_t BaseVertex)
    {
        Vector3d Sum{ 0.0, 0.0, 0.0 };
        if (BaseVertex < (uint32_t)Adjacency.VertexFaces.size())
            for (uint32_t Face : Adjacency.VertexFaces[BaseVertex])
                if (Face < (uint32_t)Adjacency.FaceNormal.size())
                    Sum = AddVector(Sum, Adjacency.FaceNormal[Face]);
        if (EvaluateVectorLengthSquared(Sum) < DirectionEpsilon)
            return Vector3d{ 0.0, 0.0, 1.0 };
        return NormalizeVector(Sum);
    }

    // A deterministic 64-bit hash (splitmix64) of one 64-bit input — the standard finalizer. Used to derive a stable per-vertex
    //    pseudo-random pattern from Seed ^ vertexIndex so Randomize is repeatable (same Seed -> same jitter) with no runtime RNG.
    uint64_t HashVertexSeed(uint64_t Mixed)
    {
        Mixed += 0x9E3779B97F4A7C15ull;
        Mixed  = (Mixed ^ (Mixed >> 30)) * 0xBF58476D1CE4E5B9ull;
        Mixed  = (Mixed ^ (Mixed >> 27)) * 0x94D049BB133111EBull;
        return Mixed ^ (Mixed >> 31);
    }

    // Map one hashed 64-bit word to a double in [-1, 1] — take the top 53 bits as a [0,1) mantissa, then centre and scale.
    double HashToSignedUnit(uint64_t Hashed)
    {
        const double Fraction = (double)(Hashed >> 11) * (1.0 / 9007199254740992.0);   // [0, 1)
        return Fraction * 2.0 - 1.0;                                                    // [-1, 1)
    }

    // A stable per-vertex random vector whose unit direction is uniform-ish over the cube and whose LENGTH is a signed magnitude in
    //    [-1, 1] (the direction × that signed scalar). RandomizeOffsetAdvance uses this directly for Uniform, and recovers the signed
    //    scalar (length with the sign of the direction's dominant sum) for NormalOnly — so both variants stay deterministic from Seed.
    Vector3d ResolveRandomVector(uint32_t Seed, uint32_t VertexIndex)
    {
        const uint64_t Base = HashVertexSeed(((uint64_t)Seed << 32) ^ (uint64_t)VertexIndex);
        const double   AxisX = HashToSignedUnit(HashVertexSeed(Base ^ 0x1111111111111111ull));
        const double   AxisY = HashToSignedUnit(HashVertexSeed(Base ^ 0x2222222222222222ull));
        const double   AxisZ = HashToSignedUnit(HashVertexSeed(Base ^ 0x3333333333333333ull));
        const double   Magnitude = HashToSignedUnit(HashVertexSeed(Base ^ 0x4444444444444444ull));   // signed [-1, 1]
        Vector3d Direction{ AxisX, AxisY, AxisZ };
        if (EvaluateVectorLengthSquared(Direction) < DirectionEpsilon)
            Direction = Vector3d{ 0.0, 0.0, 1.0 };
        else
            Direction = NormalizeVector(Direction);
        return ScaleVector(Direction, Magnitude);   // unit direction * signed magnitude -> the per-vertex jitter vector
    }

    // Recover the NormalOnly signed scalar from a random vector packed by ResolveRandomVector: its length carries the magnitude, and
    //    the sign of its component sum carries the direction, so a vertex jitters in / out reproducibly without storing an extra field.
    double ResolveNormalScalar(const Vector3d& RandomVector)
    {
        const double Length = EvaluateVectorLength(RandomVector);
        const double Sign   = (RandomVector.XCoord + RandomVector.YCoord + RandomVector.ZCoord) < 0.0 ? -1.0 : 1.0;
        return Length * Sign;
    }

    // Run the double-buffered Laplacian relaxation over AffectedVertices and return the resulting position buffer (full length, only
    //    the affected entries moved). Shared by the one-shot SmoothPolygonSelection and the interactive AccumulateSmoothSnapshot (which
    //    relaxes at Factor 1.0 to bake the full-strength target the drag then blends toward). Each pass reads the previous positions and
    //    writes fresh ones, so the relaxation is order-independent; the adjacent mean spans the FULL one-ring (unmoved outside-set
    //    adjacent vertices included), matching Blender's boundary pull. A vertex with no usable adjacency holds still. Callers pre-validate.
    std::vector<Vector3d> RelaxAffectedPositions(const PolygonCluster&               Target,
                                                 const AdjacencyIndex&               BaseAdjacency,
                                                 const std::unordered_set<uint32_t>& AffectedVertices,
                                                 double                              ClampedFactor,
                                                 int                                 ClampedIterations)
    {
        const uint32_t VertexCount = (uint32_t)Target.Attributes.Position.size();
        std::vector<Vector3d> Working = Target.Attributes.Position;
        for (int Pass = 0; Pass < ClampedIterations; ++Pass)
        {
            std::vector<Vector3d> Next = Working;
            for (uint32_t Vertex : AffectedVertices)
            {
                if (Vertex >= (uint32_t)BaseAdjacency.VertexAdjacency.size())
                    continue;
                const std::vector<uint32_t>& Ring = BaseAdjacency.VertexAdjacency[Vertex];
                if (Ring.empty())
                    continue;
                Vector3d Sum{ 0.0, 0.0, 0.0 };
                uint32_t Counted = 0;
                for (uint32_t Adjacent : Ring)
                {
                    if (Adjacent >= VertexCount)
                        continue;
                    Sum = AddVector(Sum, Working[Adjacent]);
                    ++Counted;
                }
                if (Counted == 0)
                    continue;
                const Vector3d Mean  = ScaleVector(Sum, 1.0 / (double)Counted);
                const Vector3d Delta = SubtractVector(Mean, Working[Vertex]);
                Next[Vertex] = AddVector(Working[Vertex], ScaleVector(Delta, ClampedFactor));
            }
            Working.swap(Next);
        }
        return Working;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool SmoothPolygonSelection(PolygonCluster&                     Target,
                            const AdjacencyIndex&               BaseAdjacency,
                            const std::unordered_set<uint32_t>& AffectedVertices,
                            double                              Factor,
                            int                                 Iterations)
{
    const uint32_t VertexCount = (uint32_t)Target.Attributes.Position.size();
    if (AffectedVertices.empty() || VertexCount == 0)
        return false;
    for (uint32_t Vertex : AffectedVertices)
        if (Vertex >= VertexCount)
            return false;

    const double ClampedFactor     = std::min(1.0, std::max(0.0, Factor));
    const int    ClampedIterations = std::max(1, Iterations);

    const std::vector<Vector3d> Working = RelaxAffectedPositions(Target, BaseAdjacency, AffectedVertices, ClampedFactor, ClampedIterations);
    for (uint32_t Vertex : AffectedVertices)
        Target.Attributes.Position[Vertex] = Working[Vertex];
    return true;
}

bool AccumulateSmoothSnapshot(const PolygonCluster&               Target,
                              const AdjacencyIndex&               BaseAdjacency,
                              const std::unordered_set<uint32_t>& AffectedVertices,
                              int                                 Iterations,
                              DeformSnapshot&                     Snapshot)
{
    Snapshot = DeformSnapshot{};
    const uint32_t VertexCount = (uint32_t)Target.Attributes.Position.size();
    if (AffectedVertices.empty() || VertexCount == 0)
        return false;
    for (uint32_t Vertex : AffectedVertices)
        if (Vertex >= VertexCount)
            return false;

    // Bake the full-strength relaxed target once at arm (Factor 1.0, Iterations passes). The drag then blends Origin -> this target,
    //    so heavier iteration counts on the card deepen the reachable smoothing while the mouse still governs how far toward it we go.
    const int ClampedIterations = std::max(1, Iterations);
    const std::vector<Vector3d> Relaxed = RelaxAffectedPositions(Target, BaseAdjacency, AffectedVertices, 1.0, ClampedIterations);

    Snapshot.OffsetSamples.reserve(AffectedVertices.size());
    for (uint32_t Vertex : AffectedVertices)
    {
        DeformOffsetSample Sample;
        Sample.NewVertex      = Vertex;
        Sample.OriginPosition = Target.Attributes.Position[Vertex];
        Sample.SmoothedTarget = Relaxed[Vertex];
        Snapshot.OffsetSamples.push_back(Sample);
    }
    Snapshot.Completed = true;
    return true;
}

bool SmoothOffsetAdvance(PolygonCluster&                        Target,
                         const std::vector<DeformOffsetSample>& Samples,
                         double                                 Strength)
{
    const uint32_t VertexCount = (uint32_t)Target.Attributes.Position.size();
    for (const DeformOffsetSample& Sample : Samples)
        if (Sample.NewVertex >= VertexCount)
            return false;

    const double ClampedStrength = std::min(1.0, std::max(0.0, Strength));
    for (const DeformOffsetSample& Sample : Samples)
    {
        const Vector3d Delta = SubtractVector(Sample.SmoothedTarget, Sample.OriginPosition);
        Target.Attributes.Position[Sample.NewVertex] = AddVector(Sample.OriginPosition, ScaleVector(Delta, ClampedStrength));
    }
    return true;
}

bool AccumulateDeformSnapshot(const PolygonCluster&               Target,
                              const AdjacencyIndex&               BaseAdjacency,
                              const std::unordered_set<uint32_t>& AffectedVertices,
                              uint32_t                            Seed,
                              DeformSnapshot&                     Snapshot)
{
    Snapshot = DeformSnapshot{};
    const uint32_t VertexCount = (uint32_t)Target.Attributes.Position.size();
    if (AffectedVertices.empty() || VertexCount == 0)
        return false;
    for (uint32_t Vertex : AffectedVertices)
        if (Vertex >= VertexCount)
            return false;

    Snapshot.OffsetSamples.reserve(AffectedVertices.size());
    for (uint32_t Vertex : AffectedVertices)
    {
        DeformOffsetSample Sample;
        Sample.NewVertex       = Vertex;
        Sample.OriginPosition  = Target.Attributes.Position[Vertex];
        Sample.NormalDirection = ResolveVertexNormal(BaseAdjacency, Vertex);
        Sample.RandomDirection = ResolveRandomVector(Seed, Vertex);
        Snapshot.OffsetSamples.push_back(Sample);
    }
    Snapshot.Completed = true;
    return true;
}

bool ShrinkOffsetAdvance(PolygonCluster&                        Target,
                         const std::vector<DeformOffsetSample>& Samples,
                         double                                 Offset)
{
    const uint32_t VertexCount = (uint32_t)Target.Attributes.Position.size();
    for (const DeformOffsetSample& Sample : Samples)
        if (Sample.NewVertex >= VertexCount)
            return false;

    for (const DeformOffsetSample& Sample : Samples)
        Target.Attributes.Position[Sample.NewVertex] =
            AddVector(Sample.OriginPosition, ScaleVector(Sample.NormalDirection, Offset));
    return true;
}

bool RandomizeOffsetAdvance(PolygonCluster&                        Target,
                            const std::vector<DeformOffsetSample>& Samples,
                            double                                 Amount,
                            RandomizeCategory                      Category)
{
    const uint32_t VertexCount = (uint32_t)Target.Attributes.Position.size();
    for (const DeformOffsetSample& Sample : Samples)
        if (Sample.NewVertex >= VertexCount)
            return false;

    for (const DeformOffsetSample& Sample : Samples)
    {
        Vector3d Jitter;
        if (Category == RandomizeCategory::NormalOnlyCategory)
            Jitter = ScaleVector(Sample.NormalDirection, ResolveNormalScalar(Sample.RandomDirection) * Amount);
        else
            Jitter = ScaleVector(Sample.RandomDirection, Amount);
        Target.Attributes.Position[Sample.NewVertex] = AddVector(Sample.OriginPosition, Jitter);
    }
    return true;
}

} // namespace Frontier
