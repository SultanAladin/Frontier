/*==========================================================================================================================================
                                                          VEHICLEDEPLOYMENT.CPP
==========================================================================================================================================*/
// 🧩 The vehicle's own deployment function: the footprint survey against PhysicsSpace, the stand transform on the surface's own pitch and
//    roll, the spawn brake state, and the driven-wheel rule. One path for a banked grid, a kerb, a ramp and a flat studio pad.

#include "Frontier/Extensions/Physics/VehicleDeployment.h"
#include "Frontier/Extensions/Physics/PhysicsSpace.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

constexpr double DegreesToRadians = 3.14159265358979323846 / 180.0;

} // namespace

VehicleDeploymentReading SurveyVehicleDeployment(PhysicsSpace&                   Space,
                                                 const Vehicle::VehicleGeometry& Geometry,
                                                 const SpawnPolicy&              Policy,
                                                 const std::array<double, 3>&    StartGroundPointMetres,
                                                 double                          StartYawDegrees)
{
    VehicleDeploymentReading Reading{};
    Reading.ParkedBrakes = Policy.ResolveParkedBrakeState();
    ResolveDrivenWheels(Policy.DrivesWheels, Reading.DrivesWheels);

    // 📝 The footprint comes from the authored car, not a guess: the centre of mass sets the stand, the wheelbase and track set how far
    //    the survey reaches, and the tyre radius sets the probe length.
    SpawnPolicy SurveyPolicy                    = Policy;
    SurveyPolicy.Footprint.HalfLengthMetres     = 0.5f * Geometry.Wheelbase;
    SurveyPolicy.Footprint.HalfWidthMetres      = 0.5f * std::max(Geometry.TrackFront, Geometry.TrackRear);
    SurveyPolicy.Footprint.CentreHeightMetres   = Geometry.CoMHeight;

    const SpawnSurfaceSampler Sampler = [&Space](const std::array<float, 3>& AbovePointMetres, SpawnSurfaceProbe& OutProbe)
    {
        PhysicsRay DownwardRay{};
        DownwardRay.OriginMetres = { static_cast<double>(AbovePointMetres[0]),
                                     static_cast<double>(AbovePointMetres[1]),
                                     static_cast<double>(AbovePointMetres[2]) };
        DownwardRay.Direction    = { 0.0, 0.0, -1.0 };
        DownwardRay.LengthMetres = 24.0;
        DownwardRay.IgnoredBody  = PhysicsNoBody;

        PhysicsRayHit Hit;
        if (!Space.CastRay(DownwardRay, Hit))
        {
            OutProbe.HasSurface = false;
            return false;
        }
        OutProbe.ContactPointMetres = { static_cast<float>(Hit.PointMetres[0]), static_cast<float>(Hit.PointMetres[1]),
                                        static_cast<float>(Hit.PointMetres[2]) };
        OutProbe.SurfaceNormal      = { static_cast<float>(Hit.Normal[0]), static_cast<float>(Hit.Normal[1]),
                                        static_cast<float>(Hit.Normal[2]) };
        OutProbe.HasSurface         = true;
        return true;
    };

    const std::array<float, 3> RequestedPoint = { static_cast<float>(StartGroundPointMetres[0]),
                                                   static_cast<float>(StartGroundPointMetres[1]),
                                                   static_cast<float>(StartGroundPointMetres[2]) };

    Reading.Stand               = ResolveSpawnPose(RequestedPoint, static_cast<float>(StartYawDegrees), SurveyPolicy, Sampler);
    Reading.Supported           = Reading.Stand.Supported;
    Reading.ChassisCentreMetres = Reading.Stand.PositionMetres[2] + Geometry.CoMHeight;

    if (!Reading.Stand.Surveyed)
    {
        Reading.Remark = "no surface under the footprint; the authored ground point stands";
    }
    else if (!Reading.Stand.Supported)
    {
        Reading.Remark = "surface steeper than the footprint rule accepts";
    }
    else
    {
        Reading.Remark = "stood on the surveyed surface";
    }
    return Reading;
}

VehicleStandTransform ResolveStandTransform(const SpawnPose&         Stand,
                                            const Vehicle::VehicleGeometry& Geometry,
                                            double                   CentreOfMassHeightMetres)
{
    (void)Geometry;
    VehicleStandTransform Transform{};

    // 📝 The chassis centre sits the centre of mass above the surface along the surface normal, never straight up.
    std::array<double, 3> Normal = { static_cast<double>(Stand.SurfaceNormal[0]),
                                     static_cast<double>(Stand.SurfaceNormal[1]),
                                     static_cast<double>(Stand.SurfaceNormal[2]) };
    if (!Stand.Surveyed)
    {
        Normal = { 0.0, 0.0, 1.0 };
    }

    Transform.PositionMetres = {
        static_cast<double>(Stand.PositionMetres[0]) + Normal[0] * CentreOfMassHeightMetres,
        static_cast<double>(Stand.PositionMetres[1]) + Normal[1] * CentreOfMassHeightMetres,
        static_cast<double>(Stand.PositionMetres[2]) + Normal[2] * CentreOfMassHeightMetres
    };

    // 📝 Level Euler order R = Rz(γ) · Ry(β) · Rx(α), exactly as the level authors orientation.
    const double HalfYaw   = 0.5 * static_cast<double>(Stand.YawDegrees) * DegreesToRadians;
    const double HalfPitch = 0.5 * static_cast<double>(Stand.PitchDegrees) * DegreesToRadians;
    const double HalfRoll  = 0.5 * static_cast<double>(Stand.RollDegrees) * DegreesToRadians;
    const double CosYaw = std::cos(HalfYaw), SinYaw = std::sin(HalfYaw);
    const double CosPitch = std::cos(HalfPitch), SinPitch = std::sin(HalfPitch);
    const double CosRoll = std::cos(HalfRoll), SinRoll = std::sin(HalfRoll);

    Transform.Orientation = {
        SinYaw * CosPitch * CosRoll + CosYaw * SinPitch * SinRoll,
        CosYaw * SinPitch * CosRoll - SinYaw * CosPitch * SinRoll,
        CosYaw * CosPitch * SinRoll + SinYaw * SinPitch * CosRoll,
        CosYaw * CosPitch * CosRoll - SinYaw * SinPitch * SinRoll
    };
    return Transform;
}

void FitDrivenWheels(Vehicle::VehicleSolverConfiguration& Configuration, DrivenWheelRule Rule)
{
    std::array<bool, 4> Driven{ false, false, true, true };
    ResolveDrivenWheels(Rule, Driven);
    for (size_t Wheel = 0; Wheel < Configuration.Wheels.size() && Wheel < Driven.size(); ++Wheel)
    {
        Configuration.Wheels[Wheel].Driven = Driven[Wheel];
    }
}

} // namespace Frontier
