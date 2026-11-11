/*============================================================================================================================================
                                                              INSPECTORPANEL.CPP
============================================================================================================================================*/
// 🧩 Workspace Inspector pane featuring Showcase Folder Inspector, Add Notes editor, header typography, and capability tiles

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/InspectorPanel.h"
#include "Engine/Components/Controls/DropdownSelector.h"
#include "Engine/Components/Controls/ScalarSlider.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <vector>

namespace Frontier
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool CaseInsensitiveSubstring(std::string_view Haystack, std::string_view Needle) noexcept
{
    if (Needle.empty())
    {
        return true;
    }
    if (Haystack.size() < Needle.size())
    {
        return false;
    }

    for (std::size_t Offset = 0u; Offset + Needle.size() <= Haystack.size(); ++Offset)
    {
        bool Match = true;
        for (std::size_t Index = 0u; Index < Needle.size(); ++Index)
        {
            const auto Left  = static_cast<unsigned char>(Haystack[Offset + Index]);
            const auto Right = static_cast<unsigned char>(Needle[Index]);
            if (std::tolower(Left) != std::tolower(Right))
            {
                Match = false;
                break;
            }
        }
        if (Match)
        {
            return true;
        }
    }
    return false;
}

void StrokeDashedRectangle(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    ColorRgba   Stroke,
    float       DashLength = 4.0f,
    float       GapLength  = 3.0f) noexcept
{
    auto ConstructDashedSegment = [&](Point2D Start, Point2D End) noexcept
    {
        const float DeltaX = End.X - Start.X;
        const float DeltaY = End.Y - Start.Y;
        const float Length = std::sqrt(DeltaX * DeltaX + DeltaY * DeltaY);
        if (Length <= 0.5f)
        {
            return;
        }
        const float DirX = DeltaX / Length;
        const float DirY = DeltaY / Length;
        float       Pos  = 0.0f;
        while (Pos < Length)
        {
            const float SegEnd = std::min(Length, Pos + DashLength);
            Surface.StrokeLine(
                { Start.X + DirX * Pos, Start.Y + DirY * Pos },
                { Start.X + DirX * SegEnd, Start.Y + DirY * SegEnd },
                Stroke,
                1.0f);
            Pos += DashLength + GapLength;
        }
    };

    ConstructDashedSegment({ Bounds.X, Bounds.Y }, { Bounds.Right(), Bounds.Y });
    ConstructDashedSegment({ Bounds.Right(), Bounds.Y }, { Bounds.Right(), Bounds.Bottom() });
    ConstructDashedSegment({ Bounds.Right(), Bounds.Bottom() }, { Bounds.X, Bounds.Bottom() });
    ConstructDashedSegment({ Bounds.X, Bounds.Bottom() }, { Bounds.X, Bounds.Y });
}

void FormatTransformSubtitle(OutlinerRecordEntry& Entry)
{
    char Buffer[96]{};
    std::snprintf(
        Buffer,
        sizeof(Buffer),
        "Position %.0f, %.0f, %.0f m",
        Entry.PositionX,
        Entry.PositionY,
        Entry.PositionZ);
    Entry.Subtitle = Buffer;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                         NOTES SYNCHRONIZATION & CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

void InspectorPanel::ConfigureNotesBoxOpen(bool Open) noexcept
{
    NotesBoxExpanded = Open;
}

void InspectorPanel::AssignActiveNotes(std::string_view NotesText)
{
    if (AttachedOutliner == nullptr)
    {
        return;
    }

    const OutlinerSlotToken Primary = AttachedOutliner->QueryPrimarySelection();
    OutlinerRecordEntry*    Entry   = AttachedOutliner->AccessEntry(Primary);
    if (Entry == nullptr)
    {
        return;
    }

    Entry->AuthorNotes.assign(NotesText.begin(), NotesText.end());
    NotesEditor.Begin(Entry->AuthorNotes.c_str());
    NotesEditor.Active = false;
    SyncedNotesToken   = Primary;
    NotesBoxExpanded   = !Entry->AuthorNotes.empty();
}

void InspectorPanel::ConfigureScrollOffset(float OffsetY) noexcept
{
    ScrollOffsetY = std::max(0.0f, OffsetY);
}

void InspectorPanel::SyncNotesBufferWithSelection(OutlinerSlotToken ActiveToken, const OutlinerRecordEntry* ActiveEntry) noexcept
{
    if (SyncedNotesToken == ActiveToken)
    {
        return;
    }

    SyncedNotesToken = ActiveToken;
    if (ActiveEntry != nullptr)
    {
        NotesEditor.Begin(ActiveEntry->AuthorNotes.c_str());
        NotesEditor.Active = false;
        NotesBoxExpanded   = !ActiveEntry->AuthorNotes.empty();
    }
    else
    {
        NotesEditor.Begin("");
        NotesEditor.Active = false;
        NotesBoxExpanded   = false;
    }
}

float InspectorPanel::RenderNotesControl(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   AvailableWidth,
    bool                    RightAnchoredButton,
    float                   ButtonRightX,
    float                   ButtonTopY,
    OutlinerRecordEntry&    Entry,
    const PaneInputReading& Input)
{
    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    if (!NotesBoxExpanded)
    {
        const float       BtnW = 84.0f;
        const float       BtnH = 28.0f;
        const Rectangle2D AddNotesButton{
            RightAnchoredButton ? (ButtonRightX - BtnW) : LeftX,
            RightAnchoredButton ? ButtonTopY : TopY,
            BtnW,
            BtnH
        };

        const bool Hovered = AddNotesButton.Contains(Cursor);
        StrokeDashedRectangle(
            Surface,
            AddNotesButton,
            Hovered ? ColorRgba{ 118, 118, 118, 255 } : ColorRgba{ 68, 68, 68, 255 },
            4.0f,
            3.0f);

        VectorIconRepository::ConstructControl(
            Surface,
            ControlIcon::PlusAdd,
            VectorIconPlacement{
                AddNotesButton.X + 10.0f,
                AddNotesButton.Y + 7.5f,
                13.0f,
                Hovered ? ColorRgba{ 212, 212, 212, 255 } : ColorRgba{ 137, 137, 137, 255 },
                1.9f });

        WorkspacePaneAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { AddNotesButton.X + 27.0f, AddNotesButton.Y + 8.5f },
            9.5f,
            Hovered ? ColorRgba{ 212, 212, 212, 255 } : ColorRgba{ 137, 137, 137, 255 },
            "Add notes");

        if (Hovered && Input.PrimaryPointer.Pressed)
        {
            NotesBoxExpanded   = true;
            NotesEditor.Active = true;
            NotesEditor.Caret  = NotesEditor.Length;
            NotesEditor.SelectionAnchor = NotesEditor.Caret;
        }

        return RightAnchoredButton ? 0.0f : (BtnH + 6.0f);
    }

    // Expanded optional notes box (#191919 bg, 1px solid #303030, 9px radius)
    const Rectangle2D NotesBox{ LeftX, TopY, AvailableWidth, 116.0f };
    Surface.FillRectangle(NotesBox, WorkspaceSurfaceColors::ControlWell, 9.0f);
    Surface.StrokeRectangle(NotesBox, ColorRgba{ 48, 48, 48, 255 }, 9.0f, 1.0f);

    WorkspacePaneAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Medium,
        { NotesBox.X + 10.0f, NotesBox.Y + 10.0f },
        10.0f,
        ColorRgba{ 201, 201, 201, 255 },
        "Notes");

    WorkspacePaneAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { NotesBox.X + 46.0f, NotesBox.Y + 11.5f },
        8.0f,
        ColorRgba{ 110, 110, 110, 255 },
        "OPTIONAL");

    const Rectangle2D HideBtn{ NotesBox.Right() - 46.0f, NotesBox.Y + 7.0f, 36.0f, 20.0f };
    const bool        HideHovered = HideBtn.Contains(Cursor);
    Surface.FillRectangle(HideBtn, HideHovered ? ColorRgba{ 50, 50, 50, 255 } : ColorRgba{ 37, 37, 37, 255 }, 10.0f);
    WorkspacePaneAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { HideBtn.X + 9.0f, HideBtn.Y + 5.0f },
        8.5f,
        HideHovered ? WorkspaceSurfaceColors::PrimaryInk : ColorRgba{ 142, 142, 142, 255 },
        "Hide");

    if (HideHovered && Input.PrimaryPointer.Pressed)
    {
        NotesBoxExpanded   = false;
        NotesEditor.Active = false;
        return 122.0f;
    }

    const Rectangle2D TextArea{ NotesBox.X + 10.0f, NotesBox.Y + 33.0f, NotesBox.Width - 20.0f, 73.0f };
    Surface.FillRectangle(TextArea, ColorRgba{ 17, 17, 17, 255 }, 6.0f);
    Surface.StrokeRectangle(
        TextArea,
        NotesEditor.Active ? ColorRgba{ 118, 126, 142, 255 } : ColorRgba{ 38, 38, 38, 255 },
        6.0f,
        1.0f);

    if (Input.PrimaryPointer.Pressed)
    {
        if (TextArea.Contains(Cursor))
        {
            NotesEditor.Active          = true;
            NotesEditor.Caret           = NotesEditor.Length;
            NotesEditor.SelectionAnchor = NotesEditor.Caret;
        }
        else if (NotesEditor.Active)
        {
            NotesEditor.Finalize();
        }
    }

    Entry.AuthorNotes.assign(NotesEditor.Text, NotesEditor.Length);

    if (Entry.AuthorNotes.empty() && !NotesEditor.Active)
    {
        WorkspacePaneAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { TextArea.X + 10.0f, TextArea.Y + 10.0f },
            TextArea.Width - 20.0f,
            11.0f,
            ColorRgba{ 98, 98, 98, 255 },
            "Purpose, ownership, review notes...");
    }
    else
    {
        // Word-wrap up to 3 lines inside the 73px textarea
        const float      MaxLineW = std::max(40.0f, TextArea.Width - 20.0f);
        std::string_view Remaining(Entry.AuthorNotes);
        float            LineY    = TextArea.Y + 10.0f;
        Point2D          CaretPos{ TextArea.X + 10.0f, LineY };

        for (uint32_t LineIdx = 0u; LineIdx < 3u && !Remaining.empty(); ++LineIdx)
        {
            std::size_t Take = Remaining.size();
            while (Take > 1u &&
                   WorkspacePaneAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.0f, Remaining.substr(0u, Take)) > MaxLineW)
            {
                --Take;
            }
            if (Take < Remaining.size())
            {
                const std::size_t Space = Remaining.substr(0u, Take).rfind(' ');
                if (Space != std::string_view::npos && Space > 0u)
                {
                    Take = Space + 1u;
                }
            }
            const std::string_view LineText = Remaining.substr(0u, Take);
            WorkspacePaneAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { TextArea.X + 10.0f, LineY },
                11.0f,
                ColorRgba{ 204, 204, 204, 255 },
                LineText);

            CaretPos = {
                TextArea.X + 10.0f + WorkspacePaneAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.0f, LineText),
                LineY
            };
            Remaining.remove_prefix(Take);
            LineY += 17.0f;
        }

        if (NotesEditor.Active && NotesEditor.BlinkPhase < ControlMetrics::CaretBlinkSeconds * 0.5f)
        {
            Surface.StrokeLine({ CaretPos.X, CaretPos.Y }, { CaretPos.X, CaretPos.Y + 13.0f }, WorkspaceSurfaceColors::PrimaryInk, 1.2f);
        }
    }

    return 126.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                         SHOWCASE FOLDER INSPECTOR PRESENTATION
