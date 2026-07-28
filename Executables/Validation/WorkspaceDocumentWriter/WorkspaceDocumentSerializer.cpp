/*==============================================================================================================================================
                                                        WORKSPACEDOCUMENTSERIALIZER.CPP
==============================================================================================================================================*/
// 🧩 A one-shot authoring tool that bakes the two hardcoded Suzanne test scenes into loadable WorkspaceDocument files, so the renderer can stop
//    constructing them in C++ and just DECODE them. It reads each scene's reference-geometry JSON ONCE into a shared authoring PolygonCluster (this
//    is the only place the flat-JSON scanner lives — it is authoring-time, never in the runtime), then walks the hardcoded BuildSuzanneScene
//    instance list, decomposing each column-major model matrix back into the TRS a LocalPlacement holds (uniform scale + a +Z Euler + translation,
//    which is exactly how ComposeModelMatrix built it). One shared geometry block + N placed objects become one WorkspaceDocument, encoded to
//    <scene>.wsdoc. Run once; the .wsdoc files are then the raster's geometry source. Reuses PolygonCluster + EncodeWorkspaceDocument + the scene
//    builder — it duplicates no geometry type and no scene layout.

#define _CRT_SECURE_NO_WARNINGS

#include "Authoring/Geometry/Interchange/WorkspaceDocumentEncoder.h"
#include "Graphics/Scene/SuzanneScene.h"

#include "LinearAlgebra_Float64.h"
#include "PolygonCluster.h"
#include "VertexField.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace Frontier;

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// Read the whole file into a string. Empty return signals a read failure (an empty file is also a failure here — no geometry).
std::string ReadWholeFile(const char* FilePath)
{
    std::string Text;
    FILE* Handle = std::fopen(FilePath, "rb");
    if (Handle == nullptr)
        return Text;
    std::fseek(Handle, 0, SEEK_END);
    long Size = std::ftell(Handle);
    std::fseek(Handle, 0, SEEK_SET);
    if (Size > 0)
    {
        Text.resize(static_cast<size_t>(Size));
        size_t Read = std::fread(&Text[0], 1, static_cast<size_t>(Size), Handle);
        if (Read != static_cast<size_t>(Size))
            Text.clear();
    }
    std::fclose(Handle);
    return Text;
}

// Scan the comma-separated number run inside the [ ... ] that follows the first occurrence of Key in Text, appending each parsed value to Out.
// Trusted-content scanner (the reference JSON is engine-authored, not user input): it does not validate JSON structure beyond the bracket span.
bool ScanNumberArray(const std::string& Text, const char* Key, std::vector<double>& Out)
{
    const size_t KeyPosition = Text.find(Key);
    if (KeyPosition == std::string::npos)
        return false;
    const size_t OpenBracket = Text.find('[', KeyPosition);
    if (OpenBracket == std::string::npos)
        return false;
    const size_t CloseBracket = Text.find(']', OpenBracket);
    if (CloseBracket == std::string::npos)
        return false;

    const char* Cursor = Text.c_str() + OpenBracket + 1;
    const char* End    = Text.c_str() + CloseBracket;
    while (Cursor < End)
    {
        while (Cursor < End && (*Cursor == ',' || *Cursor == ' ' || *Cursor == '\n' || *Cursor == '\r' || *Cursor == '\t'))
            ++Cursor;
        if (Cursor >= End)
            break;
        char* AfterNumber = nullptr;
        double Parsed = std::strtod(Cursor, &AfterNumber);
        if (AfterNumber == Cursor)
            break;
        Out.push_back(Parsed);
        Cursor = AfterNumber;
    }
    return true;
}

// Read the integer value that follows "Key": (e.g. "vertexCount": 507). Returns false if the key is absent.
bool ScanScalarCount(const std::string& Text, const char* Key, uint32_t& Out)
{
    const size_t KeyPosition = Text.find(Key);
    if (KeyPosition == std::string::npos)
        return false;
    const size_t Colon = Text.find(':', KeyPosition);
    if (Colon == std::string::npos)
        return false;
    Out = static_cast<uint32_t>(std::strtoul(Text.c_str() + Colon + 1, nullptr, 10));
    return true;
}

