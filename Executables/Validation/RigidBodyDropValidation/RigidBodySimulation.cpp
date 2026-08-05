/*============================================================================================================================================
                                                           RIGIDBODYSIMULATION.CPP
============================================================================================================================================*/
// 🧩 Jolt lifetime, world construction from the decoded documents, fixed-interval advance, and quaternion -> instance-matrix writeback.
//    Everything JPH lives in this translation unit; the header exposes only plain structs so the render host never sees a Jolt type.

#include "RigidBodySimulation.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>

#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        JOLT LAYER INTERFACES
//------------------------------------------------------------------------------------------------------------------------

namespace ObjectLayers
{
    static constexpr JPH::ObjectLayer Resting = 0;   // static ground: never collides with another resting body
    static constexpr JPH::ObjectLayer Falling = 1;   // dynamic crates + wrecker
    static constexpr JPH::ObjectLayer Count   = 2;
}

namespace BroadLayers
{
    static constexpr JPH::BroadPhaseLayer Resting(0);
    static constexpr JPH::BroadPhaseLayer Falling(1);
    static constexpr JPH::uint            Count = 2;
}

// Two resting bodies never need a contact; everything else does.
class ObjectPairPredicate final : public JPH::ObjectLayerPairFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer First, JPH::ObjectLayer Second) const override
    {
        return First == ObjectLayers::Falling || Second == ObjectLayers::Falling;
    }
};

class BroadLayerMapping final : public JPH::BroadPhaseLayerInterface
{
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadLayers::Count; }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer Layer) const override
    {
        return Layer == ObjectLayers::Falling ? BroadLayers::Falling : BroadLayers::Resting;
    }

    // 🔴 Only a virtual when the profiler is compiled in. jolt.lib carries features 0, so this override must stay guarded or it fails to compile.
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer Layer) const override
    {
        return Layer == BroadLayers::Falling ? "falling" : "resting";
    }
#endif
};

