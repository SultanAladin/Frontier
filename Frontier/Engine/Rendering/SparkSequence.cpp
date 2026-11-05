//==========================================================================================================================================
//                                                        SPARKSEQUENCE.CPP
//==========================================================================================================================================
//  Sparks: thrown from scrapes, integrated on the host, drawn as additive streaks.
//  Ported 1:1 from Experimental/VehicleHmiPrototype/Source/SparkSequence.js.
//==========================================================================================================================================

#include "SparkSequence.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{

// 📝 Neutral plus the variation's share of the spread: at Variation 0 every spark is the neutral
//    value, at 1 it takes the full randomised spread.
[[nodiscard]] float Mix(float Neutral, float Varied, float Variation) noexcept
{
    return Neutral + (Varied - Neutral) * Variation;
}

[[nodiscard]] float HashCell(float Value) noexcept
{
    return std::floor(Value * 0.5f);
}

} // namespace

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                         RADIANCE
//--------------------------------------------------------------------------------------------------------------------------------------

std::array<float, 3> ResolveSparkRadiance(float Kelvin) noexcept
{
    const float Hecto = std::clamp(Kelvin, 1000.0f, 6600.0f) / 100.0f;
    const float Green = std::clamp((99.4708025861f * std::log(Hecto) - 161.1195681661f) / 255.0f, 0.0f, 1.0f);
    const float Blue = Hecto <= 19.0f
        ? 0.0f
        : std::clamp((138.5177312231f * std::log(std::max(Hecto - 10.0f, 1.0f)) - 305.0447927307f) / 255.0f, 0.0f, 1.0f);
    const float Ratio = Kelvin / 1500.0f;
    const float Brightness = 8.0f * Ratio * Ratio * Ratio * Ratio
                           * std::clamp((Kelvin - SparkVisibleKelvin) / 500.0f, 0.0f, 1.0f);
    return { 1.0f * Brightness, std::pow(Green, 2.2f) * Brightness, std::pow(Blue, 2.2f) * Brightness };
}

