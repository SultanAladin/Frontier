/*==============================================================================================================================================
                                                            SURFELRADIALDEPTH.CPP
==============================================================================================================================================*/
// 🧩 The CPU mirror of compute_surfel_depth_weight (SurfelRadialDepth.glsl). A line-for-line transcription of the shader's Hamburger 4MSM so the
//    headless I4 gate can oracle a probed tile's visibility without a device readback of the shader math. Every constant matches the shader: alpha =
//    2e-6 moment bias, the sqrt(m2) scale normalisation, the LDLᵀ solve, the near/middle/far branch classification, the variance- and grazing-adaptive
//    bleed reduction, the shadow-strength multiply. If the shader math changes, this changes in the same edit (probe-transcription-goes-stale).

#include "Graphics/Surfel/SurfelRadialDepth.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

inline float ClampF(float Value, float Low, float High)
{
    return std::max(Low, std::min(High, Value));
}

// reduce_light_bleeding (SurfelRadialDepth.glsl:39-43). Remap [amount..1] -> [0..1], clamp below `amount` to 0.
float ReduceLightBleeding(float Visibility, float Amount)
{
    float A = ClampF(Amount, 0.0f, 0.99f);
    return ClampF((Visibility - A) / (1.0f - A), 0.0f, 1.0f);
}

} // namespace

float SurfelComputeDepthWeightHost(float M1In, float M2In, float M3In, float M4In,
                                   float Distance, float CosTheta,
                                   const SurfelOcclusionParams& Params)
{
    // Uninitialised => visible (the shader keys on Moments.w == 0.0, which is M4 here).
    if (M4In == 0.0f) { return 1.0f; }

    float Zf = std::max(Distance, 1e-4f);

    float M1 = std::max(0.0f, M1In);
    float M2 = std::max(0.0f, M2In);
    float M3 = std::max(0.0f, M3In);
    float M4 = std::max(0.0f, M4In);

    // Scale normalisation by RMS depth ~ sqrt(E[z^2]).
    float S     = std::max(1e-4f, std::sqrt(M2));
    float InvS  = 1.0f / S;
    float InvS2 = InvS * InvS;
    float InvS3 = InvS2 * InvS;
    float InvS4 = InvS2 * InvS2;

    float Z = Zf * InvS;

    float Bx = M1 * InvS;
    float By = M2 * InvS2;
    float Bz = M3 * InvS3;
    float Bw = M4 * InvS4;

    // Moment bias (alpha ~ 2e-6 for fp32).
    float Alpha = 2e-6f;
    Bx = Bx * (1.0f - Alpha) + 0.5f * Alpha;
    By = By * (1.0f - Alpha) + 0.5f * Alpha;
    Bz = Bz * (1.0f - Alpha) + 0.5f * Alpha;
    Bw = Bw * (1.0f - Alpha) + 0.5f * Alpha;

    float B1 = Bx;
    float B2 = By;
    float B3 = Bz;
    float B4 = Bw;

    // Normalised variance for the adaptive bleed reduction.
    float VarN      = std::max(0.0f, B2 - B1 * B1);
    float VarFactor = ClampF(std::sqrt(VarN), 0.0f, 1.0f);

    // Grazing term in [0..1].
    float CosThetaC = ClampF(CosTheta, 0.0f, 1.0f);
    float Grazing   = 1.0f - CosThetaC;

    // Effective bleed reduction: base + variance term + grazing term.
    float Bleed = ClampF(Params.BleedReduction
                         + Params.VarianceBleedScale * VarFactor
                         + Params.GrazingBiasScale * Grazing * VarFactor,
                         0.0f, 0.99f);

    float Shadow;
    float Eps = 1e-6f;

    // Solve B c = (1, z, z^2)^T with LDLᵀ.
    float L10 = B1;
    float L20 = B2;

    float D1  = std::max(Eps, B2 - B1 * B1);
    float L21 = (B3 - B2 * B1) / D1;
    float D2  = std::max(Eps, B4 - B2 * B2 - L21 * L21 * D1);

    float Rhs0 = 1.0f;
    float Rhs1 = Z;
    float Rhs2 = Z * Z;

    float Y0 = Rhs0;
    float Y1 = Rhs1 - L10 * Y0;
    float Y2 = Rhs2 - L20 * Y0 - L21 * Y1;

    float Z0  = Y0;        // D0 == 1
    float Z1  = Y1 / D1;
    float Z2v = Y2 / D2;

    float C2 = Z2v;
    float C1 = Z1 - L21 * C2;
    float C0 = Z0 - L10 * C1 - L20 * C2;

    if (std::fabs(C2) < 1e-6f)
    {
        if (Z <= B1)
        {
            Shadow = 0.0f;
        }
        else
        {
            float Diff = Z - B1;
            Shadow = 1.0f - D1 / (D1 + Diff * Diff);
        }
    }
    else
    {
        float Disc = std::max(0.0f, C1 * C1 - 4.0f * C2 * C0);
        float Sd   = std::sqrt(Disc);

        float Z2r = (-C1 - Sd) / (2.0f * C2);
        float Z3r = (-C1 + Sd) / (2.0f * C2);

        if (Z2r > Z3r)
        {
            float T = Z2r;
            Z2r = Z3r;
            Z3r = T;
        }

        if (std::fabs(Z3r - Z2r) < 1e-6f)
        {
            if (Z <= B1)
            {
                Shadow = 0.0f;
            }
            else
            {
                float Diff = Z - B1;
                Shadow = 1.0f - D1 / (D1 + Diff * Diff);
            }
        }
        else if (Z <= Z2r)
        {
            Shadow = 0.0f;
        }
        else if (Z <= Z3r)
        {
            float Denom = (Z3r - Z2r) * std::max(Eps, (Z - Z2r));
            Shadow = (Z * Z3r - B1 * (Z + Z3r) + B2) / Denom;
        }
        else
        {
            float Denom = std::max(Eps, (Z - Z2r) * (Z - Z3r));
            Shadow = 1.0f - (Z2r * Z3r - B1 * (Z2r + Z3r) + B2) / Denom;
        }
    }

    Shadow = ClampF(Shadow, 0.0f, 1.0f);

    float ShadowStrength = std::max(0.0f, Params.ShadowStrength);
    Shadow = ClampF(Shadow * ShadowStrength, 0.0f, 1.0f);

    float Visibility = 1.0f - Shadow;
    Visibility = ReduceLightBleeding(Visibility, Bleed);

    return ClampF(Visibility, 0.0f, 1.0f);
}

} // namespace Frontier
