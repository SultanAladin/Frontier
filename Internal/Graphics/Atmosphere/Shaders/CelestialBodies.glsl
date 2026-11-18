// 🧩 GPU parity celestial evaluation for SkyDome.frag: Planckian Fraunhofer Airy + Moffat seeing starfield (catalog bright stars +
//    Galactic band, zero cross decals) and 3D spherical Rayleigh+Mie multi-moon radiative transfer + Lommel-Seeliger regolith
//    (zero pow(CosM, 420) sticker halo). All coordinates in atmosphere-local Y-up space.

#ifndef FRONTIER_CELESTIAL_BODIES_GLSL
#define FRONTIER_CELESTIAL_BODIES_GLSL

uint HashCelestial3(ivec3 Cell, uint Salt)
{
    uint Word = uint(Cell.x) * 73856093u ^ uint(Cell.y) * 19349663u ^ uint(Cell.z) * 83492791u ^ (Salt * 1664525u + 1013904223u);
    Word = (Word ^ (Word >> 15u)) * 2246822519u;
    Word = (Word ^ (Word >> 13u)) * 3266489917u;
    return Word ^ (Word >> 16u);
}

float SmoothCelestialNoise3(vec3 Coord, uint Salt)
{
    ivec3 Base = ivec3(floor(Coord));
    vec3  Frac = Coord - vec3(Base);
    vec3  U    = Frac * Frac * (3.0 - 2.0 * Frac);

    float C000 = float(HashCelestial3(Base + ivec3(0, 0, 0), Salt) & 0xFFFFu) / 65535.0;
    float C100 = float(HashCelestial3(Base + ivec3(1, 0, 0), Salt) & 0xFFFFu) / 65535.0;
    float C010 = float(HashCelestial3(Base + ivec3(0, 1, 0), Salt) & 0xFFFFu) / 65535.0;
    float C110 = float(HashCelestial3(Base + ivec3(1, 1, 0), Salt) & 0xFFFFu) / 65535.0;
    float C001 = float(HashCelestial3(Base + ivec3(0, 0, 1), Salt) & 0xFFFFu) / 65535.0;
    float C101 = float(HashCelestial3(Base + ivec3(1, 0, 1), Salt) & 0xFFFFu) / 65535.0;
    float C011 = float(HashCelestial3(Base + ivec3(0, 1, 1), Salt) & 0xFFFFu) / 65535.0;
    float C111 = float(HashCelestial3(Base + ivec3(1, 1, 1), Salt) & 0xFFFFu) / 65535.0;

    float S0 = mix(mix(C000, C100, U.x), mix(C010, C110, U.x), U.y);
    float S1 = mix(mix(C001, C101, U.x), mix(C011, C111, U.x), U.y);
    return mix(S0, S1, U.z);
}

// Ballesteros (2012) B-V -> Teff -> Planckian linear Rec.709 RGB.
vec3 EvaluatePlanckianStarColor(float ColorIndexBV)
{
    float BV   = clamp(ColorIndexBV, -0.40, 2.10);
    float Teff = 4600.0 * (1.0 / (0.92 * BV + 1.70) + 1.0 / (0.92 * BV + 0.62));
    vec3  Spec = vec3(
        1.0 / (exp(clamp(22135.0 / Teff, 0.05, 50.0)) - 1.0),
        1.0 / (exp(clamp(26160.0 / Teff, 0.05, 50.0)) - 1.0),
        1.0 / (exp(clamp(32860.0 / Teff, 0.05, 50.0)) - 1.0));
    float Norm = max(1.0e-5, 0.28 * Spec.r + 0.56 * Spec.g + 0.16 * Spec.b);
    return Spec / Norm;
}

float EvaluateStellarPointSpread(float AngularSeparationRad, float PixelFootprintRad)
{
    float CoreSigma   = max(PixelFootprintRad * 0.58, 0.00018);
    float RatioSq     = (AngularSeparationRad * AngularSeparationRad) / (CoreSigma * CoreSigma);
    float AiryCore    = exp(-0.5 * RatioSq);
    float MoffatWings = 0.032 / pow(1.0 + RatioSq * 0.22, 2.75);
    float NormArea    = 2.0 * PI * CoreSigma * CoreSigma * 1.12;
    return (AiryCore + MoffatWings) * (PixelFootprintRad * PixelFootprintRad / NormArea);
}

