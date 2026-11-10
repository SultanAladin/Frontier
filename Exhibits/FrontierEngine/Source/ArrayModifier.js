//============================================================================================================================================
//                                                            ARRAYMODIFIER.JS
//============================================================================================================================================
// 🧩 Non-destructive 3D Array Modifier (Linear, Circular, Radial Multi-Ring, and 2D/3D Grid) with progressive scale/rotation
//    envelopes and live top-down SVG blueprint preview, matching Frontier's ArtworkArrays vocabulary in full 3D (+Z up).

import {
    AddVector, ScaleVector, ConstructAxisQuaternion, MultiplyQuaternion,
    NormalizeQuaternion, RotateVector, ConstructRigidTransform
} from "./TransformProjection.js";

export const ArrayLayoutModes = Object.freeze([
    ["Linear",        "linear"],
    ["Circular Arc",  "circular"],
    ["Radial Rings",  "radial"],
    ["Grid Lattice",  "grid"]
]);

export const ArrayScaleProfiles = Object.freeze([
    ["Uniform",            "uniform"],
    ["Linear Progression", "linear"],
    ["Cosine Wave",        "wave"]
]);

export function CreateDefaultArraySpec(Overrides = {})
{
    const Patch = typeof Overrides === "string" ? { Mode: Overrides } : (Overrides || {});
    return {
        Enabled:        false,
        Mode:           "linear",     // "linear" | "circular" | "radial" | "grid"
        Count:          5,            // copies for linear / circular / per-ring
        StepX:          2.6,          // [m] linear / grid X spacing
        StepY:          0.0,          // [m] linear / grid Y spacing
        StepZ:          0.0,          // [m] linear / grid Z spacing / helical rise
        StepYaw:        0.0,          // [deg] per-step yaw rotation in linear mode
        Radius:         5.5,          // [m] circular / radial base radius
        Rings:          2,            // [-] radial ring count
        RingSpacing:    3.2,          // [m] radial distance between rings
        StartAngle:     0.0,          // [deg]
        Sweep:          360.0,        // [deg]
        OrientToCenter: true,         // rotate copies to face/follow ring tangent
        StaggerRings:   true,         // half-step angle offset on odd radial rings
        Columns:        3,            // [-] grid X count
        Rows:           3,            // [-] grid Y count
        Layers:         1,            // [-] grid Z count
        CenterGrid:     true,
        ScaleProfile:   "uniform",    // "uniform" | "linear" | "wave"
        EndScale:       1.0,          // [-] end scale multiplier when ScaleProfile != uniform
        ...Patch
    };
}

// Composes ZYX intrinsic Euler angles [rad] (Yaw around +Z, Pitch around +Y, Roll around +X) into [x, y, z, w].
export function ComposeEulerRad(RollRad = 0, PitchRad = 0, YawRad = 0)
{
    const Qx = ConstructAxisQuaternion([1, 0, 0], RollRad);
    const Qy = ConstructAxisQuaternion([0, 1, 0], PitchRad);
    const Qz = ConstructAxisQuaternion([0, 0, 1], YawRad);
    return NormalizeQuaternion(MultiplyQuaternion(Qz, MultiplyQuaternion(Qy, Qx)));
}

