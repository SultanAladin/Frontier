//============================================================================================================================================
//                                                            LEVELSTRUCTURE.JS
//============================================================================================================================================
// 🧩 The Frontier proving grounds: training track, boundary barrier perimeter, tarmac markings, cones, lighting & traffic fixtures.

import { CourseMaterial } from "./MaterialSpecification.js";
import { CourseConstants, KerbConstants, SpawnPoint, StuntPark, EmitQuad, EmitBoxFacets, EmitPanelFacets, EmitPrismFacets,
         EmitExtrusionFacets, EmitConeFacets, EmitRampFacets, EmitTyreBarrierFacets, EmitSphereFacets,
         EmitStuntTabletopFacets, EmitStuntHalfPipeFacets, EmitStuntBowlFacets, EmitStuntBankRingFacets,
         EmitStuntStraightBankFacets, EmitStuntFunboxFacets, EmitStuntWhoopsFacets, EmitStuntEntryWedgeFacets,
         EmitStuntRingFacets } from "./CourseStructure.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                      SHAPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Jersey barrier cross-section (y, z), counter-clockwise: 60 cm foot, 16 cm crown, 80 cm tall.
const JerseyProfile = [[-0.30, 0.0], [0.30, 0.0], [0.30, 0.08], [0.12, 0.30], [0.08, 0.80], [-0.08, 0.80], [-0.12, 0.30], [-0.30, 0.08]];

// 📝 Traffic signal head with 3 distinct optical lenses (Red, Amber, Green)
function EmitTrafficHeadFacets() {
    const Facets = [];
    // Main housing box: 0.30m deep along X, 0.40m wide along Y, 1.20m tall along Z
    Facets.push(...EmitBoxFacets(0.30, 0.40, 1.20, CourseMaterial.SteelDark));
    // Three lenses on +X face:
    // +X face is at X = 0.5 * 0.30 = 0.15.
    const LensQuad = (Z0, Z1, Family) => {
        const Y = 0.14;
        const X = 0.155;
        // Counter-clockwise from outside:
        EmitQuad(Facets, [X, -Y, Z0], [X, Y, Z0], [X, Y, Z1], [X, -Y, Z1], Family);
    };
    LensQuad(0.80, 1.10, CourseMaterial.GateLampRed);  // Red (top)
    LensQuad(0.45, 0.75, CourseMaterial.LampWarm);     // Amber (middle)
    LensQuad(0.10, 0.40, CourseMaterial.GateLamp);     // Green (bottom)
    return Facets;
}

