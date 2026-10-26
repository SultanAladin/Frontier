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
#include <cmath>
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
// 🎨 The real rendered width of a Caption() string - Caption() draws at its own Size*1.302f font size, which
// is almost never ImGui's current font size, so a plain ImGui::CalcTextSize(Text) call (implicitly measuring
// at the active ImGui font size) over- or under-estimates it. Anything that right-aligns or reserves layout
// space for Caption() text (rather than a real ImGui widget label) needs to measure it this way instead.
float MeasureCaption(const char *Text, float Size, void *Typeface = nullptr)
{
    ImFont *Font = Typeface ? static_cast<ImFont *>(Typeface) : ImGui::GetFont();
    return Font->CalcTextSizeA(Size * 1.302f, FLT_MAX, 0.f, Text).x;
}
void StrokeIcon(ImVec2 Minimum, float Size, const char *Path, uint32_t Colour, float Weight = 1.6f) noexcept
{
    PixelSpace Surface;
    (void)Surface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 1);
    GlyphSpace::Stroke(Surface, Path, {Minimum.x, Minimum.y, Size, Weight, SpectrumQuad(Colour)});
}
// 🎨 LayerStackPanel.html's `.prev-tile`/`.slot-thumb` chequerboard background-image - "no sampled content yet"
// for a texture/generator/derived channel body, a preview tile or an empty texture slot.
void DrawCheckerboard(ImVec2 Minimum, ImVec2 Maximum, float Cell) noexcept
{
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(Minimum, Maximum, ImGui::GetColorU32(Spectrum(0x0a0a0a)));
    bool RowToggle = false;
    for (float Y = Minimum.y; Y < Maximum.y; Y += Cell)
    {
        bool Toggle = RowToggle;
        for (float X = Minimum.x; X < Maximum.x; X += Cell)
        {
            if (Toggle) Ink->AddRectFilled(ImVec2(X, Y), ImVec2(std::min(X + Cell, Maximum.x), std::min(Y + Cell, Maximum.y)), ImGui::GetColorU32(Spectrum(0x161616)));
            Toggle = !Toggle;
        }
        RowToggle = !RowToggle;
    }
}
uint32_t PackColour(const std::array<float, 4> &Value) noexcept
{
    return (uint32_t(std::clamp(Value[0], 0.f, 1.f) * 255.f) << 16) | (uint32_t(std::clamp(Value[1], 0.f, 1.f) * 255.f) << 8) |
        uint32_t(std::clamp(Value[2], 0.f, 1.f) * 255.f);
}
std::array<float, 4> UnpackColour(uint32_t Hex) noexcept
{
    return {float((Hex >> 16) & 0xff) / 255.f, float((Hex >> 8) & 0xff) / 255.f, float(Hex & 0xff) / 255.f, 1.f};
}
// 📝 LayerStackPanel.js's ResolveInkOver - the YIQ split that keeps a bubble's "Ln" label readable over
// whatever hue its tag happens to be.
uint32_t ResolveInkOver(uint32_t Hex) noexcept
{
    const float Red = float((Hex >> 16) & 0xff), Green = float((Hex >> 8) & 0xff), Blue = float(Hex & 0xff);
    return (Red * 299.f + Green * 587.f + Blue * 114.f) / 1000.f >= 128.f ? 0x111111 : 0xffffff;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     1:1 WITH LayerStackPanel.html's KINDS
//------------------------------------------------------------------------------------------------------------------------
struct KindMeta { const char *Label; uint32_t Colour; const char *Summary; };
const KindMeta &QueryKindMeta(const std::string &Kind) noexcept
{
    // 🔴 Tints are LayerKinds.js's exact hex (also LayerStackPanel.html's KINDS table) - a channel card's
    // header dot resolves the SAME table (by the selected layer's own Kind), not a per-channel tint, so every
    // channel card on one layer always reads the same classification colour.
    static const std::map<std::string, KindMeta> Table{
        {"folder",    {"Folder",    0xbfac88, "A layer group with its own blend, opacity and optional mask"}},
        {"decal",     {"Decal",     0xc4aa7a, "Surface-projected stamp"}},
        {"paint",     {"Paint",     0xf97316, "Accepts brush strokes"}},
        {"fill",      {"Fill",      0x3b82f6, "Uniform authored values"}},
        {"material",  {"Material",  0x8b5cf6, "A PBR preset, flooded"}},
        {"generator", {"Generator", 0x10b981, "Procedural over the atlas"}},
    };
    const auto Found = Table.find(Kind);
    static const KindMeta Fallback{"Layer", 0x9a9a8f, "Layer"};
    return Found == Table.end() ? Fallback : Found->second;
}
// 📝 LayerStackPanel.html's BLEND_MODES - all seven, not the first four a chip strip could fit.
constexpr const char *kBlendModes[7]{"Normal", "Multiply", "Screen", "Overlay", "Add", "Darken", "Linear Dodge"};

// 🎨 1:1 with ChannelPropertyPanel.html's CHANNEL_SLOTS palette - every channel the "Add channel" picker can
// enable, not just the handful the demo document ships pre-enabled with.
constexpr uint32_t kBaseColourTint = 0xb87333, kMetalnessTint = 0x8b5cf6, kRoughnessTint = 0x3b82f6, kHeightTint = 0x8a8a8a,
    kNormalTint = 0x10b981, kOpacityTint = 0x94a3b8, kEmissionTint = 0xf59e0b, kOcclusionTint = 0x6b7280,
    kAnisotropyTint = 0x22d3ee, kAnisotropyAngleTint = 0x0ea5e9, kClearcoatTint = 0xe2e8f0,
    kRefractionTint = 0xa78bfa, kSheenTint = 0xf472b6, kSubsurfaceTint = 0xfb7185;

// 📝 1:1 with ChannelPropertyPanel.html's CHANNEL_SLOTS table (fourteen channels, not fifteen - this port has
// no "transmission" entry, the HTML prototype never had one) - the single source of truth for every channel's
// label, group (Surface/Radiance/Reflectance/Scattering), tint, edit kind, range, default colour and atlas
// placement string. Used to render the enabled-channel chips, the "Add channel" dropdown's grouped rows and
// each channel's own property card, so all three always agree on what a given channel id means.
struct ChannelMeta
{
    const char *Id, *Label, *Group, *EditKind, *Note, *Placement;
    uint32_t Tint;
    float Minimum, Maximum, Default;
    std::array<float, 4> DefaultColour;
};
const std::array<ChannelMeta, 14> &QueryChannelCatalogue()
{
    static const std::array<ChannelMeta, 14> Catalogue{{
        {"baseColor", "Base Colour", "Surface", "color", "Flat colour source for this channel.", "Colour atlas \xC2\xB7 RGB",
            kBaseColourTint, 0.f, 1.f, 0.f, {.722f, .451f, .2f, 1.f}},
        {"metalness", "Metallic", "Surface", "scalar", "Dielectric to conductor blend.", "Material atlas \xC2\xB7 R",
            kMetalnessTint, 0.f, 1.f, 0.f, {}},
        {"roughness", "Roughness", "Surface", "scalar", "Controls micro-facet spread (0 = mirror, 1 = matte).", "Material atlas \xC2\xB7 G",
            kRoughnessTint, 0.f, 1.f, .5f, {}},
        {"height", "Height", "Surface", "scalar", "Surface displacement source.", "Material atlas \xC2\xB7 B",
            kHeightTint, 0.f, 1.f, .5f, {}},
        {"normal", "Normal", "Surface", "derived", "Derived from the painted height. No value to author.", "No storage \xC2\xB7 derived",
            kNormalTint, 0.f, 1.f, 0.f, {}},
        {"opacity", "Opacity", "Surface", "scalar", "Per-texel coverage.", "Material atlas \xC2\xB7 A",
            kOpacityTint, 0.f, 1.f, 1.f, {}},
        {"emission", "Emissive", "Radiance", "color", "Self-illumination source.", "Emissive atlas \xC2\xB7 RGB",
            kEmissionTint, 0.f, 1.f, 0.f, {0.f, 0.f, 0.f, 1.f}},
        {"occlusion", "Ambient Occlusion", "Radiance", "scalar", "Cavity / contact-shadow source.", "Emissive atlas \xC2\xB7 A",
            kOcclusionTint, 0.f, 1.f, 1.f, {}},
        {"anisotropy", "Anisotropy", "Reflectance", "scalar", "Directional-highlight stretch amount.", "Reflect atlas \xC2\xB7 R",
            kAnisotropyTint, 0.f, 1.f, 0.f, {}},
        {"anisotropyAngle", "Anisotropy Angle", "Reflectance", "scalar", "Highlight direction in degrees.", "Reflect atlas \xC2\xB7 G",
            kAnisotropyAngleTint, 0.f, 360.f, 0.f, {}},
        {"clearcoat", "Clearcoat", "Reflectance", "scalar", "Secondary lacquer-layer strength.", "Reflect atlas \xC2\xB7 B",
            kClearcoatTint, 0.f, 1.f, 0.f, {}},
        {"refractionIndex", "Refraction Index", "Reflectance", "scalar", "Index of refraction for transmission.", "Reflect atlas \xC2\xB7 A",
            kRefractionTint, 1.f, 3.f, 1.5f, {}},
        {"sheen", "Sheen", "Scattering", "color", "Fabric-like grazing highlight tint.", "Scatter atlas \xC2\xB7 RGB",
            kSheenTint, 0.f, 1.f, 0.f, {.227f, .227f, .227f, 1.f}},
        {"subsurface", "Subsurface", "Scattering", "color", "Soft subsurface-scattering tint.", "Sheen atlas \xC2\xB7 RGB",
            kSubsurfaceTint, 0.f, 1.f, 0.f, {.851f, .541f, .447f, 1.f}},
    }};
    return Catalogue;
}
constexpr const char *ChannelGroups[]{"Surface", "Radiance", "Reflectance", "Scattering"};
const ChannelMeta *QueryChannelMeta(const std::string &Id)
{
    for (const auto &Entry : QueryChannelCatalogue()) if (Id == Entry.Id) return &Entry;
    return nullptr;
}
// 🔴 ChannelPropertyPanel.html's "baseColour" is PERMANENTLY excluded from both the add-menu's toggle (it is
// drawn disabled whether on or off) and the chip's own remove button (drawn, but inert) - a Material-kind
// layer's base colour is never optional the way every other channel is.
bool IsLockedChannel(const std::string &Id) noexcept { return Id == "baseColor"; }

constexpr const char *PlusPath = "M12 5V19M5 12H19";
constexpr const char *CrossPath = "M6 6L18 18M6 18L18 6";
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
constexpr const char *CogPath = "M12 8A4 4 0 1 0 12 16A4 4 0 1 0 12 8ZM12 2V5M12 19V22M2 12H5M19 12H22M4.9 4.9L7 7M17 17L19.1 19.1M19.1 4.9L17 7M7 17L4.9 19.1";

// 📝 1:1 with ChannelPropertyPanel.html's GENERATOR_CATALOGUE - label/group/parameter metadata only, purely
// for the generator picker's visual fidelity (grouped "Mask / Wear / Procedural" option list + its parameter
// sliders). Picking one here never samples a real mesh map or runs any noise kernel - this is still UI-only
// chrome.
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
        {"CurvatureEdges", "Curvature", "Mask", "Convex edge wear", {{{"Balance", .5f}, {"Contrast", .7f}, {"Radius", .25f}}}},
        {"AmbientOcclusion", "Ambient Occlusion", "Mask", "Cavity dirt", {{{"Spread", .4f}, {"Contrast", .6f}, {nullptr, 0.f}}}},
        {"Thickness", "Thickness", "Mask", "Translucent falloff", {{{"Depth", .5f}, {"Contrast", .5f}, {nullptr, 0.f}}}},
        {"PositionGradient", "Position Gradient", "Mask", "World-axis ramp", {{{"Origin", .5f}, {"Falloff", .35f}, {nullptr, 0.f}}}},
        {"MetalEdgeWear", "Metal Edge Wear", "Wear", "Curvature + grunge", {{{"Intensity", .6f}, {"Softness", .3f}, {"Grain", .45f}}}},
        {"DirtAccumulation", "Dirt", "Wear", "Occlusion-driven", {{{"Amount", .5f}, {"Scale", .3f}, {nullptr, 0.f}}}},
        {"WaterRunoff", "Water Runoff", "Wear", "Gravity streaks", {{{"Length", .55f}, {"Density", .4f}, {"Gravity", .8f}}}},
        {"PerlinNoise", "Perlin Noise", "Procedural", "Fractal value noise", {{{"Scale", .4f}, {"Octaves", .5f}, {"Contrast", .5f}}}},
        {"VoronoiCells", "Voronoi", "Procedural", "Cellular partition", {{{"Density", .35f}, {"Jitter", .7f}, {nullptr, 0.f}}}},
        {"BrushedAnisotropy", "Brushed Metal", "Procedural", "Anisotropic streaks", {{{"Direction", 0.f}, {"Grain", .6f}, {nullptr, 0.f}}}},
    }};
    return Catalogue;
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

