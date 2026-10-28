/*============================================================================================================================================
                                                        STAMPFIDELITYVALIDATION.CPP
============================================================================================================================================*/
// 🖨️ A placed decal must look like the stamp it came from: this checks the whole fidelity chain
//    (authored colours -> .stamp.toml round-trip -> ThorVG stamp raster) keeps red red and blue blue,
//    at more than one extent. Regression cover for "decals stamp as a solid black silhouette".

#include "Engine/ContentInterchange/VectorAuthoringDocument.h"
#include "UI/TextureStudio/PatternThumbnailRaster.h"
#include <cstdio>
#include <stdexcept>

namespace
{
using namespace Frontier;
uint32_t Checks = 0;
void Confirm(bool Condition, const char *Caption)
{
    if (!Condition) throw std::runtime_error(Caption);
    ++Checks;
}

VectorAuthoringDocument ConstructColouredStamp()
{
    VectorAuthoringDocument Document;
    Document.Stamp = true;
    Document.Name = "Fidelity probe";
    Document.TokenClock = 8;
    VectorCoverageInstruction Square, Ellipse, Path;
    Square.Bounds = {150, 150, 160, 160};
    Square.Colour = {1, 0, 0, 1}; // red rect, top-left
    Ellipse.Topology[0] = 1;
    Ellipse.Bounds = {370, 150, 160, 160};
    Ellipse.Colour = {0, .75f, 0, 1}; // green ellipse, top-right
    Path.Topology[0] = 6;            // cubic path (pen shape), bottom centre
    Path.Bounds = {256, 380, 200, 180};
    Path.Colour = {0, 0, 1, 1}; // blue
    Path.Topology[1] = 0;       // segment start
    Path.Topology[2] = 3;       // segment count
    Document.Instructions = {Square, Ellipse, Path};
    // Triangle in unit-cage space (scaled by Bounds.zw): top -> bottom-right -> bottom-left -> close.
    const auto Leg = [](float Ax, float Ay, float Dx, float Dy) {
        VectorCubicSegment Segment;
        Segment.StartControl = {Ax, Ay, Ax + (Dx - Ax) / 3, Ay + (Dy - Ay) / 3};
        Segment.ControlEnd = {Ax + (Dx - Ax) * 2 / 3, Ay + (Dy - Ay) * 2 / 3, Dx, Dy};
        return Segment;
    };
    Document.Segments = {Leg(0, -.45f, .45f, .4f), Leg(.45f, .4f, -.45f, .4f), Leg(-.45f, .4f, 0, -.45f)};
    VectorAuthoringLayer Red, Green, Blue;
    Red.Token = 1; Red.Name = "Red"; Red.Root = 0;
    Green.Token = 2; Green.Name = "Green"; Green.Root = 1;
    Blue.Token = 3; Blue.Name = "Blue"; Blue.Root = 2;
    Document.Layers = {Red, Green, Blue};
    return Document;
}

struct Histogram
{
    size_t Opaque = 0, Red = 0, Green = 0, Blue = 0, Black = 0, White = 0;
};
Histogram Analyse(const std::vector<uint8_t> &Pixels)
{
    Histogram Result;
    for (size_t At = 0; At + 3 < Pixels.size(); At += 4)
    {
        const uint8_t R = Pixels[At], G = Pixels[At + 1], B = Pixels[At + 2], A = Pixels[At + 3];
        if (A < 200) continue;
        ++Result.Opaque;
        if (R > 180 && G < 90 && B < 90) ++Result.Red;
        else if (G > 140 && R < 90 && B < 90) ++Result.Green;
        else if (B > 180 && R < 90 && G < 90) ++Result.Blue;
        else if (R < 40 && G < 40 && B < 40) ++Result.Black;
        else if (R > 215 && G > 215 && B > 215) ++Result.White;
    }
    return Result;
}
void Report(const char *Label, const Histogram &H)
{
    std::printf("%s: opaque=%zu red=%zu green=%zu blue=%zu black=%zu white=%zu\n",
        Label, H.Opaque, H.Red, H.Green, H.Blue, H.Black, H.White);
}
} // namespace

int main()
{
    try
    {
        const auto Document = ConstructColouredStamp();
        const auto Direct = Analyse(RasterizeStampDocument(Document, 256));
        Report("direct256", Direct);
        Confirm(Direct.Red > 400, "Red rect missing from the direct stamp raster.");
        Confirm(Direct.Green > 300, "Green ellipse missing from the direct stamp raster.");
        Confirm(Direct.Blue > 300, "Blue path missing from the direct stamp raster.");

        const auto Decoded = DecodeVectorAuthoring(EncodeVectorAuthoring(Document));
        const auto RoundTrip = Analyse(RasterizeStampDocument(Decoded, 256));
        Report("roundtrip256", RoundTrip);
        Confirm(RoundTrip.Red > 400, "Red rect lost through the .stamp.toml round-trip (solid-colour decal bug).");
        Confirm(RoundTrip.Green > 300, "Green ellipse lost through the .stamp.toml round-trip.");
        Confirm(RoundTrip.Blue > 300, "Blue path lost through the .stamp.toml round-trip.");
        Confirm(RoundTrip.Black < RoundTrip.Opaque / 4, "Round-tripped stamp collapsed toward black.");

        const auto Large = Analyse(RasterizeStampDocument(Decoded, 1024));
        Report("roundtrip1024", Large);
        Confirm(Large.Red > 6000, "Red rect missing at 1024px (Texture Paint footprint extents).");
        std::printf("StampFidelityValidation: %u checks passed\n", Checks);
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::printf("StampFidelityValidation FAILED: %s\n", Failure.what());
        return 1;
    }
}
