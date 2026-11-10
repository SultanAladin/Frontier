//============================================================================================================================================
//                                                            WORKSPACEDOCK.JS
//============================================================================================================================================
// 🧩 Multi-zone dockable editor workspace (Left, Right, Bottom, Floating) with drag-and-drop tabs, splitters, and layout presets.

const StorageKey = "FrontierEngine.WorkspaceDock.v1";

const DefaultLayout = Object.freeze({
    LeftWidth: 268,
    RightWidth: 356,
    BottomHeight: 168,
    Zones: {
        left:   ["outliner"],
        right:  ["inspector"],
        bottom: ["log"]
    },
    ActiveTab: {
        left:   "outliner",
        right:  "inspector",
        bottom: "log"
    },
    Floating: {},
    Hidden: []
});

export function ConstructWorkspaceDock(WorkspaceContainer, ViewportNode, Panels)
{
    // Panels: [{ Id, Title, Node, DefaultZone }]
    const Registry = new Map();
    for (const Entry of Panels)
        Registry.set(Entry.Id, Entry);

    let Layout = LoadSavedLayout(Registry);

    WorkspaceContainer.innerHTML = "";
    WorkspaceContainer.classList.add("fe-dock-workspace");

    const UpperRow     = BuildDiv("fe-dock-upper");
    const LeftZone     = BuildZone("left");
    const LeftSplit    = BuildDiv("fe-dock-splitter-v");
    const CenterZone   = BuildDiv("fe-dock-center");
    const RightSplit   = BuildDiv("fe-dock-splitter-v");
    const RightZone    = BuildZone("right");
    const BottomSplit  = BuildDiv("fe-dock-splitter-h");
    const BottomZone   = BuildZone("bottom");
    const FloatLayer   = BuildDiv("fe-dock-floating-layer");
    const DropOverlay  = BuildDropOverlay();

    CenterZone.appendChild(ViewportNode);
    UpperRow.append(LeftZone.Root, LeftSplit, CenterZone, RightSplit, RightZone.Root);
    WorkspaceContainer.append(UpperRow, BottomSplit, BottomZone.Root, FloatLayer, DropOverlay);

    const Zones = { left: LeftZone, right: RightZone, bottom: BottomZone };
    let DraggedPanelId = null;
    let OpenMenuNode = null;

    function CloseMenu()
    {
        if (OpenMenuNode)
        {
            OpenMenuNode.remove();
            OpenMenuNode = null;
        }
    }
    window.addEventListener("pointerdown", (Event) =>
    {
        if (OpenMenuNode && !OpenMenuNode.contains(Event.target))
            CloseMenu();
    });

    function BuildZone(SlotName)
    {
        const Root   = BuildDiv(`fe-dock-zone fe-dock-${SlotName}`);
        const TabBar = BuildDiv("fe-dock-tabbar");
        const Tabs   = BuildDiv("fe-dock-tabs");
        const Actions = BuildDiv("fe-dock-zone-actions");
        const MenuBtn = document.createElement("button");
        MenuBtn.type = "button";
        MenuBtn.className = "fe-dock-iconbtn";
        MenuBtn.title = "Dock options";
        MenuBtn.textContent = "▾";
        MenuBtn.addEventListener("click", (Event) =>
        {
            Event.stopPropagation();
            const ActiveId = Layout.ActiveTab[SlotName] || Layout.Zones[SlotName][0];
            if (ActiveId)
                ShowDockMenu(ActiveId, MenuBtn.getBoundingClientRect());
        });

        const FloatBtn = document.createElement("button");
        FloatBtn.type = "button";
        FloatBtn.className = "fe-dock-iconbtn";
        FloatBtn.title = "Detach active tab as floating window";
        FloatBtn.textContent = "⧉";
        FloatBtn.addEventListener("click", (Event) =>
        {
            Event.stopPropagation();
            const ActiveId = Layout.ActiveTab[SlotName] || Layout.Zones[SlotName][0];
            if (ActiveId)
                MovePanel(ActiveId, "float");
        });

        Actions.append(FloatBtn, MenuBtn);
        TabBar.append(Tabs, Actions);
        const Body = BuildDiv("fe-dock-body");
        Root.append(TabBar, Body);
        return { SlotName, Root, Tabs, Body };
    }

    function BuildDropOverlay()
    {
        const Overlay = BuildDiv("fe-dock-drop-overlay");
        const Targets = [
            { Slot: "left",   Label: "Dock Left" },
            { Slot: "right",  Label: "Dock Right" },
            { Slot: "bottom", Label: "Dock Bottom" },
            { Slot: "float",  Label: "Float Window" }
        ];
        for (const Spec of Targets)
        {
            const Box = BuildDiv("fe-dock-drop-target");
            Box.dataset.slot = Spec.Slot;
            Box.textContent = Spec.Label;
            Box.addEventListener("dragover", (Event) =>
            {
                Event.preventDefault();
                Box.classList.add("is-hovered");
            });
            Box.addEventListener("dragleave", () => Box.classList.remove("is-hovered"));
            Box.addEventListener("drop", (Event) =>
            {
                Event.preventDefault();
                Box.classList.remove("is-hovered");
                const PanelId = DraggedPanelId || Event.dataTransfer?.getData("text/plain");
                WorkspaceContainer.classList.remove("is-dragging-tab");
                DraggedPanelId = null;
                if (PanelId && Registry.has(PanelId))
                    MovePanel(PanelId, Spec.Slot);
            });
            Overlay.appendChild(Box);
        }
        return Overlay;
    }

    function RemovePanelEverywhere(PanelId)
    {
        for (const Slot of ["left", "right", "bottom"])
        {
            Layout.Zones[Slot] = Layout.Zones[Slot].filter((Id) => Id !== PanelId);
            if (Layout.ActiveTab[Slot] === PanelId)
                Layout.ActiveTab[Slot] = Layout.Zones[Slot][0] || "";
        }
        delete Layout.Floating[PanelId];
        Layout.Hidden = Layout.Hidden.filter((Id) => Id !== PanelId);
    }

    function MovePanel(PanelId, Destination, Coordinates = null)
    {
        if (!Registry.has(PanelId))
            return;
        RemovePanelEverywhere(PanelId);
        if (Destination === "hide")
        {
            Layout.Hidden.push(PanelId);
        }
        else if (Destination === "float")
        {
            const Rect = WorkspaceContainer.getBoundingClientRect();
            Layout.Floating[PanelId] = Coordinates || {
                X: Math.max(24, Math.round((Rect.width || 960) * 0.5 - 170)),
                Y: 56,
                Width: 340,
                Height: PanelId === "log" ? 240 : 420
            };
        }
        else if (Layout.Zones[Destination])
        {
            Layout.Zones[Destination].push(PanelId);
            Layout.ActiveTab[Destination] = PanelId;
        }
        PersistLayout();
        SynchroniseDom();
    }

    function TogglePanel(PanelId)
    {
        if (Layout.Hidden.includes(PanelId))
        {
            const Spec = Registry.get(PanelId);
            MovePanel(PanelId, Spec?.DefaultZone || "right");
        }
        else
        {
            MovePanel(PanelId, "hide");
        }
    }

    function FocusPanel(PanelId)
    {
        if (Layout.Hidden.includes(PanelId))
        {
            const Spec = Registry.get(PanelId);
            MovePanel(PanelId, Spec?.DefaultZone || "bottom");
            return;
        }
        for (const Slot of ["left", "right", "bottom"])
        {
            if (Layout.Zones[Slot].includes(PanelId))
            {
                Layout.ActiveTab[Slot] = PanelId;
                PersistLayout();
                SynchroniseDom();
                return;
            }
        }
    }

    function QueryPanelVisible(PanelId)
    {
        return !Layout.Hidden.includes(PanelId);
    }

    function ResetLayout()
    {
        Layout = CloneDefaultLayout(Registry);
        PersistLayout();
        SynchroniseDom();
    }

    function ShowDockMenu(PanelId, AnchorRect)
    {
        CloseMenu();
        const Spec = Registry.get(PanelId);
        if (!Spec)
            return;
        const Menu = BuildDiv("fe-dock-menu");
        Menu.style.left = `${Math.min(window.innerWidth - 180, Math.max(8, AnchorRect.left))}px`;
        Menu.style.top  = `${Math.min(window.innerHeight - 200, AnchorRect.bottom + 4)}px`;

        const Choices = [
            { Label: "Dock Left",        Action: () => MovePanel(PanelId, "left") },
            { Label: "Dock Right",       Action: () => MovePanel(PanelId, "right") },
            { Label: "Dock Bottom",      Action: () => MovePanel(PanelId, "bottom") },
            { Label: "Floating Window",  Action: () => MovePanel(PanelId, "float") },
            { Label: "Hide Panel",       Action: () => MovePanel(PanelId, "hide") },
            { Label: "Reset All Docks",  Action: () => ResetLayout() }
        ];
        for (const Choice of Choices)
        {
            const Btn = document.createElement("button");
            Btn.type = "button";
            Btn.textContent = Choice.Label;
            Btn.addEventListener("click", () =>
            {
                CloseMenu();
                Choice.Action();
            });
            Menu.appendChild(Btn);
        }
        document.body.appendChild(Menu);
        OpenMenuNode = Menu;
    }

    function SynchroniseDom()
    {
        if (WorkspaceContainer.style && typeof WorkspaceContainer.style.setProperty === "function")
        {
            WorkspaceContainer.style.setProperty("--fe-left-width", `${Layout.LeftWidth}px`);
            WorkspaceContainer.style.setProperty("--fe-right-width", `${Layout.RightWidth}px`);
            WorkspaceContainer.style.setProperty("--fe-bottom-height", `${Layout.BottomHeight}px`);
        }

        for (const Slot of ["left", "right", "bottom"])
        {
            const Zone = Zones[Slot];
            const Ids = Layout.Zones[Slot].filter((Id) => Registry.has(Id));
            Zone.Tabs.innerHTML = "";
            Zone.Body.innerHTML = "";

            if (Ids.length === 0)
            {
                Zone.Root.hidden = true;
                continue;
            }
            Zone.Root.hidden = false;
            if (!Ids.includes(Layout.ActiveTab[Slot]))
                Layout.ActiveTab[Slot] = Ids[0];
            const ActiveId = Layout.ActiveTab[Slot];

            for (const Id of Ids)
            {
                const Spec = Registry.get(Id);
                const Tab = document.createElement("button");
                Tab.type = "button";
                Tab.className = "fe-dock-tab" + (Id === ActiveId ? " is-active" : "");
                Tab.draggable = true;
                Tab.innerHTML = `<span class="fe-dock-tab-grip">⋮⋮</span><span>${Spec.Title}</span>`;
                Tab.addEventListener("click", () =>
                {
                    Layout.ActiveTab[Slot] = Id;
                    PersistLayout();
                    SynchroniseDom();
                });
                Tab.addEventListener("contextmenu", (Event) =>
                {
                    Event.preventDefault();
                    ShowDockMenu(Id, Tab.getBoundingClientRect());
                });
                Tab.addEventListener("dragstart", (Event) =>
                {
                    DraggedPanelId = Id;
                    Event.dataTransfer?.setData("text/plain", Id);
                    requestAnimationFrame(() => WorkspaceContainer.classList.add("is-dragging-tab"));
                });
                Tab.addEventListener("dragend", () =>
                {
                    DraggedPanelId = null;
                    WorkspaceContainer.classList.remove("is-dragging-tab");
                });
                Zone.Tabs.appendChild(Tab);
            }

            const ActiveSpec = Registry.get(ActiveId);
            if (ActiveSpec?.Node)
            {
                ActiveSpec.Node.hidden = false;
                Zone.Body.appendChild(ActiveSpec.Node);
            }
        }

        LeftSplit.hidden   = LeftZone.Root.hidden;
        RightSplit.hidden  = RightZone.Root.hidden;
        BottomSplit.hidden = BottomZone.Root.hidden;

        // Floating windows
        FloatLayer.innerHTML = "";
        for (const [PanelId, BoxSpec] of Object.entries(Layout.Floating))
        {
            const Spec = Registry.get(PanelId);
            if (!Spec)
                continue;
            const Win = BuildDiv("fe-dock-float");
            Win.style.left   = `${BoxSpec.X}px`;
            Win.style.top    = `${BoxSpec.Y}px`;
            Win.style.width  = `${BoxSpec.Width}px`;
            Win.style.height = `${BoxSpec.Height}px`;

            const Head = BuildDiv("fe-dock-float-head");
            const TitleSpan = document.createElement("span");
            TitleSpan.textContent = `⋮⋮  ${Spec.Title}`;

            const Controls = BuildDiv("fe-dock-zone-actions");
            const DockBtn = document.createElement("button");
            DockBtn.type = "button";
            DockBtn.className = "fe-dock-iconbtn";
            DockBtn.title = "Dock options";
            DockBtn.textContent = "▾";
            DockBtn.addEventListener("click", (Event) =>
            {
                Event.stopPropagation();
                ShowDockMenu(PanelId, DockBtn.getBoundingClientRect());
            });

            const ReattachBtn = document.createElement("button");
            ReattachBtn.type = "button";
            ReattachBtn.className = "fe-dock-iconbtn";
            ReattachBtn.title = "Return to default dock";
            ReattachBtn.textContent = "↙";
            ReattachBtn.addEventListener("click", (Event) =>
            {
                Event.stopPropagation();
                MovePanel(PanelId, Spec.DefaultZone || "right");
            });

            const CloseBtn = document.createElement("button");
            CloseBtn.type = "button";
            CloseBtn.className = "fe-dock-iconbtn";
            CloseBtn.title = "Hide panel";
            CloseBtn.textContent = "×";
            CloseBtn.addEventListener("click", (Event) =>
            {
                Event.stopPropagation();
                MovePanel(PanelId, "hide");
            });

            Controls.append(DockBtn, ReattachBtn, CloseBtn);
            Head.append(TitleSpan, Controls);

            WireFloatDrag(Head, Win, BoxSpec);

            const Body = BuildDiv("fe-dock-body");
            Spec.Node.hidden = false;
            Body.appendChild(Spec.Node);
            Win.append(Head, Body);
            FloatLayer.appendChild(Win);
        }

        if (typeof window !== "undefined" && typeof window.dispatchEvent === "function" && typeof Event === "function")
            window.dispatchEvent(new Event("resize"));
    }

    function WireFloatDrag(Handle, WindowNode, BoxSpec)
    {
        Handle.addEventListener("pointerdown", (DownEvent) =>
        {
            if (DownEvent.target.closest("button"))
                return;
            DownEvent.preventDefault();
            Handle.setPointerCapture(DownEvent.pointerId);
            const StartX = DownEvent.clientX - BoxSpec.X;
            const StartY = DownEvent.clientY - BoxSpec.Y;
            const OnMove = (MoveEvent) =>
            {
                const Bounds = WorkspaceContainer.getBoundingClientRect();
                BoxSpec.X = Math.max(0, Math.min(Bounds.width - 120, Math.round(MoveEvent.clientX - StartX)));
                BoxSpec.Y = Math.max(0, Math.min(Bounds.height - 48, Math.round(MoveEvent.clientY - StartY)));
                WindowNode.style.left = `${BoxSpec.X}px`;
                WindowNode.style.top  = `${BoxSpec.Y}px`;
            };
            const OnUp = () =>
            {
                Handle.removeEventListener("pointermove", OnMove);
                Handle.removeEventListener("pointerup", OnUp);
                BoxSpec.Width  = WindowNode.offsetWidth;
                BoxSpec.Height = WindowNode.offsetHeight;
                PersistLayout();
            };
            Handle.addEventListener("pointermove", OnMove);
            Handle.addEventListener("pointerup", OnUp);
        });
    }

    WireSplitter(LeftSplit, "x", (Delta, Start) =>
    {
        Layout.LeftWidth = Math.max(180, Math.min(520, Start + Delta));
        WorkspaceContainer.style.setProperty("--fe-left-width", `${Layout.LeftWidth}px`);
    }, () => Layout.LeftWidth);

    WireSplitter(RightSplit, "x", (Delta, Start) =>
    {
        Layout.RightWidth = Math.max(240, Math.min(580, Start - Delta));
        WorkspaceContainer.style.setProperty("--fe-right-width", `${Layout.RightWidth}px`);
    }, () => Layout.RightWidth);

    WireSplitter(BottomSplit, "y", (Delta, Start) =>
    {
        Layout.BottomHeight = Math.max(120, Math.min(460, Start - Delta));
        WorkspaceContainer.style.setProperty("--fe-bottom-height", `${Layout.BottomHeight}px`);
    }, () => Layout.BottomHeight);

    function WireSplitter(SplitterNode, Axis, ApplySize, ReadInitial)
    {
        SplitterNode.addEventListener("pointerdown", (DownEvent) =>
        {
            DownEvent.preventDefault();
            SplitterNode.setPointerCapture(DownEvent.pointerId);
            SplitterNode.classList.add("fe-dock-splitter-active");
            const Origin = Axis === "x" ? DownEvent.clientX : DownEvent.clientY;
            const StartSize = ReadInitial();
            const OnMove = (MoveEvent) =>
            {
                const Current = Axis === "x" ? MoveEvent.clientX : MoveEvent.clientY;
                ApplySize(Math.round(Current - Origin), StartSize);
                window.dispatchEvent(new Event("resize"));
            };
            const OnUp = () =>
            {
                SplitterNode.classList.remove("fe-dock-splitter-active");
                SplitterNode.removeEventListener("pointermove", OnMove);
                SplitterNode.removeEventListener("pointerup", OnUp);
                PersistLayout();
                window.dispatchEvent(new Event("resize"));
            };
            SplitterNode.addEventListener("pointermove", OnMove);
            SplitterNode.addEventListener("pointerup", OnUp);
        });
    }

    function PersistLayout()
    {
        try { sessionStorage.setItem(StorageKey, JSON.stringify(Layout)); }
        catch {}
    }

    SynchroniseDom();

    return {
        MovePanel,
        TogglePanel,
        FocusPanel,
        QueryPanelVisible,
        ResetLayout,
        ShowDockMenu
    };
}

function BuildDiv(ClassName)
{
    const Node = document.createElement("div");
    Node.className = ClassName;
    return Node;
}

function CloneDefaultLayout(Registry)
{
    const Copy = JSON.parse(JSON.stringify(DefaultLayout));
    for (const [Id, Spec] of Registry.entries())
    {
        const Present = Copy.Zones.left.includes(Id) || Copy.Zones.right.includes(Id) || Copy.Zones.bottom.includes(Id);
        if (!Present)
        {
            const Zone = Spec.DefaultZone || "right";
            Copy.Zones[Zone].push(Id);
        }
    }
    return Copy;
}

function LoadSavedLayout(Registry)
{
    try
    {
        const Raw = sessionStorage.getItem(StorageKey);
        if (!Raw)
            return CloneDefaultLayout(Registry);
        const Parsed = JSON.parse(Raw);
        if (!Parsed || !Parsed.Zones)
            return CloneDefaultLayout(Registry);
        return Parsed;
    }
    catch
    {
        return CloneDefaultLayout(Registry);
    }
}
