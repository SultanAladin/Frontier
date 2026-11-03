/*==========================================================================================================================================
                                                              TYRESOFTBODY.H
==========================================================================================================================================*/
// 🧩 Unified soft-body tyre physics engine supporting Spring, 32x5 XPBD compliant carcass, and 128-Hex8 3D Neo-Hookean FEM formulations.

#pragma once

#include "Engine/PhysicalDynamics/Vehicle/VehicleMath.h"

#include <array>
#include <cstdint>

namespace Frontier::Vehicle
{

//------------------------------------------------------------------------------------------------------------------------
//                                               TYRE SOFT-BODY FORMULATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class TyreFormulation : uint8_t
{
    Spring = 0,   // [-] pressure-stiffened radial carcass spring + analytical volumetric bulge (VehicleVisual Path B)
    Xpbd   = 1,   // [-] 32x5 (160-node, 352-constraint) XPBD compliant-distance carcass (VehicleVisual Path A)
    Fem3D  = 2    // [-] 32x4 (128-Hex8, 320-node) 3D Neo-Hookean hyperelastic solid carcass (VehicleVisual Path A)
};

[[nodiscard]] inline const char* DescribeTyreFormulation(TyreFormulation Formulation) noexcept
{
    switch (Formulation)
    {
        case TyreFormulation::Spring: return "Spring";
        case TyreFormulation::Xpbd:   return "XPBD";
        case TyreFormulation::Fem3D:  return "3D FEM";
    }
    return "Spring";
}

//------------------------------------------------------------------------------------------------------------------------
//                                              CARCASS LATTICE DIMENSIONS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t TyreSectorCount    = 32u;    // [-] circumferential divisions around 2*pi
inline constexpr uint32_t TyreRingCount      = 5u;     // [-] meridional rings: 0=inner bead, 1=inner shoulder, 2=crown, 3=outer shoulder, 4=outer bead
inline constexpr uint32_t TyreNodesPerWheel  = 160u;   // [-] TyreSectorCount * TyreRingCount
inline constexpr uint32_t TyreHexCount       = 128u;   // [-] 32 sectors * 4 meridional bands of Hex8 solid cells

struct TyreSoftBodyParameters
{
    float OuterRadiusMetres        = 0.50855f;    // [m] unloaded crown radius (matches VehicleVisual.slang)
    float ShoulderRadiusMetres     = 0.49200f;    // [m] unloaded shoulder ring radius (m = 1, 3)
    float RimRadiusMetres          = 0.34480f;    // [m] rigid bead/rim radius (m = 0, 4)
    float CarcassHalfWidthMetres   = 0.11800f;    // [m] visual/nodal carcass half-width
    float ContactTreadWidthMetres  = 0.39580f;    // [m] ground-trace tread width
    float ReferenceVolumeM3        = 0.04200f;    // [m^3] unloaded enclosed pneumatic volume
    float ReferencePressurePa      = 220000.0f;   // [Pa] nominal 2.20 bar cold gauge pressure

    // 📝 Contact trace fan parameters (GRIT ComputeSuspensionDisplacements).
    float    FanHalfAngleRadians   = 0.7854f;     // [rad] +-45 deg contact fan in wheel plane
    float    FanStepRadians        = 0.0873f;     // [rad] 5 deg fan ray angular spacing
    uint32_t LateralSliceCount     = 3u;          // [-] lateral slices across tread width

    // 📝 Spring formulation stiffness & carcass damping.
    float VerticalStiffnessNpm     = 255000.0f;   // [N/m] radial vertical stiffness at ReferencePressurePa
    float PressureStiffnessShare   = 0.72f;       // [-] fraction of radial stiffness carried by inflation gas
    float VerticalDampingNsm       = 1650.0f;     // [N*s/m] radial sidewall hysteresis damping
    float LateralBeltStiffnessNpm  = 155000.0f;   // [N/m] lateral carcass belt shear stiffness
    float MaxDeflectionMetres      = 0.135f;      // [m] rim-bottoming radial compression limit

    // 📝 XPBD formulation compliances & substepping.
    uint32_t XpbdSubsteps          = 6u;          // [-] XPBD substeps per vehicle physics step
    uint32_t XpbdIterations        = 8u;          // [-] Gauss-Seidel constraint sweeps per substep
    float    XpbdCrownCompliance   = 1.5e-5f;     // [1/N] crown hoop constraint compliance
    float    XpbdShoulderCompliance= 3.5e-5f;     // [1/N] shoulder hoop constraint compliance
    float    XpbdSidewallCompliance= 2.2e-5f;     // [1/N] meridional sidewall constraint compliance
    float    XpbdShearCompliance   = 5.5e-5f;     // [1/N] diagonal belt/sidewall shear compliance
    float    XpbdNodeMassKg        = 0.085f;      // [kg] mobile carcass node lumped mass

