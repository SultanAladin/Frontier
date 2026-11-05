/*==========================================================================================================================================
                                                           TYRECARCASSSOLID.CPP
==========================================================================================================================================*/
// 🧩 The Hex8 carcass: rest geometry, material and initial-stress stiffness, the inflation solve, the block-circulant compliance
//    tables at five pressures, and each step's unilateral road contact (projected Gauss–Seidel on the exact contact compliance).
//    Wheel axes: hub at the origin, x forward, y along the axle, z up.

#include "TyreCarcassSolid.h"

#include "TyreCarcassLattice.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <tuple>

namespace Frontier::Vehicle
{

namespace
{

constexpr double Pi = 3.14159265358979323846;

// 📝 The VehicleHMI demo's carcass section for its 0.50855 m tyre on a 0.3448 m rim (TyreContinuum's Profile): radius, lateral
//    position and thickness of the bead, shoulder, crown, shoulder and bead rings.
struct SectionPoint
{
    double Radius, Lateral, Thickness;
};
constexpr SectionPoint DemoSection[5] = { { 0.34480, -0.115, 0.018 }, { 0.48650, -0.118, 0.016 }, { 0.50855, 0.000, 0.024 },
                                          { 0.48650, 0.118, 0.016 },  { 0.34480, 0.115, 0.018 } };
constexpr double DemoRadius = 0.50855, DemoRim = 0.34480, DemoWidth = 0.3958;

// 📝 Natural corner coordinates: ξ around the tyre, η across it, ζ through the carcass (inner → outer).
constexpr int Corner[8][3] = { { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 }, { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } };

constexpr int    LevelCount = 5;                                   // pressures 0, ½, 1, 1½, 2 × the reference
constexpr int    Mobile     = TyreCarcassSolid::SectorCount * 3 * 2;   // shoulder, crown, shoulder rings on both layers
constexpr int    Freedom    = Mobile * 3;                          // [-] degrees of freedom
constexpr double Margin     = 0.03;                                // [m] tread nodes within this gap join the contact problem

int MobileIndex(int Sector, int Ring, int Layer)
{
    return ((((Sector % TyreCarcassSolid::SectorCount) + TyreCarcassSolid::SectorCount) % TyreCarcassSolid::SectorCount) * 3 + (Ring - 1)) * 2 + Layer;
}

void Turn(const Quat& Q, const double V[3], double Out[3])
{
    const double X = Q.x, Y = Q.y, Z = Q.z, W = Q.w;
    const double Tx = 2.0 * (Y * V[2] - Z * V[1]), Ty = 2.0 * (Z * V[0] - X * V[2]), Tz = 2.0 * (X * V[1] - Y * V[0]);
    Out[0] = V[0] + W * Tx + (Y * Tz - Z * Ty);
    Out[1] = V[1] + W * Ty + (Z * Tx - X * Tz);
    Out[2] = V[2] + W * Tz + (X * Ty - Y * Tx);
}

void TurnBack(const Quat& Q, const double V[3], double Out[3])
{
    Turn(Quat{ -Q.x, -Q.y, -Q.z, Q.w }, V, Out);
}

// 📝 In-place dense Cholesky (lower triangle, row-major n × n); false when a pivot is not positive.
bool FactorCholesky(std::vector<double>& A, int N)
{
    for (int J = 0; J < N; ++J)
    {
        double Diagonal = A[static_cast<size_t>(J) * N + J];
        for (int K = 0; K < J; ++K) Diagonal -= A[static_cast<size_t>(J) * N + K] * A[static_cast<size_t>(J) * N + K];
        if (!(Diagonal > 0.0)) return false;
        const double Root = std::sqrt(Diagonal);
        A[static_cast<size_t>(J) * N + J] = Root;
        for (int I = J + 1; I < N; ++I)
        {
            double Sum = A[static_cast<size_t>(I) * N + J];
            const double* RowI = &A[static_cast<size_t>(I) * N];
            const double* RowJ = &A[static_cast<size_t>(J) * N];
            for (int K = 0; K < J; ++K) Sum -= RowI[K] * RowJ[K];
            A[static_cast<size_t>(I) * N + J] = Sum / Root;
        }
    }
    return true;
}

void SolveCholesky(const std::vector<double>& L, int N, std::vector<double>& B)
{
    for (int I = 0; I < N; ++I)
    {
        double Sum = B[static_cast<size_t>(I)];
        for (int K = 0; K < I; ++K) Sum -= L[static_cast<size_t>(I) * N + K] * B[static_cast<size_t>(K)];
        B[static_cast<size_t>(I)] = Sum / L[static_cast<size_t>(I) * N + I];
    }
    for (int I = N - 1; I >= 0; --I)
    {
        double Sum = B[static_cast<size_t>(I)];
        for (int K = I + 1; K < N; ++K) Sum -= L[static_cast<size_t>(K) * N + I] * B[static_cast<size_t>(K)];
        B[static_cast<size_t>(I)] = Sum / L[static_cast<size_t>(I) * N + I];
    }
}

// 📝 A symmetric 3 × 3 tensor's positive principal part (cyclic Jacobi rotations, then Σ max(0, σᵢ) vᵢ vᵢᵀ).
void KeepTension(double S[3][3])
{
    double A[3][3], V[3][3] = { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
    for (int Row = 0; Row < 3; ++Row)
        for (int Column = 0; Column < 3; ++Column) A[Row][Column] = S[Row][Column];
    for (int Sweep = 0; Sweep < 24; ++Sweep)
    {
        const double Off = A[0][1] * A[0][1] + A[0][2] * A[0][2] + A[1][2] * A[1][2];
        if (Off < 1.0e-24 * (A[0][0] * A[0][0] + A[1][1] * A[1][1] + A[2][2] * A[2][2] + 1.0e-300)) break;
        for (int P = 0; P < 2; ++P)
            for (int Q = P + 1; Q < 3; ++Q)
            {
                if (std::fabs(A[P][Q]) < 1.0e-300) continue;
                const double Theta = 0.5 * std::atan2(2.0 * A[P][Q], A[Q][Q] - A[P][P]);
                const double C = std::cos(Theta), Sn = std::sin(Theta);
                for (int K = 0; K < 3; ++K)
                {
                    const double Akp = A[K][P], Akq = A[K][Q];
                    A[K][P] = C * Akp - Sn * Akq;
                    A[K][Q] = Sn * Akp + C * Akq;
                }
                for (int K = 0; K < 3; ++K)
                {
                    const double Apk = A[P][K], Aqk = A[Q][K];
                    A[P][K] = C * Apk - Sn * Aqk;
                    A[Q][K] = Sn * Apk + C * Aqk;
                }
                for (int K = 0; K < 3; ++K)
                {
                    const double Vkp = V[K][P], Vkq = V[K][Q];
                    V[K][P] = C * Vkp - Sn * Vkq;
                    V[K][Q] = Sn * Vkp + C * Vkq;
                }
            }
    }
    for (int Row = 0; Row < 3; ++Row)
        for (int Column = 0; Column < 3; ++Column)
        {
            double Sum = 0.0;
            for (int K = 0; K < 3; ++K) Sum += std::max(0.0, A[K][K]) * V[Row][K] * V[Column][K];
            S[Row][Column] = Sum;
        }
}

// 📝 Cylindrical axes of a sector's nodes in the wheel axes: radial, axial, tangential.
void SectorAxes(int Sector, double Axes[3][3])
{
    const double Angle = 2.0 * Pi * Sector / TyreCarcassSolid::SectorCount;
    const double Cos = std::cos(Angle), Sin = std::sin(Angle);
    Axes[0][0] = Cos;  Axes[0][1] = 0.0; Axes[0][2] = Sin;    // e_r
    Axes[1][0] = 0.0;  Axes[1][1] = 1.0; Axes[1][2] = 0.0;    // e_y
    Axes[2][0] = -Sin; Axes[2][1] = 0.0; Axes[2][2] = Cos;    // e_t
}

} // namespace

// 📝 One design's tables: the rest tread nodes, and per pressure level the inflation displacement of a tread node and the
//    block-circulant compliance G[d][target ring][source ring] (3 × 3, cylindrical components) of a unit force at sector 0.
struct TyreCarcassCompliance
{
    double                                                   Rest[TyreCarcassSolid::NodeCount][3]{};
    double                                                   ReferencePressure = 110000.0;
    std::array<std::array<std::array<double, 3>, 3>, LevelCount> Inflation{};   // [level][ring] cylindrical [m]
    std::vector<double>                                      Compliance;      // [level][d][target][source][3][3]
    double                                                   RestAirVolume = 0.0, RubberVolume = 0.0;
    bool                                                     Valid = false;

