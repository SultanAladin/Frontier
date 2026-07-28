/*==============================================================================================================================================
                                                                SVGRASTERIZER.H
==============================================================================================================================================*/
// 🧩 The backend-agnostic SVG-to-pixels stage: rasterizes an SVG source buffer into a caller-owned un-premultiplied RGBA8 block at a requested
//    square pixel size, using the vendored thorvg software raster engine. It touches no GPU — the output is a plain CPU pixel buffer the icon
//    registry then uploads. thorvg's one-time engine bring-up is reference-counted here so many rasterizations share a single Initializer::init.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_ICONS_SVGRASTERIZER_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_ICONS_SVGRASTERIZER_H

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The result of one rasterization: a dense RGBA8 block laid out row-major, tightly packed (stride == PixelWidth * 4), plus
//    its dimensions. Pixels are un-premultiplied ABGR8888S (thorvg's straight-alpha byte order), which maps directly onto
//    VK_FORMAT_R8G8B8A8_UNORM with no channel swizzle. RasterSuccess is false when the source failed to load or the engine was
//    unavailable, in which case Pixels is empty and the dimensions are zero.
struct SvgRasterOutput
{
    std::vector<uint32_t> Pixels;              // [rgba] - PixelWidth * PixelHeight straight-alpha texels, row-major
    uint32_t              PixelWidth  = 0u;    // [px]   - raster width
    uint32_t              PixelHeight = 0u;    // [px]   - raster height
    bool                  RasterSuccess = false; // [-]  - true only when the SVG loaded and rendered cleanly
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Bring the thorvg software engine up once for the process. Reference-counted: every InitializeSvgEngine must be paired with a
// FinalizeSvgEngine, and only the first call actually initializes / the last actually terminates. Returns false when thorvg
// could not start the software raster engine. Safe to call from icon-pack registration before any rasterization.
bool InitializeSvgEngine();

// Release one reference taken by InitializeSvgEngine; the underlying engine terminates only when the last reference drops.
void FinalizeSvgEngine() noexcept;

// Rasterize SvgByteSource (a complete SVG document of SvgByteCount bytes) into an EdgePixels x EdgePixels RGBA8 block scaled to
// fill that square. The engine must already be initialized. On failure returns an output with RasterSuccess == false and no
// pixels. The picture is scaled to the requested edge regardless of its intrinsic viewBox, so every icon rasterizes to a
// uniform tile. EdgePixels is clamped to a sane ceiling to bound the allocation.
[[nodiscard]] SvgRasterOutput RasterizeSvg(const char* SvgByteSource, uint32_t SvgByteCount, uint32_t EdgePixels);

} // namespace Frontier

#endif
