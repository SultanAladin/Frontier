/*==========================================================================================================================================
                                                               VEHICLEMATH.H
==========================================================================================================================================*/
// 🧩 Right-handed +X forward, +Y left, +Z up vector, quaternion, and piecewise lookup algebra for physical dynamics.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace Frontier::Vehicle
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCALAR CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr float PiConstant              = 3.14159265358979323846f;   // [rad] half-turn angle
inline constexpr float TwoPiConstant           = 6.28318530717958647692f;   // [rad] full-turn angle
inline constexpr float DegreesToRadians        = 0.01745329251994329577f;   // [rad/deg] angular conversion
inline constexpr float RadiansToDegrees        = 57.2957795130823208768f;   // [deg/rad] angular conversion
inline constexpr float RpmToRadiansPerSecond   = 0.10471975511965977f;      // [(rad/s)/rpm] shaft conversion
inline constexpr float RadiansPerSecondToRpm   = 9.549296585513721f;        // [rpm/(rad/s)] shaft conversion
inline constexpr float StandardGravity         = 9.80665f;                  // [m/s^2] standard sea-level gravity
inline constexpr float AtmosphericPressurePa   = 101325.0f;                 // [Pa] standard sea-level pressure
inline constexpr float DryAirGasConstant       = 287.058f;                  // [J/(kg*K)] specific gas constant of dry air
inline constexpr float StefanBoltzmannConstant = 5.670374419e-8f;           // [W/(m^2*K^4)] radiative constant

[[nodiscard]] inline float ClampScalar(float Value, float Minimum, float Maximum) noexcept
{
    return Value < Minimum ? Minimum : (Value > Maximum ? Maximum : Value);
}

[[nodiscard]] inline float SignScalar(float Value) noexcept
{
    return Value > 0.0f ? 1.0f : (Value < 0.0f ? -1.0f : 0.0f);
}

[[nodiscard]] inline float LerpScalar(float First, float Second, float Blend) noexcept
{
    return First + (Second - First) * Blend;
}

