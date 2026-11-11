/*============================================================================================================================================
                                                            WORKSPACEPANEVISUALS.H
============================================================================================================================================*/
// 🧩 Shared Project-Zero typography, color tokens, input readings, Showcase folder art, and vector emblems for workspace panes

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/Controls/ControlStyle.h"
#include "Engine/Components/Controls/TextInputField.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class OutlinerSlotCategory : uint8_t
{
    Folder              = 0, // [-] - Organizational Showcase folder container
    ConstructableFirst  = 1, // [-] - Generic constructable slot category 1
    ConstructableSecond = 2, // [-] - Generic constructable slot category 2
    ConstructableThird  = 3, // [-] - Generic constructable slot category 3
    ConstructableFourth = 4, // [-] - Generic constructable slot category 4
    Count               = 5, // [count] - Total slot categories
};

enum class ViewportProjectionPreset : uint8_t
{
    Perspective  = 0, // [-] - 3D perspective camera projection
    Orthographic = 1, // [-] - Isometric orthographic camera projection
    Top          = 2, // [-] - Plan-view top orthographic projection
    Front        = 3, // [-] - Elevation front orthographic projection
    Right        = 4, // [-] - Profile right orthographic projection
    Count        = 5, // [count] - Total projection presets
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct PaneInputReading
{
    ControlPointer PrimaryPointer   {};    // [-]     - Left-button cursor contact reading
    bool           SecondaryPressed = false; // [-]   - Right mouse button pressed this step
    float          WheelDeltaY      = 0.0f;  // [steps] - Vertical mouse-wheel scroll delta
    bool           ShiftHeld        = false; // [-]   - Shift modifier held
    bool           ControlHeld      = false; // [-]   - Control modifier held
    char           TypedUtf8[32]    {};    // [-]     - UTF-8 characters typed this step
    bool           BackspacePressed = false; // [-]   - Backspace key pressed this step
    bool           DeletePressed    = false; // [-]   - Delete key pressed this step
    bool           EnterPressed     = false; // [-]   - Enter key pressed this step
    bool           EscapePressed    = false; // [-]   - Escape key pressed this step
};

struct ProjectZeroSurfaceColors
{
    static constexpr ColorRgba DockBackground     = { 18,  18,  18,  255 }; // [-] - #121212 primary pane background
    static constexpr ColorRgba FooterBackground   = { 21,  21,  21,  255 }; // [-] - #151515 bottom telemetry strip
    static constexpr ColorRgba ControlWell        = { 25,  25,  25,  255 }; // [-] - #191919 search & counter well
    static constexpr ColorRgba ControlWellBorder  = { 44,  44,  44,  255 }; // [-] - #2C2C2C crisp recessed border
    static constexpr ColorRgba SectionSurface     = { 32,  32,  32,  255 }; // [-] - #202020 Showcase inspector surface
    static constexpr ColorRgba GenericSurface     = { 26,  26,  26,  255 }; // [-] - #1A1A1A generic inspector surface
    static constexpr ColorRgba OverlaySheet       = { 26,  26,  26,  252 }; // [-] - #1A1A1A dropdown & context sheet
    static constexpr ColorRgba OverlayHover       = { 44,  44,  44,  255 }; // [-] - #2C2C2C hovered directive row
    static constexpr ColorRgba RowHover           = { 28,  28,  28,  255 }; // [-] - #1C1C1C hovered outliner row
    static constexpr ColorRgba RowSelected        = { 38,  38,  38,  255 }; // [-] - #262626 selected outliner row
    static constexpr ColorRgba RowSelectedBorder  = { 68,  68,  68,  255 }; // [-] - #444444 selected row border
    static constexpr ColorRgba SelectionAmber     = { 255, 180, 84,  255 }; // [-] - #FFB454 selection & drop accent
    static constexpr ColorRgba StatusGreen        = { 52,  199, 89,  255 }; // [-] - #34C759 active visible status
    static constexpr ColorRgba StatusGreenInk     = { 16,  55,  25,  255 }; // [-] - #103719 dark check ink on green
    static constexpr ColorRgba ShowcaseSage       = { 162, 184, 124, 255 }; // [-] - #A2B87C default Showcase folder tint
    static constexpr ColorRgba ShowcaseEmerald    = { 88,  153, 121, 255 }; // [-] - #589979 collection accent green
    static constexpr ColorRgba ShowcaseWarmGold   = { 185, 169, 105, 255 }; // [-] - #B9A969 warm notes accent
    static constexpr ColorRgba PreviewGoldInk     = { 191, 162, 122, 255 }; // [-] - #BFA27A preview & active tool ink
    static constexpr ColorRgba ConstructCream     = { 222, 222, 219, 255 }; // [-] - #DEDEDB Construct button fill
    static constexpr ColorRgba ConstructDarkInk   = { 36,  36,  36,  255 }; // [-] - #242424 Construct button ink
    static constexpr ColorRgba PrimaryInk         = { 240, 240, 240, 255 }; // [-] - #F0F0F0 bright foreground ink
    static constexpr ColorRgba FolderHeadingInk   = { 211, 213, 207, 255 }; // [-] - #D3D5CF Showcase sage-white ink
    static constexpr ColorRgba SecondaryInk       = { 186, 186, 186, 255 }; // [-] - #BABABA default row label ink
    static constexpr ColorRgba MutedInk           = { 145, 145, 145, 255 }; // [-] - #919191 secondary caption ink
    static constexpr ColorRgba FaintInk           = { 105, 105, 105, 255 }; // [-] - #696969 subtle telemetry ink
    static constexpr ColorRgba HairlineLight      = { 255, 255, 255, 16  }; // [-] - Subtle translucent border rule
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

namespace WorkspacePaneVisuals
{

[[nodiscard]] ImFont*          ResolveDmSansFont(FontWeightClass Weight) noexcept;
[[nodiscard]] PaneInputReading CaptureImGuiPaneInput(Rectangle2D PaneScreenBounds, float ScaleFactor) noexcept;
void                           ForwardInputToTextBuffer(TextEditBuffer& Editor, const PaneInputReading& Input, double Δt) noexcept;

void DrawWeightedText(
    PixelSpace&      Surface,
    FontWeightClass  Weight,
    Point2D          Position,
    float            PixelSize,
    ColorRgba        Ink,
    std::string_view Text) noexcept;

[[nodiscard]] float MeasureWeightedTextWidth(
    PixelSpace&      Surface,
    FontWeightClass  Weight,
    float            PixelSize,
    std::string_view Text) noexcept;

void DrawTruncatedText(
    PixelSpace&      Surface,
    FontWeightClass  Weight,
    Point2D          Position,
    float            MaxWidth,
    float            PixelSize,
    ColorRgba        Ink,
    std::string_view Text) noexcept;

void DrawVerticalScrollbar(
    PixelSpace& Surface,
    Rectangle2D ViewportBounds,
    float       ContentHeight,
    float       ScrollOffsetY,
    Point2D     Cursor) noexcept;

void DrawShowcaseFolderEmblem(
    PixelSpace& Surface,
    Point2D     Origin,
    float       Size,
    ColorRgba   AccentTint = ProjectZeroSurfaceColors::ShowcaseSage,
    float       Opacity    = 1.0f) noexcept;

void DrawSlotCategoryEmblem(
    PixelSpace&          Surface,
    OutlinerSlotCategory Category,
    Point2D              Origin,
    float                Size,
    ColorRgba            FolderTint = ProjectZeroSurfaceColors::ShowcaseSage,
    float                Opacity    = 1.0f) noexcept;

[[nodiscard]] ColorRgba   QuerySlotCategoryColor(OutlinerSlotCategory Category, ColorRgba FolderTint) noexcept;
[[nodiscard]] const char* QueryProjectionLabel(ViewportProjectionPreset Preset) noexcept;

void DrawEditPencilEmblem(PixelSpace& Surface, Point2D Origin, float Size, ColorRgba Ink) noexcept;
void DrawSimulateFlaskEmblem(PixelSpace& Surface, Point2D Origin, float Size, ColorRgba Ink) noexcept;
void DrawPlayTriangleEmblem(PixelSpace& Surface, Point2D Origin, float Size, ColorRgba Ink) noexcept;
void DrawPauseBarsEmblem(PixelSpace& Surface, Point2D Origin, float Size, ColorRgba Ink) noexcept;
void DrawConstructCubePlusEmblem(PixelSpace& Surface, Point2D Origin, float Size, ColorRgba Ink) noexcept;
void DrawFocusReticleEmblem(PixelSpace& Surface, Point2D Origin, float Size, ColorRgba Ink) noexcept;
void DrawSplitViewportEmblem(PixelSpace& Surface, Point2D Origin, float Size, ColorRgba Ink) noexcept;
void DrawDiagnosticsBarsEmblem(PixelSpace& Surface, Point2D Origin, float Size, ColorRgba Ink) noexcept;
void DrawViewportGearEmblem(PixelSpace& Surface, Point2D Origin, float Size, ColorRgba Ink) noexcept;

void DrawOrientationAxes(
    PixelSpace&              Surface,
    Rectangle2D              PaneBounds,
    ViewportProjectionPreset Projection) noexcept;

} // namespace WorkspacePaneVisuals

} // namespace Frontier
