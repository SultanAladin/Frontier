/*==========================================================================================================================================
                                                            SESSIONPOLICY.CPP
==========================================================================================================================================*/
// 🧩 Default Engine SessionPolicy, LobbyPolicy, ProvingGroundsPolicy, RacePolicy, and EliminationPolicy implementations.

#include "Frontier/Engine/Session/SessionPolicy.h"
#include "Frontier/Engine/Session/SessionSpace.h"
#include "Frontier/Engine/Scene/WorldSpace.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    BASE SESSION POLICY
//------------------------------------------------------------------------------------------------------------------------

bool SessionPolicy::RequestAgentRespawn(SessionSpace& Session, WorldSpace& World, PlayerToken Token)
{
    if (!RespawnRules.IsRespawnAllowed())
    {
        return false; // RespawnPolicy::Disabled (e.g. in Lobby)
    }

    Agent* ControlledAgent = Session.FindAgentByOwner(Token);
    if (ControlledAgent == nullptr)
    {
        return false;
    }

    // 📝 Where it comes back to is RespawnPolicy's business; how it is stood and handed over is SpawnPolicy's: the same footprint survey
    //    stands it on the surface it returns to, with the brakes the policy asks for at the start.
    ControlledAgent->AssignSpawnPolicy(SpawnRules);
    ControlledAgent->PlaceInWorld(
        ControlledAgent->QuerySpawnPositionMetres(),
        ControlledAgent->QuerySpawnYawDegrees(),
        &World,
        true);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                DEFAULT ENGINE LOBBY POLICY
//------------------------------------------------------------------------------------------------------------------------

LobbyPolicy::LobbyPolicy()
{
    NetRules.AuthorityRule          = NetAuthorityRule::HostAuthority;
    NetRules.AllowJoinInProgress    = true;
    NetRules.AllowSyntheticBackfill = false;

    // Lobby rule: no respawning in lobby, and every preview car is handed over parked on its turntable pad
    RespawnRules.Rule               = RespawnRule::Disabled;
    SpawnRules                      = SpawnPolicy::StudioParked();
}

void LobbyPolicy::OnSessionBegin(SessionSpace& Session, WorldSpace& World)
{
    CountdownActive           = false;
    ReadyToLaunchMatch        = false;
    CountdownRemainingSeconds = Session.QueryConfiguration().LobbyCountdownSeconds;
    CountdownDurationSeconds  = CountdownRemainingSeconds;
    NextMatchPolicyName       = Session.QueryConfiguration().MatchPolicyName;

    for (PlayerProfile& Profile : Session.AccessPlayerStore().AccessProfiles())
    {
        if (Profile.Connected)
        {
            Profile.Ready = Profile.SyntheticAi;
            OnPlayerJoined(Session, World, Profile.Token);
        }
    }
}

void LobbyPolicy::OnPlayerJoined(SessionSpace& Session, WorldSpace& World, PlayerToken Token)
{
    const PlayerProfile* Profile = Session.QueryPlayerStore().FindProfile(Token);
    if (Profile == nullptr)
    {
        return;
    }

    if (Session.FindAgentByOwner(Token) == nullptr)
    {
        const size_t SlotIndex = Session.QueryAgents().size();
        const std::array<float, 3> BayPosition = (SlotIndex == 0)
            ? std::array<float, 3>{ 0.0f, 0.0f, 0.0f }
            : std::array<float, 3>{ -5.2f * static_cast<float>(SlotIndex), (SlotIndex % 2 == 1 ? 3.4f : -3.4f), 0.0f };

        Agent& PreviewAgent = Session.SpawnVehicleAgentForPlayer(
            World,
            Token,
            SlotIndex == 0 ? "ControlVehicle" : ("LobbyPreview_" + Profile->DisplayName),
            BayPosition,
            TurntableYawDegrees);
        (void)PreviewAgent;
    }
}

void LobbyPolicy::OnPlayerLeft(SessionSpace& Session, WorldSpace& World, PlayerToken Token)
{
    (void)Session.ReleaseAgentForPlayer(World, Token);
    if (!Session.QueryPlayerStore().AreAllConnectedReady())
    {
        CountdownActive           = false;
        CountdownRemainingSeconds = CountdownDurationSeconds;
    }
}

void LobbyPolicy::OnSessionStep(SessionSpace& Session, WorldSpace& World, float StepSeconds)
{
    TurntableYawDegrees = std::fmod(TurntableYawDegrees + TurntableAngularSpeedDegPerS * StepSeconds, 360.0f);

    if (const PlayerProfile* Host = Session.QueryPlayerStore().FindLocalHostProfile())
    {
        if (Agent* HostAgent = Session.FindAgentByOwner(Host->Token))
        {
            const std::array<float, 3> PedestalCenter = {
                0.0f,
                0.0f,
                HostAgent->QueryVehicleConfiguration().RideHeightOffsetMetres
            };
            HostAgent->PlaceInWorld(PedestalCenter, TurntableYawDegrees, &World, true);
        }
    }

    if (Session.QueryPlayerStore().AreAllConnectedReady())
    {
        CountdownActive = true;
        CountdownRemainingSeconds = std::max(0.0f, CountdownRemainingSeconds - StepSeconds);
        if (CountdownRemainingSeconds <= 0.0f)
        {
            ReadyToLaunchMatch = true;
        }
    }
    else
    {
        CountdownActive           = false;
        CountdownRemainingSeconds = CountdownDurationSeconds;
        ReadyToLaunchMatch        = false;
    }
}

bool LobbyPolicy::ShouldTransitionLevel(const SessionSpace& /*Session*/,
                                        std::string&        OutTargetLevelPath,
                                        std::string&        OutTargetPolicyName) const
{
    if (!ReadyToLaunchMatch)
    {
        return false;
    }
    OutTargetLevelPath  = TargetProvingGroundsLevelPath;
    OutTargetPolicyName = NextMatchPolicyName;
    return true;
}

void LobbyPolicy::SelectStudioBackdrop(StudioBackdropStyle Style, WorldSpace& /*World*/)
{
    ActiveBackdrop = Style;
}

void LobbyPolicy::UpdatePlayerVehicleConfiguration(SessionSpace&                   Session,
                                                   WorldSpace&                     World,
                                                   PlayerToken                     Token,
                                                   const VehicleUnitConfiguration& Spec)
{
    (void)Session.AccessPlayerStore().AssignVehicleConfiguration(Token, Spec);
    ActiveBackdrop = Spec.Backdrop;

    if (Agent* PreviewAgent = Session.FindAgentByOwner(Token))
    {
        PreviewAgent->AssignVehicleConfiguration(Spec);
        (void)PreviewAgent->SynchronizeWithWorld(World);
    }
}

void LobbyPolicy::SetPlayerReady(SessionSpace& Session, PlayerToken Token, bool ReadyState)
{
    (void)Session.AccessPlayerStore().AssignReadyState(Token, ReadyState);
}

//------------------------------------------------------------------------------------------------------------------------
//                                       DEFAULT ENGINE PROVING GROUNDS POLICY
//------------------------------------------------------------------------------------------------------------------------

ProvingGroundsPolicy::ProvingGroundsPolicy()
{
    NetRules.AuthorityRule          = NetAuthorityRule::HostAuthority;
    NetRules.AllowJoinInProgress    = true;
    NetRules.AllowSyntheticBackfill = true;

    // ProvingGrounds rule: respawning is enabled in proving grounds, and every car stands on the surveyed surface with live brakes
    RespawnRules.Rule               = RespawnRule::SpawnGrid;
    SpawnRules                      = SpawnPolicy::GridRelease();
}

void ProvingGroundsPolicy::OnSessionBegin(SessionSpace& Session, WorldSpace& World)
{
    size_t GridSlot = 0;
    for (const PlayerProfile& Profile : Session.QueryPlayerStore().QueryProfiles())
    {
        if (!Profile.Connected)
        {
            continue;
        }
        const std::array<float, 3> GridPosition = {
            -40.0f - static_cast<float>(GridSlot / 2) * 6.5f,
            (GridSlot % 2 == 0) ? -2.2f : 2.2f,
            Profile.VehicleSpec.RideHeightOffsetMetres
        };
        Agent& SpawnedAgent = Session.SpawnVehicleAgentForPlayer(
            World,
            Profile.Token,
            GridSlot == 0 ? "ControlVehicle" : ("GridVehicle_" + Profile.DisplayName),
            GridPosition,
            0.0f);
        SpawnedAgent.PlaceInWorld(GridPosition, 0.0f, &World, true);
        ++GridSlot;
    }
}

void ProvingGroundsPolicy::OnPlayerJoined(SessionSpace& Session, WorldSpace& World, PlayerToken Token)
{
    if (Session.FindAgentByOwner(Token) == nullptr)
    {
        const size_t GridSlot = Session.QueryAgents().size();
        const std::array<float, 3> GridPosition = {
            -40.0f - static_cast<float>(GridSlot) * 6.5f,
            0.0f,
            0.0f
        };
        Agent& SpawnedAgent = Session.SpawnVehicleAgentForPlayer(
            World,
            Token,
            "GridVehicle_" + std::to_string(Token.Value),
            GridPosition,
            0.0f);
        SpawnedAgent.PlaceInWorld(GridPosition, 0.0f, &World, true);
    }
}

void ProvingGroundsPolicy::OnPlayerLeft(SessionSpace& Session, WorldSpace& World, PlayerToken Token)
{
    (void)Session.ReleaseAgentForPlayer(World, Token);
}

void ProvingGroundsPolicy::OnSessionStep(SessionSpace& Session, WorldSpace& World, float /*StepSeconds*/)
{
    for (const PlayerProfile& Profile : Session.QueryPlayerStore().QueryProfiles())
    {
        if (!Profile.Connected)
        {
            continue;
        }
        if (Profile.LatestInput.HasAction(InputActionResetUnit))
        {
            (void)RequestAgentRespawn(Session, World, Profile.Token);
        }
        else if (const Agent* ActiveAgent = Session.FindAgentByOwner(Profile.Token))
        {
            if (ActiveAgent->QueryPositionMetres()[2] < RespawnRules.OutOfBoundsAltitudeMetres)
            {
                (void)RequestAgentRespawn(Session, World, Profile.Token);
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                               ENGINE RACE POLICY
//------------------------------------------------------------------------------------------------------------------------

RacePolicy::RacePolicy(uint32_t TargetLapCount)
    : LapTarget(std::max(1u, TargetLapCount))
{
    RespawnRules.Rule = RespawnRule::LastCheckpoint;
    Checkpoints = {
        { { -10.0f,   0.0f, 0.0f }, 9.0f, "Launch Gate" },
        { {  32.0f,   0.0f, 0.0f }, 9.0f, "Slalom Entry" },
        { {  34.0f,  38.5f, 0.0f }, 9.0f, "Maintenance Hangar" },
        { { -40.0f,   0.0f, 0.0f }, 9.5f, "Start / Finish Line" }
    };
}

void RacePolicy::OnSessionBegin(SessionSpace& Session, WorldSpace& World)
{
    ProvingGroundsPolicy::OnSessionBegin(Session, World);
    ProgressTable.clear();
    WinnerToken            = {};
    RaceFinished           = false;
    PostFinishTimerSeconds = 0.0f;

    for (const PlayerProfile& Profile : Session.QueryPlayerStore().QueryProfiles())
    {
        if (!Profile.Connected)
        {
            continue;
        }
        RacePlayerProgress Progress{};
        Progress.Token = Profile.Token;
        if (const Agent* Spawned = Session.FindAgentByOwner(Profile.Token))
        {
            Progress.LastClearedPositionM = Spawned->QuerySpawnPositionMetres();
            Progress.LastClearedYawDeg    = Spawned->QuerySpawnYawDegrees();
        }
        ProgressTable.push_back(Progress);
    }
}

void RacePolicy::OnPlayerJoined(SessionSpace& Session, WorldSpace& World, PlayerToken Token)
{
    ProvingGroundsPolicy::OnPlayerJoined(Session, World, Token);
    RacePlayerProgress Progress{};
    Progress.Token = Token;
    if (const Agent* Spawned = Session.FindAgentByOwner(Token))
    {
        Progress.LastClearedPositionM = Spawned->QuerySpawnPositionMetres();
        Progress.LastClearedYawDeg    = Spawned->QuerySpawnYawDegrees();
    }
    ProgressTable.push_back(Progress);
}

void RacePolicy::OnPlayerLeft(SessionSpace& Session, WorldSpace& World, PlayerToken Token)
{
    ProvingGroundsPolicy::OnPlayerLeft(Session, World, Token);
    ProgressTable.erase(
        std::remove_if(ProgressTable.begin(), ProgressTable.end(),
                       [Token](const RacePlayerProgress& Entry) { return Entry.Token == Token; }),
        ProgressTable.end());
}

bool RacePolicy::RequestAgentRespawn(SessionSpace& Session, WorldSpace& World, PlayerToken Token)
{
    if (!RespawnRules.IsRespawnAllowed())
    {
        return false;
    }
    Agent* ControlledAgent = Session.FindAgentByOwner(Token);
    if (ControlledAgent == nullptr)
    {
        return false;
    }
    if (RespawnRules.Rule == RespawnRule::LastCheckpoint)
    {
        for (const RacePlayerProgress& Entry : ProgressTable)
        {
            if (Entry.Token == Token)
            {
                ControlledAgent->PlaceInWorld(Entry.LastClearedPositionM, Entry.LastClearedYawDeg, &World, false);
                return true;
            }
        }
    }
    return ProvingGroundsPolicy::RequestAgentRespawn(Session, World, Token);
}

void RacePolicy::OnSessionStep(SessionSpace& Session, WorldSpace& World, float StepSeconds)
{
    ProvingGroundsPolicy::OnSessionStep(Session, World, StepSeconds);

    if (RaceFinished)
    {
        PostFinishTimerSeconds += StepSeconds;
        return;
    }

    for (RacePlayerProgress& Entry : ProgressTable)
    {
        if (Entry.Finished)
        {
            continue;
        }
        Entry.ElapsedRaceSeconds += static_cast<double>(StepSeconds);

        const Agent* ControlledAgent = Session.FindAgentByOwner(Entry.Token);
        if (ControlledAgent == nullptr || Checkpoints.empty())
        {
            continue;
        }

        const RaceCheckpointGate& Gate = Checkpoints[Entry.NextCheckpointIndex];
        const auto& Pos = ControlledAgent->QueryPositionMetres();
        const float Dx  = Pos[0] - Gate.PositionMetres[0];
        const float Dy  = Pos[1] - Gate.PositionMetres[1];
        const float Dist = std::sqrt(Dx * Dx + Dy * Dy);

        if (Dist <= Gate.RadiusMetres)
        {
            Entry.LastClearedPositionM = Gate.PositionMetres;
            Entry.LastClearedYawDeg    = ControlledAgent->QueryYawDegrees();
            ++Entry.NextCheckpointIndex;
            if (Entry.NextCheckpointIndex >= Checkpoints.size())
            {
                Entry.NextCheckpointIndex = 0;
                ++Entry.CompletedLaps;
                if (PlayerProfile* Profile = Session.AccessPlayerStore().FindProfile(Entry.Token))
                {
                    Profile->Score += 100;
                }
                if (Entry.CompletedLaps >= LapTarget)
                {
                    Entry.Finished = true;
                    if (!WinnerToken.IsValid())
                    {
                        WinnerToken  = Entry.Token;
                        RaceFinished = true;
                    }
                }
            }
        }
    }

    std::sort(ProgressTable.begin(), ProgressTable.end(),
              [](const RacePlayerProgress& Left, const RacePlayerProgress& Right)
              {
                  if (Left.CompletedLaps != Right.CompletedLaps)
                  {
                      return Left.CompletedLaps > Right.CompletedLaps;
                  }
                  return Left.NextCheckpointIndex > Right.NextCheckpointIndex;
              });
}

bool RacePolicy::ShouldTransitionLevel(const SessionSpace& /*Session*/,
                                       std::string&        OutTargetLevelPath,
                                       std::string&        OutTargetPolicyName) const
{
    if (!RaceFinished || PostFinishTimerSeconds < 4.0f)
    {
        return false;
    }
    OutTargetLevelPath  = ReturnLobbyLevelPath;
    OutTargetPolicyName = "LobbyPolicy";
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            ENGINE ELIMINATION POLICY
//------------------------------------------------------------------------------------------------------------------------

EliminationPolicy::EliminationPolicy(float IntervalSeconds)
    : EliminationIntervalSeconds(std::max(3.0f, IntervalSeconds))
    , RoundTimerSeconds(std::max(3.0f, IntervalSeconds))
{
    RespawnRules.Rule = RespawnRule::SpawnGrid;
    SpawnRules        = SpawnPolicy::GridRelease();
}

void EliminationPolicy::OnSessionBegin(SessionSpace& Session, WorldSpace& World)
{
    ProvingGroundsPolicy::OnSessionBegin(Session, World);
    Standings.clear();
    WinnerToken           = {};
    MatchComplete         = false;
    PostMatchTimerSeconds = 0.0f;
    RoundTimerSeconds     = EliminationIntervalSeconds;

    for (const PlayerProfile& Profile : Session.QueryPlayerStore().QueryProfiles())
    {
        if (Profile.Connected)
        {
            Standings.push_back(EliminationStandingEntry{ Profile.Token, 0.0f, false });
        }
    }
}

void EliminationPolicy::OnPlayerJoined(SessionSpace& Session, WorldSpace& World, PlayerToken Token)
{
    ProvingGroundsPolicy::OnPlayerJoined(Session, World, Token);
    Standings.push_back(EliminationStandingEntry{ Token, 0.0f, false });
}

void EliminationPolicy::OnPlayerLeft(SessionSpace& Session, WorldSpace& World, PlayerToken Token)
{
    ProvingGroundsPolicy::OnPlayerLeft(Session, World, Token);
    Standings.erase(
        std::remove_if(Standings.begin(), Standings.end(),
                       [Token](const EliminationStandingEntry& Entry) { return Entry.Token == Token; }),
        Standings.end());
}

void EliminationPolicy::OnSessionStep(SessionSpace& Session, WorldSpace& World, float StepSeconds)
{
    ProvingGroundsPolicy::OnSessionStep(Session, World, StepSeconds);

    if (MatchComplete)
    {
        PostMatchTimerSeconds += StepSeconds;
        return;
    }

    for (EliminationStandingEntry& Entry : Standings)
    {
        if (Entry.Eliminated)
        {
            continue;
        }
        if (const Agent* ControlledAgent = Session.FindAgentByOwner(Entry.Token))
        {
            const auto& Pos = ControlledAgent->QueryPositionMetres();
            Entry.TrackScoreMetres = Pos[0] + std::fabs(ControlledAgent->QueryForwardSpeedMps()) * 0.5f;
        }
    }

    std::sort(Standings.begin(), Standings.end(),
              [](const EliminationStandingEntry& Left, const EliminationStandingEntry& Right)
              {
                  if (Left.Eliminated != Right.Eliminated)
                  {
                      return !Left.Eliminated;
                  }
                  return Left.TrackScoreMetres > Right.TrackScoreMetres;
              });

    if (QueryActivePlayerCount() <= 1)
    {
        for (const EliminationStandingEntry& Entry : Standings)
        {
            if (!Entry.Eliminated)
            {
                WinnerToken = Entry.Token;
                break;
            }
        }
        MatchComplete = true;
        return;
    }

    RoundTimerSeconds -= StepSeconds;
    if (RoundTimerSeconds <= 0.0f)
    {
        RoundTimerSeconds = EliminationIntervalSeconds;
        for (auto It = Standings.rbegin(); It != Standings.rend(); ++It)
        {
            if (!It->Eliminated)
            {
                It->Eliminated = true;
                (void)Session.ReleaseAgentForPlayer(World, It->Token);
                break;
            }
        }

        if (QueryActivePlayerCount() <= 1)
        {
            for (const EliminationStandingEntry& Entry : Standings)
            {
                if (!Entry.Eliminated)
                {
                    WinnerToken = Entry.Token;
                    if (PlayerProfile* Winner = Session.AccessPlayerStore().FindProfile(WinnerToken))
                    {
                        Winner->Score += 250;
                    }
                    break;
                }
            }
            MatchComplete = true;
        }
    }
}

uint32_t EliminationPolicy::QueryActivePlayerCount() const noexcept
{
    uint32_t Count = 0;
    for (const EliminationStandingEntry& Entry : Standings)
    {
        if (!Entry.Eliminated)
        {
            ++Count;
        }
    }
    return Count;
}

bool EliminationPolicy::ShouldTransitionLevel(const SessionSpace& /*Session*/,
                                              std::string&        OutTargetLevelPath,
                                              std::string&        OutTargetPolicyName) const
{
    if (!MatchComplete || PostMatchTimerSeconds < 4.0f)
    {
        return false;
    }
    OutTargetLevelPath  = ReturnLobbyLevelPath;
    OutTargetPolicyName = "LobbyPolicy";
    return true;
}

} // namespace Frontier
