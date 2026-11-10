//============================================================================================================================================
//                                                           CINEMATICCAMERA.JS
//============================================================================================================================================
// 🧩 Continuous, zero-fade-to-black Catmull-Rom spline camera director for both Lobby · Studio and Proving Grounds.
//    Replaces hard cuts + black veils with seamless C¹ closed-loop automotive photography choreography.

import { EaseCinematic, InterpolateAngle, ClampStudioEye } from "./LobbyCameraMotion.js";
import { ResolveCourseHeight } from "./CourseStructure.js";
import { TransformPoint } from "./TransformProjection.js";
import { VerticalFovFromFocalLength, FocalLengthFromVerticalFov } from "./CameraVisuals.js";

// Closed-loop studio showcase keyframes — smoothly connected via periodic Catmull-Rom interpolation (zero cuts, zero black fades).
const StudioSeamlessKeyframes = Object.freeze([
    { Name: "01 · Hero Three-Quarter Stance",   Duration: 6.5, FocalMm: 45, Yaw:  0.68, Pitch: 0.11, Distance:  6.2, TargetOffset: [ 0.25,  0.00, 0.36] },
    { Name: "02 · Smoked Nose & Splitter Skim", Duration: 5.8, FocalMm: 54, Yaw:  0.06, Pitch: 0.06, Distance:  4.3, TargetOffset: [ 1.15,  0.00, 0.28] },
    { Name: "03 · Caliper & Fender Arch Glide", Duration: 5.8, FocalMm: 58, Yaw: -0.58, Pitch: 0.05, Distance:  4.1, TargetOffset: [ 1.05, -0.35, 0.24] },
    { Name: "04 · Beltline Softbox Highlight",  Duration: 6.8, FocalMm: 46, Yaw: -1.48, Pitch: 0.17, Distance:  6.4, TargetOffset: [ 0.00,  0.00, 0.38] },
    { Name: "05 · Rear Haunch & Diffuser Arc",  Duration: 6.2, FocalMm: 44, Yaw: -2.55, Pitch: 0.09, Distance:  5.3, TargetOffset: [-1.05,  0.00, 0.34] },
    { Name: "06 · Cyclorama Fleet Crane",       Duration: 7.4, FocalMm: 35, Yaw:  2.65, Pitch: 0.31, Distance: 13.4, TargetOffset: [ 0.00,  0.00, 0.48] },
    { Name: "07 · Softbox Crown Descent",       Duration: 6.5, FocalMm: 42, Yaw:  1.52, Pitch: 0.20, Distance:  8.2, TargetOffset: [ 0.15,  0.00, 0.40] }
]);

const StudioSculptureKeyframes = Object.freeze([
    { Name: "01 · Front Badge Low Push",        Duration: 6.0, FocalMm: 65, Yaw:  0.28, Pitch: 0.05, Distance:  3.9, TargetOffset: [ 1.25,  0.00, 0.26] },
    { Name: "02 · Shoulder Crease Specular",    Duration: 6.5, FocalMm: 58, Yaw: -0.88, Pitch: 0.13, Distance:  4.6, TargetOffset: [ 0.20, -0.25, 0.42] },
    { Name: "03 · Rear Deck & Wheel Lip",       Duration: 6.0, FocalMm: 62, Yaw: -2.35, Pitch: 0.08, Distance:  4.3, TargetOffset: [-1.00, -0.25, 0.32] },
    { Name: "04 · Greenhouse & Cabin Silhouette", Duration: 6.5, FocalMm: 50, Yaw: 1.45, Pitch: 0.18, Distance: 5.2, TargetOffset: [ 0.05,  0.20, 0.52] }
]);

const CourseSeamlessKeyframes = Object.freeze([
    { Name: "01 · Low Tracking Sweep",          Duration: 6.5, FocalMm: 35, Yaw:  0.65, Pitch: 0.10, Distance:  7.2, TargetOffset: [ 0.40,  0.00, 0.42] },
    { Name: "02 · Apex Wheel Close-Up",         Duration: 5.8, FocalMm: 52, Yaw: -0.45, Pitch: 0.05, Distance:  4.6, TargetOffset: [ 1.05, -0.30, 0.24] },
    { Name: "03 · Broadside Drift Arc",         Duration: 6.8, FocalMm: 42, Yaw: -1.75, Pitch: 0.14, Distance:  7.8, TargetOffset: [ 0.00,  0.00, 0.40] },
    { Name: "04 · Helicopter Chase Crane",      Duration: 7.5, FocalMm: 30, Yaw:  2.75, Pitch: 0.34, Distance: 13.8, TargetOffset: [ 0.60,  0.00, 0.50] },
    { Name: "05 · High-Speed Nose Lead",        Duration: 6.2, FocalMm: 44, Yaw:  1.45, Pitch: 0.12, Distance:  6.5, TargetOffset: [ 0.50,  0.00, 0.38] }
]);

