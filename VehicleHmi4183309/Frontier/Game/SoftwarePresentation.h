#pragma once
/*==========================================================================================================================================
                                                         SOFTWAREPRESENTATION.H
==========================================================================================================================================*/
// 🧩 A CPU rasteriser standing in for the GPU presentation: draws the Lobby (studio floor, softbox, the
//    vehicle from its facet archives) with a z-buffer and one overhead light, and writes a PPM. It exists
//    so the C++ Lobby can be *seen* and regression-checked anywhere; the GLFW/WebGPU front-end replaces
//    this class, nothing else.

#include "Frontier/Engine/Effects/VehicleInspection.h"
#include "Frontier/Engine/Effects/PlumeSolver.h"
#include "Frontier/Engine/Scene/FacetArchive.h"

#include <array>
#include <string>
#include <vector>

namespace Frontier
{

struct PresentationVehicle
{
    const FacetArchiveData* Body = nullptr;
    const FacetArchiveData* Tyre = nullptr;
    const FacetArchiveData* Rim = nullptr;
    std::array<float, 3> PositionMetres = { 0.0f, 0.0f, 0.53f };
    float YawDegrees = 0.0f;
    std::array<float, 3> Paint = { 0.20f, 0.21f, 0.23f };   // linear-ish albedo tint
    VehicleInspection Inspection{};   // visual effect for this presentation of the vehicle
    // 📝 Particle Editor proof points (world space, from ParticleSpace::QueryParticle): glowing cube
    //    centres over the vehicle. Presentation only.
    std::vector<std::array<float, 3>> ParticlePoints;
};

// 📝 Renders one frame of the studio: white cyclorama floor disc, the softbox overhead, and the vehicles.
//    Camera orbits the centre car at (YawDegrees, Pitch, DistanceMetres). Returns false if the file
//    cannot be written.
bool RenderLobbyPpm(const std::string& Path, const std::vector<PresentationVehicle>& Vehicles,
                    float CameraYawDegrees, float CameraPitchDegrees, float CameraDistanceMetres,
                    int Width = 640, int Height = 400);

// 📝 Fluid proof: side (X–Z) maximum-density/heat projection of the CPU plume lattice, so the buoyant
//    rise shows (grey smoke, orange where hot). Presentation only — solver: Engine/Effects/PlumeSolver.
bool RenderPlumePreviewPpm(const std::string& Path, const PlumeSolver& Solver, int Width = 320, int Height = 320);

} // namespace Frontier
