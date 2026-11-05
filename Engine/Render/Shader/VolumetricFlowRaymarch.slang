/*==========================================================================================================================================
                                                  VOLUMETRICFLOWRAYMARCH.SLANG
==========================================================================================================================================*/
// 🧩 Standalone translation of RaymarchShaderSource in the retained VolumetricFlowSolver.html.
//    No native renderer submission is implemented here. The view uniform and texture bindings match that source.

// 📝 49 populated floats in a 208-byte uniform; vec3 members share rows with their following scalars.
struct ViewParameterization
{
    float3 CameraOrigin;
    float MarchStepCount;
    float3 CameraBasisRight;
    float DensityScale;
    float3 CameraBasisUp;
    float RadianceScale;
    float3 CameraBasisForward;
    float AspectRatio;
    float3 ShadowDirection;
    float ShadowStepCount;
    float ScatterAnisotropy;
    float ChannelSelector;
    float ExposureScale;
    float SootOpacity;
    float TemperatureFloor;
    float TemperatureSpan;
    float EmissionThreshold;
    float SmokeAlbedo;
    float3 AbsorptionTint;
    float SpectralStrength;
    float CoreThickness;
    float CoreLuminosity;
    float SaturationGain;
    float HighlightShoulder;
    float PaletteBlend;
    float SmokeRampScale;
    float IrradianceBlend;

    float TurbulenceStrength;
    float TurbulenceDetail;
    float TurbulenceBand;
    float TurbulenceDrift;
    float ElapsedInterval;

    float ScatterBandCount;
    float ScatterExtinctionDecay;
    float ScatterContributionDecay;
    float ScatterAnisotropyDecay;
    float MultiScatterAmbient;
};

[[vk::binding(0, 0)]] ConstantBuffer<ViewParameterization> View;
[[vk::binding(1, 0)]] Texture3D<float4> ScalarVolume;
[[vk::binding(2, 0)]] Texture3D<float4> VelocityVolume;
[[vk::binding(3, 0)]] SamplerState TrilinearFetch;
[[vk::binding(4, 0)]] Texture2D<float4> PaletteRamp;
[[vk::binding(5, 0)]] Texture3D<float4> IrradianceVolume;

struct RasterVertex
{
    float4 ClipPosition : SV_Position;
    float2 ScreenPoint : TEXCOORD0;
};

[shader("vertex")]
RasterVertex ProjectFullscreenCorner(uint VertexIndex : SV_VertexID)
{
    float CornerX = float((VertexIndex & 1u) * 2u) - 1.0;
    float CornerY = float((VertexIndex >> 1u) * 2u) - 1.0;
    RasterVertex Emitted;
    Emitted.ClipPosition = float4(CornerX, CornerY, 0.0, 1.0);
    Emitted.ScreenPoint = float2(CornerX, CornerY);
    return Emitted;
}

// 📝 The domain cage is additive emission rather than an opaque box.
float2 IntersectUnitCube(float3 RayOrigin, float3 RayDirection)
{
    let InverseDirection = 1.0 / RayDirection;
    let LowerParameter   = (float3(0.0) - RayOrigin) * InverseDirection;
    let UpperParameter   = (float3(1.0) - RayOrigin) * InverseDirection;

    let NearParameter = min(LowerParameter, UpperParameter);
    let FarParameter  = max(LowerParameter, UpperParameter);

    let EntryParameter = max(max(NearParameter.x, NearParameter.y), NearParameter.z);
    let ExitParameter  = min(min(FarParameter.x,  FarParameter.y),  FarParameter.z);

    return float2(max(EntryParameter, 0.0), ExitParameter);
}

