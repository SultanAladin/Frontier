//============================================================================================================================================
//                                                              EDITORSHELL.JS
//============================================================================================================================================
// 🧩 Exact ProjectZeroEditor (port 8081) shell around FrontierEngine's WebGPU viewport:
//    - Top Notch + Control Center Drawer (Dashboard + Settings Hub/Dialogs)
//    - Bottom Asset Browser Drawer (Material Orbs, Meshes, WGSL Shaders, Icons, Live Vehicle Apply)
//    - 3-Column Dockable Workspace (.workspace, slanted .document-tab drag-and-drop, + menu, .inspector-workspace)
//    - Outliner (Visible/Hidden 64px stat cards, Search + Filter chips, Collapsible Collections incl. Cameras, Footer)
//    - Viewport Header (Scene switcher + Edit / Sim / Play modes + Construct, Focus, Camera select, Split, Diagnostics, Settings)

import { ConstructEntities, InspectionModes } from "./InspectorPages.js";
import { EditorControls, ClearElement } from "./EditorControls.js";
import { ConstructGlyph as ConstructEditorGlyph } from "./EditorIcons.js";
import { ConstructGlyph as ConstructPzGlyph } from "./ProjectZeroIcons.js";
import { ConstructTopNotch, ConstructBottomAssetBrowser } from "./ProjectZeroDrawers.js";
import { LevelAtlas, QueryLevelEntry } from "./LevelAtlas.js";

const StorageKey = "IlluminationExhibit.Editor";
const GroupOrder = ["Cameras", "Lights", "Render", "Environment", "Vehicle", "Effects", "Destruction", "Level"];
const LiveInterval = 200.0;

function Construct(Tag, ClassName = "", Text = "")
{
    const Element = document.createElement(Tag);
    if (ClassName)
        Element.className = ClassName;
    if (Text)
        Element.textContent = Text;
    return Element;
}

function SetCssVar(Element, Name, Value)
{
    if (Element && Element.style && typeof Element.style.setProperty === "function")
        Element.style.setProperty(Name, Value);
}

