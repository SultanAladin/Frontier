/*============================================================================================================================================
                                                           WORKSPACEPANEVISUALS.CPP
============================================================================================================================================*/
// 🧩 Shared Project-Zero typography, color tokens, input readings, Showcase folder art, and vector emblems for workspace panes

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/WorkspacePaneVisuals.h"
#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>

namespace Frontier::WorkspacePaneVisuals
{

//------------------------------------------------------------------------------------------------------------------------
//                                              TYPOGRAPHY & INPUT UTILITIES
//------------------------------------------------------------------------------------------------------------------------

ImFont* ResolveDmSansFont(FontWeightClass Weight) noexcept
{
    TypographyRepository* Repository = TypographyRepository::QueryCurrent();
    if (Repository == nullptr)
    {
        return nullptr;
    }

    int32_t GroupIndex = Repository->ResolveGroupIndex("DM Sans");
    if (GroupIndex < 0)
    {
        GroupIndex = Repository->ResolveGroupIndex("Inter");
    }
    if (GroupIndex < 0)
    {
        return nullptr;
    }

    return Repository->QueryFont(static_cast<uint32_t>(GroupIndex), Weight);
}

PaneInputReading CaptureImGuiPaneInput(Rectangle2D PaneScreenBounds, float ScaleFactor) noexcept
{
    PaneInputReading Reading{};
    if (ImGui::GetCurrentContext() == nullptr)
    {
        return Reading;
    }

    const float   SafeScale = std::max(0.25f, ScaleFactor);
    const ImGuiIO& Io       = ImGui::GetIO();

    Reading.PrimaryPointer.X        = (Io.MousePos.x - PaneScreenBounds.X) / SafeScale;
    Reading.PrimaryPointer.Y        = (Io.MousePos.y - PaneScreenBounds.Y) / SafeScale;
    Reading.PrimaryPointer.Down     = Io.MouseDown[ImGuiMouseButton_Left];
    Reading.PrimaryPointer.Pressed  = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    Reading.PrimaryPointer.Released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    Reading.PrimaryPointer.Enabled  = true;

    Reading.SecondaryPressed = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    Reading.WheelDeltaY      = Io.MouseWheel;
    Reading.ShiftHeld        = Io.KeyShift;
    Reading.ControlHeld      = Io.KeyCtrl;
    Reading.BackspacePressed = ImGui::IsKeyPressed(ImGuiKey_Backspace);
    Reading.DeletePressed    = ImGui::IsKeyPressed(ImGuiKey_Delete);
    Reading.EnterPressed     = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
    Reading.EscapePressed    = ImGui::IsKeyPressed(ImGuiKey_Escape);

    std::size_t WriteCursor = 0u;
    for (int Index = 0; Index < Io.InputQueueCharacters.Size; ++Index)
    {
        const ImWchar Codepoint = Io.InputQueueCharacters[Index];
        if (Codepoint < 32u || Codepoint == 127u)
        {
            continue;
        }
        if (Codepoint < 0x80u && WriteCursor + 2u < sizeof(Reading.TypedUtf8))
        {
            Reading.TypedUtf8[WriteCursor++] = static_cast<char>(Codepoint);
        }
    }
    Reading.TypedUtf8[WriteCursor] = '\0';
    return Reading;
}

void ForwardInputToTextBuffer(TextEditBuffer& Editor, const PaneInputReading& Input, double Δt) noexcept
{
    if (!Editor.Active)
    {
        return;
    }

    Editor.BlinkPhase = std::fmod(Editor.BlinkPhase + static_cast<float>(std::max(0.0, Δt)), ControlMetrics::CaretBlinkSeconds);
    if (Input.TypedUtf8[0] != '\0')
    {
        Editor.Insert(Input.TypedUtf8);
    }
    if (Input.BackspacePressed)
    {
        Editor.Backspace();
    }
    if (Input.DeletePressed)
    {
        Editor.DeleteForward();
    }
    if (Input.EnterPressed)
    {
        Editor.Finalize();
    }
    if (Input.EscapePressed)
    {
        Editor.Cancel();
    }
}

void DrawWeightedText(
    PixelSpace&      Surface,
    FontWeightClass  Weight,
    Point2D          Position,
    float            PixelSize,
    ColorRgba        Ink,
    std::string_view Text) noexcept
{
    if (Text.empty())
    {
        return;
    }

    ImFont* Face = ResolveDmSansFont(Weight);
    if (Face != nullptr)
    {
        Surface.PushTypeface(Face);
    }
    Surface.Text(Position, PixelSize, Ink, Text.data(), Text.data() + Text.size());
    if (Face != nullptr)
    {
        Surface.PopTypeface();
    }
}

float MeasureWeightedTextWidth(
    PixelSpace&      Surface,
    FontWeightClass  Weight,
    float            PixelSize,
    std::string_view Text) noexcept
{
    if (Text.empty())
    {
        return 0.0f;
    }

    ImFont* Face = ResolveDmSansFont(Weight);
    if (Face != nullptr)
    {
        Surface.PushTypeface(Face);
    }
    const float Width = Surface.MeasureTextWidth(PixelSize, Text.data(), Text.data() + Text.size());
    if (Face != nullptr)
    {
        Surface.PopTypeface();
    }
    return Width;
}

void DrawTruncatedText(
    PixelSpace&      Surface,
    FontWeightClass  Weight,
    Point2D          Position,
    float            MaxWidth,
    float            PixelSize,
    ColorRgba        Ink,
    std::string_view Text) noexcept
{
    if (Text.empty() || MaxWidth <= 2.0f)
    {
        return;
    }

    if (MeasureWeightedTextWidth(Surface, Weight, PixelSize, Text) <= MaxWidth)
    {
        DrawWeightedText(Surface, Weight, Position, PixelSize, Ink, Text);
        return;
    }

    constexpr std::string_view Ellipsis = "...";
    const float                EllipsisW = MeasureWeightedTextWidth(Surface, Weight, PixelSize, Ellipsis);
    if (EllipsisW >= MaxWidth)
    {
        return;
    }

    std::size_t CutCount = Text.size();
    while (CutCount > 0u)
    {
        --CutCount;
        std::string Candidate(Text.substr(0u, CutCount));
        Candidate.append(Ellipsis);
        if (MeasureWeightedTextWidth(Surface, Weight, PixelSize, Candidate) <= MaxWidth)
        {
            DrawWeightedText(Surface, Weight, Position, PixelSize, Ink, Candidate);
            return;
        }
    }
}

void DrawVerticalScrollbar(
    PixelSpace& Surface,
    Rectangle2D ViewportBounds,
    float       ContentHeight,
    float       ScrollOffsetY,
    Point2D     Cursor) noexcept
{
    if (ContentHeight <= ViewportBounds.Height + 1.0f || ViewportBounds.Height <= 16.0f)
    {
        return;
    }

    const float VisibleRatio = std::clamp(ViewportBounds.Height / ContentHeight, 0.08f, 1.0f);
    const float TrackPadding = 4.0f;
    const float TrackHeight  = ViewportBounds.Height - TrackPadding * 2.0f;
    const float ThumbHeight  = std::max(24.0f, TrackHeight * VisibleRatio);
    const float MaxScroll    = std::max(1.0f, ContentHeight - ViewportBounds.Height);
    const float ScrollT      = std::clamp(ScrollOffsetY / MaxScroll, 0.0f, 1.0f);
    const float ThumbY       = ViewportBounds.Y + TrackPadding + (TrackHeight - ThumbHeight) * ScrollT;

    const Rectangle2D ThumbBounds{
        ViewportBounds.Right() - 8.0f,
        ThumbY,
        5.0f,
        ThumbHeight
    };
    const bool Hovered = ThumbBounds.Contains(Cursor);
    Surface.FillRectangle(ThumbBounds, Hovered ? ColorRgba{ 58, 58, 58, 235 } : ColorRgba{ 38, 38, 38, 220 }, 3.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                       SHOWCASE FOLDER & CATEGORY VECTOR EMBLEMS
//------------------------------------------------------------------------------------------------------------------------

void DrawShowcaseFolderEmblem(
    PixelSpace& Surface,
    Point2D     Origin,
    float       Size,
    ColorRgba   AccentTint,
    float       Opacity) noexcept
{
    const float Unit = Size / 256.0f;
    const auto  ProjectX = [&](float Coord) noexcept { return Origin.X + Coord * Unit; };
    const auto  ProjectY = [&](float Coord) noexcept { return Origin.Y + Coord * Unit; };

    // Back folder body (#303030 with #555555 perimeter)
    const Rectangle2D BackPlate{ ProjectX(28.0f), ProjectY(54.0f), 196.0f * Unit, 146.0f * Unit };
    Surface.FillRectangle(BackPlate, ColorRgba{ 48, 48, 48, 255 }.ScaledAlpha(Opacity), std::max(2.0f, 20.0f * Unit));
    Surface.StrokeRectangle(BackPlate, ColorRgba{ 85, 85, 85, 255 }.ScaledAlpha(Opacity), std::max(2.0f, 20.0f * Unit), 1.0f);

    // Two cream index sheets peeking above the front pocket (#DEDEDB)
    const Rectangle2D LeftSheet{ ProjectX(48.0f), ProjectY(62.0f), 88.0f * Unit, 72.0f * Unit };
    const Rectangle2D RightSheet{ ProjectX(126.0f), ProjectY(68.0f), 74.0f * Unit, 68.0f * Unit };
    Surface.FillRectangle(LeftSheet, ColorRgba{ 222, 222, 219, 255 }.ScaledAlpha(Opacity), std::max(1.5f, 10.0f * Unit));
    Surface.FillRectangle(RightSheet, ColorRgba{ 214, 214, 210, 255 }.ScaledAlpha(Opacity), std::max(1.5f, 9.0f * Unit));

    // Stepped front pocket (#3C3C3C with #555555 stroke)
    const std::array<Point2D, 8> PocketVertices = {{
        { ProjectX(28.0f),  ProjectY(88.0f)  },
        { ProjectX(108.0f), ProjectY(88.0f)  },
        { ProjectX(134.0f), ProjectY(104.0f) },
        { ProjectX(224.0f), ProjectY(104.0f) },
        { ProjectX(224.0f), ProjectY(202.0f) },
        { ProjectX(28.0f),  ProjectY(202.0f) },
        { ProjectX(28.0f),  ProjectY(145.0f) },
        { ProjectX(28.0f),  ProjectY(88.0f)  },
    }};
    Surface.FillPolygon(std::span<const Point2D>(PocketVertices.data(), 6u), ColorRgba{ 60, 60, 60, 255 }.ScaledAlpha(Opacity));
    Surface.StrokePolyline(
        std::span<const Point2D>(PocketVertices.data(), 6u),
        ColorRgba{ 88, 88, 88, 255 }.ScaledAlpha(Opacity),
        1.0f,
        true);

    // Subtle pocket lip highlight
    Surface.StrokeLine(
        { ProjectX(34.0f), ProjectY(90.0f) },
        { ProjectX(106.0f), ProjectY(90.0f) },
        ColorRgba{ 255, 255, 255, 28 }.ScaledAlpha(Opacity),
        1.0f);

    // Showcase sage-accented corner seat (#393939 + AccentTint)
    const Rectangle2D CornerSeat{ ProjectX(42.0f), ProjectY(144.0f), 46.0f * Unit, 46.0f * Unit };
    Surface.FillRectangle(CornerSeat, ColorRgba{ 52, 52, 52, 255 }.ScaledAlpha(Opacity), std::max(1.5f, 10.0f * Unit));
    Surface.FillRectangle(CornerSeat.Inset(1.0f), AccentTint.WithAlpha(42u).ScaledAlpha(Opacity), std::max(1.0f, 9.0f * Unit));
    Surface.StrokeRectangle(CornerSeat, AccentTint.WithAlpha(165u).ScaledAlpha(Opacity), std::max(1.5f, 10.0f * Unit), 1.0f);

    // Mini tree-branch emblem inside corner seat
    const Point2D TopCenter{ CornerSeat.Center().X, CornerSeat.Y + CornerSeat.Height * 0.30f };
    const Point2D MidCenter{ CornerSeat.Center().X, CornerSeat.Y + CornerSeat.Height * 0.54f };
    const Point2D LeftLeaf{ CornerSeat.X + CornerSeat.Width * 0.28f, CornerSeat.Y + CornerSeat.Height * 0.72f };
    const Point2D RightLeaf{ CornerSeat.X + CornerSeat.Width * 0.72f, CornerSeat.Y + CornerSeat.Height * 0.72f };
    const ColorRgba SymbolInk = AccentTint.ScaledAlpha(Opacity);
    Surface.StrokeLine(TopCenter, MidCenter, SymbolInk, 1.1f);
    Surface.StrokeLine({ LeftLeaf.X, MidCenter.Y }, { RightLeaf.X, MidCenter.Y }, SymbolInk, 1.1f);
    Surface.FillCircle(TopCenter, std::max(1.1f, 3.8f * Unit), SymbolInk);
    Surface.FillCircle(LeftLeaf, std::max(1.0f, 3.2f * Unit), SymbolInk);
    Surface.FillCircle(RightLeaf, std::max(1.0f, 3.2f * Unit), SymbolInk);
}

ColorRgba QuerySlotCategoryColor(OutlinerSlotCategory Category, ColorRgba FolderTint) noexcept
{
    switch (Category)
    {
        case OutlinerSlotCategory::Folder:              return FolderTint;
        case OutlinerSlotCategory::ConstructableFirst:  return ColorRgba{ 79,  163, 227, 255 };
        case OutlinerSlotCategory::ConstructableSecond: return ColorRgba{ 123, 198, 148, 255 };
        case OutlinerSlotCategory::ConstructableThird:  return ColorRgba{ 245, 166, 35,  255 };
        case OutlinerSlotCategory::ConstructableFourth: return ColorRgba{ 181, 156, 230, 255 };
        case OutlinerSlotCategory::Count:               break;
    }
    return ProjectZeroSurfaceColors::SecondaryInk;
}

void DrawSlotCategoryEmblem(
    PixelSpace&          Surface,
    OutlinerSlotCategory Category,
    Point2D              Origin,
    float                Size,
    ColorRgba            FolderTint,
    float                Opacity) noexcept
{
    if (Category == OutlinerSlotCategory::Folder)
    {
        DrawShowcaseFolderEmblem(Surface, Origin, Size, FolderTint, Opacity);
        return;
    }

    const ColorRgba Tint = QuerySlotCategoryColor(Category, FolderTint).ScaledAlpha(Opacity);
    ControlIcon     Icon = ControlIcon::CubeShape;
    switch (Category)
    {
        case OutlinerSlotCategory::ConstructableFirst:  Icon = ControlIcon::CubeShape;       break;
        case OutlinerSlotCategory::ConstructableSecond: Icon = ControlIcon::LayersSlabs;     break;
        case OutlinerSlotCategory::ConstructableThird:  Icon = ControlIcon::SunIllumination; break;
        case OutlinerSlotCategory::ConstructableFourth: Icon = ControlIcon::CameraBody;      break;
        case OutlinerSlotCategory::Folder:
        case OutlinerSlotCategory::Count:               break;
    }

    VectorIconRepository::DrawControl(
        Surface,
        Icon,
        VectorIconPlacement{ Origin.X, Origin.Y, Size, Tint, 1.8f });
}

const char* QueryProjectionLabel(ViewportProjectionPreset Preset) noexcept
{
    switch (Preset)
    {
        case ViewportProjectionPreset::Perspective:  return "Perspective";
        case ViewportProjectionPreset::Orthographic: return "Orthographic";
        case ViewportProjectionPreset::Top:          return "Top";
        case ViewportProjectionPreset::Front:        return "Front";
        case ViewportProjectionPreset::Right:        return "Right";
        case ViewportProjectionPreset::Count:        break;
    }
    return "Perspective";
}

//------------------------------------------------------------------------------------------------------------------------
//                                            VIEWPORT TOOLBAR VECTOR EMBLEMS
//------------------------------------------------------------------------------------------------------------------------

void DrawEditPencilEmblem(PixelSpace& Surface, Point2D Origin, float Size, ColorRgba Ink) noexcept
{
    VectorIconRepository::StrokeSvgPath(
        Surface,
        "M15 3 L21 9 L9 21 L3 21 L3 15 Z M12.5 5.5 L18.5 11.5",
        VectorIconPlacement{ Origin.X, Origin.Y, Size, Ink, 1.85f },
        24.0f);
}

void DrawSimulateFlaskEmblem(PixelSpace& Surface, Point2D Origin, float Size, ColorRgba Ink) noexcept
{
    VectorIconRepository::StrokeSvgPath(
        Surface,
        "M9 3 H15 M10 3 V8.5 L4.5 18.5 A2 2 0 0 0 6.2 21 H17.8 A2 2 0 0 0 19.5 18.5 L14 8.5 V3 M7.2 15 H16.8",
        VectorIconPlacement{ Origin.X, Origin.Y, Size, Ink, 1.85f },
        24.0f);
}

void DrawPlayTriangleEmblem(PixelSpace& Surface, Point2D Origin, float Size, ColorRgba Ink) noexcept
{
    VectorIconRepository::FillSvgPath(
        Surface,
        "M7 4 L20 12 L7 20 Z",
        VectorIconPlacement{ Origin.X, Origin.Y, Size, Ink, 1.5f },
        24.0f);
}

void DrawPauseBarsEmblem(PixelSpace& Surface, Point2D Origin, float Size, ColorRgba Ink) noexcept
{
    const float Scale = Size / 24.0f;
    Surface.FillRectangle({ Origin.X + 6.0f * Scale, Origin.Y + 4.5f * Scale, 4.0f * Scale, 15.0f * Scale }, Ink, 1.2f * Scale);
    Surface.FillRectangle({ Origin.X + 14.0f * Scale, Origin.Y + 4.5f * Scale, 4.0f * Scale, 15.0f * Scale }, Ink, 1.2f * Scale);
}

void DrawConstructCubePlusEmblem(PixelSpace& Surface, Point2D Origin, float Size, ColorRgba Ink) noexcept
{
    VectorIconRepository::StrokeSvgPath(
        Surface,
        "M9 3 L16 6.5 V13.5 L9 17 L2 13.5 V6.5 Z M2 6.5 L9 10 L16 6.5 M9 10 V17",
        VectorIconPlacement{ Origin.X, Origin.Y, Size, Ink.ScaledAlpha(0.78f), 1.8f },
        24.0f);
    VectorIconRepository::StrokeSvgPath(
        Surface,
        "M18.5 13 V21 M14.5 17 H22.5",
        VectorIconPlacement{ Origin.X, Origin.Y, Size, Ink, 2.2f },
        24.0f);
}

void DrawFocusReticleEmblem(PixelSpace& Surface, Point2D Origin, float Size, ColorRgba Ink) noexcept
{
    VectorIconRepository::StrokeSvgPath(
        Surface,
        "M3 8 V3 H8 M16 3 H21 V8 M21 16 V21 H16 M8 21 H3 V16",
        VectorIconPlacement{ Origin.X, Origin.Y, Size, Ink, 1.9f },
        24.0f);
    Surface.FillCircle({ Origin.X + Size * 0.5f, Origin.Y + Size * 0.5f }, Size * 0.12f, Ink);
}

void DrawSplitViewportEmblem(PixelSpace& Surface, Point2D Origin, float Size, ColorRgba Ink) noexcept
{
    const float Scale = Size / 24.0f;
    const Rectangle2D Outer{ Origin.X + 2.5f * Scale, Origin.Y + 4.5f * Scale, 19.0f * Scale, 15.0f * Scale };
    Surface.StrokeRectangle(Outer, Ink, 2.5f * Scale, 1.4f);
    Surface.StrokeLine(
        { Origin.X + 12.0f * Scale, Outer.Y + 1.0f },
        { Origin.X + 12.0f * Scale, Outer.Bottom() - 1.0f },
        Ink,
        1.4f);
}

void DrawDiagnosticsBarsEmblem(PixelSpace& Surface, Point2D Origin, float Size, ColorRgba Ink) noexcept
{
    const float Scale = Size / 24.0f;
    Surface.FillRectangle({ Origin.X + 3.0f * Scale,  Origin.Y + 13.0f * Scale, 3.8f * Scale, 8.0f * Scale  }, Ink, 1.0f * Scale);
    Surface.FillRectangle({ Origin.X + 10.1f * Scale, Origin.Y + 8.0f * Scale,  3.8f * Scale, 13.0f * Scale }, Ink, 1.0f * Scale);
    Surface.FillRectangle({ Origin.X + 17.2f * Scale, Origin.Y + 3.0f * Scale,  3.8f * Scale, 18.0f * Scale }, Ink, 1.0f * Scale);
}

void DrawViewportGearEmblem(PixelSpace& Surface, Point2D Origin, float Size, ColorRgba Ink) noexcept
{
    VectorIconRepository::DrawControl(
        Surface,
        ControlIcon::SettingsGear,
        VectorIconPlacement{ Origin.X, Origin.Y, Size, Ink, 1.85f });
}

void DrawOrientationAxes(
    PixelSpace&              Surface,
    Rectangle2D              PaneBounds,
    ViewportProjectionPreset Projection) noexcept
{
    if (PaneBounds.Width < 110.0f || PaneBounds.Height < 110.0f)
    {
        return;
    }

    const Point2D TriadOrigin{ PaneBounds.Right() - 44.0f, PaneBounds.Bottom() - 38.0f };
    Point2D       AxisX{ TriadOrigin.X + 24.0f, TriadOrigin.Y + 8.0f  };
    Point2D       AxisY{ TriadOrigin.X - 18.0f, TriadOrigin.Y + 11.0f };
    Point2D       AxisZ{ TriadOrigin.X,         TriadOrigin.Y - 26.0f };

    if (Projection == ViewportProjectionPreset::Top)
    {
        AxisX = { TriadOrigin.X + 25.0f, TriadOrigin.Y };
        AxisY = { TriadOrigin.X,         TriadOrigin.Y - 25.0f };
        AxisZ = { TriadOrigin.X - 12.0f, TriadOrigin.Y + 12.0f };
    }
    else if (Projection == ViewportProjectionPreset::Front)
    {
        AxisX = { TriadOrigin.X + 25.0f, TriadOrigin.Y };
        AxisZ = { TriadOrigin.X,         TriadOrigin.Y - 25.0f };
        AxisY = { TriadOrigin.X - 12.0f, TriadOrigin.Y + 12.0f };
    }
    else if (Projection == ViewportProjectionPreset::Right)
    {
        AxisY = { TriadOrigin.X + 25.0f, TriadOrigin.Y };
        AxisZ = { TriadOrigin.X,         TriadOrigin.Y - 25.0f };
        AxisX = { TriadOrigin.X - 12.0f, TriadOrigin.Y + 12.0f };
    }

    const ColorRgba RedAxis{ 216, 104, 104, 230 };
    const ColorRgba GreenAxis{ 110, 192, 122, 230 };
    const ColorRgba BlueAxis{ 101, 153, 226, 230 };

    Surface.StrokeLine(TriadOrigin, AxisX, RedAxis, 1.6f);
    Surface.StrokeLine(TriadOrigin, AxisY, GreenAxis, 1.6f);
    Surface.StrokeLine(TriadOrigin, AxisZ, BlueAxis, 1.6f);

    Surface.FillCircle(AxisX, 3.2f, RedAxis);
    Surface.FillCircle(AxisY, 3.2f, GreenAxis);
    Surface.FillCircle(AxisZ, 3.2f, BlueAxis);
    Surface.FillCircle(TriadOrigin, 2.4f, ColorRgba{ 196, 196, 196, 220 });

    DrawWeightedText(Surface, FontWeightClass::Medium, { AxisX.X + 4.0f, AxisX.Y - 6.0f }, 9.0f, RedAxis, "X");
    DrawWeightedText(Surface, FontWeightClass::Medium, { AxisY.X - 10.0f, AxisY.Y - 5.0f }, 9.0f, GreenAxis, "Y");
    DrawWeightedText(Surface, FontWeightClass::Medium, { AxisZ.X - 3.0f, AxisZ.Y - 14.0f }, 9.0f, BlueAxis, "Z");
}

} // namespace Frontier::WorkspacePaneVisuals
