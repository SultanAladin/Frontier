/*==============================================================================================================================================
                                                            TWOLEVELTRACE.GLSL
==============================================================================================================================================*/
// 🧩 The ray walk over both levels of the acceleration structure: descend the TOP-level tree (a Karras radix tree over placed instances, rebuilt on
//    the GPU every frame), and at each instance it reaches, transform the ray into that mesh's LOCAL space and descend the BOTTOM-level tree (a
//    SAH tree built once on the CPU at asset load). One closest hit over the whole scene comes back.
//
//    🔴 THIS IS AN #include MODULE AND HAS NO main() AND NO BINDINGS OF ITS OWN. Every consumer — the surfel trace, ambient occlusion, shadow rays —
//       owns its own dispatch and differs only in where rays come from and what it does with a hit. Declaring the buffers here would force all of
//       them onto one descriptor layout and one set index forever. The includer declares them, under the names this file expects, and the contract
//       below is what it must satisfy. That contract is checked by nothing but the linker's silence, so it is spelled out rather than implied.
//
//    ⚠️ COMPILE WITH glslc AND AN -I PATH, NOT glslangValidator. A bare .glsl is never discovered by ShaderPlan.ps1's .comp glob; it is compiled
//       only through whichever .comp includes it, and only glslc resolves #include at all. Same constraint ShadowTileStore.glsl already carries.
//
//    📝 THE INCLUDER MUST DECLARE, BEFORE #include-ing THIS FILE:
//         SceneInstance      Instances[]        - mirroring SuzanneSceneInstance (208 B); Model and InverseModel are the fields read here
//         GeometryArenaSlice Slices[]           - mirroring GeometryArenaSlice (32 B)
//         uint               ArenaNodeWords[]   - the concatenated BOTTOM-level node blob
//         uint               ArenaPrimitives[]  - the concatenated primitive-order table
//         uint               TreeNodeWords[]    - the TOP-level node words (internal nodes then leaves)
//         uint               MeshIndices[]      - the shared index stream
//         vec3 PositionForVertex(uint VertexIndex)  - the shared vertex stream's position reader
//         uint TraceInstanceCount, uint TraceSliceCount  - as uniforms or push constants
//
//       📝 POSITIONS COME THROUGH AN ACCESSOR RATHER THAN A RAW float[] BECAUSE GLSL CANNOT PASS AN SSBO BLOCK AS AN ARGUMENT. SurfaceShade.frag
//          already carries the same shape for the same reason (its two near-identical PositionForVertex bodies are the language's price for two
//          storage blocks, not duplication). Routing through a function also lets a consumer bind whichever stream it owns without this module
//          having to know the vertex struct at all.

#ifndef FRONTIER_ACCELERATION_TWOLEVELTRACE_GLSL
#define FRONTIER_ACCELERATION_TWOLEVELTRACE_GLSL

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 Mirrors GeometryTreeWordsPerNode / GeometryTreeLeafFlag (Acceleration/GeometryTreeBuild.h), and matches InstanceTreeBuild.comp and
//    InstanceTreeRefit.comp. BOTH levels use the same 8-word node and the same leaf marker; only the INTERIOR words 6/7 differ in meaning, which
//    is documented at each descent below.
const uint TreeWordsPerNode = 8u;
const uint TreeLeafFlag     = 0xFFFFu;

// 📝 The shared vertex stream's stride in FLOATS. RenderVertex is 32 B — position at byte 0, normal at 12, uv at 24 — so a position is three floats
//    starting at (VertexIndex * 8). Only the position is read here; a ray does not care about the rest.
const uint RenderVertexStrideFloats = 8u;

// 🔴 THE BOTTOM-LEVEL STACK IS SIZED BY THE BUILDER'S HARD DEPTH LIMIT, NOT BY A GUESS. GeometryTreeMaxDepth is 40 and the builder turns any node
//    reaching it into a leaf, so no BLAS descent can ever push more than 40 deferred siblings. 64 is that bound with headroom, and it makes the
//    stack provably sufficient rather than probably sufficient.
const uint BottomLevelStackSize = 64u;

