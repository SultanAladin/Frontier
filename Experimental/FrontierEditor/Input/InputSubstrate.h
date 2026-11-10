/*==========================================================================================================================================
                                                              INPUTSUBSTRATE.H
==========================================================================================================================================*/
// 🧩 Raw device ingestion: queued key/button transitions and pointer motion folded into per-cycle levels and edges.

#pragma once

#include "Engine/Interface/PixelSpace.h"
#include "Input/KeyAssignment.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace Frontier
{

//----------------------------------------------------------------------------------------------------------------------
//                                                    DEVICE READINGS
//----------------------------------------------------------------------------------------------------------------------

struct InputModifiers
{
    bool Control = false;   // [-]
    bool Alt     = false;   // [-]
    bool Shift   = false;   // [-]
    bool Super   = false;   // [-]

    [[nodiscard]] bool operator==(const InputModifiers&) const noexcept = default;
};

// Polled gamepad snapshot (the host polls; the substrate only stores). Axes are pre-deadzone [-1, 1],
// triggers [0, 1]. Buttons is a bit field in GLFW gamepad order (A=bit0, B=bit1, X=bit2, Y=bit3, ...).
struct GamepadReading
{
    float    LeftX        = 0.0f;   // [-1..1]
    float    LeftY        = 0.0f;   // [-1..1]
    float    RightX       = 0.0f;   // [-1..1]
    float    RightY       = 0.0f;   // [-1..1]
    float    TriggerLeft  = 0.0f;   // [0..1]
    float    TriggerRight = 0.0f;   // [0..1]
    uint32_t Buttons      = 0;      // [bits]
    bool     Connected    = false;  // [-]
};

//----------------------------------------------------------------------------------------------------------------------
//                                                    INPUT SUBSTRATE
//----------------------------------------------------------------------------------------------------------------------

// 📝 The substrate knows devices, not actions. Hosts inject transitions as they arrive (GLFW callbacks,
//    synthetic test events); BeginCycle() folds the queue into the levels/edges every consumer reads for
//    that cycle. Edges therefore survive exactly one cycle, key repeat never manufactures a press, and a
//    press+release inside one cycle reports both edges with the level down. Single-threaded by contract:
//    inject from the thread that calls BeginCycle (GLFW callbacks already satisfy this).
class InputSubstrate
{
  public:
    // Ingestion --------------------------------------------------------------------------------------
    void InjectKeyTransition(InputKeyCategory Key, bool Down, bool Repeat);
    void InjectPointerButtonTransition(PointerButtonCategory Button, bool Down);
    void InjectPointerPosition(float X, float Y);      // [px] - Accumulates displacement since last cycle
    void InjectWheel(float DeltaY);                    // [lines] - Accumulates until BeginCycle
    void InjectModifiers(InputModifiers Modifiers);
    void InjectGamepad(GamepadReading Reading);
    void ResetPointerAnchor();                         // Next position sets the anchor without a delta spike

    // Cycle fold -------------------------------------------------------------------------------------
    void BeginCycle();

    // Queries (valid after BeginCycle, until the next BeginCycle) ------------------------------------
    [[nodiscard]] bool QueryKeyDown(InputKeyCategory Key) const noexcept;
    [[nodiscard]] bool QueryKeyPressed(InputKeyCategory Key) const noexcept;    // [-] - Non-repeat press edge
    [[nodiscard]] bool QueryKeyReleased(InputKeyCategory Key) const noexcept;   // [-]
    [[nodiscard]] bool QueryPointerButtonDown(PointerButtonCategory Button) const noexcept;
    [[nodiscard]] bool QueryPointerButtonPressed(PointerButtonCategory Button) const noexcept;
    [[nodiscard]] bool QueryPointerButtonReleased(PointerButtonCategory Button) const noexcept;

    [[nodiscard]] InputModifiers        QueryModifiers() const noexcept { return Modifiers; }
    [[nodiscard]] PlanePoint            QueryPointerPosition() const noexcept { return PointerPosition; }   // [px]
    [[nodiscard]] PlanePoint            QueryPointerDelta() const noexcept { return PointerDelta; }         // [px]
    [[nodiscard]] float                 QueryWheelDelta() const noexcept { return WheelDelta; }             // [lines]
    [[nodiscard]] const GamepadReading& QueryGamepad() const noexcept { return Gamepad; }

    // First non-repeat key press this cycle (drives press-to-assign capture). nullopt when none.
    [[nodiscard]] std::optional<InputKeyCategory> QueryFirstPressedKey() const noexcept { return FirstPressedKey; }

  private:
    struct KeyTransition
    {
        InputKeyCategory Key    = InputKeyCategory::None;
        bool             Down   = false;   // [-]
        bool             Repeat = false;   // [-]
    };
    struct PointerButtonTransition
    {
        PointerButtonCategory Button = PointerButtonCategory::None;
        bool                  Down   = false;   // [-]
    };

    static constexpr size_t KeyCount    = static_cast<size_t>(InputKeyCategory::Count);
    static constexpr size_t ButtonCount = static_cast<size_t>(PointerButtonCategory::Count);

    std::vector<KeyTransition>           PendingKeys;
    std::vector<PointerButtonTransition> PendingButtons;
    std::array<bool, KeyCount>           KeyDownFlags{};
    std::array<bool, KeyCount>           KeyPressedFlags{};
    std::array<bool, KeyCount>           KeyReleasedFlags{};
    std::array<bool, ButtonCount>        ButtonDownFlags{};
    std::array<bool, ButtonCount>        ButtonPressedFlags{};
    std::array<bool, ButtonCount>        ButtonReleasedFlags{};

    InputModifiers                      Modifiers{};
    PlanePoint                          PointerPosition{};    // [px]
    PlanePoint                          PointerAnchor{};      // [px] - Last injected position
    PlanePoint                          PointerDelta{};       // [px] - Folded at BeginCycle
    PlanePoint                          PendingDelta{};       // [px] - Accumulating between cycles
    bool                                HasPointerAnchor = false;
    float                               WheelDelta       = 0.0f;   // [lines]
    float                               PendingWheel     = 0.0f;   // [lines]
    GamepadReading                      Gamepad{};
    std::optional<InputKeyCategory>     FirstPressedKey;
};

} // namespace Frontier
