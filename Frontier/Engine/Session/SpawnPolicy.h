/*==========================================================================================================================================
                                                              SPAWNPOLICY.H
==========================================================================================================================================*/
// 🧩 Where a vehicle Agent stands when it appears and how its brakes behave in the opening seconds of a session. A vehicle spawns the
//    same way on a banked grid, a kerb, a hill or a flat studio floor: SpawnSupportRule surveys the surface under the whole footprint and
//    the Agent rests on it with its real pitch and roll. SpawnPolicy carries only the rules a session varies — brakes engaged or
//    released at the start, the parked pedal share, the unsupported settle time, and which wheels drive.

#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     SPAWN SUPPORT RULES
//------------------------------------------------------------------------------------------------------------------------

enum class SpawnSupportRule : uint8_t
{
    SurveySlope         = 0, // 📝 Probe the ground under the whole footprint: the Agent rests on the steepest safe stand
    LevelGround         = 1, // 📝 Trust the authored Z as flat (a studio pad, a bench, a scripted pose)
    SuspendAboveSurface = 2  // 📝 Hold the Agent at a hover height and let it drop onto whatever is below
};

enum class DrivenWheelRule : uint8_t
{
    RearDriven  = 0, // 📝 rear axle drives (the ControlVehicle's specification)
    FrontDriven = 1,
    AllDriven   = 2
};

// 📝 The vehicle's own ground envelope, so one survey covers the whole tyre patch rather than one point under the centre of mass.
struct SpawnFootprint
{
    float HalfLengthMetres   = 1.70f; // [m]  half the wheelbase envelope
    float HalfWidthMetres    = 1.05f; // [m]  half the track envelope
    float CentreHeightMetres = 0.53f; // [m]  centre of mass above the surface at rest
    float HoverMetres        = 0.60f; // [m]  extra height a SuspendAboveSurface spawn starts from
    float SearchHeightMetres = 6.00f; // [m]  how far above the requested point the survey starts looking down
    float MaximumSlopeDegrees = 42.0f; // [°]  steepest stand the survey accepts before it reports Unsupported
};

// 📝 What a surface sampler reports under one probe point.
struct SpawnSurfaceProbe
{
    std::array<float, 3> ContactPointMetres = { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> SurfaceNormal      = { 0.0f, 0.0f, 1.0f };
    bool                 HasSurface         = false;
};

// 📝 Asks the world where the ground is under a point (a physics ray in play, a height field in a test).
using SpawnSurfaceSampler = std::function<bool(const std::array<float, 3>& AbovePointMetres, SpawnSurfaceProbe& OutProbe)>;

// 📝 The stand the survey resolves: the authored origin on the surface, the heading, and the surface's own pitch and roll.
struct SpawnPose
{
    std::array<float, 3> PositionMetres      = { 0.0f, 0.0f, 0.0f }; // [m] ground point under the centre of mass
    float                YawDegrees          = 0.0f;                 // [°]  heading about +Z
    float                PitchDegrees        = 0.0f;                 // [°]  level Euler β, nose up is negative
    float                RollDegrees         = 0.0f;                 // [°]  level Euler α, left side up is positive
    std::array<float, 3> SurfaceNormal       = { 0.0f, 0.0f, 1.0f }; // [-]  surveyed, normalised
    float                GroundHeightMetres  = 0.0f;                 // [m]  lowest contact point of the footprint
    float                SlopeDegrees        = 0.0f;                 // [°]  angle between the normal and +Z
    bool                 Supported           = false;                // [-]  every probe point found a surface
    bool                 Surveyed            = false;                // [-]  a sampler answered at all
};

// 📝 The brake state the session asks for the instant a vehicle appears.
struct ParkedBrakeState
{
    bool  HandbrakeEngaged = true;  // [-]  rear mechanical lock held on the pad
    float PedalShare       = 0.0f;  // [-]  foot-brake share applied while parked
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     SPAWN POLICY
//------------------------------------------------------------------------------------------------------------------------

struct SpawnPolicy
{
    SpawnSupportRule SupportRule            = SpawnSupportRule::SurveySlope;
    SpawnFootprint   Footprint{};
    DrivenWheelRule  DrivesWheels           = DrivenWheelRule::RearDriven;

    bool  BrakesEnabledAtSpawn  = true;   // [-]  a disabled start ignores the pedals for BrakeHoldSeconds
    bool  HandbrakeEngagedAtSpawn = true; // [-]  the car is handed over parked rather than rolling
    float BrakeHoldSeconds      = 0.0f;   // [s]  how long the spawn brake rules outrank the driver
    float ParkedPedalShare      = 0.35f;  // [-]  foot brake held while the spawn rules outrank the driver
    float ReleasingSeconds      = 0.35f;  // [s]  ramp from the parked share to the driver's own demand

    // 📝 A Car Studio turntable: the car is parked on its pad with the handbrake on and the driver ignored until ready-up.
    [[nodiscard]] static SpawnPolicy StudioParked();

    // 📝 A proving-ground grid: the car stands on the surveyed surface, brakes live, handbrake already released.
    [[nodiscard]] static SpawnPolicy GridRelease();

    // 📝 An air drop: no brakes until the wheels are down, then the rear lock holds the landing.
    [[nodiscard]] static SpawnPolicy SuspendedDrop();

    // 📝 The brake state asked for at the moment of spawning.
    [[nodiscard]] ParkedBrakeState ResolveParkedBrakeState() const noexcept;

    // 📝 Blends the driver's pedal demand with the spawn rules for SecondsSinceSpawn, writing the resolved demand back out.
    void ResolvePedalDemand(float  SecondsSinceSpawn,
                            float  DriverPedal,
                            float  DriverHandbrake,
                            float& OutPedal,
                            float& OutHandbrake) const noexcept;

    // 📝 True while the spawn brake rules still outrank the driver (the wheels are not his yet).
    [[nodiscard]] bool OutranksDriver(float SecondsSinceSpawn) const noexcept
    {
        return SecondsSinceSpawn < BrakeHoldSeconds;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     SPAWN FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The single way any Agent is stood in the world: survey the footprint under RequestedGroundPointMetres through Sampler and resolve
//    the pose the Agent rests in, whatever the ground does there. Without a sampler the requested point is trusted as authored.
[[nodiscard]] SpawnPose ResolveSpawnPose(const std::array<float, 3>& RequestedGroundPointMetres,
                                         float                      YawDegrees,
                                         const SpawnPolicy&         Policy,
                                         const SpawnSurfaceSampler& Sampler);

// 📝 Writes the four-corner driven mask a spec's DrivesWheels asks for (FL, FR, RL, RR).
void ResolveDrivenWheels(DrivenWheelRule Rule, std::array<bool, 4>& OutDriven) noexcept;

} // namespace Frontier
