/*==========================================================================================================================================
                                                           PACEJKAMAGICFORMULA.H
==========================================================================================================================================*/
// 🧩 Pacejka Magic-Formula (MF6.1) tyre force formula
//
//    Project Drive's Pacejka implementation from SultanAladin/Frontier- at 3f14808: longitudinal/lateral
//    forces, self-aligning torque and combined-slip weighting. It is separate from the HTML demo's reduced
//    tyre solver. The source's GRIT-parity and MF6.1 claims are not independently established for every term,
//    and its default coefficients have not been fitted to a measured tyre in this repository.
//
//    Pure C++/scalar wheel-axes math: NO Unreal, NO Jolt, NO threading. The vehicle layer feeds it slip/load/camber and
//    applies the returned Fx/Fy/Mz through the RigidBodySolver seam. Loads are Newtons in, forces Newtons out (the authored car
//    normalises to kN internally, exactly as GRIT does).
//
//    References (as cited in the GRIT source and confirmed in Phase-1 research):
//      • Pacejka, H.B. (2012) "Tire and Vehicle Dynamics", 3rd ed. — MF6.1 pure & combined slip, self-aligning trail.
//      • Pacejka & Besselink (1997) "Magic Formula Tyre formula with Transient Properties" (Delft Tyre 97).
//      • Besselink et al. (2010), Vehicle Unit Dynamics 48(6) — MF/Swift.

#pragma once

#include <cmath>

namespace Frontier::Vehicle
{

//------------------------------------------------------------------------------------------------------------------------
//                                          PARAMETERS (mirrors GRIT FTireSpecSheet + globals)
//------------------------------------------------------------------------------------------------------------------------
// Field names and default figures match the GRIT authoring sheet (FTireSpecSheet in TireSpecifications.h) so a ported
//    .tyrx / spec reproduces GRIT behaviour exactly. Prepare() derives the same scaled coefficients GRIT precomputes.

struct PacejkaParameters
{
    // Globals
    float Fz0  = 5.0f;   // [kN] reference vertical load
    float Lx   = 1.2f;   // [-]  longitudinal force scale
    float Ly   = 1.3f;   // [-]  lateral force scale
    float Ltr  = 1.0f;   // [-]  aligning-torque scale

    // Load sensitivity: effective μ falls with |dfz|
    float MuLoadSensitivityLong = 0.08f;
    float MuLoadSensitivityLat  = 0.12f;
    float MuLoadMinFactor       = 0.60f;

    // Longitudinal (Fx)
    float pBx1 = 18.0f, pBx2 = 20.0f, pBx3 = 0.50f;
    float pCx1 = 1.55f;
    float pDx1 = 1.6f,  pDx2 = -0.03f, pDx3 = 0.015f;
    float pEx1 = -0.10f, pEx2 = -0.15f, pEx3 = 0.005f, pEx4 = 0.030f;
    float pHx1 = 0.0f,  pHx2 = 0.0f;
    float pVx1 = 0.0f,  pVx2 = 0.0f;

    // Lateral (Fy)
    float pBy1 = 18.0f, pBy2 = 10.0f, pBy3 = 0.10f;
    float pCy1 = 1.45f;
    float pDy1 = 1.85f, pDy2 = -0.06f, pDy3 = 0.10f;
    float pEy1 = -1.2f, pEy2 = -0.50f, pEy3 = 0.20f;
    float pHy1 = 0.0f,  pHy2 = 0.0f;
    float pVy1 = 0.0f,  pVy2 = 0.0f, pVy3 = 0.0f, pVy4 = 0.0f;

    // Camber thrust
    float pCamber1 = 0.12f, pCamber2 = -0.05f;

    // Self-aligning (Mz)
    float qDz1 = 0.12f, qDz2 = -0.008f, qDz3 = 0.05f;
    float qBz1 = 12.0f, qBz2 = -1.2f, qBz3 = 0.8f, qBz5 = 0.5f;
    float qCz1 = 1.2f;
    float qEz1 = -1.5f, qEz2 = 0.8f, qEz3 = 0.0f, qEz4 = 0.2f, qEz5 = -0.5f;

