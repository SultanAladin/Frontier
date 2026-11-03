/*====================================================================================================
                                      WATERSIMULATION.JS
====================================================================================================*/
// 🧩 2.5D height-field water simulation: a stateful wave equation on a regular grid.
// This is a real simulation (height + previous height + damping + propagation), not a
// looping wave texture. State lives in typed arrays; the renderer samples the height
// grid as a displacement texture each frame.

// [m] - Simulation cell spacing. 256 cells x 32 m = 8 km, matching the world span.
const SimCellCount = 256;
const SimCellSizeM = 32.0;
// [m/s] - Shallow-water wave propagation speed c = sqrt(g * depth).
const WaveSpeedMps = 55.0;
// [-] - Energy damping per second.
const DampingPerSecond = 0.20;
// [-] - Explicit Euler stability limit: c * dt / dx <= 1 / sqrt(2).
const StabilityLimit = 1.0 / Math.SQRT2;

class WaterSimulation
{
    constructor(CellCount = SimCellCount, CellSizeM = SimCellSizeM)
    {
        this.CellCount = CellCount;
        this.CellSizeM = CellSizeM;
        this.WorldHalfSideM = (CellCount * CellSizeM) / 2.0;
        this.MaxDtSec = CellSizeM / (WaveSpeedMps * Math.SQRT2);
        this.CurrentHeight = new Float32Array(CellCount * CellCount);
        this.PreviousHeight = new Float32Array(CellCount * CellCount);
        this.NextHeight = new Float32Array(CellCount * CellCount);
        this.Dirty = true;
    }

    // [idx] - Cell coordinates for a world position.
    WorldToCell(WorldX, WorldZ)
    {
        const Cx = (WorldX + this.WorldHalfSideM) / this.CellSizeM;
        const Cz = (this.WorldHalfSideM - WorldZ) / this.CellSizeM;
        return [Cx, Cz];
    }

    // Reset the simulation to a flat surface.
    Reset()
    {
        this.CurrentHeight.fill(0.0);
        this.PreviousHeight.fill(0.0);
        this.NextHeight.fill(0.0);
        this.Dirty = true;
    }

    // Add a Gaussian splash source (click impact, raindrop, or brush dipping below water).
    // StrengthM is the peak surface displacement in metres.
    AddSplash(WorldX, WorldZ, RadiusM, StrengthM)
    {
        const Cell = this.WorldToCell(WorldX, WorldZ);
        const CenterCx = Cell[0];
        const CenterCz = Cell[1];
        const RadiusC = RadiusM / this.CellSizeM;
        const X0 = Math.max(0, Math.floor(CenterCx - RadiusC));
        const X1 = Math.min(this.CellCount - 1, Math.ceil(CenterCx + RadiusC));
        const Z0 = Math.max(0, Math.floor(CenterCz - RadiusC));
        const Z1 = Math.min(this.CellCount - 1, Math.ceil(CenterCz + RadiusC));
        const InvTwoR2 = 1.0 / (2.0 * RadiusC * RadiusC);
        for (let Cz = Z0; Cz <= Z1; Cz++)
        {
            for (let Cx = X0; Cx <= X1; Cx++)
            {
                const Dx = Cx - CenterCx;
                const Dz = Cz - CenterCz;
                const D2 = Dx * Dx + Dz * Dz;
                if (D2 > RadiusC * RadiusC) continue;
                const Idx = Cz * this.CellCount + Cx;
                const Displacement = StrengthM * Math.exp(-D2 * InvTwoR2);
                this.CurrentHeight[Idx] += Displacement;
                // Zero initial velocity: the previous state carries the same displacement
                // so the first explicit step is stable (no spurious 2*h injection).
                this.PreviousHeight[Idx] += Displacement;
            }
        }
        this.Dirty = true;
    }

    // Advance the simulation by DeltaTauSec seconds with sub-stepping for stability.
    // ∂²h/∂t² = c² ∇²h − γ ∂h/∂t, integrated as
    // h(t+dt) = 2h(t) − h(t−dt) + (c dt / dx)² ∇²h − γ (h(t) − h(t−dt))
    Step(DeltaTauSec)
    {
        if (DeltaTauSec <= 0.0) return;
        const Steps = Math.ceil(DeltaTauSec / this.MaxDtSec);
        const Dt = DeltaTauSec / Steps;
        const Coef2 = (WaveSpeedMps * Dt / this.CellSizeM) ** 2;
        const Damping = DampingPerSecond * Dt;
        const Size = this.CellCount;
        const Last = Size - 1;

        let Current = this.CurrentHeight;
        let Previous = this.PreviousHeight;
        let Next = this.NextHeight;

        for (let S = 0; S < Steps; S++)
        {
            for (let Cz = 1; Cz < Last; Cz++)
            {
                const Row = Cz * Size;
                const RowUp = Row + Size;
                const RowDown = Row - Size;
                for (let Cx = 1; Cx < Last; Cx++)
                {
                    const Idx = Row + Cx;
                    const Laplacian =
                        Current[Idx + 1] + Current[Idx - 1] +
                        Current[RowUp + Cx] + Current[RowDown + Cx] -
                        4.0 * Current[Idx];
                    const Propagation = Coef2 * Laplacian;
                    const Drag = Damping * (Current[Idx] - Previous[Idx]);
                    Next[Idx] = 2.0 * Current[Idx] - Previous[Idx] + Propagation - Drag;
                }
            }
            // [-] - Zero-gradient (reflecting) boundary: waves bounce at the world edge.
            for (let Cx = 0; Cx < Size; Cx++)
            {
                Next[Cx] = Next[Size + Cx];
                Next[Last * Size + Cx] = Next[(Last - 1) * Size + Cx];
            }
            for (let Cz = 0; Cz < Size; Cz++)
            {
                Next[Cz * Size] = Next[Cz * Size + 1];
                Next[Cz * Size + Last] = Next[Cz * Size + Last - 1];
            }
            // Rotate buffers: Previous <- Current <- Next <- (old Previous, reused).
            const OldPrevious = Previous;
            Previous = Current;
            Current = Next;
            Next = OldPrevious;
        }
        this.CurrentHeight = Current;
        this.PreviousHeight = Previous;
        this.NextHeight = Next;
        this.Dirty = true;
    }

    // [Float32Array] - Current surface displacement, length CellCount² (R channel of the
    // displacement texture uploaded to the GPU each frame).
    HeightData()
    {
        return this.CurrentHeight;
    }
}

function RowOffset(Cx, Cz, Size) { return Cz * Size + Cx; }

export { WaterSimulation, SimCellCount, SimCellSizeM, WaveSpeedMps, DampingPerSecond, StabilityLimit };
