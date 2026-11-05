/*==========================================================================================================================================
                                                         SPARK COLLISION
                                     ------------------------------------------------------------------
                     Swept point collision against the analytic course, static world boxes
                                        and convex debris bodies.
==========================================================================================================================================*/

// 🧩 Sparks against the world: the portable (CPU) form of the demo's SparkCollision.js, 1:1.
//
// 📝 A spark is a point with a 3 mm skin swept along its step. Unlike an endpoint test, a swept test
//    cannot skip a thin wall: the course is walked in 2 cm horizontal intervals and the first
//    crossing bisected ten times, so a ramp or a raised kerb is caught even when both ends of the step
//    lie above the flat pad. Convex boxes and debris bodies are swept against their plane sets.

#pragma once

#include "Frontier/Engine/Foundation/Math.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Frontier
{

// 📝 The swept point's skin [m]. A spark is a glowing chip, but it is not a mathematical point: three
//    millimetres is what keeps it from sinking into the surface it is skipping over.
inline constexpr float SparkSkin = 0.003f;

// 📝 Where and when a swept point met something, and how far to lift it clear.
struct SparkContact
{
    float Time = 0.0f;         // [-] fraction of the step at which it met
    Vec3 Normal = { 0.0f, 0.0f, 1.0f };   // [-] unit, outward from the surface
    float Push = 0.0f;         // [m] how far along the normal to lift the point clear
};

// 📝 A static axis-aligned box. Planes, when given, is the box's six outward planes (four floats each:
//    normal xyz, then the plane offset); otherwise BoxPlanes() builds them from the bounds.
struct SparkBox
{
    Vec3 Minimum = {};
    Vec3 Maximum = {};
    std::vector<float> Planes;
};

// 📝 A convex debris body: a bounding sphere for the broadphase and a plane set for the sweep.
struct SparkConvexBody
{
    Vec3 Position = {};
    float Radius = 0.0f;
    std::vector<float> WorldPlanes;
};

// 📝 The six outward planes of an axis-aligned box, four floats each (normal xyz, offset).
[[nodiscard]] std::array<float, 24> BoxPlanes(Vec3 Minimum, Vec3 Maximum) noexcept;

// 📝 Sweeps a point from From to To through a convex hull given as Planes (four floats each: normal
//    xyz, then the plane offset). Returns the earliest contact, or nothing when the step is clear.
[[nodiscard]] std::optional<SparkContact> SweepConvex(Vec3 From, Vec3 To, std::span<const float> Planes) noexcept;

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                        THE COURSE
//--------------------------------------------------------------------------------------------------------------------------------------

// 📝 The surface sparks skip off. The demo's is the analytic course (a flat pad, a ramp and a kerb);
//    a host with its own terrain supplies it here. The default is the flat pad.
class SparkCourseSurface
{
  public:
    virtual ~SparkCourseSurface() = default;

    // 📝 The course's height [m] at (X, Y).
    [[nodiscard]] virtual float Height(float X, float Y) const noexcept = 0;

    // 📝 The course's unit normal there, by central differences over ±0.15 m.
    [[nodiscard]] Vec3 Normal(float X, float Y) const noexcept;
};

// 📝 A flat course at height zero: the pad.
class FlatSparkCourse final : public SparkCourseSurface
{
  public:
    [[nodiscard]] float Height(float, float) const noexcept override { return 0.0f; }
};

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                          SWEEP
//--------------------------------------------------------------------------------------------------------------------------------------

// 📝 Sweeps a point from From to To through every box, every convex body and the course, and returns
//    the earliest contact. Course may be null, in which case only the boxes and bodies are swept.
[[nodiscard]] std::optional<SparkContact> SweepSpark(Vec3 From,
                                                      Vec3 To,
                                                      std::span<const SparkBox> Boxes,
                                                      std::span<const SparkConvexBody> Bodies,
                                                      const SparkCourseSurface* Course) noexcept;

// 📐 A spark that meets a surface keeps 55 % of its tangential speed and leaves with 28 % of the
//    approach speed reflected — it skips, it does not bounce.
[[nodiscard]] Vec3 BounceSpark(Vec3 Velocity, Vec3 Normal) noexcept;

} // namespace Frontier
