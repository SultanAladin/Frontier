/*==========================================================================================================================================
                                                              JOLTADAPTER.CPP
==========================================================================================================================================*/
// 🧩 Implementation of JoltAdapter: multi-occupant rigid-body + force-zone + vehicle integration with substepping and SLERP pose blending.

#include "Engine/PhysicalDynamics/JoltAdapter.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Frontier
{

namespace
{

[[nodiscard]] float EstimateFallbackMassKg(const RigidBodySpecification& Spec) noexcept
{
    if (Spec.MassKilograms > 1.0e-4f) return Spec.MassKilograms;
    switch (Spec.Shape.Category)
    {
        case CollisionShapeCategory::Sphere:
        {
            const float R = std::max(0.05f, Spec.Shape.Radius);
            return 1000.0f * (4.0f / 3.0f) * Vehicle::PiConstant * R * R * R;
        }
        case CollisionShapeCategory::Box:
        {
            const Vehicle::Vec3 H = Spec.Shape.HalfExtents;
            return 1000.0f * 8.0f * std::max(0.02f, H.x) * std::max(0.02f, H.y) * std::max(0.02f, H.z);
        }
        case CollisionShapeCategory::Capsule:
        case CollisionShapeCategory::Cylinder:
        {
            const float R = std::max(0.05f, Spec.Shape.Radius);
            const float L = 2.0f * std::max(0.05f, Spec.Shape.HalfHeight);
            return 1000.0f * Vehicle::PiConstant * R * R * L;
        }
        default:
            return 10.0f;
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                             QUATERNION & POSE INTERPOLATION
//------------------------------------------------------------------------------------------------------------------------

Vehicle::Quat InterpolateOrientationSlerp(const Vehicle::Quat& Start, const Vehicle::Quat& Target, float Fraction) noexcept
{
    const float ClampedT = Vehicle::ClampScalar(Fraction, 0.0f, 1.0f);
    Vehicle::Quat End    = Target;
    float Cosine         = Start.x * End.x + Start.y * End.y + Start.z * End.z + Start.w * End.w;
    if (Cosine < 0.0f)
    {
        Cosine = -Cosine;
        End    = {-End.x, -End.y, -End.z, -End.w};
    }

    float ScaleStart = 1.0f - ClampedT;
    float ScaleEnd   = ClampedT;
    if (Cosine < 0.9995f)
    {
        const float Angle    = std::acos( std::min(1.0f, Cosine) );
        const float InvSin   = 1.0f / std::max(1.0e-6f, std::sin(Angle));
        ScaleStart           = std::sin((1.0f - ClampedT) * Angle) * InvSin;
        ScaleEnd             = std::sin(ClampedT * Angle) * InvSin;
    }

    Vehicle::Quat Blended{
        Start.x * ScaleStart + End.x * ScaleEnd,
        Start.y * ScaleStart + End.y * ScaleEnd,
        Start.z * ScaleStart + End.z * ScaleEnd,
        Start.w * ScaleStart + End.w * ScaleEnd};
    return Blended.Normalized();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

bool JoltAdapter::Initialize(const RigidBodyWorldSettings& WorldSettings) noexcept
{
    Finalize();
    ConfiguredSettings  = WorldSettings;
    CompletedOuterTicks = 0u;
    CompletedSubsteps   = 0u;
    SimulatedSeconds    = 0.0f;
    LatestOuterStepSec  = WorldSettings.FixedStepSeconds;
    LatestSubstepCount  = 1u;
    LatestTickWallMs    = 0.0f;
    return WorldSolver.BringUp(ConfiguredSettings);
}

void JoltAdapter::Finalize() noexcept
{
    OccupantRecords.clear();
    OccupantIndexByKey.clear();
    VehicleRecords.clear();
    VehicleIndexByKey.clear();
    ForceZones.clear();
    WorldSolver.Retire();
}

void JoltAdapter::Reset() noexcept
{
    if (!WorldSolver.Ready()) return;
    OccupantRecords.clear();
    OccupantIndexByKey.clear();
    VehicleRecords.clear();
    VehicleIndexByKey.clear();
    ForceZones.clear();
    WorldSolver.DestroyAllBodies();
    CompletedOuterTicks = 0u;
    CompletedSubsteps   = 0u;
    SimulatedSeconds    = 0.0f;
    LatestTickWallMs    = 0.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              WORLD OCCUPANT MANAGEMENT
//------------------------------------------------------------------------------------------------------------------------

RigidBodyIdentity JoltAdapter::RegisterOccupant(const AdapterOccupantSpecification& Specification) noexcept
{
    if (!WorldSolver.Ready()) return InvalidRigidBody;

    const auto Found = OccupantIndexByKey.find(Specification.OccupantKey);
    if (Found != OccupantIndexByKey.end())
    {
        ReclaimOccupant(Specification.OccupantKey);
    }

    const RigidBodyIdentity CreatedId = WorldSolver.CreateBody(Specification.Body);
    if (CreatedId == InvalidRigidBody) return InvalidRigidBody;

    OccupantSlot Slot{};
    Slot.Specification       = Specification;
    Slot.BodyIdentity        = CreatedId;
    Slot.ResolvedMassKg      = EstimateFallbackMassKg(Specification.Body);
    Slot.PreviousPosition    = Specification.Body.Position;
    Slot.PreviousOrientation = Specification.Body.Orientation;
    Slot.CurrentPosition     = Specification.Body.Position;
    Slot.CurrentOrientation  = Specification.Body.Orientation;
    Slot.KinematicStartPos   = Specification.Body.Position;
    Slot.KinematicStartOrn   = Specification.Body.Orientation;
    Slot.KinematicTargetPos  = Specification.Body.Position;
    Slot.KinematicTargetOrn  = Specification.Body.Orientation;
    Slot.LinearVelocityMps   = Specification.Body.LinearVelocity;
    Slot.AngularVelocityRps  = Specification.Body.AngularVelocity;
    Slot.AwakeCondition      = (Specification.Body.Motion == RigidBodyMotionCategory::Dynamic);

    OccupantIndexByKey[Specification.OccupantKey] = OccupantRecords.size();
    OccupantRecords.push_back(std::move(Slot));
    return CreatedId;
}

bool JoltAdapter::ReclaimOccupant(uint64_t OccupantKey) noexcept
{
    const auto Found = OccupantIndexByKey.find(OccupantKey);
    if (Found == OccupantIndexByKey.end()) return false;

    const size_t TargetIndex = Found->second;
    const RigidBodyIdentity BodyId = OccupantRecords[TargetIndex].BodyIdentity;
    if (BodyId != InvalidRigidBody && WorldSolver.Ready())
    {
        WorldSolver.DestroyBody(BodyId);
    }

    const size_t LastIndex = OccupantRecords.size() - 1u;
    if (TargetIndex != LastIndex)
    {
        OccupantRecords[TargetIndex] = std::move(OccupantRecords[LastIndex]);
        OccupantIndexByKey[OccupantRecords[TargetIndex].Specification.OccupantKey] = TargetIndex;
    }
    OccupantRecords.pop_back();
    OccupantIndexByKey.erase(Found);
    return true;
}

bool JoltAdapter::AuthenticateOccupant(uint64_t OccupantKey) const noexcept
{
    return OccupantIndexByKey.find(OccupantKey) != OccupantIndexByKey.end();
}

void JoltAdapter::ConfigureKinematicTarget(uint64_t             OccupantKey,
                                           const Vehicle::Vec3& TargetPosition,
                                           const Vehicle::Quat& TargetOrientation) noexcept
{
    const auto Found = OccupantIndexByKey.find(OccupantKey);
    if (Found == OccupantIndexByKey.end()) return;
    OccupantSlot& Slot      = OccupantRecords[Found->second];
    Slot.KinematicStartPos  = Slot.CurrentPosition;
    Slot.KinematicStartOrn  = Slot.CurrentOrientation;
    Slot.KinematicTargetPos = TargetPosition;
    Slot.KinematicTargetOrn = TargetOrientation.Normalized();
    Slot.KinematicTargeted  = true;
}

void JoltAdapter::TeleportOccupant(uint64_t             OccupantKey,
                                   const Vehicle::Vec3& Position,
                                   const Vehicle::Quat& Orientation) noexcept
{
    const auto Found = OccupantIndexByKey.find(OccupantKey);
    if (Found == OccupantIndexByKey.end()) return;
    OccupantSlot& Slot       = OccupantRecords[Found->second];
    const Vehicle::Quat Unit = Orientation.Normalized();
    WorldSolver.Teleport(Slot.BodyIdentity, Position, Unit);
    Slot.PreviousPosition    = Position;
    Slot.PreviousOrientation = Unit;
    Slot.CurrentPosition     = Position;
    Slot.CurrentOrientation  = Unit;
    Slot.KinematicTargeted   = false;
}

void JoltAdapter::ApplyOccupantForce(uint64_t OccupantKey, const Vehicle::Vec3& ForceNewtons) noexcept
{
    const auto Found = OccupantIndexByKey.find(OccupantKey);
    if (Found == OccupantIndexByKey.end()) return;
    OccupantRecords[Found->second].PendingForceN += ForceNewtons;
}

void JoltAdapter::ApplyOccupantImpulse(uint64_t OccupantKey, const Vehicle::Vec3& ImpulseNewtonSeconds) noexcept
{
    const auto Found = OccupantIndexByKey.find(OccupantKey);
    if (Found == OccupantIndexByKey.end()) return;
    OccupantSlot& Slot = OccupantRecords[Found->second];
    WorldSolver.ApplyImpulse(Slot.BodyIdentity, ImpulseNewtonSeconds);
    Slot.PeakImpulseNs = std::max(Slot.PeakImpulseNs, ImpulseNewtonSeconds.Length());
    if (Slot.Specification.FractureImpulseThresholdNs > 0.0f &&
        Slot.PeakImpulseNs >= Slot.Specification.FractureImpulseThresholdNs)
    {
        Slot.FracturedCondition = true;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                              VEHICLE MANAGEMENT
//------------------------------------------------------------------------------------------------------------------------

void JoltAdapter::CaptureVehicleWheelWorldPoses(const Vehicle::VehicleTelemetryReading& Telemetry,
                                                std::array<Vehicle::Vec3, 4>&           OutPositions,
                                                std::array<Vehicle::Quat, 4>&           OutOrientations) noexcept
{
    for (size_t Idx = 0; Idx < 4; ++Idx)
    {
        OutPositions[Idx]    = Telemetry.Wheels[Idx].HubPositionWorld;
        OutOrientations[Idx] = Telemetry.Wheels[Idx].WheelOrientationWorld;
    }
}

bool JoltAdapter::RegisterVehicle(uint64_t                                   VehicleKey,
                                  const Vehicle::VehicleSolverConfiguration& Configuration,
                                  const Vehicle::Vec3&                       InitialPosition,
                                  const Vehicle::Quat&                       InitialOrientation) noexcept
{
    if (!WorldSolver.Ready()) return false;
    if (VehicleIndexByKey.find(VehicleKey) != VehicleIndexByKey.end())
    {
        ReclaimVehicle(VehicleKey);
    }

    VehicleSlot Slot{};
    Slot.VehicleKey = VehicleKey;
    Slot.Rig        = std::make_unique<Vehicle::VehicleJoltRig>();
    if (!Slot.Rig->Construct(WorldSolver, Configuration, InitialPosition, InitialOrientation))
    {
        return false;
    }

    // Run one micro-evaluation so initial wheel world poses and GPU profile are populated immediately.
    Slot.Rig->AccessVehicle().Step(1.0f / 240.0f);
    const auto& Tel            = Slot.Rig->QueryVehicle().QueryTelemetry();
    Slot.PreviousChassisPos    = InitialPosition;
    Slot.PreviousChassisOrient = InitialOrientation;
    Slot.CurrentChassisPos     = InitialPosition;
    Slot.CurrentChassisOrient  = InitialOrientation;
    CaptureVehicleWheelWorldPoses(Tel, Slot.PreviousWheelPos, Slot.PreviousWheelOrient);
    Slot.CurrentWheelPos    = Slot.PreviousWheelPos;
    Slot.CurrentWheelOrient = Slot.PreviousWheelOrient;
    Slot.Rig->QueryVehicle().EncodeSceneVehicleProfile(Slot.EncodedGpuProfile.data());

    VehicleIndexByKey[VehicleKey] = VehicleRecords.size();
    VehicleRecords.push_back(std::move(Slot));
    return true;
}

bool JoltAdapter::ReclaimVehicle(uint64_t VehicleKey) noexcept
{
    const auto Found = VehicleIndexByKey.find(VehicleKey);
    if (Found == VehicleIndexByKey.end()) return false;

    const size_t TargetIndex = Found->second;
    if (VehicleRecords[TargetIndex].Rig)
    {
        VehicleRecords[TargetIndex].Rig->Reclaim(WorldSolver);
    }

    const size_t LastIndex = VehicleRecords.size() - 1u;
    if (TargetIndex != LastIndex)
    {
        VehicleRecords[TargetIndex] = std::move(VehicleRecords[LastIndex]);
        VehicleIndexByKey[VehicleRecords[TargetIndex].VehicleKey] = TargetIndex;
    }
    VehicleRecords.pop_back();
    VehicleIndexByKey.erase(Found);
    return true;
}

void JoltAdapter::ConfigureDriverInput(uint64_t VehicleKey, const Vehicle::DriverCommand& Command) noexcept
{
    const auto Found = VehicleIndexByKey.find(VehicleKey);
    if (Found == VehicleIndexByKey.end()) return;
    VehicleRecords[Found->second].Rig->AccessVehicle().AssignCommand(Command);
}

void JoltAdapter::ReconfigureVehicle(uint64_t VehicleKey, const Vehicle::VehicleSolverConfiguration& Configuration) noexcept
{
    const auto Found = VehicleIndexByKey.find(VehicleKey);
    if (Found == VehicleIndexByKey.end()) return;
    VehicleRecords[Found->second].Rig->AccessVehicle().Reconfigure(Configuration);
}

void JoltAdapter::ConfigureWheelEnclosure(uint64_t VehicleKey, uint32_t WheelIndex, bool EnclosedCondition) noexcept
{
    const auto Found = VehicleIndexByKey.find(VehicleKey);
    if (Found == VehicleIndexByKey.end() || WheelIndex >= 4u) return;
    VehicleSlot& Slot                = VehicleRecords[Found->second];
    Slot.WheelEnclosed[WheelIndex]   = EnclosedCondition;
    Slot.Rig->AccessVehicle().ConfigureWheelEnclosure(WheelIndex, EnclosedCondition);
}

Vehicle::VehicleJoltRig* JoltAdapter::AccessVehicleRig(uint64_t VehicleKey) noexcept
{
    const auto Found = VehicleIndexByKey.find(VehicleKey);
    return Found == VehicleIndexByKey.end() ? nullptr : VehicleRecords[Found->second].Rig.get();
}

const Vehicle::VehicleJoltRig* JoltAdapter::QueryVehicleRig(uint64_t VehicleKey) const noexcept
{
    const auto Found = VehicleIndexByKey.find(VehicleKey);
    return Found == VehicleIndexByKey.end() ? nullptr : VehicleRecords[Found->second].Rig.get();
}

//------------------------------------------------------------------------------------------------------------------------
//                                              AERODYNAMIC & FORCE ZONES
//------------------------------------------------------------------------------------------------------------------------

void JoltAdapter::RegisterForceZone(const AdapterForceZone& Zone) noexcept
{
    for (AdapterForceZone& Existing : ForceZones)
    {
        if (Existing.ZoneKey == Zone.ZoneKey)
        {
            Existing = Zone;
            return;
        }
    }
    ForceZones.push_back(Zone);
}

bool JoltAdapter::ReclaimForceZone(uint64_t ZoneKey) noexcept
{
    const auto It = std::remove_if(ForceZones.begin(), ForceZones.end(),
                                   [ZoneKey](const AdapterForceZone& Z) { return Z.ZoneKey == ZoneKey; });
    if (It == ForceZones.end()) return false;
    ForceZones.erase(It, ForceZones.end());
    return true;
}

void JoltAdapter::ClearForceZones() noexcept
{
    ForceZones.clear();
}

Vehicle::Vec3 JoltAdapter::EvaluateZoneAirflowAt(const Vehicle::Vec3& WorldPoint,
                                                 Vehicle::Vec3&       OutDirectAcceleration) const noexcept
{
    Vehicle::Vec3 Airflow{};
    OutDirectAcceleration = {0.0f, 0.0f, 0.0f};

    for (const AdapterForceZone& Zone : ForceZones)
    {
        if (!Zone.EnabledCondition) continue;
        switch (Zone.Category)
        {
            case AdapterForceZoneCategory::UniformWind:
            {
                Airflow += Zone.FlowVelocityMps;
                break;
            }
            case AdapterForceZoneCategory::VortexFunnel:
            {
                const Vehicle::Vec3 Offset   = WorldPoint - Zone.OriginMetres;
                const float         PlanarR  = std::sqrt(Offset.x * Offset.x + Offset.y * Offset.y);
                const float         OuterR   = std::max(Zone.OuterRadiusMetres, 0.5f);
                if (PlanarR < OuterR && Offset.z >= -2.0f)
                {
                    const float CoreR       = std::max(Zone.CoreRadiusMetres, 0.25f);
                    const float NormR       = PlanarR / CoreR;
                    // Burgers-Rott tangential profile: v_theta(r) = V_peak * (2*r/R_c) / (1 + (r/R_c)^2)
                    const float SwirlSpeed  = Zone.TangentialSpeedMps * (2.0f * NormR) / (1.0f + NormR * NormR);
                    const float OuterFade   = 1.0f - Vehicle::ClampScalar((PlanarR - 0.65f * OuterR) / (0.35f * OuterR), 0.0f, 1.0f);
                    const Vehicle::Vec3 Rad = PlanarR > 1.0e-4f
                                                  ? Vehicle::Vec3{Offset.x / PlanarR, Offset.y / PlanarR, 0.0f}
                                                  : Vehicle::Vec3{1.0f, 0.0f, 0.0f};
                    const Vehicle::Vec3 Tan {-Rad.y, Rad.x, 0.0f};
                    const float Inflow      = Zone.RadialInflowMps * OuterFade * std::exp(-0.25f * NormR);
                    const float Updraft     = Zone.AxialUpdraftMps * OuterFade * std::exp(-0.35f * NormR * NormR);
                    Airflow += (Tan * (SwirlSpeed * OuterFade)) - (Rad * Inflow) + Vehicle::Vec3{0.0f, 0.0f, Updraft};
                }
                break;
            }
            case AdapterForceZoneCategory::RadialImpulse:
            {
                const Vehicle::Vec3 Delta = WorldPoint - Zone.OriginMetres;
                const float         Dist  = Delta.Length();
                const float         Outer = std::max(Zone.OuterRadiusMetres, 0.25f);
                if (Dist < Outer)
                {
                    const Vehicle::Vec3 Dir = Dist > 1.0e-4f ? (Delta / Dist) : Vehicle::Vec3{0.0f, 0.0f, 1.0f};
                    const float Falloff     = 1.0f - (Dist / Outer);
                    OutDirectAcceleration  += Dir * (Zone.PeakAccelerationMps2 * Falloff * Falloff);
                }
                break;
            }
        }
    }
    return Airflow;
}

//------------------------------------------------------------------------------------------------------------------------
//                                        SUBSTEPPED TICK & POSE INTERPOLATION
//------------------------------------------------------------------------------------------------------------------------

void JoltAdapter::ExecuteSubsteppedTick(float OuterStepSeconds, uint32_t SubstepCount) noexcept
{
    if (!WorldSolver.Ready()) return;

    const auto     StartClock  = std::chrono::steady_clock::now();
    const uint32_t SafeSubsteps= std::max(1u, SubstepCount);
    const float    ClampedOuter= Vehicle::ClampScalar(OuterStepSeconds, 1.0e-5f, 0.1f);
    const float    SubDt       = ClampedOuter / static_cast<float>(SafeSubsteps);

    // 1. Snapshot PreviousPose across all registered occupants and vehicles before running substeps.
    for (OccupantSlot& Slot : OccupantRecords)
    {
        Slot.PreviousPosition    = Slot.CurrentPosition;
        Slot.PreviousOrientation = Slot.CurrentOrientation;
        Slot.KinematicStartPos   = Slot.CurrentPosition;
        Slot.KinematicStartOrn   = Slot.CurrentOrientation;
        Slot.PeakImpulseNs       = 0.0f;
    }

    for (VehicleSlot& VSlot : VehicleRecords)
    {
        VSlot.PreviousChassisPos    = VSlot.CurrentChassisPos;
        VSlot.PreviousChassisOrient = VSlot.CurrentChassisOrient;
        VSlot.PreviousWheelPos      = VSlot.CurrentWheelPos;
        VSlot.PreviousWheelOrient   = VSlot.CurrentWheelOrient;
    }

    // 2. Advance SafeSubsteps coupled substeps across kinematic targets, force zones, vehicles, and Jolt rigid bodies.
    for (uint32_t Sub = 0u; Sub < SafeSubsteps; ++Sub)
    {
        const float SubFraction = static_cast<float>(Sub + 1u) / static_cast<float>(SafeSubsteps);

        // 2a. Kinematic occupant substep interpolation & dynamic occupant force-zone + external forces.
        for (OccupantSlot& Slot : OccupantRecords)
        {
            if (Slot.BodyIdentity == InvalidRigidBody) continue;
            if (Slot.Specification.Body.Motion == RigidBodyMotionCategory::Kinematic && Slot.KinematicTargeted)
            {
                const Vehicle::Vec3 StepPos = Slot.KinematicStartPos +
                                              (Slot.KinematicTargetPos - Slot.KinematicStartPos) * SubFraction;
                const Vehicle::Quat StepOrn = InterpolateOrientationSlerp(
                    Slot.KinematicStartOrn, Slot.KinematicTargetOrn, SubFraction);
                WorldSolver.MoveKinematic(Slot.BodyIdentity, StepPos, StepOrn, SubDt);
            }
            else if (Slot.Specification.Body.Motion == RigidBodyMotionCategory::Dynamic)
            {
                Vehicle::Vec3 TotalForce = Slot.PendingForceN;
                if (Slot.Specification.ForceZoneCoupled && !ForceZones.empty())
                {
                    RigidBodyPose LivePose{};
                    if (WorldSolver.QueryPose(Slot.BodyIdentity, LivePose))
                    {
                        Vehicle::Vec3 DirectAccel{};
                        const Vehicle::Vec3 Airflow = EvaluateZoneAirflowAt(LivePose.Position, DirectAccel);
                        const Vehicle::Vec3 RelWind = Airflow - LivePose.LinearVelocity;
                        const float         RelSpd  = RelWind.Length();
                        if (RelSpd > 1.0e-3f && Slot.Specification.AerodynamicDragAreaM2 > 0.0f)
                        {
                            TotalForce += RelWind * (0.5f * 1.225f * Slot.Specification.AerodynamicDragAreaM2 * RelSpd);
                        }
                        TotalForce += DirectAccel * Slot.ResolvedMassKg;
                    }
                }
                if (TotalForce.LengthSquared() > 1.0e-8f)
                {
                    WorldSolver.ApplyForce(Slot.BodyIdentity, TotalForce);
                }
            }
        }

        // 2b. Vehicle substep forces (coupled with ambient force-zone airflow!).
        for (VehicleSlot& VSlot : VehicleRecords)
        {
            if (!VSlot.Rig) continue;
            const RigidBodyIdentity ChassisId = VSlot.Rig->QueryChassisBody();
            if (!ForceZones.empty() && ChassisId != InvalidRigidBody)
            {
                RigidBodyPose ChassisPose{};
                if (WorldSolver.QueryPose(ChassisId, ChassisPose))
                {
                    Vehicle::Vec3 DirectAccel{};
                    const Vehicle::Vec3 Airflow = EvaluateZoneAirflowAt(ChassisPose.Position, DirectAccel);
                    VSlot.Rig->AccessVehicle().ConfigureAmbientWind(Airflow);
                    if (DirectAccel.LengthSquared() > 1.0e-8f)
                    {
                        const float Mass = VSlot.Rig->QueryVehicle().QueryConfiguration().Geometry.ChassisMassKg;
                        WorldSolver.ApplyForce(ChassisId, DirectAccel * Mass);
                    }
                }
            }
            VSlot.Rig->ApplySubstepForces(WorldSolver, SubDt);
        }

        // 2c. Unified Jolt world step!
        std::vector<Vehicle::Vec3> PreStepVelocities(OccupantRecords.size());
        for (size_t Idx = 0; Idx < OccupantRecords.size(); ++Idx)
        {
            RigidBodyPose PoseBefore{};
            if (OccupantRecords[Idx].Specification.Body.Motion == RigidBodyMotionCategory::Dynamic &&
                WorldSolver.QueryPose(OccupantRecords[Idx].BodyIdentity, PoseBefore))
            {
                PreStepVelocities[Idx] = PoseBefore.LinearVelocity;
            }
        }

        WorldSolver.StepExactly(SubDt);

        // 2d. Measure contact/collision velocity jumps for fracture threshold detection.
        for (size_t Idx = 0; Idx < OccupantRecords.size(); ++Idx)
        {
            OccupantSlot& Slot = OccupantRecords[Idx];
            if (Slot.Specification.Body.Motion != RigidBodyMotionCategory::Dynamic) continue;
            RigidBodyPose PoseAfter{};
            if (WorldSolver.QueryPose(Slot.BodyIdentity, PoseAfter))
            {
                const Vehicle::Vec3 ExpectedFreeFallDelta = ConfiguredSettings.Gravity * SubDt;
                const Vehicle::Vec3 NonGravityDeltaV =
                    (PoseAfter.LinearVelocity - PreStepVelocities[Idx]) - ExpectedFreeFallDelta;
                const float ImpulseEstimate = Slot.ResolvedMassKg * NonGravityDeltaV.Length();
                Slot.PeakImpulseNs          = std::max(Slot.PeakImpulseNs, ImpulseEstimate);
                if (Slot.Specification.FractureImpulseThresholdNs > 0.0f &&
                    ImpulseEstimate >= Slot.Specification.FractureImpulseThresholdNs)
                {
                    Slot.FracturedCondition = true;
                }
            }
        }
    }

    // 3. Latch CurrentPose across all occupants and vehicles after completing SafeSubsteps.
    for (OccupantSlot& Slot : OccupantRecords)
    {
        Slot.PendingForceN     = {0.0f, 0.0f, 0.0f};
        Slot.KinematicTargeted = false;
        RigidBodyPose FinalPose{};
        if (WorldSolver.QueryPose(Slot.BodyIdentity, FinalPose))
        {
            Slot.CurrentPosition    = FinalPose.Position;
            Slot.CurrentOrientation = FinalPose.Orientation;
            Slot.LinearVelocityMps  = FinalPose.LinearVelocity;
            Slot.AngularVelocityRps = FinalPose.AngularVelocity;
            Slot.AwakeCondition     = FinalPose.Awake;
        }
    }

    for (VehicleSlot& VSlot : VehicleRecords)
    {
        if (!VSlot.Rig) continue;
        const auto Chassis = VSlot.Rig->ReadChassisFromJolt(WorldSolver);
        const auto& Tel    = VSlot.Rig->QueryVehicle().QueryTelemetry();
        VSlot.CurrentChassisPos    = Chassis.PositionWorld;
        VSlot.CurrentChassisOrient = Chassis.OrientationWorld;
        CaptureVehicleWheelWorldPoses(Tel, VSlot.CurrentWheelPos, VSlot.CurrentWheelOrient);
        VSlot.Rig->QueryVehicle().EncodeSceneVehicleProfile(VSlot.EncodedGpuProfile.data());
    }

    ++CompletedOuterTicks;
    CompletedSubsteps  += SafeSubsteps;
    SimulatedSeconds   += ClampedOuter;
    LatestOuterStepSec  = ClampedOuter;
    LatestSubstepCount  = SafeSubsteps;
    LatestTickWallMs    = static_cast<float>(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - StartClock).count());
}

RayCastReading JoltAdapter::CastWorldRay(const Vehicle::Vec3& Origin,
                                         const Vehicle::Vec3& DirectionAndLength,
                                         RigidBodyIdentity    IgnoredBody) const noexcept
{
    return WorldSolver.CastRay(Origin, DirectionAndLength, IgnoredBody);
}

bool JoltAdapter::SampleOccupantPose(uint64_t                 OccupantKey,
                                     float                    InterpolationFraction,
                                     AdapterOccupantSnapshot& OutSnapshot) const noexcept
{
    const auto Found = OccupantIndexByKey.find(OccupantKey);
    if (Found == OccupantIndexByKey.end()) return false;

    const float         Alpha = Vehicle::ClampScalar(InterpolationFraction, 0.0f, 1.0f);
    const OccupantSlot& Slot  = OccupantRecords[Found->second];

    OutSnapshot.OccupantKey          = Slot.Specification.OccupantKey;
    OutSnapshot.BodyIdentity         = Slot.BodyIdentity;
    OutSnapshot.RenderOccupantIndex  = Slot.Specification.RenderOccupantIndex;
    OutSnapshot.Motion               = Slot.Specification.Body.Motion;
    OutSnapshot.PreviousPosition     = Slot.PreviousPosition;
    OutSnapshot.PreviousOrientation  = Slot.PreviousOrientation;
    OutSnapshot.CurrentPosition      = Slot.CurrentPosition;
    OutSnapshot.CurrentOrientation   = Slot.CurrentOrientation;
    OutSnapshot.InterpolatedPosition = Slot.PreviousPosition + (Slot.CurrentPosition - Slot.PreviousPosition) * Alpha;
    OutSnapshot.InterpolatedOrient   = InterpolateOrientationSlerp(Slot.PreviousOrientation, Slot.CurrentOrientation, Alpha);
    OutSnapshot.LinearVelocityMps    = Slot.LinearVelocityMps;
    OutSnapshot.AngularVelocityRps   = Slot.AngularVelocityRps;
    OutSnapshot.PeakImpulseNs        = Slot.PeakImpulseNs;
    OutSnapshot.KineticEnergyJoules  = 0.5f * Slot.ResolvedMassKg * Slot.LinearVelocityMps.LengthSquared();
    OutSnapshot.AwakeCondition       = Slot.AwakeCondition;
    OutSnapshot.FracturedCondition   = Slot.FracturedCondition;
    return true;
}

bool JoltAdapter::SampleVehicleSnapshot(uint64_t                VehicleKey,
                                        float                   InterpolationFraction,
                                        AdapterVehicleSnapshot& OutSnapshot) const noexcept
{
    const auto Found = VehicleIndexByKey.find(VehicleKey);
    if (Found == VehicleIndexByKey.end()) return false;

    const float        Alpha = Vehicle::ClampScalar(InterpolationFraction, 0.0f, 1.0f);
    const VehicleSlot& Slot  = VehicleRecords[Found->second];

    OutSnapshot.VehicleKey            = Slot.VehicleKey;
    OutSnapshot.ChassisBody           = Slot.Rig ? Slot.Rig->QueryChassisBody() : InvalidRigidBody;
    OutSnapshot.PreviousChassisPos    = Slot.PreviousChassisPos;
    OutSnapshot.PreviousChassisOrient = Slot.PreviousChassisOrient;
    OutSnapshot.CurrentChassisPos     = Slot.CurrentChassisPos;
    OutSnapshot.CurrentChassisOrient  = Slot.CurrentChassisOrient;
    OutSnapshot.InterpolatedPos       = Slot.PreviousChassisPos + (Slot.CurrentChassisPos - Slot.PreviousChassisPos) * Alpha;
    OutSnapshot.InterpolatedOrient    = InterpolateOrientationSlerp(Slot.PreviousChassisOrient, Slot.CurrentChassisOrient, Alpha);
    OutSnapshot.PreviousWheelPos      = Slot.PreviousWheelPos;
    OutSnapshot.PreviousWheelOrient   = Slot.PreviousWheelOrient;
    OutSnapshot.CurrentWheelPos       = Slot.CurrentWheelPos;
    OutSnapshot.CurrentWheelOrient    = Slot.CurrentWheelOrient;
    OutSnapshot.WheelEnclosed         = Slot.WheelEnclosed;
    for (size_t W = 0; W < 4; ++W)
    {
        OutSnapshot.InterpolatedWheelPos[W] =
            Slot.PreviousWheelPos[W] + (Slot.CurrentWheelPos[W] - Slot.PreviousWheelPos[W]) * Alpha;
        OutSnapshot.InterpolatedWheelOrn[W] =
            InterpolateOrientationSlerp(Slot.PreviousWheelOrient[W], Slot.CurrentWheelOrient[W], Alpha);
    }
    if (Slot.Rig)
    {
        OutSnapshot.Telemetry               = Slot.Rig->QueryVehicle().QueryTelemetry();
        OutSnapshot.Telemetry.Chassis.PositionWorld    = OutSnapshot.InterpolatedPos;
        OutSnapshot.Telemetry.Chassis.OrientationWorld = OutSnapshot.InterpolatedOrient;
    }
    OutSnapshot.EncodedGpuProfile = Slot.EncodedGpuProfile;
    return true;
}

void JoltAdapter::BuildWorldSnapshot(WorldPhysicsSnapshot& OutSnapshot, float InterpolationFraction) const
{
    const float            Alpha   = Vehicle::ClampScalar(InterpolationFraction, 0.0f, 1.0f);
    const RigidBodyMetrics Metrics = WorldSolver.QueryMetrics();

    OutSnapshot.OuterTickIndex        = CompletedOuterTicks;
    OutSnapshot.TotalSubstepCount     = CompletedSubsteps;
    OutSnapshot.SimulatedSeconds      = SimulatedSeconds;
    OutSnapshot.OuterStepSeconds      = LatestOuterStepSec;
    OutSnapshot.SubstepsPerOuterTick  = LatestSubstepCount;
    OutSnapshot.InterpolationFraction = Alpha;
    OutSnapshot.ResidentBodyCount     = Metrics.BodyCount;
    OutSnapshot.AwakeBodyCount        = Metrics.ActiveBodyCount;
    OutSnapshot.StepWallMilliseconds  = LatestTickWallMs;

    OutSnapshot.Occupants.resize(OccupantRecords.size());
    for (size_t Idx = 0; Idx < OccupantRecords.size(); ++Idx)
    {
        (void)SampleOccupantPose(OccupantRecords[Idx].Specification.OccupantKey, Alpha, OutSnapshot.Occupants[Idx]);
    }

    OutSnapshot.Vehicles.resize(VehicleRecords.size());
    for (size_t Idx = 0; Idx < VehicleRecords.size(); ++Idx)
    {
        (void)SampleVehicleSnapshot(VehicleRecords[Idx].VehicleKey, Alpha, OutSnapshot.Vehicles[Idx]);
    }
}

void JoltAdapter::InterpolateWorldSnapshot(WorldPhysicsSnapshot& InOutSnapshot, float InterpolationFraction) noexcept
{
    const float Alpha                   = Vehicle::ClampScalar(InterpolationFraction, 0.0f, 1.0f);
    InOutSnapshot.InterpolationFraction = Alpha;

    for (AdapterOccupantSnapshot& Occ : InOutSnapshot.Occupants)
    {
        Occ.InterpolatedPosition = Occ.PreviousPosition + (Occ.CurrentPosition - Occ.PreviousPosition) * Alpha;
        Occ.InterpolatedOrient   = InterpolateOrientationSlerp(Occ.PreviousOrientation, Occ.CurrentOrientation, Alpha);
    }

    for (AdapterVehicleSnapshot& Veh : InOutSnapshot.Vehicles)
    {
        Veh.InterpolatedPos    = Veh.PreviousChassisPos + (Veh.CurrentChassisPos - Veh.PreviousChassisPos) * Alpha;
        Veh.InterpolatedOrient = InterpolateOrientationSlerp(Veh.PreviousChassisOrient, Veh.CurrentChassisOrient, Alpha);
        Veh.Telemetry.Chassis.PositionWorld    = Veh.InterpolatedPos;
        Veh.Telemetry.Chassis.OrientationWorld = Veh.InterpolatedOrient;
        for (size_t W = 0; W < 4; ++W)
        {
            Veh.InterpolatedWheelPos[W] =
                Veh.PreviousWheelPos[W] + (Veh.CurrentWheelPos[W] - Veh.PreviousWheelPos[W]) * Alpha;
            Veh.InterpolatedWheelOrn[W] =
                InterpolateOrientationSlerp(Veh.PreviousWheelOrient[W], Veh.CurrentWheelOrient[W], Alpha);
        }
    }
}

} // namespace Frontier
