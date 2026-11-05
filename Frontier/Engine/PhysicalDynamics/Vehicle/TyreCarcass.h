/*==========================================================================================================================================
                                                               TYRECARCASS.H
==========================================================================================================================================*/
// 🧩 The soft body under the tread, one seam for three formulations. Spring is Project Drive's radial spring on the contact fan
//    (TraceContactTyre.h, no carcass object). XPBD is a particle lattice whose constraint reactions carry the load
//    (TyreCarcassLattice). FEM is a hexahedral Neo-Hookean carcass whose bead reactions carry it (TyreCarcassSolid). Each soft
//    formulation is posed on the hub every step, meets the road plane the contact fan found, and answers the normal load, its
//    deflection and the air volume it encloses (so the gas pressure follows deformation as well as temperature). Each also lays its
//    displacements out on the renderer's 32 × 5 nodal field (VehicleVisual.slang, VehicleProfile::CarcassDisplacements).

#pragma once

#include "VehicleMath.h"

#include <array>
#include <cstdint>
#include <memory>

namespace Frontier::Vehicle
{

enum class TyreCarcassCategory : int
{
    Spring,   // the contact fan's radial spring and damper (Project Drive)
    Xpbd,     // XPBD particle lattice
    Fem,      // hexahedral (Hex8) Neo-Hookean finite elements
};

[[nodiscard]] constexpr const char* TyreCarcassName(TyreCarcassCategory Category) noexcept
{
    switch (Category)
    {
        case TyreCarcassCategory::Spring: return "Spring";
        case TyreCarcassCategory::Xpbd:   return "XPBD";
        case TyreCarcassCategory::Fem:    return "3D FEM";
    }
    return "Spring";
}

// 📝 The renderer's field: 32 sectors around the tyre × 5 rings across it (inner bead, inner shoulder, crown, outer shoulder,
//    outer bead), displacements in the wheel's spinning (material) axes: x forward, y along the axle, z up at spin angle 0.
inline constexpr uint32_t CarcassSectorCount = 32;
inline constexpr uint32_t CarcassRingCount   = 5;
inline constexpr uint32_t CarcassFieldPoints = CarcassSectorCount * CarcassRingCount;
using CarcassField                           = std::array<float, CarcassFieldPoints * 3>;

// 📝 Tuning the soft formulations share, plus lattice (XPBD) and hexahedral continuum (3D FEM) parameters.
struct TyreCarcassParameters
{
    double ReferencePressure       = 110000.0;  // [Pa]    gauge pressure the calibration is quoted at (TraceContact's reference)
    double ReferenceStiffness      = 250000.0;  // [N/m]   radial stiffness at ReferencePressure the soft formulations calibrate to
    double PressureShare           = 0.75;      // [-]     fraction of the stiffness the air carries (as TraceContactParameters)
    double TreadMass               = 9.0;       // [kg]    tread and carcass mass that moves (the rest of the 12 kg sits on the rim)
    double DampingPerSecond        = 24.0;      // [1/s]   carcass hysteresis, a velocity damping in the wheel axes
    double Friction                = 1.0;       // [-]     road friction cap on the contact correction (tangential ÷ normal)
    int    Substeps                = 1;         // [-]     lattice sub-steps per vehicle step (the FEM solves each step directly)
    double XpbdPressureForceFactor = 0.35;      // [-]     share of gauge pressure carried radially outward by XPBD tread particles
    double XpbdSpokeCompliance     = 4.0e-5;    // [m/N]   XPBD radial spoke compliance (rim to tread)
    double XpbdBeltCompliance      = 2.0e-6;    // [m/N]   XPBD circumferential belt compliance
    double XpbdLateralCompliance   = 2.0e-6;    // [m/N]   XPBD cross-tread lateral compliance
    double XpbdShearCompliance     = 3.0e-6;    // [m/N]   XPBD diagonal shear compliance
    int    XpbdIterations          = 12;        // [-]     XPBD constraint sweeps per sub-step
    double FemYoungModulus         = 3.3e6;     // [Pa]    3D Hex8 FEM smeared rubber + ply Young's modulus
    double FemPoissonRatio         = 0.45;      // [-]     3D Hex8 FEM Poisson ratio
    double FemPrestressScale       = 9.5;       // [-]     3D Hex8 FEM follower-pressure geometric stiffness scale
    double FemCordModulus          = 400.0e6;   // [Pa]    3D Hex8 FEM cord tensile modulus governing inflation growth
    double FemDampingRate          = 1500.0;    // [N·s/m] 3D Hex8 FEM radial deflection-rate damping
};

// 📝 One step's boundary conditions. HubRotation carries steer and camber, never spin (the carcass is axisymmetric about the
//    axle, so spin does not change its shape; rolling enters through the contact slip velocity).
struct CarcassContact
{
    Vec3  HubPosition{};                  // [m]
    Quat  HubRotation{};                  // [-]
    Vec3  HubVelocity{};                  // [m/s]
    float SpinRate      = 0.0f;           // [rad/s]
    Vec3  GroundPoint{};                  // [m] a point of the road plane under the hub
    Vec3  GroundNormal{0.0f, 0.0f, 1.0f}; // [-]
    bool  Ground        = false;          // [-] a road plane was found
    float GaugePressure = 110000.0f;      // [Pa] the live (gas-law) pressure
    Vec3  TangentialForce{};              // [N] planar road-on-tyre traction (Fx forward + Fy lateral) in world axes
    float Seconds       = 1.0f / 240.0f;  // [s]
};

struct CarcassResponse
{
    float    NormalLoad     = 0.0f;   // [N] road on tyre along GroundNormal
    float    Deflection     = 0.0f;   // [m] radial compression at the contact
    float    Rate           = 0.0f;   // [m/s] compression rate (+ compressing)
    Vec3     PatchCentre{};           // [m] centroid of the touching tread, world
    uint32_t ContactCount   = 0;      // [-] tread points touching the road
    float    EnclosedVolume = 0.0f;   // [m³] air enclosed by carcass and rim
};

class TyreCarcass
{
  public:
    virtual ~TyreCarcass() = default;