//------------------------------------------------------------------------------------------------------------------------

float InspectorPanel::RenderFolderInspector(
    PixelSpace&             Surface,
    Rectangle2D             ScrollViewport,
    float                   OriginY,
    OutlinerRecordEntry&    FolderEntry,
    const PaneInputReading& Input)
{
    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const float   PadX   = 22.0f;
    const float   LeftX  = ScrollViewport.X + PadX;
    const float   InnerW = std::max(120.0f, ScrollViewport.Width - PadX * 2.0f);
    float         CurY   = OriginY + 22.0f;

    // Build enclosure chain from root to FolderEntry
    std::vector<OutlinerSlotToken> TrailChain{};
    {
        OutlinerSlotToken Step  = FolderEntry.Token;
        uint32_t          Guard = 0u;
        while (Step.Valid() && Guard++ < 64u && AttachedOutliner != nullptr)
        {
            TrailChain.push_back(Step);
            const OutlinerRecordEntry* Found = AttachedOutliner->FindEntry(Step);
            Step = (Found != nullptr) ? Found->EnclosingToken : OutlinerSlotToken{};
        }
        std::reverse(TrailChain.begin(), TrailChain.end());
    }

    // 1. Breadcrumbs ("Outliner / Showcase")
    std::string BreadcrumbText = (AttachedOutliner != nullptr) ? AttachedOutliner->QueryHeadingText() : "Outliner";
    for (const OutlinerSlotToken StepToken : TrailChain)
    {
        if (const OutlinerRecordEntry* StepEntry = AttachedOutliner->FindEntry(StepToken))
        {
            BreadcrumbText += " / ";
            BreadcrumbText += StepEntry->Title;
        }
    }
    WorkspacePaneAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX, CurY },
        InnerW,
        10.0f,
        WorkspaceSurfaceColors::MutedInk,
        BreadcrumbText);
    CurY += 20.0f;

    // 2. COLLECTION eyebrow (8px, #8C9587)
    WorkspacePaneAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Medium,
        { LeftX, CurY },
        8.5f,
        ColorRgba{ 140, 149, 135, 255 },
        "COLLECTION");
    CurY += 16.0f;

    // 3. 28px Showcase Folder Icon + 27px DM Sans Light Title
    WorkspacePaneAppearance::ConstructShowcaseFolderIcon(Surface, { LeftX, CurY + 2.0f }, 28.0f, FolderEntry.FolderTint, 1.0f);
    WorkspacePaneAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Light,
        { LeftX + 40.0f, CurY + 1.0f },
        InnerW - 42.0f,
        27.0f,
        WorkspaceSurfaceColors::FolderHeadingInk,
        FolderEntry.Title);
    CurY += 38.0f;

    WorkspacePaneAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX, CurY },
        InnerW,
        11.0f,
        ColorRgba{ 136, 140, 132, 255 },
        "Scene collection  -  organizational container");
    CurY += 24.0f;

    // 4. Add Notes button / expanded Notes box
    CurY += RenderNotesControl(Surface, LeftX, CurY, InnerW, false, 0.0f, 0.0f, FolderEntry, Input);
    CurY += 10.0f;

    // 5. Interactive .collection-trail breadcrumb chips ("Scene root > Showcase")
    {
        float ChipX = LeftX;
        const char* RootLabel = "Scene root";
        const float RootW     = WorkspacePaneAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, RootLabel) + 18.0f;
        const Rectangle2D RootChip{ ChipX, CurY, RootW, 24.0f };
        Surface.FillRectangle(RootChip, ColorRgba{ 38, 38, 38, 255 }, 12.0f);
        Surface.StrokeRectangle(RootChip, ColorRgba{ 255, 255, 255, 18 }, 12.0f, 1.0f);
        WorkspacePaneAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { RootChip.X + 9.0f, RootChip.Y + 6.0f },
            10.0f,
            ColorRgba{ 182, 188, 176, 255 },
            RootLabel);
        if (ScrollViewport.Contains(Cursor) && RootChip.Contains(Cursor) && Input.PrimaryPointer.Pressed && AttachedOutliner != nullptr)
        {
            AttachedOutliner->ClearSelection();
        }
        ChipX += RootW + 6.0f;

        for (std::size_t Idx = 0u; Idx < TrailChain.size(); ++Idx)
        {
            const OutlinerRecordEntry* StepEntry = AttachedOutliner->FindEntry(TrailChain[Idx]);
            if (StepEntry == nullptr)
            {
                continue;
            }
            WorkspacePaneAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { ChipX, CurY + 6.0f },
                10.0f,
                ColorRgba{ 118, 122, 114, 255 },
                ">");
            ChipX += 11.0f;

            const bool  Current = (Idx + 1u == TrailChain.size());
            const float ChipW   = std::min(140.0f, WorkspacePaneAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, StepEntry->Title) + 18.0f);
            if (ChipX + ChipW > LeftX + InnerW && ChipX > LeftX + 20.0f)
            {
                ChipX = LeftX;
                CurY += 28.0f;
            }
            const Rectangle2D StepChip{ ChipX, CurY, ChipW, 24.0f };
            Surface.FillRectangle(StepChip, ColorRgba{ 38, 38, 38, 255 }, 12.0f);
            Surface.StrokeRectangle(
                StepChip,
                Current ? FolderEntry.FolderTint.WithAlpha(110u) : ColorRgba{ 255, 255, 255, 18 },
                12.0f,
                1.0f);
            WorkspacePaneAppearance::ConstructTruncatedText(
                Surface,
                FontWeightClass::Regular,
                { StepChip.X + 9.0f, StepChip.Y + 6.0f },
                ChipW - 18.0f,
                10.0f,
                Current ? ColorRgba{ 215, 223, 207, 255 } : ColorRgba{ 182, 188, 176, 255 },
                StepEntry->Title);

            if (!Current && ScrollViewport.Contains(Cursor) && StepChip.Contains(Cursor) && Input.PrimaryPointer.Pressed)
            {
                AttachedOutliner->SelectSingle(StepEntry->Token);
            }
            ChipX += ChipW + 6.0f;
        }
        CurY += 38.0f;
    }

    // Gather folder inventory statistics
    const uint32_t TotalWorkspace = (AttachedOutliner != nullptr) ? AttachedOutliner->QueryTotalCount() : 0u;
    const uint32_t DirectCount    = (AttachedOutliner != nullptr) ? AttachedOutliner->CountDirectNested(FolderEntry.Token) : 0u;
    const uint32_t NestedTotal    = (AttachedOutliner != nullptr) ? AttachedOutliner->CountAllNested(FolderEntry.Token) : 0u;
    const uint32_t DeeperCount    = (NestedTotal >= DirectCount) ? (NestedTotal - DirectCount) : 0u;
    const uint32_t SubfolderCount = (AttachedOutliner != nullptr) ? AttachedOutliner->CountNestedFolders(FolderEntry.Token) : 0u;

    uint32_t                                          FolderVisibleCount = 0u;
    std::array<uint32_t, 5>                           CategoryCounts{};
    std::vector<const OutlinerRecordEntry*>           FolderEntries{};

    if (AttachedOutliner != nullptr)
    {
        for (const auto& Candidate : AttachedOutliner->QueryEntries())
        {
            if (Candidate.Token == FolderEntry.Token)
            {
                continue;
            }
            bool InFolder = false;
            OutlinerSlotToken Walk  = Candidate.EnclosingToken;
            uint32_t          Guard = 0u;
            while (Walk.Valid() && Guard++ < 64u)
            {
                if (Walk == FolderEntry.Token)
                {
                    InFolder = true;
                    break;
                }
                const OutlinerRecordEntry* Enc = AttachedOutliner->FindEntry(Walk);
                Walk = (Enc != nullptr) ? Enc->EnclosingToken : OutlinerSlotToken{};
            }
            if (!InFolder)
            {
                continue;
            }

            if (AttachedOutliner->QueryEffectiveVisibility(Candidate.Token))
            {
                ++FolderVisibleCount;
            }
            const auto CatOrd = static_cast<std::size_t>(Candidate.Category);
            if (CatOrd < CategoryCounts.size())
            {
                ++CategoryCounts[CatOrd];
            }
            FolderEntries.push_back(&Candidate);
        }
    }
    const uint32_t FolderHiddenCount = NestedTotal - FolderVisibleCount;

    // 6. Collection contents block (#202020, 25px radius, 52px Light number + Direct / Nested / Subfolders)
    {
        const Rectangle2D ContentsBlock{ LeftX, CurY, InnerW, 198.0f };
        Surface.FillRectangle(ContentsBlock, WorkspaceSurfaceColors::SectionSurface, 25.0f);
        Surface.StrokeRectangle(ContentsBlock, WorkspaceSurfaceColors::HairlineLight, 25.0f, 1.0f);

        Surface.FillRectangle({ ContentsBlock.X + 23.0f, ContentsBlock.Y + 24.0f, 3.0f, 15.0f }, FolderEntry.FolderTint, 1.5f);
        WorkspacePaneAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { ContentsBlock.X + 35.0f, ContentsBlock.Y + 23.0f },
            13.0f,
            WorkspaceSurfaceColors::FolderHeadingInk,
            "Collection contents");

        const uint32_t SharePercent = (TotalWorkspace > 1u)
                                          ? static_cast<uint32_t>(std::lround(100.0 * static_cast<double>(NestedTotal) / static_cast<double>(TotalWorkspace - 1u)))
                                          : 0u;
        char ShareLabel[48]{};
        std::snprintf(ShareLabel, sizeof(ShareLabel), "%u%% of scene", std::min(100u, SharePercent));
        const float ShareW = WorkspacePaneAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.5f, ShareLabel);
        WorkspacePaneAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { ContentsBlock.Right() - 23.0f - ShareW, ContentsBlock.Y + 25.0f },
            9.5f,
            ColorRgba{ 140, 144, 136, 255 },
            ShareLabel);

        char BigCountText[32]{};
        std::snprintf(BigCountText, sizeof(BigCountText), "%u", NestedTotal);
        WorkspacePaneAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Light,
            { ContentsBlock.X + 23.0f, ContentsBlock.Y + 52.0f },
            52.0f,
            WorkspaceSurfaceColors::FolderHeadingInk,
            BigCountText);

        const float BigW = WorkspacePaneAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, 52.0f, BigCountText);
        WorkspacePaneAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Light,
            { ContentsBlock.X + 30.0f + BigW, ContentsBlock.Y + 84.0f },
            15.0f,
            ColorRgba{ 130, 133, 127, 255 },
            (NestedTotal == 1u) ? "entry" : "entries");

        // 3-column Direct / Nested / Subfolders row
        const float DividerY = ContentsBlock.Y + 126.0f;
        Surface.StrokeLine(
            { ContentsBlock.X + 23.0f, DividerY },
            { ContentsBlock.Right() - 23.0f, DividerY },
            WorkspaceSurfaceColors::HairlineLight,
            1.0f);

        const float ColW = (InnerW - 46.0f) / 3.0f;
        const std::array<std::pair<uint32_t, const char*>, 3> Facts = {{
            { DirectCount,    "Direct"     },
            { DeeperCount,    "Nested"     },
            { SubfolderCount, "Subfolders" },
        }};

        for (std::size_t Col = 0u; Col < Facts.size(); ++Col)
        {
            const float ColX = ContentsBlock.X + 23.0f + static_cast<float>(Col) * ColW;
            char        ValBuf[24]{};
            std::snprintf(ValBuf, sizeof(ValBuf), "%u", Facts[Col].first);
            WorkspacePaneAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Light,
                { ColX, DividerY + 15.0f },
                21.0f,
                WorkspaceSurfaceColors::FolderHeadingInk,
                ValBuf);
            WorkspacePaneAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { ColX, DividerY + 41.0f },
                9.5f,
                ColorRgba{ 141, 145, 136, 255 },
                Facts[Col].second);
        }

        CurY += ContentsBlock.Height + 12.0f;
    }

    // 7. 2-column Visible & Hidden status filter blocks (#202020, 23px radius, 32px Light count)
    {
        const float       HalfW = (InnerW - 10.0f) * 0.5f;
        const Rectangle2D VisBlock{ LeftX, CurY, HalfW, 92.0f };
        const Rectangle2D HidBlock{ VisBlock.Right() + 10.0f, CurY, HalfW, 92.0f };

        auto ConstructVisibilityBlock = [&](Rectangle2D Block, bool VisibleSlot, uint32_t Count, bool SelectedFilter)
        {
            const bool Hovered = ScrollViewport.Contains(Cursor) && Block.Contains(Cursor);
            Surface.FillRectangle(Block, Hovered ? ColorRgba{ 38, 38, 38, 255 } : WorkspaceSurfaceColors::SectionSurface, 23.0f);
            Surface.StrokeRectangle(
                Block,
                SelectedFilter ? ColorRgba{ 165, 185, 154, 130 } : WorkspaceSurfaceColors::HairlineLight,
                23.0f,
                1.0f);

            Surface.FillCircle(
                { Block.X + 22.0f, Block.Y + 24.0f },
                3.5f,
                VisibleSlot ? ColorRgba{ 91, 185, 121, 255 } : ColorRgba{ 160, 135, 111, 255 });
            WorkspacePaneAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { Block.X + 33.0f, Block.Y + 18.0f },
                11.0f,
                WorkspaceSurfaceColors::FolderHeadingInk,
                VisibleSlot ? "Visible" : "Hidden");

            char NumBuf[24]{};
            std::snprintf(NumBuf, sizeof(NumBuf), "%u", Count);
            WorkspacePaneAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Light,
                { Block.X + 18.0f, Block.Y + 42.0f },
                32.0f,
                WorkspaceSurfaceColors::FolderHeadingInk,
                NumBuf);

            return Hovered && Input.PrimaryPointer.Pressed;
        };

        if (ConstructVisibilityBlock(VisBlock, true, FolderVisibleCount, FolderVisibilityFilter == OutlinerVisibilityFilter::VisibleOnly))
        {
            FolderVisibilityFilter = (FolderVisibilityFilter == OutlinerVisibilityFilter::VisibleOnly)
                                         ? OutlinerVisibilityFilter::All
                                         : OutlinerVisibilityFilter::VisibleOnly;
        }
        if (ConstructVisibilityBlock(HidBlock, false, FolderHiddenCount, FolderVisibilityFilter == OutlinerVisibilityFilter::HiddenOnly))
        {
            FolderVisibilityFilter = (FolderVisibilityFilter == OutlinerVisibilityFilter::HiddenOnly)
                                         ? OutlinerVisibilityFilter::All
                                         : OutlinerVisibilityFilter::HiddenOnly;
        }

        CurY += 104.0f;
    }

    // 8. Composition block (#202020, 25px radius, 12px stacked bar + 2-column category breakdown)
    {
        const Rectangle2D CompBlock{ LeftX, CurY, InnerW, 136.0f };
        Surface.FillRectangle(CompBlock, WorkspaceSurfaceColors::SectionSurface, 25.0f);
        Surface.StrokeRectangle(CompBlock, WorkspaceSurfaceColors::HairlineLight, 25.0f, 1.0f);

        Surface.FillRectangle({ CompBlock.X + 23.0f, CompBlock.Y + 22.0f, 3.0f, 15.0f }, FolderEntry.FolderTint, 1.5f);
        WorkspacePaneAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { CompBlock.X + 35.0f, CompBlock.Y + 21.0f },
            13.0f,
            WorkspaceSurfaceColors::FolderHeadingInk,
            "Composition");

        const Rectangle2D BarTrack{ CompBlock.X + 23.0f, CompBlock.Y + 50.0f, InnerW - 46.0f, 12.0f };
        Surface.FillRectangle(BarTrack, ColorRgba{ 22, 24, 21, 255 }, 6.0f);

        if (NestedTotal > 0u)
        {
            float SegX = BarTrack.X;
            for (std::size_t Cat = 0u; Cat < CategoryCounts.size(); ++Cat)
            {
                if (CategoryCounts[Cat] == 0u)
                {
                    continue;
                }
                const float SegW = std::max(4.0f, BarTrack.Width * (static_cast<float>(CategoryCounts[Cat]) / static_cast<float>(NestedTotal)));
                const float ClampedW = std::min(SegW, BarTrack.Right() - SegX);
                if (ClampedW > 0.5f)
                {
                    const ColorRgba SegColor = WorkspacePaneAppearance::QueryCategoryAccent(
                        static_cast<ConstructSlotCategory>(Cat),
                        FolderEntry.FolderTint);
                    Surface.FillRectangle({ SegX, BarTrack.Y, ClampedW, BarTrack.Height }, SegColor, 4.0f);
                    SegX += ClampedW + 2.0f;
                }
            }
        }

        constexpr std::array<const char*, 5> CatNames = {{
            "Subfolders",
            "Type 1",
            "Type 2",
            "Type 3",
            "Type 4",
        }};
        const float HalfCol = (InnerW - 56.0f) * 0.5f;
        for (std::size_t Cat = 0u; Cat < 4u; ++Cat)
        {
            const std::size_t SlotOrd  = Cat + 1u;
            const float       LegendX  = CompBlock.X + 23.0f + static_cast<float>(Cat % 2u) * (HalfCol + 10.0f);
            const float       LegendY  = CompBlock.Y + 76.0f + static_cast<float>(Cat / 2u) * 22.0f;
            const VisualToken CatToken = WorkspacePaneAppearance::QueryCategoryVisualToken(
                static_cast<ConstructSlotCategory>(SlotOrd),
                6.5f,
                FolderEntry.FolderTint);

            WorkspacePaneAppearance::ConstructVisualToken(Surface, { LegendX + 4.0f, LegendY + 6.5f }, CatToken);
            WorkspacePaneAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { LegendX + 13.0f, LegendY },
                10.0f,
                ColorRgba{ 174, 180, 166, 255 },
                CatNames[SlotOrd]);

            char CntBuf[16]{};
            std::snprintf(CntBuf, sizeof(CntBuf), "%u", CategoryCounts[SlotOrd]);
            const float CntW = WorkspacePaneAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, CntBuf);
            WorkspacePaneAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { LegendX + HalfCol - CntW, LegendY },
                10.0f,
                ColorRgba{ 212, 215, 205, 255 },
                CntBuf);
        }

        CurY += CompBlock.Height + 12.0f;
    }

    // 9. Browse contents block (#202020, 25px radius: Find in folder + Scope/Sort toggles + clickable entries)
    {
        std::vector<const OutlinerRecordEntry*> FilteredEntries{};
        const std::string_view                  SearchText(FolderSearchEditor.Text, FolderSearchEditor.Length);

        for (const OutlinerRecordEntry* Candidate : FolderEntries)
        {
            if (BrowseScope == FolderBrowseScope::DirectOnly && Candidate->EnclosingToken != FolderEntry.Token)
            {
                continue;
            }
            const bool EffVis = AttachedOutliner->QueryEffectiveVisibility(Candidate->Token);
            if (FolderVisibilityFilter == OutlinerVisibilityFilter::VisibleOnly && !EffVis)
            {
                continue;
            }
            if (FolderVisibilityFilter == OutlinerVisibilityFilter::HiddenOnly && EffVis)
            {
                continue;
            }
            if (!SearchText.empty() &&
                !CaseInsensitiveSubstring(Candidate->Title, SearchText) &&
                !CaseInsensitiveSubstring(Candidate->CategoryLabel, SearchText))
            {
                continue;
            }
            FilteredEntries.push_back(Candidate);
        }

        if (BrowseSort == FolderBrowseSort::Alphabetical)
        {
            std::sort(
                FilteredEntries.begin(),
                FilteredEntries.end(),
                [](const OutlinerRecordEntry* Left, const OutlinerRecordEntry* Right)
                {
                    return Left->Title < Right->Title;
                });
        }

        const std::size_t DisplayedRows = std::min<std::size_t>(FilteredEntries.size(), 6u);
        const float       RowsHeight    = (DisplayedRows == 0u) ? 34.0f : static_cast<float>(DisplayedRows) * 36.0f;
        const Rectangle2D BrowseBlock{ LeftX, CurY, InnerW, 158.0f + RowsHeight };

        Surface.FillRectangle(BrowseBlock, WorkspaceSurfaceColors::SectionSurface, 25.0f);
        Surface.StrokeRectangle(BrowseBlock, WorkspaceSurfaceColors::HairlineLight, 25.0f, 1.0f);

        Surface.FillRectangle({ BrowseBlock.X + 23.0f, BrowseBlock.Y + 22.0f, 3.0f, 15.0f }, FolderEntry.FolderTint, 1.5f);
        WorkspacePaneAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { BrowseBlock.X + 35.0f, BrowseBlock.Y + 21.0f },
            13.0f,
            WorkspaceSurfaceColors::FolderHeadingInk,
            "Browse contents");

        char ShownText[32]{};
        std::snprintf(ShownText, sizeof(ShownText), "%zu shown", FilteredEntries.size());
        const float ShownW = WorkspacePaneAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.5f, ShownText);
        WorkspacePaneAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { BrowseBlock.Right() - 23.0f - ShownW, BrowseBlock.Y + 24.0f },
            9.5f,
            ColorRgba{ 140, 144, 136, 255 },
            ShownText);

        WorkspacePaneAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { BrowseBlock.X + 23.0f, BrowseBlock.Y + 48.0f },
            9.5f,
            ColorRgba{ 136, 142, 128, 255 },
            "Find in folder");

        const Rectangle2D SearchField{ BrowseBlock.X + 23.0f, BrowseBlock.Y + 64.0f, InnerW - 46.0f, 32.0f };
        Surface.FillRectangle(SearchField, ColorRgba{ 24, 26, 23, 255 }, 12.0f);
        Surface.StrokeRectangle(
            SearchField,
            FolderSearchEditor.Active ? ColorRgba{ 165, 185, 154, 150 } : ColorRgba{ 255, 255, 255, 18 },
            12.0f,
            1.0f);

        if (ScrollViewport.Contains(Cursor) && Input.PrimaryPointer.Pressed)
        {
            if (SearchField.Contains(Cursor))
            {
                FolderSearchEditor.Active          = true;
                FolderSearchEditor.Caret           = FolderSearchEditor.Length;
                FolderSearchEditor.SelectionAnchor = FolderSearchEditor.Caret;
            }
            else if (FolderSearchEditor.Active)
            {
                FolderSearchEditor.Finalize();
            }
        }

        if (FolderSearchEditor.Length == 0u && !FolderSearchEditor.Active)
        {
            WorkspacePaneAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { SearchField.X + 10.0f, SearchField.Y + 9.0f },
                11.0f,
                ColorRgba{ 118, 124, 112, 255 },
                "Name or category...");
        }
        else
        {
            WorkspacePaneAppearance::ConstructTruncatedText(
                Surface,
                FontWeightClass::Regular,
                { SearchField.X + 10.0f, SearchField.Y + 9.0f },
                SearchField.Width - 20.0f,
                11.0f,
                ColorRgba{ 189, 197, 180, 255 },
                SearchText);
        }

        // Scope & Sort dropdown selectors (Reusable Studio Controls DropdownSelector style)
        const float       FilterBtnW = (InnerW - 46.0f - 10.0f) * 0.5f;
        const Rectangle2D ScopeBtn{ BrowseBlock.X + 23.0f, SearchField.Bottom() + 10.0f, FilterBtnW, 32.0f };
        const Rectangle2D SortBtn{ ScopeBtn.Right() + 10.0f, SearchField.Bottom() + 10.0f, FilterBtnW, 32.0f };
        ScopeButtonBounds = ScopeBtn;
        SortButtonBounds  = SortBtn;

        ControlPointer DropdownPointer = Input.PrimaryPointer;
        if (!ScrollViewport.Contains(Cursor) || ScopeDropdownOpen || SortDropdownOpen)
        {
            DropdownPointer.Enabled = ScrollViewport.Contains(Cursor) && !ScopeDropdownOpen && !SortDropdownOpen;
        }

        const ControlContact ScopeHit = DropdownSelector::ConstructButton(
            Surface,
            ScopeBtn,
            (BrowseScope == FolderBrowseScope::NestedAll) ? "Scope: Nested" : "Scope: Direct",
            ScopeDropdownOpen,
            DropdownPointer);
        if (ScopeHit.Pressed)
        {
            ScopeDropdownOpen = !ScopeDropdownOpen;
            SortDropdownOpen  = false;
        }

        const ControlContact SortHit = DropdownSelector::ConstructButton(
            Surface,
            SortBtn,
            (BrowseSort == FolderBrowseSort::OutlinerOrder) ? "Sort: Order" : "Sort: Name",
            SortDropdownOpen,
            DropdownPointer);
        if (SortHit.Pressed)
        {
            SortDropdownOpen  = !SortDropdownOpen;
            ScopeDropdownOpen = false;
        }

        float EntryRowY = ScopeBtn.Bottom() + 12.0f;
        if (DisplayedRows == 0u)
        {
            WorkspacePaneAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { BrowseBlock.X + 23.0f, EntryRowY + 6.0f },
                10.5f,
                ColorRgba{ 126, 132, 120, 255 },
                FolderEntries.empty() ? "Folder is currently empty." : "No entries match filter.");
        }
        else
        {
            for (std::size_t RowIdx = 0u; RowIdx < DisplayedRows; ++RowIdx)
            {
                const OutlinerRecordEntry* SubEntry = FilteredEntries[RowIdx];
                const Rectangle2D          RowRect{ BrowseBlock.X + 23.0f, EntryRowY, InnerW - 46.0f, 34.0f };
                Surface.StrokeLine({ RowRect.X, RowRect.Y }, { RowRect.Right(), RowRect.Y }, WorkspaceSurfaceColors::HairlineLight, 1.0f);

                const bool RowHover = ScrollViewport.Contains(Cursor) && RowRect.Contains(Cursor);
                if (RowHover)
                {
                    Surface.FillRectangle({ RowRect.X - 4.0f, RowRect.Y + 2.0f, RowRect.Width + 8.0f, RowRect.Height - 4.0f }, ColorRgba{ 42, 44, 40, 255 }, 6.0f);
                }

                WorkspacePaneAppearance::ConstructSlotCategoryIcon(Surface, SubEntry->Category, { RowRect.X + 2.0f, RowRect.Y + 8.0f }, 18.0f, SubEntry->FolderTint, 1.0f);
                WorkspacePaneAppearance::ConstructTruncatedText(
                    Surface,
                    FontWeightClass::Regular,
                    { RowRect.X + 27.0f, RowRect.Y + 10.0f },
                    RowRect.Width - 62.0f,
                    11.0f,
                    WorkspaceSurfaceColors::FolderHeadingInk,
                    SubEntry->Title);

                const Rectangle2D EyeBtn{ RowRect.Right() - 22.0f, RowRect.Y + 7.0f, 20.0f, 20.0f };
                VectorIconRepository::ConstructControl(
                    Surface,
                    SubEntry->VisibilityEnabled ? ControlIcon::EyeVisible : ControlIcon::EyeHidden,
                    VectorIconPlacement{ EyeBtn.X + 3.0f, EyeBtn.Y + 3.0f, 14.0f, ColorRgba{ 150, 156, 144, 255 }, 1.8f });

                if (RowHover && Input.PrimaryPointer.Pressed && AttachedOutliner != nullptr)
                {
                    if (EyeBtn.Contains(Cursor))
                    {
                        if (OutlinerRecordEntry* MutableSubEntry = AttachedOutliner->AccessEntry(SubEntry->Token))
                        {
                            MutableSubEntry->VisibilityEnabled = !MutableSubEntry->VisibilityEnabled;
                        }
                    }
                    else
                    {
                        AttachedOutliner->SelectSingle(SubEntry->Token);
                    }
                }
                EntryRowY += 36.0f;
            }
        }

        CurY += BrowseBlock.Height + 12.0f;
    }

    // 10. Collection appearance / Folder tint section (#202020, 25px radius, warm #B9A969 bar)
    {
        const Rectangle2D TintBlock{ LeftX, CurY, InnerW, 96.0f };
        Surface.FillRectangle(TintBlock, WorkspaceSurfaceColors::SectionSurface, 25.0f);
        Surface.StrokeRectangle(TintBlock, WorkspaceSurfaceColors::HairlineLight, 25.0f, 1.0f);

        Surface.FillRectangle({ TintBlock.X + 23.0f, TintBlock.Y + 22.0f, 3.0f, 15.0f }, WorkspaceSurfaceColors::ShowcaseWarmGold, 1.5f);
        WorkspacePaneAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { TintBlock.X + 35.0f, TintBlock.Y + 21.0f },
            13.0f,
            WorkspaceSurfaceColors::FolderHeadingInk,
            "Collection appearance");

        WorkspacePaneAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { TintBlock.X + 23.0f, TintBlock.Y + 56.0f },
            10.5f,
            ColorRgba{ 158, 169, 147, 255 },
            "Folder tint");

        constexpr std::array<ColorRgba, 5> Swatches = {{
            { 162, 184, 124, 255 },
            { 88,  153, 121, 255 },
            { 185, 169, 105, 255 },
            { 131, 200, 204, 255 },
            { 196, 138, 158, 255 },
        }};

        float SwatchX = TintBlock.Right() - 23.0f - static_cast<float>(Swatches.size()) * 26.0f;
        for (const ColorRgba Swatch : Swatches)
        {
            const Point2D SwatchCenter{ SwatchX + 10.0f, TintBlock.Y + 62.0f };
            VisualToken   SwatchToken{};
            SwatchToken.Shape     = VisualTokenShape::Circle;
            SwatchToken.FillColor = Swatch;
            SwatchToken.Extent    = 19.0f;
            WorkspacePaneAppearance::ConstructVisualToken(Surface, SwatchCenter, SwatchToken);
            if (FolderEntry.FolderTint == Swatch)
            {
                Surface.StrokeCircle(SwatchCenter, 12.0f, WorkspaceSurfaceColors::PrimaryInk, 1.4f);
            }
            const Rectangle2D Hit{ SwatchCenter.X - 11.0f, SwatchCenter.Y - 11.0f, 22.0f, 22.0f };
            if (ScrollViewport.Contains(Cursor) && Hit.Contains(Cursor) && Input.PrimaryPointer.Pressed)
            {
                FolderEntry.FolderTint = Swatch;
            }
            SwatchX += 26.0f;
        }

        CurY += TintBlock.Height + 28.0f;
    }

    return CurY - OriginY;
}

