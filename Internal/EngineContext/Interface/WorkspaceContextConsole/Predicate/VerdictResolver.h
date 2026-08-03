/*==============================================================================================================================================
                                                           VERDICTRESOLVER.H
==============================================================================================================================================*/
// 🧩 The single extension point of the whole console. Every workspace supplies ONE of these — a pure function that, given an action descriptor
//    and the workspace's own live document, returns that action's ActionVerdict. The console's rail (to drop empty clusters), action grid (to
//    grey or omit tiles) and header text (to state a shortfall) all route their "what is this action's standing?" question through this hook,
//    so the console never learns whether the gate behind it counts selection strata, document preconditions, or reveal conditions.
//
//    🔴 It is a FUNCTION POINTER plus a borrowed Context, deliberately not std::function and not an abstract interface. A raw pointer heap-
//       allocates nothing and stays trivially copyable, matching the constexpr-table idiom the descriptors are authored in; std::function would
//       type-erase onto the heap on the per-frame path, and a vtable interface would drag the Handler/Manager object register the naming skill
//       bans. The workspace's live document rides Context as an opaque pointer the resolver casts back to its own struct — modelling casts it to
//       its selection strata, construction to its ConstructionDocument, paint to its seeded-value array. The console passes Context through
//       untouched and never dereferences it.
//
//    🔴 The resolver MUST be pure and side-effect-free: it is called many times per frame (once per visible tile, plus the rail's per-cluster
//       tallies), sometimes on the same action, and the console assumes two calls with the same Descriptor and Context agree. A resolver that
//       mutated Context, or read a clock, would make a tile's standing flicker between the tally pass and the draw pass.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_PREDICATE_VERDICTRESOLVER_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_PREDICATE_VERDICTRESOLVER_H

#include "ActionVerdict.h"

namespace Frontier
{

struct ActionDescriptor;

//------------------------------------------------------------------------------------------------------------------------
//                                                             TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 The gate hook. Action is the descriptor being judged (borrowed, from the workspace's constexpr action table); Context is the workspace's
//    own live document as an opaque pointer, cast back inside the resolver. Returns the reduced ActionVerdict the console draws.
using VerdictResolver = ActionVerdict (*)(const ActionDescriptor& Action, const void* Context);


// 📝 A resolver paired with the context it reads, passed together so a console call site carries exactly one argument for "how to judge an action
//    here" rather than two that could drift apart. The workspace fills both once per frame and hands the pair down; the console copies it freely
//    (both members are borrowed).
struct VerdictBinding
{
    VerdictResolver Resolve = nullptr;   // [-] - the workspace's gate function; null means every action resolves Live (an unguarded console)
    const void*     Context = nullptr;   // [-] - the workspace's live document, opaque to the console
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Judge one action through a binding. Centralises the null-resolver rule — a binding with no Resolve treats every action as Live — so no call
// site has to remember it, and an unguarded console (a workspace with no gate) draws every tile normally rather than omitting them all.
[[nodiscard]] ActionVerdict ResolveActionVerdict(const VerdictBinding& Binding, const ActionDescriptor& Action);

} // namespace Frontier

#endif
