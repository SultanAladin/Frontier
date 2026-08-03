/*==============================================================================================================================================
                                                     GEOMETRYTREEVALIDATIONENTRY.CPP
==============================================================================================================================================*/
// 🧩 The exit gate for the CPU surface-area-heuristic tree build (the BLAS of the two-level acceleration structure). CPU only — no device, no
//    window, no Vulkan — because the builder it judges is pure C++ over local-space geometry.
//
//    🔴 THE INVARIANTS BELOW ARE STRUCTURAL, NOT STATISTICAL, AND THAT IS DELIBERATE. A tree can be badly built and still traverse: poor splits
//       cost performance and nothing else, so "the tree is worse than it should be" is not something this gate tries to judge. What it judges is
//       whether the tree is a LIE — a leaf naming primitives it does not contain, a child box escaping its parent, a primitive reachable twice or
//       not at all, a relative offset pointing outside the blob. Those are the failures that produce wrong pixels or a hung traversal, and each one
//       is silent under casual inspection. Judging with a tolerance, or checking only that the build "returned true", would hide exactly the bugs
//       this gate exists to catch.
//
//    📝 The containment check is the load-bearing one. Every primitive in a leaf's (offset, count) range must lie inside that leaf's box, AND every
//       node's box must contain both children's boxes. Together those two make the tree sound for traversal: a ray that misses a node's box has
//       genuinely missed everything beneath it. Get the partition wrong and the first check fires; get the bounds recomputation wrong and the
//       second does.

#include "Graphics/Acceleration/GeometryTreeBuild.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{

int32_t FailureTally = 0;
int32_t CheckTally   = 0;

void Report(bool Condition, const std::string& Label)
{
    ++CheckTally;
    if (!Condition)
    {
        ++FailureTally;
        std::printf("  [FAIL] %s\n", Label.c_str());
    }
}

// ─── DETERMINISTIC GEOMETRY ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// 📝 A fixed linear congruential generator rather than <random>, so a failure reproduces exactly from its seed on any machine and any standard
//    library. The gate must be able to say "seed 7 fails" and have that mean the same thing tomorrow.
struct SequenceSource
{
    uint32_t State = 1u;
};

uint32_t NextWord(SequenceSource& Source)
{
    Source.State = Source.State * 1664525u + 1013904223u;
    return Source.State;
}

float NextUnit(SequenceSource& Source)
{
    return static_cast<float>(NextWord(Source) >> 8) / static_cast<float>(1u << 24);
}

float NextRange(SequenceSource& Source, float Low, float High)
{
    return Low + NextUnit(Source) * (High - Low);
}

struct TriangleMesh
{
    std::vector<float>    Positions;
    std::vector<uint32_t> Indices;
    std::string           Label;
};

// A cloud of independent triangles scattered through a box — the general case, with no structure for the builder to exploit.
TriangleMesh MakeScatteredMesh(uint32_t TriangleCount, float Extent, uint32_t Seed, const char* Label)
{
    TriangleMesh   Mesh;
    SequenceSource Source{ Seed };

    Mesh.Label = Label;
    Mesh.Positions.reserve(static_cast<size_t>(TriangleCount) * 9);
    Mesh.Indices.reserve(static_cast<size_t>(TriangleCount) * 3);

    for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
    {
        const float CentreX = NextRange(Source, -Extent, Extent);
        const float CentreY = NextRange(Source, -Extent, Extent);
        const float CentreZ = NextRange(Source, -Extent, Extent);
        const float Radius  = NextRange(Source, Extent * 0.001f, Extent * 0.05f);

        for (uint32_t Corner = 0; Corner < 3; ++Corner)
        {
            Mesh.Positions.push_back(CentreX + NextRange(Source, -Radius, Radius));
            Mesh.Positions.push_back(CentreY + NextRange(Source, -Radius, Radius));
            Mesh.Positions.push_back(CentreZ + NextRange(Source, -Radius, Radius));
            Mesh.Indices.push_back(Triangle * 3 + Corner);
        }
    }

    return Mesh;
}

