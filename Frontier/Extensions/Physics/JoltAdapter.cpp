/*==========================================================================================================================================
                                                              JOLTADAPTER.CPP
==========================================================================================================================================*/
// 🧩 The PhysicsAdapter on Jolt: body shapes and mass, stepping, readings, placement, forces and the nearest-hit ray query.

#include "Frontier/Extensions/Physics/JoltAdapter.h"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

JPH::Vec3 ToVector(const std::array<double, 3>& Value)
{
    return JPH::Vec3(static_cast<float>(Value[0]), static_cast<float>(Value[1]), static_cast<float>(Value[2]));
}

JPH::RVec3 ToPosition(const std::array<double, 3>& Value)
{
    return JPH::RVec3(static_cast<JPH::Real>(Value[0]), static_cast<JPH::Real>(Value[1]), static_cast<JPH::Real>(Value[2]));
}

JPH::Quat ToRotation(const std::array<double, 4>& Value)
{
    return JPH::Quat(static_cast<float>(Value[0]), static_cast<float>(Value[1]), static_cast<float>(Value[2]), static_cast<float>(Value[3]))
        .Normalized();
}

} // namespace

std::unique_ptr<PhysicsAdapter> ConstructJoltAdapter(const PhysicsSpaceSettings& Settings)
{
    return std::make_unique<JoltAdapter>(Settings);
}

JoltAdapter::JoltAdapter(const PhysicsSpaceSettings& Settings) : Backend(Settings.Workers)
{
    const JPH::uint Capacity = std::max<JPH::uint>(16, Settings.MaximumBodies);
    Solver.Init(Capacity, 0, Capacity, Capacity, Backend.QueryLayers(), Backend.QueryLayerFilter(), Backend.QueryPairFilter());
    Solver.SetGravity(ToVector(Settings.GravityMps2));
    Solver.SetContactListener(&Contacts);
    BodyIndices.assign(Capacity, PhysicsNoBody);
}

JoltAdapter::~JoltAdapter()
{
    JPH::BodyInterface& Interface = Solver.GetBodyInterface();
    for (const JPH::BodyID& Body : Bodies)
    {
        Interface.RemoveBody(Body);
        Interface.DestroyBody(Body);
    }
}

JPH::RefConst<JPH::Shape> JoltAdapter::ConstructShape(const PhysicsBodyDescriptor& Descriptor) const
{
    switch (Descriptor.Shape)
    {
        case PhysicsShape::Sphere:
        {
            if (!(Descriptor.HalfExtentMetres[0] > 1.0e-3)) return nullptr;
            return new JPH::SphereShape(static_cast<float>(Descriptor.HalfExtentMetres[0]));
        }
        case PhysicsShape::Box:
        {
            const JPH::Vec3 Half = ToVector(Descriptor.HalfExtentMetres);
            if (!(Half.ReduceMin() > 1.0e-3f)) return nullptr;
            // 📝 The convex radius rounds the box's edges; it must stay below half the smallest side.
            return new JPH::BoxShape(Half, std::min(JPH::cDefaultConvexRadius, 0.5f * Half.ReduceMin()));
        }
        case PhysicsShape::Hull:
        {
            if (Descriptor.Points.size() < 4) return nullptr;
            JPH::Array<JPH::Vec3> Points;
            Points.reserve(Descriptor.Points.size());
            for (const auto& Point : Descriptor.Points) Points.push_back(JPH::Vec3(Point[0], Point[1], Point[2]));
            JPH::ConvexHullShapeSettings Settings(Points, JPH::cDefaultConvexRadius);
            const JPH::ShapeSettings::ShapeResult Result = Settings.Create();
            return Result.IsValid() ? Result.Get() : nullptr;
        }
        case PhysicsShape::Triangles:
        {
            if (Descriptor.Motion != PhysicsMotion::Fixed || Descriptor.Points.size() < 3 || Descriptor.Triangles.size() < 3) return nullptr;
            JPH::VertexList Corners;
            Corners.reserve(Descriptor.Points.size());
            for (const auto& Point : Descriptor.Points) Corners.push_back(JPH::Float3(Point[0], Point[1], Point[2]));
            JPH::IndexedTriangleList Faces;
            Faces.reserve(Descriptor.Triangles.size() / 3);
            for (size_t Index = 0; Index + 2 < Descriptor.Triangles.size(); Index += 3)
            {
                const uint32_t A = Descriptor.Triangles[Index], B = Descriptor.Triangles[Index + 1], C = Descriptor.Triangles[Index + 2];
                if (A >= Corners.size() || B >= Corners.size() || C >= Corners.size() || A == B || B == C || A == C) continue;
                Faces.push_back(JPH::IndexedTriangle(A, B, C, 0));
            }
            if (Faces.empty()) return nullptr;
            JPH::MeshShapeSettings          Settings(std::move(Corners), std::move(Faces));
            const JPH::ShapeSettings::ShapeResult Result = Settings.Create();
            return Result.IsValid() ? Result.Get() : nullptr;
        }
    }
    return nullptr;
}

