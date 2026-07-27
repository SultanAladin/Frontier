/*============================================================================================================================================
                                                        INTERCHANGEFILMBOXDECODER.CPP
============================================================================================================================================*/
// 🧩 The Autodesk FBX decoder (ufbx). Unlike cgltf / fast_obj, ufbx ships a separate implementation source (ufbx.c, listed on
//    APP_SRCS5), so this TU includes only the header — no implementation macro. Translates ufbx's node / mesh graph into
//    engine-native PolygonClusters, letting ufbx auto-convert the file's axis + unit metadata to the engine convention
//    (Z-up right-handed, cm) through its load options (target_axes = right-handed-Z-up, target_unit_meters = 0.01, geometry-
//    modifying space conversion). Because ufbx bakes the conversion into the geometry, this decoder applies NO further rotation
//    or scale — positions arrive already Z-up in cm. The FBX enclosure nesting carries through each ImportedModel's SourceIndex /
//    EnclosureIndex. The ufbx_* vocabulary never leaves this file — every object crosses the boundary as an ImportedModel.

#include "ufbx.h"

#include "InterchangeDecoders.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    Vector3d NormalizeOrUp(double SourceX, double SourceY, double SourceZ)
    {
        const double Length = std::sqrt(SourceX * SourceX + SourceY * SourceY + SourceZ * SourceZ);
        if (Length < 1.0e-12) return Vector3d{ 0.0, 0.0, 1.0 };
        return Vector3d{ SourceX / Length, SourceY / Length, SourceZ / Length };
    }

    // Translate ONE ufbx mesh into a PolygonCluster. ufbx has already converted axis + unit (target_axes / target_unit_meters), so the
    //    values arrive Z-up in cm — but vertex_position.values are still MESH-LOCAL, so the node's geometry_to_world (its own TRS folded
    //    up its enclosure chain, plus any FBX geometric offset) is baked into every position here; without it a mesh authored away from the
    //    origin lands at the local origin (invisible / stacked on Suzanne). Normals transform through the inverse-transpose so a non-
    //    uniform node scale never skews them. Faces preserve tris / quads / N-gons via ufbx_face's index range. The mesh's logical vertex
    //    slots (vertex_position.indices) become the cluster's compact vertex indices — a UV/normal seam splits at the corner, not the
    //    position, so per-corner UVs carry the seam truth. Returns false when the mesh has no non-degenerate faces.
    bool TranslateMesh(const ufbx_node* Node, const ufbx_mesh* Source, PolygonCluster& Cluster, bool& TextureCoordinatesPresent)
    {
        if (Source == nullptr || Source->num_faces == 0 || !Source->vertex_position.exists) return false;

        ResetPolygonCluster(Cluster);
        VertexField& Attributes = Cluster.Attributes;

        // The node -> world transform (identity when the node is missing) + its normal matrix, applied to every vertex below.
        const ufbx_matrix GeometryToWorld = (Node != nullptr) ? Node->geometry_to_world : ufbx_identity_matrix;
        const ufbx_matrix NormalMatrix    = ufbx_matrix_for_normals(&GeometryToWorld);

        const bool HasNormal  = Source->vertex_normal.exists;
        const bool HasTexture = Source->vertex_uv.exists;
        const bool MultiMaterial = Source->materials.count > 1 && Source->face_material.count == Source->num_faces;

        // Compact the mesh's logical vertices (num_vertices) into the cluster: one position per logical vertex, remapped densely so
        //    the cluster only carries the vertices its faces touch. vertex_first_index locates a representative corner per vertex for
        //    the (fallback) per-vertex normal seed; per-corner normals/UVs are read at the corner below.
        std::unordered_map<uint32_t, uint32_t> VertexRemap;

        for (size_t Face = 0; Face < Source->num_faces; ++Face)
        {
            const ufbx_face FaceRange = Source->faces.data[Face];
            if (FaceRange.num_indices < 3) continue;   // skip point / line / empty faces

            for (uint32_t Slot = 0; Slot < FaceRange.num_indices; ++Slot)
            {
                const uint32_t CornerIndex   = FaceRange.index_begin + Slot;
                const uint32_t LogicalVertex = Source->vertex_position.indices.data[CornerIndex];

                uint32_t LocalVertex;
                auto Existing = VertexRemap.find(LogicalVertex);
                if (Existing != VertexRemap.end())
                {
                    LocalVertex = Existing->second;
                }
                else
                {
                    LocalVertex = static_cast<uint32_t>(Attributes.Position.size());
                    const ufbx_vec3 LocalPosition = Source->vertex_position.values.data[LogicalVertex];
                    const ufbx_vec3 Position      = ufbx_transform_position(&GeometryToWorld, LocalPosition);
                    Attributes.Position.push_back(Vector3d{ Position.x, Position.y, Position.z });
                    Attributes.Normal.push_back(Vector3d{ 0.0, 0.0, 0.0 });   // seeded from the corner normal below, else synthesized
                    VertexRemap.emplace(LogicalVertex, LocalVertex);
                }

                if (HasNormal)
                {
                    Vector3d& Target = Attributes.Normal[LocalVertex];
                    if (Target.XCoord == 0.0 && Target.YCoord == 0.0 && Target.ZCoord == 0.0)
                    {
                        const uint32_t NormalIndex = Source->vertex_normal.indices.data[CornerIndex];
                        const ufbx_vec3 LocalNormal = Source->vertex_normal.values.data[NormalIndex];
                        const ufbx_vec3 Normal      = ufbx_transform_direction(&NormalMatrix, LocalNormal);
                        Target = NormalizeOrUp(Normal.x, Normal.y, Normal.z);
                    }
                }

                Cluster.FaceVertexIndices.push_back(LocalVertex);

                Vector2d CornerTexture{ 0.0, 0.0 };
                if (HasTexture)
                {
                    const uint32_t TextureIndex = Source->vertex_uv.indices.data[CornerIndex];
                    const ufbx_vec2 UV          = Source->vertex_uv.values.data[TextureIndex];
                    CornerTexture = Vector2d{ UV.x, UV.y };
                }
                Cluster.FaceCornerTexture.push_back(CornerTexture);
            }

            Cluster.FaceVertexCounts.push_back(FaceRange.num_indices);
            if (MultiMaterial) Cluster.FaceMaterialIndex.push_back(Source->face_material.data[Face]);
        }

        if (Cluster.FaceVertexCounts.empty()) return false;

        TextureCoordinatesPresent = HasTexture;
        if (!HasTexture) Cluster.FaceCornerTexture.clear();

        // Synthesize a flat face normal for any vertex still without one (source omitted normals AND generate_missing_normals off).
        bool NormalMissing = false;
        for (const Vector3d& Normal : Attributes.Normal)
        {
            if (Normal.XCoord == 0.0 && Normal.YCoord == 0.0 && Normal.ZCoord == 0.0) { NormalMissing = true; break; }
        }
        if (NormalMissing)
        {
            size_t Cursor = 0;
            for (uint32_t Face = 0; Face < Cluster.FaceVertexCounts.size(); ++Face)
            {
                const uint32_t CornerCount = Cluster.FaceVertexCounts[Face];
                const uint32_t IndexA = Cluster.FaceVertexIndices[Cursor];
                const uint32_t IndexB = Cluster.FaceVertexIndices[Cursor + 1];
                const uint32_t IndexC = Cluster.FaceVertexIndices[Cursor + 2];
                const Vector3d& PointA = Attributes.Position[IndexA];
                const Vector3d& PointB = Attributes.Position[IndexB];
                const Vector3d& PointC = Attributes.Position[IndexC];
                const double EdgeOneX = PointB.XCoord - PointA.XCoord;
                const double EdgeOneY = PointB.YCoord - PointA.YCoord;
                const double EdgeOneZ = PointB.ZCoord - PointA.ZCoord;
                const double EdgeTwoX = PointC.XCoord - PointA.XCoord;
                const double EdgeTwoY = PointC.YCoord - PointA.YCoord;
                const double EdgeTwoZ = PointC.ZCoord - PointA.ZCoord;
                Vector3d FaceNormal{ EdgeOneY * EdgeTwoZ - EdgeOneZ * EdgeTwoY,
                                    EdgeOneZ * EdgeTwoX - EdgeOneX * EdgeTwoZ,
                                    EdgeOneX * EdgeTwoY - EdgeOneY * EdgeTwoX };
                const double Length = std::sqrt(FaceNormal.XCoord * FaceNormal.XCoord +
                                                FaceNormal.YCoord * FaceNormal.YCoord +
                                                FaceNormal.ZCoord * FaceNormal.ZCoord);
                if (Length > 1.0e-12)
                {
                    FaceNormal = Vector3d{ FaceNormal.XCoord / Length, FaceNormal.YCoord / Length, FaceNormal.ZCoord / Length };
                    for (uint32_t Slot = 0; Slot < CornerCount; ++Slot)
                    {
                        Vector3d& Target = Attributes.Normal[Cluster.FaceVertexIndices[Cursor + Slot]];
                        if (Target.XCoord == 0.0 && Target.YCoord == 0.0 && Target.ZCoord == 0.0) Target = FaceNormal;
                    }
                }
                Cursor += CornerCount;
            }
        }

        EvaluatePolygonDescriptor(Cluster);
        return true;
    }

    void ResolveTitle(const char* SourceName, int32_t Ordinal, char* Title, size_t Capacity)
    {
        if (SourceName != nullptr && SourceName[0] != '\0')
        {
            std::strncpy(Title, SourceName, Capacity - 1);
            Title[Capacity - 1] = '\0';
            return;
        }
        std::snprintf(Title, Capacity, "Imported %d", Ordinal);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool DecodeFilmbox(const char* Path, const InterchangeOptions& Options, std::vector<ImportedModel>& Result)
{
    (void)Options;   // FBX carries authoritative axis + unit metadata; ufbx applies the engine target below, not the fallback opts.

    ufbx_load_opts LoadOptions = {};
    LoadOptions.target_axes              = ufbx_axes_right_handed_z_up;   // convert whatever the file authored onto engine Z-up RH
    LoadOptions.target_unit_meters       = 0.01;                         // one world unit = 1 cm (engine authoring unit)
    LoadOptions.space_conversion         = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;   // bake the conversion into positions, not a root xform
    LoadOptions.generate_missing_normals = true;                         // let ufbx fill normals; the decoder still synthesizes as a guard

    ufbx_error LoadError = {};
    ufbx_scene* Scene = ufbx_load_file(Path, &LoadOptions, &LoadError);
    if (Scene == nullptr) return false;

    // Two-pass so enclosing-node slots always resolve: record which nodes emit a model (mesh-bearing), then translate + resolve each
    //    EnclosureIndex to the nearest mesh-bearing enclosing node's slot. ufbx_scene.nodes is a flat list including the root node.
    std::unordered_map<const ufbx_node*, int32_t> NodeSlot;
    NodeSlot.reserve(Scene->nodes.count);

    const int32_t AppendBase = static_cast<int32_t>(Result.size());
    int32_t NextSlot = AppendBase;
    for (size_t NodeIndex = 0; NodeIndex < Scene->nodes.count; ++NodeIndex)
    {
        const ufbx_node* Node = Scene->nodes.data[NodeIndex];
        if (Node->mesh != nullptr && Node->mesh->num_faces > 0) NodeSlot.emplace(Node, NextSlot++);
    }

    for (size_t NodeIndex = 0; NodeIndex < Scene->nodes.count; ++NodeIndex)
    {
        const ufbx_node* Node = Scene->nodes.data[NodeIndex];
        auto SlotEntry = NodeSlot.find(Node);
        if (SlotEntry == NodeSlot.end()) continue;

        ImportedModel Model;
        bool TextureCoordinatesPresent = false;
        if (!TranslateMesh(Node, Node->mesh, Model.Geometry, TextureCoordinatesPresent)) continue;

        Model.TextureCoordinatesPresent = TextureCoordinatesPresent;
        Model.SourceIndex = SlotEntry->second;

        Model.EnclosureIndex = -1;
        for (const ufbx_node* EnclosingNode = Node->parent; EnclosingNode != nullptr; EnclosingNode = EnclosingNode->parent)
        {
            auto EnclosingNodeSlot = NodeSlot.find(EnclosingNode);
            if (EnclosingNodeSlot != NodeSlot.end()) { Model.EnclosureIndex = EnclosingNodeSlot->second; break; }
        }

        // Base colour: the PBR base-colour factor of the mesh's first material (linear RGB), else white.
        if (Node->mesh->materials.count > 0)
        {
            const ufbx_material* Material = Node->mesh->materials.data[0];
            if (Material != nullptr && Material->pbr.base_color.has_value)
            {
                Model.BaseColourHint[0] = static_cast<float>(Material->pbr.base_color.value_vec3.x);
                Model.BaseColourHint[1] = static_cast<float>(Material->pbr.base_color.value_vec3.y);
                Model.BaseColourHint[2] = static_cast<float>(Material->pbr.base_color.value_vec3.z);
            }
        }

        const char* SourceName = (Node->name.length > 0) ? Node->name.data
                                                          : (Node->mesh->name.length > 0 ? Node->mesh->name.data : nullptr);
        ResolveTitle(SourceName, Model.SourceIndex, Model.Title, sizeof(Model.Title));

        Result.push_back(std::move(Model));
    }

    ufbx_free_scene(Scene);
    return true;
}

} // namespace Frontier
