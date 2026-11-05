/*==========================================================================================================================================
                                                          TYRECARCASSLATTICE.CPP
==========================================================================================================================================*/
// 🧩 The XPBD carcass: distance constraints with compliance (Macklin et al., XPBD: λ with α̃ = α / Δt²), a global enclosed-volume
//    constraint with the air's bulk compliance, positional road contact with Coulomb friction, and the spokes' reaction on the rim.
//    Wheel axes: hub at the origin, x forward, y along the axle, z up.

#include "TyreCarcassLattice.h"

#include <algorithm>
#include <cmath>

namespace Frontier::Vehicle
{

namespace
{

constexpr double Pi                  = 3.14159265358979323846;
constexpr double AirGamma            = 1.4;        // [-]  adiabatic index of air
constexpr double AtmosphericPressure = 101325.0;   // [Pa]

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

int ParticleIndex(int Sector, int Row)
{
    const int Wrapped = ((Sector % TyreCarcassLattice::SectorCount) + TyreCarcassLattice::SectorCount) % TyreCarcassLattice::SectorCount;
    return Wrapped * TyreCarcassLattice::RowCount + Row;
}

double Distance(const double A[3], const double B[3])
{
    const double Dx = A[0] - B[0], Dy = A[1] - B[1], Dz = A[2] - B[2];
    return std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
}

} // namespace

TyreCarcassLattice::TyreCarcassLattice(const TyreCarcassParameters& Parameters, float TyreRadius, float TyreRimRadius, float TyreWidth)
    : PressureForceFactor(Parameters.XpbdPressureForceFactor)
    , SpokeCompliance(Parameters.XpbdSpokeCompliance)
    , BeltCompliance(Parameters.XpbdBeltCompliance)
    , LateralCompliance(Parameters.XpbdLateralCompliance)
    , ShearCompliance(Parameters.XpbdShearCompliance)
    , Iterations(std::max(1, Parameters.XpbdIterations))
    , Tuning(Parameters)
    , Radius(TyreRadius)
    , RimRadius(TyreRimRadius)
    , Width(TyreWidth)
{
    // 📝 The 5ec lattice's proportions (shoulder rows ±0.13 m on a 0.3958 m tread, shoulders 6 mm under the crown), scaled.
    const double Lateral  = 0.13 / 0.3958 * Width;
    const double Shoulder = 0.006 / 0.50855 * Radius;
    RowOffsets[0] = -Lateral; RowOffsets[1] = 0.0; RowOffsets[2] = Lateral;
    RowRadii[0] = Radius - Shoulder; RowRadii[1] = Radius; RowRadii[2] = Radius - Shoulder;
    BuildRestShape();
    Reset(CarcassContact{});
}

void TyreCarcassLattice::BuildRestShape()
{
    const double MovingMass = std::max(0.5, Tuning.TreadMass);
    for (int Sector = 0; Sector < SectorCount; ++Sector)
    {
        const double Angle = 2.0 * Pi * Sector / SectorCount;   // [rad] 0 = +x (forward), toward +z
        const double Cos = std::cos(Angle), Sin = std::sin(Angle);
        for (int Row = 0; Row < RowCount; ++Row)
        {
            const int Index = ParticleIndex(Sector, Row);
            RestPositions[Index][0] = RowRadii[Row] * Cos;
            RestPositions[Index][1] = RowOffsets[Row];
            RestPositions[Index][2] = RowRadii[Row] * Sin;
            RimAnchors[Index][0]    = RimRadius * Cos;
            RimAnchors[Index][1]    = RowOffsets[Row] * 0.8;
            RimAnchors[Index][2]    = RimRadius * Sin;
            InverseMass[Index]      = ParticleCount / MovingMass;
        }
    }
    ConstraintCount = 0;
    const auto AddConstraint = [&](int A, int B, int Category)
    {
        DistanceConstraint& Constraint = Constraints[ConstraintCount++];
        Constraint.A          = A;
        Constraint.B          = B;
        Constraint.Category   = Category;
        Constraint.Rest       = B < 0 ? Distance(RestPositions[A], RimAnchors[A]) : Distance(RestPositions[A], RestPositions[B]);
    };
    for (int Sector = 0; Sector < SectorCount; ++Sector)
        for (int Row = 0; Row < RowCount; ++Row)
        {
            const int Index = ParticleIndex(Sector, Row);
            AddConstraint(Index, -1, 0);                                  // spoke to the rim
            AddConstraint(Index, ParticleIndex(Sector + 1, Row), 1);      // belt
            if (Row + 1 < RowCount)
            {
                AddConstraint(Index, ParticleIndex(Sector, Row + 1), 2);  // across
                AddConstraint(Index, ParticleIndex(Sector + 1, Row + 1), 3);
                AddConstraint(ParticleIndex(Sector + 1, Row), ParticleIndex(Sector, Row + 1), 3);
            }
        }
    const double Signed = ComputeVolume(RestPositions, nullptr);
    VolumeSign          = Signed < 0.0 ? -1.0 : 1.0;
    RestVolume          = std::fabs(Signed);
}

void TyreCarcassLattice::Reset(const CarcassContact&)
{
    for (int Index = 0; Index < ParticleCount; ++Index)
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            Positions[Index][Axis]  = RestPositions[Index][Axis];
            Predicted[Index][Axis]  = RestPositions[Index][Axis];
            Velocities[Index][Axis] = 0.0;
        }
    PreviousDeflection = 0.0;
}

