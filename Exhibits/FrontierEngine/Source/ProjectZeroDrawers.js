/* ============================================================================================================================================
                                                          PROJECTZERODRAWERS.JS
   ============================================================================================================================================
   Exact ProjectZeroEditor Top Notch (Control Center Drawer) and Bottom Asset Browser Drawer for FrontierEngine.
*/

import { ConstructGlyph, ProjectZeroIconUris } from "./ProjectZeroIcons.js";
import { PaintOptions, SurfaceTextures } from "./VehicleAppearance.js";

const NOTCH_TRAPEZOID_SVG = `<svg viewBox="0 0 400 36" preserveAspectRatio="none" aria-hidden="true"><path d="M0 0C15 0 20 6 25 15L35 28C40 34 45 36 52 36H348C355 36 360 34 365 28L375 15C380 6 385 0 400 0Z"></path></svg>`;

function Clamp(Value, Low, High) {
    return Math.min(High, Math.max(Low, Value));
}

function ColorHexFromRgb(Rgb) {
    const ToHex = (C) => Math.round(Clamp(C, 0, 1) * 255).toString(16).padStart(2, "0");
    return `#${ToHex(Rgb[0])}${ToHex(Rgb[1])}${ToHex(Rgb[2])}`;
}

function AttachDrawerController({
    Root,
    Edge,
    HandleLabel,
    HandleClass,
    InitialFraction = 0.5,
    DefaultHeight = 520,
    OnOpenChange
}) {
    const IsTop = Edge === "top";
    const HandleWidth = 220;
    const State = {
        Open: 0,
        HandleFraction: InitialFraction,
        Dragging: false
    };

    const Surface = document.createElement("div");
    Surface.className = "drawer-surface";

    const ContentHost = document.createElement("div");
    ContentHost.style.height = "100%";
    Surface.append(ContentHost);

    const PageGrip = document.createElement("button");
    PageGrip.type = "button";
    PageGrip.className = "drawer-page-grip";
    PageGrip.innerHTML = `<i></i><span>${IsTop ? "Drag up to close" : "Drag down to close"}</span>`;
    Surface.append(PageGrip);

    const Handle = document.createElement("button");
    Handle.type = "button";
    Handle.className = `drawer-handle ${HandleClass}`;
    Handle.setAttribute("aria-label", HandleLabel);
    Handle.innerHTML = `${NOTCH_TRAPEZOID_SVG}<span>${HandleLabel}</span>`;

    Root.className = `drawer-shell ${Edge} ${Edge === "top" ? "control-drawer" : "asset-drawer"}`;
    Root.innerHTML = "";
    Root.append(Surface, Handle);

    const ViewportWidth = () => (typeof window !== "undefined" && window.innerWidth) ? window.innerWidth : 1440;
    const ViewportHeight = () => (typeof window !== "undefined" && window.innerHeight) ? window.innerHeight : 900;
    const MaxSpan = () => Math.max(260, ViewportHeight() - 36);

    const ApplyPosition = () => {
        const Span = MaxSpan();
        const VisiblePx = Math.round(Clamp(State.Open, 0, 1) * Span);
        const Offset = IsTop ? (VisiblePx - Span) : (Span - VisiblePx);
        const IsOpen = State.Open > 0.01;
        Root.classList.toggle("open", IsOpen);
        Root.classList.toggle("dragging", State.Dragging);
        Root.style.transform = `translate3d(0, ${Offset}px, 0)`;
        Surface.style.height = `${VisiblePx}px`;
        if (Root.style.setProperty) Root.style.setProperty("--drawer-visible", `${VisiblePx}px`);
        Handle.setAttribute("aria-expanded", String(IsOpen));
        const MaxLeft = Math.max(24, ViewportWidth() - HandleWidth - 12);
        const LeftPx = Math.round(Clamp(State.HandleFraction * ViewportWidth() - HandleWidth * 0.5, 12, MaxLeft));
        Handle.style.left = `${LeftPx}px`;
    };

    const SetOpen = (NextOpen) => {
        const Clamped = Clamp(NextOpen, 0, 1);
        const WasOpen = State.Open > 0.01;
        State.Open = Clamped;
        ApplyPosition();
        const NowOpen = State.Open > 0.01;
        if (WasOpen !== NowOpen && OnOpenChange) OnOpenChange(NowOpen);
    };

    const Toggle = () => {
        if (State.Open > 0.02) {
            SetOpen(0);
        } else {
            const Target = Edge === "top"
                ? Clamp(DefaultHeight / MaxSpan(), 0.45, 0.85)
                : 0.66;
            SetOpen(Target);
        }
    };

    let Gesture = null;
    const BeginDrag = (Event, AllowHorizontal) => {
        if (Event.button !== undefined && Event.button !== 0) return;
        Gesture = {
            StartX: Event.clientX || 0,
            StartY: Event.clientY || 0,
            StartOpen: State.Open,
            StartFraction: State.HandleFraction,
            Axis: null,
            Moved: false,
            AllowHorizontal
        };
        if (Event.currentTarget && Event.currentTarget.setPointerCapture && Event.pointerId !== undefined) {
            try { Event.currentTarget.setPointerCapture(Event.pointerId); } catch {}
        }
    };

    const MoveDrag = (Event) => {
        if (!Gesture) return;
        const Dx = (Event.clientX || 0) - Gesture.StartX;
        const Dy = (Event.clientY || 0) - Gesture.StartY;
        if (!Gesture.Axis && Math.hypot(Dx, Dy) > 5) {
            Gesture.Axis = (Gesture.AllowHorizontal && Math.abs(Dx) > Math.abs(Dy) * 1.15) ? "x" : "y";
            Gesture.Moved = true;
            State.Dragging = true;
        }
        if (Gesture.Axis === "x") {
            State.HandleFraction = Clamp(Gesture.StartFraction + Dx / ViewportWidth(), 0.08, 0.92);
            ApplyPosition();
        } else if (Gesture.Axis === "y") {
            const Signed = IsTop ? Dy : -Dy;
            State.Open = Clamp(Gesture.StartOpen + Signed / MaxSpan(), 0, 1);
            ApplyPosition();
        }
    };

    const EndDrag = () => {
        if (!Gesture) return;
        const Current = Gesture;
        Gesture = null;
        State.Dragging = false;
        if (!Current.Moved) {
            Toggle();
            return;
        }
        if (Current.Axis === "y") {
            if (State.Open < 0.16) SetOpen(0);
            else if (State.Open > 0.82) SetOpen(1);
            else SetOpen(State.Open);
        } else {
            ApplyPosition();
        }
    };

    Handle.addEventListener("pointerdown", (E) => BeginDrag(E, true));
    Handle.addEventListener("pointermove", MoveDrag);
    Handle.addEventListener("pointerup", EndDrag);
    Handle.addEventListener("pointercancel", EndDrag);

    PageGrip.addEventListener("pointerdown", (E) => BeginDrag(E, false));
    PageGrip.addEventListener("pointermove", MoveDrag);
    PageGrip.addEventListener("pointerup", EndDrag);
    PageGrip.addEventListener("pointercancel", EndDrag);

    if (typeof window !== "undefined" && window.addEventListener) {
        window.addEventListener("resize", ApplyPosition);
    }

    ApplyPosition();
    return {
        ContentHost,
        SetOpen,
        Toggle,
        IsOpen: () => State.Open > 0.01
    };
}

