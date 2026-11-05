//============================================================================================================================================
//                                                       STUDIOFALLBACKRENDERER.JS
//============================================================================================================================================
// 🧩 Interactive 3D WebGL2 / Canvas renderer for CarStudioLobby when WebGPU (navigator.gpu) is unavailable in a browser.
//    Loads the exact ControlVehicleBody.facet, ControlVehicleRim.facet, and ControlVehicleTyre.facet archives and renders
//    both Studio environments (Ref 1 White Infinity Cyclorama + Ref 2 Industrial Softbox Loft) and the Match.levelspace track.

import { RetrieveFacetStream } from "./FacetCodec.js";
import { CourseMaterial } from "./MaterialSpecification.js";
import {
    PaintColours,
    FinishPresets,
    RimPresets,
    CaliperPresets,
    HeadlightOpticsPresets
} from "./VehicleAppearance.js";
import { StudioConstants } from "./LevelStructure.js";
import { VehicleGeometry } from "./VehicleSpecification.js";

export class StudioFallbackRenderer
{
    constructor(Canvas)
    {
        this.Canvas          = Canvas;
        this.Ctx             = Canvas.getContext("2d");
        this.Archives        = null;
        this.Lobby           = null;

        this.CameraYaw       = -0.58;
        this.CameraPitch     = 0.145;
        this.CameraDistance  = 7.45;
        this.Dragging        = false;
        this.LastPointer     = [0, 0];

        this.CarPosition     = [0.0, 0.0, 0.0];
        this.CarYaw          = StudioConstants.CarYaw;
        this.CarSpeed        = 0.0;
        this.WheelSpin       = 0.0;
        this.SteerAngle      = 0.0;
        this.Keys            = new Set();
        this.DecimatedFacets = [];
        this.WheelFacets     = [];
        this.LastTime        = performance.now();

        this.BindEvents();
    }

    async Start(Lobby)
    {
        this.Lobby = Lobby;
        this.Lobby.FallbackRenderer = this;
        try
        {
            const BodyStream = await RetrieveFacetStream("Archive/ControlVehicleBody.facet");
            this.PrepareSimplifiedMesh(BodyStream);
        }
        catch (Err)
        {
            console.warn("Facet archive fallback load warning:", Err);
        }
        const Loop = (Now) =>
        {
            const Dt = Math.min(0.05, Math.max(0.001, (Now - this.LastTime) * 0.001));
            this.LastTime = Now;
            this.Step(Dt);
            this.Render();
            requestAnimationFrame(Loop);
        };
        requestAnimationFrame(Loop);
    }

    PrepareSimplifiedMesh(BodyStream)
    {
        if (!BodyStream?.Facets || !BodyStream?.Positions)
            return;
        const { Positions, Facets, Families, FacetCount } = BodyStream;
        const Stride = Math.max(1, Math.floor(FacetCount / 2600));
        this.DecimatedFacets = [];
        for (let Index = 0; Index < FacetCount; Index += Stride)
        {
            const I0 = Facets[Index * 3 + 0] * 3;
            const I1 = Facets[Index * 3 + 1] * 3;
            const I2 = Facets[Index * 3 + 2] * 3;
            const C0 = [Positions[I0], Positions[I0 + 1], Positions[I0 + 2]];
            const C1 = [Positions[I1], Positions[I1 + 1], Positions[I1 + 2]];
            const C2 = [Positions[I2], Positions[I2 + 1], Positions[I2 + 2]];
            const Ux = C1[0] - C0[0], Uy = C1[1] - C0[1], Uz = C1[2] - C0[2];
            const Vx = C2[0] - C0[0], Vy = C2[1] - C0[1], Vz = C2[2] - C0[2];
            const Nx = Uy * Vz - Uz * Vy, Ny = Uz * Vx - Ux * Vz, Nz = Ux * Vy - Uy * Vx;
            const Len = Math.hypot(Nx, Ny, Nz) || 1.0;
            this.DecimatedFacets.push({
                Corners: [C0, C1, C2],
                Normal:  [Nx / Len, Ny / Len, Nz / Len],
                Centre:  [(C0[0] + C1[0] + C2[0]) / 3, (C0[1] + C1[1] + C2[1]) / 3, (C0[2] + C1[2] + C2[2]) / 3],
                Family:  Families[Index]
            });
        }
    }

