/*============================================================================================================================================
                                                   SHALLOWWATERREFERENCE.JS
============================================================================================================================================*/
// 🧩 Depth-integrated 2.5D shallow-water reference solver: continuity flux, pressure gradient, advection, friction, wet/dry

export function ConstructReferenceBasin(Override = {})
{
    return {
        Resolution: 96,                 // [idx]
        CellSpacing: 25,                // [m]
        OriginX: 0,                     // [m]
        OriginZ: 0,                     // [m]
        Gravity: 9.81,                  // [m/s²]
        SeaLevel: 0,                    // [m]
        BedElevation: -100,             // [m] - uniform bed
        CourantNumber: 0.22,            // [-]
        Friction: 0.04,                 // [1/s]
        WindStrength: 0,                // [m/s²]
        WindDirection: [1, 0],          // [-]
        DryDepth: 0.02,                 // [m]
        ReflectiveBoundary: true,       // [-]
        ...Override
    };
}

export class ShallowWaterReference
{
    constructor(Configuration = ConstructReferenceBasin())
    {
        this.Configuration = Configuration;
        const CellCount = Configuration.Resolution * Configuration.Resolution;
        this.Eta = new Float64Array(CellCount);
        this.VelocityX = new Float64Array(CellCount);
        this.VelocityZ = new Float64Array(CellCount);
        this.Bed = new Float64Array(CellCount).fill(Configuration.BedElevation);
        this.NextEta = new Float64Array(CellCount);
        this.NextVelocityX = new Float64Array(CellCount);
        this.NextVelocityZ = new Float64Array(CellCount);
        this.ElapsedSeconds = 0;
        this.StepCount = 0;
    }

    get Resolution()
    {
        return this.Configuration.Resolution;
    }

    get CellSpacing()
    {
        return this.Configuration.CellSpacing;
    }

    ResolveIndex(Column, Row)
    {
        const Resolution = this.Configuration.Resolution;
        const Clamp = (Coordinate) => this.Configuration.ReflectiveBoundary
            ? Math.max(0, Math.min(Resolution - 1, Coordinate < 0 ? -Coordinate - 1 : Coordinate >= Resolution ? 2 * Resolution - Coordinate - 1 : Coordinate))
            : Math.max(0, Math.min(Resolution - 1, Coordinate));
        return Clamp(Row) * Resolution + Clamp(Column);
    }

    ResolveDepth(Index)
    {
        return Math.max(0, this.Configuration.SeaLevel - this.Bed[Index] + this.Eta[Index]);
    }

    ConfigureBed(Predicate)
    {
        const Resolution = this.Configuration.Resolution;
        for (let Row = 0; Row < Resolution; ++Row)
        {
            for (let Column = 0; Column < Resolution; ++Column)
            {
                const WorldX = this.Configuration.OriginX + (Column + 0.5) * this.CellSpacing;
                const WorldZ = this.Configuration.OriginZ + (Row + 0.5) * this.CellSpacing;
                const Index = Row * Resolution + Column;
                this.Bed[Index] = Predicate(WorldX, WorldZ, Index);
                this.Eta[Index] = Math.min(this.Eta[Index], Math.max(0, this.Bed[Index] - this.Configuration.SeaLevel));
            }
        }
    }

    InjectImpulse(WorldX, WorldZ, Amplitude, Radius)
    {
        const Resolution = this.Configuration.Resolution;
        for (let Row = 0; Row < Resolution; ++Row)
        {
            for (let Column = 0; Column < Resolution; ++Column)
            {
                const DeltaX = this.Configuration.OriginX + (Column + 0.5) * this.CellSpacing - WorldX;
                const DeltaZ = this.Configuration.OriginZ + (Row + 0.5) * this.CellSpacing - WorldZ;
                const DistanceSquared = DeltaX * DeltaX + DeltaZ * DeltaZ;
                if (DistanceSquared > Radius * Radius) continue;
                const Index = Row * Resolution + Column;
                if (this.ResolveDepth(Index) <= this.Configuration.DryDepth) continue;
                this.Eta[Index] += Amplitude * Math.exp(-DistanceSquared / (2 * (Radius * 0.4) ** 2));
            }
        }
    }

