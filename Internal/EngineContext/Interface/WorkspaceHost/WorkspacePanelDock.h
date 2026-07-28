/*==============================================================================================================================================
                                                            WORKSPACEPANELDOCK.H
==============================================================================================================================================*/
// 🧩 The chrome-style workspace dock, shared UI. EVERY tab — docked region or floating window — is a CUSTOM trapezoid drawn on the foreground
//    draw list (ImGui's own tab bar cannot be a trapezoid). The docking model is OURS, not ImGui's: a recursive partition tree splits the
//    workspace area into leaves, draggable gutters resize the split, and a torn-off tab becomes a rectangular floating window whose header also
//    carries trapezoid tabs. Dropping a floating window on a leaf's left / right / top / bottom edge splits it; dropping on the centre stacks it
//    as tabs — exactly like ImGui docking, but we own it end to end. Double-click a trapezoid to rename it inline. This is the interior dock the
//    WorkspaceDockHost drives; the host keeps the workspace-registration API, this owns the freeform docking. Interface.lib + vendored ImGui only.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_WORKSPACEPANELDOCK_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_WORKSPACEPANELDOCK_H

#include "../Theme/ThemeConfiguration.h"
#include "../Components/Controls/InlineTextEditor.h"
#include "SketchOutliner/SketchOutlinerPanel.h"

#include <vector>
#include <cstdint>

