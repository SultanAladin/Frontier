/*============================================================================================================================================
                                                           COMPONENTUVSURFACE.CPP
============================================================================================================================================*/
// 🧩 Projects the cluster's per-corner UVs to panel pixel space through a pan + zoom affine map and constructs the base face
//    outlines plus the highlight-in-place selection passes. One O(corners) walk over the original polygons — no triangulation, no
//    depth, no camera. The face outline is the true polygon loop, so a quad reads as four edges and an N-gon as N (no fan diagonal).

#include "ComponentUVSurface.h"

#include "PolygonCluster.h"
#include "AdjacencyIndex.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // Map one UV pair to panel pixel space. The MINUS on v is the V-up (Blender) convention: v = 0 at the bottom of the canvas.
    UVScreenPoint ProjectCorner(double UCoord, double VCoord, const UVTransform& Transform)
    {
        UVScreenPoint Point;
        Point.ScreenX = Transform.CanvasCenterX + (static_cast<float>(UCoord) - Transform.PanOffsetU) * Transform.ZoomScale;
        Point.ScreenY = Transform.CanvasCenterY - (static_cast<float>(VCoord) - Transform.PanOffsetV) * Transform.ZoomScale;
        return Point;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool ResolveUVSurface(const PolygonCluster&     Cluster,
                      const AdjacencyIndex&     Adjacency,
                      const UVTransform&        Transform,
                      const UVSurfaceControl&   Control,
                      UVSurfaceOutput&          Result)
{
    (void)Adjacency;   // 📝 borrowed for API symmetry with the 3D overlay; edge keys come from the free EncodeEdgeKey below.

    Result.FaceLoops.clear();
    Result.VertexDots.clear();
    Result.BaseFills.clear();
    Result.SelectionFills.clear();
    Result.SelectionLoops.clear();
    Result.SelectionDots.clear();

    const std::vector<uint32_t>& FaceCounts  = Cluster.FaceVertexCounts;
    const std::vector<uint32_t>& FaceIndices = Cluster.FaceVertexIndices;
    const std::vector<Vector2d>& CornerUV    = Cluster.FaceCornerTexture;

    // Guard: per-corner UVs must be present and parallel to the face-index stream, and there must be faces to draw.
    if (FaceCounts.empty() || FaceIndices.empty() || CornerUV.size() != FaceIndices.size())
        return false;

    const bool WantFaceFill   = Control.SelectionFaces    != nullptr && !Control.SelectionFaces->empty();
    const bool WantEdgeHigh   = Control.EdgeOverlayEnabled && Control.SelectionEdges != nullptr && !Control.SelectionEdges->empty();
    const bool WantVertexHigh = Control.SelectionVertices != nullptr && !Control.SelectionVertices->empty();

    // One walk over the original polygons. FaceCounts[f] is face f's corner count; CornerBase is the running slice offset into
    //    both FaceIndices (vertex ids) and CornerUV (per-corner UVs) — identical to ConstructRenderVertexStream's face walk.
    uint32_t CornerBase = 0;
    for (uint32_t FaceOrdinal = 0; FaceOrdinal < FaceCounts.size(); ++FaceOrdinal)
    {
        const uint32_t CornerCount = FaceCounts[FaceOrdinal];
        if (CornerCount < 3 || CornerBase + CornerCount > FaceIndices.size())
            return false;   // malformed slice — reject the whole frame rather than draw a partial face.

        const bool FaceSelected = Control.SelectionFaces != nullptr &&
                                  Control.SelectionFaces->find(FaceOrdinal) != Control.SelectionFaces->end();

        // Base outline (always) + optional selected-face fill / bright outline. Project every corner once into a scratch loop.
        UVPolyline BaseLoop;
        BaseLoop.Closed = true;
        BaseLoop.Points.reserve(CornerCount);
        UVFillPolygon Fill;
        if (FaceSelected && WantFaceFill)
            Fill.Points.reserve(CornerCount);

        for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
        {
            const uint32_t Slot = CornerBase + Corner;
            const UVScreenPoint Point = ProjectCorner(CornerUV[Slot].XCoord, CornerUV[Slot].YCoord, Transform);
            BaseLoop.Points.push_back(Point);
            if (FaceSelected && WantFaceFill)
                Fill.Points.push_back(Point);

            // Base vertex overlay: dot every corner point when the pass is enabled.
            if (Control.VertexOverlayEnabled)
                Result.VertexDots.push_back(Point);

            // Selected-vertex highlight: this corner's underlying vertex id is in the selected set → bright dot (a seam vertex
            //    shared by two faces lights up in both its corner positions, so both UV islands show the selection).
            if (WantVertexHigh)
            {
                const uint32_t VertexId = FaceIndices[Slot];
                if (Control.SelectionVertices->find(VertexId) != Control.SelectionVertices->end())
                    Result.SelectionDots.push_back(Point);
            }
        }

        // Selected single edges: for each consecutive corner pair (wrap-around), encode its vertex-pair key; if selected, stroke
        //    a bright two-point run. This is seam-aware — the edge is tested by its vertex ids, drawn at these face-local UVs.
        if (WantEdgeHigh)
        {
            for (uint32_t Corner = 0; Corner < CornerCount; ++Corner)
            {
                const uint32_t SlotA = CornerBase + Corner;
                const uint32_t SlotB = CornerBase + (Corner + 1) % CornerCount;
                const uint64_t EdgeKey = EncodeEdgeKey(FaceIndices[SlotA], FaceIndices[SlotB]);
                if (Control.SelectionEdges->find(EdgeKey) != Control.SelectionEdges->end())
                {
                    UVPolyline Segment;
                    Segment.Closed = false;
                    Segment.Points.push_back(BaseLoop.Points[Corner]);
                    Segment.Points.push_back(BaseLoop.Points[(Corner + 1) % CornerCount]);
                    Result.SelectionLoops.push_back(std::move(Segment));
                }
            }
        }

        // Neutral shell fill: one convex fill per face under the outlines, built from the same projected loop (the runtime tints
        //    it a soft translucent grey-blue). Constructed for every face when the pass is on — independent of selection.
        if (Control.ShellFillEnabled)
        {
            UVFillPolygon ShellFill;
            ShellFill.Points = BaseLoop.Points;
            Result.BaseFills.push_back(std::move(ShellFill));
        }

        // Record this face: the fill + bright outline first (so the runtime can layer selection over the dim base), then the
        //    base outline (always constructed when the face pass is on, so the whole atlas stays visible under the highlight).
        if (FaceSelected && WantFaceFill)
        {
            Result.SelectionFills.push_back(std::move(Fill));
            UVPolyline BrightOutline = BaseLoop;
            Result.SelectionLoops.push_back(std::move(BrightOutline));
        }
        if (Control.FaceOverlayEnabled)
            Result.FaceLoops.push_back(std::move(BaseLoop));

        CornerBase += CornerCount;
    }

    return true;
}

} // namespace Frontier
