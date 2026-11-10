/*==========================================================================================================================================
                                                             INPUTSUBSTRATE.CPP
==========================================================================================================================================*/
// 🧩 Raw device ingestion: queued key/button transitions and pointer motion folded into per-cycle levels and edges.

#include "Input/InputSubstrate.h"

namespace Frontier
{

//----------------------------------------------------------------------------------------------------------------------
//                                                         INGESTION
//----------------------------------------------------------------------------------------------------------------------

void InputSubstrate::InjectKeyTransition(InputKeyCategory Key, bool Down, bool Repeat)
{
    if (Key == InputKeyCategory::None || Key == InputKeyCategory::Count)
    {
        return;
    }
    PendingKeys.push_back(KeyTransition{Key, Down, Repeat});
}

void InputSubstrate::InjectPointerButtonTransition(PointerButtonCategory Button, bool Down)
{
    if (Button == PointerButtonCategory::None || Button == PointerButtonCategory::Count)
    {
        return;
    }
    PendingButtons.push_back(PointerButtonTransition{Button, Down});
}

void InputSubstrate::InjectPointerPosition(float X, float Y)
{
    if (HasPointerAnchor)
    {
        PendingDelta.X += X - PointerAnchor.X;
        PendingDelta.Y += Y - PointerAnchor.Y;
    }
    PointerAnchor    = PlanePoint{X, Y};
    HasPointerAnchor = true;
}

void InputSubstrate::InjectWheel(float DeltaY)
{
    PendingWheel += DeltaY;
}

void InputSubstrate::InjectModifiers(InputModifiers NewModifiers)
{
    Modifiers = NewModifiers;
}

void InputSubstrate::InjectGamepad(GamepadReading Reading)
{
    Gamepad = Reading;
}

void InputSubstrate::ResetPointerAnchor()
{
    HasPointerAnchor = false;
    PendingDelta     = {};
}

//----------------------------------------------------------------------------------------------------------------------
//                                                         CYCLE FOLD
//----------------------------------------------------------------------------------------------------------------------

void InputSubstrate::BeginCycle()
{
    KeyPressedFlags.fill(false);
    KeyReleasedFlags.fill(false);
    ButtonPressedFlags.fill(false);
    ButtonReleasedFlags.fill(false);
    FirstPressedKey.reset();

    // 📝 Transitions fold in arrival order; levels track the last transition, edges latch on any genuine
    //    level change. A repeat press while already down is not a change and produces no edge.
    for (const KeyTransition& Transition : PendingKeys)
    {
        const size_t Index = static_cast<size_t>(Transition.Key);
        if (Transition.Down)
        {
            if (!KeyDownFlags[Index] && !Transition.Repeat)
            {
                KeyPressedFlags[Index] = true;
                if (!FirstPressedKey.has_value())
                {
                    FirstPressedKey = Transition.Key;
                }
            }
            KeyDownFlags[Index] = true;
        }
        else
        {
            if (KeyDownFlags[Index])
            {
                KeyReleasedFlags[Index] = true;
            }
            KeyDownFlags[Index] = false;
        }
    }
    PendingKeys.clear();

    for (const PointerButtonTransition& Transition : PendingButtons)
    {
        const size_t Index = static_cast<size_t>(Transition.Button);
        if (Transition.Down)
        {
            if (!ButtonDownFlags[Index])
            {
                ButtonPressedFlags[Index] = true;
            }
            ButtonDownFlags[Index] = true;
        }
        else
        {
            if (ButtonDownFlags[Index])
            {
                ButtonReleasedFlags[Index] = true;
            }
            ButtonDownFlags[Index] = false;
        }
    }
    PendingButtons.clear();

    PointerPosition = PointerAnchor;
    PointerDelta    = PendingDelta;
    PendingDelta    = {};
    WheelDelta      = PendingWheel;
    PendingWheel    = 0.0f;
}

//----------------------------------------------------------------------------------------------------------------------
//                                                           QUERIES
//----------------------------------------------------------------------------------------------------------------------

bool InputSubstrate::QueryKeyDown(InputKeyCategory Key) const noexcept
{
    const size_t Index = static_cast<size_t>(Key);
    return Index < KeyCount && KeyDownFlags[Index];
}

bool InputSubstrate::QueryKeyPressed(InputKeyCategory Key) const noexcept
{
    const size_t Index = static_cast<size_t>(Key);
    return Index < KeyCount && KeyPressedFlags[Index];
}

bool InputSubstrate::QueryKeyReleased(InputKeyCategory Key) const noexcept
{
    const size_t Index = static_cast<size_t>(Key);
    return Index < KeyCount && KeyReleasedFlags[Index];
}

bool InputSubstrate::QueryPointerButtonDown(PointerButtonCategory Button) const noexcept
{
    const size_t Index = static_cast<size_t>(Button);
    return Index < ButtonCount && ButtonDownFlags[Index];
}

bool InputSubstrate::QueryPointerButtonPressed(PointerButtonCategory Button) const noexcept
{
    const size_t Index = static_cast<size_t>(Button);
    return Index < ButtonCount && ButtonPressedFlags[Index];
}

bool InputSubstrate::QueryPointerButtonReleased(PointerButtonCategory Button) const noexcept
{
    const size_t Index = static_cast<size_t>(Button);
    return Index < ButtonCount && ButtonReleasedFlags[Index];
}

} // namespace Frontier