// Turn one reference-geometry JSON file into a shared authoring PolygonCluster: positions + normals populate the VertexField, and every triangle
// (3 consecutive indices) becomes one 3-corner face so ConstructRenderVertexStream fan-triangulates it back to the identical triangle list the
// runtime raster consumes today. UVs are absent (the reference body carries none). Returns false with Cluster untouched on a malformed file.
bool ReadReferenceCluster(const char* JsonPath, PolygonCluster& Cluster)
{
    const std::string Text = ReadWholeFile(JsonPath);
    if (Text.empty())
    {
        std::fprintf(stderr, "[writer] read failed or empty: %s\n", JsonPath);
        return false;
    }

    uint32_t VertexCount   = 0u;
    uint32_t TriangleCount = 0u;
    if (!ScanScalarCount(Text, "\"vertexCount\"", VertexCount) || !ScanScalarCount(Text, "\"triCount\"", TriangleCount))
    {
        std::fprintf(stderr, "[writer] missing vertexCount / triCount: %s\n", JsonPath);
        return false;
    }

    std::vector<double> Positions;
    std::vector<double> Normals;
    std::vector<double> Indices;
    const bool HavePositions = ScanNumberArray(Text, "\"positions\"", Positions);
    ScanNumberArray(Text, "\"normals\"", Normals);   // optional
    const bool HaveIndices   = ScanNumberArray(Text, "\"indices\"", Indices);
    if (!HavePositions || !HaveIndices)
    {
        std::fprintf(stderr, "[writer] missing positions / indices: %s\n", JsonPath);
        return false;
    }
    if (Positions.size() != static_cast<size_t>(VertexCount) * 3 || Indices.size() != static_cast<size_t>(TriangleCount) * 3)
    {
        std::fprintf(stderr, "[writer] array length disagrees with declared counts: %s\n", JsonPath);
        return false;
    }
    const bool NormalsPresent = Normals.size() == static_cast<size_t>(VertexCount) * 3;

    PolygonCluster Built;
    Built.Attributes.Position.resize(VertexCount);
    if (NormalsPresent)
        Built.Attributes.Normal.resize(VertexCount);
    for (uint32_t VertexIterator = 0; VertexIterator < VertexCount; ++VertexIterator)
    {
        Built.Attributes.Position[VertexIterator] =
            Vector3d{ Positions[VertexIterator * 3 + 0], Positions[VertexIterator * 3 + 1], Positions[VertexIterator * 3 + 2] };
        if (NormalsPresent)
            Built.Attributes.Normal[VertexIterator] =
                Vector3d{ Normals[VertexIterator * 3 + 0], Normals[VertexIterator * 3 + 1], Normals[VertexIterator * 3 + 2] };
    }

    Built.FaceVertexIndices.reserve(Indices.size());
    Built.FaceVertexCounts.reserve(TriangleCount);
    for (size_t IndexIterator = 0; IndexIterator < Indices.size(); ++IndexIterator)
    {
        const uint32_t VertexIndex = static_cast<uint32_t>(Indices[IndexIterator] + 0.5);
        if (VertexIndex >= VertexCount)
        {
            std::fprintf(stderr, "[writer] index out of range: %s\n", JsonPath);
            return false;
        }
        Built.FaceVertexIndices.push_back(VertexIndex);
    }
    for (uint32_t TriangleIterator = 0; TriangleIterator < TriangleCount; ++TriangleIterator)
        Built.FaceVertexCounts.push_back(3u);

    Cluster = std::move(Built);
    return true;
}

