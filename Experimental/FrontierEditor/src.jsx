import React, { useState, useEffect, useRef } from "react";
import { createRoot } from "react-dom/client";
import {
    Palette, Circle, Cylinder, Square, Rainbow, ArrowDown, CloudFog, CloudRain, CloudSnow, CloudHail,
    Cloud, Trees, Mountain, Waves, Camera, Box, ArrowUpRight, RotateCcw, MoveUpRight, Wind,
    SlidersHorizontal, Layers, Orbit, Leaf, Sparkles, Droplets, Aperture, ScanLine, Monitor, Bug
} from "lucide-react";
import { Moon, Stars, Sun } from "./celestial-icons.jsx";
import "./style.css";
import "./pz-theme.css";
import ConstructMenu, { nativeWorld, constructItems, NativeEntityInspector } from "./construct-menu.jsx";
import OutlinerIcon from "./outliner-icons.jsx";
import { PzGlyph, PzIcon } from "./project-zero-icons.jsx";
import { CloudControls, FogControls } from "./atmosphere-shared-controls.jsx";
import LocalVolumeInspector, { localVolumeObjects } from "./local-volumes.jsx";
import BakeQuickTiles from "./bake-quick-tiles.jsx";
import MaterialInspector from "./material-inspector.jsx";
import { materialDefaults } from "./material-model.js";
import ObjectInspector, { meshDefaults } from "./object-inspector.jsx";
import { migrateOrganization, organizationNode, SceneTree, OrganizationInspector } from "./organization.jsx";
import { speciesTypes, speciesObjects, speciesDefaults, SpeciesCollection, SpeciesInspector } from "./foliage-inspectors.jsx";
import ExtraInspectors, { extraDefaults, extraSwitches, extraDescriptions } from "./extra-inspectors.jsx";
import { DisplayInspector, DebugChannelsInspector, DisplayChannels, displayDebugDefaults } from "./display-debug-inspectors.jsx";
import { FrontierCheckerViewport, FrontierNotch, FrontierAssetDrawer, PzActionIcon } from "./pz-viewport.jsx";
import { ParticleGauge, FallSpeed, WaterSection, WaveProfile, WaterTransmission, WaterCurrent, ShoreFoam } from "./fluid-graphics.jsx";
import { cameraOptics, formatDistance } from "./optics.js";
import { SensorField, DepthOfField } from "./camera-graphics.jsx";
import { WindFlow, HazeTransmission, PrecipitationField, OzoneAbsorption } from "./weather-graphics.jsx";
import { Iris, TerrainContours, RoughnessProfile, ErosionChannels } from "./property-graphics.jsx";
import { SunGizmo, ScatteringGraph, IlluminanceCurve } from "./environment-graphics.jsx";

// Outdoor Evergreen Valley entities + Project-Zero Cameras collection (Editor Camera, Cine Camera, Post Process) + Display & Debug channels.
const objects = [
    { id: "display", name: "Display", type: "Renderer & display", group: "Render", icon: Monitor, color: "#9ac2b0" },
    { id: "debug-channels", name: "Debug channels", type: "Inspection views", group: "Render", icon: Bug, color: "#d4b475" },
    { id: "editor-camera", base: "camera", name: "Editor Camera", type: "Permanent navigation camera", group: "Cameras", icon: Camera, color: "#9eb8e8", permanent: true },
    { id: "cine-camera", base: "camera", name: "Cine Camera", type: "3:2 optical cinema camera", group: "Cameras", icon: Camera, color: "#bb9ae4" },
    { id: "post-process", name: "Post Process", type: "Exposure, tone mapping & lens grading", group: "Cameras", icon: Aperture, color: "#d6b07a" },
    { id: "sun", name: "Sun", type: "Directional light", group: "Environment", icon: Sun, color: "#e8b65f" },
    { id: "flare", name: "Lens Flare", type: "Sun optical effect", parent: "sun", group: "Environment", icon: Sparkles, color: "#e2b58a" },
    { id: "moon", name: "Moon", type: "Lunar light", group: "Environment", icon: Moon, color: "#b5c4df" },
    { id: "stars", name: "Stars", type: "Celestial field", group: "Environment", icon: Stars, color: "#bea4e1" },
    { id: "sky", name: "Atmosphere", type: "Procedural sky", group: "Environment", icon: Orbit, color: "#99aafa" },
    { id: "rainbow", name: "Rainbow", type: "Atmospheric optical effect", parent: "sky", group: "Environment", icon: Rainbow, color: "#d5a4c4" },
    { id: "cloud", name: "Clouds", type: "Volumetric clouds", group: "Environment", icon: Cloud, color: "#8ebce8" },
    { id: "precip", name: "Precipitation", type: "Cloud subcomponent", parent: "cloud", group: "Environment", icon: Droplets, color: "#75c7df" },
    ...localVolumeObjects,
    { id: "air", name: "Air / Wind", type: "Global wind field", group: "Environment", icon: Wind, color: "#72c8b3" },
    { id: "forest", name: "Forest", type: "Foliage collection", group: "World", icon: Trees, color: "#93c779" },
    ...speciesObjects,
    { id: "terrain", name: "Terrain", type: "Landscape", group: "World", icon: Mountain, color: "#d6a078" },
    { id: "water", name: "Lake", type: "Water surface", group: "Water bodies", icon: Waves, color: "#74bdd4" },
    { id: "ocean", name: "Ocean", type: "Open water body", group: "Water bodies", icon: Waves, color: "#82abc9" },
    { id: "river", name: "River", type: "Flowing water body", group: "Water bodies", icon: MoveUpRight, color: "#87c1ba" },
    { id: "liquid", name: "Liquid Volume", type: "Contained fluid", group: "Water bodies", icon: Box, color: "#98b6d2" },
    { id: "cube", name: "Cube", type: "Mesh object", group: "World", icon: Box, color: "#a7bbd4" },
    { id: "sphere", name: "Sphere", type: "Mesh object", group: "World", icon: Circle, color: "#a8c2cc" },
    { id: "cylinder", name: "Cylinder", type: "Mesh object", group: "World", icon: Cylinder, color: "#b3b3d5" },
    { id: "plane", name: "Plane", type: "Mesh object", group: "World", icon: Square, color: "#a5b8c7" },
    { id: "material", name: "Standard PBR", type: "Surface material", group: "Materials", icon: Palette, color: "#c5a6d7" }
];

// Sunless Lobby · Studio scene: NO Sun & Sky (no sun, flare, moon, stars, sky, rainbow); enclosed studio bay lights + Cameras + meshes + materials.
const lobbyStudioObjects = [
    { id: "display", name: "Display", type: "Renderer & display", group: "Render", icon: Monitor, color: "#9ac2b0" },
    { id: "debug-channels", name: "Debug channels", type: "Inspection views", group: "Render", icon: Bug, color: "#d4b475" },
    { id: "editor-camera", base: "camera", name: "Editor Camera", type: "Permanent navigation camera", group: "Cameras", icon: Camera, color: "#9eb8e8", permanent: true },
    { id: "cine-camera", base: "camera", name: "Cine Camera", type: "3:2 optical cinema camera", group: "Cameras", icon: Camera, color: "#bb9ae4" },
    { id: "post-process", name: "Post Process", type: "Exposure, tone mapping & lens grading", group: "Cameras", icon: Aperture, color: "#d6b07a" },
    { id: "studio-key", name: "Overhead Softbox", type: "24×11 m rectangular area light", group: "Environment", icon: Sparkles, color: "#f0d49a" },
    { id: "studio-rim", name: "Rim Strip Array", type: "Linear studio grazing strips", group: "Environment", icon: Layers, color: "#9cc4f0" },
    { id: "turntable", name: "Hero Turntable", type: "40 m enclosed cyclorama bay", group: "World", icon: Orbit, color: "#b6c2cf" },
    { id: "cube", name: "Cube", type: "Mesh object", group: "World", icon: Box, color: "#a7bbd4" },
    { id: "sphere", name: "Sphere", type: "Mesh object", group: "World", icon: Circle, color: "#a8c2cc" },
    { id: "cylinder", name: "Cylinder", type: "Mesh object", group: "World", icon: Cylinder, color: "#b3b3d5" },
    { id: "plane", name: "Plane", type: "Mesh object", group: "World", icon: Square, color: "#a5b8c7" },
    { id: "material", name: "Standard PBR", type: "Surface material", group: "Materials", icon: Palette, color: "#c5a6d7" }
];

const cardIconColors = {
    "Sun direction": "#e8b65f", Illuminance: "#e8c76a", Temperature: "#e69c79",
    "Daylight cycle": "#d4b970", "Sun disc": "#f0bd72",
    "Atmospheric scattering": "#99aafa", "Aerosol haze": "#d4b583", "Ozone absorption": "#bd97db",
    "Ground fog": "#8fc2c6", "Density falloff": "#8eaddb", "Ground reflectance": "#d8c077",
    "Cloud coverage": "#8ebce8", "Cloud base": "#a1b3e2", "Layer thickness": "#a6a0d9",
    "Wind field": "#72c8b3", Precipitation: "#75c7df", "Particle size": "#96bbe9", "Fall speed": "#79b7d5",
    "Forest distribution": "#93c779", "Species blend": "#b4c875", "Growth variation": "#c2b783", "Wind response": "#72c8b3",
    "Elevation & contours": "#d6a078", "Surface detail": "#cab281", "Erosion channels": "#81b8c8",
    "Snow line": "#a4c6e6", "Rock exposure": "#c29583", "Water level": "#74bdd4", "Surface ripples": "#80c8bd",
    "Water clarity": "#9cbde7", "Water current": "#7dc3bf", "Shoreline foam": "#bfd0ad",
    "Lens & field of view": "#bb9ae4", Aperture: "#d2a0cc", "Focus distance": "#98abe2"
};

