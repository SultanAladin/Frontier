/*==============================================================================================================================================
                                                          PAINTICONPACK.CPP
==============================================================================================================================================*/
// 🧩 The paint card's embedded glyph art and its registry bindings. GENERATED from Documentation/Prototypes/PaintToolMenu.html by
//    _ClaudeScratch/tmp/EmitPaintIconPack.py, which evaluates that file's own GLYPH table rather than transcribing it.
//    ⚠️ EDIT THE PROTOTYPE AND RE-RUN THE EMITTER, never this file. Hand edits are lost the next time it is generated.
//
//    Each parameter mark is a complete 24x24-viewBox document built from the prototype's S() solid, D() dashed, O() stroked-circle and
//    B() filled-dot helpers, with the same geometry, widths and opacities. The prototype strokes `currentColor`, which a standalone
//    SVG has no context for, so the ink is resolved here — the live tier at --ink #ececf0, the gated tier at --faint #55555d.
//
//    The instrument nib art is NOT duplicated here: it lives in PaintCatalogue.cpp (which owns both crops) and this file only binds
//    it into the registry. One copy of 251 KiB of art, not two.

#include "PaintIconPack.h"

#include "PaintCatalogue.h"

#include "EngineContext/Interface/Icons/SvgIconRegistry.h"

#include <cstdio>
#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       EMBEDDED GLYPHS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 One parameter glyph pairs its prototype name with the live and gated documents. The name is the prototype's own identifier
//    so a schema table reads the same in C++ as it does in the HTML; the registry key is derived from it, never hand-written.
struct PaintGlyphEntry
{
    const char* GlyphName;      // [-] - prototype identifier, e.g. "ParamSize"
    const char* LiveDocument;   // [-] - complete <svg>, strokes at --ink
    const char* GatedDocument;  // [-] - the same art at --faint
};

constexpr const char* PaintGlyphParamBinderLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<circle cx=\"12\" cy=\"12\" r=\"8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" opacity=\"1\"/>\n"
    "<path d=\"M12 4v16\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.55\"/>\n"
    "<path d=\"M4.5 15c5 2 10 2 15 0\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.55\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamBinderGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<circle cx=\"12\" cy=\"12\" r=\"8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" opacity=\"1\"/>\n"
    "<path d=\"M12 4v16\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.55\"/>\n"
    "<path d=\"M4.5 15c5 2 10 2 15 0\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.55\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamBlendLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<circle cx=\"9\" cy=\"12\" r=\"6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" opacity=\"1\"/>\n"
    "<circle cx=\"15\" cy=\"12\" r=\"6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" opacity=\"0.6\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamBlendGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<circle cx=\"9\" cy=\"12\" r=\"6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" opacity=\"1\"/>\n"
    "<circle cx=\"15\" cy=\"12\" r=\"6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" opacity=\"0.6\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamBristleLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M7 3h10v7H7z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M8 10l-1 11M12 10v11M16 10l1 11\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.75\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamBristleGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M7 3h10v7H7z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M8 10l-1 11M12 10v11M16 10l1 11\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.75\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamDensityLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.7\"/>\n"
    "<circle cx=\"8\" cy=\"8\" r=\"1.5\" fill=\"#ececf0\" opacity=\"0.9\"/>\n"
    "<circle cx=\"14\" cy=\"8\" r=\"1.5\" fill=\"#ececf0\" opacity=\"0.9\"/>\n"
    "<circle cx=\"8\" cy=\"14\" r=\"1.5\" fill=\"#ececf0\" opacity=\"0.9\"/>\n"
    "<circle cx=\"14\" cy=\"14\" r=\"1.5\" fill=\"#ececf0\" opacity=\"0.9\"/>\n"
    "<circle cx=\"11\" cy=\"11\" r=\"1.2\" fill=\"#ececf0\" opacity=\"0.6\"/>\n"
    "<circle cx=\"17\" cy=\"17\" r=\"1.2\" fill=\"#ececf0\" opacity=\"0.6\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamDensityGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.7\"/>\n"
    "<circle cx=\"8\" cy=\"8\" r=\"1.5\" fill=\"#55555d\" opacity=\"0.9\"/>\n"
    "<circle cx=\"14\" cy=\"8\" r=\"1.5\" fill=\"#55555d\" opacity=\"0.9\"/>\n"
    "<circle cx=\"8\" cy=\"14\" r=\"1.5\" fill=\"#55555d\" opacity=\"0.9\"/>\n"
    "<circle cx=\"14\" cy=\"14\" r=\"1.5\" fill=\"#55555d\" opacity=\"0.9\"/>\n"
    "<circle cx=\"11\" cy=\"11\" r=\"1.2\" fill=\"#55555d\" opacity=\"0.6\"/>\n"
    "<circle cx=\"17\" cy=\"17\" r=\"1.2\" fill=\"#55555d\" opacity=\"0.6\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamDripLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M6 4h12\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M9 8v2M15 8v2\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.6\"/>\n"
    "<path d=\"M12 12c2 3 3 4.5 3 6a3 3 0 0 1-6 0c0-1.5 1-3 3-6z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamDripGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M6 4h12\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M9 8v2M15 8v2\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.6\"/>\n"
    "<path d=\"M12 12c2 3 3 4.5 3 6a3 3 0 0 1-6 0c0-1.5 1-3 3-6z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamFlowLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M4 7c4 0 4 5 8 5s4-5 8-5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M4 15c4 0 4 5 8 5s4-5 8-5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.6\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamFlowGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M4 7c4 0 4 5 8 5s4-5 8-5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M4 15c4 0 4 5 8 5s4-5 8-5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.6\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamGlowLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<circle cx=\"12\" cy=\"12\" r=\"3.2\" fill=\"#ececf0\" opacity=\"1\"/>\n"
    "<circle cx=\"12\" cy=\"12\" r=\"7\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" opacity=\"0.45\"/>\n"
    "<path d=\"M12 2v3M12 19v3M2 12h3M19 12h3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.7\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamGlowGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<circle cx=\"12\" cy=\"12\" r=\"3.2\" fill=\"#55555d\" opacity=\"1\"/>\n"
    "<circle cx=\"12\" cy=\"12\" r=\"7\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" opacity=\"0.45\"/>\n"
    "<path d=\"M12 2v3M12 19v3M2 12h3M19 12h3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.7\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamGradeLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M4 18L12 5l8 13\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M8 18h8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.6\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamGradeGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M4 18L12 5l8 13\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M8 18h8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.6\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamGrainLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<circle cx=\"6\" cy=\"7\" r=\"1.3\" fill=\"#ececf0\" opacity=\"0.8\"/>\n"
    "<circle cx=\"12\" cy=\"9\" r=\"1.3\" fill=\"#ececf0\" opacity=\"0.8\"/>\n"
    "<circle cx=\"18\" cy=\"6\" r=\"1.3\" fill=\"#ececf0\" opacity=\"0.8\"/>\n"
    "<circle cx=\"8\" cy=\"14\" r=\"1.3\" fill=\"#ececf0\" opacity=\"0.6\"/>\n"
    "<circle cx=\"15\" cy=\"15\" r=\"1.3\" fill=\"#ececf0\" opacity=\"0.6\"/>\n"
    "<circle cx=\"11\" cy=\"19\" r=\"1.3\" fill=\"#ececf0\" opacity=\"0.45\"/>\n"
    "<circle cx=\"19\" cy=\"17\" r=\"1.3\" fill=\"#ececf0\" opacity=\"0.45\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamGrainGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<circle cx=\"6\" cy=\"7\" r=\"1.3\" fill=\"#55555d\" opacity=\"0.8\"/>\n"
    "<circle cx=\"12\" cy=\"9\" r=\"1.3\" fill=\"#55555d\" opacity=\"0.8\"/>\n"
    "<circle cx=\"18\" cy=\"6\" r=\"1.3\" fill=\"#55555d\" opacity=\"0.8\"/>\n"
    "<circle cx=\"8\" cy=\"14\" r=\"1.3\" fill=\"#55555d\" opacity=\"0.6\"/>\n"
    "<circle cx=\"15\" cy=\"15\" r=\"1.3\" fill=\"#55555d\" opacity=\"0.6\"/>\n"
    "<circle cx=\"11\" cy=\"19\" r=\"1.3\" fill=\"#55555d\" opacity=\"0.45\"/>\n"
    "<circle cx=\"19\" cy=\"17\" r=\"1.3\" fill=\"#55555d\" opacity=\"0.45\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamJitterLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M3 12h3l2-5 3 10 3-8 2 3h5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamJitterGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M3 12h3l2-5 3 10 3-8 2 3h5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamLeadLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M12 3v6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M9 9h6l-1 11h-4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<circle cx=\"12\" cy=\"18\" r=\"1.3\" fill=\"#ececf0\" opacity=\"0.6\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamLeadGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M12 3v6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M9 9h6l-1 11h-4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<circle cx=\"12\" cy=\"18\" r=\"1.3\" fill=\"#55555d\" opacity=\"0.6\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamModeLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M4 12h16\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.7\"/>\n"
    "<circle cx=\"8\" cy=\"8\" r=\"1.4\" fill=\"#ececf0\" opacity=\"0.7\"/>\n"
    "<circle cx=\"16\" cy=\"16\" r=\"1.4\" fill=\"#ececf0\" opacity=\"0.7\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamModeGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M4 12h16\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.7\"/>\n"
    "<circle cx=\"8\" cy=\"8\" r=\"1.4\" fill=\"#55555d\" opacity=\"0.7\"/>\n"
    "<circle cx=\"16\" cy=\"16\" r=\"1.4\" fill=\"#55555d\" opacity=\"0.7\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamNibLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M12 3l4 9h-8z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M12 12v8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.7\"/>\n"
    "<circle cx=\"12\" cy=\"9\" r=\"1.3\" fill=\"#ececf0\" opacity=\"0.6\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamNibGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M12 3l4 9h-8z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M12 12v8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.7\"/>\n"
    "<circle cx=\"12\" cy=\"9\" r=\"1.3\" fill=\"#55555d\" opacity=\"0.6\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamOpacityLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<circle cx=\"12\" cy=\"12\" r=\"8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" opacity=\"1\"/>\n"
    "<path d=\"M12 4a8 8 0 0 1 0 16z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.5\"/>\n"
    "<path d=\"M12 4v16\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\"0.5\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamOpacityGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<circle cx=\"12\" cy=\"12\" r=\"8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" opacity=\"1\"/>\n"
    "<path d=\"M12 4a8 8 0 0 1 0 16z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.5\"/>\n"
    "<path d=\"M12 4v16\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\"0.5\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamPaintLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M5 5h11v6H5z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M16 8h3v5h-6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.7\"/>\n"
    "<path d=\"M13 13v3a2 2 0 0 0 4 0v-3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.6\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamPaintGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M5 5h11v6H5z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M16 8h3v5h-6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.7\"/>\n"
    "<path d=\"M13 13v3a2 2 0 0 0 4 0v-3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.6\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamPigmentLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M8 3h8v5a4 4 0 0 1-1 2.6L12 14l-3-3.4A4 4 0 0 1 8 8z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M12 14v7\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.7\"/>\n"
    "<circle cx=\"12\" cy=\"8\" r=\"1.9\" fill=\"#ececf0\" opacity=\"0.55\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamPigmentGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M8 3h8v5a4 4 0 0 1-1 2.6L12 14l-3-3.4A4 4 0 0 1 8 8z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M12 14v7\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.7\"/>\n"
    "<circle cx=\"12\" cy=\"8\" r=\"1.9\" fill=\"#55555d\" opacity=\"0.55\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamPressureLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M12 3v9\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M8 8l4 4 4-4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M4 17h16\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.7\"/>\n"
    "<path d=\"M6 20h12\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.45\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamPressureGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M12 3v9\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M8 8l4 4 4-4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M4 17h16\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.7\"/>\n"
    "<path d=\"M6 20h12\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.45\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamScatterLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<circle cx=\"5\" cy=\"6\" r=\"1.5\" fill=\"#ececf0\" opacity=\"0.85\"/>\n"
    "<circle cx=\"12\" cy=\"5\" r=\"1.2\" fill=\"#ececf0\" opacity=\"0.6\"/>\n"
    "<circle cx=\"19\" cy=\"8\" r=\"1.5\" fill=\"#ececf0\" opacity=\"0.85\"/>\n"
    "<circle cx=\"8\" cy=\"13\" r=\"1.2\" fill=\"#ececf0\" opacity=\"0.55\"/>\n"
    "<circle cx=\"16\" cy=\"15\" r=\"1.5\" fill=\"#ececf0\" opacity=\"0.8\"/>\n"
    "<circle cx=\"6\" cy=\"19\" r=\"1.2\" fill=\"#ececf0\" opacity=\"0.5\"/>\n"
    "<circle cx=\"13\" cy=\"20\" r=\"1.4\" fill=\"#ececf0\" opacity=\"0.7\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamScatterGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<circle cx=\"5\" cy=\"6\" r=\"1.5\" fill=\"#55555d\" opacity=\"0.85\"/>\n"
    "<circle cx=\"12\" cy=\"5\" r=\"1.2\" fill=\"#55555d\" opacity=\"0.6\"/>\n"
    "<circle cx=\"19\" cy=\"8\" r=\"1.5\" fill=\"#55555d\" opacity=\"0.85\"/>\n"
    "<circle cx=\"8\" cy=\"13\" r=\"1.2\" fill=\"#55555d\" opacity=\"0.55\"/>\n"
    "<circle cx=\"16\" cy=\"15\" r=\"1.5\" fill=\"#55555d\" opacity=\"0.8\"/>\n"
    "<circle cx=\"6\" cy=\"19\" r=\"1.2\" fill=\"#55555d\" opacity=\"0.5\"/>\n"
    "<circle cx=\"13\" cy=\"20\" r=\"1.4\" fill=\"#55555d\" opacity=\"0.7\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamShapeLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<circle cx=\"8\" cy=\"10\" r=\"5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" opacity=\"1\"/>\n"
    "<path d=\"M14 5h6v10h-6z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.7\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamShapeGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<circle cx=\"8\" cy=\"10\" r=\"5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" opacity=\"1\"/>\n"
    "<path d=\"M14 5h6v10h-6z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.7\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamSizeLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M4 20V8M4 20h12\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M4 20L20 4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M20 4h-5M20 4v5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamSizeGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M4 20V8M4 20h12\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M4 20L20 4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M20 4h-5M20 4v5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamSmearLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M4 9c5-3 11 3 16 0\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M4 15c5-3 11 3 16 0\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.5\"/>\n"
    "<path d=\"M4 20h16\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\"0.4\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamSmearGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M4 9c5-3 11 3 16 0\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M4 15c5-3 11 3 16 0\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.5\"/>\n"
    "<path d=\"M4 20h16\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\"0.4\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamSmoothLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M3 16c4 0 5-8 9-8s5 8 9 8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamSmoothGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M3 16c4 0 5-8 9-8s5 8 9 8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamSoftnessLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<circle cx=\"12\" cy=\"12\" r=\"8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" opacity=\"0.35\"/>\n"
    "<circle cx=\"12\" cy=\"12\" r=\"5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" opacity=\"0.6\"/>\n"
    "<circle cx=\"12\" cy=\"12\" r=\"2.4\" fill=\"#ececf0\" opacity=\"0.9\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamSoftnessGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<circle cx=\"12\" cy=\"12\" r=\"8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" opacity=\"0.35\"/>\n"
    "<circle cx=\"12\" cy=\"12\" r=\"5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" opacity=\"0.6\"/>\n"
    "<circle cx=\"12\" cy=\"12\" r=\"2.4\" fill=\"#55555d\" opacity=\"0.9\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamSparkleLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M12 3v6M12 15v6M3 12h6M15 12h6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M6.5 6.5l3 3M14.5 14.5l3 3M17.5 6.5l-3 3M9.5 14.5l-3 3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.7\"/>\n"
    "<circle cx=\"12\" cy=\"12\" r=\"1.9\" fill=\"#ececf0\" opacity=\"1\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamSparkleGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M12 3v6M12 15v6M3 12h6M15 12h6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M6.5 6.5l3 3M14.5 14.5l3 3M17.5 6.5l-3 3M9.5 14.5l-3 3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.7\"/>\n"
    "<circle cx=\"12\" cy=\"12\" r=\"1.9\" fill=\"#55555d\" opacity=\"1\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamSpreadLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M12 4v4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M12 8L5 20M12 8l7 12\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M7 15h10\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\"0.55\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamSpreadGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M12 4v4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M12 8L5 20M12 8l7 12\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M7 15h10\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\"0.55\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamStrengthLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M13 3L5 14h6l-2 7 8-11h-6z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamStrengthGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M13 3L5 14h6l-2 7 8-11h-6z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamTaperLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M7 4h10l-3 16h-4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamTaperGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M7 4h10l-3 16h-4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamTiltLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M4 20h16\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M7 20L17 5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M11 20a7 7 0 0 0-2-4.4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.65\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamTiltGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M4 20h16\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M7 20L17 5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M11 20a7 7 0 0 0-2-4.4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.65\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamToneLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<circle cx=\"12\" cy=\"12\" r=\"8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" opacity=\"1\"/>\n"
    "<path d=\"M12 4a8 8 0 0 1 0 16\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M12 4a8 8 0 0 0 0 16\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.4\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamToneGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<circle cx=\"12\" cy=\"12\" r=\"8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" opacity=\"1\"/>\n"
    "<path d=\"M12 4a8 8 0 0 1 0 16\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M12 4a8 8 0 0 0 0 16\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.4\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamWetnessLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M12 3c3.5 5 6 8 6 11a6 6 0 0 1-12 0c0-3 2.5-6 6-11z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M9.5 15a3 3 0 0 0 3 3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.6\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamWetnessGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M12 3c3.5 5 6 8 6 11a6 6 0 0 1-12 0c0-3 2.5-6 6-11z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "<path d=\"M9.5 15a3 3 0 0 0 3 3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"0.6\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamWidthLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M3 12h18M6 9l-3 3 3 3M18 9l3 3-3 3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "</svg>\n";

constexpr const char* PaintGlyphParamWidthGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">\n"
    "<path d=\"M3 12h18M6 9l-3 3 3 3M18 9l3 3-3 3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>\n"
    "</svg>\n";

constexpr PaintGlyphEntry PaintGlyphs[] =
{
    { "ParamBinder",   PaintGlyphParamBinderLive,   PaintGlyphParamBinderGated   },
    { "ParamBlend",    PaintGlyphParamBlendLive,    PaintGlyphParamBlendGated    },
    { "ParamBristle",  PaintGlyphParamBristleLive,  PaintGlyphParamBristleGated  },
    { "ParamDensity",  PaintGlyphParamDensityLive,  PaintGlyphParamDensityGated  },
    { "ParamDrip",     PaintGlyphParamDripLive,     PaintGlyphParamDripGated     },
    { "ParamFlow",     PaintGlyphParamFlowLive,     PaintGlyphParamFlowGated     },
    { "ParamGlow",     PaintGlyphParamGlowLive,     PaintGlyphParamGlowGated     },
    { "ParamGrade",    PaintGlyphParamGradeLive,    PaintGlyphParamGradeGated    },
    { "ParamGrain",    PaintGlyphParamGrainLive,    PaintGlyphParamGrainGated    },
    { "ParamJitter",   PaintGlyphParamJitterLive,   PaintGlyphParamJitterGated   },
    { "ParamLead",     PaintGlyphParamLeadLive,     PaintGlyphParamLeadGated     },
    { "ParamMode",     PaintGlyphParamModeLive,     PaintGlyphParamModeGated     },
    { "ParamNib",      PaintGlyphParamNibLive,      PaintGlyphParamNibGated      },
    { "ParamOpacity",  PaintGlyphParamOpacityLive,  PaintGlyphParamOpacityGated  },
    { "ParamPaint",    PaintGlyphParamPaintLive,    PaintGlyphParamPaintGated    },
    { "ParamPigment",  PaintGlyphParamPigmentLive,  PaintGlyphParamPigmentGated  },
    { "ParamPressure", PaintGlyphParamPressureLive, PaintGlyphParamPressureGated },
    { "ParamScatter",  PaintGlyphParamScatterLive,  PaintGlyphParamScatterGated  },
    { "ParamShape",    PaintGlyphParamShapeLive,    PaintGlyphParamShapeGated    },
    { "ParamSize",     PaintGlyphParamSizeLive,     PaintGlyphParamSizeGated     },
    { "ParamSmear",    PaintGlyphParamSmearLive,    PaintGlyphParamSmearGated    },
    { "ParamSmooth",   PaintGlyphParamSmoothLive,   PaintGlyphParamSmoothGated   },
    { "ParamSoftness", PaintGlyphParamSoftnessLive, PaintGlyphParamSoftnessGated },
    { "ParamSparkle",  PaintGlyphParamSparkleLive,  PaintGlyphParamSparkleGated  },
    { "ParamSpread",   PaintGlyphParamSpreadLive,   PaintGlyphParamSpreadGated   },
    { "ParamStrength", PaintGlyphParamStrengthLive, PaintGlyphParamStrengthGated },
    { "ParamTaper",    PaintGlyphParamTaperLive,    PaintGlyphParamTaperGated    },
    { "ParamTilt",     PaintGlyphParamTiltLive,     PaintGlyphParamTiltGated     },
    { "ParamTone",     PaintGlyphParamToneLive,     PaintGlyphParamToneGated     },
    { "ParamWetness",  PaintGlyphParamWetnessLive,  PaintGlyphParamWetnessGated  },
    { "ParamWidth",    PaintGlyphParamWidthLive,    PaintGlyphParamWidthGated    },
};

} // namespace


