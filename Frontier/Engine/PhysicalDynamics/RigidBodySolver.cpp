/*==========================================================================================================================================
                                                            RIGIDBODYSOLVER.CPP
==========================================================================================================================================*/
// 🧩 Jolt Physics v5.2.0 rigid-body world implementation (the sole translation unit exposing JPH:: headers).

#include "Engine/PhysicalDynamics/RigidBodySolver.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <thread>

JPH_SUPPRESS_WARNINGS

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                            PROCESS-WIDE JOLT REGISTRATION
//------------------------------------------------------------------------------------------------------------------------

namespace
{

std::mutex RegistrationMutex;
uint32_t   RegistrationCount = 0u;

void ReportJoltTrace(const char* Format, ...)
{
    va_list Arguments;
    va_start(Arguments, Format);
    char ScratchText[1024];
    std::vsnprintf(ScratchText, sizeof(ScratchText), Format, Arguments);
    va_end(Arguments);
    std::fprintf(stderr, "[Jolt] %s\n", ScratchText);
}

#ifdef JPH_ENABLE_ASSERTS
bool HandleJoltAssertion(const char* Expression, const char* Message, const char* File, JPH::uint Line)
{
    std::fprintf(stderr, "[Jolt] %s:%u: (%s) %s\n", File, Line, Expression, Message != nullptr ? Message : "");
    return false;
}
#endif

bool AcquireJoltRegistration() noexcept
{
    std::lock_guard<std::mutex> Lock(RegistrationMutex);
    if (RegistrationCount++ > 0u) return true;

    JPH::RegisterDefaultAllocator();
    JPH::Trace = ReportJoltTrace;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = HandleJoltAssertion;)
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    return JPH::Factory::sInstance != nullptr;
}

