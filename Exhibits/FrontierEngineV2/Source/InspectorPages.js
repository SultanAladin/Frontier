//============================================================================================================================================
//                                                             INSPECTORPAGES.JS
//============================================================================================================================================
// 🧩 What the outliner lists and what the inspector shows for each entry: render settings, sun, vehicle, smoke + fire, fracture.

// 📝 The Frontier Editor carries no render settings; the exhibit adds them as an inspector of their own beside the
//    entities it shares with the editor's vocabulary. The Smoke + Fire inspector is the native Gas card's sections
//    (Domain, Source, Simulation, Appearance) plus Fire and Burnout, every slider generated from the gas catalogue;
//    the Fracture inspector is the Fracture Editor's (Material, Impact, Fragment quality, Bake, Geometry receipt),
//    driving the exhibit's own breakable props.

import { RendererLabels } from "./RendererNames.js";
import { GasParameterCatalogue, ConstructGasParameters } from "./SmokeSequence.js";
import { FractureMaterials, FractureMaterialOrder } from "./FractureStructure.js";
import { FractureModes } from "./FractureSequence.js";
import { VehicleGeometry } from "./VehicleSpecification.js";
import { ArenaZones, TrainingDrills } from "./LevelStructure.js";
import { DerezPresets } from "./EffectSequence.js";
import { FormatPrimary } from "./TrainingSequence.js";
import { CameraRigs } from "./CabinStructure.js";
import { AutomotiveMode, RoofFixtureType } from "./LightingSystem.js";
import { CaptureFrameCount, CaptureMatrix } from "./BenchmarkMetrics.js";
import { PaintOptions, SurfaceTextures } from "./VehicleAppearance.js";
import { LevelAtlas } from "./LevelAtlas.js";
import { BuildInteractiveCameraCards, ComputeCameraOptics } from "./CameraVisuals.js";
import { BuildArrayModifierCard } from "./ArrayModifier.js";
import { ChoreographyStyles } from "./CinematicCamera.js";
import { LightType } from "./LightingSystem.js";

export const InspectionModes = [
    ["Final image", 0], ["Direct sun only", 1], ["Indirect diffuse (GI)", 2], ["Indirect irradiance (denoised)", 3],
    ["ReSTIR irradiance (raw, no denoise)", 4], ["Glossy reflections", 5], ["BVH / Distance-field inspection", 6],
    ["Surface cards (texel space)", 7], ["Normals", 8], ["Albedo", 9], ["Fresnel reflectance (environment)", 10],
    ["Sample duplication map D (§5)", 11], ["Reconnection vertex mode k (§4)", 12], ["Reciprocal pairing delta (§3)", 13]
];

export const DistanceInspections = [
    ["Triangle / field normals · dynamic objects", 1], ["BVH / SDF traversal step heat", 2], ["Ray-traced NEE / surface cache", 3]
];

export const StressMotionOptions = [["Static", 0], ["One in ten", 1], ["All moving", 2]];

export const IgnitionOptions = [["Authored (350 °C held 4 s)", 0], ["Diagnostic (160 °C held 1 s)", 1], ["Off", 2]];

export const LightingPresets = {
    Noon:   { SunElevation: 64,  SunAzimuth: 145, SolarIlluminance: 12, EmissionScale: 1.0 },
    Golden: { SunElevation: 10,  SunAzimuth: 160, SolarIlluminance: 12, EmissionScale: 1.0 },
    Dusk:   { SunElevation: 1.5, SunAzimuth: 172, SolarIlluminance: 12, EmissionScale: 1.0 },
    Night:  { SunElevation: -14, SunAzimuth: 172, SolarIlluminance: 12, EmissionScale: 1.0 }
};

// 📝 Places R can't reach: the editor drops the car at the start of each feature (LevelStructure's ArenaZones).
export const LevelZones = ArenaZones;

// 📝 Look presets for the gas: each a partial override of the catalogue defaults.
export const GasPresets = {
    "Tyre smoke":   {},
    "Thick burnout": { Emission: 2.2, Density: 1.6, Dissipation: 0.12, Albedo: 0.9, Vorticity: 2.2, Expansion: 1.4, MultiScatter: 0.9 },
    "Light haze":   { Emission: 0.6, Density: 0.55, Dissipation: 0.35, Detail: 0.75, Erosion: 0.9, Albedo: 0.92 },
    "Inferno":      { FlameIntensity: 2.4, FlameTemperature: 1.18, FlameFlicker: 0.85, Soot: 2.2, Heat: 1.8, FireLight: 2.2, Buoyancy: 3.4 },
    "Crosswind":    { WindSpeed: 5.5, WindHeading: 95.0, Turbulence: 0.7, Dissipation: 0.28 },
    // 📝 The look before the reference pass: big warped billows, softer and cloudier.
    "Legacy billows": { Density: 1.0, Dissipation: 0.22, Vorticity: 1.6, Turbulence: 0.4, TurbulenceScale: 1.1, Albedo: 0.86, Phase: 0.45,
                        MultiScatter: 0.45, Detail: 0.65, DetailScale: 0.85, Erosion: 0.75, Warp: 0.35, Powder: 0.2, WindSpeed: 0.0 }
};

