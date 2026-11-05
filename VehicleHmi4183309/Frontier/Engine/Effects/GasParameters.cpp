#include "Frontier/Engine/Effects/GasParameters.h"
#include <cmath>

namespace Frontier
{
// 📝 1:1 with GasParameterCatalogue in SmokeSequence.js @4183309 (Key, Label, Unit, Default, Minimum, Maximum, Step).
const std::array<GasParameterEntry, 33> GasParameterCatalogue = {{
    {"Emission",        "Emission rate",       "×",      1.0f,    0.0f,   4.0f,   0.05f},
    {"Heat",            "Temperature",         "×",      1.0f,    0.0f,   4.0f,   0.05f},
    {"Expansion",       "Volume expansion",    "×",      1.0f,    0.0f,   3.0f,   0.05f},
    {"Soot",            "Soot yield",          "×",      1.0f,    0.0f,   4.0f,   0.05f},
    {"Momentum",        "Carrier drag",        "1/s",    9.0f,    0.0f,   24.0f,  0.5f},
    {"TreadEjection",   "Tread ejection",      "×",      1.0f,    0.0f,   3.0f,   0.05f},
    {"Buoyancy",        "Buoyancy",            "m/s²·K", 2.4f,    0.0f,   8.0f,   0.1f},
    {"Dissipation",     "Dissipation",         "1/s",    0.10f,   0.0f,   0.9f,   0.01f},
    {"Cooling",         "Cooling",             "1/s",    0.55f,   0.0f,   0.95f,  0.01f},
    {"Vorticity",       "Vorticity confinement","ε",     3.0f,    0.0f,   6.0f,   0.1f},
    {"Turbulence",      "Turbulence",          "m/s",    0.60f,   0.0f,   2.0f,   0.02f},
    {"TurbulenceScale", "Turbulence frequency","1/m",    3.7f,    0.2f,   4.0f,   0.05f},
    {"WindSpeed",       "Wind speed",          "m/s",    0.8f,    0.0f,   12.0f,  0.1f},
    {"WindHeading",     "Wind heading",        "°",      30.0f,   0.0f,   360.0f, 1.0f},
    {"TimeScale",       "Time scale",          "×",      1.0f,    0.25f,  2.0f,   0.05f},
    {"PressureSweeps",  "Pressure iterations", "",       16.0f,   4.0f,   40.0f,  2.0f},
    {"Density",         "Density",             "×",      1.0f,    0.2f,   3.0f,   0.05f},
    {"Albedo",          "Albedo",              "",       0.88f,   0.2f,   0.98f,  0.01f},
    {"Phase",           "Forward scattering",  "g",      0.38f,   0.0f,   0.9f,   0.01f},
    {"MultiScatter",    "Multiple scattering", "",       0.75f,   0.0f,   1.5f,   0.05f},
    {"Ambient",         "Ambient light",       "×",      1.0f,    0.0f,   3.0f,   0.05f},
    {"Detail",          "Detail",              "",       0.70f,   0.0f,   1.0f,   0.02f},
    {"DetailScale",     "Detail frequency",    "1/m",    2.2f,    0.2f,   4.0f,   0.05f},
    {"Erosion",         "Edge erosion",        "",       1.0f,    0.0f,   1.5f,   0.05f},
    {"Warp",            "Billow warp",         "",       0.0f,    0.0f,   1.0f,   0.05f},
    {"Powder",          "Powder",              "",       0.0f,    0.0f,   1.0f,   0.05f},
    {"FlameIntensity",  "Flame intensity",     "×",      1.0f,    0.0f,   4.0f,   0.05f},
    {"FlameTemperature","Flame temperature",   "×",      1.0f,    0.7f,   1.6f,   0.01f},
    {"FlameCore",       "Flame core",          "K",      1800.0f, 1300.0f,2400.0f, 10.0f},
    {"FlameTip",        "Flame tip",           "K",      1150.0f, 900.0f, 1600.0f, 10.0f},
    {"FlameFlicker",    "Flicker",             "",       0.6f,    0.0f,   1.0f,   0.02f},
    {"FlameLoss",       "Flame decay",         "1/s",    0.96f,   0.5f,   0.995f, 0.005f},
    {"FireLight",       "Fire light",          "×",      1.0f,    0.0f,   4.0f,   0.05f},
}};

namespace {
struct FieldBinding { std::string_view Key; float GasParameters::* Field; };
// Order must match GasParameters member order / catalogue order.
const FieldBinding FieldBindings[] = {
    {"Emission",&GasParameters::Emission},{"Heat",&GasParameters::Heat},{"Expansion",&GasParameters::Expansion},
    {"Soot",&GasParameters::Soot},{"Momentum",&GasParameters::Momentum},{"TreadEjection",&GasParameters::TreadEjection},
    {"Buoyancy",&GasParameters::Buoyancy},{"Dissipation",&GasParameters::Dissipation},{"Cooling",&GasParameters::Cooling},
    {"Vorticity",&GasParameters::Vorticity},{"Turbulence",&GasParameters::Turbulence},{"TurbulenceScale",&GasParameters::TurbulenceScale},
    {"WindSpeed",&GasParameters::WindSpeed},{"WindHeading",&GasParameters::WindHeading},{"TimeScale",&GasParameters::TimeScale},
    {"PressureSweeps",&GasParameters::PressureSweeps},{"Density",&GasParameters::Density},{"Albedo",&GasParameters::Albedo},
    {"Phase",&GasParameters::Phase},{"MultiScatter",&GasParameters::MultiScatter},{"Ambient",&GasParameters::Ambient},
    {"Detail",&GasParameters::Detail},{"DetailScale",&GasParameters::DetailScale},{"Erosion",&GasParameters::Erosion},
    {"Warp",&GasParameters::Warp},{"Powder",&GasParameters::Powder},{"FlameIntensity",&GasParameters::FlameIntensity},
    {"FlameTemperature",&GasParameters::FlameTemperature},{"FlameCore",&GasParameters::FlameCore},{"FlameTip",&GasParameters::FlameTip},
    {"FlameFlicker",&GasParameters::FlameFlicker},{"FlameLoss",&GasParameters::FlameLoss},{"FireLight",&GasParameters::FireLight},
};
const GasParameterEntry* FindEntry(std::string_view Key) noexcept {
    for (const auto& E : GasParameterCatalogue) if (E.Key == Key) return &E; return nullptr;
}
} // namespace

float* GasParameters::Resolve(std::string_view Key) noexcept {
    for (const auto& B : FieldBindings) if (B.Key == Key) return &(this->*B.Field); return nullptr;
}
const float* GasParameters::Resolve(std::string_view Key) const noexcept {
    for (const auto& B : FieldBindings) if (B.Key == Key) return &(this->*B.Field); return nullptr;
}
void GasParameters::AssignClamped(std::string_view Key, float Value) noexcept {
    const GasParameterEntry* E = FindEntry(Key); float* Slot = Resolve(Key);
    if (!E || !Slot) return;
    float V = Value < E->Minimum ? E->Minimum : (Value > E->Maximum ? E->Maximum : Value);
    if (E->Step > 0.0f) V = E->Minimum + std::round((V - E->Minimum) / E->Step) * E->Step;
    *Slot = V < E->Minimum ? E->Minimum : (V > E->Maximum ? E->Maximum : V);
}

// 📝 InspectorPages.js GasPresets @4183309, verbatim values.
const std::array<GasPreset, 6> GasPresets = {{
    {"Tyre smoke",    [](GasParameters&) noexcept {}},
    {"Thick burnout", [](GasParameters& G) noexcept { G.Emission=2.2f; G.Density=1.6f; G.Dissipation=0.12f; G.Albedo=0.9f; G.Vorticity=2.2f; G.Expansion=1.4f; G.MultiScatter=0.9f; }},
    {"Light haze",    [](GasParameters& G) noexcept { G.Emission=0.6f; G.Density=0.55f; G.Dissipation=0.35f; G.Detail=0.75f; G.Erosion=0.9f; G.Albedo=0.92f; }},
    {"Inferno",       [](GasParameters& G) noexcept { G.FlameIntensity=2.4f; G.FlameTemperature=1.18f; G.FlameFlicker=0.85f; G.Soot=2.2f; G.Heat=1.8f; G.FireLight=2.2f; G.Buoyancy=3.4f; }},
    {"Crosswind",     [](GasParameters& G) noexcept { G.WindSpeed=5.5f; G.WindHeading=95.0f; G.Turbulence=0.7f; G.Dissipation=0.28f; }},
    {"Legacy billows",[](GasParameters& G) noexcept { G.Density=1.0f; G.Dissipation=0.22f; G.Vorticity=1.6f; G.Turbulence=0.4f; G.TurbulenceScale=1.1f; G.Albedo=0.86f; G.Phase=0.45f; G.MultiScatter=0.45f; G.Detail=0.65f; G.DetailScale=0.85f; G.Erosion=0.75f; G.Warp=0.35f; G.Powder=0.2f; G.WindSpeed=0.0f; }},
}};
bool ApplyGasPreset(GasParameters& Gas, std::string_view Name) noexcept {
    for (const auto& P : GasPresets) if (P.Name == Name) { Gas.ResetToDefaults(); P.Apply(Gas); return true; }
    return false; // unknown name: leave settings untouched (never wipe on a refused preset)
}

PlumeSettings ResolvePlumeSettings(const GasParameters& Gas, bool Enabled) noexcept {
    PlumeSettings S; S.PlumeEnabled = Enabled;
    S.PlumeEmissionRate = Gas.Emission; S.PlumeDensity = 1.2f * Gas.Density; S.PlumeHeat = Gas.Heat;
    S.PlumeSoot = Gas.Soot; S.PlumeBuoyancy = Gas.Buoyancy; S.PlumeDissipation = Gas.Dissipation;
    S.PlumeCooling = Gas.Cooling; S.PlumeVorticity = Gas.Vorticity; S.PlumeTurbulence = Gas.Turbulence;
    S.PlumeTurbulenceScale = Gas.TurbulenceScale; S.PlumeWindSpeed = Gas.WindSpeed; S.PlumeWindDirection = Gas.WindHeading;
    S.PlumeAlbedo = Gas.Albedo; S.PlumeAnisotropy = Gas.Phase; S.PlumeScattering = Gas.MultiScatter;
    S.PlumeAmbient = Gas.Ambient; S.PlumeDetail = Gas.Detail; S.PlumeDetailScale = Gas.DetailScale;
    S.PlumeFire = Gas.FlameIntensity; S.PlumeFlameCore = Gas.FlameCore * Gas.FlameTemperature;
    S.PlumeFlameTip = Gas.FlameTip * Gas.FlameTemperature; S.PlumeFireLight = Gas.FireLight;
    const float Remaining = 1.0f - Gas.FlameLoss;
    S.PlumeFlameDecay = -std::log(Remaining > 0.005f ? Remaining : 0.005f);
    return S;
}
int ResolvePressureSweeps(const GasParameters& Gas) noexcept {
    const int Rounded = 2 * static_cast<int>(std::round(Gas.PressureSweeps / 2.0f));
    return Rounded > 4 ? Rounded : 4;
}
} // namespace Frontier
