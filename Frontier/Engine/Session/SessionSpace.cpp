/*==========================================================================================================================================
                                                              SESSIONSPACE.CPP
==========================================================================================================================================*/
// 🧩 SessionSpace lifecycle, PlayerStore integration, SessionPolicy execution, and InputDriver / SyntheticDriver Agent control.

#include "Frontier/Engine/Session/SessionSpace.h"
#include "Frontier/Engine/Scene/WorldSpace.h"

#include <algorithm>

namespace Frontier
{

SessionSpace::SessionSpace(SessionConfiguration InitialConfiguration)
    : Configuration(std::move(InitialConfiguration))
    , ActiveLevelPath(Configuration.LobbyLevelPath)
{
}

void SessionSpace::BeginPolicy(std::unique_ptr<SessionPolicy> Policy, WorldSpace& World)
{
    ActivePolicy = std::move(Policy);
    if (ActivePolicy != nullptr)
    {
        ActivePolicy->OnSessionBegin(*this, World);
    }
}

PlayerToken SessionSpace::AddPlayer(WorldSpace&      World,
                                    std::string_view DisplayName,
                                    std::string_view NetworkEndpoint,
                                    bool             IsLocalHost)
{
    if (Players.QueryConnectedCount() >= Configuration.MaximumPlayers)
    {
        return PlayerToken{};
    }

    const PlayerToken Token = Players.RegisterPlayer(std::string(DisplayName), IsLocalHost, 0);
    if (PlayerProfile* Mutable = Players.FindProfile(Token))
    {
        Mutable->NetworkEndpoint = std::string(NetworkEndpoint);
    }
    HumanDrivers.emplace_back(Token);

    if (ActivePolicy != nullptr)
    {
        ActivePolicy->OnPlayerJoined(*this, World, Token);
    }
    RebindAllDriverPointers();
    return Token;
}

PlayerToken SessionSpace::AddSyntheticPlayer(WorldSpace&                           World,
                                             std::string_view                      DisplayName,
                                             const std::vector<SyntheticWaypoint>& Waypoints)
{
    if (Players.QueryConnectedCount() >= Configuration.MaximumPlayers)
    {
        return PlayerToken{};
    }

    const PlayerToken Token = Players.RegisterPlayer(std::string(DisplayName), false, 0);
    if (PlayerProfile* Mutable = Players.FindProfile(Token))
    {
        Mutable->NetworkEndpoint = "synthetic";
        Mutable->SyntheticAi     = true;
        Mutable->Ready           = true;
    }

    SyntheticDriver Driver(Token);
    if (!Waypoints.empty())
    {
        Driver.ConfigureWaypoints(Waypoints);
    }
    AiDrivers.push_back(std::move(Driver));

    if (ActivePolicy != nullptr)
    {
        ActivePolicy->OnPlayerJoined(*this, World, Token);
    }
    RebindAllDriverPointers();
    return Token;
}

bool SessionSpace::RemovePlayer(WorldSpace& World, PlayerToken Token)
{
    if (ActivePolicy != nullptr)
    {
        ActivePolicy->OnPlayerLeft(*this, World, Token);
    }
    (void)ReleaseAgentForPlayer(World, Token);
    HumanDrivers.erase(
        std::remove_if(HumanDrivers.begin(), HumanDrivers.end(),
                       [Token](const InputDriver& Driver) { return Driver.QueryPlayerToken() == Token; }),
        HumanDrivers.end());
    AiDrivers.erase(
        std::remove_if(AiDrivers.begin(), AiDrivers.end(),
                       [Token](const SyntheticDriver& Driver) { return Driver.QueryPlayerToken() == Token; }),
        AiDrivers.end());
    RebindAllDriverPointers();
    return Players.RemovePlayer(Token);
}

void SessionSpace::SubmitPlayerInput(PlayerToken Token, const InputPacket& Packet)
{
    if (PlayerProfile* Profile = Players.FindProfile(Token))
    {
        Profile->LatestInput = Packet;
    }
}

void SessionSpace::StepSession(WorldSpace& World, float StepSeconds)
{
    RebindAllDriverPointers();

    // 1. Human-controlled Agents driven via InputDriver
    for (InputDriver& Driver : HumanDrivers)
    {
        if (const PlayerProfile* Profile = Players.FindProfile(Driver.QueryPlayerToken()))
        {
            Driver.DriveFromPacket(Profile->LatestInput, StepSeconds, &World);
        }
    }

    // 2. AI-controlled Agents driven via SyntheticDriver
    for (SyntheticDriver& Driver : AiDrivers)
    {
        const InputPacket Synthesized = Driver.StepDrive(StepSeconds, &World);
        if (PlayerProfile* Profile = Players.FindProfile(Driver.QueryPlayerToken()))
        {
            Profile->LatestInput = Synthesized;
        }
    }

    // 3. Advance active SessionPolicy rules (Lobby turntable & ready countdown, RespawnPolicy, Race gates, Elimination timer)
    if (ActivePolicy != nullptr)
    {
        ActivePolicy->OnSessionStep(*this, World, StepSeconds);
    }
}

bool SessionSpace::RequestAgentRespawn(WorldSpace& World, PlayerToken Token)
{
    if (ActivePolicy == nullptr)
    {
        return false;
    }
    return ActivePolicy->RequestAgentRespawn(*this, World, Token);
}

Agent& SessionSpace::SpawnVehicleAgentForPlayer(WorldSpace&                 World,
                                                PlayerToken                 OwnerToken,
                                                std::string                 RecordName,
                                                const std::array<float, 3>& PositionMetres,
                                                float                       YawDegrees)
{
    if (Agent* ExistingAgent = FindAgentByOwner(OwnerToken))
    {
        // 📝 Every spawn (and every respawn) carries the active policy's spawn rules: the stand, the brake rules at the start, and the
        //    driven wheels all come from one place.
        ExistingAgent->AssignSpawnPolicy(ActivePolicy != nullptr ? ActivePolicy->QuerySpawnPolicy() : SpawnPolicy{});
        ExistingAgent->PlaceInWorld(PositionMetres, YawDegrees, &World, true);
        if (const PlayerProfile* Profile = Players.FindProfile(OwnerToken))
        {
            ExistingAgent->AssignVehicleConfiguration(Profile->VehicleSpec);
            (void)ExistingAgent->SynchronizeWithWorld(World);
        }
        RebindAllDriverPointers();
        return *ExistingAgent;
    }

    RecordToken BoundToken{};
    for (const SceneRecordBase* Existing : World.FindAllByType("Vehicle"))
    {
        if (Existing != nullptr && Existing->QueryRecordName() == RecordName)
        {
            BoundToken = Existing->QueryToken();
            break;
        }
    }

    if (!BoundToken.IsValid())
    {
        BoundToken = World.ConstructRecord("Vehicle", std::move(RecordName));
    }

    Agents.emplace_back(BoundToken, OwnerToken.Value, AgentCategory::Vehicle);
    Agent& CreatedAgent = Agents.back();

    CreatedAgent.AssignSpawnPolicy(ActivePolicy != nullptr ? ActivePolicy->QuerySpawnPolicy() : SpawnPolicy{});
    if (const PlayerProfile* Profile = Players.FindProfile(OwnerToken))
    {
        CreatedAgent.AssignVehicleConfiguration(Profile->VehicleSpec);
        CreatedAgent.AssignDriverKind(Profile->SyntheticAi ? AgentDriverKind::SyntheticDriven : AgentDriverKind::InputDriven);
    }

    CreatedAgent.PlaceInWorld(PositionMetres, YawDegrees, &World, true);
    (void)Players.AssignActiveUnit(OwnerToken, BoundToken);
    if (PlayerProfile* Mutable = Players.FindProfile(OwnerToken))
    {
        Mutable->StoredUnitToken = BoundToken;
    }
    RebindAllDriverPointers();
    return CreatedAgent;
}

bool SessionSpace::ReleaseAgentForPlayer(WorldSpace& World, PlayerToken OwnerToken)
{
    for (auto It = Agents.begin(); It != Agents.end(); ++It)
    {
        if (It->QueryOwnerPlayerToken() == OwnerToken.Value)
        {
            const RecordToken Record = It->QueryRecordToken();
            if (Record.IsValid())
            {
                (void)World.DestroyRecord(Record);
            }
            Agents.erase(It);
            (void)Players.ReleaseActiveUnit(OwnerToken, true);
            RebindAllDriverPointers();
            return true;
        }
    }
    return false;
}

InputDriver* SessionSpace::BindInputDriverToAgent(PlayerToken Token, Agent& TargetAgent)
{
    for (SyntheticDriver& Ai : AiDrivers)
    {
        if (Ai.QueryBoundAgent() == &TargetAgent)
        {
            Ai.UnbindAgent();
        }
    }
    InputDriver* Driver = FindInputDriver(Token);
    if (Driver == nullptr)
    {
        HumanDrivers.emplace_back(Token);
        Driver = &HumanDrivers.back();
    }
    if (PlayerProfile* Profile = Players.FindProfile(Token))
    {
        Profile->SyntheticAi = false;
    }
    TargetAgent.AssignOwnerPlayerToken(Token.Value);
    Driver->BindAgent(&TargetAgent);
    return Driver;
}

SyntheticDriver* SessionSpace::BindSyntheticDriverToAgent(PlayerToken                           Token,
                                                          Agent&                                TargetAgent,
                                                          const std::vector<SyntheticWaypoint>& Waypoints)
{
    for (InputDriver& Human : HumanDrivers)
    {
        if (Human.QueryBoundAgent() == &TargetAgent)
        {
            Human.UnbindAgent();
        }
    }
    SyntheticDriver* Driver = FindSyntheticDriver(Token);
    if (Driver == nullptr)
    {
        AiDrivers.emplace_back(Token);
        Driver = &AiDrivers.back();
    }
    if (!Waypoints.empty())
    {
        Driver->ConfigureWaypoints(Waypoints);
    }
    if (PlayerProfile* Profile = Players.FindProfile(Token))
    {
        Profile->SyntheticAi = true;
    }
    TargetAgent.AssignOwnerPlayerToken(Token.Value);
    Driver->BindAgent(&TargetAgent);
    return Driver;
}

Agent* SessionSpace::FindAgentByOwner(PlayerToken OwnerToken) noexcept
{
    for (Agent& Entry : Agents)
    {
        if (Entry.QueryOwnerPlayerToken() == OwnerToken.Value)
        {
            return &Entry;
        }
    }
    return nullptr;
}

const Agent* SessionSpace::FindAgentByOwner(PlayerToken OwnerToken) const noexcept
{
    for (const Agent& Entry : Agents)
    {
        if (Entry.QueryOwnerPlayerToken() == OwnerToken.Value)
        {
            return &Entry;
        }
    }
    return nullptr;
}

InputDriver* SessionSpace::FindInputDriver(PlayerToken OwnerToken) noexcept
{
    for (InputDriver& Driver : HumanDrivers)
    {
        if (Driver.QueryPlayerToken() == OwnerToken)
        {
            return &Driver;
        }
    }
    return nullptr;
}

SyntheticDriver* SessionSpace::FindSyntheticDriver(PlayerToken OwnerToken) noexcept
{
    for (SyntheticDriver& Driver : AiDrivers)
    {
        if (Driver.QueryPlayerToken() == OwnerToken)
        {
            return &Driver;
        }
    }
    return nullptr;
}

void SessionSpace::ClearAgents() noexcept
{
    for (InputDriver& Human : HumanDrivers)
    {
        Human.UnbindAgent();
    }
    for (SyntheticDriver& Ai : AiDrivers)
    {
        Ai.UnbindAgent();
    }
    Agents.clear();
}

void SessionSpace::RebindAllDriverPointers() noexcept
{
    for (InputDriver& Human : HumanDrivers)
    {
        if (const PlayerProfile* Profile = Players.FindProfile(Human.QueryPlayerToken()))
        {
            if (!Profile->SyntheticAi)
            {
                Human.BindAgent(FindAgentByOwner(Human.QueryPlayerToken()));
            }
        }
    }
    for (SyntheticDriver& Ai : AiDrivers)
    {
        if (const PlayerProfile* Profile = Players.FindProfile(Ai.QueryPlayerToken()))
        {
            if (Profile->SyntheticAi)
            {
                Ai.BindAgent(FindAgentByOwner(Ai.QueryPlayerToken()));
            }
        }
    }
}

} // namespace Frontier
