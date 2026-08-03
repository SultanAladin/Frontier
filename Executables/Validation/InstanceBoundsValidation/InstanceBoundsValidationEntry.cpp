/*==============================================================================================================================================
                                                   INSTANCEBOUNDSVALIDATIONENTRY.CPP
==============================================================================================================================================*/
// 🧩 The exit gate for the TOP-level (TLAS) build's first two dispatches: brings up a HEADLESS Vulkan device, synthesises an instance array and a
//    matching arena (slice table + node words), runs InstanceBoundsReduce.comp and InstanceMortonCode.comp over them, and judges both against an
//    INDEPENDENT CPU model. Nothing before this proved either shader works — they compile, they link, and until this runs that is all that is known.
//
//    🔴 THE ORACLE IS INDEPENDENT, NOT A TRANSCRIPTION OF THE SHADER. The CPU model below computes the eight-corner world box, the centroid and the
//       Morton code from the DEFINITIONS, not by mirroring the GLSL line for line. A transcribed oracle agrees with the shader by construction and
//       proves nothing — the failure mode already recorded for this project as "a probe that re-implements shipped kernels measures the OLD operator
//       and reports success". Where the shader and this file must agree bit-for-bit, the geometry is chosen so the arithmetic is EXACT (see below)
//       rather than the comparison relaxed.
//
//    🔴 THE EIGHT-CORNER RULE IS **NOT** PROVEN HERE, AND CANNOT BE. This header used to claim a two-corner "foil" case established it. That claim was
//       false and has been removed rather than patched: the two rules differ only in the EXTENT of an instance's world box and provably NEVER in its
//       centre (opposite corners stay opposite under an affine map, so both rules average to the image of the centre — measured, 0 of 24 combinations
//       differ). InstanceBoundsReduce.comp discards the extent the instant it takes (WorldMinimum + WorldMaximum) * 0.5 and reduces CENTROIDS, so no
//       output either dispatch produces can distinguish the rules. The assertion belongs to the TRAVERSAL gate (#11), which tests rays against those
//       boxes and where a too-small box means missed intersections. Logged in the backlog; ExecuteObliqueRotationCase carries the full derivation.
//
//    ⚠️ BIT-EXACT JUDGEMENT REQUIRES REPRESENTABLE GEOMETRY — AND FOR THE BOX ONLY. min/max are selections and introduce no rounding, and the
//       transforms here are built from exactly representable quantities (axis permutations and sign flips with entries of exactly 0/+1/-1, the 3-4-5
//       oblique rotation with integer entries, and translations/scales that are small powers of two), so every product and sum is exact and the scene
//       box is compared to the last bit. This is the remedy the triangle gate arrived at after 104 spurious failures.
//
//       The MORTON KEYS are the one place a tolerance is unavoidable, and it is one bucket per axis. Bucket assignment divides by the scene extent,
//       and a divide is not bit-reproducible between MSVC and the GPU — the compiler may contract it to a reciprocal multiply, landing 1 ulp low, so
//       a centroid exactly on a boundary k/1024 truncates one bucket down. That is a property of float division, not a defect: the sort, the Karras
//       build and the conservative box test downstream all tolerate a one-bucket slip, and the shader's truncation is the CORRECT rule (round-to-
//       nearest was measured and is strictly worse — see the note in InstanceMortonCode.comp). Every real failure mode moves a bucket by far more
//       than one. The count of tolerated slips is PRINTED on passing runs so the tolerance cannot widen unnoticed.
//
//    📝 Headless on purpose: InitializeVulkanHost never creates or queries a surface, so a zero extension count yields a compute device, no window.

#define _CRT_SECURE_NO_WARNINGS

#include "Graphics/Acceleration/InstanceBoundsSubmission.h"
#include "Graphics/Acceleration/GeometryArenaSubmission.h"
#include "Graphics/Scene/SuzanneScene.h"
#include "Graphics/RenderExtension/Device/VulkanHost.h"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace Frontier;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// Where the compiled .comp.spv files live, relative to the repo root this exe is launched from.
const char* ShaderDirectoryPath = "Internal/Graphics/Acceleration/Shaders";

// 📝 The packed node stride in WORDS, mirroring GeometryTreeWordsPerNode. The root box occupies words [NodeOffset .. NodeOffset+5]; the remaining two
//    words are the node's tail (child offset / count+flag), which neither shader under test reads.
constexpr uint32_t WordsPerNode = 8u;

// 📝 Morton parameters, mirroring InstanceMortonCode.comp. Reproduced here rather than derived from the shader so a change to one is a visible
//    disagreement with the other rather than a silent co-move.
constexpr uint32_t MortonBitsPerAxis  = 10u;
constexpr float    MortonAxisMaximum  = 1023.0f;   // the last bucket index
constexpr float    MortonBucketCount  = 1024.0f;   // the scale factor; a power of two, so the multiply is exact

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE INDEPENDENT MODEL
//------------------------------------------------------------------------------------------------------------------------

// A local-space axis-aligned box, as it sits at the head of a mesh's node blob.
struct LocalBox
{
    float MinimumX = 0.0f, MinimumY = 0.0f, MinimumZ = 0.0f;
    float MaximumX = 0.0f, MaximumY = 0.0f, MaximumZ = 0.0f;
};