// Decomposes a column-major 4x4 T*R*S matrix into { Position, Quaternion, EulerDeg, Scale }.
export function DecomposeTransform(Matrix)
{
    const Position = [Matrix[12], Matrix[13], Matrix[14]];
    const Sx = Math.hypot(Matrix[0], Matrix[1], Matrix[2]) || 1.0;
    const Sy = Math.hypot(Matrix[4], Matrix[5], Matrix[6]) || 1.0;
    const Sz = Math.hypot(Matrix[8], Matrix[9], Matrix[10]) || 1.0;
    const R00 = Matrix[0] / Sx, R01 = Matrix[4] / Sy, R02 = Matrix[8]  / Sz;
    const R10 = Matrix[1] / Sx, R11 = Matrix[5] / Sy, R12 = Matrix[9]  / Sz;
    const R20 = Matrix[2] / Sx, R21 = Matrix[6] / Sy, R22 = Matrix[10] / Sz;

    const Trace = R00 + R11 + R22;
    let Qx, Qy, Qz, Qw;
    if (Trace > 0.0)
    {
        const S = Math.sqrt(Trace + 1.0) * 2.0;
        Qw = 0.25 * S;
        Qx = (R21 - R12) / S;
        Qy = (R02 - R20) / S;
        Qz = (R10 - R01) / S;
    }
    else if (R00 > R11 && R00 > R22)
    {
        const S = Math.sqrt(1.0 + R00 - R11 - R22) * 2.0;
        Qw = (R21 - R12) / S;
        Qx = 0.25 * S;
        Qy = (R01 + R10) / S;
        Qz = (R02 + R20) / S;
    }
    else if (R11 > R22)
    {
        const S = Math.sqrt(1.0 + R11 - R00 - R22) * 2.0;
        Qw = (R02 - R20) / S;
        Qx = (R01 + R10) / S;
        Qy = 0.25 * S;
        Qz = (R12 + R21) / S;
    }
    else
    {
        const S = Math.sqrt(1.0 + R22 - R00 - R11) * 2.0;
        Qw = (R10 - R01) / S;
        Qx = (R02 + R20) / S;
        Qy = (R12 + R21) / S;
        Qz = 0.25 * S;
    }
    const Quaternion = NormalizeQuaternion([Qx, Qy, Qz, Qw]);
    const SinPitch = Math.max(-1.0, Math.min(1.0, -R20));
    const Pitch = Math.asin(SinPitch);
    let Roll, Yaw;
    if (Math.abs(SinPitch) < 0.9999)
    {
        Roll = Math.atan2(R21, R22);
        Yaw  = Math.atan2(R10, R00);
    }
    else
    {
        Roll = 0.0;
        Yaw  = Math.atan2(-R01, R11);
    }
    const RadToDeg = 180.0 / Math.PI;
    return {
        Position,
        Quaternion,
        EulerDeg: [Roll * RadToDeg, Pitch * RadToDeg, Yaw * RadToDeg],
        Scale: [Sx, Sy, Sz]
    };
}

function ResolveScaleFactor(Spec, Index, TotalCount)
{
    if (Spec.ScaleProfile === "uniform" || TotalCount <= 1)
        return 1.0;
    const T = Index / Math.max(1, TotalCount - 1);
    const End = Math.max(0.15, Math.min(4.0, Number(Spec.EndScale) || 1.0));
    if (Spec.ScaleProfile === "linear")
        return 1.0 + (End - 1.0) * T;
    if (Spec.ScaleProfile === "wave")
        return 1.0 + (End - 1.0) * (0.5 - 0.5 * Math.cos(2.0 * Math.PI * T));
    return 1.0;
}