const Accent = {
    Render: "#9fb4d6", Cameras: "#bb9ae4", Lights: "#f0c869", Environment: "#e8c27a", Vehicle: "#e1d34a", Effects: "#e8833a", Destruction: "#8fb3a4", Level: "#a8bbeb"
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     ENTITIES
//------------------------------------------------------------------------------------------------------------------------

// 📝 { Id, Group, Name, Type, Glyph, Colour, Meta(), State() ("live" | "warm" | ""), Toggle?, Parent?, Description,
//    Switches?(), Build(Controls, Body) }.
export function ConstructEntities(Host)
{
    const S = Host.Settings;
    const Entities = [
        { Id: "render", Group: "Render", Name: "Render settings", Type: "Renderer", Glyph: "Monitor", Colour: Accent.Render,
          Description: "BVH ReSTIR Path Tracer, resolution, reflections & debug views",
          Meta: () => `${S.RayTracing ? "BVH · PT" : "SDF · GI"} · ${Host.GradeName}`, State: () => "live", Build: (C, B) => BuildRender(Host, C, B),
          Switches: () => [Feature("Raytracing", "Zap", S, "RayTracing"), Feature("Indirect", "Sparkles", S, "Indirect"),
                           Feature("Reflections", "Aperture", S, "Reflection"), Feature("Denoiser", "Layers", S, "VarianceKernel"),
                           Feature("TAA", "Scan", S, "Antialias")] },
        { Id: "field", Group: "Render", Name: "BVH & distance field", Type: "Acceleration structures", Glyph: "GridLattice", Colour: Accent.Render,
          Description: "Two-level BVH (TLAS + BLAS) & toroidal SDF cascades",
          Meta: () => (S.RayTracing && Host.RayTracing ? `${Host.RayTracing.ActiveInstances} TLAS` : `${Host.Grade.ClipResolution}³`),
          State: () => (S.FreezeDistance ? "warm" : "live"), Build: (C, B) => BuildField(Host, C, B) },
        { Id: "cards", Group: "Render", Name: "Surface cache", Type: "Surface cards", Glyph: "LayoutGrid", Colour: Accent.Render,
          Description: "Card radiosity & relighting budget", Meta: () => `${Host.Cards.Cards.length}`, State: () => "live",
          Build: (C, B) => BuildCards(Host, C, B) },

        // 📝 Project-Zero Cameras collection (Editor Camera + Post Process everywhere; Cine Camera in Lobby only).
        { Id: "editor-camera", Group: "Cameras", Name: "Editor Camera", Type: "Permanent editor navigation camera", Glyph: "Camera", Colour: Accent.Cameras,
          Permanent: true,
          Description: "Permanent editor navigation camera · not a scene render camera",
          Meta: () => `${(S.EditorFocalLength || 35).toFixed(0)} mm · ${Host.FreeCamera || (Host.Lobby && S.CameraMode === "Orbit") ? "active" : "standby"}`,
          State: () => (Host.FreeCamera || (Host.Lobby && S.CameraMode === "Orbit") ? "live" : ""),
          Build: (C, B) => BuildEditorCamera(Host, C, B) },
        ...(Host.Lobby ? [{
          Id: "cine-camera", Group: "Cameras", Name: "Cine Camera", Type: "Lobby cine camera · 3D debug & spline", Glyph: "Clapperboard", Colour: Accent.Cameras,
          Description: "Lobby-only 3D cine camera (visible in Edit mode, hidden in Play) · seamless C¹ spline choreography",
          Meta: () => (S.CameraMode === "Cinematic" ? "rolling" : `${(S.CineFocalLength || 50).toFixed(0)} mm f/${(S.CineAperture || 2.0).toFixed(1)}`),
          State: () => (S.CameraMode === "Cinematic" ? "live" : ""),
          Toggle: { Read: () => Host.CineCameraVisible !== false, Write: (On) => { Host.CineCameraVisible = On; Host.EditorOccupantsDirty = true; } },
          Build: (C, B) => BuildCineCamera(Host, C, B)
        }] : []),
        { Id: "post-process", Group: "Cameras", Name: "Post Process", Type: "Post processing", Glyph: "Aperture", Colour: Accent.Cameras,
          Description: "Exposure, cockpit windshield metering, tone mapping, bloom & vignette",
          Meta: () => `${(S.ExposureCompensation >= 0 ? "+" : "") + (S.ExposureCompensation || 0).toFixed(1)} EV`,
          State: () => (S.PostEnabled !== false ? "live" : ""),
          Toggle: { Read: () => S.PostEnabled !== false, Write: (On) => { S.PostEnabled = On; Host.ApplyPostProcessStyle?.(); } },
          Switches: () => [Feature("Auto exposure", "Aperture", S, "AutoExposure"),
                           Feature("Cockpit EV", "Eye", S, "CockpitExposure"),
                           Feature("TAA", "Scan", S, "Antialias")],
          Build: (C, B) => BuildPostProcess(Host, C, B) }
    ];

    // 📝 Studio Lights collection (Lobby cyclorama softbox, cove washes, rim strips & user-added studio fixtures — strictly NO sun/sky).
    if (Host.Lighting?.StudioLights?.length)
    {
        Host.Lighting.StudioLights.forEach((Light, Index) =>
        {
            const LightId = Light.Id || `studio-light-${Index}`;
            Light.Id = LightId;
            Entities.push({
                Id: LightId,
                Group: "Lights",
                Name: Light.Name || `Studio Light ${Index + 1}`,
                Type: Light.Role || (Light.Type === LightType.Omni ? "Studio Omni Light" : "Studio Flood / Spot"),
                Glyph: Light.SoftboxLinked ? "Sun" : "Lightbulb",
                Colour: Accent.Lights,
                Description: "Indoor studio lighting fixture · clustered direct + multi-bounce surface cards",
                Meta: () => (Light.Enabled !== false ? `${Math.round(Light.Intensity)} cd` : "off"),
                State: () => (Light.Enabled !== false ? "live" : ""),
                Toggle: {
                    Read: () => Light.Enabled !== false,
                    Write: (On) => { Light.Enabled = On; Host.SyncStudioLightMaterial?.(); }
                },
                Build: (C, B) => BuildStudioLight(Host, Light, Index, C, B)
            });
        });
    }

    // 📝 Studio Lobby is an indoor cyclorama studio: Sun & sky only belongs to outdoor scenes (Proving Grounds).
    if (!Host.Lobby)
    {
        Entities.push(
            { Id: "sun", Group: "Environment", Name: "Sun & sky", Type: "Directional light", Glyph: "Sun", Colour: Accent.Environment,
              Description: "Light, direction & atmosphere", Meta: () => `${S.SunElevation.toFixed(0)}°`, State: () => (S.SunElevation > 0 ? "live" : "warm"),
              Build: (C, B) => BuildSun(Host, C, B),
              Switches: () => [Feature("Shadows", "Sun", S, "DirectShadow"), Feature("Auto exposure", "Aperture", S, "AutoExposure")] }
        );
    }

    Entities.push(
        { Id: "vehicle", Group: "Vehicle", Name: "ControlVehicle", Type: "Vehicle", Glyph: "Car", Colour: Accent.Vehicle,
          Description: "Telemetry, transform, burnout, tyres & child hierarchy", Meta: () => `${Math.abs(Host.Vehicle.QueryTelemetry().Speed * 3.6).toFixed(0)} km/h`,
          State: () => (Host.Vehicle.Burnout ? "warm" : "live"), Build: (C, B) => BuildVehicle(Host, C, B),
          Switches: () => [Feature("Line lock", "Flame", S, "LineLock"), Feature("Thermal grip", "Thermometer", S, "ThermalGrip"),
                           Feature("Tyre marks", "CircleDot", S, "TyreMarks"),
                           Feature("Sparks", "Zap", S, "Sparks")] },
        { Id: "vehicle-camera", Group: "Vehicle", Name: "Vehicle Camera", Type: "Parented 3D camera · child of ControlVehicle",
          Glyph: "Camera", Colour: Accent.Cameras, Parent: "vehicle",
          Description: "Low-poly 3D camera parented in ControlVehicle's local frame · moves & rotates with chassis",
          Meta: () => (S.CameraMode === "VehicleCamera" ? "active" : `${(S.VehicleCamFocalLength || 32).toFixed(0)} mm f/${(S.VehicleCamAperture || 2.8).toFixed(1)}`),
          State: () => (S.CameraMode === "VehicleCamera" ? "live" : (Host.VehicleCamera?.Visible !== false ? "warm" : "")),
          Toggle: {
              Read: () => Host.VehicleCamera?.Visible !== false,
              Write: (On) => { if (Host.VehicleCamera) Host.VehicleCamera.Visible = On; Host.EditorOccupantsDirty = true; }
          },
          Build: (C, B) => BuildVehicleCamera(Host, C, B) },
        { Id: "cabin", Group: "Vehicle", Name: "Cabin · HMI", Type: "Interior", Glyph: "Monitor", Colour: Accent.Vehicle, Parent: "vehicle",
          Description: "Tablet & cluster, body textures, finishes, mirrors, glass", Meta: () => S.CameraMode,
          State: () => (Host.Cabin && Host.Cabin.Screen ? "live" : "warm"), Build: (C, B) => BuildCabin(Host, C, B),
          Switches: () => [{ Name: "See-through glass", Glyph: "Eye", Read: () => Host.Cabin.Settings.SeeThroughGlass,
                             Write: (On) => { Host.Cabin.Settings.SeeThroughGlass = On; } },
                           { Name: "Screens", Glyph: "Monitor", Read: () => Host.Cabin.Settings.Screens, Write: (On) => { Host.Cabin.Settings.Screens = On; } }] },
        { Id: "marks", Group: "Vehicle", Name: "Tyre marks", Type: "Rubber deposit", Glyph: "CircleDot", Colour: Accent.Vehicle, Parent: "vehicle",
          Description: "Rubber laid by sliding work", Meta: () => `${Host.Marks.Metrics.LaidMetres.toFixed(0)} m`, State: () => (S.TyreMarks ? "live" : ""),
          Build: (C, B) => BuildMarks(Host, C, B),
          Toggle: { Read: () => S.TyreMarks, Write: (On) => { S.TyreMarks = On; } } },
        { Id: "gas", Group: "Effects", Name: "Smoke & fire", Type: "Gas domain", Glyph: "Flame", Colour: Accent.Effects,
          Description: "Eulerian smoke · combustion · tyre sources", Meta: () => (Host.Smoke.Resident ? "resident" : "idle"),
          State: () => (Host.Smoke.Resident ? "live" : ""), Build: (C, B) => BuildGas(Host, C, B),
          Toggle: { Read: () => S.SmokeEnabled, Write: (On) => { S.SmokeEnabled = On; } },
          Switches: () => [Feature("Smoke", "CloudFog", S, "SmokeEnabled"),
                           { Name: "Ignition", Glyph: "Flame", Read: () => S.IgnitionMode !== 2, Write: (On) => { S.IgnitionMode = On ? 0 : 2; } },
                           { Name: "Test plume", Glyph: "Wind", Read: () => S.TestPlume > 0, Write: (On) => { S.TestPlume = On ? 1 : 0; } },
                           Feature("Line lock", "Gauge", S, "LineLock")] },
        { Id: "fracture", Group: "Destruction", Name: "Fracture", Type: "Breakable props", Glyph: "Hammer", Colour: Accent.Destruction,
          Description: "Energy-limited fracture · fragment bodies", Meta: () => `${Host.Fracture.Props.filter((Prop) => Prop.State === "intact").length}/${Host.Fracture.Props.length}`,
          State: () => (Host.Fracture.Metrics.ActiveFragments > 0 ? "warm" : "live"), Build: (C, B) => BuildFractureGlobal(Host, C, B),
          Toggle: { Read: () => Host.Fracture.Settings.Enabled, Write: (On) => { Host.Fracture.Settings.Enabled = On; } },
          Switches: () => [Feature("Breakable", "Hammer", Host.Fracture.Settings, "Enabled"), Feature("Respawn", "RotateCcw", Host.Fracture.Settings, "Respawn")] }
    );
    for (const Prop of Host.Fracture.Props)
    {
        const Material = () => FractureMaterials[Prop.Recipe.Material];
        Entities.push({
            Id: `prop-${Prop.Index}`, Group: "Destruction", Name: Prop.Definition.Name, Type: "Breakable prop", Parent: "fracture",
            Glyph: Prop.Definition.Shape.Kind === "prism" ? "CircleDot" : Prop.Recipe.Material.includes("glass") || Prop.Recipe.Material === "tempered" ? "Gem" : "Box",
            Colour: Accent.Destruction, Description: "Per-object geometry · Fracture editor",
            Meta: () => (Prop.State === "intact" ? Material().Label : `${Prop.Pieces.length} pcs`),
            State: () => (Prop.State === "intact" ? "live" : "warm"),
            Toggle: { Read: () => Prop.Recipe.Enabled, Write: (On) => { Prop.Recipe.Enabled = On; } },
            Build: (C, B) => BuildProp(Host, Prop, C, B),
            Switches: () => [Feature("Breakable", "Hammer", Prop.Recipe, "Enabled"),
                             { Name: "Dynamic", Glyph: "Zap", Read: () => Prop.Recipe.Mode === FractureModes.Dynamic,
                               Write: (On) => Host.Fracture.AssignRecipe(Prop, { Mode: On ? FractureModes.Dynamic : FractureModes.Baked }) }]
        });
    }
    Entities.push(
        { Id: "derez", Group: "Effects", Name: "Derez · VFX", Type: "Particle effect", Glyph: "Sparkles", Colour: Accent.Effects,
          Description: "Particle Editor presets · recovery & respawn", Meta: () => Host.Effects.Settings.Preset,
          State: () => (Host.Recovery.Busy || Host.Effects.Metrics.Live > 0 ? "warm" : "live"), Build: (C, B) => BuildDerez(Host, C, B),
          Switches: () => [Feature("Auto recovery", "Shield", Host.Recovery.Settings, "Enabled")] },
        { Id: "training", Group: "Level", Name: "Training arena", Type: "Drills", Glyph: "Flag", Colour: Accent.Level,
          Description: "Ten timed stations · best times", Meta: () => (Host.Training.Active ? Host.Training.Active.Drill.Name : `${TrainingDrills.length} drills`),
          State: () => (Host.Training.Active && Host.Training.Active.Phase === "running" ? "warm" : "live"), Build: (C, B) => BuildTraining(Host, C, B) },
        { Id: "cones", Group: "Level", Name: "Cones", Type: "Rigid bodies", Glyph: "TrafficCone", Colour: Accent.Level, Parent: "training",
          Description: "Knockable cones · rez back on reset", Meta: () => `${Host.Cones.Cones.length}`,
          State: () => (Host.Cones.Metrics.Moving > 0 ? "warm" : "live"), Build: (C, B) => BuildCones(Host, C, B) },
        { Id: "lobby-session", Group: "Level", Name: "Lobby · Studio session", Type: "Multiplayer bay session", Glyph: "Lobby", Colour: Accent.Level,
          Description: "5-turntable studio · live players entering & leaving · rank plates",
          Meta: () => (Host.LobbySession?.QueryActive() ? `${Host.LobbySession.QueryOccupiedCount()}/5 bays` : "studio"),
          State: () => (Host.LobbySession?.QueryActive() ? "live" : ""), Build: (C, B) => BuildLobbySession(Host, C, B) },
        { Id: "level", Group: "Level", Name: Host.Lobby ? "Lobby · Studio" : "Proving grounds", Type: "Level", Glyph: "Map", Colour: Accent.Level,
          Description: "Level selector, zones, teleports & scene budget", Meta: () => (Host.Lobby ? "Studio" : `${ArenaZones.length} zones`), State: () => "live", Build: (C, B) => BuildLevel(Host, C, B) },
        { Id: "stress", Group: "Level", Name: "Stress test", Type: "Object pool", Glyph: "Boxes", Colour: Accent.Level,
          Description: "Pre-built pool · union, cards & draws under load", Meta: () => `${Host.Stress.QueryActive()}`,
          State: () => (Host.Stress.QueryActive() > 0 ? "warm" : ""), Build: (C, B) => BuildStress(Host, C, B) }
    );

    // Constructed 3D Primitives (Shift+A -> Cube / Pillar / Barrier, with Transform + Array Modifier)
    if (Host.ConstructedPrimitives?.length)
    {
        for (const Prim of Host.ConstructedPrimitives)
        {
            const Occ = Host.Scene?.Occupants?.[Prim.Occupant];
            Entities.push({
                Id: Prim.Id,
                Group: "Level",
                Name: Prim.Name,
                Type: `${Prim.Kind} primitive · Arrayable`,
                Glyph: "Box",
                Colour: Accent.Level,
                Description: "Constructed 3D scene primitive · supports GizmoPRO transform & Array Modifier",
                Meta: () =>
                {
                    const Arr = Host.ArrayModifiers?.get(Prim.Id);
                    return Arr?.Enabled ? `Array · ${Arr.Mode}` : Prim.Kind;
                },
                State: () => (Occ && !Occ.Inactive ? "live" : ""),
                Toggle: {
                    Read: () => !!(Occ && !Occ.Inactive),
                    Write: (On) => { if (Occ) { Occ.Inactive = !On; Host.Scene?.ConstructDrawRuns(); Host.EditorOccupantsDirty = true; } }
                },
                Build: (C, B) => BuildPrimitiveInspector(Host, Prim, C, B)
            });
        }
    }

    // Lobby Display Turntable Vehicles (selectable & arrayable in Lobby EDIT mode)
    if (Host.Lobby && Host.Scene?.GuestBays?.length)
    {
        Host.Scene.GuestBays.forEach((Bay, Idx) =>
        {
            const BayId = `lobby-bay-${Idx}`;
            const BodyOcc = Host.Scene.Occupants[Bay.Body];
            Entities.push({
                Id: BayId,
                Group: "Level",
                Parent: "lobby-session",
                Name: Bay.Name || `Turntable Vehicle ${Idx + 1}`,
                Type: "Studio bay vehicle",
                Glyph: "Car",
                Colour: Accent.Vehicle,
                Description: "Lobby turntable vehicle · selectable via GPU picking & transformable in Edit mode",
                Meta: () => (BodyOcc && !BodyOcc.Inactive ? "occupied" : "vacant"),
                State: () => (BodyOcc && !BodyOcc.Inactive ? "live" : ""),
                Toggle: {
                    Read: () => !!(BodyOcc && !BodyOcc.Inactive),
                    Write: (On) =>
                    {
                        for (const OccId of [Bay.Body, Bay.Plate, ...(Bay.Wheels || [])])
                            if (OccId >= 0 && Host.Scene.Occupants[OccId]) Host.Scene.Occupants[OccId].Inactive = !On;
                        Host.Scene.ConstructDrawRuns();
                        Host.PriorValid = false;
                    }
                },
                Build: (C, B) => BuildLobbyBayInspector(Host, BayId, Idx, C, B)
            });
        });
    }

    // Selectable Level Scene Shapes (spheres, mirrors, columns, ramps, barriers — selectable & gizmo-transformable in Edit mode)
    if (Host.Scene?.Occupants?.length)
    {
        const ShowcaseShapes = new Set(["AluminiumSphere", "GoldSphere", "GlassSphere", "MirrorSheet", "ChromeColumn", "ChromePanel", "Ramp", "Tower"]);
        Host.Scene.Occupants.forEach((Occ, OccIdx) =>
        {
            if (!Host.IsSelectableLevelShape?.(Occ))
                return;
            // Register showcase shapes in Outliner tree + any level shape currently selected via viewport click
            const ShapeId = `shape-${OccIdx}`;
            if (!ShowcaseShapes.has(Occ.Level) && Host.Shell?.Selected !== ShapeId)
                return;
            Entities.push({
                Id: ShapeId,
                Group: "Level",
                Name: Occ.Name || `${Occ.Level} ${OccIdx}`,
                Type: `Scene shape · ${Occ.Level}`,
                Glyph: "Box",
                Colour: Accent.Level,
                Description: "Scene shape object · click in Edit mode to move, rotate, scale, or array with GizmoPRO",
                Meta: () => Occ.Level,
                State: () => (!Occ.Inactive ? "live" : ""),
                Toggle: {
                    Read: () => !Occ.Inactive,
                    Write: (On) => { Occ.Inactive = !On; Host.Scene?.ConstructDrawRuns(); Host.EditorOccupantsDirty = true; Host.PriorValid = false; }
                },
                Build: (C, B) => BuildShapeInspector(Host, ShapeId, Occ, C, B)
            });
        });
    }

    return Entities;
}

function Feature(Name, Glyph, Target, Key)
{
    return { Name, Glyph, Read: () => !!Target[Key], Write: (On) => { Target[Key] = On; } };
}

function Slider(C, Parent, Target, Key, Label, Minimum, Maximum, Step, Unit = "", Digits = 2, Labels = null, OnInput = null)
{
    return C.Slider(Parent, Target, Key, { Label, Minimum, Maximum, Step, Unit, Digits, Labels, OnInput });
}

function GasSlider(C, Parent, Gas, Key, Labels = null)
{
    const Entry = GasParameterCatalogue[Key];
    const Digits = Entry.Step >= 1 ? 0 : Entry.Step >= 0.1 ? 1 : Entry.Step >= 0.01 ? 2 : 3;
    return C.Slider(Parent, Gas, Key, { Label: Entry.Label, Minimum: Entry.Minimum, Maximum: Entry.Maximum, Step: Entry.Step,
                                        Unit: Entry.Unit, Digits, Labels });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  RENDER SETTINGS
//------------------------------------------------------------------------------------------------------------------------

function BuildRender(Host, C, Body)
{
    const S = Host.Settings;
    const Arch = C.Card(Body, RendererLabels.bvh.Long, "Zap", { Accent: Accent.Render, Index: 1 });
    C.Metric(Arch, () => (S.RayTracing ? `${S.MaxBounces}B GI · ${S.ReflectionBounces}B Refl · M=${S.InitialCandidates}` : `SDF · ${S.MaxBounces}B GI · ${S.ReflectionBounces}B Refl`), "", 0,
             () => [S.RayTracing ? "BVH path tracing (software)" : "SDF distance field (multi-bounce)", "live"]);
    C.Muted(Arch, "Two-level BVH (TLAS over scene occupants + Binned-SAH BLAS per mesh) or toroidal SDF distance field with multi-bounce GI & reflections, RIS Next-Event Estimation, MIS, and Russian Roulette.");
    C.Segmented(Arch, S, "RayTracing", [[RendererLabels.bvh.Long, true], [RendererLabels.sdf.Long, false]], { Label: "Renderer architecture" });
    Slider(C, Arch, S, "MaxBounces", "GI path bounces", 1, 4, 1, "", 0, ["1 bounce", "4 bounces"]);
    Slider(C, Arch, S, "ReflectionBounces", "Reflection bounces", 1, 4, 1, "", 0, ["1 bounce", "4 bounces"]);
    Slider(C, Arch, S, "InitialCandidates", "Initial RIS path candidates", 1, 4, 1, "", 0, ["Fast", "Low variance"]);
    C.Switch(Arch, S, "RayTracedShadows", { Label: "Ray-traced direct & IES shadows", Detail: "Exact BVH visibility for sun cone, street lamps & headlights" });
    C.Switch(Arch, S, "RayTracedReflections", { Label: "Ray-traced multi-bounce reflections", Detail: "GGX VNDF & mirror BVH reflections across car paint, glass & wet asphalt (on by default)" });
    C.Switch(Arch, S, "MultiBounceNEE", { Label: "Multi-bounce NEE + RIS", Detail: "Direct sun, fire & IES light sampling at secondary path vertices" });
    C.Switch(Arch, S, "RussianRoulette", { Label: "Russian roulette termination", Detail: "Throughput-weighted unbiased path continuation" });
    C.Switch(Arch, S, "FireflyFilter", { Label: "Firefly & boiling suppression", Detail: "Jacobian & luminance outlier clamp" });

    const Enhanced = C.Card(Body, "ReSTIR PT Enhanced · 2026", "Sparkles", { Accent: Accent.Render, Index: 2 });
    C.Metric(Enhanced, () => (S.RayTracing && S.RestirEnhanced ? "Enhanced 2026 · 64B" : (S.RayTracing ? "Baseline 2022" : "SDF Mode")), "", 0,
             () => [`N=${S.SpatialCount} paired · R=${S.SpatialRadius}px`, S.RestirEnhanced ? "live" : "warm"]);
    C.Muted(Enhanced, "Algorithmic advances from Lin, Kettunen & Wyman (PACMCGIT May 2026, doi:10.1145/3804494): reciprocal Gaussian paired spatial reuse, scene-independent ray-footprint reconnection, 17×17 duplication maps, unified 64B diffuse+glossy reservoirs, and marginalized RGB vector shading.");
    C.Switch(Enhanced, S, "RestirEnhanced", { Label: "Enable ReSTIR PT Enhanced (2026)", Detail: "Toggle off to compare against 2022 ReSTIR PT baseline" });
    C.Switch(Enhanced, S, "PairedSpatialReuse", { Label: "§3 Reciprocal paired spatial reuse", Detail: "Self-inverting 254²/230²/210² Gaussian pairing maps (2 shifts/pair vs 4)" });
    C.Switch(Enhanced, S, "FootprintReconnection", { Label: "§4 Dual ray-footprint reconnection", Detail: "Scene-independent Eq. (5) footprint + single-vertex α ≥ 0.20 safeguard" });
    Slider(C, Enhanced, S, "FootprintConstant", "§4 Footprint threshold c", 0.002, 0.12, 0.002, "", 3, ["Reconnect early", "Postpone"]);
    C.Switch(Enhanced, S, "DuplicationMap", { Label: "§5 17×17 sample duplication map", Detail: "Adaptive c_Cap = lerp(M_max, 1, D^0.1) suppresses correlation blobs" });
    C.Switch(Enhanced, S, "UnifiedDiGi", { Label: "§6.1 Unified 64B diffuse + glossy reservoir", Detail: "Single path-tree reservoir shades both GI and multi-bounce reflections" });
    C.Switch(Enhanced, S, "ForcedNeeReconnect", { Label: "§6.2 Replay queue + forced NEE + initial RR", Detail: "Workgroup replay compaction, terminal light reconnect & external RR" });
    C.Switch(Enhanced, S, "VectorShading", { Label: "§6.3 Marginalized RGB vector shading", Detail: "Shades with Σ m_i F_rgb(Y_i) W_i |J_i| to eliminate chroma noise" });
    C.Switch(Enhanced, S, "DualMotionVectors", { Label: "§6.4 Dual motion vectors", Detail: "Static world-space reprojection fallback on dynamic disocclusions" });

    const Refl = C.Card(Body, "Ray-traced reflections", "Aperture", { Accent: Accent.Render, Index: 2 });
    C.Metric(Refl, () => (S.Reflection ? (S.RayTracing && S.RayTracedReflections ? `BVH · ${S.ReflectionBounces} bounce${S.ReflectionBounces > 1 ? "s" : ""}` : `SDF · ${S.ReflectionBounces} bounce${S.ReflectionBounces > 1 ? "s" : ""}`) : "Off"), "", 0,
             () => [`rough ≤ ${(S.ReflectionRoughness ?? 0.80).toFixed(2)}`, S.Reflection ? "live" : "warm"]);
    C.Muted(Refl, "Multi-bounce GGX VNDF glossy and mirror reflections traced against the Two-Level BVH with direct NEE, surface-card GI, and SVGF spatio-temporal reconstruction.");
    C.Switch(Refl, S, "Reflection", { Label: "Enable glossy & mirror reflections", Detail: "Screen-space reflection target + exterior mirror glass" });
    C.Switch(Refl, S, "RayTracedReflections", { Label: "Use BVH reflection rays", Detail: "Exact triangle hits instead of SDF sphere-tracing" });
    Slider(C, Refl, S, "ReflectionBounces", "Reflection bounces", 1, 4, 1, "", 0, ["Single bounce", "4 bounces"]);
    Slider(C, Refl, S, "ReflectionScale", "Reflection intensity", 0, 3, 0.05, "×", 2);
    Slider(C, Refl, S, "ReflectionRoughness", "Roughness cutoff", 0.15, 1.0, 0.05, "", 2, ["Mirror only", "All surfaces"]);

    const Quality = C.Card(Body, "Resolution & quality", "Monitor", { Accent: Accent.Render, Index: 3 });
    C.Metric(Quality, () => (Host.ScreenExtent ? `${Host.ScreenExtent[0]}×${Host.ScreenExtent[1]}` : "–"), "px", 0,
             () => [`trace ${Host.TraceExtent ? Host.TraceExtent.join("×") : "–"}`, "live"]);
    C.Muted(Quality, "Render target after the scale and the pixel ceiling of the quality grade; ReSTIR GI and reflections trace at the trace resolution.");
    C.Segmented(Quality, S, "Quality", [["Low", 0], ["Medium", 1], ["High", 2]], { Label: "Quality grade · reloads" });
    C.Segmented(Quality, S, "TraceScale", [["Full", 1], ["Half", 2], ["Quarter", 4]], { Label: "Trace resolution" });
    Slider(C, Quality, S, "RenderScale", "Render scale", 0.25, 2.0, 0.05, "×", 2, ["Faster", "Sharper"]);

    const Gather = C.Card(Body, "Global illumination · ReSTIR", "Sparkles", { Accent: Accent.Render, Index: 4 });
    C.Switch(Gather, S, "TemporalReuse", { Label: "Temporal reuse", Detail: "Reservoirs carried through motion vectors" });
    C.Switch(Gather, S, "SpatialReuse", { Label: "Spatial reuse", Detail: "Neighbouring reservoirs merged per pixel" });
    C.Switch(Gather, S, "VisibilityReuse", { Label: "Visibility validation", Detail: "Reused samples re-traced for occlusion" });
    C.Switch(Gather, S, "RadianceRefresh", { Label: "Refresh reused radiance" });
    Slider(C, Gather, S, "SpatialCount", "Spatial candidates", 0, 8, 1, "", 0);
    Slider(C, Gather, S, "SpatialRadius", "Spatial radius", 2, 48, 1, "px", 0);
    Slider(C, Gather, S, "TemporalTallyLimit", "Temporal M limit", 1, 40, 1, "", 0, ["Responsive", "Stable"]);
    Slider(C, Gather, S, "IndirectScale", "Indirect intensity", 0, 3, 0.05, "×", 2);

    const Svgf = C.Card(Body, "SVGF reconstruction quality", "Layers", { Accent: Accent.Render, Index: 5 });
    C.Muted(Svgf, "Edge-stopping à-trous wavelet passes and variance-guided history limits for diffuse GI and glossy reflections.");
    Slider(C, Svgf, S, "DiffuseWavelets", "Diffuse wavelet passes", 1, 5, 1, "", 0, ["Crisp", "Smooth"]);
    Slider(C, Svgf, S, "GlossyWavelets", "Glossy wavelet passes", 1, 4, 1, "", 0, ["Sharp", "Smooth"]);
    Slider(C, Svgf, S, "DiffuseColourSigma", "Diffuse luminance sigma", 1.0, 8.0, 0.25, "σ", 2);
    Slider(C, Svgf, S, "GlossyColourSigma", "Glossy luminance sigma", 1.0, 8.0, 0.25, "σ", 2);
    Slider(C, Svgf, S, "DiffuseTallyLimit", "Diffuse history frames", 4, 48, 2, "f", 0);
    Slider(C, Svgf, S, "GlossyTallyLimit", "Glossy history frames", 4, 32, 2, "f", 0);

    const Image = C.Card(Body, "Exposure & image", "Aperture", { Accent: Accent.Render, Index: 5 });
    C.Switch(Image, S, "AutoExposure", { Label: "Auto exposure" });
    Slider(C, Image, S, "ExposureCompensation", "Exposure compensation", -4, 4, 0.1, "EV", 1, ["Darker", "Brighter"]);
    C.Switch(Image, S, "DirectShadow", { Label: "Sun shadows", Detail: "Ray-traced BVH cone or cascaded shadow depth" });
    C.Switch(Image, S, "Reflection", { Label: "Glossy reflections", Detail: "BVH ray-traced or SDF-traced, SVGF denoised" });
    C.Switch(Image, S, "VarianceKernel", { Label: "Denoiser (SVGF)" });
    C.Switch(Image, S, "Antialias", { Label: "Temporal antialiasing", Detail: "Also resolves dithered tyre marks" });

    const Lights = C.Card(Body, "Clustered lights", "Lightbulb", { Accent: Accent.Render, Index: 6 });
    C.Switch(Lights, S, "LightGrid", { Label: "World light grid",
                                       Detail: "Each receiver tests only the lights whose reach touches its grid cell. Off tests every light (the reference; same image, slower)" });
    Slider(C, Lights, S, "LightGridReach", "Clustered light reach scale", 0.1, 1.0, 0.05, "×", 2);
    C.Switch(Lights, S, "IesProfiles", { Label: "IES photometric profiles",
                                         Detail: "Beams follow LM-63 Type C tables (Profiles/*.ies: authored demonstration curves; measured TILT=NONE files drop in). Off: the analytic beam shapes" });
    C.Switch(Lights, S, "EmissiveLights", { Label: "Emissive clusters (BVH)",
                                            Detail: "The second light structure: emissive triangles (lamp lenses, tunnel strips, signals) sampled as lights by next-event estimation with exact area pdfs, partitioned with the BSDF hit. Ray tracing only" });
    C.Stats(Lights, [["clusters", () => Host.Emissive ? Host.Emissive.Statistics.Instances : 0, "", 0], ["triangles", () => Host.Emissive ? Host.Emissive.Statistics.Triangles : 0, "", 0],
                     ["capped cells", () => Host.Emissive ? Host.Emissive.Statistics.TruncatedCells : 0, "", 0]], 3);
    C.Muted(Lights, "Point, street, beam and flood lights are binned into a 16 x 16 x 4 world grid of 32-bit masks. The same grid serves the direct pass, the card relighting and every path vertex of the ray tracer.");

    const Screen = C.Card(Body, "Screen-space traces", "Scan", { Accent: Accent.Render, Index: 7 });
    C.Switch(Screen, S, "ScreenTraversal", { Label: "Screen traces (master gate)",
                                            Detail: "Walks the nearest-depth pyramid first; only rays the screen cannot settle go on to the BVH or the distance field. Enabling it restarts temporal reuse once" });
    C.Switch(Screen, S, "ScreenIndirect", { Label: "Screen traces for GI candidates", Detail: "Needs the master gate" });
    C.Switch(Screen, S, "ScreenReflection", { Label: "Screen traces for reflections", Detail: "Needs the master gate" });
    C.Muted(Screen, "Each gate applies only when the master gate is on. A miss resumes in the BVH path tracer, or in the distance field in SDF mode.");

    const Debug = C.Card(Body, "Debug views", "Bug", { Accent: Accent.Render, Index: 8 });
    C.Select(Debug, S, "Inspection", InspectionModes, { Label: "Display" });
    C.Select(Debug, S, "DistanceInspection", DistanceInspections, { Label: "BVH / SDF view" });
    C.Switch(Debug, S, "HudVisible", { Label: "Readout overlay", Detail: "H" });
    C.Switch(Debug, S, "InsetEnabled", { Label: "Distance slice inset", Detail: "J" });
    Slider(C, Debug, S, "SliceHeight", "Slice height", 0.05, 4.0, 0.05, "m", 2);

    const Timing = C.Card(Body, "Frame timing", "Activity", { Accent: Accent.Render, Index: 9 });
    C.Metric(Timing, () => 1.0 / Math.max(Host.FrameSeconds || 1, 1.0e-4), "fps", 0, () => [`1% low ${Host.LowRate || "–"}`, ""]);
    C.Stats(Timing, [["Frame", () => (Host.FrameSeconds || 0) * 1000.0, "ms", 1], ["Draw runs", () => Host.Scene.DrawRuns.length],
                     ["TLAS instances", () => (Host.RayTracing ? Host.RayTracing.ActiveInstances : 0)]]);
    C.Readout(Timing, () =>
    {
        if (!Host.Metrics.Enabled)
            return ["GPU pass timings need the timestamp-query feature;", "this adapter does not expose it."];
        return Object.entries(Host.Metrics.Durations).filter(([, Milliseconds]) => Milliseconds > 0.0)
                     .map(([Name, Milliseconds]) => `${Name.padEnd(24)}${Milliseconds.toFixed(2).padStart(7)} ms`);
    });

    const Log = C.Card(Body, "Hardware log", "Cpu", { Accent: Accent.Render, Index: 10 });
    C.Muted(Log, `Captures ${CaptureFrameCount} frames per configuration on this machine, then builds a plain-text log to copy. Nothing is saved to disk. `
                 + "The matrix runs the distance field, the triangle BVH reflections and the screen traces in turn.");
    C.Readout(Log, () => Host.Benchmark.StatusLines());
    const LogRow = C.ActionRow(Log);
    C.Button(LogRow, "Capture current", "Activity", () => Host.Benchmark.Begin(Host, [{ Name: "Current settings", Settings: {} }]));
    C.Button(LogRow, "Capture matrix", "Layers", () => Host.Benchmark.Begin(Host, CaptureMatrix));
    C.Button(LogRow, "Copy log", "Check", () => Host.Benchmark.Copy());
}

function BuildField(Host, C, Body)
{
    const S = Host.Settings;
    if (Host.RayTracing)
    {
        const Bvh = C.Card(Body, "Two-Level BVH (TLAS + BLAS)", "Zap", { Accent: Accent.Render, Index: 1 });
        C.Metric(Bvh, () => Host.RayTracing.ActiveInstances, "instances", 0,
                 () => [`${Host.RayTracing.TotalTriangles} triangles`, S.RayTracing ? "live" : "warm"]);
        C.Stats(Bvh, [["TLAS nodes", () => Host.RayTracing.UsedTlasNodes],
                      ["BLAS nodes", () => Math.max(0, Host.RayTracing.StaticNodeCount - 4608)],
                      ["Triangles", () => Host.RayTracing.TotalTriangles]]);
        C.Muted(Bvh, "TLAS is rebuilt over active occupants each frame; per-mesh Binned-SAH BLAS hierarchies are built at bring-up and extended dynamically on fracture.");
    }
    const Field = C.Card(Body, "Cascades", "GridLattice", { Accent: Accent.Render, Index: 2 });
    C.Metric(Field, () => Host.Distance.TotalBricks, "bricks", 0, () => [S.FreezeDistance ? "frozen" : "updating", S.FreezeDistance ? "warm" : "live"]);
    C.Muted(Field, "Bricks rewritten since start: scrolling windows and moving occupants only.");
    C.Switch(Field, S, "FreezeDistance", { Label: "Freeze field updates" });
    C.Select(Field, S, "DistanceInspection", DistanceInspections, { Label: "Field view (display: global distance field)" });
    C.Switch(Field, S, "InsetEnabled", { Label: "Distance slice inset" });
    Slider(C, Field, S, "SliceHeight", "Slice height", 0.05, 4.0, 0.05, "m", 2);
    C.Readout(Field, () => Host.QueryFieldLines());
}

function BuildCards(Host, C, Body)
{
    const S = Host.Settings;
    const Cards = C.Card(Body, "Surface cache", "LayoutGrid", { Accent: Accent.Render, Index: 1 });
    C.Metric(Cards, () => Host.Cards.ScheduledTiles, "tiles", 0, () => ["relit this frame", ""]);
    C.Switch(Cards, S, "CardRadiosity", { Label: "Card radiosity", Detail: "Multi-bounce inside the cache" });
    Slider(C, Cards, S, "CardTileBudget", "Static tiles per frame", 0, 8192, 64, "", 0, ["Lazy", "Eager"]);
    C.Readout(Cards, () => Host.QueryDepotLines());
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      SUN
//------------------------------------------------------------------------------------------------------------------------

function BuildSun(Host, C, Body)
{
    const S = Host.Settings;
    const Preset = C.Card(Body, "Lighting preset", "Sunrise", { Accent: Accent.Environment, Index: 1 });
    C.Segmented(Preset, S, "LightingPreset", Object.keys(LightingPresets).map((Name) => [Name, Name]),
                { OnChange: (Name) => Host.ApplyPreset(Name) });
    C.Muted(Preset, "Night drops the sun below the horizon: the lamps, the hangar strips and the tunnel light the course through the cards.");

    const Direction = C.Card(Body, "Sun direction", "Orbit", { Accent: Accent.Environment, Index: 2 });
    C.Metric(Direction, () => S.SunElevation, "°", 1, () => [S.SunElevation < 0 ? "below horizon" : "above horizon", S.SunElevation < 0 ? "warm" : "live"]);
    C.Instrument(Direction, 92, (Context, Width, Height) => DrawSunArc(Context, Width, Height, S.SunElevation, S.SunAzimuth), true);
    Slider(C, Direction, S, "SunElevation", "Elevation", -20, 85, 0.5, "°", 1, ["Night", "Zenith"]);
    Slider(C, Direction, S, "SunAzimuth", "Azimuth", -180, 180, 1, "°", 0);

    const Intensity = C.Card(Body, "Light intensity", "Lightbulb", { Accent: Accent.Environment, Index: 3 });
    Slider(C, Intensity, S, "SolarIlluminance", "Solar intensity", 1, 30, 0.5, "", 1);
    Slider(C, Intensity, S, "SkyScale", "Sky intensity", 0, 3, 0.05, "×", 2);
    Slider(C, Intensity, S, "EmissionScale", "Emitters (lamps, flames)", 0, 4, 0.05, "×", 2);
}

function DrawSunArc(Context, Width, Height, Elevation, Azimuth)
{
    const Horizon = Height * 0.78, Radius = Height * 0.62, Centre = Width * 0.5;
    Context.strokeStyle = "#3a3a3a";
    Context.lineWidth = 2;
    Context.beginPath();
    Context.moveTo(24, Horizon);
    Context.lineTo(Width - 24, Horizon);
    Context.stroke();
    Context.setLineDash([6, 8]);
    Context.beginPath();
    Context.arc(Centre, Horizon, Radius, Math.PI, 2.0 * Math.PI);
    Context.stroke();
    Context.setLineDash([]);
    const Angle = Math.PI - (Elevation + 20.0) / 105.0 * Math.PI;
    const X = Centre + Math.cos(Angle) * Radius, Y = Horizon - Math.sin(Angle) * Radius;
    const Glow = Context.createRadialGradient(X, Y, 2, X, Y, 34);
    Glow.addColorStop(0, Elevation > 0 ? "#ffe3a0" : "#9fb0d8");
    Glow.addColorStop(1, "rgba(0,0,0,0)");
    Context.fillStyle = Glow;
    Context.beginPath();
    Context.arc(X, Y, 34, 0, 2.0 * Math.PI);
    Context.fill();
    Context.fillStyle = Elevation > 0 ? "#ffd27a" : "#c3cff0";
    Context.beginPath();
    Context.arc(X, Y, 9, 0, 2.0 * Math.PI);
    Context.fill();
    Context.fillStyle = "#7d7d7d";
    Context.font = "20px DM Sans, sans-serif";
    Context.fillText(`azimuth ${Azimuth.toFixed(0)}°`, 26, Height - 12);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     VEHICLE
//------------------------------------------------------------------------------------------------------------------------

function BuildVehicle(Host, C, Body)
{
    BuildTransformCard(Host, "vehicle", C, Body, { AllowScale: false, AccentColour: Accent.Vehicle });
    const S = Host.Settings;
    const Telemetry = () => Host.Vehicle.QueryTelemetry();
    const Drive = C.Card(Body, "Telemetry", "Gauge", { Accent: Accent.Vehicle, Index: 1 });
    C.Metric(Drive, () => Math.abs(Telemetry().Speed * 3.6), "km/h", 0, () => [`gear ${Telemetry().Gear}`, ""]);
    C.Stats(Drive, [["Engine", () => Telemetry().EngineRPM, "rpm"], ["Boost", () => Telemetry().Boost, "bar", 2],
                    ["Contact", () => `${Telemetry().Contacts}/4`]]);

    C.Switch(Drive, S, "SteeringAssist", { Label: "Countersteer assist", Detail: "Speed-aware slip correction; player steering takes priority" });
    Slider(C, Drive, S, "SteeringAssistStrength", "Assist strength", 0, 1, 0.05, "", 2, ["Off", "Strong"]);

    const Burnout = C.Card(Body, "Burnout · line lock", "Flame", { Accent: Accent.Effects, Index: 2 });
    C.Metric(Burnout, () => Math.abs(Telemetry().RearSpin * 3.6), "km/h wheelspin", 0,
             () => (Host.Vehicle.Burnout ? ["LINE LOCK ON", "warm"] : [S.LineLock ? "ready" : "off", S.LineLock ? "live" : ""]));
    C.Muted(Burnout, "Hold W and S together at a crawl: line lock clamps only the front calipers (their static friction holds the car), the rears spin against them on the limiter and pour their power into the tread — smoke, heat, rubber. Lift S to launch.");
    C.Switch(Burnout, S, "LineLock", { Label: "Line lock (W + S)", Detail: "Front brakes only, gear held" });
    C.Stats(Burnout, [["Rear left", () => Host.Thermal.Wheels[2].Surface - 273.15, "°C"], ["Rear right", () => Host.Thermal.Wheels[3].Surface - 273.15, "°C"],
                      ["Ignited", () => Host.Thermal.Wheels.filter((Wheel) => Wheel.Ignited).length]]);

    const Names = ["FL", "FR", "RL", "RR"];
    const Tyres = C.Card(Body, "Tyres", "CircleDot", { Accent: Accent.Vehicle, Index: 3, Subtitle: "Volumetric tyre physics · pressure · wear · failure" });
    C.Stats(Tyres, [0, 1, 2, 3].map((Wheel) => [Names[Wheel], () => Host.Thermal.Wheels[Wheel].Surface - 273.15, "°"]), 4);
    C.Stats(Tyres, [0, 1, 2, 3].map((Wheel) => [Names[Wheel] + " kPa", () => Host.Thermal.Wheels[Wheel].GaugePressure / 1000.0]), 4);
    C.Stats(Tyres, [0, 1, 2, 3].map((Wheel) => [Names[Wheel] + " mm", () => Host.Vehicle.Wheels[Wheel].TyreDeflection * 1000.0, "", 0]), 4);
    C.Stats(Tyres, [0, 1, 2, 3].map((Wheel) => [Names[Wheel] + " tread", () => Host.Thermal.QueryTreadFractions()[Wheel] * 100.0, "%"]), 4);
    C.Readout(Tyres, () => [Host.QueryTyreLive().States.map((State, Wheel) => `${Names[Wheel]} ${State || "ok"}`).join(" · ")]);
    C.Switch(Tyres, S, "TyreCarcass", { Label: "XPBD tyre carcass (GPU)",
                                       Detail: "A 1024-particle pneumatic lattice per tyre (cords, volume row, shape matching), 32 substeps a frame, sets each tyre's vertical stiffness and contact patch. Off: the analytic carcass spring" });
    C.Stats(Tyres, [0, 1, 2, 3].map((Wheel) => [Names[Wheel] + " XPBD kN", () => (Host.TyreCarcass ? Host.TyreCarcass.Readings[Wheel].Load : 0.0) / 1000.0, "", 2]), 4);
    C.Stats(Tyres, [0, 1, 2, 3].map((Wheel) => [Names[Wheel] + " patch cm", () => (Host.TyreCarcass ? Host.TyreCarcass.Readings[Wheel].PatchLength : 0.0) * 100.0, "", 0]), 4);
    C.Stats(Tyres, [0, 1, 2, 3].map((Wheel) => [Names[Wheel] + " kN/m", () => (Host.Vehicle.Wheels[Wheel].CarcassSecant || 0.0) / 1000.0, "", 0]), 4);
    C.Muted(Tyres, "XPBD soft-body carcass on the GPU (the Slate engine's six TyreSolver kernels, 16 × 64 particles, 3712 cords, pressure-volume row, shape block). The rim is driven by the vehicle, the load comes back two frames late and the vehicle integrates against the secant stiffness it implies. The vertex shader draws the rubber at the carcass's deflection with its patch flattening, sidewall bulge and shear.");
    C.Switch(Tyres, S, "TyreCarcass", { Label: "XPBD tetrahedral tyre carcass", Detail: "GPU finite-volume inflation & contact lattice" });
    Slider(C, Tyres, S, "TyreSubsteps", "XPBD carcass substeps", 8, 32, 4, "", 0, ["Fast", "Stiff"], () => Host.ApplySetting("TyreCarcass"));
    Slider(C, Tyres, S, "TyrePressureFront", "Front cold pressure", 0, 400, 5, "kPa", 0, ["Flat", "Over"], () => Host.ApplySetting("TyrePressure"));
    Slider(C, Tyres, S, "TyrePressureRear", "Rear cold pressure", 0, 400, 5, "kPa", 0, ["Flat", "Over"], () => Host.ApplySetting("TyrePressure"));
    C.Switch(Tyres, S, "TyreWear", { Label: "Tread wear", Detail: "Sliding work abrades the tread, hotter faster" });
    Slider(C, Tyres, S, "WearScale", "Wear rate", 0.5, 40, 0.5, "×", 1, ["Real", "Demo"]);
    C.Switch(Tyres, S, "TyreFailure", { Label: "Failure", Detail: "Worn through, cooked or over-pressured tyres burst" });
    C.Select(Tyres, S, "IgnitionMode", IgnitionOptions, { Label: "Tyre ignition" });
    C.Switch(Tyres, S, "ThermalGrip", { Label: "Thermal grip", Detail: "Hot or under-inflated tyres lose grip" });
    const Damage = { Wheel: 0 };
    C.Segmented(Tyres, Damage, "Wheel", Names.map((Name, Index) => [Name, Index]), { Label: "Damage a tyre" });
    const DamageRow = C.ActionRow(Tyres);
    C.Button(DamageRow, "Puncture", "Droplets", () => Host.PunctureTyre(Damage.Wheel, false));
    C.Button(DamageRow, "Blowout", "Bomb", () => Host.PunctureTyre(Damage.Wheel, true), "danger");
    C.Button(DamageRow, "Shred", "Zap", () => { Host.PunctureTyre(Damage.Wheel, true); Host.Vehicle.ShredTyre(Damage.Wheel); }, "danger");
    const Row = C.ActionRow(Tyres);
    C.Button(Row, "Fresh tyres", "Droplets", () => Host.ApplySetting("CoolTyres"));
    C.Button(Row, "Inspect FL", "Camera", () => Host.AssignCameraMode("Tyre FL"));
    C.Button(Row, "Respawn", "RotateCcw", () => Host.RespawnVehicle());

    const SparkStyle = C.Card(Body, "Spark variation", "Zap", { Accent: Accent.Vehicle, Index: 5 });
    Slider(C, SparkStyle, Host.Sparks.Settings, "Variation", "Fragment / burst variation", 0, 1, 0.05, "×", 2);
    C.Muted(SparkStyle, "Mostly fine filings, occasional heavier chips, irregular contact bursts and varied streaks. Applies to newly emitted sparks; collision and cooling remain active.");

    const LightingCard = C.Card(Body, "Automotive lighting", "Sun", { Accent: Accent.Vehicle, Index: 6 });
    const ModeNames = [["Off", AutomotiveMode.Off], ["Low beam", AutomotiveMode.LowBeam], ["High beam", AutomotiveMode.HighBeam], ["Fog", AutomotiveMode.Fog]];
    C.Segmented(LightingCard, { Mode: Host.Lighting ? Host.Lighting.VehicleLightMode : AutomotiveMode.LowBeam }, "Mode", ModeNames, {
        Label: "Headlights (hotkey I)",
        OnChange: (M) => {
            if (Host.Lighting) {
                Host.Lighting.SetVehicleMode(M);
                Host.Cards?.InvalidateLighting();
                Host.PriorValid = false;
            }
        }
    });
    C.Switch(LightingCard, { Enabled: Host.Lighting ? Host.Lighting.RoofLightEnabled : false }, "Enabled", {
        Label: "Auxiliary roof light (hotkey L)",
        Detail: "Multi-lens forward flood",
        OnChange: (On) => {
            if (Host.Lighting) {
                Host.Lighting.RoofLightEnabled = On;
                Host.Cards?.InvalidateLighting();
                Host.PriorValid = false;
            }
        }
    });
    const FixtureOptions = [["Slim LED lightbar", RoofFixtureType.SlimLED], ["Dual rally pods", RoofFixtureType.RallyPod]];
    C.Segmented(LightingCard, { Fixture: Host.Lighting ? Host.Lighting.RoofFixture : RoofFixtureType.SlimLED }, "Fixture", FixtureOptions, {
        Label: "Rooftop auxiliary fixture",
        OnChange: (F) => {
            if (Host.Lighting) {
                Host.Lighting.SetRoofFixture(F);
                Host.Cards?.InvalidateLighting();
                Host.PriorValid = false;
            }
        }
    });
    C.Muted(LightingCard, "Light sources with analytical beam profiles (roadway, low and high beam, fog, floodlight), coupled in real time to surface cards for ReSTIR GI multi-bounce radiosity.");

    const Camera = C.Card(Body, "Camera", "Camera", { Accent: Accent.Vehicle, Index: 4 });
    C.Segmented(Camera, { Free: Host.FreeCamera }, "Free", [["Car rigs", false], ["Free (F8)", true]], { OnChange: (Free) => Host.AssignCamera(Free) });
    C.Select(Camera, S, "CameraMode", Object.keys(CameraRigs).map((Name) => [Name, Name]), { Label: "Rig (C cycles)", OnChange: (Mode) => Host.AssignCameraMode(Mode) });
    const OrbitRow = C.ActionRow(Camera);
    C.Button(OrbitRow, "Orbit vehicle", "Camera", () => Host.AssignCameraMode("Orbit"));
    C.Button(OrbitRow, "Reset orbit", "RotateCcw", () => { Host.Orbit.Reset(); Host.PriorValid = false; });
    C.Muted(Camera, "Orbit: drag for 360° rotation, scroll to zoom. View angle stays where you leave it—even as the car turns. No chase recentering, shake or speed FOV.");
    C.Switch(Camera, S, "CameraShake", { Label: "Driving camera shake" });
    Slider(C, Camera, S, "CameraShakeStrength", "Shake strength", 0, 1, 0.05, "×", 2);
    C.Switch(Camera, S, "SpeedFov", { Label: "Speed-based FOV" });
    Slider(C, Camera, S, "SpeedFovBoost", "FOV boost at speed", 0, 24, 1, "°", 0);
    C.Muted(Camera, "Shake and speed FOV apply to driving cameras only; the HMI touch view and tyre inspection stay steady.");
    C.Muted(Camera, "Drag to orbit the chase camera, wheel to zoom; inside the car drag to look around and click the tablet to use it. Free camera: WASD, Q/E, Shift.");
}

function BuildCabin(Host, C, Body)
{
    const S = Host.Settings, Cabin = Host.Cabin, Settings = Cabin.Settings;
    const Screens = C.Card(Body, "HMI", "Monitor", { Accent: Accent.Vehicle, Index: 1, Subtitle: "Experimental/TabletPanel, live in the cabin" });
    C.Muted(Screens, "The accepted TabletPanel prototype — launcher, twelve applications, five launcher designs — and its hexagon cluster draw into their own canvases; "
                   + "here they are textures on the centre-stack tablet and the wheel-mounted hexagonal display. The cluster and the Telemetry and Tyres applications read the solver; "
                   + "the Tyres slider pumps the tyres; the Garage shows this car, rendered live by the exhibit's own studio pass. From the cockpit, click the tablet.");
    C.Switch(Screens, Settings, "Screens", { Label: "Screens on" });
    Slider(C, Screens, Settings, "Brightness", "Screen brightness", 0, 1, 0.05, "×", 2);
    Slider(C, Screens, S, "HmiScale", "Tablet / HMI size (not cluster)", 0.75, 2.5, 0.05, "×", 2);
    Slider(C, Screens, S, "CockpitFov", "Cockpit field of view / zoom", 30, 90, 1, "°", 0);
    const FocusRow = C.ActionRow(Screens);
    C.Button(FocusRow, "Focus HMI · touch (M)", "Camera", () => Host.AssignCameraMode("HMI"));
    C.Button(FocusRow, "Reset HMI size / zoom", "RotateCcw", () => { S.HmiScale = 1; S.HmiZoom = 1; S.CockpitFov = 70; C.Synchronise(); });
    C.Muted(Screens, "HMI camera frames the tablet for touch. Scroll to zoom in HMI/cockpit views. Tablet size scales its housing, screen and touch area only; the cluster stays fixed.");
    const Apps = C.ActionRow(Screens);
    for (const [Caption, Application] of [["Home", null], ["Garage", "garage"], ["Tyres", "tyres"], ["Telemetry", "telemetry"]])
        C.Button(Apps, Caption, null, () => { if (!Cabin.Screen) return; if (Application) Cabin.Screen.PresentApplication(Application); else Cabin.Screen.PresentHome(); });
    C.Readout(Screens, () => Cabin.QueryReadout());

    const FinishCard = C.Card(Body, "Body finish & surface texture", "Palette", { Accent: Accent.Vehicle, Index: 2, Subtitle: "Live on HMI Garage & 3D world car" });
    C.Muted(FinishCard, "Applies 3D expanding paint ripples and procedural surface micro-textures (Smooth Clearcoat, Carbon Twill, Brushed Grain, Hex Ceramic) simultaneously to the tablet preview, cabin mirrors, and world vehicle.");
    if (Host.Appearance)
    {
        C.Segmented(FinishCard, Host.Appearance, "SurfaceTexture",
            SurfaceTextures.map((Tex) => [Tex.Short, Tex.Id]),
            { Label: "Body surface texture", OnChange: (TexId) => { Host.Appearance.AssignSurfaceTexture(TexId); if (Cabin.Screen) Cabin.Screen.Dirty = true; Host.PriorValid = false; } });
        const PaintChoices = PaintOptions.map((Paint) => [Paint.Name, Paint.Hex]);
        C.Select(FinishCard, { Hex: Host.Appearance.Colour }, "Hex", PaintChoices, {
            Label: "Paint finish (ripple)",
            OnChange: (Hex) =>
            {
                const Chosen = PaintOptions.find((Option) => Option.Hex === Hex);
                if (Chosen)
                {
                    if (Cabin.Screen)
                    {
                        Cabin.Screen.FinishColour = Chosen.Hex;
                        Cabin.Screen.BeginPaint(Chosen, null);
                    }
                    else
                    {
                        Host.Appearance.BeginRipple(Chosen);
                    }
                }
            }
        });
    }

    const Size = C.Card(Body, "HUD size", "MoveSpace", { Accent: Accent.Vehicle, Index: 2, Subtitle: "On-screen readout only · cabin stays fixed" });
    C.Muted(Size, "Resize and place the on-screen HUD/readout, not the dashboard, steering wheel, seats or camera. H toggles the HUD.");
    C.Switch(Size, S, "HudVisible", { Label: "Show HUD", OnChange: () => Host.ApplySetting("HudVisible") });
    Slider(C, Size, S, "HudScale", "HUD scale", 0.6, 1.8, 0.05, "×", 2, ["Smaller", "Larger"]);
    Slider(C, Size, S, "HudAcross", "HUD across", -200, 200, 5, "px", 0);
    Slider(C, Size, S, "HudDown", "HUD down", -200, 200, 5, "px", 0);
    const SizeRow = C.ActionRow(Size);
    C.Button(SizeRow, "Reset HUD", "RotateCcw", () => { Object.assign(S, { HudScale: 1, HudAcross: 0, HudDown: 0 }); C.Synchronise(); });
    C.Button(SizeRow, "Driver's view", "Camera", () => Host.AssignCameraMode("Cockpit"));

    const Glass = C.Card(Body, "Glass & mirrors", "Eye", { Accent: Accent.Vehicle, Index: 3 });
    C.Muted(Glass, "See-through glazing leaves the opaque raster and is drawn over what is behind it — the sky's Fresnel reflection and a sun glint, 72 % transmission; "
                 + "it casts no shadow, so the sun reaches the cabin. Each mirror renders the driver's eye reflected in its plane through an off-axis frustum fitted to "
                 + "its glass (a small lit forward pass), one mirror per frame while they can be seen.");
    C.Switch(Glass, Settings, "SeeThroughGlass", { Label: "See-through glass" });
    Slider(C, Glass, Settings, "GlassTransmission", "Transmission", 0.2, 0.95, 0.01, "", 2, ["Limo tint", "Clear"]);
    C.Segmented(Glass, Settings, "Mirrors", [["Off", 0], ["When visible", 1], ["Always", 2]], { Label: "Mirror views" });

    const Views = C.Card(Body, "Interior cameras & cockpit exposure", "Camera", { Accent: Accent.Vehicle, Index: 4 });
    C.Select(Views, S, "CameraMode", [["Vehicle Camera (Parented)", "VehicleCamera"], ...Object.keys(CameraRigs).map((Name) => [Name, Name])],
             { Label: "Rig (C cycles)", OnChange: (Mode) => Host.AssignCameraMode(Mode) });
    C.Switch(Views, S, "CockpitExposure", { Label: "Windshield-weighted cockpit metering",
                                            Detail: "Prevents dark dashboard upholstery from over-boosting auto-exposure when viewing inside the cabin" });
    Slider(C, Views, S, "CockpitExposureBias", "Cockpit exposure bias", -2.5, 1.5, 0.05, "EV", 2, ["Crisp exterior", "Bright dash"]);
    const Row = C.ActionRow(Views);
    C.Button(Row, "Driver", "Camera", () => Host.AssignCameraMode("Cockpit"));
    C.Button(Row, "Passenger", "Camera", () => Host.AssignCameraMode("Passenger"));
    C.Button(Row, "Parented 3D", "Camera", () => Host.AssignCameraMode("VehicleCamera"));
    C.Button(Row, "Orbit", "Camera", () => Host.AssignCameraMode("Orbit"));
}

function BuildMarks(Host, C, Body)
{
    const S = Host.Settings;
    const Marks = C.Card(Body, "Rubber deposit", "CircleDot", { Accent: Accent.Vehicle, Index: 1 });
    C.Metric(Marks, () => Host.Marks.Metrics.LaidMetres, "m laid", 0, () => [S.TyreMarks ? "recording" : "off", S.TyreMarks ? "live" : ""]);
    C.Muted(Marks, "Each texel gains the sliding work per unit area a tyre does on it (60 kJ/m² is fully black): a burnout blackens its patch in a moment, a drift leaves grey arcs that darken pass after pass. A second channel records where across the tread each texel lies, so every mark has the tread's exact edges and its four grooves, far sharper than the 5.5 cm texels. Wheels riding on debris lay nothing.");
    C.Switch(Marks, S, "TyreMarks", { Label: "Lay rubber" });
    Slider(C, Marks, S, "TyreMarkStrength", "Deposit strength", 0, 4, 0.05, "×", 2, ["Faint", "Heavy"]);
    const Row = C.ActionRow(Marks);
    C.Button(Row, "Clear marks", "Trash2", () => Host.Marks.Clear(), "danger");
    C.Readout(Marks, () => Host.Marks.QueryReadout());
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SMOKE + FIRE
//------------------------------------------------------------------------------------------------------------------------

function BuildGas(Host, C, Body)
{
    const S = Host.Settings, Gas = S.Gas;
    const Domain = C.Card(Body, "Domain", "Box", { Accent: Accent.Effects, Index: 1, Subtitle: "Toroidal lattice following the car" });
    C.Metric(Domain, () => Host.Smoke.Extent.join("×"), "cells", 0, () => (Host.Smoke.Resident ? ["resident", "live"] : ["idle", ""]));
    C.Muted(Domain, "The requested Slate plume: GPU coarse flow carrying fine smoke/heat/flame/soot, toroidal windows, 60 Hz transport and a coverage-clipped march. No passes run while idle.");
    C.Stats(Domain, [["Sources", () => Host.Smoke.Metrics.Sources], ["Ticks", () => Host.Smoke.Metrics.Ticks], ["Relocations", () => Host.Smoke.Metrics.Relocations]]);
    C.Muted(Domain, "Slate 3c987f3 plume: toroidal transport, vorticity, warm-started pressure, detailed multi-scattering; marched at the GI trace resolution before TAA.");
    C.Segmented(Domain, S, "TestPlume", [["Off", 0], ["Smoke plume", 1], ["Fire", 2]], { Label: "Test emitter · 3 m ahead of the car" });
    const Row = C.ActionRow(Domain);
    C.Button(Row, "Clear smoke", "Trash2", () => Host.ApplySetting("ClearSmoke"));
    C.Button(Row, "Defaults", "RotateCcw", () => { Object.assign(Gas, ConstructGasParameters()); C.Synchronise(); });

    const Source = C.Card(Body, "Source", "Droplets", { Accent: Accent.Effects, Index: 2, Subtitle: "Multipliers on the tyre model" });
    for (const Key of ["Emission", "Heat", "Soot"])
        GasSlider(C, Source, Gas, Key);

    const Simulation = C.Card(Body, "Simulation", "Wind", { Accent: Accent.Effects, Index: 3 });
    C.Instrument(Simulation, 86, (Context, Width, Height) => DrawDecay(Context, Width, Height, Gas), true);
    for (const Key of ["Buoyancy", "Dissipation", "Cooling", "Vorticity", "Turbulence", "TurbulenceScale"])
        GasSlider(C, Simulation, Gas, Key);
    GasSlider(C, Simulation, Gas, "WindSpeed", ["Still", "Gale"]);
    GasSlider(C, Simulation, Gas, "WindHeading");
    GasSlider(C, Simulation, Gas, "TimeScale", ["Slow motion", "Fast"]);
    GasSlider(C, Simulation, Gas, "PressureSweeps");

    const Appearance = C.Card(Body, "Appearance", "CloudFog", { Accent: Accent.Effects, Index: 4 });
    for (const Key of ["Density", "Albedo", "Phase", "MultiScatter", "Ambient", "Detail", "DetailScale"])
        GasSlider(C, Appearance, Gas, Key);

    const Fire = C.Card(Body, "Fire", "Flame", { Accent: Accent.Effects, Index: 5, Subtitle: "Combustion of the tyre" });
    C.Instrument(Fire, 54, (Context, Width, Height) => DrawBlackbody(Context, Width, Height, Gas), true);
    C.Select(Fire, S, "IgnitionMode", IgnitionOptions, { Label: "Ignition" });
    for (const Key of ["FlameIntensity", "FlameCore", "FlameTip", "FlameTemperature", "FlameLoss", "FireLight"])
        GasSlider(C, Fire, Gas, Key);

    const Presets = C.Card(Body, "Presets", "Sparkles", { Accent: Accent.Effects, Index: 6 });
    const PresetRow = C.ActionRow(Presets);
    for (const [Name, Values] of Object.entries(GasPresets))
        C.Button(PresetRow, Name, null, () => { Object.assign(Gas, ConstructGasParameters(), Values); C.Synchronise(); });
    C.Readout(Presets, () => Host.Smoke.QueryReadout(Host.Thermal));
}

// 📐 Survival of density and heat after emission: (1 − loss)^t, with the rise of a warm parcel under the buoyancy.
function DrawDecay(Context, Width, Height, Gas)
{
    const Left = 18, Right = Width - 18, Top = 18, Bottom = Height - 26;
    Context.strokeStyle = "#2c2c2c";
    Context.lineWidth = 1;
    for (let Line = 0; Line <= 4; ++Line)
    {
        const Y = Top + (Bottom - Top) * Line / 4;
        Context.beginPath();
        Context.moveTo(Left, Y);
        Context.lineTo(Right, Y);
        Context.stroke();
    }
    const Curve = (Loss, Colour) =>
    {
        Context.strokeStyle = Colour;
        Context.lineWidth = 3;
        Context.beginPath();
        for (let Step = 0; Step <= 60; ++Step)
        {
            const Seconds = Step / 60 * 12.0;
            const Survival = Math.pow(Math.max(0.0, 1.0 - Loss), Seconds);
            const X = Left + (Right - Left) * Step / 60, Y = Bottom - (Bottom - Top) * Survival;
            if (Step === 0) Context.moveTo(X, Y); else Context.lineTo(X, Y);
        }
        Context.stroke();
    };
    Curve(Gas.Dissipation, "#d8d8d8");
    Curve(Gas.Cooling, "#e8833a");
    Context.fillStyle = "#7d7d7d";
    Context.font = "18px DM Sans, sans-serif";
    Context.fillText("density", Left, Height - 4);
    Context.fillStyle = "#e8833a";
    Context.fillText("heat", Left + 80, Height - 4);
    Context.fillStyle = "#7d7d7d";
    Context.fillText("12 s", Right - 40, Height - 4);
}

// 📐 The flame palette the march uses: blackbody chromaticity × (T / 1350 K)⁴ from 1000 K to 1800 K × temperature.
const BlackbodyStops = [[800, 0.015, 0.0], [1000, 0.040, 0.0], [1200, 0.087, 0.0], [1400, 0.130, 0.0], [1600, 0.171, 0.0],
                        [1800, 0.209, 0.0], [2000, 0.250, 0.006], [2500, 0.356, 0.065], [3000, 0.456, 0.147], [4000, 0.637, 0.366]];

export function ResolveBlackbody(Kelvin)
{
    const T = Math.max(800, Math.min(4000, Kelvin));
    let Lower = 0;
    for (let Index = 1; Index < BlackbodyStops.length - 1; ++Index)
        if (T >= BlackbodyStops[Index][0])
            Lower = Index;
    const [T0, G0, B0] = BlackbodyStops[Lower], [T1, G1, B1] = BlackbodyStops[Lower + 1];
    const F = Math.max(0, Math.min(1, (T - T0) / (T1 - T0)));
    return [1.0, G0 + (G1 - G0) * F, B0 + (B1 - B0) * F];
}

// 📐 Helland's blackbody fit (the march's ResolveBlackbodyColour), linear.
function ResolveHelland(Kelvin)
{
    const Hecto = Math.max(1000.0, Math.min(6600.0, Kelvin)) / 100.0;
    const Green = Math.min(1.0, Math.max(0.0, (99.4708025861 * Math.log(Hecto) - 161.1195681661) / 255.0));
    const Blue = Hecto <= 19.0 ? 0.0 : Math.min(1.0, Math.max(0.0, (138.5177312231 * Math.log(Math.max(Hecto - 10.0, 1.0)) - 305.0447927307) / 255.0));
    return [1.0, Math.pow(Green, 2.2), Math.pow(Blue, 2.2)];
}

function DrawBlackbody(Context, Width, Height, Gas)
{
    const Left = 18, Right = Width - 18;
    const Tip = Gas.FlameTip * Gas.FlameTemperature, Core = Gas.FlameCore * Gas.FlameTemperature;
    for (let X = Left; X < Right; X += 2)
    {
        const Mapped = (X - Left) / (Right - Left);
        const Kelvin = Tip + (Core - Tip) * Mapped;
        const Colour = ResolveHelland(Kelvin);
        const Brightness = Math.min(1.6, Math.pow(Kelvin / 1500.0, 4.0));
        const Tone = Colour.map((Channel) => Math.round(255 * Math.pow(Math.min(1.0, Channel * Brightness / (1.0 + Channel * Brightness)) * 1.6, 1 / 2.2)));
        Context.fillStyle = `rgb(${Math.min(255, Tone[0])},${Math.min(255, Tone[1])},${Math.min(255, Tone[2])})`;
        Context.fillRect(X, 14, 2, Height - 50);
    }
    Context.fillStyle = "#7d7d7d";
    Context.font = "18px DM Sans, sans-serif";
    Context.fillText(`tip ${Tip.toFixed(0)} K`, Left, Height - 8);
    Context.fillText(`core ${Core.toFixed(0)} K`, Right - 100, Height - 8);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FRACTURE
//------------------------------------------------------------------------------------------------------------------------

function BuildFractureGlobal(Host, C, Body)
{
    const F = Host.Fracture, S = F.Settings;
    const Props = C.Card(Body, "Breakable props", "Boxes", { Accent: Accent.Destruction, Index: 1 });
    C.Metric(Props, () => F.Props.filter((Prop) => Prop.State === "intact").length, `/ ${F.Props.length} intact`, 0,
             () => [`${F.Metrics.Breaks} breaks`, F.Metrics.Breaks ? "warm" : ""]);
    C.Muted(Props, "Each prop is a closed convex solid with a Fracture-editor recipe. Strike one with more than its threshold energy (½·μ·vₙ², μ the reduced mass of car and prop) and it fractures; below that it stops the car.");
    C.Stats(Props, [["Fragments", () => F.Metrics.ActiveFragments], ["Asleep", () => F.Metrics.Sleeping], ["In the field", () => F.Metrics.InField || 0]]);
    const Row = C.ActionRow(Props);
    C.Button(Row, "Reassemble all", "Undo2", () => F.ReassembleAll());
    C.Button(Row, "Break all", "Bomb", () => F.Props.forEach((Prop) => F.BreakFromEditor(Prop)), "danger");

    const Debris = C.Card(Body, "Debris", "Gem", { Accent: Accent.Destruction, Index: 2, Subtitle: "Convex rigid bodies · stays where it lands" });
    C.Muted(Debris, "Every fragment is a convex hull in the shared rigid-body world: against the course, the walls, every other fragment and cone, and the car both ways — pieces of 8 kg and more can be driven over. Large fragments get a distance volume of their own the frame they are born and join the global distance field, so the debris shadows and bounces light in the GI.");
    C.Switch(Debris, S, "Respawn", { Label: "Reassemble after the lifetime", Detail: "Off: debris stays until reset (T)" });
    Slider(C, Debris, S, "Lifetime", "Lifetime before reassembly", 3, 120, 1, "s", 0);
    Slider(C, Debris, S, "Separation", "Separation", 0, 3, 0.05, "×", 2, ["Crumble", "Explode"]);
    Slider(C, Debris, S, "ImpactShare", "Impact share (Dynamic)", 0.05, 1.0, 0.01, "", 2, ["Few pieces", "Many"]);
    Slider(C, Debris, Host.Bodies.Settings, "Bounce", "Bounce", 0, 3, 0.05, "×", 2);
    Slider(C, Debris, Host.Bodies.Settings, "Friction", "Friction", 0, 3, 0.05, "×", 2);
    Slider(C, Debris, S, "FieldRadius", "Distance field from radius", 0.25, 4, 0.05, "voxels", 2, ["More pieces", "Fewer"]);
    C.Switch(Debris, Host.Bodies.Settings, "Vehicle", { Label: "Car collides with debris", Detail: "Both ways" });
    C.Muted(Debris, Host.Bodies.Backend || "Rigid-body physics");
    C.Stats(Debris, [["Bodies", () => Host.Bodies.Metrics.Bodies], ["Awake", () => Host.Bodies.Metrics.Awake], ["Step", () => Host.Bodies.Metrics.Milliseconds, "ms", 2]]);
    C.Stats(Debris, [["Contacts", () => Host.Bodies.Metrics.Contacts], ["Pool volumes", () => Host.Volumes.PoolMetrics.Live], ["Computed", () => Host.Volumes.PoolMetrics.Computed]]);

    const Bake = C.Card(Body, "Bake · all props", "Layers", { Accent: Accent.Destruction, Index: 3 });
    C.Muted(Bake, "Dynamic cuts fragments on the spot at the true impact point with the impact's share of energy (one per frame, ~10 ms); Baked uses the stored pattern — no cost at impact.");
    const BakeRow = C.ActionRow(Bake);
    C.Button(BakeRow, "All dynamic", "Zap", () => F.Props.forEach((Prop) => F.AssignRecipe(Prop, { Mode: FractureModes.Dynamic })));
    C.Button(BakeRow, "All baked", "Layers", () => F.Props.forEach((Prop) => F.AssignRecipe(Prop, { Mode: FractureModes.Baked })));
    C.Readout(Bake, () => F.QueryReadout());
}

function BuildProp(Host, Prop, C, Body)
{
    const EntityId = `prop-${Prop.Index}`;
    BuildTransformCard(Host, EntityId, C, Body, { AccentColour: Accent.Destruction });
    BuildArrayModifierCard(Host, EntityId, C, Body, Accent.Destruction);

    const PropEntityId = `prop-${Prop.Index}`;
    BuildTransformCard(Host, PropEntityId, C, Body, { AccentColour: Accent.Destruction });
    BuildArrayModifierCard(Host, PropEntityId, C, Body, Accent.Destruction);

    const F = Host.Fracture, Recipe = Prop.Recipe;
    const Material = () => FractureMaterials[Recipe.Material];
    const Reduced = () => VehicleGeometry.Mass * Prop.Mass / (VehicleGeometry.Mass + Prop.Mass);
    const BreakSpeed = () => Math.sqrt(2.0 * Recipe.Threshold / Reduced());
    const Preview = { Separation: 0.18 };
    let RedrawPreview = () => {};

    const Materials = C.Card(Body, "Material", "Layers", { Accent: Accent.Destruction, Index: 1, Subtitle: "Fracture response · not surface appearance" });
    const Grid = document.createElement("div");
    Grid.className = "material-grid";
    const Chips = FractureMaterialOrder.map((Key) =>
    {
        const Chip = document.createElement("button");
        Chip.className = "material-chip";
        const Dot = document.createElement("i");
        Dot.style.background = FractureMaterials[Key].Colour;
        const Name = document.createElement("span");
        Name.textContent = FractureMaterials[Key].Label;
        Chip.append(Dot, Name);
        Chip.addEventListener("click", () => { F.AssignRecipe(Prop, { Material: Key }); C.Synchronise(); RedrawPreview(); });
        Grid.appendChild(Chip);
        return [Chip, Key];
    });
    Materials.appendChild(Grid);
    C.Bindings.push(() => Chips.forEach(([Chip, Key]) => Chip.classList.toggle("active", Recipe.Material === Key)));
    C.Synchronise();
    C.Stats(Materials, [["Crack resistance", () => Material().Gc, "J/m²"], ["Density", () => Material().Density, "kg/m³"],
                        ["Prop mass", () => Prop.Mass, "kg"]]);
    const Note = C.Muted(Materials, "");
    C.Live.push(() => { Note.textContent = Material().Note; });

    const Impact = C.Card(Body, "Impact", "Zap", { Accent: Accent.Destruction, Index: 2 });
    C.Metric(Impact, () => BreakSpeed() * 3.6, "km/h to break", 0, () => [`${(Recipe.Threshold / 1000).toFixed(1)} kJ`, ""]);
    C.Instrument(Impact, 96, (Context, Width, Height) => DrawBreakChart(Context, Width, Height, Reduced(), Recipe.Threshold), true);
    Slider(C, Impact, Recipe, "Threshold", "Break threshold", 100, 150000, 100, "J", 0, ["Brittle", "Stops the car"]);
    Slider(C, Impact, Recipe, "Energy", "Baked impact energy", 200, 50000, 100, "J", 0, null, () => { F.AssignRecipe(Prop, {}); RedrawPreview(); });
    Slider(C, Impact, Recipe, "Seed", "Pattern seed", 1, 9999, 1, "#", 0, null, () => { F.AssignRecipe(Prop, {}); RedrawPreview(); });
    C.Muted(Impact, "The impact point is wherever the car strikes (Dynamic) or the face it approaches from (Baked). Closer, larger pieces receive cut priority.");

    const Quality = C.Card(Body, "Fragment quality", "Gem", { Accent: Accent.Destruction, Index: 3 });
    RedrawPreview = C.Instrument(Quality, 150, (Context, Width, Height) => DrawFragments(Context, Width, Height, Prop, Preview.Separation));
    Slider(C, Quality, Preview, "Separation", "Preview separation", 0, 1, 0.01, "", 2, null, () => RedrawPreview());
    Slider(C, Quality, Recipe, "Ceiling", "Fragment ceiling", 2, Prop.Definition.Capacity, 1, "pcs", 0, null, () => { F.AssignRecipe(Prop, {}); RedrawPreview(); });
    Slider(C, Quality, Recipe, "MinimumSize", "Minimum span", 0.01, 0.3, 0.005, "m", 3, null, () => { F.AssignRecipe(Prop, {}); RedrawPreview(); });
    C.Pills(Quality, ["Needle rejection", "Closed caps", "Quality-aware triangulation"]);
    C.Muted(Quality, "Near-edge cuts and tiny pieces are refused. A limit is a ceiling — not a promise to force bad fragments.");

    const Bake = C.Card(Body, "Bake", "Layers", { Accent: Accent.Destruction, Index: 4 });
    C.Segmented(Bake, Recipe, "Mode", [["Dynamic", FractureModes.Dynamic], ["Baked", FractureModes.Baked]],
                { OnChange: (Mode) => { F.AssignRecipe(Prop, { Mode }); RedrawPreview(); } });
    const Explain = C.Muted(Bake, "");
    C.Live.push(() => { Explain.textContent = Recipe.Mode === FractureModes.Dynamic ? "Generate fragments on demand at the true impact point." : "Use the stored fragment geometry — no fracture generation at impact."; });
    const Actions = C.ActionRow(Bake);
    C.Button(Actions, "Bake geometry", "Layers", () => { F.BakeProp(Prop); RedrawPreview(); }, "primary");
    C.Button(Actions, "Break now", "Hammer", () => F.BreakFromEditor(Prop));
    C.Button(Actions, "Reassemble", "Undo2", () => { if (Prop.State !== "intact") F.ReassembleProp(Prop); });

    const Receipt = C.Card(Body, "Geometry receipt", "Scan", { Accent: Accent.Destruction, Index: 5 });
    C.Stats(Receipt, [["Fragments", () => (Prop.Receipt ? Prop.Receipt.Fragments : 0)],
                      ["Volume error", () => (Prop.Receipt ? Prop.Receipt.VolumeError.toExponential(1) : "–")],
                      ["Min quality", () => (Prop.Receipt ? Prop.Receipt.Quality : 0), "", 3]]);
    C.Receipt(Receipt, [["Triangles", () => (Prop.Receipt ? Prop.Receipt.Triangles.toLocaleString("en-US") : "–")],
                        ["Refused candidates", () => (Prop.Receipt ? Prop.Receipt.Rejected : "–")],
                        ["Energy spent", () => (Prop.Receipt ? `${Prop.Receipt.Spent.toFixed(0)} / ${Prop.Receipt.Budget.toFixed(0)} J` : "–")],
                        ["Generation", () => (Prop.Receipt ? `${Prop.Receipt.Milliseconds.toFixed(1)} ms` : "–")],
                        ["Topology", () => (Prop.Receipt && Prop.Receipt.Closed ? "Closed" : "Open")],
                        ["State", () => Prop.State]]);
    C.Muted(Receipt, "Material-biased, energy-limited geometric partitioning (the Fracture Editor's). Not a calibrated physical failure simulation.");
}

// 📐 Impact energy ½·μ·v² against speed, with the threshold and the speed at which it is reached.
function DrawBreakChart(Context, Width, Height, Reduced, Threshold)
{
    const Left = 18, Right = Width - 18, Top = 16, Bottom = Height - 28;
    // 📝 The axis spans a little over twice the breaking speed, so the threshold sits low and the curve stays visible.
    const MaximumSpeed = Math.max(4.0, Math.min(30.0, 2.2 * Math.sqrt(2.0 * Threshold / Reduced)));
    const MaximumEnergy = 0.5 * Reduced * MaximumSpeed * MaximumSpeed;
    const Point = (Speed, Energy) => [Left + (Right - Left) * Speed / MaximumSpeed, Bottom - (Bottom - Top) * Math.min(1.0, Energy / MaximumEnergy)];
    const Gradient = Context.createLinearGradient(0, Top, 0, Bottom);
    Gradient.addColorStop(0, "rgba(143,179,164,0.45)");
    Gradient.addColorStop(1, "rgba(143,179,164,0.02)");
    Context.beginPath();
    Context.moveTo(Left, Bottom);
    for (let Step = 0; Step <= 80; ++Step)
    {
        const Speed = MaximumSpeed * Step / 80;
        const [X, Y] = Point(Speed, 0.5 * Reduced * Speed * Speed);
        Context.lineTo(X, Y);
    }
    Context.lineTo(Right, Bottom);
    Context.closePath();
    Context.fillStyle = Gradient;
    Context.fill();
    Context.strokeStyle = "#8fb3a4";
    Context.lineWidth = 3;
    Context.beginPath();
    for (let Step = 0; Step <= 80; ++Step)
    {
        const Speed = MaximumSpeed * Step / 80;
        const [X, Y] = Point(Speed, 0.5 * Reduced * Speed * Speed);
        if (Step === 0) Context.moveTo(X, Y); else Context.lineTo(X, Y);
    }
    Context.stroke();
    const [, Line] = Point(0, Threshold);
    Context.strokeStyle = "#e0a24a";
    Context.setLineDash([8, 8]);
    Context.lineWidth = 2;
    Context.beginPath();
    Context.moveTo(Left, Line);
    Context.lineTo(Right, Line);
    Context.stroke();
    Context.setLineDash([]);
    const Speed = Math.sqrt(2.0 * Threshold / Reduced);
    if (Speed < MaximumSpeed)
    {
        const [X, Y] = Point(Speed, Threshold);
        Context.fillStyle = "#f3f3f3";
        Context.beginPath();
        Context.arc(X, Y, 7, 0, 2.0 * Math.PI);
        Context.fill();
    }
    Context.fillStyle = "#7d7d7d";
    Context.font = "18px DM Sans, sans-serif";
    Context.fillText("impact speed →", Right - 140, Height - 6);
    Context.fillStyle = "#e0a24a";
    Context.fillText("threshold", Left + 4, Line - 8);
}

// 📐 The baked fragments, exploded from the centre and flat-shaded back to front from a three-quarter view.
function DrawFragments(Context, Width, Height, Prop, Separation)
{
    const Fragments = Prop.Baked ? Prop.Baked.Fragments : [];
    const Material = FractureMaterials[Prop.Recipe.Material];
    const Yaw = 0.72, Pitch = 0.42;
    const Rotate = (P) =>
    {
        const X = P[0] * Math.cos(Yaw) - P[1] * Math.sin(Yaw), Y = P[0] * Math.sin(Yaw) + P[1] * Math.cos(Yaw);
        return [X, Y * Math.cos(Pitch) - P[2] * Math.sin(Pitch), Y * Math.sin(Pitch) + P[2] * Math.cos(Pitch)];
    };
    const Triangles = [];
    let Reach = 0.01;
    for (const Fragment of Fragments)
    {
        const Offset = Fragment.Centroid.map((Component) => Component * (1.0 + 2.2 * Separation));
        for (const Triangle of Fragment.Triangles)
        {
            const Corners = Triangle.Corners.map((Corner) => Rotate([Corner[0] + Offset[0], Corner[1] + Offset[1], Corner[2] + Offset[2]]));
            for (const Corner of Corners)
                Reach = Math.max(Reach, Math.abs(Corner[0]), Math.abs(Corner[2]));
            const U = [Corners[1][0] - Corners[0][0], Corners[1][1] - Corners[0][1], Corners[1][2] - Corners[0][2]];
            const V = [Corners[2][0] - Corners[0][0], Corners[2][1] - Corners[0][1], Corners[2][2] - Corners[0][2]];
            const Normal = [U[1] * V[2] - U[2] * V[1], U[2] * V[0] - U[0] * V[2], U[0] * V[1] - U[1] * V[0]];
            const Length = Math.hypot(...Normal) || 1.0;
            if (Normal[1] / Length > 0.02)
                continue;
            const Light = Math.max(0.0, (-0.35 * Normal[0] - 0.55 * Normal[1] + 0.76 * Normal[2]) / Length);
            Triangles.push({ Corners, Depth: (Corners[0][1] + Corners[1][1] + Corners[2][1]) / 3, Light, Interior: Triangle.Family === Material.Interior });
        }
    }
    Triangles.sort((First, Second) => Second.Depth - First.Depth);
    const Scale = Math.min(Width, Height) * 0.42 / Reach;
    const Skin = HexToRgb(Material.Colour), Inside = HexToRgb(Material.InteriorColour);
    for (const Triangle of Triangles)
    {
        const Base = Triangle.Interior ? Inside : Skin;
        const Shade = 0.32 + 0.68 * Triangle.Light;
        Context.fillStyle = `rgb(${Base.map((Channel) => Math.round(Channel * Shade)).join(",")})`;
        Context.strokeStyle = "rgba(10,10,10,0.55)";
        Context.lineWidth = 1;
        Context.beginPath();
        Triangle.Corners.forEach((Corner, Index) =>
        {
            const X = Width * 0.5 + Corner[0] * Scale, Y = Height * 0.53 - Corner[2] * Scale;
            if (Index === 0) Context.moveTo(X, Y); else Context.lineTo(X, Y);
        });
        Context.closePath();
        Context.fill();
        Context.stroke();
    }
    Context.fillStyle = "#7d7d7d";
    Context.font = "18px DM Sans, sans-serif";
    Context.fillText(`${Fragments.length} fragments · ${Prop.Recipe.Mode}`, 18, Height - 12);
}

function HexToRgb(Hex)
{
    const Value = parseInt(Hex.slice(1), 16);
    return [(Value >> 16) & 255, (Value >> 8) & 255, Value & 255];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   LEVEL, STRESS
//------------------------------------------------------------------------------------------------------------------------

function BuildLevel(Host, C, Body)
{
    const S = Host.Settings;
    const Test = C.Card(Body, "Metal & glass row", "Layers", { Accent: Accent.Level, Index: 1, Subtitle: "Mirror · aluminium · gold · glass" });
    C.Muted(Test, "A plane mirror and three 1 m spheres on the open asphalt south-east of the course. Metals reflect in their own colour; the glass bends the " +
                  "light behind it by its index of refraction, and every surface's reflectance follows Fresnel: F0 at normal incidence, rising toward grazing.");
    Slider(C, Test, S, "GlassIndex", "Glass index of refraction", 1.0, 2.5, 0.01, "n", 2, ["Thin", "Dense"]);
    C.Muted(Test, "Glass F0 = ((n − 1)/(n + 1))². At n = 1.5 that is 4 %. Inspection → Fresnel reflectance shows the environment reflectance per pixel.");

    const Zones = C.Card(Body, "Proving grounds", "Map", { Accent: Accent.Level, Index: 2, Subtitle: "Teleport the car" });
    C.Muted(Zones, "A 216 × 120 m asphalt pad in a grass surround. West: the main straight with kerbs and a jump, a skidpad, a concrete burnout box, a smash alley of breakable props, a slatted hangar for bounce light, a container yard and a lit tunnel. East: the training arena — launch strip, slalom, moose test, cone bowling, spin box and a figure-8.");
    for (const Zone of LevelZones)
        C.LinkButton(Zones, `${Zone.Name} · ${Zone.Note}`, () => Host.TeleportVehicle(Zone));

    const Budget = C.Card(Body, "Scene budget", "Cpu", { Accent: Accent.Level, Index: 3 });
    C.Stats(Budget, [["Occupants", () => Host.Scene.Occupants.length], ["In the field", () => Host.Scene.QueryUnionCount()],
                     ["Volumes", () => Host.Volumes.Volumes.length]]);
    C.Stats(Budget, [["Cards", () => Host.Cards.Cards.length], ["Draw runs", () => Host.Scene.DrawRuns.length],
                     ["Volume atlas", () => (Host.Volumes.SpaceExtent.reduce((A, B) => A * B, 1) * 8 / 1048576), "MB"]]);
}

function BuildStress(Host, C, Body)
{
    const S = Host.Settings;
    const Stress = C.Card(Body, "Stress test", "Boxes", { Accent: Accent.Level, Index: 1 });
    C.Metric(Stress, () => Host.Stress.QueryActive(), `/ ${Host.Stress.QueryCapacity()} objects`, 0,
             () => [`${Host.Stress.Moving.length} moving`, Host.Stress.Moving.length ? "warm" : ""]);
    C.Muted(Stress, "The pool is built with the scene; enabling objects lengthens the union span, grows the draw runs, joins the relighting lists and adds colliders — bookkeeping, not construction.");
    const Row = C.ActionRow(Stress);
    for (const Count of [64, 256, 1024])
        C.Button(Row, `+${Count}`, "Plus", () => Host.ApplySetting(`StressAdd${Count}`));
    const Row2 = C.ActionRow(Stress);
    C.Button(Row2, "Fill pool", "Boxes", () => Host.ApplySetting("StressFill"));
    C.Button(Row2, "Clear", "Trash2", () => Host.ApplySetting("StressClear"), "danger");
    C.Segmented(Stress, S, "StressMotion", StressMotionOptions, { Label: "Motion", OnChange: () => Host.ApplySetting("StressMotion") });
    C.Readout(Stress, () => Host.Stress.QueryReadout());
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  TRAINING ARENA
//------------------------------------------------------------------------------------------------------------------------

function BuildTraining(Host, C, Body)
{
    const T = Host.Training;
    const Live = C.Card(Body, "Now running", "Timer", { Accent: Accent.Level, Index: 1 });
    C.Readout(Live, () => T.QueryReadout());
    C.Muted(Live, "Drive into any station and its drill starts on its own; or start one here — the car derezzes to the start and the cones the last run knocked over rez back onto their marks. R returns to the running drill's start, T resets the whole arena.");
    const Row = C.ActionRow(Live);
    C.Button(Row, "Reset arena", "RotateCcw", () => Host.ResetArena());
    C.Button(Row, "Stop drill", "Pause", () => T.Cancel());

    const List = C.Card(Body, "Stations", "Flag", { Accent: Accent.Level, Index: 2, Subtitle: "Best times are kept in this browser" });
    for (const Drill of TrainingDrills)
    {
        const Row = document.createElement("div");
        Row.className = "drill-row";
        const Title = document.createElement("div");
        Title.className = "drill-title";
        Title.textContent = Drill.Name;
        const Note = document.createElement("div");
        Note.className = "drill-note";
        Note.textContent = Drill.Note;
        const Best = document.createElement("div");
        Best.className = "drill-best";
        Row.append(Title, Note, Best);
        C.Button(Row, "Start", Drill.Glyph in { Gauge: 1 } ? "Gauge" : "Play", () => T.StartDrill(Drill.Id));
        List.appendChild(Row);
        C.Live.push(() =>
        {
            const Record = T.Best[Drill.Id];
            const Last = (T.Results[Drill.Id] || [])[0];
            Best.textContent = [Record ? `best ${FormatPrimary(Record)}` : "", Last ? `last ${FormatPrimary(Last)}` : ""].filter(Boolean).join("  ·  ");
        });
    }
    const Clear = C.ActionRow(List);
    C.Button(Clear, "Clear best times", "Trash2", () => T.ClearBest(), "danger");
}

function BuildCones(Host, C, Body)
{
    const K = Host.Cones;
    const Cones = C.Card(Body, "Cones", "TrafficCone", { Accent: Accent.Level, Index: 1, Subtitle: "70 cm slalom cone · 3.2 kg" });
    C.Metric(Cones, () => K.Metrics.Displaced, `/ ${K.Cones.length} displaced`, 0, () => [`${K.Metrics.Moving} moving`, K.Metrics.Moving ? "warm" : ""]);
    C.Muted(Cones, "A fluorescent PVC cone with two retroreflective collars on a chamfered rubber base, weighted low like a real one so it rocks before it topples. At rest each cone is a sleeping rigid body and a static occupant (in the field, carded, relit round-robin); a knocked cone follows its body every frame until it settles.");
    const Row = C.ActionRow(Cones);
    C.Button(Row, "Rez all back", "Undo2", () => K.ResetCones(null, true));
    C.Button(Row, "Snap back", "RotateCcw", () => K.ResetCones(null, false));
    C.Readout(Cones, () => K.QueryReadout());
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   DEREZ · VFX
//------------------------------------------------------------------------------------------------------------------------

function BuildDerez(Host, C, Body)
{
    const E = Host.Effects, S = E.Settings, R = Host.Recovery;
    const Palette = () => E.ApplyPalette(Host.MaterialRecords);
    const Preset = C.Card(Body, "Preset", "Sparkles", { Accent: Accent.Effects, Index: 1, Subtitle: "From the Particle Editor" });
    C.Muted(Preset, "The car comes apart cell by cell on one release clock: the surface raster glows each cell in its own facet's colour and drops it the instant the effect raster launches its cubes — each cube the colour of the face it came from, glowing in it, leaving with the car's velocity at that moment (the car keeps moving while it dissolves). Rez runs the clock backwards.");
    C.Segmented(Preset, S, "Preset", Object.keys(DerezPresets).map((Name) => [Name, Name]),
                { Label: "Look", OnChange: (Name) => { E.ApplyPreset(Name, Host.MaterialRecords); C.Synchronise(); } });
    const Test = C.ActionRow(Preset);
    C.Button(Test, "Derez & respawn", "Sparkles", () => Host.RespawnVehicle());
    C.Button(Test, "Burst nearest cone", "TrafficCone", () =>
    {
        const Cone = Host.Cones.Cones.reduce((Best, Candidate) =>
            (Distance(Candidate.Body.Position, Host.Vehicle.Position) < Distance(Best.Body.Position, Host.Vehicle.Position) ? Candidate : Best));
        Host.Cones.BurstCone(Cone);
    });

    const Cubes = C.Card(Body, "Cubes", "Box", { Accent: Accent.Effects, Index: 2 });
    Slider(C, Cubes, S, "HoldOffset", "Hold before the wave", 0, 2, 0.05, "s", 2);
    Slider(C, Cubes, S, "Wave", "Wave spread", 0.3, 4, 0.05, "s", 2, ["Snap", "Slow sweep"]);
    Slider(C, Cubes, S, "Burst", "Burst along the normal", 0, 4, 0.05, "m/s", 2);
    Slider(C, Cubes, S, "SpeedMinimum", "Slowest launch", 0, 4, 0.05, "m/s", 2);
    Slider(C, Cubes, S, "SpeedMaximum", "Fastest launch", 0, 8, 0.05, "m/s", 2);
    Slider(C, Cubes, S, "Drag", "Drag", 0.05, 3, 0.05, "/s", 2);
    Slider(C, Cubes, S, "Life", "Cube life", 0.5, 6, 0.1, "s", 1);
    Slider(C, Cubes, S, "Size", "Cube size", 0.02, 0.2, 0.005, "m", 3);
    Slider(C, Cubes, S, "Flight", "Rez flight", 0.4, 4, 0.05, "s", 2);
    C.Segmented(Cubes, S, "Style", [["Cubes", 0], ["Glitch", 1]], { Label: "Style" });

    const Colours = C.Card(Body, "Glow", "Palette", { Accent: Accent.Effects, Index: 3 });
    C.Segmented(Colours, S, "Faces", [["Car's own colours", true], ["Palette", false]], { Label: "Cube colour" });
    Slider(C, Colours, S, "GlowStrength", "Face glow", 0, 20, 0.5, "× albedo", 1);
    C.Switch(Colours, S, "Momentum", { Label: "Keep the car's momentum", Detail: "It coasts on while it comes apart; cubes leave at its velocity" });
    C.Colour(Colours, S, "Glow", { Label: "Glow", OnChange: Palette });
    C.Colour(Colours, S, "Hot", { Label: "Hot edge", OnChange: Palette });
    C.Colour(Colours, S, "Glitch", { Label: "Glitch", OnChange: Palette });
    Slider(C, Colours, S, "Intensity", "Intensity", 0, 4, 0.05, "×", 2, null, Palette);

    const Recovery = C.Card(Body, "Recovery", "Shield", { Accent: Accent.Effects, Index: 4, Subtitle: "Derez → hidden → move → rez" });
    C.Muted(Recovery, "An overturned car, a car tumbling through the air, or one that has left the grounds derezzes and rezzes back — upright where it lay, or at the last safe spot. R and every teleport use the same sequence.");
    C.Switch(Recovery, R.Settings, "Enabled", { Label: "Automatic recovery" });
    Slider(C, Recovery, R.Settings, "OverturnSeconds", "Overturned for", 0.3, 5, 0.1, "s", 1);
    Slider(C, Recovery, R.Settings, "AirborneSeconds", "Airborne for", 1, 6, 0.1, "s", 1);
    Slider(C, Recovery, R.Settings, "TumbleSeconds", "Tumbling for", 0.3, 3, 0.1, "s", 1);
    Slider(C, Recovery, R.Settings, "TumbleRate", "Tumble rate", 2, 12, 0.5, "rad/s", 1);
    C.Readout(Recovery, () => [...R.QueryReadout(), ...E.QueryReadout()]);
}

function Distance(Left, Right)
{
    return Math.hypot(Left[0] - Right[0], Left[1] - Right[1]);
}

//------------------------------------------------------------------------------------------------------------------------
//                                      CAMERAS (EDITOR CAMERA, CINE CAMERA, POST PROCESS)
//------------------------------------------------------------------------------------------------------------------------

function ComputeThinLensOptics(Focal, Aperture, Distance, Aspect = 16 / 9, Sensor = 36.0)
{
    const H = (Focal * Focal) / (Math.max(0.5, Aperture) * 0.03) + Focal;
    const S = Math.max(0.25, Distance) * 1000.0;
    const Near = (H * S) / (H + S - Focal) / 1000.0;
    const Far = H - (S - Focal) > 0 ? (H * S) / (H - S + Focal) / 1000.0 : Infinity;
    const HorizontalFov = (2.0 * Math.atan(Sensor / (2.0 * Focal)) * 180.0) / Math.PI;
    const VerticalFov = (2.0 * Math.atan(Sensor / Aspect / (2.0 * Focal)) * 180.0) / Math.PI;
    return { HorizontalFov, VerticalFov, Pupil: Focal / Math.max(0.5, Aperture), Near, Far, Hyperfocal: H / 1000.0 };
}

function BuildEditorCamera(Host, C, Body)
{
    const S = Host.Settings;
    const Nav = C.Card(Body, "Editor navigation camera", "Camera", { Accent: Accent.Cameras, Index: 1, Subtitle: "Permanent scene viewport camera" });
    C.Metric(Nav, () => `${(S.EditorFocalLength || 35).toFixed(1)} mm`, "", 0,
             () => [Host.FreeCamera || (Host.Lobby && S.CameraMode === "Orbit") ? "Active in viewport" : "Standby · press F8 or Edit", "live"]);
    C.Muted(Nav, "Editor navigation camera · always present in every scene. Hold Right-Mouse + WASD in Edit mode to fly freely without interfering with G/R/S gizmo shortcuts.");
    if (!Host.Lobby)
        Slider(C, Nav, S, "EditorMoveSpeed", "Free-flight move speed", 2, 45, 0.5, "m/s", 1, ["Walk", "Fast"]);
    const NavRow = C.ActionRow(Nav);
    C.Button(NavRow, Host.Lobby ? "Engage Studio Orbit" : "Engage Editor Camera (F8)", "Camera", () =>
    {
        if (Host.Lobby) Host.AssignCameraMode("Orbit");
        else Host.AssignCamera(true);
    });
    if (!Host.Lobby)
        C.Button(NavRow, "Return to Chase", "Car", () => Host.AssignCamera(false));

    BuildInteractiveCameraCards(C, Body, {
        Accent: Accent.Cameras,
        ReadState: () => ({
            FocalLength: S.EditorFocalLength || 35,
            Aperture: S.EditorAperture || 4.0,
            FocusDistance: S.EditorFocusDistance || 12.0,
            SensorWidth: S.EditorSensorWidth || 36.0,
            DofEnabled: Boolean(S.EditorDofEnabled)
        }),
        WriteState: (Patch) =>
        {
            if (Patch.FocalLength !== undefined) S.EditorFocalLength = Patch.FocalLength;
            if (Patch.Aperture !== undefined) S.EditorAperture = Patch.Aperture;
            if (Patch.FocusDistance !== undefined) S.EditorFocusDistance = Patch.FocusDistance;
            if (Patch.SensorWidth !== undefined) S.EditorSensorWidth = Patch.SensorWidth;
            if (Patch.DofEnabled !== undefined) S.EditorDofEnabled = Patch.DofEnabled;
            Host.PriorValid = false;
        }
    });
}

function BuildVehicleCamera(Host, C, Body)
{
    const S = Host.Settings;
    const VC = Host.VehicleCamera;
    BuildTransformCard(Host, "vehicle-camera", C, Body, { AccentColour: Accent.Cameras, Subtitle: "World-space pose · auto-solves local offset on ControlVehicle" });

    const Mount = C.Card(Body, "Parented rig · ControlVehicle local space", "Car", { Accent: Accent.Vehicle, Index: 2, Subtitle: "Child of ControlVehicle · low-poly 3D camera mesh" });
    C.Metric(Mount, () => `${VC.LocalOffset.map((V) => V.toFixed(2)).join(", ")}`, " m", 0,
             () => [S.CameraMode === "VehicleCamera" ? "Viewing through rig" : (VC.Visible !== false ? "3D mesh visible" : "mesh hidden"),
                    S.CameraMode === "VehicleCamera" ? "live" : "warm"]);
    C.Muted(Mount, "Parented directly to ControlVehicle: inherits chassis translation, roll, pitch and yaw every frame. Its low-poly 3D camera body + matte box renders in the scene whenever you view from another camera.");

    const LocalProxy = {
        get X() { return VC.LocalOffset[0]; }, set X(V) { VC.LocalOffset[0] = V; Host.EditorOccupantsDirty = true; Host.PriorValid = false; },
        get Y() { return VC.LocalOffset[1]; }, set Y(V) { VC.LocalOffset[1] = V; Host.EditorOccupantsDirty = true; Host.PriorValid = false; },
        get Z() { return VC.LocalOffset[2]; }, set Z(V) { VC.LocalOffset[2] = V; Host.EditorOccupantsDirty = true; Host.PriorValid = false; },
        get Pitch() { return VC.LocalEulerDeg[1]; }, set Pitch(V) { VC.LocalEulerDeg[1] = V; Host.EditorOccupantsDirty = true; Host.PriorValid = false; },
        get Yaw()   { return VC.LocalEulerDeg[2]; }, set Yaw(V)   { VC.LocalEulerDeg[2] = V; Host.EditorOccupantsDirty = true; Host.PriorValid = false; }
    };
    Slider(C, Mount, LocalProxy, "X", "Local forward (+X)", -8.0, 6.0, 0.05, "m", 2);
    Slider(C, Mount, LocalProxy, "Y", "Local lateral (+Y)", -4.5, 4.5, 0.05, "m", 2);
    Slider(C, Mount, LocalProxy, "Z", "Local height (+Z)", 0.1, 4.5, 0.05, "m", 2);
    Slider(C, Mount, LocalProxy, "Pitch", "Local pitch", -45, 45, 0.5, "°", 1);
    Slider(C, Mount, LocalProxy, "Yaw", "Local yaw", -180, 180, 1, "°", 0);

    const Actions = C.ActionRow(Mount);
    C.Button(Actions, "View Through Vehicle Camera", "Camera", () =>
    {
        Host.AssignCamera(false);
        Host.EditorFlightActive = false;
        Host.AssignCameraMode("VehicleCamera");
        Host.Shell?.SyncViewportToolbar?.();
    });
    C.Button(Actions, "Reset Mount", "RotateCcw", () =>
    {
        VC.LocalOffset = [-3.25, 0.0, 1.48];
        VC.LocalEulerDeg = [0.0, 9.0, 0.0];
        VC.Scale = [1.0, 1.0, 1.0];
        VC.AttachedSocket = "ChaseBoom";
        Host.EditorOccupantsDirty = true;
        C.Synchronise();
    });

    const SocketCard = C.Card(Body, "Vehicle Attachment Sockets", "MoveSpace", {
        Accent: Accent.Cameras,
        Index: 1,
        Subtitle: "Editor-only socket debug (hidden automatically in Play mode)"
    });
    C.Switch(SocketCard, Host, "ShowSockets", {
        Label: "Show socket location debug",
        Detail: "Draws 3D local XYZ triads, diamond reticles & socket labels in Edit mode",
        OnChange: () => Host.Gizmo?.RenderToolbar?.()
    });
    const SocketGrid = C.ActionRow(SocketCard);
    for (const Spec of (Host.QuerySocketSpecs?.() || []).slice(0, 6))
    {
        C.Button(SocketGrid, Spec.Name, "Target", () =>
        {
            Host.SnapVehicleCameraToSocket?.(Spec.Id);
            C.Synchronise();
        });
    }

    BuildInteractiveCameraCards(C, Body, {
        Accent: Accent.Cameras,
        ReadState: () => ({
            FocalLength: S.VehicleCamFocalLength || 32,
            Aperture: S.VehicleCamAperture || 2.8,
            FocusDistance: S.VehicleCamFocusDistance || 5.5,
            SensorWidth: S.VehicleCamSensorWidth || 36.0,
            DofEnabled: S.VehicleCamDofEnabled !== false
        }),
        WriteState: (Patch) =>
        {
            if (Patch.FocalLength !== undefined) S.VehicleCamFocalLength = Patch.FocalLength;
            if (Patch.Aperture !== undefined) S.VehicleCamAperture = Patch.Aperture;
            if (Patch.FocusDistance !== undefined) S.VehicleCamFocusDistance = Patch.FocusDistance;
            if (Patch.SensorWidth !== undefined) S.VehicleCamSensorWidth = Patch.SensorWidth;
            if (Patch.DofEnabled !== undefined) S.VehicleCamDofEnabled = Patch.DofEnabled;
            Host.PriorValid = false;
        }
    });
}

function BuildCineCamera(Host, C, Body)
{
    const S = Host.Settings;
    const Director = Host.CinematicCamera;
    BuildTransformCard(Host, "cine-camera", C, Body, { AccentColour: Accent.Cameras, Subtitle: "Dragging in Edit mode switches choreography to Manual Framing" });

    const Card = C.Card(Body, "Seamless spline choreography", "Clapperboard", { Accent: Accent.Cameras, Index: 1, Subtitle: "Continuous C¹ Catmull-Rom spline · zero black fades" });
    C.Metric(Card, () => Director.QueryActiveShot(Host.Lobby).Name, "", 0,
             () => [S.CameraMode === "Cinematic" ? "Rolling · seamless" : "3D rig active", S.CameraMode === "Cinematic" ? "live" : "warm"]);
    C.Muted(Card, "Continuous periodic C¹ Catmull-Rom camera spline with unwrapped yaw and smooth quintic cross-blend on manual shot skips — never fades to black.");
    C.Select(Card, Director, "Style", ChoreographyStyles, {
        Label: "Choreography style",
        OnChange: (Val) => { Director.SetStyle(Number(Val)); if (Number(Val) !== 2) Host.CineCameraManual = null; }
    });
    Slider(C, Card, Director, "Speed", "Spline playback speed", 0.2, 2.5, 0.05, "×", 2, ["Slow-mo", "Dynamic"]);
    const Actions = C.ActionRow(Card);
    C.Button(Actions, "Engage Cine Camera", "Clapperboard", () => { Host.AssignCamera(false); Host.EditorFlightActive = false; Host.AssignCameraMode("Cinematic"); });
    C.Button(Actions, "Previous keyframe", "Undo2", () => { Director.SetStyle(0); Host.CineCameraManual = null; Director.AdvanceShot(Host.Lobby, -1); Host.AssignCameraMode("Cinematic"); });
    C.Button(Actions, "Next keyframe", "Play", () => { Director.SetStyle(0); Host.CineCameraManual = null; Director.AdvanceShot(Host.Lobby, 1); Host.AssignCameraMode("Cinematic"); });

    BuildInteractiveCameraCards(C, Body, {
        Accent: Accent.Cameras,
        ReadState: () => ({
            FocalLength: S.CineFocalLength || 50,
            Aperture: S.CineAperture || 2.0,
            FocusDistance: S.CineFocusDistance || 6.5,
            SensorWidth: S.CineSensorWidth || 36.0,
            DofEnabled: S.CineDofEnabled !== false
        }),
        WriteState: (Patch) =>
        {
            if (Patch.FocalLength !== undefined) S.CineFocalLength = Patch.FocalLength;
            if (Patch.Aperture !== undefined) S.CineAperture = Patch.Aperture;
            if (Patch.FocusDistance !== undefined) S.CineFocusDistance = Patch.FocusDistance;
            if (Patch.SensorWidth !== undefined) S.CineSensorWidth = Patch.SensorWidth;
            if (Patch.DofEnabled !== undefined) S.CineDofEnabled = Patch.DofEnabled;
            Host.PriorValid = false;
        }
    });
}

function BuildPostProcess(Host, C, Body)
{
    const S = Host.Settings;
    const Exposure = C.Card(Body, "Exposure & Cockpit Metering", "Aperture", { Accent: Accent.Cameras, Index: 1 });
    C.Metric(Exposure, () => `${(S.ExposureCompensation >= 0 ? "+" : "") + (S.ExposureCompensation || 0).toFixed(2)}`, " EV", 0,
             () => [S.AutoExposure ? "Auto metering" : "Manual EV", "live"]);
    C.Switch(Exposure, S, "AutoExposure", { Label: "Automatic luminance adaptation", Detail: "Histogram eye adaptation across bright tarmac and studio highlights" });
    Slider(C, Exposure, S, "ExposureCompensation", "Global EV Compensation", -4.0, 4.0, 0.05, "EV", 2, ["−4 EV", "+4 EV"]);
    C.Switch(Exposure, S, "CockpitExposure", { Label: "Windshield-weighted cockpit metering",
                                               Detail: "Balances bright exterior road/studio seen through glazing against dark interior trim when inside the cabin" });
    Slider(C, Exposure, S, "CockpitExposureBias", "In-cockpit EV trim", -2.5, 1.5, 0.05, "EV", 2, ["Crisp exterior", "Bright cabin"]);

    const Tone = C.Card(Body, "Tone Mapping", "Palette", { Accent: Accent.Cameras, Index: 2 });
    Slider(C, Tone, S, "PostSaturation", "Saturation", 0.0, 2.0, 0.02, "", 2, ["Monochrome", "Vivid"], () => Host.ApplyPostProcessStyle?.());
    Slider(C, Tone, S, "PostContrast", "Contrast", 0.5, 2.0, 0.02, "", 2, ["Flat", "Crisp"], () => Host.ApplyPostProcessStyle?.());
    C.Switch(Tone, S, "Antialias", { Label: "Temporal anti-aliasing (TAA)", Detail: "Sub-pixel jitter neighbourhood clamp" });

    const LensFx = C.Card(Body, "Lens Effects", "Sparkles", { Accent: Accent.Cameras, Index: 3 });
    Slider(C, LensFx, S, "PostBloom", "Bloom Intensity", 0.0, 1.0, 0.01, "", 2, ["Off", "Strong"], () => Host.ApplyPostProcessStyle?.());
    Slider(C, LensFx, S, "PostVignette", "Vignette", 0.0, 1.0, 0.01, "", 2, ["None", "Cinema"], () => Host.ApplyPostProcessStyle?.());
    Slider(C, LensFx, S, "GlassIndex", "Refraction IOR (glazing)", 1.0, 2.0, 0.01, "", 2, ["Air 1.0", "Crystal 2.0"]);
}

//------------------------------------------------------------------------------------------------------------------------
//                      TRANSFORM CARD, STUDIO LIGHTS, CONSTRUCTED PRIMITIVES & LOBBY BAYS
//------------------------------------------------------------------------------------------------------------------------

function BuildTransformCard(Host, EntityId, C, Body, Options = {})
{
    const PoseNow = () => Host.QueryEntityTransformPose?.(EntityId) || { Position: [0, 0, 0], Rotation: [0, 0, 0], Scale: [1, 1, 1] };
    const Card = C.Card(Body, "Transform · GizmoPRO", "MoveSpace", {
        Accent: Options.AccentColour || Accent.Level,
        Index: 0,
        Subtitle: Options.Subtitle || "Edit mode · G Grab · R Rotate · S Scale (Ctrl snaps)"
    });
    C.Stats(Card, [
        ["Pos X", () => PoseNow().Position[0], "m", 2],
        ["Pos Y", () => PoseNow().Position[1], "m", 2],
        ["Pos Z", () => PoseNow().Position[2], "m", 2]
    ], 3);
    C.Stats(Card, [
        ["Roll",  () => PoseNow().Rotation[0], "°", 1],
        ["Pitch", () => PoseNow().Rotation[1], "°", 1],
        ["Yaw",   () => PoseNow().Rotation[2], "°", 1]
    ], 3);

    const Proxy = {
        get PosX() { return PoseNow().Position[0]; },
        set PosX(V) { const P = PoseNow(); P.Position[0] = V; Host.ApplyEntityTransformPose(EntityId, P); },
        get PosY() { return PoseNow().Position[1]; },
        set PosY(V) { const P = PoseNow(); P.Position[1] = V; Host.ApplyEntityTransformPose(EntityId, P); },
        get PosZ() { return PoseNow().Position[2]; },
        set PosZ(V) { const P = PoseNow(); P.Position[2] = V; Host.ApplyEntityTransformPose(EntityId, P); },
        get Yaw()  { return PoseNow().Rotation[2]; },
        set Yaw(V)  { const P = PoseNow(); P.Rotation[2] = V; Host.ApplyEntityTransformPose(EntityId, P); },
        get Pitch() { return PoseNow().Rotation[1]; },
        set Pitch(V) { const P = PoseNow(); P.Rotation[1] = V; Host.ApplyEntityTransformPose(EntityId, P); },
        get UniformScale() { return PoseNow().Scale[0]; },
        set UniformScale(V) { const P = PoseNow(); P.Scale = [V, V, V]; Host.ApplyEntityTransformPose(EntityId, P); }
    };

    const Span = Host.Lobby ? 18 : 65;
    Slider(C, Card, Proxy, "PosX", "Position X", -Span, Span, 0.05, "m", 2);
    Slider(C, Card, Proxy, "PosY", "Position Y", -Span, Span, 0.05, "m", 2);
    Slider(C, Card, Proxy, "PosZ", "Position Z", -0.5, 14.0, 0.05, "m", 2);
    Slider(C, Card, Proxy, "Yaw", "Rotation Yaw", -180, 180, 1, "°", 0);
    Slider(C, Card, Proxy, "Pitch", "Rotation Pitch", -89, 89, 1, "°", 0);
    if (Options.AllowScale !== false)
        Slider(C, Card, Proxy, "UniformScale", "Uniform Scale", 0.2, 5.0, 0.05, "×", 2);

    const Row = C.ActionRow(Card);
    C.Button(Row, "Edit in Viewport (Gizmo)", "MoveSpace", () =>
    {
        if (Host.Settings.RunMode !== "EDIT")
        {
            Host.AssignRunMode("EDIT");
            Host.Shell?.SyncViewportToolbar?.();
        }
        Host.FocusSelectedEntity?.();
    });
    if (Options.AllowScale !== false)
    {
        C.Button(Row, "Reset Scale", "RotateCcw", () =>
        {
            const P = PoseNow();
            P.Scale = [1, 1, 1];
            Host.ApplyEntityTransformPose(EntityId, P);
            C.Synchronise();
        });
    }
}

function RgbArrayToHex(Arr)
{
    const ToHex = (V) => Math.max(0, Math.min(255, Math.round((V || 0) * 255))).toString(16).padStart(2, "0");
    return `#${ToHex(Arr[0])}${ToHex(Arr[1])}${ToHex(Arr[2])}`;
}

function HexToRgbArray(Hex)
{
    const Clean = String(Hex || "#ffffff").replace("#", "");
    if (Clean.length !== 6) return [1, 1, 1];
    return [
        parseInt(Clean.slice(0, 2), 16) / 255,
        parseInt(Clean.slice(2, 4), 16) / 255,
        parseInt(Clean.slice(4, 6), 16) / 255
    ];
}

function BuildStudioLight(Host, Light, Index, C, Body)
{
    const EntityId = Light.Id || `studio-light-${Index}`;
    BuildTransformCard(Host, EntityId, C, Body, {
        AllowScale: false,
        AccentColour: Accent.Lights,
        Subtitle: "Move & aim studio fixture in Edit mode (G / R)"
    });

    const Photo = C.Card(Body, "Studio Photometry & Beam", "Lightbulb", {
        Accent: Accent.Lights,
        Index: 1,
        Subtitle: Light.SoftboxLinked ? "Synchronized with overhead emissive softbox diffuser" : "Clustered studio fixture · direct + card bounce"
    });
    C.Metric(Photo, () => Math.round(Light.Intensity), " cd", 0,
             () => [Light.Enabled !== false ? (Light.Role || "Studio active") : "Disabled", Light.Enabled !== false ? "live" : ""]);

    const BeamProxy = {
        get Intensity() { return Light.Intensity; },
        set Intensity(V) { Light.Intensity = V; Host.SyncStudioLightMaterial?.(); },
        get Reach() { return Light.Reach; },
        set Reach(V) { Light.Reach = V; Host.SyncStudioLightMaterial?.(); },
        get InnerDeg() { return Math.acos(Math.max(-1, Math.min(1, Light.Params[0] ?? 0.9))) * 180 / Math.PI; },
        set InnerDeg(Deg)
        {
            const Outer = Math.max(Deg + 2, this.OuterDeg);
            Light.Params[0] = Math.cos(Deg * Math.PI / 180);
            Light.Params[1] = Math.cos(Outer * Math.PI / 180);
            Host.SyncStudioLightMaterial?.();
        },
        get OuterDeg() { return Math.acos(Math.max(-1, Math.min(1, Light.Params[1] ?? 0.3))) * 180 / Math.PI; },
        set OuterDeg(Deg)
        {
            const Clamped = Math.max(this.InnerDeg + 2, Deg);
            Light.Params[1] = Math.cos(Clamped * Math.PI / 180);
            Host.SyncStudioLightMaterial?.();
        },
        get ColourHex() { return RgbArrayToHex(Light.Colour); },
        set ColourHex(Hex) { Light.Colour = HexToRgbArray(Hex); Host.SyncStudioLightMaterial?.(); }
    };

    C.Select(Photo, Light, "Type", [["Flood / Softbox", LightType.Floodlight], ["Omni Point", LightType.Omni], ["Collimated Spot", LightType.HighBeam]],
             { Label: "Fixture distribution", OnChange: (Val) => { Light.Type = Number(Val); Host.SyncStudioLightMaterial?.(); } });
    Slider(C, Photo, BeamProxy, "Intensity", "Luminous intensity", 0, 2200, 10, "cd", 0, ["Off", "Key"]);
    Slider(C, Photo, BeamProxy, "Reach", "Attenuation reach", 4, 65, 1, "m", 0);
    if (Light.Type !== LightType.Omni)
    {
        Slider(C, Photo, BeamProxy, "InnerDeg", "Inner hotspot half-angle", 4, 75, 1, "°", 0);
        Slider(C, Photo, BeamProxy, "OuterDeg", "Outer falloff half-angle", 8, 88, 1, "°", 0);
    }
    C.Colour(Photo, BeamProxy, "ColourHex", { Label: "Emitter tint", OnChange: () => Host.SyncStudioLightMaterial?.() });

    const Actions = C.ActionRow(Photo);
    C.Button(Actions, "Aim at ControlVehicle", "Car", () =>
    {
        const Target = Host.Vehicle ? Host.Vehicle.Position : [0, 0, 0.5];
        const Dx = Target[0] - Light.Position[0], Dy = Target[1] - Light.Position[1], Dz = Target[2] - Light.Position[2];
        const Len = Math.hypot(Dx, Dy, Dz) || 1;
        Light.Direction = [Dx / Len, Dy / Len, Dz / Len];
        Host.SyncStudioLightMaterial?.();
        C.Synchronise();
    });
    C.Button(Actions, "+ Add Studio Light", "Lightbulb", () => Host.ConstructStudioLight?.("Spot"));
}

function BuildPrimitiveInspector(Host, Prim, C, Body)
{
    BuildTransformCard(Host, Prim.Id, C, Body, { AccentColour: Accent.Level });
    BuildArrayModifierCard(Host, Prim.Id, C, Body, Accent.Level);
}

function BuildShapeInspector(Host, ShapeId, Occ, C, Body)
{
    BuildTransformCard(Host, ShapeId, C, Body, { AccentColour: Accent.Level });
    BuildArrayModifierCard(Host, ShapeId, C, Body, Accent.Level);
    const Card = C.Card(Body, Occ.Name || "Scene Shape", "Box", { Accent: Accent.Level, Index: 3 });
    C.Muted(Card, `Scene shape (${Occ.Level}) · click and drag the GizmoPRO handles or press G / R / S in Edit mode to move, rotate, or scale.`);
}

function BuildLobbyBayInspector(Host, BayId, Idx, C, Body)
{
    BuildTransformCard(Host, BayId, C, Body, { AccentColour: Accent.Vehicle });
    BuildArrayModifierCard(Host, BayId, C, Body, Accent.Vehicle);
    const Card = C.Card(Body, `Turntable Bay ${Idx + 1}`, "Car", { Accent: Accent.Vehicle, Index: 3 });
    C.Muted(Card, "Select any turntable vehicle directly in the viewport in Edit mode to translate, rotate, scale, or array it.");
}

function BuildLobbySession(Host, C, Body)
{
    const Session = Host.LobbySession;
    const Switcher = C.Card(Body, "Active scene level", "Map", { Accent: Accent.Level, Index: 1 });
    C.Segmented(Switcher, { Level: Host.Lobby ? "lobby" : "proving" }, "Level",
        LevelAtlas.map((Entry) => [Entry.Title, Entry.Key]),
        { Label: "Switch level", OnChange: (Key) => Host.SwitchLevel(Key) });
    C.Muted(Switcher, "Switch between the outdoor Proving Grounds and the 40 m cyclorama Lobby · Studio while keeping the full dockable Editor UI.");

    const Roster = C.Card(Body, "Studio turntable session", "Lobby", { Accent: Accent.Level, Index: 2, Subtitle: "5 turntables · simulated matchmaking" });
    if (!Session || !Session.QueryActive())
    {
        C.Muted(Roster, "Switch to Lobby · Studio above to enter the 5-turntable automotive cyclorama and watch simulated online players roll into and out of the surrounding bays.");
        const GoRow = C.ActionRow(Roster);
        C.Button(GoRow, "Enter Lobby · Studio", "Play", () => Host.SwitchLevel("lobby"));
        return;
    }
    C.Metric(Roster, () => Session.QueryOccupiedCount(), "/ 5 turntables occupied", 0,
             () => [Session.Settings.AutoSimulate ? "auto matchmaking" : "manual", "live"]);
    C.Switch(Roster, Session.Settings, "AutoSimulate", { Label: "Auto-simulate players entering & leaving", Detail: "Online drivers roll onto and off the 4 guest turntables" });
    Slider(C, Roster, Session.Settings, "IntervalSeconds", "Event cadence", 2.5, 18.0, 0.5, "s", 1, ["Busy", "Calm"]);
    const SimRow = C.ActionRow(Roster);
    C.Button(SimRow, "Player join", "Plus", () => Session.TriggerJoin());
    C.Button(SimRow, "Player leave", "RotateCcw", () => Session.TriggerLeave());
    const BatchRow = C.ActionRow(Roster);
    C.Button(BatchRow, "Fill all bays", "Check", () => Session.FillAllBays());
    C.Button(BatchRow, "Clear guest bays", "Trash2", () => Session.ClearAllBays(), "danger");
    C.Readout(Roster, () => [...Session.QuerySummaryLines(), ...(Session.History.length ? ["— Recent events —", ...Session.History.slice(0, 4)] : [])]);
}
