/*==========================================================================================================================================
                                                          SMOKE SEQUENCE
                                     ------------------------------------------------------------------
                    The tyre plume system: the gas parameter catalogue the editor drives, the
                    mapping onto the lattice's settings, vehicle telemetry projected onto the
                        plume's wheels, and the frame advance that schedules the step.
==========================================================================================================================================*/

// 🧩 The tyre plume system: the portable (CPU) form of the demo's SmokeSequence.js, 1:1.
//
// 📝 SmokeSequence is the adapter between the vehicle and the plume lattice: it owns the parameter
//    catalogue the editor exposes, maps it onto PlumeSettings once a frame, projects the vehicle's
//    telemetry into sources through PlumeSourceProjection, and hands both to PlumeSpace::Schedule.
//    The lattice itself, the step record and the residency rules live in PlumeSpace; the physics of a
//    smoking tyre lives here.
//
//    | Concern        | Owner                                                                            |
//    |----------------|----------------------------------------------------------------------------------|
//    | parameter set  | GasParameterCatalogue — name, default, range, step, unit, label                  |
//    | slider → step  | MapGasParameters()                                                               |
//    | telemetry      | ProjectPlumeTelemetry() — steered tread headings, contact, thermal state         |
//    | sources        | PlumeSourceProjection — onset hysteresis, burnout, burning rubber, pops, bursts  |
//    | lattice        | PlumeSpace — toroidal window, fixed-rate step, record, residency                 |
//
// 📝 Nothing here touches a device. The caller copies the source table and the step record into its own
//    bindings, which is what makes the whole system testable on a headless host.

#pragma once

#include "Frontier/Engine/Foundation/Math.h"
#include "Frontier/Engine/Rendering/PlumeSourceProjection.h"
#include "Frontier/Engine/Rendering/PlumeSpace.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{

// 📝 The gas parameters, in catalogue order. Every field is a float, so the catalogue below can name
//    them by pointer-to-member and the editor can drive them generically.
struct GasParameters
{
    // source — multipliers on the tyre model's own rates, which stay the physical port
    float Emission = 1.0f;
    float Heat = 1.0f;
    float Expansion = 1.0f;
    float Soot = 1.0f;
    float Momentum = 9.0f;
    float TreadEjection = 1.0f;
    // simulation
    float Buoyancy = 2.4f;
    float Dissipation = 0.10f;
    float Cooling = 0.55f;
    float Vorticity = 3.0f;
    float Turbulence = 0.60f;
    float TurbulenceScale = 3.7f;
    float WindSpeed = 0.8f;
    float WindHeading = 30.0f;
    float TimeScale = 1.0f;
    float PressureSweeps = 16.0f;
    // appearance
    float Density = 1.0f;
    float Albedo = 0.88f;
    float Phase = 0.38f;
    float MultiScatter = 0.75f;
    float Ambient = 1.0f;
    float Detail = 0.70f;
    float DetailScale = 2.2f;
    float Erosion = 1.0f;
    float Warp = 0.0f;
    float Powder = 0.0f;
    // fire
    float FlameIntensity = 1.0f;
    float FlameTemperature = 1.0f;
    float FlameCore = 1800.0f;
    float FlameTip = 1150.0f;
    float FlameFlicker = 0.6f;
    float FlameLoss = 0.96f;
    float FireLight = 1.0f;
};

// 📝 One entry of the catalogue: the name the editor and the URL share, where the value lives, the
//    range and step the slider uses, the unit and the label.
struct GasParameterDescriptor
{
    const char* Name = "";
    float GasParameters::* Slot = nullptr;
    float Default = 0.0f;
    float Minimum = 0.0f;
    float Maximum = 1.0f;
    float Step = 0.01f;
    const char* Unit = "";
    const char* Label = "";
};

// 📝 The catalogue, in editor order. Its size is fixed at compile time.
[[nodiscard]] std::span<const GasParameterDescriptor> GasParameterCatalogue() noexcept;

// 📝 The catalogue defaults, as a parameter set.
[[nodiscard]] GasParameters ConstructGasParameters() noexcept;

// 📝 Reads one named parameter (0 when the name is unknown).
[[nodiscard]] float ReadGasParameter(const GasParameters& Set, std::string_view Name) noexcept;

// 📝 Writes one named parameter, clamped to the catalogue's range. False when the name is unknown.
[[nodiscard]] bool AssignGasParameter(GasParameters& Set, std::string_view Name, float Magnitude) noexcept;

// 📝 Clamps every parameter into its catalogue range.
void ClampGasParameters(GasParameters& Set) noexcept;

// 📝 The gas parameters as the lattice's settings. PlumeDensity carries the 1.2 the exhibit's smoke
//    reads at; FlameLoss is a per-second retention, so it becomes a decay rate of −ln(1 − loss).
[[nodiscard]] PlumeSettings MapGasParameters(const GasParameters& Gas, bool Enabled = true) noexcept;

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                        TELEMETRY
//--------------------------------------------------------------------------------------------------------------------------------------

// 📝 One wheel as the vehicle model reports it, before the plume projection reads it.
struct SmokeWheelState
{
    Rotor Steer = {};                  // the steering turn about the wheel's upright axis
    float Radius = 0.35f;              // [m]
    float Spin = 0.0f;                 // [rad/s] signed
};

