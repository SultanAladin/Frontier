/*============================================================================================================================================
                                                      PATTERNTHUMBNAILATLASVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Prove the gallery card atlas bakes faithfully: CPU raster coverage and blending, 24-slot page layout with
// replicated gutters, and revision-gated page pixels from the headless panel.

#include "UI/TextureStudio/PatternThumbnailRaster.h"
#include "UI/TextureStudio/PatternWorkspacePanel.h"
#include <cmath>
#include <iostream>

using namespace Frontier;

namespace
{
uint32_t Assertions = 0;

void Confirm(bool Condition, const char *Caption)
{
    if (!Condition) throw std::runtime_error(Caption);
    ++Assertions;
}

std::array<uint8_t, 4> Texel(const std::vector<uint8_t> &Pixels, uint32_t Extent, uint32_t X, uint32_t Y)
{
    const size_t Offset = (size_t(Y) * Extent + X) * 4;
    return {Pixels[Offset], Pixels[Offset + 1], Pixels[Offset + 2], Pixels[Offset + 3]};
}

bool Near(uint8_t Actual, int Expected) { return std::abs(int(Actual) - Expected) <= 1; }

constexpr uint32_t PackAbgr(uint8_t Red, uint8_t Green, uint8_t Blue, uint8_t Alpha)
{
    return uint32_t(Alpha) << 24 | uint32_t(Blue) << 16 | uint32_t(Green) << 8 | uint32_t(Red);
}
}

int main()
{
    try
    {
        // Solid-triangle fill at 1:1 tile scale, including ABGR to RGBA channel order.
        PatternWorkspacePanel::PatternThumbnailShape Red;
        Red.Triangles = {{{10, 10}, {100, 10}, {10, 100}}};
        Red.FillColour = PackAbgr(10, 20, 30, 255);
        Red.Filled = true;
        const std::vector<uint8_t> Tile = RasterizePatternThumbnail({Red}, 512, PackAbgr(0, 0, 0, 255));
        Confirm(Tile.size() == size_t(512) * 512 * 4, "Thumbnail payload size changed.");
        const auto Inside = Texel(Tile, 512, 15, 15);
        Confirm(Inside[0] == 10 && Inside[1] == 20 && Inside[2] == 30 && Inside[3] == 255, "Triangle fill channel order changed.");
        const auto Outside = Texel(Tile, 512, 90, 90);
        Confirm(Outside[0] == 0 && Outside[1] == 0 && Outside[2] == 0 && Outside[3] == 255, "Triangle fill escaped its edges.");
        // Reversed winding must paint the identical coverage: tessellated card loops arrive in either order.
        PatternWorkspacePanel::PatternThumbnailShape Reversed = Red;
        Reversed.Triangles = {{{10, 10}, {10, 100}, {100, 10}}};
        Confirm(RasterizePatternThumbnail({Reversed}, 512, PackAbgr(0, 0, 0, 255)) == Tile, "Triangle fill depends on winding order.");
        // Source-over blending against an opaque backdrop.
        PatternWorkspacePanel::PatternThumbnailShape Half = Red;
        Half.FillColour = PackAbgr(200, 0, 0, 128);
        const std::vector<uint8_t> Backdrop = RasterizePatternThumbnail({Half}, 512, PackAbgr(100, 100, 100, 255));
        const auto Blend = Texel(Backdrop, 512, 15, 15);
        Confirm(Near(Blend[0], 150) && Near(Blend[1], 50) && Near(Blend[2], 50) && Blend[3] == 255,
            "Thumbnail blend is not source-over.");
        const auto Kept = Texel(Backdrop, 512, 400, 400);
        Confirm(Kept[0] == 100 && Kept[1] == 100 && Kept[2] == 100 && Kept[3] == 255, "Blend touched pixels outside the fill.");
        bool Rejected = false;
        try { (void)RasterizePatternThumbnail({Red}, 0, PackAbgr(0, 0, 0, 255)); }
        catch (const std::exception &) { Rejected = true; }
        Confirm(Rejected, "Thumbnail raster accepted a zero extent.");
        // Polyline stroke band: interior rows paint, rows past half-width stay clean.
        PatternWorkspacePanel::PatternThumbnailShape Stroke;
        Stroke.Outline = {{{10, 200}, {100, 200}}};
        Stroke.LineColour = PackAbgr(255, 255, 255, 255);
        Stroke.Stroked = true;
        Stroke.StrokeWidth = 8;
        const std::vector<uint8_t> Lines = RasterizePatternThumbnail({Stroke}, 512, PackAbgr(0, 0, 0, 255));
        Confirm(Texel(Lines, 512, 50, 200)[3] == 255, "Stroke missed its centerline.");
        Confirm(Texel(Lines, 512, 50, 210)[0] == 0 && Texel(Lines, 512, 50, 210)[3] == 255, "Stroke painted past its half width.");
        PatternWorkspacePanel::PatternThumbnailShape Degenerate;
        Degenerate.Outline = {{{300, 300}}};
        Degenerate.LineColour = Stroke.LineColour;
        Degenerate.Stroked = true;
        Degenerate.ClosedOutline = true;
        Degenerate.StrokeWidth = 8;
        const std::vector<uint8_t> Untouched = RasterizePatternThumbnail({Degenerate}, 64, PackAbgr(7, 7, 7, 255));
        bool Painted = false;
        for (size_t Pixel = 0; Pixel < Untouched.size(); Pixel += 4)
            Painted = Painted || Untouched[Pixel] != 7 || Untouched[Pixel + 3] != 255;
        Confirm(!Painted, "Degenerate outline painted.");
        // Page blit: slot offsets, replicated gutters, baked background for empty slots.
        constexpr uint32_t CardWell = PackAbgr(20, 23, 24, 255);
        const std::vector<uint8_t> Blank = RasterizePatternThumbnail({}, 256, CardWell);
        std::vector<uint8_t> Slot0 = Blank, Slot23 = Blank;
        Slot0[0] = 1; Slot0[1] = 2; Slot0[2] = 3; Slot0[3] = 255;
        const size_t Far = (size_t(255) * 256 + 255) * 4;
        Slot0[Far] = 4; Slot0[Far + 1] = 5; Slot0[Far + 2] = 6; Slot0[Far + 3] = 255;
        Slot23[0] = 7; Slot23[1] = 8; Slot23[2] = 9; Slot23[3] = 255;
        std::vector<std::vector<uint8_t>> Tiles(24, Blank);
        Tiles[0] = Slot0;
        Tiles[23] = Slot23;
        const std::vector<uint8_t> Atlas = BlitPatternThumbnailAtlas(Tiles, 256, 6, CardWell);
        Confirm(Atlas.size() == size_t(1548) * 1548 * 4, "Atlas payload size changed.");
        const auto Slot0Near = Texel(Atlas, 1548, 1, 1);
        Confirm(Slot0Near[0] == 1 && Slot0Near[1] == 2 && Slot0Near[2] == 3 && Slot0Near[3] == 255, "Atlas slot 0 offset changed.");
        const auto Slot0Far = Texel(Atlas, 1548, 256, 256);
        Confirm(Slot0Far[0] == 4 && Slot0Far[1] == 5 && Slot0Far[2] == 6 && Slot0Far[3] == 255, "Atlas slot 0 far corner changed.");
        const auto Gutter = Texel(Atlas, 1548, 0, 1);
        Confirm(Gutter == Slot0Near, "Atlas gutter edge is not replicated.");
        const auto Corner = Texel(Atlas, 1548, 0, 0);
        Confirm(Corner == Slot0Near, "Atlas gutter corner is not replicated.");
        const auto Last = Texel(Atlas, 1548, 5 * 258 + 1, 3 * 258 + 1);
        Confirm(Last[0] == 7 && Last[1] == 8 && Last[2] == 9 && Last[3] == 255, "Atlas slot 23 offset changed.");
        const auto Empty = Texel(Atlas, 1548, 258 + 11, 11);
        Confirm(Empty[0] == 20 && Empty[1] == 23 && Empty[2] == 24 && Empty[3] == 255, "Atlas empty slot lost its baked background.");
        Rejected = false;
        try
        {
            std::vector<std::vector<uint8_t>> Ragged = Tiles;
            Ragged[5].resize(100);
            (void)BlitPatternThumbnailAtlas(Ragged, 256, 6, CardWell);
        }
        catch (const std::exception &) { Rejected = true; }
        Confirm(Rejected, "Atlas blit accepted a ragged thumb.");
        Rejected = false;
        try { (void)BlitPatternThumbnailAtlas(std::vector<std::vector<uint8_t>>(37, Blank), 256, 6, CardWell); }
        catch (const std::exception &) { Rejected = true; }
        Confirm(Rejected, "Atlas blit exceeded its grid capacity.");
        // Headless panel: one library-wide atlas baked once; filters and page flips only re-point
        // card UVs, so the revision (and GPU upload) never moves after the first bake.
        PatternWorkspacePanel Workspace;
        Confirm(Workspace.ConfigurePresetDirectory("EngineContent/PatternArchives/TextureStudio"), "Atlas panel rejected the pattern library.");
        Confirm(PatternWorkspacePanel::QueryPatternAtlasExtent() == 1560, "Atlas extent is not the settled 1560.");
        const uint64_t First = Workspace.QueryPatternAtlasRevision();
        Confirm(First > 0, "Atlas revision never advanced past zero.");
        const auto &Pixels = Workspace.QueryPatternAtlasPixels();
        Confirm(Pixels.size() == size_t(1560) * 1560 * 4, "Atlas page payload size changed.");
        Confirm(Workspace.QueryPatternAtlasRevision() == First, "Atlas revision moved without new bakes.");
        bool Artwork = false;
        for (uint32_t Y = 1; Y <= 128 && !Artwork; ++Y)
            for (uint32_t X = 1; X <= 128; ++X)
            {
                const auto Card = Texel(Pixels, 1560, X, Y);
                if (Card[0] != 20 || Card[1] != 23 || Card[2] != 24 || Card[3] != 255) { Artwork = true; break; }
            }
        Confirm(Artwork, "Atlas lead tile holds no baked artwork.");
        Confirm(Texel(Pixels, 1560, 0, 1) == Texel(Pixels, 1560, 1, 1), "Atlas page gutter is not replicated.");
        size_t ArtworkTiles = 0;
        for (size_t Slot = 0; Slot < 144; ++Slot)
        {
            const auto Card = Texel(Pixels, 1560, uint32_t((Slot % 12) * 130 + 65), uint32_t((Slot / 12) * 130 + 65));
            if (Card[0] != 20 || Card[1] != 23 || Card[2] != 24 || Card[3] != 255) ++ArtworkTiles;
        }
        Confirm(ArtworkTiles > 24, "Atlas holds no more than one page of baked artwork.");
        Workspace.ConfigureLibraryFilter("Geometric constructions", "", true);
        Confirm(Workspace.QueryPatternAtlasRevision() == First, "Filter change rebaked the settled atlas.");
        for (size_t Page = 0; Page < 5; ++Page)
        {
            Workspace.ConfigureLibraryPage(Page);
            Confirm(Workspace.QueryPatternAtlasRevision() == First, "Page flip rebaked the settled atlas.");
        }
        Workspace.ConfigureLibraryPage(0);
        Workspace.ConfigureLibraryFilter("All patterns", "", true);
        Confirm(Workspace.QueryPatternAtlasRevision() == First, "Filter reset rebaked the settled atlas.");
        std::cout << "{\"result\":\"passed\",\"checks\":" << Assertions << "}\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cout << "{\"result\":\"failed\",\"error\":\"" << Failure.what() << "\"}\n";
        return 1;
    }
}
