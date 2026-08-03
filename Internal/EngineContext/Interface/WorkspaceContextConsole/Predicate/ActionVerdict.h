/*==============================================================================================================================================
                                                            ACTIONVERDICT.H
==============================================================================================================================================*/
// 🧩 The one answer every workspace's gate model collapses to. The three card workspaces disagree ONLY on how an action's standing is decided —
//    modelling reads a 3-state selection-stratum test, construction a 5-state document gate, texture-paint an 8-condition reveal — but every one
//    of those decisions reduces to the SAME question the rail, the action grid and the header text all ask each frame: "for the workspace as it
//    is right now, is this action shown, shown-but-blocked, or live — and if blocked, what is short and what would it yield?" That reduced answer
//    is ActionVerdict. It carries no workspace vocabulary, so the console draws it without knowing which gate produced it.
//
//    🔴 Standing is a THREE-way distinction and the three are not interchangeable. Omitted drops the action from the grid entirely ("not for this
//       workspace state"); Gated draws it greyed with Shortfall replacing its label under the pointer ("for this state, but not yet"); Live draws
//       it normally. Collapsing Omitted and Gated into one "unavailable" loses the difference between an action that does not belong and one that
//       is one step away — which is the whole reason a gated tile still occupies its cell instead of vanishing.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_PREDICATE_ACTIONVERDICT_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_PREDICATE_ACTIONVERDICT_H

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                             TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 An action's standing against the workspace as it is right now. This is the shared reduction of modelling's ToolAvailability,
//    construction's GateResult and paint's reveal conditions — three gate models, one drawable answer.
enum class ActionStanding
{
    Omitted,   // not for this workspace state — dropped from the grid entirely
    Gated,     // for this state, but a precondition is short — drawn greyed, states its Shortfall under the pointer
    Live,      // ready — drawn normally, reports its activation
};


// 📝 The full answer a resolver hands back for one action. Standing is the only field the console MUST read; Shortfall and Yield are
//    the prose a gated tile explains itself with, and are meaningful only when Standing is Gated (Shortfall) or when a workspace wants
//    to preview an action's result (Yield). Both are borrowed pointers into the workspace's own tables — the resolver does not allocate,
//    so a verdict is trivially copyable and costs nothing to return by value on the per-frame path.
//    🔴 Shortfall is null for a Live action and non-null for a Gated one; that pairing is what the header text keys on, NOT Standing
//       alone, because construction's five gate reasons all map to Gated yet carry five different Shortfall strings.
struct ActionVerdict
{
    ActionStanding Standing  = ActionStanding::Omitted;   // [-] - shown / shown-but-blocked / live
    const char*    Shortfall = nullptr;                   // [-] - what is short; null unless Standing == Gated
    const char*    Yield     = nullptr;                   // [-] - what a run would produce (construction's "-> shell"); null when none
};

} // namespace Frontier

#endif