    [[nodiscard]] const double* Block(int Level, int Offset, int Target, int Source) const
    {
        const size_t Index = ((((static_cast<size_t>(Level) * TyreCarcassSolid::SectorCount + static_cast<size_t>(Offset)) * 3 + static_cast<size_t>(Target)) * 3 +
                               static_cast<size_t>(Source)) * 9);
        return &Compliance[Index];
    }
};

namespace
{

// 📝 Volume enclosed by one layer's faces and the rim band between its beads: V = ⅙ Σ a·(b × c), oriented by the rest shape.
double LayerVolume(const double (*Points)[3], int Layer)
{
    double     Volume6  = 0.0;
    const auto Triangle = [&](const double* A, const double* B, const double* C)
    {
        Volume6 += A[0] * (B[1] * C[2] - B[2] * C[1]) + A[1] * (B[2] * C[0] - B[0] * C[2]) + A[2] * (B[0] * C[1] - B[1] * C[0]);
    };
    for (int Sector = 0; Sector < TyreCarcassSolid::SectorCount; ++Sector)
    {
        for (int Ring = 0; Ring + 1 < TyreCarcassSolid::RingCount; ++Ring)
        {
            const double* A = Points[TyreCarcassSolid::NodeIndex(Sector, Ring, Layer)];
            const double* B = Points[TyreCarcassSolid::NodeIndex(Sector + 1, Ring, Layer)];
            const double* C = Points[TyreCarcassSolid::NodeIndex(Sector + 1, Ring + 1, Layer)];
            const double* D = Points[TyreCarcassSolid::NodeIndex(Sector, Ring + 1, Layer)];
            Triangle(A, B, C);
            Triangle(A, C, D);
        }
        const double* A = Points[TyreCarcassSolid::NodeIndex(Sector, TyreCarcassSolid::RingCount - 1, Layer)];
        const double* B = Points[TyreCarcassSolid::NodeIndex(Sector + 1, TyreCarcassSolid::RingCount - 1, Layer)];
        const double* C = Points[TyreCarcassSolid::NodeIndex(Sector + 1, 0, Layer)];
        const double* D = Points[TyreCarcassSolid::NodeIndex(Sector, 0, Layer)];
        Triangle(A, B, C);
        Triangle(A, C, D);
    }
    return std::fabs(Volume6 / 6.0);
}

std::shared_ptr<const TyreCarcassCompliance> ConstructCompliance(double Radius, double RimRadius, double Width, const TyreCarcassSolid::Material& Design,
                                                                 double ReferencePressure)
{
    auto Tables = std::make_shared<TyreCarcassCompliance>();
    Tables->ReferencePressure = ReferencePressure;
    // ① rest nodes on the demo's section, scaled to this tyre
    const double RadialScale = (Radius - RimRadius) / (DemoRadius - DemoRim), LateralScale = Width / DemoWidth;
    for (int Sector = 0; Sector < TyreCarcassSolid::SectorCount; ++Sector)
    {
        const double Angle = 2.0 * Pi * Sector / TyreCarcassSolid::SectorCount;
        const double Cos = std::cos(Angle), Sin = std::sin(Angle);
        for (int Ring = 0; Ring < TyreCarcassSolid::RingCount; ++Ring)
        {
            const SectionPoint& Point = DemoSection[Ring];
            const double Outer = RimRadius + (Point.Radius - DemoRim) * RadialScale, Lateral = Point.Lateral * LateralScale;
            const bool   Bead  = Ring == 0 || Ring == TyreCarcassSolid::RingCount - 1;
            const double Inner = Bead ? RimRadius : Outer - Point.Thickness * RadialScale;
            double* I = Tables->Rest[TyreCarcassSolid::NodeIndex(Sector, Ring, 0)];
            double* O = Tables->Rest[TyreCarcassSolid::NodeIndex(Sector, Ring, 1)];
            I[0] = Inner * Cos; I[1] = Lateral * (Bead ? 0.80 : 0.94); I[2] = Inner * Sin;
            O[0] = Outer * Cos; O[1] = Lateral;                        O[2] = Outer * Sin;
        }
    }
    Tables->RestAirVolume = LayerVolume(Tables->Rest, 0);
    Tables->RubberVolume  = LayerVolume(Tables->Rest, 1) - Tables->RestAirVolume;

    // ② per cell and Gauss point: ∇N (8 × 3) and dV
    struct GaussPoint
    {
        double Gradient[8][3];
        double Volume;
    };
    std::vector<std::array<int, 8>>  Cells;
    std::vector<std::array<GaussPoint, 8>> Points;
    const double Station = 1.0 / std::sqrt(3.0);
    for (int Sector = 0; Sector < TyreCarcassSolid::SectorCount; ++Sector)
        for (int Ring = 0; Ring + 1 < TyreCarcassSolid::RingCount; ++Ring)
        {
            std::array<int, 8> Nodes{};
            for (int A = 0; A < 8; ++A)
                Nodes[static_cast<size_t>(A)] =
                    TyreCarcassSolid::NodeIndex(Sector + (Corner[A][0] > 0 ? 1 : 0), Ring + (Corner[A][1] > 0 ? 1 : 0), Corner[A][2] > 0 ? 1 : 0);
            std::array<GaussPoint, 8> Gauss{};
            for (int Q = 0; Q < 8; ++Q)
            {
                const double Xi[3] = { Corner[Q][0] * Station, Corner[Q][1] * Station, Corner[Q][2] * Station };
                double       Natural[8][3];
                for (int A = 0; A < 8; ++A)
                    for (int J = 0; J < 3; ++J)
                    {
                        double Product = 0.125 * Corner[A][J];
                        for (int K = 0; K < 3; ++K)
                            if (K != J) Product *= 1.0 + Corner[A][K] * Xi[K];
                        Natural[A][J] = Product;
                    }
                double Jacobian[3][3] = {};
                for (int A = 0; A < 8; ++A)
                    for (int Row = 0; Row < 3; ++Row)
                        for (int Column = 0; Column < 3; ++Column) Jacobian[Row][Column] += Tables->Rest[Nodes[static_cast<size_t>(A)]][Row] * Natural[A][Column];
                const double C00 = Jacobian[1][1] * Jacobian[2][2] - Jacobian[1][2] * Jacobian[2][1];
                const double C01 = Jacobian[1][2] * Jacobian[2][0] - Jacobian[1][0] * Jacobian[2][2];
                const double C02 = Jacobian[1][0] * Jacobian[2][1] - Jacobian[1][1] * Jacobian[2][0];
                const double C10 = Jacobian[0][2] * Jacobian[2][1] - Jacobian[0][1] * Jacobian[2][2];
                const double C11 = Jacobian[0][0] * Jacobian[2][2] - Jacobian[0][2] * Jacobian[2][0];
                const double C12 = Jacobian[0][1] * Jacobian[2][0] - Jacobian[0][0] * Jacobian[2][1];
                const double C20 = Jacobian[0][1] * Jacobian[1][2] - Jacobian[0][2] * Jacobian[1][1];
                const double C21 = Jacobian[0][2] * Jacobian[1][0] - Jacobian[0][0] * Jacobian[1][2];
                const double C22 = Jacobian[0][0] * Jacobian[1][1] - Jacobian[0][1] * Jacobian[1][0];
                const double Det = Jacobian[0][0] * C00 + Jacobian[0][1] * C01 + Jacobian[0][2] * C02;
                const double Cof[3][3] = { { C00, C01, C02 }, { C10, C11, C12 }, { C20, C21, C22 } };
                // ∇N = J⁻ᵀ ∂N/∂ξ, J⁻ᵀ = Cof / det
                for (int A = 0; A < 8; ++A)
                    for (int Row = 0; Row < 3; ++Row)
                    {
                        double Sum = 0.0;
                        for (int K = 0; K < 3; ++K) Sum += Cof[Row][K] / Det * Natural[A][K];
                        Gauss[static_cast<size_t>(Q)].Gradient[A][Row] = Sum;
                    }
                Gauss[static_cast<size_t>(Q)].Volume = std::fabs(Det);
            }
            Cells.push_back(Nodes);
            Points.push_back(Gauss);
        }
    const auto Freedom3 = [&](int Node) -> int
    {
        const int Ring = (Node / TyreCarcassSolid::LayerCount) % TyreCarcassSolid::RingCount;
        if (Ring == 0 || Ring == TyreCarcassSolid::RingCount - 1) return -1;
        const int Sector = Node / (TyreCarcassSolid::LayerCount * TyreCarcassSolid::RingCount);
        return MobileIndex(Sector, Ring, Node % TyreCarcassSolid::LayerCount) * 3;
    };

    // ③ material stiffness K_m: K_ab,ij = dV [λ ∂ᵢN_a ∂ⱼN_b + μ (∂ⱼN_a ∂ᵢN_b + δᵢⱼ ∇N_a·∇N_b)]
    const double μ = Design.YoungModulus / (2.0 * (1.0 + Design.PoissonRatio));
    const double λ = Design.YoungModulus * Design.PoissonRatio / ((1.0 + Design.PoissonRatio) * (1.0 - 2.0 * Design.PoissonRatio));
    std::vector<double> Material(static_cast<size_t>(Freedom) * Freedom, 0.0);
    for (size_t Index = 0; Index < Cells.size(); ++Index)
        for (const GaussPoint& Point : Points[Index])
            for (int A = 0; A < 8; ++A)
            {
                const int RowBase = Freedom3(Cells[Index][static_cast<size_t>(A)]);
                if (RowBase < 0) continue;
                for (int B = 0; B < 8; ++B)
                {
                    const int ColumnBase = Freedom3(Cells[Index][static_cast<size_t>(B)]);
                    if (ColumnBase < 0) continue;
                    const double* Ga = Point.Gradient[A];
                    const double* Gb = Point.Gradient[B];
                    const double  Dot = Ga[0] * Gb[0] + Ga[1] * Gb[1] + Ga[2] * Gb[2];
                    for (int I = 0; I < 3; ++I)
                        for (int J = 0; J < 3; ++J)
                            Material[static_cast<size_t>(RowBase + I) * Freedom + static_cast<size_t>(ColumnBase + J)] +=
                                Point.Volume * (λ * Ga[I] * Gb[J] + μ * (Ga[J] * Gb[I] + (I == J ? Dot : 0.0)));
                }
            }

    // ④ the inflation at the reference pressure: follower load p A on the inner faces, u_p = K_m⁻¹ f_p, stresses σ = C ε(u_p)
    std::vector<double> Load(static_cast<size_t>(Freedom), 0.0);
    for (size_t Index = 0; Index < Cells.size(); ++Index)
    {
        const auto&   Nodes = Cells[Index];
        const double* A0 = Tables->Rest[Nodes[0]];
        const double* A1 = Tables->Rest[Nodes[1]];
        const double* A2 = Tables->Rest[Nodes[2]];
        const double* A3 = Tables->Rest[Nodes[3]];
        const double  D1[3] = { A2[0] - A0[0], A2[1] - A0[1], A2[2] - A0[2] }, D2[3] = { A3[0] - A1[0], A3[1] - A1[1], A3[2] - A1[2] };
        double        Area[3] = { 0.5 * (D1[1] * D2[2] - D1[2] * D2[1]), 0.5 * (D1[2] * D2[0] - D1[0] * D2[2]), 0.5 * (D1[0] * D2[1] - D1[1] * D2[0]) };
        const double  Centre[3] = { 0.25 * (A0[0] + A1[0] + A2[0] + A3[0]), 0.25 * (A0[1] + A1[1] + A2[1] + A3[1]), 0.25 * (A0[2] + A1[2] + A2[2] + A3[2]) };
        const double  Planar = std::sqrt(Centre[0] * Centre[0] + Centre[2] * Centre[2]);
        const double  Middle = 0.5 * (Radius + RimRadius);
        const double  Away = Area[0] * (Centre[0] - Centre[0] / Planar * Middle) + Area[1] * Centre[1] + Area[2] * (Centre[2] - Centre[2] / Planar * Middle);
        if (Away < 0.0)
            for (double& Component : Area) Component = -Component;
        for (int K = 0; K < 4; ++K)
        {
            const int Base = Freedom3(Nodes[static_cast<size_t>(K)]);
            if (Base < 0) continue;
            for (int Axis = 0; Axis < 3; ++Axis) Load[static_cast<size_t>(Base + Axis)] += 0.25 * ReferencePressure * Area[Axis];
        }
    }
    std::vector<double> Factor = Material;
    if (!FactorCholesky(Factor, Freedom)) return Tables;
    std::vector<double> Inflated = Load;
    SolveCholesky(Factor, Freedom, Inflated);
    std::vector<double> Prestress(static_cast<size_t>(Freedom) * Freedom, 0.0);
    for (size_t Index = 0; Index < Cells.size(); ++Index)
        for (const GaussPoint& Point : Points[Index])
        {
            // ε = sym(Σ u_a ⊗ ∇N_a), σ = λ tr ε I + 2 μ ε
            double Gradient[3][3] = {};
            for (int A = 0; A < 8; ++A)
            {
                const int Base = Freedom3(Cells[Index][static_cast<size_t>(A)]);
                if (Base < 0) continue;
                for (int Row = 0; Row < 3; ++Row)
                    for (int Column = 0; Column < 3; ++Column) Gradient[Row][Column] += Inflated[static_cast<size_t>(Base + Row)] * Point.Gradient[A][Column];
            }
            double Stress[3][3];
            const double Trace = Gradient[0][0] + Gradient[1][1] + Gradient[2][2];
            for (int Row = 0; Row < 3; ++Row)
                for (int Column = 0; Column < 3; ++Column)
                    Stress[Row][Column] = (Row == Column ? λ * Trace : 0.0) + μ * (Gradient[Row][Column] + Gradient[Column][Row]);
            // 📝 Cords and membranes carry tension only (they wrinkle rather than push): keep σ's positive principal part.
            KeepTension(Stress);
            // K_σ: δᵢⱼ dV ∇N_a · σ · ∇N_b
            for (int A = 0; A < 8; ++A)
            {
                const int RowBase = Freedom3(Cells[Index][static_cast<size_t>(A)]);
                if (RowBase < 0) continue;
                double Projected[3];
                for (int Column = 0; Column < 3; ++Column)
                    Projected[Column] = Point.Gradient[A][0] * Stress[0][Column] + Point.Gradient[A][1] * Stress[1][Column] + Point.Gradient[A][2] * Stress[2][Column];
                for (int B = 0; B < 8; ++B)
                {
                    const int ColumnBase = Freedom3(Cells[Index][static_cast<size_t>(B)]);
                    if (ColumnBase < 0) continue;
                    const double Term = Point.Volume * (Projected[0] * Point.Gradient[B][0] + Projected[1] * Point.Gradient[B][1] + Projected[2] * Point.Gradient[B][2]);
                    for (int I = 0; I < 3; ++I) Prestress[static_cast<size_t>(RowBase + I) * Freedom + static_cast<size_t>(ColumnBase + I)] += Term;
                }
            }
        }

    // ⑤ per pressure level: K = K_m + κ (p / p_ref) K_σ, then the columns of K⁻¹ for unit forces on sector 0's tread nodes. The
    //    authored tyre is the tyre at its reference pressure, so a level's inflation is (p / p_ref − 1) u_p from the authored shape.
    Tables->Compliance.assign(static_cast<size_t>(LevelCount) * TyreCarcassSolid::SectorCount * 3 * 3 * 9, 0.0);
    // 📝 The cords carry the inflation, so the tyre grows by u_p E / E_cord, not by the soft structural u_p (the stress is the same).
    const double Growth = Design.CordModulus > Design.YoungModulus ? Design.YoungModulus / Design.CordModulus : 1.0;
    for (int Level = 0; Level < LevelCount; ++Level)
    {
        const double Ratio = 0.5 * Level;
        std::vector<double> System(static_cast<size_t>(Freedom) * Freedom);
        for (size_t Index = 0; Index < System.size(); ++Index) System[Index] = Material[Index] + Design.PrestressScale * Ratio * Prestress[Index];
        if (!FactorCholesky(System, Freedom)) return Tables;
        for (int Ring = 0; Ring < 3; ++Ring)
            for (int Axis = 0; Axis < 3; ++Axis) Tables->Inflation[static_cast<size_t>(Level)][static_cast<size_t>(Ring)][static_cast<size_t>(Axis)] =
                    (Ratio - 1.0) * Growth * Inflated[static_cast<size_t>(MobileIndex(0, Ring + 1, 1) * 3 + Axis)];
        for (int Source = 0; Source < 3; ++Source)
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                // 📝 At sector 0 the cylindrical axes are the wheel axes (e_r = x, e_y = y, e_t = z).
                std::vector<double> Column(static_cast<size_t>(Freedom), 0.0);
                Column[static_cast<size_t>(MobileIndex(0, Source + 1, 1) * 3 + Axis)] = 1.0;
                SolveCholesky(System, Freedom, Column);
                for (int Offset = 0; Offset < TyreCarcassSolid::SectorCount; ++Offset)
                {
                    double Axes[3][3];
                    SectorAxes(Offset, Axes);
                    for (int Target = 0; Target < 3; ++Target)
                    {
                        const double* U = &Column[static_cast<size_t>(MobileIndex(Offset, Target + 1, 1) * 3)];
                        double* Block = const_cast<double*>(Tables->Block(Level, Offset, Target, Source));
                        for (int Component = 0; Component < 3; ++Component)
                            Block[Component * 3 + Axis] = Axes[Component][0] * U[0] + Axes[Component][1] * U[1] + Axes[Component][2] * U[2];
                    }
                }
            }
    }
    Tables->Valid = true;
    return Tables;
}

// 📝 Tables are reused across tyres of identical geometry, material and reference pressure.
std::shared_ptr<const TyreCarcassCompliance> ResolveCompliance(double Radius, double RimRadius, double Width, const TyreCarcassSolid::Material& Design,
                                                               double ReferencePressure)
{
    using Key = std::tuple<double, double, double, double, double, double, double, double>;
    static std::mutex                                                         Lock;
    static std::map<Key, std::weak_ptr<const TyreCarcassCompliance>>          Designs;
    const Key Wanted{ Radius, RimRadius, Width, Design.YoungModulus, Design.PoissonRatio, Design.PrestressScale, Design.CordModulus, ReferencePressure };
    std::lock_guard<std::mutex> Guard(Lock);
    if (auto Found = Designs.find(Wanted); Found != Designs.end())
        if (auto Existing = Found->second.lock()) return Existing;
    auto Built = ConstructCompliance(Radius, RimRadius, Width, Design, ReferencePressure);
    if (!Built->Valid) return nullptr;
    Designs[Wanted] = Built;
    return Built;
}

} // namespace

TyreCarcassSolid::TyreCarcassSolid(const TyreCarcassParameters& Parameters, float TyreRadius, float TyreRimRadius, float TyreWidth)
    : TyreCarcassSolid(
          Parameters,
          TyreRadius,
          TyreRimRadius,
          TyreWidth,
          Material{
              Parameters.FemYoungModulus,
              std::clamp(Parameters.FemPoissonRatio, 0.05, 0.49),
              Parameters.FemPrestressScale,
              Parameters.FemCordModulus,
              Parameters.FemDampingRate })
{
}

TyreCarcassSolid::TyreCarcassSolid(const TyreCarcassParameters& Parameters, float TyreRadius, float TyreRimRadius, float TyreWidth, const Material& Chosen)
    : Tuning(Parameters), Design(Chosen), Radius(TyreRadius), RimRadius(TyreRimRadius), Width(TyreWidth)
{
    Tables = ResolveCompliance(Radius, RimRadius, Width, Design, Tuning.ReferencePressure);
    if (Tables)
    {
        RestAirVolume = Tables->RestAirVolume;
        AirVolume     = RestAirVolume;
    }
    Reset(CarcassContact{});
}

void TyreCarcassSolid::Reset(const CarcassContact&)
{
    Displacement.fill(0.0);
    TreadForce.fill(0.0);
    AirVolume          = RestAirVolume;
    PreviousDeflection = 0.0;
}

double TyreCarcassSolid::SolveContact(const CarcassContact& Contact, int& Touching, double PatchSum[3])
{
    Touching    = 0;
    PatchSum[0] = PatchSum[1] = PatchSum[2] = 0.0;
    TreadForce.fill(0.0);
    if (!Tables) return 0.0;
    // 📝 The level pair bracketing the live pressure (linear between levels, clamped to 0 … 2 × the reference).
    const double Ratio = std::clamp(static_cast<double>(Contact.GaugePressure) / Tables->ReferencePressure, 0.0, 2.0);
    const int    Low   = std::min(LevelCount - 2, static_cast<int>(Ratio / 0.5));
    const double Blend = Ratio / 0.5 - Low;

    // ① inflated tread positions and their gaps to the road plane (wheel axes)
    const double GroundOffsetWorld[3] = { static_cast<double>(Contact.GroundPoint.x) - Contact.HubPosition.x,
                                          static_cast<double>(Contact.GroundPoint.y) - Contact.HubPosition.y,
                                          static_cast<double>(Contact.GroundPoint.z) - Contact.HubPosition.z };
    const double NormalWorld[3]       = { Contact.GroundNormal.x, Contact.GroundNormal.y, Contact.GroundNormal.z };
    double       GroundPoint[3], Normal[3];
    TurnBack(Contact.HubRotation, GroundOffsetWorld, GroundPoint);
    TurnBack(Contact.HubRotation, NormalWorld, Normal);
    std::array<double, TreadCount * 3> Inflated{};
    std::array<double, TreadCount>     Gap{};
    for (int Sector = 0; Sector < SectorCount; ++Sector)
    {
        double Axes[3][3];
        SectorAxes(Sector, Axes);
        for (int Ring = 0; Ring < TreadRings; ++Ring)
        {
            const int     Tread = Sector * TreadRings + Ring;
            const double* Rest  = Tables->Rest[NodeIndex(Sector, Ring + 1, 1)];
            const auto&   A     = Tables->Inflation[static_cast<size_t>(Low)][static_cast<size_t>(Ring)];
            const auto&   B     = Tables->Inflation[static_cast<size_t>(Low + 1)][static_cast<size_t>(Ring)];
            const double  Cylindrical[3] = { A[0] + Blend * (B[0] - A[0]), A[1] + Blend * (B[1] - A[1]), A[2] + Blend * (B[2] - A[2]) };
            double        Offset[3];
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                Offset[Axis] = Axes[0][Axis] * Cylindrical[0] + Axes[1][Axis] * Cylindrical[1] + Axes[2][Axis] * Cylindrical[2];
                Inflated[static_cast<size_t>(Tread * 3 + Axis)] = Offset[Axis];
            }
            Gap[static_cast<size_t>(Tread)] = (Rest[0] + Offset[0] - GroundPoint[0]) * Normal[0] + (Rest[1] + Offset[1] - GroundPoint[1]) * Normal[1] +
                                              (Rest[2] + Offset[2] - GroundPoint[2]) * Normal[2];
        }
    }
    // ② the contact candidates and their compliance W_ab = n · G_ab · n (Cartesian blocks from the cylindrical table)
    std::array<int, TreadCount> Candidates{};
    int                         Count = 0;
    if (Contact.Ground)
        for (int Tread = 0; Tread < TreadCount; ++Tread)
            if (Gap[static_cast<size_t>(Tread)] < Margin) Candidates[static_cast<size_t>(Count++)] = Tread;
    const auto CartesianBlock = [&](int Target, int Source, double Out[3][3])
    {
        const int SectorT = Target / TreadRings, RingT = Target % TreadRings, SectorS = Source / TreadRings, RingS = Source % TreadRings;
        const int Offset  = ((SectorT - SectorS) % SectorCount + SectorCount) % SectorCount;
        const double* A = Tables->Block(Low, Offset, RingT, RingS);
        const double* B = Tables->Block(Low + 1, Offset, RingT, RingS);
        double        Cylindrical[3][3];
        for (int Row = 0; Row < 3; ++Row)
            for (int Column = 0; Column < 3; ++Column) Cylindrical[Row][Column] = A[Row * 3 + Column] + Blend * (B[Row * 3 + Column] - A[Row * 3 + Column]);
        double AxesT[3][3], AxesS[3][3];
        SectorAxes(SectorT, AxesT);
        SectorAxes(SectorS, AxesS);
        // G_cart = Tᵀ_t G_cyl T_s, rows of Axes are the cylindrical unit vectors in wheel axes
        for (int Row = 0; Row < 3; ++Row)
            for (int Column = 0; Column < 3; ++Column)
            {
                double Sum = 0.0;
                for (int I = 0; I < 3; ++I)
                    for (int J = 0; J < 3; ++J) Sum += AxesT[I][Row] * Cylindrical[I][J] * AxesS[J][Column];
                Out[Row][Column] = Sum;
            }
    };
    std::vector<double> W(static_cast<size_t>(Count) * static_cast<size_t>(Count));
    for (int A = 0; A < Count; ++A)
        for (int B = 0; B < Count; ++B)
        {
            double Block[3][3];
            CartesianBlock(Candidates[static_cast<size_t>(A)], Candidates[static_cast<size_t>(B)], Block);
            double Sum = 0.0;
            for (int Row = 0; Row < 3; ++Row)
                for (int Column = 0; Column < 3; ++Column) Sum += Normal[Row] * Block[Row][Column] * Normal[Column];
            W[static_cast<size_t>(A) * Count + B] = Sum;
        }
    // ③ projected Gauss–Seidel on the complementarity: λ ≥ 0, gap + W λ ≥ 0, λ (gap + W λ) = 0
    std::vector<double> Force(static_cast<size_t>(Count), 0.0);
    for (int Sweep = 0; Sweep < 200; ++Sweep)
    {
        double Largest = 0.0;
        for (int A = 0; A < Count; ++A)
        {
            double Residual = Gap[static_cast<size_t>(Candidates[static_cast<size_t>(A)])];
            for (int B = 0; B < Count; ++B) Residual += W[static_cast<size_t>(A) * Count + B] * Force[static_cast<size_t>(B)];
            const double Diagonal = W[static_cast<size_t>(A) * Count + A];
            if (!(Diagonal > 0.0)) continue;
            const double Next = std::max(0.0, Force[static_cast<size_t>(A)] - Residual / Diagonal);
            Largest = std::max(Largest, std::fabs(Next - Force[static_cast<size_t>(A)]));
            Force[static_cast<size_t>(A)] = Next;
        }
        if (Largest < 1.0e-3) break;
    }
    // ④ the tread's displacement: inflation plus the compliance's response to normal + Coulomb-bounded tangential contact forces
    double Total = 0.0;
    for (int A = 0; A < Count; ++A)
        if (Force[static_cast<size_t>(A)] > 0.0) Total += Force[static_cast<size_t>(A)];

