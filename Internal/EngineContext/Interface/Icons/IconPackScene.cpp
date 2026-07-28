/*==============================================================================================================================================
                                                                ICONPACKSCENE.CPP
==============================================================================================================================================*/
// 🧩 The embedded scene-outliner glyph tier. Each glyph is a complete 32x32 SVG document held as a string literal and handed to the registry under
//    its "scene-" key. The art follows the one house palette shared with the global and CAD tiers — blue strokes (#2563eb / #3b82f6), pink accents
//    (#ec4899), green (#10b981), amber (#f59e0b), purple (#a855f7), slate (#64748b) — so a mesh or a camera reads the same as everywhere else.
//    Folders are NOT authored here: the scene directory shows the shared global "g-folder" / "g-folder-open" glyph for its containers.

#include "EngineContext/Interface/Icons/IconPackScene.h"

#include "EngineContext/Interface/Icons/SvgIconRegistry.h"

#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          EMBEDDED GLYPHS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 One entry pairs a registry key with its SVG document. A trailing helper measures the literal so a caller never mismatches
//    the byte count against the source.
struct GlyphEntry
{
    const char* IconKey;      // [-] - registry key, "scene-" prefixed
    const char* SvgDocument;  // [-] - complete <svg> document, ASCII
};

// 📝 The scene glyphs are authored for a TINY draw box (18px), so they are drawn BOLD: opaque fills instead of the old
//    ~0.15 fill-opacity washes, heavier 2.5-3px strokes, and fewer hairline details that vanish at row size. This keeps a high
//    ratio of saturated pixels per glyph so the icon reads at full colour rather than a pale outline. A white ImGui tint then
//    passes the SVG's own colour straight through.

// The scene root — a solid world sphere with a lighter meridian tracing over it.
constexpr const char* GlyphScene =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"16\" cy=\"16\" r=\"11\" fill=\"#3b82f6\" stroke=\"#2563eb\" stroke-width=\"2\"/>"
    "<path d=\"M5 16H27M16 5V27\" stroke=\"#bfdbfe\" stroke-width=\"1.6\"/>"
    "<path d=\"M16 5C10 9 10 23 16 27C22 23 22 9 16 5Z\" stroke=\"#bfdbfe\" stroke-width=\"1.6\"/>"
    "<circle cx=\"16\" cy=\"16\" r=\"2.4\" fill=\"#ec4899\"/>"
    "</svg>";

// A polygon mesh surface — a solid oblique cube with darker near edges.
constexpr const char* GlyphMesh =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M16 4L27 10V22L16 28L5 22V10L16 4Z\" fill=\"#3b82f6\" stroke=\"#2563eb\" stroke-width=\"2.5\" stroke-linejoin=\"round\"/>"
    "<path d=\"M5 10L16 16L27 10\" stroke=\"#1e40af\" stroke-width=\"2.5\" stroke-linejoin=\"round\"/>"
    "<path d=\"M16 16V28\" stroke=\"#1e40af\" stroke-width=\"2.5\"/>"
    "</svg>";

// The sun / directional emitter — a solid disc with eight bold rays.
constexpr const char* GlyphSun =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<circle cx=\"16\" cy=\"16\" r=\"6\" fill=\"#f59e0b\" stroke=\"#d97706\" stroke-width=\"1.5\"/>"
    "<path d=\"M16 2V6M16 26V30M2 16H6M26 16H30M6 6L8.8 8.8M23.2 23.2L26 26M26 6L23.2 8.8M8.8 23.2L6 26\" stroke=\"#f59e0b\" stroke-width=\"2.6\" stroke-linecap=\"round\"/>"
    "</svg>";

// The area / softbox emitter — a solid foreshortened panel casting an amber cone.
constexpr const char* GlyphArea =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M6 5L22 8V18L6 15Z\" fill=\"#a855f7\" stroke=\"#7e22ce\" stroke-width=\"2\" stroke-linejoin=\"round\"/>"
    "<path d=\"M22 10L28 14M22 13L27 20M20 16L23 24\" stroke=\"#f59e0b\" stroke-width=\"2.2\" stroke-linecap=\"round\"/>"
    "</svg>";

// A camera — a solid body with a bold lens iris.
constexpr const char* GlyphCamera =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 11H9L11 8H21L23 11H28V24H4Z\" fill=\"#3b82f6\" stroke=\"#2563eb\" stroke-width=\"2\" stroke-linejoin=\"round\"/>"
    "<circle cx=\"16\" cy=\"17\" r=\"5\" fill=\"#1e40af\" stroke=\"#bfdbfe\" stroke-width=\"2\"/>"
    "<circle cx=\"16\" cy=\"17\" r=\"1.8\" fill=\"#ec4899\"/>"
    "</svg>";

// An environment / HDRI dome — a solid framed image with a horizon sweep and a sun dot.
constexpr const char* GlyphEnvironment =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<rect x=\"5\" y=\"6\" width=\"22\" height=\"20\" rx=\"2\" fill=\"#3b82f6\" stroke=\"#2563eb\" stroke-width=\"2\"/>"
    "<circle cx=\"11\" cy=\"12\" r=\"2.6\" fill=\"#f59e0b\"/>"
    "<path d=\"M5 22L13 15L18 19L23 14L27 17\" stroke=\"#10b981\" stroke-width=\"2.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "</svg>";

constexpr GlyphEntry SceneGlyphs[] =
{
    { "scene-root",        GlyphScene       },
    { "scene-mesh",        GlyphMesh        },
    { "scene-sun",         GlyphSun         },
    { "scene-area",        GlyphArea        },
    { "scene-camera",      GlyphCamera      },
    { "scene-environment", GlyphEnvironment },
};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool RegisterSceneIconPack(SvgIconRegistry& Registry)
{
    bool EveryGlyphRegistered = true;
    for (const GlyphEntry& Glyph : SceneGlyphs)
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
