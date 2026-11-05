#pragma once
/*==========================================================================================================================================
                                                          PARTICLESPACE.H
==========================================================================================================================================*/
// 🧩 Visual effects only: the Particle Editor runtime, ported 1:1 from Source/EffectSequence.js @4183309 —
//    the three Particle Editor presets ("Derez cube", "Reverse derez", "Glitch dissolve"), the settings
//    defaults, the 8-emitter pool, mask modes and the vehicle/box cell counts. Flight is the same closed
//    form the GPU raster evaluates (release on one clock, drag-limited burst along the anchor normal);
//    this CPU mirror exists for presentations, tools and validation. Physics/session never read it.

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Frontier
{
enum class EffectKind : uint8_t { VehicleDerez = 0, VehicleRez = 1, BoxDerez = 2, BoxRez = 3, Debris = 4 };
enum class MaskMode : uint8_t { Off = 0, Derez = 1, Rez = 2, Hidden = 3 };
inline constexpr int ParticleEmitterSlots = 8;    // EmitterSlots
inline constexpr int VehicleBodyCells = 1536, VehicleWheelCells = 96; // per wheel
inline constexpr int VehicleCellCount = VehicleBodyCells + 4 * VehicleWheelCells;
inline constexpr int BoxCellCount = 216;          // 6 × 6 per face

struct ParticlePreset
{
    std::string_view Name; int Style; // 0 cubes, 1 glitch
    float HoldOffset, Wave, Burst, SpeedMinimum, SpeedMaximum, Drag, Life, Size, Flight;
    const char* Glow; const char* Hot; const char* Glitch;
};
// 📝 DerezPresets (EffectSequence.js), verbatim.
extern const std::array<ParticlePreset, 3> ParticlePresets;

struct ParticleSettings
{   // 📝 EffectSettingsDefaults: preset fields + Intensity 1.0, Faces true, Momentum true, GlowStrength 6.0.
    std::string_view Preset = "Derez cube";
    int Style = 0; float HoldOffset = 0.3f, Wave = 1.0f, Burst = 0.5f, SpeedMinimum = 0.6f, SpeedMaximum = 2.6f,
          Drag = 0.4f, Life = 3.5f, Size = 0.075f, Flight = 1.2f;
    const char* Glow = "#35d6ff"; const char* Hot = "#ffffff"; const char* Glitch = "#ff2d6f";
    float Intensity = 1.0f; bool Faces = true, Momentum = true; float GlowStrength = 6.0f;
};

struct ParticleAnchor { float Position[3]; float Normal[3]; float Family; };
struct ParticleEmitter
{
    bool Active = false; EffectKind Kind = EffectKind::VehicleDerez; float StartSeconds = 0.0f;
    float OriginVelocity[3] = {0, 0, 0}; uint32_t Seed = 1; int CellCount = 0;
};

class ParticleSpace
{
public:
    ParticleSettings Settings;
    MaskMode Mask = MaskMode::Off; float MaskStartSeconds = 0.0f; uint32_t MaskSeed = 1;
    float ClockSeconds = 0.0f;
    std::array<ParticleEmitter, ParticleEmitterSlots> Emitters{};
    int EmittedTotal = 0;

    bool ApplyPreset(std::string_view Name) noexcept; // false (no change) on unknown name
    // Returns emitter slot or −1 when the pool is full (JS pool is fixed at 8).
    int Emit(EffectKind Kind, const float OriginVelocity[3], int CellCount, float NowSeconds) noexcept;
    float DerezVehicle(const float VehicleVelocity[3], float NowSeconds) noexcept; // returns mask duration [s]
    float RezVehicle(const float VehicleVelocity[3], float NowSeconds) noexcept;
    void Advance(float StepSeconds) noexcept { ClockSeconds += StepSeconds; }
    [[nodiscard]] bool IsEmitterLive(const ParticleEmitter& E, float NowSeconds) const noexcept;
    [[nodiscard]] int LiveEmitterCount(float NowSeconds) const noexcept;
    // 📝 Closed-form flight for one anchor at time t (EffectRaster.wgsl terms): released after
    //    HoldOffset + Wave·hash, travels Burst·speed along the normal with exponential drag, lives Life.
    struct ParticleState { bool Released; float Position[3]; float Age; };
    [[nodiscard]] ParticleState QueryParticle(const ParticleAnchor& Anchor, const ParticleEmitter& E, float NowSeconds) const noexcept;
    [[nodiscard]] float QueryMaskDuration(MaskMode Mode) const noexcept; // HoldOffset + Wave + Life of active preset
};
} // namespace Frontier
