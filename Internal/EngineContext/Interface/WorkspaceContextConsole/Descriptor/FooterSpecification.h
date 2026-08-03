/*==============================================================================================================================================
                                                         FOOTERSPECIFICATION.H
==============================================================================================================================================*/
// 🧩 What the options slide's pinned footer says: its two button captions, and the note beside them. Authored per workspace because the two
//    workspaces that carry a footer mean genuinely different things by it — modelling's pair is Cancel/Apply over the open action's name, paint's is
//    Reset/Select over a LIVE "<n> live parameters · <n> groups" tally — and neither is a stand-in for the other.
//
//    🔴 The captions are strings rather than an enum of two known pairs, because the footer is the one place a workspace states what pressing the
//       button MEANS. "Cancel" that discards and "Reset" that re-seeds the values in place are different acts, and the console cannot tell them apart
//       — so it reports both buttons distinctly (ConsoleResult::RevertRequested / CommitRequested) and lets the workspace decide. Naming them here
//       keeps the console from growing a paint-shaped special case in its draw path.
//
//    📝 Every field is optional. A workspace that says nothing gets Cancel/Apply over the open action's label, which is what modelling and
//       construction author — so adding this cost neither of them a line.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_DESCRIPTOR_FOOTERSPECIFICATION_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_DESCRIPTOR_FOOTERSPECIFICATION_H

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct FooterSpecification
{
    // 📝 The right, emphasised button — the one that commits. Null keeps "Apply".
    const char* CommitCaption = nullptr;   // [-] - "Apply" / "Select"
    // 📝 The left, ghost button. Null keeps "Cancel".
    const char* RevertCaption = nullptr;   // [-] - "Cancel" / "Reset"

    // 🔴 The note on the left of the footer, refilled by the workspace EVERY frame when it carries live numbers. Paint's reads "7 live parameters · 2
    //    groups" and both figures move as the reader edits — a footer that only knew the open action's label could not say it. Null falls back to that
    //    label, which is what a workspace with nothing live to report wants.
    const char* NoteText      = nullptr;   // [-] - the footer's left-hand note; null draws the open action's label
    // 📝 Accents the DIGITS inside NoteText rather than the whole run, matching the prototype's `.spend b{ color:var(--accent) }`. A flag rather than a
    //    pre-split string so a workspace can keep composing its note with one snprintf.
    bool        AccentFigures = false;     // [-] - draw digit runs in the accent ink
};

} // namespace Frontier

#endif