// Turn one Wavefront .obj into a shared authoring PolygonCluster, PRESERVING each face's authored corner count — a quad `f a b c d` becomes one
// 4-corner face, a triangle `f a b c` one 3-corner face. This is the difference that makes the topology wireframe meaningful: the runtime
// ConstructDisplayPolygons fan-triangulates each face at load and records which authored face every display triangle came from, so the resolve can
// collapse the internal quad diagonals. Only positions (`v`) and faces (`f`) are read — normals (`vn`) and UVs (`vt`) are intentionally dropped
// here (this bake feeds the position-only visibility raster; normals return when deferred shading needs them). Corner tokens are `v`, `v/t`,
// `v//n`, or `v/t/n`; the leading vertex index is 1-based (a negative index counts back from the current vertex total). Returns false with Cluster
// untouched on a missing / empty file or an out-of-range index.
bool ReadObjCluster(const char* ObjPath, PolygonCluster& Cluster)
{
    const std::string Text = ReadWholeFile(ObjPath);
    if (Text.empty())
    {
        std::fprintf(stderr, "[writer] read failed or empty: %s\n", ObjPath);
        return false;
    }

    PolygonCluster Built;
    const char* Cursor = Text.c_str();
    const char* End    = Text.c_str() + Text.size();
    while (Cursor < End)
    {
        // Isolate this line [LineStart, LineEnd).
        const char* LineStart = Cursor;
        while (Cursor < End && *Cursor != '\n')
            ++Cursor;
        const char* LineEnd = Cursor;
        if (Cursor < End)
            ++Cursor;   // step past the newline for the next iteration

        // Skip leading whitespace to the first token.
        const char* Scan = LineStart;
        while (Scan < LineEnd && (*Scan == ' ' || *Scan == '\t' || *Scan == '\r'))
            ++Scan;
        if (Scan >= LineEnd)
            continue;

        if (Scan[0] == 'v' && (Scan + 1) < LineEnd && (Scan[1] == ' ' || Scan[1] == '\t'))
        {
            // Vertex position: v x y z.
            double Coordinates[3] = { 0.0, 0.0, 0.0 };
            const char* NumberCursor = Scan + 1;
            for (int AxisIterator = 0; AxisIterator < 3; ++AxisIterator)
            {
                char* AfterNumber = nullptr;
                Coordinates[AxisIterator] = std::strtod(NumberCursor, &AfterNumber);
                if (AfterNumber == NumberCursor)
                    break;
                NumberCursor = AfterNumber;
            }
            // Blender exports the .obj right-handed / Z-up, and the engine world (CoordinateSpace.h) is ALSO right-handed /
            // Z-up — so the axis convert is the IDENTITY: no swap, no negation. An earlier (x,z,-y) / (-x,z,y) swap assumed a
            // Y-up engine and was wrong (it laid the head on its side, then face-down + mirrored). Positions pass straight
            // through; only the centroid re-centre below runs, so the instance rotation spins each head in place. If a DCC that
            // is Y-up (Maya, glTF) is imported later, THAT source needs a (x, z, -y) convert applied here first.
            Built.Attributes.Position.push_back(Vector3d{ Coordinates[0], Coordinates[1], Coordinates[2] });
        }
        else if (Scan[0] == 'f' && (Scan + 1) < LineEnd && (Scan[1] == ' ' || Scan[1] == '\t'))
        {
            // Face: one corner per whitespace-separated token; each token's leading integer is the (1-based, or negative-relative) vertex index.
            const char* TokenCursor = Scan + 1;
            uint32_t    CornerCount = 0u;
            while (TokenCursor < LineEnd)
            {
                while (TokenCursor < LineEnd && (*TokenCursor == ' ' || *TokenCursor == '\t' || *TokenCursor == '\r'))
                    ++TokenCursor;
                if (TokenCursor >= LineEnd)
                    break;
                char* AfterInteger = nullptr;
                const long Reference = std::strtol(TokenCursor, &AfterInteger, 10);
                if (AfterInteger == TokenCursor)
                    break;
                // Resolve to a 0-based index: positive is 1-based, negative counts back from the current position total.
                const long ResolvedIndex = (Reference > 0)
                                         ? (Reference - 1)
                                         : (static_cast<long>(Built.Attributes.Position.size()) + Reference);
                if (ResolvedIndex < 0 || ResolvedIndex >= static_cast<long>(Built.Attributes.Position.size()))
                {
                    std::fprintf(stderr, "[writer] face index out of range: %s\n", ObjPath);
                    return false;
                }
                Built.FaceVertexIndices.push_back(static_cast<uint32_t>(ResolvedIndex));
                ++CornerCount;
                // Advance past the rest of this token (the /t/n portion, if any).
                while (TokenCursor < LineEnd && *TokenCursor != ' ' && *TokenCursor != '\t' && *TokenCursor != '\r')
                    ++TokenCursor;
            }
            if (CornerCount >= 3u)
                Built.FaceVertexCounts.push_back(CornerCount);
            else if (CornerCount > 0u)
            {
                // A degenerate face (1-2 corners) would desync FaceVertexIndices from FaceVertexCounts — drop its stray indices.
                Built.FaceVertexIndices.resize(Built.FaceVertexIndices.size() - CornerCount);
            }
        }
    }

    if (Built.Attributes.Position.empty() || Built.FaceVertexCounts.empty())
    {
        std::fprintf(stderr, "[writer] no positions / faces parsed: %s\n", ObjPath);
        return false;
    }

    // Re-centre the body on the origin. The .obj sits on the ground plane (centroid ~+0.78 in Y), but the instance matrices rotate each head ABOUT
    // THE ORIGIN to fan them out radially — the same convention the origin-centred reference JSON body used. An off-origin body swings through an arc
    // under that rotation and reads as tilted (the snout points up instead of outward). Subtracting the centroid restores the in-place spin, so the
    // face points radially outward again. Pure translation — winding, face topology, and the +Z snout direction are untouched.
    Vector3d Centroid{ 0.0, 0.0, 0.0 };
    for (size_t VertexIterator = 0; VertexIterator < Built.Attributes.Position.size(); ++VertexIterator)
    {
        Centroid.XCoord += Built.Attributes.Position[VertexIterator].XCoord;
        Centroid.YCoord += Built.Attributes.Position[VertexIterator].YCoord;
        Centroid.ZCoord += Built.Attributes.Position[VertexIterator].ZCoord;
    }
    const double VertexTotal = static_cast<double>(Built.Attributes.Position.size());
    Centroid.XCoord /= VertexTotal;
    Centroid.YCoord /= VertexTotal;
    Centroid.ZCoord /= VertexTotal;
    for (size_t VertexIterator = 0; VertexIterator < Built.Attributes.Position.size(); ++VertexIterator)
    {
        Built.Attributes.Position[VertexIterator].XCoord -= Centroid.XCoord;
        Built.Attributes.Position[VertexIterator].YCoord -= Centroid.YCoord;
        Built.Attributes.Position[VertexIterator].ZCoord -= Centroid.ZCoord;
    }

    Cluster = std::move(Built);
    return true;
}

