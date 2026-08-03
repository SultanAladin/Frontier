// ============================================================================================================================================
//                                                          COMPONENTOVERLAY.FRAG
// ============================================================================================================================================
// 🧩 Fragment stage of the component (sub-object) overlay. For each pixel: read the resolved identity, split it into partition + primitive, walk
//    partition -> instance -> model matrix and primitive -> indices -> the triangle's three corners, transform those corners to SCREEN space, and
//    measure this pixel's distance to each corner (vertex mode) or to each edge segment (edge mode). Close enough to a handle -> draw it, tinted by
//    whether that exact component is the hovered one (orange) or the selected one (blue).
//
// 💡 WHY RECONSTRUCT INSTEAD OF DRAWING POINTS AND LINES. A conventional overlay issues a second geometry pass as GL_POINTS / GL_LINES, which costs a
//    vertex walk proportional to the model, needs a depth bias to sit on its own surface without z-fighting, and draws handles for geometry that is
//    not even visible. Reconstructing from the id buffer inverts all three: the cost is one fullscreen pass regardless of polygon count, there is no
//    bias fight because the handle is derived from the very pixel that won the depth test, and a vertex hidden behind the surface simply never has a
//    pixel that names its triangle, so it is culled for free.
//
// ⚠️ THE RECONSTRUCTION IS ONLY VALID FOR THE PIXEL'S OWN TRIANGLE. A pixel can only draw handles belonging to the primitive the id names AT THAT
//    PIXEL. That is what makes the overlay depth-correct, and it is also its one structural limit: a vertex dot is clipped by the silhouette of its
//    own triangle, so a dot straddling a silhouette edge shows only the inside half. Drawing the outside half would require sampling a neighbourhood
//    and would immediately reintroduce the occlusion-contour bug the selection outline had to solve — a handle would bleed onto whatever occludes it.
//    Clipped-to-silhouette is the honest, correct read here.
#version 450

layout(location = 0) in  vec2 FragTexCoord;
layout(location = 0) out vec4 OutColour;

// 📝 The resolved identity. usampler2D + NEAREST: a filtered id averages two unrelated ordinals into a meaningless third, which here would
//    reconstruct an entirely wrong triangle.
layout(set = 0, binding = 0) uniform usampler2D IdentityImage;

// 📝 Scene depth, point-sampled. Used to unproject THIS pixel back to a world position so the face-mode fill and the handle tests share the same
//    surface the raster resolved.
// ⚠️ STANDARD Z (VK_COMPARE_OP_LESS_OR_EQUAL, cleared to 1.0 — see VisibilityRasterization.cpp): smaller depth is NEARER, far plane is 1.0.
layout(set = 0, binding = 1) uniform sampler2D SceneDepthImage;

// One RenderVertex, stride-32: position @0, normal @12, texcoord @24. Only POSITION is read here, but the full layout is declared so the std430
// stride stays 32 and indexing matches the buffer the raster and the shade pass share.
struct RenderVertex
{
    float PositionX; float PositionY; float PositionZ; // @0
    float NormalX;   float NormalY;   float NormalZ;   // @12
    float TexU;      float TexV;                       // @24
};

layout(std430, set = 0, binding = 2) readonly buffer VertexBlock
{
    RenderVertex Vertices[];
};

layout(std430, set = 0, binding = 3) readonly buffer IndexBlock
{
    uint Indices[];
};

// Mirrors SuzanneSceneInstance / VisibilityRaster.vert's SceneInstance (std140). Only Model and PartitionId are read.
// 🔴 MIRRORS SuzanneSceneInstance (Graphics/Scene/SuzanneScene.h) FIELD FOR FIELD — 208 bytes. Three other shaders carry the same hand-written copy
//    (VisibilityRaster.vert, SurfaceShade.frag, SoftwareRasterization.comp). A copy that falls behind the C++ struct still compiles and still
//    validates; it just strides by the wrong size, so instance N reads the tail of instance N-1. The host static_assert is what catches it.
struct SceneInstance
{
    mat4 Model;
    vec4 NormalBasis[3];
    vec4 Tint;
    uint PartitionId;
    uint MaterialId;
    uint MeshOrdinal;    // bottom-level tree slice — ray tracing only, unused here
    uint Pad0;
    mat4 InverseModel;   // world -> local, for the ray trace — unused here
};

