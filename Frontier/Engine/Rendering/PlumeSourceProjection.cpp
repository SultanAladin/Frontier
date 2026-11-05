//==========================================================================================================================================
//                                                    PLUMESOURCEPROJECTION.CPP
//==========================================================================================================================================
//  Vehicle telemetry projected into smoke, soot and fire sources, fire lights and ember emitters.
//  Ported 1:1 from Experimental/VehicleHmiPrototype/Source/PlumeSourceProjection.js.
//==========================================================================================================================================

#include "PlumeSpace.h"
#include "PlumeSourceProjection.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                           ROTORS
//--------------------------------------------------------------------------------------------------------------------------------------

Rotor ComposeRotor(Rotor Left, Rotor Right) noexcept
{
    return Rotor{ Left.W * Right.X + Left.X * Right.W + Left.Y * Right.Z - Left.Z * Right.Y,
                  Left.W * Right.Y - Left.X * Right.Z + Left.Y * Right.W + Left.Z * Right.X,
                  Left.W * Right.Z + Left.X * Right.Y - Left.Y * Right.X + Left.Z * Right.W,
                  Left.W * Right.W - Left.X * Right.X - Left.Y * Right.Y - Left.Z * Right.Z };
}

Rotor AxisRotor(Vec3 Axis, float Turn) noexcept
{
    const float Half = 0.5f * Turn;
    const float Sine = std::sin(Half);
    return Rotor{ Axis.X * Sine, Axis.Y * Sine, Axis.Z * Sine, std::cos(Half) };
}

