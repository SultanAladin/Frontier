//----------------------------------------------------------------------------------------------------------------------------
//                                        Frontier · World Blockout · Renderer3D
//----------------------------------------------------------------------------------------------------------------------------
// 📝 What this file is: a software 3D viewport, so the blockout can be looked at rather than read.
//
//    Two layers over the same sphere, both sampled on a disc centred under the camera:
//      ① the terrain, resampled from the height field and shaded by height and surface normal;
//      ② the water, resampled from the ocean's own surface — the solve where it exists, the spectrum where it does not.
//
//    Three decisions carry the file:
//      · the disc. Rings crowd towards the eye and stretch to the horizon, so a three-kilometre view resolves metres and the
//        sea still meets the sky. A lat/lon rectangle cannot do both at once.
//      · height, not depth. The ground records the height of whatever landed on a pixel and the sea is drawn only where that
//        height is under the waterline. Ordered by depth alone, two kilometre-wide triangulations of nearly parallel
//        surfaces cross inside a pixel and the far sea comes back combed with wedges of seabed.
//      · two exaggerations. Land relief is drawn 4–6×, water 12–30×, and shading measures the unexaggerated surface, so a
//        calm sea does not shade like static.
//
//    No WebGL, no library, no canvas: triangles are rasterised into the same RGB raster and deflated PNG that draws the maps,
//    so a figure in the evidence folder and the viewport in the page are the same pixels. A 640 × 400 frame of thirty
//    thousand triangles costs about a tenth of a second, and nothing renders unless it is asked to.
//----------------------------------------------------------------------------------------------------------------------------

'use strict';
//----------------------------------------------------------------------------------------------------------------------------
//                                        Frontier · World Blockout · Renderer3D
//----------------------------------------------------------------------------------------------------------------------------
// 📝 What this file is: a software 3D viewport, so the blockout can be looked at rather than read.
//
//    Two layers over the same sphere:
//      ① the terrain, sampled from the height field into a spherical cap mesh and shaded by band and face normal;
//      ② the ocean, taken straight from the adaptive mesh — the same vertices the plan produced — drawn with a z-buffer and
//         alpha blended over the seabed, so the water reads as water and the tiers can be painted when asked.
//
//    No WebGL, no library, no canvas: triangles are rasterised into the same RGB raster and deflated PNG that draws the maps,
//    so a figure in the evidence folder and the viewport in the page are the same pixels. It is CPU work, and it is sized
//    for that: the viewport asks the ocean for a twenty-four thousand triangle plan, not the hundred-and-twenty thousand the
//    resolution map is drawn from.
//----------------------------------------------------------------------------------------------------------------------------