layout(std140, set = 0, binding = 4) readonly buffer InstanceBlock
{
    SceneInstance Instances[];
};

// 💡 THE AUTHORED-TOPOLOGY PROVENANCE — the reason this overlay addresses ngons and quads rather than the triangles they were cut into. The raster's id
//    names a display TRIANGLE, but a modeller selects the polygon they built; these three tables carry that mapping, resolved once at scene load
//    (ResolveAuthoredTopology) because deciding "is this side a real edge" is a set-membership test over every loop edge — a hash lookup on the CPU, a
//    linear scan per pixel on the GPU. Entry T describes the triangle whose indices are 3T..3T+2; CornerVertex / SideEdge hold three entries per
//    triangle, indexed 3T + slot.
// ⚠️ Every read is length()-guarded. An unbound or short table means the authored view is unavailable, and the correct degradation is to draw NOTHING
//    rather than to fall back to triangle keys — a silent fallback would look like the bug this change fixes.
layout(std430, set = 0, binding = 5) readonly buffer SourceFaceBlock
{
    uint SourceFace[];        // per triangle: authored face ordinal
};

layout(std430, set = 0, binding = 6) readonly buffer CornerVertexBlock
{
    uint CornerVertex[];      // per triangle corner: CLUSTER vertex index (shared across faces, unlike the render index)
};

layout(std430, set = 0, binding = 7) readonly buffer SideEdgeBlock
{
    uint SideEdge[];          // per triangle side: authored edge ordinal, or InvalidAuthoredEdge for a fan diagonal
};

layout(push_constant) uniform ComponentConstants
{
    mat4  ViewProjection;        // [-]  - world -> clip, for projecting reconstructed corners to screen
    vec4  HandleColour;          // [-]  - unhighlighted handle RGBA
    vec4  HoverColour;           // [-]  - hovered component RGBA (orange)
    vec4  SelectedColour;        // [-]  - selected component RGBA (blue)
    vec4  WireColour;            // [-]  - face-mode boundary wire RGBA

    uint  ComponentMode;         // [-]  - 0 Object (draw nothing), 1 Vertex, 2 Edge, 3 Face
    uint  SelectedPartition;     // [-]  - committed partition
    uint  SelectedPrimitive;     // [-]  - committed primitive
    uint  SelectedComponent;     // [-]  - committed component key (mesh vertex index / edge key); sentinel = whole primitive
    uint  FloorPartitionBase;    // [-]  - ordinals >= this are floor (geometry not bound; rejected)
    uint  AuthoredTriangleCount; // [-]  - triangles the authored tables cover; 0 = authored view unavailable, draw nothing

    int   CursorX;               // [px] - cursor position, or negative when the pointer is outside the window
    int   CursorY;               // [px] - cursor position, or negative when the pointer is outside the window

    float VertexDotRadius;       // [px] - half-width of a vertex dot
    float EdgeLineWidth;         // [px] - half-width of an edge line
    float FaceTintStrength;      // [-]  - alpha of the face fill
} Constants;

// ---- Identity pack (must match VisibilityRaster.frag / SoftwareRasterization.comp) ----
const uint PrimitiveBits      = 20u;
const uint PrimitiveMask      = (1u << PrimitiveBits) - 1u;
const uint VisibilitySentinel = 0xFFFFFFFFu;
const uint NoSelectionSentinel = 0xFFFFFFFFu;

// "This triangle side is a fan diagonal, not an authored edge" (must match InvalidAuthoredEdge in WorkspaceDocumentDecoder.h).
const uint InvalidAuthoredEdge = 0xFFFFFFFFu;

// ---- Component modes (must match ComponentSelectionMode in ComponentOverlayInscription.h) ----
const uint ModeObject = 0u;
const uint ModeVertex = 1u;
const uint ModeEdge   = 2u;
const uint ModeFace   = 3u;

