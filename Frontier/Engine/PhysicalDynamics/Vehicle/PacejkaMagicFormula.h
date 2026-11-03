/*==========================================================================================================================================
                                                          PACEJKAMAGICFORMULA.H
==========================================================================================================================================*/
// 🧩 Pacejka MF6.1 longitudinal, lateral, combined-slip, and self-aligning torque model ported from GRIT VehicleSolver.

#pragma once

#include "Engine/PhysicalDynamics/Vehicle/VehicleMath.h"

namespace Frontier::Vehicle
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  PACEJKA PARAMETERS
//------------------------------------------------------------------------------------------------------------------------

struct PacejkaParameters
{
    float Fz0 = 4.0f;   // [kN] nominal vertical reference load

    // 📝 Pure longitudinal MF6.1 coefficients (GRIT FPacejkaLongitudinal).
    float pCx1 = 1.65f;     // [-] longitudinal shape factor
    float pDx1 = 1.28f;     // [-] peak longitudinal friction at Fz0
    float pDx2 = -0.08f;    // [-] longitudinal friction load variation
    float pDx3 = 0.05f;     // [1/rad^2] longitudinal camber degradation
    float pEx1 = 0.15f;     // [-] longitudinal curvature at Fz0
    float pEx2 = -0.05f;    // [-] longitudinal curvature linear load term
    float pEx3 = -0.02f;    // [-] longitudinal curvature quadratic load term
    float pEx4 = 0.04f;     // [-] asymmetric drive/brake curvature factor
    float pBx1 = 16.0f;     // [-] longitudinal slip stiffness factor
    float pBx2 = -2.4f;     // [-] longitudinal stiffness load variation
    float pBx3 = 0.12f;     // [1/rad] longitudinal stiffness camber reduction
    float pHx1 = 0.0f;      // [-] longitudinal horizontal shift
    float pHx2 = 0.0f;      // [-] longitudinal horizontal shift load variation
    float pVx1 = 0.0f;      // [-] longitudinal vertical shift
    float pVx2 = 0.0f;      // [-] longitudinal vertical shift load variation
    float Lx   = 1.0f;      // [-] longitudinal grip scaling factor

    // 📝 Pure lateral MF6.1 coefficients (GRIT FPacejkaLateral).
    float pCy1     = 1.35f;     // [-] lateral shape factor
    float pDy1     = 1.24f;     // [-] peak lateral friction at Fz0
    float pDy2     = -0.10f;    // [-] lateral friction load variation
    float pDy3     = 2.10f;     // [1/rad^2] lateral camber friction reduction
    float pEy1     = -0.60f;    // [-] lateral curvature at Fz0
    float pEy2     = -0.12f;    // [-] lateral curvature load variation
    float pEy3     = 0.05f;     // [-] lateral curvature sign asymmetry
    float pBy1     = 14.5f;     // [1/rad] cornering stiffness factor
    float pBy2     = -1.8f;     // [1/rad] cornering stiffness load variation
    float pBy3     = 0.18f;     // [1/rad] cornering stiffness camber factor
    float pHy1     = 0.0f;      // [rad] lateral horizontal shift
    float pHy2     = 0.0f;      // [rad] lateral horizontal shift load variation
    float pVy1     = 0.0f;      // [-] lateral vertical shift
    float pVy2     = 0.0f;      // [-] lateral vertical shift load variation
    float pVy3     = -0.18f;    // [1/rad] camber vertical shift
    float pVy4     = -0.06f;    // [1/rad] camber vertical shift load variation
    float pCamber1 = 1.15f;     // [1/rad] camber thrust stiffness
    float pCamber2 = -0.12f;    // [1/rad] camber thrust load variation
    float Ly       = 1.0f;      // [-] lateral grip scaling factor

    // 📝 Load sensitivity modifiers (GRIT TireSpecifications).
    float MuLoadSensitivityLong = 0.06f;   // [-] high-load longitudinal friction decay
    float MuLoadSensitivityLat  = 0.08f;   // [-] high-load lateral friction decay
    float MuLoadMinFactor       = 0.65f;   // [-] floor on load-sensitivity multiplier

