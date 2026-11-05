/*==========================================================================================================================================
                                                           VEHICLESCAN.SLANG
                                     ------------------------------------------------------------------
                 1:1 X-Ray Scan & Ghost holographic overlay shaders for the Vehicle HMI Configurator:
                   - GhostVertex / GhostFragment: double-sided additive Fresnel + scanlines + wavefront glow
                   - WireframeVertex / WireframeFragment: 14° body / 28° wheel dihedral crease lines (#8fd8f2)
                   - CurtainVertex / CurtainFragment: dual wavefront scan sheets (wide glow + core sheet)
                   - SparkleVertex / SparkleFragment: 130 billboarded #8fd8f2 floating holographic sparkles
==========================================================================================================================================*/

struct ScanUniforms
{
    float4x4 ViewProjection;
    float4   CameraWorldAndTime;   // xyz = camera world position, w = elapsed seconds
    float4   HeroOriginAndYaw;     // xyz = hero world origin, w = yaw radians
    float4   ScanParameters;       // x = ScanX, y = HoloAmount, z = CurtainVis, w = ModeCode (1=Ghost, 2=Wireframe, 3=Scan)
    float4   BoundsAndSparkle;     // x = FrontX, y = RearX, z = SparkleOpacity, w = BodyZRebase
};

struct SparkleSeedRecord
{
    float4 OffsetAndPhase;         // xyz = vehicle-local offset, w = phase
    float4 SpeedAndScale;          // x = speed, y = scale, zw = reserved
};

[[vk::binding(0, 0)]] ConstantBuffer<ScanUniforms> ScanFrame;
[[vk::binding(1, 0)]] StructuredBuffer<SparkleSeedRecord> SparkleCloud;

float3 TransformVehicleToWorld(float3 LocalPos)
{
    float Yaw = ScanFrame.HeroOriginAndYaw.w;
    float CosY = cos(Yaw);
    float SinY = sin(Yaw);
    return float3(
        ScanFrame.HeroOriginAndYaw.x + LocalPos.x * CosY - LocalPos.y * SinY,
        ScanFrame.HeroOriginAndYaw.y + LocalPos.x * SinY + LocalPos.y * CosY,
        ScanFrame.HeroOriginAndYaw.z + LocalPos.z);
}

