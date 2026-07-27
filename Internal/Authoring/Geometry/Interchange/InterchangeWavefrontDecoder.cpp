/*============================================================================================================================================
                                                       INTERCHANGEWAVEFRONTDECODER.CPP
============================================================================================================================================*/
// 🧩 The Wavefront OBJ decoder (fast_obj). This is the ONE translation unit that defines FAST_OBJ_IMPLEMENTATION, so the parser's
//    function bodies live here alone. Translates fast_obj's object / face arrays into engine-native PolygonClusters (native unit
//    x1, per-corner UVs when present, faces preserving tris / quads / N-gons), synthesizing flat face normals when the source omits
//    them. OBJ carries no axis metadata; the common exporter convention is Y-up, so positions carry the same Y-up -> Z-up rotation
//    the glTF + Suzanne paths use so an import lands upright beside them. The fastObj* vocabulary never leaves this file — every
//    object crosses the boundary as an ImportedModel.

#define FAST_OBJ_IMPLEMENTATION
#include "fast_obj.h"

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
    // OBJ authors an author-chosen unit (x1, no metadata) and the common Y-up convention; the engine is Z-up right-handed. The
    //    axis carry is the same proper +90° rotation about world X — (x, y, z) -> (x, -z, y) — the glTF + Suzanne paths use, so
    //    winding / handedness survive and an OBJ import lands upright beside the others. The x1 unit rides UnitScaleToCentimetres.
    Vector3d ConvertPosition(float SourceX, float SourceY, float SourceZ, double UnitScale)
    {
        return Vector3d{ SourceX * UnitScale, -SourceZ * UnitScale, SourceY * UnitScale };
    }

    Vector3d ConvertNormal(float SourceX, float SourceY, float SourceZ)
    {
        Vector3d Rotated{ SourceX, -SourceZ, SourceY };
        const double Length = std::sqrt(Rotated.XCoord * Rotated.XCoord +
                                        Rotated.YCoord * Rotated.YCoord +
                                        Rotated.ZCoord * Rotated.ZCoord);
        if (Length < 1.0e-12) return Vector3d{ 0.0, 0.0, 1.0 };
        return Vector3d{ Rotated.XCoord / Length, Rotated.YCoord / Length, Rotated.ZCoord / Length };
    }

    // Translate the faces of ONE OBJ object (the range face_offset .. face_offset + face_count) into a PolygonCluster. fast_obj
    //    stores per-vertex attributes 1-indexed in shared arrays (index 0 = absent); this remaps the referenced positions into a
    //    compact per-object VertexField so the cluster owns only its own vertices. Returns false when the object holds no faces.
    bool TranslateObject(const fastObjMesh* Source,
                         const fastObjGroup& Object,
                         double              UnitScale,
                         PolygonCluster&     Cluster,
                         bool&               TextureCoordinatesPresent)
    {
        if (Object.face_count == 0) return false;

        ResetPolygonCluster(Cluster);
        VertexField& Attributes = Cluster.Attributes;

        // Remap fast_obj's global 1-indexed position slots to a compact local vertex range, so a per-object cluster references
        //    only the positions its own faces touch. The map keys on the source position index; the normal / UV for a corner ride
        //    the corner (not the vertex), so a vertex reused with different UVs across faces still stores one position + per-corner UV.
        std::unordered_map<unsigned int, uint32_t> PositionRemap;
        bool AnyTexture = false;

        // Walk the object's contiguous index range once the face-corner cursor is located at its index_offset.
        unsigned int IndexCursor = Object.index_offset;
        for (unsigned int Face = 0; Face < Object.face_count; ++Face)
        {
            const unsigned int CornerCount = Source->face_vertices[Object.face_offset + Face];
            if (CornerCount < 3)
            {
                IndexCursor += CornerCount;   // skip a degenerate face (a point / line) but keep the cursor aligned
                continue;
            }

            for (unsigned int Slot = 0; Slot < CornerCount; ++Slot)
            {
                const fastObjIndex Reference = Source->indices[IndexCursor + Slot];

                uint32_t LocalVertex;
                auto Existing = PositionRemap.find(Reference.p);
                if (Existing != PositionRemap.end())
                {
                    LocalVertex = Existing->second;
                }
                else
                {
                    LocalVertex = static_cast<uint32_t>(Attributes.Position.size());
                    // 📝 Guard the vendor index: a malformed OBJ can reference a position slot past position_count; reading past the
                    //    array would be undefined behaviour, so an out-of-range reference decodes to the origin rather than reading OOB.
                    if (Reference.p < Source->position_count)
                    {
                        const float* Position = Source->positions + static_cast<size_t>(Reference.p) * 3;
                        Attributes.Position.push_back(ConvertPosition(Position[0], Position[1], Position[2], UnitScale));
                    }
                    else
                    {
                        Attributes.Position.push_back(Vector3d{ 0.0, 0.0, 0.0 });
                    }
                    Attributes.Normal.push_back(Vector3d{ 0.0, 0.0, 0.0 });   // filled from the corner normal below, else synthesized
                    PositionRemap.emplace(Reference.p, LocalVertex);
                }

                // A present normal (index != 0) seeds the vertex normal; a vertex shared across faces keeps the first non-zero.
                if (Reference.n != 0 && Reference.n < Source->normal_count)
                {
                    Vector3d& Target = Attributes.Normal[LocalVertex];
                    if (Target.XCoord == 0.0 && Target.YCoord == 0.0 && Target.ZCoord == 0.0)
                    {
                        const float* Normal = Source->normals + static_cast<size_t>(Reference.n) * 3;
                        Target = ConvertNormal(Normal[0], Normal[1], Normal[2]);
                    }
                }

                Cluster.FaceVertexIndices.push_back(LocalVertex);

                Vector2d CornerTexture{ 0.0, 0.0 };
                if (Reference.t != 0 && Reference.t < Source->texcoord_count)
                {
                    const float* Texture = Source->texcoords + static_cast<size_t>(Reference.t) * 2;
                    CornerTexture = Vector2d{ Texture[0], Texture[1] };
                    AnyTexture = true;
                }
                Cluster.FaceCornerTexture.push_back(CornerTexture);
            }

            Cluster.FaceVertexCounts.push_back(CornerCount);
            IndexCursor += CornerCount;
        }

        if (Cluster.FaceVertexCounts.empty()) return false;

        TextureCoordinatesPresent = AnyTexture;
        if (!AnyTexture) Cluster.FaceCornerTexture.clear();

        // Synthesize a flat face normal for any vertex the source left without one (OBJ frequently omits normals entirely).
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

bool DecodeWavefront(const char* Path, const InterchangeOptions& Options, std::vector<ImportedModel>& Result)
{
    fastObjMesh* Source = fast_obj_read(Path);
    if (Source == nullptr) return false;

    const double UnitScale = Options.UnitScaleToCentimetres;   // OBJ has no unit metadata; the runtime supplies x1 by default.
    const int32_t AppendBase = static_cast<int32_t>(Result.size());

    // Each 'o' object becomes one ImportedModel. OBJ objects are same-tier with no enclosure metadata, so every emitted object is a
    //    source-root (EnclosureIndex -1). A file with no 'o' tag exposes a single synthetic object spanning every face.
    auto AppendObject = [&](const fastObjGroup& Object)
    {
        ImportedModel Model;
        bool TextureCoordinatesPresent = false;
        if (!TranslateObject(Source, Object, UnitScale, Model.Geometry, TextureCoordinatesPresent)) return;

        Model.TextureCoordinatesPresent = TextureCoordinatesPresent;
        Model.SourceIndex = static_cast<int32_t>(Result.size());
        Model.EnclosureIndex = -1;

        // Base colour: the diffuse (Kd) of the first face's material, else white. fast_obj face_materials is 1-indexed per face.
        if (Source->material_count > 0 && Object.face_count > 0)
        {
            const unsigned int MaterialSlot = Source->face_materials[Object.face_offset];
            if (MaterialSlot < Source->material_count)
            {
                const fastObjMaterial& Material = Source->materials[MaterialSlot];
                Model.BaseColourHint[0] = Material.Kd[0];
                Model.BaseColourHint[1] = Material.Kd[1];
                Model.BaseColourHint[2] = Material.Kd[2];
            }
        }

        ResolveTitle(Object.name, Model.SourceIndex - AppendBase, Model.Title, sizeof(Model.Title));
        Result.push_back(std::move(Model));
    };

    if (Source->object_count > 0)
    {
        for (unsigned int ObjectIndex = 0; ObjectIndex < Source->object_count; ++ObjectIndex)
            AppendObject(Source->objects[ObjectIndex]);
    }
    else
    {
        // No 'o' tag: treat the whole file as one object spanning every face from index 0.
        fastObjGroup WholeFile = {};
        WholeFile.name         = nullptr;
        WholeFile.face_count   = Source->face_count;
        WholeFile.face_offset  = 0;
        WholeFile.index_offset = 0;
        AppendObject(WholeFile);
    }

    fast_obj_destroy(Source);
    return true;
}

} // namespace Frontier
