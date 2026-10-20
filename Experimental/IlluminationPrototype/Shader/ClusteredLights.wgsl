//============================================================================================================================================
//                                                          CLUSTEREDLIGHTS.WGSL
//============================================================================================================================================
// 🧩 World-space clustered lighting with IES profiles: roadway batwing, asymmetric low beam, collimated high beam, fog, floodlight.

struct LightRecord {
    PositionType    : vec4f, // xyz = world position, w = light type (0: omni, 1: street lamp, 2: low beam, 3: high beam, 4: fog, 5: flood)
    DirectionReach  : vec4f, // xyz = forward direction, w = reach radius [m]
    ColourIntensity : vec4f, // rgb = linear color, w = intensity
    Params          : vec4f, // x = inner angle cos, y = outer angle cos, z = profile variant, w = active
}

struct LightTable
{
    Lights : array<LightRecord, 32>,
    Minimum : vec4f,
    InverseExtent : vec4f,
    Masks : array<vec4u, 256>,
    Profile : vec4f,
    Candela : array<vec4f, 1675>,
}

fn SelectLightMask(Position: vec3f, Table: ptr<uniform, LightTable>) -> u32
{
    if ((*Table).Minimum.w < 0.5) { return 0xffffffffu; }
    let Local = (Position - (*Table).Minimum.xyz) * (*Table).InverseExtent.xyz;
    if (any(Local < vec3f(0.0)) || any(Local >= vec3f(1.0))) { return 0xffffffffu; }
    let Cell = min(vec3u(Local * vec3f(16.0, 16.0, 4.0)), vec3u(15u, 15u, 3u));
    let Index = Cell.x + 16u * (Cell.y + 16u * Cell.z);
    return (*Table).Masks[Index / 4u][Index % 4u];
}

fn PhotometricTexel(Table: ptr<uniform, LightTable>, Index: u32) -> f32
{
    return (*Table).Candela[Index / 4u][Index % 4u];
}

fn SamplePhotometricCandela(Light: LightRecord, Direction: vec3f, Table: ptr<uniform, LightTable>) -> f32
{
    let Forward = normalize(Light.DirectionReach.xyz);
    let Reference = select(vec3f(0.0, 0.0, 1.0), vec3f(0.0, 1.0, 0.0), abs(Forward.z) > 0.99);
    let Right = normalize(cross(Forward, Reference));
    let Up = cross(Right, Forward);
    let Theta = acos(clamp(dot(Forward, Direction), -1.0, 1.0)) * 57.295779513;
    var Phi = atan2(dot(Direction, Up), dot(Direction, Right)) * 57.295779513;
    Phi = ((Phi + 360.0) % 360.0) / 10.0;
    let X = u32(floor(Theta));
    let Y = u32(floor(Phi));
    let X1 = min(X + 1u, 180u);
    let Y1 = min(Y + 1u, 36u);
    let A = mix(PhotometricTexel(Table, Y * 181u + X), PhotometricTexel(Table, Y * 181u + X1), fract(Theta));
    let B = mix(PhotometricTexel(Table, Y1 * 181u + X), PhotometricTexel(Table, Y1 * 181u + X1), fract(Theta));
    return mix(A, B, fract(Phi));
}

