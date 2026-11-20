/*============================================================================================================================================
                                                          FLUIDOBSTACLEVOXELIZER.H
============================================================================================================================================*/
// 🧩 Voxelizes WideBoundingTree & analytical FluidObstacle primitives into solid fluid collision masks

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/FluidSpace/FluidDescriptors.h"
#include "Engine/SimulationSpace/FluidSpace/FluidGridBuffers.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

namespace Frontier::SimulationSpace::FluidSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FLUID OBSTACLE VOXELIZER
//------------------------------------------------------------------------------------------------------------------------

class FluidObstacleVoxelizer
{
public:
    // Evaluates analytical obstacle mask (0 or 1) at WorldPoint (+Z Up)
    [[nodiscard]] static float EvaluateAnalyticalObstacleMask(
        const Mathematics::Axis3D&   WorldPoint,
        const FluidObstacleSettings& Obstacle,
        float                        ElapsedSeconds) noexcept;

    // Bakes WideBoundingTree & analytical obstacle occupancy into HeroGrid.SceneSolidMask
    static void BakeSceneCollisionMask(
        const SpatialAcceleration::WideBoundingTree* OptionalTree,
        const FluidObstacleSettings&                 Obstacle,
        float                                        ElapsedSeconds,
        FluidGridVolume&                             InOutHeroGrid) noexcept;
};

} // namespace Frontier::SimulationSpace::FluidSpace
