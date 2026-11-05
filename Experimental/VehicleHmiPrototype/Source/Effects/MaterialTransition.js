//============================================================================================================================================
//                                                          MATERIALTRANSITION.JS
//============================================================================================================================================
// 🧩 Continuous 3D radial material transition (1.35 s ease-out cubic sphere to 6.4 m) from raycast drop point or nose fallback.
//
// 📝 Mirrors the reference configurator's makeScannable / RipRun behaviour 1:1 while feeding every ReSTIR GI stage
//    (DirectRadiance, RayTraversal, ReflectionTraversal, DisplayRecombination, ForwardRaster) through reserved trailing
//    records in the shared Materials storage buffer so no pipeline exceeds WebGPU's 8-storage-buffer stage ceiling.

import { EncodeMaterialRecords, MaterialRecordBytes } from "../MaterialSpecification.js";
import { InvertTransform } from "../TransformProjection.js";

export const PaintRippleDuration  = 1.35;                 // [s]   reference Truck.tsx paint ripple duration
export const PaintRippleMaxRadius = 6.4;                  // [m]   reference Truck.tsx paint ripple maximum sphere radius
export const PaintRippleBand      = 0.55;                 // [m]   smoothstep(uRipR - 0.55, uRipR, dist)
export const DefaultPaintOrigin   = Object.freeze([2.75, 0.0, 0.72]); // [m] nose-front fallback in Z-up body space

export function EaseOutCubic(Progress)
{
    const Clamped = Math.max(0.0, Math.min(1.0, Progress));
    const Inv = 1.0 - Clamped;
    return 1.0 - Inv * Inv * Inv;
}

export class MaterialTransition
{
    constructor(BaseMaterialCount, Upload)
    {
        this.PreviousSlot  = BaseMaterialCount;
        this.StateSlot     = BaseMaterialCount + 1;
        this.TransformSlot = BaseMaterialCount + 2;
        this.Upload        = Upload;

        this.Active        = false;
        this.Elapsed       = 0.0;
        this.Duration      = PaintRippleDuration;
        this.MaxRadius     = PaintRippleMaxRadius;
        this.Radius        = 0.0;
        this.Origin        = [...DefaultPaintOrigin];
        this.Previous      = null;
        this.Next          = null;
    }

    static ConstructInitialRecords(InitialBodyCoat)
    {
        const Previous = CloneMaterial(InitialBodyCoat);
        const IdleState = {
            Albedo: [...DefaultPaintOrigin],
            Roughness: 0.0,
            Emission: [0.0, 0.0, 0.0],
            Metalness: 0.0,
            CoatWeight: PaintRippleBand,
            CoatRoughness: 0.03,
            SpecularLevel: 0.04,
            Transmission: 0.0,
            IndexOfRefraction: 0.0
        };
        const IdentityRow = {
            Albedo: [1.0, 0.0, 0.0], Roughness: 0.0,
            Emission: [0.0, 1.0, 0.0], Metalness: 0.0,
            CoatWeight: 0.0, CoatRoughness: 0.0, SpecularLevel: 1.0, Transmission: 0.0,
            IndexOfRefraction: 0.0
        };
        return [Previous, IdleState, IdentityRow];
    }

    Begin(PreviousMaterial, NextMaterial, LocalOrigin = DefaultPaintOrigin, Duration = PaintRippleDuration, MaxRadius = PaintRippleMaxRadius)
    {
        // Commit any in-flight ripple cleanly if a rapid second selection arrives mid-wave.
        const Source = this.Active && this.Next ? CloneMaterial(this.Next) : CloneMaterial(PreviousMaterial);
        this.Previous  = Source;
        this.Next      = CloneMaterial(NextMaterial);
        this.Origin    = Array.isArray(LocalOrigin) && LocalOrigin.length === 3
                       ? [Number(LocalOrigin[0]) || 0.0, Number(LocalOrigin[1]) || 0.0, Number(LocalOrigin[2]) || 0.0]
                       : [...DefaultPaintOrigin];
        this.Duration  = Math.max(0.05, Number(Duration) || PaintRippleDuration);
        this.MaxRadius = Math.max(0.5, Number(MaxRadius) || PaintRippleMaxRadius);
        this.Elapsed   = 0.0;
        this.Radius    = 0.0;
        this.Active    = true;

        if (this.Upload)
        {
            this.Upload(this.PreviousSlot * MaterialRecordBytes, EncodeMaterialRecords([this.Previous]));
            this.UploadRippleState();
        }
    }