    double TractionPerNormal[3] = { 0.0, 0.0, 0.0 };
    if (Total > 1.0e-6)
    {
        const double RawTangential[3] = {
            static_cast<double>(Contact.TangentialForce.x),
            static_cast<double>(Contact.TangentialForce.y),
            static_cast<double>(Contact.TangentialForce.z) };
        const double NormalProjection = RawTangential[0] * NormalWorld[0] + RawTangential[1] * NormalWorld[1] + RawTangential[2] * NormalWorld[2];
        double       PlanarWorld[3]   = {
            RawTangential[0] - NormalProjection * NormalWorld[0],
            RawTangential[1] - NormalProjection * NormalWorld[1],
            RawTangential[2] - NormalProjection * NormalWorld[2] };
        const double PlanarMagnitude = std::sqrt(PlanarWorld[0] * PlanarWorld[0] + PlanarWorld[1] * PlanarWorld[1] + PlanarWorld[2] * PlanarWorld[2]);
        if (PlanarMagnitude > 1.0e-6)
        {
            const double CoulombLimit = std::max(0.0, Tuning.Friction) * Total;
            const double ClampScale   = PlanarMagnitude > CoulombLimit ? (CoulombLimit / PlanarMagnitude) : 1.0;
            for (int Axis = 0; Axis < 3; ++Axis) PlanarWorld[Axis] *= ClampScale;
            double TractionWheel[3] = {};
            TurnBack(Contact.HubRotation, PlanarWorld, TractionWheel);
            for (int Axis = 0; Axis < 3; ++Axis) TractionPerNormal[Axis] = TractionWheel[Axis] / Total;
        }
    }

