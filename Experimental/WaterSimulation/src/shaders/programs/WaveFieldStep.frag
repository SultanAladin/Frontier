#version 300 es
precision highp float;

#include "Common.glsl"
#include "Noise.glsl"
#include "TerrainSampling.glsl"

uniform sampler2D uField;
uniform vec2  uDomain;
uniform float uResolution;
uniform float uTexelWorld;
uniform float uDeltaSeconds;
uniform float uCelerityScale;
uniform float uDamping;
uniform float uWindForcing;
uniform float uMaxHeightRatio;
uniform float uSimulationTime;

out vec4 FragColor;

// 🧩 Explicit second-order wave equation on a global grid; celerity follows the local water depth
void main()
{
    vec2 Coordinate = gl_FragCoord.xy / float(uResolution);
    vec2 WorldXz = (Coordinate - 0.5) * uDomain;
    vec2 Step = vec2(1.0 / float(uResolution), 0.0);
    vec2 StepVertical = vec2(0.0, 1.0 / float(uResolution));

    vec4 Center = textureLod(uField, Coordinate, 0.0);
    float LeftSample = textureLod(uField, Coordinate - Step, 0.0).r;
    float RightSample = textureLod(uField, Coordinate + Step, 0.0).r;
    float DownSample = textureLod(uField, Coordinate - StepVertical, 0.0).r;
    float UpSample = textureLod(uField, Coordinate + StepVertical, 0.0).r;

    float Height = Center.r;
    float Velocity = Center.g; 
    float Laplacian = (LeftSample + RightSample + DownSample + UpSample - 4.0 * Height)
                      / max(uTexelWorld * uTexelWorld, 0.00001);

    float Depth = max(SeaLevel - SampleTerrainHeight(WorldXz), 0.0);
    float Celerity = uCelerityScale * sqrt(Gravity * clamp(Depth, 0.35, 44.0));
    float NewVelocity = Velocity + uDeltaSeconds * Celerity * Celerity * Laplacian;

    // Wind builds the sea state from rest through band-limited pressure noise
    vec2 ForcingCoordinate = WorldXz * 0.045 + vec2(uSimulationTime * 0.06, uSimulationTime * -0.03);
    float Forcing = FractalNoise(ForcingCoordinate, 3) - 0.5;
    NewVelocity += Forcing * uWindForcing * uDeltaSeconds * 14.0;

    NewVelocity *= pow(clamp(uDamping, 0.5, 0.9995), uDeltaSeconds * 60.0);

    // Dry sand carries no waves, and the outer boundary absorbs outgoing energy
    float LandMask = smoothstep(-0.5, 1.2, Depth);
    NewVelocity *= LandMask;

    vec2 Edge = min(Coordinate, vec2(1.0) - Coordinate);
    float Sponge = smoothstep(0.0, 0.07, min(Edge.x, Edge.y));
    NewVelocity *= mix(0.02, 1.0, Sponge);

    float NewHeight = Height + uDeltaSeconds * NewVelocity;
    float HeightLimit = max(Depth * uMaxHeightRatio, 0.06);
    NewHeight = clamp(NewHeight, -HeightLimit, HeightLimit) * LandMask;

    FragColor = vec4(NewHeight, NewVelocity, 0.0, 1.0);
}