export class EditorShell
{
    constructor(Host)
    {
        this.Host          = Host;
        this.Entities      = ConstructEntities(Host);
        this.Controls      = new EditorControls((Key, Target) => this.HandleChange(Key, Target));
        this.Query         = "";
        this.VisibilityFilter = "all"; // "all" | "visible" | "hidden"
        this.GroupFilter   = new Set();
        this.Collapsed     = {};
        this.CollapsedParents = {};
        this.Rows          = new Map();
        this.LastLive      = 0.0;
        this.ActivePopup   = null;
        this.SettingsOpen  = false;
        this.LevelMenuOpen = false;

        let Stored = null;
        try
        {
            Stored = JSON.parse(localStorage.getItem(StorageKey) || "null");
        }
        catch (Failure)
        {
            Stored = null;
        }
        const DefaultSelection = Host.Lobby ? "lobby-session" : "render";
        this.Selected = Stored && this.Entities.some((Entity) => Entity.Id === Stored.Selected)
                      ? (Host.Lobby && (Stored.Selected === "gas" || Stored.Selected === "sun") ? "lobby-session" : Stored.Selected)
                      : DefaultSelection;

        this.Host.ApplyPostProcessStyle?.();
        this.MountProjectZeroDocking();
        this.MountDrawers();
        this.ConstructOutliner();
        this.ConstructViewportBar();
        this.ConstructStatus();
        this.Select(this.Selected);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                       POPUP MENUS (ProjectZeroEditor .popup-menu)
    //--------------------------------------------------------------------------------------------------------------------

    ClosePopup()
    {
        if (this.ActivePopup)
        {
            this.ActivePopup.Backdrop.remove();
            this.ActivePopup.Menu.remove();
            this.ActivePopup = null;
        }
        this.LevelMenuOpen = false;
    }

    OpenPopup(AnchorElement, BuildItems, TrackAsLevelMenu = false)
    {
        this.ClosePopup();
        const Rect = AnchorElement ? AnchorElement.getBoundingClientRect() : { left: 120, bottom: 48 };
        const Backdrop = Construct("div", "menu-dismiss");
        const Menu = Construct("div", "popup-menu");
        Menu.style.left = `${Math.max(8, Math.min((window.innerWidth || 1280) - 250, Math.round(Rect.left || 40)))}px`;
        Menu.style.top  = `${Math.max(8, Math.round((Rect.bottom || 40) + 6))}px`;

        Backdrop.addEventListener("pointerdown", () => this.ClosePopup());
        BuildItems(Menu, () => this.ClosePopup());

        document.body.appendChild(Backdrop);
        document.body.appendChild(Menu);
        this.ActivePopup = { Backdrop, Menu };
        this.LevelMenuOpen = TrackAsLevelMenu;
    }

    QueryLevelMenuOpen()
    {
        return !!this.LevelMenuOpen;
    }

    CloseLevelMenu()
    {
        if (this.LevelMenuOpen)
            this.ClosePopup();
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                           DOCKABLE WORKSPACE (.workspace / .dock / .tab-strip / .document-tab)
    //--------------------------------------------------------------------------------------------------------------------

    MountProjectZeroDocking()
    {
        this.WorkspaceEl = document.getElementById("pz-workspace");
        this.Panels = {
            Outliner:  { Id: "Outliner",  Title: "Outliner",   Node: document.getElementById("outliner") },
            Viewport:  { Id: "Viewport",  Title: "Viewport",   Node: document.getElementById("viewport-column") },
            Inspector: { Id: "Inspector", Title: "Inspector",  Node: document.getElementById("inspector") },
            LogStream: { Id: "LogStream", Title: "Log Stream", Node: document.getElementById("log-stream-enclosure") }
        };

        this.DockState = {
            LeftWidth: 316,
            RightWidth: 356,
            WideInspector: false,
            Docks: {
                left:   ["Outliner"],
                centre: ["Viewport"],
                right:  ["Inspector"]
            },
            Active: {
                left:   "Outliner",
                centre: "Viewport",
                right:  "Inspector"
            }
        };

        this.DockDom = {
            left:   { Dock: document.getElementById("dock-left"),   Tabs: document.getElementById("tabs-left"),   Host: document.getElementById("host-left") },
            centre: { Dock: document.getElementById("dock-centre"), Tabs: document.getElementById("tabs-centre"), Host: document.getElementById("host-centre") },
            right:  { Dock: document.getElementById("dock-right"),  Tabs: document.getElementById("tabs-right"),  Host: document.getElementById("host-right") }
        };

        if (!this.WorkspaceEl || !this.DockDom.left.Tabs || !this.DockDom.centre.Tabs || !this.DockDom.right.Tabs)
            return;

        this.ReturnViewportBtn = Construct("button", "return-viewport hidden", "Return to Viewport");
        this.ReturnViewportBtn.type = "button";
        this.ReturnViewportBtn.addEventListener("click", () =>
        {
            this.DockState.WideInspector = false;
            this.SyncDockDom();
            this.Host.ReconstructScreenTargets?.();
        });
        document.body.appendChild(this.ReturnViewportBtn);

        for (const Slot of ["left", "centre", "right"])
        {
            const Strip = this.DockDom[Slot].Tabs;
            Strip.addEventListener("dragover", (Event) => Event.preventDefault());
            Strip.addEventListener("drop", (Event) =>
            {
                Event.preventDefault();
                const TabId = Event.dataTransfer?.getData("application/frontier-tab") || this.DraggedTab;
                this.DraggedTab = null;
                if (TabId && this.Panels[TabId])
                    this.MoveTabToDock(TabId, Slot);
            });
        }

        this.BindDividerDrag(document.getElementById("divider-left"), "left");
        this.BindDividerDrag(document.getElementById("divider-right"), "right");

        window.addEventListener("keydown", (Event) =>
        {
            if (Event.target instanceof HTMLInputElement || Event.target instanceof HTMLSelectElement)
                return;
            if (Event.code === "Backquote" && !Event.repeat)
            {
                Event.preventDefault();
                this.ToggleLogStreamTab();
            }
            if (Event.code === "F3" && !Event.repeat)
            {
                Event.preventDefault();
                this.Host.Dock?.Toggle("Hud");
                this.SyncViewportToolbar();
            }
            if (Event.shiftKey && Event.code === "KeyA" && !Event.repeat)
            {
                Event.preventDefault();
                if (this.ConstructBtn)
                    this.OpenConstructMenu(this.ConstructBtn);
            }
        });

        this.SyncDockDom();
        if (typeof ResizeObserver === "function")
        {
            const StageEl = document.getElementById("stage");
            if (StageEl)
            {
                this.StageResizeObserver = new ResizeObserver(() => this.Host.ReconstructScreenTargets?.());
                this.StageResizeObserver.observe(StageEl);
            }
        }
    }

    BindDividerDrag(Divider, Side)
    {
        if (!Divider)
            return;
        Divider.addEventListener("pointerdown", (StartEvent) =>
        {
            StartEvent.preventDefault();
            const StartX = StartEvent.clientX || 0;
            const StartW = Side === "left" ? this.DockState.LeftWidth : this.DockState.RightWidth;
            document.body.classList.add("resizing");
            const OnMove = (MoveEvent) =>
            {
                const Delta = (MoveEvent.clientX || 0) - StartX;
                if (Side === "left")
                    this.DockState.LeftWidth = Math.max(185, Math.min(540, StartW + Delta));
                else
                    this.DockState.RightWidth = Math.max(205, Math.min(640, StartW - Delta));
                SetCssVar(this.WorkspaceEl, "--left", `${this.DockState.LeftWidth}px`);
                SetCssVar(this.WorkspaceEl, "--right", `${this.DockState.RightWidth}px`);
            };
            const OnUp = () =>
            {
                document.body.classList.remove("resizing");
                window.removeEventListener("pointermove", OnMove);
                window.removeEventListener("pointerup", OnUp);
                this.Host.ReconstructScreenTargets?.();
            };
            window.addEventListener("pointermove", OnMove);
            window.addEventListener("pointerup", OnUp);
        });
    }

    MoveTabToDock(TabId, TargetSlot)
    {
        for (const Slot of ["left", "centre", "right"])
        {
            this.DockState.Docks[Slot] = this.DockState.Docks[Slot].filter((Id) => Id !== TabId);
            if (this.DockState.Active[Slot] === TabId)
                this.DockState.Active[Slot] = this.DockState.Docks[Slot][0] || "";
        }
        if (TargetSlot)
        {
            this.DockState.Docks[TargetSlot].push(TabId);
            this.DockState.Active[TargetSlot] = TabId;
            if (TabId === "LogStream")
            {
                this.Panels.LogStream.Node?.classList.remove("hidden");
                this.Host.LogStream?.ToggleOpen?.(true);
            }
        }
        this.SyncDockDom();
        this.Host.ReconstructScreenTargets?.();
    }

    ToggleLogStreamTab()
    {
        const InAnyDock = ["left", "centre", "right"].some((Slot) => this.DockState.Docks[Slot].includes("LogStream"));
        if (InAnyDock)
            this.MoveTabToDock("LogStream", null);
        else
            this.MoveTabToDock("LogStream", "right");
    }

    ResetDockLayout()
    {
        this.DockState.LeftWidth = 316;
        this.DockState.RightWidth = 356;
        this.DockState.WideInspector = false;
        this.DockState.Docks = { left: ["Outliner"], centre: ["Viewport"], right: ["Inspector"] };
        this.DockState.Active = { left: "Outliner", centre: "Viewport", right: "Inspector" };
        this.SyncDockDom();
        this.Host.ReconstructScreenTargets?.();
    }

    OpenTabAddMenu(Button)
    {
        this.OpenPopup(Button, (Menu, Close) =>
        {
            Menu.appendChild(Construct("label", "", "PANELS"));
            for (const [Id, Spec] of Object.entries(this.Panels))
            {
                const Open = ["left", "centre", "right"].some((S) => this.DockState.Docks[S].includes(Id));
                const Row = Construct("button");
                Row.type = "button";
                const Dot = Construct("i", `dot${Open ? " green" : ""}`);
                Row.append(Dot, Construct("span", "", Spec.Title), Construct("small", "", Open ? "Open" : "Add"));
                Row.addEventListener("click", () =>
                {
                    const DefaultSlot = Id === "Outliner" ? "left" : (Id === "Viewport" ? "centre" : "right");
                    if (!Open)
                        this.MoveTabToDock(Id, DefaultSlot);
                    else
                    {
                        for (const S of ["left", "centre", "right"])
                            if (this.DockState.Docks[S].includes(Id))
                                this.DockState.Active[S] = Id;
                        this.SyncDockDom();
                    }
                    Close();
                });
                Menu.appendChild(Row);
            }
            Menu.appendChild(Construct("hr"));
            Menu.appendChild(Construct("label", "", "DRAWERS & WORKSPACE"));

            const OpenCc = Construct("button", "", "");
            OpenCc.type = "button";
            OpenCc.append(ConstructPzGlyph("sliders", 14), Construct("span", "", "Control Center"), Construct("small", "", "Top Notch"));
            OpenCc.addEventListener("click", () => { Close(); this.TopNotch?.Toggle(); });
            Menu.appendChild(OpenCc);

            const OpenAssets = Construct("button", "", "");
            OpenAssets.type = "button";
            OpenAssets.append(ConstructPzGlyph("swatch", 14), Construct("span", "", "Asset Browser"), Construct("small", "", "Bottom"));
            OpenAssets.addEventListener("click", () => { Close(); this.AssetDrawer?.Toggle(); });
            Menu.appendChild(OpenAssets);

            const ResetRow = Construct("button", "", "");
            ResetRow.type = "button";
            ResetRow.append(ConstructPzGlyph("refresh", 14), Construct("span", "", "Reset Dock Layout"));
            ResetRow.addEventListener("click", () => { Close(); this.ResetDockLayout(); });
            Menu.appendChild(ResetRow);
        });
    }

    SyncDockDom()
    {
        if (!this.WorkspaceEl)
            return;
        SetCssVar(this.WorkspaceEl, "--left", `${this.DockState.LeftWidth}px`);
        SetCssVar(this.WorkspaceEl, "--right", `${this.DockState.RightWidth}px`);
        this.WorkspaceEl.classList.toggle("inspector-workspace", !!this.DockState.WideInspector);
        if (this.ReturnViewportBtn)
            this.ReturnViewportBtn.classList.toggle("hidden", !this.DockState.WideInspector);

        for (const Slot of ["left", "centre", "right"])
        {
            const { Tabs, Host } = this.DockDom[Slot];
            ClearElement(Tabs);
            const List = this.DockState.Docks[Slot];
            if (!List.includes(this.DockState.Active[Slot]))
                this.DockState.Active[Slot] = List[0] || "";
            const ActiveId = this.DockState.Active[Slot];

            for (const TabId of List)
            {
                const Spec = this.Panels[TabId];
                if (!Spec)
                    continue;
                const Tab = Construct("div", `document-tab${TabId === ActiveId ? " active" : ""}`);
                Tab.draggable = true;
                Tab.addEventListener("dragstart", (Event) =>
                {
                    this.DraggedTab = TabId;
                    Event.dataTransfer?.setData("application/frontier-tab", TabId);
                });
                Tab.addEventListener("click", () =>
                {
                    this.DockState.Active[Slot] = TabId;
                    this.SyncDockDom();
                });
                if (TabId === "Inspector")
                {
                    Tab.title = "Double-click to expand Inspector workspace";
                    Tab.addEventListener("dblclick", () =>
                    {
                        this.DockState.WideInspector = !this.DockState.WideInspector;
                        this.SyncDockDom();
                        this.Host.ReconstructScreenTargets?.();
                    });
                }
                Tab.appendChild(Construct("span", "", Spec.Title));
                if (TabId !== "Viewport")
                {
                    const CloseBtn = Construct("button");
                    CloseBtn.type = "button";
                    CloseBtn.title = `Close ${Spec.Title}`;
                    CloseBtn.appendChild(ConstructPzGlyph("close", 10));
                    CloseBtn.addEventListener("click", (Event) =>
                    {
                        Event.stopPropagation();
                        this.MoveTabToDock(TabId, null);
                    });
                    Tab.appendChild(CloseBtn);
                }
                Tabs.appendChild(Tab);
            }

            if (Slot === "left")
            {
                const AddBtn = Construct("button", "tab-add");
                AddBtn.type = "button";
                AddBtn.title = "Open panel or workspace menu";
                AddBtn.appendChild(ConstructPzGlyph("plus", 12));
                AddBtn.addEventListener("click", (Event) =>
                {
                    Event.stopPropagation();
                    this.OpenTabAddMenu(AddBtn);
                });
                Tabs.appendChild(AddBtn);
            }

            // Show active panel inside Host; keep Viewport node alive even when hidden so WebGPU canvas persists cleanly
            for (const [Id, Spec] of Object.entries(this.Panels))
            {
                if (!Spec.Node)
                    continue;
                if (List.includes(Id))
                {
                    if (Spec.Node.parentNode !== Host)
                        Host.appendChild(Spec.Node);
                    Spec.Node.style.display = (Id === ActiveId) ? "flex" : "none";
                    if (Id === "LogStream")
                        Spec.Node.classList.toggle("hidden", Id !== ActiveId);
                }
                else if (Id !== "Viewport" && Spec.Node.parentNode === Host)
                {
                    Spec.Node.style.display = "none";
                }
            }
            if (List.length === 0)
            {
                let Empty = Host.querySelector?.(".empty-dock");
                if (!Empty || !Empty.className)
                {
                    Empty = Construct("div", "empty-dock", "Drop a tab here or press + to open a panel.");
                    Host.appendChild(Empty);
                }
            }
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                               TOP NOTCH (CONTROL CENTER) & BOTTOM ASSET BROWSER
    //--------------------------------------------------------------------------------------------------------------------

    MountDrawers()
    {
        this.TopNotch = ConstructTopNotch(document.getElementById("top-notch-drawer"), this.Host, {
            IsDiagnosticsVisible: () => !!(this.Host.Dock ? this.Host.Dock.QueryVisible("Hud") : this.Host.Settings.HudVisible),
            OnToggleDiagnostics: () =>
            {
                this.Host.Dock?.Toggle("Hud");
                this.SyncViewportToolbar();
            },
            OnSettingsChanged: () =>
            {
                this.Controls.Synchronise();
                this.RefreshRows();
                this.SyncViewportToolbar();
            },
            OnOpenAssetBrowser: (Category) => this.AssetDrawer?.OpenCategory(Category)
        });

        this.AssetDrawer = ConstructBottomAssetBrowser(document.getElementById("bottom-asset-drawer"), this.Host, {
            OnAssetApplied: (Asset) =>
            {
                this.Controls.Synchronise();
                this.RefreshRows();
                this.Announce(`Applied ${Asset.Name} to ControlVehicle`);
            }
        });
    }

    HandleChange(Key, Target)
    {
        this.Host.ApplySetting(Key, Target);
        this.RefreshRows();
        this.TopNotch?.Refresh?.();
        this.SyncViewportToolbar();
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                       OUTLINER (ProjectZeroEditor 1:1)
    //--------------------------------------------------------------------------------------------------------------------

    ConstructOutliner()
    {
        const Body = document.getElementById("outliner-body");
        if (!Body)
            return;
        ClearElement(Body);

        const ActiveLevel = QueryLevelEntry(this.Host.LevelKey);

        // 1. Outliner Heading
        const Heading = Construct("div", "outliner-heading");
        this.OutlinerCountLabel = Construct("small", "", `${ActiveLevel.Title} · ${this.Entities.length} objects`);
        Heading.append(
            Construct("h1", "", "Outliner"),
            this.OutlinerCountLabel
        );
        const AddQuick = Construct("button");
        AddQuick.type = "button";
        AddQuick.title = "Quick actions & scene construct";
        AddQuick.appendChild(ConstructPzGlyph("plus", 14));
        AddQuick.addEventListener("click", () => this.OpenConstructMenu(AddQuick));
        Heading.appendChild(AddQuick);
        Body.appendChild(Heading);

        // 2. Visible / Hidden 64px Stat Cards
        const Stats = Construct("div", "stats");
        const MakeStatCard = (LabelText, DiscClass, DiscText, FilterValue) =>
        {
            const Card = Construct("button");
            Card.type = "button";
            const Disc = Construct("span", DiscClass, DiscText);
            const Small = Construct("small", "", LabelText);
            const Strong = Construct("strong", "", "0");
            Card.append(Disc, Small, Strong);
            Card.addEventListener("click", () =>
            {
                this.VisibilityFilter = (this.VisibilityFilter === FilterValue) ? "all" : FilterValue;
                this.RenderTree();
            });
            Stats.appendChild(Card);
            return Strong;
        };
        this.VisibleCount = MakeStatCard("Visible", "status-disc", "✓", "visible");
        this.HiddenCount  = MakeStatCard("Hidden", "muted-disc", "◌", "hidden");
        Body.appendChild(Stats);

        // 3. Search + Filter Row
        const SearchRow = Construct("div", "outliner-search");
        const SearchLabel = Construct("label");
        SearchLabel.appendChild(ConstructPzGlyph("search", 13));
        const SearchInput = document.createElement("input");
        SearchInput.type = "search";
        SearchInput.placeholder = "Search scene...";
        SearchInput.addEventListener("input", () =>
        {
            this.Query = String(SearchInput.value || "").toLowerCase();
            this.RenderTree();
        });
        SearchLabel.appendChild(SearchInput);

        const FilterBtn = Construct("button");
        FilterBtn.type = "button";
        FilterBtn.append(ConstructPzGlyph("filter", 12), Construct("span", "", "Filter"));
        FilterBtn.addEventListener("click", () =>
        {
            this.OpenPopup(FilterBtn, (Menu, Close) =>
            {
                Menu.appendChild(Construct("label", "", "COLLECTIONS"));
                for (const Group of GroupOrder)
                {
                    if (!this.Entities.some((E) => E.Group === Group))
                        continue;
                    const Active = this.GroupFilter.has(Group);
                    const Row = Construct("button");
                    Row.type = "button";
                    Row.append(
                        Construct("i", `dot${Active ? " green" : ""}`),
                        Construct("span", "", Group),
                        Construct("small", "", Active ? "Filtered" : "")
                    );
                    Row.addEventListener("click", () =>
                    {
                        if (Active) this.GroupFilter.delete(Group);
                        else this.GroupFilter.add(Group);
                        Close();
                        this.RenderFilterChips();
                        this.RenderTree();
                    });
                    Menu.appendChild(Row);
                }
                if (this.GroupFilter.size > 0 || this.VisibilityFilter !== "all")
                {
                    Menu.appendChild(Construct("hr"));
                    const ClearBtn = Construct("button", "", "Clear all filters");
                    ClearBtn.type = "button";
                    ClearBtn.addEventListener("click", () =>
                    {
                        this.GroupFilter.clear();
                        this.VisibilityFilter = "all";
                        Close();
                        this.RenderFilterChips();
                        this.RenderTree();
                    });
                    Menu.appendChild(ClearBtn);
                }
            });
        });
        SearchRow.append(SearchLabel, FilterBtn);
        Body.appendChild(SearchRow);

        this.FilterChips = Construct("div", "filter-chips hidden");
        Body.appendChild(this.FilterChips);

        // 4. Outliner Rows
        this.Tree = Construct("div", "outliner-rows");
        Body.appendChild(this.Tree);

        // 5. Outliner Footer (ProjectZeroEditor telemetry strip; sunless STUDIO readout in Lobby!)
        const Footer = Construct("footer", "outliner-footer");
        this.OutlinerFooterItems = {};
        const AddFootItem = (Key, LabelText, Initial) =>
        {
            const Wrap = Construct("span");
            const Small = Construct("small", "", LabelText);
            const Bold = Construct("b", "", Initial);
            Wrap.append(Small, Bold);
            Footer.appendChild(Wrap);
            this.OutlinerFooterItems[Key] = Bold;
        };
        AddFootItem("State", "SCENE", "LIVE");
        AddFootItem("Quality", "QUALITY", this.Host.GradeName || "High");
        if (this.Host.Lobby)
            AddFootItem("Lighting", "STUDIO", "Cyclorama");
        else
            AddFootItem("Lighting", "SUN", `${this.Host.Settings.SunElevation.toFixed(0)}°`);
        AddFootItem("Speed", "VEHICLE", "0 km/h");
        Body.appendChild(Footer);

        this.RenderTree();
    }

    RenderFilterChips()
    {
        if (!this.FilterChips)
            return;
        ClearElement(this.FilterChips);
        const Chips = [...this.GroupFilter];
        this.FilterChips.classList.toggle("hidden", Chips.length === 0 && this.VisibilityFilter === "all");
        if (this.VisibilityFilter !== "all")
        {
            const Chip = Construct("button", "", `${this.VisibilityFilter} ×`);
            Chip.type = "button";
            Chip.addEventListener("click", () => { this.VisibilityFilter = "all"; this.RenderFilterChips(); this.RenderTree(); });
            this.FilterChips.appendChild(Chip);
        }
        for (const Group of Chips)
        {
            const Chip = Construct("button", "", `${Group} ×`);
            Chip.type = "button";
            Chip.addEventListener("click", () => { this.GroupFilter.delete(Group); this.RenderFilterChips(); this.RenderTree(); });
            this.FilterChips.appendChild(Chip);
        }
    }

    RenderTree()
    {
        if (!this.Tree)
            return;
        ClearElement(this.Tree);
        this.Rows.clear();

        const MatchesSearch = (Entity) =>
            !this.Query ||
            Entity.Name.toLowerCase().includes(this.Query) ||
            Entity.Type.toLowerCase().includes(this.Query) ||
            Entity.Group.toLowerCase().includes(this.Query);

        const MatchesVis = (Entity) =>
        {
            if (this.VisibilityFilter === "all") return true;
            const On = Entity.Toggle ? !!Entity.Toggle.Read() : true;
            return this.VisibilityFilter === "visible" ? On : !On;
        };

        for (const Group of GroupOrder)
        {
            if (this.GroupFilter.size > 0 && !this.GroupFilter.has(Group))
                continue;
            const Members = this.Entities.filter((Entity) => Entity.Group === Group);
            const Filtered = Members.filter((Entity) =>
                (MatchesSearch(Entity) && MatchesVis(Entity)) ||
                Members.some((Child) => Child.Parent === Entity.Id && MatchesSearch(Child) && MatchesVis(Child))
            );
            if (Filtered.length === 0)
                continue;

            const Open = !this.Collapsed[Group] || !!this.Query;
            const FolderId = `folder:${Group}`;

            // Folder Collection Row (ProjectZeroEditor .outliner-row folder — chevron collapses, row opens FolderInspector)
            const FolderRow = Construct("div", `outliner-row folder-row${this.Selected === FolderId ? " selected" : ""}`);
            SetCssVar(FolderRow, "--depth", "0");
            const Disclosure = Construct("button", "row-disclosure");
            Disclosure.type = "button";
            Disclosure.appendChild(ConstructPzGlyph(Open ? "chevron-down" : "chevron-right", 10));
            Disclosure.addEventListener("click", (Event) =>
            {
                Event.stopPropagation();
                this.Collapsed[Group] = !this.Collapsed[Group];
                this.RenderTree();
            });
            const FolderIcon = ConstructPzGlyph(Open ? "folder-open" : "folder", 16);
            const Identity = Construct("div", "row-identity");
            Identity.append(
                Construct("span", "row-name", Group),
                Construct("small", "", `${Members.length} ${Members.length === 1 ? "object" : "objects"}`)
            );

            // Collection-level visibility eye toggle for toggleable members (e.g. Studio Lights!)
            const Toggleables = Members.filter((M) => M.Toggle && !M.Permanent);
            FolderRow.append(Disclosure, FolderIcon, Identity);
            if (Toggleables.length > 0)
            {
                const AnyVisible = Toggleables.some((M) => !!M.Toggle.Read());
                const FolderEye = Construct("button", "visibility");
                FolderEye.type = "button";
                FolderEye.title = `Toggle all ${Group}`;
                FolderEye.appendChild(ConstructPzGlyph(AnyVisible ? "eye" : "eye-off", 13));
                FolderEye.addEventListener("click", (Event) =>
                {
                    Event.stopPropagation();
                    const TargetOn = !AnyVisible;
                    for (const M of Toggleables) M.Toggle.Write(TargetOn);
                    this.Controls.Synchronise();
                    this.RenderTree();
                });
                FolderRow.appendChild(FolderEye);
            }

            FolderRow.addEventListener("click", () =>
            {
                if (this.Collapsed[Group])
                    this.Collapsed[Group] = false;
                this.SelectFolder(Group);
            });
            this.Tree.appendChild(FolderRow);

            if (Open)
            {
                for (const Entity of Filtered)
                {
                    if (Entity.Parent && this.CollapsedParents[Entity.Parent] && !this.Query)
                        continue;
                    this.Tree.appendChild(this.ConstructRow(Entity));
                }
            }
        }
        this.RefreshRows();
    }

    ConstructRow(Entity)
    {
        const Depth = Entity.Parent ? 2 : 1;
        const HasChildren = this.Entities.some((Child) => Child.Parent === Entity.Id);
        const ParentOpen = !this.CollapsedParents[Entity.Id];
        const Row = Construct("div", `outliner-row${Entity.Id === this.Selected ? " selected" : ""}`);
        SetCssVar(Row, "--depth", String(Depth));
        Row.style.paddingLeft = `${4 + Depth * 14}px`;

        let Disclosure;
        if (HasChildren)
        {
            Disclosure = Construct("button", "row-disclosure");
            Disclosure.type = "button";
            Disclosure.appendChild(ConstructPzGlyph(ParentOpen ? "chevron-down" : "chevron-right", 10));
            Disclosure.addEventListener("click", (Event) =>
            {
                Event.stopPropagation();
                this.CollapsedParents[Entity.Id] = !this.CollapsedParents[Entity.Id];
                this.RenderTree();
            });
        }
        else
        {
            Disclosure = Construct("span", "row-disclosure empty");
        }
        const Icon = ConstructEditorGlyph(Entity.Glyph, 16, 1.6);

        const Identity = Construct("div", "row-identity");
        const NameSpan = Construct("span", "row-name", Entity.Name);
        const MetaSmall = Construct("small", "", "");
        Identity.append(NameSpan, MetaSmall);

        const StateBadge = Construct("span", "row-status");
        StateBadge.appendChild(ConstructPzGlyph("check", 9));

        Row.append(Disclosure, Icon, Identity, StateBadge);
        Row.addEventListener("click", () => this.Select(Entity.Id));

        let Eye = null;
        if (Entity.Toggle && !Entity.Permanent)
        {
            Eye = Construct("button", "visibility");
            Eye.type = "button";
            Eye.setAttribute("aria-label", `Toggle ${Entity.Name}`);
            Eye.addEventListener("click", (Event) =>
            {
                Event.stopPropagation();
                Entity.Toggle.Write(!Entity.Toggle.Read());
                this.Host.ApplySetting(`Toggle:${Entity.Id}`, null);
                this.Controls.Synchronise();
                this.RefreshRows();
            });
            Row.appendChild(Eye);
        }
        else if (Entity.Permanent)
        {
            Eye = Construct("button", "visibility");
            Eye.type = "button";
            Eye.disabled = true;
            Eye.title = "Permanent editor camera (always active)";
            Eye.appendChild(ConstructPzGlyph("eye", 13));
            Row.appendChild(Eye);
        }

        this.Rows.set(Entity.Id, { Row, Meta: MetaSmall, State: StateBadge, Eye, Entity, EyeOn: null });
        return Row;
    }

    RefreshRows()
    {
        let Enabled = 0, Disabled = 0;
        for (const Entity of this.Entities)
        {
            const On = Entity.Toggle ? !!Entity.Toggle.Read() : true;
            if (On) Enabled++; else Disabled++;
        }
        if (this.VisibleCount) this.VisibleCount.textContent = String(Enabled);
        if (this.HiddenCount)  this.HiddenCount.textContent  = String(Disabled);

        for (const Record of this.Rows.values())
        {
            const On = Record.Entity.Toggle ? !!Record.Entity.Toggle.Read() : true;
            Record.Meta.textContent = Record.Entity.Meta ? Record.Entity.Meta() : Record.Entity.Type;
            const StateName = On ? (Record.Entity.State ? Record.Entity.State() : "live") : "idle";
            Record.State.className = `row-status ${StateName}`;
            Record.Row.classList.toggle("hidden-row", !On);
            Record.Row.classList.toggle("selected", Record.Entity.Id === this.Selected);
            if (Record.Eye && !Record.Entity.Permanent && Record.EyeOn !== On)
            {
                ClearElement(Record.Eye);
                Record.Eye.appendChild(ConstructPzGlyph(On ? "eye" : "eye-off", 13));
                Record.EyeOn = On;
            }
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                 INSPECTOR
    //--------------------------------------------------------------------------------------------------------------------

    get SelectedId()
    {
        return this.Selected;
    }

    SelectEntity(Id)
    {
        this.Select(Id);
    }

    RefreshInspector()
    {
        this.Controls.Synchronise();
        this.Controls.RefreshLive();
        this.RefreshRows();
    }

    RefreshSceneOutliner(SelectId = null)
    {
        this.Entities = ConstructEntities(this.Host);
        if (this.OutlinerCountLabel)
        {
            const ActiveLevel = QueryLevelEntry(this.Host.LevelKey);
            this.OutlinerCountLabel.textContent = `${ActiveLevel.Title} · ${this.Entities.length} objects`;
        }
        this.RenderTree();
        if (SelectId)
            this.Select(SelectId);
    }

    SelectFolder(Group)
    {
        this.Selected = `folder:${Group}`;
        this.ConstructFolderInspector(Group);
        this.RenderTree();
    }

    ConstructFolderInspector(Group)
    {
        const Top = document.getElementById("inspector-top");
        ClearElement(Top);
        const Crumbs = Construct("div", "crumbs", "Inspector");
        Crumbs.append(ConstructEditorGlyph("ChevronRight", 12, 1.8), Construct("span", "", Group));
        const Status = Construct("div", "save-status");
        Status.append(ConstructEditorGlyph("Check", 12, 2), Construct("span", "", "Collection"));
        Top.append(Crumbs, Status);

        const Content = document.getElementById("inspector-content");
        ClearElement(Content);
        this.Controls.Reset();

        const Members = this.Entities.filter((E) => E.Group === Group);
        const VisibleMembers = Members.filter((E) => (E.Toggle ? !!E.Toggle.Read() : true));
        const Header = Construct("div", "object-header");
        const Titles = Construct("div");
        Titles.append(Construct("div", "eyebrow", "Collection Folder"), Construct("h1", "", Group));
        Header.appendChild(Titles);
        Content.appendChild(Header);

        const Cards = Construct("div", "cards");
        Content.appendChild(Cards);

        const Summary = this.Controls.Card(Cards, `${Group} overview`, "Layers", { Index: 1 });
        this.Controls.Stats(Summary, [
            ["Objects", () => Members.length],
            ["Visible", () => Members.filter((E) => (E.Toggle ? !!E.Toggle.Read() : true)).length],
            ["Hidden",  () => Members.filter((E) => (E.Toggle ? !E.Toggle.Read() : false)).length]
        ], 3);

        if (Group === "Lights" && this.Host.Lobby)
        {
            this.Controls.Muted(Summary, "Enclosed Studio Cyclorama lighting rig: overhead key softbox, directional cove washes, and rim strips. Outdoor sun and sky illumination are excluded from Lobby · Studio.");
        }

        const Toggleables = Members.filter((M) => M.Toggle && !M.Permanent);
        if (Toggleables.length > 0 || Group === "Lights" || Group === "Level")
        {
            const Row = this.Controls.ActionRow(Summary);
            if (Toggleables.length > 0)
            {
                this.Controls.Button(Row, "Show All", "Eye", () =>
                {
                    Toggleables.forEach((M) => M.Toggle.Write(true));
                    this.RenderTree();
                    this.ConstructFolderInspector(Group);
                });
                this.Controls.Button(Row, "Hide All", "Eye", () =>
                {
                    Toggleables.forEach((M) => M.Toggle.Write(false));
                    this.RenderTree();
                    this.ConstructFolderInspector(Group);
                });
            }
            if (Group === "Lights")
            {
                this.Controls.Button(Row, "+ Add Studio Light", "Lightbulb", () => this.Host.ConstructStudioLight?.("Spot"));
            }
            if (Group === "Level")
            {
                this.Controls.Button(Row, "+ Construct Cube", "Box", () => this.Host.ConstructPrimitive?.("Cube"));
            }
        }

        const ListCard = this.Controls.Card(Cards, "Collection contents", "LayoutGrid", { Index: 2 });
        const MemberList = Construct("div", "fe-folder-member-list");
        for (const Member of Members)
        {
            const Item = Construct("button", "fe-folder-member");
            Item.type = "button";
            const Left = Construct("div", "fe-folder-member-id");
            Left.append(ConstructEditorGlyph(Member.Glyph, 14, 1.6), Construct("strong", "", Member.Name));
            const Right = Construct("span", "fe-folder-member-meta", Member.Meta ? Member.Meta() : Member.Type);
            Item.append(Left, Right);
            Item.addEventListener("click", () => this.Select(Member.Id));
            MemberList.appendChild(Item);
        }
        ListCard.appendChild(MemberList);
    }

    Select(Id)
    {
        if (typeof Id === "string" && Id.startsWith("folder:"))
        {
            this.SelectFolder(Id.slice(7));
            return;
        }
        const Entity = this.Entities.find((Candidate) => Candidate.Id === Id) || this.Entities[0];
        this.Selected = Entity.Id;
        try
        {
            localStorage.setItem(StorageKey, JSON.stringify({ Selected: this.Selected }));
        }
        catch (Failure)
        {
        }
        this.ConstructInspector(Entity);
        this.RefreshRows();
    }

    ConstructInspector(Entity)
    {
        const Top = document.getElementById("inspector-top");
        ClearElement(Top);
        const Crumbs = Construct("div", "crumbs", "Inspector");
        Crumbs.append(ConstructEditorGlyph("ChevronRight", 12, 1.8), Construct("span", "", Entity.Group));
        const Status = Construct("div", "save-status");
        Status.append(ConstructEditorGlyph("Check", 12, 2), Construct("span", "", "Changes apply live"));
        Top.append(Crumbs, Status);

        const Content = document.getElementById("inspector-content");
        ClearElement(Content);
        this.Controls.Reset();
        const Header = Construct("div", "object-header");
        const Titles = Construct("div");
        Titles.append(Construct("div", "eyebrow", Entity.Type), Construct("h1", "", Entity.Name));
        const Actions = Construct("div", "object-actions");
        if (Entity.Toggle && !Entity.Permanent)
        {
            const Pill = Construct("button", "enabled-pill");
            const Dot = Construct("span");
            const Caption = Construct("em", "", "");
            Caption.style.fontStyle = "normal";
            Pill.append(Dot, Caption);
            const Show = () =>
            {
                const On = !!Entity.Toggle.Read();
                Pill.classList.toggle("disabled", !On);
                Caption.textContent = On ? "Enabled" : "Disabled";
            };
            Pill.addEventListener("click", () =>
            {
                Entity.Toggle.Write(!Entity.Toggle.Read());
                this.Host.ApplySetting(`Toggle:${Entity.Id}`, null);
                this.Controls.Synchronise();
                this.RefreshRows();
            });
            Show();
            this.Controls.Bindings.push(Show);
            Actions.appendChild(Pill);
        }
        Header.append(Titles, Actions);
        Content.appendChild(Header);
        const Section = Construct("div", "section-label");
        Section.append(Construct("span", "", "PROPERTIES"), Construct("span", "", Entity.Description || ""));
        Content.appendChild(Section);
        if (Entity.Switches)
            this.Controls.PropertySwitches(Content, Entity.Switches());
        const Cards = Construct("div", "cards");
        Content.appendChild(Cards);
        Entity.Build(this.Controls, Cards);
        const Footer = Construct("footer", "inspector-footer");
        const Left = Construct("span");
        Left.append(Construct("span", "footer-dot"), Construct("span", "", "Changes apply in real time"));
        Footer.append(Left, Construct("span", "", `${Entity.Name} / ${Entity.Type}`));
        Content.appendChild(Footer);
        if (Content.scrollTo)
            Content.scrollTo(0, 0);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                     VIEWPORT HEADER (ProjectZeroEditor 1:1 with Edit / Sim / Play + Action Row)
    //--------------------------------------------------------------------------------------------------------------------

    OpenSceneSwitcherMenu(Anchor)
    {
        const ActiveEntry = QueryLevelEntry(this.Host.LevelKey);
        this.OpenPopup(Anchor, (Menu, Close) =>
        {
            Menu.appendChild(Construct("label", "", "SCENES"));
            for (const Entry of LevelAtlas)
            {
                const Row = Construct("button");
                Row.type = "button";
                Row.append(
                    Construct("i", `dot${Entry.Key === ActiveEntry.Key ? " green" : ""}`),
                    Construct("span", "", Entry.Title),
                    Construct("small", "", Entry.Key === "lobby" ? "Studio Bay" : "Outdoor")
                );
                Row.addEventListener("click", () =>
                {
                    Close();
                    this.Host.SwitchLevel(Entry.Key);
                });
                Menu.appendChild(Row);
            }
            Menu.appendChild(Construct("hr"));
            const SplashBtn = Construct("button");
            SplashBtn.type = "button";
            SplashBtn.append(ConstructPzGlyph("viewport", 14), Construct("span", "", "Splash & Level Select"), Construct("small", "", "Esc"));
            SplashBtn.addEventListener("click", () =>
            {
                Close();
                document.getElementById("lobby-return")?.click();
            });
            Menu.appendChild(SplashBtn);
        }, true);
    }

    OpenConstructMenu(Anchor)
    {
        this.OpenPopup(Anchor, (Menu, Close) =>
        {
            Menu.appendChild(Construct("label", "", "3D PRIMITIVES (ARRAYABLE)"));
            for (const [Kind, Label, Sub] of [
                ["Cube",    "Construct Cube",    "1.2 m Box"],
                ["Pillar",  "Construct Pillar",  "2.4 m Column"],
                ["Barrier", "Construct Barrier", "Jersey Block"]
            ])
            {
                const Row = Construct("button");
                Row.type = "button";
                Row.append(ConstructPzGlyph("construct", 14), Construct("span", "", Label), Construct("small", "", Sub));
                Row.addEventListener("click", () =>
                {
                    Close();
                    this.Host.ConstructPrimitive?.(Kind);
                });
                Menu.appendChild(Row);
            }

            if (this.Host.Lobby)
            {
                Menu.appendChild(Construct("hr"));
                Menu.appendChild(Construct("label", "", "STUDIO LIGHTS"));
                for (const [Kind, Label, Sub] of [
                    ["Spot",  "Add Studio Spot / Flood", "Directional"],
                    ["Point", "Add Studio Point Light",  "Omni"]
                ])
                {
                    const Row = Construct("button");
                    Row.type = "button";
                    Row.append(ConstructPzGlyph("sliders", 14), Construct("span", "", Label), Construct("small", "", Sub));
                    Row.addEventListener("click", () =>
                    {
                        Close();
                        this.Host.ConstructStudioLight?.(Kind);
                    });
                    Menu.appendChild(Row);
                }
            }

            Menu.appendChild(Construct("hr"));
            Menu.appendChild(Construct("label", "", "CAMERAS & POST"));
            for (const [Id, Label, Sub] of [
                ["editor-camera",  "Editor Camera",  "Navigation"],
                ["vehicle-camera", "Vehicle Camera", "Parented 3D"],
                ["cine-camera",    "Cine Camera",    "Seamless Spline"],
                ["post-process",   "Post Process",   "EV & Grading"]
            ])
            {
                const Row = Construct("button");
                Row.type = "button";
                Row.append(ConstructPzGlyph("camera", 14), Construct("span", "", Label), Construct("small", "", Sub));
                Row.addEventListener("click", () =>
                {
                    Close();
                    this.Select(Id);
                    if (Id === "cine-camera")
                        this.Host.AssignCameraMode("Cinematic");
                    else if (Id === "vehicle-camera")
                        this.Host.AssignCameraMode("VehicleCamera");
                    else if (Id === "editor-camera")
                        this.Host.Lobby ? this.Host.AssignCameraMode("Orbit") : this.Host.AssignCamera(true);
                    this.SyncViewportToolbar();
                });
                Menu.appendChild(Row);
            }
            Menu.appendChild(Construct("hr"));
            Menu.appendChild(Construct("label", "", "SCENE ACTIONS"));

            const RespawnRow = Construct("button");
            RespawnRow.type = "button";
            RespawnRow.append(ConstructPzGlyph("refresh", 14), Construct("span", "", "Respawn ControlVehicle"), Construct("small", "", "R"));
            RespawnRow.addEventListener("click", () => { Close(); this.Host.RespawnVehicle(); });
            Menu.appendChild(RespawnRow);

            const ReassembleRow = Construct("button");
            ReassembleRow.type = "button";
            ReassembleRow.append(ConstructPzGlyph("construct", 14), Construct("span", "", "Reassemble Breakable Props"));
            ReassembleRow.addEventListener("click", () => { Close(); this.Host.Fracture?.ReassembleAll?.(); });
            Menu.appendChild(ReassembleRow);

            const AssetsRow = Construct("button");
            AssetsRow.type = "button";
            AssetsRow.append(ConstructPzGlyph("swatch", 14), Construct("span", "", "Browse Materials & Assets"));
            AssetsRow.addEventListener("click", () => { Close(); this.AssetDrawer?.OpenCategory("materials"); });
            Menu.appendChild(AssetsRow);
        });
    }

    ConstructViewportBar()
    {
        const Bar = document.getElementById("viewport-bar");
        if (!Bar)
            return;
        ClearElement(Bar);
        const Host = this.Host;
        const ActiveLevel = QueryLevelEntry(Host.LevelKey);

        // Row 1: .viewport-heading-row (Scene identity + Edit / Sim / Play mode segmented bar)
        const HeadingRow = Construct("div", "viewport-heading-row");

        const Identity = Construct("div", "viewport-identity");
        Identity.appendChild(ConstructPzGlyph("viewport", 16));
        const IdentityText = Construct("div");
        const SceneBtn = Construct("button", "viewport-scene-btn");
        SceneBtn.type = "button";
        SceneBtn.title = "Switch scene (Proving Grounds / Lobby · Studio)";
        SceneBtn.textContent = `${ActiveLevel.Title} ▾`;
        SceneBtn.addEventListener("click", (Event) =>
        {
            Event.stopPropagation();
            this.OpenSceneSwitcherMenu(SceneBtn);
        });
        const Subtitle = Construct("span", "", Host.Lobby
            ? "Enclosed Studio Cyclorama · Studio Lights Only (No Sun & Sky)"
            : "WebGPU · Distance-Field BVH ReSTIR GI · Multi-Bounce");
        IdentityText.append(SceneBtn, Subtitle);
        Identity.append(IdentityText, Construct("em", "viewport-preview-badge", "WEBGPU LIVE"));
        if (Identity.lastChild) Identity.lastChild.style.fontStyle = "normal";

        const Modes = Construct("div", "viewport-modes");
        Modes.setAttribute("role", "group");
        Modes.setAttribute("aria-label", "Viewport run mode");
        this.ModeButtons = [];
        for (const [ModeKey, LabelText, GlyphName, Tip] of [
            ["EDIT", "Edit", "edit",     "Edit mode — free-flight / studio orbit editor camera"],
            ["SIM",  "Sim",  "simulate", "Simulation mode — live physics & AI while inspecting"],
            ["PLAY", "Play", "play",     "Play mode — drive ControlVehicle with chase camera"]
        ])
        {
            const Btn = Construct("button");
            Btn.type = "button";
            Btn.title = Tip;
            Btn.append(ConstructPzGlyph(GlyphName, 11), Construct("span", "", LabelText));
            Btn.addEventListener("click", () =>
            {
                Host.AssignRunMode(ModeKey);
                this.SyncViewportToolbar();
                this.Controls.Synchronise();
            });
            Modes.appendChild(Btn);
            this.ModeButtons.push([Btn, ModeKey]);
        }

        HeadingRow.append(Identity, Modes);
        Bar.appendChild(HeadingRow);

        // Row 2: .viewport-action-row (Construct, Focus, Divider, Camera Select, Split, Diagnostics, Settings)
        const ActionRow = Construct("div", "viewport-action-row");

        const ConstructBtn = Construct("button", "viewport-construct");
        ConstructBtn.type = "button";
        ConstructBtn.append(ConstructPzGlyph("construct", 13), Construct("span", "", "Construct"), Construct("kbd", "", "Shift+A"));
        ConstructBtn.addEventListener("click", (Event) =>
        {
            Event.stopPropagation();
            this.OpenConstructMenu(ConstructBtn);
        });
        this.ConstructBtn = ConstructBtn;

        const FocusBtn = Construct("button", "viewport-icon-button");
        FocusBtn.type = "button";
        FocusBtn.title = "Focus / Respawn ControlVehicle (R)";
        FocusBtn.appendChild(ConstructPzGlyph("focus", 14));
        FocusBtn.addEventListener("click", () => Host.RespawnVehicle());

        const ProjectionWrap = Construct("div", "viewport-projection");
        ProjectionWrap.appendChild(ConstructPzGlyph("camera", 13));
        const CameraSelect = document.createElement("select");
        CameraSelect.setAttribute("aria-label", "Active viewport camera");
        const CameraChoices = [
            ["Editor",        Host.Lobby ? "Editor Camera · Studio Orbit" : "Editor Camera · Free (F8)"],
            ["VehicleCamera", "Vehicle Camera · Parented 3D"],
            ...(Host.Lobby ? [["Cinematic", "Cine Camera · Lobby Spline"]] : []),
            ["Cockpit",       "Driver Cockpit · Interior"],
            ["Chase",         Host.Lobby ? "Turntable Presentation" : "Chase Camera · Vehicle"]
        ];
        for (const [Val, Text] of CameraChoices)
        {
            const Opt = document.createElement("option");
            Opt.value = Val;
            Opt.textContent = Text;
            CameraSelect.appendChild(Opt);
        }
        CameraSelect.addEventListener("change", () =>
        {
            const Choice = CameraSelect.value;
            Host.EditorFlightActive = false;
            if (Choice === "Editor")
            {
                if (Host.Lobby) Host.AssignCameraMode("Orbit");
                else Host.AssignCamera(true);
                this.Select("editor-camera");
            }
            else if (Choice === "VehicleCamera")
            {
                Host.AssignCamera(false);
                Host.AssignCameraMode("VehicleCamera");
                this.Select("vehicle-camera");
            }
            else if (Choice === "Cinematic")
            {
                Host.AssignCamera(false);
                Host.AssignCameraMode("Cinematic");
                this.Select("cine-camera");
            }
            else if (Choice === "Cockpit")
            {
                Host.AssignCamera(false);
                Host.AssignCameraMode("Cockpit");
                this.Select("cabin");
            }
            else
            {
                Host.AssignCamera(false);
                Host.AssignCameraMode(Host.Lobby ? "Orbit" : "Chase");
            }
            this.SyncViewportToolbar();
        });
        this.CameraSelect = CameraSelect;
        ProjectionWrap.appendChild(CameraSelect);

        const Secondary = Construct("div", "viewport-secondary-tools");

        this.InsetButton = Construct("button", "viewport-icon-button");
        this.InsetButton.type = "button";
        this.InsetButton.title = "Distance-field GDF slice view (J)";
        this.InsetButton.appendChild(ConstructPzGlyph("split", 14));
        this.InsetButton.addEventListener("click", () =>
        {
            Host.Dock?.Toggle("Inset");
            this.SyncViewportToolbar();
        });

        this.HudButton = Construct("button", "viewport-icon-button");
        this.HudButton.type = "button";
        this.HudButton.title = "Viewport diagnostics readout (F3 / H)";
        this.HudButton.appendChild(ConstructPzGlyph("diagnostics", 14));
        this.HudButton.addEventListener("click", () =>
        {
            Host.Dock?.Toggle("Hud");
            this.SyncViewportToolbar();
        });

        this.ViewportSettingsBtn = Construct("button", "viewport-icon-button");
        this.ViewportSettingsBtn.type = "button";
        this.ViewportSettingsBtn.title = "Viewport shading & debug settings";
        this.ViewportSettingsBtn.appendChild(ConstructPzGlyph("settings", 14));
        this.ViewportSettingsBtn.addEventListener("click", () =>
        {
            this.SettingsOpen = !this.SettingsOpen;
            this.ViewportSettingsPopover.classList.toggle("hidden", !this.SettingsOpen);
            this.SyncViewportToolbar();
        });

        Secondary.append(this.InsetButton, this.HudButton, this.ViewportSettingsBtn);
        ActionRow.append(ConstructBtn, FocusBtn, Construct("span", "viewport-tool-divider"), ProjectionWrap, Secondary);
        Bar.appendChild(ActionRow);

        // Viewport Settings Popover (Inspection channel, Post Process, Clean View, Log Stream)
        const Popover = Construct("div", "viewport-settings-popover hidden");
        const PopHeader = Construct("header");
        PopHeader.append(Construct("strong", "", "Viewport Settings"));
        const PopClose = Construct("button", "", "×");
        PopClose.type = "button";
        PopClose.addEventListener("click", () =>
        {
            this.SettingsOpen = false;
            Popover.classList.add("hidden");
            this.SyncViewportToolbar();
        });
        PopHeader.appendChild(PopClose);

        const ChannelLabel = Construct("label", "", "Inspection Channel");
        const Display = document.createElement("select");
        for (const [Caption, Value] of InspectionModes)
        {
            const Choice = document.createElement("option");
            Choice.value = String(Value);
            Choice.textContent = Caption;
            Display.appendChild(Choice);
        }
        Display.addEventListener("change", () =>
        {
            Host.Settings.Inspection = Number(Display.value);
            Host.ApplySetting("Inspection");
            this.Controls.Synchronise();
        });
        this.DisplaySelect = Display;
        ChannelLabel.appendChild(Display);

        const PostLabel = Construct("label", "inline-check");
        PostLabel.appendChild(Construct("span", "", "Post Process Grading"));
        const PostCheck = document.createElement("input");
        PostCheck.type = "checkbox";
        PostCheck.checked = Boolean(Host.Settings.PostEnabled);
        PostCheck.addEventListener("change", () =>
        {
            Host.Settings.PostEnabled = PostCheck.checked;
            Host.ApplyPostProcessStyle();
            this.Controls.Synchronise();
        });
        this.PostCheck = PostCheck;
        PostLabel.appendChild(PostCheck);

        const LogBtn = Construct("button", "", "Toggle Log Stream (`)");
        LogBtn.type = "button";
        LogBtn.style.cssText = "height:30px;border-radius:8px;border:1px solid #ffffff14;background:#222;color:#ddd;cursor:pointer;font-size:11px;";
        LogBtn.addEventListener("click", () => this.ToggleLogStreamTab());

        Popover.append(PopHeader, ChannelLabel, PostLabel, LogBtn);
        Bar.appendChild(Popover);
        this.ViewportSettingsPopover = Popover;

        this.SyncViewportToolbar();
    }

    SyncViewportToolbar()
    {
        const Host = this.Host;
        const RunMode = Host.Settings.RunMode || "PLAY";
        if (this.ModeButtons)
        {
            for (const [Btn, Key] of this.ModeButtons)
            {
                const IsActive = Key === RunMode;
                Btn.classList.toggle("active", IsActive);
                Btn.setAttribute("aria-pressed", String(IsActive));
            }
        }
        if (this.CameraSelect)
        {
            if (Host.FreeCamera || Host.EditorFlightActive)
                this.CameraSelect.value = "Editor";
            else if (Host.Settings.CameraMode === "VehicleCamera")
                this.CameraSelect.value = "VehicleCamera";
            else if (Host.Settings.CameraMode === "Cinematic")
                this.CameraSelect.value = "Cinematic";
            else if (Host.Settings.CameraMode === "Cockpit")
                this.CameraSelect.value = "Cockpit";
            else
                this.CameraSelect.value = Host.Lobby && RunMode === "EDIT" ? "Editor" : "Chase";
        }
        if (this.DisplaySelect)
            this.DisplaySelect.value = String(Host.Settings.Inspection);
        if (this.PostCheck)
            this.PostCheck.checked = Boolean(Host.Settings.PostEnabled);
        if (this.HudButton)
            this.HudButton.setAttribute("aria-pressed", String(!!Host.Dock?.QueryVisible("Hud")));
        if (this.InsetButton)
            this.InsetButton.setAttribute("aria-pressed", String(!!Host.Dock?.QueryVisible("Inset")));
        if (this.ViewportSettingsBtn)
            this.ViewportSettingsBtn.setAttribute("aria-pressed", String(!!this.SettingsOpen));
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                          VIEWPORT FOOTER / TELEMETRY
    //--------------------------------------------------------------------------------------------------------------------

    ConstructStatus()
    {
        const Status = document.getElementById("status");
        if (!Status)
            return;
        ClearElement(Status);
        this.StatusItems = {};

        const Left = Construct("span");
        const RateStrong = Construct("b", "live", "60 fps");
        const FrameStrong = Construct("b", "", "16.7 ms");
        const ModeStrong = Construct("b", "", this.Host.Settings.RunMode || "PLAY");
        Left.append(
            Construct("span", "", "REALTIME "), RateStrong,
            Construct("span", "", "  ·  FRAME "), FrameStrong,
            Construct("span", "", "  ·  MODE "), ModeStrong
        );

        const Right = Construct("span");
        const CamStrong = Construct("b", "", "0, 0, 0");
        const GpuStrong = Construct("b", "", this.Host.AdapterDescription || "WebGPU");
        Right.append(
            Construct("span", "", "CAM "), CamStrong,
            Construct("span", "", "  ·  GPU "), GpuStrong
        );

        Status.append(Left, Right);
        this.StatusItems = { Rate: RateStrong, Frame: FrameStrong, Mode: ModeStrong, Camera: CamStrong, Adapter: GpuStrong };
    }

    Present(Now, HudLines)
    {
        if (Now - this.LastLive < LiveInterval)
            return;
        this.LastLive = Now;
        const Host = this.Host;
        const HudText = document.getElementById("hud-text");
        if (HudText)
            HudText.textContent = HudLines.join("\n");

        const Telemetry = Host.Vehicle.QueryTelemetry();
        const Fps = `${(1.0 / Math.max(Host.FrameSeconds || 1, 1.0e-4)).toFixed(0)} fps`;
        const Ms  = `${((Host.FrameSeconds || 0) * 1000.0).toFixed(1)} ms`;
        const SpeedStr = `${Math.abs(Telemetry.Speed * 3.6).toFixed(0)} km/h`;

        if (this.StatusItems)
        {
            this.StatusItems.Rate.textContent    = Fps;
            this.StatusItems.Frame.textContent   = Ms;
            this.StatusItems.Mode.textContent    = Host.Settings.RunMode || "PLAY";
            const Eye = Host.CameraEye || [0, 0, 0];
            this.StatusItems.Camera.textContent  = Eye.map((C) => C.toFixed(0)).join(", ");
            this.StatusItems.Adapter.textContent = Host.AdapterDescription || "WebGPU";
        }
        if (this.OutlinerFooterItems)
        {
            this.OutlinerFooterItems.Quality.textContent = Host.GradeName || "High";
            this.OutlinerFooterItems.Lighting.textContent = Host.Lobby
                ? "Cyclorama"
                : `${Host.Settings.SunElevation.toFixed(0)}°`;
            this.OutlinerFooterItems.Speed.textContent = SpeedStr;
        }

        this.SyncViewportToolbar();
        this.Controls.RefreshLive();
        this.RefreshRows();
    }

    Announce(Text)
    {
        const Notice = document.getElementById("event-notice");
        if (!Notice)
            return;
        Notice.textContent = Text;
        Notice.classList.remove("hidden");
        clearTimeout(this.NoticeTimer);
        this.NoticeTimer = setTimeout(() => Notice.classList.add("hidden"), 2400);
    }
}
