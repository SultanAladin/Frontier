// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//                                                  ATMOSPHERE LATTICE NOISE 🧩
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
// Exact GPU ports of the procedural lattice noise in AtmosphereProfile.h (namespace Atmosphere), shared by VolumetricCloudField.glsl and
// VolumetricFogField.glsl: HashLatticeCell (32-bit avalanche hash -> [0, 1)), EvaluateQuinticNoise3D (quintic-faded value noise) and
// EvaluateRotatedFbm3D (orthonormal domain-rotated fBm, lacunarity 2.03). Same constants, same operation order, so CPU and GPU agree.

#ifndef ATMOSPHERE_LATTICE_NOISE_GLSL
#define ATMOSPHERE_LATTICE_NOISE_GLSL

float HashLatticeCell(int CellX, int CellY, int CellZ, uint Salt)
{
    uint Mixed = uint(CellX) * 0x9E3779B1u ^ uint(CellY) * 0x85EBCA77u ^ uint(CellZ) * 0xC2B2AE3Du ^ Salt * 0x27D4EB2Fu;
    Mixed ^= Mixed >> 16u;
    Mixed *= 0x7FEB352Du;
    Mixed ^= Mixed >> 15u;
    Mixed *= 0x846CA68Bu;
    Mixed ^= Mixed >> 16u;
    return float(Mixed >> 8u) * (1.0 / 16777216.0);
}

float EvaluateLatticeQuintic(float Parameter)
{
    return Parameter * Parameter * Parameter * (Parameter * (Parameter * 6.0 - 15.0) + 10.0);
}

float EvaluateQuinticNoise3D(vec3 Coord, uint Salt)
{
    vec3  Floored = floor(Coord);
    ivec3 Cell    = ivec3(Floored);
    float Ux      = EvaluateLatticeQuintic(Coord.x - Floored.x);
    float Uy      = EvaluateLatticeQuintic(Coord.y - Floored.y);
    float Uz      = EvaluateLatticeQuintic(Coord.z - Floored.z);
    float C00 = mix(HashLatticeCell(Cell.x, Cell.y,     Cell.z,     Salt), HashLatticeCell(Cell.x + 1, Cell.y,     Cell.z,     Salt), Ux);
    float C10 = mix(HashLatticeCell(Cell.x, Cell.y + 1, Cell.z,     Salt), HashLatticeCell(Cell.x + 1, Cell.y + 1, Cell.z,     Salt), Ux);
    float C01 = mix(HashLatticeCell(Cell.x, Cell.y,     Cell.z + 1, Salt), HashLatticeCell(Cell.x + 1, Cell.y,     Cell.z + 1, Salt), Ux);
    float C11 = mix(HashLatticeCell(Cell.x, Cell.y + 1, Cell.z + 1, Salt), HashLatticeCell(Cell.x + 1, Cell.y + 1, Cell.z + 1, Salt), Ux);
    return mix(mix(C00, C10, Uy), mix(C01, C11, Uy), Uz);
}

float EvaluateRotatedFbm3D(vec3 Coord, uint OctaveCount, uint Salt)
{
    float Total        = 0.0;
    float Amplitude    = 0.5;
    float AmplitudeSum = 0.0;
    for (uint Octave = 0u; Octave < OctaveCount; ++Octave)
    {
        Total        += Amplitude * EvaluateQuinticNoise3D(Coord, Salt + Octave * 37u);
        AmplitudeSum += Amplitude;
        vec3 Rotated = vec3( 0.80 * Coord.x - 0.48 * Coord.y + 0.36 * Coord.z,
                             0.48 * Coord.x + 0.86 * Coord.y + 0.16 * Coord.z,
                            -0.36 * Coord.x + 0.04 * Coord.y + 0.93 * Coord.z);
        Coord      = Rotated * 2.03 + vec3(13.7, -7.1, 5.3);
        Amplitude *= 0.5;
    }
    return (AmplitudeSum > 0.0) ? (Total / AmplitudeSum) : 0.0;
}

#endif