// Recover the TRS a LocalPlacement holds from one instance's column-major model matrix. ComposeModelMatrix built every instance as uniform
// scale -> +Z rotation -> translation, so the decomposition is exact: translation is column 3, uniform scale is the length of column 0, and the
// +Z Euler angle (degrees) is atan2 of the normalized column-0 (cos, sin) pair. Rotation X / Y are zero by construction.
LocalPlacement DecomposePlacement(const float Model[16])
{
    LocalPlacement Placement;

    Placement.Location[0] = Model[12];
    Placement.Location[1] = Model[13];
    Placement.Location[2] = Model[14];

    const double Column0X = Model[0];
    const double Column0Y = Model[1];
    const double ScaleFactor = std::sqrt(Column0X * Column0X + Column0Y * Column0Y);
    Placement.Scale[0] = static_cast<float>(ScaleFactor);
    Placement.Scale[1] = static_cast<float>(ScaleFactor);
    Placement.Scale[2] = static_cast<float>(ScaleFactor);

    const double RadiansToDegrees = 57.295779513082320876798154814105;
    double AngleDegrees = 0.0;
    if (ScaleFactor > 1e-9)
        AngleDegrees = std::atan2(Column0Y / ScaleFactor, Column0X / ScaleFactor) * RadiansToDegrees;
    Placement.Rotation[0] = 0.0f;
    Placement.Rotation[1] = 0.0f;
    Placement.Rotation[2] = static_cast<float>(AngleDegrees);

    return Placement;
}

