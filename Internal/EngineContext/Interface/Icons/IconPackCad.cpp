/*==============================================================================================================================================
                                                                ICONPACKCAD.CPP
==============================================================================================================================================*/
// 🧩 The embedded CAD glyph tier. Each glyph is a complete 32x32 SVG document held as a string literal, keyed to a SketchClassification. Seven map
//    to the shared toolbar art (profile, curve, constraint, dimension, component, mate, coordinate); the datum primitives, the document root, the
//    feature solid, and the two draughting glyphs are authored here in the same house palette. Registration rasterizes once and dedups by content.

#include "EngineContext/Interface/Icons/IconPackCad.h"

#include "EngineContext/Interface/Icons/SvgIconRegistry.h"

#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          EMBEDDED GLYPHS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

struct GlyphEntry
{
    const char* IconKey;      // [-] - registry key, "cad-" prefixed
    const char* SvgDocument;  // [-] - complete <svg> document, ASCII
};

// 📝 The CAD glyphs are drawn into a tiny 18px row box, so the fills are OPAQUE (the old ~0.15-0.2 fill-opacity washes read pale at
//    that size) and the meridian / hidden-edge hairlines sit as lighter tints over the solid fill rather than as near-transparent
//    strokes. The panel draws these at a white tint, so each glyph's own colour reaches the screen at full saturation.

// The document root — stacked sheets standing for the whole parametric document.
constexpr const char* GlyphDocument =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M16 4L28 10L16 16L4 10L16 4Z\" fill=\"#10b981\" stroke=\"#059669\" stroke-width=\"2\" stroke-linejoin=\"round\"/>"
    "<path d=\"M4 16L16 22L28 16\" stroke=\"#2563eb\" stroke-width=\"2.5\" stroke-linejoin=\"round\"/>"
    "<path d=\"M4 22L16 28L28 22\" stroke=\"#ec4899\" stroke-width=\"2.5\" stroke-linejoin=\"round\"/>"
    "<circle cx=\"16\" cy=\"10\" r=\"2.2\" fill=\"#f59e0b\"/>"
    "</svg>";

// Datum plane — a foreshortened quad with a corner origin marker.
constexpr const char* GlyphDatumPlane =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M6 10L26 6L26 22L6 26Z\" fill=\"#f59e0b\" stroke=\"#d97706\" stroke-width=\"2\" stroke-linejoin=\"round\"/>"
    "<path d=\"M6 26L6 10M26 22L26 6\" stroke=\"#fef3c7\" stroke-width=\"1.2\" stroke-dasharray=\"2 2\"/>"
    "<circle cx=\"6\" cy=\"26\" r=\"2.5\" fill=\"#ec4899\"/>"
    "</svg>";

// Datum axis — a directed reference line between two ticks.
constexpr const char* GlyphDatumAxis =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M6 26L26 6\" stroke=\"#f59e0b\" stroke-width=\"2.5\" stroke-linecap=\"round\" stroke-dasharray=\"5 3\"/>"
    "<circle cx=\"6\" cy=\"26\" r=\"2.5\" fill=\"#ec4899\"/>"
    "<path d=\"M22 6L26 6L26 10\" stroke=\"#f59e0b\" stroke-width=\"2.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "</svg>";

// Datum point — a ringed reference point on crosshairs.
constexpr const char* GlyphDatumPoint =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M16 6V26M6 16H26\" stroke=\"#64748b\" stroke-width=\"1\" stroke-dasharray=\"2 2\"/>"
    "<circle cx=\"16\" cy=\"16\" r=\"5\" stroke=\"#f59e0b\" stroke-width=\"2\"/>"
    "<circle cx=\"16\" cy=\"16\" r=\"2\" fill=\"#ec4899\"/>"
    "</svg>";