void ReleaseJoltRegistration() noexcept
{
    std::lock_guard<std::mutex> Lock(RegistrationMutex);
    if (RegistrationCount == 0u || --RegistrationCount > 0u) return;

    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

namespace ObjectLayers
{
constexpr JPH::ObjectLayer NonMoving = 0;
constexpr JPH::ObjectLayer Moving    = 1;
constexpr JPH::uint        Count     = 2;
} // namespace ObjectLayers

namespace BroadPhaseLayers
{
constexpr JPH::BroadPhaseLayer NonMoving(0);
constexpr JPH::BroadPhaseLayer Moving(1);
constexpr JPH::uint            Count = 2;
} // namespace BroadPhaseLayers

[[nodiscard]] JPH::Vec3 ToJolt(const Vehicle::Vec3& Vector) noexcept
{
    return JPH::Vec3(Vector.x, Vector.y, Vector.z);
}

[[nodiscard]] JPH::RVec3 ToJoltReal(const Vehicle::Vec3& Vector) noexcept
{
    return JPH::RVec3(Vector.x, Vector.y, Vector.z);
}

[[nodiscard]] Vehicle::Vec3 FromJolt(JPH::Vec3Arg Vector) noexcept
{
    return {Vector.GetX(), Vector.GetY(), Vector.GetZ()};
}

[[nodiscard]] Vehicle::Vec3 FromJoltReal(JPH::RVec3Arg Vector) noexcept
{
    return {static_cast<float>(Vector.GetX()), static_cast<float>(Vector.GetY()), static_cast<float>(Vector.GetZ())};
}

[[nodiscard]] JPH::Quat ToJolt(const Vehicle::Quat& Rotation) noexcept
{
    const Vehicle::Quat Unit = Rotation.Normalized();
    return JPH::Quat(Unit.x, Unit.y, Unit.z, Unit.w);
}

[[nodiscard]] Vehicle::Quat FromJolt(JPH::QuatArg Rotation) noexcept
{
    return {Rotation.GetX(), Rotation.GetY(), Rotation.GetZ(), Rotation.GetW()};
}

[[nodiscard]] JPH::EMotionType ToJolt(RigidBodyMotionCategory Category) noexcept
{
    switch (Category)
    {
        case RigidBodyMotionCategory::Static:    return JPH::EMotionType::Static;
        case RigidBodyMotionCategory::Kinematic: return JPH::EMotionType::Kinematic;
        case RigidBodyMotionCategory::Dynamic:   return JPH::EMotionType::Dynamic;
    }
    return JPH::EMotionType::Dynamic;
}

[[nodiscard]] JPH::ShapeRefC ConstructJoltShape(const CollisionShapeSpecification& Specification, std::string& OutDiagnostic) noexcept
{
    JPH::ShapeSettings::ShapeResult ShapeResult;
    switch (Specification.Category)
    {
        case CollisionShapeCategory::Plane:
        {
            const Vehicle::Vec3 Normal = Specification.PlaneNormal.Normalized();
            if (Normal.LengthSquared() < 0.5f)
            {
                OutDiagnostic = "plane normal is degenerate";
                return nullptr;
            }
            const JPH::Plane Plane(ToJolt(Normal), -Specification.PlaneOffset);
            const JPH::PlaneShapeSettings PlaneSettings(Plane, nullptr, std::max(1.0f, Specification.PlaneHalfExtent));
            ShapeResult = PlaneSettings.Create();
            break;
        }
        case CollisionShapeCategory::Box:
        {
            const Vehicle::Vec3 Extents{
                std::max(0.01f, Specification.HalfExtents.x),
                std::max(0.01f, Specification.HalfExtents.y),
                std::max(0.01f, Specification.HalfExtents.z)};
            const float ConvexRadius = std::min({0.02f, Extents.x * 0.25f, Extents.y * 0.25f, Extents.z * 0.25f});
            const JPH::BoxShapeSettings BoxSettings(ToJolt(Extents), ConvexRadius);
            ShapeResult = BoxSettings.Create();
            break;
        }
        case CollisionShapeCategory::Sphere:
        {
            const JPH::SphereShapeSettings SphereSettings(std::max(0.005f, Specification.Radius));
            ShapeResult = SphereSettings.Create();
            break;
        }
        case CollisionShapeCategory::Capsule:
        {
            const JPH::CapsuleShapeSettings CapsuleSettings(std::max(0.005f, Specification.HalfHeight), std::max(0.005f, Specification.Radius));
            const auto YCapsule = CapsuleSettings.Create();
            if (YCapsule.HasError())
            {
                OutDiagnostic = YCapsule.GetError().c_str();
                return nullptr;
            }
            // 📝 Jolt capsules align along local +Y; rotate +90 deg about +X so local axis aligns with Frontier +Z.
            const JPH::RotatedTranslatedShapeSettings ZAligned(
                JPH::Vec3::sZero(), JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 0.5f * JPH::JPH_PI), YCapsule.Get());
            ShapeResult = ZAligned.Create();
            break;
        }
        case CollisionShapeCategory::Cylinder:
        {
            const float HalfHeight   = std::max(0.01f, Specification.HalfHeight);
            const float Radius       = std::max(0.01f, Specification.Radius);
            const float ConvexRadius = std::min({0.02f, HalfHeight * 0.25f, Radius * 0.25f});
            const JPH::CylinderShapeSettings CylinderSettings(HalfHeight, Radius, ConvexRadius);
            const auto YCylinder = CylinderSettings.Create();
            if (YCylinder.HasError())
            {
                OutDiagnostic = YCylinder.GetError().c_str();
                return nullptr;
            }
            const JPH::RotatedTranslatedShapeSettings ZAligned(
                JPH::Vec3::sZero(), JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 0.5f * JPH::JPH_PI), YCylinder.Get());
            ShapeResult = ZAligned.Create();
            break;
        }
        case CollisionShapeCategory::HeightField:
        {
            const uint32_t Count = Specification.HeightSampleCount;
            if (Count < 2u || Specification.HeightSamples.size() < static_cast<size_t>(Count) * static_cast<size_t>(Count))
            {
                OutDiagnostic = "heightfield requires SampleCount >= 2 and SampleCount^2 samples";
                return nullptr;
            }
            // 📝 Jolt HeightFieldShape spans (X, height=Y, Z); rotate +90 deg about +X and map Frontier (X, Y, height=Z).
            std::vector<float> JoltHeights(static_cast<size_t>(Count) * static_cast<size_t>(Count), 0.0f);
            for (uint32_t RowY = 0; RowY < Count; ++RowY)
            {
                for (uint32_t ColX = 0; ColX < Count; ++ColX)
                {
                    JoltHeights[static_cast<size_t>(RowY) * Count + ColX] =
                        Specification.HeightSamples[static_cast<size_t>(RowY) * Count + ColX];
                }
            }
            const JPH::HeightFieldShapeSettings HeightSettings(
                JoltHeights.data(),
                JPH::Vec3(Specification.HeightOrigin.x, Specification.HeightOrigin.z, -Specification.HeightOrigin.y),
                JPH::Vec3(Specification.HeightScale.x, Specification.HeightScale.z, Specification.HeightScale.y),
                Count);
            const auto YHeight = HeightSettings.Create();
            if (YHeight.HasError())
            {
                OutDiagnostic = YHeight.GetError().c_str();
                return nullptr;
            }
            const JPH::RotatedTranslatedShapeSettings ZHeight(
                JPH::Vec3::sZero(), JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 0.5f * JPH::JPH_PI), YHeight.Get());
            ShapeResult = ZHeight.Create();
            break;
        }
    }

    if (ShapeResult.HasError())
    {
        OutDiagnostic = ShapeResult.GetError().c_str();
        return nullptr;
    }

    JPH::ShapeRefC BaseShape = ShapeResult.Get();
    if (Specification.ShapeLocalOffset.LengthSquared() > 1.0e-10f)
    {
        const JPH::RotatedTranslatedShapeSettings OffsetSettings(
            ToJolt(Specification.ShapeLocalOffset), JPH::Quat::sIdentity(), BaseShape);
        const auto OffsetResult = OffsetSettings.Create();
        if (OffsetResult.HasError())
        {
            OutDiagnostic = OffsetResult.GetError().c_str();
            return nullptr;
        }
        BaseShape = OffsetResult.Get();
    }
    return BaseShape;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      JOLT WORLD
