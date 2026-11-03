/*==========================================================================================================================================
                                                        VEHICLESCENEPUBLISHER.CPP
==========================================================================================================================================*/
// 🧩 Implementation of VehicleSolver -> SceneViewport / IlluminationHost occupant relocation and VehicleProfile GPU upload.

#include "Engine/PhysicalDynamics/Vehicle/VehicleScenePublisher.h"

#include "Engine/Rendering/GlobalIllumination/IlluminationHost.h"
#include "Engine/Rendering/GlobalIllumination/OccupantRelocation.h"
#include "Engine/Rendering/GlobalIllumination/TransformProjection.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Frontier::Vehicle
{

namespace
{

[[nodiscard]] GlobalIllumination::Transform BuildSceneTransform(const Vec3& Position, const Quat& Orientation) noexcept
{
    const Quat Unit = Orientation.Normalized();
    return GlobalIllumination::ConstructRigidTransform(
        {static_cast<double>(Position.x), static_cast<double>(Position.y), static_cast<double>(Position.z)},
        {static_cast<double>(Unit.x), static_cast<double>(Unit.y), static_cast<double>(Unit.z), static_cast<double>(Unit.w)});
}

} // namespace

void PublishVehicleToHost(GlobalIllumination::IlluminationHost& Host, const VehicleSolver& Solver) noexcept
{
    const VehicleTelemetryReading& Telemetry = Solver.QueryTelemetry();
    std::vector<GlobalIllumination::OccupantPlacement> Placements;
    Placements.reserve(5u);

    if (Host.Scene.VehicleBody >= 0)
    {
        GlobalIllumination::OccupantPlacement BodyPlacement{};
        BodyPlacement.Occupant = static_cast<uint32_t>(Host.Scene.VehicleBody);
        BodyPlacement.Active   = true;
        BodyPlacement.Current  = BuildSceneTransform(Telemetry.Chassis.PositionWorld, Telemetry.Chassis.OrientationWorld);
        Placements.push_back(BodyPlacement);
    }

    std::array<bool, 4> ShreddedFlags{false, false, false, false};
    bool                AnyShredded = false;
    for (size_t Wheel = 0; Wheel < 4; ++Wheel)
    {
        const int32_t WheelOccupant = Host.Scene.VehicleWheels[Wheel];
        if (WheelOccupant < 0) continue;

        const WheelCornerTelemetry& Corner = Telemetry.Wheels[Wheel];
        GlobalIllumination::OccupantPlacement WheelPlacement{};
        WheelPlacement.Occupant = static_cast<uint32_t>(WheelOccupant);
        WheelPlacement.Active   = true;
        WheelPlacement.Current  = BuildSceneTransform(Corner.HubPositionWorld, Corner.WheelOrientationWorld);
        Placements.push_back(WheelPlacement);

        ShreddedFlags[Wheel] = Corner.Shredded;
        AnyShredded          = AnyShredded || Corner.Shredded;
    }

    if (!Placements.empty())
    {
        GlobalIllumination::RelocateOccupants(Host, Placements);
        if (Host.RayTracing)
        {
            Host.RayTracing->RefreshTlas(Host.Scene, AnyShredded ? &ShreddedFlags : nullptr);
        }
    }

    if (Host.VehicleRegion)
    {
        alignas(16) std::array<uint8_t, SceneVehicleProfileByteCount> ProfileBytes{};
        Solver.EncodeSceneVehicleProfile(ProfileBytes.data());
        Host.Exchange.WriteRegion(Host.VehicleRegion, 0, ProfileBytes.data(), ProfileBytes.size());
    }
}

void PublishVehicleToViewport(GlobalIllumination::SceneViewport& Viewport, const VehicleSolver& Solver) noexcept
{
    if (GlobalIllumination::IlluminationHost* Host = Viewport.AccessHost(); Host != nullptr)
    {
        PublishVehicleToHost(*Host, Solver);
    }
}

} // namespace Frontier::Vehicle
