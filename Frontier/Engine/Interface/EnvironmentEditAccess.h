/*==========================================================================================================================================
                                                         ENVIRONMENTEDITACCESS.H
==========================================================================================================================================*/
// 🧩 Non-owning environment edit references, refreshed by the workspace after scene mutations and before inspection.

#pragma once

#include "Engine/Scene/EngineConfigurations.h"
#include "Engine/Scene/EnvironmentDiagnostics.h"

#include <array>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENVIRONMENT EDIT ACCESS
//------------------------------------------------------------------------------------------------------------------------

struct EnvironmentEditAccess
{
    AtmosphereConfiguration* Atmosphere = nullptr;
    ForceFieldConfiguration* Forces = nullptr;
    std::shared_ptr<const EnvironmentRenderReadout> Rendering;
    SolarConfiguration                  *Solar      = nullptr;
    CloudConfiguration                  *Clouds     = nullptr;
    CloudConfiguration                  *LocalCloud = nullptr;
    FogConfiguration                    *Fog        = nullptr;
    FogConfiguration                    *LocalFog   = nullptr;
    WindConfiguration                   *Wind       = nullptr;
    LightningConfiguration              *Lightning  = nullptr;
    std::array<PlanetConfiguration *, 4> Planets{};
    bool                                 CelestialReady = false;
    std::array<double, 3>                LunarPlacement{}; // [° azimuth, ° elevation, days]
    std::array<std::array<double, 2>, 4> PlanetPlacements{};
    bool                                 CameraReady   = false;
    std::array<double, 3>                CameraEye     = {0.0, 0.0, 2.0};
    std::array<double, 3>                CameraForward = {1.0, 0.0, 0.0};
    std::array<uint32_t, 2>              ViewExtent    = {0, 0};
};

} // namespace Frontier