export function ConstructTopNotch(Root, Host, Callbacks = {}) {
    if (!Root) return null;
    let CurrentPage = "Dashboard";
    let NotificationsEnabled = true;

    const Drawer = AttachDrawerController({
        Root,
        Edge: "top",
        HandleLabel: "Control Center",
        HandleClass: "notch-handle",
        InitialFraction: 0.5,
        DefaultHeight: 510,
        OnOpenChange: (Open) => {
            if (!Open) CurrentPage = "Dashboard";
            RenderContent();
        }
    });

    const Shade = document.createElement("div");
    Shade.className = "shade";
    Drawer.ContentHost.append(Shade);

    const QualityPresetLabel = () => {
        if (Host.Settings.BounceCount >= 3 && Host.Settings.RenderScale >= 0.95) return "High";
        if (Host.Settings.BounceCount >= 2 && Host.Settings.RenderScale >= 0.75) return "Medium";
        return "Low";
    };

    const CycleQuality = () => {
        const Cur = QualityPresetLabel();
        if (Cur === "High") {
            Host.Settings.BounceCount = 1;
            Host.Settings.RenderScale = 0.65;
        } else if (Cur === "Low") {
            Host.Settings.BounceCount = 2;
            Host.Settings.RenderScale = 0.85;
        } else {
            Host.Settings.BounceCount = 3;
            Host.Settings.RenderScale = 1.0;
        }
        Host.ResetAccumulation();
    };

    const ReflectionSummary = () => {
        if (!Host.Settings.EnableReflections) return "Off";
        return Host.Settings.TemporalAccumulation ? "Raytraced" : "Single";
    };

    const MakeTile = (Active, IconNode, LabelText, OnClick) => {
        const Btn = document.createElement("button");
        Btn.type = "button";
        if (Active) Btn.className = "active";
        const Circle = document.createElement("i");
        Circle.append(IconNode);
        const Span = document.createElement("span");
        Span.textContent = LabelText;
        Btn.append(Circle, Span);
        Btn.addEventListener("click", () => {
            OnClick();
            if (Callbacks.OnSettingsChanged) Callbacks.OnSettingsChanged();
            RenderContent();
        });
        return Btn;
    };

    const RenderDashboard = () => {
        const Section = document.createElement("section");
        Section.className = "control-dashboard";

        const Header = document.createElement("header");
        Header.innerHTML = `<div>Control Center</div>`;
        const RightActions = document.createElement("span");
        RightActions.append(ConstructGlyph("wireless", 16));
        const GearBtn = document.createElement("button");
        GearBtn.type = "button";
        GearBtn.title = "Open Settings";
        GearBtn.append(ConstructGlyph("settings", 16));
        GearBtn.addEventListener("click", () => {
            CurrentPage = "Settings";
            RenderContent();
        });
        RightActions.append(GearBtn);
        Header.append(RightActions);

        const Grid = document.createElement("div");
        Grid.className = "dashboard-grid";

        Grid.append(
            MakeTile(
                Host.Settings.BounceCount > 0,
                ConstructGlyph("sun", 22),
                Host.Settings.BounceCount === 0
                    ? "GI: Off"
                    : `GI: ${Host.Settings.BounceCount} ${Host.Settings.BounceCount === 1 ? "Bounce" : "Bounces"}`,
                () => {
                    Host.Settings.BounceCount = (Host.Settings.BounceCount + 1) % 5;
                    Host.ResetAccumulation();
                }
            ),
            MakeTile(
                Boolean(Host.Settings.EnableReflections),
                ConstructGlyph("reflection", 22),
                `Refl: ${ReflectionSummary()}`,
                () => {
                    Host.Settings.EnableReflections = !Host.Settings.EnableReflections;
                    Host.ResetAccumulation();
                }
            ),
            MakeTile(
                Boolean(Host.Settings.TemporalAccumulation),
                ConstructGlyph("aa", 22),
                "Anti-Aliasing",
                () => {
                    Host.Settings.TemporalAccumulation = !Host.Settings.TemporalAccumulation;
                    Host.ResetAccumulation();
                }
            ),
            MakeTile(
                Boolean(Callbacks.IsDiagnosticsVisible && Callbacks.IsDiagnosticsVisible()),
                ConstructGlyph("fps", 22),
                "FPS Overlay",
                () => {
                    if (Callbacks.OnToggleDiagnostics) Callbacks.OnToggleDiagnostics();
                }
            ),
            MakeTile(
                NotificationsEnabled,
                ConstructGlyph("bell", 22),
                "Notifications",
                () => { NotificationsEnabled = !NotificationsEnabled; }
            ),
            MakeTile(
                QualityPresetLabel() !== "Low",
                ConstructGlyph("sliders", 22),
                QualityPresetLabel(),
                CycleQuality
            ),
            MakeTile(
                Boolean(Host.Settings.ShowSurfaceCards),
                ConstructGlyph("patches", 22),
                `Cards: ${Host.Settings.ShowSurfaceCards ? "On" : "Off"}`,
                () => {
                    Host.Settings.ShowSurfaceCards = !Host.Settings.ShowSurfaceCards;
                    Host.ResetAccumulation();
                }
            ),
            MakeTile(
                Boolean(Host.Settings.SpatialReuse),
                ConstructGlyph("ray", 22),
                "Raytracing",
                () => {
                    Host.Settings.SpatialReuse = !Host.Settings.SpatialReuse;
                    Host.ResetAccumulation();
                }
            )
        );

        const ScaleBar = document.createElement("div");
        ScaleBar.className = "render-scale";
        ScaleBar.append(ConstructGlyph("monitor", 18));
        const Range = document.createElement("input");
        Range.type = "range";
        Range.min = "25";
        Range.max = "100";
        Range.step = "5";
        Range.value = String(Math.round((Host.Settings.RenderScale || 1.0) * 100));
        Range.setAttribute("aria-label", "Render scale");
        const Output = document.createElement("output");
        Output.textContent = `${Range.value}%`;
        Range.addEventListener("input", () => {
            const Pct = Number(Range.value);
            Host.Settings.RenderScale = Clamp(Pct / 100, 0.25, 1.0);
            Output.textContent = `${Pct}%`;
            Host.ResetAccumulation();
            if (Callbacks.OnSettingsChanged) Callbacks.OnSettingsChanged();
        });
        ScaleBar.append(Range, Output);

        Section.append(Header, Grid, ScaleBar);
        return Section;
    };

    const RenderSettingsHub = () => {
        const Section = document.createElement("section");
        Section.className = "settings-hub";

        const Header = document.createElement("header");
        const BackBtn = document.createElement("button");
        BackBtn.type = "button";
        BackBtn.append(ConstructGlyph("back", 16));
        BackBtn.addEventListener("click", () => { CurrentPage = "Dashboard"; RenderContent(); });

        const TitleWrap = document.createElement("div");
        TitleWrap.innerHTML = `<div>Settings</div><small>Engine &amp; Editor</small>`;

        const CloseBtn = document.createElement("button");
        CloseBtn.type = "button";
        CloseBtn.append(ConstructGlyph("close", 16));
        CloseBtn.addEventListener("click", () => Drawer.SetOpen(0));
        Header.append(BackBtn, TitleWrap, CloseBtn);
        Section.append(Header);

        const HubEntries = [
            ["Render", "sliders", "Render Settings", "Global illumination, ReSTIR spatial/temporal reuse & scale"],
            ["Appearance", "palette", "Appearance", "Workspace docking, panel chrome & cabin HUD"],
            ["Input", "gamepad", "Input", "Navigation camera speed, vehicle drive & shortcuts"],
            ["Notifications", "bell", "Notifications", "Diagnostic toasts & telemetry alerts"],
            ["Materials", "swatch", "Materials", "Open the bottom Asset Browser material library"]
        ];
        for (const [Id, IconName, Title, Desc] of HubEntries) {
            const Row = document.createElement("button");
            Row.type = "button";
            Row.className = "hub-row";
            Row.append(ConstructGlyph(IconName, 20));
            const Copy = document.createElement("span");
            Copy.innerHTML = `${Title}<small>${Desc}</small>`;
            Row.append(Copy, ConstructGlyph("chevron", 14));
            Row.addEventListener("click", () => {
                if (Id === "Materials") {
                    Drawer.SetOpen(0);
                    if (Callbacks.OnOpenAssetBrowser) Callbacks.OnOpenAssetBrowser("materials");
                    return;
                }
                CurrentPage = Id;
                RenderContent();
            });
            Section.append(Row);
        }
        return Section;
    };

    const RenderSubDialog = (Id) => {
        const Titles = {
            Render: ["Render Settings", "Live WebGPU ReSTIR GI, reflection rays, and internal resolution."],
            Appearance: ["Appearance", "ProjectZeroEditor dark neutral workspace and cabin HUD."],
            Input: ["Input", "Viewport orbit/fly controls and vehicle dynamics controls."],
            Notifications: ["Notifications", "Telemetry overlays and runtime alerts."]
        };
        const [Title, Desc] = Titles[Id] || ["Settings", ""];
        const Section = document.createElement("section");
        Section.className = "settings-dialog";

        const Header = document.createElement("header");
        const BackBtn = document.createElement("button");
        BackBtn.type = "button";
        BackBtn.append(ConstructGlyph("back", 16));
        BackBtn.addEventListener("click", () => { CurrentPage = "Settings"; RenderContent(); });
        const Copy = document.createElement("div");
        Copy.innerHTML = `<h2>${Title}</h2><p>${Desc}</p>`;
        const CloseBtn = document.createElement("button");
        CloseBtn.type = "button";
        CloseBtn.append(ConstructGlyph("close", 16));
        CloseBtn.addEventListener("click", () => Drawer.SetOpen(0));
        Header.append(BackBtn, Copy, CloseBtn);

        const Body = document.createElement("div");
        Body.className = "settings-body";

        const AddToggleRow = (LabelText, Checked, OnChange) => {
            const Row = document.createElement("label");
            Row.className = "cc-setting-row";
            const Span = document.createElement("span");
            Span.textContent = LabelText;
            const Input = document.createElement("input");
            Input.type = "checkbox";
            Input.checked = Checked;
            Input.addEventListener("change", () => {
                OnChange(Input.checked);
                if (Callbacks.OnSettingsChanged) Callbacks.OnSettingsChanged();
            });
            Row.append(Span, Input);
            Body.append(Row);
        };

        if (Id === "Render") {
            AddToggleRow("Temporal Reservoir Accumulation", Boolean(Host.Settings.TemporalAccumulation), (V) => {
                Host.Settings.TemporalAccumulation = V;
                Host.ResetAccumulation();
            });
            AddToggleRow("ReSTIR Spatial Reuse", Boolean(Host.Settings.SpatialReuse), (V) => {
                Host.Settings.SpatialReuse = V;
                Host.ResetAccumulation();
            });
            AddToggleRow("Specular Multi-Bounce Reflections", Boolean(Host.Settings.EnableReflections), (V) => {
                Host.Settings.EnableReflections = V;
                Host.ResetAccumulation();
            });
            AddToggleRow("Post Process Grading & Vignette", Boolean(Host.Settings.PostEnabled), (V) => {
                Host.Settings.PostEnabled = V;
                Host.ApplyPostProcessStyle();
            });
        } else if (Id === "Appearance") {
            AddToggleRow("Cabin Telemetry HUD Overlay", Boolean(Host.Settings.ShowCabinOverlay), (V) => {
                Host.Settings.ShowCabinOverlay = V;
                if (Host.Overlay) Host.Overlay.SetVisible(V);
            });
            AddToggleRow("Surface Radiance Cache Debug", Boolean(Host.Settings.ShowSurfaceCards), (V) => {
                Host.Settings.ShowSurfaceCards = V;
                Host.ResetAccumulation();
            });
        } else if (Id === "Input") {
            const Info = document.createElement("div");
            Info.className = "cc-setting-row";
            Info.innerHTML = `<span>RMB + WASD / QE Fly · W/S Throttle · A/D Steer · Space Handbrake · C Camera · F3 Diagnostics</span>`;
            Body.append(Info);
        } else {
            AddToggleRow("Enable Viewport Diagnostic Banners", NotificationsEnabled, (V) => {
                NotificationsEnabled = V;
            });
        }

        Section.append(Header, Body);
        return Section;
    };

    function RenderContent() {
        Shade.innerHTML = "";
        if (CurrentPage === "Dashboard") Shade.append(RenderDashboard());
        else if (CurrentPage === "Settings") Shade.append(RenderSettingsHub());
        else Shade.append(RenderSubDialog(CurrentPage));
    }

    RenderContent();
    return {
        ...Drawer,
        Refresh: RenderContent
    };
}