namespace Frontier
{

struct SvgIconRegistry;

//------------------------------------------------------------------------------------------------------------------------
//                                                          ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which workspace a tab belongs to (its broad category). The (+) dropdown assigns one; the leaf / window placeholder prints it. (Field is
//    `.Category` per the naming skills — the banned `Kind` / `Role` are deliberately avoided.) This is NOT the panel a tab hosts — see WorkspacePanel.
enum class WorkspaceCategory
{
    Empty      = 0,   // [-] - blank workspace
    Viewport   = 1,   // [-] - 3D viewport workspace
    Modeling   = 2,   // [-] - modelling workspace
    Painting   = 3,   // [-] - 3D texture-painting workspace
    UV         = 4,   // [-] - 2D UV editing workspace
    Draughting = 5,   // [-] - 2D draughting workspace
    Terrain    = 6,   // [-] - SDF terrain workspace
    Baking     = 7,   // [-] - texture-bake workspace
    Simulation = 8,   // [-] - physics-simulation workspace
};


// 📝 One document type the (+) dropdown offers, supplied by the application (a standalone app hands ONE; the Editor hands all six). Label is the
//    (+) menu row text; NameStem is the tab-caption stem the numbered instance carries ("<NameStem> N", e.g. "Sketch 1", "PaintWorkspace 2");
//    Category tints the placeholder text + drives per-type instance numbering. The dock never hardcodes a workspace name — the app owns the list.
struct WorkspaceDocumentType
{
    const char*       Label     = "New Tab";                  // [-] - (+) menu row caption
    const char*       NameStem  = "Tab";                      // [-] - tab-caption stem; instances read "<NameStem> N"
    WorkspaceCategory Category  = WorkspaceCategory::Empty;    // [-] - the spawned document's broad category
};

// 📝 The one drag intent in flight at a time. Reorder = a held top-strip trapezoid slides sideways (tears off past the threshold);
//    Window = a floating window is being moved (and may dock on release); Resize = a floating window's grip; Partition = a split gutter.
enum class WorkspaceDragMode
{
    None      = 0,   // [-] - nothing held
    Reorder   = 1,   // [-] - a top-strip trapezoid is held
    Window    = 2,   // [-] - a floating window is being moved
    Resize    = 3,   // [-] - a floating window's resize grip is held
    Partition = 4,   // [-] - a split gutter is being dragged
    PanelBox  = 5,   // [-] - a panel box is being moved within its tab body
    PanelResize = 6, // [-] - a floating panel box edge / corner handle is being dragged
    PanelBandResize = 7, // [-] - a docked band's gutter (outer band depth or inner panel split) is being dragged
};

// 📝 What a Window-mode drag was torn from — the drop model differs by origin. Tab: a document tab pulled off its strip; it FLOATS by default and
//    docks only in a distinct edge / corner band (or onto an actual tab strip), so a casual drop leaves it floating. Panel: a panel window moved
//    bodily; it uses the five-zone cross (centre square stacks, four bands split), so a drop over any leaf always docks. ResolveDock branches on this.
enum class WorkspaceDragOrigin
{
    Tab   = 0,   // [-] - torn from a document tab strip — float-by-default, edge / corner docking only
    Panel = 1,   // [-] - a panel window moved bodily — five-zone cross, always docks over a leaf
};

// 📝 Where a partition divides its two children. Row = a vertical gutter (left / right children); Column = a horizontal gutter (top / bottom).
enum class WorkspacePartitionAxis
{
    Row    = 0,   // [-] - children side by side, vertical gutter
    Column = 1,   // [-] - children stacked, horizontal gutter
};

// 📝 The landing zone a dragged window would dock into, resolved each frame from the cursor. Side selects the split / stack.
enum class WorkspaceDockZone
{
    None   = 0,   // [-] - not over a valid target
    Strip  = 1,   // [-] - back onto the top strip
    Center = 2,   // [-] - stack as tabs on the hovered leaf (or fill the empty area)
    Left   = 3,   // [-] - split: new region on the left
    Right  = 4,   // [-] - split: new region on the right
    Top    = 5,   // [-] - split: new region on top
    Bottom = 6,   // [-] - split: new region on the bottom
};


//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Where a panel box is anchored inside its tab body. Floating = a free rectangle overlaid on the body. The four docked sides RESERVE a band:
//    Left / Right take a full-height vertical column, Top / Bottom take a horizontal row spanning the width BETWEEN the columns, and Centre fills
//    whatever those leave. Multiple panels may share a side — they SPLIT the band along its long axis (Left / Right split vertically, Top / Bottom
//    split horizontally) in list order, each keeping SlotFraction of the band. Centre holds one panel (a new Centre dock evicts the prior one).
//    Docked panels always paint BENEATH floating panels (lowest z-order).
enum class WorkspacePanelDockSide
{
    Floating = 0,   // [-] - free rectangle overlaid on the body
    Left     = 1,   // [-] - reserved full-height column on the body's left
    Right    = 2,   // [-] - reserved full-height column on the body's right
    Top      = 3,   // [-] - reserved row along the top, between the Left / Right columns
    Bottom   = 4,   // [-] - reserved row along the bottom, between the Left / Right columns
    Centre   = 5,   // [-] - fills the middle the docked sides leave
};

// 📝 What a panel box actually shows in its body. Placeholder = the blank "Panel N" chrome (a plain dark body). SketchOutliner = the box hosts the
//    parametric-sketch modeling tree (ConstructSketchOutlinerPanel drawn into the body). A tab may hold at most ONE SketchOutliner box (the (V)
//    dropdown greys its row once the tab already carries one). New categories (a real 3D viewport, a property inspector, …) append here.
enum class WorkspacePanelContent
{
    Placeholder    = 0,   // [-] - blank "Panel N" chrome (dark body, no content)
    SketchOutliner = 1,   // [-] - the parametric-sketch modeling tree (one per tab)
};

// 📝 One panel box dropped inside a tab's body: a rectangle with a black header strip ("Panel N" + close (x)), a dark-grey body, and a footer
//    strip. It is FLOATING (a free overlay positioned by Offset* / sized by Width / Height) until dragged onto an edge / centre band, which DOCKS
//    it to that side (see WorkspacePanelDockSide). Offset* is relative to the tab body's top-left so a floating box tracks the body on move / resize.
//    DockExtent is the docked BAND's depth (a fraction of the body span — column width for Left / Right, row height for Top / Bottom; the band takes
//    the first member's DockExtent). SlotFraction is this panel's share of its band's long axis when several panels share the side (normalised across
//    the side each frame). Both are unused while Floating / Centre. Content selects what fills the body — see WorkspacePanelContent.
struct WorkspacePanelBox
{
    uint32_t               Identifier   = 0;                           // [-] - unique-within-its-tab box key
    char                   Title[64]    = {};                          // [-] - header label, e.g. "Panel 1"
    WorkspacePanelContent  Content      = WorkspacePanelContent::Placeholder; // [-] - what the body draws (blank chrome or the sketch outliner)
    WorkspacePanelDockSide Dock         = WorkspacePanelDockSide::Floating; // [-] - anchor: floating overlay or a reserved body side
    float                  OffsetX      = 0.0f;                        // [px] - floating top-left x, relative to the tab body's top-left
    float                  OffsetY      = 0.0f;                        // [px] - floating top-left y, relative to the tab body's top-left
    float                  Width        = 220.0f;                      // [px] - floating width
    float                  Height       = 160.0f;                      // [px] - floating height
    float                  DockExtent   = 0.28f;                       // [0-1]- docked band depth as a fraction of the body span (L/R width, T/B height)
    float                  SlotFraction = 1.0f;                        // [0-1]- this panel's share of its band's long axis (split among the side's panels)
};

// 📝 One rectangle in the layout: a rect plus a screen-relative bottom edge, shared by leaves, gutters, and dock-preview overlays.
struct WorkspaceRect
{
    float PositionX = 0.0f;   // [px] - top-left x
    float PositionY = 0.0f;   // [px] - top-left y
    float Width     = 0.0f;   // [px]
    float Height    = 0.0f;   // [px]
};

// 📝 One region in a tab body's PANEL split tree — the panel-side twin of WorkspaceRegion (the window/tab tree). A LEAF holds an ordered stack of
//    panel-box ids (drawn as one panel body; a >1 stack tab-bars them, mirroring how a WorkspaceRegion leaf stacks documents). A PARTITION owns two
//    link indices (FirstLink / SecondLink — non-kinship names for the two split halves; the window tree's equivalent fields are FirstChild /
//    SecondChild, which predate the kinship-word ban and are left as-is) split along Axis at Ratio, with a draggable gutter between them. Indices
//    point into WorkspaceDocument.PanelRegions (an index pool, so a partition never dangles). Leaf == true selects the leaf fields; else partition.
struct WorkspacePanelRegion
{
    bool                   Leaf     = true;                             // [-]  - true = leaf (panel-box ids), false = partition (two links)
    bool                   Occupied = true;                            // [-]  - false = a freed pool slot (skipped)

