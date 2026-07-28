/*============================================================================================================================================
                                                        WORKSPACEDOCUMENTENCODER.CPP
============================================================================================================================================*/
// 🧩 TOML read / write for the saved-scene document. Encode serializes the shared geometry table (each block's VertexField positions / normals /
//    per-corner UVs + the face streams, all as flat number arrays) and the placed-object rows (geometry reference + placement + tint + enclosure)
//    into one TOML document behind a "FRWSDOC" magic + version. Decode is the mirror: parse the file (toml++ with exceptions OFF — errors arrive in
//    the parse_result, never thrown), validate the magic / version / array self-consistency / geometry references, and rebuild the PolygonClusters.
//    The vendored toml++ header is the only dependency beyond the engine's own PolygonCluster; nothing here touches Vulkan or ImGui.

#include "WorkspaceDocumentEncoder.h"

#include "LinearAlgebra_Float64.h"
#include "VertexField.h"

#include <toml.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr const char* DocumentMagic   = "FRWSDOC";
constexpr int64_t      DocumentVersion = 1;

// Append the three components of every Vector3d in Field into a flat TOML array (x0,y0,z0, x1,y1,z1, ...).
toml::array FlattenVector3d(const std::vector<Vector3d>& Field)
{
    toml::array Out;
    for (const Vector3d& Component : Field)
    {
        Out.push_back(Component.XCoord);
        Out.push_back(Component.YCoord);
        Out.push_back(Component.ZCoord);
    }
    return Out;
}

// Append the two components of every Vector2d in Field into a flat TOML array (u0,v0, u1,v1, ...).
toml::array FlattenVector2d(const std::vector<Vector2d>& Field)
{
    toml::array Out;
    for (const Vector2d& Component : Field)
    {
        Out.push_back(Component.XCoord);
        Out.push_back(Component.YCoord);
    }
    return Out;
}

// Append every uint32 as an int64 (TOML has one integer kind) into a flat array.
toml::array FlattenIndices(const std::vector<uint32_t>& Indices)
{
    toml::array Out;
    for (uint32_t Index : Indices)
        Out.push_back(static_cast<int64_t>(Index));
    return Out;
}

// Read a TOML number array as a flat run of doubles. Returns false when the node is absent or not an array (a missing OPTIONAL array is the
// caller's call — it passes Required=false so absence yields an empty run, not a failure).
bool ScanDoubleRun(const toml::table& Table, const char* Key, bool Required, std::vector<double>& Out)
{
    Out.clear();
    const toml::node* Node = Table.get(Key);
    if (Node == nullptr)
        return !Required;
    const toml::array* Values = Node->as_array();
    if (Values == nullptr)
        return false;
    Out.reserve(Values->size());
    for (const toml::node& Value : *Values)
    {
        if (const auto Number = Value.value<double>())
            Out.push_back(*Number);
        else
            return false;
    }
    return true;
}

// Rebuild a Vector3d run from a flat double run (length must be a multiple of 3).
bool RestoreVector3d(const std::vector<double>& Flat, std::vector<Vector3d>& Out)
{
    if ((Flat.size() % 3) != 0)
        return false;
    Out.resize(Flat.size() / 3);
    for (size_t Component = 0; Component < Out.size(); ++Component)
    {
        Out[Component].XCoord = Flat[Component * 3 + 0];
        Out[Component].YCoord = Flat[Component * 3 + 1];
        Out[Component].ZCoord = Flat[Component * 3 + 2];
    }
    return true;
}

// Rebuild a Vector2d run from a flat double run (length must be a multiple of 2).
bool RestoreVector2d(const std::vector<double>& Flat, std::vector<Vector2d>& Out)
{
    if ((Flat.size() % 2) != 0)
        return false;
    Out.resize(Flat.size() / 2);
    for (size_t Component = 0; Component < Out.size(); ++Component)
    {
        Out[Component].XCoord = Flat[Component * 2 + 0];
        Out[Component].YCoord = Flat[Component * 2 + 1];
    }
    return true;
}