// Stand the shared head upright: rotate every local vertex position (and normal, if present) +90 degrees about the local +X axis. The authored
// Suzzane.obj sits crown-toward-camera in Blender's convention, which renders face-down / upside-down under the engine's Z-up world; a +90-degree
// X turn (Y -> Z, Z -> -Y) lifts the crown to +Z so the head stands up before the instance's +Z facing spin places it on the ring. Pure per-vertex
// rotation baked into the geometry once, so it applies to every instance and to both scene sources without touching the Z-only placement decompose.
void StandGeometryUpright(PolygonCluster& Cluster)
{
    for (size_t VertexIterator = 0; VertexIterator < Cluster.Attributes.Position.size(); ++VertexIterator)
    {
        Vector3d& Position = Cluster.Attributes.Position[VertexIterator];
        const double OriginalY = Position.YCoord;
        const double OriginalZ = Position.ZCoord;
        Position.YCoord = -OriginalZ;
        Position.ZCoord =  OriginalY;
    }
    for (size_t NormalIterator = 0; NormalIterator < Cluster.Attributes.Normal.size(); ++NormalIterator)
    {
        Vector3d& Normal = Cluster.Attributes.Normal[NormalIterator];
        const double OriginalY = Normal.YCoord;
        const double OriginalZ = Normal.ZCoord;
        Normal.YCoord = -OriginalZ;
        Normal.ZCoord =  OriginalY;
    }
}

// Build a rectangular floor slab as an authoring PolygonCluster: an axis-aligned box SpanX by SpanY wide and Thickness deep, its TOP face lying on
// the world Z = 0 plane (so the slab sits just under the ground, heads resting on it) and centred on the origin in X / Y. Eight corners, six quad
// faces wound counter-clockwise when viewed from outside (matching the raster's front-face convention so back-face cull keeps the outward faces).
// Normals are the six axis directions, one per corner reference. This is the floor's geometry block — one placed object instances it at identity.
void BuildFloorSlabCluster(double SpanX, double SpanY, double Thickness, PolygonCluster& Cluster)
{
    PolygonCluster Built;

    const double HalfX = SpanX * 0.5;
    const double HalfY = SpanY * 0.5;
    const double TopZ    = 0.0;             // top face on the ground plane
    const double BottomZ = -Thickness;      // slab extends downward

    // Eight corners: 0-3 bottom (z = BottomZ), 4-7 top (z = TopZ), each ring wound counter-clockwise seen from +Z.
    Built.Attributes.Position =
    {
        Vector3d{ -HalfX, -HalfY, BottomZ },   // 0
        Vector3d{  HalfX, -HalfY, BottomZ },   // 1
        Vector3d{  HalfX,  HalfY, BottomZ },   // 2
        Vector3d{ -HalfX,  HalfY, BottomZ },   // 3
        Vector3d{ -HalfX, -HalfY, TopZ    },   // 4
        Vector3d{  HalfX, -HalfY, TopZ    },   // 5
        Vector3d{  HalfX,  HalfY, TopZ    },   // 6
        Vector3d{ -HalfX,  HalfY, TopZ    },   // 7
    };

    // Six quad faces, each wound counter-clockwise when viewed from OUTSIDE the slab (so the outward normal faces the viewer and back-face cull
    // keeps it). Top (+Z), bottom (-Z), and the four sides.
    const uint32_t FaceCorners[6][4] =
    {
        { 4, 5, 6, 7 },   // +Z top    (viewed from above)
        { 3, 2, 1, 0 },   // -Z bottom (viewed from below)
        { 0, 1, 5, 4 },   // -Y front
        { 2, 3, 7, 6 },   // +Y back
        { 1, 2, 6, 5 },   // +X right
        { 3, 0, 4, 7 },   // -X left
    };

    Built.FaceVertexIndices.reserve(24);
    Built.FaceVertexCounts.reserve(6);
    for (uint32_t FaceIterator = 0; FaceIterator < 6; ++FaceIterator)
    {
        for (uint32_t CornerIterator = 0; CornerIterator < 4; ++CornerIterator)
            Built.FaceVertexIndices.push_back(FaceCorners[FaceIterator][CornerIterator]);
        Built.FaceVertexCounts.push_back(4u);
    }

    Cluster = std::move(Built);
}

