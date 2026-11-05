/*==========================================================================================================================================
                                                            SESSIONEXTENSION.H
==========================================================================================================================================*/
// 🧩 SessionExtension bridges SessionSpace, LevelTransition, InputEncoder/InputDriver/SyntheticDriver, WorldSpace, PhysicsExtension,
//    and NetworkRecordIndex.

#pragma once

#include "Frontier/Engine/Input/InputEncoder.h"
#include "Frontier/Engine/Session/LevelTransition.h"
#include "Frontier/Engine/Session/SessionSpace.h"
#include "Frontier/Extensions/Networking/NetworkRecordIndex.h"

#include <string>

struct GLFWwindow;

namespace Frontier
{

class PhysicsExtension;
class WorldSpace;

class SessionExtension
{
  public:
    SessionExtension(SessionSpace& InSession, WorldSpace& InWorld);

    void ConfigureProjectRootDirectory(std::string RootPath) { ProjectRoot = std::move(RootPath); }
    void ConfigurePolicyFactory(LevelTransition::PolicyFactory Factory);

    [[nodiscard]] InputEncoder&             AccessInputEncoder() noexcept { return LocalEncoder; }
    [[nodiscard]] const InputEncoder&       QueryInputEncoder() const noexcept { return LocalEncoder; }
    [[nodiscard]] LevelTransition&          AccessTransition() noexcept { return Transition; }
    [[nodiscard]] const LevelTransition&    QueryTransition() const noexcept { return Transition; }
    [[nodiscard]] NetworkRecordIndex&       AccessRecordIndex() noexcept { return RecordIndex; }
    [[nodiscard]] const NetworkRecordIndex& QueryRecordIndex() const noexcept { return RecordIndex; }

    void StepFrame(GLFWwindow* Window, float StepSeconds);
    void StepWithInputState(const RawInputState& LocalHostInputState, float StepSeconds);
    void SynchronizePhysicsVehicle(PhysicsExtension& Physics, float StepSeconds);

  private:
    SessionSpace&      Session;
    WorldSpace&        World;
    InputEncoder       LocalEncoder{};
    LevelTransition    Transition{};
    NetworkRecordIndex RecordIndex{};
    std::string        ProjectRoot{};
};

} // namespace Frontier