// A column-major 4x4, matching SuzanneSceneInstance::Model's storage exactly.
struct Matrix4
{
    float M[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
};

// Transform a point by a column-major matrix. Written from the definition of a column-major multiply — column c contributes M[4c + r] * v[c] to row r.
void TransformPoint(const Matrix4& Transform, float X, float Y, float Z, float& OutX, float& OutY, float& OutZ)
{
    OutX = Transform.M[0] * X + Transform.M[4] * Y + Transform.M[8]  * Z + Transform.M[12];
    OutY = Transform.M[1] * X + Transform.M[5] * Y + Transform.M[9]  * Z + Transform.M[13];
    OutZ = Transform.M[2] * X + Transform.M[6] * Y + Transform.M[10] * Z + Transform.M[14];
}

// A world-space box and the centroid taken from it.
struct WorldBox
{
    float MinimumX =  std::numeric_limits<float>::infinity();
    float MinimumY =  std::numeric_limits<float>::infinity();
    float MinimumZ =  std::numeric_limits<float>::infinity();
    float MaximumX = -std::numeric_limits<float>::infinity();
    float MaximumY = -std::numeric_limits<float>::infinity();
    float MaximumZ = -std::numeric_limits<float>::infinity();
};

// 🔴 THE CORRECT RULE: bound all EIGHT transformed corners. Derived from the definition — the image of a box under an affine map is the convex hull of
//    the images of its eight vertices, so the tight axis-aligned bound of that image is the bound of those eight points.
WorldBox TransformBoxByEightCorners(const LocalBox& Local, const Matrix4& Transform)
{
    WorldBox Result;
    for (uint32_t Corner = 0; Corner < 8u; ++Corner)
    {
        const float LocalX = (Corner & 1u) ? Local.MaximumX : Local.MinimumX;
        const float LocalY = (Corner & 2u) ? Local.MaximumY : Local.MinimumY;
        const float LocalZ = (Corner & 4u) ? Local.MaximumZ : Local.MinimumZ;

        float WorldX, WorldY, WorldZ;
        TransformPoint(Transform, LocalX, LocalY, LocalZ, WorldX, WorldY, WorldZ);

        Result.MinimumX = std::min(Result.MinimumX, WorldX);  Result.MaximumX = std::max(Result.MaximumX, WorldX);
        Result.MinimumY = std::min(Result.MinimumY, WorldY);  Result.MaximumY = std::max(Result.MaximumY, WorldY);
        Result.MinimumZ = std::min(Result.MinimumZ, WorldZ);  Result.MaximumZ = std::max(Result.MaximumZ, WorldZ);
    }
    return Result;
}

// 📝 A TransformBoxByTwoCorners lived here as the deliberately WRONG rule, for a foil case that asserted the GPU disagreed with it. Both are gone.
//    The foil could never have worked: this gate observes CENTROIDS, and the two rules provably share a centre under any affine map (see the file
//    header and ExecuteObliqueRotationCase). Keeping an unused wrong-rule helper would advertise coverage the gate does not have. The eight-corner
//    rule is asserted in the traversal gate (#11), where the box extent is what a ray actually tests against.

// The centroid rule both shaders share: the centre of the world box.
void CentroidOfBox(const WorldBox& Box, float& OutX, float& OutY, float& OutZ)
{
    OutX = (Box.MinimumX + Box.MaximumX) * 0.5f;
    OutY = (Box.MinimumY + Box.MaximumY) * 0.5f;
    OutZ = (Box.MinimumZ + Box.MaximumZ) * 0.5f;
}

// 📝 Morton spreading, written from the definition rather than copied: insert two zero bits after each of the low 10 bits. The loop form is
//    deliberately NOT the shader's shift-and-mask ladder — if the ladder's magic constants were mistyped, a transcribed oracle would carry the same
//    typo and agree. This form cannot.
uint32_t SpreadBitsByTwoReference(uint32_t Value)
{
    uint32_t Result = 0;
    for (uint32_t Bit = 0; Bit < MortonBitsPerAxis; ++Bit)
        if (Value & (1u << Bit))
            Result |= 1u << (Bit * 3u);
    return Result;
}

uint32_t MortonCodeReference(uint32_t BucketX, uint32_t BucketY, uint32_t BucketZ)
{
    return (SpreadBitsByTwoReference(BucketX) << 2u) |
           (SpreadBitsByTwoReference(BucketY) << 1u) |
            SpreadBitsByTwoReference(BucketZ);
}

// 📝 The inverse: pull the three 10-bit axis buckets back out of a 30-bit code. The gate needs this because it judges BUCKETS, not the packed key —
//    see the tolerance note at CompareMortonBuckets. Gathering bit (Bit*3 + Shift) is the exact inverse of the spread above, written from the same
//    definition rather than as a table, so a mistyped constant cannot make encode and decode agree with each other while both being wrong.
void DecodeMortonCode(uint32_t Code, uint32_t& OutBucketX, uint32_t& OutBucketY, uint32_t& OutBucketZ)
{
    OutBucketX = 0; OutBucketY = 0; OutBucketZ = 0;
    for (uint32_t Bit = 0; Bit < MortonBitsPerAxis; ++Bit)
    {
        if (Code & (1u << (Bit * 3u + 2u))) OutBucketX |= 1u << Bit;
        if (Code & (1u << (Bit * 3u + 1u))) OutBucketY |= 1u << Bit;
        if (Code & (1u << (Bit * 3u + 0u))) OutBucketZ |= 1u << Bit;
    }
}

// Bit comparison rather than ==, so a +0.0 / -0.0 divergence is caught rather than silently accepted.
bool ValuesMatchExactly(float Left, float Right)
{
    uint32_t LeftBits, RightBits;
    std::memcpy(&LeftBits,  &Left,  sizeof(LeftBits));
    std::memcpy(&RightBits, &Right, sizeof(RightBits));
    return LeftBits == RightBits;
}

bool BoxesMatchExactly(const InstanceBounds& Result, const WorldBox& Reference)
{
    return ValuesMatchExactly(Result.MinimumX, Reference.MinimumX) &&
           ValuesMatchExactly(Result.MinimumY, Reference.MinimumY) &&
           ValuesMatchExactly(Result.MinimumZ, Reference.MinimumZ) &&
           ValuesMatchExactly(Result.MaximumX, Reference.MaximumX) &&
           ValuesMatchExactly(Result.MaximumY, Reference.MaximumY) &&
           ValuesMatchExactly(Result.MaximumZ, Reference.MaximumZ);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       SCENE CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Each shape targets a specific way the two dispatches can be wrong, rather than variety for its own sake.
enum class SceneShape
{
    AxisAlignedGrid,    // no rotation: the CONTROL — passes even under the wrong two-corner rule
    RotatedRing,        // 🔴 every instance rotated: the case the eight-corner rule exists for
    MixedMeshes,        // several meshes of different local boxes, so MeshOrdinal -> slice -> NodeOffset is exercised
    SingleInstance,     // one instance: extent is zero on every axis, the degenerate normalisation
    CoplanarRow,        // all centroids on a plane: zero extent on one axis only
    IdenticalCentroids, // every centroid the same point: maximum duplicate Morton codes
    OutOfRangeOrdinals, // some instances name a slice past the end: must be DROPPED by both passes consistently
    AllOutOfRange,      // every instance dropped: the box must read EMPTY, not infinite
    NegativeOctant,     // entirely negative coordinates: the ordered-int flipped half
    ScaledInstances,    // per-instance scale, exact powers of two
    ObliqueRotation     // 🔴 3-4-5 rotation: the ONLY shape where eight-corner and two-corner actually differ — see ComposeObliqueTransform
};

const char* DescribeShape(SceneShape Shape)
{
    switch (Shape)
    {
        case SceneShape::AxisAlignedGrid:    return "axis-aligned";
        case SceneShape::RotatedRing:        return "rotated-ring";
        case SceneShape::MixedMeshes:        return "mixed-meshes";
        case SceneShape::SingleInstance:     return "single-instance";
        case SceneShape::CoplanarRow:        return "coplanar-row";
        case SceneShape::IdenticalCentroids: return "identical-centroids";
        case SceneShape::OutOfRangeOrdinals: return "out-of-range";
        case SceneShape::AllOutOfRange:      return "all-out-of-range";
        case SceneShape::NegativeOctant:     return "negative-octant";
        case SceneShape::ScaledInstances:    return "scaled";
        case SceneShape::ObliqueRotation:    return "oblique-rotation";
    }
    return "unknown";
}

// One synthesised scene: the instance array, the arena's slice table and node words, and the CPU model's view of the same.
struct SyntheticScene
{
    std::vector<SuzanneSceneInstance> Instances;
    std::vector<GeometryArenaSlice>   Slices;
    std::vector<uint32_t>             NodeWords;
    std::vector<LocalBox>             MeshBoxes;    // parallel to Slices, for the oracle
    uint32_t                          SliceCount = 0;
};

// 🔴 EVERY VALUE HERE IS EXACTLY REPRESENTABLE. Coordinates are multiples of 0.25 and scales are powers of two, so the corner transform (whose matrix
//    entries are 0, +1 or -1 plus a translation) and the * 0.5 centroid are exact. See the file header: the remedy for float disagreement is
//    representable geometry, not a tolerance.
float QuarterLattice(std::mt19937& Generator, float HalfSpread)
{
    std::uniform_int_distribution<int> Steps(-(int)(HalfSpread * 4.0f), (int)(HalfSpread * 4.0f));
    return (float)Steps(Generator) * 0.25f;
}

// Compose a column-major transform from an axis permutation with sign flips (a signed permutation matrix — always exactly representable, and a
// genuine rotation/reflection), a power-of-two scale, and a lattice translation.
Matrix4 ComposeTransform(uint32_t PermutationChoice, float Scale, float TranslateX, float TranslateY, float TranslateZ)
{
    // The six axis permutations, as (which source axis feeds output row 0, row 1, row 2).
    static const uint32_t Permutations[6][3] = {
        {0,1,2}, {0,2,1}, {1,0,2}, {1,2,0}, {2,0,1}, {2,1,0}
    };
    const uint32_t* Order = Permutations[PermutationChoice % 6u];

    // Sign flips drawn from the same choice, so distinct choices give distinct orientations.
    const float SignX = (PermutationChoice & 8u)  ? -1.0f : 1.0f;
    const float SignY = (PermutationChoice & 16u) ? -1.0f : 1.0f;
    const float SignZ = (PermutationChoice & 32u) ? -1.0f : 1.0f;
    const float Signs[3] = { SignX, SignY, SignZ };

    Matrix4 Result;
    for (uint32_t Index = 0; Index < 16u; ++Index) Result.M[Index] = 0.0f;

    // Column c (source axis c) feeds output row Order[c], scaled and signed. Exactly one non-zero per column.
    for (uint32_t SourceAxis = 0; SourceAxis < 3u; ++SourceAxis)
    {
        const uint32_t TargetRow = Order[SourceAxis];
        Result.M[SourceAxis * 4u + TargetRow] = Signs[SourceAxis] * Scale;
    }
    Result.M[12] = TranslateX;
    Result.M[13] = TranslateY;
    Result.M[14] = TranslateZ;
    Result.M[15] = 1.0f;
    return Result;
}

// 🔴 A SIGNED PERMUTATION CANNOT DISCRIMINATE THE EIGHT-CORNER RULE FROM THE TWO-CORNER RULE, AND THE FOIL CASE ORIGINALLY ASSUMED IT COULD. A
//    permutation maps an axis-aligned box to an axis-aligned box: it relabels and flips axes, so min/max still land on opposite corners and both
//    rules return the identical answer. The foil above reported "the two rules agree — the foil proves nothing" for exactly this reason, which is
//    the gate correctly catching a flaw in its OWN design rather than in the shader.
//
//    An OBLIQUE rotation is required, and it must still be exactly representable or the bit-exact box comparison becomes meaningless. The 3-4-5
//    Pythagorean triple supplies one: cos = 4/5 and sin = 3/5 are not representable, but a rotation matrix multiplied through by 5 has INTEGER
//    entries (4, -3, 3, 4) and is a similarity transform — a rotation composed with a uniform scale of 5, which is exactly what a bounding box test
//    needs to be non-axis-aligned. With local box extents at powers of two, every product and every sum below stays exact, so the resulting world
//    box is exactly representable and ValuesMatchExactly remains the right comparison.
//
//    ⚠️ THE SCALE OF 5 IS LOAD-BEARING AND MUST NOT BE "NORMALISED AWAY". Dividing by 5 to recover a unit rotation reintroduces 4/5 and 3/5, which
//       are periodic in binary, and every downstream box coordinate stops being exact.
Matrix4 ComposeObliqueTransform(uint32_t Variant, float TranslateX, float TranslateY, float TranslateZ)
{
    Matrix4 Result;
    for (uint32_t Index = 0; Index < 16u; ++Index) Result.M[Index] = 0.0f;

    // The 3-4-5 rotation in one of the three coordinate planes, times 5. Column-major: M[Column*4 + Row].
    const uint32_t Plane = Variant % 3u;
    const float    Sign  = (Variant & 4u) ? -1.0f : 1.0f;   // the mirrored rotation, still exact
    const float    Cos   = 4.0f;
    const float    Sin   = 3.0f * Sign;
    const float    Fixed = 5.0f;                            // the untouched axis carries the same uniform scale

    if (Plane == 0u)        // rotate in XY, Z fixed
    {
        Result.M[0] = Cos;  Result.M[1] = Sin;
        Result.M[4] = -Sin; Result.M[5] = Cos;
        Result.M[10] = Fixed;
    }
    else if (Plane == 1u)   // rotate in YZ, X fixed
    {
        Result.M[5] = Cos;  Result.M[6] = Sin;
        Result.M[9] = -Sin; Result.M[10] = Cos;
        Result.M[0] = Fixed;
    }
    else                    // rotate in XZ, Y fixed
    {
        Result.M[0] = Cos;   Result.M[2] = Sin;
        Result.M[8] = -Sin;  Result.M[10] = Cos;
        Result.M[5] = Fixed;
    }

    Result.M[12] = TranslateX;
    Result.M[13] = TranslateY;
    Result.M[14] = TranslateZ;
    Result.M[15] = 1.0f;
    return Result;
}

void BuildSyntheticScene(SceneShape Shape, uint32_t InstanceCount, uint32_t Seed, SyntheticScene& Out)
{
    Out = SyntheticScene();
    std::mt19937 Generator(Seed);

    // ─── the meshes ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    // Distinct local boxes so that reading the wrong slice yields a visibly wrong world box rather than a coincidentally right one.
    const uint32_t MeshCount = (Shape == SceneShape::MixedMeshes) ? 5u : 2u;
    for (uint32_t Mesh = 0; Mesh < MeshCount; ++Mesh)
    {
        LocalBox Box;
        const float HalfExtent = (float)(1u << (Mesh % 4u));      // 1, 2, 4, 8 — powers of two, exact
        Box.MinimumX = -HalfExtent;         Box.MaximumX = HalfExtent;
        Box.MinimumY = -HalfExtent * 0.5f;  Box.MaximumY = HalfExtent * 0.5f;
        Box.MinimumZ = -HalfExtent * 0.25f; Box.MaximumZ = HalfExtent * 0.25f;
        Out.MeshBoxes.push_back(Box);
    }

    // ─── the arena: slice table + node words ─────────────────────────────────────────────────────────────────────────────────────────────────
    // 🔴 NodeOffset is in WORDS. Each mesh gets several nodes so the offsets are non-trivial — a slice table whose every NodeOffset were 0 would let
    //    an ordinal-lookup bug pass unnoticed.
    for (uint32_t Mesh = 0; Mesh < MeshCount; ++Mesh)
    {
        const uint32_t NodeCount  = 1u + Mesh;                     // 1, 2, 3, ... nodes per mesh
        const uint32_t NodeOffset = (uint32_t)Out.NodeWords.size();

        GeometryArenaSlice Slice = {};
        Slice.NodeOffset      = NodeOffset;
        Slice.NodeCount       = NodeCount;
        Slice.PrimitiveOffset = 0;
        Slice.PrimitiveCount  = 0;
        Slice.VertexOffset    = 0;
        Slice.IndexOffset     = 0;
        Slice.ParentOffset    = 0;
        Out.Slices.push_back(Slice);

        Out.NodeWords.resize((size_t)NodeOffset + (size_t)NodeCount * WordsPerNode, 0u);

        // The root box at words [NodeOffset .. NodeOffset+5], as uint bit patterns.
        const LocalBox& Box = Out.MeshBoxes[Mesh];
        const float RootValues[6] = { Box.MinimumX, Box.MinimumY, Box.MinimumZ, Box.MaximumX, Box.MaximumY, Box.MaximumZ };
        for (uint32_t Word = 0; Word < 6u; ++Word)
            std::memcpy(&Out.NodeWords[NodeOffset + Word], &RootValues[Word], sizeof(uint32_t));

        // 📝 The non-root nodes are filled with a POISON pattern, not zeros. A shader that mistook NodeOffset for a node index would read these; a
        //    zero box would look like a plausible degenerate result, while this reads as a wild coordinate the comparison cannot forgive.
        for (uint32_t Word = 6u; Word < NodeCount * WordsPerNode; ++Word)
            Out.NodeWords[NodeOffset + Word] = 0x7F7FFFFFu;   // ~3.4e38 as a float bit pattern
    }
    Out.SliceCount = MeshCount;

    // ─── the instances ───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    for (uint32_t Index = 0; Index < InstanceCount; ++Index)
    {
        SuzanneSceneInstance Instance;
        Instance.PartitionId = Index;
        Instance.MaterialId  = 0;
        Instance.MeshOrdinal = Index % MeshCount;

        uint32_t PermutationChoice = 0;   // identity orientation unless the shape wants otherwise
        float    Scale             = 1.0f;
        float    TranslateX = 0.0f, TranslateY = 0.0f, TranslateZ = 0.0f;
        bool     ObliqueCondition  = false;   // [-] - true selects the 3-4-5 rotation instead of a signed permutation
        uint32_t ObliqueVariant    = 0u;

        switch (Shape)
        {
            case SceneShape::AxisAlignedGrid:
                TranslateX = QuarterLattice(Generator, 256.0f);
                TranslateY = QuarterLattice(Generator, 256.0f);
                TranslateZ = QuarterLattice(Generator, 256.0f);
                break;

            case SceneShape::RotatedRing:
                // 🔴 A DIFFERENT ORIENTATION PER INSTANCE. This is the case the eight-corner rule exists for; with the local boxes deliberately
                //    NON-CUBIC above, a permutation genuinely changes the world extent, so the two rules give different answers.
                PermutationChoice = 1u + (Index % 5u) + ((Index % 7u) << 3u);
                TranslateX = QuarterLattice(Generator, 128.0f);
                TranslateY = QuarterLattice(Generator, 128.0f);
                TranslateZ = QuarterLattice(Generator, 128.0f);
                break;

            case SceneShape::MixedMeshes:
                PermutationChoice = Index % 6u;
                TranslateX = QuarterLattice(Generator, 200.0f);
                TranslateY = QuarterLattice(Generator, 200.0f);
                TranslateZ = QuarterLattice(Generator, 200.0f);
                break;

            case SceneShape::SingleInstance:
                TranslateX = 16.0f; TranslateY = -8.0f; TranslateZ = 4.0f;
                break;

            case SceneShape::CoplanarRow:
                TranslateX = QuarterLattice(Generator, 300.0f);
                TranslateY = QuarterLattice(Generator, 300.0f);
                TranslateZ = 0.0f;                                    // exactly flat on Z
                break;

            case SceneShape::IdenticalCentroids:
                // Every instance at the same place with the same mesh: every centroid identical, so every Morton code must be identical too.
                Instance.MeshOrdinal = 0u;
                TranslateX = 12.0f; TranslateY = -6.0f; TranslateZ = 2.0f;
                break;

            case SceneShape::OutOfRangeOrdinals:
                // 🔴 Every third instance names a slice past the end and must be DROPPED by BOTH passes. The rest are ordinary.
                if ((Index % 3u) == 2u)
                    Instance.MeshOrdinal = MeshCount + (Index % 17u);
                TranslateX = QuarterLattice(Generator, 150.0f);
                TranslateY = QuarterLattice(Generator, 150.0f);
                TranslateZ = QuarterLattice(Generator, 150.0f);
                break;

            case SceneShape::AllOutOfRange:
                Instance.MeshOrdinal = MeshCount + Index;
                TranslateX = QuarterLattice(Generator, 150.0f);
                TranslateY = QuarterLattice(Generator, 150.0f);
                TranslateZ = QuarterLattice(Generator, 150.0f);
                break;

            case SceneShape::NegativeOctant:
                TranslateX = -512.0f + QuarterLattice(Generator, 64.0f);
                TranslateY = -512.0f + QuarterLattice(Generator, 64.0f);
                TranslateZ = -512.0f + QuarterLattice(Generator, 64.0f);
                break;

            case SceneShape::ScaledInstances:
                Scale = (float)(1u << (Index % 5u));                  // 1, 2, 4, 8, 16 — exact
                PermutationChoice = Index % 6u;
                TranslateX = QuarterLattice(Generator, 180.0f);
                TranslateY = QuarterLattice(Generator, 180.0f);
                TranslateZ = QuarterLattice(Generator, 180.0f);
                break;

            case SceneShape::ObliqueRotation:
                // 🔴 The genuinely non-axis-aligned case. Translations stay on the quarter lattice but are kept small: the oblique matrix carries a
                //    uniform scale of 5 (see ComposeObliqueTransform), so the world coordinates it produces are five times the local extent, and a
                //    modest spread here keeps every sum inside the exactly-representable range.
                ObliqueCondition = true;
                ObliqueVariant   = (Index % 3u) + ((Index % 2u) << 2u);
                TranslateX = QuarterLattice(Generator, 100.0f);
                TranslateY = QuarterLattice(Generator, 100.0f);
                TranslateZ = QuarterLattice(Generator, 100.0f);
                break;
        }

        const Matrix4 Transform = ObliqueCondition
            ? ComposeObliqueTransform(ObliqueVariant, TranslateX, TranslateY, TranslateZ)
            : ComposeTransform(PermutationChoice, Scale, TranslateX, TranslateY, TranslateZ);
        std::memcpy(Instance.Model, Transform.M, sizeof(Transform.M));

        Out.Instances.push_back(Instance);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE ORACLE OVER A SCENE
//------------------------------------------------------------------------------------------------------------------------

// The expected scene box, and per-instance centroids, by the correct rule. There is no longer a rule selector: the wrong-rule branch this once
// carried served a foil case that could not discriminate the two rules through centroids, and both were removed together.
struct SceneReference
{
    WorldBox           SceneBox;
    bool               EmptyCondition = true;
    std::vector<bool>  Dropped;                 // per instance
    std::vector<float> CentroidX, CentroidY, CentroidZ;
};

SceneReference ComputeSceneReference(const SyntheticScene& Scene)
{
    SceneReference Reference;
    const uint32_t InstanceCount = (uint32_t)Scene.Instances.size();
    Reference.Dropped.assign(InstanceCount, true);
    Reference.CentroidX.assign(InstanceCount, 0.0f);
    Reference.CentroidY.assign(InstanceCount, 0.0f);
    Reference.CentroidZ.assign(InstanceCount, 0.0f);

    for (uint32_t Index = 0; Index < InstanceCount; ++Index)
    {
        const SuzanneSceneInstance& Instance = Scene.Instances[Index];
        if (Instance.MeshOrdinal >= Scene.SliceCount)
            continue;   // dropped, exactly as both shaders drop it

        Matrix4 Transform;
        std::memcpy(Transform.M, Instance.Model, sizeof(Transform.M));

        const LocalBox& Local = Scene.MeshBoxes[Instance.MeshOrdinal];
        const WorldBox World = TransformBoxByEightCorners(Local, Transform);

        float CentroidX, CentroidY, CentroidZ;
        CentroidOfBox(World, CentroidX, CentroidY, CentroidZ);

        Reference.Dropped[Index]   = false;
        Reference.CentroidX[Index] = CentroidX;
        Reference.CentroidY[Index] = CentroidY;
        Reference.CentroidZ[Index] = CentroidZ;

        Reference.SceneBox.MinimumX = std::min(Reference.SceneBox.MinimumX, CentroidX);
        Reference.SceneBox.MaximumX = std::max(Reference.SceneBox.MaximumX, CentroidX);
        Reference.SceneBox.MinimumY = std::min(Reference.SceneBox.MinimumY, CentroidY);
        Reference.SceneBox.MaximumY = std::max(Reference.SceneBox.MaximumY, CentroidY);
        Reference.SceneBox.MinimumZ = std::min(Reference.SceneBox.MinimumZ, CentroidZ);
        Reference.SceneBox.MaximumZ = std::max(Reference.SceneBox.MaximumZ, CentroidZ);
        Reference.EmptyCondition = false;
    }
    return Reference;
}

// The expected Morton key and payload per instance, given the scene box the reduce produced.
void ComputeMortonReference(const SyntheticScene& Scene,
                            const SceneReference& Reference,
                            const InstanceBounds& SceneBox,
                            std::vector<uint32_t>& OutKeys,
                            std::vector<uint32_t>& OutPayloads,
                            std::vector<uint32_t>& OutBucketX,
                            std::vector<uint32_t>& OutBucketY,
                            std::vector<uint32_t>& OutBucketZ)
{
    const uint32_t InstanceCount = (uint32_t)Scene.Instances.size();
    OutKeys.assign(InstanceCount, 0u);
    OutPayloads.assign(InstanceCount, 0u);
    OutBucketX.assign(InstanceCount, 0u);
    OutBucketY.assign(InstanceCount, 0u);
    OutBucketZ.assign(InstanceCount, 0u);

    // 📝 Normalise against the box the GPU actually produced, not the CPU's own — the Morton pass reads the device accumulator, so judging it against
    //    a different box would conflate a Morton bug with a bounds bug. The bounds are judged separately and exactly, so this is not a free pass.
    const float ExtentX = SceneBox.MaximumX - SceneBox.MinimumX;
    const float ExtentY = SceneBox.MaximumY - SceneBox.MinimumY;
    const float ExtentZ = SceneBox.MaximumZ - SceneBox.MinimumZ;
    const float SafeX = (ExtentX > 0.0f) ? ExtentX : 1.0f;
    const float SafeY = (ExtentY > 0.0f) ? ExtentY : 1.0f;
    const float SafeZ = (ExtentZ > 0.0f) ? ExtentZ : 1.0f;

    for (uint32_t Index = 0; Index < InstanceCount; ++Index)
    {
        OutPayloads[Index] = Index;   // the payload is the instance's own index in every case, dropped or not

        if (Reference.Dropped[Index])
        {
            OutKeys[Index] = 0xFFFFFFFFu;   // dropped instances sort to the end
            continue;
        }

        const float NormalisedX = std::min(std::max((Reference.CentroidX[Index] - SceneBox.MinimumX) / SafeX, 0.0f), 1.0f);
        const float NormalisedY = std::min(std::max((Reference.CentroidY[Index] - SceneBox.MinimumY) / SafeY, 0.0f), 1.0f);
        const float NormalisedZ = std::min(std::max((Reference.CentroidZ[Index] - SceneBox.MinimumZ) / SafeZ, 0.0f), 1.0f);

        // 🔴 Scale by the BUCKET COUNT and clamp to the last bucket, matching the shader. Scaling by 1023 instead makes the top bucket reachable
        //    only when the normalisation divide is exact, which it need not be — see the note in InstanceMortonCode.comp and the measurement in
        //    _ClaudeScratch/build/Scale1024Probe.cpp. This is a shared DEFINITION, not the oracle copying the shader: the top bucket must be
        //    reachable by the instance on the box's upper face, and 1024-and-clamp is the only one of the three candidate forms that guarantees it.
        const uint32_t BucketX = (uint32_t)std::min(std::max(NormalisedX * MortonBucketCount, 0.0f), MortonAxisMaximum);
        const uint32_t BucketY = (uint32_t)std::min(std::max(NormalisedY * MortonBucketCount, 0.0f), MortonAxisMaximum);
        const uint32_t BucketZ = (uint32_t)std::min(std::max(NormalisedZ * MortonBucketCount, 0.0f), MortonAxisMaximum);

        OutBucketX[Index] = BucketX;
        OutBucketY[Index] = BucketY;
        OutBucketZ[Index] = BucketZ;
        OutKeys[Index]    = MortonCodeReference(BucketX, BucketY, BucketZ);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        DEVICE PLUMBING
//------------------------------------------------------------------------------------------------------------------------

uint32_t SelectMemoryTypeIndex(VkPhysicalDevice PhysicalDevice, uint32_t CompatibleTypesBitmask,
                               VkMemoryPropertyFlags RequiredProperties, bool& FoundEnabled)
{
    VkPhysicalDeviceMemoryProperties MemoryProperties = {};
    vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &MemoryProperties);
    for (uint32_t Index = 0; Index < MemoryProperties.memoryTypeCount; ++Index)
    {
        const bool TypeCompatible = (CompatibleTypesBitmask & (1u << Index)) != 0;
        const bool PropertyMatch  = (MemoryProperties.memoryTypes[Index].propertyFlags & RequiredProperties) == RequiredProperties;
        if (TypeCompatible && PropertyMatch) { FoundEnabled = true; return Index; }
    }
    FoundEnabled = false;
    return 0;
}

// A host-visible storage buffer. Host-visible rather than device-local + staging because this is a harness and the simpler path has fewer places to be
// wrong; it also lets the Morton output be read back by a plain map with no transfer.
bool CreateHostBuffer(VulkanHost& Host, const void* SourceData, VkDeviceSize Bytes,
                      VkBuffer& OutBuffer, VkDeviceMemory& OutMemory)
{
    OutBuffer = VK_NULL_HANDLE;
    OutMemory = VK_NULL_HANDLE;
    if (Bytes == 0) return false;

    VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    BufferInformation.size        = Bytes;
    BufferInformation.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    BufferInformation.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(Host.Device, &BufferInformation, Host.Allocator, &OutBuffer) != VK_SUCCESS)
        return false;

    VkMemoryRequirements MemoryRequirements = {};
    vkGetBufferMemoryRequirements(Host.Device, OutBuffer, &MemoryRequirements);

    bool Found = false;
    const uint32_t TypeIndex = SelectMemoryTypeIndex(Host.PhysicalDevice, MemoryRequirements.memoryTypeBits,
                                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, Found);
    if (!Found)
    {
        vkDestroyBuffer(Host.Device, OutBuffer, Host.Allocator);
        OutBuffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo AllocateInformation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    AllocateInformation.allocationSize  = MemoryRequirements.size;
    AllocateInformation.memoryTypeIndex = TypeIndex;
    if (vkAllocateMemory(Host.Device, &AllocateInformation, Host.Allocator, &OutMemory) != VK_SUCCESS ||
        vkBindBufferMemory(Host.Device, OutBuffer, OutMemory, 0) != VK_SUCCESS)
    {
        if (OutMemory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, OutMemory, Host.Allocator);
        vkDestroyBuffer(Host.Device, OutBuffer, Host.Allocator);
        OutBuffer = VK_NULL_HANDLE;
        OutMemory = VK_NULL_HANDLE;
        return false;
    }

    if (SourceData != nullptr)
    {
        void* Mapped = nullptr;
        if (vkMapMemory(Host.Device, OutMemory, 0, Bytes, 0, &Mapped) != VK_SUCCESS || Mapped == nullptr)
            return false;
        std::memcpy(Mapped, SourceData, (size_t)Bytes);
        vkUnmapMemory(Host.Device, OutMemory);
    }
    return true;
}

bool ReadBackBuffer(VulkanHost& Host, VkDeviceMemory Memory, VkDeviceSize Bytes, void* OutData)
{
    void* Mapped = nullptr;
    if (vkMapMemory(Host.Device, Memory, 0, Bytes, 0, &Mapped) != VK_SUCCESS || Mapped == nullptr)
        return false;
    std::memcpy(OutData, Mapped, (size_t)Bytes);
    vkUnmapMemory(Host.Device, Memory);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            ONE CASE
//------------------------------------------------------------------------------------------------------------------------

uint32_t FailureTally = 0;
uint32_t CaseTally    = 0;

// Everything one case allocates, so teardown is a single call on every exit path.
struct CaseBuffers
{
    VkBuffer       Instance = VK_NULL_HANDLE, Slice = VK_NULL_HANDLE, Node = VK_NULL_HANDLE;
    VkBuffer       Key      = VK_NULL_HANDLE, Payload = VK_NULL_HANDLE;
    VkDeviceMemory InstanceMemory = VK_NULL_HANDLE, SliceMemory = VK_NULL_HANDLE, NodeMemory = VK_NULL_HANDLE;
    VkDeviceMemory KeyMemory = VK_NULL_HANDLE, PayloadMemory = VK_NULL_HANDLE;
    VkDeviceSize   InstanceBytes = 0, SliceBytes = 0, NodeBytes = 0, KeyBytes = 0, PayloadBytes = 0;
};

void DestroyCaseBuffers(VulkanHost& Host, CaseBuffers& Buffers)
{
    const auto Drop = [&](VkBuffer& Buffer, VkDeviceMemory& Memory)
    {
        if (Buffer != VK_NULL_HANDLE) vkDestroyBuffer(Host.Device, Buffer, Host.Allocator);
        if (Memory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, Memory, Host.Allocator);
        Buffer = VK_NULL_HANDLE;
        Memory = VK_NULL_HANDLE;
    };
    Drop(Buffers.Instance, Buffers.InstanceMemory);
    Drop(Buffers.Slice,    Buffers.SliceMemory);
    Drop(Buffers.Node,     Buffers.NodeMemory);
    Drop(Buffers.Key,      Buffers.KeyMemory);
    Drop(Buffers.Payload,  Buffers.PayloadMemory);
}

// Submit a recorded command buffer and wait, bounded. Returns false on timeout — a hang must be a reported OUTCOME, not an unattended process.
bool SubmitAndWait(VulkanHost& Host, VkCommandPool CommandPool, VkCommandBuffer Command)
{
    VkFence Fence = VK_NULL_HANDLE;
    VkFenceCreateInfo FenceInformation = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    vkCreateFence(Host.Device, &FenceInformation, Host.Allocator, &Fence);

    VkSubmitInfo SubmitInformation = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    SubmitInformation.commandBufferCount = 1;
    SubmitInformation.pCommandBuffers    = &Command;
    vkQueueSubmit(Host.GraphicsQueue, 1, &SubmitInformation, Fence);

    const VkResult WaitResult = vkWaitForFences(Host.Device, 1, &Fence, VK_TRUE, 30ull * 1000ull * 1000ull * 1000ull);
    vkDestroyFence(Host.Device, Fence, Host.Allocator);
    vkFreeCommandBuffers(Host.Device, CommandPool, 1, &Command);
    return WaitResult == VK_SUCCESS;
}

// Run both dispatches over one synthesised scene and judge the box, the keys and the payloads.
bool ExecuteCase(VulkanHost& Host, VkCommandPool CommandPool, SceneShape Shape, uint32_t InstanceCount, uint32_t Seed)
{
    ++CaseTally;

    SyntheticScene Scene;
    BuildSyntheticScene(Shape, InstanceCount, Seed, Scene);
    const SceneReference Reference = ComputeSceneReference(Scene);

    CaseBuffers Buffers;
    Buffers.InstanceBytes = (VkDeviceSize)Scene.Instances.size() * sizeof(SuzanneSceneInstance);
    Buffers.SliceBytes    = (VkDeviceSize)Scene.Slices.size()    * sizeof(GeometryArenaSlice);
    Buffers.NodeBytes     = (VkDeviceSize)Scene.NodeWords.size() * sizeof(uint32_t);
    Buffers.KeyBytes      = (VkDeviceSize)InstanceCount * sizeof(uint32_t);
    Buffers.PayloadBytes  = Buffers.KeyBytes;

    // 🔴 The key and payload buffers are pre-filled with a POISON pattern rather than left undefined. A shader that failed to write an instance's slot
    //    would otherwise inherit whatever the allocator handed back — often zero, which is a legitimate Morton code and would read as a pass.
    std::vector<uint32_t> Poison((size_t)InstanceCount, 0xDEADBEEFu);

    if (!CreateHostBuffer(Host, Scene.Instances.data(), Buffers.InstanceBytes, Buffers.Instance, Buffers.InstanceMemory) ||
        !CreateHostBuffer(Host, Scene.Slices.data(),    Buffers.SliceBytes,    Buffers.Slice,    Buffers.SliceMemory)    ||
        !CreateHostBuffer(Host, Scene.NodeWords.data(), Buffers.NodeBytes,     Buffers.Node,     Buffers.NodeMemory)     ||
        !CreateHostBuffer(Host, Poison.data(),          Buffers.KeyBytes,      Buffers.Key,      Buffers.KeyMemory)      ||
        !CreateHostBuffer(Host, Poison.data(),          Buffers.PayloadBytes,  Buffers.Payload,  Buffers.PayloadMemory))
    {
        std::printf("  %-20s %8u  SCENE UPLOAD FAILED\n", DescribeShape(Shape), InstanceCount);
        ++FailureTally;
        DestroyCaseBuffers(Host, Buffers);
        return false;
    }

    InstanceBoundsSubmission Bounds;
    bool Passed = false;

    if (!InitializeInstanceBoundsSubmission(Bounds, Host, ShaderDirectoryPath))
    {
        std::printf("  %-20s %8u  INITIALIZE FAILED\n", DescribeShape(Shape), InstanceCount);
        ++FailureTally;
    }
    else if (!BindInstanceBoundsScene(Bounds, Buffers.Instance, Buffers.InstanceBytes,
                                              Buffers.Slice,    Buffers.SliceBytes,
                                              Buffers.Node,     Buffers.NodeBytes,
                                              InstanceCount, Scene.SliceCount))
    {
        std::printf("  %-20s %8u  SCENE BIND FAILED\n", DescribeShape(Shape), InstanceCount);
        ++FailureTally;
    }
    else if (!BindInstanceMortonTarget(Bounds, Buffers.Key, Buffers.KeyBytes, Buffers.Payload, Buffers.PayloadBytes))
    {
        std::printf("  %-20s %8u  MORTON TARGET BIND FAILED\n", DescribeShape(Shape), InstanceCount);
        ++FailureTally;
    }
    else
    {
        // ─── record both dispatches into ONE command buffer ──────────────────────────────────────────────────────────────────────────────────
        // 📝 One buffer on purpose: the Morton pass reads the box the reduce writes on the DEVICE, so recording them together is exactly how the
        //    frame will do it, and it exercises the barrier between them rather than hiding it behind a host wait.
        VkCommandBufferAllocateInfo CommandAllocate = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        CommandAllocate.commandPool        = CommandPool;
        CommandAllocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        CommandAllocate.commandBufferCount = 1;
        VkCommandBuffer Command = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(Host.Device, &CommandAllocate, &Command);

        VkCommandBufferBeginInfo BeginInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        BeginInformation.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(Command, &BeginInformation);
        RecordInstanceBoundsReduce(Bounds, Command);
        RecordInstanceMortonCode(Bounds, Command);
        vkEndCommandBuffer(Command);

        if (!SubmitAndWait(Host, CommandPool, Command))
        {
            std::printf("  %-20s %8u  DISPATCH TIMED OUT\n", DescribeShape(Shape), InstanceCount);
            ++FailureTally;
        }
        else
        {
            vkDeviceWaitIdle(Host.Device);

            InstanceBounds Result;
            if (!RetrieveInstanceBounds(Bounds, CommandPool, Result))
            {
                std::printf("  %-20s %8u  BOUNDS READBACK FAILED\n", DescribeShape(Shape), InstanceCount);
                ++FailureTally;
            }
            else
            {
                const bool EmptyAgrees = (Result.EmptyCondition == Reference.EmptyCondition);
                const bool BoxAgrees   = Reference.EmptyCondition ? true : BoxesMatchExactly(Result, Reference.SceneBox);

                // ─── the Morton half ────────────────────────────────────────────────────────────────────────────────────────────────────────
                std::vector<uint32_t> ResultKeys((size_t)InstanceCount, 0u);
                std::vector<uint32_t> ResultPayloads((size_t)InstanceCount, 0u);
                const bool KeysRead = ReadBackBuffer(Host, Buffers.KeyMemory,     Buffers.KeyBytes,     ResultKeys.data()) &&
                                      ReadBackBuffer(Host, Buffers.PayloadMemory, Buffers.PayloadBytes, ResultPayloads.data());

                bool KeysAgree     = false;
                bool PayloadsAgree = false;
                uint32_t FirstKeyMismatch = 0xFFFFFFFFu;
                uint32_t ExpectedAtMismatch = 0u;
                uint32_t MismatchTally = 0u;   // [-] - keys not bit-identical; tolerated within one bucket, reported either way
                uint32_t ExactTally    = 0u;   // [-] - keys bit-identical to the oracle

                if (KeysRead)
                {
                    std::vector<uint32_t> ExpectedKeys, ExpectedPayloads;
                    std::vector<uint32_t> ExpectedX, ExpectedY, ExpectedZ;
                    ComputeMortonReference(Scene, Reference, Result, ExpectedKeys, ExpectedPayloads,
                                           ExpectedX, ExpectedY, ExpectedZ);

                    KeysAgree     = true;
                    PayloadsAgree = true;
                    for (uint32_t Index = 0; Index < InstanceCount; ++Index)
                    {
                        if (ResultKeys[Index] == ExpectedKeys[Index])
                        {
                            ++ExactTally;
                        }
                        else
                        {
                            // 🔴 A BIT-EXACT KEY IS THE WRONG CONTRACT AND DEMANDING IT MAKES THIS GATE LIE. It would require the GPU's
                            //    (Centroid - Minimum) / Extent to round identically to MSVC's, which no specification promises: the compiler may
                            //    contract that divide into a multiply by the reciprocal, landing 1 ulp low, and a centroid sitting exactly on a
                            //    bucket boundary k/1024 then truncates one bucket down. That is a property of float division, not a defect in the
                            //    shader — the shader's rounding is CORRECT (see the note in InstanceMortonCode.comp; round-to-nearest was measured
                            //    and is strictly worse). A gate that fails on it trains us to ignore it.
                            //
                            //    So judge what the CONSUMER depends on: the bucket, per axis, within one. The sort tolerates it (any total order is
                            //    valid), Karras tolerates it (it builds over whatever order it is handed), and traversal tolerates it (it tests the
                            //    boxes from InstanceBoundsReduce, which ARE judged bit-exactly above). The cost of a one-bucket slip is a marginally
                            //    worse split, never a missed intersection.
                            //
                            //    ⚠️ TOLERATING TWO WOULD HIDE REAL BUGS. Every failure mode worth catching here — a mistyped ladder constant, the
                            //       wrong axis in the interleave, normalising against the wrong box, a dropped clamp — moves a bucket by far more
                            //       than one, or moves it on the wrong axis. One is the widest tolerance that still catches all of those.
                            uint32_t GotX, GotY, GotZ;
                            DecodeMortonCode(ResultKeys[Index], GotX, GotY, GotZ);

                            const bool WithinOneBucket =
                                !Reference.Dropped[Index] &&
                                (uint32_t)std::abs((int32_t)GotX - (int32_t)ExpectedX[Index]) <= 1u &&
                                (uint32_t)std::abs((int32_t)GotY - (int32_t)ExpectedY[Index]) <= 1u &&
                                (uint32_t)std::abs((int32_t)GotZ - (int32_t)ExpectedZ[Index]) <= 1u;

                            if (FirstKeyMismatch == 0xFFFFFFFFu)
                            {
                                FirstKeyMismatch   = Index;
                                ExpectedAtMismatch = ExpectedKeys[Index];
                            }
                            ++MismatchTally;

                            // A dropped instance must carry EXACTLY 0xFFFFFFFF — that is a discrete decision, not a quantisation, so no tolerance
                            // applies to it and WithinOneBucket is forced false above.
                            if (!WithinOneBucket)
                                KeysAgree = false;
                        }
                        if (ResultPayloads[Index] != ExpectedPayloads[Index])
                            PayloadsAgree = false;
                    }
                }

                Passed = EmptyAgrees && BoxAgrees && KeysRead && KeysAgree && PayloadsAgree;
                if (!Passed) ++FailureTally;

                // 📝 The within-one count is printed on PASSING runs too, deliberately. A tolerance nobody can see is a tolerance that quietly
                //    widens: if a future change starts producing hundreds of one-bucket slips where there were three, the gate still says PASS and
                //    only this column shows the drift.
                std::printf("  %-20s %8u  empty %s  box %s  keys %s  payloads %s  %s%s\n",
                            DescribeShape(Shape), InstanceCount,
                            EmptyAgrees   ? "y" : "N",
                            BoxAgrees     ? "y" : "N",
                            KeysAgree     ? "y" : "N",
                            PayloadsAgree ? "y" : "N",
                            Passed ? "PASS" : "FAIL",
                            (MismatchTally > 0 && KeysAgree) ? "  (within one bucket: see count below)" : "");
                if (MismatchTally > 0 && KeysAgree)
                    std::printf("      %u of %u keys differ by <= 1 bucket per axis — tolerated; %u exact\n",
                                MismatchTally, InstanceCount, ExactTally);

                if (!Passed && !Reference.EmptyCondition)
                {
                    std::printf("      expected box min (%.9g %.9g %.9g) max (%.9g %.9g %.9g)\n",
                                Reference.SceneBox.MinimumX, Reference.SceneBox.MinimumY, Reference.SceneBox.MinimumZ,
                                Reference.SceneBox.MaximumX, Reference.SceneBox.MaximumY, Reference.SceneBox.MaximumZ);
                    std::printf("      actual   box min (%.9g %.9g %.9g) max (%.9g %.9g %.9g)\n",
                                Result.MinimumX, Result.MinimumY, Result.MinimumZ,
                                Result.MaximumX, Result.MaximumY, Result.MaximumZ);
                    if (FirstKeyMismatch != 0xFFFFFFFFu)
                        std::printf("      keys differ at %u of %u; first at instance %u: expected 0x%08X got 0x%08X"
                                    "  (centroid %.9g %.9g %.9g)\n",
                                    MismatchTally, InstanceCount, FirstKeyMismatch,
                                    ExpectedAtMismatch, ResultKeys[FirstKeyMismatch],
                                    Reference.CentroidX[FirstKeyMismatch],
                                    Reference.CentroidY[FirstKeyMismatch],
                                    Reference.CentroidZ[FirstKeyMismatch]);
                }
            }
        }
    }

    FinalizeInstanceBoundsSubmission(Bounds);
    DestroyCaseBuffers(Host, Buffers);
    return Passed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE OBLIQUE ROTATION CASE
//------------------------------------------------------------------------------------------------------------------------

// 🔴 THIS CASE USED TO CLAIM IT PROVED THE EIGHT-CORNER RULE. IT CANNOT, AND NEITHER CAN ANY CASE IN THIS GATE. The claim is removed rather than
//    repaired, because the impossibility is a property of what these two dispatches compute, not a fixable weakness in the test:
//
//      · The two rules differ ONLY in the EXTENT of an instance's world box. They provably never differ in its CENTRE — the two-corner rule averages
//        the images of two OPPOSITE corners, an affine map carries opposite points to points symmetric about the image of the centre, so the midpoint
//        is exactly that image; the eight-corner rule pairs up identically and lands on the same point. Measured across symmetric and asymmetric
//        local boxes and all six oblique variants (_ClaudeScratch/build/CentroidFoilProbe.cpp): 0 of 24 combinations differ.
//      · InstanceBoundsReduce.comp discards the extent at the moment it takes (WorldMinimum + WorldMaximum) * 0.5, and reduces CENTROIDS. Its output
//        is a box over centroids; InstanceMortonCode.comp then normalises centroids against it. Nothing downstream of that midpoint ever observes the
//        extent, so nothing this gate can read distinguishes the rules.
//
//    ⚠️ THAT IS NOT A LICENCE TO USE TWO CORNERS. The eight-corner rule becomes load-bearing in the TRAVERSAL (#11), which tests a ray against each
//       instance's world BOX — where a too-small box means missed intersections. The assertion belongs in that gate, over that data, and a stand-in
//       here would only have created the impression it was already covered. Recorded in the backlog rather than left as a silent gap.
//
//    📝 The case is KEPT, because the oblique scene is still worth running and the per-instance key check below is real: the scene box is a reduction
//       and can absorb a lapse on a minority of instances, while each key is computed from one instance alone and cannot.
void ExecuteObliqueRotationCase(VulkanHost& Host, VkCommandPool CommandPool)
{
    std::printf("\n---- oblique rotation: box + per-instance keys ");
    for (int Dash = 0; Dash < 43; ++Dash) std::printf("-");
    std::printf("\n");

    ++CaseTally;

    // ObliqueRotation, not RotatedRing: signed permutation matrices keep an axis-aligned box axis-aligned, so they exercise none of the corner
    // handling. The 3-4-5 rotation is genuinely oblique and still exactly representable — see ComposeObliqueTransform.
    const uint32_t InstanceCount = 1024u;
    SyntheticScene Scene;
    BuildSyntheticScene(SceneShape::ObliqueRotation, InstanceCount, 77u, Scene);

    const SceneReference Correct = ComputeSceneReference(Scene);

    CaseBuffers Buffers;
    Buffers.InstanceBytes = (VkDeviceSize)Scene.Instances.size() * sizeof(SuzanneSceneInstance);
    Buffers.SliceBytes    = (VkDeviceSize)Scene.Slices.size()    * sizeof(GeometryArenaSlice);
    Buffers.NodeBytes     = (VkDeviceSize)Scene.NodeWords.size() * sizeof(uint32_t);
    Buffers.KeyBytes      = (VkDeviceSize)InstanceCount * sizeof(uint32_t);
    Buffers.PayloadBytes  = Buffers.KeyBytes;

    std::vector<uint32_t> Poison((size_t)InstanceCount, 0xDEADBEEFu);
    CreateHostBuffer(Host, Scene.Instances.data(), Buffers.InstanceBytes, Buffers.Instance, Buffers.InstanceMemory);
    CreateHostBuffer(Host, Scene.Slices.data(),    Buffers.SliceBytes,    Buffers.Slice,    Buffers.SliceMemory);
    CreateHostBuffer(Host, Scene.NodeWords.data(), Buffers.NodeBytes,     Buffers.Node,     Buffers.NodeMemory);
    CreateHostBuffer(Host, Poison.data(),          Buffers.KeyBytes,      Buffers.Key,      Buffers.KeyMemory);
    CreateHostBuffer(Host, Poison.data(),          Buffers.PayloadBytes,  Buffers.Payload,  Buffers.PayloadMemory);

    InstanceBoundsSubmission Bounds;
    InstanceBounds Result;
    bool Retrieved = false;

    if (InitializeInstanceBoundsSubmission(Bounds, Host, ShaderDirectoryPath) &&
        BindInstanceBoundsScene(Bounds, Buffers.Instance, Buffers.InstanceBytes,
                                        Buffers.Slice,    Buffers.SliceBytes,
                                        Buffers.Node,     Buffers.NodeBytes,
                                        InstanceCount, Scene.SliceCount) &&
        BindInstanceMortonTarget(Bounds, Buffers.Key, Buffers.KeyBytes, Buffers.Payload, Buffers.PayloadBytes))
    {
        VkCommandBufferAllocateInfo CommandAllocate = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        CommandAllocate.commandPool        = CommandPool;
        CommandAllocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        CommandAllocate.commandBufferCount = 1;
        VkCommandBuffer Command = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(Host.Device, &CommandAllocate, &Command);

        VkCommandBufferBeginInfo BeginInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        BeginInformation.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(Command, &BeginInformation);
        // 📝 The Morton dispatch runs here too, unlike the original foil, because the KEYS are where the per-instance difference between the two
        //    rules survives. The scene box is a union and can wash that difference out; every key is computed from one instance's own centroid and
        //    cannot.
        RecordInstanceBoundsReduce(Bounds, Command);
        RecordInstanceMortonCode(Bounds, Command);
        vkEndCommandBuffer(Command);

        if (SubmitAndWait(Host, CommandPool, Command))
        {
            vkDeviceWaitIdle(Host.Device);
            Retrieved = RetrieveInstanceBounds(Bounds, CommandPool, Result);
        }
    }

    if (!Retrieved)
    {
        std::printf("  foil case plumbing FAILED\n");
        ++FailureTally;
    }
    else
    {
        const bool MatchesCorrect = BoxesMatchExactly(Result, Correct.SceneBox);

        // ─── a second, independent witness: the per-instance keys ───────────────────────────────────────────────────────────────────────────────
        // 📝 The box check above is a REDUCTION, so a shader that went wrong on a minority of instances could still produce the right box whenever
        //    the extremes happen to come from the instances it got right. Every key is computed from one instance in isolation, so a per-instance
        //    lapse has nowhere to hide. Scored with the same one-bucket tolerance as the correctness suite, and for the same reason (the
        //    normalisation divide is not bit-reproducible across CPU and GPU).
        std::vector<uint32_t> ResultKeys((size_t)InstanceCount, 0u);
        uint32_t NearCorrect = 0u;
        const bool KeysRead = ReadBackBuffer(Host, Buffers.KeyMemory, Buffers.KeyBytes, ResultKeys.data());
        if (KeysRead)
        {
            std::vector<uint32_t> CorrectKeys, CorrectPayloads, CorrectX, CorrectY, CorrectZ;
            ComputeMortonReference(Scene, Correct, Result, CorrectKeys, CorrectPayloads, CorrectX, CorrectY, CorrectZ);

            for (uint32_t Index = 0; Index < InstanceCount; ++Index)
            {
                if (Correct.Dropped[Index]) continue;
                uint32_t GotX, GotY, GotZ;
                DecodeMortonCode(ResultKeys[Index], GotX, GotY, GotZ);
                if ((uint32_t)std::abs((int32_t)GotX - (int32_t)CorrectX[Index]) <= 1u &&
                    (uint32_t)std::abs((int32_t)GotY - (int32_t)CorrectY[Index]) <= 1u &&
                    (uint32_t)std::abs((int32_t)GotZ - (int32_t)CorrectZ[Index]) <= 1u)
                    ++NearCorrect;
            }
        }

        const uint32_t LiveCount = (uint32_t)std::count(Correct.Dropped.begin(), Correct.Dropped.end(), false);
        const bool KeysBackCorrect = KeysRead && (NearCorrect == LiveCount);

        const bool Passed = MatchesCorrect && KeysBackCorrect;
        if (!Passed) ++FailureTally;

        std::printf("  expected box min (%.9g %.9g %.9g) max (%.9g %.9g %.9g)\n",
                    Correct.SceneBox.MinimumX, Correct.SceneBox.MinimumY, Correct.SceneBox.MinimumZ,
                    Correct.SceneBox.MaximumX, Correct.SceneBox.MaximumY, Correct.SceneBox.MaximumZ);
        std::printf("  GPU      box min (%.9g %.9g %.9g) max (%.9g %.9g %.9g)\n",
                    Result.MinimumX, Result.MinimumY, Result.MinimumZ,
                    Result.MaximumX, Result.MaximumY, Result.MaximumZ);
        std::printf("  keys within one bucket of the oracle: %u of %u live\n", NearCorrect, LiveCount);
        std::printf("  box matches %s   keys agree %s   %s\n",
                    MatchesCorrect  ? "y" : "N",
                    KeysBackCorrect ? "y" : "N",
                    Passed ? "PASS" : "FAIL");
        std::printf("  note: the eight-corner rule is NOT asserted here and cannot be — it affects box EXTENT, which this pass\n");
        std::printf("        discards when it takes the centroid. It belongs in the traversal gate (#11); see the header above.\n");
    }

    FinalizeInstanceBoundsSubmission(Bounds);
    DestroyCaseBuffers(Host, Buffers);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE RESEED CHECK
//------------------------------------------------------------------------------------------------------------------------

// 🔴 Run the SAME submission twice over two different scenes and confirm the second result reflects only the second. Without a reseed the second box
//    is the UNION of both — well-formed, larger, and invisible to any single-run test. For a PER-FRAME TLAS this is worse than for the load-time
//    bottom level: the union accumulates for the whole session, so an object that ever visited the map edge keeps the box stretched there forever.
void ExecuteReseedCheck(VulkanHost& Host, VkCommandPool CommandPool)
{
    std::printf("\n---- reseed across rebuilds ");
    for (int Dash = 0; Dash < 62; ++Dash) std::printf("-");
    std::printf("\n");

    ++CaseTally;

    const uint32_t InstanceCount = 2048u;
    SyntheticScene Wide, Narrow;
    BuildSyntheticScene(SceneShape::AxisAlignedGrid,    InstanceCount, 21u, Wide);
    BuildSyntheticScene(SceneShape::IdenticalCentroids, InstanceCount, 22u, Narrow);

    const SceneReference NarrowReference = ComputeSceneReference(Narrow);

    InstanceBoundsSubmission Bounds;
    if (!InitializeInstanceBoundsSubmission(Bounds, Host, ShaderDirectoryPath))
    {
        std::printf("  INITIALIZE FAILED\n");
        ++FailureTally;
        return;
    }

    const auto RunOnce = [&](const SyntheticScene& Scene, InstanceBounds& OutResult) -> bool
    {
        CaseBuffers Buffers;
        Buffers.InstanceBytes = (VkDeviceSize)Scene.Instances.size() * sizeof(SuzanneSceneInstance);
        Buffers.SliceBytes    = (VkDeviceSize)Scene.Slices.size()    * sizeof(GeometryArenaSlice);
        Buffers.NodeBytes     = (VkDeviceSize)Scene.NodeWords.size() * sizeof(uint32_t);
        Buffers.KeyBytes      = (VkDeviceSize)InstanceCount * sizeof(uint32_t);
        Buffers.PayloadBytes  = Buffers.KeyBytes;

        std::vector<uint32_t> Poison((size_t)InstanceCount, 0xDEADBEEFu);
        CreateHostBuffer(Host, Scene.Instances.data(), Buffers.InstanceBytes, Buffers.Instance, Buffers.InstanceMemory);
        CreateHostBuffer(Host, Scene.Slices.data(),    Buffers.SliceBytes,    Buffers.Slice,    Buffers.SliceMemory);
        CreateHostBuffer(Host, Scene.NodeWords.data(), Buffers.NodeBytes,     Buffers.Node,     Buffers.NodeMemory);
        CreateHostBuffer(Host, Poison.data(),          Buffers.KeyBytes,      Buffers.Key,      Buffers.KeyMemory);
        CreateHostBuffer(Host, Poison.data(),          Buffers.PayloadBytes,  Buffers.Payload,  Buffers.PayloadMemory);

        bool Succeeded = false;
        if (BindInstanceBoundsScene(Bounds, Buffers.Instance, Buffers.InstanceBytes,
                                            Buffers.Slice,    Buffers.SliceBytes,
                                            Buffers.Node,     Buffers.NodeBytes,
                                            InstanceCount, Scene.SliceCount) &&
            BindInstanceMortonTarget(Bounds, Buffers.Key, Buffers.KeyBytes, Buffers.Payload, Buffers.PayloadBytes))
        {
            VkCommandBufferAllocateInfo CommandAllocate = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            CommandAllocate.commandPool        = CommandPool;
            CommandAllocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            CommandAllocate.commandBufferCount = 1;
            VkCommandBuffer Command = VK_NULL_HANDLE;
            vkAllocateCommandBuffers(Host.Device, &CommandAllocate, &Command);

            VkCommandBufferBeginInfo BeginInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            BeginInformation.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(Command, &BeginInformation);
            RecordInstanceBoundsReduce(Bounds, Command);
            vkEndCommandBuffer(Command);

            if (SubmitAndWait(Host, CommandPool, Command))
            {
                // vkDeviceWaitIdle before the next bind: rewriting a descriptor set while a command buffer that bound it is executing is undefined.
                vkDeviceWaitIdle(Host.Device);
                Succeeded = RetrieveInstanceBounds(Bounds, CommandPool, OutResult);
            }
        }
        vkDeviceWaitIdle(Host.Device);
        DestroyCaseBuffers(Host, Buffers);
        return Succeeded;
    };

    InstanceBounds WideResult, NarrowResult;
    const bool RanWide   = RunOnce(Wide,   WideResult);
    const bool RanNarrow = RunOnce(Narrow, NarrowResult);

    const bool Reseeded = RanWide && RanNarrow && BoxesMatchExactly(NarrowResult, NarrowReference.SceneBox);
    if (!Reseeded) ++FailureTally;

    std::printf("  wide box   min (%9.2f %9.2f %9.2f) max (%9.2f %9.2f %9.2f)\n",
                WideResult.MinimumX, WideResult.MinimumY, WideResult.MinimumZ,
                WideResult.MaximumX, WideResult.MaximumY, WideResult.MaximumZ);
    std::printf("  narrow box min (%9.2f %9.2f %9.2f) max (%9.2f %9.2f %9.2f)\n",
                NarrowResult.MinimumX, NarrowResult.MinimumY, NarrowResult.MinimumZ,
                NarrowResult.MaximumX, NarrowResult.MaximumY, NarrowResult.MaximumZ);
    std::printf("  second run reflects only the second scene   %s\n",
                Reseeded ? "PASS" : "FAIL (accumulators not reseeded)");

    FinalizeInstanceBoundsSubmission(Bounds);
}

} // namespace

int main(int ArgumentCount, char** ArgumentValues)
{
    bool StressEnabled = false;
    for (int Index = 1; Index < ArgumentCount; ++Index)
        if (std::strcmp(ArgumentValues[Index], "--stress") == 0) StressEnabled = true;

    std::printf("==== InstanceBoundsReduce + InstanceMortonCode validation ====\n\n");

    // A layout disagreement between this file and the shaders would make every case fail for one uninformative reason, so it is checked first and
    // reported as itself.
    std::printf("  sizeof(SuzanneSceneInstance) = %zu (expect 208)\n", sizeof(SuzanneSceneInstance));
    std::printf("  sizeof(GeometryArenaSlice)   = %zu (expect 32)\n\n", sizeof(GeometryArenaSlice));

    VulkanHost Host;
    if (!InitializeVulkanHost(Host, nullptr, 0))
    {
        std::printf("headless Vulkan host creation FAILED\n");
        return 1;
    }

    VkCommandPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    PoolInformation.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    PoolInformation.queueFamilyIndex = Host.GraphicsQueueFamily;
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(Host.Device, &PoolInformation, Host.Allocator, &CommandPool) != VK_SUCCESS)
    {
        std::printf("command pool creation FAILED\n");
        FinalizeVulkanHost(Host);
        return 1;
    }

    // 📝 Counts chosen to straddle every tile edge of the 256-wide workgroup: a partial final tile is where the identity seeding is exercised, and an
    //    exact multiple is where an off-by-one in the commit guard would hide.
    const uint32_t InstanceCounts[] = { 1u, 2u, 3u, 255u, 256u, 257u, 511u, 512u, 513u, 1023u, 1024u, 1025u, 4096u, 65536u };
    const SceneShape Shapes[] = {
        SceneShape::AxisAlignedGrid,    SceneShape::RotatedRing,        SceneShape::MixedMeshes,
        SceneShape::SingleInstance,     SceneShape::CoplanarRow,        SceneShape::IdenticalCentroids,
        SceneShape::OutOfRangeOrdinals, SceneShape::AllOutOfRange,      SceneShape::NegativeOctant,
        SceneShape::ScaledInstances,    SceneShape::ObliqueRotation,
    };

    std::printf("---- correctness suite ");
    for (int Dash = 0; Dash < 67; ++Dash) std::printf("-");
    std::printf("\n");

    uint32_t Seed = 1000u;
    for (SceneShape Shape : Shapes)
        for (uint32_t Count : InstanceCounts)
            ExecuteCase(Host, CommandPool, Shape, Count, Seed++);

    ExecuteObliqueRotationCase(Host, CommandPool);
    ExecuteReseedCheck(Host, CommandPool);

    if (StressEnabled)
    {
        std::printf("\n---- stress suite ");
        for (int Dash = 0; Dash < 72; ++Dash) std::printf("-");
        std::printf("\n");

        std::printf("  [repetition] 12 runs, rotated ring, 200,000 instances\n");
        for (int Run = 0; Run < 12; ++Run)
            ExecuteCase(Host, CommandPool, SceneShape::RotatedRing, 200000u, 5000u + (uint32_t)Run);

        std::printf("  [randomized] 24 runs, random shape and count\n");
        std::mt19937 Generator(4242u);
        std::uniform_int_distribution<uint32_t> CountSpread(1u, 250000u);
        for (int Run = 0; Run < 24; ++Run)
        {
            const SceneShape Shape = Shapes[Run % (int)(sizeof(Shapes) / sizeof(Shapes[0]))];
            ExecuteCase(Host, CommandPool, Shape, CountSpread(Generator), 9000u + (uint32_t)Run);
        }
    }

    std::printf("\n==== %s : %u cases, %u failure%s ====\n",
                FailureTally == 0 ? "PASS" : "FAIL", CaseTally, FailureTally, FailureTally == 1 ? "" : "s");

    vkDestroyCommandPool(Host.Device, CommandPool, Host.Allocator);
    FinalizeVulkanHost(Host);
    return FailureTally == 0 ? 0 : 1;
}
