/*==========================================================================================================================================
                                                          PLUMEPRESSURE.SLANG
==========================================================================================================================================*/
// 🧩 Pressure projection translated from VehicleHmiPrototype/Shader/PlumePressure.wgsl.
//    The host must preserve the source's toroidal pressure storage and ping-pong descriptor order.

struct PlumeUniform
{
    int4   VelocityOrigin;
    int4   PriorVelocityOrigin;
    uint4  VelocityExtent;
    uint4  ThermoExtent;
    float4 Lattice;
    float4 Forces;
    float4 Retention;
    float4 Wind;
    float4 SourceMinimum;
    float4 SourceMaximum;
    float4 Optical;
    float4 Sun;
    float4 Combustion;
    float4 Scattering;
    float4 Detail;
    float4 Flame;
};

[[vk::binding(0, 0)]] ConstantBuffer<PlumeUniform> Plume;
[[vk::binding(1, 0)]] Texture3D<float4> VelocityLattice;
[[vk::binding(2, 0)]] RWStructuredBuffer<float> Divergence;
[[vk::binding(3, 0)]] RWStructuredBuffer<float> PressureSource;
[[vk::binding(4, 0)]] RWStructuredBuffer<float> PressureTarget;
[[vk::binding(5, 0)]] [[vk::image_format("rgba16f")]] RWTexture3D<float4> LatticeTarget;

static const float PlumeSpeedCeiling = 40.0f;

int3 ResolveVelocityTexel(int3 Local)
{
    const int3 Cell = Plume.VelocityOrigin.xyz + Local;
    const int2 Extent = int2(Plume.VelocityExtent.xy);
    return int3(((Cell.xy % Extent) + Extent) % Extent, Cell.z);
}

bool ClassifyInside(int3 Local)
{
    return all(Local >= int3(0, 0, 0)) && all(Local < int3(Plume.VelocityExtent.xyz));
}

float4 LoadVelocity(int3 Local)
{
    const int3 Clamped = clamp(Local, int3(0, 0, 0), int3(Plume.VelocityExtent.xyz) - 1);
    return VelocityLattice.Load(int4(ResolveVelocityTexel(Clamped), 0));
}

uint ResolveLocalIndex(int3 Local)
{
    const int3 Extent = int3(Plume.VelocityExtent.xyz);
    return uint((Local.z * Extent.y + Local.y) * Extent.x + Local.x);
}

uint ResolvePressureIndex(int3 Local)
{
    const int3 Extent = int3(Plume.VelocityExtent.xyz);
    const int3 Texel = ResolveVelocityTexel(Local);
    return uint((Texel.z * Extent.y + Texel.y) * Extent.x + Texel.x);
}

float ResolveNeighbourPressure(int3 Local, float Centre)
{
    if (Local.z < 0) return Centre;
    if (!ClassifyInside(Local)) return 0.0f;
    if (LoadVelocity(Local).w > 0.5f) return Centre;
    return PressureSource[ResolvePressureIndex(Local)];
}

float3 ProjectVelocityWorld(uint3 Local)
{
    const float3 Cell = float3(Plume.VelocityOrigin.xyz + int3(Local)) + 0.5f;
    return float3(Cell.xy * Plume.Lattice.y, Plume.Lattice.z + Cell.z * Plume.Lattice.y);
}

bool ClassifyPriorInterior(float3 Position)
{
    const float Edge = Plume.Lattice.y;
    const float3 Local = float3(Position.xy / Edge - float2(Plume.PriorVelocityOrigin.xy),
                                (Position.z - Plume.Lattice.z) / Edge);
    return all(Local >= 0.5f) && all(Local <= float3(Plume.VelocityExtent.xyz) - 0.5f);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void ResolveDivergence(uint3 Invocation : SV_DispatchThreadID)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz)) return;
    const int3 Local = int3(Invocation);
    const float4 Own = LoadVelocity(Local);
    const float Scale = 0.5f / Plume.Lattice.y;
    const float Flux = (LoadVelocity(Local + int3(1, 0, 0)).x - LoadVelocity(Local - int3(1, 0, 0)).x)
                     + (LoadVelocity(Local + int3(0, 1, 0)).y - LoadVelocity(Local - int3(0, 1, 0)).y)
                     + (LoadVelocity(Local + int3(0, 0, 1)).z - LoadVelocity(Local - int3(0, 0, 1)).z);
    Divergence[ResolveLocalIndex(Local)] = Own.w > 0.5f ? 0.0f : Flux * Scale;
    if (!ClassifyPriorInterior(ProjectVelocityWorld(Invocation)))
    {
        const uint Index = ResolvePressureIndex(Local);
        PressureSource[Index] = 0.0f;
        PressureTarget[Index] = 0.0f;
    }
}

[shader("compute")]
[numthreads(4, 4, 4)]
void RelaxPressure(uint3 Invocation : SV_DispatchThreadID)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz)) return;
    const int3 Local = int3(Invocation);
    const uint Index = ResolvePressureIndex(Local);
    const float Centre = PressureSource[Index];
    if (LoadVelocity(Local).w > 0.5f)
    {
        PressureTarget[Index] = Centre;
        return;
    }
    const float Sum = ResolveNeighbourPressure(Local + int3(1, 0, 0), Centre)
                    + ResolveNeighbourPressure(Local - int3(1, 0, 0), Centre)
                    + ResolveNeighbourPressure(Local + int3(0, 1, 0), Centre)
                    + ResolveNeighbourPressure(Local - int3(0, 1, 0), Centre)
                    + ResolveNeighbourPressure(Local + int3(0, 0, 1), Centre)
                    + ResolveNeighbourPressure(Local - int3(0, 0, 1), Centre);
    const float Edge = Plume.Lattice.y;
    PressureTarget[Index] = (Sum - Divergence[ResolveLocalIndex(Local)] * Edge * Edge) / 6.0f;
}

[shader("compute")]
[numthreads(4, 4, 4)]
void ProjectVelocity(uint3 Invocation : SV_DispatchThreadID)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz)) return;
    const int3 Local = int3(Invocation);
    const float4 Own = LoadVelocity(Local);
    const int3 Texel = ResolveVelocityTexel(Local);
    if (Own.w > 0.5f)
    {
        LatticeTarget[Texel] = Own;
        return;
    }
    const float Centre = PressureSource[ResolvePressureIndex(Local)];
    const float Scale = 0.5f / Plume.Lattice.y;
    const float3 Gradient = float3(
        ResolveNeighbourPressure(Local + int3(1, 0, 0), Centre) - ResolveNeighbourPressure(Local - int3(1, 0, 0), Centre),
        ResolveNeighbourPressure(Local + int3(0, 1, 0), Centre) - ResolveNeighbourPressure(Local - int3(0, 1, 0), Centre),
        ResolveNeighbourPressure(Local + int3(0, 0, 1), Centre) - ResolveNeighbourPressure(Local - int3(0, 0, 1), Centre)) * Scale;
    float3 Velocity = Own.xyz - Gradient;
    const float Speed = length(Velocity);
    if (Speed > PlumeSpeedCeiling) Velocity *= PlumeSpeedCeiling / Speed;
    LatticeTarget[Texel] = float4(Velocity, 0.0f);
}