// ⚠️ THE TOP-LEVEL STACK HAS NO SUCH GUARANTEE AND THAT IS WHY IT IS GUARDED. A Karras tree's depth is data-dependent: clustered instances produce a
//    deeper tree than uniform ones, and a degenerate scene where every instance shares a Morton code degrades toward a linked list of depth N. 64
//    covers a balanced tree of 2^64 instances and every realistic scene, but "every realistic scene" is not a proof, so TraceTwoLevel reports an
//    overflow instead of writing past the array. See TraceHit::OverflowCondition.
const uint TopLevelStackSize = 64u;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One closest hit. Distance is in WORLD units at both levels — see the note on ray transformation in TraceBottomLevel for why that holds even
//    for a non-uniformly scaled instance.
//
//    🔴 HitCondition IS THE ONLY FIELD THAT SAYS WHETHER THE REST MEAN ANYTHING. On a miss the indices are left at their sentinels and Distance at
//       the ray's far limit, which is a legitimate-looking distance; a consumer that reads Distance without checking this flag shades every missed
//       ray as if it struck something at exactly the limit.
struct TraceHit
{
    float Distance;            // [cm] - world-space distance along the ray to the hit
    vec2  Barycentric;         // [-]  - (u, v) within the hit triangle; the third weight is 1 - u - v
    uint  InstanceIndex;       // [-]  - which instance was struck, or 0xFFFFFFFF on a miss
    uint  PrimitiveIndex;      // [-]  - the mesh-local triangle index, or 0xFFFFFFFF on a miss
    bool  HitCondition;        // [-]  - true when the fields above describe a real intersection
    bool  OverflowCondition;   // [-]  - true when a traversal stack filled and a branch was dropped; the hit may be wrong, not merely absent
};

