#pragma once
/*==========================================================================================================================================
                                                         SOFTWAREPRESENTATION.H
==========================================================================================================================================*/
// 🧩 Portable studio & in-cabin HMI Garage software presentation renderer (PPM).
//    Renders FacetArchiveData vehicles with 1:1 3D paint ripple transitions, Ghost Fresnel/scanline/edge
//    shells, 14°/28° dihedral wireframes, dual wavefront curtains, and 130 holographic sparkles.
//    RenderHmiGaragePpm renders ONLY the single hero vehicle from the orbiting configurator camera.

#include "Frontier/Engine/Effects/MaterialTransition.h"
#include "Frontier/Engine/Effects/VehicleInspection.h"
#include "Frontier/Engine/Scene/FacetArchive.h"
#include "Frontier/Engine/Scene/VehicleCabin.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

struct PresentationVehicle
{
    uint32_t VehicleIdentifier = 1;
    const FacetArchiveData* Body = nullptr;
    const FacetArchiveData* Tyre = nullptr;
    const FacetArchiveData* Rim = nullptr;
    std::array<float, 3> PositionMetres = { 0.0f, 0.0f, 0.53285f };
    float YawDegrees = 0.0f;
    std::array<float, 3> Paint = { 0.539f, 0.578f, 0.631f };   // Liquid Silver default
    VehicleInspection Inspection{};
    MaterialTransition PaintRipple{};
    bool UsePaintRipple = false;
};

// 📝 Renders the multi-vehicle studio lobby scene into a binary PPM.
[[nodiscard]] bool RenderLobbyPpm(const std::string& Path, const std::vector<PresentationVehicle>& Vehicles,
                                  float CameraYawDegrees, float CameraPitchDegrees, float CameraDistanceMetres,
                                  int Width = 640, int Height = 400);

// 📝 Renders the single-vehicle HMI Garage configurator viewport (1024x640 default), excluding all non-hero
//    vehicles and applying the Garage configurator's orbiting camera, 3D paint ripple, and X-Ray Scan effects.
[[nodiscard]] bool RenderHmiGaragePpm(const std::string& Path,
                                      const std::vector<PresentationVehicle>& AllSceneVehicles,
                                      uint32_t HeroVehicleIdentifier,
                                      const GarageConfiguratorState& Garage,
                                      int Width = 1024,
                                      int Height = 640);

} // namespace Frontier