// 📐 Photometric / IES profile evaluation
fn EvaluateLightIES(Light: LightRecord, TowardPoint: vec3f, Distance: f32, Table: ptr<uniform, LightTable>) -> f32 {
    let Type = u32(Light.PositionType.w + 0.5);
    if (Type == 6u && (*Table).Profile.x > 0.5)
    {
        return SamplePhotometricCandela(Light, TowardPoint / max(Distance, 1.0e-4), Table);
    }
    if (Type == 0u) {
        return 1.0; // Omnidirectional point
    }

    let LDir = TowardPoint / max(Distance, 1.0e-4);
    let Forward = normalize(Light.DirectionReach.xyz);
    let CosTheta = dot(LDir, Forward);

    // Type 1: Street Lamp / Roadway Batwing (IES Type II / III)
    if (Type == 1u) {
        let CosDown = -LDir.z;
        if (CosDown <= 0.05) { return 0.0; }
        let Theta = acos(clamp(CosDown, 0.0, 1.0));
        // Smooth batwing distribution without sharp ring discontinuities
        let Batwing = sin(Theta * 1.8) * CosDown * 2.2;
        return max(0.0, Batwing);
    }

    // Type 2: Automotive Low Beam (Asymmetric sharp cutoff, right-side kick, wide horizontal spread)
    if (Type == 2u) {
        if (CosTheta <= 0.08) { return 0.0; }
        let Right = normalize(cross(Forward, vec3f(0.0, 0.0, 1.0)));
        let Up = cross(Right, Forward);
        let LocalY = dot(LDir, Right);
        let LocalZ = dot(LDir, Up);

        // Asymmetric beam cutoff (ECE / DOT standard: lower on oncoming lane, kicks up towards shoulder)
        let CutoffZ = -0.02 - 0.04 * max(0.0, LocalY);
        let TopFade = clamp((CutoffZ + 0.035 - LocalZ) / 0.035, 0.0, 1.0);
        let HorizAngle = atan2(LocalY, CosTheta);
        let HorizFade = max(0.0, cos(clamp(HorizAngle * 1.6, -1.57, 1.57)));
        return TopFade * HorizFade * (CosTheta * CosTheta);
    }

    // Type 3: Automotive High Beam (Collimated pencil spotlight with soft halo)
    if (Type == 3u) {
        if (CosTheta <= 0.15) { return 0.0; }
        return pow(max(0.0, CosTheta), 14.0);
    }

    // Type 4: Automotive Fog Light (Wide fan, razor-sharp top cutoff to prevent glare in fog/smoke)
    if (Type == 4u) {
        if (CosTheta <= 0.08) { return 0.0; }
        let Right = normalize(cross(Forward, vec3f(0.0, 0.0, 1.0)));
        let Up = cross(Right, Forward);
        let LocalY = dot(LDir, Right);
        let LocalZ = dot(LDir, Up);
        if (LocalZ > -0.005) { return 0.0; }
        let HorizAngle = atan2(LocalY, CosTheta);
        return max(0.0, cos(clamp(HorizAngle * 1.3, -1.57, 1.57))) * pow(CosTheta, 4.0);
    }

    // Type 5: Roof Floodlight (Multi-lens LED bar or dual rally pods)
    if (Type == 5u) {
        let InnerCos = Light.Params.x;
        let OuterCos = Light.Params.y;
        return smoothstep(OuterCos, InnerCos, CosTheta);
    }

    return 1.0;
}

// 📐 Smooth, physical inverse-square illuminance with windowed reach
fn EvaluateLightArrival(Light: LightRecord, Position: vec3f, Normal: vec3f, Table: ptr<uniform, LightTable>) -> vec3f {
    if (Light.Params.w < 0.5) {
        return vec3f(0.0);
    }

    let LightPos = Light.PositionType.xyz;
    let Reach = Light.DirectionReach.w;
    let Toward = LightPos - Position;
    let D2 = dot(Toward, Toward);

    if (D2 >= Reach * Reach || Reach <= 0.0) {
        return vec3f(0.0);
    }

    let Distance = sqrt(D2);
    let LightDir = Toward / max(Distance, 1.0e-4);
    let NdotL = max(dot(Normal, LightDir), 0.0);

    if (NdotL <= 0.0) {
        return vec3f(0.0);
    }

    // Window function: C2 continuous polynomial smoothly dropping to 0 at Reach
    let Ratio = D2 / (Reach * Reach);
    let Window = (1.0 - Ratio * Ratio) * (1.0 - Ratio * Ratio);

    // IES profile evaluation along the beam direction
    let IES = EvaluateLightIES(Light, -Toward, Distance, Table);
    if (IES <= 0.0) {
        return vec3f(0.0);
    }

    let Intensity = Light.ColourIntensity.w;
    let Colour = Light.ColourIntensity.rgb;

    // Standard physical inverse-square falloff with radius margin to avoid singularity
    let Falloff = Window / max(D2, 0.36);
    return Colour * (Intensity * Falloff * IES * NdotL);
}
