/*==========================================================================================================================================
                                                                  AGENT.H
==========================================================================================================================================*/
// 🧩 Controllable scene Agent (vehicle, character, spectator camera rig) bound to a WorldSpace RecordToken and driven by either
//    an InputDriver (human-controlled) or a SyntheticDriver (autonomous AI).

#pragma once

#include "Frontier/Engine/Input/InputPacket.h"
#include "Frontier/Engine/Scene/RecordEntry.h"
#include "Frontier/Engine/Session/SpawnPolicy.h"

#include <array>
#include <cstdint>
#include <string>

namespace Frontier
{

class WorldSpace;

//------------------------------------------------------------------------------------------------------------------------
//                                                    AGENT CLASSIFICATION
//------------------------------------------------------------------------------------------------------------------------

enum class AgentCategory : uint8_t
{
    Vehicle   = 0,
    Character = 1,
    Spectator = 2
};

enum class AgentDriverKind : uint8_t
{
    Unassigned      = 0,
    InputDriven     = 1, // Driven by InputDriver (human player)
    SyntheticDriven = 2  // Driven by SyntheticDriver (AI)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                CAR STUDIO APPEARANCE & STANCE
//------------------------------------------------------------------------------------------------------------------------

enum class StudioBackdropStyle : uint8_t
{
    WhiteCyclorama = 0, // Reference Image 1: Curved white infinity cyclorama + mirror reflection turntable
    SoftboxLoft    = 1  // Reference Image 2: Dark industrial truss ceiling + overhead rectangular softbox + white stage pad
};

struct VehicleUnitConfiguration
{
    std::string         PaintName               = "Obsidian Black";
    std::array<float,3> PaintAlbedo             = { 0.03f, 0.032f, 0.036f };
    float               PaintRoughness          = 0.08f;
    float               PaintMetallic           = 0.88f;
    float               ClearCoatWeight         = 1.0f;
    float               ClearCoatRoughness      = 0.04f;

    std::string         RimFinishName           = "Satin Gunmetal";
    std::array<float,3> RimAlbedo               = { 0.22f, 0.23f, 0.25f };
    float               RimRoughness            = 0.24f;
    float               RimMetallic             = 0.92f;

    std::string         CaliperName             = "Acid Green";
    std::array<float,3> CaliperAlbedo           = { 0.58f, 0.84f, 0.08f };

    std::string         HeadlightOpticsName     = "Xenon Ice White";
    std::array<float,3> HeadlightTint           = { 0.92f, 0.96f, 1.00f };
    float               HeadlightLuminance      = 32.0f;

    StudioBackdropStyle Backdrop                = StudioBackdropStyle::WhiteCyclorama;
    float               RideHeightOffsetMetres  = 0.0f;
    float               CamberFrontDegrees      = -1.8f;
    float               CamberRearDegrees       = -2.2f;
    float               FrontPressureKpa        = 220.0f;
    float               RearPressureKpa         = 240.0f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                            AGENT
//------------------------------------------------------------------------------------------------------------------------

class Agent
{
  public:
    Agent() = default;
    Agent(RecordToken BoundRecordToken,
          uint64_t    OwnerPlayerTokenValue,
          AgentCategory InitialCategory = AgentCategory::Vehicle);

    [[nodiscard]] RecordToken                     QueryRecordToken() const noexcept { return BoundRecord; }
    [[nodiscard]] uint64_t                        QueryOwnerPlayerToken() const noexcept { return OwnerPlayerToken; }
    [[nodiscard]] AgentCategory                   QueryCategory() const noexcept { return Category; }
    [[nodiscard]] AgentDriverKind                 QueryDriverKind() const noexcept { return DriverKind; }
    [[nodiscard]] const InputPacket&              QueryLatestInput() const noexcept { return LatestInput; }
    [[nodiscard]] const VehicleUnitConfiguration& QueryVehicleConfiguration() const noexcept { return VehicleSpec; }
    [[nodiscard]] VehicleUnitConfiguration&       AccessVehicleConfiguration() noexcept { return VehicleSpec; }

