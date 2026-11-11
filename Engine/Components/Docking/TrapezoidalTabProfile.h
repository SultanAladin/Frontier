/*============================================================================================================================================
                                                  TRAPEZOIDALTABPROFILE.H
============================================================================================================================================*/
// 🧩 Shared Project-Zero trapezoidal tab silhouette, top/bottom notch contour math, and patched ImGui style configuration

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Frontier::Components
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr std::size_t MaxTrapezoidContourVertices = 24;    // [count] - Sampled outline vertices including rounded upper shoulders
inline constexpr float       DmSansCssToImGuiScale       = 1.302f; // [-]     - (992 - (-310)) / 1000 metric factor matching browser px

//------------------------------------------------------------------------------------------------------------------------
//                                                     STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ScreenPoint2D
{
    float X = 0.0f; // [px] - Horizontal display coordinate
    float Y = 0.0f; // [px] - Vertical display coordinate
};

struct RgbaColor8
{
    uint8_t Red   = 0;   // [0..255] - Red channel
    uint8_t Green = 0;   // [0..255] - Green channel
    uint8_t Blue  = 0;   // [0..255] - Blue channel
    uint8_t Alpha = 255; // [0..255] - Opacity channel
};

struct TrapezoidalTabMetrics
{
    float UpperCornerSlantPx  = 14.0f; // [px] - Horizontal inset of upper corners (PatchA TabSlant)
    float UpperCornerRadiusPx = 5.0f;  // [px] - Quadratic Bezier fillet radius at upper corners
    float AdjacentOverlapPx   = 24.0f; // [px] - Interlocking horizontal overlap between neighbor tabs (PatchB TabOverlap)
    float TabBodyHeightPx     = 24.0f; // [px] - Trapezoidal tab height inside strip (PatchB TabHeight)
    float StripTopMarginPx    = 4.0f;  // [px] - Strip clearance above tabs; total strip = 28 px (PatchB TabStripPadTop)
    float MinimumTabWidthPx   = 92.0f; // [px] - Minimum interactive tab width before shrink clamp
    float CloseDiscRatio      = 0.80f; // [-]  - Circular button diameter relative to tab height (PatchC/PatchG)
    float DisplayScaleFactor  = 1.0f;  // [-]  - Per-monitor DPI scale factor applied to pixel metrics
};

struct TrapezoidalThemeRamp
{
    RgbaColor8 WorkspaceBackdrop  { 11,  11,  12,  255 }; // [-] - Root dock strip / outer gutter (#0b0b0c)
    RgbaColor8 PanelSurface       { 20,  20,  22,  255 }; // [-] - Active panel interior & active tab fill (#141416)
    RgbaColor8 ElevatedSection    { 26,  26,  29,  255 }; // [-] - Inspector section background (#1a1a1d)
    RgbaColor8 InactiveTabFill    { 15,  15,  17,  255 }; // [-] - Background of unselected trapezoidal tabs (#0f0f11)
    RgbaColor8 HoveredTabFill     { 30,  30,  34,  255 }; // [-] - Hovered trapezoidal tab fill (#1e1e22)
    RgbaColor8 HairlineBorder     { 38,  38,  43,  255 }; // [-] - 1 px structural separator & outline (#26262b)
    RgbaColor8 PrimaryLabel       { 234, 234, 238, 255 }; // [-] - Active tab & heading text (#eaeaee)
    RgbaColor8 MutedLabel         { 142, 142, 150, 255 }; // [-] - Inactive tab & secondary label text (#8e8e96)
    RgbaColor8 EmeraldIndicator   { 34,  197, 94,  255 }; // [-] - Active status indicator (#22c55e)
    RgbaColor8 AmberAccent        { 245, 158, 11,  255 }; // [-] - Solar / highlight accent (#f59e0b)
    RgbaColor8 RejectionCrimson   { 239, 68,  68,  255 }; // [-] - Rejected cross-application dock indicator (#ef4444)
};

struct TrapezoidalContour
{
    std::array<ScreenPoint2D, MaxTrapezoidContourVertices> Vertices{};          // [px]    - Closed polygon points in clockwise screen order
    std::size_t                                            VertexCount  = 0;    // [count] - Active vertex count in Vertices
    float                                                  EffectiveSlant = 0.0f; // [px]  - Clamped horizontal bevel slant actually applied
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] constexpr float ComputeImGuiFontPixelSize(float BrowserCssPixels, float DisplayScale = 1.0f) noexcept
{
    return BrowserCssPixels * DmSansCssToImGuiScale * DisplayScale;
}

[[nodiscard]] TrapezoidalTabMetrics ConstructProjectZeroTabMetrics(float DisplayScale = 1.0f) noexcept;

[[nodiscard]] TrapezoidalContour ConstructTrapezoidalContour(
    float                        OriginX,
    float                        OriginY,
    float                        WidthPx,
    float                        HeightPx,
    const TrapezoidalTabMetrics& Metrics,
    bool                         EnableInvertedSlope = false) noexcept;

[[nodiscard]] bool QueryPointInsideContour(const TrapezoidalContour& Contour, ScreenPoint2D TestPoint) noexcept;

void ConfigureImGuiTrapezoidalStyle(
    const TrapezoidalTabMetrics& Metrics,
    const TrapezoidalThemeRamp&  ThemeRamp = TrapezoidalThemeRamp{}) noexcept;

} // namespace Frontier::Components
