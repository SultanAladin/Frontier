/*==============================================================================================================================================
                                                            ICONPACKTOOLMENU.CPP
==============================================================================================================================================*/
// 🧩 The tool-menu glyph tier's embedded art. Each entry is a complete 24x24-viewBox SVG document transcribed from the prototype's GLYPH / BADGE
//    tables: the S() solid stroke, D() dashed stroke, F() filled path, O() stroked circle and B() filled dot builders become <path> and <circle>
//    elements with the same geometry, widths and opacities. The prototype's strokes are `currentColor`, which a standalone SVG has no context for,
//    so the ink is resolved here — the live tier at --ink #ececf0, the gated tier at --faint #55555d.

#include "EngineContext/Interface/Icons/IconPackToolMenu.h"

#include "EngineContext/Interface/Icons/SvgIconRegistry.h"

#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        EMBEDDED GLYPHS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 One glyph pairs its prototype name with the live and gated documents. The name is the prototype's own identifier so a band
//    table reads the same in C++ as it does in the HTML; the registry key is derived from it, never hand-written.
struct ToolGlyphEntry
{
    const char* GlyphName;      // [-] - prototype identifier, e.g. "PrimitiveBox"
    const char* LiveDocument;   // [-] - complete <svg>, strokes at --ink
    const char* GatedDocument;  // [-] - the same art at --faint
};

constexpr const char* ToolGlyphAxisMarkerLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 20V8M12 20l-7-4M12 20l7-4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"7\" r=\"1.7\" fill=\"#ececf0\" opacity=\"1\"/>"
    "<circle cx=\"4\" cy=\"15\" r=\"1.5\" fill=\"#ececf0\" opacity=\".7\"/>"
    "<circle cx=\"20\" cy=\"15\" r=\"1.5\" fill=\"#ececf0\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphAxisMarkerGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 20V8M12 20l-7-4M12 20l7-4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"7\" r=\"1.7\" fill=\"#55555d\" opacity=\"1\"/>"
    "<circle cx=\"4\" cy=\"15\" r=\"1.5\" fill=\"#55555d\" opacity=\".7\"/>"
    "<circle cx=\"20\" cy=\"15\" r=\"1.5\" fill=\"#55555d\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphBevelChamferLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 20V9l5-5h11\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 9h5V4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".5\"/>"
    "<path d=\"M9 9L20 20\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".4\"/>"
    "</svg>";
constexpr const char* ToolGlyphBevelChamferGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 20V9l5-5h11\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 9h5V4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".5\"/>"
    "<path d=\"M9 9L20 20\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".4\"/>"
    "</svg>";
constexpr const char* ToolGlyphBisectPlaneLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M5 9l7-4 7 4-7 4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M5 9v6l7 4 7-4V9\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "<path d=\"M2 12h20\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphBisectPlaneGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M5 9l7-4 7 4-7 4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M5 9v6l7 4 7-4V9\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "<path d=\"M2 12h20\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphBoundaryTraceLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 8c4-3 12-3 16 0\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 8v8c4 3 12 3 16 0V8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".45\"/>"
    "<circle cx=\"4\" cy=\"8\" r=\"1.7\" fill=\"#ececf0\" opacity=\"1\"/>"
    "<circle cx=\"20\" cy=\"8\" r=\"1.7\" fill=\"#ececf0\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphBoundaryTraceGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 8c4-3 12-3 16 0\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 8v8c4 3 12 3 16 0V8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".45\"/>"
    "<circle cx=\"4\" cy=\"8\" r=\"1.7\" fill=\"#55555d\" opacity=\"1\"/>"
    "<circle cx=\"20\" cy=\"8\" r=\"1.7\" fill=\"#55555d\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphBridgeSpanLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 6v12M20 6v12\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 9c5 5 11 5 16 0\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 15c5 5 11 5 16 0\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".55\"/>"
    "</svg>";
constexpr const char* ToolGlyphBridgeSpanGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 6v12M20 6v12\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 9c5 5 11 5 16 0\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 15c5 5 11 5 16 0\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".55\"/>"
    "</svg>";
constexpr const char* ToolGlyphCircleFitLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M5 5h14v14H5z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\"0.55\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"7\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphCircleFitGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M5 5h14v14H5z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\"0.55\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"7\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphCollapseDownLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M5 5h14v14H5z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".5\"/>"
    "<path d=\"M8 8l4 4 4-4M8 16l4-4 4 4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"1.9\" fill=\"#ececf0\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphCollapseDownGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M5 5h14v14H5z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".5\"/>"
    "<path d=\"M8 8l4 4 4-4M8 16l4-4 4 4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"1.9\" fill=\"#55555d\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphConnectPathLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"6\" cy=\"6\" r=\"2.2\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" opacity=\"1\"/>"
    "<circle cx=\"18\" cy=\"18\" r=\"2.2\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" opacity=\"1\"/>"
    "<path d=\"M7.6 7.6l8.8 8.8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphConnectPathGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"6\" cy=\"6\" r=\"2.2\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" opacity=\"1\"/>"
    "<circle cx=\"18\" cy=\"18\" r=\"2.2\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" opacity=\"1\"/>"
    "<path d=\"M7.6 7.6l8.8 8.8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphCoplanarUnifyLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 9l9-4 9 4-9 4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M3 9v6l9 4 9-4V9\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "<path d=\"M12 13v6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphCoplanarUnifyGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 9l9-4 9 4-9 4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M3 9v6l9 4 9-4V9\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "<path d=\"M12 13v6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphCreaseWeightLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 15h18\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M6 15V9M10 15v-4M14 15v-6M18 15v-3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".75\"/>"
    "</svg>";
constexpr const char* ToolGlyphCreaseWeightGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 15h18\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M6 15V9M10 15v-4M14 15v-6M18 15v-3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".75\"/>"
    "</svg>";
constexpr const char* ToolGlyphDecimateSparseLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "<path d=\"M4 12h16M12 4v8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".8\"/>"
    "<path d=\"M12 12v8M4 16h8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".35\"/>"
    "</svg>";
constexpr const char* ToolGlyphDecimateSparseGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "<path d=\"M4 12h16M12 4v8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".8\"/>"
    "<path d=\"M12 12v8M4 16h8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".35\"/>"
    "</svg>";
constexpr const char* ToolGlyphDetachComponentLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M11 5H7a2 2 0 0 0-2 2v10a2 2 0 0 0 2 2h4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M14 12h7M18 9l3 3-3 3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphDetachComponentGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M11 5H7a2 2 0 0 0-2 2v10a2 2 0 0 0 2 2h4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M14 12h7M18 9l3 3-3 3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphDissolveAngleLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 19L12 6l8 13\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M8.5 15a7 7 0 0 0 7 0\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphDissolveAngleGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 19L12 6l8 13\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M8.5 15a7 7 0 0 0 7 0\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphDissolveFadeLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"8\" cy=\"8\" r=\"2.1\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" opacity=\"1\"/>"
    "<circle cx=\"15\" cy=\"9\" r=\"1.5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" opacity=\".75\"/>"
    "<circle cx=\"11\" cy=\"15\" r=\"1.1\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" opacity=\".55\"/>"
    "<circle cx=\"17\" cy=\"16\" r=\".8\" fill=\"#ececf0\" opacity=\".45\"/>"
    "</svg>";
constexpr const char* ToolGlyphDissolveFadeGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"8\" cy=\"8\" r=\"2.1\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" opacity=\"1\"/>"
    "<circle cx=\"15\" cy=\"9\" r=\"1.5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" opacity=\".75\"/>"
    "<circle cx=\"11\" cy=\"15\" r=\"1.1\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" opacity=\".55\"/>"
    "<circle cx=\"17\" cy=\"16\" r=\".8\" fill=\"#55555d\" opacity=\".45\"/>"
    "</svg>";
constexpr const char* ToolGlyphDuplicateOffsetLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4h12v12H4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M8 20h12V8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphDuplicateOffsetGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4h12v12H4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M8 20h12V8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphExtractSurfaceLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 15l8-4 8 4-8 4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "<path d=\"M6 8l6-3 6 3-6 3z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 3V1\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphExtractSurfaceGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 15l8-4 8 4-8 4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "<path d=\"M6 8l6-3 6 3-6 3z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 3V1\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphExtrudeOutLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 14l8-4 8 4-8 4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 10V3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9 6l3-3 3 3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphExtrudeOutGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 14l8-4 8 4-8 4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 10V3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9 6l3-3 3 3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphExtrudeSpikeLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 18l8-14 8 14z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"4\" r=\"1.9\" fill=\"#ececf0\" opacity=\"1\"/>"
    "<path d=\"M4 18h16\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".55\"/>"
    "</svg>";
constexpr const char* ToolGlyphExtrudeSpikeGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 18l8-14 8 14z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"4\" r=\"1.9\" fill=\"#55555d\" opacity=\"1\"/>"
    "<path d=\"M4 18h16\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".55\"/>"
    "</svg>";
constexpr const char* ToolGlyphFillPatchLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 4l8 6-3 9H7l-3-9z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M7 12h10M9 16h6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphFillPatchGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 4l8 6-3 9H7l-3-9z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M7 12h10M9 16h6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphFlattenPlaneLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 18h18\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"7\" cy=\"7\" r=\"1.6\" fill=\"#ececf0\" opacity=\".85\"/>"
    "<circle cx=\"12\" cy=\"5\" r=\"1.6\" fill=\"#ececf0\" opacity=\".85\"/>"
    "<circle cx=\"17\" cy=\"8\" r=\"1.6\" fill=\"#ececf0\" opacity=\".85\"/>"
    "<path d=\"M7 9v7M12 7v9M17 10v6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\"0.55\"/>"
    "</svg>";
constexpr const char* ToolGlyphFlattenPlaneGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 18h18\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"7\" cy=\"7\" r=\"1.6\" fill=\"#55555d\" opacity=\".85\"/>"
    "<circle cx=\"12\" cy=\"5\" r=\"1.6\" fill=\"#55555d\" opacity=\".85\"/>"
    "<circle cx=\"17\" cy=\"8\" r=\"1.6\" fill=\"#55555d\" opacity=\".85\"/>"
    "<path d=\"M7 9v7M12 7v9M17 10v6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\"0.55\"/>"
    "</svg>";
constexpr const char* ToolGlyphGridPatchLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 5h16v14H4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9.33 5v14M14.66 5v14M4 9.66h16M4 14.33h16\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".55\"/>"
    "</svg>";
constexpr const char* ToolGlyphGridPatchGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 5h16v14H4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9.33 5v14M14.66 5v14M4 9.66h16M4 14.33h16\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".55\"/>"
    "</svg>";
constexpr const char* ToolGlyphHoleSealLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 12a8 8 0 0 1 16 0\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 12a8 8 0 0 0 16 0\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\"0.55\"/>"
    "<path d=\"M8 12h8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".55\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"1.5\" fill=\"#ececf0\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphHoleSealGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 12a8 8 0 0 1 16 0\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 12a8 8 0 0 0 16 0\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\"0.55\"/>"
    "<path d=\"M8 12h8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".55\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"1.5\" fill=\"#55555d\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphHullWrapLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3l8 6-3 11H7L4 9z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"8\" cy=\"10\" r=\"1.5\" fill=\"#ececf0\" opacity=\".8\"/>"
    "<circle cx=\"15\" cy=\"9\" r=\"1.5\" fill=\"#ececf0\" opacity=\".8\"/>"
    "<circle cx=\"12\" cy=\"15\" r=\"1.5\" fill=\"#ececf0\" opacity=\".8\"/>"
    "</svg>";
constexpr const char* ToolGlyphHullWrapGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3l8 6-3 11H7L4 9z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"8\" cy=\"10\" r=\"1.5\" fill=\"#55555d\" opacity=\".8\"/>"
    "<circle cx=\"15\" cy=\"9\" r=\"1.5\" fill=\"#55555d\" opacity=\".8\"/>"
    "<circle cx=\"12\" cy=\"15\" r=\"1.5\" fill=\"#55555d\" opacity=\".8\"/>"
    "</svg>";
constexpr const char* ToolGlyphInsetRingLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M8.5 8.5h7v7h-7z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".65\"/>"
    "</svg>";
constexpr const char* ToolGlyphInsetRingGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M8.5 8.5h7v7h-7z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".65\"/>"
    "</svg>";
constexpr const char* ToolGlyphLinkedShellLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"6\" cy=\"7\" r=\"2.2\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" opacity=\"1\"/>"
    "<circle cx=\"18\" cy=\"7\" r=\"2.2\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"17\" r=\"2.2\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" opacity=\"1\"/>"
    "<path d=\"M8 8l8-1M7.5 9.2l3.5 6M16.5 9.2l-3.5 6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".85\"/>"
    "</svg>";
constexpr const char* ToolGlyphLinkedShellGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"6\" cy=\"7\" r=\"2.2\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" opacity=\"1\"/>"
    "<circle cx=\"18\" cy=\"7\" r=\"2.2\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"17\" r=\"2.2\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" opacity=\"1\"/>"
    "<path d=\"M8 8l8-1M7.5 9.2l3.5 6M16.5 9.2l-3.5 6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".85\"/>"
    "</svg>";
constexpr const char* ToolGlyphLoopHighlightLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" opacity=\".4\"/>"
    "<path d=\"M4 12a8 8 0 0 0 16 0\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 12a8 8 0 0 1 16 0\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphLoopHighlightGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" opacity=\".4\"/>"
    "<path d=\"M4 12a8 8 0 0 0 16 0\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 12a8 8 0 0 1 16 0\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphLoopInsertLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 6h16v12H4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "<path d=\"M4 12h16\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9.33 6v12M14.66 6v12\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".4\"/>"
    "</svg>";
constexpr const char* ToolGlyphLoopInsertGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 6h16v12H4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "<path d=\"M4 12h16\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9.33 6v12M14.66 6v12\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".4\"/>"
    "</svg>";
constexpr const char* ToolGlyphLoopOffsetLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 9h16\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 15h16\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\"0.55\"/>"
    "<path d=\"M12 11v2\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphLoopOffsetGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 9h16\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 15h16\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\"0.55\"/>"
    "<path d=\"M12 11v2\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphLooseReclaimLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M5 9h14l-1.5 11h-11z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M3 6h18\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"8\" cy=\"4\" r=\"1.2\" fill=\"#ececf0\" opacity=\".55\"/>"
    "<circle cx=\"13\" cy=\"3\" r=\"1.2\" fill=\"#ececf0\" opacity=\".55\"/>"
    "<circle cx=\"17\" cy=\"4.5\" r=\"1.2\" fill=\"#ececf0\" opacity=\".55\"/>"
    "</svg>";
constexpr const char* ToolGlyphLooseReclaimGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M5 9h14l-1.5 11h-11z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M3 6h18\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"8\" cy=\"4\" r=\"1.2\" fill=\"#55555d\" opacity=\".55\"/>"
    "<circle cx=\"13\" cy=\"3\" r=\"1.2\" fill=\"#55555d\" opacity=\".55\"/>"
    "<circle cx=\"17\" cy=\"4.5\" r=\"1.2\" fill=\"#55555d\" opacity=\".55\"/>"
    "</svg>";
constexpr const char* ToolGlyphMaterialSwatchLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M6 6h12v12H6z\" fill=\"#ececf0\" opacity=\".85\"/>"
    "<circle cx=\"9\" cy=\"9\" r=\"1.2\" fill=\"#ececf0\" opacity=\".4\"/>"
    "<circle cx=\"15\" cy=\"15\" r=\"1.2\" fill=\"#ececf0\" opacity=\".4\"/>"
    "</svg>";
constexpr const char* ToolGlyphMaterialSwatchGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M6 6h12v12H6z\" fill=\"#55555d\" opacity=\".85\"/>"
    "<circle cx=\"9\" cy=\"9\" r=\"1.2\" fill=\"#55555d\" opacity=\".4\"/>"
    "<circle cx=\"15\" cy=\"15\" r=\"1.2\" fill=\"#55555d\" opacity=\".4\"/>"
    "</svg>";
constexpr const char* ToolGlyphMergeInwardLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M5 5l5 5M19 5l-5 5M5 19l5-5M19 19l-5-5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"2.4\" fill=\"#ececf0\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphMergeInwardGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M5 5l5 5M19 5l-5 5M5 19l5-5M19 19l-5-5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"2.4\" fill=\"#55555d\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphMirrorAxisLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3v18\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".8\"/>"
    "<path d=\"M9 7L4 12l5 5z\" fill=\"#ececf0\" opacity=\".9\"/>"
    "<path d=\"M15 7l5 5-5 5z\" fill=\"#ececf0\" opacity=\".9\"/>"
    "</svg>";
constexpr const char* ToolGlyphMirrorAxisGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3v18\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".8\"/>"
    "<path d=\"M9 7L4 12l5 5z\" fill=\"#55555d\" opacity=\".9\"/>"
    "<path d=\"M15 7l5 5-5 5z\" fill=\"#55555d\" opacity=\".9\"/>"
    "</svg>";
constexpr const char* ToolGlyphNonManifoldRepairLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3v18\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 8L4 5M12 8l8-3M12 14l-6 4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".8\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"2.2\" fill=\"#ececf0\" opacity=\".9\"/>"
    "</svg>";
constexpr const char* ToolGlyphNonManifoldRepairGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3v18\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 8L4 5M12 8l8-3M12 14l-6 4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".8\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"2.2\" fill=\"#55555d\" opacity=\".9\"/>"
    "</svg>";
constexpr const char* ToolGlyphNormalArrowLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 16c4-4 14-4 18 0\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 14V4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9 7l3-3 3 3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphNormalArrowGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 16c4-4 14-4 18 0\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 14V4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9 7l3-3 3 3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphOutlineRingLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M9 9h6v6H9z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphOutlineRingGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M9 9h6v6H9z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamAngleLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 20h16M4 20L18 6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M11 20a7 7 0 0 0-2-4.4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamAngleGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 20h16M4 20L18 6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M11 20a7 7 0 0 0-2-4.4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamAxisLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 21V9M12 21l-7-4M12 21l7-4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"7\" r=\"1.6\" fill=\"#ececf0\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamAxisGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 21V9M12 21l-7-4M12 21l7-4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"7\" r=\"1.6\" fill=\"#55555d\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamClampLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M5 4h14v6H5z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9 10v6a3 3 0 0 0 6 0v-6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamClampGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M5 4h14v6H5z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9 10v6a3 3 0 0 0 6 0v-6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamClosedLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"7.5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" opacity=\"1\"/>"
    "<path d=\"M9.5 12l1.8 1.8 3.2-3.6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamClosedGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"7.5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" opacity=\"1\"/>"
    "<path d=\"M9.5 12l1.8 1.8 3.2-3.6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamCountLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 12h18\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M7 9v6M12 9v6M17 9v6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamCountGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 12h18\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M7 9v6M12 9v6M17 9v6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamDepthLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 8l8-4 8 4-8 4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 8v6l8 4 8-4V8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".65\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamDepthGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 8l8-4 8 4-8 4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 8v6l8 4 8-4V8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".65\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamDistanceLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 4v16M8 8l4-4 4 4M8 16l4 4 4-4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamDistanceGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 4v16M8 8l4-4 4 4M8 16l4 4 4-4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamHeightLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 4v16\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M8 8l4-4 4 4M8 16l4 4 4-4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamHeightGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 4v16\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M8 8l4-4 4 4M8 16l4 4 4-4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamLinkLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M9 12h6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M10 8H8a4 4 0 0 0 0 8h2M14 8h2a4 4 0 0 1 0 8h-2\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamLinkGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M9 12h6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M10 8H8a4 4 0 0 0 0 8h2M14 8h2a4 4 0 0 1 0 8h-2\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamMaterialLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M6 6h12v12H6z\" fill=\"#ececf0\" opacity=\".8\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamMaterialGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M6 6h12v12H6z\" fill=\"#55555d\" opacity=\".8\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamMergeLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M5 6l6 6-6 6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M19 6l-6 6 6 6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"1.6\" fill=\"#ececf0\" opacity=\".8\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamMergeGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M5 6l6 6-6 6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M19 6l-6 6 6 6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"1.6\" fill=\"#55555d\" opacity=\".8\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamMirrorLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3v18\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".8\"/>"
    "<path d=\"M9 7L4 12l5 5z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M15 7l5 5-5 5z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamMirrorGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3v18\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".8\"/>"
    "<path d=\"M9 7L4 12l5 5z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M15 7l5 5-5 5z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamNormalLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 18h18\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "<path d=\"M12 18V5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9 8l3-3 3 3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamNormalGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 18h18\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "<path d=\"M12 18V5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9 8l3-3 3 3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamPivotLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"7\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" opacity=\".55\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"2.3\" fill=\"#ececf0\" opacity=\"1\"/>"
    "<path d=\"M12 2v3M12 19v3M2 12h3M19 12h3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".8\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamPivotGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"7\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" opacity=\".55\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"2.3\" fill=\"#55555d\" opacity=\"1\"/>"
    "<path d=\"M12 2v3M12 19v3M2 12h3M19 12h3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".8\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamProfileLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 20c6 0 6-16 16-16\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamProfileGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 20c6 0 6-16 16-16\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamRadiusLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" opacity=\".6\"/>"
    "<path d=\"M12 12L19 8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"1.8\" fill=\"#ececf0\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamRadiusGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" opacity=\".6\"/>"
    "<path d=\"M12 12L19 8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"1.8\" fill=\"#55555d\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamRingLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"3.4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamRingGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"3.4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamSeedLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"2.2\" fill=\"#ececf0\" opacity=\"1\"/>"
    "<circle cx=\"6\" cy=\"7\" r=\"1.4\" fill=\"#ececf0\" opacity=\".7\"/>"
    "<circle cx=\"18\" cy=\"8\" r=\"1.4\" fill=\"#ececf0\" opacity=\".7\"/>"
    "<circle cx=\"7\" cy=\"17\" r=\"1.4\" fill=\"#ececf0\" opacity=\".7\"/>"
    "<circle cx=\"17\" cy=\"16\" r=\"1.4\" fill=\"#ececf0\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamSeedGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"2.2\" fill=\"#55555d\" opacity=\"1\"/>"
    "<circle cx=\"6\" cy=\"7\" r=\"1.4\" fill=\"#55555d\" opacity=\".7\"/>"
    "<circle cx=\"18\" cy=\"8\" r=\"1.4\" fill=\"#55555d\" opacity=\".7\"/>"
    "<circle cx=\"7\" cy=\"17\" r=\"1.4\" fill=\"#55555d\" opacity=\".7\"/>"
    "<circle cx=\"17\" cy=\"16\" r=\"1.4\" fill=\"#55555d\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamSmoothLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 16c4 0 5-8 9-8s5 8 9 8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamSmoothGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 16c4 0 5-8 9-8s5 8 9 8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamSnapLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M6 4v7a6 6 0 0 0 12 0V4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M6 4h4M14 4h4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamSnapGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M6 4v7a6 6 0 0 0 12 0V4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M6 4h4M14 4h4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamStratumLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3l9 5-9 5-9-5z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M3 13l9 5 9-5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamStratumGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3l9 5-9 5-9-5z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M3 13l9 5 9-5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamStrengthLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M13 3L5 14h6l-2 7 8-11h-6z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamStrengthGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M13 3L5 14h6l-2 7 8-11h-6z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamTaperLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M7 4h10l-3 16h-4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamTaperGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M7 4h10l-3 16h-4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamThicknessLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 9h16M4 15h16\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 9v6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamThicknessGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 9h16M4 15h16\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 9v6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamThresholdLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 16h6l3-8 3 12 3-8h3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamThresholdGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 16h6l3-8 3 12 3-8h3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamToleranceLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 9h18M3 15h18\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "<path d=\"M12 5v14\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"1.9\" fill=\"#ececf0\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamToleranceGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 9h18M3 15h18\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "<path d=\"M12 5v14\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"1.9\" fill=\"#55555d\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamTopologyLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "<path d=\"M4 12h16M12 4v8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "<path d=\"M12 12l8 8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamTopologyGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "<path d=\"M4 12h16M12 4v8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "<path d=\"M12 12l8 8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamUnitLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 9h18v6H3z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M8 9v3M13 9v3M18 9v3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamUnitGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 9h18v6H3z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M8 9v3M13 9v3M18 9v3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamVisibleLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M2 12s3.8-6 10-6 10 6 10 6-3.8 6-10 6-10-6-10-6z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"2.4\" fill=\"#ececf0\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamVisibleGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M2 12s3.8-6 10-6 10 6 10 6-3.8 6-10 6-10-6-10-6z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"2.4\" fill=\"#55555d\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamWidthLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 12h18M6 9l-3 3 3 3M18 9l3 3-3 3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphParamWidthGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 12h18M6 9l-3 3 3 3M18 9l3 3-3 3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphPartitionSolidLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"9\" cy=\"12\" r=\"6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" opacity=\"1\"/>"
    "<circle cx=\"15\" cy=\"12\" r=\"6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" opacity=\".6\"/>"
    "<path d=\"M12 6.6a6 6 0 0 0 0 10.8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".9\"/>"
    "</svg>";
constexpr const char* ToolGlyphPartitionSolidGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"9\" cy=\"12\" r=\"6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" opacity=\"1\"/>"
    "<circle cx=\"15\" cy=\"12\" r=\"6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" opacity=\".6\"/>"
    "<path d=\"M12 6.6a6 6 0 0 0 0 10.8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".9\"/>"
    "</svg>";
constexpr const char* ToolGlyphPlanarCorrectLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 14l6-4 6 4 6-4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".55\"/>"
    "<path d=\"M3 18h18\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9 10v8M15 14v4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphPlanarCorrectGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 14l6-4 6 4 6-4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".55\"/>"
    "<path d=\"M3 18h18\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9 10v8M15 14v4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphPokeCentreLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3l9 6-9 12L3 9z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M3 9h18M12 3v18\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".5\"/>"
    "<circle cx=\"12\" cy=\"11\" r=\"1.7\" fill=\"#ececf0\" opacity=\".9\"/>"
    "</svg>";
constexpr const char* ToolGlyphPokeCentreGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3l9 6-9 12L3 9z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M3 9h18M12 3v18\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".5\"/>"
    "<circle cx=\"12\" cy=\"11\" r=\"1.7\" fill=\"#55555d\" opacity=\".9\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveBoxLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3l8 4.5v9L12 21l-8-4.5v-9z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 7.5l8 4.5 8-4.5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "<path d=\"M12 12v9\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveBoxGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3l8 4.5v9L12 21l-8-4.5v-9z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 7.5l8 4.5 8-4.5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "<path d=\"M12 12v9\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveCapsuleLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M7 9a5 5 0 0 1 10 0v6a5 5 0 0 1-10 0z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M7 12h10\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".4\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveCapsuleGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M7 9a5 5 0 0 1 10 0v6a5 5 0 0 1-10 0z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M7 12h10\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".4\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveCircleLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 8c5.5 0 10 1.8 10 4s-4.5 4-10 4-10-1.8-10-4 4.5-4 10-4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"1.6\" fill=\"#ececf0\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveCircleGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 8c5.5 0 10 1.8 10 4s-4.5 4-10 4-10-1.8-10-4 4.5-4 10-4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"1.6\" fill=\"#55555d\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveConeLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3l7 14H5z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M5 17c0 1.7 3.1 3 7 3s7-1.3 7-3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveConeGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3l7 14H5z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M5 17c0 1.7 3.1 3 7 3s7-1.3 7-3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveCylinderLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M5 7c0-1.7 3.1-3 7-3s7 1.3 7 3-3.1 3-7 3-7-1.3-7-3z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M5 7v10c0 1.7 3.1 3 7 3s7-1.3 7-3V7\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveCylinderGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M5 7c0-1.7 3.1-3 7-3s7 1.3 7 3-3.1 3-7 3-7-1.3-7-3z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M5 7v10c0 1.7 3.1 3 7 3s7-1.3 7-3V7\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveDiscLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"8.5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"3.2\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" opacity=\".55\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"1.4\" fill=\"#ececf0\" opacity=\".8\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveDiscGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"8.5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"3.2\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" opacity=\".55\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"1.4\" fill=\"#55555d\" opacity=\".8\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveGearLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" opacity=\"1\"/>"
    "<path d=\"M12 3v3M12 18v3M3 12h3M18 12h3M5.6 5.6l2.1 2.1M16.3 16.3l2.1 2.1M18.4 5.6l-2.1 2.1M7.7 16.3l-2.1 2.1\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".75\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveGearGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" opacity=\"1\"/>"
    "<path d=\"M12 3v3M12 18v3M3 12h3M18 12h3M5.6 5.6l2.1 2.1M16.3 16.3l2.1 2.1M18.4 5.6l-2.1 2.1M7.7 16.3l-2.1 2.1\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".75\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveHelixLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M8 4c8 0 8 4 0 4s-8 4 0 4 8 4 0 4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M16 4v16\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".4\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveHelixGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M8 4c8 0 8 4 0 4s-8 4 0 4 8 4 0 4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M16 4v16\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".4\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveIcoSphereLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3l8 6-3 10H7L4 9z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 3v16M4 9h16M7 19l5-10 5 10\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".45\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveIcoSphereGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3l8 6-3 10H7L4 9z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 3v16M4 9h16M7 19l5-10 5 10\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".45\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitivePlaneLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M2 15l10-6 10 6-10 6z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M7 15l5-3 5 3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".45\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitivePlaneGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M2 15l10-6 10 6-10 6z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M7 15l5-3 5 3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".45\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitivePrismLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3l8 5v8l-8 5-8-5V8z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 3v18\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".4\"/>"
    "<path d=\"M4 8l8 5 8-5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".55\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitivePrismGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3l8 5v8l-8 5-8-5V8z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 3v18\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".4\"/>"
    "<path d=\"M4 8l8 5 8-5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".55\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitivePyramidLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3L21 18H3z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 3v15\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".4\"/>"
    "<path d=\"M3 18l9 3 9-3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitivePyramidGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3L21 18H3z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 3v15\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".4\"/>"
    "<path d=\"M3 18l9 3 9-3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveSphereLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"8.5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" opacity=\"1\"/>"
    "<path d=\"M3.5 12c0-2.2 3.8-4 8.5-4s8.5 1.8 8.5 4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "<path d=\"M3.5 12c0 2.2 3.8 4 8.5 4s8.5-1.8 8.5-4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".35\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveSphereGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"8.5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" opacity=\"1\"/>"
    "<path d=\"M3.5 12c0-2.2 3.8-4 8.5-4s8.5 1.8 8.5 4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "<path d=\"M3.5 12c0 2.2 3.8 4 8.5 4s8.5-1.8 8.5-4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".35\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveTerrainLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M2 18l6-8 4 5 3-4 7 7z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M2 21h20\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveTerrainGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M2 18l6-8 4 5 3-4 7 7z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M2 21h20\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveTextLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 6V4h16v2\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 4v16\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M8 20h8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveTextGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 6V4h16v2\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 4v16\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M8 20h8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveTorusLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"8.5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" opacity=\"1\"/>"
    "<path d=\"M4.5 12c0 1.9 3.4 3.4 7.5 3.4s7.5-1.5 7.5-3.4-3.4-3.4-7.5-3.4S4.5 10.1 4.5 12z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveTorusGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"8.5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" opacity=\"1\"/>"
    "<path d=\"M4.5 12c0 1.9 3.4 3.4 7.5 3.4s7.5-1.5 7.5-3.4-3.4-3.4-7.5-3.4S4.5 10.1 4.5 12z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveTubeLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M5 7c0-1.7 3.1-3 7-3s7 1.3 7 3-3.1 3-7 3-7-1.3-7-3z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M5 7v10c0 1.7 3.1 3 7 3s7-1.3 7-3V7\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "<circle cx=\"12\" cy=\"7\" r=\"3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" opacity=\".55\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveTubeGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M5 7c0-1.7 3.1-3 7-3s7 1.3 7 3-3.1 3-7 3-7-1.3-7-3z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M5 7v10c0 1.7 3.1 3 7 3s7-1.3 7-3V7\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "<circle cx=\"12\" cy=\"7\" r=\"3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" opacity=\".55\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveWedgeLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 19V6l16 13z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 19h16\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "<path d=\"M4 6l6 3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".4\"/>"
    "</svg>";
