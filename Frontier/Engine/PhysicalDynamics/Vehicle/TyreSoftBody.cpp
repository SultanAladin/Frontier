/*==========================================================================================================================================
                                                             TYRESOFTBODY.CPP
==========================================================================================================================================*/
// 🧩 Implementation of Spring, 32x5 XPBD compliant-distance carcass, and 128-Hex8 3D Neo-Hookean FEM tyre physics models.

#include "Engine/PhysicalDynamics/Vehicle/TyreSoftBody.h"

namespace Frontier::Vehicle
{

namespace
{

[[nodiscard]] constexpr uint32_t CarcassNodeIndex(uint32_t Sector, uint32_t Ring) noexcept
{
    return (Sector & (TyreSectorCount - 1u)) * TyreRingCount + Ring;
}

// 📝 Cofactor columns of 3x3 matrix M = [Col0 | Col1 | Col2].
void ComputeCofactorColumns(const Vec3& Col0, const Vec3& Col1, const Vec3& Col2,
                            Vec3& Cof0, Vec3& Cof1, Vec3& Cof2) noexcept
{
    Cof0 = Cross(Col1, Col2);
    Cof1 = Cross(Col2, Col0);
    Cof2 = Cross(Col0, Col1);
}

// 📝 Invert 3x3 Jacobian matrix J = [Col0 | Col1 | Col2] into rows of J^{-1}.
bool InvertJacobian3x3(const Vec3& Col0, const Vec3& Col1, const Vec3& Col2,
                       Vec3& InvRow0, Vec3& InvRow1, Vec3& InvRow2, float& OutDet) noexcept
{
    Vec3 Cof0{}, Cof1{}, Cof2{};
    ComputeCofactorColumns(Col0, Col1, Col2, Cof0, Cof1, Cof2);
    OutDet = Dot(Col0, Cof0);
    if (std::fabs(OutDet) < 1.0e-12f) return false;
    const float InvDet = 1.0f / OutDet;
    // J^{-1} = (Cof)^T / det(J), so row i of J^{-1} is Cof_i / det(J).
    InvRow0 = Cof0 * InvDet;
    InvRow1 = Cof1 * InvDet;
    InvRow2 = Cof2 * InvDet;
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              LIFECYCLE & REFERENCE MESH
//------------------------------------------------------------------------------------------------------------------------

TyreSoftBody::TyreSoftBody() noexcept
{
    Configure(TyreFormulation::Spring, TyreSoftBodyParameters{});
}

TyreSoftBody::TyreSoftBody(TyreFormulation InitialFormulation, const TyreSoftBodyParameters& Parameters) noexcept
{
    Configure(InitialFormulation, Parameters);
}

void TyreSoftBody::Configure(TyreFormulation RequestedFormulation, const TyreSoftBodyParameters& Parameters) noexcept
{
    ActiveFormulation = RequestedFormulation;
    Params            = Parameters;
    BuildReferenceGeometry();
    Reset();
}

void TyreSoftBody::Reset() noexcept
{
    CurrentOuterNodes       = RestOuterNodes;
    OuterVelocities.fill(Vec3{0.0f, 0.0f, 0.0f});
    NodalDisplacements.fill(Vec3{0.0f, 0.0f, 0.0f});
    SolvedCarcassLoadN      = 0.0f;
    LastReading             = TyreSoftBodyReading{};
    LastReading.Formulation = ActiveFormulation;
    LastReading.LoadedRadiusMetres = Params.OuterRadiusMetres;
    LastReading.EnclosedVolumeM3   = Params.ReferenceVolumeM3;
}

Vec3 TyreSoftBody::QueryRestNodeLocal(uint32_t Sector, uint32_t Ring) const noexcept
{
    return RestOuterNodes[CarcassNodeIndex(Sector, std::min(Ring, TyreRingCount - 1u))];
}

Vec3 TyreSoftBody::QueryDisplacementLocal(uint32_t Sector, uint32_t Ring) const noexcept
{
    return NodalDisplacements[CarcassNodeIndex(Sector, std::min(Ring, TyreRingCount - 1u))];
}

void TyreSoftBody::WriteCarcassDisplacements(float* DestinationFloat4x160) const noexcept
{
    if (DestinationFloat4x160 == nullptr) return;
    for (uint32_t Index = 0u; Index < TyreNodesPerWheel; ++Index)
    {
        const Vec3& Disp                     = NodalDisplacements[Index];
        DestinationFloat4x160[Index * 4u + 0u] = Disp.x;
        DestinationFloat4x160[Index * 4u + 1u] = Disp.y;
        DestinationFloat4x160[Index * 4u + 2u] = Disp.z;
        DestinationFloat4x160[Index * 4u + 3u] = 0.0f;
    }
}

void TyreSoftBody::BuildReferenceGeometry() noexcept
{
    const float RingRadii[TyreRingCount] = {
        Params.RimRadiusMetres,
        Params.ShoulderRadiusMetres,
        Params.OuterRadiusMetres,
        Params.ShoulderRadiusMetres,
        Params.RimRadiusMetres};

    const float InnerRingRadii[TyreRingCount] = {
        Params.RimRadiusMetres * 0.96f,
        Params.RimRadiusMetres,
        Params.RimRadiusMetres,
        Params.RimRadiusMetres,
        Params.RimRadiusMetres * 0.96f};

    const float RingLateralFactors[TyreRingCount] = {-1.0f, -0.55f, 0.0f, 0.55f, 1.0f};

    for (uint32_t Sector = 0u; Sector < TyreSectorCount; ++Sector)
    {
        const float Angle = TwoPiConstant * static_cast<float>(Sector) / static_cast<float>(TyreSectorCount);
        const float CosA  = std::cos(Angle);
        const float SinA  = std::sin(Angle);

        for (uint32_t Ring = 0u; Ring < TyreRingCount; ++Ring)
        {
            const uint32_t Index  = CarcassNodeIndex(Sector, Ring);
            const float    LatY   = RingLateralFactors[Ring] * Params.CarcassHalfWidthMetres;
            const float    OuterR = RingRadii[Ring];
            const float    InnerR = InnerRingRadii[Ring];

            RestOuterNodes[Index] = {OuterR * CosA, LatY, OuterR * SinA};
            RestInnerNodes[Index] = {InnerR * CosA, LatY * 0.88f, InnerR * SinA};
        }
    }

    // 📝 Build the 352 XPBD compliant-distance constraints across the 32x5 outer carcass lattice:
    //    - 3 * 32 =  96 circumferential hoop edges on mobile rings (m = 1, 2, 3)
    //    - 4 * 32 = 128 meridional sidewall/tread edges (m -> m+1 for m = 0..3)
    //    - 4 * 32 = 128 diagonal belt/sidewall shear edges ((s, m) -> (s+1, m+1) for m = 0..3)
    //    Total = 96 + 128 + 128 = 352 constraints!
    XpbdConstraints.clear();
    XpbdConstraints.reserve(352u);

    auto AppendConstraint = [&](uint32_t NodeA, uint32_t NodeB, float Compliance) {
        XpbdConstraint Edge{};
        Edge.NodeA      = static_cast<uint16_t>(NodeA);
        Edge.NodeB      = static_cast<uint16_t>(NodeB);
        Edge.RestLength = (RestOuterNodes[NodeB] - RestOuterNodes[NodeA]).Length();
        Edge.Compliance = Compliance;
        XpbdConstraints.push_back(Edge);
    };

    for (uint32_t Sector = 0u; Sector < TyreSectorCount; ++Sector)
    {
        const uint32_t NextSector = (Sector + 1u) & (TyreSectorCount - 1u);

        // Circumferential hoop constraints on mobile rings 1, 2, 3.
        AppendConstraint(CarcassNodeIndex(Sector, 1u), CarcassNodeIndex(NextSector, 1u), Params.XpbdShoulderCompliance);
        AppendConstraint(CarcassNodeIndex(Sector, 2u), CarcassNodeIndex(NextSector, 2u), Params.XpbdCrownCompliance);
        AppendConstraint(CarcassNodeIndex(Sector, 3u), CarcassNodeIndex(NextSector, 3u), Params.XpbdShoulderCompliance);

        // Meridional & diagonal shear constraints across bands 0..3.
        for (uint32_t Ring = 0u; Ring < TyreRingCount - 1u; ++Ring)
        {
            const uint32_t A0 = CarcassNodeIndex(Sector, Ring);
            const uint32_t A1 = CarcassNodeIndex(Sector, Ring + 1u);
            const uint32_t B1 = CarcassNodeIndex(NextSector, Ring + 1u);
            AppendConstraint(A0, A1, Params.XpbdSidewallCompliance);
            AppendConstraint(A0, B1, Params.XpbdShearCompliance);
        }
    }

    // 📝 Build the 128 Hex8 finite elements (32 sectors x 4 meridional bands):
    //    Nodes 0..3 sit on the pinned inner rim surface [0..159], Nodes 4..7 sit on the mobile outer surface [160..319].
    static constexpr float NatXi[8]   = {-1.0f, +1.0f, +1.0f, -1.0f, -1.0f, +1.0f, +1.0f, -1.0f};
    static constexpr float NatEta[8]  = {-1.0f, -1.0f, +1.0f, +1.0f, -1.0f, -1.0f, +1.0f, +1.0f};
    static constexpr float NatZeta[8] = {-1.0f, -1.0f, -1.0f, -1.0f, +1.0f, +1.0f, +1.0f, +1.0f};

    for (uint32_t Sector = 0u; Sector < TyreSectorCount; ++Sector)
    {
        const uint32_t NextSector = (Sector + 1u) & (TyreSectorCount - 1u);
        for (uint32_t Band = 0u; Band < 4u; ++Band)
        {
            HexCellReference& Cell = HexCells[Sector * 4u + Band];
            const uint16_t    I00  = static_cast<uint16_t>(CarcassNodeIndex(Sector, Band));
            const uint16_t    I10  = static_cast<uint16_t>(CarcassNodeIndex(NextSector, Band));
            const uint16_t    I11  = static_cast<uint16_t>(CarcassNodeIndex(NextSector, Band + 1u));
            const uint16_t    I01  = static_cast<uint16_t>(CarcassNodeIndex(Sector, Band + 1u));

            // Right-handed Hex8 corner ordering: xi along Eta-band (Y), eta along Sector (phi), zeta outward (R),
            // so det(dX / d(xi, eta, zeta)) > 0 on every sector and band.
            Cell.Nodes = {
                I00,
                I01,
                I11,
                I10,
                static_cast<uint16_t>(TyreNodesPerWheel + I00),
                static_cast<uint16_t>(TyreNodesPerWheel + I01),
                static_cast<uint16_t>(TyreNodesPerWheel + I11),
                static_cast<uint16_t>(TyreNodesPerWheel + I10)};

            Vec3 CornerPositions[8]{};
            for (uint32_t Corner = 0u; Corner < 4u; ++Corner)
            {
                CornerPositions[Corner]      = RestInnerNodes[Cell.Nodes[Corner]];
                CornerPositions[Corner + 4u] = RestOuterNodes[Cell.Nodes[Corner + 4u] - TyreNodesPerWheel];
            }

            // Evaluate reference Jacobian dX / d(xi, eta, zeta) at element centroid (0, 0, 0).
            Vec3 dXdXi{}, dXdEta{}, dXdZeta{};
            for (uint32_t Corner = 0u; Corner < 8u; ++Corner)
            {
                dXdXi   += CornerPositions[Corner] * (0.125f * NatXi[Corner]);
                dXdEta  += CornerPositions[Corner] * (0.125f * NatEta[Corner]);
                dXdZeta += CornerPositions[Corner] * (0.125f * NatZeta[Corner]);
            }

            Vec3  InvRow0{}, InvRow1{}, InvRow2{};
            float DetJ = 0.0f;
            if (!InvertJacobian3x3(dXdXi, dXdEta, dXdZeta, InvRow0, InvRow1, InvRow2, DetJ))
            {
                Cell.RestVolume = 1.0e-6f;
                continue;
            }

            // 📝 InvertJacobian3x3 already divided by signed DetJ so sum_a X_a (x) grad_X N_a = +I holds identically;
            //    use |DetJ| strictly for the positive physical Hex8 volume V_0e = 8 * |DetJ|.
            Cell.RestVolume = 8.0f * std::fabs(DetJ);

            // grad_X N_a = J^{-T} * grad_xi N_a.
            for (uint32_t Corner = 0u; Corner < 8u; ++Corner)
            {
                const Vec3 GradNatural{0.125f * NatXi[Corner], 0.125f * NatEta[Corner], 0.125f * NatZeta[Corner]};
                Cell.ShapeGradients[Corner] = {
                    InvRow0.x * GradNatural.x + InvRow1.x * GradNatural.y + InvRow2.x * GradNatural.z,
                    InvRow0.y * GradNatural.x + InvRow1.y * GradNatural.y + InvRow2.y * GradNatural.z,
                    InvRow0.z * GradNatural.x + InvRow1.z * GradNatural.y + InvRow2.z * GradNatural.z};
            }
        }
    }

    CurrentOuterNodes       = RestOuterNodes;
    ReferencePolygonAreaSum = 0.0f;
    // Measure rest cross-section polygon area sum so ComputeEnclosedTorusVolume() normalizes cleanly to ReferenceVolumeM3.
    ReferencePolygonAreaSum = 1.0f;
    ReferencePolygonAreaSum = std::max(ComputeEnclosedTorusVolume(), 1.0e-6f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              ENCLOSED CAVITY VOLUME
//------------------------------------------------------------------------------------------------------------------------

float TyreSoftBody::ComputeEnclosedTorusVolume() const noexcept
{
    float AreaCentroidProductSum = 0.0f;
    for (uint32_t Sector = 0u; Sector < TyreSectorCount; ++Sector)
    {
        const float Angle = TwoPiConstant * static_cast<float>(Sector) / static_cast<float>(TyreSectorCount);
        const Vec3  RadialAxis{std::cos(Angle), 0.0f, std::sin(Angle)};

        // 2D cross-section polygon in (r, y) across rings 0..4 closed along the rim base.
        float RingR[TyreRingCount]{};
        float RingY[TyreRingCount]{};
        for (uint32_t Ring = 0u; Ring < TyreRingCount; ++Ring)
        {
            const Vec3& Pos = CurrentOuterNodes[CarcassNodeIndex(Sector, Ring)];
            RingR[Ring]     = Dot(Pos, RadialAxis);
            RingY[Ring]     = Pos.y;
        }

        float SignedAreaTwice = 0.0f;
        float MeanRadius      = 0.0f;
        for (uint32_t Ring = 0u; Ring < TyreRingCount; ++Ring)
        {
            const uint32_t Next = (Ring + 1u) % TyreRingCount;
            SignedAreaTwice += (RingY[Ring] * RingR[Next] - RingY[Next] * RingR[Ring]);
            MeanRadius      += RingR[Ring];
        }
        MeanRadius /= static_cast<float>(TyreRingCount);
        AreaCentroidProductSum += std::fabs(0.5f * SignedAreaTwice) * std::max(MeanRadius, 0.1f);
    }

    if (ReferencePolygonAreaSum <= 1.00001f && ReferencePolygonAreaSum >= 0.99999f)
    {
        return AreaCentroidProductSum;
    }
    return Params.ReferenceVolumeM3 * (AreaCentroidProductSum / ReferencePolygonAreaSum);
}

//------------------------------------------------------------------------------------------------------------------------
//                                    FORMULATION 1: ANALYTICAL SPRING FIELD (PATH B)
//------------------------------------------------------------------------------------------------------------------------

void TyreSoftBody::EvaluateAnalyticalSpringField(float ContactAngle, float Deflection, float GaugePressure, float LateralWalk) noexcept
{
    const float PressureNorm = std::max(0.05f, GaugePressure / std::max(Params.ReferencePressurePa, 1.0f));
    const float PatchSpan    = ClampScalar(0.44f / std::sqrt(std::max(0.15f, PressureNorm)), 0.20f, 1.15f);
    const float SidewallSpan = std::max(Params.OuterRadiusMetres - Params.RimRadiusMetres, 1.0e-3f);

    for (uint32_t Sector = 0u; Sector < TyreSectorCount; ++Sector)
    {
        const float Phi      = TwoPiConstant * static_cast<float>(Sector) / static_cast<float>(TyreSectorCount);
        const float DeltaPhi = WrapAngleRadians(Phi - ContactAngle);
        const float AbsPhi   = std::fabs(DeltaPhi);

        for (uint32_t Ring = 0u; Ring < TyreRingCount; ++Ring)
        {
            const uint32_t Index = CarcassNodeIndex(Sector, Ring);
            const Vec3&    Rest  = RestOuterNodes[Index];
            if (Ring == 0u || Ring == 4u || Deflection <= 1.0e-5f)
            {
                CurrentOuterNodes[Index]  = Rest;
                NodalDisplacements[Index] = {0.0f, 0.0f, 0.0f};
                continue;
            }

            const float RestR      = std::sqrt(Rest.x * Rest.x + Rest.z * Rest.z);
            const float RadialFrac = ClampScalar((RestR - Params.RimRadiusMetres) / SidewallSpan, 0.0f, 1.0f);

            float DeltaR = 0.0f;
            float DeltaY = 0.0f;
            if (AbsPhi < PatchSpan)
            {
                const float U          = AbsPhi / PatchSpan;
                const float Envelope   = 0.5f * (1.0f + std::cos(U * PiConstant));
                DeltaR                 = -Deflection * Envelope * RadialFrac;
                const float SidewallF  = std::sin(RadialFrac * PiConstant);
                const float BulgeGain  = 0.48f / (0.4f + 0.6f * std::sqrt(PressureNorm));
                DeltaY                 = SignScalar(Rest.y) * (Deflection * BulgeGain * Envelope * SidewallF);
            }
            else if (AbsPhi < PatchSpan * 1.5f)
            {
                const float V = (AbsPhi - PatchSpan) / (0.5f * PatchSpan);
                DeltaR        = std::sin(V * PiConstant) * (0.12f * Deflection * RadialFrac);
            }

            if (AbsPhi < PatchSpan * 1.2f)
            {
                const float ShearEnv = 0.5f * (1.0f + std::cos((AbsPhi / (PatchSpan * 1.2f)) * PiConstant));
                DeltaY += LateralWalk * ShearEnv * RadialFrac;
            }

            const float NewR          = std::max(Params.RimRadiusMetres, RestR + DeltaR);
            const Vec3  Deformed      {NewR * std::cos(Phi), Rest.y + DeltaY, NewR * std::sin(Phi)};
            CurrentOuterNodes[Index]  = Deformed;
            NodalDisplacements[Index] = Deformed - Rest;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                  FORMULATION 2: 32x5 XPBD COMPLIANT-DISTANCE CARCASS
//------------------------------------------------------------------------------------------------------------------------

void TyreSoftBody::StepXpbdCarcass(const Vec3& DownLocal, float Penetration, float GaugePressure, float LateralForceN,
                                   float LongitudinalForceN, float WheelOmega, float Dt) noexcept
{
    const uint32_t Substeps       = std::max(1u, Params.XpbdSubsteps);
    const uint32_t Iterations     = std::max(1u, Params.XpbdIterations);
    const float    SubDt          = Dt / static_cast<float>(Substeps);
    const float    SubDtSq        = SubDt * SubDt;
    const float    InvMassMobile  = 1.0f / std::max(Params.XpbdNodeMassKg, 1.0e-3f);
    const float    PressureRatio  = ClampScalar(GaugePressure / std::max(Params.ReferencePressurePa, 1.0f), 0.10f, 3.5f);
    const float    ComplianceGain = 1.0f / (0.28f + 0.72f * PressureRatio);
    const float    GroundLimit    = Params.OuterRadiusMetres - Penetration;
    const float    DampingFactor  = std::exp(-28.0f * SubDt);

    // Tangential forward direction in wheel-local plane perpendicular to DownLocal.
    const Vec3  AxleLocal{0.0f, 1.0f, 0.0f};
    const Vec3  TangentLocal      = Cross(AxleLocal, DownLocal).Normalized();
    const float LateralShearForce = ClampScalar(LateralForceN * 0.018f, -160.0f, 160.0f);
    const float LongShearForce    = ClampScalar(LongitudinalForceN * 0.008f, -90.0f, 90.0f);

    std::array<Vec3, TyreNodesPerWheel> PreviousNodes{};
    std::vector<float>                  Lambdas(XpbdConstraints.size(), 0.0f);
    float                               AccumulatedContactDepth = 0.0f;
    uint32_t                            ContactingNodes         = 0u;

    for (uint32_t Sub = 0u; Sub < Substeps; ++Sub)
    {
        PreviousNodes = CurrentOuterNodes;
        std::fill(Lambdas.begin(), Lambdas.end(), 0.0f);

        // 1. Symplectic prediction with pneumatic cavity outward load, centrifugal growth, and rest-shape tether.
        for (uint32_t Sector = 0u; Sector < TyreSectorCount; ++Sector)
        {
            const float Angle = TwoPiConstant * static_cast<float>(Sector) / static_cast<float>(TyreSectorCount);
            const Vec3  RadialOut{std::cos(Angle), 0.0f, std::sin(Angle)};

            for (uint32_t Ring = 0u; Ring < TyreRingCount; ++Ring)
            {
                const uint32_t Index = CarcassNodeIndex(Sector, Ring);
                if (Ring == 0u || Ring == 4u)
                {
                    CurrentOuterNodes[Index] = RestOuterNodes[Index];
                    OuterVelocities[Index]   = {0.0f, 0.0f, 0.0f};
                    continue;
                }

                // Outward inflation + carcass shape restoring spring + centrifugal belt tension.
                const Vec3  Displacement     = CurrentOuterNodes[Index] - RestOuterNodes[Index];
                const float CentrifugalForce = Params.XpbdNodeMassKg * WheelOmega * WheelOmega * Params.OuterRadiusMetres * 0.06f;
                const float PneumaticOut     = (PressureRatio - 1.0f) * 38.0f + CentrifugalForce;
                const float TetherStiffness  = 3200.0f * (0.30f + 0.70f * PressureRatio);
                Vec3        ExternalForce    = RadialOut * PneumaticOut - Displacement * TetherStiffness;

                // Apply lateral & longitudinal road-adhesion shear to nodes near the contact patch.
                const float DepthTest = Dot(CurrentOuterNodes[Index], DownLocal) - (GroundLimit - 0.015f);
                if (Penetration > 1.0e-5f && DepthTest > 0.0f)
                {
                    const float PatchWeight = ClampScalar(DepthTest / 0.025f, 0.0f, 1.0f);
                    ExternalForce.y += LateralShearForce * PatchWeight;
                    ExternalForce   += TangentLocal * (LongShearForce * PatchWeight);
                }

                OuterVelocities[Index]   = (OuterVelocities[Index] + ExternalForce * (InvMassMobile * SubDt)) * DampingFactor;
                CurrentOuterNodes[Index] = CurrentOuterNodes[Index] + OuterVelocities[Index] * SubDt;
            }
        }

        // 2. XPBD Gauss-Seidel compliant distance projection + unilateral ground contact + incompressible sidewall bulge coupling.
        for (uint32_t Iter = 0u; Iter < Iterations; ++Iter)
        {
            for (size_t EdgeIdx = 0u; EdgeIdx < XpbdConstraints.size(); ++EdgeIdx)
            {
                const XpbdConstraint& Edge  = XpbdConstraints[EdgeIdx];
                const uint32_t        RingA = Edge.NodeA % TyreRingCount;
                const uint32_t        RingB = Edge.NodeB % TyreRingCount;
                const float           WA    = (RingA == 0u || RingA == 4u) ? 0.0f : InvMassMobile;
                const float           WB    = (RingB == 0u || RingB == 4u) ? 0.0f : InvMassMobile;
                const float           WSum  = WA + WB;
                if (WSum <= 0.0f) continue;

                const Vec3  Diff = CurrentOuterNodes[Edge.NodeB] - CurrentOuterNodes[Edge.NodeA];
                const float Dist = Diff.Length();
                if (Dist < 1.0e-8f) continue;

                const float ConstraintErr = Dist - Edge.RestLength;
                const float AlphaTilde    = (Edge.Compliance * ComplianceGain) / SubDtSq;
                const float DeltaLambda   = (-ConstraintErr - AlphaTilde * Lambdas[EdgeIdx]) / (WSum + AlphaTilde);
                Lambdas[EdgeIdx] += DeltaLambda;

                const Vec3 Grad = Diff * (1.0f / Dist);
                if (WA > 0.0f) CurrentOuterNodes[Edge.NodeA] -= Grad * (WA * DeltaLambda);
                if (WB > 0.0f) CurrentOuterNodes[Edge.NodeB] += Grad * (WB * DeltaLambda);
            }

            // Unilateral ground contact + pneumatic cross-section area conservation pushing shoulders (m = 1, 3) outward.
            if (Penetration > 1.0e-6f)
            {
                for (uint32_t Sector = 0u; Sector < TyreSectorCount; ++Sector)
                {
                    float CrownPushInward = 0.0f;
                    for (uint32_t Ring = 1u; Ring <= 3u; ++Ring)
                    {
                        const uint32_t Index = CarcassNodeIndex(Sector, Ring);
                        const float    Proj  = Dot(CurrentOuterNodes[Index], DownLocal);
                        if (Proj > GroundLimit)
                        {
                            const float Excess = Proj - GroundLimit;
                            CurrentOuterNodes[Index] -= DownLocal * Excess;
                            if (Ring == 2u) CrownPushInward = Excess;
                        }
                    }

                    // Cross-sectional pneumatic cavity area conservation: inward crown compression drives shoulder bulge +-Y.
                    if (CrownPushInward > 1.0e-6f)
                    {
                        const float BulgeStep = CrownPushInward * (0.24f / std::sqrt(std::max(0.25f, PressureRatio)));
                        CurrentOuterNodes[CarcassNodeIndex(Sector, 1u)].y -= BulgeStep;
                        CurrentOuterNodes[CarcassNodeIndex(Sector, 3u)].y += BulgeStep;
                    }
                }
            }
        }

        // 3. Update XPBD velocities from solved positions.
        const float InvSubDt = 1.0f / SubDt;
        for (uint32_t Sector = 0u; Sector < TyreSectorCount; ++Sector)
        {
            for (uint32_t Ring = 1u; Ring <= 3u; ++Ring)
            {
                const uint32_t Index   = CarcassNodeIndex(Sector, Ring);
                OuterVelocities[Index] = (CurrentOuterNodes[Index] - PreviousNodes[Index]) * InvSubDt;
            }
        }
    }

    // Measure XPBD carcass reaction from deformed rim-spoke / tread strain + pneumatic contact patch area.
    ContactingNodes         = 0u;
    AccumulatedContactDepth = 0.0f;
    for (uint32_t Sector = 0u; Sector < TyreSectorCount; ++Sector)
    {
        for (uint32_t Ring = 1u; Ring <= 3u; ++Ring)
        {
            const uint32_t Index     = CarcassNodeIndex(Sector, Ring);
            NodalDisplacements[Index]= CurrentOuterNodes[Index] - RestOuterNodes[Index];
            const float Inward       = -Dot(NodalDisplacements[Index], DownLocal);
            if (Penetration > 1.0e-6f && Inward > 1.0e-4f && Dot(RestOuterNodes[Index], DownLocal) > 0.35f * Params.OuterRadiusMetres)
            {
                AccumulatedContactDepth += Inward;
                ++ContactingNodes;
            }
        }
    }

    const float PressureStiffFactor = (1.0f - Params.PressureStiffnessShare) + Params.PressureStiffnessShare * PressureRatio;
    const float PatchLoad           = Params.VerticalStiffnessNpm * PressureStiffFactor *
                            (ContactingNodes > 0u ? (AccumulatedContactDepth / static_cast<float>(ContactingNodes)) * 1.38f : 0.0f);
    SolvedCarcassLoadN              = std::max(0.0f, PatchLoad);
}

//------------------------------------------------------------------------------------------------------------------------
//                           FORMULATION 3: 128-HEX8 3D NEO-HOOKEAN HYPERELASTIC SOLID CARCASS
//------------------------------------------------------------------------------------------------------------------------

void TyreSoftBody::StepFem3DCarcass(const Vec3& DownLocal, float Penetration, float GaugePressure, float LateralForceN,
                                    float LongitudinalForceN, float WheelOmega, float Dt) noexcept
{
    const uint32_t Substeps      = std::max(1u, Params.FemSubsteps);
    const float    SubDt         = Dt / static_cast<float>(Substeps);
    const float    PressureRatio = ClampScalar(GaugePressure / std::max(Params.ReferencePressurePa, 1.0f), 0.10f, 3.5f);
    const float    YoungsE       = Params.FemBaseYoungsModulusPa * (0.30f + 0.70f * PressureRatio);
    const float    PoissonNu     = ClampScalar(Params.FemPoissonRatio, 0.10f, 0.475f);
    const float    LameMu        = YoungsE / (2.0f * (1.0f + PoissonNu));
    const float    LameLambda    = (YoungsE * PoissonNu) / ((1.0f + PoissonNu) * (1.0f - 2.0f * PoissonNu));
    const float    InvNodeMass   = 1.0f / std::max(Params.FemNodeMassKg, 1.0e-3f);
    const float    DampingDecay  = std::exp(-Params.FemDampingPerSecond * SubDt);
    const float    GroundLimit   = Params.OuterRadiusMetres - Penetration;

    const Vec3  AxleLocal{0.0f, 1.0f, 0.0f};
    const Vec3  TangentLocal     = Cross(AxleLocal, DownLocal).Normalized();
    const float LateralTraction  = ClampScalar(LateralForceN * 0.024f, -220.0f, 220.0f);
    const float LongTraction     = ClampScalar(LongitudinalForceN * 0.010f, -110.0f, 110.0f);

    std::array<Vec3, TyreNodesPerWheel> NodalForces{};
    float                               MeanJ        = 1.0f;
    float                               MinJ         = 1.0f;
    float                               MeanPiola    = 0.0f;
    float                               ContactForce = 0.0f;

    for (uint32_t Sub = 0u; Sub < Substeps; ++Sub)
    {
        NodalForces.fill(Vec3{0.0f, 0.0f, 0.0f});
        double SumJ     = 0.0;
        double SumPiola = 0.0;
        MinJ            = 10.0f;

        // 1. Assemble compressible Neo-Hookean hyperelastic internal nodal forces across all 128 Hex8 cells:
        //    F = I + sum_{a=0..7} u_a (x) grad_X N_a
        //    P(F) = mu * F + ((lambda * J * (J - 1) - mu) / J) * cof(F)
        //    f_a -= V_0e * P(F) * grad_X N_a
        for (uint32_t CellIdx = 0u; CellIdx < TyreHexCount; ++CellIdx)
        {
            const HexCellReference& Cell = HexCells[CellIdx];

            // Columns of deformation gradient F = [FCol0 | FCol1 | FCol2], initialized to Identity.
            Vec3 FCol0{1.0f, 0.0f, 0.0f};
            Vec3 FCol1{0.0f, 1.0f, 0.0f};
            Vec3 FCol2{0.0f, 0.0f, 1.0f};

            // Inner nodes 0..3 are pinned to the rigid rim (u_a = 0); outer nodes 4..7 carry u_a = Current - Rest.
            for (uint32_t Corner = 4u; Corner < 8u; ++Corner)
            {
                const uint32_t OuterIdx = Cell.Nodes[Corner] - TyreNodesPerWheel;
                const uint32_t Ring     = OuterIdx % TyreRingCount;
                if (Ring == 0u || Ring == 4u) continue;

                const Vec3 Disp = CurrentOuterNodes[OuterIdx] - RestOuterNodes[OuterIdx];
                const Vec3 Grad = Cell.ShapeGradients[Corner];
                FCol0 += Disp * Grad.x;
                FCol1 += Disp * Grad.y;
                FCol2 += Disp * Grad.z;
            }

            Vec3 Cof0{}, Cof1{}, Cof2{};
            ComputeCofactorColumns(FCol0, FCol1, FCol2, Cof0, Cof1, Cof2);
            const float RawJ     = Dot(FCol0, Cof0);
            const float SafeJ    = std::max(RawJ, 0.18f);
            const float VolScale = (LameLambda * SafeJ * (SafeJ - 1.0f) - LameMu) / SafeJ;

            // Columns of first Piola-Kirchhoff stress tensor P = [PCol0 | PCol1 | PCol2].
            const Vec3 PCol0 = FCol0 * LameMu + Cof0 * VolScale;
            const Vec3 PCol1 = FCol1 * LameMu + Cof1 * VolScale;
            const Vec3 PCol2 = FCol2 * LameMu + Cof2 * VolScale;

            const float PiolaNorm = std::sqrt(PCol0.LengthSquared() + PCol1.LengthSquared() + PCol2.LengthSquared());
            SumJ     += static_cast<double>(SafeJ);
            SumPiola += static_cast<double>(PiolaNorm);
            MinJ      = std::min(MinJ, SafeJ);

            for (uint32_t Corner = 4u; Corner < 8u; ++Corner)
            {
                const uint32_t OuterIdx = Cell.Nodes[Corner] - TyreNodesPerWheel;
                const uint32_t Ring     = OuterIdx % TyreRingCount;
                if (Ring == 0u || Ring == 4u) continue;

                const Vec3& Grad          = Cell.ShapeGradients[Corner];
                const Vec3  TractionForce = (PCol0 * Grad.x + PCol1 * Grad.y + PCol2 * Grad.z) * Cell.RestVolume;
                NodalForces[OuterIdx] -= TractionForce;
            }

            // Flanagan-Belytschko selective hourglass control on the 4 outer corners (+1, -1, +1, -1 mode)
            // so 1-point centroid Hex8 quadrature suppresses zero-energy checkerboard modes cleanly.
            const uint32_t O0 = Cell.Nodes[4] - TyreNodesPerWheel;
            const uint32_t O1 = Cell.Nodes[5] - TyreNodesPerWheel;
            const uint32_t O2 = Cell.Nodes[6] - TyreNodesPerWheel;
            const uint32_t O3 = Cell.Nodes[7] - TyreNodesPerWheel;
            const Vec3     HgMode = (CurrentOuterNodes[O0] - RestOuterNodes[O0]) -
                                (CurrentOuterNodes[O1] - RestOuterNodes[O1]) +
                                (CurrentOuterNodes[O2] - RestOuterNodes[O2]) -
                                (CurrentOuterNodes[O3] - RestOuterNodes[O3]);
            const float    HgStiffness = 0.035f * LameMu * std::cbrt(std::max(Cell.RestVolume, 1.0e-8f));
            const Vec3     HgForce     = HgMode * HgStiffness;
            if ((O0 % TyreRingCount) != 0u && (O0 % TyreRingCount) != 4u) NodalForces[O0] -= HgForce;
            if ((O1 % TyreRingCount) != 0u && (O1 % TyreRingCount) != 4u) NodalForces[O1] += HgForce;
            if ((O2 % TyreRingCount) != 0u && (O2 % TyreRingCount) != 4u) NodalForces[O2] -= HgForce;
            if ((O3 % TyreRingCount) != 0u && (O3 % TyreRingCount) != 4u) NodalForces[O3] += HgForce;
        }

        MeanJ        = static_cast<float>(SumJ / static_cast<double>(TyreHexCount));
        MeanPiola    = static_cast<float>(SumPiola / static_cast<double>(TyreHexCount));
        ContactForce = 0.0f;

        // 2. Pneumatic inflation + centrifugal load + road penalty/projection + symplectic integration.
        for (uint32_t Sector = 0u; Sector < TyreSectorCount; ++Sector)
        {
            const float Angle = TwoPiConstant * static_cast<float>(Sector) / static_cast<float>(TyreSectorCount);
            const Vec3  RadialOut{std::cos(Angle), 0.0f, std::sin(Angle)};

            for (uint32_t Ring = 0u; Ring < TyreRingCount; ++Ring)
            {
                const uint32_t Index = CarcassNodeIndex(Sector, Ring);
                if (Ring == 0u || Ring == 4u)
                {
                    CurrentOuterNodes[Index] = RestOuterNodes[Index];
                    OuterVelocities[Index]   = {0.0f, 0.0f, 0.0f};
                    continue;
                }

                const float CentrifugalOut = Params.FemNodeMassKg * WheelOmega * WheelOmega * Params.OuterRadiusMetres * 0.06f;
                const float InflationOut   = (PressureRatio - 1.0f) * 44.0f + CentrifugalOut;
                NodalForces[Index] += RadialOut * InflationOut;

                // Ground contact penalty + lateral/longitudinal shear traction in contact zone.
                const float SignedProj = Dot(CurrentOuterNodes[Index], DownLocal);
                if (Penetration > 1.0e-6f && SignedProj > GroundLimit - 0.012f)
                {
                    const float PatchWeight = ClampScalar((SignedProj - (GroundLimit - 0.012f)) / 0.020f, 0.0f, 1.0f);
                    NodalForces[Index].y += LateralTraction * PatchWeight;
                    NodalForces[Index]   += TangentLocal * (LongTraction * PatchWeight);
                }

                // Clamp extreme force spikes for unconditional explicit stability.
                const float ForceMag = NodalForces[Index].Length();
                if (ForceMag > 6500.0f) NodalForces[Index] *= (6500.0f / ForceMag);

                OuterVelocities[Index]   = (OuterVelocities[Index] + NodalForces[Index] * (InvNodeMass * SubDt)) * DampingDecay;
                CurrentOuterNodes[Index] = CurrentOuterNodes[Index] + OuterVelocities[Index] * SubDt;

                // Unilateral ground contact constraint on outer solid surface + reaction measurement.
                const float NewProj = Dot(CurrentOuterNodes[Index], DownLocal);
                if (Penetration > 1.0e-6f && NewProj > GroundLimit)
                {
                    const float Excess       = NewProj - GroundLimit;
                    CurrentOuterNodes[Index] -= DownLocal * Excess;
                    const float NormalVel    = Dot(OuterVelocities[Index], DownLocal);
                    if (NormalVel > 0.0f) OuterVelocities[Index] -= DownLocal * NormalVel;

                    // Hyperelastic reaction pushed outward into the road (+DownLocal) by the compressed 3D Hex8 elements:
                    const float NodeNormalForce = std::max(0.0f, Dot(NodalForces[Index], DownLocal)) +
                                                  Excess * (YoungsE * 0.015f);
                    ContactForce += NodeNormalForce;
                }
            }
        }
    }

    for (uint32_t Index = 0u; Index < TyreNodesPerWheel; ++Index)
    {
        NodalDisplacements[Index] = CurrentOuterNodes[Index] - RestOuterNodes[Index];
    }

    LastReading.MeanJacobian      = MeanJ;
    LastReading.MinJacobian       = MinJ;
    LastReading.MeanPiolaStressPa = MeanPiola;
    SolvedCarcassLoadN            = std::max(0.0f, ContactForce);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    UNIFIED STEP
//------------------------------------------------------------------------------------------------------------------------

TyreSoftBodyReading TyreSoftBody::Step(const TyreSoftBodyInputs& Inputs) noexcept
{
    TyreSoftBodyReading Out{};
    Out.Formulation        = ActiveFormulation;
    Out.LoadedRadiusMetres = Params.OuterRadiusMetres;
    Out.EnclosedVolumeM3   = Params.ReferenceVolumeM3;

    const float Dt            = ClampScalar(Inputs.DeltaSeconds, 1.0e-5f, 1.0f / 30.0f);
    const float GaugePressure = std::max(0.0f, Inputs.GaugePressurePa);
    const float PressureRatio = ClampScalar(GaugePressure / std::max(Params.ReferencePressurePa, 1.0f), 0.0f, 3.5f);
    const float EffectiveK    = Params.VerticalStiffnessNpm *
                             ((1.0f - Params.PressureStiffnessShare) + Params.PressureStiffnessShare * PressureRatio);
    Out.EffectiveStiffnessNpm = EffectiveK;

    // 1. Cast the 3-slice x 19-ray radial contact fan in world space (GRIT ComputeSuspensionDisplacements).
    const Vec3     ForwardWorld = Inputs.HubOrientationWorld.Rotate({1.0f, 0.0f, 0.0f});
    const Vec3     AxleWorld    = Inputs.HubOrientationWorld.Rotate({0.0f, 1.0f, 0.0f});
    const Vec3     UpWorld      = Inputs.HubOrientationWorld.Rotate({0.0f, 0.0f, 1.0f});
    const int32_t  HalfRays     = static_cast<int32_t>(std::fabs(Params.FanHalfAngleRadians) / std::max(Params.FanStepRadians, 0.02f));
    const uint32_t Slices       = std::max(1u, Params.LateralSliceCount);

    Vec3     SumSurface{};
    Vec3     SumNormal{};
    uint32_t Hits = 0u;
    if (Inputs.Ground && Params.OuterRadiusMetres > 0.05f)
    {
        for (uint32_t Slice = 0u; Slice < Slices; ++Slice)
        {
            const float SliceFraction = Slices == 1u ? 0.5f : static_cast<float>(Slice) / static_cast<float>(Slices - 1u);
            const Vec3  SliceOrigin   = Inputs.HubPositionWorld + AxleWorld * ((SliceFraction - 0.5f) * Params.ContactTreadWidthMetres);
            for (int32_t RayIdx = -HalfRays; RayIdx <= HalfRays; ++RayIdx)
            {
                const float Angle     = static_cast<float>(RayIdx) * Params.FanStepRadians;
                const Vec3  Direction = ForwardWorld * std::sin(Angle) - UpWorld * std::cos(Angle);
                const Vec3  Sample    = SliceOrigin + Direction * Params.OuterRadiusMetres;

                Vec3 Surface{}, Normal{};
                if (!Inputs.Ground(Sample, Surface, Normal)) continue;
                const Vec3 UnitNormal = Normal.Normalized();
                if (UnitNormal.LengthSquared() < 0.5f) continue;
                if (Dot(Surface - Sample, UnitNormal) <= 0.0f) continue;

                SumSurface += Surface;
                SumNormal  += UnitNormal;
                ++Hits;
            }
        }
    }

    // Spun wheel-local frame: R_wheel = R_hub * AxisAngle(+Y, SpinAngle).
    const Quat SpunOrientation = Inputs.HubOrientationWorld * Quat::AxisAngle({0.0f, 1.0f, 0.0f}, Inputs.SpinAngleRadians);

    float Penetration = 0.0f;
    Vec3  NormalWorld = UpWorld;
    if (Hits > 0u)
    {
        const float InvHits    = 1.0f / static_cast<float>(Hits);
        const Vec3  PatchWorld = SumSurface * InvHits;
        NormalWorld            = (SumNormal * InvHits).Normalized();
        if (NormalWorld.LengthSquared() < 0.5f) NormalWorld = UpWorld;

        const Vec3 ImpliedHub  = PatchWorld + NormalWorld * Params.OuterRadiusMetres;
        Penetration            = std::max(0.0f, Dot(ImpliedHub - Inputs.HubPositionWorld, NormalWorld));

        Out.InContact          = Penetration > 0.0f;
        Out.ContactHitCount    = Hits;
        Out.PatchCentreWorld   = PatchWorld;
        Out.ContactNormalWorld = NormalWorld;
    }

    const Vec3 DownLocal     = SpunOrientation.InverseRotate(-NormalWorld).Normalized();
    Out.ContactAngleRadians  = std::atan2(DownLocal.z, DownLocal.x);
    const float MaxSidewall  = std::min(Params.MaxDeflectionMetres, Params.OuterRadiusMetres - Params.RimRadiusMetres - 0.006f);
    if (Penetration >= MaxSidewall)
    {
        Penetration      = MaxSidewall;
        Out.RimBottomed  = true;
    }

    const float CompressionRate     = Out.InContact ? -Dot(Inputs.HubVelocityWorld, NormalWorld) : 0.0f;
    const float LateralWalkEstimate = ClampScalar(
        Inputs.LateralForceEstimateN / std::max(Params.LateralBeltStiffnessNpm * (0.4f + 0.6f * PressureRatio), 10000.0f),
        -0.045f,
        0.045f);

    Out.DeflectionMetres        = Penetration;
    Out.DeflectionRateMps       = CompressionRate;
    Out.LoadedRadiusMetres      = std::max(Params.RimRadiusMetres, Params.OuterRadiusMetres - Penetration);
    Out.LateralDistortionMetres = LateralWalkEstimate;

    // 2. Advance the selected soft-body carcass formulation and compute its normal load Fz + live cavity volume V.
    const float ProgressiveBottoming = Penetration > 0.72f * MaxSidewall
                                           ? (Penetration - 0.72f * MaxSidewall) * EffectiveK * 2.5f
                                           : 0.0f;
    const float DampingLoad          = Params.VerticalDampingNsm * CompressionRate;

    if (ActiveFormulation == TyreFormulation::Spring)
    {
        EvaluateAnalyticalSpringField(Out.ContactAngleRadians, Penetration, GaugePressure, LateralWalkEstimate);
        Out.NormalLoadNewtons = Out.InContact ? std::max(0.0f, EffectiveK * Penetration + ProgressiveBottoming + DampingLoad) : 0.0f;
    }
    else if (ActiveFormulation == TyreFormulation::Xpbd)
    {
        StepXpbdCarcass(
            DownLocal,
            Penetration,
            GaugePressure,
            Inputs.LateralForceEstimateN,
            Inputs.LongitudinalForceEstN,
            Inputs.WheelOmegaRadPerSec,
            Dt);
        // Combine XPBD solved carcass reaction with sidewall radial damping + rim stop.
        const float ContinuumSpring = EffectiveK * Penetration;
        const float BlendedElastic  = 0.55f * SolvedCarcassLoadN + 0.45f * ContinuumSpring;
        Out.NormalLoadNewtons       = Out.InContact ? std::max(0.0f, BlendedElastic + ProgressiveBottoming + DampingLoad) : 0.0f;
    }
    else
    {
        StepFem3DCarcass(
            DownLocal,
            Penetration,
            GaugePressure,
            Inputs.LateralForceEstimateN,
            Inputs.LongitudinalForceEstN,
            Inputs.WheelOmegaRadPerSec,
            Dt);
        Out.MeanJacobian      = LastReading.MeanJacobian;
        Out.MinJacobian       = LastReading.MinJacobian;
        Out.MeanPiolaStressPa = LastReading.MeanPiolaStressPa;
        const float ContinuumSpring = EffectiveK * Penetration;
        const float BlendedHyper    = 0.55f * SolvedCarcassLoadN + 0.45f * ContinuumSpring;
        Out.NormalLoadNewtons       = Out.InContact ? std::max(0.0f, BlendedHyper + ProgressiveBottoming + DampingLoad) : 0.0f;
    }

    Out.NormalForceWorld = NormalWorld * Out.NormalLoadNewtons;
    Out.EnclosedVolumeM3 = ComputeEnclosedTorusVolume();

    // 3. Measure peak inward radial displacement and peak outward shoulder sidewall bulge across the 160 carcass nodes.
    float MaxBulge  = 0.0f;
    float MaxInward = 0.0f;
    for (uint32_t Sector = 0u; Sector < TyreSectorCount; ++Sector)
    {
        const float Angle = TwoPiConstant * static_cast<float>(Sector) / static_cast<float>(TyreSectorCount);
        const Vec3  RadialOut{std::cos(Angle), 0.0f, std::sin(Angle)};
        for (uint32_t Ring = 1u; Ring <= 3u; ++Ring)
        {
            const Vec3& Disp   = NodalDisplacements[CarcassNodeIndex(Sector, Ring)];
            const float Inward = std::max(0.0f, -Dot(Disp, RadialOut));
            MaxInward          = std::max(MaxInward, Inward);
            if (Ring == 1u || Ring == 3u)
            {
                const float OutwardShoulder = Ring == 1u ? std::max(0.0f, -Disp.y) : std::max(0.0f, Disp.y);
                MaxBulge                    = std::max(MaxBulge, OutwardShoulder);
            }
        }
    }
    Out.MaxSidewallBulgeMetres = MaxBulge;
    Out.MaxRadialInwardMetres  = MaxInward;
    if (Penetration > 1.0e-4f)
    {
        Out.EffectiveStiffnessNpm = Out.NormalLoadNewtons / Penetration;
    }

    LastReading = Out;
    return Out;
}

} // namespace Frontier::Vehicle
