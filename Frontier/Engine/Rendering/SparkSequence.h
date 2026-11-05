/*==========================================================================================================================================
                                                          SPARK SEQUENCE
                                     ------------------------------------------------------------------
                    Sparks: hot metal ground off where a rim or the body slides on the road
                     or a wall — thrown, cooling, bouncing, drawn as additive streaks.
==========================================================================================================================================*/

// 🧩 Sparks: the portable (CPU) form of the demo's SparkSequence.js, 1:1.
//
// 📝 The vehicle solver reports each frame's scrapes (VehicleSolver.RecordScrape): the strongest per
//    contact — a hull patch dragged along the asphalt, a rim pinched through a flat or bare once the
//    rubber has gone — with the contact point, the sliding velocity of the metal over the surface, the
//    surface normal and a strength. Each scrape throws irregular packets of sparks in proportion, with
//    class-dependent temperatures:
//
//    | Source | Where a spark leaves                          | With what velocity                                    |
//    |--------|-----------------------------------------------|-------------------------------------------------------|
//    | hull   | the scraping point                            | 30–75 % of the slide, fanned, kicked off the surface  |
//    | rim    | the rim's edge a little (α ≤ 0.47 rad) behind | that point's own: Slip + S(1 − cos α)·F + |S| sin α·N |
//    |        | the patch, where the metal leaves the road    | (S = Spin·r) — up off the trailing edge, so the trail |
//    |        |                                               | streams back from a rolling wheel                     |
//
//    In flight a spark falls, feels air drag, cools radiatively (its glow follows the blackbody
//    temperature) and skips off the road, losing most of its speed, until it is too cool to see.
//
//    A few hundred particles: they are integrated here on the host and drawn after the temporal
//    antialiasing as thin additive streaks (2.2 mm across, with bounded subpixel widths) from where
//    the camera saw them a frame ago to where it sees them now (SparkRaster.slang), depth-tested
//    against the scene — thin, bright and fast-moving, they would otherwise be averaged away by the
//    history.

#pragma once