constexpr const char* ToolGlyphPrimitiveWedgeGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 19V6l16 13z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 19h16\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "<path d=\"M4 6l6 3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".4\"/>"
    "</svg>";
constexpr const char* ToolGlyphQuadrangulatePairLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 4l16 16\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".4\"/>"
    "<path d=\"M9 13l3 3 3-3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphQuadrangulatePairGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 4l16 16\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".4\"/>"
    "<path d=\"M9 13l3 3 3-3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphReferenceArcLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 19a15 15 0 0 1 16-13\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"4\" cy=\"19\" r=\"2.2\" fill=\"#ececf0\" opacity=\"1\"/>"
    "<circle cx=\"20\" cy=\"6\" r=\"2.2\" fill=\"#ececf0\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphReferenceArcGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 19a15 15 0 0 1 16-13\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"4\" cy=\"19\" r=\"2.2\" fill=\"#55555d\" opacity=\"1\"/>"
    "<circle cx=\"20\" cy=\"6\" r=\"2.2\" fill=\"#55555d\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphReferenceAxisLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 21V3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 21L3 16M12 21l9-5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "<circle cx=\"12\" cy=\"21\" r=\"1.8\" fill=\"#ececf0\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphReferenceAxisGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 21V3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 21L3 16M12 21l9-5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "<circle cx=\"12\" cy=\"21\" r=\"1.8\" fill=\"#55555d\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphReferenceBezierLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 19C3 9 21 19 21 6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"3\" cy=\"19\" r=\"2.2\" fill=\"#ececf0\" opacity=\"1\"/>"
    "<circle cx=\"21\" cy=\"6\" r=\"2.2\" fill=\"#ececf0\" opacity=\"1\"/>"
    "<path d=\"M3 19l6-3M21 6l-6 4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphReferenceBezierGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 19C3 9 21 19 21 6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"3\" cy=\"19\" r=\"2.2\" fill=\"#55555d\" opacity=\"1\"/>"
    "<circle cx=\"21\" cy=\"6\" r=\"2.2\" fill=\"#55555d\" opacity=\"1\"/>"
    "<path d=\"M3 19l6-3M21 6l-6 4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphReferenceCircleArcLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" opacity=\".5\"/>"
    "<path d=\"M12 4a8 8 0 0 1 8 8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"1.8\" fill=\"#ececf0\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphReferenceCircleArcGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" opacity=\".5\"/>"
    "<path d=\"M12 4a8 8 0 0 1 8 8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"1.8\" fill=\"#55555d\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphReferenceCursorLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"4.5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" opacity=\".7\"/>"
    "<path d=\"M12 2v5M12 17v5M2 12h5M17 12h5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"1.6\" fill=\"#ececf0\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphReferenceCursorGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"4.5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" opacity=\".7\"/>"
    "<path d=\"M12 2v5M12 17v5M2 12h5M17 12h5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"1.6\" fill=\"#55555d\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphReferenceEdgeLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"5\" cy=\"19\" r=\"2.6\" fill=\"#ececf0\" opacity=\"1\"/>"
    "<circle cx=\"19\" cy=\"5\" r=\"2.6\" fill=\"#ececf0\" opacity=\"1\"/>"
    "<path d=\"M6.6 17.4L17.4 6.6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphReferenceEdgeGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"5\" cy=\"19\" r=\"2.6\" fill=\"#55555d\" opacity=\"1\"/>"
    "<circle cx=\"19\" cy=\"5\" r=\"2.6\" fill=\"#55555d\" opacity=\"1\"/>"
    "<path d=\"M6.6 17.4L17.4 6.6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphReferenceGridLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 3h18v18H3z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".8\"/>"
    "<path d=\"M9 3v18M15 3v18M3 9h18M3 15h18\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphReferenceGridGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 3h18v18H3z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".8\"/>"
    "<path d=\"M9 3v18M15 3v18M3 9h18M3 15h18\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphReferenceMeasureLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 8h18v8H3z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M7 8v4M11 8v3M15 8v4M19 8v3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphReferenceMeasureGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 8h18v8H3z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M7 8v4M11 8v3M15 8v4M19 8v3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphReferencePathLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 18C4 8 12 18 12 8s8 2 8-4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"4\" cy=\"18\" r=\"2.2\" fill=\"#ececf0\" opacity=\"1\"/>"
    "<circle cx=\"20\" cy=\"4\" r=\"2.2\" fill=\"#ececf0\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphReferencePathGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 18C4 8 12 18 12 8s8 2 8-4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"4\" cy=\"18\" r=\"2.2\" fill=\"#55555d\" opacity=\"1\"/>"
    "<circle cx=\"20\" cy=\"4\" r=\"2.2\" fill=\"#55555d\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphReferencePlaneRefLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M2 15l10-6 10 6-10 6z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".7\"/>"
    "<path d=\"M12 9V3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9.5 5.5L12 3l2.5 2.5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphReferencePlaneRefGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M2 15l10-6 10 6-10 6z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".7\"/>"
    "<path d=\"M12 9V3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9.5 5.5L12 3l2.5 2.5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphReferenceSplineLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 18C7 6 17 22 21 8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"3\" cy=\"18\" r=\"1.9\" fill=\"#ececf0\" opacity=\".8\"/>"
    "<circle cx=\"9\" cy=\"12\" r=\"1.6\" fill=\"#ececf0\" opacity=\".55\"/>"
    "<circle cx=\"15\" cy=\"15\" r=\"1.6\" fill=\"#ececf0\" opacity=\".55\"/>"
    "<circle cx=\"21\" cy=\"8\" r=\"1.9\" fill=\"#ececf0\" opacity=\".8\"/>"
    "</svg>";
constexpr const char* ToolGlyphReferenceSplineGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 18C7 6 17 22 21 8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"3\" cy=\"18\" r=\"1.9\" fill=\"#55555d\" opacity=\".8\"/>"
    "<circle cx=\"9\" cy=\"12\" r=\"1.6\" fill=\"#55555d\" opacity=\".55\"/>"
    "<circle cx=\"15\" cy=\"15\" r=\"1.6\" fill=\"#55555d\" opacity=\".55\"/>"
    "<circle cx=\"21\" cy=\"8\" r=\"1.9\" fill=\"#55555d\" opacity=\".8\"/>"
    "</svg>";
constexpr const char* ToolGlyphReferenceVertexLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"3\" fill=\"#ececf0\" opacity=\"1\"/>"
    "<path d=\"M12 2v6M12 16v6M2 12h6M16 12h6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphReferenceVertexGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"3\" fill=\"#55555d\" opacity=\"1\"/>"
    "<path d=\"M12 2v6M12 16v6M2 12h6M16 12h6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphRelaxNeighbourLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"2.2\" fill=\"#ececf0\" opacity=\"1\"/>"
    "<circle cx=\"5\" cy=\"7\" r=\"1.7\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" opacity=\".7\"/>"
    "<circle cx=\"19\" cy=\"7\" r=\"1.7\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" opacity=\".7\"/>"
    "<circle cx=\"5\" cy=\"17\" r=\"1.7\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" opacity=\".7\"/>"
    "<circle cx=\"19\" cy=\"17\" r=\"1.7\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" opacity=\".7\"/>"
    "<path d=\"M7 8.5l3 2M17 8.5l-3 2M7 15.5l3-2M17 15.5l-3-2\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphRelaxNeighbourGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"12\" r=\"2.2\" fill=\"#55555d\" opacity=\"1\"/>"
    "<circle cx=\"5\" cy=\"7\" r=\"1.7\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" opacity=\".7\"/>"
    "<circle cx=\"19\" cy=\"7\" r=\"1.7\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" opacity=\".7\"/>"
    "<circle cx=\"5\" cy=\"17\" r=\"1.7\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" opacity=\".7\"/>"
    "<circle cx=\"19\" cy=\"17\" r=\"1.7\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" opacity=\".7\"/>"
    "<path d=\"M7 8.5l3 2M17 8.5l-3 2M7 15.5l3-2M17 15.5l-3-2\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphRemeshUniformLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4l6 8-4 8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".45\"/>"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 4v16M4 12h16\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".8\"/>"
    "</svg>";
constexpr const char* ToolGlyphRemeshUniformGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4l6 8-4 8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".45\"/>"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 4v16M4 12h16\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".8\"/>"
    "</svg>";
constexpr const char* ToolGlyphRemoveCrossLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M5 7h14M9 7V5h6v2M7 7l1 13h8l1-13\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphRemoveCrossGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M5 7h14M9 7V5h6v2M7 7l1 13h8l1-13\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphRingHighlightLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 8c0-2.5 3.6-4 8-4s8 1.5 8 4-3.6 4-8 4-8-1.5-8-4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 8v8c0 2.5 3.6 4 8 4s8-1.5 8-4V8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphRingHighlightGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 8c0-2.5 3.6-4 8-4s8 1.5 8 4-3.6 4-8 4-8-1.5-8-4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 8v8c0 2.5 3.6 4 8 4s8-1.5 8-4V8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphRipApartLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"5\" r=\"2\" fill=\"#ececf0\" opacity=\"1\"/>"
    "<path d=\"M9 9L4 20M15 9l5 11\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M10 6.5L6 5M14 6.5L18 5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphRipApartGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"5\" r=\"2\" fill=\"#55555d\" opacity=\"1\"/>"
    "<path d=\"M9 9L4 20M15 9l5 11\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M10 6.5L6 5M14 6.5L18 5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphRotateArcLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M20 12a8 8 0 1 1-2.3-5.6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M20 4v4h-4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphRotateArcGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M20 12a8 8 0 1 1-2.3-5.6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M20 4v4h-4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphScaleCornerLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M5 19V9M5 19h10\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M5 19L19 5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M19 5h-4M19 5v4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphScaleCornerGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M5 19V9M5 19h10\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M5 19L19 5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M19 5h-4M19 5v4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphSceneCameraLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 8h11v9H3z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M14 12l6-3.5v9L14 14z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".75\"/>"
    "<circle cx=\"6.5\" cy=\"11\" r=\"1.4\" fill=\"#ececf0\" opacity=\".8\"/>"
    "</svg>";
constexpr const char* ToolGlyphSceneCameraGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 8h11v9H3z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M14 12l6-3.5v9L14 14z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".75\"/>"
    "<circle cx=\"6.5\" cy=\"11\" r=\"1.4\" fill=\"#55555d\" opacity=\".8\"/>"
    "</svg>";
constexpr const char* ToolGlyphSceneDuplicateLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4h12v12H4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".55\"/>"
    "<path d=\"M8 8h12v12H8z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphSceneDuplicateGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4h12v12H4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".55\"/>"
    "<path d=\"M8 8h12v12H8z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphSceneGroupLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 3h18v18H3z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".6\"/>"
    "<path d=\"M7 7h4v4H7zM13 13h4v4h-4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphSceneGroupGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 3h18v18H3z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".6\"/>"
    "<path d=\"M7 7h4v4H7zM13 13h4v4h-4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphSceneImagePlaneLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 5h18v14H3z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M3 16l5-5 4 4 3-3 6 6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "<circle cx=\"8.5\" cy=\"9.5\" r=\"1.5\" fill=\"#ececf0\" opacity=\".8\"/>"
    "</svg>";
constexpr const char* ToolGlyphSceneImagePlaneGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 5h18v14H3z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M3 16l5-5 4 4 3-3 6 6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "<circle cx=\"8.5\" cy=\"9.5\" r=\"1.5\" fill=\"#55555d\" opacity=\".8\"/>"
    "</svg>";
constexpr const char* ToolGlyphSceneImportLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3v11\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M8 10l4 4 4-4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 17v3h16v-3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphSceneImportGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3v11\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M8 10l4 4 4-4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 17v3h16v-3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphSceneLightLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"10\" r=\"4.5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" opacity=\"1\"/>"
    "<path d=\"M12 14.5V18\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9.5 20h5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 2v2M4.5 6l1.4 1.4M19.5 6l-1.4 1.4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphSceneLightGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"12\" cy=\"10\" r=\"4.5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" opacity=\"1\"/>"
    "<path d=\"M12 14.5V18\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9.5 20h5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 2v2M4.5 6l1.4 1.4M19.5 6l-1.4 1.4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphSceneLinkAssetLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M9 15l6-6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M10 6l2-2a4 4 0 0 1 6 6l-2 2\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M14 18l-2 2a4 4 0 0 1-6-6l2-2\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphSceneLinkAssetGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M9 15l6-6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M10 6l2-2a4 4 0 0 1 6 6l-2 2\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M14 18l-2 2a4 4 0 0 1-6-6l2-2\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphScenePasteLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M8 4h8v3H8z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M6 6H4v14h16V6h-2\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M8 12h8M8 16h5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".55\"/>"
    "</svg>";
constexpr const char* ToolGlyphScenePasteGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M8 4h8v3H8z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M6 6H4v14h16V6h-2\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M8 12h8M8 16h5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".55\"/>"
    "</svg>";
constexpr const char* ToolGlyphSceneRecallLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 12a8 8 0 1 0 3-6.2\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M3 4v5h5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"1.6\" fill=\"#ececf0\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphSceneRecallGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 12a8 8 0 1 0 3-6.2\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M3 4v5h5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"1.6\" fill=\"#55555d\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphSceneUnitsLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 6h16v12H4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M8 6v4M12 6v6M16 6v4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "<path d=\"M7 15h10\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphSceneUnitsGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 6h16v12H4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M8 6v4M12 6v6M16 6v4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "<path d=\"M7 15h10\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphSeamSplitLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3v7\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9 14L6 21M15 14l3 7\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9 12h6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphSeamSplitGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3v7\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9 14L6 21M15 14l3 7\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9 12h6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphSelectionGrowLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M8 8h8v8H8z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".6\"/>"
    "<path d=\"M6 6l1.6 1.6M18 6l-1.6 1.6M6 18l1.6-1.6M18 18l-1.6-1.6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphSelectionGrowGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M8 8h8v8H8z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".6\"/>"
    "<path d=\"M6 6l1.6 1.6M18 6l-1.6 1.6M6 18l1.6-1.6M18 18l-1.6-1.6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphSelectionShrinkLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M8 8h8v8H8z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".6\"/>"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M7.6 7.6L6 6M16.4 7.6L18 6M7.6 16.4L6 18M16.4 16.4L18 18\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphSelectionShrinkGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M8 8h8v8H8z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".6\"/>"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M7.6 7.6L6 6M16.4 7.6L18 6M7.6 16.4L6 18M16.4 16.4L18 18\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphShadeFacetLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 17l4.5-7 4.5 4 4.5-6L21 17\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"7.5\" cy=\"10\" r=\"1.5\" fill=\"#ececf0\" opacity=\".6\"/>"
    "<circle cx=\"16.5\" cy=\"8\" r=\"1.5\" fill=\"#ececf0\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphShadeFacetGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 17l4.5-7 4.5 4 4.5-6L21 17\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"7.5\" cy=\"10\" r=\"1.5\" fill=\"#55555d\" opacity=\".6\"/>"
    "<circle cx=\"16.5\" cy=\"8\" r=\"1.5\" fill=\"#55555d\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphShadeSmoothCurveLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 17c3.5 0 4-9 9-9s5.5 9 9 9\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"7\" cy=\"13\" r=\"1.4\" fill=\"#ececf0\" opacity=\".5\"/>"
    "<circle cx=\"12\" cy=\"8.4\" r=\"1.4\" fill=\"#ececf0\" opacity=\".5\"/>"
    "<circle cx=\"17\" cy=\"13\" r=\"1.4\" fill=\"#ececf0\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphShadeSmoothCurveGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 17c3.5 0 4-9 9-9s5.5 9 9 9\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"7\" cy=\"13\" r=\"1.4\" fill=\"#55555d\" opacity=\".5\"/>"
    "<circle cx=\"12\" cy=\"8.4\" r=\"1.4\" fill=\"#55555d\" opacity=\".5\"/>"
    "<circle cx=\"17\" cy=\"13\" r=\"1.4\" fill=\"#55555d\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphSharpEdgeMarkLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 18L12 6l8 12\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 6V2\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9 4.5l3-2.5 3 2.5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphSharpEdgeMarkGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 18L12 6l8 12\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 6V2\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9 4.5l3-2.5 3 2.5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphSharpenInverseLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 17l9-10 9 10\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M3 17c4 0 5-7 9-7s5 7 9 7\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphSharpenInverseGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 17l9-10 9 10\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M3 17c4 0 5-7 9-7s5 7 9 7\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphShellSeparateLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M10 4L4 12l6 8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M14 4l6 8-6 8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 3v18\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphShellSeparateGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M10 4L4 12l6 8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M14 4l6 8-6 8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 3v18\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".5\"/>"
    "</svg>";
constexpr const char* ToolGlyphShellThicknessLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 8c4-3 14-3 18 0\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M3 13c4-3 14-3 18 0\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "<path d=\"M3 8v5M21 8v5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphShellThicknessGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 8c4-3 14-3 18 0\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M3 13c4-3 14-3 18 0\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "<path d=\"M3 8v5M21 8v5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphShortestPathLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"5\" cy=\"18\" r=\"2.2\" fill=\"#ececf0\" opacity=\"1\"/>"
    "<circle cx=\"19\" cy=\"6\" r=\"2.2\" fill=\"#ececf0\" opacity=\"1\"/>"
    "<path d=\"M6.5 16.5C9 14 9 10 12 10s3-3 5.5-3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphShortestPathGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"5\" cy=\"18\" r=\"2.2\" fill=\"#55555d\" opacity=\"1\"/>"
    "<circle cx=\"19\" cy=\"6\" r=\"2.2\" fill=\"#55555d\" opacity=\"1\"/>"
    "<path d=\"M6.5 16.5C9 14 9 10 12 10s3-3 5.5-3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphSimilarTraitLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 5h6v6H4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M14 5h6v6h-6z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 15h6v6H4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".4\"/>"
    "<path d=\"M11 8h2\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".8\"/>"
    "</svg>";
constexpr const char* ToolGlyphSimilarTraitGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 5h6v6H4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M14 5h6v6h-6z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 15h6v6H4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".4\"/>"
    "<path d=\"M11 8h2\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".8\"/>"
    "</svg>";
constexpr const char* ToolGlyphSlideTrackLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 12h18\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M8 8l-4 4 4 4M16 8l4 4-4 4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"2.4\" fill=\"#ececf0\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphSlideTrackGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 12h18\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M8 8l-4 4 4 4M16 8l4 4-4 4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"2.4\" fill=\"#55555d\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphSmoothSurfaceLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 16c4 0 5-8 9-8s5 8 9 8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M3 20h18\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".4\"/>"
    "</svg>";
constexpr const char* ToolGlyphSmoothSurfaceGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 16c4 0 5-8 9-8s5 8 9 8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M3 20h18\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".4\"/>"
    "</svg>";
constexpr const char* ToolGlyphSpanEvenLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 12h18\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".5\"/>"
    "<circle cx=\"4\" cy=\"12\" r=\"2\" fill=\"#ececf0\" opacity=\"1\"/>"
    "<circle cx=\"9.33\" cy=\"12\" r=\"2\" fill=\"#ececf0\" opacity=\"1\"/>"
    "<circle cx=\"14.66\" cy=\"12\" r=\"2\" fill=\"#ececf0\" opacity=\"1\"/>"
    "<circle cx=\"20\" cy=\"12\" r=\"2\" fill=\"#ececf0\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphSpanEvenGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 12h18\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".5\"/>"
    "<circle cx=\"4\" cy=\"12\" r=\"2\" fill=\"#55555d\" opacity=\"1\"/>"
    "<circle cx=\"9.33\" cy=\"12\" r=\"2\" fill=\"#55555d\" opacity=\"1\"/>"
    "<circle cx=\"14.66\" cy=\"12\" r=\"2\" fill=\"#55555d\" opacity=\"1\"/>"
    "<circle cx=\"20\" cy=\"12\" r=\"2\" fill=\"#55555d\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphSpinDiagonalLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M6 18L18 6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M14 4.5a8 8 0 0 1 5 5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "<path d=\"M18.6 6.4l.9 3.4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphSpinDiagonalGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M6 18L18 6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M14 4.5a8 8 0 0 1 5 5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "<path d=\"M18.6 6.4l.9 3.4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphStraightenLineLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 15l4-6 5 7 4-8 5 4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\"0.55\"/>"
    "<path d=\"M3 19h18\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphStraightenLineGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 15l4-6 5 7 4-8 5 4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\"0.55\"/>"
    "<path d=\"M3 19h18\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphStratumPromoteLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"5\" cy=\"18\" r=\"2\" fill=\"#ececf0\" opacity=\"1\"/>"
    "<path d=\"M8 16l4-4\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 12h7v-7z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".8\"/>"
    "</svg>";
constexpr const char* ToolGlyphStratumPromoteGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"5\" cy=\"18\" r=\"2\" fill=\"#55555d\" opacity=\"1\"/>"
    "<path d=\"M8 16l4-4\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 12h7v-7z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".8\"/>"
    "</svg>";
constexpr const char* ToolGlyphSubdivideQuadsLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 4v16M4 12h16\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphSubdivideQuadsGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 4v16M4 12h16\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphSweepPathLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 19C8 19 8 6 20 6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\"0.55\"/>"
    "<path d=\"M4 16v6M1 19h6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"20\" cy=\"6\" r=\"2.6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphSweepPathGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 19C8 19 8 6 20 6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\"0.55\"/>"
    "<path d=\"M4 16v6M1 19h6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"20\" cy=\"6\" r=\"2.6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphSymmetryBalanceLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3v18\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".8\"/>"
    "<path d=\"M9 7L4 12l5 5z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M15 7l5 5-5 5z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"1.5\" fill=\"#ececf0\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphSymmetryBalanceGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3v18\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.4\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".8\"/>"
    "<path d=\"M9 7L4 12l5 5z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M15 7l5 5-5 5z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"1.5\" fill=\"#55555d\" opacity=\".6\"/>"
    "</svg>";
constexpr const char* ToolGlyphTranslateArrowsLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3v18M3 12h18\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9 6l3-3 3 3M9 18l3 3 3-3M6 9l-3 3 3 3M18 9l3 3-3 3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphTranslateArrowsGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 3v18M3 12h18\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9 6l3-3 3 3M9 18l3 3 3-3M6 9l-3 3 3 3M18 9l3 3-3 3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphTriangulateSplitLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 4l16 16\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".8\"/>"
    "</svg>";
constexpr const char* ToolGlyphTriangulateSplitGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M4 4l16 16\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".8\"/>"
    "</svg>";
constexpr const char* ToolGlyphUnSubdivideMergeLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 4v16M4 12h16\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".45\"/>"
    "<path d=\"M9 9l3 3 3-3M9 15l3-3 3 3\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphUnSubdivideMergeGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 4h16v16H4z\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M12 4v16M4 12h16\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\".45\"/>"
    "<path d=\"M9 9l3 3 3-3M9 15l3-3 3 3\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphUvSeamMarkLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 12h16\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\"1\"/>"
    "<path d=\"M4 8v8M20 8v8\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphUvSeamMarkGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 12h16\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-dasharray=\"2 2.4\" opacity=\"1\"/>"
    "<path d=\"M4 8v8M20 8v8\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".7\"/>"
    "</svg>";
constexpr const char* ToolGlyphWeldTargetLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"6\" cy=\"12\" r=\"2.2\" fill=\"#ececf0\" opacity=\"1\"/>"
    "<circle cx=\"18\" cy=\"12\" r=\"2.6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" opacity=\"1\"/>"
    "<path d=\"M9 12h6\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M13 9.5l2.5 2.5-2.5 2.5\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphWeldTargetGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"6\" cy=\"12\" r=\"2.2\" fill=\"#55555d\" opacity=\"1\"/>"
    "<circle cx=\"18\" cy=\"12\" r=\"2.6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" opacity=\"1\"/>"
    "<path d=\"M9 12h6\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M13 9.5l2.5 2.5-2.5 2.5\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "</svg>";
constexpr const char* ToolGlyphWindingReverseLive =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 13c4-4 14-4 18 0\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9 6l3-3 3 3M12 3v7\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M15 20l-3 3-3-3M12 16v7\" fill=\"none\" stroke=\"#ececf0\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".55\"/>"
    "</svg>";
constexpr const char* ToolGlyphWindingReverseGated =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3 13c4-4 14-4 18 0\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M9 6l3-3 3 3M12 3v7\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\"1\"/>"
    "<path d=\"M15 20l-3 3-3-3M12 16v7\" fill=\"none\" stroke=\"#55555d\" stroke-width=\"1.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\".55\"/>"
    "</svg>";

