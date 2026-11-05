/*==========================================================================================================================================
                                                     PLUME SOURCE PROJECTION
                                     ------------------------------------------------------------------
                  Vehicle telemetry projected into smoke, soot and fire sources, fire lights
                          and ember emitters: onset hysteresis, burnout, burning
                                              rubber, afterfire pops.
==========================================================================================================================================*/

// 🧩 Where a plume comes from: the portable (CPU) form of the demo's PlumeSourceProjection.js, 1:1.
//
// 📝 A port of Frontier's vehicle gas intake: SmokeOnsetClassifier.h decides whether a tyre is smoking
//    (entry above SourcePowerOnset sustained for SourceOnsetSeconds, release below SourceReleaseRatio
//    of it after SourceReleaseSeconds), and GasVehicleIntake.h ResolveWheelSmoke() turns the sliding
//    power, the tread temperature and the burning state into rates at the contact patch — smoke, heat
//    and flame per second — with a share of the patch velocity as injected momentum. The constants are
//    SmokeSpecification's. What the exhibit adds:
//
//    | Source    | Where and why                                                                                |
//    |-----------|----------------------------------------------------------------------------------------------|
//    | patch     | Frontier's, plus the wheel's surface slip: a spinning tyre throws its smoke rearward along    |
//    |           | the tread, which is what makes a burnout boil out behind the car instead of hovering          |
//    | arch      | a third of the patch's smoke carried round on the tread and shed behind and above the hub —   |
//    |           | the cloud that rolls out of the wheel arch                                                    |
//    | burning   | the flame wrapping the lower tyre, shedding black soot rather than white rubber smoke          |
//    | exhaust   | afterfire pops: short incandescent bursts thrown rearward with a dark puff                    |
//    | burst     | transient: a tyre letting go, fracture dust — queued with a duration and shed until done      |
//
//    Smoke follows the tread's heat: cold rubber barely smokes, a tread past 250 °C vapourises — the
//    Warm factor below.
//
// 📝 The result is the flat source table PlumeSpace::Schedule consumes (PlumeSourceStride floats per
//    source), plus the fire lights and ember emitters the renderer and the particle system read. No
//    device, no allocation per frame.

#pragma once

#include "Frontier/Engine/Foundation/Math.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier
{

// 📝 A unit quaternion in xyzw order (the demo's convention): the turning operator of an orientation.
struct Rotor
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    float W = 1.0f;
};

// 📐 The Hamilton product: applying Right's turn and then Left's.
[[nodiscard]] Rotor ComposeRotor(Rotor Left, Rotor Right) noexcept;

// 📐 The turn of θ [rad] about Axis (unit): (Axis·sin(θ/2), cos(θ/2)).
[[nodiscard]] Rotor AxisRotor(Vec3 Axis, float Turn) noexcept;

// 📐 v' = v + 2w(q̂×v) + 2q̂×(q̂×v), the expanded sandwich product without forming the conjugate.
[[nodiscard]] Vec3 RotateByRotor(Rotor Turn, Vec3 Vector) noexcept;

// 📝 The tyre-plume constants (SmokeSpecification).
struct SmokeSpecification
{
    static constexpr float SourcePowerOnset = 200.0f;      // [W]   sliding power at which a tyre begins to smoke
    static constexpr float SourceReleaseRatio = 0.60f;     // [-]   fraction of the onset that holds a smoking tyre smoking
    static constexpr float SourceOnsetSeconds = 0.05f;     // [s]   the onset power must be sustained this long
    static constexpr float SourceReleaseSeconds = 0.08f;   // [s]   grace before a quiet tyre stops smoking
    static constexpr float SourcePowerFull = 18000.0f;     // [W]   sliding power at which the source saturates
    static constexpr float MinimumSlipSpeed = 0.35f;       // [m/s] below this the tread is not sliding, whatever the power
    static constexpr float DensityRate = 18.0f;            // [1/s] smoke density shed by a saturated patch
    static constexpr float CarrierVelocityShare = 0.15f;   // [-]   share of the contact-patch velocity the smoke is born with
    static constexpr float SmokeHeatRate = 0.04f;          // [K/s] heat a saturated patch carries into the lattice
    static constexpr float FireHeatPowerScale = 0.001f;    // [K/J] combustion power to lattice heat
    static constexpr float PatchRadius = 0.16f;            // [m]   GasRoadTyrePatchRadius
};

