/*============================================================================================================================================
                                                      UNREALCAMERARIG.JS
============================================================================================================================================*/
// 🧩 Editor fly-camera with right-mouse look, WASD/QE translation, inertial damping, wheel speed scaling and altitude scaling

export function ConstructCameraConfiguration(Override = {})
{
    return {
        FlySpeed: 420,                  // [m/s] - base translation speed
        MouseSensitivity: 0.0022,       // [rad/px]
        AccelerationSharpness: 8.5,     // [1/s]
        LinearDamping: 6.4,             // [1/s]
        PitchCeiling: 89 * Math.PI / 180,
        BoostMultiplier: 4.0,
        FineMultiplier: 0.22,
        AltitudeScalingEnabled: true,
        PlaneLockedWASD: false,
        MinimumSpeed: 0.5,
        MaximumSpeed: 900000,
        ...Override
    };
}

export class UnrealCameraRig
{
    constructor(Configuration = ConstructCameraConfiguration())
    {
        this.Configuration = Configuration;
        this.Position = [0, 2400, 26000];   // [m]
        this.Yaw = 0;                       // [rad]
        this.Pitch = -0.32;                 // [rad]
        this.Velocity = [0, 0, 0];          // [m/s]
        this.FlySpeed = Configuration.FlySpeed;
        this.PressedKeys = new Set();
        this.LookEnabled = false;
        this.PanEnabled = false;
        this.FieldOfViewDegrees = 62;
        this.NearPlane = 1.0;
        this.FarPlane = 60000000;
    }

    ResolveBasis()
    {
        const CosinePitch = Math.cos(this.Pitch);
        const Forward = [Math.sin(this.Yaw) * CosinePitch, Math.sin(this.Pitch), -Math.cos(this.Yaw) * CosinePitch];
        const Right = [Math.cos(this.Yaw), 0, Math.sin(this.Yaw)];
        const Up = [
            Right[1] * Forward[2] - Right[2] * Forward[1],
            Right[2] * Forward[0] - Right[0] * Forward[2],
            Right[0] * Forward[1] - Right[1] * Forward[0]
        ];
        return { Forward, Right, Up };
    }

    ResolveRay(ScreenX, ScreenY, Aspect)
    {
        const { Forward, Right, Up } = this.ResolveBasis();
        const Tangent = Math.tan(this.FieldOfViewDegrees * Math.PI / 360);
        const OffsetX = ScreenX * Tangent * Aspect;
        const OffsetY = ScreenY * Tangent;
        const Direction = [
            Forward[0] + Right[0] * OffsetX + Up[0] * OffsetY,
            Forward[1] + Right[1] * OffsetX + Up[1] * OffsetY,
            Forward[2] + Right[2] * OffsetX + Up[2] * OffsetY
        ];
        const Length = Math.hypot(...Direction);
        return [Direction[0] / Length, Direction[1] / Length, Direction[2] / Length];
    }

    RotateBy(DeltaX, DeltaY)
    {
        this.Yaw += DeltaX * this.Configuration.MouseSensitivity;
        this.Pitch = Math.max(-this.Configuration.PitchCeiling, Math.min(this.Configuration.PitchCeiling, this.Pitch - DeltaY * this.Configuration.MouseSensitivity));
    }

    PanBy(DeltaX, DeltaY, SurfaceElevation = 0)
    {
        const { Right, Up } = this.ResolveBasis();
        const Altitude = Math.max(50, this.Position[1] - SurfaceElevation);
        const Scale = Altitude * 0.0016;
        this.Position[0] -= (Right[0] * DeltaX + Up[0] * -DeltaY) * Scale;
        this.Position[1] -= (Right[1] * DeltaX + Up[1] * -DeltaY) * Scale;
        this.Position[2] -= (Right[2] * DeltaX + Up[2] * -DeltaY) * Scale;
    }

    AdjustSpeed(WheelDelta)
    {
        const Factor = Math.pow(1.18, -WheelDelta / 100);
        this.FlySpeed = Math.max(this.Configuration.MinimumSpeed, Math.min(this.Configuration.MaximumSpeed, this.FlySpeed * Factor));
        return this.FlySpeed;
    }

