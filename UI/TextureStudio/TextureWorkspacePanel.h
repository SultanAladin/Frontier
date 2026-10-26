/*============================================================================================================================================
                                                          TEXTUREWORKSPACEPANEL.H
============================================================================================================================================*/
// 🧩 Native UI-only port of Experimental/TextureStudio/src/TexturePaintStudio.jsx's "Texture" workspace chrome:
//    the layer stack (rows, folders, filters, add-layer menu), the tabbed inspector (properties/channels/decals
//    and history), the mask toolbar and the viewport's surrounding chrome (toolbar, authoring bar, scene
//    caption/nav/axis gizmo, footer). The 3D canvas itself, real paint/mask/decal compositing and the asset
//    content-browser drawer are explicitly out of scope for this pass (see the user's "only do the UI not the
//    viewport" direction) - every control here operates on an in-memory representative demo document, not a
//    real paintable surface.
//
// 📝 The layer stack (left dock) and the per-channel inspector card (right dock, "Channel targets") are both
// transcribed 1:1 against their own dedicated prototype, not the original JSX, per the port-target notes in
// Documentation/Plans/References/InheritedEngine/Prototypes/LayerStackPanel.html and ChannelPropertyPanel.html:
// a bubble+track identity ladder with drag-to-reorder and a full 7-mode blend dropdown for the stack, and a
// real "+" add-channel dropdown (never a toggle switch) with a two-slot texture source and a generator picker
// for the channel cards.