TextureWorkspacePanel::TextureWorkspacePanel()
{
    // 📝 Every Controls.Button() self-registers its own on-screen extent through this callback (see
    // VectorAuthoringControls.cpp's Register()) - without it, QueryControlExtent() on any header pill,
    // workspace-switch tab, authoring-tool button or channel mode switch would always come back empty, and
    // the evidence harness's Click() helper would reject every one of those with "Target control was never
    // drawn." even though the button is actually on screen.
    Controls.RegisterExtent = [&](std::string_view Caption, PlaneExtent Extent) { ControlExtents[std::string(Caption)] = Extent; };
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
        // the "full static" scope locked in for this pass. Tag/Paint hues mirror LayerStackPanel.html's own
        // sample pose (InitializeLayerStackSample) - distinct per layer, not merely the layer's Kind colour.
        Layers.clear();
        Layers.push_back({"Clay base", "material", 0, true, false, true, false, "Normal", 100, 0, 0, true, "Black", false, 92.f,
            0x8b5cf6, 0xb87333, {"baseColor", "roughness", "metalness"}});
        Layers.push_back({"Body details", "folder", 0, true, false, true, false, "Normal", 100, 2, 0, false, "White", false, 100.f,
            0xbfac88, 0x2e2d2b, {}});
        Layers.push_back({"Edge wear", "paint", 1, true, false, true, false, "Multiply", 72, 0, 0, false, "White", false, 100.f,
            0xf97316, 0x2e2d2b, {"baseColor", "roughness"}});
        Layers.push_back({"Scratch normal", "paint", 1, true, true, true, false, "Normal", 100, 0, 0, false, "White", false, 100.f,
            0xef4444, 0x2e2d2b, {"normal", "height"}});
        Layers.push_back({"Maker's stamp", "decal", 0, true, false, true, false, "Normal", 100, 0, 2, false, "White", false, 100.f,
            0xc4aa7a, 0x2e2d2b, {}});
        Layers.push_back({"Spout tint", "fill", 0, false, false, true, false, "Normal", 55, 0, 0, true, "White", true, 100.f,
            0x3b82f6, 0x6f7c93, {"baseColor"}});
        Layers.push_back({"Procedural noise", "generator", 0, true, false, true, false, "Overlay", 38, 0, 0, false, "White", false, 100.f,
            0x10b981, 0x2e2d2b, {"roughness", "normal"}});
        SelectedLayer = 0;
        Info = "Representative teapot document \xC2\xB7 layer paint/mask/decal compositing is not implemented in this native build.";
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

void TextureWorkspacePanel::SelectLayer(int Index)
{
    if (Index < 0 || Index >= int(Layers.size())) return;
    SelectedLayer = Index;
    // 📝 LayerStackPanel.html's "focus" verb - clicking a row body both selects AND opens its own inline fold.
    Layers[size_t(Index)].Expanded = true;
    RightTab = InspectorTab::Properties;
}

void TextureWorkspacePanel::ToggleChannel(TextureLayerRow &Layer, const std::string &Id, bool Enable)
{
    if (IsLockedChannel(Id)) return;
    auto Has = [&](const std::string &ChannelId) { return std::find(Layer.Channels.begin(), Layer.Channels.end(), ChannelId) != Layer.Channels.end(); };
    auto Add = [&](const std::string &ChannelId) { if (!Has(ChannelId)) Layer.Channels.push_back(ChannelId); };
    auto Remove = [&](const std::string &ChannelId) { Layer.Channels.erase(std::remove(Layer.Channels.begin(), Layer.Channels.end(), ChannelId), Layer.Channels.end()); };
    if (Enable)
    {
        Add(Id);
        if (Id == "normal") Add("height"); // Normal is purely derived from Height - it never exists alone.
    }
    else
    {
        Remove(Id);
        if (Id == "height") Remove("normal");
    }
    // 📝 Re-sort to the catalogue's canonical Surface/Radiance/Reflectance/Scattering order so a freshly added
    // channel slots into the same stable position every time, instead of wherever it happened to be clicked
    // from in the grouped picker.
    const auto &Catalogue = QueryChannelCatalogue();
    std::stable_sort(Layer.Channels.begin(), Layer.Channels.end(), [&](const std::string &A, const std::string &B)
    {
        const auto IndexOf = [&](const std::string &ChannelId) -> size_t
        {
            for (size_t Index = 0; Index < Catalogue.size(); ++Index) if (ChannelId == Catalogue[Index].Id) return Index;
            return Catalogue.size();
        };
        return IndexOf(A) < IndexOf(B);
    });
}
void TextureWorkspacePanel::AddLayer(const std::string &Kind)
{
    if (Layers.size() >= 64) return;
    const auto &Meta = QueryKindMeta(Kind);
    TextureLayerRow Row;
    Row.Kind = Kind;
    Row.Name = Kind == "folder" ? "New folder" : std::string(Meta.Label) + " layer";
    Row.Tag = Meta.Colour;
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
//                                                     LAYER STACK DOCK  (LayerStackPanel.html transcription)
//------------------------------------------------------------------------------------------------------------------------

bool TextureWorkspacePanel::ConstructRadioDropdown(const char *Id, const char *Preview, int &Selection,
    std::span<const char *const> Captions, float Width)
{
    // 📝 LayerStackPanel.html's BuildDropdown - a value-pill face with a grey caret cap, opening a
    // sharp-cornered menu where EVERY row carries a radio ring (filled when current) rather than a checkmark.
    const float S = InterfaceScale;
    ImGui::PushID(Id);
    bool Changed = false;
    ImGui::SetNextItemWidth(Width);
    // 📝 Dear ImGui's own BeginCombo() caps an unconstrained popup at 8 "items" tall using its own generic
    // FontSize+ItemSpacing row unit - but every row here is drawn at a taller, custom 24*S Selectable height,
    // so that generic estimate under-measures the real content and silently clips off the last of our 7 blend
    // modes ("Linear Dodge") with no scrollbar to reveal it. Give the popup our own real row pitch instead of
    // letting ImGui guess.
    const float RowPitch = 24 * S + ControlKitTokens::DropdownRowGap * S;
    const float NeededHeight = Captions.size() * RowPitch + ControlKitTokens::DropdownPaddingY * S * 2 + 4 * S;
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, NeededHeight));
    if (DropdownControlSurface::BeginSelection("##Combo", Preview, S))
    {
        auto *Ink = ImGui::GetWindowDrawList();
        for (size_t Index = 0; Index < Captions.size(); ++Index)
        {
            const bool On = Selection == int(Index);
            ImGui::PushID(int(Index));
            const float RowWidth = std::max(ImGui::GetContentRegionAvail().x, Width);
            const bool Pressed = ImGui::Selectable("##Row", On, 0, ImVec2(RowWidth, 24 * S));
            const auto Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
            Ink->AddText(ImVec2(Minimum.x + 10 * S, Minimum.y + (Maximum.y - Minimum.y - ImGui::GetFontSize()) * .5f),
                ImGui::GetColorU32(Spectrum(0xd5d5c4)), Captions[Index]);
            const ImVec2 RadioCentre(Maximum.x - 14 * S, (Minimum.y + Maximum.y) * .5f);
            Ink->AddCircle(RadioCentre, 6 * S, ImGui::GetColorU32(Spectrum(On ? 0x4a90e2 : 0x3a3a3a)), 16, On ? 2.f * S : 1.2f * S);
            if (On) Ink->AddCircleFilled(RadioCentre, 3 * S, ImGui::GetColorU32(Spectrum(0x4a90e2)));
            ImGui::PopID();
            if (Pressed) { Selection = int(Index); Changed = true; }
        }
        DropdownControlSurface::EndSelection();
    }
    Track(Id);
    ImGui::PopID();
    return Changed;
}

bool TextureWorkspacePanel::ConstructOpacityScrub(const char *Id, int &Percent, float Width, float Height)
{
    // 📝 LayerStackPanel.html's `.row .pill` / `.slider .num .n` - a borderless capsule that SCRUBS
    // horizontally (ew-resize), not a click-to-edit field.
    const float S = InterfaceScale;
    ImGui::PushID(Id);
    const auto Origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("Scrub", ImVec2(Width, Height));
    Track(Id);
    bool Changed = false;
    if (ImGui::IsItemActive())
    {
        const float Delta = ImGui::GetIO().MouseDelta.x;
        if (Delta != 0.f)
        {
            const int Next = std::clamp(Percent + int(std::lround(Delta * .6f)), 0, 100);
            if (Next != Percent) { Percent = Next; Changed = true; }
        }
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + Height), ImGui::GetColorU32(Spectrum(0x000000)), Height * .5f);
    char Label[16];
    std::snprintf(Label, sizeof(Label), "%d%%", Percent);
    const auto TextSize = ImGui::CalcTextSize(Label);
    Ink->AddText(ImVec2(Origin.x + (Width - TextSize.x) * .5f, Origin.y + (Height - TextSize.y) * .5f), ImGui::GetColorU32(Spectrum(0x9a9a8f)), Label);
    ImGui::PopID();
    return Changed;
}