// Coordinate frame — a three-axis manipulator triad (shared with the Move tool art).
constexpr const char* GlyphCoordinate =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M16 4V28M4 16H28\" stroke=\"#3b82f6\" stroke-width=\"2\"/>"
    "<path d=\"M12 8L16 4L20 8M12 24L16 28L20 24M8 12L4 16L8 20M24 12L28 16L24 20\" stroke=\"#3b82f6\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "</svg>";

// Sketch profile — a closed polygon loop with an origin (shared with the Polygon tool art).
constexpr const char* GlyphProfile =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M16 4L26.3923 10V22L16 28L5.6077 22V10L16 4Z\" stroke=\"#2563eb\" stroke-width=\"2.5\" stroke-linejoin=\"round\"/>"
    "<circle cx=\"16\" cy=\"16\" r=\"12\" stroke=\"#f59e0b\" stroke-width=\"1\" stroke-dasharray=\"2 2\"/>"
    "<circle cx=\"16\" cy=\"16\" r=\"2\" fill=\"#ec4899\"/>"
    "<circle cx=\"16\" cy=\"4\" r=\"2\" fill=\"#10b981\"/>"
    "</svg>";

// Sketch curve — a control-point spline (shared with the Spline tool art).
constexpr const char* GlyphCurve =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 16C8 4 16 28 28 8\" stroke=\"#3b82f6\" stroke-width=\"2.5\" stroke-linecap=\"round\"/>"
    "<path d=\"M4 16L8 4L16 28L28 8\" stroke=\"#f59e0b\" stroke-width=\"1\" stroke-dasharray=\"2 2\"/>"
    "<rect x=\"6.5\" y=\"2.5\" width=\"3\" height=\"3\" fill=\"#ec4899\"/>"
    "<rect x=\"14.5\" y=\"26.5\" width=\"3\" height=\"3\" fill=\"#10b981\"/>"
    "<circle cx=\"4\" cy=\"16\" r=\"2\" fill=\"#64748b\"/>"
    "<circle cx=\"28\" cy=\"8\" r=\"2\" fill=\"#64748b\"/>"
    "</svg>";

// Geometric constraint — a coincidence marker on crossed references (shared with the Coincident constraint art).
constexpr const char* GlyphConstraint =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M8 8L24 24M24 8L8 24\" stroke=\"#64748b\" stroke-width=\"2.5\" stroke-linecap=\"round\"/>"
    "<circle cx=\"16\" cy=\"16\" r=\"3\" fill=\"#ef4444\" stroke=\"#ef4444\" stroke-width=\"2\"/>"
    "</svg>";

// Dimension constraint — a dimensioned span with extension lines (shared with the Dimension tool art).
constexpr const char* GlyphDimension =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M6 26V10M26 26V10\" stroke=\"#64748b\" stroke-width=\"1.5\" stroke-linecap=\"round\"/>"
    "<path d=\"M4 14H28\" stroke=\"#3b82f6\" stroke-width=\"2\" stroke-linecap=\"round\"/>"
    "<path d=\"M10 10L6 14L10 18M22 10L26 14L22 18\" stroke=\"#3b82f6\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "<path d=\"M6 22L26 22\" stroke=\"#64748b\" stroke-width=\"1.5\" stroke-dasharray=\"3 3\"/>"
    "</svg>";

// Feature operation — an extruded solid rising from a profile.
constexpr const char* GlyphFeature =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M6 22L14 26L14 16L6 12Z\" fill=\"#ec4899\" stroke=\"#be185d\" stroke-width=\"2\" stroke-linejoin=\"round\"/>"
    "<path d=\"M14 16L24 12L24 22L14 26Z\" fill=\"#3b82f6\" stroke=\"#2563eb\" stroke-width=\"2\" stroke-linejoin=\"round\"/>"
    "<path d=\"M6 12L16 8L24 12\" stroke=\"#10b981\" stroke-width=\"2.5\" stroke-linejoin=\"round\"/>"
    "<path d=\"M16 8L14 16\" stroke=\"#10b981\" stroke-width=\"2.5\"/>"
    "</svg>";

