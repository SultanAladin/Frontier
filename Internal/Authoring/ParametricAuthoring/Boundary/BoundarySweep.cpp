/*==============================================================================================================================================
                                                              BOUNDARYSWEEP.CPP
==============================================================================================================================================*/
// 🧩 The sweep body. Resolve the axis and the two plane offsets, raise a start ring and an end ring of vertices (drafted by a mitered per-corner
//    offset), attach the caps, then attach one wall quad per ring span. The stitching in AttachFace does the rest: because the wall quads name the
//    SAME vertex tokens the caps used, every shared edge is found in the lookup and its radial ring grows to two — the prism closes itself.

#include "BoundarySweep.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{
    constexpr double DraftEpsilon = 1.0e-9;   // [-] - below this a draft reach is no taper at all, so the miter solve is skipped entirely

    // 📝 The miter reach beyond which a sharp corner bevels instead of firing off to infinity. |sum| of two unit normals is 2*cos(half-angle), so the
    //    scale blows up as the corner closes; clamping it caps the drafted corner's excursion at four times the nominal reach.
    constexpr double MiterClamp = 4.0;

    // 📝 The in-plane outward normal at each corner of a ring, mitered so a constant perpendicular offset is held through the joint (the same
    //    construction SolveOpenCurveOffset uses in 2D, lifted into the profile plane).
    //
    //    🔴 ClosedRing IS NOT DECORATION. A closed ring's first and last corners each have two adjacent spans; an OPEN run's endpoints have only one.
    //       Wrapping the index with % Count on an open run miters the first corner against the LAST span — a span at the far end of the profile that
    //       shares no geometry with it — so both endpoints of every drafted thin wall bend in an arbitrary direction. On an open run the endpoints
    //       ride their single adjacent span's normal unscaled instead.
    std::vector<BoundaryVector> ResolveRingMiterNormals(const std::vector<BoundaryVector>& Ring, BoundaryVector Axis, bool ClosedRing)
    {
        const size_t Count = Ring.size();
        std::vector<BoundaryVector> Normals(Count, BoundaryVector{});
        if (Count < 2) return Normals;

        auto SpanNormal = [&](BoundaryVector From, BoundaryVector To) -> BoundaryVector
        {
            const BoundaryVector Span = SubtractBoundaryVector(To, From);
            // The in-plane right-hand normal of the span: cross(span, axis) points outward for a ring wound CCW about +axis.
            bool Resolved = false;
            const BoundaryVector Normal = NormalizeBoundaryVector(CrossBoundaryVector(Span, Axis), Resolved);
            return Resolved ? Normal : BoundaryVector{};
        };

        for (size_t Index = 0; Index < Count; ++Index)
        {
            const bool LeadingEnd  = (!ClosedRing && Index == 0);
            const bool TrailingEnd = (!ClosedRing && Index + 1 == Count);

            if (LeadingEnd)  { Normals[Index] = SpanNormal(Ring[0], Ring[1]);                       continue; }
            if (TrailingEnd) { Normals[Index] = SpanNormal(Ring[Count - 2], Ring[Count - 1]);       continue; }

            const BoundaryVector Incoming = SpanNormal(Ring[(Index + Count - 1) % Count], Ring[Index]);
            const BoundaryVector Outgoing = SpanNormal(Ring[Index], Ring[(Index + 1) % Count]);
            BoundaryVector Averaged = AddBoundaryVector(Incoming, Outgoing);
            const double   Length   = EvaluateBoundaryLength(Averaged);
            if (Length < DraftEpsilon)
            {
                Normals[Index] = Outgoing;   // a ~180 degree reversal: the two normals cancel, so ride the outgoing one unscaled
                continue;
            }
            double Scale = 2.0 / Length;     // |sum| = 2*cos(half-angle) for two unit normals
            if (Scale > MiterClamp) Scale = MiterClamp;
            Normals[Index] = ScaleBoundaryVector(Averaged, Scale / Length);
        }
        return Normals;
    }

    // 📝 The two plane offsets a sweep runs between, honouring the symmetric straddle. Distance is expected already sign-normalised (positive).
    void ResolveSweepOffsets(bool SymmetricEnabled, double Distance, double& OutStart, double& OutEnd)
    {
        if (SymmetricEnabled)
        {
            OutStart = -Distance * 0.5;
            OutEnd   =  Distance * 0.5;
        }
        else
        {
            OutStart = 0.0;
            OutEnd   = Distance;
        }
    }

    // 📝 Fold a signed distance into the axis, so everything downstream sweeps FORWARD along a unit axis by a positive amount.
    void NormalizeSweepSign(BoundaryVector& Axis, double& Distance)
    {
        if (Distance < 0.0)
        {
            Axis     = ScaleBoundaryVector(Axis, -1.0);
            Distance = -Distance;
        }
    }

    // 📝 Place one ring's two lifts from its stored base positions. Shared verbatim by the build and by every re-drag, which is what makes a drag
    //    idempotent: both paths run this one function against the same BasePositions, so replaying a distance always lands in the same place.
    void EnforceRingElevation(FullBrepBody&          Body,
                              const SweepRingRecord& Ring,
                              BoundaryVector         Axis,
                              double                 StartOffset,
                              double                 EndOffset,
                              double                 DraftReach,
                              bool                   ClosedRing)
    {
        const size_t Count = Ring.BasePositions.size();
        if (Count == 0 || Ring.StartRing.size() != Count || Ring.EndRing.size() != Count) return;

        const std::vector<BoundaryVector> Normals =
            (std::fabs(DraftReach) > DraftEpsilon) ? ResolveRingMiterNormals(Ring.BasePositions, Axis, ClosedRing)
                                                   : std::vector<BoundaryVector>(Count, BoundaryVector{});

        for (size_t Index = 0; Index < Count; ++Index)
        {
            const BoundaryVector Base = Ring.BasePositions[Index];
            EnforceVertexPosition(Body, Ring.StartRing[Index], AddBoundaryVector(Base, ScaleBoundaryVector(Axis, StartOffset)));
            const BoundaryVector Drafted = AddBoundaryVector(Base, ScaleBoundaryVector(Normals[Index], DraftReach));
            EnforceVertexPosition(Body, Ring.EndRing[Index], AddBoundaryVector(Drafted, ScaleBoundaryVector(Axis, EndOffset)));
        }
    }

    uint32_t ComposeGroupTag(const SweepSpecification& Specification, SweepGroupCategory Category)
    {
        return Specification.GroupTagBase + static_cast<uint32_t>(Category);
    }

    // ── Winding + hole-ring audit helpers. The audit works in the plane PERPENDICULAR to the sweep axis (a sketch profile's own plane): every ring
    //    projects into an orthonormal (Right, Up) frame, so all containment / crossing tests run in plain 2D. ─────────────────────────────────

    // 📝 The Newell normal of a ring — exact for a planar ring, robust to a slightly non-planar one. For a ring wound counter-clockwise seen from
    //    +Axis it points along +Axis, so its dot with Axis carries the winding sign.
    BoundaryVector EvaluateRingNewellNormal(const std::vector<BoundaryVector>& Ring)
    {
        BoundaryVector Normal;
        const size_t Count = Ring.size();
        if (Count < 3) return Normal;
        for (size_t Index = 0; Index < Count; ++Index)
        {
            const BoundaryVector& A = Ring[Index];
            const BoundaryVector& B = Ring[(Index + 1) % Count];
            Normal.XCoord += (A.YCoord - B.YCoord) * (A.ZCoord + B.ZCoord);
            Normal.YCoord += (A.ZCoord - B.ZCoord) * (A.XCoord + B.XCoord);
            Normal.ZCoord += (A.XCoord - B.XCoord) * (A.YCoord + B.YCoord);
        }
        return Normal;
    }

    // 📝 The winding sign of Ring seen from +Axis: +1 counter-clockwise, -1 clockwise, 0 when the ring has no clean winding (zero area, or its plane
    //    not perpendicular to the sweep — a tilted ring cannot be swept into a prism whose caps and walls hold the winding contract). The
    //    normalised-Newell dot makes the test scale-independent: a planar ring's unit Newell normal is ±Axis.
    int EvaluateRingWinding(const std::vector<BoundaryVector>& Ring, const BoundaryVector& Axis)
    {
        bool Normalized = false;
        const BoundaryVector UnitNormal = NormalizeBoundaryVector(EvaluateRingNewellNormal(Ring), Normalized);
        if (!Normalized) return 0;
        const double Projection = DotBoundaryVector(UnitNormal, Axis);
        if (Projection > 0.5) return +1;
        if (Projection < -0.5) return -1;
        return 0;
    }

    struct SweepPlaneFrame
    {
        BoundaryVector Right;   // [-] - unit, spans the profile plane with Up
        BoundaryVector Up;      // [-] - unit
    };

    SweepPlaneFrame ResolveSweepPlaneFrame(const BoundaryVector& Axis)
    {
        SweepPlaneFrame Frame;
        // Seed from whichever world axis is least aligned with the sweep, so the cross product never degenerates.
        BoundaryVector Seed{ 0.0, 0.0, 1.0 };
        if (std::fabs(Axis.ZCoord) > 0.9) Seed = BoundaryVector{ 1.0, 0.0, 0.0 };
        bool Resolved = false;
        Frame.Right = NormalizeBoundaryVector(CrossBoundaryVector(Seed, Axis), Resolved);
        if (!Resolved) Frame.Right = BoundaryVector{ 1.0, 0.0, 0.0 };
        Frame.Up = CrossBoundaryVector(Axis, Frame.Right);
        return Frame;
    }

    struct PlanePoint2D
    {
        double U = 0.0;   // [mm] - along the frame's Right
        double V = 0.0;   // [mm] - along the frame's Up
    };

    std::vector<PlanePoint2D> ProjectRing(const std::vector<BoundaryVector>& Ring, const SweepPlaneFrame& Frame)
    {
        std::vector<PlanePoint2D> Projected;
        Projected.reserve(Ring.size());
        for (const BoundaryVector& Point : Ring)
            Projected.push_back(PlanePoint2D{ DotBoundaryVector(Point, Frame.Right), DotBoundaryVector(Point, Frame.Up) });
        return Projected;
    }

    bool PointInsidePolygon2D(double X, double Y, const std::vector<PlanePoint2D>& Polygon)
    {
        bool Inside = false;
        const size_t Count = Polygon.size();
        for (size_t Index = 0, Previous = Count - 1; Index < Count; Previous = Index++)
        {
            const double Xi = Polygon[Index].U,    Yi = Polygon[Index].V;
            const double Xj = Polygon[Previous].U, Yj = Polygon[Previous].V;
            if (((Yi > Y) != (Yj > Y)) && (X < (Xj - Xi) * (Y - Yi) / (Yj - Yi) + Xi))
                Inside = !Inside;
        }
        return Inside;
    }

    // 📝 The signed 2D cross product (B−A)×(C−A): positive when C lies left of the directed line A→B. The orientation predicate every
    //    crossing / containment test builds on.
    double Orientation2D(const PlanePoint2D& A, const PlanePoint2D& B, const PlanePoint2D& C)
    {
        return (B.U - A.U) * (C.V - A.V) - (B.V - A.V) * (C.U - A.U);
    }

    // 📝 Proper segment crossing: each segment's endpoints STRICTLY straddle the other's line (opposite-signed orientations). Shared endpoints and
    //    collinear touch are not crossings — adjacent ring edges meet at a vertex by definition and must not trip the audit.
    bool SegmentsProperlyCross(const PlanePoint2D& A, const PlanePoint2D& B, const PlanePoint2D& C, const PlanePoint2D& D)
    {
        constexpr double OrientationEpsilon = 1.0e-9;   // [mm²] - below this an orientation reads as collinear
        const double O1 = Orientation2D(A, B, C);
        const double O2 = Orientation2D(A, B, D);
        const double O3 = Orientation2D(C, D, A);
        const double O4 = Orientation2D(C, D, B);
        const bool  StraddleFirst  = (O1 >  OrientationEpsilon && O2 < -OrientationEpsilon)
                                  || (O1 < -OrientationEpsilon && O2 >  OrientationEpsilon);
        const bool  StraddleSecond = (O3 >  OrientationEpsilon && O4 < -OrientationEpsilon)
                                  || (O3 < -OrientationEpsilon && O4 >  OrientationEpsilon);
        return StraddleFirst && StraddleSecond;
    }

    bool RingSelfIntersects(const std::vector<PlanePoint2D>& Ring)
    {
        const size_t Count = Ring.size();
        if (Count < 3) return false;
        for (size_t First = 0; First < Count; ++First)
        {
            const PlanePoint2D& A = Ring[First];
            const PlanePoint2D& B = Ring[(First + 1) % Count];
            for (size_t Second = First + 1; Second < Count; ++Second)
            {
                // Adjacent edge pairs share a vertex — skip them (the wrap pair e0 / eN-1 too).
                if (Second == First || Second == (First + 1) % Count || (First == 0 && Second + 1 == Count)) continue;
                const PlanePoint2D& C = Ring[Second];
                const PlanePoint2D& D = Ring[(Second + 1) % Count];
                if (SegmentsProperlyCross(A, B, C, D)) return true;
            }
        }
        return false;
    }

    // 📝 Whether every point of Inner lies inside Outer AND no Inner edge crosses an Outer edge. A hole whose boundary crosses the outer ring
    //    would punch a cavity that breaks the solid's exterior — it must read as outside, not merely "mostly inside".
    bool RingInsideRing(const std::vector<PlanePoint2D>& Inner, const std::vector<PlanePoint2D>& Outer)
    {
        for (const PlanePoint2D& Point : Inner)
            if (!PointInsidePolygon2D(Point.U, Point.V, Outer)) return false;
        for (size_t First = 0; First < Inner.size(); ++First)
        {
            const PlanePoint2D& A = Inner[First];
            const PlanePoint2D& B = Inner[(First + 1) % Inner.size()];
            for (size_t Second = 0; Second < Outer.size(); ++Second)
            {
                const PlanePoint2D& C = Outer[Second];
                const PlanePoint2D& D = Outer[(Second + 1) % Outer.size()];
                if (SegmentsProperlyCross(A, B, C, D)) return false;
            }
        }
        return true;
    }

    // 📝 Whether two hole rings overlap: one contains the other (a vertex of one inside the other) or their boundaries properly cross. Disjoint
    //    holes share neither, so a nested pair — which has no well-defined "which void wins" region — is flagged.
    bool HolesOverlap(const std::vector<PlanePoint2D>& FirstHole, const std::vector<PlanePoint2D>& SecondHole)
    {
        for (const PlanePoint2D& Point : FirstHole)
            if (PointInsidePolygon2D(Point.U, Point.V, SecondHole)) return true;
        for (const PlanePoint2D& Point : SecondHole)
            if (PointInsidePolygon2D(Point.U, Point.V, FirstHole)) return true;
        for (size_t First = 0; First < FirstHole.size(); ++First)
        {
            const PlanePoint2D& A = FirstHole[First];
            const PlanePoint2D& B = FirstHole[(First + 1) % FirstHole.size()];
            for (size_t Second = 0; Second < SecondHole.size(); ++Second)
            {
                const PlanePoint2D& C = SecondHole[Second];
                const PlanePoint2D& D = SecondHole[(Second + 1) % SecondHole.size()];
                if (SegmentsProperlyCross(A, B, C, D)) return true;
            }
        }
        return false;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SweepProfileValidation ValidateSweepProfile(const SweepProfile& Profile, const BoundaryVector& Direction)
{
    SweepProfileValidation Validation;

    bool AxisResolved = false;
    const BoundaryVector Axis = NormalizeBoundaryVector(Direction, AxisResolved);
    if (!AxisResolved)
    {
        Validation.SoundStatus = false;
        Validation.Findings.emplace_back("sweep direction is degenerate");
        return Validation;
    }
    const SweepPlaneFrame Frame = ResolveSweepPlaneFrame(Axis);

    // Outer ring: the sweep's wall-normal logic assumes outer-CCW seen from +Direction, so its winding is load-bearing.
    const int OuterWinding = EvaluateRingWinding(Profile.OuterRing, Axis);
    if (OuterWinding == 0)
    {
        Validation.SoundStatus = false;
        Validation.OuterCCW    = false;
        Validation.Findings.emplace_back("outer ring is degenerate (no area in the sweep plane)");
    }
    else if (OuterWinding < 0)
    {
        Validation.OuterCCW = false;
        Validation.Findings.emplace_back("outer ring wound clockwise; normalised by reversal");
    }
    const std::vector<PlanePoint2D> OuterProjection = ProjectRing(Profile.OuterRing, Frame);
    if (RingSelfIntersects(OuterProjection))
    {
        Validation.SoundStatus = false;
        Validation.RingsSimple = false;
        Validation.Findings.emplace_back("outer ring self-intersects");
    }

    // Hole rings: winding (CW seen from +Direction), containment inside the outer, simplicity, pairwise disjointness.
    std::vector<std::vector<PlanePoint2D>> HoleProjections;
    for (const std::vector<BoundaryVector>& Hole : Profile.InnerRings)
    {
        if (Hole.size() < 3)
        {
            Validation.HoleWindingsCW.push_back(1);   // nothing to normalise — the sweep skips a degenerate hole anyway
            Validation.Findings.emplace_back("hole ring with too few points skipped");
            continue;
        }

        const int HoleWinding = EvaluateRingWinding(Hole, Axis);
        if (HoleWinding == 0)
        {
            Validation.SoundStatus = false;
            Validation.HolesCW     = false;
            Validation.HoleWindingsCW.push_back(0);
            Validation.Findings.emplace_back("hole ring is degenerate (no area in the sweep plane)");
            continue;
        }
        const bool HoleIsCW = (HoleWinding < 0);
        Validation.HoleWindingsCW.push_back(HoleIsCW ? 1 : 0);
        if (!HoleIsCW)
        {
            Validation.HolesCW = false;
            Validation.Findings.emplace_back("hole ring wound counter-clockwise; normalised by reversal");
        }

        const std::vector<PlanePoint2D> HoleProjection = ProjectRing(Hole, Frame);
        if (RingSelfIntersects(HoleProjection))
        {
            Validation.SoundStatus = false;
            Validation.RingsSimple = false;
            Validation.Findings.emplace_back("hole ring self-intersects");
        }
        if (!RingInsideRing(HoleProjection, OuterProjection))
        {
            Validation.SoundStatus = false;
            Validation.HolesContained = false;
            Validation.Findings.emplace_back("hole ring not fully inside the outer ring");
        }
        for (const std::vector<PlanePoint2D>& Prior : HoleProjections)
        {
            if (HolesOverlap(Prior, HoleProjection))
            {
                Validation.SoundStatus = false;
                Validation.HolesDisjoint = false;
                Validation.Findings.emplace_back("hole rings overlap or nest");
            }
        }
        HoleProjections.push_back(HoleProjection);
    }
    return Validation;
}

const char* ResolveSweepOutcomeLabel(SweepOutcomeCategory Category)
{
    switch (Category)
    {
        case SweepOutcomeCategory::Completed:         return "Completed";
        case SweepOutcomeCategory::DegenerateProfile: return "Profile has too few points to sweep";
        case SweepOutcomeCategory::ZeroDistance:      return "Sweep distance is zero";
        case SweepOutcomeCategory::DegenerateAxis:    return "Sweep direction could not be resolved";
        case SweepOutcomeCategory::OpenProfileCapped: return "An open profile cannot be capped — swept as an uncapped sheet";
        case SweepOutcomeCategory::ValidationFault:   return "Swept boundary failed its structural audit and was rolled back";
        case SweepOutcomeCategory::WindingFault:      return "Profile failed its winding / hole-ring audit";
    }
    return "";
}

bool ExtrudeProfileIntoBrep(FullBrepBody&             Body,
                            const SweepProfile&       Profile,
                            const SweepSpecification& Specification,
                            SweepOutcome&             Outcome)
{
    Outcome = SweepOutcome{};

    // ── Validate the request before touching the body, so a rejection leaves nothing behind. ─────────────────────────────
    const size_t OuterCount  = Profile.OuterRing.size();
    const size_t MinimumRing = Profile.ClosedEnabled ? 3u : 2u;
    if (OuterCount < MinimumRing)
    {
        Outcome.Category = SweepOutcomeCategory::DegenerateProfile;
        Outcome.Notice   = ResolveSweepOutcomeLabel(Outcome.Category);
        return false;
    }

    bool AxisResolved = false;
    BoundaryVector Axis = NormalizeBoundaryVector(Specification.Direction, AxisResolved);
    if (!AxisResolved)
    {
        Outcome.Category = SweepOutcomeCategory::DegenerateAxis;
        Outcome.Notice   = ResolveSweepOutcomeLabel(Outcome.Category);
        return false;
    }

    // 🔴 Sign normalisation FIRST, before any gate reads the distance. Everything downstream then works with a forward axis and a positive height.
    double Distance = Specification.Distance;
    NormalizeSweepSign(Axis, Distance);

    // 🔴 The zero gate is keyed off the WELD RADIUS, not a separate looser epsilon. A sweep shorter than two weld radii cannot separate its two rings:
    //    ResolveOrAttachVertex welds the end corner onto the start corner, every wall quad collapses to a repeated-vertex ring that AttachCoEdgeRing
    //    silently rejects, and the caller is handed a "Completed" sweep with no walls. One tolerance, one meaning.
    if (Distance <= Body.WeldTolerance * 2.0)
    {
        Outcome.Category = SweepOutcomeCategory::ZeroDistance;
        Outcome.Notice   = ResolveSweepOutcomeLabel(Outcome.Category);
        return false;
    }

    // 🔴 Winding / hole-ring pre-flight, BEFORE anything attaches. The outward-wall and cap-normal logic BELOW assumes the winding contract (outer
    //    CCW / holes CW seen from +Direction); a violation would silently flip wall quads and invert cap normals, and a malformed hole (outside the
    //    outer, self-crossing, nested) would tear the cavity. Structural faults reject the sweep with nothing left behind; a PURE winding inversion
    //    is sound and is normalised by reversal on the working copy, so a caller that hands a well-formed but inverted ring still gets a correct solid.
    const SweepProfileValidation WindingAudit = ValidateSweepProfile(Profile, Axis);
    if (!WindingAudit.SoundStatus)
    {
        Outcome.Category = SweepOutcomeCategory::WindingFault;
        Outcome.Notice   = WindingAudit.Findings.empty() ? ResolveSweepOutcomeLabel(Outcome.Category) : WindingAudit.Findings.front();
        return false;
    }
    SweepProfile Work = Profile;
    if (!WindingAudit.OuterCCW)
        std::reverse(Work.OuterRing.begin(), Work.OuterRing.end());
    for (size_t Index = 0; Index < Work.InnerRings.size() && Index < WindingAudit.HoleWindingsCW.size(); ++Index)
        if (WindingAudit.HoleWindingsCW[Index] == 0)
            std::reverse(Work.InnerRings[Index].begin(), Work.InnerRings[Index].end());

    const bool CapsPossible  = Work.ClosedEnabled;
    const bool CapsRequested = Specification.StartCapEnabled || Specification.EndCapEnabled;
    const bool CapsDropped   = CapsRequested && !CapsPossible;

    // 🔴 A closed profile swept with BOTH caps off is an open tube, not a solid: its two ring-ends are rim edges of radial count 1. Filing that under
    //    an Outer envelope claims a watertight exterior the geometry does not have, and every consumer that trusts the category (the boolean's
    //    inside/outside classification above all) is then working from a lie. The envelope category follows what was actually built.
    const bool ClosedSolid = CapsPossible && (Specification.StartCapEnabled && Specification.EndCapEnabled);
    const EnvelopeToken Envelope = AttachEnvelope(Body, ClosedSolid ? EnvelopeCategory::Outer : EnvelopeCategory::Sheet);
    Outcome.Envelope = Envelope;

    // The sweep as actually built, carried on the outcome so a re-drag replays it without the caller re-supplying anything.
    Outcome.ResolvedSweep           = Specification;
    Outcome.ResolvedSweep.Direction = Axis;
    Outcome.ResolvedSweep.Distance  = Distance;
    Outcome.ClosedProfile           = Work.ClosedEnabled;

    double StartOffset = 0.0, EndOffset = 0.0;
    ResolveSweepOffsets(Specification.SymmetricEnabled, Distance, StartOffset, EndOffset);
    const double DraftReach = Distance * std::tan(Specification.DraftRadians);

    // ── Raise the rings. Every ring (outer + each hole) is lifted twice, and the two lifts are recorded index-for-index so the wall band can pair
    //    them without a second search. Vertices go through ResolveOrAttachVertex so a sweep laid over an existing body WELDS onto it. ──────────────
    const uint32_t StartTag = ComposeGroupTag(Specification, SweepGroupCategory::StartCap);
    const uint32_t EndTag   = ComposeGroupTag(Specification, SweepGroupCategory::EndCap);

    auto RaiseRing = [&](const std::vector<BoundaryVector>& Ring, bool HoleRing) -> bool
    {
        if (Ring.size() < MinimumRing) return false;
        const std::vector<BoundaryVector> Normals =
            (std::fabs(DraftReach) > DraftEpsilon) ? ResolveRingMiterNormals(Ring, Axis, Work.ClosedEnabled)
                                                   : std::vector<BoundaryVector>(Ring.size(), BoundaryVector{});

        SweepRingRecord Entry;
        Entry.HoleRing      = HoleRing;
        Entry.BasePositions = Ring;   // 🔴 the profile-plane source, stored once and never re-derived — the whole of the anti-drift fix
        Entry.StartRing.reserve(Ring.size());
        Entry.EndRing.reserve(Ring.size());

        for (size_t Index = 0; Index < Ring.size(); ++Index)
        {
            const BoundaryVector Base = Ring[Index];
            const BoundaryVector StartPosition = AddBoundaryVector(Base, ScaleBoundaryVector(Axis, StartOffset));
            // 🔴 The draft offsets the END ring only, along the corner's own in-plane miter normal. A hole ring's outward normal points into its
            //    cavity, so the same positive reach correctly widens the cavity as it widens the outer boundary — the wall thins uniformly, which is
            //    what a positive draft angle means physically. No per-ring sign test is needed.
            const BoundaryVector Drafted = AddBoundaryVector(Base, ScaleBoundaryVector(Normals[Index], DraftReach));
            const BoundaryVector EndPosition = AddBoundaryVector(Drafted, ScaleBoundaryVector(Axis, EndOffset));

            Entry.StartRing.push_back(ResolveOrAttachVertex(Body, StartPosition, StartTag));
            Entry.EndRing.push_back  (ResolveOrAttachVertex(Body, EndPosition,   EndTag));
        }
        Outcome.Rings.push_back(std::move(Entry));
        return true;
    };

    if (!RaiseRing(Work.OuterRing, false))
    {
        DetachEnvelope(Body, Envelope);
        Outcome = SweepOutcome{};
        Outcome.Category = SweepOutcomeCategory::DegenerateProfile;
        Outcome.Notice   = ResolveSweepOutcomeLabel(Outcome.Category);
        return false;
    }
    for (const std::vector<BoundaryVector>& Hole : Work.InnerRings)
        RaiseRing(Hole, true);   // a degenerate hole is skipped rather than failing the whole sweep

    // ── Caps. The start cap is the outer ring REVERSED (so its Newell normal opposes the sweep and points out of the solid); the end cap takes the
    //    ring as authored. Hole rings are attached as inner loops with the matching reversal, so the punched region reads as a void on both faces. ─
    if (CapsPossible && Specification.StartCapEnabled)
    {
        std::vector<VertexToken> Ring = Outcome.Rings.front().StartRing;
        std::reverse(Ring.begin(), Ring.end());
        Outcome.StartCapFace = AttachFace(Body, Envelope, Ring, FaceCategory::Planar, StartTag);
        for (size_t Index = 1; Index < Outcome.Rings.size(); ++Index)
        {
            if (!Outcome.Rings[Index].HoleRing) continue;
            std::vector<VertexToken> HoleRing = Outcome.Rings[Index].StartRing;
            std::reverse(HoleRing.begin(), HoleRing.end());
            AttachInnerLoop(Body, Outcome.StartCapFace, HoleRing);
        }
    }
    if (CapsPossible && Specification.EndCapEnabled)
    {
        Outcome.EndCapFace = AttachFace(Body, Envelope, Outcome.Rings.front().EndRing, FaceCategory::Planar, EndTag);
        for (size_t Index = 1; Index < Outcome.Rings.size(); ++Index)
        {
            if (!Outcome.Rings[Index].HoleRing) continue;
            AttachInnerLoop(Body, Outcome.EndCapFace, Outcome.Rings[Index].EndRing);
        }
    }

    // ── The wall band. One quad per ring span: [A0, B0, B1, A1], which is outward-facing for a CCW outer ring AND for a CW hole ring, so there is no
    //    per-ring winding branch here. A closed ring wraps; an open run stops one span short (its two ends are the sheet's rim). ────────────────────
    for (const SweepRingRecord& Ring : Outcome.Rings)
    {
        const size_t Count = Ring.StartRing.size();
        const size_t Spans = Work.ClosedEnabled ? Count : (Count > 0 ? Count - 1 : 0);
        const uint32_t WallTag = ComposeGroupTag(Specification,
                                                 Ring.HoleRing ? SweepGroupCategory::InnerWall : SweepGroupCategory::OuterWall);
        for (size_t Index = 0; Index < Spans; ++Index)
        {
            const size_t Next = (Index + 1) % Count;
            const std::vector<VertexToken> Quad = { Ring.StartRing[Index], Ring.StartRing[Next],
                                                    Ring.EndRing[Next],    Ring.EndRing[Index] };
            const FaceToken Wall = AttachFace(Body, Envelope, Quad, FaceCategory::Ruled, WallTag);
            if (TokenAssigned(Wall)) Outcome.WallFaces.push_back(Wall);
        }
    }

    // Report the outer ring's corners as the drag handles (the hole rings ride Outcome.Rings and the cap faces' inner loops).
    Outcome.StartRingVertices = Outcome.Rings.front().StartRing;
    Outcome.EndRingVertices   = Outcome.Rings.front().EndRing;

    // 🔴 The audit is scoped to THIS envelope. Auditing the whole body would let an older sheet or a mid-edit non-manifold envelope elsewhere fail a
    //    sweep that built perfectly — and, worse, would roll back correct geometry over a fault it did not cause.
    Outcome.Audit = ValidateBrepEnvelope(Body, Envelope);
    if (!Outcome.Audit.SoundStatus)
    {
        // Roll back per the header's rollback contract; the audit findings survive on the outcome so the caller can still report why.
        DetachEnvelope(Body, Envelope);
        Outcome.Envelope     = EnvelopeToken{};
        Outcome.StartCapFace = FaceToken{};
        Outcome.EndCapFace   = FaceToken{};
        Outcome.WallFaces.clear();
        Outcome.Rings.clear();
        Outcome.StartRingVertices.clear();
        Outcome.EndRingVertices.clear();
        Outcome.Category = SweepOutcomeCategory::ValidationFault;
        Outcome.Notice   = ResolveSweepOutcomeLabel(Outcome.Category);
        return false;
    }

    // 📝 Dropped caps are a SUCCESS with a notice, not a rejection: an open profile bounds no face to cap, and failing the whole sweep over a flag
    //    defaulted to true (which the caller never set) would reject every thin-wall extrude. The category carries it so a caller can branch on it.
    Outcome.Category = CapsDropped ? SweepOutcomeCategory::OpenProfileCapped : SweepOutcomeCategory::Completed;
    Outcome.Notice   = CapsDropped ? ResolveSweepOutcomeLabel(Outcome.Category) : std::string();
    return true;
}

bool EnforceSweepDistance(FullBrepBody& Body, const SweepOutcome& Outcome, double DistanceMm)
{
    // 🔴 An outcome that never constructed has no rings to move, and the old signature's "return true" on that case told an interactive drag its
    //    frame had landed when nothing had happened at all.
    if (!QuerySweepConstructed(Outcome.Category) || Outcome.Rings.empty()) return false;

    bool AxisResolved = false;
    BoundaryVector Axis = NormalizeBoundaryVector(Outcome.ResolvedSweep.Direction, AxisResolved);
    if (!AxisResolved) return false;

    // Re-normalise exactly as the build did, so dragging back through zero flips the sweep rather than inverting the solid.
    double Distance = DistanceMm;
    NormalizeSweepSign(Axis, Distance);

    double StartOffset = 0.0, EndOffset = 0.0;
    ResolveSweepOffsets(Outcome.ResolvedSweep.SymmetricEnabled, Distance, StartOffset, EndOffset);
    const double DraftReach = Distance * std::tan(Outcome.ResolvedSweep.DraftRadians);

    // 🔴 Every ring, outer AND hole, replaced from its own stored BasePositions. Nothing is read back out of the body and re-projected, so a symmetric
    //    drag is idempotent: replaying the same distance twice lands in the same place, and the centre never creeps.
    for (const SweepRingRecord& Ring : Outcome.Rings)
        EnforceRingElevation(Body, Ring, Axis, StartOffset, EndOffset, DraftReach, Outcome.ClosedProfile);

    return true;
}

} // namespace Frontier