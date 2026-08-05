/*============================================================================================================================================
                                                           RIGIDBODYSCENEAUTHOR.CPP
============================================================================================================================================*/
// 🧩 Builds the two drop-scene documents. The crate document holds ONE unit-cube geometry block; every tower crate and the wrecker are placed
//    objects that instance it at a different translation and scale, so the whole dynamic population costs one geometry block on disk and one
//    instanced draw on the GPU. The ground document holds one slab block and one static object at identity.
//
//    Winding matches the raster's front-face convention (counter-clockwise seen from OUTSIDE), copied from the floor slab the visibility scenes
//    already ship — a reversed quad would be back-face culled and the crate would render hollow.

#include "RigidBodySceneAuthor.h"

#include "Authoring/Geometry/Interchange/WorkspaceDocumentEncoder.h"
#include "Graphics/Scene/SurfacePresetTable.h"

#include <string>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        GEOMETRY AUTHORING
//------------------------------------------------------------------------------------------------------------------------

// The six quad faces of an axis-aligned box over corners 0-3 (low Z ring) and 4-7 (high Z ring), each wound counter-clockwise when viewed from
// outside the box. Shared by the crate cube and the ground slab: only the corner coordinates differ.
static const uint32_t BoxFaceCorners[6][4] =
{
    { 4, 5, 6, 7 },   // +Z top
    { 3, 2, 1, 0 },   // -Z bottom
    { 0, 1, 5, 4 },   // -Y front
    { 2, 3, 7, 6 },   // +Y back
    { 1, 2, 6, 5 },   // +X right
    { 3, 0, 4, 7 },   // -X left
};

