/*==============================================================================================================================================
                                                            BOUNDARYTESSELLATION.CPP
==============================================================================================================================================*/
// 🧩 Derive the three viewport streams from a boundary body. The only non-trivial part is face triangulation: project the face onto its dominant
//    plane (dropping the axis its normal is largest in, so an edge-on face never collapses), bridge any hole rings into the outer ring, then ear-clip.
//    A convex ring short-circuits to a fan, which covers every wall quad a sweep produces.

#include "BoundaryTessellation.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{
    // 📝 A 2D projection of a ring point, carrying the index of the 3D position it came from so the emitted triangles reference real vertices.
    struct PlanarPoint
    {
        double   U = 0.0, V = 0.0;
        uint32_t Source = 0u;   // [-] - index into the face's flattened 3D position run
    };

    // Drop the axis the normal is largest in; the remaining two span the face without degeneracy.
    int ResolveDropAxis(BoundaryVector Normal)
    {
        const double AbsoluteX = std::fabs(Normal.XCoord);
        const double AbsoluteY = std::fabs(Normal.YCoord);
        const double AbsoluteZ = std::fabs(Normal.ZCoord);
        if (AbsoluteX >= AbsoluteY && AbsoluteX >= AbsoluteZ) return 0;
        return (AbsoluteY >= AbsoluteZ) ? 1 : 2;
    }

    PlanarPoint ProjectToPlane(BoundaryVector Position, int DropAxis, uint32_t Source)
    {
        PlanarPoint Point;
        Point.Source = Source;
        if      (DropAxis == 0) { Point.U = Position.YCoord; Point.V = Position.ZCoord; }
        else if (DropAxis == 1) { Point.U = Position.ZCoord; Point.V = Position.XCoord; }
        else                    { Point.U = Position.XCoord; Point.V = Position.YCoord; }
        return Point;
    }

    double SignedArea(const std::vector<PlanarPoint>& Ring)
    {
        double Twice = 0.0;
        const size_t Count = Ring.size();
        for (size_t Index = 0; Index < Count; ++Index)
        {
            const PlanarPoint& Current = Ring[Index];
            const PlanarPoint& Next    = Ring[(Index + 1) % Count];
            Twice += Current.U * Next.V - Next.U * Current.V;
        }
        return Twice * 0.5;
    }

    double Cross2(const PlanarPoint& A, const PlanarPoint& B, const PlanarPoint& C)
    {
        return (B.U - A.U) * (C.V - A.V) - (B.V - A.V) * (C.U - A.U);
    }

    bool PointInTriangle(const PlanarPoint& Probe, const PlanarPoint& A, const PlanarPoint& B, const PlanarPoint& C)
    {
        const double AreaA = Cross2(A, B, Probe);
        const double AreaB = Cross2(B, C, Probe);
        const double AreaC = Cross2(C, A, Probe);
        const bool AnyNegative = (AreaA < 0.0) || (AreaB < 0.0) || (AreaC < 0.0);
        const bool AnyPositive = (AreaA > 0.0) || (AreaB > 0.0) || (AreaC > 0.0);
        return !(AnyNegative && AnyPositive);
    }

    bool RingConvex(const std::vector<PlanarPoint>& Ring)
    {
        const size_t Count = Ring.size();
        if (Count < 4) return true;
        bool AnyNegative = false, AnyPositive = false;
        for (size_t Index = 0; Index < Count; ++Index)
        {
            const double Turn = Cross2(Ring[Index], Ring[(Index + 1) % Count], Ring[(Index + 2) % Count]);
            if (Turn < -1.0e-12) AnyNegative = true;
            if (Turn >  1.0e-12) AnyPositive = true;
        }
        return !(AnyNegative && AnyPositive);
    }

    // 📝 Ear-clip a CCW simple ring into triangle triples over its Source indices. A convex ring fans instead. The step budget makes a pathological
    //    ring terminate with a partial fan rather than hanging.
    void ClipEars(const std::vector<PlanarPoint>& Ring, std::vector<uint32_t>& OutTriples)
    {
        const size_t Count = Ring.size();
        if (Count < 3) return;

        if (RingConvex(Ring))
        {
            for (size_t Index = 1; Index + 1 < Count; ++Index)
            {
                OutTriples.push_back(Ring[0].Source);
                OutTriples.push_back(Ring[Index].Source);
                OutTriples.push_back(Ring[Index + 1].Source);
            }
            return;
        }

        std::vector<uint32_t> Remaining(Count);
        for (size_t Index = 0; Index < Count; ++Index) Remaining[Index] = static_cast<uint32_t>(Index);

        size_t Budget = Count * Count + 16u;
        while (Remaining.size() > 3u && Budget-- > 0u)
        {
            bool Clipped = false;
            const size_t Live = Remaining.size();
            for (size_t Index = 0; Index < Live; ++Index)
            {
                const PlanarPoint& Previous = Ring[Remaining[(Index + Live - 1) % Live]];
                const PlanarPoint& Corner   = Ring[Remaining[Index]];
                const PlanarPoint& Next     = Ring[Remaining[(Index + 1) % Live]];
                if (Cross2(Previous, Corner, Next) <= 0.0) continue;   // reflex — not an ear

                bool Contains = false;
                for (size_t Other = 0; Other < Live && !Contains; ++Other)
                {
                    if (Other == Index || Other == (Index + Live - 1) % Live || Other == (Index + 1) % Live) continue;
                    Contains = PointInTriangle(Ring[Remaining[Other]], Previous, Corner, Next);
                }
                if (Contains) continue;

                OutTriples.push_back(Previous.Source);
                OutTriples.push_back(Corner.Source);
                OutTriples.push_back(Next.Source);
                Remaining.erase(Remaining.begin() + static_cast<long>(Index));
                Clipped = true;
                break;
            }
            if (!Clipped) break;   // no ear found (a self-intersecting ring) — emit what survives below
        }

        for (size_t Index = 1; Index + 1 < Remaining.size(); ++Index)
        {
            OutTriples.push_back(Ring[Remaining[0]].Source);
            OutTriples.push_back(Ring[Remaining[Index]].Source);
            OutTriples.push_back(Ring[Remaining[Index + 1]].Source);
        }
    }

    // 📝 Bridge one hole ring into the outer ring: find the hole's rightmost point, join it to the nearest visible outer point with a doubled seam,
    //    producing one simple ring. Repeated per hole, outermost-first by rightmost extent so the seams never cross.
    void BridgeHole(std::vector<PlanarPoint>& Outer, const std::vector<PlanarPoint>& Hole)
    {
        if (Hole.size() < 3 || Outer.size() < 3) return;

        size_t HoleStart = 0;
        for (size_t Index = 1; Index < Hole.size(); ++Index)
            if (Hole[Index].U > Hole[HoleStart].U) HoleStart = Index;

        size_t OuterJoin = 0;
        double NearestSquared = 1.0e30;
        for (size_t Index = 0; Index < Outer.size(); ++Index)
        {
            const double DeltaU = Outer[Index].U - Hole[HoleStart].U;
            const double DeltaV = Outer[Index].V - Hole[HoleStart].V;
            const double Squared = DeltaU * DeltaU + DeltaV * DeltaV;
            if (Squared < NearestSquared) { NearestSquared = Squared; OuterJoin = Index; }
        }

        std::vector<PlanarPoint> Merged;
        Merged.reserve(Outer.size() + Hole.size() + 2u);
        for (size_t Index = 0; Index <= OuterJoin; ++Index) Merged.push_back(Outer[Index]);
        for (size_t Step = 0; Step <= Hole.size(); ++Step)  Merged.push_back(Hole[(HoleStart + Step) % Hole.size()]);
        for (size_t Index = OuterJoin; Index < Outer.size(); ++Index) Merged.push_back(Outer[Index]);
        Outer.swap(Merged);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void AssembleBoundaryRenderStream(const FullBrepBody& Body, BoundaryRenderStream& OutStream)
{
    OutStream.Vertices.clear();
    OutStream.Indices.clear();
    OutStream.TriangleFaces.clear();
    OutStream.Revision = Body.Revision;

    std::vector<FaceToken> Faces;
    ResolveLiveFaces(Body, Faces);

    for (FaceToken Face : Faces)
    {
        const SolidFace* const FaceRecord = ArenaResolve(Body.Faces, Face);
        if (FaceRecord == nullptr) continue;

        std::vector<LoopToken> Loops;
        if (!ResolveFaceLoops(Body, Face, Loops) || Loops.empty()) continue;

        std::vector<BoundaryVector> Positions;   // the face's own flattened 3D run; triples index into it
        const int DropAxis = ResolveDropAxis(FaceRecord->PlaneNormal);

        std::vector<PlanarPoint> Outer;
        {
            std::vector<BoundaryVector> Ring;
            if (!ResolveLoopPositions(Body, Loops.front(), Ring) || Ring.size() < 3) continue;
            for (const BoundaryVector& Point : Ring)
            {
                Outer.push_back(ProjectToPlane(Point, DropAxis, static_cast<uint32_t>(Positions.size())));
                Positions.push_back(Point);
            }
        }
        // The ear clipper wants CCW in the projected frame; the projection can invert it, so re-orient rather than assume.
        if (SignedArea(Outer) < 0.0) std::reverse(Outer.begin(), Outer.end());

        // Holes are bridged rightmost-first so their seams cannot cross one another.
        std::vector<std::vector<PlanarPoint>> Holes;
        for (size_t Index = 1; Index < Loops.size(); ++Index)
        {
            std::vector<BoundaryVector> Ring;
            if (!ResolveLoopPositions(Body, Loops[Index], Ring) || Ring.size() < 3) continue;
            std::vector<PlanarPoint> Hole;
            for (const BoundaryVector& Point : Ring)
            {
                Hole.push_back(ProjectToPlane(Point, DropAxis, static_cast<uint32_t>(Positions.size())));
                Positions.push_back(Point);
            }
            if (SignedArea(Hole) > 0.0) std::reverse(Hole.begin(), Hole.end());   // a hole runs opposite the outer ring
            Holes.push_back(std::move(Hole));
        }
        std::sort(Holes.begin(), Holes.end(),
                  [](const std::vector<PlanarPoint>& Left, const std::vector<PlanarPoint>& Right)
                  {
                      const auto Rightmost = [](const std::vector<PlanarPoint>& Ring)
                      {
                          double Best = Ring.front().U;
                          for (const PlanarPoint& Point : Ring) Best = (std::max)(Best, Point.U);
                          return Best;
                      };
                      return Rightmost(Left) > Rightmost(Right);
                  });
        for (const std::vector<PlanarPoint>& Hole : Holes) BridgeHole(Outer, Hole);

        std::vector<uint32_t> Triples;
        ClipEars(Outer, Triples);
        if (Triples.size() < 3u) continue;

        const uint32_t Base = static_cast<uint32_t>(OutStream.Vertices.size());
        for (const BoundaryVector& Point : Positions)
        {
            BoundaryRenderVertex Vertex;
            Vertex.PositionX = static_cast<float>(Point.XCoord);
            Vertex.PositionY = static_cast<float>(Point.YCoord);
            Vertex.PositionZ = static_cast<float>(Point.ZCoord);
            Vertex.NormalX   = static_cast<float>(FaceRecord->PlaneNormal.XCoord);
            Vertex.NormalY   = static_cast<float>(FaceRecord->PlaneNormal.YCoord);
            Vertex.NormalZ   = static_cast<float>(FaceRecord->PlaneNormal.ZCoord);
            OutStream.Vertices.push_back(Vertex);
        }
        for (size_t Index = 0; Index + 2 < Triples.size(); Index += 3)
        {
            OutStream.Indices.push_back(Base + Triples[Index]);
            OutStream.Indices.push_back(Base + Triples[Index + 1]);
            OutStream.Indices.push_back(Base + Triples[Index + 2]);
            OutStream.TriangleFaces.push_back(Face);
        }
    }
}

void AssembleBoundaryEdgeStream(const FullBrepBody& Body, BoundaryEdgeStream& OutStream)
{
    OutStream.Segments.clear();
    OutStream.Revision = Body.Revision;

    std::vector<EdgeToken> Edges;
    ResolveLiveEdges(Body, Edges);
    OutStream.Segments.reserve(Edges.size());

    for (EdgeToken Edge : Edges)
    {
        const SolidEdge* const Record = ArenaResolve(Body.Edges, Edge);
        if (Record == nullptr) continue;
        const BoundaryVector From = ResolveVertexPosition(Body, Record->VertexA);
        const BoundaryVector To   = ResolveVertexPosition(Body, Record->VertexB);

        BoundaryEdgeSegment Segment;
        Segment.StartX = static_cast<float>(From.XCoord);
        Segment.StartY = static_cast<float>(From.YCoord);
        Segment.StartZ = static_cast<float>(From.ZCoord);
        Segment.EndX   = static_cast<float>(To.XCoord);
        Segment.EndY   = static_cast<float>(To.YCoord);
        Segment.EndZ   = static_cast<float>(To.ZCoord);
        Segment.Edge        = Edge;
        Segment.RadialCount = Record->RadialCount;
        OutStream.Segments.push_back(Segment);
    }
}

void AssembleBoundaryVertexStream(const FullBrepBody& Body, BoundaryVertexStream& OutStream)
{
    OutStream.Handles.clear();
    OutStream.Revision = Body.Revision;

    std::vector<VertexToken> Vertices;
    ResolveLiveVertices(Body, Vertices);
    OutStream.Handles.reserve(Vertices.size());

    for (VertexToken Vertex : Vertices)
    {
        const SolidVertex* const Record = ArenaResolve(Body.Vertices, Vertex);
        if (Record == nullptr) continue;

        BoundaryVertexHandle Handle;
        Handle.PositionX = static_cast<float>(Record->Position.XCoord);
        Handle.PositionY = static_cast<float>(Record->Position.YCoord);
        Handle.PositionZ = static_cast<float>(Record->Position.ZCoord);
        Handle.Vertex    = Vertex;
        Handle.Valence   = static_cast<uint32_t>(Record->IncidentEdges.size());
        OutStream.Handles.push_back(Handle);
    }
}

bool RefreshBoundaryStreams(const FullBrepBody&   Body,
                            BoundaryRenderStream& RenderStream,
                            BoundaryEdgeStream&   EdgeStream,
                            BoundaryVertexStream& VertexStream)
{
    if (!QueryBoundaryStreamStale(Body, RenderStream.Revision) &&
        !QueryBoundaryStreamStale(Body, EdgeStream.Revision)   &&
        !QueryBoundaryStreamStale(Body, VertexStream.Revision))
        return false;

    AssembleBoundaryRenderStream(Body, RenderStream);
    AssembleBoundaryEdgeStream(Body, EdgeStream);
    AssembleBoundaryVertexStream(Body, VertexStream);
    return true;
}

} // namespace Frontier