vec3 PositionForVertex(uint VertexIndex)
{
    RenderVertex Vertex = Vertices[VertexIndex];
    return vec3(Vertex.PositionX, Vertex.PositionY, Vertex.PositionZ);
}

// 📝 World -> screen pixels, using the FORWARD view-projection straight from the push block. An earlier draft passed the inverse (to mirror
//    SurfaceShadeConstants, which genuinely needs clip->world for its view rays) and called inverse() here — a full mat4 inversion per FRAGMENT to
//    transform three points in one direction. This pass never unprojects, so the forward matrix is simply the right thing to hand it.
// ⚠️ The w <= 0 guard is not optional: a corner behind the eye divides by a negative w and lands at a mirrored on-screen position, which would draw a
//    phantom handle in the wrong place. Such corners are reported behind and excluded by the caller rather than clamped.
vec2 ProjectToScreen(vec3 WorldPosition, vec2 ScreenSize, out bool OutBehind)
{
    vec4 Clip = Constants.ViewProjection * vec4(WorldPosition, 1.0);
    OutBehind = Clip.w <= 1e-6;                         // behind the eye: no meaningful screen position
    if (OutBehind)
        return vec2(-1e6);
    vec3 Ndc = Clip.xyz / Clip.w;                       // Vulkan NDC: xy in [-1,1] with y DOWN
    return (Ndc.xy * 0.5 + 0.5) * ScreenSize;
}

// Distance in pixels from Point to the SEGMENT AB — not to the infinite line. Clamping the projection parameter to [0,1] is what keeps an edge
// highlight from extending past its endpoints into the neighbouring face.
float DistanceToSegment(vec2 Point, vec2 A, vec2 B)
{
    vec2  Span      = B - A;
    float SpanLenSq = dot(Span, Span);
    if (SpanLenSq < 1e-12)
        return length(Point - A);                       // degenerate edge collapsed to a point
    float Parameter = clamp(dot(Point - A, Span) / SpanLenSq, 0.0, 1.0);
    return length(Point - (A + Parameter * Span));
}

// 📝 The identity + geometry a single texel resolves to. Valid is false whenever the texel names nothing this pass can reconstruct — an empty pixel, a
//    floor partition (geometry not bound), or an out-of-range index — and every caller must check it before trusting the rest.
struct TexelSurface
{
    bool  Valid;
    uint  Partition;
    uint  Primitive;
    uint  MeshIndex[3];   // the triangle's three RENDER vertex indices, in winding order (positions only — see below)
    vec2  Screen[3];      // those corners projected to screen pixels
    bool  Behind[3];      // corner is behind the eye; its screen position is meaningless

    // 📝 The AUTHORED identities of the same triangle, which is what a component highlight is keyed on. MeshIndex above is only ever used to FETCH
    //    POSITIONS — it is a corner slot, not an identity, because ConstructDisplayPolygons expands one render vertex per face corner, so a cluster
    //    vertex shared by four quads wears four different render indices. Keying on it would light one face's copy of a vertex and leave the other
    //    three dark, which is exactly the triangulated-view bug this table set removes.
    uint  AuthoredFace;       // authored face ordinal (all fan triangles of one ngon share it)
    uint  AuthoredVertex[3];  // per corner: cluster vertex index
    uint  AuthoredEdge[3];    // per side S (corner S -> corner S+1): authored edge ordinal, or InvalidAuthoredEdge if a fan diagonal
    bool  TopologyValid;      // the provenance tables covered this triangle; false = authored view unavailable, draw nothing
};

