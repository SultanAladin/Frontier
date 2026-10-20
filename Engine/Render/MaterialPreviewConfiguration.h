/*==========================================================================================================================================
                                                   MATERIALPREVIEWCONFIGURATION.H
==========================================================================================================================================*/
// 🧩 Bound interactive material inspection independently of authored materials and atlas thumbnail recipes.

#pragma once
#include <cmath>
#include <array>
#include <cstdint>

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
    // 📝 The viewport's own width/height ratio. The projection is a matched pair, so without it a non-square panel
    //    stretches the subject. Zero means "square/unset" and reproduces the previous matched-scale behaviour exactly.
    float                      AspectRatio = 0;
    bool operator==(const MaterialPreviewConfiguration &) const = default;
};
inline bool ValidateMaterialPreviewConfiguration(const MaterialPreviewConfiguration &Configuration)
{
    return uint32_t(Configuration.Shape) <= 3 && uint32_t(Configuration.Environment) <= 3 &&
        std::isfinite(Configuration.Zoom) && Configuration.Zoom >= .5f && Configuration.Zoom <= 2 &&
        std::isfinite(Configuration.Exposure) && Configuration.Exposure >= .25f && Configuration.Exposure <= 4 &&
        std::isfinite(Configuration.PanX) && std::abs(Configuration.PanX) <= 2 &&
        std::isfinite(Configuration.PanY) && std::abs(Configuration.PanY) <= 2 &&
        // 📝 Yaw is a full turn, not a half turn. The interactive canvas wraps it with remainder(.., 2pi), so bounding it
        //    at pi made the validator reject the very values the panel produces and froze rotation mid-drag.
        std::isfinite(Configuration.Yaw) && std::abs(Configuration.Yaw) <= 6.2831854f &&
        std::isfinite(Configuration.Pitch) && std::abs(Configuration.Pitch) <= 1.45f &&
        std::isfinite(Configuration.AspectRatio) && Configuration.AspectRatio >= 0 && Configuration.AspectRatio <= 64;
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
    // 📝 Aspect describes the panel, not the subject, so a view reset must leave it untouched.
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
    // 📝 Square panels keep the historical matched X/Y scale. A wider panel narrows the horizontal span by exactly its
    //    aspect, so the subject keeps its true proportions and fills the viewport instead of being clipped or stretched.
    //    Vertical scale is deliberately untouched: the vertical field of view is the stable one.
    const float HorizontalAspect = Configuration.AspectRatio > 0 ? Configuration.AspectRatio : 1.f;
    for (size_t Column = 0; Column < 4; ++Column)
    {
        Projection.ClipFromSurface[Column * 4] *= Configuration.Zoom / HorizontalAspect;
        Projection.ClipFromSurface[Column * 4 + 1] *= -Configuration.Zoom;
        Projection.ClipFromSurface[Column * 4 + 2] *= -.4f;
    }
    Projection.ClipFromSurface[12] += Configuration.PanX / HorizontalAspect;
    Projection.ClipFromSurface[13] += Configuration.PanY;
    Projection.ClipFromSurface[14] += .5f;
    Projection.ViewFromSurface[12] += Configuration.PanX / Configuration.Zoom;
    Projection.ViewFromSurface[13] -= Configuration.PanY / Configuration.Zoom;
    Projection.ViewFromSurface[14] -= 3;
    return Projection;
}

}
