/*==============================================================================================================================================
                                                         WORKSPACEDOCUMENTDECODER.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the .wsdoc → raster bridge. Decode the document, triangulate block 0 through the engine's ConstructRenderVertexStream, then
//    walk the placed objects: each object's LocalPlacement (translation cm, Euler degrees, per-axis scale) recomposes into the column-major model
//    matrix the vertex stage multiplies, plus the rotation-only 3x3 (padded to std140) that transforms normals. Tint and a running placement
//    ordinal (the visibility identity) come straight off the object. The recomposition is the inverse of the writer's DecomposePlacement, so a
//    round trip through the file reproduces the original hardcoded scene.

#include "Graphics/Scene/WorkspaceDocumentDecoder.h"

#include "Authoring/Geometry/Modeling/Display/DisplayPolygonAssembly.h"
#include "LinearAlgebra_Float64.h"

#include <cmath>
#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr double DegreesToRadians = 0.017453292519943295769236907684886;

// Compose the column-major world matrix World = Translate * RotZ * RotY * RotX * Scale from a LocalPlacement, matching the raster's Model[16]
// (element index Column*4 + Row, translation in the last column). Scale is per-axis; rotation is intrinsic X→Y→Z in degrees. Both Suzanne scenes
// only ever carry a +Z rotation, so this reduces to the writer's scale→rotate(+Z)→translate for them, but the general form keeps any authored
// placement honest.
void ComposeModelMatrix(const LocalPlacement& Placement, float OutModel[16])
{
    const double RadiansX = static_cast<double>(Placement.Rotation[0]) * DegreesToRadians;
    const double RadiansY = static_cast<double>(Placement.Rotation[1]) * DegreesToRadians;
    const double RadiansZ = static_cast<double>(Placement.Rotation[2]) * DegreesToRadians;
    const double CosX = std::cos(RadiansX), SinX = std::sin(RadiansX);
    const double CosY = std::cos(RadiansY), SinY = std::sin(RadiansY);
    const double CosZ = std::cos(RadiansZ), SinZ = std::sin(RadiansZ);

    // Rotation basis R = Rz * Ry * Rx (column vectors R0, R1, R2), before scale.
    const double R00 =  CosZ * CosY;
    const double R10 =  SinZ * CosY;
    const double R20 = -SinY;
    const double R01 =  CosZ * SinY * SinX - SinZ * CosX;
    const double R11 =  SinZ * SinY * SinX + CosZ * CosX;
    const double R21 =  CosY * SinX;
    const double R02 =  CosZ * SinY * CosX + SinZ * SinX;
    const double R12 =  SinZ * SinY * CosX - CosZ * SinX;
    const double R22 =  CosY * CosX;

    const double ScaleX = static_cast<double>(Placement.Scale[0]);
    const double ScaleY = static_cast<double>(Placement.Scale[1]);
    const double ScaleZ = static_cast<double>(Placement.Scale[2]);

    // Column 0 (scaled local +X).
    OutModel[0]  = static_cast<float>(R00 * ScaleX); OutModel[1]  = static_cast<float>(R10 * ScaleX); OutModel[2]  = static_cast<float>(R20 * ScaleX); OutModel[3]  = 0.0f;
    // Column 1 (scaled local +Y).
    OutModel[4]  = static_cast<float>(R01 * ScaleY); OutModel[5]  = static_cast<float>(R11 * ScaleY); OutModel[6]  = static_cast<float>(R21 * ScaleY); OutModel[7]  = 0.0f;
    // Column 2 (scaled local +Z).
    OutModel[8]  = static_cast<float>(R02 * ScaleZ); OutModel[9]  = static_cast<float>(R12 * ScaleZ); OutModel[10] = static_cast<float>(R22 * ScaleZ); OutModel[11] = 0.0f;
    // Column 3 (translation).
    OutModel[12] = Placement.Location[0]; OutModel[13] = Placement.Location[1]; OutModel[14] = Placement.Location[2]; OutModel[15] = 1.0f;
}

// The rotation-only normal basis (no scale) as three column vec3s padded to vec4 (std140), from the same Euler rotation. The upper-left 3x3 of a
// pure rotation is orthonormal, so its inverse-transpose equals itself and it transforms normals directly.
void ComposeNormalBasis(const LocalPlacement& Placement, float OutBasis[12])
{
    const double RadiansX = static_cast<double>(Placement.Rotation[0]) * DegreesToRadians;
    const double RadiansY = static_cast<double>(Placement.Rotation[1]) * DegreesToRadians;
    const double RadiansZ = static_cast<double>(Placement.Rotation[2]) * DegreesToRadians;
    const double CosX = std::cos(RadiansX), SinX = std::sin(RadiansX);
    const double CosY = std::cos(RadiansY), SinY = std::sin(RadiansY);
    const double CosZ = std::cos(RadiansZ), SinZ = std::sin(RadiansZ);

    // Column 0.
    OutBasis[0]  = static_cast<float>( CosZ * CosY);                     OutBasis[1]  = static_cast<float>( SinZ * CosY);                     OutBasis[2]  = static_cast<float>(-SinY);        OutBasis[3]  = 0.0f;
    // Column 1.
    OutBasis[4]  = static_cast<float>( CosZ * SinY * SinX - SinZ * CosX); OutBasis[5]  = static_cast<float>( SinZ * SinY * SinX + CosZ * CosX); OutBasis[6]  = static_cast<float>( CosY * SinX); OutBasis[7]  = 0.0f;
    // Column 2.
    OutBasis[8]  = static_cast<float>( CosZ * SinY * CosX + SinZ * SinX); OutBasis[9]  = static_cast<float>( SinZ * SinY * CosX - CosZ * SinX); OutBasis[10] = static_cast<float>( CosY * CosX); OutBasis[11] = 0.0f;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool LoadWorkspaceScene(const char*                        Path,
                        RenderVertexStream&                Geometry,
                        std::vector<SuzanneSceneInstance>& Instances,
                        WorkspaceDocument*                 Document,
                        std::vector<uint32_t>*             TriangleSourceFace)
{
    Geometry = RenderVertexStream{};
    Instances.clear();
    if (Document != nullptr)
        *Document = WorkspaceDocument{};
    if (TriangleSourceFace != nullptr)
        TriangleSourceFace->clear();

    WorkspaceDocument Decoded;
    if (!DecodeWorkspaceDocument(Path, Decoded))
    {
        std::fprintf(stderr, "[workspace-scene] decode failed: %s\n", Path ? Path : "(null)");
        return false;
    }
    if (Decoded.Geometry.empty() || Decoded.Objects.empty())
    {
        std::fprintf(stderr, "[workspace-scene] document has no geometry / objects: %s\n", Path ? Path : "(null)");
        return false;
    }

    // Single-block runtime path: block 0 is the shared head every object instances. Derive its GPU stream through the engine's own aggregate
    // triangulation. ConstructDisplayPolygons fan-triangulates identically to ConstructRenderVertexStream (same Stream), but ALSO records one
    // TriangleOrigin per emitted triangle — the source-face provenance the topology debug view needs. We hand the raster the identical Stream and
    // (optionally) lift the per-triangle SourceFace ordinal out of the parallel provenance map.
    DisplayPolygons Display;
    if (!ConstructDisplayPolygons(Decoded.Geometry[0].Geometry, Display))
    {
        std::fprintf(stderr, "[workspace-scene] geometry block 0 failed to triangulate: %s\n", Path);
        Geometry = RenderVertexStream{};
        return false;
    }
    Geometry = std::move(Display.Stream);

    if (TriangleSourceFace != nullptr)
    {
        TriangleSourceFace->reserve(Display.TriangleOrigins.size());
        for (size_t TriangleIterator = 0; TriangleIterator < Display.TriangleOrigins.size(); ++TriangleIterator)
            TriangleSourceFace->push_back(Display.TriangleOrigins[TriangleIterator].SourceFace);
    }

    Instances.reserve(Decoded.Objects.size());
    for (size_t ObjectIterator = 0; ObjectIterator < Decoded.Objects.size(); ++ObjectIterator)
    {
        const WorkspaceObject& Object = Decoded.Objects[ObjectIterator];
        if (Object.GeometryIndex != 0u)
            continue;   // single-block runtime: objects on other blocks are not drawn here

        SuzanneSceneInstance Instance;
        ComposeModelMatrix(Object.Placement, Instance.Model);
        ComposeNormalBasis(Object.Placement, Instance.NormalBasis);
        Instance.Tint[0] = Object.Tint[0];
        Instance.Tint[1] = Object.Tint[1];
        Instance.Tint[2] = Object.Tint[2];
        Instance.Tint[3] = 1.0f;
        Instance.PartitionId = static_cast<uint32_t>(Instances.size());
        Instances.push_back(Instance);
    }

    if (Instances.empty())
    {
        std::fprintf(stderr, "[workspace-scene] no objects reference geometry block 0: %s\n", Path);
        Geometry = RenderVertexStream{};
        return false;
    }

    if (Document != nullptr)
        *Document = std::move(Decoded);
    return true;
}

bool LoadFloorDocument(const char*                        Path,
                       RenderVertexStream&                Geometry,
                       std::vector<SuzanneSceneInstance>& Instances)
{
    // The floor decode is exactly the main scene decode — triangulate block 0, recompose its objects into instances — only the caller's intent differs
    // (a second mesh in the shared buffer, not more heads). Delegate rather than duplicate: the floor doc needs no document registration and no
    // per-triangle provenance (there is no topology-wireframe view of the floor), so both optional outputs are dropped.
    return LoadWorkspaceScene(Path, Geometry, Instances, nullptr, nullptr);
}

} // namespace Frontier