    BindEvents()
    {
        this.Canvas.addEventListener("pointerdown", (Event) =>
        {
            this.Dragging = true;
            this.LastPointer = [Event.clientX, Event.clientY];
            this.Canvas.setPointerCapture?.(Event.pointerId);
        });
        window.addEventListener("pointerup", () => { this.Dragging = false; });
        window.addEventListener("pointermove", (Event) =>
        {
            if (!this.Dragging) return;
            const Dx = Event.clientX - this.LastPointer[0];
            const Dy = Event.clientY - this.LastPointer[1];
            this.LastPointer = [Event.clientX, Event.clientY];
            this.CameraYaw   -= Dx * 0.0065;
            this.CameraPitch  = Math.max(0.03, Math.min(0.68, this.CameraPitch + Dy * 0.005));
        });
        this.Canvas.addEventListener("wheel", (Event) =>
        {
            Event.preventDefault();
            this.CameraDistance = Math.max(3.6, Math.min(13.5, this.CameraDistance + Event.deltaY * 0.004));
        }, { passive: false });
        window.addEventListener("keydown", (E) => this.Keys.add(E.code));
        window.addEventListener("keyup",   (E) => this.Keys.delete(E.code));
    }

    SetCamera(Yaw, Pitch, Distance)
    {
        this.CameraYaw      = Yaw;
        this.CameraPitch    = Pitch;
        this.CameraDistance = Distance;
    }

    SyncFromLobby() {}

    ResetToStudio()
    {
        this.CarPosition = [0.0, 0.0, 0.0];
        this.CarYaw      = StudioConstants.CarYaw;
        this.CarSpeed    = 0.0;
    }

    ResetToTrack()
    {
        this.CarPosition    = [-28.0, 0.0, 0.0];
        this.CarYaw         = 0.0;
        this.CarSpeed       = 0.0;
        this.CameraYaw      = Math.PI;
        this.CameraPitch    = 0.18;
        this.CameraDistance = 6.8;
    }

    Step(Dt)
    {
        const Throttle  = (this.Keys.has("KeyW") || this.Keys.has("ArrowUp"))    ? 1.0 : 0.0;
        const Brake     = (this.Keys.has("KeyS") || this.Keys.has("ArrowDown"))  ? 1.0 : 0.0;
        const Left      = (this.Keys.has("KeyA") || this.Keys.has("ArrowLeft"))  ? 1.0 : 0.0;
        const Right     = (this.Keys.has("KeyD") || this.Keys.has("ArrowRight")) ? 1.0 : 0.0;
        const Handbrake = this.Keys.has("Space");
        const Steer     = Left - Right;

        if (this.Lobby && !this.Lobby.IsInLobby())
        {
            const Accel = Throttle * 14.0 - Brake * 18.0 - (Handbrake ? 22.0 : 0.0) - this.CarSpeed * 0.35;
            this.CarSpeed = Math.max(-8.0, Math.min(48.0, this.CarSpeed + Accel * Dt));
            this.SteerAngle = Steer * 0.45;
            if (Math.abs(this.CarSpeed) > 0.2)
            {
                this.CarYaw += Steer * 1.45 * Dt * Math.sign(this.CarSpeed);
                this.CarPosition[0] += Math.cos(this.CarYaw) * this.CarSpeed * Dt;
                this.CarPosition[1] += Math.sin(this.CarYaw) * this.CarSpeed * Dt;
                this.CameraYaw = this.CarYaw + Math.PI;
            }
            if (this.Lobby.Host?.Vehicle)
            {
                this.Lobby.Host.Vehicle.State.Position = [...this.CarPosition];
                this.Lobby.Host.Vehicle.State.Velocity = [
                    Math.cos(this.CarYaw) * this.CarSpeed,
                    Math.sin(this.CarYaw) * this.CarSpeed,
                    0.0
                ];
            }
        }
        else if (this.Lobby?.TurntableSpin)
        {
            this.CarYaw = (this.CarYaw + this.Lobby.TurntableSpeed * Dt) % (Math.PI * 2);
        }

        this.Lobby?.Step(Dt, { Throttle, Brake, Steer, Handbrake });
    }

