//==========================================================================================================================================
//                                                      NEARESTDEPTHSPACE.CPP
//==========================================================================================================================================
//  The nearest-depth pyramid over the screen: level extents, dispatch counts, and the host mirror of the
//  reduction chain. Ported 1:1 from Experimental/VehicleHmiPrototype/Source/NearestDepthSpace.js.
//==========================================================================================================================================

#include "NearestDepthSpace.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

void NearestDepthSpace::Configure(uint32_t ScreenWidth, uint32_t ScreenHeight)
{
    Width = ScreenWidth;
    Height = ScreenHeight;
    if (Width == 0u || Height == 0u)
    {
        Levels = 0u;
        Extents.clear();
        return;
    }

    // 📝 Level extents follow max(1, ⌊extent / 2^L⌋) down to a single texel, the chain WebGPU itself defines.
    Levels = static_cast<uint32_t>(std::floor(std::log2(double(std::max(Width, Height))))) + 1u;
    Extents.clear();
    Extents.reserve(Levels);
    for (uint32_t Level = 0; Level < Levels; ++Level)
        Extents.push_back({ std::max<uint32_t>(1u, Width >> Level), std::max<uint32_t>(1u, Height >> Level) });
}

std::array<uint32_t, 2> NearestDepthSpace::LevelExtent(uint32_t Level) const noexcept
{
    if (Level >= Extents.size())
        return { 0u, 0u };
    return Extents[Level];
}

std::array<uint32_t, 3> NearestDepthSpace::TransferDispatch() const noexcept
{
    return { (Width + 7u) / 8u, (Height + 7u) / 8u, 1u };
}

std::array<uint32_t, 3> NearestDepthSpace::ReductionDispatch(uint32_t Level) const noexcept
{
    const std::array<uint32_t, 2> Extent = LevelExtent(Level);
    return { (Extent[0] + 7u) / 8u, (Extent[1] + 7u) / 8u, 1u };
}

std::vector<std::vector<float>> NearestDepthSpace::Reduce(const float* ReversedDepth, uint32_t ScreenWidth, uint32_t ScreenHeight)
{
    std::vector<std::vector<float>> Pyramid;
    if (!ReversedDepth || ScreenWidth == 0u || ScreenHeight == 0u)
        return Pyramid;

    NearestDepthSpace Shape;
    Shape.Configure(ScreenWidth, ScreenHeight);
    Pyramid.reserve(Shape.Levels);
    Pyramid.push_back(std::vector<float>(ReversedDepth, ReversedDepth + size_t(ScreenWidth) * ScreenHeight));

    for (uint32_t Level = 1; Level < Shape.Levels; ++Level)
    {
        const std::array<uint32_t, 2> SourceExtent = Shape.LevelExtent(Level - 1u);
        const std::array<uint32_t, 2> TargetExtent = Shape.LevelExtent(Level);
        const std::vector<float>& Source = Pyramid.back();

        std::vector<float> Target(size_t(TargetExtent[0]) * TargetExtent[1], 0.0f);
        for (uint32_t Y = 0; Y < TargetExtent[1]; ++Y)
        {
            for (uint32_t X = 0; X < TargetExtent[0]; ++X)
            {
                // 📝 Where the source extent is odd its last row or column has no partner, so the final
                //    target texel takes three source texels on that axis instead of two — no pixel is
                //    ever left unbounded.
                const bool LastColumn = (SourceExtent[0] & 1u) != 0u && X + 1u == TargetExtent[0];
                const bool LastRow = (SourceExtent[1] & 1u) != 0u && Y + 1u == TargetExtent[1];
                const uint32_t SpanX = 2u + (LastColumn ? 1u : 0u);
                const uint32_t SpanY = 2u + (LastRow ? 1u : 0u);

                float Nearest = 0.0f;
                for (uint32_t Row = 0; Row < SpanY; ++Row)
                    for (uint32_t Column = 0; Column < SpanX; ++Column)
                    {
                        const uint32_t SourceX = std::min<uint32_t>(X * 2u + Column, SourceExtent[0] - 1u);
                        const uint32_t SourceY = std::min<uint32_t>(Y * 2u + Row, SourceExtent[1] - 1u);
                        Nearest = std::max(Nearest, Source[size_t(SourceY) * SourceExtent[0] + SourceX]);
                    }
                Target[size_t(Y) * TargetExtent[0] + X] = Nearest;
            }
        }
        Pyramid.push_back(std::move(Target));
    }
    return Pyramid;
}

bool NearestDepthSpace::VerifyBounds(const std::vector<std::vector<float>>& Pyramid, uint32_t ScreenWidth, uint32_t ScreenHeight)
{
    if (Pyramid.empty() || ScreenWidth == 0u || ScreenHeight == 0u)
        return false;
    if (Pyramid[0].size() != size_t(ScreenWidth) * ScreenHeight)
        return false;

    NearestDepthSpace Shape;
    Shape.Configure(ScreenWidth, ScreenHeight);
    if (Pyramid.size() != Shape.Levels)
        return false;

    for (uint32_t Level = 1; Level < Shape.Levels; ++Level)
    {
        const std::array<uint32_t, 2> Extent = Shape.LevelExtent(Level);
        if (Pyramid[Level].size() != size_t(Extent[0]) * Extent[1])
            return false;

        // 📝 Every pixel of level 0 maps to exactly one tile of this level, and that tile's texel must
        //    be ≥ the pixel's depth: the bound the screen walk crosses tiles on.
        for (uint32_t Y = 0; Y < ScreenHeight; ++Y)
            for (uint32_t X = 0; X < ScreenWidth; ++X)
            {
                const uint32_t TileX = std::min<uint32_t>(X >> Level, Extent[0] - 1u);
                const uint32_t TileY = std::min<uint32_t>(Y >> Level, Extent[1] - 1u);
                const float Pixel = Pyramid[0][size_t(Y) * ScreenWidth + X];
                const float Bound = Pyramid[Level][size_t(TileY) * Extent[0] + TileX];
                if (!(Bound >= Pixel))
                    return false;
            }
    }
    return true;
}

} // namespace Frontier
