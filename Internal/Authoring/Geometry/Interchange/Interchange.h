/*============================================================================================================================================
                                                              INTERCHANGE.H
============================================================================================================================================*/
// 🧩 The public asset-import boundary. Today it translates an on-disk model file (glTF / GLB / OBJ / FBX) into engine-native
//    PolygonClusters, one per source object, ready to flow through RegisterAuthoredObject exactly like the embedded Suzanne + the
//    ground Plane; the same boundary is where future asset classes (images / HDRI environment, CAD formats) will land beside the
//    model path. The vendored decoder libraries (cgltf / fast_obj / ufbx) and their own type vocabulary stay sealed inside the
//    per-format decoder translation units behind a private header; nothing vendored crosses this public seam. Every model decoder
//    emits the SAME ImportedModel contract, so the runtime never branches on source format — it just registers each returned object.

#pragma once
#ifndef FRONTIER_AUTHORING_GEOMETRY_INTERCHANGE_INTERCHANGE_H
#define FRONTIER_AUTHORING_GEOMETRY_INTERCHANGE_INTERCHANGE_H

#include "PolygonCluster.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONTRACT
//------------------------------------------------------------------------------------------------------------------------

// 📝 One translated source object. Geometry is the engine-native authoring polygon (positions in cm, per-corner UVs when the
//    source carried them, faces preserving tris / quads / N-gons) — the SAME PolygonCluster shape AssembleCalibrationSpecimen
//    fills, so it flows straight into RegisterAuthoredObject. BaseColourHint seeds the object's ObjectMaterial (linear RGB from
//    the source material's base-colour factor, else white). TextureCoordinatesPresent is false for a UV-less source (empty
//    FaceCornerTexture) — the caller then leaves paint disabled rather than stamping into a degenerate UV layout.
//
// 📝 SourceIndex / EnclosureIndex express the source file's object nesting WITHOUT a live pointer: they index into the vector
//    EnumerateModelObjects fills (SourceIndex is this object's own slot; EnclosureIndex is its enclosing object's slot, or -1 for a
//    source-root). The runtime maps SourceIndex -> the pool id RegisterAuthoredObject returned, then re-nests through
//    AppendPlacedGeometry / RelocatePlacedGeometry so the authored scene nesting mirrors the file. A single object returns -1.
struct ImportedModel
{
    PolygonCluster Geometry                   = {};                  // [-]  - engine-native authoring polygon (cm, Z-up, topology preserved)
    float          BaseColourHint[3]          = { 1.0f, 1.0f, 1.0f };// [-]  - linear RGB base-colour seed for the object material
    char           Title[48]                  = {};                  // [-]  - source object name (falls back to a per-format default)
    bool           TextureCoordinatesPresent  = false;               // [-]  - true when the source carried UV0 (paint-eligible)
    int32_t        SourceIndex                = -1;                  // [-]  - this object's slot in the enumerated vector
    int32_t        EnclosureIndex             = -1;                  // [-]  - enclosing object's slot, or -1 for a source-root object
};

// 📝 Per-import knobs the runtime hands the decoders. UnitScaleToCentimetres is the fallback multiplier when a format carries no
//    unit metadata (OBJ = 1); glTF forces its own 100 (metres -> cm) and FBX derives its factor from ufbx unit metadata, so this
//    field is only consulted by decoders that lack an authoritative unit. SynthesizeAbsentNormals fills flat face normals when a
//    source omits them (OBJ frequently does). RecomputeNormalsAlways overrides even present normals (diagnostic; default false).
struct InterchangeOptions
{
    double UnitScaleToCentimetres  = 1.0;    // [-] - fallback linear scale when the format has no unit metadata
    bool   SynthesizeAbsentNormals = true;   // [-] - derive flat face normals when the source omits them
    bool   RecomputeNormalsAlways  = false;  // [-] - ignore source normals and always synthesize (diagnostic)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Translate the FIRST (or merged-root) object of a model file into one ImportedModel. Returns false on an unreadable path, an
// unsupported extension, or a parse failure (Result is left untouched). Convenience wrapper over EnumerateModelObjects for the
// common single-object case; multi-object files should call EnumerateModelObjects to receive every object as-is.
bool TranslateModelFile(const char* Path, const InterchangeOptions& Options, ImportedModel& Result);

// Translate EVERY object of a model file into the vector (appended, not cleared), preserving the source nesting through each
// entry's SourceIndex / EnclosureIndex. This is the multi-object-as-is path: a file with N separated objects yields N entries, each
// its own PolygonCluster with the source nesting recorded. Returns false on an unreadable path / unsupported extension / parse
// failure (nothing appended); returns true with zero appended objects for a valid-but-empty file.
bool EnumerateModelObjects(const char* Path, const InterchangeOptions& Options, std::vector<ImportedModel>& Result);

} // namespace Frontier

#endif
