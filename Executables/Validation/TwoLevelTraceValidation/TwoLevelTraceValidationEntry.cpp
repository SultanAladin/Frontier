/*==============================================================================================================================================
                                                     TWOLEVELTRACEVALIDATIONENTRY.CPP
==============================================================================================================================================*/
// 🧩 The exit gate for TwoLevelTrace.glsl: the ray walk that descends the TOP-level instance tree and, at each instance it reaches, the BOTTOM-level
//    mesh tree in that instance's local space. It runs the WHOLE chain on a real device — the four TLAS dispatches that build the instance tree,
//    then the trace dispatch over it — because a traversal is only meaningful over a tree the engine actually produces.
//
//    🔴 THE ORACLE IS CPU BRUTE FORCE AND THAT IS THE POINT. Every ray is answered a second time by looping every instance and every triangle and
//       keeping the closest — no acceleration structure, no early exit, no cleverness. It is slow and it is obviously correct, which is the only
//       kind of oracle worth having: an oracle that shares an idea with the thing it checks agrees with it in exactly the cases where both are
//       wrong. This one shares nothing but the ray-triangle formula, and even that is written out independently below.
//
//    🔴 THE MESHES ARE REAL TRIANGLES WITH REAL SAH TREES, NOT SYNTHETIC BOXES. InstanceTreeValidation builds fake single-node meshes with poison
//       words because it only ever reads their ROOT box; a traversal dereferences everything below that root, so it needs trees with genuine depth,
//       genuine relative child hops, and genuine leaf primitive ranges. BuildGeometryTree is called for real here, which also means the SHIPPED
//       builder is what the gate descends rather than a transcription of it.
//
//    ⚠️ WHAT THIS GATE CANNOT SEE: the descent ORDER. Both levels enter the nearer child first and defer the farther one, which is what makes the
//       shrinking distance limit prune anything — but an unordered walk returns the IDENTICAL closest hit, just after visiting more nodes. Every
//       assertion below would still pass. Ordering is a cost property and needs a node-visit counter to check, which is logged as follow-up rather
//       than bolted on here.
//
//    📝 Headless on purpose: InitializeVulkanHost never creates or queries a surface, so a zero extension count yields a compute device, no window.

#define _CRT_SECURE_NO_WARNINGS

#include "Graphics/Acceleration/InstanceBoundsSubmission.h"
#include "Graphics/Acceleration/InstanceTreeSubmission.h"
#include "Graphics/Acceleration/RadixSortSubmission.h"
#include "Graphics/Acceleration/GeometryArenaSubmission.h"
#include "Graphics/Acceleration/GeometryTreeBuild.h"
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

constexpr uint32_t WordsPerNode = InstanceTreeWordsPerNode;
constexpr uint32_t LeafFlag     = 0xFFFFu;

// 📝 The probe's local_size_x. Must match TwoLevelTraceProbe.comp or the final partial tile of rays is dispatched wrong.
constexpr uint32_t TraceLanesPerGroup = 64;

// 📝 RenderVertex is 32 B — position @0, normal @12, uv @24 — so a vertex is 8 floats and only the first three are read by the trace.
constexpr uint32_t VertexStrideFloats = 8;

// Where the shader modules land — ShaderPlan.ps1 writes each .spv beside its source.
const char* const ShaderDirectory = "Internal/Graphics/Acceleration/Shaders";

//------------------------------------------------------------------------------------------------------------------------
//                                                          SMALL GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

struct Vector3
{
    float X = 0.0f, Y = 0.0f, Z = 0.0f;
};

Vector3 Subtract(const Vector3& Left, const Vector3& Right)
{
    return Vector3{ Left.X - Right.X, Left.Y - Right.Y, Left.Z - Right.Z };
}

Vector3 CrossProduct(const Vector3& Left, const Vector3& Right)
{
    return Vector3{ Left.Y * Right.Z - Left.Z * Right.Y,
                    Left.Z * Right.X - Left.X * Right.Z,
                    Left.X * Right.Y - Left.Y * Right.X };
}

float DotProduct(const Vector3& Left, const Vector3& Right)
{
    return Left.X * Right.X + Left.Y * Right.Y + Left.Z * Right.Z;
}

struct Matrix4
{
    float M[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };   // column-major, matching SuzanneSceneInstance::Model
};

// Transform a POINT: the translation column participates.
Vector3 TransformPoint(const Matrix4& Transform, const Vector3& Point)
{
    return Vector3{ Transform.M[0] * Point.X + Transform.M[4] * Point.Y + Transform.M[8]  * Point.Z + Transform.M[12],
                    Transform.M[1] * Point.X + Transform.M[5] * Point.Y + Transform.M[9]  * Point.Z + Transform.M[13],
                    Transform.M[2] * Point.X + Transform.M[6] * Point.Y + Transform.M[10] * Point.Z + Transform.M[14] };
}

