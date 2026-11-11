/*============================================================================================================================================
                                                          CONTENTBROWSERPANEL.CPP
============================================================================================================================================*/
// 🗂️ Implementation of bottom-edge ContentBrowser drawer, ColourCodex Grid/Vertical/Horizontal views, Colour Filters, and colour-slot drops

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

struct BuiltInMaterialPreset
{
    const char* Title;
    ColorRgba   OrbColour;
};

constexpr std::array<BuiltInMaterialPreset, 9> MaterialPresets = {{
    { "Standard Preset",   { 199u, 173u, 160u, 255u } },
    { "Screen Reflection", { 164u, 182u, 204u, 255u } },
    { "Dielectric Glass",  { 196u, 224u, 238u, 255u } },
    { "Woven Fabric",      { 148u, 112u, 134u, 255u } },
    { "Brushed Metal",     { 188u, 192u, 198u, 255u } },
    { "Automotive Coat",   { 184u, 48u,  58u,  255u } },
    { "Translucent Wax",   { 218u, 186u, 152u, 255u } },
    { "Emissive Lamp",     { 246u, 196u, 92u,  255u } },
    { "Unlit Flat",        { 152u, 162u, 156u, 255u } },
}};

[[nodiscard]] float TargetRatioForExtent(ContentShelfExtent Extent) noexcept
{
    switch (Extent)
    {
        case ContentShelfExtent::Closed:
            return 0.0f;
        case ContentShelfExtent::SplitHalf:
            return 0.48f;
        case ContentShelfExtent::FullHeight:
            return 0.94f;
    }
    return 0.0f;
}

[[nodiscard]] const char* CategoryCaption(ContentCategoryKind Category) noexcept
{
    switch (Category)
    {
        case ContentCategoryKind::All:
            return "All";
        case ContentCategoryKind::Materials:
            return "Materials";
        case ContentCategoryKind::Colours:
            return "Colours";
        case ContentCategoryKind::Images:
            return "Images";
        case ContentCategoryKind::Fonts:
            return "Fonts";
        case ContentCategoryKind::Models:
            return "Models";
        case ContentCategoryKind::Icons:
            return "Icons";
        case ContentCategoryKind::Code:
            return "Code";
        case ContentCategoryKind::Other:
            return "Other";
    }
    return "All";
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
    float                             FontSize = 10.0f,
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

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           CONTENTBROWSERPANEL IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

ContentBrowserPanel::ContentBrowserPanel()
    : ShelfAxis(ShelfSpring.RegisterAxis(0.0, 240.0, 26.0))
{
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

void ContentBrowserPanel::ActivateCategory(ContentCategoryKind NextCategory) noexcept
{
    Category         = NextCategory;
    RepositoryScrollY = 0.0f;
}

void ContentBrowserPanel::ConfigureOriginFilter(ContentOriginFilter NextOrigin) noexcept
{
    OriginScope      = NextOrigin;
    RepositoryScrollY = 0.0f;
}

void ContentBrowserPanel::ConfigureStandardLayout(StandardRepositoryLayout NextLayout) noexcept
{
    StandardLayout = NextLayout;
}

void ContentBrowserPanel::ConfigureColourLayout(ColourRepositoryLayout NextLayout) noexcept
{
    ColourLayout     = NextLayout;
    RepositoryScrollY = 0.0f;
}

void ContentBrowserPanel::ConfigureColourFilter(ColourFilterGroup NextFilter) noexcept
{
    ColourFilter     = NextFilter;
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
    SearchBuffer     = std::string(SearchText);
    RepositoryScrollY = 0.0f;
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
                StatusMessage = "Applied " + Entry->AliasName + " (" + Entry->HexUpper + ") -> " + OutlinerEntry->Title;
                return true;
            }
        }
    }
    return false;
}

