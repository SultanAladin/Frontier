//========================================================================================================================
//                                                SCENEOBJECTINSPECTOR.CPP
//========================================================================================================================
// 🧩 ControlKit drawing for the flat SceneObject list. Numeric rows use a value pill and a slider; colour uses preset
//    swatches; choice uses the ControlKit dropdown. Every widget writes through the store, which applies the rules.
//========================================================================================================================

#include "SceneObjectInspector.h"

#include <algorithm>
#include <cstdio>

namespace Frontier {

namespace {

constexpr float CardRadius   = 18.0f;
constexpr float HeadingHeight = 44.0f;   // title line + description line
constexpr float SwatchStep   = 40.0f;    // 32 px swatch + 8 px gap

// Text for the pill: numbers use the unit's own precision; a clamped value never shows more digits than its range needs.
void FormatNumber(float Value, const SceneObjectPropertyDescriptor& D, char* Buffer, size_t Size) noexcept
{
    const bool Whole = D.Kind == SceneObjectPropertyKind::Intensity || D.Kind == SceneObjectPropertyKind::Angle;
    std::snprintf(Buffer, Size, Whole ? "%.0f" : "%.2f", static_cast<double>(Value));
}

size_t RowCountOf(const SceneObjectPartEntry& Part) noexcept
{
    return SceneObjectPropertiesOf(Part.Class).Count;
}

}   // namespace

SceneObjectColour SceneObjectInspector::ColourPreset(uint32_t Index) noexcept
{
    // 📝 Warm white (the template default), neutral white, cool white, high-pressure sodium, and a daylight blue.
    static constexpr SceneObjectColour Presets[ColourPresetCount] = {
        { 1.0f, 217.0f / 255.0f, 160.0f / 255.0f },
        { 1.0f, 1.0f, 1.0f },
        { 201.0f / 255.0f, 230.0f / 255.0f, 1.0f },
        { 1.0f, 179.0f / 255.0f, 71.0f / 255.0f },
        { 0.7f, 0.85f, 1.0f },
    };
    return Presets[std::min(Index, ColourPresetCount - 1u)];
}

float SceneObjectInspector::DrawStore(PixelSpace& Surface, float X, float Y, float Width, SceneObjectPartStore& Store,
                                      SceneObjectInspectorState& State, const ControlPointer& Pointer, float Opacity) noexcept
{
    const ColorQuad TitleInk = ControlKit::Palette().Text;
    const ColorQuad MutedInk = ControlKit::Palette().TextDim;
    const float     Gap      = ControlKitTokens::RowGap;
    float           Cursor   = Y;

    // While a dropdown is open, the rest of the inspector does not take input.
    ControlPointer Local = Pointer;
    if (State.OpenChoicePart >= 0)
        Local.Enabled = false;

    const auto& Parts = Store.Parts();
    for (size_t PartIndex = 0u; PartIndex < Parts.size(); ++PartIndex)
    {
        const SceneObjectPartEntry& Part = Parts[PartIndex];
        const SceneObjectPropertySpan Span = SceneObjectPropertiesOf(Part.Class);

        // Card height is known up front: heading, one row per property, and padding on both sides.
        const float CardHeight = ControlKit::SectionPadding * 2.0f + HeadingHeight +
                                 static_cast<float>(RowCountOf(Part)) * (ControlKitTokens::ControlHeight + Gap) - Gap;
        const PlaneExtent Content = ControlKit::SectionCard(Surface, Spanning(X, Cursor, Width, CardHeight), CardRadius, Opacity);

        char Title[96];
        std::snprintf(Title, sizeof(Title), "%s", SceneObjectPartLabel(Part.Class));
        ControlKit::SectionHeading(Surface, Content.MinimumX, Content.MinimumY, Content.Width(), Title,
                                   SceneObjectPartClassName(Part.Class), TitleInk, MutedInk, Opacity);

        float RowY = Content.MinimumY + HeadingHeight;
        for (uint32_t V = 0u; V < Span.Count; ++V)
        {
            const SceneObjectPropertyDescriptor& D = Span.First[V];
            const SceneObjectPropertyValue& Value  = Part.Values[V];
            const PlaneExtent Control = ControlKit::ControlRow(Surface, Content.MinimumX, RowY, Content.Width(), D.Name,
                                                               TitleInk, Opacity);

            if (IsNumericKind(D.Kind))
            {
                char Number[24];
                FormatNumber(Value.Number, D, Number, sizeof(Number));
                const float PillWidth = ControlKit::PropertyPillWidth;
                ControlKit::ValuePill(Surface, Control.MinimumX, Control.MinimumY, Number, D.Unit, Opacity,
                                      PillWidth, ControlKit::PropertyPillUnitWidth);
                const PlaneExtent Track = Spanning(Control.MinimumX + PillWidth + Gap, Control.MinimumY,
                                                   Control.Width() - PillWidth - Gap, ControlKitTokens::ControlHeight);
                float Next = Value.Number;
                const bool Dragging = State.DraggingPart == static_cast<int32_t>(PartIndex) && State.DraggingProperty == V;
                const ControlHit Hit = ControlKit::Slider(Surface, Track, D.Minimum, D.Maximum, Value.Number, Dragging,
                                                          Local, Next, false, false, Opacity);
                if (Hit.Pressed)
                {
                    State.DraggingPart     = static_cast<int32_t>(PartIndex);
                    State.DraggingProperty = V;
                }
                if (Dragging && Next != Value.Number)
                    Store.SetNumber(PartIndex, D.Name, Next);
            }
            else if (D.Kind == SceneObjectPropertyKind::Colour)
            {
                for (uint32_t P = 0u; P < SceneObjectInspector::ColourPresetCount; ++P)
                {
                    const SceneObjectColour Preset = SceneObjectInspector::ColourPreset(P);
                    const float Cx = Control.MinimumX + 16.0f + static_cast<float>(P) * SwatchStep;
                    const float Cy = Control.MinimumY + ControlKitTokens::ControlHeight * 0.5f;
                    const bool Selected = Value.Colour.Red == Preset.Red && Value.Colour.Green == Preset.Green &&
                                          Value.Colour.Blue == Preset.Blue;
                    const ColorQuad Ink{ Preset.Red, Preset.Green, Preset.Blue, 1.0f };
                    const ControlHit Hit = ControlKit::Swatch(Surface, Cx, Cy, Ink, Selected, false, Local, Opacity);
                    if (Hit.Clicked)
                        Store.SetColour(PartIndex, D.Name, Preset);
                }
            }
            else if (D.Kind == SceneObjectPropertyKind::Choice)
            {
                const uint32_t Selected = std::min(Value.Choice, D.OptionCount - 1u);
                const bool Open = State.OpenChoicePart == static_cast<int32_t>(PartIndex) && State.OpenChoiceProperty == V;
                const ControlHit Hit = ControlKit::Dropdown(Surface, Control, D.Options[Selected], Open, Local, Opacity);
                if (Hit.Clicked)
                {
                    State.OpenChoicePart     = static_cast<int32_t>(PartIndex);
                    State.OpenChoiceProperty = V;
                    State.OpenChoiceButton   = Control;
                }
            }

            RowY += ControlKitTokens::ControlHeight + Gap;
        }

        Cursor += CardHeight + Gap;
    }

    return Cursor - Y;
}

void SceneObjectInspector::DrawFloatingMenu(PixelSpace& Surface, SceneObjectPartStore& Store, SceneObjectInspectorState& State,
                                            const ControlPointer& Pointer, float Opacity) noexcept
{
    if (State.OpenChoicePart < 0 || static_cast<size_t>(State.OpenChoicePart) >= Store.Count())
        return;

    const SceneObjectPartEntry& Part = Store.Parts()[static_cast<size_t>(State.OpenChoicePart)];
    const SceneObjectPropertySpan Span = SceneObjectPropertiesOf(Part.Class);
    if (State.OpenChoiceProperty >= Span.Count)
        return;

    const SceneObjectPropertyDescriptor& D = Span.First[State.OpenChoiceProperty];
    const uint32_t Selected = std::min(Part.Values[State.OpenChoiceProperty].Choice, D.OptionCount - 1u);

    uint32_t Picked = Selected;
    const ControlHit Hit = ControlKit::DropdownMenu(Surface, State.OpenChoiceButton, D.Options, D.OptionCount, Selected,
                                                    Pointer, Picked, Opacity);
    if (Hit.Clicked)
        Store.SetChoice(static_cast<size_t>(State.OpenChoicePart), D.Name, Picked);

    // A click anywhere outside the menu closes it, as does a pick.
    if (Hit.Clicked || (Pointer.Pressed && !Hit.Hovered))
        State.OpenChoicePart = -1;
}

}   // namespace Frontier