vec3 EvaluateProceduralStarTier(vec3 Dir, float GridScale, float PixelFootprintRad, uint Salt, float MagMin, float MagSpan, float GalLatSin)
{
    vec3  Scaled   = Dir * GridScale;
    ivec3 BaseCell = ivec3(floor(Scaled));
    vec3  Accum    = vec3(0.0);

    for (int DZ = -1; DZ <= 1; ++DZ)
    {
        for (int DY = -1; DY <= 1; ++DY)
        {
            for (int DX = -1; DX <= 1; ++DX)
            {
                ivec3 Cell = BaseCell + ivec3(DX, DY, DZ);
                uint  H0   = HashCelestial3(Cell, Salt);
                float Occ  = 0.24 + 0.44 * exp(-GalLatSin * GalLatSin / 0.09);
                if (float(H0 & 0xFFFu) / 4095.0 > Occ)
                    continue;

                uint  H1      = HashCelestial3(Cell + ivec3(13, -7, 29), Salt + 3u);
                vec3  StarPos = vec3(Cell) + vec3(
                    float(H0 & 0x3FFu) / 1023.0,
                    float((H0 >> 10u) & 0x3FFu) / 1023.0,
                    float((H0 >> 20u) & 0x3FFu) / 1023.0);
                float Len = length(StarPos);
                if (Len < 1.0e-4)
                    continue;
                vec3  StarDir = StarPos / Len;
                float AngSep  = length(Dir - StarDir);
                if (AngSep > PixelFootprintRad * 5.0)
                    continue;

                float UMag = pow(float(H1 & 0xFFFFu) / 65535.0, 1.55);
                float VMag = MagMin + MagSpan * UMag;
                float BV   = -0.25 + 1.85 * pow(float((H1 >> 16u) & 0xFFFFu) / 65535.0, 1.2);
                float Flux = 0.22 * pow(10.0, -0.4 * VMag);
                Accum += EvaluatePlanckianStarColor(BV) * (Flux * EvaluateStellarPointSpread(AngSep, PixelFootprintRad));
            }
        }
    }
    return Accum;
}

vec3 EvaluateExoatmosphericStarfield(vec3 ViewDirLocal, float PixelFootprintRad)
{
    const vec3 GalacticNorth = normalize(vec3(-0.4838, 0.4560, -0.7470));
    float GalSin = dot(ViewDirLocal, GalacticNorth);
    float Band   = exp(-(GalSin * GalSin) / (2.0 * 0.135 * 0.135));
    float Clouds = SmoothCelestialNoise3(ViewDirLocal * 5.5, 41u);
    float Dust   = SmoothCelestialNoise3(ViewDirLocal * 11.0 + vec3(3.1, -1.7, 2.4), 79u);
    float DustEx = smoothstep(0.42, 0.78, Dust) * exp(-(GalSin * GalSin) / 0.015);
    vec3  MilkyWay = vec3(1.06, 0.99, 0.90) * (0.0022 * Band * (0.55 + 0.80 * Clouds) * (1.0 - 0.68 * DustEx));

    vec3 Stars = MilkyWay;
    Stars += EvaluateProceduralStarTier(ViewDirLocal,  22.0, PixelFootprintRad, 101u, 0.20, 3.10, GalSin);
    Stars += EvaluateProceduralStarTier(ViewDirLocal,  54.0, PixelFootprintRad, 211u, 3.10, 1.90, GalSin);
    Stars += EvaluateProceduralStarTier(ViewDirLocal, 118.0, PixelFootprintRad, 307u, 4.90, 1.80, GalSin);
    return Stars;
}