void TextureWorkspacePanel::ConstructLayerExpand(TextureLayerRow &Layer, float Width, int Index)
{
    // 📝 LayerStackPanel.html's BuildExpand - Blend (full 7-mode dropdown) / Opacity / Tag / Mask, nothing
    // else: no "Visible" row (the row's own eye is the one visibility control) and no base-colour swatch (the
    // thumb's own ink already carries that hue).
    //
    // 📝 Every interactive control in here is scoped per-row for real ImGui identity by the caller's
    // enclosing PushID(Index) (ConstructLayersDock), so two simultaneously-expanded rows never actually
    // cross-wire each other's widget state. But QueryControlExtent()'s ControlExtents map is a separate,
    // hand-rolled registry keyed purely by the literal Caption string passed to RegisterExtent/Track - it is
    // NOT scoped by ImGui's ID stack, so a literal caption reused across rows (e.g. two expanded rows both
    // calling Controls.Range("Opacity", ...)) collides: whichever row drew last simply overwrites the
    // other's entry. Re-Track() every such control immediately after it draws, under an ":Index"-suffixed
    // key, so evidence/tooling can reliably target one specific row's control even while others are expanded.
    const float S = InterfaceScale;
    auto *Ink = ImGui::GetWindowDrawList();
    const auto Origin = ImGui::GetCursorScreenPos();
    Ink->AddLine(Origin, ImVec2(Origin.x + Width, Origin.y), ImGui::GetColorU32(Spectrum(0xffffff, 0.06f)));
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 10 * S, Origin.y + 8 * S));
    ImGui::PushItemWidth(Width - 20 * S);
    ImGui::BeginGroup();
    {
        ImGui::TextColored(Spectrum(0x747467), "Blend");
        int BlendIndex = 0;
        for (int BlendSearch = 0; BlendSearch < 7; ++BlendSearch) if (Layer.Blend == kBlendModes[BlendSearch]) BlendIndex = BlendSearch;
        if (ConstructRadioDropdown("BlendMenu", kBlendModes[size_t(BlendIndex)], BlendIndex, kBlendModes, Width - 20 * S))
            Layer.Blend = kBlendModes[size_t(BlendIndex)];
        Track(std::string("LayerBlendMenu:") + std::to_string(Index));
        ImGui::Dummy(ImVec2(1, 6 * S));

        // 📝 Controls.Range()/Controls.Colour() already draw their own TextDisabled(Caption) label above the
        // field (see VectorAuthoringControls.cpp's Number()/Colour()) - a real "Opacity"/"Tag" caption here,
        // not a "##"-prefixed one, so that label renders as intended instead of the literal "##..." text.
        float OpacityF = float(Layer.Opacity);
        if (Controls.Range("Opacity", OpacityF, 0, 100)) Layer.Opacity = int(OpacityF);
        Track(std::string("LayerOpacity:") + std::to_string(Index));
        ImGui::Dummy(ImVec2(1, 6 * S));

        auto TagArray = UnpackColour(Layer.Tag);
        if (Controls.Colour("Tag", TagArray)) Layer.Tag = PackColour(TagArray);
        Track(std::string("LayerTag:") + std::to_string(Index));
        ImGui::Dummy(ImVec2(1, 8 * S));

        ImGui::TextColored(Spectrum(0x6a6a5e), "MASK");
        ImGui::Dummy(ImVec2(1, 2 * S));
        if (!Layer.HasMask)
        {
            if (Controls.Button("Add mask", Width - 20 * S, false, PlusPath)) Layer.HasMask = true;
            Track(std::string("LayerAddMask:") + std::to_string(Index));
        }
        else
        {
            const auto MaskOrigin = ImGui::GetCursorScreenPos();
            const float Base = Layer.MaskFill == "Black" ? 0.f : 1.f;
            const float Light = Layer.MaskInverted ? 1.f - Base : Base;
            const uint32_t Byte = uint32_t(std::lround(Light * 255.f));
            const uint32_t Tone = (Byte << 16) | (Byte << 8) | Byte;
            Ink->AddRectFilled(MaskOrigin, ImVec2(MaskOrigin.x + 28 * S, MaskOrigin.y + 28 * S), ImGui::GetColorU32(Spectrum(Tone)), 6 * S);
            Ink->AddRect(MaskOrigin, ImVec2(MaskOrigin.x + 28 * S, MaskOrigin.y + 28 * S), ImGui::GetColorU32(Spectrum(0xffffff, .08f)), 6 * S);
            Caption(ImVec2(MaskOrigin.x + 36 * S, MaskOrigin.y + 9 * S), "Layer mask", 10 * S, 0xd9d4c4);
            ImGui::SetCursorScreenPos(ImVec2(MaskOrigin.x + Width - 20 * S - 18 * S, MaskOrigin.y + 5 * S));
            if (ImGui::InvisibleButton("RemoveMask", ImVec2(18 * S, 18 * S))) Layer.HasMask = false;
            Track(std::string("LayerRemoveMask:") + std::to_string(Index));
            StrokeIcon(ImGui::GetItemRectMin(), 11 * S, CrossPath, ImGui::IsItemHovered() ? 0xe05a5a : 0x8a8a7d);
            Controls.Tooltip("Remove mask", "Delete this layer's mask.");
            ImGui::SetCursorScreenPos(ImVec2(MaskOrigin.x, MaskOrigin.y + 34 * S));
            Controls.Range("Strength", Layer.MaskStrength, 0, 100);
            Track(std::string("LayerMaskStrength:") + std::to_string(Index));
        }
    }
    ImGui::EndGroup();
    ImGui::PopItemWidth();
    ImGui::Dummy(ImVec2(1, 10 * S));
}

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

    // 📝 LayerStackPanel.html's `.filter` - a magnifier icon beside a plain text filter, nothing else (no
    // Visible/Hidden/Locked stat tiles - the reference panel does not have that row at all).
    const auto FilterOrigin = ImGui::GetCursorScreenPos();
    StrokeIcon(ImVec2(FilterOrigin.x + 6 * S, FilterOrigin.y + 7 * S), 13 * S, SearchPath, 0x8a8a7d, 1.4f);
    ImGui::SetCursorScreenPos(ImVec2(FilterOrigin.x + 24 * S, FilterOrigin.y));
    char QueryBuffer[96];
    std::snprintf(QueryBuffer, sizeof(QueryBuffer), "%s", LayerQuery.c_str());
    ImGui::SetNextItemWidth(Width - 16 * S - 24 * S);
    if (ImGui::InputTextWithHint("##FindTextureLayer", "Filter...", QueryBuffer, sizeof(QueryBuffer))) LayerQuery = QueryBuffer;
    ImGui::Dummy(ImVec2(1, 8 * S));

    size_t Hidden = 0;
    for (const auto &Layer : Layers) if (!Layer.Visible) ++Hidden;

    int SkipDepthBelow = -1; // when set, hide rows with Depth > this until a sibling/ancestor resets it
    const bool Dragging = DragSourceIndex >= 0;
    if (Dragging) DragHoverSlot = -1;
    std::vector<float> RowTopY(Layers.size(), -1.f);
    float LastBottom = ImGui::GetCursorScreenPos().y;
    bool AnyDrawn = false;

    for (int Index = 0; Index < int(Layers.size()); ++Index)
    {
        auto &Layer = Layers[size_t(Index)];
        if (SkipDepthBelow >= 0)
        {
            if (Layer.Depth > SkipDepthBelow) continue;
            SkipDepthBelow = -1;
        }
        if (!LayerQuery.empty())
        {
            std::string Folded = Layer.Name;
            std::transform(Folded.begin(), Folded.end(), Folded.begin(), ::tolower);
            std::string Needle = LayerQuery;
            std::transform(Needle.begin(), Needle.end(), Needle.begin(), ::tolower);
            if (Folded.find(Needle) == std::string::npos) continue;
        }
        if (Layer.Kind == "folder" && !Layer.Open) SkipDepthBelow = Layer.Depth;
        AnyDrawn = true;

        const auto &Meta = QueryKindMeta(Layer.Kind);
        const float RowHeight = 52 * S;
        const auto RowOrigin = ImGui::GetCursorScreenPos();
        const float RowWidth = ImGui::GetContentRegionAvail().x;
        RowTopY[size_t(Index)] = RowOrigin.y;
        if (Dragging && DragHoverSlot < 0 && ImGui::GetIO().MousePos.y < RowOrigin.y + RowHeight * .5f)
            DragHoverSlot = Index;

        // --- identity bubble + continuous timeline track (LayerStackPanel.html's .bubble / .track-col) -----
        const float BubbleColumnW = 34 * S, TrackColumnW = 16 * S;
        const uint32_t TrackHue = Layer.Visible ? Layer.Tag : 0x2a2a2a;
        const ImVec2 BubbleCentre(RowOrigin.x + BubbleColumnW * .5f, RowOrigin.y + 20 * S);
        Ink->AddCircleFilled(BubbleCentre, 13 * S, ImGui::GetColorU32(Spectrum(TrackHue)));
        char BubbleLabel[8];
        std::snprintf(BubbleLabel, sizeof(BubbleLabel), "L%d", int(Layers.size()) - Index);
        const auto BubbleTextSize = ImGui::CalcTextSize(BubbleLabel);
        Caption(ImVec2(BubbleCentre.x - BubbleTextSize.x * .42f, BubbleCentre.y - BubbleTextSize.y * .42f), BubbleLabel, 9.5f * S,
            Layer.Visible ? ResolveInkOver(Layer.Tag) : 0x6c6c70);
        const float TrackX = RowOrigin.x + BubbleColumnW + TrackColumnW * .5f;
        Ink->AddLine(ImVec2(TrackX, RowOrigin.y), ImVec2(TrackX, RowOrigin.y + RowHeight), ImGui::GetColorU32(Spectrum(TrackHue)), 3 * S);
        Ink->AddCircleFilled(ImVec2(TrackX, RowOrigin.y + 20 * S), 3 * S, ImGui::GetColorU32(Spectrum(0xffffff)));

        const float CardX = RowOrigin.x + BubbleColumnW + TrackColumnW;
        const float CardWidth = RowWidth - BubbleColumnW - TrackColumnW;
        const bool Selected = SelectedLayer == Index;
        Ink->AddRectFilled(ImVec2(CardX, RowOrigin.y), ImVec2(CardX + CardWidth, RowOrigin.y + RowHeight),
            ImGui::GetColorU32(Spectrum(Layer.Visible ? (Selected ? (Layer.Expanded ? 0x000000 : 0x28271f) : 0x201f1b) : 0x1a1a17)), Layer.Expanded ? 8 * S : 8 * S,
            Layer.Expanded ? (ImDrawFlags_RoundCornersTop) : ImDrawFlags_RoundCornersAll);
        if (Layer.Expanded) Ink->AddRect(ImVec2(CardX, RowOrigin.y), ImVec2(CardX + CardWidth, RowOrigin.y + RowHeight), ImGui::GetColorU32(Spectrum(0x2a2a30)), 8 * S, ImDrawFlags_RoundCornersTop);
        if (Selected) Ink->AddRectFilled(ImVec2(CardX, RowOrigin.y), ImVec2(CardX + 3 * S, RowOrigin.y + RowHeight), ImGui::GetColorU32(Spectrum(0xe8e8e8)), 2 * S);

        ImGui::PushID(Index);
        const float Indent = float(Layer.Depth) * 16 * S;

        // --- drag handle (grip) -------------------------------------------------------------------------
        ImGui::SetCursorScreenPos(ImVec2(CardX + 6 * S + Indent, RowOrigin.y + (RowHeight - 14 * S) * .5f));
        ImGui::BeginDisabled(!LayerQuery.empty());
        ImGui::InvisibleButton("Grip", ImVec2(14 * S, 14 * S));
        Track(std::string("TextureLayerGrip:") + std::to_string(Index));
        if (ImGui::IsItemActivated()) { DragSourceIndex = Index; DragArmed = false; }
        if (DragSourceIndex == Index && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.f * S)) DragArmed = true;
        ImGui::EndDisabled();
        StrokeIcon(ImGui::GetItemRectMin(), 12 * S, GripPath, DragSourceIndex == Index && DragArmed ? 0xc9c9ba : 0x55554a);

        float TextX = CardX + 24 * S + Indent;
        if (Layer.Kind == "folder")
        {
            const ImVec2 Minimum(TextX, RowOrigin.y + (RowHeight - 18 * S) * .5f);
            ImGui::SetCursorScreenPos(Minimum);
            if (ImGui::InvisibleButton("Disclosure", ImVec2(18 * S, 18 * S))) Layer.Open = !Layer.Open;
            Track(std::string("TextureLayerDisclosure:") + std::to_string(Index));
            StrokeIcon(ImVec2(Minimum.x + 3 * S, Minimum.y + 3 * S), 12 * S, Layer.Open ? ChevronDownPath : ChevronRightPath, 0x9a9a8c);
            TextX += 22 * S;
        }

        // --- thumb: dark tile, a centred paint-ink swatch only while baseColor is painted, a small kind dot --
        const ImVec2 ThumbMin(TextX, RowOrigin.y + (RowHeight - 34 * S) * .5f), ThumbMax(ThumbMin.x + 34 * S, ThumbMin.y + 34 * S);
        Ink->AddRectFilled(ThumbMin, ThumbMax, ImGui::GetColorU32(Spectrum(0x000000)), 6 * S);
        Ink->AddRect(ThumbMin, ThumbMax, ImGui::GetColorU32(Spectrum(0xffffff, .05f)), 6 * S);
        const bool PaintsBaseColour = std::find(Layer.Channels.begin(), Layer.Channels.end(), std::string("baseColor")) != Layer.Channels.end();
        if (PaintsBaseColour)
        {
            const ImVec2 InkMin((ThumbMin.x + ThumbMax.x) * .5f - 7.5f * S, (ThumbMin.y + ThumbMax.y) * .5f - 7.5f * S);
            Ink->AddRectFilled(InkMin, ImVec2(InkMin.x + 15 * S, InkMin.y + 15 * S), ImGui::GetColorU32(Spectrum(Layer.Paint)), 3 * S);
        }
        Ink->AddCircleFilled(ImVec2(ThumbMax.x - 6 * S, ThumbMin.y + 6 * S), 3 * S, ImGui::GetColorU32(Spectrum(Meta.Colour)));

        const ImVec2 SelectMin(ThumbMax.x + 8 * S, RowOrigin.y), SelectMax(CardX + CardWidth - 100 * S, RowOrigin.y + RowHeight);
        ImGui::SetCursorScreenPos(SelectMin);
        if (ImGui::InvisibleButton("Select", ImVec2(std::max(10.f, SelectMax.x - SelectMin.x), RowHeight)))
            SelectLayer(Index);
        Track(std::string("TextureLayerSelect:") + std::to_string(Index));
        Ink->PushClipRect(SelectMin, SelectMax, true);
        Caption(ImVec2(SelectMin.x, RowOrigin.y + 10 * S), Layer.Name.c_str(), 11 * S, Layer.Visible ? 0xd5d5c4 : 0x7a7a6c);
        char Meta2[96];
        if (Layer.Kind == "folder")
            std::snprintf(Meta2, sizeof(Meta2), "%d item%s", Layer.ChildCount, Layer.ChildCount == 1 ? "" : "s");
        else
            std::snprintf(Meta2, sizeof(Meta2), "%s \xC2\xB7 %s \xC2\xB7 %zu ch", Meta.Label, Layer.Blend.c_str(), Layer.Channels.size());
        Caption(ImVec2(SelectMin.x, RowOrigin.y + 30 * S), Meta2, 8.5f * S, 0x8f8f7e);
        Ink->PopClipRect();

        // --- opacity scrub pill, eye (always visible), settings caret ------------------------------------
        ImGui::SetCursorScreenPos(ImVec2(CardX + CardWidth - 92 * S, RowOrigin.y + (RowHeight - 18 * S) * .5f));
        ConstructOpacityScrub((std::string("TextureLayerOpacity:") + std::to_string(Index)).c_str(), Layer.Opacity, 36 * S, 18 * S);

        ImGui::SetCursorScreenPos(ImVec2(CardX + CardWidth - 50 * S, RowOrigin.y + (RowHeight - 20 * S) * .5f));
        if (ImGui::InvisibleButton("Eye", ImVec2(20 * S, 20 * S))) Layer.Visible = !Layer.Visible;
        Track(std::string("TextureLayerEye:") + std::to_string(Index));
        StrokeIcon(ImGui::GetItemRectMin(), 14 * S, Layer.Visible ? EyePath : EyeOffPath, Layer.Visible ? 0xb6b6a6 : 0x5f5f54);
        Controls.Tooltip(Layer.Visible ? "Hide layer" : "Show layer", Layer.Visible ? "Hide layer" : "Show layer");

        ImGui::SetCursorScreenPos(ImVec2(CardX + CardWidth - 24 * S, RowOrigin.y + (RowHeight - 20 * S) * .5f));
        if (ImGui::InvisibleButton("Caret", ImVec2(20 * S, 20 * S))) Layer.Expanded = !Layer.Expanded;
        Track(std::string("TextureLayerCaret:") + std::to_string(Index));
        StrokeIcon(ImGui::GetItemRectMin(), 13 * S, Layer.Expanded ? ChevronDownPath : ChevronRightPath, Layer.Expanded ? 0xd5d5c4 : 0x8f8f7e);
        Controls.Tooltip(Layer.Expanded ? "Collapse" : "Expand", "Blend, opacity, tag and mask for this layer.");

        float RowBottom = RowOrigin.y + RowHeight;
        if (Layer.Expanded)
        {
            // 📝 The expand's final height isn't known until its content is laid out, so its backing rect is
            // drawn with a split channel - content goes on the top channel first, the background fill is
            // queued on the bottom channel once the real bottom Y is known, then both merge back in order.
            Ink->ChannelsSplit(2);
            Ink->ChannelsSetCurrent(1);
            ImGui::SetCursorScreenPos(ImVec2(CardX, RowBottom));
            ConstructLayerExpand(Layer, CardWidth, Index);
            RowBottom = ImGui::GetCursorScreenPos().y;
            Ink->ChannelsSetCurrent(0);
            Ink->AddRectFilled(ImVec2(CardX, RowOrigin.y + RowHeight), ImVec2(CardX + CardWidth, RowBottom),
                ImGui::GetColorU32(Spectrum(0x141414)), 8 * S, ImDrawFlags_RoundCornersBottom);
            Ink->ChannelsMerge();
        }
        ImGui::PopID();

        ImGui::SetCursorScreenPos(ImVec2(RowOrigin.x, RowBottom + 4 * S));
        LastBottom = RowBottom + 4 * S;
    }
    if (!AnyDrawn) ImGui::TextColored(Spectrum(0x75756a), "No layers match this filter.");
    if (Dragging && DragHoverSlot < 0) DragHoverSlot = int(Layers.size());
    if (Dragging)
    {
        float LineY = LastBottom - 4 * S;
        if (DragHoverSlot < int(Layers.size()) && RowTopY[size_t(DragHoverSlot)] >= 0.f) LineY = RowTopY[size_t(DragHoverSlot)] - 2 * S;
        Ink->AddLine(ImVec2(Origin.x + 8 * S, LineY), ImVec2(Origin.x + Width - 8 * S, LineY), ImGui::GetColorU32(Spectrum(0xe8e8e8)), 2 * S);
    }
    if (DragSourceIndex >= 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        if (DragArmed && DragHoverSlot >= 0 && DragHoverSlot != DragSourceIndex && DragHoverSlot != DragSourceIndex + 1 &&
            DragSourceIndex < int(Layers.size()))
        {
            const auto Moved = Layers[size_t(DragSourceIndex)];
            Layers.erase(Layers.begin() + DragSourceIndex);
            const int Target = DragHoverSlot > DragSourceIndex ? DragHoverSlot - 1 : DragHoverSlot;
            Layers.insert(Layers.begin() + std::clamp(Target, 0, int(Layers.size())), Moved);
            SelectedLayer = std::clamp(Target, 0, int(Layers.size()) - 1);
        }
        DragSourceIndex = -1;
        DragHoverSlot = -1;
        DragArmed = false;
    }
    if (Layers.empty()) ImGui::TextColored(Spectrum(0x75756a), "No layers yet - use + to add one.");
    // 📝 The loop above ends with a bare SetCursorScreenPos (positioning the *next* row that never comes on
    // the last iteration) - submit a zero-size item here so the child's content rect actually grows to match,
    // instead of leaving ImGui's cursor past every real item and tripping its "extends window boundaries"
    // recoverable-error check.
    ImGui::Dummy(ImVec2(1, 1));
    if (Hidden) { ImGui::TextColored(Spectrum(0x716f62), "%zu / 64 \xC2\xB7 %zu hidden", Layers.size(), Hidden); }
    else { ImGui::TextColored(Spectrum(0x716f62), "%zu / 64", Layers.size()); }
    ImGui::EndChild();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     VIEWPORT DOCK (CHROME ONLY - NO REAL 3D CANVAS)
