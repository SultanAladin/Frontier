/*==========================================================================================================================================
                                                                INPUTPANEL.H
==========================================================================================================================================*/
// 🧩 Input workspace panel: live action/axis readings, press-to-assign capture, conflicts, TOML persistence.

#pragma once

#include "EditorTheme.h"
#include "Input/InputMap.h"

#include <filesystem>
#include <functional>
#include <string>

namespace Frontier
{

//----------------------------------------------------------------------------------------------------------------------
//                                                         INPUT PANEL
//----------------------------------------------------------------------------------------------------------------------

// 📝 The visible face of the shared input layer inside the editor workspace. Everything it shows is read
//    from the same InputMap the application integrates — lamps and bars are the actual folded readings,
//    reassignment goes through InputMap::ReassignAction (conflicts refuse on screen), and Save/Load use
//    InputMapCodec, so the panel is an interactive test bench, not a mock.
class InputPanel
{
  public:
    explicit InputPanel(InputMap& Map) : Map(Map) {}

    void ConstructInterface();

    [[nodiscard]] bool QueryCapturing() const noexcept { return !CaptureAction.empty(); }

    // Host wiring --------------------------------------------------------------------------------------
    void ConfigureTheme(const EditorTheme& Value) { Palette = Value; }   // Appearance-resolved, per cycle
    void ObserveSubstrate(const InputSubstrate* Substrate) noexcept { Observed = Substrate; }
    void ConfigurePersistencePath(std::filesystem::path Path) { PersistencePath = std::move(Path); }
    std::function<void(bool)>            ConfigureSimulationCaptured;   // Host: context + cursor policy
    std::function<InputPreferences&()>   QueryPreferences;              // Unused today; reserved for live sensitivity

  private:
    void ServiceCapture();

    InputMap&               Map;
    EditorTheme             Palette;
    const InputSubstrate*   Observed = nullptr;
    std::filesystem::path   PersistencePath;
    std::string             StatusLine;
    std::string             CaptureAction;                  // [text] - Empty = not capturing
    InputContextCategory    CaptureContext = InputContextCategory::Editor;
};

} // namespace Frontier
