'use strict';
//----------------------------------------------------------------------------------------------------------------------------
//                                            Frontier · World Blockout · OceanField
//----------------------------------------------------------------------------------------------------------------------------
// 📝 What this file is: the 2.5D half of the water.
//
//    Two fields, one surface:
//      ① A Saint-Venant (shallow-water) solve on a local patch around the viewer. Real continuity and momentum on a grid:
//         surface elevation Eta, transport QX/QY on cell faces, wet-dry coupling to the terrain SDF, volume and energy
//         tracked every step. This is geometry and motion, not a normal-map trick.
//      ② An analytic directional spectrum for everything the patch cannot hold: nine wave trains, dispersive, evaluated at
//         any longitude/latitude and time. It drives the patch boundary (a wave maker) and stands in for the far field.
//
//    Honest limitation, stated once here and repeated in the README: Saint-Venant is non-dispersive, so at four kilometres
//    of water the celerity sqrt(g·h) is 198 m/s and the patch would only carry hundred-kilometre waves. The solve therefore
//    runs on a mixed-layer depth clamped by SolvableDepth, which keeps celerity in a believable band. Dispersion is the
//    spectral field's job. The blend between the two is what the resolution map in the editor is showing.
//
//    The Terrain field is the source of truth for where water is: depth is read from its signed distance, so a gulf carved
//    in TerrainField becomes shallow water here without either file knowing about the other's internals.
//----------------------------------------------------------------------------------------------------------------------------