constexpr ToolGlyphEntry ToolGlyphs[] =
{
    { "AxisMarker",         ToolGlyphAxisMarkerLive,         ToolGlyphAxisMarkerGated         },
    { "BevelChamfer",       ToolGlyphBevelChamferLive,       ToolGlyphBevelChamferGated       },
    { "BisectPlane",        ToolGlyphBisectPlaneLive,        ToolGlyphBisectPlaneGated        },
    { "BoundaryTrace",      ToolGlyphBoundaryTraceLive,      ToolGlyphBoundaryTraceGated      },
    { "BridgeSpan",         ToolGlyphBridgeSpanLive,         ToolGlyphBridgeSpanGated         },
    { "CircleFit",          ToolGlyphCircleFitLive,          ToolGlyphCircleFitGated          },
    { "CollapseDown",       ToolGlyphCollapseDownLive,       ToolGlyphCollapseDownGated       },
    { "ConnectPath",        ToolGlyphConnectPathLive,        ToolGlyphConnectPathGated        },
    { "CoplanarUnify",      ToolGlyphCoplanarUnifyLive,      ToolGlyphCoplanarUnifyGated      },
    { "CreaseWeight",       ToolGlyphCreaseWeightLive,       ToolGlyphCreaseWeightGated       },
    { "DecimateSparse",     ToolGlyphDecimateSparseLive,     ToolGlyphDecimateSparseGated     },
    { "DetachComponent",    ToolGlyphDetachComponentLive,    ToolGlyphDetachComponentGated    },
    { "DissolveAngle",      ToolGlyphDissolveAngleLive,      ToolGlyphDissolveAngleGated      },
    { "DissolveFade",       ToolGlyphDissolveFadeLive,       ToolGlyphDissolveFadeGated       },
    { "DuplicateOffset",    ToolGlyphDuplicateOffsetLive,    ToolGlyphDuplicateOffsetGated    },
    { "ExtractSurface",     ToolGlyphExtractSurfaceLive,     ToolGlyphExtractSurfaceGated     },
    { "ExtrudeOut",         ToolGlyphExtrudeOutLive,         ToolGlyphExtrudeOutGated         },
    { "ExtrudeSpike",       ToolGlyphExtrudeSpikeLive,       ToolGlyphExtrudeSpikeGated       },
    { "FillPatch",          ToolGlyphFillPatchLive,          ToolGlyphFillPatchGated          },
    { "FlattenPlane",       ToolGlyphFlattenPlaneLive,       ToolGlyphFlattenPlaneGated       },
    { "GridPatch",          ToolGlyphGridPatchLive,          ToolGlyphGridPatchGated          },
    { "HoleSeal",           ToolGlyphHoleSealLive,           ToolGlyphHoleSealGated           },
    { "HullWrap",           ToolGlyphHullWrapLive,           ToolGlyphHullWrapGated           },
    { "InsetRing",          ToolGlyphInsetRingLive,          ToolGlyphInsetRingGated          },
    { "LinkedShell",        ToolGlyphLinkedShellLive,        ToolGlyphLinkedShellGated        },
    { "LoopHighlight",      ToolGlyphLoopHighlightLive,      ToolGlyphLoopHighlightGated      },
    { "LoopInsert",         ToolGlyphLoopInsertLive,         ToolGlyphLoopInsertGated         },
    { "LoopOffset",         ToolGlyphLoopOffsetLive,         ToolGlyphLoopOffsetGated         },
    { "LooseReclaim",       ToolGlyphLooseReclaimLive,       ToolGlyphLooseReclaimGated       },
    { "MaterialSwatch",     ToolGlyphMaterialSwatchLive,     ToolGlyphMaterialSwatchGated     },
    { "MergeInward",        ToolGlyphMergeInwardLive,        ToolGlyphMergeInwardGated        },
    { "MirrorAxis",         ToolGlyphMirrorAxisLive,         ToolGlyphMirrorAxisGated         },
    { "NonManifoldRepair",  ToolGlyphNonManifoldRepairLive,  ToolGlyphNonManifoldRepairGated  },
    { "NormalArrow",        ToolGlyphNormalArrowLive,        ToolGlyphNormalArrowGated        },
    { "OutlineRing",        ToolGlyphOutlineRingLive,        ToolGlyphOutlineRingGated        },
    { "ParamAngle",         ToolGlyphParamAngleLive,         ToolGlyphParamAngleGated         },
    { "ParamAxis",          ToolGlyphParamAxisLive,          ToolGlyphParamAxisGated          },
    { "ParamClamp",         ToolGlyphParamClampLive,         ToolGlyphParamClampGated         },
    { "ParamClosed",        ToolGlyphParamClosedLive,        ToolGlyphParamClosedGated        },
    { "ParamCount",         ToolGlyphParamCountLive,         ToolGlyphParamCountGated         },
    { "ParamDepth",         ToolGlyphParamDepthLive,         ToolGlyphParamDepthGated         },
    { "ParamDistance",      ToolGlyphParamDistanceLive,      ToolGlyphParamDistanceGated      },
    { "ParamHeight",        ToolGlyphParamHeightLive,        ToolGlyphParamHeightGated        },
    { "ParamLink",          ToolGlyphParamLinkLive,          ToolGlyphParamLinkGated          },
    { "ParamMaterial",      ToolGlyphParamMaterialLive,      ToolGlyphParamMaterialGated      },
    { "ParamMerge",         ToolGlyphParamMergeLive,         ToolGlyphParamMergeGated         },
    { "ParamMirror",        ToolGlyphParamMirrorLive,        ToolGlyphParamMirrorGated        },
    { "ParamNormal",        ToolGlyphParamNormalLive,        ToolGlyphParamNormalGated        },
    { "ParamPivot",         ToolGlyphParamPivotLive,         ToolGlyphParamPivotGated         },
    { "ParamProfile",       ToolGlyphParamProfileLive,       ToolGlyphParamProfileGated       },
    { "ParamRadius",        ToolGlyphParamRadiusLive,        ToolGlyphParamRadiusGated        },
    { "ParamRing",          ToolGlyphParamRingLive,          ToolGlyphParamRingGated          },
    { "ParamSeed",          ToolGlyphParamSeedLive,          ToolGlyphParamSeedGated          },
    { "ParamSmooth",        ToolGlyphParamSmoothLive,        ToolGlyphParamSmoothGated        },
    { "ParamSnap",          ToolGlyphParamSnapLive,          ToolGlyphParamSnapGated          },
    { "ParamStratum",       ToolGlyphParamStratumLive,       ToolGlyphParamStratumGated       },
    { "ParamStrength",      ToolGlyphParamStrengthLive,      ToolGlyphParamStrengthGated      },
    { "ParamTaper",         ToolGlyphParamTaperLive,         ToolGlyphParamTaperGated         },
    { "ParamThickness",     ToolGlyphParamThicknessLive,     ToolGlyphParamThicknessGated     },
    { "ParamThreshold",     ToolGlyphParamThresholdLive,     ToolGlyphParamThresholdGated     },
    { "ParamTolerance",     ToolGlyphParamToleranceLive,     ToolGlyphParamToleranceGated     },
    { "ParamTopology",      ToolGlyphParamTopologyLive,      ToolGlyphParamTopologyGated      },
    { "ParamUnit",          ToolGlyphParamUnitLive,          ToolGlyphParamUnitGated          },
    { "ParamVisible",       ToolGlyphParamVisibleLive,       ToolGlyphParamVisibleGated       },
    { "ParamWidth",         ToolGlyphParamWidthLive,         ToolGlyphParamWidthGated         },
    { "PartitionSolid",     ToolGlyphPartitionSolidLive,     ToolGlyphPartitionSolidGated     },
    { "PlanarCorrect",      ToolGlyphPlanarCorrectLive,      ToolGlyphPlanarCorrectGated      },
    { "PokeCentre",         ToolGlyphPokeCentreLive,         ToolGlyphPokeCentreGated         },
    { "PrimitiveBox",       ToolGlyphPrimitiveBoxLive,       ToolGlyphPrimitiveBoxGated       },
    { "PrimitiveCapsule",   ToolGlyphPrimitiveCapsuleLive,   ToolGlyphPrimitiveCapsuleGated   },
    { "PrimitiveCircle",    ToolGlyphPrimitiveCircleLive,    ToolGlyphPrimitiveCircleGated    },
    { "PrimitiveCone",      ToolGlyphPrimitiveConeLive,      ToolGlyphPrimitiveConeGated      },
    { "PrimitiveCylinder",  ToolGlyphPrimitiveCylinderLive,  ToolGlyphPrimitiveCylinderGated  },
    { "PrimitiveDisc",      ToolGlyphPrimitiveDiscLive,      ToolGlyphPrimitiveDiscGated      },
    { "PrimitiveGear",      ToolGlyphPrimitiveGearLive,      ToolGlyphPrimitiveGearGated      },
    { "PrimitiveHelix",     ToolGlyphPrimitiveHelixLive,     ToolGlyphPrimitiveHelixGated     },
    { "PrimitiveIcoSphere", ToolGlyphPrimitiveIcoSphereLive, ToolGlyphPrimitiveIcoSphereGated },
    { "PrimitivePlane",     ToolGlyphPrimitivePlaneLive,     ToolGlyphPrimitivePlaneGated     },
    { "PrimitivePrism",     ToolGlyphPrimitivePrismLive,     ToolGlyphPrimitivePrismGated     },
    { "PrimitivePyramid",   ToolGlyphPrimitivePyramidLive,   ToolGlyphPrimitivePyramidGated   },
    { "PrimitiveSphere",    ToolGlyphPrimitiveSphereLive,    ToolGlyphPrimitiveSphereGated    },
    { "PrimitiveTerrain",   ToolGlyphPrimitiveTerrainLive,   ToolGlyphPrimitiveTerrainGated   },
    { "PrimitiveText",      ToolGlyphPrimitiveTextLive,      ToolGlyphPrimitiveTextGated      },
    { "PrimitiveTorus",     ToolGlyphPrimitiveTorusLive,     ToolGlyphPrimitiveTorusGated     },
    { "PrimitiveTube",      ToolGlyphPrimitiveTubeLive,      ToolGlyphPrimitiveTubeGated      },
    { "PrimitiveWedge",     ToolGlyphPrimitiveWedgeLive,     ToolGlyphPrimitiveWedgeGated     },
    { "QuadrangulatePair",  ToolGlyphQuadrangulatePairLive,  ToolGlyphQuadrangulatePairGated  },
    { "ReferenceArc",       ToolGlyphReferenceArcLive,       ToolGlyphReferenceArcGated       },
    { "ReferenceAxis",      ToolGlyphReferenceAxisLive,      ToolGlyphReferenceAxisGated      },
    { "ReferenceBezier",    ToolGlyphReferenceBezierLive,    ToolGlyphReferenceBezierGated    },
    { "ReferenceCircleArc", ToolGlyphReferenceCircleArcLive, ToolGlyphReferenceCircleArcGated },
    { "ReferenceCursor",    ToolGlyphReferenceCursorLive,    ToolGlyphReferenceCursorGated    },
    { "ReferenceEdge",      ToolGlyphReferenceEdgeLive,      ToolGlyphReferenceEdgeGated      },
    { "ReferenceGrid",      ToolGlyphReferenceGridLive,      ToolGlyphReferenceGridGated      },
    { "ReferenceMeasure",   ToolGlyphReferenceMeasureLive,   ToolGlyphReferenceMeasureGated   },
    { "ReferencePath",      ToolGlyphReferencePathLive,      ToolGlyphReferencePathGated      },
    { "ReferencePlaneRef",  ToolGlyphReferencePlaneRefLive,  ToolGlyphReferencePlaneRefGated  },
    { "ReferenceSpline",    ToolGlyphReferenceSplineLive,    ToolGlyphReferenceSplineGated    },
    { "ReferenceVertex",    ToolGlyphReferenceVertexLive,    ToolGlyphReferenceVertexGated    },
    { "RelaxNeighbour",     ToolGlyphRelaxNeighbourLive,     ToolGlyphRelaxNeighbourGated     },
    { "RemeshUniform",      ToolGlyphRemeshUniformLive,      ToolGlyphRemeshUniformGated      },
    { "RemoveCross",        ToolGlyphRemoveCrossLive,        ToolGlyphRemoveCrossGated        },
    { "RingHighlight",      ToolGlyphRingHighlightLive,      ToolGlyphRingHighlightGated      },
    { "RipApart",           ToolGlyphRipApartLive,           ToolGlyphRipApartGated           },
    { "RotateArc",          ToolGlyphRotateArcLive,          ToolGlyphRotateArcGated          },
    { "ScaleCorner",        ToolGlyphScaleCornerLive,        ToolGlyphScaleCornerGated        },
    { "SceneCamera",        ToolGlyphSceneCameraLive,        ToolGlyphSceneCameraGated        },
    { "SceneDuplicate",     ToolGlyphSceneDuplicateLive,     ToolGlyphSceneDuplicateGated     },
    { "SceneGroup",         ToolGlyphSceneGroupLive,         ToolGlyphSceneGroupGated         },
    { "SceneImagePlane",    ToolGlyphSceneImagePlaneLive,    ToolGlyphSceneImagePlaneGated    },
    { "SceneImport",        ToolGlyphSceneImportLive,        ToolGlyphSceneImportGated        },
    { "SceneLight",         ToolGlyphSceneLightLive,         ToolGlyphSceneLightGated         },
    { "SceneLinkAsset",     ToolGlyphSceneLinkAssetLive,     ToolGlyphSceneLinkAssetGated     },
    { "ScenePaste",         ToolGlyphScenePasteLive,         ToolGlyphScenePasteGated         },
    { "SceneRecall",        ToolGlyphSceneRecallLive,        ToolGlyphSceneRecallGated        },
    { "SceneUnits",         ToolGlyphSceneUnitsLive,         ToolGlyphSceneUnitsGated         },
    { "SeamSplit",          ToolGlyphSeamSplitLive,          ToolGlyphSeamSplitGated          },
    { "SelectionGrow",      ToolGlyphSelectionGrowLive,      ToolGlyphSelectionGrowGated      },
    { "SelectionShrink",    ToolGlyphSelectionShrinkLive,    ToolGlyphSelectionShrinkGated    },
    { "ShadeFacet",         ToolGlyphShadeFacetLive,         ToolGlyphShadeFacetGated         },
    { "ShadeSmoothCurve",   ToolGlyphShadeSmoothCurveLive,   ToolGlyphShadeSmoothCurveGated   },
    { "SharpEdgeMark",      ToolGlyphSharpEdgeMarkLive,      ToolGlyphSharpEdgeMarkGated      },
    { "SharpenInverse",     ToolGlyphSharpenInverseLive,     ToolGlyphSharpenInverseGated     },
    { "ShellSeparate",      ToolGlyphShellSeparateLive,      ToolGlyphShellSeparateGated      },
    { "ShellThickness",     ToolGlyphShellThicknessLive,     ToolGlyphShellThicknessGated     },
    { "ShortestPath",       ToolGlyphShortestPathLive,       ToolGlyphShortestPathGated       },
    { "SimilarTrait",       ToolGlyphSimilarTraitLive,       ToolGlyphSimilarTraitGated       },
    { "SlideTrack",         ToolGlyphSlideTrackLive,         ToolGlyphSlideTrackGated         },
    { "SmoothSurface",      ToolGlyphSmoothSurfaceLive,      ToolGlyphSmoothSurfaceGated      },
    { "SpanEven",           ToolGlyphSpanEvenLive,           ToolGlyphSpanEvenGated           },
    { "SpinDiagonal",       ToolGlyphSpinDiagonalLive,       ToolGlyphSpinDiagonalGated       },
    { "StraightenLine",     ToolGlyphStraightenLineLive,     ToolGlyphStraightenLineGated     },
    { "StratumPromote",     ToolGlyphStratumPromoteLive,     ToolGlyphStratumPromoteGated     },
    { "SubdivideQuads",     ToolGlyphSubdivideQuadsLive,     ToolGlyphSubdivideQuadsGated     },
    { "SweepPath",          ToolGlyphSweepPathLive,          ToolGlyphSweepPathGated          },
    { "SymmetryBalance",    ToolGlyphSymmetryBalanceLive,    ToolGlyphSymmetryBalanceGated    },
    { "TranslateArrows",    ToolGlyphTranslateArrowsLive,    ToolGlyphTranslateArrowsGated    },
    { "TriangulateSplit",   ToolGlyphTriangulateSplitLive,   ToolGlyphTriangulateSplitGated   },
    { "UnSubdivideMerge",   ToolGlyphUnSubdivideMergeLive,   ToolGlyphUnSubdivideMergeGated   },
    { "UvSeamMark",         ToolGlyphUvSeamMarkLive,         ToolGlyphUvSeamMarkGated         },
    { "WeldTarget",         ToolGlyphWeldTargetLive,         ToolGlyphWeldTargetGated         },
    { "WindingReverse",     ToolGlyphWindingReverseLive,     ToolGlyphWindingReverseGated     },
};

