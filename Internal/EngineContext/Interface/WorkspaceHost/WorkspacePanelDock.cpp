/*==============================================================================================================================================
                                                            WORKSPACEPANELDOCK.CPP
==============================================================================================================================================*/
// 🧩 Chrome-style workspace dock where EVERY tab is a hand-drawn trapezoid and the docking model is OURS (not ImGui's DockSpace). A recursive
//    partition tree (WorkspaceRegion pool) splits the central area into leaves; each leaf and each floating window carries a trapezoid tab bar
//    drawn on the foreground draw list. Dragging a leaf tab past a small threshold tears it into a floating window; dragging that window over a
//    leaf's left / right / top / bottom edge splits it, over its centre stacks it as tabs; a gutter between split regions is draggable. This is
//    the shared interior dock the WorkspaceDockHost drives — the host owns workspace registration, this owns the freeform docking. Double-click
//    a trapezoid to rename inline; the per-header (+) adds a document. Interface.lib + vendored ImGui only.

#include "WorkspacePanelDock.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      DIAGNOSTIC LOG
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 🐞 Panel-box docking trace. Appends one line per event to WorkspacePanelDock.log beside the running exe (working directory). Opened lazily on
    //    the first line, flushed every write so a crash still leaves the tail on disk. This exists to diagnose the panel horizontal-split bug: each
    //    line records body dimensions, cursor, the resolved dock side, the onto-panel index, whether a dock was detected, the preview (blue overlay)
    //    rect, and how that overlay is split. Prefix every line with PanelLog(...) — remove the calls once the split behaviour is confirmed.
    void PanelLog(const char* Format, ...)
    {
        static std::FILE* File = std::fopen("WorkspacePanelDock.log", "w");
        if (File == nullptr)
            return;
        va_list Args;
        va_start(Args, Format);
        std::vfprintf(File, Format, Args);
        va_end(Args);
        std::fputc('\n', File);
        std::fflush(File);
    }

    // 🐞 Log-only name for a panel dock side.
    const char* PanelSideName(WorkspacePanelDockSide Side)
    {
        switch (Side)
        {
        case WorkspacePanelDockSide::Left:     return "Left(column)";
        case WorkspacePanelDockSide::Right:    return "Right(column)";
        case WorkspacePanelDockSide::Top:      return "Top(row)";
        case WorkspacePanelDockSide::Bottom:   return "Bottom(row)";
        case WorkspacePanelDockSide::Centre:   return "Centre";
        default:                               return "Floating";
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 The smaller trapezoid tab bar carried by a docked leaf and by a floating window.
    constexpr float BarHeight          = 35.0f;    // [px] - tab-bar band height on a leaf / window (lengthened for taller tabs)
    constexpr float BarTabWidth        = 150.0f;   // [px]
    constexpr float BarTabOverlap      = 12.0f;    // [px]
    constexpr float BarTabSlant        = 12.0f;    // [px]
    constexpr float BarTabHeight       = 27.0f;    // [px] - ALL tabs share this height (sits inside the 35px header band)
    constexpr float BarPadLeft         = 10.0f;    // [px]
    constexpr float BarTabStep         = BarTabWidth - BarTabOverlap;   // [px]

    // 📝 Floating-window + dock-tree metrics.
    constexpr float TearWidth   = 380.0f;   // [px] - seed width of a torn-off window
    constexpr float TearHeight  = 280.0f;   // [px] - seed height of a torn-off window
    constexpr float MinWidth    = 220.0f;   // [px] - smallest a window resizes to
    constexpr float MinHeight   = 150.0f;   // [px]
    constexpr float ResizeGrip  = 16.0f;    // [px] - corner grip hit area
    constexpr float WindowRounding = 7.0f;  // [px] - corner radius of a floating window (docked leaves tile edge-to-edge, so they stay square)
    constexpr float GutterWidth = 3.0f;     // [px] - thickness of a split gutter (thin resize handle between docked workspaces)
    constexpr float OuterBand   = 30.0f;    // [px] - thickness of the outer (whole-area) dock band

    // 📝 The (+) dropdown is driven by the app-supplied WorkspacePanelDock.DocumentCatalogue (see ConfigureWorkspaceCatalogue). When an unconfigured
    //    host opens the menu (the WorkspaceDock validation), this single generic type is offered so the (+) still works — a plain "Tab N".
    const WorkspaceDocumentType GenericDocumentType = { "New Tab", "Tab", WorkspaceCategory::Empty };

    // 📝 The active (+) catalogue for one menu paint: the app's list when configured, else the one generic fallback. Returned by pointer + count so
    //    the overlay iterates a uniform surface either way.
    const WorkspaceDocumentType* ActiveCatalogue(const WorkspacePanelDock& State, int& OutCount)
    {
        if (!State.DocumentCatalogue.empty())
        {
            OutCount = (int)State.DocumentCatalogue.size();
            return State.DocumentCatalogue.data();
        }
        OutCount = 1;
        return &GenericDocumentType;
    }

    // 📝 The (V) panel dropdown. Choosing a row DROPS a fresh free-floating "Panel N" box into the active tab's body (a rectangle with a header +
    //    close (x)). One row for now — "New Panel"; the list is here so more spawn presets can be added later without touching the overlay code.
    const char* PanelMenuEntries[] =
    {
        "New Panel",
    };
    constexpr int PanelMenuEntryCount = (int)(sizeof(PanelMenuEntries) / sizeof(PanelMenuEntries[0]));

    const char* CategoryLabel(WorkspaceCategory Category)
    {
        switch (Category)
        {
        case WorkspaceCategory::Viewport:   return "Viewport";
        case WorkspaceCategory::Modeling:   return "Modeling";
        case WorkspaceCategory::Painting:   return "Texture Painting";
        case WorkspaceCategory::UV:         return "UV Editing";
        case WorkspaceCategory::Draughting: return "Draughting";
        case WorkspaceCategory::Terrain:    return "Terrain";
        case WorkspaceCategory::Baking:     return "Texture Baking";
        case WorkspaceCategory::Simulation: return "Simulation";
        default:                            return "Empty";
        }
    }

    // 📝 The chrome is GREY / BLACK / WHITE only — no per-category hue, no accent on a tab. The ACTIVE tab (and a LONE tab that is the only one on
    //    its bar) is BLACK; inactive tabs are a chrome grey that lifts one step on hover. White is reserved for TEXT — the title reads white on a
    //    black tab and black on a grey tab. Category is still tracked (it names the workspace in the placeholder text + (+) menu labels) but never
    //    colours the chrome.
    constexpr ImU32 TabBlackFill    = IM_COL32( 0,  0,  0, 255);    // [-] - active tab / lone tab — pitch black (matches the render body below)
    constexpr ImU32 TabInactiveFill = IM_COL32(96, 99,105, 255);    // [-] - chrome grey — an inactive tab
    constexpr ImU32 TabHoverFill    = IM_COL32(120,124,130, 255);   // [-] - one step lighter — an inactive tab under the cursor

    // The fill for one bar tab: active OR lone → black; hovered inactive → lighter grey; inactive → grey. Greyscale + black only.
    ImU32 TabFill(bool IsActive, bool IsLone, bool Hovered)
    {
        if (IsActive || IsLone)
            return TabBlackFill;
        return Hovered ? TabHoverFill : TabInactiveFill;
    }

    // The title-text colour that reads against a given tab fill: white on the black (active / lone) tab, black on the grey inactive tabs.
    ImU32 TabTextColor(bool IsActive, bool IsLone)
    {
        return (IsActive || IsLone) ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 255);
    }

    // Resolve a document's category through the registry (returns Empty when the id is unknown).
    WorkspaceCategory CategoryOf(const WorkspacePanelDock& State, uint32_t Identifier)
    {
        for (const WorkspaceDocument& Document : State.Documents)
            if (Document.Identifier == Identifier)
                return Document.Category;
        return WorkspaceCategory::Empty;
    }

    // 📝 What a press inside a leaf / window tab bar resolves to. (`Action` avoids the banned `Kind` / `Role`.)
    enum class WorkspaceTabAction { None, Rename, Close, Relocate };
    struct WorkspaceTabHit
    {
        WorkspaceTabAction Action   = WorkspaceTabAction::None;
        uint32_t           Document = 0;
        float              GrabLeft = 0.0f;   // [px] - trapezoid left, so a pulled-out tab keeps its cursor offset
    };
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL — INPUT SNAPSHOT
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    float MouseX()   { return ImGui::GetIO().MousePos.x; }
    float MouseY()   { return ImGui::GetIO().MousePos.y; }
    bool  Down()     { return ImGui::IsMouseDown(ImGuiMouseButton_Left); }
    bool  Clicked()  { return ImGui::IsMouseClicked(ImGuiMouseButton_Left); }
    bool  DblClick() { return ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left); }

    bool Inside(float MinX, float MinY, float MaxX, float MaxY)
    {
        return MouseX() >= MinX && MouseX() <= MaxX && MouseY() >= MinY && MouseY() <= MaxY;
    }
    bool InsideRect(const WorkspaceRect& Rect)
    {
        return Inside(Rect.PositionX, Rect.PositionY, Rect.PositionX + Rect.Width, Rect.PositionY + Rect.Height);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL — REGISTRY + CONTAINERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    WorkspaceDocument* ResolveDocument(WorkspacePanelDock& State, uint32_t Identifier)
    {
        for (WorkspaceDocument& Document : State.Documents)
            if (Document.Identifier == Identifier)
                return &Document;
        return nullptr;
    }

    bool TitleAlreadyPresent(const WorkspacePanelDock& State, const char* Candidate)
    {
        for (const WorkspaceDocument& Document : State.Documents)
            if (std::strcmp(Document.Title, Candidate) == 0)
                return true;
        return false;
    }

    // 📝 Distinct titles WITHOUT trailing digits: on a collision we append a NATO word (Blender-style "cooler than _1"), then pairs.
    const char* const TitleSuffixes[] =
    {
        "Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot", "Golf", "Hotel", "India", "Juliet",
        "Kilo", "Lima", "Mike", "November", "Oscar", "Papa", "Quebec", "Romeo", "Sierra", "Tango",
    };
    constexpr int TitleSuffixCount = (int)(sizeof(TitleSuffixes) / sizeof(TitleSuffixes[0]));

    void ResolveDistinctTitle(const WorkspacePanelDock& State, const char* Base, char* OutTitle, size_t Capacity)
    {
        std::snprintf(OutTitle, Capacity, "%s", Base ? Base : "Workspace");
        if (!TitleAlreadyPresent(State, OutTitle))
            return;
        for (int First = 0; First < TitleSuffixCount; ++First)
        {
            std::snprintf(OutTitle, Capacity, "%s %s", Base, TitleSuffixes[First]);
            if (!TitleAlreadyPresent(State, OutTitle))
                return;
        }
        for (int First = 0; First < TitleSuffixCount; ++First)
            for (int Second = 0; Second < TitleSuffixCount; ++Second)
            {
                std::snprintf(OutTitle, Capacity, "%s %s %s", Base, TitleSuffixes[First], TitleSuffixes[Second]);
                if (!TitleAlreadyPresent(State, OutTitle))
                    return;
            }
    }

    // 📝 Resolve the next free "<Prefix> N" title (Tab 1, Tab 2, … or Panel 1, Panel 2, …). Scans the registry for the lowest N whose
    //    "<Prefix> N" is not already taken, so closing a middle tab and adding again reuses the gap. Used by both the (+) (Tab N) and (V) (Panel N).
    void ResolveNumberedTitle(const WorkspacePanelDock& State, const char* Prefix, char* OutTitle, size_t Capacity)
    {
        for (int Number = 1; ; ++Number)
        {
            std::snprintf(OutTitle, Capacity, "%s %d", Prefix, Number);
            if (!TitleAlreadyPresent(State, OutTitle))
                return;
        }
    }

    // Intern a new document in the registry and return its id (does NOT place it in any container).
    uint32_t ConstructDocument(WorkspacePanelDock& State, const char* Title, WorkspaceCategory Category)
    {
        WorkspaceDocument Document;
        Document.Identifier = State.NextDocument++;
        Document.Category   = Category;
        ResolveDistinctTitle(State, Title, Document.Title, sizeof(Document.Title));
        State.Documents.push_back(Document);
        return Document.Identifier;
    }

    int ConstructLeaf(WorkspacePanelDock& State, const std::vector<uint32_t>& Documents);   // fwd (defined in the dock-tree pool section)
    template <typename Visitor> void ForEachLeaf(WorkspacePanelDock& State, int Index, Visitor&& Visit);   // fwd

    // Intern a document and place it into a target container: an existing leaf region, an existing floating window, or — when neither is given —
    //    the root leaf (constructing it if the desk is empty). Returns the new document id. This is how both boot seeding and the per-header (+)
    //    button add a workspace, so a new document always lands in the header that spawned it (Chrome model).
    uint32_t AttachDocument(WorkspacePanelDock& State, const char* Title, WorkspaceCategory Category, int TargetRegion, uint32_t TargetWindow)
    {
        const uint32_t Identifier = ConstructDocument(State, Title, Category);
        if (TargetWindow != 0)
        {
            for (WorkspaceFloatingWindow& Window : State.Floating)
                if (Window.Identifier == TargetWindow)
                {
                    Window.Documents.push_back(Identifier);
                    Window.ActiveIdentifier = Identifier;
                    return Identifier;
                }
        }
        if (TargetRegion >= 0 && TargetRegion < (int)State.Regions.size()
            && State.Regions[TargetRegion].Occupied && State.Regions[TargetRegion].Leaf)
        {
            State.Regions[TargetRegion].Documents.push_back(Identifier);
            State.Regions[TargetRegion].ActiveIdentifier = Identifier;
            return Identifier;
        }
        // No live target → add to the root leaf, seeding the tree if the desk is empty.
        if (State.RootRegion >= 0 && State.Regions[State.RootRegion].Leaf)
        {
            State.Regions[State.RootRegion].Documents.push_back(Identifier);
            State.Regions[State.RootRegion].ActiveIdentifier = Identifier;
        }
        else if (State.RootRegion < 0)
        {
            State.RootRegion = ConstructLeaf(State, { Identifier });
        }
        else   // root is a partition — drop into its first leaf
        {
            int First = -1;
            ForEachLeaf(State, State.RootRegion, [&](int Index) { if (First < 0) First = Index; });
            if (First >= 0)
            {
                State.Regions[First].Documents.push_back(Identifier);
                State.Regions[First].ActiveIdentifier = Identifier;
            }
        }
        return Identifier;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL — DOCK-TREE POOL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // Reserve a region slot (reusing a freed one when possible) and return its index. Do NOT hold a WorkspaceRegion& across this call — it
    //    may reallocate the pool; always re-index afterwards.
    int AllocateRegion(WorkspacePanelDock& State)
    {
        for (size_t Index = 0; Index < State.Regions.size(); ++Index)
            if (!State.Regions[Index].Occupied)
            {
                State.Regions[Index] = WorkspaceRegion();
                State.Regions[Index].Occupied = true;
                return (int)Index;
            }
        State.Regions.emplace_back();
        State.Regions.back().Occupied = true;
        return (int)State.Regions.size() - 1;
    }

    void FreeRegion(WorkspacePanelDock& State, int Index)
    {
        if (Index < 0 || Index >= (int)State.Regions.size())
            return;
        State.Regions[Index] = WorkspaceRegion();
        State.Regions[Index].Occupied = false;
    }

    int ConstructLeaf(WorkspacePanelDock& State, const std::vector<uint32_t>& Documents)
    {
        const int Index = AllocateRegion(State);
        WorkspaceRegion& Leaf = State.Regions[Index];
        Leaf.Leaf             = true;
        Leaf.Documents        = Documents;
        Leaf.ActiveIdentifier = Documents.empty() ? 0 : Documents.back();
        Leaf.FirstChild       = -1;
        Leaf.SecondChild      = -1;
        return Index;
    }

    // Swap one child index for another wherever it appears (the tree top, or a partition's first / second child). NewIndex is the freshly-made
    //    partition that ALREADY holds OldIndex as one of its own children — it must be skipped, else its own child link would be repointed at
    //    itself (a self-cycle) and the next layout / prune walk would recurse forever. That self-reference was the rare dock crash.
    void ReplaceChild(WorkspacePanelDock& State, int OldIndex, int NewIndex)
    {
        if (State.RootRegion == OldIndex) { State.RootRegion = NewIndex; return; }
        for (int Index = 0; Index < (int)State.Regions.size(); ++Index)
        {
            if (Index == NewIndex)
                continue;   // never rewrite the new partition's own link to OldIndex (would make it point at itself)
            WorkspaceRegion& Region = State.Regions[Index];
            if (!Region.Occupied || Region.Leaf)
                continue;
            if (Region.FirstChild  == OldIndex) Region.FirstChild  = NewIndex;
            if (Region.SecondChild == OldIndex) Region.SecondChild = NewIndex;
        }
    }

    // Depth-first prune: an empty leaf is freed (→ -1); a partition with one surviving child collapses into that child. Returns the new
    //    index for the subtree rooted at Index.
    int PruneSubtree(WorkspacePanelDock& State, int Index)
    {
        if (Index < 0 || Index >= (int)State.Regions.size() || !State.Regions[Index].Occupied)
            return -1;   // out of range / already-freed slot → treat as empty (never dereference a stale index)
        if (State.Regions[Index].Leaf)
        {
            if (State.Regions[Index].Documents.empty()) { FreeRegion(State, Index); return -1; }
            return Index;
        }
        const int First  = PruneSubtree(State, State.Regions[Index].FirstChild);
        const int Second = PruneSubtree(State, State.Regions[Index].SecondChild);
        if (First < 0 && Second < 0) { FreeRegion(State, Index); return -1; }
        if (First  < 0)              { FreeRegion(State, Index); return Second; }
        if (Second < 0)              { FreeRegion(State, Index); return First; }
        State.Regions[Index].FirstChild  = First;
        State.Regions[Index].SecondChild = Second;
        return Index;
    }

    void PruneAll(WorkspacePanelDock& State)
    {
        State.RootRegion = PruneSubtree(State, State.RootRegion);
        State.Floating.erase(std::remove_if(State.Floating.begin(), State.Floating.end(),
            [](const WorkspaceFloatingWindow& Window) { return Window.Documents.empty(); }), State.Floating.end());
    }

    template <typename Visitor>
    void ForEachLeaf(WorkspacePanelDock& State, int Index, Visitor&& Visit)
    {
        if (Index < 0 || Index >= (int)State.Regions.size() || !State.Regions[Index].Occupied)
            return;
        if (State.Regions[Index].Leaf) { Visit(Index); return; }
        ForEachLeaf(State, State.Regions[Index].FirstChild,  Visit);
        ForEachLeaf(State, State.Regions[Index].SecondChild, Visit);
    }

    template <typename Visitor>
    void ForEachPartition(WorkspacePanelDock& State, int Index, Visitor&& Visit)
    {
        if (Index < 0 || Index >= (int)State.Regions.size() || !State.Regions[Index].Occupied || State.Regions[Index].Leaf)
            return;
        Visit(Index);
        ForEachPartition(State, State.Regions[Index].FirstChild,  Visit);
        ForEachPartition(State, State.Regions[Index].SecondChild, Visit);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL — DOCUMENT REMOVAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    void EraseFromStack(std::vector<uint32_t>& Stack, uint32_t Identifier, uint32_t& Active)
    {
        Stack.erase(std::remove(Stack.begin(), Stack.end(), Identifier), Stack.end());
        if (Active == Identifier)
            Active = Stack.empty() ? 0 : Stack.front();
    }

    // Pull a document out of whichever container holds it (a floating window / a docked leaf), fixing that container's active id.
    //    Does NOT delete the registry entry and does NOT prune — the caller decides.
    void DetachDocument(WorkspacePanelDock& State, uint32_t Identifier)
    {
        for (WorkspaceFloatingWindow& Window : State.Floating)
            for (uint32_t WindowId : Window.Documents)
                if (WindowId == Identifier)
                {
                    EraseFromStack(Window.Documents, Identifier, Window.ActiveIdentifier);
                    return;
                }
        for (WorkspaceRegion& Region : State.Regions)
            if (Region.Occupied && Region.Leaf)
                for (uint32_t LeafId : Region.Documents)
                    if (LeafId == Identifier)
                    {
                        EraseFromStack(Region.Documents, Identifier, Region.ActiveIdentifier);
                        return;
                    }
    }

    void CloseDocument(WorkspacePanelDock& State, uint32_t Identifier)
    {
        DetachDocument(State, Identifier);
        State.Documents.erase(std::remove_if(State.Documents.begin(), State.Documents.end(),
            [Identifier](const WorkspaceDocument& Document) { return Document.Identifier == Identifier; }), State.Documents.end());
        if (State.Rename.TargetIdentifier == Identifier)
            State.Rename.TargetIdentifier = 0;
        if (State.PendingTabDocument == Identifier)
            State.PendingTabDocument = 0;
        PruneAll(State);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL — INLINE RENAME
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Begin editing a document title. The live edit lives in State.Rename, the shared InlineTextEditor caret editor keyed by the document id.
    void BeginRename(WorkspacePanelDock& State, uint32_t Identifier)
    {
        WorkspaceDocument* Document = ResolveDocument(State, Identifier);
        if (Document == nullptr)
            return;
        BeginInlineTextEdit(State.Rename, Identifier, Document->Title);
    }

    // 📝 Settle the in-flight title edit through the shared editor: Enter commits into the document's Title (empty → "Untitled"), Escape cancels.
    //    Runs once per frame after the strip is drawn. The document is resolved from the live edit's target id.
    void ResolveRename(WorkspacePanelDock& State)
    {
        if (State.Rename.TargetIdentifier == 0)
            return;
        WorkspaceDocument* Document = ResolveDocument(State, State.Rename.TargetIdentifier);
        if (Document == nullptr) { State.Rename.TargetIdentifier = 0; return; }
        ResolveInlineTextEdit(State.Rename, Document->Title, (int)sizeof(Document->Title), "Untitled");
    }

    // 📝 Commit the in-flight title edit right now (no Enter needed) — used when a click elsewhere pulls focus off the caption, so the name settles
    //    the way a normal text field does on blur.
    void CommitRename(WorkspacePanelDock& State)
    {
        if (State.Rename.TargetIdentifier == 0)
            return;
        WorkspaceDocument* Document = ResolveDocument(State, State.Rename.TargetIdentifier);
        if (Document == nullptr) { State.Rename.TargetIdentifier = 0; return; }
        CommitInlineTextEdit(State.Rename, Document->Title, (int)sizeof(Document->Title), "Untitled");
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL — TRAPEZOID PAINT
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // Paint the (editable) title: static text, or — while this document is under rename — the live buffer plus a blinking caret. Delegates to
    //    the shared InlineTextEditor so every renamable overlay label edits through one definition.
    void PaintTitleText(ImDrawList* DrawList, const WorkspacePanelDock& State, uint32_t Identifier, const char* Title,
                        ImVec2 TextPos, float ClipRight, ImU32 Color)
    {
        PaintInlineTextEdit(DrawList, State.Rename, Identifier, Title, TextPos, ClipRight, Color);
    }

    ImVec2 CloseCenter(float Left, float Top, float Height, float Width, float Slant)
    {
        return ImVec2(Left + Width - Slant - 8.0f, Top + Height * 0.5f);
    }

    // Paint one trapezoid: top edge inset by Slant per side, full width at the bottom (mirrors the prototype clip-path); title; close (x). The
    //    tab itself is a plain (unrounded) trapezoid — rounding lives on floating windows, not tabs. TextColor draws the title + close so a black
    //    tab reads white text and a grey tab reads black text.
    void PaintTrapezoid(ImDrawList* DrawList, const WorkspacePanelDock& State,
                        float Left, float Bottom, float Height, ImU32 Fill, ImU32 TextColor,
                        uint32_t Identifier, const char* Title, float Width, float Slant, bool ShowClose, bool CloseHovered)
    {
        const float Top = Bottom - Height;
        ImVec2 Quad[4] =
        {
            ImVec2(Left + Slant, Top),
            ImVec2(Left + Width - Slant, Top),
            ImVec2(Left + Width, Bottom),
            ImVec2(Left, Bottom),
        };
        DrawList->AddConvexPolyFilled(Quad, 4, Fill);

        const float TextLeft  = Left + Slant + 12.0f;
        const float TextRight = Left + Width - Slant - 22.0f;
        PaintTitleText(DrawList, State, Identifier, Title, ImVec2(TextLeft, Top + (Height - ImGui::GetTextLineHeight()) * 0.5f),
                       TextRight, TextColor);

        if (ShowClose)
        {
            const ImVec2 Center = CloseCenter(Left, Top, Height, Width, Slant);
            const float  Arm    = 3.5f;
            DrawList->AddLine(ImVec2(Center.x - Arm, Center.y - Arm), ImVec2(Center.x + Arm, Center.y + Arm), TextColor, 1.4f);
            DrawList->AddLine(ImVec2(Center.x - Arm, Center.y + Arm), ImVec2(Center.x + Arm, Center.y - Arm), TextColor, 1.4f);
        }
    }

    void DrawChevronDown(ImDrawList* DrawList, ImVec2 Center, float Size, ImU32 Color)
    {
        DrawList->AddLine(ImVec2(Center.x - Size * 0.42f, Center.y - Size * 0.18f),
                          ImVec2(Center.x,                Center.y + Size * 0.22f), Color, 1.6f);
        DrawList->AddLine(ImVec2(Center.x,                Center.y + Size * 0.22f),
                          ImVec2(Center.x + Size * 0.42f, Center.y - Size * 0.18f), Color, 1.6f);
    }

    void DrawPlus(ImDrawList* DrawList, ImVec2 Center, float Size, ImU32 Color)
    {
        DrawList->AddLine(ImVec2(Center.x - Size * 0.5f, Center.y), ImVec2(Center.x + Size * 0.5f, Center.y), Color, 1.7f);
        DrawList->AddLine(ImVec2(Center.x, Center.y - Size * 0.5f), ImVec2(Center.x, Center.y + Size * 0.5f), Color, 1.7f);
    }

    // 📝 The per-header (+) add button carried by every leaf and floating window (Chrome model — each header owns its own). It sits after the
    //    tab trapezoids, offset from the header's right edge. HeaderRight is the right x of the header band; the returned centre is on its midline.
    constexpr float HeaderAddRadius = 12.0f;   // [px] - per-header (+) hit + hover-circle radius (hit area unchanged)
    constexpr float HeaderAddGlyph  = 10.4f;   // [px] - drawn (+) glyph span — 80% of the former 13px so the icon reads smaller
    ImVec2 HeaderAddCenter(float HeaderRight, float HeaderTop)
    {
        return ImVec2(HeaderRight - HeaderAddRadius - 8.0f, HeaderTop + BarHeight * 0.5f);
    }

    // The x the tab strip must stop before, so tabs never paint over the (+). It is the (+) button's left edge, less a small gap.
    float HeaderTabClipRight(float HeaderRight, float HeaderTop)
    {
        return HeaderAddCenter(HeaderRight, HeaderTop).x - HeaderAddRadius - 4.0f;
    }

    bool HeaderAddHovered(float HeaderRight, float HeaderTop)
    {
        const ImVec2 Center = HeaderAddCenter(HeaderRight, HeaderTop);
        const float  Dx = MouseX() - Center.x, Dy = MouseY() - Center.y;
        return Dx * Dx + Dy * Dy <= HeaderAddRadius * HeaderAddRadius;
    }

    // 📝 The per-header (V) panel button at the FAR LEFT of every leaf / window header (the mirror of the (+) on the right). Clicking it opens
    //    the panel dropdown; choosing a panel retypes the current tab. HeaderLeft is the header band's left x; the returned centre sits on its midline.
    constexpr float HeaderPanelRadius = 12.0f;   // [px] - (V) hit + hover-circle radius
    constexpr float HeaderPanelGlyph  = 11.0f;   // [px] - drawn chevron span
    constexpr float HeaderPanelInset  = 8.0f;    // [px] - gap from the header's left edge to the (V) centre band
    ImVec2 HeaderPanelCenter(float HeaderLeft, float HeaderTop)
    {
        return ImVec2(HeaderLeft + HeaderPanelInset + HeaderPanelRadius, HeaderTop + BarHeight * 0.5f);
    }

    // The x the tab strip must START at, so the first trapezoid never paints over the (V). It is the (V) button's right edge, plus a small gap.
    float HeaderTabClipLeft(float HeaderLeft, float HeaderTop)
    {
        return HeaderPanelCenter(HeaderLeft, HeaderTop).x + HeaderPanelRadius + 4.0f;
    }

    bool HeaderPanelHovered(float HeaderLeft, float HeaderTop)
    {
        const ImVec2 Center = HeaderPanelCenter(HeaderLeft, HeaderTop);
        const float  Dx = MouseX() - Center.x, Dy = MouseY() - Center.y;
        return Dx * Dx + Dy * Dy <= HeaderPanelRadius * HeaderPanelRadius;
    }

    // Draw the per-header (V). Returns the button centre so the caller can anchor the panel dropdown just below it.
    ImVec2 PaintHeaderPanel(ImDrawList* DrawList, const ThemeConfiguration& Theme, float HeaderLeft, float HeaderTop)
    {
        const ImVec2 Center  = HeaderPanelCenter(HeaderLeft, HeaderTop);
        const bool   Hovered = HeaderPanelHovered(HeaderLeft, HeaderTop);
        if (Hovered)
            DrawList->AddCircleFilled(Center, HeaderPanelRadius, Theme.Palette.ControlHovered, 20);
        DrawChevronDown(DrawList, Center, HeaderPanelGlyph, Hovered ? Theme.Palette.TextPrimary : Theme.Palette.TextMuted);
        return Center;
    }

    // Draw the per-header (+). Returns the button centre so the caller can anchor the add-menu just below it.
    ImVec2 PaintHeaderAdd(ImDrawList* DrawList, const ThemeConfiguration& Theme, float HeaderRight, float HeaderTop)
    {
        const ImVec2 Center = HeaderAddCenter(HeaderRight, HeaderTop);
        const bool   Hovered = HeaderAddHovered(HeaderRight, HeaderTop);
        if (Hovered)
            DrawList->AddCircleFilled(Center, HeaderAddRadius, Theme.Palette.ControlHovered, 20);
        DrawPlus(DrawList, Center, HeaderAddGlyph, Hovered ? Theme.Palette.TextPrimary : Theme.Palette.TextMuted);
        return Center;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL — LEAF / WINDOW TAB BAR
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // Paint the bar-sized trapezoid tabs for a document stack (inactive first, the active one last over the body fill). ClipRight is the x the
    //    tabs must NOT draw past — the left edge of the header's (+) button — so an overflowing stack is clipped instead of painting over the (+).
    void PaintTabBar(ImDrawList* DrawList, const ThemeConfiguration& Theme, const WorkspacePanelDock& State,
                     float OriginX, float OriginY, const std::vector<uint32_t>& Documents, uint32_t Active, ImU32 BodyFill, float ClipRight)
    {
        const float Bottom = OriginY + BarHeight;
        DrawList->PushClipRect(ImVec2(OriginX, OriginY), ImVec2(ClipRight, Bottom + 1.0f), true);
        // ALL tabs share ONE height (BarTabHeight) and ONE index-based x — switching the active tab therefore never changes any tab's size or
        //    position; only the FILL differs (active / lone = black, inactive = grey). Draw inactive first so the black active trapezoid overlaps
        //    its neighbours on top, but its rect is identical to theirs.
        const bool IsLone = Documents.size() == 1;   // a bar with a single tab paints that tab black regardless of active state
        // Tabs start AFTER the far-left (V) panel button (Chrome model — every header carries its own (V) before the tabs).
        const float TabStripLeft = HeaderTabClipLeft(OriginX, OriginY);
        auto PaintOne = [&](size_t Index, bool ActivePass)
        {
            const uint32_t Identifier = Documents[Index];
            const bool     IsActive   = Identifier == Active;
            if (IsActive != ActivePass)
                return;
            const float Height = BarTabHeight;
            const float Left   = TabStripLeft + (float)Index * BarTabStep;
            const float Top    = Bottom - Height;
            const bool  Hovered = Inside(Left, Top, Left + BarTabWidth, Bottom);
            const ImVec2 Center = CloseCenter(Left, Top, Height, BarTabWidth, BarTabSlant);
            const bool  CloseHovered = Hovered && Inside(Center.x - 9.0f, Center.y - 9.0f, Center.x + 9.0f, Center.y + 9.0f);
            const WorkspaceDocument* Document = nullptr;
            for (const WorkspaceDocument& Candidate : State.Documents)
                if (Candidate.Identifier == Identifier) { Document = &Candidate; break; }
            // Active / lone tab is black (white text); inactive is grey (black text, hover lifts it one step). No hue on the chrome.
            const ImU32 Fill      = TabFill(IsActive, IsLone, Hovered);
            const ImU32 TextColor = TabTextColor(IsActive, IsLone);
            (void)BodyFill;
            (void)Theme;
            PaintTrapezoid(DrawList, State, Left, Bottom, Height, Fill, TextColor, Identifier,
                           Document ? Document->Title : "", BarTabWidth, BarTabSlant, IsActive || Hovered, CloseHovered);
        };
        for (size_t Index = 0; Index < Documents.size(); ++Index) PaintOne(Index, false);
        for (size_t Index = 0; Index < Documents.size(); ++Index) PaintOne(Index, true);
        DrawList->PopClipRect();
    }

    // Resolve a press inside a bar-sized tab bar → the action for the topmost trapezoid the cursor is over (or None).
    WorkspaceTabHit HitTabBar(float OriginX, float OriginY, const std::vector<uint32_t>& Documents, uint32_t Active)
    {
        const float Bottom = OriginY + BarHeight;
        (void)Active;   // all tabs share one height now — the active id no longer changes any tab's hit rect
        const float TabStripLeft = HeaderTabClipLeft(OriginX, OriginY);   // tabs start after the far-left (V) panel button
        for (size_t Index = 0; Index < Documents.size(); ++Index)
        {
            const uint32_t Identifier = Documents[Index];
            const float    Height = BarTabHeight;
            const float    Left   = TabStripLeft + (float)Index * BarTabStep;
            const float    Top    = Bottom - Height;
            if (!Inside(Left, Top, Left + BarTabWidth, Bottom))
                continue;
            const ImVec2 Center = CloseCenter(Left, Top, Height, BarTabWidth, BarTabSlant);
            const bool   CloseHovered = Inside(Center.x - 9.0f, Center.y - 9.0f, Center.x + 9.0f, Center.y + 9.0f);
            if (DblClick() && !CloseHovered) return { WorkspaceTabAction::Rename,   Identifier, Left };
            if (CloseHovered)                return { WorkspaceTabAction::Close,    Identifier, Left };
            return                                  { WorkspaceTabAction::Relocate, Identifier, Left };
        }
        return {};
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL — FLOAT A DOCUMENT
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // Pull one document out of wherever it lives into a fresh floating window under the cursor, and begin moving that window.
    void FloatDocument(WorkspacePanelDock& State, uint32_t Identifier, float GrabX, float GrabY)
    {
        DetachDocument(State, Identifier);
        PruneAll(State);

        WorkspaceFloatingWindow Window;
        Window.Identifier       = State.NextWindow++;
        Window.Documents        = { Identifier };
        Window.ActiveIdentifier = Identifier;
        Window.PositionX        = MouseX() - GrabX;
        Window.PositionY        = MouseY() - GrabY;
        Window.Width            = TearWidth;
        Window.Height           = TearHeight;
        State.Floating.push_back(Window);

        State.DragMode   = WorkspaceDragMode::Window;
        State.DragOrigin = WorkspaceDragOrigin::Tab;   // torn from a tab strip → float-by-default, edge / corner docking only
        State.DragWindow = Window.Identifier;
        State.DragGrabX  = GrabX;
        State.DragGrabY  = GrabY;
    }

    WorkspaceFloatingWindow* ResolveWindow(WorkspacePanelDock& State, uint32_t Identifier)
    {
        for (WorkspaceFloatingWindow& Window : State.Floating)
            if (Window.Identifier == Identifier)
                return &Window;
        return nullptr;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL — LAYOUT
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    WorkspaceRect CentralRect()
    {
        // No global top strip (Chrome model): the dock tree fills the ENTIRE viewport work area. Each leaf carries its own header instead.
        const ImGuiViewport* Viewport = ImGui::GetMainViewport();
        return { Viewport->WorkPos.x, Viewport->WorkPos.y, Viewport->WorkSize.x, Viewport->WorkSize.y };
    }

    void LayoutRegion(WorkspacePanelDock& State, int Index, WorkspaceRect Area)
    {
        if (Index < 0 || Index >= (int)State.Regions.size() || !State.Regions[Index].Occupied)
            return;
        {
            WorkspaceRegion& Region = State.Regions[Index];
            Region.Rect      = Area;
            Region.RectValid = true;
            if (Region.Leaf)
                return;
        }
        // Re-read fields by index after each recursion (the pool never reallocates during layout, but stay index-safe).
        const WorkspacePartitionAxis Axis  = State.Regions[Index].Axis;
        const float                  Ratio = State.Regions[Index].Ratio;
        const int                    First = State.Regions[Index].FirstChild;
        const int                    Second = State.Regions[Index].SecondChild;

        if (Axis == WorkspacePartitionAxis::Row)
        {
            const float WidthA = std::round((Area.Width - GutterWidth) * Ratio);
            LayoutRegion(State, First,  { Area.PositionX,                       Area.PositionY, WidthA,                            Area.Height });
            LayoutRegion(State, Second, { Area.PositionX + WidthA + GutterWidth, Area.PositionY, Area.Width - WidthA - GutterWidth, Area.Height });
            State.Regions[Index].Gutter = { Area.PositionX + WidthA, Area.PositionY, GutterWidth, Area.Height };
        }
        else
        {
            const float HeightA = std::round((Area.Height - GutterWidth) * Ratio);
            LayoutRegion(State, First,  { Area.PositionX, Area.PositionY,                        Area.Width, HeightA });
            LayoutRegion(State, Second, { Area.PositionX, Area.PositionY + HeightA + GutterWidth, Area.Width, Area.Height - HeightA - GutterWidth });
            State.Regions[Index].Gutter = { Area.PositionX, Area.PositionY + HeightA, Area.Width, GutterWidth };
        }
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL — DOCK RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // Resolve where the currently-dragged floating window would land, into State.Preview* (the translucent overlay + the release target).
    void ResolveDock(WorkspacePanelDock& State)
    {
        State.PreviewZone   = WorkspaceDockZone::None;
        State.PreviewRegion = -1;
        State.PreviewWindow = 0;

        const WorkspaceRect Central = CentralRect();

        // Over ANOTHER floating window's tab bar → stack the dragged docs onto it (dock inside its tabs). Topmost first; skip the held one.
        for (int Index = (int)State.Floating.size() - 1; Index >= 0; --Index)
        {
            const WorkspaceFloatingWindow& Probe = State.Floating[Index];
            if (Probe.Identifier == State.DragWindow)
                continue;
            const WorkspaceRect Bar = { Probe.PositionX, Probe.PositionY, Probe.Width, BarHeight };
            if (InsideRect(Bar))
            {
                State.PreviewZone   = WorkspaceDockZone::Center;
                State.PreviewRegion = -1;
                State.PreviewWindow = Probe.Identifier;
                State.PreviewRect   = { Probe.PositionX, Probe.PositionY, Probe.Width, Probe.Height };
                return;
            }
        }

        if (!InsideRect(Central))
            return;

        // Deepest leaf under the cursor (leaves never overlap, so the last hit is the one).
        int LeafHit = -1;
        ForEachLeaf(State, State.RootRegion, [&](int Index) { if (InsideRect(State.Regions[Index].Rect)) LeafHit = Index; });

        const bool TabDrag = State.DragOrigin == WorkspaceDragOrigin::Tab;

        // The hovered leaf's TAB BAR stacks as tabs (dropping into the strip = "put this doc among these tabs"), across the WHOLE strip width. This is
        //    a TAB-only shortcut: for a tab drag the strip is the ONLY per-leaf dock target (the body interior floats), so it returns Center here. A
        //    PANEL drag deliberately SKIPS this — its five-zone cross below owns the whole leaf including the strip (the cross's centre square covers
        //    the strip), so leaving this block to panels would let the 35px strip return Center and shadow a Left / Right aim near the leaf top (the
        //    reported "panels only dock vertically"). The panel cross therefore resolves the strip region itself as part of the cross.
        if (TabDrag && LeafHit >= 0)
        {
            const WorkspaceRect& HitRect = State.Regions[LeafHit].Rect;
            const WorkspaceRect  HitBar  = { HitRect.PositionX, HitRect.PositionY, HitRect.Width, BarHeight };
            if (InsideRect(HitBar))
            {
                State.PreviewZone   = WorkspaceDockZone::Center;
                State.PreviewRegion = LeafHit;
                State.PreviewRect   = HitRect;
                return;
            }
        }

        // Outer whole-area band — a whole-central-area split (30% band down one side of the ENTIRE dock), distinct from a per-leaf split. For a TAB
        //    drag this (plus the strip stack above) is the dock model: dock only at a distinct dock edge / corner, else float. For a PANEL drag it is
        //    reserved for when the cursor is NOT over any leaf (the surrounding margin / gutters), so the per-leaf 50/50 cross is never stolen — a
        //    panel drop over a leaf is always resolved by that leaf's own cross below. (Without the LeafHit<0 guard for panels, dragging to a small
        //    leaf's edge — also within OuterBand of the central border — produced a 30/70 whole-area split instead of the leaf's 50/50 split.)
        const bool NearLeft   = MouseX() - Central.PositionX < OuterBand;
        const bool NearRight  = Central.PositionX + Central.Width - MouseX() < OuterBand;
        const bool NearTop    = MouseY() - Central.PositionY < OuterBand;
        const bool NearBottom = Central.PositionY + Central.Height - MouseY() < OuterBand;
        // A tab drag may edge-dock over a leaf (the outer band is its whole-dock edge target), so it drops the LeafHit<0 guard a panel keeps.
        const bool OuterBandOpen = TabDrag ? (State.RootRegion >= 0) : (State.RootRegion >= 0 && LeafHit < 0);
        if (OuterBandOpen && (NearLeft || NearRight || NearTop || NearBottom))
        {
            WorkspaceDockZone Side = NearLeft ? WorkspaceDockZone::Left : NearRight ? WorkspaceDockZone::Right
                                   : NearTop  ? WorkspaceDockZone::Top  : WorkspaceDockZone::Bottom;
            WorkspaceRect Preview;
            if (Side == WorkspaceDockZone::Left)        Preview = { Central.PositionX, Central.PositionY, Central.Width * 0.3f, Central.Height };
            else if (Side == WorkspaceDockZone::Right)  Preview = { Central.PositionX + Central.Width * 0.7f, Central.PositionY, Central.Width * 0.3f, Central.Height };
            else if (Side == WorkspaceDockZone::Top)    Preview = { Central.PositionX, Central.PositionY, Central.Width, Central.Height * 0.3f };
            else                                        Preview = { Central.PositionX, Central.PositionY + Central.Height * 0.7f, Central.Width, Central.Height * 0.3f };
            State.PreviewZone   = Side;
            State.PreviewRegion = -1;
            State.PreviewRect   = Preview;
            return;
        }

        // Empty central area → the drop becomes the root leaf.
        if (State.RootRegion < 0)
        {
            State.PreviewZone   = WorkspaceDockZone::Center;
            State.PreviewRegion = -1;
            State.PreviewRect   = Central;
            return;
        }

        // A TAB drag stops here: it docks only via the strip stack or the outer edge / corner band above; over a leaf BODY it floats (PreviewZone
        //    stays None), so a casual drop leaves the torn tab as a floating window exactly where released — the original tab behaviour. The five-zone
        //    body cross below is a PANEL-only gesture (moving a panel window bodily always docks it over a leaf).
        if (TabDrag)
            return;

        // Inner leaf zones (PANEL drag only) — a THREE-BY-THREE GRID over the leaf, rewritten from the former cross because its corner tie-break kept
        //    resolving to a vertical split. The grid removes the tie-break entirely: each cell is a plain rectangle, so which split you get is decided
        //    only by which column / row the cursor sits in — never by a pixel-depth race between two axes. The leaf is cut into thirds on each axis:
        //
        //        +------+------+------+     Left column  (any row)   -> horizontal split, new panel takes the LEFT half   (side by side)
        //        | L    | Top  | R    |     Right column (any row)   -> horizontal split, new panel takes the RIGHT half  (side by side)
        //        +------+------+------+     Top-middle cell          -> vertical   split, new panel takes the TOP half    (stacked)
        //        | L    |center| R    |     Bottom-middle cell       -> vertical   split, new panel takes the BOTTOM half (stacked)
        //        +------+------+------+     Centre cell               -> stack as tabs (also covers the tab strip)
        //        | L    | Bot  | R    |
        //        +------+------+------+     The whole left / right third (corners included) is horizontal, so a drop anywhere down a side — or into a
        //                                   top / bottom corner — splits side by side, exactly the requested behaviour. Only the narrow middle column's
        //                                   top / bottom thirds stack vertically.
        if (LeafHit >= 0)
        {
            const WorkspaceRect Rect = State.Regions[LeafHit].Rect;

            const float ColumnThird = Rect.Width  / 3.0f;   // [px] - a column of the 3x3 grid
            const float RowThird    = Rect.Height / 3.0f;    // [px] - a row of the 3x3 grid
            const float LocalX      = MouseX() - Rect.PositionX;   // cursor within the leaf
            const float LocalY      = MouseY() - Rect.PositionY;

            const bool LeftColumn   = LocalX < ColumnThird;
            const bool RightColumn  = LocalX > Rect.Width - ColumnThird;
            const bool TopRow       = LocalY < RowThird;
            const bool BottomRow    = LocalY > Rect.Height - RowThird;

            WorkspaceDockZone Side; WorkspaceRect Preview;
            if (LeftColumn)         { Side = WorkspaceDockZone::Left;   Preview = { Rect.PositionX, Rect.PositionY, Rect.Width * 0.5f, Rect.Height }; }
            else if (RightColumn)   { Side = WorkspaceDockZone::Right;  Preview = { Rect.PositionX + Rect.Width * 0.5f, Rect.PositionY, Rect.Width * 0.5f, Rect.Height }; }
            else if (TopRow)        { Side = WorkspaceDockZone::Top;    Preview = { Rect.PositionX, Rect.PositionY, Rect.Width, Rect.Height * 0.5f }; }
            else if (BottomRow)     { Side = WorkspaceDockZone::Bottom; Preview = { Rect.PositionX, Rect.PositionY + Rect.Height * 0.5f, Rect.Width, Rect.Height * 0.5f }; }
            else                    { Side = WorkspaceDockZone::Center; Preview = Rect; }   // centre cell → stack as tabs

            State.PreviewZone   = Side;
            State.PreviewRegion = LeafHit;
            State.PreviewRect   = Preview;
        }
    }

    void ApplyDock(WorkspacePanelDock& State, const std::vector<uint32_t>& Documents, WorkspaceDockZone Zone, int TargetRegion)
    {
        if (Documents.empty() || Zone == WorkspaceDockZone::None)
            return;

        const bool SideFirst = Zone == WorkspaceDockZone::Left || Zone == WorkspaceDockZone::Top;
        const WorkspacePartitionAxis Axis = (Zone == WorkspaceDockZone::Left || Zone == WorkspaceDockZone::Right)
                                          ? WorkspacePartitionAxis::Row : WorkspacePartitionAxis::Column;

        if (TargetRegion < 0)   // outer / whole central area
        {
            if (State.RootRegion < 0)   // empty central area → the drop simply becomes the root leaf
            {
                State.RootRegion = ConstructLeaf(State, Documents);
                return;
            }
            if (Zone == WorkspaceDockZone::Center)   // a centre landing with no target leaf on a populated tree stacks onto the root leaf if it
            {                                        //    is a leaf, else falls through to an outer split (never overwrites the root subtree)
                if (State.Regions[State.RootRegion].Leaf)
                {
                    WorkspaceRegion& RootLeaf = State.Regions[State.RootRegion];
                    for (uint32_t Identifier : Documents)
                        RootLeaf.Documents.push_back(Identifier);
                    RootLeaf.ActiveIdentifier = Documents.back();
                    return;
                }
            }
            const int PreviousRoot = State.RootRegion;
            const int Leaf      = ConstructLeaf(State, Documents);
            const int Partition = AllocateRegion(State);
            WorkspaceRegion& Node = State.Regions[Partition];
            Node.Leaf        = false;
            Node.Axis        = Axis;
            Node.Ratio       = SideFirst ? 0.3f : 0.7f;
            Node.FirstChild  = SideFirst ? Leaf : PreviousRoot;
            Node.SecondChild = SideFirst ? PreviousRoot : Leaf;
            State.RootRegion = Partition;
            return;
        }

        if (Zone == WorkspaceDockZone::Center)   // stack onto the target leaf
        {
            WorkspaceRegion& Target = State.Regions[TargetRegion];
            for (uint32_t Identifier : Documents)
                Target.Documents.push_back(Identifier);
            Target.ActiveIdentifier = Documents.back();
            return;
        }

        // Split the target leaf: wrap it and the new leaf in a fresh partition where the leaf used to hang.
        const int Leaf      = ConstructLeaf(State, Documents);
        const int Partition = AllocateRegion(State);
        {
            WorkspaceRegion& Node = State.Regions[Partition];
            Node.Leaf        = false;
            Node.Axis        = Axis;
            Node.Ratio       = SideFirst ? 0.4f : 0.6f;
            Node.FirstChild  = SideFirst ? Leaf : TargetRegion;
            Node.SecondChild = SideFirst ? TargetRegion : Leaf;
        }
        ReplaceChild(State, TargetRegion, Partition);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL — PAINT DOCK TREE
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    void PaintLeaf(ImDrawList* DrawList, const ThemeConfiguration& Theme, const WorkspacePanelDock& State, const WorkspaceRegion& Leaf)
    {
        const WorkspaceRect& Rect = Leaf.Rect;
        // The render region (below the tab bar) is FULL BLACK — that is the "viewport" the user wants pitch black; only the tab-bar chrome
        //    above it stays grey. No divider line between the header and the body (the black tab bottom already meets the black body).
        const ImU32 RenderBlack = IM_COL32(0, 0, 0, 255);
        DrawList->AddRectFilled(ImVec2(Rect.PositionX, Rect.PositionY), ImVec2(Rect.PositionX + Rect.Width, Rect.PositionY + Rect.Height),
                                RenderBlack);
        DrawList->AddRect(ImVec2(Rect.PositionX + 0.5f, Rect.PositionY + 0.5f),
                          ImVec2(Rect.PositionX + Rect.Width - 0.5f, Rect.PositionY + Rect.Height - 0.5f), Theme.Palette.PanelBorder);
        DrawList->AddRectFilled(ImVec2(Rect.PositionX, Rect.PositionY), ImVec2(Rect.PositionX + Rect.Width, Rect.PositionY + BarHeight),
                                Theme.Palette.PanelHeader);

        DrawList->PushClipRect(ImVec2(Rect.PositionX, Rect.PositionY), ImVec2(Rect.PositionX + Rect.Width, Rect.PositionY + BarHeight), true);
        PaintTabBar(DrawList, Theme, State, Rect.PositionX, Rect.PositionY, Leaf.Documents, Leaf.ActiveIdentifier, RenderBlack,
                    HeaderTabClipRight(Rect.PositionX + Rect.Width, Rect.PositionY));
        DrawList->PopClipRect();
        PaintHeaderPanel(DrawList, Theme, Rect.PositionX, Rect.PositionY);              // this leaf's own (V) panel dropdown (far left)
        PaintHeaderAdd(DrawList, Theme, Rect.PositionX + Rect.Width, Rect.PositionY);   // this leaf's own (+)

        // Panel boxes the active tab hosts are painted in a later pass (ConstructPanelBoxesPass) so their drag / close input resolves against a
        //    mutable State — here we only draw the tab chrome + black body.
    }

    void PaintDockTree(ImDrawList* DrawList, const ThemeConfiguration& Theme, WorkspacePanelDock& State)
    {
        ForEachLeaf(State, State.RootRegion, [&](int Index) { PaintLeaf(DrawList, Theme, State, State.Regions[Index]); });
        ForEachPartition(State, State.RootRegion, [&](int Index)
        {
            const WorkspaceRect& Gutter = State.Regions[Index].Gutter;
            const bool Hovered = InsideRect(Gutter) ||
                                 (State.DragMode == WorkspaceDragMode::Partition && State.DragRegion == Index);
            DrawList->AddRectFilled(ImVec2(Gutter.PositionX, Gutter.PositionY),
                                    ImVec2(Gutter.PositionX + Gutter.Width, Gutter.PositionY + Gutter.Height),
                                    Hovered ? Theme.Palette.ControlActive : Theme.Palette.ControlBackground);
        });
    }

    void PaintPanelBoxesForBody(ImDrawList*, const ThemeConfiguration&, WorkspacePanelDock&, uint32_t, const WorkspaceRect&, bool);   // fwd

    void PaintFloatingWindow(ImDrawList* DrawList, const ThemeConfiguration& Theme, WorkspacePanelDock& State,
                             const WorkspaceFloatingWindow& Window, bool Focused, bool BlockInput)
    {
        const float Left = Window.PositionX, Top = Window.PositionY, Right = Left + Window.Width, Bottom = Top + Window.Height;
        const ImU32 RenderBlack = IM_COL32(0, 0, 0, 255);
        // Floating windows carry rounded corners (docked leaves do not — they tile edge-to-edge). The black body + outline round on all four
        //    corners; the grey header band rounds only its TOP two so it seats flush against the flat accent line beneath it.
        DrawList->AddRectFilled(ImVec2(Left, Top), ImVec2(Right, Bottom), RenderBlack, WindowRounding);   // floating render body is black too
        DrawList->AddRect(ImVec2(Left + 0.5f, Top + 0.5f), ImVec2(Right - 0.5f, Bottom - 0.5f),
                          Focused ? Theme.Palette.ControlActive : Theme.Palette.PanelBorder, WindowRounding);
        DrawList->AddRectFilled(ImVec2(Left, Top), ImVec2(Right, Top + BarHeight), Theme.Palette.PanelHeader,
                                WindowRounding, ImDrawFlags_RoundCornersTop);

        DrawList->PushClipRect(ImVec2(Left, Top), ImVec2(Right, Top + BarHeight), true);
        PaintTabBar(DrawList, Theme, State, Left, Top, Window.Documents, Window.ActiveIdentifier, RenderBlack,
                    HeaderTabClipRight(Right, Top));
        DrawList->PopClipRect();
        PaintHeaderPanel(DrawList, Theme, Left, Top);   // this window's own (V) panel dropdown (far left)
        PaintHeaderAdd(DrawList, Theme, Right, Top);    // this window's own (+)

        // Panel boxes the active tab hosts paint HERE — right after this window's chrome — so a window drawn LATER (higher in the stack) covers
        //    them and the tab z-order hides them. Input for these boxes is resolved separately in ConstructPanelBoxesPass. The body is the area
        //    below this window's BarHeight header (matches BodyRectOf).
        WorkspaceRect WindowBody;
        WindowBody.PositionX = Left; WindowBody.PositionY = Top + BarHeight;
        WindowBody.Width = Window.Width; WindowBody.Height = std::max(0.0f, Window.Height - BarHeight);
        PaintPanelBoxesForBody(DrawList, Theme, State, Window.ActiveIdentifier, WindowBody, BlockInput);

        const ImU32 GripColor = Focused ? Theme.Palette.TextMuted : Theme.Palette.PanelBorder;
        DrawList->AddLine(ImVec2(Right - 4.0f, Bottom - 12.0f), ImVec2(Right - 12.0f, Bottom - 4.0f), GripColor, 1.4f);
        DrawList->AddLine(ImVec2(Right - 4.0f, Bottom - 7.0f),  ImVec2(Right - 7.0f,  Bottom - 4.0f), GripColor, 1.4f);
    }

    void PaintDockPreview(ImDrawList* DrawList, const ThemeConfiguration& Theme, const WorkspacePanelDock& State)
    {
        if (State.PreviewZone == WorkspaceDockZone::None)
            return;
        const WorkspaceRect& Rect = State.PreviewRect;
        const ImU32 Accent = Theme.Palette.SelectionMarker;
        const ImU32 Wash   = (Accent & 0x00FFFFFF) | ((ImU32)56 << 24);
        DrawList->AddRectFilled(ImVec2(Rect.PositionX, Rect.PositionY), ImVec2(Rect.PositionX + Rect.Width, Rect.PositionY + Rect.Height), Wash);
        DrawList->AddRect(ImVec2(Rect.PositionX + 1.0f, Rect.PositionY + 1.0f),
                          ImVec2(Rect.PositionX + Rect.Width - 1.0f, Rect.PositionY + Rect.Height - 1.0f), Accent, 0.0f, 0, 2.0f);

        // A stack-as-tabs landing (onto a leaf or another window) also underlines the tab bar so the "dock inside tabs" intent is obvious.
        const bool StacksAsTabs = State.PreviewZone == WorkspaceDockZone::Center && (State.PreviewRegion >= 0 || State.PreviewWindow != 0);
        if (StacksAsTabs)
        {
            const ImU32 BarWash = (Accent & 0x00FFFFFF) | ((ImU32)120 << 24);
            DrawList->AddRectFilled(ImVec2(Rect.PositionX, Rect.PositionY), ImVec2(Rect.PositionX + Rect.Width, Rect.PositionY + BarHeight), BarWash);
        }
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL — PANEL BOXES (per-tab body)
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr float PanelBoxHeader = 22.0f;   // [px] - a panel box's header-strip height (holds the "Panel N" label + close (x))
    constexpr float PanelBoxFooter = 16.0f;   // [px] - a panel box's footer-strip height (a thin status band beneath the body)
    constexpr float PanelBoxClose  = 16.0f;   // [px] - the close (x) hit square at the header's right end
    constexpr float PanelBoxMinW   = 120.0f;  // [px] - keep a floating box from clipping its own header text / handles
    constexpr float PanelBoxMinH   = 80.0f;   // [px]
    constexpr float PanelResizeBand = 6.0f;   // [px] - the grab thickness of a floating box's edge / corner resize handles
    constexpr float PanelDockBand   = 44.0f;  // [px] - how deep the edge / bottom drop zones reach in from the body rim
    constexpr float PanelDockMin    = 0.12f;  // [0-1]- min / max docked band fraction of the body span
    constexpr float PanelDockMax    = 0.6f;   // [0-1]

    // Resize-edge bitmask (State.PanelResizeEdge): corners combine two bits.
    enum { EdgeLeft = 1, EdgeRight = 2, EdgeTop = 4, EdgeBottom = 8 };

    //--------------------------------------------------------------------------------------------------------------------
    //  PANEL SPLIT-TREE POOL — the panel-side twin of the window dock-tree pool (AllocateRegion / ConstructLeaf / …). Each
    //  WorkspaceDocument owns its own PanelRegions pool + PanelRoot, so every tab body carries an independent split tree.
    //  Same index-pool discipline: NEVER hold a WorkspacePanelRegion& across an AllocatePanelRegion — re-index afterwards.
    //--------------------------------------------------------------------------------------------------------------------

    int AllocatePanelRegion(WorkspaceDocument& Document)
    {
        for (size_t Index = 0; Index < Document.PanelRegions.size(); ++Index)
            if (!Document.PanelRegions[Index].Occupied)
            {
                Document.PanelRegions[Index] = WorkspacePanelRegion();
                Document.PanelRegions[Index].Occupied = true;
                return (int)Index;
            }
        Document.PanelRegions.emplace_back();
        Document.PanelRegions.back().Occupied = true;
        return (int)Document.PanelRegions.size() - 1;
    }

    void FreePanelRegion(WorkspaceDocument& Document, int Index)
    {
        if (Index < 0 || Index >= (int)Document.PanelRegions.size())
            return;
        Document.PanelRegions[Index] = WorkspacePanelRegion();
        Document.PanelRegions[Index].Occupied = false;
    }

    int ConstructPanelLeaf(WorkspaceDocument& Document, const std::vector<uint32_t>& Boxes)
    {
        const int Index = AllocatePanelRegion(Document);
        WorkspacePanelRegion& Leaf = Document.PanelRegions[Index];
        Leaf.Leaf       = true;
        Leaf.Boxes      = Boxes;
        Leaf.ActiveBox  = Boxes.empty() ? 0 : Boxes.back();
        Leaf.FirstLink  = -1;
        Leaf.SecondLink = -1;
        return Index;
    }

    // Swap one link index for another wherever it appears (the tree top, or a partition's first / second link). NewIndex is the freshly-made
    //    partition that ALREADY holds OldIndex as one of its own links — it must be skipped, else its own link would repoint at itself (a
    //    self-cycle) and the next layout / prune walk would recurse forever (the window tree's ReplaceChild carries the identical guard).
    void ReplacePanelLink(WorkspaceDocument& Document, int OldIndex, int NewIndex)
    {
        if (Document.PanelRoot == OldIndex) { Document.PanelRoot = NewIndex; return; }
        for (int Index = 0; Index < (int)Document.PanelRegions.size(); ++Index)
        {
            if (Index == NewIndex)
                continue;
            WorkspacePanelRegion& Region = Document.PanelRegions[Index];
            if (!Region.Occupied || Region.Leaf)
                continue;
            if (Region.FirstLink  == OldIndex) Region.FirstLink  = NewIndex;
            if (Region.SecondLink == OldIndex) Region.SecondLink = NewIndex;
        }
    }

    // Depth-first prune: an empty leaf is freed (→ -1); a partition with one surviving link collapses into it. Returns the subtree's new index.
    int PrunePanelSubtree(WorkspaceDocument& Document, int Index)
    {
        if (Index < 0 || Index >= (int)Document.PanelRegions.size() || !Document.PanelRegions[Index].Occupied)
            return -1;
        if (Document.PanelRegions[Index].Leaf)
        {
            if (Document.PanelRegions[Index].Boxes.empty()) { FreePanelRegion(Document, Index); return -1; }
            return Index;
        }
        const int First  = PrunePanelSubtree(Document, Document.PanelRegions[Index].FirstLink);
        const int Second = PrunePanelSubtree(Document, Document.PanelRegions[Index].SecondLink);
        if (First < 0 && Second < 0) { FreePanelRegion(Document, Index); return -1; }
        if (First  < 0)              { FreePanelRegion(Document, Index); return Second; }
        if (Second < 0)              { FreePanelRegion(Document, Index); return First; }
        Document.PanelRegions[Index].FirstLink  = First;
        Document.PanelRegions[Index].SecondLink = Second;
        return Index;
    }

    template <typename Visitor>
    void ForEachPanelLeaf(WorkspaceDocument& Document, int Index, Visitor&& Visit)
    {
        if (Index < 0 || Index >= (int)Document.PanelRegions.size() || !Document.PanelRegions[Index].Occupied)
            return;
        if (Document.PanelRegions[Index].Leaf) { Visit(Index); return; }
        ForEachPanelLeaf(Document, Document.PanelRegions[Index].FirstLink,  Visit);
        ForEachPanelLeaf(Document, Document.PanelRegions[Index].SecondLink, Visit);
    }

    template <typename Visitor>
    void ForEachPanelPartition(WorkspaceDocument& Document, int Index, Visitor&& Visit)
    {
        if (Index < 0 || Index >= (int)Document.PanelRegions.size() || !Document.PanelRegions[Index].Occupied ||
            Document.PanelRegions[Index].Leaf)
            return;
        Visit(Index);
        ForEachPanelPartition(Document, Document.PanelRegions[Index].FirstLink,  Visit);
        ForEachPanelPartition(Document, Document.PanelRegions[Index].SecondLink, Visit);
    }

    // Lay the panel tree rooted at Index into Area (absolute body coords), filling each region's cached Rect / Gutter for this frame's paint + input.
    void LayoutPanelRegion(WorkspaceDocument& Document, int Index, WorkspaceRect Area)
    {
        if (Index < 0 || Index >= (int)Document.PanelRegions.size() || !Document.PanelRegions[Index].Occupied)
            return;
        {
            WorkspacePanelRegion& Region = Document.PanelRegions[Index];
            Region.Rect      = Area;
            Region.RectValid = true;
            if (Region.Leaf)
                return;
        }
        const WorkspacePartitionAxis Axis   = Document.PanelRegions[Index].Axis;
        const float                  Ratio  = Document.PanelRegions[Index].Ratio;
        const int                    First  = Document.PanelRegions[Index].FirstLink;
        const int                    Second = Document.PanelRegions[Index].SecondLink;

        if (Axis == WorkspacePartitionAxis::Row)
        {
            const float WidthA = std::round((Area.Width - GutterWidth) * Ratio);
            LayoutPanelRegion(Document, First,  { Area.PositionX,                        Area.PositionY, WidthA,                            Area.Height });
            LayoutPanelRegion(Document, Second, { Area.PositionX + WidthA + GutterWidth, Area.PositionY, Area.Width - WidthA - GutterWidth, Area.Height });
            Document.PanelRegions[Index].Gutter = { Area.PositionX + WidthA, Area.PositionY, GutterWidth, Area.Height };
        }
        else
        {
            const float HeightA = std::round((Area.Height - GutterWidth) * Ratio);
            LayoutPanelRegion(Document, First,  { Area.PositionX, Area.PositionY,                         Area.Width, HeightA });
            LayoutPanelRegion(Document, Second, { Area.PositionX, Area.PositionY + HeightA + GutterWidth, Area.Width, Area.Height - HeightA - GutterWidth });
            Document.PanelRegions[Index].Gutter = { Area.PositionX, Area.PositionY + HeightA, Area.Width, GutterWidth };
        }
    }

    // Find the deepest panel-tree leaf whose laid-out Rect contains the cursor (call after LayoutPanelRegion filled the rects). -1 = none / no tree.
    int PanelLeafAtCursor(WorkspaceDocument& Document)
    {
        int Hit = -1;
        ForEachPanelLeaf(Document, Document.PanelRoot, [&](int LeafIndex)
        {
            if (InsideRect(Document.PanelRegions[LeafIndex].Rect))
                Hit = LeafIndex;   // leaves are disjoint, so at most one contains the cursor
        });
        return Hit;
    }

    // 📝 Move a panel box INTO the tree at a five-zone landing. Center stacks the box id onto TargetLeaf's stack; Left / Right / Top / Bottom wrap
    //    TargetLeaf in a fresh partition with the newcomer's leaf on the correct side. TargetLeaf < 0 seeds the empty tree with a lone leaf. The box
    //    id must already be lifted out of any prior leaf / the floating overlay by the caller (DetachPanelBox), so this only inserts.
    void ApplyPanelDock(WorkspaceDocument& Document, uint32_t BoxIdentifier, WorkspaceDockZone Zone, int TargetLeaf)
    {
        if (BoxIdentifier == 0 || Zone == WorkspaceDockZone::None)
            return;

        if (Document.PanelRoot < 0 || TargetLeaf < 0)   // empty tree → the box becomes the lone root leaf
        {
            Document.PanelRoot = ConstructPanelLeaf(Document, { BoxIdentifier });
            return;
        }

        if (Zone == WorkspaceDockZone::Center)   // stack onto the target leaf (tab it in)
        {
            WorkspacePanelRegion& Target = Document.PanelRegions[TargetLeaf];
            Target.Boxes.push_back(BoxIdentifier);
            Target.ActiveBox = BoxIdentifier;
            return;
        }

        const bool SideFirst = Zone == WorkspaceDockZone::Left || Zone == WorkspaceDockZone::Top;
        const WorkspacePartitionAxis Axis = (Zone == WorkspaceDockZone::Left || Zone == WorkspaceDockZone::Right)
                                          ? WorkspacePartitionAxis::Row : WorkspacePartitionAxis::Column;

        const int Leaf      = ConstructPanelLeaf(Document, { BoxIdentifier });
        const int Partition = AllocatePanelRegion(Document);
        {
            WorkspacePanelRegion& Node = Document.PanelRegions[Partition];
            Node.Leaf       = false;
            Node.Axis       = Axis;
            Node.Ratio      = SideFirst ? 0.5f : 0.5f;   // a fresh split shares evenly; the gutter can reflow it later
            Node.FirstLink  = SideFirst ? Leaf : TargetLeaf;
            Node.SecondLink = SideFirst ? TargetLeaf : Leaf;
        }
        ReplacePanelLink(Document, TargetLeaf, Partition);
    }

    // True if a panel box id currently lives in the split tree (any leaf holds it). A tree-owned box paints from its leaf rect, so the floating
    //    overlay pass must skip it even though its registry entry still reads Dock == Floating (the Dock flag is meaningful only while truly floating).
    bool BoxInTree(WorkspaceDocument& Document, uint32_t BoxIdentifier)
    {
        bool Found = false;
        ForEachPanelLeaf(Document, Document.PanelRoot, [&](int LeafIndex)
        {
            for (uint32_t Id : Document.PanelRegions[LeafIndex].Boxes)
                if (Id == BoxIdentifier) Found = true;
        });
        return Found;
    }

    // Lift a panel box id out of wherever it currently lives — its tree leaf (pruning the tree afterwards) AND the floating overlay flag. After this
    //    the box is owned by nobody in the layout; the caller re-homes it (ApplyPanelDock, or leaving it Floating with a fresh Offset).
    void DetachPanelBox(WorkspaceDocument& Document, uint32_t BoxIdentifier)
    {
        bool Removed = false;
        ForEachPanelLeaf(Document, Document.PanelRoot, [&](int LeafIndex)
        {
            std::vector<uint32_t>& Stack = Document.PanelRegions[LeafIndex].Boxes;
            const size_t Before = Stack.size();
            Stack.erase(std::remove(Stack.begin(), Stack.end(), BoxIdentifier), Stack.end());
            if (Stack.size() != Before)
            {
                Removed = true;
                Document.PanelRegions[LeafIndex].ActiveBox = Stack.empty() ? 0 : Stack.back();
            }
        });
        if (Removed)
            Document.PanelRoot = PrunePanelSubtree(Document, Document.PanelRoot);
    }

    // The band depth (a fraction of the body span) for one docked side: the depth is taken from the FIRST panel list-docked to that side. 0 = the
    //    side is empty (reserves nothing).
    float BandDepthFraction(const std::vector<WorkspacePanelBox>& Boxes, WorkspacePanelDockSide Side)
    {
        for (const WorkspacePanelBox& Box : Boxes)
            if (Box.Dock == Side)
                return std::min(PanelDockMax, std::max(PanelDockMin, Box.DockExtent));
        return 0.0f;
    }

    // The centre region a body leaves after the four side bands reserve their depths (Left / Right full-height columns, Top / Bottom rows between
    //    the columns). Every docked rect is derived from this same frame.
    WorkspaceRect CentreRegionOf(const WorkspaceRect& Body, const std::vector<WorkspacePanelBox>& Boxes)
    {
        const float LeftW   = Body.Width  * BandDepthFraction(Boxes, WorkspacePanelDockSide::Left);
        const float RightW  = Body.Width  * BandDepthFraction(Boxes, WorkspacePanelDockSide::Right);
        const float TopH    = Body.Height * BandDepthFraction(Boxes, WorkspacePanelDockSide::Top);
        const float BottomH = Body.Height * BandDepthFraction(Boxes, WorkspacePanelDockSide::Bottom);
        WorkspaceRect Centre;
        Centre.PositionX = Body.PositionX + LeftW;
        Centre.PositionY = Body.PositionY + TopH;
        Centre.Width     = std::max(0.0f, Body.Width  - LeftW - RightW);
        Centre.Height    = std::max(0.0f, Body.Height - TopH  - BottomH);
        return Centre;
    }

    // 📝 The absolute rect one panel box occupies inside a body. Left / Right are full-height columns; Top / Bottom are rows spanning the width
    //    BETWEEN the columns; Centre fills the leftover. Several panels on one side SPLIT its band along the long axis (vertical for Left / Right,
    //    horizontal for Top / Bottom) in list order, each taking a normalised share of SlotFraction. Floating boxes use their stored Offset* / size.
    WorkspaceRect ResolveBoxRect(const WorkspacePanelBox& Box, const WorkspaceRect& Body, const std::vector<WorkspacePanelBox>& Boxes)
    {
        if (Box.Dock == WorkspacePanelDockSide::Floating)
        {
            const float W = std::max(PanelBoxMinW, Box.Width), H = std::max(PanelBoxMinH, Box.Height);
            const float X = std::min(std::max(0.0f, Box.OffsetX), std::max(0.0f, Body.Width  - W));
            const float Y = std::min(std::max(0.0f, Box.OffsetY), std::max(0.0f, Body.Height - H));
            WorkspaceRect Rect; Rect.PositionX = Body.PositionX + X; Rect.PositionY = Body.PositionY + Y; Rect.Width = W; Rect.Height = H;
            return Rect;
        }

        const WorkspaceRect Centre = CentreRegionOf(Body, Boxes);
        if (Box.Dock == WorkspacePanelDockSide::Centre)
            return Centre;

        // The full band rect for this side (before the intra-side split).
        WorkspaceRect Band;
        const float LeftW   = Body.Width  * BandDepthFraction(Boxes, WorkspacePanelDockSide::Left);
        const float RightW  = Body.Width  * BandDepthFraction(Boxes, WorkspacePanelDockSide::Right);
        const float TopH    = Body.Height * BandDepthFraction(Boxes, WorkspacePanelDockSide::Top);
        const float BottomH = Body.Height * BandDepthFraction(Boxes, WorkspacePanelDockSide::Bottom);
        bool Vertical = false;   // panels stack top→bottom (Left / Right) vs left→right (Top / Bottom)
        switch (Box.Dock)
        {
        case WorkspacePanelDockSide::Left:
            Band.PositionX = Body.PositionX; Band.PositionY = Body.PositionY; Band.Width = LeftW; Band.Height = Body.Height; Vertical = true; break;
        case WorkspacePanelDockSide::Right:
            Band.PositionX = Body.PositionX + Body.Width - RightW; Band.PositionY = Body.PositionY; Band.Width = RightW; Band.Height = Body.Height; Vertical = true; break;
        case WorkspacePanelDockSide::Top:
            Band.PositionX = Centre.PositionX; Band.PositionY = Body.PositionY; Band.Width = Centre.Width; Band.Height = TopH; break;
        case WorkspacePanelDockSide::Bottom:
            Band.PositionX = Centre.PositionX; Band.PositionY = Body.PositionY + Body.Height - BottomH; Band.Width = Centre.Width; Band.Height = BottomH; break;
        default: break;
        }

        // Split the band across its long axis by the side members' SlotFraction (normalised). This box gets the slot at its list position.
        float Total = 0.0f;
        int   Slots = 0, MyOrder = 0;
        for (const WorkspacePanelBox& Other : Boxes)
        {
            if (Other.Dock != Box.Dock)
                continue;
            if (Other.Identifier == Box.Identifier)
                MyOrder = Slots;
            Total += std::max(0.05f, Other.SlotFraction);
            ++Slots;
        }
        if (Slots <= 1 || Total <= 0.0f)
            return Band;

        float Before = 0.0f, Order = 0;
        for (const WorkspacePanelBox& Other : Boxes)
        {
            if (Other.Dock != Box.Dock)
                continue;
            if ((int)Order == MyOrder)
                break;
            Before += std::max(0.05f, Other.SlotFraction) / Total;
            ++Order;
        }
        const float Share = std::max(0.05f, Box.SlotFraction) / Total;
        WorkspaceRect Rect = Band;
        if (Vertical)
        {
            Rect.PositionY = Band.PositionY + Band.Height * Before;
            Rect.Height    = Band.Height * Share;
        }
        else
        {
            Rect.PositionX = Band.PositionX + Band.Width * Before;
            Rect.Width     = Band.Width * Share;
        }
        return Rect;
    }

    // Paint one panel box: a BLACK header strip ("Panel N" + close (x)), a DARK-GREY body, and a footer strip. `Rect` is the box's absolute area.
    void PaintPanelBox(ImDrawList*               DrawList,
                       const ThemeConfiguration& Theme,
                       const WorkspacePanelBox&  Box,
                       const WorkspaceRect&      Rect,
                       bool                      CloseHovered)
    {
        const float Left = Rect.PositionX, Top = Rect.PositionY, Right = Left + Rect.Width, Bottom = Top + Rect.Height;
        const bool  Floating = Box.Dock == WorkspacePanelDockSide::Floating;
        const float Round = Floating ? 5.0f : 0.0f;
        const ImU32 HeaderBlack = IM_COL32(0, 0, 0, 255);          // header is black
        const ImU32 BodyGrey    = IM_COL32(44, 46, 51, 255);       // body is dark grey
        const ImU32 FooterGrey  = IM_COL32(30, 31, 35, 255);       // footer a touch darker than the body

        DrawList->AddRectFilled(ImVec2(Left, Top), ImVec2(Right, Bottom), BodyGrey, Round);
        DrawList->AddRectFilled(ImVec2(Left, Top), ImVec2(Right, std::min(Bottom, Top + PanelBoxHeader)), HeaderBlack,
                                Round, ImDrawFlags_RoundCornersTop);
        if (Bottom - PanelBoxFooter > Top + PanelBoxHeader)
            DrawList->AddRectFilled(ImVec2(Left, Bottom - PanelBoxFooter), ImVec2(Right, Bottom), FooterGrey,
                                    Round, ImDrawFlags_RoundCornersBottom);
        DrawList->AddRect(ImVec2(Left + 0.5f, Top + 0.5f), ImVec2(Right - 0.5f, Bottom - 0.5f), Theme.Palette.PanelBorder, Round);

        DrawList->AddText(ImVec2(Left + 8.0f, Top + (PanelBoxHeader - ImGui::GetTextLineHeight()) * 0.5f), IM_COL32(255, 255, 255, 255), Box.Title);

        // Close (x) at the header's right end.
        const float CloseLeft = Right - PanelBoxClose - 3.0f;
        const float CloseTop  = Top + (PanelBoxHeader - PanelBoxClose) * 0.5f;
        if (CloseHovered)
            DrawList->AddRectFilled(ImVec2(CloseLeft, CloseTop), ImVec2(CloseLeft + PanelBoxClose, CloseTop + PanelBoxClose),
                                    Theme.Palette.ControlHovered, 3.0f);
        const ImU32 Glyph = CloseHovered ? IM_COL32(255, 255, 255, 255) : Theme.Palette.TextMuted;
        const float Pad = 4.0f;
        DrawList->AddLine(ImVec2(CloseLeft + Pad, CloseTop + Pad),
                          ImVec2(CloseLeft + PanelBoxClose - Pad, CloseTop + PanelBoxClose - Pad), Glyph, 1.6f);
        DrawList->AddLine(ImVec2(CloseLeft + PanelBoxClose - Pad, CloseTop + Pad),
                          ImVec2(CloseLeft + Pad, CloseTop + PanelBoxClose - Pad), Glyph, 1.6f);
    }

    // Which resize edges (bitmask) the cursor is over on a FLOATING box's rim, or 0 if none. Corners return two bits.
    int PanelResizeEdgeAt(const WorkspaceRect& Rect)
    {
        const float L = Rect.PositionX, T = Rect.PositionY, R = L + Rect.Width, B = T + Rect.Height;
        if (!Inside(L - PanelResizeBand, T - PanelResizeBand, R + PanelResizeBand, B + PanelResizeBand))
            return 0;
        int Edge = 0;
        if (std::abs(MouseX() - L) <= PanelResizeBand) Edge |= EdgeLeft;
        if (std::abs(MouseX() - R) <= PanelResizeBand) Edge |= EdgeRight;
        if (std::abs(MouseY() - T) <= PanelResizeBand) Edge |= EdgeTop;
        if (std::abs(MouseY() - B) <= PanelResizeBand) Edge |= EdgeBottom;
        return Edge;
    }

    // The nearest edge (as a dock side) a cursor sits by within an arbitrary rect, or Floating if it is in the rect's open middle. Corners resolve
    //    to the closer rim; a generous centre square (when AllowCentre) resolves to Centre. Used both for the whole body (edge docking) and for one
    //    docked panel's own rect (panel-on-panel docking — dropping onto a panel's L/R/T/B rim inserts beside it on that side).
    WorkspacePanelDockSide EdgeSideWithin(const WorkspaceRect& Rect, float Band, bool AllowCentre)
    {
        if (!InsideRect(Rect))
            return WorkspacePanelDockSide::Floating;
        const float FromLeft   = MouseX() - Rect.PositionX;
        const float FromRight  = Rect.PositionX + Rect.Width  - MouseX();
        const float FromTop    = MouseY() - Rect.PositionY;
        const float FromBottom = Rect.PositionY + Rect.Height - MouseY();
        const float Nearest = std::min(std::min(FromLeft, FromRight), std::min(FromTop, FromBottom));
        if (Nearest <= Band)
        {
            if (Nearest == FromLeft)   return WorkspacePanelDockSide::Left;
            if (Nearest == FromRight)  return WorkspacePanelDockSide::Right;
            if (Nearest == FromTop)    return WorkspacePanelDockSide::Top;
            return WorkspacePanelDockSide::Bottom;
        }
        return AllowCentre ? WorkspacePanelDockSide::Centre : WorkspacePanelDockSide::Floating;
    }

    // The dock side a cursor selects over the WHOLE BODY, resolved on a THREE-BY-THREE GRID rather than the nearest single edge. A body is usually
    //    far wider than tall, so a nearest-edge test (min of the four rim distances) makes FromTop / FromBottom win almost everywhere and the LEFT /
    //    RIGHT sides become unreachable — the reported "panels only ever dock vertically". The grid fixes that: the leftmost / rightmost third of the
    //    body (full height, any row) selects Left / Right, so a drop anywhere down a side — corners included — docks HORIZONTALLY; only the middle
    //    column's top / bottom third selects Top / Bottom. Floating = the dead-centre cell (the small CentreTargetRect handles the Centre dock).
    WorkspacePanelDockSide BodyGridSide(const WorkspaceRect& Body)
    {
        if (!InsideRect(Body))
            return WorkspacePanelDockSide::Floating;
        const float LocalX = MouseX() - Body.PositionX;
        const float LocalY = MouseY() - Body.PositionY;
        const float ColumnThird = Body.Width  / 3.0f;
        const float RowThird    = Body.Height / 3.0f;
        if (LocalX < ColumnThird)                    return WorkspacePanelDockSide::Left;
        if (LocalX > Body.Width - ColumnThird)       return WorkspacePanelDockSide::Right;
        if (LocalY < RowThird)                       return WorkspacePanelDockSide::Top;
        if (LocalY > Body.Height - RowThird)         return WorkspacePanelDockSide::Bottom;
        return WorkspacePanelDockSide::Floating;   // dead centre → no rim dock (the small centre target still triggers Centre)
    }

    // The small centre drop target (a box in the middle of the centre region) — dropping inside it docks to Centre (fills the remaining centre).
    WorkspaceRect CentreTargetRect(const WorkspaceRect& Centre)
    {
        const float Side = 34.0f;
        WorkspaceRect Rect;
        Rect.PositionX = Centre.PositionX + (Centre.Width  - Side) * 0.5f;
        Rect.PositionY = Centre.PositionY + (Centre.Height - Side) * 0.5f;
        Rect.Width = Side; Rect.Height = Side;
        return Rect;
    }

    // The absolute rect a dock band highlights (the area a release would give the box), for the live preview. Depth mirrors a fresh dock (0.28) so
    //    the preview matches what lands. Left / Right are full height; Top / Bottom span the current centre width; Centre = the leftover centre.
    WorkspaceRect PanelDockPreviewRect(WorkspacePanelDockSide Side, const WorkspaceRect& Body, const std::vector<WorkspacePanelBox>& Boxes)
    {
        const WorkspaceRect Centre = CentreRegionOf(Body, Boxes);
        const float ColW = Body.Width  * 0.28f;
        const float RowH = Body.Height * 0.28f;
        WorkspaceRect Rect;
        switch (Side)
        {
        case WorkspacePanelDockSide::Left:   Rect.PositionX = Body.PositionX; Rect.PositionY = Body.PositionY; Rect.Width = ColW; Rect.Height = Body.Height; break;
        case WorkspacePanelDockSide::Right:  Rect.PositionX = Body.PositionX + Body.Width - ColW; Rect.PositionY = Body.PositionY; Rect.Width = ColW; Rect.Height = Body.Height; break;
        case WorkspacePanelDockSide::Top:    Rect.PositionX = Centre.PositionX; Rect.PositionY = Body.PositionY; Rect.Width = Centre.Width; Rect.Height = RowH; break;
        case WorkspacePanelDockSide::Bottom: Rect.PositionX = Centre.PositionX; Rect.PositionY = Body.PositionY + Body.Height - RowH; Rect.Width = Centre.Width; Rect.Height = RowH; break;
        default:                             Rect = Centre; break;   // Centre → the leftover centre region
        }
        return Rect;
    }

    // Evict any box already docked to Centre back to Floating (Centre holds one panel). The evicted box parks near the body's top-left.
    void EvictCentre(WorkspaceDocument& Document, uint32_t KeepIdentifier)
    {
        for (WorkspacePanelBox& Box : Document.PanelBoxes)
        {
            if (Box.Identifier == KeepIdentifier || Box.Dock != WorkspacePanelDockSide::Centre)
                continue;
            Box.Dock = WorkspacePanelDockSide::Floating;
            Box.OffsetX = 32.0f; Box.OffsetY = 32.0f;
        }
    }

    // How many panels list-dock to one side (its band's members). A side with 2+ members has inner gutters between successive members.
    int SideMemberTotal(const std::vector<WorkspacePanelBox>& Boxes, WorkspacePanelDockSide Side)
    {
        int Total = 0;
        for (const WorkspacePanelBox& Box : Boxes)
            if (Box.Dock == Side)
                ++Total;
        return Total;
    }

    // 📝 Detect a docked-band gutter under the cursor. Two gutter families: the OUTER gutter along a band's inner rim (the seam against the centre —
    //    drag it to grow / shrink that band's DockExtent), and the INNER gutter between two successive panels sharing a side (drag it to reflow their
    //    SlotFraction). Fills Side (the band) and Slot (-1 = outer gutter; >= 0 = the inner gutter after the member at that list slot) and returns
    //    true on a hit. Left / Right bands run vertically (outer gutter is a vertical seam, inner gutters are horizontal); Top / Bottom run the other way.
    bool BandGutterAt(const WorkspaceRect&                    Body,
                      const std::vector<WorkspacePanelBox>&   Boxes,
                      WorkspacePanelDockSide&                 Side,
                      int&                                    Slot)
    {
        const WorkspacePanelDockSide Sides[4] =
        {
            WorkspacePanelDockSide::Left, WorkspacePanelDockSide::Right,
            WorkspacePanelDockSide::Top,  WorkspacePanelDockSide::Bottom,
        };
        const WorkspaceRect Centre = CentreRegionOf(Body, Boxes);
        for (WorkspacePanelDockSide Candidate : Sides)
        {
            if (SideMemberTotal(Boxes, Candidate) == 0)
                continue;
            const bool Vertical = Candidate == WorkspacePanelDockSide::Left || Candidate == WorkspacePanelDockSide::Right;

            // Outer gutter: the band's seam against the centre region.
            float Seam = 0.0f;
            switch (Candidate)
            {
            case WorkspacePanelDockSide::Left:   Seam = Centre.PositionX; break;
            case WorkspacePanelDockSide::Right:  Seam = Centre.PositionX + Centre.Width; break;
            case WorkspacePanelDockSide::Top:    Seam = Centre.PositionY; break;
            default:                             Seam = Centre.PositionY + Centre.Height; break;   // Bottom
            }
            if (Vertical)
            {
                if (std::abs(MouseX() - Seam) <= PanelResizeBand && MouseY() >= Body.PositionY && MouseY() <= Body.PositionY + Body.Height)
                {
                    Side = Candidate; Slot = -1; return true;
                }
            }
            else if (std::abs(MouseY() - Seam) <= PanelResizeBand && MouseX() >= Centre.PositionX && MouseX() <= Centre.PositionX + Centre.Width)
            {
                Side = Candidate; Slot = -1; return true;
            }

            // Inner gutters: the seams between successive same-side members (walk the split the way ResolveBoxRect lays it out).
            int Order = 0;
            for (const WorkspacePanelBox& Box : Boxes)
            {
                if (Box.Dock != Candidate)
                    continue;
                const WorkspaceRect Rect = ResolveBoxRect(Box, Body, Boxes);
                if (Order + 1 < SideMemberTotal(Boxes, Candidate))   // a trailing seam exists after every member but the last
                {
                    if (Vertical)
                    {
                        const float Edge = Rect.PositionY + Rect.Height;
                        if (std::abs(MouseY() - Edge) <= PanelResizeBand && MouseX() >= Rect.PositionX && MouseX() <= Rect.PositionX + Rect.Width)
                        {
                            Side = Candidate; Slot = Order; return true;
                        }
                    }
                    else
                    {
                        const float Edge = Rect.PositionX + Rect.Width;
                        if (std::abs(MouseX() - Edge) <= PanelResizeBand && MouseY() >= Rect.PositionY && MouseY() <= Rect.PositionY + Rect.Height)
                        {
                            Side = Candidate; Slot = Order; return true;
                        }
                    }
                }
                ++Order;
            }
        }
        return false;
    }

    // Insert a freshly-docked box into a side's member order and normalise the side's SlotFraction. If OntoIndex >= 0 (panel-on-panel), the box lands
    //    just after that panel in the list so it splits beside it; otherwise it appends. Every same-side member is reset to an equal share so a new
    //    arrival divides the band evenly. The band's DockExtent is carried by the first member, so a fresh side seeds it to a sensible default.
    //    When dropping ONTO a panel (OntoIndex >= 0), the box joins THAT panel's band (the onto-panel's own Dock side) — NOT the rim direction it was
    //    dropped against. The rim direction (RimSide) only decides ORDER relative to the onto-panel: a Left / Top rim inserts the box BEFORE it, a
    //    Right / Bottom rim inserts AFTER. This is what makes [A] + drop-on-A-right become [A | B] within one band, never a separate body column.
    void DockBoxToSide(WorkspaceDocument&     Document,
                       uint32_t               BoxIdentifier,
                       WorkspacePanelDockSide RimSide,
                       int                    OntoIndex)
    {
        // Lift the box out of the list first.
        WorkspacePanelBox Held;
        int From = -1;
        for (int Index = 0; Index < (int)Document.PanelBoxes.size(); ++Index)
            if (Document.PanelBoxes[Index].Identifier == BoxIdentifier) { Held = Document.PanelBoxes[Index]; From = Index; break; }
        if (From < 0)
            return;

        // The band the box actually joins: the onto-panel's own side for panel-on-panel, else the rim side directly (body-edge drop).
        WorkspacePanelDockSide Side = RimSide;
        uint32_t OntoIdentifier = 0;
        if (OntoIndex >= 0 && OntoIndex < (int)Document.PanelBoxes.size())
        {
            Side = Document.PanelBoxes[OntoIndex].Dock;
            OntoIdentifier = Document.PanelBoxes[OntoIndex].Identifier;
        }

        const bool FirstOnSide = SideMemberTotal(Document.PanelBoxes, Side) == (Held.Dock == Side ? 1 : 0);
        Held.Dock = Side;
        Held.SlotFraction = 1.0f;
        if (FirstOnSide || Held.DockExtent <= 0.0f)
            Held.DockExtent = 0.28f;

        Document.PanelBoxes.erase(Document.PanelBoxes.begin() + From);

        // Re-find the onto-panel (the erase may have shifted it) and insert before / after it per the rim direction. A Left / Top rim lands the box
        //    ahead of the onto-panel in list order (so it sits above / left of it); a Right / Bottom rim lands it after.
        int InsertAt = (int)Document.PanelBoxes.size();
        if (OntoIdentifier != 0)
        {
            for (int Index = 0; Index < (int)Document.PanelBoxes.size(); ++Index)
                if (Document.PanelBoxes[Index].Identifier == OntoIdentifier) { InsertAt = Index; break; }
            const bool After = RimSide == WorkspacePanelDockSide::Right || RimSide == WorkspacePanelDockSide::Bottom;
            if (After)
                ++InsertAt;
        }
        InsertAt = std::min((int)Document.PanelBoxes.size(), std::max(0, InsertAt));
        Document.PanelBoxes.insert(Document.PanelBoxes.begin() + InsertAt, Held);

        // Equalise the side's shares so the newcomer divides the band evenly.
        const int Members = SideMemberTotal(Document.PanelBoxes, Side);
        if (Members > 0)
            for (WorkspacePanelBox& Box : Document.PanelBoxes)
                if (Box.Dock == Side)
                    Box.SlotFraction = 1.0f / (float)Members;
    }

    // 📝 Resolve where a floating box being dragged would land this frame. First test each DOCKED panel's own rim (panel-on-panel): a drop on a
    //    docked panel's L/R/T/B edge inserts beside it on THAT side. Then the small centre target → Centre. Then the body's outer rim → that side.
    //    Otherwise Floating. Returns the side plus (for panel-on-panel) the list index of the panel dropped onto, so the caller inserts adjacent.
    WorkspacePanelDockSide ResolvePanelDrop(const WorkspaceRect&                    Body,
                                            const std::vector<WorkspacePanelBox>&   Boxes,
                                            uint32_t                                DraggedIdentifier,
                                            int&                                    OntoIndex)
    {
        OntoIndex = -1;
        // Panel-on-panel: a docked panel's own rim wins (checked topmost-first).
        for (int Index = (int)Boxes.size() - 1; Index >= 0; --Index)
        {
            const WorkspacePanelBox& Box = Boxes[Index];
            if (Box.Identifier == DraggedIdentifier || Box.Dock == WorkspacePanelDockSide::Floating)
                continue;
            const WorkspaceRect Rect = ResolveBoxRect(Box, Body, Boxes);
            const WorkspacePanelDockSide Sub = EdgeSideWithin(Rect, PanelDockBand * 0.6f, false);
            if (Sub != WorkspacePanelDockSide::Floating)
            {
                OntoIndex = Index;
                return Sub;
            }
        }
        // Small centre target → Centre.
        const WorkspaceRect Centre = CentreRegionOf(Body, Boxes);
        if (InsideRect(CentreTargetRect(Centre)))
            return WorkspacePanelDockSide::Centre;
        // Body outer rim → that side (no centre-by-proximity; the small target is the only Centre trigger).
        return EdgeSideWithin(Body, PanelDockBand, false);
    }

    // 📝 What a dragged panel box would land on this frame, resolved from the cursor over the panel tree + floating overlay. Zone is the five-zone
    //    cross result (Center = tab-stack, Left/Right/Top/Bottom = split); None = leave floating. Exactly one of the two targets is set when Zone is
    //    not None: TargetLeaf >= 0 names an existing TREE leaf (the drop splits / stacks it); FloatingTarget != 0 names a FLOATING box (which must be
    //    seeded into the tree first, then split / stacked). A drop landing on empty body carves nothing (None) — panels only dock relative to another
    //    panel, matching the "drop B onto A" model.
    struct PanelDropTarget
    {
        WorkspaceDockZone Zone          = WorkspaceDockZone::None;   // [-] - the five-zone landing
        int               TargetLeaf    = -1;                        // [-] - a tree leaf index (-1 = none)
        uint32_t          FloatingTarget = 0;                        // [-] - a floating box id (0 = none)
    };

    // 📝 The panel drop cross over one rect. Unlike the window dock's THIRDS grid (where every pixel resolves to a zone), a panel cross reserves only
    //    a THIN RIM for the four splits + a SMALL CENTRE SQUARE for the tab-stack; the whole large interior between them returns None so a release
    //    there leaves the panel FLOATING. This is the reported "it wants to dock everywhere so I cannot float it" fix: the split zones now sit only
    //    along the edges. A drop truly on a rim splits; dead centre stacks; anywhere else floats. Small rects clamp the bands so they never overlap.
    WorkspaceDockZone PanelCrossZone(const WorkspaceRect& Rect)
    {
        if (!InsideRect(Rect))
            return WorkspaceDockZone::None;
        const float LocalX = MouseX() - Rect.PositionX;
        const float LocalY = MouseY() - Rect.PositionY;

        // Edge split band: a fixed 28px rim, but never more than ~30% of the span so a tiny panel keeps a live interior + centre square.
        const float EdgeX = std::min(28.0f, Rect.Width  * 0.30f);
        const float EdgeY = std::min(28.0f, Rect.Height * 0.30f);
        if (LocalX < EdgeX)                 return WorkspaceDockZone::Left;
        if (LocalX > Rect.Width  - EdgeX)   return WorkspaceDockZone::Right;
        if (LocalY < EdgeY)                 return WorkspaceDockZone::Top;
        if (LocalY > Rect.Height - EdgeY)   return WorkspaceDockZone::Bottom;

        // Centre tab-stack square: a small 44px box dead-centre; the interior around it (between rim and square) stays None → floats on release.
        const float HalfSquare = 22.0f;
        const float CentreX = Rect.Width  * 0.5f, CentreY = Rect.Height * 0.5f;
        if (std::abs(LocalX - CentreX) <= HalfSquare && std::abs(LocalY - CentreY) <= HalfSquare)
            return WorkspaceDockZone::Center;

        return WorkspaceDockZone::None;   // live interior → the panel stays floating where released
    }

    // 📝 Resolve the dragged box's landing. TREE leaves are tested first (their laid-out rects are current after LayoutPanelRegion); then floating
    //    boxes, front→back (list back = topmost). The dragged box itself is skipped. The cursor over a panel's five-zone cross gives the zone; empty
    //    body → None (stays floating). Pure — reads only laid-out geometry + the cursor, so the input and paint passes agree.
    PanelDropTarget ResolvePanelDrop(WorkspaceDocument& Document, const WorkspaceRect& Body, uint32_t DraggedIdentifier)
    {
        PanelDropTarget Result;

        // Tree leaves (deepest wins; leaves are disjoint so the first containing hit is unique).
        int LeafHit = -1;
        ForEachPanelLeaf(Document, Document.PanelRoot, [&](int LeafIndex)
        {
            if (InsideRect(Document.PanelRegions[LeafIndex].Rect))
                LeafHit = LeafIndex;
        });
        if (LeafHit >= 0)
        {
            const WorkspaceRect&    Rect = Document.PanelRegions[LeafHit].Rect;
            const WorkspaceDockZone Zone = PanelCrossZone(Rect);
            if (Zone != WorkspaceDockZone::None)
            {
                Result.Zone       = Zone;
                Result.TargetLeaf = LeafHit;
                return Result;
            }
        }

        // Floating boxes, topmost first — a floating panel is a valid drop target (this is the case that never fired before: the old resolver skipped
        //    Floating boxes, so panel-on-floating-panel returned nothing). Dropping onto A's cross seeds A into the tree and docks the dragged box.
        for (int Index = (int)Document.PanelBoxes.size() - 1; Index >= 0; --Index)
        {
            const WorkspacePanelBox& Box = Document.PanelBoxes[Index];
            if (Box.Identifier == DraggedIdentifier || Box.Dock != WorkspacePanelDockSide::Floating)
                continue;
            const WorkspaceRect Rect = ResolveBoxRect(Box, Body, Document.PanelBoxes);
            const WorkspaceDockZone Zone = PanelCrossZone(Rect);
            if (Zone != WorkspaceDockZone::None)
            {
                Result.Zone           = Zone;
                Result.FloatingTarget = Box.Identifier;
                return Result;
            }
        }
        return Result;   // None → stays floating
    }

    // The absolute rect the blue preview overlay fills for a resolved landing: the half / centre of the target panel's own rect the drop would take.
    WorkspaceRect PanelDropPreviewRect(WorkspaceDocument& Document, const WorkspaceRect& Body, const PanelDropTarget& Target)
    {
        WorkspaceRect Host;
        if (Target.TargetLeaf >= 0)
            Host = Document.PanelRegions[Target.TargetLeaf].Rect;
        else
        {
            for (const WorkspacePanelBox& Box : Document.PanelBoxes)
                if (Box.Identifier == Target.FloatingTarget) { Host = ResolveBoxRect(Box, Body, Document.PanelBoxes); break; }
        }
        WorkspaceRect Rect = Host;
        switch (Target.Zone)
        {
        case WorkspaceDockZone::Left:   Rect.Width  = Host.Width  * 0.5f; break;
        case WorkspaceDockZone::Right:  Rect.Width  = Host.Width  * 0.5f; Rect.PositionX = Host.PositionX + Host.Width  * 0.5f; break;
        case WorkspaceDockZone::Top:    Rect.Height = Host.Height * 0.5f; break;
        case WorkspaceDockZone::Bottom: Rect.Height = Host.Height * 0.5f; Rect.PositionY = Host.PositionY + Host.Height * 0.5f; break;
        default: break;   // Center → the whole host rect
        }
        return Rect;
    }

    const char* PanelZoneName(WorkspaceDockZone Zone)
    {
        switch (Zone)
        {
        case WorkspaceDockZone::Left:   return "Left(split-column)";
        case WorkspaceDockZone::Right:  return "Right(split-column)";
        case WorkspaceDockZone::Top:    return "Top(split-row)";
        case WorkspaceDockZone::Bottom: return "Bottom(split-row)";
        case WorkspaceDockZone::Center: return "Center(tab-stack)";
        default:                        return "None(floating)";
        }
    }

    // 📝 Paint (only) every panel box a tab hosts, clipped to that tab's body rect. Called INTERLEAVED with chrome — right after the owning
    //    container's own body / tab bar paints — so a container painted LATER (higher in the tab z-order, e.g. a floating window over a leaf) draws
    //    over these panels and hides them, exactly as the tab z-order dictates. Docked boxes paint first (lowest z), floating boxes over them. When
    //    this container owns the active PanelBox drag, the small centre drop target + the live dock preview paint too. Input is resolved separately
    //    in ConstructPanelBoxesForBody so drag / close mutate State against the final geometry.
    void PaintPanelBoxesForBody(ImDrawList*               DrawList,
                                const ThemeConfiguration& Theme,
                                WorkspacePanelDock&       State,
                                uint32_t                  DocumentIdentifier,
                                const WorkspaceRect&      Body,
                                bool                      BlockInput)
    {
        WorkspaceDocument* Document = ResolveDocument(State, DocumentIdentifier);
        if (Document == nullptr || Document->PanelBoxes.empty())
            return;

        DrawList->PushClipRect(ImVec2(Body.PositionX, Body.PositionY),
                               ImVec2(Body.PositionX + Body.Width, Body.PositionY + Body.Height), true);

        // -- DOCKED boxes first (lowest z-order — reserve bands), then FLOATING boxes over them, back-to-front ------------
        for (int Pass = 0; Pass < 2; ++Pass)
        {
            const bool FloatingPass = Pass == 1;
            for (int Index = 0; Index < (int)Document->PanelBoxes.size(); ++Index)
            {
                const WorkspacePanelBox& Box = Document->PanelBoxes[Index];
                if ((Box.Dock == WorkspacePanelDockSide::Floating) != FloatingPass)
                    continue;
                const WorkspaceRect Rect = ResolveBoxRect(Box, Body, Document->PanelBoxes);
                const float CloseLeft = Rect.PositionX + Rect.Width - PanelBoxClose - 3.0f;
                const float CloseTop  = Rect.PositionY + (PanelBoxHeader - PanelBoxClose) * 0.5f;
                const bool  CloseHovered = !BlockInput &&
                                           Inside(CloseLeft, CloseTop, CloseLeft + PanelBoxClose, CloseTop + PanelBoxClose);
                PaintPanelBox(DrawList, Theme, Box, Rect, CloseHovered);
            }
        }

        const bool Dragging = State.DragMode == WorkspaceDragMode::PanelBox && State.DragPanelDocument == DocumentIdentifier;

        // -- The small centre drop target (only while a box is being dragged in this tab) --------------------------------
        if (Dragging)
        {
            const WorkspaceRect Target = CentreTargetRect(CentreRegionOf(Body, Document->PanelBoxes));
            const bool Hot = State.PanelPreviewSide == WorkspacePanelDockSide::Centre;
            const ImU32 Accent = Theme.Palette.SelectionMarker;
            const ImU32 Fill = Hot ? ((Accent & 0x00FFFFFF) | ((ImU32)96 << 24)) : IM_COL32(255, 255, 255, 40);
            DrawList->AddRectFilled(ImVec2(Target.PositionX, Target.PositionY),
                                    ImVec2(Target.PositionX + Target.Width, Target.PositionY + Target.Height), Fill, 4.0f);
            DrawList->AddRect(ImVec2(Target.PositionX, Target.PositionY),
                              ImVec2(Target.PositionX + Target.Width, Target.PositionY + Target.Height),
                              Hot ? Accent : IM_COL32(255, 255, 255, 140), 4.0f, 0, Hot ? 2.0f : 1.0f);
        }

        // -- Live dock preview while a box is being dragged over a side / panel band --------------------------------------
        if (Dragging && State.PanelPreviewSide != WorkspacePanelDockSide::Floating &&
            State.PanelPreviewSide != WorkspacePanelDockSide::Centre)
        {
            // Recompute the onto-panel this frame (paint is separate from input, so we resolve the landing here from the same geometry).
            int PreviewOnto = -1;
            ResolvePanelDrop(Body, Document->PanelBoxes, State.DragPanelBox, PreviewOnto);
            WorkspaceRect Rect;
            if (PreviewOnto >= 0)   // panel-on-panel: highlight the half of the onto-panel the drop would take
            {
                // The box joins the onto-panel's OWN band, so the split follows that band's long axis (vertical for a Left / Right column, horizontal
                //    for a Top / Bottom row) — NOT the rim direction. The rim only picks WHICH half: a Left / Top rim takes the leading half, a
                //    Right / Bottom rim the trailing half. This makes the preview match exactly where the box lands.
                const WorkspaceRect Onto = ResolveBoxRect(Document->PanelBoxes[PreviewOnto], Body, Document->PanelBoxes);
                const WorkspacePanelDockSide OntoSide = Document->PanelBoxes[PreviewOnto].Dock;
                const bool BandVertical = OntoSide == WorkspacePanelDockSide::Left || OntoSide == WorkspacePanelDockSide::Right;
                const bool Trailing = State.PanelPreviewSide == WorkspacePanelDockSide::Right ||
                                      State.PanelPreviewSide == WorkspacePanelDockSide::Bottom;
                Rect = Onto;
                if (BandVertical)
                {
                    Rect.Height *= 0.5f;
                    if (Trailing) Rect.PositionY += Onto.Height * 0.5f;
                }
                else
                {
                    Rect.Width *= 0.5f;
                    if (Trailing) Rect.PositionX += Onto.Width * 0.5f;
                }
            }
            else
            {
                Rect = PanelDockPreviewRect(State.PanelPreviewSide, Body, Document->PanelBoxes);
            }
            const ImU32 Accent = Theme.Palette.SelectionMarker;
            const ImU32 Wash   = (Accent & 0x00FFFFFF) | ((ImU32)64 << 24);
            DrawList->AddRectFilled(ImVec2(Rect.PositionX, Rect.PositionY),
                                    ImVec2(Rect.PositionX + Rect.Width, Rect.PositionY + Rect.Height), Wash);
            DrawList->AddRect(ImVec2(Rect.PositionX + 1.0f, Rect.PositionY + 1.0f),
                              ImVec2(Rect.PositionX + Rect.Width - 1.0f, Rect.PositionY + Rect.Height - 1.0f), Accent, 0.0f, 0, 2.0f);
        }

        DrawList->PopClipRect();
    }

    // Resolve INPUT for every panel box a tab hosts against that tab's body rect (the area below its BarHeight header): press resolution
    //    (close / floating-resize / band-gutter / header-move), advancing an in-flight move / resize / band-gutter drag, and the drop landing.
    //    This does NOT paint — painting runs per-container (PaintPanelBoxesForBody) interleaved with chrome so panels honour the tab z-order.
    //    BlockInput freezes interaction. Returns true if this pass consumed the press (or owns an in-flight band-gutter drag).
    bool ConstructPanelBoxesForBody(WorkspacePanelDock&       State,
                                    uint32_t                  DocumentIdentifier,
                                    const WorkspaceRect&      Body,
                                    bool                      BlockInput)
    {
        WorkspaceDocument* Document = ResolveDocument(State, DocumentIdentifier);
        if (Document == nullptr || Document->PanelBoxes.empty())
            return false;

        const bool Idle = State.DragMode == WorkspaceDragMode::None;
        const bool CanPress = !BlockInput && Clicked() && Idle;

        // -- Press resolution (topmost box first) : close (x) → floating resize handle → band gutter → header move ---------
        //    A DOCKED box has no floating resize handle; instead the shared inner borders (band gutters) resize the bands, so the band-gutter test
        //    runs before the header move and independently of which box is topmost.
        int  PressBox   = -1;
        bool PressClose = false;
        int  PressEdge  = 0;
        for (int Index = (int)Document->PanelBoxes.size() - 1; Index >= 0 && PressBox < 0; --Index)
        {
            const WorkspacePanelBox& Box = Document->PanelBoxes[Index];
            const WorkspaceRect Rect = ResolveBoxRect(Box, Body, Document->PanelBoxes);
            const float CloseLeft = Rect.PositionX + Rect.Width - PanelBoxClose - 3.0f;
            const float CloseTop  = Rect.PositionY + (PanelBoxHeader - PanelBoxClose) * 0.5f;
            if (!CanPress)
                continue;
            if (Inside(CloseLeft, CloseTop, CloseLeft + PanelBoxClose, CloseTop + PanelBoxClose))
            {
                PressBox = Index; PressClose = true; continue;
            }
            const int Edge = Box.Dock == WorkspacePanelDockSide::Floating ? PanelResizeEdgeAt(Rect) : 0;
            if (Edge != 0)
            {
                PressBox = Index; PressEdge = Edge; continue;
            }
            if (Inside(Rect.PositionX, Rect.PositionY, Rect.PositionX + Rect.Width, Rect.PositionY + PanelBoxHeader))
            {
                PressBox = Index;
            }
        }

        // Close (x): drop the box outright, then re-equalise whatever side it left (so its former band members reflow to fill the gap).
        if (PressBox >= 0 && PressClose)
        {
            const WorkspacePanelDockSide Vacated = Document->PanelBoxes[PressBox].Dock;
            Document->PanelBoxes.erase(Document->PanelBoxes.begin() + PressBox);
            if (Vacated != WorkspacePanelDockSide::Floating && Vacated != WorkspacePanelDockSide::Centre)
            {
                const int Members = SideMemberTotal(Document->PanelBoxes, Vacated);
                if (Members > 0)
                    for (WorkspacePanelBox& Box : Document->PanelBoxes)
                        if (Box.Dock == Vacated)
                            Box.SlotFraction = 1.0f / (float)Members;
            }
            return true;
        }

        // A resize-handle or header press lifts the box to the top of the stack and starts the matching drag.
        if (PressBox >= 0)
        {
            WorkspacePanelBox Held = Document->PanelBoxes[PressBox];
            const WorkspaceRect Rect = ResolveBoxRect(Held, Body, Document->PanelBoxes);
            Document->PanelBoxes.erase(Document->PanelBoxes.begin() + PressBox);
            Document->PanelBoxes.push_back(Held);
            State.DragPanelDocument = DocumentIdentifier;
            State.DragPanelBox      = Held.Identifier;
            if (PressEdge != 0)
            {
                State.DragMode        = WorkspaceDragMode::PanelResize;
                State.PanelResizeEdge = PressEdge;
            }
            else
            {
                State.DragMode  = WorkspaceDragMode::PanelBox;
                State.DragGrabX = MouseX() - Rect.PositionX;
                State.DragGrabY = MouseY() - Rect.PositionY;
            }
        }

        // A press on a docked-band gutter (no box header claimed it) starts a band resize instead — outer gutter grows the band depth, an inner
        //    gutter reflows two members' shares.
        if (PressBox < 0 && CanPress)
        {
            WorkspacePanelDockSide GutterSide; int GutterSlot;
            if (BandGutterAt(Body, Document->PanelBoxes, GutterSide, GutterSlot))
            {
                State.DragMode          = WorkspaceDragMode::PanelBandResize;
                State.DragPanelDocument = DocumentIdentifier;
                State.DragPanelBox      = 0;
                State.BandGutterSide    = GutterSide;
                State.BandGutterSlot    = GutterSlot;
            }
        }

        // -- Advance an in-flight move of one of THIS tab's boxes (with live dock preview) --------------------------------
        State.PanelPreviewSide = WorkspacePanelDockSide::Floating;
        int PreviewOnto = -1;
        if (State.DragMode == WorkspaceDragMode::PanelBox && State.DragPanelDocument == DocumentIdentifier)
        {
            for (WorkspacePanelBox& Box : Document->PanelBoxes)
            {
                if (Box.Identifier != State.DragPanelBox)
                    continue;
                // A docked box being dragged first pops back to floating at the cursor so it moves freely (and its old side reflows next frame).
                if (Box.Dock != WorkspacePanelDockSide::Floating)
                    Box.Dock = WorkspacePanelDockSide::Floating;
                float NewX = MouseX() - State.DragGrabX - Body.PositionX;
                float NewY = MouseY() - State.DragGrabY - Body.PositionY;
                Box.OffsetX = std::min(std::max(0.0f, NewX), std::max(0.0f, Body.Width  - Box.Width));
                Box.OffsetY = std::min(std::max(0.0f, NewY), std::max(0.0f, Body.Height - Box.Height));
                State.PanelPreviewSide = ResolvePanelDrop(Body, Document->PanelBoxes, Box.Identifier, PreviewOnto);
                if (!Down())
                {
                    const WorkspacePanelDockSide Landing = State.PanelPreviewSide;
                    if (Landing == WorkspacePanelDockSide::Centre)
                    {
                        EvictCentre(*Document, Box.Identifier);
                        Box.Dock = WorkspacePanelDockSide::Centre;
                    }
                    else if (Landing != WorkspacePanelDockSide::Floating)
                    {
                        DockBoxToSide(*Document, Box.Identifier, Landing, PreviewOnto);
                    }
                    // Floating stays floating (left where the cursor released it).
                    State.DragMode = WorkspaceDragMode::None;
                    State.DragPanelDocument = 0; State.DragPanelBox = 0;
                    State.PanelPreviewSide = WorkspacePanelDockSide::Floating;
                }
                break;
            }
        }

        // -- Advance an in-flight edge / corner resize of a floating box -------------------------------------------------
        if (State.DragMode == WorkspaceDragMode::PanelResize && State.DragPanelDocument == DocumentIdentifier)
        {
            for (WorkspacePanelBox& Box : Document->PanelBoxes)
            {
                if (Box.Identifier != State.DragPanelBox)
                    continue;
                float MinX = Box.OffsetX, MinY = Box.OffsetY;
                float MaxX = Box.OffsetX + Box.Width, MaxY = Box.OffsetY + Box.Height;
                const float LocalX = std::min(std::max(0.0f, MouseX() - Body.PositionX), Body.Width);
                const float LocalY = std::min(std::max(0.0f, MouseY() - Body.PositionY), Body.Height);
                if (State.PanelResizeEdge & EdgeLeft)   MinX = std::min(LocalX, MaxX - PanelBoxMinW);
                if (State.PanelResizeEdge & EdgeRight)  MaxX = std::max(LocalX, MinX + PanelBoxMinW);
                if (State.PanelResizeEdge & EdgeTop)    MinY = std::min(LocalY, MaxY - PanelBoxMinH);
                if (State.PanelResizeEdge & EdgeBottom) MaxY = std::max(LocalY, MinY + PanelBoxMinH);
                Box.OffsetX = MinX; Box.OffsetY = MinY;
                Box.Width   = MaxX - MinX; Box.Height = MaxY - MinY;
                break;
            }
            if (!Down())
            {
                State.DragMode = WorkspaceDragMode::None;
                State.DragPanelDocument = 0; State.DragPanelBox = 0; State.PanelResizeEdge = 0;
            }
        }

        // -- Advance an in-flight docked-band gutter resize ---------------------------------------------------------------
        if (State.DragMode == WorkspaceDragMode::PanelBandResize && State.DragPanelDocument == DocumentIdentifier)
        {
            const WorkspacePanelDockSide Side = State.BandGutterSide;
            const bool Vertical = Side == WorkspacePanelDockSide::Left || Side == WorkspacePanelDockSide::Right;
            if (State.BandGutterSlot < 0)
            {
                // Outer gutter → the band's depth (a fraction of the body span), clamped to [PanelDockMin, PanelDockMax].
                float Depth = 0.0f;
                switch (Side)
                {
                case WorkspacePanelDockSide::Left:   Depth = (MouseX() - Body.PositionX) / std::max(1.0f, Body.Width); break;
                case WorkspacePanelDockSide::Right:  Depth = (Body.PositionX + Body.Width - MouseX()) / std::max(1.0f, Body.Width); break;
                case WorkspacePanelDockSide::Top:    Depth = (MouseY() - Body.PositionY) / std::max(1.0f, Body.Height); break;
                default:                             Depth = (Body.PositionY + Body.Height - MouseY()) / std::max(1.0f, Body.Height); break;
                }
                Depth = std::min(PanelDockMax, std::max(PanelDockMin, Depth));
                for (WorkspacePanelBox& Box : Document->PanelBoxes)
                    if (Box.Dock == Side)
                        Box.DockExtent = Depth;   // every member shares the one band depth
            }
            else
            {
                // Inner gutter between the member at BandGutterSlot and its successor → reflow the two shares by the cursor's position along the band.
                WorkspacePanelBox* Lead = nullptr; WorkspacePanelBox* Next = nullptr;
                int Order = 0;
                for (WorkspacePanelBox& Box : Document->PanelBoxes)
                {
                    if (Box.Dock != Side)
                        continue;
                    if (Order == State.BandGutterSlot)     Lead = &Box;
                    else if (Order == State.BandGutterSlot + 1) { Next = &Box; break; }
                    ++Order;
                }
                if (Lead != nullptr && Next != nullptr)
                {
                    const WorkspaceRect LeadRect = ResolveBoxRect(*Lead, Body, Document->PanelBoxes);
                    const WorkspaceRect NextRect = ResolveBoxRect(*Next, Body, Document->PanelBoxes);
                    const float Pair = Lead->SlotFraction + Next->SlotFraction;
                    float LeadShare;
                    if (Vertical)
                    {
                        const float Lo = LeadRect.PositionY, Hi = NextRect.PositionY + NextRect.Height;
                        LeadShare = (std::min(std::max(MouseY(), Lo), Hi) - Lo) / std::max(1.0f, Hi - Lo);
                    }
                    else
                    {
                        const float Lo = LeadRect.PositionX, Hi = NextRect.PositionX + NextRect.Width;
                        LeadShare = (std::min(std::max(MouseX(), Lo), Hi) - Lo) / std::max(1.0f, Hi - Lo);
                    }
                    LeadShare = std::min(0.9f, std::max(0.1f, LeadShare));
                    Lead->SlotFraction = Pair * LeadShare;
                    Next->SlotFraction = Pair * (1.0f - LeadShare);
                }
            }
            if (!Down())
            {
                State.DragMode = WorkspaceDragMode::None;
                State.DragPanelDocument = 0;
                State.BandGutterSide = WorkspacePanelDockSide::Floating; State.BandGutterSlot = -1;
            }
        }

        return PressBox >= 0 || (State.DragMode == WorkspaceDragMode::PanelBandResize && State.DragPanelDocument == DocumentIdentifier);
    }

    // The body rect of a container (the area below its BarHeight header). Panel boxes live here.
    WorkspaceRect BodyRectOf(float OriginX, float OriginY, float Width, float Height)
    {
        WorkspaceRect Body;
        Body.PositionX = OriginX;
        Body.PositionY = OriginY + BarHeight;
        Body.Width     = Width;
        Body.Height    = std::max(0.0f, Height - BarHeight);
        return Body;
    }

    // Resolve panel-box INPUT (only) for every docked leaf + floating window, TOPMOST-FIRST so a box on the front window wins the press over a leaf's
    //    box beneath it: floating windows front→back (the list is back→front, so walk it in reverse), then the tiled leaves. Once one body consumes
    //    the press every later body is frozen (BlockInput || Consumed). Painting is NOT here — it interleaves with chrome (PaintPanelBoxesForBody).
    bool ConstructPanelBoxesPass(WorkspacePanelDock& State, bool BlockInput)
    {
        bool Consumed = false;
        for (int Index = (int)State.Floating.size() - 1; Index >= 0; --Index)
        {
            const WorkspaceFloatingWindow& Window = State.Floating[Index];
            const WorkspaceRect Body = BodyRectOf(Window.PositionX, Window.PositionY, Window.Width, Window.Height);
            if (ConstructPanelBoxesForBody(State, Window.ActiveIdentifier, Body, BlockInput || Consumed))
                Consumed = true;
        }
        ForEachLeaf(State, State.RootRegion, [&](int Index)
        {
            const WorkspaceRegion& Leaf = State.Regions[Index];
            const WorkspaceRect Body = BodyRectOf(Leaf.Rect.PositionX, Leaf.Rect.PositionY, Leaf.Rect.Width, Leaf.Rect.Height);
            if (ConstructPanelBoxesForBody(State, Leaf.ActiveIdentifier, Body, BlockInput || Consumed))
                Consumed = true;
        });
        return Consumed;
    }

    // Paint every leaf's panel boxes (beneath the floating windows). Called from the top-level construct right AFTER the dock tree paints and BEFORE
    //    the floating windows, so a floating window (and its own panels) draw over any leaf panel it overlaps — the tab z-order hides them.
    void PaintLeafPanelBoxesPass(ImDrawList* DrawList, const ThemeConfiguration& Theme, WorkspacePanelDock& State, bool BlockInput)
    {
        ForEachLeaf(State, State.RootRegion, [&](int Index)
        {
            const WorkspaceRegion& Leaf = State.Regions[Index];
            const WorkspaceRect Body = BodyRectOf(Leaf.Rect.PositionX, Leaf.Rect.PositionY, Leaf.Rect.Width, Leaf.Rect.Height);
            PaintPanelBoxesForBody(DrawList, Theme, State, Leaf.ActiveIdentifier, Body, BlockInput);
        });
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL — PER-HEADER (+) RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // Resolve a click on any docked leaf's per-header (+) button → open the add-menu anchored under that (+), targeting that leaf. (The floating
    //    window (+) is resolved inside ConstructFloating so it batches with its topmost-window press order.) Returns true if a (+) was hit.
    bool ResolveLeafAddButtons(WorkspacePanelDock& State, bool BlockInput)
    {
        if (BlockInput || !Clicked() || State.DragMode != WorkspaceDragMode::None)
            return false;
        bool Hit = false;
        ForEachLeaf(State, State.RootRegion, [&](int Index)
        {
            if (Hit)
                return;
            const WorkspaceRect& Rect = State.Regions[Index].Rect;
            const float HeaderRight = Rect.PositionX + Rect.Width;

            // Far-left (V) panel dropdown → open it targeting THIS leaf's active tab (the panel choice retypes that document).
            if (HeaderPanelHovered(Rect.PositionX, Rect.PositionY))
            {
                Hit = true;
                const ImVec2 Center = HeaderPanelCenter(Rect.PositionX, Rect.PositionY);
                State.PanelMenuOpen           = !State.PanelMenuOpen;
                State.PanelMenuOpenedFrame     = ImGui::GetFrameCount();
                State.PanelMenuAnchorX         = Center.x - HeaderPanelRadius;
                State.PanelMenuAnchorY         = Rect.PositionY + BarHeight + 2.0f;
                State.PanelMenuTargetDocument  = State.PanelMenuOpen ? State.Regions[Index].ActiveIdentifier : 0;
                State.AddMenuOpen              = false;   // the two dropdowns are mutually exclusive
                return;
            }

            if (!HeaderAddHovered(HeaderRight, Rect.PositionY))
                return;
            Hit = true;
            const ImVec2 Center = HeaderAddCenter(HeaderRight, Rect.PositionY);
            State.AddMenuOpen         = !State.AddMenuOpen;
            State.AddMenuOpenedFrame  = ImGui::GetFrameCount();
            State.AddMenuAnchorX      = Center.x - HeaderAddRadius;
            State.AddMenuAnchorY      = Rect.PositionY + BarHeight + 2.0f;
            State.AddMenuTargetRegion = State.AddMenuOpen ? Index : -1;
            State.AddMenuTargetWindow = 0;
            State.PanelMenuOpen       = false;   // mutually exclusive with the (V) panel dropdown
        });
        return Hit;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL — FLOATING WINDOWS + DOCKING
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // Advance active window / resize / partition drags, paint every floating window, then resolve fresh presses on windows, gutters, and
    //    docked-leaf tab bars. Mirrors the HTML ConstructFloating exactly.
    void ConstructFloating(ImDrawList* DrawList, const ThemeConfiguration& Theme, WorkspacePanelDock& State, bool BlockInput)
    {
        // -- Active drag updates ---------------------------------------------------------------------------------------
        if (State.DragMode == WorkspaceDragMode::Window && State.DragWindow != 0)
        {
            WorkspaceFloatingWindow* Window = ResolveWindow(State, State.DragWindow);
            if (Window != nullptr)
            {
                Window->PositionX = MouseX() - State.DragGrabX;
                Window->PositionY = MouseY() - State.DragGrabY;
                ResolveDock(State);
            }
            if (!Down())
            {
                // A window only leaves the floating layer when it is released over a REAL landing zone (edge / corner / centre / tab strip /
                //    another window's tab bar). With no preview, it simply stays floating exactly where it was dropped.
                if (Window != nullptr && State.PreviewZone != WorkspaceDockZone::None)
                {
                    const std::vector<uint32_t> Documents      = Window->Documents;
                    const WorkspaceDockZone     Zone           = State.PreviewZone;
                    const int                   Target         = State.PreviewRegion;
                    const uint32_t              TargetWindowId = State.PreviewWindow;
                    const uint32_t              WindowId       = Window->Identifier;
                    State.Floating.erase(std::remove_if(State.Floating.begin(), State.Floating.end(),
                        [WindowId](const WorkspaceFloatingWindow& Candidate) { return Candidate.Identifier == WindowId; }), State.Floating.end());
                    if (TargetWindowId != 0)   // stack onto another floating window's tab bar
                    {
                        WorkspaceFloatingWindow* Host = ResolveWindow(State, TargetWindowId);
                        if (Host != nullptr)
                        {
                            for (uint32_t Identifier : Documents)
                                Host->Documents.push_back(Identifier);
                            if (!Documents.empty())
                                Host->ActiveIdentifier = Documents.back();
                        }
                    }
                    else
                    {
                        // Drop a stale target (a leaf freed since the preview resolved) back to an outer landing so we never index a freed slot.
                        const bool TargetValid = Target < 0
                            || (Target < (int)State.Regions.size() && State.Regions[Target].Occupied && State.Regions[Target].Leaf);
                        ApplyDock(State, Documents, Zone, TargetValid ? Target : -1);
                    }
                    PruneAll(State);
                }
                State.DragMode = WorkspaceDragMode::None;
                State.DragWindow = 0;
                State.PreviewZone = WorkspaceDockZone::None;
                State.PreviewWindow = 0;
            }
        }
        else if (State.DragMode == WorkspaceDragMode::Resize && State.DragWindow != 0)
        {
            WorkspaceFloatingWindow* Window = ResolveWindow(State, State.DragWindow);
            if (Window != nullptr)
            {
                Window->Width  = std::max(MinWidth,  MouseX() - Window->PositionX);
                Window->Height = std::max(MinHeight, MouseY() - Window->PositionY);
            }
            if (!Down()) { State.DragMode = WorkspaceDragMode::None; State.DragWindow = 0; }
        }
        else if (State.DragMode == WorkspaceDragMode::Partition && State.DragRegion >= 0)
        {
            // Guard the region index: a prune (from closing a doc mid-drag) can free it, and a partition that collapsed becomes a leaf.
            if (State.DragRegion < (int)State.Regions.size()
                && State.Regions[State.DragRegion].Occupied && !State.Regions[State.DragRegion].Leaf)
            {
                WorkspaceRegion& Node = State.Regions[State.DragRegion];
                const WorkspaceRect& Rect = Node.Rect;
                if (Node.Axis == WorkspacePartitionAxis::Row)
                    Node.Ratio = std::max(0.1f, std::min(0.9f, (MouseX() - Rect.PositionX) / Rect.Width));
                else
                    Node.Ratio = std::max(0.1f, std::min(0.9f, (MouseY() - Rect.PositionY) / Rect.Height));
            }
            if (!Down()) { State.DragMode = WorkspaceDragMode::None; State.DragRegion = -1; }
        }

        // -- Paint windows bottom → top (back = topmost). Each window's own panel boxes paint inside PaintFloatingWindow, right after its chrome,
        //    so a higher window covers a lower one's panels (tab z-order). ---------------------------------------------------
        for (size_t Index = 0; Index < State.Floating.size(); ++Index)
            PaintFloatingWindow(DrawList, Theme, State, State.Floating[Index], Index + 1 == State.Floating.size(), BlockInput);

        // -- Pending docked-tab press → tear out only once dragged past the threshold ----------------------------------
        if (State.PendingTabDocument != 0)
        {
            constexpr float TearDrag = 6.0f;   // [px] - cursor travel from the press point before a click becomes a tear
            const float Travel = std::abs(MouseX() - State.PendingTabPressX) + std::abs(MouseY() - State.PendingTabPressY);
            if (!Down())
            {
                State.PendingTabDocument = 0;   // released without moving → it was a plain activate; nothing to detach
            }
            else if (Travel > TearDrag && State.DragMode == WorkspaceDragMode::None)
            {
                const uint32_t Identifier = State.PendingTabDocument;
                const float    GrabLeft   = State.PendingTabGrabLeft;
                State.PendingTabDocument = 0;
                FloatDocument(State, Identifier, MouseX() - GrabLeft, 15.0f);   // now drag it out into a floating window
            }
        }

        // -- Fresh presses (only when idle) ----------------------------------------------------------------------------
        if (BlockInput || !Clicked() || State.DragMode != WorkspaceDragMode::None)
            return;

        // 📝 A fresh press anywhere commits an in-flight title edit (click-elsewhere = confirm), matching a normal text field losing focus. The
        //    caret stops blinking and the typed name settles. If the press then re-lands on the same caption, the Rename hit below simply reopens
        //    the edit — so double-clicking a tab to rename it again still works.
        if (State.Rename.TargetIdentifier != 0)
            CommitRename(State);

        // Floating windows first (topmost down).
        for (int Index = (int)State.Floating.size() - 1; Index >= 0; --Index)
        {
            const WorkspaceFloatingWindow& Probe = State.Floating[Index];
            const bool InsideWindow = Inside(Probe.PositionX, Probe.PositionY, Probe.PositionX + Probe.Width, Probe.PositionY + Probe.Height);
            const bool OnGrip = Inside(Probe.PositionX + Probe.Width - ResizeGrip, Probe.PositionY + Probe.Height - ResizeGrip,
                                       Probe.PositionX + Probe.Width, Probe.PositionY + Probe.Height);
            if (!InsideWindow && !OnGrip)
                continue;

            // Bring to front.
            WorkspaceFloatingWindow Front = State.Floating[Index];
            State.Floating.erase(State.Floating.begin() + Index);
            State.Floating.push_back(Front);
            WorkspaceFloatingWindow& Window = State.Floating.back();

            if (OnGrip)
            {
                State.DragMode = WorkspaceDragMode::Resize;
                State.DragWindow = Window.Identifier;
                return;
            }

            // This window's own far-left (V) panel dropdown → open it targeting this window's active tab.
            if (HeaderPanelHovered(Window.PositionX, Window.PositionY))
            {
                const ImVec2 Center = HeaderPanelCenter(Window.PositionX, Window.PositionY);
                State.PanelMenuOpen           = !State.PanelMenuOpen;
                State.PanelMenuOpenedFrame     = ImGui::GetFrameCount();
                State.PanelMenuAnchorX         = Center.x - HeaderPanelRadius;
                State.PanelMenuAnchorY         = Window.PositionY + BarHeight + 2.0f;
                State.PanelMenuTargetDocument  = State.PanelMenuOpen ? Window.ActiveIdentifier : 0;
                State.AddMenuOpen              = false;   // mutually exclusive with the (+) dropdown
                return;
            }

            // This window's own per-header (+) → open the add-menu targeting this window.
            if (HeaderAddHovered(Window.PositionX + Window.Width, Window.PositionY))
            {
                const ImVec2 Center = HeaderAddCenter(Window.PositionX + Window.Width, Window.PositionY);
                State.AddMenuOpen         = !State.AddMenuOpen;
                State.AddMenuOpenedFrame  = ImGui::GetFrameCount();
                State.AddMenuAnchorX      = Center.x - HeaderAddRadius;
                State.AddMenuAnchorY      = Window.PositionY + BarHeight + 2.0f;
                State.AddMenuTargetWindow = State.AddMenuOpen ? Window.Identifier : 0;
                State.AddMenuTargetRegion = -1;
                State.PanelMenuOpen       = false;   // mutually exclusive with the (V) panel dropdown
                return;
            }

            const WorkspaceTabHit HitResult = HitTabBar(Window.PositionX, Window.PositionY, Window.Documents, Window.ActiveIdentifier);
            if (HitResult.Action == WorkspaceTabAction::Rename) { BeginRename(State, HitResult.Document); return; }
            if (HitResult.Action == WorkspaceTabAction::Close)  { CloseDocument(State, HitResult.Document); return; }
            if (HitResult.Action == WorkspaceTabAction::Relocate)
            {
                Window.ActiveIdentifier = HitResult.Document;
                if (Window.Documents.size() == 1)
                {
                    // A lone tab IS the window — pressing it just moves the whole window.
                    State.DragMode   = WorkspaceDragMode::Window;
                    State.DragOrigin = WorkspaceDragOrigin::Panel;   // moving a window bodily → five-zone cross
                    State.DragWindow = Window.Identifier;
                    State.DragGrabX  = MouseX() - Window.PositionX;
                    State.DragGrabY  = MouseY() - Window.PositionY;
                }
                else
                {
                    // Multi-tab window: a plain click only activates; pulling the tab into its own window is deferred until it drags past the
                    //    threshold (resolved above), so switching tabs inside a window never detaches or reorders them.
                    State.PendingTabDocument = HitResult.Document;
                    State.PendingTabGrabLeft = HitResult.GrabLeft;
                    State.PendingTabPressX   = MouseX();
                    State.PendingTabPressY   = MouseY();
                }
                return;
            }

            // Pressed the window body (not a tab / grip) → move it.
            if (Inside(Window.PositionX, Window.PositionY, Window.PositionX + Window.Width, Window.PositionY + BarHeight))
            {
                State.DragMode   = WorkspaceDragMode::Window;
                State.DragOrigin = WorkspaceDragOrigin::Panel;   // moving a window bodily → five-zone cross
                State.DragWindow = Window.Identifier;
                State.DragGrabX  = MouseX() - Window.PositionX;
                State.DragGrabY  = MouseY() - Window.PositionY;
            }
            return;
        }

        // Then split gutters, then docked-leaf tab bars.
        bool Handled = false;
        ForEachPartition(State, State.RootRegion, [&](int RegionIndex)
        {
            if (!Handled && InsideRect(State.Regions[RegionIndex].Gutter))
            {
                State.DragMode = WorkspaceDragMode::Partition;
                State.DragRegion = RegionIndex;
                Handled = true;
            }
        });
        if (Handled)
            return;

        ForEachLeaf(State, State.RootRegion, [&](int RegionIndex)
        {
            if (Handled)
                return;
            WorkspaceRegion& Leaf = State.Regions[RegionIndex];
            const WorkspaceTabHit HitResult = HitTabBar(Leaf.Rect.PositionX, Leaf.Rect.PositionY, Leaf.Documents, Leaf.ActiveIdentifier);
            if (HitResult.Action == WorkspaceTabAction::None)
                return;
            Handled = true;
            if (HitResult.Action == WorkspaceTabAction::Rename)        BeginRename(State, HitResult.Document);
            else if (HitResult.Action == WorkspaceTabAction::Close)    CloseDocument(State, HitResult.Document);
            else if (HitResult.Action == WorkspaceTabAction::Relocate)
            {
                // A plain click only ACTIVATES the tab (position + size stay put). Tearing out to a floating window is deferred until the cursor
                //    actually drags past a threshold (resolved below), so switching between |Tab A||Tab B| never detaches or rearranges them.
                Leaf.ActiveIdentifier      = HitResult.Document;
                State.PendingTabDocument   = HitResult.Document;
                State.PendingTabGrabLeft   = HitResult.GrabLeft;
                State.PendingTabPressX     = MouseX();
                State.PendingTabPressY     = MouseY();
            }
        });
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL — (+) ADD-MENU OVERLAY
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr float AddMenuWidth    = 200.0f;   // [px]
    constexpr float AddMenuRowHeight = 26.0f;   // [px]
    constexpr float AddMenuPadY      = 6.0f;    // [px] - top/bottom inset inside the overlay

    // Paint + resolve the (+) add-document overlay on the FOREGROUND draw list — the same top layer as the trapezoid tabs, so it is never
    //    occluded by them (the previous ImGui popup drew below the foreground list). A click on an entry adds that workspace; a click anywhere
    //    else (or Escape) closes it. Returns true while the overlay is showing so the strip can ignore stray clicks that belong to the menu.
    bool ConstructAddMenu(ImDrawList* DrawList, const ThemeConfiguration& Theme, WorkspacePanelDock& State)
    {
        if (!State.AddMenuOpen)
            return false;

        int CatalogueCount = 0;
        const WorkspaceDocumentType* Catalogue = ActiveCatalogue(State, CatalogueCount);

        const float Width  = AddMenuWidth;
        const float Height = AddMenuPadY * 2.0f + AddMenuRowHeight * (float)CatalogueCount;

        // Clamp the overlay inside the viewport so an anchor near the right / bottom edge (a (+) on the last tab) never spills off-screen and
        //    gets clipped. Shift left / up by exactly the overhang, then floor at the work area's top-left.
        const ImGuiViewport* Viewport = ImGui::GetMainViewport();
        const float MarginEdge = 4.0f;
        const float MaxLeft = Viewport->WorkPos.x + Viewport->WorkSize.x - Width  - MarginEdge;
        const float MaxTop  = Viewport->WorkPos.y + Viewport->WorkSize.y - Height - MarginEdge;
        const float Left   = std::max(Viewport->WorkPos.x + MarginEdge, std::min(State.AddMenuAnchorX, MaxLeft));
        const float Top    = std::max(Viewport->WorkPos.y + MarginEdge, std::min(State.AddMenuAnchorY, MaxTop));
        const float Right  = Left + Width;
        const float Bottom = Top + Height;

        DrawList->AddRectFilled(ImVec2(Left, Top), ImVec2(Right, Bottom), Theme.Palette.PanelBackground, 4.0f);
        DrawList->AddRect(ImVec2(Left + 0.5f, Top + 0.5f), ImVec2(Right - 0.5f, Bottom - 0.5f), Theme.Palette.PanelBorder, 4.0f);

        int Chosen = -1;
        for (int Index = 0; Index < CatalogueCount; ++Index)
        {
            const float RowTop = Top + AddMenuPadY + (float)Index * AddMenuRowHeight;
            const bool  Hovered = Inside(Left, RowTop, Right, RowTop + AddMenuRowHeight);
            if (Hovered)
                DrawList->AddRectFilled(ImVec2(Left + 3.0f, RowTop), ImVec2(Right - 3.0f, RowTop + AddMenuRowHeight), Theme.Palette.ControlHovered, 3.0f);
            // A grey bullet (accent on hover) so the menu reads the same grey/black + accent language as the tabs — no per-category hue.
            DrawList->AddRectFilled(ImVec2(Left + 10.0f, RowTop + AddMenuRowHeight * 0.5f - 5.0f),
                                    ImVec2(Left + 20.0f, RowTop + AddMenuRowHeight * 0.5f + 5.0f),
                                    Hovered ? Theme.Palette.AccentPrimary : Theme.Palette.TextMuted, 2.0f);
            DrawList->AddText(ImVec2(Left + 30.0f, RowTop + (AddMenuRowHeight - ImGui::GetTextLineHeight()) * 0.5f),
                              Theme.Palette.TextPrimary, Catalogue[Index].Label);
            if (Hovered && Clicked())
                Chosen = Index;
        }

        if (Chosen >= 0)
        {
            // A new instance is "<NameStem> N" (the lowest free number for THAT stem), carrying the type's category. The stem comes from the
            //    app-supplied catalogue, so ParametricSketcher yields "Sketch 1", the Editor yields "PaintWorkspace 1" / "Sketch 1" / …
            const WorkspaceDocumentType& Type = Catalogue[Chosen];
            char TabTitle[64];
            ResolveNumberedTitle(State, Type.NameStem, TabTitle, sizeof(TabTitle));
            AttachDocument(State, TabTitle, Type.Category,
                           State.AddMenuTargetRegion, State.AddMenuTargetWindow);
            State.AddMenuOpen         = false;
            State.AddMenuTargetRegion = -1;
            State.AddMenuTargetWindow = 0;
        }
        // Dismiss on Escape or a click that lands outside the overlay (but not the (+) button itself — that toggle is handled in the strip).
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            State.AddMenuOpen = false;
        }
        else if (Clicked() && !Inside(Left, Top, Right, Bottom) && ImGui::GetFrameCount() != State.AddMenuOpenedFrame)
        {
            State.AddMenuOpen = false;   // the opening click (same frame) is exempt — it lands on the (+) button, outside this rect
        }
        return State.AddMenuOpen;
    }

    // Paint + resolve the (V) PANEL dropdown on the foreground draw list. Choosing a row DROPS a fresh "Panel N" box into the current tab's
    //    (PanelMenuTargetDocument) body — a real free-floating rectangle with a header + close (x). The tab keeps its title and is NOT duplicated.
    //    Same dismiss rules as the (+) overlay. Returns true while it is showing so the layers beneath ignore stray clicks.
    bool ConstructPanelMenu(ImDrawList* DrawList, const ThemeConfiguration& Theme, WorkspacePanelDock& State)
    {
        if (!State.PanelMenuOpen)
            return false;

        const float Width  = AddMenuWidth;
        const float Height = AddMenuPadY * 2.0f + AddMenuRowHeight * (float)PanelMenuEntryCount;

        const ImGuiViewport* Viewport = ImGui::GetMainViewport();
        const float MarginEdge = 4.0f;
        const float MaxLeft = Viewport->WorkPos.x + Viewport->WorkSize.x - Width  - MarginEdge;
        const float MaxTop  = Viewport->WorkPos.y + Viewport->WorkSize.y - Height - MarginEdge;
        const float Left   = std::max(Viewport->WorkPos.x + MarginEdge, std::min(State.PanelMenuAnchorX, MaxLeft));
        const float Top    = std::max(Viewport->WorkPos.y + MarginEdge, std::min(State.PanelMenuAnchorY, MaxTop));
        const float Right  = Left + Width;
        const float Bottom = Top + Height;

        DrawList->AddRectFilled(ImVec2(Left, Top), ImVec2(Right, Bottom), Theme.Palette.PanelBackground, 4.0f);
        DrawList->AddRect(ImVec2(Left + 0.5f, Top + 0.5f), ImVec2(Right - 0.5f, Bottom - 0.5f), Theme.Palette.PanelBorder, 4.0f);

        int Chosen = -1;
        for (int Index = 0; Index < PanelMenuEntryCount; ++Index)
        {
            const float RowTop = Top + AddMenuPadY + (float)Index * AddMenuRowHeight;
            const bool  Hovered = Inside(Left, RowTop, Right, RowTop + AddMenuRowHeight);
            if (Hovered)
                DrawList->AddRectFilled(ImVec2(Left + 3.0f, RowTop), ImVec2(Right - 3.0f, RowTop + AddMenuRowHeight), Theme.Palette.ControlHovered, 3.0f);
            DrawList->AddRectFilled(ImVec2(Left + 10.0f, RowTop + AddMenuRowHeight * 0.5f - 5.0f),
                                    ImVec2(Left + 20.0f, RowTop + AddMenuRowHeight * 0.5f + 5.0f),
                                    Hovered ? Theme.Palette.AccentPrimary : Theme.Palette.TextMuted, 2.0f);
            DrawList->AddText(ImVec2(Left + 30.0f, RowTop + (AddMenuRowHeight - ImGui::GetTextLineHeight()) * 0.5f),
                              Theme.Palette.TextPrimary, PanelMenuEntries[Index]);
            if (Hovered && Clicked())
                Chosen = Index;
        }

        if (Chosen >= 0)
        {
            // Drop a fresh "Panel N" box into the current tab's body. Its title (e.g. "Tab 1") is untouched; no tab is added. Successive spawns
            //    cascade down-right so a new box never lands exactly on the last one.
            WorkspaceDocument* Target = ResolveDocument(State, State.PanelMenuTargetDocument);
            if (Target != nullptr)
            {
                WorkspacePanelBox Box;
                Box.Identifier = Target->NextPanel;
                std::snprintf(Box.Title, sizeof(Box.Title), "Panel %u", Target->NextPanel);
                const float Step = 26.0f;
                const int   Cascade = (int)(Target->NextPanel - 1) % 6;
                Box.OffsetX = 24.0f + Step * (float)Cascade;
                Box.OffsetY = 24.0f + Step * (float)Cascade;
                Target->PanelBoxes.push_back(Box);
                ++Target->NextPanel;
            }
            State.PanelMenuOpen           = false;
            State.PanelMenuTargetDocument = 0;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            State.PanelMenuOpen = false;
        }
        else if (Clicked() && !Inside(Left, Top, Right, Bottom) && ImGui::GetFrameCount() != State.PanelMenuOpenedFrame)
        {
            State.PanelMenuOpen = false;   // the opening click (same frame) is exempt — it lands on the (V) button, outside this rect
        }
        return State.PanelMenuOpen;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Reseed the desk with ONE instance of the given type: drop the tree + registry, then add "<NameStem> 1" into a fresh root leaf and activate
    //    it. Shared by boot (generic "Tab") and ConfigureWorkspaceCatalogue (the app's default type). No global strip — the leaf owns its own tabs.
    void SeedRootDocument(WorkspacePanelDock& State, const WorkspaceDocumentType& Type)
    {
        State.Documents.clear();
        State.Regions.clear();
        State.Floating.clear();
        State.RootRegion   = -1;
        State.NextDocument = 1;
        State.NextWindow   = 1;

        char TabTitle[64];
        ResolveNumberedTitle(State, Type.NameStem, TabTitle, sizeof(TabTitle));
        AttachDocument(State, TabTitle, Type.Category, -1, 0);
        if (State.RootRegion >= 0 && State.Regions[State.RootRegion].Leaf && !State.Regions[State.RootRegion].Documents.empty())
            State.Regions[State.RootRegion].ActiveIdentifier = State.Regions[State.RootRegion].Documents.front();
    }
}

void InitializeWorkspacePanelDock(WorkspacePanelDock& State)
{
    State.DocumentCatalogue.clear();
    State.DefaultCatalogueIndex = 0;
    // Seed the ROOT dock leaf (filling the window) with one generic "Tab 1"; ConfigureWorkspaceCatalogue reseeds it with the app's own type.
    SeedRootDocument(State, GenericDocumentType);
}


void ConfigureWorkspaceCatalogue(WorkspacePanelDock& State, const WorkspaceDocumentType* Types, int Count, int DefaultTypeIndex)
{
    State.DocumentCatalogue.clear();
    for (int Index = 0; Index < Count; ++Index)
        State.DocumentCatalogue.push_back(Types[Index]);

    State.DefaultCatalogueIndex = (DefaultTypeIndex >= 0 && DefaultTypeIndex < Count) ? DefaultTypeIndex : 0;

    // Reseed the desk with the default type so the first tab reads (e.g.) "Sketch 1" rather than the generic "Tab 1".
    const WorkspaceDocumentType& Default =
        State.DocumentCatalogue.empty() ? GenericDocumentType : State.DocumentCatalogue[State.DefaultCatalogueIndex];
    SeedRootDocument(State, Default);
}

void ConstructWorkspacePanelDock(const ThemeConfiguration& Theme, WorkspacePanelDock& State)
{
    const ImGuiViewport* Viewport = ImGui::GetMainViewport();
    ImDrawList*          DrawList = ImGui::GetForegroundDrawList();
    // While either the (+) or the (V) overlay is open, the strip / dock / floating layers ignore clicks (a menu click must not fall through to a
    //    tab beneath it).
    const bool           BlockInput = State.AddMenuOpen || State.PanelMenuOpen;

    // Desk fill behind the central area. With no root leaf and no floating windows the desk is FULL BLACK (Chrome: no workspaces = empty).
    const WorkspaceRect Central = CentralRect();
    const bool DeskEmpty = State.RootRegion < 0 && State.Floating.empty();
    DrawList->AddRectFilled(ImVec2(Central.PositionX, Central.PositionY),
                            ImVec2(Central.PositionX + Central.Width, Central.PositionY + Central.Height),
                            DeskEmpty ? IM_COL32(0, 0, 0, 255) : Theme.Palette.DeskBackground);

    // Empty-desk prompt: a centered (+) button (opens the add-menu → the chosen workspace becomes the root leaf) plus a one-line hint.
    if (DeskEmpty)
    {
        const ImVec2 Center(Central.PositionX + Central.Width * 0.5f, Central.PositionY + Central.Height * 0.5f - 14.0f);
        const float  Radius = 26.0f;
        const float  Dx = MouseX() - Center.x, Dy = MouseY() - Center.y;
        const bool   Hovered = Dx * Dx + Dy * Dy <= Radius * Radius;
        DrawList->AddCircle(Center, Radius, Hovered ? Theme.Palette.AccentPrimary : Theme.Palette.PanelBorder, 32, 2.0f);
        DrawPlus(DrawList, Center, 24.0f, Hovered ? Theme.Palette.TextPrimary : Theme.Palette.TextMuted);
        const char* Hint = "click + to open a workspace";
        const ImVec2 Size = ImGui::CalcTextSize(Hint);
        DrawList->AddText(ImVec2(Center.x - Size.x * 0.5f, Center.y + Radius + 12.0f), Theme.Palette.TextMuted, Hint);
        if (!BlockInput && Hovered && Clicked())
        {
            State.AddMenuOpen         = true;
            State.AddMenuOpenedFrame  = ImGui::GetFrameCount();
            State.AddMenuAnchorX      = Center.x - AddMenuWidth * 0.5f;
            State.AddMenuAnchorY      = Center.y + Radius + 28.0f;
            State.AddMenuTargetRegion = -1;   // no leaf yet → AttachDocument seeds the root
            State.AddMenuTargetWindow = 0;
        }
    }

    // Lay out the dock tree, paint it + floating windows + the live dock preview, and resolve per-header (+) clicks (no global strip anymore).
    if (State.RootRegion >= 0)
        LayoutRegion(State, State.RootRegion, Central);
    // Panel-box INPUT (drag / resize / close / dock) resolves FIRST — right after layout, before any painting — so every box's geometry is final
    //    for this frame and the paints below (interleaved with chrome) show no drag lag. Topmost-first so the front window's box wins the press.
    ConstructPanelBoxesPass(State, BlockInput);
    PaintDockTree(DrawList, Theme, State);
    // Leaf panels paint AFTER the dock tree but BEFORE the floating windows, so a floating window (and its own panels, painted inside
    //    ConstructFloating) draws over any leaf panel it overlaps — panels honour the tab z-order rather than always sitting on top.
    PaintLeafPanelBoxesPass(DrawList, Theme, State, BlockInput);
    ResolveLeafAddButtons(State, BlockInput);
    ConstructFloating(DrawList, Theme, State, BlockInput);
    PaintDockPreview(DrawList, Theme, State);

    // The (+) and (V) overlays are painted LAST so they sit on top of the strip / dock / windows on the same foreground layer (fixes the old
    //    popup-behind-tabs Z-order). They resolve their own clicks; BlockInput above already froze the layers beneath them this frame. Only one is
    //    ever open at a time (opening one closes the other), so their click resolution never collides.
    ConstructAddMenu(DrawList, Theme, State);
    ConstructPanelMenu(DrawList, Theme, State);

    ResolveRename(State);
}

}   // namespace Frontier
