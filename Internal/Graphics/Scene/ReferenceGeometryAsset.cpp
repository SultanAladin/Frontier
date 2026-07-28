/*==============================================================================================================================================
                                                         REFERENCEGEOMETRYASSET.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the reference-geometry JSON load boundary. The engine's asset files are a flat, trusted shape — three top-level number
//    arrays ("positions", "normals", "indices") with scalar counts — so the reader is a purpose-built scanner over the three named arrays rather
//    than a general JSON parser: locate a key, read the bracketed run of comma-separated numbers after it. Positions and normals interleave into
//    stride-32 RenderVertex (texcoord zeroed); indices copy straight across as the uint32 triangle list. Every array length is checked against the
//    declared counts before anything is emitted, so a truncated or out-of-range file fails cleanly with an empty Result.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/Scene/ReferenceGeometryAsset.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

void ReportGeometryAsset(const char* MessageText, const char* Detail)
{
    std::fprintf(stderr, "[GeometryAsset] %s: %s\n", MessageText, Detail ? Detail : "");
}

// Read the whole file into a string. Empty return signals a read failure (an empty file is also a failure here — no geometry to load).
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
        Text.resize((size_t)Size);
        size_t Read = std::fread(&Text[0], 1, (size_t)Size, Handle);
        if (Read != (size_t)Size)
            Text.clear();
    }
    std::fclose(Handle);
    return Text;
}

// Scan the comma-separated number run inside the [ ... ] that follows the first occurrence of "Key" in Text, appending each parsed value to Out.
// Returns false if the key or its opening bracket is absent. Trusted-content scanner: it does not validate JSON structure beyond the bracket span.
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
        // Skip separators / whitespace to the next number token.
        while (Cursor < End && (*Cursor == ',' || *Cursor == ' ' || *Cursor == '\n' || *Cursor == '\r' || *Cursor == '\t'))
            ++Cursor;
        if (Cursor >= End)
            break;
        char* AfterNumber = nullptr;
        double Parsed = std::strtod(Cursor, &AfterNumber);
        if (AfterNumber == Cursor)   // no progress — not a number; stop
            break;
        Out.push_back(Parsed);
        Cursor = AfterNumber;
    }
    return true;
}

// Read the integer value that follows "Key": (e.g. "vertexCount": 507). Returns false if the key is absent. Used for the declared counts.
bool ScanScalarCount(const std::string& Text, const char* Key, uint32_t& Out)
{
    const size_t KeyPosition = Text.find(Key);
    if (KeyPosition == std::string::npos)
        return false;
    const size_t Colon = Text.find(':', KeyPosition);
    if (Colon == std::string::npos)
        return false;
    Out = (uint32_t)std::strtoul(Text.c_str() + Colon + 1, nullptr, 10);
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool LoadReferenceMeshAsset(const char* FilePath, RenderVertexStream& Result)
{
    Result = RenderVertexStream{};
    if (FilePath == nullptr)
        return false;

    const std::string Text = ReadWholeFile(FilePath);
    if (Text.empty())
    {
        ReportGeometryAsset("file read failed or empty", FilePath);
        return false;
    }

    uint32_t VertexCount = 0;
    uint32_t TriangleCount = 0;
    if (!ScanScalarCount(Text, "\"vertexCount\"", VertexCount) || !ScanScalarCount(Text, "\"triCount\"", TriangleCount))
    {
        ReportGeometryAsset("missing vertexCount / triCount", FilePath);
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
        ReportGeometryAsset("missing positions / indices array", FilePath);
        return false;
    }

    if (Positions.size() != (size_t)VertexCount * 3 || Indices.size() != (size_t)TriangleCount * 3)
    {
        ReportGeometryAsset("array length does not match declared counts", FilePath);
        return false;
    }
    const bool NormalsPresent = Normals.size() == (size_t)VertexCount * 3;

    Result.Vertices.resize(VertexCount);
    for (uint32_t VertexIterator = 0; VertexIterator < VertexCount; ++VertexIterator)
    {
        RenderVertex& Vertex = Result.Vertices[VertexIterator];
        Vertex.Position[0] = (float)Positions[VertexIterator * 3 + 0];
        Vertex.Position[1] = (float)Positions[VertexIterator * 3 + 1];
        Vertex.Position[2] = (float)Positions[VertexIterator * 3 + 2];
        if (NormalsPresent)
        {
            Vertex.Normal[0] = (float)Normals[VertexIterator * 3 + 0];
            Vertex.Normal[1] = (float)Normals[VertexIterator * 3 + 1];
            Vertex.Normal[2] = (float)Normals[VertexIterator * 3 + 2];
        }
        Vertex.TextureCoordinate[0] = 0.0f;
        Vertex.TextureCoordinate[1] = 0.0f;
    }

    Result.Indices.resize(Indices.size());
    for (size_t IndexIterator = 0; IndexIterator < Indices.size(); ++IndexIterator)
    {
        const uint32_t Value = (uint32_t)(Indices[IndexIterator] + 0.5);
        if (Value >= VertexCount)
        {
            ReportGeometryAsset("index out of range", FilePath);
            Result = RenderVertexStream{};
            return false;
        }
        Result.Indices[IndexIterator] = Value;
    }

    return true;
}

} // namespace Frontier
