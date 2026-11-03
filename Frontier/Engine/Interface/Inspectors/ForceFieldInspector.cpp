/*==========================================================================================================================================
                                                          FORCEFIELDINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 Native field controls and actual solver readouts, with world-space velocity arrows rather than fabricated force magnitudes.

#include "Engine/Interface/Inspectors/ForceFieldInspector.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorControls.h"
#include <cmath>
#include <cstdio>

namespace Frontier
{

void ForceFieldInspector::ConstructInspector()
{
    const auto    Theme = ControlTheme::ResolveWind();
    const auto    Faces = EditorTypefaces::Resolve();
    ControlColumn Page(Theme, Faces, InspectorRuntime::Instance().ResolveInterfaceScale());
    const auto   &Access  = InspectorRuntime::Instance().QueryEnvironmentAccess();
    const auto    Readout = Access.Rendering;
    bool          Changed = false;
    if (PageHeading::Construct(Page, "FieldPower", "FORCE FIELD / ATMOSPHERIC FLOW", "Force Field", Parameters.Enabled ? "● On" : "○ Off",
                               "One evaluated field. Independent contributors. Explicit receivers."))
    {
        Parameters.Enabled = !Parameters.Enabled;
        Changed            = true;
    }
    CardLook Card;
    Card.Fill    = ResolveCssColour(0x252C23FF);
    Card.FillEnd = ResolveCssColour(0x1D221EFF);
    Card.Border  = ResolveCssColour(0xB6CE9B21);
    Page.BeginCard(Card, "Field composition", nullptr);
    CaptionText::Construct(Page, "Wind supplies atmospheric flow. The field combines its registered sources; each medium decides whether "
                                 "to follow. These are air velocities in m/s, not forces in newtons.");
    if (Readout && Readout->Ready)
    {
        char Number[64];
        std::snprintf(Number, sizeof(Number), "%.2f",
                      std::hypot(Readout->WindAtCamera[0], Readout->WindAtCamera[1], Readout->WindAtCamera[2]));
        ReadoutBlock::Construct(Page, "Evaluated at camera", Number, "m/s");
        for (const auto &Contributor : Readout->Contributions)
            CaptionText::Construct(Page, Contributor.c_str(), 6, 6);
        if (Readout->Contributions.empty()) CaptionText::Construct(Page, "No active field contributors.");
    }
    else
        CaptionText::Construct(Page, "Waiting for the live viewport's evaluated field. No velocity measurement is fabricated.");
    if (NextButton::Construct(Page, "FieldWind", "Open Wind contributor ↗", Access.Wind != nullptr, "Add a Wind record first."))
        InspectorRuntime::Instance().RequestSelection("Wind");
    Changed |= SwitchRow::Construct(Page, "FieldTurbulence", "Include curl turbulence", Parameters.IncludeTurbulence);
    Page.EndCard();
    Page.BeginCard(Card, "World vectors", nullptr);
    Changed |= SwitchRow::Construct(Page, "FieldVectors", "Show world vectors", Parameters.Visualize);
    CaptionText::Construct(Page, "Perspective-projected 3D samples of the same field used by the media. X-ray diagnostic overlay: arrows "
                                 "are clipped to the viewport, not occluded by scene surfaces.");
    if (NextButton::Construct(Page, "CentreField", "Place inspection region ahead of camera", Access.CameraReady,
                              "Requires a viewport camera."))
    {
        for (size_t Axis = 0; Axis < 3; ++Axis)
            Parameters.CentreMetres[Axis] = static_cast<float>(Access.CameraEye[Axis] + Access.CameraForward[Axis] * 30);
        Changed = true;
    }
    Changed |= AxisFields::Construct(Page, "FieldCentre", "Region centre · X / Y / Z (m)", Parameters.CentreMetres,
                                     {-100000, -100000, -10000}, {100000, 100000, 20000});
    Changed |= AxisFields::Construct(Page, "FieldHalfExtent", "Region half extents · X / Y / Z (m)", Parameters.HalfExtentsMetres,
                                     {1, 1, 1}, {1000, 1000, 1000});
    Changed |= SliderRow::ConstructInteger(Page, "FieldGrid", "Samples per axis", Parameters.GridResolution, 3, 8, "");
    Changed |= SliderRow::Construct(Page, "FieldArrowTime", "Arrow time scale", Parameters.ArrowSeconds, .05f, 2, .05f, "s");
    CaptionText::Construct(Page, "Arrow length = velocity × time scale. The display never changes field strength or density.");
    Page.EndCard();
    Page.BeginCard(Card, "Receivers", nullptr);
    if (Readout && Readout->Ready)
    {
        char Counts[128];
        std::snprintf(Counts, sizeof(Counts), "Rendered local volumes: %u clouds · %u fog", Readout->LocalClouds, Readout->LocalFogVolumes);
        CaptionText::Construct(Page, Counts);
    }
    CaptionText::Construct(Page, "Global clouds, local clouds and local fog use their Follow wind switches. Independent local fog wind "
                                 "remains available. No vegetation consumer is registered in Everest; tree bending is not claimed.");
    Page.EndCard();
    Page.Finalize();
    if (Changed) Parameters.AdvanceRevision();
}

} // namespace Frontier

#elif FRONTIER_EDITOR
namespace Frontier
{
void ForceFieldInspector::ConstructInspector() {}
} // namespace Frontier
#endif
