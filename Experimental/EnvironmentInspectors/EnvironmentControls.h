/*==========================================================================================================================================
                                                    ENVIRONMENTCONTROLS.H
==========================================================================================================================================*/
// 🧩 Shared controls for experimental environmental inspectors.
#pragma once
namespace Frontier::Experimental
{
// Call immediately after a focusable diagram/slider item; arrows repeat, Shift multiplies the step.
struct DiagramKeys
{
    int Horizontal = 0, Vertical = 0;
    bool Home = false, End = false;
    float Step = 1.f;
    bool Any() const { return Horizontal != 0 || Vertical != 0 || Home || End; }
};
DiagramKeys ReadDiagramKeys();
bool AuthoringSlider(const char* Id, const char* Label, float& Value, float Minimum, float Maximum,
                     float Step, const char* Unit, const char* Format = "%.1f", float Width = 0.0f);
}