uint32_t JoltAdapter::AddBody(const PhysicsBodyDescriptor& Descriptor)
{
    JPH::RefConst<JPH::Shape> Shape = ConstructShape(Descriptor);
    if (Shape == nullptr) return PhysicsNoBody;
    const bool Moving = Descriptor.Motion == PhysicsMotion::Moving;
    const bool Authored = Moving && Descriptor.MassKilograms > 0.0 && Descriptor.InertiaKgm2[0] > 0.0 && Descriptor.InertiaKgm2[1] > 0.0 &&
                          Descriptor.InertiaKgm2[2] > 0.0;
    if (Authored)
    {
        // 📝 An authored inertia is about the body's origin: the shape's own centre of mass moves onto it.
        const JPH::Vec3 Centre = Shape->GetCenterOfMass();
        if (Centre.LengthSq() > 1.0e-12f)
        {
            JPH::OffsetCenterOfMassShapeSettings Offset(-Centre, Shape);
            const JPH::ShapeSettings::ShapeResult Result = Offset.Create();
            if (!Result.IsValid()) return PhysicsNoBody;
            Shape = Result.Get();
        }
    }
    JPH::BodyCreationSettings Settings(Shape, ToPosition(Descriptor.PositionMetres), ToRotation(Descriptor.Orientation),
                                       Moving ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static, Moving ? JoltLayers::Moving : JoltLayers::Fixed);
    Settings.mFriction       = static_cast<float>(Descriptor.Friction);
    Settings.mRestitution    = static_cast<float>(Descriptor.Restitution);
    Settings.mLinearDamping  = static_cast<float>(Descriptor.LinearDamping);
    Settings.mAngularDamping = static_cast<float>(Descriptor.AngularDamping);
    Settings.mAllowSleeping  = Descriptor.Sleeps;
    Settings.mUserData       = Descriptor.Record;
    if (Moving)
    {
        Settings.mLinearVelocity  = ToVector(Descriptor.LinearVelocityMps);
        Settings.mAngularVelocity = ToVector(Descriptor.AngularVelocityRads);
        if (Descriptor.Swept) Settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
        if (Authored)
        {
            Settings.mOverrideMassProperties          = JPH::EOverrideMassProperties::MassAndInertiaProvided;
            Settings.mMassPropertiesOverride.mMass    = static_cast<float>(Descriptor.MassKilograms);
            Settings.mMassPropertiesOverride.mInertia = JPH::Mat44::sScale(ToVector(Descriptor.InertiaKgm2));
        }
        else if (Descriptor.MassKilograms > 0.0)
        {
            Settings.mOverrideMassProperties       = JPH::EOverrideMassProperties::CalculateInertia;
            Settings.mMassPropertiesOverride.mMass = static_cast<float>(Descriptor.MassKilograms);
        }
    }
    JPH::BodyInterface& Interface = Solver.GetBodyInterface();
    const JPH::BodyID   Body      = Interface.CreateAndAddBody(Settings, Moving ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
    if (Body.IsInvalid()) return PhysicsNoBody;
    const uint32_t Index = static_cast<uint32_t>(Bodies.size());
    Bodies.push_back(Body);
    if (Body.GetIndex() < BodyIndices.size()) BodyIndices[Body.GetIndex()] = Index;
    return Index;
}

void JoltAdapter::Prepare()
{
    Solver.OptimizeBroadPhase();
}

void JoltAdapter::Step(double Seconds, uint32_t CollisionSteps)
{
    if (!(Seconds > 0.0)) return;
    (void)Solver.Update(static_cast<float>(Seconds), static_cast<int>(std::max(1u, CollisionSteps)), &Backend.AccessTemporary(),
                        &Backend.AccessJobs());
}

PhysicsBodyReading JoltAdapter::ReadBody(uint32_t Index) const
{
    PhysicsBodyReading Reading;
    if (Index >= Bodies.size()) return Reading;
    const JPH::BodyInterface& Interface = Solver.GetBodyInterfaceNoLock();
    const JPH::BodyID         Body      = Bodies[Index];
    JPH::RVec3                Position;
    JPH::Quat                 Rotation;
    Interface.GetPositionAndRotation(Body, Position, Rotation);
    const JPH::Vec3 Linear  = Interface.GetLinearVelocity(Body);
    const JPH::Vec3 Angular = Interface.GetAngularVelocity(Body);
    Reading.PositionMetres      = { Position.GetX(), Position.GetY(), Position.GetZ() };
    Reading.Orientation         = { Rotation.GetX(), Rotation.GetY(), Rotation.GetZ(), Rotation.GetW() };
    Reading.LinearVelocityMps   = { Linear.GetX(), Linear.GetY(), Linear.GetZ() };
    Reading.AngularVelocityRads = { Angular.GetX(), Angular.GetY(), Angular.GetZ() };
    Reading.Awake               = Interface.IsActive(Body);
    return Reading;
}

void JoltAdapter::PlaceBody(uint32_t Index, const PhysicsBodyReading& Pose)
{
    if (Index >= Bodies.size()) return;
    JPH::BodyInterface& Interface = Solver.GetBodyInterfaceNoLock();
    Interface.SetPositionAndRotation(Bodies[Index], ToPosition(Pose.PositionMetres), ToRotation(Pose.Orientation), JPH::EActivation::Activate);
    Interface.SetLinearAndAngularVelocity(Bodies[Index], ToVector(Pose.LinearVelocityMps), ToVector(Pose.AngularVelocityRads));
}

void JoltAdapter::ApplyForce(uint32_t Index, const std::array<double, 3>& Force, const std::array<double, 3>& Point)
{
    if (Index >= Bodies.size()) return;
    Solver.GetBodyInterfaceNoLock().AddForce(Bodies[Index], ToVector(Force), ToPosition(Point));
}

void JoltAdapter::ApplyTorque(uint32_t Index, const std::array<double, 3>& Torque)
{
    if (Index >= Bodies.size()) return;
    Solver.GetBodyInterfaceNoLock().AddTorque(Bodies[Index], ToVector(Torque));
}

bool JoltAdapter::CastRay(const PhysicsRay& Ray, PhysicsRayHit& Hit) const
{
    const JPH::Vec3 Direction = ToVector(Ray.Direction);
    if (!(Ray.LengthMetres > 0.0) || Direction.LengthSq() < 1.0e-12f) return false;
    const JPH::RRayCast Cast{ ToPosition(Ray.OriginMetres), Direction.Normalized() * static_cast<float>(Ray.LengthMetres) };
    JPH::RayCastResult  Result;
    bool                Met = false;
    if (Ray.IgnoredBody < Bodies.size())
    {
        const JPH::IgnoreSingleBodyFilter Filter(Bodies[Ray.IgnoredBody]);
        Met = Solver.GetNarrowPhaseQueryNoLock().CastRay(Cast, Result, {}, {}, Filter);
    }
    else
    {
        Met = Solver.GetNarrowPhaseQueryNoLock().CastRay(Cast, Result);
    }
    if (!Met) return false;
    const JPH::RVec3 Point = Cast.GetPointOnRay(Result.mFraction);
    JPH::Vec3        Normal(0.0f, 0.0f, 1.0f);
    {
        JPH::BodyLockRead Lock(Solver.GetBodyLockInterfaceNoLock(), Result.mBodyID);
        if (Lock.Succeeded()) Normal = Lock.GetBody().GetWorldSpaceSurfaceNormal(Result.mSubShapeID2, Point);
    }
    Hit.PointMetres    = { static_cast<double>(Point.GetX()), static_cast<double>(Point.GetY()), static_cast<double>(Point.GetZ()) };
    Hit.Normal         = { Normal.GetX(), Normal.GetY(), Normal.GetZ() };
    Hit.DistanceMetres = static_cast<double>(Result.mFraction) * Ray.LengthMetres;
    const JPH::uint32 JoltIndex = Result.mBodyID.GetIndex();
    Hit.Body = JoltIndex < BodyIndices.size() ? BodyIndices[JoltIndex] : PhysicsNoBody;
    return true;
}

void JoltAdapter::AssignMass(uint32_t Index, double MassKilograms, const std::array<double, 3>& InertiaKgm2)
{
    if (Index >= Bodies.size() || !(MassKilograms > 0.0)) return;
    JPH::BodyLockWrite Lock(Solver.GetBodyLockInterfaceNoLock(), Bodies[Index]);
    if (!Lock.Succeeded() || !Lock.GetBody().IsDynamic()) return;
    JPH::MotionProperties* Motion = Lock.GetBody().GetMotionProperties();
    Motion->SetInverseMass(static_cast<float>(1.0 / MassKilograms));
    const JPH::Vec3 Inverse(InertiaKgm2[0] > 0.0 ? static_cast<float>(1.0 / InertiaKgm2[0]) : 0.0f, InertiaKgm2[1] > 0.0 ? static_cast<float>(1.0 / InertiaKgm2[1]) : 0.0f,
                            InertiaKgm2[2] > 0.0 ? static_cast<float>(1.0 / InertiaKgm2[2]) : 0.0f);
    Motion->SetInverseInertia(Inverse, JPH::Quat::sIdentity());
}

void JoltAdapter::Dispatch(uint32_t Count, const std::function<void(uint32_t)>& Task)
{
    if (Count == 0) return;
    if (Count == 1 || Backend.QueryWorkers() <= 1)
    {
        for (uint32_t Index = 0; Index < Count; ++Index) Task(Index);
        return;
    }
    // 📝 One job per item on Jolt's pool, gathered by a barrier; the caller's thread joins in while it waits.
    JPH::JobSystem&          Jobs    = Backend.AccessJobs();
    JPH::JobSystem::Barrier* Barrier = Jobs.CreateBarrier();
    for (uint32_t Index = 0; Index < Count; ++Index)
    {
        JPH::JobHandle Handle = Jobs.CreateJob("Frontier dispatch", JPH::Color::sGreen, [&Task, Index] { Task(Index); });
        Barrier->AddJob(Handle);
    }
    Jobs.WaitForJobs(Barrier);
    Jobs.DestroyBarrier(Barrier);
}

size_t JoltAdapter::QueryAwakeCount() const
{
    return Solver.GetNumActiveBodies(JPH::EBodyType::RigidBody);
}

} // namespace Frontier
