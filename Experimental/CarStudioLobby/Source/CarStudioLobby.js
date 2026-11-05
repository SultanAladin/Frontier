//============================================================================================================================================
//                                                           CARSTUDIOLOBBY.JS
//============================================================================================================================================
// 🧩 Car Studio Configurator Lobby (Default.levelspace (LobbyPolicy)) + Multiplayer SessionSpace (PlayerStore, PlayerProfile, PlayerToken) +
//    LevelTransition (TransferConfiguration) -> ProvingGroundVehicle.levelspace (RacePolicy / EliminationPolicy) + DeployableUnit & InputEncoder.

import {
    PaintColours,
    FinishPresets,
    RimPresets,
    CaliperPresets,
    HeadlightOpticsPresets,
    StudioEnvironmentPresets
} from "./VehicleAppearance.js";
import { AutomotiveMode, RoofFixtureType } from "./LightingSystem.js";
import { StudioConstants, ArenaZones, TrainingDrills } from "./LevelStructure.js";

//------------------------------------------------------------------------------------------------------------------------
//                                      ENGINE/INPUT: INPUTPACKET & INPUTENCODER
//------------------------------------------------------------------------------------------------------------------------

export class InputPacket
{
    constructor()
    {
        this.SequenceIndex  = 0;
        this.ThrottleAxis   = 0.0;
        this.BrakeAxis      = 0.0;
        this.SteerAxis      = 0.0;
        this.HandbrakeAxis  = 0.0;
        this.LookYawDelta   = 0.0;
        this.LookPitchDelta = 0.0;
        this.ActionBits     = 0;
    }
}

export class InputEncoder
{
    constructor()
    {
        this.Sequence = 0;
        this.LatestPacket = new InputPacket();
    }

