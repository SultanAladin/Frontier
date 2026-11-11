/*============================================================================================================================================
                                                 TRAPEZOIDALTABPROFILE.CPP
============================================================================================================================================*/
// 🧩 Analytical trapezoidal tab & notch contour builder and patched Dear ImGui theme bridge

#include "Engine/Components/Docking/TrapezoidalTabProfile.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace Frontier::Components
{

namespace
{

[[nodiscard]] ImVec4 ConvertColorToFloat4(RgbaColor8 Color) noexcept
{
    constexpr float InverseByte = 1.0f / 255.0f;
    return ImVec4(
        static_cast<float>(Color.Red)   * InverseByte,
        static_cast<float>(Color.Green) * InverseByte,
        static_cast<float>(Color.Blue)  * InverseByte,
        static_cast<float>(Color.Alpha) * InverseByte);
}

void AppendContourVertex(TrapezoidalContour& Contour, ScreenPoint2D Point) noexcept
{
    if (Contour.VertexCount < MaxTrapezoidContourVertices)
    {
        Contour.Vertices[Contour.VertexCount++] = Point;
    }
}

void SampleQuadraticCorner(
    TrapezoidalContour& Contour,
    ScreenPoint2D       PreviousCorner,
    ScreenPoint2D       CurrentCorner,
    ScreenPoint2D       NextCorner,
    float               CornerRadius,
    std::size_t         Subdivisions) noexcept
{
    if (CornerRadius <= 0.01f || Subdivisions == 0)
    {
        AppendContourVertex(Contour, CurrentCorner);
        return;
    }

    const float IncomingDx  = PreviousCorner.X - CurrentCorner.X;
    const float IncomingDy  = PreviousCorner.Y - CurrentCorner.Y;
    const float OutgoingDx  = NextCorner.X - CurrentCorner.X;
    const float OutgoingDy  = NextCorner.Y - CurrentCorner.Y;
    const float IncomingLen = std::sqrt(IncomingDx * IncomingDx + IncomingDy * IncomingDy);
    const float OutgoingLen = std::sqrt(OutgoingDx * OutgoingDx + OutgoingDy * OutgoingDy);

    if (IncomingLen <= 1e-4f || OutgoingLen <= 1e-4f)
    {
        AppendContourVertex(Contour, CurrentCorner);
        return;
    }

    const float ClampedRadius = std::min({ CornerRadius, IncomingLen * 0.45f, OutgoingLen * 0.45f });
    const ScreenPoint2D EntryPoint{
        CurrentCorner.X + (IncomingDx / IncomingLen) * ClampedRadius,
        CurrentCorner.Y + (IncomingDy / IncomingLen) * ClampedRadius
    };
    const ScreenPoint2D ExitPoint{
        CurrentCorner.X + (OutgoingDx / OutgoingLen) * ClampedRadius,
        CurrentCorner.Y + (OutgoingDy / OutgoingLen) * ClampedRadius
    };

    for (std::size_t Step = 0; Step <= Subdivisions; ++Step)
    {
        const float Parameter      = static_cast<float>(Step) / static_cast<float>(Subdivisions);
        const float Complement     = 1.0f - Parameter;
        const float WeightEntry    = Complement * Complement;
        const float WeightControl  = 2.0f * Complement * Parameter;
        const float WeightExit     = Parameter * Parameter;

        AppendContourVertex(Contour, ScreenPoint2D{
            WeightEntry * EntryPoint.X + WeightControl * CurrentCorner.X + WeightExit * ExitPoint.X,
            WeightEntry * EntryPoint.Y + WeightControl * CurrentCorner.Y + WeightExit * ExitPoint.Y
        });
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                            METRIC & CONTOUR CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

TrapezoidalTabMetrics ConstructProjectZeroTabMetrics(float DisplayScale) noexcept
{
    const float SafeScale = (DisplayScale > 0.1f) ? DisplayScale : 1.0f;
    TrapezoidalTabMetrics Metrics{};
    Metrics.UpperCornerSlantPx  = std::trunc(14.0f * SafeScale);
    Metrics.UpperCornerRadiusPx = std::trunc(5.0f  * SafeScale);
    Metrics.AdjacentOverlapPx   = std::trunc(24.0f * SafeScale);
    Metrics.TabBodyHeightPx     = std::trunc(24.0f * SafeScale);
    Metrics.StripTopMarginPx    = std::trunc(4.0f  * SafeScale);
    Metrics.MinimumTabWidthPx   = std::trunc(92.0f * SafeScale);
    Metrics.CloseDiscRatio      = 0.80f;
    Metrics.DisplayScaleFactor  = SafeScale;
    return Metrics;
}

TrapezoidalContour ConstructTrapezoidalContour(
    float                        OriginX,
    float                        OriginY,
    float                        WidthPx,
    float                        HeightPx,
    const TrapezoidalTabMetrics& Metrics,
    bool                         EnableInvertedSlope) noexcept
{
    TrapezoidalContour Contour{};
    if (WidthPx <= 1.0f || HeightPx <= 1.0f)
    {
        return Contour;
    }

    const float EffectiveSlant = std::min(Metrics.UpperCornerSlantPx, WidthPx * 0.16f);
    Contour.EffectiveSlant     = EffectiveSlant;

    const float LeftX   = OriginX;
    const float RightX  = OriginX + WidthPx;
    const float TopY    = OriginY;
    const float BottomY = OriginY + HeightPx;

    std::array<ScreenPoint2D, 4> RawCorners{};
    std::array<float, 4>         CornerRadii{};

    if (!EnableInvertedSlope)
    {
        RawCorners[0]  = ScreenPoint2D{ LeftX,                  BottomY };
        RawCorners[1]  = ScreenPoint2D{ LeftX + EffectiveSlant, TopY    };
        RawCorners[2]  = ScreenPoint2D{ RightX - EffectiveSlant, TopY   };
        RawCorners[3]  = ScreenPoint2D{ RightX,                 BottomY };
        CornerRadii    = { 0.0f, Metrics.UpperCornerRadiusPx, Metrics.UpperCornerRadiusPx, 0.0f };
    }
    else
    {
        RawCorners[0]  = ScreenPoint2D{ LeftX,                   TopY    };
        RawCorners[1]  = ScreenPoint2D{ RightX,                  TopY    };
        RawCorners[2]  = ScreenPoint2D{ RightX - EffectiveSlant, BottomY };
        RawCorners[3]  = ScreenPoint2D{ LeftX + EffectiveSlant,  BottomY };
        CornerRadii    = { 0.0f, 0.0f, Metrics.UpperCornerRadiusPx, Metrics.UpperCornerRadiusPx };
    }

    for (std::size_t CornerIndex = 0; CornerIndex < 4; ++CornerIndex)
    {
        const ScreenPoint2D& Previous = RawCorners[(CornerIndex + 3) & 3u];
        const ScreenPoint2D& Current  = RawCorners[CornerIndex];
        const ScreenPoint2D& Next     = RawCorners[(CornerIndex + 1) & 3u];
        SampleQuadraticCorner(Contour, Previous, Current, Next, CornerRadii[CornerIndex], 6);
    }

    return Contour;
}

bool QueryPointInsideContour(const TrapezoidalContour& Contour, ScreenPoint2D TestPoint) noexcept
{
    if (Contour.VertexCount < 3)
    {
        return false;
    }

    bool InsidePolygon = false;
    std::size_t PreviousIndex = Contour.VertexCount - 1;
    for (std::size_t Index = 0; Index < Contour.VertexCount; PreviousIndex = Index++)
    {
        const ScreenPoint2D VertexA = Contour.Vertices[Index];
        const ScreenPoint2D VertexB = Contour.Vertices[PreviousIndex];

        const bool StraddlesScanline = ((VertexA.Y > TestPoint.Y) != (VertexB.Y > TestPoint.Y));
        if (StraddlesScanline)
        {
            const float IntersectionX =
                (VertexB.X - VertexA.X) * (TestPoint.Y - VertexA.Y) / (VertexB.Y - VertexA.Y) + VertexA.X;
            if (TestPoint.X < IntersectionX)
            {
                InsidePolygon = !InsidePolygon;
            }
        }
    }
    return InsidePolygon;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           PATCHED IMGUI STYLE CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

void ConfigureImGuiTrapezoidalStyle(
    const TrapezoidalTabMetrics& Metrics,
    const TrapezoidalThemeRamp&  ThemeRamp) noexcept
{
    ImGuiStyle& Style = ImGui::GetStyle();

    Style.WindowPadding                  = ImVec2(10.0f, 10.0f);
    Style.WindowRounding                 = 6.0f;
    Style.WindowBorderSize               = 1.0f;
    Style.ChildRounding                  = 5.0f;
    Style.ChildBorderSize                = 1.0f;
    Style.FramePadding                   = ImVec2(10.0f, 5.0f);
    Style.FrameRounding                  = 5.0f;
    Style.FrameBorderSize                = 1.0f;
    Style.ItemSpacing                    = ImVec2(8.0f, 6.0f);
    Style.ItemInnerSpacing               = ImVec2(6.0f, 4.0f);
    Style.IndentSpacing                  = 14.0f;
    Style.ScrollbarSize                  = 9.0f;
    Style.ScrollbarRounding              = 5.0f;
    Style.DockingSeparatorSize           = 2.0f;

    // Patched ImGui trapezoidal tab tokens (PatchA + PatchB + PatchC + PatchF + PatchG):
    Style.TabRounding                      = Metrics.UpperCornerRadiusPx;
    Style.TabBorderSize                    = 1.0f;
    Style.TabSlant                         = Metrics.UpperCornerSlantPx;
    Style.TabOverlap                       = Metrics.AdjacentOverlapPx;
    Style.TabHeight                        = Metrics.TabBodyHeightPx;
    Style.TabStripPadTop                   = Metrics.StripTopMarginPx;
    Style.TabButtonRounding                = 1.0f;
    Style.TabBarBorderSize                 = 0.0f;
    Style.TabBarOverlineSize               = 0.0f;
    Style.TabMinWidthBase                  = Metrics.MinimumTabWidthPx;
    Style.TabCloseButtonMinWidthSelected   = -1.0f;
    Style.TabCloseButtonMinWidthUnselected = -1.0f;

    ImVec4* Colors                       = Style.Colors;
    Colors[ImGuiCol_Text]                = ConvertColorToFloat4(ThemeRamp.PrimaryLabel);
    Colors[ImGuiCol_TextDisabled]        = ConvertColorToFloat4(ThemeRamp.MutedLabel);
    Colors[ImGuiCol_WindowBg]            = ConvertColorToFloat4(ThemeRamp.PanelSurface);
    Colors[ImGuiCol_ChildBg]             = ConvertColorToFloat4(ThemeRamp.ElevatedSection);
    Colors[ImGuiCol_Border]              = ConvertColorToFloat4(ThemeRamp.HairlineBorder);
    Colors[ImGuiCol_BorderShadow]        = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    Colors[ImGuiCol_FrameBg]             = ConvertColorToFloat4(ThemeRamp.ElevatedSection);
    Colors[ImGuiCol_FrameBgHovered]      = ConvertColorToFloat4(ThemeRamp.HoveredTabFill);
    Colors[ImGuiCol_FrameBgActive]       = ConvertColorToFloat4(ThemeRamp.HoveredTabFill);
    Colors[ImGuiCol_TitleBg]             = ConvertColorToFloat4(ThemeRamp.WorkspaceBackdrop);
    Colors[ImGuiCol_TitleBgActive]       = ConvertColorToFloat4(ThemeRamp.WorkspaceBackdrop);
    Colors[ImGuiCol_TitleBgCollapsed]    = ConvertColorToFloat4(ThemeRamp.WorkspaceBackdrop);
    Colors[ImGuiCol_Header]              = ConvertColorToFloat4(ThemeRamp.ElevatedSection);
    Colors[ImGuiCol_HeaderHovered]       = ConvertColorToFloat4(ThemeRamp.HoveredTabFill);
    Colors[ImGuiCol_HeaderActive]        = ConvertColorToFloat4(ThemeRamp.HoveredTabFill);
    Colors[ImGuiCol_Separator]           = ConvertColorToFloat4(ThemeRamp.HairlineBorder);
    Colors[ImGuiCol_SeparatorHovered]    = ConvertColorToFloat4(ThemeRamp.AmberAccent);
    Colors[ImGuiCol_SeparatorActive]     = ConvertColorToFloat4(ThemeRamp.AmberAccent);
    Colors[ImGuiCol_Button]              = ConvertColorToFloat4(ThemeRamp.ElevatedSection);
    Colors[ImGuiCol_ButtonHovered]       = ConvertColorToFloat4(ThemeRamp.HoveredTabFill);
    Colors[ImGuiCol_ButtonActive]        = ConvertColorToFloat4(ThemeRamp.PanelSurface);

    // Active tab matches PanelSurface (#141416) so its trapezoid base fuses seamlessly into the docked panel!
    Colors[ImGuiCol_Tab]                 = ConvertColorToFloat4(ThemeRamp.InactiveTabFill);
    Colors[ImGuiCol_TabHovered]          = ConvertColorToFloat4(ThemeRamp.HoveredTabFill);
    Colors[ImGuiCol_TabSelected]         = ConvertColorToFloat4(ThemeRamp.PanelSurface);
    Colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    Colors[ImGuiCol_TabDimmed]           = ConvertColorToFloat4(ThemeRamp.InactiveTabFill);
    Colors[ImGuiCol_TabDimmedSelected]   = ConvertColorToFloat4(ThemeRamp.PanelSurface);
    Colors[ImGuiCol_DockingEmptyBg]      = ConvertColorToFloat4(ThemeRamp.WorkspaceBackdrop);
    Colors[ImGuiCol_DockingPreview]      = ImVec4(0.133f, 0.773f, 0.369f, 0.35f);
}

} // namespace Frontier::Components
