//==========================================================================================================================================
//                                                        SPARKCOLLISION.CPP
//==========================================================================================================================================
//  Swept point collision against the analytic course, static world boxes and convex debris bodies.
//  Ported 1:1 from Experimental/VehicleHmiPrototype/Source/SparkCollision.js.
//==========================================================================================================================================

#include "SparkCollision.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

[[nodiscard]] float PlaneOffset(std::span<const float> Planes, size_t Index, Vec3 Point) noexcept
{
    const Vec3 Normal = { Planes[Index], Planes[Index + 1u], Planes[Index + 2u] };
    return Dot(Normal, Point) - Planes[Index + 3u];
}

} // namespace

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                          PLANES
//--------------------------------------------------------------------------------------------------------------------------------------

std::array<float, 24> BoxPlanes(Vec3 Minimum, Vec3 Maximum) noexcept
{
    return { 1.0f,  0.0f,  0.0f,  Maximum.X,
            -1.0f,  0.0f,  0.0f, -Minimum.X,
             0.0f,  1.0f,  0.0f,  Maximum.Y,
             0.0f, -1.0f,  0.0f, -Minimum.Y,
             0.0f,  0.0f,  1.0f,  Maximum.Z,
             0.0f,  0.0f, -1.0f, -Minimum.Z };
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                       CONVEX SWEEP
//--------------------------------------------------------------------------------------------------------------------------------------

std::optional<SparkContact> SweepConvex(Vec3 From, Vec3 To, std::span<const float> Planes) noexcept
{
    float Enter = 0.0f;
    float Exit = 1.0f;
    float Nearest = -1.0e30f;
    bool Inside = true;
    bool HaveNormal = false;
    bool HaveExitNormal = false;
    Vec3 Normal = {};
    Vec3 ExitNormal = {};

    for (size_t Offset = 0u; Offset + 4u <= Planes.size(); Offset += 4u)
    {
        const Vec3 Plane = { Planes[Offset], Planes[Offset + 1u], Planes[Offset + 2u] };
        const float A = PlaneOffset(Planes, Offset, From) - SparkSkin;
        const float B = PlaneOffset(Planes, Offset, To) - SparkSkin;

        if (A > 0.0f)
            Inside = false;
        if (A > Nearest)
        {
            Nearest = A;
            ExitNormal = Plane;
            HaveExitNormal = true;
        }
        if (A > 0.0f && B >= A)
            return std::nullopt;                        // moving away from, or along, the outside
        if (A <= 0.0f && B <= 0.0f)
            continue;                                   // entirely inside this plane

        const float T = A / (A - B);
        if (A > B)
        {
            if (T >= Enter)
            {
                Enter = T;
                Normal = Plane;
                HaveNormal = true;
            }
        }
        else
        {
            Exit = std::min(Exit, T);
        }
        if (Enter > Exit)
            return std::nullopt;
    }

    SparkContact Contact;
    if (Inside)
    {
        if (!HaveExitNormal)
            return std::nullopt;
        Contact.Time = 0.0f;
        Contact.Normal = Normalise(ExitNormal);
        Contact.Push = -Nearest + SparkSkin;
        return Contact;
    }
    if (!HaveNormal || Enter > 1.0f)
        return std::nullopt;
    Contact.Time = Enter;
    Contact.Normal = Normalise(Normal);
    Contact.Push = SparkSkin;
    return Contact;
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                          COURSE
//--------------------------------------------------------------------------------------------------------------------------------------

Vec3 SparkCourseSurface::Normal(float X, float Y) const noexcept
{
    constexpr float Step = 0.15f;
    const float SlopeX = (Height(X + Step, Y) - Height(X - Step, Y)) / (2.0f * Step);
    const float SlopeY = (Height(X, Y + Step) - Height(X, Y - Step)) / (2.0f * Step);
    const float Magnitude = std::sqrt(SlopeX * SlopeX + SlopeY * SlopeY + 1.0f);
    return { -SlopeX / Magnitude, -SlopeY / Magnitude, 1.0f / Magnitude };
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                           SWEEP
//--------------------------------------------------------------------------------------------------------------------------------------

std::optional<SparkContact> SweepSpark(Vec3 From,
                                        Vec3 To,
                                        std::span<const SparkBox> Boxes,
                                        std::span<const SparkConvexBody> Bodies,
                                        const SparkCourseSurface* Course) noexcept
{
    std::optional<SparkContact> Hit;
    const auto Take = [&Hit](const std::optional<SparkContact>& Candidate)
    {
        if (Candidate && (!Hit || Candidate->Time < Hit->Time))
            Hit = Candidate;
    };
    const auto Overlaps = [From, To](Vec3 Minimum, Vec3 Maximum)
    {
        for (uint32_t Axis = 0u; Axis < 3u; ++Axis)
        {
            const float Low = Axis == 0u ? Minimum.X : (Axis == 1u ? Minimum.Y : Minimum.Z);
            const float High = Axis == 0u ? Maximum.X : (Axis == 1u ? Maximum.Y : Maximum.Z);
            const float SegmentLow = Axis == 0u ? From.X : (Axis == 1u ? From.Y : From.Z);
            const float SegmentHigh = Axis == 0u ? To.X : (Axis == 1u ? To.Y : To.Z);
            if (!(std::max(SegmentLow, SegmentHigh) + SparkSkin >= Low && std::min(SegmentLow, SegmentHigh) - SparkSkin <= High))
                return false;
        }
        return true;
    };

    std::array<float, 24> Scratch = {};
    for (const SparkBox& Box : Boxes)
    {
        if (!Overlaps(Box.Minimum, Box.Maximum))
            continue;
        if (Box.Planes.empty())
            Scratch = BoxPlanes(Box.Minimum, Box.Maximum);
        Take(SweepConvex(From, To, Box.Planes.empty() ? std::span<const float>(Scratch) : std::span<const float>(Box.Planes)));
    }

    for (const SparkConvexBody& Body : Bodies)
    {
        const Vec3 Minimum = Body.Position - Vec3{ Body.Radius, Body.Radius, Body.Radius };
        const Vec3 Maximum = Body.Position + Vec3{ Body.Radius, Body.Radius, Body.Radius };
        if (!Overlaps(Minimum, Maximum))
            continue;
        Take(SweepConvex(From, To, Body.WorldPlanes));
    }

    if (Course == nullptr)
        return Hit;

    // 📝 Resolve the course in ≤ 2 cm horizontal intervals, then bisect the first crossing. This also
    //    catches the ramp and raised kerbs when both endpoints lie above the flat pad.
    const float Span = std::sqrt((To.X - From.X) * (To.X - From.X) + (To.Y - From.Y) * (To.Y - From.Y));
    const uint32_t Count = std::max(1u, uint32_t(std::ceil(Span / 0.02f)));
    const auto At = [From, To](float T)
    {
        return From + (To - From) * T;
    };
    const auto Clearance = [Course](Vec3 Point)
    {
        return Point.Z - Course->Height(Point.X, Point.Y) - SparkSkin;
    };

    for (uint32_t Index = 0u; Index <= Count; ++Index)
    {
        const float T = float(Index) / float(Count);
        if (Hit && T > Hit->Time)
            break;
        if (Clearance(At(T)) > 0.0f)
            continue;

        float Low = std::max(0.0f, float(Index - 1u) / float(Count));
        float High = T;
        for (uint32_t Step = 0u; Step < 10u; ++Step)
        {
            const float Middle = 0.5f * (Low + High);
            if (Clearance(At(Middle)) > 0.0f)
                Low = Middle;
            else
                High = Middle;
        }

        SparkContact Contact;
        Contact.Time = High;
        const Vec3 Surface = At(High);
        Contact.Normal = Course->Normal(Surface.X, Surface.Y);
        Contact.Push = std::max(SparkSkin, -Clearance(Surface) + SparkSkin);
        Take(Contact);
        break;
    }
    return Hit;
}

Vec3 BounceSpark(Vec3 Velocity, Vec3 Normal) noexcept
{
    const float Approach = std::min(0.0f, Dot(Velocity, Normal));
    const Vec3 Tangential = Velocity - Normal * Approach;
    return Tangential * 0.55f - Normal * (Approach * 0.28f);
}

} // namespace Frontier
