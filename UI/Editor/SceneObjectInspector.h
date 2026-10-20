//========================================================================================================================
//                                                SCENEOBJECTINSPECTOR.H
//========================================================================================================================
// 🧩 Inspector for a SceneObject. Draws every part as one flat card and every property with a ControlKit widget.
//    The store owns the values; the inspector only reports edits. Dropdown menus draw in a second pass so they overlay.
//========================================================================================================================
#pragma once

#include "ControlKit.h"
#include "SceneObjectPartStore.h"

#include <cstdint>

namespace Frontier {

struct SceneObjectInspectorState
{
    int32_t     DraggingPart     = -1;   // [-] Part whose slider is held, -1 for none
    uint32_t    DraggingProperty = 0u;   // [-] Property index of the held slider
    int32_t     OpenChoicePart   = -1;   // [-] Part whose dropdown menu is open, -1 for none
    uint32_t    OpenChoiceProperty = 0u; // [-] Property index of the open dropdown
    PlaneExtent OpenChoiceButton{};      // [px] Button extent the open menu is anchored to
};

class SceneObjectInspector
{
public:
    // 📝 Draws the whole store top to bottom. Returns the height used. Call DrawFloatingMenu after this.
    static float DrawStore(PixelSpace& Surface, float X, float Y, float Width, SceneObjectPartStore& Store,
                           SceneObjectInspectorState& State, const ControlPointer& Pointer, float Opacity = 1.0f) noexcept;

    // 📝 Draws the open dropdown menu over everything else and applies a pick to the store.
    static void DrawFloatingMenu(PixelSpace& Surface, SceneObjectPartStore& Store, SceneObjectInspectorState& State,
                                 const ControlPointer& Pointer, float Opacity = 1.0f) noexcept;

    // 📝 Preset colours offered for a Colour property. A free colour picker is not part of ControlKit yet.
    static constexpr uint32_t ColourPresetCount = 5u;
    [[nodiscard]] static SceneObjectColour ColourPreset(uint32_t Index) noexcept;
};

}   // namespace Frontier
