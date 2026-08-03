/*==============================================================================================================================================
                                                            GEOMETRYTREEBUILD.H
==============================================================================================================================================*/
// 🧩 The BOTTOM level of the two-level acceleration structure: a surface-area-heuristic bounding volume tree over ONE mesh's triangles, in that
//    mesh's LOCAL space, built on the CPU once at asset load. The top level (a Morton/Karras tree over placed instances) is built on the GPU every
//    frame and lives in InstanceBoundsSubmission + the radix sort; the two meet in the trace, which walks the instance tree, transforms the ray
//    into local space, and walks the mesh tree found there. (VolumeBoundsSubmission is NOT the top level's bounds pass — it reduces triangle
//    centroids, which is a bottom-level shape; the instance-centroid reduce the TLAS needs is InstanceBoundsSubmission.)
//
//    🔴 THE CPU IS ONLY EVER ALLOWED TO SEE LOCAL-SPACE, PER-MESH GEOMETRY. That restriction is the entire reason a CPU builder is admissible here
//       and it is not a stylistic preference — it is what keeps the cost off the frame. A tree built over this mesh's own triangles is built ONCE
//       and then shared by every instance of that mesh, however many there are and wherever they move, because moving an instance changes its
//       world transform and not one of its local vertex positions. The moment this builder is handed world-space geometry, or re-run because
//       something moved, the cost becomes per-frame and scales with the scene — which is precisely the failure the two-level split exists to avoid.
//       If a caller ever needs a world-space tree, that is the TOP level's job, on the GPU.
//
//    📝 Ported 1:1 from three-mesh-bvh (webgiya's BVH dependency: src/core/build/{buildTree,splitUtils,sortUtils,buildUtils,computeBoundsUtils}.js
//       and src/utils/ArrayBoxUtilities.js). The constants, the bin count, the cost model, the Hoare partition and the packed node layout are all
//       carried across unchanged so the C++ tree and the reference JavaScript tree agree node for node on the same input. Where the reference
//       relies on JavaScript behaviour that C++ does not share, the difference is called out at the point it matters rather than silently patched.
//
//    ⚠️ SKINNED MESHES ARE NOT SUPPORTED YET, BUT THE LAYOUT IS ALREADY SHAPED FOR THEM. Skinning moves vertices and freezes topology, so the tree
//       STRUCTURE built here over a bind pose stays valid for every later pose — only the node bounds go stale. The eventual fix is a refit: one
//       bottom-up leaves-to-root pass on the GPU recomputing each box from its children, with no allocation and no reordering (this is what DXR
//       calls PERFORM_UPDATE). Refit needs to walk child->parent, and the ported node layout deliberately has NO parent pointer, so
//       GeometryTreeParentTable below builds that mapping as a SEPARATE array rather than by widening the node. The refit pass itself is not
//       written: no skinned mesh exists to validate it against yet, and an untested shader is worse than an absent one.

#pragma once
#ifndef FRONTIER_GRAPHICS_ACCELERATION_GEOMETRYTREEBUILD_H
#define FRONTIER_GRAPHICS_ACCELERATION_GEOMETRYTREEBUILD_H

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Split strategies, matching three-mesh-bvh Constants.js exactly (CENTER 0, AVERAGE 1, SAH 2). SAH is the one that earns a CPU build: it costs
//    more to construct and produces a measurably better tree, which is the right trade for something built once and then traversed forever.
enum class GeometryTreeStrategy : uint8_t
{
    Center  = 0,   // Split at the midpoint of the centroid box's longest edge — cheapest, worst quality
    Average = 1,   // Split at the mean centroid along the node box's longest edge
    Sah     = 2    // Split at the binned surface-area-heuristic minimum — the default here
};

// 📝 The SAH cost model, verbatim from Constants.js. The absolute values are meaningless; only their RATIO matters, since it expresses how much
//    more a primitive intersection costs than a box test. Changing one without the other silently retunes every tree this builder produces.
constexpr float GeometryTreePrimitiveIntersectCost = 1.25f;   // [-] - modelled cost of intersecting one primitive
constexpr float GeometryTreeTraversalCost          = 1.0f;    // [-] - modelled cost of descending through one interior node

// 📝 The SAH bin count (splitUtils.js BIN_COUNT). Below BIN_COUNT/4 primitives the reference abandons binning and evaluates every primitive
//    position exactly, because at that size exhaustive is both faster and better; GeometryTreeExactSplitLimit is that threshold.
constexpr uint32_t GeometryTreeBinCount         = 32;
constexpr uint32_t GeometryTreeExactSplitLimit  = GeometryTreeBinCount / 4;

// 📝 Build limits (Constants.js DEFAULT_OPTIONS). MaxLeafSize is the primitive count at or below which a node stops splitting; MaxDepth is a hard
//    stop that turns any node reaching it into a leaf regardless of size.
constexpr uint32_t GeometryTreeMaxLeafSize = 10;
constexpr uint32_t GeometryTreeMaxDepth    = 40;

