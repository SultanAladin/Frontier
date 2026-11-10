import React, { useState, useRef, useEffect } from "react";
import OutlinerIcon from "./outliner-icons.jsx";
import { ProjectZeroIcons } from "./project-zero-icons.jsx";

const PreviewAnchors = Object.freeze({
    sun:             [76, 16],
    flare:           [70, 22],
    moon:            [22, 17],
    stars:           [34, 13],
    sky:             [50, 14],
    rainbow:         [61, 24],
    cloud:           [46, 25],
    precip:          [46, 36],
    "local-cloud":   [30, 31],
    "local-fog":     [64, 42],
    air:             [18, 34],
    terrain:         [50, 74],
    forest:          [28, 63],
    water:           [70, 73],
    ocean:           [83, 68],
    river:           [56, 81],
    liquid:          [38, 79],
    cube:            [43, 56],
    sphere:          [53, 54],
    cylinder:        [62, 57],
    plane:           [49, 64],
    material:        [24, 52],
    "editor-camera": [15, 76],
    "cine-camera":   [25, 72],
    "post-process":  [17, 62],
    "studio-key":    [50, 20],
    "studio-rim":    [74, 34],
    turntable:       [50, 62]
});

export function PzActionIcon({ name, size = 16 })
{
    let paths = null;
    switch (name) {
        case "edit":
            paths = <><path d="M12 20h9" /><path d="M16.5 3.5a2.1 2.1 0 0 1 3 3L7 19l-4 1 1-4Z" /></>;
            break;
        case "simulate":
            paths = <><circle cx="12" cy="12" r="3" /><path d="M12 2v4m0 12v4M4.93 4.93l2.83 2.83m8.48 8.48 2.83 2.83M2 12h4m12 0h4M4.93 19.07l2.83-2.83m8.48-8.48 2.83-2.83" /></>;
            break;
        case "play":
            paths = <polygon points="6 4 20 12 6 20 6 4" fill="currentColor" stroke="none" />;
            break;
        case "construct":
            paths = <><path d="M12 3 4 7.5v9L12 21l8-4.5v-9L12 3Z" /><path d="M12 12 20 7.5M12 12v9M12 12 4 7.5" /></>;
            break;
        case "focus":
            paths = <><circle cx="12" cy="12" r="3" /><path d="M3 7V5a2 2 0 0 1 2-2h2m10 0h2a2 2 0 0 1 2 2v2m0 10v2a2 2 0 0 1-2 2h-2M7 21H5a2 2 0 0 1-2-2v-2" /></>;
            break;
        case "camera":
            paths = <><path d="M14.5 4h-5L7 7H4a2 2 0 0 0-2 2v9a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2V9a2 2 0 0 0-2-2h-3l-2.5-3Z" /><circle cx="12" cy="13" r="3" /></>;
            break;
        case "split":
            paths = <><rect x="3" y="4" width="18" height="16" rx="2" /><line x1="12" y1="4" x2="12" y2="20" /></>;
            break;
        case "diagnostics":
            paths = <polyline points="22 12 18 12 15 21 9 3 6 12 2 12" />;
            break;
        case "settings":
            paths = <><circle cx="12" cy="12" r="3" /><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 1 1-4 0v-.09a1.65 1.65 0 0 0-1-1.51 1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 1 1 0-4h.09a1.65 1.65 0 0 0 1.51-1 1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33h.08a1.65 1.65 0 0 0 1-1.51V3a2 2 0 1 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82v.08a1.65 1.65 0 0 0 1.51 1H21a2 2 0 1 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1Z" /></>;
            break;
        case "sun":
            paths = <><circle cx="12" cy="12" r="4" /><path d="M12 2v2m0 16v2M4.93 4.93l1.41 1.41m11.32 11.32 1.41 1.41M2 12h2m16 0h2M6.34 17.66l-1.41 1.41M19.07 4.93l-1.41 1.41" /></>;
            break;
        case "reflection":
            paths = <><path d="m12 3 4 7-4 11-4-11 4-7Z" /><path d="M4 13h16" /></>;
            break;
        case "aa":
            paths = <><rect x="3" y="3" width="7" height="7" rx="1" /><rect x="14" y="3" width="7" height="7" rx="1" /><rect x="3" y="14" width="7" height="7" rx="1" /><rect x="14" y="14" width="7" height="7" rx="1" /></>;
            break;
        case "fps":
            paths = <><path d="m12 14 4-4" /><path d="M3.34 19a10 10 0 1 1 17.32 0" /></>;
            break;
        case "bell":
            paths = <><path d="M6 8a6 6 0 0 1 12 0c0 7 3 9 3 9H3s3-2 3-9" /><path d="M10.3 21a1.94 1.94 0 0 0 3.4 0" /></>;
            break;
        case "sliders":
            paths = <><line x1="4" y1="21" x2="4" y2="14" /><line x1="4" y1="10" x2="4" y2="3" /><line x1="12" y1="21" x2="12" y2="12" /><line x1="12" y1="8" x2="12" y2="3" /><line x1="20" y1="21" x2="20" y2="16" /><line x1="20" y1="12" x2="20" y2="3" /><line x1="2" y1="14" x2="6" y2="14" /><line x1="10" y1="8" x2="14" y2="8" /><line x1="18" y1="16" x2="22" y2="16" /></>;
            break;
        case "patches":
            paths = <><rect x="3" y="3" width="18" height="18" rx="2" /><path d="M3 9h18M3 15h18M9 3v18M15 3v18" /></>;
            break;
        case "ray":
            paths = <path d="M13 2 3 14h9l-1 8 10-12h-9l1-8Z" />;
            break;
        case "monitor":
            paths = <><rect x="2" y="3" width="20" height="14" rx="2" /><line x1="8" y1="21" x2="16" y2="21" /><line x1="12" y1="17" x2="12" y2="21" /></>;
            break;
        case "wireless":
            paths = <><path d="M5 12.55a11 11 0 0 1 14.08 0" /><path d="M1.42 9a16 16 0 0 1 21.16 0" /><path d="M8.53 16.11a6 6 0 0 1 6.95 0" /><circle cx="12" cy="20" r="1" fill="currentColor" /></>;
            break;
        case "back":
            paths = <polyline points="15 18 9 12 15 6" />;
            break;
        case "close":
            paths = <><line x1="18" y1="6" x2="6" y2="18" /><line x1="6" y1="6" x2="18" y2="18" /></>;
            break;
        case "chevron":
            paths = <polyline points="9 18 15 12 9 6" />;
            break;
        case "plus":
            paths = <><line x1="12" y1="5" x2="12" y2="19" /><line x1="5" y1="12" x2="19" y2="12" /></>;
            break;
        case "search":
            paths = <><circle cx="11" cy="11" r="8" /><line x1="21" y1="21" x2="16.65" y2="16.65" /></>;
            break;
        case "filter":
            paths = <polygon points="22 3 2 3 10 12.46 10 19 14 21 14 12.46 22 3" />;
            break;
        case "viewport":
            paths = <><rect x="3" y="4" width="18" height="16" rx="2" /><path d="M3 9h18" /></>;
            break;
        case "swatch":
        case "palette":
            paths = <><circle cx="12" cy="12" r="9" /><circle cx="9" cy="10" r="1.4" fill="currentColor" /><circle cx="15" cy="10" r="1.4" fill="currentColor" /><circle cx="12" cy="15" r="1.4" fill="currentColor" /></>;
            break;
        case "gamepad":
            paths = <><rect x="2" y="6" width="20" height="12" rx="4" /><path d="M6 12h4m-2-2v4m7-1h.01M18 11h.01" /></>;
            break;
        case "refresh":
            paths = <><path d="M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8" /><path d="M3 3v5h5" /></>;
            break;
        default:
            paths = <circle cx="12" cy="12" r="5" />;
    }
    return (
        <svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.7" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true" style={{ flexShrink: 0 }}>
            {paths}
        </svg>
    );
}