// A miss, with every field at the value a consumer should see when it ignores HitCondition and reads on anyway.
TraceHit EmptyTraceHit(float MaximumDistance)
{
    TraceHit Hit;
    Hit.Distance          = MaximumDistance;
    Hit.Barycentric       = vec2(0.0);
    Hit.InstanceIndex     = 0xFFFFFFFFu;
    Hit.PrimitiveIndex    = 0xFFFFFFFFu;
    Hit.HitCondition      = false;
    Hit.OverflowCondition = false;
    return Hit;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     INTERSECTION PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

// The slab test: does the ray meet the axis-aligned box within [NearLimit, FarLimit], and if so, at what distance does it ENTER. EntryDistance is
// clamped up to NearLimit, so a ray originating inside the box reports NearLimit rather than a negative entry — which is what a traversal wants for
// ordering, since an enclosing box should sort ahead of everything it contains.
//
// 🔴 THE RECIPROCAL IS PASSED IN, NOT COMPUTED HERE, AND THE 0 * inf CASE IS WHY THIS TAKES TWO STEPS RATHER THAN ONE. An axis-aligned ray has a zero
//    direction component, whose reciprocal is ±inf; if the origin sits exactly on a slab plane the product is 0 * inf = NaN. Every comparison against
//    NaN is false, so a naive test REJECTS the box — and it rejects it only for rays that are exactly axis-aligned, which is precisely the set a debug
//    view or a hemisphere basis fires most.
//
//    ⚠️ min/max ORDERING ALONE IS NOT ENOUGH, AND BELIEVING OTHERWISE COST THIS FILE 14 GATE FAILURES. It is true that GLSL's min(a, b) and max(a, b)
//       return the non-NaN operand when one is NaN — but the operand that survives is the OTHER PLANE'S ±inf, not a neutral value. For a ray running
//       exactly along a box's face (origin ON the maximum plane, direction parallel to it) the pair is (-inf, NaN): the near end resolves to -inf
//       correctly, and the far end resolves to -inf as well, collapsing the interval and rejecting a box the triangle test would have hit.
//
//    🔴 A NaN ON AN AXIS MEANS THAT AXIS CONSTRAINS NOTHING, SO IT IS REPLACED WITH THE UNBOUNDED INTERVAL. The ray lies exactly IN one of that slab's
//       planes and travels parallel to it, so it is on the slab boundary for every t and the honest interval is (-inf, +inf) — let the other two axes
//       decide. Substituting that is what makes this test CONSERVATIVE, which is the property an accelerator must have: rejecting a box whose
//       triangles the triangle test would accept changes the answer, while accepting a box that holds nothing merely costs a leaf visit.
//
//    📝 Clamping the reciprocal to a huge finite value is the tempting alternative and it does NOT work — it turns 0 * inf into a clean 0, which
//       collapses the same interval to [-4e37, 0] and rejects the same box. It only converts a NaN into a confident wrong answer.
//
// ⚠️ Boxes here may be INVERTED (minimum +inf, maximum -inf) rather than merely empty. That is a valid state, not corruption: the TLAS refit leaves
//    a dropped instance's leaf inverted deliberately so it fails every ray test instead of swallowing the scene, and an interior node over an
//    all-dropped subtree is then correctly inverted too. The test below rejects such a box naturally — Entry ends up greater than Exit — so no
//    special case is needed, but a future "optimisation" that assumes Minimum <= Maximum would silently start hitting them.
bool IntersectRayBox(vec3 Origin, vec3 InverseDirection, vec3 Minimum, vec3 Maximum,
                     float NearLimit, float FarLimit, out float EntryDistance)
{
    vec3 FirstPlane  = (Minimum - Origin) * InverseDirection;
    vec3 SecondPlane = (Maximum - Origin) * InverseDirection;

    // An axis whose direction component is zero contributes a NaN to one or both planes. isnan() on EITHER end marks the whole axis as unconstrained;
    // testing only the end that happens to be NaN would leave the other end's ±inf in place, which is exactly the collapse described above.
    bvec3 Unconstrained = bvec3(isnan(FirstPlane.x) || isnan(SecondPlane.x),
                                isnan(FirstPlane.y) || isnan(SecondPlane.y),
                                isnan(FirstPlane.z) || isnan(SecondPlane.z));

    vec3 NearPlane = mix(min(FirstPlane, SecondPlane), vec3(-1.0 / 0.0), Unconstrained);
    vec3 FarPlane  = mix(max(FirstPlane, SecondPlane), vec3( 1.0 / 0.0), Unconstrained);

    float Entry = max(max(NearPlane.x, NearPlane.y), max(NearPlane.z, NearLimit));
    float Exit  = min(min(FarPlane.x,  FarPlane.y),  min(FarPlane.z,  FarLimit));

    EntryDistance = Entry;
    return Entry <= Exit;
}

// Möller–Trumbore: intersect the ray with triangle ABC, reporting the distance along the ray and the barycentric pair (u, v) where the hit point is
// A + u(B-A) + v(C-A). Returns false for a miss, for a degenerate triangle, and for a hit outside (NearLimit, FarLimit].
//
// 🔴 THIS IS DOUBLE-SIDED ON PURPOSE AND MUST STAY THAT WAY. A single-sided test — rejecting a negative determinant — is right for a rasteriser and
//    wrong for global illumination: bounce rays are fired from a surface into the hemisphere and routinely strike the BACK of geometry, a light leak
//    through a wall being the obvious cost of getting it wrong. Only the determinant's MAGNITUDE is tested, so the sign (which face was struck)
//    never enters the decision.
//
// 📝 The unnormalised direction is fine here. The returned Distance is in units of the direction vector's own length, which is exactly what makes
//    the bottom level's local-space hit comparable to the top level's world-space limits — see TraceBottomLevel.
bool IntersectRayTriangle(vec3 Origin, vec3 Direction, vec3 A, vec3 B, vec3 C,
                          float NearLimit, float FarLimit,
                          out float Distance, out vec2 Barycentric)
{
    Distance    = FarLimit;
    Barycentric = vec2(0.0);

    vec3 EdgeAB = B - A;
    vec3 EdgeAC = C - A;

    vec3  Normal      = cross(Direction, EdgeAC);
    float Determinant = dot(EdgeAB, Normal);

    // ⚠️ A ray parallel to the triangle's plane, or a triangle with no area, gives a determinant at or near zero and a reciprocal that explodes. The
    //    epsilon is absolute and deliberately tiny: it is a divide-by-zero guard, NOT a "sliver triangles are unimportant" filter. Raising it to a
    //    scene-relative value would silently drop thin geometry at grazing angles, which is most of a wall seen from a surfel sitting on it.
    if (abs(Determinant) < 1e-12)
        return false;

    float InverseDeterminant = 1.0 / Determinant;

    vec3  ToOrigin = Origin - A;
    float U        = dot(ToOrigin, Normal) * InverseDeterminant;
    if (U < 0.0 || U > 1.0)
        return false;

    vec3  Perpendicular = cross(ToOrigin, EdgeAB);
    float V             = dot(Direction, Perpendicular) * InverseDeterminant;
    if (V < 0.0 || U + V > 1.0)
        return false;

    float Candidate = dot(EdgeAC, Perpendicular) * InverseDeterminant;
    if (Candidate <= NearLimit || Candidate > FarLimit)
        return false;

    Distance    = Candidate;
    Barycentric = vec2(U, V);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      NODE WORD DECODING
//------------------------------------------------------------------------------------------------------------------------

// The six bounds floats of a node, at either level and out of either buffer — the one thing the two encodings genuinely share. Two readers rather
// than one taking a buffer parameter, again because GLSL cannot pass a storage block as an argument.
void ReadArenaNodeBox(uint WordBase, out vec3 Minimum, out vec3 Maximum)
{
    Minimum = vec3(uintBitsToFloat(ArenaNodeWords[WordBase + 0u]),
                   uintBitsToFloat(ArenaNodeWords[WordBase + 1u]),
                   uintBitsToFloat(ArenaNodeWords[WordBase + 2u]));
    Maximum = vec3(uintBitsToFloat(ArenaNodeWords[WordBase + 3u]),
                   uintBitsToFloat(ArenaNodeWords[WordBase + 4u]),
                   uintBitsToFloat(ArenaNodeWords[WordBase + 5u]));
}

void ReadTreeNodeBox(uint WordBase, out vec3 Minimum, out vec3 Maximum)
{
    Minimum = vec3(uintBitsToFloat(TreeNodeWords[WordBase + 0u]),
                   uintBitsToFloat(TreeNodeWords[WordBase + 1u]),
                   uintBitsToFloat(TreeNodeWords[WordBase + 2u]));
    Maximum = vec3(uintBitsToFloat(TreeNodeWords[WordBase + 3u]),
                   uintBitsToFloat(TreeNodeWords[WordBase + 4u]),
                   uintBitsToFloat(TreeNodeWords[WordBase + 5u]));
}

// 🔴 THE LEAF TEST IS THE HIGH HALF OF WORD 7 AND NOTHING ELSE, AT BOTH LEVELS. The low half is the primitive count, which for a small leaf is a
//    small number — and an interior node stores its split axis (0, 1 or 2) in that same word, which is also a small number. Testing the whole word,
//    or the low half, therefore reads leaves as interior nodes and follows a primitive OFFSET as if it were a child pointer: a tree that is
//    structurally valid, traverses without faulting, and describes nothing. GeometryTreeBuild.h:69-72 states the same rule from the build side.
bool NodeIsLeaf(uint SeventhWord)
{
    return (SeventhWord >> 16u) == TreeLeafFlag;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       BOTTOM-LEVEL DESCENT
//------------------------------------------------------------------------------------------------------------------------

// Walk one mesh's SAH tree in that mesh's LOCAL space, keeping the closest triangle hit. Returns true when Hit was improved. LocalOrigin and
// LocalDirection must already be in the mesh's local frame; InverseLocalDirection is their reciprocal, computed once by the caller.
//
// 🔴 THE BOTTOM LEVEL'S INTERIOR WORD 6 IS A *RELATIVE* HOP TO THE RIGHT CHILD, AND THE LEFT CHILD IS IMPLICIT AT Slot + 1. This is the single
//    easiest thing to get wrong in this file. The CPU builder emits depth-first, so a subtree is laid down contiguously from its own slot: the left
//    child always sits immediately after its parent, and only the right child needs storing — as the DISTANCE from this node, not as an absolute
//    index (GeometryTreeBuild.cpp:706-709 writes exactly `RightSlot - Slot`). Reading it as absolute yields a node that exists, has a plausible box,
//    and belongs to a completely different part of the mesh. Nothing faults; the geometry is just quietly wrong, and only for rays that descend far
//    enough for the two interpretations to diverge. The TOP level stores BOTH children absolutely, which is why the two descents cannot share code.
//
// 📝 Ordered descent: the nearer child is entered first and the farther one is pushed. That is what makes the MaximumDistance shrink below effective
//    — once a hit is found, the deferred far subtrees are rejected on their box test rather than descended, which is the whole performance argument
//    for a BVH over a linear scan. An unordered descent is still CORRECT, so this cannot be validated by output alone; it is a cost property.
bool TraceBottomLevel(uint NodeOffset, uint PrimitiveOffset, uint IndexOffset, uint VertexOffset,
                      vec3 LocalOrigin, vec3 LocalDirection, vec3 InverseLocalDirection,
                      float NearLimit, inout float FarLimit,
                      out uint OutPrimitive, out vec2 OutBarycentric)
{
    OutPrimitive   = 0xFFFFFFFFu;
    OutBarycentric = vec2(0.0);

    bool Improved = false;

    uint Stack[BottomLevelStackSize];
    uint StackDepth = 0u;

    // Node indices are LOCAL to this mesh's tree; NodeOffset converts one to a word address in the shared arena. NodeOffset is in WORDS already
    // (GeometryArenaSubmission.h:19-22), so it is added AFTER the stride multiply, never multiplied by it.
    uint Current = 0u;   // the mesh's root

    while (true)
    {
        uint WordBase = NodeOffset + Current * TreeWordsPerNode;

        vec3 Minimum, Maximum;
        ReadArenaNodeBox(WordBase, Minimum, Maximum);

        float Entry;
        bool  Reached = IntersectRayBox(LocalOrigin, InverseLocalDirection, Minimum, Maximum, NearLimit, FarLimit, Entry);

        if (Reached)
        {
            uint SeventhWord = ArenaNodeWords[WordBase + 7u];

            if (NodeIsLeaf(SeventhWord))
            {
                // 📝 A leaf names a RANGE OF THE PRIMITIVE-ORDER TABLE, not a range of triangles. The build permutes primitives so each leaf's
                //    members are contiguous, and stores only (offset, count) into that permutation — so the triangle index must be fetched through
                //    ArenaPrimitives. Skipping that indirection gives boxes that are all correct and leaves that name entirely the wrong triangles
                //    (GeometryTreeBuild.h:102-105 says the same from the build side).
                uint FirstEntry = ArenaNodeWords[WordBase + 6u];
                uint EntryCount = SeventhWord & 0xFFFFu;

                for (uint Step = 0u; Step < EntryCount; ++Step)
                {
                    uint Triangle = ArenaPrimitives[PrimitiveOffset + FirstEntry + Step];

                    uint CornerBase = IndexOffset + Triangle * 3u;
                    uint IndexA     = VertexOffset + MeshIndices[CornerBase + 0u];
                    uint IndexB     = VertexOffset + MeshIndices[CornerBase + 1u];
                    uint IndexC     = VertexOffset + MeshIndices[CornerBase + 2u];

                    float Candidate;
                    vec2  Weights;
                    if (IntersectRayTriangle(LocalOrigin, LocalDirection,
                                             PositionForVertex(IndexA), PositionForVertex(IndexB), PositionForVertex(IndexC),
                                             NearLimit, FarLimit, Candidate, Weights))
                    {
                        // ⚠️ SHRINKING FarLimit IN PLACE IS WHAT MAKES THIS A CLOSEST-HIT SEARCH RATHER THAN AN ANY-HIT ONE, and it is why FarLimit
                        //    is inout. The caller's limit tightens as hits are found, here and across instances at the top level, so later boxes
                        //    fail cheaply. Dropping the write still returns a correct closest hit — the comparison above would catch it — but every
                        //    remaining node in the scene is then tested against the ORIGINAL limit.
                        FarLimit       = Candidate;
                        OutPrimitive   = Triangle;
                        OutBarycentric = Weights;
                        Improved       = true;
                    }
                }
            }
            else
            {
                // The left child is the very next node; word 6 is the hop from HERE to the right child. See the 🔴 above.
                uint LeftChild  = Current + 1u;
                uint RightChild = Current + ArenaNodeWords[WordBase + 6u];

                vec3 LeftMinimum, LeftMaximum, RightMinimum, RightMaximum;
                ReadArenaNodeBox(NodeOffset + LeftChild  * TreeWordsPerNode, LeftMinimum,  LeftMaximum);
                ReadArenaNodeBox(NodeOffset + RightChild * TreeWordsPerNode, RightMinimum, RightMaximum);

                float LeftEntry, RightEntry;
                bool  LeftReached  = IntersectRayBox(LocalOrigin, InverseLocalDirection, LeftMinimum,  LeftMaximum,  NearLimit, FarLimit, LeftEntry);
                bool  RightReached = IntersectRayBox(LocalOrigin, InverseLocalDirection, RightMinimum, RightMaximum, NearLimit, FarLimit, RightEntry);

                if (LeftReached && RightReached)
                {
                    uint NearChild = (LeftEntry <= RightEntry) ? LeftChild  : RightChild;
                    uint FarChild  = (LeftEntry <= RightEntry) ? RightChild : LeftChild;

                    // ⚠️ A FULL STACK DROPS THE FAR BRANCH AND KEEPS GOING. The builder's MaxDepth of 40 makes BottomLevelStackSize provably
                    //    sufficient, so this arm is unreachable for any tree this builder produces — it is here because "unreachable" depends on a
                    //    constant in another file, and the alternative to dropping the branch is writing past the array.
                    if (StackDepth < BottomLevelStackSize)
                    {
                        Stack[StackDepth] = FarChild;
                        ++StackDepth;
                    }

                    Current = NearChild;
                    continue;
                }
                else if (LeftReached)
                {
                    Current = LeftChild;
                    continue;
                }
                else if (RightReached)
                {
                    Current = RightChild;
                    continue;
                }
            }
        }

        // Miss, or a leaf just consumed: take the nearest deferred subtree, or finish.
        if (StackDepth == 0u)
            break;

        --StackDepth;
        Current = Stack[StackDepth];
    }

    return Improved;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         TOP-LEVEL DESCENT
//------------------------------------------------------------------------------------------------------------------------

// The reciprocal of a direction, with the axis-aligned case left as ±inf rather than guarded. IntersectRayBox is built to absorb the inf (see its
// 🔴), so clamping here would be the worse fix: a large-but-finite reciprocal shifts every slab distance on that axis by a tiny amount, which turns
// an exact miss into a hit at a grazing angle instead of removing the problem.
vec3 ReciprocalDirection(vec3 Direction)
{
    return vec3(1.0) / Direction;
}

// Walk the whole scene: descend the instance tree, and at each instance reached, descend that instance's mesh tree in local space. Returns the
// closest hit over everything.
//
// 🔴 THE LOCAL RAY DIRECTION IS DELIBERATELY *NOT* RENORMALISED, AND THIS IS THE SUBTLEST CORRECTNESS POINT IN THE FILE. InverseModel carries the
//    instance's inverse scale, so a world direction of unit length becomes a local direction of length 1/scale. Leaving it that way means the
//    bottom level's parametric t is measured in units of the ORIGINAL world direction — so the t it returns is directly comparable against
//    FarLimit, against hits on other instances, and against the caller's world-space maximum, with no per-instance rescaling anywhere.
//    Normalising it would make t a LOCAL distance: correct within one mesh, and wrong the moment two instances of different scale are compared.
//    The failure is not a crash or an obvious artefact — it is the wrong object winning the closest-hit test, but only between instances whose
//    scales differ, which a uniformly-scaled test scene will never reveal. Origins transform with w=1, directions with w=0.
//
// ⚠️ ONE SHARED FarLimit SPANS BOTH LEVELS AND EVERY INSTANCE. It enters as the caller's maximum, tightens on the first hit, and every subsequent
//    box test at both levels is judged against the tightened value. That single shrinking scalar is what keeps a two-level walk near the cost of a
//    one-level one; it works only because of the unnormalised-direction rule above.
TraceHit TraceTwoLevel(vec3 WorldOrigin, vec3 WorldDirection, float NearLimit, float MaximumDistance)
{
    TraceHit Hit = EmptyTraceHit(MaximumDistance);

    // A scene with no instances has no tree at all — not an empty tree, no nodes. Reading node 0 would read whatever the allocation last held.
    if (TraceInstanceCount == 0u)
        return Hit;

    vec3 InverseWorldDirection = ReciprocalDirection(WorldDirection);

    // 🔴 A ONE-INSTANCE SCENE HAS ZERO INTERNAL NODES AND ITS LONE LEAF IS NODE 0. The Karras build writes N-1 internal nodes, so at N=1 the leaf
    //    array starts at 0 and the tree IS that single leaf — there is no interior node to descend from. InstanceTreeBuild.comp returns early for
    //    this case and InstanceTreeRefit.comp skips the climb; a traversal that assumes an interior root reads a leaf's instance index as a child
    //    pointer. InternalCount is the same expression both of those use.
    uint InternalCount = TraceInstanceCount - 1u;

    float FarLimit = MaximumDistance;

    uint Stack[TopLevelStackSize];
    uint StackDepth = 0u;

    uint Current = 0u;   // the root: internal node 0, or the lone leaf when InstanceCount is 1

    while (true)
    {
        uint WordBase = Current * TreeWordsPerNode;

        vec3 Minimum, Maximum;
        ReadTreeNodeBox(WordBase, Minimum, Maximum);

        float Entry;
        bool  Reached = IntersectRayBox(WorldOrigin, InverseWorldDirection, Minimum, Maximum, NearLimit, FarLimit, Entry);

        if (Reached)
        {
            uint SeventhWord = TreeNodeWords[WordBase + 7u];

            if (NodeIsLeaf(SeventhWord))
            {
                // 📝 A TOP-LEVEL LEAF'S WORD 6 IS THE INSTANCE INDEX ITSELF — no indirection through a primitive table, because the top level's
                //    "primitives" ARE instances (InstanceTreeRefit.comp:192-196). Its count word is always 1 and is not read.
                uint InstanceIndex = TreeNodeWords[WordBase + 6u];

                if (InstanceIndex < TraceInstanceCount)
                {
                    uint MeshOrdinal = Instances[InstanceIndex].MeshOrdinal;

                    // 🔴 THE SAME DROP TEST EVERY EARLIER PASS APPLIED. The reduce, the Morton emit and the refit all ignore an instance whose
                    //    ordinal is out of range, and the refit leaves its leaf box inverted so it fails the box test above anyway. Repeating the
                    //    test here is not redundancy: it is what keeps a malformed ordinal from indexing the slice table out of bounds in the one
                    //    pass that actually dereferences it.
                    if (MeshOrdinal < TraceSliceCount)
                    {
                        mat4 InverseModel = Instances[InstanceIndex].InverseModel;

                        vec3 LocalOrigin    = (InverseModel * vec4(WorldOrigin,    1.0)).xyz;
                        vec3 LocalDirection = (InverseModel * vec4(WorldDirection, 0.0)).xyz;   // NOT normalised — see the 🔴 above

                        uint LocalPrimitive;
                        vec2 LocalBarycentric;

                        if (TraceBottomLevel(Slices[MeshOrdinal].NodeOffset,
                                             Slices[MeshOrdinal].PrimitiveOffset,
                                             Slices[MeshOrdinal].IndexOffset,
                                             Slices[MeshOrdinal].VertexOffset,
                                             LocalOrigin, LocalDirection, ReciprocalDirection(LocalDirection),
                                             NearLimit, FarLimit,
                                             LocalPrimitive, LocalBarycentric))
                        {
                            // FarLimit was tightened in place by the call above, so it IS the hit distance.
                            Hit.Distance       = FarLimit;
                            Hit.Barycentric    = LocalBarycentric;
                            Hit.InstanceIndex  = InstanceIndex;
                            Hit.PrimitiveIndex = LocalPrimitive;
                            Hit.HitCondition   = true;
                        }
                    }
                }
            }
            else
            {
                // 🔴 BOTH TOP-LEVEL CHILDREN ARE ABSOLUTE INDICES — the opposite of the bottom level's relative right-child hop. The Karras build
                //    writes nodes from independent lanes in no order at all, so nothing guarantees a child sits near its parent and a relative
                //    offset would be meaningless (InstanceTreeBuild.comp:168-175 states this from the build side). Applying the bottom level's
                //    rule here — or this one there — is the mistake the two 🔴 markers exist to prevent.
                uint LeftChild  = TreeNodeWords[WordBase + 6u];
                uint RightChild = TreeNodeWords[WordBase + 7u];

                vec3 LeftMinimum, LeftMaximum, RightMinimum, RightMaximum;
                ReadTreeNodeBox(LeftChild  * TreeWordsPerNode, LeftMinimum,  LeftMaximum);
                ReadTreeNodeBox(RightChild * TreeWordsPerNode, RightMinimum, RightMaximum);

                float LeftEntry, RightEntry;
                bool  LeftReached  = IntersectRayBox(WorldOrigin, InverseWorldDirection, LeftMinimum,  LeftMaximum,  NearLimit, FarLimit, LeftEntry);
                bool  RightReached = IntersectRayBox(WorldOrigin, InverseWorldDirection, RightMinimum, RightMaximum, NearLimit, FarLimit, RightEntry);

                if (LeftReached && RightReached)
                {
                    uint NearChild = (LeftEntry <= RightEntry) ? LeftChild  : RightChild;
                    uint FarChild  = (LeftEntry <= RightEntry) ? RightChild : LeftChild;

                    // ⚠️ UNLIKE THE BOTTOM LEVEL, THIS OVERFLOW IS REACHABLE AND IS REPORTED RATHER THAN SWALLOWED. A Morton tree's depth is
                    //    data-dependent: instances clustered tightly enough to share codes degrade it toward a list of depth N. Dropping the far
                    //    branch keeps the walk finite and the result merely INCOMPLETE, but an incomplete closest-hit search is a wrong answer that
                    //    looks like a valid one, so the flag rides out on the hit for a caller — and the gate — to see.
                    if (StackDepth < TopLevelStackSize)
                    {
                        Stack[StackDepth] = FarChild;
                        ++StackDepth;
                    }
                    else
                    {
                        Hit.OverflowCondition = true;
                    }

                    Current = NearChild;
                    continue;
                }
                else if (LeftReached)
                {
                    Current = LeftChild;
                    continue;
                }
                else if (RightReached)
                {
                    Current = RightChild;
                    continue;
                }
            }
        }

        if (StackDepth == 0u)
            break;

        --StackDepth;
        Current = Stack[StackDepth];
    }

    return Hit;
}

#endif