// 📝 Enclosed volume by the divergence theorem over the closed tread and rim surface: tread quads between rows, side walls from
//    the shoulder rows to their rim anchors, and the rim band (kinematic, included so the surface closes). Gradients ∂V/∂p
//    collect the triangle terms for the tread particles.
double TyreCarcassLattice::ComputeVolume(const double (*Points)[3], double* Gradients) const
{
    if (Gradients)
        for (int Index = 0; Index < ParticleCount * 3; ++Index) Gradients[Index] = 0.0;
    double Volume6 = 0.0;   // 6 V, the sum of a·(b × c)
    const auto Triangle = [&](const double A[3], const double B[3], const double C[3], int IndexA, int IndexB, int IndexC)
    {
        const double Cross[3] = { B[1] * C[2] - B[2] * C[1], B[2] * C[0] - B[0] * C[2], B[0] * C[1] - B[1] * C[0] };
        Volume6 += A[0] * Cross[0] + A[1] * Cross[1] + A[2] * Cross[2];
        if (!Gradients) return;
        const double GradientB[3] = { C[1] * A[2] - C[2] * A[1], C[2] * A[0] - C[0] * A[2], C[0] * A[1] - C[1] * A[0] };
        const double GradientC[3] = { A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0] };
        const int     Indices[3]  = { IndexA, IndexB, IndexC };
        const double* Terms[3]    = { Cross, GradientB, GradientC };
        for (int Corner = 0; Corner < 3; ++Corner)
        {
            if (Indices[Corner] < 0) continue;
            for (int Axis = 0; Axis < 3; ++Axis) Gradients[Indices[Corner] * 3 + Axis] += Terms[Corner][Axis] / 6.0;
        }
    };
    const auto Quad = [&](const double A[3], const double B[3], const double C[3], const double D[3], int Ia, int Ib, int Ic, int Id)
    {
        Triangle(A, B, C, Ia, Ib, Ic);
        Triangle(A, C, D, Ia, Ic, Id);
    };
    for (int Sector = 0; Sector < SectorCount; ++Sector)
    {
        const int Next = (Sector + 1) % SectorCount;
        for (int Row = 0; Row + 1 < RowCount; ++Row)
        {
            const int A = ParticleIndex(Sector, Row), B = ParticleIndex(Next, Row);
            const int C = ParticleIndex(Next, Row + 1), D = ParticleIndex(Sector, Row + 1);
            Quad(Points[A], Points[B], Points[C], Points[D], A, B, C, D);
        }
        for (int Side = 0; Side < 2; ++Side)
        {
            const int Row = Side == 0 ? 0 : RowCount - 1;
            const int A = ParticleIndex(Sector, Row), B = ParticleIndex(Next, Row);
            Quad(Points[A], Points[B], RimAnchors[B], RimAnchors[A], A, B, -1, -1);
        }
        for (int Row = 0; Row + 1 < RowCount; ++Row)
        {
            const int A = ParticleIndex(Sector, Row), B = ParticleIndex(Next, Row);
            const int C = ParticleIndex(Next, Row + 1), D = ParticleIndex(Sector, Row + 1);
            Quad(RimAnchors[A], RimAnchors[B], RimAnchors[C], RimAnchors[D], -1, -1, -1, -1);
        }
    }
    return Volume6 / 6.0;
}

