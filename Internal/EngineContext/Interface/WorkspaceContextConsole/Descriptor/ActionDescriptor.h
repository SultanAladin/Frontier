/*==============================================================================================================================================
                                                            ACTIONDESCRIPTOR.H
==============================================================================================================================================*/
// 🧩 One action tile in the console's grid — a modelling tool, a construction operation, a paint instrument. Ported from ToolCard's
//    ToolTileDescriptor, with ONE deliberate subtraction: the StrataMask and inline Shortfall that baked modelling's 3-state gate into the tile
//    are GONE. An action's standing is no longer a property of the descriptor; it is answered per frame by the workspace's VerdictResolver, which
//    reads whatever gate model the workspace actually has. What stays here is only what is intrinsic to the action regardless of gate: its label,
//    its glyph, its accelerator, its parameter rows, and an opaque Gate token the resolver reads.
//
//    🔴 GateToken is the descriptor's ONLY concession to the gate, and it is deliberately opaque: modelling packs its selection-stratum mask into
//       it, construction indexes its operation table with it, paint keys its reveal condition off it. The console passes GateToken to the resolver
//       untouched and never interprets it — the same way Context is opaque going the other direction. This keeps one descriptor table serving three
//       gate models without the console learning any of them.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_DESCRIPTOR_ACTIONDESCRIPTOR_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_DESCRIPTOR_ACTIONDESCRIPTOR_H

namespace Frontier
{

struct ParameterDescriptor;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct ActionDescriptor
{
    const char*                Label;           // [-]  - tile caption
    const char*                Keystroke;       // [-]  - accelerator in the tile corner; null for none
    const char*                GlyphName;       // [-]  - tile artwork, prototype identifier
    // 🔴 Opaque to the console; the workspace's VerdictResolver casts/reads it to decide standing. Modelling packs a stratum mask here,
    //    construction an operation index, paint a reveal-condition code. See the header note.
    unsigned int               GateToken;       // [-]  - the resolver's key for this action; console never interprets it
    const ParameterDescriptor* Parameters;      // [-]  - options-pane rows (borrowed)
    int                        ParameterCount;  // [idx]- number of rows
};

} // namespace Frontier

#endif