float ResolveSparkGain(const SparkParticle& Spark) noexcept
{
    const float Fade = std::clamp((Spark.LifeSeconds - Spark.Age) / 0.10f, 0.0f, 1.0f);
    const float Ripple = 1.0f - Spark.Flicker * (0.5f + 0.5f * std::sin(Spark.Age * Spark.Frequency * 6.283185307f + Spark.Phase));
    return Spark.Energy * Fade * Ripple;
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                        LIFECYCLE
//--------------------------------------------------------------------------------------------------------------------------------------

SparkSequence::SparkSequence()
    : InstanceTable(size_t(SparkCapacity) * SparkInstanceFloats, 0.0f)
{
    Pool.reserve(SparkCapacity);
}

void SparkSequence::Clear() noexcept
{
    Pool.clear();
    PulseKeys.clear();
    Pulses.clear();
    Elapsed = 0.0f;
    Count = 0u;
}

float SparkSequence::Uniform() noexcept
{
    Seed = Seed * 1664525u + 1013904223u;
    return float(Seed) / 4294967296.0f;
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                          EMISSION
//--------------------------------------------------------------------------------------------------------------------------------------

void SparkSequence::Emit(std::span<const SparkScrape> Scrapes, float Seconds)
{
    if (!Controls.Enabled || !(Seconds > 0.0f))
        return;

    Elapsed += std::min(Seconds, 0.1f);
    for (size_t Index = 0u; Index < PulseKeys.size();)
    {
        if (Elapsed - Pulses[Index].Last > 0.5f)
        {
            PulseKeys.erase(PulseKeys.begin() + std::ptrdiff_t(Index));
            Pulses.erase(Pulses.begin() + std::ptrdiff_t(Index));
        }
        else
        {
            ++Index;
        }
    }

    const float Variation = std::clamp(Controls.Variation, 0.0f, 1.0f);
    LastScrapes = uint32_t(Scrapes.size());

    for (const SparkScrape& Scrape : Scrapes)
    {
        // 📝 Correlated 40–130 ms contact packets, not an identical stream every frame.
        uint64_t Key;
        if (Scrape.Rim && Scrape.Wheel >= 0)
            Key = uint64_t(0x776865656Cu) + uint64_t(Scrape.Wheel);        // "whee" + the wheel index
        else
            Key = (uint64_t(uint32_t(int32_t(HashCell(Scrape.Point.X)) + 4096)) << 42u)
                ^ (uint64_t(uint32_t(int32_t(HashCell(Scrape.Point.Y)) + 4096)) << 21u)
                ^ uint64_t(uint32_t(int32_t(HashCell(Scrape.Point.Z)) + 4096));

        size_t Slot = PulseKeys.size();
        for (size_t Index = 0u; Index < PulseKeys.size(); ++Index)
            if (PulseKeys[Index] == Key)
            {
                Slot = Index;
                break;
            }

        if (Slot == PulseKeys.size() || Elapsed >= Pulses[Slot].Next)
        {
            Pulse Fresh;
            Fresh.Next = Elapsed + 0.04f + Uniform() * 0.09f;
            Fresh.Last = Elapsed;
            Fresh.Power = Uniform() < 0.65f ? 0.25f + Uniform() * 0.70f : 1.2f + Uniform() * 1.4f;
            if (Slot == PulseKeys.size())
            {
                if (PulseKeys.size() >= 128u)
                {
                    PulseKeys.erase(PulseKeys.begin());
                    Pulses.erase(Pulses.begin());
                }
                PulseKeys.push_back(Key);
                Pulses.push_back(Fresh);
                Slot = PulseKeys.size() - 1u;
            }
            else
            {
                Pulses[Slot] = Fresh;
            }
        }
        Pulses[Slot].Last = Elapsed;

        const float Expected = SparksPerStrength * Controls.Rate * Scrape.Strength * Seconds * Mix(1.0f, Pulses[Slot].Power, Variation);
        const float Whole = std::floor(Expected);
        uint32_t Count = uint32_t(Whole) + (Uniform() < Expected - Whole ? 1u : 0u);
        Count = std::min<uint32_t>(Count, uint32_t(SparkMaximumPerScrape));

        const Vec3 Normal = Scrape.Normal;
        const Vec3 Slide = Scrape.Velocity;
        const float SlideSpeed = Length(Slide);

        for (uint32_t Spark = 0u; Spark < Count && Pool.size() < SparkCapacity; ++Spark)
        {
            // Mostly fine fast filings, some medium streaks, a few heavier orange chips.
            const float Roll = Uniform();
            const SparkKind Kind = Roll < 0.72f ? SparkKind::Filing : (Roll < 0.94f ? SparkKind::Streak : SparkKind::Chip);
            const bool Heavy = Kind == SparkKind::Chip;
            const float Size = Mix(1.0f,
                                   Heavy ? 1.3f + 0.7f * Uniform()
                                         : (Kind == SparkKind::Streak ? 0.7f + 0.6f * Uniform() : 0.25f + 0.5f * Uniform()),
                                   Variation);
            const float SpeedShare = Mix(1.0f, Heavy ? 0.35f + 0.40f * Uniform() : 0.65f + 0.55f * Uniform(), Variation);
            const Vec3 Spread = { Uniform() * 2.0f - 1.0f, Uniform() * 2.0f - 1.0f, Uniform() * 2.0f - 1.0f };

            Vec3 Origin;
            Vec3 Velocity;
            if (Scrape.Rim)
            {
                // 📐 Detached α behind the patch in the sense the rim turns (most right behind it): from
                //    the patch at R((1 − cos α)·N − σ·sin α·F), moving at Slip + S(1 − cos α)·F +
                //    |S|·sin α·N, σ = sign S; it keeps 75–100 % of that, scattered in a narrow cone and
                //    bounced a little off the road.
                const float Sense = Scrape.SpinSpeed < 0.0f ? -1.0f : 1.0f;
                const float Detach = 0.02f + 0.45f * Uniform() * Uniform();
                const float Cosine = std::cos(Detach);
                const float Sine = std::sin(Detach);
                Origin = Scrape.Point + Normal * (Scrape.Radius * (1.0f - Cosine)) - Scrape.Forward * (Scrape.Radius * Sense * Sine);
                const Vec3 Metal = Slide + Scrape.Forward * (Scrape.SpinSpeed * (1.0f - Cosine))
                                 + Normal * (std::fabs(Scrape.SpinSpeed) * Sine);
                const float Speed = Length(Metal);
                const float Share = 0.75f + 0.25f * Uniform();
                const float Scatter = 0.12f * Speed + 0.4f;
                const float Bounce = 0.3f + 1.2f * Uniform();
                Velocity = Metal * Share + Spread * Scatter + Normal * Bounce;
            }
            else
            {
                // 📝 Along the slide at 30–75 % of its speed, fanned ±35° and kicked 1–4 m/s off the
                //    surface.
                const float Share = 0.3f + 0.45f * Uniform();
                const float Fan = 0.6f * std::max(2.0f, SlideSpeed * 0.35f);
                const float Kick = 1.0f + 3.0f * Uniform();
                constexpr float Jitter = 0.04f;
                Origin = { Scrape.Point.X + (Uniform() - 0.5f) * Jitter,
                           Scrape.Point.Y + (Uniform() - 0.5f) * Jitter,
                           Scrape.Point.Z + 0.01f + Uniform() * 0.02f };
                Velocity = Slide * Share + Spread * Fan + Normal * Kick;
            }
            Velocity = Velocity * SpeedShare;

            SparkParticle Particle;
            Particle.Position = Origin;
            Particle.Prior = Origin;
            Particle.Velocity = Velocity;
            Particle.Kind = Kind;
            Particle.Kelvin = Mix(2450.0f, Heavy ? 1800.0f + 800.0f * Uniform() : 2100.0f + 900.0f * Uniform(), Variation);
            Particle.Drag = Mix(SparkAirDrag, Heavy ? 0.65f + 1.2f * Uniform() : 2.0f + 2.5f * Uniform(), Variation);
            Particle.Cooling = Mix(1.0f,
                                   Heavy ? 1.2f + 0.5f * Uniform()
                                         : (Kind == SparkKind::Streak ? 0.85f + 0.4f * Uniform() : 0.55f + 0.35f * Uniform()),
                                   Variation);
            Particle.LifeSeconds = Mix(2.5f,
                                       Heavy ? 0.9f + Uniform()
                                             : (Kind == SparkKind::Streak ? 0.45f + 0.75f * Uniform() : 0.20f + 0.4f * Uniform()),
                                       Variation);
            Particle.BounceScale = Mix(1.0f, 0.70f + 0.30f * Uniform(), Variation);
            Particle.Energy = Mix(1.0f, 0.35f + 0.85f * Uniform(), Variation);
            Particle.Flicker = Mix(0.0f, 0.04f + 0.12f * Uniform(), Variation);
            Particle.Frequency = 10.0f + 18.0f * Uniform();
            Particle.Phase = Uniform() * 6.283185307f;
            Particle.PixelWidth = Mix(1.0f,
                                      Heavy ? 0.9f + 0.1f * Uniform()
                                            : (Kind == SparkKind::Streak ? 0.7f + 0.25f * Uniform() : 0.4f + 0.35f * Uniform()),
                                      Variation);
            Particle.Shutter = Mix(1.0f, Heavy ? 0.55f + 0.45f * Uniform() : 0.18f + 0.45f * Uniform(), Variation);
            Particle.Age = 0.0f;
            Particle.Size = Size * Controls.Size;
            Particle.Impacts = 0u;
            // 📝 New sparks integrate only their birth-frame remainder, with the same swept collision
            //    as older sparks.
            Particle.Born = true;
            Particle.BornSeconds = Uniform() * std::min(Seconds, 0.1f);
            Pool.push_back(Particle);
            ++TotalEmitted;
        }
    }
    // 📝 The new sparks are live at once; Advance() integrates them and compacts the pool.
    Count = uint32_t(Pool.size());
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                         ADVANCE
//--------------------------------------------------------------------------------------------------------------------------------------

std::span<const SparkParticle> SparkSequence::Particles() const noexcept
{
    return std::span<const SparkParticle>(Pool.data(), Count);
}

std::span<const float> SparkSequence::Instances() const noexcept
{
    return std::span<const float>(InstanceTable.data(), size_t(Count) * SparkInstanceFloats);
}

SparkInstance SparkSequence::Instance(uint32_t Index) const noexcept
{
    SparkInstance Result;
    if (Index >= Count)
        return Result;
    const float* Record = InstanceTable.data() + size_t(Index) * SparkInstanceFloats;
    Result.Head = { Record[0], Record[1], Record[2] };
    Result.Width = Record[3];
    Result.Tail = { Record[4], Record[5], Record[6] };
    Result.PixelWidth = Record[7];
    Result.Radiance = { Record[8], Record[9], Record[10] };
    Result.Shutter = Record[11];
    return Result;
}

void SparkSequence::Advance(float Seconds,
                             std::span<const SparkBox> Boxes,
                             std::span<const SparkConvexBody> Bodies,
                             const SparkCourseSurface* Course)
{
    if (!(Seconds > 0.0f))
        return;
    if (!Controls.Enabled)
    {
        Clear();
        return;
    }

    // 📝 Broadphase once per frame, not once per particle or substep: everything that could be reached
    //    this frame, from the swept bounds of the whole pool. The filtered lists outlive the block that
    //    fills them, because the substep loop sweeps through spans onto them.
    std::vector<SparkBox> NearBoxes;
    std::vector<SparkConvexBody> NearBodies;
    if (!Pool.empty())
    {
        Vec3 Minimum = { 1.0e30f, 1.0e30f, 1.0e30f };
        Vec3 Maximum = { -1.0e30f, -1.0e30f, -1.0e30f };
        for (const SparkParticle& Spark : Pool)
            for (uint32_t Axis = 0u; Axis < 3u; ++Axis)
            {
                const float Component = Axis == 0u ? Spark.Velocity.X : (Axis == 1u ? Spark.Velocity.Y : Spark.Velocity.Z);
                const float Origin = Axis == 0u ? Spark.Position.X : (Axis == 1u ? Spark.Position.Y : Spark.Position.Z);
                const float Travel = std::fabs(Component) * std::min(Seconds, 0.1f) + 0.2f;
                if (Axis == 0u)
                {
                    Minimum.X = std::min(Minimum.X, Origin - Travel);
                    Maximum.X = std::max(Maximum.X, Origin + Travel);
                }
                else if (Axis == 1u)
                {
                    Minimum.Y = std::min(Minimum.Y, Origin - Travel);
                    Maximum.Y = std::max(Maximum.Y, Origin + Travel);
                }
                else
                {
                    Minimum.Z = std::min(Minimum.Z, Origin - Travel);
                    Maximum.Z = std::max(Maximum.Z, Origin + Travel);
                }
            }

        NearBoxes.reserve(Boxes.size());
        for (const SparkBox& Box : Boxes)
            if (Maximum.X >= Box.Minimum.X && Minimum.X <= Box.Maximum.X
                && Maximum.Y >= Box.Minimum.Y && Minimum.Y <= Box.Maximum.Y
                && Maximum.Z >= Box.Minimum.Z && Minimum.Z <= Box.Maximum.Z)
                NearBoxes.push_back(Box);

        NearBodies.reserve(Bodies.size());
        for (const SparkConvexBody& Body : Bodies)
        {
            const Vec3 Low = Body.Position - Vec3{ Body.Radius, Body.Radius, Body.Radius };
            const Vec3 High = Body.Position + Vec3{ Body.Radius, Body.Radius, Body.Radius };
            if (Maximum.X >= Low.X && Minimum.X <= High.X
                && Maximum.Y >= Low.Y && Minimum.Y <= High.Y
                && Maximum.Z >= Low.Z && Minimum.Z <= High.Z)
                NearBodies.push_back(Body);
        }

        Boxes = std::span<const SparkBox>(NearBoxes);
        Bodies = std::span<const SparkConvexBody>(NearBodies);
    }

    uint32_t Survivors = 0u;
    for (SparkParticle& Spark : Pool)
    {
        Spark.Prior = Spark.Position;
        const float Duration = Spark.Born ? Spark.BornSeconds : std::min(Seconds, 0.1f);
        Spark.Born = false;
        const uint32_t Steps = std::max(1u, uint32_t(std::ceil(Duration * 120.0f)));
        const float Dt = Duration / float(Steps);

        for (uint32_t Step = 0u; Step < Steps; ++Step)
        {
            Spark.Age += Dt;
            const float Damping = std::exp(-Spark.Drag * Dt);
            Spark.Velocity = { Spark.Velocity.X * Damping,
                               Spark.Velocity.Y * Damping,
                               Spark.Velocity.Z * Damping - SparkGravity * Dt };

            const Vec3 From = Spark.Position;
            const Vec3 To = From + Spark.Velocity * Dt;
            const std::optional<SparkContact> Hit = SweepSpark(From, To, Boxes, Bodies, Course);
            if (Hit)
            {
                Spark.Position = (From + (To - From) * Hit->Time) + Hit->Normal * Hit->Push;
                Spark.Velocity = BounceSpark(Spark.Velocity, Hit->Normal) * Spark.BounceScale;
                ++Spark.Impacts;
                Spark.Kelvin = SparkAmbientKelvin + (Spark.Kelvin - SparkAmbientKelvin) * 0.92f;
                // 📝 Do not draw a streak through the wall or the ground after a bounce.
                Spark.Prior = Spark.Position;
            }
            else
            {
                Spark.Position = To;
            }
            const float Rate = SparkCoolingSeconds * Spark.Cooling * std::max(0.5f, Spark.Size);
            Spark.Kelvin = SparkAmbientKelvin + (Spark.Kelvin - SparkAmbientKelvin) * std::exp(-Dt / Rate);
        }

        if (Spark.Kelvin > SparkVisibleKelvin && Spark.Age < Spark.LifeSeconds
            && (Spark.Impacts < 8u || Length(Spark.Velocity) > 0.6f))
        {
            if (Survivors != 0u)
                Pool[Survivors] = Spark;
            ++Survivors;
        }
    }
    Pool.resize(Survivors);
    Count = Survivors;

    if (Count == 0u)
        return;

    // 📝 Instances: the streak from where the spark was a frame ago (the raster projects it with last
    //    frame's camera, so the streak is the motion the camera saw) to where it is.
    const float Brightness = Controls.Brightness;
    for (uint32_t Index = 0u; Index < Count; ++Index)
    {
        const SparkParticle& Spark = Pool[Index];
        float* Record = InstanceTable.data() + size_t(Index) * SparkInstanceFloats;
        const std::array<float, 3> Radiance = ResolveSparkRadiance(Spark.Kelvin);
        const float Gain = ResolveSparkGain(Spark) * Brightness;
        Record[0] = Spark.Position.X; Record[1] = Spark.Position.Y; Record[2] = Spark.Position.Z;
        Record[3] = SparkWidth * Spark.Size;
        Record[4] = Spark.Prior.X; Record[5] = Spark.Prior.Y; Record[6] = Spark.Prior.Z;
        Record[7] = Spark.PixelWidth;
        Record[8] = Radiance[0] * Gain;
        Record[9] = Radiance[1] * Gain;
        Record[10] = Radiance[2] * Gain;
        Record[11] = Spark.Shutter;
    }
}

std::string SparkSequence::QueryReadout() const
{
    char Text[160];
    std::snprintf(Text, sizeof(Text), "sparks live: %u · emitted %u · scrapes this frame %u", Count, TotalEmitted, LastScrapes);
    return std::string(Text);
}

} // namespace Frontier