double TyreCarcassLattice::SubStep(const CarcassContact& Contact, double Dt, double Groundward[3], double& ContactLoad, int& Touching,
                                   double PatchSum[3])
{
    // 📝 The road plane and the hub's travel in the wheel axes. No gravity here: the tread's weight is part of the unsprung mass
    //    the vehicle solver carries (as with the spring), so the rim reaction equals the road's push at equilibrium.
    const double GroundOffsetWorld[3] = { static_cast<double>(Contact.GroundPoint.x) - Contact.HubPosition.x,
                                          static_cast<double>(Contact.GroundPoint.y) - Contact.HubPosition.y,
                                          static_cast<double>(Contact.GroundPoint.z) - Contact.HubPosition.z };
    const double NormalWorld[3]       = { Contact.GroundNormal.x, Contact.GroundNormal.y, Contact.GroundNormal.z };
    const double HubVelocityWorld[3]  = { Contact.HubVelocity.x, Contact.HubVelocity.y, Contact.HubVelocity.z };
    double       GroundPoint[3], Normal[3], HubVelocity[3];
    TurnBack(Contact.HubRotation, GroundOffsetWorld, GroundPoint);
    TurnBack(Contact.HubRotation, NormalWorld, Normal);
    TurnBack(Contact.HubRotation, HubVelocityWorld, HubVelocity);
    const double Spin = Contact.SpinRate;

    // ① predict
    for (int Index = 0; Index < ParticleCount; ++Index)
        for (int Axis = 0; Axis < 3; ++Axis) Predicted[Index][Axis] = Positions[Index][Axis] + Velocities[Index][Axis] * Dt;
    for (int Index = 0; Index < ConstraintCount; ++Index) Constraints[static_cast<size_t>(Index)].Lambda = 0.0;

    const auto Solve = [&](int A, const double PointB[3], double InverseMassB, double Rest, double Compliance, double& Lambda) -> double
    {
        const double Delta[3] = { Predicted[A][0] - PointB[0], Predicted[A][1] - PointB[1], Predicted[A][2] - PointB[2] };
        const double Length   = std::sqrt(Delta[0] * Delta[0] + Delta[1] * Delta[1] + Delta[2] * Delta[2]);
        if (Length < 1.0e-12) return 0.0;
        const double C          = Length - Rest;
        const double AlphaTilde = Compliance / (Dt * Dt);
        const double Change     = (-C - AlphaTilde * Lambda) / (InverseMass[A] + InverseMassB + AlphaTilde);
        Lambda += Change;
        const double Scale = Change * InverseMass[A] / Length;
        for (int Axis = 0; Axis < 3; ++Axis) Predicted[A][Axis] += Scale * Delta[Axis];
        return Change;
    };

    const double Gauge            = std::max(0.0, static_cast<double>(Contact.GaugePressure));
    const double VolumeCompliance = RestVolume / (AirGamma * (AtmosphericPressure + Gauge));
    double       VolumeLambda     = 0.0;
    double       Impulse          = 0.0;
    double       Gradients[ParticleCount * 3];
    bool         Touched[ParticleCount] = {};

    // ② iterate
    for (int Iteration = 0; Iteration < Iterations; ++Iteration)
    {
        // 📝 Inflation η·p·Aᵢ radially outward, spread over the iterations (a = F/m, Δx = a Δt² / N each sweep).
        if (Gauge > 0.0 && PressureForceFactor > 0.0)
            for (int Index = 0; Index < ParticleCount; ++Index)
            {
                const int    Row    = Index % RowCount;
                const double Area   = (2.0 * Pi * RowRadii[Row] / SectorCount) * (Width / RowCount);
                const double Push   = PressureForceFactor * Gauge * Area * InverseMass[Index] * Dt * Dt / Iterations;
                const double Radial = std::sqrt(Predicted[Index][0] * Predicted[Index][0] + Predicted[Index][2] * Predicted[Index][2]);
                if (Radial > 1.0e-9)
                {
                    Predicted[Index][0] += Push * Predicted[Index][0] / Radial;
                    Predicted[Index][2] += Push * Predicted[Index][2] / Radial;
                }
            }
        for (int Index = 0; Index < ConstraintCount; ++Index)
        {
            DistanceConstraint& Constraint = Constraints[static_cast<size_t>(Index)];
            const double Compliance = ResolveCompliance(Constraint.Category);
            if (Constraint.B < 0)
            {
                Solve(Constraint.A, RimAnchors[Constraint.A], 0.0, Constraint.Rest, Compliance, Constraint.Lambda);
                continue;
            }
            // 📝 Two particles: A takes the standard update, B the same Δλ along the updated direction.
            const double Before = Constraint.Lambda;
            Solve(Constraint.A, Predicted[Constraint.B], InverseMass[Constraint.B], Constraint.Rest, Compliance, Constraint.Lambda);
            const double Delta[3] = { Predicted[Constraint.B][0] - Predicted[Constraint.A][0], Predicted[Constraint.B][1] - Predicted[Constraint.A][1],
                                      Predicted[Constraint.B][2] - Predicted[Constraint.A][2] };
            const double Length   = std::sqrt(Delta[0] * Delta[0] + Delta[1] * Delta[1] + Delta[2] * Delta[2]);
            if (Length > 1.0e-12)
            {
                const double Scale = (Constraint.Lambda - Before) * InverseMass[Constraint.B] / Length;
                for (int Axis = 0; Axis < 3; ++Axis) Predicted[Constraint.B][Axis] += Scale * Delta[Axis];
            }
        }
        // 📝 Volume C = V − V₀ with the air's compliance: Δλ = (−C − α̃ λ) / (Σ w |∇C|² + α̃).
        {
            const double Volume     = VolumeSign * ComputeVolume(Predicted, Gradients);
            const double C          = Volume - RestVolume;
            const double AlphaTilde = VolumeCompliance / (Dt * Dt);
            double       Denominator = AlphaTilde;
            for (int Index = 0; Index < ParticleCount; ++Index)
                Denominator += InverseMass[Index] * (Gradients[Index * 3] * Gradients[Index * 3] + Gradients[Index * 3 + 1] * Gradients[Index * 3 + 1] +
                                                     Gradients[Index * 3 + 2] * Gradients[Index * 3 + 2]);
            const double Change = (-C - AlphaTilde * VolumeLambda) / Denominator;
            VolumeLambda += Change;
            for (int Index = 0; Index < ParticleCount; ++Index)
                for (int Axis = 0; Axis < 3; ++Axis) Predicted[Index][Axis] += Change * InverseMass[Index] * VolumeSign * Gradients[Index * 3 + Axis];
        }
        // 📝 Road contact: positional projection along the normal, friction against the particle's slip travel (hub travel plus
        //    spin surface motion, so a freely rolling tread sticks), capped at μ × the normal correction.
        if (!Contact.Ground) continue;
        for (int Index = 0; Index < ParticleCount; ++Index)
        {
            const double Offset[3]   = { GroundPoint[0] - Predicted[Index][0], GroundPoint[1] - Predicted[Index][1], GroundPoint[2] - Predicted[Index][2] };
            const double Penetration = Offset[0] * Normal[0] + Offset[1] * Normal[1] + Offset[2] * Normal[2];
            if (Penetration <= 0.0) continue;
            Touched[Index] = true;
            double Correction[3] = { Normal[0] * Penetration, Normal[1] * Penetration, Normal[2] * Penetration };
            // ω ŷ × x = ω (z, 0, −x)
            const double Travel[3] = { (HubVelocity[0] + Spin * Predicted[Index][2]) * Dt, HubVelocity[1] * Dt,
                                       (HubVelocity[2] - Spin * Predicted[Index][0]) * Dt };
            const double Along     = Travel[0] * Normal[0] + Travel[1] * Normal[1] + Travel[2] * Normal[2];
            const double Tangent[3] = { Travel[0] - Normal[0] * Along, Travel[1] - Normal[1] * Along, Travel[2] - Normal[2] * Along };
            const double TangentLength = std::sqrt(Tangent[0] * Tangent[0] + Tangent[1] * Tangent[1] + Tangent[2] * Tangent[2]);
            if (TangentLength > 1.0e-12)
            {
                const double Scale = -std::min(TangentLength, Tuning.Friction * Penetration) / TangentLength;
                for (int Axis = 0; Axis < 3; ++Axis) Correction[Axis] += Tangent[Axis] * Scale;
            }
            for (int Axis = 0; Axis < 3; ++Axis) Predicted[Index][Axis] += Correction[Axis];
            Impulse += Penetration / (InverseMass[Index] * Dt);   // [N·s] m Δx / Δt over every sweep
        }
    }

    // ③ velocities from positions, with the carcass's hysteresis damping
    const double Damping = std::exp(-Tuning.DampingPerSecond * Dt);
    for (int Index = 0; Index < ParticleCount; ++Index)
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            Velocities[Index][Axis] = (Predicted[Index][Axis] - Positions[Index][Axis]) / Dt * Damping;
            Positions[Index][Axis]  = Predicted[Index][Axis];
        }

    // 📝 The spokes' reaction on the rim: each spoke's constraint force on its particle is λ ∇C / Δt², the rim feels the opposite.
    double Reaction[3] = { 0.0, 0.0, 0.0 };
    for (int Index = 0; Index < ConstraintCount; ++Index)
    {
        const DistanceConstraint& Constraint = Constraints[static_cast<size_t>(Index)];
        if (Constraint.B >= 0) continue;
        const double* Anchor  = RimAnchors[Constraint.A];
        const double  Delta[3] = { Positions[Constraint.A][0] - Anchor[0], Positions[Constraint.A][1] - Anchor[1], Positions[Constraint.A][2] - Anchor[2] };
        const double  Length   = std::sqrt(Delta[0] * Delta[0] + Delta[1] * Delta[1] + Delta[2] * Delta[2]);
        if (Length < 1.0e-12) continue;
        const double Scale = -Constraint.Lambda / (Dt * Dt) / Length;
        for (int Axis = 0; Axis < 3; ++Axis) Reaction[Axis] += Scale * Delta[Axis];
    }
    Turn(Contact.HubRotation, Reaction, Groundward);
    ContactLoad = Impulse / Dt;
    Touching    = 0;
    PatchSum[0] = PatchSum[1] = PatchSum[2] = 0.0;
    for (int Index = 0; Index < ParticleCount; ++Index)
    {
        if (!Touched[Index]) continue;
        double World[3];
        Turn(Contact.HubRotation, Positions[Index], World);
        PatchSum[0] += World[0] + Contact.HubPosition.x;
        PatchSum[1] += World[1] + Contact.HubPosition.y;
        PatchSum[2] += World[2] + Contact.HubPosition.z;
        ++Touching;
    }
    return Reaction[0] * Normal[0] + Reaction[1] * Normal[1] + Reaction[2] * Normal[2];
}