// 📝 The whole reconstruction for ONE texel, factored out so the current pixel and the CURSOR pixel go through byte-identical code. That sharing is the
//    point: hover is decided by comparing the component key here against the key there, and if the two were computed by separate code paths any
//    disagreement between them would show up as a highlight that flickers or lands on the wrong handle.
TexelSurface ResolveTexelSurface(ivec2 Texel, ivec2 Bounds)
{
    TexelSurface Surface;
    Surface.Valid = false;
    Surface.Partition = 0u;
    Surface.Primitive = 0u;
    Surface.AuthoredFace  = NoSelectionSentinel;
    Surface.TopologyValid = false;
    for (int Slot = 0; Slot < 3; ++Slot)
    {
        Surface.MeshIndex[Slot] = 0u;
        Surface.Screen[Slot]    = vec2(-1e6);
        Surface.Behind[Slot]    = true;
        Surface.AuthoredVertex[Slot] = NoSelectionSentinel;
        Surface.AuthoredEdge[Slot]   = InvalidAuthoredEdge;
    }

    if (Texel.x < 0 || Texel.y < 0 || Texel.x >= Bounds.x || Texel.y >= Bounds.y)
        return Surface;

    uint Identity = texelFetch(IdentityImage, Texel, 0).r;
    if (Identity == VisibilitySentinel)
        return Surface;                                 // empty pixel

    Surface.Partition = Identity >> PrimitiveBits;
    Surface.Primitive = Identity & PrimitiveMask;

    // ⚠️ Floor partitions are rejected, NOT clamped. The floor rasterizes into this same id buffer but its vertex / index buffers are not bound here
    //    (see the header), so indexing the heads' SSBO with a floor index would read an unrelated position and draw a handle in mid-air. Rejecting is
    //    the only correct answer until a second geometry source is bound.
    if (Surface.Partition >= Constants.FloorPartitionBase)
        return Surface;

    // partition -> instance. The partition ordinal indexes the instance array directly (the raster assigns PartitionId = instance index).
    if (Surface.Partition >= uint(Instances.length()))
        return Surface;
    mat4 Model = Instances[Surface.Partition].Model;

    // primitive -> the triangle's three indices -> world-space corners.
    uint IndexBase = Surface.Primitive * 3u;
    if (IndexBase + 2u >= uint(Indices.length()))
        return Surface;
    Surface.MeshIndex[0] = Indices[IndexBase + 0u];
    Surface.MeshIndex[1] = Indices[IndexBase + 1u];
    Surface.MeshIndex[2] = Indices[IndexBase + 2u];
    if (max(max(Surface.MeshIndex[0], Surface.MeshIndex[1]), Surface.MeshIndex[2]) >= uint(Vertices.length()))
        return Surface;

    vec2 ScreenSize = vec2(Bounds);
    for (int Slot = 0; Slot < 3; ++Slot)
    {
        vec3 World = (Model * vec4(PositionForVertex(Surface.MeshIndex[Slot]), 1.0)).xyz;
        bool Behind;
        Surface.Screen[Slot] = ProjectToScreen(World, ScreenSize, Behind);
        Surface.Behind[Slot] = Behind;
    }

    // The authored provenance of this same triangle. Tables are parallel to the triangle list, so the primitive ordinal indexes them directly. All three
    // must cover it — a partial upload leaves TopologyValid false and the caller draws nothing rather than mixing authored and triangle keys.
    //
    // ⚠️ AuthoredTriangleCount is the gate, and a length() test is NOT an acceptable substitute for it. When the tables have not been uploaded, the host
    //    still has to keep bindings 5-7 legally bound (an unbound descriptor is undefined memory, not an empty buffer), so it aliases them onto the index
    //    buffer — which then reports a healthy non-zero length while holding index data. Gating on length() would pass and reinterpret those indices as
    //    face ordinals, painting confident nonsense. The pushed count is the only value that knows whether the tables are real.
    uint CornerBase = Surface.Primitive * 3u;
    if (Surface.Primitive < Constants.AuthoredTriangleCount
        && Surface.Primitive < uint(SourceFace.length())
        && CornerBase + 2u < uint(CornerVertex.length())
        && CornerBase + 2u < uint(SideEdge.length()))
    {
        Surface.AuthoredFace = SourceFace[Surface.Primitive];
        for (int Slot = 0; Slot < 3; ++Slot)
        {
            Surface.AuthoredVertex[Slot] = CornerVertex[CornerBase + uint(Slot)];
            Surface.AuthoredEdge[Slot]   = SideEdge[CornerBase + uint(Slot)];
        }
        Surface.TopologyValid = true;
    }

    Surface.Valid = true;
    return Surface;
}

