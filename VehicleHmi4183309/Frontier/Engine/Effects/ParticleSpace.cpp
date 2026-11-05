#include "Frontier/Engine/Effects/ParticleSpace.h"
#include <cmath>

namespace Frontier
{
const std::array<ParticlePreset, 3> ParticlePresets = {{
    {"Derez cube",      0, 0.3f,  1.0f, 0.5f, 0.6f, 2.6f, 0.4f, 3.5f, 0.075f, 1.2f, "#35d6ff", "#ffffff", "#ff2d6f"},
    {"Reverse derez",   0, 0.15f, 0.8f, 0.3f, 0.4f, 1.8f, 0.6f, 2.8f, 0.07f,  1.3f, "#35d6ff", "#ffffff", "#ff2d6f"},
    {"Glitch dissolve", 1, 0.1f,  0.9f, 0.2f, 0.2f, 1.2f, 1.2f, 2.0f, 0.06f,  1.0f, "#00ffd0", "#ffffff", "#ff2d6f"},
}};
namespace {
// Deterministic per-particle hash (stable across runs; the GPU uses its own cell hash in EffectRecords.wgsl).
float Hash01(uint32_t Seed, uint32_t Index) noexcept
{
    uint32_t H = Seed * 2654435761u ^ (Index + 1u) * 2246822519u;
    H ^= H >> 13; H *= 3266489917u; H ^= H >> 16;
    return float(H & 0xFFFFFFu) / float(0x1000000u);
}
}
bool ParticleSpace::ApplyPreset(std::string_view Name) noexcept
{
    for (const auto& P : ParticlePresets) if (P.Name == Name)
    {
        Settings.Preset = P.Name; Settings.Style = P.Style; Settings.HoldOffset = P.HoldOffset; Settings.Wave = P.Wave;
        Settings.Burst = P.Burst; Settings.SpeedMinimum = P.SpeedMinimum; Settings.SpeedMaximum = P.SpeedMaximum;
        Settings.Drag = P.Drag; Settings.Life = P.Life; Settings.Size = P.Size; Settings.Flight = P.Flight;
        Settings.Glow = P.Glow; Settings.Hot = P.Hot; Settings.Glitch = P.Glitch; return true;
    }
    return false;
}
int ParticleSpace::Emit(EffectKind Kind, const float OriginVelocity[3], int CellCount, float NowSeconds) noexcept
{
    for (int Slot = 0; Slot < ParticleEmitterSlots; ++Slot)
    {
        ParticleEmitter& E = Emitters[size_t(Slot)];
        if (E.Active && IsEmitterLive(E, NowSeconds)) continue;
        E.Active = true; E.Kind = Kind; E.StartSeconds = NowSeconds; E.CellCount = CellCount;
        E.Seed = 1u + uint32_t(EmittedTotal) * 7919u;
        for (int A = 0; A < 3; ++A) E.OriginVelocity[A] = OriginVelocity ? OriginVelocity[A] : 0.0f;
        ++EmittedTotal; return Slot;
    }
    return -1;
}
float ParticleSpace::QueryMaskDuration(MaskMode) const noexcept { return Settings.HoldOffset + Settings.Wave + Settings.Life; }
float ParticleSpace::DerezVehicle(const float V[3], float Now) noexcept
{ Mask = MaskMode::Derez; MaskStartSeconds = Now; const int S = Emit(EffectKind::VehicleDerez, V, VehicleCellCount, Now); MaskSeed = S >= 0 ? Emitters[size_t(S)].Seed : 1; return QueryMaskDuration(MaskMode::Derez); }
float ParticleSpace::RezVehicle(const float V[3], float Now) noexcept
{ Mask = MaskMode::Rez; MaskStartSeconds = Now; const int S = Emit(EffectKind::VehicleRez, V, VehicleCellCount, Now); MaskSeed = S >= 0 ? Emitters[size_t(S)].Seed : 1; return QueryMaskDuration(MaskMode::Rez); }
bool ParticleSpace::IsEmitterLive(const ParticleEmitter& E, float Now) const noexcept
{ return E.Active && (Now - E.StartSeconds) <= QueryMaskDuration(MaskMode::Derez); }
int ParticleSpace::LiveEmitterCount(float Now) const noexcept { int N = 0; for (const auto& E : Emitters) N += IsEmitterLive(E, Now) ? 1 : 0; return N; }
ParticleSpace::ParticleState ParticleSpace::QueryParticle(const ParticleAnchor& A, const ParticleEmitter& E, float Now) const noexcept
{
    // Anchor index recovered from position hash input is not stored; callers pass anchors in cell order —
    // the release hash uses the anchor's family/position quantisation so state is a pure function of inputs.
    const uint32_t Index = uint32_t(std::fabs(A.Position[0]) * 131.0f + std::fabs(A.Position[1]) * 137.0f + std::fabs(A.Position[2]) * 149.0f + A.Family * 17.0f);
    const float Release = Settings.HoldOffset + Settings.Wave * Hash01(E.Seed, Index);
    const float Age = Now - E.StartSeconds - Release;
    ParticleState Out{Age >= 0.0f && Age <= Settings.Life, {A.Position[0], A.Position[1], A.Position[2]}, Age};
    if (!Out.Released) return Out;
    const float Speed = Settings.SpeedMinimum + (Settings.SpeedMaximum - Settings.SpeedMinimum) * Hash01(E.Seed ^ 0x9E3779B9u, Index);
    const float Drag = std::max(Settings.Drag, 1e-4f);
    const float Travel = Settings.Burst * Speed * (1.0f - std::exp(-Drag * Age)) / Drag; // ∫ v·e^(−drag·t)
    const float MomentumT = Settings.Momentum ? Age : 0.0f;
    for (int Axis = 0; Axis < 3; ++Axis)
        Out.Position[Axis] = A.Position[Axis] + A.Normal[Axis] * Travel + E.OriginVelocity[Axis] * MomentumT;
    return Out;
}
} // namespace Frontier