class BroadPairPredicate final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer Layer, JPH::BroadPhaseLayer Broad) const override
    {
        return Layer == ObjectLayers::Falling || Broad == BroadLayers::Falling;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          OWNED JOLT WORLD
//------------------------------------------------------------------------------------------------------------------------

// 📝 Everything Jolt owns, hidden behind RigidBodySimulation::Internals. Declaration order matters on teardown: the world must outlive the
//    allocators it borrows, so it is declared LAST and therefore destroyed FIRST.
struct RigidBodyInternals
{
    ObjectPairPredicate  ObjectPairs      = {};
    BroadLayerMapping    BroadMapping     = {};
    BroadPairPredicate   BroadPairs       = {};
    JPH::TempAllocatorImpl        Scratch;
    JPH::JobSystemThreadPool      Jobs;
    JPH::PhysicsSystem            World;
    std::vector<JPH::BodyID>      BodyIds  = {};

    RigidBodyInternals()
        : Scratch(16 * 1024 * 1024)
        , Jobs(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, 2)
    {
    }
};

// Density of every dynamic box, so mass follows authored size: the 3 m wrecker masses 27x a 1 m crate and drives the tower apart on impact.
static constexpr float BoxDensity = 420.0f;   // [kg/m³] - crate-like, a touch under water

//------------------------------------------------------------------------------------------------------------------------
//                                                        TRANSFORM COMPOSITION
//------------------------------------------------------------------------------------------------------------------------

// Compose a column-major 4x4 from a rotation quaternion, a translation, and a per-axis scale, matching the layout the decoder writes
// (index Column*4 + Row, translation in the last column). This is the Euler bypass: the solved quaternion goes straight to a basis.
static void ComposeSolvedModel(const JPH::Quat& Rotation, const JPH::RVec3& Translation, const float Scale[3], float OutModel[16])
{
    const JPH::Mat44 Basis = JPH::Mat44::sRotation(Rotation);

    for (uint32_t ColumnIterator = 0; ColumnIterator < 3u; ++ColumnIterator)
    {
        const JPH::Vec3 Axis = Basis.GetColumn3((JPH::uint)ColumnIterator) * Scale[ColumnIterator];
        OutModel[ColumnIterator * 4 + 0] = Axis.GetX();
        OutModel[ColumnIterator * 4 + 1] = Axis.GetY();
        OutModel[ColumnIterator * 4 + 2] = Axis.GetZ();
        OutModel[ColumnIterator * 4 + 3] = 0.0f;
    }

    OutModel[12] = (float)Translation.GetX();
    OutModel[13] = (float)Translation.GetY();
    OutModel[14] = (float)Translation.GetZ();
    OutModel[15] = 1.0f;
}

// Normal basis: the rotation columns divided by scale (the inverse-transpose of a rotation-and-uniform-scale basis), three vec3 rows each padded
// to vec4 the way the GPU record expects. Uniform scale here, so a plain reciprocal is exact.
static void ComposeSolvedNormalBasis(const JPH::Quat& Rotation, const float Scale[3], float OutBasis[12])
{
    const JPH::Mat44 Basis = JPH::Mat44::sRotation(Rotation);

    for (uint32_t ColumnIterator = 0; ColumnIterator < 3u; ++ColumnIterator)
    {
        const float Extent    = Scale[ColumnIterator];
        const float Reciprocal = (std::fabs(Extent) > 1.0e-8f) ? (1.0f / Extent) : 0.0f;
        const JPH::Vec3 Axis   = Basis.GetColumn3((JPH::uint)ColumnIterator) * Reciprocal;
        OutBasis[ColumnIterator * 4 + 0] = Axis.GetX();
        OutBasis[ColumnIterator * 4 + 1] = Axis.GetY();
        OutBasis[ColumnIterator * 4 + 2] = Axis.GetZ();
        OutBasis[ColumnIterator * 4 + 3] = 0.0f;
    }
}

// Analytic inverse of the composed model: S⁻¹ * Rᵀ * T⁻¹, same shape as the decoder's ComposeInverseModelMatrix. A degenerate axis yields a zero
// row rather than an infinity, so a collapsed scale cannot poison every ray that reads this record with a NaN.
static void ComposeSolvedInverseModel(const JPH::Quat& Rotation, const JPH::RVec3& Translation, const float Scale[3], float OutInverse[16])
{
    const JPH::Mat44 Basis = JPH::Mat44::sRotation(Rotation);

    float Reciprocal[3] = { 0.0f, 0.0f, 0.0f };
    for (uint32_t AxisIterator = 0; AxisIterator < 3u; ++AxisIterator)
        Reciprocal[AxisIterator] = (std::fabs(Scale[AxisIterator]) > 1.0e-8f) ? (1.0f / Scale[AxisIterator]) : 0.0f;

    // Rows of Rᵀ are the columns of R; scaling row i by 1/s_i gives S⁻¹Rᵀ.
    float Linear[3][3] = {};
    for (uint32_t RowIterator = 0; RowIterator < 3u; ++RowIterator)
    {
        const JPH::Vec3 Axis = Basis.GetColumn3((JPH::uint)RowIterator);
        Linear[RowIterator][0] = Axis.GetX() * Reciprocal[RowIterator];
        Linear[RowIterator][1] = Axis.GetY() * Reciprocal[RowIterator];
        Linear[RowIterator][2] = Axis.GetZ() * Reciprocal[RowIterator];
    }

    for (uint32_t ColumnIterator = 0; ColumnIterator < 3u; ++ColumnIterator)
    {
        OutInverse[ColumnIterator * 4 + 0] = Linear[0][ColumnIterator];
        OutInverse[ColumnIterator * 4 + 1] = Linear[1][ColumnIterator];
        OutInverse[ColumnIterator * 4 + 2] = Linear[2][ColumnIterator];
        OutInverse[ColumnIterator * 4 + 3] = 0.0f;
    }

    // Translation column: -S⁻¹Rᵀ t
    const float Position[3] = { (float)Translation.GetX(), (float)Translation.GetY(), (float)Translation.GetZ() };
    for (uint32_t RowIterator = 0; RowIterator < 3u; ++RowIterator)
    {
        OutInverse[12 + RowIterator] = -(Linear[RowIterator][0] * Position[0]
                                       + Linear[RowIterator][1] * Position[1]
                                       + Linear[RowIterator][2] * Position[2]);
    }
    OutInverse[15] = 1.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          WORLD CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

// Turn an authored yaw in degrees into the spawn quaternion. The authored scene only ever turns about +Z, so reading Rotation[2] alone is exact
// here — and once the solver owns the body, orientation never round-trips through Euler again.
static JPH::Quat SpawnRotationFromYaw(float YawDegrees)
{
    return JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), YawDegrees * 3.14159265358979f / 180.0f);
}