    // 📝 Lax–Friedrichs numerical flux: the symmetric part transports volume, the celerity term
    //    suppresses the collocated-grid checkerboard without adding artificial wave texture.
    ResolveFaceFlux(LeftIndex, RightIndex)
    {
        const Configuration = this.Configuration;
        const LeftDepth = this.ResolveDepth(LeftIndex);
        const RightDepth = this.ResolveDepth(RightIndex);
        if (LeftDepth <= Configuration.DryDepth && RightDepth <= Configuration.DryDepth) return 0;
        const Celerity = Math.sqrt(Configuration.Gravity * Math.max(Configuration.DryDepth, Math.max(LeftDepth, RightDepth)));
        return 0.5 * (LeftDepth * this.VelocityX[LeftIndex] + RightDepth * this.VelocityX[RightIndex])
            - 0.5 * Celerity * (this.Eta[RightIndex] - this.Eta[LeftIndex]);
    }

    ResolveFaceFluxZ(FrontIndex, BackIndex)
    {
        const Configuration = this.Configuration;
        const FrontDepth = this.ResolveDepth(FrontIndex);
        const BackDepth = this.ResolveDepth(BackIndex);
        if (FrontDepth <= Configuration.DryDepth && BackDepth <= Configuration.DryDepth) return 0;
        const Celerity = Math.sqrt(Configuration.Gravity * Math.max(Configuration.DryDepth, Math.max(FrontDepth, BackDepth)));
        return 0.5 * (FrontDepth * this.VelocityZ[FrontIndex] + BackDepth * this.VelocityZ[BackIndex])
            - 0.5 * Celerity * (this.Eta[BackIndex] - this.Eta[FrontIndex]);
    }

