// Volumetric authored tyres, pressure-driven deformation, damage visibility and cabin articulation; shared by all raster paths.
struct VehicleRecord {
    Cabin : vec4f, // steering angle, see-through glass, reserved, lamp
    Column : vec4f,
    Axis : vec4f,
    CabinFrame : vec4f,
    CabinShift : vec4f, // tablet centre xyz, tablet-only scale w
    Wheels : array<vec4f, 4>, // x = shredded; yzw reserved
    Deformation : array<vec4f, 4>, // contact angle, deflection, pressure, lateral distortion
    RadialDisplacement : array<vec4f, 32>, // four wheels × 32 radial particles, packed as vec4
    ContinuumDisplacement : array<vec4f, 448>, // four wheels × 16 angles × 7 section points
}

const TyreRimRadius: f32 = 0.3448;
const TyreOuterRadius: f32 = 0.50855;
const PI: f32 = 3.141592653589793;

struct DeformedVertex { Position : vec3f, Normal : vec3f, }

// Interpolates all three solved displacement components over the tyre cross-section and circumference.
// The same mapping is used by raster, shadows, mirrors and software triangle intersections.
fn EvaluateContinuumShift(Wheel: u32, Position: vec3f, ContactAngle: f32) -> vec3f {
    let Section = array<vec2f, 7>(vec2f(0.3448, -0.105), vec2f(0.395, -0.145), vec2f(0.478, -0.145),
        vec2f(0.50855, 0.0), vec2f(0.478, 0.145), vec2f(0.395, 0.145), vec2f(0.3448, 0.105));
    let Radius = length(Position.xz);
    let Phi = atan2(Position.z, Position.x);
    let Coordinate = fract((Phi - ContactAngle) / (2.0 * PI) + 2.0) * 16.0;
    let Low = u32(floor(Coordinate)) % 16u;
    let High = (Low + 1u) % 16u;
    let Point = vec2f(Radius, Position.y);
    var Nearest = 1e20;
    var Segment = 0u;
    var Blend = 0.0;
    for (var Index = 0u; Index < 6u; Index++) {
        let Edge = Section[Index + 1u] - Section[Index];
        let Along = clamp(dot(Point - Section[Index], Edge) / dot(Edge, Edge), 0.0, 1.0);
        let Delta = Point - (Section[Index] + Along * Edge);
        let Distance = dot(Delta, Delta);
        if (Distance < Nearest) { Nearest = Distance; Segment = Index; Blend = Along; }
    }
    let Offset = Wheel * 112u;
    let A = mix(VehicleState.ContinuumDisplacement[Offset + Low * 7u + Segment].xyz,
        VehicleState.ContinuumDisplacement[Offset + Low * 7u + Segment + 1u].xyz, Blend);
    let B = mix(VehicleState.ContinuumDisplacement[Offset + High * 7u + Segment].xyz,
        VehicleState.ContinuumDisplacement[Offset + High * 7u + Segment + 1u].xyz, Blend);
    let Shift = mix(A, B, fract(Coordinate));
    return vec3f(Shift.x * cos(Phi) - Shift.z * sin(Phi), Shift.y, Shift.x * sin(Phi) + Shift.z * cos(Phi));
}