export const ChoreographyStyles = Object.freeze([
    ["Seamless Studio Showcase", 0],
    ["Close-Up Sculpture Glide", 1],
    ["Static Manual Framing",    2]
]);

// 1D Catmull-Rom cubic Hermite spline through P0, P1, P2, P3 for u in [0, 1].
function CatmullRomScalar(P0, P1, P2, P3, U)
{
    const U2 = U * U;
    const U3 = U2 * U;
    return 0.5 * (
        (2.0 * P1) +
        (-P0 + P2) * U +
        (2.0 * P0 - 5.0 * P1 + 4.0 * P2 - P3) * U2 +
        (-P0 + 3.0 * P1 - 3.0 * P2 + P3) * U3
    );
}

// Unwraps angles relative to Base so Catmull-Rom never flips across the +-pi seam.
function UnwrapAngleNear(Angle, Reference)
{
    const Turn = 2.0 * Math.PI;
    let Delta = (Angle - Reference) % Turn;
    if (Delta > Math.PI) Delta -= Turn;
    if (Delta < -Math.PI) Delta += Turn;
    return Reference + Delta;
}

export class CinematicCamera
{
    constructor()
    {
        this.ShotIndex      = 0;
        this.Elapsed        = 0.0;
        this.Speed          = 1.0;
        this.Paused         = false;
        this.Style          = 0;     // 0 = Seamless Showcase, 1 = Close-Up Sculpture, 2 = Manual Keyframe
        this.Letterbox      = false;
        this.ManualBlend    = null;  // smooth transition when user clicks Next/Prev shot (zero black fade!)
        this.LastPose       = null;
        this.ManualYaw      = 0.72;
        this.ManualPitch    = 0.14;
        this.ManualDistance = 6.4;
    }

    SetStyle(StyleIndex)
    {
        const Next = Math.max(0, Math.min(2, Number(StyleIndex) || 0));
        if (Next !== this.Style && this.LastPose)
        {
            this.ManualBlend = {
                Elapsed: 0.0,
                Duration: 1.2,
                FromEye: this.LastPose.Eye.slice(),
                FromTarget: this.LastPose.Target.slice(),
                FromFov: this.LastPose.Fov
            };
        }
        this.Style = Next;
        this.ShotIndex = 0;
        this.Elapsed = 0.0;
    }

    AssignStyle(StyleIndex)
    {
        this.SetStyle(StyleIndex);
    }

    QueryShots(IsLobby)
    {
        if (!IsLobby)
            return CourseSeamlessKeyframes;
        return this.Style === 1 ? StudioSculptureKeyframes : StudioSeamlessKeyframes;
    }

    QueryActiveShot(IsLobby)
    {
        const Shots = this.QueryShots(IsLobby);
        return Shots[this.ShotIndex % Shots.length];
    }

    // Smoothly glides to the requested keyframe over 1.35 s — NEVER cuts or fades to black.
    AdvanceShot(IsLobby, Step = 1)
    {
        const Shots = this.QueryShots(IsLobby);
        if (this.LastPose)
        {
            this.ManualBlend = {
                Elapsed: 0.0,
                Duration: 1.35,
                FromEye: this.LastPose.Eye.slice(),
                FromTarget: this.LastPose.Target.slice(),
                FromFov: this.LastPose.Fov
            };
        }
        this.ShotIndex = (this.ShotIndex + Step + Shots.length) % Shots.length;
        this.Elapsed = 0.0;
        return this.QueryActiveShot(IsLobby);
    }

