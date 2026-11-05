/*==========================================================================================================================================
                                                          PLUMEVORTICITY.SLANG
==========================================================================================================================================*/
// 🧩 Coarse-lattice curl and vorticity confinement translated from VehicleHmiPrototype/Shader/PlumeVorticity.wgsl.

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
[[vk::binding(2, 0)]] RWStructuredBuffer<float4> Curl;
[[vk::binding(3, 0)]] [[vk::image_format("rgba16f")]] RWTexture3D<float4> LatticeTarget;

int3 ResolveVelocityTexel(int3 Local)
{
    const int3 Cell = Plume.VelocityOrigin.xyz + Local;
    const int2 Extent = int2(Plume.VelocityExtent.xy);
    return int3(((Cell.xy % Extent) + Extent) % Extent, Cell.z);
}

float4 LoadVelocity(int3 Local)
{
    const int3 Clamped = clamp(Local, int3(0, 0, 0), int3(Plume.VelocityExtent.xyz) - 1);
    return VelocityLattice.Load(int4(ResolveVelocityTexel(Clamped), 0));
}

uint ResolveLocalIndex(int3 Local)
{
    const int3 Extent = int3(Plume.VelocityExtent.xyz);
    const int3 Clamped = clamp(Local, int3(0, 0, 0), Extent - 1);
    return uint((Clamped.z * Extent.y + Clamped.y) * Extent.x + Clamped.x);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void ResolveCurl(uint3 Invocation : SV_DispatchThreadID)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz)) return;
    const int3 Local = int3(Invocation);
    const float Scale = 0.5f / Plume.Lattice.y;
    const float3 Xp = LoadVelocity(Local + int3(1, 0, 0)).xyz;
    const float3 Xm = LoadVelocity(Local - int3(1, 0, 0)).xyz;
    const float3 Yp = LoadVelocity(Local + int3(0, 1, 0)).xyz;
    const float3 Ym = LoadVelocity(Local - int3(0, 1, 0)).xyz;
    const float3 Zp = LoadVelocity(Local + int3(0, 0, 1)).xyz;
    const float3 Zm = LoadVelocity(Local - int3(0, 0, 1)).xyz;
    const float3 Rotation = float3((Yp.z - Ym.z) - (Zp.y - Zm.y),
                                   (Zp.x - Zm.x) - (Xp.z - Xm.z),
                                   (Xp.y - Xm.y) - (Yp.x - Ym.x)) * Scale;
    Curl[ResolveLocalIndex(Local)] = float4(Rotation, length(Rotation));
}

[shader("compute")]
[numthreads(4, 4, 4)]
void ConfineVorticity(uint3 Invocation : SV_DispatchThreadID)
{
    if (any(Invocation >= Plume.VelocityExtent.xyz)) return;
    const int3 Local = int3(Invocation);
    const float4 Own = LoadVelocity(Local);
    float3 Velocity = Own.xyz;
    if (Own.w < 0.5f && Plume.Forces.z > 0.0f)
    {
        const float Scale = 0.5f / Plume.Lattice.y;
        const float3 Gradient = float3(
            Curl[ResolveLocalIndex(Local + int3(1, 0, 0))].w - Curl[ResolveLocalIndex(Local - int3(1, 0, 0))].w,
            Curl[ResolveLocalIndex(Local + int3(0, 1, 0))].w - Curl[ResolveLocalIndex(Local - int3(0, 1, 0))].w,
            Curl[ResolveLocalIndex(Local + int3(0, 0, 1))].w - Curl[ResolveLocalIndex(Local - int3(0, 0, 1))].w) * Scale;
        const float Magnitude = length(Gradient);
        if (Magnitude > 1.0e-5f)
        {
            const float3 Rotation = Curl[ResolveLocalIndex(Local)].xyz;
            Velocity += Plume.Forces.z * Plume.Lattice.y * cross(Gradient / Magnitude, Rotation) * Plume.Lattice.w;
        }
    }
    LatticeTarget[ResolveVelocityTexel(Local)] = float4(Velocity, Own.w);
}