    const double ForceDirection[3] = {
        Normal[0] + TractionPerNormal[0],
        Normal[1] + TractionPerNormal[1],
        Normal[2] + TractionPerNormal[2] };

    for (int Tread = 0; Tread < TreadCount; ++Tread)
    {
        double Sum[3] = { Inflated[static_cast<size_t>(Tread * 3)], Inflated[static_cast<size_t>(Tread * 3 + 1)], Inflated[static_cast<size_t>(Tread * 3 + 2)] };
        for (int A = 0; A < Count; ++A)
        {
            if (Force[static_cast<size_t>(A)] <= 0.0) continue;
            double Block[3][3];
            CartesianBlock(Tread, Candidates[static_cast<size_t>(A)], Block);
            for (int Row = 0; Row < 3; ++Row)
                Sum[Row] += (Block[Row][0] * ForceDirection[0] + Block[Row][1] * ForceDirection[1] + Block[Row][2] * ForceDirection[2]) * Force[static_cast<size_t>(A)];
        }
        for (int Axis = 0; Axis < 3; ++Axis) Displacement[static_cast<size_t>(Tread * 3 + Axis)] = Sum[Axis];
    }
    for (int A = 0; A < Count; ++A)
    {
        const double Pushed = Force[static_cast<size_t>(A)];
        if (Pushed <= 0.0) continue;
        const int Tread = Candidates[static_cast<size_t>(A)];
        TreadForce[static_cast<size_t>(Tread)] = Pushed;
        const double* Rest = Tables->Rest[NodeIndex(Tread / TreadRings, Tread % TreadRings + 1, 1)];
        const double  Local[3] = { Rest[0] + Displacement[static_cast<size_t>(Tread * 3)], Rest[1] + Displacement[static_cast<size_t>(Tread * 3 + 1)],
                                   Rest[2] + Displacement[static_cast<size_t>(Tread * 3 + 2)] };
        double World[3];
        Turn(Contact.HubRotation, Local, World);
        PatchSum[0] += Pushed * (World[0] + Contact.HubPosition.x);
        PatchSum[1] += Pushed * (World[1] + Contact.HubPosition.y);
        PatchSum[2] += Pushed * (World[2] + Contact.HubPosition.z);
        ++Touching;
    }
    // ⑤ air volume: the outer surface's enclosed volume less the rubber's
    {
        double Outer[NodeCount][3];
        for (int Node = 0; Node < NodeCount; ++Node)
            for (int Axis = 0; Axis < 3; ++Axis) Outer[Node][Axis] = Tables->Rest[Node][Axis];
        for (int Tread = 0; Tread < TreadCount; ++Tread)
        {
            double* Point = Outer[NodeIndex(Tread / TreadRings, Tread % TreadRings + 1, 1)];
            for (int Axis = 0; Axis < 3; ++Axis) Point[Axis] += Displacement[static_cast<size_t>(Tread * 3 + Axis)];
        }
        AirVolume = LayerVolume(Outer, 1) - Tables->RubberVolume;
    }
    return Total;
}