//------------------------------------------------------------------------------------------------------------------------

struct RigidBodySolver::JoltWorld
{
    struct BodySlot
    {
        JPH::BodyID             Body{};
        std::string             Name{};
        RigidBodyMotionCategory Motion = RigidBodyMotionCategory::Dynamic;
        CollisionShapeCategory  Shape  = CollisionShapeCategory::Box;
        bool                    Alive  = false;
    };

    JPH::BroadPhaseLayerInterfaceTable      BroadPhaseTable;
    JPH::ObjectLayerPairFilterTable         ObjectPairFilter;
    JPH::ObjectVsBroadPhaseLayerFilterTable BroadPhaseFilter;
    JPH::TempAllocatorImpl                  TemporaryAllocator;
    JPH::JobSystemThreadPool                Jobs;
    JPH::PhysicsSystem                      Physics;
    std::vector<BodySlot>                   Slots{};
    std::vector<uint32_t>                   FreeSlots{};

    explicit JoltWorld(const RigidBodyWorldSettings& Config)
        : BroadPhaseTable(ObjectLayers::Count, BroadPhaseLayers::Count)
        , ObjectPairFilter(ObjectLayers::Count)
        , BroadPhaseFilter(BroadPhaseTable, BroadPhaseLayers::Count, ObjectPairFilter, ObjectLayers::Count)
        , TemporaryAllocator(std::max(1024u * 1024u, Config.TemporaryAllocationBytes))
        , Jobs(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
               static_cast<int>(Config.WorkerThreads > 0u
                                    ? Config.WorkerThreads
                                    : std::clamp(std::thread::hardware_concurrency() > 2u ? std::thread::hardware_concurrency() - 2u : 1u, 1u, 4u)))
    {
        BroadPhaseTable.MapObjectToBroadPhaseLayer(ObjectLayers::NonMoving, BroadPhaseLayers::NonMoving);
        BroadPhaseTable.MapObjectToBroadPhaseLayer(ObjectLayers::Moving, BroadPhaseLayers::Moving);

        ObjectPairFilter.EnableCollision(ObjectLayers::Moving, ObjectLayers::NonMoving);
        ObjectPairFilter.EnableCollision(ObjectLayers::Moving, ObjectLayers::Moving);

        Physics.Init(
            std::max(16u, Config.MaxBodies),
            0u,
            std::max(64u, Config.MaxBodyPairs),
            std::max(64u, Config.MaxContactConstraints),
            BroadPhaseTable,
            BroadPhaseFilter,
            ObjectPairFilter);

        Physics.SetGravity(ToJolt(Config.Gravity));

        JPH::PhysicsSettings JoltSettings            = Physics.GetPhysicsSettings();
        JoltSettings.mSpeculativeContactDistance     = std::max(0.0f, Config.SpeculativeContactDistance);
        JoltSettings.mPenetrationSlop                = std::max(0.0f, Config.PenetrationSlop);
        JoltSettings.mTimeBeforeSleep                = std::max(0.05f, Config.TimeBeforeSleepSeconds);
        Physics.SetPhysicsSettings(JoltSettings);
    }