float3 CageRadiance(float3 RayOrigin, float3 RayDirection, float DepthLimit)
{
    let EdgeHalfWidth = 0.0045;
    let WireTint      = float3(0.26, 0.29, 0.36);

    var Accumulated = float3(0.0);

    for (var Axis = 0; Axis < 3; Axis = Axis + 1)
    {
        let Direction = RayDirection[Axis];
        if (abs(Direction) < 1e-6) { continue; }

        for (var Side = 0; Side < 2; Side = Side + 1)
        {
            let PlanePosition = float(Side);
            let Parameter     = (PlanePosition - RayOrigin[Axis]) / Direction;
            if (Parameter <= 0.0 || Parameter >= DepthLimit) { continue; }

            let HitPoint = RayOrigin + RayDirection * Parameter;

            let FirstAxis  = (Axis + 1) % 3;
            let SecondAxis = (Axis + 2) % 3;
            let FirstCoordinate  = HitPoint[FirstAxis];
            let SecondCoordinate = HitPoint[SecondAxis];

            if (FirstCoordinate  < -EdgeHalfWidth || FirstCoordinate  > 1.0 + EdgeHalfWidth ||
                SecondCoordinate < -EdgeHalfWidth || SecondCoordinate > 1.0 + EdgeHalfWidth) { continue; }

            let FirstBorder  = min(abs(FirstCoordinate),  abs(1.0 - FirstCoordinate));
            let SecondBorder = min(abs(SecondCoordinate), abs(1.0 - SecondCoordinate));

            let EdgeDistance = min(FirstBorder, SecondBorder);

            let Coverage = 1.0 - smoothstep(EdgeHalfWidth * 0.5, EdgeHalfWidth * 1.5, EdgeDistance);
            Accumulated += WireTint * Coverage;
        }
    }

    return min(Accumulated, float3(1.0));
}

// 📝 Blackbody intensity retains the T⁴ envelope even when a palette replaces hue.
float PlanckRadiance(float Wavelength, float Temperature)
{

    let FirstRadiation  = 3.7418e8;
    let SecondRadiation = 1.4388e4;

    let WavelengthFifth = pow(Wavelength, 5.0);
    let ExponentTerm    = exp(SecondRadiation / (Wavelength * max(Temperature, 1.0))) - 1.0;
    return FirstRadiation / (WavelengthFifth * max(ExponentTerm, 1e-6));
}

float3 BlackbodyInterpolant(float NormalizedTemperature)
{

    let Kelvin = View.TemperatureFloor + NormalizedTemperature * View.TemperatureSpan;

    let LongRadiance   = PlanckRadiance(0.610, Kelvin);
    let MediumRadiance = PlanckRadiance(0.550, Kelvin);
    let ShortRadiance  = PlanckRadiance(0.465, Kelvin);

    var Spectral = float3(LongRadiance, MediumRadiance, ShortRadiance);

    Spectral = Spectral / max(MediumRadiance, 1e-6);

    let IntensityEnvelope = pow(NormalizedTemperature, 4.0);

    return Spectral * IntensityEnvelope;
}

float3 SamplePaletteRamp(float Parameter, float RowIndex)
{
    let Coordinate = float2(clamp(Parameter, 0.0, 1.0), (RowIndex + 0.5) * 0.5);
    return PaletteRamp.SampleLevel(TrilinearFetch, Coordinate, 0.0).rgb;
}

float3 EmissionColour(float NormalizedTemperature)
{
    let Physical = BlackbodyInterpolant(NormalizedTemperature);
    if (View.PaletteBlend < 0.001) { return Physical; }

    let StampHue          = SamplePaletteRamp(NormalizedTemperature, 0.0);
    let IntensityEnvelope = pow(NormalizedTemperature, 4.0);
    return lerp(Physical, StampHue * IntensityEnvelope, View.PaletteBlend);
}

float HenyeyGreenstein(float CosineAngle, float Anisotropy)
{
    let AnisotropySquared = Anisotropy * Anisotropy;
    let Denominator       = 1.0 + AnisotropySquared - 2.0 * Anisotropy * CosineAngle;
    return (1.0 - AnisotropySquared) / (4.0 * 3.14159265 * pow(max(Denominator, 1e-4), 1.5));
}

// 📝 Synthesized view-only turbulence follows the retained solver noise, without changing its volumes.
float HashLattice(float3 LatticePoint)
{

    var Accumulated = dot(LatticePoint, float3(127.1, 311.7, 74.7));
    Accumulated = frac(sin(Accumulated) * 43758.5453123);
    return Accumulated * 2.0 - 1.0;
}

float ValueNoise(float3 Position)
{
    let Lattice  = floor(Position);
    let Fraction = frac(Position);

    let Blend    = Fraction * Fraction * Fraction * (Fraction * (Fraction * 6.0 - 15.0) + 10.0);

    return lerp(lerp(lerp(HashLattice(Lattice + float3(0.0, 0.0, 0.0)),
                       HashLattice(Lattice + float3(1.0, 0.0, 0.0)), Blend.x),
                   lerp(HashLattice(Lattice + float3(0.0, 1.0, 0.0)),
                       HashLattice(Lattice + float3(1.0, 1.0, 0.0)), Blend.x), Blend.y),
               lerp(lerp(HashLattice(Lattice + float3(0.0, 0.0, 1.0)),
                       HashLattice(Lattice + float3(1.0, 0.0, 1.0)), Blend.x),
                   lerp(HashLattice(Lattice + float3(0.0, 1.0, 1.0)),
                       HashLattice(Lattice + float3(1.0, 1.0, 1.0)), Blend.x), Blend.y), Blend.z);
}