// Computes all placements [{ Position, Quaternion, Scale, Transform, RelX, RelY, YawRad }] INCLUDING slot 0 (the source)
// and returns ExtraPlacements = Slots.slice(1) for the pooled GPU clone occupants.
export function ComputeArrayPlacements(BasePoseInput, Spec, MaxExtraCopies = 48)
{
    const BasePose  = (ArrayBuffer.isView(BasePoseInput) || Array.isArray(BasePoseInput))
                    ? DecomposeTransform(BasePoseInput)
                    : (BasePoseInput || {});
    const BasePos   = BasePose.Position || [0, 0, 0];
    const BaseQuat  = BasePose.Quaternion
                    || (BasePose.Rotation
                        ? ComposeEulerRad(
                            (BasePose.Rotation[0] || 0) * Math.PI / 180,
                            (BasePose.Rotation[1] || 0) * Math.PI / 180,
                            (BasePose.Rotation[2] || 0) * Math.PI / 180)
                        : [0, 0, 0, 1]);
    const BaseScale = BasePose.Scale || [1, 1, 1];

    if (!Spec || !Spec.Enabled)
        return { All: [{ Position: BasePos, Quaternion: BaseQuat, Scale: BaseScale, RelX: 0, RelY: 0, YawRad: 0 }], Extra: [] };

    const Mode = Spec.Mode || "linear";
    const RawEntries = [];

    if (Mode === "linear")
    {
        const Count = Math.max(1, Math.min(MaxExtraCopies + 1, Math.round(Spec.Count || 1)));
        const StepLocal = [Number(Spec.StepX) || 0, Number(Spec.StepY) || 0, Number(Spec.StepZ) || 0];
        const StepYawRad = ((Number(Spec.StepYaw) || 0) * Math.PI) / 180.0;
        for (let I = 0; I < Count; ++I)
        {
            const OffsetWorld = RotateVector(BaseQuat, ScaleVector(StepLocal, I));
            RawEntries.push({
                Offset: OffsetWorld,
                RelX: StepLocal[0] * I,
                RelY: StepLocal[1] * I,
                YawDelta: StepYawRad * I
            });
        }
    }
    else if (Mode === "circular" || Mode === "radial")
    {
        const PerRing = Math.max(2, Math.min(32, Math.round(Spec.Count || 6)));
        const Rings = Mode === "radial" ? Math.max(1, Math.min(5, Math.round(Spec.Rings || 2))) : 1;
        const BaseRadius = Math.max(0.25, Number(Spec.Radius) || 5.0);
        const RingStep = Mode === "radial" ? Math.max(0.5, Number(Spec.RingSpacing) || 3.0) : 0.0;
        const SweepDeg = Number(Spec.Sweep) || 360.0;
        const StartDeg = Number(Spec.StartAngle) || 0.0;
        const FullCircle = Math.abs(Math.abs(SweepDeg) - 360.0) < 0.5;
        const HelicalZ = Number(Spec.StepZ) || 0.0;

        // Centre of the ring is offset by -Radius along local +X from BasePos so slot 0 sits right at BasePos when StartAngle=0!
        const LocalAnchorAngle = (StartDeg * Math.PI) / 180.0;
        const RingCenterOffsetLocal = [-Math.cos(LocalAnchorAngle) * BaseRadius, -Math.sin(LocalAnchorAngle) * BaseRadius, 0.0];

        for (let Ring = 0; Ring < Rings; ++Ring)
        {
            const R = BaseRadius + Ring * RingStep;
            const Stagger = (Mode === "radial" && Spec.StaggerRings && (Ring % 2 === 1)) ? (0.5 * SweepDeg) / PerRing : 0.0;
            for (let K = 0; K < PerRing; ++K)
            {
                if (RawEntries.length >= MaxExtraCopies + 1)
                    break;
                const T = PerRing <= 1 ? 0 : K / (FullCircle ? PerRing : PerRing - 1);
                const AngleDeg = StartDeg + Stagger + SweepDeg * T;
                const AngleRad = (AngleDeg * Math.PI) / 180.0;
                const LocalPt = [
                    RingCenterOffsetLocal[0] + Math.cos(AngleRad) * R,
                    RingCenterOffsetLocal[1] + Math.sin(AngleRad) * R,
                    HelicalZ * RawEntries.length
                ];
                RawEntries.push({
                    Offset: RotateVector(BaseQuat, LocalPt),
                    RelX: LocalPt[0],
                    RelY: LocalPt[1],
                    YawDelta: Spec.OrientToCenter ? (AngleRad - LocalAnchorAngle) : 0.0
                });
            }
        }
    }
    else if (Mode === "grid")
    {
        const Cols = Math.max(1, Math.min(8, Math.round(Spec.Columns || 3)));
        const Rows = Math.max(1, Math.min(8, Math.round(Spec.Rows || 3)));
        const Layers = Math.max(1, Math.min(4, Math.round(Spec.Layers || 1)));
        const Sx = Number(Spec.StepX) || 2.5;
        const Sy = Number(Spec.StepY) || 2.5;
        const Sz = Number(Spec.StepZ) || 2.0;

        for (let Lz = 0; Lz < Layers; ++Lz)
        {
            for (let Ry = 0; Ry < Rows; ++Ry)
            {
                for (let Cx = 0; Cx < Cols; ++Cx)
                {
                    if (RawEntries.length >= MaxExtraCopies + 1)
                        break;
                    const LocalPt = [Cx * Sx, Ry * Sy, Lz * Sz];
                    RawEntries.push({
                        Offset: RotateVector(BaseQuat, LocalPt),
                        RelX: LocalPt[0],
                        RelY: LocalPt[1],
                        YawDelta: 0.0
                    });
                }
            }
        }
    }

    const Total = RawEntries.length;
    const All = RawEntries.map((Item, Index) =>
    {
        const Factor = Index === 0 ? 1.0 : ResolveScaleFactor(Spec, Index, Total);
        const YawQuat = ConstructAxisQuaternion([0, 0, 1], Item.YawDelta);
        const Quat = NormalizeQuaternion(MultiplyQuaternion(YawQuat, BaseQuat));
        const Pos = AddVector(BasePos, Item.Offset);
        const Scale = [BaseScale[0] * Factor, BaseScale[1] * Factor, BaseScale[2] * Factor];
        return {
            Position: Pos,
            Quaternion: Quat,
            Scale,
            Transform: ConstructRigidTransform(Pos, Quat, Scale),
            RelX: Item.RelX,
            RelY: Item.RelY,
            ScaleFactor: Factor,
            YawRad: Item.YawDelta
        };
    });

    return { All, Extra: All.slice(1) };
}