const propertySwitches = {
    ...extraSwitches,
    sun: [["Illuminance", "Sunlight", Sun], ["Sun disc", "Sun disc", Sun], ["Daylight cycle", "Day cycle", Orbit]],
    sky: [["Atmospheric scattering", "Scattering", Orbit], ["Aerosol haze", "Aerosols", Sparkles], ["Ozone absorption", "Ozone", Layers], ["Ground fog", "Fog", CloudFog]],
    cloud: [["Cloud coverage", "Cloud layer", Cloud], ["Layer thickness", "Volume", Layers]],
    air: [["Wind field", "Wind field", Wind]],
    precip: [["Precipitation", "Falling particles", Droplets], ["Fall speed", "Falling motion", ArrowDown]],
    forest: [["Forest distribution", "Distribution", Trees]],
    ...Object.fromEntries(Object.keys(speciesTypes).map((key) => [key, [["Instance variation", "Variation", SlidersHorizontal]]])),
    terrain: [["Erosion channels", "Erosion", Droplets], ["Snow line", "Snow cover", Mountain], ["Rock exposure", "Exposed rock", Layers]],
    water: [["Surface ripples", "Ripples", Waves], ["Water clarity", "Transmission", Droplets], ["Water current", "Current", MoveUpRight], ["Shoreline foam", "Foam", Sparkles]],
    camera: [["Aperture", "Depth of field", Aperture]]
};

function readProject()
{
    try { return JSON.parse(localStorage.getItem("frontier-project") || "{}"); }
    catch { return {}; }
}
const stored = readProject();