    // -- leaf --
    std::vector<uint32_t>  Boxes;                                       // [-]  - ordered panel-box ids stacked in this leaf (back = active / topmost)
    uint32_t               ActiveBox = 0;                              // [-]  - the leaf's foreground panel box

    // -- partition --
    WorkspacePartitionAxis Axis       = WorkspacePartitionAxis::Row;    // [-]  - how the gutter divides the two halves
    float                  Ratio      = 0.5f;                           // [0-1]- FirstLink's share of the span
    int                    FirstLink  = -1;                             // [-]  - PanelRegions index of split half A (-1 = none; mirrors FirstChild)
    int                    SecondLink = -1;                             // [-]  - PanelRegions index of split half B (-1 = none; mirrors SecondChild)

    // -- cached layout (recomputed each frame) --
    WorkspaceRect          Rect;                                        // [px] - this region's area within the tab body
    WorkspaceRect          Gutter;                                      // [px] - partition gutter (unused for a leaf)
    bool                   RectValid = false;                           // [-]  - Rect / Gutter are current this frame
};

// 📝 One workspace document — the thing a trapezoid represents, wherever it lives (a docked leaf or a floating window). The Title is the editable
//    display name (double-click a trapezoid to rename inline). A document is referenced everywhere by Identifier. A tab HOSTS a set of panel boxes
//    (PanelBoxes) — free-floating rectangles the (V) dropdown drops into its body; NextPanel numbers them "Panel 1", "Panel 2", …
//    A box stays FLOATING (a free overlay) until dropped onto another panel's five-zone cross, which moves it into PanelRegions — the per-body panel
//    split tree (root = PanelRoot; -1 = no docked panels). The tree fills the body beneath the floating overlay boxes.
struct WorkspaceDocument
{
    uint32_t                          Identifier = 0;                       // [-] - unique registry key
    char                              Title[64]  = {};                      // [-] - display title (inline-renamable), e.g. "Tab 1"
    WorkspaceCategory                 Category   = WorkspaceCategory::Empty;// [-] - the tab's broad workspace category
    std::vector<WorkspacePanelBox>    PanelBoxes;                           // [-] - panel-box registry: floating overlays + the boxes the tree holds
    std::vector<WorkspacePanelRegion> PanelRegions;                         // [-] - this body's panel split-tree pool (index pool; freed slots reused)
    int                               PanelRoot  = -1;                      // [-] - PanelRegions index of the tree top (-1 = no docked panels)
    uint32_t                          NextPanel  = 1;                       // [-] - monotonic panel-box id / "Panel N" number source