    EncodeFromDriverControls(Controls)
    {
        const Packet = new InputPacket();
        Packet.SequenceIndex = ++this.Sequence;
        Packet.ThrottleAxis  = Controls?.Throttle ?? 0.0;
        Packet.BrakeAxis     = Controls?.Brake    ?? 0.0;
        Packet.SteerAxis     = Controls?.Steer    ?? 0.0;
        Packet.HandbrakeAxis = Controls?.Handbrake ? 1.0 : 0.0;
        Packet.ActionBits    = (Controls?.Handbrake ? 1 : 0)
                             | (Controls?.Reverse   ? 2 : 0)
                             | (Controls?.Boost     ? 4 : 0);
        this.LatestPacket = Packet;
        return Packet;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                      ENGINE/SCENE: DEPLOYABLEUNIT
//------------------------------------------------------------------------------------------------------------------------

export class DeployableUnit
{
    constructor(UnitToken, OwnerPlayerToken, Spec)
    {
        this.UnitToken        = UnitToken;
        this.OwnerPlayerToken = OwnerPlayerToken;
        this.Spec             = { ...Spec };
        this.LastInputPacket  = new InputPacket();
        this.Position         = [StudioConstants.Centre[0], StudioConstants.Centre[1], 0.0];
        this.Yaw              = StudioConstants.CarYaw;
        this.SpeedKmh         = 0.0;
    }

    ApplyInputPacket(Packet)
    {
        this.LastInputPacket = Packet;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                 ENGINE/SESSION: PLAYERTOKEN, PLAYERPROFILE, PLAYERSTORE
//------------------------------------------------------------------------------------------------------------------------

const PeerCallsigns = [
    { Name: "V. Källenius", PaintIndex: 2, FinishId: "gloss",    RimId: "weissach_au", CaliperId: "yellow", OpticsId: "xenon" },
    { Name: "M. Webber",    PaintIndex: 4, FinishId: "metallic", RimId: "satin_black", CaliperId: "red",    OpticsId: "amber" },
    { Name: "K. Estre",     PaintIndex: 8, FinishId: "gloss",    RimId: "silver",      CaliperId: "black",  OpticsId: "amber" },
    { Name: "L. Vanthoor",  PaintIndex: 3, FinishId: "satin",    RimId: "gunmetal",    CaliperId: "acid",   OpticsId: "laser" },
    { Name: "S. Dumas",     PaintIndex: 7, FinishId: "metallic", RimId: "mag_blue",    CaliperId: "yellow", OpticsId: "xenon" }
];

export class PlayerStore
{
    constructor()
    {
        this.NextTokenValue = 1001;
        this.Profiles       = [];
        this.PeerCursor     = 0;
    }

    RegisterPlayer(DisplayName, IsLocalHost = false, Spec = {})
    {
        const Token = { Value: this.NextTokenValue++ };
        const Profile = {
            Token,
            DisplayName,
            IsLocalHost,
            Connected:       true,
            Ready:           false,
            ActiveUnitToken: { Value: Token.Value + 5000 },
            StoredUnitToken: { Value: 0 },
            Score:           0,
            Lap:             1,
            GateIndex:       0,
            BestLapSeconds:  null,
            Eliminated:      false,
            DistanceToGate:  0.0,
            Spec: {
                PaintIndex:   Spec.PaintIndex   ?? 0,
                CustomHex:    Spec.CustomHex    ?? null,
                FinishId:     Spec.FinishId     ?? "metallic",
                RimId:        Spec.RimId        ?? "satin_black",
                CaliperId:    Spec.CaliperId    ?? "red",
                OpticsId:     Spec.OpticsId     ?? "amber",
                RoofFixture:  Spec.RoofFixture  ?? RoofFixtureType.None,
                FrontKpa:     Spec.FrontKpa     ?? 220,
                RearKpa:      Spec.RearKpa      ?? 220,
                WindowSmoke:  Spec.WindowSmoke  ?? false
            }
        };
        this.Profiles.push(Profile);
        return Profile;
    }

    RemovePlayer(TokenValue)
    {
        const Index = this.Profiles.findIndex((P) => P.Token.Value === TokenValue && !P.IsLocalHost);
        if (Index >= 0)
        {
            this.Profiles.splice(Index, 1);
            return true;
        }
        return false;
    }

    FindProfile(TokenValue)
    {
        return this.Profiles.find((P) => P.Token.Value === TokenValue) || null;
    }

    LocalHostProfile()
    {
        return this.Profiles.find((P) => P.IsLocalHost) || this.Profiles[0] || null;
    }

    AreAllConnectedReady()
    {
        const Connected = this.Profiles.filter((P) => P.Connected);
        return Connected.length > 0 && Connected.every((P) => P.Ready);
    }

    CreateTransferConfiguration(SourceLevel, TargetLevel, ProcedureId, Epoch)
    {
        return {
            TransitionEpoch: Epoch,
            SourceLevel,
            TargetLevel,
            ProcedureId,
            PreservedPlayers: this.Profiles.map((P) => ({
                TokenValue:      P.Token.Value,
                DisplayName:     P.DisplayName,
                IsLocalHost:     P.IsLocalHost,
                StoredUnitToken: P.ActiveUnitToken.Value,
                Score:           P.Score,
                Spec:            { ...P.Spec }
            }))
        };
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                      CAMERA PRESETS FOR CAR STUDIO LOBBY
//------------------------------------------------------------------------------------------------------------------------

export const StudioCameraPresets = Object.freeze([
    { Id: "ref2_loft",   Name: "3/4 Softbox Loft (Ref 2)", Yaw: -0.58, Pitch: 0.145, Distance: 7.45 },
    { Id: "ref1_cyclo",  Name: "Low Cyclorama (Ref 1)",    Yaw:  0.46, Pitch: 0.082, Distance: 6.15 },
    { Id: "profile",     Name: "Side Profile",             Yaw:  1.57, Pitch: 0.075, Distance: 6.40 },
    { Id: "front_quart", Name: "Front Optics & Splitter",  Yaw:  0.22, Pitch: 0.110, Distance: 5.20 },
    { Id: "rear_aero",   Name: "Rear Swan-Neck Wing",      Yaw:  2.62, Pitch: 0.160, Distance: 5.60 },
    { Id: "softbox_up",  Name: "Overhead Softbox Canopy",  Yaw: -0.45, Pitch: 0.055, Distance: 8.60 }
]);

//------------------------------------------------------------------------------------------------------------------------
//                                      CAR STUDIO LOBBY & SESSION CONTROLLER
//------------------------------------------------------------------------------------------------------------------------

export class CarStudioLobby
{
    constructor(Host)
    {
        this.Host                 = Host;
        this.PlayerStore          = new PlayerStore();
        this.InputEncoder         = new InputEncoder();
        this.CurrentLevel         = "Content/Levels/Default.levelspace (LobbyPolicy)";
        this.TargetProcedure      = "RacePolicy"; // "RacePolicy" | "EliminationPolicy"
        this.TransitionEpoch      = 1;
        this.TransitionActive     = false;
        this.TransitionCountdown  = 0.0;
        this.LastTransfer         = null;

        this.StudioPresetId       = "softbox";
        this.TurntableSpin        = false;
        this.TurntableSpeed       = 0.28; // [rad/s]
        this.TurntableAngle       = StudioConstants.CarYaw;
        this.ActiveCameraPreset   = "ref2_loft";

        // Match Procedure runtime state
        this.MatchElapsed         = 0.0;
        this.MatchTotalLaps       = 3;
        this.EliminationInterval  = 25.0;
        this.EliminationTimer     = 25.0;
        this.MatchFinished        = false;
        this.MatchWinner          = null;

        // Register Local Host + 1 initial remote peer in the Lobby
        this.LocalProfile = this.PlayerStore.RegisterPlayer("Player 01 (Host)", true, {
            PaintIndex:  0, // Slate Grey Metallic (Ref 2)
            FinishId:    "metallic",
            RimId:       "satin_black",
            CaliperId:   "red",
            OpticsId:    "amber",
            RoofFixture: 0,
            FrontKpa:    220,
            RearKpa:     220,
            WindowSmoke: false
        });
        this.LocalUnit = new DeployableUnit(this.LocalProfile.ActiveUnitToken, this.LocalProfile.Token, this.LocalProfile.Spec);

        // Seed one remote peer in the lobby so the multiplayer roster is immediately visible
        this.SimulatePeerJoin(true);

        this.BuildInterface();
        this.ApplyReferencePreset("ref2_softbox", false);
    }

    IsInLobby()
    {
        return this.CurrentLevel.endsWith("Default.levelspace (LobbyPolicy)");
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                      STUDIO & VEHICLE SPEC APPLICATION
    //--------------------------------------------------------------------------------------------------------------------

    ApplyReferencePreset(PresetKey, UpdateCamera = true)
    {
        const Spec = this.LocalProfile.Spec;
        if (PresetKey === "ref1_cyclorama")
        {
            // Reference Image 1: Seamless White Infinity Cyclorama + Glossy Reflective Turntable Floor + Obsidian Black GT3 RS
            this.StudioPresetId = "cyclorama";
            Spec.PaintIndex     = 1; // Obsidian Black
            Spec.CustomHex      = null;
            Spec.FinishId       = "gloss";
            Spec.RimId          = "satin_black";
            Spec.CaliperId      = "red";
            Spec.OpticsId       = "xenon";
            Spec.RoofFixture    = 0;
            this.ActiveCameraPreset = "ref1_cyclo";
        }
        else
        {
            // Reference Image 2: Dark Industrial Truss Ceiling + Giant Suspended Rectangular Softbox + White Stage Pad + Slate Grey + Amber Lights
            this.StudioPresetId = "softbox";
            Spec.PaintIndex     = 0; // Slate Grey Metallic
            Spec.CustomHex      = null;
            Spec.FinishId       = "metallic";
            Spec.RimId          = "satin_black";
            Spec.CaliperId      = "black";
            Spec.OpticsId       = "amber";
            Spec.RoofFixture    = 0;
            this.ActiveCameraPreset = "ref2_loft";
        }

        this.SyncSpecToRenderer( true );
        if (UpdateCamera || this.IsInLobby())
        {
            this.SelectCameraPreset(this.ActiveCameraPreset);
        }
        this.RefreshInterface();
    }

    SyncSpecToRenderer(StudioChanged = false)
    {
        const Spec = this.LocalProfile.Spec;
        const Appearance = this.Host.Cabin?.Appearance;
        if (Appearance)
        {
            Appearance.FinishId = Spec.FinishId;
            Appearance.SelectIndex(Spec.PaintIndex);
            if (Spec.CustomHex)
                Appearance.SelectCustomHex(Spec.CustomHex);
            Appearance.SelectFinish(Spec.FinishId);
            Appearance.SelectRim(Spec.RimId);
            Appearance.SelectCaliper(Spec.CaliperId);
            const Optics = Appearance.SelectHeadlightOptics(Spec.OpticsId);
            Appearance.SetWindowSmoke(Spec.WindowSmoke);
            if (this.Host.Lighting && Optics)
            {
                this.Host.Lighting.HeadlightColour  = [...Optics.BeamColour];
                this.Host.Lighting.VehicleLightMode = AutomotiveMode.LowBeam;
                this.Host.Lighting.RoofLightEnabled = Spec.RoofFixture > 0;
                this.Host.Lighting.RoofFixture      = Spec.RoofFixture === 2 ? RoofFixtureType.RallyPod : RoofFixtureType.SlimLED;
                this.Host.Lighting.StudioMode       = this.StudioPresetId;
            }
            if (StudioChanged)
            {
                Appearance.SelectStudioEnvironment(this.StudioPresetId);
            }
        }
        if (this.Host?.InflateTyres)
            this.Host.InflateTyres(Spec.FrontKpa, Spec.RearKpa);

        // Configure studio lighting atmosphere in IlluminationHost when inside Default.levelspace (LobbyPolicy)
        if (this.IsInLobby() && this.Host?.Settings)
        {
            if (this.StudioPresetId === "cyclorama")
            {
                this.Host.Settings.SunElevation       = 68.0;
                this.Host.Settings.SunAzimuth         = 40.0;
                this.Host.Settings.SolarIlluminance   = 1.65;
                this.Host.Settings.SkyScale           = 0.85;
                this.Host.Settings.IndirectScale      = 1.25;
                this.Host.Settings.EmissionScale      = 1.25;
                this.Host.Settings.ScreenTraversal    = true;
                this.Host.Settings.TriangleReflection = true;
            }
            else
            {
                // Industrial Softbox Loft: dark overhead ceiling, dramatic overhead softbox + warm amber headlights
                this.Host.Settings.SunElevation       = 24.0;
                this.Host.Settings.SunAzimuth         = 115.0;
                this.Host.Settings.SolarIlluminance   = 0.22;
                this.Host.Settings.SkyScale           = 0.14;
                this.Host.Settings.IndirectScale      = 1.35;
                this.Host.Settings.EmissionScale      = 1.45;
                this.Host.Settings.ScreenTraversal    = true;
                this.Host.Settings.TriangleReflection = true;
            }
            if (StudioChanged && this.Host.Cards && this.Host.Scene && this.Host.VertexRecords)
            {
                this.Host.Cards.CaptureCards(this.Host.Scene, this.Host.VertexRecords);
                this.Host.Cards.InvalidateLighting();
                this.Host.PriorValid = false;
            }
        }

        // Notify fallback renderer if active
        if (this.FallbackRenderer)
        {
            this.FallbackRenderer.SyncFromLobby(this);
        }
    }

    SelectCameraPreset(PresetId)
    {
        const Preset = StudioCameraPresets.find((P) => P.Id === PresetId) || StudioCameraPresets[0];
        this.ActiveCameraPreset = Preset.Id;
        if (this.Host?.Orbit)
        {
            this.Host.Settings.CameraMode = "Orbit";
            this.Host.Orbit.Yaw           = Preset.Yaw;
            this.Host.Orbit.Pitch         = Preset.Pitch;
            this.Host.Orbit.Distance      = Preset.Distance;
            this.Host.PriorValid          = false;
            this.Host.Shell?.Controls?.Synchronise?.();
        }
        if (this.FallbackRenderer)
        {
            this.FallbackRenderer.SetCamera(Preset.Yaw, Preset.Pitch, Preset.Distance);
        }
        this.RefreshInterface();
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                      MULTIPLAYER LOBBY ROSTER ACTIONS
    //--------------------------------------------------------------------------------------------------------------------

    SimulatePeerJoin(InitialReady = false)
    {
        if (this.PlayerStore.Profiles.length >= 4)
            return null;
        const Template = PeerCallsigns[this.PlayerStore.PeerCursor % PeerCallsigns.length];
        this.PlayerStore.PeerCursor++;
        const Peer = this.PlayerStore.RegisterPlayer(Template.Name, false, {
            PaintIndex:  Template.PaintIndex,
            FinishId:    Template.FinishId,
            RimId:       Template.RimId,
            CaliperId:   Template.CaliperId,
            OpticsId:    Template.OpticsId,
            RoofFixture: RoofFixtureType.None,
            FrontKpa:    220,
            RearKpa:     220
        });
        Peer.Ready = Boolean(InitialReady);
        this.EvaluateLobbyReadiness();
        this.RefreshInterface();
        return Peer;
    }

    SimulatePeerLeave(TokenValue)
    {
        if (this.PlayerStore.RemovePlayer(TokenValue))
        {
            this.EvaluateLobbyReadiness();
            this.RefreshInterface();
        }
    }

    TogglePlayerReady(TokenValue)
    {
        const Profile = this.PlayerStore.FindProfile(TokenValue);
        if (!Profile)
            return;
        Profile.Ready = !Profile.Ready;
        this.EvaluateLobbyReadiness();
        this.RefreshInterface();
    }

    RandomizePeerSpec(TokenValue)
    {
        const Profile = this.PlayerStore.FindProfile(TokenValue);
        if (!Profile)
            return;
        Profile.Spec.PaintIndex = (Profile.Spec.PaintIndex + 1 + Math.floor(Math.random() * (PaintColours.length - 1))) % PaintColours.length;
        Profile.Spec.FinishId   = FinishPresets[Math.floor(Math.random() * FinishPresets.length)].Id;
        Profile.Spec.RimId      = RimPresets[Math.floor(Math.random() * RimPresets.length)].Id;
        Profile.Spec.CaliperId  = CaliperPresets[Math.floor(Math.random() * CaliperPresets.length)].Id;
        if (Profile.IsLocalHost)
            this.SyncSpecToRenderer(false);
        this.RefreshInterface();
    }

    EvaluateLobbyReadiness()
    {
        if (!this.IsInLobby())
            return;
        if (this.PlayerStore.AreAllConnectedReady())
        {
            if (!this.TransitionActive)
            {
                this.TransitionActive    = true;
                this.TransitionCountdown = 2.4;
            }
        }
        else
        {
            this.TransitionActive    = false;
            this.TransitionCountdown = 0.0;
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                         LEVEL TRANSITION: LOBBY.LEVELSPACE <-> PROVINGGROUNDVEHICLE.LEVELSPACE
    //--------------------------------------------------------------------------------------------------------------------

    ExecuteTransitionToMatch()
    {
        this.TransitionActive    = false;
        this.TransitionCountdown = 0.0;
        this.TransitionEpoch++;
        this.LastTransfer = this.PlayerStore.CreateTransferConfiguration(
            "Content/Levels/Default.levelspace (LobbyPolicy)",
            "Content/Levels/ProvingGroundVehicle.levelspace",
            this.TargetProcedure,
            this.TransitionEpoch
        );
        this.CurrentLevel     = "Content/Levels/ProvingGroundVehicle.levelspace";
        this.TurntableSpin    = false;
        this.MatchElapsed     = 0.0;
        this.EliminationTimer = this.EliminationInterval;
        this.MatchFinished    = false;
        this.MatchWinner      = null;

        for (const Profile of this.PlayerStore.Profiles)
        {
            Profile.Lap            = 1;
            Profile.GateIndex      = 0;
            Profile.Eliminated     = false;
            Profile.DistanceToGate = 0.0;
        }

        // Transition Host renderer to ProvingGroundVehicle.levelspace track environment & spawn DeployableUnit on Start Grid
        if (this.Host?.Settings)
        {
            this.Host.Settings.SunElevation     = 32.0;
            this.Host.Settings.SunAzimuth       = 38.0;
            this.Host.Settings.SolarIlluminance = 3.20;
            this.Host.Settings.SkyScale         = 1.00;
            this.Host.Settings.IndirectScale    = 1.00;
            this.Host.Settings.EmissionScale    = 1.00;
            this.Host.Settings.CameraMode       = "Chase";
            if (this.Host.TeleportVehicle && ArenaZones[1])
                this.Host.TeleportVehicle(ArenaZones[1], true);
            this.Host.Shell?.Controls?.Synchronise?.();
            if (this.Host.Cards)
                this.Host.Cards.InvalidateLighting();
        }
        if (this.FallbackRenderer)
        {
            this.FallbackRenderer.ResetToTrack();
        }
        this.SyncSpecToRenderer(false);
        this.RefreshInterface();
    }

    ExecuteTransitionToLobby()
    {
        this.TransitionActive    = false;
        this.TransitionCountdown = 0.0;
        this.TransitionEpoch++;
        this.LastTransfer = this.PlayerStore.CreateTransferConfiguration(
            "Content/Levels/ProvingGroundVehicle.levelspace",
            "Content/Levels/Default.levelspace (LobbyPolicy)",
            "LobbyPolicy · RespawnPolicy: Disabled · SpawnPolicy: StudioParked",
            this.TransitionEpoch
        );
        this.CurrentLevel = "Content/Levels/Default.levelspace (LobbyPolicy)";
        for (const Profile of this.PlayerStore.Profiles)
        {
            if (Profile.IsLocalHost)
                Profile.Ready = false;
        }

        if (this.Host)
        {
            if (this.Host.TeleportVehicle && ArenaZones[0])
                this.Host.TeleportVehicle(ArenaZones[0], true);
            this.SelectCameraPreset(this.ActiveCameraPreset);
        }
        if (this.FallbackRenderer)
        {
            this.FallbackRenderer.ResetToStudio();
        }
        this.SyncSpecToRenderer(true);
        this.RefreshInterface();
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                             FRAME UPDATE
    //--------------------------------------------------------------------------------------------------------------------

    Step(DeltaSeconds, Controls)
    {
        const ClampedDt = Math.min(Math.max(DeltaSeconds, 0.0), 0.1);
        const Packet = this.InputEncoder.EncodeFromDriverControls(Controls);
        this.LocalUnit.ApplyInputPacket(Packet);

        if (this.IsInLobby())
        {
            // Rotate vehicle smoothly on the Car Studio turntable when Turntable Auto-Spin is enabled
            if (this.Host?.Vehicle && this.TurntableSpin)
            {
                const [Sx, Sy] = StudioConstants.Centre;
                this.TurntableAngle = (this.TurntableAngle + this.TurntableSpeed * ClampedDt) % (Math.PI * 2.0);
                this.Host.Vehicle.Respawn([Sx, Sy, 0.63], this.TurntableAngle);
                this.Host.AfterVehicleMoved();
            }

            if (this.TransitionActive)
            {
                this.TransitionCountdown -= ClampedDt;
                this.UpdateTransitionBanner();
                if (this.TransitionCountdown <= 0.0)
                {
                    this.ExecuteTransitionToMatch();
                }
            }
            return;
        }

        // Active in ProvingGroundVehicle.levelspace: step RacePolicy or EliminationPolicy
        this.StepMatchProcedure(ClampedDt);
    }

    StepMatchProcedure(DeltaSeconds)
    {
        if (!this.MatchFinished)
            this.MatchElapsed += DeltaSeconds;

        const CircuitGates = [
            { Position: [-22.0, 0.0], Radius: 8.5, Label: "Start Gantry" },
            { Position: [ 58.0, 0.0], Radius: 8.5, Label: "Finish Gantry" },
            { Position: [ 95.0, 0.0], Radius: 9.0, Label: "East Slalom" },
            { Position: [ 34.0, 38.5], Radius: 9.0, Label: "Service Hangar" },
            { Position: [-30.0, 18.0], Radius: 8.5, Label: "Skidpad South" },
            { Position: [-36.0, 0.0], Radius: 8.5, Label: "Lap Line" }
        ];

        const Pos = this.Host?.Vehicle?.Position || this.FallbackRenderer?.CarPosition || [-40.0, 0.0, 0.0];
        const Vel = this.Host?.Vehicle?.LinearVelocity || [0.0, 0.0, 0.0];
        this.LocalUnit.Position = Pos;
        this.LocalUnit.SpeedKmh = Math.hypot(Vel[0], Vel[1]) * 3.6;

        const Gate = CircuitGates[this.LocalProfile.GateIndex % CircuitGates.length];
        const Dist = Math.hypot(Pos[0] - Gate.Position[0], Pos[1] - Gate.Position[1]);
        this.LocalProfile.DistanceToGate = Dist;

        if (!this.MatchFinished && !this.LocalProfile.Eliminated && Dist <= Gate.Radius)
        {
            this.LocalProfile.GateIndex++;
            this.LocalProfile.Score += 150;
            if (this.LocalProfile.GateIndex >= CircuitGates.length)
            {
                this.LocalProfile.GateIndex = 0;
                const LapTime = this.MatchElapsed;
                if (!this.LocalProfile.BestLapSeconds || LapTime < this.LocalProfile.BestLapSeconds)
                    this.LocalProfile.BestLapSeconds = LapTime;
                if (this.TargetProcedure === "RacePolicy" && this.LocalProfile.Lap >= this.MatchTotalLaps)
                {
                    this.MatchFinished = true;
                    this.MatchWinner   = this.LocalProfile.DisplayName;
                }
                else
                {
                    this.LocalProfile.Lap++;
                }
            }
        }

        // Advance simulated peer telemetry along the circuit
        this.PlayerStore.Profiles.forEach((Peer, Index) =>
        {
            if (Peer.IsLocalHost || Peer.Eliminated || this.MatchFinished)
                return;
            const SpeedFactor = 0.24 + (Index * 0.04);
            Peer.DistanceToGate = Math.max(2.0, 40.0 - ((this.MatchElapsed * 14.0 * SpeedFactor) % 38.0));
            const TotalGatesPassed = Math.floor(this.MatchElapsed * SpeedFactor);
            Peer.Lap       = Math.min(this.MatchTotalLaps, 1 + Math.floor(TotalGatesPassed / CircuitGates.length));
            Peer.GateIndex = TotalGatesPassed % CircuitGates.length;
        });

        if (this.TargetProcedure === "EliminationPolicy" && !this.MatchFinished)
        {
            this.EliminationTimer -= DeltaSeconds;
            if (this.EliminationTimer <= 0.0)
            {
                this.EliminationTimer = this.EliminationInterval;
                const Active = this.PlayerStore.Profiles.filter((P) => !P.Eliminated);
                if (Active.length > 1)
                {
                    Active.sort((A, B) =>
                        (B.Lap * 100 + B.GateIndex * 10 - B.DistanceToGate * 0.1) -
                        (A.Lap * 100 + A.GateIndex * 10 - A.DistanceToGate * 0.1)
                    );
                    const Last = Active[Active.length - 1];
                    Last.Eliminated = true;
                }
                const Remaining = this.PlayerStore.Profiles.filter((P) => !P.Eliminated);
                if (Remaining.length === 1)
                {
                    this.MatchFinished = true;
                    this.MatchWinner   = Remaining[0].DisplayName;
                }
            }
        }

        this.UpdateMatchHud(CircuitGates);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                             UI CONSTRUCTION
    //--------------------------------------------------------------------------------------------------------------------

    BuildInterface()
    {
        const Viewport = document.querySelector(".viewport");
        if (!Viewport)
            return;

        // Default to clean full-bleed Studio view so the Car Studio Configurator is front and center
        document.body.classList.add("panels-hidden", "studio-lobby-active");

        const Overlay = document.createElement("div");
        Overlay.id = "car-studio-overlay";
        Overlay.innerHTML = `
            <!-- TOP SESSION & LEVEL BAR -->
            <div class="cs-topbar">
                <div class="cs-brand">
                    <span class="cs-badge">FRONTIER · SESSION</span>
                    <span class="cs-level-tag" id="cs-level-label">Default.levelspace (LobbyPolicy) · Car Studio Configurator</span>
                    <span class="cs-epoch-tag" id="cs-epoch-label">Epoch #1 · LobbyPolicy · RespawnPolicy: Disabled · SpawnPolicy: StudioParked</span>
                </div>
                <div class="cs-StudioPresets" id="cs-studio-preset-bar">
                    <button type="button" class="cs-preset-pill is-active" data-ref-preset="ref2_softbox" title="Reference Photo 2: Dark industrial ceiling + suspended giant rectangular softbox + white stage pad + Slate Grey + Amber headlights">
                        <span class="cs-dot" style="background:#ffae19"></span>
                        Studio 2 · Softbox Loft (Ref 2)
                    </button>
                    <button type="button" class="cs-preset-pill" data-ref-preset="ref1_cyclorama" title="Reference Photo 1: Seamless white infinity cyclorama + mirror-gloss reflective turntable floor + Obsidian Black + Red calipers">
                        <span class="cs-dot" style="background:#ffffff"></span>
                        Studio 1 · White Cyclorama (Ref 1)
                    </button>
                </div>
                <div class="cs-top-actions">
                    <button type="button" class="cs-action-btn" id="cs-btn-return-lobby" style="display:none">
                        ← Return to Car Studio (Default.levelspace (LobbyPolicy))
                    </button>
                    <button type="button" class="cs-action-btn" id="cs-btn-toggle-editor" title="Toggle Frontier Outliner & Render Inspector">
                        Engine Inspector
                    </button>
                </div>
            </div>

            <!-- LEFT DOCK: MULTIPLAYER SESSION ROSTER (PlayerStore / SessionSpace) -->
            <aside class="cs-dock cs-dock-left" id="cs-lobby-left">
                <div class="cs-card">
                    <div class="cs-card-head">
                        <div>
                            <div class="cs-kicker">ENGINE / SESSION / PLAYERSTORE</div>
                            <h2 class="cs-title">Session Roster</h2>
                        </div>
                        <span class="cs-count-pill" id="cs-player-count">2 / 4 Players</span>
                    </div>
                    <div class="cs-roster-list" id="cs-roster-list"></div>
                    <div class="cs-roster-actions">
                        <button type="button" class="cs-btn-secondary" id="cs-btn-add-peer">+ Simulate Player Join</button>
                        <button type="button" class="cs-btn-secondary" id="cs-btn-ready-peers">Toggle Peers Ready</button>
                    </div>
                </div>

                <div class="cs-card">
                    <div class="cs-card-head">
                        <div>
                            <div class="cs-kicker">SESSIONPOLICY · NETPOLICY · RESPAWNPOLICY</div>
                            <h2 class="cs-title">ProvingGrounds SessionPolicy</h2>
                        </div>
                    </div>
                    <div class="cs-procedure-grid" id="cs-procedure-grid">
                        <button type="button" class="cs-proc-btn is-selected" data-proc="RacePolicy">
                            <div class="cs-proc-name">RacePolicy</div>
                            <div class="cs-proc-desc">ProvingGroundVehicle.levelspace · 3-Lap Circuit · Respawn: LastCheckpoint · Spawn: GridRelease</div>
                        </button>
                        <button type="button" class="cs-proc-btn" data-proc="EliminationPolicy">
                            <div class="cs-proc-name">EliminationPolicy</div>
                            <div class="cs-proc-desc">ProvingGroundVehicle.levelspace · Elimination · Respawn: SpawnGrid · Spawn: GridRelease</div>
                        </button>
                    </div>
                </div>

                <div class="cs-card cs-card-compact">
                    <div class="cs-kicker">STUDIO CAMERA & TURNTABLE</div>
                    <div class="cs-cam-grid" id="cs-cam-grid"></div>
                    <div class="cs-turntable-row">
                        <label class="cs-check-label">
                            <input type="checkbox" id="cs-turntable-toggle" />
                            <span>Turntable Auto-Spin</span>
                        </label>
                        <input type="range" id="cs-turntable-yaw" min="-3.14" max="3.14" step="0.02" value="0.56" title="Rotate Studio Turntable" />
                    </div>
                </div>
            </aside>

            <!-- RIGHT DOCK: CAR STUDIO CONFIGURATOR (Agent & VehicleAppearance) -->
            <aside class="cs-dock cs-dock-right" id="cs-lobby-right">
                <div class="cs-card">
                    <div class="cs-card-head">
                        <div>
                            <div class="cs-kicker">DEPLOYABLEUNIT SPECIFICATION</div>
                            <h2 class="cs-title">Car Studio Configurator</h2>
                        </div>
                        <span class="cs-spec-badge" id="cs-active-paint-name">Slate Grey</span>
                    </div>

                    <!-- Studio Architecture Mode -->
                    <div class="cs-section">
                        <div class="cs-section-label">Studio Architecture & Lighting</div>
                        <div class="cs-seg-row" id="cs-studio-env-seg">
                            <button type="button" class="cs-seg-btn is-selected" data-studio-env="softbox">Softbox Loft (Ref 2)</button>
                            <button type="button" class="cs-seg-btn" data-studio-env="cyclorama">White Cyclorama (Ref 1)</button>
                        </div>
                    </div>

                    <!-- Exterior Paint Swatches + Custom Picker -->
                    <div class="cs-section">
                        <div class="cs-section-label">
                            <span>Body Coat Paint</span>
                            <label class="cs-hex-picker">
                                <span>Custom</span>
                                <input type="color" id="cs-custom-color" value="#585f68" />
                            </label>
                        </div>
                        <div class="cs-swatch-grid" id="cs-paint-swatches"></div>
                    </div>

                    <!-- Clearcoat & Surface Finish -->
                    <div class="cs-section">
                        <div class="cs-section-label">Clearcoat Finish</div>
                        <div class="cs-seg-row" id="cs-finish-seg"></div>
                    </div>

                    <!-- Forged Rims -->
                    <div class="cs-section">
                        <div class="cs-section-label">Forged Wheel Rim Finish</div>
                        <div class="cs-chip-row" id="cs-rim-chips"></div>
                    </div>

                    <!-- Brake Calipers -->
                    <div class="cs-section">
                        <div class="cs-section-label">Ceramic Brake Calipers</div>
                        <div class="cs-chip-row" id="cs-caliper-chips"></div>
                    </div>

                    <!-- Headlight Optics & Roof Equipment -->
                    <div class="cs-section">
                        <div class="cs-section-label">Headlight Optics & Aux Beam</div>
                        <div class="cs-seg-row" id="cs-optics-seg"></div>
                        <div class="cs-seg-row" id="cs-roof-seg" style="margin-top:6px">
                            <button type="button" class="cs-seg-btn is-selected" data-roof="0">Slicktop</button>
                            <button type="button" class="cs-seg-btn" data-roof="1">Roof Lightbar</button>
                            <button type="button" class="cs-seg-btn" data-roof="2">Rally Pods</button>
                        </div>
                    </div>

                    <!-- Stance / Tyre Pressures & Glazing -->
                    <div class="cs-section">
                        <div class="cs-section-label">
                            <span>Tyre Pressure (Front / Rear)</span>
                            <span class="cs-mono" id="cs-pressure-readout">220 / 220 kPa</span>
                        </div>
                        <div class="cs-dual-sliders">
                            <input type="range" id="cs-front-kpa" min="160" max="300" step="5" value="220" />
                            <input type="range" id="cs-rear-kpa"  min="160" max="300" step="5" value="220" />
                        </div>
                        <label class="cs-check-label" style="margin-top:6px">
                            <input type="checkbox" id="cs-window-smoke" />
                            <span>Smoked Studio Privacy Glazing</span>
                        </label>
                    </div>
                </div>
            </aside>

            <!-- BOTTOM BAR: READY UP & LEVELTRANSITION -->
            <div class="cs-bottombar" id="cs-lobby-bottom">
                <div class="cs-bottom-info">
                    <div class="cs-kicker">LEVELTRANSITION · TRANSFERCONFIGURATION</div>
                    <div class="cs-bottom-status" id="cs-ready-summary">
                        Configure your Agent (InputDriver / SyntheticDriver) in Car Studio and press Ready. When all players are Ready, SessionSpace transitions to ProvingGroundVehicle.levelspace.
                    </div>
                </div>
                <div class="cs-bottom-buttons">
                    <button type="button" class="cs-btn-ready" id="cs-btn-local-ready">
                        PRESS READY
                    </button>
                    <button type="button" class="cs-btn-launch" id="cs-btn-force-launch" title="Immediately trigger LevelTransition to ProvingGroundVehicle.levelspace">
                        Launch ProvingGroundVehicle.levelspace →
                    </button>
                </div>
            </div>

            <!-- PROVINGGROUNDVEHICLE.LEVELSPACE DRIVING & PROCEDURE HUD -->
            <div class="cs-match-hud" id="cs-match-hud" style="display:none">
                <div class="cs-match-banner">
                    <div>
                        <div class="cs-kicker" id="cs-match-proc-kicker">PROVINGGROUNDVEHICLE.LEVELSPACE · RACEPROCEDURE</div>
                        <div class="cs-match-gate" id="cs-match-gate-label">Next Gate: Start Gantry</div>
                    </div>
                    <div class="cs-match-metrics">
                        <div class="cs-metric-box">
                            <span class="cs-metric-lbl">LAP</span>
                            <span class="cs-metric-val" id="cs-match-lap">1 / 3</span>
                        </div>
                        <div class="cs-metric-box">
                            <span class="cs-metric-lbl" id="cs-match-timer-lbl">TIME</span>
                            <span class="cs-metric-val" id="cs-match-timer">00:00.0</span>
                        </div>
                        <div class="cs-metric-box">
                            <span class="cs-metric-lbl">SPEED</span>
                            <span class="cs-metric-val" id="cs-match-speed">0 km/h</span>
                        </div>
                    </div>
                </div>
                <div class="cs-match-standings" id="cs-match-standings"></div>
                <div class="cs-input-packet-bar" id="cs-input-packet-bar">
                    <span>GLFW InputEncoder → InputPacket #0</span>
                    <span>W/S Throttle/Brake · A/D Steer · Space Handbrake · C Camera · R Reset</span>
                </div>
            </div>
        `;
        Viewport.appendChild(Overlay);

        this.BindInterfaceEvents();
        this.RefreshInterface();
    }

    BindInterfaceEvents()
    {
        // Reference Photo Presets (Ref 1 vs Ref 2)
        document.querySelectorAll("[data-ref-preset]").forEach((Btn) =>
        {
            Btn.addEventListener("click", () =>
            {
                const Key = Btn.getAttribute("data-ref-preset");
                this.ApplyReferencePreset(Key, true);
            });
        });

        // Studio Environment Segment
        document.querySelectorAll("[data-studio-env]").forEach((Btn) =>
        {
            Btn.addEventListener("click", () =>
            {
                this.StudioPresetId = Btn.getAttribute("data-studio-env");
                this.SyncSpecToRenderer(true);
                this.RefreshInterface();
            });
        });

        // Paint Swatches
        const PaintGrid = document.getElementById("cs-paint-swatches");
        if (PaintGrid)
        {
            PaintGrid.innerHTML = PaintColours.map((Paint, Index) => `
                <button type="button" class="cs-swatch" data-paint-index="${Index}" title="${Paint.Name} (${Paint.Tag})">
                    <span class="cs-swatch-fill" style="background:${Paint.Css}"></span>
                    <span class="cs-swatch-name">${Paint.Name}</span>
                </button>
            `).join("");
            PaintGrid.querySelectorAll("[data-paint-index]").forEach((Btn) =>
            {
                Btn.addEventListener("click", () =>
                {
                    this.LocalProfile.Spec.PaintIndex = Number(Btn.getAttribute("data-paint-index"));
                    this.LocalProfile.Spec.CustomHex  = null;
                    this.SyncSpecToRenderer(false);
                    this.RefreshInterface();
                });
            });
        }

        // Custom Hex Color Picker
        const CustomColor = document.getElementById("cs-custom-color");
        CustomColor?.addEventListener("input", () =>
        {
            this.LocalProfile.Spec.CustomHex = CustomColor.value;
            this.SyncSpecToRenderer(false);
            this.RefreshInterface();
        });

        // Finish Presets
        const FinishSeg = document.getElementById("cs-finish-seg");
        if (FinishSeg)
        {
            FinishSeg.innerHTML = FinishPresets.map((F) => `
                <button type="button" class="cs-seg-btn" data-finish-id="${F.Id}">${F.Name}</button>
            `).join("");
            FinishSeg.querySelectorAll("[data-finish-id]").forEach((Btn) =>
            {
                Btn.addEventListener("click", () =>
                {
                    this.LocalProfile.Spec.FinishId = Btn.getAttribute("data-finish-id");
                    this.SyncSpecToRenderer(false);
                    this.RefreshInterface();
                });
            });
        }

        // Rim Presets
        const RimContainer = document.getElementById("cs-rim-chips");
        if (RimContainer)
        {
            RimContainer.innerHTML = RimPresets.map((R) => `
                <button type="button" class="cs-chip" data-rim-id="${R.Id}">
                    <span class="cs-dot" style="background:${R.Css}"></span>
                    <span>${R.Name}</span>
                </button>
            `).join("");
            RimContainer.querySelectorAll("[data-rim-id]").forEach((Btn) =>
            {
                Btn.addEventListener("click", () =>
                {
                    this.LocalProfile.Spec.RimId = Btn.getAttribute("data-rim-id");
                    this.SyncSpecToRenderer(false);
                    this.RefreshInterface();
                });
            });
        }

        // Brake Caliper Presets
        const CaliperContainer = document.getElementById("cs-caliper-chips");
        if (CaliperContainer)
        {
            CaliperContainer.innerHTML = CaliperPresets.map((C) => `
                <button type="button" class="cs-chip" data-caliper-id="${C.Id}">
                    <span class="cs-dot" style="background:${C.Css}"></span>
                    <span>${C.Name}</span>
                </button>
            `).join("");
            CaliperContainer.querySelectorAll("[data-caliper-id]").forEach((Btn) =>
            {
                Btn.addEventListener("click", () =>
                {
                    this.LocalProfile.Spec.CaliperId = Btn.getAttribute("data-caliper-id");
                    this.SyncSpecToRenderer(false);
                    this.RefreshInterface();
                });
            });
        }

        // Headlight Optics Presets
        const OpticsSeg = document.getElementById("cs-optics-seg");
        if (OpticsSeg)
        {
            OpticsSeg.innerHTML = HeadlightOpticsPresets.map((O) => `
                <button type="button" class="cs-seg-btn" data-optics-id="${O.Id}">
                    <span class="cs-dot" style="background:${O.Css}"></span>
                    ${O.Name}
                </button>
            `).join("");
            OpticsSeg.querySelectorAll("[data-optics-id]").forEach((Btn) =>
            {
                Btn.addEventListener("click", () =>
                {
                    this.LocalProfile.Spec.OpticsId = Btn.getAttribute("data-optics-id");
                    this.SyncSpecToRenderer(true);
                    this.RefreshInterface();
                });
            });
        }

        // Roof Fixture
        document.querySelectorAll("[data-roof]").forEach((Btn) =>
        {
            Btn.addEventListener("click", () =>
            {
                this.LocalProfile.Spec.RoofFixture = Number(Btn.getAttribute("data-roof"));
                this.SyncSpecToRenderer(false);
                this.RefreshInterface();
            });
        });

        // Tyre Pressures & Smoked Glass
        const FrontKpa = document.getElementById("cs-front-kpa");
        const RearKpa  = document.getElementById("cs-rear-kpa");
        const UpdatePressure = () =>
        {
            this.LocalProfile.Spec.FrontKpa = Number(FrontKpa?.value || 220);
            this.LocalProfile.Spec.RearKpa  = Number(RearKpa?.value  || 220);
            this.SyncSpecToRenderer(false);
            this.RefreshInterface();
        };
        FrontKpa?.addEventListener("input", UpdatePressure);
        RearKpa?.addEventListener("input", UpdatePressure);

        const WindowSmoke = document.getElementById("cs-window-smoke");
        WindowSmoke?.addEventListener("change", () =>
        {
            this.LocalProfile.Spec.WindowSmoke = WindowSmoke.checked;
            this.SyncSpecToRenderer(false);
            this.RefreshInterface();
        });

        // Camera Presets & Turntable
        const CamGrid = document.getElementById("cs-cam-grid");
        if (CamGrid)
        {
            CamGrid.innerHTML = StudioCameraPresets.map((C) => `
                <button type="button" class="cs-cam-btn" data-cam-id="${C.Id}">${C.Name}</button>
            `).join("");
            CamGrid.querySelectorAll("[data-cam-id]").forEach((Btn) =>
            {
                Btn.addEventListener("click", () => this.SelectCameraPreset(Btn.getAttribute("data-cam-id")));
            });
        }

        const SpinToggle = document.getElementById("cs-turntable-toggle");
        SpinToggle?.addEventListener("change", () =>
        {
            this.TurntableSpin = SpinToggle.checked;
        });

        const YawSlider = document.getElementById("cs-turntable-yaw");
        YawSlider?.addEventListener("input", () =>
        {
            this.TurntableAngle = Number(YawSlider.value);
            if (this.Host?.Vehicle && this.IsInLobby())
            {
                const [Sx, Sy] = StudioConstants.Centre;
                this.Host.Vehicle.Respawn([Sx, Sy, 0.63], this.TurntableAngle);
                this.Host.AfterVehicleMoved();
            }
            if (this.FallbackRenderer)
                this.FallbackRenderer.CarYaw = this.TurntableAngle;
        });

        // Session Roster & Procedure Controls
        document.getElementById("cs-btn-add-peer")?.addEventListener("click", () => this.SimulatePeerJoin(false));
        document.getElementById("cs-btn-ready-peers")?.addEventListener("click", () =>
        {
            const Peers = this.PlayerStore.Profiles.filter((P) => !P.IsLocalHost);
            const AnyNotReady = Peers.some((P) => !P.Ready);
            Peers.forEach((P) => { P.Ready = AnyNotReady; });
            this.EvaluateLobbyReadiness();
            this.RefreshInterface();
        });

        document.querySelectorAll("[data-proc]").forEach((Btn) =>
        {
            Btn.addEventListener("click", () =>
            {
                this.TargetProcedure = Btn.getAttribute("data-proc");
                this.RefreshInterface();
            });
        });

        // Ready Up & Force Launch
        document.getElementById("cs-btn-local-ready")?.addEventListener("click", () =>
        {
            this.TogglePlayerReady(this.LocalProfile.Token.Value);
        });
        document.getElementById("cs-btn-force-launch")?.addEventListener("click", () =>
        {
            this.ExecuteTransitionToMatch();
        });
        document.getElementById("cs-btn-return-lobby")?.addEventListener("click", () =>
        {
            this.ExecuteTransitionToLobby();
        });
        document.getElementById("cs-btn-toggle-editor")?.addEventListener("click", () =>
        {
            document.body.classList.toggle("panels-hidden");
            this.Host?.Resize?.();
        });
    }

    RefreshInterface()
    {
        const InLobby = this.IsInLobby();
        document.body.classList.toggle("studio-lobby-active", InLobby);
        document.body.classList.toggle("match-level-active", !InLobby);

        const LevelLabel = document.getElementById("cs-level-label");
        if (LevelLabel)
        {
            LevelLabel.textContent = InLobby
                ? "Default.levelspace (LobbyPolicy) · Car Studio Configurator"
                : `ProvingGroundVehicle.levelspace · Proving Grounds (${this.TargetProcedure})`;
        }
        const EpochLabel = document.getElementById("cs-epoch-label");
        if (EpochLabel)
        {
            EpochLabel.textContent = `Epoch #${this.TransitionEpoch} · ${InLobby ? "LobbyPolicy · RespawnPolicy: Disabled · SpawnPolicy: StudioParked" : `${this.TargetProcedure} · SpawnPolicy: GridRelease`}`;
        }

        const ShowInLobby = (Id, Visible) =>
        {
            const El = document.getElementById(Id);
            if (El) El.style.display = Visible ? "" : "none";
        };
        ShowInLobby("cs-lobby-left",        InLobby);
        ShowInLobby("cs-lobby-right",       InLobby);
        ShowInLobby("cs-lobby-bottom",      InLobby);
        ShowInLobby("cs-studio-preset-bar", InLobby);
        ShowInLobby("cs-btn-return-lobby", !InLobby);
        ShowInLobby("cs-match-hud",        !InLobby);

        // Update Top Reference Preset pills
        document.querySelectorAll("[data-ref-preset]").forEach((Btn) =>
        {
            const Key = Btn.getAttribute("data-ref-preset");
            const Active = (Key === "ref1_cyclorama" && this.StudioPresetId === "cyclorama")
                        || (Key === "ref2_softbox"   && this.StudioPresetId === "softbox");
            Btn.classList.toggle("is-active", Active);
        });

        // Update Studio Env segment
        document.querySelectorAll("[data-studio-env]").forEach((Btn) =>
        {
            Btn.classList.toggle("is-selected", Btn.getAttribute("data-studio-env") === this.StudioPresetId);
        });

        // Update Configurator selections
        const Spec = this.LocalProfile.Spec;
        const PaintEntry = PaintColours[Spec.PaintIndex] || PaintColours[0];
        const PaintBadge = document.getElementById("cs-active-paint-name");
        if (PaintBadge)
            PaintBadge.textContent = Spec.CustomHex ? `Custom (${Spec.CustomHex})` : PaintEntry.Name;

        document.querySelectorAll("[data-paint-index]").forEach((Btn) =>
        {
            Btn.classList.toggle("is-selected", !Spec.CustomHex && Number(Btn.getAttribute("data-paint-index")) === Spec.PaintIndex);
        });
        document.querySelectorAll("[data-finish-id]").forEach((Btn) =>
        {
            Btn.classList.toggle("is-selected", Btn.getAttribute("data-finish-id") === Spec.FinishId);
        });
        document.querySelectorAll("[data-rim-id]").forEach((Btn) =>
        {
            Btn.classList.toggle("is-selected", Btn.getAttribute("data-rim-id") === Spec.RimId);
        });
        document.querySelectorAll("[data-caliper-id]").forEach((Btn) =>
        {
            Btn.classList.toggle("is-selected", Btn.getAttribute("data-caliper-id") === Spec.CaliperId);
        });
        document.querySelectorAll("[data-optics-id]").forEach((Btn) =>
        {
            Btn.classList.toggle("is-selected", Btn.getAttribute("data-optics-id") === Spec.OpticsId);
        });
        document.querySelectorAll("[data-roof]").forEach((Btn) =>
        {
            Btn.classList.toggle("is-selected", Number(Btn.getAttribute("data-roof")) === Spec.RoofFixture);
        });
        document.querySelectorAll("[data-cam-id]").forEach((Btn) =>
        {
            Btn.classList.toggle("is-selected", Btn.getAttribute("data-cam-id") === this.ActiveCameraPreset);
        });
        document.querySelectorAll("[data-proc]").forEach((Btn) =>
        {
            Btn.classList.toggle("is-selected", Btn.getAttribute("data-proc") === this.TargetProcedure);
        });

        const PressureReadout = document.getElementById("cs-pressure-readout");
        if (PressureReadout)
            PressureReadout.textContent = `${Spec.FrontKpa} / ${Spec.RearKpa} kPa`;

        // Render Session Roster (PlayerStore)
        const CountPill = document.getElementById("cs-player-count");
        if (CountPill)
            CountPill.textContent = `${this.PlayerStore.Profiles.length} / 4 Players`;

        const RosterEl = document.getElementById("cs-roster-list");
        if (RosterEl)
        {
            RosterEl.innerHTML = this.PlayerStore.Profiles.map((P) =>
            {
                const Swatch = P.Spec.CustomHex || (PaintColours[P.Spec.PaintIndex]?.Css ?? "#585f68");
                const PaintTitle = P.Spec.CustomHex ? "Custom Paint" : (PaintColours[P.Spec.PaintIndex]?.Name ?? "Slate Grey");
                const RimTitle   = RimPresets.find((R) => R.Id === P.Spec.RimId)?.Name ?? "Satin Black";
                return `
                    <div class="cs-player-row ${P.Ready ? "is-ready" : ""}">
                        <div class="cs-player-main">
                            <span class="cs-player-swatch" style="background:${Swatch}"></span>
                            <div>
                                <div class="cs-player-name">
                                    ${P.DisplayName}
                                    <span class="cs-token-tag">#${P.Token.Value}</span>
                                </div>
                                <div class="cs-player-spec">${PaintTitle} · ${RimTitle} · Unit #${P.ActiveUnitToken.Value}</div>
                            </div>
                        </div>
                        <div class="cs-player-controls">
                            <button type="button" class="cs-ready-chip ${P.Ready ? "is-on" : ""}" data-toggle-ready="${P.Token.Value}">
                                ${P.Ready ? "READY" : "NOT READY"}
                            </button>
                            ${!P.IsLocalHost ? `
                                <button type="button" class="cs-icon-mini" data-rand-peer="${P.Token.Value}" title="Randomize Peer Spec">↻</button>
                                <button type="button" class="cs-icon-mini cs-leave" data-remove-peer="${P.Token.Value}" title="Player Leave">✕</button>
                            ` : ""}
                        </div>
                    </div>
                `;
            }).join("");

            RosterEl.querySelectorAll("[data-toggle-ready]").forEach((Btn) =>
            {
                Btn.addEventListener("click", () => this.TogglePlayerReady(Number(Btn.getAttribute("data-toggle-ready"))));
            });
            RosterEl.querySelectorAll("[data-rand-peer]").forEach((Btn) =>
            {
                Btn.addEventListener("click", () => this.RandomizePeerSpec(Number(Btn.getAttribute("data-rand-peer"))));
            });
            RosterEl.querySelectorAll("[data-remove-peer]").forEach((Btn) =>
            {
                Btn.addEventListener("click", () => this.SimulatePeerLeave(Number(Btn.getAttribute("data-remove-peer"))));
            });
        }

        // Update Bottom Ready button & status
        const LocalReadyBtn = document.getElementById("cs-btn-local-ready");
        if (LocalReadyBtn)
        {
            LocalReadyBtn.classList.toggle("is-ready", this.LocalProfile.Ready);
            LocalReadyBtn.textContent = this.LocalProfile.Ready ? "✓ READY (CLICK TO CANCEL)" : "PRESS READY";
        }
        this.UpdateTransitionBanner();
    }

    UpdateTransitionBanner()
    {
        const Summary = document.getElementById("cs-ready-summary");
        if (!Summary)
            return;
        if (this.TransitionActive)
        {
            Summary.innerHTML = `<strong style="color:#3ec46d">All players READY!</strong> Executing <code>LevelTransition</code> → <code>ProvingGroundVehicle.levelspace</code> (${this.TargetProcedure}) in <strong>${Math.max(0, this.TransitionCountdown).toFixed(1)}s</strong>…`;
        }
        else
        {
            const ReadyCount = this.PlayerStore.Profiles.filter((P) => P.Ready).length;
            Summary.innerHTML = `Ready status: <strong>${ReadyCount} / ${this.PlayerStore.Profiles.length}</strong> players ready · Target: <code>ProvingGroundVehicle.levelspace</code> (${this.TargetProcedure})`;
        }
    }

    UpdateMatchHud(CircuitGates)
    {
        const Kicker = document.getElementById("cs-match-proc-kicker");
        const GateLbl = document.getElementById("cs-match-gate-label");
        const LapEl   = document.getElementById("cs-match-lap");
        const TimeLbl = document.getElementById("cs-match-timer-lbl");
        const TimeEl  = document.getElementById("cs-match-timer");
        const SpeedEl = document.getElementById("cs-match-speed");
        const StandEl = document.getElementById("cs-match-standings");
        const PktBar  = document.getElementById("cs-input-packet-bar");

        if (Kicker)
            Kicker.textContent = `PROVINGGROUNDVEHICLE.LEVELSPACE · ${this.TargetProcedure.toUpperCase()} · EPOCH #${this.TransitionEpoch}`;

        const Gate = CircuitGates[this.LocalProfile.GateIndex % CircuitGates.length];
        if (GateLbl)
        {
            if (this.MatchFinished)
                GateLbl.textContent = `Session Complete · Winner: ${this.MatchWinner}`;
            else if (this.LocalProfile.Eliminated)
                GateLbl.textContent = `Agent Eliminated — Spectating Session`;
            else
                GateLbl.textContent = `Checkpoint ${this.LocalProfile.GateIndex + 1}/${CircuitGates.length}: ${Gate.Label} (${this.LocalProfile.DistanceToGate.toFixed(0)} m)`;
        }

        if (LapEl)
            LapEl.textContent = `${this.LocalProfile.Lap} / ${this.MatchTotalLaps}`;

        if (this.TargetProcedure === "EliminationPolicy")
        {
            if (TimeLbl) TimeLbl.textContent = "ELIMINATION IN";
            if (TimeEl)  TimeEl.textContent  = `${Math.max(0, this.EliminationTimer).toFixed(1)}s`;
        }
        else
        {
            if (TimeLbl) TimeLbl.textContent = "RACE TIME";
            if (TimeEl)  TimeEl.textContent  = `${this.MatchElapsed.toFixed(1)}s`;
        }

        if (SpeedEl)
            SpeedEl.textContent = `${Math.round(this.LocalUnit.SpeedKmh)} km/h`;

        if (StandEl)
        {
            const Sorted = [...this.PlayerStore.Profiles].sort((A, B) =>
            {
                if (A.Eliminated !== B.Eliminated) return A.Eliminated ? 1 : -1;
                return (B.Lap * 100 + B.GateIndex * 10 - B.DistanceToGate * 0.1) -
                       (A.Lap * 100 + A.GateIndex * 10 - A.DistanceToGate * 0.1);
            });
            StandEl.innerHTML = Sorted.map((P, Idx) =>
            {
                const Swatch = P.Spec.CustomHex || (PaintColours[P.Spec.PaintIndex]?.Css ?? "#585f68");
                return `
                    <div class="cs-standing-chip ${P.Eliminated ? "is-eliminated" : ""}">
                        <span class="cs-pos">P${Idx + 1}</span>
                        <span class="cs-dot" style="background:${Swatch}"></span>
                        <span>${P.DisplayName}</span>
                        <span class="cs-mono">${P.Eliminated ? "OUT" : `L${P.Lap}·G${P.GateIndex + 1}`}</span>
                    </div>
                `;
            }).join("");
        }

        if (PktBar)
        {
            const Pkt = this.InputEncoder.LatestPacket;
            PktBar.innerHTML = `
                <span><strong>InputEncoder → InputPacket #${Pkt.SequenceIndex}</strong> · Thr ${Pkt.ThrottleAxis.toFixed(2)} · Brk ${Pkt.BrakeAxis.toFixed(2)} · Str ${Pkt.SteerAxis.toFixed(2)} · HBrk ${Pkt.HandbrakeAxis.toFixed(0)}</span>
                <span>Agent #${this.LocalUnit.UnitToken.Value} (Owner PlayerToken #${this.LocalProfile.Token.Value}) · WASD / Arrows to Drive</span>
            `;
        }
    }
}