function App()
{
    const [nativeRows, setNativeRows] = useState([]);
    const [constructInspect, setConstructInspect] = useState(null);

    // ProjectZeroEditor 3-column dockable shell + Top Notch + Bottom Asset Browser + Viewport RunMode state
    const [sceneKey, setSceneKey] = useState(() => {
        if (typeof window !== "undefined" && /(?:lobby=1|level=lobby)/i.test(window.location.search))
            return "lobby";
        return "valley";
    });
    const [runMode, setRunMode] = useState("EDIT"); // "EDIT" | "SIM" | "PLAY"
    const [leftWidth, setLeftWidth] = useState(316);
    const [rightWidth, setRightWidth] = useState(388);
    const [wideWorkspace, setWideWorkspace] = useState(false);
    const [notchOpen, setNotchOpen] = useState(false);
    const [assetOpen, setAssetOpen] = useState(false);
    const [assetCategory, setAssetCategory] = useState("all");
    const [splitView, setSplitView] = useState(false);
    const [diagnosticsOpen, setDiagnosticsOpen] = useState(false);
    const [vpSettingsOpen, setVpSettingsOpen] = useState(false);
    const [visibilityFilter, setVisibilityFilter] = useState("all"); // "all" | "visible" | "hidden"
    const [filterGroup, setFilterGroup] = useState("All");
    const [paneTabs, setPaneTabs] = useState({ Left: ["Outliner"], Centre: ["Viewport"], Right: ["Inspector"] });
    const [activePaneTab, setActivePaneTab] = useState({ Left: "Outliner", Centre: "Viewport", Right: "Inspector" });
    const [tabMenu, setTabMenu] = useState(null);
    const [sceneMenu, setSceneMenu] = useState(null);
    const [filterMenu, setFilterMenu] = useState(null);
    const draggedTabRef = useRef(null);
    const dividerRef = useRef(null);

    const syncNative = (data) => {
        setNativeRows(data.entities.map((entity) => ({
            id: entity.id, base: "native", name: entity.name,
            type: constructItems[entity.kind].name, group: "World",
            icon: entity.camera ? Camera : Box, color: "#a8bbeb", native: entity
        })));
        setOrganization((groups) => {
            const ids = data.entities.map((e) => e.id);
            const next = groups.map((g) => ({ ...g, members: g.members.filter((id) => !id.startsWith("native-") || ids.includes(id)) }));
            const owned = new Set(next.filter((g) => g.kind === "folder").flatMap((g) => g.members));
            const missing = ids.filter((id) => !owned.has(id));
            let folder = next.find((g) => g.id === "folder-native-authoring");
            if (!folder)
            {
                folder = { id: "folder-native-authoring", kind: "folder", name: "Native authoring scene", members: [] };
                next.push(folder);
            }
            folder.members = [...folder.members, ...missing];
            return next;
        });
    };

    useEffect(() => {
        let active = true;
        nativeWorld().then((data) => { if (active) syncNative(data); }).catch(() => {});
        return () => { active = false; };
    }, []);

    const activeSceneObjects = sceneKey === "lobby" ? lobbyStudioObjects : objects;
    const [organization, setOrganization] = useState(() =>
        migrateOrganization(
            stored,
            Array.from(new Map([...objects, ...lobbyStudioObjects].map((o) => [o.id, o])).values())
        )
    );
    const [selected, setSelected] = useState(() => (typeof window !== "undefined" && /(?:lobby=1|level=lobby)/i.test(window.location.search)) ? "editor-camera" : "sun");
    const [query, setQuery] = useState("");
    const [collapsed, setCollapsed] = useState({});
    const [hidden, setHidden] = useState(stored.hidden || {});
    const [values, setValues] = useState(stored.values || {});
    const [add, setAdd] = useState(false);
    const [extras, setExtras] = useState((stored.extras || []).map((o) => ({ ...objects.find((b) => b.id === o.base), ...o })));
    const [saved, setSaved] = useState(true);

    // Resize divider pointer drag (ProjectZeroEditor style)
    useEffect(() => {
        const onMove = (e) => {
            const drag = dividerRef.current;
            if (!drag) return;
            const delta = e.clientX - drag.x;
            if (drag.side === "Left")
                setLeftWidth(Math.max(220, Math.min(520, drag.width + delta)));
            else
                setRightWidth(Math.max(280, Math.min(680, drag.width - delta)));
        };
        const onUp = () => {
            dividerRef.current = null;
            document.body.classList.remove("resizing");
        };
        window.addEventListener("pointermove", onMove);
        window.addEventListener("pointerup", onUp);
        return () => {
            window.removeEventListener("pointermove", onMove);
            window.removeEventListener("pointerup", onUp);
        };
    }, []);

    // Keyboard shortcuts: Shift+A Construct, F3 Diagnostics / Debug channels
    useEffect(() => {
        const shortcut = (e) => {
            if (/INPUT|TEXTAREA|SELECT/.test(e.target.tagName) || e.target.isContentEditable)
                return;
            if (e.shiftKey && e.code === "KeyA")
            {
                e.preventDefault();
                setAdd(true);
            }
            else if (e.key === "F3")
            {
                e.preventDefault();
                setDiagnosticsOpen((v) => !v);
            }
        };
        window.addEventListener("keydown", shortcut);
        return () => window.removeEventListener("keydown", shortcut);
    }, []);

    const existing = [
        ...activeSceneObjects,
        ...(sceneKey === "valley" ? extras : []),
        ...nativeRows
    ];
    const derived = existing
        .filter((o) => !o.parent)
        .flatMap((parent) =>
            activeSceneObjects
                .filter((template) => template.parent === (parent.base || parent.id) && !existing.some((child) => child.parent === parent.id && (child.base || child.id) === template.id))
                .map((template) => ({ ...template, id: parent.id + "-" + template.id, base: template.id, parent: parent.id }))
        );
    const unsorted = [...existing, ...derived];
    const all = [...unsorted.filter((o) => !o.parent).flatMap((o) => [o, ...unsorted.filter((c) => c.parent === o.id)]), ...organization.map(organizationNode)];
    const obj = all.find((o) => o.id === selected) || all[0];
    const kind = obj.base || obj.id;

    const defaults = {
        ...materialDefaults,
        ...meshDefaults,
        ...extraDefaults,
        ...speciesDefaults,
        ...displayDebugDefaults,
        ...(speciesTypes[kind]?.defaults || {}),
        intensity: 110, temperature: 5600, azimuth: 135, elevation: 38,
        rainRate: 6, dropSize: 2, precipType: "Rain", fallSpeed: 6,
        currentSpeed: 0.4, currentBearing: 120, foam: 35, discSize: 0.53,
        fog: 18, falloff: 1.6, albedo: 30, snowline: 120, slope: 42,
        rayleigh: 1.2, ozone: 65, bearing: 225, gusts: 35,
        cloudBase: 1.8, thickness: 2.4, time: 16.5, density: 68,
        scale: 1.2, seed: 428, wind: 2.4, biodiversity: 65,
        haze: 25, clouds: 35, altitude: 180, roughness: 62, erosion: 40,
        waterLevel: 12, ripple: 35, clarity: 78,
        focal: obj.id === "editor-camera" ? 28 : 50,
        aperture: obj.id === "editor-camera" ? 5.6 : 2.8,
        focus: obj.id === "editor-camera" ? 18 : 12,
        navSpeed: 12, sensorWidth: 36,
        evComp: 0, saturation: 1, contrast: 1, bloom: 0.25, vignette: 0.15,
        softboxEv: 11.5, softboxTemp: 5600, rimIntensity: 8.5, turntableRpm: 1.5
    };

    const v = (key) => values[obj.id]?.[key] ?? defaults[key];
    const set = (key, val) => {
        setValues((s) => ({ ...s, [obj.id]: { ...s[obj.id], [key]: val } }));
        setSaved(false);
    };
    const setEntityValue = (entityId, key, val) => {
        setValues((s) => ({ ...s, [entityId]: { ...s[entityId], [key]: val } }));
        setSaved(false);
    };
    const reset = () => {
        setValues((s) => ({ ...s, [obj.id]: {} }));
        setSaved(false);
    };
    const isHidden = (id, seen = new Set()) => {
        if (seen.has(id)) return false;
        seen.add(id);
        const item = all.find((o) => o.id === id);
        return !!(hidden[id] || (item?.parent && isHidden(item.parent, new Set(seen))) || organization.some((g) => g.members.includes(id) && isHidden(g.id, new Set(seen))));
    };
    const renameOrganization = (name) => {
        setOrganization((groups) => groups.map((g) => (g.id === obj.id ? { ...g, name } : g)));
        setSaved(false);
    };
    const updateMember = (id, adding) => {
        setOrganization((groups) =>
            groups.map((g) => {
                if (g.id === obj.id)
                    return { ...g, members: adding ? [...new Set([...g.members, id])] : g.members.filter((m) => m !== id) };
                if (adding && obj.base === "folder" && g.kind === "folder")
                    return { ...g, members: g.members.filter((m) => m !== id) };
                return g;
            })
        );
        setSaved(false);
    };
    const addOrganization = (orgKind) => {
        const id = orgKind + "-" + Date.now();
        const item = { id, kind: orgKind, name: (orgKind === "folder" ? "Folder" : "Collection") + " " + (organization.filter((o) => o.kind === orgKind).length + 1), members: [] };
        setOrganization((g) => [...g, item]);
        setSelected(id);
        setAdd(false);
        setSaved(false);
    };

    const forestSpecies = all.filter((o) => o.parent === obj.id && speciesTypes[o.base || o.id]);
    const toggleHidden = (id) => {
        setHidden((s) => ({ ...s, [id]: !s[id] }));
        setSaved(false);
    };

    const num = (key, unit = "", digits = 0) => (
        <>
            <span>{Number(v(key)).toLocaleString("en-US", { maximumFractionDigits: digits })}</span>
            <small>{unit}</small>
        </>
    );
    const slider = (key, min, max, step = 1) => {
        const cur = Number(v(key));
        const pct = Math.max(0, Math.min(100, ((cur - min) / Math.max(1e-6, max - min)) * 100));
        return (
            <input
                aria-label={key}
                type="range"
                min={min}
                max={max}
                step={step}
                value={cur}
                onChange={(e) => set(key, +e.target.value)}
                style={{ "--fill": `${pct}%`, "--progress": `${pct}%` }}
            />
        );
    };

    const optics = cameraOptics(v("focal"), v("aperture"), v("focus"));
    const featureOn = (title) => values[obj.id]?.["feature:" + title] !== false;

    // ProjectZeroEditor Lighting-styled card (.lp-card + neutral graphite surface)
    const card = (title, CardIcon, body, cls = "") => (
        <section
            className={`card lp-card ${cls} ${!featureOn(title) ? "feature-disabled" : ""}`}
            style={{ "--card-icon": cardIconColors[title] || obj.color }}
        >
            <div className="card-heading">
                <span>
                    {CardIcon && <CardIcon size={15} />}
                    {title}
                </span>
                {!featureOn(title) && <span className="card-off-label">OFF</span>}
            </div>
            <fieldset className="card-body" disabled={!featureOn(title)} inert={!featureOn(title) ? "" : undefined}>
                {body}
            </fieldset>
        </section>
    );

    const saveProject = () => {
        try
        {
            localStorage.setItem("frontier-project", JSON.stringify({
                values, hidden, organization, organizationVersion: 2,
                extras: extras.map(({ icon, ...o }) => o)
            }));
            setSaved(true);
        }
        catch
        {
            setSaved(false);
            window.alert("Unable to save locally. Remove a baked image or free browser storage, then try again.");
        }
    };

    const moveTabToSide = (tabName, targetSide) => {
        setPaneTabs((prev) => {
            const cleaned = {
                Left: prev.Left.filter((t) => t !== tabName),
                Centre: prev.Centre.filter((t) => t !== tabName),
                Right: prev.Right.filter((t) => t !== tabName)
            };
            cleaned[targetSide] = [...cleaned[targetSide], tabName];
            return cleaned;
        });
        setActivePaneTab((prev) => ({ ...prev, [targetSide]: tabName }));
        setTabMenu(null);
    };

    const displayState = { ...displayDebugDefaults, ...(values.display || {}), ...(values["debug-channels"] || {}) };
    const activeDebugIndex = Number(displayState.debugChannel ?? 0);
    const activeDebugEntry = DisplayChannels.find((c) => c.id === activeDebugIndex) || DisplayChannels[0];

    const inspectorSubtitle =
        (obj.native ? "Native authoring record · read-only inspector" : null) ||
        (kind === "display" ? "Renderer, resolution, exposure & SVGF quality" : null) ||
        (kind === "debug-channels" ? "Inspection channels & BVH / SDF diagnostics" : null) ||
        (kind === "material" ? "Surface channels & studio sphere" : null) ||
        (["cube", "sphere", "cylinder", "plane"].includes(kind) ? "Geometry, position & shared material" : null) ||
        (obj.isContainer ? "Organization, membership & visibility" : null) ||
        (["local-cloud", "local-fog"].includes(kind) ? "Local volume · shared effects, bounds & position" : null) ||
        extraDescriptions[kind] ||
        (speciesTypes[kind] && "Forest / Species · generator asset & placement") ||
        (kind === "sun" ? "Light, direction & atmosphere"
            : kind === "forest" ? "Collection distribution & species layers"
            : kind === "sky" ? "Molecules, aerosols & absorption"
            : kind === "cloud" ? "Coverage, altitude & structure"
            : kind === "air" ? "Direction, speed & gusts"
            : kind === "precip" ? "Clouds / Precipitation · type, intensity & motion"
            : kind === "terrain" ? "Shape, surface & erosion"
            : kind === "water" ? "Surface, movement & depth"
            : "Lens, focus & framing");

    const inspectorBody = (
        <section className="inspector pz-inspector-pane" style={{ "--accent": obj.color }}>
            <header className="inspector-top">
                <div className="pz-crumbs">
                    <span>Inspector</span>
                    <PzGlyph name="chevronRight" size={12} />
                    <span>{obj.group}</span>
                </div>
                <button type="button" className={`save-status ${saved ? "saved" : ""}`} onClick={saveProject}>
                    {saved ? <PzGlyph name="check" size={12} /> : <span className="unsaved-dot" />}
                    {saved ? "Saved" : "Save"}
                </button>
            </header>

            <div className="inspector-content">
                <div className="object-header pz-subject-hero">
                    <div className="object-title">
                        <OutlinerIcon object={obj} values={values[obj.id] || {}} size={28} />
                        <div>
                            <div className="workspace-eyebrow">{obj.type.toUpperCase()}</div>
                            <h1>{obj.name}</h1>
                        </div>
                    </div>
                    <div className="object-actions">
                        {!obj.isContainer && !obj.native && (
                            <button type="button" className="reset" title="Reset object properties" onClick={reset}>
                                <RotateCcw size={13} /> Reset
                            </button>
                        )}
                        {!["display", "debug-channels"].includes(kind) && !obj.permanent && (
                            <button
                                type="button"
                                className={`enabled-pill ${isHidden(obj.id) ? "disabled" : ""}`}
                                title={isHidden(obj.id) && !hidden[obj.id] ? "Enable the containing folder, collection, or parent first" : undefined}
                                disabled={!!obj.native || (isHidden(obj.id) && !hidden[obj.id])}
                                onClick={() => toggleHidden(obj.id)}
                            >
                                <span />
                                {obj.native ? "Native record" : isHidden(obj.id) ? "Disabled" : "Enabled"}
                            </button>
                        )}
                    </div>
                </div>

                <div className="section-label">
                    <span>{obj.isContainer ? "COLLECTION / FOLDER" : kind === "display" || kind === "debug-channels" ? "RENDER PIPELINE" : "LIGHTING & PROPERTIES"}</span>
                    <span>{inspectorSubtitle}</span>
                </div>

                <div className="property-switches" role="group" aria-label="Property switches">
                    {(kind === "local-fog"
                        ? propertySwitches.sky.filter(([title]) => title === "Ground fog")
                        : propertySwitches[kind === "local-cloud" ? "cloud" : kind] || []
                    ).map(([title, label, ToggleIcon]) => (
                        <button
                            key={title}
                            type="button"
                            className={`property-switch ${featureOn(title) ? "is-on" : "is-off"}`}
                            aria-label={`Toggle ${label}`}
                            aria-pressed={featureOn(title)}
                            onClick={() => set("feature:" + title, !featureOn(title))}
                        >
                            <span className="switch-icon"><ToggleIcon size={16} strokeWidth={1.7} /></span>
                            <span className="switch-name">{label}</span>
                            <span className="switch-state">{featureOn(title) ? "ON" : "OFF"}</span>
                        </button>
                    ))}
                </div>

                {kind === "display" && <DisplayInspector v={v} set={set} slider={slider} />}
                {kind === "debug-channels" && <DebugChannelsInspector v={v} set={set} slider={slider} />}

                {obj.isContainer && (
                    <OrganizationInspector
                        key={obj.id}
                        node={obj}
                        all={all}
                        card={card}
                        onRename={renameOrganization}
                        onMember={updateMember}
                        onSelect={setSelected}
                        isHidden={isHidden}
                        toggleHidden={toggleHidden}
                    />
                )}

                {!obj.isContainer && !["display", "debug-channels"].includes(kind) && (
                    <div className={`cards lighting-panel ${kind}`}>
                        {obj.native && <NativeEntityInspector entity={obj.native} />}
                        {kind === "material" && <MaterialInspector key={obj.id} card={card} v={v} set={set} values={values[obj.id]} />}
                        {["cube", "sphere", "cylinder", "plane"].includes(kind) && (
                            <ObjectInspector
                                shape={kind}
                                card={card}
                                v={v}
                                set={set}
                                slider={slider}
                                materials={all.filter((o) => (o.base || o.id) === "material")}
                                onSelect={setSelected}
                                values={values}
                            />
                        )}
                        <SpeciesInspector kind={kind} card={card} v={v} set={set} slider={slider} num={num} disabled={isHidden(obj.id)} />
                        <ExtraInspectors kind={kind} card={card} v={v} set={set} slider={slider} num={num} featureOn={featureOn} active={!isHidden(obj.id)} />

                        {["sun", "sky"].includes(kind) && (
                            <div className="effect-child-links">
                                {all.filter((child) => child.parent === obj.id).map((child) => (
                                    <button key={child.id} type="button" className="cloud-child-link" onClick={() => setSelected(child.id)}>
                                        <span>
                                            <OutlinerIcon object={child} size={18} />
                                            <span>{child.name}<small>Optical effect subcomponent</small></span>
                                        </span>
                                        <PzGlyph name="chevronRight" size={14} />
                                    </button>
                                ))}
                            </div>
                        )}

                        {kind === "sun" && <>
                            {card("Sun direction", Orbit, <>
                                <div className="direction-top">
                                    <div className="metric">{num("elevation", "°")}</div>
                                    <span className="metric-caption">{v("elevation") < 0 ? "Below the horizon" : "Above the horizon"}</span>
                                </div>
                                <SunGizmo azimuth={v("azimuth")} elevation={v("elevation")} onChange={(a, e) => { set("azimuth", a); set("elevation", e); }} />
                                <div className="direction-controls">
                                    <label><span>Azimuth <MoveUpRight size={12} /></span><div>{v("azimuth")}<small>°</small></div>{slider("azimuth", 0, 360)}</label>
                                    <label><span>Elevation <ArrowUpRight size={12} /></span><div>{v("elevation")}<small>°</small></div>{slider("elevation", -90, 90)}</label>
                                </div>
                            </>, "direction-card")}
                            {card("Illuminance", Sun, <>
                                <div className="metric">{num("intensity", "klux")}</div>
                                <p className="muted">Direct sunlight intensity</p>
                                <IlluminanceCurve intensity={v("intensity")} />
                                {slider("intensity", 0, 150)}
                                <div className="range-labels"><span>0 klux</span><span>150 klux</span></div>
                            </>, "intensity-card")}
                            {card("Temperature", Sparkles, <>
                                <div className="metric">{num("temperature", "K")}</div>
                                <div className="temperature-track">{slider("temperature", 2000, 10000, 100)}</div>
                                <div className="range-labels"><span>Warm</span><span>Cool</span></div>
                                <div className="temperature-footer">
                                    <span className="color-chip" style={{ background: v("temperature") < 4500 ? "#efd0a3" : v("temperature") > 7000 ? "#b9d4f3" : "#efe6d3" }} />
                                    <span>{v("temperature") < 4500 ? "Golden warmth" : v("temperature") > 7000 ? "Cool daylight" : "Natural daylight"}</span>
                                </div>
                            </>, "temperature-card")}
                            {card("Daylight cycle", Sun, <>
                                <div className="cycle-title">
                                    <div className="metric time">{String(Math.floor(v("time"))).padStart(2, "0")}<span>:{v("time") % 1 >= 0.5 ? "30" : "00"}</span></div>
                                    <span className="small-pill">{v("time") < 6 || v("time") >= 18 ? "Night" : v("time") < 12 ? "Morning" : v("time") < 17 ? "Afternoon" : "Evening"}</span>
                                </div>
                                <DayCurve time={v("time")} />
                                {slider("time", 0, 24, 0.5)}
                                <div className="range-labels"><span>00:00</span><span>06:00</span><span>12:00</span><span>18:00</span><span>24:00</span></div>
                            </>, "cycle-card")}
                            {card("Sun disc", Sun, <>
                                <div className="disc-layout">
                                    <div><div className="metric">{num("discSize", "°", 2)}</div><p className="muted">Angular diameter</p></div>
                                    <div className="solar-disc"><i style={{ width: 18 + v("discSize") * 30, height: 18 + v("discSize") * 30 }} /><span /></div>
                                </div>
                                {slider("discSize", 0.1, 2, 0.01)}
                                <div className="range-labels"><span>Pinpoint</span><span>Broad disc</span></div>
                                <div className="disc-note"><span className="color-chip" />Apparent size of the sun in the sky</div>
                            </>, "disc-card")}
                        </>}

                        {kind === "forest" && <>
                            {card("Forest distribution", Trees, <>
                                <div className="metric">{num("density", "%")}</div>
                                <p className="muted">Collection coverage · weighted species distribution</p>
                                <div className="forest-map">
                                    {Array.from({ length: 65 }, (_, i) => {
                                        const species = forestSpecies.filter((c) => !isHidden(c.id));
                                        const weights = species.map((c) => values[c.id]?.speciesWeight ?? speciesTypes[c.base || c.id].defaults.speciesWeight);
                                        const total = weights.reduce((a, b) => a + b, 0);
                                        if (!total) return null;
                                        let pick = (((i * 37) % 65) / 65) * total, index = 0;
                                        while (index < weights.length - 1 && pick >= weights[index]) { pick -= weights[index]; index++; }
                                        const item = species[index], PlantIcon = item.icon;
                                        return <PlantIcon key={i} size={13 + ((i * 7) % 15)} style={{ left: `${5 + ((i * 29 + v("seed") * 7) % 88)}%`, top: `${8 + ((i * 43 + v("seed") * 3) % 78)}%`, color: item.color, opacity: i < v("density") * 0.65 ? 0.3 + (i % 4) * 0.16 : 0.04 }} />;
                                    })}
                                </div>
                                {slider("density", 0, 100)}
                                <div className="range-labels"><span>Sparse planting</span><span>Dense planting</span></div>
                                <div className="control-line seed"><span>Distribution seed</span><button type="button" onClick={() => set("seed", Math.floor(Math.random() * 9999))}>{v("seed")}<RotateCcw size={12} /></button></div>
                            </>, "wide-card forest-distribution")}
                            {card("Species collection", Leaf, (
                                <SpeciesCollection
                                    children={forestSpecies}
                                    values={values}
                                    isHidden={isHidden}
                                    onSelect={setSelected}
                                    onAdd={(base) => {
                                        const template = speciesObjects.find((o) => o.id === base);
                                        const count = forestSpecies.filter((o) => (o.base || o.id) === base).length + 1;
                                        const item = { ...template, base, id: base + "-" + Date.now(), parent: obj.id, name: template.name + " " + count };
                                        setExtras((a) => [...a, item]);
                                        setSelected(item.id);
                                        setSaved(false);
                                    }}
                                />
                            ), "wide-card")}
                        </>}

                        {kind === "sky" && <>
                            {card("Atmospheric scattering", Orbit, <>
                                <div className="scattering-top">
                                    <div><div className="eyebrow">RAYLEIGH STRENGTH</div><div className="metric">{num("rayleigh", "×", 1)}</div><p className="muted">How air molecules scatter sunlight</p></div>
                                    <div className="scattering-legend"><span><i />Molecules</span><span><i />Aerosols</span></div>
                                </div>
                                <ScatteringGraph rayleigh={v("rayleigh")} haze={featureOn("Aerosol haze") ? v("haze") : 0} />
                                <div className="scattering-bottom"><span>Short wavelengths scatter more strongly</span><span>λ⁻⁴</span></div>
                                {slider("rayleigh", 0, 3, 0.1)}
                                <div className="range-labels"><span>Subtle blue</span><span>Deep blue</span></div>
                            </>, "wide-card scattering-card")}
                            {card("Aerosol haze", Sparkles, <>
                                <div className="scattering-top"><div className="metric">{num("haze", "%")}</div><span className="small-pill">Mie scattering</span></div>
                                <p className="muted">Suspended particles soften and attenuate light</p>
                                <HazeTransmission haze={featureOn("Aerosol haze") ? v("haze") : 0} />
                                {slider("haze", 0, 100)}
                                <div className="range-labels"><span>Clear air</span><span>Dense haze</span></div>
                            </>)}
                            {card("Ozone absorption", Layers, <>
                                <div className="metric">{num("ozone", "%")}</div>
                                <p className="muted">Selective absorption across visible wavelengths</p>
                                <OzoneAbsorption amount={v("ozone")} />
                                {slider("ozone", 0, 100)}
                                <div className="range-labels"><span>Neutral</span><span>Rich twilight</span></div>
                            </>)}
                            <FogControls card={card} v={v} slider={slider} num={num} />
                            {card("Density falloff", MoveUpRight, <>
                                <div className="metric">{num("falloff", "km", 1)}</div>
                                <p className="muted">Atmospheric density scale height</p>
                                <div className="falloff-chart">{Array.from({ length: 10 }, (_, i) => <div key={i}><span>{9 - i}</span><i style={{ width: `${8 + Math.exp(-(9 - i) / v("falloff")) * 85}%` }} /></div>)}</div>
                                {slider("falloff", 0.5, 8, 0.1)}
                                <div className="range-labels"><span>Low layer</span><span>High atmosphere</span></div>
                            </>)}
                            {card("Ground reflectance", Sun, <>
                                <div className="reflectance-content">
                                    <div><div className="metric">{num("albedo", "%")}</div><p className="muted">Light scattered back into the atmosphere</p></div>
                                    <div className="albedo-swatch" style={{ background: `rgb(${40 + v("albedo") * 1.9},${40 + v("albedo") * 1.9},${40 + v("albedo") * 1.9})` }}><ArrowUpRight size={24} /></div>
                                </div>
                                {slider("albedo", 0, 100)}
                                <div className="range-labels"><span>Dark ground</span><span>Reflective surface</span></div>
                            </>, "wide-card")}
                        </>}

                        {kind === "local-fog" && <FogControls card={card} v={v} slider={slider} num={num} />}
                        {["cloud", "local-cloud"].includes(kind) && <>
                            <CloudControls card={card} v={v} set={set} slider={slider} num={num} />
                            {kind === "cloud" && (
                                <button type="button" className="cloud-child-link" onClick={() => setSelected(all.find((o) => o.parent === obj.id)?.id || "precip")}>
                                    <span><PzIcon name="outliner-precipitation" size={16} /><span>Precipitation<small>Cloud subcomponent</small></span></span>
                                    <PzGlyph name="chevronRight" size={14} />
                                </button>
                            )}
                        </>}
                        <LocalVolumeInspector key={"local-volume:" + obj.id} kind={kind} card={card} v={v} set={set} />

                        {kind === "air" && <>
                            {card("Wind field", Wind, <>
                                <div className="scattering-top">
                                    <div><div className="metric">{num("wind", "m/s", 1)}</div><p className="muted">{v("wind") === 0 ? "Calm" : v("wind") < 4 ? "Light breeze" : v("wind") < 12 ? "Moderate wind" : "Strong wind"} · sustained speed</p></div>
                                    <span className="small-pill">{v("bearing")}° · {["N", "NE", "E", "SE", "S", "SW", "W", "NW"][Math.round(v("bearing") / 45) % 8]}</span>
                                </div>
                                <WindFlow speed={v("wind")} bearing={v("bearing")} gusts={v("gusts")} active={!isHidden(obj.id) && featureOn("Wind field")} onChange={(bearing, speed) => { set("bearing", bearing); set("wind", speed); }} />
                                <div className="wind-controls">
                                    <label><span>Speed <strong>{v("wind")} <small>m/s</small></strong></span>{slider("wind", 0, 30, 0.1)}<em>Calm to strong</em></label>
                                    <label><span>Travel direction <strong>{v("bearing")}°</strong></span>{slider("bearing", 0, 360)}<em>Clockwise from north</em></label>
                                    <label><span>Gust variation <strong>{v("gusts")}<small>%</small></strong></span>{slider("gusts", 0, 100)}<em>Steady to turbulent</em></label>
                                </div>
                            </>, "wide-card wind-field-card")}
                        </>}

                        {kind === "precip" && <>
                            {card("Precipitation", Droplets, <>
                                <div className="scattering-top">
                                    <div><div className="metric">{num("rainRate", "mm/h", 1)}</div><p className="muted">{v("precipType") === "Rain" ? "Rainfall intensity" : `Liquid-equivalent ${v("precipType") === "Hail" ? "hail" : "snowfall"} rate`}</p></div>
                                    <div className="weather-segments" role="group" aria-label="Precipitation type">
                                        {[{ type: "Rain", icon: CloudRain }, { type: "Snow", icon: CloudSnow }, { type: "Hail", icon: CloudHail }].map(({ type, icon: ModeIcon }) => (
                                            <button key={type} type="button" aria-pressed={v("precipType") === type} onClick={() => { set("precipType", type); set("fallSpeed", type === "Snow" ? 1.2 : type === "Hail" ? 12 : 6); set("dropSize", Math.min(v("dropSize"), type === "Rain" ? 6 : type === "Snow" ? 12 : 30)); }}>
                                                <ModeIcon size={14} />{type}
                                            </button>
                                        ))}
                                    </div>
                                </div>
                                <PrecipitationField rate={v("rainRate")} type={v("precipType")} size={v("dropSize")} fallSpeed={v("fallSpeed")} moving={featureOn("Fall speed")} active={!isHidden(obj.id) && featureOn("Precipitation")} />
                                {isHidden(obj.id) && <p className="weather-disabled">{hidden[obj.parent] ? "Clouds are disabled. Enable the parent to resume precipitation." : "Precipitation is disabled."}</p>}
                                {slider("rainRate", 0, 30, 0.5)}
                                <div className="range-labels"><span>None</span><span>Heavy precipitation</span></div>
                            </>, "wide-card")}
                            {card("Particle size", Droplets, <>
                                <div className="metric">{num("dropSize", "mm", 1)}</div>
                                <p className="muted">{v("precipType") === "Snow" ? "Snowflake diameter" : v("precipType") === "Hail" ? "Hailstone diameter" : "Droplet diameter"}</p>
                                <ParticleGauge type={v("precipType")} size={v("dropSize")} />
                                {slider("dropSize", 0.5, v("precipType") === "Hail" ? 30 : v("precipType") === "Snow" ? 12 : 6, 0.1)}
                                <div className="range-labels"><span>Fine particles</span><span>Large particles</span></div>
                            </>)}
                            {card("Fall speed", ArrowDown, <>
                                <div className="metric">{num("fallSpeed", "m/s", 1)}</div>
                                <p className="muted">Vertical particle velocity</p>
                                <FallSpeed speed={v("fallSpeed")} type={v("precipType")} />
                                {slider("fallSpeed", 0.2, 30, 0.1)}
                                <div className="range-labels"><span>Slow descent</span><span>Fast fall</span></div>
                                <div className="fall-speed-note">10 m descent <strong>{(10 / v("fallSpeed")).toFixed(1)} s</strong></div>
                            </>)}
                        </>}

                        {kind === "terrain" && <>
                            {card("Elevation & contours", Mountain, <>
                                <div className="scattering-top"><div><div className="metric">{num("altitude", "m")}</div><p className="muted">Peak elevation above the base plane</p></div><span className="small-pill">Topography</span></div>
                                <TerrainContours altitude={v("altitude")} roughness={v("roughness")} erosion={v("erosion")} />
                                {slider("altitude", 0, 500)}
                                <div className="range-labels"><span>Lowlands</span><span>Alpine</span></div>
                            </>, "wide-card")}
                            {card("Surface detail", SlidersHorizontal, <>
                                <div className="metric">{num("roughness", "%")}</div>
                                <p className="muted">Fine-scale ridges &amp; surface variation</p>
                                <RoughnessProfile roughness={v("roughness")} />
                                {slider("roughness", 0, 100)}
                                <div className="range-labels"><span>Rolling</span><span>Jagged</span></div>
                            </>)}
                            {card("Erosion channels", Droplets, <>
                                <div className="metric">{num("erosion", "%")}</div>
                                <p className="muted">Runoff carves branching drainage paths</p>
                                <ErosionChannels erosion={v("erosion")} />
                                {slider("erosion", 0, 100)}
                                <div className="range-labels"><span>Untouched</span><span>Weathered</span></div>
                            </>)}
                            {card("Snow line", Mountain, <>
                                <div className="metric">{num("snowline", "m")}</div>
                                <p className="muted">Snow begins above this elevation</p>
                                <div className="snowline-diagram"><div style={{ background: `linear-gradient(#dedddd 0%,#dedddd ${Math.max(0, 100 - v("snowline") / 5)}%,#665f54 ${Math.max(0, 100 - v("snowline") / 5)}%,#393732 100%)` }} /><i style={{ bottom: `${v("snowline") / 5}%` }} /></div>
                                {slider("snowline", 0, 500)}
                                <div className="range-labels"><span>Snow-covered</span><span>Bare peaks</span></div>
                            </>)}
                            {card("Rock exposure", Layers, <>
                                <div className="metric">{num("slope", "°")}</div>
                                <p className="muted">Slope threshold for exposed rock</p>
                                <div className="slope-diagram"><div style={{ transform: `rotate(-${v("slope")}deg)` }} /><span /><i /></div>
                                {slider("slope", 0, 90)}
                                <div className="range-labels"><span>Gentle slopes</span><span>Steep faces</span></div>
                            </>)}
                        </>}

                        {kind === "water" && <>
                            {card("Water level", Waves, <>
                                <div className="scattering-top"><div><div className="metric">{num("waterLevel", "m", 1)}</div><p className="muted">Surface elevation above the scene datum</p></div><span className="small-pill">Lake basin</span></div>
                                <WaterSection level={v("waterLevel")} onChange={(height) => set("waterLevel", height)} />
                                {slider("waterLevel", 0, 25, 0.5)}
                                <div className="range-labels"><span>0 m · datum</span><span>25 m</span></div>
                            </>, "wide-card")}
                            {card("Surface ripples", Waves, <>
                                <div className="metric">{num("ripple", "%")}</div>
                                <p className="muted">Small-wave amplitude</p>
                                <WaveProfile amount={v("ripple")} active={featureOn("Surface ripples") && !isHidden(obj.id)} />
                                {slider("ripple", 0, 100)}
                                <div className="range-labels"><span>Still surface</span><span>Choppy</span></div>
                            </>)}
                            {card("Water clarity", Droplets, <>
                                <div className="metric">{num("clarity", "%")}</div>
                                <p className="muted">Light attenuation through water</p>
                                <WaterTransmission clarity={v("clarity")} />
                                {slider("clarity", 0, 100)}
                                <div className="range-labels"><span>Turbid</span><span>Clear</span></div>
                                <div className="water-detail">Light at 5 m <strong>{Math.round(Math.exp(-(0.02 + (1 - v("clarity") / 100) * 0.78) * 5) * 100)}%</strong></div>
                            </>)}
                            {card("Water current", MoveUpRight, <>
                                <div className="scattering-top"><div className="metric">{num("currentSpeed", "m/s", 1)}</div><span className="small-pill">{v("currentBearing")}°</span></div>
                                <p className="muted">Surface flow · clockwise from north</p>
                                <WaterCurrent speed={featureOn("Water current") && !isHidden(obj.id) ? v("currentSpeed") : 0} bearing={v("currentBearing")} />
                                <div className="control-line"><span>Speed</span><span>{v("currentSpeed")} m/s</span></div>
                                {slider("currentSpeed", 0, 3, 0.1)}
                                <div className="control-line current-bearing"><span>Direction</span><span>{v("currentBearing")}°</span></div>
                                {slider("currentBearing", 0, 360)}
                            </>)}
                            {card("Shoreline foam", Sparkles, <>
                                <div className="metric">{num("foam", "%")}</div>
                                <p className="muted">Foam coverage at the water’s edge</p>
                                <ShoreFoam amount={v("foam")} />
                                {slider("foam", 0, 100)}
                                <div className="range-labels"><span>Clean edge</span><span>Foamy shore</span></div>
                            </>)}
                        </>}

                        {kind === "camera" && <>
                            {obj.id === "editor-camera" && card("Navigation & Sensor", Camera, <>
                                <div className="scattering-top">
                                    <div><div className="metric">{num("navSpeed", "m/s", 1)}</div><p className="muted">Permanent editor viewport navigation speed</p></div>
                                    <span className="small-pill">Always Active</span>
                                </div>
                                {slider("navSpeed", 1, 60, 0.5)}
                                <div className="range-labels"><span>Precision · 1 m/s</span><span>Flyover · 60 m/s</span></div>
                            </>)}
                            {card("Lens & field of view", Camera, <>
                                <div className="scattering-top"><div><div className="metric">{num("focal", "mm")}</div><p className="muted">{obj.id === "cine-camera" ? "3:2 cinema sensor · optical framing" : "Focal length · full-frame sensor"}</p></div><span className="small-pill">{optics.horizontalFov.toFixed(1)}° horizontal</span></div>
                                <SensorField focal={v("focal")} aperture={v("aperture")} focus={v("focus")} optics={optics} />
                                {slider("focal", 16, 120)}
                                <div className="range-labels"><span>Wide angle · 16 mm</span><span>Telephoto · 120 mm</span></div>
                                <div className="camera-linked-stats">
                                    <div><span>Vertical FOV</span><strong>{optics.verticalFov.toFixed(1)}°</strong></div>
                                    <div><span>Frame at focus</span><strong>{optics.frameWidth.toFixed(1)} × {optics.frameHeight.toFixed(1)}<small> m</small></strong></div>
                                    <div><span>Entrance pupil</span><strong>{optics.pupil.toFixed(1)}<small> mm</small></strong></div>
                                </div>
                            </>, "wide-card sensor-card")}
                            {card("Aperture", Aperture, <>
                                <div className="metric"><small>ƒ / </small>{num("aperture", "", 1)}</div>
                                <p className="muted">Controls light intake &amp; depth of field</p>
                                <div className="aperture-iris"><Iris opening={(16 / v("aperture") - 1) / (16 / 1.4 - 1)} label="Aperture blades opening and closing" /></div>
                                {slider("aperture", 1.4, 16, 0.1)}
                                <div className="range-labels"><span>Wide open</span><span>Stopped down</span></div>
                                <div className="camera-linked-stats two">
                                    <div><span>Pupil diameter</span><strong>{optics.pupil.toFixed(1)}<small> mm</small></strong></div>
                                    <div><span>In-focus depth</span><strong>{formatDistance(optics.depth)}</strong></div>
                                </div>
                            </>)}
                            {card("Focus distance", ScanLine, <>
                                <div className="metric">{num("focus", "m")}</div>
                                <p className="muted">Drag the focal plane to set subject distance</p>
                                <DepthOfField focus={v("focus")} optics={optics} enabled={featureOn("Aperture")} onChange={(distance) => set("focus", distance)} />
                                {slider("focus", 1, 100)}
                                <div className="range-labels"><span>Near · 1 m</span><span>Far · 100 m</span></div>
                                <div className="camera-linked-stats two">
                                    <div><span>Near limit</span><strong>{featureOn("Aperture") ? formatDistance(optics.near) : "—"}</strong></div>
                                    <div><span>Far limit</span><strong>{featureOn("Aperture") ? formatDistance(optics.far) : "—"}</strong></div>
                                </div>
                                <div className="focus-footnote">{featureOn("Aperture") ? `Hyperfocal ${formatDistance(optics.hyperfocal)} · CoC 0.03 mm` : "Depth of field is disabled · all distances remain sharp"}</div>
                            </>, "focus-distance-card")}
                        </>}

                        {kind === "post-process" && <>
                            {card("Exposure & EV Compensation", Aperture, <>
                                <div className="scattering-top"><div><div className="metric">{num("evComp", "EV", 2)}</div><p className="muted">ACES filmic post-exposure bias</p></div><span className="small-pill">ACES Filmic</span></div>
                                {slider("evComp", -4, 4, 0.05)}
                                <div className="range-labels"><span>-4.0 EV · Underexpose</span><span>+4.0 EV · Overexpose</span></div>
                            </>, "wide-card")}
                            {card("Color Grading", Palette, <>
                                <div className="control-line"><span>Saturation</span><span>{Number(v("saturation")).toFixed(2)}×</span></div>
                                {slider("saturation", 0, 2, 0.02)}
                                <div className="control-line" style={{ marginTop: 10 }}><span>Contrast</span><span>{Number(v("contrast")).toFixed(2)}×</span></div>
                                {slider("contrast", 0.5, 2, 0.02)}
                            </>)}
                            {card("Lens Effects", Sparkles, <>
                                <div className="control-line"><span>Bloom Intensity</span><span>{Math.round(Number(v("bloom")) * 100)}%</span></div>
                                {slider("bloom", 0, 1, 0.01)}
                                <div className="control-line" style={{ marginTop: 10 }}><span>Vignette</span><span>{Math.round(Number(v("vignette")) * 100)}%</span></div>
                                {slider("vignette", 0, 1, 0.01)}
                            </>)}
                        </>}

                        {kind === "studio-key" && <>
                            {card("Overhead Softbox Bank", Sparkles, <>
                                <div className="scattering-top"><div><div className="metric">{num("softboxEv", "EV", 1)}</div><p className="muted">24×11 m overhead studio softbox · no outdoor sun or sky</p></div><span className="small-pill">Studio Key</span></div>
                                {slider("softboxEv", 4, 16, 0.1)}
                                <div className="control-line" style={{ marginTop: 10 }}><span>Correlated Color Temp</span><span>{v("softboxTemp")} K</span></div>
                                {slider("softboxTemp", 3200, 7500, 50)}
                            </>, "wide-card")}
                        </>}

                        {kind === "studio-rim" && <>
                            {card("Rim Strip Array", Layers, <>
                                <div className="scattering-top"><div><div className="metric">{num("rimIntensity", "EV", 1)}</div><p className="muted">Grazing perimeter strips highlighting silhouette curvature</p></div><span className="small-pill">Grazing Rim</span></div>
                                {slider("rimIntensity", 0, 14, 0.1)}
                            </>, "wide-card")}
                        </>}

                        {kind === "turntable" && <>
                            {card("Cyclorama Turntable", Orbit, <>
                                <div className="scattering-top"><div><div className="metric">{num("turntableRpm", "RPM", 2)}</div><p className="muted">Enclosed 40 m automotive presentation cove</p></div><span className="small-pill">Enclosed Bay</span></div>
                                {slider("turntableRpm", 0, 6, 0.05)}
                            </>, "wide-card")}
                        </>}
                    </div>
                )}

                {!obj.isContainer && !["display", "debug-channels", "post-process", "studio-key", "studio-rim", "turntable"].includes(kind) && (
                    <BakeQuickTiles key={"bake-" + obj.id} object={obj} kind={kind} values={values[obj.id] || {}} settings={{ ...defaults, ...values[obj.id] }} onSet={set} allValues={values} />
                )}

                <footer className="inspector-footer">
                    <span><span className="footer-dot" />{obj.native ? "Native record · saved to authoring scene" : "HTML shadow-box mirror · changes apply live"}</span>
                    <span>{obj.name} <span className="footer-slash">/</span> {obj.type}</span>
                </footer>
            </div>
        </section>
    );

    const leafEntities = all.filter((n) => !n.isContainer);
    const visibleLeafCount = leafEntities.filter((n) => !isHidden(n.id)).length;
    const hiddenLeafCount = leafEntities.length - visibleLeafCount;
    const sceneTitleText = sceneKey === "lobby" ? "Lobby · Studio.scene" : "Evergreen valley.scene";

    const treeEntities = visibilityFilter === "all"
        ? all
        : all.filter((n) => n.isContainer || (visibilityFilter === "visible" ? !isHidden(n.id) : isHidden(n.id)));

    const outlinerBody = (
        <aside className="outliner pz-outliner-pane">
            <div className="outliner-heading">
                <h1>Outliner</h1>
                <small>{sceneTitleText} · {leafEntities.length} objects</small>
                <button
                    type="button"
                    aria-label="Open Construct"
                    title="Construct · Shift+A"
                    onClick={() => setAdd(!add)}
                >
                    <PzActionIcon name={add ? "close" : "plus"} size={14} />
                </button>
            </div>

            {add && (
                <ConstructMenu
                    entities={activeSceneObjects.filter((o) => !["display", "debug-channels"].includes(o.id))}
                    properties={constructInspect ? inspectorBody : null}
                    onInspect={(id) => { setConstructInspect(id); if (id) setSelected(id); }}
                    onClose={() => { setAdd(false); setConstructInspect(null); }}
                    onOrganize={addOrganization}
                    onCreated={(data) => {
                        syncNative(data);
                        setSelected(data.entities.at(-1).id);
                        setQuery("");
                        setCollapsed((s) => ({ ...s, Folders: false, "folder-native-authoring": false }));
                        setSaved(false);
                    }}
                />
            )}

            <div className="stats">
                <button
                    type="button"
                    className={visibilityFilter === "visible" ? "is-filtered" : ""}
                    onClick={() => setVisibilityFilter((f) => (f === "visible" ? "all" : "visible"))}
                >
                    <span className="status-disc">✓</span>
                    <small>Visible</small>
                    <strong>{visibleLeafCount}</strong>
                </button>
                <button
                    type="button"
                    className={visibilityFilter === "hidden" ? "is-filtered" : ""}
                    onClick={() => setVisibilityFilter((f) => (f === "hidden" ? "all" : "hidden"))}
                >
                    <span className="muted-disc">◌</span>
                    <small>Hidden</small>
                    <strong>{hiddenLeafCount}</strong>
                </button>
            </div>

            <div className="outliner-search">
                <label>
                    <PzActionIcon name="search" size={13} />
                    <input
                        type="search"
                        placeholder="Search scene..."
                        value={query}
                        onChange={(e) => setQuery(e.target.value)}
                    />
                </label>
                <button
                    type="button"
                    onClick={(e) => {
                        const rect = e.currentTarget.getBoundingClientRect();
                        setFilterMenu({ x: rect.left, y: rect.bottom + 6 });
                    }}
                >
                    <PzActionIcon name="filter" size={12} />
                    <span>{filterGroup === "All" ? "Filter" : filterGroup}</span>
                </button>
            </div>

            <SceneTree
                all={treeEntities}
                organization={organization}
                selected={obj.id}
                onSelect={setSelected}
                collapsed={collapsed}
                setCollapsed={setCollapsed}
                isHidden={isHidden}
                toggleHidden={toggleHidden}
                query={query}
                values={values}
                filterGroup={filterGroup}
            />

            <footer className="outliner-footer">
                <span><small>SCENE</small><b>LIVE</b></span>
                <span><small>RENDER</small><b>{displayState.rayTracing !== false ? "BVH PT" : "SDF GI"}</b></span>
                {sceneKey === "lobby" ? (
                    <span><small>STUDIO</small><b>Cyclorama</b></span>
                ) : (
                    <span><small>SUN</small><b>{values.sun?.elevation ?? 38}°</b></span>
                )}
                <span><small>MODE</small><b>{runMode}</b></span>
            </footer>
        </aside>
    );

    const postVals = values["post-process"] || {};
    const postDisabled = isHidden("post-process");
    const evBias = Number(postVals.evComp ?? 0);
    const satVal = Number(postVals.saturation ?? 1);
    const conVal = Number(postVals.contrast ?? 1);
    const vigVal = postDisabled ? 0 : Number(postVals.vignette ?? 0.15);
    const stageFilter = postDisabled
        ? "none"
        : `brightness(${Math.pow(2, evBias * 0.35).toFixed(3)}) saturate(${satVal.toFixed(3)}) contrast(${conVal.toFixed(3)})`;

    const activeCameraValue = selected === "cine-camera" ? "cine-camera" : "editor-camera";

    const viewportBody = (
        <div className="pz-viewport-pane">
            <header className="viewport-toolbar">
                <div className="viewport-heading-row">
                    <div className="viewport-identity">
                        <PzActionIcon name="viewport" size={16} />
                        <div>
                            <button
                                type="button"
                                className="viewport-scene-btn"
                                onClick={(e) => {
                                    const rect = e.currentTarget.getBoundingClientRect();
                                    setSceneMenu({ x: rect.left, y: rect.bottom + 6 });
                                }}
                            >
                                {sceneTitleText} ▾
                            </button>
                            <span>
                                {sceneKey === "lobby"
                                    ? "Enclosed Studio Cyclorama · Studio Lights Only (No Sun & Sky)"
                                    : "HTML C++ Shadow-Box Mirror · BVH ReSTIR GI · Multi-Bounce"}
                            </span>
                        </div>
                        <em className="viewport-preview-badge">HTML PREVIEW</em>
                    </div>

                    <div className="viewport-modes" role="group" aria-label="Viewport run mode">
                        {[
                            ["EDIT", "Edit", "edit"],
                            ["SIM",  "Sim",  "simulate"],
                            ["PLAY", "Play", "play"]
                        ].map(([modeKey, label, glyph]) => (
                            <button
                                key={modeKey}
                                type="button"
                                className={runMode === modeKey ? "active" : ""}
                                aria-pressed={runMode === modeKey}
                                onClick={() => setRunMode(modeKey)}
                            >
                                <PzActionIcon name={glyph} size={11} />
                                <span>{label}</span>
                            </button>
                        ))}
                    </div>
                </div>

                <div className="viewport-action-row">
                    <button type="button" className="viewport-construct" onClick={() => setAdd(true)} title="Construct · Shift+A">
                        <PzActionIcon name="construct" size={13} />
                        <span>Construct</span>
                        <kbd>Shift+A</kbd>
                    </button>

                    <button
                        type="button"
                        className="viewport-icon-button"
                        title="Focus selected object (F)"
                        onClick={() => document.querySelector(".tree-row.selected")?.scrollIntoView({ block: "nearest" })}
                    >
                        <PzActionIcon name="focus" size={14} />
                    </button>

                    <span className="viewport-tool-divider" />

                    <div className="viewport-projection">
                        <PzActionIcon name="camera" size={13} />
                        <select
                            aria-label="Active viewport camera"
                            value={activeCameraValue}
                            onChange={(e) => setSelected(e.target.value)}
                        >
                            <option value="editor-camera">Editor Camera · Navigation</option>
                            <option value="cine-camera">Cine Camera · 3:2 Optical</option>
                        </select>
                    </div>

                    <div className="viewport-secondary-tools">
                        <button
                            type="button"
                            className="viewport-icon-button"
                            aria-pressed={splitView}
                            title="Split comparison view"
                            onClick={() => setSplitView(!splitView)}
                        >
                            <PzActionIcon name="split" size={14} />
                        </button>
                        <button
                            type="button"
                            className="viewport-icon-button"
                            aria-pressed={diagnosticsOpen || activeDebugIndex > 0}
                            title="Diagnostics & debug readout (F3)"
                            onClick={() => setDiagnosticsOpen(!diagnosticsOpen)}
                        >
                            <PzActionIcon name="diagnostics" size={14} />
                        </button>
                        <button
                            type="button"
                            className="viewport-icon-button"
                            aria-pressed={vpSettingsOpen}
                            title="Viewport shading & post-process settings"
                            onClick={() => setVpSettingsOpen(!vpSettingsOpen)}
                        >
                            <PzActionIcon name="settings" size={14} />
                        </button>
                    </div>
                </div>

                {vpSettingsOpen && (
                    <div className="viewport-settings-popover">
                        <header>
                            <strong>Viewport Settings</strong>
                            <button type="button" onClick={() => setVpSettingsOpen(false)}>×</button>
                        </header>
                        <label>
                            <span>Debug Inspection Channel</span>
                            <select
                                aria-label="Debug channel"
                                value={activeDebugIndex}
                                onChange={(e) => setEntityValue("debug-channels", "debugChannel", Number(e.target.value))}
                            >
                                {DisplayChannels.map((ch) => (
                                    <option key={ch.id} value={ch.id}>{ch.code} · {ch.label}</option>
                                ))}
                            </select>
                        </label>
                        <label className="inline-check">
                            <span>Post Process Grading</span>
                            <input
                                type="checkbox"
                                checked={!isHidden("post-process")}
                                onChange={() => toggleHidden("post-process")}
                            />
                        </label>
                        <button
                            type="button"
                            style={{ height: 30, borderRadius: 8, border: "1px solid #ffffff14", background: "#222", color: "#eee", cursor: "pointer", fontSize: 11 }}
                            onClick={() => { setSelected("display"); setVpSettingsOpen(false); }}
                        >
                            Open Display Inspector →
                        </button>
                    </div>
                )}
            </header>

            <div className={`pz-scene-stage ${splitView ? "is-split" : ""}`} style={{ filter: stageFilter }}>
                <FrontierCheckerViewport all={all} selected={obj.id} onSelect={setSelected} isHidden={isHidden} values={values} viewIndex={1} sceneKey={sceneKey} />
                {splitView && (
                    <FrontierCheckerViewport all={all} selected={obj.id} onSelect={setSelected} isHidden={isHidden} values={values} viewIndex={2} sceneKey={sceneKey} />
                )}
                <div className="pz-post-vignette" style={{ opacity: vigVal }} />

                {(diagnosticsOpen || activeDebugIndex > 0) && (
                    <aside className="pz-diagnostics-card" aria-label="Active diagnostics readout">
                        <div>
                            <span className="workspace-eyebrow">DIAGNOSTICS · F3 · {runMode}</span>
                            <strong>{activeDebugEntry.code} · {activeDebugEntry.label}</strong>
                            <p>{activeDebugEntry.detail}</p>
                        </div>
                        <div className="pz-diag-actions">
                            <button type="button" onClick={() => setSelected("debug-channels")}>Channels</button>
                            <button type="button" onClick={() => setSelected("post-process")}>Post</button>
                            <button type="button" onClick={() => { setDiagnosticsOpen(false); setEntityValue("debug-channels", "debugChannel", 0); }}>Close</button>
                        </div>
                    </aside>
                )}
            </div>

            <footer className="pz-viewport-footer">
                <span>SCENE <b>{sceneTitleText}</b> · MODE <b>{runMode}</b> · VISIBLE <b>{visibleLeafCount}/{leafEntities.length}</b></span>
                <span>BVH REFLECTIONS {displayState.rayTracedReflections !== false ? "ON" : "OFF"} · {displayState.giBounces ?? 3}B GI · CAMERAS <b>3</b></span>
            </footer>
        </div>
    );

    const closeTab = (tabName) => {
        if (tabName === "Viewport") return;
        setPaneTabs((prev) => ({
            Left: prev.Left.filter((t) => t !== tabName),
            Centre: prev.Centre.filter((t) => t !== tabName),
            Right: prev.Right.filter((t) => t !== tabName)
        }));
    };

    const renderTabStrip = (side) => {
        const tabs = paneTabs[side];
        const current = tabs.includes(activePaneTab[side]) ? activePaneTab[side] : tabs[0];
        return (
            <header
                className="tab-strip"
                onDragOver={(e) => e.preventDefault()}
                onDrop={(e) => {
                    e.preventDefault();
                    const tabId = e.dataTransfer?.getData("application/frontier-tab") || draggedTabRef.current;
                    draggedTabRef.current = null;
                    if (tabId) moveTabToSide(tabId, side);
                }}
            >
                {tabs.map((tabName) => (
                    <div
                        key={tabName}
                        draggable
                        className={`document-tab ${current === tabName ? "active" : ""}`}
                        title={tabName === "Inspector" ? "Double-click to expand Inspector workspace" : undefined}
                        onDragStart={(e) => {
                            draggedTabRef.current = tabName;
                            e.dataTransfer?.setData("application/frontier-tab", tabName);
                        }}
                        onClick={() => setActivePaneTab((s) => ({ ...s, [side]: tabName }))}
                        onDoubleClick={() => {
                            if (tabName === "Inspector") setWideWorkspace((w) => !w);
                        }}
                    >
                        <span>{tabName}</span>
                        {tabName !== "Viewport" && (
                            <button
                                type="button"
                                aria-label={`Close ${tabName}`}
                                onClick={(e) => { e.stopPropagation(); closeTab(tabName); }}
                            >
                                <PzActionIcon name="close" size={10} />
                            </button>
                        )}
                    </div>
                ))}
                {side === "Left" && (
                    <button
                        type="button"
                        className="tab-add"
                        aria-label="Open panel or workspace menu"
                        onClick={(e) => {
                            const rect = e.currentTarget.getBoundingClientRect();
                            setTabMenu({ side, x: rect.left, y: rect.bottom + 6 });
                        }}
                    >
                        <PzActionIcon name="plus" size={12} />
                    </button>
                )}
            </header>
        );
    };

    const renderPanelContent = (tabName) => {
        if (tabName === "Outliner") return outlinerBody;
        if (tabName === "Viewport") return viewportBody;
        if (tabName === "Inspector") return add && constructInspect ? <section className="inspector pz-inspector-pane" /> : inspectorBody;
        return <div className="pz-empty-dock">Drop a tab here or press + to open a panel.</div>;
    };

    const switchScene = (nextScene) => {
        setSceneKey(nextScene);
        setSceneMenu(null);
        if (nextScene === "lobby" && ["sun", "flare", "moon", "stars", "sky", "rainbow", "cloud", "precip", "forest", "terrain", "water", "ocean", "river", "liquid"].includes(selected)) {
            setSelected("editor-camera");
        }
    };

    return (
        <div id="Editor" className="pz-editor-root">
            {/* Top Notch + Control Center Drawer (ProjectZeroEditor 1:1) */}
            <FrontierNotch
                open={notchOpen}
                onToggle={(next) => setNotchOpen(typeof next === "boolean" ? next : !notchOpen)}
                displayValues={displayState}
                onUpdateDisplay={setEntityValue}
                onSelectEntity={setSelected}
                diagnosticsVisible={diagnosticsOpen}
                onToggleDiagnostics={() => setDiagnosticsOpen((v) => !v)}
                onOpenAssets={(cat) => { setAssetCategory(cat || "all"); setAssetOpen(true); }}
            />

            {wideWorkspace && (
                <button type="button" className="return-viewport" onClick={() => setWideWorkspace(false)}>
                    Return to Viewport
                </button>
            )}

            {/* 3-Column Dockable Workspace (ProjectZeroEditor 1:1) */}
            <main
                className={`workspace ${wideWorkspace ? "inspector-workspace" : ""}`}
                style={{ "--left": `${leftWidth}px`, "--right": `${rightWidth}px` }}
            >
                {["Left", "Centre", "Right"].map((side) => {
                    const tabs = paneTabs[side];
                    const current = tabs.includes(activePaneTab[side]) ? activePaneTab[side] : tabs[0];
                    return (
                        <section key={side} className={`dock ${side.toLowerCase()}`} aria-label={`${side} editor dock`}>
                            {renderTabStrip(side)}
                            <div className="pz-dock-body">
                                {renderPanelContent(current)}
                            </div>
                            {side !== "Centre" && !wideWorkspace && (
                                <div
                                    className={`divider ${side.toLowerCase()}`}
                                    role="separator"
                                    aria-orientation="vertical"
                                    onPointerDown={(e) => {
                                        dividerRef.current = { side, width: side === "Left" ? leftWidth : rightWidth, x: e.clientX };
                                        document.body.classList.add("resizing");
                                    }}
                                />
                            )}
                        </section>
                    );
                })}
            </main>

            {/* Bottom Asset Browser Drawer (ProjectZeroEditor 1:1) */}
            <FrontierAssetDrawer
                open={assetOpen}
                onToggle={(next) => setAssetOpen(typeof next === "boolean" ? next : !assetOpen)}
                initialCategory={assetCategory}
                materialValues={values.material || {}}
                onApplyMaterial={(asset) => {
                    setValues((prev) => ({
                        ...prev,
                        material: {
                            ...(prev.material || {}),
                            albedo: asset.color || "#c5a6d7",
                            roughness: asset.roughness ?? 25,
                            metallic: asset.metallic ?? 50
                        }
                    }));
                    setSelected("material");
                    setSaved(false);
                }}
            />

            {/* Scene Switcher Menu (Evergreen valley.scene vs sunless Lobby · Studio.scene) */}
            {sceneMenu && (
                <>
                    <div className="menu-dismiss" onPointerDown={() => setSceneMenu(null)} />
                    <div className="popup-menu" role="menu" style={{ left: Math.max(8, sceneMenu.x), top: sceneMenu.y }}>
                        <label>SCENES</label>
                        <button type="button" onClick={() => switchScene("valley")}>
                            <i className={`dot ${sceneKey === "valley" ? "green" : ""}`} />
                            <span>Evergreen valley.scene</span>
                            <small>Outdoor</small>
                        </button>
                        <button type="button" onClick={() => switchScene("lobby")}>
                            <i className={`dot ${sceneKey === "lobby" ? "green" : ""}`} />
                            <span>Lobby · Studio.scene</span>
                            <small>No Sun &amp; Sky</small>
                        </button>
                    </div>
                </>
            )}

            {/* Outliner Filter Menu */}
            {filterMenu && (
                <>
                    <div className="menu-dismiss" onPointerDown={() => setFilterMenu(null)} />
                    <div className="popup-menu" role="menu" style={{ left: Math.max(8, filterMenu.x), top: filterMenu.y }}>
                        <label>COLLECTIONS</label>
                        {["All", "Render", "Cameras", "Environment", "World", "Water bodies", "Materials"].map((cat) => (
                            <button key={cat} type="button" onClick={() => { setFilterGroup(cat); setFilterMenu(null); }}>
                                <i className={`dot ${filterGroup === cat ? "green" : ""}`} />
                                <span>{cat}</span>
                            </button>
                        ))}
                    </div>
                </>
            )}

            {/* + Dock Menu */}
            {tabMenu && (
                <>
                    <div className="menu-dismiss" onPointerDown={() => setTabMenu(null)} />
                    <div
                        className="popup-menu"
                        role="menu"
                        style={{ left: Math.max(8, Math.min((window.innerWidth || 1280) - 240, tabMenu.x)), top: tabMenu.y }}
                    >
                        <label>PANELS</label>
                        {["Outliner", "Viewport", "Inspector"].map((t) => {
                            const openNow = ["Left", "Centre", "Right"].some((s) => paneTabs[s].includes(t));
                            const defaultSide = t === "Outliner" ? "Left" : (t === "Viewport" ? "Centre" : "Right");
                            return (
                                <button key={t} type="button" onClick={() => moveTabToSide(t, openNow ? tabMenu.side : defaultSide)}>
                                    <i className={`dot ${openNow ? "green" : ""}`} />
                                    <span>{t}</span>
                                    <small>{openNow ? "Open" : "Add"}</small>
                                </button>
                            );
                        })}
                        <hr />
                        <label>DRAWERS &amp; WORKSPACE</label>
                        <button type="button" onClick={() => { setTabMenu(null); setNotchOpen(true); }}>
                            <PzActionIcon name="sliders" size={14} />
                            <span>Control Center</span>
                            <small>Top Notch</small>
                        </button>
                        <button type="button" onClick={() => { setTabMenu(null); setAssetOpen(true); }}>
                            <PzActionIcon name="swatch" size={14} />
                            <span>Asset Browser</span>
                            <small>Bottom</small>
                        </button>
                        <button
                            type="button"
                            onClick={() => {
                                setPaneTabs({ Left: ["Outliner"], Centre: ["Viewport"], Right: ["Inspector"] });
                                setActivePaneTab({ Left: "Outliner", Centre: "Viewport", Right: "Inspector" });
                                setWideWorkspace(false);
                                setTabMenu(null);
                            }}
                        >
                            <PzActionIcon name="refresh" size={14} />
                            <span>Reset Dock Layout</span>
                        </button>
                    </div>
                </>
            )}
        </div>
    );
}