Vec3 RotateByRotor(Rotor Turn, Vec3 Vector) noexcept
{
    const Vec3 Axis = { Turn.X, Turn.Y, Turn.Z };
    const Vec3 Twice = Cross(Axis, Vector) * 2.0f;
    return (Vector + Twice * Turn.W) + Cross(Axis, Twice);
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                       LIFECYCLE
//--------------------------------------------------------------------------------------------------------------------------------------

PlumeSourceProjection::PlumeSourceProjection()
    : Table(size_t(PlumeSourceCapacity) * PlumeSourceStride, 0.0f)
{
}

void PlumeSourceProjection::Reset() noexcept
{
    for (SmokeOnsetTracker& Tracker : Trackers)
        Tracker = SmokeOnsetTracker{};
    PopSeconds = {};
    PopStrength = {};
    Bursts.clear();
}

double PlumeSourceProjection::ProjectUniform() noexcept
{
    Seed ^= Seed << 13u;
    Seed ^= Seed >> 17u;
    Seed ^= Seed << 5u;
    return double(Seed) / 4294967296.0;
}

void PlumeSourceProjection::QueueBurst(const PlumeBurst& Burst)
{
    PlumeBurst Pending = Burst;
    Pending.RemainingSeconds = Burst.Seconds;
    Bursts.push_back(Pending);
    if (Bursts.size() > 4u)
        Bursts.erase(Bursts.begin());
}

// 📝 Bare rims have no tyre rubber to vaporise, and a tyre off the ground has nothing to slide against.
bool PlumeSourceProjection::AdvanceOnset(uint32_t Wheel, float Power, float SlipSpeed, bool Contact, float Seconds) noexcept
{
    if (Wheel >= Trackers.size())
        return false;
    SmokeOnsetTracker& Tracker = Trackers[Wheel];

    if (!Contact || !(Seconds > 0.0f))
    {
        Tracker = SmokeOnsetTracker{};
        return false;
    }

    const bool AboveEntry = Power > SmokeSpecification::SourcePowerOnset && SlipSpeed >= SmokeSpecification::MinimumSlipSpeed;
    const bool AboveRelease = Power > SmokeSpecification::SourcePowerOnset * SmokeSpecification::SourceReleaseRatio
                           && SlipSpeed >= SmokeSpecification::MinimumSlipSpeed;

    if (AboveEntry)
    {
        Tracker.RequestSeconds += Seconds;
        Tracker.QuietSeconds = 0.0f;
        if (Tracker.RequestSeconds + 1.0e-6f >= SmokeSpecification::SourceOnsetSeconds)
            Tracker.Active = true;
    }
    else if (Tracker.Active && AboveRelease)
    {
        Tracker.QuietSeconds = 0.0f;
    }
    else
    {
        Tracker.RequestSeconds = 0.0f;
        Tracker.QuietSeconds += Seconds;
        if (Tracker.QuietSeconds >= SmokeSpecification::SourceReleaseSeconds)
            Tracker.Active = false;
    }
    return Tracker.Active;
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                        PROJECTION
//--------------------------------------------------------------------------------------------------------------------------------------

std::span<const float> PlumeSourceProjection::Records() const noexcept
{
    return std::span<const float>(Table.data(), size_t(Written) * PlumeSourceStride);
}

std::span<float> PlumeSourceProjection::Records() noexcept
{
    return std::span<float>(Table.data(), size_t(Written) * PlumeSourceStride);
}

PlumeSourceEntry PlumeSourceProjection::Source(uint32_t Index) const noexcept
{
    PlumeSourceEntry Entry;
    if (Index >= Written)
        return Entry;
    const float* Record = Table.data() + size_t(Index) * PlumeSourceStride;
    Entry.Position = { Record[0], Record[1], Record[2] };
    Entry.Radius = Record[3];
    Entry.Velocity = { Record[4], Record[5], Record[6] };
    Entry.Smoke = Record[8];
    Entry.Heat = Record[9];
    Entry.Flame = Record[10];
    Entry.Soot = Record[11];
    return Entry;
}

bool PlumeSourceProjection::AppendSource(Vec3 Position, float Radius, Vec3 Velocity, float Smoke, float Heat, float Flame, float Soot)
{
    if (Written >= PlumeSourceCapacity)
        return false;
    if (Smoke <= 0.0f && Heat <= 0.0f && Flame <= 0.0f && Soot <= 0.0f)
        return false;

    float* Record = Table.data() + size_t(Written) * PlumeSourceStride;
    Record[0] = Position.X; Record[1] = Position.Y; Record[2] = Position.Z; Record[3] = Radius;
    Record[4] = Velocity.X; Record[5] = Velocity.Y; Record[6] = Velocity.Z; Record[7] = 0.0f;
    Record[8] = Smoke; Record[9] = Heat; Record[10] = Flame; Record[11] = Soot;
    ++Written;
    return true;
}

uint32_t PlumeSourceProjection::Project(const PlumeTelemetry& Telemetry, float Seconds, const PlumeProjectionOptions& Options)
{
    const float Rate = Options.EmissionRate;
    const float SmokeYield = Options.SmokeYield * Rate;
    const float HeatYield = Options.HeatYield * Rate;
    const float SootYield = Options.SootYield * Rate;

    Written = 0u;
    Smoking = 0u;
    Flames.clear();
    EmberEmitters.clear();
    Elapsed += Seconds;

    // ① tyres — ResolveWheelSmoke(), thrown along the tread's slip, shed again from the arch
    for (uint32_t Index = 0u; Index < Telemetry.Wheels.size(); ++Index)
    {
        const PlumeWheelReading& Wheel = Telemetry.Wheels[Index];
        if (Index >= Trackers.size())
            break;

        const PlumeWheelThermal& Thermal = Wheel.Thermal;
        const bool Burning = Thermal.Ignited && Thermal.CombustionPower > 0.0f;
        const float Power = Wheel.SlidingPower;
        const bool Sliding = AdvanceOnset(Index, Power, Wheel.SlipSpeed, Wheel.InContact, Seconds);
        if (!Sliding && !Burning)
            continue;

        ++Smoking;

        // 📝 A burning tyre off the ground keeps burning at its hub; otherwise the source is the patch.
        const Vec3 Anchor = (Burning && !Wheel.InContact) ? Wheel.HubPosition : Wheel.ContactPoint;
        const Vec3 Position = Anchor + Vec3{ 0.0f, 0.0f, 0.04f };

        const float Strength = Sliding
            ? std::min(4.0f, std::max(0.0f, (Power - SmokeSpecification::SourcePowerOnset)
                                          / std::max(1.0f, SmokeSpecification::SourcePowerFull - SmokeSpecification::SourcePowerOnset)))
            : 0.0f;
        const float Warm = std::min(1.0f, std::max(0.0f, (Thermal.SurfaceTemperature - SmokeWarmBase) / SmokeWarmSpan));
        const float Smoke = SmokeSpecification::DensityRate * Strength * (0.35f + 0.65f * Warm) * (Wheel.Burst ? 0.25f : 1.0f);

        // 📝 The tread at the patch moves at (surface speed − ground speed) along the wheel's heading;
        //    smoke leaves with a share of that, rearward of a tyre spinning forward, plus a little lift.
        const float SurfaceSlip = Wheel.SurfaceSpeed - Dot(Wheel.ContactVelocity, Wheel.Longitudinal);
        const float Fling = std::max(-SmokeFlingCeiling, std::min(SmokeFlingCeiling, SmokeFlingShare * SurfaceSlip));
        const Vec3 Thrown = Wheel.ContactVelocity * SmokeSpecification::CarrierVelocityShare + Wheel.Longitudinal * (-Fling);
        AppendSource(Position, SmokeSpecification::PatchRadius * 1.4f, Thrown + Vec3{ 0.0f, 0.0f, 0.35f },
                     Smoke * SmokeYield, SmokeSpecification::SmokeHeatRate * Strength * HeatYield, 0.0f, 0.0f);

        if (Sliding && std::fabs(Fling) > 1.0f && Wheel.Radius > 0.0f)
        {
            const Vec3 Back = Wheel.Longitudinal * (Fling > 0.0f ? -1.0f : 1.0f);
            const Vec3 Arch = (Wheel.HubPosition + Back * (Wheel.Radius * 0.85f)) + Vec3{ 0.0f, 0.0f, Wheel.Radius * 0.35f };
            AppendSource(Arch, SmokeArchRadius, Back * (0.45f * std::fabs(Fling)) + Vec3{ 0.0f, 0.0f, 1.0f },
                         SmokeArchShare * Smoke * SmokeYield, 0.0f, 0.0f, 0.0f);
        }

        // 📝 Burning rubber burns where air reaches it — the outer sidewall and the back of the tread —
        //    and climbs past the arch. Two flame seats per tyre, both in open air beside the body, so
        //    the lattice's velocity cells there are fluid and the column rises instead of pooling under
        //    the arch. Together they shed Frontier's 12/s of incandescence and black soot in place of
        //    the white rubber smoke.
        if (Burning)
        {
            const Vec3 Outward = Vec3{ -Wheel.Longitudinal.Y, Wheel.Longitudinal.X, 0.0f } * ((Index & 1u) != 0u ? -1.0f : 1.0f);
            const float Radius = Wheel.Radius > 0.0f ? Wheel.Radius : 0.5f;
            const Vec3 Sidewall = (Wheel.HubPosition + Outward * SmokeFlameSidewallReach) + Vec3{ 0.0f, 0.0f, -0.3f * Radius };
            const Vec3 Trailing = (Wheel.HubPosition + Wheel.Longitudinal * (-(Radius + 0.12f))) + Vec3{ 0.0f, 0.0f, -0.15f * Radius };
            const Vec3 Carried = Wheel.ContactVelocity * SmokeSpecification::CarrierVelocityShare;
            const float Heat = Thermal.CombustionPower * SmokeSpecification::FireHeatPowerScale * HeatYield;

            AppendSource(Sidewall, SmokeFlameSeatRadius,
                         Carried + Outward * 0.4f + Vec3{ 0.0f, 0.0f, SmokeFlameRise },
                         0.6f * SmokeYield, 0.6f * Heat, 7.0f * Rate, 8.0f * SootYield);
            AppendSource(Trailing, SmokeTrailingSeatRadius, Carried + Vec3{ 0.0f, 0.0f, SmokeFlameRise },
                         0.4f * SmokeYield, 0.4f * Heat, 5.0f * Rate, 6.0f * SootYield);

            const float Flicker = 0.78f + 0.22f * std::sin(Elapsed * 17.0f + float(Index) * 2.1f)
                                              * std::sin(Elapsed * 6.3f + float(Index));
            const float Intensity = Thermal.CombustionPower * SmokeFireLightGain * Options.FireLight * Flicker;
            PlumeSourceFireLight Light;
            Light.Position = Sidewall + Vec3{ 0.0f, 0.0f, 0.45f };
            Light.Radius = 0.4f;
            Light.Radiance = Vec3{ SmokeFireColour[0], SmokeFireColour[1], SmokeFireColour[2] } * Intensity;
            Flames.push_back(Light);

            PlumeEmberEmitter Ember;
            Ember.Position = Sidewall;
            Ember.Velocity = Wheel.ContactVelocity * 0.3f + Vec3{ 0.0f, 0.0f, 2.2f };
            Ember.Rate = 55.0f * Options.Embers;
            Ember.Temperature = 1350.0f;
            Ember.Spread = 0.9f;
            Ember.Lift = 2.5f;
            Ember.Gravity = 0.15f;
            Ember.Life = 1.6f;
            EmberEmitters.push_back(Ember);
        }
    }

    // ② exhaust afterfire: while the window is open each tip fires 35–90 ms bursts with random gaps
    Popping = false;
    for (uint32_t Tip = 0u; Tip < Telemetry.Exhausts.size() && Tip < 2u; ++Tip)
    {
        PopSeconds[Tip] = std::max(0.0f, PopSeconds[Tip] - Seconds);
        if (Options.Afterfire && Telemetry.AfterfireSeconds > 0.0f && PopSeconds[Tip] <= 0.0f
            && ProjectUniform() < double(Seconds) * 14.0)
        {
            PopSeconds[Tip] = 0.035f + 0.055f * float(ProjectUniform());
            PopStrength[Tip] = 0.6f + 0.8f * float(ProjectUniform());
        }
        if (PopSeconds[Tip] <= 0.0f)
            continue;

        Popping = true;
        const PlumeExhaustReading& Exhaust = Telemetry.Exhausts[Tip];
        const float Strength = PopStrength[Tip];
        const float Speed = 8.0f + 6.0f * Strength;
        const Vec3 Velocity = Telemetry.Velocity * 0.7f + Exhaust.Direction * Speed;
        const Vec3 Nozzle = Exhaust.Position + Exhaust.Direction * 0.15f;
        AppendSource(Nozzle, SmokeExhaustRadius, Velocity,
                     1.0f * Strength * SmokeYield, 260.0f * Strength * HeatYield,
                     40.0f * Strength * Rate, 1.2f * Strength * SootYield);

        PlumeSourceFireLight Light;
        Light.Position = Nozzle + Exhaust.Direction * 0.3f;
        Light.Radius = 0.25f;
        Light.Radiance = Vec3{ 1.0f, 0.55f, 0.2f } * (9.0f * Strength * Options.FireLight);
        Flames.push_back(Light);

        PlumeEmberEmitter Ember;
        Ember.Position = Nozzle;
        Ember.Velocity = Velocity;
        Ember.Rate = 260.0f * Strength * Options.Embers;
        Ember.Temperature = 1700.0f;
        Ember.Spread = 1.6f;
        Ember.Lift = 0.4f;
        Ember.Gravity = 0.6f;
        Ember.Life = 0.35f;
        EmberEmitters.push_back(Ember);
    }

    // ③ transient bursts until their time is spent
    for (PlumeBurst& Burst : Bursts)
    {
        const float Share = std::min(1.0f, Burst.RemainingSeconds / std::max(Burst.Seconds, 1.0e-3f));
        AppendSource(Burst.Position, Burst.Radius, Burst.Velocity,
                     Burst.Smoke * Share * Rate, Burst.Heat * Share * HeatYield,
                     Burst.Flame * Share * Rate, Burst.Soot * Share * SootYield);
    }
    for (PlumeBurst& Burst : Bursts)
        Burst.RemainingSeconds -= Seconds;
    std::erase_if(Bursts, [](const PlumeBurst& Burst) { return Burst.RemainingSeconds <= 0.0f; });

    // 📝 Brightest first: the light table keeps the strongest four.
    std::stable_sort(Flames.begin(), Flames.end(), [](const PlumeSourceFireLight& Left, const PlumeSourceFireLight& Right)
    {
        return (Right.Radiance.X + Right.Radiance.Y) < (Left.Radiance.X + Left.Radiance.Y);
    });
    return Written;
}

bool PlumeSourceProjection::ResolveBounds(Vec3& Minimum, Vec3& Maximum) const noexcept
{
    if (Written == 0u)
        return false;

    Minimum = Vec3{ 1.0e30f, 1.0e30f, 1.0e30f };
    Maximum = Vec3{ -1.0e30f, -1.0e30f, -1.0e30f };
    for (uint32_t Index = 0u; Index < Written; ++Index)
    {
        const float* Record = Table.data() + size_t(Index) * PlumeSourceStride;
        const float Reach = std::max(Record[3], 0.3f) * 3.5f;
        const Vec3 Position = { Record[0], Record[1], Record[2] };
        const Vec3 Low = Position - Vec3{ Reach, Reach, Reach };
        const Vec3 High = Position + Vec3{ Reach, Reach, Reach };
        Minimum = { std::min(Minimum.X, Low.X), std::min(Minimum.Y, Low.Y), std::min(Minimum.Z, Low.Z) };
        Maximum = { std::max(Maximum.X, High.X), std::max(Maximum.Y, High.Y), std::max(Maximum.Z, High.Z) };
    }
    return true;
}

} // namespace Frontier
