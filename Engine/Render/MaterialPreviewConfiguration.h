/*==========================================================================================================================================
                                                   MATERIALPREVIEWCONFIGURATION.H
==========================================================================================================================================*/
// 🧩 Bound interactive material inspection independently of authored materials and atlas thumbnail recipes.

#pragma once
#include <cmath>
#include <array>
#include <cstdint>
#include <stdexcept>

namespace Frontier
{
enum class MaterialPreviewShape : uint32_t
{
    ShaderBall,
    Sphere,
    Plane,
    Cube
};
enum class MaterialPreviewEnvironment : uint32_t
{
    StudioSoftbox,
    WarmCourtyard,
    CoolRim,
    Overcast
};
struct MaterialPreviewConfiguration
{
    MaterialPreviewShape       Shape       = MaterialPreviewShape::ShaderBall;
    MaterialPreviewEnvironment Environment = MaterialPreviewEnvironment::StudioSoftbox;
    float                      Zoom        = 1;
    float                      Exposure    = 1;
    float                      PanX        = 0;
    float                      PanY        = 0;
    float                      Yaw         = 0;
    float                      Pitch       = 0;
    float                      ViewportAspect = 1;
    bool operator==(const MaterialPreviewConfiguration &) const = default;
};
inline bool ValidateMaterialPreviewConfiguration(const MaterialPreviewConfiguration &Configuration)
{
    return uint32_t(Configuration.Shape) <= 3 && uint32_t(Configuration.Environment) <= 3 &&
        std::isfinite(Configuration.Zoom) && Configuration.Zoom >= .5f && Configuration.Zoom <= 2 &&
        std::isfinite(Configuration.Exposure) && Configuration.Exposure >= .25f && Configuration.Exposure <= 4 &&
        std::isfinite(Configuration.PanX) && std::abs(Configuration.PanX) <= 2 &&
        std::isfinite(Configuration.PanY) && std::abs(Configuration.PanY) <= 2 &&
        std::isfinite(Configuration.Yaw) && std::abs(Configuration.Yaw) <= 3.141593f &&
        std::isfinite(Configuration.Pitch) && std::abs(Configuration.Pitch) <= 1.45f &&
        std::isfinite(Configuration.ViewportAspect) && Configuration.ViewportAspect >= .1f && Configuration.ViewportAspect <= 10;
}

struct MaterialPreviewProjection
{
    std::array<float,16> ViewFromSurface;
    std::array<float,16> ClipFromSurface;
};

inline void ResetMaterialPreviewView(MaterialPreviewConfiguration &Configuration)
{
    Configuration.Zoom = 1;
    Configuration.PanX = Configuration.PanY = Configuration.Yaw = Configuration.Pitch = 0;
}

inline MaterialPreviewProjection ConstructMaterialPreviewProjection(const MaterialPreviewConfiguration &Configuration)
{
    const float θ = Configuration.Yaw, φ = Configuration.Pitch;
    const float Cosθ = std::cos(θ), Sinθ = std::sin(θ), Cosφ = std::cos(φ), Sinφ = std::sin(φ);
    const float Translation = 16.f * uint32_t(Configuration.Shape);
    MaterialPreviewProjection Projection;
    Projection.ViewFromSurface = {Cosθ, Sinφ * Sinθ, -Cosφ * Sinθ, 0,
        0, Cosφ, Sinφ, 0, Sinθ, -Sinφ * Cosθ, Cosφ * Cosθ, 0,
        -Cosθ * Translation, -Sinφ * Sinθ * Translation, Cosφ * Sinθ * Translation, 1};
    Projection.ClipFromSurface = Projection.ViewFromSurface;
    for (size_t Column = 0; Column < 4; ++Column)
    {
        Projection.ClipFromSurface[Column * 4] *= Configuration.Zoom;
        Projection.ClipFromSurface[Column * 4 + 1] *= -Configuration.Zoom;
        Projection.ClipFromSurface[Column * 4 + 2] *= -.4f;
    }
    for (size_t Column = 0; Column < 4; ++Column) Projection.ClipFromSurface[Column * 4] /= Configuration.ViewportAspect;
    Projection.ClipFromSurface[12] += Configuration.PanX;
    Projection.ClipFromSurface[13] += Configuration.PanY;
    Projection.ClipFromSurface[14] += .5f;
    Projection.ViewFromSurface[12] += Configuration.PanX / Configuration.Zoom;
    Projection.ViewFromSurface[13] -= Configuration.PanY / Configuration.Zoom;
    Projection.ViewFromSurface[14] -= 3;
    return Projection;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     DESTINATION / DISPLAY CONVERGENCE
//------------------------------------------------------------------------------------------------------------------------

class MaterialPreviewMotion
{
public:
    void ConfigureDestination(const MaterialPreviewConfiguration &Configuration)
    {
        if (!ValidateMaterialPreviewConfiguration(Configuration)) throw std::invalid_argument("Invalid preview navigation destination.");
        Destination = Configuration;
    }
    void Reset(const MaterialPreviewConfiguration &Configuration)
    {
        ConfigureDestination(Configuration); Displayed = Destination;
    }
    bool Advance(float ElapsedSeconds, bool ReducedMotion = false)
    {
        if (!std::isfinite(ElapsedSeconds) || ElapsedSeconds < 0) return false;
        const auto Previous = Displayed;
        const float Fraction = ReducedMotion ? 1 : -std::expm1(-ElapsedSeconds / .12f);
        const auto Converge = [Fraction](float Position, float Target)
        {
            const float Result = Position + (Target - Position) * Fraction;
            return std::abs(Result - Target) < .00001f ? Target : Result;
        };
        Displayed.PanX = Converge(Displayed.PanX, Destination.PanX);
        Displayed.PanY = Converge(Displayed.PanY, Destination.PanY);
        Displayed.Pitch = Converge(Displayed.Pitch, Destination.Pitch);
        const float Difference = std::remainder(Destination.Yaw - Displayed.Yaw, 6.28318530718f);
        Displayed.Yaw = std::remainder(Displayed.Yaw + Difference * Fraction, 6.28318530718f);
        if (std::abs(std::remainder(Destination.Yaw - Displayed.Yaw, 6.28318530718f)) < .00001f) Displayed.Yaw = Destination.Yaw;
        Displayed.Zoom = std::exp(Converge(std::log(Displayed.Zoom), std::log(Destination.Zoom)));
        Displayed.Shape = Destination.Shape; Displayed.Environment = Destination.Environment;
        Displayed.Exposure = Destination.Exposure; Displayed.ViewportAspect = Destination.ViewportAspect;
        return Previous != Displayed;
    }
    [[nodiscard]] const MaterialPreviewConfiguration &QueryDisplayed() const noexcept { return Displayed; }
private:
    MaterialPreviewConfiguration Destination, Displayed;
};

}
