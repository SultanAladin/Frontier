/*==========================================================================================================================================
                                                                 AGENT.CPP
==========================================================================================================================================*/
// 🧩 Controllable scene Agent kinematics: the spawn stand it was placed on, the spawn brake rules that outrank its driver for the opening
//    seconds, and the WorldSpace VehicleRecord synchronization.

#include "Frontier/Engine/Scene/Agent.h"
#include "Frontier/Engine/Scene/WorldSpace.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

Agent::Agent(RecordToken BoundRecordToken, uint64_t OwnerPlayerTokenValue, AgentCategory InitialCategory)
    : BoundRecord(BoundRecordToken)
    , OwnerPlayerToken(OwnerPlayerTokenValue)
    , Category(InitialCategory)
    , DriverKind(OwnerPlayerTokenValue != 0 ? AgentDriverKind::InputDriven : AgentDriverKind::Unassigned)
{
}

void Agent::ResolveEffectivePedals(const InputPacket& Packet, float& OutBrake, float& OutHandbrake) const noexcept
{
    const float DriverBrake     = std::clamp(Packet.BrakeAxis, 0.0f, 1.0f);
    const float DriverHandbrake = Packet.HasAction(InputActionHandbrake) || Packet.HandbrakeAxis >= 0.5f ? 1.0f : 0.0f;

    SpawnRules.ResolvePedalDemand(SecondsSinceSpawn, DriverBrake, DriverHandbrake, OutBrake, OutHandbrake);

    // 📝 A parked Agent keeps its handbrake latched until the driver actually asks for drive, so a studio car cannot roll off its pad
    //    the moment the spawn rules stop outranking him.
    if (HandbrakeLatched && Packet.ThrottleAxis > 0.05f)
    {
        const_cast<Agent*>(this)->HandbrakeLatched = false;
    }
    if (HandbrakeLatched)
    {
        OutHandbrake = 1.0f;
    }
}

void Agent::ApplySpawnPose(const SpawnPose& Pose, WorldSpace* World) noexcept
{
    SpawnStand            = Pose;
    PositionMetres        = Pose.PositionMetres;
    SpawnPositionMetres   = Pose.PositionMetres;
    YawDegrees            = Pose.YawDegrees;
    SpawnYawDegrees       = Pose.YawDegrees;
    PitchDegrees          = Pose.PitchDegrees;
    RollDegrees           = Pose.RollDegrees;
    ForwardSpeedMps       = 0.0f;
    SecondsSinceSpawn     = 0.0f;
    HandbrakeLatched      = SpawnRules.HandbrakeEngagedAtSpawn && SpawnRules.BrakesEnabledAtSpawn;

    if (World != nullptr)
    {
        (void)SynchronizeWithWorld(*World);
    }
}

void Agent::ResetSpawnClock() noexcept
{
    SecondsSinceSpawn = 0.0f;
}

void Agent::AdvanceSpawnClock(float StepSeconds) noexcept
{
    SecondsSinceSpawn += std::clamp(StepSeconds, 0.0f, 0.25f);
}