const Renderer3D = (function ()
{
    const RasterApi = typeof Raster !== 'undefined' ? Raster
        : (typeof require === 'function' ? require('./Raster.js') : null);

    const DEFAULTS = {
        Width: 720, Height: 420, FieldOfViewDeg: 42,
        TargetLon: 12, TargetLat: 6, DistanceKm: 3200,
        AzimuthDeg: 18, ElevationDeg: 26, AltitudeKm: 0, Relief: 4, WaveScale: 14,
        LightAzimuthDeg: 305, LightElevationDeg: 42, NearKm: 1
    };

    function CreateCamera(Options)
    {
        const Settings = Object.assign({}, DEFAULTS, Options || {});
        return {
            Width: Settings.Width, Height: Settings.Height,
            TargetLon: Settings.TargetLon, TargetLat: Settings.TargetLat,
            DistanceKm: Settings.DistanceKm, AzimuthDeg: Settings.AzimuthDeg, ElevationDeg: Settings.ElevationDeg,
            AltitudeKm: Settings.AltitudeKm, Relief: Settings.Relief, WaveScale: Settings.WaveScale,
            FieldOfViewDeg: Settings.FieldOfViewDeg,
            LightAzimuthDeg: Settings.LightAzimuthDeg, LightElevationDeg: Settings.LightElevationDeg,
            NearKm: Settings.NearKm
        };
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    THE SPHERE
    //------------------------------------------------------------------------------------------------------------------------

    const DEG = Math.PI / 180;

    function SpherePoint(Lon, Lat, RadiusKm, Target)
    {
        const CosLat = Math.cos(Lat * DEG);
        Target[0] = RadiusKm * CosLat * Math.cos(Lon * DEG);
        Target[1] = RadiusKm * CosLat * Math.sin(Lon * DEG);
        Target[2] = RadiusKm * Math.sin(Lat * DEG);
        return Target;
    }

    // 📝 The camera sits on the local normal at the target and swings by azimuth and elevation around it, so "orbit" always
    //    means the same thing whether the view is a district or a hemisphere.
    function ResolveFrame(Camera, PlanetRadiusKm)
    {
        const Anchor = [0, 0, 0];
        SpherePoint(Camera.TargetLon, Camera.TargetLat, PlanetRadiusKm, Anchor);
        const Up = Normalise(Anchor);
        const East = Normalise(Cross([0, 0, 1], Up));
        const North = Normalise(Cross(Up, East));
        let Position, Forward;
        if (Camera.AltitudeKm > 0)
        {
            // 📝 Globe mode: the camera sits over the target at an altitude and looks straight down at the planet's centre.
            //    A surface aim point on a 6,371-kilometre sphere is a fine way to look at a coast and a poor way to look at a
            //    planet — the disc lands low in the frame and half of it is off the bottom edge.
            Position = Add(Anchor, Scale(Up, Camera.AltitudeKm));
            Forward = Normalise(Sub(Anchor, Position));
        }
        else
        {
            const Azimuth = Camera.AzimuthDeg * DEG, Elevation = Camera.ElevationDeg * DEG;
            const Direction = Normalise(Add(Add(Scale(East, Math.sin(Azimuth) * Math.cos(Elevation)),
                Scale(North, Math.cos(Azimuth) * Math.cos(Elevation))), Scale(Up, Math.sin(Elevation))));
            Position = Add(Anchor, Scale(Direction, Camera.DistanceKm));
            Forward = Normalise(Sub(Anchor, Position));
        }
        // 📝 Looking straight down makes the usual cross product of forward and up vanish, and a NaN basis draws nothing at
        //    all. Over a globe the screen's right is due east, and north falls out of it.
        const Right = Camera.AltitudeKm > 0 ? East : Normalise(Cross(Forward, Up));
        const UpView = Normalise(Cross(Right, Forward));
        const Light = Normalise(Add(Add(Scale(East, Math.sin(Camera.LightAzimuthDeg * DEG)),
            Scale(North, Math.cos(Camera.LightAzimuthDeg * DEG))),
            Scale(Up, Math.tan(Camera.LightElevationDeg * DEG))));
        return {
            Anchor, Up, East, North, Position, Forward, Right, UpView, Light,
            Focal: (Camera.Height / 2) / Math.tan(Camera.FieldOfViewDeg * DEG / 2)
        };
    }

    const Normalise = Vector =>
    {
        const Length = Math.max(1e-9, Math.hypot(Vector[0], Vector[1], Vector[2]));
        return [Vector[0] / Length, Vector[1] / Length, Vector[2] / Length];
    };
    const Dot = (A, B) => A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
    const Sub = (A, B) => [A[0] - B[0], A[1] - B[1], A[2] - B[2]];
    const Add = (A, B) => [A[0] + B[0], A[1] + B[1], A[2] + B[2]];
    const Scale = (A, K) => [A[0] * K, A[1] * K, A[2] * K];
    const Cross = (A, B) => [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]];

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    THE LAYERS
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Vertical exaggeration is a cheat with a limit. Waves need sixty times their height to read at all, but the same
    //    sixty turns the four-kilometre abyss into a hundred-and-sixty-kilometre pit, and the frame falls into it — the
    //    camera ends up looking at the inside of a bowl instead of a coast. So relief is exaggerated and depth is clamped:
    //    a twenty-sixth of the view distance, never more, which keeps the seabed a seabed at every scale.
    function DisplayHeight(Metres, Scale, DistanceKm)
    {
        // 📝 The limit is on the *drawn* depth, not on the metres: a clamp in metres turns into a canyon again the moment the
        //    water's exaggeration goes up, and at sixty times a two-hundred-metre shelf is fourteen kilometres of pit.
        const LimitKm = Math.max(0.02, DistanceKm * 0.18);
        return Math.max(-LimitKm, (Metres / 1000) * Scale);
    }

    // 📝 Destination point on a sphere: walk Kilometres from a point along a compass bearing. Two places need this — placing
    //    the disc under the camera, and sampling the disc's rings — so it lives here rather than inside either.
    function Destination(LonDeg, LatDeg, BearingDeg, Kilometres)
    {
        const Angle = Kilometres / 6371;
        const Bearing = BearingDeg * DEG;
        const SinLat = Math.sin(LatDeg * DEG), CosLat = Math.cos(LatDeg * DEG);
        const Lat = Math.asin(SinLat * Math.cos(Angle) + CosLat * Math.sin(Angle) * Math.cos(Bearing));
        const Lon = LonDeg * DEG + Math.atan2(Math.sin(Bearing) * Math.sin(Angle) * CosLat, Math.cos(Angle) - SinLat * Math.sin(Lat));
        return { Lon: Lon / DEG, Lat: Lat / DEG };
    }

    // 📝 Both layers are the same object seen two ways: a disc of the planet around the point the camera is aimed at, with
    //    rings that grow outwards. Near the target the samples are metres apart, at the horizon they are kilometres apart, and
    //    no ring is ever so wide that the ground between two of them goes missing — which is exactly what a lat/lon rectangle
    //    does when it is asked to reach the horizon, and why this is a disc and not a rectangle.
    function BuildDisc(Camera, Options)
    {
        const PlanetRadiusKm = Options.PlanetRadiusKm;
        const Rings = Options.Rings ?? 84;
        const Segments = Options.Segments ?? 176;
        // 📝 How far the eye can see decides how far the disc goes: the horizon from this altitude, plus the distance to the
        //    target, so the sea always meets the sky instead of ending in mid-frame.
        const AltitudeKm = Camera.AltitudeKm > 0
            ? Camera.AltitudeKm
            : Math.max(0.2, Camera.DistanceKm * Math.sin(Camera.ElevationDeg * DEG));
        const HorizonKm = Math.sqrt(2 * PlanetRadiusKm * AltitudeKm + AltitudeKm * AltitudeKm);
        // 📝 The rings are centred on the ground under the camera, not on the point it is aimed at. The eye resolves
        //    things at its feet and nothing at the horizon, and a disc centred on the target spends its whole budget on
        //    the far distance while the near ground — the only part with visible detail — is drawn in three-kilometre teeth.
        const GroundKm = Camera.AltitudeKm > 0
            ? Math.max(0.01, Camera.AltitudeKm * 0.5)
            : Math.max(0.01, Camera.DistanceKm * Math.cos(Camera.ElevationDeg * DEG));
        const Centre = Camera.AltitudeKm > 0
            ? { Lon: Camera.TargetLon, Lat: Camera.TargetLat }
            : Destination(Camera.TargetLon, Camera.TargetLat, Camera.AzimuthDeg, GroundKm / 111.32);
        const MaxKm = Options.MaxKm ?? Math.max(GroundKm * 1.4,
            Math.min(18000, (HorizonKm + GroundKm) * 1.02));
        const Count = (Rings + 1) * (Segments + 1);
        const ShadeScale = Options.ShadeScale ?? 1;
        const Positions = new Float32Array(Count * 3);
        const Reals = new Float32Array(Count * 3);
        const Heights = new Float32Array(Count);
        const Weights = new Float32Array(Count);
        const Extras = new Float32Array(Count);
        const Labels = Options.Labels ? new Uint8Array(Count) : null;
        const Point = [0, 0, 0];
        const SinLat = Math.sin(Centre.Lat * DEG), CosLat = Math.cos(Centre.Lat * DEG);
        const Lon0 = Centre.Lon * DEG;
        for (let Ring = 0; Ring <= Rings; ++Ring)
        {
            // 📝 Ring spacing is a power curve: a flat one crowds the horizon with rings nobody can see and starves the
            //    ground at the camera's feet, which is the only place the eye can resolve anything.
            // 📝 The innermost ring is a ten-thousandth of the disc off the centre, never zero: a ring of one point has no
            //    triangles, and a fixed number of metres is a hole at one scale and a pixel at another.
            const RadiusKm = Math.max(MaxKm * 1e-4, MaxKm * Math.pow(Ring / Rings, Options.RingPower ?? 1.7));
            const Angle = RadiusKm / PlanetRadiusKm;
            const SinAngle = Math.sin(Angle), CosAngle = Math.cos(Angle);
            for (let Segment = 0; Segment <= Segments; ++Segment)
            {
                const Bearing = (Segment / Segments) * Math.PI * 2;
                // 📝 Destination point on a sphere: walk RadiusKm from the target along the bearing and let the trigonometry
                //    carry the curvature — which at the horizon is more than ten kilometres of sag and not a detail to fake.
                const Lat = Math.asin(SinLat * CosAngle + CosLat * SinAngle * Math.cos(Bearing));
                const Lon = Lon0 + Math.atan2(Math.sin(Bearing) * SinAngle * CosLat, CosAngle - SinLat * Math.sin(Lat));
                const LatDeg = Math.max(-89.9, Math.min(89.9, Lat / DEG));
                const LonDeg = Lon / DEG;
                const Index = Ring * (Segments + 1) + Segment;
                const Sample = Options.Sample(LonDeg, LatDeg);
                Heights[Index] = Sample.Height;
                Weights[Index] = Sample.Weight === undefined ? 0 : Sample.Weight;
                Extras[Index] = Sample.Extra === undefined ? 0 : Sample.Extra;
                if (Labels) Labels[Index] = Sample.Label === undefined ? 0 : Sample.Label;
                // 📝 Shading is measured on a surface exaggerated by its own amount, not by the silhouette's: the water needs
                //    a third of its displacement for a crest to catch light, and the whole of it only to be seen edge-on.
                SpherePoint(LonDeg, LatDeg, PlanetRadiusKm + (Sample.Height * ShadeScale) / 1000, Point);
                Reals[Index * 3] = Point[0]; Reals[Index * 3 + 1] = Point[1]; Reals[Index * 3 + 2] = Point[2];
                SpherePoint(LonDeg, LatDeg,
                    PlanetRadiusKm + DisplayHeight(Sample.Height, Options.Scale, Camera.DistanceKm), Point);
                Positions[Index * 3] = Point[0];
                Positions[Index * 3 + 1] = Point[1];
                Positions[Index * 3 + 2] = Point[2];
            }
        }
        // 📝 Shading normals come from the unexaggerated surface: a one-metre wave drawn sixty times over is a sixty-degree
        //    slope, and shading that would turn a calm sea into static. The eye should see the wave's real gradient and let
        //    the exaggeration change the silhouette alone.
        // 📝 A wave field sampled at ten kilometres a cell does not shade as a wave, it shades as noise — those concentric
        //    moiré bands across the far sea are aliasing, not water. So the normal fades to the sphere's own normal as the
        //    cells grow: the sea is glassy at the horizon and rippled at the camera, which is also what the mesh does.
        const Normals = new Float32Array(Count * 3);
        for (let Ring = 0; Ring <= Rings; ++Ring)
        {
            for (let Segment = 0; Segment <= Segments; ++Segment)
            {
                const Index = Ring * (Segments + 1) + Segment;
                let Nx = Reals[Index * 3], Ny = Reals[Index * 3 + 1], Nz = Reals[Index * 3 + 2];
                const Length = Math.hypot(Nx, Ny, Nz) || 1;
                Nx /= Length; Ny /= Length; Nz /= Length;
                // 📝 The cell is the *local* ring spacing, not the average one: the rings crowd towards the target, and an
                //    average spacing of a kilometre was quietly switching the wave shading off everywhere, including at the
                //    one place it is sampled finely enough to be true.
                const RingKm = MaxKm * Math.pow(Ring / Rings, Options.RingPower ?? 1.7);
                const PreviousKm = Ring === 0 ? 0 : MaxKm * Math.pow((Ring - 1) / Rings, Options.RingPower ?? 1.7);
                const RadialKm = Math.max(0.001, RingKm - PreviousKm);
                const CellKm = Math.max(RadialKm, (2 * Math.PI * RingKm) / Segments);
                // 📝 The shortest wave in the field is forty metres; a cell of four hundred cannot show it, so above a quarter
                //    of a kilometre the normal goes back to the sphere's. That is the LOD rule for the whole surface, not a fudge.
                const Detail = 1 / (1 + Math.pow(CellKm / 0.25, 2));
                const Near = Ring > 0 ? (Index - Segments - 1) * 3 : Index * 3;
                const Side = Segment > 0 ? (Index - 1) * 3 : Index * 3;
                const Ax = Reals[Near] - Reals[Index * 3], Ay = Reals[Near + 1] - Reals[Index * 3 + 1], Az = Reals[Near + 2] - Reals[Index * 3 + 2];
                const Bx = Reals[Side] - Reals[Index * 3], By = Reals[Side + 1] - Reals[Index * 3 + 1], Bz = Reals[Side + 2] - Reals[Index * 3 + 2];
                let Cx = Ay * Bz - Az * By, Cy = Az * Bx - Ax * Bz, Cz = Ax * By - Ay * Bx;
                const Cl = Math.hypot(Cx, Cy, Cz);
                if (Cl > 1e-9)
                {
                    Cx /= Cl; Cy /= Cl; Cz /= Cl;
                    if (Cx * Nx + Cy * Ny + Cz * Nz < 0) { Cx = -Cx; Cy = -Cy; Cz = -Cz; }
                    const Mix = 0.75 * Detail;
                    Nx = Nx * (1 - Mix) + Cx * Mix; Ny = Ny * (1 - Mix) + Cy * Mix; Nz = Nz * (1 - Mix) + Cz * Mix;
                    const Nl = Math.hypot(Nx, Ny, Nz) || 1;
                    Nx /= Nl; Ny /= Nl; Nz /= Nl;
                }
                Normals[Index * 3] = Nx; Normals[Index * 3 + 1] = Ny; Normals[Index * 3 + 2] = Nz;
            }
        }
        const Indices = [];
        for (let Ring = 0; Ring < Rings; ++Ring)
        {
            for (let Segment = 0; Segment < Segments; ++Segment)
            {
                const A = Ring * (Segments + 1) + Segment, B = A + 1, C = A + Segments + 1, D = C + 1;
                Indices.push(A, B, C, B, D, C);
            }
        }
        return {
            Positions, Normals, Reals, Heights, Weights, Extras, Labels,
            Indices: Uint32Array.from(Indices), Triangles: Rings * Segments * 2, Rings, Segments, MaxKm
        };
    }

    const BAND_ORDER = ['Abyss', 'Deep', 'Shelf', 'Coast', 'Plain', 'Highland', 'Peak'];

    function BuildTerrain(Field, Camera, Options)
    {
        Options = Options || {};
        const Planet = Field.Planet;
        const Disc = BuildDisc(Camera, Object.assign({
            PlanetRadiusKm: Planet.RadiusKm,
            Scale: Options.Scale ?? Camera.Relief,
            Labels: true,
            Sample: (Lon, Lat) =>
            {
                const Height = Field.Sample.Height(Lon, Lat);
                // 📝 Band() names the band; the rasteriser wants its index, and a name that missed the list must fall back to
                //    the abyss rather than to NaN, which paints as black.
                const Band = Math.max(0, BAND_ORDER.indexOf(Field.Sample.Band(Height)));
                return { Height, Label: Band, Extra: Height };
            }
        }, Options));
        Disc.Bands = Disc.Labels;
        Disc.BandOrder = BAND_ORDER;
        return Disc;
    }

    // 📝 The water is a disc too, sampled from the same blend the mesh uses: the solve where it exists, the spectrum where
    //    it does not. Drawing the adaptive plan itself in 3D was tried and rejected — at a low camera its radial fan reads as
    //    wedges of geometry rather than sea, and the plan is already drawn, in full, in the resolution map. What the viewport
    //    has to show is the surface, so it asks the surface for every vertex.
    function BuildWater(Ocean, Camera, Options)
    {
        Options = Options || {};
        const Time = Options.Time ?? Ocean.Clock;
        return BuildDisc(Camera, Object.assign({
            PlanetRadiusKm: Ocean.Planet.RadiusKm,
            Scale: Options.Scale ?? Camera.WaveScale,
            ShadeScale: Options.ShadeScale ?? Math.max(1, 0.4 * (Options.Scale ?? Camera.WaveScale)),
            Rings: Options.Rings ?? 96,
            Sample: (Lon, Lat) =>
            {
                const Surface = Ocean.SurfaceHeight(Lon, Lat, Time);
                return {
                    Height: Surface.Height,
                    Weight: Surface.Weight,
                    Extra: Math.max(0, -Ocean.Terrain.Sample.Height(Lon, Lat))
                };
            }
        }, Options))
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    THE RASTERISER
    //------------------------------------------------------------------------------------------------------------------------

    const TERRAIN_COLOURS = [
        [11, 22, 32], [19, 44, 58], [29, 74, 88], [215, 199, 160], [125, 151, 97], [164, 150, 122], [222, 227, 230]
    ];
    // 📝 The bands are boundaries, not tiles. Painting a whole triangle the colour of its first vertex drew the seabed in
    //    chevrons that showed through the water, so the height picks a position between two bands and the colour is mixed
    //    there: the same seven colours, without the staircase.
    const BAND_EDGES = [-4800, -2200, -600, 40, 420, 900, 1500];

    function TerrainColour(Metres)
    {
        let Band = 0;
        if (Metres >= BAND_EDGES[BAND_EDGES.length - 1]) Band = BAND_EDGES.length - 1;
        else while (Band < BAND_EDGES.length - 1 && Metres >= BAND_EDGES[Band + 1]) ++Band;
        const Low = BAND_EDGES[Band], High = Band + 1 < BAND_EDGES.length ? BAND_EDGES[Band + 1] : Low + 1;
        const T = High > Low ? Math.max(0, Math.min(1, (Metres - Low) / (High - Low))) : 0;
        const A = TERRAIN_COLOURS[Math.min(TERRAIN_COLOURS.length - 1, Band)];
        const B = TERRAIN_COLOURS[Math.min(TERRAIN_COLOURS.length - 1, Band + 1)];
        return [Lerp(A[0], B[0], T), Lerp(A[1], B[1], T), Lerp(A[2], B[2], T)];
    }

    function Render(Camera, PlanetRadiusKm, Layers, Options)
    {
        Options = Options || {};
        const Started = Date.now();
        const Raster = RasterApi.CreateRaster(Camera.Width, Camera.Height, Options.Sky ?? '#060a10');
        // 📝 A vertical gradient behind everything: the vacuum reads as sky, and the eye gets a horizon to sit the planet on.
        const Sky = Options.SkyColour ? RasterApi.Colour(Options.SkyColour) : [18, 34, 47];
        for (let Row = 0; Row < Camera.Height; ++Row)
        {
            const T = Math.pow(Row / Camera.Height, 1.5);
            RasterApi.FillRect(Raster, 0, Row, Camera.Width, 1,
                [Sky[0] * (0.25 + T * 0.9), Sky[1] * (0.25 + T * 0.9), Sky[2] * (0.25 + T * 0.9)], 1);
        }
        const Frame = ResolveFrame(Camera, PlanetRadiusKm);
        // 📝 The near plane has to be a fraction of the view, not a kilometre. At a globe a kilometre is nothing; at a
        //    three-kilometre view of a swell it is the entire foreground, and the sea came back with its bottom half missing.
        // 📝 "How big is this view" is the altitude when the camera is over a globe and the distance when it is over a coast;
        //    using the distance in globe mode fogged a planet that was twenty thousand kilometres away into mud.
        const ViewKm = Camera.AltitudeKm > 0 ? Camera.AltitudeKm : Camera.DistanceKm;
        const NearKm = Math.min(Camera.NearKm, Math.max(0.0005, ViewKm * 0.05));
        const Depth = new Float32Array(Camera.Width * Camera.Height).fill(Infinity);
        // 📝 Two buffers the sea needs and depth alone cannot give. Ground records the height of whatever terrain landed on
        //    the pixel, and WaterDepth orders the sea against itself; the sea is then drawn wherever the ground under it is
        //    below the waterline, and nowhere else. Comparing the two layers by depth instead drew them into each other —
        //    both meshes are tens of kilometres a triangle, and their linear depths cross within a pixel.
        const Ground = new Float32Array(Camera.Width * Camera.Height).fill(NaN);
        const WaterDepth = new Float32Array(Camera.Width * Camera.Height).fill(Infinity);
        const Stats = { TerrainTriangles: 0, WaterTriangles: 0, Dots: 0 };

        const Project = (Point, Out) =>
        {
            const Relative = Sub(Point, Frame.Position);
            const Z = Dot(Relative, Frame.Forward);
            if (Z <= NearKm) { Out.Visible = false; return Out; }
            const Scale = Frame.Focal / Z;
            Out.X = Camera.Width / 2 + Dot(Relative, Frame.Right) * Scale;
            Out.Y = Camera.Height / 2 - Dot(Relative, Frame.UpView) * Scale;
            Out.Z = Z;
            Out.Visible = true;
            return Out;
        };

        const Screen = [
            { X: 0, Y: 0, Z: 0, Visible: false }, { X: 0, Y: 0, Z: 0, Visible: false }, { X: 0, Y: 0, Z: 0, Visible: false }
        ];
        // 📝 One normal per triangle, averaged from the three vertex normals the builder measured on the real surface.
        const LayerNormal = (Layer, I0, I1, I2) =>
        {
            const N = Layer.Normals;
            if (!N) return Normalise(Cross(Sub([Layer.Positions[I1 * 3], Layer.Positions[I1 * 3 + 1], Layer.Positions[I1 * 3 + 2]],
                [Layer.Positions[I0 * 3], Layer.Positions[I0 * 3 + 1], Layer.Positions[I0 * 3 + 2]]),
                Sub([Layer.Positions[I2 * 3], Layer.Positions[I2 * 3 + 1], Layer.Positions[I2 * 3 + 2]],
                [Layer.Positions[I0 * 3], Layer.Positions[I0 * 3 + 1], Layer.Positions[I0 * 3 + 2]])));
            const Out = [N[I0 * 3] + N[I1 * 3] + N[I2 * 3], N[I0 * 3 + 1] + N[I1 * 3 + 1] + N[I2 * 3 + 1],
                N[I0 * 3 + 2] + N[I1 * 3 + 2] + N[I2 * 3 + 2]];
            return Normalise(Out);
        };

        // 📝 Flat-shaded scanline fill with an interpolated depth: three edge functions, both windings, no per-pixel lighting,
        //    which is the only way a hundred thousand triangles survive in JavaScript.
        const Edge = (P, Q, X, Y) => (Q.X - P.X) * (Y - P.Y) - (X - P.X) * (Q.Y - P.Y);
        // 📝 Two layers, two rules. The ground owns a pixel by depth and records its own height there. The sea owns a pixel
        //    only where that recorded height is below the waterline, and then only against itself. Ordering the two by depth
        //    instead was a mistake of scale: both meshes are kilometres to a triangle, their linear depths cross inside a
        //    single pixel, and the far sea came back combed with wedges of seabed.
        function DrawTriangle(A, B, C, Colour, Alpha, OptionsDrawn)
        {
            const Sea = !!OptionsDrawn && OptionsDrawn.Sea;
            const Area = Edge(A, B, C.X, C.Y);
            // 📝 Degenerate only, never merely small. Culling sub-pixel triangles looked like a tidy way to lose the needles
            //    at a sea's edge and instead punched a hole through the aim point, where the disc's rings are all closer
            //    together than a pixel.
            if (Math.abs(Area) < 1e-9) return;
            const MinimumX = Math.max(0, Math.floor(Math.min(A.X, B.X, C.X)));
            const MaximumX = Math.min(Camera.Width - 1, Math.ceil(Math.max(A.X, B.X, C.X)));
            const MinimumY = Math.max(0, Math.floor(Math.min(A.Y, B.Y, C.Y)));
            const MaximumY = Math.min(Camera.Height - 1, Math.ceil(Math.max(A.Y, B.Y, C.Y)));
            if (MaximumX < MinimumX || MaximumY < MinimumY) return;
            const Pixels = Raster.Pixels;
            const Mix = Alpha === undefined ? 1 : Alpha;
            for (let Y = MinimumY; Y <= MaximumY; ++Y)
            {
                for (let X = MinimumX; X <= MaximumX; ++X)
                {
                    const Px = X + 0.5, Py = Y + 0.5;
                    const W0 = Edge(A, B, Px, Py), W1 = Edge(B, C, Px, Py), W2 = Edge(C, A, Px, Py);
                    if (Area > 0 ? (W0 < 0 || W1 < 0 || W2 < 0) : (W0 > 0 || W1 > 0 || W2 > 0)) continue;
                    const Z = (W2 * A.Z + W1 * B.Z + W0 * C.Z) / Area;
                    if (Z <= NearKm) continue;
                    const Index = Y * Camera.Width + X;
                    if (Sea)
                    {
                        const Solid = Ground[Index];
                        if (Solid === Solid && Solid >= 0) continue;
                        if (Z >= WaterDepth[Index]) continue;
                        WaterDepth[Index] = Z;
                    }
                    else
                    {
                        if (Z >= Depth[Index]) continue;
                        Depth[Index] = Z;
                        Ground[Index] = (W2 * A.H + W1 * B.H + W0 * C.H) / Area;
                    }
                    const Offset = Index * 3;
                    Pixels[Offset] = Pixels[Offset] * (1 - Mix) + Colour[0] * Mix;
                    Pixels[Offset + 1] = Pixels[Offset + 1] * (1 - Mix) + Colour[1] * Mix;
                    Pixels[Offset + 2] = Pixels[Offset + 2] * (1 - Mix) + Colour[2] * Mix;
                    ++Stats.Dots;
                }
            }
        }

        // ① terrain
        if (Layers.Terrain)
        {
            const Terrain = Layers.Terrain;
            const Positions = Terrain.Positions, Indices = Terrain.Indices;
            for (let Index = 0; Index < Indices.length; Index += 3)
            {
                const I0 = Indices[Index], I1 = Indices[Index + 1], I2 = Indices[Index + 2];
                const P0 = [Positions[I0 * 3], Positions[I0 * 3 + 1], Positions[I0 * 3 + 2]];
                const P1 = [Positions[I1 * 3], Positions[I1 * 3 + 1], Positions[I1 * 3 + 2]];
                const P2 = [Positions[I2 * 3], Positions[I2 * 3 + 1], Positions[I2 * 3 + 2]];
                const Normal = LayerNormal(Terrain, I0, I1, I2);
                // 📝 The normal points out of the sphere, so a back face is one whose normal disagrees with the eye ray.
                const ToEye = Normalise(Sub(Frame.Position, P0));
                if (Dot(Normal, ToEye) <= 0) continue;
                Project(P0, Screen[0]); Project(P1, Screen[1]); Project(P2, Screen[2]);
                if (!Screen[0].Visible || !Screen[1].Visible || !Screen[2].Visible) continue;
                Screen[0].H = Terrain.Extras[I0]; Screen[1].H = Terrain.Extras[I1]; Screen[2].H = Terrain.Extras[I2];
                const MeanHeight = (Screen[0].H + Screen[1].H + Screen[2].H) / 3;
                // 📝 The seabed is a shape, not a lit surface. Shaded like land, its exaggerated relief faceted into chevrons
                //    and the chevrons showed straight through the water; drowned, it goes flat and dark and reads as depth.
                const Underwater = MeanHeight < 0;
                const Shade = Underwater
                    ? 0.46 + 0.22 * Math.max(0, Dot(Normal, Frame.Light))
                    : Math.max(0.28, Math.min(1.25, 0.35 + 0.78 * Math.max(0, Dot(Normal, Frame.Light))));
                const Haze = Math.min(0.55, Math.max(0, (Screen[0].Z - ViewKm * 2.2) / (ViewKm * 8)));
                const Mixed = TerrainColour(MeanHeight);
                const Drown = Underwater ? Math.min(0.75, (-MeanHeight) / 1400) : 0;
                const Base = [Lerp(Mixed[0], 14, Drown), Lerp(Mixed[1], 36, Drown), Lerp(Mixed[2], 48, Drown)];
                const Fog = [Sky[0] * (0.6 + Haze), Sky[1] * (0.6 + Haze), Sky[2] * (0.6 + Haze)];
                DrawTriangle(Screen[0], Screen[1], Screen[2], [
                    Lerp(Base[0] * Shade, Fog[0], Haze), Lerp(Base[1] * Shade, Fog[1], Haze),
                    Lerp(Base[2] * Shade, Fog[2], Haze)
                ], 1, null);
                ++Stats.TerrainTriangles;
            }
        }

        // ② water, blended over whatever the seabed left behind
        if (Layers.Water)
        {
            const Water = Layers.Water;
            const Positions = Water.Positions, Indices = Water.Indices;
            const PaintTiers = !!Options.PaintTiers;
            for (let Index = 0; Index < Indices.length; Index += 3)
            {
                const I0 = Indices[Index], I1 = Indices[Index + 1], I2 = Indices[Index + 2];
                const P0 = [Positions[I0 * 3], Positions[I0 * 3 + 1], Positions[I0 * 3 + 2]];
                const P1 = [Positions[I1 * 3], Positions[I1 * 3 + 1], Positions[I1 * 3 + 2]];
                const P2 = [Positions[I2 * 3], Positions[I2 * 3 + 1], Positions[I2 * 3 + 2]];
                const Normal = LayerNormal(Water, I0, I1, I2);
                const ToEye = Normalise(Sub(Frame.Position, P0));
                if (Dot(Normal, ToEye) <= 0.02) continue;
                Project(P0, Screen[0]); Project(P1, Screen[1]); Project(P2, Screen[2]);
                if (!Screen[0].Visible || !Screen[1].Visible || !Screen[2].Visible) continue;
                // 📝 The wave normal does the sparkle: a crest facing the light reads bright, a back slope goes dark, and the
                //    whole thing is geometry rather than a normal map.
                const Facing = Math.max(0, Dot(Normal, Frame.Light));
                const DepthM = Math.max(0, (Water.Extras[I0] + Water.Extras[I1] + Water.Extras[I2]) / 3);
                // 📝 No sea on dry land — but "dry" has to mean all three corners, not the average. Judged on the average the
                //    shoreline's own triangles were dropped and the coast came out combed with the seabed showing through.
                if (Math.max(Water.Extras[I0], Water.Extras[I1], Water.Extras[I2]) <= 0) continue;
                const Shallow = Math.min(1, DepthM / 260);
                // 📝 Paint-the-LOD tints the water by which field is authoritative here: green where the 2.5D solve is the
                //    surface, blue-grey where the spectrum has taken over. It is the same distinction the resolution map draws.
                const Weight = (Water.Weights[I0] + Water.Weights[I1] + Water.Weights[I2]) / 3;
                const Solve = 1 - Weight;
                const Colour = PaintTiers
                    ? [
                        Lerp(52, 150, Solve) * (0.7 + Facing * 0.6),
                        Lerp(120, 214, Solve) * (0.7 + Facing * 0.6),
                        Lerp(168, 172, Solve) * (0.7 + Facing * 0.6)
                    ]
                    : [
                        // 📝 Turquoise over the shelf, navy over the abyss, and the highlight carries the wave normal. Depth is
                        //    the ocean's own colour scale, exactly as the map draws it.
                        Math.min(255, Lerp(74, 10, Shallow) + Facing * 68),
                        Math.min(255, Lerp(178, 44, Shallow) + Facing * 84),
                        Math.min(255, Lerp(172, 76, Shallow) + Facing * 72)
                    ];
                // 📝 Clear water shows its bottom, deep water keeps it: the sea gets more opaque as it gets deeper, which is
                //    also what hides the seabed's own triangulation from the surface.
                const Alpha = Options.WaterAlpha ?? (PaintTiers ? 0.95 : 0.58 + 0.36 * Shallow);
                DrawTriangle(Screen[0], Screen[1], Screen[2], Colour, Alpha, { Sea: true });
                ++Stats.WaterTriangles;
            }
        }

        Stats.Milliseconds = Date.now() - Started;
        Stats.Raster = Raster;
        return Stats;
    }

    const Lerp = (A, B, T) => A + (B - A) * Math.max(0, Math.min(1, T));

    function ToDataUrl(Stats)
    {
        return RasterApi.ToDataUrl(RasterApi.EncodePng(Stats.Raster));
    }

    // 📝 The raster is three bytes a pixel; a canvas ImageData is four, and the fourth is alpha. Handing the RGB stream to
    //    ImageData.set() does not convert it, it shifts it — every pixel reads its neighbour's channels and the canvas comes
    //    out transparent and striped. This is the conversion, and it is the only one.
    function ToRgba(Stats)
    {
        const Source = Stats.Raster.Pixels;
        const Count = Source.length / 3;
        const Out = new Uint8ClampedArray(Count * 4);
        for (let Index = 0; Index < Count; ++Index)
        {
            Out[Index * 4] = Source[Index * 3];
            Out[Index * 4 + 1] = Source[Index * 3 + 1];
            Out[Index * 4 + 2] = Source[Index * 3 + 2];
            Out[Index * 4 + 3] = 255;
        }
        return Out;
    }

    // 📝 A planet seen from 9,000 km subtends forty-nine degrees, and a forty-two degree frame cuts it in half. The altitude
    //    that fits is a function of the field of view and nothing else: sin(α) = R / (R + h) for the angular radius α the
    //    frame can hold with a margin, so h = R / sin(α) − R.
    function FrameGlobe(Camera, PlanetRadiusKm)
    {
        // 📝 The frame is not square and the field of view is vertical, so the tighter of the two half-angles is the one that
        //    decides: a portrait stage clips the planet at the sides long before it clips it top and bottom.
        const Half = (Camera.FieldOfViewDeg * DEG) / 2;
        const Across = Math.atan(Math.tan(Half) * (Camera.Width / Camera.Height));
        const Alpha = Math.min(1.2, Math.min(Half, Across) * 0.86);
        Camera.AltitudeKm = Math.max(600, PlanetRadiusKm / Math.sin(Alpha) - PlanetRadiusKm);
        return Camera.AltitudeKm;
    }

    // 📝 Orbit and zoom are pure functions of the camera so the page, the evidence and a test all move the view the same way.
    function Orbit(Camera, DeltaAzimuthDeg, DeltaElevationDeg)
    {
        Camera.AzimuthDeg = (Camera.AzimuthDeg + DeltaAzimuthDeg) % 360;
        Camera.ElevationDeg = Math.max(4, Math.min(84, Camera.ElevationDeg + DeltaElevationDeg));
        return Camera;
    }

    function Zoom(Camera, Kilometres)
    {
        Camera.DistanceKm = Math.max(6, Math.min(28000, Camera.DistanceKm + Kilometres));
        return Camera;
    }

    return {
        CreateCamera, BuildDisc, BuildTerrain, BuildWater, Destination, DisplayHeight, FrameGlobe, Render, Orbit,
        Zoom, ToDataUrl, ToRgba,
        ResolveFrame, SpherePoint, DEFAULTS
    };
})();

if (typeof module !== 'undefined' && module.exports) module.exports = Renderer3D;
