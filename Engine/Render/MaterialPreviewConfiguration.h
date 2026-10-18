/*==========================================================================================================================================
                                                   MATERIALPREVIEWCONFIGURATION.H
==========================================================================================================================================*/
// 🧩 Bound interactive material inspection independently of authored materials and atlas thumbnail recipes.

#pragma once
#include <cmath>
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
    bool operator==(const MaterialPreviewConfiguration &) const = default;
};
inline bool ValidateMaterialPreviewConfiguration(const MaterialPreviewConfiguration &Configuration)
{
    return uint32_t(Configuration.Shape) <= 3 && uint32_t(Configuration.Environment) <= 3 &&
        std::isfinite(Configuration.Zoom) && Configuration.Zoom >= .5f && Configuration.Zoom <= 2 &&
        std::isfinite(Configuration.Exposure) && Configuration.Exposure >= .25f && Configuration.Exposure <= 4;
}
}
