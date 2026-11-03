/*============================================================================================================================================
                                                          SHADERS.JS
============================================================================================================================================*/
// 🧩 GLSL ES 3.0 programs for sky, crust clipmaps, simulated and spectral ocean, 2.5D solver and editor overlays

const CameraProjection = `
uniform vec3 CameraRight;
uniform vec3 CameraUp;
uniform vec3 CameraForward;
uniform vec2 ProjectionScale;
uniform float LogFarPlane;
uniform float NearPlane;
uniform float FarPlane;
uniform vec3 CameraAbsolute;

vec4 ProjectCameraRelative(vec3 Rel)
{
    vec3 CameraSpace = vec3(dot(CameraRight, Rel), dot(CameraUp, Rel), dot(-CameraForward, Rel));
    float Depth = -CameraSpace.z;
    if (Depth < NearPlane) return vec4(2.0, 2.0, 2.0, 1.0);
    vec4 Clip = vec4(CameraSpace.x * ProjectionScale.x, CameraSpace.y * ProjectionScale.y, 0.0, Depth);
    Clip.z = (2.0 * log2(1.0 + Depth) / LogFarPlane - 1.0) * Depth;
    return Clip;
}
`;

const Atmosphere = `
uniform vec3 SunDirection;
uniform float HazeDensity;

vec3 EvaluateSky(vec3 Direction)
{
    float Elevation = clamp(Direction.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 Horizon = vec3(0.60, 0.72, 0.81);
    vec3 Zenith = vec3(0.075, 0.20, 0.44);
    vec3 BaseColour = mix(Horizon, Zenith, pow(Elevation, 0.72));
    float Alignment = max(0.0, dot(Direction, SunDirection));
    float Disc = pow(Alignment, 1600.0);
    float Glow = pow(Alignment, 14.0);
    return BaseColour + vec3(1.0, 0.88, 0.68) * Disc * 6.0 + vec3(0.48, 0.34, 0.20) * Glow * 0.22;
}
`;

const HeightfieldSampling = `
uniform highp sampler2D ElevationField;
uniform int ElevationResolution;
uniform float DomainExtent;
uniform vec2 CameraOffset;

float SampleElevationAtWorld(vec2 World)
{
    vec2 Uv = (World + DomainExtent * 0.5) / DomainExtent;
    vec2 Texel = clamp(Uv, 0.0, 1.0) * float(ElevationResolution) - 0.5;
    ivec2 BaseTexel = ivec2(floor(Texel));
    vec2 Fraction = fract(Texel);
    ivec2 Maximum = ivec2(ElevationResolution - 1);
    float SouthWest = texelFetch(ElevationField, clamp(BaseTexel, ivec2(0), Maximum), 0).r;
    float SouthEast = texelFetch(ElevationField, clamp(BaseTexel + ivec2(1, 0), ivec2(0), Maximum), 0).r;
    float NorthWest = texelFetch(ElevationField, clamp(BaseTexel + ivec2(0, 1), ivec2(0), Maximum), 0).r;
    float NorthEast = texelFetch(ElevationField, clamp(BaseTexel + ivec2(1, 1), ivec2(0), Maximum), 0).r;
    float South = mix(SouthWest, SouthEast, Fraction.x);
    float North = mix(NorthWest, NorthEast, Fraction.x);
    return mix(South, North, Fraction.y);
}
`;

export const FullscreenVertexShader = `#version 300 es
precision highp float;
precision highp sampler2D;
out vec2 ScreenCoordinate;
void main()
{
    vec2 Position = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    ScreenCoordinate = Position * 2.0 - 1.0;
    gl_Position = vec4(ScreenCoordinate, 0.0, 1.0);
}
`;

