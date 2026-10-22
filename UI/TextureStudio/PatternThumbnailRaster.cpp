/*============================================================================================================================================
                                                         PATTERNTHUMBNAILRASTER.CPP
============================================================================================================================================*/
// 🧩 Scanline triangle fills and distance-field polylines for gallery atlas thumbnails; matches vector card appearance.

#include "UI/TextureStudio/PatternThumbnailRaster.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Frontier
{
namespace
{
void BlendPixel(uint8_t *Pixel, uint32_t Abgr)
{
    const uint32_t Alpha = Abgr >> 24;
    if (!Alpha) return;
    const uint32_t Source[3]{Abgr & 255, (Abgr >> 8) & 255, (Abgr >> 16) & 255};
    if (Alpha == 255)
    {
        Pixel[0] = uint8_t(Source[0]);
        Pixel[1] = uint8_t(Source[1]);
        Pixel[2] = uint8_t(Source[2]);
        Pixel[3] = 255;
        return;
    }
    for (size_t Channel = 0; Channel < 3; ++Channel)
        Pixel[Channel] = uint8_t((Source[Channel] * Alpha + Pixel[Channel] * (255 - Alpha) + 127) / 255);
}

void FillTriangle(std::vector<uint8_t> &Pixels, uint32_t Extent, float Ax, float Ay, float Bx, float By, float Cx, float Cy,
    uint32_t Colour)
{
    const float Area = (Bx - Ax) * (Cy - Ay) - (By - Ay) * (Cx - Ax);
    if (!std::isfinite(Area) || Area == 0) return;
    const int MinimumX = std::max(0, int(std::floor(std::min({Ax, Bx, Cx}))));
    const int MaximumX = std::min(int(Extent), int(std::ceil(std::max({Ax, Bx, Cx}))));
    const int MinimumY = std::max(0, int(std::floor(std::min({Ay, By, Cy}))));
    const int MaximumY = std::min(int(Extent), int(std::ceil(std::max({Ay, By, Cy}))));
    for (int Y = MinimumY; Y < MaximumY; ++Y)
        for (int X = MinimumX; X < MaximumX; ++X)
        {
            const float Px = float(X) + .5f, Py = float(Y) + .5f;
            float First = (Bx - Ax) * (Py - Ay) - (By - Ay) * (Px - Ax);
            float Second = (Cx - Bx) * (Py - By) - (Cy - By) * (Px - Bx);
            float Third = (Ax - Cx) * (Py - Cy) - (Ay - Cy) * (Px - Cx);
            // 📝 Normalized winding claims shared edges, so dense tessellations never show pinhole seams.
            if (Area < 0) { First = -First; Second = -Second; Third = -Third; }
            if (First >= 0 && Second >= 0 && Third >= 0)
                BlendPixel(&Pixels[(size_t(Y) * Extent + X) * 4], Colour);
        }
}

void StrokeSegment(std::vector<uint8_t> &Pixels, uint32_t Extent, float Ax, float Ay, float Bx, float By, float Radius,
    uint32_t Colour)
{
    const int MinimumX = std::max(0, int(std::floor(std::min(Ax, Bx) - Radius - 1)));
    const int MaximumX = std::min(int(Extent), int(std::ceil(std::max(Ax, Bx) + Radius + 1)));
    const int MinimumY = std::max(0, int(std::floor(std::min(Ay, By) - Radius - 1)));
    const int MaximumY = std::min(int(Extent), int(std::ceil(std::max(Ay, By) + Radius + 1)));
    const float Dx = Bx - Ax, Dy = By - Ay;
    const float Length2 = std::max(Dx * Dx + Dy * Dy, 1e-12f);
    for (int Y = MinimumY; Y < MaximumY; ++Y)
        for (int X = MinimumX; X < MaximumX; ++X)
        {
            const float Px = float(X) + .5f - Ax, Py = float(Y) + .5f - Ay;
            const float Along = std::clamp((Px * Dx + Py * Dy) / Length2, 0.f, 1.f);
            const float OffX = Px - Dx * Along, OffY = Py - Dy * Along;
            if (OffX * OffX + OffY * OffY <= Radius * Radius)
                BlendPixel(&Pixels[(size_t(Y) * Extent + X) * 4], Colour);
        }
}
} // namespace

std::vector<uint8_t> RasterizePatternThumbnail(
    const std::vector<PatternWorkspacePanel::PatternThumbnailShape> &Shapes, uint32_t Extent, uint32_t BackgroundAbgr)
{
    if (!Extent || Extent > 512) throw std::runtime_error("Thumbnail raster extent is unbounded.");
    std::vector<uint8_t> Pixels(size_t(Extent) * Extent * 4);
    for (size_t Pixel = 0; Pixel < Pixels.size(); Pixel += 4)
    {
        Pixels[Pixel] = uint8_t(BackgroundAbgr & 255);
        Pixels[Pixel + 1] = uint8_t((BackgroundAbgr >> 8) & 255);
        Pixels[Pixel + 2] = uint8_t((BackgroundAbgr >> 16) & 255);
        Pixels[Pixel + 3] = 255;
    }
    const float Scale = float(Extent) / 512; // Tile space 0..512.
    for (const auto &Shape : Shapes)
    {
        if (Shape.Filled && Shape.Triangles.size() >= 3)
            for (size_t First = 0; First + 2 < Shape.Triangles.size(); First += 3)
                FillTriangle(Pixels, Extent, Shape.Triangles[First][0] * Scale, Shape.Triangles[First][1] * Scale,
                    Shape.Triangles[First + 1][0] * Scale, Shape.Triangles[First + 1][1] * Scale,
                    Shape.Triangles[First + 2][0] * Scale, Shape.Triangles[First + 2][1] * Scale, Shape.FillColour);
        if (Shape.Stroked && Shape.Outline.size() > 1)
        {
            const float Radius = std::max(Shape.StrokeWidth * Scale, 1.f) * .5f;
            const size_t Spans = Shape.Outline.size() - 1 + (Shape.ClosedOutline ? 1 : 0);
            for (size_t Span = 0; Span < Spans; ++Span)
            {
                const auto &Start = Shape.Outline[Span % Shape.Outline.size()];
                const auto &End = Shape.Outline[(Span + 1) % Shape.Outline.size()];
                StrokeSegment(Pixels, Extent, Start[0] * Scale, Start[1] * Scale, End[0] * Scale, End[1] * Scale,
                    Radius, Shape.LineColour);
            }
        }
    }
    return Pixels;
}

std::vector<uint8_t> BlitPatternThumbnailAtlas(
    const std::vector<std::vector<uint8_t>> &Thumbs, uint32_t TileExtent, uint32_t Columns, uint32_t BackgroundAbgr)
{
    if (!TileExtent || TileExtent > 512 || !Columns || Columns > 16 || Thumbs.size() > size_t(Columns) * Columns)
        throw std::runtime_error("Atlas blit exceeds bounded grid capacity.");
    for (const auto &Thumb : Thumbs)
        if (Thumb.size() != size_t(TileExtent) * TileExtent * 4)
            throw std::runtime_error("Atlas thumb does not match the tile extent.");
    const uint32_t Stride = TileExtent + 2, AtlasExtent = Stride * Columns;
    std::vector<uint8_t> Atlas(size_t(AtlasExtent) * AtlasExtent * 4);
    for (size_t Pixel = 0; Pixel < Atlas.size(); Pixel += 4)
    {
        Atlas[Pixel] = uint8_t(BackgroundAbgr & 255);
        Atlas[Pixel + 1] = uint8_t((BackgroundAbgr >> 8) & 255);
        Atlas[Pixel + 2] = uint8_t((BackgroundAbgr >> 16) & 255);
        Atlas[Pixel + 3] = 255;
    }
    for (size_t Slot = 0; Slot < Thumbs.size(); ++Slot)
    {
        const auto &Thumb = Thumbs[Slot];
        const uint32_t Left = uint32_t(Slot % Columns) * Stride + 1, Top = uint32_t(Slot / Columns) * Stride + 1;
        for (uint32_t Row = 0; Row < TileExtent; ++Row)
        {
            const uint8_t *Source = &Thumb[size_t(Row) * TileExtent * 4];
            uint8_t *Target = &Atlas[(size_t(Top + Row) * AtlasExtent + Left) * 4];
            std::copy_n(Source, size_t(TileExtent) * 4, Target);
            // 📝 Replicated gutters keep bilinear sampling inside the tile, mirroring the GPU atlas copies.
            std::copy_n(Source, 4, Target - 4);
            std::copy_n(Source + (size_t(TileExtent) - 1) * 4, 4, Target + size_t(TileExtent) * 4);
        }
        const uint8_t *FirstRow = &Atlas[(size_t(Top) * AtlasExtent + Left - 1) * 4];
        uint8_t *TopGutter = &Atlas[(size_t(Top - 1) * AtlasExtent + Left - 1) * 4];
        std::copy_n(FirstRow, size_t(TileExtent + 2) * 4, TopGutter);
        const uint8_t *LastRow = &Atlas[(size_t(Top + TileExtent - 1) * AtlasExtent + Left - 1) * 4];
        uint8_t *BottomGutter = &Atlas[(size_t(Top + TileExtent) * AtlasExtent + Left - 1) * 4];
        std::copy_n(LastRow, size_t(TileExtent + 2) * 4, BottomGutter);
    }
    return Atlas;
}
} // namespace Frontier