// Radial XPBD displacements when enabled; legacy analytic deformation otherwise.
// Evaluates radial contact patch flattening, volumetric sidewall bulges (approximating sidewall expansion),
// shoulder expansion waves, lateral slip shear and internal inflation pressure stiffening in real time.
fn ResolveWheelVertex(Wheel: u32, Family: u32, Position: vec3f, Normal: vec3f) -> DeformedVertex {
    var Result: DeformedVertex;
    Result.Position = Position;
    Result.Normal = Normal;

    // All vertices of a shredded rubber facet collapse to the axle: no rubber or rubber shadow.
    // Rim/hub/brake facets and all occupant steering/spin/suspension transforms stay intact.
    if (Family == TyreFamily && VehicleState.Wheels[Wheel].x > 0.5) {
        Result.Position = vec3f(0.0);
        return Result;
    }

    if (Family != TyreFamily) {
        return Result;
    }

    let Deform = VehicleState.Deformation[Wheel];
    let Deflection = Deform.y;
    if (Deflection <= 1.0e-5) {
        return Result;
    }

    let ContactAngle = Deform.x;
    let Pressure = max(0.0, Deform.z);
    let LateralDistortion = Deform.w;

    let R = length(Position.xz);
    if (R <= TyreRimRadius + 1.0e-4) {
        return Result;
    }

    let Phi = atan2(Position.z, Position.x);
    var DPhi = Phi - ContactAngle;
    DPhi = DPhi - 2.0 * PI * round(DPhi / (2.0 * PI));

    if (VehicleState.Wheels[Wheel].y > 1.5)
    {
        let Shift = EvaluateContinuumShift(Wheel, Position, ContactAngle);
        Result.Position = Position + Shift;
        let UnitNormal = normalize(Normal);
        let ReferenceAxis = select(vec3f(1.0, 0.0, 0.0), vec3f(0.0, 1.0, 0.0), abs(UnitNormal.x) > 0.8);
        let First = normalize(cross(UnitNormal, ReferenceAxis));
        let Second = cross(UnitNormal, First);
        let Epsilon = 0.0005;
        let TangentFirst = First + (EvaluateContinuumShift(Wheel, Position + First * Epsilon, ContactAngle) - Shift) / Epsilon;
        let TangentSecond = Second + (EvaluateContinuumShift(Wheel, Position + Second * Epsilon, ContactAngle) - Shift) / Epsilon;
        Result.Normal = normalize(cross(TangentFirst, TangentSecond));
        return Result;
    }

    if (VehicleState.Wheels[Wheel].y > 0.5)
    {
        let Coordinate = ((DPhi / (2.0 * PI) + 1.0) % 1.0) * 32.0;
        let Low = u32(floor(Coordinate)) % 32u;
        let High = (Low + 1u) % 32u;
        let A = VehicleState.RadialDisplacement[Wheel * 8u + Low / 4u][Low % 4u];
        let B = VehicleState.RadialDisplacement[Wheel * 8u + High / 4u][High % 4u];
        let Inward = mix(A, B, fract(Coordinate));
        let Fraction = clamp((R - TyreRimRadius) / (TyreOuterRadius - TyreRimRadius), 0.0, 1.0);
        let NewRadius = max(TyreRimRadius, R - Inward * Fraction);
        Result.Position = vec3f(NewRadius * cos(Phi), Position.y, NewRadius * sin(Phi));
        let Slope = -(B - A) * 32.0 / (2.0 * PI) * Fraction;
        Result.Normal = normalize(Normal - vec3f(-sin(Phi), 0.0, cos(Phi)) * Slope / max(R, 0.01));
        return Result;
    }

    // Internal pressure scaling:
    // Reference pressure is 220 kPa. Under higher pressure the contact patch is shorter and stiffer;
    // under lower pressure (deflation) the contact patch elongates and sidewalls bulge dramatically.
    let PNorm = max(0.05, Pressure / 220000.0);
    let PatchSpan = clamp(0.44 / sqrt(max(0.15, PNorm)), 0.20, 1.15);

    // Normalized radial depth: 0 at bead/rim, 1 at outer tread crown.
    let RadialFrac = clamp((R - TyreRimRadius) / (TyreOuterRadius - TyreRimRadius), 0.0, 1.0);

    var DR: f32 = 0.0;
    var DY: f32 = 0.0;
    let AbsDPhi = abs(DPhi);

    // ① Contact flattening constraint:
    if (AbsDPhi < PatchSpan) {
        let U = AbsDPhi / PatchSpan;
        let Envelope = 0.5 * (1.0 + cos(U * PI));
        DR = -Deflection * Envelope * RadialFrac;

        // ② Volumetric conservation (Poisson / Chaos Flesh hyperelastic continuum):
        // Compressing the rubber tyre radially in contact pushes the material outward along the sidewalls.
        let SidewallFactor = sin(RadialFrac * PI);
        let BulgeIntensity = 0.48 / (0.4 + 0.6 * sqrt(PNorm));
        DY = sign(Position.y) * (Deflection * BulgeIntensity * Envelope * SidewallFactor);
    }

    // ③ Volumetric wave expansion (bead/shoulder transition zone just outside contact):
    let WaveOuter = PatchSpan * 1.5;
    if (AbsDPhi >= PatchSpan && AbsDPhi < WaveOuter) {
        let V = (AbsDPhi - PatchSpan) / (WaveOuter - PatchSpan);
        let Wave = sin(V * PI) * (0.12 * Deflection * RadialFrac);
        DR = DR + Wave;
    }

    // ④ Lateral cornering slip / shear strain:
    if (AbsDPhi < PatchSpan * 1.2) {
        let UShear = AbsDPhi / (PatchSpan * 1.2);
        let ShearEnv = 0.5 * (1.0 + cos(UShear * PI));
        DY = DY + LateralDistortion * ShearEnv * RadialFrac;
    }

    let NewR = max(TyreRimRadius, R + DR);
    Result.Position = vec3f(NewR * cos(Phi), Position.y + DY, NewR * sin(Phi));

    // Tilt deformed normal to maintain accurate shading over the contact patch & bulging sidewalls:
    if (AbsDPhi < PatchSpan) {
        let Tilt = -sin((DPhi / PatchSpan) * PI) * (Deflection / (TyreOuterRadius * PatchSpan));
        Result.Normal = normalize(Normal + vec3f(-sin(Phi), 0.0, cos(Phi)) * Tilt);
    }

    return Result;
}

// 📐 Rodrigues: Vector turned by Angle about the unit Axis.
fn TurnAboutAxis(Vector: vec3f, Axis: vec3f, Angle: f32) -> vec3f
{
    let C = cos(Angle);
    let S = sin(Angle);
    return Vector * C + cross(Axis, Vector) * S + Axis * dot(Axis, Vector) * (1.0 - C);
}

// 📝 A steering-wheel facet (car-body frame) turned about the column by the wheel's angle.
fn TurnSteeringWheel(Position: vec3f, Normal: vec3f) -> DeformedVertex
{
    var Turned: DeformedVertex;
    let Axis = normalize(VehicleState.Axis.xyz);
    let Pivot = VehicleState.Column.xyz;
    Turned.Position = Pivot + TurnAboutAxis(Position - Pivot, Axis, VehicleState.Cabin.x);
    Turned.Normal = TurnAboutAxis(Normal, Axis, VehicleState.Cabin.x);
    return Turned;
}

fn PlaceTablet(Position: vec3f) -> vec3f {
    return VehicleState.CabinShift.xyz + (Position - VehicleState.CabinShift.xyz) * VehicleState.CabinShift.w;
}
// Only the steering assembly articulates. HUD sizing never moves physical cabin geometry.
fn DeformBodyVertex(Family: u32, Position: vec3f, Normal: vec3f) -> DeformedVertex
{
    var Moved: DeformedVertex;
    Moved.Position = Position;
    Moved.Normal = Normal;
    if (Family == TabletHousingFamily || Family == DisplayGlassFamily) { Moved.Position = PlaceTablet(Position); }
    if (Family == SteeringWheelFamily)
    {
        Moved = TurnSteeringWheel(Position, Normal);
    }
    return Moved;
}