//------------------------------------------------------------------------------------------------------------------------

bool TextureWorkspacePanel::ConstructInlineDropdown(const char *Label, const char *Id, int &Selection,
    std::span<const char *const> Captions, float ComboWidth)
{
    const float S = InterfaceScale;
    ImGui::PushID(Id);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(Spectrum(0x747467), "%s", Label);
    ImGui::SameLine(0, 6 * S);
    bool Changed = false;
    if (!Captions.empty())
    {
        ImGui::SetNextItemWidth(ComboWidth);
        if (DropdownControlSurface::BeginSelection("##Selection", Captions[size_t(std::clamp(Selection, 0, int(Captions.size()) - 1))], S))
        {
            for (size_t Index = 0; Index < Captions.size(); ++Index)
                if (DropdownControlSurface::Option(Captions[Index], Selection == int(Index), S)) { Selection = int(Index); Changed = true; }
            DropdownControlSurface::EndSelection();
        }
        Track(Id);
    }
    ImGui::PopID();
    return Changed;
}

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

    // 📝 Was built from Controls.Dropdown() - the inspector's full-width, label-ABOVE-box form field - laid
    // out with plain ImGui::SameLine() calls. Controls.Dropdown() force-overrides its combo to the full
    // remaining line width (SetNextItemWidth(-1)) regardless of what the caller asked for, so every entry here
    // actually rendered on its OWN line and stacked on top of the authoring bar / "Stamp studio ->" link below
    // it instead of sitting in one compact row. ConstructInlineDropdown() is the single-line "label beside a
    // fixed-width box" variant the web app's .tp-header-field uses for exactly this toolbar.
    Controls.ControlHeight = 26;
    const char *ShadingLabels[]{"Lit", "Wireframe", "Matcap"};
    const char *TargetLabels[]{"Whole object", "Lid", "Body", "Handle", "Spout"};
    const char *UVLabels[]{"UV0", "UV1"};
    const float ShadingBoxWidth = 92 * S, TargetBoxWidth = 110 * S, UVBoxWidth = 66 * S;
    const float FieldGap = 6 * S, GroupGap = 10 * S, IconGap = 4 * S, IconWidth = 28 * S;
    const float GroupWidth =
        ImGui::CalcTextSize("Shading").x + FieldGap + ShadingBoxWidth + GroupGap +
        ImGui::CalcTextSize("Target").x + FieldGap + TargetBoxWidth + GroupGap +
        ImGui::CalcTextSize("UV set").x + FieldGap + UVBoxWidth + GroupGap +
        IconWidth + IconGap + IconWidth + IconGap + IconWidth;
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + Width - GroupWidth, Origin.y + 8 * S));
    ImGui::BeginGroup();
    {
        int ShadingIndex = int(Shading);
        if (ConstructInlineDropdown("Shading", "ShadingSelect", ShadingIndex, ShadingLabels, ShadingBoxWidth)) Shading = ShadingMode(ShadingIndex);
        ImGui::SameLine(0, GroupGap);
        ConstructInlineDropdown("Target", "TargetSelect", TargetPart, TargetLabels, TargetBoxWidth);
        ImGui::SameLine(0, GroupGap);
        ConstructInlineDropdown("UV set", "UVSetSelect", UVSet, UVLabels, UVBoxWidth);
        ImGui::SameLine(0, GroupGap);
        if (Controls.Button("Frame", IconWidth, false, FramePath)) Info = "Frame view (chrome only - no live camera behind this).";
        Controls.Tooltip("Frame selection", "Frame selection");
        ImGui::SameLine(0, IconGap);
        if (Controls.Button("Grid", IconWidth, GridVisible, GridPath)) GridVisible = !GridVisible;
        Controls.Tooltip("Toggle grid", "Toggle grid");
        ImGui::SameLine(0, IconGap);
        if (Controls.Button("Reset", IconWidth, false, ResetPath)) ZoomPercent = 100;
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
    ImGui::TextColored(Spectrum(0x87876f), "%s", Digital != DigitalTool::None ? "Digital tool armed \xC2\xB7 click the surface to apply" :
        Tool == AuthoringTool::Stamp ? "Stamp placement \xC2\xB7 click the surface to anchor a decal" :
        Tool == AuthoringTool::Transform ? "Transform \xC2\xB7 drag handles to move / rotate / scale" : "Navigate \xC2\xB7 drag to orbit");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 96 * S + ImGui::GetCursorPosX());
    if (Controls.Button("Stamp studio ->", 94 * S, false))
    {
        if (ActivateStamp) ActivateStamp();
    }
    Controls.Tooltip("Open Stamp studio", "Open the Stamp workspace to author reusable decal artwork.");

    // --- mask toolbar (only when the selected row is a mask) -----------------------------------------------
    float ChromeY = BarY + 62 * S;

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

    ImGui::TextColored(Spectrum(0x7c7c6d), "Teapot / %s", Layer.Kind == "folder" ? "Layer folder" : "Surface layer");
    ImGui::Dummy(ImVec2(1, 8 * S));
    const auto IdentityOrigin = ImGui::GetCursorScreenPos();
    auto *Ink = ImGui::GetWindowDrawList();
    const ImVec2 IconMin(IdentityOrigin.x, IdentityOrigin.y), IconMax(IconMin.x + 38 * S, IconMin.y + 38 * S);
    Ink->AddRectFilled(IconMin, IconMax, ImGui::GetColorU32(Spectrum(0x161614)), 8 * S);
    Ink->AddCircleFilled(ImVec2((IconMin.x + IconMax.x) * .5f, (IconMin.y + IconMax.y) * .5f), 7 * S, ImGui::GetColorU32(Spectrum(Meta.Colour)));
    Caption(ImVec2(IconMax.x + 10 * S, IdentityOrigin.y), (std::string(Meta.Label) + " LAYER").c_str(), 7.5f * S, 0x7c7c6d);
    Caption(ImVec2(IconMax.x + 10 * S, IdentityOrigin.y + 12 * S), Layer.Name.c_str(), 12 * S, 0xd9d9cc);
    char Position[80];
    std::snprintf(Position, sizeof(Position), "Position %d of %zu \xC2\xB7 top to bottom", SelectedLayer + 1, Layers.size());
    Caption(ImVec2(IconMax.x + 10 * S, IdentityOrigin.y + 26 * S), Position, 8.5f * S, 0x8f8f7e);
    ImGui::SetCursorScreenPos(ImVec2(IdentityOrigin.x, IdentityOrigin.y + 46 * S));

    if (Controls.Button(Layer.Visible ? "Visible" : "Hidden", 92 * S, false, Layer.Visible ? EyePath : EyeOffPath)) Layer.Visible = !Layer.Visible;
    ImGui::SameLine(0, 6 * S);
    if (Controls.Button(Layer.Locked ? "Locked" : "Unlocked", 92 * S, false, Layer.Locked ? LockPath : UnlockPath)) Layer.Locked = !Layer.Locked;
    ImGui::Dummy(ImVec2(1, 10 * S));

    if (Layer.Kind == "folder")
    {
        ImGui::TextColored(Spectrum(0x7c7c6d), "FOLDER SETTINGS");
        Controls.BeginCard("", true);
        {
            char NameBuffer[64];
            std::snprintf(NameBuffer, sizeof(NameBuffer), "%s", Layer.Name.c_str());
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##FolderName", NameBuffer, sizeof(NameBuffer))) Layer.Name = NameBuffer;
            ImGui::TextColored(Spectrum(0x747467), "Blend");
            int BlendIndex = 0;
            for (int Index = 0; Index < 7; ++Index) if (Layer.Blend == kBlendModes[Index]) BlendIndex = Index;
            if (ConstructRadioDropdown("FolderBlend", kBlendModes[size_t(BlendIndex)], BlendIndex, kBlendModes, ImGui::GetContentRegionAvail().x))
                Layer.Blend = kBlendModes[size_t(BlendIndex)];
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
        ImGui::TextColored(Spectrum(0x747467), "Blend");
        int BlendIndex = 0;
        for (int Index = 0; Index < 7; ++Index) if (Layer.Blend == kBlendModes[Index]) BlendIndex = Index;
        if (ConstructRadioDropdown("LayerBlend", kBlendModes[size_t(BlendIndex)], BlendIndex, kBlendModes, ImGui::GetContentRegionAvail().x))
            Layer.Blend = kBlendModes[size_t(BlendIndex)];
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

    {
        // 📝 1:1 with ChannelPropertyPanel.html's ConstructChipsRegion - always present (even with zero
        // channels enabled, which shows the empty-state note) so "+" is always reachable, not hidden away the
        // moment a layer happens to start with no channels. Header: a Channels label, a count badge and a
        // "Clear all" link (shown only once more than the permanently-locked baseColor channel is enabled).
        const bool HasBaseColour = std::find(Layer.Channels.begin(), Layer.Channels.end(), std::string("baseColor")) != Layer.Channels.end();
        const size_t Removable = Layer.Channels.size() - (HasBaseColour ? 1 : 0);
        Controls.BeginCard("", true);
        {
            const auto HeadOrigin = ImGui::GetCursorScreenPos();
            Caption(HeadOrigin, "CHANNELS", 9 * S, 0x8b8b7f);
            char CountBadge[16];
            std::snprintf(CountBadge, sizeof(CountBadge), "%zu", Layer.Channels.size());
            const auto BadgeTextSize = ImGui::CalcTextSize(CountBadge);
            const ImVec2 BadgeMin(HeadOrigin.x + 62 * S, HeadOrigin.y - 2 * S);
            Ink->AddRectFilled(BadgeMin, ImVec2(BadgeMin.x + BadgeTextSize.x * .72f + 12 * S, BadgeMin.y + 14 * S), ImGui::GetColorU32(Spectrum(0xe8e8e8)), 7 * S);
            Caption(ImVec2(BadgeMin.x + 6 * S, BadgeMin.y + 2 * S), CountBadge, 8 * S, 0x111111);
            if (Removable > 0)
            {
                ImGui::SetCursorScreenPos(ImVec2(HeadOrigin.x + ImGui::GetContentRegionAvail().x - 52 * S, HeadOrigin.y - 2 * S));
                if (ImGui::InvisibleButton("ClearAllHit", ImVec2(52 * S, 16 * S)))
                {
                    for (const auto &Entry : QueryChannelCatalogue())
                        if (!IsLockedChannel(Entry.Id)) ToggleChannel(Layer, Entry.Id, false);
                }
                Track("Clear all");
                Caption(ImGui::GetItemRectMin(), "Clear all", 9 * S, ImGui::IsItemHovered() ? 0xe05a5a : 0x8a8a7d);
            }
            ImGui::SetCursorScreenPos(ImVec2(HeadOrigin.x, HeadOrigin.y + 18 * S));

            // --- chips: tint dot + label + a per-chip "x" (inert/dimmed for the locked baseColor channel) ---
            const float RightEdge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            bool FirstChip = true;
            for (const auto &Id : Layer.Channels)
            {
                const auto *ChannelInfo = QueryChannelMeta(Id);
                if (!ChannelInfo) continue;
                const bool Locked = IsLockedChannel(Id);
                const float ChipHeight = 22 * S;
                const float ChipWidth = 16 * S + ImGui::CalcTextSize(ChannelInfo->Label).x + 22 * S;
                if (!FirstChip)
                {
                    if (ImGui::GetCursorScreenPos().x + 6 * S + ChipWidth > RightEdge) ImGui::NewLine();
                    else ImGui::SameLine(0, 6 * S);
                }
                FirstChip = false;
                ImGui::PushID(Id.c_str());
                const auto ChipOrigin = ImGui::GetCursorScreenPos();
                Ink->AddRectFilled(ChipOrigin, ImVec2(ChipOrigin.x + ChipWidth, ChipOrigin.y + ChipHeight), ImGui::GetColorU32(Spectrum(0x191916)), ChipHeight * .5f);
                Ink->AddCircleFilled(ImVec2(ChipOrigin.x + 11 * S, ChipOrigin.y + ChipHeight * .5f), 3 * S, ImGui::GetColorU32(Spectrum(ChannelInfo->Tint)));
                Caption(ImVec2(ChipOrigin.x + 19 * S, ChipOrigin.y + (ChipHeight - 9 * S) * .5f), ChannelInfo->Label, 9 * S, 0xbfbfae);
                ImGui::SetCursorScreenPos(ImVec2(ChipOrigin.x + ChipWidth - 17 * S, ChipOrigin.y + (ChipHeight - 14 * S) * .5f));
                ImGui::BeginDisabled(Locked);
                if (ImGui::InvisibleButton("Remove", ImVec2(14 * S, 14 * S))) ToggleChannel(Layer, Id, false);
                ImGui::EndDisabled();
                Track(std::string("ChannelChipRemove:") + Id);
                StrokeIcon(ImGui::GetItemRectMin(), 10 * S, CrossPath, Locked ? 0x3f3f38 : ImGui::IsItemHovered() ? 0xc9c9ba : 0x86867d);
                if (!Locked) Controls.Tooltip((std::string("Disable ") + ChannelInfo->Label).c_str(), "Removes this channel target from the layer.");
                ImGui::SetCursorScreenPos(ImVec2(ChipOrigin.x + ChipWidth, ChipOrigin.y));
                ImGui::PopID();
            }
            // --- the round "+" add-channel button, ending the chip row (ChannelPropertyPanel.html's .add-wrap) ---
            {
                const float AddSize = 22 * S;
                if (!FirstChip)
                {
                    if (ImGui::GetCursorScreenPos().x + 6 * S + AddSize > RightEdge) ImGui::NewLine();
                    else ImGui::SameLine(0, 6 * S);
                }
                const auto AddOrigin = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("Add channel", ImVec2(AddSize, AddSize));
                Track("Add channel");
                const bool AddOpen = ImGui::IsPopupOpen("ChannelAddMenu");
                Ink->AddCircle(ImVec2(AddOrigin.x + AddSize * .5f, AddOrigin.y + AddSize * .5f), AddSize * .5f - 1 * S,
                    ImGui::GetColorU32(Spectrum(AddOpen || ImGui::IsItemHovered() ? 0xe8e8e8 : 0x4a4a42)), 0, 1.4f * S);
                StrokeIcon(ImVec2(AddOrigin.x + AddSize * .5f - 5 * S, AddOrigin.y + AddSize * .5f - 5 * S), 10 * S, PlusPath,
                    AddOpen || ImGui::IsItemHovered() ? 0xe8e8e8 : 0x8a8a7d, 1.6f);
                if (ImGui::IsItemClicked()) ImGui::OpenPopup("ChannelAddMenu");
                Controls.Tooltip("Add channel", "Enable another painting channel target on this layer.");
                if (DropdownControlSurface::BeginMenu("ChannelAddMenu", S)) { ConstructChannelAddMenu(Layer); DropdownControlSurface::EndMenu(); }
            }
            ImGui::NewLine();
            if (Layer.Channels.empty())
                ImGui::TextColored(Spectrum(0x75756a), "No painting channels enabled. Existing channel values are retained.");
        }
        Controls.EndCard();
        for (const auto &Entry : QueryChannelCatalogue())
            if (std::find(Layer.Channels.begin(), Layer.Channels.end(), Entry.Id) != Layer.Channels.end())
                ConstructChannelCard(Width, Entry.Id, Entry.Label, Entry.EditKind, Entry.Note, Entry.Placement, Entry.Minimum, Entry.Maximum,
                    Entry.Default, Entry.DefaultColour, Meta.Colour);
    }
}

void TextureWorkspacePanel::ConstructChannelAddMenu(TextureLayerRow &Layer)
{
    // 📝 1:1 with ChannelPropertyPanel.html's ".add-menu" - grouped Surface/Radiance/Reflectance/Scattering
    // rows, each a plain click-to-toggle entry (a checkmark marks an enabled one) - NEVER a switch/toggle
    // control. Clicking a row does not close the menu (OpenPicker survives a toggle in the reference too), so
    // several channels can be enabled in one visit; only the "+" button or clicking outside closes it.
    const float S = InterfaceScale;
    const auto &Catalogue = QueryChannelCatalogue();
    ImGui::TextDisabled("ADD CHANNEL");
    for (const char *Group : ChannelGroups)
    {
        ImGui::TextDisabled("%s", Group);
        for (const auto &Entry : Catalogue)
        {
            if (std::strcmp(Entry.Group, Group) != 0) continue;
            const bool Locked = IsLockedChannel(Entry.Id);
            const bool On = std::find(Layer.Channels.begin(), Layer.Channels.end(), std::string(Entry.Id)) != Layer.Channels.end();
            ImGui::PushID(Entry.Id);
            const float RowWidth = std::max(ImGui::GetContentRegionAvail().x, 200.f * S);
            ImGui::BeginDisabled(Locked);
            // 📝 Dear ImGui's Selectable() now auto-closes its parent popup by default (ImGuiItemFlags_
            // AutoClosePopups) - ImGuiSelectableFlags_NoAutoClosePopups is required here so toggling a row
            // behaves like ChannelPropertyPanel.html's OpenPicker (stays open across several picks); only the
            // "+" button itself or an outside click should close this menu.
            const bool Pressed = ImGui::Selectable("##Row", On, ImGuiSelectableFlags_NoAutoClosePopups, ImVec2(RowWidth, DropdownControlSurface::RowHeight * S));
            ImGui::EndDisabled();
            const auto Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
            auto *Ink = ImGui::GetWindowDrawList();
            Ink->AddCircleFilled(ImVec2(Minimum.x + 10 * S, (Minimum.y + Maximum.y) * .5f), 4 * S, ImGui::GetColorU32(Spectrum(Entry.Tint, Locked ? .4f : 1.f)));
            Ink->AddText(ImVec2(Minimum.x + 20 * S, Minimum.y + (Maximum.y - Minimum.y - ImGui::GetFontSize()) * .5f),
                ImGui::GetColorU32(Spectrum(Locked ? 0x5d5d54 : 0xd5d5c4)), Entry.Label);
            if (On)
            {
                PixelSpace Surface;
                (void)Surface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 1);
                GlyphSpace::Stroke(Surface, CheckPath, {Maximum.x - 18 * S, (Minimum.y + Maximum.y) * .5f - 6 * S, 12 * S, 1.6f, SpectrumQuad(Locked ? 0x5d5d54 : 0xe8e8e8)});
            }
            Track(Entry.Label);
            ImGui::PopID();
            if (Pressed && !Locked) ToggleChannel(Layer, Entry.Id, !On);
        }
        ImGui::Spacing();
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 220 * S);
    ImGui::TextColored(Spectrum(0x686859), "Toggling Normal also toggles Height - Normal is always derived from it.");
    ImGui::PopTextWrapPos();
}