    // 📝 This tab's own parametric-sketch tree, edited in place by its SketchOutliner box (if any). Independent per tab — each tab holds a distinct
    //    part. Lazily seeded the first time the tab spawns its outliner (OutlinerReady gates the one-time InitializeSketchOutlinerSample). At most one
    //    box in the tab draws it (the (V) row is greyed once present), so this state has a single writer.
    SketchOutlinerUi::SketchOutlinerState OutlinerState;                    // [-] - this tab's sketch-outliner tree (one per tab)
    bool                              OutlinerReady = false;                // [-] - true once the sample tree has been seeded for this tab
};

// 📝 One region in the dock tree. A LEAF holds an ordered list of document ids drawn as a trapezoid tab bar over a body. A PARTITION owns
//    two child region indices split along an axis at Ratio, with a draggable gutter between them. Indices point into WorkspacePanelDock.Regions
//    (an index pool, so a partition never dangles). Leaf == true selects the leaf fields; else the partition fields. RectValid gates the cached
//    layout rect for input (it is recomputed every frame before paint).
struct WorkspaceRegion
{
    bool                   Leaf      = true;                              // [-]  - true = leaf (documents), false = partition (two children)
    bool                   Occupied  = true;                             // [-]  - false = a freed pool slot (skipped)

    // -- leaf --
    std::vector<uint32_t>  Documents;                                    // [-]  - ordered document ids stacked as trapezoid tabs
    uint32_t               ActiveIdentifier = 0;                         // [-]  - the leaf's foreground document

    // -- partition --
    WorkspacePartitionAxis Axis        = WorkspacePartitionAxis::Row;    // [-]  - how the gutter divides the two children
    float                  Ratio       = 0.5f;                           // [0-1]- first child's share of the span
    int                    FirstChild  = -1;                             // [-]  - Regions index of child A (-1 = none)
    int                    SecondChild = -1;                             // [-]  - Regions index of child B (-1 = none)

    // -- cached layout (recomputed each frame) --
    WorkspaceRect          Rect;                                         // [px] - this region's area
    WorkspaceRect          Gutter;                                       // [px] - partition gutter (unused for a leaf)
    bool                   RectValid = false;                            // [-]  - Rect / Gutter are current this frame
};

// 📝 A rectangular floating window: an ordered document stack with a trapezoid tab-bar header, movable + resizable, dockable on release.
struct WorkspaceFloatingWindow
{
    uint32_t              Identifier = 0;                     // [-]  - unique window key
    std::vector<uint32_t> Documents;                          // [-]  - ordered document ids (trapezoid tabs on the header)
    uint32_t              ActiveIdentifier = 0;               // [-]  - the window's foreground document
    float                 PositionX = 0.0f;                   // [px] - top-left x
    float                 PositionY = 0.0f;                   // [px] - top-left y
    float                 Width     = 380.0f;                 // [px]
    float                 Height    = 280.0f;                 // [px]
};

// 📝 All caller-owned interior-dock state the panel edits in place. There is NO global top strip (Chrome model): every dock leaf and every
//    floating window carries its OWN trapezoid tab bar + (+) button in its header. The dock tree is an INDEX POOL (Regions) with RootRegion as
//    the tree top (-1 = empty desk). Floating windows live in their own list. The registry (Documents) owns every document once; every container
//    references documents by id. The WorkspaceDockHost embeds one instance of this inside its WorkspaceDockState.
struct WorkspacePanelDock
{
    std::vector<WorkspaceDocument>       Documents;             // [-] - registry: every document, owned once

    // 📝 The document types the (+) dropdown offers (app-supplied via ConfigureWorkspaceCatalogue). Empty until configured — the dock then falls
    //    back to a single generic "New Tab" so an unconfigured host (the WorkspaceDock validation) still works. DefaultCatalogueIndex seeds boot.
    std::vector<WorkspaceDocumentType>   DocumentCatalogue;     // [-] - (+) menu entries; empty = generic "New Tab" fallback
    int                                  DefaultCatalogueIndex = 0; // [-] - which catalogue type seeds the first tab on boot