// Bake the floor into its own WorkspaceDocument: one geometry block (the slab) plus ONE placed object at identity, tinted a neutral grey. This is the
// same shape as BakeScene (one shared block + placed objects referencing block 0) but for a single-instance standalone floor — no material yet, the
// checker returns when a material lands. The slab is authored upright in world space, so no StandGeometryUpright turn is applied here.
bool BakeFloorDocument(double SpanX, double SpanY, double Thickness, const char* BlockTitle, WorkspaceDocument& Document)
{
    WorkspaceGeometryBlock Block;
    Block.Title = BlockTitle;
    BuildFloorSlabCluster(SpanX, SpanY, Thickness, Block.Geometry);
    Document.Geometry.push_back(std::move(Block));

    WorkspaceObject Object;
    Object.GeometryIndex  = 0u;
    Object.Title          = BlockTitle;
    Object.Placement      = LocalPlacement{};        // identity TRS: the slab is authored in world space
    Object.Tint[0]        = 0.34f;                   // neutral grey (linear RGB) — checker pattern arrives with the material
    Object.Tint[1]        = 0.34f;
    Object.Tint[2]        = 0.34f;
    Object.EnclosureIndex = -1;
    Document.Objects.push_back(std::move(Object));
    return true;
}

// Bake one scene into a WorkspaceDocument: read its shared head into a single geometry block, then turn every hardcoded instance into a placed
// object referencing block 0 (title "<BlockTitle> NNN", TRS from the instance matrix, tint from the instance, document-root nesting).
bool BakeScene(SuzanneSceneChoice Choice, const char* GeometryPath, bool SourceIsObj, const char* BlockTitle, WorkspaceDocument& Document)
{
    WorkspaceGeometryBlock Block;
    Block.Title = BlockTitle;
    const bool GeometryRead = SourceIsObj ? ReadObjCluster(GeometryPath, Block.Geometry)
                                          : ReadReferenceCluster(GeometryPath, Block.Geometry);
    if (!GeometryRead)
        return false;
    StandGeometryUpright(Block.Geometry);
    Document.Geometry.push_back(std::move(Block));

    std::vector<SuzanneSceneInstance> Instances;
    BuildSuzanneScene(Choice, 0, Instances);

    Document.Objects.reserve(Instances.size());
    for (size_t InstanceIterator = 0; InstanceIterator < Instances.size(); ++InstanceIterator)
    {
        const SuzanneSceneInstance& Instance = Instances[InstanceIterator];
        WorkspaceObject Object;
        Object.GeometryIndex  = 0u;
        Object.Title          = std::string(BlockTitle) + " " + std::to_string(InstanceIterator);
        Object.Placement      = DecomposePlacement(Instance.Model);
        Object.Tint[0]        = Instance.Tint[0];
        Object.Tint[1]        = Instance.Tint[1];
        Object.Tint[2]        = Instance.Tint[2];
        Object.EnclosureIndex = -1;
        Document.Objects.push_back(std::move(Object));
    }
    return true;
}

