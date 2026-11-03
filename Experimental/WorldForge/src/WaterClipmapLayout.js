/*============================================================================================================================================
                                                     WATERCLIPMAPLAYOUT.JS
============================================================================================================================================*/
// 🧩 Camera-anchored nested simulation windows, toroidal texel addressing and Courant-limited temporal tiers

export function ConstructWaterConfiguration(Override = {})
{
    return {
        GridResolution: 256,            // [idx] - cells per ring edge
        RingCount: 7,                   // [-]   - nested simulation tiers
        InnerExtent: 1600,              // [m]   - finest ring side length
        Gravity: 9.81,                  // [m/s²]
        SeaLevel: 0,                    // [m]
        MaximumDepth: 4000,             // [m]   - bounds the Courant estimate
        CourantNumber: 0.24,            // [-]
        Damping: 0.055,                 // [1/s] - bed friction and viscous loss
        WindStrength: 0.42,             // [m/s²]
        WindDirection: [0.83, 0.56],    // [-]
        SwellAmplitude: 0.75,           // [m]
        SwellWavelength: 210,           // [m]
        DryDepth: 0.02,                 // [m]   - wet/dry threshold
        ...Override
    };
}

export function ResolvePositiveModulo(Dividend, Divisor)
{
    return ((Dividend % Divisor) + Divisor) % Divisor;
}

export function ResolveRingExtent(Configuration, Level)
{
    return Configuration.InnerExtent * Math.pow(2, Level);
}

export function ResolveWaveCelerity(Configuration)
{
    return Math.sqrt(Configuration.Gravity * Configuration.MaximumDepth);
}

// 📝 Every ring owns its own Courant-limited Δτ; coarse tiers advance more physical time per
//    invocation, which is the temporal counterpart of their coarser spatial sampling.
export function ResolveRingWindow(Configuration, Level, CameraX, CameraZ)
{
    const Resolution = Configuration.GridResolution;
    const Extent = ResolveRingExtent(Configuration, Level);
    const CellSpacing = Extent / Resolution;
    const OriginCellsX = Math.floor(CameraX / CellSpacing) - Math.floor(Resolution / 2);
    const OriginCellsZ = Math.floor(CameraZ / CellSpacing) - Math.floor(Resolution / 2);
    const WaveSpeed = Math.max(1, ResolveWaveCelerity(Configuration) + Configuration.WindStrength * 10);
    const DeltaTime = Configuration.CourantNumber * CellSpacing / WaveSpeed;
    return {
        Level,
        Extent,
        CellSpacing,
        OriginCellsX,
        OriginCellsZ,
        ShiftX: ResolvePositiveModulo(OriginCellsX, Resolution),
        ShiftZ: ResolvePositiveModulo(OriginCellsZ, Resolution),
        WorldOriginX: OriginCellsX * CellSpacing,
        WorldOriginZ: OriginCellsZ * CellSpacing,
        DeltaTime,
        CellCount: Resolution * Resolution
    };
}

export function ResolveWindows(Configuration, CameraX, CameraZ)
{
    const Windows = [];
    for (let Level = 0; Level < Configuration.RingCount; ++Level)
        Windows.push(ResolveRingWindow(Configuration, Level, CameraX, CameraZ));
    return Windows;
}

export function ComputeAdaptivityReport(Configuration, WorldExtent, Windows)
{
    const FinestSpacing = Windows[0].CellSpacing;
    const UniformCellEdge = WorldExtent / FinestSpacing;
    const UniformCells = UniformCellEdge * UniformCellEdge;
    const AdaptiveCells = Windows.reduce((Accumulator, Window) => Accumulator + Window.CellCount, 0);
    const CoarsestExtent = Windows[Windows.length - 1].Extent;
    return {
        FinestSpacing,
        CoarsestSpacing: Windows[Windows.length - 1].CellSpacing,
        CoarsestExtent,
        UniformCells,
        AdaptiveCells,
        SavingsFactor: UniformCells / AdaptiveCells,
        SimulatedSecondsPerFrame: Windows.reduce((Accumulator, Window) => Accumulator + Window.DeltaTime, 0),
        BytesPerStateField: Configuration.GridResolution * Configuration.GridResolution * 16
    };
}

// 📝 Spectral displacement is used only beyond the outermost simulated window; it is geometric
//    tessellation, never a scrolling normal or colour texture.
export function ConstructSpectralWaveSet(Override = {})
{
    return {
        Amplitude: 1.15,
        Wavelength: 340,
        Direction: [0.88, 0.47],
        Steepness: 0.62,
        HarmonicCount: 5,
        ...Override
    };
}
