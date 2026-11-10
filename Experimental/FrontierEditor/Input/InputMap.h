/*==========================================================================================================================================
                                                                 INPUTMAP.H
==========================================================================================================================================*/
// 🧩 Action/axis resolution over the input substrate: context stack, chord matching, conflict refusal.

#pragma once

#include "Input/InputSubstrate.h"
#include "Input/KeyAssignment.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Frontier
{

struct InputPreferences;

//----------------------------------------------------------------------------------------------------------------------
//                                                      ACTION READING
//----------------------------------------------------------------------------------------------------------------------

// One action's folded reading for the current cycle (union over its assignments in the live context).
struct ActionReading
{
    bool Down     = false;   // [-] - Held at cycle fold
    bool Pressed  = false;   // [-] - Press edge this cycle
    bool Released = false;   // [-] - Release edge this cycle
};

//----------------------------------------------------------------------------------------------------------------------
//                                                         INPUT MAP
//----------------------------------------------------------------------------------------------------------------------

// 📝 The map owns assignments and the context stack; the substrate owns devices. Integrate() once per
//    cycle (after InputSubstrate::BeginCycle) folds every assignment of the live context into readings
//    that stay queryable until the next Integrate. Rules that make it robust enough to share between the
//    editor and simulation hosts:
//      ① Only the stack-top context resolves — the same key can drive an editor command and a
//        simulation control without ever firing both.
//      ② Text entry suppresses the Editor context entirely (the host sets it from ImGui WantTextInput),
//        so typing "r" in a filter box can never trigger a shortcut; Escape still resolves so hosts
//        can always leave capture.
//      ③ Pointer-displacement axes read zero unless the pointer is captured (host hides the cursor for
//        simulation look); editor mouse motion can never leak into look axes.
//      ④ Conflicts are refused at registration with a diagnostic naming both actions, and the first
//        assignment keeps the control — no silent shadowing, ever.
//      ⑤ Pointer response (sensitivity, pitch inversion) comes from the shared InputPreferences, the
//        same values the Control Centre edits — one source of pointer truth for every application.
class InputMap
{
  public:
    // Registration -----------------------------------------------------------------------------------
    bool RegisterAssignment(KeyAssignment Assignment, std::string& Diagnostic);
    bool RegisterAxisAssignment(AxisAssignment Assignment, std::string& Diagnostic);
    void ConfigureDefaults();   // Editor + Simulation defaults (see InputMap.cpp); conflicts logged, not fatal

    // Reassign one action's chord in a context (press-to-assign capture). Refuses on conflict.
    bool ReassignAction(std::string_view Action, InputContextCategory Context, InputKeyChord Chord,
                        std::string& Diagnostic);

    // Context stack ----------------------------------------------------------------------------------
    void                   PushContext(InputContextCategory Context);
    void                   PopContext();
    [[nodiscard]] InputContextCategory QueryContext() const noexcept;
    void                   ConfigureTextEntryActive(bool Active) noexcept { TextEntryActive = Active; }
    void                   ConfigurePointerCaptured(bool Captured) noexcept { PointerCaptured = Captured; }
    [[nodiscard]] bool     QueryTextEntryActive() const noexcept { return TextEntryActive; }
    [[nodiscard]] bool     QueryPointerCaptured() const noexcept { return PointerCaptured; }

    // Cycle fold + queries ---------------------------------------------------------------------------
    void Integrate(const InputSubstrate& Substrate, const InputPreferences& Preferences);

    [[nodiscard]] ActionReading QueryAction(std::string_view Action) const;
    [[nodiscard]] float         QueryAxis(std::string_view Axis) const;   // [-1..1] after summation+clamp

    [[nodiscard]] const std::vector<KeyAssignment>&  QueryAssignments() const noexcept { return Assignments; }
    [[nodiscard]] const std::vector<AxisAssignment>& QueryAxisAssignments() const noexcept { return AxisAssignments; }
    [[nodiscard]] const std::vector<std::string>&    QueryConflicts() const noexcept { return Conflicts; }

  private:
    [[nodiscard]] const KeyAssignment* FindChordConflict(InputContextCategory Context, const InputKeyChord& Chord,
                                                         std::string_view ExcludingAction) const;
    [[nodiscard]] const KeyAssignment* FindButtonConflict(InputContextCategory Context,
                                                          PointerButtonCategory   Button,
                                                          std::string_view        ExcludingAction) const;

    std::vector<KeyAssignment>                     Assignments;
    std::vector<AxisAssignment>                    AxisAssignments;
    std::vector<std::string>                       Conflicts;
    std::vector<InputContextCategory>              ContextStack{InputContextCategory::Editor};
    std::unordered_map<std::string, ActionReading> ActionReadings;
    std::unordered_map<std::string, float>         AxisReadings;
    bool                                           TextEntryActive = false;
    bool                                           PointerCaptured = false;
};

} // namespace Frontier