float FractalNoise(float3 Position)
{
    var Total     = 0.0;
    var Amplitude = 0.5;
    var Sample    = Position;
    for (var Octave = 0; Octave < 2; Octave = Octave + 1)
    {
        Total     += Amplitude * ValueNoise(Sample);
        Sample     = Sample * 2.02;
        Amplitude *= 0.5;
    }
    return Total;
}

float EvaluateStrainRate(float3 SamplePoint)
{

    let Offset = 1.0 / max(View.TurbulenceDetail, 1.0);

    let RightVelocity = VelocityVolume.SampleLevel(TrilinearFetch, SamplePoint + float3(Offset, 0.0, 0.0), 0.0).xyz;
    let LeftVelocity  = VelocityVolume.SampleLevel(TrilinearFetch, SamplePoint - float3(Offset, 0.0, 0.0), 0.0).xyz;
    let UpperVelocity = VelocityVolume.SampleLevel(TrilinearFetch, SamplePoint + float3(0.0, Offset, 0.0), 0.0).xyz;
    let LowerVelocity = VelocityVolume.SampleLevel(TrilinearFetch, SamplePoint - float3(0.0, Offset, 0.0), 0.0).xyz;
    let FrontVelocity = VelocityVolume.SampleLevel(TrilinearFetch, SamplePoint + float3(0.0, 0.0, Offset), 0.0).xyz;
    let BackVelocity  = VelocityVolume.SampleLevel(TrilinearFetch, SamplePoint - float3(0.0, 0.0, Offset), 0.0).xyz;

    let GradientX = (RightVelocity - LeftVelocity) * 0.5;
    let GradientY = (UpperVelocity - LowerVelocity) * 0.5;
    let GradientZ = (FrontVelocity - BackVelocity) * 0.5;

    return sqrt(dot(GradientX, GradientX) + dot(GradientY, GradientY) + dot(GradientZ, GradientZ));
}

float3 SynthesizeDetailBand(float3 Position, float BandScale)
{
    let Probe   = Position * BandScale;

    let Epsilon = 0.75;

    let PotentialX = FractalNoise(Probe);
    let PotentialY = FractalNoise(Probe + float3(31.416, 11.180, 71.828));
    let PotentialZ = FractalNoise(Probe + float3(57.721, 93.147, 26.180));

    let GradientYx = FractalNoise(Probe + float3(Epsilon, 0.0, 0.0) + float3(31.416, 11.180, 71.828)) - PotentialY;
    let GradientZx = FractalNoise(Probe + float3(Epsilon, 0.0, 0.0) + float3(57.721, 93.147, 26.180)) - PotentialZ;
    let GradientXy = FractalNoise(Probe + float3(0.0, Epsilon, 0.0)) - PotentialX;
    let GradientZy = FractalNoise(Probe + float3(0.0, Epsilon, 0.0) + float3(57.721, 93.147, 26.180)) - PotentialZ;
    let GradientXz = FractalNoise(Probe + float3(0.0, 0.0, Epsilon)) - PotentialX;
    let GradientYz = FractalNoise(Probe + float3(0.0, 0.0, Epsilon) + float3(31.416, 11.180, 71.828)) - PotentialY;

    return float3(GradientZy - GradientYz, GradientXz - GradientZx, GradientYx - GradientXy) / Epsilon;
}

float3 ApplyTurbulentDisplacement(float3 SamplePoint)
{
    if (View.TurbulenceStrength <= 0.0001) { return SamplePoint; }

    let ResolvedVelocity = VelocityVolume.SampleLevel(TrilinearFetch, SamplePoint, 0.0).xyz;
    let CarriedPoint     = SamplePoint - ResolvedVelocity * View.TurbulenceDrift
                         + float3(0.0, View.ElapsedInterval * 0.05, 0.0);

    let StrainRate  = EvaluateStrainRate(SamplePoint);
    let StrainGate  = tanh(StrainRate * View.TurbulenceBand);

    let CoarseBand = SynthesizeDetailBand(CarriedPoint, View.TurbulenceDetail);
    let FineBand   = SynthesizeDetailBand(CarriedPoint, View.TurbulenceDetail * 2.03) * 0.5;

    let CellSize     = 1.0 / max(View.TurbulenceDetail, 1.0);
    let Displacement = (CoarseBand + FineBand) * StrainGate * View.TurbulenceStrength * CellSize;

    return clamp(SamplePoint + Displacement, float3(0.0), float3(1.0));
}