CarcassResponse TyreCarcassLattice::Advance(const CarcassContact& Contact)
{
    CarcassResponse Response;
    const int Steps = std::max(1, Tuning.Substeps);
    const double Dt = Contact.Seconds / Steps;
    if (!(Dt > 0.0)) return Response;
    double   LoadSum = 0.0, PatchSum[3] = { 0.0, 0.0, 0.0 };
    int      Touching = 0;
    for (int Step = 0; Step < Steps; ++Step)
    {
        double Groundward[3], ContactLoad = 0.0, Patch[3];
        int    Count = 0;
        LoadSum += SubStep(Contact, Dt, Groundward, ContactLoad, Count, Patch);
        if (Step + 1 == Steps)
        {
            Touching = Count;
            PatchSum[0] = Patch[0]; PatchSum[1] = Patch[1]; PatchSum[2] = Patch[2];
        }
    }
    Response.NormalLoad     = static_cast<float>(std::max(0.0, LoadSum / Steps));
    Response.ContactCount   = static_cast<uint32_t>(Touching);
    Response.EnclosedVolume = static_cast<float>(QueryVolume());
    if (Touching > 0)
        Response.PatchCentre = Vec3{ static_cast<float>(PatchSum[0] / Touching), static_cast<float>(PatchSum[1] / Touching),
                                     static_cast<float>(PatchSum[2] / Touching) };
    if (Contact.Ground)
    {
        // 📝 Deflection: how far the road plane reaches into the undeformed tyre along its normal.
        const Vec3   Offset   = Contact.HubPosition - Contact.GroundPoint;
        const double Height   = Dot(Offset, Contact.GroundNormal);
        const double Deflection = std::clamp(Radius - Height, 0.0, Radius - RimRadius);
        Response.Deflection = static_cast<float>(Deflection);
        Response.Rate       = static_cast<float>((Deflection - PreviousDeflection) / Contact.Seconds);
        PreviousDeflection  = Deflection;
    }
    else
    {
        PreviousDeflection = 0.0;
    }
    return Response;
}