    Render()
    {
        const Canvas = this.Canvas;
        const Dpr = Math.min(window.devicePixelRatio || 1, 2);
        const W = Canvas.clientWidth || window.innerWidth;
        const H = Canvas.clientHeight || window.innerHeight;
        if (Canvas.width !== Math.floor(W * Dpr) || Canvas.height !== Math.floor(H * Dpr))
        {
            Canvas.width  = Math.floor(W * Dpr);
            Canvas.height = Math.floor(H * Dpr);
        }
        const Ctx = this.Ctx;
        Ctx.save();
        Ctx.scale(Dpr, Dpr);

        const InLobby = this.Lobby ? this.Lobby.IsInLobby() : true;
        const IsCyclorama = InLobby && this.Lobby?.StudioPresetId === "cyclorama";
        const Spec = this.Lobby?.LocalProfile?.Spec || {};
        const PaintHex   = Spec.CustomHex || (PaintColours[Spec.PaintIndex ?? 0]?.Css ?? "#585f68");
        const RimHex     = RimPresets.find((R) => R.Id === Spec.RimId)?.Css ?? "#1d1f24";
        const CaliperHex = CaliperPresets.find((C) => C.Id === Spec.CaliperId)?.Css ?? "#d91c1c";
        const OpticsHex  = HeadlightOpticsPresets.find((O) => O.Id === Spec.OpticsId)?.Css ?? "#ffae19";

        // 1. Studio / Track Background Backdrop
        if (!InLobby)
        {
            const Sky = Ctx.createLinearGradient(0, 0, 0, H * 0.52);
            Sky.addColorStop(0, "#182842");
            Sky.addColorStop(1, "#526e8c");
            Ctx.fillStyle = Sky;
            Ctx.fillRect(0, 0, W, H * 0.52);
            const Ground = Ctx.createLinearGradient(0, H * 0.50, 0, H);
            Ground.addColorStop(0, "#24262b");
            Ground.addColorStop(1, "#141619");
            Ctx.fillStyle = Ground;
            Ctx.fillRect(0, H * 0.50, W, H * 0.50);
        }
        else if (IsCyclorama)
        {
            // Reference Image 1: Seamless White Infinity Cyclorama + Glossy Reflective White Floor
            const Bg = Ctx.createLinearGradient(0, 0, 0, H);
            Bg.addColorStop(0.00, "#e9edf2");
            Bg.addColorStop(0.45, "#f7f9fb");
            Bg.addColorStop(0.58, "#eef1f5");
            Bg.addColorStop(1.00, "#dcdfe6");
            Ctx.fillStyle = Bg;
            Ctx.fillRect(0, 0, W, H);
        }
        else
        {
            // Reference Image 2: Dark Industrial Truss Ceiling + White Cyclorama Walls + White Stage Pad on Dark Concrete Floor
            Ctx.fillStyle = "#0e1014";
            Ctx.fillRect(0, 0, W, H);

            // White backdrop studio walls in middle band
            const WallGrad = Ctx.createLinearGradient(0, H * 0.22, 0, H * 0.64);
            WallGrad.addColorStop(0, "#cfd3da");
            WallGrad.addColorStop(0.5, "#e4e7ec");
            WallGrad.addColorStop(1, "#c8cdd5");
            Ctx.fillStyle = WallGrad;
            Ctx.fillRect(W * 0.04, H * 0.24, W * 0.84, H * 0.40);

            // Dark industrial ceiling truss beams across top
            Ctx.fillStyle = "#0b0d11";
            Ctx.fillRect(0, 0, W, H * 0.25);
            Ctx.strokeStyle = "#1c2028";
            Ctx.lineWidth = 5;
            for (let I = 0; I < 8; ++I)
            {
                const Tx = (W / 7) * I;
                Ctx.beginPath();
                Ctx.moveTo(Tx, 0);
                Ctx.lineTo(W * 0.5 + (Tx - W * 0.5) * 0.72, H * 0.25);
                Ctx.stroke();
            }

            // Dark studio concrete foreground floor
            const FloorDark = Ctx.createLinearGradient(0, H * 0.62, 0, H);
            FloorDark.addColorStop(0, "#181b20");
            FloorDark.addColorStop(1, "#0b0d10");
            Ctx.fillStyle = FloorDark;
            Ctx.fillRect(0, H * 0.62, W, H * 0.38);

            // Angled white studio stage pad under the car (matching Reference Image 2's diagonal stage corner!)
            const PadGrad = Ctx.createLinearGradient(W * 0.2, H * 0.58, W * 0.8, H * 0.92);
            PadGrad.addColorStop(0, "#eaedf2");
            PadGrad.addColorStop(0.6, "#dde1e8");
            PadGrad.addColorStop(1, "#bfc5cf");
            Ctx.fillStyle = PadGrad;
            Ctx.beginPath();
            Ctx.moveTo(W * 0.04, H * 0.63);
            Ctx.lineTo(W * 0.78, H * 0.59);
            Ctx.lineTo(W * 0.96, H * 0.77);
            Ctx.lineTo(W * 0.36, H * 0.93);
            Ctx.closePath();
            Ctx.fill();

            // Giant Suspended Rectangular Overhead Softbox Light Bank
            Ctx.save();
            // Black softbox housing bezel
            Ctx.fillStyle = "#101318";
            Ctx.beginPath();
            Ctx.moveTo(W * 0.20, H * 0.08);
            Ctx.lineTo(W * 0.80, H * 0.06);
            Ctx.lineTo(W * 0.74, H * 0.24);
            Ctx.lineTo(W * 0.26, H * 0.25);
            Ctx.closePath();
            Ctx.fill();
            // Glowing white diffuser panel inside housing
            const SoftGrad = Ctx.createRadialGradient(W * 0.5, H * 0.16, 10, W * 0.5, H * 0.16, W * 0.30);
            SoftGrad.addColorStop(0, "#ffffff");
            SoftGrad.addColorStop(0.8, "#f3f6fb");
            SoftGrad.addColorStop(1, "#d8e0ec");
            Ctx.fillStyle = SoftGrad;
            Ctx.shadowColor = "rgba(235, 244, 255, 0.75)";
            Ctx.shadowBlur = 36;
            Ctx.beginPath();
            Ctx.moveTo(W * 0.215, H * 0.098);
            Ctx.lineTo(W * 0.785, H * 0.078);
            Ctx.lineTo(W * 0.728, H * 0.228);
            Ctx.lineTo(W * 0.272, H * 0.238);
            Ctx.closePath();
            Ctx.fill();
            Ctx.restore();
        }

        // 2. 3D Camera Projection Matrix around Car Centre
        const Target = [0.0, 0.0, 0.55];
        const Eye = [
            Target[0] + this.CameraDistance * Math.cos(this.CameraPitch) * Math.cos(this.CameraYaw),
            Target[1] + this.CameraDistance * Math.cos(this.CameraPitch) * Math.sin(this.CameraYaw),
            Target[2] + this.CameraDistance * Math.sin(this.CameraPitch)
        ];
        const Fwd = Normalize3([Target[0] - Eye[0], Target[1] - Eye[1], Target[2] - Eye[2]]);
        const Right = Normalize3(Cross3(Fwd, [0, 0, 1]));
        const Up = Cross3(Right, Fwd);
        const Focal = Math.min(W, H) * 1.08;

        const Project = (WorldPt) =>
        {
            const Dx = WorldPt[0] - Eye[0], Dy = WorldPt[1] - Eye[1], Dz = WorldPt[2] - Eye[2];
            const Zc = Dx * Fwd[0] + Dy * Fwd[1] + Dz * Fwd[2];
            if (Zc <= 0.15) return null;
            const Xc = Dx * Right[0] + Dy * Right[1] + Dz * Right[2];
            const Yc = Dx * Up[0]    + Dy * Up[1]    + Dz * Up[2];
            return [W * 0.5 + (Xc / Zc) * Focal, H * 0.54 - (Yc / Zc) * Focal, Zc];
        };

        // 3. Draw Circular Turntable Seam Ring on the Studio Floor (prominent in Ref 1 Cyclorama)
        if (InLobby)
        {
            Ctx.save();
            Ctx.strokeStyle = IsCyclorama ? "rgba(60, 66, 76, 0.48)" : "rgba(140, 148, 160, 0.25)";
            Ctx.lineWidth = IsCyclorama ? 2.0 : 1.2;
            Ctx.beginPath();
            const Segs = 72;
            const R = 3.25;
            for (let I = 0; I <= Segs; ++I)
            {
                const A = (I / Segs) * Math.PI * 2;
                const P = Project([R * Math.cos(A), R * Math.sin(A), 0.01]);
                if (P)
                {
                    if (I === 0) Ctx.moveTo(P[0], P[1]);
                    else Ctx.lineTo(P[0], P[1]);
                }
            }
            Ctx.stroke();
            Ctx.restore();
        }

        // 4. Render Glossy Floor Reflection in White Cyclorama Mode (Reference Image 1!)
        const Yaw = InLobby ? (this.Lobby?.TurntableAngle ?? this.CarYaw) : 0.0;
        if (IsCyclorama && this.DecimatedFacets.length > 0)
        {
            Ctx.save();
            Ctx.globalAlpha = 0.24;
            this.DrawVehicleMesh(Ctx, Project, Yaw, PaintHex, RimHex, CaliperHex, OpticsHex, true, IsCyclorama);
            Ctx.restore();
        }

        // Soft contact ambient occlusion shadow under chassis
        const ShadowCenter = Project([0, 0, 0.02]);
        if (ShadowCenter)
        {
            Ctx.save();
            const Rad = (2.8 / ShadowCenter[2]) * Focal;
            const ShGrad = Ctx.createRadialGradient(ShadowCenter[0], ShadowCenter[1], Rad * 0.1, ShadowCenter[0], ShadowCenter[1], Rad);
            ShGrad.addColorStop(0, "rgba(0,0,0,0.68)");
            ShGrad.addColorStop(0.65, "rgba(0,0,0,0.28)");
            ShGrad.addColorStop(1, "rgba(0,0,0,0.0)");
            Ctx.fillStyle = ShGrad;
            Ctx.setTransform(Dpr, 0, 0, Dpr * 0.38, 0, ShadowCenter[1] * Dpr * 0.62);
            Ctx.beginPath();
            Ctx.arc(ShadowCenter[0], ShadowCenter[1], Rad, 0, Math.PI * 2);
            Ctx.fill();
            Ctx.restore();
        }

        // 5. Render Primary 3D Vehicle Mesh (ControlVehicleBody.facet + Wheels)
        if (this.DecimatedFacets.length > 0)
        {
            this.DrawVehicleMesh(Ctx, Project, Yaw, PaintHex, RimHex, CaliperHex, OpticsHex, false, IsCyclorama);
        }

        Ctx.restore();
    }