// A flat grid on the XY plane — zero extent on Z. Exercises the degenerate-axis path that a naive longest-edge seed would mishandle.
TriangleMesh MakeFlatGridMesh(uint32_t EdgeCount, const char* Label)
{
    TriangleMesh Mesh;
    Mesh.Label = Label;

    for (uint32_t Y = 0; Y <= EdgeCount; ++Y)
        for (uint32_t X = 0; X <= EdgeCount; ++X)
        {
            Mesh.Positions.push_back(static_cast<float>(X));
            Mesh.Positions.push_back(static_cast<float>(Y));
            Mesh.Positions.push_back(0.0f);
        }

    const uint32_t Stride = EdgeCount + 1;
    for (uint32_t Y = 0; Y < EdgeCount; ++Y)
        for (uint32_t X = 0; X < EdgeCount; ++X)
        {
            const uint32_t Corner = Y * Stride + X;
            Mesh.Indices.push_back(Corner);
            Mesh.Indices.push_back(Corner + 1);
            Mesh.Indices.push_back(Corner + Stride);

            Mesh.Indices.push_back(Corner + 1);
            Mesh.Indices.push_back(Corner + Stride + 1);
            Mesh.Indices.push_back(Corner + Stride);
        }

    return Mesh;
}

// 🔴 Every triangle at the SAME position. The centroid box collapses to a point on all three axes, so no split can separate anything: the SAH
//    finds nothing better than a leaf, or the partition puts everything on one side. Either way the builder must TERMINATE and produce an
//    oversized leaf rather than recurse forever on an identical range. This is the case that hangs a builder missing the SplitOffset guard.
TriangleMesh MakeCoincidentMesh(uint32_t TriangleCount, const char* Label)
{
    TriangleMesh Mesh;
    Mesh.Label = Label;

    for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
        {
            Mesh.Positions.push_back(Corner == 1 ? 1.0f : 0.0f);
            Mesh.Positions.push_back(Corner == 2 ? 1.0f : 0.0f);
            Mesh.Positions.push_back(0.0f);
            Mesh.Indices.push_back(Triangle * 3 + Corner);
        }

    return Mesh;
}

// Triangles strung along a single axis — the worst case for a builder that always splits the same way.
TriangleMesh MakeColinearMesh(uint32_t TriangleCount, const char* Label)
{
    TriangleMesh Mesh;
    Mesh.Label = Label;

    for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
    {
        const float Base = static_cast<float>(Triangle);
        const float Corners[9] = { Base, 0.0f, 0.0f,  Base + 0.5f, 0.1f, 0.0f,  Base, 0.0f, 0.1f };
        for (uint32_t Index = 0; Index < 9; ++Index) Mesh.Positions.push_back(Corners[Index]);
        for (uint32_t Corner = 0; Corner < 3; ++Corner) Mesh.Indices.push_back(Triangle * 3 + Corner);
    }

    return Mesh;
}

// ─── NODE ACCESS ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// Decode the packed form the traversal shader will read, so the gate exercises the SAME layout rather than a friendlier mirror of it.

struct DecodedNode
{
    float    Bounds[6] = {};
    bool     LeafCondition = false;
    uint32_t Offset       = 0;   // leaf
    uint32_t Count        = 0;   // leaf
    uint32_t RightRelative = 0;  // interior
    uint32_t SplitAxis    = 0;   // interior
};

DecodedNode DecodeNode(const GeometryTree& Tree, uint32_t NodeIndex)
{
    DecodedNode  Node;
    const size_t Base = static_cast<size_t>(NodeIndex) * GeometryTreeWordsPerNode;

    for (int32_t Index = 0; Index < 6; ++Index)
    {
        const uint32_t Word = Tree.NodeWords[Base + static_cast<size_t>(Index)];
        std::memcpy(&Node.Bounds[Index], &Word, sizeof(float));
    }

    const uint32_t FinalWord = Tree.NodeWords[Base + 7];
    Node.LeafCondition = ((FinalWord >> 16) == GeometryTreeLeafFlag);

    if (Node.LeafCondition)
    {
        Node.Offset = Tree.NodeWords[Base + 6];
        Node.Count  = FinalWord & 0xFFFFu;
    }
    else
    {
        Node.RightRelative = Tree.NodeWords[Base + 6];
        Node.SplitAxis     = FinalWord;
    }

    return Node;
}