    // 📝 3D Hex8 Neo-Hookean FEM homogenized pneumatic-carcass continuum parameters (CFL-stable at 240 Hz x 12 substeps).
    uint32_t FemSubsteps           = 12u;         // [-] explicit hyperelastic substeps per vehicle step
    float    FemBaseYoungsModulusPa= 6.8e5f;      // [Pa] homogenized shell-cavity Young's modulus at ReferencePressurePa
    float    FemPoissonRatio       = 0.42f;       // [-] nearly incompressible elastomer Poisson ratio (drives sidewall bulge)
    float    FemNodeMassKg         = 0.120f;      // [kg] outer-surface lumped nodal mass (~11.5 kg mobile carcass / 96 nodes)
    float    FemDampingPerSecond   = 48.0f;       // [1/s] Rayleigh mass-proportional elastomer damping
};

struct TyreSoftBodyInputs
{
    Vec3        HubPositionWorld        {0.0f, 0.0f, 0.50855f};   // [m] wheel hub centre in world space
    Quat        HubOrientationWorld     = Quat::Identity();       // [-] chassis * steer * camber orientation (no spin)
    Vec3        HubVelocityWorld        {0.0f, 0.0f, 0.0f};       // [m/s] hub world linear velocity
    float       SpinAngleRadians        = 0.0f;                   // [rad] cumulative wheel rotation about +Y axle
    float       WheelOmegaRadPerSec     = 0.0f;                   // [rad/s] wheel angular velocity
    float       GaugePressurePa         = 220000.0f;              // [Pa] live inflation gauge pressure from ThermalIntegrator
    float       LateralForceEstimateN   = 0.0f;                   // [N] previous/current lateral road force shearing the belt
    float       LongitudinalForceEstN   = 0.0f;                   // [N] previous/current longitudinal road force twisting belt
    float       DeltaSeconds            = 1.0f / 240.0f;          // [s] step duration
    GroundQuery Ground                  {};                       // [-] world ground height/surface callback
};

struct TyreSoftBodyReading
{
    TyreFormulation Formulation             = TyreFormulation::Spring;   // [-] active soft-body backend
    bool            InContact               = false;                     // [-] true when contact fan hit ground
    uint32_t        ContactHitCount         = 0u;                        // [-] rays/nodes in ground contact
    Vec3            PatchCentreWorld        {0.0f, 0.0f, 0.0f};          // [m] averaged contact patch centre
    Vec3            ContactNormalWorld      {0.0f, 0.0f, 1.0f};          // [-] unit outward road normal
    Vec3            NormalForceWorld        {0.0f, 0.0f, 0.0f};          // [N] normal support reaction Fz * N
    float           NormalLoadNewtons       = 0.0f;                      // [N] scalar vertical/normal tyre load Fz
    float           DeflectionMetres        = 0.0f;                      // [m] radial tyre compression at contact patch
    float           DeflectionRateMps       = 0.0f;                      // [m/s] radial compression velocity (+compressing)
    float           LoadedRadiusMetres      = 0.50855f;                  // [m] R_outer - Deflection
    float           ContactAngleRadians     = -1.5707963f;               // [rad] contact patch angle in spun wheel-local frame
    float           LateralDistortionMetres = 0.0f;                      // [m] lateral tread-band shear walk
    float           MaxSidewallBulgeMetres  = 0.0f;                      // [m] peak outward shoulder lateral bulge |u_y|
    float           MaxRadialInwardMetres   = 0.0f;                      // [m] peak inward crown radial displacement
    float           EnclosedVolumeM3        = 0.04200f;                  // [m^3] live deformed enclosed cavity volume
    float           EffectiveStiffnessNpm   = 255000.0f;                 // [N/m] effective vertical rate Fz / max(Deflection, eps)
    float           MeanJacobian            = 1.0f;                      // [-] mean(det(F)) across 128 Hex8 cells (Fem3D)
    float           MinJacobian             = 1.0f;                      // [-] min(det(F)) across 128 Hex8 cells (Fem3D)
    float           MeanPiolaStressPa       = 0.0f;                      // [Pa] mean(||P||_F) first Piola-Kirchhoff norm (Fem3D)
    bool            RimBottomed             = false;                     // [-] true when deflection reaches rim pinch limit
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 TYRE SOFT-BODY SOLVER
//------------------------------------------------------------------------------------------------------------------------

class TyreSoftBody
{
  public:
    TyreSoftBody() noexcept;
    explicit TyreSoftBody(TyreFormulation InitialFormulation, const TyreSoftBodyParameters& Parameters = {}) noexcept;

