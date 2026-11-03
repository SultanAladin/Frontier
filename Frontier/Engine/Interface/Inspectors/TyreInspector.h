/*==========================================================================================================================================
                                                             TYREINSPECTOR.H
==========================================================================================================================================*/
// 🧩 Native C++ TyreInspector: per-corner compound window, 3-node carcass/gas state, Pacejka MF6.1 factors, puncture, and pit enclosure.

#pragma once

#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

#include <string>

namespace Frontier
{

#if FRONTIER_EDITOR

class TyreInspector final : public RecordInspector
{
  public:
    explicit TyreInspector(TyreCornerConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "VEHICLE · CORNER TYRE"; }
    [[nodiscard]] bool        QueryOccupiesColumn() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.EnclosedCondition; }
    void                      ConfigureToggle(bool Enabled) override
    {
        Parameters.EnclosedCondition = Enabled;
        Parameters.AdvanceRevision();
    }
    [[nodiscard]] std::string QueryDirectoryReadout() const override;

    void ConstructInspector() override;

  private:
    TyreCornerConfiguration& Parameters;
};

#endif

} // namespace Frontier