void Agent::ApplyInputPacket(const InputPacket& Packet, float StepSeconds, WorldSpace* World) noexcept
{
    LatestInput = Packet;
    const float Dt = std::clamp(StepSeconds, 0.0f, 0.25f);
    AdvanceSpawnClock(Dt);

    float EffectiveBrake     = 0.0f;
    float EffectiveHandbrake = 0.0f;
    ResolveEffectivePedals(Packet, EffectiveBrake, EffectiveHandbrake);

    if (Category == AgentCategory::Vehicle && HandbrakeLatched)
    {
        // 📝 A parked Agent does not creep: the latched handbrake holds it against the pad, pedals or no pedals.
        ForwardSpeedMps = 0.0f;
    }
    else if (Category == AgentCategory::Vehicle)
    {
        const float AccelMps2 = Packet.ThrottleAxis * 11.5f
                              - EffectiveBrake * 16.0f
                              - EffectiveHandbrake * 20.0f
                              - ForwardSpeedMps * 0.28f;
        ForwardSpeedMps = std::clamp(ForwardSpeedMps + AccelMps2 * Dt, -10.0f, 65.0f);

        if (std::fabs(ForwardSpeedMps) > 0.15f)
        {
            const float DirectionSign = ForwardSpeedMps >= 0.0f ? 1.0f : -1.0f;
            YawDegrees += Packet.SteerAxis * 72.0f * Dt * DirectionSign;
            const float YawRadians = YawDegrees * (3.14159265358979323846f / 180.0f);
            PositionMetres[0] += std::cos(YawRadians) * ForwardSpeedMps * Dt;
            PositionMetres[1] += std::sin(YawRadians) * ForwardSpeedMps * Dt;
        }
    }

    if (World != nullptr)
    {
        (void)SynchronizeWithWorld(*World);
    }
}

void Agent::PlaceInWorld(const std::array<float, 3>& UpdatedPositionMetres,
                         float                       UpdatedYawDegrees,
                         WorldSpace*                 World,
                         bool                        RecordAsSpawnAnchor) noexcept
{
    PositionMetres  = UpdatedPositionMetres;
    YawDegrees      = UpdatedYawDegrees;
    ForwardSpeedMps = 0.0f;
    if (RecordAsSpawnAnchor)
    {
        SpawnPositionMetres = UpdatedPositionMetres;
        SpawnYawDegrees     = UpdatedYawDegrees;

        // 📝 A recorded spawn anchor is also a fresh start: the spawn brake rules and the parked handbrake apply again.
        SpawnStand.PositionMetres = UpdatedPositionMetres;
        SpawnStand.YawDegrees     = UpdatedYawDegrees;
        PitchDegrees              = SpawnStand.PitchDegrees;
        RollDegrees               = SpawnStand.RollDegrees;
        ResetSpawnClock();
        HandbrakeLatched = SpawnRules.HandbrakeEngagedAtSpawn && SpawnRules.BrakesEnabledAtSpawn;
    }

    if (World != nullptr)
    {
        (void)SynchronizeWithWorld(*World);
    }
}

bool Agent::SynchronizeWithWorld(WorldSpace& World) const noexcept
{
    SceneRecordBase* RawRecord = World.FindRecord(BoundRecord);
    if (RawRecord == nullptr)
    {
        return false;
    }

    if (auto* Vehicle = dynamic_cast<VehicleRecord*>(RawRecord))
    {
        VehicleConfiguration& Config = Vehicle->AccessConfiguration();
        Config.Spatial.PositionMetres[0]     = PositionMetres[0];
        Config.Spatial.PositionMetres[1]     = PositionMetres[1];
        Config.Spatial.PositionMetres[2]     = PositionMetres[2];
        Config.Spatial.OrientationDegrees[0] = RollDegrees;
        Config.Spatial.OrientationDegrees[1] = PitchDegrees;
        Config.Spatial.OrientationDegrees[2] = YawDegrees;
        Config.Possessed                     = (DriverKind != AgentDriverKind::Unassigned);
        Config.AdvanceRevision();
        return true;
    }

    if (auto* Rigid = dynamic_cast<RigidBodyRecord*>(RawRecord))
    {
        SpatialConfiguration& Spatial = Rigid->AccessSpatial();
        Spatial.PositionMetres[0]     = PositionMetres[0];
        Spatial.PositionMetres[1]     = PositionMetres[1];
        Spatial.PositionMetres[2]     = PositionMetres[2];
        Spatial.OrientationDegrees[0] = RollDegrees;
        Spatial.OrientationDegrees[1] = PitchDegrees;
        Spatial.OrientationDegrees[2] = YawDegrees;
        return true;
    }

    return false;
}

} // namespace Frontier
