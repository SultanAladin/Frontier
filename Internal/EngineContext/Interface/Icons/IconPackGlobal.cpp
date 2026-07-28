/*==============================================================================================================================================
                                                                ICONPACKGLOBAL.CPP
==============================================================================================================================================*/
// 🧩 The embedded global glyph tier. Each glyph is a complete 32x32 SVG document held as a string literal and handed to the registry under its
//    "g-" key. The art follows one house palette shared with the CAD tier — blue strokes (#2563eb / #3b82f6), pink accents (#ec4899), green
//    (#10b981), amber (#f59e0b), slate (#64748b) — so a folder here reads the same as a folder anywhere. Registration rasterizes once and dedups.

#include "EngineContext/Interface/Icons/IconPackGlobal.h"

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
    const char* IconKey;      // [-] - registry key, "g-" prefixed
    const char* SvgDocument;  // [-] - complete <svg> document, ASCII
};

// The closed folder — the neutral container glyph every outliner shows for a group with no domain of its own.
constexpr const char* GlyphFolderClosed =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 8C4 6.89543 4.89543 6 6 6H12L16 10H26C27.1046 10 28 10.8954 28 12V24C28 25.1046 27.1046 26 26 26H6C4.89543 26 4 25.1046 4 24V8Z\" fill=\"#3b82f6\" fill-opacity=\"0.2\" stroke=\"#3b82f6\" stroke-width=\"2\" stroke-linejoin=\"round\"/>"
    "</svg>";

// The open folder — the same container while its subtree is expanded.
constexpr const char* GlyphFolderOpen =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 8C4 6.89543 4.89543 6 6 6H12L16 10H26C27.1046 10 28 10.8954 28 12V14M4 24V8M4 24L8 14H29L25 26H6C4.89543 26 4 25.1046 4 24Z\" fill=\"#3b82f6\" fill-opacity=\"0.2\" stroke=\"#3b82f6\" stroke-width=\"2\" stroke-linejoin=\"round\"/>"
    "</svg>";

// The open eye — a row that is currently visible.
constexpr const char* GlyphEyeOpen =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 16C4 16 9 8 16 8C23 8 28 16 28 16C28 16 23 24 16 24C9 24 4 16 4 16Z\" stroke=\"#3b82f6\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "<circle cx=\"16\" cy=\"16\" r=\"4\" stroke=\"#3b82f6\" stroke-width=\"2\"/>"
    "<circle cx=\"16\" cy=\"16\" r=\"1.5\" fill=\"#10b981\"/>"
    "</svg>";

// The struck-through eye — a row that is currently hidden.
constexpr const char* GlyphEyeClosed =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M4 16C4 16 9 8 16 8C23 8 28 16 28 16C28 16 23 24 16 24C9 24 4 16 4 16Z\" stroke=\"#64748b\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "<circle cx=\"16\" cy=\"16\" r=\"4\" stroke=\"#64748b\" stroke-width=\"2\"/>"
    "<path d=\"M6 6L26 26\" stroke=\"#ef4444\" stroke-width=\"2\" stroke-linecap=\"round\"/>"
    "</svg>";

// The down chevron — an expanded twisty.
constexpr const char* GlyphChevronDown =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M8 12L16 20L24 12\" stroke=\"#64748b\" stroke-width=\"2.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "</svg>";

// The right chevron — a collapsed twisty.
constexpr const char* GlyphChevronRight =
    "<svg viewBox=\"0 0 32 32\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M12 8L20 16L12 24\" stroke=\"#64748b\" stroke-width=\"2.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
    "</svg>";

constexpr GlyphEntry GlobalGlyphs[] =
{
    { "g-folder",         GlyphFolderClosed },
    { "g-folder-open",    GlyphFolderOpen   },
    { "g-eye-open",       GlyphEyeOpen      },
    { "g-eye-closed",     GlyphEyeClosed    },
    { "g-chevron-down",   GlyphChevronDown  },
    { "g-chevron-right",  GlyphChevronRight },
};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool RegisterGlobalIconPack(SvgIconRegistry& Registry)
{
    bool EveryGlyphRegistered = true;
    for (const GlyphEntry& Glyph : GlobalGlyphs)
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
