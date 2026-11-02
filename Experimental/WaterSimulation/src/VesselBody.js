/*====================================================================================================================================
                                                        VESSEL BODY.JS
====================================================================================================================================*/
// 🧩 Buoyancy from the water height atlas, autopilot path, wake and spray emission, and hull placement

import { Mat4FromPose, Saturate, Mix } from './LinearAlgebra.js';

const ProbeOffsets =
[
    { Name: 'Bow',       X:  0.0, Z: -2.85, Weight: 0.34 },
    { Name: 'Stern',     X:  0.0, Z:  2.95, Weight: 0.34 },
    { Name: 'Port',      X: -1.05, Z: 0.35, Weight: 0.16 },
    { Name: 'Starboard', X:  1.05, Z: 0.35, Weight: 0.16 }
];

export class VesselBody
{
    constructor(Descriptor = {})
    {
        this.CourseCenter = Descriptor.CourseCenter ?? [0, -4];
        this.CourseRadius = Descriptor.CourseRadius ?? 36;
        this.CourseAngle = Descriptor.CourseAngle ?? 1.15;
        this.Position = [0, 0, 20];
        this.Heading = 0.0;
        this.Pitch = 0.0;
        this.Roll = 0.0;
        this.Speed = 0.0;
        this.Heave = 0.0;
        this.Transform = new Float32Array(16);
        this.SampledHeights = [0, 0, 0, 0];
        this.WakeTimer = 0.0;
        this.Emitters = null;
        this.SplashBurst = 0.0;
    }

    ProbePosition(Probe, Heading)
    {
        const Cosine = Math.cos(Heading);
        const Sine = Math.sin(Heading);
        return [
            this.Position[0] + Probe.X * Cosine - Probe.Z * Sine,
            this.Position[2] + Probe.X * Sine + Probe.Z * Cosine
        ];
    }

    Update(DeltaSeconds, HeightSampler, Settings, SimulationTime)
    {
        const Throttle = Saturate(Settings.VesselThrottle);
        const TargetSpeed = Mix(0.55, 11.0, Throttle * Throttle);
        this.Speed = Mix(this.Speed, TargetSpeed, Saturate(DeltaSeconds * 1.6));

        this.CourseAngle += (this.Speed / this.CourseRadius) * DeltaSeconds;
        const Wobble = Math.sin(SimulationTime * 0.13) * 0.19;
        const Angle = this.CourseAngle + Wobble;

        const TargetX = this.CourseCenter[0] + Math.sin(Angle) * this.CourseRadius;
        const TargetZ = this.CourseCenter[1] + Math.cos(Angle) * this.CourseRadius;
        this.Position[0] = TargetX;
        this.Position[2] = TargetZ;

        // 📝 Local -Z is the bow, so the yaw that aligns it with the course tangent is atan2(-cos, sin)
        const HeadingTarget = Math.atan2(-Math.cos(Angle), Math.sin(Angle));
        let HeadingDelta = HeadingTarget - this.Heading;
        while (HeadingDelta > Math.PI) HeadingDelta -= Math.PI * 2.0;
        while (HeadingDelta < -Math.PI) HeadingDelta += Math.PI * 2.0;
        this.Heading += HeadingDelta * Saturate(DeltaSeconds * 2.2);

        let WeightedHeight = 0.0;
        let BowHeight = 0.0;
        let SternHeight = 0.0;
        let PortHeight = 0.0;
        let StarboardHeight = 0.0;
        let WeightTotal = 0.0;
        const HeadingSource = Mat4FromPose(new Float32Array(16), [0, 0, 0], this.Heading, 0, 0, 1.0);

        ProbeOffsets.forEach((Probe, Index) =>
        {
            const World = [Probe.X * HeadingSource[0] + Probe.Z * HeadingSource[8] + this.Position[0],
                           Probe.X * HeadingSource[2] + Probe.Z * HeadingSource[10] + this.Position[2]];
            const Height = HeightSampler(World[0], World[1]);
            this.SampledHeights[Index] = Height;

            WeightedHeight += Height * Probe.Weight;
            WeightTotal += Probe.Weight;

            if (Probe.Name === 'Bow') BowHeight = Height;
            if (Probe.Name === 'Stern') SternHeight = Height;
            if (Probe.Name === 'Port') PortHeight = Height;
            if (Probe.Name === 'Starboard') StarboardHeight = Height;
        });

        const AverageHeight = WeightTotal > 0.0 ? WeightedHeight / WeightTotal : 0.0;
        const TargetHeave = AverageHeight + 0.16;
        const TargetPitch = Math.atan2(SternHeight - BowHeight, 5.8) * 0.9;
        const TargetRoll = Math.atan2(StarboardHeight - PortHeight, 2.1) * 0.85;

        const Response = Saturate(DeltaSeconds * 4.2);
        this.Heave = Mix(this.Heave, TargetHeave, Response);
        this.Pitch = Mix(this.Pitch, TargetPitch, Response);
        this.Roll = Mix(this.Roll, TargetRoll, Saturate(DeltaSeconds * 3.0));

        this.Position[1] = this.Heave;
        Mat4FromPose(this.Transform, this.Position, this.Heading, this.Pitch, this.Roll, 1.0);

        this.SplashBurst = Saturate(Math.abs(BowHeight - SternHeight) * 1.6);
        return this;
    }

    // 💡 Wake leaves the transom, so splats trail the hull along its heading
    QueueWake(RippleField, FoamField, Settings, DeltaSeconds)
    {
        const Throttle = Saturate(Settings.VesselThrottle);
        const WakeStrength = (0.35 + this.Speed * 0.22) * (0.55 + Throttle * 0.9);
        const Cosine = Math.cos(this.Heading);
        const Sine = Math.sin(this.Heading);
        const SternX = this.Position[0] + Sine * 3.1;
        const SternZ = this.Position[2] + Cosine * 3.1;
        const PointSize = 10.0 + this.Speed * 1.4;

        if (RippleField)
        {
            RippleField.QueueSplat(SternX, SternZ, -WakeStrength, PointSize);
            RippleField.QueueSplat(SternX - Cosine * 1.1, SternZ + Sine * 1.1, WakeStrength * 0.45, PointSize * 0.8);

            const BowX = this.Position[0] - Sine * 2.9;
            const BowZ = this.Position[2] - Cosine * 2.9;
            RippleField.QueueSplat(BowX, BowZ, this.SplashBurst * 0.55, PointSize * 0.7);
        }

        if (FoamField)
        {
            this.WakeTimer += DeltaSeconds * (2.0 + this.Speed * 1.6);

            if (this.WakeTimer > 0.05)
            {
                this.WakeTimer = 0.0;
                FoamField.QueueSplat(SternX, SternZ, 0.35 + this.Speed * 0.075, PointSize * 1.6);
                const SideX = Cosine * 1.4;
                const SideZ = -Sine * 1.4;
                FoamField.QueueSplat(SternX - SideX, SternZ - SideZ, 0.22 + this.Speed * 0.04, PointSize * 1.1);
                FoamField.QueueSplat(SternX + SideX, SternZ + SideZ, 0.22 + this.Speed * 0.04, PointSize * 1.1);
            }
        }
    }

    EmitterDiagnostics()
    {
        const Cosine = Math.cos(this.Heading);
        const Sine = Math.sin(this.Heading);

        return {
            Bow:   [this.Position[0] - Sine * 3.0, this.Position[1] + 0.15, this.Position[2] - Cosine * 3.0],
            Stern: [this.Position[0] + Sine * 3.2, this.Position[1] + 0.1, this.Position[2] + Cosine * 3.2]
        };
    }
}
