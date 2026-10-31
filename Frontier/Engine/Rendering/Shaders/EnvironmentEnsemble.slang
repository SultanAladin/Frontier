//==========================================================================================================================================
//                                                ENVIRONMENT ENSEMBLE SLANG COMMON
//==========================================================================================================================================
// Multi-realm float4 record decoders, spherical planet geometry, portal ray routing, and procedural 3D Perlin-Worley noise.

static const float kPi      = 3.14159265358979323846f;
static const float kTwoPi   = 6.28318530717958647692f;
static const float kInv4Pi  = 0.07957747154594766788f;

static const uint kRealmStride       = 16u;
static const uint kSunStride         = 6u;
static const uint kMoonStride        = 4u;
static const uint kPlanetStride      = 6u;
static const uint kCloudStride       = 5u;
static const uint kFogBoxStride      = 3u;
static const uint kLuminaireStride   = 3u;
static const uint kLightningStride   = 3u;
static const uint kFacetStride       = 3u;
static const uint kSphereStride      = 2u;
static const uint kGlassStride       = 3u;
static const uint kPortalStride      = 9u;
static const uint kFlareSourceStride = 4u;
static const uint kCustomStarStride  = 2u;

struct PushDispatchHeader
{
    uint4  Counts; // x = Width, y = Height, z = TargetIndex (RealmId or FrameParity), w = Flags
    float4 Tuning; // x = Param0, y = Param1, z = Param2, w = Param3
};

float3 SafeNormalize(float3 V)
{
    float L2 = dot(V, V);
    return (L2 > 1.0e-20f) ? (V * rsqrt(L2)) : float3(0.0f, 1.0f, 0.0f);
}

bool RaySphereBoth(float3 Origin, float3 Dir, float Radius, out float OutNear, out float OutFar)
{
    float B   = dot(Origin, Dir);
    float C   = dot(Origin, Origin) - Radius * Radius;
    float Det = B * B - C;
    if (Det < 0.0f)
    {
        OutNear = 0.0f;
        OutFar  = 0.0f;
        return false;
    }
    float S = sqrt(Det);
    OutNear = -B - S;
    OutFar  = -B + S;
    return true;
}

float DistanceToSphereExit(float3 Origin, float3 Dir, float Radius)
{
    float T0, T1;
    if (!RaySphereBoth(Origin, Dir, Radius, T0, T1) || T1 <= 0.0f)
    {
        return 0.0f;
    }
    return T1;
}

bool HitsPlanetGround(float3 Origin, float3 Dir, float BottomRadiusKm, out float OutGroundT)
{
    float T0, T1;
    OutGroundT = 0.0f;
    if (!RaySphereBoth(Origin, Dir, BottomRadiusKm, T0, T1))
    {
        return false;
    }
    if (T0 > 1.0e-4f)
    {
        OutGroundT = T0;
        return true;
    }
    return false;
}

float RayleighPhaseVal(float CosTheta)
{
    return (3.0f / (16.0f * kPi)) * (1.0f + CosTheta * CosTheta);
}

float CornetteShanksPhaseVal(float CosTheta, float G)
{
    float G2    = G * G;
    float Numer = 3.0f * (1.0f - G2) * (1.0f + CosTheta * CosTheta);
    float Denom = (8.0f * kPi) * (2.0f + G2) * pow(max(1.0f + G2 - 2.0f * G * CosTheta, 1.0e-4f), 1.5f);
    return Numer / Denom;
}

float HenyeyGreensteinVal(float CosTheta, float G)
{
    float G2    = G * G;
    float Denom = max(1.0f + G2 - 2.0f * G * CosTheta, 1.0e-4f);
    return (1.0f - G2) / (4.0f * kPi * Denom * sqrt(Denom));
}

//------------------------------------------------------------------------------------------------------------------------
//                                      PROCEDURAL 3D HASH, VALUE & WORLEY NOISE
//------------------------------------------------------------------------------------------------------------------------

uint HashPcg(uint InputVal)
{
    uint State = InputVal * 747796405u + 2891336453u;
    uint Word  = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
    return (Word >> 22u) ^ Word;
}

uint HashGrid3D(int3 Cell, uint Seed)
{
    uint H = Seed;
    H = HashPcg(H ^ uint(Cell.x * 73856093));
    H = HashPcg(H ^ uint(Cell.y * 19349663));
    H = HashPcg(H ^ uint(Cell.z * 83492791));
    return H;
}

