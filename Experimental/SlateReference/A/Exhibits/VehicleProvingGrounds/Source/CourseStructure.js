//============================================================================================================================================
//                                                            COURSESTRUCTURE.JS
//============================================================================================================================================
// 🧩 The proving grounds' terrain — the height query the tyres, fragments and smoke read — and the facet emitters every shape uses.

import { CourseMaterial } from "./MaterialSpecification.js";

// 📝 Metres, +X forward along the main straight / +Y left / +Z up. The asphalt pad runs 312 m east–west (the proving
//    grounds' original 120 m square plus the training arena's 96 m east wing plus the stunt park's 96 m far-east wing)
//    by 120 m; past it a grass plane 2 cm lower runs out to 480 m. Relief is analytic and small — the ramp wedge, the
//    kerbs and the stunt park's ramps — so the tyres, the debris and the renderer all read one exact surface.
export const CourseConstants = Object.freeze({
    PadMinimum:     Object.freeze([-60.0, -60.0]),
    PadMaximum:     Object.freeze([252.0, 60.0]),
    GroundCell:     4.0,
    RampNearX:      22.0,
    RampRunX:       6.0,
    RampRise:       1.35,
    RampHalfWidth:  3.2,
    RampCentreY:    0.0,
    SurroundHeight: -0.02
});

// 📝 The training cone: a 70 cm slalom cone after the common EN 13422 pattern — a chamfered square rubber base, a
//    fluorescent PVC body with a rounded tip, two retroreflective collars — weighted low the way a real one is (the
//    base carries most of its 3.2 kg), so it rocks and settles rather than toppling at a brush.
export const ConeConstants = Object.freeze({
    BaseHalf:     0.19,    // [m]  half side of the square base
    BaseHeight:   0.03,    // [m]
    BaseChamfer:  0.05,    // [m]  corner chamfer of the base
    BottomRadius: 0.150,   // [m]  body radius on the base
    TopRadius:    0.032,   // [m]  body radius under the tip
    BodyTop:      0.70,    // [m]
    TipTop:       0.716,   // [m]
    Segments:     24,      // [-]
    Bands:        Object.freeze([Object.freeze([0.30, 0.40]), Object.freeze([0.47, 0.53])]),   // [m] retroreflective collars
    Mass:         3.2,     // [kg]
    CentreHeight: 0.15     // [m]  centre of mass above the ground
});

// 📝 Raised kerb strips, [x₀, x₁, y₀, y₁] at KerbHeight: red-and-white along both edges of the main straight.
export const KerbConstants = Object.freeze({
    Height: 0.120,
    Depth:  0.300,
    SegmentLength: 4.0,
    Strips: Object.freeze([
        Object.freeze([-52.0, 20.0, 6.00, 6.30]),
        Object.freeze([-52.0, 20.0, -6.30, -6.00])
    ])
});

// 📝 Where the car starts and where R puts it back: the grid box behind the start line, facing down the straight.
export const SpawnPoint = Object.freeze({ Position: [-40.0, 0.0], Yaw: 0.0 });

//------------------------------------------------------------------------------------------------------------------------
//                                                   TERRAIN QUERIES
//------------------------------------------------------------------------------------------------------------------------

export function WithinKerb(PositionX, PositionY)
{
    for (const [X0, X1, Y0, Y1] of KerbConstants.Strips)
        if (PositionX >= X0 && PositionX <= X1 && PositionY >= Y0 && PositionY <= Y1)
            return true;
    return false;
}

// 📝 CourseHeight(): kerbs, then the ramp wedge (dropping to zero past the crest so the car launches), then the stunt
//    park's ramps (a max-composite over the far-east wing); flat elsewhere.
export function ResolveCourseHeight(PositionX, PositionY)
{
    const C = CourseConstants;
    if (WithinKerb(PositionX, PositionY))
        return KerbConstants.Height;
    if (PositionX >= C.RampNearX && PositionX <= C.RampNearX + C.RampRunX && Math.abs(PositionY - C.RampCentreY) <= C.RampHalfWidth)
        return C.RampRise * (PositionX - C.RampNearX) / C.RampRunX;
    if (PositionX > StuntBounds.Minimum[0] && PositionX < StuntBounds.Maximum[0]
     && PositionY > StuntBounds.Minimum[1] && PositionY < StuntBounds.Maximum[1])
        return ResolveStuntHeight(PositionX, PositionY);
    return 0.0;
}

