/*============================================================================================================================================
                                                          PATTERNTHUMBNAILRASTER.H
============================================================================================================================================*/
// 🧩 CPU rasterization of cached gallery thumbnail shapes into atlas-ready RGBA pixels; the GPU uploads each page once.

#pragma once
#include "UI/TextureStudio/PatternWorkspacePanel.h"
#include <cstdint>
#include <vector>

namespace Frontier
{
// 📝 Tile space 0..512 maps to Extent×Extent RGBA8; shapes paint in order with source-over blending.
[[nodiscard]] std::vector<uint8_t> RasterizePatternThumbnail(
    const std::vector<PatternWorkspacePanel::PatternThumbnailShape> &Shapes, uint32_t Extent, uint32_t BackgroundAbgr);
// 📝 Blits square thumbs into a Columns×Columns grid with replicated 1px gutters, matching MaterialThumbnailAtlas slots.
[[nodiscard]] std::vector<uint8_t> BlitPatternThumbnailAtlas(
    const std::vector<std::vector<uint8_t>> &Thumbs, uint32_t TileExtent, uint32_t Columns, uint32_t BackgroundAbgr);
}