// Read a TOML integer array into a uint32 run.
bool ScanIndexRun(const toml::table& Table, const char* Key, std::vector<uint32_t>& Out)
{
    Out.clear();
    const toml::node* Node = Table.get(Key);
    if (Node == nullptr)
        return true;   // absent index arrays are legal (an empty face-corner-texture, say)
    const toml::array* Values = Node->as_array();
    if (Values == nullptr)
        return false;
    Out.reserve(Values->size());
    for (const toml::node& Value : *Values)
    {
        if (const auto Number = Value.value<int64_t>())
            Out.push_back(static_cast<uint32_t>(*Number));
        else
            return false;
    }
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool EncodeWorkspaceDocument(const char* Path, const WorkspaceDocument& Document)
{
    if (Path == nullptr || Path[0] == '\0')
        return false;

    // Reject a dangling geometry reference before writing anything — a document that would not decode must not be written.
    for (const WorkspaceObject& Object : Document.Objects)
        if (Object.GeometryIndex >= Document.Geometry.size())
            return false;

    toml::table Root;
    Root.insert("magic", DocumentMagic);
    Root.insert("version", DocumentVersion);

    // -- Shared geometry blocks -----------------------------------------------------------------------------------------
    toml::array GeometryBlocks;
    for (const WorkspaceGeometryBlock& Block : Document.Geometry)
    {
        const PolygonCluster& Cluster = Block.Geometry;
        toml::table BlockTable;
        BlockTable.insert("title",      Block.Title);
        BlockTable.insert("positions",  FlattenVector3d(Cluster.Attributes.Position));
        BlockTable.insert("normals",    FlattenVector3d(Cluster.Attributes.Normal));
        BlockTable.insert("faceVerts",  FlattenIndices(Cluster.FaceVertexIndices));
        BlockTable.insert("faceCounts", FlattenIndices(Cluster.FaceVertexCounts));
        BlockTable.insert("cornerUv",   FlattenVector2d(Cluster.FaceCornerTexture));
        GeometryBlocks.push_back(std::move(BlockTable));
    }
    Root.insert("geometry", std::move(GeometryBlocks));

    // -- Placed objects -------------------------------------------------------------------------------------------------
    toml::array ObjectRows;
    for (const WorkspaceObject& Object : Document.Objects)
    {
        toml::table ObjectTable;
        ObjectTable.insert("title",     Object.Title);
        ObjectTable.insert("geometry",  static_cast<int64_t>(Object.GeometryIndex));
        ObjectTable.insert("enclosure", static_cast<int64_t>(Object.EnclosureIndex));
        ObjectTable.insert("location",  toml::array{ Object.Placement.Location[0], Object.Placement.Location[1], Object.Placement.Location[2] });
        ObjectTable.insert("rotation",  toml::array{ Object.Placement.Rotation[0], Object.Placement.Rotation[1], Object.Placement.Rotation[2] });
        ObjectTable.insert("scale",     toml::array{ Object.Placement.Scale[0],    Object.Placement.Scale[1],    Object.Placement.Scale[2] });
        ObjectTable.insert("tint",      toml::array{ Object.Tint[0], Object.Tint[1], Object.Tint[2] });
        ObjectRows.push_back(std::move(ObjectTable));
    }
    Root.insert("object", std::move(ObjectRows));

    std::ofstream Stream(Path, std::ios::binary | std::ios::trunc);
    if (!Stream.is_open())
        return false;
    Stream << Root;
    return Stream.good();
}

bool DecodeWorkspaceDocument(const char* Path, WorkspaceDocument& Result)
{
    Result = WorkspaceDocument{};
    if (Path == nullptr || Path[0] == '\0')
        return false;

    toml::parse_result Parsed = toml::parse_file(Path);
    if (!Parsed)
        return false;
    const toml::table& Root = Parsed.table();

    // -- Magic + version ------------------------------------------------------------------------------------------------
    const auto Magic   = Root["magic"].value<std::string>();
    const auto Version = Root["version"].value<int64_t>();
    if (!Magic || *Magic != DocumentMagic || !Version || *Version != DocumentVersion)
        return false;

    // -- Shared geometry blocks -----------------------------------------------------------------------------------------
    const toml::array* GeometryBlocks = Root["geometry"].as_array();
    if (GeometryBlocks == nullptr)
        return false;
    for (const toml::node& BlockNode : *GeometryBlocks)
    {
        const toml::table* BlockTable = BlockNode.as_table();
        if (BlockTable == nullptr)
            return false;

        WorkspaceGeometryBlock Block;
        Block.Title = BlockTable->get("title") ? BlockTable->get("title")->value_or(std::string{}) : std::string{};

        std::vector<double> Positions;
        std::vector<double> Normals;
        std::vector<double> CornerUv;
        if (!ScanDoubleRun(*BlockTable, "positions", true,  Positions) ||
            !ScanDoubleRun(*BlockTable, "normals",   false, Normals)   ||
            !ScanDoubleRun(*BlockTable, "cornerUv",  false, CornerUv))
            return false;
        if (!ScanIndexRun(*BlockTable, "faceVerts",  Block.Geometry.FaceVertexIndices) ||
            !ScanIndexRun(*BlockTable, "faceCounts", Block.Geometry.FaceVertexCounts))
            return false;
        if (!RestoreVector3d(Positions, Block.Geometry.Attributes.Position) ||
            !RestoreVector3d(Normals,   Block.Geometry.Attributes.Normal)   ||
            !RestoreVector2d(CornerUv,  Block.Geometry.FaceCornerTexture))
            return false;

        Result.Geometry.push_back(std::move(Block));
    }

    // -- Placed objects -------------------------------------------------------------------------------------------------
    const toml::array* ObjectRows = Root["object"].as_array();
    if (ObjectRows == nullptr)
        return false;
    for (const toml::node& ObjectNode : *ObjectRows)
    {
        const toml::table* ObjectTable = ObjectNode.as_table();
        if (ObjectTable == nullptr)
            return false;

        WorkspaceObject Object;
        Object.Title          = ObjectTable->get("title") ? ObjectTable->get("title")->value_or(std::string{}) : std::string{};
        Object.GeometryIndex  = static_cast<uint32_t>((*ObjectTable)["geometry"].value_or<int64_t>(0));
        Object.EnclosureIndex = static_cast<int32_t>((*ObjectTable)["enclosure"].value_or<int64_t>(-1));

        std::vector<double> Location;
        std::vector<double> Rotation;
        std::vector<double> Scale;
        std::vector<double> Tint;
        if (!ScanDoubleRun(*ObjectTable, "location", true, Location) || Location.size() != 3 ||
            !ScanDoubleRun(*ObjectTable, "rotation", true, Rotation) || Rotation.size() != 3 ||
            !ScanDoubleRun(*ObjectTable, "scale",    true, Scale)    || Scale.size()    != 3 ||
            !ScanDoubleRun(*ObjectTable, "tint",     true, Tint)     || Tint.size()     != 3)
            return false;
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            Object.Placement.Location[Axis] = static_cast<float>(Location[Axis]);
            Object.Placement.Rotation[Axis] = static_cast<float>(Rotation[Axis]);
            Object.Placement.Scale[Axis]    = static_cast<float>(Scale[Axis]);
            Object.Tint[Axis]               = static_cast<float>(Tint[Axis]);
        }

        if (Object.GeometryIndex >= Result.Geometry.size())   // dangling reference — reject the whole document
        {
            Result = WorkspaceDocument{};
            return false;
        }
        Result.Objects.push_back(std::move(Object));
    }

    return true;
}

} // namespace Frontier
