/*==========================================================================================================================================
                                                            TYREINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 Native C++ TyreInspector: per-corner compound window, 3-node temperatures, PV=mRT gas pressure, Pacejka MF6.1, and pit enclosure.

#include "Engine/Interface/Inspectors/TyreInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorControls.h"

#include <algorithm>
#include <cstdio>

namespace Frontier
{

namespace
{

constexpr const char* CompoundNames[5] = {"Soft", "Medium", "Hard", "Intermediate", "Wet"};

} // namespace

std::string TyreInspector::QueryDirectoryReadout() const
{
    const char* Compound = CompoundNames[std::clamp(Parameters.CompoundIndex, 0, 4)];
    char        Text[96]{};
    std::snprintf(Text, sizeof(Text), "%s \xC2\xB7 %.0f kPa \xC2\xB7 %s",
                  Compound,
                  Parameters.ColdPressureKpa,
                  Parameters.EnclosedCondition ? "enclosed" : "pit lane");
    return Text;
}

void TyreInspector::ConstructInspector()
{
    const auto    Theme = ControlTheme::ResolveNative();
    const auto    Faces = EditorTypefaces::Resolve();
    ControlColumn Page(Theme, Faces, InspectorRuntime::Instance().ResolveInterfaceScale());
    bool          Changed = false;

    const std::string Title  = "Tyre \xC2\xB7 " + Parameters.CornerCode;
    const char*       Status = Parameters.EnclosedCondition ? "\xE2\x97\x8F Enclosed" : "\xE2\x97\x8B Pit lane";
    if (PageHeading::Construct(Page, "TyrePower", "VEHICLE / CORNER TYRE", Title.c_str(), Status,
                               "Independent corner tyre record: 3-node carcass thermodynamics, Pacejka MF6.1 shape, and pit enclosure."))
    {
        Parameters.EnclosedCondition = !Parameters.EnclosedCondition;
        Changed                      = true;
    }

    int UnitIdx = Parameters.UnitDisplayImperial ? 1 : 0;
    static constexpr const char* UnitOptions[2] = {"Metric \xC2\xB7 kPa \xC2\xB7 \xC2\xB0" "C \xC2\xB7 mm", "Imperial \xC2\xB7 psi \xC2\xB7 \xC2\xB0" "F \xC2\xB7 in"};
    if (ChoiceRow::Construct(Page, "TyreUnits", "Display units", UnitOptions, 2, UnitIdx))
    {
        Parameters.UnitDisplayImperial = (UnitIdx == 1);
    }

    const bool  Imp    = Parameters.UnitDisplayImperial;
    const float PScale = Imp ? 0.14503774f : 1.0f;
    const float LScale = Imp ? 0.03937008f : 1.0f;
    const char* PUnit  = Imp ? "psi" : "kPa";
    const char* LUnit  = Imp ? "in" : "mm";

    char PressStr[24]{}, SurfStr[24]{}, WearStr[24]{}, LapStr[24]{};
    std::snprintf(PressStr, sizeof(PressStr), "%.0f", Parameters.ColdPressureKpa * PScale);
    std::snprintf(SurfStr, sizeof(SurfStr), "%.0f", Imp ? (Parameters.SurfaceTemperatureC * 1.8f + 32.0f) : Parameters.SurfaceTemperatureC);
    std::snprintf(WearStr, sizeof(WearStr), "%.0f", Parameters.WearPercent);
    std::snprintf(LapStr, sizeof(LapStr), "%d", Parameters.StintLaps);
    const MetricTile Tiles[4] = {
        {"Cold pressure", PressStr, PUnit},
        {"Tread surface", SurfStr, Imp ? "\xC2\xB0" "F" : "\xC2\xB0" "C"},
        {"Wear", WearStr, "%"},
        {"Stint", LapStr, "laps"}};
    MetricTiles::Construct(Page, Tiles, 4, 0);

    CardLook Look;
    Look.Radius        = 22.0f;
    Look.PaddingTop    = 16.0f;
    Look.PaddingSide   = 14.0f;
    Look.PaddingBottom = 8.0f;
    Look.MarginBottom  = 16.0f;
    Look.HeadingGlyph  = "Torus";
    Page.BeginCard(Look, "Compound, carcass & Pacejka MF6.1", CompoundNames[std::clamp(Parameters.CompoundIndex, 0, 4)]);

    Changed |= ChoiceRow::Construct(Page, "TyreCmp", "Compound", CompoundNames, 5, Parameters.CompoundIndex);
    Changed |= ToggleRow::Construct(Page, "TyreEnc", "Enclosed by vehicle (false = Pit Lane)", Parameters.EnclosedCondition);
    Changed |= ToggleRow::Construct(Page, "TyrePunc", "Punctured carcass (rapid deflation)", Parameters.PuncturedCondition);
    Changed |= SliderRow::Construct(Page, "TyreColdP", "Cold inflation pressure", Parameters.ColdPressureKpa, 120.0f, 260.0f, 1.0f, PUnit, PScale);
    Changed |= SliderRow::Construct(Page, "TyreHotP", "Operating carcass pressure", Parameters.HotPressureKpa, 140.0f, 320.0f, 1.0f, PUnit, PScale);
    Changed |= SliderRow::Construct(Page, "TyreSurfT", "Surface temperature", Parameters.SurfaceTemperatureC, 20.0f, 160.0f, 1.0f, "\xC2\xB0" "C");
    Changed |= SliderRow::Construct(Page, "TyreBeltT", "Belt intermediate temperature", Parameters.BeltTemperatureC, 20.0f, 160.0f, 1.0f, "\xC2\xB0" "C");
    Changed |= SliderRow::Construct(Page, "TyreGasT", "Internal gas temperature (PV=mRT)", Parameters.GasTemperatureC, 20.0f, 150.0f, 1.0f, "\xC2\xB0" "C");
    Changed |= SliderRow::Construct(Page, "TyreCoreT", "Inner ply core temperature", Parameters.CoreTemperatureC, 20.0f, 160.0f, 1.0f, "\xC2\xB0" "C");
    Changed |= SliderRow::Construct(Page, "TyreWear", "Wear consumed", Parameters.WearPercent, 0.0f, 100.0f, 1.0f, "%");
    Changed |= SliderRow::Construct(Page, "TyreTread", "Tread groove depth", Parameters.TreadDepthMm, 1.5f, 9.5f, 0.1f, LUnit, LScale);
    Changed |= SliderRow::Construct(Page, "TyreRad", "Loaded outer radius", Parameters.LoadedRadiusMm, 280.0f, 400.0f, 1.0f, LUnit, LScale);
    Changed |= SliderRow::Construct(Page, "TyreWid", "Nominal section width", Parameters.SectionWidthMm, 180.0f, 360.0f, 1.0f, LUnit, LScale);
    Changed |= SliderRow::Construct(Page, "TyreLoad", "Standing contact load", Parameters.ContactPatchLoadKn, 0.5f, 8.0f, 0.1f, "kN");
    Changed |= SliderRow::Construct(Page, "TyrePcx", "Pacejka longitudinal shape (PCx1)", Parameters.PacejkaLongitudinalShape, 1.0f, 2.3f, 0.02f, "");
    Changed |= SliderRow::Construct(Page, "TyrePcy", "Pacejka lateral shape (PCy1)", Parameters.PacejkaLateralShape, 0.9f, 2.0f, 0.02f, "");
    Changed |= SliderRow::Construct(Page, "TyreSens", "Normal-load sensitivity", Parameters.LoadSensitivity, 0.0f, 0.45f, 0.01f, "");
    Page.EndCard();

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
