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

    // 📝 Two axes spanning the plane perpendicular to Axis, so a ring's miter normals (which are meaningful in the profile plane, not in world XY)
    //    can be computed for an arbitrary sweep direction rather than only for +Z.
    void ResolvePlaneFrame(BoundaryVector Axis, BoundaryVector& OutRight, BoundaryVector& OutUp)
    {
        // Seed from whichever world axis is least aligned with the sweep, so the cross product never degenerates.
        BoundaryVector Seed{ 0.0, 0.0, 1.0 };
        if (std::fabs(Axis.ZCoord) > 0.9) Seed = BoundaryVector{ 1.0, 0.0, 0.0 };

        bool Resolved = false;
        OutRight = NormalizeBoundaryVector(CrossBoundaryVector(Seed, Axis), Resolved);
        if (!Resolved) OutRight = BoundaryVector{ 1.0, 0.0, 0.0 };
        OutUp = CrossBoundaryVector(Axis, OutRight);
    }

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
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

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

    const bool CapsPossible  = Profile.ClosedEnabled;
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
    Outcome.ClosedProfile           = Profile.ClosedEnabled;

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
            (std::fabs(DraftReach) > DraftEpsilon) ? ResolveRingMiterNormals(Ring, Axis, Profile.ClosedEnabled)
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

    if (!RaiseRing(Profile.OuterRing, false))
    {
        DetachEnvelope(Body, Envelope);
        Outcome = SweepOutcome{};
        Outcome.Category = SweepOutcomeCategory::DegenerateProfile;
        Outcome.Notice   = ResolveSweepOutcomeLabel(Outcome.Category);
        return false;
    }
    for (const std::vector<BoundaryVector>& Hole : Profile.InnerRings)
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
        const size_t Spans = Profile.ClosedEnabled ? Count : (Count > 0 ? Count - 1 : 0);
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