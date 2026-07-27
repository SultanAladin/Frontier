/*==============================================================================================================================================
                                                            CONTROLSGALLERYPANEL.CPP
==============================================================================================================================================*/
// 🧩 Reproduces every card of ControlsPreview.html with the REAL Interface controls. Each BeginPropertyCard/EndPropertyCard block maps to one
//    <div class="panel"> in the mockup; the labels ("Degree", "Percent", "Position", ...) and the control choices mirror the HTML exactly so
//    a side-by-side comparison is one-to-one. Nothing here is app-specific — it is a pure showcase over ControlsGalleryState.

#include "ControlsGalleryPanel.h"

#include "EngineContext/Interface/Components/PropertyPanelBase.h"

#include "EngineContext/Interface/Components/Controls/ValueSlider.h"

#include "EngineContext/Interface/Components/Controls/ScalarEntry.h"

#include "EngineContext/Interface/Components/Controls/VectorEntry.h"

#include "EngineContext/Interface/Components/Controls/ColorEntry.h"

#include "EngineContext/Interface/Components/Controls/SelectionEntry.h"

#include "EngineContext/Interface/Components/Controls/BooleanEntry.h"

#include "EngineContext/Interface/Components/Controls/PathEntry.h"

#include "EngineContext/Interface/Components/Controls/Dropdown.h"

