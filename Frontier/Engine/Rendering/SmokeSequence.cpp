//==========================================================================================================================================
//                                                        SMOKESEQUENCE.CPP
//==========================================================================================================================================
//  The tyre plume system: the gas parameter catalogue, the mapping onto the lattice's settings, the
//  projection of vehicle telemetry, and the frame advance that schedules the step.
//  Ported 1:1 from Experimental/VehicleHmiPrototype/Source/SmokeSequence.js.
//==========================================================================================================================================

#include "SmokeSequence.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace Frontier
{

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                     PARAMETER CATALOGUE
//--------------------------------------------------------------------------------------------------------------------------------------

std::span<const GasParameterDescriptor> GasParameterCatalogue() noexcept
{
    static constexpr GasParameterDescriptor Catalogue[] = {
        // source — multipliers on the tyre model's own rates, which stay the physical port
        { "Emission",         &GasParameters::Emission,         1.00f,  0.00f,   4.00f, 0.050f, "×",      "Emission rate" },
        { "Heat",             &GasParameters::Heat,             1.00f,  0.00f,   4.00f, 0.050f, "×",      "Temperature" },
        { "Expansion",        &GasParameters::Expansion,        1.00f,  0.00f,   3.00f, 0.050f, "×",      "Volume expansion" },
        { "Soot",             &GasParameters::Soot,             1.00f,  0.00f,   4.00f, 0.050f, "×",      "Soot yield" },
        { "Momentum",         &GasParameters::Momentum,         9.00f,  0.00f,  24.00f, 0.500f, "1/s",    "Carrier drag" },
        { "TreadEjection",    &GasParameters::TreadEjection,    1.00f,  0.00f,   3.00f, 0.050f, "×",      "Tread ejection" },
        // simulation
        { "Buoyancy",         &GasParameters::Buoyancy,         2.40f,  0.00f,   8.00f, 0.100f, "m/s²·K", "Buoyancy" },
        { "Dissipation",      &GasParameters::Dissipation,      0.10f,  0.00f,   0.90f, 0.010f, "1/s",    "Dissipation" },
        { "Cooling",          &GasParameters::Cooling,          0.55f,  0.00f,   0.95f, 0.010f, "1/s",    "Cooling" },
        { "Vorticity",        &GasParameters::Vorticity,        3.00f,  0.00f,   6.00f, 0.100f, "ε",      "Vorticity confinement" },
        { "Turbulence",       &GasParameters::Turbulence,       0.60f,  0.00f,   2.00f, 0.020f, "m/s",    "Turbulence" },
        { "TurbulenceScale",  &GasParameters::TurbulenceScale,  3.70f,  0.20f,   4.00f, 0.050f, "1/m",    "Turbulence frequency" },
        { "WindSpeed",        &GasParameters::WindSpeed,        0.80f,  0.00f,  12.00f, 0.100f, "m/s",    "Wind speed" },
        { "WindHeading",      &GasParameters::WindHeading,     30.00f,  0.00f, 360.00f, 1.000f, "°",      "Wind heading" },
        { "TimeScale",        &GasParameters::TimeScale,        1.00f,  0.25f,   2.00f, 0.050f, "×",      "Time scale" },
        { "PressureSweeps",   &GasParameters::PressureSweeps,  16.00f,  4.00f,  40.00f, 2.000f, "",       "Pressure iterations" },
        // appearance
        { "Density",          &GasParameters::Density,          1.00f,  0.20f,   3.00f, 0.050f, "×",      "Density" },
        { "Albedo",           &GasParameters::Albedo,           0.88f,  0.20f,   0.98f, 0.010f, "",       "Albedo" },
        { "Phase",            &GasParameters::Phase,            0.38f,  0.00f,   0.90f, 0.010f, "g",      "Forward scattering" },
        { "MultiScatter",     &GasParameters::MultiScatter,     0.75f,  0.00f,   1.50f, 0.050f, "",       "Multiple scattering" },
        { "Ambient",          &GasParameters::Ambient,          1.00f,  0.00f,   3.00f, 0.050f, "×",      "Ambient light" },
        { "Detail",           &GasParameters::Detail,           0.70f,  0.00f,   1.00f, 0.020f, "",       "Detail" },
        { "DetailScale",      &GasParameters::DetailScale,      2.20f,  0.20f,   4.00f, 0.050f, "1/m",    "Detail frequency" },
        { "Erosion",          &GasParameters::Erosion,          1.00f,  0.00f,   1.50f, 0.050f, "",       "Edge erosion" },
        { "Warp",             &GasParameters::Warp,             0.00f,  0.00f,   1.00f, 0.050f, "",       "Billow warp" },
        { "Powder",           &GasParameters::Powder,           0.00f,  0.00f,   1.00f, 0.050f, "",       "Powder" },
        // fire
        { "FlameIntensity",   &GasParameters::FlameIntensity,   1.00f,  0.00f,   4.00f, 0.050f, "×",      "Flame intensity" },
        { "FlameTemperature", &GasParameters::FlameTemperature, 1.00f,  0.70f,   1.60f, 0.010f, "×",      "Flame temperature" },
        { "FlameCore",        &GasParameters::FlameCore,     1800.00f, 1300.0f, 2400.0f, 10.00f, "K",      "Flame core" },
        { "FlameTip",         &GasParameters::FlameTip,      1150.00f, 900.0f,  1600.0f, 10.00f, "K",      "Flame tip" },
        { "FlameFlicker",     &GasParameters::FlameFlicker,     0.60f,  0.00f,   1.00f, 0.020f, "",       "Flicker" },
        { "FlameLoss",        &GasParameters::FlameLoss,        0.96f,  0.50f,   0.995f, 0.005f, "1/s",    "Flame decay" },
        { "FireLight",        &GasParameters::FireLight,        1.00f,  0.00f,   4.00f, 0.050f, "×",      "Fire light" },
    };
    return std::span<const GasParameterDescriptor>(Catalogue, sizeof(Catalogue) / sizeof(Catalogue[0]));
}

GasParameters ConstructGasParameters() noexcept
{
    GasParameters Set;
    for (const GasParameterDescriptor& Entry : GasParameterCatalogue())
        Set.*Entry.Slot = Entry.Default;
    return Set;
}

float ReadGasParameter(const GasParameters& Set, std::string_view Name) noexcept
{
    for (const GasParameterDescriptor& Entry : GasParameterCatalogue())
        if (Name == Entry.Name)
            return Set.*Entry.Slot;
    return 0.0f;
}

bool AssignGasParameter(GasParameters& Set, std::string_view Name, float Magnitude) noexcept
{
    for (const GasParameterDescriptor& Entry : GasParameterCatalogue())
    {
        if (Name != Entry.Name)
            continue;
        Set.*Entry.Slot = std::clamp(Magnitude, Entry.Minimum, Entry.Maximum);
        return true;
    }
    return false;
}

void ClampGasParameters(GasParameters& Set) noexcept
{
    for (const GasParameterDescriptor& Entry : GasParameterCatalogue())
        Set.*Entry.Slot = std::clamp(Set.*Entry.Slot, Entry.Minimum, Entry.Maximum);
}

PlumeSettings MapGasParameters(const GasParameters& Gas, bool Enabled) noexcept
{
    PlumeSettings Settings;
    Settings.PlumeEnabled = Enabled;
    Settings.PlumeEmissionRate = Gas.Emission;
    Settings.PlumeDensity = 1.2f * Gas.Density;
    Settings.PlumeHeat = Gas.Heat;
    Settings.PlumeSoot = Gas.Soot;
    Settings.PlumeBuoyancy = Gas.Buoyancy;
    Settings.PlumeDissipation = Gas.Dissipation;
    Settings.PlumeCooling = Gas.Cooling;
    Settings.PlumeVorticity = Gas.Vorticity;
    Settings.PlumeTurbulence = Gas.Turbulence;
    Settings.PlumeTurbulenceScale = Gas.TurbulenceScale;
    Settings.PlumeWindSpeed = Gas.WindSpeed;
    Settings.PlumeWindDirection = Gas.WindHeading;
    Settings.PlumeAlbedo = Gas.Albedo;
    Settings.PlumeAnisotropy = Gas.Phase;
    Settings.PlumeScattering = Gas.MultiScatter;
    Settings.PlumeAmbient = Gas.Ambient;
    Settings.PlumeDetail = Gas.Detail;
    Settings.PlumeDetailScale = Gas.DetailScale;
    Settings.PlumeFire = Gas.FlameIntensity;
    Settings.PlumeFlameCore = Gas.FlameCore * Gas.FlameTemperature;
    Settings.PlumeFlameTip = Gas.FlameTip * Gas.FlameTemperature;
    Settings.PlumeFireLight = Gas.FireLight;
    // 📝 FlameLoss is the fraction of the flame that survives one second; the lattice needs the rate.
    Settings.PlumeFlameDecay = -std::log(std::max(0.005f, 1.0f - Gas.FlameLoss));
    return Settings;
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                         TELEMETRY
//--------------------------------------------------------------------------------------------------------------------------------------

PlumeTelemetry ProjectPlumeTelemetry(const SmokeVehicleState& Vehicle,
                                      std::span<const SmokeContactReading> Readings,
                                      std::span<const SmokeThermalReading> Thermal) noexcept
{
    PlumeTelemetry Telemetry;
    Telemetry.Position = Vehicle.Position;
    Telemetry.Velocity = Vehicle.LinearVelocity;

    for (uint32_t Index = 0u; Index < Readings.size(); ++Index)
    {
        const SmokeContactReading& Reading = Readings[Index];
        const SmokeWheelState& Wheel = Index < Vehicle.Wheels.size() ? Vehicle.Wheels[Index] : SmokeWheelState{};
        const SmokeThermalReading& Heat = Index < Thermal.size() ? Thermal[Index] : SmokeThermalReading{};

        PlumeWheelReading Projected;
        Projected.ContactPoint = Reading.ContactPoint;
        Projected.HubPosition = Reading.HubPosition;
        Projected.ContactVelocity = Reading.ContactVelocity;
        // 📝 The tread heading is the body's turn composed with the wheel's steering turn about the
        //    upright, applied to the body's forward axis.
        Projected.Longitudinal = RotateByRotor(ComposeRotor(Vehicle.Orientation, Wheel.Steer), Vec3{ 1.0f, 0.0f, 0.0f });
        Projected.SlipSpeed = Reading.SlipSpeed;
        // 📝 Bare rims have no tyre rubber to vaporise.
        Projected.SlidingPower = Reading.Shredded ? 0.0f : Reading.SlidingPower;
        Projected.SurfaceSpeed = Wheel.Spin * Wheel.Radius;
        Projected.Radius = Wheel.Radius;
        // 📝 A patch more than half on the road, and not on debris that would cushion the scrub.
        Projected.InContact = Reading.ContactFraction > 0.5f && !Reading.OnDebris;
        Projected.Shredded = Reading.Shredded;
        Projected.Burst = Heat.Burst;
        Projected.Thermal.Ignited = !Reading.Shredded && Heat.Ignited;
        Projected.Thermal.CombustionPower = Heat.CombustionPower;
        Projected.Thermal.SurfaceTemperature = Heat.Surface;
        Telemetry.Wheels.push_back(Projected);
    }
    return Telemetry;
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                          SEQUENCE
//--------------------------------------------------------------------------------------------------------------------------------------

SmokeSequence::SmokeSequence(const PlumeGrade& Configuration)
    : Lattice(Configuration)
{
}

void SmokeSequence::ReconstructGrade(const PlumeGrade& Configuration)
{
    Lattice.ReconstructGrade(Configuration);
    Seated = false;
    Clear();
}

void SmokeSequence::Clear() noexcept
{
    Lattice.Clear();
    Projection.Reset();
    Aggregate = PlumeFireLight{};
    Aggregate.Radiance = { 0.0f, 0.0f, 0.0f };
    Seated = false;
}

void SmokeSequence::Advance(float FrameSeconds, const SmokeAdvanceFrame& Frame)
{
    const PlumeSettings Mapped = MapGasParameters(Frame.Gas, Frame.Enabled);
    this->Mapped = Mapped;

    // 📝 The simulation's own clock: the gas TimeScale stretches it, capped at 0.1 s so a hitch cannot
    //    launch the plume out of its window.
    const float Step = std::min(0.1f, FrameSeconds * Frame.Gas.TimeScale);

    PlumeProjectionOptions Options;
    Options.Afterfire = true;
    Options.EmissionRate = Mapped.PlumeEmissionRate;
    Options.SmokeYield = Mapped.PlumeDensity;
    Options.HeatYield = Mapped.PlumeHeat;
    Options.SootYield = Mapped.PlumeSoot;
    Options.FireLight = Mapped.PlumeFireLight;

    const PlumeTelemetry Telemetry = ProjectPlumeTelemetry(Frame.Vehicle, Frame.Readings, Frame.Thermal);
    Projection.Project(Telemetry, Step, Options);

    // 📝 Preserve editor, fracture and burst plumes without duplicating the four tyre sources: the
    //    first four slots belong to the tyres, so only the extras are appended.
    Queued = Projection.Count();
    for (size_t Index = 4u; Index < Frame.Emissions.size(); ++Index)
    {
        const SmokeEmission& Extra = Frame.Emissions[Index];
        if (!Extra.Active || Projection.Count() >= PlumeSourceCapacity)
            continue;
        if (Projection.AppendSource(Extra.Position, Extra.Radius > 0.0f ? Extra.Radius : 0.30f,
                                    Extra.Velocity, Extra.SmokeRate, Extra.HeatRate, Extra.FlameRate, Extra.SootRate))
            ++Queued;
    }

    // 📝 The editor's pressure iterations, rounded to an even count: a host dispatch, so it costs
    //    nothing to change it live.
    Lattice.AssignPressureSweeps(uint32_t(std::round(Frame.Gas.PressureSweeps)));

    Lattice.Schedule(Step, FrameSeconds, { Frame.Vehicle.Position.X, Frame.Vehicle.Position.Y, Frame.Vehicle.Position.Z },
                     Projection.Records(), Projection.Count(), Mapped,
                     { Frame.SunDirection.X, Frame.SunDirection.Y, Frame.SunDirection.Z });
    if (!Seated)
    {
        Origin = Lattice.Origin();
        Seated = true;
    }
    else if (Origin[0] != Lattice.Origin()[0] || Origin[1] != Lattice.Origin()[1])
    {
        Origin = Lattice.Origin();
        ++Moved;                       // the toroidal window relabelled its texels
    }

    // 📝 The fire lights: brightest first, published every frame because the direct pass lights the
    //    scene with them even when the plume is not resident.
    std::vector<PlumeFireLight> Lamps;
    if (Frame.Enabled)
    {
        for (const PlumeSourceFireLight& Light : Projection.Lights())
        {
            PlumeFireLight Published;
            Published.Position = { Light.Position.X, Light.Position.Y, Light.Position.Z };
            Published.Radius = Light.Radius;
            Published.Radiance = { Light.Radiance.X, Light.Radiance.Y, Light.Radiance.Z };
            Lamps.push_back(Published);
        }
    }
    Lattice.AssignFireLights(Lamps);

    // 📝 The aggregate: the sources' lights weighted by luminance, for a single soft light in the
    //    direct pass.
    float Total = 0.0f;
    for (const PlumeFireLight& Lamp : Lamps)
        Total += Lamp.Radiance[0];
    Aggregate.Intensity = Total;
    if (Total > 0.0f)
    {
        Vec3 Weighted = {};
        for (const PlumeFireLight& Lamp : Lamps)
            Weighted = Weighted + Vec3{ Lamp.Position[0], Lamp.Position[1], Lamp.Position[2] } * Lamp.Radiance[0];
        Weighted = Weighted * (1.0f / Total);
        Aggregate.Position = { Weighted.X, Weighted.Y, Weighted.Z };
    }
}

void SmokeSequence::CommitStep() noexcept
{
    Lattice.CommitStep();
}

std::vector<std::string> SmokeSequence::QueryReadout() const
{
    std::ostringstream Second;
    Second << "sources " << Projection.Count() << " · steps " << Lattice.StepsTaken() << " · "
           << (Lattice.IsResident() ? "resident" : "idle");
    std::ostringstream First;
    First << "Slate 3c987f3 plume · " << Lattice.Configuration().Name << " · " << Lattice.QueryDescription();
    return { First.str(), Second.str() };
}

} // namespace Frontier