// 📝 Tuned constants the specification above does not carry.
inline constexpr float SmokeWarmBase = 333.15f;        // [K]   below this a sliding tread hardly smokes (60 °C)
inline constexpr float SmokeWarmSpan = 190.0f;         // [K]   from WarmBase up to full vapourisation
inline constexpr float SmokeFlingShare = 0.35f;        // [-]   of the surface slip speed the thrown smoke carries
inline constexpr float SmokeFlingCeiling = 9.0f;       // [m/s]
inline constexpr float SmokeArchShare = 0.35f;         // [-]   of the patch smoke shed from the wheel arch
inline constexpr float SmokeFlameSidewallReach = 0.24f;// [m]   hub to the flame seat outside the sidewall
inline constexpr float SmokeFlameRise = 2.6f;          // [m/s] momentum of a flame seat: burning rubber jets its vapour upward
inline constexpr float SmokeFireLightGain = 0.00042f;  // [lx·m²/W] combustion power to fire-light intensity
inline constexpr float SmokeFlameSeatRadius = 0.30f;
inline constexpr float SmokeTrailingSeatRadius = 0.28f;
inline constexpr float SmokeArchRadius = 0.28f;
inline constexpr float SmokeExhaustRadius = 0.10f;

// 📝 A sooty rubber flame, linear Rec.709.
inline constexpr std::array<float, 3> SmokeFireColour = { 1.0f, 0.46f, 0.14f };

//------------------------------------------------------------------------------------------------------------------------
//                                                       TELEMETRY
//------------------------------------------------------------------------------------------------------------------------

// 📝 What the tyre thermal model reports for one wheel.
struct PlumeWheelThermal
{
    bool Ignited = false;              // the rubber is burning
    float CombustionPower = 0.0f;      // [W]
    float SurfaceTemperature = 0.0f;   // [K] tread surface
};

// 📝 What the tyre contact solver reports for one wheel, plus the wheel's own geometry.
struct PlumeWheelReading
{
    Vec3 ContactPoint = {};            // [m] world
    Vec3 HubPosition = {};             // [m] world
    Vec3 ContactVelocity = {};         // [m/s] world velocity of the patch
    Vec3 Longitudinal = { 1.0f, 0.0f, 0.0f };   // [-] unit tread heading, steered
    float SlipSpeed = 0.0f;            // [m/s]
    float SlidingPower = 0.0f;         // [W]
    float SurfaceSpeed = 0.0f;         // [m/s] spin × radius
    float Radius = 0.35f;              // [m]
    bool InContact = false;            // the patch is on the road and not on debris
    bool Shredded = false;             // no tread left: no rubber to vapourise
    bool Burst = false;                // the tyre just let go: a quarter of the usual smoke
    PlumeWheelThermal Thermal = {};
};

// 📝 One exhaust tip.
struct PlumeExhaustReading
{
    Vec3 Position = {};                // [m] world
    Vec3 Direction = { 0.0f, 0.0f, 1.0f };   // [-] unit, the jet axis
};

// 📝 Everything the projection reads in one frame.
struct PlumeTelemetry
{
    Vec3 Position = {};                // [m] the vehicle (the plume window's focus)
    Vec3 Velocity = {};                // [m/s]
    std::vector<PlumeExhaustReading> Exhausts;
    float AfterfireSeconds = 0.0f;     // [s] remaining window during which the exhaust may pop
    std::vector<PlumeWheelReading> Wheels;
};

// 📝 Per-advance multipliers and switches.
struct PlumeProjectionOptions
{
    bool Afterfire = false;
    float EmissionRate = 1.0f;         // [×] scales every source
    float SmokeYield = 1.0f;           // [×] on top of EmissionRate
    float HeatYield = 1.0f;
    float SootYield = 1.0f;
    float FireLight = 1.0f;            // [×] fire-light intensity
    float Embers = 1.0f;               // [×] ember emission rate
};

// 📝 One fire light: a world position, a softening radius and a linear radiance.
struct PlumeSourceFireLight
{
    Vec3 Position = {};
    float Radius = 0.0f;
    Vec3 Radiance = {};
};

// 📝 One ember emitter, for the particle system: where embers are born, how fast, how hot, how they
//    spread, rise and fall, and how long they live.
struct PlumeEmberEmitter
{
    Vec3 Position = {};
    Vec3 Velocity = {};
    float Rate = 0.0f;                 // [1/s]
    float Temperature = 0.0f;          // [K]
    float Spread = 0.0f;               // [-] cone half-angle factor
    float Lift = 0.0f;                 // [m/s] buoyant rise
    float Gravity = 0.0f;              // [m/s²] settling
    float Life = 0.0f;                 // [s]
};

