// 🧩 Analytic shoreline and seabed profile; baked once into the terrain atlas at startup
#include "Noise.glsl"

// 🔴 This function is mirrored in src/TerrainShape.js. Changing one without the other breaks vessel grounding.
float TerrainHeight(vec2 WorldXz)
{
    float Landward = smoothstep(-70.0, 130.0, WorldXz.y);
    float Base = mix(-11.0, 6.2, Landward);

    // Sand bars parallel to the beach
    float BarBand = smoothstep(-95.0, -18.0, WorldXz.y) * (1.0 - Landward);
    float SandBar = 0.85 * sin(WorldXz.y * 0.082 + WorldXz.x * 0.017 + 1.7) * BarBand;

    // Dunes above the shoreline
    float DuneFade = smoothstep(0.58, 1.0, Landward);
    float Dune = (FractalNoise(WorldXz * 0.011 + vec2(4.2, 8.9), 4) - 0.5) * 9.0 * DuneFade;

    // Seabed undulation
    float Bed = (FractalNoise(WorldXz * 0.026 + vec2(17.0, 3.0), 3) - 0.5) * 1.7 * (1.0 - Landward);

    // Rock outcrops that shape the shoreline and break incoming swell
    float Island = 0.0;
    vec2 IslandCenter = vec2(-104.0, -66.0);
    float IslandRadius = length((WorldXz - IslandCenter) * vec2(1.0, 0.78));
    Island += 11.0 * (1.0 - smoothstep(9.0, 52.0, IslandRadius));

    vec2 RockCenter = vec2(74.0, -128.0);
    float RockRadius = length((WorldXz - RockCenter) * vec2(1.0, 1.35));
    Island += 6.5 * (1.0 - smoothstep(4.0, 26.0, RockRadius));

    vec2 SpireCenter = vec2(118.0, 6.0);
    float SpireRadius = length((WorldXz - SpireCenter) * vec2(0.62, 1.0));
    Island += 8.5 * (1.0 - smoothstep(3.0, 19.0, SpireRadius));

    return Base + SandBar + Dune + Bed + Island;
}

vec3 TerrainNormal(vec2 WorldXz)
{
    const float Epsilon = 0.8;
    float HeightLeft = TerrainHeight(WorldXz - vec2(Epsilon, 0.0));
    float HeightRight = TerrainHeight(WorldXz + vec2(Epsilon, 0.0));
    float HeightDown = TerrainHeight(WorldXz - vec2(0.0, Epsilon));
    float HeightUp = TerrainHeight(WorldXz + vec2(0.0, Epsilon));

    return normalize(vec3(HeightLeft - HeightRight, 2.0 * Epsilon, HeightDown - HeightUp));
}