function RenderArrayBlueprintSvg(Placements, Mode)
{
    const Points = Placements.All || [];
    let MinX = -2, MaxX = 2, MinY = -2, MaxY = 2;
    for (const P of Points)
    {
        MinX = Math.min(MinX, P.RelX - 1);
        MaxX = Math.max(MaxX, P.RelX + 1);
        MinY = Math.min(MinY, P.RelY - 1);
        MaxY = Math.max(MaxY, P.RelY + 1);
    }
    const Span = Math.max(MaxX - MinX, MaxY - MinY, 4.0);
    const MidX = 0.5 * (MinX + MaxX);
    const MidY = 0.5 * (MinY + MaxY);
    const MapX = (X) => 170 + ((X - MidX) / Span) * 250;
    const MapY = (Y) => 78 - ((Y - MidY) / Span) * 116;

    const Dots = Points.map((P, Index) =>
    {
        const Cx = MapX(P.RelX);
        const Cy = MapY(P.RelY);
        const R = Math.max(3.0, Math.min(8.5, 5.2 * (P.ScaleFactor || 1)));
        const Hx = Cx + Math.cos(P.YawRad || 0) * (R + 4.5);
        const Hy = Cy - Math.sin(P.YawRad || 0) * (R + 4.5);
        const Fill = Index === 0 ? "#f5d565" : "#8ab4f8";
        const Stroke = Index === 0 ? "#fff3b0" : "#bcd6ff";
        return `<g>
            <line x1="${Cx.toFixed(1)}" y1="${Cy.toFixed(1)}" x2="${Hx.toFixed(1)}" y2="${Hy.toFixed(1)}" stroke="${Stroke}" stroke-width="1.2" stroke-opacity="0.75"/>
            <circle cx="${Cx.toFixed(1)}" cy="${Cy.toFixed(1)}" r="${R.toFixed(1)}" fill="${Fill}" fill-opacity="${Index === 0 ? "0.95" : "0.72"}" stroke="${Stroke}" stroke-width="1"/>
        </g>`;
    }).join("");

    return `<svg viewBox="0 0 340 156" role="img" aria-label="Array Modifier top-down layout preview">
        <rect x="8" y="8" width="324" height="140" rx="10" fill="#151619" stroke="#ffffff12"/>
        <line x1="170" y1="14" x2="170" y2="142" stroke="#ffffff0d" stroke-dasharray="3 4"/>
        <line x1="16" y1="78" x2="324" y2="78" stroke="#ffffff0d" stroke-dasharray="3 4"/>
        ${Dots}
        <text x="18" y="24" fill="#8b94a3" font-size="8.5" letter-spacing="0.09em">${Mode.toUpperCase()} · ${Points.length} TOTAL (${Math.max(0, Points.length - 1)} CLONES)</text>
        <text x="18" y="140" fill="#6f7785" font-size="8">Gold = Source pivot · Blue = Live GPU instances</text>
    </svg>`;
}