void TextureWorkspacePanel::ConstructChannelCard(float Width, const std::string &Id, const char *Label,
    const char *EditKind, const char *Note, const char *Placement, float Minimum, float Maximum, float Default,
    const std::array<float, 4> &DefaultColour, uint32_t HeaderHue)
{
    const float S = InterfaceScale;
    const bool Derived = std::strcmp(EditKind, "derived") == 0;
    // 📝 Every nested control below (the Texture/Value/Generator switch, the swatch/slider/dropdown bodies)
    // reuses the exact same literal captions across every channel card ("Texture", "Amount", "Generator"...) -
    // without this PushID scope they'd all resolve to the same ImGui ID within this shared "Channel targets"
    // card and Dear ImGui would raise its "2 visible items with conflicting ID" programmer error the moment two
    // channel cards are open at once. Scoping the whole card by the channel's stable id (not the display
    // Label, which can collide with other UI strings) fixes every control inside for free.
    ImGui::PushID(Id.c_str());
    const std::string Key = QueryChannelKey(Id);
    int &Mode = ChannelSourceMode.try_emplace(Key, Derived ? 2 : std::strcmp(EditKind, "color") == 0 ? 1 : 0).first->second;
    Controls.BeginCard("", true);
    {
        const auto HeadOrigin = ImGui::GetCursorScreenPos();
        auto *Ink = ImGui::GetWindowDrawList();
        // 🔴 1:1 with ChannelPropertyPanel.html's ".ch-dot" - every channel card on a layer shares the SAME
        // classification hue (the layer's own Kind), not a per-channel tint; the per-channel tint lives only
        // on the chip/add-menu swatches above.
        Ink->AddCircleFilled(ImVec2(HeadOrigin.x + 5 * S, HeadOrigin.y + 7 * S), 4 * S, ImGui::GetColorU32(Spectrum(HeaderHue)));
        if (ImGui::InvisibleButton("Fold", ImVec2(Width - 24 * S, 20 * S)))
        {
            // ChannelPropertyPanel.html folds via Collapsed.has(Key) - a plain click-to-fold accordion with no
            // persisted "Folded" set needed here since every card simply starts open.
        }
        Caption(ImVec2(HeadOrigin.x + 16 * S, HeadOrigin.y + 2 * S), Label, 10 * S, 0xd9d4c4);
        // 📝 Mirrors ChannelPropertyPanel.html's `.ch-src` - names whichever source mode is actually active
        // right now, exactly "Value"/"Texture"/"Generator"/"Derived", never "Colour" or "Height-derived".
        const char *ModeCaption = Derived ? "Derived" : Mode == 1 ? "Value" : Mode == 2 ? "Generator" : "Texture";
        Caption(ImVec2(HeadOrigin.x + Width - 110 * S, HeadOrigin.y + 3 * S), ModeCaption, 8.5f * S, 0x9a9a8c);
        ImGui::SetCursorScreenPos(ImVec2(HeadOrigin.x, HeadOrigin.y + 24 * S));
        if (Derived)
        {
            // 📝 Normal has no source switch at all in the reference - it only ever follows Height.
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
            ImGui::TextColored(Spectrum(0x868872), "%s", Note);
            ImGui::PopTextWrapPos();
            ConstructChannelPreview("Derived", Placement, false, 0);
        }
        else
        {
            const char *Modes[]{"Texture", "Value", "Generator"};
            for (int Index = 0; Index < 3; ++Index)
            {
                if (Index) ImGui::SameLine(0, 4 * S);
                if (Controls.Button(Modes[Index], 0, Mode == Index, {}, true)) Mode = Index;
                // 📝 Controls.Button() registers its extent under the bare literal Caption ("Texture"/"Value"/
                // "Generator") with no awareness of this card's enclosing PushID(Id) scope - two simultaneously
                // -open channel cards (the normal case: every assigned channel's card is always expanded, not
                // an accordion) would otherwise leave QueryControlExtent("Generator") ambiguously resolving to
                // whichever card happened to draw its button last. Re-Track() it under a channel-qualified key
                // immediately so tooling can reliably target one specific channel's mode switch.
                Track(std::string("ChannelMode:") + Id + ":" + Modes[Index]);
            }
            ImGui::Dummy(ImVec2(1, 6 * S));
            if (Mode == 0) ConstructChannelTextureSource(Key, Id, Label);
            else if (Mode == 1) ConstructChannelColourSource(Key, Id, Label, EditKind, Minimum, Maximum, Default, DefaultColour);
            else ConstructChannelGeneratorSource(Key, Label);
            if (Mode != 0) ConstructChannelPreview(Modes[Mode], Placement, Mode == 1, Mode == 1 && std::strcmp(EditKind, "color") == 0 ? PackColour(ChannelColour.try_emplace(Key, DefaultColour).first->second) : 0);
        }
    }
    Controls.EndCard();
    ImGui::PopID();
}

