#include "Engine/ContentInterchange/VectorAuthoringDocument.h"
#include "Engine/Render/VectorSurfaceCompilation.h"
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace
{
void Require(bool Condition, const char *Message)
{
    if (!Condition) throw std::runtime_error(Message);
}
}

int main(int ArgumentCount, char **Arguments)
{
    try
    {
        if (ArgumentCount != 2) throw std::runtime_error("Usage: VectorAuthoringImportValidation <native.vector.toml>");
        std::ifstream Input(Arguments[1], std::ios::binary);
        if (!Input) throw std::runtime_error("Cannot open generated native Pattern TOML.");
        const std::string Source(std::istreambuf_iterator<char>(Input), {});
        Require(!Source.empty() && Source.size() <= 600000, "Generated native Pattern TOML is empty or exceeds its source bound.");
        const auto Document = Frontier::DecodeVectorAuthoring(Source);
        Require(Document.Name == "Native importer acceptance fixture", "Pattern name did not round-trip.");
        Require(Document.Layers.size() == 3, "Pattern layer count did not round-trip.");
        Require(Document.Layers[0].Mask.has_value() && Document.Layers[0].MaskEnabled,
            "Supported native shape mask was not imported.");
        Require(std::abs(Document.Instructions[0].Colour[0] - 85.f / 255.f) < .00001f,
            "Pattern sRGB paint was not retained for the Float32 channel conversion.");
        Require(Document.Layers[1].SurfaceStructure == Frontier::VectorAuthoringSurfaceStructure::Stitch &&
            Document.Layers[1].TextileGeometry == Frontier::VectorAuthoringTextileGeometry::StitchThread,
            "Golden thread identity was not retained independently of colour.");
        const auto HeightIndex = size_t(Frontier::VectorAuthoringSurfaceChannel::Height);
        const auto NormalIndex = size_t(Frontier::VectorAuthoringSurfaceChannel::Normal);
        const auto RoughnessIndex = size_t(Frontier::VectorAuthoringSurfaceChannel::Roughness);
        const auto MetalnessIndex = size_t(Frontier::VectorAuthoringSurfaceChannel::Metalness);
        Require(Document.Layers[1].SurfaceChannels[HeightIndex].Enabled &&
            Document.Layers[1].SurfaceChannels[HeightIndex].Strength == .75f &&
            Document.Layers[1].SurfaceChannels[NormalIndex].Enabled &&
            Document.Layers[1].SurfaceChannels[NormalIndex].Strength == 1.25f,
            "Textile Height/Normal contribution metadata did not round-trip.");
        Require(Document.Layers[1].SurfaceChannels[RoughnessIndex].Strength == .24f &&
            Document.Layers[1].SurfaceChannels[MetalnessIndex].Strength == .92f,
            "Per-shape dielectric/golden-thread scalar material values did not round-trip.");
        (void)Frontier::ConstructVectorAuthoringCoverage(Document);
        constexpr std::array Channels{
            Frontier::VectorAuthoringSurfaceChannel::Colour,
            Frontier::VectorAuthoringSurfaceChannel::Height,
            Frontier::VectorAuthoringSurfaceChannel::Normal,
            Frontier::VectorAuthoringSurfaceChannel::Roughness,
            Frontier::VectorAuthoringSurfaceChannel::Metalness,
            Frontier::VectorAuthoringSurfaceChannel::Opacity,
            Frontier::VectorAuthoringSurfaceChannel::Occlusion};
        for (const auto Channel : Channels) (void)Frontier::CompileVectorSurface(Document, Channel);
        std::cout << "Native Pattern import passed: " << Document.Layers.size() << " layers, "
            << Document.Instructions.size() << " instructions, " << Document.Segments.size()
            << " cubic segments, seven compiled Float32 surface channels.\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "Native Pattern import validation failed: " << Failure.what() << '\n';
        return 1;
    }
}