double TyreCarcassLattice::ResolveCompliance(int Category) const noexcept
{
    switch (Category)
    {
        case 0:  return SpokeCompliance;
        case 1:  return BeltCompliance;
        case 2:  return LateralCompliance;
        default: return ShearCompliance;
    }
}

double TyreCarcassLattice::QueryVolume() const
{
    return VolumeSign * ComputeVolume(Positions, nullptr);
}

std::array<double, 3> TyreCarcassLattice::QueryParticle(int Index) const
{
    return { Positions[Index][0], Positions[Index][1], Positions[Index][2] };
}

std::array<double, 3> TyreCarcassLattice::QueryRestParticle(int Index) const
{
    return { RestPositions[Index][0], RestPositions[Index][1], RestPositions[Index][2] };
}

void TyreCarcassLattice::SampleField(float SpinAngle, CarcassField& Field) const
{
    Field.fill(0.0f);
    const double Cos = std::cos(SpinAngle), Sin = std::sin(SpinAngle);
    for (uint32_t Sector = 0; Sector < CarcassSectorCount; ++Sector)
    {
        // 📝 A material point at angle φ sits at wheel angle φ − spin (a positive spin turns +x toward −z).
        const double Material = 2.0 * Pi * Sector / CarcassSectorCount;
        double       Wheel    = std::fmod(Material - SpinAngle, 2.0 * Pi);
        if (Wheel < 0.0) Wheel += 2.0 * Pi;
        const double Position = Wheel / (2.0 * Pi) * SectorCount;
        const int    Lower    = static_cast<int>(std::floor(Position)) % SectorCount;
        const int    Upper    = (Lower + 1) % SectorCount;
        const double Blend    = Position - std::floor(Position);
        for (int Row = 0; Row < RowCount; ++Row)
        {
            const int    A = ParticleIndex(Lower, Row), B = ParticleIndex(Upper, Row);
            double       Displacement[3];
            for (int Axis = 0; Axis < 3; ++Axis)
                Displacement[Axis] = (1.0 - Blend) * (Positions[A][Axis] - RestPositions[A][Axis]) + Blend * (Positions[B][Axis] - RestPositions[B][Axis]);
            const uint32_t Point = Sector * CarcassRingCount + static_cast<uint32_t>(Row + 1);
            Field[Point * 3 + 0] = static_cast<float>(Cos * Displacement[0] - Sin * Displacement[2]);
            Field[Point * 3 + 1] = static_cast<float>(Displacement[1]);
            Field[Point * 3 + 2] = static_cast<float>(Sin * Displacement[0] + Cos * Displacement[2]);
        }
    }
}

} // namespace Frontier::Vehicle
