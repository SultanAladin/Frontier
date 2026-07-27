/*==============================================================================================================================================
                                                             DIAGNOSTICARCHIVE.H
==============================================================================================================================================*/
// 🧩 The append-only diagnostic log for the renderer. One BroadcastDiagnostic entry point classified by severity, plus terse ISSUE_* macros for
//    call sites. Output is routed to the console (and stderr for faults) with a severity tag and source location. Build profile decides what
//    survives compilation: the development profile keeps every classification; the shipping profile compiles Trace and Notice out entirely so
//    release builds carry no chatter. This header also resolves the profile — if neither macro is defined, development is assumed (validation on).

#pragma once
#ifndef FRONTIER_GRAPHICS_RENDEREXTENSION_DIAGNOSTICS_DIAGNOSTICARCHIVE_H
#define FRONTIER_GRAPHICS_RENDEREXTENSION_DIAGNOSTICS_DIAGNOSTICARCHIVE_H


//------------------------------------------------------------------------------------------------------------------------
//                                                         BUILD PROFILE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Exactly one profile is active. Default to development when the build system names neither, so an un-flagged compile
//    stays safe-and-verbose rather than silently shipping. Development additionally implies the Vulkan validation layer.
#if !defined(FRONTIER_DEVELOPMENT_PROFILE) && !defined(FRONTIER_SHIPPING_PROFILE)
    #define FRONTIER_DEVELOPMENT_PROFILE
#endif

#if defined(FRONTIER_DEVELOPMENT_PROFILE) && defined(FRONTIER_SHIPPING_PROFILE)
    #error "Define at most one of FRONTIER_DEVELOPMENT_PROFILE / FRONTIER_SHIPPING_PROFILE."
#endif

#if defined(FRONTIER_DEVELOPMENT_PROFILE) && !defined(FRONTIER_VULKAN_VALIDATION)
    #define FRONTIER_VULKAN_VALIDATION
#endif

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Severity of a diagnostic entry. Trace is fine-grained flow; Notice is an ordinary milestone; Caution is a recoverable
//    concern; Fault is a genuine failure routed to stderr. Ordered low-to-high so a minimum-severity gate is a simple compare.
enum class DiagnosticClassification
{
    Trace   = 0,   // [-] - Fine-grained flow, compiled out in the shipping profile
    Notice  = 1,   // [-] - Ordinary milestone, compiled out in the shipping profile
    Caution = 2,   // [-] - Recoverable concern; survives every profile
    Fault   = 3    // [-] - Genuine failure; survives every profile, routed to stderr
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Append one classified diagnostic. SourceTag names the emitting subsystem (e.g. "render-extension"); MessageText is the body.
// SourceFile / SourceLine locate the call and are supplied by the ISSUE_* macros. Faults flush to stderr; the rest to stdout.
void BroadcastDiagnostic(DiagnosticClassification Classification,
                         const char*              SourceTag,
                         const char*              SourceFile,
                         int                      SourceLine,
                         const char*              MessageText,
                         ...);

} // namespace Frontier

//------------------------------------------------------------------------------------------------------------------------
//                                                         ISSUE MACROS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Terse call sites. Trace and Notice evaporate to nothing in the shipping profile (no argument evaluation, no code). Caution
//    and Fault always compile. Each forwards __FILE__ / __LINE__ so the archive can locate the emission. The macros name
//    Frontier::DiagnosticClassification explicitly so a call site outside the namespace still resolves.
#ifdef FRONTIER_SHIPPING_PROFILE
    #define ISSUE_TRACE(Tag, ...)   ((void)0)
    #define ISSUE_NOTICE(Tag, ...)  ((void)0)
#else
    #define ISSUE_TRACE(Tag, ...)   ::Frontier::BroadcastDiagnostic(::Frontier::DiagnosticClassification::Trace,  (Tag), __FILE__, __LINE__, __VA_ARGS__)
    #define ISSUE_NOTICE(Tag, ...)  ::Frontier::BroadcastDiagnostic(::Frontier::DiagnosticClassification::Notice, (Tag), __FILE__, __LINE__, __VA_ARGS__)
#endif

#define ISSUE_CAUTION(Tag, ...)     ::Frontier::BroadcastDiagnostic(::Frontier::DiagnosticClassification::Caution, (Tag), __FILE__, __LINE__, __VA_ARGS__)
#define ISSUE_FAULT(Tag, ...)       ::Frontier::BroadcastDiagnostic(::Frontier::DiagnosticClassification::Fault,   (Tag), __FILE__, __LINE__, __VA_ARGS__)

#endif
