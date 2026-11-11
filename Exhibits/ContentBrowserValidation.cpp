/*============================================================================================================================================
                                                         CONTENTBROWSERVALIDATION.CPP
============================================================================================================================================*/
// 🗂️ End-to-end CTest validation and 6-image CPU visual snapshot suite for ContentBrowser, ColourCodex, Colour Filters, and Vertical/Horizontal strips

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/ContentBrowser/ColourCodex.h"
#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace
{

using Frontier::ColorRgba;
using Frontier::ColourRepositoryLayout;
using Frontier::ColourCodex;
using Frontier::ColourCodexEntry;
using Frontier::ColourContrastInk;
using Frontier::ColourFilterGroup;
using Frontier::ColourSlotRegistration;
using Frontier::ColourSortOrder;
using Frontier::ContentBrowserPanel;
using Frontier::ContentBrowserPointerInput;
using Frontier::ContentCategoryKind;
using Frontier::ContentShelfExtent;
using Frontier::PixelSpace;
using Frontier::Point2D;
using Frontier::Rectangle2D;
using Frontier::ThemeRepository;
using Frontier::TypographyRepository;
using Frontier::Components::ConfigureImGuiWorkspaceTheme;
using Frontier::Components::DockPlacementDirection;
using Frontier::Components::HeadlessWorkspaceDockContainer;
using Frontier::Components::HostWindowToken;
using Frontier::Components::PaneDirectiveMask;
using Frontier::Components::RgbaColor8;
using Frontier::Components::WorkspaceDockPaneDescriptor;
using Frontier::Exhibits::ResolveRepositoryRootPath;
using Frontier::Exhibits::SoftwareRasterCanvas;
using Frontier::Windowing::WindowConfigurationDescriptor;

//------------------------------------------------------------------------------------------------------------------------
//                                                 VALIDATION ASSERTIONS
//------------------------------------------------------------------------------------------------------------------------

bool VerifyAssertion(bool Condition, const char* Description) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[ContentBrowserValidation] FAILED: %s\n", Description);
        return false;
    }
    return true;
}