float ValueNoise3D(float3 P, uint Seed)
{
    float3 Fl = floor(P);
    int3   I0 = int3(Fl);
    float3 Fr = P - Fl;
    float3 U  = Fr * Fr * (3.0f - 2.0f * Fr);

    float N000 = float(HashGrid3D(I0 + int3(0, 0, 0), Seed) & 0xFFFFFFu) / float(0x1000000u);
    float N100 = float(HashGrid3D(I0 + int3(1, 0, 0), Seed) & 0xFFFFFFu) / float(0x1000000u);
    float N010 = float(HashGrid3D(I0 + int3(0, 1, 0), Seed) & 0xFFFFFFu) / float(0x1000000u);
    float N110 = float(HashGrid3D(I0 + int3(1, 1, 0), Seed) & 0xFFFFFFu) / float(0x1000000u);
    float N001 = float(HashGrid3D(I0 + int3(0, 0, 1), Seed) & 0xFFFFFFu) / float(0x1000000u);
    float N101 = float(HashGrid3D(I0 + int3(1, 0, 1), Seed) & 0xFFFFFFu) / float(0x1000000u);
    float N011 = float(HashGrid3D(I0 + int3(0, 1, 1), Seed) & 0xFFFFFFu) / float(0x1000000u);
    float N111 = float(HashGrid3D(I0 + int3(1, 1, 1), Seed) & 0xFFFFFFu) / float(0x1000000u);

    float Nx00 = lerp(N000, N100, U.x);
    float Nx10 = lerp(N010, N110, U.x);
    float Nx01 = lerp(N001, N101, U.x);
    float Nx11 = lerp(N011, N111, U.x);
    return lerp(lerp(Nx00, Nx10, U.y), lerp(Nx01, Nx11, U.y), U.z);
}

float FbmValue3D(float3 P, int Octaves, uint Seed)
{
    float Sum  = 0.0f;
    float Amp  = 0.5f;
    float Norm = 0.0f;
    float3 Q   = P;
    for (int I = 0; I < Octaves; ++I)
    {
        Sum  += Amp * ValueNoise3D(Q, Seed + uint(I) * 97u);
        Norm += Amp;
        Q    *= 2.03f;
        Amp  *= 0.5f;
    }
    return (Norm > 0.0f) ? (Sum / Norm) : 0.0f;
}

float WorleyInverted3D(float3 P, uint Seed)
{
    int3  Cell  = int3(floor(P));
    float BestD = 4.0f;
    for (int Dz = -1; Dz <= 1; ++Dz)
    {
        for (int Dy = -1; Dy <= 1; ++Dy)
        {
            for (int Dx = -1; Dx <= 1; ++Dx)
            {
                int3 C  = Cell + int3(Dx, Dy, Dz);
                uint H0 = HashGrid3D(C, Seed);
                uint H1 = HashPcg(H0);
                uint H2 = HashPcg(H1);
                float3 Feat = float3(C) + float3(
                    float(H0 & 0xFFFFu) / 65535.0f,
                    float(H1 & 0xFFFFu) / 65535.0f,
                    float(H2 & 0xFFFFu) / 65535.0f);
                float3 Diff = Feat - P;
                BestD = min(BestD, dot(Diff, Diff));
            }
        }
    }
    return saturate(1.0f - sqrt(BestD));
}

//------------------------------------------------------------------------------------------------------------------------
//                                         ENSEMBLE RECORD DECODING HELPERS
//------------------------------------------------------------------------------------------------------------------------

uint RealmSlotBase(StructuredBuffer<float4> Ensemble, uint RealmId)
{
    uint TableOffset = uint(Ensemble[5].x + 0.5f);
    return TableOffset + RealmId * kRealmStride;
}

uint SunSlotBase(StructuredBuffer<float4> Ensemble, uint GlobalSunIdx)
{
    uint TableOffset = uint(Ensemble[5].y + 0.5f);
    return TableOffset + GlobalSunIdx * kSunStride;
}

float3 CameraRayDirection(StructuredBuffer<float4> Ensemble, float2 ScreenUv)
{
    float  TanHalfY = Ensemble[1].w;
    float3 Right    = Ensemble[2].xyz;
    float  Aspect   = Ensemble[2].w;
    float3 Up       = Ensemble[3].xyz;
    float3 Forward  = Ensemble[4].xyz;

    float NdcX = (ScreenUv.x * 2.0f - 1.0f) * Aspect * TanHalfY;
    float NdcY = (1.0f - ScreenUv.y * 2.0f) * TanHalfY;
    return SafeNormalize(Forward + Right * NdcX + Up * NdcY);
}
