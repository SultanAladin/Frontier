/*==========================================================================================================================================
                                                           RECORDINSPECTORPANEL.H
============================================================================================================================================*/
// 🧩 Inspector for the Outliner selection: intentionally blank for now; its content arrives later.

#pragma once

#include "Engine/Scene/RecordToken.h"

#include <functional>

namespace Frontier
{

class SceneDirectoryPanel;

//----------------------------------------------------------------------------------------------------------------------
//                                                    RECORD INSPECTOR PANEL
//----------------------------------------------------------------------------------------------------------------------

// 📝 Placeholder member of the editor triad (Outliner · Viewport · Inspector): the panel is docked and
//    wired to the Outliner, but deliberately draws nothing for now — its identity/nesting/visibility
//    content is a later step. Selection routing stays available for that step via SelectRecord.
class RecordInspectorPanel
{
  public:
    explicit RecordInspectorPanel(const SceneDirectoryPanel& Outliner) : OutlinerPanel(Outliner) {}

    void ConstructInterface();   // Intentionally blank for now (see class note)

    std::function<void(RecordToken)> SelectRecord;   // Host wiring: OutlinerPanel.Select

  private:
    const SceneDirectoryPanel& OutlinerPanel;   // Reserved for the inspector content step
};

} // namespace Frontier