bool InitializeRigidBodySimulation(RigidBodySimulation& Simulation, const RigidBodySceneProportions& Proportions,
                                  const WorkspaceDocument& CrateDocument, uint32_t CrateSlotBase,
                                  const WorkspaceDocument& GroundDocument, uint32_t GroundSlotBase,
                                  const RigidBodyTuning& Tuning)
{
    FinalizeRigidBodySimulation(Simulation);

    JPH::RegisterDefaultAllocator();
    if (JPH::Factory::sInstance == nullptr)
        JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    RigidBodyInternals* Internals = new RigidBodyInternals();

    // Room for the whole authored population plus headroom; body pairs and contact constraints scale with a collapsing stack's contact count.
    const JPH::uint BodyCeiling = (JPH::uint)(CrateDocument.Objects.size() + GroundDocument.Objects.size() + 64u);
    Internals->World.Init(BodyCeiling, 0, BodyCeiling * 8u, BodyCeiling * 8u,
                          Internals->BroadMapping, Internals->BroadPairs, Internals->ObjectPairs);
    Internals->World.SetGravity(JPH::Vec3(0.0f, 0.0f, -Tuning.GravityStrength));   // 🔴 world is Z-up: gravity is -Z, not -Y

    JPH::BodyInterface& Bodies = Internals->World.GetBodyInterface();

    // --- Static ground, one body per object in the ground document -----------------------------------------------------
    // The slab is authored with its top face on z = 0 and its object sits at identity, so its collision box is centred half a depth BELOW the
    // ground plane. Deriving that from the proportions (rather than the placement) keeps the collider flush with the rendered slab.
    const float GroundHalfSpan  = Proportions.GroundSpan * 0.5f;
    const float GroundHalfDepth = Proportions.GroundDepth * 0.5f;
    for (size_t ObjectIterator = 0; ObjectIterator < GroundDocument.Objects.size(); ++ObjectIterator)
    {
        const WorkspaceObject& Object = GroundDocument.Objects[ObjectIterator];

        JPH::BoxShapeSettings SlabShape(JPH::Vec3(GroundHalfSpan * Object.Placement.Scale[0],
                                                  GroundHalfSpan * Object.Placement.Scale[1],
                                                  GroundHalfDepth * Object.Placement.Scale[2]));
        SlabShape.SetEmbedded();

        JPH::BodyCreationSettings SlabSettings(SlabShape.Create().Get(),
                                               JPH::RVec3(Object.Placement.Location[0],
                                                          Object.Placement.Location[1],
                                                          Object.Placement.Location[2] - GroundHalfDepth),
                                               SpawnRotationFromYaw(Object.Placement.Rotation[2]),
                                               JPH::EMotionType::Static, ObjectLayers::Resting);
        SlabSettings.mFriction = Tuning.Friction;
        Bodies.CreateAndAddBody(SlabSettings, JPH::EActivation::DontActivate);

        // The ground never moves, so it gets NO link — its authored instance record is already correct and re-writing it every frame would be
        // pure cost. Only dynamic bodies earn a writeback slot.
        (void)GroundSlotBase;
    }

    // --- Dynamic crates + wrecker, one body per object in the crate document -------------------------------------------
    const float CrateHalfEdge = Proportions.CrateEdge * 0.5f;
    Simulation.Links.reserve(CrateDocument.Objects.size());
    for (size_t ObjectIterator = 0; ObjectIterator < CrateDocument.Objects.size(); ++ObjectIterator)
    {
        const WorkspaceObject& Object = CrateDocument.Objects[ObjectIterator];

        // Half-extents come from the AUTHORED scale, so the collider always matches the box the raster draws — including the scaled-up wrecker,
        // which is the same geometry block at a larger scale and needs no special case.
        const float HalfExtent[3] =
        {
            CrateHalfEdge * Object.Placement.Scale[0],
            CrateHalfEdge * Object.Placement.Scale[1],
            CrateHalfEdge * Object.Placement.Scale[2],
        };

        JPH::BoxShapeSettings CrateShape(JPH::Vec3(HalfExtent[0], HalfExtent[1], HalfExtent[2]));
        CrateShape.SetEmbedded();

        JPH::BodyCreationSettings CrateSettings(CrateShape.Create().Get(),
                                                JPH::RVec3(Object.Placement.Location[0],
                                                           Object.Placement.Location[1],
                                                           Object.Placement.Location[2]),
                                                SpawnRotationFromYaw(Object.Placement.Rotation[2]),
                                                JPH::EMotionType::Dynamic, ObjectLayers::Falling);
        CrateSettings.mFriction    = Tuning.Friction;
        CrateSettings.mRestitution = Tuning.Restitution;
        CrateSettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        CrateSettings.mMassPropertiesOverride.mMass = 8.0f * HalfExtent[0] * HalfExtent[1] * HalfExtent[2] * BoxDensity;

        const JPH::BodyID Constructed = Bodies.CreateAndAddBody(CrateSettings, JPH::EActivation::Activate);

        RigidBodyLink Link;
        Link.SceneSlot       = CrateSlotBase + (uint32_t)ObjectIterator;
        Link.BodyOrdinal     = (uint32_t)Internals->BodyIds.size();
        Link.HalfExtent[0]   = HalfExtent[0];
        Link.HalfExtent[1]   = HalfExtent[1];
        Link.HalfExtent[2]   = HalfExtent[2];
        Link.Mass            = CrateSettings.mMassPropertiesOverride.mMass;
        Link.SeedLocation[0] = Object.Placement.Location[0];
        Link.SeedLocation[1] = Object.Placement.Location[1];
        Link.SeedLocation[2] = Object.Placement.Location[2];
        Link.SeedYaw         = Object.Placement.Rotation[2];
        Simulation.Links.push_back(Link);

        Internals->BodyIds.push_back(Constructed);
    }

    Internals->World.OptimizeBroadPhase();

    Simulation.Internals          = Internals;
    Simulation.SettleResidue      = 0.0f;
    Simulation.Readout            = RigidBodyReadout{};
    Simulation.Readout.BodyCount  = (uint32_t)Simulation.Links.size();
    return true;
}

