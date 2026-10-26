/*============================================================================================================================================
                                                          TEXTUREWORKSPACEPANEL.CPP
============================================================================================================================================*/
#include "UI/TextureStudio/TextureWorkspacePanel.h"
#include "Engine/Interface/ControlKit.h"
#include "Engine/Interface/DropdownControls.h"
#include "Engine/Interface/GlyphSpace.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <utility>

namespace Frontier
{
namespace
{
ImVec4 Spectrum(uint32_t Colour, float Alpha = 1.f) noexcept
{
    return ImVec4(float((Colour >> 16) & 0xff) / 255.f, float((Colour >> 8) & 0xff) / 255.f, float(Colour & 0xff) / 255.f, Alpha);
}
ColorQuad SpectrumQuad(uint32_t Colour, float Alpha = 1.f) noexcept
{
    return ColorQuad{float((Colour >> 16) & 0xff) / 255.f, float((Colour >> 8) & 0xff) / 255.f, float(Colour & 0xff) / 255.f, Alpha};
}
// 🎨 Absolute-position draw-list text - same duplicated Caption() helper every other native workspace carries.
void Caption(ImVec2 Position, const char *Text, float Size, uint32_t Colour, void *Typeface = nullptr)
{
    ImGui::GetWindowDrawList()->AddText(Typeface ? static_cast<ImFont *>(Typeface) : ImGui::GetFont(), Size * 1.302f, Position,
        ImGui::GetColorU32(Spectrum(Colour)), Text);
}
void StrokeIcon(ImVec2 Minimum, float Size, const char *Path, uint32_t Colour, float Weight = 1.6f) noexcept
{
    PixelSpace Surface;
    (void)Surface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 1);
    GlyphSpace::Stroke(Surface, Path, {Minimum.x, Minimum.y, Size, Weight, SpectrumQuad(Colour)});
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     1:1 WITH textureDocument.js's TEXTURE_KINDS
//------------------------------------------------------------------------------------------------------------------------
struct KindMeta { const char *Label; uint32_t Colour; const char *Glyph; };
const KindMeta &QueryKindMeta(const std::string &Kind) noexcept
{
    static const std::map<std::string, KindMeta> Table{
        {"folder",   {"Folder",   0xbfac88, "F"}},
        {"decal",    {"Decal",    0xc4aa7a, "D"}},
        {"paint",    {"Paint",    0xd79564, "P"}},
        {"fill",     {"Fill",     0x779cd1, "L"}},
        {"material", {"Material", 0xaa98d0, "M"}},
        {"generator",{"Generator",0x78ab96, "G"}},
    };
    const auto Found = Table.find(Kind);
    static const KindMeta Fallback{"Layer", 0x9a9a8f, "?"};
    return Found == Table.end() ? Fallback : Found->second;
}
// 🎨 1:1 with paintChannelModel.js's PAINT_CHANNELS palette for the subset of channels this static port surfaces.
constexpr uint32_t kBaseColourTint = 0xb87333, kMetalnessTint = 0x8b5cf6, kRoughnessTint = 0x3b82f6, kHeightTint = 0x8a8a8a,
    kNormalTint = 0x10b981, kOpacityTint = 0x94a3b8;

constexpr const char *PlusPath = "M12 5V19M5 12H19";
constexpr const char *LockPath = "M6 10H18V21H6ZM8 10V7A4 4 0 0 1 16 7V10";
constexpr const char *UnlockPath = "M6 10H18V21H6ZM8 10V7A4 4 0 0 1 15.5 6.3";
constexpr const char *EyePath = "M2 12Q12 0 22 12Q12 24 2 12ZM12 9A3 3 0 1 0 12 15A3 3 0 1 0 12 9Z";
constexpr const char *EyeOffPath = "M3 3L21 21M10.5 10.6A3 3 0 0 0 13.5 13.5M7.4 7.5Q2 11 2 12Q12 24 20 18M9.9 4.2Q17 2 22 12Q21 14 19 16.1";
constexpr const char *ChevronRightPath = "M9 18L15 12L9 6";
constexpr const char *ChevronDownPath = "M6 9L12 15L18 9";
constexpr const char *GripPath = "M8 5A1 1 0 1 0 8 5.01M8 12A1 1 0 1 0 8 12.01M8 19A1 1 0 1 0 8 19.01M16 5A1 1 0 1 0 16 5.01M16 12A1 1 0 1 0 16 12.01M16 19A1 1 0 1 0 16 19.01";
constexpr const char *SearchPath = "M11 19A8 8 0 1 0 11 3A8 8 0 0 0 11 19ZM21 21L16.65 16.65";
constexpr const char *UndoPath = "M3 10L7 6L11 10M7 6V15A5 5 0 0 0 17 15";
constexpr const char *RedoPath = "M21 10L17 6L13 10M17 6V15A5 5 0 0 1 7 15";
constexpr const char *BrushPath = "M9.06 11.9L15 5.95A2.12 2.12 0 0 1 18 8.94L12.06 14.9M9.06 11.9L4 20.9L8 22L13.06 18.9";
constexpr const char *EraserPath = "M20 20H7L3.6 16.6A2 2 0 0 1 3.6 13.7L13.4 3.9A2 2 0 0 1 16.2 3.9L20.6 8.3A2 2 0 0 1 20.6 11.1L11.8 20";
constexpr const char *BucketPath = "M19 11H5L10 4L19 11ZM5 11Q5 17 12 20Q19 17 19 11";
constexpr const char *SlidersPath = "M4 21V14M4 10V3M12 21V12M12 8V3M20 21V16M20 12V3M1 14H7M9 8H15M17 16H23";
constexpr const char *StampPath = "M5 22H19M12 11V16M7 16H17L18.5 22H5.5ZM9 3H15V8A3 3 0 0 1 9 8Z";
constexpr const char *HistoryPath = "M3 12A9 9 0 1 0 6 5.3L3 8M3 3V8H8M12 7V12L15 15";
constexpr const char *FramePath = "M3 7V3H7M17 3H21V7M21 17V21H17M7 21H3V17";
constexpr const char *GridPath = "M3 9H21M3 15H21M9 3V21M15 3V21";
constexpr const char *ResetPath = "M12 3A9 9 0 1 0 20.5 8.5M20.5 3V8.5H15";
constexpr const char *CheckPath = "M20 6L9 17L4 12";
constexpr const char *FolderOpenPath = "M22 19H2V5H9L11 8H22Z";
constexpr const char *FolderPlusPath = "M22 19H2V5H9L11 8H22Z M12 11V17M9 14H15";
constexpr const char *SavePath = "M19 21H5A2 2 0 0 1 3 19V5A2 2 0 0 1 5 3H16L21 8V19A2 2 0 0 1 19 21ZM7 3V8H15V3M7 13H17V21H7Z";
constexpr const char *ArrowUpRightPath = "M7 17L17 7M17 7H8M17 7V16";
constexpr const char *ImagePath = "M3 4H21V20H3ZM3 15L8.5 9.5L13 14L16.5 10.5L21 15";
constexpr const char *UploadPath = "M12 16V4M7 9L12 4L17 9M4 20H20";
constexpr const char *TrashPath = "M4 7H20M9 7V4H15V7M7 7L8 21H16L17 7";
constexpr const char *SparklePath = "M11 3L12.6 8.4L18 10L12.6 11.6L11 17L9.4 11.6L4 10L9.4 8.4ZM19 14L19.8 16.6L22 17L19.8 17.4L19 20L18.2 17.4L16 17L18.2 16.6Z";

// 📝 1:1 with paintChannelModel.js's CHANNEL_GENERATORS - label/group/parameter metadata only, purely for the
// generator picker's visual fidelity (grouped "Mask / Wear / Procedural" option list + its parameter sliders).
// Picking one here never samples a real mesh map or runs any noise kernel - this is still UI-only chrome.
struct ChannelGeneratorMeta
{
    const char *Id;
    const char *Label;
    const char *Group;
    const char *Note;
    std::array<std::pair<const char *, float>, 3> Parameters; // {label, default}; unused slots have a null label
};
const std::array<ChannelGeneratorMeta, 10> &QueryGeneratorCatalogue()
{
    static const std::array<ChannelGeneratorMeta, 10> Catalogue{{
        {"CurvatureEdges", "Curvature", "Mask", "Needs curvature mesh map", {{{"Balance", .5f}, {"Contrast", .7f}, {"Radius", .25f}}}},
        {"AmbientOcclusion", "Ambient Occlusion", "Mask", "Needs occlusion mesh map", {{{"Spread", .4f}, {"Contrast", .6f}, {nullptr, 0.f}}}},
        {"Thickness", "Thickness", "Mask", "Needs thickness mesh map", {{{"Depth", .5f}, {"Contrast", .5f}, {nullptr, 0.f}}}},
        {"PositionGradient", "Position Gradient", "Mask", "World-axis ramp configuration", {{{"Origin", .5f}, {"Falloff", .35f}, {nullptr, 0.f}}}},
        {"MetalEdgeWear", "Metal Edge Wear", "Wear", "Needs curvature / grunge inputs", {{{"Intensity", .6f}, {"Softness", .3f}, {"Grain", .45f}}}},
        {"DirtAccumulation", "Dirt", "Wear", "Needs occlusion input", {{{"Amount", .5f}, {"Scale", .3f}, {nullptr, 0.f}}}},
        {"WaterRunoff", "Water Runoff", "Wear", "Gravity-streak configuration", {{{"Length", .55f}, {"Density", .4f}, {"Gravity", .8f}}}},
        {"PerlinNoise", "Perlin Noise", "Procedural", "Fractal-noise configuration", {{{"Scale", .4f}, {"Octaves", .5f}, {"Contrast", .5f}}}},
        {"VoronoiCells", "Voronoi", "Procedural", "Cellular configuration", {{{"Density", .35f}, {"Jitter", .7f}, {nullptr, 0.f}}}},
        {"BrushedAnisotropy", "Brushed Metal", "Procedural", "Directional-grain configuration", {{{"Angle", 0.f}, {"Grain", .6f}, {nullptr, 0.f}}}},
    }};
    return Catalogue;
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

TextureWorkspacePanel::TextureWorkspacePanel()
{
    (void)Panels.ConfigureDockspaceKey("TextureWorkspace Texture panels");
    Panels.ConfigureWorkspaceInsets(43, 24);
    Panels.ConfigureTabExtent(130, 26);
    Panels.Register({"texture-layers", "Layers", [&]() { ConstructLayersDock(); }});
    Panels.Register({"texture-viewport", "Texture viewport", [&]() { ConstructViewportDock(); }});
    Panels.Register({"texture-inspector", "Texture inspector", [&]() { ConstructInspectorDock(); }});
    // 📝 Default fractions mirror readLayout()'s --layers-width:278 / --inspector-width:338 defaults against
    // TexturePaintStudio.jsx's nominal 1436px authoring canvas (278/1436, remainder, 338/1436).
    Panels.ConfigureColumns({{"texture-layers", .194f}, {"texture-viewport", .571f}, {"texture-inspector", .235f}});
}

bool TextureWorkspacePanel::InitializeContent() noexcept
{
    try
    {
        // 📝 Representative demo document (teapot study) standing in for a real .texture document - no paint
        // compositor, mask raster or decal placement is implemented behind any of this; every row below exists
        // only so the layer stack / inspector / history chrome has something real-looking to display, matching
        // the "full static" scope locked in for this pass.
        Layers.clear();
        Layers.push_back({"Clay base", "material", 0, true, false, true, "Normal", 100, 0, 0, true, true, {"baseColor", "roughness", "metalness"}});
        Layers.push_back({"Body details", "folder", 0, true, false, true, "Normal", 100, 2, 0, false, true, {}});
        Layers.push_back({"Edge wear", "paint", 1, true, false, true, "Multiply", 72, 0, 0, false, true, {"baseColor", "roughness"}});
        Layers.push_back({"Scratch normal", "paint", 1, true, true, true, "Normal", 100, 0, 0, false, true, {"normal", "height"}});
        Layers.push_back({"Maker's stamp", "decal", 0, true, false, true, "Normal", 100, 0, 2, false, true, {}});
        Layers.push_back({"Spout tint", "fill", 0, false, false, true, "Normal", 55, 0, 0, true, false, {"baseColor"}});
        Layers.push_back({"Procedural noise", "generator", 0, true, false, true, "Overlay", 38, 0, 0, false, true, {"roughness", "normal"}});
        SelectedLayer = 0;
        MaskSelected = false;
        Info = "Representative teapot document · layer paint/mask/decal compositing is not implemented in this native build.";
        return true;
    }
    catch (...) { return false; }
}
void TextureWorkspacePanel::FinalizeContent() noexcept { ControlExtents.clear(); }
void TextureWorkspacePanel::IntegrateContent(double) noexcept {}

PlaneExtent TextureWorkspacePanel::QueryControlExtent(const std::string &Caption) const
{
    const auto Position = ControlExtents.find(Caption);
    return Position == ControlExtents.end() ? PlaneExtent{} : Position->second;
}
void TextureWorkspacePanel::Track(const std::string &Caption)
{
    const ImVec2 Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
    ControlExtents[Caption] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
}

std::string TextureWorkspacePanel::QueryChannelKey(const std::string &ChannelId) const
{
    return std::to_string(SelectedLayer) + ":" + ChannelId;
}

void TextureWorkspacePanel::SelectLayer(int Index, bool ToggleMask)
{
    if (Index < 0 || Index >= int(Layers.size())) return;
    SelectedLayer = Index;
    MaskSelected = ToggleMask && Layers[size_t(Index)].HasMask;
    RightTab = InspectorTab::Properties;
}
void TextureWorkspacePanel::AddLayer(const std::string &Kind)
{
    if (Layers.size() >= 64) return;
    const auto &Meta = QueryKindMeta(Kind);
    TextureLayerRow Row;
    Row.Kind = Kind;
    Row.Name = Kind == "folder" ? "New folder" : std::string(Meta.Label) + " layer";
    if (Kind == "material" || Kind == "fill" || Kind == "paint") Row.Channels = {"baseColor", "roughness"};
    if (Kind == "decal") Row.DecalCount = 0;
    Layers.insert(Layers.begin(), Row);
    SelectLayer(0);
    AddLayerMenuOpen = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     HEADER (WORKSPACE SWITCHER)
//------------------------------------------------------------------------------------------------------------------------

void TextureWorkspacePanel::ConstructHeader()
{
    const auto *Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(Viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(Viewport->WorkSize.x, 43 * InterfaceScale));
    ImGui::Begin("Texture header", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoScrollbar);
    const auto Origin = ImGui::GetWindowPos();
    const float Width = ImGui::GetWindowWidth();
    const float S = InterfaceScale;
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + 43 * S), ImGui::GetColorU32(Spectrum(0x0b0b0b)));
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 16 * S, Origin.y + 13 * S));
    ImGui::TextColored(Spectrum(0xc5c5b8), "Teapot study");
    ImGui::SameLine();
    ImGui::TextColored(Spectrum(0x67675b), ".texture");

