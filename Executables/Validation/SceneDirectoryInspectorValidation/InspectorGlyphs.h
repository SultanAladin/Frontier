/*==============================================================================================================================================
                                                        INSPECTORGLYPHS.H
==============================================================================================================================================*/
// 🧩 The scene-directory inspector's icon pack. PLACEHOLDER stage — real per-classification art (the Layers / Folder / Cube / Cylinder /
//    Sphere / Cone / Draw-Sketch / Revolve / Loft builders) and the chrome UI glyphs are deferred, so every referenced key registers one
//    neutral reference mark for now. 🔴 The panel resolves classification glyphs under "sdi-class-<key>" ("sdi-class-scene", …) and chrome
//    under "sdi-ui-<key>" ("sdi-ui-search", …); this pack binds the reference mark under exactly those keys so the panel finds a texture.
//    Swap the reference mark for the recoloured GLYPH builders later to bring in the real art. Its function lives in the app-local namespace.

#pragma once
#ifndef FRONTIER_VALIDATION_SCENEDIRECTORYINSPECTOR_INSPECTORGLYPHS_H
#define FRONTIER_VALIDATION_SCENEDIRECTORYINSPECTOR_INSPECTORGLYPHS_H

#include "SceneDirectoryInspector.h"

#include <string>

namespace Frontier { struct SvgIconRegistry; }

namespace SceneDirectoryInspectorValidation
{

// The registry key a classification glyph resolves under ("sdi-class-scene", …).
std::string ClassificationGlyphKey(RecordClassification Classification);

// The registry key a chrome / UI glyph resolves under ("sdi-ui-search", …). Name is a UI glyph token (search / chevron / plus / …).
std::string ChromeGlyphKey(const char* Name);

// Register the reference mark under every classification key and every chrome key. Returns false when any raster / upload failed. Call
// once after the registry is initialized, alongside the global pack.
bool RegisterInspectorGlyphPack(Frontier::SvgIconRegistry& Registry);

} // namespace SceneDirectoryInspectorValidation

#endif
