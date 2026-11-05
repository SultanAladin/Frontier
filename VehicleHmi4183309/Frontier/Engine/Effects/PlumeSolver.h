#pragma once
/*==========================================================================================================================================
                                                          PLUMESOLVER.H
==========================================================================================================================================*/
// 🧩 Visual effects only: CPU mirror of the HTML fluid/gas solver, ported 1:1 from the WGSL at 4183309 —
//    PlumeAdvection.wgsl (semi-Lagrangian velocity + thermo advection, saturated buoyancy),
//    PlumeVorticity.wgsl (curl + confinement), PlumePressure.wgsl (divergence, warm-started Jacobi with the
//    h² factor, gradient projection), driven by PlumeSpace.js `EncodeRecord` retention factors.
//    Grades/defaults are PlumeSpace.js verbatim. Boundary rules follow PlumePressure.wgsl: solid neighbour →
//    centre pressure (Neumann), outside sideways/above → 0 (Dirichlet), below floor → Neumann.
//    Presentations/tools read this; physics and session never do.

#include "Frontier/Engine/Effects/GasParameters.h"
#include <array>
#include <vector>

namespace Frontier
{
struct PlumeGrade { const char* Name; int Extent[3]; float CellExtent; int PressureSweeps; int MarchSamples; };
// 📝 PlumeGrades (PlumeSpace.js): Low / Medium / High / Ultra.
extern const std::array<PlumeGrade, 4> PlumeGrades;

struct PlumeSource
{
    float Position[3] = {0, 0, 0};
    float Velocity[3] = {0, 0, 1};
    float Radius = 0.3f;
    float SmokeRate = 0.0f, HeatRate = 0.0f, FlameRate = 0.0f, SootRate = 0.0f; // PlumeSourceProjection rates
};

class PlumeSolver
{
public:
    // Velocity lattice is half the thermo extent per axis (PlumeSpace.AssignConfiguration); coarse edge = 2× cell.
    explicit PlumeSolver(const PlumeGrade& Grade = PlumeGrades[2]);
    void Resize(const PlumeGrade& Grade);
    void Clear() noexcept;
    // One fixed step. Mirrors RecordTransport order: advect velocity → curl → confine → divergence →
    // Jacobi sweeps (warm-started, pressure kept between steps) → project → advect thermo.
    void Step(float StepSeconds, const PlumeSettings& Settings, const std::vector<PlumeSource>& Sources, int PressureSweeps);
    void AddSource(const PlumeSource& Source, float StepSeconds, const PlumeSettings& Settings);

    [[nodiscard]] float QueryDensity(int X, int Y, int Z) const noexcept;
    [[nodiscard]] float QueryHeat(int X, int Y, int Z) const noexcept;
    [[nodiscard]] float QueryMaximumDensity() const noexcept;
    [[nodiscard]] float QueryMaximumSpeed() const noexcept;
    [[nodiscard]] float QueryDivergenceMagnitude() const noexcept; // post-projection residual (solver health)
    [[nodiscard]] bool IsFinite() const noexcept;
    int StepsTaken = 0;
    int ThermoExtent[3] = {0, 0, 0}, VelocityExtent[3] = {0, 0, 0};
    float CellExtent = 0.25f, CoarseEdge = 0.5f;
private:
    std::vector<float> Vel_, VelNext_, Thermo_, ThermoNext_, Pressure_, PressureNext_, Divergence_, Curl_;
    [[nodiscard]] int VIndex(int X, int Y, int Z) const noexcept { return (Z * VelocityExtent[1] + Y) * VelocityExtent[0] + X; }
    [[nodiscard]] int TIndex(int X, int Y, int Z) const noexcept { return (Z * ThermoExtent[1] + Y) * ThermoExtent[0] + X; }
    float SampleVel(int C, float X, float Y, float Z) const noexcept;   // trilinear, clamped (LatticeSampler)
    float SampleThermo(int C, float X, float Y, float Z) const noexcept;
    float NeighbourPressure(int X, int Y, int Z, float Centre) const noexcept;
};
} // namespace Frontier
