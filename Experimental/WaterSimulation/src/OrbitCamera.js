/*====================================================================================================================================
                                                        ORBIT CAMERA.JS
====================================================================================================================================*/
// 🧩 Pointer-driven orbit camera with exponential distance, height clamp, and matrix projection

import { Mat4Multiply, Mat4Perspective, Mat4LookAt, Vec3Normalize } from './LinearAlgebra.js';

export class OrbitCamera
{
    constructor(CanvasElement, Descriptor = {})
    {
        this.Canvas        = CanvasElement;
        this.Yaw           = Descriptor.Yaw ?? 0.34;
        this.Pitch         = Descriptor.Pitch ?? 0.16;
        this.Distance      = Descriptor.Distance ?? 96;
        this.Target        = Descriptor.Target ? [...Descriptor.Target] : [0, 0.6, 0];
        this.MinDistance   = Descriptor.MinDistance ?? 9;
        this.MaxDistance   = Descriptor.MaxDistance ?? 620;
        this.MinPitch      = Descriptor.MinPitch ?? -0.05;
        this.MaxPitch      = Descriptor.MaxPitch ?? 1.28;
        this.FieldOfView   = Descriptor.FieldOfView ?? 0.95;
        this.NearPlane     = Descriptor.NearPlane ?? 0.35;
        this.FarPlane      = Descriptor.FarPlane ?? 4200;
        this.ViewMatrix    = new Float32Array(16);
        this.ProjectionMatrix = new Float32Array(16);
        this.ViewProjection   = new Float32Array(16);
        this.InverseViewProjection = new Float32Array(16);
        this.EyePosition   = [0, 0, 0];
        this.AutoOrbitRate = 0.0;
        this.PointerCategory = null;
        this.LastPointer = [0, 0];

        this.BindPointer();
    }

    BindPointer()
    {
        const Canvas = this.Canvas;

        Canvas.addEventListener('contextmenu', (Event) => Event.preventDefault());

        Canvas.addEventListener('pointerdown', (Event) =>
        {
            this.PointerCategory =
                (Event.button === 2 || Event.shiftKey) ? 'pan' : (Event.button === 1 ? 'pan' : 'orbit');
            this.LastPointer = [Event.clientX, Event.clientY];
            Canvas.setPointerCapture(Event.pointerId);
            Canvas.classList.add('Dragging');
        });

        Canvas.addEventListener('pointermove', (Event) =>
        {
            if (!this.PointerCategory)
            {
                return;
            }

            const DeltaX = Event.clientX - this.LastPointer[0];
            const DeltaY = Event.clientY - this.LastPointer[1];
            this.LastPointer = [Event.clientX, Event.clientY];

            if (this.PointerCategory === 'orbit')
            {
                this.Yaw   -= DeltaX * 0.0042;
                this.Pitch -= DeltaY * 0.0034;
                this.Pitch  = Math.min(this.MaxPitch, Math.max(this.MinPitch, this.Pitch));
                return;
            }

            const Scale = this.Distance * 0.0018;
            const Right = [Math.cos(this.Yaw), 0, -Math.sin(this.Yaw)];
            const Forward = [-Math.sin(this.Yaw), 0, -Math.cos(this.Yaw)];

            this.Target[0] -= Right[0] * DeltaX * Scale - Forward[0] * DeltaY * Scale;
            this.Target[2] -= Right[2] * DeltaX * Scale - Forward[2] * DeltaY * Scale;
        });

        const ReleasePointer = (Event) =>
        {
            this.PointerCategory = null;
            Canvas.classList.remove('Dragging');
            if (Canvas.hasPointerCapture(Event.pointerId))
            {
                Canvas.releasePointerCapture(Event.pointerId);
            }
        };

        Canvas.addEventListener('pointerup', ReleasePointer);
        Canvas.addEventListener('pointercancel', ReleasePointer);

        Canvas.addEventListener('wheel', (Event) =>
        {
            Event.preventDefault();
            const Factor = Math.exp(Event.deltaY * 0.0011);
            this.Distance = Math.min(this.MaxDistance, Math.max(this.MinDistance, this.Distance * Factor));
        }, { passive: false });
    }

    AddDistance(Delta)
    {
        this.Distance = Math.min(this.MaxDistance, Math.max(this.MinDistance, this.Distance + Delta));
    }

