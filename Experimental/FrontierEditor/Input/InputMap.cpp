/*==========================================================================================================================================
                                                                INPUTMAP.CPP
==========================================================================================================================================*/
// 🧩 Action/axis resolution over the input substrate: context stack, chord matching, conflict refusal.

#include "Input/InputMap.h"

#include "Engine/Interface/ConfigurationStructure.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

//----------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//----------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool ModifiersMatch(const InputKeyChord& Chord, InputModifiers Modifiers) noexcept
{
    return Chord.Control == Modifiers.Control && Chord.Alt == Modifiers.Alt && Chord.Shift == Modifiers.Shift &&
           Chord.Super == Modifiers.Super;
}

[[nodiscard]] float ApplyDeadzone(float Reading, float Deadzone) noexcept
{
    const float Magnitude = std::fabs(Reading);
    if (Magnitude <= Deadzone)
    {
        return 0.0f;
    }
    const float Rescaled = (Magnitude - Deadzone) / std::max(1.0f - Deadzone, 0.001f);
    return (Reading < 0.0f ? -Rescaled : Rescaled);
}

[[nodiscard]] InputKeyChord Chord(InputKeyCategory Key, bool Control = false, bool Alt = false, bool Shift = false,
                                  bool Super = false) noexcept
{
    InputKeyChord Result;
    Result.Key     = Key;
    Result.Control = Control;
    Result.Alt     = Alt;
    Result.Shift   = Shift;
    Result.Super   = Super;
    return Result;
}

} // namespace

//----------------------------------------------------------------------------------------------------------------------
//                                                       REGISTRATION
//----------------------------------------------------------------------------------------------------------------------

const KeyAssignment* InputMap::FindChordConflict(InputContextCategory Context, const InputKeyChord& Candidate,
                                                 std::string_view ExcludingAction) const
{
    if (Candidate.Key == InputKeyCategory::None)
    {
        return nullptr;
    }
    for (const KeyAssignment& Existing : Assignments)
    {
        if (Existing.Context == Context && Existing.Action != ExcludingAction && Existing.Chord == Candidate)
        {
            return &Existing;
        }
    }
    return nullptr;
}

const KeyAssignment* InputMap::FindButtonConflict(InputContextCategory Context, PointerButtonCategory Button,
                                                  std::string_view ExcludingAction) const
{
    if (Button == PointerButtonCategory::None)
    {
        return nullptr;
    }
    for (const KeyAssignment& Existing : Assignments)
    {
        if (Existing.Context == Context && Existing.Action != ExcludingAction &&
            Existing.Chord.Key == InputKeyCategory::None && Existing.PointerButton == Button)
        {
            return &Existing;
        }
    }
    return nullptr;
}

bool InputMap::RegisterAssignment(KeyAssignment Assignment, std::string& Diagnostic)
{
    if (Assignment.Action.empty())
    {
        Diagnostic = "Assignment refused: empty action name.";
        return false;
    }
    if (Assignment.Chord.Key == InputKeyCategory::None && Assignment.PointerButton == PointerButtonCategory::None)
    {
        Diagnostic = "Assignment refused for '" + Assignment.Action + "': neither a chord nor a pointer button.";
        return false;
    }
    const KeyAssignment* Conflict = FindChordConflict(Assignment.Context, Assignment.Chord, Assignment.Action);
    if (Conflict == nullptr)
    {
        Conflict = FindButtonConflict(Assignment.Context, Assignment.PointerButton, Assignment.Action);
    }
    if (Conflict != nullptr)
    {
        Diagnostic = "Conflict in " + std::string(InputContextName(Assignment.Context)) + " context: '" +
                     Assignment.Action + "' refused, control already feeds '" + Conflict->Action + "'.";
        Conflicts.push_back(Diagnostic);
        return false;
    }
    Assignments.push_back(std::move(Assignment));
    Diagnostic.clear();
    return true;
}

bool InputMap::RegisterAxisAssignment(AxisAssignment Assignment, std::string& Diagnostic)
{
    if (Assignment.Axis.empty() || Assignment.Source == AxisSourceCategory::None)
    {
        Diagnostic = "Axis assignment refused: empty axis name or no source.";
        return false;
    }
    if (Assignment.Source == AxisSourceCategory::KeyPair &&
        (Assignment.NegativeKey == InputKeyCategory::None || Assignment.PositiveKey == InputKeyCategory::None ||
         Assignment.NegativeKey == Assignment.PositiveKey))
    {
        Diagnostic = "Axis assignment refused for '" + Assignment.Axis + "': KeyPair needs two distinct keys.";
        return false;
    }
    // 📝 Axes sum, so a duplicate source is redundant rather than conflicting; refuse exact duplicates
    //    to keep authored maps honest.
    for (const AxisAssignment& Existing : AxisAssignments)
    {
        if (Existing == Assignment)
        {
            Diagnostic = "Axis assignment refused for '" + Assignment.Axis + "': exact duplicate.";
            Conflicts.push_back(Diagnostic);
            return false;
        }
    }
    AxisAssignments.push_back(std::move(Assignment));
    Diagnostic.clear();
    return true;
}

