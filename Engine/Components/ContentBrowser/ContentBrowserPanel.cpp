/*============================================================================================================================================
                                                          CONTENTBROWSERPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of ContentBrowser drawer, pre-baked ThumbnailSheet rendering, SSD/HDD streaming, workspace masks & ColourCodex

#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{

[[nodiscard]] float TargetRatioForExtent(ContentShelfExtent Extent) noexcept
{
    switch (Extent)
    {
        case ContentShelfExtent::Closed:     return 0.0f;
        case ContentShelfExtent::SplitHalf:  return 0.48f;
        case ContentShelfExtent::FullHeight: return 0.94f;
    }
    return 0.0f;
}

[[nodiscard]] std::pair<std::string, std::string> SplitUppercaseTileHeading(std::string_view Name)
{
    std::string Upper;
    Upper.reserve(Name.size());
    for (const char Ch : Name)
    {
        Upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(Ch))));
    }
    const std::size_t SpacePos = Upper.find(' ');
    if (SpacePos == std::string::npos)
    {
        return { Upper, {} };
    }
    return { Upper.substr(0u, SpacePos), Upper.substr(SpacePos + 1u) };
}

bool ConstructCapsuleControl(
    PixelSpace&                       Canvas,
    Rectangle2D                       Bounds,
    const char*                       Label,
    bool                              Selected,
    const ContentBrowserPointerInput& Input,
    float                             FontSize      = 10.0f,
    bool                              AccentPrimary = false)
{
    const Point2D Cursor{ Input.CursorX, Input.CursorY };
    const bool    Hovered = Bounds.Contains(Cursor);

    ColorRgba Fill   = ColorRgba{ 32u, 32u, 34u, 255u };
    ColorRgba Border = ColorRgba{ 255u, 255u, 255u, 22u };
    ColorRgba Ink    = ColorRgba{ 168u, 168u, 172u, 255u };

    if (AccentPrimary)
    {
        Fill   = Hovered ? ColorRgba{ 237u, 224u, 203u, 255u } : ColorRgba{ 211u, 197u, 178u, 255u };
        Border = Fill;
        Ink    = ColorRgba{ 36u, 32u, 27u, 255u };
    }
    else if (Selected)
    {
        Fill   = ColorRgba{ 54u, 49u, 41u, 255u };
        Border = ColorRgba{ 211u, 197u, 178u, 130u };
        Ink    = ColorRgba{ 239u, 228u, 210u, 255u };
    }
    else if (Hovered)
    {
        Fill   = ColorRgba{ 44u, 44u, 47u, 255u };
        Border = ColorRgba{ 255u, 255u, 255u, 48u };
        Ink    = ColorRgba{ 226u, 226u, 228u, 255u };
    }

    const float Radius = std::min(14.0f, Bounds.Height * 0.5f);
    Canvas.FillRectangle(Bounds, Fill, Radius);
    Canvas.StrokeRectangle(Bounds, Border, Radius, 1.0f);
    Canvas.TextCentered(Bounds, FontSize, Ink, Label);
    return Hovered && Input.Pressed;
}

constexpr std::array<ContentCategoryKind, 15> OrderedRailCategories = {{
    ContentCategoryKind::All,
    ContentCategoryKind::Materials,
    ContentCategoryKind::Colours,
    ContentCategoryKind::Geometry,
    ContentCategoryKind::UVLayouts,
    ContentCategoryKind::Textures,
    ContentCategoryKind::Images,
    ContentCategoryKind::Vectors,
    ContentCategoryKind::Fonts,
    ContentCategoryKind::Simulations,
    ContentCategoryKind::Volumetrics,
    ContentCategoryKind::Particles,
    ContentCategoryKind::Photometry,
    ContentCategoryKind::Audio,
    ContentCategoryKind::Shaders,
}};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           CONTENTBROWSERPANEL IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

ContentBrowserPanel::ContentBrowserPanel()
    : ShelfAxis(ShelfSpring.RegisterAxis(0.0, 240.0, 26.0))
{
    (void)StreamChannelThread.Initialize({}, Threading::StorageHardwareTier::AutoDetect, nullptr);
}

ContentBrowserPanel::~ContentBrowserPanel() noexcept
{
    StreamChannelThread.Finalize();
}

void ContentBrowserPanel::ConfigureShelfExtent(ContentShelfExtent RequestedExtent, bool Immediate) noexcept
{
    TargetExtent       = RequestedExtent;
    const double Ratio = static_cast<double>(TargetRatioForExtent(RequestedExtent));
    if (Immediate)
    {
        ShelfSpring.Axis(ShelfAxis).ConfigurePosition(Ratio);
    }
    else
    {
        ShelfSpring.Axis(ShelfAxis).Depart(Ratio);
    }
}

void ContentBrowserPanel::ConfigureWorkspaceDomain(WorkspaceDomainKind Workspace) noexcept
{
    ActiveWorkspace   = Workspace;
    RepositoryScrollY = 0.0f;

    // 🔴 Snap active category if newly entered workspace blocks previous selection
    if (!ThumbnailSheetRepository::QueryCategoryPermitted(ActiveWorkspace, Category))
    {
        Category = ContentCategoryKind::All;
    }

    const auto ValidEntries = ThumbnailRepository.QueryFilteredEntries(ActiveWorkspace, ContentCategoryKind::All, OriginScope, SearchBuffer);
    if (!ValidEntries.empty() && std::find(ValidEntries.begin(), ValidEntries.end(), SelectedContentIndex) == ValidEntries.end())
    {
        SelectedContentIndex = ValidEntries.front();
    }
}

bool ContentBrowserPanel::ActivateCategory(ContentCategoryKind NextCategory) noexcept
{
    if (!ThumbnailSheetRepository::QueryCategoryPermitted(ActiveWorkspace, NextCategory))
    {
        StatusMessage = std::string("Blocked in ") + ThumbnailSheetRepository::QueryWorkspaceTitle(ActiveWorkspace) +
                        " workspace: " + ThumbnailSheetRepository::QueryCategoryTitle(NextCategory);
        return false;
    }
    Category          = NextCategory;
    RepositoryScrollY = 0.0f;
    const auto ValidEntries = ThumbnailRepository.QueryFilteredEntries(ActiveWorkspace, Category, OriginScope, SearchBuffer);
    if (!ValidEntries.empty())
    {
        SelectedContentIndex = ValidEntries.front();
    }
    return true;
}

void ContentBrowserPanel::ConfigureOriginFilter(ContentOriginFilter NextOrigin) noexcept
{
    OriginScope       = NextOrigin;
    RepositoryScrollY = 0.0f;
}

void ContentBrowserPanel::ConfigureStandardLayout(StandardRepositoryLayout NextLayout) noexcept
{
    StandardLayout = NextLayout;
}

void ContentBrowserPanel::ConfigureColourLayout(ColourRepositoryLayout NextLayout) noexcept
{
    ColourLayout      = NextLayout;
    RepositoryScrollY = 0.0f;
}

void ContentBrowserPanel::ConfigureColourFilter(ColourFilterGroup NextFilter) noexcept
{
    ColourFilter      = NextFilter;
    RepositoryScrollY = 0.0f;
    const auto Filtered = CodexLibrary.QueryFilteredIndices(ColourFilter, ColourSort, SearchBuffer);
    if (!Filtered.empty() && std::find(Filtered.begin(), Filtered.end(), SelectedColourIndex) == Filtered.end())
    {
        SelectedColourIndex = Filtered.front();
    }
}

void ContentBrowserPanel::ConfigureColourSort(ColourSortOrder NextOrder) noexcept
{
    ColourSort = NextOrder;
}

void ContentBrowserPanel::ConfigureSearchQuery(std::string_view SearchText)
{
    SearchBuffer      = std::string(SearchText);
    RepositoryScrollY = 0.0f;
}

void ContentBrowserPanel::ConfigureStorageHardwareTier(
    Threading::StorageHardwareTier RequestedTier,
    const std::filesystem::path&   MountTarget) noexcept
{
    StreamChannelThread.ConfigureHardwareTierOverride(RequestedTier, MountTarget);
}

void ContentBrowserPanel::ConfigureImportDestination(const ImportIntakeConfiguration& Policy)
{
    ThumbnailRepository.ConfigureImportPolicy(Policy);
}

bool ContentBrowserPanel::SelectColourByName(std::string_view AliasName) noexcept
{
    const std::size_t Found = CodexLibrary.FindEntryByName(AliasName);
    if (Found >= CodexLibrary.QueryEntries().size())
    {
        return false;
    }
    SelectedColourIndex = Found;
    return true;
}

ImportIntakeResult ContentBrowserPanel::IntakeExternalAssetDrop(const std::filesystem::path& DroppedSourceFile)
{
    ImportIntakeResult Result = ThumbnailRepository.IntakeExternalAssetDrop(DroppedSourceFile, ActiveWorkspace);
    StatusMessage             = Result.DiagnosticMessage;
    if (!Result.Accepted)
    {
        return Result;
    }

    if (Result.DetectedCategory == ContentCategoryKind::Colours)
    {
        std::string Diagnostic{};
        std::size_t AddedColourIdx = 0u;
        if (ColourCodex::MatchesSwatchExtension(DroppedSourceFile))
        {
            if (CodexLibrary.LoadSingleSwatchFile(DroppedSourceFile, Diagnostic, &AddedColourIdx))
            {
                SelectedColourIndex = AddedColourIdx;
            }
        }
        else
        {
            (void)CodexLibrary.LoadFromTomlFile(DroppedSourceFile, Diagnostic, true);
        }
    }

    OriginScope          = ContentOriginFilter::Imported;
    Category             = Result.DetectedCategory;
    SelectedContentIndex = Result.RepositoryIndex;
    return Result;
}

void ContentBrowserPanel::ClearColourSlots() noexcept
{
    RegisteredSlots.clear();
}

void ContentBrowserPanel::RegisterColourSlot(const ColourSlotRegistration& Registration)
{
    for (ColourSlotRegistration& Existing : RegisteredSlots)
    {
        if (Existing.SlotIdentifier == Registration.SlotIdentifier)
        {
            Existing = Registration;
            return;
        }
    }
    RegisteredSlots.push_back(Registration);
    if (FocusedSlotId.empty())
    {
        FocusedSlotId = Registration.SlotIdentifier;
    }
}

void ContentBrowserPanel::FocusColourSlot(std::string_view SlotIdentifier) noexcept
{
    FocusedSlotId = std::string(SlotIdentifier);
}

void ContentBrowserPanel::AttachOutlinerFallback(OutlinerPanel* TargetOutliner) noexcept
{
    FallbackOutliner = TargetOutliner;
}

bool ContentBrowserPanel::ApplySelectedColourToActiveSlot()
{
    return ApplyColourIndexToSlot(SelectedColourIndex, FocusedSlotId);
}

bool ContentBrowserPanel::ApplyColourIndexToSlot(std::size_t ColourIndex, std::string_view TargetSlotIdentifier)
{
    const ColourCodexEntry* Entry = CodexLibrary.QueryEntryAt(ColourIndex);
    if (Entry == nullptr)
    {
        return false;
    }

    ColourSlotRegistration* ChosenSlot = nullptr;
    if (!TargetSlotIdentifier.empty())
    {
        for (ColourSlotRegistration& Slot : RegisteredSlots)
        {
            if (Slot.SlotIdentifier == TargetSlotIdentifier)
            {
                ChosenSlot = &Slot;
                break;
            }
        }
    }
    if (ChosenSlot == nullptr && !FocusedSlotId.empty())
    {
        for (ColourSlotRegistration& Slot : RegisteredSlots)
        {
            if (Slot.SlotIdentifier == FocusedSlotId)
            {
                ChosenSlot = &Slot;
                break;
            }
        }
    }
    if (ChosenSlot == nullptr && !RegisteredSlots.empty())
    {
        ChosenSlot = &RegisteredSlots.front();
    }

    if (ChosenSlot != nullptr)
    {
        if (ChosenSlot->TargetColour != nullptr)
        {
            *ChosenSlot->TargetColour = Entry->FillColour;
        }
        if (ChosenSlot->NotifyColourAssigned)
        {
            ChosenSlot->NotifyColourAssigned(Entry->FillColour, *Entry);
        }
        FocusedSlotId = ChosenSlot->SlotIdentifier;
        StatusMessage = "Applied " + Entry->AliasName + " (" + Entry->HexUpper + ") -> " + ChosenSlot->DisplayTitle;
        return true;
    }

    if (FallbackOutliner != nullptr)
    {
        const auto SelectedTokens = FallbackOutliner->QuerySelectedTokens();
        if (!SelectedTokens.empty())
        {
            if (OutlinerRecordEntry* OutlinerEntry = FallbackOutliner->AccessEntry(SelectedTokens.front()))
            {
                OutlinerEntry->FolderTint = Entry->FillColour;
                StatusMessage             = "Applied " + Entry->AliasName + " (" + Entry->HexUpper + ") -> " + OutlinerEntry->Title;
                return true;
            }
        }
    }
    return false;
}

ContentBrowserReading ContentBrowserPanel::InspectReading() const noexcept
{
    uint32_t PermittedCount = 0u;
    for (const ContentCategoryKind Candidate : OrderedRailCategories)
    {
        if (Candidate != ContentCategoryKind::All &&
            ThumbnailSheetRepository::QueryCategoryPermitted(ActiveWorkspace, Candidate))
        {
            ++PermittedCount;
        }
    }

    ContentBrowserReading Reading{};
    Reading.ShelfExtent            = TargetExtent;
    Reading.ShelfProgress          = std::clamp(static_cast<float>(ShelfSpring.Axis(ShelfAxis).Current), 0.0f, 1.0f);
    Reading.ActiveWorkspace        = ActiveWorkspace;
    Reading.ActiveCategory         = Category;
    Reading.ActiveOrigin           = OriginScope;
    Reading.ActiveColourLayout     = ColourLayout;
    Reading.ActiveStandardLayout   = StandardLayout;
    Reading.ActiveColourFilter     = ColourFilter;
    Reading.ActiveColourSort       = ColourSort;
    Reading.ActiveStorageTier      = StreamChannelThread.QueryResolvedTier();
    Reading.ActiveImportMode       = ThumbnailRepository.QueryImportPolicy().Mode;
    Reading.PermittedCategoryCount = PermittedCount;
    Reading.VisibleEntryCount      = LastVisibleCount;
    Reading.ColourFiltersVisible   = (Category == ContentCategoryKind::Colours);
    Reading.DraggingColourActive   = DraggingColour;
    if (const ColourCodexEntry* Selected = CodexLibrary.QueryEntryAt(SelectedColourIndex))
    {
        Reading.SelectedColourAlias = Selected->AliasName;
    }
    Reading.LastStatusMessage = StatusMessage;
    return Reading;
}

bool ContentBrowserPanel::QueryOpen() const noexcept
{
    return TargetExtent != ContentShelfExtent::Closed || ShelfSpring.Axis(ShelfAxis).Current > 0.005;
}

bool ContentBrowserPanel::QueryBlocksUnderlyingWorkspace() const noexcept
{
    return TargetExtent == ContentShelfExtent::FullHeight && !DraggingColour;
}

Rectangle2D ContentBrowserPanel::QueryNotchTabBounds(Rectangle2D ViewportLogicalBounds) const noexcept
{
    const float Progress   = std::clamp(static_cast<float>(ShelfSpring.Axis(ShelfAxis).Current), 0.0f, 1.0f);
    const float ShelfH     = std::round(ViewportLogicalBounds.Height * Progress);
    const float DrawerTopY = ViewportLogicalBounds.Bottom() - ShelfH;
    const float NotchW     = 212.0f;
    const float NotchH     = 22.0f;
    return {
        ViewportLogicalBounds.X + (ViewportLogicalBounds.Width - NotchW) * 0.5f,
        std::max(ViewportLogicalBounds.Y, DrawerTopY - NotchH),
        NotchW,
        NotchH,
    };
}

Rectangle2D ContentBrowserPanel::QueryRenderedColourCellBounds(std::size_t ColourIndex) const noexcept
{
    for (const RecordedCellHitbox& Hitbox : LastColourHitboxes)
    {
        if (Hitbox.EntryIndex == ColourIndex)
        {
            return Hitbox.Bounds;
        }
    }
    return {};
}

ColourCodex& ContentBrowserPanel::AccessColourCodex() noexcept
{
    return CodexLibrary;
}

const ColourCodex& ContentBrowserPanel::QueryColourCodex() const noexcept
{
    return CodexLibrary;
}

ThumbnailSheetRepository& ContentBrowserPanel::AccessThumbnailRepository() noexcept
{
    return ThumbnailRepository;
}

const ThumbnailSheetRepository& ContentBrowserPanel::QueryThumbnailRepository() const noexcept
{
    return ThumbnailRepository;
}

Threading::AssetStreamThread& ContentBrowserPanel::AccessStreamThread() noexcept
{
    return StreamChannelThread;
}

const Threading::AssetStreamThread& ContentBrowserPanel::QueryStreamThread() const noexcept
{
    return StreamChannelThread;
}

void ContentBrowserPanel::RenderBottomNotchHandle(PixelSpace& Canvas, Rectangle2D NotchBounds, bool Hovered) const noexcept
{
    const float Inset = NotchBounds.Width * 0.09f;
    const std::array<Point2D, 4> Contour = {{
        { NotchBounds.X + Inset, NotchBounds.Y },
        { NotchBounds.Right() - Inset, NotchBounds.Y },
        { NotchBounds.Right(), NotchBounds.Bottom() },
        { NotchBounds.X, NotchBounds.Bottom() },
    }};
    Canvas.FillPolygon(Contour, Hovered ? ColorRgba{ 44u, 42u, 38u, 255u } : ColorRgba{ 28u, 28u, 30u, 255u });
    Canvas.StrokePolyline(Contour, ColorRgba{ 255u, 255u, 255u, 34u }, 1.0f, true);
    Canvas.TextCentered(
        NotchBounds,
        10.0f,
        Hovered ? ColorRgba{ 238u, 228u, 212u, 255u } : ColorRgba{ 182u, 178u, 170u, 255u },
        "ContentBrowser");
}

void ContentBrowserPanel::RenderColourSlotHighlights(PixelSpace& Canvas, Point2D Cursor) const noexcept
{
    if (!DraggingColour)
    {
        return;
    }
    for (const ColourSlotRegistration& Slot : RegisteredSlots)
    {
        if (Slot.ScreenBounds.Width <= 1.0f || Slot.ScreenBounds.Height <= 1.0f)
        {
            continue;
        }
        const bool Hovered = Slot.ScreenBounds.Contains(Cursor);
        Canvas.StrokeRectangle(
            Slot.ScreenBounds.Inset(-3.0f),
            Hovered ? ColorRgba{ 246u, 198u, 108u, 255u } : ColorRgba{ 226u, 200u, 155u, 170u },
            8.0f,
            Hovered ? 2.2f : 1.3f);
    }
}

void ContentBrowserPanel::RenderColoursRepository(
    PixelSpace&                       Canvas,
    Rectangle2D                       RepositoryArea,
    const ContentBrowserPointerInput& PointerInput)
{
    LastColourHitboxes.clear();

    const Point2D Cursor{ PointerInput.CursorX, PointerInput.CursorY };
    std::vector<std::size_t> FilteredIndices;
    const auto AllEntries = CodexLibrary.QueryEntries();
    for (const std::size_t Idx : CodexLibrary.QueryFilteredIndices(ColourFilter, ColourSort, SearchBuffer))
    {
        const ColourCodexEntry& Entry = AllEntries[Idx];
        if (OriginScope == ContentOriginFilter::Engine && !Entry.ReadOnlyEngineEntry)
        {
            continue;
        }
        // 📝 Project origin searches both Project colours AND EngineContent stock colours
        if (OriginScope == ContentOriginFilter::Imported && Entry.ReadOnlyEngineEntry)
        {
            continue;
        }
        FilteredIndices.push_back(Idx);
    }
    LastVisibleCount = static_cast<uint32_t>(FilteredIndices.size());

    if (FilteredIndices.empty())
    {
        Canvas.TextCentered(RepositoryArea, 13.0f, ColorRgba{ 142u, 142u, 146u, 255u }, "No .ColourSwatch entries match active filter");
        return;
    }

    if (ColourLayout == ColourRepositoryLayout::VerticalStrips)
    {
        Canvas.FillRectangle(RepositoryArea, ColorRgba{ 20u, 20u, 22u, 255u }, 14.0f);
        Canvas.PushClip(RepositoryArea);

        const float StripCount = static_cast<float>(FilteredIndices.size());
        const float StripW     = std::max(44.0f, RepositoryArea.Width / StripCount);

        for (std::size_t OrderIdx = 0u; OrderIdx < FilteredIndices.size(); ++OrderIdx)
        {
            const std::size_t       EntryIdx = FilteredIndices[OrderIdx];
            const ColourCodexEntry& Entry    = AllEntries[EntryIdx];
            const float             LeftX    = RepositoryArea.X + std::floor(static_cast<float>(OrderIdx) * StripW);
            const float             RightX   = (OrderIdx + 1u == FilteredIndices.size())
                                                   ? RepositoryArea.Right()
                                                   : (RepositoryArea.X + std::floor(static_cast<float>(OrderIdx + 1u) * StripW));
            const Rectangle2D StripRect{ LeftX, RepositoryArea.Y, std::max(1.0f, RightX - LeftX), RepositoryArea.Height };

            Canvas.FillRectangle(StripRect, Entry.FillColour, 0.0f);
            const ColourContrastInk Ink = ColourCodex::QueryContrastInk(Entry.FillColour);

            if (EntryIdx == SelectedColourIndex)
            {
                Canvas.FillCircle({ StripRect.Center().X, StripRect.Y + 16.0f }, 4.0f, Ink.PrimaryInk);
            }

            const Point2D TextExtent = Canvas.MeasureText(14.5f, Entry.HexCompact.c_str());
            const Point2D RotatedAnchor{
                StripRect.X + (StripRect.Width - TextExtent.Y) * 0.5f,
                StripRect.Bottom() - 18.0f,
            };
            Canvas.TextVerticalUp(RotatedAnchor, 14.5f, Ink.PrimaryInk, Entry.HexCompact.c_str());

            LastColourHitboxes.push_back({ EntryIdx, StripRect });

            if (RepositoryArea.Contains(Cursor) && StripRect.Contains(Cursor))
            {
                if (PointerInput.DoubleClicked)
                {
                    SelectedColourIndex = EntryIdx;
                    (void)ApplyColourIndexToSlot(EntryIdx);
                }
                else if (PointerInput.Pressed)
                {
                    SelectedColourIndex = EntryIdx;
                    PotentialColourDrag = true;
                    DraggedColourIndex  = EntryIdx;
                    DragPressOrigin     = Cursor;
                }
            }
        }

        const ColourContrastInk FirstInk = ColourCodex::QueryContrastInk(AllEntries[FilteredIndices.front()].FillColour);
        const char* BannerTitle =
            (ColourFilter == ColourFilterGroup::All) ? "ColourCodex Spectrum" : ColourCodex::QueryFilterGroupTitle(ColourFilter);
        Canvas.Text({ RepositoryArea.X + 24.0f, RepositoryArea.Y + 22.0f }, 14.0f, FirstInk.PrimaryInk, BannerTitle);

        Canvas.PopClip();
        Canvas.StrokeRectangle(RepositoryArea, ColorRgba{ 255u, 255u, 255u, 26u }, 14.0f, 1.0f);
        return;
    }

    if (ColourLayout == ColourRepositoryLayout::HorizontalStrips)
    {
        Canvas.FillRectangle(RepositoryArea, ColorRgba{ 20u, 20u, 22u, 255u }, 14.0f);
        Canvas.PushClip(RepositoryArea);

        if (RepositoryArea.Contains(Cursor) && std::fabs(PointerInput.WheelDeltaY) > 1.0e-4f)
        {
            RepositoryScrollY = std::max(0.0f, RepositoryScrollY - PointerInput.WheelDeltaY * 36.0f);
        }

        const float BandH     = std::max(40.0f, RepositoryArea.Height / std::min<float>(10.0f, static_cast<float>(FilteredIndices.size())));
        const float MaxScroll = std::max(0.0f, BandH * static_cast<float>(FilteredIndices.size()) - RepositoryArea.Height);
        RepositoryScrollY     = std::min(RepositoryScrollY, MaxScroll);

        for (std::size_t OrderIdx = 0u; OrderIdx < FilteredIndices.size(); ++OrderIdx)
        {
            const std::size_t       EntryIdx = FilteredIndices[OrderIdx];
            const ColourCodexEntry& Entry    = AllEntries[EntryIdx];
            const float             BandY    = RepositoryArea.Y + static_cast<float>(OrderIdx) * BandH - RepositoryScrollY;
            const Rectangle2D       BandRect{ RepositoryArea.X, BandY, RepositoryArea.Width, std::ceil(BandH) };
            if (BandRect.Bottom() < RepositoryArea.Y || BandRect.Y > RepositoryArea.Bottom())
            {
                continue;
            }

            Canvas.FillRectangle(BandRect, Entry.FillColour, 0.0f);
            const ColourContrastInk Ink = ColourCodex::QueryContrastInk(Entry.FillColour);

            if (EntryIdx == SelectedColourIndex)
            {
                Canvas.FillRectangle({ BandRect.X, BandRect.Y, 5.0f, BandRect.Height }, Ink.PrimaryInk, 0.0f);
            }

            Canvas.Text(
                { BandRect.X + 20.0f, BandRect.Y + (BandRect.Height - 14.0f) * 0.5f },
                13.0f,
                Ink.PrimaryInk,
                Entry.AliasName.c_str());

            const std::string RightSpec = Entry.HexCompact + "   RGB " + ColourCodex::FormatRgbTriplet(Entry.FillColour);
            Canvas.TextRightAligned(
                { BandRect.X, BandRect.Y, BandRect.Width - 20.0f, BandRect.Height },
                12.5f,
                Ink.PrimaryInk,
                RightSpec.c_str());

            LastColourHitboxes.push_back({ EntryIdx, BandRect });

            if (RepositoryArea.Contains(Cursor) && BandRect.Contains(Cursor))
            {
                if (PointerInput.DoubleClicked)
                {
                    SelectedColourIndex = EntryIdx;
                    (void)ApplyColourIndexToSlot(EntryIdx);
                }
                else if (PointerInput.Pressed)
                {
                    SelectedColourIndex = EntryIdx;
                    PotentialColourDrag = true;
                    DraggedColourIndex  = EntryIdx;
                    DragPressOrigin     = Cursor;
                }
            }
        }

        Canvas.PopClip();
        Canvas.StrokeRectangle(RepositoryArea, ColorRgba{ 255u, 255u, 255u, 26u }, 14.0f, 1.0f);
        return;
    }

    // GridCircles: Multi-column flat circular .ColourSwatch cells
    Canvas.PushClip(RepositoryArea);
    if (RepositoryArea.Contains(Cursor) && std::fabs(PointerInput.WheelDeltaY) > 1.0e-4f)
    {
        RepositoryScrollY = std::max(0.0f, RepositoryScrollY - PointerInput.WheelDeltaY * 38.0f);
    }

    constexpr float CellGap  = 10.0f;
    constexpr float CellH    = 128.0f;
    const uint32_t  Columns  = std::max(1u, static_cast<uint32_t>((RepositoryArea.Width + CellGap) / 124.0f));
    const float     CellW    = (RepositoryArea.Width - CellGap * static_cast<float>(Columns - 1u)) / static_cast<float>(Columns);
    const uint32_t  RowCount = static_cast<uint32_t>((FilteredIndices.size() + Columns - 1u) / Columns);
    const float     TotalH   = static_cast<float>(RowCount) * (CellH + CellGap);
    RepositoryScrollY        = std::min(RepositoryScrollY, std::max(0.0f, TotalH - RepositoryArea.Height));

    for (std::size_t OrderIdx = 0u; OrderIdx < FilteredIndices.size(); ++OrderIdx)
    {
        const std::size_t       EntryIdx = FilteredIndices[OrderIdx];
        const ColourCodexEntry& Entry    = AllEntries[EntryIdx];
        const uint32_t          Col      = static_cast<uint32_t>(OrderIdx) % Columns;
        const uint32_t          Row      = static_cast<uint32_t>(OrderIdx) / Columns;
        const Rectangle2D       CellRect{
            RepositoryArea.X + static_cast<float>(Col) * (CellW + CellGap),
            RepositoryArea.Y + static_cast<float>(Row) * (CellH + CellGap) - RepositoryScrollY,
            CellW,
            CellH,
        };
        if (CellRect.Bottom() < RepositoryArea.Y || CellRect.Y > RepositoryArea.Bottom())
        {
            continue;
        }

        const bool Hovered  = RepositoryArea.Contains(Cursor) && CellRect.Contains(Cursor);
        const bool Selected = (EntryIdx == SelectedColourIndex);
        Canvas.FillRectangle(
            CellRect,
            Selected ? ColorRgba{ 50u, 45u, 38u, 255u } : (Hovered ? ColorRgba{ 40u, 40u, 43u, 255u } : ColorRgba{ 32u, 32u, 35u, 255u }),
            15.0f);
        Canvas.StrokeRectangle(
            CellRect,
            Selected ? ColorRgba{ 214u, 192u, 148u, 150u } : ColorRgba{ 255u, 255u, 255u, 20u },
            15.0f,
            1.0f);

        const Point2D CircleCenter{ CellRect.Center().X, CellRect.Y + 46.0f };
        Canvas.FillCircle(CircleCenter, 26.0f, Entry.FillColour);
        Canvas.StrokeCircle(CircleCenter, 26.0f, ColorRgba{ 255u, 255u, 255u, 38u }, 1.0f);

        Canvas.TextCentered({ CellRect.X + 4.0f, CellRect.Y + 84.0f, CellRect.Width - 8.0f, 16.0f }, 10.5f, ColorRgba{ 224u, 224u, 226u, 255u }, Entry.AliasName.c_str());
        Canvas.TextCentered({ CellRect.X + 4.0f, CellRect.Y + 102.0f, CellRect.Width - 8.0f, 14.0f }, 9.0f, ColorRgba{ 132u, 132u, 136u, 255u }, Entry.HexUpper.c_str());

        LastColourHitboxes.push_back({ EntryIdx, CellRect });

        if (Hovered)
        {
            if (PointerInput.DoubleClicked)
            {
                SelectedColourIndex = EntryIdx;
                (void)ApplyColourIndexToSlot(EntryIdx);
            }
            else if (PointerInput.Pressed)
            {
                SelectedColourIndex = EntryIdx;
                PotentialColourDrag = true;
                DraggedColourIndex  = EntryIdx;
                DragPressOrigin     = Cursor;
            }
        }
    }
    Canvas.PopClip();
}

void ContentBrowserPanel::RenderNonColoursRepository(
    PixelSpace&                       Canvas,
    Rectangle2D                       RepositoryArea,
    const ContentBrowserPointerInput& PointerInput)
{
    LastColourHitboxes.clear();

    const std::vector<std::size_t> Filtered =
        ThumbnailRepository.QueryFilteredEntries(ActiveWorkspace, Category, OriginScope, SearchBuffer);
    LastVisibleCount = static_cast<uint32_t>(Filtered.size());

    if (Filtered.empty())
    {
        Canvas.TextCentered(RepositoryArea, 12.5f, ColorRgba{ 142u, 142u, 146u, 255u }, "No compatible entries in active origin/workspace");
        return;
    }

    // Stream any non-resident ThumbnailSheet tiles via SSD/HDD AssetStreamThread
    ThumbnailRepository.RequestVisibleTileStreaming(Filtered, StreamChannelThread);

    const Point2D Cursor{ PointerInput.CursorX, PointerInput.CursorY };
    Canvas.PushClip(RepositoryArea);
    if (RepositoryArea.Contains(Cursor) && std::fabs(PointerInput.WheelDeltaY) > 1.0e-4f)
    {
        RepositoryScrollY = std::max(0.0f, RepositoryScrollY - PointerInput.WheelDeltaY * 38.0f);
    }

    if (StandardLayout == StandardRepositoryLayout::List)
    {
        const float MaxScroll = std::max(0.0f, static_cast<float>(Filtered.size()) * 54.0f - RepositoryArea.Height);
        RepositoryScrollY     = std::min(RepositoryScrollY, MaxScroll);

        for (std::size_t OrderIdx = 0u; OrderIdx < Filtered.size(); ++OrderIdx)
        {
            const std::size_t             EntryIdx = Filtered[OrderIdx];
            const ContentRepositoryEntry* Entry    = ThumbnailRepository.QueryEntryAt(EntryIdx);
            if (Entry == nullptr)
            {
                continue;
            }
            const Rectangle2D RowRect{ RepositoryArea.X, RepositoryArea.Y + static_cast<float>(OrderIdx) * 54.0f - RepositoryScrollY, RepositoryArea.Width, 48.0f };
            if (RowRect.Bottom() < RepositoryArea.Y || RowRect.Y > RepositoryArea.Bottom())
            {
                continue;
            }
            const bool Selected = (SelectedContentIndex == EntryIdx);
            Canvas.FillRectangle(RowRect, Selected ? ColorRgba{ 48u, 44u, 38u, 255u } : ColorRgba{ 31u, 31u, 34u, 255u }, 12.0f);

            const ThumbnailUvQuad TileUv = ThumbnailRepository.QueryTileUv(EntryIdx);
            const Rectangle2D     IconBox{ RowRect.X + 8.0f, RowRect.Y + 6.0f, 36.0f, 36.0f };
            if (TileUv.Valid)
            {
                Canvas.FillTexturedRectangle(IconBox, TileUv.TexturePointer, TileUv.UvMin, TileUv.UvMax, 8.0f);
            }

            Canvas.Text({ RowRect.X + 54.0f, RowRect.Y + 11.0f }, 11.5f, ColorRgba{ 226u, 226u, 228u, 255u }, Entry->DisplayTitle.c_str());
            Canvas.Text({ RowRect.X + 54.0f, RowRect.Y + 27.0f }, 9.0f, ColorRgba{ 138u, 138u, 142u, 255u }, Entry->ContainerFormat.c_str());

            if (RepositoryArea.Contains(Cursor) && RowRect.Contains(Cursor) && PointerInput.Pressed)
            {
                SelectedContentIndex = EntryIdx;
            }
        }
    }
    else
    {
        constexpr float CellGap  = 10.0f;
        constexpr float CellH    = 134.0f;
        const uint32_t  Columns  = std::max(1u, static_cast<uint32_t>((RepositoryArea.Width + CellGap) / 136.0f));
        const float     CellW    = (RepositoryArea.Width - CellGap * static_cast<float>(Columns - 1u)) / static_cast<float>(Columns);
        const uint32_t  RowCount = static_cast<uint32_t>((Filtered.size() + Columns - 1u) / Columns);
        RepositoryScrollY        = std::min(RepositoryScrollY, std::max(0.0f, static_cast<float>(RowCount) * (CellH + CellGap) - RepositoryArea.Height));

        for (std::size_t OrderIdx = 0u; OrderIdx < Filtered.size(); ++OrderIdx)
        {
            const std::size_t             EntryIdx = Filtered[OrderIdx];
            const ContentRepositoryEntry* Entry    = ThumbnailRepository.QueryEntryAt(EntryIdx);
            if (Entry == nullptr)
            {
                continue;
            }
            const uint32_t    Col = static_cast<uint32_t>(OrderIdx) % Columns;
            const uint32_t    Row = static_cast<uint32_t>(OrderIdx) / Columns;
            const Rectangle2D CellRect{
                RepositoryArea.X + static_cast<float>(Col) * (CellW + CellGap),
                RepositoryArea.Y + static_cast<float>(Row) * (CellH + CellGap) - RepositoryScrollY,
                CellW,
                CellH,
            };
            if (CellRect.Bottom() < RepositoryArea.Y || CellRect.Y > RepositoryArea.Bottom())
            {
                continue;
            }

            const bool Selected = (SelectedContentIndex == EntryIdx);
            const bool Hovered  = RepositoryArea.Contains(Cursor) && CellRect.Contains(Cursor);
            Canvas.FillRectangle(
                CellRect,
                Selected ? ColorRgba{ 48u, 44u, 38u, 255u } : (Hovered ? ColorRgba{ 38u, 38u, 42u, 255u } : ColorRgba{ 31u, 31u, 34u, 255u }),
                15.0f);
            Canvas.StrokeRectangle(
                CellRect,
                Selected ? ColorRgba{ 214u, 192u, 148u, 150u } : ColorRgba{ 255u, 255u, 255u, 20u },
                15.0f,
                1.0f);

            // Single textured quad sampled directly from pre-baked 1024x1024 ThumbnailSheet page
            const ThumbnailUvQuad TileUv = ThumbnailRepository.QueryTileUv(EntryIdx);
            const Rectangle2D     ThumbRect{ CellRect.Center().X - 31.0f, CellRect.Y + 12.0f, 62.0f, 62.0f };
            if (TileUv.Valid)
            {
                Canvas.FillTexturedRectangle(ThumbRect, TileUv.TexturePointer, TileUv.UvMin, TileUv.UvMax, 12.0f);
            }

            Canvas.TextCentered({ CellRect.X + 5.0f, CellRect.Y + 82.0f, CellRect.Width - 10.0f, 16.0f }, 10.0f, ColorRgba{ 226u, 226u, 228u, 255u }, Entry->DisplayTitle.c_str());

            const auto EntryCat    = static_cast<ContentCategoryKind>(Entry->IndexHeader.CategoryCode);
            const auto EntryOrigin = static_cast<ContentOriginFilter>(Entry->IndexHeader.OriginCode);
            const char* OriginTag  = (EntryOrigin == ContentOriginFilter::Engine) ? "Engine" : ((EntryOrigin == ContentOriginFilter::Imported) ? "Imported" : "Project");
            const std::string Sub  = std::string(ThumbnailSheetRepository::QueryCategoryTitle(EntryCat)) + " · " + OriginTag;
            Canvas.TextCentered({ CellRect.X + 5.0f, CellRect.Y + 102.0f, CellRect.Width - 10.0f, 14.0f }, 8.8f, ColorRgba{ 132u, 132u, 136u, 255u }, Sub.c_str());

            if (Hovered && PointerInput.Pressed)
            {
                SelectedContentIndex = EntryIdx;
            }
        }
    }
    Canvas.PopClip();
}

void ContentBrowserPanel::RenderContentDetailsSidebar(
    PixelSpace&                       Canvas,
    Rectangle2D                       SidebarArea,
    const ContentBrowserPointerInput& PointerInput)
{
    Canvas.StrokeLine({ SidebarArea.X, SidebarArea.Y }, { SidebarArea.X, SidebarArea.Bottom() }, ColorRgba{ 255u, 255u, 255u, 18u }, 1.0f);
    const Rectangle2D Inner{ SidebarArea.X + 18.0f, SidebarArea.Y + 6.0f, std::max(80.0f, SidebarArea.Width - 18.0f), SidebarArea.Height - 12.0f };
    Canvas.PushClip(Inner);

    Canvas.Text({ Inner.X, Inner.Y }, 9.0f, ColorRgba{ 162u, 150u, 132u, 255u }, "CONTENT DETAILS");

    if (Category == ContentCategoryKind::Colours)
    {
        if (const ColourCodexEntry* Entry = CodexLibrary.QueryEntryAt(SelectedColourIndex))
        {
            const float             TileH = std::clamp(Inner.Height - 134.0f, 174.0f, 268.0f);
            const Rectangle2D       TileRect{ Inner.X, Inner.Y + 22.0f, Inner.Width, TileH };
            const ColourContrastInk Ink = ColourCodex::QueryContrastInk(Entry->FillColour);

            Canvas.FillRectangle(TileRect, Entry->FillColour, 20.0f);
            Canvas.StrokeRectangle(TileRect, Ink.BorderInk, 20.0f, 1.0f);

            const auto [HeadingLine1, HeadingLine2] = SplitUppercaseTileHeading(Entry->AliasName);
            Canvas.Text({ TileRect.X + 20.0f, TileRect.Y + 20.0f }, 22.0f, Ink.PrimaryInk, HeadingLine1.c_str());
            if (!HeadingLine2.empty())
            {
                Canvas.Text({ TileRect.X + 20.0f, TileRect.Y + 46.0f }, 22.0f, Ink.PrimaryInk, HeadingLine2.c_str());
            }

            const std::string HexLine  = "HEX: " + Entry->HexUpper;
            const std::string RgbLine  = "RGB: " + ColourCodex::FormatRgbTriplet(Entry->FillColour);
            const std::string CmykLine = "CMYK: " + ColourCodex::FormatCmykQuadruplet(Entry->CmykPercent);

            Canvas.Text({ TileRect.X + 20.0f, TileRect.Bottom() - 68.0f }, 12.0f, Ink.PrimaryInk, HexLine.c_str());
            Canvas.Text({ TileRect.X + 20.0f, TileRect.Bottom() - 48.0f }, 12.0f, Ink.PrimaryInk, RgbLine.c_str());
            Canvas.Text({ TileRect.X + 20.0f, TileRect.Bottom() - 28.0f }, 12.0f, Ink.PrimaryInk, CmykLine.c_str());

            const Point2D Cursor{ PointerInput.CursorX, PointerInput.CursorY };
            if (TileRect.Contains(Cursor))
            {
                if (PointerInput.DoubleClicked)
                {
                    (void)ApplyColourIndexToSlot(SelectedColourIndex);
                }
                else if (PointerInput.Pressed)
                {
                    PotentialColourDrag = true;
                    DraggedColourIndex  = SelectedColourIndex;
                    DragPressOrigin     = Cursor;
                }
            }

            std::string TargetLabel = "Active colour slot";
            for (const ColourSlotRegistration& Slot : RegisteredSlots)
            {
                if (Slot.SlotIdentifier == FocusedSlotId)
                {
                    TargetLabel = Slot.DisplayTitle;
                    break;
                }
            }
            if (TargetLabel == "Active colour slot" && !RegisteredSlots.empty())
            {
                TargetLabel = RegisteredSlots.front().DisplayTitle;
            }

            const std::string TargetSummary = "Target: " + TargetLabel + "  ·  .ColourSwatch";
            Canvas.Text({ Inner.X, TileRect.Bottom() + 12.0f }, 9.8f, ColorRgba{ 156u, 156u, 160u, 255u }, TargetSummary.c_str());

            const std::string ApplyBtnText = "Apply to " + TargetLabel;
            if (ConstructCapsuleControl(
                    Canvas,
                    { Inner.X, TileRect.Bottom() + 34.0f, Inner.Width, 30.0f },
                    ApplyBtnText.c_str(),
                    false,
                    PointerInput,
                    10.5f,
                    true))
            {
                (void)ApplyColourIndexToSlot(SelectedColourIndex);
            }
        }
    }
    else
    {
        if (const ContentRepositoryEntry* Entry = ThumbnailRepository.QueryEntryAt(SelectedContentIndex))
        {
            Canvas.Text({ Inner.X, Inner.Y + 24.0f }, 16.0f, ColorRgba{ 230u, 230u, 232u, 255u }, Entry->DisplayTitle.c_str());

            const Rectangle2D PreviewBox{ Inner.X, Inner.Y + 52.0f, Inner.Width, 148.0f };
            Canvas.FillRectangle(PreviewBox, ColorRgba{ 26u, 26u, 29u, 255u }, 16.0f);
            Canvas.StrokeRectangle(PreviewBox, ColorRgba{ 255u, 255u, 255u, 20u }, 16.0f, 1.0f);

            const ThumbnailUvQuad TileUv = ThumbnailRepository.QueryTileUv(SelectedContentIndex);
            if (TileUv.Valid)
            {
                const Rectangle2D BigThumb{ PreviewBox.Center().X - 54.0f, PreviewBox.Center().Y - 54.0f, 108.0f, 108.0f };
                Canvas.FillTexturedRectangle(BigThumb, TileUv.TexturePointer, TileUv.UvMin, TileUv.UvMax, 16.0f);
            }

            const auto EntryCat    = static_cast<ContentCategoryKind>(Entry->IndexHeader.CategoryCode);
            const auto EntryOrigin = static_cast<ContentOriginFilter>(Entry->IndexHeader.OriginCode);
            const char* OriginName = (EntryOrigin == ContentOriginFilter::Engine) ? "EngineContent (Read-Only)" : ((EntryOrigin == ContentOriginFilter::Imported) ? "Imported (.Space)" : "Project Content");

            const std::string FormatLine = "Container: " + Entry->ContainerFormat + "  (" + ThumbnailSheetRepository::QueryCategoryTitle(EntryCat) + ")";
            const std::string OriginLine = std::string("Origin: ") + OriginName;
            const std::string SheetLine  = "Sheet Page #" + std::to_string(Entry->IndexHeader.SheetPageOrdinal) +
                                           " · Tile (" + std::to_string(Entry->IndexHeader.TileColumn) + ", " +
                                           std::to_string(Entry->IndexHeader.TileRow) + ") · 64B Index";

            Canvas.Text({ Inner.X, PreviewBox.Bottom() + 14.0f }, 10.0f, ColorRgba{ 204u, 194u, 178u, 255u }, FormatLine.c_str());
            Canvas.Text({ Inner.X, PreviewBox.Bottom() + 32.0f }, 9.5f, ColorRgba{ 152u, 152u, 156u, 255u }, OriginLine.c_str());
            Canvas.Text({ Inner.X, PreviewBox.Bottom() + 49.0f }, 9.2f, ColorRgba{ 132u, 132u, 138u, 255u }, SheetLine.c_str());
        }
    }

    Canvas.PopClip();
}

void ContentBrowserPanel::RenderWithInput(
    PixelSpace&                       Canvas,
    Rectangle2D                       ViewportLogicalBounds,
    const ContentBrowserPointerInput& PointerInput,
    double                            DeltaSeconds)
{
    (void)ShelfSpring.Integrate(DeltaSeconds > 0.0 ? DeltaSeconds : (1.0 / 60.0));

    const Point2D Cursor{ PointerInput.CursorX, PointerInput.CursorY };

    if (PotentialColourDrag && PointerInput.HeldDown && !DraggingColour)
    {
        const float Dx = Cursor.X - DragPressOrigin.X;
        const float Dy = Cursor.Y - DragPressOrigin.Y;
        if (std::hypot(Dx, Dy) > 5.0f)
        {
            DraggingColour = true;
            if (TargetExtent == ContentShelfExtent::FullHeight)
            {
                ConfigureShelfExtent(ContentShelfExtent::SplitHalf, false);
            }
        }
    }

    if (PointerInput.Released)
    {
        if (DraggingColour)
        {
            for (const ColourSlotRegistration& Slot : RegisteredSlots)
            {
                if (Slot.ScreenBounds.Contains(Cursor))
                {
                    (void)ApplyColourIndexToSlot(DraggedColourIndex, Slot.SlotIdentifier);
                    break;
                }
            }
        }
        PotentialColourDrag = false;
        DraggingColour      = false;
    }

    if (PointerInput.Pressed && !QueryBlocksUnderlyingWorkspace())
    {
        for (const ColourSlotRegistration& Slot : RegisteredSlots)
        {
            if (Slot.ScreenBounds.Contains(Cursor))
            {
                FocusedSlotId = Slot.SlotIdentifier;
                break;
            }
        }
    }

    RenderColourSlotHighlights(Canvas, Cursor);

    const float       Progress    = std::clamp(static_cast<float>(ShelfSpring.Axis(ShelfAxis).Current), 0.0f, 1.0f);
    const float       DrawerH     = std::round(ViewportLogicalBounds.Height * Progress);
    const Rectangle2D NotchBounds = QueryNotchTabBounds(ViewportLogicalBounds);
    const bool        NotchHover  = NotchBounds.Contains(Cursor);

    if (NotchHover && PointerInput.Pressed)
    {
        ConfigureShelfExtent(
            TargetExtent == ContentShelfExtent::Closed ? ContentShelfExtent::FullHeight : ContentShelfExtent::Closed,
            false);
    }

    RenderBottomNotchHandle(Canvas, NotchBounds, NotchHover);

    if (DrawerH <= 2.0f)
    {
        if (DraggingColour)
        {
            if (const ColourCodexEntry* Dragged = CodexLibrary.QueryEntryAt(DraggedColourIndex))
            {
                Canvas.FillCircle(Cursor, 14.0f, Dragged->FillColour);
                Canvas.StrokeCircle(Cursor, 14.0f, ColorRgba{ 255u, 255u, 255u, 220u }, 2.0f);
            }
        }
        return;
    }

    const Rectangle2D DrawerRect{
        ViewportLogicalBounds.X,
        ViewportLogicalBounds.Bottom() - DrawerH,
        ViewportLogicalBounds.Width,
        DrawerH,
    };

    Canvas.PushClip(DrawerRect);
    Canvas.FillRectangle(DrawerRect, ColorRgba{ 22u, 22u, 24u, 252u }, 0.0f);
    Canvas.StrokeLine({ DrawerRect.X, DrawerRect.Y }, { DrawerRect.Right(), DrawerRect.Y }, ColorRgba{ 255u, 255u, 255u, 32u }, 1.0f);

    const float PadX = 24.0f;
    float       CurY = DrawerRect.Y + 14.0f;

    // 1. Header Bar + Workspace Domain Switcher + SSD/HDD Tier & Import Destination Toggles
    const std::string Eyebrow = std::string("CONTENT / LIBRARY  ·  ") +
                                ThumbnailSheetRepository::QueryWorkspaceTitle(ActiveWorkspace) + " WORKSPACE";
    Canvas.Text({ DrawerRect.X + PadX, CurY }, 8.5f, ColorRgba{ 162u, 150u, 132u, 255u }, Eyebrow.c_str());
    Canvas.Text({ DrawerRect.X + PadX, CurY + 12.0f }, 21.0f, ColorRgba{ 234u, 234u, 236u, 255u }, "ContentBrowser");

    // Workspace Domain Filter Pills (demonstrates strict per-workspace category blocking)
    constexpr std::array<std::pair<WorkspaceDomainKind, const char*>, 5> QuickWorkspaces = {{
        { WorkspaceDomainKind::SceneWorld, "Scene"      },
        { WorkspaceDomainKind::UV,         "UV"         },
        { WorkspaceDomainKind::Material,   "Material"   },
        { WorkspaceDomainKind::Simulation, "Simulation" },
        { WorkspaceDomainKind::Audio,      "Audio"      },
    }};
    float WsX = DrawerRect.X + 240.0f;
    for (const auto& [WsKind, WsLabel] : QuickWorkspaces)
    {
        const float CapsuleW = std::max(52.0f, Canvas.MeasureTextWidth(9.2f, WsLabel) + 20.0f);
        if (ConstructCapsuleControl(Canvas, { WsX, CurY + 6.0f, CapsuleW, 25.0f }, WsLabel, ActiveWorkspace == WsKind, PointerInput, 9.2f))
        {
            ConfigureWorkspaceDomain(WsKind);
        }
        WsX += CapsuleW + 5.0f;
    }

    float RightBtnX = DrawerRect.Right() - PadX;
    RightBtnX -= 32.0f;
    if (ConstructCapsuleControl(Canvas, { RightBtnX, CurY + 4.0f, 32.0f, 28.0f }, "x", false, PointerInput))
    {
        ConfigureShelfExtent(ContentShelfExtent::Closed, false);
    }

    RightBtnX -= 68.0f;
    const bool InSplit = (TargetExtent == ContentShelfExtent::SplitHalf);
    if (ConstructCapsuleControl(Canvas, { RightBtnX, CurY + 4.0f, 62.0f, 28.0f }, InSplit ? "Full" : "Split", InSplit, PointerInput))
    {
        ConfigureShelfExtent(InSplit ? ContentShelfExtent::FullHeight : ContentShelfExtent::SplitHalf, false);
    }

    RightBtnX -= 88.0f;
    if (ConstructCapsuleControl(Canvas, { RightBtnX, CurY + 4.0f, 82.0f, 28.0f }, "+ Colour", false, PointerInput))
    {
        if (ThumbnailSheetRepository::QueryCategoryPermitted(ActiveWorkspace, ContentCategoryKind::Colours))
        {
            const std::size_t Added = CodexLibrary.AddProjectColour("Project Swatch", "#2454FF", "Project");
            (void)ActivateCategory(ContentCategoryKind::Colours);
            ConfigureColourFilter(ColourFilterGroup::All);
            SelectedColourIndex = Added;
            StatusMessage       = "Created Project Swatch (.ColourSwatch #2454FF)";
        }
    }

    RightBtnX -= 106.0f;
    const bool ScratchMode = (ThumbnailRepository.QueryImportPolicy().Mode == ImportDestinationMode::ScratchTempFolder);
    if (ConstructCapsuleControl(
            Canvas,
            { RightBtnX, CurY + 4.0f, 100.0f, 28.0f },
            ScratchMode ? "Import: Temp" : "Import: Project",
            ScratchMode,
            PointerInput,
            9.0f))
    {
        ImportIntakeConfiguration NextPolicy = ThumbnailRepository.QueryImportPolicy();
        NextPolicy.Mode = ScratchMode ? ImportDestinationMode::ProjectImportedFolder : ImportDestinationMode::ScratchTempFolder;
        ThumbnailRepository.ConfigureImportPolicy(NextPolicy);
        StatusMessage = ScratchMode
                            ? "Import destination -> Projects/<Project>/Content/Imported/"
                            : "Import destination -> Scratch Temp (Projects/_Scratch/Saved/Imported/)";
    }

    RightBtnX -= 84.0f;
    const bool RotationalActive = (StreamChannelThread.QueryResolvedTier() == Threading::StorageHardwareTier::RotationalSequentialIo);
    if (ConstructCapsuleControl(
            Canvas,
            { RightBtnX, CurY + 4.0f, 78.0f, 28.0f },
            RotationalActive ? "I/O: HDD" : "I/O: SSD",
            RotationalActive,
            PointerInput,
            9.0f))
    {
        ConfigureStorageHardwareTier(
            RotationalActive ? Threading::StorageHardwareTier::FlashStorageRandomIo
                  : Threading::StorageHardwareTier::RotationalSequentialIo);
    }

    CurY += 44.0f;
    Canvas.StrokeLine({ DrawerRect.X + PadX, CurY }, { DrawerRect.Right() - PadX, CurY }, ColorRgba{ 255u, 255u, 255u, 16u }, 1.0f);
    CurY += 10.0f;

    // 2. Origin Scope Tabs + Layout Switcher
    constexpr std::array<std::pair<ContentOriginFilter, const char*>, 4> Origins = {{
        { ContentOriginFilter::All,      "All"      },
        { ContentOriginFilter::Project,  "Project"  },
        { ContentOriginFilter::Imported, "Imported" },
        { ContentOriginFilter::Engine,   "Engine"   },
    }};
    float OriginX = DrawerRect.X + PadX;
    for (const auto& [ScopeEnum, Label] : Origins)
    {
        if (ConstructCapsuleControl(Canvas, { OriginX, CurY, 72.0f, 26.0f }, Label, OriginScope == ScopeEnum, PointerInput, 9.5f))
        {
            ConfigureOriginFilter(ScopeEnum);
        }
        OriginX += 78.0f;
    }

    if (Category == ContentCategoryKind::Colours)
    {
        float LayoutX = DrawerRect.Right() - PadX - 236.0f;
        if (ConstructCapsuleControl(Canvas, { LayoutX, CurY, 66.0f, 26.0f }, "Grid", ColourLayout == ColourRepositoryLayout::GridCircles, PointerInput, 9.5f))
        {
            ConfigureColourLayout(ColourRepositoryLayout::GridCircles);
        }
        LayoutX += 72.0f;
        if (ConstructCapsuleControl(Canvas, { LayoutX, CurY, 78.0f, 26.0f }, "Vertical", ColourLayout == ColourRepositoryLayout::VerticalStrips, PointerInput, 9.5f))
        {
            ConfigureColourLayout(ColourRepositoryLayout::VerticalStrips);
        }
        LayoutX += 84.0f;
        if (ConstructCapsuleControl(Canvas, { LayoutX, CurY, 80.0f, 26.0f }, "Horizontal", ColourLayout == ColourRepositoryLayout::HorizontalStrips, PointerInput, 9.5f))
        {
            ConfigureColourLayout(ColourRepositoryLayout::HorizontalStrips);
        }
    }
    else
    {
        float LayoutX = DrawerRect.Right() - PadX - 134.0f;
        if (ConstructCapsuleControl(Canvas, { LayoutX, CurY, 64.0f, 26.0f }, "Grid", StandardLayout == StandardRepositoryLayout::Grid, PointerInput, 9.5f))
        {
            ConfigureStandardLayout(StandardRepositoryLayout::Grid);
        }
        LayoutX += 70.0f;
        if (ConstructCapsuleControl(Canvas, { LayoutX, CurY, 64.0f, 26.0f }, "List", StandardLayout == StandardRepositoryLayout::List, PointerInput, 9.5f))
        {
            ConfigureStandardLayout(StandardRepositoryLayout::List);
        }
    }

    CurY += 34.0f;

    // 3. Dedicated Colour Filters Bar (ONLY visible when Colours category is active)
    if (Category == ContentCategoryKind::Colours)
    {
        Canvas.Text({ DrawerRect.X + PadX, CurY + 7.0f }, 9.0f, ColorRgba{ 168u, 154u, 132u, 255u }, "COLOUR FILTERS");
        float ChipX = DrawerRect.X + PadX + 104.0f;

        constexpr std::array<ColourFilterGroup, 8> FilterOrder = {{
            ColourFilterGroup::All,
            ColourFilterGroup::AugustBeigeAndBrown,
            ColourFilterGroup::RedsAndBerries,
            ColourFilterGroup::Yellows,
            ColourFilterGroup::Greens,
            ColourFilterGroup::Blues,
            ColourFilterGroup::Purples,
            ColourFilterGroup::Neutrals,
        }};

        for (const ColourFilterGroup Group : FilterOrder)
        {
            const char* Title = ColourCodex::QueryFilterGroupTitle(Group);
            const float BtnW  = std::max(54.0f, Canvas.MeasureTextWidth(9.5f, Title) + 22.0f);
            if (ChipX + BtnW > DrawerRect.Right() - PadX - 122.0f)
            {
                break;
            }
            if (ConstructCapsuleControl(Canvas, { ChipX, CurY, BtnW, 25.0f }, Title, ColourFilter == Group, PointerInput, 9.5f))
            {
                ConfigureColourFilter(Group);
            }
            ChipX += BtnW + 6.0f;
        }

        const std::string SortCaption = std::string("Sort: ") + ColourCodex::QuerySortOrderTitle(ColourSort);
        if (ConstructCapsuleControl(
                Canvas,
                { DrawerRect.Right() - PadX - 114.0f, CurY, 114.0f, 25.0f },
                SortCaption.c_str(),
                false,
                PointerInput,
                9.5f))
        {
            const uint8_t NextSort = (static_cast<uint8_t>(ColourSort) + 1u) % 3u;
            ConfigureColourSort(static_cast<ColourSortOrder>(NextSort));
        }

        CurY += 33.0f;
    }

    Canvas.StrokeLine({ DrawerRect.X + PadX, CurY }, { DrawerRect.Right() - PadX, CurY }, ColorRgba{ 255u, 255u, 255u, 14u }, 1.0f);
    CurY += 10.0f;

    // 4. Three-Column Body: Left Permitted-Only Category Rail | Centre Repository | Right Content Details Tile
    constexpr float FooterH  = 32.0f;
    const float     BodyH    = std::max(60.0f, DrawerRect.Bottom() - FooterH - CurY);
    const float     RailW    = 148.0f;
    const float     DetailsW = 286.0f;
    const float     CentreW  = std::max(160.0f, DrawerRect.Width - PadX * 2.0f - RailW - DetailsW - 32.0f);

    const Rectangle2D RailArea{ DrawerRect.X + PadX, CurY, RailW, BodyH };
    const Rectangle2D CentreArea{ RailArea.Right() + 16.0f, CurY, CentreW, BodyH };
    const Rectangle2D DetailsArea{ CentreArea.Right() + 16.0f, CurY, DetailsW, BodyH };

    Canvas.PushClip(RailArea);
    float RailY = RailArea.Y + 2.0f;
    for (const ContentCategoryKind CategoryOption : OrderedRailCategories)
    {
        // 🔴 Completely exclude blocked categories in specialized workspaces (e.g. Particles/Simulations/Audio in UV)
        if (!ThumbnailSheetRepository::QueryCategoryPermitted(ActiveWorkspace, CategoryOption))
        {
            continue;
        }
        if (RailY + 26.0f > RailArea.Bottom())
        {
            break;
        }

        const Rectangle2D CatBtn{ RailArea.X, RailY, RailArea.Width - 10.0f, 25.0f };
        const bool        ActiveCat = (Category == CategoryOption);
        const bool        Hovered   = CatBtn.Contains(Cursor);
        if (ActiveCat || Hovered)
        {
            Canvas.FillRectangle(
                CatBtn,
                ActiveCat ? ColorRgba{ 45u, 42u, 37u, 255u } : ColorRgba{ 34u, 34u, 37u, 255u },
                8.0f);
        }
        Canvas.Text(
            { CatBtn.X + 10.0f, CatBtn.Y + 6.0f },
            10.2f,
            ActiveCat ? ColorRgba{ 234u, 224u, 206u, 255u } : ColorRgba{ 148u, 148u, 152u, 255u },
            ThumbnailSheetRepository::QueryCategoryTitle(CategoryOption));

        if (Hovered && PointerInput.Pressed)
        {
            (void)ActivateCategory(CategoryOption);
        }
        RailY += 27.0f;
    }
    Canvas.PopClip();
    Canvas.StrokeLine({ RailArea.Right(), RailArea.Y }, { RailArea.Right(), RailArea.Bottom() }, ColorRgba{ 255u, 255u, 255u, 18u }, 1.0f);

    if (Category == ContentCategoryKind::Colours)
    {
        RenderColoursRepository(Canvas, CentreArea, PointerInput);
    }
    else
    {
        RenderNonColoursRepository(Canvas, CentreArea, PointerInput);
    }

    RenderContentDetailsSidebar(Canvas, DetailsArea, PointerInput);

    // 5. Footer Status Bar
    const Rectangle2D FooterRect{ DrawerRect.X + PadX, DrawerRect.Bottom() - FooterH, DrawerRect.Width - PadX * 2.0f, FooterH };
    Canvas.StrokeLine({ FooterRect.X, FooterRect.Y }, { FooterRect.Right(), FooterRect.Y }, ColorRgba{ 255u, 255u, 255u, 14u }, 1.0f);

    const char* DefaultHint =
        (Category == ContentCategoryKind::Colours)
            ? "Drag any .ColourSwatch circle/strip onto a colour slot or double-click to apply"
            : "Pre-baked 1024x1024 ThumbnailSheet + 64B ContentIndex · Drop external files to quarantine & import";
    Canvas.Text(
        { FooterRect.X, FooterRect.Y + 9.0f },
        9.5f,
        ColorRgba{ 152u, 152u, 156u, 255u },
        StatusMessage.empty() ? DefaultHint : StatusMessage.c_str());

    const std::string RightMeta = std::string(Threading::AssetStreamThread::QueryHardwareTierTitle(StreamChannelThread.QueryResolvedTier())) +
                                  " · " + ThumbnailSheetRepository::QueryWorkspaceTitle(ActiveWorkspace) + " Mask";
    Canvas.TextRightAligned(FooterRect, 9.0f, ColorRgba{ 122u, 122u, 128u, 255u }, RightMeta.c_str());

    Canvas.PopClip();

    // Cursor-tracked colour marker while actively dragging onto workspace colour slots
    if (DraggingColour)
    {
        if (const ColourCodexEntry* Dragged = CodexLibrary.QueryEntryAt(DraggedColourIndex))
        {
            Canvas.FillCircle(Cursor, 15.0f, Dragged->FillColour);
            Canvas.StrokeCircle(Cursor, 15.0f, ColorRgba{ 255u, 255u, 255u, 230u }, 2.0f);
        }
    }
}

void ContentBrowserPanel::Render(Rectangle2D HostBoundsPhysical, double DeltaSeconds, float ScaleFactor)
{
    if (ImGui::GetCurrentContext() == nullptr || HostBoundsPhysical.Width <= 4.0f || HostBoundsPhysical.Height <= 4.0f)
    {
        return;
    }

    const float       SafeScale = std::clamp(ScaleFactor, 0.75f, 2.0f);
    ImDrawList*       FgList    = ImGui::GetForegroundDrawList();
    PixelSpace        Canvas(FgList, { HostBoundsPhysical.X, HostBoundsPhysical.Y }, SafeScale);
    const Rectangle2D LogicalBounds{
        0.0f,
        0.0f,
        HostBoundsPhysical.Width / SafeScale,
        HostBoundsPhysical.Height / SafeScale,
    };

    const ImGuiIO& Io = ImGui::GetIO();
    ContentBrowserPointerInput Pointer{};
    Pointer.CursorX       = (Io.MousePos.x - HostBoundsPhysical.X) / SafeScale;
    Pointer.CursorY       = (Io.MousePos.y - HostBoundsPhysical.Y) / SafeScale;
    Pointer.Pressed       = ImGui::IsMouseClicked(0);
    Pointer.HeldDown      = ImGui::IsMouseDown(0);
    Pointer.Released      = ImGui::IsMouseReleased(0);
    Pointer.DoubleClicked = ImGui::IsMouseDoubleClicked(0);
    Pointer.WheelDeltaY   = Io.MouseWheel;

    RenderWithInput(Canvas, LogicalBounds, Pointer, DeltaSeconds);
}

} // namespace Frontier