ContentBrowserReading ContentBrowserPanel::InspectReading() const noexcept
{
    ContentBrowserReading Reading{};
    Reading.ShelfExtent          = TargetExtent;
    Reading.ShelfProgress        = std::clamp(static_cast<float>(ShelfSpring.Axis(ShelfAxis).Current), 0.0f, 1.0f);
    Reading.ActiveCategory       = Category;
    Reading.ActiveColourLayout   = ColourLayout;
    Reading.ActiveStandardLayout = StandardLayout;
    Reading.ActiveColourFilter   = ColourFilter;
    Reading.ActiveColourSort     = ColourSort;
    Reading.VisibleEntryCount    = LastVisibleCount;
    Reading.ColourFiltersVisible = (Category == ContentCategoryKind::Colours);
    Reading.DraggingColourActive = DraggingColour;
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
        if (OriginScope == ContentOriginFilter::Project && Entry.ReadOnlyEngineEntry)
        {
            continue;
        }
        if (OriginScope == ContentOriginFilter::Imported && Entry.ReadOnlyEngineEntry)
        {
            continue;
        }
        FilteredIndices.push_back(Idx);
    }
    LastVisibleCount = static_cast<uint32_t>(FilteredIndices.size());

    if (FilteredIndices.empty())
    {
        Canvas.TextCentered(RepositoryArea, 13.0f, ColorRgba{ 142u, 142u, 146u, 255u }, "No colours match active Colour Filter");
        return;
    }

    if (ColourLayout == ColourRepositoryLayout::VerticalStrips)
    {
        // Flush edge-to-edge vertical spectrum strips (0 px gap) with rotated -90 deg HEX codes
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

        // Top-left spectrum group overlay label (matching "August Beige & Brown" reference banner)
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
        // Flush edge-to-edge horizontal spectrum bands stacked top-to-bottom (0 px gap)
        Canvas.FillRectangle(RepositoryArea, ColorRgba{ 20u, 20u, 22u, 255u }, 14.0f);
        Canvas.PushClip(RepositoryArea);

        if (RepositoryArea.Contains(Cursor) && std::fabs(PointerInput.WheelDeltaY) > 1.0e-4f)
        {
            RepositoryScrollY = std::max(0.0f, RepositoryScrollY - PointerInput.WheelDeltaY * 36.0f);
        }

        const float BandH = std::max(40.0f, RepositoryArea.Height / std::min<float>(10.0f, static_cast<float>(FilteredIndices.size())));
        const float MaxScroll = std::max(0.0f, BandH * static_cast<float>(FilteredIndices.size()) - RepositoryArea.Height);
        RepositoryScrollY = std::min(RepositoryScrollY, MaxScroll);

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

    // GridCircles: Multi-column flat circular colour swatches
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
    RepositoryScrollY         = std::min(RepositoryScrollY, std::max(0.0f, TotalH - RepositoryArea.Height));

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

        // Flat circular colour fill (no gradient/photo)
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
    LastVisibleCount = static_cast<uint32_t>(MaterialPresets.size());

    const Point2D Cursor{ PointerInput.CursorX, PointerInput.CursorY };
    Canvas.PushClip(RepositoryArea);

    if (StandardLayout == StandardRepositoryLayout::List)
    {
        for (std::size_t Idx = 0u; Idx < MaterialPresets.size(); ++Idx)
        {
            const Rectangle2D RowRect{ RepositoryArea.X, RepositoryArea.Y + static_cast<float>(Idx) * 52.0f, RepositoryArea.Width, 46.0f };
            const bool        Selected = (SelectedMaterialIndex == Idx);
            Canvas.FillRectangle(RowRect, Selected ? ColorRgba{ 48u, 44u, 38u, 255u } : ColorRgba{ 31u, 31u, 34u, 255u }, 12.0f);
            Canvas.FillCircle({ RowRect.X + 26.0f, RowRect.Center().Y }, 15.0f, MaterialPresets[Idx].OrbColour);
            Canvas.Text({ RowRect.X + 54.0f, RowRect.Y + 15.0f }, 11.5f, ColorRgba{ 222u, 222u, 224u, 255u }, MaterialPresets[Idx].Title);
            if (RowRect.Contains(Cursor) && PointerInput.Pressed)
            {
                SelectedMaterialIndex = static_cast<uint32_t>(Idx);
            }
        }
    }
    else
    {
        const uint32_t Columns = std::max(1u, static_cast<uint32_t>((RepositoryArea.Width + 10.0f) / 132.0f));
        const float    CellW   = (RepositoryArea.Width - 10.0f * static_cast<float>(Columns - 1u)) / static_cast<float>(Columns);
        for (std::size_t Idx = 0u; Idx < MaterialPresets.size(); ++Idx)
        {
            const uint32_t    Col = static_cast<uint32_t>(Idx) % Columns;
            const uint32_t    Row = static_cast<uint32_t>(Idx) / Columns;
            const Rectangle2D CellRect{ RepositoryArea.X + static_cast<float>(Col) * (CellW + 10.0f), RepositoryArea.Y + static_cast<float>(Row) * 136.0f, CellW, 126.0f };
            const bool        Selected = (SelectedMaterialIndex == Idx);
            Canvas.FillRectangle(CellRect, Selected ? ColorRgba{ 48u, 44u, 38u, 255u } : ColorRgba{ 31u, 31u, 34u, 255u }, 15.0f);
            Canvas.StrokeRectangle(CellRect, ColorRgba{ 255u, 255u, 255u, 20u }, 15.0f, 1.0f);
            const Point2D OrbCenter{ CellRect.Center().X, CellRect.Y + 46.0f };
            Canvas.FillCircle(OrbCenter, 26.0f, MaterialPresets[Idx].OrbColour);
            Canvas.FillCircle({ OrbCenter.X - 8.0f, OrbCenter.Y - 9.0f }, 6.0f, ColorRgba{ 255u, 255u, 255u, 90u });
            Canvas.TextCentered({ CellRect.X + 4.0f, CellRect.Y + 86.0f, CellRect.Width - 8.0f, 16.0f }, 10.5f, ColorRgba{ 222u, 222u, 224u, 255u }, MaterialPresets[Idx].Title);
            Canvas.TextCentered({ CellRect.X + 4.0f, CellRect.Y + 104.0f, CellRect.Width - 8.0f, 14.0f }, 9.0f, ColorRgba{ 126u, 126u, 130u, 255u }, "Materials · Engine");
            if (CellRect.Contains(Cursor) && PointerInput.Pressed)
            {
                SelectedMaterialIndex = static_cast<uint32_t>(Idx);
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
            const float             TileH = std::clamp(Inner.Height - 126.0f, 178.0f, 274.0f);
            const Rectangle2D       TileRect{ Inner.X, Inner.Y + 22.0f, Inner.Width, TileH };
            const ColourContrastInk Ink = ColourCodex::QueryContrastInk(Entry->FillColour);

            // Flat colour-filled swatch tile (zero photo background, contrasting black or white ink)
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

            const std::string TargetSummary = "Target: " + TargetLabel;
            Canvas.Text({ Inner.X, TileRect.Bottom() + 12.0f }, 10.0f, ColorRgba{ 156u, 156u, 160u, 255u }, TargetSummary.c_str());

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
        const BuiltInMaterialPreset& Mat = MaterialPresets[SelectedMaterialIndex % MaterialPresets.size()];
        Canvas.Text({ Inner.X, Inner.Y + 26.0f }, 18.0f, ColorRgba{ 226u, 226u, 228u, 255u }, Mat.Title);
        const Rectangle2D PreviewBox{ Inner.X, Inner.Y + 58.0f, Inner.Width, 150.0f };
        Canvas.FillRectangle(PreviewBox, ColorRgba{ 26u, 26u, 29u, 255u }, 16.0f);
        Canvas.FillCircle(PreviewBox.Center(), 44.0f, Mat.OrbColour);
        Canvas.Text({ Inner.X, PreviewBox.Bottom() + 14.0f }, 10.0f, ColorRgba{ 148u, 148u, 152u, 255u }, "Built-in material preset · switch to Colours for ColourCodex.");
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

    // Handle active colour drag threshold, auto-split reveal, and colour-slot drop
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

    // Track clicks on registered workspace colour slots when visible above the shelf
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

    // 1. Header Bar ("CONTENT / LIBRARY" + "ContentBrowser")
    Canvas.Text({ DrawerRect.X + PadX, CurY }, 8.5f, ColorRgba{ 162u, 150u, 132u, 255u }, "CONTENT / LIBRARY");
    Canvas.Text({ DrawerRect.X + PadX, CurY + 12.0f }, 21.0f, ColorRgba{ 234u, 234u, 236u, 255u }, "ContentBrowser");

    float RightBtnX = DrawerRect.Right() - PadX;
    RightBtnX -= 32.0f;
    if (ConstructCapsuleControl(Canvas, { RightBtnX, CurY + 4.0f, 32.0f, 28.0f }, "x", false, PointerInput))
    {
        ConfigureShelfExtent(ContentShelfExtent::Closed, false);
    }

    RightBtnX -= 78.0f;
    const bool InSplit = (TargetExtent == ContentShelfExtent::SplitHalf);
    if (ConstructCapsuleControl(
            Canvas,
            { RightBtnX, CurY + 4.0f, 72.0f, 28.0f },
            InSplit ? "Full" : "Split",
            InSplit,
            PointerInput))
    {
        ConfigureShelfExtent(InSplit ? ContentShelfExtent::FullHeight : ContentShelfExtent::SplitHalf, false);
    }

    RightBtnX -= 94.0f;
    if (ConstructCapsuleControl(Canvas, { RightBtnX, CurY + 4.0f, 88.0f, 28.0f }, "+ Colour", false, PointerInput))
    {
        const std::size_t Added = CodexLibrary.AddProjectColour("Project Colour", "#2454FF", "Project");
        ActivateCategory(ContentCategoryKind::Colours);
        ConfigureColourFilter(ColourFilterGroup::All);
        SelectedColourIndex = Added;
        StatusMessage       = "Created custom Project Colour (#2454FF)";
    }

    RightBtnX -= 98.0f;
    if (ConstructCapsuleControl(Canvas, { RightBtnX, CurY + 4.0f, 92.0f, 28.0f }, "+ Material", false, PointerInput))
    {
        ActivateCategory(ContentCategoryKind::Materials);
        StatusMessage = "Selected Materials repository";
    }

    CurY += 44.0f;
    Canvas.StrokeLine({ DrawerRect.X + PadX, CurY }, { DrawerRect.Right() - PadX, CurY }, ColorRgba{ 255u, 255u, 255u, 16u }, 1.0f);
    CurY += 10.0f;

    // 2. Origin Tabs + Search + Layout Switcher (Colours gets Grid / Vertical / Horizontal; others get Grid / List)
    constexpr std::array<std::pair<ContentOriginFilter, const char*>, 4> Origins = {{
        { ContentOriginFilter::All,      "All"      },
        { ContentOriginFilter::Project,  "Project"  },
        { ContentOriginFilter::Imported, "Imported" },
        { ContentOriginFilter::Engine,   "Engine"   },
    }};
    float OriginX = DrawerRect.X + PadX;
    for (const auto& [ScopeEnum, Label] : Origins)
    {
        if (ConstructCapsuleControl(Canvas, { OriginX, CurY, 68.0f, 26.0f }, Label, OriginScope == ScopeEnum, PointerInput, 9.5f))
        {
            ConfigureOriginFilter(ScopeEnum);
        }
        OriginX += 74.0f;
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

    // 4. Three-Column Body: Left Category Rail | Centre Repository / Strips | Right Content Details Tile
    constexpr float FooterH  = 32.0f;
    const float     BodyH    = std::max(60.0f, DrawerRect.Bottom() - FooterH - CurY);
    const float     RailW    = 144.0f;
    const float     DetailsW = 286.0f;
    const float     CentreW  = std::max(160.0f, DrawerRect.Width - PadX * 2.0f - RailW - DetailsW - 32.0f);

    const Rectangle2D RailArea{ DrawerRect.X + PadX, CurY, RailW, BodyH };
    const Rectangle2D CentreArea{ RailArea.Right() + 16.0f, CurY, CentreW, BodyH };
    const Rectangle2D DetailsArea{ CentreArea.Right() + 16.0f, CurY, DetailsW, BodyH };

    constexpr std::array<ContentCategoryKind, 9> AllCategories = {{
        ContentCategoryKind::All,
        ContentCategoryKind::Materials,
        ContentCategoryKind::Colours,
        ContentCategoryKind::Images,
        ContentCategoryKind::Fonts,
        ContentCategoryKind::Models,
        ContentCategoryKind::Icons,
        ContentCategoryKind::Code,
        ContentCategoryKind::Other,
    }};

    float RailY = RailArea.Y + 2.0f;
    for (const ContentCategoryKind CategoryOption : AllCategories)
    {
        if (RailY + 28.0f > RailArea.Bottom())
        {
            break;
        }
        const Rectangle2D CatBtn{ RailArea.X, RailY, RailArea.Width - 10.0f, 27.0f };
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
            { CatBtn.X + 10.0f, CatBtn.Y + 7.0f },
            10.5f,
            ActiveCat ? ColorRgba{ 234u, 224u, 206u, 255u } : ColorRgba{ 148u, 148u, 152u, 255u },
            CategoryCaption(CategoryOption));

        if (Hovered && PointerInput.Pressed)
        {
            ActivateCategory(CategoryOption);
        }
        RailY += 30.0f;
    }
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
            ? "Drag any colour circle/strip onto a colour slot or double-click to apply"
            : "Click ContentBrowser notch or Split to dock alongside scene panels";
    Canvas.Text(
        { FooterRect.X, FooterRect.Y + 9.0f },
        9.5f,
        ColorRgba{ 152u, 152u, 156u, 255u },
        StatusMessage.empty() ? DefaultHint : StatusMessage.c_str());
    Canvas.TextRightAligned(FooterRect, 9.0f, ColorRgba{ 116u, 116u, 120u, 255u }, "CONTENTBROWSER · COLOURCODEX");

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