    Controls.ControlHeight = 24;
    const float NavigationX = Origin.x + std::max(230.f * S, Width - 620.f * S);
    const char *Workspaces[]{"Material", "Pattern", "Texture", "Stamp", "Baking"};
    const float Widths[]{64, 58, 54, 52, 64};
    float PositionX = NavigationX;
    for (size_t Index = 0; Index < 5; ++Index)
    {
        ImGui::SetCursorScreenPos(ImVec2(PositionX, Origin.y + 9 * S));
        ImGui::BeginDisabled((Index == 0 && !ActivateMaterial) || (Index == 1 && !ActivatePattern) || (Index == 3 && !ActivateStamp) ||
            (Index == 4 && !ActivateBaking));
        if (Controls.Button(Workspaces[Index], Widths[Index] * S, Index == 2, {}, true))
        {
            if (Index == 0 && ActivateMaterial) ActivateMaterial();
            if (Index == 1 && ActivatePattern) ActivatePattern();
            if (Index == 3 && ActivateStamp) ActivateStamp();
            if (Index == 4 && ActivateBaking) ActivateBaking();
        }
        Controls.Tooltip(Workspaces[Index], "Switch workspace without losing your document.");
        ImGui::EndDisabled();
        PositionX += Widths[Index] * S;
    }

    // 🧩 1:1 with StudioHeader's three texture-specific right-side children: the active digital brush preset
    // name (Brush button), an "Open project" icon button and a filled "Save project" pill.
    float ActionX = Origin.x + Width - 44 * S;
    {
        const float PillWidth = 108 * S, PillHeight = 25 * S;
        ActionX -= PillWidth + 8 * S;
        const ImVec2 Minimum(ActionX, Origin.y + (43 * S - PillHeight) * .5f), Maximum(Minimum.x + PillWidth, Minimum.y + PillHeight);
        ImGui::SetCursorScreenPos(Minimum);
        ImGui::InvisibleButton("SaveProject", ImVec2(PillWidth, PillHeight));
        const bool Hovered = ImGui::IsItemHovered();
        Ink->AddRectFilled(Minimum, Maximum, ImGui::GetColorU32(Spectrum(Hovered ? 0x2c2c29 : 0x232321)), 6 * S);
        Ink->AddRect(Minimum, Maximum, ImGui::GetColorU32(Spectrum(0xffffff, 0.05f)), 6 * S);
        StrokeIcon({Minimum.x + 10 * S, Minimum.y + (PillHeight - 11 * S) * .5f}, 11 * S, SavePath, 0xc9c9bb);
        Caption(ImVec2(Minimum.x + 26 * S, Minimum.y + (PillHeight - 9 * S) * .42f), "Save project", 9.5f * S, 0xc9c9bb);
        if (Hovered) ImGui::SetTooltip("Native project save is not wired up yet - this is UI chrome only.");
    }
    {
        const float ButtonSize = 28 * S;
        ActionX -= ButtonSize + 2 * S;
        const ImVec2 Minimum(ActionX, Origin.y + (43 * S - ButtonSize) * .5f), Maximum(Minimum.x + ButtonSize, Minimum.y + ButtonSize);
        ImGui::SetCursorScreenPos(Minimum);
        ImGui::InvisibleButton("OpenProject", ImVec2(ButtonSize, ButtonSize));
        const bool Hovered = ImGui::IsItemHovered();
        if (Hovered) Ink->AddRectFilled(Minimum, Maximum, ImGui::GetColorU32(Spectrum(0x1c1c1a)), 6 * S);
        StrokeIcon({Minimum.x + (ButtonSize - 14 * S) * .5f, Minimum.y + (ButtonSize - 11 * S) * .5f}, 14 * S, FolderOpenPath, 0xa8a89a);
        if (Hovered) ImGui::SetTooltip("Open project");
    }
    {
        const float PillWidth = 118 * S, PillHeight = 25 * S;
        ActionX -= PillWidth + 8 * S;
        const ImVec2 Minimum(ActionX, Origin.y + (43 * S - PillHeight) * .5f), Maximum(Minimum.x + PillWidth, Minimum.y + PillHeight);
        ImGui::SetCursorScreenPos(Minimum);
        ImGui::InvisibleButton("ActiveBrush", ImVec2(PillWidth, PillHeight));
        const bool Hovered = ImGui::IsItemHovered();
        Ink->AddRectFilled(Minimum, Maximum, ImGui::GetColorU32(Spectrum(Hovered ? 0x242420 : 0x1a1a18)), 6 * S);
        StrokeIcon({Minimum.x + 9 * S, Minimum.y + (PillHeight - 11 * S) * .5f}, 11 * S, BrushPath, 0xc7b98f);
        Caption(ImVec2(Minimum.x + 24 * S, Minimum.y + (PillHeight - 9 * S) * .42f), "Soft round", 9 * S, 0xc7b98f);
        if (Hovered) ImGui::SetTooltip("Digital brush settings (chrome only - no paint compositor behind this).");
    }