// 📝 CourseNormal(): central differences over ±0.15 m.
export function ResolveCourseNormal(PositionX, PositionY)
{
    const Step = 0.15;
    const SlopeX = (ResolveCourseHeight(PositionX + Step, PositionY) - ResolveCourseHeight(PositionX - Step, PositionY)) / (2.0 * Step);
    const SlopeY = (ResolveCourseHeight(PositionX, PositionY + Step) - ResolveCourseHeight(PositionX, PositionY - Step)) / (2.0 * Step);
    const Length = Math.hypot(SlopeX, SlopeY, 1.0);
    return [-SlopeX / Length, -SlopeY / Length, 1.0 / Length];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FACET EMISSION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Each emitter appends world- or local-space facets as { Corners: [[x,y,z]×3], Family }, counter-clockwise outward.
export function EmitQuad(Facets, A, B, C, D, Family)
{
    Facets.push({ Corners: [A, B, C], Family });
    Facets.push({ Corners: [A, C, D], Family });
}

// 📝 Ramp wedge, world space: sloped surface, far vertical face, the two side walls. The authored corners wind inward
//    (DriveSceneAuthor), so each facet's winding is reversed: the distance transfer classifies inside by back faces.
export function EmitRampFacets()
{
    const C = CourseConstants;
    const Near = C.RampNearX, Far = C.RampNearX + C.RampRunX, Rise = C.RampRise;
    const Low = C.RampCentreY - C.RampHalfWidth, High = C.RampCentreY + C.RampHalfWidth;
    const Facets = [];
    EmitQuad(Facets, [Near, Low, 0.0], [Near, High, 0.0], [Far, High, Rise], [Far, Low, Rise], CourseMaterial.Ramp);
    EmitQuad(Facets, [Far, Low, Rise], [Far, High, Rise], [Far, High, 0.0], [Far, Low, 0.0], CourseMaterial.Ramp);
    Facets.push({ Corners: [[Near, Low, 0.0], [Far, Low, Rise], [Far, Low, 0.0]], Family: CourseMaterial.Ramp });
    Facets.push({ Corners: [[Near, High, 0.0], [Far, High, 0.0], [Far, High, Rise]], Family: CourseMaterial.Ramp });
    for (const Facet of Facets)
        Facet.Corners = [Facet.Corners[0], Facet.Corners[2], Facet.Corners[1]];
    return Facets;
}

// 📝 The chamfered square base as an octagon, counter-clockwise from +X.
function ResolveConeBaseOutline()
{
    const H = ConeConstants.BaseHalf, C = ConeConstants.BaseChamfer;
    return [[H, -(H - C)], [H, H - C], [H - C, H], [-(H - C), H], [-H, H - C], [-H, -(H - C)], [-(H - C), -H], [H - C, -H]];
}

// 📝 One training cone, local to its base centre: the octagonal base (flat), the body split at the collars (smooth
//    normals, so 24 segments read as round), and a two-ring dome for the tip.
export function EmitConeFacets()
{
    const K = ConeConstants;
    const Facets = [];
    const Outline = ResolveConeBaseOutline();
    for (let Side = 0; Side < Outline.length; ++Side)
    {
        const [X0, Y0] = Outline[Side], [X1, Y1] = Outline[(Side + 1) % Outline.length];
        EmitQuad(Facets, [X0, Y0, 0.0], [X1, Y1, 0.0], [X1, Y1, K.BaseHeight], [X0, Y0, K.BaseHeight], CourseMaterial.ConeBase);
        Facets.push({ Corners: [[0.0, 0.0, K.BaseHeight], [X0, Y0, K.BaseHeight], [X1, Y1, K.BaseHeight]], Family: CourseMaterial.ConeBase });
        Facets.push({ Corners: [[0.0, 0.0, 0.0], [X1, Y1, 0.0], [X0, Y0, 0.0]], Family: CourseMaterial.ConeBase });
    }
    const Radius = (Z) => K.BottomRadius + (K.TopRadius - K.BottomRadius) * (Z - K.BaseHeight) / (K.BodyTop - K.BaseHeight);
    const Slope = (K.BottomRadius - K.TopRadius) / (K.BodyTop - K.BaseHeight);
    const Splits = [K.BaseHeight, K.Bands[0][0], K.Bands[0][1], K.Bands[1][0], K.Bands[1][1], K.BodyTop];
    const Families = [CourseMaterial.ConeOrange, CourseMaterial.ConeBand, CourseMaterial.ConeOrange, CourseMaterial.ConeBand, CourseMaterial.ConeOrange];
    const Ring = (Angle, Z, R) => [R * Math.cos(Angle), R * Math.sin(Angle), Z];
    const Normal = (Angle, Lift) => { const L = Math.hypot(1.0, Lift); return [Math.cos(Angle) / L, Math.sin(Angle) / L, Lift / L]; };
    for (let Segment = 0; Segment < K.Segments; ++Segment)
    {
        const A0 = 2.0 * Math.PI * Segment / K.Segments, A1 = 2.0 * Math.PI * (Segment + 1) / K.Segments;
        for (let Band = 0; Band + 1 < Splits.length; ++Band)
        {
            const Z0 = Splits[Band], Z1 = Splits[Band + 1];
            const P00 = Ring(A0, Z0, Radius(Z0)), P10 = Ring(A1, Z0, Radius(Z0)), P11 = Ring(A1, Z1, Radius(Z1)), P01 = Ring(A0, Z1, Radius(Z1));
            const N0 = Normal(A0, Slope), N1 = Normal(A1, Slope);
            Facets.push({ Corners: [P00, P10, P11], Normals: [N0, N1, N1], Family: Families[Band] });
            Facets.push({ Corners: [P00, P11, P01], Normals: [N0, N1, N0], Family: Families[Band] });
        }
        // 📝 The tip: the body's last ring, a ring two thirds of the way round the dome, the apex.
        const Mid = K.BodyTop + 0.6 * (K.TipTop - K.BodyTop), MidRadius = 0.62 * K.TopRadius;
        const Q00 = Ring(A0, K.BodyTop, K.TopRadius), Q10 = Ring(A1, K.BodyTop, K.TopRadius);
        const Q01 = Ring(A0, Mid, MidRadius), Q11 = Ring(A1, Mid, MidRadius);
        const M0 = Normal(A0, 1.1), M1 = Normal(A1, 1.1);
        Facets.push({ Corners: [Q00, Q10, Q11], Normals: [Normal(A0, Slope), Normal(A1, Slope), M1], Family: CourseMaterial.ConeOrange });
        Facets.push({ Corners: [Q00, Q11, Q01], Normals: [Normal(A0, Slope), M1, M0], Family: CourseMaterial.ConeOrange });
        Facets.push({ Corners: [Q01, Q11, [0.0, 0.0, K.TipTop]], Normals: [M0, M1, [0.0, 0.0, 1.0]], Family: CourseMaterial.ConeOrange });
    }
    return Facets;
}

// 📝 The cone's collision hull points, local to its base centre: a regular octagon a touch wider than the base's
//    flats (at the ground and on top of the base) and a small octagon under the tip, every ring at the same angles
//    so each face of the hull is a plane — 18 planes, 24 points. A fair hull for something that rolls.
export function ResolveConeHullPoints()
{
    const K = ConeConstants;
    const Points = [];
    const Base = K.BaseHalf * 1.04;
    for (const [Z, Radius] of [[0.0, Base], [K.BaseHeight, Base], [K.BodyTop, K.TopRadius]])
        for (let Index = 0; Index < 8; ++Index)
        {
            const Angle = 2.0 * Math.PI * (Index + 0.5) / 8;
            const R = Radius / Math.cos(Math.PI / 8);   // the octagon's flats at Radius
            Points.push([R * Math.cos(Angle), R * Math.sin(Angle), Z]);
        }
    return Points;
}

// 📝 A row of tyre stacks along x (a tyre barrier section), local to its bottom centre: each stack four tyres high,
//    the top tyre painted white so the wall reads at speed.
export function EmitTyreBarrierFacets(Count, Spacing, Radius, Height, Sides = 12)
{
    const Facets = [];
    for (let Stack = 0; Stack < Count; ++Stack)
    {
        const Offset = (Stack - 0.5 * (Count - 1)) * Spacing;
        const Split = 0.75 * Height;
        for (const [Z0, Z1, Family] of [[0.0, Split, CourseMaterial.Tyre], [Split, Height, CourseMaterial.PaintWhite]])
        {
            for (let Side = 0; Side < Sides; ++Side)
            {
                const A0 = 2.0 * Math.PI * Side / Sides, A1 = 2.0 * Math.PI * (Side + 1) / Sides;
                const P0 = [Offset + Radius * Math.cos(A0), Radius * Math.sin(A0)], P1 = [Offset + Radius * Math.cos(A1), Radius * Math.sin(A1)];
                EmitQuad(Facets, [P0[0], P0[1], Z0], [P1[0], P1[1], Z0], [P1[0], P1[1], Z1], [P0[0], P0[1], Z1], Family);
                if (Z1 === Height)
                    Facets.push({ Corners: [[Offset, 0.0, Height], [P0[0], P0[1], Height], [P1[0], P1[1], Height]], Family: CourseMaterial.Tyre });
                if (Z0 === 0.0)
                    Facets.push({ Corners: [[Offset, 0.0, 0.0], [P1[0], P1[1], 0.0], [P0[0], P0[1], 0.0]], Family });
            }
        }
    }
    return Facets;
}

// 📝 A closed box between two corners (any placement), outward counter-clockwise winding.
export function EmitCornerBoxFacets(Minimum, Maximum, Family)
{
    const [X0, Y0, Z0] = Minimum, [X1, Y1, Z1] = Maximum;
    const Facets = [];
    EmitQuad(Facets, [X0, Y0, Z1], [X1, Y0, Z1], [X1, Y1, Z1], [X0, Y1, Z1], Family);
    EmitQuad(Facets, [X0, Y1, Z0], [X1, Y1, Z0], [X1, Y0, Z0], [X0, Y0, Z0], Family);
    EmitQuad(Facets, [X1, Y0, Z0], [X1, Y1, Z0], [X1, Y1, Z1], [X1, Y0, Z1], Family);
    EmitQuad(Facets, [X0, Y1, Z0], [X0, Y0, Z0], [X0, Y0, Z1], [X0, Y1, Z1], Family);
    EmitQuad(Facets, [X1, Y1, Z0], [X0, Y1, Z0], [X0, Y1, Z1], [X1, Y1, Z1], Family);
    EmitQuad(Facets, [X0, Y0, Z0], [X1, Y0, Z0], [X1, Y0, Z1], [X0, Y0, Z1], Family);
    return Facets;
}

// 📝 A closed axis-aligned box, local to its bottom centre, outward counter-clockwise winding. FaceFamilies, when
//    given, colours the six faces individually: [top, bottom, +X, −X, +Y, −Y].
export function EmitBoxFacets(ExtentX, ExtentY, ExtentZ, Family, FaceFamilies = null)
{
    const X = 0.5 * ExtentX, Y = 0.5 * ExtentY, Z = ExtentZ;
    const F = FaceFamilies || [Family, Family, Family, Family, Family, Family];
    const Facets = [];
    EmitQuad(Facets, [-X, -Y, Z], [X, -Y, Z], [X, Y, Z], [-X, Y, Z], F[0]);       // top    (+Z)
    EmitQuad(Facets, [-X, Y, 0.0], [X, Y, 0.0], [X, -Y, 0.0], [-X, -Y, 0.0], F[1]); // bottom (−Z)
    EmitQuad(Facets, [X, -Y, 0.0], [X, Y, 0.0], [X, Y, Z], [X, -Y, Z], F[2]);       // front  (+X)
    EmitQuad(Facets, [-X, Y, 0.0], [-X, -Y, 0.0], [-X, -Y, Z], [-X, Y, Z], F[3]);   // back   (−X)
    EmitQuad(Facets, [X, Y, 0.0], [-X, Y, 0.0], [-X, Y, Z], [X, Y, Z], F[4]);       // left   (+Y)
    EmitQuad(Facets, [-X, -Y, 0.0], [X, -Y, 0.0], [X, -Y, Z], [-X, -Y, Z], F[5]);   // right  (−Y)
    return Facets;
}

// 📝 A wall panel: Outer on every face but the −Y one, which is the painted Inner face a building shows inside.
export function EmitPanelFacets(ExtentX, ExtentY, ExtentZ, Outer, Inner)
{
    return EmitBoxFacets(ExtentX, ExtentY, ExtentZ, Outer, [Outer, Outer, Outer, Outer, Outer, Inner]);
}

// 📝 A vertical prism of Sides facets (tyre stacks), local to its bottom centre.
export function EmitPrismFacets(Radius, Height, Sides, Family)
{
    const Facets = [];
    for (let Side = 0; Side < Sides; ++Side)
    {
        const A0 = 2.0 * Math.PI * Side / Sides, A1 = 2.0 * Math.PI * (Side + 1) / Sides;
        const P0 = [Radius * Math.cos(A0), Radius * Math.sin(A0)], P1 = [Radius * Math.cos(A1), Radius * Math.sin(A1)];
        EmitQuad(Facets, [P0[0], P0[1], 0.0], [P1[0], P1[1], 0.0], [P1[0], P1[1], Height], [P0[0], P0[1], Height], Family);
        Facets.push({ Corners: [[0.0, 0.0, Height], [P0[0], P0[1], Height], [P1[0], P1[1], Height]], Family });
        Facets.push({ Corners: [[0.0, 0.0, 0.0], [P1[0], P1[1], 0.0], [P0[0], P0[1], 0.0]], Family });
    }
    return Facets;
}

// 📝 A UV sphere resting on the ground (centre at z = Radius, bottom-centre convention like the prism), outward
//    counter-clockwise winding — the distance transfer classifies inside by back faces. Corner normals are the exact
//    radial directions, so the raster shades it smooth; the pole bands collapse to fans with one degenerate triangle
//    each, which rasterises to nothing and intersects nothing.
export function EmitSphereFacets(Radius, Bands, Segments, Family)
{
    const Facets = [];
    const Corner = (Band, Segment) =>
    {
        const Theta = Math.PI * Band / Bands;
        const Phi = 2.0 * Math.PI * Segment / Segments;
        const Normal = [Math.sin(Theta) * Math.cos(Phi), Math.sin(Theta) * Math.sin(Phi), Math.cos(Theta)];
        return { Position: [Radius * Normal[0], Radius * Normal[1], Radius * (1.0 + Normal[2])], Normal };
    };
    for (let Band = 0; Band < Bands; ++Band)
    {
        for (let Segment = 0; Segment < Segments; ++Segment)
        {
            const P10 = Corner(Band + 1, Segment), P11 = Corner(Band + 1, Segment + 1), P01 = Corner(Band, Segment + 1);
            const P00 = Corner(Band, Segment);
            Facets.push({ Corners: [P10.Position, P11.Position, P01.Position],
                          Normals: [P10.Normal, P11.Normal, P01.Normal], Family });
            Facets.push({ Corners: [P10.Position, P01.Position, P00.Position],
                          Normals: [P10.Normal, P01.Normal, P00.Normal], Family });
        }
    }
    return Facets;
}

// 📝 A convex (y, z) profile, counter-clockwise seen from +X, extruded along x over Length and centred on x = 0.
export function EmitExtrusionFacets(Profile, Length, Family)
{
    const X = 0.5 * Length;
    const Facets = [];
    const Count = Profile.length;
    for (let Index = 0; Index < Count; ++Index)
    {
        const [Y0, Z0] = Profile[Index], [Y1, Z1] = Profile[(Index + 1) % Count];
        EmitQuad(Facets, [-X, Y0, Z0], [-X, Y1, Z1], [X, Y1, Z1], [X, Y0, Z0], Family);
    }
    for (let Index = 1; Index + 1 < Count; ++Index)
    {
        Facets.push({ Corners: [[X, ...Profile[0]], [X, ...Profile[Index]], [X, ...Profile[Index + 1]]], Family });
        Facets.push({ Corners: [[-X, ...Profile[0]], [-X, ...Profile[Index + 1]], [-X, ...Profile[Index]]], Family });
    }
    return Facets;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              STUNT PARK (FAR-EAST WING)
//------------------------------------------------------------------------------------------------------------------------

// 📝 The stunt park: a freeride skate-style wing east of the training arena (x 158..252) — deliberately NOT a track.
//    Three tabletop kickers (S/M/Big Air), a half-pipe, an above-grade bowl, a 45° banked ring, a 45° straight wall-ride
//    bank, a funbox, a whoops rhythm section, a bowl entry wedge and a jump-through stunt ring, scattered over open
//    asphalt with flat run-ups and landing corridors between them.
//
//    Physics contract: every driven surface is single-valued h(x, y) (ResolveStuntHeight, max-composite like the rest
//    of the course), so the struts, the hull and the debris read it exactly as they read the ramp and the kerbs. The
//    facet emitters below SAMPLE THE SAME functions for their vertices, and ResolveCourseNormal for their normals —
//    physics and visuals match by construction, to the grid sagitta (≤ 1 cm at the 0.5 m grid). Steep is fine (the
//    struts hold contact while the car tilts under ~75°); vertical and overhanging are not representable, so the
//    stunt ring is visual only (its pylons collide) and full loop-the-loop ceiling driving is out of level scope.
export const StuntBounds = Object.freeze({
    Minimum: Object.freeze([158.0, -58.0]),
    Maximum: Object.freeze([252.0, 58.0])
});

const StuntHalfPi = Math.PI / 180.0;

// 📝 Derived once: the bowl's rim (a 4.6 m circular transition to 70°). The entry wedge meets the rim deck exactly.
const StuntBowlRise = 4.6 * (1.0 - Math.cos(70.0 * StuntHalfPi));   // [m] ≈ 3.027

// 📝 The single source of truth: the heightfield, the facet emitters and the Jolt proxies all read this table.
//    Tabletops launch +X: quad up over Up, flat Deck, quad down over Down. HalfPipe/StraightBank taper to the flat
//    over Taper at both X ends. Bowl/BankRing are radial about Centre. Funbox is a flat deck with chamfer Run on all
//    four sides. Whoops are sin² bumps of wavelength Length with a 1 m edge fade. EntryWedge falls from Height at YHigh
//    to the flat at YLow. Ring is visual only.
export const StuntPark = Object.freeze({
    Tabletops: Object.freeze([
        Object.freeze({ Name: "Tabletop S", X0: 186.0, Up: 7.0, Height: 1.4, Deck: 3.0, Down: 9.0,  CentreY: -38.0, HalfWidth: 3.0 }),
        Object.freeze({ Name: "Tabletop M", X0: 184.0, Up: 8.0, Height: 2.2, Deck: 3.5, Down: 11.0, CentreY: -28.0, HalfWidth: 3.0 }),
        Object.freeze({ Name: "Tabletop L", X0: 182.0, Up: 9.0, Height: 3.2, Deck: 4.0, Down: 14.0, CentreY: -17.0, HalfWidth: 4.0 })
    ]),
    HalfPipe:     Object.freeze({ X0: 198.0, X1: 222.0, CentreY: 38.0, Floor: 3.0, Radius: 3.6, Angle: 70.0, Deck: 2.5, Chamfer: 3.0, Taper: 3.0 }),
    Bowl:         Object.freeze({ Centre: Object.freeze([230.0, 12.0]), Floor: 6.0, Radius: 4.6, Angle: 70.0, DeckRadius: 13.0 }),
    BankRing:     Object.freeze({ Centre: Object.freeze([192.0, 10.0]), Inner: 7.0, Run: 4.0, Height: 2.7, DeckRadius: 13.0 }),
    StraightBank: Object.freeze({ X0: 162.0, X1: 196.0, ToeY: -14.0, CrestY: -10.0, Height: 2.7, DeckToY: -8.0, ChamferToY: -5.3, Taper: 3.0 }),
    Funbox:       Object.freeze({ MinimumX: 225.0, MaximumX: 231.0, MinimumY: -43.0, MaximumY: -37.0, Height: 1.6, Run: 2.5 }),
    Whoops:       Object.freeze({ X0: 162.0, X1: 192.0, Y0: -52.0, Y1: -46.0, Amplitude: 0.35, Length: 6.0 }),
    EntryWedge:   Object.freeze({ X0: 226.5, X1: 233.5, YHigh: 26.0, YLow: 37.0, Height: StuntBowlRise }),
    Ring:         Object.freeze({ Centre: Object.freeze([214.0, -17.0, 9.0]), Radius: 5.5, Tube: 0.5 })
});

function StuntSmooth01(T)
{
    const Clamped = T <= 0.0 ? 0.0 : T >= 1.0 ? 1.0 : T;
    return Clamped * Clamped * (3.0 - 2.0 * Clamped);
}

// 📝 Circular vert-ramp transition: flat at Offset 0, lip angle Angle at Offset Run. Height(Offset), with Run/Rise out.
function StuntTransition(Offset, Radius, AngleRadians)
{
    const U = Math.min(Math.max(Offset / Radius, 0.0), Math.sin(AngleRadians));
    return Radius * (1.0 - Math.sqrt(Math.max(0.0, 1.0 - U * U)));
}

function ResolveTabletopHeight(Feature, X, Y)
{
    if (Y < Feature.CentreY - Feature.HalfWidth || Y > Feature.CentreY + Feature.HalfWidth)
        return 0.0;
    const Lip = Feature.X0 + Feature.Up, DeckEnd = Lip + Feature.Deck, Exit = DeckEnd + Feature.Down;
    if (X < Feature.X0 || X > Exit)
        return 0.0;
    if (X < Lip)
    {
        const S = (X - Feature.X0) / Feature.Up;
        return Feature.Height * S * S;
    }
    if (X < DeckEnd)
        return Feature.Height;
    const S = (X - DeckEnd) / Feature.Down;
    return Feature.Height * (1.0 - S * S);
}

function ResolveHalfPipeHeight(Feature, X, Y)
{
    if (X < Feature.X0 || X > Feature.X1)
        return 0.0;
    const Envelope = StuntSmooth01((X - Feature.X0) / Feature.Taper) * StuntSmooth01((Feature.X1 - X) / Feature.Taper);
    if (Envelope <= 0.0)
        return 0.0;
    const Angle = Feature.Angle * StuntHalfPi;
    const Run = Feature.Radius * Math.sin(Angle), Rise = Feature.Radius * (1.0 - Math.cos(Angle));
    const Distance = Math.abs(Y - Feature.CentreY);
    if (Distance <= Feature.Floor)
        return 0.0;
    const WallOuter = Feature.Floor + Run, DeckOuter = WallOuter + Feature.Deck;
    if (Distance <= WallOuter)
        return StuntTransition(Distance - Feature.Floor, Feature.Radius, Angle) * Envelope;
    if (Distance <= DeckOuter)
        return Rise * Envelope;
    if (Distance <= DeckOuter + Feature.Chamfer)
        return Rise * (1.0 - (Distance - DeckOuter) / Feature.Chamfer) * Envelope;
    return 0.0;
}

function ResolveBowlHeight(Feature, X, Y)
{
    const Angle = Feature.Angle * StuntHalfPi;
    const Run = Feature.Radius * Math.sin(Angle), Rise = Feature.Radius * (1.0 - Math.cos(Angle));
    const Rim = Feature.Floor + Run, Outer = Feature.DeckRadius + Rise;
    const Radius = Math.hypot(X - Feature.Centre[0], Y - Feature.Centre[1]);
    if (Radius <= Feature.Floor || Radius >= Outer)
        return 0.0;
    if (Radius <= Rim)
        return StuntTransition(Radius - Feature.Floor, Feature.Radius, Angle);
    if (Radius <= Feature.DeckRadius)
        return Rise;
    return Rise * (1.0 - (Radius - Feature.DeckRadius) / Rise);
}

function ResolveBankRingHeight(Feature, X, Y)
{
    const Crest = Feature.Inner + Feature.Run, Outer = Feature.DeckRadius + Feature.Height;
    const Radius = Math.hypot(X - Feature.Centre[0], Y - Feature.Centre[1]);
    if (Radius <= Feature.Inner || Radius >= Outer)
        return 0.0;
    if (Radius <= Crest)
        return Feature.Height * StuntSmooth01((Radius - Feature.Inner) / Feature.Run);
    if (Radius <= Feature.DeckRadius)
        return Feature.Height;
    return Feature.Height * (1.0 - (Radius - Feature.DeckRadius) / Feature.Height);
}

function ResolveStraightBankHeight(Feature, X, Y)
{
    if (X < Feature.X0 || X > Feature.X1 || Y < Feature.ToeY || Y > Feature.ChamferToY)
        return 0.0;
    const Envelope = StuntSmooth01((X - Feature.X0) / Feature.Taper) * StuntSmooth01((Feature.X1 - X) / Feature.Taper);
    if (Envelope <= 0.0)
        return 0.0;
    if (Y <= Feature.CrestY)
        return Feature.Height * StuntSmooth01((Y - Feature.ToeY) / (Feature.CrestY - Feature.ToeY)) * Envelope;
    if (Y <= Feature.DeckToY)
        return Feature.Height * Envelope;
    return Feature.Height * (1.0 - (Y - Feature.DeckToY) / (Feature.ChamferToY - Feature.DeckToY)) * Envelope;
}

function ResolveFunboxHeight(Feature, X, Y)
{
    const OutsideX = Math.max(Feature.MinimumX - X, 0.0, X - Feature.MaximumX);
    const OutsideY = Math.max(Feature.MinimumY - Y, 0.0, Y - Feature.MaximumY);
    const Distance = Math.hypot(OutsideX, OutsideY);
    if (Distance >= Feature.Run)
        return 0.0;
    return Feature.Height * (1.0 - Distance / Feature.Run);
}

function ResolveWhoopsHeight(Feature, X, Y)
{
    if (X < Feature.X0 || X > Feature.X1 || Y < Feature.Y0 || Y > Feature.Y1)
        return 0.0;
    const Wave = Math.sin(Math.PI * (X - Feature.X0) / Feature.Length);
    const Fade = StuntSmooth01((Y - Feature.Y0) / 1.0) * StuntSmooth01((Feature.Y1 - Y) / 1.0);
    return Feature.Amplitude * Wave * Wave * Fade;
}

function ResolveEntryWedgeHeight(Feature, X, Y)
{
    if (X < Feature.X0 || X > Feature.X1 || Y < Feature.YHigh || Y > Feature.YLow)
        return 0.0;
    return Feature.Height * (1.0 - StuntSmooth01((Y - Feature.YHigh) / (Feature.YLow - Feature.YHigh)));
}

// 📝 The stunt park's relief: the max over every feature (the ring is visual only — it has no height).
export function ResolveStuntHeight(X, Y)
{
    const Park = StuntPark;
    let Height = 0.0;
    for (const Tabletop of Park.Tabletops)
        Height = Math.max(Height, ResolveTabletopHeight(Tabletop, X, Y));
    Height = Math.max(Height, ResolveHalfPipeHeight(Park.HalfPipe, X, Y));
    Height = Math.max(Height, ResolveBowlHeight(Park.Bowl, X, Y));
    Height = Math.max(Height, ResolveBankRingHeight(Park.BankRing, X, Y));
    Height = Math.max(Height, ResolveStraightBankHeight(Park.StraightBank, X, Y));
    Height = Math.max(Height, ResolveFunboxHeight(Park.Funbox, X, Y));
    Height = Math.max(Height, ResolveWhoopsHeight(Park.Whoops, X, Y));
    Height = Math.max(Height, ResolveEntryWedgeHeight(Park.EntryWedge, X, Y));
    return Height;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              STUNT FACET EMISSION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Grid samplers: each emitter samples ITS OWN feature height (never the max-composite), so every surface stays
//    smooth where features overlap — the higher one wins by depth, exactly as the physics max-composite says — and
//    creases shade honestly from each surface's own normal. All-zero quads are skipped: the flat ground cells cover them.
const StuntGridStep = 0.5;   // [m]

function StuntFeatureNormal(HeightAt, X, Y)
{
    const Step = 0.15;
    const SlopeX = (HeightAt(X + Step, Y) - HeightAt(X - Step, Y)) / (2.0 * Step);
    const SlopeY = (HeightAt(X, Y + Step) - HeightAt(X, Y - Step)) / (2.0 * Step);
    const Length = Math.hypot(SlopeX, SlopeY, 1.0);
    return [-SlopeX / Length, -SlopeY / Length, 1.0 / Length];
}

function StuntCorner(HeightAt, X, Y)
{
    return { Position: [X, Y, HeightAt(X, Y)], Normal: StuntFeatureNormal(HeightAt, X, Y) };
}

function EmitStuntGrid(Facets, HeightAt, X0, X1, Y0, Y1, FamilyAt)
{
    const StepsX = Math.max(1, Math.round((X1 - X0) / StuntGridStep));
    const StepsY = Math.max(1, Math.round((Y1 - Y0) / StuntGridStep));
    const StepX = (X1 - X0) / StepsX, StepY = (Y1 - Y0) / StepsY;
    for (let Ix = 0; Ix < StepsX; ++Ix)
    {
        for (let Iy = 0; Iy < StepsY; ++Iy)
        {
            const A = StuntCorner(HeightAt, X0 + Ix * StepX, Y0 + Iy * StepY);
            const B = StuntCorner(HeightAt, X0 + (Ix + 1) * StepX, Y0 + Iy * StepY);
            const C = StuntCorner(HeightAt, X0 + (Ix + 1) * StepX, Y0 + (Iy + 1) * StepY);
            const D = StuntCorner(HeightAt, X0 + Ix * StepX, Y0 + (Iy + 1) * StepY);
            if (A.Position[2] <= 0.001 && B.Position[2] <= 0.001 && C.Position[2] <= 0.001 && D.Position[2] <= 0.001)
                continue;
            const Family = FamilyAt(0.5 * (A.Position[0] + C.Position[0]), 0.5 * (A.Position[1] + C.Position[1]));
            Facets.push({ Corners: [A.Position, B.Position, C.Position], Normals: [A.Normal, B.Normal, C.Normal], Family });
            Facets.push({ Corners: [A.Position, C.Position, D.Position], Normals: [A.Normal, C.Normal, D.Normal], Family });
        }
    }
}

function EmitStuntRadial(Facets, HeightAt, CentreX, CentreY, R0, R1, Sectors, FamilyAt)
{
    const Rings = Math.max(1, Math.round((R1 - R0) / StuntGridStep));
    const StepR = (R1 - R0) / Rings;
    for (let Ring = 0; Ring < Rings; ++Ring)
    {
        const Ra = R0 + Ring * StepR, Rb = Ra + StepR;
        for (let Sector = 0; Sector < Sectors; ++Sector)
        {
            const A0 = 2.0 * Math.PI * Sector / Sectors, A1 = 2.0 * Math.PI * (Sector + 1) / Sectors;
            const P00 = StuntCorner(HeightAt, CentreX + Ra * Math.cos(A0), CentreY + Ra * Math.sin(A0));
            const P01 = StuntCorner(HeightAt, CentreX + Rb * Math.cos(A0), CentreY + Rb * Math.sin(A0));
            const P11 = StuntCorner(HeightAt, CentreX + Rb * Math.cos(A1), CentreY + Rb * Math.sin(A1));
            const P10 = StuntCorner(HeightAt, CentreX + Ra * Math.cos(A1), CentreY + Ra * Math.sin(A1));
            if (P00.Position[2] <= 0.001 && P01.Position[2] <= 0.001 && P11.Position[2] <= 0.001 && P10.Position[2] <= 0.001)
                continue;
            const MidR = 0.5 * (Ra + Rb), MidA = 0.5 * (A0 + A1);
            const Family = FamilyAt(CentreX + MidR * Math.cos(MidA), CentreY + MidR * Math.sin(MidA));
            Facets.push({ Corners: [P00.Position, P01.Position, P11.Position],
                          Normals: [P00.Normal, P01.Normal, P11.Normal], Family });
            Facets.push({ Corners: [P00.Position, P11.Position, P10.Position],
                          Normals: [P00.Normal, P11.Normal, P10.Normal], Family });
        }
    }
}

// 📝 Vertical side skirts along hard edges (the top grid ends at the edge; the skirt closes the side). A strip whose
//    far end tapers to the flat is one triangle, never a degenerate quad.
function EmitSkirtStrip(Facets, Normal, Base0, Top0, Base1, Top1)
{
    const N = [Normal, Normal, Normal];
    const Flat0 = Top0[2] <= 0.001, Flat1 = Top1[2] <= 0.001;
    if (Flat0 && Flat1)
        return;
    if (Flat0)
        Facets.push({ Corners: [Base0, Top1, Base1], Normals: N, Family: CourseMaterial.Ramp });
    else if (Flat1)
        Facets.push({ Corners: [Base0, Top0, Base1], Normals: N, Family: CourseMaterial.Ramp });
    else
    {
        Facets.push({ Corners: [Base0, Top0, Top1], Normals: N, Family: CourseMaterial.Ramp });
        Facets.push({ Corners: [Base0, Top1, Base1], Normals: N, Family: CourseMaterial.Ramp });
    }
}

function EmitTabletopSkirts(Facets, Feature, ExitX)
{
    for (const [EdgeY, Sign] of [[Feature.CentreY - Feature.HalfWidth, -1.0], [Feature.CentreY + Feature.HalfWidth, 1.0]])
    {
        for (let X = Feature.X0; X < ExitX - 1.0e-6; X += StuntGridStep)
        {
            const X1 = Math.min(X + StuntGridStep, ExitX);
            const H0 = ResolveTabletopHeight(Feature, X, EdgeY), H1 = ResolveTabletopHeight(Feature, X1, EdgeY);
            const Normal = [0.0, Sign, 0.0];
            if (Sign < 0.0)
                EmitSkirtStrip(Facets, Normal, [X1, EdgeY, 0.0], [X1, EdgeY, H1], [X, EdgeY, 0.0], [X, EdgeY, H0]);
            else
                EmitSkirtStrip(Facets, Normal, [X, EdgeY, 0.0], [X, EdgeY, H0], [X1, EdgeY, 0.0], [X1, EdgeY, H1]);
        }
    }
}

function EmitEntryWedgeSkirts(Facets, Feature)
{
    for (const [EdgeX, Sign] of [[Feature.X0, -1.0], [Feature.X1, 1.0]])
    {
        for (let Y = Feature.YHigh; Y < Feature.YLow - 1.0e-6; Y += StuntGridStep)
        {
            const Y1 = Math.min(Y + StuntGridStep, Feature.YLow);
            const H0 = ResolveEntryWedgeHeight(Feature, EdgeX, Y), H1 = ResolveEntryWedgeHeight(Feature, EdgeX, Y1);
            const Normal = [Sign, 0.0, 0.0];
            if (Sign < 0.0)
                EmitSkirtStrip(Facets, Normal, [EdgeX, Y, 0.0], [EdgeX, Y, H0], [EdgeX, Y1, 0.0], [EdgeX, Y1, H1]);
            else
                EmitSkirtStrip(Facets, Normal, [EdgeX, Y1, 0.0], [EdgeX, Y1, H1], [EdgeX, Y, 0.0], [EdgeX, Y, H0]);
        }
    }
}

// 📝 World-space emitters (the Ramp precedent): each placement rides the identity, the table carries the positions.
//    The entry wedge's top row tucks half a metre under the bowl's rim deck instead of meeting it coplanar.
export function EmitStuntTabletopFacets(Index)
{
    const Feature = StuntPark.Tabletops[Index];
    const Facets = [];
    const Lip = Feature.X0 + Feature.Up, DeckEnd = Lip + Feature.Deck, Exit = DeckEnd + Feature.Down;
    const HeightAt = (X, Y) => ResolveTabletopHeight(Feature, X, Y);
    EmitStuntGrid(Facets, HeightAt, Feature.X0 - 1.0, Exit + 1.0, Feature.CentreY - Feature.HalfWidth, Feature.CentreY + Feature.HalfWidth,
        (X) => Math.abs(X - Lip) < 0.3 ? CourseMaterial.PaintYellow
             : X >= Lip && X <= DeckEnd ? CourseMaterial.ApronConcrete : CourseMaterial.Ramp);
    EmitTabletopSkirts(Facets, Feature, Exit);
    return Facets;
}

export function EmitStuntHalfPipeFacets()
{
    const Feature = StuntPark.HalfPipe;
    const Facets = [];
    const Angle = Feature.Angle * StuntHalfPi;
    const Half = Feature.Floor + Feature.Radius * Math.sin(Angle) + Feature.Deck + Feature.Chamfer + 1.0;
    EmitStuntGrid(Facets, (X, Y) => ResolveHalfPipeHeight(Feature, X, Y),
        Feature.X0 - 1.0, Feature.X1 + 1.0, Feature.CentreY - Half, Feature.CentreY + Half, () => CourseMaterial.Ramp);
    return Facets;
}

export function EmitStuntBowlFacets()
{
    const Feature = StuntPark.Bowl;
    const Facets = [];
    const Angle = Feature.Angle * StuntHalfPi;
    const Outer = Feature.DeckRadius + Feature.Radius * (1.0 - Math.cos(Angle)) + 1.0;
    EmitStuntRadial(Facets, (X, Y) => ResolveBowlHeight(Feature, X, Y),
        Feature.Centre[0], Feature.Centre[1], Feature.Floor - 1.0, Outer, 72, () => CourseMaterial.Ramp);
    return Facets;
}

export function EmitStuntBankRingFacets()
{
    const Feature = StuntPark.BankRing;
    const Facets = [];
    EmitStuntRadial(Facets, (X, Y) => ResolveBankRingHeight(Feature, X, Y),
        Feature.Centre[0], Feature.Centre[1], Feature.Inner - 1.0, Feature.DeckRadius + Feature.Height + 1.0, 72,
        () => CourseMaterial.Ramp);
    return Facets;
}

export function EmitStuntStraightBankFacets()
{
    const Feature = StuntPark.StraightBank;
    const Facets = [];
    EmitStuntGrid(Facets, (X, Y) => ResolveStraightBankHeight(Feature, X, Y),
        Feature.X0 - 1.0, Feature.X1 + 1.0, Feature.ToeY - 1.0, Feature.ChamferToY + 1.0, () => CourseMaterial.Ramp);
    return Facets;
}

export function EmitStuntFunboxFacets()
{
    const Feature = StuntPark.Funbox;
    const Facets = [];
    EmitStuntGrid(Facets, (X, Y) => ResolveFunboxHeight(Feature, X, Y),
        Feature.MinimumX - Feature.Run - 1.0, Feature.MaximumX + Feature.Run + 1.0,
        Feature.MinimumY - Feature.Run - 1.0, Feature.MaximumY + Feature.Run + 1.0,
        (X, Y) => X >= Feature.MinimumX && X <= Feature.MaximumX && Y >= Feature.MinimumY && Y <= Feature.MaximumY
                  ? CourseMaterial.ApronConcrete : CourseMaterial.Ramp);
    return Facets;
}

export function EmitStuntWhoopsFacets()
{
    const Feature = StuntPark.Whoops;
    const Facets = [];
    EmitStuntGrid(Facets, (X, Y) => ResolveWhoopsHeight(Feature, X, Y),
        Feature.X0 - 1.0, Feature.X1 + 1.0, Feature.Y0 - 1.0, Feature.Y1 + 1.0, () => CourseMaterial.Ramp);
    return Facets;
}

export function EmitStuntEntryWedgeFacets()
{
    const Feature = StuntPark.EntryWedge;
    const Facets = [];
    EmitStuntGrid(Facets, (X, Y) => ResolveEntryWedgeHeight(Feature, X, Y),
        Feature.X0, Feature.X1, Feature.YHigh + 0.5, Feature.YLow, () => CourseMaterial.Ramp);
    EmitEntryWedgeSkirts(Facets, Feature);
    return Facets;
}

// 📝 The jump-through stunt ring: a torus standing in the Y–Z plane past the Big Air lip (visual only — the pylons
//    collide, the tube does not, so a clean flight threads the middle and the ground line passes underneath).
export function EmitStuntRingFacets()
{
    const Ring = StuntPark.Ring;
    const Facets = [];
    const ArcSegments = 28, TubeSegments = 8;
    const Corner = (Arc, Tube) =>
    {
        const Theta = 2.0 * Math.PI * Arc / ArcSegments, Phi = 2.0 * Math.PI * Tube / TubeSegments;
        const Radial = [0.0, Math.cos(Theta), Math.sin(Theta)];
        const Normal = [Math.cos(Phi), Math.sin(Phi) * Radial[1], Math.sin(Phi) * Radial[2]];
        return { Position: [Ring.Centre[0] + Ring.Tube * Normal[0],
                            Ring.Centre[1] + Ring.Radius * Radial[1] + Ring.Tube * Normal[1],
                            Ring.Centre[2] + Ring.Radius * Radial[2] + Ring.Tube * Normal[2]], Normal };
    };
    for (let Arc = 0; Arc < ArcSegments; ++Arc)
    {
        const Family = (Arc >> 1) & 1 ? CourseMaterial.PaintWhite : CourseMaterial.PaintYellow;
        for (let Tube = 0; Tube < TubeSegments; ++Tube)
        {
            const P00 = Corner(Arc, Tube), P01 = Corner(Arc, Tube + 1), P11 = Corner(Arc + 1, Tube + 1), P10 = Corner(Arc + 1, Tube);
            Facets.push({ Corners: [P00.Position, P01.Position, P11.Position],
                          Normals: [P00.Normal, P01.Normal, P11.Normal], Family });
            Facets.push({ Corners: [P00.Position, P11.Position, P10.Position],
                          Normals: [P00.Normal, P11.Normal, P10.Normal], Family });
        }
    }
    return Facets;
}

//------------------------------------------------------------------------------------------------------------------------
//                                         STUNT PARK JOLT PROXIES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Loose bodies (cones, debris) integrate in Jolt against its own statics — the analytic course means nothing there —
//    so every stunt feature the car drives gets a coarse convex proxy: wedges and chords, each an 8-corner cloud the
//    hull builder turns into a static body. Debris rests on chords instead of curves; nothing driven reads these.
function StuntChordCorners(X0, Y0, H0, X1, Y1, H1, Base = -0.3)
{
    return [[X0, Y0, H0], [X0, Y0, Base], [X0, Y1, H0], [X0, Y1, Base],
            [X1, Y0, H1], [X1, Y0, Base], [X1, Y1, H1], [X1, Y1, Base]];
}

// 📝 The same chord with the slope running along Y instead of X (half-pipe walls, the straight bank, the wedge).
function StuntChordYCorners(X0, Y0, H0, X1, Y1, H1, Base = -0.3)
{
    return [[X0, Y0, H0], [X0, Y0, Base], [X1, Y0, H0], [X1, Y0, Base],
            [X0, Y1, H1], [X0, Y1, Base], [X1, Y1, H1], [X1, Y1, Base]];
}

export function ResolveStuntJoltBoxes()
{
    const Park = StuntPark;
    const Boxes = [];
    for (const Feature of Park.Tabletops)
    {
        const Lip = Feature.X0 + Feature.Up, DeckEnd = Lip + Feature.Deck, Exit = DeckEnd + Feature.Down;
        const Low = Feature.CentreY - Feature.HalfWidth, High = Feature.CentreY + Feature.HalfWidth;
        Boxes.push(StuntChordCorners(Feature.X0, Low, 0.0, Lip, High, Feature.Height));
        Boxes.push(StuntChordCorners(Lip, Low, Feature.Height, DeckEnd, High, Feature.Height));
        Boxes.push(StuntChordCorners(DeckEnd, Low, Feature.Height, Exit, High, 0.0));
    }
    {
        // 📝 Half-pipe: per wall a lower chord, an upper chord, the deck and the chamfer, over the untapered middle.
        const Feature = Park.HalfPipe;
        const Angle = Feature.Angle * StuntHalfPi;
        const Run = Feature.Radius * Math.sin(Angle), Rise = Feature.Radius * (1.0 - Math.cos(Angle));
        const Mid = Feature.Floor + 0.5 * Run, MidH = StuntTransition(0.5 * Run, Feature.Radius, Angle);
        const WallOuter = Feature.Floor + Run, DeckOuter = WallOuter + Feature.Deck;
        const X0 = Feature.X0 + Feature.Taper, X1 = Feature.X1 - Feature.Taper;
        for (const Sign of [-1.0, 1.0])
        {
            const At = (Distance) => Feature.CentreY + Sign * Distance;
            Boxes.push(StuntChordYCorners(X0, At(Feature.Floor), 0.0, X1, At(Mid), MidH));
            Boxes.push(StuntChordYCorners(X0, At(Mid), MidH, X1, At(WallOuter), Rise));
            Boxes.push(StuntChordYCorners(X0, At(WallOuter), Rise, X1, At(DeckOuter), Rise));
            Boxes.push(StuntChordYCorners(X0, At(DeckOuter), Rise, X1, At(DeckOuter + Feature.Chamfer), 0.0));
        }
    }
    {
        // 📝 Bowl and bank ring: eight sectors, each a transition chord plus a deck/chamfer chord.
        const Radial = (Centre, Inner, Crest, CrestH, Outer) =>
        {
            for (let Sector = 0; Sector < 8; ++Sector)
            {
                const A0 = Math.PI * Sector / 4.0, A1 = A0 + Math.PI / 4.0;
                const On = (Radius, Angle) => [Centre[0] + Radius * Math.cos(Angle), Centre[1] + Radius * Math.sin(Angle)];
                const Chord = (R0, H0, R1, H1) =>
                {
                    const P00 = On(R0, A0), P01 = On(R0, A1), P10 = On(R1, A0), P11 = On(R1, A1);
                    Boxes.push([[P00[0], P00[1], H0], [P00[0], P00[1], -0.3], [P01[0], P01[1], H0], [P01[0], P01[1], -0.3],
                                [P10[0], P10[1], H1], [P10[0], P10[1], -0.3], [P11[0], P11[1], H1], [P11[0], P11[1], -0.3]]);
                };
                Chord(Inner, 0.0, Crest, CrestH);
                Chord(Crest, CrestH, Outer, 0.0);
            }
        };
        const BowlAngle = Park.Bowl.Angle * StuntHalfPi;
        const BowlRun = Park.Bowl.Radius * Math.sin(BowlAngle), BowlRise = Park.Bowl.Radius * (1.0 - Math.cos(BowlAngle));
        Radial(Park.Bowl.Centre, Park.Bowl.Floor, Park.Bowl.Floor + BowlRun, BowlRise, Park.Bowl.DeckRadius + BowlRise);
        Radial(Park.BankRing.Centre, Park.BankRing.Inner, Park.BankRing.Inner + Park.BankRing.Run, Park.BankRing.Height,
               Park.BankRing.DeckRadius + Park.BankRing.Height);
    }
    {
        // 📝 Straight bank: the bank chord, the deck and the chamfer, over the untapered middle.
        const Feature = Park.StraightBank;
        const X0 = Feature.X0 + Feature.Taper, X1 = Feature.X1 - Feature.Taper;
        Boxes.push(StuntChordYCorners(X0, Feature.ToeY, 0.0, X1, Feature.CrestY, Feature.Height));
        Boxes.push(StuntChordYCorners(X0, Feature.CrestY, Feature.Height, X1, Feature.DeckToY, Feature.Height));
        Boxes.push(StuntChordYCorners(X0, Feature.DeckToY, Feature.Height, X1, Feature.ChamferToY, 0.0));
    }
    {
        // 📝 Funbox: the deck plus one chamfer chord per side (the conical corners read as flat ground to debris).
        const Feature = Park.Funbox;
        Boxes.push(StuntChordCorners(Feature.MinimumX, Feature.MinimumY, Feature.Height,
                                     Feature.MaximumX, Feature.MaximumY, Feature.Height));
        Boxes.push(StuntChordCorners(Feature.MinimumX - Feature.Run, Feature.MinimumY, 0.0,
                                     Feature.MinimumX, Feature.MaximumY, Feature.Height));
        Boxes.push(StuntChordCorners(Feature.MaximumX, Feature.MinimumY, Feature.Height,
                                     Feature.MaximumX + Feature.Run, Feature.MaximumY, 0.0));
        Boxes.push(StuntChordYCorners(Feature.MinimumX, Feature.MinimumY - Feature.Run, 0.0,
                                      Feature.MaximumX, Feature.MinimumY, Feature.Height));
        Boxes.push(StuntChordYCorners(Feature.MinimumX, Feature.MaximumY, Feature.Height,
                                      Feature.MaximumX, Feature.MaximumY + Feature.Run, 0.0));
    }
    {
        // 📝 Entry wedge: one chord (the whoops are low enough to read as flat ground to debris).
        const Feature = Park.EntryWedge;
        Boxes.push(StuntChordYCorners(Feature.X0, Feature.YHigh, Feature.Height, Feature.X1, Feature.YLow, 0.0));
    }
    return Boxes;
}
