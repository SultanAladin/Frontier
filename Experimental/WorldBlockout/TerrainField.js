/*==========================================================================================================================================
                                                        TERRAINFIELD.JS
==========================================================================================================================================*/
// 🧩 The planet-blockout terrain: one equirectangular height field, the vertical SDF it implies, authored continent and gulf
//    stamps, sculpt brushes, city pads, and the mesh extraction that turns the field into triangles at a chosen LOD.
'use strict';

const TerrainField = (() =>
{
    const MetresPerKilometre = 1000;

    //------------------------------------------------------------------------------------------------------------------------
    //                                                     THE PLANET
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 A planet record is small on purpose: radius, grid, sea level and the mesh spacing. Everything else is authored on
    //    the field — continents, gulfs, city pads — so the record can be written to TOML in a few dozen lines.
    function ConfigurePlanet()
    {
        return {
            Identity: { Name: 'Terra-01', Classification: 'Ocean world · blocking pass', Seed: 20261103 },
            RadiusKm: 6371,                 // [km]  - Mean radius; a real globe, not a plane
            Grid: { Width: 512, Height: 256 },   // [cells] - Equirectangular field, 0.703° per cell
            SeaLevel: 0,                    // [m]   - Datum; heights are metres above it
            Floor: -4800,                   // [m]   - Abyssal plain the field starts on
            Shelf: -110,                    // [m]   - Coastal shelf the stamps leave behind
            MeshSpacingKm: 180,             // [km]  - Vertex spacing for the coarse blocking mesh
            Ridge: { Amplitude: 900, WidthCells: 7, Octaves: 4 }
        };
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                      THE FIELD
    //------------------------------------------------------------------------------------------------------------------------

    function CreateField(Planet)
    {
        const Width = Planet.Grid.Width, Height = Planet.Grid.Height;
        const Heights = new Float32Array(Width * Height).fill(Planet.Floor);
        const Field = {
            Planet, Width, Height, Heights,
            Revision: 0,
            Continents: [],      // authored stamps, kept for the record and the outliner
            Cities: [],
            Undo: [],
            Cursor: { Lon: 20, Lat: 6 }
        };
        // 📝 One field object carries its own sampling API so the ocean, the mesh and the editor cannot drift apart on which
        //    helpers they call.
        Field.Sample = {
            Height: (Lon, Lat) => SampleHeight(Field, Lon, Lat),
            Distance: (Lon, Lat) => SampleDistance(Field, Lon, Lat),
            Slope: (Lon, Lat) => SampleSlope(Field, Lon, Lat),
            Band: Height => ResolveBand(Field, Height),
            Cell: (Lon, Lat) => ({ X: CellOfLon(Field, Lon), Y: CellOfLat(Field, Lat) }),
            Offset: (X, Y) => ({ Lon: LonOfCell(Field, X), Lat: LatOfCell(Field, Y) }),
            CellKm: Lat => KilometresPerCell(Field, Lat)
        };
        return Field;
    }

    // 📝 Grid, longitude and latitude are three views of the same cell: keeping the conversions in one place is what stops
    //    brushes from drifting when the field is resized.
    const IndexOf = (Field, X, Y) => (Y % Field.Height) * Field.Width + ((X % Field.Width) + Field.Width) % Field.Width;
    const LonOfCell = (Field, X) => (X / Field.Width) * 360 - 180;
    const LatOfCell = (Field, Y) => 90 - (Y / Field.Height) * 180;
    const CellOfLon = (Field, Lon) => (Lon + 180) / 360 * Field.Width;
    const CellOfLat = (Field, Lat) => (90 - Lat) / 180 * Field.Height;

    // 📝 One cell is wider than it is tall away from the equator; a brush given in kilometres must respect that.
    const KilometresPerCell = (Field, Lat) => (2 * Math.PI * Field.Planet.RadiusKm / Field.Width) * Math.cos(Lat * Math.PI / 180);

    function HeightOf(Field, X, Y)
    {
        return Field.Heights[IndexOf(Field, X, Y)];
    }

    function AssignHeight(Field, X, Y, Value)
    {
        Field.Heights[IndexOf(Field, X, Y)] = Math.max(Field.Planet.Floor - 900, Math.min(9400, Value));
    }

    function SampleHeight(Field, Lon, Lat)
    {
        const X = Math.min(Field.Width - 1.001, Math.max(0, CellOfLon(Field, Lon)));
        const Y = Math.min(Field.Height - 1.001, Math.max(0, CellOfLat(Field, Lat)));
        const X0 = Math.floor(X), Y0 = Math.floor(Y);
        const Fx = X - X0, Fy = Y - Y0;
        const A = HeightOf(Field, X0, Y0) * (1 - Fx) + HeightOf(Field, X0 + 1, Y0) * Fx;
        const B = HeightOf(Field, X0, Y0 + 1) * (1 - Fx) + HeightOf(Field, X0 + 1, Y0 + 1) * Fx;
        return A * (1 - Fy) + B * Fy;
    }

    // 📝 The signed distance the mesh sees is vertical: positive above the sea, negative below. It is exact for the height
    //    graph and is what the surface extraction and every "is this water" question read.
    function SampleDistance(Field, Lon, Lat)
    {
        return SampleHeight(Field, Lon, Lat) - Field.Planet.SeaLevel;
    }

    function SampleSlope(Field, Lon, Lat)
    {
        const Step = 0.35;
        const DX = (SampleHeight(Field, Lon + Step, Lat) - SampleHeight(Field, Lon - Step, Lat))
                 / (2 * Step * Math.PI / 180 * Field.Planet.RadiusKm * 1000 * Math.cos(Lat * Math.PI / 180));
        const DY = (SampleHeight(Field, Lon, Lat + Step) - SampleHeight(Field, Lon, Lat - Step))
                 / (2 * Step * Math.PI / 180 * Field.Planet.RadiusKm * 1000);
        return Math.atan(Math.hypot(DX, DY)) * 180 / Math.PI;
    }

    function ResolveBand(Field, Height)
    {
        const Planet = Field.Planet;
        if (Height < Planet.Floor + 1800) return 'Abyss';
        if (Height < Planet.Shelf) return 'Deep';
        if (Height < Planet.SeaLevel) return 'Shelf';
        if (Height < 250) return 'Coast';
        if (Height < 900) return 'Plain';
        if (Height < 2000) return 'Highland';
        return 'Peak';
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    VALUE NOISE
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Deterministic value noise: the record seeds it, so two editors with the same record sculpt the same planet.
    function Hash(X, Y, Seed)
    {
        let Value = (X * 374761393 + Y * 668265263 + Seed * 1442695041) | 0;
        Value = (Value ^ (Value >>> 13)) * 1274126177;
        return (((Value ^ (Value >>> 16)) >>> 0) % 10000) / 10000;
    }

    const Fade = T => T * T * (3 - 2 * T);

    function Noise(Field, X, Y)
    {
        const X0 = Math.floor(X), Y0 = Math.floor(Y);
        const Fx = Fade(X - X0), Fy = Fade(Y - Y0);
        const A = Hash(X0, Y0, Field.Planet.Identity.Seed), B = Hash(X0 + 1, Y0, Field.Planet.Identity.Seed);
        const C = Hash(X0, Y0 + 1, Field.Planet.Identity.Seed), D = Hash(X0 + 1, Y0 + 1, Field.Planet.Identity.Seed);
        return (A * (1 - Fx) + B * Fx) * (1 - Fy) + (C * (1 - Fx) + D * Fx) * Fy;
    }

    function RidgedNoise(Field, X, Y, Octaves)
    {
        let Total = 0, Amplitude = 0.5, Frequency = 1;
        for (let Octave = 0; Octave < Octaves; ++Octave)
        {
            const Value = 1 - Math.abs(Noise(Field, X * Frequency, Y * Frequency) * 2 - 1);
            Total += Value * Amplitude;
            Amplitude *= 0.5;
            Frequency *= 2.07;
        }
        return Total;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  GEOMETRY HELPERS
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Signed outline distance: a ray cast for inside/outside, then the shortest distance to any segment in kilometres, so
    //    a ramp follows the coast line and not just its corners.
    function ResolveOutlineDistance(Polygon, Lon, Lat)
    {
        const Cosine = Math.max(0.08, Math.cos(Lat * Math.PI / 180));
        let Inside = false, Minimum = Infinity;
        for (let Index = 0, Previous = Polygon.length - 1; Index < Polygon.length; Previous = Index++)
        {
            const [X1, Y1] = Polygon[Previous], [X2, Y2] = Polygon[Index];
            if ((Y1 > Lat) !== (Y2 > Lat) && Lon < (X2 - X1) * (Lat - Y1) / (Y2 - Y1) + X1) Inside = !Inside;
            const AX = (Lon - X1) * Cosine, AY = Lat - Y1;
            const BX = (Lon - X2) * Cosine, BY = Lat - Y2;
            const SpanX = BX - AX, SpanY = BY - AY;
            const Length = SpanX * SpanX + SpanY * SpanY;
            const T = Length < 1e-12 ? 0 : Math.max(0, Math.min(1, -(AX * SpanX + AY * SpanY) / Length));
            Minimum = Math.min(Minimum, Math.hypot(AX + SpanX * T, AY + SpanY * T));
        }
        return { Inside, DistanceKm: Minimum === Infinity ? 0 : Minimum * 111.32 };
    }

    const Smoothstep = T => T <= 0 ? 0 : T >= 1 ? 1 : T * T * (3 - 2 * T);

    // 📝 A stamp tests every cell against every outline segment; a bounding box with the ramp folded in throws away the
    //    cells that could not possibly be near the shape, which is most of the planet.
    function ResolveOutlineBounds(Polygon, RampKm)
    {
        let MinLon = Infinity, MaxLon = -Infinity, MinLat = Infinity, MaxLat = -Infinity;
        for (const [Lon, Lat] of Polygon)
        {
            MinLon = Math.min(MinLon, Lon); MaxLon = Math.max(MaxLon, Lon);
            MinLat = Math.min(MinLat, Lat); MaxLat = Math.max(MaxLat, Lat);
        }
        const Margin = RampKm / 111.32 + 2;
        return { MinLon: MinLon - Margin, MaxLon: MaxLon + Margin, MinLat: MinLat - Margin, MaxLat: MaxLat + Margin };
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                       STAMPS
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 The owner's ask: block out continents and gulfs at planet scale. A stamp is a polygon plus a plateau height, a ramp
    //    in kilometres and a ridge amplitude; it blends with max, so a second stamp never digs a hole in the first.
    function StampContinent(Field, Options)
    {
        const Record = {
            Kind: 'Continent', Name: Options.Name, Polygon: Options.Polygon,
            Plateau: Options.Plateau ?? 620, RampKm: Options.RampKm ?? 420,
            Ridge: Options.Ridge ?? 1, Relief: Options.Relief ?? 240, ShelfKm: Options.ShelfKm ?? 90
        };
        const Planet = Field.Planet;
        const Bounds = ResolveOutlineBounds(Record.Polygon, Record.RampKm);
        let Touched = 0, Peak = -Infinity;
        for (let Y = 0; Y < Field.Height; ++Y)
        {
            const Lat = LatOfCell(Field, Y);
            if (Lat < Bounds.MinLat || Lat > Bounds.MaxLat) continue;
            for (let X = 0; X < Field.Width; ++X)
            {
                const Lon = LonOfCell(Field, X);
                if (Lon < Bounds.MinLon || Lon > Bounds.MaxLon) continue;
                const Outline = ResolveOutlineDistance(Record.Polygon, Lon, Lat);
                if (!Outline.Inside && Outline.DistanceKm > Record.RampKm) continue;
                const Inland = Outline.Inside ? 1 : 1 - Smoothstep(Outline.DistanceKm / Record.RampKm);
                if (Inland <= 0.001) continue;
                // 📝 The shelf keeps the last ninety kilometres under water: a coast, not a wall.
                const Coastal = Smoothstep(1 - Math.min(1, Outline.DistanceKm / Record.ShelfKm));
                const Ridge = (RidgedNoise(Field, X / Planet.Ridge.WidthCells, Y / Planet.Ridge.WidthCells, Planet.Ridge.Octaves) - 0.42)
                    * Planet.Ridge.Amplitude * Record.Ridge;
                // 📝 Broad noise keeps a continent from being a billiard table: the blockout needs plains, not one altitude.
                const Relief = (Noise(Field, X / 11, Y / 11) - 0.5) * Record.Relief * Inland;
                const Plateau = Record.Plateau * Inland + Ridge * Inland + Relief;
                // 📝 Rise lets the interior stand above the coast instead of the coast standing above the interior.
                const Rise = Smoothstep(Math.min(1, Outline.DistanceKm / Math.max(80, Record.RampKm)));
                const PlateauHere = Record.Plateau * (0.35 + 0.65 * Rise) + Ridge * Rise + Relief * Rise;
                const Target = Outline.Inside ? Math.max(Planet.Shelf * (1 - Coastal), PlateauHere)
                                              : Planet.Floor + (Planet.Shelf - Planet.Floor) * (1 - Smoothstep(Outline.DistanceKm / Record.RampKm)) * 0.5;
                const Existing = Field.Heights[IndexOf(Field, X, Y)];
                const Value = Math.max(Existing, Target);
                if (Math.abs(Value - Existing) > 0.5)
                {
                    Field.Heights[IndexOf(Field, X, Y)] = Value;
                    ++Touched;
                    Peak = Math.max(Peak, Value);
                }
            }
        }
        Record.Touched = Touched;
        Record.Peak = Peak === -Infinity ? 0 : Peak;
        Field.Continents.push(Record);
        ++Field.Revision;
        return Record;
    }

    // 📝 A gulf is the same polygon walked the other way: the floor is pushed down to a shelf depth and the field keeps the
    //    lower of the two, so carving never leaves a lip behind.
    function CarveGulf(Field, Options)
    {
        const Record = {
            Kind: 'Gulf', Name: Options.Name, Polygon: Options.Polygon,
            Floor: Options.Floor ?? -140, RampKm: Options.RampKm ?? 120
        };
        const Bounds = ResolveOutlineBounds(Record.Polygon, Record.RampKm);
        let Touched = 0;
        for (let Y = 0; Y < Field.Height; ++Y)
        {
            const Lat = LatOfCell(Field, Y);
            if (Lat < Bounds.MinLat || Lat > Bounds.MaxLat) continue;
            for (let X = 0; X < Field.Width; ++X)
            {
                const Lon = LonOfCell(Field, X);
                if (Lon < Bounds.MinLon || Lon > Bounds.MaxLon) continue;
                const Outline = ResolveOutlineDistance(Record.Polygon, Lon, Lat);
                if (!Outline.Inside && Outline.DistanceKm > Record.RampKm) continue;
                const Share = Outline.Inside ? 1 : 1 - Smoothstep(Outline.DistanceKm / Record.RampKm);
                const Existing = Field.Heights[IndexOf(Field, X, Y)];
                const Value = Math.min(Existing, Record.Floor + (1 - Share) * 400);
                if (Math.abs(Value - Existing) > 0.5)
                {
                    Field.Heights[IndexOf(Field, X, Y)] = Value;
                    ++Touched;
                }
            }
        }
        Record.Touched = Touched;
        Field.Continents.push(Record);
        ++Field.Revision;
        return Record;
    }

    // 📝 An archipelago is a scatter of named discs: the same idea as a continent stamp with a radius instead of an outline.
    function StampArchipelago(Field, Options)
    {
        const Record = { Kind: 'Archipelago', Name: Options.Name, Islands: [] };
        const Count = Options.Count ?? 14;
        const Radius = Options.Radius ?? 900;
        let Touched = 0;
        for (let Index = 0; Index < Count; ++Index)
        {
            const Angle = Hash(Index, 1, Field.Planet.Identity.Seed) * Math.PI * 2;
            const Spread = Radius * Math.sqrt(Hash(Index, 2, Field.Planet.Identity.Seed));
            const Lon = Options.Lon + Math.cos(Angle) * Spread / 111.32;
            const Lat = Options.Lat + Math.sin(Angle) * Spread / 111.32;
            const SizeKm = (Options.IslandKm ?? 260) * (0.55 + Hash(Index, 3, Field.Planet.Identity.Seed) * 0.95);
            const Peak = (Options.Peak ?? 520) * (0.55 + Hash(Index, 4, Field.Planet.Identity.Seed) * 0.8);
            const CellRadius = SizeKm / Math.max(1, KilometresPerCell(Field, Lat));
            const CentreX = Math.round(CellOfLon(Field, Lon)), CentreY = Math.round(CellOfLat(Field, Lat));
            const Span = Math.ceil(CellRadius * 2.2);
            for (let OffsetY = -Span; OffsetY <= Span; ++OffsetY)
            {
                for (let OffsetX = -Span; OffsetX <= Span; ++OffsetX)
                {
                    if (CentreY + OffsetY < 0 || CentreY + OffsetY >= Field.Height) continue;
                    const Share = Math.hypot(OffsetX, OffsetY) / Math.max(1, CellRadius);
                    if (Share > 1) continue;
                    const Target = Peak * (1 - Smoothstep(Share)) + Field.Planet.Shelf * 0.2;
                    const Existing = Field.Heights[IndexOf(Field, CentreX + OffsetX, CentreY + OffsetY)];
                    if (Target > Existing + 1)
                    {
                        Field.Heights[IndexOf(Field, CentreX + OffsetX, CentreY + OffsetY)] = Target;
                        ++Touched;
                    }
                }
            }
            Record.Islands.push({ Lon, Lat, SizeKm, Peak });
        }
        Record.Touched = Touched;
        Field.Continents.push(Record);
        ++Field.Revision;
        return Record;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                       BRUSHES
    //------------------------------------------------------------------------------------------------------------------------

    const Falloff = (Share, Mode) => Mode === 'Linear' ? 1 - Share : Mode === 'Sharp' ? (1 - Share) ** 3
        : Smoothstep(1 - Share);

    // 📝 Flat terrain sculpting: the owner raises, lowers, flattens and smooths from the abyssal floor. A brush works in
    //    kilometres on the sphere, writes once, and leaves one undo entry per stroke, never one per pointer move.
    function ApplyBrush(Field, Options)
    {
        const RadiusKm = Math.max(1, Options.RadiusKm ?? 400);
        const Strength = Options.Strength ?? 0.5;
        const Mode = Options.Falloff ?? 'Smooth';
        const Tool = Options.Tool ?? 'Raise';
        const Lat = Options.Lat, Lon = Options.Lon;
        const CellKm = Math.max(0.1, KilometresPerCell(Field, Lat));
        const SpanX = Math.ceil(RadiusKm / CellKm);
        const SpanY = Math.ceil(RadiusKm / Math.max(0.1, 111.32));
        const CentreX = Math.round(CellOfLon(Field, Lon)), CentreY = Math.round(CellOfLat(Field, Lat));
        const Step = 0.34 * (Field.Planet.SeaLevel - Field.Planet.Floor) * Strength;
        const Snapshot = new Map();
        let Changed = 0;
        for (let OffsetY = -SpanY; OffsetY <= SpanY; ++OffsetY)
        {
            const Y = CentreY + OffsetY;
            if (Y < 0 || Y >= Field.Height) continue;
            for (let OffsetX = -SpanX; OffsetX <= SpanX; ++OffsetX)
            {
                const X = CentreX + OffsetX;
                const DistanceKm = Math.hypot(OffsetX * CellKm, OffsetY * 111.32);
                if (DistanceKm > RadiusKm) continue;
                const Share = Falloff(DistanceKm / RadiusKm, Mode);
                const Index = IndexOf(Field, X, Y);
                if (!Snapshot.has(Index)) Snapshot.set(Index, Field.Heights[Index]);
                const Existing = Field.Heights[Index];
                let Value = Existing;
                // 📝 A stroke moves a third of the planet's vertical range at full strength: from the abyssal floor to a
                //    coastline in three drags, which is the rate an author actually wants at this scale.
                if (Tool === 'Raise') Value = Existing + Step * Share;
                else if (Tool === 'Lower') Value = Existing - Step * Share;
                else if (Tool === 'Plateau')
                {
                    // 📝 A plateau is flat to three quarters of its radius, then blends: a shelf, not a dome.
                    const Flat = Smoothstep(Math.min(1, (1 - DistanceKm / RadiusKm) / 0.25));
                    Value = Existing + ((Options.Target ?? 240) - Existing) * Strength * Flat;
                }
                else if (Tool === 'Smooth')
                {
                    const Average = (HeightOf(Field, X - 1, Y) + HeightOf(Field, X + 1, Y)
                                   + HeightOf(Field, X, Y - 1) + HeightOf(Field, X, Y + 1)) / 4;
                    Value = Existing + (Average - Existing) * Strength * Share;
                }
                const Clamped = Math.max(Field.Planet.Floor - 900, Math.min(9400, Value));
                if (Math.abs(Clamped - Existing) > 0.01)
                {
                    Field.Heights[Index] = Clamped;
                    ++Changed;
                }
            }
        }
        if (Changed > 0)
        {
            Field.Undo.push(Snapshot);
            if (Field.Undo.length > 40) Field.Undo.shift();
            ++Field.Revision;
        }
        return Changed;
    }

    function UndoStroke(Field)
    {
        const Snapshot = Field.Undo.pop();
        if (!Snapshot) return 0;
        for (const [Index, Value] of Snapshot) Field.Heights[Index] = Value;
        ++Field.Revision;
        return Snapshot.size;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                        CITIES
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 "Even for cities I need the base first": a city is an anchor plus a flattened pad. The pad is placed on land, never
    //    in the sea and never on a cliff, and it is the ground a future city tile sits on.
    function ResolveCitySite(Field, Lon, Lat, RadiusKm)
    {
        const Height = SampleHeight(Field, Lon, Lat);
        const Slope = SampleSlope(Field, Lon, Lat);
        if (Height <= Field.Planet.SeaLevel + 4) return { Valid: false, Reason: 'Below the waterline', Height, Slope };
        if (Slope > 9) return { Valid: false, Reason: `Ground is too steep (${Slope.toFixed(1)}°)`, Height, Slope };
        const Edge = Math.min(RadiusKm / Math.max(1, KilometresPerCell(Field, Lat)), RadiusKm / 111.32);
        const Rim = Math.min(
            SampleHeight(Field, Lon + Edge, Lat), SampleHeight(Field, Lon - Edge, Lat),
            SampleHeight(Field, Lon, Lat + Edge), SampleHeight(Field, Lon, Lat - Edge));
        if (Rim <= Field.Planet.SeaLevel + 2) return { Valid: false, Reason: 'The pad would reach open water', Height, Slope };
        return { Valid: true, Reason: 'Dry ground, gentle slope', Height, Slope };
    }

    function PlaceCityPad(Field, Options)
    {
        const Site = ResolveCitySite(Field, Options.Lon, Options.Lat, Options.RadiusKm ?? 60);
        if (!Site.Valid) return { ...Site, City: null };
        const RadiusKm = Options.RadiusKm ?? 60;
        const Target = Math.round(Site.Height);
        const CellKm = Math.max(0.1, KilometresPerCell(Field, Options.Lat));
        const SpanX = Math.ceil(RadiusKm / CellKm);
        const SpanY = Math.ceil(RadiusKm / 111.32);
        const CentreX = Math.round(CellOfLon(Field, Options.Lon)), CentreY = Math.round(CellOfLat(Field, Options.Lat));
        const Snapshot = new Map();
        for (let OffsetY = -SpanY; OffsetY <= SpanY; ++OffsetY)
        {
            const Y = CentreY + OffsetY;
            if (Y < 0 || Y >= Field.Height) continue;
            for (let OffsetX = -SpanX; OffsetX <= SpanX; ++OffsetX)
            {
                const DistanceKm = Math.hypot(OffsetX * CellKm, OffsetY * 111.32);
                if (DistanceKm > RadiusKm) continue;
                const Index = IndexOf(Field, CentreX + OffsetX, Y);
                if (!Snapshot.has(Index)) Snapshot.set(Index, Field.Heights[Index]);
                // 📝 Flat inside three quarters of the pad, blended over the last quarter: a pad is a platform, not a dome.
                const Normalised = DistanceKm / RadiusKm;
                const Share = Normalised <= 0.75 ? 1 : Smoothstep((1 - Normalised) / 0.25);
                Field.Heights[Index] += (Target - Field.Heights[Index]) * Share;
            }
        }
        Field.Undo.push(Snapshot);
        const City = {
            Name: Options.Name ?? `City-${Field.Cities.length + 1}`,
            Lon: Options.Lon, Lat: Options.Lat, RadiusKm,
            Height: Target, AreaKm2: Math.round(Math.PI * RadiusKm * RadiusKm), Slope: Site.Slope
        };
        Field.Cities.push(City);
        ++Field.Revision;
        return { ...Site, City };
    }

    function RemoveCity(Field, Name)
    {
        const Index = Field.Cities.findIndex(City => City.Name === Name);
        if (Index < 0) return false;
        Field.Cities.splice(Index, 1);
        ++Field.Revision;
        return true;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    MESH EXTRACTION
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 The field is a height graph, so its iso-surface is a triangulation of that graph — watertight by construction. The
    //    spacing is in kilometres, which is how one record draws a blocking mesh and a close mesh from the same field.
    function ExtractMesh(Field, Options)
    {
        const SpacingKm = Math.max(1, Options.SpacingKm ?? Field.Planet.MeshSpacingKm);
        const Bounds = Options.Bounds ?? { LonMin: -180, LonMax: 180, LatMin: -90, LatMax: 90 };
        const Columns = Math.max(2, Math.round((Bounds.LonMax - Bounds.LonMin) / 360 * Field.Width
            * (KilometresPerCell(Field, 0) / SpacingKm)));
        const Rows = Math.max(2, Math.min(Field.Height * 2, Columns / 2));
        const Vertices = [], Indices = [];
        for (let Row = 0; Row <= Rows; ++Row)
        {
            const Lat = Bounds.LatMax - (Row / Rows) * (Bounds.LatMax - Bounds.LatMin);
            for (let Column = 0; Column <= Columns; ++Column)
            {
                const Lon = Bounds.LonMin + (Column / Columns) * (Bounds.LonMax - Bounds.LonMin);
                Vertices.push(Lon, Lat, SampleHeight(Field, Lon, Lat) / MetresPerKilometre);
            }
        }
        for (let Row = 0; Row < Rows; ++Row)
            for (let Column = 0; Column < Columns; ++Column)
            {
                const A = Row * (Columns + 1) + Column, B = A + 1, C = A + Columns + 1, D = C + 1;
                Indices.push(A, C, B, B, C, D);
            }
        return {
            Vertices: Float32Array.from(Vertices), Indices: Uint32Array.from(Indices),
            Columns, Rows, SpacingKm, Triangles: (Columns * Rows * 2), VerticesCount: (Columns + 1) * (Rows + 1)
        };
    }

    // 📝 Every interior edge of a triangulated graph belongs to exactly two triangles. That count is the watertightness proof
    //    a blockout mesh needs before anything is built on top of it.
    function ResolveManifold(Mesh)
    {
        const Indices = Mesh.Indices;
        const Counts = new Map();
        for (let Index = 0; Index < Indices.length; Index += 3)
        {
            const Tri = [Indices[Index], Indices[Index + 1], Indices[Index + 2]];
            for (let Edge = 0; Edge < 3; ++Edge)
            {
                const A = Tri[Edge], B = Tri[(Edge + 1) % 3];
                const Key = A < B ? `${A}:${B}` : `${B}:${A}`;
                Counts.set(Key, (Counts.get(Key) || 0) + 1);
            }
        }
        let Shared = 0, Open = 0, Overused = 0;
        for (const Count of Counts.values())
        {
            if (Count === 2) ++Shared;
            else if (Count === 1) ++Open;
            else ++Overused;
        }
        // 📝 A graph mesh is a sheet: it has a border, and every other edge is shared. That is the watertightness claim.
        const Boundary = 2 * (Mesh.Columns + Mesh.Rows);
        return {
            Edges: Counts.size, Shared, Open, Overused, Boundary,
            Watertight: Overused === 0 && Open === Boundary
        };
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    MEASUREMENTS
    //------------------------------------------------------------------------------------------------------------------------

    function ResolveCoverage(Field)
    {
        let Land = 0, Ocean = 0, Peak = -Infinity, Deepest = Infinity, Sum = 0;
        for (let Index = 0; Index < Field.Heights.length; ++Index)
        {
            const Height = Field.Heights[Index];
            Sum += Height;
            Peak = Math.max(Peak, Height);
            Deepest = Math.min(Deepest, Height);
            if (Height > Field.Planet.SeaLevel) ++Land; else ++Ocean;
        }
        const Total = Field.Heights.length;
        const CellKm2 = (2 * Math.PI * Field.Planet.RadiusKm) ** 2 / Total;
        return {
            LandShare: Land / Total, LandCells: Land, OceanCells: Ocean,
            LandKm2: Math.round(Land * CellKm2), OceanKm2: Math.round(Ocean * CellKm2),
            Peak, Deepest, Mean: Sum / Total, LandPercent: Land / Total * 100
        };
    }

    function EncodeRecord(Field)
    {
        const Planet = Field.Planet, Coverage = ResolveCoverage(Field);
        const Lines = ['# Terra-01 · world blockout record', '# proposed: new record', ''];
        Lines.push('[planet]');
        Lines.push(`name = "${Planet.Identity.Name}"`);
        Lines.push(`classification = "${Planet.Identity.Classification}"`);
        Lines.push(`seed = ${Planet.Identity.Seed}`);
        Lines.push(`radius_km = ${Planet.RadiusKm}`);
        Lines.push(`grid_width = ${Planet.Grid.Width}`);
        Lines.push(`grid_height = ${Planet.Grid.Height}`);
        Lines.push(`sea_level_m = ${Planet.SeaLevel}`);
        Lines.push(`floor_m = ${Planet.Floor}`);
        Lines.push(`mesh_spacing_km = ${Planet.MeshSpacingKm}`);
        Lines.push('');
        Lines.push('[planet.coverage]');
        Lines.push(`land_percent = ${Coverage.LandPercent.toFixed(1)}`);
        Lines.push(`land_km2 = ${Coverage.LandKm2}`);
        Lines.push(`peak_m = ${Math.round(Coverage.Peak)}`);
        Lines.push(`deepest_m = ${Math.round(Coverage.Deepest)}`);
        Lines.push('');
        Lines.push('[planet.stamps]');
        for (const Stamp of Field.Continents)
        {
            Lines.push(`[[planet.stamps.${Stamp.Kind.toLowerCase()}]]`);
            Lines.push(`name = "${Stamp.Name}"`);
            Lines.push(`vertices = ${Stamp.Polygon ? Stamp.Polygon.length : Stamp.Islands.length}`);
            if (Stamp.Plateau !== undefined) Lines.push(`plateau_m = ${Stamp.Plateau}`);
            if (Stamp.Floor !== undefined) Lines.push(`floor_m = ${Stamp.Floor}`);
            Lines.push(`touched_cells = ${Stamp.Touched}`);
        }
        Lines.push('');
        Lines.push('[planet.cities]');
        for (const City of Field.Cities)
        {
            Lines.push(`[[planet.cities.entry]]`);
            Lines.push(`name = "${City.Name}"`);
            Lines.push(`lon = ${City.Lon.toFixed(3)}`);
            Lines.push(`lat = ${City.Lat.toFixed(3)}`);
            Lines.push(`pad_radius_km = ${City.RadiusKm}`);
            Lines.push(`pad_height_m = ${City.Height}`);
            Lines.push(`area_km2 = ${City.AreaKm2}`);
        }
        return Lines.join('\n');
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                       EXPORTS
    //------------------------------------------------------------------------------------------------------------------------

    return {
        ConfigurePlanet, CreateField, IndexOf, LonOfCell, LatOfCell, CellOfLon, CellOfLat, KilometresPerCell,
        HeightOf, AssignHeight, SampleHeight, SampleDistance, SampleSlope, ResolveBand,
        Noise, RidgedNoise, ResolveOutlineDistance, Smoothstep,
        StampContinent, CarveGulf, StampArchipelago, ApplyBrush, UndoStroke,
        ResolveCitySite, PlaceCityPad, RemoveCity,
        ExtractMesh, ResolveManifold, ResolveCoverage, EncodeRecord,
        ContinentOutlines, GulfOutlines
    };

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  AUTHORED OUTLINES
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Coarse authored outlines in longitude/latitude, about 120 km of fidelity — enough to block out a continent, not a
    //    claim to be cartography. The editor can stamp any of them, or carve a gulf out of one.
    // 📝 Authored the way a blockout author would: a dozen to forty vertices per coast, read off a real atlas, enough that
    //    Africa reads as Africa at a glance and a district can be dropped on the right shoulder of it.
    function ContinentOutlines()
    {
        return {
            Africa: [
                [10.5, 37.3], [11.0, 33.5], [20.0, 32.5], [25.0, 31.6], [30.0, 31.2], [32.3, 31.2], [34.0, 28.0],
                [35.5, 23.5], [37.5, 21.0], [39.5, 15.0], [43.0, 12.5], [51.2, 11.8], [51.4, 10.4], [50.0, 5.0],
                [44.0, 2.0], [41.0, -2.0], [40.0, -7.0], [39.5, -10.0], [40.5, -15.0], [35.0, -20.0], [33.0, -25.0],
                [32.0, -28.0], [28.0, -33.0], [25.0, -34.0], [20.0, -34.6], [18.4, -34.2], [17.0, -33.0], [15.0, -27.0],
                [13.0, -23.0], [11.8, -17.0], [12.0, -9.0], [9.5, -1.0], [9.0, 3.0], [6.0, 4.5], [3.0, 6.2], [0.0, 5.6],
                [-4.0, 5.0], [-7.5, 4.4], [-11.0, 6.5], [-13.0, 9.0], [-16.0, 12.0], [-17.5, 15.0], [-16.5, 19.0],
                [-16.0, 21.0], [-13.0, 27.5], [-10.0, 30.0], [-9.8, 33.0], [-6.0, 35.8], [0.0, 36.2], [10.5, 37.3]
            ],
            Eurasia: [
                [-9.5, 36.5], [-9.0, 39.5], [-9.0, 43.5], [-4.0, 48.5], [0.0, 49.5], [4.0, 52.0], [9.0, 54.0],
                [8.0, 57.0], [5.0, 58.5], [10.0, 63.5], [15.0, 68.0], [25.0, 71.0], [35.0, 70.0], [45.0, 68.0],
                [60.0, 70.0], [75.0, 73.0], [90.0, 75.0], [105.0, 77.0], [120.0, 74.0], [140.0, 72.0], [160.0, 69.0],
                [170.0, 62.0], [160.0, 55.0], [155.0, 50.0], [140.0, 45.0], [130.0, 40.0], [122.0, 31.0], [120.0, 22.0],
                [108.0, 21.0], [105.0, 10.0], [100.0, 6.0], [97.0, 15.0], [90.0, 21.0], [80.0, 10.0], [75.0, 8.0],
                [70.0, 20.0], [65.0, 25.0], [58.0, 23.0], [52.0, 25.0], [48.0, 30.0], [43.0, 30.0], [38.0, 36.0],
                [30.0, 36.5], [26.0, 40.0], [20.0, 41.0], [14.0, 38.0], [12.0, 45.0], [3.0, 43.0], [-2.0, 37.0],
                [-9.5, 36.5]
            ],
            SouthAmerica: [
                [-79.0, 9.0], [-76.0, 12.0], [-66.0, 10.5], [-60.0, 8.0], [-52.0, 4.0], [-50.0, 0.0], [-45.0, -1.5],
                [-38.0, -5.0], [-35.0, -8.0], [-39.0, -13.0], [-42.0, -22.0], [-48.0, -25.0], [-52.0, -32.0],
                [-57.0, -35.0], [-58.0, -38.0], [-65.0, -45.0], [-68.0, -52.0], [-70.0, -55.0], [-75.0, -50.0],
                [-74.0, -45.0], [-73.0, -40.0], [-72.0, -30.0], [-70.0, -23.0], [-71.0, -18.0], [-75.0, -15.0],
                [-77.0, -10.0], [-80.0, -5.0], [-81.0, 0.0], [-79.0, 9.0]
            ],
            Australia: [
                [114.0, -21.5], [113.5, -26.0], [115.0, -30.0], [118.0, -34.5], [125.0, -32.5], [132.0, -32.0],
                [138.0, -35.0], [141.0, -38.0], [147.0, -38.5], [150.0, -35.0], [153.0, -28.0], [153.0, -25.0],
                [146.0, -19.0], [142.0, -11.0], [137.0, -12.0], [132.0, -11.0], [129.0, -15.0], [122.0, -17.0],
                [114.0, -21.5]
            ]
        };
    }

    function GulfOutlines()
    {
        return {
            PersianGulf: [[47.5, 29.5], [50.5, 27.0], [54.0, 24.5], [57.5, 25.5], [56.0, 27.5], [50.0, 30.0]],
            GulfOfGuinea: [[-4.0, 5.0], [4.0, 3.0], [9.0, 2.0], [9.5, -1.0], [4.0, -2.0], [-4.0, 0.5]],
            RedSea: [[34.5, 28.0], [38.0, 22.0], [43.0, 13.0], [44.5, 12.0], [40.0, 20.0], [36.0, 26.0]]
        };
    }
})();

if (typeof module !== 'undefined' && module.exports) module.exports = TerrainField;
