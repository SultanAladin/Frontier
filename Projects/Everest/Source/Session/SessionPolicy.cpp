/*==========================================================================================================================================
                                                            SESSIONPOLICY.CPP
==========================================================================================================================================*/
// 🧩 Project Everest custom SessionPolicy implementations and ProvingGrounds waypoint circuit for SyntheticDriver.

#include "Projects/Everest/Source/Session/SessionPolicy.h"

namespace Frontier::Everest
{

EverestLobbyPolicy::EverestLobbyPolicy()
{
    // Lobby rule: no respawning in Lobby; transition targets ProvingGroundVehicle.levelspace
    RespawnRules.Rule             = RespawnRule::Disabled;
    NetRules.AuthorityRule        = NetAuthorityRule::HostAuthority;
    NetRules.AllowJoinInProgress  = true;
    TargetProvingGroundsLevelPath = "Content/Levels/ProvingGroundVehicle.levelspace";

    // 📝 The studio bay: the car is parked on its pad with the handbrake on and no foot brake, so the turntable turns a stationary car.
    SpawnRules                    = SpawnPolicy::StudioParked();
}

EverestProvingGroundsPolicy::EverestProvingGroundsPolicy()
{
    // ProvingGrounds rule: respawning allowed at start grid
    RespawnRules.Rule               = RespawnRule::SpawnGrid;
    NetRules.AuthorityRule          = NetAuthorityRule::HostAuthority;
    NetRules.AllowSyntheticBackfill = true;

    // 📝 The proving ground: the car is stood on the surveyed surface with its brakes live and the handbrake already released.
    SpawnRules                      = SpawnPolicy::GridRelease();
}

EverestRacePolicy::EverestRacePolicy(uint32_t TargetLapCount)
    : RacePolicy(TargetLapCount)
{
    // ProvingGrounds Race rule: respawning allowed at last cleared checkpoint
    RespawnRules.Rule    = RespawnRule::LastCheckpoint;
    ReturnLobbyLevelPath = "Content/Levels/Default.levelspace";
    SpawnRules           = SpawnPolicy::GridRelease();
}

EverestEliminationPolicy::EverestEliminationPolicy(float IntervalSeconds)
    : EliminationPolicy(IntervalSeconds)
{
    // ProvingGrounds Elimination rule: respawning allowed at start grid while active
    RespawnRules.Rule    = RespawnRule::SpawnGrid;
    ReturnLobbyLevelPath = "Content/Levels/Default.levelspace";
    SpawnRules           = SpawnPolicy::GridRelease();
}

std::vector<SyntheticWaypoint> ConstructEverestCircuitWaypoints()
{
    return {
        { { -10.0f,   0.0f, 0.0f }, 26.0f, 9.0f },
        { {  32.0f,   0.0f, 0.0f }, 20.0f, 9.0f },
        { {  34.0f,  38.5f, 0.0f }, 18.0f, 9.0f },
        { { -40.0f,   0.0f, 0.0f }, 24.0f, 9.5f }
    };
}

std::unique_ptr<SessionPolicy> ConstructEverestSessionPolicy(std::string_view PolicyName)
{
    if (PolicyName == "ProvingGroundsPolicy")
    {
        return std::make_unique<EverestProvingGroundsPolicy>();
    }
    if (PolicyName == "RacePolicy")
    {
        return std::make_unique<EverestRacePolicy>(2);
    }
    if (PolicyName == "EliminationPolicy")
    {
        return std::make_unique<EverestEliminationPolicy>(10.0f);
    }
    return std::make_unique<EverestLobbyPolicy>();
}

} // namespace Frontier::Everest