    [[nodiscard]] const std::array<float, 3>&     QueryPositionMetres() const noexcept { return PositionMetres; }
    [[nodiscard]] const std::array<float, 3>&     QuerySpawnPositionMetres() const noexcept { return SpawnPositionMetres; }
    [[nodiscard]] float                           QueryYawDegrees() const noexcept { return YawDegrees; }
    [[nodiscard]] float                           QuerySpawnYawDegrees() const noexcept { return SpawnYawDegrees; }
    [[nodiscard]] float                           QueryPitchDegrees() const noexcept { return PitchDegrees; }
    [[nodiscard]] float                           QueryRollDegrees() const noexcept { return RollDegrees; }
    [[nodiscard]] float                           QueryForwardSpeedMps() const noexcept { return ForwardSpeedMps; }
    [[nodiscard]] const SpawnPose&                QuerySpawnStand() const noexcept { return SpawnStand; }
    [[nodiscard]] const SpawnPolicy&              QuerySpawnPolicy() const noexcept { return SpawnRules; }
    [[nodiscard]] float                           QuerySecondsSinceSpawn() const noexcept { return SecondsSinceSpawn; }
    [[nodiscard]] bool                            IsHandbrakeLatched() const noexcept { return HandbrakeLatched; }

    // 📝 The session's spawn rules decide this Agent's stance, its brake rules, and how long they outrank whoever drives it.
    void AssignSpawnPolicy(const SpawnPolicy& UpdatedRules) noexcept { SpawnRules = UpdatedRules; }
    // 📝 Stands the Agent where the footprint survey resolved: position, heading, and the surface's own pitch and roll.
    void ApplySpawnPose(const SpawnPose& Pose, WorldSpace* World = nullptr) noexcept;
    void ResetSpawnClock() noexcept;
    void AdvanceSpawnClock(float StepSeconds) noexcept;

    // 📝 Blends the driver's pedal demand with the Agent's spawn brake rules — brakes disabled at spawn ignore the driver for a while,
    //    and a latched handbrake holds a parked studio car until the driver actually asks for drive.
    void ResolveEffectivePedals(const InputPacket& Packet, float& OutBrake, float& OutHandbrake) const noexcept;

    void BindRecord(RecordToken Token) noexcept { BoundRecord = Token; }
    void AssignOwnerPlayerToken(uint64_t PlayerTokenValue) noexcept { OwnerPlayerToken = PlayerTokenValue; }
    void AssignDriverKind(AgentDriverKind Kind) noexcept { DriverKind = Kind; }
    void AssignVehicleConfiguration(const VehicleUnitConfiguration& Spec) { VehicleSpec = Spec; }

    // Applies one normalized InputPacket (from either InputDriver or SyntheticDriver) to this Agent and writes to WorldSpace.
    void ApplyInputPacket(const InputPacket& Packet, float StepSeconds, WorldSpace* World = nullptr) noexcept;

    // Places the Agent in world space and optionally marks this pose as its spawn anchor for RespawnPolicy.
    void PlaceInWorld(const std::array<float, 3>& UpdatedPositionMetres,
                      float                       UpdatedYawDegrees,
                      WorldSpace*                 World = nullptr,
                      bool                        RecordAsSpawnAnchor = false) noexcept;

    bool SynchronizeWithWorld(WorldSpace& World) const noexcept;

  private:
    RecordToken              BoundRecord{};
    uint64_t                 OwnerPlayerToken = 0;
    AgentCategory            Category         = AgentCategory::Vehicle;
    AgentDriverKind          DriverKind       = AgentDriverKind::Unassigned;
    InputPacket              LatestInput{};
    VehicleUnitConfiguration VehicleSpec{};

    std::array<float, 3>     PositionMetres      = { -40.0f, 0.0f, 0.0f };
    std::array<float, 3>     SpawnPositionMetres = { -40.0f, 0.0f, 0.0f };
    float                    YawDegrees          = 0.0f;
    float                    SpawnYawDegrees     = 0.0f;
    float                    PitchDegrees        = 0.0f;
    float                    RollDegrees         = 0.0f;
    float                    ForwardSpeedMps     = 0.0f;

    SpawnPolicy              SpawnRules{};                 // [-] spawn stance, brake rules, and driven wheels
    SpawnPose                SpawnStand{};                 // [-] the surface stand the survey resolved
    float                    SecondsSinceSpawn = 0.0f;     // [s] spawn brake rules outrank the driver for this long
    bool                     HandbrakeLatched  = false;    // [-] a parked car stays parked until the driver asks for drive
};

using DeployableUnit = Agent;
using UnitCategory   = AgentCategory;

} // namespace Frontier
