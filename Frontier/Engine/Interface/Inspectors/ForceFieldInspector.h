/*==========================================================================================================================================
                                                           FORCEFIELDINSPECTOR.H
==========================================================================================================================================*/
// 🧩 Atmospheric composition and world-vector inspection; Wind remains its own editable contributor.

#pragma once

#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

namespace Frontier
{

#if FRONTIER_EDITOR

class ForceFieldInspector final : public RecordInspector
{
  public:
    explicit ForceFieldInspector(ForceFieldConfiguration &Configuration) : Parameters(Configuration) {}
    const char *QueryEyebrowText() const noexcept override
    {
        return "FORCE FIELD · ATMOSPHERIC FLOW";
    }
    bool QueryOccupiesColumn() const noexcept override
    {
        return true;
    }
    bool QueryToggleSupported() const noexcept override
    {
        return true;
    }
    bool QueryToggleEnabled() const noexcept override
    {
        return Parameters.Enabled;
    }
    void ConfigureToggle(bool Enabled) override
    {
        Parameters.Enabled = Enabled;
        Parameters.AdvanceRevision();
    }
    uint64_t QueryRevision() const noexcept override
    {
        return Parameters.Revision;
    }
    const char *QueryDirectoryGlyph() const noexcept override
    {
        return "Wind";
    }
    void ConstructInspector() override;

    bool QueryViewTarget(InspectorViewTarget& View) const noexcept override
    {
        View.Position = {Parameters.CentreMetres[0],Parameters.CentreMetres[1],Parameters.CentreMetres[2]};
        View.Radius = std::max({Parameters.HalfExtentsMetres[0],Parameters.HalfExtentsMetres[1],Parameters.HalfExtentsMetres[2]});
        return true;
    }

  private:
    ForceFieldConfiguration &Parameters;
};

#endif

} // namespace Frontier