    // 📝 Combined-slip ellipse weighting coefficients (MF6.1 Eq. 4.E45-4.E49).
    float rBx1 = 13.0f;   // [-] Fx combined-slip slope factor
    float rCx1 = 1.02f;   // [-] Fx combined-slip shape factor
    float rBy1 = 10.5f;   // [-] Fy combined-slip slope factor
    float rBy2 = 6.0f;    // [-] Fy combined-slip alpha dependency
    float rBy4 = 0.0f;    // [rad] Fy combined-slip shift
    float rCy1 = 1.01f;   // [-] Fy combined-slip shape factor
    float rVy1 = 0.0f;    // [-] ply-steer combined shift
    float rVy2 = 0.0f;    // [-] ply-steer load term
    float rVy3 = 0.0f;    // [1/rad] ply-steer camber term
    float rVy4 = 12.0f;   // [1/rad] ply-steer alpha decay
    float rVy5 = 1.9f;    // [-] ply-steer kappa slope

    // 📝 Pneumatic trail & self-aligning torque coefficients (GRIT ComputeSelfAligningTorque).
    float qBz1 = 9.5f;     // [1/rad] trail stiffness
    float qBz2 = -1.4f;    // [1/rad] trail stiffness linear load term
    float qBz3 = -0.3f;    // [1/rad] trail stiffness quadratic load term
    float qBz5 = 0.20f;    // [1/rad] trail stiffness camber sensitivity
    float qCz1 = 1.12f;    // [-] trail shape factor
    float qDz1 = 0.011f;   // [m/kN] nominal pneumatic trail per kN
    float qDz2 = -0.002f;  // [m/kN] pneumatic trail load variation
    float qDz3 = 0.15f;    // [1/rad] pneumatic trail camber variation
    float qEz1 = -1.2f;    // [-] trail curvature
    float qEz2 = -0.25f;   // [-] trail curvature linear load term
    float qEz3 = 0.0f;     // [-] trail curvature quadratic load term
    float qEz4 = 0.08f;    // [1/rad] trail curvature camber term
    float qEz5 = -0.4f;    // [1/rad^2] trail curvature quadratic camber term
    float Ltr  = 1.0f;     // [-] pneumatic trail scaling factor

    // 📝 Transient relaxation lengths (MF6.1 Eq. 4.E9).
    float RelaxationLengthLong       = 0.24f;   // [m] longitudinal relaxation distance at Fz0
    float RelaxationLengthLat        = 0.34f;   // [m] lateral relaxation distance at Fz0
    float RelaxationLoadExponentLong = 0.35f;   // [-] load exponent on longitudinal relaxation
    float RelaxationLoadExponentLat  = 0.45f;   // [-] load exponent on lateral relaxation