[[nodiscard]] inline float WrapAngleRadians(float Angle) noexcept
{
    while (Angle > PiConstant) Angle -= TwoPiConstant;
    while (Angle < -PiConstant) Angle += TwoPiConstant;
    return Angle;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SPATIAL VECTOR
//------------------------------------------------------------------------------------------------------------------------

struct Vec3
{
    float x = 0.0f;   // [m] forward (+X) component
    float y = 0.0f;   // [m] leftward (+Y) component
    float z = 0.0f;   // [m] upward (+Z) component

    constexpr Vec3() noexcept = default;
    constexpr Vec3(float X, float Y, float Z) noexcept : x(X), y(Y), z(Z) {}

    [[nodiscard]] constexpr Vec3 operator+(const Vec3& Other) const noexcept { return {x + Other.x, y + Other.y, z + Other.z}; }
    [[nodiscard]] constexpr Vec3 operator-(const Vec3& Other) const noexcept { return {x - Other.x, y - Other.y, z - Other.z}; }
    [[nodiscard]] constexpr Vec3 operator-() const noexcept { return {-x, -y, -z}; }
    [[nodiscard]] constexpr Vec3 operator*(float Scale) const noexcept { return {x * Scale, y * Scale, z * Scale}; }
    [[nodiscard]] constexpr Vec3 operator/(float Scale) const noexcept
    {
        const float Inverse = 1.0f / Scale;
        return {x * Inverse, y * Inverse, z * Inverse};
    }

    constexpr Vec3& operator+=(const Vec3& Other) noexcept
    {
        x += Other.x;
        y += Other.y;
        z += Other.z;
        return *this;
    }

    constexpr Vec3& operator-=(const Vec3& Other) noexcept
    {
        x -= Other.x;
        y -= Other.y;
        z -= Other.z;
        return *this;
    }

    constexpr Vec3& operator*=(float Scale) noexcept
    {
        x *= Scale;
        y *= Scale;
        z *= Scale;
        return *this;
    }

    [[nodiscard]] constexpr float LengthSquared() const noexcept { return x * x + y * y + z * z; }
    [[nodiscard]] float           Length() const noexcept { return std::sqrt(LengthSquared()); }

    [[nodiscard]] Vec3 Normalized() const noexcept
    {
        const float Magnitude = Length();
        return Magnitude > 1.0e-12f ? (*this * (1.0f / Magnitude)) : Vec3{0.0f, 0.0f, 0.0f};
    }

    [[nodiscard]] bool Finite() const noexcept { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
};

[[nodiscard]] constexpr Vec3 operator*(float Scale, const Vec3& Vector) noexcept
{
    return Vector * Scale;
}

[[nodiscard]] constexpr float Dot(const Vec3& Left, const Vec3& Right) noexcept
{
    return Left.x * Right.x + Left.y * Right.y + Left.z * Right.z;
}

[[nodiscard]] constexpr Vec3 Cross(const Vec3& Left, const Vec3& Right) noexcept
{
    return {
        Left.y * Right.z - Left.z * Right.y,
        Left.z * Right.x - Left.x * Right.z,
        Left.x * Right.y - Left.y * Right.x};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      QUATERNION
//------------------------------------------------------------------------------------------------------------------------

struct Quat
{
    float x = 0.0f;   // [-] vector X component
    float y = 0.0f;   // [-] vector Y component
    float z = 0.0f;   // [-] vector Z component
    float w = 1.0f;   // [-] scalar W component

    constexpr Quat() noexcept = default;
    constexpr Quat(float X, float Y, float Z, float W) noexcept : x(X), y(Y), z(Z), w(W) {}

    [[nodiscard]] static constexpr Quat Identity() noexcept { return {0.0f, 0.0f, 0.0f, 1.0f}; }

    [[nodiscard]] static Quat AxisAngle(const Vec3& Axis, float AngleRadians) noexcept
    {
        const Vec3  Unit = Axis.Normalized();
        const float Half = 0.5f * AngleRadians;
        const float Sine = std::sin(Half);
        return {Unit.x * Sine, Unit.y * Sine, Unit.z * Sine, std::cos(Half)};
    }

    [[nodiscard]] constexpr Quat Conjugate() const noexcept { return {-x, -y, -z, w}; }

    [[nodiscard]] Quat Normalized() const noexcept
    {
        const float Norm = std::sqrt(x * x + y * y + z * z + w * w);
        return Norm > 1.0e-12f ? Quat{x / Norm, y / Norm, z / Norm, w / Norm} : Identity();
    }

    [[nodiscard]] constexpr Quat operator*(const Quat& Other) const noexcept
    {
        return {
            w * Other.x + x * Other.w + y * Other.z - z * Other.y,
            w * Other.y - x * Other.z + y * Other.w + z * Other.x,
            w * Other.z + x * Other.y - y * Other.x + z * Other.w,
            w * Other.w - x * Other.x - y * Other.y - z * Other.z};
    }

    [[nodiscard]] constexpr Vec3 Rotate(const Vec3& Vector) const noexcept
    {
        const Vec3 Imaginary{x, y, z};
        const Vec3 TwiceCross = Cross(Imaginary, Vector) * 2.0f;
        return Vector + TwiceCross * w + Cross(Imaginary, TwiceCross);
    }

    [[nodiscard]] constexpr Vec3 InverseRotate(const Vec3& Vector) const noexcept
    {
        return Conjugate().Rotate(Vector);
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   PIECEWISE CURVE
//------------------------------------------------------------------------------------------------------------------------

struct PiecewiseCurve
{
    std::vector<std::pair<float, float>> Knots{};   // [-] sorted (abscissa, ordinate) pairs

    PiecewiseCurve() = default;
    PiecewiseCurve(std::initializer_list<std::pair<float, float>> Entries) : Knots(Entries) {}

    [[nodiscard]] float Sample(float DomainValue) const noexcept
    {
        if (Knots.empty()) return 0.0f;
        if (DomainValue <= Knots.front().first) return Knots.front().second;
        if (DomainValue >= Knots.back().first) return Knots.back().second;

        for (size_t Index = 1; Index < Knots.size(); ++Index)
        {
            if (DomainValue <= Knots[Index].first)
            {
                const float Span  = std::max(Knots[Index].first - Knots[Index - 1].first, 1.0e-8f);
                const float Ratio = (DomainValue - Knots[Index - 1].first) / Span;
                return LerpScalar(Knots[Index - 1].second, Knots[Index].second, Ratio);
            }
        }
        return Knots.back().second;
    }
};

// 📝 Ground surface query: given a world sample point, writes the ground surface point and unit outward normal beneath/near it.
using GroundQuery = std::function<bool(const Vec3& SampleWorld, Vec3& SurfaceWorld, Vec3& SurfaceNormal)>;

} // namespace Frontier::Vehicle