// 3D spherical Rayleigh + Mie lunar atmosphere + Lommel-Seeliger cratered regolith (zero sticker halo).
vec4 EvaluateSingleSphericalMoon(vec3  ViewDirLocal,
                                 vec3  SunDirLocal,
                                 vec3  MoonDirLocal,
                                 float AngularRadiusRad,
                                 float TopScaleRatio,
                                 vec3  BodyTint,
                                 float Albedo,
                                 vec3  RayleighExt,
                                 float MieExt,
                                 uint  SeedSalt)
{
    float SinOuter = clamp(sin(AngularRadiusRad * TopScaleRatio), 0.0002, 0.85);
    if (dot(ViewDirLocal, MoonDirLocal) < cos(asin(SinOuter) * 1.12))
        return vec4(0.0);

    float DistUnits  = 1.0 / max(1.0e-4, sin(AngularRadiusRad));
    vec3  MoonCenter = MoonDirLocal * DistUnits;
    vec2  OuterHit   = IntersectSphereGeneral(-MoonCenter, ViewDirLocal, TopScaleRatio);
    if (OuterHit.y <= 0.0)
        return vec4(0.0);

    vec2  CoreHit   = IntersectSphereGeneral(-MoonCenter, ViewDirLocal, 1.0);
    bool  HitsSolid = (CoreHit.x > 0.0);
    float TStart    = max(0.0, OuterHit.x);
    float TEnd      = HitsSolid ? CoreHit.x : OuterHit.y;

    vec3  AtmoTrans = vec3(1.0);
    vec3  AtmoRad   = vec3(0.0);

    if (TopScaleRatio > 1.002 && TEnd > TStart)
    {
        const int ATMO_STEPS = 14;
        float StepLen   = (TEnd - TStart) / float(ATMO_STEPS);
        float ScaleH    = max(0.004, (TopScaleRatio - 1.0) * 0.22);
        float CosSun    = dot(ViewDirLocal, SunDirLocal);
        float PhaseR    = RayleighPhase(CosSun);
        float PhaseM    = CornetteShanksPhase(CosSun, 0.72);

        for (int Step = 0; Step < ATMO_STEPS; ++Step)
        {
            float T        = TStart + (float(Step) + 0.5) * StepLen;
            vec3  LocalPos = (ViewDirLocal * T) - MoonCenter;
            float Rad      = length(LocalPos);
            float Alt      = max(0.0, Rad - 1.0);
            float DensR    = exp(-Alt / ScaleH);
            float DensM    = exp(-Alt / (ScaleH * 0.45));

            vec3  SigR     = RayleighExt * DensR;
            vec3  SigM     = vec3(MieExt * DensM);
            vec3  SigT     = SigR + SigM * 1.11;
            vec3  StepTr   = exp(-SigT * StepLen);
            vec3  StepInt  = (vec3(1.0) - StepTr) / max(SigT, vec3(1.0e-6));

            // Solar shadow & optical thickness check against solid lunar sphere.
            vec2  ShadowCore = IntersectSphereGeneral(LocalPos, SunDirLocal, 1.0);
            vec3  SunAtten   = vec3(0.0);
            if (ShadowCore.x <= 0.0)
            {
                float MuSun   = dot(LocalPos / Rad, SunDirLocal);
                float Chapman = 1.0 / max(0.08, MuSun + 0.15 * pow(max(0.0, 1.0 + MuSun), 1.4));
                SunAtten      = exp(-(SigR + SigM) * ScaleH * Chapman);
            }

            vec3 Scat = (SigR * PhaseR + SigM * PhaseM) * SunAtten * Atmosphere.SolarIlluminance.rgb;
            AtmoRad   += AtmoTrans * Scat * StepInt;
            AtmoTrans *= StepTr;
        }
    }

    if (!HitsSolid)
    {
        float MeanTr = (AtmoTrans.r + AtmoTrans.g + AtmoTrans.b) / 3.0;
        return vec4(AtmoRad, 1.0 - MeanTr);
    }

    vec3  HitLocal = normalize(ViewDirLocal * CoreHit.x - MoonCenter);
    float Maria    = SmoothCelestialNoise3(HitLocal * 3.2, SeedSalt);
    float Craters  = SmoothCelestialNoise3(HitLocal * 16.0, SeedSalt + 17u);
    vec3  SurfTint = BodyTint * (0.62 + 0.58 * smoothstep(0.32, 0.72, Maria)) * (0.82 + 0.32 * Craters);

    float Mu0      = max(0.0, dot(HitLocal, SunDirLocal));
    float MuV      = max(0.04, dot(HitLocal, -ViewDirLocal));
    vec3  SurfaceL = vec3(0.0);
    if (Mu0 > 0.0)
    {
        float Lommel = Mu0 / (Mu0 + MuV);
        float Surge  = 1.0 + 0.35 * exp(-acos(clamp(dot(-ViewDirLocal, SunDirLocal), -1.0, 1.0)) / 0.14);
        float ScaleH = max(0.004, (TopScaleRatio - 1.0) * 0.22);
        vec3  SunTr  = (TopScaleRatio > 1.002)
                     ? exp(-(RayleighExt + vec3(MieExt)) * ScaleH / max(0.08, Mu0 + 0.12))
                     : vec3(1.0);
        SurfaceL = Atmosphere.SolarIlluminance.rgb * SunTr * SurfTint * (Albedo * (2.0 / PI) * Lommel * Surge);
    }
    // Subtle planetary shine on night side.
    SurfaceL += SurfTint * Albedo * vec3(0.0014, 0.0020, 0.0032) * max(0.15, 0.5 - 0.5 * dot(HitLocal, SunDirLocal));

    return vec4(AtmoRad + AtmoTrans * SurfaceL, 1.0);
}

#endif