    Finish()
    {
        if (!this.Active)
            return;
        this.Active  = false;
        this.Elapsed = this.Duration;
        this.Radius  = this.MaxRadius;
        if (this.Next)
            this.Previous = CloneMaterial(this.Next);
        if (this.Upload)
            this.UploadRippleState();
    }

    Advance(Seconds, BodyWorldTransform = null)
    {
        const Step = Math.max(0.0, Math.min(0.1, Number(Seconds) || 0.0));
        if (BodyWorldTransform && this.Upload)
            this.UploadBodyTransform(BodyWorldTransform);
        if (!this.Active)
            return;
        this.Elapsed += Step;
        const Progress = Math.min(this.Elapsed / this.Duration, 1.0);
        this.Radius = EaseOutCubic(Progress) * this.MaxRadius;
        if (Progress >= 1.0)
        {
            this.Finish();
            return;
        }
        if (this.Upload)
            this.UploadRippleState();
    }

    EvaluateMixAtLocalPoint(LocalPoint)
    {
        if (!this.Active)
            return 1.0;
        const Dx = LocalPoint[0] - this.Origin[0];
        const Dy = LocalPoint[1] - this.Origin[1];
        const Dz = LocalPoint[2] - this.Origin[2];
        const Distance = Math.hypot(Dx, Dy, Dz);
        const Edge0 = this.Radius - PaintRippleBand;
        const Edge1 = this.Radius;
        const T = Math.max(0.0, Math.min(1.0, (Distance - Edge0) / Math.max(Edge1 - Edge0, 1.0e-5)));
        const Smooth = T * T * (3.0 - 2.0 * T);
        return 1.0 - Smooth;
    }

    UploadRippleState()
    {
        const Scalars = new Float32Array(16);
        Scalars[0] = this.Origin[0];
        Scalars[1] = this.Origin[1];
        Scalars[2] = this.Origin[2];
        Scalars[3] = this.Radius;
        Scalars[7] = this.Active ? 1.0 : 0.0;
        Scalars[8] = PaintRippleBand;
        this.Upload(this.StateSlot * MaterialRecordBytes, Scalars);
    }

    UploadBodyTransform(BodyWorldTransform)
    {
        const Inverse = InvertTransform(BodyWorldTransform);
        // Column-major mat4x4 -> 3 row vectors [R00, R01, R02, Tx], [R10, R11, R12, Ty], [R20, R21, R22, Tz].
        const Scalars = new Float32Array(16);
        Scalars[0]  = Inverse[0]; Scalars[1]  = Inverse[4]; Scalars[2]  = Inverse[8];  Scalars[3]  = Inverse[12];
        Scalars[4]  = Inverse[1]; Scalars[5]  = Inverse[5]; Scalars[6]  = Inverse[9];  Scalars[7]  = Inverse[13];
        Scalars[8]  = Inverse[2]; Scalars[9]  = Inverse[6]; Scalars[10] = Inverse[10]; Scalars[11] = Inverse[14];
        this.Upload(this.TransformSlot * MaterialRecordBytes, Scalars);
    }
}

function CloneMaterial(Material)
{
    return {
        Albedo: [...Material.Albedo],
        Roughness: Material.Roughness,
        Emission: [...(Material.Emission || [0.0, 0.0, 0.0])],
        Metalness: Material.Metalness ?? 0.0,
        CoatWeight: Material.CoatWeight ?? 0.0,
        CoatRoughness: Material.CoatRoughness ?? 0.03,
        SpecularLevel: Material.SpecularLevel ?? 0.04,
        Transmission: Material.Transmission ?? 0.0,
        IndexOfRefraction: Material.IndexOfRefraction ?? 0.0
    };
}
