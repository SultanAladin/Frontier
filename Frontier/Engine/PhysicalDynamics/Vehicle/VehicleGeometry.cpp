/*==========================================================================================================================================
                                                            VEHICLEGEOMETRY.CPP
==========================================================================================================================================*/
// 🧩 Principal inertia computation for the ProvingGround ControlVehicle chassis.

#include "Engine/PhysicalDynamics/Vehicle/VehicleGeometry.h"

namespace Frontier::Vehicle
{

Vec3 VehicleGeometrySpecification::PrincipalInertiaKgm2() const noexcept
{
    const float FullX = 2.0f * ChassisHalfExtentsMetres.x;
    const float FullY = 2.0f * ChassisHalfExtentsMetres.y;
    const float FullZ = 2.0f * ChassisHalfExtentsMetres.z;
    const float Scale = ChassisMassKg / 12.0f;
    return {
        Scale * (FullY * FullY + FullZ * FullZ),
        Scale * (FullX * FullX + FullZ * FullZ),
        Scale * (FullX * FullX + FullY * FullY)};
}

Vec3 VehicleGeometrySpecification::InversePrincipalInertia() const noexcept
{
    const Vec3 Inertia = PrincipalInertiaKgm2();
    return {
        1.0f / std::max(Inertia.x, 1.0f),
        1.0f / std::max(Inertia.y, 1.0f),
        1.0f / std::max(Inertia.z, 1.0f)};
}

} // namespace Frontier::Vehicle
