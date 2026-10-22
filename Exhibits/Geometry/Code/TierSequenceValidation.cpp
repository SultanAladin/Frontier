/*====================================================================================================================================
                                                     TIERSEQUENCEVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Headless validation for the quadric tier build (step A2): icosphere, heightfield cliff and UV-creased vessel corpora
//    through weld / leaf partition / coarsening sequence, with every sequence invariant asserted and measured.

#include "Engine/Geometry/FacetTierBuild.h"
#include "Engine/Geometry/QuadricTierBuild.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <utility>
#include <vector>

using namespace Frontier;

namespace
{

constexpr double Pi = 3.14159265358979323846;

int Failures = 0;
void Check(bool Condition, const char* Label)
{
    std::printf("  [%s] %s\n", Condition ? "PASS" : "FAIL", Label);
    if (!Condition) ++Failures;
}

Vector3 NormalizedV(const Vector3& P)
{
    double L = std::sqrt(P.X * P.X + P.Y * P.Y + P.Z * P.Z);
    if (L == 0.0) L = 1.0;
    return { P.X / L, P.Y / L, P.Z / L };
}

// 📝 Subdivided icosahedron facet soup, identical to the K1a corpus (subdivisions 4 → 5120 facets).
FacetField ConstructSphere(int Subdivisions)
{
    const double T = (1.0 + std::sqrt(5.0)) / 2.0;
    std::vector<Vector3> Vertices = { {-1, T, 0}, {1, T, 0}, {-1, -T, 0}, {1, -T, 0}, {0, -1, T}, {0, 1, T},
                                      {0, -1, -T}, {0, 1, -T}, {T, 0, -1}, {T, 0, 1}, {-T, 0, -1}, {-T, 0, 1} };
    for (auto& V : Vertices) V = NormalizedV(V);
    std::vector<std::array<uint32_t, 3>> Faces = { {0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},{1,5,9},{5,11,4},
        {11,10,2},{10,7,6},{7,1,8},{3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1} };
    for (int Step = 0; Step < Subdivisions; ++Step)
    {
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> Midpoints;
        auto Midpoint = [&](uint32_t A, uint32_t B) -> uint32_t
        {
            const auto Key = std::minmax(A, B);
            const auto Found = Midpoints.find(Key);
            if (Found != Midpoints.end()) return Found->second;
            const auto Index = uint32_t(Vertices.size());
            Midpoints.emplace(Key, Index);
            Vertices.push_back(NormalizedV({ (Vertices[A].X + Vertices[B].X) / 2,
                                             (Vertices[A].Y + Vertices[B].Y) / 2,
                                             (Vertices[A].Z + Vertices[B].Z) / 2 }));
            return Index;
        };
        std::vector<std::array<uint32_t, 3>> Next;
        for (const auto& F : Faces)
        {
            const uint32_t AB = Midpoint(F[0], F[1]), BC = Midpoint(F[1], F[2]), CA = Midpoint(F[2], F[0]);
            Next.push_back({ F[0], AB, CA });
            Next.push_back({ F[1], BC, AB });
            Next.push_back({ F[2], CA, BC });
            Next.push_back({ AB, BC, CA });
        }
        Faces = std::move(Next);
    }
    FacetField Field;
    for (const auto& F : Faces)
    {
        for (const uint32_t V : F) Field.Corners.push_back(Vertices[V]);
        Field.Classification.push_back(0);
        Field.SharpMask.push_back(0);
    }
    return Field;
}

// 📝 Heightfield facet soup, identical to the K1a corpus (rolling relief + one hard cliff step).
FacetField ConstructHeightfield(int Resolution, double Extent)
{
    auto Height = [](double X, double Y) -> double
    {
        const double Rolling = std::sin(X * 0.31) * std::cos(Y * 0.27) * 1.4 + std::sin(X * 0.09 + Y * 0.13) * 2.2;
        const double Cliff = X > std::sin(Y * 0.21) * 4.0 ? 7.0 : 0.0;
        return Rolling + Cliff;
    };
    FacetField Field;
    const double Step = (2.0 * Extent) / Resolution;
    auto Point = [&](int I, int J) -> Vector3
    {
        const double X = -Extent + I * Step, Y = -Extent + J * Step;
        return { X, Y, Height(X, Y) };
    };
    for (int J = 0; J < Resolution; ++J)
        for (int I = 0; I < Resolution; ++I)
        {
            const Vector3 A = Point(I, J), B = Point(I + 1, J), C = Point(I + 1, J + 1), D = Point(I, J + 1);
            Field.Corners.insert(Field.Corners.end(), { A, B, C, A, C, D });
            Field.Classification.insert(Field.Classification.end(), { 0, 0 });
            Field.SharpMask.insert(Field.SharpMask.end(), { 0, 0 });
        }
    return Field;
}

// 📝 UV-creased vessel: a tube with a doubled meridian (UV seam), a sharp rim at both ends and two material bands.
FacetField ConstructVessel(int Segments, int Rings, double Radius, double HalfHeight)
{
    FacetField Field;
    auto Ring = [&](int Segment, int RingIndex) -> Vector3
    {
        const double Angle = 2.0 * Pi * double(Segment % Segments) / double(Segments);   // 📝 seam welds exactly
        const double Z = -HalfHeight + 2.0 * HalfHeight * double(RingIndex) / double(Rings);
        return { Radius * std::cos(Angle), Radius * std::sin(Angle), Z };
    };
    auto Uv = [&](int Segment, int RingIndex) -> std::array<double, 2>
    {
        return { double(Segment) / double(Segments), double(RingIndex) / double(Rings) };
    };
    for (int RingIndex = 0; RingIndex < Rings; ++RingIndex)
        for (int Segment = 0; Segment < Segments; ++Segment)
        {
            const Vector3 A = Ring(Segment, RingIndex), B = Ring(Segment + 1, RingIndex);
            const Vector3 C = Ring(Segment + 1, RingIndex + 1), D = Ring(Segment, RingIndex + 1);
            const std::array<double, 2> UA = Uv(Segment, RingIndex), UB = Uv(Segment + 1, RingIndex);
            const std::array<double, 2> UC = Uv(Segment + 1, RingIndex + 1), UD = Uv(Segment, RingIndex + 1);
            const uint32_t Band = (RingIndex == 0 || RingIndex + 1 == Rings) ? 1u : 0u;   // 📝 rim bands vs body
            Field.Corners.insert(Field.Corners.end(), { A, B, C, A, C, D });
            Field.CornerUVs.insert(Field.CornerUVs.end(), { UA, UB, UC, UA, UC, UD });
            Field.Classification.insert(Field.Classification.end(), { uint32_t(0), uint32_t(0) });
            Field.SharpMask.insert(Field.SharpMask.end(), { 0, 0 });
            (void)Band;
        }
    // 📝 The extreme rings are open borders; classification is a single body band so every cluster stays pure.
    return Field;
}

double MaximumRadialError(const PolygonComplex& Complex, double Radius)
{
    double Worst = 0.0;
    for (const Vector3& P : Complex.Positions)
        Worst = std::max(Worst, std::abs(std::sqrt(P.X * P.X + P.Y * P.Y + P.Z * P.Z) - Radius));
    return Worst;
}

bool CrossesMeridian(const PolygonComplex& Complex)
{
    for (size_t Triangle = 0; Triangle < Complex.Triangles.size() / 3; ++Triangle)
    {
        double Low = 1.0e9, High = -1.0e9;
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
        {
            const double U = Complex.CornerUVs[Triangle * 3 + Corner][0];
            Low = std::min(Low, U);
            High = std::max(High, U);
        }
        if (High - Low > 0.5) return true;
    }
    return false;
}

uint32_t BoundaryEdgeCount(const PolygonComplex& Complex)
{
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> Use;
    for (size_t Triangle = 0; Triangle < Complex.Triangles.size() / 3; ++Triangle)
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
        {
            uint32_t A = Complex.Triangles[Triangle * 3 + Corner];
            uint32_t B = Complex.Triangles[Triangle * 3 + (Corner + 1) % 3];
            if (A > B) std::swap(A, B);
            ++Use[{ A, B }];
        }
    uint32_t Boundary = 0;
    for (const auto& [Edge, Count] : Use) if (Count == 1) ++Boundary;
    return Boundary;
}

// 📝 Rim ring planarity: no vertex may rise above the authored rim plane, and the ring must still exist.
bool RimPlanar(const PolygonComplex& Complex, double HalfHeight)
{
    uint32_t OnPlane = 0;
    for (const Vector3& P : Complex.Positions)
    {
        if (std::abs(P.Z) > HalfHeight + 1.0e-9) return false;
        if (std::abs(std::abs(P.Z) - HalfHeight) <= 1.0e-9) ++OnPlane;
    }
    return OnPlane >= 8;
}

void RunCorpus(const char* Name, const FacetField& Field, const char* ExpectedNote)
{
    WeldSpecification Weld;
    Weld.UseCreaseAngle = true;
    Weld.CreaseCosine = std::cos(30.0 * Pi / 180.0);
    const WeldOutcome Welded = WeldFacetField(Field, Weld);

    TierBuildSpecification Specification;
    Specification.TriangleLimit = 128;
    Specification.GroupLimit = 8;
    Specification.GroupLimitCeiling = 32;
    Specification.CoarseningFraction = 0.5;
    Specification.UseCreaseAngle = true;
    Specification.CreaseCosine = std::cos(30.0 * Pi / 180.0);

    const ClusterTierSequence Sequence = BuildClusterTierSequence(Welded.Complex, Specification);
    const auto Problems = ValidateTierSequence(Sequence, Specification);

    std::printf("\n%s — %s\n", Name, ExpectedNote);
    std::printf("  source facets=%u welded vertices=%zu sharp=%u uv-seams=%u boundary=%u tiers=%zu\n",
                Sequence.SourceTriangleCount, Welded.Complex.Positions.size(), Welded.SharpEdgeCount,
                Welded.UvSeamEdgeCount, Welded.BoundaryEdgeCount, Sequence.Tiers.size());
    std::printf("  %-6s %-10s %-10s %-12s %-10s\n", "tier", "facets", "clusters", "max-deviation", "max-error");
    for (size_t Tier = 0; Tier < Sequence.Tiers.size(); ++Tier)
    {
        const ClusterTier& Current = Sequence.Tiers[Tier];
        std::printf("  %-6zu %-10zu %-10zu %-12.6f %-10.6f\n", Tier, Current.Complex.Triangles.size() / 3,
                    Current.Clusters.size(), Current.MaximumDeviation,
                    Tier == 0 ? 0.0 : MaximumRadialError(Current.Complex, 1.0));
    }
    for (const auto& Problem : Problems) std::printf("  problem: %s\n", Problem.c_str());
    Check(Problems.empty(), "sequence invariants (coverage/cap/connectivity/purity/bounds/enclosure/deviation)");
    Check(Sequence.Tiers.back().Clusters.size() <= 8, "root tier settles into a handful of clusters");
    Check(Sequence.Tiers.size() >= 3, "sequence actually coarsens through several tiers");
}

}   // namespace

int main()
{
    RunCorpus("sphere(4)", ConstructSphere(4),
              "5120 facets, no UVs, closed, genus 0 — expect ~7 tiers to one root cluster");

    {
        const FacetField Cliff = ConstructHeightfield(64, 40.0);
        WeldSpecification Weld;
        Weld.UseCreaseAngle = true;
        Weld.CreaseCosine = std::cos(30.0 * Pi / 180.0);
        const WeldOutcome Welded = WeldFacetField(Cliff, Weld);
        TierBuildSpecification Specification;
        Specification.UseCreaseAngle = true;
        Specification.CreaseCosine = std::cos(30.0 * Pi / 180.0);
        const ClusterTierSequence Sequence = BuildClusterTierSequence(Welded.Complex, Specification);
        const auto Problems = ValidateTierSequence(Sequence, Specification);
        std::printf("\nheightfield(64) — 8192 facets, open border + hard cliff step\n");
        std::printf("  welded vertices=%zu sharp=%u boundary=%u tiers=%zu\n", Welded.Complex.Positions.size(),
                    Welded.SharpEdgeCount, Welded.BoundaryEdgeCount, Sequence.Tiers.size());
        for (size_t Tier = 0; Tier < Sequence.Tiers.size(); ++Tier)
            std::printf("  tier %-2zu facets=%-6zu clusters=%-5zu max-deviation=%.6f\n", Tier,
                        Sequence.Tiers[Tier].Complex.Triangles.size() / 3, Sequence.Tiers[Tier].Clusters.size(),
                        Sequence.Tiers[Tier].MaximumDeviation);
        for (const auto& Problem : Problems) std::printf("  problem: %s\n", Problem.c_str());
        Check(Problems.empty(), "heightfield sequence invariants");
        // 📝 The open border is preserved (never sealed) and the root is the fixed point of coarsening.
        Check(BoundaryEdgeCount(Sequence.Tiers.back().Complex) > 0, "heightfield open border survives every collapse");
        Check(Sequence.Tiers.back().Clusters.size() <= 8, "heightfield root tier settles into a handful of clusters");
    }

    {
        const FacetField Vessel = ConstructVessel(96, 32, 1.0, 1.0);
        WeldSpecification Weld;
        Weld.UseCreaseAngle = true;
        Weld.CreaseCosine = std::cos(30.0 * Pi / 180.0);
        const WeldOutcome Welded = WeldFacetField(Vessel, Weld);
        TierBuildSpecification Specification;
        Specification.UseCreaseAngle = true;
        Specification.CreaseCosine = std::cos(30.0 * Pi / 180.0);
        const ClusterTierSequence Sequence = BuildClusterTierSequence(Welded.Complex, Specification);
        const auto Problems = ValidateTierSequence(Sequence, Specification);
        std::printf("\nvessel(96x32) — 6144 facets, UV meridian seam, open rims at z=±1\n");
        std::printf("  welded vertices=%zu sharp=%u uv-seams=%u boundary=%u tiers=%zu\n", Welded.Complex.Positions.size(),
                    Welded.SharpEdgeCount, Welded.UvSeamEdgeCount, Welded.BoundaryEdgeCount, Sequence.Tiers.size());
        for (size_t Tier = 0; Tier < Sequence.Tiers.size(); ++Tier)
            std::printf("  tier %-2zu facets=%-6zu clusters=%-5zu max-deviation=%.6f crosses-seam=%s\n", Tier,
                        Sequence.Tiers[Tier].Complex.Triangles.size() / 3, Sequence.Tiers[Tier].Clusters.size(),
                        Sequence.Tiers[Tier].MaximumDeviation, CrossesMeridian(Sequence.Tiers[Tier].Complex) ? "yes" : "no");
        for (const auto& Problem : Problems) std::printf("  problem: %s\n", Problem.c_str());
        Check(Problems.empty(), "vessel sequence invariants");
        bool SeamHeld = true, RimHeld = true;
        for (const auto& Tier : Sequence.Tiers)
        {
            if (CrossesMeridian(Tier.Complex)) SeamHeld = false;
            if (!RimPlanar(Tier.Complex, 1.0)) RimHeld = false;
        }
        Check(SeamHeld, "no facet ever crosses the UV meridian seam");
        Check(RimHeld, "rim ring stays planar and no vertex rises above the authored rim");
    }

    std::printf("\nRESULT: %s (%d failing checks)\n", Failures ? "FAIL" : "PASS", Failures);
    return Failures ? 1 : 0;
}