    IntegrateTimestep(ForcedDeltaTime = null)
    {
        const Configuration = this.Configuration;
        const Resolution = this.Configuration.Resolution;
        const Spacing = this.CellSpacing;
        let MinimumBed = Infinity;
        for (let Index = 0; Index < this.Bed.length; ++Index) MinimumBed = Math.min(MinimumBed, this.Bed[Index]);
        const MaximumDepth = Math.max(Configuration.DryDepth, Configuration.SeaLevel - MinimumBed);
        const Celerity = Math.sqrt(Configuration.Gravity * MaximumDepth);
        const Δτ = ForcedDeltaTime ?? Configuration.CourantNumber * Spacing / Math.max(1, Celerity);
        const Viscosity = 0.11 * Celerity * Spacing;

        for (let Row = 0; Row < Resolution; ++Row)
        {
            for (let Column = 0; Column < Resolution; ++Column)
            {
                const Index = Row * Resolution + Column;
                const Depth = this.ResolveDepth(Index);
                const WestIndex = this.ResolveIndex(Column - 1, Row);
                const EastIndex = this.ResolveIndex(Column + 1, Row);
                const SouthIndex = this.ResolveIndex(Column, Row - 1);
                const NorthIndex = this.ResolveIndex(Column, Row + 1);

                if (Depth > Configuration.DryDepth)
                {
                    const GradientX = (this.Eta[EastIndex] - this.Eta[WestIndex]) / (2 * Spacing);
                    const GradientZ = (this.Eta[NorthIndex] - this.Eta[SouthIndex]) / (2 * Spacing);
                    const 𝑢 = this.VelocityX[Index];
                    const 𝑣 = this.VelocityZ[Index];
                    const AdvectionX = 𝑢 > 0 ? (𝑢 - this.VelocityX[WestIndex]) / Spacing : (this.VelocityX[EastIndex] - 𝑢) / Spacing;
                    const AdvectionZ = 𝑣 > 0 ? (𝑣 - this.VelocityZ[SouthIndex]) / Spacing : (this.VelocityZ[NorthIndex] - 𝑣) / Spacing;
                    const CrossX = 𝑣 > 0 ? (𝑢 - this.VelocityX[SouthIndex]) / Spacing : (this.VelocityX[NorthIndex] - 𝑢) / Spacing;
                    const CrossZ = 𝑢 > 0 ? (𝑣 - this.VelocityZ[WestIndex]) / Spacing : (this.VelocityZ[EastIndex] - 𝑣) / Spacing;
                    const DiffusionX = (this.VelocityX[EastIndex] + this.VelocityX[WestIndex] - 2 * 𝑢) / (Spacing * Spacing);
                    const DiffusionZ = (this.VelocityZ[NorthIndex] + this.VelocityZ[SouthIndex] - 2 * 𝑣) / (Spacing * Spacing);
                    const DepthFriction = Configuration.Friction * (1 + 12 * Configuration.DryDepth / Math.max(Depth, Configuration.DryDepth));
                    this.NextVelocityX[Index] = 𝑢 + Δτ * (
                        -Configuration.Gravity * GradientX - 𝑢 * AdvectionX - 𝑣 * CrossX
                        - DepthFriction * 𝑢 + Configuration.WindStrength * Configuration.WindDirection[0]
                        + Viscosity * DiffusionX);
                    this.NextVelocityZ[Index] = 𝑣 + Δτ * (
                        -Configuration.Gravity * GradientZ - 𝑢 * CrossZ - 𝑣 * AdvectionZ
                        - DepthFriction * 𝑣 + Configuration.WindStrength * Configuration.WindDirection[1]
                        + Viscosity * DiffusionZ);
                }
                else
                {
                    this.NextVelocityX[Index] = 0;
                    this.NextVelocityZ[Index] = 0;
                }

                const BoundaryWest = Column === 0 && Configuration.ReflectiveBoundary;
                const BoundaryEast = Column === Resolution - 1 && Configuration.ReflectiveBoundary;
                const BoundarySouth = Row === 0 && Configuration.ReflectiveBoundary;
                const BoundaryNorth = Row === Resolution - 1 && Configuration.ReflectiveBoundary;
                const FluxWest = BoundaryWest ? 0 : this.ResolveFaceFlux(WestIndex, Index);
                const FluxEast = BoundaryEast ? 0 : this.ResolveFaceFlux(Index, EastIndex);
                const FluxSouth = BoundarySouth ? 0 : this.ResolveFaceFluxZ(SouthIndex, Index);
                const FluxNorth = BoundaryNorth ? 0 : this.ResolveFaceFluxZ(Index, NorthIndex);
                const NextEta = this.Eta[Index] - Δτ * ((FluxEast - FluxWest) + (FluxNorth - FluxSouth)) / Spacing;
                this.NextEta[Index] = Math.max(-MaximumDepth, Math.min(MaximumDepth, NextEta));
            }
        }

        [this.Eta, this.NextEta] = [this.NextEta, this.Eta];
        [this.VelocityX, this.NextVelocityX] = [this.NextVelocityX, this.VelocityX];
        [this.VelocityZ, this.NextVelocityZ] = [this.NextVelocityZ, this.VelocityZ];
        this.ElapsedSeconds += Δτ;
        ++this.StepCount;
        return Δτ;
    }

    ComputeDiagnostics()
    {
        const Configuration = this.Configuration;
        let Volume = 0;
        let Energy = 0;
        let WetCells = 0;
        let MinimumEta = Infinity;
        let MaximumEta = -Infinity;
        let MaximumVelocity = 0;
        for (let Index = 0; Index < this.Eta.length; ++Index)
        {
            const Depth = this.ResolveDepth(Index);
            if (Depth > Configuration.DryDepth) ++WetCells;
            Volume += Depth * this.CellSpacing * this.CellSpacing;
            Energy += 0.5 * Depth * (this.VelocityX[Index] ** 2 + this.VelocityZ[Index] ** 2)
                + 0.5 * Configuration.Gravity * this.Eta[Index] ** 2;
            MinimumEta = Math.min(MinimumEta, this.Eta[Index]);
            MaximumEta = Math.max(MaximumEta, this.Eta[Index]);
            MaximumVelocity = Math.max(MaximumVelocity, Math.hypot(this.VelocityX[Index], this.VelocityZ[Index]));
        }
        return { Volume, Energy, WetCells, MinimumEta, MaximumEta, MaximumVelocity, ElapsedSeconds: this.ElapsedSeconds, StepCount: this.StepCount };
    }
}