// 📝 Key → { Emit, Voxel [m] (distance volume), Cards [texel/m], Distance (false: rastered only) }.
export const LevelShapes = Object.freeze({
    Kerb:         { Emit: () => EmitBoxFacets(KerbConstants.SegmentLength, KerbConstants.Depth, KerbConstants.Height, CourseMaterial.KerbRed), Voxel: 0.04, Cards: 16 },
    Ramp:         { Emit: () => EmitRampFacets(), Voxel: 0.12, Cards: 20 },
    Cone:         { Emit: () => EmitConeFacets(), Voxel: 0.02, Cards: 24 },
    HangarPanel:  { Emit: () => EmitPanelFacets(4.0, 0.4, 8.0, CourseMaterial.Cladding, CourseMaterial.WallChalk), Voxel: 0.28, Cards: 6 },
    HangarAccent: { Emit: () => EmitPanelFacets(4.0, 0.4, 8.0, CourseMaterial.Cladding, CourseMaterial.WallCrimson), Voxel: 0.28, Cards: 6 },
    HangarRoof:   { Emit: () => EmitBoxFacets(28.8, 2.0, 0.3, CourseMaterial.Cladding), Voxel: 0.24, Cards: 4 },
    LampStrip:    { Emit: () => EmitBoxFacets(24.0, 0.25, 0.12, CourseMaterial.LampCool), Voxel: 0.1, Cards: 4 },
    Cabinet:      { Emit: () => EmitBoxFacets(2.0, 0.6, 1.8, CourseMaterial.ContainerRed), Voxel: 0.08, Cards: 12 },
    Container40:  { Emit: () => EmitBoxFacets(12.2, 2.44, 2.6, CourseMaterial.ContainerOrange), Voxel: 0.12, Cards: 5 },
    Container20:  { Emit: () => EmitBoxFacets(6.1, 2.44, 2.6, CourseMaterial.ContainerBlue), Voxel: 0.12, Cards: 6 },
    TunnelWall:   { Emit: () => EmitBoxFacets(4.0, 0.5, 4.5, CourseMaterial.TunnelConcrete), Voxel: 0.15, Cards: 10 },
    TunnelRoof:   { Emit: () => EmitBoxFacets(9.0, 4.0, 0.5, CourseMaterial.TunnelConcrete), Voxel: 0.12, Cards: 8 },
    TunnelLamp:   { Emit: () => EmitBoxFacets(3.0, 0.12, 0.16, CourseMaterial.LampCool), Voxel: 0.04, Cards: 16 },
    Tower:        { Emit: () => EmitBoxFacets(5.0, 5.0, 9.0, CourseMaterial.BarrierConcrete), Voxel: 0.3, Cards: 6 },
    TowerCabin:   { Emit: () => EmitBoxFacets(6.5, 6.5, 2.6, CourseMaterial.Glass), Voxel: 0.15, Cards: 8 },
    TowerRoof:    { Emit: () => EmitBoxFacets(7.5, 7.5, 0.4, CourseMaterial.SteelDark), Voxel: 0.15, Cards: 6 },
    Jersey:       { Emit: () => EmitExtrusionFacets(JerseyProfile, 2.0, CourseMaterial.BarrierConcrete), Voxel: 0.05, Cards: 14 },
    TyreStack:    { Emit: () => EmitPrismFacets(0.33, 1.2, 12, CourseMaterial.Tyre), Voxel: 0.06, Cards: 16 },
    Pole:         { Emit: () => EmitBoxFacets(0.3, 0.3, 11.0, CourseMaterial.SteelDark), Voxel: 0.25, Cards: 0, Distance: false },
    PoleHead:     { Emit: () => EmitBoxFacets(1.8, 0.6, 0.3, CourseMaterial.SteelDark,
                                              [CourseMaterial.SteelDark, CourseMaterial.LampWarm, CourseMaterial.SteelDark,
                                               CourseMaterial.SteelDark, CourseMaterial.SteelDark, CourseMaterial.SteelDark]), Voxel: 0.05, Cards: 16 },
    FramePost:    { Emit: () => EmitBoxFacets(0.12, 0.12, 2.3, CourseMaterial.SteelDark), Voxel: 0.1, Cards: 0, Distance: false },
    TrafficPost:  { Emit: () => EmitBoxFacets(0.16, 0.16, 3.8, CourseMaterial.SteelDark), Voxel: 0.1, Cards: 0, Distance: false },
    TrafficHead:  { Emit: () => EmitTrafficHeadFacets(), Voxel: 0.05, Cards: 8 },
    GantryPost:   { Emit: () => EmitBoxFacets(0.3, 0.3, 5.6, CourseMaterial.SteelDark), Voxel: 0.2, Cards: 0, Distance: false },
    GantryBeam:   { Emit: () => EmitBoxFacets(21.6, 0.5, 0.6, CourseMaterial.SteelDark), Voxel: 0.12, Cards: 4 },
    GantryLamp:   { Emit: () => EmitBoxFacets(20.0, 0.22, 0.10, CourseMaterial.GateLamp), Voxel: 0.1, Cards: 4 },
    SignPost:     { Emit: () => EmitBoxFacets(0.12, 0.12, 3.8, CourseMaterial.SteelDark), Voxel: 0.1, Cards: 0, Distance: false },
    TyreBarrier:  { Emit: () => EmitTyreBarrierFacets(6, 0.68, 0.33, 1.2), Voxel: 0.06, Cards: 12 },
    // 📝 The materials garden: one shape per sphere so cards and field see the true family, and a framed mirror panel
    //    whose −Y face (the road side) is the mirror.
    GardenAluminium: { Emit: () => EmitSphereFacets(0.6, 16, 24, CourseMaterial.Aluminium), Voxel: 0.03, Cards: 16 },
    GardenGold:      { Emit: () => EmitSphereFacets(0.6, 16, 24, CourseMaterial.Gold), Voxel: 0.03, Cards: 16 },
    GardenGlass:     { Emit: () => EmitSphereFacets(0.6, 16, 24, CourseMaterial.ShowcaseGlass), Voxel: 0.03, Cards: 16 },
    GardenMirror:    { Emit: () => EmitBoxFacets(2.4, 0.15, 2.0, CourseMaterial.SteelDark,
                                                [CourseMaterial.SteelDark, CourseMaterial.SteelDark, CourseMaterial.SteelDark,
                                                 CourseMaterial.SteelDark, CourseMaterial.SteelDark, CourseMaterial.Mirror]),
                       Voxel: 0.08, Cards: 12 },
    // 📝 The stunt park (far-east wing): world-space facets sampled from the stunt heightfield (CourseStructure's
    //    StuntPark table), so each placement rides the identity. Coarse voxels: the distance volumes auto-coarsen to
    //    128 texels on the longest axis, and these are big concrete the GI only needs to occlude, never to resolve.
    StuntTableS:    { Emit: () => EmitStuntTabletopFacets(0), Voxel: 0.2, Cards: 6 },
    StuntTableM:    { Emit: () => EmitStuntTabletopFacets(1), Voxel: 0.2, Cards: 6 },
    StuntTableL:    { Emit: () => EmitStuntTabletopFacets(2), Voxel: 0.2, Cards: 6 },
    StuntPipe:      { Emit: () => EmitStuntHalfPipeFacets(), Voxel: 0.25, Cards: 4 },
    StuntBowl:      { Emit: () => EmitStuntBowlFacets(), Voxel: 0.3, Cards: 4 },
    StuntBankRing:  { Emit: () => EmitStuntBankRingFacets(), Voxel: 0.25, Cards: 4 },
    StuntBank:      { Emit: () => EmitStuntStraightBankFacets(), Voxel: 0.25, Cards: 4 },
    StuntFunbox:    { Emit: () => EmitStuntFunboxFacets(), Voxel: 0.15, Cards: 8 },
    StuntWhoops:    { Emit: () => EmitStuntWhoopsFacets(), Voxel: 0.1, Cards: 8 },
    StuntWedge:     { Emit: () => EmitStuntEntryWedgeFacets(), Voxel: 0.15, Cards: 8 },
    StuntRing:      { Emit: () => EmitStuntRingFacets(), Voxel: 0.12, Cards: 8 },
    StuntPylon:     { Emit: () => EmitBoxFacets(1.2, 1.2, 9.0, CourseMaterial.BarrierConcrete), Voxel: 0.12, Cards: 8 }
});

