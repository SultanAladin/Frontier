/*==============================================================================================================================================
                                                    INSPECTORCONTENTPROFILE.H
==============================================================================================================================================*/
// 🧩 The scene-directory inspector's own content profile for the shared outliner panel. It exists so ONE vocabulary drives both halves: the
//    profile's opaque ClassificationId IS the SDI RecordClassification cast to int, so a row created through the outliner's add menu already
//    carries the classification the property cards read. 🔴 Before this profile the rail ran the CAD sketch profile and the cards read a
//    parallel token→classification side-table only the seed wrote, so every ADDED row resolved to Solid and drew the wrong card schema — an
//    add was silently mis-typed. Reading the classification off the tree itself removes the side-table and the drift with it. The tables
//    (labels · hues · glyph keys) are assembled from SceneDirectoryInspector's own resolvers rather than restated, so there is nothing to
//    keep in lock-step. The add catalogue covers every creatable classification (bodies + sketches + primitives + features); Scene is the
//    document root and is never added, matching AddChoices.

#pragma once
#ifndef FRONTIER_VALIDATION_SCENEDIRECTORYINSPECTOR_INSPECTORCONTENTPROFILE_H
#define FRONTIER_VALIDATION_SCENEDIRECTORYINSPECTOR_INSPECTORCONTENTPROFILE_H

#include "EngineContext/Interface/WorkspaceHost/SketchOutliner/OutlinerContentProfile.h"

#include "SceneDirectoryInspector.h"

namespace SceneDirectoryInspectorValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// The SDI content profile: nine classification rows (Scene + Folder are containers), the add catalogue over the eight creatable
// classifications, the nine hue facets, and a SeedSample that leaves the tree EMPTY. Assembled once on first call and held for the process.
const Frontier::SketchOutlinerUi::OutlinerContentProfile& ResolveInspectorContentProfile();

// The SDI classification an outliner ClassificationId denotes. The profile stores the enum's own value, so this is a bounds-guarded cast;
// anything out of range reads as Solid (a safe card schema).
RecordClassification ResolveClassificationOfId(int ClassificationId);

}   // namespace SceneDirectoryInspectorValidation

#endif
