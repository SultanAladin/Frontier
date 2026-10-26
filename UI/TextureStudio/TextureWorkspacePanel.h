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

    void SelectLayer(int Index, bool ToggleMask = false);
    void AddLayer(const std::string &Kind);

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
    void ConstructChannelCard(float Width, const char *Label, uint32_t Tint, const char *Mode, const char *Note);
    void ConstructMaskToolbar(float Width);

    PanelDockspace Panels;
    std::map<std::string, PlaneExtent> ControlExtents;
    void Track(const std::string &Caption);

    std::string Info;
    VectorAuthoringControlSurface Controls;
    void *RegularTypeface = nullptr, *LightTypeface = nullptr;
};
} // namespace Frontier
