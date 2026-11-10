/*==========================================================================================================================================
                                                              VIEWPORTPANEL.H
============================================================================================================================================*/
// 🧩 The scene canvas's docking home: fully blank for now except its header row; renderer arrives later.

#pragma once

#include "EditorTheme.h"

#include <cstddef>
#include <functional>
#include <string>

namespace Frontier
{

//----------------------------------------------------------------------------------------------------------------------
//                                                       VIEWPORT PANEL
//----------------------------------------------------------------------------------------------------------------------

// 📝 Deliberately blank per the current scope: the panel draws only its header row (record summary +
//    live input context, in the Appearance theme) and stays an empty surface below it. The one piece
//    of behaviour it already owns is invisible: the whole surface is the pointer-capture target for
//    the Simulation input context (click to capture, Esc releases), so the shared input layer can be
//    driven before any scene rendering exists. Scene rendering connects here in a later step.
class ViewportPanel
{
  public:
    void ConstructInterface();
    void ConfigureTheme(const EditorTheme& Value) { Palette = Value; }

    // Host wiring, refreshed by WorkspaceHost::Synchronize every cycle.
    size_t                RecordCount        = 0;      // [records]
    std::string           SelectedName;                // [text] - Empty when nothing is selected
    bool                  SimulationCaptured = false;  // [-]
    std::function<void()> RequestCaptureToggle;        // Click on the surface (capture ⇄ release is host policy)

  private:
    EditorTheme Palette;
};

} // namespace Frontier