// 📝 The stunt ring's two pylons, under the tube's south and north feet (StuntPark.Ring).
const StuntPylonY = [StuntPark.Ring.Centre[1] - StuntPark.Ring.Radius, StuntPark.Ring.Centre[1] + StuntPark.Ring.Radius];

// 📝 Shapes the car collides with (as world boxes). Stunt ramps are NOT here: a ramp's box would fill the air over its
//    slopes, so the car meets them through the analytic course height only — exactly like the jump ramp. Only the
//    stunt ring's pylons (true vertical boxes) collide; the ring tube itself is flown through, never hit.
const ColliderShapes = new Set(["HangarPanel", "HangarAccent", "Cabinet", "Container40", "Container20", "TunnelWall",
                                "Tower", "Jersey", "TyreStack", "Pole", "FramePost", "TrafficPost", "TyreBarrier", "GantryPost",
                                "GardenAluminium", "GardenGold", "GardenGlass", "GardenMirror", "StuntPylon"]);

//------------------------------------------------------------------------------------------------------------------------
//                                                    PLACEMENTS
//------------------------------------------------------------------------------------------------------------------------

const Degrees = Math.PI / 180.0;

export function ResolveLevelPlacements()
{
    const List = [];
    const Place = (Shape, Name, X, Y, Z = 0.0, Yaw = 0.0, Material = -1) =>
        List.push({ Shape, Name, Position: [X, Y, Z], Yaw, Material, Collider: ColliderShapes.has(Shape) });

    // ① Boundary Jersey Barriers Perimeter: completely surrounds the large asphalt course [-58.5..250.5] X [-58.5..58.5] Y,
    //    the stunt park's far-east wing included — the old east wall is gone, so the park is driven to, not fenced off.
    for (let X = -58.0; X <= 250.0; X += 4.0)
        Place("Jersey", `Boundary North ${X.toFixed(0)}`, X, 58.5, 0.0, 0.0);
    for (let X = -58.0; X <= 250.0; X += 4.0)
        Place("Jersey", `Boundary South ${X.toFixed(0)}`, X, -58.5, 0.0, 0.0);
    for (let Y = -54.0; Y <= 54.0; Y += 4.0)
        Place("Jersey", `Boundary West ${Y.toFixed(0)}`, -58.5, Y, 0.0, 90.0 * Degrees);
    for (let Y = -54.0; Y <= 54.0; Y += 4.0)
        Place("Jersey", `Boundary East ${Y.toFixed(0)}`, 250.5, Y, 0.0, 90.0 * Degrees);

    // ② Main straight kerbs & jump ramp
    KerbConstants.Strips.forEach(([X0, X1, Y0, Y1], Strip) =>
    {
        const Segments = Math.round((X1 - X0) / KerbConstants.SegmentLength);
        for (let Segment = 0; Segment < Segments; ++Segment)
            Place("Kerb", `Kerb ${Strip ? "south" : "north"} ${Segment}`, X0 + (Segment + 0.5) * KerbConstants.SegmentLength, 0.5 * (Y0 + Y1), 0.0, 0.0,
                  (Segment & 1) ? CourseMaterial.PaintWhite : CourseMaterial.KerbRed);
    });
    Place("Ramp", "Jump ramp", 0.0, 0.0);

    // ③ The Garage / Hangar: tool cabinets, slatted roof, lighting
    for (let Panel = 0; Panel < 7; ++Panel)
        Place("HangarAccent", `Hangar back wall ${Panel}`, 22.0 + 4.0 * Panel, 46.2);
    for (let Panel = 0; Panel < 4; ++Panel)
    {
        Place("HangarPanel", `Hangar west wall ${Panel}`, 19.8, 32.0 + 4.0 * Panel, 0.0, 90.0 * Degrees);
        Place("HangarPanel", `Hangar east wall ${Panel}`, 48.2, 32.0 + 4.0 * Panel, 0.0, -90.0 * Degrees);
    }
    for (let Slat = 0; Slat < 6; ++Slat)
    {
        Place("HangarRoof", `Hangar roof ${Slat}`, 34.0, 31.0 + 3.0 * Slat, 8.0);
        Place("LampStrip", `Hangar lamp ${Slat}`, 34.0, 31.0 + 3.0 * Slat, 7.82);
    }
    for (const X of [24.0, 27.0, 41.0, 44.0])
        Place("Cabinet", `Tool cabinet ${X}`, X, 45.3);
    for (const [X, Y] of [[46.6, 43.8], [46.6, 42.9], [45.8, 43.6]])
        Place("TyreStack", "Hangar tyres", X, Y);

    // ④ Tunnel along the south edge, with illuminated lamps on both walls
    for (let Segment = 0; Segment < 8; ++Segment)
    {
        const X = -26.0 + 4.0 * Segment;
        Place("TunnelWall", `Tunnel north wall ${Segment}`, X, -43.75, 0.0, 0.0, CourseMaterial.WallCobalt);
        Place("TunnelWall", `Tunnel south wall ${Segment}`, X, -52.25, 0.0, 0.0, CourseMaterial.WallChalk);
        Place("TunnelRoof", `Tunnel roof ${Segment}`, X, -48.0, 4.5, 90.0 * Degrees);
        if (Segment % 2 === 0)
        {
            Place("TunnelLamp", `Tunnel lamp N${Segment}`, X, -44.06, 3.6);
            Place("TunnelLamp", `Tunnel lamp S${Segment}`, X, -51.94, 3.6);
        }
    }

    // ⑤ Control tower
    Place("Tower", "Control tower", -54.0, -11.0);
    Place("TowerCabin", "Control tower cabin", -54.0, -11.0, 9.0);
    Place("TowerRoof", "Control tower roof", -54.0, -11.0, 11.6);

    // ⑥ Smash alley barriers, tyre wall buffers
    for (let Barrier = 0; Barrier < 19; ++Barrier)
    {
        const X = -6.0 + 3.0 * Barrier;
        Place("Jersey", `Alley barrier N${Barrier}`, X, -19.6);
        Place("Jersey", `Alley barrier S${Barrier}`, X, -34.4);
    }
    for (let Stack = 0; Stack < 14; ++Stack)
        Place("TyreStack", `Burnout tyre wall ${Stack}`, -49.0 + 0.7 * Stack, -32.6);
    for (let Stack = 0; Stack < 18; ++Stack)
        Place("TyreStack", `Run-off tyre wall ${Stack}`, 58.6, -6.0 + 0.7 * Stack);
    for (const X of [6.0, 22.0])
    {
        Place("FramePost", `Pane post ${X}a`, X, -25.1);
        Place("FramePost", `Pane post ${X}b`, X, -28.9);
    }

    // ⑦ Traffic Light Fixtures (Posts & Heads)
    const TrafficLights = [
        [ -36.0, 8.5, 0.0 ],       // Start line north
        [ -36.0, -8.5, 0.0 ],      // Start line south
        [ 64.0, 18.0, -90.0 ],     // East junction north
        [ 64.0, -18.0, 90.0 ],     // East junction south
        [ 110.0, 0.0, 180.0 ],     // Central arena crossing
        [ 18.0, 26.0, 0.0 ]        // Garage entrance
    ];
    TrafficLights.forEach(([X, Y, Facing], Index) =>
    {
        Place("TrafficPost", `Traffic light post ${Index}`, X, Y);
        const Angle = Facing * Degrees;
        Place("TrafficHead", `Traffic light head ${Index}`, X + 0.35 * Math.cos(Angle), Y + 0.35 * Math.sin(Angle), 2.5, Angle);
    });

    // ⑧ Street Lamp Poles (Roadway Batwing Photometric Distribution)
    const Poles = [
        [-45.0, 10.0, -90.0], [-15.0, 10.0, -90.0], [15.0, 10.0, -90.0], [45.0, 10.0, -90.0],
        [-30.0, -10.0, 90.0], [0.0, -10.0, 90.0], [30.0, -10.0, 90.0], [-48.0, 30.0, 0.0],
        [-12.0, 44.0, 180.0], [-56.0, -24.0, 0.0], [20.0, -38.0, 90.0], [8.0, 30.0, 0.0],
        [85.0, -36.0, 180.0], [85.0, 8.0, 180.0], [85.0, 44.0, 180.0], [115.0, -36.0, 0.0],
        [115.0, 8.0, 0.0], [115.0, 44.0, 0.0], [145.0, -24.0, 180.0], [145.0, 24.0, 180.0],
        // 📝 Stunt park: on the flat only — the plaza, between the bank and the ring, between ring/bowl/pipe,
        //    between the kickers, mid landing-corridor and the north-east corner. All clear of ramps and flight lines.
        [164.0, 14.0, 0.0], [180.0, -2.0, 45.0], [210.0, 18.0, 0.0],
        [200.0, -33.0, 90.0], [222.0, -32.0, 90.0], [246.0, 26.0, 225.0]
    ];
    Poles.forEach(([X, Y, Facing], Index) =>
    {
        Place("Pole", `Light pole ${Index}`, X, Y);
        const Angle = Facing * Degrees;
        Place("PoleHead", `Light pole head ${Index}`, X + 0.7 * Math.cos(Angle), Y + 0.7 * Math.sin(Angle), 10.8, Angle);
    });

    // ⑨ Materials garden: metal and glass spheres plus a standing mirror, north of the start straight where the chase
    //    camera sees them — clear of kerbs, poles, the skidpad ring and the drill zone edge.
    Place("GardenAluminium", "Aluminium sphere", -34.0, 11.5);
    Place("GardenGold", "Gold sphere", -31.5, 11.5);
    Place("GardenGlass", "Glass sphere", -29.0, 11.5);
    Place("GardenMirror", "Standing mirror", -26.5, 11.5);

    // ⑩ Stunt park (far-east wing): freeride stunt features over open asphalt — no track, no gates to follow. Facets
    //    are world-space (the Ramp precedent), so every feature placement rides the identity; only the ring's pylons
    //    stand on their own positions. Run-ups and the Big Air landing corridor stay flat and empty by layout.
    Place("StuntTableS", "Tabletop S", 0.0, 0.0);
    Place("StuntTableM", "Tabletop M", 0.0, 0.0);
    Place("StuntTableL", "Tabletop L · Big Air", 0.0, 0.0);
    Place("StuntPipe", "Half-pipe", 0.0, 0.0);
    Place("StuntBowl", "Bowl", 0.0, 0.0);
    Place("StuntBankRing", "45° bank ring", 0.0, 0.0);
    Place("StuntBank", "45° wall-ride bank", 0.0, 0.0);
    Place("StuntFunbox", "Funbox", 0.0, 0.0);
    Place("StuntWhoops", "Whoops", 0.0, 0.0);
    Place("StuntWedge", "Bowl entry wedge", 0.0, 0.0);
    Place("StuntRing", "Stunt ring", 0.0, 0.0);
    Place("StuntPylon", "Stunt ring pylon south", StuntPark.Ring.Centre[0], StuntPylonY[0]);
    Place("StuntPylon", "Stunt ring pylon north", StuntPark.Ring.Centre[0], StuntPylonY[1]);

    return List;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONES
//------------------------------------------------------------------------------------------------------------------------

export const SkidpadRing = Object.freeze({ Centre: [-30.0, 30.0], Radius: 14.0 });

export function ResolveConePlacements()
{
    const List = [];
    const Cone = (Group, X, Y, Yaw = 0.0) => List.push({
        Name: `Cone ${Group} ${List.filter((Item) => Item.Group === Group).length}`,
        Position: [X, Y],
        Yaw,
        Group
    });

    // ① Skidpad ring (smooth concentric layout)
    for (let Index = 0; Index < 24; ++Index)
    {
        const Angle = 2.0 * Math.PI * Index / 24;
        Cone("skidpad", SkidpadRing.Centre[0] + 14.8 * Math.cos(Angle), SkidpadRing.Centre[1] + 14.8 * Math.sin(Angle), Angle);
    }
    for (let Index = 0; Index < 8; ++Index)
    {
        const Angle = 2.0 * Math.PI * (Index + 0.5) / 8;
        Cone("skidpad", SkidpadRing.Centre[0] + 8.4 * Math.cos(Angle), SkidpadRing.Centre[1] + 8.4 * Math.sin(Angle), Angle);
    }

    // ② Main Straight track bounds
    Cone("straight", -34.15, 6.7);
    Cone("straight", -34.15, -6.7);
    for (const X of [16.0, 20.0])
    {
        Cone("straight", X, 4.5);
        Cone("straight", X, -4.5);
    }

    // ③ Slalom Agility Course (X = 95, Y from -35 to 35, 10m intervals)
    for (let Index = 0; Index < 8; ++Index)
    {
        Cone("slalom", 95.0, -35.0 + 10.0 * Index);
    }

    // ④ Double lane-change / Moose test section (around X = 125)
    for (let i = 0; i < 5; ++i)
    {
        Cone("moose", 122.0, -25.0 + 6.0 * i);
        Cone("moose", 126.0, -25.0 + 6.0 * i);
    }
    for (let i = 0; i < 5; ++i)
    {
        Cone("moose", 125.0, 10.0 + 6.0 * i);
        Cone("moose", 129.0, 10.0 + 6.0 * i);
    }

    // ⑤ Training maneuver & parking bays
    for (let i = 0; i < 4; ++i)
    {
        Cone("parking", 62.0 + 7.0 * i, 36.0);
        Cone("parking", 62.0 + 7.0 * i, 46.0);
    }

    // ⑥ Stunt park: a plaza slalom, the Big Air gate pair and the half-pipe gate pair — all on the flat, so they
    //    sleep where placed (a cone on a ramp would wake and slide).
    Cone("stunt", 161.0, 6.0);
    Cone("stunt", 165.0, 10.0);
    Cone("stunt", 169.0, 14.0);
    Cone("stunt", 173.0, 12.0);
    Cone("stunt", 180.0, -14.5);
    Cone("stunt", 180.0, -19.5);
    Cone("stunt", 196.0, 34.0);
    Cone("stunt", 196.0, 42.0);

    return List;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 BREAKABLE PROPS
//------------------------------------------------------------------------------------------------------------------------

export function ResolveBreakables()
{
    const Approach = [-1.0, 0.0, 0.0];
    const List = [];
    const Add = (Name, Shape, Material, X, Y, Yaw, Options = {}) => List.push({
        Name, Shape, Material, Position: [X, Y, 0.0], Yaw, Approach, Capacity: Options.Capacity || 40,
        DensityScale: Options.DensityScale || 1.0, Mode: Options.Mode || "dynamic", Energy: Options.Energy || 6000, Seed: Options.Seed
    });
    const Crate = { Kind: "box", Extent: [0.9, 0.9, 0.9] };
    const Pane = { Kind: "box", Extent: [3.4, 0.05, 2.0] };
    const Tempered = { Kind: "box", Extent: [3.4, 0.04, 2.0] };
    const Pillar = { Kind: "prism", Radius: 0.36, Height: 1.8, Sides: 8 };
    const Barrier = { Kind: "box", Extent: [1.8, 0.5, 0.8] };
    const Wall = { Kind: "box", Extent: [3.6, 0.5, 1.2] };
    const Block = { Kind: "box", Extent: [1.2, 0.6, 0.8] };

    for (const Y of [-28.1, -27.0, -25.9])
        Add(`Wooden crate ${List.length + 1}`, Crate, "wood", -2.0, Y, 0.12 * (Y + 27.0), { DensityScale: 0.32, Capacity: 32 });
    Add("Glass pane", Pane, "glass", 6.0, -27.0, 90.0 * Degrees, { Capacity: 48 });
    Add("Concrete pillar A", Pillar, "concrete", 14.0, -28.6, 0.0, { Capacity: 40 });
    Add("Concrete pillar B", Pillar, "concrete", 14.0, -25.4, 0.0, { Capacity: 40 });
    Add("Tempered pane", Tempered, "tempered", 22.0, -27.0, 90.0 * Degrees, { Capacity: 48 });
    Add("ABS barrier A", Barrier, "plastic", 30.0, -28.0, 90.0 * Degrees, { Capacity: 24 });
    Add("ABS barrier B", Barrier, "plastic", 30.0, -26.0, 90.0 * Degrees, { Capacity: 24 });
    Add("Stone wall", Wall, "rock", 38.0, -27.0, 90.0 * Degrees, { Capacity: 40 });
    Add("Wooden crate 4", Crate, "wood", 46.0, -28.2, 0.3, { DensityScale: 0.32, Capacity: 32 });
    Add("Wooden crate 5", Crate, "wood", 46.0, -25.8, -0.2, { DensityScale: 0.32, Capacity: 32 });
    Add("Concrete block", Block, "concrete", 46.5, -27.0, 90.0 * Degrees, { Capacity: 40 });
    return List;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   GROUND PAINT
//------------------------------------------------------------------------------------------------------------------------

const PaintLift = 0.004;

export function EmitLevelGroundFacets()
{
    const C = CourseConstants;
    const Facets = [];
    const Outer = 240.0;
    const CentreX = 0.5 * (C.PadMinimum[0] + C.PadMaximum[0]);
    EmitQuad(Facets, [CentreX - Outer, -Outer, C.SurroundHeight], [CentreX + Outer, -Outer, C.SurroundHeight],
             [CentreX + Outer, Outer, C.SurroundHeight], [CentreX - Outer, Outer, C.SurroundHeight], CourseMaterial.Grass);

    const CellsX = Math.round((C.PadMaximum[0] - C.PadMinimum[0]) / C.GroundCell);
    const CellsY = Math.round((C.PadMaximum[1] - C.PadMinimum[1]) / C.GroundCell);
    for (let Row = 0; Row < CellsX; ++Row)
    {
        for (let Column = 0; Column < CellsY; ++Column)
        {
            const X0 = C.PadMinimum[0] + Row * C.GroundCell, X1 = X0 + C.GroundCell;
            const Y0 = C.PadMinimum[1] + Column * C.GroundCell, Y1 = Y0 + C.GroundCell;
            const Hash = Math.abs(Math.sin(Row * 12.9898 + Column * 78.233) * 43758.5453) % 1.0;
            const Family = Hash < 0.12 ? CourseMaterial.AsphaltPatch : CourseMaterial.Asphalt;
            EmitQuad(Facets, [X0, Y0, 0.0], [X1, Y0, 0.0], [X1, Y1, 0.0], [X0, Y1, 0.0], Family);
        }
    }

    const Paint = (X0, Y0, X1, Y1, Family) =>
        EmitQuad(Facets, [Math.min(X0, X1), Math.min(Y0, Y1), PaintLift], [Math.max(X0, X1), Math.min(Y0, Y1), PaintLift],
                 [Math.max(X0, X1), Math.max(Y0, Y1), PaintLift], [Math.min(X0, X1), Math.max(Y0, Y1), PaintLift], Family);

    const White = CourseMaterial.PaintWhite, Yellow = CourseMaterial.PaintYellow;
    const Blue = CourseMaterial.PaintBlue, Green = CourseMaterial.PaintGreen;

    // ① Boundary edge lines inside jersey barrier perimeter (the stunt park's far-east wing included)
    Paint(-56.5, 56.5, 250.5, 56.7, White);
    Paint(-56.5, -56.7, 250.5, -56.5, White);
    Paint(-56.7, -56.5, -56.5, 56.5, White);
    Paint(250.5, -56.5, 250.7, 56.5, White);

    // ② Main straight lines & center dashes
    for (const Y of [6.0, -6.0])
    {
        Paint(-52.0, Y - 0.1, 20.0, Y + 0.1, White);
        Paint(30.0, Y - 0.1, 56.0, Y + 0.1, White);
    }
    for (let X = -50.0; X < 54.0; X += 6.0)
    {
        Paint(X, -0.08, X + 3.2, 0.08, White);
    }

    // ③ Start line grid box
    for (let Square = 0; Square < 20; ++Square)
    {
        for (let Rank = 0; Rank < 2; ++Rank)
        {
            if ((Square + Rank) % 2 === 0)
                Paint(-34.3 + 0.3 * Rank, -5.8 + 0.58 * Square, -34.0 + 0.3 * Rank, -5.8 + 0.58 * (Square + 1), White);
        }
    }

    // ④ Skidpad circular guide rings
    PaintRing(Paint, Facets, SkidpadRing.Centre[0], SkidpadRing.Centre[1], SkidpadRing.Radius, 0.25, 72, 1.0, White);
    PaintRing(Paint, Facets, SkidpadRing.Centre[0], SkidpadRing.Centre[1], 8.5, 0.18, 36, 0.5, Yellow);

    // ⑤ Garage forecourt apron line
    Paint(18.0, 29.4, 50.0, 29.6, Yellow);
    for (const X of [24.0, 30.0, 36.0, 42.0, 48.0])
        Paint(X - 0.08, 32.0, X + 0.08, 42.0, White);

    // ⑥ Tunnel approach lane markings
    Paint(-36.0, -44.6, 10.0, -44.4, White);
    Paint(-36.0, -51.6, 10.0, -51.4, White);

    // ⑦ Smash alley entry hatching
    Paint(-8.0, -20.7, 52.0, -20.5, Yellow);
    Paint(-8.0, -33.5, 52.0, -33.3, Yellow);

    // ⑧ East wing training lanes
    // Center boulevard (Y = 0)
    for (let X = 60.0; X < 148.0; X += 6.0)
        Paint(X, -0.08, X + 3.2, 0.08, Yellow);
    // North lane (Y = 22.0)
    for (let X = 60.0; X < 148.0; X += 6.0)
        Paint(X, 21.92, X + 3.2, 22.08, White);
    // South lane (Y = -22.0)
    for (let X = 60.0; X < 148.0; X += 6.0)
        Paint(X, -22.08, X + 3.2, -21.92, White);

    // Slalom lane boundaries (X = 95, Y between -40 and 40)
    Paint(91.0, -40.0, 91.15, 40.0, White);
    Paint(99.0, -40.0, 99.15, 40.0, White);

    // ⑨ Stunt park markings — paint lies flat, so it keeps to the flat: the entry gate, the plaza ring, the Big Air
    //    approach bars and landing-corridor dashes, the wall-ride line and the whoops lane edges.
    Paint(155.7, -4.0, 156.3, -2.0, Blue);
    Paint(155.7, 18.0, 156.3, 20.0, Blue);
    for (let X = 157.0; X < 163.0; X += 3.0)
        Paint(X, 7.92, X + 1.8, 8.08, White);
    PaintRing(Paint, Facets, 166.0, 8.0, 5.0, 0.25, 24, 0.5, Blue);
    for (const X of [170.0, 174.0, 178.0])
        Paint(X, -19.0, X + 0.4, -15.0, White);
    for (let X = 209.0; X < 246.0; X += 6.0)
    {
        Paint(X, -23.1, X + 3.2, -22.9, White);
        Paint(X, -11.1, X + 3.2, -10.9, White);
    }
    Paint(162.0, -14.6, 180.0, -14.4, Green);
    Paint(162.0, -52.6, 192.0, -52.4, White);
    Paint(162.0, -45.6, 192.0, -45.4, White);

    return Facets;
}

function PaintRing(Paint, Facets, CentreX, CentreY, Radius, Width, Segments, Duty, Family)
{
    for (let Segment = 0; Segment < Segments; ++Segment)
    {
        const A0 = 2.0 * Math.PI * Segment / Segments, A1 = 2.0 * Math.PI * (Segment + Duty) / Segments;
        const Inner = Radius - 0.5 * Width, Outer = Radius + 0.5 * Width;
        const P = (Angle, R) => [CentreX + R * Math.cos(Angle), CentreY + R * Math.sin(Angle), PaintLift];
        EmitQuad(Facets, P(A0, Inner), P(A0, Outer), P(A1, Outer), P(A1, Inner), Family);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 DRILLS AND ZONES
//------------------------------------------------------------------------------------------------------------------------

export const TrainingDrills = Object.freeze([
    { Id: "skidpad", Name: "Skidpad", Glyph: "CircleDot", Note: "Hold the 14 m ring",
      Teleport: { Position: [-30.0, 18.5], Yaw: 0.0 }, Zone: { Minimum: [-47.0, 13.0], Maximum: [-13.0, 47.0] }, Groups: ["skidpad"] },
    { Id: "jump", Name: "Jump", Glyph: "TrendingUp", Note: "Airtime and distance past the lip",
      Teleport: { Position: [-20.0, 0.0], Yaw: 0.0 }, Lip: 28.0, LipY: 0.0, LipHalf: 3.4, Groups: ["straight"] },
    { Id: "smash", Name: "Smash alley", Glyph: "Hammer", Note: "Breakable props",
      Teleport: { Position: [-16.0, -27.0], Yaw: 0.0 }, Zone: { Minimum: [-12.0, -34.4], Maximum: [54.0, -19.6] }, Groups: [] },
    { Id: "slalom", Name: "Slalom", Glyph: "Waves", Note: "8 cones agility course",
      Teleport: { Position: [95.0, -42.0], Yaw: 90.0 }, Groups: ["slalom"] },
    { Id: "bigair", Name: "Big Air", Glyph: "Target", Note: "Thread the ring off the big kicker",
      Teleport: { Position: [150.0, -17.0], Yaw: 0.0 }, Lip: 191.0, LipY: -17.0, LipHalf: 4.5, Groups: ["stunt"] }
]);

export const ArenaZones = Object.freeze([
    { Name: "Start grid",  Position: [-40.0, 0.0],   Yaw: 0.0,   Note: "Main straight" },
    { Name: "Garage",      Position: [34.0, 20.0],   Yaw: 90.0,  Note: "Hangar & vehicle workshop" },
    { Name: "Tunnel",      Position: [-36.0, -48.0], Yaw: 0.0,   Note: "Lamps in the dark" },
    { Name: "East Course", Position: [80.0, 0.0],    Yaw: 0.0,   Note: "Open proving grounds" },
    { Name: "Smash Alley", Position: [-16.0, -27.0], Yaw: 0.0,   Note: "Breakable objects" },
    { Name: "Skidpad",     Position: [-30.0, 18.5],  Yaw: 0.0,   Note: "14 m drift ring" },
    { Name: "Stunt Park",  Position: [166.0, 8.0],   Yaw: 0.0,   Note: "Freeride stunt park" },
    { Name: "Big Air",     Position: [150.0, -17.0], Yaw: 0.0,   Note: "Big kicker & stunt ring" }
]);

export const LevelClearZones = Object.freeze([
    [-60.0, -9.0, 60.0, 9.0],      // main straight
    [-47.0, 13.0, -13.0, 50.0],    // skidpad
    [16.0, 22.0, 54.0, 50.0],      // hangar and forecourt
    [-12.0, -40.0, 54.0, -17.0],   // smash alley
    [-38.0, -56.0, 12.0, -40.0],   // tunnel and approaches
    [-60.0, -16.0, -47.0, -6.0],   // control tower
    [60.0, -58.0, 154.0, 58.0],    // east training grounds
    [158.0, -58.0, 252.0, 58.0]    // stunt park
]);

export { SpawnPoint };
