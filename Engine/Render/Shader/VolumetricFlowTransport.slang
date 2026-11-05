/*==========================================================================================================================================
                                                   VOLUMETRICFLOWTRANSPORT.SLANG
==========================================================================================================================================*/
// 🧩 Advection, source injection and influence entries translated from the retained editor's SolverShaderSource.
//    Resource layout and workgroup sizes match the retained WebGPU source; native runtime integration remains pending.

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

[[vk::binding(0, 0)]] ConstantBuffer<FlowParameterization> Flow;
[[vk::binding(1, 0)]] Texture3D<float4> VelocitySource;
[[vk::binding(2, 0)]] Texture3D<float4> ScalarSource;
[[vk::binding(3, 0)]] SamplerState TrilinearFetch;
[[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture3D<float4> VelocityTarget;
[[vk::binding(5, 0)]] [[vk::image_format("rgba16f")]] RWTexture3D<float4> ScalarTarget;

float3 VoxelToCoordinate(float3 Index, float Extent) { return (Index + 0.5f) / Extent; }
bool CoordinateInterior(float3 Coordinate) { return all(Coordinate >= 0.0f) && all(Coordinate <= 1.0f); }
float3 BacktraceCoordinate(float3 Coordinate, float3 Velocity, float Interval) { return Coordinate - Velocity * Interval; }

float HashLattice(float3 LatticePoint)
{
    float3 Accumulated = frac(LatticePoint * float3(0.1031f, 0.1030f, 0.0973f));
    Accumulated += dot(Accumulated, Accumulated.yxz + 33.33f);
    return frac((Accumulated.x + Accumulated.y) * Accumulated.z);
}

float LatticeNoise(float3 SamplePoint)
{
    const float3 LatticeCell = floor(SamplePoint);
    const float3 CellOffset = frac(SamplePoint);
    const float3 Smooth = CellOffset * CellOffset * (3.0f - 2.0f * CellOffset);
    const float Corner000 = HashLattice(LatticeCell + float3(0, 0, 0));
    const float Corner100 = HashLattice(LatticeCell + float3(1, 0, 0));
    const float Corner010 = HashLattice(LatticeCell + float3(0, 1, 0));
    const float Corner110 = HashLattice(LatticeCell + float3(1, 1, 0));
    const float Corner001 = HashLattice(LatticeCell + float3(0, 0, 1));
    const float Corner101 = HashLattice(LatticeCell + float3(1, 0, 1));
    const float Corner011 = HashLattice(LatticeCell + float3(0, 1, 1));
    const float Corner111 = HashLattice(LatticeCell + float3(1, 1, 1));
    const float EdgeX00 = lerp(Corner000, Corner100, Smooth.x);
    const float EdgeX10 = lerp(Corner010, Corner110, Smooth.x);
    const float EdgeX01 = lerp(Corner001, Corner101, Smooth.x);
    const float EdgeX11 = lerp(Corner011, Corner111, Smooth.x);
    return lerp(lerp(EdgeX00, EdgeX10, Smooth.y), lerp(EdgeX01, EdgeX11, Smooth.y), Smooth.z);
}

float3 CurlNoiseVelocity(float3 SamplePoint)
{
    const float Epsilon = 0.55f;
    const float3 Probe = SamplePoint + float3(0.0f, -Flow.ElapsedInterval * 0.35f, 0.0f);
    const float3 PotentialA = float3(LatticeNoise(Probe + float3(0.0f, 0.0f, 0.0f)),
                                    LatticeNoise(Probe + float3(41.7f, 17.3f, 8.1f)),
                                    LatticeNoise(Probe + float3(11.2f, 63.9f, 29.4f)));
    const float GradientYx = LatticeNoise(Probe + float3(41.7f + Epsilon, 17.3f, 8.1f)) - PotentialA.y;
    const float GradientZx = LatticeNoise(Probe + float3(11.2f + Epsilon, 63.9f, 29.4f)) - PotentialA.z;
    const float GradientXy = LatticeNoise(Probe + float3(Epsilon, 0.0f, 0.0f) + float3(0.0f, Epsilon, 0.0f)) - PotentialA.x;
    const float GradientZy = LatticeNoise(Probe + float3(11.2f, 63.9f + Epsilon, 29.4f)) - PotentialA.z;
    const float GradientXz = LatticeNoise(Probe + float3(0.0f, 0.0f, Epsilon)) - PotentialA.x;
    const float GradientYz = LatticeNoise(Probe + float3(41.7f, 17.3f, 8.1f + Epsilon)) - PotentialA.y;
    return float3(GradientZy - GradientYz, GradientXz - GradientZx, GradientYx - GradientXy) / Epsilon;
}

float4 ClampToNeighbourhood(float4 Corrected, float3 Coordinate, float Extent)
{
    const float3 BaseCell = floor(Coordinate * Extent - 0.5f);
    float4 LowerBound = 1e30f;
    float4 UpperBound = -1e30f;
    for (int Z = 0; Z < 2; ++Z)
        for (int Y = 0; Y < 2; ++Y)
            for (int X = 0; X < 2; ++X)
            {
                const float3 NeighbourCoord = (BaseCell + float3(X, Y, Z) + 0.5f) / Extent;
                const float4 Value = ScalarSource.SampleLevel(TrilinearFetch, NeighbourCoord, 0.0f);
                LowerBound = min(LowerBound, Value);
                UpperBound = max(UpperBound, Value);
            }
    return clamp(Corrected, LowerBound, UpperBound);
}

float4 ClampVelocityNeighbourhood(float4 Corrected, float3 Coordinate, float Extent)
{
    const float3 BaseCell = floor(Coordinate * Extent - 0.5f);
    float4 LowerBound = 1e30f;
    float4 UpperBound = -1e30f;
    for (int Z = 0; Z < 2; ++Z)
        for (int Y = 0; Y < 2; ++Y)
            for (int X = 0; X < 2; ++X)
            {
                const float3 NeighbourCoord = (BaseCell + float3(X, Y, Z) + 0.5f) / Extent;
                const float4 Value = VelocitySource.SampleLevel(TrilinearFetch, NeighbourCoord, 0.0f);
                LowerBound = min(LowerBound, Value);
                UpperBound = max(UpperBound, Value);
            }
    return clamp(Corrected, LowerBound, UpperBound);
}

[shader("compute")]
[numthreads(8, 8, 4)]
void AdvectVelocityCarrier(uint3 VoxelId : SV_DispatchThreadID)
{
    const float Extent = Flow.VoxelExtent;
    if (any(float3(VoxelId) >= Extent)) return;
    const float3 Coordinate = VoxelToCoordinate(float3(VoxelId), Extent);
    const float3 LocalVelocity = VelocitySource.SampleLevel(TrilinearFetch, Coordinate, 0.0f).xyz;
    const float3 TracedBack = BacktraceCoordinate(Coordinate, LocalVelocity, Flow.AdvectionDeltatau);
    const float4 ForwardValue = VelocitySource.SampleLevel(TrilinearFetch, clamp(TracedBack, 0.0f, 1.0f), 0.0f);
    float4 Advected = ForwardValue;
    if (Flow.MacCormackEnabled > 0.5f && CoordinateInterior(TracedBack))
    {
        const float3 TracedForward = BacktraceCoordinate(TracedBack, ForwardValue.xyz, -Flow.AdvectionDeltatau);
        const float4 RoundTrip = VelocitySource.SampleLevel(TrilinearFetch, clamp(TracedForward, 0.0f, 1.0f), 0.0f);
        const float4 Original = VelocitySource.SampleLevel(TrilinearFetch, Coordinate, 0.0f);
        Advected = ClampVelocityNeighbourhood(ForwardValue + 0.5f * (Original - RoundTrip), TracedBack, Extent);
    }
    VelocityTarget[VoxelId] = float4(Advected.xyz * Flow.VelocityDissipation, 0.0f);
}

[shader("compute")]
[numthreads(8, 8, 4)]
void AdvectScalarCarrier(uint3 VoxelId : SV_DispatchThreadID)
{
    const float Extent = Flow.VoxelExtent;
    if (any(float3(VoxelId) >= Extent)) return;
    const float3 Coordinate = VoxelToCoordinate(float3(VoxelId), Extent);
    const float3 LocalVelocity = VelocitySource.SampleLevel(TrilinearFetch, Coordinate, 0.0f).xyz;
    const float3 TracedBack = BacktraceCoordinate(Coordinate, LocalVelocity, Flow.AdvectionDeltatau);
    const float4 ForwardValue = ScalarSource.SampleLevel(TrilinearFetch, clamp(TracedBack, 0.0f, 1.0f), 0.0f);
    float4 Advected = ForwardValue;
    if (Flow.MacCormackEnabled > 0.5f && CoordinateInterior(TracedBack))
    {
        const float3 TracedVelocity = VelocitySource.SampleLevel(TrilinearFetch, clamp(TracedBack, 0.0f, 1.0f), 0.0f).xyz;
        const float3 TracedForward = BacktraceCoordinate(TracedBack, TracedVelocity, -Flow.AdvectionDeltatau);
        const float4 RoundTrip = ScalarSource.SampleLevel(TrilinearFetch, clamp(TracedForward, 0.0f, 1.0f), 0.0f);
        const float4 Original = ScalarSource.SampleLevel(TrilinearFetch, Coordinate, 0.0f);
        Advected = ClampToNeighbourhood(ForwardValue + 0.5f * (Original - RoundTrip), TracedBack, Extent);
    }
    float Density = Advected.x * Flow.DensityDissipation;
    float Temperature = Advected.y * Flow.TemperatureDissipation;
    float Fuel = Advected.z * Flow.FuelDissipation;
    float Soot = Advected.w * Flow.DensityDissipation;
    if (Flow.CombustionEnabled > 0.5f && Fuel > 0.0001f && Temperature > Flow.IgnitionTemperature)
    {
        const float Burned = min(Fuel, Flow.BurnRate * Flow.AdvectionDeltatau);
        Fuel -= Burned;
        Temperature += Burned * Flow.HeatRelease;
        Soot += Burned * Flow.SootRelease;
        Density += Burned * Flow.SootRelease * 0.5f;
    }
    const float CoolingExcess = max(Temperature - Flow.AmbientTemperature, 0.0f);
    const float Normalized = CoolingExcess / max(Flow.CarrierCeiling, 0.001f);
    const float Quartic = Normalized * Normalized * Normalized * Normalized;
    const float CoolingDrop = Flow.RadiationCoefficient * Quartic * Flow.CarrierCeiling * Flow.AdvectionDeltatau;
    Temperature = Flow.AmbientTemperature + max(CoolingExcess - CoolingDrop, 0.0f);
    ScalarTarget[VoxelId] = min(float4(max(Density, 0.0f), Temperature, max(Fuel, 0.0f), max(Soot, 0.0f)), Flow.CarrierCeiling);
}

[shader("compute")]
[numthreads(8, 8, 4)]
void AccumulateInfluence(uint3 VoxelId : SV_DispatchThreadID)
{
    const float Extent = Flow.VoxelExtent;
    if (any(float3(VoxelId) >= Extent)) return;
    const float3 Coordinate = VoxelToCoordinate(float3(VoxelId), Extent);
    float3 Velocity = VelocitySource.SampleLevel(TrilinearFetch, Coordinate, 0.0f).xyz;
    const float4 Carrier = ScalarSource.SampleLevel(TrilinearFetch, Coordinate, 0.0f);
    const float Buoyancy = -Flow.DensityBuoyancy * Carrier.x + Flow.TemperatureBuoyancy * (Carrier.y - Flow.AmbientTemperature);
    Velocity.y += Buoyancy * Flow.Deltatau;
    if (Flow.SwirlStrength > 0.0001f || Flow.InjectionVelocity > 0.0001f)
    {
        const float3 SourceCentre = float3(Flow.SourceLongitude, Flow.SourceAltitude, Flow.SourceLatitude);
        const float3 SourceOffset = Coordinate - SourceCentre;
        const float Reach = max(Flow.InjectionRadius * 2.5f, 0.001f);
        const float Proximity = 1.0f - smoothstep(0.0f, Reach, length(SourceOffset));
        if (Proximity > 0.0001f)
        {
            const float3 Tangential = float3(-SourceOffset.z, 0.0f, SourceOffset.x);
            Velocity += Tangential * Flow.SwirlStrength * Proximity * Flow.Deltatau;
            Velocity.y += Flow.InjectionVelocity * Proximity * Flow.Deltatau;
        }
    }
    Velocity += float3(Flow.WindLongitudinal, Flow.WindVertical, Flow.WindLateral) * Flow.Deltatau;
    if (Flow.WindGustStrength > 0.0001f)
    {
        const float3 GustLattice = Coordinate * Flow.GustFrequency + Flow.ElapsedInterval * 0.11f;
        Velocity += CurlNoiseVelocity(GustLattice) * Flow.WindGustStrength * Flow.Deltatau;
    }
    const float Speed = length(Velocity);
    if (Speed > Flow.VelocityCeiling && Speed > 1e-6f) Velocity *= Flow.VelocityCeiling / Speed;
    const float ShellDepth = 2.0f / Extent;
    const float EdgeDistance = min(min(min(Coordinate.x, 1.0f - Coordinate.x), min(Coordinate.y, 1.0f - Coordinate.y)),
                                   min(Coordinate.z, 1.0f - Coordinate.z));
    if (EdgeDistance < ShellDepth) Velocity *= lerp(Flow.BoundaryDamping, 1.0f, EdgeDistance / ShellDepth);
    VelocityTarget[VoxelId] = float4(Velocity, 0.0f);
}

float ValueNoise(float3 Position)
{
    const float3 Lattice = floor(Position);
    const float3 Fraction = frac(Position);
    const float3 Blend = Fraction * Fraction * Fraction * (Fraction * (Fraction * 6.0f - 15.0f) + 10.0f);
    return lerp(lerp(lerp(HashLattice(Lattice + float3(0, 0, 0)), HashLattice(Lattice + float3(1, 0, 0)), Blend.x),
                     lerp(HashLattice(Lattice + float3(0, 1, 0)), HashLattice(Lattice + float3(1, 1, 0)), Blend.x), Blend.y),
                lerp(lerp(HashLattice(Lattice + float3(0, 0, 1)), HashLattice(Lattice + float3(1, 0, 1)), Blend.x),
                     lerp(HashLattice(Lattice + float3(0, 1, 1)), HashLattice(Lattice + float3(1, 1, 1)), Blend.x), Blend.y), Blend.z);
}

float FractalNoise(float3 Position)
{
    float Total = 0.0f;
    float Amplitude = 0.5f;
    float3 Sample = Position;
    for (int Octave = 0; Octave < 3; ++Octave)
    {
        Total += Amplitude * ValueNoise(Sample);
        Sample *= 2.02f;
        Amplitude *= 0.5f;
    }
    return Total;
}

float EvaluateSourceDistance(float3 Offset)
{
    const int Shape = int(Flow.SourceShape);
    const float Radius = Flow.InjectionRadius;
    if (Shape == 1)
    {
        const float Planar = length(Offset.xz) - Radius;
        return length(float2(Planar, Offset.y)) - Radius * Flow.SourceMinorScale;
    }
    if (Shape == 2)
    {
        const float3 Corner = abs(Offset) - Radius;
        return length(max(Corner, 0.0f)) + min(max(Corner.x, max(Corner.y, Corner.z)), 0.0f);
    }
    if (Shape == 3)
    {
        const float Vertical = abs(Offset.y) - Radius * Flow.SourceMinorScale;
        const float Lateral = max(abs(Offset.x), abs(Offset.z)) - Radius * 2.4f;
        return max(Vertical, Lateral);
    }
    if (Shape == 4)
    {
        const float Radial = length(Offset.xz) - Radius;
        const float Vertical = abs(Offset.y) - Radius * Flow.SourceMinorScale;
        return max(Radial, Vertical);
    }
    return length(Offset) - Radius;
}

[shader("compute")]
[numthreads(8, 8, 4)]
void InjectScalarSource(uint3 VoxelId : SV_DispatchThreadID)
{
    const float Extent = Flow.VoxelExtent;
    if (any(float3(VoxelId) >= Extent)) return;
    const float3 Coordinate = VoxelToCoordinate(float3(VoxelId), Extent);
    float4 Carrier = ScalarSource.SampleLevel(TrilinearFetch, Coordinate, 0.0f);
    const float3 SourceCentre = float3(Flow.SourceLongitude, Flow.SourceAltitude, Flow.SourceLatitude);
    const float SourceRadius = max(Flow.InjectionRadius, 0.001f);
    const float Distance = EvaluateSourceDistance(Coordinate - SourceCentre);
    if (Distance < SourceRadius)
    {
        float Falloff = 1.0f - smoothstep(-0.65f, 0.0f, Distance / SourceRadius);
        const float3 NoiseOrigin = Coordinate * Flow.NoiseScale - float3(0.0f, Flow.ElapsedInterval * Flow.NoiseDrift, 0.0f);
        const float Signed = (FractalNoise(NoiseOrigin) - 0.5f) * 2.0f;
        Falloff *= max(1.0f + Signed * Flow.NoiseGain, 0.0f);
        if (Falloff > 0.0001f)
        {
            Carrier.x += Flow.InjectionDensity * Falloff * Flow.Deltatau;
            Carrier.y += Flow.InjectionTemperature * Falloff * Flow.Deltatau;
            Carrier.z += Flow.InjectionDensity * Falloff * Flow.Deltatau * Flow.FuelYield;
            Carrier.w += Flow.InjectionDensity * Falloff * Flow.Deltatau * Flow.SootYield;
        }
    }
    ScalarTarget[VoxelId] = min(Carrier, Flow.CarrierCeiling);
}
