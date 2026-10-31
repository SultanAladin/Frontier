/*==========================================================================================================================================
                                                      POLYGONCLUSTEREDITOR.H
==========================================================================================================================================*/
// 🧩 Standalone virtualized polygon cluster LOD DAG authoring editor bound to PolygonClusterInspector.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Scene/RecordToken.h"

namespace Frontier
{

#if FRONTIER_EDITOR

class RecordInspector;

class PolygonClusterEditor
{
  public:
    static void ConstructInterface(RecordToken Token, RecordInspector& Inspector);
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
