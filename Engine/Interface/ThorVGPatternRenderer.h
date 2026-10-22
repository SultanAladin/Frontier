/*==========================================================================================================================================
                                                        THORVGPATTERNRENDERER.H
===========================================================================================================================================*/
// 🧩 Crisp SVG rasterization for Pattern Studio via ThorVG software renderer.
// One SVG shape is instanced/scaled multiple times via LayerStack transforms;
// ThorVG renders at native display scale for sharp edges, not low-res bitmaps.

#pragma once
#include <vector>
#include <string>
#include <cstdint>

namespace Frontier
{

struct PatternSvgInstance
{
    std::string SvgSource; // Full SVG text with <svg> root
    float X = 0, Y = 0; // Canvas position (512-unit canvas)
    float Width = 100, Height = 100;
    float Rotation = 0; // Degrees
    bool FlipX = false, FlipY = false;
    float Opacity = 1;
};

// Render a stack of SVG instances to RGBA at given extent. Each instance shares the same
// underlying SVG shape but with distinct transforms (LayerStack instancing).
// Returns RGBA8 with premultiplied alpha, row-major.
std::vector<uint8_t> RenderPatternLayerStack(
    const std::vector<PatternSvgInstance> &Instances,
    uint32_t Extent, float DisplayScale = 1.0f);

// Single SVG shape rasterized once and returned as RGBA; caller may instance via transforms
// without re-parsing the SVG (ThorVG picture reuse).
std::vector<uint8_t> RenderPatternSvg(
    const std::string &SvgSource,
    uint32_t Width, uint32_t Height, float DisplayScale = 1.0f);

// Validate SVG source without rendering; returns empty on success or diagnostic.
std::string ValidatePatternSvg(const std::string &SvgSource);

} // namespace Frontier