function DayCurve({ time })
{
    const height = (t) => 60 - 42 * Math.sin(((t - 6) / 24) * Math.PI * 2);
    const x = 18 + (time / 24) * 464;
    const y = height(time);
    const path = Array.from({ length: 97 }, (_, i) => `${i ? "L" : "M"}${18 + (i / 96) * 464} ${height(i / 4)}`).join(" ");
    return (
        <svg className="day-curve full-day-curve" viewBox="0 0 500 123" role="img" aria-label={`24-hour daylight cycle, selected time ${Math.floor(time)}:${time % 1 ? "30" : "00"}`}>
            <rect x="18" y="10" width="116" height="98" rx="4" fill="#8397c109" />
            <rect x="366" y="10" width="116" height="98" rx="4" fill="#8397c109" />
            <path d="M18 60 H482" stroke="#8c806b44" strokeDasharray="3 5" />
            <path d={path} stroke="#a8a1a1" strokeOpacity=".45" fill="none" strokeWidth="1.3" />
            <path d={Array.from({ length: 49 }, (_, i) => `${i ? "L" : "M"}${134 + (i / 48) * 232} ${height(6 + i / 4)}`).join(" ")} stroke="#dac18a" fill="none" strokeWidth="1.5" />
            <path d={`M${x} 10 V110`} stroke="#c0ad8c" strokeDasharray="2 4" strokeOpacity=".45" />
            <circle cx={x} cy={y} r="11" fill={time < 6 || time >= 18 ? "#a7b7df" : "#e9c889"} opacity=".1" />
            <circle cx={x} cy={y} r="4" fill={time < 6 || time >= 18 ? "#b3c2df" : "#efd19a"} />
            <g fill="#7a7770" fontSize="8">
                <text x="28" y="115">NIGHT</text>
                <text x="235" y="115">DAY</text>
                <text x="442" y="115">NIGHT</text>
            </g>
        </svg>
    );
}

createRoot(document.getElementById("root")).render(<App />);