bool ValidateColourCodexMathematicsAndTomlPersistence()
{
    ColourCodex Codex{};
    if (!VerifyAssertion(Codex.QueryEntries().size() >= 38u, "Built-in ColourCodex contains >= 38 named colours"))
    {
        return false;
    }

    // Contrast ink verification matching reference swatch tiles & August Beige & Brown strips
    const std::size_t FigIdx = Codex.FindEntryByName("Fig Purple");
    const std::size_t PinIdx = Codex.FindEntryByName("Pineapple Yellow");
    const std::size_t BlzIdx = Codex.FindEntryByName("August Blaze");
    const std::size_t TanIdx = Codex.FindEntryByName("August Tangerine");
    const std::size_t MarIdx = Codex.FindEntryByName("August Marigold");
    const std::size_t LemIdx = Codex.FindEntryByName("August Lemon");

    if (!VerifyAssertion(FigIdx < Codex.QueryEntries().size() && PinIdx < Codex.QueryEntries().size(), "Built-in Fig Purple & Pineapple Yellow exist"))
    {
        return false;
    }

    const ColourCodexEntry* FigEntry = Codex.QueryEntryAt(FigIdx);
    const ColourCodexEntry* PinEntry = Codex.QueryEntryAt(PinIdx);
    const ColourCodexEntry* BlzEntry = Codex.QueryEntryAt(BlzIdx);
    const ColourCodexEntry* TanEntry = Codex.QueryEntryAt(TanIdx);
    const ColourCodexEntry* MarEntry = Codex.QueryEntryAt(MarIdx);
    const ColourCodexEntry* LemEntry = Codex.QueryEntryAt(LemIdx);

    if (!VerifyAssertion(FigEntry->HexUpper == "#3B1F3A" && !ColourCodex::QueryContrastInk(FigEntry->FillColour).DarkInk, "Fig Purple (#3B1F3A) selects white contrast ink"))
    {
        return false;
    }
    if (!VerifyAssertion(PinEntry->HexUpper == "#F6B800" && ColourCodex::QueryContrastInk(PinEntry->FillColour).DarkInk, "Pineapple Yellow (#F6B800) selects black contrast ink"))
    {
        return false;
    }
    if (!VerifyAssertion(
            BlzEntry != nullptr && BlzEntry->HexCompact == "FF7B00" && !ColourCodex::QueryContrastInk(BlzEntry->FillColour).DarkInk &&
            TanEntry != nullptr && TanEntry->HexCompact == "FF8800" && !ColourCodex::QueryContrastInk(TanEntry->FillColour).DarkInk &&
            MarEntry != nullptr && MarEntry->HexCompact == "FF9500" && ColourCodex::QueryContrastInk(MarEntry->FillColour).DarkInk &&
            LemEntry != nullptr && LemEntry->HexCompact == "FFEA00" && ColourCodex::QueryContrastInk(LemEntry->FillColour).DarkInk,
            "August Beige & Brown spectrum matches white ink on FF7B00..FF8800 and black ink on FF9500..FFEA00"))
    {
        return false;
    }

    const auto AugustIndices = Codex.QueryFilteredIndices(ColourFilterGroup::AugustBeigeAndBrown, ColourSortOrder::Spectrum);
    if (!VerifyAssertion(AugustIndices.size() == 10u, "August Beige & Brown Colour Filter yields 10 spectrum bands"))
    {
        return false;
    }

    // Project colour addition + .toml persistence round-trip
    (void)Codex.AddProjectColour("Studio Ultramarine", "#2454FF", "Blues");
    const std::filesystem::path TempToml = std::filesystem::temp_directory_path() / "FrontierColourCodexValidation.toml";
    std::error_code             RemoveErr{};
    std::filesystem::remove(TempToml, RemoveErr);

    std::string Diagnostic{};
    if (!VerifyAssertion(Codex.SaveToTomlFile(TempToml, Diagnostic), "Export ColourCodex .toml file"))
    {
        return false;
    }

    ColourCodex ReloadedCodex{};
    if (!VerifyAssertion(ReloadedCodex.LoadFromTomlFile(TempToml, Diagnostic), "Import ColourCodex .toml file"))
    {
        return false;
    }

    const std::size_t UltramarineIdx = ReloadedCodex.FindEntryByName("Studio Ultramarine");
    if (!VerifyAssertion(
            UltramarineIdx < ReloadedCodex.QueryEntries().size() &&
            ReloadedCodex.QueryEntryAt(UltramarineIdx)->HexUpper == "#2454FF",
            "Reloaded ColourCodex preserves custom .toml entry Studio Ultramarine (#2454FF)"))
    {
        return false;
    }

    std::filesystem::remove(TempToml, RemoveErr);
    return true;
}

void RenderWorkspaceInspectorSlotOverlay(
    PixelSpace& Canvas,
    Rectangle2D AlbedoSlotRect,
    ColorRgba   AlbedoColour,
    Rectangle2D EmissionSlotRect,
    ColorRgba   EmissionColour)
{
    const Rectangle2D Tile{ AlbedoSlotRect.X - 16.0f, AlbedoSlotRect.Y - 40.0f, AlbedoSlotRect.Width + 32.0f, 156.0f };
    Canvas.FillRectangle(Tile, ColorRgba{ 28u, 28u, 31u, 245u }, 14.0f);
    Canvas.StrokeRectangle(Tile, ColorRgba{ 255u, 255u, 255u, 28u }, 14.0f, 1.0f);
    Canvas.Text({ Tile.X + 16.0f, Tile.Y + 13.0f }, 10.0f, ColorRgba{ 168u, 154u, 132u, 255u }, "MATERIAL COLOUR SLOTS (DROP TARGETS)");

    // Slot 1: Surface Albedo
    Canvas.FillRectangle(AlbedoSlotRect, ColorRgba{ 38u, 38u, 42u, 255u }, 9.0f);
    Canvas.StrokeRectangle(AlbedoSlotRect, ColorRgba{ 255u, 255u, 255u, 30u }, 9.0f, 1.0f);
    Canvas.FillRectangle({ AlbedoSlotRect.X + 8.0f, AlbedoSlotRect.Y + 6.0f, 36.0f, AlbedoSlotRect.Height - 12.0f }, AlbedoColour, 6.0f);
    const std::string AlbedoText = "Surface Albedo · " + ColourCodex::FormatHexUpper(AlbedoColour);
    Canvas.Text({ AlbedoSlotRect.X + 54.0f, AlbedoSlotRect.Y + 12.0f }, 11.0f, ColorRgba{ 228u, 228u, 230u, 255u }, AlbedoText.c_str());

    // Slot 2: Emission Tint
    Canvas.FillRectangle(EmissionSlotRect, ColorRgba{ 38u, 38u, 42u, 255u }, 9.0f);
    Canvas.StrokeRectangle(EmissionSlotRect, ColorRgba{ 255u, 255u, 255u, 30u }, 9.0f, 1.0f);
    Canvas.FillRectangle({ EmissionSlotRect.X + 8.0f, EmissionSlotRect.Y + 6.0f, 36.0f, EmissionSlotRect.Height - 12.0f }, EmissionColour, 6.0f);
    const std::string EmissionText = "Emission Tint · " + ColourCodex::FormatHexUpper(EmissionColour);
    Canvas.Text({ EmissionSlotRect.X + 54.0f, EmissionSlotRect.Y + 12.0f }, 11.0f, ColorRgba{ 228u, 228u, 230u, 255u }, EmissionText.c_str());
}

