/*==========================================================================================================================================
                                                             SESSIONPOLICY.H
==========================================================================================================================================*/
// 🧩 Session, Network, Respawn, Lobby, ProvingGrounds, Race, and Elimination policies for SessionSpace.
//    Engine provides default policies (LobbyPolicy with RespawnRule::Disabled, ProvingGroundsPolicy/RacePolicy/EliminationPolicy with
//    respawning enabled); projects such as Everest can use or subclass these policies in Projects/<Name>/Source/Session/SessionPolicy.h.

#pragma once

#include "Frontier/Engine/Scene/Agent.h"
#include "Frontier/Engine/Session/PlayerToken.h"
#include "Frontier/Engine/Session/SpawnPolicy.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{

class SessionSpace;
class WorldSpace;

//------------------------------------------------------------------------------------------------------------------------
//                                                 NET POLICY & RESPAWN POLICY
//------------------------------------------------------------------------------------------------------------------------

enum class NetAuthorityRule : uint8_t
{
    HostAuthority      = 0,
    Standalone         = 1,
    ClientInterpolated = 2
};

struct NetPolicy
{
    NetAuthorityRule AuthorityRule          = NetAuthorityRule::HostAuthority;
    bool             AllowJoinInProgress    = true;
    bool             AllowSyntheticBackfill = true;
    float            ReplicationTickHz      = 60.0f;
};

enum class RespawnRule : uint8_t
{
    Disabled       = 0, // Lobby rule: no respawning in Lobby
    SpawnGrid      = 1, // ProvingGrounds rule: respawn at assigned start grid slot
    LastCheckpoint = 2  // Race rule: respawn at last cleared checkpoint gate in ProvingGrounds
};

struct RespawnPolicy
{
    RespawnRule Rule                      = RespawnRule::Disabled;
    float       RespawnDelaySeconds       = 0.0f;
    float       OutOfBoundsAltitudeMetres = -15.0f;

