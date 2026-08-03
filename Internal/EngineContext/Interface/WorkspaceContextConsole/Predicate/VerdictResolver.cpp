/*==============================================================================================================================================
                                                           VERDICTRESOLVER.CPP
==============================================================================================================================================*/
// 🧩 The one non-obvious rule of the gate hook: a binding with no resolver treats every action as Live. Centralised here so no call site — the
//    rail's per-cluster tallies, the action grid's per-tile draw, the header text — has to remember it, and an unguarded console (a workspace with
//    no gate at all) draws every tile normally rather than omitting them all.

#include "VerdictResolver.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

ActionVerdict ResolveActionVerdict(const VerdictBinding& Binding, const ActionDescriptor& Action)
{
    // 📝 No resolver → everything is Live. This is the unguarded-console path, and it is the ONLY place the null-resolver default lives.
    if (Binding.Resolve == nullptr)
    {
        return ActionVerdict{ ActionStanding::Live, nullptr, nullptr };
    }
    return Binding.Resolve(Action, Binding.Context);
}

} // namespace Frontier