    ImGui::SetCursorScreenPos(ImVec2(Origin.x + Width - 44 * S, Origin.y + 9 * S));
    ImGui::BeginDisabled(!ActivateMaterial);
    if (ImGui::Button("X", ImVec2(28 * S, 24 * S)) && ActivateMaterial) ActivateMaterial();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close texture workspace");
    ImGui::EndDisabled();
    ImGui::End();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     LAYER STACK DOCK
//------------------------------------------------------------------------------------------------------------------------

void TextureWorkspacePanel::ConstructLayersDock()
{
    const float S = InterfaceScale;
    const auto Origin = ImGui::GetCursorScreenPos();
    const float Width = ImGui::GetContentRegionAvail().x;
    auto *Ink = ImGui::GetWindowDrawList();

    Caption(ImVec2(Origin.x + 16 * S, Origin.y + 15 * S), "Layers", 23 * S, 0xd9d9cc, LightTypeface);
    char CountLabel[32];
    std::snprintf(CountLabel, sizeof(CountLabel), "%zu / 64", Layers.size());
    Caption(ImVec2(Origin.x + 100 * S, Origin.y + 22 * S), CountLabel, 9 * S, 0x6f6f63);
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + Width - 70 * S, Origin.y + 13 * S));
    // 📝 The web app's "New folder" control is a round icon-only button (FolderPlus, no visible label) - the
    // web app's toolbar pairs it with a plain icon-only "+" (Add) button next to it; give it the same
    // treatment here rather than a text caption, which would overflow this narrow 28px slot.
    if (Controls.Button("New folder", 28 * S, false, FolderPlusPath)) AddLayer("folder");
    Controls.Tooltip("New folder", "Group the current selection into a layer folder.");
    ImGui::SameLine(0, 4 * S);
    if (Controls.Button("Add", 28 * S, AddLayerMenuOpen, PlusPath)) { AddLayerMenuOpen = !AddLayerMenuOpen; if (AddLayerMenuOpen) ImGui::OpenPopup("Add a texture layer"); }
    Controls.Tooltip("Add a layer", "Choose a layer kind to add above the current selection.");
    if (DropdownControlSurface::BeginMenu("Add a texture layer", S))
    {
        ImGui::TextDisabled("ADD A LAYER");
        for (const char *Kind : {"paint", "fill", "material", "generator", "decal", "folder"})
        {
            const auto &Meta = QueryKindMeta(Kind);
            char Label[64];
            std::snprintf(Label, sizeof(Label), "%s%s", Meta.Label, std::strcmp(Kind, "folder") == 0 ? "" : " layer");
            if (DropdownControlSurface::Option(Label, false, S)) { AddLayer(Kind); ImGui::CloseCurrentPopup(); }
        }
        DropdownControlSurface::EndMenu();
    }
    else AddLayerMenuOpen = false;

    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 8 * S, Origin.y + 56 * S));
    ImGui::BeginChild("Texture layers body", ImVec2(Width - 16 * S, 0), ImGuiChildFlags_None);

    size_t Visible = 0, Hidden = 0, Locked = 0;
    for (const auto &Layer : Layers) { if (Layer.Visible) ++Visible; else ++Hidden; if (Layer.Locked) ++Locked; }
    const size_t Counts[]{Visible, Hidden, Locked};
    const char *Captions[]{"Visible", "Hidden", "Locked"};
    const float StatWidth = (ImGui::GetContentRegionAvail().x - 8 * S) / 3;
    for (int Index = 0; Index < 3; ++Index)
    {
        if (Index) ImGui::SameLine(0, 4 * S);
        char Label[32];
        std::snprintf(Label, sizeof(Label), "%02zu\n%s", Counts[Index], Captions[Index]);
        Controls.ControlHeight = 46;
        if (Controls.Button(Label, StatWidth, LayerFilter == Index + 1, {}, true)) LayerFilter = LayerFilter == Index + 1 ? 0 : Index + 1;
    }
    Controls.ControlHeight = 30;
    ImGui::Spacing();
    char QueryBuffer[96];
    std::snprintf(QueryBuffer, sizeof(QueryBuffer), "%s", LayerQuery.c_str());
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##FindTextureLayer", "Find a layer…", QueryBuffer, sizeof(QueryBuffer))) LayerQuery = QueryBuffer;
    if (LayerFilter)
    {
        ImGui::TextColored(Spectrum(0x8d8d7c), "%s layers", Captions[LayerFilter - 1]);
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear filter")) LayerFilter = 0;
    }
    ImGui::Dummy(ImVec2(1, 6 * S));
    // 📝 The loaded UI typeface only covers the default ASCII glyph range (see TypefaceRegistry::Acquire),
    // so a real Unicode arrow glyph here renders as the ImGui "missing glyph" fallback ('?'). Every other
    // panel in this codebase sticks to ASCII for exactly this reason - do the same.
    ImGui::TextColored(Spectrum(0x7c7c6d), "TOP -> BOTTOM");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 56 * S + ImGui::GetCursorPosX());
    ImGui::TextColored(Spectrum(0x7c7c6d), "%zu shown", Layers.size());
    ImGui::Dummy(ImVec2(1, 4 * S));

    int SkipDepthBelow = -1; // when set, hide rows with Depth > this until a sibling/ancestor resets it
    for (int Index = 0; Index < int(Layers.size()); ++Index)
    {
        auto &Layer = Layers[size_t(Index)];
        if (SkipDepthBelow >= 0)
        {
            if (Layer.Depth > SkipDepthBelow) continue;
            SkipDepthBelow = -1;
        }
        if (LayerFilter == 1 && !Layer.Visible) continue;
        if (LayerFilter == 2 && Layer.Visible) continue;
        if (LayerFilter == 3 && !Layer.Locked) continue;
        if (!LayerQuery.empty())
        {
            std::string Folded = Layer.Name;
            std::transform(Folded.begin(), Folded.end(), Folded.begin(), ::tolower);
            std::string Needle = LayerQuery;
            std::transform(Needle.begin(), Needle.end(), Needle.begin(), ::tolower);
            if (Folded.find(Needle) == std::string::npos) continue;
        }
        if (Layer.Kind == "folder" && !Layer.Open) SkipDepthBelow = Layer.Depth;

        const auto &Meta = QueryKindMeta(Layer.Kind);
        const float RowHeight = 52 * S;
        const auto RowOrigin = ImGui::GetCursorScreenPos();
        const float RowWidth = ImGui::GetContentRegionAvail().x;
        const bool Selected = SelectedLayer == Index && !MaskSelected;
        Ink->AddRectFilled(RowOrigin, ImVec2(RowOrigin.x + RowWidth, RowOrigin.y + RowHeight),
            ImGui::GetColorU32(Spectrum(Layer.Visible ? (Selected ? 0x28271f : 0x201f1b) : 0x1a1a17)), 8 * S);
        if (Selected) Ink->AddRect(RowOrigin, ImVec2(RowOrigin.x + RowWidth, RowOrigin.y + RowHeight), ImGui::GetColorU32(Spectrum(0x5b523b)), 8 * S);
        ImGui::PushID(Index);
        const float Indent = float(Layer.Depth) * 16 * S;

        ImGui::SetCursorScreenPos(ImVec2(RowOrigin.x + 4 * S + Indent, RowOrigin.y + (RowHeight - 14 * S) * .5f));
        StrokeIcon(ImGui::GetCursorScreenPos(), 12 * S, GripPath, 0x55554a);
        ImGui::Dummy(ImVec2(14 * S, 14 * S));

        float TextX = RowOrigin.x + 24 * S + Indent;
        if (Layer.Kind == "folder")
        {
            const ImVec2 Minimum(TextX, RowOrigin.y + (RowHeight - 18 * S) * .5f);
            ImGui::SetCursorScreenPos(Minimum);
            if (ImGui::InvisibleButton("Disclosure", ImVec2(18 * S, 18 * S))) Layer.Open = !Layer.Open;
            Track(std::string("TextureLayerDisclosure:") + std::to_string(Index));
            StrokeIcon(ImVec2(Minimum.x + 3 * S, Minimum.y + 3 * S), 12 * S, Layer.Open ? ChevronDownPath : ChevronRightPath, 0x9a9a8c);
            TextX += 22 * S;
        }

        const ImVec2 ThumbMin(TextX, RowOrigin.y + (RowHeight - 34 * S) * .5f), ThumbMax(ThumbMin.x + 34 * S, ThumbMin.y + 34 * S);
        Ink->AddRectFilled(ThumbMin, ThumbMax, ImGui::GetColorU32(Spectrum(0x161614)), 6 * S);
        Ink->AddRectFilled(ImVec2(ThumbMin.x, ThumbMin.y), ImVec2(ThumbMax.x, ThumbMin.y + 4 * S), ImGui::GetColorU32(Spectrum(Meta.Colour)), 6 * S);
        Caption(ImVec2((ThumbMin.x + ThumbMax.x) * .5f - 4 * S, (ThumbMin.y + ThumbMax.y) * .5f - 2 * S), Meta.Glyph, 11 * S, Meta.Colour);

        const ImVec2 SelectMin(ThumbMax.x + 8 * S, RowOrigin.y), SelectMax(RowOrigin.x + RowWidth - 62 * S, RowOrigin.y + RowHeight);
        ImGui::SetCursorScreenPos(SelectMin);
        if (ImGui::InvisibleButton("Select", ImVec2(std::max(10.f, SelectMax.x - SelectMin.x), RowHeight)))
            SelectLayer(Index, false);
        Track(std::string("TextureLayerSelect:") + std::to_string(Index));
        Ink->PushClipRect(SelectMin, SelectMax, true);
        Caption(ImVec2(SelectMin.x, RowOrigin.y + 10 * S), Layer.Name.c_str(), 11 * S, Layer.Visible ? 0xd5d5c4 : 0x7a7a6c);
        char Meta2[96];
        if (Layer.Kind == "folder")
            std::snprintf(Meta2, sizeof(Meta2), "%d item%s", Layer.ChildCount, Layer.ChildCount == 1 ? "" : "s");
        else
            std::snprintf(Meta2, sizeof(Meta2), "%s \xC2\xB7 %s \xC2\xB7 %d%%", Meta.Label, Layer.Blend.c_str(), Layer.Opacity);
        Caption(ImVec2(SelectMin.x, RowOrigin.y + 30 * S), Meta2, 8.5f * S, 0x8f8f7e);
        Ink->PopClipRect();

        ImGui::SetCursorScreenPos(ImVec2(RowOrigin.x + RowWidth - 56 * S, RowOrigin.y + (RowHeight - 20 * S) * .5f));
        if (ImGui::InvisibleButton("Eye", ImVec2(20 * S, 20 * S))) Layer.Visible = !Layer.Visible;
        Track(std::string("TextureLayerEye:") + std::to_string(Index));
        StrokeIcon(ImGui::GetItemRectMin(), 14 * S, Layer.Visible ? EyePath : EyeOffPath, Layer.Visible ? 0xb6b6a6 : 0x5f5f54);
        Controls.Tooltip(Layer.Visible ? "Hide layer" : "Show layer", Layer.Visible ? "Hide layer" : "Show layer");

        ImGui::SetCursorScreenPos(ImVec2(RowOrigin.x + RowWidth - 28 * S, RowOrigin.y + (RowHeight - 20 * S) * .5f));
        if (ImGui::InvisibleButton("Lock", ImVec2(20 * S, 20 * S))) Layer.Locked = !Layer.Locked;
        Track(std::string("TextureLayerLock:") + std::to_string(Index));
        StrokeIcon(ImGui::GetItemRectMin(), 13 * S, Layer.Locked ? LockPath : UnlockPath, Layer.Locked ? 0xc9a36b : 0x5f5f54);
        Controls.Tooltip(Layer.Locked ? "Unlock layer" : "Lock layer", Layer.Locked ? "Unlock layer" : "Lock layer");

        ImGui::SetCursorScreenPos(ImVec2(RowOrigin.x, RowOrigin.y + RowHeight + 3 * S));
        ImGui::PopID();

        // 📝 1:1 with the web layer row's nested mask chip (Colour mask / Layer mask indicator) - a thin
        // selectable sub-row under the layer, present when that layer carries a mask.
        if (Layer.HasMask && Layer.Open)
        {
            const auto MaskOrigin = ImGui::GetCursorScreenPos();
            const float MaskHeight = 24 * S;
            const bool MaskRowSelected = SelectedLayer == Index && MaskSelected;
            Ink->AddRectFilled(MaskOrigin, ImVec2(MaskOrigin.x + RowWidth, MaskOrigin.y + MaskHeight),
                ImGui::GetColorU32(Spectrum(MaskRowSelected ? 0x24241c : 0x1b1b18)), 6 * S);
            ImGui::PushID(Index + 10000);
            ImGui::SetCursorScreenPos(MaskOrigin);
            if (ImGui::InvisibleButton("MaskSelect", ImVec2(RowWidth, MaskHeight))) SelectLayer(Index, true);
            Track(std::string("TextureLayerMask:") + std::to_string(Index));
            Caption(ImVec2(MaskOrigin.x + 36 * S + Indent, MaskOrigin.y + 6 * S), Layer.MaskEnabled ? "Layer mask" : "Layer mask (bypassed)",
                9.5f * S, MaskRowSelected ? 0xd5d5c4 : 0x8f8f7e);
            ImGui::PopID();
            ImGui::SetCursorScreenPos(ImVec2(MaskOrigin.x, MaskOrigin.y + MaskHeight + 3 * S));
        }
    }
    if (Layers.empty()) ImGui::TextColored(Spectrum(0x75756a), "No layers yet - use + to add one.");
    // 📝 The loop above ends with a bare SetCursorScreenPos (positioning the *next* row that never comes on
    // the last iteration) - submit a zero-size item here so the child's content rect actually grows to match,
    // instead of leaving ImGui's cursor past every real item and tripping its "extends window boundaries"
    // recoverable-error check.
    ImGui::Dummy(ImVec2(1, 1));
    ImGui::EndChild();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     VIEWPORT DOCK (CHROME ONLY - NO REAL 3D CANVAS)