bool InputMap::ReassignAction(std::string_view Action, InputContextCategory Context, InputKeyChord NewChord,
                              std::string& Diagnostic)
{
    KeyAssignment* Target = nullptr;
    for (KeyAssignment& Existing : Assignments)
    {
        if (Existing.Action == Action && Existing.Context == Context && Existing.Chord.Key != InputKeyCategory::None)
        {
            Target = &Existing;
            break;
        }
    }
    if (Target == nullptr)
    {
        Diagnostic = "Reassignment refused: no chord assignment for '" + std::string(Action) + "'.";
        return false;
    }
    if (const KeyAssignment* Conflict = FindChordConflict(Context, NewChord, Action); Conflict != nullptr)
    {
        Diagnostic = "Reassignment refused: control already feeds '" + Conflict->Action + "' in this context.";
        Conflicts.push_back(Diagnostic);
        return false;
    }
    Target->Chord = NewChord;
    Diagnostic.clear();
    return true;
}

void InputMap::ConfigureDefaults()
{
    std::string        Diagnostic;
    const auto         Action = [&](const char* Name, InputContextCategory Context, InputKeyChord KeyChord) {
        (void)RegisterAssignment(KeyAssignment{Name, Context, KeyChord, PointerButtonCategory::None}, Diagnostic);
    };
    const auto Axis = [&](AxisAssignment Assignment) { (void)RegisterAxisAssignment(std::move(Assignment), Diagnostic); };
    using Key       = InputKeyCategory;
    using Context   = InputContextCategory;
    using Source    = AxisSourceCategory;

    // Editor context: panel visibility + workspace commands. F-keys avoid colliding with the Control
    // Centre's own InputPreferences shortcuts (ControlCentre/Typography/Theme/DismissPanel).
    Action("Editor.FocusSimulation", Context::Editor, Chord(Key::F5));
    Action("Editor.ToggleInputPanel", Context::Editor, Chord(Key::F6));
    Action("Editor.ToggleLogStream", Context::Editor, Chord(Key::F7));
    Action("Editor.ToggleOutliner", Context::Editor, Chord(Key::F8));
    Action("Editor.ToggleInspector", Context::Editor, Chord(Key::F9));
    Action("Editor.ResetLayout", Context::Editor, Chord(Key::R, true, false, true));

    // Simulation context: driving controls for the captured viewport.
    Action("Simulation.Handbrake", Context::Simulation, Chord(Key::Space));
    Action("Simulation.ResetVehicle", Context::Simulation, Chord(Key::R));
    Action("Simulation.QuitCapture", Context::Simulation, Chord(Key::Escape));

    Axis(AxisAssignment{"Simulation.Steer", Context::Simulation, Source::KeyPair, Key::A, Key::D, 1.0f, 0.12f});
    Axis(AxisAssignment{"Simulation.Steer", Context::Simulation, Source::GamepadLeftX, Key::None, Key::None, 1.0f, 0.12f});
    Axis(AxisAssignment{"Simulation.Throttle", Context::Simulation, Source::KeyPair, Key::S, Key::W, 1.0f, 0.12f});
    Axis(AxisAssignment{"Simulation.Throttle", Context::Simulation, Source::GamepadTriggerRight, Key::None, Key::None, 1.0f, 0.05f});
    Axis(AxisAssignment{"Simulation.Brake", Context::Simulation, Source::GamepadTriggerLeft, Key::None, Key::None, 1.0f, 0.05f});
    Axis(AxisAssignment{"Simulation.LookX", Context::Simulation, Source::PointerX, Key::None, Key::None, 0.0025f, 0.0f});
    Axis(AxisAssignment{"Simulation.LookY", Context::Simulation, Source::PointerY, Key::None, Key::None, 0.0025f, 0.0f});
    Axis(AxisAssignment{"Simulation.Zoom", Context::Simulation, Source::WheelY, Key::None, Key::None, 1.0f, 0.0f});
}

//----------------------------------------------------------------------------------------------------------------------
//                                                       CONTEXT STACK
//----------------------------------------------------------------------------------------------------------------------

void InputMap::PushContext(InputContextCategory Context)
{
    if (Context != InputContextCategory::Count && Context != QueryContext())
    {
        ContextStack.push_back(Context);
    }
}

void InputMap::PopContext()
{
    if (ContextStack.size() > 1)
    {
        ContextStack.pop_back();
    }
}

InputContextCategory InputMap::QueryContext() const noexcept
{
    return ContextStack.empty() ? InputContextCategory::Editor : ContextStack.back();
}

