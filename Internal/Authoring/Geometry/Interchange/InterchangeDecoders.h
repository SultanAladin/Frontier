/*============================================================================================================================================
                                                           INTERCHANGEDECODERS.H
============================================================================================================================================*/
// 🧩 The PRIVATE per-format decoder seam, included ONLY by the decoder translation units + the public dispatch (Interchange.cpp).
//    Each decoder translates one file format's native container into the shared ImportedModel contract. The vendored libraries
//    (cgltf / fast_obj / ufbx) are pulled in by the decoder .cpp bodies alone, so their type vocabulary never reaches this header
//    or the public one — the translation to PolygonCluster happens entirely inside each .cpp. Keeping this header vendor-free is
//    what lets the public Interchange.h stay a clean engine-only boundary.

#pragma once
#ifndef FRONTIER_AUTHORING_GEOMETRY_INTERCHANGE_INTERCHANGEDECODERS_H
#define FRONTIER_AUTHORING_GEOMETRY_INTERCHANGE_INTERCHANGEDECODERS_H

#include "Interchange.h"

#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       PER-FORMAT DECODERS
//------------------------------------------------------------------------------------------------------------------------

// glTF 2.0 + GLB (cgltf). Metres -> cm (x100); Y-up -> Z-up right-handed; walks the node tree for the source nesting.
bool DecodeGltf(const char* Path, const InterchangeOptions& Options, std::vector<ImportedModel>& Result);

// Wavefront OBJ (fast_obj). Native unit x1; synthesizes flat normals when absent; groups -> objects; UVs optional.
bool DecodeWavefront(const char* Path, const InterchangeOptions& Options, std::vector<ImportedModel>& Result);

// Autodesk FBX (ufbx). Axis + unit auto-convert to the engine convention (Z-up, cm) via ufbx load options; carries FBX nesting.
bool DecodeFilmbox(const char* Path, const InterchangeOptions& Options, std::vector<ImportedModel>& Result);

} // namespace Frontier

#endif
