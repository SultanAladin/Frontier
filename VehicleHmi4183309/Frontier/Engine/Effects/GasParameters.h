#pragma once
/*==========================================================================================================================================
                                                          GASPARAMETERS.H
==========================================================================================================================================*/
// 🧩 Visual effects only (Engine/Effects): the Fluid/Gas editor's parameter surface, ported 1:1 from the HTML
//    demo at 4183309 — Source/SmokeSequence.js `GasParameterCatalogue` (32 keys, in catalogue order) and
//    `MapPlumeSettings`, plus the six look presets from Source/InspectorPages.js `GasPresets`.
//    Written fresh from those JS sources; the post-4183309 C++ attempts were discarded per owner instruction.
//    Nothing here is read by physics, session rules or the packet contract — presentations/solvers only.

#include <array>
#include <cstdint>
#include <string_view>

namespace Frontier
{

struct GasParameterEntry
{
    std::string_view Key;
    std::string_view Label;
    std::string_view Unit;
    float Default = 0.0f, Minimum = 0.0f, Maximum = 0.0f, Step = 0.0f;
};

// 📝 Catalogue order is the JS object order: source (6) · simulation (10) · appearance (10) · fire (7) = 33 entries
//    in the source file (Emission…FireLight). Count is taken from the array, never hard-coded elsewhere.
extern const std::array<GasParameterEntry, 33> GasParameterCatalogue;

struct GasParameters
{
    // source — multipliers on the tyre model's own rates
    float Emission = 1.0f, Heat = 1.0f, Expansion = 1.0f, Soot = 1.0f, Momentum = 9.0f, TreadEjection = 1.0f;
    // simulation
    float Buoyancy = 2.4f, Dissipation = 0.10f, Cooling = 0.55f, Vorticity = 3.0f, Turbulence = 0.60f,
          TurbulenceScale = 3.7f, WindSpeed = 0.8f, WindHeading = 30.0f, TimeScale = 1.0f, PressureSweeps = 16.0f;
    // appearance
    float Density = 1.0f, Albedo = 0.88f, Phase = 0.38f, MultiScatter = 0.75f, Ambient = 1.0f, Detail = 0.70f,
          DetailScale = 2.2f, Erosion = 1.0f, Warp = 0.0f, Powder = 0.0f;
    // fire
    float FlameIntensity = 1.0f, FlameTemperature = 1.0f, FlameCore = 1800.0f, FlameTip = 1150.0f,
          FlameFlicker = 0.6f, FlameLoss = 0.96f, FireLight = 1.0f;

    [[nodiscard]] float* Resolve(std::string_view Key) noexcept;
    [[nodiscard]] const float* Resolve(std::string_view Key) const noexcept;
    // 📝 Editor slider semantics: clamp to [Minimum, Maximum], then snap to the Step grid anchored at Minimum.
    void AssignClamped(std::string_view Key, float Value) noexcept;
    void ResetToDefaults() noexcept { *this = GasParameters{}; }
};

// 📝 The six InspectorPages look presets (partial overrides over catalogue defaults).
struct GasPreset { std::string_view Name; void (*Apply)(GasParameters&) noexcept; };
extern const std::array<GasPreset, 6> GasPresets;
bool ApplyGasPreset(GasParameters& Gas, std::string_view Name) noexcept; // false (no change) on unknown name

// 📝 MapPlumeSettings (SmokeSequence.js), verbatim — including PlumeDensity = 1.2 × Density and
//    PlumeFlameDecay = −ln(max(0.005, 1 − FlameLoss)).
struct PlumeSettings
{
    bool PlumeEnabled = true;
    float PlumeEmissionRate = 1.0f, PlumeDensity = 1.2f, PlumeHeat = 1.0f, PlumeSoot = 1.0f, PlumeBuoyancy = 2.4f,
          PlumeDissipation = 0.10f, PlumeCooling = 0.55f, PlumeVorticity = 3.0f, PlumeTurbulence = 0.60f,
          PlumeTurbulenceScale = 3.7f, PlumeWindSpeed = 0.8f, PlumeWindDirection = 30.0f, PlumeAlbedo = 0.88f,
          PlumeAnisotropy = 0.38f, PlumeScattering = 0.75f, PlumeAmbient = 1.0f, PlumeDetail = 0.70f,
          PlumeDetailScale = 2.2f, PlumeFire = 1.0f, PlumeFlameCore = 1800.0f, PlumeFlameTip = 1150.0f,
          PlumeFireLight = 1.0f, PlumeFlameDecay = 4.0f;
    // 📝 PlumeDefaults (PlumeSpace.js) fields MapPlumeSettings passes through untouched.
    float PlumeExtinction = 3.6f, PlumeSootAlbedo = 0.05f, PlumeEmbers = 1.0f;
    int PlumeGrade = 2;
};
[[nodiscard]] PlumeSettings ResolvePlumeSettings(const GasParameters& Gas, bool Enabled = true) noexcept;
// 📝 SmokeSequence.Advance: PressureSweeps = max(4, 2·round(Gas.PressureSweeps / 2)).
[[nodiscard]] int ResolvePressureSweeps(const GasParameters& Gas) noexcept;

} // namespace Frontier