bool ValidateAndRenderContentBrowserSnapshots()
{
    IMGUI_CHECKVERSION();
    ImGuiContext* GuiSession = ImGui::CreateContext();
    if (!VerifyAssertion(GuiSession != nullptr, "Initialize ImGui session"))
    {
        return false;
    }
    ImGui::SetCurrentContext(GuiSession);

    ImGuiIO& Io    = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.LogFilename = nullptr;
    Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    const auto           RepositoryRoot = ResolveRepositoryRootPath();
    TypographyRepository Typefaces{};
    if (!VerifyAssertion(
            Typefaces.LoadRepository(RepositoryRoot / "EngineContent/FontsArchives") == 5u,
            "Load FontRepository.FontSpace"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }
    TypographyRepository::AssignCurrent(&Typefaces);
    if (ImFont* DefaultFace = Typefaces.QueryFont(0u, Frontier::FontWeightClass::Regular))
    {
        Io.FontDefault = DefaultFace;
    }

    unsigned char* TexturePixels = nullptr;
    int            TextureWidth  = 0;
    int            TextureHeight = 0;
    Io.Fonts->GetTexDataAsRGBA32(&TexturePixels, &TextureWidth, &TextureHeight);

    constexpr uint32_t CanvasWidth  = 1440u;
    constexpr uint32_t CanvasHeight = 900u;
    const Rectangle2D  ViewportBounds{ 0.0f, 0.0f, static_cast<float>(CanvasWidth), static_cast<float>(CanvasHeight) };

    HeadlessWorkspaceDockContainer Dock{};
    const auto WindowDescriptor = WindowConfigurationDescriptor::ConstructApplicationWindow("Frontier", CanvasWidth, CanvasHeight);
    if (!Dock.Initialize(WindowDescriptor))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }
    (void)Dock.RegisterPane(WorkspaceDockPaneDescriptor{ "Outliner", DockPlacementDirection::SplitLeft, 0.21f, PaneDirectiveMask::StandardPaneDirectives, {} });
    (void)Dock.RegisterPane(WorkspaceDockPaneDescriptor{ "Viewport", DockPlacementDirection::CenterTabGroup, 0.54f, PaneDirectiveMask::CentralViewportDirectives, {} });
    (void)Dock.RegisterPane(WorkspaceDockPaneDescriptor{ "Inspector", DockPlacementDirection::SplitRight, 0.25f, PaneDirectiveMask::StandardPaneDirectives, {} });

    ThemeRepository Theme{};
    ContentBrowserPanel Browser{};

    ColorRgba         SurfaceAlbedoColour   = { 196u, 178u, 160u, 255u };
    ColorRgba         EmissionTintColour = { 56u,  72u,  96u,  255u };
    const Rectangle2D AlbedoSlotRect     = { 1092.0f, 118.0f, 304.0f, 38.0f };
    const Rectangle2D EmissionSlotRect   = { 1092.0f, 168.0f, 304.0f, 38.0f };

    Browser.RegisterColourSlot(ColourSlotRegistration{ "Material.SurfaceAlbedo", "Surface Albedo", AlbedoSlotRect, &SurfaceAlbedoColour, {} });
    Browser.RegisterColourSlot(ColourSlotRegistration{ "Material.EmissionTint", "Emission Tint", EmissionSlotRect, &EmissionTintColour, {} });

    const std::filesystem::path OutputDirectory = RepositoryRoot / "Exhibits/Images/ContentBrowser";
    std::filesystem::create_directories(OutputDirectory);

    auto StepPresentation = [&](const ContentBrowserPointerInput& Input)
    {
        ConfigureImGuiWorkspaceTheme(Dock.ActiveTabMetrics(), Theme);
        Io.DisplaySize = ImVec2(static_cast<float>(CanvasWidth), static_cast<float>(CanvasHeight));
        Io.DeltaTime   = 1.0f / 60.0f;
        ImGui::NewFrame();
        Dock.PresentWindowImGuiPanes(HostWindowToken{ 0u });

        PixelSpace ForegroundCanvas(ImGui::GetForegroundDrawList(), { 0.0f, 0.0f }, 1.0f);
        RenderWorkspaceInspectorSlotOverlay(ForegroundCanvas, AlbedoSlotRect, SurfaceAlbedoColour, EmissionSlotRect, EmissionTintColour);
        Browser.RenderWithInput(ForegroundCanvas, ViewportBounds, Input, 1.0 / 60.0);
        ImGui::Render();
    };

    auto WriteCurrentSnapshot = [&](const char* FileName) -> bool
    {
        SoftwareRasterCanvas Raster(CanvasWidth, CanvasHeight, RgbaColor8{ 16, 16, 18, 255 });
        Raster.RasterizeImGuiBatches(ImGui::GetDrawData());
        return VerifyAssertion(Raster.WritePngFile(OutputDirectory / FileName), FileName);
    };

    ContentBrowserPointerInput IdleInput{};

    // Snapshot 01: Closed bottom-edge "ContentBrowser" trapezoidal notch
    Browser.ConfigureShelfExtent(ContentShelfExtent::Closed, true);
    Browser.ActivateCategory(ContentCategoryKind::Materials);
    for (int Warmup = 0; Warmup < 3; ++Warmup)
    {
        StepPresentation(IdleInput);
    }
    if (!VerifyAssertion(!Browser.InspectReading().ColourFiltersVisible, "Colour Filters hidden when Materials is active"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }
    if (!WriteCurrentSnapshot("01-ClosedBottomNotch.png"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // Snapshot 02: FullHeight ContentBrowser -> Colours -> GridCircles -> Fig Purple (#3B1F3A, white ink)
    Browser.ConfigureShelfExtent(ContentShelfExtent::FullHeight, true);
    Browser.ActivateCategory(ContentCategoryKind::Colours);
    Browser.ConfigureColourLayout(ColourRepositoryLayout::GridCircles);
    Browser.ConfigureColourFilter(ColourFilterGroup::All);
    (void)Browser.SelectColourByName("Fig Purple");
    StepPresentation(IdleInput);
    if (!VerifyAssertion(Browser.InspectReading().ColourFiltersVisible, "Colour Filters visible when Colours is active"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }
    if (!WriteCurrentSnapshot("02-ColoursGridFigPurple.png"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // Snapshot 03: Colours -> GridCircles -> Pineapple Yellow (#F6B800, black ink)
    (void)Browser.SelectColourByName("Pineapple Yellow");
    StepPresentation(IdleInput);
    if (!WriteCurrentSnapshot("03-ColoursGridPineappleYellow.png"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // Snapshot 04: Colours -> VerticalStrips -> "August Beige & Brown" (FF7B00..FFEA00 rotated -90deg HEX labels)
    Browser.ConfigureColourLayout(ColourRepositoryLayout::VerticalStrips);
    Browser.ConfigureColourFilter(ColourFilterGroup::AugustBeigeAndBrown);
    (void)Browser.SelectColourByName("August Amber");
    StepPresentation(IdleInput);
    if (!VerifyAssertion(Browser.InspectReading().VisibleEntryCount == 10u, "VerticalStrips renders 10 August Beige & Brown strips"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }
    if (!WriteCurrentSnapshot("04-ColoursVerticalStripsAugustBeigeAndBrown.png"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // Snapshot 05: Colours -> HorizontalStrips -> All spectrum bands stacked horizontally
    Browser.ConfigureColourLayout(ColourRepositoryLayout::HorizontalStrips);
    Browser.ConfigureColourFilter(ColourFilterGroup::AugustBeigeAndBrown);
    (void)Browser.SelectColourByName("August Gold");
    StepPresentation(IdleInput);
    if (!WriteCurrentSnapshot("05-ColoursHorizontalStrips.png"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // Interactive Test A: Double-click on Pineapple Yellow in SplitHalf Grid view -> assigns Surface Albedo (#F6B800)
    Browser.ConfigureShelfExtent(ContentShelfExtent::SplitHalf, true);
    Browser.ConfigureColourLayout(ColourRepositoryLayout::GridCircles);
    Browser.ConfigureColourFilter(ColourFilterGroup::All);
    Browser.FocusColourSlot("Material.SurfaceAlbedo");
    StepPresentation(IdleInput);

    const std::size_t PinIndex = Browser.QueryColourCodex().FindEntryByName("Pineapple Yellow");
    const Rectangle2D PinCell  = Browser.QueryRenderedColourCellBounds(PinIndex);
    if (!VerifyAssertion(PinCell.Width > 10.0f && PinCell.Height > 10.0f, "Pineapple Yellow grid cell hitbox recorded"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }
    {
        ContentBrowserPointerInput DblClick{};
        DblClick.CursorX       = PinCell.Center().X;
        DblClick.CursorY       = PinCell.Center().Y;
        DblClick.Pressed       = true;
        DblClick.DoubleClicked = true;
        StepPresentation(DblClick);
    }
    if (!VerifyAssertion(
            SurfaceAlbedoColour.Red == 246u && SurfaceAlbedoColour.Green == 184u && SurfaceAlbedoColour.Blue == 0u,
            "Double-clicking Pineapple Yellow updates Surface Albedo slot to #F6B800"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // Interactive Test B: Drag August Blaze (#FF7B00) from VerticalStrips onto Emission Tint slot
    Browser.ConfigureColourLayout(ColourRepositoryLayout::VerticalStrips);
    Browser.ConfigureColourFilter(ColourFilterGroup::AugustBeigeAndBrown);
    StepPresentation(IdleInput);

    const std::size_t BlazeIndex = Browser.QueryColourCodex().FindEntryByName("August Blaze");
    const Rectangle2D BlazeStrip = Browser.QueryRenderedColourCellBounds(BlazeIndex);
    if (!VerifyAssertion(BlazeStrip.Width > 10.0f && BlazeStrip.Height > 10.0f, "August Blaze vertical strip hitbox recorded"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    {
        ContentBrowserPointerInput Press{};
        Press.CursorX  = BlazeStrip.Center().X;
        Press.CursorY  = BlazeStrip.Center().Y;
        Press.Pressed  = true;
        Press.HeldDown = true;
        StepPresentation(Press);

        ContentBrowserPointerInput Move{};
        Move.CursorX  = EmissionSlotRect.Center().X;
        Move.CursorY  = EmissionSlotRect.Center().Y;
        Move.HeldDown = true;
        StepPresentation(Move);

        if (!VerifyAssertion(Browser.InspectReading().DraggingColourActive, "Colour drag tracking activates across deadzone"))
        {
            ImGui::DestroyContext(GuiSession);
            return false;
        }

        ContentBrowserPointerInput Release{};
        Release.CursorX  = EmissionSlotRect.Center().X;
        Release.CursorY  = EmissionSlotRect.Center().Y;
        Release.Released = true;
        StepPresentation(Release);
    }

    if (!VerifyAssertion(
            EmissionTintColour.Red == 255u && EmissionTintColour.Green == 123u && EmissionTintColour.Blue == 0u,
            "Dragging August Amber vertical strip onto Emission Tint updates slot to #FF7B00"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    // Snapshot 06: SplitHalf shelf showing live Material Colour Slots (#F6B800 & #FF7B00) above August Beige & Brown strips
    StepPresentation(IdleInput);
    if (!WriteCurrentSnapshot("06-SplitShelfColourSlotAssignment.png"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }

    TypographyRepository::AssignCurrent(nullptr);
    Dock.Finalize();
    ImGui::DestroyContext(GuiSession);
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    if (!ValidateColourCodexMathematicsAndTomlPersistence())
    {
        return 1;
    }
    if (!ValidateAndRenderContentBrowserSnapshots())
    {
        return 1;
    }
    std::printf("[ContentBrowserValidation] All ColourCodex, Colour Filters, Vertical/Horizontal strip, and colour-slot checks passed.\n");
    return 0;
}