float3 RotateVehicleNormalToWorld(float3 LocalNormal)
{
    float Yaw = ScanFrame.HeroOriginAndYaw.w;
    float CosY = cos(Yaw);
    float SinY = sin(Yaw);
    return normalize(float3(
        LocalNormal.x * CosY - LocalNormal.y * SinY,
        LocalNormal.x * SinY + LocalNormal.y * CosY,
        LocalNormal.z));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    1. GHOST SHELL PASS
//------------------------------------------------------------------------------------------------------------------------

struct GhostVertexInput
{
    [[vk::location(0)]] float3 Position : POSITION;
    [[vk::location(1)]] float3 Normal   : NORMAL;
};

struct GhostVaryings
{
    float4 ClipPosition : SV_Position;
    float3 WorldPos     : TEXCOORD0;
    float3 WorldNormal  : TEXCOORD1;
    float3 LocalPos     : TEXCOORD2;
};

[shader("vertex")]
GhostVaryings GhostVertex(GhostVertexInput Input)
{
    GhostVaryings Out;
    float3 Rebasing = float3(Input.Position.x, Input.Position.y, Input.Position.z - ScanFrame.BoundsAndSparkle.w);
    float3 WorldPos = TransformVehicleToWorld(Rebasing);
    Out.ClipPosition = mul(ScanFrame.ViewProjection, float4(WorldPos, 1.0));
    Out.WorldPos = WorldPos;
    Out.WorldNormal = RotateVehicleNormalToWorld(Input.Normal);
    Out.LocalPos = Rebasing;
    return Out;
}

[shader("fragment")]
float4 GhostFragment(GhostVaryings Input) : SV_Target0
{
    float Mode = ScanFrame.ScanParameters.w;
    float HoloAmt = ScanFrame.ScanParameters.y;
    float ScanX = ScanFrame.ScanParameters.x;
    float Time = ScanFrame.CameraWorldAndTime.w;

    float Reveal = (Mode < 1.5) ? 1.0 : smoothstep(-0.35, 0.28, Input.LocalPos.x - ScanX);
    if (Reveal * HoloAmt <= 1e-4)
        discard;

    float3 ViewDir = normalize(ScanFrame.CameraWorldAndTime.xyz - Input.WorldPos);
    float AbsNdotV = saturate(abs(dot(normalize(Input.WorldNormal), ViewDir)));
    float Fresnel = pow(1.0 - AbsNdotV, 2.15);
    float Scanlines = 0.88 + 0.12 * sin(Input.LocalPos.z * 88.0 - Time * 3.2);
    float EdgeGlow = (Mode > 2.5) ? exp(-pow((Input.LocalPos.x - ScanX) / 0.19, 2.0)) : 0.0;

    float3 BaseTint = float3(0.392, 0.716, 0.840); // #a9dcec linearized
    float3 EdgeTint = float3(0.039, 0.509, 0.939); // #38bdf8 linearized
    float BaseWeight = (0.055 + 0.42 * Fresnel) * Scanlines;
    float3 Additive = (BaseTint * BaseWeight + EdgeTint * (EdgeGlow * 1.15)) * (HoloAmt * Reveal);
    return float4(Additive, saturate(HoloAmt * Reveal));
}

//------------------------------------------------------------------------------------------------------------------------
//                                             2. DIHEDRAL WIREFRAME PASS (14° / 28°)
//------------------------------------------------------------------------------------------------------------------------

struct WireframeVertexInput
{
    [[vk::location(0)]] float3 Position : POSITION;
};

struct WireframeVaryings
{
    float4 ClipPosition : SV_Position;
    float3 LocalPos     : TEXCOORD0;
};

[shader("vertex")]
WireframeVaryings WireframeVertex(WireframeVertexInput Input)
{
    WireframeVaryings Out;
    float3 Rebasing = float3(Input.Position.x, Input.Position.y, Input.Position.z - ScanFrame.BoundsAndSparkle.w);
    float3 WorldPos = TransformVehicleToWorld(Rebasing);
    Out.ClipPosition = mul(ScanFrame.ViewProjection, float4(WorldPos, 1.0));
    Out.LocalPos = Rebasing;
    return Out;
}

[shader("fragment")]
float4 WireframeFragment(WireframeVaryings Input) : SV_Target0
{
    float Mode = ScanFrame.ScanParameters.w;
    float HoloAmt = ScanFrame.ScanParameters.y;
    float ScanX = ScanFrame.ScanParameters.x;

    if (Mode > 2.5 && Input.LocalPos.x < ScanX + 0.05)
        discard;

    float Wave = (Mode > 2.5) ? exp(-pow((Input.LocalPos.x - ScanX) / 0.28, 2.0)) : 0.0;
    float BaseAlpha = (Mode > 1.5 && Mode < 2.5) ? 0.62 : ((Mode < 1.5) ? 0.24 : (0.36 + 0.48 * Wave));
    float Alpha = saturate(BaseAlpha * HoloAmt);
    float3 WireColour = float3(0.279, 0.687, 0.888); // #8fd8f2 linearized
    return float4(WireColour * Alpha, Alpha);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              3. DUAL WAVEFRONT CURTAIN PASS
//------------------------------------------------------------------------------------------------------------------------

struct CurtainVaryings
{
    float4 ClipPosition : SV_Position;
    float2 Uv           : TEXCOORD0;
    uint   SheetIndex   : TEXCOORD1;
};

[shader("vertex")]
CurtainVaryings CurtainVertex(uint VertexId : SV_VertexID, uint InstanceId : SV_InstanceID)
{
    static const float2 QuadCorners[6] = {
        float2(-1.0, -1.0), float2( 1.0, -1.0), float2( 1.0,  1.0),
        float2(-1.0, -1.0), float2( 1.0,  1.0), float2(-1.0,  1.0)
    };
    float2 Corner = QuadCorners[VertexId % 6];
    float HalfSpanY = (InstanceId == 0) ? 1.20 : 1.20;
    float HalfHeightZ = (InstanceId == 0) ? 1.25 : 0.275;
    // Instance 0 = wide 2.4x2.5 m soft glow; Instance 1 = tight 0.55x2.5 m core sheet along Y/Z at ScanX.
    float XJitter = (InstanceId == 0) ? (Corner.x * 0.12) : (Corner.x * 0.015);
    float3 LocalPos = float3(
        ScanFrame.ScanParameters.x + XJitter,
        Corner.x * HalfSpanY,
        0.66 + Corner.y * HalfHeightZ);

    CurtainVaryings Out;
    float3 WorldPos = TransformVehicleToWorld(LocalPos);
    Out.ClipPosition = mul(ScanFrame.ViewProjection, float4(WorldPos, 1.0));
    Out.Uv = Corner * 0.5 + 0.5;
    Out.SheetIndex = InstanceId;
    return Out;
}

[shader("fragment")]
float4 CurtainFragment(CurtainVaryings Input) : SV_Target0
{
    float Vis = ScanFrame.ScanParameters.z;
    if (Vis <= 1e-4)
        discard;

    float2 CenterDist = abs(Input.Uv - 0.5) * 2.0;
    float RadialFalloff = saturate(1.0 - dot(CenterDist, CenterDist));
    float PeakOpacity = (Input.SheetIndex == 0) ? 0.075 : 0.30;
    float Alpha = Vis * PeakOpacity * RadialFalloff * RadialFalloff;
    float3 Tint = (Input.SheetIndex == 0) ? float3(0.039, 0.509, 0.939) : float3(0.480, 0.840, 0.980);
    return float4(Tint * Alpha, Alpha);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              4. 130 HOLOGRAPHIC SPARKLES PASS
//------------------------------------------------------------------------------------------------------------------------

struct SparkleVaryings
{
    float4 ClipPosition : SV_Position;
    float2 QuadCoord    : TEXCOORD0;
    float  Twinkle      : TEXCOORD1;
};

[shader("vertex")]
SparkleVaryings SparkleVertex(uint VertexId : SV_VertexID, uint InstanceId : SV_InstanceID)
{
    static const float2 Quad[6] = {
        float2(-1.0, -1.0), float2( 1.0, -1.0), float2( 1.0,  1.0),
        float2(-1.0, -1.0), float2( 1.0,  1.0), float2(-1.0,  1.0)
    };
    SparkleSeedRecord Seed = SparkleCloud[InstanceId];
    float Time = ScanFrame.CameraWorldAndTime.w * Seed.SpeedAndScale.x + Seed.OffsetAndPhase.w;
    float3 LocalCenter = Seed.OffsetAndPhase.xyz + float3(
        sin(Time * 1.7) * 0.08,
        cos(Time * 1.3 + 1.1) * 0.08,
        sin(Time * 2.1 + 2.4) * 0.07);
    float3 WorldCenter = TransformVehicleToWorld(LocalCenter);

    float3 ToCam = normalize(ScanFrame.CameraWorldAndTime.xyz - WorldCenter);
    float3 Right = normalize(cross(float3(0.0, 0.0, 1.0), ToCam));
    float3 Up = cross(ToCam, Right);
    float2 Offset = Quad[VertexId % 6];
    float Radius = 0.018 * Seed.SpeedAndScale.y;
    float3 WorldCorner = WorldCenter + (Right * Offset.x + Up * Offset.y) * Radius;

    SparkleVaryings Out;
    Out.ClipPosition = mul(ScanFrame.ViewProjection, float4(WorldCorner, 1.0));
    Out.QuadCoord = Offset;
    Out.Twinkle = 0.45 + 0.55 * (0.5 + 0.5 * sin(Time * 5.2));
    return Out;
}

[shader("fragment")]
float4 SparkleFragment(SparkleVaryings Input) : SV_Target0
{
    float Opacity = ScanFrame.BoundsAndSparkle.z;
    float R2 = dot(Input.QuadCoord, Input.QuadCoord);
    if (R2 > 1.0 || Opacity <= 1e-4)
        discard;
    float Glow = exp(-R2 * 3.4) * Input.Twinkle * Opacity;
    float3 SparkleColour = float3(0.279, 0.687, 0.888); // #8fd8f2 linearized
    return float4(SparkleColour * Glow, Glow);
}
