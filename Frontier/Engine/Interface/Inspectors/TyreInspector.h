/*==========================================================================================================================================
                                                             TYREINSPECTOR.H
==========================================================================================================================================*/
// 🧩 TyreConfiguration: one tyre of a vehicle, the prototype's tyre card: the corner it is fitted to, its compound and window,
//    the cold pressure, whether it is fitted or in the pit lane, and its live temperatures, pressure, load, slip and wear.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

namespace Frontier
{

#if FRONTIER_EDITOR

class TyreInspector final : public RecordInspector
{
  public:
    explicit TyreInspector(TyreConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char*        QueryEyebrowText() const noexcept override { return "VEHICLE · TYRE"; }
    [[nodiscard]] const char*        QuerySectionNote() const noexcept override { return "Fitted set"; }
    [[nodiscard]] bool               QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool               QueryToggleEnabled() const noexcept override { return Parameters.Enclosed; }
    void                             ConfigureToggle(bool Enabled) override { Parameters.Enclosed = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t           QueryRevision() const noexcept override { return Parameters.Revision; }
    [[nodiscard]] bool               QueryOccupiesColumn() const noexcept override { return true; }
    [[nodiscard]] std::string        QueryDirectoryReadout() const override;
    void                             ConstructInspector() override;

    [[nodiscard]] TyreConfiguration&       AccessConfiguration() noexcept { return Parameters; }
    [[nodiscard]] const TyreConfiguration& QueryConfiguration() const noexcept { return Parameters; }

  private:
    TyreConfiguration& Parameters;
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
