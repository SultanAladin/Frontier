#pragma once
/*==========================================================================================================================================
                                                          VEHICLEINSPECTION.H
==========================================================================================================================================*/
// 🧩 Vehicle inspection modes and the 1:1 X-Ray Scan state machine.
//    Preserves the baseline InspectionView / VehicleInspection contract while supplying exact parity with
//    the interactive configurator's Ghost, 14°/28° Dihedral Wireframe, Dual Wavefront Curtains, and 130 Sparkles.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Frontier
{

enum class InspectionView : uint8_t
{
    Solid = 0,       // painted bodywork, opaque
    Ghost = 1,       // double-sided additive Fresnel + scanline + edge shell (#a9dcec + #38bdf8)
    Wireframe = 2,   // 14° body / 28° wheel dihedral hard-crease wireframe (#8fd8f2)
    Scan = 3         // 6.6 s nose-to-tail X-Ray sweep dissolving solid bodywork into Ghost + Wireframe + Curtains + Sparkles
};

// 📝 96-byte 16-byte-aligned GPU uniform block matching Shader/VehicleScan.wgsl and VehicleScan.slang.
struct alignas(16) ScanEffectRecord
{
    std::array<float, 16> ViewProjection = {};
    std::array<float, 4>  CameraWorld = { 0.0f, 0.0f, 0.0f, 1.0f };       // xyz = camera world position, w = ElapsedSeconds
    std::array<float, 4>  HeroOriginAndYaw = { 0.0f, 0.0f, 0.0f, 0.0f };  // xyz = hero world origin, w = yaw (radians)
    std::array<float, 4>  ScanParameters = { 3.25f, 0.0f, 0.0f, 0.0f };   // x = ScanX, y = HoloAmount, z = CurtainAlpha, w = ModeCode
    std::array<float, 4>  BoundsAndSparkle = { 3.25f, -3.35f, 0.0f, 0.11285f }; // x = FrontX, y = RearX, z = SparkleOpacity, w = BodyZRebase
};
static_assert(sizeof(ScanEffectRecord) == 128, "ScanEffectRecord must remain 16-byte aligned");

[[nodiscard]] inline float EaseInOutCubic(float T) noexcept
{
    const float X = std::clamp(T, 0.0f, 1.0f);
    return X < 0.5f ? 4.0f * X * X * X : 1.0f - std::pow(-2.0f * X + 2.0f, 3.0f) * 0.5f;
}

[[nodiscard]] inline float SmoothstepScalar(float Edge0, float Edge1, float Value) noexcept
{
    if (std::fabs(Edge1 - Edge0) < 1e-7f)
        return Value >= Edge1 ? 1.0f : 0.0f;
    const float T = std::clamp((Value - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

[[nodiscard]] inline float DampScalar(float Current, float Target, float Lambda, float DeltaSeconds) noexcept
{
    const float Factor = 1.0f - std::exp(-std::max(0.0f, Lambda) * std::max(0.0f, DeltaSeconds));
    return Current + (Target - Current) * Factor;
}

struct VehicleInspection
{
    // 📝 1:1 configurator X-Ray Scan timing: 4.6 s nose-to-tail sweep, 1.0 s hold, 1.0 s fade (6.6 s cycle).
    static constexpr float ScanCycleSeconds = 6.6f;
    static constexpr float ScanSweepSeconds = 4.6f;
    static constexpr float ScanHoldSeconds = 1.0f;
    static constexpr float ScanRampLambda = 3.0f;
    static constexpr float DefaultFrontX = 3.25f;
    static constexpr float DefaultRearX = -3.35f;

    InspectionView View = InspectionView::Solid;
    float ScanSeconds = 0.0f;        // [s] clock driving the sweep
    float ScanPeriodSeconds = 3.2f;  // [s] one full sweep of the car's length (editor X-ray band)
    float ElapsedSeconds = 0.0f;     // [s] monotonic presentation clock (sparkle drift, scanlines)
    float RampAmount = 0.0f;         // host-side 3/s damped 0→1 engagement of the X-ray family views
    float FrontX = DefaultFrontX;    // [m] hero-local X of the nose
    float RearX = DefaultRearX;      // [m] hero-local X of the tail

    void Cycle() noexcept
    {
        SetView(static_cast<InspectionView>((static_cast<uint8_t>(View) + 1u) % 4u));
        ScanSeconds = 0.0f;
    }

    void SetView(InspectionView NextView) noexcept
    {
        if (View != NextView)
            ScanSeconds = 0.0f;
        View = NextView;
        if (View != InspectionView::Solid && RampAmount <= 0.0f)
            RampAmount = 1.0f;   // immediate engagement when an X-ray view is explicitly selected on host
    }

    void Advance(float StepSeconds) noexcept
    {
        const float Step = std::max(0.0f, StepSeconds);
        ElapsedSeconds += Step;
        if (View == InspectionView::Scan)
            ScanSeconds += Step;
        RampAmount = DampScalar(RampAmount, View != InspectionView::Solid ? 1.0f : 0.0f,
                                ScanRampLambda, std::min(Step, 0.25f));
    }

    // 📝 Face opacity the presentation should use (the editor's X-ray ghosts at 45 %).
    [[nodiscard]] float ResolveFaceOpacity() const noexcept
    {
        switch (View)
        {
            case InspectionView::Ghost: return 0.45f;
            case InspectionView::Scan: return 0.45f;
            case InspectionView::Wireframe: return 0.0f;
            default: return 1.0f;
        }
    }

    // 📝 Whether hidden edges show through the faces (true for every X-ray family view).
    [[nodiscard]] bool ShowsHiddenEdges() const noexcept { return View != InspectionView::Solid; }

    // 📝 The sweep band's centre, as a fraction of the car's length (0 at the nose, 1 at the tail).
    [[nodiscard]] float ResolveScanFraction() const noexcept
    {
        if (ScanPeriodSeconds <= 0.0f)
            return 0.0f;
        const float Phase = ScanSeconds / ScanPeriodSeconds;
        return Phase - static_cast<float>(static_cast<int>(Phase));
    }

    // 📝 Phase within the 6.6 s configurator X-Ray cycle (4.6 s sweep + 1.0 s hold + 1.0 s fade).
    [[nodiscard]] float ResolveCyclePhase() const noexcept
    {
        const float Wrapped = std::fmod(std::max(0.0f, ScanSeconds), ScanCycleSeconds);
        return Wrapped < 0.0f ? Wrapped + ScanCycleSeconds : Wrapped;
    }

    // 📝 Eased sweep fraction [0 = nose, 1 = tail] used by the 1:1 X-Ray Scan visuals.
    [[nodiscard]] float ResolveSweepFraction() const noexcept
    {
        if (View != InspectionView::Scan)
            return -1.0f;
        const float Phase = ResolveCyclePhase();
        if (Phase >= ScanSweepSeconds)
            return 1.0f;
        return EaseInOutCubic(Phase / ScanSweepSeconds);
    }

    // 📝 World/vehicle-local X coordinate of the scan plane (+3.25 m nose → -3.35 m tail).
    [[nodiscard]] float ResolveScanX() const noexcept
    {
        if (View == InspectionView::Ghost || View == InspectionView::Wireframe)
            return RearX - 2.0f;   // fully swept: every ghost/wireframe surface is revealed
        if (View != InspectionView::Scan)
            return FrontX + 2.0f;  // parked ahead of the nose: nothing revealed
        const float Fraction = std::clamp(ResolveSweepFraction(), 0.0f, 1.0f);
        return FrontX + (RearX - FrontX) * Fraction;
    }

    // 📝 Envelope within one 6.6 s scan cycle: 1.0 during sweep + hold, linear fade over the final 1.0 s.
    [[nodiscard]] float ResolveCycleAmount() const noexcept
    {
        if (View != InspectionView::Scan)
            return (View != InspectionView::Solid) ? 1.0f : 0.0f;
        const float Phase = ResolveCyclePhase();
        const float HoldEnd = ScanSweepSeconds + ScanHoldSeconds;
        if (Phase < HoldEnd)
            return 1.0f;
        return std::clamp(1.0f - (Phase - HoldEnd) / std::max(0.1f, ScanCycleSeconds - HoldEnd), 0.0f, 1.0f);
    }

    [[nodiscard]] float ResolveHoloAmount() const noexcept
    {
        if (View == InspectionView::Solid && RampAmount < 1e-3f)
            return 0.0f;
        const float EffectiveRamp = (View != InspectionView::Solid && RampAmount <= 0.0f) ? 1.0f : RampAmount;
        return std::clamp(EffectiveRamp * ResolveCycleAmount(), 0.0f, 1.0f);
    }

    // 📝 Make-it-disappear solid dissolve: lamps (families 10, 13, 14) remain opaque; bodywork dissolves behind ScanX.
    [[nodiscard]] float ResolveSolidAlpha(float LocalX, uint32_t MaterialFamily) const noexcept
    {
        if (MaterialFamily == 10u || MaterialFamily == 13u || MaterialFamily == 14u)
            return 1.0f;
        if (View == InspectionView::Wireframe)
            return 0.035f;
        if (View == InspectionView::Ghost)
            return 0.055f;
        const float Holo = ResolveHoloAmount();
        if (Holo <= 1e-4f)
            return 1.0f;
        const float Sf = SmoothstepScalar(-0.90f, -0.05f, LocalX - ResolveScanX());
        return std::clamp(1.0f - Sf * Holo * 0.965f, 0.0f, 1.0f);
    }

    // 📝 Double-sided additive Ghost evaluation (#a9dcec base + #38bdf8 wavefront edge).
    [[nodiscard]] std::array<float, 3> ResolveGhostAdditive(float LocalX, float LocalZ, float AbsNdotV) const noexcept
    {
        if (View == InspectionView::Wireframe)
            return { 0.0f, 0.0f, 0.0f };
        const float Holo = (View == InspectionView::Ghost)
            ? std::max(RampAmount, 1.0f)
            : ResolveHoloAmount();
        if (Holo <= 1e-4f)
            return { 0.0f, 0.0f, 0.0f };

        const float ScanX = ResolveScanX();
        const float Reveal = (View == InspectionView::Ghost) ? 1.0f : SmoothstepScalar(-0.35f, 0.28f, LocalX - ScanX);
        if (Reveal <= 1e-4f)
            return { 0.0f, 0.0f, 0.0f };

        const float Fresnel = std::pow(std::clamp(1.0f - std::clamp(AbsNdotV, 0.0f, 1.0f), 0.0f, 1.0f), 2.15f);
        const float Scanlines = 0.88f + 0.12f * std::sin(LocalZ * 88.0f - ElapsedSeconds * 3.2f);
        const float EdgeGlow = (View == InspectionView::Scan)
            ? std::exp(-std::pow((LocalX - ScanX) / 0.19f, 2.0f))
            : 0.0f;

        // Linearized #a9dcec (0.392, 0.716, 0.840) and #38bdf8 (0.039, 0.509, 0.939)
        const float BaseWeight = (0.055f + 0.42f * Fresnel) * Scanlines;
        const float EdgeWeight = EdgeGlow * 1.15f;
        const float Mask = Holo * Reveal;
        return {
            (0.392f * BaseWeight + 0.039f * EdgeWeight) * Mask,
            (0.716f * BaseWeight + 0.509f * EdgeWeight) * Mask,
            (0.840f * BaseWeight + 0.939f * EdgeWeight) * Mask
        };
    }

    // 📝 Dihedral wireframe clip & opacity (#8fd8f2).
    [[nodiscard]] float ResolveWireframeOpacity(float MidpointLocalX) const noexcept
    {
        if (View == InspectionView::Solid)
            return 0.0f;
        if (View == InspectionView::Wireframe)
            return 0.62f;
        if (View == InspectionView::Ghost)
            return 0.24f;
        const float Holo = ResolveHoloAmount();
        if (Holo <= 1e-4f || MidpointLocalX < ResolveScanX() + 0.05f)
            return 0.0f;
        const float Wave = std::exp(-std::pow((MidpointLocalX - ResolveScanX()) / 0.28f, 2.0f));
        return std::clamp((0.36f + 0.48f * Wave) * Holo, 0.0f, 1.0f);
    }

    // 📝 Dual scan curtain opacities: {WideGlowOpacity, CoreSheetOpacity}.
    [[nodiscard]] std::array<float, 2> ResolveCurtainOpacities() const noexcept
    {
        if (View != InspectionView::Scan)
            return { 0.0f, 0.0f };
        const float Phase = ResolveCyclePhase();
        const float SweepVis = SmoothstepScalar(0.0f, 0.28f, Phase)
            * (1.0f - SmoothstepScalar(ScanSweepSeconds - 0.35f, ScanSweepSeconds + 0.05f, Phase));
        const float Vis = SweepVis * ResolveHoloAmount();
        return { 0.075f * Vis, 0.30f * Vis };
    }

    [[nodiscard]] float ResolveSparkleOpacity() const noexcept
    {
        if (View == InspectionView::Solid)
            return 0.0f;
        if (View == InspectionView::Ghost || View == InspectionView::Wireframe)
            return 0.38f;
        return 0.52f * ResolveHoloAmount();
    }
};

} // namespace Frontier