export const SkyFragmentShader = `#version 300 es
precision highp float;
precision highp sampler2D;
in vec2 ScreenCoordinate;
out vec4 Colour;
${CameraProjection}
${Atmosphere}
void main()
{
    vec3 Direction = normalize(CameraForward + CameraRight * (ScreenCoordinate.x / ProjectionScale.x)
        + CameraUp * (ScreenCoordinate.y / ProjectionScale.y));
    Colour = vec4(EvaluateSky(Direction), 1.0);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                                     CRUST CLIPMAPS
//------------------------------------------------------------------------------------------------------------------------

export const TerrainVertexShader = `#version 300 es
precision highp float;
precision highp sampler2D;
precision highp int;
layout(location = 0) in vec2 GridCoordinate;
${CameraProjection}
${HeightfieldSampling}
uniform vec3 PatchOrigin;
uniform float PatchExtent;
uniform float CameraElevation;
uniform float PlanetRadius;
uniform float VerticalExaggeration;
uniform int GeometryMode;
out vec3 RelPosition;
out vec3 SurfaceNormal;
out float Elevation;
out vec2 FieldUv;
out vec3 LodTint;

void main()
{
    vec3 Rel;
    vec2 World;
    if (GeometryMode == 1)
    {
        float Longitude = (GridCoordinate.x - 0.5) * 6.28318530718;
        float Latitude = (GridCoordinate.y - 0.5) * 3.14159265359;
        vec3 Unit = vec3(cos(Latitude) * cos(Longitude), sin(Latitude), cos(Latitude) * sin(Longitude));
        vec2 Uv = vec2(GridCoordinate.x, GridCoordinate.y);
        float Height = texelFetch(ElevationField, ivec2(clamp(Uv, 0.0, 1.0) * float(ElevationResolution - 1)), 0).r;
        Elevation = Height;
        FieldUv = Uv;
        vec3 SpherePosition = Unit * (PlanetRadius + Height * VerticalExaggeration);
        Rel = SpherePosition - CameraAbsolute;
        SurfaceNormal = Unit;
    }
    else
    {
        World = CameraOffset + PatchOrigin.xz + GridCoordinate * PatchExtent;
        Elevation = SampleElevationAtWorld(World);
        FieldUv = (World + DomainExtent * 0.5) / DomainExtent;
        float Texel = DomainExtent / float(ElevationResolution);
        float HeightEast = SampleElevationAtWorld(World + vec2(Texel, 0.0));
        float HeightWest = SampleElevationAtWorld(World - vec2(Texel, 0.0));
        float HeightNorth = SampleElevationAtWorld(World + vec2(0.0, Texel));
        float HeightSouth = SampleElevationAtWorld(World - vec2(0.0, Texel));
        SurfaceNormal = normalize(vec3(HeightWest - HeightEast, 2.0 * Texel, HeightSouth - HeightNorth));
        Rel = vec3(PatchOrigin.x + GridCoordinate.x * PatchExtent, Elevation - CameraElevation, PatchOrigin.z + GridCoordinate.y * PatchExtent);
    }
    RelPosition = Rel;
    LodTint = vec3(0.0);
    gl_Position = ProjectCameraRelative(Rel);
}
`;

export const TerrainFragmentShader = `#version 300 es
precision highp float;
precision highp sampler2D;
in vec3 RelPosition;
in vec3 SurfaceNormal;
in float Elevation;
in vec2 FieldUv;
in vec3 LodTint;
out vec4 Colour;
${CameraProjection}
${Atmosphere}
uniform float SeaLevel;
uniform float ElapsedSeconds;
uniform int ViewMode;
uniform vec3 TintColour;
uniform float ContourInterval;
uniform float DetailStrength;

float EvaluateContour(float HeightValue)
{
    float Phase = abs(fract(HeightValue / ContourInterval - 0.5) - 0.5) * ContourInterval;
    return 1.0 - smoothstep(0.0, max(1.0, ContourInterval * 0.012), Phase);
}