// 📝 The nearest VERTEX to Point, as an AUTHORED (cluster) vertex index. Returns the sentinel when no corner is within Radius or the provenance is
//    unavailable.
// ⚠️ The key is the CLUSTER vertex index, never the render index. Both are uints and both are "the vertex", which is what makes this easy to get wrong:
//    the render stream carries one vertex PER FACE CORNER, so a cluster vertex shared by four quads exists four times in it under four different
//    indices. Keying on the render index therefore highlights only the copy belonging to the triangle under the cursor, and the same physical vertex
//    reads as a different component from each adjacent face — the vertex appears to flicker as the cursor crosses a face boundary.
uint NearestVertexKey(TexelSurface Surface, vec2 Point, float Radius)
{
    if (!Surface.TopologyValid)
        return NoSelectionSentinel;

    float BestDistance = 1e9;
    uint  BestKey      = NoSelectionSentinel;
    for (int Slot = 0; Slot < 3; ++Slot)
    {
        if (Surface.Behind[Slot]) continue;
        if (Surface.AuthoredVertex[Slot] == NoSelectionSentinel) continue;
        vec2  Delta     = Point - Surface.Screen[Slot];
        float Chebyshev = max(abs(Delta.x), abs(Delta.y));   // square dot: two abs/max instead of a length, crisp at 3px
        if (Chebyshev <= Radius && Chebyshev < BestDistance)
        {
            BestDistance = Chebyshev;
            BestKey      = Surface.AuthoredVertex[Slot];
        }
    }
    return BestKey;
}

// 📝 The nearest AUTHORED EDGE to Point, as an authored edge ordinal. Returns the sentinel when no authored edge is within Width.
//
// 💡 FAN DIAGONALS ARE SKIPPED, and this is the heart of the authored-topology fix. Triangulating a quad cuts it with an interior diagonal that exists
//    only in the display mesh; triangulating an ngon adds several. Those sides are geometrically real but topologically invented, so highlighting one
//    would offer the modeller an edge their model does not contain — and selecting it would be meaningless to any edit operation. The load-time
//    resolution already marked each side (SideEdge slot == InvalidAuthoredEdge for a diagonal), so rejecting them here is a single compare.
//
// ⚠️ The key is the authored edge ORDINAL, which also retires a latent overflow: the previous key packed the endpoint pair as min*65536 + max, which
//    silently collides once a vertex index reaches 65536 (it happened to work only because this scene is smaller than that). An ordinal is one uint per
//    unique loop edge by construction, so it is order-independent and collision-free at any mesh size — the two triangles sharing an edge resolve to the
//    same ordinal without any packing at all.
uint NearestEdgeKey(TexelSurface Surface, vec2 Point, float Width)
{
    if (!Surface.TopologyValid)
        return NoSelectionSentinel;

    float BestDistance = 1e9;
    uint  BestKey      = NoSelectionSentinel;
    for (int Slot = 0; Slot < 3; ++Slot)
    {
        int Next = (Slot + 1) % 3;
        if (Surface.Behind[Slot] || Surface.Behind[Next]) continue;
        if (Surface.AuthoredEdge[Slot] == InvalidAuthoredEdge) continue;   // fan diagonal: not a real edge
        float Distance = DistanceToSegment(Point, Surface.Screen[Slot], Surface.Screen[Next]);
        if (Distance <= Width && Distance < BestDistance)
        {
            BestDistance = Distance;
            BestKey      = Surface.AuthoredEdge[Slot];
        }
    }
    return BestKey;
}