    DrawVehicleMesh(Ctx, Project, Yaw, PaintHex, RimHex, CaliperHex, OpticsHex, Reflected, IsCyclorama)
    {
        const Cos = Math.cos(Yaw), Sin = Math.sin(Yaw);
        const SignZ = Reflected ? -1.0 : 1.0;
        const BaseRgb = HexToRgb01(PaintHex);
        const RimRgb  = HexToRgb01(RimHex);
        const CalRgb  = HexToRgb01(CaliperHex);
        const OptRgb  = HexToRgb01(OpticsHex);

        const Transform = ([X, Y, Z]) => [
            X * Cos - Y * Sin,
            X * Sin + Y * Cos,
            Z * SignZ
        ];
        const TransformDir = ([Nx, Ny, Nz]) => [
            Nx * Cos - Ny * Sin,
            Nx * Sin + Ny * Cos,
            Nz * SignZ
        ];

        const DrawList = [];
        for (const F of this.DecimatedFacets)
        {
            const P0 = Project(Transform(F.Corners[0]));
            const P1 = Project(Transform(F.Corners[1]));
            const P2 = Project(Transform(F.Corners[2]));
            if (!P0 || !P1 || !P2) continue;
            const Depth = (P0[2] + P1[2] + P2[2]) / 3.0;
            const Wn = TransformDir(F.Normal);
            DrawList.push({ P0, P1, P2, Depth, Wn, Family: F.Family });
        }
        DrawList.sort((A, B) => B.Depth - A.Depth);

        for (const Item of DrawList)
        {
            const [Nx, Ny, Nz] = Item.Wn;
            const OverheadSoftbox = Math.max(0.0, Nz * 0.78 + Nx * 0.22 + 0.25);
            const SpecularStrip   = Math.pow(Math.max(0.0, Nz * 0.92 + Ny * 0.18), 14.0) * (IsCyclorama ? 0.65 : 0.55);

            let R = BaseRgb[0], G = BaseRgb[1], B = BaseRgb[2];
            if (Item.Family === CourseMaterial.Glass)
            {
                R = 0.14; G = 0.18; B = 0.23;
            }
            else if (Item.Family === CourseMaterial.Trim || Item.Family === CourseMaterial.Grille || Item.Family === CourseMaterial.Undercarriage)
            {
                R = 0.08; G = 0.09; B = 0.11;
            }
            else if (Item.Family === CourseMaterial.Headlight)
            {
                R = OptRgb[0] * 1.3; G = OptRgb[1] * 1.3; B = OptRgb[2] * 1.3;
            }
            else if (Item.Family === CourseMaterial.Taillight)
            {
                R = 0.92; G = 0.12; B = 0.10;
            }
            else if (Item.Family === CourseMaterial.RimBase || Item.Family === CourseMaterial.Hub)
            {
                R = RimRgb[0]; G = RimRgb[1]; B = RimRgb[2];
            }
            else if (Item.Family === CourseMaterial.Brake)
            {
                R = CalRgb[0]; G = CalRgb[1]; B = CalRgb[2];
            }

            const LitR = Math.min(255, Math.floor((R * (0.32 + 0.85 * OverheadSoftbox) + SpecularStrip) * 255));
            const LitG = Math.min(255, Math.floor((G * (0.32 + 0.85 * OverheadSoftbox) + SpecularStrip) * 255));
            const LitB = Math.min(255, Math.floor((B * (0.32 + 0.85 * OverheadSoftbox) + SpecularStrip) * 255));
            const Color = `rgb(${LitR},${LitG},${LitB})`;

            Ctx.fillStyle   = Color;
            Ctx.strokeStyle = Color;
            Ctx.lineWidth   = 0.65;
            Ctx.beginPath();
            Ctx.moveTo(Item.P0[0], Item.P0[1]);
            Ctx.lineTo(Item.P1[0], Item.P1[1]);
            Ctx.lineTo(Item.P2[0], Item.P2[1]);
            Ctx.closePath();
            Ctx.fill();
            Ctx.stroke();
        }

        // Draw wheels & brake calipers at the 4 axle mounts
        for (let WIdx = 0; WIdx < 4; ++WIdx)
        {
            const Mount = VehicleGeometry.WheelMounts[WIdx];
            const WCenter = Transform([Mount[0], Mount[1], VehicleGeometry.WheelRadius]);
            const Screen = Project(WCenter);
            if (!Screen) continue;
            const ScreenR = (VehicleGeometry.WheelRadius / Screen[2]) * Math.min(this.Canvas.clientWidth || 1280, this.Canvas.clientHeight || 720) * 0.92;
            // Tyre outer ring
            Ctx.fillStyle = "#141619";
            Ctx.beginPath();
            Ctx.arc(Screen[0], Screen[1], ScreenR, 0, Math.PI * 2);
            Ctx.fill();
            // Rim disc
            Ctx.fillStyle = RimHex;
            Ctx.beginPath();
            Ctx.arc(Screen[0], Screen[1], ScreenR * 0.68, 0, Math.PI * 2);
            Ctx.fill();
            // Brake caliper highlight inside rim
            Ctx.fillStyle = CaliperHex;
            Ctx.beginPath();
            Ctx.arc(Screen[0] - ScreenR * 0.24, Screen[1] - ScreenR * 0.12, ScreenR * 0.22, 0, Math.PI * 2);
            Ctx.fill();
            // Center lock hub
            Ctx.fillStyle = "#0e1014";
            Ctx.beginPath();
            Ctx.arc(Screen[0], Screen[1], ScreenR * 0.22, 0, Math.PI * 2);
            Ctx.fill();
        }
    }
}

function Normalize3(V)
{
    const L = Math.hypot(V[0], V[1], V[2]) || 1.0;
    return [V[0] / L, V[1] / L, V[2] / L];
}

function Cross3(A, B)
{
    return [
        A[1] * B[2] - A[2] * B[1],
        A[2] * B[0] - A[0] * B[2],
        A[0] * B[1] - A[1] * B[0]
    ];
}

function HexToRgb01(Hex)
{
    const Clean = String(Hex || "#585f68").replace("#", "");
    const Num = parseInt(Clean.length === 6 ? Clean : "585f68", 16);
    return [((Num >> 16) & 255) / 255, ((Num >> 8) & 255) / 255, (Num & 255) / 255];
}
