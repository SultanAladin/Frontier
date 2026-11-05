/*==========================================================================================================================================
                                                            TYRECARCASSSOLID.H
==========================================================================================================================================*/
// 🧩 The 3D FEM tyre carcass: a ring of 32 × 4 eight-node hexahedra (Hex8) through the carcass thickness, on the VehicleHMI demo's
//    five-point section (bead, shoulder, crown, shoulder, bead) and thicknesses, 320 nodes on two layers, the beads held by the
//    rim. Linear elasticity with full 2 × 2 × 2 Gauss integration gives the material stiffness K_m; the inflation's own stresses
//    σ_p (the carcass solved under its follower pressure) give the initial-stress stiffness K_σ = ∫ ∇N · σ_p · ∇Nᵀ dV, which is
//    what makes a pneumatic tyre stiffer the harder it is pumped up: K(p) = K_m + κ (p / p_ref) K_σ. Road contact is unilateral
//    (Signorini): the tread nodes' gaps to the road plane and the exact contact compliance W = N K⁻¹ Nᵀ form a small linear
//    complementarity problem solved by projected Gauss–Seidel each step, so the contact forces, the patch, the deflected shape and
//    the load all come out of the elastic solution, not out of a prescribed shape. The ring repeats every sector, so K⁻¹ is
//    block-circulant: one sector's columns, at a few pressures, hold the whole compliance (shared by the vehicle's tyres). The load
//    the vehicle feels is the sum of the contact forces plus the carcass damping on the deflection rate. New (2026-11-04): the demo's
//    own Hex8 solid prescribes its deformation and returns the spring's force.

#pragma once

#include "TyreCarcass.h"

#include <array>
#include <memory>
#include <vector>

namespace Frontier::Vehicle
{

// 📝 The compliance tables of one carcass design (geometry, material, calibration), built once and shared by every tyre of it.
struct TyreCarcassCompliance;

class TyreCarcassSolid final : public TyreCarcass
{
  public:
    static constexpr int SectorCount = 32;
    static constexpr int RingCount   = 5;
    static constexpr int LayerCount  = 2;   // 0 inner (air side), 1 outer (road side)
    static constexpr int NodeCount   = SectorCount * RingCount * LayerCount;
    static constexpr int CellCount   = SectorCount * (RingCount - 1);
    static constexpr int TreadRings  = RingCount - 2;            // the outer rings that meet the road (shoulder, crown, shoulder)
    static constexpr int TreadCount  = SectorCount * TreadRings;

    // 📝 Material and calibration (Exhibits/VehiclePhysics, TyreCarcassValidation fits them to TyreCarcassParameters' law).
    struct Material
    {
        double YoungModulus   = 3.3e6;   // [Pa] rubber and cord plies, smeared over the carcass thickness
        double PoissonRatio   = 0.45;    // [-]
        double PrestressScale = 9.5;     // [-] κ, scales the inflation's initial-stress stiffness
        double CordModulus    = 400.0e6; // [Pa] the plies' tensile modulus, which sets how much the tyre grows when pumped up
        double DampingRate    = 1500.0;  // [N·s/m] carcass damping on the deflection rate (as the spring's)
    };

    TyreCarcassSolid(const TyreCarcassParameters& Parameters, float Radius, float RimRadius, float Width);
    TyreCarcassSolid(const TyreCarcassParameters& Parameters, float Radius, float RimRadius, float Width, const Material& Design);

    void            Reset(const CarcassContact& Contact) override;
    CarcassResponse Advance(const CarcassContact& Contact) override;
    void            SampleField(float SpinAngle, CarcassField& Field) const override;
    [[nodiscard]] float               QueryRestVolume() const override { return static_cast<float>(RestAirVolume); }
    [[nodiscard]] TyreCarcassCategory QueryCategory() const override { return TyreCarcassCategory::Fem; }

    [[nodiscard]] static constexpr int NodeIndex(int Sector, int Ring, int Layer) noexcept
    {
        return ((((Sector % SectorCount) + SectorCount) % SectorCount) * RingCount + Ring) * LayerCount + Layer;
    }
    // 📝 A tread node's current and rest position [m] (wheel axes), its contact force [N] in the last step.
    [[nodiscard]] std::array<double, 3> QueryTreadNode(int Sector, int TreadRing) const;
    [[nodiscard]] std::array<double, 3> QueryRestTreadNode(int Sector, int TreadRing) const;
    [[nodiscard]] double                QueryTreadForce(int Sector, int TreadRing) const;
    [[nodiscard]] double                QueryAirVolume() const noexcept { return AirVolume; }
    [[nodiscard]] bool                  QueryReady() const noexcept { return static_cast<bool>(Tables); }

  private:
    double SolveContact(const CarcassContact& Contact, int& Touching, double PatchSum[3]);

    TyreCarcassParameters                        Tuning;
    Material                                     Design;
    double                                       Radius = 0.50855, RimRadius = 0.3446, Width = 0.3958;
    std::shared_ptr<const TyreCarcassCompliance> Tables;
    std::array<double, TreadCount * 3>           Displacement{};   // [m] tread displacement from rest, wheel axes
    std::array<double, TreadCount>               TreadForce{};     // [N] contact force on each tread node
    double                                       RestAirVolume = 0.0, AirVolume = 0.0;
    double                                       PreviousDeflection = 0.0;
};

} // namespace Frontier::Vehicle