// 🔴 CONTAINMENT IS A ULP-SCALED TEST, NOT AN EXACT ONE, AND THAT IS A PROPERTY OF THE REFERENCE — NOT A CONCESSION.
//    three-mesh-bvh stores each primitive as CENTRE and HALF-EXTENT in a Float32Array and reconstitutes the box as `centre ± half`. That is two
//    roundings; recomputing min/max straight from the vertices is one. The two disagree in the last bit, so the stored leaf box can sit one ULP
//    INSIDE the true box. The reference has the identical arithmetic (`buildTree.js:131` is a Float32Array, and buildUtils.js copies boundingData
//    verbatim with no outward rounding) and absorbs the difference in its ray-box test, so a port that padded the box here would no longer be the
//    same builder. Measured worst case across every mesh in this gate: 32 ULP, with the absolute error tracking coordinate magnitude — 1.2e-10 at
//    extent 1e-3 and 9.8e-4 at extent 1e4, which is the signature of rounding rather than a logic fault.
//
//    The tolerance therefore scales with the coordinate, and stays TIGHT: the failure this check exists to catch is a partition that files a
//    primitive under the wrong leaf, which puts a whole triangle outside the box — geometric error, thousands of ULP or more. A fixed absolute
//    epsilon would be simultaneously too loose at 1e-3 extent and too tight at 1e4.
double WorstObservedRatio = 0.0;   // the largest containment miss the whole run saw, as a fraction of its allowance — reported at the end

// 🔴 THE TOLERANCE SCALES WITH THE MESH, NOT WITH THE COORDINATE, AND THAT DISTINCTION IS THE WHOLE POINT.
//    The rounding is committed while computing `(min + max) * 0.5` and `(max - min) * 0.5` at the scale of the VERTICES. A coordinate that lands
//    near zero through cancellation still carries the error incurred at the operands' magnitude, so measuring the miss in ULPs of that tiny result
//    reports a huge figure for an ordinary rounding: measured, the eight stress seeds that exposed this all miss at coordinates around 0.02-0.2
//    inside meshes of extent 160-890, giving 128-512 "ULP" for an absolute error of ~1e-6. A per-coordinate ULP bound is therefore not merely too
//    tight — it is the wrong quantity. The probe that established this also showed the centre/half round trip alone accounts for 100.0% of every
//    one of those misses, leaving nothing for the partition to be blamed for.
//
//    So the allowance is a relative epsilon against the MESH EXTENT, which is the scale the arithmetic actually happened at. It stays vanishingly
//    small in absolute terms (a few parts in 10^7 of the model) while a misfiled primitive — the fault this check exists to catch — misses by a
//    fraction of the model and still fails loudly. Verified by injection: computing a child box over one fewer primitive produces 42 failures.
bool WithinRoundingTolerance(float Bound, float Value, bool LowerSide, float MeshScale)
{
    if (LowerSide ? (Value >= Bound) : (Value <= Bound)) return true;   // inside: nothing to forgive

    // 16 float32 epsilons of the mesh scale. The measured worst case is ~2e-6 against a scale of ~900, i.e. under 2 of these.
    const float Allowed = MeshScale * std::numeric_limits<float>::epsilon() * 16.0f;
    const float Miss    = std::fabs(Value - Bound);

    if (Allowed > 0.0f)
    {
        const double Ratio = static_cast<double>(Miss) / Allowed;
        if (Ratio > WorstObservedRatio) WorstObservedRatio = Ratio;
    }

    return Miss <= Allowed;
}

// The axis-aligned box of one triangle, from the ORIGINAL mesh, by original primitive index.
void TriangleBounds(const TriangleMesh& Mesh, uint32_t Triangle, float* OutMinimum, float* OutMaximum)
{
    for (uint32_t Axis = 0; Axis < 3; ++Axis)
    {
        OutMinimum[Axis] =  std::numeric_limits<float>::infinity();
        OutMaximum[Axis] = -std::numeric_limits<float>::infinity();
    }

    for (uint32_t Corner = 0; Corner < 3; ++Corner)
    {
        const uint32_t Vertex = Mesh.Indices[static_cast<size_t>(Triangle) * 3 + Corner];
        for (uint32_t Axis = 0; Axis < 3; ++Axis)
        {
            const float Value = Mesh.Positions[static_cast<size_t>(Vertex) * 3 + Axis];
            if (Value < OutMinimum[Axis]) OutMinimum[Axis] = Value;
            if (Value > OutMaximum[Axis]) OutMaximum[Axis] = Value;
        }
    }
}

