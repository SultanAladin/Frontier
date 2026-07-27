//==========================================================================================================================================
//                                                            DraughtBoolean.cpp
//==========================================================================================================================================
// 🧩 2D boolean regions for the draughting workspace. The clip runs on Clipper2 (a Vatti sweep-line on scaled integer coordinates): every
//    operand is converted to a Clipper2 double path, the base becomes the subject and all tools the clips, and one Execute resolves the
//    whole selection at once into a nested PolyTree of outer contours with their hole children. This is robust against the collinear /
//    exact-touch degeneracies that the earlier Greiner-Hormann port needed rotate-retries and containment fallbacks to survive, and it
//    carries holes + multiple operands natively (no pairwise fold, no re-fuse pass). AppendBooleanResult flattens the selection, solves,
//    and emits one analytic Profile per surviving outer loop (its contained holes carried), hiding the operands (recoverable, not deleted).

#include "DraughtBoolean.h"

#include <algorithm>   // 📝 std::reverse — orient the walked contours
#include <cmath>       // 📝 std::fabs — sliver-area cutoff
#include <cstdio>      // 📝 std::snprintf — seed the Profile title
#include <cstdint>

#include "clipper2/clipper.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        GEOMETRY HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr float  AreaEps        = 1e-6f;   // [mm²] - drop loops below this magnitude as slivers
    constexpr double BooleanPrecision = 4;     // [-]   - Clipper2 decimal precision: 1e-4 mm (0.1 µm) fixed-point grid

    // 📝 Twice-signed shoelace area; positive when the loop winds counter-clockwise in world mm.
    float LoopTwiceArea(const std::vector<ImVec2>& Loop)
    {
        const size_t Count = Loop.size();
        if (Count < 3)
            return 0.0f;
        float Sum = 0.0f;
        for (size_t Index = 0; Index < Count; ++Index)
        {
            const ImVec2& Current = Loop[Index];
            const ImVec2& Next     = Loop[(Index + 1) % Count];
            Sum += Current.x * Next.y - Next.x * Current.y;
        }
        return Sum;
    }

    // 📝 Even-odd raycast containment: is (X,Y) inside the loop Points?
    bool PointInsideLoop(float X, float Y, const std::vector<ImVec2>& Points)
    {
        bool Inside = false;
        const size_t Count = Points.size();
        for (size_t Index = 0, Previous = Count - 1; Index < Count; Previous = Index++)
        {
            const float Xi = Points[Index].x,    Yi = Points[Index].y;
            const float Xj = Points[Previous].x, Yj = Points[Previous].y;
            if (((Yi > Y) != (Yj > Y)) && (X < (Xj - Xi) * (Y - Yi) / (Yj - Yi) + Xi))
                Inside = !Inside;
        }
        return Inside;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                     CLIPPER2 CONVERSION
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Convert one world-mm loop into a Clipper2 double path (no repeated closing point; Clipper2 treats every path as closed).
    Clipper2Lib::PathD ConvertLoopToPath(const std::vector<ImVec2>& Loop)
    {
        Clipper2Lib::PathD Path;
        Path.reserve(Loop.size());
        for (const ImVec2& Point : Loop)
            Path.emplace_back((double)Point.x, (double)Point.y);
        return Path;
    }

    // 📝 Convert a set of world-mm loops into a Clipper2 double path-set.
    Clipper2Lib::PathsD ConvertLoopsToPaths(const std::vector<std::vector<ImVec2>>& Loops)
    {
        Clipper2Lib::PathsD Paths;
        Paths.reserve(Loops.size());
        for (const auto& Loop : Loops)
            if (Loop.size() >= 3)
                Paths.push_back(ConvertLoopToPath(Loop));
        return Paths;
    }

    // 📝 Convert one Clipper2 double path back to a world-mm loop, forcing the requested winding (outer CCW, hole CW). Clipper2's
    //    positive-Y-down convention matches our mm shoelace, so a positive signed area is CCW here too.
    std::vector<ImVec2> ConvertPathToLoop(const Clipper2Lib::PathD& Path, bool CounterClockwiseEnabled)
    {
        std::vector<ImVec2> Loop;
        Loop.reserve(Path.size());
        for (const Clipper2Lib::PointD& Point : Path)
            Loop.emplace_back((float)Point.x, (float)Point.y);
        if (Loop.size() >= 3)
        {
            const bool CurrentlyCounterClockwise = LoopTwiceArea(Loop) >= 0.0f;
            if (CurrentlyCounterClockwise != CounterClockwiseEnabled)
                std::reverse(Loop.begin(), Loop.end());
        }
        return Loop;
    }

    // 📝 Walk a Clipper2 PolyTree node into the flat outer(CCW)/hole(CW) loop-set the workspace stores. A non-hole node is an outer
    //    contour; each of its children is a hole; a hole's own children are fresh outers (an island inside a hole) — recurse so
    //    nested regions never drop. Slivers below AreaEps are discarded.
    void AccumulatePolyTree(const Clipper2Lib::PolyPathD& Node, std::vector<std::vector<ImVec2>>& Out)
    {
        for (size_t Index = 0; Index < Node.Count(); ++Index)
        {
            const Clipper2Lib::PolyPathD& Child = *Node.Child(Index);
            const bool HoleEnabled = Child.IsHole();
            std::vector<ImVec2> Loop = ConvertPathToLoop(Child.Polygon(), !HoleEnabled);
            if (Loop.size() >= 3 && std::fabs(LoopTwiceArea(Loop) * 0.5f) >= AreaEps)
                Out.push_back(std::move(Loop));
            AccumulatePolyTree(Child, Out);   // holes carry outer children; outers carry hole children
        }
    }

    Clipper2Lib::ClipType ResolveClipType(BooleanCategory Op)
    {
        switch (Op)
        {
            case BooleanCategory::Union:     return Clipper2Lib::ClipType::Union;
            case BooleanCategory::Subtract:  return Clipper2Lib::ClipType::Difference;
            case BooleanCategory::Intersect: return Clipper2Lib::ClipType::Intersection;
        }
        return Clipper2Lib::ClipType::Union;
    }

    // 📝 Split a normalized loop set into its outer loops (CCW, positive area) and hole loops (CW, negative area).
    void PartitionOutersAndHoles(const std::vector<std::vector<ImVec2>>& Loops,
                                 std::vector<std::vector<ImVec2>>&        Outers,
                                 std::vector<std::vector<ImVec2>>&        Holes)
    {
        for (const auto& Loop : Loops)
        {
            if (LoopTwiceArea(Loop) >= 0.0f)
                Outers.push_back(Loop);
            else
                Holes.push_back(Loop);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        LOOP NORMALIZATION
//------------------------------------------------------------------------------------------------------------------------

void NormalizeBooleanLoops(std::vector<std::vector<ImVec2>>& Loops)
{
    // Clipper2 already classifies outer/hole nesting; this pass only drops slivers and re-asserts the outer-CCW / hole-CW winding
    //    by containment depth (even ⇒ outer, odd ⇒ hole), so a caller can hand in raw loops and still get the workspace convention.
    std::vector<std::vector<ImVec2>> Kept;
    Kept.reserve(Loops.size());
    for (auto& Loop : Loops)
        if (Loop.size() >= 3 && std::fabs(LoopTwiceArea(Loop) * 0.5f) >= AreaEps)
            Kept.push_back(std::move(Loop));

    if (Kept.size() < 2)
    {
        if (Kept.size() == 1 && LoopTwiceArea(Kept[0]) < 0.0f)
            std::reverse(Kept[0].begin(), Kept[0].end());
        Loops = std::move(Kept);
        return;
    }

    // Depth by containment: count how many other loops contain this one. Even depth ⇒ outer (CCW), odd ⇒ hole (CW).
    std::vector<std::vector<ImVec2>> Out;
    Out.reserve(Kept.size());
    for (size_t Index = 0; Index < Kept.size(); ++Index)
    {
        std::vector<ImVec2> Loop = Kept[Index];
        int Depth = 0;
        for (size_t Other = 0; Other < Kept.size(); ++Other)
        {
            if (Other == Index)
                continue;
            if (PointInsideLoop(Loop.front().x, Loop.front().y, Kept[Other]))
                ++Depth;
        }
        const bool OuterEnabled = (Depth % 2) == 0;
        const bool CurrentlyCounterClockwise = LoopTwiceArea(Loop) >= 0.0f;
        if (CurrentlyCounterClockwise != OuterEnabled)
            std::reverse(Loop.begin(), Loop.end());
        Out.push_back(std::move(Loop));
    }
    Loops = std::move(Out);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CLIPPER2-BACKED SOLVES
//------------------------------------------------------------------------------------------------------------------------

std::vector<std::vector<ImVec2>> SolveRegionBoolean(const std::vector<std::vector<ImVec2>>& OperandLoops,
                                                    BooleanCategory                         Op)
{
    if (OperandLoops.empty())
        return {};
    if (OperandLoops.size() == 1)
    {
        std::vector<std::vector<ImVec2>> Single = { OperandLoops[0] };
        NormalizeBooleanLoops(Single);
        return Single;
    }

    // The base (operand 0) is the subject; every tool is a clip. One Execute resolves the whole selection: Union merges all, Subtract
    //    carves every tool out of the base (a tool fully inside becomes a hole child), Intersect keeps only the shared region. Clipper2's
    //    NonZero rule matches the workspace's filled-interior intent. The PolyTree gives outer/hole nesting directly — no fold, no re-fuse.
    std::vector<std::vector<ImVec2>> BaseLoops{ OperandLoops.front() };
    std::vector<std::vector<ImVec2>> ToolLoops(OperandLoops.begin() + 1, OperandLoops.end());
    Clipper2Lib::PathsD Subjects = ConvertLoopsToPaths(BaseLoops);
    Clipper2Lib::PathsD Clips    = ConvertLoopsToPaths(ToolLoops);

    Clipper2Lib::PolyTreeD Tree;
    Clipper2Lib::BooleanOp(ResolveClipType(Op), Clipper2Lib::FillRule::NonZero, Subjects, Clips, Tree, (int)BooleanPrecision);

    std::vector<std::vector<ImVec2>> Result;
    AccumulatePolyTree(Tree, Result);
    return Result;
}

std::vector<std::vector<ImVec2>> SolveLoopBoolean(const std::vector<ImVec2>& Subject,
                                                  const std::vector<ImVec2>& Clip,
                                                  BooleanCategory            Op)
{
    // A two-operand convenience over the same Clipper2 path (base = Subject, single tool = Clip).
    return SolveRegionBoolean({ Subject, Clip }, Op);
}

std::vector<std::vector<ImVec2>> SolveRegionBooleanWithHoles(const std::vector<std::vector<std::vector<ImVec2>>>& OperandRegions,
                                                             BooleanCategory                                      Op)
{
    if (OperandRegions.empty())
        return {};

    // A single region is returned as-is (re-wound), matching SolveRegionBoolean's single-operand path.
    if (OperandRegions.size() == 1)
    {
        std::vector<std::vector<ImVec2>> Single = OperandRegions.front();
        NormalizeBooleanLoops(Single);
        return Single;
    }

    // Build the subject from EVERY loop of operand 0 (its outer + any holes it already carries) and the clips from EVERY loop of the
    //    tool operands. Each loop is wound to the workspace convention first (outer CCW / hole CW) so Clipper2's NonZero rule reads an
    //    existing hole as a void that survives the clip — this is what lets a Profile-with-holes chain into a further boolean without
    //    dropping its earlier voids. A tool that itself carries holes feeds those holes too (a ring tool removes only its solid annulus).
    auto AppendRegionPaths = [](const std::vector<std::vector<ImVec2>>& Region, Clipper2Lib::PathsD& Out)
    {
        for (const std::vector<ImVec2>& Loop : Region)
        {
            if (Loop.size() < 3)
                continue;
            Out.push_back(ConvertLoopToPath(Loop));
        }
    };

    Clipper2Lib::PathsD Subjects;
    Clipper2Lib::PathsD Clips;
    AppendRegionPaths(OperandRegions.front(), Subjects);
    for (size_t Index = 1; Index < OperandRegions.size(); ++Index)
        AppendRegionPaths(OperandRegions[Index], Clips);

    Clipper2Lib::PolyTreeD Tree;
    Clipper2Lib::BooleanOp(ResolveClipType(Op), Clipper2Lib::FillRule::NonZero, Subjects, Clips, Tree, (int)BooleanPrecision);

    std::vector<std::vector<ImVec2>> Result;
    AccumulatePolyTree(Tree, Result);
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        ORCHESTRATION
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    const char* BooleanTitle(BooleanCategory Op)
    {
        switch (Op)
        {
            case BooleanCategory::Union:     return "Union Profile";
            case BooleanCategory::Subtract:  return "Subtract Profile";
            case BooleanCategory::Intersect: return "Intersect Profile";
        }
        return "Profile";
    }

    void RaiseNotice(DraughtShapeStore& Store, const char* Message)
    {
        std::snprintf(Store.Notice, sizeof(Store.Notice), "%s", Message);
        Store.NoticeTimer = 4.0f;   // [s] - the view decays this each frame
    }
}

BooleanOutcome AppendBooleanResult(DraughtShapeStore& Store, BooleanCategory Op)
{
    if (Store.SelectionSet.size() < 2)
    {
        RaiseNotice(Store, "Boolean needs two shapes");
        return BooleanOutcome::TooFewOperands;
    }

    // Validate every operand is present + closed, and cache its flattened region (outer CCW + any holes it already carries, CW) BEFORE
    //    mutating the store. Carrying each operand's existing HoleLoops into the solve is what makes chained subtracts accumulate holes:
    //    a Profile cut once already stores its first void, so the second subtract must see that void or it re-emerges as solid.
    std::vector<std::vector<std::vector<ImVec2>>> OperandRegions;
    OperandRegions.reserve(Store.SelectionSet.size());
    uint32_t BaseIdentifier = Store.SelectionSet.front();
    uint32_t BaseTint = 0;
    uint32_t BaseFolder = 0;
    for (uint32_t Identifier : Store.SelectionSet)
    {
        DraughtShape* Shape = ResolveDraughtShape(Store, Identifier);
        if (!Shape || !Shape->ClosedEnabled)
        {
            RaiseNotice(Store, "Boolean needs closed shapes");
            return BooleanOutcome::NeedsClosedShapes;
        }
        // Flatten each operand DENSELY before the clip. Clipper2 is a straight-segment sweep-line — it cannot carry an analytic arc
        //    through a boolean, so a curved operand must enter as a polyline, and the surviving result stores THAT polyline (the solve
        //    runs once, not per-frame, so it cannot refine on later zoom). Sampling at the fixed per-category default (~64 for a circle)
        //    left the result visibly faceted when zoomed; a high fixed budget makes each operand fine enough that the clipped region reads
        //    as one smooth curve across normal working zoom. This is the standard CAD pipeline: tessellate fine, clip, keep the polyline.
        constexpr int BooleanOperandBudget = 256;   // [-] - segments per curved operand fed to the clip (circle → 256-gon)
        std::vector<ImVec2> Loop;
        EvaluateFilledPolygon(*Shape, Loop, BooleanOperandBudget);
        if (Loop.size() < 3)
        {
            RaiseNotice(Store, "Boolean needs closed shapes");
            return BooleanOutcome::NeedsClosedShapes;
        }
        if (Identifier == BaseIdentifier)
        {
            BaseTint   = Shape->TintIndex;
            BaseFolder = Shape->FolderIdentifier;
        }

        // This operand's region: its outer loop, then any holes it already carries (a Profile from an earlier boolean). Enforce the
        //    outer CCW + each hole CW so Clipper2's NonZero rule reads the holes as voids — the winding stored on HoleLoops is already CW
        //    (this file produced it), but re-assert it so a hand-authored or re-imported hole can never flip the interior fill.
        std::vector<std::vector<ImVec2>> Region;
        Region.reserve(1 + Shape->HoleLoops.size());
        Region.push_back(std::move(Loop));
        for (const std::vector<ImVec2>& Hole : Shape->HoleLoops)
        {
            if (Hole.size() < 3)
                continue;
            std::vector<ImVec2> HoleLoop = Hole;
            if (LoopTwiceArea(HoleLoop) >= 0.0f)   // a hole must wind CW (negative area) so NonZero punches it
                std::reverse(HoleLoop.begin(), HoleLoop.end());
            Region.push_back(std::move(HoleLoop));
        }
        OperandRegions.push_back(std::move(Region));
    }

    if (OperandRegions.size() < 2)
    {
        RaiseNotice(Store, "Boolean needs closed shapes");
        return BooleanOutcome::NeedsClosedShapes;
    }

    // Solve over the operand REGIONS (outer + carried holes), then classify the result into outer loops + their contained holes.
    std::vector<std::vector<ImVec2>> Solved = SolveRegionBooleanWithHoles(OperandRegions, Op);
    std::vector<std::vector<ImVec2>> Outers, Holes;
    PartitionOutersAndHoles(Solved, Outers, Holes);
    if (Outers.empty())
    {
        RaiseNotice(Store, "Boolean produced no region");
        return BooleanOutcome::EmptyResult;
    }

    // Append one Profile per surviving outer loop (Subtract can split the base; Union can leave disjoint regions — never drop geometry).
    uint32_t LastIdentifier = 0;
    for (const auto& Outer : Outers)
    {
        DraughtShape Fresh;
        Fresh.Identifier      = Store.NextIdentifier++;
        Fresh.Category        = DraughtShapeCategory::Profile;
        Fresh.Points          = Outer;
        Fresh.ClosedEnabled   = true;
        Fresh.FillEnabled     = true;
        Fresh.Displayed       = true;
        Fresh.TintIndex       = BaseTint;
        Fresh.FolderIdentifier = BaseFolder;
        for (const auto& Hole : Holes)
        {
            if (PointInsideLoop(Hole.front().x, Hole.front().y, Outer))
                Fresh.HoleLoops.push_back(Hole);
        }
        std::snprintf(Fresh.Title, sizeof(Fresh.Title), "%s %u", BooleanTitle(Op), Fresh.Identifier);

        Store.Shapes.push_back(Fresh);
        LastIdentifier = Fresh.Identifier;

        char LogLabel[64];
        std::snprintf(LogLabel, sizeof(LogLabel), "Added %s", Fresh.Title);
        AppendDraughtEdit(Store, Fresh.Identifier, LogLabel, "spline");
    }

    // Hide the operands (recoverable via the outliner — not deleted).
    for (uint32_t Identifier : Store.SelectionSet)
    {
        if (DraughtShape* Shape = ResolveDraughtShape(Store, Identifier))
            Shape->Displayed = false;
    }

    Store.Selected = LastIdentifier;
    Store.SelectionSet.clear();
    Store.Hovered = 0;
    return BooleanOutcome::Committed;
}

} // namespace Frontier