// Component instance — a grouped assembly of parts (shared with the Group tool art).
constexpr const char* GlyphComponent =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<rect x=\"4\" y=\"4\" width=\"24\" height=\"24\" stroke=\"#64748b\" stroke-width=\"1.5\" stroke-dasharray=\"3 3\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"4\" fill=\"#3b82f6\"/>"
    "<rect x=\"16\" y=\"16\" width=\"8\" height=\"8\" fill=\"#ec4899\"/>"
    "<path d=\"M22 6L26 10H18Z\" fill=\"#10b981\"/>"
    "</svg>";

// Assembly mate — concentric alignment between two parts (shared with the Concentric constraint art).
constexpr const char* GlyphMate =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"16\" cy=\"16\" r=\"10\" stroke=\"#ef4444\" stroke-width=\"2.5\"/>"
    "<circle cx=\"16\" cy=\"16\" r=\"5\" stroke=\"#ef4444\" stroke-width=\"2.5\"/>"
    "<circle cx=\"16\" cy=\"16\" r=\"1.5\" fill=\"#64748b\"/>"
    "</svg>";

// Draughting view — a framed projection window with a corner fold.
constexpr const char* GlyphDrawingView =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M6 6H22L26 10V26H6Z\" fill=\"#3b82f6\" stroke=\"#2563eb\" stroke-width=\"2\" stroke-linejoin=\"round\"/>"
    "<path d=\"M22 6V10H26\" stroke=\"#bfdbfe\" stroke-width=\"2\" stroke-linejoin=\"round\"/>"
    "<path d=\"M11 20L15 15L18 18L21 14\" stroke=\"#10b981\" stroke-width=\"2.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "<circle cx=\"11\" cy=\"20\" r=\"1.8\" fill=\"#ec4899\"/>"
    "</svg>";

// Draughting annotation — a leader line to a text tag.
constexpr const char* GlyphAnnotation =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M6 26L14 18\" stroke=\"#f59e0b\" stroke-width=\"2.5\" stroke-linecap=\"round\"/>"
    "<circle cx=\"6\" cy=\"26\" r=\"2.2\" fill=\"#ec4899\"/>"
    "<rect x=\"14\" y=\"6\" width=\"14\" height=\"12\" rx=\"2\" fill=\"#3b82f6\" stroke=\"#2563eb\" stroke-width=\"2\"/>"
    "<path d=\"M18 10H24M18 14H22\" stroke=\"#bfdbfe\" stroke-width=\"2\" stroke-linecap=\"round\"/>"
    "</svg>";

constexpr GlyphEntry CadGlyphs[] =
{
    { "cad-document",     GlyphDocument    },
    { "cad-datum-plane",  GlyphDatumPlane  },
    { "cad-datum-axis",   GlyphDatumAxis   },
    { "cad-datum-point",  GlyphDatumPoint  },
    { "cad-coordinate",   GlyphCoordinate  },
    { "cad-profile",      GlyphProfile     },
    { "cad-curve",        GlyphCurve       },
    { "cad-constraint",   GlyphConstraint  },
    { "cad-dimension",    GlyphDimension   },
    { "cad-feature",      GlyphFeature     },
    { "cad-component",    GlyphComponent   },
    { "cad-mate",         GlyphMate        },
    { "cad-drawing-view", GlyphDrawingView },
    { "cad-annotation",   GlyphAnnotation  },
};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool RegisterCadIconPack(SvgIconRegistry& Registry)
{
    bool EveryGlyphRegistered = true;
    for (const GlyphEntry& Glyph : CadGlyphs)
    {
        const uint32_t ByteCount = static_cast<uint32_t>(std::strlen(Glyph.SvgDocument));
        if (!RegisterSvgIcon(Registry, Glyph.IconKey, Glyph.SvgDocument, ByteCount, 0u))
        {
            EveryGlyphRegistered = false;
        }
    }
    return EveryGlyphRegistered;
}

} // namespace Frontier
