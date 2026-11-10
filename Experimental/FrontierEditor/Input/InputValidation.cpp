/*==========================================================================================================================================
                                                            INPUTVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Headless checks for the shared input layer: synthetic transitions through substrate → map → readings.

#include "Input/InputMap.h"
#include "Input/InputMapCodec.h"
#include "Input/InputSubstrate.h"

#include "Engine/Interface/ConfigurationStructure.h"

#include <cmath>
#include <cstdio>
#include <filesystem>

namespace
{

int Checks   = 0;
int Failures = 0;

void Confirm(bool Condition, const char* Description)
{
    ++Checks;
    if (!Condition)
    {
        ++Failures;
        std::printf("FAIL  %s\n", Description);
    }
}

[[nodiscard]] bool Near(float Left, float Right) noexcept
{
    return std::fabs(Left - Right) < 0.0001f;
}

using namespace Frontier;

struct Rig
{
    InputSubstrate    Substrate;
    InputMap          Map;
    InputPreferences  Preferences{};   // Unity sensitivity (50%)

    Rig() { Map.ConfigureDefaults(); }

    void Cycle()
    {
        Substrate.BeginCycle();
        Map.Integrate(Substrate, Preferences);
    }
    void Press(InputKeyCategory Key, InputModifiers Modifiers = {})
    {
        Substrate.InjectModifiers(Modifiers);
        Substrate.InjectKeyTransition(Key, true, false);
    }
    void Release(InputKeyCategory Key, InputModifiers Modifiers = {})
    {
        Substrate.InjectModifiers(Modifiers);
        Substrate.InjectKeyTransition(Key, false, false);
    }
};

} // namespace

int main()
{
    using Key = InputKeyCategory;

    // ① Press / hold / release edges with exact cycle lifetimes.
    {
        Rig Test;
        Test.Press(Key::F7);
        Test.Cycle();
        Confirm(Test.Map.QueryAction("Editor.ToggleLogStream").Pressed, "F7 press edge fires Editor.ToggleLogStream");
        Confirm(Test.Map.QueryAction("Editor.ToggleLogStream").Down, "F7 held on press cycle");
        Test.Cycle();
        Confirm(!Test.Map.QueryAction("Editor.ToggleLogStream").Pressed, "press edge expires after one cycle");
        Confirm(Test.Map.QueryAction("Editor.ToggleLogStream").Down, "level persists while held");
        Test.Release(Key::F7);
        Test.Cycle();
        Confirm(Test.Map.QueryAction("Editor.ToggleLogStream").Released, "release edge fires");
        Confirm(!Test.Map.QueryAction("Editor.ToggleLogStream").Down, "level clears on release");
    }

    // ② Key repeat never manufactures a second press.
    {
        Rig Test;
        Test.Press(Key::F6);
        Test.Cycle();
        Test.Substrate.InjectKeyTransition(Key::F6, true, true);
        Test.Cycle();
        Confirm(!Test.Map.QueryAction("Editor.ToggleInputPanel").Pressed, "repeat does not re-press");
        Confirm(Test.Map.QueryAction("Editor.ToggleInputPanel").Down, "repeat keeps level down");
    }

    // ③ Press+release inside one cycle reports both edges.
    {
        Rig Test;
        Test.Press(Key::F8);
        Test.Release(Key::F8);
        Test.Cycle();
        const ActionReading Reading = Test.Map.QueryAction("Editor.ToggleOutliner");
        Confirm(Reading.Pressed && Reading.Released && !Reading.Down, "same-cycle tap reports both edges, level up");
    }

    // ④ Chord modifiers must match exactly.
    {
        Rig Test;
        Test.Press(Key::R);
        Test.Cycle();
        Confirm(!Test.Map.QueryAction("Editor.ResetLayout").Pressed, "bare R does not fire Ctrl+Shift+R");
        Test.Release(Key::R);
        Test.Press(Key::R, InputModifiers{true, false, true, false});
        Test.Cycle();
        Confirm(Test.Map.QueryAction("Editor.ResetLayout").Pressed, "Ctrl+Shift+R fires Editor.ResetLayout");
        Test.Press(Key::R, InputModifiers{true, true, true, false});
        Test.Cycle();
        Confirm(!Test.Map.QueryAction("Editor.ResetLayout").Down, "extra Alt breaks the chord");
    }

    // ⑤ Context isolation: simulation controls stay silent in the editor context and vice versa.
    {
        Rig Test;
        Test.Press(Key::W);
        Test.Press(Key::Space);
        Test.Cycle();
        Confirm(Near(Test.Map.QueryAxis("Simulation.Throttle"), 0.0f), "throttle silent in Editor context");
        Confirm(!Test.Map.QueryAction("Simulation.Handbrake").Down, "handbrake silent in Editor context");
        Test.Map.PushContext(InputContextCategory::Simulation);
        Test.Cycle();
        Confirm(Near(Test.Map.QueryAxis("Simulation.Throttle"), 1.0f), "W drives throttle in Simulation context");
        Confirm(Test.Map.QueryAction("Simulation.Handbrake").Down, "Space is handbrake in Simulation context");
        Test.Release(Key::W);
        Test.Press(Key::A);
        Test.Cycle();
        Confirm(Near(Test.Map.QueryAxis("Simulation.Steer"), -1.0f), "A steers negative");
        Test.Press(Key::F7);
        Test.Cycle();
        Confirm(!Test.Map.QueryAction("Editor.ToggleLogStream").Pressed, "editor shortcut silent in Simulation context");
        Test.Press(Key::Escape);
        Test.Cycle();
        Confirm(Test.Map.QueryAction("Simulation.QuitCapture").Pressed, "Escape resolves as QuitCapture");
        Test.Map.PopContext();
        Confirm(Test.Map.QueryContext() == InputContextCategory::Editor, "pop restores Editor context");
    }

    // ⑥ Text entry suppresses editor shortcuts (Escape exempt by design — no editor Escape action exists).
    {
        Rig Test;
        Test.Map.ConfigureTextEntryActive(true);
        Test.Press(Key::F9);
        Test.Cycle();
        Confirm(!Test.Map.QueryAction("Editor.ToggleInspector").Pressed, "F9 suppressed during text entry");
        Test.Map.ConfigureTextEntryActive(false);
        Test.Release(Key::F9);
        Test.Press(Key::F9);
        Test.Cycle();
        Confirm(Test.Map.QueryAction("Editor.ToggleInspector").Pressed, "F9 fires after text entry ends");
    }

    // ⑦ Pointer axes require capture; displacement respects preferences.
    {
        Rig Test;
        Test.Map.PushContext(InputContextCategory::Simulation);
        Test.Substrate.InjectPointerPosition(100.0f, 100.0f);
        Test.Cycle();
        Test.Substrate.InjectPointerPosition(500.0f, 100.0f);
        Test.Cycle();
        Confirm(Near(Test.Map.QueryAxis("Simulation.LookX"), 0.0f), "look axis zero without pointer capture");
        Test.Map.ConfigurePointerCaptured(true);
        Test.Substrate.InjectPointerPosition(900.0f, 100.0f);
        Test.Cycle();
        Confirm(Test.Map.QueryAxis("Simulation.LookX") > 0.9f, "captured pointer sweep drives look axis (clamped)");
        Test.Cycle();
        Confirm(Near(Test.Map.QueryAxis("Simulation.LookX"), 0.0f), "look axis is a per-cycle displacement, not a level");
    }

    // ⑧ Gamepad axes: deadzone rescale + trigger.
    {
        Rig Test;
        Test.Map.PushContext(InputContextCategory::Simulation);
        GamepadReading Pad{};
        Pad.Connected    = true;
        Pad.LeftX        = 0.05f;    // Inside the 0.12 deadzone
        Pad.TriggerRight = 0.5f;
        Test.Substrate.InjectGamepad(Pad);
        Test.Cycle();
        Confirm(Near(Test.Map.QueryAxis("Simulation.Steer"), 0.0f), "stick inside deadzone reads zero");
        Confirm(Test.Map.QueryAxis("Simulation.Throttle") > 0.4f, "trigger feeds throttle");
        Pad.LeftX = -1.0f;
        Test.Substrate.InjectGamepad(Pad);
        Test.Cycle();
        Confirm(Near(Test.Map.QueryAxis("Simulation.Steer"), -1.0f), "full stick reads -1 after rescale");
    }

    // ⑨ Wheel accumulates within a cycle and resets after it.
    {
        Rig Test;
        Test.Map.PushContext(InputContextCategory::Simulation);
        Test.Substrate.InjectWheel(0.25f);
        Test.Substrate.InjectWheel(0.25f);
        Test.Cycle();
        Confirm(Near(Test.Map.QueryAxis("Simulation.Zoom"), 0.5f), "wheel injections accumulate per cycle");
        Test.Cycle();
        Confirm(Near(Test.Map.QueryAxis("Simulation.Zoom"), 0.0f), "wheel accumulator resets next cycle");
    }

    // ⑩ Conflicts are refused with a diagnostic; the first assignment keeps the control.
    {
        Rig         Test;
        std::string Diagnostic;
        const bool  Accepted = Test.Map.RegisterAssignment(
            KeyAssignment{"Editor.Rogue", InputContextCategory::Editor,
                          InputKeyChord{Key::F7, false, false, false, false}, PointerButtonCategory::None},
            Diagnostic);
        Confirm(!Accepted, "duplicate F7 in Editor context refused");
        Confirm(!Diagnostic.empty() && !Test.Map.QueryConflicts().empty(), "conflict recorded with diagnostic");
        Test.Press(Key::F7);
        Test.Cycle();
        Confirm(Test.Map.QueryAction("Editor.ToggleLogStream").Pressed, "original F7 owner still fires");
        Confirm(!Test.Map.QueryAction("Editor.Rogue").Pressed, "refused action never fires");

        // The same chord in the other context is a different control entirely.
        const bool CrossContext = Test.Map.RegisterAssignment(
            KeyAssignment{"Simulation.AlsoF7", InputContextCategory::Simulation,
                          InputKeyChord{Key::F7, false, false, false, false}, PointerButtonCategory::None},
            Diagnostic);
        Confirm(CrossContext, "same chord allowed in a different context");
    }

    // ⑪ Reassignment (press-to-assign) moves the chord and respects conflicts.
    {
        Rig         Test;
        std::string Diagnostic;
        Confirm(Test.Map.ReassignAction("Editor.ToggleLogStream", InputContextCategory::Editor,
                                        InputKeyChord{Key::L, true, false, false, false}, Diagnostic),
                "reassign ToggleLogStream to Ctrl+L");
        Test.Press(Key::L, InputModifiers{true, false, false, false});
        Test.Cycle();
        Confirm(Test.Map.QueryAction("Editor.ToggleLogStream").Pressed, "new chord fires after reassign");
        Confirm(!Test.Map.ReassignAction("Editor.ToggleOutliner", InputContextCategory::Editor,
                                         InputKeyChord{Key::L, true, false, false, false}, Diagnostic),
                "reassign onto an owned chord refused");
    }

    // ⑫ TOML round-trip: defaults encode, decode into a fresh map, readings match.
    {
        Rig               Source;
        std::string       Diagnostic;
        const auto        Path = std::filesystem::temp_directory_path() / "FrontierEditorInputValidation.toml";
        Confirm(InputMapCodec::Encode(Path, Source.Map, Diagnostic), "input map encodes to TOML");
        InputMap Restored;
        Confirm(InputMapCodec::Decode(Path, Restored, Diagnostic), "input map decodes from TOML");
        Confirm(Restored.QueryAssignments().size() == Source.Map.QueryAssignments().size(),
                "round-trip preserves action assignment count");
        Confirm(Restored.QueryAxisAssignments().size() == Source.Map.QueryAxisAssignments().size(),
                "round-trip preserves axis assignment count");
        InputSubstrate Substrate;
        Substrate.InjectKeyTransition(Key::F5, true, false);
        Substrate.BeginCycle();
        InputPreferences Preferences{};
        Restored.Integrate(Substrate, Preferences);
        Confirm(Restored.QueryAction("Editor.FocusSimulation").Pressed, "restored map resolves F5 identically");
        std::error_code Error;
        std::filesystem::remove(Path, Error);
    }

    std::printf("InputValidation: %d checks, %d failures\n", Checks, Failures);
    return Failures == 0 ? 0 : 1;
}
