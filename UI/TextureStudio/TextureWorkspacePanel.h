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

#pragma once
#include "Engine/Interface/PanelDockspace.h"
#include "Engine/Interface/PixelSpace.h"
#include "Engine/Interface/VectorAuthoringControls.h"
#include "Engine/Lifecycle/ActivationSequence.h"
#include <array>
#include <functional>
#include <map>
#include <set>
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
    // 📝 1:1 with textureDocument.js's TEXTURE_KINDS keys/colours - a flat row list is enough to reproduce the
    // web layer stack's folder/decal/paint/fill/material/generator rows without a real document model.
    struct TextureLayerRow
    {
        std::string Name;
        std::string Kind; // "folder" | "decal" | "paint" | "fill" | "material" | "generator"
        int Depth = 0;
        bool Visible = true;
        bool Locked = false;
        bool Open = true; // folder expand/collapse
        std::string Blend = "Normal";
        int Opacity = 100;
        int ChildCount = 0;
        int DecalCount = 0;
        bool HasMask = false;
        bool MaskEnabled = true;
        std::vector<std::string> Channels; // enabled painting-channel ids
    };
    std::vector<TextureLayerRow> Layers;
    int SelectedLayer = 1;
    bool MaskSelected = false;
    std::string LayerQuery;
    int LayerFilter = 0; // 0 all, 1 visible, 2 hidden, 3 locked
    bool AddLayerMenuOpen = false;
    bool ChannelPickerOpen = false; // "Add channel" grouped checklist, expanded inline under the Channel targets card
    std::set<std::string> FoldedChannels; // channel ids collapsed in the inspector

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

    // 📝 Per-channel source-mode state for the "Channel targets" cards (ChannelPropertyPanel.jsx's Texture /
    // Value / Generator switch, minus the Gradient mode which needs the point-gradient editor out of scope
    // here). Keyed by "<layer row index>:<channel id>" so switching the selected layer and back doesn't
    // clobber another layer's choice. Nothing here touches real compositing - it only decides which static
    // body the inspector renders for that channel.
    std::map<std::string, int> ChannelSourceMode;               // 0 = Texture, 1 = Colour/Value, 2 = Generator
    std::map<std::string, std::array<float, 4>> ChannelColour;  // Colour-mode swatch (baseColor channel only)
    std::map<std::string, float> ChannelAmount;                 // Colour-mode scalar value (non-baseColor channels)
    std::map<std::string, int> ChannelGeneratorChoice;          // -1 = none picked yet, else index into ChannelGeneratorCatalogue
    std::map<std::string, float> ChannelGeneratorParameters;    // keyed by "<channel key>:<parameter key>"
    [[nodiscard]] std::string QueryChannelKey(const std::string &ChannelId) const;

    void SelectLayer(int Index, bool ToggleMask = false);
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
    void ConstructViewportDock();
    void ConstructInspectorDock();
    void ConstructInspectorProperties(float Width);
    void ConstructInspectorHistory(float Width);
    void ConstructChannelCard(float Width, const std::string &Id, const char *Label, uint32_t Tint, const char *EditKind,
        const char *Note, float Minimum, float Maximum, float Default, const std::array<float, 4> &DefaultColour);
    // 📝 One body-renderer per Texture/Colour/Generator source mode (ChannelPropertyPanel.jsx's three branches,
    // minus the Gradient mode - see ConstructChannelCard's comment for why).
    void ConstructChannelTextureSource(const std::string &Key, const char *Label);
    void ConstructChannelColourSource(const std::string &Key, const std::string &Id, const char *Label, const char *EditKind,
        float Minimum, float Maximum, float Default, const std::array<float, 4> &DefaultColour);
    void ConstructChannelGeneratorSource(const std::string &Key, const char *Label);
    // 📝 1:1 with ChannelPropertyPanel.jsx's inline (non-floating) "channel-picker" - a grouped checklist that
    // expands directly under the "+ Add channel" button inside the same card, not a separate popup/menu.
    void ConstructChannelPicker(float Width, TextureLayerRow &Layer);
    void ConstructMaskToolbar(float Width);
    // 📝 The inspector's Controls.Dropdown() is a full-width, label-ABOVE-box form field (built for vertical
    // property-panel forms like "Blend"/"Opacity") - it even force-overrides any caller-requested width to the
    // full line. Reusing it for the viewport toolbar's Shading/Target/UV set row, which needs several compact
    // selectors side by side on ONE line, made every entry wrap onto its own line and stack on top of the rows
    // below it (the authoring bar, the "Stamp studio ->" link). This is the compact single-line variant that
    // mirrors the web app's ".tp-header-field" (a small label immediately left of a fixed-width select box).
    bool ConstructInlineDropdown(const char *Label, const char *Id, int &Selection, std::span<const char *const> Captions, float ComboWidth);

    PanelDockspace Panels;
    std::map<std::string, PlaneExtent> ControlExtents;
    void Track(const std::string &Caption);

    std::string Info;
    VectorAuthoringControlSurface Controls;
    void *RegularTypeface = nullptr, *LightTypeface = nullptr;
};
} // namespace Frontier
