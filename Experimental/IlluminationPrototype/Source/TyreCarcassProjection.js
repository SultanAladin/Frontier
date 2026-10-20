// Reduced-order radial XPBD carcass: compliant rim tethers, circumferential coupling,
// gas-volume compliance and unilateral road contact. Quasi-static like the strut equilibrium.
// This is a radial particle lattice, not a volumetric FEM or full 3D tyre model.
export const CarcassParticleCount = 32;
const Radius = 0.50855;
const RimRadius = 0.3448;
const RestVolume = 0.034;
const ProjectionSeconds = 1 / 120;
const MaximumCompression = 0.14;
const SampleCount = 29;
const Curves = new Map();

export class TyreCarcassProjection
{
    constructor(Pressure = 220000)
    {
        this.AssignPressure(Pressure);
        this.Compression = 0;
        this.Volume = RestVolume;
        this.RadialDisplacement = new Float64Array(CarcassParticleCount);
        this.Reaction = 0;
    }

    AssignPressure(Pressure)
    {
        if (!Number.isFinite(Pressure) || Pressure < 0) throw new Error("Invalid carcass pressure");
        // Curves are shared; coarse pressure bins keep thermal updates from rebuilding every substep.
        const Quantized = Math.round(Pressure / 5000) * 5000;
        if (this.Pressure === Quantized) return;
        this.Pressure = Quantized;
        if (!Curves.has(Quantized))
        {
            const Samples = Array.from({ length: SampleCount }, (_, Index) =>
                TyreCarcassProjection.ProjectCompression(Index * MaximumCompression / (SampleCount - 1), Quantized));
            // A convex response prevents the coupled suspension bisection from selecting a negative tangent.
            for (let Index = 1; Index < Samples.length; ++Index)
                Samples[Index].Reaction = Math.max(Samples[Index - 1].Reaction, Samples[Index].Reaction);
            Curves.set(Quantized, Samples);
            if (Curves.size > 64) Curves.delete(Curves.keys().next().value);
        }
        this.Curve = Curves.get(Quantized);
    }

    static ProjectCompression(Compression, Pressure, Iterations = 80)
    {
        if (!Number.isFinite(Compression) || Compression < 0 || !Number.isFinite(Pressure) || Pressure < 0) throw new Error("Invalid projection input");
        const Count = CarcassParticleCount;
        const Displacement = new Float64Array(Count);
        const Attachment = new Float64Array(Count);
        const Circumference = new Float64Array(Count);
        const Contact = new Float64Array(Count);
        const Cosines = Float64Array.from({ length: Count }, (_, Index) => Math.cos(Index * 2 * Math.PI / Count));
        const StepSquared = ProjectionSeconds ** 2;
        const RadialCompliance = 1 / ((4000 + 0.30 * Pressure) * StepSquared);
        const BeltCompliance = 1 / (45000 * StepSquared);
        const VolumeStiffness = 30000 + 2.4 * Pressure;
        const VolumeCompliance = 1 / (VolumeStiffness * StepSquared);
        let VolumeMultiplier = 0;
        for (let Iteration = 0; Iteration < Iterations; ++Iteration)
        {
            for (let Index = 0; Index < Count; ++Index)
            {
                // Δλ = (-C - α̃ λ) / (Σ w |∇C|² + α̃), α̃ = compliance / Δt².
                const Correction = (-Displacement[Index] - RadialCompliance * Attachment[Index]) / (1 + RadialCompliance);
                Attachment[Index] += Correction;
                Displacement[Index] += Correction;
            }
            for (let Index = 0; Index < Count; ++Index)
            {
                const Next = (Index + 1) % Count;
                const Constraint = Displacement[Index] - Displacement[Next];
                const Correction = (-Constraint - BeltCompliance * Circumference[Index]) / (2 + BeltCompliance);
                Circumference[Index] += Correction;
                Displacement[Index] += Correction;
                Displacement[Next] -= Correction;
            }
            // Linearized annular volume: mean inward radial displacement is its first-order gradient.
            const Mean = Displacement.reduce((Sum, Entry) => Sum + Entry, 0) / Count;
            const VolumeCorrection = (-Mean - VolumeCompliance * VolumeMultiplier) / (1 / Count + VolumeCompliance);
            VolumeMultiplier += VolumeCorrection;
            for (let Index = 0; Index < Count; ++Index) Displacement[Index] += VolumeCorrection / Count;
            for (let Index = 0; Index < Count; ++Index)
            {
                const Gradient = Cosines[Index];
                const Gap = Radius - Compression - (Radius - Displacement[Index]) * Gradient;
                if (Gradient <= 0 || (Gap >= 0 && Contact[Index] === 0)) continue;
                const Previous = Contact[Index];
                Contact[Index] = Math.max(0, Previous - Gap / (Gradient * Gradient));
                Displacement[Index] += (Contact[Index] - Previous) * Gradient;
            }
        }
        let Reaction = 0;
        for (let Index = 0; Index < Count; ++Index) Reaction += Contact[Index];
        Reaction /= StepSquared;
        const Mean = Displacement.reduce((Sum, Entry) => Sum + Entry, 0) / Count;
        const Volume = RestVolume * Math.max(0.3, 1 - 2 * Radius * Mean / (Radius * Radius - RimRadius * RimRadius));
        return { Reaction, Volume, RadialDisplacement: Displacement,
                 Multipliers: { Attachment, Circumference, Contact, Volume: VolumeMultiplier } };
    }

    EvaluateReaction(Compression)
    {
        const Coordinate = Math.max(0, Math.min(SampleCount - 1, Compression / MaximumCompression * (SampleCount - 1)));
        const Low = Math.min(SampleCount - 2, Math.floor(Coordinate));
        const Blend = Coordinate - Low;
        return this.Curve[Low].Reaction * (1 - Blend) + this.Curve[Low + 1].Reaction * Blend;
    }

    ResolveCompression(Compression)
    {
        this.Compression = Math.max(0, Math.min(MaximumCompression, Compression));
        const Coordinate = this.Compression / MaximumCompression * (SampleCount - 1);
        const Low = Math.min(SampleCount - 2, Math.floor(Coordinate));
        const Blend = Coordinate - Low;
        const A = this.Curve[Low], B = this.Curve[Low + 1];
        this.Reaction = this.EvaluateReaction(this.Compression);
        this.Volume = A.Volume * (1 - Blend) + B.Volume * Blend;
        for (let Index = 0; Index < CarcassParticleCount; ++Index)
            this.RadialDisplacement[Index] = A.RadialDisplacement[Index] * (1 - Blend) + B.RadialDisplacement[Index] * Blend;
        return this.Reaction;
    }
}