// 📝 The stratum badges. Unlike a tool glyph these carry their own fixed palette (the blue/sky/dim house colours) over a black
//    rounded backing, so they do NOT take a gated variant — a badge names the current selection and is never unavailable.
struct StratumBadgeEntry
{
    const char* StratumName;   // [-] - "Face", "None", ...
    const char* BadgeDocument; // [-] - complete <svg> including its own backing rect
};

constexpr const char* StratumBadgeVertex =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<rect x=\"0\" y=\"0\" width=\"24\" height=\"24\" rx=\"5\" fill=\"#000\"/>"
    "<line x1=\"4\" y1=\"20\" x2=\"20\" y2=\"20\" stroke=\"#8a8a99\" stroke-width=\"1.4\" stroke-linecap=\"round\"/>"
    "<line x1=\"4\" y1=\"20\" x2=\"18\" y2=\"6\" stroke=\"#8a8a99\" stroke-width=\"1.4\" stroke-linecap=\"round\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"3.4\" stroke=\"#5b8cff\" stroke-width=\"1.6\" fill=\"none\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"4.6\" fill=\"#5b8cff33\"/>"
    "</svg>";
constexpr const char* StratumBadgeEdge =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<rect x=\"0\" y=\"0\" width=\"24\" height=\"24\" rx=\"5\" fill=\"#000\"/>"
    "<line x1=\"4\" y1=\"20\" x2=\"20\" y2=\"4\" stroke=\"#5b8cff\" stroke-width=\"2.4\" stroke-linecap=\"round\"/>"
    "<circle cx=\"4\" cy=\"20\" r=\"2.4\" fill=\"#7ec8ff\"/>"
    "<circle cx=\"20\" cy=\"4\" r=\"2.4\" fill=\"#7ec8ff\"/>"
    "</svg>";
constexpr const char* StratumBadgeFace =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<rect x=\"0\" y=\"0\" width=\"24\" height=\"24\" rx=\"5\" fill=\"#000\"/>"
    "<path d=\"M4 8L12 4l8 4-8 4z\" fill=\"#5b8cffcc\" stroke-linejoin=\"round\"/>"
    "<path d=\"M4 8L12 4l8 4-8 4z\" stroke=\"#7ec8ff\" stroke-width=\"1.4\" fill=\"none\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "<circle cx=\"4\" cy=\"8\" r=\"1.6\" fill=\"#e8e8f0\"/>"
    "<circle cx=\"20\" cy=\"8\" r=\"1.6\" fill=\"#e8e8f0\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"1.6\" fill=\"#e8e8f0\"/>"
    "<circle cx=\"12\" cy=\"4\" r=\"1.6\" fill=\"#e8e8f0\"/>"
    "</svg>";
constexpr const char* StratumBadgeLoop =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<rect x=\"0\" y=\"0\" width=\"24\" height=\"24\" rx=\"5\" fill=\"#000\"/>"
    "<path d=\"M4 12a8 8 0 0 0 16 0\" stroke=\"#5b8cff\" stroke-width=\"2.2\" fill=\"none\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "<path d=\"M4 12a8 8 0 0 1 16 0\" stroke=\"#7ec8ff\" stroke-width=\"1.6\" fill=\"none\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "<circle cx=\"4\" cy=\"12\" r=\"2\" fill=\"#e8e8f0\"/>"
    "<circle cx=\"20\" cy=\"12\" r=\"2\" fill=\"#e8e8f0\"/>"
    "</svg>";
constexpr const char* StratumBadgeBorder =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<rect x=\"0\" y=\"0\" width=\"24\" height=\"24\" rx=\"5\" fill=\"#000\"/>"
    "<path d=\"M4 8c4-3 12-3 16 0\" stroke=\"#5b8cff\" stroke-width=\"2.4\" fill=\"none\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "<path d=\"M4 8v8c4 3 12 3 16 0V8\" stroke=\"#8a8a99\" stroke-width=\"1.3\" fill=\"none\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "<circle cx=\"4\" cy=\"8\" r=\"2.2\" fill=\"#7ec8ff\"/>"
    "<circle cx=\"20\" cy=\"8\" r=\"2.2\" fill=\"#7ec8ff\"/>"
    "</svg>";
constexpr const char* StratumBadgeObject =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<rect x=\"0\" y=\"0\" width=\"24\" height=\"24\" rx=\"5\" fill=\"#000\"/>"
    "<path d=\"M12 3l8 5v8l-8 5-8-5V8z\" fill=\"#5b8cffaa\" stroke-linejoin=\"round\"/>"
    "<path d=\"M12 3l8 5v8l-8 5-8-5V8z\" stroke=\"#7ec8ff\" stroke-width=\"1.5\" fill=\"none\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "<path d=\"M4 8l8 5 8-5M12 13v8\" stroke=\"#e8e8f0\" stroke-width=\"1.1\" fill=\"none\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "</svg>";
constexpr const char* StratumBadgeNone =
    "<svg viewBox=\"0 0 24 24\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<rect x=\"0\" y=\"0\" width=\"24\" height=\"24\" rx=\"5\" fill=\"#000\"/>"
    "<path d=\"M5 5h14v14H5z\" stroke=\"#8a8a99\" stroke-width=\"1.3\" fill=\"none\" stroke-linecap=\"round\" stroke-linejoin=\"round\" stroke-dasharray=\"2.6 2.4\"/>"
    "<line x1=\"12\" y1=\"8.5\" x2=\"12\" y2=\"15.5\" stroke=\"#7ec8ff\" stroke-width=\"1.4\" stroke-linecap=\"round\"/>"
    "<line x1=\"8.5\" y1=\"12\" x2=\"15.5\" y2=\"12\" stroke=\"#7ec8ff\" stroke-width=\"1.4\" stroke-linecap=\"round\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"2.2\" stroke=\"#5b8cff\" stroke-width=\"1.4\" fill=\"none\"/>"
    "</svg>";

constexpr StratumBadgeEntry StratumBadges[] =
{
    { "Vertex", StratumBadgeVertex },
    { "Edge",   StratumBadgeEdge },
    { "Face",   StratumBadgeFace },
    { "Loop",   StratumBadgeLoop },
    { "Border", StratumBadgeBorder },
    { "Object", StratumBadgeObject },
    { "None",   StratumBadgeNone },
};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::string ResolveToolGlyphKey(const char* GlyphName, bool GatedCondition)
{
    std::string IconKey = "tool-";
    IconKey += (GlyphName != nullptr) ? GlyphName : "";
    if (GatedCondition)
    {
        IconKey += "-gated";
    }
    return IconKey;
}


bool RegisterToolMenuIconPack(SvgIconRegistry& Registry)
{
    bool EveryGlyphRegistered = true;

    for (const ToolGlyphEntry& Glyph : ToolGlyphs)
    {
        const uint32_t LiveBytes  = static_cast<uint32_t>(std::strlen(Glyph.LiveDocument));
        const uint32_t GatedBytes = static_cast<uint32_t>(std::strlen(Glyph.GatedDocument));

        if (!RegisterSvgIcon(Registry, ResolveToolGlyphKey(Glyph.GlyphName, false),
                             Glyph.LiveDocument, LiveBytes, ToolGlyphMasterEdge))
        {
            EveryGlyphRegistered = false;
        }
        if (!RegisterSvgIcon(Registry, ResolveToolGlyphKey(Glyph.GlyphName, true),
                             Glyph.GatedDocument, GatedBytes, ToolGlyphMasterEdge))
        {
            EveryGlyphRegistered = false;
        }
    }

    // 📝 Badges register under their own "tool-badge-" prefix so a stratum named like a glyph can never collide with one.
    for (const StratumBadgeEntry& Badge : StratumBadges)
    {
        const uint32_t ByteCount = static_cast<uint32_t>(std::strlen(Badge.BadgeDocument));
        std::string    IconKey   = "tool-badge-";
        IconKey += Badge.StratumName;
        if (!RegisterSvgIcon(Registry, IconKey, Badge.BadgeDocument, ByteCount, ToolGlyphMasterEdge))
        {
            EveryGlyphRegistered = false;
        }
    }

    return EveryGlyphRegistered;
}

} // namespace Frontier