// Populate Cluster with an axis-aligned box spanning [MinX,MaxX] x [MinY,MaxY] x [MinZ,MaxZ] in its own local frame: eight corners, six quads.
static void ComposeBoxCluster(double MinX, double MaxX, double MinY, double MaxY, double MinZ, double MaxZ, PolygonCluster& Cluster)
{
    PolygonCluster Built;

    Built.Attributes.Position =
    {
        Vector3d{ MinX, MinY, MinZ },   // 0
        Vector3d{ MaxX, MinY, MinZ },   // 1
        Vector3d{ MaxX, MaxY, MinZ },   // 2
        Vector3d{ MinX, MaxY, MinZ },   // 3
        Vector3d{ MinX, MinY, MaxZ },   // 4
        Vector3d{ MaxX, MinY, MaxZ },   // 5
        Vector3d{ MaxX, MaxY, MaxZ },   // 6
        Vector3d{ MinX, MaxY, MaxZ },   // 7
    };

    Built.FaceVertexIndices.reserve(24);
    Built.FaceVertexCounts.reserve(6);
    for (uint32_t FaceIterator = 0; FaceIterator < 6u; ++FaceIterator)
    {
        for (uint32_t CornerIterator = 0; CornerIterator < 4u; ++CornerIterator)
            Built.FaceVertexIndices.push_back(BoxFaceCorners[FaceIterator][CornerIterator]);
        Built.FaceVertexCounts.push_back(4u);
    }

    Cluster = std::move(Built);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         DOCUMENT AUTHORING
//------------------------------------------------------------------------------------------------------------------------

// Deterministic pseudo-random in [-1,1] from an integer seed. Used only to give each tower crate a small planar offset and yaw so the stack is
// imperfect and the collapse is asymmetric — a perfectly aligned tower topples straight down its own axis and reads as a lift, not a collapse.
// Deterministic by construction so the authored document (and therefore every run) is reproducible.
static float ScatteredFraction(uint32_t Seed)
{
    uint32_t Mixed = Seed * 747796405u + 2891336453u;
    Mixed        ^= Mixed >> 15;
    Mixed        *= 2246822519u;
    Mixed        ^= Mixed >> 13;
    return (float)(Mixed & 0xFFFFu) / 32767.5f - 1.0f;
}

// Bake the crate document: one unit-cube geometry block centred on its own origin, then the tower's crates followed by the wrecker. Every object
// carries its world placement in the document — the host reads these back to construct the matching rigid bodies, so the file is the initial
// condition and nothing about the layout is duplicated in code.
static void ComposeCrateDocument(const RigidBodySceneProportions& Proportions, WorkspaceDocument& Document)
{
    // 🔴 The cube is authored centred on its local origin, NOT resting on it. Jolt's BoxShape is centre-origin too, so a centred authoring frame
    //    lets the host hand the solver the same translation the document carries with no half-height correction anywhere.
    const double HalfEdge = (double)Proportions.CrateEdge * 0.5;

    WorkspaceGeometryBlock Block;
    Block.Title = "Crate";
    ComposeBoxCluster(-HalfEdge, HalfEdge, -HalfEdge, HalfEdge, -HalfEdge, HalfEdge, Block.Geometry);
    Document.Geometry.push_back(std::move(Block));

    const float Edge        = Proportions.CrateEdge;
    const float FootprintHalf = 0.5f * Edge * (float)(Proportions.TowerColumns - 1u);

    for (uint32_t LevelIterator = 0; LevelIterator < Proportions.TowerLevels; ++LevelIterator)
    {
        for (uint32_t RowIterator = 0; RowIterator < Proportions.TowerColumns; ++RowIterator)
        {
            for (uint32_t ColumnIterator = 0; ColumnIterator < Proportions.TowerColumns; ++ColumnIterator)
            {
                const uint32_t Ordinal = (LevelIterator * Proportions.TowerColumns + RowIterator) * Proportions.TowerColumns + ColumnIterator;

                // Planar jitter is a small fraction of the edge (±4 %) plus a few degrees of yaw: enough that the contact manifolds are uneven and
                // the tower shears sideways as it falls, small enough that the initial stack is stable if the wrecker never lands.
                const float SlideX = ScatteredFraction(Ordinal * 3u + 0u) * Edge * 0.04f;
                const float SlideY = ScatteredFraction(Ordinal * 3u + 1u) * Edge * 0.04f;
                const float Yaw    = ScatteredFraction(Ordinal * 3u + 2u) * 4.0f;   // [°]

                WorkspaceObject Object;
                Object.GeometryIndex   = 0u;
                Object.Title           = "Crate " + std::to_string(Ordinal);
                Object.Placement.Location[0] = (float)ColumnIterator * Edge - FootprintHalf + SlideX;
                Object.Placement.Location[1] = (float)RowIterator    * Edge - FootprintHalf + SlideY;
                Object.Placement.Location[2] = ((float)LevelIterator + 0.5f) * Edge;   // level 0 rests ON the ground plane
                Object.Placement.Rotation[2] = Yaw;
                Object.Placement.Scale[0]    = 1.0f;
                Object.Placement.Scale[1]    = 1.0f;
                Object.Placement.Scale[2]    = 1.0f;

                // Tint ramps with height so the collapse is legible: warm at the base, cool at the top.
                const float Rise = Proportions.TowerLevels > 1u ? (float)LevelIterator / (float)(Proportions.TowerLevels - 1u) : 0.0f;
                Object.Tint[0]   = 0.62f - 0.34f * Rise;
                Object.Tint[1]   = 0.36f + 0.10f * Rise;
                Object.Tint[2]   = 0.20f + 0.52f * Rise;
                Object.MaterialId     = SurfacePresetPlastic;
                Object.EnclosureIndex = -1;
                Document.Objects.push_back(std::move(Object));
            }
        }
    }

    // The wrecker: the same unit cube scaled up, parked above the tower's centre. It is the LAST object in the document, which is the contract the
    // host relies on to tell it apart from the crates without a name comparison.
    WorkspaceObject Wrecker;
    Wrecker.GeometryIndex   = 0u;
    Wrecker.Title           = "Wrecker";
    Wrecker.Placement.Location[0] = 0.0f;
    Wrecker.Placement.Location[1] = 0.0f;
    Wrecker.Placement.Location[2] = Proportions.WreckerHeight;
    Wrecker.Placement.Scale[0]    = Proportions.WreckerEdge / Proportions.CrateEdge;
    Wrecker.Placement.Scale[1]    = Proportions.WreckerEdge / Proportions.CrateEdge;
    Wrecker.Placement.Scale[2]    = Proportions.WreckerEdge / Proportions.CrateEdge;
    Wrecker.Tint[0]         = 0.86f;
    Wrecker.Tint[1]         = 0.84f;
    Wrecker.Tint[2]         = 0.80f;
    Wrecker.MaterialId      = SurfacePresetMetal;
    Wrecker.EnclosureIndex  = -1;
    Document.Objects.push_back(std::move(Wrecker));
}

// Bake the ground document: one slab block whose TOP face lies on z = 0, instanced by a single static object at identity.
static void ComposeGroundDocument(const RigidBodySceneProportions& Proportions, WorkspaceDocument& Document)
{
    const double HalfSpan = (double)Proportions.GroundSpan * 0.5;

    WorkspaceGeometryBlock Block;
    Block.Title = "Ground";
    ComposeBoxCluster(-HalfSpan, HalfSpan, -HalfSpan, HalfSpan, -(double)Proportions.GroundDepth, 0.0, Block.Geometry);
    Document.Geometry.push_back(std::move(Block));

    WorkspaceObject Object;
    Object.GeometryIndex   = 0u;
    Object.Title           = "Ground";
    Object.Placement       = LocalPlacement{};   // identity: the slab is authored in world space
    Object.Tint[0]         = 0.30f;
    Object.Tint[1]         = 0.31f;
    Object.Tint[2]         = 0.33f;
    Object.MaterialId      = SurfacePresetFloor;
    Object.EnclosureIndex  = -1;
    Document.Objects.push_back(std::move(Object));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool AuthorRigidBodyDropDocuments(const RigidBodySceneProportions& Proportions, const std::string& CratePath, const std::string& GroundPath)
{
    WorkspaceDocument Crates;
    ComposeCrateDocument(Proportions, Crates);
    if (!EncodeWorkspaceDocument(CratePath.c_str(), Crates))
        return false;

    WorkspaceDocument Ground;
    ComposeGroundDocument(Proportions, Ground);
    return EncodeWorkspaceDocument(GroundPath.c_str(), Ground);
}

} // namespace Frontier