    // 📝 Undeformed, at rest, posed on the hub.
    virtual void Reset(const CarcassContact& Contact) = 0;
    // 📝 One vehicle step (the model sub-steps itself).
    virtual CarcassResponse Advance(const CarcassContact& Contact) = 0;
    // 📝 Displacements from rest on the renderer's field, turned into the material axes of a wheel spun by SpinAngle.
    virtual void SampleField(float SpinAngle, CarcassField& Field) const = 0;
    [[nodiscard]] virtual float QueryRestVolume() const = 0;
    [[nodiscard]] virtual TyreCarcassCategory QueryCategory() const = 0;
};

// 📝 Radial stiffness the soft models aim for at a gauge pressure (the same law as TraceContactTyre's PressureStiffness).
[[nodiscard]] inline double ResolveCarcassStiffness(const TyreCarcassParameters& Parameters, double GaugePressure) noexcept
{
    const double Reference = Parameters.ReferencePressure > 1.0 ? Parameters.ReferencePressure : 1.0;
    const double Ratio     = GaugePressure >= 0.0 ? GaugePressure / Reference : 1.0;
    const double Share     = Parameters.PressureShare < 0.0 ? 0.0 : (Parameters.PressureShare > 1.0 ? 1.0 : Parameters.PressureShare);
    return Parameters.ReferenceStiffness * ((1.0 - Share) + Share * (Ratio < 4.0 ? Ratio : 4.0));
}

// 📝 XPBD and FEM carcasses for a tyre of Radius, RimRadius and Width; Spring answers null (the contact fan carries it).
[[nodiscard]] std::unique_ptr<TyreCarcass> ConstructTyreCarcass(TyreCarcassCategory Category, const TyreCarcassParameters& Parameters,
                                                                float Radius, float RimRadius, float Width);

} // namespace Frontier::Vehicle
