/*==========================================================================================================================================
                                                                SLIDERROW.H
==========================================================================================================================================*/
// 🧩 .co-slider: label, editable figure and unit over the accent track (ObservatorySlider.js, SliderControl.css).
//    Quantity is the stored setting; the figure shows Quantity × DisplayScale. Disabled rows are pending a native
//    field and show Tooltip.

#pragma once

#include <cstdint>

namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    SLIDER ROW
//------------------------------------------------------------------------------------------------------------------------

class SliderRow
{
  public:
    static bool Construct(ControlColumn& Column, const char* Id, const char* Label, float& Quantity, float Minimum, float Maximum, float Step,
                          const char* Unit, float DisplayScale = 1.0f, bool Enabled = true, const char* Tooltip = nullptr);
    static bool ConstructInteger(ControlColumn& Column, const char* Id, const char* Label, uint32_t& Quantity, uint32_t Minimum,
                                 uint32_t Maximum, const char* Unit, bool Enabled = true, const char* Tooltip = nullptr);
};

} // namespace Frontier
