/*============================================================================================================================================
                                                          RAYPICKINTERSECTION.CPP
============================================================================================================================================*/
// 🧩 The cluster-native single-click picker. For each display triangle: resolve its three source corner cursors -> source
//    vertices -> object-space positions, run the Möller–Trumbore test, and keep the nearest hit. Then classify barycentrically:
//    the largest corner weight is the snapped vertex; the two corners NOT opposite the smallest weight form the nearest triangle
//    side, and if those two source-vertices are a real loop edge in the adjacency it is the picked edge, else a fan diagonal ->
//    InvalidEdgeKey. Face is always the hit triangle's SourceFace. Optional backface reject uses the triangle's geometric normal
//    against the ray direction so only the visible front shell resolves when X-ray is off.

#include "RayPickIntersection.h"

#include "PolygonCluster.h"
#include "AdjacencyIndex.h"

#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 The source vertex index a display triangle's corner k came from: its flat corner cursor indexes FaceVertexIndices.
    //    Returns InvalidCornerReference when the cursor is out of range (a malformed / default origin), so the caller skips it.
    uint32_t ResolveCornerVertex(const PolygonCluster& Cluster, uint32_t CornerCursor)
    {
        if (CornerCursor >= Cluster.FaceVertexIndices.size()) return InvalidCornerReference;
        return Cluster.FaceVertexIndices[CornerCursor];
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

PickOutcome ResolveElementPick(const Ray&              ObjectRay,
                               const PolygonCluster&   Cluster,
                               const DisplayPolygons&  Display,
                               const AdjacencyIndex&   Adjacency,
                               bool                    BackfaceSelectEnabled)
{
    PickOutcome Outcome = {};

    const std::vector<Vector3d>& Position      = Cluster.Attributes.Position;
    const uint32_t               VertexCount   = (uint32_t)Position.size();
    const uint32_t               TriangleCount = (uint32_t)Display.TriangleOrigins.size();

    double   NearestDistance = 0.0;
    bool     NearestFound    = false;
    uint32_t NearestTriangle = InvalidCornerReference;
    double   NearestU = 0.0, NearestV = 0.0;

    for (uint32_t TriangleOrdinal = 0; TriangleOrdinal < TriangleCount; ++TriangleOrdinal)
    {
        const TriangleOrigin& Origin = Display.TriangleOrigins[TriangleOrdinal];

        const uint32_t VertexIndexA = ResolveCornerVertex(Cluster, Origin.SourceCorner[0]);
        const uint32_t VertexIndexB = ResolveCornerVertex(Cluster, Origin.SourceCorner[1]);
        const uint32_t VertexIndexC = ResolveCornerVertex(Cluster, Origin.SourceCorner[2]);
        if (VertexIndexA >= VertexCount || VertexIndexB >= VertexCount || VertexIndexC >= VertexCount) continue;

        const Vector3d& CornerA = Position[VertexIndexA];
        const Vector3d& CornerB = Position[VertexIndexB];
        const Vector3d& CornerC = Position[VertexIndexC];

        // 📝 Backface reject: skip triangles whose geometric normal faces the same way the ray travels (a back face), so the
        //    pick resolves against the visible front shell only. X-ray (BackfaceSelectEnabled) accepts either facing.
        if (!BackfaceSelectEnabled)
        {
            const Vector3d EdgeAB = SubtractVector(CornerB, CornerA);
            const Vector3d EdgeAC = SubtractVector(CornerC, CornerA);
            const Vector3d GeometricNormal = CrossProduct(EdgeAB, EdgeAC);
            if (DotProduct(GeometricNormal, ObjectRay.Direction) > 0.0) continue;   // back face — away from the viewer
        }

        double HitDistance = 0.0, HitU = 0.0, HitV = 0.0;
        if (!ResolveRayTriangleIntersection(ObjectRay, CornerA, CornerB, CornerC, HitDistance, HitU, HitV)) continue;

        if (!NearestFound || HitDistance < NearestDistance)
        {
            NearestFound    = true;
            NearestDistance = HitDistance;
            NearestTriangle = TriangleOrdinal;
            NearestU        = HitU;
            NearestV        = HitV;
        }
    }

    if (!NearestFound) return Outcome;   // HitEnabled stays false

    // --- Resolve the nearest hit to its face / vertex / edge. --------------------------------------------------------
    const TriangleOrigin& Origin = Display.TriangleOrigins[NearestTriangle];
    const uint32_t VertexIndexA = ResolveCornerVertex(Cluster, Origin.SourceCorner[0]);
    const uint32_t VertexIndexB = ResolveCornerVertex(Cluster, Origin.SourceCorner[1]);
    const uint32_t VertexIndexC = ResolveCornerVertex(Cluster, Origin.SourceCorner[2]);

    // Barycentric weights: WeightA = 1 - U - V (corner A), WeightB = U (corner B), WeightC = V (corner C).
    const double WeightA = 1.0 - NearestU - NearestV;
    const double WeightB = NearestU;
    const double WeightC = NearestV;

    Outcome.HitEnabled      = true;
    Outcome.TriangleOrdinal = NearestTriangle;
    Outcome.Distance        = NearestDistance;
    Outcome.Face            = Origin.SourceFace;
    Outcome.HitPoint        = AddVector(ObjectRay.Origin, ScaleVector(ObjectRay.Direction, NearestDistance));

    // 📝 Snapped vertex: the corner carrying the largest weight (the hit sits nearest it).
    if (WeightA >= WeightB && WeightA >= WeightC)      Outcome.Vertex = VertexIndexA;
    else if (WeightB >= WeightA && WeightB >= WeightC) Outcome.Vertex = VertexIndexB;
    else                                               Outcome.Vertex = VertexIndexC;

    // 📝 Nearest triangle side: opposite the SMALLEST-weight corner (that corner is farthest, so the hit is nearest the side
    //    joining the other two). If those two source vertices form a real loop edge in the adjacency, that is the picked edge;
    //    otherwise the side is a fan diagonal and Edge stays InvalidEdgeKey (edge mode never selects a fake edge).
    uint32_t SideStart, SideEnd;
    if (WeightA <= WeightB && WeightA <= WeightC)      { SideStart = VertexIndexB; SideEnd = VertexIndexC; }  // A smallest -> side BC
    else if (WeightB <= WeightA && WeightB <= WeightC) { SideStart = VertexIndexC; SideEnd = VertexIndexA; }  // B smallest -> side CA
    else                                               { SideStart = VertexIndexA; SideEnd = VertexIndexB; }  // C smallest -> side AB

    const uint64_t CandidateKey = EncodeEdgeKey(SideStart, SideEnd);
    if (Adjacency.EdgeFaces.find(CandidateKey) != Adjacency.EdgeFaces.end())
        Outcome.Edge = CandidateKey;   // a real loop edge
    // else: fan diagonal — Edge stays InvalidEdgeKey

    return Outcome;
}

} // namespace Frontier