// 📝 A transient source queued with a duration: a tyre letting go, fracture dust.
struct PlumeBurst
{
    Vec3 Position = {};
    float Radius = 0.30f;
    Vec3 Velocity = {};
    float Smoke = 0.0f;                // [1/s]
    float Heat = 0.0f;                 // [K/s]
    float Flame = 0.0f;                // [1/s]
    float Soot = 0.0f;                 // [1/s]
    float Seconds = 0.0f;              // [s] how long the burst lasts
    float RemainingSeconds = 0.0f;     // [s] set to Seconds by QueueBurst, counted down by Project
};

// 📝 One source, decoded: the twelve floats the lattice consumes.
struct PlumeSourceEntry
{
    Vec3 Position = {};
    float Radius = 0.0f;
    Vec3 Velocity = {};
    float Smoke = 0.0f;                // [1/s]
    float Heat = 0.0f;                 // [K/s]
    float Flame = 0.0f;                // [1/s]
    float Soot = 0.0f;                 // [1/s]
};

// 📝 One wheel's smoking onset: entry needs power and slip speed sustained, release has grace, a
//    loss of contact cuts the source at once.
struct SmokeOnsetTracker
{
    float RequestSeconds = 0.0f;
    float QuietSeconds = 0.0f;
    bool Active = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      PROJECTION
//------------------------------------------------------------------------------------------------------------------------

class PlumeSourceProjection
{
  public:
    PlumeSourceProjection();

    // 📝 Forgets the onset hysteresis, the pops and the queued bursts; the source table is refilled by
    //    the next Project.
    void Reset() noexcept;

    // 📝 Queues a transient source, shed until its time is spent. At most four are held; the oldest is
    //    dropped for a newer one.
    void QueueBurst(const PlumeBurst& Burst);

    // 📝 AdvanceSmokeOnset(). Returns whether the tyre is smoking after this advance.
    [[nodiscard]] bool AdvanceOnset(uint32_t Wheel, float Power, float SlipSpeed, bool Contact, float Seconds) noexcept;

    // 📝 Fills the source table with this advance's sources, the fire lights and the ember emitters.
    //    Returns the number of sources written.
    uint32_t Project(const PlumeTelemetry& Telemetry, float Seconds, const PlumeProjectionOptions& Options);

    // 📝 Bounds of every source grown by 3.5 of its radii (the Gaussian's reach), for the lattice's
    //    cheap reject. Empty when there is no source.
    [[nodiscard]] bool ResolveBounds(Vec3& Minimum, Vec3& Maximum) const noexcept;

    // 📝 The flat source table, PlumeSourceStride floats per source, ready for PlumeSpace::Schedule.
    [[nodiscard]] std::span<const float> Records() const noexcept;
    [[nodiscard]] std::span<float> Records() noexcept;

    [[nodiscard]] uint32_t Count() const noexcept { return Written; }
    [[nodiscard]] uint32_t ActiveTyres() const noexcept { return Smoking; }
    [[nodiscard]] bool IsPopping() const noexcept { return Popping; }
    [[nodiscard]] float ElapsedSeconds() const noexcept { return Elapsed; }
    [[nodiscard]] const std::vector<PlumeSourceFireLight>& Lights() const noexcept { return Flames; }
    [[nodiscard]] const std::vector<PlumeEmberEmitter>& Embers() const noexcept { return EmberEmitters; }
    [[nodiscard]] const std::array<SmokeOnsetTracker, 4>& Onsets() const noexcept { return Trackers; }

    // 📝 One source, decoded from the flat table.
    [[nodiscard]] PlumeSourceEntry Source(uint32_t Index) const noexcept;

    // 📝 Appends one source to the table, at the next slot, if a slot is free and the source sheds
    //    something. Returns whether it was appended.
    bool AppendSource(Vec3 Position, float Radius, Vec3 Velocity, float Smoke, float Heat, float Flame, float Soot);

  private:
    double ProjectUniform() noexcept;

    std::array<SmokeOnsetTracker, 4> Trackers = {};
    std::array<float, 2> PopSeconds = {};
    std::array<float, 2> PopStrength = {};
    uint32_t Seed = 0x2545F491u;

    std::vector<float> Table;
    uint32_t Written = 0u;
    uint32_t Smoking = 0u;
    bool Popping = false;
    std::vector<PlumeBurst> Bursts;
    std::vector<PlumeSourceFireLight> Flames;
    std::vector<PlumeEmberEmitter> EmberEmitters;
    float Elapsed = 0.0f;
};

} // namespace Frontier
