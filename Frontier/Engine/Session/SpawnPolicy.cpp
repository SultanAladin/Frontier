/*==========================================================================================================================================
                                                             SPAWNPOLICY.CPP
==========================================================================================================================================*/
// 🧩 Vehicle spawn rules: the footprint survey that stands an Agent on any surface, the parked brake state a session hands over with, and
//    the blend between those spawn rules and the driver's own pedal demand.

#include "Frontier/Engine/Session/SpawnPolicy.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

constexpr float Pi = 3.14159265358979323846f;

// 📝 The footprint probes: four tyre patches, the centre of mass, and the two axle midpoints.
constexpr std::array<std::array<float, 2>, 7> FootprintProbes = {
    std::array<float, 2>{ 1.0f, 1.0f },  std::array<float, 2>{ 1.0f, -1.0f }, std::array<float, 2>{ -1.0f, 1.0f },
    std::array<float, 2>{ -1.0f, -1.0f }, std::array<float, 2>{ 0.0f, 0.0f }, std::array<float, 2>{ 1.0f, 0.0f },
    std::array<float, 2>{ -1.0f, 0.0f }
};

float SmoothStep(float Edge0, float Edge1, float Value) noexcept
{
    if (Edge1 <= Edge0) return Value >= Edge1 ? 1.0f : 0.0f;
    const float Blend = std::clamp((Value - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
    return Blend * Blend * (3.0f - 2.0f * Blend);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     POLICY PRESETS
//------------------------------------------------------------------------------------------------------------------------

SpawnPolicy SpawnPolicy::StudioParked()
{
    SpawnPolicy Studio{};
    Studio.SupportRule            = SpawnSupportRule::LevelGround;
    Studio.BrakesEnabledAtSpawn   = true;
    Studio.HandbrakeEngagedAtSpawn = true;
    Studio.BrakeHoldSeconds       = 0.0f;   // 📝 the studio pad keeps the car parked for as long as it is in the bay
    Studio.ParkedPedalShare       = 0.0f;   // 📝 no foot brake: the wheels stay free so the turntable turn is honest
    Studio.ReleasingSeconds       = 0.0f;
    return Studio;
}

SpawnPolicy SpawnPolicy::GridRelease()
{
    SpawnPolicy Grid{};
    Grid.SupportRule              = SpawnSupportRule::SurveySlope;
    Grid.BrakesEnabledAtSpawn     = true;
    Grid.HandbrakeEngagedAtSpawn  = false;  // 📝 a launch grid hands the car over live
    Grid.BrakeHoldSeconds         = 0.0f;
    Grid.ParkedPedalShare         = 0.0f;
    Grid.ReleasingSeconds         = 0.0f;
    return Grid;
}

SpawnPolicy SpawnPolicy::SuspendedDrop()
{
    SpawnPolicy Drop{};
    Drop.SupportRule              = SpawnSupportRule::SuspendAboveSurface;
    Drop.BrakesEnabledAtSpawn     = false;  // 📝 nothing to brake against while the wheels are in the air
    Drop.HandbrakeEngagedAtSpawn  = true;
    Drop.BrakeHoldSeconds         = 0.75f;
    Drop.ParkedPedalShare         = 0.0f;
    Drop.ReleasingSeconds         = 0.25f;
    return Drop;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     BRAKE RULES
//------------------------------------------------------------------------------------------------------------------------

ParkedBrakeState SpawnPolicy::ResolveParkedBrakeState() const noexcept
{
    ParkedBrakeState State{};
    if (!BrakesEnabledAtSpawn)
    {
        return State; // 📝 brakes disabled at the start: neither pedal is touched until the rules allow it
    }
    State.HandbrakeEngaged = HandbrakeEngagedAtSpawn;
    State.PedalShare       = std::clamp(ParkedPedalShare, 0.0f, 1.0f);
    return State;
}

void SpawnPolicy::ResolvePedalDemand(float  SecondsSinceSpawn,
                                     float  DriverPedal,
                                     float  DriverHandbrake,
                                     float& OutPedal,
                                     float& OutHandbrake) const noexcept
{
    if (!BrakesEnabledAtSpawn)
    {
        // 📝 Brakes disabled at the start: neither pedal answers the driver until the rules release them.
        if (OutranksDriver(SecondsSinceSpawn))
        {
            OutPedal     = 0.0f;
            OutHandbrake = 0.0f;
            return;
        }
        OutPedal     = std::clamp(DriverPedal, 0.0f, 1.0f);
        OutHandbrake = std::clamp(DriverHandbrake, 0.0f, 1.0f);
        return;
    }

    const ParkedBrakeState Parked = ResolveParkedBrakeState();

    if (OutranksDriver(SecondsSinceSpawn))
    {
        OutPedal     = Parked.PedalShare;
        OutHandbrake = Parked.HandbrakeEngaged ? 1.0f : 0.0f;
        return;
    }

    // 📝 The handover: from the parked share back to the driver's own demand over ReleasingSeconds, so the car is never dropped into a step
    //    change that would snap the tyres out from under it. The handbrake is the driver's alone once the rules have released him.
    const float Handover = SmoothStep(0.0f, std::max(1.0e-4f, ReleasingSeconds), SecondsSinceSpawn - BrakeHoldSeconds);
    const float Driver   = std::clamp(DriverPedal, 0.0f, 1.0f);
    OutPedal             = std::clamp(Parked.PedalShare + (Driver - Parked.PedalShare) * Handover, 0.0f, 1.0f);
    OutHandbrake         = std::clamp(DriverHandbrake, 0.0f, 1.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     FOOTPRINT SURVEY
//------------------------------------------------------------------------------------------------------------------------

SpawnPose ResolveSpawnPose(const std::array<float, 3>& RequestedGroundPointMetres,
                           float                      YawDegrees,
                           const SpawnPolicy&         Policy,
                           const SpawnSurfaceSampler& Sampler)
{
    SpawnPose Pose{};
    Pose.PositionMetres = RequestedGroundPointMetres;
    Pose.YawDegrees     = YawDegrees;
    Pose.GroundHeightMetres = RequestedGroundPointMetres[2];

    if (Policy.SupportRule == SpawnSupportRule::LevelGround || !Sampler)
    {
        Pose.Supported = true; // 📝 a studio pad, a bench, or a caller with no world to ask
        return Pose;
    }

    const float YawRadians = YawDegrees * (Pi / 180.0f);
    const float CosYaw     = std::cos(YawRadians);
    const float SinYaw     = std::sin(YawRadians);

    std::array<float, 3> NormalSum  = { 0.0f, 0.0f, 0.0f };
    float                Lowest     = 0.0f;
    bool                 AnySurface = false;
    bool                 HasLowest  = false;

    for (const std::array<float, 2>& Probe : FootprintProbes)
    {
        const float LocalX = Probe[0] * Policy.Footprint.HalfLengthMetres;
        const float LocalY = Probe[1] * Policy.Footprint.HalfWidthMetres;
        const std::array<float, 3> WorldPoint = {
            RequestedGroundPointMetres[0] + LocalX * CosYaw - LocalY * SinYaw,
            RequestedGroundPointMetres[1] + LocalX * SinYaw + LocalY * CosYaw,
            RequestedGroundPointMetres[2] + Policy.Footprint.CentreHeightMetres + Policy.Footprint.SearchHeightMetres
        };

        SpawnSurfaceProbe Surface{};
        if (!Sampler(WorldPoint, Surface) || !Surface.HasSurface)
        {
            continue;
        }
        AnySurface = true;
        NormalSum[0] += Surface.SurfaceNormal[0];
        NormalSum[1] += Surface.SurfaceNormal[1];
        NormalSum[2] += Surface.SurfaceNormal[2];
        Lowest    = HasLowest ? std::min(Lowest, Surface.ContactPointMetres[2]) : Surface.ContactPointMetres[2];
        HasLowest = true;
    }

    if (!AnySurface)
    {
        Pose.Surveyed  = true;
        Pose.Supported = false;
        if (Policy.SupportRule == SpawnSupportRule::SuspendAboveSurface)
        {
            Pose.PositionMetres[2] = RequestedGroundPointMetres[2] + Policy.Footprint.HoverMetres;
        }
        return Pose;
    }

    const float Length = std::sqrt(NormalSum[0] * NormalSum[0] + NormalSum[1] * NormalSum[1] + NormalSum[2] * NormalSum[2]);
    Pose.SurfaceNormal = (Length > 1.0e-5f) ? std::array<float, 3>{ NormalSum[0] / Length, NormalSum[1] / Length, NormalSum[2] / Length }
                                           : std::array<float, 3>{ 0.0f, 0.0f, 1.0f };
    Pose.GroundHeightMetres = Lowest;
    Pose.SlopeDegrees       = std::acos(std::clamp(Pose.SurfaceNormal[2], -1.0f, 1.0f)) * (180.0f / Pi);
    Pose.Supported          = Pose.SlopeDegrees <= Policy.Footprint.MaximumSlopeDegrees;
    Pose.Surveyed           = true;

    // 📝 The stand: the surface normal in the Agent's own axes gives the pitch and roll the chassis rests at. The survey's normal is a
    //    world vector; turning it back by the heading leaves the two Euler angles the level's R = Rz · Ry · Rx convention expects.
    const float NormalForward = Pose.SurfaceNormal[0] * CosYaw + Pose.SurfaceNormal[1] * SinYaw;
    const float NormalLeft    = -Pose.SurfaceNormal[0] * SinYaw + Pose.SurfaceNormal[1] * CosYaw;
    Pose.PitchDegrees         = std::asin(std::clamp(-NormalForward, -1.0f, 1.0f)) * (180.0f / Pi);
    Pose.RollDegrees          = std::asin(std::clamp(NormalLeft, -1.0f, 1.0f)) * (180.0f / Pi);

    if (!Pose.Supported)
    {
        // 📝 Too steep to stand on: keep the heading, drop the pitch and roll, and report it — the caller decides whether to hold the car.
        Pose.PitchDegrees = 0.0f;
        Pose.RollDegrees  = 0.0f;
    }

    Pose.PositionMetres[2] = Pose.GroundHeightMetres;
    if (Policy.SupportRule == SpawnSupportRule::SuspendAboveSurface)
    {
        Pose.PositionMetres[2] += Policy.Footprint.HoverMetres;
    }
    return Pose;
}

void ResolveDrivenWheels(DrivenWheelRule Rule, std::array<bool, 4>& OutDriven) noexcept
{
    OutDriven = { false, false, false, false };
    switch (Rule)
    {
    case DrivenWheelRule::FrontDriven:
        OutDriven = { true, true, false, false };
        break;
    case DrivenWheelRule::AllDriven:
        OutDriven = { true, true, true, true };
        break;
    case DrivenWheelRule::RearDriven:
    default:
        OutDriven = { false, false, true, true };
        break;
    }
}

} // namespace Frontier
