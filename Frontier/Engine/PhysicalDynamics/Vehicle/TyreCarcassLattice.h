/*==========================================================================================================================================
                                                           TYRECARCASSLATTICE.H
==========================================================================================================================================*/
// 🧩 The XPBD tyre carcass: 28 sectors × 3 rows of tread particles (two shoulders and the crown) tied to the rigid rim by compliant
//    spokes (a ring on an elastic foundation), belt, lateral and shear distance constraints between the particles, and one global
//    volume constraint whose compliance is the enclosed air's bulk compliance V / (γ p), so a flattened patch bulges the sidewalls
//    instead of losing air. Inflation enters as a force: every particle carries η·p·A radially outward, which tensions spokes and
//    belt; pneumatic stiffness is that tension working over the growing patch. Road contact is a positional constraint with
//    Coulomb-capped friction. The load the vehicle feels is the spokes' summed XPBD reaction on the rim (λ ∇C / Δt²), so the lattice
//    carries the corner in place of the spring. Solved in the steered wheel axes: the rim is axisymmetric, so spin does not change
//    the carcass shape. From streamlinkinbox/Frontier arena/5ec53579-frontier (Engine/Physics/TyreCarcassLattice), where it was
//    validated against the VehicleHMI demo's carcass spring; here it is posed on a road plane and sized by the tyre it stands for.

#pragma once

#include "TyreCarcass.h"

#include <array>

namespace Frontier::Vehicle
{

class TyreCarcassLattice final : public TyreCarcass
{
  public:
    static constexpr int SectorCount   = 28;
    static constexpr int RowCount      = 3;
    static constexpr int ParticleCount = SectorCount * RowCount;

    TyreCarcassLattice(const TyreCarcassParameters& Parameters, float Radius, float RimRadius, float Width);

    void            Reset(const CarcassContact& Contact) override;
    CarcassResponse Advance(const CarcassContact& Contact) override;
    void            SampleField(float SpinAngle, CarcassField& Field) const override;
    [[nodiscard]] float               QueryRestVolume() const override { return static_cast<float>(RestVolume); }
    [[nodiscard]] TyreCarcassCategory QueryCategory() const override { return TyreCarcassCategory::Xpbd; }

    // 📝 A particle's position and rest position [m] in the wheel axes (hub at the origin).
    [[nodiscard]] std::array<double, 3> QueryParticle(int Index) const;
    [[nodiscard]] std::array<double, 3> QueryRestParticle(int Index) const;
    [[nodiscard]] double                QueryVolume() const;

    // 📝 Calibrated against TyreCarcassParameters' stiffness law (Exhibits/VehiclePhysics, TyreCarcassValidation).
    double PressureForceFactor = 0.35;    // [-]    η, the share of the gauge pressure the tread particles carry outward
    double SpokeCompliance     = 4.0e-5;  // [m/N]  radial spokes, rim to tread (the elastic foundation)
    double BeltCompliance      = 2.0e-6;  // [m/N]  around the tyre
    double LateralCompliance   = 2.0e-6;  // [m/N]  across the tread
    double ShearCompliance     = 3.0e-6;  // [m/N]  diagonals
    int    Iterations          = 12;      // [-]    XPBD iterations per sub-step

  private:
    struct DistanceConstraint
    {
        int    A          = 0;
        int    B          = -1;     // -1: the rim anchor of A
        int    Category   = 0;      // 0 spoke, 1 belt, 2 lateral, 3 shear (the compliance is read at solve time)
        double Rest       = 0.0;    // [m]
        double Lambda     = 0.0;    // [N·s²]
    };

    void   BuildRestShape();
    double ComputeVolume(const double (*Points)[3], double* Gradients) const;
    double SubStep(const CarcassContact& Contact, double Seconds, double Groundward[3], double& ContactLoad, int& Touching,
                   double PatchSum[3]);
    [[nodiscard]] double ResolveCompliance(int Category) const noexcept;

    TyreCarcassParameters Tuning;
    double Radius = 0.50855, RimRadius = 0.3446, Width = 0.3958;
    double RowOffsets[RowCount]{}, RowRadii[RowCount]{};
    double Positions[ParticleCount][3]{};
    double Predicted[ParticleCount][3]{};
    double Velocities[ParticleCount][3]{};
    double RestPositions[ParticleCount][3]{};
    double RimAnchors[ParticleCount][3]{};
    double InverseMass[ParticleCount]{};
    std::array<DistanceConstraint, ParticleCount * 5> Constraints{};
    int    ConstraintCount = 0;
    double RestVolume      = 0.0;
    double VolumeSign      = 1.0;
    double PreviousDeflection = 0.0;
};

} // namespace Frontier::Vehicle