    // Combined slip — longitudinal weighting
    float rBx1 = 12.0f, rBx2 = 8.0f, rBx3 = 0.0f, rCx1 = 1.0f, rEx1 = 0.0f, rEx2 = 0.0f, rHx1 = 0.0f;
    // Combined slip — lateral weighting
    float rBy1 = 5.0f, rBy2 = 4.0f, rBy3 = 0.0f, rBy4 = 0.0f, rCy1 = 0.75f, rEy1 = 0.0f, rEy2 = 0.0f;
    float rHy1 = 0.0f, rHy2 = 0.0f;
    float rVy1 = 0.0f, rVy2 = 0.0f, rVy3 = 0.0f, rVy4 = 0.0f, rVy5 = 1.9f, rVy6 = 0.0f;

    // Transient relaxation lengths (used by TyreSlipDynamics, kept here with the tyre spec)
    float RelaxationLengthLong        = 0.052f;  // [m]
    float RelaxationLengthLat         = 0.048f;  // [m]
    float RelaxationLoadExponentLong  = 0.4f;    // [-]
    float RelaxationLoadExponentLat   = 0.45f;   // [-]

    // MF6.1 inflation-pressure terms (Pacejka 2012, §4.3.2), dpi = (p − NominalPressure) / NominalPressure. Zero keeps the
    //    source's pressure-blind formula; the vehicle passes the live gauge pressure when PressureSensitiveTyre is on.
    float NominalPressure = 110000.0f;   // [Pa]
    float ppx1 = 0.0f, ppx2 = 0.0f;      // [-] longitudinal slip stiffness  × (1 + ppx1 dpi + ppx2 dpi²)
    float ppx3 = 0.0f, ppx4 = 0.0f;      // [-] longitudinal peak friction   × (1 + ppx3 dpi + ppx4 dpi²)
    float ppy1 = 0.0f, ppy2 = 0.0f;      // [-] cornering stiffness          × (1 + ppy1 dpi + ppy2 dpi²)
    float ppy3 = 0.0f, ppy4 = 0.0f;      // [-] lateral peak friction        × (1 + ppy3 dpi + ppy4 dpi²)
    float ppz1 = 0.0f;                    // [-] pneumatic trail              × (1 + ppz1 dpi)

    // 📝 Beyond Project Drive (VehicleDynamicsValidation found both): the sheet's load factor 1 − s |dfz| also penalises loads BELOW
    //    Fz0, so grip rises with load up to 5 kN (real tyres lose μ as load rises); and its combined-slip weighting normalises the
    //    slips by the slip stiffnesses in kN, so G stays ≈ 1 and traction and cornering both reach their peaks at once. The
    //    switches below apply the factor above Fz0 only, and weight combined slip as MF6.1 does (Pacejka 2012, eq. 4.E50–4.E67).
    //    Both off keeps the source exactly.
    bool LoadSensitivityAboveReference = false;
    bool CombinedSlipMf61              = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                          PRECOMPUTED STORE (mirrors GRIT FPACEJKAPRECOMPUTEDCACHE)
//------------------------------------------------------------------------------------------------------------------------

struct PacejkaCoefficients
{
    float Fz0 = 5.0f, InvFz0 = 0.2f, Ly = 1.3f;
    float MuLoadSensitivityLong = 0.08f, MuLoadSensitivityLat = 0.12f, MuLoadMinFactor = 0.60f;

    float Cx_scaled = 0, pDx1_Lx = 0, pDx2_Lx = 0, pDx3 = 0, pBx1_Lx = 0, pBx2_Lx = 0, pBx3 = 0;
    float pEx1_Lx = 0, pEx2_Lx = 0, pEx3_Lx = 0, pEx4 = 0, pHx1_Lx = 0, pHx2_Lx = 0, pVx1_Lx = 0, pVx2_Lx = 0;

