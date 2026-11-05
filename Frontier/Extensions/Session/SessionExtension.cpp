/*==========================================================================================================================================
                                                           SESSIONEXTENSION.CPP
==========================================================================================================================================*/
// 🧩 SessionExtension frame orchestration across InputEncoder, SessionSpace, LevelTransition, PhysicsExtension, and NetworkRecordIndex.

#include "Frontier/Extensions/Session/SessionExtension.h"
#include "Frontier/Engine/Scene/WorldSpace.h"
#include "Frontier/Extensions/Physics/PhysicsExtension.h"

namespace Frontier
{

SessionExtension::SessionExtension(SessionSpace& InSession, WorldSpace& InWorld)
    : Session(InSession)
    , World(InWorld)
{
}

void SessionExtension::ConfigurePolicyFactory(LevelTransition::PolicyFactory Factory)
{
    Transition.ConfigurePolicyFactory(std::move(Factory));
}

void SessionExtension::StepFrame(GLFWwindow* Window, float StepSeconds)
{
    const InputPacket Packet = LocalEncoder.SampleGlfwWindow(Window, StepSeconds);
    if (const PlayerProfile* LocalHost = Session.QueryPlayerStore().FindLocalHostProfile())
    {
        Session.SubmitPlayerInput(LocalHost->Token, Packet);
    }

    Session.StepSession(World, StepSeconds);

    for (const Agent& Entry : Session.QueryAgents())
    {
        (void)RecordIndex.AllocateAndBind(
            Entry.QueryRecordToken(),
            PlayerToken{ Entry.QueryOwnerPlayerToken() });
    }

    if (const SessionPolicy* Policy = Session.QueryPolicy())
    {
        std::string TargetLevel{};
        std::string TargetPolicy{};
        if (Policy->ShouldTransitionLevel(Session, TargetLevel, TargetPolicy))
        {
            if (Transition.RequestTransition(std::move(TargetLevel), std::move(TargetPolicy)))
            {
                RecordIndex.Clear();
                (void)Transition.ExecuteTransition(Session, World, ProjectRoot);
            }
        }
    }
}

void SessionExtension::StepWithInputState(const RawInputState& LocalHostInputState, float StepSeconds)
{
    const InputPacket Packet = LocalEncoder.EncodeState(LocalHostInputState, StepSeconds);
    if (const PlayerProfile* LocalHost = Session.QueryPlayerStore().FindLocalHostProfile())
    {
        Session.SubmitPlayerInput(LocalHost->Token, Packet);
    }

    Session.StepSession(World, StepSeconds);

    for (const Agent& Entry : Session.QueryAgents())
    {
        (void)RecordIndex.AllocateAndBind(
            Entry.QueryRecordToken(),
            PlayerToken{ Entry.QueryOwnerPlayerToken() });
    }

    if (const SessionPolicy* Policy = Session.QueryPolicy())
    {
        std::string TargetLevel{};
        std::string TargetPolicy{};
        if (Policy->ShouldTransitionLevel(Session, TargetLevel, TargetPolicy))
        {
            if (Transition.RequestTransition(std::move(TargetLevel), std::move(TargetPolicy)))
            {
                RecordIndex.Clear();
                (void)Transition.ExecuteTransition(Session, World, ProjectRoot);
            }
        }
    }
}

void SessionExtension::SynchronizePhysicsVehicle(PhysicsExtension& Physics, float StepSeconds)
{
    const PlayerProfile* LocalHost = Session.QueryPlayerStore().FindLocalHostProfile();
    if (LocalHost == nullptr)
    {
        return;
    }

    PhysicsVehicleInput VehicleInput{};
    VehicleInput.Throttle  = LocalHost->LatestInput.ThrottleAxis;
    VehicleInput.Brake     = LocalHost->LatestInput.BrakeAxis;
    VehicleInput.Steer     = -LocalHost->LatestInput.SteerAxis;
    VehicleInput.Handbrake = LocalHost->LatestInput.HandbrakeAxis >= 0.5f;

    // 📝 The Agent owns the spawn brake rules, so the pedals handed to the solver are the same ones it resolved — a car parked on a studio
    //    pad or released on a grid behaves identically whether the Agent integrates it or the physics does.
    if (const Agent* HostAgent = Session.FindAgentByOwner(LocalHost->Token))
    {
        float ResolvedPedal     = VehicleInput.Brake;
        float ResolvedHandbrake = VehicleInput.Handbrake ? 1.0f : 0.0f;
        HostAgent->ResolveEffectivePedals(LocalHost->LatestInput, ResolvedPedal, ResolvedHandbrake);
        VehicleInput.Brake     = ResolvedPedal;
        VehicleInput.Handbrake = ResolvedHandbrake >= 0.5f;
    }

    Physics.AssignVehicleInput(VehicleInput);
    if (LocalHost->LatestInput.HasAction(InputActionResetUnit))
    {
        if (Session.QueryPolicy() == nullptr || Session.QueryPolicy()->QueryRespawnPolicy().IsRespawnAllowed())
        {
            Physics.RequestVehicleReset();
        }
    }
    (void)Physics.Step(World, static_cast<double>(StepSeconds));
}

} // namespace Frontier