void FinalizeRigidBodySimulation(RigidBodySimulation& Simulation)
{
    if (Simulation.Internals == nullptr)
        return;

    RigidBodyInternals* Internals = (RigidBodyInternals*)Simulation.Internals;
    JPH::BodyInterface& Bodies    = Internals->World.GetBodyInterface();
    for (size_t BodyIterator = 0; BodyIterator < Internals->BodyIds.size(); ++BodyIterator)
    {
        Bodies.RemoveBody(Internals->BodyIds[BodyIterator]);
        Bodies.DestroyBody(Internals->BodyIds[BodyIterator]);
    }

    delete Internals;
    Simulation.Internals = nullptr;
    Simulation.Links.clear();
    Simulation.Readout   = RigidBodyReadout{};

    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                              ADVANCE
//------------------------------------------------------------------------------------------------------------------------

// Refresh the readout from the live bodies. Energy is ½mv² plus a box's rotational term about its own centre, which is enough to show the settle
// curve flatten without reaching into MotionProperties for the exact inertia tensor.
static void SurveyRigidBodies(RigidBodySimulation& Simulation)
{
    RigidBodyInternals* Internals = (RigidBodyInternals*)Simulation.Internals;
    const JPH::BodyInterface& Bodies = Internals->World.GetBodyInterface();

    uint32_t Awake       = 0u;
    float    Energy      = 0.0f;
    float    Highest     = 0.0f;
    bool     HighestSeen = false;

    for (size_t LinkIterator = 0; LinkIterator < Simulation.Links.size(); ++LinkIterator)
    {
        const RigidBodyLink& Link = Simulation.Links[LinkIterator];
        const JPH::BodyID    Id   = Internals->BodyIds[Link.BodyOrdinal];

        if (Bodies.IsActive(Id))
            ++Awake;

        const JPH::Vec3 Linear  = Bodies.GetLinearVelocity(Id);
        const JPH::Vec3 Angular = Bodies.GetAngularVelocity(Id);

        // Solid-box inertia about each principal axis: m(b² + c²)/12 over the full extents. Averaged into one scalar for the readout.
        const float FullX     = 2.0f * Link.HalfExtent[0];
        const float FullY     = 2.0f * Link.HalfExtent[1];
        const float FullZ     = 2.0f * Link.HalfExtent[2];
        const float InertiaX  = Link.Mass * (FullY * FullY + FullZ * FullZ) / 12.0f;
        const float InertiaY  = Link.Mass * (FullX * FullX + FullZ * FullZ) / 12.0f;
        const float InertiaZ  = Link.Mass * (FullX * FullX + FullY * FullY) / 12.0f;

        Energy += 0.5f * Link.Mass * Linear.LengthSq();
        Energy += 0.5f * (InertiaX * Angular.GetX() * Angular.GetX()
                        + InertiaY * Angular.GetY() * Angular.GetY()
                        + InertiaZ * Angular.GetZ() * Angular.GetZ());

        const float Height = (float)Bodies.GetCenterOfMassPosition(Id).GetZ();
        if (!HighestSeen || Height > Highest)
        {
            Highest     = Height;
            HighestSeen = true;
        }
    }

    Simulation.Readout.BodyCount     = (uint32_t)Simulation.Links.size();
    Simulation.Readout.AwakeCount    = Awake;
    Simulation.Readout.KineticEnergy = Energy;
    // 📝 With no bodies there is no tallest one, and reporting 0 m would be indistinguishable from a crate resting exactly at the origin. Publish a
    //    quiet NaN instead so the window can render it as "—": an empty world is a DIFFERENT reading from a settled one, not a zero.
    Simulation.Readout.HighestCrate  = HighestSeen ? Highest : std::numeric_limits<float>::quiet_NaN();
}

void AdvanceRigidBodySimulation(RigidBodySimulation& Simulation, const RigidBodyTuning& Tuning, float RealSeconds)
{
    if (Simulation.Internals == nullptr)
        return;

    RigidBodyInternals* Internals = (RigidBodyInternals*)Simulation.Internals;

    if (!Tuning.Advancing)
    {
        Simulation.SettleResidue = 0.0f;   // drop banked time so resuming does not fast-forward through the pause
        SurveyRigidBodies(Simulation);
        return;
    }

    Internals->World.SetGravity(JPH::Vec3(0.0f, 0.0f, -Tuning.GravityStrength));

    const float StepSeconds = (Tuning.StepSeconds > 1.0e-4f) ? Tuning.StepSeconds : 1.0e-4f;
    const int   Substeps    = (Tuning.Substeps > 0) ? Tuning.Substeps : 1;

    // Bank the real time and consume it in fixed intervals, so the solve is frame-rate independent. Capped at 4 steps per call: a hitch (or a
    // debugger break) otherwise banks seconds of time and the catch-up spikes the frame that resumes.
    Simulation.SettleResidue += (RealSeconds > 0.0f && RealSeconds < 0.25f) ? RealSeconds : StepSeconds;

    uint32_t Consumed = 0u;
    while (Simulation.SettleResidue >= StepSeconds && Consumed < 4u)
    {
        Internals->World.Update(StepSeconds, Substeps, &Internals->Scratch, &Internals->Jobs);
        Simulation.SettleResidue -= StepSeconds;
        Simulation.Readout.ElapsedSeconds += StepSeconds;
        ++Simulation.Readout.AdvanceOrdinal;
        ++Consumed;
    }
    if (Simulation.SettleResidue > StepSeconds * 4.0f)
        Simulation.SettleResidue = 0.0f;   // shed an unrecoverable backlog rather than carrying it forever

    SurveyRigidBodies(Simulation);
}

void ReseedRigidBodySimulation(RigidBodySimulation& Simulation, const RigidBodyTuning& Tuning)
{
    if (Simulation.Internals == nullptr)
        return;

    RigidBodyInternals* Internals = (RigidBodyInternals*)Simulation.Internals;
    JPH::BodyInterface& Bodies    = Internals->World.GetBodyInterface();

    for (size_t LinkIterator = 0; LinkIterator < Simulation.Links.size(); ++LinkIterator)
    {
        const RigidBodyLink& Link = Simulation.Links[LinkIterator];
        const JPH::BodyID    Id   = Internals->BodyIds[Link.BodyOrdinal];

        Bodies.SetPositionRotationAndVelocity(Id,
                                              JPH::RVec3(Link.SeedLocation[0], Link.SeedLocation[1], Link.SeedLocation[2]),
                                              SpawnRotationFromYaw(Link.SeedYaw),
                                              JPH::Vec3::sZero(), JPH::Vec3::sZero());
        Bodies.SetFriction(Id, Tuning.Friction);
        Bodies.SetRestitution(Id, Tuning.Restitution);
        Bodies.ActivateBody(Id);
    }

    Simulation.SettleResidue = 0.0f;
    Simulation.Readout       = RigidBodyReadout{};
    SurveyRigidBodies(Simulation);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                             WRITEBACK
//------------------------------------------------------------------------------------------------------------------------

void TransferRigidBodyTransforms(const RigidBodySimulation& Simulation, std::vector<SuzanneSceneInstance>& Instances)
{
    if (Simulation.Internals == nullptr)
        return;

    RigidBodyInternals* Internals = (RigidBodyInternals*)Simulation.Internals;
    const JPH::BodyInterface& Bodies = Internals->World.GetBodyInterface();

    for (size_t LinkIterator = 0; LinkIterator < Simulation.Links.size(); ++LinkIterator)
    {
        const RigidBodyLink& Link = Simulation.Links[LinkIterator];
        if (Link.SceneSlot >= Instances.size())
            continue;

        JPH::RVec3 Translation = JPH::RVec3::sZero();
        JPH::Quat  Rotation    = JPH::Quat::sIdentity();
        Bodies.GetPositionAndRotation(Internals->BodyIds[Link.BodyOrdinal], Translation, Rotation);

        // The authored cube is a UNIT box centred on its origin, so the render scale that reproduces this collider is its half-extent doubled.
        const float Scale[3] = { Link.HalfExtent[0] * 2.0f, Link.HalfExtent[1] * 2.0f, Link.HalfExtent[2] * 2.0f };

        SuzanneSceneInstance& Instance = Instances[Link.SceneSlot];
        ComposeSolvedModel(Rotation, Translation, Scale, Instance.Model);
        ComposeSolvedNormalBasis(Rotation, Scale, Instance.NormalBasis);
        ComposeSolvedInverseModel(Rotation, Translation, Scale, Instance.InverseModel);
    }
}

} // namespace Frontier