//------------------------------------------------------------------------------------------------------------------------

void TextureWorkspacePanel::ConstructViewportDock()
{
    const float S = InterfaceScale;
    const auto Origin = ImGui::GetCursorScreenPos();
    const float Width = ImGui::GetContentRegionAvail().x;
    auto *Ink = ImGui::GetWindowDrawList();

    // --- .tp-viewport-toolbar --------------------------------------------------------------------------------
    StrokeIcon(ImVec2(Origin.x + 14 * S, Origin.y + 12 * S), 13 * S, StampPath, 0x9a9a8c);
    Caption(ImVec2(Origin.x + 34 * S, Origin.y + 9 * S), "Teapot", 10.5f * S, 0xd5d5c4);
    Ink->AddRectFilled(ImVec2(Origin.x + 84 * S, Origin.y + 9 * S), ImVec2(Origin.x + 140 * S, Origin.y + 25 * S), ImGui::GetColorU32(Spectrum(0x2a2a1e)), 4 * S);
    Caption(ImVec2(Origin.x + 90 * S, Origin.y + 13 * S), "PREVIEW", 7.5f * S, 0xb9a66a);

    Controls.ControlHeight = 26;
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + Width - 428 * S, Origin.y + 8 * S));
    ImGui::BeginGroup();
    {
        const char *ShadingLabels[]{"Lit", "Wireframe", "Matcap"};
        int ShadingIndex = int(Shading);
        ImGui::SetNextItemWidth(92 * S);
        if (Controls.Dropdown("Shading", ShadingIndex, ShadingLabels)) Shading = ShadingMode(ShadingIndex);
        ImGui::SameLine(0, 6 * S);
        const char *TargetLabels[]{"Whole object", "Lid", "Body", "Handle", "Spout"};
        ImGui::SetNextItemWidth(110 * S);
        Controls.Dropdown("Target", TargetPart, TargetLabels);
        ImGui::SameLine(0, 6 * S);
        const char *UVLabels[]{"UV0", "UV1"};
        ImGui::SetNextItemWidth(66 * S);
        Controls.Dropdown("UV set", UVSet, UVLabels);
        ImGui::SameLine(0, 10 * S);
        if (Controls.Button("Frame", 28 * S, false, FramePath)) Info = "Frame view (chrome only - no live camera behind this).";
        Controls.Tooltip("Frame selection", "Frame selection");
        ImGui::SameLine(0, 4 * S);
        if (Controls.Button("Grid", 28 * S, GridVisible, GridPath)) GridVisible = !GridVisible;
        Controls.Tooltip("Toggle grid", "Toggle grid");
        ImGui::SameLine(0, 4 * S);
        if (Controls.Button("Reset", 28 * S, false, ResetPath)) ZoomPercent = 100;
        Controls.Tooltip("Reset view", "Reset view");
    }
    ImGui::EndGroup();
    Controls.ControlHeight = 30;

    // --- .tp-authoring-bar -------------------------------------------------------------------------------------
    float BarY = Origin.y + 40 * S;
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 12 * S, BarY));
    ImGui::BeginGroup();
    {
        const char *Tools[]{"Navigate", "Stamp", "Transform"};
        for (int Index = 0; Index < 3; ++Index)
        {
            if (Index) ImGui::SameLine(0, 2 * S);
            if (Controls.Button(Tools[Index], 70 * S, int(Tool) == Index, {}, true)) Tool = AuthoringTool(Index);
        }
        ImGui::SameLine(0, 10 * S);
        const char *DigitalTools[]{"Paint", "Erase", "Fill"};
        const char *DigitalIcons[]{BrushPath, EraserPath, BucketPath};
        for (int Index = 0; Index < 3; ++Index)
        {
            if (Index) ImGui::SameLine(0, 2 * S);
            if (Controls.Button(DigitalTools[Index], 60 * S, int(Digital) == Index + 1, DigitalIcons[Index]))
                Digital = Digital == DigitalTool(Index + 1) ? DigitalTool::None : DigitalTool(Index + 1);
        }
        ImGui::SameLine(0, 4 * S);
        char BrushLabel[32];
        std::snprintf(BrushLabel, sizeof(BrushLabel), "%d px", int(BrushSize));
        if (Controls.Button(BrushLabel, 56 * S, MaskOptionsOpen, SlidersPath)) MaskOptionsOpen = !MaskOptionsOpen;
        Controls.Tooltip("Digital brush settings", "Digital brush settings (chrome only).");
        ImGui::SameLine(0, 10 * S);
        // 📝 "##PreviewMode" keeps these two distinct from the header's "Material"/"Pattern"/"Texture"/"Stamp"/
        // "Baking" workspace-switch pills in the shared caption->extent registry (Controls.Button's Caption
        // doubles as its ImGui ID and QueryControlExtent key) - ImGui renders only the text before "##".
        if (Controls.Button("Material##PreviewMode", 62 * S, Preview == PreviewMode::Material, {}, true)) Preview = PreviewMode::Material;
        ImGui::SameLine(0, 2 * S);
        if (Controls.Button("Mask##PreviewMode", 50 * S, Preview == PreviewMode::Mask, {}, true)) Preview = PreviewMode::Mask;
    }
    ImGui::EndGroup();
    if (MaskOptionsOpen)
    {
        ImGui::SetCursorScreenPos(ImVec2(Origin.x + 12 * S, BarY + 32 * S));
        ImGui::SetNextItemWidth(220 * S);
        ImGui::SliderFloat("Brush size", &BrushSize, 2.f, 256.f, "%.0f px");
        BarY += 28 * S;
    }
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 12 * S, BarY + 32 * S));
    ImGui::TextColored(Spectrum(0x87876f), "%s", Digital != DigitalTool::None ? "Digital tool armed · click the surface to apply" :
        Tool == AuthoringTool::Stamp ? "Stamp placement · click the surface to anchor a decal" :
        Tool == AuthoringTool::Transform ? "Transform · drag handles to move / rotate / scale" : "Navigate · drag to orbit");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 96 * S + ImGui::GetCursorPosX());
    if (Controls.Button("Stamp studio ->", 94 * S, false))
    {
        if (ActivateStamp) ActivateStamp();
    }
    Controls.Tooltip("Open Stamp studio", "Open the Stamp workspace to author reusable decal artwork.");

    // --- mask toolbar (only when the selected row is a mask) -----------------------------------------------
    float ChromeY = BarY + 62 * S;
    if (MaskSelected)
    {
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, ChromeY));
        ConstructMaskToolbar(Width);
        ChromeY = ImGui::GetCursorScreenPos().y;
    }

    // --- .tp-scene-viewport (static placeholder - the real 3D canvas is explicitly out of scope) -----------
    const ImVec2 SceneMin(Origin.x + 8 * S, ChromeY + 6 * S), SceneMax(Origin.x + Width - 8 * S, Origin.y + ImGui::GetContentRegionAvail().y + (ImGui::GetCursorScreenPos().y - Origin.y) - 34 * S);
    const ImVec2 ActualMax(SceneMin.x + std::max(120.f, SceneMax.x - SceneMin.x), std::max(SceneMin.y + 120 * S, SceneMax.y));
    Ink->AddRectFilled(SceneMin, ActualMax, ImGui::GetColorU32(Spectrum(0x141412)), 10 * S);
    Ink->AddRect(SceneMin, ActualMax, ImGui::GetColorU32(Spectrum(0xffffff, 0.04f)), 10 * S);
    if (GridVisible)
    {
        const float Step = 24 * S;
        for (float X = SceneMin.x + std::fmod(ActualMax.x - SceneMin.x, Step) * .5f; X < ActualMax.x; X += Step)
            Ink->AddLine(ImVec2(X, SceneMin.y), ImVec2(X, ActualMax.y), ImGui::GetColorU32(Spectrum(0xffffff, 0.015f)));
        for (float Y = SceneMin.y + std::fmod(ActualMax.y - SceneMin.y, Step) * .5f; Y < ActualMax.y; Y += Step)
            Ink->AddLine(ImVec2(SceneMin.x, Y), ImVec2(ActualMax.x, Y), ImGui::GetColorU32(Spectrum(0xffffff, 0.015f)));
    }
    Caption(ImVec2((SceneMin.x + ActualMax.x) * .5f - 94 * S, (SceneMin.y + ActualMax.y) * .5f - 7 * S),
        "3D viewport not hosted in this native build", 10 * S, 0x5f5f55);

    // .tp-scene-caption
    Ink->AddCircleFilled(ImVec2(SceneMin.x + 14 * S, SceneMin.y + 16 * S), 2.5f * S, ImGui::GetColorU32(Spectrum(0x8aa27e)));
    Caption(ImVec2(SceneMin.x + 22 * S, SceneMin.y + 11 * S), "TEAPOT STUDY", 8.5f * S, 0xaeae9c);
    Caption(ImVec2(SceneMin.x + 22 * S, SceneMin.y + 23 * S), "01 / DEFAULT MESH", 7.5f * S, 0x6c6c5e);

    // .tp-scene-nav (zoom readout)
    const ImVec2 NavMin(ActualMax.x - 96 * S, SceneMin.y + 8 * S);
    Ink->AddRectFilled(NavMin, ImVec2(NavMin.x + 86 * S, NavMin.y + 24 * S), ImGui::GetColorU32(Spectrum(0x1b1b18)), 12 * S);
    ImGui::SetCursorScreenPos(ImVec2(NavMin.x + 4 * S, NavMin.y + 3 * S));
    if (ImGui::InvisibleButton("ZoomOut", ImVec2(18 * S, 18 * S))) ZoomPercent = std::max(25, ZoomPercent - 10);
    StrokeIcon(ImGui::GetItemRectMin(), 11 * S, "M5 12H19", 0xb6b6a6);
    char ZoomLabel[16];
    std::snprintf(ZoomLabel, sizeof(ZoomLabel), "%d%%", ZoomPercent);
    Caption(ImVec2(NavMin.x + 28 * S, NavMin.y + 6 * S), ZoomLabel, 9 * S, 0xc5c5b4);
    ImGui::SetCursorScreenPos(ImVec2(NavMin.x + 64 * S, NavMin.y + 3 * S));
    if (ImGui::InvisibleButton("ZoomIn", ImVec2(18 * S, 18 * S))) ZoomPercent = std::min(400, ZoomPercent + 10);
    StrokeIcon(ImGui::GetItemRectMin(), 11 * S, PlusPath, 0xb6b6a6);

    // .tp-view-axis (static gizmo)
    const ImVec2 AxisCentre(ActualMax.x - 40 * S, ActualMax.y - 54 * S);
    Ink->AddLine(ImVec2(AxisCentre.x, AxisCentre.y - 20 * S), AxisCentre, ImGui::GetColorU32(Spectrum(0x666666)));
    Ink->AddLine(ImVec2(AxisCentre.x + 20 * S, AxisCentre.y + 10 * S), AxisCentre, ImGui::GetColorU32(Spectrum(0x666666)));
    Ink->AddLine(ImVec2(AxisCentre.x - 20 * S, AxisCentre.y + 10 * S), AxisCentre, ImGui::GetColorU32(Spectrum(0x666666)));
    Ink->AddCircleFilled(ImVec2(AxisCentre.x, AxisCentre.y - 20 * S), 7 * S, ImGui::GetColorU32(Spectrum(0x253b2d)));
    Caption(ImVec2(AxisCentre.x - 3 * S, AxisCentre.y - 24 * S), "Y", 7.5f * S, 0x95bca1);
    Ink->AddCircleFilled(ImVec2(AxisCentre.x + 20 * S, AxisCentre.y + 10 * S), 7 * S, ImGui::GetColorU32(Spectrum(0x44302e)));
    Caption(ImVec2(AxisCentre.x + 17 * S, AxisCentre.y + 6 * S), "X", 7.5f * S, 0xc18e85);
    Ink->AddCircleFilled(ImVec2(AxisCentre.x - 20 * S, AxisCentre.y + 10 * S), 7 * S, ImGui::GetColorU32(Spectrum(0x2b3346)));
    Caption(ImVec2(AxisCentre.x - 23 * S, AxisCentre.y + 6 * S), "Z", 7.5f * S, 0x97a7c8);

    Caption(ImVec2(SceneMin.x + 14 * S, ActualMax.y - 18 * S), "Drag to orbit \xC2\xB7 scroll to zoom", 8 * S, 0x66665a);

    // .tp-viewport-footer
    ImGui::SetCursorScreenPos(ImVec2(Origin.x, ActualMax.y + 4 * S));
    ImGui::TextColored(Spectrum(0x9a9a8f), "Preparing scene");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 240 * S + ImGui::GetCursorPosX());
    ImGui::TextColored(Spectrum(0x716f62), "Layer stack live \xC2\xB7 top layers cover lower decals");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     MASK TOOLBAR (TextureMaskToolbar.jsx)