    void Configure(TyreFormulation RequestedFormulation, const TyreSoftBodyParameters& Parameters = {}) noexcept;
    void Reset() noexcept;

    [[nodiscard]] TyreFormulation               QueryFormulation() const noexcept { return ActiveFormulation; }
    [[nodiscard]] const TyreSoftBodyParameters& QueryParameters() const noexcept { return Params; }
    [[nodiscard]] const TyreSoftBodyReading&    QueryReading() const noexcept { return LastReading; }

    TyreSoftBodyReading Step(const TyreSoftBodyInputs& Inputs) noexcept;

    // 📝 Wheel-local rest and deformed carcass positions on the 32x5 grid (valid for all 3 formulations!).
    [[nodiscard]] Vec3 QueryRestNodeLocal(uint32_t Sector, uint32_t Ring) const noexcept;
    [[nodiscard]] Vec3 QueryDisplacementLocal(uint32_t Sector, uint32_t Ring) const noexcept;
    [[nodiscard]] Vec3 QueryDeformedNodeLocal(uint32_t Sector, uint32_t Ring) const noexcept
    {
        return QueryRestNodeLocal(Sector, Ring) + QueryDisplacementLocal(Sector, Ring);
    }

    // 📝 Writes the 160 float4 wheel-local nodal displacements u = x - X into a contiguous [160 * 4] float slice.
    void WriteCarcassDisplacements(float* DestinationFloat4x160) const noexcept;

  private:
    struct XpbdConstraint
    {
        uint16_t NodeA      = 0u;
        uint16_t NodeB      = 0u;
        float    RestLength = 0.0f;
        float    Compliance = 1.0e-5f;
    };

    struct HexCellReference
    {
        std::array<uint16_t, 8> Nodes{};           // [-] 0..3 inner rim quad, 4..7 outer mobile quad (0..319)
        std::array<Vec3, 8>     ShapeGradients{};  // [1/m] reference centroid dN_a / dX
        float                   RestVolume = 0.0f; // [m^3] reference element volume V_0e
    };

    void BuildReferenceGeometry() noexcept;
    void EvaluateAnalyticalSpringField(float ContactAngle, float Deflection, float GaugePressure, float LateralWalk) noexcept;
    void StepXpbdCarcass(const Vec3& DownLocal, float Penetration, float GaugePressure, float LateralForceN,
                         float LongitudinalForceN, float WheelOmega, float Dt) noexcept;
    void StepFem3DCarcass(const Vec3& DownLocal, float Penetration, float GaugePressure, float LateralForceN,
                          float LongitudinalForceN, float WheelOmega, float Dt) noexcept;
    [[nodiscard]] float ComputeEnclosedTorusVolume() const noexcept;

    TyreFormulation                        ActiveFormulation = TyreFormulation::Spring;
    TyreSoftBodyParameters                 Params{};
    TyreSoftBodyReading                    LastReading{};

    std::array<Vec3, TyreNodesPerWheel>    RestOuterNodes{};        // [m] 160 outer carcass rest positions
    std::array<Vec3, TyreNodesPerWheel>    RestInnerNodes{};        // [m] 160 inner rim-seat rest positions
    std::array<Vec3, TyreNodesPerWheel>    CurrentOuterNodes{};     // [m] 160 live outer carcass positions
    std::array<Vec3, TyreNodesPerWheel>    OuterVelocities{};       // [m/s] 160 live outer carcass velocities
    std::array<Vec3, TyreNodesPerWheel>    NodalDisplacements{};    // [m] u = CurrentOuterNodes - RestOuterNodes

    std::vector<XpbdConstraint>            XpbdConstraints{};       // [-] 352 compliant distance constraints
    std::array<HexCellReference, TyreHexCount> HexCells{};          // [-] 128 Hex8 reference finite elements
    float                                  ReferencePolygonAreaSum = 1.0f;   // [m^2] rest cross-section sum for volume scaling
    float                                  SolvedCarcassLoadN      = 0.0f;   // [N] reaction solved by XPBD / Fem3D carcass
};

} // namespace Frontier::Vehicle