// 📝 The packed node stride in bytes (Constants.js BYTES_PER_NODE = 6*4 + 4 + 4) and the marker that identifies a leaf.
//
//    🔴 THE LEAF MARKER LIVES IN THE UPPER 16 BITS OF THE FINAL WORD AND THAT IS THE ONLY THING DISTINGUISHING A LEAF FROM AN INTERIOR NODE. An
//       interior node stores its split axis (0, 1 or 2) in that same word; a leaf stores its primitive count in the low half and 0xFFFF in the
//       high half. A traversal that tests the wrong half, or a build that forgets to stamp the marker, produces a tree that reads as structurally
//       valid and silently treats leaves as interior nodes — following a primitive offset as if it were a child pointer.
constexpr uint32_t GeometryTreeBytesPerNode   = 6 * 4 + 4 + 4;
constexpr uint32_t GeometryTreeWordsPerNode   = GeometryTreeBytesPerNode / 4;
constexpr uint32_t GeometryTreeLeafFlag       = 0xFFFF;

// 📝 Sentinel for "this node has no parent" in GeometryTreeParentTable — the root, and only the root.
constexpr uint32_t GeometryTreeNoParent = 0xFFFFFFFFu;


//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Build-time options, mirroring three-mesh-bvh's DEFAULT_OPTIONS for the fields this port supports. The reference's JavaScript-only options
//    (shared array buffers, progress callbacks, worker ranges, indirect mode) are deliberately absent rather than stubbed.
//
//    ⚠️ The reference defaults strategy to CENTER; this port defaults to SAH. That is a considered divergence, not a transcription slip: CENTER is
//       the sensible default for a library that must build fast in a browser on the main thread, and SAH is the sensible default for a builder that
//       runs once at asset load and whose output is traversed for the lifetime of the process.
struct GeometryTreeOptions
{
    GeometryTreeStrategy Strategy     = GeometryTreeStrategy::Sah;   // [-] - split rule; SAH unless a caller has reason otherwise
    uint32_t             MaxLeafSize  = GeometryTreeMaxLeafSize;     // [-] - stop splitting at or below this primitive count
    uint32_t             MaxDepth     = GeometryTreeMaxDepth;        // [-] - hard depth stop; deeper nodes become leaves
    bool                 DynamicCondition = false;                   // [-] - true for a mesh that will be skinned/deformed; see below
};

// 📝 A built tree, ready to upload. The node blob is the packed 32-byte-per-node form the traversal shader reads directly; PrimitiveOrder is the
//    builder's reordering of primitive indices, which leaf offsets index into.
//
//    🔴 PRIMITIVEORDER IS NOT OPTIONAL AND NOT COSMETIC. The build PERMUTES primitives so that each leaf's members are contiguous, and a leaf then
//       stores only (offset, count) into that permutation. Uploading the node blob without this array, or uploading a stale copy of it, gives a
//       tree whose boxes are all correct and whose leaves name entirely the wrong triangles — geometry that intersects nothing, or intersects a
//       neighbour, with no malformed data anywhere to catch.
//
//    📝 ParentTable is populated only when GeometryTreeOptions::DynamicCondition was set. It is the child->parent mapping a future GPU refit needs
//       and that the ported node layout cannot supply, and it is kept OUT of the node blob so the blob stays byte-identical to the reference.
struct GeometryTree
{
    std::vector<uint32_t> NodeWords;        // [-] - packed nodes, GeometryTreeWordsPerNode words each; word 0 of node 0 is the root
    std::vector<uint32_t> PrimitiveOrder;   // [-] - permuted primitive indices; leaf (offset,count) ranges index this
    std::vector<uint32_t> ParentTable;      // [-] - one parent node index per node, or empty when the mesh is static
    uint32_t              NodeCount    = 0; // [-] - nodes written; NodeWords.size() / GeometryTreeWordsPerNode
    uint32_t              LeafCount    = 0; // [-] - leaves written, for reporting and validation
    uint32_t              MaximumDepth = 0; // [-] - deepest level reached; equal to MaxDepth means the limit was hit and quality was clamped
    bool                  ReadyCondition = false;   // [-] - true when the build produced a usable tree
};


//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build a tree over the given indexed triangle mesh, in the mesh's own local space, into Result (cleared first). Positions is a tightly packed
// XYZ float array of PositionCount vertices (3 floats each); Indices is three indices per triangle. Returns false, with Result.ReadyCondition
// false, when the mesh is empty or the inputs disagree about size.
//
// 🔴 POSITIONS MUST BE LOCAL SPACE. See the file header — passing world-space geometry compiles, runs, produces a correct-looking tree, and
//    quietly converts a once-per-asset cost into a per-frame one that scales with the scene.
bool BuildGeometryTree(const float*               Positions,
                       uint32_t                   PositionCount,
                       const uint32_t*            Indices,
                       uint32_t                   IndexCount,
                       const GeometryTreeOptions& Options,
                       GeometryTree&              Result);

// Reset a tree to empty, releasing its storage. Safe on a never-built value.
void ClearGeometryTree(GeometryTree& Tree);

} // namespace Frontier

#endif