void TextureWorkspacePanel::ConstructChannelPreview(const char *ModeCaption, const char *Placement, bool SolidFill, uint32_t FillColour)
{
    // 📝 1:1 with ChannelPropertyPanel.html's ConstructPreview - a small tile (a flat fill for Value mode, a
    // chequerboard for everything else since nothing is actually rasterised here) plus "{Mode} preview" and
    // the channel's atlas placement string.
    const float S = InterfaceScale;
    ImGui::Dummy(ImVec2(1, 5 * S));
    const auto Origin = ImGui::GetCursorScreenPos();
    const ImVec2 TileMax(Origin.x + 30 * S, Origin.y + 30 * S);
    if (SolidFill) ImGui::GetWindowDrawList()->AddRectFilled(Origin, TileMax, ImGui::GetColorU32(Spectrum(FillColour)), 6 * S);
    else DrawCheckerboard(Origin, TileMax, 7 * S);
    ImGui::GetWindowDrawList()->AddRect(Origin, TileMax, ImGui::GetColorU32(Spectrum(0xffffff, .05f)), 6 * S);
    Caption(ImVec2(TileMax.x + 8 * S, Origin.y + 2 * S), (std::string(ModeCaption) + " preview").c_str(), 9.5f * S, 0x9a9a8c);
    Caption(ImVec2(TileMax.x + 8 * S, Origin.y + 14 * S), Placement, 8.5f * S, 0x6c6c5e);
    ImGui::Dummy(ImVec2(1, 34 * S));
}