    ResolveEffectiveSpeed(SurfaceElevation = 0)
    {
        let Speed = this.FlySpeed;
        if (this.PressedKeys.has('ShiftLeft') || this.PressedKeys.has('ShiftRight')) Speed *= this.Configuration.BoostMultiplier;
        if (this.PressedKeys.has('ControlLeft') || this.PressedKeys.has('ControlRight')) Speed *= this.Configuration.FineMultiplier;
        if (this.Configuration.AltitudeScalingEnabled)
        {
            const Altitude = Math.max(0, this.Position[1] - SurfaceElevation);
            Speed *= Math.max(1, Math.pow(Altitude / 400, 0.62));
        }
        return Math.min(this.Configuration.MaximumSpeed, Speed);
    }

    IntegrateMotion(DeltaSeconds, SurfaceElevation = 0)
    {
        const Keys = this.PressedKeys;
        const { Forward, Right } = this.ResolveBasis();
        const HorizontalForward = this.Configuration.PlaneLockedWASD
            ? [Forward[0], 0, Forward[2]] : Forward;
        const NormalizedLength = Math.hypot(...HorizontalForward) || 1;
        const UnitForward = [HorizontalForward[0] / NormalizedLength, HorizontalForward[1] / NormalizedLength, HorizontalForward[2] / NormalizedLength];
        let IntentX = 0;
        let IntentY = 0;
        let IntentZ = 0;
        if (Keys.has('KeyW')) { IntentX += UnitForward[0]; IntentY += UnitForward[1]; IntentZ += UnitForward[2]; }
        if (Keys.has('KeyS')) { IntentX -= UnitForward[0]; IntentY -= UnitForward[1]; IntentZ -= UnitForward[2]; }
        if (Keys.has('KeyD')) { IntentX += Right[0]; IntentZ += Right[2]; }
        if (Keys.has('KeyA')) { IntentX -= Right[0]; IntentZ -= Right[2]; }
        if (Keys.has('KeyE')) IntentY += 1;
        if (Keys.has('KeyQ')) IntentY -= 1;
        const IntentLength = Math.hypot(IntentX, IntentY, IntentZ);
        if (IntentLength > 0) { IntentX /= IntentLength; IntentY /= IntentLength; IntentZ /= IntentLength; }
        const Speed = this.ResolveEffectiveSpeed(SurfaceElevation);
        const TargetVelocity = [IntentX * Speed, IntentY * Speed, IntentZ * Speed];
        const Blend = 1 - Math.exp(-this.Configuration.AccelerationSharpness * DeltaSeconds);
        for (let Axis = 0; Axis < 3; ++Axis)
        {
            this.Velocity[Axis] += (TargetVelocity[Axis] - this.Velocity[Axis]) * Blend;
            this.Velocity[Axis] *= Math.exp(-Math.min(1, DeltaSeconds) * (IntentLength > 0 ? 0 : this.Configuration.LinearDamping));
            this.Position[Axis] += this.Velocity[Axis] * DeltaSeconds;
        }
        return this.Position;
    }

    FocusOn(Target, Distance = 900)
    {
        const { Forward } = this.ResolveBasis();
        this.Position[0] = Target[0] - Forward[0] * Distance;
        this.Position[1] = Target[1] + Distance * 0.55;
        this.Position[2] = Target[2] - Forward[2] * Distance;
        this.Pitch = -0.42;
        this.Velocity.fill(0);
    }

    ApplyViewPreset(Identifier, Extent, SurfaceElevation = 0)
    {
        const Presets = {
            Continent: { Position: [0, Extent * 0.42, Extent * 0.62], Yaw: 0, Pitch: -0.58 },
            Shoreline: { Position: [-Extent * 0.06, 340, Extent * 0.14], Yaw: 0.15, Pitch: -0.10 },
            SeaLevel: { Position: [0, 45, Extent * 0.09], Yaw: 0.0, Pitch: -0.03 },
            TopDown: { Position: [0, Extent * 0.9, 1], Yaw: 0, Pitch: -1.5607 },
            CityVase: { Position: [-Extent * 0.055, 220, Extent * 0.145], Yaw: 0.2, Pitch: -0.45 }
        };
        const Preset = Presets[Identifier] || Presets.Continent;
        this.Position = [...Preset.Position];
        this.Yaw = Preset.Yaw;
        this.Pitch = Preset.Pitch;
        this.Velocity.fill(0);
        return this.Position;
    }
}