float EvaluateShadowOpticalDepth(float3 SamplePoint)
{
    let StepCount = int(View.ShadowStepCount);
    if (StepCount <= 0) { return 0.0; }

    let ShadowExtent = IntersectUnitCube(SamplePoint, View.ShadowDirection);
    if (ShadowExtent.y <= ShadowExtent.x) { return 0.0; }

    let StepLength   = (ShadowExtent.y - ShadowExtent.x) / float(StepCount);
    var Accumulated  = 0.0;

    for (var StepIndex = 0; StepIndex < StepCount; StepIndex = StepIndex + 1)
    {
        let Parameter   = ShadowExtent.x + (float(StepIndex) + 0.5) * StepLength;
        let ProbePoint  = SamplePoint + View.ShadowDirection * Parameter;
        let ProbeSample = ScalarVolume.SampleLevel(TrilinearFetch, ProbePoint, 0.0);
        Accumulated    += (ProbeSample.x + ProbeSample.w * View.SootOpacity) * StepLength;
    }

    return Accumulated * View.DensityScale;
}

struct ScatterResponse
{
    float Weighting;
    float Isotropy;
};

// 📝 One shadow march feeds multiple decaying scattering bands; do not apply transmittance twice.
ScatterResponse EvaluateDeepScatter(float OpticalDepth, float ScatterCosine)
{
    let BandCount = int(View.ScatterBandCount);

    var Weighting = 0.0;
    var Isotropy  = 0.0;
    var Normaliser = 0.0;
    var EnergyTotal = 0.0;

    var BandAttenuation = 1.0;
    var BandContribution = 1.0;
    var BandAnisotropy   = View.ScatterAnisotropy;

    for (var BandIndex = 0; BandIndex < BandCount; BandIndex = BandIndex + 1)
    {
        let BandTransmittance = exp(-OpticalDepth * BandAttenuation);
        let BandPhase         = HenyeyGreenstein(ScatterCosine, BandAnisotropy);

        let BandEnergy = BandContribution * BandTransmittance;
        Weighting  += BandEnergy * BandPhase;

        Isotropy   += BandEnergy * (1.0 - abs(BandAnisotropy));
        Normaliser += BandContribution;
        EnergyTotal += BandEnergy;

        BandAttenuation  *= View.ScatterExtinctionDecay;
        BandContribution *= View.ScatterContributionDecay;
        BandAnisotropy   *= View.ScatterAnisotropyDecay;
    }

    ScatterResponse Response;

    Response.Weighting = Weighting / max(Normaliser, 1e-4);
    Response.Isotropy  = clamp(Isotropy / max(EnergyTotal, 1e-6), 0.0, 1.0);
    return Response;
}

