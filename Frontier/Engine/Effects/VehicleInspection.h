#pragma once
/*==========================================================================================================================================
                                                          VEHICLEINSPECTION.H
==========================================================================================================================================*/
// 🧩 Vehicle inspection modes and reference-derived X-Ray Scan timing.
//    The software presenter and native shader are not yet equivalent to the Three.js renderer.

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

// 📝 208-byte 16-byte-aligned uniform for the standalone native VehicleScan.slang scaffold.
//    It is a separate layout from the prototype WGSL uniform and is not yet bound by the native renderer.
struct alignas(16) ScanEffectRecord
{
    std::array<float, 16> ViewProjection = {}; // row-major, matching Slang's default host matrix layout
    std::array<float, 4>  CameraWorld = { 0.0f, 0.0f, 0.0f, 1.0f };       // xyz = camera world position, w = ElapsedSeconds
    std::array<float, 4>  HeroOriginAndYaw = { 0.0f, 0.0f, 0.0f, 0.0f };  // xyz = hero world origin, w = yaw (radians)
    std::array<float, 4>  ScanParameters = { 3.25f, 0.0f, 0.0f, 0.0f };   // x = ScanX, y = HoloAmount, z = CurtainAlpha, w = ModeCode
    std::array<float, 4>  BoundsAndSparkle = { 3.25f, -3.35f, 0.0f, 0.11285f }; // x = FrontX, y = RearX, z = SparkleOpacity, w = BodyZRebase
    std::array<float, 16> View = {};
    std::array<float, 4>  Viewport = { 1.0f, 0.0f, 0.0f, 0.0f }; // pixel ratio, inverse physical width/height, reserved
};
static_assert(sizeof(ScanEffectRecord) == 208, "ScanEffectRecord must remain 16-byte aligned");

[[nodiscard]] inline double EaseInOutCubic(double T) noexcept
{
    const double X = std::clamp(T, 0.0, 1.0);
    return X < 0.5 ? 4.0 * X * X * X : 1.0 - std::pow(-2.0 * X + 2.0, 3.0) * 0.5;
}