using namespace Frontier;


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ConstructControlsGalleryPanel(const ThemeConfiguration& Theme, ControlsGalleryState& State)
{
    BeginPropertyPanel(Theme, "##controls-gallery");

    // -- ValueSlider: bounded slider + editable pill (ControlsPreview.html line 228) ----------------------------------
    if (BeginPropertyCard(Theme, "ValueSlider", &State.ValueSliderExpanded))
    {
        ValueSliderDescriptor Degree = {};
        Degree.Label = "Degree"; Degree.Value = &State.Degree;
        Degree.Minimum = 0.0f; Degree.Maximum = 360.0f; Degree.Format = "%.0f"; Degree.Unit = "\xC2\xB0"; Degree.Enabled = true;
        ConstructValueSlider(Theme, Degree);

        ValueSliderDescriptor Percent = {};
        Percent.Label = "Percent"; Percent.Value = &State.Percent;
        Percent.Minimum = 0.0f; Percent.Maximum = 1.0f; Percent.Format = "%.2f"; Percent.Unit = "%"; Percent.Enabled = true;
        ConstructValueSlider(Theme, Percent);

        ValueSliderDescriptor Pixel = {};
        Pixel.Label = "Pixel"; Pixel.Value = &State.Pixel;
        Pixel.Minimum = 1.0f; Pixel.Maximum = 512.0f; Pixel.Format = "%.0f"; Pixel.Unit = "px"; Pixel.Enabled = true;
        ConstructValueSlider(Theme, Pixel);

        EndPropertyCard(Theme);
    }

    // -- ScalarEntry: unbounded drag pill (line 251) ------------------------------------------------------------------
    if (BeginPropertyCard(Theme, "ScalarEntry", &State.ScalarExpanded))
    {
        ScalarEntryDescriptor Intensity = {};
        Intensity.Label = "Intensity"; Intensity.Value = &State.Intensity;
        Intensity.Step = 0.01f; Intensity.Minimum = 0.0f; Intensity.Maximum = 0.0f; Intensity.Format = "%.2f"; Intensity.Unit = "\xC3\x97"; Intensity.Enabled = true;
        ConstructScalarEntry(Theme, Intensity);

        ScalarEntryDescriptor Radius = {};
        Radius.Label = "Radius"; Radius.Value = &State.Radius;
        Radius.Step = 0.1f; Radius.Minimum = 0.0f; Radius.Maximum = 0.0f; Radius.Format = "%.1f"; Radius.Unit = "cm"; Radius.Enabled = true;
        ConstructScalarEntry(Theme, Radius);

        EndPropertyCard(Theme);
    }

    // -- VectorEntry: XYZ (line 269) ----------------------------------------------------------------------------------
    if (BeginPropertyCard(Theme, "VectorEntry", &State.VectorExpanded))
    {
        VectorEntryDescriptor Position = {};
        Position.Label = "Position"; Position.Components = State.Position; Position.ComponentCount = 3;
        Position.Step = 0.01f; Position.Format = "%.2f"; Position.Enabled = true;
        ConstructVectorEntry(Theme, Position);

        EndPropertyCard(Theme);
    }

    // -- ColorEntry: Figma picker (line 285) --------------------------------------------------------------------------
    if (BeginPropertyCard(Theme, "ColorEntry", &State.ColorExpanded))
    {
        ColorEntryDescriptor BaseColor = {};
        BaseColor.Label = "Base Color"; BaseColor.Channels = State.BaseColor; BaseColor.IncludeAlpha = true; BaseColor.Enabled = true;
        ConstructColorEntry(Theme, BaseColor);

        EndPropertyCard(Theme);
    }

    // -- SelectionEntry: segmented pill (line 314) --------------------------------------------------------------------
    if (BeginPropertyCard(Theme, "SelectionEntry", &State.SelectionExpanded))
    {
        static const char* const ShadowOptions[] = { "Off", "Hard", "Soft", "Merged" };
        SelectionEntryDescriptor CastShadow = {};
        CastShadow.Label = "Cast Shadow"; CastShadow.SelectedIndex = &State.CastShadow;
        CastShadow.Options = ShadowOptions; CastShadow.OptionCount = 4; CastShadow.Enabled = true;
        ConstructSelectionEntry(Theme, CastShadow);

        EndPropertyCard(Theme);
    }

    // -- SelectionEntry: size preset (line 325) -----------------------------------------------------------------------
    if (BeginPropertyCard(Theme, "Size Preset", &State.SizeExpanded))
    {
        static const char* const SizeOptions[] = { "S", "M", "L", "XL" };
        SelectionEntryDescriptor Size = {};
        Size.Label = "Size"; Size.SelectedIndex = &State.SizePreset;
        Size.Options = SizeOptions; Size.OptionCount = 4; Size.Enabled = true;
        ConstructSelectionEntry(Theme, Size);

        EndPropertyCard(Theme);
    }

    // -- BooleanEntry: toggle switch (line 336) -----------------------------------------------------------------------
    if (BeginPropertyCard(Theme, "BooleanEntry", &State.BooleanExpanded))
    {
        BooleanEntryDescriptor IndirectGi = {};
        IndirectGi.Label = "Indirect GI"; IndirectGi.Value = &State.IndirectGi; IndirectGi.Enabled = true;
        ConstructBooleanEntry(Theme, IndirectGi);

        BooleanEntryDescriptor Wireframe = {};
        Wireframe.Label = "Wireframe"; Wireframe.Value = &State.Wireframe; Wireframe.Enabled = true;
        ConstructBooleanEntry(Theme, Wireframe);

        EndPropertyCard(Theme);
    }

    // -- PathEntry: editable text + browse (line 351) -----------------------------------------------------------------
    if (BeginPropertyCard(Theme, "PathEntry", &State.PathExpanded))
    {
        PathEntryDescriptor MeshSource = {};
        MeshSource.Label = "Mesh Source"; MeshSource.Buffer = State.MeshSource;
        MeshSource.BufferCapacity = static_cast<int>(sizeof(State.MeshSource)); MeshSource.Enabled = true;
        ConstructPathEntry(Theme, MeshSource);

        EndPropertyCard(Theme);
    }

    // -- Dropdown: combo popup (line 366) -----------------------------------------------------------------------------
    if (BeginPropertyCard(Theme, "Dropdown", &State.DropdownExpanded))
    {
        static const char* const ShadingOptions[] = { "Lit", "Unlit", "Normals", "Wireframe" };
        DropdownDescriptor Shading = {};
        Shading.Label = "Shading"; Shading.SelectedIndex = &State.Shading;
        Shading.Options = ShadingOptions; Shading.OptionCount = 4; Shading.Enabled = true;
        ConstructDropdown(Theme, Shading);

        EndPropertyCard(Theme);
    }

    EndPropertyPanel(Theme);
}
