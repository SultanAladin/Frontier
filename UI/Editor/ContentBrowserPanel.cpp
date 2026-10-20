/*==========================================================================================================================================
                                                         CONTENTBROWSERPANEL.CPP
==========================================================================================================================================*/
// 🧩 Adapt pinned content-browser navigation to real, root-confined files without importing scene or painting code.

#include "ContentBrowserPanel.h"
#include "Engine/Interface/IconPresentation.h"
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <utility>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                       LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

ContentBrowserPanel::ContentBrowserPanel(std::filesystem::path Requested) : Root(std::move(Requested))
{
}

bool ContentBrowserPanel::InitializeContent() noexcept
{
    try
    {
        // An unavailable content root is a visible browser diagnostic, not a failure of the editor host.
        (void)Directory.ConfigureRoot(Root);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void ContentBrowserPanel::IntegrateContent(double Δτ) noexcept
{
    Expansion.Integrate(Δτ);
    Search.Advance(static_cast<float>(std::min(Δτ, 0.1)));
}

void ContentBrowserPanel::FinalizeContent() noexcept
{
    Search.Active = false;
    PressedPath.clear();
    Expansion.ConfigureOpen(false);
}

void ContentBrowserPanel::ConfigureOpen(bool Open) noexcept
{
    Expansion.ConfigureOpen(Open);
    if (!Open)
    {
        Search.Active = false;
        CategoryMenu  = false;
        PressedPath.clear();
    }
}

PlaneExtent ContentBrowserPanel::PhysicalExtent(PlaneExtent Extent) const noexcept
{
    return {Extent.MinimumX * Scale, Extent.MinimumY * Scale, Extent.MaximumX * Scale, Extent.MaximumY * Scale};
}

PlaneExtent ContentBrowserPanel::QueryEntryExtent(size_t Index) const noexcept
{
    return Index < EntryExtents.size() ? EntryExtents[Index] : PlaneExtent{};
}

void ContentBrowserPanel::ResetLocation()
{
    Scroll = 0;
    PressedPath.clear();
    Search.Active = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SEARCH AND KEYS
//------------------------------------------------------------------------------------------------------------------------

void ContentBrowserPanel::IntegrateSearch()
{
    auto &IO = ImGui::GetIO();
    if (!Search.Active)
        return;
    IO.WantTextInput = true;
    ImGui::SetNextFrameWantCaptureKeyboard(true);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        Search.Cancel();
    else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false))
        Search.Commit();
    else
    {
        if ((IO.KeyCtrl || IO.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_A, false))
            Search.SelectAll();
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace))
            Search.Backspace();
        if (ImGui::IsKeyPressed(ImGuiKey_Delete))
            Search.Delete();
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
            Search.MoveCaret(-1, IO.KeyShift);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
            Search.MoveCaret(1, IO.KeyShift);
        if (ImGui::IsKeyPressed(ImGuiKey_Home))
            Search.MoveHome(IO.KeyShift);
        if (ImGui::IsKeyPressed(ImGuiKey_End))
            Search.MoveEnd(IO.KeyShift);
        if (!IO.KeyCtrl && !IO.KeySuper && !IO.KeyAlt)
            for (const auto Character : IO.InputQueueCharacters)
            {
                if (Character < 32 || Character == 127)
                    continue;
                char UTF8[5]{};
                if (Character < 0x80)
                    UTF8[0] = static_cast<char>(Character);
                else if (Character < 0x800)
                {
                    UTF8[0] = static_cast<char>(0xC0 | (Character >> 6));
                    UTF8[1] = static_cast<char>(0x80 | (Character & 0x3F));
                }
                else if (Character < 0x10000)
                {
                    UTF8[0] = static_cast<char>(0xE0 | (Character >> 12));
                    UTF8[1] = static_cast<char>(0x80 | ((Character >> 6) & 0x3F));
                    UTF8[2] = static_cast<char>(0x80 | (Character & 0x3F));
                }
                else
                {
                    UTF8[0] = static_cast<char>(0xF0 | (Character >> 18));
                    UTF8[1] = static_cast<char>(0x80 | ((Character >> 12) & 0x3F));
                    UTF8[2] = static_cast<char>(0x80 | ((Character >> 6) & 0x3F));
                    UTF8[3] = static_cast<char>(0x80 | (Character & 0x3F));
                }
                Search.Insert(UTF8);
            }
    }
    Directory.ConfigureFilter(Category, Search.Text);
    Scroll = 0;
}