#include "Frontier/Engine/Foundation/Math.h"
#include "Frontier/Engine/Rendering/SparkCollision.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Frontier
{

inline constexpr uint32_t SparkCapacity = 2048;             // [-]   particles at once
inline constexpr uint32_t SparkInstanceFloats = 12;         // [f32] head xyz + width, tail xyz + pixel width, radiance rgb + shutter
inline constexpr float SparkGravity = 9.81f;                // [m/s²]
inline constexpr float SparkAirDrag = 1.6f;                 // [1/s]
inline constexpr float SparkCoolingSeconds = 0.42f;         // [s]   e-folding of a spark's excess temperature
inline constexpr float SparkVisibleKelvin = 950.0f;         // [K]   below this a spark is gone
inline constexpr float SparkAmbientKelvin = 293.0f;         // [K]   what a spark cools toward
inline constexpr float SparksPerStrength = 700.0f;          // [1/s] per unit scrape strength
inline constexpr float SparkWidth = 0.0022f;                // [m]   streak width at Size 1
inline constexpr float SparkMaximumPerScrape = 96.0f;       // [-]   packets are capped, however hard the scrape

// 📝 The three kinds: mostly fine fast filings, some medium streaks, a few heavier orange chips.
enum class SparkKind : uint8_t
{
    Filing = 0,
    Streak = 1,
    Chip = 2
};

// 📝 The editor's spark controls.
struct SparkSettings
{
    bool Enabled = true;
    float Rate = 1.0f;             // [×]   sparks per unit strength
    float Brightness = 1.0f;       // [×]
    float Size = 1.0f;             // [×]   streak width
    float Variation = 1.0f;        // [-]   0 gives every spark the neutral value, 1 the full spread
};

// 📝 One scrape: metal sliding on a surface. Rim, when set, is a wheel rim rather than the hull.
struct SparkScrape
{
    Vec3 Point = {};               // [m]   world contact point
    Vec3 Velocity = {};            // [m/s] the metal's slide over the surface
    Vec3 Normal = { 0.0f, 0.0f, 1.0f };   // [-] unit, outward from the surface
    float Strength = 0.0f;         // [-]
    int32_t Wheel = -1;            // [-]   index, when this is a rim
    bool Rim = false;
    Vec3 Forward = { 1.0f, 0.0f, 0.0f };  // [-] the rim's forward axis
    float SpinSpeed = 0.0f;        // [m/s] rim surface speed (spin × radius), signed
    float Radius = 0.35f;          // [m]
};

// 📝 One particle.
struct SparkParticle
{
    Vec3 Position = {};
    Vec3 Prior = {};               // [m] where the camera saw it a frame ago
    Vec3 Velocity = {};            // [m/s]
    SparkKind Kind = SparkKind::Filing;
    float Kelvin = 2450.0f;        // [K]
    float Drag = SparkAirDrag;     // [1/s]
    float Cooling = 1.0f;          // [×]
    float LifeSeconds = 2.5f;      // [s]
    float BounceScale = 1.0f;      // [-]
    float Energy = 1.0f;           // [×]
    float Flicker = 0.0f;          // [-]
    float Frequency = 10.0f;       // [Hz]
    float Phase = 0.0f;            // [rad]
    float PixelWidth = 1.0f;       // [×]
    float Shutter = 1.0f;          // [-]   fraction of the frame's motion the streak spans
    float Age = 0.0f;              // [s]
    float Size = 1.0f;             // [×]   width multiplier, Settings.Size folded in
    uint32_t Impacts = 0u;         // [-]
    bool Born = false;             // a spark born late in the frame integrates only the remainder
    float BornSeconds = 0.0f;      // [s]
};

// 📝 One instance, decoded: the streak's head and tail, its width and its radiance.
struct SparkInstance
{
    Vec3 Head = {};
    float Width = 0.0f;            // [m]
    Vec3 Tail = {};
    float PixelWidth = 1.0f;       // [×]
    Vec3 Radiance = {};
    float Shutter = 1.0f;          // [-]
};

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                        RADIANCE
//--------------------------------------------------------------------------------------------------------------------------------------

// 📐 Blackbody colour (Helland's fit, linear) × Stefan–Boltzmann brightness referred to 1500 K, in the
//    scene's radiance units (sunlit white ≈ 5): a fresh spark is a few hundred times brighter than the
//    road it flies over.
[[nodiscard]] std::array<float, 3> ResolveSparkRadiance(float Kelvin) noexcept;

// 📝 Stable per-particle modulation: the end-of-life fade and the flicker. Never re-rolled in the
//    render loop — shared with the pooled GI light proxies so visual and lighting energy agree.
[[nodiscard]] float ResolveSparkGain(const SparkParticle& Spark) noexcept;

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                        SEQUENCE
//--------------------------------------------------------------------------------------------------------------------------------------

class SparkSequence
{
  public:
    SparkSequence();

    // 📝 Throws sparks from this frame's scrapes. Packets are correlated 40–130 ms bursts per source,
    //    not an identical stream every frame, so a scrape reads as grinding metal rather than a hose.
    void Emit(std::span<const SparkScrape> Scrapes, float Seconds);

    // 📝 Ballistic with linear drag, radiative cooling toward ambient, skips off the course. Boxes and
    //    Bodies are swept (SparkCollision); Course may be null for an airborne-only advance.
    void Advance(float Seconds,
                 std::span<const SparkBox> Boxes = {},
                 std::span<const SparkConvexBody> Bodies = {},
                 const SparkCourseSurface* Course = nullptr);

    // 📝 Forgets every spark.
    void Clear() noexcept;

    [[nodiscard]] SparkSettings& Settings() noexcept { return Controls; }
    [[nodiscard]] const SparkSettings& Settings() const noexcept { return Controls; }

    [[nodiscard]] std::span<const SparkParticle> Particles() const noexcept;
    [[nodiscard]] uint32_t Live() const noexcept { return Count; }

    // 📝 The instance table, SparkInstanceFloats floats per live spark, in the layout SparkRaster.slang
    //    reads: head xyz + width, tail xyz + pixel width, radiance rgb + shutter fraction.
    [[nodiscard]] std::span<const float> Instances() const noexcept;
    [[nodiscard]] SparkInstance Instance(uint32_t Index) const noexcept;

    [[nodiscard]] uint32_t Emitted() const noexcept { return TotalEmitted; }
    [[nodiscard]] uint32_t LastScrapeCount() const noexcept { return LastScrapes; }
    [[nodiscard]] float Clock() const noexcept { return Elapsed; }

    // 📝 The number of correlated packets currently tracked (one per scraping source, capped at 128).
    [[nodiscard]] uint32_t PulseCount() const noexcept { return uint32_t(PulseKeys.size()); }

    [[nodiscard]] std::string QueryReadout() const;

  private:
    float Uniform() noexcept;

    SparkSettings Controls = {};
    std::vector<SparkParticle> Pool;
    std::vector<float> InstanceTable;
    uint32_t Count = 0u;
    uint32_t TotalEmitted = 0u;
    uint32_t LastScrapes = 0u;
    float Elapsed = 0.0f;
    uint32_t Seed = 0x5EEDu;

    // 📝 The correlated packets: one per scraping source, keyed by the wheel index for a rim or by the
    //    half-metre cell of the contact point for the hull.
    struct Pulse
    {
        float Next = 0.0f;
        float Last = 0.0f;
        float Power = 1.0f;
    };
    std::vector<uint64_t> PulseKeys;
    std::vector<Pulse> Pulses;
};

} // namespace Frontier
