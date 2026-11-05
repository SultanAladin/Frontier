/*==========================================================================================================================================
                                                            VEHICLEINSPECTOR.H
==========================================================================================================================================*/
// 🧩 VehicleConfiguration: the vehicle inspector (SultanAladin/Frontier arena/ca09b37f-frontier, Experimental/VehicleInspector), in
//    C++ and bound to the live specification: overview (dyno sheet, performance estimates, carcass, induction, aids, physics-thread
//    stepping), chassis, engine and induction with interactive curve authoring for all eight engine/turbo/supercharger curves,
//    driveline, tyres (four-corner pit bay, carcass and Pacejka curve previews, soft-body lattice/FEM parameters, Magic Formula),
//    brakes and aids, aero, fuel and fluids, and the read-only live telemetry dock while simulating.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

#include <array>
#include <cstdint>

namespace Frontier
{

#if FRONTIER_EDITOR

class ControlColumn;

class VehicleInspector final : public RecordInspector
{
  public:
    static constexpr int    SectionCount        = 9;
    static constexpr size_t TelemetryTraceCount = 240;

    explicit VehicleInspector(VehicleConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "VEHICLE · PROJECT DRIVE ON JOLT"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "Specification"; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Physics; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Physics = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    [[nodiscard]] bool        QueryOccupiesColumn() const noexcept override { return true; }
    [[nodiscard]] int         QuerySectionCount() const noexcept override { return SectionCount; }
    void                      ConfigureSection(int Section) noexcept override { ActiveSection = Section; }
    [[nodiscard]] int         QueryActiveSection() const noexcept { return ActiveSection; }
    [[nodiscard]] std::string QueryDirectoryReadout() const override;
    void                      ConstructInspector() override;

  private:
    bool ConstructOverview(ControlColumn& Page);
    bool ConstructCurveEditor(ControlColumn& Page);
    bool ConstructTyreBay(ControlColumn& Page);
    void ConstructTyreMechanicsPreview(ControlColumn& Page);
    bool ConstructFigures(ControlColumn& Page, const char* const* Sections, int Count);
    bool ConstructRatios(ControlColumn& Page);
    void ConstructTelemetry(ControlColumn& Page);
    void ConstructCurveChart(ControlColumn& Page, const char* Title, const Vehicle::Curve& Torque, float RedlineRpm);

    VehicleConfiguration&                  Parameters;
    int                                    ActiveSection      = 0;
    bool                                   FormulaOpen        = false;
    int                                    SelectedCurveIndex = 0;
    int                                    SelectedCurvePoint = 0;
    int                                    DraggedCurvePoint  = -1;
    uint64_t                               LastTraceSteps     = 0;
    size_t                                 TraceCount         = 0;
    size_t                                 TraceCursor        = 0;
    std::array<float, TelemetryTraceCount> SpeedTraceKmh{};
    std::array<float, TelemetryTraceCount> RpmTrace{};
    std::array<float, TelemetryTraceCount> BoostTraceBar{};
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
