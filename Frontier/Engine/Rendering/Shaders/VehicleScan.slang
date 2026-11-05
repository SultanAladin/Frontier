/*==========================================================================================================================================
                                                           VEHICLESCAN.SLANG
                                     ------------------------------------------------------------------
                 Native scan shader scaffold. Reference equations and particle attributes follow Truck.tsx
                 and Drei; Ghost and Wireframe modes are additional behavior. This shader is not yet
                 connected to the native renderer, and its scan plane remains vehicle-local.
==========================================================================================================================================*/

struct ScanUniforms
{
    float4x4 ViewProjection;
    float4   CameraWorldAndTime;   // xyz = camera world position, w = elapsed seconds
    float4   HeroOriginAndYaw;     // xyz = hero world origin, w = yaw radians
    float4   ScanParameters;       // x = ScanX, y = HoloAmount, z = CurtainVis, w = ModeCode (1=Ghost, 2=Wireframe, 3=Scan)
    float4   BoundsAndSparkle;     // x = FrontX, y = RearX, z = SparkleOpacity, w = BodyZRebase
    float4x4 View;
    float4   Viewport;             // x = pixel ratio, yz = inverse physical screen extent
};

struct SparkleSeedRecord
{
    float4 PositionAndSize;        // xyz = vehicle-local offset, w = point size
    float4 NoiseAndSpeed;          // xyz = noise, w = speed
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
    const float Mode = ScanFrame.ScanParameters.w;
    if (Mode > 1.5 && Mode < 2.5) discard; // extra Wireframe-only mode
    const float ScanD = Input.LocalPos.x - ScanFrame.ScanParameters.x;
    const float Region = Mode < 1.5 ? 1.0 : smoothstep(-0.9, -0.05, ScanD);
    const float Visibility = Region * ScanFrame.ScanParameters.y;
    if (Visibility < 0.003) discard;

    const float3 ViewDir = normalize(ScanFrame.CameraWorldAndTime.xyz - Input.WorldPos);
    const float Fresnel = pow(saturate(1.0 - abs(dot(normalize(Input.WorldNormal), ViewDir))), 2.3);
    // Engine floor is Z=0; retained scene's Y-up floor is Y=-0.63.
    const float Lines = smoothstep(0.72, 1.0, sin((Input.WorldPos.z - 0.63) * 85.0) * 0.5 + 0.5);
    const float Edge = exp(-pow((ScanD + 0.18) * 4.2, 2.0));
    const float Alpha = (0.028 + Fresnel * 0.50 + Lines * 0.040 + Edge * 0.30) * Visibility;
    const float3 BaseTint = float3(0.396755, 0.715694, 0.838799); // #a9dcec, linear
    const float3 EdgeTint = float3(0.039546, 0.508881, 0.938686); // #38bdf8, linear
    return float4(BaseTint + EdgeTint * Edge * 1.15, saturate(Alpha)); // additive, src-alpha/one
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

    const float BaseAlpha = (Mode > 1.5 && Mode < 2.5) ? 0.82 : 0.55;
    const float Alpha = saturate(BaseAlpha * HoloAmt);
    return float4(float3(0.274677, 0.686685, 0.887923), Alpha); // additive, src-alpha/one
}

//------------------------------------------------------------------------------------------------------------------------
//                                              3. DUAL WAVEFRONT CURTAIN PASS
//------------------------------------------------------------------------------------------------------------------------

struct CurtainVaryings
{
    float4 ClipPosition : SV_Position;
    float2 Uv           : TEXCOORD0;
    nointerpolation uint SheetIndex : TEXCOORD1;
};

[shader("vertex")]
CurtainVaryings CurtainVertex(uint VertexId : SV_VertexID, uint InstanceId : SV_InstanceID)
{
    static const float2 QuadCorners[6] = {
        float2(-1.0, -1.0), float2( 1.0, -1.0), float2( 1.0,  1.0),
        float2(-1.0, -1.0), float2( 1.0,  1.0), float2(-1.0,  1.0)
    };
    const float2 Corner = QuadCorners[VertexId % 6];
    const float HalfSpanY = (InstanceId == 0) ? 1.20 : 0.275;
    // Both untextured planes span 2.5 m vertically and sit at the same scan X.
    const float3 LocalPos = float3(ScanFrame.ScanParameters.x, Corner.x * HalfSpanY, 0.66 + Corner.y * 1.25);

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
    const float PeakOpacity = (Input.SheetIndex == 0) ? 0.075 : 0.30;
    const float Alpha = ScanFrame.ScanParameters.z * PeakOpacity;
    if (Alpha <= 0.001) discard;
    return float4(float3(0.039546, 0.508881, 0.938686), saturate(Alpha)); // constant, additive
}

//------------------------------------------------------------------------------------------------------------------------
//                                              4. 130 HOLOGRAPHIC SPARKLES PASS
//------------------------------------------------------------------------------------------------------------------------

struct SparkleVaryings
{
    float4 ClipPosition : SV_Position;
    float2 QuadCoord    : TEXCOORD0;
};

[shader("vertex")]
SparkleVaryings SparkleVertex(uint VertexId : SV_VertexID, uint InstanceId : SV_InstanceID)
{
    static const float2 Quad[6] = {
        float2(-1.0, -1.0), float2( 1.0, -1.0), float2( 1.0,  1.0),
        float2(-1.0, -1.0), float2( 1.0,  1.0), float2(-1.0,  1.0)
    };
    const SparkleSeedRecord Seed = SparkleCloud[InstanceId];
    const float3 BaseWorld = TransformVehicleToWorld(Seed.PositionAndSize.xyz);
    const float Time = ScanFrame.CameraWorldAndTime.w * Seed.NoiseAndSpeed.w;
    const float X = BaseWorld.x;
    const float3 World = BaseWorld + float3(cos(Time + X * Seed.NoiseAndSpeed.z * 100.0) * 0.2,
                                            -cos(Time + X * Seed.NoiseAndSpeed.y * 100.0) * 0.2,
                                            sin(Time + X * Seed.NoiseAndSpeed.x * 100.0) * 0.2);
    const float ViewDepth = -mul(ScanFrame.View, float4(World, 1.0)).z;
    float4 Clip = mul(ScanFrame.ViewProjection, float4(World, 1.0));
    SparkleVaryings Out;
    Out.QuadCoord = Quad[VertexId % 6];
    if (ViewDepth <= 0.0 || Clip.w <= 0.0)
    {
        Out.ClipPosition = float4(0.0, 0.0, -2.0, 1.0);
        return Out;
    }
    const float Diameter = max(1.0, Seed.PositionAndSize.w * 25.0 * ScanFrame.Viewport.x / max(ViewDepth, 1e-6));
    Clip.xy += Out.QuadCoord * Diameter * ScanFrame.Viewport.yz * Clip.w;
    Out.ClipPosition = Clip;
    return Out;
}

[shader("fragment")]
float4 SparkleFragment(SparkleVaryings Input) : SV_Target0
{
    const float Distance = length(Input.QuadCoord) * 0.5;
    const float Alpha = saturate((0.05 / max(Distance, 1e-6) - 0.1) * ScanFrame.BoundsAndSparkle.z);
    if (Alpha <= 0.0) discard;
    return float4(float3(0.274677, 0.686685, 0.887923), Alpha); // normal src-alpha/one-minus-src-alpha
}
