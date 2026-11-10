import React, { useState } from "react";
import { ProjectZeroIcons } from "./project-zero-icons.jsx";

const FolderIconMap = Object.freeze({
    Cameras: "folder-scene",
    Environment: "folder-environment",
    "Water bodies": "folder-water",
    Scene: "folder-scene",
    World: "folder-world",
    Materials: "folder-materials",
    Render: "graphics-card-2d"
});

// Shared concepts map directly to ProjectZeroEditor's EngineContent/Icons artwork;
// entities that do not exist in ProjectZeroEditor (botanical species silhouettes, ocean, river)
// return null here so OutlinerIcon preserves FrontierEditor's specialized symbol.
const EntityIconMap = Object.freeze({
    sun: "sun",
    flare: "lens-flare",
    moon: "moon",
    stars: "outliner-stars",
    sky: "sky-scattering",
    rainbow: "rainbow",
    cloud: "clouds",
    precip: "outliner-precipitation",
    "local-cloud": "local-cloud",
    "local-fog": "local-fog",
    air: "wind",
    forest: "forest",
    terrain: "terrain",
    water: "fluid",
    liquid: "fluid-glass",
    cube: "editor-mesh",
    sphere: "editor-sphere-collider",
    cylinder: "editor-mesh",
    plane: "editor-mesh",
    material: "editor-material",
    camera: "camera",
    "editor-camera": "camera",
    "cine-camera": "clapperboard",
    "post-process": "environment-exposure",
    "studio-key": "editor-area-light",
    "studio-rim": "editor-spotlight",
    turntable: "editor-grid",
    display: "graphics-card-2d",
    "debug-channels": "editor-debug-bug",
    collection: "editor-collection",
    project: "earth",
    native: "editor-mesh"
});

export function outlinerIconSource(object, values = {})
{
    const kind = object.base || object.id;
    if (kind === "folder")
    {
        const category = object.category || Object.keys(FolderIconMap).find((key) => key.toLowerCase() === (object.name || "").toLowerCase());
        const iconName = FolderIconMap[category] || "folder-generic";
        return ProjectZeroIcons[iconName] || ProjectZeroIcons["folder-generic"];
    }
    if (kind === "native" && object.native?.camera)
        return ProjectZeroIcons.camera;
    const mapped = EntityIconMap[object.id] || EntityIconMap[kind];
    return mapped ? ProjectZeroIcons[mapped] || null : null;
}

export default function OutlinerIcon({ object, values = {}, size = 22 })
{
    const src = outlinerIconSource(object, values);
    const [failed, setFailed] = useState(null);
    const Fallback = object.icon;
    const style = { width: size, height: size, flexShrink: 0, objectFit: "contain" };

    if (!src || failed === src)
        return Fallback ? <Fallback className="outliner-icon-fallback" size={size} style={{ ...style, color: object.color }} aria-hidden="true" /> : null;

    return (
        <img
            className="outliner-asset-icon"
            src={src}
            alt=""
            aria-hidden="true"
            draggable={false}
            width={size}
            height={size}
            style={style}
            onError={() => setFailed(src)}
            data-icon-kind={object.base || object.id}
        />
    );
}