// Transform a DIRECTION: the translation column does not. Mirrors the shader's w=0 multiply.
Vector3 TransformDirection(const Matrix4& Transform, const Vector3& Direction)
{
    return Vector3{ Transform.M[0] * Direction.X + Transform.M[4] * Direction.Y + Transform.M[8]  * Direction.Z,
                    Transform.M[1] * Direction.X + Transform.M[5] * Direction.Y + Transform.M[9]  * Direction.Z,
                    Transform.M[2] * Direction.X + Transform.M[6] * Direction.Y + Transform.M[10] * Direction.Z };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       SCENE CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Shapes chosen for how they stress the TRAVERSAL, which is a different list again from the tree gate's. What matters here is what a RAY meets:
//    how deep it must descend, how many instances it passes through, and whether the two levels' differing child encodings can be told apart.
enum class SceneShape
{
    SeparatedRow,       // instances in a line along X, well apart: the baseline, and the case a wrong hop still passes
    DeepMesh,           // one mesh with enough triangles to force real SAH depth: where a wrong relative hop finally diverges
    OverlappingStack,   // instances overlapping along the ray: proves the CLOSEST hit wins, not merely A hit
    RotatedInstances,   // every instance rotated and scaled: proves InverseModel and the unnormalised-direction rule
    NonUniformScale,    // 🔴 differing scales per instance: the ONLY shape that catches a renormalised local direction
    MixedMeshes,        // several distinct meshes interleaved: proves the slice lookup, not just one mesh's tree
    SingleInstance,     // 🔴 zero internal nodes; the root IS a leaf
    EmptyScene          // 🔴 no instances at all: every ray must miss cleanly and nothing may be dereferenced
};

const char* DescribeShape(SceneShape Shape)
{
    switch (Shape)
    {
        case SceneShape::SeparatedRow:     return "separated-row";
        case SceneShape::DeepMesh:         return "deep-mesh";
        case SceneShape::OverlappingStack: return "overlapping-stack";
        case SceneShape::RotatedInstances: return "rotated-instances";
        case SceneShape::NonUniformScale:  return "non-uniform-scale";
        case SceneShape::MixedMeshes:      return "mixed-meshes";
        case SceneShape::SingleInstance:   return "single-instance";
        case SceneShape::EmptyScene:       return "empty-scene";
    }
    return "unknown";
}

// 📝 One mesh's geometry in its own local space, kept host-side so the oracle can loop it directly. Positions are the de-interleaved XYZ run
//    BuildGeometryTree takes; VertexData is the interleaved stride-32 form the shader reads.
struct MeshGeometry
{
    std::vector<float>    Positions;    // 3 floats per vertex, for the builder and the oracle
    std::vector<float>    VertexData;   // 8 floats per vertex (RenderVertex), for the device
    std::vector<uint32_t> Indices;      // 3 per triangle
    GeometryTree          Tree;
};

struct SyntheticScene
{
    std::vector<SuzanneSceneInstance> Instances;
    std::vector<Matrix4>              Models;         // parallel to Instances, for the oracle
    std::vector<MeshGeometry>         Meshes;
    std::vector<GeometryArenaSlice>   Slices;
    std::vector<uint32_t>             ArenaNodeWords;
    std::vector<uint32_t>             ArenaPrimitives;
    std::vector<float>                VertexStream;   // every mesh's VertexData, concatenated
    std::vector<uint32_t>             IndexStream;    // every mesh's Indices, concatenated
    uint32_t                          SliceCount = 0;
};

// Append one axis-aligned box as 12 triangles, at the given local centre and half-extent. Returns nothing — the mesh grows in place.
//
// 📝 A BOX RATHER THAN A TETRAHEDRON so that a ray along any axis meets two facing triangles, which is what makes the closest-hit test meaningful
//    (a single-sided shell would let a front/back mix-up pass). Every coordinate is a multiple of 0.25 and every scale a power of two, so the whole
//    scene is exactly representable and the distance comparison can be tight rather than generous.
void AppendBox(MeshGeometry& Mesh, float CentreX, float CentreY, float CentreZ, float HalfX, float HalfY, float HalfZ)
{
    const uint32_t BaseVertex = (uint32_t)(Mesh.Positions.size() / 3u);

    for (uint32_t Corner = 0; Corner < 8u; ++Corner)
    {
        const float X = CentreX + ((Corner & 1u) ? HalfX : -HalfX);
        const float Y = CentreY + ((Corner & 2u) ? HalfY : -HalfY);
        const float Z = CentreZ + ((Corner & 4u) ? HalfZ : -HalfZ);

        Mesh.Positions.push_back(X);
        Mesh.Positions.push_back(Y);
        Mesh.Positions.push_back(Z);

        // The interleaved RenderVertex form: position, then a placeholder normal and uv the trace never reads.
        Mesh.VertexData.push_back(X);    Mesh.VertexData.push_back(Y);    Mesh.VertexData.push_back(Z);
        Mesh.VertexData.push_back(0.0f); Mesh.VertexData.push_back(0.0f); Mesh.VertexData.push_back(1.0f);
        Mesh.VertexData.push_back(0.0f); Mesh.VertexData.push_back(0.0f);
    }

    // The six faces, two triangles each, corners indexed by the (x,y,z) bit pattern above.
    static const uint32_t Faces[12][3] =
    {
        {0,2,1}, {1,2,3},   // -Z
        {4,5,6}, {5,7,6},   // +Z
        {0,1,4}, {1,5,4},   // -Y
        {2,6,3}, {3,6,7},   // +Y
        {0,4,2}, {2,4,6},   // -X
        {1,3,5}, {3,5,7}    // +X
    };

    for (uint32_t Face = 0; Face < 12u; ++Face)
    {
        Mesh.Indices.push_back(BaseVertex + Faces[Face][0]);
        Mesh.Indices.push_back(BaseVertex + Faces[Face][1]);
        Mesh.Indices.push_back(BaseVertex + Faces[Face][2]);
    }
}

// A mesh of BoxCount boxes laid out along X inside the mesh's own local space. One box gives a 12-triangle tree that stays a single leaf; many
// boxes force the SAH builder to split, which is the only way to get genuine interior nodes and genuine relative child hops to traverse.
MeshGeometry MakeBoxCluster(uint32_t BoxCount, float Spacing, float HalfExtent)
{
    MeshGeometry Mesh;
    for (uint32_t Index = 0; Index < BoxCount; ++Index)
    {
        const float Centre = ((float)Index - (float)(BoxCount - 1u) * 0.5f) * Spacing;
        AppendBox(Mesh, Centre, 0.0f, 0.0f, HalfExtent, HalfExtent, HalfExtent);
    }
    return Mesh;
}

// A signed permutation matrix times a scale, plus a translation. Always exactly representable, and a genuine rotation/reflection.
Matrix4 ComposeTransform(uint32_t PermutationChoice, float ScaleX, float ScaleY, float ScaleZ,
                         float TranslateX, float TranslateY, float TranslateZ)
{
    static const uint32_t Permutations[6][3] = { {0,1,2}, {0,2,1}, {1,0,2}, {1,2,0}, {2,0,1}, {2,1,0} };
    const uint32_t* Order = Permutations[PermutationChoice % 6u];

    const float Signs[3] = { (PermutationChoice & 8u)  ? -1.0f : 1.0f,
                             (PermutationChoice & 16u) ? -1.0f : 1.0f,
                             (PermutationChoice & 32u) ? -1.0f : 1.0f };
    const float Scales[3] = { ScaleX, ScaleY, ScaleZ };

    Matrix4 Result;
    for (uint32_t Index = 0; Index < 16u; ++Index) Result.M[Index] = 0.0f;
    for (uint32_t SourceAxis = 0; SourceAxis < 3u; ++SourceAxis)
        Result.M[SourceAxis * 4u + Order[SourceAxis]] = Signs[SourceAxis] * Scales[SourceAxis];
    Result.M[12] = TranslateX;
    Result.M[13] = TranslateY;
    Result.M[14] = TranslateZ;
    Result.M[15] = 1.0f;
    return Result;
}

// 🔴 THE INVERSE IS BUILT ANALYTICALLY FROM THE SAME PARTS, NOT BY A NUMERIC INVERSION. A signed permutation times a scale inverts exactly: transpose
//    the permutation and reciprocate the scale. Every scale used here is a power of two, so the reciprocal is exact and Model * InverseModel is
//    bit-exact identity — which matters because a tolerance in the inverse would show up as a tolerance in every hit distance, and the gate could no
//    longer tell a real distance error from its own arithmetic. WorkspaceDocumentDecoder::ComposeInverseModelMatrix inverts the shipped path the same
//    way and for the same reason.
Matrix4 ComposeInverse(uint32_t PermutationChoice, float ScaleX, float ScaleY, float ScaleZ,
                       float TranslateX, float TranslateY, float TranslateZ)
{
    static const uint32_t Permutations[6][3] = { {0,1,2}, {0,2,1}, {1,0,2}, {1,2,0}, {2,0,1}, {2,1,0} };
    const uint32_t* Order = Permutations[PermutationChoice % 6u];

    const float Signs[3] = { (PermutationChoice & 8u)  ? -1.0f : 1.0f,
                             (PermutationChoice & 16u) ? -1.0f : 1.0f,
                             (PermutationChoice & 32u) ? -1.0f : 1.0f };
    const float Scales[3] = { ScaleX, ScaleY, ScaleZ };

    // The forward map sends local axis S to world axis Order[S], scaled by Signs[S]*Scales[S], then translates. The inverse undoes the translation
    // first, then sends world axis Order[S] back to local axis S with the reciprocal factor.
    Matrix4 Result;
    for (uint32_t Index = 0; Index < 16u; ++Index) Result.M[Index] = 0.0f;

    const float Translation[3] = { TranslateX, TranslateY, TranslateZ };
    for (uint32_t SourceAxis = 0; SourceAxis < 3u; ++SourceAxis)
    {
        const uint32_t TargetAxis = Order[SourceAxis];
        const float    Factor     = 1.0f / (Signs[SourceAxis] * Scales[SourceAxis]);

        Result.M[TargetAxis * 4u + SourceAxis] = Factor;
        Result.M[12 + SourceAxis]             -= Translation[TargetAxis] * Factor;
    }
    Result.M[15] = 1.0f;
    return Result;
}

// Assemble the whole scene: meshes, their SAH trees, the arena, the shared streams, and the placed instances.
bool BuildSyntheticScene(SceneShape Shape, uint32_t InstanceCount, uint32_t Seed, SyntheticScene& Out)
{
    Out = SyntheticScene();
    std::mt19937 Generator(Seed);

    // ─── the meshes ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    // 📝 Box counts chosen so the SAH builder produces genuinely different tree DEPTHS. MaxLeafSize is 10 triangles, so a 12-triangle single box is
    //    one split deep while a 32-box cluster (384 triangles) is several levels — and it is only at depth that reading the relative right-child hop
    //    as an absolute index starts naming a different node. A gate built entirely from single boxes would pass with that bug in place.
    switch (Shape)
    {
        case SceneShape::DeepMesh:
            Out.Meshes.push_back(MakeBoxCluster(32u, 4.0f, 1.0f));   // 384 triangles: real depth
            break;

        case SceneShape::MixedMeshes:
            Out.Meshes.push_back(MakeBoxCluster(1u,  0.0f, 2.0f));   // 12 triangles
            Out.Meshes.push_back(MakeBoxCluster(4u,  4.0f, 1.0f));   // 48
            Out.Meshes.push_back(MakeBoxCluster(16u, 2.0f, 0.5f));   // 192
            Out.Meshes.push_back(MakeBoxCluster(2u,  8.0f, 2.0f));   // 24
            break;

        default:
            Out.Meshes.push_back(MakeBoxCluster(1u, 0.0f, 2.0f));
            Out.Meshes.push_back(MakeBoxCluster(8u, 4.0f, 1.0f));    // 96 triangles: two or three levels
            break;
    }

    // ─── build each mesh's tree and lay it into the arena + the shared streams ───────────────────────────────────────────────────────────────
    GeometryTreeOptions Options;   // SAH, MaxLeafSize 10, MaxDepth 40 — the shipped defaults
    for (MeshGeometry& Mesh : Out.Meshes)
    {
        if (!BuildGeometryTree(Mesh.Positions.data(), (uint32_t)(Mesh.Positions.size() / 3u),
                               Mesh.Indices.data(),   (uint32_t)Mesh.Indices.size(),
                               Options, Mesh.Tree))
            return false;

        GeometryArenaSlice Slice = {};
        Slice.NodeOffset      = (uint32_t)Out.ArenaNodeWords.size();               // WORDS, per GeometryArenaSubmission.h
        Slice.NodeCount       = Mesh.Tree.NodeCount;
        Slice.PrimitiveOffset = (uint32_t)Out.ArenaPrimitives.size();
        Slice.PrimitiveCount  = (uint32_t)Mesh.Tree.PrimitiveOrder.size();
        Slice.VertexOffset    = (uint32_t)(Out.VertexStream.size() / VertexStrideFloats);
        Slice.IndexOffset     = (uint32_t)Out.IndexStream.size();
        Slice.ParentOffset    = GeometryTreeNoParent;                              // static mesh; never 0, which is a legitimate offset
        Out.Slices.push_back(Slice);

        Out.ArenaNodeWords.insert(Out.ArenaNodeWords.end(),  Mesh.Tree.NodeWords.begin(),      Mesh.Tree.NodeWords.end());
        Out.ArenaPrimitives.insert(Out.ArenaPrimitives.end(), Mesh.Tree.PrimitiveOrder.begin(), Mesh.Tree.PrimitiveOrder.end());
        Out.VertexStream.insert(Out.VertexStream.end(),      Mesh.VertexData.begin(),          Mesh.VertexData.end());
        Out.IndexStream.insert(Out.IndexStream.end(),        Mesh.Indices.begin(),             Mesh.Indices.end());
    }
    Out.SliceCount = (uint32_t)Out.Slices.size();

    const uint32_t MeshCount = (uint32_t)Out.Meshes.size();

    // ─── the instances ───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    const uint32_t PlacedCount = (Shape == SceneShape::EmptyScene)     ? 0u
                               : (Shape == SceneShape::SingleInstance) ? 1u
                               : InstanceCount;

    for (uint32_t Index = 0; Index < PlacedCount; ++Index)
    {
        SuzanneSceneInstance Instance;
        Instance.PartitionId = Index;
        Instance.MaterialId  = 0;
        Instance.MeshOrdinal = Index % MeshCount;

        uint32_t PermutationChoice = 0;
        float    ScaleX = 1.0f, ScaleY = 1.0f, ScaleZ = 1.0f;
        float    TranslateX = 0.0f, TranslateY = 0.0f, TranslateZ = 0.0f;

        switch (Shape)
        {
            case SceneShape::SeparatedRow:
            case SceneShape::DeepMesh:
            case SceneShape::MixedMeshes:
                // Spread far enough apart that each instance owns its own stretch of the X axis, so an axis-aligned probe ray meets exactly one.
                TranslateX = (float)Index * 64.0f - 512.0f;
                TranslateY = (float)((Index % 5u)) * 8.0f;
                TranslateZ = (float)((Index % 3u)) * 8.0f;
                break;

            case SceneShape::OverlappingStack:
                // 🔴 DELIBERATELY OVERLAPPING ALONG Z. A ray down -Z passes through every instance in the stack, so returning A hit is easy and
                //    returning the CLOSEST one is the actual claim. Any traversal that forgets to shrink the distance limit, or that compares
                //    against a stale limit, fails here and nowhere else in this list.
                TranslateX = (float)((Index % 4u)) * 2.0f;
                TranslateY = (float)((Index % 3u)) * 2.0f;
                TranslateZ = (float)Index * 8.0f;
                break;

            case SceneShape::RotatedInstances:
                PermutationChoice = 1u + (Index % 5u) + ((Index % 7u) << 3u);
                TranslateX = (float)Index * 48.0f - 384.0f;
                TranslateY = (float)((Index % 4u)) * 4.0f;
                TranslateZ = (float)((Index % 5u)) * 4.0f;
                break;

            case SceneShape::NonUniformScale:
            {
                // 🔴 THE SHAPE THAT CATCHES A RENORMALISED LOCAL DIRECTION, AND THE ONLY ONE THAT CAN. If the traversal normalises the direction
                //    after transforming it into local space, the distance it returns is measured in LOCAL units — so an instance scaled by 4 reports
                //    a hit at a quarter of its true world distance. Every instance still gets hit, every barycentric is still right, and the closest-
                //    hit contest is decided by scale rather than by distance. A uniformly-scaled scene cannot see this: there, local and world
                //    distances differ by one shared factor and the ORDERING survives.
                static const float ScaleChoices[4] = { 0.25f, 0.5f, 2.0f, 4.0f };
                ScaleX = ScaleChoices[Index % 4u];
                ScaleY = ScaleChoices[(Index / 4u) % 4u];
                ScaleZ = ScaleChoices[(Index / 16u) % 4u];
                TranslateX = (float)Index * 64.0f - 512.0f;
                TranslateY = 0.0f;
                TranslateZ = 0.0f;
                break;
            }

            // SingleInstance and EmptyScene need no placement rule of their own — the first sits at the origin-relative default below, and the
            // second never reaches this loop at all.
            default:
                TranslateX = (float)Index * 64.0f - 512.0f;
                break;
        }

        const Matrix4 Model   = ComposeTransform(PermutationChoice, ScaleX, ScaleY, ScaleZ, TranslateX, TranslateY, TranslateZ);
        const Matrix4 Inverse = ComposeInverse  (PermutationChoice, ScaleX, ScaleY, ScaleZ, TranslateX, TranslateY, TranslateZ);

        std::memcpy(Instance.Model,        Model.M,   sizeof(Model.M));
        std::memcpy(Instance.InverseModel, Inverse.M, sizeof(Inverse.M));

        Out.Instances.push_back(Instance);
        Out.Models.push_back(Model);
    }

    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            THE ORACLE
//------------------------------------------------------------------------------------------------------------------------

// 📝 One answer, from either side. The GPU's comes back through the readback buffer; the oracle's is computed below.
struct TraceAnswer
{
    float    Distance       = 0.0f;
    float    BarycentricU   = 0.0f;
    float    BarycentricV   = 0.0f;
    uint32_t InstanceIndex  = 0xFFFFFFFFu;
    uint32_t PrimitiveIndex = 0xFFFFFFFFu;
    bool     HitCondition   = false;
    bool     OverflowCondition = false;
};

// Möller–Trumbore again, written out independently of the shader's copy.
//
// 🔴 THE DUPLICATION IS DELIBERATE AND MUST NOT BE FACTORED AWAY. If the oracle called the same code the traversal does, the two would agree
//    precisely in the cases where that shared code is wrong — which is the one thing an oracle exists to rule out. This is the same reasoning that
//    keeps a probe from re-implementing a shipped kernel and then measuring itself.
bool OracleIntersectTriangle(const Vector3& Origin, const Vector3& Direction,
                             const Vector3& A, const Vector3& B, const Vector3& C,
                             float NearLimit, float FarLimit,
                             float& OutDistance, float& OutU, float& OutV)
{
    const Vector3 EdgeAB = Subtract(B, A);
    const Vector3 EdgeAC = Subtract(C, A);

    const Vector3 Normal      = CrossProduct(Direction, EdgeAC);
    const float   Determinant = DotProduct(EdgeAB, Normal);

    if (std::fabs(Determinant) < 1e-12f)
        return false;

    const float   InverseDeterminant = 1.0f / Determinant;
    const Vector3 ToOrigin           = Subtract(Origin, A);

    const float U = DotProduct(ToOrigin, Normal) * InverseDeterminant;
    if (U < 0.0f || U > 1.0f)
        return false;

    const Vector3 Perpendicular = CrossProduct(ToOrigin, EdgeAB);
    const float   V             = DotProduct(Direction, Perpendicular) * InverseDeterminant;
    if (V < 0.0f || U + V > 1.0f)
        return false;

    const float Candidate = DotProduct(EdgeAC, Perpendicular) * InverseDeterminant;
    if (Candidate <= NearLimit || Candidate > FarLimit)
        return false;

    OutDistance = Candidate;
    OutU        = U;
    OutV        = V;
    return true;
}

// 🔴 BRUTE FORCE: EVERY INSTANCE, EVERY TRIANGLE, NO TREE. This walks nothing and prunes nothing, which is exactly why it can be trusted to say what
//    the right answer is. It transforms the ray the same way the traversal claims to — through InverseModel, direction with w=0 and NOT renormalised
//    — because that convention is part of the SPECIFICATION being checked, not part of the traversal's implementation of it.
TraceAnswer OracleTrace(const SyntheticScene& Scene, const Vector3& Origin, const Vector3& Direction,
                        float NearLimit, float MaximumDistance)
{
    TraceAnswer Answer;
    Answer.Distance = MaximumDistance;

    float ClosestSoFar = MaximumDistance;

    for (uint32_t InstanceIndex = 0; InstanceIndex < (uint32_t)Scene.Instances.size(); ++InstanceIndex)
    {
        const SuzanneSceneInstance& Instance = Scene.Instances[InstanceIndex];
        if (Instance.MeshOrdinal >= Scene.SliceCount)
            continue;   // the same drop test every pass applies

        Matrix4 Inverse;
        std::memcpy(Inverse.M, Instance.InverseModel, sizeof(Inverse.M));

        const Vector3 LocalOrigin    = TransformPoint(Inverse, Origin);
        const Vector3 LocalDirection = TransformDirection(Inverse, Direction);   // NOT normalised — see the header

        const MeshGeometry& Mesh          = Scene.Meshes[Instance.MeshOrdinal];
        const uint32_t      TriangleCount = (uint32_t)(Mesh.Indices.size() / 3u);

        for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
        {
            const uint32_t IndexA = Mesh.Indices[Triangle * 3u + 0u];
            const uint32_t IndexB = Mesh.Indices[Triangle * 3u + 1u];
            const uint32_t IndexC = Mesh.Indices[Triangle * 3u + 2u];

            const Vector3 A{ Mesh.Positions[IndexA * 3u], Mesh.Positions[IndexA * 3u + 1u], Mesh.Positions[IndexA * 3u + 2u] };
            const Vector3 B{ Mesh.Positions[IndexB * 3u], Mesh.Positions[IndexB * 3u + 1u], Mesh.Positions[IndexB * 3u + 2u] };
            const Vector3 C{ Mesh.Positions[IndexC * 3u], Mesh.Positions[IndexC * 3u + 1u], Mesh.Positions[IndexC * 3u + 2u] };

            float Candidate = 0.0f, U = 0.0f, V = 0.0f;
            if (!OracleIntersectTriangle(LocalOrigin, LocalDirection, A, B, C, NearLimit, ClosestSoFar, Candidate, U, V))
                continue;

            ClosestSoFar          = Candidate;
            Answer.Distance       = Candidate;
            Answer.BarycentricU   = U;
            Answer.BarycentricV   = V;
            Answer.InstanceIndex  = InstanceIndex;
            Answer.PrimitiveIndex = Triangle;
            Answer.HitCondition   = true;
        }
    }

    return Answer;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            RAY SETS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One ray as the probe reads it: origin + near limit, direction + maximum distance. Matches TraceRay in TwoLevelTraceProbe.comp word for word.
struct DeviceRay
{
    float OriginX = 0.0f, OriginY = 0.0f, OriginZ = 0.0f, NearLimit = 0.0f;
    float DirectionX = 0.0f, DirectionY = 0.0f, DirectionZ = 0.0f, MaximumDistance = 0.0f;
};

// 📝 One hit as the probe writes it. Matches TraceResult; the flags are uints because a GLSL bool has no defined size in a storage block.
// 🔴 AnyHitFlag IS THE .w LANE OF DistanceAndBarycentric, carrying TraceAnyHitTwoLevel's boolean occlusion result (1.0 = occluded). It was the
//    probe's only unused output slot; assertion 8 asserts it equals HitFlag, which is the whole proof that the any-hit twin matches closest-hit.
struct DeviceResult
{
    float    Distance = 0.0f, BarycentricU = 0.0f, BarycentricV = 0.0f, AnyHitFlag = 0.0f;
    uint32_t InstanceIndex = 0, PrimitiveIndex = 0, HitFlag = 0, OverflowFlag = 0;
};

static_assert(sizeof(DeviceRay)    == 32, "DeviceRay must match TraceRay in TwoLevelTraceProbe.comp");
static_assert(sizeof(DeviceResult) == 32, "DeviceResult must match TraceResult in TwoLevelTraceProbe.comp");

const float DefaultMaximumDistance = 1.0e30f;

void PushRay(std::vector<DeviceRay>& Rays, const Vector3& Origin, const Vector3& Direction,
             float NearLimit = 0.0f, float MaximumDistance = DefaultMaximumDistance)
{
    DeviceRay Ray;
    Ray.OriginX = Origin.X; Ray.OriginY = Origin.Y; Ray.OriginZ = Origin.Z; Ray.NearLimit = NearLimit;
    Ray.DirectionX = Direction.X; Ray.DirectionY = Direction.Y; Ray.DirectionZ = Direction.Z;
    Ray.MaximumDistance = MaximumDistance;
    Rays.push_back(Ray);
}

// 🔴 THE AXIS-ALIGNED RAYS ARE NOT FILLER — THEY ARE THE NaN CASE. A direction with a zero component has an infinite reciprocal, and if the origin
//    also sits on a slab plane the slab test computes 0 * inf = NaN. Every comparison against NaN is false, so a naive box test REJECTS the box and
//    the ray misses geometry it passes straight through. Random directions never produce an exact zero, so a ray set without these cannot see it.
void BuildRaySet(const SyntheticScene& Scene, uint32_t Seed, std::vector<DeviceRay>& Rays)
{
    Rays.clear();
    std::mt19937 Generator(Seed);

    // ─── axis-aligned sweeps, both directions on all three axes ─────────────────────────────────────────────────────────────────────────────
    for (int32_t Step = -8; Step <= 8; ++Step)
    {
        const float Offset = (float)Step * 4.0f;

        PushRay(Rays, Vector3{ -4096.0f, Offset,   0.0f    }, Vector3{  1.0f, 0.0f, 0.0f });
        PushRay(Rays, Vector3{  4096.0f, Offset,   0.0f    }, Vector3{ -1.0f, 0.0f, 0.0f });
        PushRay(Rays, Vector3{  Offset,  -4096.0f, 0.0f    }, Vector3{  0.0f, 1.0f, 0.0f });
        PushRay(Rays, Vector3{  Offset,   4096.0f, 0.0f    }, Vector3{  0.0f,-1.0f, 0.0f });
        PushRay(Rays, Vector3{  Offset,   0.0f,   -4096.0f }, Vector3{  0.0f, 0.0f, 1.0f });
        PushRay(Rays, Vector3{  Offset,   0.0f,    4096.0f }, Vector3{  0.0f, 0.0f,-1.0f });
    }

    // ─── rays aimed straight at each instance's placed centre, from a shell around it ────────────────────────────────────────────────────────
    // 📝 These are the rays most likely to HIT, which matters because a gate made only of random rays is mostly a miss/miss agreement test — and
    //    miss/miss agrees even when the traversal is completely broken.
    for (uint32_t InstanceIndex = 0; InstanceIndex < (uint32_t)Scene.Instances.size(); ++InstanceIndex)
    {
        const Matrix4& Model  = Scene.Models[InstanceIndex];
        const Vector3  Centre = TransformPoint(Model, Vector3{ 0.0f, 0.0f, 0.0f });

        static const Vector3 ShellDirections[6] =
        {
            {  1.0f,  0.0f,  0.0f }, { -1.0f,  0.0f,  0.0f },
            {  0.0f,  1.0f,  0.0f }, {  0.0f, -1.0f,  0.0f },
            {  0.0f,  0.0f,  1.0f }, {  0.0f,  0.0f, -1.0f }
        };

        for (uint32_t Which = 0; Which < 6u; ++Which)
        {
            const Vector3 Direction = ShellDirections[Which];
            const Vector3 Origin{ Centre.X - Direction.X * 256.0f,
                                  Centre.Y - Direction.Y * 256.0f,
                                  Centre.Z - Direction.Z * 256.0f };
            PushRay(Rays, Origin, Direction);
        }

        // 🔴 A RAY STARTING INSIDE THE GEOMETRY. GI rays are fired FROM surfaces, so the origin sits on or within the hull and the first thing the
        //    ray meets may be a backface. A single-sided triangle test passes every other case in this file and fails this one.
        PushRay(Rays, Centre, Vector3{ 0.30f, 0.55f, 0.78f });
        PushRay(Rays, Centre, Vector3{ -0.62f, 0.19f, -0.76f });
    }

    // ─── oblique rays through the scene at large ────────────────────────────────────────────────────────────────────────────────────────────
    std::uniform_real_distribution<float> Spread(-1.0f, 1.0f);
    for (uint32_t Index = 0; Index < 192u; ++Index)
    {
        const Vector3 Origin{ Spread(Generator) * 1024.0f, Spread(Generator) * 1024.0f, Spread(Generator) * 1024.0f };

        Vector3 Direction{ Spread(Generator), Spread(Generator), Spread(Generator) };
        const float Length = std::sqrt(DotProduct(Direction, Direction));
        if (Length < 1e-6f) continue;
        Direction.X /= Length; Direction.Y /= Length; Direction.Z /= Length;

        PushRay(Rays, Origin, Direction);
    }

    // ─── grazing rays along the empty corridor between instances ────────────────────────────────────────────────────────────────────────────
    // 📝 The instances in most shapes are spaced 64 apart along X; these run down the gaps. A traversal that follows a wrong child hop tends to
    //    report hits HERE — in empty space — which is a far louder signal than a missed hit.
    for (int32_t Slot = -8; Slot <= 8; ++Slot)
    {
        const float Corridor = (float)Slot * 64.0f - 32.0f;
        PushRay(Rays, Vector3{ Corridor, -4096.0f, 0.0f }, Vector3{ 0.0f, 1.0f, 0.0f });
        PushRay(Rays, Vector3{ Corridor, 0.0f, -4096.0f }, Vector3{ 0.0f, 0.0f, 1.0f });
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

// A host-visible storage buffer, for the scene inputs the harness feeds in. Simpler than device-local + staging, and this is a harness.
bool CreateHostBufferWithUsage(VulkanHost& Host, const void* SourceData, VkDeviceSize Bytes,
                               VkBufferUsageFlags Usage, VkBuffer& OutBuffer, VkDeviceMemory& OutMemory)
{
    OutBuffer = VK_NULL_HANDLE;
    OutMemory = VK_NULL_HANDLE;
    if (Bytes == 0) return false;

    VkBufferCreateInfo BufferInformation = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    BufferInformation.size        = Bytes;
    BufferInformation.usage       = Usage;
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

bool CreateHostBuffer(VulkanHost& Host, const void* SourceData, VkDeviceSize Bytes,
                      VkBuffer& OutBuffer, VkDeviceMemory& OutMemory)
{
    return CreateHostBufferWithUsage(Host, SourceData, Bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, OutBuffer, OutMemory);
}

bool ReadHostBytes(VulkanHost& Host, VkDeviceMemory Memory, VkDeviceSize Bytes, void* OutData)
{
    void* Mapped = nullptr;
    if (vkMapMemory(Host.Device, Memory, 0, Bytes, 0, &Mapped) != VK_SUCCESS || Mapped == nullptr)
        return false;
    std::memcpy(OutData, Mapped, (size_t)Bytes);
    vkUnmapMemory(Host.Device, Memory);
    return true;
}

// Submit a recorded command buffer and wait, bounded. A hang must be a reported OUTCOME, not an unattended process.
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

// Load a .spv and wrap it in a shader module.
bool LoadShaderModule(VulkanHost& Host, const char* Path, VkShaderModule& OutModule)
{
    OutModule = VK_NULL_HANDLE;

    std::FILE* File = std::fopen(Path, "rb");
    if (File == nullptr) return false;

    std::fseek(File, 0, SEEK_END);
    const long Length = std::ftell(File);
    std::fseek(File, 0, SEEK_SET);
    if (Length <= 0 || (Length % 4) != 0) { std::fclose(File); return false; }

    std::vector<uint32_t> Code((size_t)Length / 4u, 0u);
    const size_t ReadCount = std::fread(Code.data(), 1, (size_t)Length, File);
    std::fclose(File);
    if (ReadCount != (size_t)Length) return false;

    VkShaderModuleCreateInfo ModuleInformation = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ModuleInformation.codeSize = (size_t)Length;
    ModuleInformation.pCode    = Code.data();
    return vkCreateShaderModule(Host.Device, &ModuleInformation, Host.Allocator, &OutModule) == VK_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE TRACE PIPELINE
//------------------------------------------------------------------------------------------------------------------------

// 📝 The probe's push block, matching TwoLevelTraceProbeConstants byte for byte.
struct TraceProbeConstants
{
    uint32_t InstanceCount = 0;
    uint32_t SliceCount    = 0;
    uint32_t RayCount      = 0;
};

constexpr uint32_t TraceBindingCount = 9;

struct TracePipeline
{
    VkPipeline            Pipeline   = VK_NULL_HANDLE;
    VkDescriptorSetLayout SetLayout  = VK_NULL_HANDLE;
    VkPipelineLayout      Layout     = VK_NULL_HANDLE;
    VkDescriptorPool      Pool       = VK_NULL_HANDLE;
    VkDescriptorSet       Set        = VK_NULL_HANDLE;
    bool                  ReadyCondition = false;
};

void FinalizeTracePipeline(VulkanHost& Host, TracePipeline& Trace)
{
    if (Trace.Pipeline  != VK_NULL_HANDLE) vkDestroyPipeline(Host.Device, Trace.Pipeline, Host.Allocator);
    if (Trace.Layout    != VK_NULL_HANDLE) vkDestroyPipelineLayout(Host.Device, Trace.Layout, Host.Allocator);
    if (Trace.SetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(Host.Device, Trace.SetLayout, Host.Allocator);
    if (Trace.Pool      != VK_NULL_HANDLE) vkDestroyDescriptorPool(Host.Device, Trace.Pool, Host.Allocator);
    Trace = TracePipeline();
}

bool InitializeTracePipeline(VulkanHost& Host, TracePipeline& Trace)
{
    Trace = TracePipeline();

    VkDescriptorSetLayoutBinding Bindings[TraceBindingCount] = {};
    for (uint32_t Index = 0; Index < TraceBindingCount; ++Index)
    {
        Bindings[Index].binding         = Index;
        Bindings[Index].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Bindings[Index].descriptorCount = 1;
        Bindings[Index].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo SetLayoutInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    SetLayoutInformation.bindingCount = TraceBindingCount;
    SetLayoutInformation.pBindings    = Bindings;
    if (vkCreateDescriptorSetLayout(Host.Device, &SetLayoutInformation, Host.Allocator, &Trace.SetLayout) != VK_SUCCESS)
        return false;

    VkPushConstantRange PushRange = {};
    PushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    PushRange.offset     = 0;
    PushRange.size       = sizeof(TraceProbeConstants);

    VkPipelineLayoutCreateInfo LayoutInformation = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    LayoutInformation.setLayoutCount         = 1;
    LayoutInformation.pSetLayouts            = &Trace.SetLayout;
    LayoutInformation.pushConstantRangeCount = 1;
    LayoutInformation.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Host.Device, &LayoutInformation, Host.Allocator, &Trace.Layout) != VK_SUCCESS)
    {
        FinalizeTracePipeline(Host, Trace);
        return false;
    }

    std::string ModulePath = std::string(ShaderDirectory) + "/TwoLevelTraceProbe.comp.spv";
    VkShaderModule Module = VK_NULL_HANDLE;
    if (!LoadShaderModule(Host, ModulePath.c_str(), Module))
    {
        FinalizeTracePipeline(Host, Trace);
        return false;
    }

    VkComputePipelineCreateInfo PipelineInformation = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    PipelineInformation.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    PipelineInformation.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    PipelineInformation.stage.module = Module;
    PipelineInformation.stage.pName  = "main";
    PipelineInformation.layout       = Trace.Layout;

    const VkResult PipelineResult = vkCreateComputePipelines(Host.Device, VK_NULL_HANDLE, 1, &PipelineInformation,
                                                             Host.Allocator, &Trace.Pipeline);
    vkDestroyShaderModule(Host.Device, Module, Host.Allocator);
    if (PipelineResult != VK_SUCCESS)
    {
        FinalizeTracePipeline(Host, Trace);
        return false;
    }

    VkDescriptorPoolSize PoolSize = {};
    PoolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    PoolSize.descriptorCount = TraceBindingCount;

    VkDescriptorPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    PoolInformation.maxSets       = 1;
    PoolInformation.poolSizeCount = 1;
    PoolInformation.pPoolSizes    = &PoolSize;
    if (vkCreateDescriptorPool(Host.Device, &PoolInformation, Host.Allocator, &Trace.Pool) != VK_SUCCESS)
    {
        FinalizeTracePipeline(Host, Trace);
        return false;
    }

    VkDescriptorSetAllocateInfo SetAllocate = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    SetAllocate.descriptorPool     = Trace.Pool;
    SetAllocate.descriptorSetCount = 1;
    SetAllocate.pSetLayouts        = &Trace.SetLayout;
    if (vkAllocateDescriptorSets(Host.Device, &SetAllocate, &Trace.Set) != VK_SUCCESS)
    {
        FinalizeTracePipeline(Host, Trace);
        return false;
    }

    Trace.ReadyCondition = true;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            ONE CASE
//------------------------------------------------------------------------------------------------------------------------

uint32_t FailureTally = 0;
uint32_t CaseTally    = 0;

struct CaseBuffers
{
    VkBuffer       Instance = VK_NULL_HANDLE;  VkDeviceMemory InstanceMemory = VK_NULL_HANDLE;  VkDeviceSize InstanceBytes = 0;
    VkBuffer       Slice    = VK_NULL_HANDLE;  VkDeviceMemory SliceMemory    = VK_NULL_HANDLE;  VkDeviceSize SliceBytes    = 0;
    VkBuffer       Node     = VK_NULL_HANDLE;  VkDeviceMemory NodeMemory     = VK_NULL_HANDLE;  VkDeviceSize NodeBytes     = 0;
    VkBuffer       Primitive= VK_NULL_HANDLE;  VkDeviceMemory PrimitiveMemory= VK_NULL_HANDLE;  VkDeviceSize PrimitiveBytes= 0;
    VkBuffer       Vertex   = VK_NULL_HANDLE;  VkDeviceMemory VertexMemory   = VK_NULL_HANDLE;  VkDeviceSize VertexBytes   = 0;
    VkBuffer       Index    = VK_NULL_HANDLE;  VkDeviceMemory IndexMemory    = VK_NULL_HANDLE;  VkDeviceSize IndexBytes    = 0;
    VkBuffer       Ray      = VK_NULL_HANDLE;  VkDeviceMemory RayMemory      = VK_NULL_HANDLE;  VkDeviceSize RayBytes      = 0;
    VkBuffer       Result   = VK_NULL_HANDLE;  VkDeviceMemory ResultMemory   = VK_NULL_HANDLE;  VkDeviceSize ResultBytes   = 0;
};

void DestroyCaseBuffers(VulkanHost& Host, CaseBuffers& Buffers)
{
    const auto Release = [&](VkBuffer& Buffer, VkDeviceMemory& Memory)
    {
        if (Buffer != VK_NULL_HANDLE) vkDestroyBuffer(Host.Device, Buffer, Host.Allocator);
        if (Memory != VK_NULL_HANDLE) vkFreeMemory(Host.Device, Memory, Host.Allocator);
        Buffer = VK_NULL_HANDLE;
        Memory = VK_NULL_HANDLE;
    };

    Release(Buffers.Instance,  Buffers.InstanceMemory);
    Release(Buffers.Slice,     Buffers.SliceMemory);
    Release(Buffers.Node,      Buffers.NodeMemory);
    Release(Buffers.Primitive, Buffers.PrimitiveMemory);
    Release(Buffers.Vertex,    Buffers.VertexMemory);
    Release(Buffers.Index,     Buffers.IndexMemory);
    Release(Buffers.Ray,       Buffers.RayMemory);
    Release(Buffers.Result,    Buffers.ResultMemory);
}

// Run the whole chain for one scene + ray set: build the TLAS on the device, then trace every ray over it, and hand back what the GPU said.
//
// 🔴 THE TRACE IS RECORDED INTO THE SAME COMMAND BUFFER AS THE FOUR BUILD DISPATCHES, AFTER A BARRIER. The refit writes the node boxes the trace
//    reads, and two compute dispatches in one command buffer are NOT ordered by recording order — without the barrier the trace is free to descend a
//    tree whose boxes are still in flight. That is exactly the seam that was missing between the Morton emit and the sort in the TLAS work, and it
//    fails the same silent way: a well-formed walk over partially-written data, wrong differently each run rather than obviously broken.
bool ExecuteChain(VulkanHost&                Host,
                  VkCommandPool              CommandPool,
                  TracePipeline&             Trace,
                  const SyntheticScene&      Scene,
                  const std::vector<DeviceRay>& Rays,
                  std::vector<DeviceResult>& OutResults,
                  const char*&               OutFailureReason)
{
    OutFailureReason = nullptr;

    const uint32_t InstanceCount = (uint32_t)Scene.Instances.size();
    const uint32_t RayCount      = (uint32_t)Rays.size();

    OutResults.assign(RayCount, DeviceResult());

    CaseBuffers Buffers;
    const auto Abandon = [&](const char* Reason) -> bool
    {
        OutFailureReason = Reason;
        DestroyCaseBuffers(Host, Buffers);
        return false;
    };

    // 📝 An empty scene still needs every buffer to EXIST — a descriptor write of VK_NULL_HANDLE is invalid, and the empty-scene assertion is
    //    precisely about the trace returning cleanly rather than the harness refusing to run. One padding element keeps each allocation legal.
    std::vector<SuzanneSceneInstance> InstanceUpload = Scene.Instances;
    if (InstanceUpload.empty()) InstanceUpload.resize(1);

    std::vector<uint32_t> PrimitiveUpload = Scene.ArenaPrimitives;
    if (PrimitiveUpload.empty()) PrimitiveUpload.resize(1, 0u);

    Buffers.InstanceBytes  = (VkDeviceSize)InstanceUpload.size()       * sizeof(SuzanneSceneInstance);
    Buffers.SliceBytes     = (VkDeviceSize)Scene.Slices.size()         * sizeof(GeometryArenaSlice);
    Buffers.NodeBytes      = (VkDeviceSize)Scene.ArenaNodeWords.size() * sizeof(uint32_t);
    Buffers.PrimitiveBytes = (VkDeviceSize)PrimitiveUpload.size()      * sizeof(uint32_t);
    Buffers.VertexBytes    = (VkDeviceSize)Scene.VertexStream.size()   * sizeof(float);
    Buffers.IndexBytes     = (VkDeviceSize)Scene.IndexStream.size()    * sizeof(uint32_t);
    Buffers.RayBytes       = (VkDeviceSize)RayCount                    * sizeof(DeviceRay);
    Buffers.ResultBytes    = (VkDeviceSize)RayCount                    * sizeof(DeviceResult);

    if (!CreateHostBuffer(Host, InstanceUpload.data(),        Buffers.InstanceBytes,  Buffers.Instance,  Buffers.InstanceMemory) ||
        !CreateHostBuffer(Host, Scene.Slices.data(),          Buffers.SliceBytes,     Buffers.Slice,     Buffers.SliceMemory) ||
        !CreateHostBuffer(Host, Scene.ArenaNodeWords.data(),  Buffers.NodeBytes,      Buffers.Node,      Buffers.NodeMemory) ||
        !CreateHostBuffer(Host, PrimitiveUpload.data(),       Buffers.PrimitiveBytes, Buffers.Primitive, Buffers.PrimitiveMemory) ||
        !CreateHostBuffer(Host, Scene.VertexStream.data(),    Buffers.VertexBytes,    Buffers.Vertex,    Buffers.VertexMemory) ||
        !CreateHostBuffer(Host, Scene.IndexStream.data(),     Buffers.IndexBytes,     Buffers.Index,     Buffers.IndexMemory) ||
        !CreateHostBuffer(Host, Rays.data(),                  Buffers.RayBytes,       Buffers.Ray,       Buffers.RayMemory) ||
        !CreateHostBuffer(Host, nullptr,                      Buffers.ResultBytes,    Buffers.Result,    Buffers.ResultMemory))
        return Abandon("scene buffer creation failed");

    // ─── the TLAS build submissions ─────────────────────────────────────────────────────────────────────────────────────────────────────────
    InstanceBoundsSubmission Bounds;
    RadixSortSubmission      Sort;
    InstanceTreeSubmission   Tree;

    const auto Teardown = [&]()
    {
        FinalizeInstanceTreeSubmission(Tree);
        FinalizeRadixSortSubmission(Sort);
        FinalizeInstanceBoundsSubmission(Bounds);
        DestroyCaseBuffers(Host, Buffers);
    };

    // 🔴 AN EMPTY SCENE SKIPS THE BUILD ENTIRELY AND STILL TRACES. There is no tree to build over zero instances — the submissions would refuse —
    //    but the trace must still run and must still miss cleanly. Short-circuiting the whole case instead would make the empty-scene assertion
    //    vacuous: it would prove the harness can skip, not that the traversal is safe.
    const bool BuildRequired = InstanceCount > 0u;

    if (BuildRequired)
    {
        if (!InitializeInstanceBoundsSubmission(Bounds, Host, ShaderDirectory)) { Teardown(); OutFailureReason = "InstanceBounds init failed"; return false; }
        if (!InitializeRadixSortSubmission(Sort, Host, InstanceCount, ShaderDirectory)) { Teardown(); OutFailureReason = "RadixSort init failed"; return false; }
        if (!InitializeInstanceTreeSubmission(Tree, Host, InstanceCount, ShaderDirectory)) { Teardown(); OutFailureReason = "InstanceTree init failed"; return false; }

        VkBuffer MortonKeyBuffer = VK_NULL_HANDLE, MortonPayloadBuffer = VK_NULL_HANDLE;
        RetrieveRadixSortInputBuffers(Sort, MortonKeyBuffer, MortonPayloadBuffer);

        const VkDeviceSize KeyBytes = (VkDeviceSize)InstanceCount * sizeof(uint32_t);

        if (!BindInstanceBoundsScene(Bounds, Buffers.Instance, Buffers.InstanceBytes, Buffers.Slice, Buffers.SliceBytes,
                                     Buffers.Node, Buffers.NodeBytes, InstanceCount, Scene.SliceCount) ||
            !BindInstanceMortonTarget(Bounds, MortonKeyBuffer, KeyBytes, MortonPayloadBuffer, KeyBytes) ||
            !SetRadixSortKeyCount(Sort, InstanceCount))
        { Teardown(); OutFailureReason = "bounds/sort bind failed"; return false; }

        // 🔴 THE SORT'S RESULT PAIR, NOT THE PRIMARY PAIR BY NAME — the sort ping-pongs.
        VkBuffer SortedKeyBuffer = VK_NULL_HANDLE, SortedPayloadBuffer = VK_NULL_HANDLE;
        RetrieveRadixSortedBuffers(Sort, SortedKeyBuffer, SortedPayloadBuffer);

        if (!BindInstanceTreeSorted(Tree, SortedKeyBuffer, KeyBytes, SortedPayloadBuffer, KeyBytes, InstanceCount) ||
            !BindInstanceTreeScene(Tree, Buffers.Instance, Buffers.InstanceBytes, Buffers.Slice, Buffers.SliceBytes,
                                   Buffers.Node, Buffers.NodeBytes, SortedPayloadBuffer, KeyBytes, Scene.SliceCount))
        { Teardown(); OutFailureReason = "tree bind failed"; return false; }
    }

    // ─── point the trace at everything ──────────────────────────────────────────────────────────────────────────────────────────────────────
    // 📝 For an empty scene the top-level node buffer does not exist, so the arena's node buffer stands in. The trace never reads it — InstanceCount
    //    is 0 and TraceTwoLevel returns before touching a node — but the descriptor must still be valid.
    VkBuffer TreeNodeBuffer = VK_NULL_HANDLE, TreeParentBuffer = VK_NULL_HANDLE;
    if (BuildRequired) RetrieveInstanceTreeBuffers(Tree, TreeNodeBuffer, TreeParentBuffer);
    if (TreeNodeBuffer == VK_NULL_HANDLE) TreeNodeBuffer = Buffers.Node;

    VkBuffer BoundBuffers[TraceBindingCount] =
    {
        Buffers.Instance, Buffers.Slice, Buffers.Node, Buffers.Primitive,
        TreeNodeBuffer, Buffers.Index, Buffers.Vertex, Buffers.Ray, Buffers.Result
    };

    VkDescriptorBufferInfo BufferInfos[TraceBindingCount] = {};
    VkWriteDescriptorSet   Writes[TraceBindingCount]      = {};
    for (uint32_t Index = 0; Index < TraceBindingCount; ++Index)
    {
        BufferInfos[Index].buffer = BoundBuffers[Index];
        BufferInfos[Index].offset = 0;
        BufferInfos[Index].range  = VK_WHOLE_SIZE;

        Writes[Index].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Writes[Index].dstSet          = Trace.Set;
        Writes[Index].dstBinding      = Index;
        Writes[Index].descriptorCount = 1;
        Writes[Index].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Writes[Index].pBufferInfo     = &BufferInfos[Index];
    }
    vkUpdateDescriptorSets(Host.Device, TraceBindingCount, Writes, 0, nullptr);

    // ─── record: build, barrier, trace ──────────────────────────────────────────────────────────────────────────────────────────────────────
    VkCommandBufferAllocateInfo CommandAllocate = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    CommandAllocate.commandPool        = CommandPool;
    CommandAllocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    CommandAllocate.commandBufferCount = 1;
    VkCommandBuffer Command = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(Host.Device, &CommandAllocate, &Command) != VK_SUCCESS)
    { Teardown(); OutFailureReason = "command buffer allocation failed"; return false; }

    VkCommandBufferBeginInfo BeginInformation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    BeginInformation.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(Command, &BeginInformation);

    if (BuildRequired)
    {
        RecordInstanceBoundsReduce(Bounds, Command);
        RecordInstanceMortonCode(Bounds, Command);
        RecordRadixSort(Sort, Command);
        RecordInstanceTreeBuild(Tree, Command);
        RecordInstanceTreeRefit(Tree, Command);

        // 🔴 THE SEAM BETWEEN THE REFIT AND THE TRACE. See the function header: the refit's box writes must be visible to the trace's reads, and
        //    recording order alone does not establish that.
        VkBufferMemoryBarrier TreeBarrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
        TreeBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        TreeBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        TreeBarrier.buffer              = TreeNodeBuffer;
        TreeBarrier.offset              = 0;
        TreeBarrier.size                = VK_WHOLE_SIZE;
        TreeBarrier.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        TreeBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(Command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 1, &TreeBarrier, 0, nullptr);
    }

    TraceProbeConstants Constants;
    Constants.InstanceCount = InstanceCount;
    Constants.SliceCount    = Scene.SliceCount;
    Constants.RayCount      = RayCount;

    vkCmdBindPipeline(Command, VK_PIPELINE_BIND_POINT_COMPUTE, Trace.Pipeline);
    vkCmdBindDescriptorSets(Command, VK_PIPELINE_BIND_POINT_COMPUTE, Trace.Layout, 0, 1, &Trace.Set, 0, nullptr);
    vkCmdPushConstants(Command, Trace.Layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants), &Constants);
    vkCmdDispatch(Command, (RayCount + TraceLanesPerGroup - 1u) / TraceLanesPerGroup, 1, 1);

    vkEndCommandBuffer(Command);

    if (!SubmitAndWait(Host, CommandPool, Command))
    { Teardown(); OutFailureReason = "SUBMIT TIMED OUT (30s) — a traversal that never terminates"; return false; }

    if (!ReadHostBytes(Host, Buffers.ResultMemory, Buffers.ResultBytes, OutResults.data()))
    { Teardown(); OutFailureReason = "result readback failed"; return false; }

    Teardown();
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         THE ASSERTIONS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 THE TOLERANCE IS SCALED TO THE OPERANDS, NOT TO THE COORDINATE SYSTEM. A hit 4000 units away carries proportionally more rounding than one at
//    4 units, so a single absolute epsilon is simultaneously too tight far away and too loose close up — and "too loose close up" is where a real
//    error would hide. Comparing against the magnitude of the values themselves keeps the check equally strict everywhere.
bool WithinRoundingTolerance(float Left, float Right, float RelativeTolerance = 1e-4f, float AbsoluteFloor = 1e-4f)
{
    if (Left == Right) return true;
    if (std::isinf(Left) || std::isinf(Right)) return false;
    if (std::isnan(Left) || std::isnan(Right)) return false;

    const float Magnitude = std::max(std::fabs(Left), std::fabs(Right));
    return std::fabs(Left - Right) <= std::max(AbsoluteFloor, RelativeTolerance * Magnitude);
}

// Judge one scene's worth of traced rays against the oracle. Every assertion reports at most a few examples, then a tally — a broken traversal
// fails thousands of rays and a full dump buries the one line that matters.
void JudgeTrace(const char*                       Label,
                const SyntheticScene&             Scene,
                const std::vector<DeviceRay>&     Rays,
                const std::vector<DeviceResult>&  Results,
                bool                              Verbose)
{
    ++CaseTally;

    uint32_t LocalFailures = 0;
    const auto Fail = [&](const char* Message, long long Value)
    {
        if (LocalFailures < 6u)
            std::printf("  [FAIL] %-28s %s %lld\n", Label, Message, Value);
        ++LocalFailures;
    };

    const uint32_t RayCount = (uint32_t)Rays.size();

    // ─── recompute the oracle for every ray ─────────────────────────────────────────────────────────────────────────────────────────────────
    std::vector<TraceAnswer> Oracle(RayCount);
    for (uint32_t Index = 0; Index < RayCount; ++Index)
    {
        const DeviceRay& Ray = Rays[Index];
        Oracle[Index] = OracleTrace(Scene,
                                    Vector3{ Ray.OriginX, Ray.OriginY, Ray.OriginZ },
                                    Vector3{ Ray.DirectionX, Ray.DirectionY, Ray.DirectionZ },
                                    Ray.NearLimit, Ray.MaximumDistance);
    }

    // ─── 1 : hit parity ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    // 🔴 REPORTED AS TWO SEPARATE TALLIES, NOT ONE. A false MISS (the GPU missed what the oracle hit) is a traversal that dropped a branch; a false
    //    HIT is a traversal that read a box or a triangle it should never have reached. They come from opposite mistakes and a combined count would
    //    let a fix for one hide a regression in the other.
    {
        uint32_t FalseMiss = 0, FalseHit = 0;
        for (uint32_t Index = 0; Index < RayCount; ++Index)
        {
            const bool GpuHit    = Results[Index].HitFlag != 0u;
            const bool OracleHit = Oracle[Index].HitCondition;
            if (OracleHit && !GpuHit)
            {
                if (FalseMiss < 4u && Verbose)
                    std::printf("        MISS ray %u o=(%.3f %.3f %.3f) d=(%.3f %.3f %.3f) oracle inst=%u prim=%u t=%.6f\n",
                                Index, Rays[Index].OriginX, Rays[Index].OriginY, Rays[Index].OriginZ,
                                Rays[Index].DirectionX, Rays[Index].DirectionY, Rays[Index].DirectionZ,
                                Oracle[Index].InstanceIndex, Oracle[Index].PrimitiveIndex, Oracle[Index].Distance);
                ++FalseMiss;
            }
            if (!OracleHit && GpuHit)
            {
                if (FalseHit < 4u && Verbose)
                    std::printf("        FHIT ray %u o=(%.3f %.3f %.3f) d=(%.3f %.3f %.3f) gpu inst=%u prim=%u t=%.6f\n",
                                Index, Rays[Index].OriginX, Rays[Index].OriginY, Rays[Index].OriginZ,
                                Rays[Index].DirectionX, Rays[Index].DirectionY, Rays[Index].DirectionZ,
                                Results[Index].InstanceIndex, Results[Index].PrimitiveIndex, Results[Index].Distance);
                ++FalseHit;
            }
        }
        if (FalseMiss != 0u) Fail("rays the GPU missed but the oracle hit:", FalseMiss);
        if (FalseHit  != 0u) Fail("rays the GPU hit but the oracle missed:", FalseHit);
    }

    // ─── 2 : same triangle ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    // 📝 Only judged where both agree there IS a hit; assertion 1 already owns the disagreements. A ray that grazes an edge shared by two triangles
    //    can legitimately land on either, so a differing primitive is only a failure when the DISTANCES also differ — otherwise the two answers are
    //    the same point on the surface reached through different bookkeeping, which is not a defect.
    {
        uint32_t WrongInstance = 0, WrongPrimitive = 0;
        for (uint32_t Index = 0; Index < RayCount; ++Index)
        {
            if (Results[Index].HitFlag == 0u || !Oracle[Index].HitCondition) continue;

            if (Results[Index].InstanceIndex != Oracle[Index].InstanceIndex &&
                !WithinRoundingTolerance(Results[Index].Distance, Oracle[Index].Distance))
                ++WrongInstance;

            if (Results[Index].PrimitiveIndex != Oracle[Index].PrimitiveIndex &&
                !WithinRoundingTolerance(Results[Index].Distance, Oracle[Index].Distance))
                ++WrongPrimitive;
        }
        if (WrongInstance  != 0u) Fail("hits naming a different instance:", WrongInstance);
        if (WrongPrimitive != 0u) Fail("hits naming a different primitive:", WrongPrimitive);
    }

    // ─── 3 : same distance ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────
    {
        uint32_t WrongDistance = 0;
        for (uint32_t Index = 0; Index < RayCount; ++Index)
        {
            if (Results[Index].HitFlag == 0u || !Oracle[Index].HitCondition) continue;
            if (!WithinRoundingTolerance(Results[Index].Distance, Oracle[Index].Distance))
            {
                if (WrongDistance < 3u && Verbose)
                    std::printf("        ray %u: gpu t=%.6f oracle t=%.6f\n", Index, Results[Index].Distance, Oracle[Index].Distance);
                ++WrongDistance;
            }
        }
        if (WrongDistance != 0u) Fail("hits at a different distance:", WrongDistance);
    }

    // ─── 4 : the barycentrics reconstruct the hit point ─────────────────────────────────────────────────────────────────────────────────────
    // 🔴 THIS IS THE ONLY ASSERTION THAT LOOKS AT THE WEIGHTS AT ALL, AND WITHOUT IT THEY ARE UNCHECKED. A traversal can return the right triangle at
    //    the right distance with u and v swapped, or with the third weight in the first slot; every other assertion here passes. It would then be
    //    the SHADING that comes out wrong, far downstream, long after the traversal was declared correct.
    {
        uint32_t WrongPoint = 0;
        for (uint32_t Index = 0; Index < RayCount; ++Index)
        {
            if (Results[Index].HitFlag == 0u || !Oracle[Index].HitCondition) continue;

            const uint32_t InstanceIndex = Results[Index].InstanceIndex;
            if (InstanceIndex >= (uint32_t)Scene.Instances.size()) continue;

            const SuzanneSceneInstance& Instance = Scene.Instances[InstanceIndex];
            if (Instance.MeshOrdinal >= Scene.SliceCount) continue;

            const MeshGeometry& Mesh     = Scene.Meshes[Instance.MeshOrdinal];
            const uint32_t      Triangle = Results[Index].PrimitiveIndex;
            if (Triangle * 3u + 2u >= (uint32_t)Mesh.Indices.size()) { ++WrongPoint; continue; }

            const uint32_t IndexA = Mesh.Indices[Triangle * 3u + 0u];
            const uint32_t IndexB = Mesh.Indices[Triangle * 3u + 1u];
            const uint32_t IndexC = Mesh.Indices[Triangle * 3u + 2u];

            const Vector3 A{ Mesh.Positions[IndexA * 3u], Mesh.Positions[IndexA * 3u + 1u], Mesh.Positions[IndexA * 3u + 2u] };
            const Vector3 B{ Mesh.Positions[IndexB * 3u], Mesh.Positions[IndexB * 3u + 1u], Mesh.Positions[IndexB * 3u + 2u] };
            const Vector3 C{ Mesh.Positions[IndexC * 3u], Mesh.Positions[IndexC * 3u + 1u], Mesh.Positions[IndexC * 3u + 2u] };

            const float U = Results[Index].BarycentricU;
            const float V = Results[Index].BarycentricV;

            // The barycentric point, in the mesh's LOCAL space.
            const Vector3 FromWeights{ A.X + U * (B.X - A.X) + V * (C.X - A.X),
                                       A.Y + U * (B.Y - A.Y) + V * (C.Y - A.Y),
                                       A.Z + U * (B.Z - A.Z) + V * (C.Z - A.Z) };

            // The same point reached along the ray, transformed into that same local space.
            Matrix4 Inverse;
            std::memcpy(Inverse.M, Instance.InverseModel, sizeof(Inverse.M));

            const DeviceRay& Ray = Rays[Index];
            const Vector3 LocalOrigin    = TransformPoint(Inverse, Vector3{ Ray.OriginX, Ray.OriginY, Ray.OriginZ });
            const Vector3 LocalDirection = TransformDirection(Inverse, Vector3{ Ray.DirectionX, Ray.DirectionY, Ray.DirectionZ });
            const float   Distance       = Results[Index].Distance;

            const Vector3 FromRay{ LocalOrigin.X + Distance * LocalDirection.X,
                                   LocalOrigin.Y + Distance * LocalDirection.Y,
                                   LocalOrigin.Z + Distance * LocalDirection.Z };

            if (!WithinRoundingTolerance(FromWeights.X, FromRay.X, 1e-3f, 1e-3f) ||
                !WithinRoundingTolerance(FromWeights.Y, FromRay.Y, 1e-3f, 1e-3f) ||
                !WithinRoundingTolerance(FromWeights.Z, FromRay.Z, 1e-3f, 1e-3f))
            {
                if (WrongPoint < 3u && Verbose)
                    std::printf("        ray %u: weights (%.4f %.4f %.4f) vs ray (%.4f %.4f %.4f)\n", Index,
                                FromWeights.X, FromWeights.Y, FromWeights.Z, FromRay.X, FromRay.Y, FromRay.Z);
                ++WrongPoint;
            }
        }
        if (WrongPoint != 0u) Fail("barycentrics not reconstructing the hit point:", WrongPoint);
    }

    // ─── 7 : no stack overflow ──────────────────────────────────────────────────────────────────────────────────────────────────────────────
    // 📝 Reported as a count rather than left to corrupt memory. An overflow is not automatically a failure of the traversal's logic, but it IS a
    //    failure of this gate's premise: every assertion above assumes the walk was complete.
    {
        uint32_t Overflowed = 0;
        for (uint32_t Index = 0; Index < RayCount; ++Index)
            if (Results[Index].OverflowFlag != 0u) ++Overflowed;
        if (Overflowed != 0u) Fail("rays that overflowed a traversal stack:", Overflowed);
    }

    // ─── 8 : any-hit twin matches closest-hit ───────────────────────────────────────────────────────────────────────────────────────────────────
    // 🔴 THE ONE ASSERTION THAT VALIDATES TraceAnyHitTwoLevel. The claim in TwoLevelTrace.glsl is that the boolean occlusion twin is bit-identical to
    //    TraceTwoLevel(...).HitCondition — the shade + integrate shadow rays now depend on that being true. Per ray the probe wrote closest-hit's
    //    HitFlag AND any-hit's AnyHitFlag (the .w lane), traced from the SAME origin/direction/limits; here they must agree exactly. A disagreement is
    //    not a tolerance issue — both use the same box/triangle tests over the same trees — so any mismatch means an early-out dropped a branch it had
    //    to keep (any-hit missed a real blocker) or accepted one it should not have. Split by direction so a fix for one cannot mask the other.
    {
        uint32_t AnyMissedBlocker = 0, AnyPhantom = 0;
        for (uint32_t Index = 0; Index < RayCount; ++Index)
        {
            const bool ClosestHit = Results[Index].HitFlag  != 0u;
            const bool AnyHit      = Results[Index].AnyHitFlag != 0.0f;
            if (ClosestHit && !AnyHit)   // closest-hit found a blocker the any-hit walk failed to find → too-bright pixel
            {
                if (AnyMissedBlocker < 4u && Verbose)
                    std::printf("        AMISS ray %u o=(%.3f %.3f %.3f) d=(%.3f %.3f %.3f) closest inst=%u prim=%u t=%.6f\n",
                                Index, Rays[Index].OriginX, Rays[Index].OriginY, Rays[Index].OriginZ,
                                Rays[Index].DirectionX, Rays[Index].DirectionY, Rays[Index].DirectionZ,
                                Results[Index].InstanceIndex, Results[Index].PrimitiveIndex, Results[Index].Distance);
                ++AnyMissedBlocker;
            }
            if (!ClosestHit && AnyHit)   // any-hit claims occlusion where the closest-hit walk found nothing → phantom shadow
                ++AnyPhantom;
        }
        if (AnyMissedBlocker != 0u) Fail("rays any-hit missed but closest-hit blocked:", AnyMissedBlocker);
        if (AnyPhantom       != 0u) Fail("rays any-hit blocked but closest-hit cleared:", AnyPhantom);
    }

    if (LocalFailures > 8u)
        std::printf("  [FAIL] %-28s ... and %u more failing assertions\n", Label, LocalFailures - 8u);

    FailureTally += LocalFailures;

    if (LocalFailures == 0u)
    {
        uint32_t HitCount = 0;
        for (uint32_t Index = 0; Index < RayCount; ++Index)
            if (Results[Index].HitFlag != 0u) ++HitCount;
        std::printf("  [ ok ] %-28s %u rays, %u hits\n", Label, RayCount, HitCount);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE LIMIT ASSERTION
//------------------------------------------------------------------------------------------------------------------------

// ─── 5 : MaximumDistance is honoured ────────────────────────────────────────────────────────────────────────────────────────────────────────
// 🔴 A SEPARATE PASS BECAUSE IT NEEDS DIFFERENT RAYS, NOT DIFFERENT CHECKING. Each ray that hit is re-fired with its limit set just short of its own
//    hit distance, and must then MISS. Nothing in assertions 1-4 can catch a traversal that ignores the far limit — they all fire unbounded rays, so
//    an ignored limit changes nothing. It is fatal for shadow rays, where the limit is the whole query: "is anything between me and the light" is a
//    question a traversal that ignores the limit answers with "yes, the wall behind the light".
void JudgeDistanceLimit(const char*                      Label,
                        VulkanHost&                      Host,
                        VkCommandPool                    CommandPool,
                        TracePipeline&                   Trace,
                        const SyntheticScene&            Scene,
                        const std::vector<DeviceRay>&    Rays,
                        const std::vector<DeviceResult>& Results)
{
    ++CaseTally;

    std::vector<DeviceRay> Shortened;
    std::vector<uint32_t>  SourceRay;

    for (uint32_t Index = 0; Index < (uint32_t)Rays.size(); ++Index)
    {
        if (Results[Index].HitFlag == 0u) continue;

        const float Distance = Results[Index].Distance;
        if (!(Distance > 0.0f) || std::isinf(Distance)) continue;

        // 📝 A 1% haircut, not an epsilon: the limit must be clear of the hit by more than the tolerance the distance check itself allows, or a
        //    borderline pass would be indistinguishable from an ignored limit.
        DeviceRay Short = Rays[Index];
        Short.MaximumDistance = Distance * 0.99f;
        Shortened.push_back(Short);
        SourceRay.push_back(Index);
    }

    if (Shortened.empty())
    {
        std::printf("  [ ok ] %-28s no hits to shorten\n", Label);
        return;
    }

    std::vector<DeviceResult> ShortResults;
    const char* FailureReason = nullptr;
    if (!ExecuteChain(Host, CommandPool, Trace, Scene, Shortened, ShortResults, FailureReason))
    {
        std::printf("  [FAIL] %-28s %s\n", Label, FailureReason ? FailureReason : "chain failed");
        ++FailureTally;
        return;
    }

    uint32_t StillHit = 0;
    for (uint32_t Index = 0; Index < (uint32_t)ShortResults.size(); ++Index)
    {
        if (ShortResults[Index].HitFlag == 0u) continue;

        // A closer hit than the shortened limit is legitimate — the ray may meet something else on the way. Only a hit at or beyond the limit is a
        // violation.
        if (ShortResults[Index].Distance >= Shortened[Index].MaximumDistance)
            ++StillHit;
    }

    if (StillHit != 0u)
    {
        std::printf("  [FAIL] %-28s rays hitting at or past a shortened limit: %u\n", Label, StillHit);
        FailureTally += StillHit;
    }
    else
    {
        std::printf("  [ ok ] %-28s %u shortened rays all respected the limit\n", Label, (uint32_t)Shortened.size());
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            ONE CASE
//------------------------------------------------------------------------------------------------------------------------

void ExecuteCase(VulkanHost& Host, VkCommandPool CommandPool, TracePipeline& Trace,
                 SceneShape Shape, uint32_t InstanceCount, uint32_t Seed, bool Verbose)
{
    SyntheticScene Scene;
    if (!BuildSyntheticScene(Shape, InstanceCount, Seed, Scene))
    {
        std::printf("  [FAIL] %-28s scene construction failed\n", DescribeShape(Shape));
        ++FailureTally;
        ++CaseTally;
        return;
    }

    std::vector<DeviceRay> Rays;
    BuildRaySet(Scene, Seed, Rays);

    char Label[96];
    std::snprintf(Label, sizeof(Label), "%s/%u", DescribeShape(Shape), (uint32_t)Scene.Instances.size());

    std::vector<DeviceResult> Results;
    const char* FailureReason = nullptr;
    if (!ExecuteChain(Host, CommandPool, Trace, Scene, Rays, Results, FailureReason))
    {
        std::printf("  [FAIL] %-28s %s\n", Label, FailureReason ? FailureReason : "chain failed");
        ++FailureTally;
        ++CaseTally;
        return;
    }

    JudgeTrace(Label, Scene, Rays, Results, Verbose);

    char LimitLabel[96];
    std::snprintf(LimitLabel, sizeof(LimitLabel), "%s/limit", DescribeShape(Shape));
    JudgeDistanceLimit(LimitLabel, Host, CommandPool, Trace, Scene, Rays, Results);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                              MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** ArgumentValues)
{
    bool Verbose = false;
    for (int Index = 1; Index < ArgumentCount; ++Index)
        if (std::strcmp(ArgumentValues[Index], "--verbose") == 0) Verbose = true;

    std::printf("==== TwoLevelTraceValidation ====\n");

    VulkanHost Host;
    if (!InitializeVulkanHost(Host, nullptr, 0))
    {
        std::printf("[FAIL] no Vulkan device\n");
        return 1;
    }

    VkCommandPoolCreateInfo PoolInformation = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    PoolInformation.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    PoolInformation.queueFamilyIndex = Host.GraphicsQueueFamily;
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(Host.Device, &PoolInformation, Host.Allocator, &CommandPool) != VK_SUCCESS)
    {
        std::printf("[FAIL] command pool creation failed\n");
        FinalizeVulkanHost(Host);
        return 1;
    }

    TracePipeline Trace;
    if (!InitializeTracePipeline(Host, Trace))
    {
        std::printf("[FAIL] trace pipeline creation failed (is TwoLevelTraceProbe.comp.spv built?)\n");
        vkDestroyCommandPool(Host.Device, CommandPool, Host.Allocator);
        FinalizeVulkanHost(Host);
        return 1;
    }

    // 📝 Counts chosen to cross the interesting thresholds: 1 (no internal nodes), 2 (the smallest real tree), then sizes that force multi-level
    //    top-level descent. The oracle is O(rays x instances x triangles), so the largest counts stay modest — a brute force over 64 instances of a
    //    384-triangle mesh against ~600 rays is already ~15 million triangle tests per case.
    static const SceneShape Shapes[] =
    {
        SceneShape::EmptyScene,
        SceneShape::SingleInstance,
        SceneShape::SeparatedRow,
        SceneShape::DeepMesh,
        SceneShape::OverlappingStack,
        SceneShape::RotatedInstances,
        SceneShape::NonUniformScale,
        SceneShape::MixedMeshes
    };

    static const uint32_t Counts[] = { 2u, 8u, 33u };

    for (SceneShape Shape : Shapes)
    {
        // The two degenerate shapes fix their own instance count; running them three times would only repeat the same case.
        if (Shape == SceneShape::EmptyScene || Shape == SceneShape::SingleInstance)
        {
            ExecuteCase(Host, CommandPool, Trace, Shape, 1u, 1234u, Verbose);
            continue;
        }

        for (uint32_t Count : Counts)
            ExecuteCase(Host, CommandPool, Trace, Shape, Count, 1234u + Count, Verbose);
    }

    FinalizeTracePipeline(Host, Trace);
    vkDestroyCommandPool(Host.Device, CommandPool, Host.Allocator);
    FinalizeVulkanHost(Host);

    if (FailureTally == 0u)
    {
        std::printf("==== PASS : %u cases, 0 failures ====\n", CaseTally);
        return 0;
    }

    std::printf("==== FAIL : %u cases, %u failures ====\n", CaseTally, FailureTally);
    return 1;
}
