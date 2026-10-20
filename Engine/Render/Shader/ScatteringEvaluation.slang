/*==========================================================================================================================================
                                                     SCATTERINGEVALUATION.SLANG
==========================================================================================================================================*/
// 🧩 Shared angular scattering and scene-linear display conversion for preview and authored materials.

struct ScatteringStratum
{
    float4 AlbedoRoughness;
    float4 Optics;
    float4 AbsorptionThickness;
    uint4 Classification;
};
struct PreviewScattering
{
    column_major float4x4 ViewFromSurface;
    float4 LightDirectionIntensity;
    float4 LightColourExposure;
    float4 EnvironmentHorizon;
    float4 EnvironmentZenith;
    float4 EnvironmentUp;
    uint4 Configuration;
    ScatteringStratum Strata[8];
};
static const float π = 3.141592653589793;

float3 NormalizeDirection(float3 Direction)
{
    float Maximum = max(abs(Direction.x),max(abs(Direction.y),abs(Direction.z)));
    Direction = Maximum > 1e-20 ? Direction/Maximum : float3(0,0,1);
    return normalize(Direction);
}
float EvaluateGgxDistribution(float NormalHalf,float Roughness)
{
    float α = Roughness*Roughness;
    float Denominator = NormalHalf*NormalHalf*(α*α-1)+1;
    return α*α/(π*Denominator*Denominator);
}
float2 DeriveAnisotropicSpread(float Roughness,float Anisotropy)
{
    float α = Roughness*Roughness;
    float Aspect = sqrt(1-.9*abs(Anisotropy));
    return Anisotropy >= 0 ? float2(α/Aspect,α*Aspect) : float2(α*Aspect,α/Aspect);
}
float EvaluateAnisotropicDistribution(float3 Half,float2 Spread)
{
    if (Half.z <= 0) return 0;
    float Denominator = dot(Half.xy/Spread,Half.xy/Spread)+Half.z*Half.z;
    return 1/(π*Spread.x*Spread.y*Denominator*Denominator);
}
float EvaluateAnisotropicVisibility(float3 View,float3 Light,float2 Spread)
{
    if (View.z <= 0 || Light.z <= 0) return 0;
    float ViewLength = length(float3(View.xy*Spread,View.z));
    float LightLength = length(float3(Light.xy*Spread,Light.z));
    return .5/max(1e-12,Light.z*ViewLength+View.z*LightLength);
}
float EvaluateExactDielectricReflectance(float Cosine,float IncidentIor,float TransmittedIor)
{
    if (IncidentIor == TransmittedIor) return 0;
    Cosine = saturate(abs(Cosine));
    float Ratio = IncidentIor/TransmittedIor;
    float SineSquared = Ratio*Ratio*(1-Cosine*Cosine);
    if (SineSquared >= 1) return 1;
    float TransmittedCosine = sqrt(1-SineSquared);
    float Perpendicular = (IncidentIor*Cosine-TransmittedIor*TransmittedCosine)/
                          (IncidentIor*Cosine+TransmittedIor*TransmittedCosine);
    float Parallel = (TransmittedIor*Cosine-IncidentIor*TransmittedCosine)/
                     (TransmittedIor*Cosine+IncidentIor*TransmittedCosine);
    return .5*(Perpendicular*Perpendicular+Parallel*Parallel);
}
float3 EvaluateScatteringReflection(ScatteringStratum Stratum,float3 Albedo,float ViewHalf,
                                   float Distribution,float Visibility,bool ExactDielectricEnabled)
{
    float Reflectance = (Stratum.Optics.y-1)/(Stratum.Optics.y+1);
    Reflectance *= Reflectance;
    float3 FresnelOrigin = lerp(float3(Reflectance),Albedo,Stratum.Optics.x);
    float3 Fresnel = ExactDielectricEnabled ? float3(EvaluateExactDielectricReflectance(ViewHalf,1,Stratum.Optics.y)) :
        FresnelOrigin+(1-FresnelOrigin)*pow(1-ViewHalf,5);
    float3 Evaluation = Distribution*Visibility*Fresnel;
    if (Stratum.Classification.x == 0) Evaluation += (1-Fresnel)*(1-Stratum.Optics.x)*Albedo/π;
    return Evaluation;
}

