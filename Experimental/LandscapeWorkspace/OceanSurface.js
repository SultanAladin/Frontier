/*==========================================================================================================================================
                                                             OCEANSURFACE.JS
==========================================================================================================================================*/
// 🧩 Aperiodic world-space wave packets, analytic wave slopes, and pixel-footprint spectral filtering.

//------------------------------------------------------------------------------------------------------------------------
//                                                      SPECTRAL COEFFICIENTS
//------------------------------------------------------------------------------------------------------------------------

export const OceanSpectrum = Array.from({ length: 24 }, (_, Index) =>
{
    const Fraction = Math.sin((Index + 3) * 127.173) * 43758.5453;
    const Random   = Fraction - Math.floor(Fraction);
    const θ        = 0.48 + Math.sin(Index * 2.39996323) * 1.13;
    const λ        = 210 * Math.pow(0.8517, Index) * (0.83 + Random * 0.34);
    const κ        = 2 * Math.PI / λ;
    const Amplitude = Math.pow(0.835, Index) * (0.31 + Random * 0.24);
    return { Direction: [Math.cos(θ), Math.sin(θ)], κ, Amplitude, ω: Math.sqrt(9.81 * κ), Phase: Random * 28.71 };
});

function PacketEvaluation(X, Z, Seconds)
{
    // 📝 Incommensurate, slowly advecting modulation bends the spectrum without wrapping a texture or an X/Z tile.
    const PhaseX = X * 0.001371 + Z * 0.001927 - Seconds * 0.019;
    const PhaseZ = Z * 0.001119 - X * 0.001763 + Seconds * 0.013;
    return {
        X: X + 22 * Math.sin(PhaseX),
        Z: Z + 19 * Math.sin(PhaseZ),
        XX: 1 + 22 * 0.001371 * Math.cos(PhaseX),
        XZ: 22 * 0.001927 * Math.cos(PhaseX),
        ZX: -19 * 0.001763 * Math.cos(PhaseZ),
        ZZ: 1 + 19 * 0.001119 * Math.cos(PhaseZ)
    };
}