    std::vector<WorkspaceRegion>         Regions;               // [-] - dock-tree region pool (index-addressed)
    int                                  RootRegion = -1;       // [-] - tree top (-1 = empty central area)

    std::vector<WorkspaceFloatingWindow> Floating;              // [-] - floating windows, back = topmost

    uint32_t                             NextDocument = 1;      // [-] - monotonic document id source
    uint32_t                             NextWindow   = 1;      // [-] - monotonic window id source

    // 📝 The single drag in flight. Which fields matter depends on DragMode.
    WorkspaceDragMode                    DragMode = WorkspaceDragMode::None;   // [-]  - active drag intent
    WorkspaceDragOrigin                  DragOrigin = WorkspaceDragOrigin::Panel;  // [-] - Window-mode only: what was torn (Tab vs Panel drop model)
    uint32_t                             DragDocument = 0;      // [-]  - reorder: the held top-strip document
    uint32_t                             DragWindow   = 0;      // [-]  - window / resize: the held floating window
    int                                  DragRegion   = -1;     // [-]  - partition: the region whose gutter is held
    float                                DragGrabX = 0.0f;      // [px] - cursor offset inside the grabbed thing
    float                                DragGrabY = 0.0f;      // [px]

    // 📝 A pending tab press: a docked-leaf tab was CLICKED (already activated) but not yet dragged. It only tears out into a floating window once
    //    the cursor moves past a small threshold while still held — a plain click therefore just activates the tab (no tear, no resize, no reorder).
    uint32_t                             PendingTabDocument = 0;   // [-]  - the pressed-but-not-yet-dragged document (0 = none)
    float                                PendingTabGrabLeft = 0.0f;// [px] - the trapezoid's left at press (keeps the cursor offset on tear)
    float                                PendingTabPressX   = 0.0f;// [px] - cursor x at press (tear once |dx|+|dy| exceeds the threshold)
    float                                PendingTabPressY   = 0.0f;// [px]

    // 📝 The live dock preview while a floating window hovers a landing zone (drawn as a translucent accent rect; applied on release).
    WorkspaceDockZone                    PreviewZone   = WorkspaceDockZone::None;   // [-]  - resolved landing this frame
    int                                  PreviewRegion = -1;    // [-]  - leaf the preview targets (-1 = outer / strip)
    uint32_t                             PreviewWindow = 0;     // [-]  - floating window the preview stacks onto (0 = none; a dock-tree target)
    WorkspaceRect                        PreviewRect;           // [px] - the highlighted area

    // 📝 Inline rename. One rename runs at a time, driven by the shared InlineTextEditor (a document is keyed by its id). TargetIdentifier 0 = none.
    InlineTextEditState                  Rename;                // [-]  - the live document-title edit (shared caret editor)

    // 📝 The (+) add-document dropdown. Drawn as a hand-rolled foreground overlay (NOT an ImGui popup) so it sits on the same top layer as the
    //    trapezoid tabs and is never occluded by them. AddMenuOpen holds the panel open until an entry is chosen or a click lands outside.
    bool                                 AddMenuOpen        = false;   // [-]  - overlay is showing
    int                                  AddMenuOpenedFrame = -1;      // [-]  - frame the overlay opened (ignores the opening click's dismissal)
    float                                AddMenuAnchorX     = 0.0f;    // [px] - overlay top-left
    float                                AddMenuAnchorY     = 0.0f;    // [px]
    int                                  AddMenuTargetRegion = -1;     // [-]  - leaf the chosen document is added into (-1 = none / desk = new root)
    uint32_t                             AddMenuTargetWindow = 0;      // [-]  - floating window the chosen document is added into (0 = none)