//------------------------------------------------------------------------------------------------------------------------

void TextureWorkspacePanel::ConstructMaskToolbar(float Width)
{
    const float S = InterfaceScale;
    const auto Origin = ImGui::GetCursorScreenPos();
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + 70 * S), ImGui::GetColorU32(Spectrum(0x171714)), 6 * S);
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 8 * S, Origin.y + 6 * S));
    ImGui::BeginGroup();
    {
        const char *Scopes[]{"Face", "UV Tile", "UV Island", "Object"};
        static int ScopeSelection = 0;
        for (int Index = 0; Index < 4; ++Index)
        {
            if (Index) ImGui::SameLine(0, 2 * S);
            if (Controls.Button(Scopes[Index], 68 * S, ScopeSelection == Index, {}, true)) ScopeSelection = Index;
        }
        ImGui::SameLine(0, 8 * S);
        if (Controls.Button("Paint mask", 82 * S, Digital == DigitalTool::Paint, BrushPath)) Digital = Digital == DigitalTool::Paint ? DigitalTool::None : DigitalTool::Paint;
        ImGui::SameLine(0, 4 * S);
        Controls.Button("Fill selection", 92 * S, false);
        ImGui::SameLine(0, 4 * S);
        Controls.Button("Clear", 54 * S, false);
    }
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 8 * S, Origin.y + 38 * S));
    ImGui::BeginGroup();
    {
        static float Size = 4.f, Opacity = 100.f, Hardness = 80.f;
        ImGui::SetNextItemWidth(120 * S);
        ImGui::SliderFloat("Size##Mask", &Size, 0.2f, 25.f, "%.1f%%");
        ImGui::SameLine(0, 10 * S);
        ImGui::SetNextItemWidth(120 * S);
        ImGui::SliderFloat("Opacity##Mask", &Opacity, 0.f, 100.f, "%.0f%%");
        ImGui::SameLine(0, 10 * S);
        ImGui::SetNextItemWidth(120 * S);
        ImGui::SliderFloat("Hardness##Mask", &Hardness, 0.f, 100.f, "%.0f%%");
    }
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 72 * S));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     INSPECTOR DOCK (TAB STRIP + SUB-VIEWS)
//------------------------------------------------------------------------------------------------------------------------

