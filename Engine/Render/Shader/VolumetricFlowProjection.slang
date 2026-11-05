/*==========================================================================================================================================
                                                 VOLUMETRICFLOWPROJECTION.SLANG
==========================================================================================================================================*/
// 🧩 Projection and multigrid transfer entries translated from VolumetricFlowSolver.html ProjectionShaderSource.
//    Descriptor ordering, workgroup size and texture formats follow the retained WebGPU source.

struct FlowParameterization
{
    float Deltatau;
    float VoxelExtent;
    float ProjectionIterations;
    float VorticityStrength;
    float DensityBuoyancy;
    float TemperatureBuoyancy;
    float AmbientTemperature;
    float DensityDissipation;
    float TemperatureDissipation;
    float VelocityDissipation;
    float RadiationCoefficient;
    float InjectionRadius;
    float InjectionDensity;
    float InjectionTemperature;
    float WindLongitudinal;
    float WindGustStrength;
    float ElapsedInterval;
    float MacCormackEnabled;
    float VorticityEnabled;
    float SourceLongitude;
    float SourceAltitude;
    float SourceLatitude;
    float WindLateral;
    float WindVertical;
    float GustFrequency;
    float FuelYield;
    float SootYield;
    float CarrierCeiling;
    float CombustionEnabled;
    float IgnitionTemperature;
    float BurnRate;
    float HeatRelease;
    float SootRelease;
    float FuelDissipation;
    float CoreThickness;
    float CoreLuminosity;
    float AdvectionDeltatau;
    float AdvectionSubstepCount;
    float VelocityCeiling;
    float BoundaryDamping;
    float SourceShape;
    float SourceMinorScale;
    float NoiseGain;
    float NoiseScale;
    float NoiseDrift;
    float SwirlStrength;
    float InjectionVelocity;
};

struct LevelParameterization
{
    float LevelExtent;
    float CoarseExtent;
    float SmoothWeight;
    float LevelPadding;
};

