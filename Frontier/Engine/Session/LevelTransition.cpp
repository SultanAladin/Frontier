/*==========================================================================================================================================
                                                            LEVELTRANSITION.CPP
==========================================================================================================================================*/
// 🧩 Seamless level transition execution preserving PlayerStore profiles and customized VehicleUnitConfigurations.

#include "Frontier/Engine/Session/LevelTransition.h"
#include "Frontier/Engine/Scene/WorldSpace.h"

#include <filesystem>

namespace Frontier
{

LevelTransition::LevelTransition(PolicyFactory InFactory)
    : Factory(std::move(InFactory))
{
}

bool LevelTransition::RequestTransition(std::string TargetLevelPath, std::string TargetPolicyName)
{
    if (TargetLevelPath.empty())
    {
        return false;
    }
    PendingTargetLevel  = std::move(TargetLevelPath);
    PendingTargetPolicy = std::move(TargetPolicyName);
    Pending             = true;
    return true;
}

bool LevelTransition::ExecuteTransition(SessionSpace&    Session,
                                        WorldSpace&      World,
                                        std::string_view ProjectRootDirectory)
{
    if (!Pending)
    {
        return false;
    }

    // 1. Capture TransferConfiguration before clearing the source WorldSpace
    LastTransfer.SourceLevelPath   = Session.QueryActiveLevelPath();
    LastTransfer.TargetLevelPath   = PendingTargetLevel;
    LastTransfer.TargetPolicyName  = PendingTargetPolicy;
    LastTransfer.TransitionEpoch   = Session.QueryTransitionEpoch() + 1;
    LastTransfer.PreservedProfiles = Session.QueryPlayerStore().QueryProfiles();

    // 2. Clear previous level Agents and WorldSpace records
    Session.ClearAgents();
    World.Clear();

    // 3. Load destination .levelspace into WorldSpace
    if (!ProjectRootDirectory.empty())
    {
        const std::filesystem::path Candidate = std::filesystem::path(ProjectRootDirectory) / LastTransfer.TargetLevelPath;
        if (std::filesystem::exists(Candidate))
        {
            (void)World.LoadLevelSpace(Candidate);
        }
    }
    else if (std::filesystem::exists(LastTransfer.TargetLevelPath))
    {
        (void)World.LoadLevelSpace(LastTransfer.TargetLevelPath);
    }

    // 4. Restore PlayerStore profiles and increment transition epoch
    Session.AccessPlayerStore().Clear();
    for (const PlayerProfile& Saved : LastTransfer.PreservedProfiles)
    {
        Session.AccessPlayerStore().RestoreProfile(Saved);
    }
    Session.ConfigureActiveLevelPath(LastTransfer.TargetLevelPath);
    Session.IncrementTransitionEpoch();
    Pending = false;

    // 5. Construct and begin the target SessionPolicy (which spawns Agents on the destination level)
    if (Factory)
    {
        std::unique_ptr<SessionPolicy> NextPolicy = Factory(LastTransfer.TargetPolicyName);
        if (NextPolicy != nullptr)
        {
            Session.BeginPolicy(std::move(NextPolicy), World);
        }
    }

    return true;
}

} // namespace Frontier
