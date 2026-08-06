/*==============================================================================================================================================
                                                            BOUNDARYTESSELLATION.H
==============================================================================================================================================*/
// 🧩 The DERIVED streams a viewport needs from a boundary body: triangles for the shaded pass, edge segments for the wireframe + edge pick, and
//    vertex handles for the corner pick. All three are caches — FullBrepBody is the source of truth, and any of these can be discarded and rebuilt.
//    Each carries the body Revision it was derived at, so a consumer re-uploads only when the body actually moved.
//
//    🔴 THE REVISION IS THE BODY'S, NOT A HASH OF THE OUTPUT. Hashing the emitted triangles would jitter whenever tessellation density shifted and
//       provoke a re-upload every frame — the transfer-queue exhaustion the sketch store's own body-revision comment records. The body bumps its
//       revision on real change only, so gating on it is both correct and stable across idle frames.
//
//    🔴 EVERY TRIANGLE CARRIES ITS SOURCE FACE TOKEN. That is what makes a shaded-pass pick resolve back to editable topology: click a pixel, read the
//       triangle, get the face, walk its loop to the corners. A stream that dropped provenance would reproduce exactly the inert-solid problem this
//       whole structure exists to fix.

#pragma once
#ifndef FRONTIER_AUTHORING_PARAMETRICAUTHORING_BOUNDARY_BOUNDARYTESSELLATION_H
#define FRONTIER_AUTHORING_PARAMETRICAUTHORING_BOUNDARY_BOUNDARYTESSELLATION_H

#include "BoundaryTopology.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One shaded vertex: position + the face normal it inherited. Flat-shaded per face (no averaging), because a B-rep's faces are genuinely distinct
//    surfaces and smoothing across them would round off the very corners the user is trying to select.
struct BoundaryRenderVertex
{
    float PositionX = 0.0f, PositionY = 0.0f, PositionZ = 0.0f;   // [mm]
    float NormalX   = 0.0f, NormalY   = 0.0f, NormalZ   = 1.0f;   // [-]
};

// 📝 The triangle stream plus its provenance run: TriangleFaces[i] is the face that produced triangle i (indices 3i, 3i+1, 3i+2). Parallel arrays
//    rather than a per-vertex tag, since the tag is uniform across a triangle and a vertex may be shared.
struct BoundaryRenderStream
{
    std::vector<BoundaryRenderVertex> Vertices;
    std::vector<uint32_t>             Indices;         // [-] - triangle triples
    std::vector<FaceToken>            TriangleFaces;   // [-] - one per triangle; the pick's route back to topology
    uint32_t                          Revision = 0u;   // [-] - the body Revision this was derived at
};

// 📝 One pickable edge: its two endpoints in world mm, its token, and its radial count (so a rim can be drawn differently from a manifold edge —
//    a sheet's open boundary reads distinctly, which is genuinely useful feedback during an extrude).
struct BoundaryEdgeSegment
{
    float     StartX = 0.0f, StartY = 0.0f, StartZ = 0.0f;   // [mm]
    float     EndX   = 0.0f, EndY   = 0.0f, EndZ   = 0.0f;   // [mm]
    EdgeToken Edge;
    uint32_t  RadialCount = 0u;                              // [-] - 0 wire · 1 rim · 2 manifold · 3+ non-manifold
};

struct BoundaryEdgeStream
{
    std::vector<BoundaryEdgeSegment> Segments;
    uint32_t                         Revision = 0u;
};

// 📝 One pickable corner: its position, its token, and how many edges meet there (a valence readout the viewport can use to size the handle).
struct BoundaryVertexHandle
{
    float       PositionX = 0.0f, PositionY = 0.0f, PositionZ = 0.0f;   // [mm]
    VertexToken Vertex;
    uint32_t    Valence = 0u;                                           // [-] - incident edge count
};

struct BoundaryVertexStream
{
    std::vector<BoundaryVertexHandle> Handles;
    uint32_t                          Revision = 0u;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Triangulate every live face into OutStream (cleared first). Each face is projected onto its dominant plane and ear-clipped, with hole rings
// bridged into the outer ring first so a punched face triangulates correctly. Convex faces (the overwhelming majority — every wall quad) take a
// fan fast path. Every triangle records its source face.
void AssembleBoundaryRenderStream(const FullBrepBody& Body, BoundaryRenderStream& OutStream);

// Fill OutStream with one segment per live edge (cleared first).
void AssembleBoundaryEdgeStream(const FullBrepBody& Body, BoundaryEdgeStream& OutStream);

// Fill OutStream with one handle per live vertex (cleared first).
void AssembleBoundaryVertexStream(const FullBrepBody& Body, BoundaryVertexStream& OutStream);

// Whether a cached stream predates the body and must be rebuilt — the one gate every consumer should call before re-deriving or re-uploading.
inline bool QueryBoundaryStreamStale(const FullBrepBody& Body, uint32_t StreamRevision)
{
    return StreamRevision != Body.Revision;
}

// Rebuild all three streams only when stale, returning true when a rebuild happened (so the caller knows to re-upload).
bool RefreshBoundaryStreams(const FullBrepBody&   Body,
                            BoundaryRenderStream& RenderStream,
                            BoundaryEdgeStream&   EdgeStream,
                            BoundaryVertexStream& VertexStream);

} // namespace Frontier

#endif   // FRONTIER_AUTHORING_PARAMETRICAUTHORING_BOUNDARY_BOUNDARYTESSELLATION_H