void main()
{
    vec3 Normal = normalize(SurfaceNormal);
    float Distance = length(RelPosition);
    vec3 View = normalize(-RelPosition);
    if (ViewMode == 1)
    {
        float Depth = clamp((SeaLevel - Elevation) / 4000.0, 0.0, 1.0);
        vec3 OceanGradient = mix(vec3(0.03, 0.34, 0.42), vec3(0.01, 0.02, 0.12), Depth);
        vec3 LandGradient = mix(vec3(0.18, 0.55, 0.24), vec3(0.86, 0.82, 0.70), clamp(Elevation / 3200.0, 0.0, 1.0));
        vec3 BaseColour = Elevation < SeaLevel ? OceanGradient : LandGradient;
        BaseColour *= 1.0 - 0.65 * EvaluateContour(Elevation);
        Colour = vec4(mix(BaseColour, vec3(0.62, 0.75, 0.85), 1.0 - exp(-Distance * HazeDensity * 0.7)), 1.0);
        return;
    }

    vec3 Deep = vec3(0.055, 0.13, 0.16);
    vec3 Shelf = vec3(0.25, 0.32, 0.26);
    vec3 Sand = vec3(0.72, 0.66, 0.46);
    vec3 Lowland = vec3(0.24, 0.37, 0.19);
    vec3 Highland = vec3(0.42, 0.38, 0.29);
    vec3 Rock = vec3(0.46, 0.45, 0.43);
    vec3 Snow = vec3(0.90, 0.93, 0.96);
    vec3 BaseColour;
    if (Elevation < SeaLevel - 1200.0) BaseColour = mix(Shelf, Deep, clamp((-Elevation - 1200.0) / 2600.0, 0.0, 1.0));
    else if (Elevation < SeaLevel) BaseColour = mix(Shelf, Sand, clamp((Elevation - SeaLevel + 1200.0) / 1200.0, 0.0, 1.0));
    else if (Elevation < 240.0) BaseColour = mix(Sand, Lowland, clamp(Elevation / 240.0, 0.0, 1.0));
    else if (Elevation < 1100.0) BaseColour = mix(Lowland, Highland, clamp((Elevation - 240.0) / 860.0, 0.0, 1.0));
    else if (Elevation < 2300.0) BaseColour = mix(Highland, Rock, clamp((Elevation - 1100.0) / 1200.0, 0.0, 1.0));
    else BaseColour = mix(Rock, Snow, clamp((Elevation - 2300.0) / 900.0, 0.0, 1.0));

    float Slope = 1.0 - clamp(Normal.y, 0.0, 1.0);
    BaseColour = mix(BaseColour, Rock, Slope * 0.55);
    BaseColour *= 0.86 + 0.14 * EvaluateContour(Elevation);
    float Lambert = max(0.0, dot(Normal, SunDirection));
    float Ambient = 0.28 + 0.16 * clamp(Normal.y, 0.0, 1.0);
    vec3 Lit = BaseColour * (Ambient + Lambert * 0.95) + vec3(0.9, 0.82, 0.66) * pow(max(0.0, dot(reflect(-SunDirection, Normal), View)), 24.0) * 0.06;
    if (ViewMode == 2) Lit = mix(Lit, TintColour, 0.72);
    float Fog = 1.0 - exp(-Distance * HazeDensity);
    vec3 FogColour = EvaluateSky(-View);
    Colour = vec4(mix(Lit, FogColour, Fog), 1.0);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                                       OCEAN SURFACE
//------------------------------------------------------------------------------------------------------------------------

const SpectralDisplacement = `
uniform float ElapsedSeconds;
uniform float SwellAmplitude;
uniform float SwellWavelength;
uniform vec2 SwellDirection;
uniform float SpectralSteepness;

vec3 EvaluateSpectral(vec2 World, out vec3 AnalyticNormal)
{
    vec2 DirectionUnit = normalize(SwellDirection);
    vec3 Displacement = vec3(0.0);
    vec3 GradientX = vec3(0.0);
    vec3 GradientZ = vec3(0.0);
    float Amplitude = SwellAmplitude;
    float WaveNumber = 6.28318530718 / SwellWavelength;
    for (int Harmonic = 0; Harmonic < 5; ++Harmonic)
    {
        float Index = float(Harmonic);
        float Angle = Index * 1.0472;
        vec2 Direction = vec2(DirectionUnit.x * cos(Angle) - DirectionUnit.y * sin(Angle),
                              DirectionUnit.x * sin(Angle) + DirectionUnit.y * cos(Angle));
        float HarmonicWaveNumber = WaveNumber * pow(1.42, Index);
        float AngularFrequency = sqrt(9.81 * HarmonicWaveNumber);
        float Phase = HarmonicWaveNumber * dot(Direction, World) - AngularFrequency * ElapsedSeconds * 0.65 + Index * 2.1;
        float HarmonicAmplitude = Amplitude * pow(0.66, Index);
        Displacement.y += HarmonicAmplitude * sin(Phase);
        Displacement.x += SpectralSteepness * HarmonicAmplitude * Direction.x * cos(Phase);
        Displacement.z += SpectralSteepness * HarmonicAmplitude * Direction.y * cos(Phase);
        GradientX += vec3(-SpectralSteepness * HarmonicAmplitude * Direction.x * HarmonicWaveNumber * Direction.x * sin(Phase),
                           HarmonicAmplitude * HarmonicWaveNumber * Direction.x * cos(Phase),
                          -SpectralSteepness * HarmonicAmplitude * Direction.x * HarmonicWaveNumber * Direction.y * sin(Phase));
        GradientZ += vec3(-SpectralSteepness * HarmonicAmplitude * Direction.y * HarmonicWaveNumber * Direction.x * sin(Phase),
                           HarmonicAmplitude * HarmonicWaveNumber * Direction.y * cos(Phase),
                          -SpectralSteepness * HarmonicAmplitude * Direction.y * HarmonicWaveNumber * Direction.y * sin(Phase));
    }
    AnalyticNormal = normalize(cross(vec3(1.0, 0.0, 0.0) + GradientX, vec3(0.0, 0.0, 1.0) + GradientZ) * vec3(-1.0, 1.0, -1.0));
    if (AnalyticNormal.y < 0.0) AnalyticNormal = -AnalyticNormal;
    return Displacement;
}
`;

export const WaterVertexShader = `#version 300 es
precision highp float;
precision highp sampler2D;
precision highp int;
layout(location = 0) in vec2 GridCoordinate;
${CameraProjection}
${HeightfieldSampling}
${SpectralDisplacement}
uniform vec2 WindowOriginRelative;
uniform float WindowExtent;
uniform float CameraElevation;
uniform float SeaLevel;
uniform float PlanetRadius;
uniform int GeometryMode;
uniform int DisplacementSource;
uniform highp sampler2D StateField;
uniform int GridResolution;
uniform ivec2 Shift;
uniform float CellSpacing;
out vec3 RelPosition;
out vec3 SurfaceNormal;
out float WaterDepth;
out vec2 Velocity;
out float SurfaceEta;
out vec3 LodTint;

ivec2 WrapCell(ivec2 Coordinate)
{
    int ResolutionLimit = GridResolution;
    return ivec2(((Coordinate.x % ResolutionLimit) + ResolutionLimit) % ResolutionLimit, ((Coordinate.y % ResolutionLimit) + ResolutionLimit) % ResolutionLimit);
}

void main()
{
    vec2 World = CameraOffset + WindowOriginRelative + GridCoordinate * WindowExtent;
    vec3 Rel;
    vec3 Normal = vec3(0.0, 1.0, 0.0);
    float Eta = 0.0;
    vec2 FlowVelocity = vec2(0.0);
    float TerrainHeight = SampleElevationAtWorld(World);

    if (DisplacementSource == 0)
    {
        ivec2 WindowCell = ivec2(min(vec2(float(GridResolution - 1)), floor(GridCoordinate * float(GridResolution))));
        ivec2 Texel = WrapCell(WindowCell + Shift);
        vec4 CentreState = texelFetch(StateField, Texel, 0);
        vec4 EastState = texelFetch(StateField, WrapCell(Texel + ivec2(1, 0)), 0);
        vec4 WestState = texelFetch(StateField, WrapCell(Texel - ivec2(1, 0)), 0);
        vec4 NorthState = texelFetch(StateField, WrapCell(Texel + ivec2(0, 1)), 0);
        vec4 SouthState = texelFetch(StateField, WrapCell(Texel - ivec2(0, 1)), 0);
        Eta = CentreState.r;
        FlowVelocity = CentreState.gb;
        float GradientX = (EastState.r - WestState.r) / (2.0 * CellSpacing);
        float GradientZ = (NorthState.r - SouthState.r) / (2.0 * CellSpacing);
        Normal = normalize(vec3(-GradientX, 1.0, -GradientZ));
        Rel = vec3(WindowOriginRelative.x + GridCoordinate.x * WindowExtent, SeaLevel + Eta - CameraElevation,
                   WindowOriginRelative.y + GridCoordinate.y * WindowExtent);
    }
    else if (DisplacementSource == 1)
    {
        vec3 AnalyticNormal;
        vec3 Displacement = EvaluateSpectral(World, AnalyticNormal);
        Normal = AnalyticNormal;
        Eta = Displacement.y;
        Rel = vec3(WindowOriginRelative.x + GridCoordinate.x * WindowExtent + Displacement.x,
                   SeaLevel + Eta - CameraElevation,
                   WindowOriginRelative.y + GridCoordinate.y * WindowExtent + Displacement.z);
    }
    else
    {
        float Longitude = (GridCoordinate.x - 0.5) * 6.28318530718;
        float Latitude = (GridCoordinate.y - 0.5) * 3.14159265359;
        vec3 Unit = vec3(cos(Latitude) * cos(Longitude), sin(Latitude), cos(Latitude) * sin(Longitude));
        vec3 AnalyticNormal;
        vec3 Displacement = EvaluateSpectral(World, AnalyticNormal);
        Eta = Displacement.y;
        Normal = normalize(Unit + AnalyticNormal * 0.06);
        vec3 SpherePosition = Unit * (PlanetRadius + Eta);
        Rel = SpherePosition - CameraAbsolute;
    }

    WaterDepth = max(0.0, SeaLevel - TerrainHeight + Eta);
    Velocity = FlowVelocity;
    SurfaceEta = Eta;
    RelPosition = Rel;
    SurfaceNormal = Normal;
    LodTint = vec3(0.0);
    gl_Position = ProjectCameraRelative(Rel);
}
`;

export const WaterFragmentShader = `#version 300 es
precision highp float;
precision highp sampler2D;
in vec3 RelPosition;
in vec3 SurfaceNormal;
in float WaterDepth;
in vec2 Velocity;
in float SurfaceEta;
in vec3 LodTint;
out vec4 Colour;
${CameraProjection}
${Atmosphere}
uniform int ViewMode;
uniform vec3 TintColour;
uniform float SeaLevel;

void main()
{
    float Distance = length(RelPosition);
    vec3 View = normalize(-RelPosition);
    vec3 Normal = normalize(SurfaceNormal);

    if (ViewMode == 3)
    {
        float Speed = length(Velocity);
        vec3 Heat = mix(vec3(0.02, 0.08, 0.28), vec3(1.0, 0.32, 0.06), clamp(Speed / 6.0, 0.0, 1.0));
        Colour = vec4(Heat, 1.0);
        return;
    }
    if (ViewMode == 2)
    {
        float Heat = clamp(SurfaceEta / 2.5 + 0.5, 0.0, 1.0);
        vec3 Diverging = mix(vec3(0.08, 0.28, 0.72), vec3(0.92, 0.30, 0.12), Heat);
        Colour = vec4(mix(Diverging, TintColour, 0.25), 1.0);
        return;
    }

    float Shoaling = 1.0 - exp(-WaterDepth / 55.0);
    vec3 ShallowBody = vec3(0.055, 0.40, 0.40);
    vec3 DeepBody = vec3(0.008, 0.055, 0.15);
    vec3 Body = mix(ShallowBody, DeepBody, Shoaling);
    float Fresnel = 0.028 + 0.972 * pow(1.0 - max(0.0, dot(Normal, View)), 5.0);
    vec3 Reflected = EvaluateSky(reflect(-View, Normal));
    vec3 HalfVector = normalize(SunDirection + View);
    float Specular = pow(max(0.0, dot(Normal, HalfVector)), 220.0) * 1.6;
    float Foam = (1.0 - smoothstep(0.0, 2.6, WaterDepth)) * (0.35 + 0.65 * clamp(length(Velocity) / 3.0, 0.0, 1.0));
    vec3 Surface = mix(Body, Reflected, Fresnel) + vec3(1.0, 0.93, 0.78) * Specular;
    Surface = mix(Surface, vec3(0.86, 0.93, 0.95), Foam * 0.7);
    if (ViewMode == 4) Surface = mix(Surface, TintColour, 0.68);
    float Fog = 1.0 - exp(-Distance * HazeDensity);
    Colour = vec4(mix(Surface, EvaluateSky(-View), Fog), 1.0);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                                  2.5D SOLVER PASSES
//------------------------------------------------------------------------------------------------------------------------

export const SolverFragmentShader = `#version 300 es
precision highp float;
precision highp sampler2D;
precision highp int;
out vec4 NextState;
${HeightfieldSampling}
uniform highp sampler2D StateField;
uniform highp sampler2D CoarseField;
uniform highp sampler2D FineField;
uniform ivec2 Shift;
uniform ivec2 OriginCells;
uniform ivec2 PreviousOriginCells;
uniform ivec2 FineOriginCells;
uniform int GridResolution;
uniform int CoarseEnabled;
uniform int FineEnabled;
uniform float CellSpacing;
uniform float CoarseSpacing;
uniform float FineSpacing;
uniform float DeltaTime;
uniform float Gravity;
uniform float SeaLevel;
uniform float Friction;
uniform float DryDepth;
uniform vec2 WindDirection;
uniform float WindStrength;
uniform float SwellAmplitude;
uniform float SwellWavelength;
uniform float ElapsedSeconds;
uniform vec4 Impulse;
uniform float MaximumDepth;

ivec2 WrapCell(ivec2 Coordinate)
{
    int ResolutionLimit = GridResolution;
    return ivec2(((Coordinate.x % ResolutionLimit) + ResolutionLimit) % ResolutionLimit, ((Coordinate.y % ResolutionLimit) + ResolutionLimit) % ResolutionLimit);
}

vec4 ReadStateAt(ivec2 Texel)
{
    return texelFetch(StateField, WrapCell(Texel), 0);
}

vec4 ReadRingAt(sampler2D Field, vec2 World, float Spacing)
{
    ivec2 Cell = ivec2(floor(World / Spacing));
    return texelFetch(Field, WrapCell(Cell), 0);
}

float ResolveDepth(vec4 FieldSample, vec2 World)
{
    return max(0.0, SeaLevel - SampleElevationAtWorld(World) + FieldSample.r);
}

vec4 EvaluateSwell(vec2 World)
{
    vec2 Direction = normalize(vec2(0.86, 0.51));
    float HarmonicWaveNumber = 6.28318530718 / SwellWavelength;
    float AngularFrequency = sqrt(Gravity * HarmonicWaveNumber);
    float Phase = HarmonicWaveNumber * dot(Direction, World) - AngularFrequency * ElapsedSeconds;
    float Eta = SwellAmplitude * sin(Phase) + 0.45 * SwellAmplitude * sin(1.7 * HarmonicWaveNumber * dot(Direction.yx, World) - 1.35 * AngularFrequency * ElapsedSeconds);
    vec2 FlowVelocity = Direction * (AngularFrequency / max(HarmonicWaveNumber, 0.0001)) * (Eta / max(10.0, MaximumDepth)) * 0.25;
    return vec4(Eta, FlowVelocity.x, FlowVelocity.y, 0.0);
}

void main()
{
    ivec2 Texel = ivec2(gl_FragCoord.xy);
    ivec2 WindowIndex = WrapCell(Texel - Shift);
    ivec2 Cell = OriginCells + WindowIndex;
    vec2 World = (vec2(Cell) + 0.5) * CellSpacing;
    bool NewlyExposed = any(lessThan(Cell, PreviousOriginCells)) || any(greaterThanEqual(Cell, PreviousOriginCells + ivec2(GridResolution)));

    vec4 Centre = ReadStateAt(Texel);
    vec4 EastState = ReadStateAt(Texel + ivec2(1, 0));
    vec4 WestState = ReadStateAt(Texel - ivec2(1, 0));
    vec4 NorthState = ReadStateAt(Texel + ivec2(0, 1));
    vec4 SouthState = ReadStateAt(Texel - ivec2(0, 1));

    float Eta = Centre.r;
    vec2 FlowVelocity = Centre.gb;
    float DepthCentre = ResolveDepth(Centre, World);
    float DepthEast = ResolveDepth(EastState, World + vec2(CellSpacing, 0.0));
    float DepthWest = ResolveDepth(WestState, World - vec2(CellSpacing, 0.0));
    float DepthNorth = ResolveDepth(NorthState, World + vec2(0.0, CellSpacing));
    float DepthSouth = ResolveDepth(SouthState, World - vec2(0.0, CellSpacing));

    // 1. Lax-Friedrichs continuity flux over the wet/dry bathymetry.
    float Celerity = sqrt(Gravity * max(DryDepth, max(max(DepthCentre, DepthEast), max(DepthWest, max(DepthNorth, DepthSouth)))));
    float FluxEast = 0.5 * (DepthCentre * FlowVelocity.x + DepthEast * EastState.g) - 0.5 * Celerity * (EastState.r - Eta);
    float FluxWest = 0.5 * (DepthWest * WestState.g + DepthCentre * FlowVelocity.x) - 0.5 * Celerity * (Eta - WestState.r);
    float FluxNorth = 0.5 * (DepthCentre * FlowVelocity.y + DepthNorth * NorthState.b) - 0.5 * Celerity * (NorthState.r - Eta);
    float FluxSouth = 0.5 * (DepthSouth * SouthState.b + DepthCentre * FlowVelocity.y) - 0.5 * Celerity * (Eta - SouthState.r);
    float NextEta = Eta - DeltaTime * ((FluxEast - FluxWest) + (FluxNorth - FluxSouth)) / CellSpacing;

    // 2. Pressure gradient, upwind advection, bed friction, wind shear and velocity diffusion.
    float GradientX = (EastState.r - WestState.r) / (2.0 * CellSpacing);
    float GradientZ = (NorthState.r - SouthState.r) / (2.0 * CellSpacing);
    float AdvectionX = FlowVelocity.x > 0.0 ? (FlowVelocity.x - WestState.g) / CellSpacing : (EastState.g - FlowVelocity.x) / CellSpacing;
    float AdvectionZ = FlowVelocity.y > 0.0 ? (FlowVelocity.y - SouthState.b) / CellSpacing : (NorthState.b - FlowVelocity.y) / CellSpacing;
    float CrossX = FlowVelocity.y > 0.0 ? (FlowVelocity.x - SouthState.g) / CellSpacing : (NorthState.g - FlowVelocity.x) / CellSpacing;
    float CrossZ = FlowVelocity.x > 0.0 ? (FlowVelocity.y - WestState.b) / CellSpacing : (EastState.b - FlowVelocity.y) / CellSpacing;
    float DiffusionX = (EastState.g + WestState.g - 2.0 * FlowVelocity.x) / (CellSpacing * CellSpacing);
    float DiffusionZ = (NorthState.b + SouthState.b - 2.0 * FlowVelocity.y) / (CellSpacing * CellSpacing);
    float Viscosity = 0.11 * sqrt(Gravity * MaximumDepth) * CellSpacing;
    float BedFriction = Friction * (1.0 + 12.0 * DryDepth / max(DepthCentre, DryDepth));
    vec2 NextVelocity = FlowVelocity + DeltaTime * (
        -Gravity * vec2(GradientX, GradientZ)
        - vec2(FlowVelocity.x * AdvectionX + FlowVelocity.y * CrossX, FlowVelocity.x * CrossZ + FlowVelocity.y * AdvectionZ)
        - BedFriction * FlowVelocity + WindStrength * normalize(WindDirection)
        + Viscosity * vec2(DiffusionX, DiffusionZ));

    if (DepthCentre <= DryDepth) NextVelocity = vec2(0.0);
    NextEta = clamp(NextEta, -MaximumDepth, MaximumDepth);
    NextVelocity = clamp(NextVelocity, vec2(-18.0), vec2(18.0));

    // 3. Rain / tsunami impulse, injected for exactly one invocation.
    if (Impulse.z != 0.0)
    {
        vec2 Offset = World - Impulse.xy;
        float Weight = exp(-dot(Offset, Offset) / (2.0 * pow(Impulse.w * 0.4, 2.0)));
        NextEta += Impulse.z * Weight * step(DryDepth, DepthCentre);
    }

    // 4. Seed newly exposed rolling-window cells, then couple tiers in both directions.
    if (NewlyExposed)
    {
        vec4 Seed = CoarseEnabled == 1 ? ReadRingAt(CoarseField, World, CoarseSpacing) : EvaluateSwell(World);
        NextEta = mix(NextEta, Seed.r, 0.9);
        NextVelocity = mix(NextVelocity, Seed.gb, 0.9);
    }
    if (CoarseEnabled == 1)
    {
        float EdgeDistance = float(min(min(WindowIndex.x, GridResolution - 1 - WindowIndex.x), min(WindowIndex.y, GridResolution - 1 - WindowIndex.y)));
        if (EdgeDistance < 4.0)
        {
            vec4 CoarseSample = ReadRingAt(CoarseField, World, CoarseSpacing);
            float BlendWeight = clamp((4.0 - EdgeDistance) / 4.0, 0.0, 1.0) * 0.32;
            NextEta = mix(NextEta, CoarseSample.r, BlendWeight);
            NextVelocity = mix(NextVelocity, CoarseSample.gb, BlendWeight);
        }
    }
    if (FineEnabled == 1)
    {
        ivec2 FineCell = ivec2(floor(World / FineSpacing));
        if (all(greaterThanEqual(FineCell, FineOriginCells)) && all(lessThan(FineCell, FineOriginCells + ivec2(GridResolution))))
        {
            vec4 FineSample = ReadRingAt(FineField, World, FineSpacing);
            NextEta = mix(NextEta, FineSample.r, 0.55);
            NextVelocity = mix(NextVelocity, FineSample.gb, 0.55);
        }
    }

    NextState = vec4(NextEta, NextVelocity, DepthCentre);
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                                      EDITOR OVERLAY
//------------------------------------------------------------------------------------------------------------------------

export const OverlayVertexShader = `#version 300 es
precision highp float;
precision highp sampler2D;
layout(location = 0) in vec3 WorldPosition;
${CameraProjection}
void main()
{
    gl_Position = ProjectCameraRelative(WorldPosition - CameraAbsolute);
}
`;

export const OverlayFragmentShader = `#version 300 es
precision highp float;
precision highp sampler2D;
out vec4 Colour;
uniform vec4 OverlayColour;
void main()
{
    Colour = OverlayColour;
}
`;

//------------------------------------------------------------------------------------------------------------------------
//                                               CONVERTED POLYGON SURFACE
//------------------------------------------------------------------------------------------------------------------------

export const SurfaceVertexShader = `#version 300 es
precision highp float;
precision highp sampler2D;
layout(location = 0) in vec3 VertexPosition;
layout(location = 1) in vec3 VertexNormal;
${CameraProjection}
out vec3 RelPosition;
out vec3 SurfaceNormal;
void main()
{
    vec3 Rel = VertexPosition - CameraAbsolute;
    RelPosition = Rel;
    SurfaceNormal = VertexNormal;
    gl_Position = ProjectCameraRelative(Rel);
}
`;

export const SurfaceFragmentShader = `#version 300 es
precision highp float;
precision highp sampler2D;
in vec3 RelPosition;
in vec3 SurfaceNormal;
out vec4 Colour;
${CameraProjection}
${Atmosphere}
void main()
{
    vec3 Normal = normalize(SurfaceNormal);
    vec3 View = normalize(-RelPosition);
    float Lambert = max(0.0, dot(Normal, SunDirection));
    vec3 BaseColour = vec3(0.44, 0.58, 0.52) * (0.32 + 0.78 * Lambert)
        + vec3(0.8, 0.95, 1.0) * pow(max(0.0, dot(reflect(-SunDirection, Normal), View)), 32.0) * 0.15;
    float Fog = 1.0 - exp(-length(RelPosition) * HazeDensity);
    Colour = vec4(mix(BaseColour, EvaluateSky(-View), Fog), 1.0);
}
`;