float3 EvaluateScatteringSequence(PreviewScattering Scattering,float3 Normal,float3 View,float3 Light,float3 AlbedoMultiplier)
{
    float NormalView = saturate(dot(Normal,View));
    float NormalLight = saturate(dot(Normal,Light));
    if (NormalView <= 0 || NormalLight <= 0) return 0;
    float3 Half = NormalizeDirection(View+Light);
    float NormalHalf = saturate(dot(Normal,Half));
    float ViewHalf = saturate(dot(View,Half));
    float3 Accumulated = 0;
    for (uint Index = 0; Index < Scattering.Configuration.x; ++Index)
    {
        ScatteringStratum Stratum = Scattering.Strata[Index];
        float3 Albedo = Stratum.AlbedoRoughness.rgb * (Index == 0 ? AlbedoMultiplier : float3(1));
        float α = Stratum.AlbedoRoughness.w*Stratum.AlbedoRoughness.w;
        float Visibility = 0.5/max(1e-12,NormalLight*sqrt(NormalView*NormalView*(1-α*α)+α*α) +
                                       NormalView*sqrt(NormalLight*NormalLight*(1-α*α)+α*α));
        float Distribution = EvaluateGgxDistribution(NormalHalf,Stratum.AlbedoRoughness.w);
        float Reflectance = (Stratum.Optics.y-1)/(Stratum.Optics.y+1);
        Reflectance *= Reflectance;
        float3 Evaluation = EvaluateScatteringReflection(Stratum,Albedo,ViewHalf,Distribution,Visibility,false);
        if (Stratum.Classification.x == 1)
        {
            float Entrance = Reflectance+(1-Reflectance)*pow(1-NormalLight,5);
            float Exit = Reflectance+(1-Reflectance)*pow(1-NormalView,5);
            float3 Absorption = exp(-Stratum.AbsorptionThickness.rgb*Stratum.AbsorptionThickness.w*
                                    (1/max(.001,NormalView)+1/max(.001,NormalLight)));
            Evaluation += Accumulated*(1-Entrance)*(1-Exit)*Absorption;
        }
        Accumulated = lerp(Accumulated,Evaluation,Stratum.Optics.z);
    }
    return Accumulated;
}
float3 EvaluateEnvironmentRadiance(PreviewScattering Scattering,float3 Direction)
{
    float Height = saturate(.5+.5*dot(NormalizeDirection(Scattering.EnvironmentUp.xyz),Direction));
    return lerp(Scattering.EnvironmentHorizon.rgb*.35,Scattering.EnvironmentZenith.rgb,Height)*Scattering.EnvironmentHorizon.w;
}
float3 IntegrateEnvironment(PreviewScattering Scattering,float3 Normal,float3 View,float3 AlbedoMultiplier)
{
    if (Scattering.EnvironmentHorizon.w == 0) return 0;
    float3 Tangent = NormalizeDirection(cross(abs(Normal.z)<.999 ? float3(0,0,1) : float3(0,1,0),Normal));
    float3 Bitangent = cross(Normal,Tangent);
    float3 Accumulated = 0;
    uint Components = Scattering.Configuration.x+1;
    // 📝 Balance the cosine distribution with one GGX distribution per stratum; eight deterministic samples each.
    // This is a bounded preview quadrature, not a converged reference integrator or a prefiltered environment archive.
    for (uint Component = 0; Component < Components; ++Component)
        for (uint SampleIndex = 0; SampleIndex < 8; ++SampleIndex)
        {
            float First = (SampleIndex+.5)/8;
            float Second = frac(SampleIndex*.61803398875+Component*.371);
            float φ = 2*π*Second;
            float Cosine = sqrt(1-First);
            if (Component != 0)
            {
                float Roughness = Scattering.Strata[Component-1].AlbedoRoughness.w;
                float α = Roughness*Roughness;
                Cosine = sqrt((1-First)/(1+(α*α-1)*First));
            }
            float Sine = sqrt(max(0,1-Cosine*Cosine));
            float3 Direction = Tangent*(Sine*cos(φ))+Bitangent*(Sine*sin(φ))+Normal*Cosine;
            float3 Light = Component == 0 ? Direction : reflect(-View,Direction);
            float NormalLight = dot(Normal,Light);
            if (NormalLight <= 0) continue;
            float3 Half = NormalizeDirection(View+Light);
            float NormalHalf = saturate(dot(Normal,Half));
            float ViewHalf = max(1e-6,dot(View,Half));
            float Density = NormalLight/π;
            for (uint Index = 0; Index < Scattering.Configuration.x; ++Index)
                Density += EvaluateGgxDistribution(NormalHalf,Scattering.Strata[Index].AlbedoRoughness.w)*NormalHalf/(4*ViewHalf);
            Density /= Components;
            Accumulated += EvaluateScatteringSequence(Scattering,Normal,View,Light,AlbedoMultiplier)*
                           EvaluateEnvironmentRadiance(Scattering,Light)*NormalLight/max(1e-8,Density);
        }
    return Accumulated/(8*Components);
}
float3 EncodeDisplay(float3 Radiance,float Exposure)
{
    float3 Exposed = max(0,Radiance*Exposure);
    float3 Display = saturate(Exposed*(2.51*Exposed+.03)/(Exposed*(2.43*Exposed+.59)+.14));
    return select(Display<=.0031308,12.92*Display,1.055*pow(Display,float3(1.0/2.4))-.055);
}