export function EvaluateOcean(X, Z, Seconds, Amplitude = 1, Footprint = 0)
{
    const Packet = PacketEvaluation(X, Z, Seconds);
    let Height = 0, SlopeX = 0, SlopeZ = 0;
    for (const Wave of OceanSpectrum)
    {
        const Filter  = Math.exp(-Wave.κ * Wave.κ * Footprint * Footprint * 0.7);
        const Phase   = (Packet.X * Wave.Direction[0] + Packet.Z * Wave.Direction[1]) * Wave.κ - Seconds * Wave.ω + Wave.Phase;
        const Strength = Wave.Amplitude * Filter * Amplitude;
        const Derivative = Math.cos(Phase) * Strength * Wave.κ;
        Height += Math.sin(Phase) * Strength;
        SlopeX += Derivative * (Wave.Direction[0] * Packet.XX + Wave.Direction[1] * Packet.ZX);
        SlopeZ += Derivative * (Wave.Direction[0] * Packet.XZ + Wave.Direction[1] * Packet.ZZ);
    }
    return { Height, SlopeX, SlopeZ };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        GPU EQUIVALENT
//------------------------------------------------------------------------------------------------------------------------

const Scalar = Number => Number.toFixed(10);
export const OceanShader = `
vec3 OceanEvaluation(vec2 P, float Seconds, float Amplitude, float Footprint)
{
    float PhaseX = P.x * .001371 + P.y * .001927 - Seconds * .019;
    float PhaseZ = P.y * .001119 - P.x * .001763 + Seconds * .013;
    vec2 Q = P + vec2(22. * sin(PhaseX), 19. * sin(PhaseZ));
    vec4 Jacobian = vec4(1. + 22. * .001371 * cos(PhaseX), 22. * .001927 * cos(PhaseX),
        -19. * .001763 * cos(PhaseZ), 1. + 19. * .001119 * cos(PhaseZ));
    vec3 Accumulation = vec3(0.);
    ${OceanSpectrum.map(Wave => `{
        vec2 Direction = vec2(${Wave.Direction.map(Scalar).join(',')});
        float Frequency = ${Scalar(Wave.κ)};
        float Phase = dot(Q, Direction) * Frequency - Seconds * ${Scalar(Wave.ω)} + ${Scalar(Wave.Phase)};
        float Strength = ${Scalar(Wave.Amplitude)} * Amplitude * exp(-Frequency * Frequency * Footprint * Footprint * .7);
        float Derivative = cos(Phase) * Strength * Frequency;
        Accumulation += vec3(sin(Phase) * Strength,
            Derivative * dot(Direction, Jacobian.xz), Derivative * dot(Direction, Jacobian.yw));
    }`).join('\n')}
    return Accumulation;
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                                    COMPACT VERTEX-SPECTRUM PROGRAM
//------------------------------------------------------------------------------------------------------------------------

export const OceanPhaseParameters = new Float32Array(OceanSpectrum.flatMap(Wave => [Wave.ω, Wave.Phase]));

export function PrepareOceanSpectrum(Amplitude, Footprint)
{
    return new Float32Array(OceanSpectrum.flatMap(Wave => [
        ...Wave.Direction, Wave.κ, Wave.Amplitude * Amplitude * Math.exp(-Wave.κ * Wave.κ * Footprint * Footprint * .7)
    ]));
}

// 📝 Twenty-four uniform packets keep shader source and compile work small; filtering is once per spectrum, not per vertex.
export const OceanVertexShader = `
uniform vec4 OceanVectors[24];
uniform vec2 OceanPhases[24];
vec3 OceanVertexEvaluation(vec2 P, float Seconds)
{
    float PhaseX = P.x * .001371 + P.y * .001927 - Seconds * .019;
    float PhaseZ = P.y * .001119 - P.x * .001763 + Seconds * .013;
    vec2 Q = P + vec2(22. * sin(PhaseX), 19. * sin(PhaseZ));
    vec4 Jacobian = vec4(1. + 22. * .001371 * cos(PhaseX), 22. * .001927 * cos(PhaseX),
        -19. * .001763 * cos(PhaseZ), 1. + 19. * .001119 * cos(PhaseZ));
    vec3 Accumulation = vec3(0.);
    for (int Index = 0; Index < 24; Index++)
    {
        vec4 Profile = OceanVectors[Index];
        float Phase = dot(Q, Profile.xy) * Profile.z - Seconds * OceanPhases[Index].x + OceanPhases[Index].y;
        float Derivative = cos(Phase) * Profile.w * Profile.z;
        Accumulation += vec3(sin(Phase) * Profile.w,
            Derivative * dot(Profile.xy, Jacobian.xz), Derivative * dot(Profile.xy, Jacobian.yw));
    }
    return Accumulation;
}
`;

// 📝 Four resolved slope bands are evaluated at the water pixel, not interpolated across kilometre-wide triangles.
// Physical displacement still uses the full, camera-independent 24-wave spectrum in the vertex program.
export const OceanDetailShader = `
vec2 OceanDetailSlope(vec2 P, float Seconds, float Amplitude, float Footprint)
{
    float PhaseX = P.x * .001371 + P.y * .001927 - Seconds * .019;
    float PhaseZ = P.y * .001119 - P.x * .001763 + Seconds * .013;
    vec2 Q = P + vec2(22. * sin(PhaseX), 19. * sin(PhaseZ));
    vec4 Jacobian = vec4(1. + 22. * .001371 * cos(PhaseX), 22. * .001927 * cos(PhaseX),
        -19. * .001763 * cos(PhaseZ), 1. + 19. * .001119 * cos(PhaseZ));
    vec2 Slope = vec2(0.);
    ${[8, 12, 16, 20].map(Index =>
    {
        const Wave = OceanSpectrum[Index];
        return `{
            vec2 Direction = vec2(${Wave.Direction.map(Scalar).join(',')});
            float Frequency = ${Scalar(Wave.κ)};
            float Phase = dot(Q, Direction) * Frequency - Seconds * ${Scalar(Wave.ω)} + ${Scalar(Wave.Phase)};
            float Strength = ${Scalar(Wave.Amplitude)} * Amplitude * exp(-Frequency * Frequency * Footprint * Footprint * .7);
            Slope += cos(Phase) * Strength * Frequency * vec2(dot(Direction, Jacobian.xz), dot(Direction, Jacobian.yw));
        }`;
    }).join('\n')}
    return Slope;
}
`;