[shader("fragment")]
float4 IntegrateVolumeRadiance(RasterVertex Raster) : SV_Target0
{

    let ScreenOffset = float2(Raster.ScreenPoint.x * View.AspectRatio, Raster.ScreenPoint.y);
    let RayDirection = normalize(View.CameraBasisForward
                               + View.CameraBasisRight * ScreenOffset.x
                               + View.CameraBasisUp    * ScreenOffset.y);

    let MarchExtent = IntersectUnitCube(View.CameraOrigin, RayDirection);

    let BackdropTint = float3(0.0);

    if (MarchExtent.y <= MarchExtent.x)
    {
        return float4(CageRadiance(View.CameraOrigin, RayDirection, 1e30), 1.0);
    }

    let StepCount  = int(View.MarchStepCount);
    let StepLength = (MarchExtent.y - MarchExtent.x) / float(StepCount);

    var AccumulatedRadiance      = float3(0.0);
    var AccumulatedTransmittance = float3(1.0);

    for (var StepIndex = 0; StepIndex < StepCount; StepIndex = StepIndex + 1)
    {

        if (max(AccumulatedTransmittance.r,
                max(AccumulatedTransmittance.g, AccumulatedTransmittance.b)) < 0.003) { break; }

        let Parameter   = MarchExtent.x + (float(StepIndex) + 0.5) * StepLength;
        let SamplePoint = View.CameraOrigin + RayDirection * Parameter;

        // Carrier alone is displaced; the coarse irradiance and shadow lookups stay in resolved space.
        let DetailPoint = ApplyTurbulentDisplacement(SamplePoint);
        let Carrier     = ScalarVolume.SampleLevel(TrilinearFetch, DetailPoint, 0.0);

        let Density     = Carrier.x;
        let Temperature = Carrier.y;
        let Soot        = Carrier.w;

        let Fuel = Carrier.z;

        let Extinction = (Density + Soot * View.SootOpacity) * View.DensityScale;
        if (Extinction < 1e-5 && Temperature < 1e-4) { continue; }

        let SpectralWeight    = lerp(float3(1.0), View.AbsorptionTint, View.SpectralStrength);
        let StepTransmittance = exp(-Extinction * StepLength * SpectralWeight);

        let RadiatingFraction = max(Temperature - View.EmissionThreshold, 0.0);
        var SampleRadiance    = EmissionColour(RadiatingFraction) * View.RadianceScale;

        if (Fuel > 0.0001 && Temperature > 0.02)
        {
            let CoreBand    = Fuel * exp(-Fuel / max(View.CoreThickness, 0.001));
            let CoreHeat    = min(Temperature * 1.4, 1.0);
            let CoreRadiance = float3(0.24, 0.52, 1.00) * CoreBand * CoreHeat * View.CoreLuminosity;
            SampleRadiance += CoreRadiance;
        }

        if (Extinction > 1e-5)
        {
            let ShadowDepth   = EvaluateShadowOpticalDepth(SamplePoint);
            let ScatterCosine = dot(RayDirection, View.ShadowDirection);

            let Scatter     = EvaluateDeepScatter(ShadowDepth, ScatterCosine);
            let PhaseWeight = Scatter.Weighting;

            let CachedIrradiance = IrradianceVolume.SampleLevel(TrilinearFetch, SamplePoint, 0.0).xyz;
            var AmbientTerm = lerp(float3(0.34, 0.38, 0.46) * 0.55,
                                  CachedIrradiance, View.IrradianceBlend);

            let SunColour = float3(1.00, 0.96, 0.88);
            AmbientTerm  += SunColour * Scatter.Isotropy * View.MultiScatterAmbient;

            let DirectTerm  = SunColour * PhaseWeight * 5.5;

            let SmokeDepth = clamp(Density * View.SmokeRampScale, 0.0, 1.0);
            let SmokeTint  = lerp(float3(1.0), SamplePaletteRamp(SmokeDepth, 1.0), View.PaletteBlend);

            SampleRadiance += (AmbientTerm + DirectTerm) * Extinction * View.SmokeAlbedo * SmokeTint;
        }

        AccumulatedRadiance      += SampleRadiance * AccumulatedTransmittance * StepLength;
        AccumulatedTransmittance *= StepTransmittance;
    }

    let Selector = int(View.ChannelSelector);
    if (Selector == 1)
    {
        let Probe = VelocityVolume.SampleLevel(TrilinearFetch,
                                       View.CameraOrigin + RayDirection * (MarchExtent.x + (MarchExtent.y - MarchExtent.x) * 0.5), 0.0);
        return float4(abs(Probe.xyz) * 3.0, 1.0);
    }

    let FarCage  = CageRadiance(View.CameraOrigin, RayDirection, 1e30) * AccumulatedTransmittance;
    var Composited = BackdropTint * AccumulatedTransmittance + AccumulatedRadiance + FarCage;

    Composited = Composited * View.ExposureScale;

    // Luminance ACES fit, restored chromaticity, then display gamma.
    let InputLuminance  = max(dot(Composited, float3(0.2126, 0.7152, 0.0722)), 1e-6);
    let ShapedLuminance = InputLuminance * (2.6 / max(View.HighlightShoulder, 0.05));
    let MappedLuminance = clamp((ShapedLuminance * (2.51 * ShapedLuminance + 0.03))
                              / (ShapedLuminance * (2.43 * ShapedLuminance + 0.59) + 0.14),
                                0.0, 1.0);

    var Chromaticity = Composited / InputLuminance;
    Chromaticity = pow(max(Chromaticity, float3(0.0)), float3(View.SaturationGain));
    Composited = Chromaticity * MappedLuminance;

    Composited = pow(max(Composited, float3(0.0)), float3(1.0 / 2.2));

    return float4(Composited, 1.0);
}