    float Cy_scaled = 0, pDy1_Ly = 0, pDy2_Ly = 0, pDy3 = 0, pBy1_Ly = 0, pBy2_Ly = 0, pBy3 = 0;
    float pEy1_Ly = 0, pEy2_Ly = 0, pEy3 = 0, pHy1_Ly = 0, pHy2_Ly = 0, pVy1_Ly = 0, pVy2_Ly = 0, pVy3 = 0, pVy4 = 0;

    float pCamber1_Ly = 0, pCamber2_Ly = 0;

    float rHx1 = 0, rBx1 = 0, rBx2 = 0, rBx3 = 0, rCx1 = 0, rEx1 = 0, rEx2 = 0;
    float rHy1 = 0, rHy2 = 0, rBy1 = 0, rBy2 = 0, rBy3 = 0, rBy4 = 0, rCy1 = 0, rEy1 = 0, rEy2 = 0;
    float rVy1 = 0, rVy2 = 0, rVy3 = 0, rVy4 = 0, rVy5 = 0, rVy6 = 0;

    float qDz1_Ltr = 0, qDz2_Ltr = 0, qDz3 = 0, qBz1 = 0, qBz2 = 0, qBz3 = 0, qBz5 = 0, qCz1 = 0;
    float qEz1 = 0, qEz2 = 0, qEz3 = 0, qEz4 = 0, qEz5 = 0;

    float ppx1 = 0, ppx2 = 0, ppx3 = 0, ppx4 = 0, ppy1 = 0, ppy2 = 0, ppy3 = 0, ppy4 = 0, ppz1 = 0;
    bool  LoadSensitivityAboveReference = false, CombinedSlipMf61 = false;

    bool Initialized = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  RESULT
//------------------------------------------------------------------------------------------------------------------------

struct TyreForces
{
    float Fx = 0.0f;   // [N] longitudinal (drive/brake)
    float Fy = 0.0f;   // [N] lateral (cornering)
    float Mz = 0.0f;   // [N·m] self-aligning torque
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  LAW
//------------------------------------------------------------------------------------------------------------------------

class PacejkaMagicFormula
{
public:
    void AssignParameters(const PacejkaParameters& P) noexcept { Params = P; Prepare(); }
    [[nodiscard]] const PacejkaParameters& QueryParameters() const noexcept { return Params; }
    [[nodiscard]] const PacejkaCoefficients&      QueryCoefficients()      const noexcept { return Prepared; }

    // Pure-slip longitudinal force. kappa [-], camber [rad], Fz [N], dpi [-] inflation-pressure deviation.
    [[nodiscard]] float LongitudinalForce(float SlipRatio, float CamberRad, float Fz_N, float PressureDeviation = 0.0f) const noexcept;
    // Pure-slip lateral force. alpha [rad], camber [rad], Fz [N], dpi [-].
    [[nodiscard]] float LateralForce(float SlipAngleRad, float CamberRad, float Fz_N, float PressureDeviation = 0.0f) const noexcept;
    // Self-aligning torque (needs the already-combined lateral force). Returns N·m.
    [[nodiscard]] float SelfAligningTorque(float SlipAngleRad, float SlipRatio, float LateralForceN,
                                           float CamberRad, float Fz_N, float PressureDeviation = 0.0f) const noexcept;
    // MF6.1 cosine combined-slip weighting applied to pure Fx/Fy. Returns weighted (Fx, Fy).
    void ApplyCombinedSlip(float SlipRatio, float SlipAngleRad, float CamberRad, float Fz_N,
                           float& InOutFx, float& InOutFy, float PressureDeviation = 0.0f) const noexcept;

    // Full combined force+torque (mirrors GRIT ComputeCombinedPacejkaForces).
    [[nodiscard]] TyreForces Combined(float SlipRatio, float SlipAngleRad, float CamberRad, float Fz_N,
                                      float PressureDeviation = 0.0f) const noexcept;

private:
    void Prepare() noexcept;   // build Store from Params (mirrors GRIT FPACEJKAPRECOMPUTEDCACHE::Initialize)

    PacejkaParameters Params;
    PacejkaCoefficients      Prepared;
};

} // namespace Frontier::Vehicle
