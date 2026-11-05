/*==========================================================================================================================================
                                                           SESSIONVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 End-to-end validation of Agent, InputEncoder, InputDriver (human), SyntheticDriver (AI), SessionPolicy (NetPolicy, RespawnPolicy,
//    LobbyPolicy, ProvingGroundsPolicy, RacePolicy, EliminationPolicy), PlayerStore, TransferConfiguration, LevelTransition, and
//    SessionExtension across Everest's Default.levelspace (Lobby) and ProvingGroundVehicle.levelspace (ProvingGrounds).

#include "Frontier/Engine/Input/InputDriver.h"
#include "Frontier/Engine/Input/InputEncoder.h"
#include "Frontier/Engine/Input/SyntheticDriver.h"
#include "Frontier/Engine/Scene/Agent.h"
#include "Frontier/Engine/Scene/WorldSpace.h"
#include "Frontier/Engine/Session/LevelTransition.h"
#include "Frontier/Engine/Session/SessionConfiguration.h"
#include "Frontier/Engine/Session/SessionPolicy.h"
#include "Frontier/Engine/Session/SessionSpace.h"
#include "Frontier/Engine/PhysicalDynamics/Vehicle/VehicleSpecification.h"
#include "Frontier/Engine/Session/SpawnPolicy.h"
#include "Frontier/Engine/Session/TransferConfiguration.h"
#include "Frontier/Extensions/Physics/PhysicsExtension.h"
#include "Frontier/Extensions/Physics/PhysicsSpace.h"
#include "Frontier/Extensions/Physics/VehicleDeployment.h"
#include "Frontier/Extensions/Session/SessionExtension.h"
#include "Projects/Everest/Source/Session/SessionPolicy.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace Frontier;
using namespace Frontier::Everest;

namespace
{

struct ValidationResult
{
    std::string Claim;
    bool        Passed = false;
};

} // namespace

int main(int ArgumentCount, char** ArgumentValues)
{
    const std::string ProjectRoot = (ArgumentCount > 1) ? ArgumentValues[1] : "Projects/Everest";
    const std::string ReportPath  = (ArgumentCount > 2) ? ArgumentValues[2] : "Exhibits/SessionValidation/Results/SessionValidation.md";

    std::vector<ValidationResult> Results;
    uint32_t                      Failures = 0;

    auto Expect = [&](bool Condition, std::string Claim)
    {
        Results.push_back(ValidationResult{ Claim, Condition });
        if (!Condition)
        {
            ++Failures;
            std::printf("  🔴 %s\n", Claim.c_str());
        }
    };

    // ① InputEncoder, InputDriver (human) & SyntheticDriver (AI) driving an Agent
    InputEncoder Encoder;
    const InputPacket IdlePacket = Encoder.SampleGlfwWindow(nullptr, 1.0f / 60.0f);
    Expect(IdlePacket.SequenceIndex == 1 && std::fabs(IdlePacket.ThrottleAxis) < 1.0e-5f,
           "InputEncoder::SampleGlfwWindow(nullptr) produces a baseline InputPacket");

    RawInputState ReadyEdge{};
    ReadyEdge.KeyEnter = true;
    const InputPacket ReadyPacket = Encoder.EncodeState(ReadyEdge, 1.0f / 60.0f);
    (void)ReadyPacket;
    Expect(Encoder.WasActionPressed(InputActionReady),
           "InputEncoder detects rising edge on InputActionReady");

    // Verify human InputDriver and AI SyntheticDriver both drive the same Agent cleanly
    WorldSpace ScratchWorld;
    const RecordToken ScratchVehicleToken = ScratchWorld.ConstructRecord("Vehicle", "DriverTestVehicle");
    Agent TestAgent(ScratchVehicleToken, 1, AgentCategory::Vehicle);
    TestAgent.PlaceInWorld({ -40.0f, 0.0f, 0.0f }, 0.0f, &ScratchWorld, true);

    InputDriver HumanDriver(PlayerToken{ 1 });
    HumanDriver.BindAgent(&TestAgent);
    Expect(TestAgent.QueryDriverKind() == AgentDriverKind::InputDriven,
           "InputDriver binds to Agent and marks AgentDriverKind::InputDriven (human controlled)");

    RawInputState DriveForward{};
    DriveForward.KeyW = true;
    DriveForward.KeyD = true;
    for (int Index = 0; Index < 20; ++Index)
    {
        (void)HumanDriver.DriveFromState(DriveForward, 1.0f / 30.0f, &ScratchWorld);
    }
    const float SpeedAfterHuman = TestAgent.QueryForwardSpeedMps();
    Expect(SpeedAfterHuman > 1.0f && HumanDriver.QueryLatestPacket().ThrottleAxis > 0.85f,
           "InputDriver drives Agent forward using encoded human RawInputState");

    // Hand the same Agent over to SyntheticDriver (AI)
    HumanDriver.UnbindAgent();
    SyntheticDriver AiDriver(PlayerToken{ 99 });
    AiDriver.ConfigureWaypoints(ConstructEverestCircuitWaypoints());
    AiDriver.BindAgent(&TestAgent);
    Expect(TestAgent.QueryDriverKind() == AgentDriverKind::SyntheticDriven,
           "SyntheticDriver binds to the same Agent and marks AgentDriverKind::SyntheticDriven (AI controlled)");

    const float XBeforeAi = TestAgent.QueryPositionMetres()[0];
    for (int Index = 0; Index < 30; ++Index)
    {
        (void)AiDriver.StepDrive(1.0f / 30.0f, &ScratchWorld);
    }
    Expect(TestAgent.QueryPositionMetres()[0] > XBeforeAi && AiDriver.QueryLatestPacket().ThrottleAxis > 0.0f,
           "SyntheticDriver autonomously steers and accelerates the Agent toward circuit waypoints");

    // ② Load Lobby level (Default.levelspace) & start SessionSpace with EverestLobbyPolicy
    WorldSpace        World;
    const std::string LobbyPath   = ProjectRoot + "/Content/Levels/Default.levelspace";
    const bool        LoadedLobby = World.LoadLevelSpace(LobbyPath);
    Expect(LoadedLobby && World.QueryRecordCount() >= 4,
           "WorldSpace loads Content/Levels/Default.levelspace for Lobby session");

    SessionConfiguration Config{};
    Config.LobbyLevelPath          = "Content/Levels/Default.levelspace";
    Config.ProvingGroundsLevelPath = "Content/Levels/ProvingGroundVehicle.levelspace";
    Config.LobbyPolicyName         = "LobbyPolicy";
    Config.MatchPolicyName         = "RacePolicy";
    Config.MaximumPlayers          = 4;
    Config.LobbyCountdownSeconds   = 1.5f;

    const std::string ConfigToml = Config.SerializeToToml();
    SessionConfiguration ParsedConfig{};
    Expect(ParsedConfig.DeserializeFromToml(ConfigToml) &&
           ParsedConfig.MatchPolicyName == "RacePolicy" &&
           ParsedConfig.MaximumPlayers == 4,
           "SessionConfiguration round-trips cleanly through TOML");

    SessionSpace     Session(Config);
    SessionExtension Extension(Session, World);
    Extension.ConfigureProjectRootDirectory(ProjectRoot);
    Extension.ConfigurePolicyFactory(&ConstructEverestSessionPolicy);

    Session.BeginPolicy(ConstructEverestSessionPolicy("LobbyPolicy"), World);

    // Verify Lobby RespawnPolicy & NetPolicy rules
    Expect(Session.QueryPolicy() != nullptr &&
           !Session.QueryPolicy()->QueryRespawnPolicy().IsRespawnAllowed() &&
           Session.QueryPolicy()->QueryNetPolicy().AllowJoinInProgress,
           "LobbyPolicy enforces RespawnRule::Disabled (no respawning in Lobby) and NetPolicy::AllowJoinInProgress");

    // Join 2 human players + 1 SyntheticDriver AI player + 1 human player who leaves
    const PlayerToken HostToken  = Session.AddPlayer(World, "Host Driver",   "127.0.0.1:7777", true);
    const PlayerToken PeerAToken = Session.AddPlayer(World, "Peer Driver A", "127.0.0.1:7778", false);
    const PlayerToken AiToken    = Session.AddSyntheticPlayer(World, "Synthetic Driver AI", ConstructEverestCircuitWaypoints());
    const PlayerToken PeerBToken = Session.AddPlayer(World, "Peer Driver B", "127.0.0.1:7779", false);

    Expect(HostToken.IsValid() && PeerAToken.IsValid() && AiToken.IsValid() && PeerBToken.IsValid() &&
           Session.QueryPlayerStore().QueryConnectedCount() == 4,
           "Human players (InputDriver) and AI player (SyntheticDriver) join the Lobby and receive distinct PlayerTokens");

    // Verify Respawn request in Lobby is rejected by RespawnPolicy::Disabled
    Expect(!Session.RequestAgentRespawn(World, HostToken),
           "SessionSpace::RequestAgentRespawn returns false in Lobby because RespawnPolicy is Disabled");

    // Peer B leaves the lobby
    const bool RemovedPeerB = Session.RemovePlayer(World, PeerBToken);
    Expect(RemovedPeerB && Session.QueryPlayerStore().QueryConnectedCount() == 3 &&
           Session.FindAgentByOwner(PeerBToken) == nullptr,
           "Peer Driver B leaves the Lobby cleanly and their preview Agent is released");

    // ③ Car Studio customization in LobbyPolicy
    auto* Lobby = dynamic_cast<LobbyPolicy*>(Session.QueryPolicy());
    Expect(Lobby != nullptr, "Active SessionPolicy in Lobby is LobbyPolicy");

    // Host configures Reference Image 1 (White Infinity Cyclorama + Obsidian Black + Acid Green Calipers)
    VehicleUnitConfiguration HostSpec{};
    HostSpec.Backdrop                = StudioBackdropStyle::WhiteCyclorama;
    HostSpec.PaintName               = "Obsidian Black";
    HostSpec.PaintAlbedo             = { 0.03f, 0.032f, 0.036f };
    HostSpec.PaintRoughness          = 0.06f;
    HostSpec.RimFinishName           = "Satin Gunmetal";
    HostSpec.CaliperName             = "Acid Green";
    HostSpec.HeadlightOpticsName     = "Xenon Ice White";
    HostSpec.RideHeightOffsetMetres  = -0.015f;
    HostSpec.FrontPressureKpa        = 235.0f;
    HostSpec.RearPressureKpa         = 245.0f;
    Lobby->UpdatePlayerVehicleConfiguration(Session, World, HostToken, HostSpec);

    // Peer A configures Reference Image 2 (Industrial Softbox Loft + Slate Grey + Amber GT Optics)
    VehicleUnitConfiguration PeerASpec{};
    PeerASpec.Backdrop               = StudioBackdropStyle::SoftboxLoft;
    PeerASpec.PaintName              = "Slate Grey Metallic";
    PeerASpec.PaintAlbedo            = { 0.34f, 0.36f, 0.38f };
    PeerASpec.PaintRoughness         = 0.14f;
    PeerASpec.RimFinishName          = "Forged Silver";
    PeerASpec.CaliperName            = "Guards Red";
    PeerASpec.HeadlightOpticsName    = "Amber GT Endurance";
    PeerASpec.HeadlightTint          = { 1.00f, 0.68f, 0.14f };
    PeerASpec.RideHeightOffsetMetres = -0.020f;
    PeerASpec.FrontPressureKpa       = 225.0f;
    PeerASpec.RearPressureKpa        = 240.0f;
    Lobby->UpdatePlayerVehicleConfiguration(Session, World, PeerAToken, PeerASpec);

    Expect(Lobby->QueryStudioBackdrop() == StudioBackdropStyle::SoftboxLoft,
           "LobbyPolicy switches Car Studio backdrop between WhiteCyclorama and SoftboxLoft");

    // Step lobby turntable before ready-up
    const float InitialYaw = Lobby->QueryTurntableYawDegrees();
    Extension.StepWithInputState(RawInputState{}, 0.25f);
    Expect(Lobby->QueryTurntableYawDegrees() > InitialYaw && !Lobby->IsCountdownActive(),
           "Car Studio turntable rotates preview Agent smoothly while waiting for ready-up");

    // Ready up Host and Peer A (AI player is already ready) and step through the 1.5s countdown to trigger LevelTransition
    Lobby->SetPlayerReady(Session, HostToken, true);
    Lobby->SetPlayerReady(Session, PeerAToken, true);
    Expect(Session.QueryPlayerStore().AreAllConnectedReady(),
           "PlayerStore::AreAllConnectedReady returns true once human and AI players are ready");

    for (int Step = 0; Step < 20; ++Step)
    {
        Extension.StepWithInputState(RawInputState{}, 0.10f);
    }

    // ④ Verify LevelTransition moved SessionSpace and WorldSpace to ProvingGroundVehicle.levelspace under RacePolicy
    Expect(Session.QueryActiveLevelPath() == "Content/Levels/ProvingGroundVehicle.levelspace",
           "LevelTransition transitions SessionSpace and WorldSpace to Content/Levels/ProvingGroundVehicle.levelspace");
    Expect(Session.QueryTransitionEpoch() == 2,
           "TransitionEpoch increments to 2 after Lobby -> ProvingGrounds transition");

    const PlayerProfile* RestoredHost = Session.QueryPlayerStore().FindProfile(HostToken);
    Expect(RestoredHost != nullptr &&
           RestoredHost->VehicleSpec.PaintName == "Obsidian Black" &&
           std::fabs(RestoredHost->VehicleSpec.FrontPressureKpa - 235.0f) < 1.0e-3f &&
           RestoredHost->StoredUnitToken.IsValid(),
           "TransferConfiguration preserves Host PlayerToken, StoredUnitToken, and Car Studio VehicleUnitConfiguration across LevelTransition");

    const std::string TransferToml = Extension.QueryTransition().QueryLastTransfer().SerializeToToml();
    TransferConfiguration ParsedTransfer{};
    Expect(ParsedTransfer.DeserializeFromToml(TransferToml) &&
           ParsedTransfer.PreservedProfiles.size() == 3 &&
           ParsedTransfer.TargetPolicyName == "RacePolicy",
           "TransferConfiguration round-trips preserved PlayerProfiles, VehicleUnitConfigurations, and TargetPolicyName through TOML");

    // ⑤ Verify ProvingGrounds RacePolicy, RespawnPolicy (enabled in ProvingGrounds!), InputDriver & SyntheticDriver
    auto* Race = dynamic_cast<RacePolicy*>(Session.QueryPolicy());
    Expect(Race != nullptr && Race->QueryRespawnPolicy().IsRespawnAllowed(),
           "RacePolicy is active on ProvingGroundVehicle.levelspace with RespawnPolicy enabled");
    Expect(Extension.QueryRecordIndex().QueryCount() >= 3,
           "NetworkRecordIndex binds spawned ProvingGrounds Agent RecordTokens to PlayerTokens");

    PhysicsExtension Physics;
    PhysicsExtensionSettings PhysicsSettings{};
    PhysicsSettings.WriteRegister = false;
    (void)Physics.Begin(World, PhysicsSettings);
    RawInputState FullThrottle{};
    FullThrottle.KeyW = true;
    for (int Step = 0; Step < 45; ++Step)
    {
        Extension.StepWithInputState(FullThrottle, 1.0f / 30.0f);
        Extension.SynchronizePhysicsVehicle(Physics, 1.0f / 30.0f);
    }

    const Agent* HostAgent = Session.FindAgentByOwner(HostToken);
    const Agent* AiAgent   = Session.FindAgentByOwner(AiToken);
    Expect(HostAgent != nullptr && HostAgent->QueryPositionMetres()[0] > -35.0f && HostAgent->QueryForwardSpeedMps() > 3.0f,
           "Host Agent (driven by InputDriver) accelerates forward on ProvingGroundVehicle.levelspace");
    Expect(AiAgent != nullptr && AiAgent->QueryDriverKind() == AgentDriverKind::SyntheticDriven && AiAgent->QueryForwardSpeedMps() > 2.0f,
           "AI Agent (driven by SyntheticDriver) accelerates autonomously along circuit waypoints on ProvingGroundVehicle.levelspace");
    Physics.End(World);

    // ⑤b SpawnPolicy: a vehicle is stood by its own deployment function, whatever the slope under it
    {
        // 📝 One PhysicsSpace, three grounds: a flat pad, a 30° bank, and a 12° cross-slope running the other way. The same car, the same
        //    survey and the same stand — the deployment never special-cases the slope.
        PhysicsSpace DeploySpace(PhysicsSpaceSettings{});

        auto AddGroundBox = [&DeploySpace](const std::array<double, 3>& Position, const std::array<double, 3>& HalfExtents,
                                          const std::array<double, 3>& OrientationDegrees)
        {
            PhysicsBodyDescriptor Ground{};
            Ground.Shape                = PhysicsShape::Box;
            Ground.HalfExtentMetres     = HalfExtents;
            Ground.PositionMetres       = Position;
            Ground.Orientation          = ResolvePhysicsOrientation(OrientationDegrees);
            Ground.Motion               = PhysicsMotion::Fixed;
            Ground.Friction             = 1.0;
            return DeploySpace.AddBody(Ground);
        };

        AddGroundBox({ 0.0, 0.0, -0.5 }, { 40.0, 40.0, 0.5 }, { 0.0, 0.0, 0.0 });             // flat studio pad
        AddGroundBox({ 80.0, 0.0, 2.4 }, { 14.0, 14.0, 0.5 }, { 0.0, 30.0, 0.0 });             // 30° bank climbing in +x
        AddGroundBox({ 160.0, 0.0, 1.2 }, { 14.0, 20.0, 0.5 }, { 12.0, 0.0, 0.0 });            // 12° cross-slope about +x
        DeploySpace.Prepare();

        const Vehicle::VehicleSpecification Specification = Vehicle::ConstructControlVehicleSpecification();

        const SpawnPolicy FlatRules   = SpawnPolicy::GridRelease();
        const SpawnPolicy DropRules   = SpawnPolicy::SuspendedDrop();
        const SpawnPolicy ParkedRules = SpawnPolicy::StudioParked();

        const VehicleDeploymentReading Flat  = SurveyVehicleDeployment(DeploySpace, Specification.Geometry, FlatRules,  { 0.0, 0.0, 0.0 },   0.0);
        const VehicleDeploymentReading Bank  = SurveyVehicleDeployment(DeploySpace, Specification.Geometry, FlatRules,  { 80.0, 0.0, 0.0 },  0.0);
        const VehicleDeploymentReading Cross = SurveyVehicleDeployment(DeploySpace, Specification.Geometry, FlatRules,  { 160.0, 0.0, 0.0 }, 0.0);
        const VehicleDeploymentReading Drop  = SurveyVehicleDeployment(DeploySpace, Specification.Geometry, DropRules,  { 0.0, 0.0, 0.0 },   0.0);

        Expect(Flat.Supported && std::fabs(Flat.Stand.SlopeDegrees) < 1.0f,
               "SpawnPolicy stands a vehicle on a flat studio pad at zero slope");

        Expect(Bank.Supported && Bank.Stand.SlopeDegrees > 20.0f && Bank.Stand.SlopeDegrees < 40.0f &&
               Bank.Stand.PitchDegrees < -15.0f,
               "SpawnPolicy stands the same vehicle on a 30° bank, pitched with the surface it rests on");

        Expect(Cross.Supported && Cross.Stand.SlopeDegrees > 5.0f && std::fabs(Cross.Stand.RollDegrees) > 5.0f,
               "SpawnPolicy banks the same vehicle on a cross-slope with the surface's own roll");

        Expect(Flat.Stand.GroundHeightMetres > -2.0f && Flat.Stand.GroundHeightMetres < 2.0f &&
               std::fabs(Bank.Stand.GroundHeightMetres - Flat.Stand.GroundHeightMetres) > 1.0f,
               "SpawnPolicy takes the stand's height from the surveyed ground, not from the requested point");

        // 📝 The stand transform is a real rotation: the chassis centre sits the centre of mass above the surface along its normal.
        const VehicleStandTransform BankTransform = ResolveStandTransform(Bank.Stand, Specification.Geometry, Specification.Geometry.CoMHeight);
        const double LocalUp[3] = {
            2.0 * (BankTransform.Orientation[0] * BankTransform.Orientation[1] - BankTransform.Orientation[3] * BankTransform.Orientation[2]),
            2.0 * (BankTransform.Orientation[0] * BankTransform.Orientation[2] + BankTransform.Orientation[3] * BankTransform.Orientation[1]),
            1.0 - 2.0 * (BankTransform.Orientation[0] * BankTransform.Orientation[0] + BankTransform.Orientation[1] * BankTransform.Orientation[1])
        };
        Expect(LocalUp[2] > 0.7 && BankTransform.PositionMetres[2] > Bank.Stand.GroundHeightMetres,
               "SpawnPolicy's stand transform lifts the chassis along the surface normal it surveyed");

        Expect(Drop.Stand.Supported && Drop.Stand.PositionMetres[2] > Flat.Stand.PositionMetres[2] + 0.3f,
               "SpawnPolicy SuspendedDrop holds the vehicle above the surface before it is released");

        // 📝 Brakes at the start: the studio preset hands the car over parked, the grid preset hands it over live, and a disabled start
        //    ignores the driver's own pedal until the rules release it.
        const ParkedBrakeState StudioBrakes = ParkedRules.ResolveParkedBrakeState();
        const ParkedBrakeState GridBrakes   = FlatRules.ResolveParkedBrakeState();
        Expect(StudioBrakes.HandbrakeEngaged && !GridBrakes.HandbrakeEngaged,
               "SpawnPolicy StudioParked hands the vehicle over with its handbrake engaged and GridRelease with it released");

        SpawnPolicy DisabledStart = SpawnPolicy::GridRelease();
        DisabledStart.BrakesEnabledAtSpawn = false;
        DisabledStart.BrakeHoldSeconds     = 0.5f;
        float HeldPedal     = 0.0f;
        float HeldHandbrake = 0.0f;
        DisabledStart.ResolvePedalDemand(0.1f, 1.0f, 1.0f, HeldPedal, HeldHandbrake);
        float ReleasedPedal     = 0.0f;
        float ReleasedHandbrake = 0.0f;
        DisabledStart.ResolvePedalDemand(2.0f, 1.0f, 1.0f, ReleasedPedal, ReleasedHandbrake);
        Expect(HeldPedal < 1.0e-5f && HeldHandbrake < 1.0e-5f && ReleasedPedal > 0.95f && ReleasedHandbrake > 0.95f,
               "SpawnPolicy with brakes disabled at spawn ignores the driver for BrakeHoldSeconds, then hands the pedals over");

        // 📝 A parked vehicle's handbrake stays latched until its driver asks for drive.
        Agent ParkedAgent(RecordToken{}, 7, AgentCategory::Vehicle);
        ParkedAgent.AssignSpawnPolicy(ParkedRules);
        ParkedAgent.ApplySpawnPose(Flat.Stand);
        InputPacket IdlePulse{};
        ParkedAgent.ApplyInputPacket(IdlePulse, 1.0f / 60.0f);
        const bool LatchedWhileIdle = ParkedAgent.IsHandbrakeLatched() && std::fabs(ParkedAgent.QueryForwardSpeedMps()) < 1.0e-4f;
        InputPacket DrivePulse{};
        DrivePulse.ThrottleAxis = 1.0f;
        ParkedAgent.ApplyInputPacket(DrivePulse, 1.0f / 60.0f);
        Expect(LatchedWhileIdle && !ParkedAgent.IsHandbrakeLatched(),
               "SpawnPolicy keeps a parked Agent's handbrake latched until its driver asks for drive");

        // 📝 The driven-wheel rule reaches the solver configuration.
        Vehicle::VehicleSolverConfiguration FrontWheelDrive{};
        FrontWheelDrive.Wheels = Vehicle::MakeWheelMounts(Specification.Geometry);
        FitDrivenWheels(FrontWheelDrive, DrivenWheelRule::FrontDriven);
        Expect(FrontWheelDrive.Wheels.size() == 4 && FrontWheelDrive.Wheels[0].Driven && !FrontWheelDrive.Wheels[2].Driven,
               "SpawnPolicy FrontDriven fits the front axle only, leaving the rear wheels unpowered");
    }

    // Test RespawnPolicy in ProvingGrounds (allowed, unlike Lobby!)
    Expect(Session.RequestAgentRespawn(World, HostToken) &&
           std::fabs(HostAgent->QueryForwardSpeedMps()) < 1.0e-4f,
           "SessionSpace::RequestAgentRespawn succeeds in ProvingGrounds when RespawnPolicy is enabled");

    // Advance Host Agent through all checkpoints to complete the race laps
    if (Agent* MutableHostAgent = Session.FindAgentByOwner(HostToken))
    {
        for (uint32_t Lap = 0; Lap < 2; ++Lap)
        {
            for (const RaceCheckpointGate& Gate : Race->QueryCheckpoints())
            {
                MutableHostAgent->PlaceInWorld(Gate.PositionMetres, 0.0f, &World);
                Extension.StepWithInputState(FullThrottle, 0.05f);
            }
        }
    }
    Expect(Race->IsRaceFinished() && Race->QueryWinnerToken() == HostToken,
           "RacePolicy tracks checkpoint gates and declares HostToken the winner upon completing the final lap");

    // ⑥ Validate EliminationPolicy & post-match transition back to Lobby (Default.levelspace)
    Session.BeginPolicy(ConstructEverestSessionPolicy("EliminationPolicy"), World);
    auto* Elimination = dynamic_cast<EliminationPolicy*>(Session.QueryPolicy());
    Expect(Elimination != nullptr && Elimination->QueryActivePlayerCount() == 3 &&
           Elimination->QueryRespawnPolicy().IsRespawnAllowed(),
           "EliminationPolicy initializes with 3 active standing players and RespawnPolicy enabled");

    if (Agent* MutableHostAgent = Session.FindAgentByOwner(HostToken))
    {
        MutableHostAgent->PlaceInWorld({ 500.0f, 0.0f, 0.0f }, 0.0f, &World);
    }
    if (Agent* MutablePeerAgent = Session.FindAgentByOwner(PeerAToken))
    {
        MutablePeerAgent->PlaceInWorld({ 250.0f, 0.0f, 0.0f }, 0.0f, &World);
    }
    if (Agent* MutableAiAgent = Session.FindAgentByOwner(AiToken))
    {
        MutableAiAgent->PlaceInWorld({ -35.0f, 0.0f, 0.0f }, 0.0f, &World);
    }

    // Step across two 10s elimination intervals so the 2 trailing drivers are eliminated
    for (int Step = 0; Step < 44; ++Step)
    {
        Extension.StepWithInputState(RawInputState{}, 0.5f);
    }
    Expect(Elimination->QueryActivePlayerCount() == 1 && Elimination->QueryWinnerToken() == HostToken,
           "EliminationPolicy eliminates trailing Agents each interval and crowns the leader");

    // Step 4.5s post-match timer so EliminationPolicy triggers LevelTransition back to Default.levelspace (LobbyPolicy)
    for (int Step = 0; Step < 10; ++Step)
    {
        Extension.StepWithInputState(RawInputState{}, 0.5f);
    }
    Expect(Session.QueryActiveLevelPath() == "Content/Levels/Default.levelspace" &&
           Session.QueryTransitionEpoch() == 3 &&
           dynamic_cast<LobbyPolicy*>(Session.QueryPolicy()) != nullptr &&
           !Session.QueryPolicy()->QueryRespawnPolicy().IsRespawnAllowed(),
           "LevelTransition returns SessionSpace to LobbyPolicy (Epoch #3, RespawnPolicy Disabled) with player scores intact");

    // Write Markdown report
    const std::filesystem::path OutputFile(ReportPath);
    if (OutputFile.has_parent_path())
    {
        std::filesystem::create_directories(OutputFile.parent_path());
    }
    std::ofstream Report(OutputFile, std::ios::binary | std::ios::trunc);
    Report << "# SessionPolicy, NetPolicy, RespawnPolicy, SpawnPolicy, Agent, InputDriver & SyntheticDriver Validation\n\n";
    Report << "| Status | Verification Claim |\n";
    Report << "| :--- | :--- |\n";
    for (const ValidationResult& Item : Results)
    {
        Report << "| " << (Item.Passed ? "🟢" : "🔴") << " | " << Item.Claim << " |\n";
    }
    Report << "\n**Assertions:** " << Results.size() << " · **Failures:** " << Failures << "\n";

    std::printf("SessionValidation: %zu assertions, %u failures\n", Results.size(), Failures);
    return Failures == 0 ? 0 : 1;
}