    Update(DeltaSeconds)
    {
        if (this.AutoOrbitRate !== 0)
        {
            this.Yaw += this.AutoOrbitRate * DeltaSeconds;
        }

        const CosPitch = Math.cos(this.Pitch);
        this.EyePosition =
        [
            this.Target[0] + Math.sin(this.Yaw) * CosPitch * this.Distance,
            this.Target[1] + Math.sin(this.Pitch) * this.Distance + 4.0,
            this.Target[2] + Math.cos(this.Yaw) * CosPitch * this.Distance
        ];

        if (this.EyePosition[1] < 0.8)
        {
            this.EyePosition[1] = 0.8;
        }

        Mat4LookAt(this.ViewMatrix, this.EyePosition, this.Target, [0, 1, 0]);
        Mat4Perspective(this.ProjectionMatrix, this.FieldOfView, this.Canvas.width / this.Canvas.height,
                        this.NearPlane, this.FarPlane);
        Mat4Multiply(this.ViewProjection, this.ProjectionMatrix, this.ViewMatrix);
        InvertMatrix4(this.InverseViewProjection, this.ViewProjection);
    }

    Direction()
    {
        return Vec3Normalize([this.Target[0] - this.EyePosition[0],
                              this.Target[1] - this.EyePosition[1],
                              this.Target[2] - this.EyePosition[2]]);
    }
}

// 💡 General 4x4 inverse; the water needs it to rebuild world positions from the depth buffer
export function InvertMatrix4(Destination, Source)
{
    const A00 = Source[0],  A01 = Source[1],  A02 = Source[2],  A03 = Source[3];
    const A10 = Source[4],  A11 = Source[5],  A12 = Source[6],  A13 = Source[7];
    const A20 = Source[8],  A21 = Source[9],  A22 = Source[10], A23 = Source[11];
    const A30 = Source[12], A31 = Source[13], A32 = Source[14], A33 = Source[15];

    const B00 = A00 * A11 - A01 * A10;
    const B01 = A00 * A12 - A02 * A10;
    const B02 = A00 * A13 - A03 * A10;
    const B03 = A01 * A12 - A02 * A11;
    const B04 = A01 * A13 - A03 * A11;
    const B05 = A02 * A13 - A03 * A12;
    const B06 = A20 * A31 - A21 * A30;
    const B07 = A20 * A32 - A22 * A30;
    const B08 = A20 * A33 - A23 * A30;
    const B09 = A21 * A32 - A22 * A31;
    const B10 = A21 * A33 - A23 * A31;
    const B11 = A22 * A33 - A23 * A32;

    let Determinant = B00 * B11 - B01 * B10 + B02 * B09 + B03 * B08 - B04 * B07 + B05 * B06;
    if (Math.abs(Determinant) < 1e-12)
    {
        Destination.set(Source);
        return Destination;
    }

    Determinant = 1.0 / Determinant;

    Destination[0]  = (A11 * B11 - A12 * B10 + A13 * B09) * Determinant;
    Destination[1]  = (A02 * B10 - A01 * B11 - A03 * B09) * Determinant;
    Destination[2]  = (A31 * B05 - A32 * B04 + A33 * B03) * Determinant;
    Destination[3]  = (A22 * B04 - A21 * B05 - A23 * B03) * Determinant;
    Destination[4]  = (A12 * B08 - A10 * B11 - A13 * B07) * Determinant;
    Destination[5]  = (A00 * B11 - A02 * B08 + A03 * B07) * Determinant;
    Destination[6]  = (A32 * B02 - A30 * B05 - A33 * B01) * Determinant;
    Destination[7]  = (A20 * B05 - A22 * B02 + A23 * B01) * Determinant;
    Destination[8]  = (A10 * B10 - A11 * B08 + A13 * B06) * Determinant;
    Destination[9]  = (A01 * B08 - A00 * B10 - A03 * B06) * Determinant;
    Destination[10] = (A30 * B04 - A31 * B02 + A33 * B00) * Determinant;
    Destination[11] = (A21 * B02 - A20 * B04 - A23 * B00) * Determinant;
    Destination[12] = (A11 * B07 - A10 * B09 - A12 * B06) * Determinant;
    Destination[13] = (A00 * B09 - A01 * B07 + A02 * B06) * Determinant;
    Destination[14] = (A31 * B01 - A30 * B03 - A32 * B00) * Determinant;
    Destination[15] = (A20 * B03 - A21 * B01 + A22 * B00) * Determinant;

    return Destination;
}
