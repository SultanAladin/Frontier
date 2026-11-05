/*==========================================================================================================================================
                                                            TYREINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 The tyre card: corner, compound, cold pressure, fitted or in the pit lane, compound thermal window, and the corner's live
//    carcass, gas-law pressure, three-layer temperatures, slip, load and wear while the car runs.

#include "Engine/Interface/Inspectors/TyreInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/CaptionText.h"
#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/ControlTheme.h"
#include "Engine/Interface/Controls/FooterRow.h"
#include "Engine/Interface/Controls/MetricTiles.h"
#include "Engine/Interface/Controls/PageHeading.h"
#include "Engine/Interface/Controls/SectionRows.h"
#include "Engine/Interface/Controls/SelectRow.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Scene/SceneDirectory.h"
#include "Engine/Scene/VehicleReadings.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace Frontier
{

std::string TyreInspector::QueryDirectoryReadout() const
{
    static constexpr const char* Codes[] = { "FL", "FR", "RL", "RR" };
    char PressureText[24];
    std::snprintf(PressureText, sizeof(PressureText), " · %.0f kPa", Parameters.ColdPressure * 0.001f);
    return std::string(Codes[Parameters.Corner & 3u]) + " · " + Parameters.Compound + PressureText +
           (Parameters.Enclosed ? "" : " (pit)");
}

void TyreInspector::ConstructInspector()
{
    using namespace SectionRows;
    const auto    Theme = ControlTheme::ResolveNative();
    const auto    Faces = EditorTypefaces::Resolve();
    ControlColumn Page(Theme, Faces, InspectorRuntime::Instance().ResolveInterfaceScale());
    bool          Changed = false;

    static constexpr const char* Corners[] = { "Front left", "Front right", "Rear left", "Rear right" };
    const RecordToken            ThisToken = InspectorRuntime::Instance().QueryInspectedToken();
    const RecordToken            Enclosing = SceneDirectory::Instance().QueryParent(ThisToken);

    if (PageHeading::Construct(
            Page,
            "TyreHeader",
            "VEHICLE / TYRE",
            Corners[Parameters.Corner & 3u],
            Enclosing.IsValid() ? "Select vehicle" : nullptr,
            "One tyre enclosed by its vehicle: compound sets grip and thermal window, cold pressure seeds the gas law.",
            "Open the enclosing Vehicle record"))
    {
        if (Enclosing.IsValid())
        {
            InspectorRuntime::Instance().RequestWorkspaceAction("select:" + std::to_string(Enclosing.Value));
        }
    }

    const TyreCompound& Chosen = ResolveTyreCompound(Parameters.Compound);
    char GripSummary[24], WindowSummary[32], ColdSummary[24], WearSummary[24];
    std::snprintf(GripSummary, sizeof(GripSummary), "x%.2f", Chosen.GripScale);
    std::snprintf(WindowSummary, sizeof(WindowSummary), "%.0f-%.0f", Chosen.WindowLow_C, Chosen.WindowHigh_C);
    std::snprintf(ColdSummary, sizeof(ColdSummary), "%.0f", Parameters.ColdPressure * 0.001f);
    std::snprintf(WearSummary, sizeof(WearSummary), "x%.2f", Chosen.WearRate);
    const std::array<MetricTile, 4> SummaryTiles = { {
        { "Compound grip", GripSummary, "" },
        { "Window", WindowSummary, "°C" },
        { "Cold pressure", ColdSummary, "kPa" },
        { "Wear rate", WearSummary, "" } } };
    MetricTiles::Construct(Page, SummaryTiles.data(), static_cast<int>(SummaryTiles.size()), MetricTiles::NoLead);

    BeginSection(Page, "Fitting and pit bay", "CircleDot", Parameters.Enclosed ? "fitted" : "pit lane",
                 "Detaching places this tyre on the pit apron beside the car; re-enclosing mounts it back onto the hub.");
    int SelectedCorner = static_cast<int>(Parameters.Corner & 3u);
    if (SelectRow::Construct(Page, "TyreCorner", "Corner", Corners, 4, SelectedCorner))
    {
        Parameters.Corner = static_cast<uint32_t>(SelectedCorner);
        Changed           = true;
    }
    Changed |= Switch(Page, "TyreEnclosed", "Fitted to the vehicle", Parameters.Enclosed);

    std::array<const char*, 5> Compounds{};
    int SelectedCompound = 1;
    for (size_t Index = 0; Index < Compounds.size(); ++Index)
    {
        Compounds[Index] = TyreCompounds[Index].Name;
        if (Parameters.Compound == TyreCompounds[Index].Name) SelectedCompound = static_cast<int>(Index);
    }
    if (SelectRow::Construct(Page, "TyreCompound", "Compound", Compounds.data(), static_cast<int>(Compounds.size()), SelectedCompound))
    {
        Parameters.Compound = TyreCompounds[SelectedCompound].Name;
        Changed             = true;
    }
    double ColdKilopascals = Parameters.ColdPressure * 0.001;
    if (Measure(Page, "TyrePressure", "Cold pressure", ColdKilopascals, 30.0, 400.0, 1.0, "kPa"))
    {
        Parameters.ColdPressure = static_cast<float>(ColdKilopascals * 1000.0);
        Changed                 = true;
    }

    SubHeading(Page, "Tyre set identifier");
    const ImVec2 InputOrigin = Page.Space(28.0f, 2.0f, 8.0f);
    ImGui::SetCursorScreenPos(InputOrigin);
    ImGui::SetNextItemWidth(Page.QueryWidth());
    char SetText[64];
    std::snprintf(SetText, sizeof(SetText), "%s", Parameters.SetName.c_str());
    if (ImGui::InputText("##TyreSetName", SetText, sizeof(SetText)))
    {
        Parameters.SetName = SetText;
        Changed            = true;
    }

    const TyreCompound& UpdatedCompound = ResolveTyreCompound(Parameters.Compound);
    char Note[180];
    std::snprintf(Note, sizeof(Note), "Set %s · %s · grip x%.2f · peak window %.0f - %.0f °C · wear x%.2f",
                  Parameters.SetName.c_str(), UpdatedCompound.Name, UpdatedCompound.GripScale,
                  UpdatedCompound.WindowLow_C, UpdatedCompound.WindowHigh_C, UpdatedCompound.WearRate);
    CaptionText::Construct(Page, Note, 4.0f, 10.0f);
    EndSection(Page);

    VehicleReading Reading;
    if (QueryVehicleReading(ThisToken, Reading) && Reading.Simulating && (Parameters.Corner & 3u) < Reading.Telemetry.WheelCount)
    {
        const Vehicle::WheelTelemetry& W = Reading.Telemetry.Wheels[Parameters.Corner & 3u];
        BeginSection(Page, "Live corner telemetry", "Activity", Vehicle::TyreCarcassName(Reading.Carcass),
                     "Read-only snapshot from the physics thread: carcass deformation, three-layer temperatures, gas-law pressure and slip.");
        char SurfaceText[16], GasText[16], KpaText[16], LoadText[16];
        std::snprintf(SurfaceText, sizeof(SurfaceText), "%.0f", W.Thermal.SurfaceTemperature - 273.15);
        std::snprintf(GasText, sizeof(GasText), "%.0f", W.Thermal.GasTemperature - 273.15);
        std::snprintf(KpaText, sizeof(KpaText), "%.0f", W.Thermal.GaugePressure * 0.001);
        std::snprintf(LoadText, sizeof(LoadText), "%.0f", W.VerticalLoad);
        const std::array<MetricTile, 4> LiveTiles = { {
            { "Tread", SurfaceText, "°C" },
            { "Air inside", GasText, "°C" },
            { "Live pressure", KpaText, "kPa" },
            { "Vertical load", LoadText, "N" } } };
        MetricTiles::Construct(Page, LiveTiles.data(), static_cast<int>(LiveTiles.size()), MetricTiles::NoLead);

        char Line[220];
        std::snprintf(Line, sizeof(Line), "Deflection %.1f mm · enclosed air %.2f L · carcass %.0f °C · effective grip x%.2f",
                      W.TyreDeflection * 1000.0f, W.EnclosedVolume * 1000.0f, W.Thermal.CarcassTemperature - 273.15,
                      W.Thermal.GripFactor * W.WearGripFactor);
        CaptionText::Construct(Page, Line, 4.0f, 6.0f);
        std::snprintf(Line, sizeof(Line), "Slip long %+.3f · lat %+.1f° · Fx %+.0f N · Fy %+.0f N · wear %.2f mm · disc %.0f °C%s",
                      W.SlipRatio, W.SlipAngleRad * 57.2957795f, W.LongitudinalForce, W.LateralForce, W.WearDepth_mm,
                      W.BrakeTemp_K - 273.15f, W.AbsActive ? " · ABS" : "");
        CaptionText::Construct(Page, Line, 2.0f, 10.0f);
        EndSection(Page);
    }

    FooterRow::Construct(Page, "Levelspace record \xC2\xB7 Tyre", "Saved with the level");
    Page.Finalize();
    if (Changed) Parameters.AdvanceRevision();
}

} // namespace Frontier

#elif FRONTIER_EDITOR

namespace Frontier
{
std::string TyreInspector::QueryDirectoryReadout() const { return {}; }
void        TyreInspector::ConstructInspector() {}
} // namespace Frontier

#endif