    // 📝 The (V) panel dropdown carried at the far LEFT of every leaf / window header — the sibling of the (+) on the right. Same hand-rolled
    //    foreground overlay; choosing "Panel N" DROPS a real free-floating panel box (a rectangle with a "Panel N" header + close (x)) into the
    //    active tab's body. It does NOT add a tab and does NOT rename the tab. PanelMenuTargetDocument is the tab the spawned box lands in.
    bool                                 PanelMenuOpen         = false;   // [-]  - overlay is showing
    int                                  PanelMenuOpenedFrame  = -1;      // [-]  - frame the overlay opened (ignores the opening click's dismissal)
    float                                PanelMenuAnchorX      = 0.0f;    // [px] - overlay top-left
    float                                PanelMenuAnchorY      = 0.0f;    // [px]
    uint32_t                             PanelMenuTargetDocument = 0;     // [-]  - the tab document the spawned panel box lands in (0 = none)

    // 📝 A panel box held for a move / resize within its tab body. DragMode == PanelBox (move) or PanelResize (edge / corner) selects these. The box
    //    is addressed by (owning document id, box id); DragGrabX / DragGrabY (above) carry the cursor offset inside the box header so a move has no
    //    jump. PanelResizeEdge names which of the 8 handles is held. While a floating box is dragged, PanelPreviewSide is the dock band the cursor
    //    is over this frame (Floating = none) — drawn as a translucent preview and applied on release.
    uint32_t                             DragPanelDocument = 0;   // [-]  - the tab document that owns the held box (0 = none)
    uint32_t                             DragPanelBox      = 0;   // [-]  - the held panel box's id within that tab
    int                                  PanelResizeEdge   = 0;   // [-]  - bitmask of held edges: 1=Left 2=Right 4=Top 8=Bottom (corners combine two)
    WorkspacePanelDockSide               PanelPreviewSide  = WorkspacePanelDockSide::Floating;   // [-] - dock band under the dragged box this frame

    // 📝 A docked-band gutter held for resize (DragMode == PanelBandResize). BandGutterSide names the band whose OUTER gutter (against the centre) is
    //    dragged to grow / shrink the band depth. BandGutterSlot >= 0 instead drags the INNER gutter between two panels sharing the side (the panel at
    //    that list-order slot and its successor), reflowing their SlotFraction. DragPanelDocument selects the owning tab.
    WorkspacePanelDockSide               BandGutterSide = WorkspacePanelDockSide::Floating;   // [-] - band whose gutter is held (Floating = none)
    int                                  BandGutterSlot = -1;     // [-]  - inner-gutter slot index within the side (-1 = the outer band gutter)

    // 📝 The parametric-sketch icon registry for this frame, set at the top of ConstructWorkspacePanelDock from its IconRegistry argument and read by
    //    any SketchOutliner box when it draws its tree. Transient (frame-scoped, not persisted) — null falls the outliner back to procedural glyphs.
    const SvgIconRegistry*               FrameIconRegistry = nullptr;   // [-] - this frame's sketch-glyph registry (null = procedural fallback)
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Seed the root leaf with one prototype document and activate it. Its stem/category come from the configured catalogue (generic "Tab" until
//    ConfigureWorkspaceCatalogue runs). An app that wants its own (+) types calls ConfigureWorkspaceCatalogue right after this.
void InitializeWorkspacePanelDock(WorkspacePanelDock& State);

// 📝 Install the application's (+) document types and reseed the desk with one instance of DefaultTypeIndex. A standalone editor passes ONE type
//    (its own workspace); the Editor passes all six. Call once after InitializeWorkspacePanelDock. Types is caller-owned only for the duration of
//    the call (the entries are copied into the dock).
void ConfigureWorkspaceCatalogue(WorkspacePanelDock& State, const WorkspaceDocumentType* Types, int Count, int DefaultTypeIndex);

// 📝 Construct the whole interior dock for one frame: lay out + paint the dock tree, floating windows, dock preview, then resolve input
//    (activate / tear-off / dock / rename / resize / split-drag). Everything is drawn on the foreground draw list — the host does NOT wrap it in
//    a Begin. Call once per frame from the WorkspaceDockHost. IconRegistry (optional) supplies the parametric-sketch SVG glyphs any SketchOutliner
//    box in a tab body resolves; a null registry falls back to procedural strokes so the outliner still renders.
void ConstructWorkspacePanelDock(const ThemeConfiguration& Theme, WorkspacePanelDock& State, const SvgIconRegistry* IconRegistry = nullptr);

}   // namespace Frontier

#endif