//------------------------------------------------------------------------------------------------------------------------
//                                         STANDARD ENTRY INSPECTOR PRESENTATION
//------------------------------------------------------------------------------------------------------------------------

float InspectorPanel::RenderStandardEntryInspector(
    PixelSpace&             Surface,
    Rectangle2D             ScrollViewport,
    float                   OriginY,
    OutlinerRecordEntry&    Entry,
    const PaneInputReading& Input)
{
    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const float   PadX   = 26.0f;
    const float   LeftX  = ScrollViewport.X + PadX;
    const float   InnerW = std::max(120.0f, ScrollViewport.Width - PadX * 2.0f);
    float         CurY   = OriginY + 26.0f;

    // 1. workspace .inspector-heading (8px breadcrumbs, 25px Light h1, 10px eyebrow, top-right + Add notes)
    std::string Breadcrumb = "Inspector / ";
    Breadcrumb += Entry.CategoryLabel.empty() ? "General" : Entry.CategoryLabel;

    WorkspacePaneAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX, CurY + 4.0f },
        InnerW - 96.0f,
        9.0f,
        WorkspaceSurfaceColors::MutedInk,
        Breadcrumb);

    if (!NotesBoxExpanded)
    {
        RenderNotesControl(Surface, LeftX, CurY, InnerW, true, LeftX + InnerW, CurY, Entry, Input);
    }

    WorkspacePaneAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Light,
        { LeftX, CurY + 34.0f },
        InnerW,
        25.0f,
        WorkspaceSurfaceColors::PrimaryInk,
        Entry.Title);

    WorkspacePaneAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX, CurY + 67.0f },
        InnerW,
        10.5f,
        WorkspaceSurfaceColors::MutedInk,
        Entry.Subtitle.empty() ? Entry.CategoryLabel : Entry.Subtitle);

    CurY += 96.0f;

    if (NotesBoxExpanded)
    {
        CurY += RenderNotesControl(Surface, LeftX, CurY, InnerW, false, 0.0f, 0.0f, Entry, Input);
        CurY += 8.0f;
    }

    // 2. PROPERTIES section caption + Reset capsule button
    WorkspacePaneAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Medium,
        { LeftX, CurY + 8.0f },
        10.0f,
        ColorRgba{ 168, 168, 168, 255 },
        "PROPERTIES");

    const char* SubCaption = "Capabilities & spatial transform";
    const float SubCapW    = WorkspacePaneAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, SubCaption);
    WorkspacePaneAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX + InnerW - SubCapW, CurY + 8.0f },
        10.0f,
        ColorRgba{ 142, 142, 142, 255 },
        SubCaption);
    CurY += 30.0f;

    // 3. 2x2 Quick Controls Tile Grid (matching reference layout Enabled / Static tiles)
    {
        const float TileGap = 10.0f;
        const float TileW   = (InnerW - TileGap) * 0.5f;
        const float TileH   = 104.0f;

        auto ConstructCapabilityTile = [&](Rectangle2D Tile, const char* Label, CapabilityIconId CapabilityIcon, bool& CapabilityToggle)
        {
            const bool Hovered = ScrollViewport.Contains(Cursor) && Tile.Contains(Cursor);
            Surface.FillRectangle(
                Tile,
                Hovered ? ColorRgba{ 33, 33, 33, 255 } : ColorRgba{ 26, 26, 26, 255 },
                20.0f);
            Surface.StrokeRectangle(
                Tile,
                CapabilityToggle ? ColorRgba{ 88, 153, 121, 95 } : ColorRgba{ 255, 255, 255, 18 },
                20.0f,
                1.0f);

            const ColorRgba   SeatFill = CapabilityToggle ? ColorRgba{ 32, 68, 46, 255 } : ColorRgba{ 38, 38, 38, 255 };
            const Rectangle2D Seat{ Tile.Center().X - 19.0f, Tile.Y + 15.0f, 38.0f, 36.0f };
            Surface.FillRectangle(Seat, SeatFill, 11.0f);
            WorkspacePaneAppearance::ConstructCapabilityIcon(
                Surface,
                { Seat.X + 9.0f, Seat.Y + 8.0f },
                20.0f,
                CapabilityIcon,
                CapabilityToggle ? ColorRgba{ 102, 214, 138, 255 } : ColorRgba{ 176, 132, 116, 255 },
                SeatFill);

            const float LabelW = WorkspacePaneAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.5f, Label);
            WorkspacePaneAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { Tile.Center().X - LabelW * 0.5f, Tile.Y + 60.0f },
                11.5f,
                CapabilityToggle ? WorkspaceSurfaceColors::PrimaryInk : ColorRgba{ 186, 186, 186, 255 },
                Label);

            const char* StatusStr = CapabilityToggle ? "ON" : "OFF";
            const float StatusW   = WorkspacePaneAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.5f, StatusStr);
            WorkspacePaneAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Medium,
                { Tile.Center().X - StatusW * 0.5f, Tile.Y + 79.0f },
                8.5f,
                CapabilityToggle ? WorkspaceSurfaceColors::StatusGreen : ColorRgba{ 188, 122, 102, 255 },
                StatusStr);

            if (Hovered && Input.PrimaryPointer.Pressed)
            {
                CapabilityToggle = !CapabilityToggle;
            }
        };

        ConstructCapabilityTile({ LeftX, CurY, TileW, TileH }, "Enabled", CapabilityIconId::Visibility, Entry.VisibilityEnabled);
        ConstructCapabilityTile({ LeftX + TileW + TileGap, CurY, TileW, TileH }, "Cast shadows", CapabilityIconId::ShadowCaster, Entry.CastShadows);
        CurY += TileH + TileGap;

        ConstructCapabilityTile({ LeftX, CurY, TileW, TileH }, "Dynamic", CapabilityIconId::DynamicMotion, Entry.DynamicMotion);
        ConstructCapabilityTile({ LeftX + TileW + TileGap, CurY, TileW, TileH }, "Locked", CapabilityIconId::TransformLock, Entry.Locked);
        CurY += TileH + 16.0f;
    }

    // 4. Transform section (#1A1A1A, 22px radius) with interactive X/Y/Z sliders + Reset button
    {
        const Rectangle2D TransformSection{ LeftX, CurY, InnerW, 186.0f };
        Surface.FillRectangle(TransformSection, WorkspaceSurfaceColors::GenericSurface, 22.0f);
        Surface.StrokeRectangle(TransformSection, WorkspaceSurfaceColors::HairlineLight, 22.0f, 1.0f);

        WorkspacePaneAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Medium,
            { TransformSection.X + 18.0f, TransformSection.Y + 18.0f },
            10.0f,
            ColorRgba{ 148, 148, 148, 255 },
            "TRANSFORM");

        const Rectangle2D ResetBtn{ TransformSection.Right() - 82.0f, TransformSection.Y + 13.0f, 64.0f, 24.0f };
        const bool        ResetHover = ScrollViewport.Contains(Cursor) && ResetBtn.Contains(Cursor);
        Surface.FillRectangle(ResetBtn, ResetHover ? ColorRgba{ 46, 46, 46, 255 } : ColorRgba{ 36, 36, 36, 255 }, 12.0f);
        WorkspacePaneAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { ResetBtn.X + 17.0f, ResetBtn.Y + 6.0f },
            10.0f,
            ColorRgba{ 192, 192, 192, 255 },
            "Reset");

        if (ResetHover && Input.PrimaryPointer.Pressed && !Entry.Locked)
        {
            Entry.PositionX = 0.0f;
            Entry.PositionY = 0.0f;
            Entry.PositionZ = 0.0f;
            FormatTransformSubtitle(Entry);
        }

        auto ConstructAxisRow = [&](const char* AxisLabel, float RowTopY, float& Coordinate)
        {
            WorkspacePaneAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { TransformSection.X + 18.0f, RowTopY + 7.0f },
                11.0f,
                ColorRgba{ 184, 184, 184, 255 },
                AxisLabel);

            char CoordinateText[32]{};
            std::snprintf(CoordinateText, sizeof(CoordinateText), "%.1f m", Coordinate);
            const float ValW = WorkspacePaneAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.5f, CoordinateText);
            WorkspacePaneAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { TransformSection.Right() - 18.0f - ValW, RowTopY + 7.0f },
                10.5f,
                ColorRgba{ 196, 196, 196, 255 },
                CoordinateText);

            const Rectangle2D Track{
                TransformSection.X + 86.0f,
                RowTopY + 5.0f,
                std::max(40.0f, TransformSection.Width - 172.0f),
                18.0f
            };
            const float Norm = std::clamp((Coordinate + 50.0f) / 100.0f, 0.0f, 1.0f);
            Surface.FillRectangle(Track, ColorRgba{ 36, 36, 36, 255 }, 9.0f);
            Surface.FillRectangle({ Track.X, Track.Y, std::max(14.0f, Track.Width * Norm), Track.Height }, ColorRgba{ 69, 69, 69, 255 }, 9.0f);
            Surface.FillCircle({ Track.X + 9.0f + (Track.Width - 18.0f) * Norm, Track.Center().Y }, 7.5f, ColorRgba{ 224, 224, 224, 255 });

            if (!Entry.Locked && ScrollViewport.Contains(Cursor) && Track.Contains(Cursor) && Input.PrimaryPointer.Down)
            {
                const float NextNorm = std::clamp((Cursor.X - Track.X) / Track.Width, 0.0f, 1.0f);
                Coordinate           = std::round((NextNorm * 100.0f - 50.0f) * 2.0f) * 0.5f;
                FormatTransformSubtitle(Entry);
            }
        };

        ConstructAxisRow("Position X", TransformSection.Y + 50.0f, Entry.PositionX);
        ConstructAxisRow("Position Y", TransformSection.Y + 92.0f, Entry.PositionY);
        ConstructAxisRow("Position Z", TransformSection.Y + 134.0f, Entry.PositionZ);

        CurY += TransformSection.Height + 28.0f;
    }

    return CurY - OriginY;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            TOP-LEVEL INSPECTOR PRESENTATION