// Exact ProjectZeroEditor DrawerPanel (top Control Center notch & bottom Asset Browser notch)
export function DrawerPanel({ Edge = "top", Open, Toggle, Label, ClassName = "", DefaultHeight = 520, children })
{
    const IsTop = Edge === "top";
    const [VpSize, SetVpSize] = useState(() => ({
        w: typeof window !== "undefined" && window.innerWidth ? window.innerWidth : 1440,
        h: typeof window !== "undefined" && window.innerHeight ? window.innerHeight : 900
    }));
    const [Progress, SetProgress] = useState(Open ? 1 : 0);
    const [HandleFrac, SetHandleFrac] = useState(0.5);
    const [Dragging, SetDragging] = useState(false);
    const GestureRef = useRef(null);
    const LastPointerToggleRef = useRef(0);

    useEffect(() => {
        const OnResize = () => SetVpSize({ w: window.innerWidth || 1440, h: window.innerHeight || 900 });
        window.addEventListener("resize", OnResize);
        return () => window.removeEventListener("resize", OnResize);
    }, []);

    useEffect(() => {
        if (!GestureRef.current) {
            const Span = Math.max(280, VpSize.h - 36);
            SetProgress(Open ? (IsTop ? Math.min(0.85, Math.max(0.45, DefaultHeight / Span)) : 0.68) : 0);
        }
    }, [Open, VpSize.h, IsTop, DefaultHeight]);

    useEffect(() => {
        if (!Open) return;
        const OnKey = (e) => {
            if (e.key === "Escape") {
                e.stopPropagation();
                Toggle(false);
            }
        };
        window.addEventListener("keydown", OnKey);
        return () => window.removeEventListener("keydown", OnKey);
    }, [Open, Toggle]);

    const Span = Math.max(280, VpSize.h - 36);
    const VisiblePx = Math.round(Math.max(0, Math.min(1, Progress)) * Span);
    const OffsetY = IsTop ? (VisiblePx - Span) : (Span - VisiblePx);
    const HandleLeft = Math.round(Math.max(12, Math.min(VpSize.w - 232, HandleFrac * VpSize.w - 110)));

    const BeginDrag = (e, AllowHorizontal) => {
        if (e.button !== undefined && e.button !== 0) return;
        GestureRef.current = {
            x: e.clientX || 0,
            y: e.clientY || 0,
            startProg: Progress,
            startFrac: HandleFrac,
            axis: null,
            moved: false,
            allowHorizontal: AllowHorizontal
        };
        try { e.currentTarget.setPointerCapture?.(e.pointerId); } catch {}
    };

    const MoveDrag = (e) => {
        const g = GestureRef.current;
        if (!g) return;
        const dx = (e.clientX || 0) - g.x;
        const dy = (e.clientY || 0) - g.y;
        if (!g.axis && Math.hypot(dx, dy) > 5) {
            g.axis = (g.allowHorizontal && Math.abs(dx) > Math.abs(dy) * 1.15) ? "x" : "y";
            g.moved = true;
            SetDragging(true);
        }
        if (g.axis === "x") {
            SetHandleFrac(Math.max(0.08, Math.min(0.92, g.startFrac + dx / VpSize.w)));
        } else if (g.axis === "y") {
            const signed = IsTop ? dy : -dy;
            SetProgress(Math.max(0, Math.min(1, g.startProg + signed / Span)));
        }
    };

    const EndDrag = () => {
        const g = GestureRef.current;
        if (!g) return;
        GestureRef.current = null;
        SetDragging(false);
        if (!g.moved) {
            LastPointerToggleRef.current = Date.now();
            Toggle(!Open);
            return;
        }
        if (g.axis === "y") {
            if (Progress < 0.16) {
                SetProgress(0);
                Toggle(false);
            } else {
                const snapped = Progress > 0.82 ? 1 : Progress;
                SetProgress(snapped);
                Toggle(true);
            }
        }
    };

    const IsOpen = Progress > 0.01;
    const HandleClass = IsTop ? "notch-handle" : "asset-notch-handle";

    return (
        <div
            className={`drawer-shell ${Edge} ${ClassName} ${IsOpen ? "open" : ""} ${Dragging ? "dragging" : ""}`}
            style={{ transform: `translate3d(0, ${OffsetY}px, 0)` }}
        >
            <div className="drawer-surface" style={{ height: `${VisiblePx}px` }}>
                <div style={{ height: "100%" }}>{children}</div>
                <button
                    type="button"
                    className="drawer-page-grip"
                    onPointerDown={(e) => BeginDrag(e, false)}
                    onPointerMove={MoveDrag}
                    onPointerUp={EndDrag}
                    onPointerCancel={EndDrag}
                >
                    <i />
                    <span>{IsTop ? "Drag up to close" : "Drag down to close"}</span>
                </button>
            </div>
            <button
                type="button"
                className={`drawer-handle ${HandleClass}`}
                aria-label={Label}
                aria-expanded={IsOpen}
                style={{ left: `${HandleLeft}px` }}
                onClick={() => {
                    if (Date.now() - LastPointerToggleRef.current > 250)
                        Toggle(!Open);
                }}
                onPointerDown={(e) => BeginDrag(e, true)}
                onPointerMove={MoveDrag}
                onPointerUp={EndDrag}
                onPointerCancel={EndDrag}
            >
                <svg viewBox="0 0 400 36" preserveAspectRatio="none" aria-hidden="true">
                    <path d="M0 0C15 0 20 6 25 15L35 28C40 34 45 36 52 36H348C355 36 360 34 365 28L375 15C380 6 385 0 400 0Z" />
                </svg>
                <span>{Label}</span>
            </button>
        </div>
    );
}