void ContentBrowserPanel::IntegrateKeyboard()
{
    const auto &IO = ImGui::GetIO();
    if (!Focused || Search.Active || IO.WantTextInput)
        return;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        if (CategoryMenu)
            CategoryMenu = false;
        else if (!HostedPresentation)
            ConfigureOpen(false);
        else
            Focused = false;
        return;
    }
    if ((IO.KeyCtrl || IO.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_F, false))
    {
        Search.Begin(Search.Text);
        return;
    }
    bool Navigated = false;
    if (IO.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false))
        Navigated = Directory.NavigateBack();
    else if (IO.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_RightArrow, false))
        Navigated = Directory.NavigateForward();
    else if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
        Navigated = Directory.NavigateUp();
    else if (ImGui::IsKeyPressed(ImGuiKey_F5, false))
        Navigated = Directory.Refresh();
    if (Navigated)
    {
        ResetLocation();
        return;
    }
    if (!HostedPresentation && !Expansion.QueryOpen())
        return;
    const auto &Visible = Directory.QueryVisible();
    if (Visible.empty())
        return;
    const auto *Selected = Directory.QuerySelection();
    size_t      Ordinal  = 0;
    for (; Ordinal < Visible.size(); ++Ordinal)
        if (&Directory.QueryEntries()[Visible[Ordinal]] == Selected)
            break;
    int Step = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        Step = static_cast<int>(Columns);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        Step = -static_cast<int>(Columns);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && !IO.KeyAlt)
        Step = 1;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && !IO.KeyAlt)
        Step = -1;
    if (Step)
    {
        const auto Next =
            Selected ? std::clamp(static_cast<int>(Ordinal) + Step, 0, static_cast<int>(Visible.size()) - 1) : 0;
        (void)Directory.Select(Directory.QueryEntries()[Visible[static_cast<size_t>(Next)]].Path);
        const float RowTop     = static_cast<float>(Next / static_cast<int>(Columns)) * CellHeight;
        const float ViewHeight = BodyExtent.Height() / Scale;
        if (RowTop < Scroll)
            Scroll = RowTop;
        if (RowTop + CellHeight > Scroll + ViewHeight)
            Scroll = RowTop + CellHeight - ViewHeight;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && Selected && Selected->Category == ContentCategory::Directory)
    {
        const auto Path = Selected->Path;
        if (Directory.Navigate(Path))
            ResetLocation();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     FILES AND DETAILS
//------------------------------------------------------------------------------------------------------------------------

void ContentBrowserPanel::ConstructSelection(PixelSpace &Surface, const PlaneExtent &Extent)
{
    const auto &Palette = ControlKit::Palette();
    Surface.FillRectangle(Extent, Palette.CardSub, 8);
    Surface.PushClip(Extent);
    const auto *Selected = Directory.QuerySelection();
    const auto  Name     = Spanning(Extent.MinimumX + 12, Extent.MinimumY + 8, Extent.Width() - 24, 22);
    ControlKit::TextLeading(Surface, Name, 0, Palette.Text, Selected ? Selected->Name.c_str() : "Selection", 13);
    const auto        Description = Spanning(Name.MinimumX, Name.MaximumY + 2, Name.Width(), 20);
    const std::string Detail =
        Selected ? std::string(QueryContentCategoryLabel(Selected->Category)) +
                       (Selected->ByteCountKnown ? "  /  " + std::to_string(Selected->ByteCount) + " bytes" : "")
                 : "Select a file or folder";
    ControlKit::TextLeading(Surface, Description, 0, Palette.TextDim, Detail.c_str(), 11);
    if (Extent.Height() > 100)
    {
        const std::string Path =
            Selected ? EncodeContentPath(Selected->Path.lexically_relative(Directory.QueryRoot())) : "";
        ControlKit::TextLeading(Surface, Spanning(Name.MinimumX, Name.MinimumY + 70, Name.Width(), 24), 0,
                                Palette.TextDim, "Path relative to content root", 11);
        ControlKit::TextLeading(Surface, Spanning(Name.MinimumX, Name.MinimumY + 96, Name.Width(), 24), 0, Palette.Text,
                                Path.c_str(), 11);
        ControlKit::TextLeading(Surface, Spanning(Name.MinimumX, Name.MinimumY + 140, Name.Width(), 24), 0,
                                Palette.TextDim, "Read-only / file details only", 11);
        ControlKit::TextLeading(Surface, Spanning(Name.MinimumX, Name.MinimumY + 166, Name.Width(), 24), 0,
                                Palette.TextDim, "No import or content rendering", 11);
    }
    Surface.PopClip();
}

void ContentBrowserPanel::ConstructEntries(PixelSpace &Surface, const PlaneExtent &View, const ControlPointer &Pointer)
{
    const auto &Palette       = ControlKit::Palette();
    BodyExtent                = PhysicalExtent(View);
    Columns                   = Grid ? std::max(1u, static_cast<uint32_t>(View.Width() / 140)) : 1;
    CellWidth                 = View.Width() / static_cast<float>(Columns);
    CellHeight                = Grid ? 116 : 42;
    const auto &Visible       = Directory.QueryVisible();
    const float ContentHeight = static_cast<float>((Visible.size() + Columns - 1) / Columns) * CellHeight;
    Scroll = ControlKit::AdvanceScroll(Scroll, ControlKit::Over(View, Pointer) ? Pointer.Wheel : 0, ContentHeight,
                                       View.Height());
    EntryExtents.assign(Visible.size(), {});
    Surface.PushClip(View);
    std::filesystem::path NavigateTo;
    const auto           *Selection = Directory.QuerySelection();
    for (size_t Ordinal = 0; Ordinal < Visible.size(); ++Ordinal)
    {
        const float Y = View.MinimumY + static_cast<float>(Ordinal / Columns) * CellHeight - Scroll;
        if (Y + CellHeight <= View.MinimumY || Y >= View.MaximumY)
            continue;
        const auto &Entry     = Directory.QueryEntries()[Visible[Ordinal]];
        const float X         = View.MinimumX + static_cast<float>(Ordinal % Columns) * CellWidth;
        const auto  Card      = Spanning(X + 3, Y + 3, CellWidth - 8, CellHeight - 8);
        EntryExtents[Ordinal] = PhysicalExtent(Card);
        const bool Hovered    = ControlKit::Over(Card, Pointer) && ControlKit::Over(View, Pointer);
        const bool Selected   = Selection && Selection->Path == Entry.Path;
        Surface.FillRectangle(Card, Selected ? Palette.AccentSoft : Hovered ? Palette.Selected : Palette.Card, 8);
        if (Selected)
            ControlKit::OutlineRounded(Surface, Card, Palette.Accent, 8);
        const auto  Icon      = Entry.Category == ContentCategory::Directory ? ControlCentreIconCategory::FolderClosed
                                                                             : ControlCentreIconCategory::CircleInfo;
        const auto  Glyph     = Grid ? Spanning(Card.MinimumX, Card.MinimumY + 12, Card.Width(), 38)
                                     : Spanning(Card.MinimumX + 6, Card.MinimumY, 28, Card.Height());
        const float GlyphSize = Grid ? 28.0f : 18.0f;
        const bool  FileGlyph =
            Entry.Category == ContentCategory::VectorImage &&
            IconPresentation::Draw(ImGui::GetWindowDrawList(), IconSymbol::ContentFileSvg,
                                   ImVec2((Glyph.MinimumX + (Glyph.Width() - GlyphSize) * 0.5f) * Scale,
                                          (Glyph.MinimumY + (Glyph.Height() - GlyphSize) * 0.5f) * Scale),
                                   GlyphSize * Scale);
        if (!FileGlyph)
            ControlKit::GlyphCentred(Surface, Glyph, GlyphSize, Selected ? Palette.Accent : Palette.TextDim, Icon);
        const auto Name = Grid ? Spanning(Card.MinimumX + 8, Card.MinimumY + 58, Card.Width() - 16, 20)
                               : Spanning(Card.MinimumX + 40, Card.MinimumY, Card.Width() - 50, Card.Height());
        Surface.PushClip(Name);
        ControlKit::TextLeading(Surface, Name, 0, Palette.Text, Entry.Name.c_str(), 12);
        Surface.PopClip();
        if (Grid)
        {
            const auto Type = Spanning(Card.MinimumX + 8, Card.MinimumY + 82, Card.Width() - 16, 18);
            Surface.PushClip(Type);
            ControlKit::TextLeading(Surface, Type, 0, Palette.TextDim, QueryContentCategoryLabel(Entry.Category), 10);
            Surface.PopClip();
        }
        if (Hovered && Pointer.Pressed)
            PressedPath = Entry.Path;
        if (Hovered && Pointer.Released && PressedPath == Entry.Path)
            (void)Directory.Select(Entry.Path);
        if (Hovered && Pointer.Pressed && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
            Entry.Category == ContentCategory::Directory)
            NavigateTo = Entry.Path;
    }
    if (Visible.empty())
        ControlKit::TextCentred(
            Surface, View, Palette.TextDim,
            Directory.QueryDiagnostic().empty() ? "No matching files in this folder" : "Directory unavailable", 13);
    Surface.PopClip();
    ControlKit::ScrollIndicator(Surface, View, Scroll, ContentHeight);
    if (Pointer.Released)
        PressedPath.clear();
    if (!NavigateTo.empty() && Directory.Navigate(NavigateTo))
        ResetLocation();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        PANEL LAYOUT
//------------------------------------------------------------------------------------------------------------------------

void ContentBrowserPanel::ConstructLayout(PixelSpace &Surface, const PlaneExtent &Pane, const ControlPointer &Pointer)
{
    const auto &Palette = ControlKit::Palette();
    const float Left    = Pane.MinimumX + 12;
    const float Right   = Pane.MaximumX - 12;
    const float Width   = std::max(1.0f, Right - Left);
    const bool  Wide    = Width >= 840;
    if (Wide)
        CategoryMenu = false;
    auto       ChromePointer = Pointer;
    const bool MenuWasOpen   = CategoryMenu;
    ChromePointer.Enabled    = Pointer.Enabled && !CategoryMenu;
    Surface.FillRectangle(Pane, Palette.Field);
    Surface.PushClip(Pane);
    HeaderContact =
        !CategoryMenu && ControlKit::Over(Spanning(Left, Pane.MinimumY, std::max(1.0f, Width - 124), 44), Pointer);
    ControlKit::TextLeading(Surface, Spanning(Left, Pane.MinimumY + 8, Width < 450 ? 100 : 200, 30), 0, Palette.Text,
                            Width < 450 ? "Content" : "Content Browser", 14);
    const auto Button = [&](const PlaneExtent &Extent, const char *Label, bool Disabled = false)
    {
        ButtonStructure Description{Label, ButtonToneCategory::Secondary};
        Description.Disabled = Disabled;
        Description.Height   = Extent.Height();
        Description.FontSize = 12;
        return ControlKit::PillButton(Surface, Extent, Description, ChromePointer).Clicked;
    };
    if (!HostedPresentation)
    {
        const auto SizeButton =
            Spanning(Right - (Width < 450 ? 100 : 116), Pane.MinimumY + 8, Width < 450 ? 60 : 76, 28);
        SizeExtent = PhysicalExtent(SizeButton);
        if (Button(SizeButton, Width < 450 ? "Size" : Maximized ? "Restore" : "Maximize"))
            Maximized = !Maximized;
        if (ControlKit::RoundIconButton(Surface, Right - 16, Pane.MinimumY + 22, ControlCentreIconCategory::CloseCross,
                                        ChromePointer)
                .Clicked)
            ConfigureOpen(false);
    }
    const float NavigationY    = Pane.MinimumY + 48;
    const float ButtonWidths[] = {28, 28, 36, 52, 68};
    const char *Labels[]       = {"<", ">", "Up", "Root", "Reload"};
    float       X              = Left;
    for (size_t Index = 0; Index < NavigationExtents.size(); ++Index)
    {
        const auto Extent        = Spanning(X, NavigationY, ButtonWidths[Index], 28);
        NavigationExtents[Index] = PhysicalExtent(Extent);
        const bool Disabled      = Index == 0   ? !Directory.QueryCanGoBack()
                                   : Index == 1 ? !Directory.QueryCanGoForward()
                                   : Index == 2 ? Directory.QueryDirectory() == Directory.QueryRoot()
                                                : false;
        if (Button(Extent, Labels[Index], Disabled))
        {
            const bool Changed = Index == 0                      ? Directory.NavigateBack()
                                 : Index == 1                    ? Directory.NavigateForward()
                                 : Index == 2                    ? Directory.NavigateUp()
                                 : Index == 3                    ? Directory.Navigate(Directory.QueryRoot())
                                 : Directory.QueryRoot().empty() ? Directory.ConfigureRoot(Root)
                                                                 : Directory.Refresh();
            if (Changed)
                ResetLocation();
        }
        X += ButtonWidths[Index] + 4;
    }
    const auto        Location   = Spanning(Left, NavigationY + 34, Width, 24);
    const auto        Relative   = Directory.QueryDirectory().lexically_relative(Directory.QueryRoot());
    const std::string Breadcrumb = EncodeContentPath(Directory.QueryRoot().filename()) + " / " +
                                   (Relative == "." ? "" : EncodeContentPath(Relative));
    Surface.PushClip(Location);
    ControlKit::TextLeading(Surface, Location, 0, Palette.TextDim, Breadcrumb.c_str(), 12);
    Surface.PopClip();
    const auto SearchBox = Spanning(Left, Location.MaximumY + 6, std::max(40.0f, Width - 120), 32);
    SearchExtent         = PhysicalExtent(SearchBox);
    if (ControlKit::Over(SearchBox, ChromePointer) && Pointer.Pressed)
    {
        Search.Begin(Search.Text);
        CategoryMenu = false;
    }
    else if (Pointer.Pressed && Search.Active)
        Search.Commit();
    IntegrateSearch();
    (void)ControlKit::TextEntry(Surface, SearchBox, Search, Pointer, 12);
    if (!Search.Active && Search.Length == 0)
        ControlKit::TextLeading(Surface, SearchBox, 8, Palette.TextDim, "Search this folder...", 12);
    const auto View          = Spanning(Right - 110, SearchBox.MinimumY, 110, 32);
    ViewExtent               = PhysicalExtent(View);
    const char *ViewLabels[] = {"Grid", "List"};
    uint32_t    SelectedView = Grid ? 0 : 1;
    if (ControlKit::Segmented(Surface, View, ViewLabels, 2, SelectedView, ChromePointer, SelectedView).Clicked)
    {
        Grid   = SelectedView == 0;
        Scroll = 0;
    }
    const auto FilterBox = Spanning(Left, SearchBox.MaximumY + 8, std::min(160.0f, Width - 90), 30);
    FilterExtent         = PhysicalExtent(FilterBox);
    if (!Wide &&
        ControlKit::Dropdown(Surface, FilterBox, QueryContentCategoryLabel(Category), CategoryMenu, ChromePointer)
            .Clicked)
        CategoryMenu = !CategoryMenu;
    const auto Count = std::to_string(Directory.QueryVisible().size()) + " entries";
    ControlKit::TextLeading(Surface,
                            Spanning(Wide ? Left : FilterBox.MaximumX + 12, FilterBox.MinimumY,
                                     Wide ? Width : Right - FilterBox.MaximumX - 12, 30),
                            0, Palette.TextDim, Count.c_str(), 12);
    const float BodyY      = FilterBox.MaximumY + 10;
    const float BodyBottom = std::max(BodyY + 1, Pane.MaximumY - 34 - (Wide ? 0 : 68));
    const auto  Files      = Spanning(Wide ? Left + 156 : Left, BodyY, std::max(1.0f, Width - (Wide ? 396 : 0)),
                                std::max(1.0f, BodyBottom - BodyY));
    CategoryExtents.fill({});
    if (Wide)
        for (size_t Index = 0; Index < CategoryExtents.size(); ++Index)
        {
            const auto Extent      = Spanning(Left, BodyY + static_cast<float>(Index) * 38, 144, 32);
            CategoryExtents[Index] = PhysicalExtent(Extent);
            if (ControlKit::ChipButton(Surface, Extent, QueryContentCategoryLabel(static_cast<ContentCategory>(Index)),
                                       Category == static_cast<ContentCategory>(Index), Pointer)
                    .Clicked)
            {
                Category = static_cast<ContentCategory>(Index);
                Directory.ConfigureFilter(Category, Search.Text);
                Scroll = 0;
            }
        }
    auto FilePointer    = Pointer;
    FilePointer.Enabled = Pointer.Enabled && !CategoryMenu;
    ConstructEntries(Surface, Files, FilePointer);
    const auto Details = Wide ? Spanning(Right - 228, BodyY, 228, std::max(1.0f, BodyBottom - BodyY))
                              : Spanning(Left, BodyBottom + 6, Width, 58);
    ConstructSelection(Surface, Details);
    std::string Status = Directory.QueryDiagnostic();
    if (Status.empty())
        Status = Directory.QueryTruncated() ? "First 8192 directory entries only / refine the folder"
                                            : "Read-only / double-click folders / Enter opens selected folder";
    if (Directory.QueryOmittedLinks())
        Status += " / links omitted";
    const auto Footer = Spanning(Left, Pane.MaximumY - 28, Width, 24);
    Surface.PushClip(Footer);
    ControlKit::TextLeading(Surface, Footer, 0, Palette.TextDim, Status.c_str(), 11);
    Surface.PopClip();
    Surface.PopClip();
    if (!Wide && CategoryMenu)
    {
        const char *Options[7];
        for (size_t Index = 0; Index < CategoryExtents.size(); ++Index)
            Options[Index] = QueryContentCategoryLabel(static_cast<ContentCategory>(Index));
        const float MenuHeight = ControlKit::DropdownMenuExtent(FilterBox, 7).Height();
        const float MinimumY   = HostedPresentation ? Pane.MinimumY + 44 : Origin.Y / Scale + 44;
        const float MaximumY   = std::max(
            MinimumY,
            (HostedPresentation ? Pane.MaximumY : (Origin.Y + ImGui::GetIO().DisplaySize.y) / Scale) - MenuHeight - 8);
        const float MenuY  = std::clamp(FilterBox.MaximumY + 8, MinimumY, MaximumY);
        auto        Anchor = FilterBox;
        Anchor.MaximumY    = MenuY - 8;
        Anchor.MinimumY    = Anchor.MaximumY - FilterBox.Height();
        uint32_t   Chosen  = static_cast<uint32_t>(Category);
        const auto Menu    = ControlKit::DropdownMenuExtent(Anchor, 7);
        for (size_t Index = 0; Index < CategoryExtents.size(); ++Index)
            CategoryExtents[Index] = PhysicalExtent(
                Spanning(Menu.MinimumX + 6, Menu.MinimumY + 6 + static_cast<float>(Index) * 38, Menu.Width() - 12, 36));
        auto MenuPointer    = Pointer;
        MenuPointer.Enabled = Pointer.Enabled && MenuWasOpen;
        if (ControlKit::DropdownMenu(Surface, Anchor, Options, 7, static_cast<uint32_t>(Category), MenuPointer, Chosen)
                .Clicked)
        {
            Category = static_cast<ContentCategory>(Chosen);
            Directory.ConfigureFilter(Category, Search.Text);
            Scroll       = 0;
            CategoryMenu = false;
        }
        else if (Pointer.Pressed && !ControlKit::Over(Menu, Pointer) && !ControlKit::Over(FilterBox, Pointer))
            CategoryMenu = false;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DRAWER ADAPTATION
//------------------------------------------------------------------------------------------------------------------------

void ContentBrowserPanel::ConstructHostedInterface(float InterfaceScale, bool Enabled) noexcept
{
    if (QueryPhase() != ActivationPhase::Active)
        return;
    const auto &IO        = ImGui::GetIO();
    const auto  Position  = ImGui::GetCursorScreenPos();
    const auto  Available = ImGui::GetContentRegionAvail();
    const auto *Viewport  = ImGui::GetWindowViewport();
    Origin                = {Viewport->Pos.x, Viewport->Pos.y};
    Scale                 = std::clamp(InterfaceScale, .75f, 1.5f);
    ControlPointer Pointer;
    Pointer.X        = IO.MousePos.x / Scale;
    Pointer.Y        = IO.MousePos.y / Scale;
    Pointer.Pressed  = ImGui::IsMouseClicked(0);
    Pointer.Released = ImGui::IsMouseReleased(0);
    Pointer.Down     = ImGui::IsMouseDown(0);
    Pointer.Wheel    = IO.MouseWheel;
    Pointer.Enabled =
        Enabled && !IO.AppFocusLost && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    Focused = Enabled && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (!Enabled || IO.AppFocusLost)
    {
        Search.Active = CategoryMenu = false;
        PressedPath.clear();
    }
    HostedPresentation = true;
    PixelSpace Surface;
    if (Surface.Begin(SurfaceLayer::Window, IO.DisplaySize.x, IO.DisplaySize.y, Scale))
        try
        {
            if (Enabled)
                IntegrateKeyboard();
            ConstructLayout(Surface,
                            Spanning(Position.x / Scale, Position.y / Scale, Available.x / Scale, Available.y / Scale),
                            Pointer);
        }
        catch (const std::exception &Failure)
        {
            Surface.Text(Position.x / Scale, Position.y / Scale, ControlKit::Palette().Danger, Failure.what(), 12);
        }
    HostedPresentation = false;
    ImGui::Dummy(Available);
}

void ContentBrowserPanel::ConstructInterface(float InterfaceScale, bool Enabled) noexcept
{
    if (QueryPhase() != ActivationPhase::Active)
        return;
    auto      &IO             = ImGui::GetIO();
    const auto ViewportOrigin = ImGui::GetMainViewport()->Pos;
    Origin                    = {ViewportOrigin.x, ViewportOrigin.y};
    Scale                     = InterfaceScale;
    Expansion.ConfigureExtent(IO.DisplaySize.x, IO.DisplaySize.y, Scale);
    const float MaximumHeight = std::max(36.0f * Scale, std::min(900.0f * Scale, IO.DisplaySize.y - 86 * Scale));
    Expansion.ConfigureTravelLimit(
        Maximized ? MaximumHeight : std::min(MaximumHeight, std::max(IO.DisplaySize.y * 0.60f, 440.0f * Scale)));
    const bool Visible = Expansion.QueryHeight() > 0.5f || Expansion.QueryOpen();
    if (!Enabled || IO.AppFocusLost)
    {
        Search.Active = false;
        CategoryMenu  = false;
        PressedPath.clear();
        Focused = false;
        if (Expansion.QueryDragging())
            Expansion.IntegratePointer(-1e10f, -1e10f, false, false, false, IO.DeltaTime, false);
    }
    const bool Interactive = Enabled && !IO.AppFocusLost;
    ImGui::SetNextWindowPos(ViewportOrigin);
    ImGui::SetNextWindowSize(IO.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Content browser overlay", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs);
    PixelSpace Surface;
    (void)Surface.Begin(SurfaceLayer::Window, IO.DisplaySize.x, IO.DisplaySize.y, Scale);
    const auto     Pane = Spanning(Origin.X / Scale, (Origin.Y + IO.DisplaySize.y - Expansion.QueryHeight()) / Scale,
                                   IO.DisplaySize.x / Scale, Expansion.QueryHeight() / Scale);
    ControlPointer Pointer;
    Pointer.X           = IO.MousePos.x / Scale;
    Pointer.Y           = IO.MousePos.y / Scale;
    Pointer.Pressed     = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    Pointer.Down        = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    Pointer.Released    = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    Pointer.Wheel       = IO.MouseWheel;
    Pointer.Enabled     = Interactive && Expansion.QueryOpen() && !Expansion.QueryDragging();
    const bool OverPane = Pane.Encloses(Pointer.X, Pointer.Y);
    const bool OverTab  = Expansion.QueryTabExtent().Encloses(IO.MousePos.x - Origin.X, IO.MousePos.y - Origin.Y);
    if (Interactive && Pointer.Pressed)
        Focused = OverPane || OverTab;
    HeaderContact = false;
    if (Visible)
    {
        try
        {
            if (Interactive)
                IntegrateKeyboard();
            ConstructLayout(Surface, Pane, Pointer);
        }
        catch (const std::exception &Failure)
        {
            Surface.FillRectangle(Pane, ControlKit::Palette().Field);
            ControlKit::TextLeading(Surface, Pane, 12, ControlKit::Palette().Danger, Failure.what(), 12);
        }
    }
    Expansion.ConstructLayout(Surface, "Content Browser", Origin);
    if (Interactive)
    {
        Expansion.IntegratePointer(IO.MousePos.x - Origin.X, IO.MousePos.y - Origin.Y, Pointer.Pressed && !CategoryMenu,
                                   Pointer.Down, Pointer.Released, IO.DeltaTime, HeaderContact);
        if (OverPane || OverTab || Expansion.QueryDragging())
            ImGui::SetNextFrameWantCaptureMouse(true);
    }
    if (!Expansion.QueryOpen())
    {
        Search.Active = false;
        CategoryMenu  = false;
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
} // namespace Frontier
