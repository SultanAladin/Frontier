//============================================================================================================================================
//                                                           TRANSFORMGIZMO.JS
//============================================================================================================================================
// 🧩 1:1 port of References/Gizmo.html ("GizmoPRO 3D") + Slate TransformGizmo.h pixel-locked sizing (AxisEnd = 84 px),
//    paired with WebGPU G-buffer identity selection + synchronous ray picking and Blender-style G / R / S modal + handle
//    transform controls engineered to never collide with RMB+WASD camera navigation.

import {
    AddVector, SubtractVector, ScaleVector, DotVector, CrossVector, LengthVector, NormalizeVector,
    MultiplyQuaternion, NormalizeQuaternion, ConstructAxisQuaternion, RotateVector,
    ConstructRigidTransform, TransformPoint, InvertTransform
} from "./TransformProjection.js";

// Exact Gizmo.html ("GizmoPRO") color specification
const GIZMO_HEX = Object.freeze({
    x:       "#e01414",   // +X red
    y:       "#12d40a",   // +Y green
    z:       "#1560e0",   // +Z blue
    cyan:    "#1fc7c7",   // YZ plane (normal X)
    magenta: "#c81ec8",   // XZ plane (normal Y)
    yellow:  "#e0cd12",   // XY plane (normal Z)
    ring:    "#ffffff"    // central camera-facing torus
});

const AXES = Object.freeze([
    { Name: "x", Color: GIZMO_HEX.x, Dir: [1, 0, 0], PlaneColor: GIZMO_HEX.cyan,    U: [0, 1, 0], V: [0, 0, 1] },
    { Name: "y", Color: GIZMO_HEX.y, Dir: [0, 1, 0], PlaneColor: GIZMO_HEX.magenta, U: [1, 0, 0], V: [0, 0, 1] },
    { Name: "z", Color: GIZMO_HEX.z, Dir: [0, 0, 1], PlaneColor: GIZMO_HEX.yellow,  U: [1, 0, 0], V: [0, 1, 0] }
]);

// Exact Gizmo.html + TransformGizmo.h proportions (TIP = 0.95 normalized -> AxisEnd = 86 px on screen)
const AXIS_END_PX  = 86.0;
const TIP          = 0.95;
const CONE_R       = 0.064;
const CONE_LEN     = 0.19;
const SCALE_R      = 0.058;
const SCALE_LEN    = 0.14;
const SCALE_DIST   = TIP - 0.28;
const PLANE_HALF   = 0.085;
const ARC_RADIUS   = TIP * 0.62;
const ARC_BAND     = 0.040;
const ARC_SWEEP    = (31.0 * Math.PI) / 180.0;
const RING_RADIUS  = 0.16;

const SNAP_MOVE    = 0.25;
const SNAP_SCALE   = 0.10;
const SNAP_ANGLE   = (5.0 * Math.PI) / 180.0;

function SnapScalar(Val, Step)
{
    return Math.round(Val / Step) * Step;
}

function ParseHexRgb(Hex)
{
    const Clean = Hex.replace("#", "");
    const Num = parseInt(Clean, 16);
    return [(Num >> 16) & 255, (Num >> 8) & 255, Num & 255];
}

function ShadeColor(Hex, Factor, EmissiveBoost = 0)
{
    const [R, G, B] = ParseHexRgb(Hex);
    const Clamp = (V) => Math.max(0, Math.min(255, Math.round(V)));
    return `rgb(${Clamp(R * Factor + EmissiveBoost)}, ${Clamp(G * Factor + EmissiveBoost)}, ${Clamp(B * Factor + EmissiveBoost)})`;
}

function OrthonormalBasisFromAxis(Dir)
{
    const N = NormalizeVector(Dir);
    const Ref = Math.abs(N[2]) < 0.85 ? [0, 0, 1] : [1, 0, 0];
    const U = NormalizeVector(CrossVector(Ref, N));
    const V = NormalizeVector(CrossVector(N, U));
    return { N, U, V };
}

function PointToSegmentDist2D(Px, Py, Ax, Ay, Bx, By)
{
    const Dx = Bx - Ax, Dy = By - Ay;
    const Len2 = Dx * Dx + Dy * Dy;
    if (Len2 < 1e-6)
        return Math.hypot(Px - Ax, Py - Ay);
    const T = Math.max(0, Math.min(1, ((Px - Ax) * Dx + (Py - Ay) * Dy) / Len2));
    return Math.hypot(Px - (Ax + T * Dx), Py - (Ay + T * Dy));
}

function PointInQuad2D(Px, Py, Quad)
{
    let Inside = false;
    for (let I = 0, J = Quad.length - 1; I < Quad.length; J = I++)
    {
        const Xi = Quad[I][0], Yi = Quad[I][1];
        const Xj = Quad[J][0], Yj = Quad[J][1];
        const Intersect = ((Yi > Py) !== (Yj > Py)) && (Px < ((Xj - Xi) * (Py - Yi)) / ((Yj - Yi) || 1e-9) + Xi);
        if (Intersect)
            Inside = !Inside;
    }
    return Inside;
}

export class TransformGizmoController
{
    constructor(Host)
    {
        this.Host          = Host;
        this.Mode          = "all";      // "all" (GizmoPRO unified) | "translate" | "rotate" | "scale" | "select"
        this.Space         = "local";    // "local" | "world"
        this.HoveredHandle = null;
        this.Drag          = null;       // active handle drag OR Blender modal G/R/S state
        this.ScreenHandles = [];
        this.PointerClient = [0, 0];
        this.RmbHeld       = false;
        this.LastFlyAt     = 0;

        this.MountDom();
    }

    MountDom()
    {
        const Stage = document.getElementById("stage");
        if (!Stage || !document.createElement)
            return;

        // 2D high-DPI overlay canvas for GizmoPRO 3D shaded geometry, camera frustums & selection brackets
        this.OverlayCanvas = document.createElement("canvas");
        this.OverlayCanvas.id = "gizmo-overlay";
        this.OverlayCanvas.className = "gizmo-overlay-canvas";
        Stage.appendChild(this.OverlayCanvas);

        // Gizmo.html #readout pill at top-center of viewport
        this.ReadoutEl = document.createElement("div");
        this.ReadoutEl.id = "gizmo-readout";
        this.ReadoutEl.className = "gizmo-readout hidden";
        Stage.appendChild(this.ReadoutEl);

        // Compact floating Edit-mode Gizmo toolbar pill at top-left of stage
        this.ToolbarEl = document.createElement("div");
        this.ToolbarEl.id = "gizmo-toolbar";
        this.ToolbarEl.className = "gizmo-toolbar";
        this.RenderToolbar();
        Stage.appendChild(this.ToolbarEl);
    }

    RenderToolbar()
    {
        if (!this.ToolbarEl)
            return;
        const Items = [
            ["all",       "GizmoPRO",  "Unified Translate + Rotate + Scale (Gizmo.html)"],
            ["translate", "Move · G",  "Translate (press G in Edit mode)"],
            ["rotate",    "Rot · R",   "Rotate (press R in Edit mode)"],
            ["scale",     "Scale · S", "Scale (press S in Edit mode; hold RMB+WASD to fly)"]
        ];
        const SocketsOn = this.Host?.ShowSockets !== false;
        this.ToolbarEl.innerHTML = Items.map(([Key, Label, Tip]) =>
            `<button type="button" data-gizmo-mode="${Key}" class="${this.Mode === Key ? "active" : ""}" title="${Tip}">${Label}</button>`
        ).join("") +
        `<span class="gizmo-sep"></span>` +
        `<button type="button" data-gizmo-space="toggle" class="space-btn" title="Toggle Local / World gizmo orientation">${this.Space === "local" ? "Local" : "World"}</button>` +
        `<button type="button" data-gizmo-sockets="toggle" class="space-btn ${SocketsOn ? "active" : ""}" title="Toggle Editor Socket Location Visual Debug (hidden in Play mode)">Sockets</button>`;

        const Buttons = this.ToolbarEl.querySelectorAll ? this.ToolbarEl.querySelectorAll("button") : [];
        for (const Btn of Buttons)
        {
            Btn.addEventListener("click", (Event) =>
            {
                Event.stopPropagation();
                const NextMode = Btn.getAttribute("data-gizmo-mode");
                if (NextMode)
                    this.Mode = NextMode;
                else if (Btn.getAttribute("data-gizmo-space"))
                    this.Space = this.Space === "local" ? "world" : "local";
                else if (Btn.getAttribute("data-gizmo-sockets") && this.Host)
                    this.Host.ShowSockets = !(this.Host.ShowSockets !== false);
                this.CancelModal(false);
                this.RenderToolbar();
            });
        }
    }