// Top Notch + Control Center Drawer (ProjectZeroEditor Notch.jsx 1:1)
export function FrontierNotch({
    open,
    onToggle,
    displayValues,
    onUpdateDisplay,
    onSelectEntity,
    diagnosticsVisible,
    onToggleDiagnostics,
    onOpenAssets
})
{
    const [page, setPage] = useState("Dashboard");
    const [notifications, setNotifications] = useState(true);

    useEffect(() => {
        if (!open) setPage("Dashboard");
    }, [open]);

    const giBounces = displayValues.giBounces ?? 3;
    const reflOn = displayValues.rayTracedReflections !== false;
    const aaOn = displayValues.temporalAA !== false;
    const rtOn = displayValues.rayTracing !== false;
    const cardsOn = displayValues.surfaceCache !== false;
    const quality = displayValues.qualityGrade || "High";
    const scalePct = Math.round((displayValues.renderScale ?? 1) * 100);

    const cycleQuality = () => {
        const order = ["Low", "Medium", "High", "Ultra"];
        const next = order[(order.indexOf(quality) + 1) % order.length] || "High";
        onUpdateDisplay("display", "qualityGrade", next);
    };

    return (
        <DrawerPanel Edge="top" Open={open} Toggle={onToggle} Label="Control Center" ClassName="control-drawer" DefaultHeight={510}>
            <div className="shade">
                {page === "Dashboard" && (
                    <section className="control-dashboard">
                        <header>
                            <div>Control Center</div>
                            <span>
                                <PzActionIcon name="wireless" size={16} />
                                <button type="button" aria-label="Control Center settings" onClick={() => setPage("Settings")}>
                                    <PzActionIcon name="settings" size={16} />
                                </button>
                            </span>
                        </header>

                        <div className="dashboard-grid">
                            <button
                                type="button"
                                className={giBounces > 0 ? "active" : ""}
                                onClick={() => onUpdateDisplay("display", "giBounces", (giBounces + 1) % 5)}
                            >
                                <i><PzActionIcon name="sun" size={22} /></i>
                                <span>{giBounces === 0 ? "GI: Off" : `GI: ${giBounces} ${giBounces === 1 ? "Bounce" : "Bounces"}`}</span>
                            </button>

                            <button
                                type="button"
                                className={reflOn ? "active" : ""}
                                onClick={() => onUpdateDisplay("display", "rayTracedReflections", !reflOn)}
                            >
                                <i><PzActionIcon name="reflection" size={22} /></i>
                                <span>{`Refl: ${reflOn ? "Raytraced" : "Sky"}`}</span>
                            </button>

                            <button
                                type="button"
                                className={aaOn ? "active" : ""}
                                onClick={() => onUpdateDisplay("display", "temporalAA", !aaOn)}
                            >
                                <i><PzActionIcon name="aa" size={22} /></i>
                                <span>Anti-Aliasing</span>
                            </button>

                            <button
                                type="button"
                                className={diagnosticsVisible ? "active" : ""}
                                onClick={onToggleDiagnostics}
                            >
                                <i><PzActionIcon name="fps" size={22} /></i>
                                <span>FPS Overlay</span>
                            </button>

                            <button
                                type="button"
                                className={notifications ? "active" : ""}
                                onClick={() => setNotifications(!notifications)}
                            >
                                <i><PzActionIcon name="bell" size={22} /></i>
                                <span>Notifications</span>
                            </button>

                            <button
                                type="button"
                                className={quality !== "Low" ? "active" : ""}
                                onClick={cycleQuality}
                            >
                                <i><PzActionIcon name="sliders" size={22} /></i>
                                <span>{quality}</span>
                            </button>

                            <button
                                type="button"
                                className={cardsOn ? "active" : ""}
                                onClick={() => onUpdateDisplay("display", "surfaceCache", !cardsOn)}
                            >
                                <i><PzActionIcon name="patches" size={22} /></i>
                                <span>{`Patches: ${cardsOn ? "On" : "Off"}`}</span>
                            </button>

                            <button
                                type="button"
                                className={rtOn ? "active" : ""}
                                onClick={() => onUpdateDisplay("display", "rayTracing", !rtOn)}
                            >
                                <i><PzActionIcon name="ray" size={22} /></i>
                                <span>Raytracing</span>
                            </button>
                        </div>

                        <div className="render-scale">
                            <PzActionIcon name="monitor" size={18} />
                            <input
                                type="range"
                                min="25"
                                max="100"
                                step="5"
                                aria-label="Render scale"
                                value={scalePct}
                                style={{ "--fill": `${((scalePct - 25) / 75) * 100}%` }}
                                onChange={(e) => onUpdateDisplay("display", "renderScale", Number(e.target.value) / 100)}
                            />
                            <output>{scalePct}%</output>
                        </div>
                    </section>
                )}

                {page === "Settings" && (
                    <section className="settings-hub">
                        <header>
                            <button type="button" onClick={() => setPage("Dashboard")} aria-label="Back"><PzActionIcon name="back" size={16} /></button>
                            <div>
                                <div>Settings</div>
                                <small>Project-Zero C++ Mirror</small>
                            </div>
                            <button type="button" onClick={() => onToggle(false)} aria-label="Close"><PzActionIcon name="close" size={16} /></button>
                        </header>
                        {[
                            ["Render Settings", "sliders", "Global illumination, BVH ReSTIR & internal resolution"],
                            ["Appearance", "palette", "Workspace docking, neutral theme & overlays"],
                            ["Input", "gamepad", "Viewport navigation, camera speed & shortcuts"],
                            ["Notifications", "bell", "Diagnostics card & validation alerts"],
                            ["Materials", "swatch", "Open the bottom Asset Browser material library"]
                        ].map(([title, icon, desc]) => (
                            <button
                                key={title}
                                type="button"
                                className="hub-row"
                                onClick={() => {
                                    if (title === "Materials") {
                                        onToggle(false);
                                        onOpenAssets?.("materials");
                                    } else {
                                        setPage(title);
                                    }
                                }}
                            >
                                <PzActionIcon name={icon} size={20} />
                                <span>{title}<small>{desc}</small></span>
                                <PzActionIcon name="chevron" size={14} />
                            </button>
                        ))}
                    </section>
                )}

                {page !== "Dashboard" && page !== "Settings" && (
                    <section className="settings-dialog">
                        <header>
                            <button type="button" onClick={() => setPage("Settings")} aria-label="Back"><PzActionIcon name="back" size={16} /></button>
                            <div>
                                <h2>{page}</h2>
                                <p>Changes apply immediately to the HTML C++ shadow-box mirror.</p>
                            </div>
                            <button type="button" onClick={() => onToggle(false)} aria-label="Close"><PzActionIcon name="close" size={16} /></button>
                        </header>
                        <div className="settings-body">
                            {page === "Render Settings" && (
                                <>
                                    <label className="cc-setting-row">
                                        <span>Hardware BVH Raytracing</span>
                                        <input type="checkbox" checked={rtOn} onChange={(e) => onUpdateDisplay("display", "rayTracing", e.target.checked)} />
                                    </label>
                                    <label className="cc-setting-row">
                                        <span>Raytraced Specular Reflections</span>
                                        <input type="checkbox" checked={reflOn} onChange={(e) => onUpdateDisplay("display", "rayTracedReflections", e.target.checked)} />
                                    </label>
                                    <label className="cc-setting-row">
                                        <span>Surface Radiance Cache</span>
                                        <input type="checkbox" checked={cardsOn} onChange={(e) => onUpdateDisplay("display", "surfaceCache", e.target.checked)} />
                                    </label>
                                    <button
                                        type="button"
                                        className="cc-setting-row"
                                        style={{ cursor: "pointer", color: "#eee" }}
                                        onClick={() => { onSelectEntity("display"); onToggle(false); }}
                                    >
                                        <span>Open full Display Inspector</span>
                                        <PzActionIcon name="chevron" size={14} />
                                    </button>
                                </>
                            )}
                            {page === "Appearance" && (
                                <div className="cc-setting-row">
                                    <span>ProjectZeroEditor neutral dark palette (#090909 / #121212 / #191919)</span>
                                </div>
                            )}
                            {page === "Input" && (
                                <div className="cc-setting-row">
                                    <span>Shift+A Construct · F Focus Selected · F3 Diagnostics · Drag tabs between docks</span>
                                </div>
                            )}
                            {page === "Notifications" && (
                                <label className="cc-setting-row">
                                    <span>Show Diagnostics Overlay Banner</span>
                                    <input type="checkbox" checked={notifications} onChange={(e) => setNotifications(e.target.checked)} />
                                </label>
                            )}
                        </div>
                    </section>
                )}
            </div>
        </DrawerPanel>
    );
}

const BuiltInAssets = [
    { id: "mat-slate-pbr", name: "Slate Clearcoat", kind: "materials", origin: "Engine", ext: ".fmat", path: "EngineContent/Materials/SlateClearcoat.fmat", color: "#7ba3b8", roughness: 18, metallic: 85, desc: "Metallic clearcoat PBR surface." },
    { id: "mat-alabaster", name: "Studio Alabaster", kind: "materials", origin: "Engine", ext: ".fmat", path: "EngineContent/Materials/StudioAlabaster.fmat", color: "#e5e3dc", roughness: 42, metallic: 0, desc: "Matte cyclorama studio plaster." },
    { id: "mat-copper", name: "Burnished Copper", kind: "materials", origin: "Engine", ext: ".fmat", path: "EngineContent/Materials/BurnishedCopper.fmat", color: "#c87a4b", roughness: 24, metallic: 100, desc: "Conductor PBR copper with warm Fresnel edge." },
    { id: "mat-obsidian", name: "Obsidian Carbon", kind: "materials", origin: "Project", ext: ".fmat", path: "Project/Materials/ObsidianCarbon.fmat", color: "#2c3038", roughness: 14, metallic: 60, desc: "Anisotropic carbon weave coat." },
    { id: "mat-moss", name: "Alpine Moss", kind: "materials", origin: "Project", ext: ".fmat", path: "Project/Materials/AlpineMoss.fmat", color: "#6d9c62", roughness: 78, metallic: 0, desc: "Botanical ground-cover PBR material." },
    { id: "mat-cobalt", name: "Tactical Cobalt", kind: "materials", origin: "Engine", ext: ".fmat", path: "EngineContent/Materials/TacticalCobalt.fmat", color: "#3b6fd4", roughness: 20, metallic: 90, desc: "Deep cobalt automotive flake." },
    { id: "mdl-valley", name: "Evergreen Terrain", kind: "models", origin: "Project", ext: ".fmesh", path: "Project/Worlds/EvergreenTerrain.fmesh", desc: "Heightfield landscape with erosion channels." },
    { id: "mdl-cyclorama", name: "Studio Cyclorama Bay", kind: "models", origin: "Project", ext: ".fmesh", path: "Project/Lobby/CycloramaBay.fmesh", desc: "Enclosed 40 m studio cove without sun/sky." },
    { id: "mdl-shaderball", name: "Calibration Shaderball", kind: "models", origin: "Engine", ext: ".fmesh", path: "EngineContent/Meshes/Shaderball.fmesh", desc: "Dual-shell material calibration sphere." },
    { id: "code-restir", name: "ReservoirSpatial", kind: "code", origin: "Engine", ext: ".hlsl", path: "Engine/Shaders/ReservoirSpatial.hlsl", desc: "Spatial-temporal ReSTIR indirect GI pass." },
    { id: "code-atmo", name: "SkyAtmosphereLUT", kind: "code", origin: "Engine", ext: ".hlsl", path: "Engine/Shaders/SkyAtmosphereLUT.hlsl", desc: "Multiple-scattering Rayleigh/Mie/Ozone LUT." },
    ...Object.entries(ProjectZeroIcons).slice(0, 18).map(([key, uri]) => ({
        id: `icon-${key}`,
        name: key,
        kind: "icons",
        origin: "Engine",
        ext: ".svg",
        path: `EngineContent/Icons/${key}.svg`,
        previewUri: uri,
        desc: "Native Project-Zero SVG vector icon."
    }))
];

// Bottom Asset Browser Drawer (ProjectZeroEditor AssetPanel.jsx 1:1)
export function FrontierAssetDrawer({ open, onToggle, initialCategory = "all", materialValues = {}, onApplyMaterial })
{
    const [customAssets, setCustomAssets] = useState([]);
    const [category, setCategory] = useState(initialCategory);
    const [origin, setOrigin] = useState("All");
    const [search, setSearch] = useState("");
    const [viewMode, setViewMode] = useState("grid");
    const [selectedId, setSelectedId] = useState("mat-slate-pbr");
    const fileInputRef = useRef(null);

    useEffect(() => {
        if (initialCategory) setCategory(initialCategory);
    }, [initialCategory]);

    const allAssets = [...customAssets, ...BuiltInAssets];
    const filtered = allAssets.filter((item) => {
        if (category !== "all" && item.kind !== category) return false;
        if (origin !== "All" && item.origin !== origin) return false;
        if (search && !`${item.name} ${item.path} ${item.ext}`.toLowerCase().includes(search.toLowerCase())) return false;
        return true;
    });
    const selected = filtered.find((a) => a.id === selectedId) || allAssets.find((a) => a.id === selectedId) || filtered[0] || null;

    const categories = [
        ["all", "All"],
        ["materials", "Materials"],
        ["models", "Models"],
        ["icons", "Icons"],
        ["images", "Images"],
        ["fonts", "Fonts"],
        ["code", "Code"],
        ["other", "Other"]
    ];

    const createMaterial = () => {
        const palette = ["#e06c75", "#61afef", "#98c379", "#e5c07b", "#c678dd"];
        const color = palette[customAssets.length % palette.length];
        const id = `custom-mat-${customAssets.length + 1}`;
        setCustomAssets((prev) => [{
            id,
            name: `Custom Material ${prev.length + 1}`,
            kind: "materials",
            origin: "Project",
            ext: ".fmat",
            path: `Project/Materials/CustomMaterial${prev.length + 1}.fmat`,
            color,
            roughness: 25,
            metallic: 50,
            desc: "Authored in Project-Zero Asset Browser."
        }, ...prev]);
        setCategory("materials");
        setSelectedId(id);
    };

    const saveSelectedMaterial = () => {
        const color = materialValues.albedo || "#c5a6d7";
        const id = `saved-mat-${customAssets.length + 1}`;
        setCustomAssets((prev) => [{
            id,
            name: `Saved Standard PBR ${prev.length + 1}`,
            kind: "materials",
            origin: "Project",
            ext: ".fmat",
            path: `Project/Materials/SavedPBR${prev.length + 1}.fmat`,
            color,
            roughness: materialValues.roughness ?? 32,
            metallic: materialValues.metallic ?? 0,
            desc: "Saved from Standard PBR inspector state."
        }, ...prev]);
        setCategory("materials");
        setSelectedId(id);
    };

    const onImportFiles = (e) => {
        const files = Array.from(e.target.files || []);
        if (!files.length) return;
        const imported = files.map((f, idx) => {
            const lower = f.name.toLowerCase();
            const kind = /\.(png|jpg|jpeg|webp|hdr|exr)$/.test(lower) ? "images"
                : /\.(ttf|otf|woff2?)$/.test(lower) ? "fonts"
                : /\.(glb|gltf|obj|fbx)$/.test(lower) ? "models"
                : /\.(svg)$/.test(lower) ? "icons"
                : /\.(hlsl|wgsl|glsl|cpp|h|js)$/.test(lower) ? "code"
                : "other";
            return {
                id: `imported-${Date.now()}-${idx}`,
                name: f.name.replace(/\.[^.]+$/, ""),
                kind,
                origin: "Imported",
                ext: (f.name.match(/\.[^.]+$/) || [""])[0] || ".asset",
                path: `Imported/${f.name}`,
                desc: `Imported browser file (${Math.max(1, Math.round(f.size / 1024))} KB).`
            };
        });
        setCustomAssets((prev) => [...imported, ...prev]);
        setSelectedId(imported[0].id);
        e.target.value = "";
    };

    return (
        <DrawerPanel Edge="bottom" Open={open} Toggle={onToggle} Label="Asset Browser" ClassName="asset-drawer" DefaultHeight={560}>
            <section className="asset-browser">
                <header className="asset-browser-header">
                    <div>
                        <span className="asset-eyebrow">FRONTIER ASSET DEPOT</span>
                        <h1>Asset Browser <small>{allAssets.length} indexed</small></h1>
                    </div>
                    <div className="asset-header-actions">
                        <button type="button" className="primary" onClick={createMaterial}>+ Material</button>
                        <button type="button" onClick={saveSelectedMaterial}>Save selected material</button>
                        <button type="button" onClick={() => fileInputRef.current?.click()}>Import files</button>
                        <input ref={fileInputRef} type="file" multiple hidden onChange={onImportFiles} />
                        <button type="button" className="close-btn" aria-label="Close Asset Browser" onClick={() => onToggle(false)}>×</button>
                    </div>
                </header>

                <div className="asset-browser-tools">
                    <div className="asset-origin-tabs">
                        {["All", "Project", "Imported", "Engine"].map((tab) => (
                            <button key={tab} type="button" aria-pressed={origin === tab} onClick={() => setOrigin(tab)}>
                                {tab}
                            </button>
                        ))}
                    </div>
                    <input
                        type="search"
                        placeholder="Search materials, models, icons, shaders..."
                        value={search}
                        onChange={(e) => setSearch(e.target.value)}
                    />
                    <div className="asset-view-switch">
                        <button type="button" aria-pressed={viewMode === "grid"} onClick={() => setViewMode("grid")} title="Grid view">⊞</button>
                        <button type="button" aria-pressed={viewMode === "list"} onClick={() => setViewMode("list")} title="List view">☰</button>
                    </div>
                </div>

                <div className="asset-browser-body">
                    <nav className="asset-category-rail" aria-label="Asset categories">
                        {categories.map(([key, label]) => {
                            const count = key === "all" ? allAssets.length : allAssets.filter((a) => a.kind === key).length;
                            return (
                                <button key={key} type="button" aria-pressed={category === key} onClick={() => setCategory(key)}>
                                    <span>{label}</span>
                                    <small>{count}</small>
                                </button>
                            );
                        })}
                    </nav>

                    <div className="asset-results">
                        <header>
                            <h2>{(categories.find((c) => c[0] === category) || categories[0])[1]}</h2>
                            <span>{filtered.length} shown</span>
                        </header>
                        <div className={`asset-catalogue ${viewMode}`} data-drawer-scroll="true">
                            {filtered.map((asset) => (
                                <button
                                    key={asset.id}
                                    type="button"
                                    className={`asset-cell ${selected?.id === asset.id ? "selected" : ""}`}
                                    onClick={() => setSelectedId(asset.id)}
                                    onDoubleClick={() => asset.kind === "materials" && onApplyMaterial?.(asset)}
                                >
                                    <span className="asset-thumbnail">
                                        {asset.kind === "materials" ? (
                                            <i className="asset-material-orb" style={{ "--orb": asset.color || "#88aacc" }} />
                                        ) : asset.previewUri ? (
                                            <img src={asset.previewUri} alt="" />
                                        ) : (
                                            <span className="asset-kind-icon">{asset.kind === "models" ? "◈" : asset.kind === "code" ? "{}" : "•"}</span>
                                        )}
                                    </span>
                                    <span className="asset-cell-name">
                                        {asset.name}
                                        <small>{asset.origin} · {asset.ext}</small>
                                    </span>
                                </button>
                            ))}
                        </div>
                    </div>

                    <aside className="asset-preview">
                        {selected && (
                            <>
                                <div className="asset-preview-heading">
                                    <span className="asset-origin-label">{selected.origin}</span>
                                    <small>{selected.ext}</small>
                                </div>
                                <h2>{selected.name}</h2>
                                <p>{selected.desc}</p>
                                <dl>
                                    <dt>Category</dt><dd>{selected.kind}</dd>
                                    <dt>Path</dt><dd>{selected.path}</dd>
                                </dl>
                                {selected.kind === "materials" && (
                                    <div className="asset-preview-actions">
                                        <button type="button" className="primary" onClick={() => onApplyMaterial?.(selected)}>
                                            Apply to Standard PBR
                                        </button>
                                    </div>
                                )}
                            </>
                        )}
                    </aside>
                </div>

                <footer className="asset-browser-footer">
                    <span>EngineContent + Project live asset depot</span>
                    <span>Double-click any material orb to apply to Standard PBR</span>
                </footer>
            </section>
        </DrawerPanel>
    );
}

export function FrontierCheckerViewport({ all, selected, onSelect, isHidden, values, viewIndex = 1, sceneKey = "valley" })
{
    const previewable = all.filter((o) => !o.isContainer && !["display", "debug-channels"].includes(o.id) && (PreviewAnchors[o.id] || PreviewAnchors[o.base] || o.native));

    return (
        <div className="pz-checker-canvas" data-view={viewIndex} data-scene={sceneKey}>
            <svg className="pz-horizon-grid" viewBox="0 0 800 500" preserveAspectRatio="none" aria-hidden="true">
                {sceneKey === "lobby" ? (
                    <>
                        <rect x="60" y="45" width="680" height="410" rx="28" fill="none" stroke="#ffffff12" strokeWidth="1.2" />
                        <ellipse cx="400" cy="325" rx="220" ry="70" fill="none" stroke="#ffffff18" strokeWidth="1.2" />
                        <ellipse cx="400" cy="325" rx="130" ry="40" fill="none" stroke="#ffb45430" strokeWidth="1" strokeDasharray="4 4" />
                    </>
                ) : (
                    <>
                        <line x1="0" y1="265" x2="800" y2="265" stroke="#ffffff14" strokeWidth="1" />
                        {[-3, -2, -1, 0, 1, 2, 3].map((k) => (
                            <line key={k} x1={400 + k * 42} y1="265" x2={400 + k * 190} y2="500" stroke="#ffffff0c" strokeWidth="1" />
                        ))}
                        {[305, 355, 415, 475].map((y) => (
                            <line key={y} x1="0" y1={y} x2="800" y2={y} stroke="#ffffff09" strokeWidth="1" />
                        ))}
                    </>
                )}
            </svg>

            {previewable.map((entity, idx) => {
                const hidden = isHidden(entity.id);
                if (hidden)
                    return null;
                const basePos = PreviewAnchors[entity.id] || PreviewAnchors[entity.base] || [32 + ((idx * 17) % 44), 44 + ((idx * 13) % 30)];
                const shiftX = viewIndex === 2 ? -6 : 0;
                const isSelected = selected === entity.id;
                return (
                    <button
                        key={entity.id}
                        type="button"
                        className={`pz-stage-pin ${isSelected ? "is-selected" : ""}`}
                        style={{ left: `${Math.max(8, Math.min(92, basePos[0] + shiftX))}%`, top: `${basePos[1]}%` }}
                        onClick={() => onSelect(entity.id)}
                        title={`${entity.name} · ${entity.type}`}
                    >
                        <OutlinerIcon object={entity} values={values[entity.id] || {}} size={18} />
                        <span>{entity.name}</span>
                    </button>
                );
            })}
        </div>
    );
}