CarcassResponse TyreCarcassSolid::Advance(const CarcassContact& Contact)
{
    CarcassResponse Response;
    double          PatchSum[3];
    int             Touching = 0;
    const double    Load = SolveContact(Contact, Touching, PatchSum);
    Response.ContactCount   = static_cast<uint32_t>(Touching);
    Response.EnclosedVolume = static_cast<float>(AirVolume);
    if (Touching > 0 && Load > 0.0)
        Response.PatchCentre = Vec3{ static_cast<float>(PatchSum[0] / Load), static_cast<float>(PatchSum[1] / Load), static_cast<float>(PatchSum[2] / Load) };
    double Rate = 0.0;
    if (Contact.Ground)
    {
        const Vec3   Offset     = Contact.HubPosition - Contact.GroundPoint;
        const double Height     = Dot(Offset, Contact.GroundNormal);
        const double Deflection = std::clamp(Radius - Height, 0.0, Radius - RimRadius);
        Rate                    = Contact.Seconds > 0.0f ? (Deflection - PreviousDeflection) / Contact.Seconds : 0.0;
        Response.Deflection     = static_cast<float>(Deflection);
        Response.Rate           = static_cast<float>(Rate);
        PreviousDeflection      = Deflection;
    }
    else
    {
        PreviousDeflection = 0.0;
    }
    // 📝 Quasi-static elasticity plus the carcass's damping on the deflection rate (the spring's 1500 N·s/m by default).
    Response.NormalLoad = Load > 0.0 ? static_cast<float>(std::max(0.0, Load + Design.DampingRate * Rate)) : 0.0f;
    return Response;
}