// 📝 The component the CURSOR is over, resolved by re-running the identical reconstruction at the cursor texel. This is what removes the readback: the
//    host never has to learn which component is hovered, because the only consumer of that fact is this shader, and it can derive it directly.
// ⚠️ The cursor's own SEARCH RADIUS is deliberately generous (a whole dot-radius / line-width), while the DRAWN handle uses the same figure. A cursor
//    sitting just off a dot therefore still hovers it, which is what makes small handles clickable — tightening this to sub-pixel would make hover feel
//    broken at exactly the zoom levels where it matters most.
// 📝 The primitive is deliberately NOT reported. It was, until every mode gained a real authored key; keeping it would only tempt a future reader into
//    comparing it, which is the one thing that breaks whole-component highlighting (see ResolveHandleColour).
bool ResolveCursorComponent(ivec2 Bounds, out uint OutPartition, out uint OutComponent)
{
    OutPartition = NoSelectionSentinel;
    OutComponent = NoSelectionSentinel;

    // Negative cursor = pointer outside the window. Nothing is hovered, and no reconstruction is attempted.
    if (Constants.CursorX < 0 || Constants.CursorY < 0)
        return false;

    ivec2        CursorTexel = ivec2(Constants.CursorX, Constants.CursorY);
    TexelSurface Surface     = ResolveTexelSurface(CursorTexel, Bounds);
    if (!Surface.Valid)
        return false;

    OutPartition = Surface.Partition;

    vec2 CursorCentre = vec2(CursorTexel) + vec2(0.5);
    if (Constants.ComponentMode == ModeVertex)
    {
        OutComponent = NearestVertexKey(Surface, CursorCentre, max(Constants.VertexDotRadius, 1.0));
        return OutComponent != NoSelectionSentinel;      // cursor is in the triangle but not on any dot
    }
    if (Constants.ComponentMode == ModeEdge)
    {
        OutComponent = NearestEdgeKey(Surface, CursorCentre, max(Constants.EdgeLineWidth, 0.5));
        return OutComponent != NoSelectionSentinel;
    }

    // Face mode: the AUTHORED FACE is the component. Reporting the authored ordinal (not the primitive) is what makes a hovered quad light as one unit —
    // every fan triangle of that face shares the ordinal, so they all match the hover while the neighbouring face does not.
    if (!Surface.TopologyValid)
        return false;
    OutComponent = Surface.AuthoredFace;
    return OutComponent != NoSelectionSentinel;
}

// 📝 Which colour a component carries. Selected (blue) outranks hovered (orange), so clicking the component the cursor rests on reads as committed
//    rather than staying orange under the pointer. ComponentKey is the AUTHORED identity in every mode — cluster vertex index, authored edge ordinal, or
//    authored face ordinal — so all three modes compare one uint and nothing else.
//
// ⚠️ THE PRIMITIVE IS DELIBERATELY NOT PART OF ANY COMPARISON, and re-introducing it would undo the authored-topology fix. Every authored component
//    spans SEVERAL display triangles: a vertex is shared by every face touching it, an edge by the two faces either side, and a single ngon is itself cut
//    into a fan. The cursor resolves its component through whichever triangle it happens to sit on, while the pixels making up the rest of that component
//    resolve it through different ones — so requiring the primitives to agree would light only the fragment belonging to the cursor's own triangle and
//    leave the remainder of the same quad or vertex unhighlighted. Matching on the authored key alone is what makes the whole component light as a unit.
vec4 ResolveHandleColour(uint Partition, uint ComponentKey,
                         bool HoverValid, uint HoverPartition, uint HoverComponent)
{
    // The partition still matters: two heads are separate objects, and the authored keys are per-MESH ordinals shared by every instance of it. Without
    // this, hovering a vertex on one head would light the corresponding vertex on all thirteen.
    bool SelectedMatch = Partition == Constants.SelectedPartition
                      && ComponentKey != NoSelectionSentinel
                      && ComponentKey == Constants.SelectedComponent;
    if (SelectedMatch)
        return Constants.SelectedColour;

    bool HoverMatch = HoverValid
                   && Partition == HoverPartition
                   && ComponentKey != NoSelectionSentinel
                   && ComponentKey == HoverComponent;
    if (HoverMatch)
        return Constants.HoverColour;

    return Constants.HandleColour;
}