//------------------------------------------------------------------------------------------------------------------------

void InspectorPanel::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  Δt)
{
    if (Bounds.Width <= 4.0f || Bounds.Height <= 4.0f)
    {
        return;
    }

    WorkspacePaneAppearance::ForwardInputToTextBuffer(NotesEditor, Input, Δt);
    WorkspacePaneAppearance::ForwardInputToTextBuffer(FolderSearchEditor, Input, Δt);

    const Point2D     Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    constexpr float   FooterHeight = 45.0f;
    const Rectangle2D ScrollViewport{
        Bounds.X,
        Bounds.Y,
        Bounds.Width,
        std::max(0.0f, Bounds.Height - FooterHeight)
    };

    if (ScrollViewport.Contains(Cursor) && std::fabs(Input.WheelDeltaY) > 1.0e-4f)
    {
        ScrollOffsetY -= Input.WheelDeltaY * 38.0f;
    }
    ScrollOffsetY = std::max(0.0f, ScrollOffsetY);
    if (LastContentHeight > 0.0f)
    {
        ScrollOffsetY = std::min(ScrollOffsetY, std::max(0.0f, LastContentHeight - ScrollViewport.Height));
    }

    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, WorkspaceSurfaceColors::DockBackground);

    const std::span<const OutlinerSlotToken> Selected =
        (AttachedOutliner != nullptr) ? AttachedOutliner->QuerySelectedTokens() : std::span<const OutlinerSlotToken>{};

    const OutlinerSlotToken PrimaryToken = (Selected.size() == 1u) ? Selected.front() : OutlinerSlotToken{};
    OutlinerRecordEntry*    PrimaryEntry = (AttachedOutliner != nullptr) ? AttachedOutliner->AccessEntry(PrimaryToken) : nullptr;
    SyncNotesBufferWithSelection(PrimaryToken, PrimaryEntry);

    Surface.PushClip(ScrollViewport);

    std::string FooterLeftLabel = "No selection";
    if (Selected.empty() || AttachedOutliner == nullptr)
    {
        ScrollOffsetY     = 0.0f;
        LastContentHeight = ScrollViewport.Height;

        const float LeftX = ScrollViewport.X + 28.0f;
        const float TopY  = ScrollViewport.Y + 30.0f;
        WorkspacePaneAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { LeftX, TopY },
            9.0f,
            WorkspaceSurfaceColors::MutedInk,
            "Inspector");
        WorkspacePaneAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Light,
            { LeftX, TopY + 30.0f },
            25.0f,
            WorkspaceSurfaceColors::PrimaryInk,
            "No selection");
        WorkspacePaneAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { LeftX, TopY + 66.0f },
            std::max(60.0f, ScrollViewport.Width - 56.0f),
            11.0f,
            WorkspaceSurfaceColors::MutedInk,
            "Select an entry or folder in the Outliner to inspect properties.");
    }
    else if (Selected.size() > 1u)
    {
        ScrollOffsetY     = 0.0f;
        LastContentHeight = ScrollViewport.Height;

        const float LeftX = ScrollViewport.X + 26.0f;
        const float TopY  = ScrollViewport.Y + 28.0f;
        WorkspacePaneAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { LeftX, TopY },
            9.0f,
            WorkspaceSurfaceColors::MutedInk,
            "Inspector / Multi-selection");

        char Headline[64]{};
        std::snprintf(Headline, sizeof(Headline), "%zu Selected", Selected.size());
        FooterLeftLabel = Headline;

        WorkspacePaneAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Light,
            { LeftX, TopY + 30.0f },
            25.0f,
            WorkspaceSurfaceColors::PrimaryInk,
            Headline);
        WorkspacePaneAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { LeftX, TopY + 64.0f },
            10.5f,
            WorkspaceSurfaceColors::MutedInk,
            "Shift / Ctrl selection active");

        const float       InnerW = std::max(100.0f, ScrollViewport.Width - 52.0f);
        const Rectangle2D BatchSection{ LeftX, TopY + 96.0f, InnerW, 32.0f + static_cast<float>(std::min<std::size_t>(Selected.size(), 8u)) * 30.0f };
        Surface.FillRectangle(BatchSection, WorkspaceSurfaceColors::GenericSurface, 20.0f);
        Surface.StrokeRectangle(BatchSection, WorkspaceSurfaceColors::HairlineLight, 20.0f, 1.0f);

        float RowY = BatchSection.Y + 16.0f;
        for (std::size_t Idx = 0u; Idx < std::min<std::size_t>(Selected.size(), 8u); ++Idx)
        {
            if (const OutlinerRecordEntry* SubEntry = AttachedOutliner->FindEntry(Selected[Idx]))
            {
                WorkspacePaneAppearance::ConstructSlotCategoryIcon(Surface, SubEntry->Category, { BatchSection.X + 16.0f, RowY + 2.0f }, 18.0f, SubEntry->FolderTint, 1.0f);
                WorkspacePaneAppearance::ConstructTruncatedText(
                    Surface,
                    FontWeightClass::Regular,
                    { BatchSection.X + 42.0f, RowY + 4.0f },
                    BatchSection.Width - 58.0f,
                    11.5f,
                    WorkspaceSurfaceColors::PrimaryInk,
                    SubEntry->Title);
                RowY += 30.0f;
            }
        }
    }
    else if (PrimaryEntry != nullptr)
    {
        FooterLeftLabel = PrimaryEntry->CategoryLabel.empty() ? PrimaryEntry->Title : PrimaryEntry->CategoryLabel;
        if (PrimaryEntry->Category == ConstructSlotCategory::Folder)
        {
            LastContentHeight = RenderFolderInspector(Surface, ScrollViewport, ScrollViewport.Y - ScrollOffsetY, *PrimaryEntry, Input);
        }
        else
        {
            ScopeDropdownOpen = false;
            SortDropdownOpen  = false;
            LastContentHeight = RenderStandardEntryInspector(Surface, ScrollViewport, ScrollViewport.Y - ScrollOffsetY, *PrimaryEntry, Input);
        }
    }

    const float MaxScrollAfter = std::max(0.0f, LastContentHeight - ScrollViewport.Height);
    ScrollOffsetY              = std::clamp(ScrollOffsetY, 0.0f, MaxScrollAfter);
    WorkspacePaneAppearance::ConstructVerticalScrollIndicator(Surface, ScrollViewport, LastContentHeight, ScrollOffsetY, Cursor);
    Surface.PopClip();

    // workspace Inspector Footer (.inspector-footer: 45px height, #151515)
    const Rectangle2D FooterBounds{ Bounds.X, Bounds.Bottom() - FooterHeight, Bounds.Width, FooterHeight };
    Surface.FillRectangle(FooterBounds, WorkspaceSurfaceColors::FooterBackground);
    Surface.StrokeLine(
        { FooterBounds.X, FooterBounds.Y },
        { FooterBounds.Right(), FooterBounds.Y },
        WorkspaceSurfaceColors::HairlineLight,
        1.0f);

    WorkspacePaneAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { FooterBounds.X + 14.0f, FooterBounds.Y + 16.5f },
        FooterBounds.Width * 0.55f,
        10.0f,
        ColorRgba{ 112, 112, 112, 255 },
        FooterLeftLabel);

    const char* RightTag = "Native workspace";
    const float RightW   = WorkspacePaneAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, RightTag);
    WorkspacePaneAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { FooterBounds.Right() - 14.0f - RightW, FooterBounds.Y + 16.5f },
        10.0f,
        ColorRgba{ 112, 112, 112, 255 },
        RightTag);

    // Reusable Studio Controls Scope & Sort dropdown option sheets (above scroll clip, zero shadow)
    if (ScopeDropdownOpen)
    {
        constexpr std::array<const char*, 2> ScopeOptions = {{ "Scope: Nested", "Scope: Direct" }};
        const Rectangle2D SheetBounds = DropdownSelector::QuerySheetBounds(ScopeButtonBounds, static_cast<uint32_t>(ScopeOptions.size()));
        const uint32_t    CurrentIdx  = (BrowseScope == FolderBrowseScope::NestedAll) ? 0u : 1u;
        uint32_t          ChosenIdx   = CurrentIdx;
        if (DropdownSelector::ConstructOptionSheet(
                Surface,
                ScopeButtonBounds,
                ScopeOptions.data(),
                static_cast<uint32_t>(ScopeOptions.size()),
                CurrentIdx,
                Input.PrimaryPointer,
                ChosenIdx).Clicked)
        {
            BrowseScope       = (ChosenIdx == 0u) ? FolderBrowseScope::NestedAll : FolderBrowseScope::DirectOnly;
            ScopeDropdownOpen = false;
        }
        else if (Input.PrimaryPointer.Pressed && !SheetBounds.Contains(Cursor) && !ScopeButtonBounds.Contains(Cursor))
        {
            ScopeDropdownOpen = false;
        }
    }

    if (SortDropdownOpen)
    {
        constexpr std::array<const char*, 2> SortOptions = {{ "Sort: Order", "Sort: Name" }};
        const Rectangle2D SheetBounds = DropdownSelector::QuerySheetBounds(SortButtonBounds, static_cast<uint32_t>(SortOptions.size()));
        const uint32_t    CurrentIdx  = (BrowseSort == FolderBrowseSort::OutlinerOrder) ? 0u : 1u;
        uint32_t          ChosenIdx   = CurrentIdx;
        if (DropdownSelector::ConstructOptionSheet(
                Surface,
                SortButtonBounds,
                SortOptions.data(),
                static_cast<uint32_t>(SortOptions.size()),
                CurrentIdx,
                Input.PrimaryPointer,
                ChosenIdx).Clicked)
        {
            BrowseSort       = (ChosenIdx == 0u) ? FolderBrowseSort::OutlinerOrder : FolderBrowseSort::Alphabetical;
            SortDropdownOpen = false;
        }
        else if (Input.PrimaryPointer.Pressed && !SheetBounds.Contains(Cursor) && !SortButtonBounds.Contains(Cursor))
        {
            SortDropdownOpen = false;
        }
    }

    Surface.PopClip();
}

void InspectorPanel::PresentInImGuiPane(double Δt, float ScaleFactor)
{
    if (ImGui::GetCurrentContext() == nullptr)
    {
        return;
    }

    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton("##FrontierInspectorCanvas", ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)));

    const Rectangle2D ScreenBounds{ WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };

    PixelSpace             WindowSurface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    const PaneInputReading Input = WorkspacePaneAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    Render(WindowSurface, LogicalBounds, Input, Δt);
}

} // namespace Frontier
