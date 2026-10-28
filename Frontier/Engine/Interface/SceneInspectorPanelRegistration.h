/*==========================================================================================================================================
                                                  SCENE INSPECTOR PANEL REGISTRATION
                                     ------------------------------------------------------------------
                          Factory that registers the Inspector panel with the shared dockspace (interface SDK
                                                                   builds only).
==========================================================================================================================================*/

// 🧩 Factory that registers the Inspector panel with the shared dockspace (interface SDK builds only).

#pragma once

#include "Engine/Interface/PanelDockspace.h"

namespace Frontier
{

class SceneInspectorPanel;

// Builds the dock registration for one Inspector panel. The host owns the panel instance and calls
// SceneInspectorPanel::Synchronize each frame (after the Outliner's Synchronize) before
// PanelDockspace::ConstructInterface draws it.
[[nodiscard]] DockPanelSpecification MakeSceneInspectorPanelSpecification(SceneInspectorPanel& Panel);

} // namespace Frontier