    [[nodiscard]] constexpr bool IsRespawnAllowed() const noexcept
    {
        return Rule != RespawnRule::Disabled;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    BASE SESSION POLICY
//------------------------------------------------------------------------------------------------------------------------

class SessionPolicy
{
  public:
    virtual ~SessionPolicy() = default;

    [[nodiscard]] virtual std::string_view QueryPolicyName() const noexcept = 0;

    [[nodiscard]] const NetPolicy&     QueryNetPolicy() const noexcept { return NetRules; }
    [[nodiscard]] NetPolicy&           AccessNetPolicy() noexcept { return NetRules; }
    [[nodiscard]] const RespawnPolicy& QueryRespawnPolicy() const noexcept { return RespawnRules; }
    [[nodiscard]] RespawnPolicy&       AccessRespawnPolicy() noexcept { return RespawnRules; }
    // 📝 Where each vehicle stands when this policy spawns it and how its brakes behave at the start. Respawn decides *whether* a vehicle
    //    comes back; this decides how it is stood and handed over every time it does.
    [[nodiscard]] const SpawnPolicy&   QuerySpawnPolicy() const noexcept { return SpawnRules; }
    [[nodiscard]] SpawnPolicy&         AccessSpawnPolicy() noexcept { return SpawnRules; }

    virtual void OnSessionBegin(SessionSpace& Session, WorldSpace& World) = 0;
    virtual void OnPlayerJoined(SessionSpace& Session, WorldSpace& World, PlayerToken Token) = 0;
    virtual void OnPlayerLeft(SessionSpace& Session, WorldSpace& World, PlayerToken Token) = 0;
    virtual void OnSessionStep(SessionSpace& Session, WorldSpace& World, float StepSeconds) = 0;

    // Enforces RespawnRules: returns false in Lobby (RespawnRule::Disabled) and respawns the player's Agent in ProvingGrounds.
    virtual bool RequestAgentRespawn(SessionSpace& Session, WorldSpace& World, PlayerToken Token);

    [[nodiscard]] virtual bool ShouldTransitionLevel(const SessionSpace& Session,
                                                     std::string&        OutTargetLevelPath,
                                                     std::string&        OutTargetPolicyName) const
    {
        (void)Session;
        (void)OutTargetLevelPath;
        (void)OutTargetPolicyName;
        return false;
    }

  protected:
    NetPolicy     NetRules{};
    RespawnPolicy RespawnRules{};
    SpawnPolicy   SpawnRules{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                            DEFAULT ENGINE LOBBY POLICY
//------------------------------------------------------------------------------------------------------------------------

class LobbyPolicy : public SessionPolicy
{
  public:
    LobbyPolicy();
    ~LobbyPolicy() override = default;

    [[nodiscard]] std::string_view QueryPolicyName() const noexcept override { return "LobbyPolicy"; }

    void OnSessionBegin(SessionSpace& Session, WorldSpace& World) override;
    void OnPlayerJoined(SessionSpace& Session, WorldSpace& World, PlayerToken Token) override;
    void OnPlayerLeft(SessionSpace& Session, WorldSpace& World, PlayerToken Token) override;
    void OnSessionStep(SessionSpace& Session, WorldSpace& World, float StepSeconds) override;

    [[nodiscard]] bool ShouldTransitionLevel(const SessionSpace& Session,
                                             std::string&        OutTargetLevelPath,
                                             std::string&        OutTargetPolicyName) const override;

    void SelectStudioBackdrop(StudioBackdropStyle Style, WorldSpace& World);
    void UpdatePlayerVehicleConfiguration(SessionSpace&                   Session,
                                          WorldSpace&                     World,
                                          PlayerToken                     Token,
                                          const VehicleUnitConfiguration& Spec);
    void SetPlayerReady(SessionSpace& Session, PlayerToken Token, bool ReadyState);
    void SelectNextMatchPolicy(std::string PolicyName) { NextMatchPolicyName = std::move(PolicyName); }
    void SelectTargetLevelPath(std::string LevelPath) { TargetProvingGroundsLevelPath = std::move(LevelPath); }

    [[nodiscard]] StudioBackdropStyle QueryStudioBackdrop() const noexcept { return ActiveBackdrop; }
    [[nodiscard]] float               QueryTurntableYawDegrees() const noexcept { return TurntableYawDegrees; }
    [[nodiscard]] float               QueryCountdownRemainingSeconds() const noexcept { return CountdownRemainingSeconds; }
    [[nodiscard]] bool                IsCountdownActive() const noexcept { return CountdownActive; }
    [[nodiscard]] const std::string&  QueryNextMatchPolicyName() const noexcept { return NextMatchPolicyName; }

  protected:
    StudioBackdropStyle ActiveBackdrop                = StudioBackdropStyle::WhiteCyclorama;
    float               TurntableYawDegrees           = 28.0f;
    float               TurntableAngularSpeedDegPerS  = 14.0f;
    float               CountdownDurationSeconds      = 3.0f;
    float               CountdownRemainingSeconds     = 3.0f;
    bool                CountdownActive               = false;
    bool                ReadyToLaunchMatch            = false;
    std::string         NextMatchPolicyName           = "RacePolicy";
    std::string         TargetProvingGroundsLevelPath = "Content/Levels/ProvingGroundVehicle.levelspace";
};

//------------------------------------------------------------------------------------------------------------------------
//                                       DEFAULT ENGINE PROVING GROUNDS POLICY
//------------------------------------------------------------------------------------------------------------------------

class ProvingGroundsPolicy : public SessionPolicy
{
  public:
    ProvingGroundsPolicy();
    ~ProvingGroundsPolicy() override = default;

    [[nodiscard]] std::string_view QueryPolicyName() const noexcept override { return "ProvingGroundsPolicy"; }

    void OnSessionBegin(SessionSpace& Session, WorldSpace& World) override;
    void OnPlayerJoined(SessionSpace& Session, WorldSpace& World, PlayerToken Token) override;
    void OnPlayerLeft(SessionSpace& Session, WorldSpace& World, PlayerToken Token) override;
    void OnSessionStep(SessionSpace& Session, WorldSpace& World, float StepSeconds) override;
};

//------------------------------------------------------------------------------------------------------------------------
//                                               ENGINE RACE POLICY
//------------------------------------------------------------------------------------------------------------------------

struct RaceCheckpointGate
{
    std::array<float, 3> PositionMetres = { 0.0f, 0.0f, 0.0f };
    float                RadiusMetres   = 8.5f;
    std::string          GateLabel{};
};

struct RacePlayerProgress
{
    PlayerToken          Token{};
    uint32_t             CompletedLaps        = 0;
    uint32_t             NextCheckpointIndex  = 0;
    double               ElapsedRaceSeconds   = 0.0;
    bool                 Finished             = false;
    std::array<float, 3> LastClearedPositionM = { -40.0f, 0.0f, 0.0f };
    float                LastClearedYawDeg    = 0.0f;
};

class RacePolicy : public ProvingGroundsPolicy
{
  public:
    explicit RacePolicy(uint32_t TargetLapCount = 2);
    ~RacePolicy() override = default;

    [[nodiscard]] std::string_view QueryPolicyName() const noexcept override { return "RacePolicy"; }

    void OnSessionBegin(SessionSpace& Session, WorldSpace& World) override;
    void OnPlayerJoined(SessionSpace& Session, WorldSpace& World, PlayerToken Token) override;
    void OnPlayerLeft(SessionSpace& Session, WorldSpace& World, PlayerToken Token) override;
    void OnSessionStep(SessionSpace& Session, WorldSpace& World, float StepSeconds) override;
    bool RequestAgentRespawn(SessionSpace& Session, WorldSpace& World, PlayerToken Token) override;

    [[nodiscard]] bool ShouldTransitionLevel(const SessionSpace& Session,
                                             std::string&        OutTargetLevelPath,
                                             std::string&        OutTargetPolicyName) const override;

    [[nodiscard]] const std::vector<RaceCheckpointGate>& QueryCheckpoints() const noexcept { return Checkpoints; }
    [[nodiscard]] const std::vector<RacePlayerProgress>& QueryProgressOrder() const noexcept { return ProgressTable; }
    [[nodiscard]] PlayerToken                            QueryWinnerToken() const noexcept { return WinnerToken; }
    [[nodiscard]] bool                                   IsRaceFinished() const noexcept { return RaceFinished; }

  protected:
    uint32_t                        LapTarget               = 2;
    std::vector<RaceCheckpointGate> Checkpoints{};
    std::vector<RacePlayerProgress> ProgressTable{};
    PlayerToken                     WinnerToken{};
    bool                            RaceFinished            = false;
    float                           PostFinishTimerSeconds  = 0.0f;
    std::string                     ReturnLobbyLevelPath    = "Content/Levels/Default.levelspace";
};

//------------------------------------------------------------------------------------------------------------------------
//                                            ENGINE ELIMINATION POLICY
//------------------------------------------------------------------------------------------------------------------------

struct EliminationStandingEntry
{
    PlayerToken Token{};
    float       TrackScoreMetres = 0.0f;
    bool        Eliminated       = false;
};

class EliminationPolicy : public ProvingGroundsPolicy
{
  public:
    explicit EliminationPolicy(float IntervalSeconds = 15.0f);
    ~EliminationPolicy() override = default;

    [[nodiscard]] std::string_view QueryPolicyName() const noexcept override { return "EliminationPolicy"; }

    void OnSessionBegin(SessionSpace& Session, WorldSpace& World) override;
    void OnPlayerJoined(SessionSpace& Session, WorldSpace& World, PlayerToken Token) override;
    void OnPlayerLeft(SessionSpace& Session, WorldSpace& World, PlayerToken Token) override;
    void OnSessionStep(SessionSpace& Session, WorldSpace& World, float StepSeconds) override;

    [[nodiscard]] bool ShouldTransitionLevel(const SessionSpace& Session,
                                             std::string&        OutTargetLevelPath,
                                             std::string&        OutTargetPolicyName) const override;

    [[nodiscard]] const std::vector<EliminationStandingEntry>& QueryStandings() const noexcept { return Standings; }
    [[nodiscard]] uint32_t                                     QueryActivePlayerCount() const noexcept;
    [[nodiscard]] PlayerToken                                  QueryWinnerToken() const noexcept { return WinnerToken; }
    [[nodiscard]] float                                        QueryRoundRemainingSeconds() const noexcept { return RoundTimerSeconds; }

  protected:
    float                                 EliminationIntervalSeconds = 15.0f;
    float                                 RoundTimerSeconds          = 15.0f;
    std::vector<EliminationStandingEntry> Standings{};
    PlayerToken                           WinnerToken{};
    bool                                  MatchComplete              = false;
    float                                 PostMatchTimerSeconds      = 0.0f;
    std::string                           ReturnLobbyLevelPath       = "Content/Levels/Default.levelspace";
};

} // namespace Frontier