[[nodiscard]] inline float SmoothstepScalar(float Edge0, float Edge1, float Value) noexcept
{
    if (std::fabs(Edge1 - Edge0) < 1e-7f)
        return Value >= Edge1 ? 1.0f : 0.0f;
    const float T = std::clamp((Value - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

[[nodiscard]] inline double DampScalar(double Current, double Target, double Lambda, double DeltaSeconds) noexcept
{
    const double Factor = 1.0 - std::exp(-std::max(0.0, Lambda) * std::max(0.0, DeltaSeconds));
    return Current + (Target - Current) * Factor;
}

struct VehicleInspection
{
    // 📝 1:1 configurator X-Ray Scan timing: 4.6 s nose-to-tail sweep, 1.0 s hold, 1.0 s fade (6.6 s cycle).
    static constexpr double ScanCycleSeconds = 6.6;
    static constexpr double ScanSweepSeconds = 4.6;
    static constexpr double ScanHoldSeconds = 1.0;
    static constexpr double ScanRampLambda = 3.0;
    static constexpr double DefaultFrontX = 3.25;
    static constexpr double DefaultRearX = -3.35;

    InspectionView View = InspectionView::Solid;
    double ScanSeconds = 0.0;        // [s] clock driving the sweep
    float ScanPeriodSeconds = 3.2f;  // [s] one full sweep of the car's length (editor X-ray band)
    double ElapsedSeconds = 0.0;     // [s] monotonic presentation clock (sparkle drift, scanlines)
    double RampAmount = 0.0;         // host-side 3/s damped 0→1 engagement of the X-ray family views
    double FrontX = DefaultFrontX;    // [m] hero-local X of the nose
    double RearX = DefaultRearX;      // [m] hero-local X of the tail
    double HeldScanX = 3.30;         // last scan front, retained while the ramp fades
    InspectionView PriorActiveView = InspectionView::Scan;
    bool ScannableTransparent = false;
    uint64_t ScanGeneration = 0; // lets the presenter rebuild the particle cloud on each mount

    void Cycle() noexcept
    {
        SetView(static_cast<InspectionView>((static_cast<uint8_t>(View) + 1u) % 4u));
    }

    void SetView(InspectionView NextView) noexcept
    {
        if (NextView == View) return;
        if (View == InspectionView::Scan)
            HeldScanX = ResolveScanX();
        if (NextView == InspectionView::Scan)
        {
            ScanSeconds = 0.0;
            ++ScanGeneration;
        }
        if (NextView != InspectionView::Solid)
            PriorActiveView = NextView;
        View = NextView;
    }

    void Advance(double StepSeconds) noexcept
    {
        const double Elapsed = std::max(0.0, StepSeconds);
        const double Step = std::min(Elapsed, 0.05);
        ElapsedSeconds += Elapsed;
        if (View == InspectionView::Scan)
            ScanSeconds += Step;
        const double Target = View != InspectionView::Solid ? 1.0 : 0.0;
        RampAmount = DampScalar(RampAmount, Target, ScanRampLambda, Step);
        if (std::fabs(RampAmount - Target) < 0.002) RampAmount = Target;
        ScannableTransparent = RampAmount > 0.02;
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
    [[nodiscard]] double ResolveCyclePhase() const noexcept
    {
        const double Wrapped = std::fmod(std::max(0.0, ScanSeconds), ScanCycleSeconds);
        return Wrapped < 0.0f ? Wrapped + ScanCycleSeconds : Wrapped;
    }

    // 📝 Eased sweep fraction [0 = nose, 1 = tail] used by the 1:1 X-Ray Scan visuals.
    [[nodiscard]] double ResolveSweepFraction() const noexcept
    {
        if (View != InspectionView::Scan)
            return -1.0f;
        const double Phase = ResolveCyclePhase();
        if (Phase >= ScanSweepSeconds)
            return 1.0f;
        return EaseInOutCubic(Phase / ScanSweepSeconds);
    }

    // 📝 World/vehicle-local X coordinate of the scan plane (+3.25 m nose → -3.35 m tail).
    [[nodiscard]] double ResolveScanX() const noexcept
    {
        if (View == InspectionView::Ghost || View == InspectionView::Wireframe)
            return RearX - 2.0f;   // extra inspection modes reveal the whole vehicle
        if (View != InspectionView::Scan)
            return PriorActiveView == InspectionView::Scan ? HeldScanX : RearX - 2.0f;
        const double Fraction = std::clamp(ResolveSweepFraction(), 0.0, 1.0);
        return FrontX + (RearX - FrontX) * Fraction;
    }

    // 📝 Envelope within one 6.6 s scan cycle: 1.0 during sweep + hold, linear fade over the final 1.0 s.
    [[nodiscard]] double ResolveCycleAmount() const noexcept
    {
        if (View != InspectionView::Scan)
            return 1.0f; // disabling scan restores the cycle envelope while the ramp fades
        const double Phase = ResolveCyclePhase();
        const double HoldEnd = ScanSweepSeconds + ScanHoldSeconds;
        if (Phase < HoldEnd)
            return 1.0f;
        return std::clamp(1.0 - (Phase - HoldEnd) / (ScanCycleSeconds - HoldEnd), 0.0, 1.0);
    }

    [[nodiscard]] double ResolveHoloAmount() const noexcept
    {
        return std::clamp(RampAmount * ResolveCycleAmount(), 0.0, 1.0);
    }

    // 📝 Reference scan attenuation: head- and tail-lamps remain unpatched; the wheel hub is scannable.
    [[nodiscard]] float ResolveSolidAlpha(float LocalX, uint32_t MaterialFamily) const noexcept
    {
        if (MaterialFamily == 13u || MaterialFamily == 14u)
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

    // 📝 CPU presenter contribution from the reference hologram's RGB × alpha. WorldHeight is
    //    measured in the reference scene's Y-up world coordinates; geometry and lighting still differ.
    [[nodiscard]] std::array<float, 3> ResolveGhostAdditive(float LocalX, float WorldHeight, float AbsNdotV) const noexcept
    {
        const InspectionView Effective = View == InspectionView::Solid ? PriorActiveView : View;
        if (Effective == InspectionView::Wireframe) return { 0.0f, 0.0f, 0.0f };
        const float ScanD = LocalX - ResolveScanX();
        const float Vis = SmoothstepScalar(-0.9f, -0.05f, ScanD) * ResolveHoloAmount();
        if (Vis < 0.003f) return { 0.0f, 0.0f, 0.0f };
        const float Fresnel = std::pow(std::clamp(1.0f - std::fabs(AbsNdotV), 0.0f, 1.0f), 2.3f);
        const float Lines = SmoothstepScalar(0.72f, 1.0f, std::sin(WorldHeight * 85.0f) * 0.5f + 0.5f);
        const float Edge = std::exp(-std::pow((ScanD + 0.18f) * 4.2f, 2.0f));
        const float Alpha = (0.028f + Fresnel * 0.50f + Lines * 0.040f + Edge * 0.30f) * Vis;
        constexpr std::array<float, 3> Base = { 0.396755f, 0.715694f, 0.838799f }; // #a9dcec, linear
        constexpr std::array<float, 3> Accent = { 0.039546f, 0.508881f, 0.938686f }; // #38bdf8, linear
        return { (Base[0] + Accent[0] * Edge * 1.15f) * Alpha,
                 (Base[1] + Accent[1] * Edge * 1.15f) * Alpha,
                 (Base[2] + Accent[2] * Edge * 1.15f) * Alpha };
    }

    // 📝 Reference scan edge opacity; extra Wireframe mode uses its existing separate opacity.
    [[nodiscard]] float ResolveWireframeOpacity(float MidpointLocalX) const noexcept
    {
        const InspectionView Effective = View == InspectionView::Solid ? PriorActiveView : View;
        if (MidpointLocalX < ResolveScanX() + 0.05f) return 0.0f;
        return (Effective == InspectionView::Wireframe ? 0.82f : 0.55f) * ResolveHoloAmount();
    }

    [[nodiscard]] std::array<float, 2> ResolveCurtainOpacities() const noexcept
    {
        const InspectionView Effective = View == InspectionView::Solid ? PriorActiveView : View;
        if (Effective != InspectionView::Scan) return { 0.0f, 0.0f };
        const bool Sweeping = View == InspectionView::Scan && ResolveCyclePhase() < ScanSweepSeconds;
        const float Visibility = RampAmount * ResolveCycleAmount() * (Sweeping ? 1.0f : 0.25f);
        return { 0.075f * Visibility, 0.30f * Visibility };
    }

    [[nodiscard]] float ResolveSparkleOpacity() const noexcept
    {
        return View == InspectionView::Scan ? 0.55f : 0.0f;
    }

};

} // namespace Frontier
