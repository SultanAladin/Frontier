/*==========================================================================================================================================
                                                            VEHICLEINSPECTOR.H
==========================================================================================================================================*/
// 🧩 Native C++ VehicleInspector: full GRIT + 3-formulation soft-body tyre + supercharger + Jolt substepping inspector & telemetry dock.

#pragma once

#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

#include <array>
#include <string>
#include <vector>

namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                            CURVE & PERFORMANCE EVALUATION
//------------------------------------------------------------------------------------------------------------------------

struct VehiclePerformanceSummary
{
    float PeakTorqueNm         = 640.0f;   // [N*m]
    float PeakTorqueRpm        = 4500.0f;  // [rpm]
    float PeakPowerKw          = 371.0f;   // [kW]
    float PeakPowerRpm         = 6500.0f;  // [rpm]
    float PowerToWeightKwPerT  = 289.0f;   // [kW/t]
    float SpecificTorqueNmPerL = 213.0f;   // [N*m/L]
    float ZeroToHundredSec     = 3.18f;    // [s]
    float LaunchDistanceMetres = 52.4f;    // [m]
    int   ShiftsToHundred      = 2;        // [-]
    float TopSpeedKmh          = 312.0f;   // [km/h]
    float BrakingDistance100M  = 31.2f;    // [m]
    int   EnclosedTyreCount    = 4;        // [-]
};

[[nodiscard]] float EvaluateMonotoneVehicleCurve(const std::vector<VehicleCurveControlPoint>& Points,
                                                 bool                                         SmoothMonotone,
                                                 float                                        QueryX) noexcept;

[[nodiscard]] VehiclePerformanceSummary EvaluateVehiclePerformance(const VehicleConfiguration& Configuration) noexcept;

#if FRONTIER_EDITOR

//------------------------------------------------------------------------------------------------------------------------
//                                                    VEHICLE INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

class VehicleInspector final : public RecordInspector
{
  public:
    static constexpr int SectionCount = 10;

    explicit VehicleInspector(VehicleConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "VEHICLE · CHASSIS & POWERTRAIN"; }
    [[nodiscard]] bool        QueryOccupiesColumn() const noexcept override { return true; }
    [[nodiscard]] int         QuerySectionCount() const noexcept override { return SectionCount; }
    void                      ConfigureSection(int SectionIndex) noexcept override;
    [[nodiscard]] std::string QueryDirectoryReadout() const override;

    void ConstructInspector() override;

  private:
    void ConstructHeroAndChips(ControlColumn& Page, const VehiclePerformanceSummary& Summary, bool& Changed);
    void ConstructSectionTabs(ControlColumn& Page);
    void ConstructChassisSection(ControlColumn& Page, bool& Changed);
    void ConstructEngineSection(ControlColumn& Page, const VehiclePerformanceSummary& Summary, bool& Changed);
    void ConstructSuspensionSection(ControlColumn& Page, bool& Changed);
    void ConstructSoftBodySection(ControlColumn& Page, bool& Changed);
    void ConstructCurvesSection(ControlColumn& Page, bool& Changed);
    void ConstructThermalSection(ControlColumn& Page, bool& Changed);
    void ConstructTyresSection(ControlColumn& Page, bool& Changed);
    void ConstructAeroSection(ControlColumn& Page, const VehiclePerformanceSummary& Summary, bool& Changed);
    void ConstructSimulationSection(ControlColumn& Page, bool& Changed);
    void ConstructPerformanceSection(ControlColumn& Page, const VehiclePerformanceSummary& Summary);
    void ConstructTelemetryDock(ControlColumn& Page);

    VehicleConfiguration&      Parameters;
    int                        ActiveSection        = 0;
    int                        PendingScrollSection = -1;
    std::array<float, 10>      SectionOffsets       {};
    float                      TelemetryElapsedSec  = 18.4f;
    bool                       TelemetryPaused      = false;
};

#endif

} // namespace Frontier
