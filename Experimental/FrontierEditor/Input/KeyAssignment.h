/*==========================================================================================================================================
                                                              KEYASSIGNMENT.H
==========================================================================================================================================*/
// 🧩 Named assignments between physical controls and application actions/axes, scoped by input context.

#pragma once

#include "Engine/Interface/ConfigurationStructure.h"

#include <cstdint>
#include <string>

namespace Frontier
{

//----------------------------------------------------------------------------------------------------------------------
//                                                    CONTEXTS AND SOURCES
//----------------------------------------------------------------------------------------------------------------------

// 📝 One input context is live at a time (InputMap keeps a stack). Editor assignments drive the workspace
//    chrome; Simulation assignments drive the captured viewport. A physical control may serve both
//    contexts without conflict because only the top context resolves.
enum class InputContextCategory : uint32_t
{
    Editor,
    Simulation,

    Count   // [-] - Sentinel: number of contexts, not a context itself
};

[[nodiscard]] constexpr const char* InputContextName(InputContextCategory Context) noexcept
{
    switch (Context)
    {
        case InputContextCategory::Editor:     return "Editor";
        case InputContextCategory::Simulation: return "Simulation";
        case InputContextCategory::Count:      break;
    }
    return "Editor";
}

[[nodiscard]] constexpr InputContextCategory DecodeInputContextName(const char* Name) noexcept
{
    if (Name == nullptr)
    {
        return InputContextCategory::Count;
    }
    const auto Equal = [](const char* Left, const char* Right) {
        while (*Left != '\0' && *Left == *Right)
        {
            ++Left;
            ++Right;
        }
        return *Left == *Right;
    };
    if (Equal(Name, "Editor"))     return InputContextCategory::Editor;
    if (Equal(Name, "Simulation")) return InputContextCategory::Simulation;
    return InputContextCategory::Count;
}

// Pointer buttons as assignment sources (keyboard chords reuse InputKeyChord).
enum class PointerButtonCategory : uint32_t
{
    None,
    Left,
    Right,
    Middle,
    SideOne,
    SideTwo,

    Count   // [-] - Sentinel
};

[[nodiscard]] constexpr const char* PointerButtonName(PointerButtonCategory Button) noexcept
{
    switch (Button)
    {
        case PointerButtonCategory::None:    return "None";
        case PointerButtonCategory::Left:    return "Left";
        case PointerButtonCategory::Right:   return "Right";
        case PointerButtonCategory::Middle:  return "Middle";
        case PointerButtonCategory::SideOne: return "SideOne";
        case PointerButtonCategory::SideTwo: return "SideTwo";
        case PointerButtonCategory::Count:   break;
    }
    return "None";
}

// Continuous sources an axis can read. KeyPair synthesizes -1/0/+1 from two keys; Pointer axes read the
// per-cycle pointer displacement (only while the pointer is captured); Wheel reads the per-cycle scroll
// accumulation; the rest read the polled gamepad snapshot.
enum class AxisSourceCategory : uint32_t
{
    None,
    KeyPair,
    PointerX,
    PointerY,
    WheelY,
    GamepadLeftX,
    GamepadLeftY,
    GamepadRightX,
    GamepadRightY,
    GamepadTriggerLeft,
    GamepadTriggerRight,

    Count   // [-] - Sentinel
};

[[nodiscard]] constexpr const char* AxisSourceName(AxisSourceCategory Source) noexcept
{
    switch (Source)
    {
        case AxisSourceCategory::None:               return "None";
        case AxisSourceCategory::KeyPair:            return "KeyPair";
        case AxisSourceCategory::PointerX:           return "PointerX";
        case AxisSourceCategory::PointerY:           return "PointerY";
        case AxisSourceCategory::WheelY:             return "WheelY";
        case AxisSourceCategory::GamepadLeftX:       return "GamepadLeftX";
        case AxisSourceCategory::GamepadLeftY:       return "GamepadLeftY";
        case AxisSourceCategory::GamepadRightX:      return "GamepadRightX";
        case AxisSourceCategory::GamepadRightY:      return "GamepadRightY";
        case AxisSourceCategory::GamepadTriggerLeft: return "GamepadTriggerLeft";
        case AxisSourceCategory::GamepadTriggerRight: return "GamepadTriggerRight";
        case AxisSourceCategory::Count:              break;
    }
    return "None";
}

[[nodiscard]] constexpr AxisSourceCategory DecodeAxisSourceName(const char* Name) noexcept
{
    if (Name == nullptr)
    {
        return AxisSourceCategory::Count;
    }
    for (uint32_t Index = 0; Index < static_cast<uint32_t>(AxisSourceCategory::Count); ++Index)
    {
        const char* Candidate = AxisSourceName(static_cast<AxisSourceCategory>(Index));
        const char* Left      = Name;
        const char* Right     = Candidate;
        while (*Left != '\0' && *Left == *Right)
        {
            ++Left;
            ++Right;
        }
        if (*Left == *Right)
        {
            return static_cast<AxisSourceCategory>(Index);
        }
    }
    return AxisSourceCategory::Count;
}

[[nodiscard]] constexpr PointerButtonCategory DecodePointerButtonName(const char* Name) noexcept
{
    if (Name == nullptr)
    {
        return PointerButtonCategory::Count;
    }
    for (uint32_t Index = 0; Index < static_cast<uint32_t>(PointerButtonCategory::Count); ++Index)
    {
        const char* Candidate = PointerButtonName(static_cast<PointerButtonCategory>(Index));
        const char* Left      = Name;
        const char* Right     = Candidate;
        while (*Left != '\0' && *Left == *Right)
        {
            ++Left;
            ++Right;
        }
        if (*Left == *Right)
        {
            return static_cast<PointerButtonCategory>(Index);
        }
    }
    return PointerButtonCategory::Count;
}

//----------------------------------------------------------------------------------------------------------------------
//                                                    ASSIGNMENT RECORDS
//----------------------------------------------------------------------------------------------------------------------

// One action assignment: a keyboard chord (Chord.Key set) or a pointer button. Several assignments may
// feed one action; the action reading is their union. Within one context, one physical control feeds at
// most one action — InputMap refuses later duplicates as conflicts.
struct KeyAssignment
{
    std::string           Action;                                                  // [text] - e.g. "Editor.ToggleLogStream"
    InputContextCategory  Context       = InputContextCategory::Editor;
    InputKeyChord         Chord         = {};                                      // [-] - Key None => pointer assignment
    PointerButtonCategory PointerButton = PointerButtonCategory::None;

    [[nodiscard]] bool operator==(const KeyAssignment&) const noexcept = default;
};

// One axis assignment. Several assignments may feed one axis; readings sum, then clamp to [-1, 1]
// (pointer/wheel contributions are displacements and stay unclamped-summed before the same clamp —
// see InputMap::Integrate; callers wanting raw displacement scale it back themselves).
struct AxisAssignment
{
    std::string          Axis;                                                     // [text] - e.g. "Simulation.Steer"
    InputContextCategory Context     = InputContextCategory::Editor;
    AxisSourceCategory   Source      = AxisSourceCategory::None;
    InputKeyCategory     NegativeKey = InputKeyCategory::None;                     // [-] - KeyPair only
    InputKeyCategory     PositiveKey = InputKeyCategory::None;                     // [-] - KeyPair only
    float                Scale       = 1.0f;                                       // [-] - Applied after the source reading
    float                Deadzone    = 0.12f;                                      // [0-1] - Gamepad sources only

    [[nodiscard]] bool operator==(const AxisAssignment&) const noexcept = default;
};

} // namespace Frontier