// The magnitude the builder's bounds arithmetic actually operates at: the largest absolute vertex coordinate in the mesh. This, not the coordinate
// under test, is what sets the size of a rounding error — see WithinRoundingTolerance.
float ComputeMeshScale(const TriangleMesh& Mesh)
{
    float Scale = 0.0f;
    for (size_t Index = 0; Index < Mesh.Positions.size(); ++Index)
    {
        const float Magnitude = std::fabs(Mesh.Positions[Index]);
        if (Magnitude > Scale) Scale = Magnitude;
    }
    return Scale;
}

// ─── THE GATE ───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

void JudgeTree(const TriangleMesh& Mesh, const GeometryTreeOptions& Options, const std::string& CaseLabel)
{
    const float MeshScale = ComputeMeshScale(Mesh);

    GeometryTree Tree;
    const bool   BuildCondition = BuildGeometryTree(Mesh.Positions.data(),
                                                    static_cast<uint32_t>(Mesh.Positions.size() / 3),
                                                    Mesh.Indices.data(),
                                                    static_cast<uint32_t>(Mesh.Indices.size()),
                                                    Options,
                                                    Tree);

    Report(BuildCondition, CaseLabel + " : build returned true");
    if (!BuildCondition) return;

    const uint32_t TriangleCount = static_cast<uint32_t>(Mesh.Indices.size() / 3);

    Report(Tree.ReadyCondition, CaseLabel + " : ReadyCondition set");
    Report(Tree.NodeCount > 0, CaseLabel + " : at least one node");
    Report(Tree.NodeWords.size() == static_cast<size_t>(Tree.NodeCount) * GeometryTreeWordsPerNode,
           CaseLabel + " : node blob size matches NodeCount");
    Report(Tree.PrimitiveOrder.size() == TriangleCount, CaseLabel + " : permutation covers every primitive");

    // 🔴 The permutation must be a true PERMUTATION — every original index exactly once. A builder that duplicates or drops an entry produces
    //    leaves that name the wrong geometry while every box stays perfectly valid, which is invisible to any check that only looks at boxes.
    {
        std::vector<uint32_t> Seen(TriangleCount, 0);
        bool                  RangeCondition = true;
        for (uint32_t Entry : Tree.PrimitiveOrder)
        {
            if (Entry >= TriangleCount) { RangeCondition = false; break; }
            ++Seen[Entry];
        }
        Report(RangeCondition, CaseLabel + " : permutation entries in range");

        bool OnceCondition = RangeCondition;
        if (RangeCondition)
            for (uint32_t Tally : Seen)
                if (Tally != 1) { OnceCondition = false; break; }

        Report(OnceCondition, CaseLabel + " : every primitive appears exactly once");
    }

    // ─── walk the packed tree ───────────────────────────────────────────────────────────────────────────────────────────────────────────────
    std::vector<uint32_t> VisitTally(Tree.NodeCount, 0);
    std::vector<uint32_t> PrimitiveTally(TriangleCount, 0);

    uint32_t LeafTally         = 0;
    uint32_t CoveredPrimitives = 0;
    bool     OffsetCondition   = true;
    bool     LeafRangeCondition = true;
    bool     ContainmentCondition = true;
    bool     ChildContainmentCondition = true;
    bool     AxisCondition     = true;
    bool     LeafSizeCondition = true;

    std::vector<uint32_t> Walk;
    Walk.push_back(0);

    while (!Walk.empty())
    {
        const uint32_t NodeIndex = Walk.back();
        Walk.pop_back();

        if (NodeIndex >= Tree.NodeCount) { OffsetCondition = false; break; }
        if (++VisitTally[NodeIndex] > 1) { OffsetCondition = false; break; }   // a node reached twice means the offsets alias

        const DecodedNode Node = DecodeNode(Tree, NodeIndex);

        if (Node.LeafCondition)
        {
            ++LeafTally;

            if (static_cast<size_t>(Node.Offset) + Node.Count > TriangleCount)
            {
                LeafRangeCondition = false;
                continue;
            }

            CoveredPrimitives += Node.Count;

            // 📝 maxLeafSize is a stopping rule, not a guarantee — a node whose SAH finds no worthwhile split, or which hits MaxDepth, stays a
            //    leaf while holding more. So the check is that an OVERSIZED leaf only occurs where splitting was genuinely refused, which for
            //    these meshes means the coincident case. Everywhere else it would indicate the stopping rule is not being applied at all.
            if (Node.Count == 0) LeafSizeCondition = false;

            for (uint32_t Slot = 0; Slot < Node.Count; ++Slot)
            {
                const uint32_t Primitive = Tree.PrimitiveOrder[Node.Offset + Slot];
                if (Primitive >= TriangleCount) { LeafRangeCondition = false; break; }
                ++PrimitiveTally[Primitive];

                float Minimum[3], Maximum[3];
                TriangleBounds(Mesh, Primitive, Minimum, Maximum);

                // Every primitive a leaf names must sit inside that leaf's box, to within the reference's own rounding — see
                // WithinRoundingTolerance. A misfiled primitive misses by geometric distance and still fails here.
                for (uint32_t Axis = 0; Axis < 3; ++Axis)
                {
                    if (!WithinRoundingTolerance(Node.Bounds[Axis],     Minimum[Axis], true,  MeshScale)) ContainmentCondition = false;
                    if (!WithinRoundingTolerance(Node.Bounds[Axis + 3], Maximum[Axis], false, MeshScale)) ContainmentCondition = false;
                }
            }

            continue;
        }

        if (Node.SplitAxis > 2) AxisCondition = false;

        const uint32_t LeftIndex  = NodeIndex + 1;
        const uint32_t RightIndex = NodeIndex + Node.RightRelative;

        if (RightIndex >= Tree.NodeCount || Node.RightRelative < 2 || LeftIndex >= Tree.NodeCount)
        {
            OffsetCondition = false;
            continue;
        }

        // A parent's box must contain both children's. This is what makes a missed box test on the parent a sound reason to skip the subtree.
        const DecodedNode Left  = DecodeNode(Tree, LeftIndex);
        const DecodedNode Right = DecodeNode(Tree, RightIndex);
        for (uint32_t Axis = 0; Axis < 3; ++Axis)
        {
            if (!WithinRoundingTolerance(Node.Bounds[Axis],     Left.Bounds[Axis],      true,  MeshScale)) ChildContainmentCondition = false;
            if (!WithinRoundingTolerance(Node.Bounds[Axis + 3], Left.Bounds[Axis + 3],  false, MeshScale)) ChildContainmentCondition = false;
            if (!WithinRoundingTolerance(Node.Bounds[Axis],     Right.Bounds[Axis],     true,  MeshScale)) ChildContainmentCondition = false;
            if (!WithinRoundingTolerance(Node.Bounds[Axis + 3], Right.Bounds[Axis + 3], false, MeshScale)) ChildContainmentCondition = false;
        }

        Walk.push_back(RightIndex);
        Walk.push_back(LeftIndex);
    }

    Report(OffsetCondition,           CaseLabel + " : child offsets in range and non-aliasing");
    Report(LeafRangeCondition,        CaseLabel + " : leaf ranges inside the permutation");
    Report(AxisCondition,             CaseLabel + " : split axes are 0..2");
    Report(LeafSizeCondition,         CaseLabel + " : no empty leaf");
    Report(ContainmentCondition,      CaseLabel + " : every leaf primitive inside its leaf box");
    Report(ChildContainmentCondition, CaseLabel + " : every child box inside its parent box");
    Report(LeafTally == Tree.LeafCount, CaseLabel + " : reported LeafCount matches the walk");

    // Every node reachable exactly once — no orphans, no double-parenting.
    {
        bool ReachCondition = true;
        for (uint32_t Tally : VisitTally)
            if (Tally != 1) { ReachCondition = false; break; }
        Report(ReachCondition, CaseLabel + " : every node reached exactly once");
    }

    // 🔴 THE COVERAGE CHECK IS THE ONE THAT CATCHES A LOST PRIMITIVE. Leaf ranges must tile the permutation exactly: every primitive in exactly
    //    one leaf. A tree that drops geometry still traverses, still renders, and simply misses intersections — the hardest class of bug to see.
    Report(CoveredPrimitives == TriangleCount, CaseLabel + " : leaves cover every primitive");
    {
        bool OnceCondition = true;
        for (uint32_t Tally : PrimitiveTally)
            if (Tally != 1) { OnceCondition = false; break; }
        Report(OnceCondition, CaseLabel + " : every primitive in exactly one leaf");
    }

    // The parent table, when requested, must agree with the structure it mirrors — this is what a future GPU refit will walk.
    if (Options.DynamicCondition)
    {
        Report(Tree.ParentTable.size() == Tree.NodeCount, CaseLabel + " : parent table sized to the tree");

        if (Tree.ParentTable.size() == Tree.NodeCount && Tree.NodeCount > 0)
        {
            bool ParentCondition = (Tree.ParentTable[0] == GeometryTreeNoParent);
            for (uint32_t NodeIndex = 0; NodeIndex < Tree.NodeCount && ParentCondition; ++NodeIndex)
            {
                const DecodedNode Node = DecodeNode(Tree, NodeIndex);
                if (Node.LeafCondition) continue;

                const uint32_t LeftIndex  = NodeIndex + 1;
                const uint32_t RightIndex = NodeIndex + Node.RightRelative;
                if (LeftIndex >= Tree.NodeCount || RightIndex >= Tree.NodeCount) { ParentCondition = false; break; }

                if (Tree.ParentTable[LeftIndex]  != NodeIndex) ParentCondition = false;
                if (Tree.ParentTable[RightIndex] != NodeIndex) ParentCondition = false;
            }
            Report(ParentCondition, CaseLabel + " : parent table matches the child links");

            // Every node must reach the root by following parents — the exact walk a bottom-up refit performs.
            bool AscentCondition = true;
            for (uint32_t NodeIndex = 0; NodeIndex < Tree.NodeCount && AscentCondition; ++NodeIndex)
            {
                uint32_t Cursor = NodeIndex;
                uint32_t Steps  = 0;
                while (Cursor != GeometryTreeNoParent && Steps <= Tree.NodeCount)
                {
                    Cursor = Tree.ParentTable[Cursor];
                    ++Steps;
                }
                if (Steps > Tree.NodeCount) AscentCondition = false;   // a cycle, or a parent chain that never terminates
            }
            Report(AscentCondition, CaseLabel + " : every node ascends to the root without a cycle");
        }
    }
    else
    {
        Report(Tree.ParentTable.empty(), CaseLabel + " : static mesh allocates no parent table");
    }
}

} // namespace