// Bake one scene and write it out; report the outcome. Returns false on any failure so main can carry a non-zero exit.
bool BakeAndWrite(SuzanneSceneChoice Choice, const char* GeometryPath, bool SourceIsObj, const char* BlockTitle, const std::string& OutputPath)
{
    WorkspaceDocument Document;
    if (!BakeScene(Choice, GeometryPath, SourceIsObj, BlockTitle, Document))
    {
        std::fprintf(stderr, "[writer] bake failed: %s\n", OutputPath.c_str());
        return false;
    }
    if (!EncodeWorkspaceDocument(OutputPath.c_str(), Document))
    {
        std::fprintf(stderr, "[writer] encode failed: %s\n", OutputPath.c_str());
        return false;
    }
    std::printf("[writer] wrote %s (%u geometry blocks, %u objects)\n",
                OutputPath.c_str(),
                static_cast<unsigned>(Document.Geometry.size()),
                static_cast<unsigned>(Document.Objects.size()));
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                            ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

// Usage: WorkspaceDocumentWriter <asset-dir> [output-dir]. <asset-dir> holds SuzanneMesh.json + SuzanneMeshSub2.json; output-dir (default =
// asset-dir) receives SuzanneRadial.wsdoc + SuzannePyramid.wsdoc. The Build.bat passes the docs Assets dir so the .wsdoc land beside their source.
int main(int ArgumentCount, char** ArgumentValues)
{
    if (ArgumentCount < 2)
    {
        std::fprintf(stderr, "usage: %s <asset-dir> [output-dir]\n", ArgumentValues[0]);
        return 2;
    }
    const std::string AssetDir  = ArgumentValues[1];
    const std::string OutputDir = (ArgumentCount >= 3) ? std::string(ArgumentValues[2]) : AssetDir;

    // The Radial scene now bakes from the authored Wavefront .obj (mixed quads + tris) so the topology wireframe has real face boundaries to
    // resolve — the old flat-triangle SuzanneMesh.json produced an all-triangle document with no quad provenance. The Pyramid scene keeps its JSON
    // source (unchanged). The .obj lives in the content repo, referenced absolutely (it is not part of this tool's Assets dir).
    const char*       RadialObj   = "C:/Users/OS/Documents/Frontier/EngineContent/GeometryArchives/ReferenceGeometry/Suzzane.obj";
    const std::string PyramidJson = AssetDir + "/SuzanneMeshSub2.json";
    const std::string RadialOut   = OutputDir + "/SuzanneRadial.wsdoc";
    const std::string PyramidOut  = OutputDir + "/SuzannePyramid.wsdoc";

    bool AllSucceeded = true;
    AllSucceeded &= BakeAndWrite(SuzanneSceneChoice::RadialArray,   RadialObj,           true,  "Suzanne", RadialOut);
    AllSucceeded &= BakeAndWrite(SuzanneSceneChoice::PyramidStress, PyramidJson.c_str(), false, "Suzanne", PyramidOut);

    // The checkered floor is a standalone document: a single 100 x 100 m slab, 0.5 m thick, its top on the ground plane. Baked as a real mesh (one
    // geometry block + one placed object) exactly like the Suzanne heads, so the renderer LOADS it — no in-C++ floor. Grey now; the checker pattern
    // returns when a material is added. The renderer loads this doc alongside the chosen Suzanne scene and draws both into the shared visibility buffer.
    const std::string FloorOut = OutputDir + "/CheckerFloor.wsdoc";
    WorkspaceDocument FloorDocument;
    if (BakeFloorDocument(100.0, 100.0, 0.5, "CheckerFloor", FloorDocument) && EncodeWorkspaceDocument(FloorOut.c_str(), FloorDocument))
        std::printf("[writer] wrote %s (%u geometry blocks, %u objects)\n",
                    FloorOut.c_str(),
                    static_cast<unsigned>(FloorDocument.Geometry.size()),
                    static_cast<unsigned>(FloorDocument.Objects.size()));
    else
    {
        std::fprintf(stderr, "[writer] floor bake / encode failed: %s\n", FloorOut.c_str());
        AllSucceeded = false;
    }

    return AllSucceeded ? 0 : 1;
}
