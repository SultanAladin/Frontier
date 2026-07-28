/*==============================================================================================================================================
                                                                SVGRASTERIZER.CPP
==============================================================================================================================================*/
// 🧩 Drives the thorvg software raster engine: reference-counted engine bring-up, then per-icon load-scale-render into a caller-owned RGBA8
//    block. Every rasterization allocates its own picture and software canvas, targets the output buffer, adds the scaled picture, and syncs —
//    so calls are independent and hold no shared canvas state. Straight-alpha ABGR8888S output maps 1:1 onto VK_FORMAT_R8G8B8A8_UNORM upstream.
//    Disposal follows thorvg's ownership rules: a Picture is a Paint with a protected destructor, released by tvg::Paint::rel unless it has been
//    added to a canvas (which then owns it); the SwCanvas has a public destructor and is held in a unique_ptr that frees it on every exit path.

#include "EngineContext/Interface/Icons/SvgRasterizer.h"

#include <memory>

#include "thorvg.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          INTERNAL STORAGE
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 Reference count for the process-wide thorvg engine. thorvg's Initializer::init / term are themselves reference-counted,
//    but keeping our own count lets InitializeSvgEngine report the first-failure cleanly and pair exactly with Finalize.
int SvgEngineReferenceCount = 0;

// [px] - upper bound on a rasterized icon edge. Outliner glyphs are tiny; this only guards against a nonsense request
//        allocating gigabytes. A 512 px tile at RGBA8 is 1 MiB — ample headroom over any real row height.
constexpr uint32_t MaximumIconEdgePixels = 512u;

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool InitializeSvgEngine()
{
    if (SvgEngineReferenceCount > 0)
    {
        ++SvgEngineReferenceCount;
        return true;
    }

    // Threads == 0 asks thorvg to select a sensible worker count for the software engine.
    if (tvg::Initializer::init(0u) != tvg::Result::Success)
    {
        return false;
    }

    SvgEngineReferenceCount = 1;
    return true;
}

void FinalizeSvgEngine() noexcept
{
    if (SvgEngineReferenceCount <= 0)
    {
        return;
    }

    --SvgEngineReferenceCount;
    if (SvgEngineReferenceCount == 0)
    {
        tvg::Initializer::term();
    }
}

SvgRasterOutput RasterizeSvg(const char* SvgByteSource, uint32_t SvgByteCount, uint32_t EdgePixels)
{
    SvgRasterOutput Output;

    if (SvgEngineReferenceCount <= 0 || SvgByteSource == nullptr || SvgByteCount == 0u || EdgePixels == 0u)
    {
        return Output;
    }

    const uint32_t ResolvedEdge = EdgePixels > MaximumIconEdgePixels ? MaximumIconEdgePixels : EdgePixels;

    // -- load the SVG source into a picture (copy == true so thorvg owns its own copy of the bytes) --
    tvg::Picture* IconPicture = tvg::Picture::gen();
    if (IconPicture == nullptr)
    {
        return Output;
    }

    if (IconPicture->load(SvgByteSource, SvgByteCount, "svg", nullptr, true) != tvg::Result::Success)
    {
        tvg::Paint::rel(IconPicture);
        return Output;
    }

    // -- scale the picture to fill the requested square tile regardless of its intrinsic viewBox --
    IconPicture->size(static_cast<float>(ResolvedEdge), static_cast<float>(ResolvedEdge));

    // -- allocate the destination block and target a fresh software canvas at it --
    Output.Pixels.assign(static_cast<std::size_t>(ResolvedEdge) * static_cast<std::size_t>(ResolvedEdge), 0u);

    // The canvas has a public destructor (unlike Paint), so a unique_ptr frees it on every path below.
    std::unique_ptr<tvg::SwCanvas> RasterCanvas(tvg::SwCanvas::gen());
    if (RasterCanvas == nullptr)
    {
        tvg::Paint::rel(IconPicture);
        Output.Pixels.clear();
        return Output;
    }

    const uint32_t StrideTexels = ResolvedEdge;
    if (RasterCanvas->target(Output.Pixels.data(), StrideTexels, ResolvedEdge, ResolvedEdge,
                             tvg::ColorSpace::ABGR8888S) != tvg::Result::Success)
    {
        tvg::Paint::rel(IconPicture);
        Output.Pixels.clear();
        return Output;
    }

    // add transfers ownership of the picture to the canvas only on success; a failed add leaves the picture ours to release.
    if (RasterCanvas->add(IconPicture) != tvg::Result::Success)
    {
        tvg::Paint::rel(IconPicture);
        Output.Pixels.clear();
        return Output;
    }

    // From here the canvas owns the picture and releases it; a draw/sync failure leaves nothing for us to free.
    if (RasterCanvas->draw(true) != tvg::Result::Success ||
        RasterCanvas->sync() != tvg::Result::Success)
    {
        Output.Pixels.clear();
        return Output;
    }

    Output.PixelWidth    = ResolvedEdge;
    Output.PixelHeight   = ResolvedEdge;
    Output.RasterSuccess = true;
    return Output;
}

} // namespace Frontier