int main(int ArgumentCount, char** ArgumentValues)
{
    bool StressCondition = false;
    for (int32_t Index = 1; Index < ArgumentCount; ++Index)
        if (std::strcmp(ArgumentValues[Index], "--stress") == 0) StressCondition = true;

    std::printf("=== GeometryTreeValidation : CPU SAH tree build (BLAS) ===\n\n");

    // ─── the shapes that break builders ─────────────────────────────────────────────────────────────────────────────────────────────────────
    std::vector<TriangleMesh> Meshes;
    Meshes.push_back(MakeScatteredMesh(1,    10.0f, 1u, "single triangle"));
    Meshes.push_back(MakeScatteredMesh(2,    10.0f, 2u, "two triangles"));
    Meshes.push_back(MakeScatteredMesh(7,    10.0f, 3u, "below the exact-split limit"));
    Meshes.push_back(MakeScatteredMesh(11,   10.0f, 4u, "at the leaf-size boundary"));
    Meshes.push_back(MakeScatteredMesh(64,   10.0f, 5u, "small cloud"));
    Meshes.push_back(MakeScatteredMesh(1000, 50.0f, 6u, "thousand-triangle cloud"));
    Meshes.push_back(MakeScatteredMesh(5000, 1e4f,  7u, "large-extent cloud"));
    Meshes.push_back(MakeScatteredMesh(3000, 1e-3f, 8u, "tiny-extent cloud"));
    Meshes.push_back(MakeFlatGridMesh(20,           "flat grid, zero Z extent"));
    Meshes.push_back(MakeCoincidentMesh(200,        "200 coincident triangles"));
    Meshes.push_back(MakeColinearMesh(500,          "colinear strip"));

    const GeometryTreeStrategy Strategies[3] = { GeometryTreeStrategy::Sah,
                                                 GeometryTreeStrategy::Center,
                                                 GeometryTreeStrategy::Average };
    const char* StrategyNames[3] = { "SAH", "Center", "Average" };

    for (const TriangleMesh& Mesh : Meshes)
        for (int32_t Index = 0; Index < 3; ++Index)
        {
            GeometryTreeOptions Options;
            Options.Strategy = Strategies[Index];
            JudgeTree(Mesh, Options, Mesh.Label + " [" + StrategyNames[Index] + "]");
        }

    // The dynamic path — the parent table a future skinned-mesh refit will walk.
    for (const TriangleMesh& Mesh : Meshes)
    {
        GeometryTreeOptions Options;
        Options.DynamicCondition = true;
        JudgeTree(Mesh, Options, Mesh.Label + " [dynamic]");
    }

    // ─── rejection : malformed input must be refused, not absorbed ──────────────────────────────────────────────────────────────────────────
    {
        GeometryTree        Tree;
        GeometryTreeOptions Options;
        const float         Positions[9] = { 0,0,0, 1,0,0, 0,1,0 };
        const uint32_t      Indices[3]   = { 0, 1, 2 };

        Report(!BuildGeometryTree(nullptr, 3, Indices, 3, Options, Tree),      "rejects a null position pointer");
        Report(!BuildGeometryTree(Positions, 3, nullptr, 3, Options, Tree),    "rejects a null index pointer");
        Report(!BuildGeometryTree(Positions, 3, Indices, 0, Options, Tree),    "rejects a zero index count");
        Report(!BuildGeometryTree(Positions, 0, Indices, 3, Options, Tree),    "rejects a zero vertex count");
        Report(!BuildGeometryTree(Positions, 3, Indices, 2, Options, Tree),    "rejects an index count that is not a multiple of three");

        // 🔴 An out-of-range index must be REFUSED, not clamped. Clamping would read a neighbouring vertex and build a tree over geometry the
        //    caller never supplied — plausible output from corrupt input, which is worse than a hard failure.
        const uint32_t BadIndices[3] = { 0, 1, 99 };
        Report(!BuildGeometryTree(Positions, 3, BadIndices, 3, Options, Tree), "rejects an out-of-range index");
        Report(!Tree.ReadyCondition,                                           "a refused build leaves ReadyCondition false");
    }

    // ─── stress : many seeds, deeper meshes ─────────────────────────────────────────────────────────────────────────────────────────────────
    if (StressCondition)
    {
        std::printf("\n--- stress suite ---\n");
        for (uint32_t Seed = 100; Seed < 140; ++Seed)
        {
            SequenceSource Source{ Seed };
            const uint32_t Count  = 1u + (NextWord(Source) % 4000u);
            const float    Extent = NextRange(Source, 0.01f, 1000.0f);

            const TriangleMesh Mesh = MakeScatteredMesh(Count, Extent, Seed, "stress");

            GeometryTreeOptions Options;
            Options.DynamicCondition = (Seed % 2 == 0);
            JudgeTree(Mesh, Options, "stress seed " + std::to_string(Seed));
        }
    }

    // The tolerance is only trustworthy while the observed miss stays far below it — printing both keeps a silently-widening gap visible.
    std::printf("\n=== %d checks, %d failures ===\n", CheckTally, FailureTally);
    std::printf("worst containment miss: %.1f%% of the rounding allowance\n", WorstObservedRatio * 100.0);
    return (FailureTally == 0) ? 0 : 1;
}
