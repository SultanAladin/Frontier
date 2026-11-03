/*==========================================================================================================================================
                                                              WINDINSPECTOR.H
==========================================================================================================================================*/
// 🧩 WindConfiguration: the shared weather force field.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

namespace Frontier
{

class ControlColumn;

#if FRONTIER_EDITOR

//------------------------------------------------------------------------------------------------------------------------
//                                                    WIND INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

class WindInspector final : public RecordInspector
{
  public:
    explicit WindInspector(WindConfiguration &InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char *QueryEyebrowText() const noexcept override
    {
        return "ENVIRONMENT · WIND FIELD";
    }
    [[nodiscard]] const char *QuerySectionNote() const noexcept override
    {
        return "3D curl-noise advection";
    }
    [[nodiscard]] bool QueryToggleSupported() const noexcept override
    {
        return true;
    }
    [[nodiscard]] bool QueryToggleEnabled() const noexcept override
    {
        return Parameters.Enabled;
    }
    void ConfigureToggle(bool Enabled) override
    {
        Parameters.Enabled = Enabled;
        Parameters.AdvanceRevision();
    }
    [[nodiscard]] uint64_t QueryRevision() const noexcept override
    {
        return Parameters.Revision;
    }
    void               EnumerateSwitches(std::vector<InspectorSwitchEntry> &OutSwitches) override;
    void               ConstructInspector() override;
    [[nodiscard]] bool QueryOccupiesColumn() const noexcept override
    {
        return true;
    }
    [[nodiscard]] int QuerySectionCount() const noexcept override
    {
        return 4;
    }
    void ConfigureSection(int Section) noexcept override
    {
        ActivePane = std::clamp(Section, 0, 3);
    }

  private:
    bool ConstructFlow(ControlColumn &Page);
    bool ConstructVariation(ControlColumn &Page);
    bool ConstructAltitude(ControlColumn &Page);
    void ConstructReceivers(ControlColumn &Page);

    WindConfiguration &Parameters;
    int                ActivePane = 0;
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