std::array<double, 3> TyreCarcassSolid::QueryTreadNode(int Sector, int TreadRing) const
{
    const int Tread = ((Sector % SectorCount + SectorCount) % SectorCount) * TreadRings + TreadRing;
    if (!Tables) return { 0.0, 0.0, 0.0 };
    const double* Rest = Tables->Rest[NodeIndex(Sector, TreadRing + 1, 1)];
    return { Rest[0] + Displacement[static_cast<size_t>(Tread * 3)], Rest[1] + Displacement[static_cast<size_t>(Tread * 3 + 1)],
             Rest[2] + Displacement[static_cast<size_t>(Tread * 3 + 2)] };
}

std::array<double, 3> TyreCarcassSolid::QueryRestTreadNode(int Sector, int TreadRing) const
{
    if (!Tables) return { 0.0, 0.0, 0.0 };
    const double* Rest = Tables->Rest[NodeIndex(Sector, TreadRing + 1, 1)];
    return { Rest[0], Rest[1], Rest[2] };
}

double TyreCarcassSolid::QueryTreadForce(int Sector, int TreadRing) const
{
    return TreadForce[static_cast<size_t>(((Sector % SectorCount + SectorCount) % SectorCount) * TreadRings + TreadRing)];
}

void TyreCarcassSolid::SampleField(float SpinAngle, CarcassField& Field) const
{
    Field.fill(0.0f);
    const double Cos = std::cos(SpinAngle), Sin = std::sin(SpinAngle);
    for (uint32_t Sector = 0; Sector < CarcassSectorCount; ++Sector)
    {
        const double Material = 2.0 * Pi * Sector / CarcassSectorCount;
        double       Wheel    = std::fmod(Material - SpinAngle, 2.0 * Pi);
        if (Wheel < 0.0) Wheel += 2.0 * Pi;
        const double Position = Wheel / (2.0 * Pi) * SectorCount;
        const int    Lower    = static_cast<int>(std::floor(Position)) % SectorCount;
        const int    Upper    = (Lower + 1) % SectorCount;
        const double Blend    = Position - std::floor(Position);
        for (int Ring = 0; Ring < TreadRings; ++Ring)
        {
            const int A = Lower * TreadRings + Ring, B = Upper * TreadRings + Ring;
            double    Moved[3];
            for (int Axis = 0; Axis < 3; ++Axis)
                Moved[Axis] = (1.0 - Blend) * Displacement[static_cast<size_t>(A * 3 + Axis)] + Blend * Displacement[static_cast<size_t>(B * 3 + Axis)];
            const uint32_t Point = Sector * CarcassRingCount + static_cast<uint32_t>(Ring + 1);
            Field[Point * 3 + 0] = static_cast<float>(Cos * Moved[0] - Sin * Moved[2]);
            Field[Point * 3 + 1] = static_cast<float>(Moved[1]);
            Field[Point * 3 + 2] = static_cast<float>(Sin * Moved[0] + Cos * Moved[2]);
        }
    }
}

std::unique_ptr<TyreCarcass> ConstructTyreCarcass(TyreCarcassCategory Category, const TyreCarcassParameters& Parameters, float Radius,
                                                  float RimRadius, float Width)
{
    switch (Category)
    {
        case TyreCarcassCategory::Xpbd: return std::make_unique<TyreCarcassLattice>(Parameters, Radius, RimRadius, Width);
        case TyreCarcassCategory::Fem:  return std::make_unique<TyreCarcassSolid>(Parameters, Radius, RimRadius, Width);
        case TyreCarcassCategory::Spring: break;
    }
    return nullptr;
}

} // namespace Frontier::Vehicle