void TextureWorkspacePanel::ConstructChannelTextureSource(const std::string &Key, const std::string &Id, const char *Label)
{
    // 📝 1:1 with ChannelPropertyPanel.html's ConstructTextureSlot - TWO slots, not one: the painted strokes
    // (always present, since the atlas IS the paint storage) and a separately optional imported base beneath
    // it. Deleting the import leaves the paint untouched; erasing the paint leaves the import untouched.
    const float S = InterfaceScale;
    const float Width = ImGui::GetContentRegionAvail().x;
    auto *Ink = ImGui::GetWindowDrawList();
    int &Strokes = ChannelStrokeCount.try_emplace(Key, 0).first->second;
    const bool Painted = Strokes > 0;

    {
        const auto Origin = ImGui::GetCursorScreenPos();
        const float SlotHeight = 46 * S;
        Ink->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + SlotHeight), ImGui::GetColorU32(Spectrum(0x161614)), 6 * S);
        const ImVec2 ThumbMax(Origin.x + 6 * S + 34 * S, Origin.y + 6 * S + 34 * S);
        if (Painted) Ink->AddRectFilled(ImVec2(Origin.x + 6 * S, Origin.y + 6 * S), ThumbMax, ImGui::GetColorU32(Spectrum(QueryChannelMeta(Id) ? QueryChannelMeta(Id)->Tint : 0x8a8a8a)), 6 * S);
        else DrawCheckerboard(ImVec2(Origin.x + 6 * S, Origin.y + 6 * S), ThumbMax, 6 * S);
        StrokeIcon(ImVec2(Origin.x + 6 * S + 10 * S, Origin.y + 6 * S + 10 * S), 14 * S, Painted ? BrushPath : ImagePath, Painted ? 0x161614 : 0x5c5c51);
        Caption(ImVec2(Origin.x + 48 * S, Origin.y + 10 * S), Painted ? "Painted strokes" : "No strokes yet", 10 * S, 0xd5d5c4);
        char StrokeMeta[64];
        if (Painted) std::snprintf(StrokeMeta, sizeof(StrokeMeta), "%d strokes \xC2\xB7 2048 x 2048 atlas", Strokes);
        else std::snprintf(StrokeMeta, sizeof(StrokeMeta), "Atlas allocates on the first stroke");
        Caption(ImVec2(Origin.x + 48 * S, Origin.y + 24 * S), StrokeMeta, 8.5f * S, 0x8f8f7e);
        ImGui::SetCursorScreenPos(ImVec2(Origin.x + Width - 30 * S, Origin.y + (SlotHeight - 22 * S) * .5f));
        ImGui::BeginDisabled(!Painted);
        if (ImGui::InvisibleButton("ErasePaint", ImVec2(22 * S, 22 * S))) Strokes = 0;
        Track(std::string("ErasePaint:") + Key);
        ImGui::EndDisabled();
        StrokeIcon(ImGui::GetItemRectMin(), 13 * S, TrashPath, Painted ? (ImGui::IsItemHovered() ? 0xe05a5a : 0x9a9a8c) : 0x4a4a42);
        if (Painted) Controls.Tooltip("Erase painted strokes", "Clears this channel's painted atlas back to empty.");
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + SlotHeight + 5 * S));
    }

    bool &HasImport = ChannelHasImportBase.try_emplace(Key, false).first->second;
    std::string &Format = ChannelImportFormat.try_emplace(Key, "sRGB 8").first->second;
    ImGui::TextColored(Spectrum(0x8b8b7f), "IMPORTED BASE%s", HasImport ? "" : " - OPTIONAL");
    ImGui::Dummy(ImVec2(1, 2 * S));
    if (!HasImport)
    {
        const auto Origin = ImGui::GetCursorScreenPos();
        const float SlotHeight = 36 * S;
        ImGui::InvisibleButton("ImportBase", ImVec2(Width, SlotHeight));
        Track(std::string("ImportBase:") + Key);
        const bool Hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) { HasImport = true; Format = "sRGB 8"; }
        Ink->AddRect(Origin, ImVec2(Origin.x + Width, Origin.y + SlotHeight), ImGui::GetColorU32(Spectrum(Hovered ? 0xe8e8e8 : 0x3a3a34, Hovered ? 1.f : .6f)), 7 * S, 0, 1.2f * S);
        StrokeIcon(ImVec2(Origin.x + Width * .5f - 34 * S, Origin.y + (SlotHeight - 12 * S) * .5f), 13 * S, UploadPath, Hovered ? 0xd5d5c4 : 0x75756a);
        Caption(ImVec2(Origin.x + Width * .5f - 12 * S, Origin.y + (SlotHeight - 9 * S) * .5f), "Import base", 9.5f * S, Hovered ? 0xd5d5c4 : 0x75756a);
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + SlotHeight + 5 * S));
    }
    else
    {
        const auto Origin = ImGui::GetCursorScreenPos();
        const float SlotHeight = 46 * S;
        Ink->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + SlotHeight), ImGui::GetColorU32(Spectrum(0x161614)), 6 * S);
        DrawCheckerboard(ImVec2(Origin.x + 6 * S, Origin.y + 6 * S), ImVec2(Origin.x + 6 * S + 34 * S, Origin.y + 6 * S + 34 * S), 6 * S);
        StrokeIcon(ImVec2(Origin.x + 6 * S + 10 * S, Origin.y + 6 * S + 10 * S), 14 * S, ImagePath, 0x8a8a7d);
        char ImportName[64];
        std::snprintf(ImportName, sizeof(ImportName), "%s_import.png", Id.c_str());
        Caption(ImVec2(Origin.x + 48 * S, Origin.y + 10 * S), ImportName, 10 * S, 0xd5d5c4);
        char ImportMeta[64];
        std::snprintf(ImportMeta, sizeof(ImportMeta), "2048 x 2048 \xC2\xB7 %s", Format.c_str());
        Caption(ImVec2(Origin.x + 48 * S, Origin.y + 24 * S), ImportMeta, 8.5f * S, 0x8f8f7e);
        ImGui::SetCursorScreenPos(ImVec2(Origin.x + Width - 56 * S, Origin.y + (SlotHeight - 22 * S) * .5f));
        if (ImGui::InvisibleButton("ReimportBase", ImVec2(22 * S, 22 * S))) Format = Format == "sRGB 8" ? "Linear 16" : "sRGB 8";
        Track(std::string("ReimportBase:") + Key);
        StrokeIcon(ImGui::GetItemRectMin(), 13 * S, ResetPath, ImGui::IsItemHovered() ? 0xd5d5c4 : 0x9a9a8c);
        Controls.Tooltip("Replace imported base", "Replace imported base");
        ImGui::SameLine(0, 4 * S);
        if (ImGui::InvisibleButton("DropBase", ImVec2(22 * S, 22 * S))) HasImport = false;
        Track(std::string("DropBase:") + Key);
        StrokeIcon(ImGui::GetItemRectMin(), 13 * S, TrashPath, ImGui::IsItemHovered() ? 0xe05a5a : 0x9a9a8c);
        Controls.Tooltip("Delete imported base", "Deletes the imported base - any painted strokes are untouched.");
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + SlotHeight + 5 * S));
    }

    ImGui::Dummy(ImVec2(1, 2 * S));
    if (Controls.Button("Paint strokes (demo)", Width, false, BrushPath)) Strokes += 37;
    Track(std::string("PaintStrokesDemo:") + Key);
    Controls.Tooltip("Paint strokes (demo)", "Stands in for painting on the surface - this native build has no paint compositor yet.");
    (void)Label;
}

void TextureWorkspacePanel::ConstructChannelColourSource(const std::string &Key, const std::string &Id, const char *Label,
    const char *EditKind, float Minimum, float Maximum, float Default, const std::array<float, 4> &DefaultColour)
{
    // 📝 ChannelPropertyPanel.html shows a real colour field for every edit:"color" channel (baseColor,
    // emission, sheen, subsurface - not just baseColor) and a slider for edit:"scalar" channels - each colour
    // channel keeps its own catalogue default (e.g. emission starts black, sheen starts dark grey) instead of
    // every channel defaulting to the teapot's clay orange.
    const bool IsColour = std::strcmp(EditKind, "color") == 0;
    if (IsColour)
    {
        auto &Swatch = ChannelColour.try_emplace(Key, DefaultColour).first->second;
        Controls.Colour("Colour", Swatch);
    }
    else
    {
        float &Amount = ChannelAmount.try_emplace(Key, Default).first->second;
        Controls.Range("Amount", Amount, Minimum, Maximum);
    }
    (void)Id;
    (void)Label;
}