    [[nodiscard]] const BodySlot* Find(RigidBodyIdentity Identity) const noexcept
    {
        return (Identity < Slots.size() && Slots[Identity].Alive) ? &Slots[Identity] : nullptr;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                SOLVER LIFECYCLE & BODIES
//------------------------------------------------------------------------------------------------------------------------

RigidBodySolver::RigidBodySolver() noexcept = default;

RigidBodySolver::~RigidBodySolver() noexcept
{
    Retire();
}

bool RigidBodySolver::BringUp(const RigidBodyWorldSettings& Requested) noexcept
{
    Retire();
    if (!AcquireJoltRegistration())
    {
        LastDiagnostic = "Jolt factory registration failed";
        return false;
    }
    Settings      = Requested;
    Metrics       = RigidBodyMetrics{};
    Accumulator   = 0.0f;
    World         = std::make_unique<JoltWorld>(Settings);
    SolverReady   = true;
    LastDiagnostic.clear();
    return true;
}

void RigidBodySolver::Retire() noexcept
{
    if (!World) return;
    DestroyAllBodies();
    World.reset();
    SolverReady = false;
    ReleaseJoltRegistration();
}

const char* RigidBodySolver::QueryBackendVersion() noexcept
{
    static char Version[32] = {};
    if (Version[0] == '\0')
    {
        std::snprintf(Version, sizeof(Version), "Jolt %d.%d.%d", JPH_VERSION_MAJOR, JPH_VERSION_MINOR, JPH_VERSION_PATCH);
    }
    return Version;
}

RigidBodyIdentity RigidBodySolver::CreateBody(const RigidBodySpecification& Specification) noexcept
{
    if (!SolverReady)
    {
        LastDiagnostic = "solver not initialized";
        return InvalidRigidBody;
    }
    if (Specification.Shape.Category == CollisionShapeCategory::Plane && Specification.Motion != RigidBodyMotionCategory::Static)
    {
        LastDiagnostic = "plane collider must be static";
        return InvalidRigidBody;
    }

    const JPH::ShapeRefC Shape = ConstructJoltShape(Specification.Shape, LastDiagnostic);
    if (Shape == nullptr) return InvalidRigidBody;

    const bool Moving = Specification.Motion != RigidBodyMotionCategory::Static;
    JPH::BodyCreationSettings Creation(
        Shape,
        ToJoltReal(Specification.Position),
        ToJolt(Specification.Orientation),
        ToJolt(Specification.Motion),
        Moving ? ObjectLayers::Moving : ObjectLayers::NonMoving);

    Creation.mLinearVelocity  = ToJolt(Specification.LinearVelocity);
    Creation.mAngularVelocity = ToJolt(Specification.AngularVelocity);
    Creation.mFriction        = std::max(0.0f, Specification.Friction);
    Creation.mRestitution     = std::clamp(Specification.Restitution, 0.0f, 1.0f);
    Creation.mLinearDamping   = std::max(0.0f, Specification.LinearDamping);
    Creation.mAngularDamping  = std::max(0.0f, Specification.AngularDamping);
    Creation.mGravityFactor   = Specification.GravityFactor;
    Creation.mAllowSleeping   = Specification.AllowSleeping;
    Creation.mMotionQuality   = Specification.ContinuousCollision ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;

    if (Specification.Motion == RigidBodyMotionCategory::Dynamic && Specification.MassKilograms > 0.0f)
    {
        if (Specification.InertiaDiagonal.x > 0.0f && Specification.InertiaDiagonal.y > 0.0f && Specification.InertiaDiagonal.z > 0.0f)
        {
            Creation.mOverrideMassProperties         = JPH::EOverrideMassProperties::MassAndInertiaProvided;
            Creation.mMassPropertiesOverride.mMass   = Specification.MassKilograms;
            Creation.mMassPropertiesOverride.mInertia = JPH::Mat44::sScale(ToJolt(Specification.InertiaDiagonal));
        }
        else
        {
            Creation.mOverrideMassProperties       = JPH::EOverrideMassProperties::CalculateInertia;
            Creation.mMassPropertiesOverride.mMass = Specification.MassKilograms;
        }
    }

    JPH::BodyInterface& Bodies = World->Physics.GetBodyInterface();
    const JPH::BodyID   Body   = Bodies.CreateAndAddBody(Creation, Moving ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
    if (Body.IsInvalid())
    {
        LastDiagnostic = "Jolt refused body creation";
        return InvalidRigidBody;
    }

    uint32_t SlotIndex = 0u;
    if (!World->FreeSlots.empty())
    {
        SlotIndex = World->FreeSlots.back();
        World->FreeSlots.pop_back();
    }
    else
    {
        SlotIndex = static_cast<uint32_t>(World->Slots.size());
        World->Slots.emplace_back();
    }

    JoltWorld::BodySlot& Slot = World->Slots[SlotIndex];
    Slot.Body   = Body;
    Slot.Name   = Specification.Name;
    Slot.Motion = Specification.Motion;
    Slot.Shape  = Specification.Shape.Category;
    Slot.Alive  = true;
    Bodies.SetUserData(Body, SlotIndex);

    Metrics.BodyCount = World->Physics.GetNumBodies();
    return SlotIndex;
}

void RigidBodySolver::DestroyBody(RigidBodyIdentity Identity) noexcept
{
    if (!SolverReady) return;
    const JoltWorld::BodySlot* Slot = World->Find(Identity);
    if (Slot == nullptr) return;

    JPH::BodyInterface& Bodies = World->Physics.GetBodyInterface();
    if (Bodies.IsAdded(Slot->Body)) Bodies.RemoveBody(Slot->Body);
    Bodies.DestroyBody(Slot->Body);

    World->Slots[Identity] = JoltWorld::BodySlot{};
    World->FreeSlots.push_back(Identity);
    Metrics.BodyCount = World->Physics.GetNumBodies();
}

void RigidBodySolver::DestroyAllBodies() noexcept
{
    if (!World) return;
    JPH::BodyInterface& Bodies = World->Physics.GetBodyInterface();
    for (const JoltWorld::BodySlot& Slot : World->Slots)
    {
        if (!Slot.Alive) continue;
        if (Bodies.IsAdded(Slot.Body)) Bodies.RemoveBody(Slot.Body);
        Bodies.DestroyBody(Slot.Body);
    }
    World->Slots.clear();
    World->FreeSlots.clear();
    Metrics.BodyCount = World->Physics.GetNumBodies();
}

void RigidBodySolver::OptimizeBroadPhase() noexcept
{
    if (SolverReady) World->Physics.OptimizeBroadPhase();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 STEPPING & INTERACTION
//------------------------------------------------------------------------------------------------------------------------

void RigidBodySolver::StepExactly(float DeltaSeconds) noexcept
{
    if (!SolverReady || !(DeltaSeconds > 0.0f)) return;
    using Clock      = std::chrono::steady_clock;
    const auto Start = Clock::now();
    const auto Error = World->Physics.Update(
        DeltaSeconds,
        static_cast<int>(std::max(1u, Settings.CollisionStepsPerUpdate)),
        &World->TemporaryAllocator,
        &World->Jobs);
    Metrics.LastStepMilliseconds = std::chrono::duration<float, std::milli>(Clock::now() - Start).count();
    if (Error != JPH::EPhysicsUpdateError::None)
    {
        char ScratchText[128];
        std::snprintf(ScratchText, sizeof(ScratchText), "Jolt update error 0x%X", static_cast<unsigned>(Error));
        LastDiagnostic = ScratchText;
    }
    ++Metrics.StepCount;
    Metrics.StepsLastAdvance = 1u;
    Metrics.BodyCount        = World->Physics.GetNumBodies();
    Metrics.ActiveBodyCount  = World->Physics.GetNumActiveBodies(JPH::EBodyType::RigidBody);
}

void RigidBodySolver::Advance(float DeltaSeconds) noexcept
{
    Metrics.StepsLastAdvance = 0u;
    if (!SolverReady || !(DeltaSeconds > 0.0f)) return;

    Accumulator += DeltaSeconds;
    const float Limit = Settings.FixedStepSeconds * static_cast<float>(std::max(1u, Settings.MaxStepsPerAdvance));
    if (Accumulator > Limit) Accumulator = Limit;

    uint32_t Executed = 0u;
    while (Accumulator >= Settings.FixedStepSeconds)
    {
        StepExactly(Settings.FixedStepSeconds);
        Accumulator -= Settings.FixedStepSeconds;
        ++Executed;
    }
    Metrics.StepsLastAdvance   = Executed;
    Metrics.AccumulatorSeconds = Accumulator;
}

void RigidBodySolver::ApplyForce(RigidBodyIdentity Identity, const Vehicle::Vec3& ForceNewtons) noexcept
{
    if (!SolverReady || !ForceNewtons.Finite()) return;
    if (const JoltWorld::BodySlot* Slot = World->Find(Identity); Slot != nullptr && Slot->Motion == RigidBodyMotionCategory::Dynamic)
    {
        World->Physics.GetBodyInterface().AddForce(Slot->Body, ToJolt(ForceNewtons));
    }
}

void RigidBodySolver::ApplyForceAtPoint(RigidBodyIdentity Identity, const Vehicle::Vec3& ForceNewtons, const Vehicle::Vec3& WorldPoint) noexcept
{
    if (!SolverReady || !ForceNewtons.Finite() || !WorldPoint.Finite()) return;
    if (const JoltWorld::BodySlot* Slot = World->Find(Identity); Slot != nullptr && Slot->Motion == RigidBodyMotionCategory::Dynamic)
    {
        World->Physics.GetBodyInterface().AddForce(Slot->Body, ToJolt(ForceNewtons), ToJoltReal(WorldPoint));
    }
}

void RigidBodySolver::ApplyTorque(RigidBodyIdentity Identity, const Vehicle::Vec3& TorqueNewtonMetres) noexcept
{
    if (!SolverReady || !TorqueNewtonMetres.Finite()) return;
    if (const JoltWorld::BodySlot* Slot = World->Find(Identity); Slot != nullptr && Slot->Motion == RigidBodyMotionCategory::Dynamic)
    {
        World->Physics.GetBodyInterface().AddTorque(Slot->Body, ToJolt(TorqueNewtonMetres));
    }
}

void RigidBodySolver::ApplyImpulse(RigidBodyIdentity Identity, const Vehicle::Vec3& ImpulseNewtonSeconds) noexcept
{
    if (!SolverReady || !ImpulseNewtonSeconds.Finite()) return;
    if (const JoltWorld::BodySlot* Slot = World->Find(Identity); Slot != nullptr && Slot->Motion == RigidBodyMotionCategory::Dynamic)
    {
        World->Physics.GetBodyInterface().AddImpulse(Slot->Body, ToJolt(ImpulseNewtonSeconds));
    }
}

void RigidBodySolver::ApplyImpulseAtPoint(RigidBodyIdentity Identity, const Vehicle::Vec3& ImpulseNewtonSeconds, const Vehicle::Vec3& WorldPoint) noexcept
{
    if (!SolverReady || !ImpulseNewtonSeconds.Finite() || !WorldPoint.Finite()) return;
    if (const JoltWorld::BodySlot* Slot = World->Find(Identity); Slot != nullptr && Slot->Motion == RigidBodyMotionCategory::Dynamic)
    {
        World->Physics.GetBodyInterface().AddImpulse(Slot->Body, ToJolt(ImpulseNewtonSeconds), ToJoltReal(WorldPoint));
    }
}

void RigidBodySolver::AssignLinearVelocity(RigidBodyIdentity Identity, const Vehicle::Vec3& Velocity) noexcept
{
    if (!SolverReady || !Velocity.Finite()) return;
    if (const JoltWorld::BodySlot* Slot = World->Find(Identity); Slot != nullptr && Slot->Motion != RigidBodyMotionCategory::Static)
    {
        World->Physics.GetBodyInterface().SetLinearVelocity(Slot->Body, ToJolt(Velocity));
    }
}

void RigidBodySolver::AssignAngularVelocity(RigidBodyIdentity Identity, const Vehicle::Vec3& AngularVelocity) noexcept
{
    if (!SolverReady || !AngularVelocity.Finite()) return;
    if (const JoltWorld::BodySlot* Slot = World->Find(Identity); Slot != nullptr && Slot->Motion != RigidBodyMotionCategory::Static)
    {
        World->Physics.GetBodyInterface().SetAngularVelocity(Slot->Body, ToJolt(AngularVelocity));
    }
}

void RigidBodySolver::Teleport(RigidBodyIdentity Identity, const Vehicle::Vec3& Position, const Vehicle::Quat& Orientation) noexcept
{
    if (!SolverReady) return;
    if (const JoltWorld::BodySlot* Slot = World->Find(Identity); Slot != nullptr)
    {
        JPH::BodyInterface& Bodies = World->Physics.GetBodyInterface();
        Bodies.SetPositionAndRotation(
            Slot->Body,
            ToJoltReal(Position),
            ToJolt(Orientation),
            Slot->Motion == RigidBodyMotionCategory::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
        if (Slot->Motion == RigidBodyMotionCategory::Dynamic)
        {
            Bodies.SetLinearAndAngularVelocity(Slot->Body, JPH::Vec3::sZero(), JPH::Vec3::sZero());
        }
    }
}

void RigidBodySolver::MoveKinematic(RigidBodyIdentity Identity, const Vehicle::Vec3& Position, const Vehicle::Quat& Orientation, float DeltaSeconds) noexcept
{
    if (!SolverReady || !(DeltaSeconds > 0.0f)) return;
    if (const JoltWorld::BodySlot* Slot = World->Find(Identity); Slot != nullptr && Slot->Motion == RigidBodyMotionCategory::Kinematic)
    {
        World->Physics.GetBodyInterface().MoveKinematic(Slot->Body, ToJoltReal(Position), ToJolt(Orientation), DeltaSeconds);
    }
}

void RigidBodySolver::Activate(RigidBodyIdentity Identity) noexcept
{
    if (!SolverReady) return;
    if (const JoltWorld::BodySlot* Slot = World->Find(Identity); Slot != nullptr && Slot->Motion != RigidBodyMotionCategory::Static)
    {
        World->Physics.GetBodyInterface().ActivateBody(Slot->Body);
    }
}

bool RigidBodySolver::QueryPose(RigidBodyIdentity Identity, RigidBodyPose& OutPose) const noexcept
{
    if (!SolverReady) return false;
    const JoltWorld::BodySlot* Slot = World->Find(Identity);
    if (Slot == nullptr) return false;

    const JPH::BodyInterface& Bodies = World->Physics.GetBodyInterface();
    JPH::RVec3 Position;
    JPH::Quat  Orientation;
    Bodies.GetPositionAndRotation(Slot->Body, Position, Orientation);

    OutPose.Identity        = Identity;
    OutPose.Position        = FromJoltReal(Position);
    OutPose.Orientation     = FromJolt(Orientation);
    OutPose.LinearVelocity  = FromJolt(Bodies.GetLinearVelocity(Slot->Body));
    OutPose.AngularVelocity = FromJolt(Bodies.GetAngularVelocity(Slot->Body));
    OutPose.Motion          = Slot->Motion;
    OutPose.Shape           = Slot->Shape;
    OutPose.Awake           = Bodies.IsActive(Slot->Body);
    return true;
}

RayCastReading RigidBodySolver::CastRay(const Vehicle::Vec3& Origin, const Vehicle::Vec3& DirectionAndLength,
                                        RigidBodyIdentity IgnoredBody) const noexcept
{
    RayCastReading Result{};
    if (!SolverReady || DirectionAndLength.LengthSquared() < 1.0e-12f) return Result;

    const JPH::RRayCast Ray(ToJoltReal(Origin), ToJolt(DirectionAndLength));
    JPH::RayCastResult  Hit;

    bool Intersected = false;
    if (const JoltWorld::BodySlot* Ignored = World->Find(IgnoredBody); Ignored != nullptr)
    {
        const JPH::IgnoreSingleBodyFilter BodyFilter(Ignored->Body);
        Intersected = World->Physics.GetNarrowPhaseQuery().CastRay(Ray, Hit, {}, {}, BodyFilter);
    }
    else
    {
        Intersected = World->Physics.GetNarrowPhaseQuery().CastRay(Ray, Hit);
    }

    if (!Intersected) return Result;

    Result.Hit      = true;
    Result.Fraction = Hit.mFraction;
    const JPH::RVec3 HitPoint = Ray.GetPointOnRay(Hit.mFraction);
    Result.Position           = FromJoltReal(HitPoint);

    JPH::BodyLockRead Lock(World->Physics.GetBodyLockInterface(), Hit.mBodyID);
    if (Lock.Succeeded())
    {
        const JPH::Body& Body = Lock.GetBody();
        Result.Normal         = FromJolt(Body.GetWorldSpaceSurfaceNormal(Hit.mSubShapeID2, HitPoint)).Normalized();
        Result.Body           = static_cast<RigidBodyIdentity>(Body.GetUserData());
    }
    return Result;
}

} // namespace Frontier
