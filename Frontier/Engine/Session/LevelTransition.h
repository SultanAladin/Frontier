/*==========================================================================================================================================
                                                             LEVELTRANSITION.H
==========================================================================================================================================*/
// 🧩 LevelTransition captures TransferConfiguration from SessionSpace, loads the destination .levelspace into WorldSpace,
//    restores PlayerStore profiles and VehicleUnitConfigurations, and activates the target SessionPolicy.

#pragma once

#include "Frontier/Engine/Session/SessionPolicy.h"
#include "Frontier/Engine/Session/SessionSpace.h"
#include "Frontier/Engine/Session/TransferConfiguration.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace Frontier
{

class WorldSpace;

class LevelTransition
{
  public:
    using PolicyFactory = std::function<std::unique_ptr<SessionPolicy>(std::string_view PolicyName)>;

    LevelTransition() = default;
    explicit LevelTransition(PolicyFactory InFactory);

    void ConfigurePolicyFactory(PolicyFactory InFactory) { Factory = std::move(InFactory); }

    bool RequestTransition(std::string TargetLevelPath, std::string TargetPolicyName);

    [[nodiscard]] bool                         HasPendingTransition() const noexcept { return Pending; }
    [[nodiscard]] const TransferConfiguration& QueryLastTransfer() const noexcept { return LastTransfer; }

    bool ExecuteTransition(SessionSpace&    Session,
                           WorldSpace&      World,
                           std::string_view ProjectRootDirectory = {});

  private:
    PolicyFactory         Factory{};
    bool                  Pending              = false;
    std::string           PendingTargetLevel{};
    std::string           PendingTargetPolicy{};
    TransferConfiguration LastTransfer{};
};

} // namespace Frontier