//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::string ResolvePaintGlyphKey(const char* GlyphName, bool GatedCondition)
{
    std::string IconKey = "paint-";
    IconKey += (GlyphName != nullptr) ? GlyphName : "";
    if (GatedCondition)
    {
        IconKey += "-gated";
    }
    return IconKey;
}


// 📝 Keyed by index, not by label. Two instruments CAN carry the same display name in different
//    families, and a duplicate key would silently rebind the first one's texture.
std::string ResolvePaintNibKey(int InstrumentIndex)
{
    char Digits[24] = {};
    std::snprintf(Digits, sizeof(Digits), "%d", InstrumentIndex);

    std::string IconKey = "paint-nib-";
    IconKey += Digits;
    return IconKey;
}


std::string ResolvePaintStripKey(int InstrumentIndex)
{
    char Digits[24] = {};
    std::snprintf(Digits, sizeof(Digits), "%d", InstrumentIndex);

    std::string IconKey = "paint-strip-";
    IconKey += Digits;
    return IconKey;
}


bool RegisterPaintIconPack(SvgIconRegistry& Registry)
{
    bool EveryIconRegistered = true;

    for (const PaintGlyphEntry& Glyph : PaintGlyphs)
    {
        const uint32_t LiveBytes  = static_cast<uint32_t>(std::strlen(Glyph.LiveDocument));
        const uint32_t GatedBytes = static_cast<uint32_t>(std::strlen(Glyph.GatedDocument));

        if (!RegisterSvgIcon(Registry, ResolvePaintGlyphKey(Glyph.GlyphName, false),
                             Glyph.LiveDocument, LiveBytes, PaintGlyphMasterEdge))
        {
            EveryIconRegistered = false;
        }
        if (!RegisterSvgIcon(Registry, ResolvePaintGlyphKey(Glyph.GlyphName, true),
                             Glyph.GatedDocument, GatedBytes, PaintGlyphMasterEdge))
        {
            EveryIconRegistered = false;
        }
    }

    // 📝 Nib art goes through the ordinary square path: the 48x48 crop is already square, so there is
    //    nothing for the registry to distort. The full 5:1 strip is NOT registered here — it needs a
    //    non-square raster, which PaintIconStore supplies on demand for the one site that draws it.
    int InstrumentCount = 0;
    const PaintInstrumentDescriptor* const Instruments = ResolvePaintInstruments(InstrumentCount);

    for (int Index = 0; Index < InstrumentCount; ++Index)
    {
        const char* const Document = Instruments[Index].NibDocument;
        if (Document == nullptr) { continue; }

        const uint32_t ByteCount = static_cast<uint32_t>(std::strlen(Document));
        if (!RegisterSvgIcon(Registry, ResolvePaintNibKey(Index), Document, ByteCount, PaintGlyphMasterEdge))
        {
            EveryIconRegistered = false;
        }
    }

    return EveryIconRegistered;
}

} // namespace Frontier