void main()
{
    // Object mode draws no handles at all - the object outline owns that mode. Recording is skipped on the CPU too, so this is belt-and-braces.
    if (Constants.ComponentMode == ModeObject)
        discard;

    ivec2 Bounds = textureSize(IdentityImage, 0);
    ivec2 Texel  = ivec2(FragTexCoord * vec2(Bounds));

    TexelSurface Surface = ResolveTexelSurface(Texel, Bounds);
    if (!Surface.Valid)
        discard;                                        // empty pixel, floor, or unreconstructable - keep what is behind us

    // The cursor's component, resolved through the very same reconstruction. Uniform across the draw, so every invocation agrees on what is hovered.
    uint HoverPartition, HoverComponent;
    bool HoverValid = ResolveCursorComponent(Bounds, HoverPartition, HoverComponent);

    // 📝 Pixel CENTRE, not corner. Comparing a corner against a projected position biases every handle half a pixel up-left, which is visible on a
    //    3px dot.
    vec2 PixelCentre = vec2(Texel) + vec2(0.5);

    // ---- VERTEX mode: a square dot around each projected corner -------------------------------------------------------------------------------
    if (Constants.ComponentMode == ModeVertex)
    {
        uint VertexKey = NearestVertexKey(Surface, PixelCentre, max(Constants.VertexDotRadius, 1.0));
        if (VertexKey == NoSelectionSentinel)
            discard;                                    // not on any dot

        OutColour = ResolveHandleColour(Surface.Partition, VertexKey,
                                        HoverValid, HoverPartition, HoverComponent);
        return;
    }

    // ---- EDGE mode: a line along each triangle edge --------------------------------------------------------------------------------------------
    if (Constants.ComponentMode == ModeEdge)
    {
        uint EdgeKey = NearestEdgeKey(Surface, PixelCentre, max(Constants.EdgeLineWidth, 0.5));
        if (EdgeKey == NoSelectionSentinel)
            discard;                                    // not on any edge

        OutColour = ResolveHandleColour(Surface.Partition, EdgeKey,
                                        HoverValid, HoverPartition, HoverComponent);
        return;
    }

    // ---- FACE mode: tint the whole face, with a boundary wire ----------------------------------------------------------------------------------
    // 📝 Only the hovered / selected face is tinted. Tinting EVERY face would flood the frame - the wireframe already communicates the face layout,
    //    and this overlay's job in Face mode is to say which one is under the cursor.
    if (Constants.ComponentMode == ModeFace)
    {
        // 📝 Keyed on the AUTHORED face ordinal, which is what makes a quad tint as ONE unit: both of its fan triangles carry the same ordinal, so both
        //    match and the tint crosses the interior diagonal seamlessly. Keying on the primitive (as this did) tinted a single triangle, leaving the
        //    other half of the quad shaded and the diagonal glaringly visible as the tint's edge.
        if (!Surface.TopologyValid)
            discard;                                    // authored view unavailable - draw nothing rather than a triangle

        vec4 Tint = ResolveHandleColour(Surface.Partition, Surface.AuthoredFace,
                                        HoverValid, HoverPartition, HoverComponent);
        if (Tint == Constants.HandleColour)
            discard;                                    // not the hovered or selected face - leave the shaded surface alone

        // The boundary wire reads full-strength while the interior is a light tint, so the face's extent is legible without hiding its material.
        // ⚠️ This now traces only AUTHORED edges: NearestEdgeKey rejects fan-diagonal sides, so no wire is drawn across a quad's interior. That absence is
        //    the visible signature of the fix — a diagonal line through a highlighted quad means the provenance is not reaching the shader.
        bool OnBoundary = NearestEdgeKey(Surface, PixelCentre, max(Constants.EdgeLineWidth, 0.5)) != NoSelectionSentinel;

        OutColour = OnBoundary ? vec4(Tint.rgb, 1.0)
                               : vec4(Tint.rgb, clamp(Constants.FaceTintStrength, 0.0, 1.0));
        return;
    }

    discard;
}