#pragma once
#include "Engine/Interface/PanelDockspace.h"
#include "Engine/Interface/PixelSpace.h"
#include "Engine/Interface/VectorAuthoringControls.h"
#include "Engine/Lifecycle/ActivationSequence.h"
#include <array>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace Frontier
{
class TextureWorkspacePanel final : public ActivationSequence
{
  public:
    TextureWorkspacePanel();
    void ConstructInterface();
    void ConfigureInterfaceScale(float Scale) noexcept { Scale = Scale > 0.1f ? Scale : 1.f; InterfaceScale = Scale; Controls.Scale = Scale; }
    [[nodiscard]] float QueryInterfaceScale() const noexcept { return InterfaceScale; }
    // 📝 Same contract as every other native workspace's ConfigureTypeface.
    void ConfigureTypeface(void *Regular, void *Light = nullptr) noexcept { RegularTypeface = Regular; LightTypeface = Light ? Light : Regular; }

    // 📝 Cross-workspace navigation, wired exactly like Material/Pattern/Stamp/Baking's own Activate* set.
    std::function<void()> ActivateMaterial;
    std::function<void()> ActivatePattern;
    std::function<void()> ActivateStamp;
    std::function<void()> ActivateBaking;

    [[nodiscard]] PlaneExtent QueryControlExtent(const std::string &Caption) const;
    [[nodiscard]] PanelDockspace &QueryPanels() noexcept { return Panels; }
    void ConfigurePlatform() { (void)Panels.ConfigurePlatform(); }

  private:
    bool InitializeContent() noexcept override;
    void FinalizeContent() noexcept override;
    void IntegrateContent(double DeltaSeconds) noexcept override;

    //--------------------------------------------------------------------------------------------------------------
    //                                                 DEMO DOCUMENT (REPRESENTATIVE, NOT A REAL PAINTABLE SURFACE)
    //--------------------------------------------------------------------------------------------------------------
    // 📝 1:1 with LayerStackPanel.html's State.Layers records (Name/Kind/BlendIndex/Opacity/Shown/Expanded/
    // Tag/Paint/MaskFill/MaskInverted/MaskStrength), plus the Depth/ChildCount/DecalCount/Channels fields the
    // web document model (textureDocument.js) still needs for folders, decals and painting channels - the
    // reference prototype itself never models either of those.
    struct TextureLayerRow
    {
        std::string Name;
        std::string Kind; // "folder" | "decal" | "paint" | "fill" | "material" | "generator"
        int Depth = 0;
        bool Visible = true;
        bool Locked = false;
        bool Open = true;      // folder disclosure - are this folder's children shown at all
        bool Expanded = false; // LayerStackPanel.html's `.card.open` - is this row's inline Blend/Opacity/Tag/Mask fold open
        std::string Blend = "Normal";
        int Opacity = 100;
        int ChildCount = 0;
        int DecalCount = 0;
        bool HasMask = false;
        std::string MaskFill = "White";
        bool MaskInverted = false;
        float MaskStrength = 100.f;
        uint32_t Tag = 0xf97316;   // identity bubble / timeline-track hue and the expand's "Tag" colour field
        uint32_t Paint = 0x2e2d2b; // thumb's centred ink swatch, shown only while baseColor is a channel target
        std::vector<std::string> Channels; // enabled painting-channel ids
    };
    std::vector<TextureLayerRow> Layers;
    int SelectedLayer = 1;
    std::string LayerQuery;
    bool AddLayerMenuOpen = false;
    // 📝 LayerStackPanel.html's Lift/LiftedOrdinal/ResolveDropSlot/RelocateLayer - press-and-drag a row by its
    // grip handle to reorder it; arms only past a few pixels of travel so a plain click still just selects.
    int DragSourceIndex = -1;
    int DragHoverSlot = -1;
    bool DragArmed = false;

    //--------------------------------------------------------------------------------------------------------------
    //                                                 VIEWPORT / AUTHORING CHROME STATE (NO REAL 3D, NO REAL PAINT)
    //--------------------------------------------------------------------------------------------------------------
    enum class ShadingMode { Lit, Wireframe, Matcap };
    enum class AuthoringTool { Navigate, Stamp, Transform };
    enum class DigitalTool { None, Paint, Erase, Fill };
    enum class PreviewMode { Material, Mask };
    ShadingMode Shading = ShadingMode::Lit;
    int TargetPart = 0;   // 0 whole object, 1..N named parts
    int UVSet = 0;        // 0 UV0, 1 UV1
    bool GridVisible = true;
    AuthoringTool Tool = AuthoringTool::Navigate;
    DigitalTool Digital = DigitalTool::None;
    PreviewMode Preview = PreviewMode::Material;
    float BrushSize = 34.f;
    bool MaskOptionsOpen = false;
    int ZoomPercent = 100;

    //--------------------------------------------------------------------------------------------------------------
    //                                                 INSPECTOR / HISTORY TAB STATE
    //--------------------------------------------------------------------------------------------------------------
    enum class InspectorTab { Properties, History };
    InspectorTab RightTab = InspectorTab::Properties;
    int HistoryFilter = 0; // 0 all edits, 1 strokes

    // 📝 Per-channel source-mode state for the "Channel targets" cards (ChannelPropertyPanel.html's Texture /
    // Value / Generator switch). Keyed by "<layer row index>:<channel id>" so switching the selected layer and
    // back doesn't clobber another layer's choice. Nothing here touches real compositing - it only decides
    // which static body the inspector renders for that channel.
    std::map<std::string, int> ChannelSourceMode;               // 0 = Texture, 1 = Value, 2 = Generator
    std::map<std::string, std::array<float, 4>> ChannelColour;  // Value-mode swatch (colour-edit channels)
    std::map<std::string, float> ChannelAmount;                 // Value-mode scalar value (scalar-edit channels)
    std::map<std::string, int> ChannelGeneratorChoice;          // -1 = none picked yet, else index into ChannelGeneratorCatalogue
    std::map<std::string, float> ChannelGeneratorParameters;    // keyed by "<channel key>:<parameter key>"
    // 📝 ChannelPropertyPanel.html's Layer.Strokes / Layer.Textures - the Texture mode's two slots (painted
    // strokes, optional imported base) each need their own small bit of demo state since this native build has
    // no real paint canvas or file-import dialog to source them from.
    std::map<std::string, int> ChannelStrokeCount;
    std::map<std::string, bool> ChannelHasImportBase;
    std::map<std::string, std::string> ChannelImportFormat;
    [[nodiscard]] std::string QueryChannelKey(const std::string &ChannelId) const;

    void SelectLayer(int Index);
    void AddLayer(const std::string &Kind);
    // 📝 1:1 with paintChannelModel.js's togglePaintChannel - adding/removing a channel, pairing Normal and
    // Height together since Normal is purely derived from Height here.
    static void ToggleChannel(TextureLayerRow &Layer, const std::string &Id, bool Enable);

    //--------------------------------------------------------------------------------------------------------------
    //                                                 LAYOUT SECTIONS
    //--------------------------------------------------------------------------------------------------------------
    float InterfaceScale = 1.f;
    void ConstructHeader();
    void ConstructLayersDock();
    // 📝 LayerStackPanel.html's BuildExpand - the row's own inline Blend/Opacity/Tag/Mask fold, drawn directly
    // under its header row inside the SAME card when Layer.Expanded is true.
    void ConstructLayerExpand(TextureLayerRow &Layer, float Width, int Index);
    void ConstructViewportDock();
    void ConstructInspectorDock();
    void ConstructInspectorProperties(float Width);
    void ConstructInspectorHistory(float Width);
    void ConstructChannelCard(float Width, const std::string &Id, const char *Label, const char *EditKind,
        const char *Note, const char *Placement, float Minimum, float Maximum, float Default, const std::array<float, 4> &DefaultColour,
        uint32_t HeaderHue);
    // 📝 One body-renderer per Texture/Value/Generator source mode (ChannelPropertyPanel.html's three
    // branches). Each ends in ConstructChannelPreview() - the "{Mode} preview / {atlas placement}" footer
    // every channel body carries, derived or not.
    void ConstructChannelTextureSource(const std::string &Key, const std::string &Id, const char *Label);
    void ConstructChannelColourSource(const std::string &Key, const std::string &Id, const char *Label, const char *EditKind,
        float Minimum, float Maximum, float Default, const std::array<float, 4> &DefaultColour);
    void ConstructChannelGeneratorSource(const std::string &Key, const char *Label);
    void ConstructChannelPreview(const char *ModeCaption, const char *Placement, bool SolidFill, uint32_t FillColour);
    // 📝 1:1 with ChannelPropertyPanel.html's ".add-menu" - a real floating dropdown anchored under the round
    // "+" button, grouped Surface/Radiance/Reflectance/Scattering, each row a plain click-to-toggle entry (a
    // checkmark marks an enabled channel) - never a switch/toggle control, and it stays open across multiple
    // toggles exactly like the reference's OpenPicker state (only closing on the "+" button or an outside click).
    void ConstructChannelAddMenu(TextureLayerRow &Layer);
    void ConstructMaskToolbar(float Width);
    // 📝 The inspector's Controls.Dropdown() is a full-width, label-ABOVE-box form field (built for vertical
    // property-panel forms like "Blend"/"Opacity") - it even force-overrides any caller-requested width to the
    // full line. Reusing it for the viewport toolbar's Shading/Target/UV set row, which needs several compact
    // selectors side by side on ONE line, made every entry wrap onto its own line and stack on top of the rows
    // below it (the authoring bar, the "Stamp studio ->" link). This is the compact single-line variant that
    // mirrors the web app's ".tp-header-field" (a small label immediately left of a fixed-width select box).
    bool ConstructInlineDropdown(const char *Label, const char *Id, int &Selection, std::span<const char *const> Captions, float ComboWidth);
    // 📝 LayerStackPanel.html's BuildDropdown (Dropdown.cpp VERBATIM per its own notes) - a value-pill face with
    // a grey caret cap, opening a sharp-cornered menu where EVERY row carries a radio ring (filled when it is
    // the current mode) rather than a checkmark - used for the layer stack's 7-mode Blend dropdown.
    bool ConstructRadioDropdown(const char *Id, const char *Preview, int &Selection, std::span<const char *const> Captions, float Width);
    // 📝 LayerStackPanel.html's `.row .pill` - a borderless capsule showing "NN%" that SCRUBS horizontally
    // (cursor:ew-resize) rather than opening any editor; both the compact row pill and a wider one re-use this.
    bool ConstructOpacityScrub(const char *Id, int &Percent, float Width, float Height);

    PanelDockspace Panels;
    std::map<std::string, PlaneExtent> ControlExtents;
    void Track(const std::string &Caption);

    std::string Info;
    VectorAuthoringControlSurface Controls;
    void *RegularTypeface = nullptr, *LightTypeface = nullptr;
};
} // namespace Frontier