    ShowReadout(Text)
    {
        if (!this.ReadoutEl)
            return;
        this.ReadoutEl.textContent = Text;
        this.ReadoutEl.classList.remove("hidden");
    }

    HideReadout()
    {
        if (!this.ReadoutEl)
            return;
        this.ReadoutEl.classList.add("hidden");
    }

    IsActiveEditMode()
    {
        return this.Host?.Settings?.RunMode === "EDIT";
    }

    get Modal()
    {
        return this.Drag && this.Drag.Modal ? this.Drag : null;
    }

    SetVisible(Visible)
    {
        if (this.ToolbarEl)
            this.ToolbarEl.style.display = Visible ? "inline-flex" : "none";
        if (!Visible)
        {
            this.CancelModal(false);
            this.HideReadout();
        }
    }

    SetRmbHeld(Held)
    {
        this.RmbHeld = Boolean(Held);
        if (Held)
            this.LastFlyAt = performance.now();
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                    CAMERA RAY & PROJECTION MATHEMATICS
    //--------------------------------------------------------------------------------------------------------------------

    ProjectWorldToScreen(WorldPt, Width, Height)
    {
        const Cam = this.Host.CameraState;
        if (!Cam || !Width || !Height)
            return null;
        const Rel = SubtractVector(WorldPt, Cam.Position);
        const ZView = DotVector(Rel, Cam.Forward);
        if (ZView <= 0.04)
            return null;
        const XView = DotVector(Rel, Cam.Right);
        const YView = DotVector(Rel, Cam.Up);
        const InvTanY = 1.0 / Math.max(1e-5, Cam.TanY);
        const InvTanX = InvTanY / Math.max(1e-5, Cam.Aspect);
        const NdcX = (XView / ZView) * InvTanX;
        const NdcY = (YView / ZView) * InvTanY;
        return {
            X: (NdcX * 0.5 + 0.5) * Width,
            Y: (0.5 - NdcY * 0.5) * Height,
            Depth: ZView
        };
    }

    RayFromClient(ClientX, ClientY)
    {
        const Canvas = this.Host.Canvas;
        const Cam = this.Host.CameraState;
        if (!Canvas || !Cam || !Canvas.getBoundingClientRect)
            return null;
        const Rect = Canvas.getBoundingClientRect();
        const NdcX = ((ClientX - Rect.left) / Math.max(1, Rect.width)) * 2.0 - 1.0;
        const NdcY = 1.0 - ((ClientY - Rect.top) / Math.max(1, Rect.height)) * 2.0;
        const Dir = NormalizeVector(AddVector(
            Cam.Forward,
            AddVector(ScaleVector(Cam.Right, NdcX * Cam.TanY * Cam.Aspect), ScaleVector(Cam.Up, NdcY * Cam.TanY))
        ));
        return {
            Origin: Cam.Position.slice(),
            Direction: Dir,
            LocalX: ClientX - Rect.left,
            LocalY: ClientY - Rect.top,
            Width: Rect.width,
            Height: Rect.height
        };
    }

    AxisParamUnderClient(Pivot, AxisDirWorld, ClientX, ClientY)
    {
        const Ray = this.RayFromClient(ClientX, ClientY);
        if (!Ray)
            return 0;
        const W0 = SubtractVector(Pivot, Ray.Origin);
        const A = DotVector(AxisDirWorld, AxisDirWorld);
        const B = DotVector(AxisDirWorld, Ray.Direction);
        const C = DotVector(Ray.Direction, Ray.Direction);
        const D = DotVector(AxisDirWorld, W0);
        const E = DotVector(Ray.Direction, W0);
        const Denom = A * C - B * B;
        if (Math.abs(Denom) < 1e-6)
            return 0;
        return (B * E - C * D) / Denom;
    }

    PlanePointUnderClient(Pivot, PlaneNormalWorld, ClientX, ClientY)
    {
        const Ray = this.RayFromClient(ClientX, ClientY);
        if (!Ray)
            return null;
        const Denom = DotVector(PlaneNormalWorld, Ray.Direction);
        if (Math.abs(Denom) < 1e-6)
            return null;
        const T = DotVector(SubtractVector(Pivot, Ray.Origin), PlaneNormalWorld) / Denom;
        if (T <= 0)
            return null;
        return AddVector(Ray.Origin, ScaleVector(Ray.Direction, T));
    }

    AngleUnderClient(Pivot, AxisDirWorld, RefWorld, ClientX, ClientY)
    {
        const Hit = this.PlanePointUnderClient(Pivot, AxisDirWorld, ClientX, ClientY);
        if (!Hit)
            return null;
        const V = SubtractVector(Hit, Pivot);
        const ProjRef = SubtractVector(RefWorld, ScaleVector(AxisDirWorld, DotVector(RefWorld, AxisDirWorld)));
        const Ref = NormalizeVector(ProjRef);
        const Perp = NormalizeVector(CrossVector(AxisDirWorld, Ref));
        return Math.atan2(DotVector(V, Perp), DotVector(V, Ref));
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                           HANDLE DRAG & BLENDER G / R / S MODAL TRANSFORM ENGINE
    //--------------------------------------------------------------------------------------------------------------------

    ResolveSelectedTarget()
    {
        const EntityId = this.Host?.Shell?.Selected;
        if (!EntityId)
            return null;
        const Pose = this.Host.QueryEntityTransformPose?.(EntityId);
        if (!Pose)
            return null;
        return { EntityId, ...Pose };
    }

    WorldBasisForPose(Pose)
    {
        const Q = this.Space === "local" ? Pose.Quaternion : [0, 0, 0, 1];
        return {
            Quat: Q,
            Dir: (LocalVec) => NormalizeVector(RotateVector(Q, LocalVec))
        };
    }

    HitTestHandles(LocalX, LocalY)
    {
        // Priority order matching TransformGizmo.h: centre -> plane -> scale/translate -> rotate arc
        const Priority = { center: 0, plane: 1, scale: 2, translate: 3, rotate: 4 };
        const Candidates = [];
        for (const H of this.ScreenHandles)
        {
            if (H.Kind === "quad")
            {
                if (PointInQuad2D(LocalX, LocalY, H.Quad))
                    Candidates.push(H);
            }
            else if (H.Kind === "circle")
            {
                if (Math.hypot(LocalX - H.Cx, LocalY - H.Cy) <= H.Radius + 5)
                    Candidates.push(H);
            }
            else if (H.Kind === "segment")
            {
                if (PointToSegmentDist2D(LocalX, LocalY, H.X0, H.Y0, H.X1, H.Y1) <= H.HitRadius)
                    Candidates.push(H);
            }
            else if (H.Kind === "polyline")
            {
                for (let I = 0; I + 1 < H.Points.length; ++I)
                {
                    if (PointToSegmentDist2D(LocalX, LocalY, H.Points[I][0], H.Points[I][1], H.Points[I + 1][0], H.Points[I + 1][1]) <= H.HitRadius)
                    {
                        Candidates.push(H);
                        break;
                    }
                }
            }
        }
        if (!Candidates.length)
            return null;
        Candidates.sort((A, B) => (Priority[A.Info.Type] ?? 9) - (Priority[B.Info.Type] ?? 9));
        return Candidates[0].Info;
    }

    BeginHandleDrag(Info, ClientX, ClientY)
    {
        const Target = this.ResolveSelectedTarget();
        if (!Target)
            return false;
        const Basis = this.WorldBasisForPose(Target);
        const CamForward = this.Host.CameraState?.Forward || [1, 0, 0];

        const State = {
            Modal:      false,
            EntityId:   Target.EntityId,
            Info,
            Pivot:      Target.Position.slice(),
            StartPos:   Target.Position.slice(),
            StartQuat:  Target.Quaternion.slice(),
            StartScale: Target.Scale.slice(),
            StartClient: [ClientX, ClientY]
        };

        if (Info.Type === "translate" || Info.Type === "scale")
        {
            State.AxisWorld = Basis.Dir(Info.Dir);
            State.StartParam = this.AxisParamUnderClient(State.Pivot, State.AxisWorld, ClientX, ClientY);
            // WorldPerPixel scale factor for normalized 1.0 gizmo span
            State.GizmoSpan = Math.max(0.25, this.LastWorldSpan || 1.0);
        }
        else if (Info.Type === "plane" || Info.Type === "center")
        {
            State.NormalWorld = Info.Type === "center" ? CamForward.slice() : Basis.Dir(Info.Normal);
            State.UWorld = Info.U ? Basis.Dir(Info.U) : (this.Host.CameraState?.Right || [0, 1, 0]);
            State.VWorld = Info.V ? Basis.Dir(Info.V) : (this.Host.CameraState?.Up || [0, 0, 1]);
            State.StartHit = this.PlanePointUnderClient(State.Pivot, State.NormalWorld, ClientX, ClientY);
        }
        else if (Info.Type === "rotate")
        {
            State.AxisWorld = Basis.Dir(Info.Dir);
            State.RefWorld = Basis.Dir(Info.Ref);
            State.StartAngle = this.AngleUnderClient(State.Pivot, State.AxisWorld, State.RefWorld, ClientX, ClientY);
        }

        this.Drag = State;
        return true;
    }

    // Blender-style G / R / S modal operator (only in EDIT mode, never while RMB+WASD flying).
    BeginBlenderModal(Kind)
    {
        if (!this.IsActiveEditMode())
            return false;
        const Target = this.ResolveSelectedTarget();
        if (!Target)
            return false;

        // If already in modal, pressing G/R/S switches the active modal kind cleanly from original pose
        const BasePos   = this.Drag?.Modal ? this.Drag.StartPos.slice()   : Target.Position.slice();
        const BaseQuat  = this.Drag?.Modal ? this.Drag.StartQuat.slice()  : Target.Quaternion.slice();
        const BaseScale = this.Drag?.Modal ? this.Drag.StartScale.slice() : Target.Scale.slice();
        if (this.Drag?.Modal)
            this.Host.ApplyEntityTransformPose(Target.EntityId, { Position: BasePos, Quaternion: BaseQuat, Scale: BaseScale });

        this.Mode = Kind;
        this.RenderToolbar();

        const [Cx, Cy] = this.PointerClient;
        const CamForward = this.Host.CameraState?.Forward || [1, 0, 0];
        const PivotProj = this.OverlayCanvas
            ? this.ProjectWorldToScreen(BasePos, this.OverlayCanvas.clientWidth || 800, this.OverlayCanvas.clientHeight || 600)
            : { X: 400, Y: 300 };
        const Rect = this.Host.Canvas?.getBoundingClientRect?.() || { left: 0, top: 0 };
        const PivotClient = [Rect.left + (PivotProj?.X ?? 400), Rect.top + (PivotProj?.Y ?? 300)];

        this.Drag = {
            Modal:       true,
            ModalKind:   Kind,          // "translate" | "rotate" | "scale"
            Constraint:  "free",        // "free" | "x" | "y" | "z" | "yz" | "xz" | "xy"
            EntityId:    Target.EntityId,
            Pivot:       BasePos.slice(),
            StartPos:    BasePos,
            StartQuat:   BaseQuat,
            StartScale:  BaseScale,
            StartClient: [Cx, Cy],
            PivotClient,
            StartHit:    this.PlanePointUnderClient(BasePos, CamForward, Cx, Cy),
            StartDistPx: Math.max(24, Math.hypot(Cx - PivotClient[0], Cy - PivotClient[1])),
            StartAngle2D: Math.atan2(Cy - PivotClient[1], Cx - PivotClient[0])
        };

        const Label = Kind === "translate" ? "Grab [G]" : Kind === "rotate" ? "Rotate [R]" : "Scale [S]";
        this.ShowReadout(`${Label} · Free (press X/Y/Z axis · Shift+X/Y/Z plane · Ctrl snap · LMB confirm · Esc cancel)`);
        return true;
    }

    SetModalConstraint(Axis, PlaneLock = false)
    {
        if (!this.Drag || !this.Drag.Modal)
            return false;
        const Desired = PlaneLock
            ? (Axis === "x" ? "yz" : Axis === "y" ? "xz" : "xy")
            : Axis;
        this.Drag.Constraint = this.Drag.Constraint === Desired ? "free" : Desired;
        // Reset before re-evaluating under current cursor
        this.Host.ApplyEntityTransformPose(this.Drag.EntityId, {
            Position: this.Drag.StartPos.slice(),
            Quaternion: this.Drag.StartQuat.slice(),
            Scale: this.Drag.StartScale.slice()
        });
        const [Cx, Cy] = this.PointerClient;
        this.RebindModalStartAtCursor(Cx, Cy);
        this.UpdateDrag(Cx, Cy, false);
        return true;
    }

    RebindModalStartAtCursor(Cx, Cy)
    {
        if (!this.Drag || !this.Drag.Modal)
            return;
        const Target = { Position: this.Drag.StartPos, Quaternion: this.Drag.StartQuat, Scale: this.Drag.StartScale };
        const Basis = this.WorldBasisForPose(Target);
        const C = this.Drag.Constraint;
        if (C === "x" || C === "y" || C === "z")
        {
            const Local = C === "x" ? [1, 0, 0] : C === "y" ? [0, 1, 0] : [0, 0, 1];
            this.Drag.AxisWorld = Basis.Dir(Local);
            this.Drag.StartParam = this.AxisParamUnderClient(this.Drag.Pivot, this.Drag.AxisWorld, Cx, Cy);
        }
        else if (C === "yz" || C === "xz" || C === "xy")
        {
            const NormalLocal = C === "yz" ? [1, 0, 0] : C === "xz" ? [0, 1, 0] : [0, 0, 1];
            this.Drag.NormalWorld = Basis.Dir(NormalLocal);
            this.Drag.StartHit = this.PlanePointUnderClient(this.Drag.Pivot, this.Drag.NormalWorld, Cx, Cy);
        }
    }

    CancelModal(Restore = true)
    {
        if (!this.Drag)
            return;
        if (Restore)
        {
            this.Host.ApplyEntityTransformPose(this.Drag.EntityId, {
                Position: this.Drag.StartPos.slice(),
                Quaternion: this.Drag.StartQuat.slice(),
                Scale: this.Drag.StartScale.slice()
            });
            this.Host.Shell?.Controls?.Synchronise?.();
        }
        this.Drag = null;
        this.HideReadout();
    }

    ConfirmModal()
    {
        if (!this.Drag)
            return;
        this.Drag = null;
        this.HideReadout();
        this.Host.Shell?.Controls?.Synchronise?.();
    }

    UpdateDrag(ClientX, ClientY, Snapping)
    {
        const D = this.Drag;
        if (!D)
            return;

        if (D.Modal)
        {
            this.UpdateModalDrag(ClientX, ClientY, Snapping);
            return;
        }

        const Info = D.Info;
        if (Info.Type === "translate")
        {
            let Delta = this.AxisParamUnderClient(D.Pivot, D.AxisWorld, ClientX, ClientY) - D.StartParam;
            if (Snapping)
                Delta = SnapScalar(Delta, SNAP_MOVE);
            const NextPos = AddVector(D.StartPos, ScaleVector(D.AxisWorld, Delta));
            this.Host.ApplyEntityTransformPose(D.EntityId, { Position: NextPos, Quaternion: D.StartQuat, Scale: D.StartScale });
            this.ShowReadout(`${Info.Axis.toUpperCase()}  move  ${Delta >= 0 ? "+" : ""}${Delta.toFixed(3)} m`);
        }
        else if (Info.Type === "scale")
        {
            const RawDelta = (this.AxisParamUnderClient(D.Pivot, D.AxisWorld, ClientX, ClientY) - D.StartParam) / Math.max(0.25, D.GizmoSpan);
            let Factor = 1.0 + RawDelta;
            if (Snapping)
                Factor = Math.max(SNAP_SCALE, SnapScalar(Factor, SNAP_SCALE));
            Factor = Math.max(0.05, Math.min(20.0, Factor));
            const NextScale = D.StartScale.slice();
            const AxisIdx = Info.Axis === "x" ? 0 : Info.Axis === "y" ? 1 : 2;
            NextScale[AxisIdx] = D.StartScale[AxisIdx] * Factor;
            this.Host.ApplyEntityTransformPose(D.EntityId, { Position: D.StartPos, Quaternion: D.StartQuat, Scale: NextScale });
            this.ShowReadout(`${Info.Axis.toUpperCase()}  scale  ${Factor.toFixed(3)}×`);
        }
        else if (Info.Type === "plane" || Info.Type === "center")
        {
            if (Info.Type === "center" && this.Mode === "scale")
            {
                const Dx = (ClientX - D.StartClient[0]) * 0.008;
                let Factor = Math.max(0.05, 1.0 + Dx);
                if (Snapping)
                    Factor = Math.max(SNAP_SCALE, SnapScalar(Factor, SNAP_SCALE));
                const NextScale = D.StartScale.map((S) => S * Factor);
                this.Host.ApplyEntityTransformPose(D.EntityId, { Position: D.StartPos, Quaternion: D.StartQuat, Scale: NextScale });
                this.ShowReadout(`XYZ  uniform scale  ${Factor.toFixed(3)}×`);
                return;
            }
            const Hit = this.PlanePointUnderClient(D.Pivot, D.NormalWorld, ClientX, ClientY);
            if (Hit && D.StartHit)
            {
                let Move = SubtractVector(Hit, D.StartHit);
                if (Snapping)
                {
                    const Du = SnapScalar(DotVector(Move, D.UWorld), SNAP_MOVE);
                    const Dv = SnapScalar(DotVector(Move, D.VWorld), SNAP_MOVE);
                    Move = AddVector(ScaleVector(D.UWorld, Du), ScaleVector(D.VWorld, Dv));
                }
                const NextPos = AddVector(D.StartPos, Move);
                this.Host.ApplyEntityTransformPose(D.EntityId, { Position: NextPos, Quaternion: D.StartQuat, Scale: D.StartScale });
                const Tag = Info.Type === "center" ? "Screen" : `${Info.Axis.toUpperCase()}-plane`;
                this.ShowReadout(`${Tag}  move  ${LengthVector(Move).toFixed(3)} m`);
            }
        }
        else if (Info.Type === "rotate")
        {
            const Now = this.AngleUnderClient(D.Pivot, D.AxisWorld, D.RefWorld, ClientX, ClientY);
            if (Now !== null && D.StartAngle !== null)
            {
                let Da = Now - D.StartAngle;
                if (Da > Math.PI) Da -= 2.0 * Math.PI;
                if (Da < -Math.PI) Da += 2.0 * Math.PI;
                if (Snapping)
                    Da = SnapScalar(Da, SNAP_ANGLE);
                const RotQ = ConstructAxisQuaternion(D.AxisWorld, Da);
                const NextQuat = NormalizeQuaternion(MultiplyQuaternion(RotQ, D.StartQuat));
                this.Host.ApplyEntityTransformPose(D.EntityId, { Position: D.StartPos, Quaternion: NextQuat, Scale: D.StartScale });
                this.ShowReadout(`${Info.Axis.toUpperCase()}  rotate  ${((Da * 180.0) / Math.PI).toFixed(1)}°`);
            }
        }
        this.Host.Shell?.Controls?.RefreshLive?.();
    }

    UpdateModalDrag(ClientX, ClientY, Snapping)
    {
        const D = this.Drag;
        const C = D.Constraint;
        const CamForward = this.Host.CameraState?.Forward || [1, 0, 0];

        if (D.ModalKind === "translate")
        {
            if (C === "x" || C === "y" || C === "z")
            {
                let Delta = this.AxisParamUnderClient(D.Pivot, D.AxisWorld, ClientX, ClientY) - (D.StartParam || 0);
                if (Snapping) Delta = SnapScalar(Delta, SNAP_MOVE);
                const NextPos = AddVector(D.StartPos, ScaleVector(D.AxisWorld, Delta));
                this.Host.ApplyEntityTransformPose(D.EntityId, { Position: NextPos, Quaternion: D.StartQuat, Scale: D.StartScale });
                this.ShowReadout(`G · ${C.toUpperCase()}  move  ${Delta >= 0 ? "+" : ""}${Delta.toFixed(3)} m`);
            }
            else
            {
                const Normal = (C === "yz" || C === "xz" || C === "xy") ? D.NormalWorld : CamForward;
                const Hit = this.PlanePointUnderClient(D.Pivot, Normal, ClientX, ClientY);
                if (Hit && D.StartHit)
                {
                    let Move = SubtractVector(Hit, D.StartHit);
                    if (Snapping)
                        Move = Move.map((V) => SnapScalar(V, SNAP_MOVE));
                    const NextPos = AddVector(D.StartPos, Move);
                    this.Host.ApplyEntityTransformPose(D.EntityId, { Position: NextPos, Quaternion: D.StartQuat, Scale: D.StartScale });
                    this.ShowReadout(`G · ${C.toUpperCase()}  move  (${Move.map((V) => V.toFixed(2)).join(", ")}) m`);
                }
            }
        }
        else if (D.ModalKind === "rotate")
        {
            const AngleNow = Math.atan2(ClientY - D.PivotClient[1], ClientX - D.PivotClient[0]);
            let Da = -(AngleNow - D.StartAngle2D);
            if (Da > Math.PI) Da -= 2.0 * Math.PI;
            if (Da < -Math.PI) Da += 2.0 * Math.PI;
            if (Snapping) Da = SnapScalar(Da, SNAP_ANGLE);
            const AxisWorld = (C === "x" || C === "y" || C === "z") ? D.AxisWorld : CamForward;
            const RotQ = ConstructAxisQuaternion(AxisWorld, Da);
            const NextQuat = NormalizeQuaternion(MultiplyQuaternion(RotQ, D.StartQuat));
            this.Host.ApplyEntityTransformPose(D.EntityId, { Position: D.StartPos, Quaternion: NextQuat, Scale: D.StartScale });
            this.ShowReadout(`R · ${C.toUpperCase()}  rotate  ${((Da * 180.0) / Math.PI).toFixed(1)}°`);
        }
        else if (D.ModalKind === "scale")
        {
            const DistNow = Math.hypot(ClientX - D.PivotClient[0], ClientY - D.PivotClient[1]);
            let Factor = Math.max(0.05, Math.min(20.0, DistNow / Math.max(12, D.StartDistPx)));
            if (Snapping) Factor = Math.max(SNAP_SCALE, SnapScalar(Factor, SNAP_SCALE));
            const NextScale = D.StartScale.slice();
            if (C === "x") NextScale[0] *= Factor;
            else if (C === "y") NextScale[1] *= Factor;
            else if (C === "z") NextScale[2] *= Factor;
            else if (C === "yz") { NextScale[1] *= Factor; NextScale[2] *= Factor; }
            else if (C === "xz") { NextScale[0] *= Factor; NextScale[2] *= Factor; }
            else if (C === "xy") { NextScale[0] *= Factor; NextScale[1] *= Factor; }
            else { NextScale[0] *= Factor; NextScale[1] *= Factor; NextScale[2] *= Factor; }
            this.Host.ApplyEntityTransformPose(D.EntityId, { Position: D.StartPos, Quaternion: D.StartQuat, Scale: NextScale });
            this.ShowReadout(`S · ${C.toUpperCase()}  scale  ${Factor.toFixed(3)}×`);
        }
        this.Host.Shell?.Controls?.RefreshLive?.();
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                    INPUT HOOKS (POINTER + KEYBOARD)
    //--------------------------------------------------------------------------------------------------------------------

    OnKeyDown(Event)
    {
        if (!this.IsActiveEditMode())
            return false;
        const Code = Event.code;

        // Track WASD camera flight activity so 'S' never collides with backward flight
        if (this.RmbHeld || ["KeyW", "KeyA", "KeyD", "KeyQ", "KeyE"].includes(Code))
        {
            if (["KeyW", "KeyA", "KeyS", "KeyD", "KeyQ", "KeyE"].includes(Code))
                this.LastFlyAt = performance.now();
            if (!this.Drag?.Modal)
                return false;
        }

        // Inside an active Blender modal (G / R / S)
        if (this.Drag && this.Drag.Modal)
        {
            if (Code === "Escape") { Event.preventDefault(); this.CancelModal(true); return true; }
            if (Code === "Enter" || Code === "NumpadEnter") { Event.preventDefault(); this.ConfirmModal(); return true; }
            if (Code === "KeyX") { Event.preventDefault(); return this.SetModalConstraint("x", Event.shiftKey); }
            if (Code === "KeyY") { Event.preventDefault(); return this.SetModalConstraint("y", Event.shiftKey); }
            if (Code === "KeyZ") { Event.preventDefault(); return this.SetModalConstraint("z", Event.shiftKey); }
            if (Code === "KeyG" && !Event.repeat) { Event.preventDefault(); return this.BeginBlenderModal("translate"); }
            if (Code === "KeyR" && !Event.repeat) { Event.preventDefault(); return this.BeginBlenderModal("rotate"); }
            if (Code === "KeyS" && !Event.repeat) { Event.preventDefault(); return this.BeginBlenderModal("scale"); }
            return false;
        }

        // Start G / R / S when NOT holding RMB, NOT mid-WASD flight, and a transformable object is selected
        if (Event.ctrlKey || Event.metaKey || Event.altKey || Event.repeat)
            return false;
        const Target = this.ResolveSelectedTarget();
        if (!Target)
            return false;

        const FlyingRecently = (performance.now() - this.LastFlyAt) < 450;
        const LatchedWasdFlight = Boolean(this.Host?.FreeCamera || this.Host?.RmbFlying || this.RmbHeld);
        if (Code === "KeyG" && !this.RmbHeld)
        {
            Event.preventDefault();
            return this.BeginBlenderModal("translate");
        }
        if (Code === "KeyR" && !this.RmbHeld)
        {
            Event.preventDefault();
            return this.BeginBlenderModal("rotate");
        }
        if (Code === "KeyS" && !LatchedWasdFlight && !FlyingRecently)
        {
            Event.preventDefault();
            return this.BeginBlenderModal("scale");
        }
        return false;
    }

    OnPointerDown(Event)
    {
        const Canvas = this.Host.Canvas;
        if (!Canvas)
            return false;
        const Rect = Canvas.getBoundingClientRect();
        const LocalX = Event.clientX - Rect.left;
        const LocalY = Event.clientY - Rect.top;
        this.PointerClient = [Event.clientX, Event.clientY];

        if (Event.button === 2)
        {
            this.RmbHeld = true;
            if (this.Drag?.Modal)
            {
                Event.preventDefault();
                this.CancelModal(true);
                return true;
            }
            return false;
        }

        if (!this.IsActiveEditMode() || Event.button !== 0)
            return false;

        // Confirm active Blender modal on LMB
        if (this.Drag && this.Drag.Modal)
        {
            this.ConfirmModal();
            return true;
        }

        // 1. Hit-test GizmoPRO handles first
        const HitHandle = this.HitTestHandles(LocalX, LocalY);
        if (HitHandle && this.BeginHandleDrag(HitHandle, Event.clientX, Event.clientY))
        {
            return true;
        }

        // 2. Otherwise store click start so a clean click performs GPU G-buffer + ray selection!
        this.PendingPick = { X: Event.clientX, Y: Event.clientY, At: performance.now() };
        return false;
    }

    OnPointerMove(Event)
    {
        this.PointerClient = [Event.clientX, Event.clientY];
        if (!this.IsActiveEditMode())
            return false;

        if (this.Drag)
        {
            this.UpdateDrag(Event.clientX, Event.clientY, Event.ctrlKey);
            return true;
        }

        const Canvas = this.Host.Canvas;
        if (!Canvas || !Canvas.getBoundingClientRect)
            return false;
        const Rect = Canvas.getBoundingClientRect();
        const Hit = this.HitTestHandles(Event.clientX - Rect.left, Event.clientY - Rect.top);
        this.HoveredHandle = Hit;
        if (Canvas.style)
            Canvas.style.cursor = Hit ? "pointer" : "";
        return false;
    }

    OnPointerUp(Event)
    {
        if (Event.button === 2)
            this.RmbHeld = false;

        if (this.Drag && !this.Drag.Modal)
        {
            this.Drag = null;
            this.HideReadout();
            this.Host.Shell?.Controls?.Synchronise?.();
            return true;
        }

        if (this.IsActiveEditMode() && this.PendingPick && Event.button === 0)
        {
            const Pick = this.PendingPick;
            this.PendingPick = null;
            if (Math.hypot(Event.clientX - Pick.X, Event.clientY - Pick.Y) < 6.0 && (performance.now() - Pick.At) < 450)
            {
                this.Host.PerformViewportSelection?.(Event.clientX, Event.clientY);
                return true;
            }
        }
        this.PendingPick = null;
        return false;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                     3D SHADED GIZMOPRO RENDERER (Gizmo.html cones, cylinders, planes, 31° arcs, ring)
    //--------------------------------------------------------------------------------------------------------------------

    Render()
    {
        const Canvas = this.OverlayCanvas;
        const MainCanvas = this.Host.Canvas;
        if (!Canvas || !MainCanvas || !Canvas.getContext)
            return;

        const EditMode = this.IsActiveEditMode();
        if (this.ToolbarEl)
            this.ToolbarEl.style.display = EditMode ? "inline-flex" : "none";

        const CssW = MainCanvas.clientWidth || 0;
        const CssH = MainCanvas.clientHeight || 0;
        if (CssW < 8 || CssH < 8)
            return;

        const Dpr = Math.min(window.devicePixelRatio || 1, 2);
        const TargetW = Math.round(CssW * Dpr);
        const TargetH = Math.round(CssH * Dpr);
        if (Canvas.width !== TargetW || Canvas.height !== TargetH)
        {
            Canvas.width = TargetW;
            Canvas.height = TargetH;
        }

        const Ctx = Canvas.getContext("2d");
        if (!Ctx || typeof Ctx.save !== "function" || typeof Ctx.clearRect !== "function")
            return;
        Ctx.save();
        Ctx.scale(Dpr, Dpr);
        Ctx.clearRect(0, 0, CssW, CssH);

        this.ScreenHandles = [];
        // Editor-only debug visuals: when pressing Play (or Sim), all camera/socket/light/gizmo overlays hide completely
        if (!EditMode)
        {
            Ctx.restore();
            return;
        }

        // 1. Draw Socket Location visual debug markers (local XYZ triads + diamond reticles + socket name badges)
        if (this.Host?.ShowSockets !== false)
            this.DrawSocketVisualDebug(Ctx, CssW, CssH);

        // 2. Draw subtle optical camera frustums for Vehicle Camera & Lobby Cine Camera in Edit mode
        this.DrawCameraFrustums(Ctx, CssW, CssH);

        // 3. Draw Studio Light markers in Edit mode
        this.DrawStudioLightMarkers(Ctx, CssW, CssH);

        // 4. Draw GizmoPRO + Selection Brackets on selected transformable entity in Edit mode
        const Target = this.ResolveSelectedTarget();
        if (Target)
        {
            this.DrawSelectionBrackets(Ctx, Target, CssW, CssH);
            this.DrawGizmoPro(Ctx, Target, CssW, CssH);
        }

        Ctx.restore();
    }

    DrawSocketVisualDebug(Ctx, CssW, CssH)
    {
        const Sockets = this.Host.QuerySocketLocations?.() || [];
        if (!Sockets.length)
            return;

        const VehRelated = this.Host.Shell?.Selected === "vehicle" || this.Host.Shell?.Selected === "vehicle-camera";
        for (const S of Sockets)
        {
            const P = this.ProjectWorldToScreen(S.Position, CssW, CssH);
            if (!P || P.Depth > 38.0)
                continue;

            // Dashed parent-origin to socket tether line
            if (S.ParentOrigin)
            {
                const PParent = this.ProjectWorldToScreen(S.ParentOrigin, CssW, CssH);
                if (PParent)
                {
                    Ctx.save();
                    Ctx.setLineDash([2, 4]);
                    Ctx.beginPath();
                    Ctx.moveTo(PParent.X, PParent.Y);
                    Ctx.lineTo(P.X, P.Y);
                    Ctx.strokeStyle = S.Active ? "rgba(255, 209, 102, 0.42)" : "rgba(56, 217, 255, 0.22)";
                    Ctx.lineWidth = 1.0;
                    Ctx.stroke();
                    Ctx.restore();
                }
            }

            // Local 3D orientation micro-triad (+X forward red, +Y left green, +Z up blue)
            const AxisLen = 0.26;
            const Triad = [
                [S.Forward, "#ff4d5e"],
                [S.Left,    "#3fe07f"],
                [S.Up,      "#4aa3ff"]
            ];
            for (const [AxisDir, Hex] of Triad)
            {
                if (!AxisDir) continue;
                const Tip = this.ProjectWorldToScreen(AddVector(S.Position, ScaleVector(AxisDir, AxisLen)), CssW, CssH);
                if (!Tip) continue;
                Ctx.beginPath();
                Ctx.moveTo(P.X, P.Y);
                Ctx.lineTo(Tip.X, Tip.Y);
                Ctx.strokeStyle = Hex;
                Ctx.lineWidth = S.Active ? 1.8 : 1.35;
                Ctx.stroke();
            }

            // Diamond socket locator reticle + centre crosshair dot
            const R = S.Active ? 6.5 : 5.0;
            Ctx.beginPath();
            Ctx.moveTo(P.X, P.Y - R);
            Ctx.lineTo(P.X + R, P.Y);
            Ctx.lineTo(P.X, P.Y + R);
            Ctx.lineTo(P.X - R, P.Y);
            Ctx.closePath();
            Ctx.fillStyle = S.Active ? "rgba(255, 209, 102, 0.28)" : "rgba(9, 9, 9, 0.72)";
            Ctx.fill();
            Ctx.lineWidth = S.Active ? 1.7 : 1.3;
            Ctx.strokeStyle = S.Active ? "#ffd166" : (S.Group === "Hub" ? "#7ee0a3" : "#38d9ff");
            Ctx.stroke();

            Ctx.beginPath();
            Ctx.arc(P.X, P.Y, 1.5, 0, Math.PI * 2);
            Ctx.fillStyle = S.Active ? "#ffd166" : "#d9f7ff";
            Ctx.fill();

            // Monospace pill label when close enough or vehicle/camera is selected
            if (P.Depth < 18.0 || VehRelated || S.Active)
            {
                const Label = S.Name;
                Ctx.font = "600 10px ui-monospace, SFMono-Regular, Menlo, Consolas, monospace";
                const TextW = Ctx.measureText ? Ctx.measureText(Label).width : Label.length * 6;
                const BoxX = Math.round(P.X + 8);
                const BoxY = Math.round(P.Y - 8);
                const BoxW = Math.round(TextW + 10);
                const BoxH = 16;
                Ctx.fillStyle = "rgba(9, 9, 9, 0.86)";
                Ctx.fillRect(BoxX, BoxY, BoxW, BoxH);
                Ctx.strokeStyle = S.Active ? "rgba(255, 209, 102, 0.75)" : "#262626";
                Ctx.lineWidth = 1;
                Ctx.strokeRect(BoxX + 0.5, BoxY + 0.5, BoxW - 1, BoxH - 1);
                Ctx.fillStyle = S.Active ? "#ffd166" : "#d4d4d8";
                Ctx.textBaseline = "middle";
                Ctx.fillText(Label, BoxX + 5, BoxY + BoxH * 0.5 + 0.5);
            }
        }
    }

    DrawCameraFrustums(Ctx, CssW, CssH)
    {
        const Cameras = this.Host.QueryVisualizableCameras?.() || [];
        for (const CamInfo of Cameras)
        {
            if (CamInfo.IsActiveEye)
                continue;
            const Origin = CamInfo.Position;
            const Fwd = CamInfo.Forward;
            const Right = CamInfo.Right;
            const Up = CamInfo.Up;
            const Dist = Math.min(4.2, Math.max(1.2, CamInfo.FocusDistance * 0.35));
            const HalfH = Math.tan((CamInfo.VerticalFovDeg * 0.5 * Math.PI) / 180.0) * Dist;
            const HalfW = HalfH * 1.5;
            const Center = AddVector(Origin, ScaleVector(Fwd, Dist));
            const Corners = [
                AddVector(Center, AddVector(ScaleVector(Right, -HalfW), ScaleVector(Up,  HalfH))),
                AddVector(Center, AddVector(ScaleVector(Right,  HalfW), ScaleVector(Up,  HalfH))),
                AddVector(Center, AddVector(ScaleVector(Right,  HalfW), ScaleVector(Up, -HalfH))),
                AddVector(Center, AddVector(ScaleVector(Right, -HalfW), ScaleVector(Up, -HalfH)))
            ];
            const P0 = this.ProjectWorldToScreen(Origin, CssW, CssH);
            const Pc = Corners.map((C) => this.ProjectWorldToScreen(C, CssW, CssH));
            if (!P0 || Pc.some((P) => !P))
                continue;

            const IsSelected = this.Host.Shell?.Selected === CamInfo.Id;
            Ctx.strokeStyle = IsSelected ? "rgba(214, 184, 255, 0.85)" : "rgba(187, 154, 228, 0.38)";
            Ctx.lineWidth = IsSelected ? 1.5 : 1.0;
            Ctx.setLineDash([4, 4]);
            for (const P of Pc)
            {
                Ctx.beginPath();
                Ctx.moveTo(P0.X, P0.Y);
                Ctx.lineTo(P.X, P.Y);
                Ctx.stroke();
            }
            Ctx.setLineDash([]);
            Ctx.beginPath();
            Ctx.moveTo(Pc[0].X, Pc[0].Y);
            for (let I = 1; I < 4; ++I) Ctx.lineTo(Pc[I].X, Pc[I].Y);
            Ctx.closePath();
            Ctx.fillStyle = IsSelected ? "rgba(187, 154, 228, 0.08)" : "rgba(187, 154, 228, 0.03)";
            Ctx.fill();
            Ctx.stroke();
        }
    }

    DrawStudioLightMarkers(Ctx, CssW, CssH)
    {
        const Lights = this.Host.Lighting?.StudioLights || [];
        for (let Index = 0; Index < Lights.length; ++Index)
        {
            const Light = Lights[Index];
            if (!Light || Light.Disabled)
                continue;
            const P = this.ProjectWorldToScreen(Light.Position, CssW, CssH);
            if (!P)
                continue;
            const Id = `studio-light-${Index}`;
            const Selected = this.Host.Shell?.Selected === Id;
            Ctx.beginPath();
            Ctx.arc(P.X, P.Y, Selected ? 8 : 6, 0, Math.PI * 2);
            Ctx.fillStyle = Selected ? "rgba(255, 226, 138, 0.92)" : "rgba(255, 214, 120, 0.55)";
            Ctx.fill();
            Ctx.lineWidth = 1.4;
            Ctx.strokeStyle = "#141414";
            Ctx.stroke();

            const TipWorld = AddVector(Light.Position, ScaleVector(NormalizeVector(Light.Direction || [0, 0, -1]), 1.35));
            const PTip = this.ProjectWorldToScreen(TipWorld, CssW, CssH);
            if (PTip)
            {
                Ctx.beginPath();
                Ctx.moveTo(P.X, P.Y);
                Ctx.lineTo(PTip.X, PTip.Y);
                Ctx.strokeStyle = Selected ? "rgba(255, 226, 138, 0.85)" : "rgba(255, 214, 120, 0.35)";
                Ctx.lineWidth = 1.2;
                Ctx.stroke();
            }
        }
    }

    DrawSelectionBrackets(Ctx, Target, CssW, CssH)
    {
        const Half = Target.HalfExtents || [0.6, 0.6, 0.6];
        const S = Target.Scale || [1, 1, 1];
        const Hx = Half[0] * S[0], Hy = Half[1] * S[1], Hz = Half[2] * S[2];
        const Signs = [[-1,-1,-1],[1,-1,-1],[1,1,-1],[-1,1,-1],[-1,-1,1],[1,-1,1],[1,1,1],[-1,1,1]];
        const WorldCorners = Signs.map(([Sx, Sy, Sz]) =>
            AddVector(Target.Position, RotateVector(Target.Quaternion, [Sx * Hx, Sy * Hy, Sz * Hz]))
        );
        Ctx.strokeStyle = "rgba(245, 213, 101, 0.65)";
        Ctx.lineWidth = 1.25;
        const Edges = [[0,1],[1,2],[2,3],[3,0],[4,5],[5,6],[6,7],[7,4],[0,4],[1,5],[2,6],[3,7]];
        for (const [A, B] of Edges)
        {
            const Pa = WorldCorners[A], Pb = WorldCorners[B];
            const A1 = AddVector(Pa, ScaleVector(SubtractVector(Pb, Pa), 0.22));
            const B1 = AddVector(Pb, ScaleVector(SubtractVector(Pa, Pb), 0.22));
            for (const [PStart, PEnd] of [[Pa, A1], [Pb, B1]])
            {
                const S0 = this.ProjectWorldToScreen(PStart, CssW, CssH);
                const S1 = this.ProjectWorldToScreen(PEnd, CssW, CssH);
                if (S0 && S1)
                {
                    Ctx.beginPath();
                    Ctx.moveTo(S0.X, S0.Y);
                    Ctx.lineTo(S1.X, S1.Y);
                    Ctx.stroke();
                }
            }
        }
    }

    IsHandleHighlighted(Type, Axis)
    {
        const Active = this.Drag?.Info || this.HoveredHandle;
        return !!(Active && Active.Type === Type && Active.Axis === Axis);
    }

    DrawGizmoPro(Ctx, Target, CssW, CssH)
    {
        const Cam = this.Host.CameraState;
        if (!Cam)
            return;
        const PivotScreen = this.ProjectWorldToScreen(Target.Position, CssW, CssH);
        if (!PivotScreen)
            return;

        // Exact TransformGizmo.h WorldPerPixel at the pivot -> constant 86 px screen footprint!
        const WorldPerPixel = (2.0 * PivotScreen.Depth * Cam.TanY) / Math.max(1, CssH);
        const Span = AXIS_END_PX * WorldPerPixel;
        this.LastWorldSpan = Span;

        const Basis = this.WorldBasisForPose(Target);
        const LightDir = NormalizeVector(AddVector(ScaleVector(Cam.Forward, -0.65), AddVector(ScaleVector(Cam.Up, 0.55), ScaleVector(Cam.Right, 0.45))));

        const Polys = []; // { Depth, Fill, Stroke, LineWidth, Alpha, Points2D }
        const PushPoly3D = (WorldPts, NormalWorld, BaseHex, Highlighted, Alpha = 1.0, Stroke = null) =>
        {
            const ScreenPts = [];
            let DepthSum = 0;
            for (const Wp of WorldPts)
            {
                const Sp = this.ProjectWorldToScreen(Wp, CssW, CssH);
                if (!Sp) return;
                ScreenPts.push([Sp.X, Sp.Y]);
                DepthSum += Sp.Depth;
            }
            const NdotL = Math.abs(DotVector(NormalWorld, LightDir));
            const Shade = 0.52 + 0.52 * NdotL;
            const Fill = ShadeColor(BaseHex, Shade, Highlighted ? 70 : 0);
            Polys.push({
                Depth: DepthSum / WorldPts.length,
                Fill,
                Stroke: Stroke || Fill,
                LineWidth: Stroke ? 1.3 : 0.45,
                Alpha,
                Points2D: ScreenPts
            });
        };

        const ShowTranslate = this.Mode === "all" || this.Mode === "translate";
        const ShowScale     = this.Mode === "all" || this.Mode === "scale";
        const ShowRotate    = this.Mode === "all" || this.Mode === "rotate";

        for (const Ax of AXES)
        {
            const DirW = Basis.Dir(Ax.Dir);
            const UW   = Basis.Dir(Ax.U);
            const VW   = Basis.Dir(Ax.V);
            const { U: RadU, V: RadV } = OrthonormalBasisFromAxis(DirW);

            // 1. Translate Cone at TIP (Gizmo.html ConeGeometry(0.06, 0.18, 24))
            if (ShowTranslate)
            {
                const Highlight = this.IsHandleHighlighted("translate", Ax.Name);
                const BaseCenter = AddVector(Target.Position, ScaleVector(DirW, (TIP - CONE_LEN * 0.5) * Span));
                const Apex       = AddVector(Target.Position, ScaleVector(DirW, (TIP + CONE_LEN * 0.5) * Span));
                const R = CONE_R * Span;
                const Segs = 18;
                for (let I = 0; I < Segs; ++I)
                {
                    const A0 = (2 * Math.PI * I) / Segs;
                    const A1 = (2 * Math.PI * (I + 1)) / Segs;
                    const R0 = AddVector(ScaleVector(RadU, Math.cos(A0)), ScaleVector(RadV, Math.sin(A0)));
                    const R1 = AddVector(ScaleVector(RadU, Math.cos(A1)), ScaleVector(RadV, Math.sin(A1)));
                    const P0 = AddVector(BaseCenter, ScaleVector(R0, R));
                    const P1 = AddVector(BaseCenter, ScaleVector(R1, R));
                    const MidN = NormalizeVector(AddVector(AddVector(R0, R1), ScaleVector(DirW, 0.5)));
                    PushPoly3D([P0, P1, Apex], MidN, Ax.Color, Highlight);
                    PushPoly3D([BaseCenter, P1, P0], ScaleVector(DirW, -1), Ax.Color, Highlight);
                }
                const SBase = this.ProjectWorldToScreen(BaseCenter, CssW, CssH);
                const SApex = this.ProjectWorldToScreen(Apex, CssW, CssH);
                if (SBase && SApex)
                {
                    this.ScreenHandles.push({
                        Kind: "segment",
                        X0: SBase.X, Y0: SBase.Y, X1: SApex.X, Y1: SApex.Y,
                        HitRadius: 11,
                        Info: { Type: "translate", Axis: Ax.Name, Dir: Ax.Dir }
                    });
                }
            }

            // 2. Scale Cylinder inboard of cone at TIP - 0.28 (Gizmo.html CylinderGeometry(0.06, 0.06, 0.14, 24))
            if (ShowScale)
            {
                const Highlight = this.IsHandleHighlighted("scale", Ax.Name);
                const C0 = AddVector(Target.Position, ScaleVector(DirW, (SCALE_DIST - SCALE_LEN * 0.5) * Span));
                const C1 = AddVector(Target.Position, ScaleVector(DirW, (SCALE_DIST + SCALE_LEN * 0.5) * Span));
                const R = SCALE_R * Span;
                const Segs = 16;
                for (let I = 0; I < Segs; ++I)
                {
                    const A0 = (2 * Math.PI * I) / Segs;
                    const A1 = (2 * Math.PI * (I + 1)) / Segs;
                    const R0 = AddVector(ScaleVector(RadU, Math.cos(A0)), ScaleVector(RadV, Math.sin(A0)));
                    const R1 = AddVector(ScaleVector(RadU, Math.cos(A1)), ScaleVector(RadV, Math.sin(A1)));
                    const B0 = AddVector(C0, ScaleVector(R0, R));
                    const B1 = AddVector(C0, ScaleVector(R1, R));
                    const T1 = AddVector(C1, ScaleVector(R1, R));
                    const T0 = AddVector(C1, ScaleVector(R0, R));
                    PushPoly3D([B0, B1, T1, T0], NormalizeVector(AddVector(R0, R1)), Ax.Color, Highlight);
                    PushPoly3D([C1, T0, T1], DirW, Ax.Color, Highlight);
                    PushPoly3D([C0, B1, B0], ScaleVector(DirW, -1), Ax.Color, Highlight);
                }
                const SC0 = this.ProjectWorldToScreen(C0, CssW, CssH);
                const SC1 = this.ProjectWorldToScreen(C1, CssW, CssH);
                if (SC0 && SC1)
                {
                    this.ScreenHandles.push({
                        Kind: "segment",
                        X0: SC0.X, Y0: SC0.Y, X1: SC1.X, Y1: SC1.Y,
                        HitRadius: 10,
                        Info: { Type: "scale", Axis: Ax.Name, Dir: Ax.Dir }
                    });
                }
            }

            // 3. 2D Plane-Translate Quad + Two Opaque Outer L-Edges (Gizmo.html lines 149-182)
            if (ShowTranslate)
            {
                const Highlight = this.IsHandleHighlighted("plane", Ax.Name);
                const Dist = (TIP - PLANE_HALF) * Span;
                const Half = PLANE_HALF * Span;
                const Corner = AddVector(Target.Position, ScaleVector(AddVector(UW, VW), Dist));
                const Q0 = AddVector(Corner, AddVector(ScaleVector(UW, -Half), ScaleVector(VW, -Half)));
                const Q1 = AddVector(Corner, AddVector(ScaleVector(UW,  Half), ScaleVector(VW, -Half)));
                const Q2 = AddVector(Corner, AddVector(ScaleVector(UW,  Half), ScaleVector(VW,  Half))); // outer corner
                const Q3 = AddVector(Corner, AddVector(ScaleVector(UW, -Half), ScaleVector(VW,  Half)));

                PushPoly3D([Q0, Q1, Q2, Q3], DirW, Ax.PlaneColor, Highlight, Highlight ? 0.55 : 0.28);

                const SQ = [Q0, Q1, Q2, Q3].map((P) => this.ProjectWorldToScreen(P, CssW, CssH));
                if (SQ.every(Boolean))
                {
                    Polys.push({
                        Depth: (SQ[1].Depth + SQ[2].Depth + SQ[3].Depth) / 3 - 0.002,
                        Fill: null,
                        Stroke: Highlight ? "#ffffff" : Ax.PlaneColor,
                        LineWidth: Highlight ? 2.2 : 1.6,
                        Alpha: 0.96,
                        Points2D: [[SQ[1].X, SQ[1].Y], [SQ[2].X, SQ[2].Y], [SQ[3].X, SQ[3].Y]],
                        OpenPolyline: true
                    });
                    this.ScreenHandles.push({
                        Kind: "quad",
                        Quad: SQ.map((P) => [P.X, P.Y]),
                        Info: { Type: "plane", Axis: Ax.Name, Normal: Ax.Dir, U: Ax.U, V: Ax.V }
                    });
                }
            }

            // 4. Flat Curved Rotation Arc-Bar (31° annular sector centered at 45° bisector, Gizmo.html lines 189-203)
            if (ShowRotate)
            {
                const Highlight = this.IsHandleHighlighted("rotate", Ax.Name);
                const InnerR = (ARC_RADIUS - ARC_BAND) * Span;
                const OuterR = (ARC_RADIUS + ARC_BAND) * Span;
                const StartA = Math.PI * 0.25 - ARC_SWEEP * 0.5;
                const Segs = 14;
                const CenterSpine = [];
                for (let I = 0; I < Segs; ++I)
                {
                    const A0 = StartA + (ARC_SWEEP * I) / Segs;
                    const A1 = StartA + (ARC_SWEEP * (I + 1)) / Segs;
                    const Dir0 = AddVector(ScaleVector(UW, Math.cos(A0)), ScaleVector(VW, Math.sin(A0)));
                    const Dir1 = AddVector(ScaleVector(UW, Math.cos(A1)), ScaleVector(VW, Math.sin(A1)));
                    const I0 = AddVector(Target.Position, ScaleVector(Dir0, InnerR));
                    const O0 = AddVector(Target.Position, ScaleVector(Dir0, OuterR));
                    const O1 = AddVector(Target.Position, ScaleVector(Dir1, OuterR));
                    const I1 = AddVector(Target.Position, ScaleVector(Dir1, InnerR));
                    PushPoly3D([I0, O0, O1, I1], DirW, Ax.Color, Highlight, 0.96);

                    const MidPt = this.ProjectWorldToScreen(AddVector(Target.Position, ScaleVector(Dir0, ARC_RADIUS * Span)), CssW, CssH);
                    if (MidPt) CenterSpine.push([MidPt.X, MidPt.Y]);
                    if (I === Segs - 1)
                    {
                        const EndPt = this.ProjectWorldToScreen(AddVector(Target.Position, ScaleVector(Dir1, ARC_RADIUS * Span)), CssW, CssH);
                        if (EndPt) CenterSpine.push([EndPt.X, EndPt.Y]);
                    }
                }
                if (CenterSpine.length >= 2)
                {
                    this.ScreenHandles.push({
                        Kind: "polyline",
                        Points: CenterSpine,
                        HitRadius: 9,
                        Info: { Type: "rotate", Axis: Ax.Name, Dir: Ax.Dir, Ref: Ax.U }
                    });
                }
            }
        }

        // Sort 3D polygons back-to-front (painter's order)
        Polys.sort((A, B) => B.Depth - A.Depth);
        for (const Poly of Polys)
        {
            Ctx.save();
            Ctx.globalAlpha = Poly.Alpha;
            Ctx.beginPath();
            Ctx.moveTo(Poly.Points2D[0][0], Poly.Points2D[0][1]);
            for (let I = 1; I < Poly.Points2D.length; ++I)
                Ctx.lineTo(Poly.Points2D[I][0], Poly.Points2D[I][1]);
            if (!Poly.OpenPolyline)
                Ctx.closePath();
            if (Poly.Fill)
            {
                Ctx.fillStyle = Poly.Fill;
                Ctx.fill();
            }
            if (Poly.Stroke)
            {
                Ctx.strokeStyle = Poly.Stroke;
                Ctx.lineWidth = Poly.LineWidth;
                Ctx.lineJoin = "round";
                Ctx.stroke();
            }
            Ctx.restore();
        }

        // 5. Central Camera-Facing White Torus Ring (Gizmo.html TorusGeometry(0.16, 0.008, 12, 48))
        const RingHighlight = this.IsHandleHighlighted("center", "xyz");
        const RingPx = Math.max(8, (RING_RADIUS / TIP) * AXIS_END_PX);
        Ctx.beginPath();
        Ctx.arc(PivotScreen.X, PivotScreen.Y, RingPx, 0, Math.PI * 2);
        Ctx.strokeStyle = RingHighlight ? "#ffe885" : GIZMO_HEX.ring;
        Ctx.lineWidth = RingHighlight ? 2.4 : 1.7;
        Ctx.stroke();

        this.ScreenHandles.push({
            Kind: "circle",
            Cx: PivotScreen.X,
            Cy: PivotScreen.Y,
            Radius: RingPx,
            Info: { Type: "center", Axis: "xyz" }
        });
    }
}
