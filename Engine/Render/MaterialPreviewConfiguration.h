/*==========================================================================================================================================
                                                   MATERIALPREVIEWCONFIGURATION.H
==========================================================================================================================================*/
// 🧩 Bound interactive material inspection independently of authored materials and atlas thumbnail recipes.

#pragma once
#include <algorithm>
#include <cmath>
#include <array>
#include <cstdint>
#include <numbers>

namespace Frontier
{
enum class MaterialPreviewShape : uint32_t
{
    ShaderBall,
    Sphere,
    Plane,
    Cube,
    Cloth
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
    bool operator==(const MaterialPreviewConfiguration &) const = default;
};
inline bool ValidateMaterialPreviewConfiguration(const MaterialPreviewConfiguration &Configuration)
{
    return uint32_t(Configuration.Shape) <= 4 && uint32_t(Configuration.Environment) <= 3 &&
        std::isfinite(Configuration.Zoom) && Configuration.Zoom >= .5f && Configuration.Zoom <= 5 &&
        std::isfinite(Configuration.Exposure) && Configuration.Exposure >= .25f && Configuration.Exposure <= 4 &&
        std::isfinite(Configuration.PanX) && std::abs(Configuration.PanX) <= 2 &&
        std::isfinite(Configuration.PanY) && std::abs(Configuration.PanY) <= 2 &&
        std::isfinite(Configuration.Yaw) && std::abs(Configuration.Yaw) <= 3.141593f &&
        std::isfinite(Configuration.Pitch) && std::abs(Configuration.Pitch) <= 1.45f;
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

inline MaterialPreviewProjection ConstructMaterialPreviewProjection(
    const MaterialPreviewConfiguration &Configuration, float Aspect = 1)
{
    const float θ = Configuration.Yaw;
    const float Cosθ = std::cos(θ), Sinθ = std::sin(θ);
    const uint32_t Shape = std::min(uint32_t(Configuration.Shape), 4u);
    MaterialPreviewProjection Projection;
    // 📝 LookAt-style orbit (Blender/three.js convention): yaw spins about screen-vertical world-up through
    // the pivot, pitch tips about the fixed screen-right axis. The object up-axis never leans sideways and
    // vertical drags never roll; off-axis points trace screen ellipses during pitched yaw, as in standard tools.
    // Rotation basis shared by the view matrix and the normal rows.
    float R[9]{1, 0, 0, 0, 1, 0, 0, 0, 1};
    // 📝 Alloy capture rig: 34° perspective, each specimen framed the way the HTML capture frames its
    // r=1.35 ball at distance 5.8, viewed slightly from above (camera (0,1.1,5.7) looks down ~11°).
    // Pivots/radii are measured bounding volumes of the inspection geometry; zoom dollies the eye while
    // the near clamp keeps it out of the surface, mirroring the HTML optical-macro rule. The basis is a
    // lookAt from the orbit eye so the pivot stays centred; yaw still spins about world-up and pitch tips
    // about camera-right with no roll, exactly the shipped orbit feel.
    constexpr float FovY = 34 * std::numbers::pi_v<float> / 180;
    constexpr float BaseElevation = .1908f;
    constexpr float FrameRatio = 5.8f / 1.35f;
    constexpr float Pivot[5][3]{{0, -.055f, -.017f}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, -.041f, 0}};
    constexpr float Radius[5]{1.047f, .85f, 1.131f, 1.074f, .959f};
    const float Focal = Radius[Shape] * FrameRatio;
    const float Distance = std::max(Focal / Configuration.Zoom, Radius[Shape] + .2f);
    const float Elevation = std::clamp(Configuration.Pitch + BaseElevation, -1.45f, 1.45f);
    const float CosE = std::cos(Elevation), SinE = std::sin(Elevation);
    const float EyeDir[3]{-CosE * Sinθ, SinE, CosE * Cosθ};
    const float PanScale = Distance * std::tan(FovY / 2);
    float Right[3]{EyeDir[2], 0, -EyeDir[0]};
    const float RightLength = std::hypot(Right[0], Right[2]);
    Right[0] /= RightLength; Right[2] /= RightLength;
    const float Up[3]{EyeDir[1] * Right[2], EyeDir[2] * Right[0] - EyeDir[0] * Right[2], -EyeDir[1] * Right[0]};
    // 📝 Pan walks the pivot against camera-right and camera-up. The Y flip in Vulkan clip is handled
    // by the projection matrix, so pan uses consistent addition for both axes (Blender convention).
    const float Px = 16.f * float(Shape) + Pivot[Shape][0] -
        (Right[0] * Configuration.PanX + Up[0] * Configuration.PanY) * PanScale;
    const float Py = Pivot[Shape][1] - (Right[1] * Configuration.PanX + Up[1] * Configuration.PanY) * PanScale;
    const float Pz = Pivot[Shape][2] - (Right[2] * Configuration.PanX + Up[2] * Configuration.PanY) * PanScale;
    const float Ex = Px + EyeDir[0] * Distance, Ey = Py + EyeDir[1] * Distance, Ez = Pz + EyeDir[2] * Distance;
    const float Fwd[3]{-EyeDir[0], -EyeDir[1], -EyeDir[2]};
    R[0] = Right[0]; R[1] = Up[0]; R[2] = -Fwd[0];
    R[3] = Right[1]; R[4] = Up[1]; R[5] = -Fwd[1];
    R[6] = Right[2]; R[7] = Up[2]; R[8] = -Fwd[2];
    Projection.ViewFromSurface = {R[0], R[1], R[2], 0, R[3], R[4], R[5], 0, R[6], R[7], R[8], 0,
        -(R[0] * Ex + R[3] * Ey + R[6] * Ez), -(R[1] * Ex + R[4] * Ey + R[7] * Ez),
        -(R[2] * Ex + R[5] * Ey + R[8] * Ez), 1};
    // 📝 Vulkan clip (Y-down, depth 0..1); the shader divides w, so tiles and the preview share this form.
    const float SafeAspect = Aspect > 0 && std::isfinite(Aspect) ? Aspect : 1;
    constexpr float Near = .1f, Far = 20;
    const float F = 1 / std::tan(FovY / 2);
    const float P[16]{F / SafeAspect, 0, 0, 0, 0, -F, 0, 0, 0, 0, Far / (Near - Far), -1, 0, 0,
        Near * Far / (Near - Far), 0};
    const auto &V = Projection.ViewFromSurface;
    for (size_t Column = 0; Column < 4; ++Column)
        for (size_t Row = 0; Row < 4; ++Row)
        {
            float Sum = 0;
            for (size_t Axis = 0; Axis < 4; ++Axis) Sum += P[Axis * 4 + Row] * V[Column * 4 + Axis];
            Projection.ClipFromSurface[Column * 4 + Row] = Sum;
        }
    return Projection;
}

}