export function ConstructBottomAssetBrowser(Root, Host, Callbacks = {}) {
    if (!Root) return null;

    const BuildCatalogue = () => {
        const Assets = [];
        for (const Paint of PaintOptions) {
            const Slug = Paint.Name.toLowerCase().replace(/\s+/g, "-");
            Assets.push({
                Id: `mat-paint-${Slug}`,
                Name: `${Paint.Name}`,
                Kind: "materials",
                Origin: "Engine",
                Extension: ".fmat",
                Path: `EngineContent/Materials/Automotive/${Slug}.fmat`,
                Color: Paint.Hex,
                PaintHex: Paint.Hex,
                Metallic: Paint.Metallic,
                Description: `${Paint.Metallic ? "Metallic" : "Satin"} dual-lobe automotive finish with physical Fresnel response and live 3D paint ripple.`
            });
        }
        for (let Index = 0; Index < SurfaceTextures.length; ++Index) {
            const Surface = SurfaceTextures[Index];
            const Slug = Surface.Short.toLowerCase().replace(/\s+/g, "-");
            Assets.push({
                Id: `mat-surface-${Slug}`,
                Name: `${Surface.Name}`,
                Kind: "materials",
                Origin: "Project",
                Extension: ".fmat",
                Path: `Project/Materials/Finishes/${Slug}.fmat`,
                Color: ["#7ea0b8", "#383c42", "#b5bcc4", "#d8d4ca"][Index % 4],
                SurfaceId: Surface.Id,
                Description: `Procedural body surface finish (${Surface.Name}) evaluated in WebGPU G-buffer & reflection shaders.`
            });
        }
        Assets.push(
            {
                Id: "mdl-vehicle",
                Name: "ControlVehicle Chassis",
                Kind: "models",
                Origin: "Project",
                Extension: ".fmesh",
                Path: "Project/Vehicles/ControlVehicle.fmesh",
                Description: "Rigid-body chassis with 4-corner suspension, steered wheel hubs, and procedural SDF proxy."
            },
            {
                Id: "mdl-tyre",
                Name: "XPBD Deformable Tyre",
                Kind: "models",
                Origin: "Project",
                Extension: ".fmesh",
                Path: "Project/Vehicles/DeformableTyre.fmesh",
                Description: "5-ring × 28-segment XPBD carcass mesh with live ground-contact deformation."
            },
            {
                Id: "mdl-cyclorama",
                Name: "Lobby Studio Cyclorama",
                Kind: "models",
                Origin: "Project",
                Extension: ".fscene",
                Path: "Project/Scenes/LobbyStudio.fscene",
                Description: "Sunless enclosed automotive studio bay with softbox banks and rim strips."
            }
        );
        const Shaders = [
            ["RayTraversal.wgsl", "Primary BVH + analytical SDF ray traversal and G-buffer generation."],
            ["DirectReservoir.wgsl", "ReSTIR direct reservoir sampling across sun, softboxes, and emissive bay lights."],
            ["ReservoirSpatial.wgsl", "Spatial-temporal reservoir reuse and multi-bounce indirect radiance."],
            ["ReflectionTraversal.wgsl", "Roughness-driven glossy reflection ray tracing with multi-bounce GI."],
            ["DisplayRecombination.wgsl", "ACES filmic exposure, surface cache recombination, and tone mapping."]
        ];
        for (const [File, Desc] of Shaders) {
            Assets.push({
                Id: `code-${File}`,
                Name: File.replace(".wgsl", ""),
                Kind: "code",
                Origin: "Engine",
                Extension: ".wgsl",
                Path: `Exhibits/FrontierEngine/Shader/${File}`,
                Description: Desc
            });
        }
        for (const [Key, Uri] of Object.entries(ProjectZeroIconUris)) {
            Assets.push({
                Id: `icon-${Key}`,
                Name: Key,
                Kind: "icons",
                Origin: "Engine",
                Extension: ".svg",
                Path: `EngineContent/Icons/${Key}.svg`,
                PreviewUri: Uri,
                Description: "Native Project-Zero SVG vector icon."
            });
        }
        return Assets;
    };

    const CustomAssets = [];
    let Category = "all";
    let Origin = "All";
    let Search = "";
    let ViewMode = "grid";
    let SelectedId = "mat-paint-nebula-silver";

    const Drawer = AttachDrawerController({
        Root,
        Edge: "bottom",
        HandleLabel: "Asset Browser",
        HandleClass: "asset-notch-handle",
        InitialFraction: 0.5,
        DefaultHeight: 560,
        OnOpenChange: () => RenderBrowser()
    });

    const Categories = [
        ["all", "All"],
        ["materials", "Materials"],
        ["models", "Models"],
        ["icons", "Icons"],
        ["code", "Code"],
        ["images", "Images"],
        ["fonts", "Fonts"],
        ["other", "Other"]
    ];

    const RenderBrowser = () => {
        Drawer.ContentHost.innerHTML = "";
        const AllAssets = [...CustomAssets, ...BuildCatalogue()];
        const Filtered = AllAssets.filter((Item) => {
            if (Category !== "all" && Item.Kind !== Category) return false;
            if (Origin !== "All" && Item.Origin !== Origin) return false;
            if (Search) {
                const Needle = Search.toLowerCase();
                if (!`${Item.Name} ${Item.Path} ${Item.Extension}`.toLowerCase().includes(Needle)) return false;
            }
            return true;
        });

        const Selected = Filtered.find((A) => A.Id === SelectedId) || AllAssets.find((A) => A.Id === SelectedId) || Filtered[0] || null;

        const Section = document.createElement("section");
        Section.className = "asset-browser";

        // Header
        const Header = document.createElement("header");
        Header.className = "asset-browser-header";
        Header.innerHTML = `<div><span class="asset-eyebrow">FRONTIER ASSET DEPOT</span><h1>Asset Browser <small>${AllAssets.length} indexed</small></h1></div>`;

        const HeaderActions = document.createElement("div");
        HeaderActions.className = "asset-header-actions";

        const NewMatBtn = document.createElement("button");
        NewMatBtn.type = "button";
        NewMatBtn.className = "primary";
        NewMatBtn.textContent = "+ Material";
        NewMatBtn.addEventListener("click", () => {
            const HueColor = ["#e06c75", "#61afef", "#98c379", "#d19a66", "#c678dd"][CustomAssets.length % 5];
            const Id = `custom-mat-${CustomAssets.length + 1}`;
            CustomAssets.unshift({
                Id,
                Name: `Custom Studio Coat ${CustomAssets.length + 1}`,
                Kind: "materials",
                Origin: "Project",
                Extension: ".fmat",
                Path: `Project/Materials/CustomCoat${CustomAssets.length + 1}.fmat`,
                Color: HueColor,
                CustomRgb: [
                    parseInt(HueColor.slice(1, 3), 16) / 255,
                    parseInt(HueColor.slice(3, 5), 16) / 255,
                    parseInt(HueColor.slice(5, 7), 16) / 255
                ],
                Description: "User-authored Project-Zero automotive clearcoat swatch."
            });
            Category = "materials";
            SelectedId = Id;
            RenderBrowser();
        });

        const SaveMatBtn = document.createElement("button");
        SaveMatBtn.type = "button";
        SaveMatBtn.textContent = "Save vehicle finish";
        SaveMatBtn.addEventListener("click", () => {
            const Hex = (Host.Appearance && Host.Appearance.Colour) ? Host.Appearance.Colour : "#c2c8d0";
            const Id = `saved-mat-${CustomAssets.length + 1}`;
            CustomAssets.unshift({
                Id,
                Name: `Saved Vehicle Coat ${CustomAssets.length + 1}`,
                Kind: "materials",
                Origin: "Project",
                Extension: ".fmat",
                Path: `Project/Materials/SavedVehicle${CustomAssets.length + 1}.fmat`,
                Color: Hex,
                PaintHex: Hex,
                Metallic: true,
                Description: `Captured from ControlVehicle (${Hex}).`
            });
            SelectedId = Id;
            RenderBrowser();
        });

        const CloseBtn = document.createElement("button");
        CloseBtn.type = "button";
        CloseBtn.className = "close-btn";
        CloseBtn.textContent = "×";
        CloseBtn.title = "Close Asset Browser";
        CloseBtn.addEventListener("click", () => Drawer.SetOpen(0));

        HeaderActions.append(NewMatBtn, SaveMatBtn, CloseBtn);
        Header.append(HeaderActions);

        // Tools bar
        const Tools = document.createElement("div");
        Tools.className = "asset-browser-tools";

        const OriginTabs = document.createElement("div");
        OriginTabs.className = "asset-origin-tabs";
        for (const Tab of ["All", "Project", "Imported", "Engine"]) {
            const Btn = document.createElement("button");
            Btn.type = "button";
            Btn.textContent = Tab;
            Btn.setAttribute("aria-pressed", String(Origin === Tab));
            Btn.addEventListener("click", () => { Origin = Tab; RenderBrowser(); });
            OriginTabs.append(Btn);
        }

        const SearchInput = document.createElement("input");
        SearchInput.type = "search";
        SearchInput.placeholder = "Search materials, meshes, WGSL shaders, icons...";
        SearchInput.value = Search;
        SearchInput.addEventListener("input", () => {
            Search = SearchInput.value;
            RenderBrowser();
        });

        const ViewSwitch = document.createElement("div");
        ViewSwitch.className = "asset-view-switch";
        for (const [Mode, Glyph] of [["grid", "⊞"], ["list", "☰"]]) {
            const Btn = document.createElement("button");
            Btn.type = "button";
            Btn.textContent = Glyph;
            Btn.setAttribute("aria-pressed", String(ViewMode === Mode));
            Btn.addEventListener("click", () => { ViewMode = Mode; RenderBrowser(); });
            ViewSwitch.append(Btn);
        }

        Tools.append(OriginTabs, SearchInput, ViewSwitch);

        // Body
        const Body = document.createElement("div");
        Body.className = "asset-browser-body";

        const Rail = document.createElement("nav");
        Rail.className = "asset-category-rail";
        for (const [Key, Label] of Categories) {
            const Count = Key === "all" ? AllAssets.length : AllAssets.filter((A) => A.Kind === Key).length;
            const Btn = document.createElement("button");
            Btn.type = "button";
            Btn.setAttribute("aria-pressed", String(Category === Key));
            Btn.innerHTML = `<span>${Label}</span><small>${Count}</small>`;
            Btn.addEventListener("click", () => { Category = Key; RenderBrowser(); });
            Rail.append(Btn);
        }

        const Results = document.createElement("div");
        Results.className = "asset-results";
        const ResultsHeader = document.createElement("header");
        const ActiveLabel = (Categories.find((C) => C[0] === Category) || Categories[0])[1];
        ResultsHeader.innerHTML = `<h2>${ActiveLabel}</h2><span>${Filtered.length} shown</span>`;

        const Catalogue = document.createElement("div");
        Catalogue.className = `asset-catalogue ${ViewMode}`;

        for (const Asset of Filtered) {
            const Cell = document.createElement("button");
            Cell.type = "button";
            Cell.className = `asset-cell${Selected && Selected.Id === Asset.Id ? " selected" : ""}`;

            const Thumb = document.createElement("span");
            Thumb.className = "asset-thumbnail";
            if (Asset.Kind === "materials") {
                const Orb = document.createElement("i");
                Orb.className = "asset-material-orb";
                if (Orb.style.setProperty) Orb.style.setProperty("--orb", Asset.Color || "#88aacc");
                else Orb.style.backgroundColor = Asset.Color || "#88aacc";
                Thumb.append(Orb);
            } else if (Asset.PreviewUri) {
                const Img = document.createElement("img");
                Img.src = Asset.PreviewUri;
                Img.alt = "";
                Thumb.append(Img);
            } else {
                const KindGlyph = document.createElement("span");
                KindGlyph.className = "asset-kind-icon";
                KindGlyph.textContent = Asset.Kind === "models" ? "◈" : (Asset.Kind === "code" ? "{}" : "•");
                Thumb.append(KindGlyph);
            }

            const NameSpan = document.createElement("span");
            NameSpan.className = "asset-cell-name";
            NameSpan.innerHTML = `${Asset.Name}<small>${Asset.Origin} · ${Asset.Extension}</small>`;

            Cell.append(Thumb, NameSpan);
            Cell.addEventListener("click", () => {
                SelectedId = Asset.Id;
                RenderBrowser();
            });
            Cell.addEventListener("dblclick", () => ApplyAsset(Asset));
            Catalogue.append(Cell);
        }

        Results.append(ResultsHeader, Catalogue);

        const Preview = document.createElement("aside");
        Preview.className = "asset-preview";
        if (Selected) {
            Preview.innerHTML = `
                <div class="asset-preview-heading">
                    <span class="asset-origin-label">${Selected.Origin}</span>
                    <small>${Selected.Extension}</small>
                </div>
                <h2>${Selected.Name}</h2>
                <p>${Selected.Description || ""}</p>
                <dl>
                    <dt>Category</dt><dd>${Selected.Kind}</dd>
                    <dt>Path</dt><dd>${Selected.Path}</dd>
                </dl>
            `;
            if (Selected.Kind === "materials") {
                const Actions = document.createElement("div");
                Actions.className = "asset-preview-actions";
                const ApplyBtn = document.createElement("button");
                ApplyBtn.type = "button";
                ApplyBtn.className = "primary";
                ApplyBtn.textContent = "Apply to ControlVehicle";
                ApplyBtn.addEventListener("click", () => ApplyAsset(Selected));
                Actions.append(ApplyBtn);
                Preview.append(Actions);
            }
        }

        Body.append(Rail, Results, Preview);

        const Footer = document.createElement("footer");
        Footer.className = "asset-browser-footer";
        Footer.innerHTML = `<span>EngineContent + Project live asset depot</span><span>Double-click any material swatch to apply to ControlVehicle</span>`;

        Section.append(Header, Tools, Body, Footer);
        Drawer.ContentHost.append(Section);
    };

    const ApplyAsset = (Asset) => {
        if (!Asset || Asset.Kind !== "materials") return;
        if (Asset.PaintHex && Host.Appearance?.BeginPaintRipple) {
            Host.Appearance.BeginPaintRipple(Asset.PaintHex, [0, 0, 0], { Metallic: Asset.Metallic !== false });
            Host.ResetAccumulation?.();
        } else if (typeof Asset.SurfaceId === "number" && Host.Appearance?.AssignSurfaceTexture) {
            Host.Appearance.AssignSurfaceTexture(Asset.SurfaceId);
            Host.ApplySetting?.("SurfaceTexture");
        } else if (Asset.Color && Host.Appearance?.BeginPaintRipple) {
            Host.Appearance.BeginPaintRipple(Asset.Color, [0, 0, 0], { Metallic: true });
            Host.ResetAccumulation?.();
        }
        if (Callbacks.OnAssetApplied) Callbacks.OnAssetApplied(Asset);
    };

    RenderBrowser();
    return {
        ...Drawer,
        OpenCategory: (NextCategory) => {
            Category = NextCategory || "all";
            RenderBrowser();
            Drawer.SetOpen(0.66);
        },
        Refresh: RenderBrowser
    };
}
