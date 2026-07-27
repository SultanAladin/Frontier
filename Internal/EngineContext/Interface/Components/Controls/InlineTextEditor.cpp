/*==============================================================================================================================================
                                                            INLINETEXTEDITOR.CPP
==============================================================================================================================================*/
// 🧩 The shared foreground-draw-list caret editor (see InlineTextEditor.h). Extracted verbatim in behaviour from the workspace dock's former
//    hand-rolled rename so every renamable overlay label (dock tabs, floating-window titles, panel-box headers) edits through one definition:
//    printable chars off the ImGui input queue, Backspace deletes, Enter commits, Escape cancels, a blinking caret trails the live text.

#include "InlineTextEditor.h"

#include <cmath>
#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void BeginInlineTextEdit(InlineTextEditState& State, uint32_t Identifier, const char* InitialText)
{
    if (Identifier == 0)
        return;
    State.TargetIdentifier = Identifier;
    const char* Source = InitialText != nullptr ? InitialText : "";
    std::strncpy(State.Buffer, Source, sizeof(State.Buffer) - 1);
    State.Buffer[sizeof(State.Buffer) - 1] = '\0';
    State.Length = (int)std::strlen(State.Buffer);
}


bool InlineTextEditActive(const InlineTextEditState& State, uint32_t Identifier)
{
    return State.TargetIdentifier != 0 && State.TargetIdentifier == Identifier;
}


bool ResolveInlineTextEdit(InlineTextEditState& State, char* OutTitle, int OutCapacity, const char* FallbackText)
{
    if (State.TargetIdentifier == 0)
        return false;

    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        State.TargetIdentifier = 0;
        return true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
    {
        if (OutTitle != nullptr && OutCapacity > 0)
        {
            const char* Source = State.Length > 0 ? State.Buffer : (FallbackText != nullptr ? FallbackText : "Untitled");
            std::strncpy(OutTitle, Source, (size_t)OutCapacity - 1);
            OutTitle[OutCapacity - 1] = '\0';
        }
        State.TargetIdentifier = 0;
        return true;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && State.Length > 0)
        State.Buffer[--State.Length] = '\0';

    ImGuiIO& Io = ImGui::GetIO();
    for (int Index = 0; Index < Io.InputQueueCharacters.Size; ++Index)
    {
        ImWchar Char = Io.InputQueueCharacters[Index];
        if (Char < 32 || Char >= 127)   // printable ASCII only (mirrors the former dock editor)
            continue;
        if (State.Length >= (int)sizeof(State.Buffer) - 1)
            break;
        State.Buffer[State.Length++] = (char)Char;
        State.Buffer[State.Length]   = '\0';
    }
    return false;
}


void CommitInlineTextEdit(InlineTextEditState& State, char* OutTitle, int OutCapacity, const char* FallbackText)
{
    if (State.TargetIdentifier == 0)
        return;
    if (OutTitle != nullptr && OutCapacity > 0)
    {
        const char* Source = State.Length > 0 ? State.Buffer : (FallbackText != nullptr ? FallbackText : "Untitled");
        std::strncpy(OutTitle, Source, (size_t)OutCapacity - 1);
        OutTitle[OutCapacity - 1] = '\0';
    }
    State.TargetIdentifier = 0;
}


void PaintInlineTextEdit(ImDrawList* DrawList, const InlineTextEditState& State, uint32_t Identifier, const char* Title,
                         ImVec2 TextPos, float ClipRight, ImU32 Color)
{
    DrawList->PushClipRect(ImVec2(TextPos.x - 1.0f, TextPos.y - 2.0f), ImVec2(ClipRight, TextPos.y + 18.0f), true);
    if (InlineTextEditActive(State, Identifier))
    {
        DrawList->AddText(TextPos, Color, State.Buffer);
        const float CaretX = TextPos.x + ImGui::CalcTextSize(State.Buffer).x + 1.0f;
        const float CaretH = ImGui::GetTextLineHeight();
        if (std::fmod((float)ImGui::GetTime(), 1.0f) < 0.5f)
            DrawList->AddLine(ImVec2(CaretX, TextPos.y + 1.0f), ImVec2(CaretX, TextPos.y + CaretH - 1.0f), Color, 1.0f);
    }
    else
    {
        DrawList->AddText(TextPos, Color, Title);
    }
    DrawList->PopClipRect();
}

}   // namespace Frontier
