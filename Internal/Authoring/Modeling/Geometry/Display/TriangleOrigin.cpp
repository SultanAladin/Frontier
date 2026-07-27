/*============================================================================================================================================
                                                            TRIANGLEORIGIN.CPP
============================================================================================================================================*/
// 🧩 Bounds-checked access to the display mirror's provenance map. The map is a plain parallel array (DisplayPolygonAssembly
//    fills it); this accessor exists so callers resolve a triangle's source by ordinal without hand-indexing the array.

#include "TriangleOrigin.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

TriangleOrigin QueryTriangleOrigin(const DisplayPolygons& Display, uint32_t TriangleOrdinal)
{
    if (TriangleOrdinal >= Display.TriangleOrigins.size()) return TriangleOrigin{};
    return Display.TriangleOrigins[TriangleOrdinal];
}

} // namespace Frontier