    EvaluateSplinePose(Shots, Index, LocalFraction)
    {
        const Count = Shots.length;
        const K0 = Shots[(Index - 1 + Count) % Count];
        const K1 = Shots[Index % Count];
        const K2 = Shots[(Index + 1) % Count];
        const K3 = Shots[(Index + 2) % Count];

        const Y1 = K1.Yaw;
        const Y0 = UnwrapAngleNear(K0.Yaw, Y1);
        const Y2 = UnwrapAngleNear(K2.Yaw, Y1);
        const Y3 = UnwrapAngleNear(K3.Yaw, Y2);

        const U = Math.max(0.0, Math.min(1.0, LocalFraction));
        return {
            Yaw:       CatmullRomScalar(Y0, Y1, Y2, Y3, U),
            Pitch:     Math.max(0.02, Math.min(0.68, CatmullRomScalar(K0.Pitch, K1.Pitch, K2.Pitch, K3.Pitch, U))),
            Distance:  Math.max(2.8, CatmullRomScalar(K0.Distance, K1.Distance, K2.Distance, K3.Distance, U)),
            FocalMm:   Math.max(16, Math.min(120, CatmullRomScalar(K0.FocalMm, K1.FocalMm, K2.FocalMm, K3.FocalMm, U))),
            TargetOffset: [0, 1, 2].map((Axis) =>
                CatmullRomScalar(K0.TargetOffset[Axis], K1.TargetOffset[Axis], K2.TargetOffset[Axis], K3.TargetOffset[Axis], U)
            ),
            ShotName: K1.Name
        };
    }

    Step(Seconds, VehicleTransform, VehicleYaw, IsLobby = false, FocalOverrideMm = 0)
    {
        const Shots = this.QueryShots(IsLobby);
        const Dt = Math.max(0.0, Seconds);

        if (!this.Paused && this.Style !== 2)
        {
            const Active = Shots[this.ShotIndex % Shots.length];
            this.Elapsed += Dt * Math.max(0.1, this.Speed);
            while (this.Elapsed >= Active.Duration)
            {
                this.Elapsed -= Active.Duration;
                this.ShotIndex = (this.ShotIndex + 1) % Shots.length;
            }
        }

        const Current = Shots[this.ShotIndex % Shots.length];
        let Evaluated;
        if (this.Style === 2)
        {
            Evaluated = {
                Yaw: this.ManualYaw,
                Pitch: this.ManualPitch,
                Distance: this.ManualDistance,
                FocalMm: FocalOverrideMm > 0 ? FocalOverrideMm : 50,
                TargetOffset: [0.1, 0.0, 0.38],
                ShotName: "Manual Cinema Framing"
            };
        }
        else
        {
            const Fraction = this.Elapsed / Math.max(Current.Duration, 0.1);
            Evaluated = this.EvaluateSplinePose(Shots, this.ShotIndex, Fraction);
            if (FocalOverrideMm > 0)
                Evaluated.FocalMm = FocalOverrideMm;
        }

        const WorldYaw = (VehicleYaw || 0.0) + Evaluated.Yaw;
        let Target = TransformPoint(VehicleTransform, Evaluated.TargetOffset);
        const CosPitch = Math.cos(Evaluated.Pitch);
        let Eye = [
            Target[0] + Math.cos(WorldYaw) * CosPitch * Evaluated.Distance,
            Target[1] + Math.sin(WorldYaw) * CosPitch * Evaluated.Distance,
            Target[2] + Math.sin(Evaluated.Pitch) * Evaluated.Distance
        ];

        if (IsLobby)
        {
            Eye = ClampStudioEye(Eye);
        }
        else
        {
            const Ground = ResolveCourseHeight(Eye[0], Eye[1]);
            Eye[2] = Math.max(Ground + 0.42, Eye[2]);
        }

        let Fov = VerticalFovFromFocalLength(Evaluated.FocalMm);

        // Smooth manual-skip transition (zero fade-to-black)
        if (this.ManualBlend)
        {
            this.ManualBlend.Elapsed += Dt;
            const Blend = EaseCinematic(this.ManualBlend.Elapsed / this.ManualBlend.Duration);
            Eye = this.ManualBlend.FromEye.map((V, I) => V + (Eye[I] - V) * Blend);
            Target = this.ManualBlend.FromTarget.map((V, I) => V + (Target[I] - V) * Blend);
            Fov = this.ManualBlend.FromFov + (Fov - this.ManualBlend.FromFov) * Blend;
            if (IsLobby)
                Eye = ClampStudioEye(Eye);
            if (this.ManualBlend.Elapsed >= this.ManualBlend.Duration)
                this.ManualBlend = null;
        }

        const FocusDistance = Math.max(1.0, Math.hypot(Target[0] - Eye[0], Target[1] - Eye[1], Target[2] - Eye[2]));
        const Pose = {
            Eye,
            Target,
            Up: [0.0, 0.0, 1.0],
            Fov,
            FocalMm: FocalLengthFromVerticalFov(Fov),
            FocusDistance,
            ShotIndex: this.ShotIndex,
            ShotName: Evaluated.ShotName
        };
        this.LastPose = Pose;
        return Pose;
    }
}