const OceanField = (function ()
{
    const GRAVITY = 9.81;
    const DENSITY = 1025;
    const DEGREES_LAT_KM = 110.574;
    const DEGREES_LON_KM = 111.320;

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    THE OCEAN RECORD
    //------------------------------------------------------------------------------------------------------------------------

    function CreateOcean(Terrain, Options)
    {
        Options = Options || {};
        const Planet = Terrain.Planet;
        const Record = {
            Identity: { Name: Options.Name ?? `${Planet.Identity.Name}-Ocean`, Proposed: 'new record' },
            Terrain,
            Planet,
            Gravity: GRAVITY,
            Density: DENSITY,
            SeaLevel: Planet.SeaLevel,
            Damping: Options.Damping ?? 0.018,
            SolvableDepth: Options.SolvableDepth ?? 90,
            MaxDepth: Options.MaxDepth ?? 4800,
            MinWetDepth: Options.MinWetDepth ?? 0.4,
            SwellAmplitude: Options.SwellAmplitude ?? 1,
            // 📝 The blend has to finish inside the patch, or the solve would be replaced by the spectrum while it is still
            //    the highest-resolution thing on screen.
            NearBlendKm: Options.NearBlendKm ?? 0.6,
            FarBlendKm: Options.FarBlendKm ?? 0.78,
            Clock: 0,
            Waves: BuildWaves(Planet.Identity.Seed, Options.WaveCount ?? 9),
            Patch: null
        };
        Record.SwellHeight = Record.Waves.reduce((Total, Wave) => Total + Wave.AmplitudeM, 0);
        // 📝 The record carries its own closures: the mesh and the editor talk to the ocean, never to this module's internals.
        Record.SpectralHeight = (Lon, Lat, Time) => SpectralHeight(Record, Lon, Lat, Time ?? Record.Clock);
        Record.SpectralSlope = (Lon, Lat, Time) => SpectralSlope(Record, Lon, Lat, Time ?? Record.Clock);
        Record.BlendWeight = DistanceKm => BlendWeight(Record, DistanceKm);
        Record.PatchHeight = (Lon, Lat) => PatchHeight(Record.Patch, Lon, Lat);
        Record.SurfaceHeight = (Lon, Lat, Time) =>
            SurfaceHeight(Record, Lon, Lat, { Time: Time ?? Record.Clock });
        Record.Step = (DeltaSeconds, StepOptions) => StepPatch(Record, Record.Patch, DeltaSeconds, StepOptions);
        Record.Settle = (Seconds, SettleOptions) => SettlePatch(Record, Record.Patch, Seconds, SettleOptions);
        Record.Summary = () => ResolveSummary(Record);
        return Record;
    }

    // 📝 A deterministic sea state: nine trains, each one octave-ish shorter and a little weaker than the last, spread
    //    across directions so the surface never reads as one repeating sine.
    function BuildWaves(Seed, Count)
    {
        const Waves = [];
        for (let Index = 0; Index < Count; ++Index)
        {
            const Jitter = Fraction(Math.sin((Index + 1) * 12.9898 + Seed * 0.0001) * 43758.5453);
            const Spread = Fraction(Math.sin((Index + 1) * 78.233 + Seed * 0.0003) * 13579.2468);
            const Phase = Fraction(Math.sin((Index + 1) * 37.719 + Seed * 0.0007) * 24680.1357);
            Waves.push({
                WavelengthM: 42 * Math.pow(1.44, Index + Jitter * 0.35),
                AmplitudeM: 0.36 * Math.pow(0.86, Index) * (0.8 + Jitter * 0.5),
                Direction: 0.9 + Spread * 1.15,
                Phase: Phase * Math.PI * 2
            });
        }
        return Waves;
    }

    const Fraction = Value => Value - Math.floor(Value);

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    GRID GEOMETRY
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 The patch is a tangent-plane grid in kilometres around a focus point: uniform cells, uniform physics, and a cheap
    //    conversion back to longitude/latitude whenever the terrain has to be sampled.
    function DegreesLongitudePerKm(Lat)
    {
        return 1 / (DEGREES_LON_KM * Math.max(0.05, Math.cos(Lat * Math.PI / 180)));
    }

    function CellToLonLat(Patch, X, Y)
    {
        const EastKm = (X - Patch.Cells / 2 + 0.5) * Patch.CellKm;
        const NorthKm = (Patch.Cells / 2 - Y - 0.5) * Patch.CellKm;
        return {
            Lon: Patch.FocusLon + EastKm * DegreesLongitudePerKm(Patch.FocusLat),
            Lat: Patch.FocusLat + NorthKm / DEGREES_LAT_KM
        };
    }

    function LonLatToCell(Patch, Lon, Lat)
    {
        const EastKm = (Lon - Patch.FocusLon) / DegreesLongitudePerKm(Patch.FocusLat);
        const NorthKm = (Lat - Patch.FocusLat) * DEGREES_LAT_KM;
        return {
            X: EastKm / Patch.CellKm + Patch.Cells / 2 - 0.5,
            Y: Patch.Cells / 2 - NorthKm / Patch.CellKm - 0.5
        };
    }

    function CreatePatch(Ocean, Options)
    {
        Options = Options || {};
        const Cells = Options.Cells ?? 160;
        const ExtentKm = Options.ExtentKm ?? 2;
        const Patch = {
            Ocean, Cells, ExtentKm, CellKm: ExtentKm / Cells,
            FocusLon: Options.FocusLon ?? 0, FocusLat: Options.FocusLat ?? 0,
            Eta: new Float64Array(Cells * Cells),
            QX: new Float64Array((Cells + 1) * Cells),
            QY: new Float64Array(Cells * (Cells + 1)),
            Depth: new Float64Array(Cells * Cells),
            Wet: new Uint8Array(Cells * Cells),
            Probes: [],
            Steps: 0, Substeps: 0, Time: 0,
            MaxSpeed: 0, WetCells: 0, Deepest: Infinity, Shoalest: Infinity,
            Volume: 0, Energy: 0, PeakEta: 0, ForcedCells: 0
        };
        FillBathymetry(Ocean, Patch);
        return Patch;
    }

    // 📝 Water exists only where the terrain SDF says water exists. The solve reads the same field the mesh does.
    function FillBathymetry(Ocean, Patch)
    {
        const Cells = Patch.Cells;
        let WetCells = 0, Deepest = 0, Shoalest = Infinity;
        for (let Y = 0; Y < Cells; ++Y)
        {
            for (let X = 0; X < Cells; ++X)
            {
                const Location = CellToLonLat(Patch, X, Y);
                const Distance = Ocean.Terrain.Sample.Distance(Location.Lon, Location.Lat);
                const Depth = Math.max(0, Math.min(Ocean.MaxDepth, -Distance));
                const Index = Y * Cells + X;
                Patch.Depth[Index] = Depth;
                const Wet = Depth > Ocean.MinWetDepth ? 1 : 0;
                Patch.Wet[Index] = Wet;
                if (Wet)
                {
                    ++WetCells;
                    Deepest = Math.max(Deepest, Depth);
                    Shoalest = Math.min(Shoalest, Depth);
                }
            }
        }
        Patch.WetCells = WetCells;
        Patch.Deepest = WetCells ? Deepest : 0;
        Patch.Shoalest = WetCells ? Shoalest : 0;
        Patch.Volume = ResolveVolume(Patch);
        return Patch;
    }

    function ResolveVolume(Patch)
    {
        const Cells = Patch.Cells, Area = Math.pow(Patch.CellKm * 1000, 2);
        let Volume = 0;
        for (let Index = 0; Index < Cells * Cells; ++Index)
        {
            const Height = Patch.Wet[Index] ? Patch.Depth[Index] + Patch.Eta[Index] : 0;
            Volume += Math.max(0, Height) * Area;
        }
        return Volume;
    }

    function ResolveEnergy(Patch)
    {
        const Cells = Patch.Cells, Area = Math.pow(Patch.CellKm * 1000, 2);
        let Energy = 0;
        for (let Index = 0; Index < Cells * Cells; ++Index)
        {
            const Eta = Patch.Eta[Index];
            Energy += 0.5 * DENSITY * GRAVITY * Eta * Eta * Area;
        }
        Patch.Energy = Energy / 1e6;
        return Patch.Energy;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    THE 2.5D SOLVE
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Celerity uses the mixed-layer clamp, never the true four-kilometre depth: that is the whole reason the patch can
    //    carry waves at a believable speed. See the limitation at the top of the file.
    function ResolveCelerity(Ocean, Depth)
    {
        return Math.sqrt(GRAVITY * Math.min(Ocean.SolvableDepth, Math.max(0, Depth)));
    }

    function ResolveMaxCelerity(Ocean, Patch)
    {
        return ResolveCelerity(Ocean, Patch.Deepest) || 0.1;
    }

    function ResolveStableStep(Ocean, Patch)
    {
        // 📝 Courant-Friedrichs-Lewy: a wave may cross less than half a cell per substep.
        const Dx = Patch.CellKm * 1000;
        return 0.45 * Dx / Math.max(0.1, ResolveMaxCelerity(Ocean, Patch));
    }

    function StepPatch(Ocean, Patch, DeltaSeconds, Options)
    {
        Options = Options || {};
        const Stable = ResolveStableStep(Ocean, Patch);
        const Wanted = Math.max(0, DeltaSeconds);
        const Substeps = Math.max(1, Math.min(Options.MaxSubsteps ?? 96, Math.ceil(Wanted / Stable)));
        const Dt = Wanted > 0 ? Wanted / Substeps : Stable;
        let Peak = 0;
        for (let Step = 0; Step < Substeps; ++Step)
        {
            StepOnce(Ocean, Patch, Dt);
            Patch.Time += Dt;
            Ocean.Clock = Patch.Time;
            if ((Step & 3) === 0)
            {
                ApplySwell(Ocean, Patch, Patch.Time);
                Peak = Math.max(Peak, ResolvePeakEta(Patch));
            }
        }
        Patch.Steps += Substeps;
        Patch.Substeps = Substeps;
        Patch.PeakEta = Math.max(Patch.PeakEta * 0.98, Peak);
        Patch.MaxSpeed = ResolveMaxCelerity(Ocean, Patch);
        Patch.Volume = ResolveVolume(Patch);
        ResolveEnergy(Patch);
        ReadProbes(Ocean, Patch);
        return {
            Substeps, Dt, Stable, PeakEta: Patch.PeakEta, Volume: Patch.Volume,
            Energy: Patch.Energy, MaxSpeed: Patch.MaxSpeed
        };
    }

    // 📝 One substep, two passes, both over flat typed arrays:
    //      ① faces take momentum from the surface gradient (nothing if either side is dry);
    //      ② cells take elevation from the transport divergence.
    function StepOnce(Ocean, Patch, Dt)
    {
        const Cells = Patch.Cells;
        const Dx = Patch.CellKm * 1000;
        const { Eta, QX, QY, Depth, Wet } = Patch;
        const Damp = Math.max(0, 1 - Ocean.Damping * Dt);
        const CellHeight = Index => Wet[Index] ? Math.max(0, Depth[Index] + Eta[Index]) : 0;

        const Stride = Cells + 1;
        for (let Y = 0; Y < Cells; ++Y)
        {
            for (let X = 1; X < Cells; ++X)
            {
                const Face = Y * Stride + X;
                const Left = Y * Cells + X - 1, Right = Y * Cells + X;
                const Height = 0.5 * (CellHeight(Left) + CellHeight(Right));
                if (Height <= 0) { QX[Face] = 0; continue; }
                const Celerity = ResolveCelerity(Ocean, Height);
                QX[Face] = (QX[Face] - Celerity * Celerity * Dt * (Eta[Right] - Eta[Left]) / Dx) * Damp;
            }
            QX[Y * Stride] = 0;
            QX[Y * Stride + Cells] = 0;
        }

        for (let Y = 1; Y < Cells; ++Y)
        {
            for (let X = 0; X < Cells; ++X)
            {
                const Face = Y * Cells + X;
                const Below = (Y - 1) * Cells + X, Above = Y * Cells + X;
                const Height = 0.5 * (CellHeight(Below) + CellHeight(Above));
                if (Height <= 0) { QY[Face] = 0; continue; }
                const Celerity = ResolveCelerity(Ocean, Height);
                QY[Face] = (QY[Face] - Celerity * Celerity * Dt * (Eta[Above] - Eta[Below]) / Dx) * Damp;
            }
        }
        for (let X = 0; X < Cells; ++X)
        {
            QY[X] = 0;
            QY[Cells * Cells + X] = 0;
        }

        for (let Y = 0; Y < Cells; ++Y)
        {
            for (let X = 0; X < Cells; ++X)
            {
                const Index = Y * Cells + X;
                const Divergence = (QX[Y * Stride + X + 1] - QX[Y * Stride + X]) / Dx
                    + (QY[(Y + 1) * Cells + X] - QY[Y * Cells + X]) / Dx;
                let Value = Eta[Index] - Dt * Divergence;
                // 📝 Dry cells keep their elevation pinned to the bed, which is what a beach does at this resolution.
                const Floor = Wet[Index] ? -Depth[Index] : 0;
                if (Value < Floor) Value = Floor;
                Eta[Index] = Value;
            }
        }
    }

    function ResolvePeakEta(Patch)
    {
        let Peak = 0;
        for (let Index = 0; Index < Patch.Cells * Patch.Cells; ++Index)
        {
            const Magnitude = Math.abs(Patch.Eta[Index]);
            if (Magnitude > Peak) Peak = Magnitude;
        }
        return Peak;
    }

    // 📝 The patch boundary is a wave maker: a two-cell ring is eased toward the spectral field, so the solve is driven by
    //    the same sea state the far field shows. No boundary forcing means a damped puddle.
    function ApplySwell(Ocean, Patch, Time)
    {
        const Cells = Patch.Cells, Ring = 2, Amplitude = Ocean.SwellAmplitude;
        let Forced = 0;
        for (let Y = 0; Y < Cells; ++Y)
        {
            for (let X = 0; X < Cells; ++X)
            {
                const Edge = Math.min(X, Y, Cells - 1 - X, Cells - 1 - Y);
                if (Edge >= Ring) continue;
                const Index = Y * Cells + X;
                if (!Patch.Wet[Index]) continue;
                const Location = CellToLonLat(Patch, X, Y);
                const Target = SpectralHeight(Ocean, Location.Lon, Location.Lat, Time) * Amplitude;
                const Weight = 1 - Edge / Ring;
                Patch.Eta[Index] = Patch.Eta[Index] * (1 - Weight) + Target * Weight;
                ++Forced;
            }
        }
        Patch.ForcedCells = Forced;
    }

    // 📝 A wave needs time to cross the patch, so a fresh patch starts settled: run the solve forward without drawing it.
    function SettlePatch(Ocean, Patch, Seconds, Options)
    {
        Options = Options || {};
        const Stable = ResolveStableStep(Ocean, Patch);
        // 📝 Never stretch the step to fit a budget: the Courant limit is the limit, so a cap shortens the settled time instead.
        const Steps = Math.max(1, Math.min(Options.MaxSteps ?? 4000, Math.round(Seconds / Stable)));
        const Dt = Stable;
        let Peak = 0;
        for (let Step = 0; Step < Steps; ++Step)
        {
            StepOnce(Ocean, Patch, Dt);
            Patch.Time += Dt;
            ApplySwell(Ocean, Patch, Patch.Time);
            if ((Step & 15) === 0) Peak = Math.max(Peak, ResolvePeakEta(Patch));
        }
        Patch.Steps += Steps;
        Patch.Substeps = Steps;
        Ocean.Clock = Patch.Time;
        Patch.PeakEta = Math.max(Patch.PeakEta, Peak);
        Patch.MaxSpeed = ResolveMaxCelerity(Ocean, Patch);
        Patch.Volume = ResolveVolume(Patch);
        ResolveEnergy(Patch);
        ReadProbes(Ocean, Patch);
        return { Steps, Dt, PeakEta: Patch.PeakEta, Energy: Patch.Energy };
    }

    function ResetPatch(Ocean, Patch)
    {
        Patch.Eta.fill(0);
        Patch.QX.fill(0);
        Patch.QY.fill(0);
        Patch.Steps = 0;
        Patch.Time = 0;
        Patch.PeakEta = 0;
        Patch.Volume = ResolveVolume(Patch);
        Patch.Energy = 0;
        ReadProbes(Ocean, Patch);
        return Patch;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    PROBES
    //------------------------------------------------------------------------------------------------------------------------

    function AddProbe(Patch, Options)
    {
        const Cell = LonLatToCell(Patch, Options.Lon, Options.Lat);
        const Probe = {
            Name: Options.Name ?? `Probe ${Patch.Probes.length + 1}`,
            Lon: Options.Lon, Lat: Options.Lat,
            X: Math.round(Cell.X), Y: Math.round(Cell.Y),
            Samples: [], Height: 0, Wet: false
        };
        Patch.Probes.push(Probe);
        ReadProbes(Patch.Ocean, Patch);
        return Probe;
    }

    function ReadProbes(Ocean, Patch)
    {
        for (const Probe of Patch.Probes)
        {
            const Inside = Probe.X >= 0 && Probe.Y >= 0 && Probe.X < Patch.Cells && Probe.Y < Patch.Cells;
            const Index = Inside ? Probe.Y * Patch.Cells + Probe.X : -1;
            Probe.Wet = Inside && Patch.Wet[Index] === 1;
            Probe.Height = Inside ? Patch.Eta[Index] : 0;
            Probe.Samples.push(Probe.Height);
            if (Probe.Samples.length > 220) Probe.Samples.shift();
        }
        return Patch.Probes;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  THE SPECTRAL FAR FIELD
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Deep-water dispersion, evaluated not sampled: omega = sqrt(g·k). Positions are metres in a local plane, which is
    //    accurate enough beyond the patch and keeps the far field shader-friendly.
    function SpectralHeight(Ocean, Lon, Lat, Time)
    {
        const EastM = Lon * DEGREES_LON_KM * 1000;
        const NorthM = Lat * DEGREES_LAT_KM * 1000;
        let Height = 0;
        for (const Wave of Ocean.Waves)
        {
            const K = 2 * Math.PI / Wave.WavelengthM;
            const Omega = Math.sqrt(GRAVITY * K);
            const Along = Math.cos(Wave.Direction) * EastM + Math.sin(Wave.Direction) * NorthM;
            Height += Wave.AmplitudeM * Math.sin(K * Along - Omega * Time + Wave.Phase);
        }
        return Height;
    }

    function SpectralSlope(Ocean, Lon, Lat, Time)
    {
        const EastM = Lon * DEGREES_LON_KM * 1000;
        const NorthM = Lat * DEGREES_LAT_KM * 1000;
        let SlopeX = 0, SlopeY = 0;
        for (const Wave of Ocean.Waves)
        {
            const K = 2 * Math.PI / Wave.WavelengthM;
            const Omega = Math.sqrt(GRAVITY * K);
            const Along = Math.cos(Wave.Direction) * EastM + Math.sin(Wave.Direction) * NorthM;
            const Gradient = Wave.AmplitudeM * K * Math.cos(K * Along - Omega * Time + Wave.Phase);
            SlopeX += Gradient * Math.cos(Wave.Direction);
            SlopeY += Gradient * Math.sin(Wave.Direction);
        }
        return { SlopeX, SlopeY, Steepness: Math.hypot(SlopeX, SlopeY) };
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    THE BLEND
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Near the viewer the shadow of the solve is complete; past FarBlendKm the spectral field takes over. In between the
    //    two are merged by weight, which is also what hides the seam between the near grid and the far mesh.
    function BlendWeight(Ocean, DistanceKm)
    {
        const Span = Math.max(0.001, Ocean.FarBlendKm - Ocean.NearBlendKm);
        const T = (DistanceKm - Ocean.NearBlendKm) / Span;
        return T <= 0 ? 0 : T >= 1 ? 1 : T * T * (3 - 2 * T);
    }

    function PatchHeight(Patch, Lon, Lat)
    {
        const Cell = LonLatToCell(Patch, Lon, Lat);
        const X = Math.round(Cell.X), Y = Math.round(Cell.Y);
        if (X < 0 || Y < 0 || X >= Patch.Cells || Y >= Patch.Cells) return null;
        const Index = Y * Patch.Cells + X;
        return Patch.Wet[Index] ? Patch.Eta[Index] : null;
    }

    function SurfaceHeight(Ocean, Lon, Lat, Options)
    {
        Options = Options || {};
        const Focus = Options.Focus ?? (Ocean.Patch ? { Lon: Ocean.Patch.FocusLon, Lat: Ocean.Patch.FocusLat } : { Lon, Lat });
        const DistanceKm = Math.hypot((Lon - Focus.Lon) * DEGREES_LON_KM * Math.cos(Focus.Lat * Math.PI / 180),
            (Lat - Focus.Lat) * DEGREES_LAT_KM);
        const Weight = BlendWeight(Ocean, DistanceKm);
        const Far = SpectralHeight(Ocean, Lon, Lat, Options.Time ?? Ocean.Clock);
        if (Weight >= 1 || !Ocean.Patch) return { Height: Far, Weight: 1, DistanceKm };
        const Near = PatchHeight(Ocean.Patch, Lon, Lat);
        if (Near === null) return { Height: Far, Weight: 1, DistanceKm };
        return { Height: Near * (1 - Weight) + Far * Weight, Weight, DistanceKm };
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    REPORTING
    //------------------------------------------------------------------------------------------------------------------------

    function ResolveSummary(Ocean)
    {
        const Patch = Ocean.Patch;
        return {
            Clock: Ocean.Clock,
            SeaLevel: Ocean.SeaLevel,
            SwellHeight: Ocean.SwellHeight,
            Waves: Ocean.Waves.length,
            Patch: Patch ? {
                Cells: Patch.Cells, CellKm: Patch.CellKm, ExtentKm: Patch.ExtentKm,
                WetCells: Patch.WetCells, WetPercent: 100 * Patch.WetCells / (Patch.Cells * Patch.Cells),
                Deepest: Patch.Deepest, Shoalest: Patch.Shoalest,
                StableStep: ResolveStableStep(Ocean, Patch), Celerity: Patch.MaxSpeed,
                PeakEta: Patch.PeakEta, Volume: Patch.Volume, EnergyMJ: Patch.Energy,
                Substeps: Patch.Substeps, Steps: Patch.Steps, Probes: Patch.Probes.length
            } : null
        };
    }

    function EncodeRecord(Ocean)
    {
        const Lines = [];
        Lines.push('# Terra-01 · ocean record', '# proposed: new record', '');
        Lines.push('[ocean]');
        Lines.push(`name = "${Ocean.Identity.Name}"`);
        Lines.push(`sea_level_m = ${Ocean.SeaLevel}`);
        Lines.push(`gravity = ${Ocean.Gravity}`);
        Lines.push(`solver = "saint-venant 2.5D"`);
        Lines.push(`damping = ${Ocean.Damping}`);
        Lines.push(`solvable_depth_m = ${Ocean.SolvableDepth}`);
        Lines.push(`swell_amplitude = ${Ocean.SwellAmplitude}`);
        Lines.push(`near_blend_km = ${Ocean.NearBlendKm}`);
        Lines.push(`far_blend_km = ${Ocean.FarBlendKm}`);
        Lines.push('');
        Lines.push('[ocean.waves]');
        for (const Wave of Ocean.Waves)
        {
            Lines.push(`wave = { wavelength_m = ${Wave.WavelengthM.toFixed(2)}, amplitude_m = ${Wave.AmplitudeM.toFixed(3)}, `
                + `direction_deg = ${(Wave.Direction * 180 / Math.PI).toFixed(1)} }`);
        }
        return Lines.join('\n');
    }

    return {
        CreateOcean, CreatePatch, FillBathymetry, ResetPatch,
        StepPatch, SettlePatch, ResolveStableStep, ResolveCelerity, ResolveMaxCelerity,
        SpectralHeight, SpectralSlope, BlendWeight, SurfaceHeight, PatchHeight,
        ResolveVolume, ResolveEnergy, AddProbe, ReadProbes,
        CellToLonLat, LonLatToCell, ResolveSummary, EncodeRecord, GRAVITY
    };
})();

if (typeof module !== 'undefined' && module.exports) module.exports = OceanField;
