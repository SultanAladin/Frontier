/*==========================================================================================================================================
                                                  VOLUMETRICFLOWIRRADIANCE.SLANG
==========================================================================================================================================*/
// 🧩 Irradiance seed and flood-hop entries translated from the retained flow editor's WGSL.
//    This is a standalone shader; the native editor/renderer does not yet allocate or submit its cache.

struct IrradianceParameterization
{
    float CacheExtent;
    float DensityScale;
    float SootOpacity;
    float EmissionStrength;
    float3 SunDirection;
    float SunStrength;
    float3 SkyColour;
    float GroundStrength;
    float3 GroundColour;
    float PropagationGain;
    float TemperatureFloor;
    float TemperatureSpan;
    float EmissionThreshold;
    float ScatterAlbedo;
};

[[vk::binding(0, 0)]] ConstantBuffer<IrradianceParameterization> Cache;
[[vk::binding(1, 0)]] Texture3D<float4> ScalarVolume;
[[vk::binding(2, 0)]] Texture3D<float4> IrradianceRead;
[[vk::binding(3, 0)]] SamplerState TrilinearFetch;
[[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture3D<float4> IrradianceWrite;

float3 CacheCoordinate(int3 Index)
{
    return (float3(Index) + 0.5f) / Cache.CacheExtent;
}

float LocalExtinction(float3 Coordinate)
{
    const float4 Carrier = ScalarVolume.SampleLevel(TrilinearFetch, Coordinate, 0.0f);
    return (Carrier.x + Carrier.w * Cache.SootOpacity) * Cache.DensityScale;
}

float3 CacheEmission(float Temperature)
{
    const float Radiating = max(Temperature - Cache.EmissionThreshold, 0.0f);
    if (Radiating <= 0.0f) return float3(0, 0, 0);
    const float Kelvin = Cache.TemperatureFloor + Radiating * Cache.TemperatureSpan;
    const float Normalized = clamp((Kelvin - 1000.0f) / 3500.0f, 0.0f, 1.0f);
    const float3 Tint = lerp(float3(1.00f, 0.22f, 0.03f), float3(1.00f, 0.92f, 0.78f), Normalized);
    return Tint * Radiating * Radiating * Cache.EmissionStrength;
}

[shader("compute")]
[numthreads(4, 4, 4)]
void SeedIrradiance(uint3 VoxelId : SV_DispatchThreadID)
{
    const int Extent = int(Cache.CacheExtent);
    const int3 Index = int3(VoxelId);
    if (any(Index >= Extent)) return;
    const float3 Coordinate = CacheCoordinate(Index);
    const float4 Carrier = ScalarVolume.SampleLevel(TrilinearFetch, Coordinate, 0.0f);
    float Occlusion = 0.0f;
    const float ShadowStep = 1.0f / Cache.CacheExtent;
    for (int Step = 1; Step <= 6; ++Step)
    {
        const float3 Probe = Coordinate + Cache.SunDirection * (float(Step) * ShadowStep * 2.0f);
        if (any(Probe < 0.0f) || any(Probe > 1.0f)) break;
        Occlusion += LocalExtinction(Probe) * ShadowStep * 2.0f;
    }
    const float SunTransmittance = exp(-Occlusion);
    const float3 Sky = Cache.SkyColour * Cache.SunStrength * 0.35f;
    const float3 Ground = Cache.GroundColour * Cache.GroundStrength * (1.0f - Coordinate.y);
    const float3 Sun = float3(1.0f, 0.96f, 0.88f) * Cache.SunStrength * SunTransmittance;
    IrradianceWrite[Index] = float4(Sun + Sky + Ground + CacheEmission(Carrier.y), 1.0f);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void PropagateIrradiance(uint3 VoxelId : SV_DispatchThreadID)
{
    const int Extent = int(Cache.CacheExtent);
    const int3 Index = int3(VoxelId);
    if (any(Index >= Extent)) return;
    const float3 Coordinate = CacheCoordinate(Index);
    const float3 Held = IrradianceRead.SampleLevel(TrilinearFetch, Coordinate, 0.0f).xyz;
    const float StepLength = 1.0f / Cache.CacheExtent;
    float3 Gathered = 0.0f;
    float WeightSum = 0.0f;
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        for (int Side = 0; Side < 2; ++Side)
        {
            float3 Offset = 0.0f;
            const float Direction = Side == 1 ? 1.0f : -1.0f;
            if (Axis == 0) Offset.x = Direction;
            else if (Axis == 1) Offset.y = Direction;
            else Offset.z = Direction;
            const float3 NeighbourPoint = Coordinate + Offset * StepLength;
            if (any(NeighbourPoint < 0.0f) || any(NeighbourPoint > 1.0f)) continue;
            const float3 Neighbour = IrradianceRead.SampleLevel(TrilinearFetch, NeighbourPoint, 0.0f).xyz;
            const float3 Midpoint = Coordinate + Offset * StepLength * 0.5f;
            const float Attenuation = exp(-LocalExtinction(Midpoint) * StepLength);
            Gathered += Neighbour * Attenuation;
            WeightSum += 1.0f;
        }
    }
    if (WeightSum > 0.0f) Gathered /= WeightSum;
    const float3 Propagated = lerp(Held, Gathered * Cache.ScatterAlbedo, Cache.PropagationGain);
    IrradianceWrite[Index] = float4(Propagated, 1.0f);
}