void TextureWorkspacePanel::ConstructInspectorDock()
{
    const float S = InterfaceScale;
    const float Width = ImGui::GetContentRegionAvail().x;
    const auto Origin = ImGui::GetCursorScreenPos();
    if (Controls.Button("Inspector", (Width - 8 * S) * .5f, RightTab == InspectorTab::Properties, SlidersPath)) RightTab = InspectorTab::Properties;
    ImGui::SameLine(0, 4 * S);
    if (Controls.Button("History", (Width - 8 * S) * .5f, RightTab == InspectorTab::History, HistoryPath)) RightTab = InspectorTab::History;
    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 36 * S));
    ImGui::BeginChild("Texture inspector body", ImVec2(0, 0), ImGuiChildFlags_None);
    if (RightTab == InspectorTab::Properties) ConstructInspectorProperties(Width);
    else ConstructInspectorHistory(Width);
    ImGui::EndChild();
}

void TextureWorkspacePanel::ConstructInspectorProperties(float Width)
{
    const float S = InterfaceScale;
    if (Layers.empty() || SelectedLayer < 0 || SelectedLayer >= int(Layers.size()))
    {
        ImGui::Dummy(ImVec2(1, 40 * S));
        ImGui::TextColored(Spectrum(0x75756a), "No layer selected");
        ImGui::TextWrapped("Add a layer or select an existing one to inspect its settings.");
        return;
    }
    auto &Layer = Layers[size_t(SelectedLayer)];
    const auto &Meta = QueryKindMeta(Layer.Kind);

    ImGui::TextColored(Spectrum(0x7c7c6d), "Teapot / %s", MaskSelected ? "Layer mask" : Layer.Kind == "folder" ? "Layer folder" : "Surface layer");
    ImGui::Dummy(ImVec2(1, 8 * S));
    const auto IdentityOrigin = ImGui::GetCursorScreenPos();
    auto *Ink = ImGui::GetWindowDrawList();
    const ImVec2 IconMin(IdentityOrigin.x, IdentityOrigin.y), IconMax(IconMin.x + 38 * S, IconMin.y + 38 * S);
    Ink->AddRectFilled(IconMin, IconMax, ImGui::GetColorU32(Spectrum(0x161614)), 8 * S);
    Caption(ImVec2((IconMin.x + IconMax.x) * .5f - 4 * S, (IconMin.y + IconMax.y) * .5f - 6 * S), MaskSelected ? "#" : Meta.Glyph, 13 * S, Meta.Colour);
    Caption(ImVec2(IconMax.x + 10 * S, IdentityOrigin.y), MaskSelected ? "MASK PROPERTIES" : (std::string(Meta.Label) + " LAYER").c_str(), 7.5f * S, 0x7c7c6d);
    Caption(ImVec2(IconMax.x + 10 * S, IdentityOrigin.y + 12 * S), MaskSelected ? "Layer mask" : Layer.Name.c_str(), 12 * S, 0xd9d9cc);
    char Position[64];
    std::snprintf(Position, sizeof(Position), MaskSelected ? "On %s" : "Position %d of %zu \xC2\xB7 top to bottom", MaskSelected ? Layer.Name.c_str() : nullptr, SelectedLayer + 1, Layers.size());
    if (MaskSelected) std::snprintf(Position, sizeof(Position), "On %s", Layer.Name.c_str());
    Caption(ImVec2(IconMax.x + 10 * S, IdentityOrigin.y + 26 * S), Position, 8.5f * S, 0x8f8f7e);
    ImGui::SetCursorScreenPos(ImVec2(IdentityOrigin.x, IdentityOrigin.y + 46 * S));

    if (Controls.Button(Layer.Visible ? "Visible" : "Hidden", 92 * S, false, Layer.Visible ? EyePath : EyeOffPath)) Layer.Visible = !Layer.Visible;
    ImGui::SameLine(0, 6 * S);
    if (Controls.Button(Layer.Locked ? "Locked" : "Unlocked", 92 * S, false, Layer.Locked ? LockPath : UnlockPath)) Layer.Locked = !Layer.Locked;
    ImGui::Dummy(ImVec2(1, 10 * S));

    if (MaskSelected)
    {
        ImGui::TextColored(Spectrum(0x7c7c6d), "MASK SETTINGS");
        Controls.BeginCard("", true);
        {
            Caption(ImGui::GetCursorScreenPos(), "Coverage mask", 10 * S, 0xd9d4c4);
            ImGui::Dummy(ImVec2(1, 14 * S));
            const char *Kinds[]{"Fill", "Paint", "Colour", "Gradient"};
            static int MaskKind = 1;
            for (int Index = 0; Index < 4; ++Index)
            {
                if (Index) ImGui::SameLine(0, 4 * S);
                if (Controls.Button(Kinds[Index], 0, MaskKind == Index, {}, true)) MaskKind = Index;
            }
            ImGui::Dummy(ImVec2(1, 6 * S));
            static float Strength = 100.f;
            Controls.Number("Strength", Strength, 0, 100);
            ImGui::BeginDisabled(!Layer.MaskEnabled);
            ImGui::EndDisabled();
            Controls.Toggle("Enabled", Layer.MaskEnabled);
            static bool Inverted = false;
            Controls.Toggle("Invert", Inverted);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
            ImGui::TextColored(Spectrum(0x868872), "Mask authoring is UI chrome only in this native build - no coverage raster is produced yet.");
            ImGui::PopTextWrapPos();
        }
        Controls.EndCard();
        return;
    }

    if (Layer.Kind == "folder")
    {
        ImGui::TextColored(Spectrum(0x7c7c6d), "FOLDER SETTINGS");
        Controls.BeginCard("", true);
        {
            char NameBuffer[64];
            std::snprintf(NameBuffer, sizeof(NameBuffer), "%s", Layer.Name.c_str());
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##FolderName", NameBuffer, sizeof(NameBuffer))) Layer.Name = NameBuffer;
            const char *Blends[]{"Normal", "Multiply", "Screen", "Overlay"};
            int BlendIndex = 0;
            for (int Index = 0; Index < 4; ++Index) if (Layer.Blend == Blends[Index]) BlendIndex = Index;
            if (Controls.Dropdown("Blend", BlendIndex, Blends)) Layer.Blend = Blends[BlendIndex];
            float Opacity = float(Layer.Opacity);
            if (Controls.Number("Opacity", Opacity, 0, 100)) Layer.Opacity = int(Opacity);
            ImGui::TextColored(Spectrum(0x8f8f7e), "%d layer%s inside this folder", Layer.ChildCount, Layer.ChildCount == 1 ? "" : "s");
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
            ImGui::TextColored(Spectrum(0x868872), "A layer group with its own blend, opacity and optional coverage mask.");
            ImGui::PopTextWrapPos();
        }
        Controls.EndCard();
        return;
    }

    ImGui::TextColored(Spectrum(0x7c7c6d), "LAYER SETTINGS");
    Controls.BeginCard("", true);
    {
        const char *Blends[]{"Normal", "Multiply", "Screen", "Overlay"};
        int BlendIndex = 0;
        for (int Index = 0; Index < 4; ++Index) if (Layer.Blend == Blends[Index]) BlendIndex = Index;
        if (Controls.Dropdown("Blend", BlendIndex, Blends)) Layer.Blend = Blends[BlendIndex];
        float Opacity = float(Layer.Opacity);
        if (Controls.Number("Opacity", Opacity, 0, 100)) Layer.Opacity = int(Opacity);
    }
    Controls.EndCard();

    if (Layer.Kind == "decal")
    {
        ImGui::TextColored(Spectrum(0x7c7c6d), "DECAL COMPONENTS \xC2\xB7 %d", Layer.DecalCount);
        Controls.BeginCard("", true);
        {
            if (Controls.Button("Add decal component", 0, false, PlusPath)) ++Layer.DecalCount;
            for (int Index = 0; Index < Layer.DecalCount; ++Index)
            {
                ImGui::PushID(Index);
                char Label[48];
                std::snprintf(Label, sizeof(Label), "Decal component %d", Index + 1);
                ImGui::Selectable(Label, false);
                ImGui::PopID();
            }
            if (!Layer.DecalCount) ImGui::TextColored(Spectrum(0x75756a), "No decal components yet.");
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
            ImGui::TextColored(Spectrum(0x868872), "Surface-projected stamps - placement, re-anchoring and gizmo transforms are not implemented in this native build.");
            ImGui::PopTextWrapPos();
        }
        Controls.EndCard();
    }

    if (!Layer.Channels.empty())
    {
        ImGui::TextColored(Spectrum(0x7c7c6d), "CHANNEL TARGETS \xC2\xB7 %zu", Layer.Channels.size());
        Controls.BeginCard("", true);
        {
            for (const auto &Id : Layer.Channels)
            {
                ImGui::TextColored(Spectrum(0xbcbcae), "%s", Id.c_str());
                ImGui::SameLine();
            }
            ImGui::NewLine();
            if (Controls.Button("Add channel", 0, false, PlusPath)) {}
            ImGui::SameLine(0, 4 * S);
            Controls.Button("Clear all", 0, false);
        }
        Controls.EndCard();
        struct ChannelMeta { const char *Id; const char *Label; uint32_t Tint; const char *EditKind; const char *Note; float Minimum, Maximum, Default; };
        static const ChannelMeta Table[]{
            {"baseColor", "Base color", kBaseColourTint, "color", "Flat colour source for this channel.", 0.f, 1.f, 0.f},
            {"roughness", "Roughness", kRoughnessTint, "scalar", "Controls micro-facet spread (0 = mirror, 1 = matte).", 0.f, 1.f, .5f},
            {"metalness", "Metalness", kMetalnessTint, "scalar", "Dielectric to conductor blend.", 0.f, 1.f, 0.f},
            {"normal", "Normal", kNormalTint, "derived", "Normal follows the Height target.", 0.f, 1.f, 0.f},
            {"height", "Height", kHeightTint, "scalar", "Surface displacement source.", 0.f, 1.f, .5f},
            {"opacity", "Opacity", kOpacityTint, "scalar", "Per-texel coverage.", 0.f, 1.f, 1.f},
        };
        for (const auto &Entry : Table)
            if (std::find(Layer.Channels.begin(), Layer.Channels.end(), Entry.Id) != Layer.Channels.end())
                ConstructChannelCard(Width, Entry.Id, Entry.Label, Entry.Tint, Entry.EditKind, Entry.Note, Entry.Minimum, Entry.Maximum, Entry.Default);
    }

    ImGui::Dummy(ImVec2(1, 6 * S));
    if (Controls.Button("Add layer mask", 0, false, "M6 10H18V21H6ZM8 10V7A4 4 0 0 1 16 7V10")) Layer.HasMask = true;
    ImGui::SameLine(0, 6 * S);
    Controls.Button("Add colour mask", 0, false);
}