// Builds the Array Modifier inspector card for any transformable/arrayable scene entity.
export function BuildArrayModifierCard(Host, EntityId, Controls, Body, AccentColour = "#8ab4f8")
{
    const Spec = Host.EnsureArrayModifier(EntityId);
    const Card = Controls.Card(Body, "Array Modifier", "GridLattice", {
        Accent: AccentColour,
        Index: 4,
        Subtitle: "NON-DESTRUCTIVE PROCEDURAL INSTANCING (LINEAR · CIRCULAR · RADIAL · GRID)"
    });

    let RefreshDiagram = () => {};

    Controls.Switch(Card, Spec, "Enabled", {
        Label: "Enable Array Modifier",
        Detail: "Instantiates live GPU BVH & raster copies",
        OnChange: () => { Host.EditorOccupantsDirty = true; Host.SyncArrayModifiers(); Host.Scene?.ConstructDrawRuns(); Host.PriorValid = false; RefreshDiagram(); }
    });
    Controls.Select(Card, Spec, "Mode", ArrayLayoutModes, {
        Label: "Topology",
        OnChange: (Val) => { Spec.Mode = String(Val); Host.EditorOccupantsDirty = true; RefreshDiagram(); }
    });

    const DiagramBox = document.createElement("div");
    DiagramBox.className = "fe-camera-diagram fe-array-diagram";
    Card.appendChild(DiagramBox);

    // Dynamic parameter container rebuilt when Mode changes
    const ParamsHost = document.createElement("div");
    ParamsHost.className = "fe-array-params";
    Card.appendChild(ParamsHost);

    const SubControls = new Controls.constructor((Key, Target) =>
    {
        Host.EditorOccupantsDirty = true;
        Host.SyncArrayModifiers();
        Host.Scene?.ConstructDrawRuns();
        Host.PriorValid = false;
        RefreshDiagram();
        Controls.Notify(Key, Target);
    });

    const AddSlider = (Key, Label, Minimum, Maximum, Step, Unit = "", Digits = 1) =>
        SubControls.Slider(ParamsHost, Spec, Key, {
            Label, Minimum, Maximum, Step, Unit, Digits,
            OnInput: () => { Host.EditorOccupantsDirty = true; Host.SyncArrayModifiers(); Host.Scene?.ConstructDrawRuns(); Host.PriorValid = false; RefreshDiagram(); }
        });

    const RebuildModeSliders = () =>
    {
        if (ParamsHost.replaceChildren)
            ParamsHost.replaceChildren();
        else
            ParamsHost.children = [];
        SubControls.Reset();

        if (Spec.Mode === "linear")
        {
            AddSlider("Count", "Instance count", 1, 24, 1, "", 0);
            AddSlider("StepX", "Offset X (forward)", -16.0, 16.0, 0.2, "m", 1);
            AddSlider("StepY", "Offset Y (lateral)", -16.0, 16.0, 0.2, "m", 1);
            AddSlider("StepZ", "Offset Z (vertical)", -8.0, 8.0, 0.1, "m", 1);
            AddSlider("StepYaw", "Step rotation (yaw)", -90, 90, 1, "°", 0);
        }
        else if (Spec.Mode === "circular" || Spec.Mode === "radial")
        {
            AddSlider("Count", Spec.Mode === "radial" ? "Copies per ring" : "Ring count", 2, 24, 1, "", 0);
            AddSlider("Radius", "Radius", 1.0, 22.0, 0.25, "m", 2);
            if (Spec.Mode === "radial")
            {
                AddSlider("Rings", "Concentric rings", 1, 4, 1, "", 0);
                AddSlider("RingSpacing", "Ring spacing", 1.0, 10.0, 0.25, "m", 2);
                SubControls.Switch(ParamsHost, Spec, "StaggerRings", {
                    Label: "Stagger alternating rings",
                    Detail: "Brick-offset odd rings by half step",
                    OnChange: () => { Host.EditorOccupantsDirty = true; RefreshDiagram(); }
                });
            }
            AddSlider("Sweep", "Arc sweep", -360, 360, 5, "°", 0);
            AddSlider("StartAngle", "Start angle", -180, 180, 5, "°", 0);
            AddSlider("StepZ", "Helical rise Z", -2.0, 2.0, 0.05, "m", 2);
            SubControls.Switch(ParamsHost, Spec, "OrientToCenter", {
                Label: "Orient copies to center",
                Detail: "Rotate each copy along radial tangent",
                OnChange: () => { Host.EditorOccupantsDirty = true; RefreshDiagram(); }
            });
        }
        else if (Spec.Mode === "grid")
        {
            AddSlider("Columns", "Columns (X)", 1, 6, 1, "", 0);
            AddSlider("Rows", "Rows (Y)", 1, 6, 1, "", 0);
            AddSlider("Layers", "Layers (Z)", 1, 3, 1, "", 0);
            AddSlider("StepX", "Spacing X", 0.5, 14.0, 0.25, "m", 2);
            AddSlider("StepY", "Spacing Y", 0.5, 14.0, 0.25, "m", 2);
            AddSlider("StepZ", "Spacing Z", 0.5, 8.0, 0.25, "m", 2);
        }

        SubControls.Select(ParamsHost, Spec, "ScaleProfile", ArrayScaleProfiles, {
            Label: "Scale envelope",
            OnChange: (Val) => { Spec.ScaleProfile = String(Val); Host.EditorOccupantsDirty = true; RefreshDiagram(); }
        });
        if (Spec.ScaleProfile !== "uniform")
            AddSlider("EndScale", "End scale multiplier", 0.2, 2.5, 0.05, "×", 2);
    };

    let LastMode = Spec.Mode;
    let LastScaleProfile = Spec.ScaleProfile;

    RefreshDiagram = () =>
    {
        if (Spec.Mode !== LastMode || Spec.ScaleProfile !== LastScaleProfile)
        {
            LastMode = Spec.Mode;
            LastScaleProfile = Spec.ScaleProfile;
            RebuildModeSliders();
        }
        Host.SyncArrayModifiers();
        const BasePose = Host.QueryEntityTransformPose(EntityId) || { Position: [0, 0, 0], Rotation: [0, 0, 0], Scale: [1, 1, 1] };
        const Computed = ComputeArrayPlacements(BasePose, { ...Spec, Enabled: true });
        DiagramBox.innerHTML = RenderArrayBlueprintSvg(Computed, Spec.Mode);
        DiagramBox.style.opacity = Spec.Enabled ? "1" : "0.52";
    };

    RebuildModeSliders();
    RefreshDiagram();
    Controls.Bindings.push(() => { SubControls.Synchronise(); RefreshDiagram(); });
}