//----------------------------------------------------------------------------------------------------------------------
//                                                         INTEGRATION
//----------------------------------------------------------------------------------------------------------------------

void InputMap::Integrate(const InputSubstrate& Substrate, const InputPreferences& Preferences)
{
    ActionReadings.clear();
    AxisReadings.clear();

    const InputContextCategory Live = QueryContext();
    const InputModifiers       Modifiers = Substrate.QueryModifiers();
    // 📝 Text entry suppresses Editor actions — but never Escape, which hosts rely on to leave capture
    //    and close dialogs even while a text field holds keyboard focus.
    const bool EditorSuppressed = TextEntryActive && Live == InputContextCategory::Editor;

    for (const KeyAssignment& Assignment : Assignments)
    {
        if (Assignment.Context != Live)
        {
            continue;
        }
        if (EditorSuppressed && Assignment.Chord.Key != InputKeyCategory::Escape)
        {
            continue;
        }
        ActionReading& Reading = ActionReadings[Assignment.Action];
        if (Assignment.Chord.Key != InputKeyCategory::None)
        {
            if (!ModifiersMatch(Assignment.Chord, Modifiers))
            {
                continue;
            }
            Reading.Down     = Reading.Down || Substrate.QueryKeyDown(Assignment.Chord.Key);
            Reading.Pressed  = Reading.Pressed || Substrate.QueryKeyPressed(Assignment.Chord.Key);
            Reading.Released = Reading.Released || Substrate.QueryKeyReleased(Assignment.Chord.Key);
        }
        else
        {
            Reading.Down     = Reading.Down || Substrate.QueryPointerButtonDown(Assignment.PointerButton);
            Reading.Pressed  = Reading.Pressed || Substrate.QueryPointerButtonPressed(Assignment.PointerButton);
            Reading.Released = Reading.Released || Substrate.QueryPointerButtonReleased(Assignment.PointerButton);
        }
    }

    // Pointer displacement passes through the shared preferences (sensitivity + pitch inversion).
    const PlanePoint ResponsiveDelta = Preferences.EvaluatePointerDelta(Substrate.QueryPointerDelta());
    const GamepadReading& Gamepad    = Substrate.QueryGamepad();

    for (const AxisAssignment& Assignment : AxisAssignments)
    {
        if (Assignment.Context != Live)
        {
            continue;
        }
        float Reading = 0.0f;
        switch (Assignment.Source)
        {
            case AxisSourceCategory::KeyPair:
                Reading = (Substrate.QueryKeyDown(Assignment.PositiveKey) ? 1.0f : 0.0f) -
                          (Substrate.QueryKeyDown(Assignment.NegativeKey) ? 1.0f : 0.0f);
                break;
            case AxisSourceCategory::PointerX:
                Reading = PointerCaptured ? ResponsiveDelta.X : 0.0f;
                break;
            case AxisSourceCategory::PointerY:
                Reading = PointerCaptured ? ResponsiveDelta.Y : 0.0f;
                break;
            case AxisSourceCategory::WheelY:
                Reading = Substrate.QueryWheelDelta();
                break;
            case AxisSourceCategory::GamepadLeftX:       Reading = ApplyDeadzone(Gamepad.LeftX, Assignment.Deadzone); break;
            case AxisSourceCategory::GamepadLeftY:       Reading = ApplyDeadzone(Gamepad.LeftY, Assignment.Deadzone); break;
            case AxisSourceCategory::GamepadRightX:      Reading = ApplyDeadzone(Gamepad.RightX, Assignment.Deadzone); break;
            case AxisSourceCategory::GamepadRightY:      Reading = ApplyDeadzone(Gamepad.RightY, Assignment.Deadzone); break;
            case AxisSourceCategory::GamepadTriggerLeft: Reading = ApplyDeadzone(Gamepad.TriggerLeft, Assignment.Deadzone); break;
            case AxisSourceCategory::GamepadTriggerRight: Reading = ApplyDeadzone(Gamepad.TriggerRight, Assignment.Deadzone); break;
            case AxisSourceCategory::None:
            case AxisSourceCategory::Count:
                break;
        }
        AxisReadings[Assignment.Axis] += Reading * Assignment.Scale;
    }
    for (auto& Entry : AxisReadings)
    {
        Entry.second = std::clamp(Entry.second, -1.0f, 1.0f);
    }
}

ActionReading InputMap::QueryAction(std::string_view Action) const
{
    const auto Found = ActionReadings.find(std::string(Action));
    return Found == ActionReadings.end() ? ActionReading{} : Found->second;
}

float InputMap::QueryAxis(std::string_view Axis) const
{
    const auto Found = AxisReadings.find(std::string(Axis));
    return Found == AxisReadings.end() ? 0.0f : Found->second;
}

} // namespace Frontier