// 📝 What the contact solver reports for one wheel.
struct SmokeContactReading
{
    Vec3 ContactPoint = {};            // [m] world
    Vec3 HubPosition = {};             // [m] world
    Vec3 ContactVelocity = {};         // [m/s] world velocity of the patch
    float ContactFraction = 0.0f;      // [-] 1 fully on the road
    float SlipSpeed = 0.0f;            // [m/s]
    float SlidingPower = 0.0f;         // [W]
    bool OnDebris = false;
    bool Shredded = false;             // no tread left
};

// 📝 What the tyre thermal model reports for one wheel.
struct SmokeThermalReading
{
    bool Ignited = false;
    float CombustionPower = 0.0f;      // [W]
    float Surface = 0.0f;              // [K] tread surface temperature
    bool Burst = false;                // the tyre just let go
};

// 📝 The vehicle as the plume reads it.
struct SmokeVehicleState
{
    Vec3 Position = {};                // [m] world
    Vec3 LinearVelocity = {};          // [m/s]
    Rotor Orientation = {};            // [-] the body's turn
    std::vector<SmokeWheelState> Wheels;
};

// 📝 Projects the vehicle, the contact readings and the thermal state into the plume's telemetry: the
//    steered tread heading of each wheel, the contact decision and the thermal state, in wheel order.
[[nodiscard]] PlumeTelemetry ProjectPlumeTelemetry(const SmokeVehicleState& Vehicle,
                                                    std::span<const SmokeContactReading> Readings,
                                                    std::span<const SmokeThermalReading> Thermal) noexcept;

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                        SEQUENCE
//--------------------------------------------------------------------------------------------------------------------------------------

// 📝 An extra source the caller supplies directly — an editor plume, a fracture puff, a scripted burst.
//    The first four slots are the tyres'; extras follow.
struct SmokeEmission
{
    bool Active = true;
    Vec3 Position = {};
    float Radius = 0.30f;
    Vec3 Velocity = { 0.0f, 0.0f, 1.0f };
    float SmokeRate = 0.0f;
    float HeatRate = 0.0f;
    float FlameRate = 0.0f;
    float SootRate = 0.0f;
};

// 📝 One frame's advance inputs.
struct SmokeAdvanceFrame
{
    bool Enabled = true;
    SmokeVehicleState Vehicle;
    Vec3 SunDirection = { 0.0f, 0.0f, 1.0f };
    GasParameters Gas;
    std::vector<SmokeContactReading> Readings;
    std::vector<SmokeThermalReading> Thermal;
    std::vector<SmokeEmission> Emissions;
};

class SmokeSequence
{
  public:
    explicit SmokeSequence(const PlumeGrade& Configuration = PlumeGrades[2]);

    // 📝 Switches to another grade: new lattices, a clean plume.
    void ReconstructGrade(const PlumeGrade& Configuration);

    // 📝 One frame: project the telemetry into sources, schedule the lattice's step, publish the fire
    //    lights. FrameSeconds is the real elapsed time; the gas TimeScale stretches the simulation's
    //    own step, capped at 0.1 s so a hitch cannot launch the plume out of its window.
    void Advance(float FrameSeconds, const SmokeAdvanceFrame& Frame);

    // 📝 The settings the last Advance mapped — what the lattice is running this frame.
    [[nodiscard]] const PlumeSettings& Settings() const noexcept { return Mapped; }

    [[nodiscard]] const PlumeSourceProjection& Sources() const noexcept { return Projection; }
    [[nodiscard]] PlumeSourceProjection& Sources() noexcept { return Projection; }
    [[nodiscard]] const PlumeSpace& Plume() const noexcept { return Lattice; }
    [[nodiscard]] PlumeSpace& Plume() noexcept { return Lattice; }

    [[nodiscard]] uint32_t SourceCount() const noexcept { return Projection.Count(); }
    [[nodiscard]] uint32_t StepsTaken() const noexcept { return Lattice.StepsTaken(); }
    [[nodiscard]] uint32_t Relocations() const noexcept { return Moved; }
    [[nodiscard]] uint32_t QueuedSources() const noexcept { return Queued; }

    // 📝 The aggregate fire light: the sources' lights weighted by radiance, for the direct pass.
    [[nodiscard]] const PlumeFireLight& FireLight() const noexcept { return Aggregate; }

    // 📝 The editor's readout lines.
    [[nodiscard]] std::vector<std::string> QueryReadout() const;

    // 📝 Marks that the transport of the pending step has been recorded — the way the demo's
    //    RecordTransport flips the lattice parity at the end of its pass. A host with no device calls
    //    this once it has consumed the step record; a host that records the passes commits instead.
    void CommitStep() noexcept;

    // 📝 Forgets the plume and the sources at once.
    void Clear() noexcept;

  private:
    PlumeSpace Lattice;
    PlumeSourceProjection Projection;
    PlumeSettings Mapped = {};
    PlumeFireLight Aggregate = {};
    std::array<int32_t, 2> Origin = { 0, 0 };
    uint32_t Moved = 0u;
    uint32_t Queued = 0u;
    bool Seated = false;
};

} // namespace Frontier