    [[nodiscard]] static PacejkaParameters DefaultRoad() noexcept;
    [[nodiscard]] static PacejkaParameters GritGt3SlickDry() noexcept;
    [[nodiscard]] static PacejkaParameters GritStreetSuperSticky() noexcept;
    [[nodiscard]] static PacejkaParameters GritOffroadDirt() noexcept;
    [[nodiscard]] static PacejkaParameters GritDriftSemiSlick() noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   PRECOMPUTED CACHE
//------------------------------------------------------------------------------------------------------------------------

struct PacejkaDerivedTable
{
    float Fz0                   = 4.0f;    // [kN] cached reference load
    float InvFz0                = 0.25f;   // [1/kN] reciprocal reference load
    float CxScaled              = 1.65f;   // [-] pCx1 * Lx
    float CyScaled              = 1.35f;   // [-] pCy1 * Ly
    float pDx1Lx                = 1.28f;   // [-] pDx1 * Lx
    float pDx2Lx                = -0.08f;  // [-] pDx2 * Lx
    float pBx1Lx                = 16.0f;   // [-] pBx1 * Lx
    float pBx2Lx                = -2.4f;   // [-] pBx2 * Lx
    float pEx1Lx                = 0.15f;   // [-] pEx1 * Lx
    float pEx2Lx                = -0.05f;  // [-] pEx2 * Lx
    float pEx3Lx                = -0.02f;  // [-] pEx3 * Lx
    float pHx1Lx                = 0.0f;    // [-] pHx1 * Lx
    float pHx2Lx                = 0.0f;    // [-] pHx2 * Lx
    float pVx1Lx                = 0.0f;    // [-] pVx1 * Lx
    float pVx2Lx                = 0.0f;    // [-] pVx2 * Lx
    float pDy1Ly                = 1.24f;   // [-] pDy1 * Ly
    float pDy2Ly                = -0.10f;  // [-] pDy2 * Ly
    float pBy1Ly                = 14.5f;   // [1/rad] pBy1 * Ly
    float pBy2Ly                = -1.8f;   // [1/rad] pBy2 * Ly
    float pEy1Ly                = -0.60f;  // [-] pEy1 * Ly
    float pEy2Ly                = -0.12f;  // [-] pEy2 * Ly
    float pHy1Ly                = 0.0f;    // [rad] pHy1 * Ly
    float pHy2Ly                = 0.0f;    // [rad] pHy2 * Ly
    float pVy1Ly                = 0.0f;    // [-] pVy1 * Ly
    float pVy2Ly                = 0.0f;    // [-] pVy2 * Ly
    float pVy3Ly                = -0.18f;  // [1/rad] pVy3 * Ly
    float pVy4Ly                = -0.06f;  // [1/rad] pVy4 * Ly
    float pCamber1Ly            = 1.15f;   // [1/rad] pCamber1 * Ly
    float pCamber2Ly            = -0.12f;  // [1/rad] pCamber2 * Ly
    float qDz1Ltr               = 0.011f;  // [m/kN] qDz1 * Ltr
    float qDz2Ltr               = -0.002f; // [m/kN] qDz2 * Ltr
    bool  Ready                 = false;   // [-] true once Populate() ran
};

struct CombinedPacejkaResult
{
    float LongitudinalForceNewtons = 0.0f;   // [N] combined in-plane Fx (+forward)
    float LateralForceNewtons      = 0.0f;   // [N] combined in-plane Fy (+left)
    float AligningTorqueNm         = 0.0f;   // [N*m] self-aligning torque Mz about vertical
    float PneumaticTrailMetres     = 0.0f;   // [m] effective pneumatic trail lever arm
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 PACEJKA MAGIC FORMULA
//------------------------------------------------------------------------------------------------------------------------

class PacejkaMagicFormula
{
  public:
    PacejkaMagicFormula() noexcept;
    explicit PacejkaMagicFormula(const PacejkaParameters& Parameters) noexcept;

    void                                   AssignParameters(const PacejkaParameters& Parameters) noexcept;
    [[nodiscard]] const PacejkaParameters& QueryParameters() const noexcept { return Params; }

    [[nodiscard]] float LongitudinalForce(float SlipRatio, float CamberRadians, float NormalLoadNewtons, float GripMultiplier = 1.0f) const noexcept;
    [[nodiscard]] float LateralForce(float SlipAngleRadians, float CamberRadians, float NormalLoadNewtons, float GripMultiplier = 1.0f) const noexcept;
    [[nodiscard]] float SelfAligningTorque(float SlipAngleRadians, float SlipRatio, float LateralForceNewtons, float CamberRadians,
                                           float NormalLoadNewtons, float* OutPneumaticTrailMetres = nullptr) const noexcept;
    [[nodiscard]] CombinedPacejkaResult CombinedForces(float SlipRatio, float SlipAngleRadians, float CamberRadians, float NormalLoadNewtons,
                                                       float GripMultiplier = 1.0f) const noexcept;

  private:
    PacejkaParameters       Params{};
    PacejkaDerivedTable Derived{};
};

} // namespace Frontier::Vehicle