void TextureWorkspacePanel::ConstructChannelCard(float Width, const std::string &Id, const char *Label, uint32_t Tint,
    const char *EditKind, const char *Note, float Minimum, float Maximum, float Default)
{
    const float S = InterfaceScale;
    const bool Derived = std::strcmp(EditKind, "derived") == 0;
    const bool Folded = FoldedChannels.count(Id) != 0;
    // 📝 Every nested control below (fold toggle, the Texture/Colour/Generator switch, the swatch/slider/
    // dropdown bodies) reuses the exact same literal captions across every channel card ("Texture", "Amount",
    // "Generator"...) - without this PushID scope they'd all resolve to the same ImGui ID within this shared
    // "Channel targets" card and Dear ImGui would raise its "2 visible items with conflicting ID" programmer
    // error the moment two channel cards are open at once. Scoping the whole card by the channel's stable id
    // (not the display Label, which can collide with other UI strings) fixes every control inside for free.
    ImGui::PushID(Id.c_str());
    const std::string Key = QueryChannelKey(Id);
    int &Mode = ChannelSourceMode.try_emplace(Key, Derived ? 2 : std::strcmp(EditKind, "color") == 0 ? 1 : 0).first->second;
    Controls.BeginCard("", true);
    {
        const auto HeadOrigin = ImGui::GetCursorScreenPos();
        auto *Ink = ImGui::GetWindowDrawList();
        Ink->AddCircleFilled(ImVec2(HeadOrigin.x + 5 * S, HeadOrigin.y + 7 * S), 4 * S, ImGui::GetColorU32(Spectrum(Tint)));
        if (ImGui::InvisibleButton("Fold", ImVec2(Width - 24 * S, 20 * S)))
        {
            if (Folded) FoldedChannels.erase(Id); else FoldedChannels.insert(Id);
        }
        Caption(ImVec2(HeadOrigin.x + 16 * S, HeadOrigin.y + 2 * S), Label, 10 * S, 0xd9d4c4);
        // 📝 Mirrors ChannelPropertyPanel.jsx's <small> next to the channel name - it names whichever source
        // mode is actually active right now, not a fixed type hint.
        const char *ModeCaption = Derived ? "Height-derived" : Mode == 1 ? "Colour" : Mode == 2 ? "Generator" : "Texture";
        Caption(ImVec2(HeadOrigin.x + Width - 130 * S, HeadOrigin.y + 3 * S), ModeCaption, 8.5f * S, 0x9a9a8c);
        StrokeIcon(ImVec2(HeadOrigin.x + Width - 44 * S, HeadOrigin.y + 2 * S), 11 * S, Folded ? ChevronRightPath : ChevronDownPath, 0x8f8f7e);
        if (!Folded)
        {
            ImGui::SetCursorScreenPos(ImVec2(HeadOrigin.x, HeadOrigin.y + 24 * S));
            if (Derived)
            {
                // 📝 Normal has no source switch at all in the web app - it only ever follows Height.
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
                ImGui::TextColored(Spectrum(0x868872), "%s", Note);
                ImGui::PopTextWrapPos();
            }
            else
            {
                const char *Modes[]{"Texture", "Colour", "Generator"};
                for (int Index = 0; Index < 3; ++Index)
                {
                    if (Index) ImGui::SameLine(0, 4 * S);
                    if (Controls.Button(Modes[Index], 0, Mode == Index, {}, true)) Mode = Index;
                }
                ImGui::Dummy(ImVec2(1, 6 * S));
                if (Mode == 0) ConstructChannelTextureSource(Key, Label);
                else if (Mode == 1) ConstructChannelColourSource(Key, Id, Label, Minimum, Maximum, Default);
                else ConstructChannelGeneratorSource(Key, Label);
            }
        }
    }
    Controls.EndCard();
    ImGui::PopID();
}