[[vk::binding(0, 0)]] ConstantBuffer<FlowParameterization> Flow;
[[vk::binding(1, 0)]] Texture3D<float4> VelocitySource;
[[vk::binding(2, 0)]] Texture3D<float4> PressureSource;
[[vk::binding(3, 0)]] Texture3D<float4> DivergenceSource;
[[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture3D<float4> VelocityTarget;
[[vk::binding(5, 0)]] [[vk::image_format("r32f")]] RWTexture3D<float> PressureTarget;
[[vk::binding(6, 0)]] [[vk::image_format("r32f")]] RWTexture3D<float> DivergenceTarget;
[[vk::binding(7, 0)]] ConstantBuffer<LevelParameterization> Level;
[[vk::binding(8, 0)]] Texture3D<float4> ResidualSource;

float3 FetchVelocity(int3 Index, int Extent)
{
    return VelocitySource.Load(int4(clamp(Index, int3(0, 0, 0), int3(Extent - 1, Extent - 1, Extent - 1)), 0)).xyz;
}

float FetchPressure(int3 Index, int Extent)
{
    return PressureSource.Load(int4(clamp(Index, int3(0, 0, 0), int3(Extent - 1, Extent - 1, Extent - 1)), 0)).x;
}

float FetchDivergence(int3 Index, int Extent)
{
    return DivergenceSource.Load(int4(clamp(Index, int3(0, 0, 0), int3(Extent - 1, Extent - 1, Extent - 1)), 0)).x;
}

float FetchResidual(int3 Index, int Extent)
{
    return ResidualSource.Load(int4(clamp(Index, int3(0, 0, 0), int3(Extent - 1, Extent - 1, Extent - 1)), 0)).x;
}

float NeighbourSumPressure(int3 Index, int Extent)
{
    return FetchPressure(Index + int3(1, 0, 0), Extent) + FetchPressure(Index - int3(1, 0, 0), Extent)
         + FetchPressure(Index + int3(0, 1, 0), Extent) + FetchPressure(Index - int3(0, 1, 0), Extent)
         + FetchPressure(Index + int3(0, 0, 1), Extent) + FetchPressure(Index - int3(0, 0, 1), Extent);
}

[shader("compute")]
[numthreads(8, 8, 4)]
void EvaluateDivergence(uint3 VoxelId : SV_DispatchThreadID)
{
    const int Extent = int(Level.LevelExtent);
    const int3 Index = int3(VoxelId);
    if (any(Index >= int3(Extent, Extent, Extent))) return;
    const float3 Centre = FetchVelocity(Index, Extent);
    const float Divergence = (Centre.x - FetchVelocity(Index - int3(1, 0, 0), Extent).x)
                           + (Centre.y - FetchVelocity(Index - int3(0, 1, 0), Extent).y)
                           + (Centre.z - FetchVelocity(Index - int3(0, 0, 1), Extent).z);
    DivergenceTarget[Index] = Divergence;
}

[shader("compute")]
[numthreads(8, 8, 4)]
void RelaxPressure(uint3 VoxelId : SV_DispatchThreadID)
{
    const int Extent = int(Level.LevelExtent);
    const int3 Index = int3(VoxelId);
    if (any(Index >= int3(Extent, Extent, Extent))) return;
    const float RightHand = FetchDivergence(Index, Extent);
    const float Sum = NeighbourSumPressure(Index, Extent);
    const float Held = FetchPressure(Index, Extent);
    const float Target = (Sum - RightHand) / 6.0f;
    PressureTarget[Index] = Held + Level.SmoothWeight * (Target - Held);
}

[shader("compute")]
[numthreads(8, 8, 4)]
void EvaluateResidual(uint3 VoxelId : SV_DispatchThreadID)
{
    const int Extent = int(Level.LevelExtent);
    const int3 Index = int3(VoxelId);
    if (any(Index >= int3(Extent, Extent, Extent))) return;
    const float RightHand = FetchDivergence(Index, Extent);
    const float Applied = NeighbourSumPressure(Index, Extent) - 6.0f * FetchPressure(Index, Extent);
    DivergenceTarget[Index] = RightHand - Applied;
}

[shader("compute")]
[numthreads(8, 8, 4)]
void RestrictResidual(uint3 VoxelId : SV_DispatchThreadID)
{
    const int CoarseExtent = int(Level.CoarseExtent);
    const int FineExtent = int(Level.LevelExtent);
    const int3 CoarseIndex = int3(VoxelId);
    if (any(CoarseIndex >= int3(CoarseExtent, CoarseExtent, CoarseExtent))) return;
    const int3 FineBase = CoarseIndex * 2;
    float Accumulated = 0.0f;
    float WeightTotal = 0.0f;
    for (int X = -1; X <= 2; ++X)
    {
        const float WeightX = (X == -1 || X == 2) ? 1.0f : 3.0f;
        for (int Y = -1; Y <= 2; ++Y)
        {
            const float WeightY = (Y == -1 || Y == 2) ? 1.0f : 3.0f;
            for (int Z = -1; Z <= 2; ++Z)
            {
                const float WeightZ = (Z == -1 || Z == 2) ? 1.0f : 3.0f;
                const float Weight = WeightX * WeightY * WeightZ;
                Accumulated += Weight * FetchResidual(FineBase + int3(X, Y, Z), FineExtent);
                WeightTotal += Weight;
            }
        }
    }
    DivergenceTarget[CoarseIndex] = (Accumulated / WeightTotal) * 4.0f;
}

[shader("compute")]
[numthreads(8, 8, 4)]
void ProlongateCorrection(uint3 VoxelId : SV_DispatchThreadID)
{
    const int FineExtent = int(Level.LevelExtent);
    const int CoarseExtent = int(Level.CoarseExtent);
    const int3 FineIndex = int3(VoxelId);
    if (any(FineIndex >= int3(FineExtent, FineExtent, FineExtent))) return;
    const float3 Position = (float3(FineIndex) + 0.5f) * 0.5f - 0.5f;
    const float3 Lower = floor(Position);
    const float3 Fraction = Position - Lower;
    const int3 LowerIndex = int3(Lower);
    float Interpolated = 0.0f;
    for (int X = 0; X <= 1; ++X)
    {
        const float BlendX = X == 1 ? Fraction.x : 1.0f - Fraction.x;
        for (int Y = 0; Y <= 1; ++Y)
        {
            const float BlendY = Y == 1 ? Fraction.y : 1.0f - Fraction.y;
            for (int Z = 0; Z <= 1; ++Z)
            {
                const float BlendZ = Z == 1 ? Fraction.z : 1.0f - Fraction.z;
                const float Sample = FetchResidual(LowerIndex + int3(X, Y, Z), CoarseExtent);
                Interpolated += BlendX * BlendY * BlendZ * Sample;
            }
        }
    }
    PressureTarget[FineIndex] = FetchPressure(FineIndex, FineExtent) + Interpolated;
}

[shader("compute")]
[numthreads(8, 8, 4)]
void ClearPressureLevel(uint3 VoxelId : SV_DispatchThreadID)
{
    const int Extent = int(Level.LevelExtent);
    const int3 Index = int3(VoxelId);
    if (any(Index >= int3(Extent, Extent, Extent))) return;
    PressureTarget[Index] = 0.0f;
}

[shader("compute")]
[numthreads(8, 8, 4)]
void SubtractPressureGradient(uint3 VoxelId : SV_DispatchThreadID)
{
    const int Extent = int(Level.LevelExtent);
    const int3 Index = int3(VoxelId);
    if (any(Index >= int3(Extent, Extent, Extent))) return;
    const float Centre = FetchPressure(Index, Extent);
    const float3 Gradient = float3(FetchPressure(Index + int3(1, 0, 0), Extent) - Centre,
                                   FetchPressure(Index + int3(0, 1, 0), Extent) - Centre,
                                   FetchPressure(Index + int3(0, 0, 1), Extent) - Centre);
    const float3 Velocity = VelocitySource.Load(int4(Index, 0)).xyz - Gradient;
    VelocityTarget[Index] = float4(Velocity, 0.0f);
}

float3 EvaluateVorticity(int3 Index, int Extent)
{
    const float3 Right = FetchVelocity(Index + int3(1, 0, 0), Extent);
    const float3 Left  = FetchVelocity(Index - int3(1, 0, 0), Extent);
    const float3 Upper = FetchVelocity(Index + int3(0, 1, 0), Extent);
    const float3 Lower = FetchVelocity(Index - int3(0, 1, 0), Extent);
    const float3 Front = FetchVelocity(Index + int3(0, 0, 1), Extent);
    const float3 Back  = FetchVelocity(Index - int3(0, 0, 1), Extent);
    return 0.5f * float3((Upper.z - Lower.z) - (Front.y - Back.y),
                         (Front.x - Back.x) - (Right.z - Left.z),
                         (Right.y - Left.y) - (Upper.x - Lower.x));
}

[shader("compute")]
[numthreads(8, 8, 4)]
void RestoreVorticity(uint3 VoxelId : SV_DispatchThreadID)
{
    const int Extent = int(Level.LevelExtent);
    const int3 Index = int3(VoxelId);
    if (any(Index >= int3(Extent, Extent, Extent))) return;
    float3 Velocity = VelocitySource.Load(int4(Index, 0)).xyz;
    if (Flow.VorticityEnabled > 0.5f && Flow.VorticityStrength > 0.0001f)
    {
        const float3 LocalVorticity = EvaluateVorticity(Index, Extent);
        const float Right = length(EvaluateVorticity(Index + int3(1, 0, 0), Extent));
        const float Left  = length(EvaluateVorticity(Index - int3(1, 0, 0), Extent));
        const float Upper = length(EvaluateVorticity(Index + int3(0, 1, 0), Extent));
        const float Lower = length(EvaluateVorticity(Index - int3(0, 1, 0), Extent));
        const float Front = length(EvaluateVorticity(Index + int3(0, 0, 1), Extent));
        const float Back  = length(EvaluateVorticity(Index - int3(0, 0, 1), Extent));
        const float3 Gradient = 0.5f * float3(Right - Left, Upper - Lower, Front - Back);
        const float3 Direction = Gradient / (length(Gradient) + 1e-9f);
        const float3 Force = cross(Direction, LocalVorticity);
        Velocity += Force * Flow.VorticityStrength * Flow.Deltatau;
    }
    VelocityTarget[Index] = float4(Velocity, 0.0f);
}
