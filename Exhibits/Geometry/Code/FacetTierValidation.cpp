/*====================================================================================================================================
                                                      FACETTIERVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Headless validation for the C++ virtualized-geometry build (step A1): icosphere + heightfield corpora through
//    weld / classification / cluster partition, invariants asserted, numbers compared with the HTML reference.

#include "Engine/Geometry/FacetTierBuild.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <utility>

using namespace Frontier;

static Vector3 NormalizedV(const Vector3& P)
{
    double L = std::sqrt(P.X * P.X + P.Y * P.Y + P.Z * P.Z);
    if (L == 0.0) L = 1.0;
    return { P.X / L, P.Y / L, P.Z / L };
}

// 📝 Subdivided icosahedron facet soup — same construction as the HTML corpus (subdivisions 4 → 5120 facets).
static FacetField ConstructSphere(int Subdivisions)
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

// 📝 Heightfield facet soup — same height function as the HTML corpus (rolling relief + one hard cliff step).
static FacetField ConstructHeightfield(int Resolution, double Extent)
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

static int Failures = 0;
static void Check(bool Condition, const char* Label)
{
    std::printf("  [%s] %s\n", Condition ? "PASS" : "FAIL", Label);
    if (!Condition) ++Failures;
}

static void RunCorpus(const char* Name, FacetField Field, uint32_t ExpectedFacets)
{
    std::printf("%s: facets=%zu (expected %u)\n", Name, Field.Classification.size(), ExpectedFacets);
    Check(Field.Classification.size() == ExpectedFacets, "facet count matches HTML corpus");
    WeldSpecification Specification;
    Specification.UseCreaseAngle = true;
    Specification.CreaseCosine = std::cos(30.0 * 3.14159265358979323846 / 180.0);
    const WeldOutcome Weld = WeldFacetField(Field, Specification);
    const auto Clusters = PartitionFacetClusters(Weld.Complex, 128);
    const auto Problems = ValidateClusterBuild(Weld.Complex, Clusters, 128);
    for (const auto& Problem : Problems) std::printf("  problem: %s\n", Problem.c_str());
    Check(Problems.empty(), "partition invariants (coverage/cap/connectivity/purity/bounds)");
    std::printf("  welded vertices=%zu clusters=%zu sharp=%u uv-seams=%u boundary=%u\n",
                Weld.Complex.Positions.size(), Clusters.size(), Weld.SharpEdgeCount,
                Weld.UvSeamEdgeCount, Weld.BoundaryEdgeCount);
}

int main()
{
    RunCorpus("sphere(4)", ConstructSphere(4), 5120);
    RunCorpus("heightfield(64)", ConstructHeightfield(64, 40.0), 64 * 64 * 2);
    std::printf(Failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", Failures);
    return Failures ? 1 : 0;
}
