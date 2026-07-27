/*============================================================================================================================================
                                                             VERTEXFIELD.CPP
============================================================================================================================================*/
// 🧩 Structure-of-arrays vertex attribute storage. Optional arrays (Normal / TextureCoordinate / Color) lazily grow to the
//    Position length on first write so callers never have to pre-size them, and absence stays detectable by an empty array.

#include "VertexField.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Grow an optional Vector3d attribute array to the Position length, padding new slots with zero, then write Target.
    bool RefreshOptionalVector3(std::vector<Vector3d>& Attribute,
                                uint32_t               VertexCount,
                                uint32_t               VertexIndex,
                                const Vector3d&        Target)
    {
        if (VertexIndex >= VertexCount) return false;
        if (Attribute.size() < VertexCount) Attribute.resize(VertexCount, Vector3d{ 0.0, 0.0, 0.0 });
        Attribute[VertexIndex] = Target;
        return true;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

uint32_t AccumulateVertexPosition(VertexField& Field, const Vector3d& Position)
{
    const uint32_t NewIndex = (uint32_t)Field.Position.size();
    Field.Position.push_back(Position);
    return NewIndex;
}

bool RefreshVertexNormal(VertexField& Field, uint32_t VertexIndex, const Vector3d& Normal)
{
    return RefreshOptionalVector3(Field.Normal, (uint32_t)Field.Position.size(), VertexIndex, Normal);
}

bool RefreshVertexTexture(VertexField& Field, uint32_t VertexIndex, const Vector2d& TextureCoordinate)
{
    const uint32_t VertexCount = (uint32_t)Field.Position.size();
    if (VertexIndex >= VertexCount) return false;
    if (Field.TextureCoordinate.size() < VertexCount) Field.TextureCoordinate.resize(VertexCount, Vector2d{ 0.0, 0.0 });
    Field.TextureCoordinate[VertexIndex] = TextureCoordinate;
    return true;
}

bool RefreshVertexColor(VertexField& Field, uint32_t VertexIndex, const Vector3d& Color)
{
    return RefreshOptionalVector3(Field.Color, (uint32_t)Field.Position.size(), VertexIndex, Color);
}

uint32_t EvaluateVertexCount(const VertexField& Field)
{
    return (uint32_t)Field.Position.size();
}

void ResetVertexField(VertexField& Field)
{
    Field.Position.clear();
    Field.Normal.clear();
    Field.TextureCoordinate.clear();
    Field.Color.clear();
}

} // namespace Frontier