void TextureWorkspacePanel::ConstructChannelGeneratorSource(const std::string &Key, const char *Label)
{
    // 📝 1:1 with ChannelPropertyPanel.html's ConstructGeneratorSlot - a "pickbar" (name + caret) opening a
    // grouped Mask/Wear/Procedural dropdown (a checkmark + no note on the active row, a dim note on the rest),
    // then once assigned: a note line, a Reset (Cog) and a Remove (Trash) icon button, and its parameter
    // sliders. Picking a generator and dragging its sliders is purely a UI affordance here; nothing samples a
    // mesh map or runs a noise kernel.
    const float S = InterfaceScale;
    const auto &Catalogue = QueryGeneratorCatalogue();
    int &Choice = ChannelGeneratorChoice.try_emplace(Key, -1).first->second;
    const bool Assigned = Choice >= 0;

    const auto Origin = ImGui::GetCursorScreenPos();
    const float Width = ImGui::GetContentRegionAvail().x;
    auto *Ink = ImGui::GetWindowDrawList();
    ImGui::InvisibleButton("GeneratorPick", ImVec2(Width, 26 * S));
    Track(std::string("GeneratorPick:") + Key);
    const bool Hovered = ImGui::IsItemHovered();
    const bool PopupOpen = ImGui::IsPopupOpen("GeneratorMenu");
    if (ImGui::IsItemClicked()) ImGui::OpenPopup("GeneratorMenu");
    Ink->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + 26 * S), ImGui::GetColorU32(Spectrum(Hovered || PopupOpen ? 0x1a1a18 : 0x141414)), 13 * S);
    Caption(ImVec2(Origin.x + 10 * S, Origin.y + 8 * S), Assigned ? Catalogue[size_t(Choice)].Label : "Choose generator", 9.5f * S, 0xd5d5c4);
    StrokeIcon(ImVec2(Origin.x + Width - 20 * S, Origin.y + 9 * S), 9 * S, PopupOpen ? "M18 15L12 9L6 15" : ChevronDownPath, 0x8f8f7e);
    if (DropdownControlSurface::BeginMenu("GeneratorMenu", S))
    {
        const char *LastGroup = "";
        for (const auto &Entry : Catalogue)
        {
            if (std::strcmp(Entry.Group, LastGroup) != 0) { ImGui::TextDisabled("%s", Entry.Group); LastGroup = Entry.Group; }
            const bool On = Assigned && std::strcmp(Catalogue[size_t(Choice)].Id, Entry.Id) == 0;
            // 📝 Option() only ever sizes a row to fit its own Caption label - it has no idea this row also
            // draws a right-aligned descriptive Note afterwards, so without reserving room for the Note's
            // real measured width up front (MeasureCaption(), not ImGui::CalcTextSize() - the Note renders at
            // Caption()'s own 8.5*S font size, not ImGui's current one), a long note like "World-axis ramp"
            // would overlap the tail of its own label instead of sitting cleanly to its right.
            const float NoteWidth = On ? 0.f : MeasureCaption(Entry.Note, 8.5f * S);
            const float RowMinimumWidth = On ? 0.f : ImGui::CalcTextSize(Entry.Label).x + 28 * S + NoteWidth + 2 * DropdownControlSurface::PaddingX * S;
            const bool Pressed = DropdownControlSurface::Option(
                (std::string(Entry.Label) + (On ? "" : std::string("##") + Entry.Id)).c_str(), On, S, true, RowMinimumWidth);
            if (!On)
            {
                const auto Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
                Caption(ImVec2(Maximum.x - NoteWidth - 10 * S, Minimum.y + (Maximum.y - Minimum.y) * .32f), Entry.Note, 8.5f * S, 0x6c6c5e);
            }
            Track(Entry.Label);
            if (Pressed)
            {
                for (size_t Index = 0; Index < Catalogue.size(); ++Index)
                    if (std::strcmp(Catalogue[Index].Id, Entry.Id) == 0)
                    {
                        Choice = int(Index);
                        for (const auto &Parameter : Catalogue[Index].Parameters)
                            if (Parameter.first) ChannelGeneratorParameters[Key + ":" + Parameter.first] = Parameter.second;
                    }
                ImGui::CloseCurrentPopup();
            }
        }
        DropdownControlSurface::EndMenu();
    }
    ImGui::Dummy(ImVec2(1, 6 * S));

    if (!Assigned)
    {
        ImGui::TextColored(Spectrum(0x75756a), "No generator picked yet for %s.", Label);
        return;
    }
    const auto &Picked = Catalogue[size_t(Choice)];
    ImGui::TextColored(Spectrum(0x8b8b7f), "%s", Picked.Note);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 52 * S + ImGui::GetCursorPosX());
    if (Controls.Button("Reset", 24 * S, false, CogPath)) for (const auto &Parameter : Picked.Parameters) if (Parameter.first) ChannelGeneratorParameters[Key + ":" + Parameter.first] = Parameter.second;
    Controls.Tooltip("Reset parameters", "Reset parameters");
    ImGui::SameLine(0, 4 * S);
    if (Controls.Button("RemoveGenerator", 24 * S, false, TrashPath)) Choice = -1;
    Controls.Tooltip("Remove generator", "Remove generator");
    ImGui::Dummy(ImVec2(1, 4 * S));
    for (const auto &Parameter : Picked.Parameters)
    {
        if (!Parameter.first) continue;
        const std::string ParameterKey = Key + ":" + Parameter.first;
        float &Value = ChannelGeneratorParameters.try_emplace(ParameterKey, Parameter.second).first->second;
        Controls.Range(Parameter.first, Value, 0.f, std::string(Parameter.first) == "Direction" ? 360.f : 1.f);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     INSPECTOR / HISTORY TAB
//------------------------------------------------------------------------------------------------------------------------

void TextureWorkspacePanel::ConstructInspectorHistory(float Width)
{
    // 📝 Chrome-only edit log - there is no real undo stack behind this build, so the entries below are a
    // representative, static recounting of the demo document's own InitializeContent() story rather than a
    // live append-on-every-edit timeline.
    const float S = InterfaceScale;
    const char *Filters[]{"All edits", "Strokes only"};
    for (int Index = 0; Index < 2; ++Index)
    {
        if (Index) ImGui::SameLine(0, 4 * S);
        if (Controls.Button(Filters[Index], (Width - 4 * S) * .5f, HistoryFilter == Index, {}, true)) HistoryFilter = Index;
    }
    ImGui::Dummy(ImVec2(1, 8 * S));

    struct HistoryEntry { const char *Caption; const char *Stamp; bool Stroke; };
    static const HistoryEntry Entries[]{
        {"Added Procedural noise (generator)", "6 steps ago", false},
        {"Painted Spout tint fill", "5 steps ago", false},
        {"Painted 212 strokes on Maker's stamp", "4 steps ago", true},
        {"Enabled Normal + Height on Scratch normal", "3 steps ago", false},
        {"Painted 48 strokes on Edge wear", "2 steps ago", true},
        {"Grouped Body details folder", "1 step ago", false},
        {"Document opened - Teapot study", "Just now", false},
    };
    for (const auto &Entry : Entries)
    {
        if (HistoryFilter == 1 && !Entry.Stroke) continue;
        const auto Origin = ImGui::GetCursorScreenPos();
        auto *Ink = ImGui::GetWindowDrawList();
        ImGui::InvisibleButton(Entry.Caption, ImVec2(Width, 34 * S));
        if (ImGui::IsItemHovered()) Ink->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + 34 * S), ImGui::GetColorU32(Spectrum(0x1a1a17)), 6 * S);
        Ink->AddCircleFilled(ImVec2(Origin.x + 10 * S, Origin.y + 17 * S), 3 * S, ImGui::GetColorU32(Spectrum(Entry.Stroke ? 0xf97316 : 0x6c6c5e)));
        ImGui::PushTextWrapPos(Origin.x + Width - 12 * S);
        Caption(ImVec2(Origin.x + 20 * S, Origin.y + 4 * S), Entry.Caption, 9.5f * S, 0xcfcfc0);
        ImGui::PopTextWrapPos();
        Caption(ImVec2(Origin.x + 20 * S, Origin.y + 20 * S), Entry.Stamp, 8 * S, 0x6c6c5e);
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 36 * S));
    }
    ImGui::Dummy(ImVec2(1, 6 * S));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + Width);
    ImGui::TextColored(Spectrum(0x686859), "Undo / redo is not wired to a real document history in this native build.");
    ImGui::PopTextWrapPos();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     TOP-LEVEL INTERFACE ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

void TextureWorkspacePanel::ConstructInterface()
{
    if (QueryPhase() != ActivationPhase::Active) return;
    ControlExtents.clear();

    // 🧩 Same themed style-colour/style-var block every other native TextureStudio workspace (Pattern/Stamp/
    // Baking) pushes around its own ConstructInterface() body - without it every raw ImGui widget here (the
    // viewport toolbar's combos, every popup background, slider grabs) falls back to ImGui's own default
    // dark-blue theme instead of this app's warm neutral palette.
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
    const auto ToneQuad = [](ColorQuad Tone) { return ImVec4(Tone.Red, Tone.Green, Tone.Blue, Tone.Alpha); };
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Spectrum(0x121212));
    ImGui::PushStyleColor(ImGuiCol_Text, ToneQuad(Palette.Text));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ToneQuad(Palette.TextDim));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Spectrum(0x090909));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Spectrum(0x141412));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Spectrum(0x191916));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, Spectrum(0x191919));
    ImGui::PushStyleColor(ImGuiCol_Header, Spectrum(0x191919));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Spectrum(0x272923));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, Spectrum(0x2c2e27));
    ImGui::PushStyleColor(ImGuiCol_Button, Spectrum(0x222222));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Spectrum(0x2e2e28));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Spectrum(0x33352c));
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
    // 📝 PanelDockspace owns the Layers/Viewport/Inspector docks (registered in the constructor) - placement,
    // resize and drag-to-rearrange all live there, this function only supplies the header above it and the
    // status strip below it.
    Panels.ConstructInterface(InterfaceScale);

    const auto *Viewport = ImGui::GetMainViewport();
    const float StatusHeight = 24 * InterfaceScale;
    const float Bottom = Viewport->WorkPos.y + Viewport->WorkSize.y - StatusHeight;
    const float Left = Viewport->WorkPos.x, Right = Viewport->WorkPos.x + Viewport->WorkSize.x;
    ImGui::SetNextWindowPos(ImVec2(Left, Bottom));
    ImGui::SetNextWindowSize(ImVec2(Right - Left, StatusHeight));
    ImGui::Begin("Texture status", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    ImGui::TextColored(Spectrum(0x9a9a8f), "%s", Info.c_str());
    ImGui::SameLine(ImGui::GetWindowWidth() - 200 * InterfaceScale);
    ImGui::TextColored(Spectrum(0x716f62), "NATIVE TEXTURE STUDIO");
    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(20);
    ControlKit::ConfigurePalette(SavedPalette);
}
} // namespace Frontier