void TextureWorkspacePanel::ConstructChannelTextureSource(const std::string &Key, const char *Label)
{
    // 📝 1:1 with ChannelPropertyPanel.jsx's empty "channel-texture-empty" state - this native build never
    // actually imports or paints a channel source, so the card always shows the empty placeholder.
    const float S = InterfaceScale;
    const float Width = ImGui::GetContentRegionAvail().x;
    Controls.BeginCard("", false, true);
    {
        const auto Origin = ImGui::GetCursorScreenPos();
        const float CardWidth = ImGui::GetContentRegionAvail().x;
        ImGui::GetWindowDrawList()->AddRectFilled(Origin, ImVec2(Origin.x + CardWidth, Origin.y + 64 * S), ImGui::GetColorU32(Spectrum(0x161614)), 6 * S);
        StrokeIcon(ImVec2(Origin.x + CardWidth * .5f - 11 * S, Origin.y + 14 * S), 22 * S, ImagePath, 0x5c5c51);
        const char *Placeholder = "Paint or import a texture source";
        const float TextWidth = ImGui::GetFont()->CalcTextSizeA(9 * S * 1.302f, FLT_MAX, 0, Placeholder).x; // same font size Caption() actually draws with.
        Caption(ImVec2(Origin.x + (CardWidth - TextWidth) * .5f, Origin.y + 44 * S), Placeholder, 9 * S, 0x75756a);
        ImGui::Dummy(ImVec2(1, 68 * S));
    }
    Controls.EndCard();
    if (Controls.Button("Import texture", (Width - 4 * S) * .5f, false, UploadPath)) {}
    ImGui::SameLine(0, 4 * S);
    if (Controls.Button("Paint texture", (Width - 4 * S) * .5f, false, BrushPath)) {}
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextColored(Spectrum(0x686859), "PNG, JPEG or WebP - source import and in-card painting are not implemented in this native build.");
    ImGui::PopTextWrapPos();
    (void)Key;
    (void)Label;
}

void TextureWorkspacePanel::ConstructChannelColourSource(const std::string &Key, const std::string &Id, const char *Label,
    float Minimum, float Maximum, float Default)
{
    // 📝 ChannelPropertyPanel.jsx shows a real <ColorField> for edit:"color" channels (just baseColor among the
    // ones this demo document enables) and a <Slider> for edit:"scalar" channels (roughness/metalness/height/
    // opacity) - reproduce that split instead of one generic placeholder for every channel.
    if (Id == "baseColor")
    {
        auto &Swatch = ChannelColour.try_emplace(Key, std::array<float, 4>{.722f, .451f, .2f, 1.f}).first->second;
        Controls.Colour("Colour", Swatch);
    }
    else
    {
        float &Amount = ChannelAmount.try_emplace(Key, Default).first->second;
        Controls.Range("Amount", Amount, Minimum, Maximum);
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextColored(Spectrum(0x868872), "%s source value for %s - not composited onto the surface.", Id == "baseColor" ? "Flat colour" : "Flat scalar", Label);
    ImGui::PopTextWrapPos();
}

void TextureWorkspacePanel::ConstructChannelGeneratorSource(const std::string &Key, const char *Label)
{
    // 📝 1:1 with ChannelPropertyPanel.jsx's generator <select> (grouped Mask / Wear / Procedural options) plus
    // the picked generator's parameter sliders - QueryGeneratorCatalogue() mirrors paintChannelModel.js's
    // CHANNEL_GENERATORS table. Picking a generator and dragging its sliders is purely a UI affordance here;
    // nothing samples a mesh map or runs a noise kernel.
    const auto &Catalogue = QueryGeneratorCatalogue();
    std::vector<std::string> Owned;
    std::vector<const char *> Captions{"Choose a generator..."};
    Owned.reserve(Catalogue.size());
    for (const auto &Entry : Catalogue)
    {
        Owned.push_back(std::string(Entry.Group) + " \xC2\xB7 " + Entry.Label);
        Captions.push_back(Owned.back().c_str());
    }
    int &Choice = ChannelGeneratorChoice.try_emplace(Key, -1).first->second;
    int Selection = Choice + 1;
    if (Controls.Dropdown("Generator", Selection, Captions)) Choice = Selection - 1;
    if (Choice < 0)
    {
        ImGui::TextColored(Spectrum(0x75756a), "No generator picked yet for %s.", Label);
        return;
    }
    const auto &Picked = Catalogue[size_t(Choice)];
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextColored(Spectrum(0x868872), "%s - source configuration.", Picked.Note);
    ImGui::PopTextWrapPos();
    for (const auto &Parameter : Picked.Parameters)
    {
        if (!Parameter.first) continue;
        const std::string ParameterKey = Key + ":" + Parameter.first;
        float &Value = ChannelGeneratorParameters.try_emplace(ParameterKey, Parameter.second).first->second;
        Controls.Range(Parameter.first, Value, 0.f, Parameter.first == std::string("Angle") ? 360.f : 1.f);
    }
}

void TextureWorkspacePanel::ConstructInspectorHistory(float Width)
{
    const float S = InterfaceScale;
    ImGui::TextColored(Spectrum(0x7c7c6d), "DOCUMENT TIMELINE");
    ImGui::TextColored(Spectrum(0xd9d9cc), "History");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 56 * S + ImGui::GetCursorPosX());
    ImGui::TextColored(Spectrum(0x8f8f7e), "0 states");
    ImGui::Dummy(ImVec2(1, 6 * S));
    ImGui::BeginDisabled();
    Controls.Button("Undo", (Width - 8 * S) * .5f, false, UndoPath);
    ImGui::SameLine(0, 4 * S);
    Controls.Button("Redo", (Width - 8 * S) * .5f, false, RedoPath);
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(1, 8 * S));
    Controls.BeginCard("", true);
    {
        Caption(ImGui::GetCursorScreenPos(), "Last stroke", 10 * S, 0xd9d4c4);
        ImGui::Dummy(ImVec2(1, 18 * S));
        ImGui::TextColored(Spectrum(0x75756a), "No recorded stroke");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextColored(Spectrum(0x868872), "Use digital Paint / Erase / Fill on a layer or mask, or paint a channel's texture source.");
        ImGui::PopTextWrapPos();
    }
    Controls.EndCard();
    const char *Filters[]{"All edits", "Strokes"};
    for (int Index = 0; Index < 2; ++Index)
    {
        if (Index) ImGui::SameLine(0, 4 * S);
        if (Controls.Button(Filters[Index], 0, HistoryFilter == Index, {}, true)) HistoryFilter = Index;
    }
    ImGui::Dummy(ImVec2(1, 8 * S));
    ImGui::TextColored(Spectrum(0x75756a), "No stroke transactions in this session.");
    ImGui::Dummy(ImVec2(1, 10 * S));
    ImGui::TextColored(Spectrum(0x716f62), "Session history \xC2\xB7 200 undo steps");
    ImGui::BeginDisabled();
    Controls.Button("Restore this state", 0, false, ArrowUpRightPath);
    ImGui::EndDisabled();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ROOT INTERFACE / STATUS BAR
//------------------------------------------------------------------------------------------------------------------------

void TextureWorkspacePanel::ConstructInterface()
{
    const auto SavedPalette = ControlKit::Palette();
    auto Palette = SavedPalette;
    Palette.Panel = {.0706f, .0706f, .0706f, 1};
    Palette.Inset = {.098f, .098f, .098f, 1};
    Palette.Raised = {.133f, .133f, .133f, 1};
    Palette.Selected = {.20f, .208f, .173f, 1};
    Palette.Text = {.808f, .808f, .753f, 1};
    Palette.TextDim = {.56f, .58f, .50f, 1};
    Palette.TextFaint = {.35f, .37f, .30f, 1};
    Palette.Accent = {.39f, .45f, .31f, 1};
    ControlKit::ConfigurePalette(Palette);
    Controls.RegisterExtent = [&](std::string_view Caption, PlaneExtent Extent) { ControlExtents[std::string(Caption)] = Extent; };
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Spectrum(0x111111));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, Spectrum(0xd5d5c4));
    ImGui::PushStyleColor(ImGuiCol_Button, Spectrum(0x222222));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Spectrum(0x2e2e28));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Spectrum(0x33352c));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Spectrum(0x1a1a18));
    ImGui::PushStyleColor(ImGuiCol_Header, Spectrum(0x191919));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Spectrum(0x272923));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, Spectrum(0x2c2e27));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, Spectrum(0xa3aa8a));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, Spectrum(0xa3aa8a));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, Spectrum(0xb7bf9c));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, .055f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, Spectrum(0x2a2a26));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, Spectrum(0x35352f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, Spectrum(0x3c3c34));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6 * InterfaceScale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * InterfaceScale, 6 * InterfaceScale));

    ConstructHeader();
    Panels.ConstructInterface(InterfaceScale);

    const auto *Viewport = ImGui::GetMainViewport();
    const float StatusHeight = 24 * InterfaceScale;
    const float Bottom = Viewport->WorkPos.y + Viewport->WorkSize.y - StatusHeight;
    const float Left = Viewport->WorkPos.x, Right = Viewport->WorkPos.x + Viewport->WorkSize.x;
    ImGui::SetNextWindowPos(ImVec2(Left, Bottom));
    ImGui::SetNextWindowSize(ImVec2(Right - Left, StatusHeight));
    ImGui::Begin("Texture status", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    ImGui::TextColored(Spectrum(0x9a9a8f), "%s", Info.c_str());
    ImGui::SameLine(ImGui::GetWindowWidth() * .4f);
    ImGui::BeginDisabled();
    ImGui::SmallButton("Undo");
    ImGui::SameLine();
    ImGui::SmallButton("Redo");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextColored(Spectrum(0x6f6f63), "\xC2\xB7");
    ImGui::SameLine();
    ImGui::TextColored(Spectrum(0x8f8f7e), "Layer document");
    ImGui::SameLine(ImGui::GetWindowWidth() - 160 * InterfaceScale);
    ImGui::TextColored(Spectrum(0x716f62), "DIGITAL PAINT / LAYERS");
    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(17);
    ControlKit::ConfigurePalette(SavedPalette);
}
} // namespace Frontier
