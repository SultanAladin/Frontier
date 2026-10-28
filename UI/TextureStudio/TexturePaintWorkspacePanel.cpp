/*============================================================================================================================================
                                                  TEXTUREPAINTWORKSPACEPANEL.CPP
============================================================================================================================================*/
#include "UI/TextureStudio/TexturePaintWorkspacePanel.h"
#include "UI/TextureStudio/PanelChrome.h"
#include "UI/TextureStudio/PatternThumbnailRaster.h" // RasterizeStampDocument - full-fidelity stamp rasters
#include "Engine/ContentInterchange/VectorAuthoringDocument.h" // DecodeVectorAuthoring (.stamp.toml sidecars)
#include "Engine/Interface/ControlKit.h"
#include "Engine/Interface/DropdownControls.h"
#include "Engine/Interface/GlyphSpace.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <thread>

namespace Frontier
{
// Shared panel UI primitives (PanelChrome.h) - one implementation for all five workspaces.
using PanelChrome::Caption;
using PanelChrome::CaptionWidth;
using PanelChrome::Colour32;
using PanelChrome::DrawGlyph;
using PanelChrome::Lowered;
using PanelChrome::Spectrum;
using PanelChrome::SpectrumQuad;
using PanelChrome::ThemeQuad;
namespace
{
std::string Trimmed(const std::string &Text)
{
    size_t Begin = 0, End = Text.size();
    while (Begin < End && std::isspace(uint8_t(Text[Begin]))) ++Begin;
    while (End > Begin && std::isspace(uint8_t(Text[End - 1]))) --End;
    return Text.substr(Begin, End - Begin);
}
std::string JsonEscape(const std::string &Text)
{
    std::string Out;
    for (const char Character : Text)
    {
        if (Character == '"') Out += "\\\"";
        else if (Character == '\\') Out += "\\\\";
        else if (Character == '\n') Out += "\\n";
        else if (uint8_t(Character) < 0x20) { char Buffer[8]; std::snprintf(Buffer, sizeof(Buffer), "\\u%04x", Character); Out += Buffer; }
        else Out += Character;
    }
    return Out;
}
std::string NowTimeText()
{
    const std::time_t Now = std::time(nullptr);
    std::tm Local{};
#ifdef _WIN32
    localtime_s(&Local, &Now);
#else
    localtime_r(&Now, &Local);
#endif
    char Buffer[16];
    std::snprintf(Buffer, sizeof(Buffer), "%02d:%02d:%02d", Local.tm_hour, Local.tm_min, Local.tm_sec);
    return Buffer;
}
std::array<float, 4> ParseHexColour(const std::string &Hex, std::array<float, 4> Fallback) noexcept
{
    if (Hex.size() != 7 || Hex[0] != '#') return Fallback;
    const auto Component = [&](int Index)
    {
        char Byte[3]{Hex[size_t(Index)], Hex[size_t(Index) + 1], 0};
        return float(std::strtol(Byte, nullptr, 16)) / 255.f;
    };
    return {Component(1), Component(3), Component(5), 1.f};
}
std::string HexColour(const std::array<float, 4> &Colour)
{
    char Buffer[16];
    std::snprintf(Buffer, sizeof(Buffer), "#%02x%02x%02x", int(std::clamp(Colour[0], 0.f, 1.f) * 255.f),
        int(std::clamp(Colour[1], 0.f, 1.f) * 255.f), int(std::clamp(Colour[2], 0.f, 1.f) * 255.f));
    return Buffer;
}

//------------------------------------------------------------------------------------------------------------------------
//                                       STATIC TABLES (1:1 WITH THE ALLOY SOURCES)
//------------------------------------------------------------------------------------------------------------------------

struct KindMeta
{
    const char *Id;
    const char *Label;
    const char *Description;
    const char *MenuHint;
    uint32_t Colour;
    const char *Icon;
};
// 📝 textureDocument.js TEXTURE_KINDS + TextureLayerStack.jsx's add-menu hints/icon mapping, in Alloy's menu order.
constexpr std::array<KindMeta, 6> KindTable{{
    {"paint", "Paint", "A sparse digital paint layer with editable UV strokes and per-channel values.", "Digital strokes · Paint / Erase", 0xd79564,
        "M4 20L8 19L20 7L17 4L5 16ZM14 3L21 10"},
    {"fill", "Fill", "Digital channel fills, scoped to real faces, islands or mesh components.", "Digital channel region fills", 0x779cd1,
        "M5 11L12 4L19 11L12 18ZM12 18V21M5 21H19"},
    {"material", "Material", "A native material snapshot covering the whole object or one part.", "Source and material setup", 0xaa98d0,
        "M12.83 2.18a2 2 0 0 0-1.66 0L2.6 6.08a1 1 0 0 0 0 1.83l8.58 3.91a2 2 0 0 0 1.66 0l8.58-3.9a1 1 0 0 0 0-1.83Z"},
    {"generator", "Generator", "A layer for a future procedural source.", "Source and material setup", 0x78ab96,
        "M12 3L13.8 8.2L19 10L13.8 11.8L12 17L10.2 11.8L5 10L10.2 8.2Z"},
    {"decal", "Decal", "A surface-projected stamp layer containing multiple decal components.", "Surface stamps · multiple components", 0xc4aa7a,
        "M5 22H19M12 2V10M8 10H16L17 18H7Z"},
    {"folder", "Folder", "A layer group with its own blend, opacity and optional coverage mask.", "Group layers · folder mask", 0xbfac88,
        "M3 7a2 2 0 0 1 2-2h3.5l1.7 1.7H19a2 2 0 0 1 2 2v7.3a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2Z"},
}};
const KindMeta &MetaForKind(size_t Kind) noexcept
{
    // 📝 KindTable is in menu order; the panel's LayerKind enumerates the same order (see the header).
    return KindTable[std::min(Kind, KindTable.size() - 1)];
}
struct ChannelMeta
{
    const char *Id;
    const char *Label;
    const char *Short;
    const char *Group;
    uint32_t Colour;
    const char *Edit; // color | scalar | derived (paintChannelModel's per-channel editor kind)
    float Min, Max, DefaultValue;
    uint32_t DefaultColour;
    const char *Unit;
};
// 📝 paintChannelModel.js PAINT_CHANNELS (id/label/short/group/colour/edit/range/defaults/unit), verbatim.
constexpr std::array<ChannelMeta, 15> ChannelTable{{
    {"baseColor", "Base color", "C", "Surface", 0xb87333, "color", 0, 1, 0, 0xb87333, ""},
    {"metalness", "Metalness", "M", "Surface", 0x8b5cf6, "scalar", 0, 1, 0, 0, ""},
    {"roughness", "Roughness", "R", "Surface", 0x3b82f6, "scalar", 0, 1, .5f, 0, ""},
    {"height", "Height", "H", "Surface", 0x8a8a8a, "scalar", 0, 1, .5f, 0, ""},
    {"normal", "Normal", "N", "Surface", 0x10b981, "derived", 0, 1, 0, 0, ""},
    {"opacity", "Opacity", "A", "Surface", 0x94a3b8, "scalar", 0, 1, 1, 0, ""},
    {"emission", "Emission", "E", "Radiance", 0xf59e0b, "color", 0, 1, 0, 0x000000, ""},
    {"occlusion", "Ambient occlusion", "AO", "Radiance", 0x6b7280, "scalar", 0, 1, 1, 0, ""},
    {"anisotropy", "Anisotropy", "AN", "Reflectance", 0x22d3ee, "scalar", 0, 1, 0, 0, ""},
    {"anisotropyAngle", "Anisotropy angle", "°", "Reflectance", 0x0ea5e9, "scalar", 0, 360, 0, 0, "°"},
    {"clearcoat", "Clearcoat", "CC", "Reflectance", 0xe2e8f0, "scalar", 0, 1, 0, 0, ""},
    {"transmission", "Transmission", "TR", "Reflectance", 0x7dd3fc, "scalar", 0, 1, 0, 0, ""},
    {"refractionIndex", "Refraction index", "IOR", "Reflectance", 0xa78bfa, "scalar", 1, 3, 1.5f, 0, ""},
    {"sheen", "Sheen", "SH", "Scattering", 0xf472b6, "color", 0, 1, 0, 0x3a3a3a, ""},
    {"subsurface", "Subsurface", "SS", "Scattering", 0xfb7185, "color", 0, 1, 0, 0xd98a72, ""},
}};
struct PartMeta
{
    const char *Name;
    int Tile;
    uint32_t Colour;
};
// 📝 textureSurfaceModel.js TEXTURE_PARTS (order/tile/colour), verbatim.
constexpr std::array<PartMeta, 4> PartTable{{
    {"Body", 1001, 0x9daf98},
    {"Lid", 1002, 0xd5b78c},
    {"Handle", 1003, 0x92aac5},
    {"Spout", 1004, 0xb39dc5},
}};
constexpr std::array<const char *, 6> BlendTable{"Normal", "Multiply", "Screen", "Overlay", "Add", "Darken"};
struct PresetMeta
{
    const char *Id;
    const char *Name;
    const char *Note;
};
// 📝 digitalBrush.js DIGITAL_BRUSH_PRESETS, verbatim.
constexpr std::array<PresetMeta, 5> PresetTable{{
    {"round", "Studio round", "A clean, pressure-sensitive round tip"},
    {"soft", "Soft round", "A smooth digital falloff"},
    {"flat", "Flat tip", "An elliptical tip following the stroke"},
    {"pixel", "Pixel pen", "A hard square digital tip"},
    {"grain", "Grain round", "Procedural digital texture, not physical paint"},
}};
struct GeneratorMeta
{
    const char *Id;
    const char *Label;
    const char *Group;
    const char *Note;
};
// 📝 paintChannelModel.js CHANNEL_GENERATORS, verbatim.
constexpr std::array<GeneratorMeta, 10> GeneratorTable{{
    {"CurvatureEdges", "Curvature", "Mask", "Needs curvature mesh map"},
    {"AmbientOcclusion", "Ambient Occlusion", "Mask", "Needs occlusion mesh map"},
    {"Thickness", "Thickness", "Mask", "Needs thickness mesh map"},
    {"PositionGradient", "Position Gradient", "Mask", "World-axis ramp configuration"},
    {"MetalEdgeWear", "Metal Edge Wear", "Wear", "Needs curvature / grunge inputs"},
    {"DirtAccumulation", "Dirt", "Wear", "Needs occlusion input"},
    {"WaterRunoff", "Water Runoff", "Wear", "Gravity-streak configuration"},
    {"PerlinNoise", "Perlin Noise", "Procedural", "Fractal-noise configuration"},
    {"VoronoiCells", "Voronoi", "Procedural", "Cellular configuration"},
    {"BrushedAnisotropy", "Brushed Metal", "Procedural", "Directional-grain configuration"},
}};
struct GeneratorParamMeta
{
    const char *GeneratorId;
    const char *Key;
    const char *Label;
    float Value;
};
// 📝 paintChannelModel.js CHANNEL_GENERATORS parameters (key/label/default, all 0..1), verbatim.
constexpr std::array<GeneratorParamMeta, 24> GeneratorParamTable{{
    {"CurvatureEdges", "Balance", "Balance", .5f}, {"CurvatureEdges", "Contrast", "Contrast", .7f}, {"CurvatureEdges", "Radius", "Radius", .25f},
    {"AmbientOcclusion", "Spread", "Spread", .4f}, {"AmbientOcclusion", "Contrast", "Contrast", .6f},
    {"Thickness", "Depth", "Depth", .5f}, {"Thickness", "Contrast", "Contrast", .5f},
    {"PositionGradient", "Origin", "Origin", .5f}, {"PositionGradient", "Falloff", "Falloff", .35f},
    {"MetalEdgeWear", "Intensity", "Intensity", .6f}, {"MetalEdgeWear", "Softness", "Softness", .3f}, {"MetalEdgeWear", "Roughness", "Grain", .45f},
    {"DirtAccumulation", "Amount", "Amount", .5f}, {"DirtAccumulation", "Scale", "Scale", .3f},
    {"WaterRunoff", "Length", "Length", .55f}, {"WaterRunoff", "Density", "Density", .4f}, {"WaterRunoff", "Gravity", "Gravity", .8f},
    {"PerlinNoise", "Scale", "Scale", .4f}, {"PerlinNoise", "Octaves", "Octaves", .5f}, {"PerlinNoise", "Contrast", "Contrast", .5f},
    {"VoronoiCells", "Density", "Density", .35f}, {"VoronoiCells", "Jitter", "Jitter", .7f},
    {"BrushedAnisotropy", "Direction", "Angle", 0.f}, {"BrushedAnisotropy", "Grain", "Grain", .6f},
}};
constexpr const char *LayersIconPath =
    "M12.83 2.18a2 2 0 0 0-1.66 0L2.6 6.08a1 1 0 0 0 0 1.83l8.58 3.91a2 2 0 0 0 1.66 0l8.58-3.9a1 1 0 0 0 0-1.83Z "
    "M2 12a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 1.65 0l8.58-3.9A1 1 0 0 0 22 12 "
    "M2 17a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 1.65 0l8.58-3.9A1 1 0 0 0 22 17";
constexpr const char *BoxIconPath =
    "M21 8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16Z"
    "M3.3 7 12 12l8.7-5M12 22V12";
struct Vec3 { float X = 0, Y = 0, Z = 0; };
Vec3 Sub(Vec3 A, Vec3 B) noexcept { return {A.X - B.X, A.Y - B.Y, A.Z - B.Z}; }
Vec3 CrossV(Vec3 A, Vec3 B) noexcept { return {A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X}; }
float DotV(Vec3 A, Vec3 B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
Vec3 NormalizeV(Vec3 V) noexcept
{
    const float Length = std::sqrt(DotV(V, V));
    return Length > 1e-9f ? Vec3{V.X / Length, V.Y / Length, V.Z / Length} : V;
}
bool PointInTriangle(float Px, float Py, const float A[2], const float B[2], const float C[2], float Weights[3]) noexcept
{
    const float Denominator = (B[1] - C[1]) * (A[0] - C[0]) + (C[0] - B[0]) * (A[1] - C[1]);
    if (std::abs(Denominator) < 1e-12f) return false;
    Weights[0] = ((B[1] - C[1]) * (Px - C[0]) + (C[0] - B[0]) * (Py - C[1])) / Denominator;
    Weights[1] = ((C[1] - A[1]) * (Px - C[0]) + (A[0] - C[0]) * (Py - C[1])) / Denominator;
    Weights[2] = 1.f - Weights[0] - Weights[1];
    return Weights[0] >= -1e-5f && Weights[1] >= -1e-5f && Weights[2] >= -1e-5f;
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

TexturePaintWorkspacePanel::TexturePaintWorkspacePanel()
{
    // 📝 Same reusable PanelDockspace + 278/1fr/338 grid (against the shared 1436px reference) as Baking;
    // bottom inset reserves only the 31px .tp-status-bar (the content browser was deleted).
    (void)Panels.ConfigureDockspaceKey("TextureWorkspace TexturePaint panels");
    Panels.ConfigureWorkspaceInsets(43, 31);
    Panels.ConfigureTabExtent(130, 26);
    Panels.Register({"texture-layers", "Layer stack", [&]() { ConstructLayersDock(); }});
    Panels.Register({"texture-viewport", "Viewport Teapot", [&]() { ConstructViewportDock(); }});
    Panels.Register({"texture-inspector", "Inspector", [&]() { ConstructInspectorDock(); }});
    Panels.ConfigureColumns({{"texture-layers", 278.f / 1436}, {"texture-viewport", 820.f / 1436}, {"texture-inspector", 338.f / 1436}});
    ResetDocument();
}

bool TexturePaintWorkspacePanel::InitializeContent() noexcept
{
    try
    {
        Assets = BuildDemoBakeAssets("Teapot");
        BuildTopology();
        return !Topology.empty();
    }
    catch (...) { return false; }
}
void TexturePaintWorkspacePanel::FinalizeContent() noexcept
{
    ControlExtents.clear();
    LastPickTriangles.clear();
}
void TexturePaintWorkspacePanel::IntegrateContent(double DeltaSeconds) noexcept
{
    ClockSeconds += DeltaSeconds;
    // 🖌️ Wetness/solvent drying, drips and dust settle run on wall-clock time, exactly like the HTML fields.
    if (StampCanvasConfigured) StampEngine.StepFields(DeltaSeconds);
    // 📝 Alloy coalesces continuous edits into one history step after 650ms idle (commitTextureHistory's key).
    if (PendingHistory && ClockSeconds - PendingHistoryClock > 0.65)
    {
        CommitHistory(PendingHistory->Label, PendingHistory->Detail, PendingHistory->Kind);
        PendingHistory.reset();
    }
    // 📝 Alloy autosaves a local draft 500ms after each document change; natively that is a real JSON file in
    // the configured project directory, and the status line says so only once the write actually succeeded.
    if (Dirty && !ProjectDirectory.empty() && ClockSeconds - LastEditClock > 0.5)
    {
        if (WriteProjectFile(ProjectDirectory / "texture-paint-draft.texture.json"))
        {
            Dirty = false;
            Status = "Layer draft saved locally";
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     DOCUMENT OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

void TexturePaintWorkspacePanel::ResetDocument()
{
    Layers.clear();
    const auto MakeLayer = [&](const char *Id, const char *Name, LayerKind Kind, std::vector<std::string> Channels)
    {
        LayerState Layer;
        Layer.Id = Id;
        Layer.Name = Name;
        Layer.Kind = Kind;
        Layer.Channels = std::move(Channels);
        return Layer;
    };
    // 🧩 createTextureDocument()'s default stack, topmost first.
    Layers.push_back(MakeLayer("surface-detail", "Surface detail", LayerKind::Paint, {"baseColor", "roughness"}));
    Layers.push_back(MakeLayer("surface-finish", "Surface finish", LayerKind::Fill, {"baseColor", "roughness"}));
    Layers.push_back(MakeLayer("base-material", "Base material", LayerKind::Material,
        {"baseColor", "metalness", "roughness", "height", "normal", "occlusion"}));
    SelectedLayerId = "surface-detail";
    MaskSelected = false;
    Brush = BrushState{};
    ClearStampCanvas();
    HistoryEntries.clear();
    HistoryIndex = -1;
    PendingHistory.reset();
    CommitHistory("Opened project", DocumentName, "project");
}

TexturePaintWorkspacePanel::LayerState *TexturePaintWorkspacePanel::FindLayer(const std::string &Id) noexcept
{
    for (auto &Layer : Layers)
        if (Layer.Id == Id) return &Layer;
    return nullptr;
}
const TexturePaintWorkspacePanel::LayerState *TexturePaintWorkspacePanel::FindLayer(const std::string &Id) const noexcept
{
    for (const auto &Layer : Layers)
        if (Layer.Id == Id) return &Layer;
    return nullptr;
}
std::string TexturePaintWorkspacePanel::QuerySelectedLayerName() const
{
    const auto *Layer = FindLayer(SelectedLayerId);
    return Layer ? Layer->Name : std::string{};
}
int TexturePaintWorkspacePanel::LayerDepth(const LayerState &Layer) const noexcept
{
    int Depth = 0;
    std::string Parent = Layer.ParentId;
    while (!Parent.empty() && Depth < 16)
    {
        const auto *Next = FindLayer(Parent);
        if (!Next) break;
        ++Depth;
        Parent = Next->ParentId;
    }
    return Depth;
}
bool TexturePaintWorkspacePanel::EffectiveLocked(const LayerState &Layer) const noexcept
{
    if (Layer.Locked) return true;
    std::string Parent = Layer.ParentId;
    while (!Parent.empty())
    {
        const auto *Next = FindLayer(Parent);
        if (!Next) return false;
        if (Next->Locked) return true;
        Parent = Next->ParentId;
    }
    return false;
}
bool TexturePaintWorkspacePanel::EffectiveVisible(const LayerState &Layer) const noexcept
{
    if (!Layer.Visible) return false;
    std::string Parent = Layer.ParentId;
    while (!Parent.empty())
    {
        const auto *Next = FindLayer(Parent);
        if (!Next) return true;
        if (!Next->Visible) return false;
        Parent = Next->ParentId;
    }
    return true;
}
bool TexturePaintWorkspacePanel::HiddenByCollapsedFolder(const LayerState &Layer) const noexcept
{
    std::string Parent = Layer.ParentId;
    while (!Parent.empty())
    {
        const auto *Next = FindLayer(Parent);
        if (!Next) return false;
        if (Next->Kind == LayerKind::Folder && !Next->Expanded) return true;
        Parent = Next->ParentId;
    }
    return false;
}
bool TexturePaintWorkspacePanel::IsDescendantOf(const std::string &Id, const std::string &AncestorId) const noexcept
{
    const auto *Layer = FindLayer(Id);
    std::string Parent = Layer ? Layer->ParentId : std::string{};
    while (!Parent.empty())
    {
        if (Parent == AncestorId) return true;
        const auto *Next = FindLayer(Parent);
        if (!Next) return false;
        Parent = Next->ParentId;
    }
    return false;
}
void TexturePaintWorkspacePanel::SelectLayer(const std::string &Id, bool Mask)
{
    const auto *Layer = FindLayer(Id);
    if (!Layer) return;
    DigitalActive = false; // Alloy cancels the armed digital tool on target change
    SelectedLayerId = Id;
    MaskSelected = Mask && Layer->Mask.Present;
    if (MaskSelected && MaskPreview == false && Layer->Mask.Present) { /* preview tab stays user-controlled */ }
    LayerNameSelectionId.clear(); // force the inspector name field to re-sync
}
std::string TexturePaintWorkspacePanel::MakeLayerId(const char *Prefix)
{
    std::string Id;
    do { Id = std::string(Prefix) + "-" + std::to_string(NextLayerSerial++); } while (FindLayer(Id));
    return Id;
}
std::string TexturePaintWorkspacePanel::UniqueLayerName(const std::string &Base) const
{
    const auto Taken = [&](const std::string &Name)
    {
        for (const auto &Layer : Layers)
            if (Layer.Name == Name) return true;
        return false;
    };
    if (!Taken(Base)) return Base;
    for (int Index = 2; Index < 1000; ++Index)
    {
        const std::string Candidate = Base + " " + std::to_string(Index);
        if (!Taken(Candidate)) return Candidate;
    }
    return Base;
}
void TexturePaintWorkspacePanel::AddLayer(LayerKind Kind)
{
    if (Layers.size() >= 64) { Error = "The 64-layer limit has been reached."; return; }
    LayerState Layer;
    Layer.Id = MakeLayerId(MetaForKind(size_t(Kind)).Id);
    Layer.Kind = Kind;
    Layer.Name = UniqueLayerName(Kind == LayerKind::Folder ? "Folder" : std::string(MetaForKind(size_t(Kind)).Label) + " layer");
    Layer.Expanded = Kind == LayerKind::Folder;
    if (Kind != LayerKind::Folder) Layer.Channels = {"baseColor", "roughness"};
    // 📝 insertTextureLayer inserts above the current selection (vector order is topmost-first).
    size_t InsertAt = 0;
    if (const auto *Selected = SelectedLayer())
    {
        Layer.ParentId = Selected->ParentId;
        InsertAt = size_t(Selected - Layers.data());
    }
    const std::string NewId = Layer.Id, NewName = Layer.Name;
    Layers.insert(Layers.begin() + intptr_t(InsertAt), std::move(Layer));
    SelectLayer(NewId);
    CommitHistory(Kind == LayerKind::Folder ? "Add folder" : "Add layer", NewName, Kind == LayerKind::Folder ? "folder" : "layer");
    MarkDirty();
}
void TexturePaintWorkspacePanel::DuplicateSelectedLayer()
{
    const auto *Source = SelectedLayer();
    if (!Source || Layers.size() >= 64) return;
    LayerState Copy = *Source;
    Copy.Id = MakeLayerId(MetaForKind(size_t(Copy.Kind)).Id);
    Copy.Name = UniqueLayerName(Source->Name + " copy");
    const size_t InsertAt = size_t(Source - Layers.data());
    const std::string NewId = Copy.Id, NewName = Copy.Name;
    Layers.insert(Layers.begin() + intptr_t(InsertAt), std::move(Copy));
    SelectLayer(NewId);
    CommitHistory("Duplicate layer / folder", NewName, "layer");
    MarkDirty();
}
void TexturePaintWorkspacePanel::DeleteSelectedLayer()
{
    const auto *Source = SelectedLayer();
    if (!Source || EffectiveLocked(*Source)) return;
    const std::string RemovedId = Source->Id, RemovedName = Source->Name;
    const bool WasFolder = Source->Kind == LayerKind::Folder;
    std::vector<std::string> Removed{RemovedId};
    if (WasFolder)
        for (const auto &Layer : Layers)
            if (IsDescendantOf(Layer.Id, RemovedId)) Removed.push_back(Layer.Id);
    Layers.erase(std::remove_if(Layers.begin(), Layers.end(),
                     [&](const LayerState &Layer) { return std::find(Removed.begin(), Removed.end(), Layer.Id) != Removed.end(); }),
        Layers.end());
    MaskSelected = false;
    SelectedLayerId = Layers.empty() ? std::string{} : Layers.front().Id;
    LayerNameSelectionId.clear();
    CommitHistory(WasFolder ? "Remove / ungroup folder" : "Delete layer", RemovedName, WasFolder ? "folder" : "layer");
    MarkDirty();
}
void TexturePaintWorkspacePanel::MoveSelectedLayer(int Direction)
{
    const auto *Selected = SelectedLayer();
    if (!Selected || EffectiveLocked(*Selected)) return;
    const size_t Index = size_t(Selected - Layers.data());
    // 📝 Swap with the nearest sibling (same parent) in the requested direction.
    for (intptr_t At = intptr_t(Index) + Direction; At >= 0 && At < intptr_t(Layers.size()); At += Direction)
    {
        if (Layers[size_t(At)].ParentId == Selected->ParentId)
        {
            std::swap(Layers[Index], Layers[size_t(At)]);
            CommitHistory("Reorder layers", "Top-to-bottom layer order", "layer");
            MarkDirty();
            return;
        }
        if (!IsDescendantOf(Layers[size_t(At)].Id, Direction < 0 ? Selected->ParentId : Layers[size_t(At)].Id)) { /* keep scanning */ }
    }
}
void TexturePaintWorkspacePanel::SetLayerParent(const std::string &Id, const std::string &ParentId)
{
    auto *Layer = FindLayer(Id);
    if (!Layer || Layer->ParentId == ParentId) return;
    if (!ParentId.empty())
    {
        const auto *Parent = FindLayer(ParentId);
        if (!Parent || Parent->Kind != LayerKind::Folder || ParentId == Id || IsDescendantOf(ParentId, Id)) return;
    }
    LayerState Moving = *Layer;
    Moving.ParentId = ParentId;
    const std::string Name = Moving.Name;
    Layers.erase(Layers.begin() + intptr_t(Layer - Layers.data()));
    size_t InsertAt = 0;
    if (!ParentId.empty())
    {
        // 📝 Land directly under the folder row, ahead of its existing children.
        for (size_t Index = 0; Index < Layers.size(); ++Index)
            if (Layers[Index].Id == ParentId) { InsertAt = Index + 1; break; }
        if (auto *Parent = FindLayer(ParentId)) Parent->Expanded = true;
    }
    Layers.insert(Layers.begin() + intptr_t(InsertAt), std::move(Moving));
    const auto *Parent = FindLayer(ParentId);
    CommitHistory("Move to folder", Name + " -> " + (Parent ? Parent->Name : std::string("Root")), "folder");
    MarkDirty();
}
void TexturePaintWorkspacePanel::UngroupSelectedFolder()
{
    const auto *Folder = SelectedLayer();
    if (!Folder || Folder->Kind != LayerKind::Folder || EffectiveLocked(*Folder)) return;
    const std::string FolderId = Folder->Id, FolderName = Folder->Name, GrandParent = Folder->ParentId;
    for (auto &Layer : Layers)
        if (Layer.ParentId == FolderId) Layer.ParentId = GrandParent;
    Layers.erase(std::remove_if(Layers.begin(), Layers.end(), [&](const LayerState &Layer) { return Layer.Id == FolderId; }), Layers.end());
    SelectedLayerId = Layers.empty() ? std::string{} : Layers.front().Id;
    MaskSelected = false;
    CommitHistory("Remove / ungroup folder", FolderName, "folder");
    MarkDirty();
}
void TexturePaintWorkspacePanel::AddMaskToSelected(bool ColourMask)
{
    auto *Layer = SelectedLayer();
    if (!Layer || Layer->Mask.Present || EffectiveLocked(*Layer)) return;
    Layer->Mask = MaskState{};
    Layer->Mask.Present = true;
    Layer->Mask.Kind = ColourMask ? "color" : "fill";
    const std::string Name = Layer->Name;
    SelectLayer(Layer->Id, true);
    CommitHistory(ColourMask ? "Add colour mask" : "Add layer mask", Name, "mask");
    MarkDirty();
}
void TexturePaintWorkspacePanel::RemoveSelectedMask()
{
    auto *Layer = SelectedLayer();
    if (!Layer || !Layer->Mask.Present || EffectiveLocked(*Layer)) return;
    Layer->Mask = MaskState{};
    MaskSelected = false;
    if (MaskPreview) MaskPreview = false;
    CommitHistory("Remove mask", Layer->Name, "mask");
    MarkDirty();
}
void TexturePaintWorkspacePanel::ToggleChannel(LayerState &Layer, const std::string &ChannelId)
{
    const auto Present = std::find(Layer.Channels.begin(), Layer.Channels.end(), ChannelId);
    if (Present == Layer.Channels.end())
    {
        Layer.Channels.push_back(ChannelId);
        // 📝 paintChannelModel's invariant: Normal is Height-derived, so enabling it enables Height too.
        if (ChannelId == "normal" && std::find(Layer.Channels.begin(), Layer.Channels.end(), "height") == Layer.Channels.end())
            Layer.Channels.push_back("height");
    }
    else Layer.Channels.erase(Present);
    // 📝 Keep document order canonical (Alloy normalizes channels back into PAINT_CHANNELS order).
    std::vector<std::string> Ordered;
    for (const auto &Channel : ChannelTable)
        if (std::find(Layer.Channels.begin(), Layer.Channels.end(), Channel.Id) != Layer.Channels.end()) Ordered.push_back(Channel.Id);
    Layer.Channels = std::move(Ordered);
    CommitHistory("Channel targets", Layer.Name + " · " + std::to_string(Layer.Channels.size()) + " enabled", "channel");
    MarkDirty();
}
void TexturePaintWorkspacePanel::ClearSelectedPaint()
{
    auto *Layer = SelectedLayer();
    if (!Layer || EffectiveLocked(*Layer)) return;
    Layer->Dabs.clear();
    Layer->Fills.clear();
    Layer->StrokeCount = 0;
    CommitHistory("Clear layer digital paint", Layer->Name, "stroke");
    MarkDirty();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     HISTORY
//------------------------------------------------------------------------------------------------------------------------

TexturePaintWorkspacePanel::Snapshot TexturePaintWorkspacePanel::CaptureSnapshot() const
{
    return Snapshot{Layers, SelectedLayerId, MaskSelected, Brush};
}
void TexturePaintWorkspacePanel::RestoreSnapshot(const Snapshot &State)
{
    Layers = State.Layers;
    Brush = State.Brush;
    SelectedLayerId = FindLayer(State.SelectedId) ? State.SelectedId : (Layers.empty() ? std::string{} : Layers.front().Id);
    const auto *Selected = FindLayer(SelectedLayerId);
    MaskSelected = State.MaskSelected && Selected && Selected->Mask.Present;
    DigitalActive = false;
    StrokeActive = false;
    // 📝 The raster stamp canvas is not snapshot state (a 2048x512 canvas per undo step would be absurd), so
    // undo/redo drops it and the preview falls back to the dab truth restored above.
    ClearStampCanvas();
    LayerNameSelectionId.clear();
}
void TexturePaintWorkspacePanel::CommitHistory(const std::string &Label, const std::string &Detail, const std::string &Kind)
{
    HistoryEntry Entry;
    Entry.State = CaptureSnapshot();
    Entry.Label = Label;
    Entry.Detail = Detail;
    Entry.Kind = Kind;
    Entry.TimeText = NowTimeText();
    if (HistoryIndex >= 0 && HistoryIndex + 1 < int(HistoryEntries.size()))
        HistoryEntries.erase(HistoryEntries.begin() + HistoryIndex + 1, HistoryEntries.end());
    HistoryEntries.push_back(std::move(Entry));
    // 📝 TEXTURE_HISTORY_LIMIT: keep at most 80 undo steps behind the present state.
    while (HistoryEntries.size() > 81) HistoryEntries.erase(HistoryEntries.begin());
    HistoryIndex = int(HistoryEntries.size()) - 1;
    HistoryChosen = -1;
}
void TexturePaintWorkspacePanel::QueueHistory(const std::string &Label, const std::string &Detail, const std::string &Kind)
{
    HistoryEntry Pending;
    Pending.Label = Label;
    Pending.Detail = Detail;
    Pending.Kind = Kind;
    PendingHistory = std::move(Pending);
    PendingHistoryClock = ClockSeconds;
    MarkDirty();
}
void TexturePaintWorkspacePanel::Undo()
{
    if (!CanUndo()) return;
    PendingHistory.reset();
    --HistoryIndex;
    RestoreSnapshot(HistoryEntries[size_t(HistoryIndex)].State);
    Status = "Saving layer draft…";
    MarkDirty();
}
void TexturePaintWorkspacePanel::Redo()
{
    if (!CanRedo()) return;
    PendingHistory.reset();
    ++HistoryIndex;
    RestoreSnapshot(HistoryEntries[size_t(HistoryIndex)].State);
    Status = "Saving layer draft…";
    MarkDirty();
}
void TexturePaintWorkspacePanel::RestoreHistoryEntry(int Index)
{
    if (Index < 0 || Index >= int(HistoryEntries.size()) || Index == HistoryIndex) return;
    PendingHistory.reset();
    HistoryIndex = Index;
    RestoreSnapshot(HistoryEntries[size_t(Index)].State);
    Status = "Saving layer draft…";
    MarkDirty();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PROJECT SAVE
//------------------------------------------------------------------------------------------------------------------------

std::string TexturePaintWorkspacePanel::SerializeProject() const
{
    // 🧩 Hand-serialized alloy.texture.v1 subset (this tree vendors no JSON writer): every field written here is
    // real document state. Paint dabs/fills serialize as compact preview records, not Alloy's replayable stroke
    // geometry - the header comment's scope note applies to this file too.
    std::string Out = "{\n  \"schema\": \"alloy.texture.v1\",\n  \"name\": \"" + JsonEscape(DocumentName) + "\",\n  \"scene\": \"teapot\",\n";
    Out += "  \"digitalPainting\": {\"preset\": \"" + JsonEscape(Brush.Preset) + "\", \"tool\": \"" + JsonEscape(Brush.Tool) +
        "\", \"color\": \"" + HexColour(Brush.Colour) + "\", \"size\": " + std::to_string(Brush.Size) + "},\n  \"layers\": [\n";
    for (size_t Index = 0; Index < Layers.size(); ++Index)
    {
        const auto &Layer = Layers[Index];
        Out += "    {\"id\": \"" + JsonEscape(Layer.Id) + "\", \"kind\": \"" + MetaForKind(size_t(Layer.Kind)).Id +
            "\", \"name\": \"" + JsonEscape(Layer.Name) + "\", \"parentId\": " +
            (Layer.ParentId.empty() ? std::string("null") : "\"" + JsonEscape(Layer.ParentId) + "\"") +
            ", \"visible\": " + (Layer.Visible ? "true" : "false") + ", \"locked\": " + (Layer.Locked ? "true" : "false") +
            ", \"opacity\": " + std::to_string(Layer.Opacity) + ", \"blend\": \"" + JsonEscape(Layer.Blend) + "\", \"channels\": [";
        for (size_t Channel = 0; Channel < Layer.Channels.size(); ++Channel)
            Out += (Channel ? ", " : "") + std::string("\"") + Layer.Channels[Channel] + "\"";
        Out += "]";
        if (!Layer.ChannelModes.empty() || !Layer.ChannelValues.empty() || !Layer.ChannelColours.empty() || !Layer.ChannelGenerators.empty())
        {
            // 📝 Sparse channelSettings (only channels the author touched); readers fall back to
            // paintChannelModel's defaults for anything absent, exactly like normalizeChannelSettings.
            Out += ", \"channelSettings\": {";
            bool FirstChannel = true;
            for (const auto &Channel : ChannelTable)
            {
                const auto Mode = Layer.ChannelModes.find(Channel.Id);
                const auto Value = Layer.ChannelValues.find(Channel.Id);
                const auto Colour = Layer.ChannelColours.find(Channel.Id);
                const auto Generator = Layer.ChannelGenerators.find(Channel.Id);
                if (Mode == Layer.ChannelModes.end() && Value == Layer.ChannelValues.end() && Colour == Layer.ChannelColours.end() &&
                    Generator == Layer.ChannelGenerators.end())
                    continue;
                Out += (FirstChannel ? "" : ", ") + std::string("\"") + Channel.Id + "\": {\"mode\": \"" +
                    (Mode != Layer.ChannelModes.end() ? Mode->second : std::string("value")) + "\"";
                FirstChannel = false;
                if (Colour != Layer.ChannelColours.end()) Out += ", \"value\": \"" + HexColour(Colour->second) + "\"";
                else if (Value != Layer.ChannelValues.end()) Out += ", \"value\": " + std::to_string(Value->second);
                if (Generator != Layer.ChannelGenerators.end() && !Generator->second.empty())
                {
                    Out += ", \"generator\": {\"id\": \"" + JsonEscape(Generator->second) + "\", \"parameters\": {";
                    bool FirstParam = true;
                    if (const auto Params = Layer.ChannelGeneratorParams.find(Channel.Id); Params != Layer.ChannelGeneratorParams.end())
                        for (const auto &[Key, ParamValue] : Params->second)
                        {
                            Out += (FirstParam ? "" : ", ") + std::string("\"") + Key + "\": " + std::to_string(ParamValue);
                            FirstParam = false;
                        }
                    Out += "}}";
                }
                Out += "}";
            }
            Out += "}";
        }
        if (!Layer.SourceAsset.empty())
            Out += ", \"sourceAsset\": {\"type\": \"" + JsonEscape(Layer.SourceAssetType) + "\", \"name\": \"" + JsonEscape(Layer.SourceAsset) + "\"}";
        if (Layer.Mask.Present)
            Out += ", \"mask\": {\"kind\": \"" + JsonEscape(Layer.Mask.Kind) + "\", \"enabled\": " + (Layer.Mask.Enabled ? "true" : "false") +
                ", \"inverted\": " + (Layer.Mask.Inverted ? "true" : "false") + ", \"fill\": " + std::to_string(Layer.Mask.Fill) +
                ", \"strength\": " + std::to_string(Layer.Mask.Strength) + ", \"color\": \"" + HexColour(Layer.Mask.Colour) + "\"}";
        Out += ", \"previewDabs\": " + std::to_string(Layer.Dabs.size()) + ", \"previewFills\": " + std::to_string(Layer.Fills.size());
        if (!Layer.Stamps.empty())
        {
            // 🖨️ Stamp placements are small enough to serialize whole (id/uv/size/rotation/tint);
            // like dabs, the current reader doesn't restore preview paint from them yet.
            Out += ", \"stamps\": [";
            for (size_t StampIndex = 0; StampIndex < Layer.Stamps.size(); ++StampIndex)
            {
                const auto &Placement = Layer.Stamps[StampIndex];
                Out += (StampIndex ? ", " : "") + std::string("{\"id\": \"") + JsonEscape(Placement.StampId) +
                    "\", \"u\": " + std::to_string(Placement.U) + ", \"v\": " + std::to_string(Placement.V) +
                    ", \"radius\": " + std::to_string(Placement.Radius) + ", \"rotation\": " + std::to_string(Placement.Rotation) +
                    ", \"tint\": \"" + HexColour(Placement.Tint) + "\"" +
                    ", \"roughness\": " + std::to_string(Placement.Roughness) +
                    ", \"metalness\": " + std::to_string(Placement.Metalness) +
                    ", \"height\": " + std::to_string(Placement.Height);
                if (Placement.Projected)
                {
                    const auto Vec = [](const float Value[3]) {
                        return "[" + std::to_string(Value[0]) + ", " + std::to_string(Value[1]) + ", " + std::to_string(Value[2]) + "]";
                    };
                    Out += ", \"projector\": {\"origin\": " + Vec(Placement.Origin) + ", \"right\": " + Vec(Placement.BaseRight) +
                        ", \"up\": " + Vec(Placement.BaseUp) + ", \"normal\": " + Vec(Placement.Normal) +
                        ", \"worldRadius\": " + std::to_string(Placement.WorldRadius) + "}";
                }
                Out += "}";
            }
            Out += "]";
        }
        Out += "}";
        Out += Index + 1 < Layers.size() ? ",\n" : "\n";
    }
    Out += "  ]\n}\n";
    return Out;
}
bool TexturePaintWorkspacePanel::WriteProjectFile(const std::filesystem::path &Path) const
{
    try
    {
        std::error_code Failure;
        if (!Path.parent_path().empty()) std::filesystem::create_directories(Path.parent_path(), Failure);
        std::ofstream Output(Path, std::ios::binary | std::ios::trunc);
        if (!Output) return false;
        Output << SerializeProject();
        return bool(Output);
    }
    catch (...) { return false; }
}
void TexturePaintWorkspacePanel::SaveProject()
{
    if (ProjectDirectory.empty())
    {
        Error = "No project directory is configured for this session, so the layer project cannot be written.";
        return;
    }
    std::string FileStem = DocumentName;
    for (auto &Character : FileStem)
        if (!std::isalnum(uint8_t(Character)) && Character != '_' && Character != '-') Character = '-';
    if (FileStem.empty()) FileStem = "Surface";
    if (WriteProjectFile(ProjectDirectory / (FileStem + ".texture.json")))
    {
        Dirty = false;
        Status = "Layer draft saved locally";
        ShowToast("Layer project saved · sources included, no composited maps");
    }
    else Error = "The layer project could not be written to the project directory.";
}
void TexturePaintWorkspacePanel::OpenProject()
{
    // 📝 Same honest limitation as Baking's OpenRecipe: no native file picker is wired into this build yet.
    Status = "Opening a layer project needs a native file picker this build does not have wired up yet - Save project still writes the real JSON file.";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     TEAPOT TOPOLOGY + UV SETS
//------------------------------------------------------------------------------------------------------------------------

int TexturePaintWorkspacePanel::PartForPatch(int Patch) noexcept
{
    // 🧩 textureSurfaceModel.js teapotPatchPart: lid 20-27, handle 12-15, spout 16-19, body otherwise.
    if (Patch >= 20 && Patch < 28) return 1;
    if (Patch >= 12 && Patch < 16) return 2;
    if (Patch >= 16 && Patch < 20) return 3;
    return 0;
}
void TexturePaintWorkspacePanel::SourceUvFor(const BakeVertex &Vertex, float Out[2]) const noexcept
{
    // 📝 Invert BakeMesh.cpp's packTeapotUVs (8x4 patch tiling, 0.045 gutter, 1-T / 1-S) back to the original
    // per-patch UV0 - the overlapping "Source UV0" set Alloy shows read-only.
    const int Patch = int(Vertex.ChartId) - 1;
    if (Patch < 0 || Patch >= 32) { Out[0] = Vertex.Uv[0]; Out[1] = Vertex.Uv[1]; return; }
    constexpr float Gutter = 0.045f;
    const float Column = float(Patch % 8), Row = float(Patch / 8);
    const float LocalU = std::clamp((Vertex.Uv[0] * 8.f - Column - Gutter) / (1.f - 2.f * Gutter), 0.f, 1.f);
    const float LocalV = std::clamp((Vertex.Uv[1] * 4.f - Row - Gutter) / (1.f - 2.f * Gutter), 0.f, 1.f);
    Out[0] = 1.f - LocalU; // source.getX == T, and the packer stored (1 - T)
    Out[1] = 1.f - LocalV; // source.getY == S, and the packer stored (1 - S)
}
void TexturePaintWorkspacePanel::AuthoringUvFor(const BakeVertex &Vertex, float Out[2]) const noexcept
{
    // 🧩 createTextureTeapotGeometry's authoring set: each part's patches re-tiled inside its own UDIM tile.
    const int Patch = int(Vertex.ChartId) - 1;
    float Source[2];
    SourceUvFor(Vertex, Source);
    if (Patch < 0 || Patch >= 32) { Out[0] = Source[0]; Out[1] = Source[1]; return; }
    const int Part = PartForPatch(Patch);
    const int Local = Part == 0 ? (Patch < 12 ? Patch : Patch - 16)
        : Part == 1 ? Patch - 20
        : Part == 2 ? Patch - 12
                    : Patch - 16;
    const float Columns = (Part == 0 || Part == 1) ? 4.f : 2.f;
    const float Rows = Part == 0 ? 4.f : 2.f;
    constexpr float Gutter = 0.045f;
    Out[0] = float(Part) + (float(Local % int(Columns)) + Gutter + Source[0] * (1.f - Gutter * 2.f)) / Columns;
    Out[1] = (float(Local / int(Columns)) + Gutter + Source[1] * (1.f - Gutter * 2.f)) / Rows;
}
void TexturePaintWorkspacePanel::BuildTopology()
{
    Topology.clear();
    if (Assets.Low.Parts.empty()) return;
    const auto &Part = Assets.Low.Parts.front();
    Topology.reserve(Part.TriangleCount());
    for (size_t Index = 0; Index + 2 < Part.Indices.size(); Index += 3)
    {
        TopologyTriangle Triangle;
        Triangle.Index = int(Topology.size());
        float Centroid[2]{0, 0};
        for (int Corner = 0; Corner < 3; ++Corner)
        {
            const uint32_t VertexIndex = Part.Indices[Index + size_t(Corner)];
            Triangle.VertexIndex[Corner] = VertexIndex;
            const auto &Vertex = Part.Vertices[VertexIndex];
            AuthoringUvFor(Vertex, Triangle.AuthoringUv[Corner]);
            SourceUvFor(Vertex, Triangle.SourceUv[Corner]);
            Centroid[0] += Triangle.AuthoringUv[Corner][0] / 3.f;
            Centroid[1] += Triangle.AuthoringUv[Corner][1] / 3.f;
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                Triangle.Position[Corner][Axis] = Vertex.Position[Axis];
                Triangle.CentroidWorld[Axis] += Vertex.Position[Axis] / 3.f;
            }
            if (!Corner)
            {
                Triangle.Chart = int(Vertex.ChartId) - 1;
                Triangle.Part = PartForPatch(Triangle.Chart);
            }
        }
        Triangle.CentroidUv[0] = Centroid[0];
        Triangle.CentroidUv[1] = Centroid[1];
        // 🌐 World frame for decal projection: outward face normal (same cross order the 3D draw's
        // front-face test relies on) + a bounding sphere for cheap projector overlap rejection.
        {
            const Vec3 A{Triangle.Position[0][0], Triangle.Position[0][1], Triangle.Position[0][2]};
            const Vec3 B{Triangle.Position[1][0], Triangle.Position[1][1], Triangle.Position[1][2]};
            const Vec3 C{Triangle.Position[2][0], Triangle.Position[2][1], Triangle.Position[2][2]};
            const Vec3 N = NormalizeV(CrossV(Sub(B, A), Sub(C, A)));
            Triangle.Normal[0] = N.X; Triangle.Normal[1] = N.Y; Triangle.Normal[2] = N.Z;
            const Vec3 Centre{Triangle.CentroidWorld[0], Triangle.CentroidWorld[1], Triangle.CentroidWorld[2]};
            Triangle.BoundsRadius = 0;
            for (const Vec3 Corner : {A, B, C})
            {
                const Vec3 D = Sub(Corner, Centre);
                Triangle.BoundsRadius = std::max(Triangle.BoundsRadius, std::sqrt(DotV(D, D)));
            }
        }
        Topology.push_back(Triangle);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PREVIEW COMPOSITE
//------------------------------------------------------------------------------------------------------------------------

namespace
{
void BlendOver(std::array<float, 3> &Base, const std::array<float, 4> &Colour, float Alpha, const std::string &Blend) noexcept
{
    Alpha = std::clamp(Alpha * Colour[3], 0.f, 1.f);
    if (Alpha <= 0.f) return;
    for (int Channel = 0; Channel < 3; ++Channel)
    {
        float Mixed = Colour[size_t(Channel)];
        if (Blend == "Multiply") Mixed = Base[size_t(Channel)] * Colour[size_t(Channel)];
        else if (Blend == "Screen") Mixed = 1.f - (1.f - Base[size_t(Channel)]) * (1.f - Colour[size_t(Channel)]);
        else if (Blend == "Overlay")
            Mixed = Base[size_t(Channel)] < .5f ? 2.f * Base[size_t(Channel)] * Colour[size_t(Channel)]
                                                : 1.f - 2.f * (1.f - Base[size_t(Channel)]) * (1.f - Colour[size_t(Channel)]);
        else if (Blend == "Add") Mixed = std::min(1.f, Base[size_t(Channel)] + Colour[size_t(Channel)]);
        else if (Blend == "Darken") Mixed = std::min(Base[size_t(Channel)], Colour[size_t(Channel)]);
        Base[size_t(Channel)] += (Mixed - Base[size_t(Channel)]) * Alpha;
    }
}
} // namespace

bool TexturePaintWorkspacePanel::FillMatches(const PaintFill &Fill, const TopologyTriangle &Triangle) noexcept
{
    switch (Fill.Scope)
    {
        case RegionScope::Face: return Fill.Triangle == Triangle.Index;
        case RegionScope::Island: return Fill.Chart == Triangle.Chart;
        case RegionScope::Tile: [[fallthrough]];
        case RegionScope::Object: return Fill.Part == Triangle.Part;
    }
    return false;
}
float TexturePaintWorkspacePanel::MaskCoverageAt(const LayerState &Layer, const TopologyTriangle &Triangle, const float Uv[2],
    const std::vector<int32_t> *DabCandidates) const noexcept
{
    const auto &Mask = Layer.Mask;
    if (!Mask.Present || !Mask.Enabled) return 1.f;
    float Value = Mask.Fill;
    if (Mask.Kind == "color")
    {
        // 📝 Colour-selection masks read source RGB per tile in Alloy; the native preview approximates the
        // selection as distance from the part colour to the authored colour (tolerance/softness falloff).
        const uint32_t PartColour = PartTable[size_t(std::clamp(Triangle.Part, 0, 3))].Colour;
        const float Reference[3]{float((PartColour >> 16) & 0xff) / 255.f, float((PartColour >> 8) & 0xff) / 255.f, float(PartColour & 0xff) / 255.f};
        const float Distance = std::sqrt((Reference[0] - Mask.Colour[0]) * (Reference[0] - Mask.Colour[0]) +
            (Reference[1] - Mask.Colour[1]) * (Reference[1] - Mask.Colour[1]) + (Reference[2] - Mask.Colour[2]) * (Reference[2] - Mask.Colour[2]));
        Value = std::clamp(1.f - (Distance - Mask.Tolerance) / std::max(Mask.Softness, 0.01f), 0.f, 1.f);
    }
    else if (Mask.Kind == "gradient")
    {
        Value = std::clamp(Uv[1], 0.f, 1.f); // simple vertical field stand-in for the point-gradient editor
    }
    for (const auto &Fill : Mask.Fills)
        if (FillMatches(Fill, Triangle)) Value += (Fill.Colour[0] - Value) * std::clamp(Fill.Colour[3], 0.f, 1.f);
    const auto ApplyMaskDab = [&](const auto &Dab)
    {
        if (Dab.Part != Triangle.Part) return;
        const float Du = Uv[0] - Dab.U, Dv = Uv[1] - Dab.V;
        const float Distance = std::sqrt(Du * Du + Dv * Dv);
        if (Distance < Dab.Radius)
        {
            const float Falloff = std::clamp(1.f - Distance / Dab.Radius, 0.f, 1.f);
            Value += (Dab.Colour[0] - Value) * Falloff * std::clamp(Dab.Colour[3], 0.f, 1.f);
        }
    };
    if (DabCandidates)
        for (const int32_t Index : *DabCandidates) ApplyMaskDab(Mask.Dabs[size_t(Index)]);
    else
        for (const auto &Dab : Mask.Dabs) ApplyMaskDab(Dab);
    if (Mask.Inverted) Value = 1.f - Value;
    return std::clamp(Value * Mask.Strength / 100.f + (1.f - Mask.Strength / 100.f), 0.f, 1.f);
}

std::array<float, 3> TexturePaintWorkspacePanel::EvaluatePreviewColour(const TopologyTriangle &Triangle) const noexcept
{
    // 🎨 Layer 0: the locked white mesh-base fallback Alloy composites under everything.
    std::array<float, 3> Base{0.92f, 0.91f, 0.88f};
    const float *Uv = Triangle.CentroidUv;
    if (MaskPreview)
    {
        if (const auto *Selected = FindLayer(SelectedLayerId); Selected && Selected->Mask.Present)
        {
            const float Coverage = MaskCoverageAt(*Selected, Triangle, Uv);
            return {Coverage, Coverage, Coverage};
        }
    }
    // 📝 Bottom-to-top (vector order is topmost-first), honoring visibility/opacity/blend/masks - the same
    // evaluation order Alloy's stack uses. This centroid form backs the fallback (untextured) preview;
    // the UV canvas compositor below drives the same per-layer block per texel.
    for (auto Iterator = Layers.rbegin(); Iterator != Layers.rend(); ++Iterator)
        ApplyPreviewLayer(Base, *Iterator, Triangle, Uv, nullptr, nullptr);
    return Base;
}

void TexturePaintWorkspacePanel::ApplyPreviewLayer(std::array<float, 3> &Base, const LayerState &Layer,
    const TopologyTriangle &Triangle, const float Uv[2], const std::vector<int32_t> *DabCandidates,
    const std::vector<int32_t> *MaskDabCandidates, bool VisibilityPrechecked) const noexcept
{
    if (Layer.Kind == LayerKind::Folder || Layer.Opacity <= 0.f) return;
    if (!VisibilityPrechecked && !EffectiveVisible(Layer)) return;
    const float Coverage = MaskCoverageAt(Layer, Triangle, Uv, MaskDabCandidates);
    const float Alpha = Layer.Opacity / 100.f * Coverage;
    if (Alpha <= 0.f) return;
    if (Layer.Kind == LayerKind::Material && Layer.SourceColour[3] > 0.f)
        BlendOver(Base, Layer.SourceColour, Alpha, Layer.Blend);
    for (const auto &Fill : Layer.Fills)
        if (FillMatches(Fill, Triangle)) BlendOver(Base, Fill.Colour, Alpha, Layer.Blend);
    if (StampCanvasConfigured && !StampCanvasLayerId.empty() && Layer.Id == StampCanvasLayerId)
    {
        // 🖌️ This layer owns the PaintWorkshop stamp canvas: its paint is the raster (colour + coverage);
        // the dab records stay the document/undo truth.
        const auto Sample = StampEngine.SampleCanvas(Uv[0], Uv[1]);
        if (Sample[3] > 0.001f)
            BlendOver(Base, {Sample[0], Sample[1], Sample[2], 1.f}, Alpha * Sample[3], Layer.Blend);
    }
    else
    {
        const auto ApplyDab = [&](const auto &Dab)
        {
            if (Dab.Part != Triangle.Part) return;
            const float Du = Uv[0] - Dab.U, Dv = Uv[1] - Dab.V;
            const float Distance = std::sqrt(Du * Du + Dv * Dv);
            if (Distance < Dab.Radius)
                BlendOver(Base, Dab.Colour, Alpha * std::clamp(1.f - Distance / Dab.Radius, 0.f, 1.f), Layer.Blend);
        };
        if (DabCandidates)
            for (const int32_t Index : *DabCandidates) ApplyDab(Layer.Dabs[size_t(Index)]);
        else
            for (const auto &Dab : Layer.Dabs) ApplyDab(Dab);
    }
    // 🖨️ Decals ride over the layer's paint. PROJECTED stamps evaluate in world space: the texel's
    // 3D point goes through the projector frame (in-box test + facing fade), so the art lands only
    // on the surface it was aimed at - no copies on other UV islands, no mirror on the back side,
    // no grazing-angle smear. Legacy placements keep the UV-planar quad. Either way the sample
    // comes from the footprint-matched raster (ResolveStampRasters), placement tint applied.
    bool WorldReady = false;
    Vec3 World{0, 0, 0};
    for (const auto &Placement : Layer.Stamps)
    {
        float SampleU, SampleV, Fade = 1.f;
        if (Placement.Projected)
        {
            if (Placement.WorldRadius <= 0.f) continue;
            if (!WorldReady)
            {
                float Weights[3];
                BarycentricWeights(Triangle, Uv, Weights);
                World = {Weights[0] * Triangle.Position[0][0] + Weights[1] * Triangle.Position[1][0] + Weights[2] * Triangle.Position[2][0],
                    Weights[0] * Triangle.Position[0][1] + Weights[1] * Triangle.Position[1][1] + Weights[2] * Triangle.Position[2][1],
                    Weights[0] * Triangle.Position[0][2] + Weights[1] * Triangle.Position[1][2] + Weights[2] * Triangle.Position[2][2]};
                WorldReady = true;
            }
            const Vec3 D{World.X - Placement.Origin[0], World.Y - Placement.Origin[1], World.Z - Placement.Origin[2]};
            const float R = Placement.WorldRadius;
            if (DotV(D, D) > (R * 1.95f) * (R * 1.95f)) continue; // outside the projector's reach entirely
            const float Depth = DotV(D, Vec3{Placement.Normal[0], Placement.Normal[1], Placement.Normal[2]});
            if (Depth < -R * 1.25f || Depth > R * 0.75f) continue; // projector box: wrap curve behind, limited lift in front
            const float LocalX = DotV(D, Vec3{Placement.AxisRight[0], Placement.AxisRight[1], Placement.AxisRight[2]});
            const float LocalY = DotV(D, Vec3{Placement.AxisUp[0], Placement.AxisUp[1], Placement.AxisUp[2]});
            if (std::fabs(LocalX) > R || std::fabs(LocalY) > R) continue;
            const float Facing = Triangle.Normal[0] * Placement.Normal[0] +
                Triangle.Normal[1] * Placement.Normal[1] + Triangle.Normal[2] * Placement.Normal[2];
            if (Facing < 0.02f) continue; // back-facing or edge-on to the projector: no decal, no smear
            Fade = std::clamp((Facing - 0.02f) / 0.33f, 0.f, 1.f);
            SampleU = LocalX / (2.f * R) + 0.5f;
            SampleV = 0.5f - LocalY / (2.f * R);
        }
        else
        {
            const float Du = Uv[0] - Placement.U, Dv = Uv[1] - Placement.V;
            if (std::fabs(Du) > Placement.Radius * 1.42f || std::fabs(Dv) > Placement.Radius * 1.42f) continue;
            const float CosR = std::cos(Placement.Rotation), SinR = std::sin(Placement.Rotation);
            const float LocalX = Du * CosR + Dv * SinR, LocalY = -Du * SinR + Dv * CosR;
            SampleU = LocalX / (2.f * Placement.Radius) + 0.5f;
            SampleV = 0.5f - LocalY / (2.f * Placement.Radius);
            if (SampleU < 0.f || SampleU > 1.f || SampleV < 0.f || SampleV > 1.f) continue;
        }
        const auto *Entry = ResolvedStampRaster(Placement);
        if (!Entry) continue;
        const auto Texel = StampLibrary::Sample(*Entry, SampleU, SampleV);
        if (Texel[3] <= 0.003f) continue;
        BlendOver(Base,
            {Texel[0] * Placement.Tint[0], Texel[1] * Placement.Tint[1], Texel[2] * Placement.Tint[2], Texel[3] * Placement.Tint[3] * Fade},
            Alpha, Layer.Blend);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     UV PREVIEW CANVAS (PER-TEXEL COMPOSITE)
//------------------------------------------------------------------------------------------------------------------------
// 🎨 The HTML workshop paints into a UV-space texture and the 3D view samples it per pixel; this is
// the native equivalent. The four UDIM parts (authoring U in [0,4], V in [0,1]) become the four 512²
// tiles of one atlas page (tile row 0 = V 1, so the page reads upright in the UV view). An owner map
// (owning Topology triangle per texel) keeps region fills/masks/dab part checks exact at texel
// fidelity, dab spatial grids keep large documents cheap, per-layer content bounds limit each pass
// to the texels the layer can touch, and a content fingerprint gates recomposites to real changes
// (row-parallel across CPU cores, throttled to ~30 Hz while a stroke is live).

namespace
{
// Dab indices bucketed over authoring UV ([0,4]x[0,1] in 1/16 cells); insertion order per cell
// preserves document dab order, so candidate-restricted loops composite identically to full loops.
struct PreviewDabGrid
{
    bool Active = false;
    std::vector<int32_t> Cells[64 * 16];
};

template <typename Records> PreviewDabGrid BuildPreviewDabGrid(const Records &Dabs)
{
    PreviewDabGrid Grid;
    if (Dabs.size() <= 16) return Grid;
    Grid.Active = true;
    const auto CellX = [](float U) { return std::clamp(int(U * 16.f), 0, 63); };
    const auto CellY = [](float V) { return std::clamp(int(V * 16.f), 0, 15); };
    for (int32_t Index = 0; Index < int32_t(Dabs.size()); ++Index)
    {
        const auto &Dab = Dabs[size_t(Index)];
        for (int Y = CellY(Dab.V - Dab.Radius); Y <= CellY(Dab.V + Dab.Radius); ++Y)
            for (int X = CellX(Dab.U - Dab.Radius); X <= CellX(Dab.U + Dab.Radius); ++X)
                Grid.Cells[size_t(Y * 64 + X)].push_back(Index);
    }
    return Grid;
}

const std::vector<int32_t> *PreviewDabCell(const PreviewDabGrid &Grid, float U, float V)
{
    if (!Grid.Active) return nullptr;
    return &Grid.Cells[size_t(std::clamp(int(V * 16.f), 0, 15) * 64 + std::clamp(int(U * 16.f), 0, 63))];
}

// Row-parallel loop for the compositor's texel passes: work-stealing over rows across up to 8
// threads, serial below 32 rows (spawn cost would dominate). RowFn must only touch its own row's
// texels (all compositor passes do), which keeps output byte-identical to the serial order.
template <typename RowFunction> void ParallelCanvasRows(int First, int Last, RowFunction &&RowFn)
{
    const int Rows = Last - First;
    static const unsigned Hardware = std::thread::hardware_concurrency();
    const int Workers = int(std::clamp(Hardware ? Hardware : 1u, 1u, 8u));
    if (Rows < 32 || Workers <= 1)
    {
        for (int Y = First; Y < Last; ++Y) RowFn(Y);
        return;
    }
    std::atomic<int> Next{First};
    const auto Work = [&]
    {
        for (int Y = Next.fetch_add(1, std::memory_order_relaxed); Y < Last; Y = Next.fetch_add(1, std::memory_order_relaxed))
            RowFn(Y);
    };
    std::vector<std::thread> Pool;
    Pool.reserve(size_t(Workers - 1));
    for (int Index = 1; Index < Workers; ++Index) Pool.emplace_back(Work);
    Work();
    for (auto &Thread : Pool) Thread.join();
}
} // namespace

uint64_t TexturePaintWorkspacePanel::QueryPaintCanvasRevision() { return PaintCanvasRevision; }
const std::vector<uint8_t> &TexturePaintWorkspacePanel::QueryPaintCanvasPixels() { return PaintCanvasPixels; }

bool TexturePaintWorkspacePanel::PreviewCanvasTextured() const
{
    return PaintCanvasComposited && QueryPaintCanvasImage && QueryPaintCanvasImage().Texture != 0;
}

std::array<float, 2> TexturePaintWorkspacePanel::PreviewCanvasUv(int Part, float U, float V) const noexcept
{
    const float Tile = float(PaintCanvasTile), Extent = float(PaintCanvasExtent);
    const int P = std::clamp(Part, 0, 3);
    const float OriginX = float((P % 2) * int(PaintCanvasTile + 2) + 1), OriginY = float((P / 2) * int(PaintCanvasTile + 2) + 1);
    return {(OriginX + std::clamp(U - float(P), 0.f, 1.f) * Tile) / Extent,
        (OriginY + (1.f - std::clamp(V, 0.f, 1.f)) * Tile) / Extent};
}

void TexturePaintWorkspacePanel::BuildPreviewCanvasOwners()
{
    const int Extent = int(PaintCanvasExtent), Tile = int(PaintCanvasTile);
    PaintCanvasOwners.assign(size_t(Extent) * Extent, -1);
    for (size_t TriangleIndex = 0; TriangleIndex < Topology.size(); ++TriangleIndex)
    {
        const auto &Triangle = Topology[TriangleIndex];
        const int Part = std::clamp(Triangle.Part, 0, 3);
        const int OriginX = (Part % 2) * (Tile + 2) + 1, OriginY = (Part / 2) * (Tile + 2) + 1;
        float X[3], Y[3];
        for (int Corner = 0; Corner < 3; ++Corner)
        {
            X[Corner] = (Triangle.AuthoringUv[Corner][0] - float(Part)) * float(Tile);
            Y[Corner] = (1.f - Triangle.AuthoringUv[Corner][1]) * float(Tile);
        }
        const float Area = (X[1] - X[0]) * (Y[2] - Y[0]) - (Y[1] - Y[0]) * (X[2] - X[0]);
        if (std::fabs(Area) < 1e-6f) continue;
        const int MinX = std::clamp(int(std::floor(std::min({X[0], X[1], X[2]}))), 0, Tile - 1);
        const int MaxX = std::clamp(int(std::ceil(std::max({X[0], X[1], X[2]}))), 0, Tile - 1);
        const int MinY = std::clamp(int(std::floor(std::min({Y[0], Y[1], Y[2]}))), 0, Tile - 1);
        const int MaxY = std::clamp(int(std::ceil(std::max({Y[0], Y[1], Y[2]}))), 0, Tile - 1);
        for (int Py = MinY; Py <= MaxY; ++Py)
            for (int Px = MinX; Px <= MaxX; ++Px)
            {
                const float Cx = Px + .5f, Cy = Py + .5f;
                const float W0 = (X[1] - Cx) * (Y[2] - Cy) - (Y[1] - Cy) * (X[2] - Cx);
                const float W1 = (X[2] - Cx) * (Y[0] - Cy) - (Y[2] - Cy) * (X[0] - Cx);
                const float W2 = (X[0] - Cx) * (Y[1] - Cy) - (Y[0] - Cy) * (X[1] - Cx);
                const bool Inside = Area > 0.f ? (W0 >= 0.f && W1 >= 0.f && W2 >= 0.f) : (W0 <= 0.f && W1 <= 0.f && W2 <= 0.f);
                if (Inside) PaintCanvasOwners[size_t(OriginY + Py) * Extent + (OriginX + Px)] = int32_t(TriangleIndex);
            }
    }
    PaintCanvasOwnersBuilt = true;
    PaintCanvasOwnersTopology = Topology.size();
}

uint64_t TexturePaintWorkspacePanel::PreviewCanvasFingerprint() const noexcept
{
    uint64_t Hash = 1469598103934665603ull;
    const auto Mix = [&](uint64_t Value) noexcept { Hash ^= Value; Hash *= 1099511628211ull; };
    const auto MixText = [&](const std::string &Text) noexcept
    {
        for (const char Character : Text) Mix(uint8_t(Character));
        Mix(0xffu);
    };
    const auto MixFloat = [&](float Value) noexcept
    {
        uint32_t Bits;
        std::memcpy(&Bits, &Value, 4);
        Mix(Bits);
    };
    Mix(Topology.size());
    Mix(Layers.size());
    Mix(MaskPreview ? 1 : 0);
    MixText(SelectedLayerId);
    Mix(StampCanvasConfigured ? 1 : 0);
    MixText(StampCanvasLayerId);
    Mix(StampLibraryGeneration);
    Mix(StampLib.Entries().size());
    for (const auto &Layer : Layers)
    {
        MixText(Layer.Id);
        MixText(Layer.ParentId);
        Mix(uint32_t(Layer.Kind));
        Mix(Layer.Visible ? 1 : 0);
        MixFloat(Layer.Opacity);
        MixText(Layer.Blend);
        for (const float Channel : Layer.SourceColour) MixFloat(Channel);
        Mix(Layer.Dabs.size());
        Mix(Layer.Fills.size());
        for (const auto &Fill : Layer.Fills)
        {
            Mix(uint32_t(Fill.Scope));
            Mix(uint32_t(Fill.Part + 2));
            Mix(uint32_t(Fill.Chart + 2));
            Mix(uint32_t(Fill.Triangle + 2));
            for (const float Channel : Fill.Colour) MixFloat(Channel);
        }
        Mix(Layer.Stamps.size());
        for (const auto &Placement : Layer.Stamps)
        {
            MixText(Placement.StampId);
            MixFloat(Placement.U);
            MixFloat(Placement.V);
            MixFloat(Placement.Radius);
            MixFloat(Placement.Rotation);
            for (const float Channel : Placement.Tint) MixFloat(Channel);
            Mix(Placement.Projected ? 1 : 0);
            if (Placement.Projected)
            {
                for (const float Value : Placement.Origin) MixFloat(Value);
                for (const float Value : Placement.AxisRight) MixFloat(Value);
                for (const float Value : Placement.AxisUp) MixFloat(Value);
                for (const float Value : Placement.Normal) MixFloat(Value);
                MixFloat(Placement.WorldRadius);
            }
        }
        const auto &Mask = Layer.Mask;
        Mix(Mask.Present ? 1 : 0);
        Mix(Mask.Enabled ? 1 : 0);
        Mix(Mask.Inverted ? 1 : 0);
        MixText(Mask.Kind);
        MixFloat(Mask.Fill);
        MixFloat(Mask.Strength);
        MixFloat(Mask.Tolerance);
        MixFloat(Mask.Softness);
        for (const float Channel : Mask.Colour) MixFloat(Channel);
        Mix(Mask.Fills.size());
        for (const auto &Fill : Mask.Fills)
        {
            Mix(uint32_t(Fill.Scope));
            Mix(uint32_t(Fill.Part + 2));
            Mix(uint32_t(Fill.Chart + 2));
            Mix(uint32_t(Fill.Triangle + 2));
            for (const float Channel : Fill.Colour) MixFloat(Channel);
        }
        Mix(Mask.Dabs.size());
    }
    Mix(HoverStampActive ? 1 : 0);
    if (HoverStampActive)
    {
        MixText(HoverStamp.StampId);
        for (const float Value : HoverStamp.Origin) MixFloat(Value);
        for (const float Value : HoverStamp.AxisRight) MixFloat(Value);
        for (const float Value : HoverStamp.AxisUp) MixFloat(Value);
        for (const float Value : HoverStamp.Normal) MixFloat(Value);
        MixFloat(HoverStamp.WorldRadius);
        for (const float Channel : HoverStamp.Tint) MixFloat(Channel);
    }
    if (StampCanvasConfigured)
    {
        // The physics canvas keeps evolving (drying/flow) without touching the document; probe it on
        // a coarse lattice so the preview recomposites while it visibly settles, then goes quiet.
        for (int Y = 0; Y < 8; ++Y)
            for (int X = 0; X < 32; ++X)
            {
                const auto Sample = StampEngine.SampleCanvas((X + .5f) / 32.f * 4.f, (Y + .5f) / 8.f);
                for (const float Channel : Sample) Mix(uint32_t(std::clamp(Channel, 0.f, 1.f) * 255.f));
            }
    }
    return Hash;
}

void TexturePaintWorkspacePanel::EnsurePreviewCanvas()
{
    if (!QueryPaintCanvasImage || QueryPaintCanvasImage().Texture == 0) return; // no GPU canvas in this host
    if (Topology.empty()) return;
    if (!PaintCanvasOwnersBuilt || PaintCanvasOwnersTopology != Topology.size())
    {
        BuildPreviewCanvasOwners();
        PaintCanvasComposited = false;
    }
    ResolveStampRasters(); // footprint-matched stamp rasters must exist before either preview path samples them
    const uint64_t Fingerprint = PreviewCanvasFingerprint();
    if (PaintCanvasComposited && Fingerprint == PaintCanvasFingerprintCache) return;
    const double Now = ImGui::GetTime();
    if ((StrokeActive || StampDrag.Active || HoverStampActive) && PaintCanvasComposited && Now - PaintCanvasLastComposite < 0.033) return;
    CompositePreviewCanvas();
    PaintCanvasFingerprintCache = Fingerprint;
    PaintCanvasComposited = true;
    PaintCanvasLastComposite = Now;
    ++PaintCanvasRevision;
}

void TexturePaintWorkspacePanel::CompositePreviewCanvas()
{
    const int Extent = int(PaintCanvasExtent), Tile = int(PaintCanvasTile);
    if (PaintCanvasScratch.size() != size_t(Extent) * Extent) PaintCanvasScratch.assign(size_t(Extent) * Extent, {});
    ParallelCanvasRows(0, Extent, [&](int Y)
    {
        for (int X = 0; X < Extent; ++X) PaintCanvasScratch[size_t(Y) * Extent + X] = {0.92f, 0.91f, 0.88f};
    });
    const auto TileOriginX = [&](int Part) { return (Part % 2) * (Tile + 2) + 1; };
    const auto TileOriginY = [&](int Part) { return (Part / 2) * (Tile + 2) + 1; };
    bool MaskPreviewDone = false;
    if (MaskPreview)
    {
        if (const auto *Selected = FindLayer(SelectedLayerId); Selected && Selected->Mask.Present)
        {
            MaskPreviewDone = true;
            const PreviewDabGrid MaskGrid = BuildPreviewDabGrid(Selected->Mask.Dabs);
            for (int Part = 0; Part < 4; ++Part)
            {
                const int Ox = TileOriginX(Part), Oy = TileOriginY(Part);
                ParallelCanvasRows(0, Tile, [&](int Py)
                {
                    for (int Px = 0; Px < Tile; ++Px)
                    {
                        const size_t At = size_t(Oy + Py) * Extent + (Ox + Px);
                        const int32_t Owner = PaintCanvasOwners[At];
                        if (Owner < 0) continue;
                        const float Uv[2]{float(Part) + (Px + .5f) / Tile, 1.f - (Py + .5f) / Tile};
                        const float Coverage = MaskCoverageAt(*Selected, Topology[size_t(Owner)], Uv, PreviewDabCell(MaskGrid, Uv[0], Uv[1]));
                        PaintCanvasScratch[At] = {Coverage, Coverage, Coverage};
                    }
                });
            }
        }
    }
    if (!MaskPreviewDone)
    {
        std::vector<PreviewDabGrid> DabGrids(Layers.size()), MaskGrids(Layers.size());
        for (size_t Index = 0; Index < Layers.size(); ++Index)
        {
            DabGrids[Index] = BuildPreviewDabGrid(Layers[Index].Dabs);
            MaskGrids[Index] = BuildPreviewDabGrid(Layers[Index].Mask.Dabs);
        }
        struct TileBox
        {
            int MinX = 0, MinY = 0, MaxX = -1, MaxY = -1;
        };
        for (int LayerIndex = int(Layers.size()) - 1; LayerIndex >= 0; --LayerIndex)
        {
            const auto &Layer = Layers[size_t(LayerIndex)];
            if (Layer.Kind == LayerKind::Folder || !EffectiveVisible(Layer) || Layer.Opacity <= 0.f) continue;
            TileBox Boxes[4];
            const auto AddTileBox = [&](int Part, float U0, float V0, float U1, float V1)
            {
                const float LocalU0 = std::clamp(U0 - float(Part), 0.f, 1.f), LocalU1 = std::clamp(U1 - float(Part), 0.f, 1.f);
                const float LocalV0 = std::clamp(V0, 0.f, 1.f), LocalV1 = std::clamp(V1, 0.f, 1.f);
                if (LocalU1 < LocalU0 || LocalV1 < LocalV0) return;
                const int X0 = std::clamp(int(std::floor(LocalU0 * Tile)) - 1, 0, Tile - 1);
                const int X1 = std::clamp(int(std::ceil(LocalU1 * Tile)) + 1, 0, Tile - 1);
                const int Y0 = std::clamp(int(std::floor((1.f - LocalV1) * Tile)) - 1, 0, Tile - 1);
                const int Y1 = std::clamp(int(std::ceil((1.f - LocalV0) * Tile)) + 1, 0, Tile - 1);
                TileBox &Box = Boxes[Part];
                if (Box.MaxX < 0) Box = {X0, Y0, X1, Y1};
                else
                {
                    Box.MinX = std::min(Box.MinX, X0);
                    Box.MinY = std::min(Box.MinY, Y0);
                    Box.MaxX = std::max(Box.MaxX, X1);
                    Box.MaxY = std::max(Box.MaxY, Y1);
                }
            };
            const auto AddUvBox = [&](float U0, float V0, float U1, float V1)
            {
                for (int Part = 0; Part < 4; ++Part)
                    if (U1 >= float(Part) && U0 <= float(Part) + 1.f) AddTileBox(Part, U0, V0, U1, V1);
            };
            const bool EngineLayer = StampCanvasConfigured && !StampCanvasLayerId.empty() && Layer.Id == StampCanvasLayerId;
            if (Layer.Kind == LayerKind::Material && Layer.SourceColour[3] > 0.f) AddUvBox(0.f, 0.f, 4.f, 1.f);
            for (const auto &Fill : Layer.Fills)
            {
                int Part = Fill.Part;
                if (Fill.Scope == RegionScope::Face && Fill.Triangle >= 0 && Fill.Triangle < int(Topology.size()))
                    Part = Topology[size_t(Fill.Triangle)].Part;
                else if (Fill.Scope == RegionScope::Island)
                    Part = Fill.Chart >= 0 ? PartForPatch(Fill.Chart) : -1;
                if (Part >= 0 && Part < 4) AddTileBox(Part, float(Part), 0.f, float(Part) + 1.f, 1.f);
                else AddUvBox(0.f, 0.f, 4.f, 1.f);
            }
            if (EngineLayer && Layer.Dabs.empty()) AddUvBox(0.f, 0.f, 4.f, 1.f);
            const float DabPad = EngineLayer ? 2.f : 1.f; // engine deposits spread past the dab record radius
            for (const auto &Dab : Layer.Dabs)
                AddUvBox(Dab.U - Dab.Radius * DabPad, Dab.V - Dab.Radius * DabPad, Dab.U + Dab.Radius * DabPad, Dab.V + Dab.Radius * DabPad);
            for (const auto &Placement : Layer.Stamps)
            {
                if (!Placement.Projected)
                {
                    AddUvBox(Placement.U - Placement.Radius * 1.42f, Placement.V - Placement.Radius * 1.42f,
                        Placement.U + Placement.Radius * 1.42f, Placement.V + Placement.Radius * 1.42f);
                    continue;
                }
                // 🎯 A projector's UV footprint is wherever the surface inside its reach lives in UV:
                // bound it by the UV AABBs of the triangles whose world bounds meet the projector sphere.
                const float Reach = Placement.WorldRadius * 1.95f;
                for (const auto &Candidate : Topology)
                {
                    const float Dx = Candidate.CentroidWorld[0] - Placement.Origin[0];
                    const float Dy = Candidate.CentroidWorld[1] - Placement.Origin[1];
                    const float Dz = Candidate.CentroidWorld[2] - Placement.Origin[2];
                    const float Limit = Reach + Candidate.BoundsRadius;
                    if (Dx * Dx + Dy * Dy + Dz * Dz > Limit * Limit) continue;
                    float MinU = Candidate.AuthoringUv[0][0], MaxU = MinU, MinV = Candidate.AuthoringUv[0][1], MaxV = MinV;
                    for (int Corner = 1; Corner < 3; ++Corner)
                    {
                        MinU = std::min(MinU, Candidate.AuthoringUv[Corner][0]);
                        MaxU = std::max(MaxU, Candidate.AuthoringUv[Corner][0]);
                        MinV = std::min(MinV, Candidate.AuthoringUv[Corner][1]);
                        MaxV = std::max(MaxV, Candidate.AuthoringUv[Corner][1]);
                    }
                    AddUvBox(MinU, MinV, MaxU, MaxV);
                }
            }
            for (int Part = 0; Part < 4; ++Part)
            {
                const TileBox &Box = Boxes[Part];
                if (Box.MaxX < 0) continue;
                const int Ox = TileOriginX(Part), Oy = TileOriginY(Part);
                ParallelCanvasRows(Box.MinY, Box.MaxY + 1, [&](int Py)
                {
                    for (int Px = Box.MinX; Px <= Box.MaxX; ++Px)
                    {
                        const size_t At = size_t(Oy + Py) * Extent + (Ox + Px);
                        const int32_t Owner = PaintCanvasOwners[At];
                        if (Owner < 0) continue;
                        const float Uv[2]{float(Part) + (Px + .5f) / Tile, 1.f - (Py + .5f) / Tile};
                        ApplyPreviewLayer(PaintCanvasScratch[At], Layer, Topology[size_t(Owner)], Uv,
                            PreviewDabCell(DabGrids[size_t(LayerIndex)], Uv[0], Uv[1]),
                            PreviewDabCell(MaskGrids[size_t(LayerIndex)], Uv[0], Uv[1]), true);
                    }
                });
            }
        }
    }
    if (HoverStampActive && !MaskPreviewDone)
    {
        // 👻 The hover ghost composites on top of the stack through the very same per-texel stamp
        // evaluation (projector included), over the same topology-scanned bounds - what you see
        // trailing the cursor is pixel-for-pixel what a click will drop.
        LayerState Ghost;
        Ghost.Visible = true;
        Ghost.Opacity = 100.f;
        Ghost.Blend = "Normal";
        Ghost.Stamps.push_back(HoverStamp);
        struct GhostBox { int MinX = 0, MinY = 0, MaxX = -1, MaxY = -1; };
        GhostBox Boxes[4];
        const float Reach = HoverStamp.WorldRadius * 1.95f;
        for (const auto &Candidate : Topology)
        {
            const float Dx = Candidate.CentroidWorld[0] - HoverStamp.Origin[0];
            const float Dy = Candidate.CentroidWorld[1] - HoverStamp.Origin[1];
            const float Dz = Candidate.CentroidWorld[2] - HoverStamp.Origin[2];
            const float Limit = Reach + Candidate.BoundsRadius;
            if (Dx * Dx + Dy * Dy + Dz * Dz > Limit * Limit) continue;
            const int Part = std::clamp(Candidate.Part, 0, 3);
            float MinU = Candidate.AuthoringUv[0][0], MaxU = MinU, MinV = Candidate.AuthoringUv[0][1], MaxV = MinV;
            for (int Corner = 1; Corner < 3; ++Corner)
            {
                MinU = std::min(MinU, Candidate.AuthoringUv[Corner][0]);
                MaxU = std::max(MaxU, Candidate.AuthoringUv[Corner][0]);
                MinV = std::min(MinV, Candidate.AuthoringUv[Corner][1]);
                MaxV = std::max(MaxV, Candidate.AuthoringUv[Corner][1]);
            }
            const int X0 = std::clamp(int(std::floor((MinU - float(Part)) * Tile)) - 1, 0, Tile - 1);
            const int X1 = std::clamp(int(std::ceil((MaxU - float(Part)) * Tile)) + 1, 0, Tile - 1);
            const int Y0 = std::clamp(int(std::floor((1.f - MaxV) * Tile)) - 1, 0, Tile - 1);
            const int Y1 = std::clamp(int(std::ceil((1.f - MinV) * Tile)) + 1, 0, Tile - 1);
            GhostBox &Box = Boxes[Part];
            if (Box.MaxX < 0) Box = {X0, Y0, X1, Y1};
            else
            {
                Box.MinX = std::min(Box.MinX, X0); Box.MinY = std::min(Box.MinY, Y0);
                Box.MaxX = std::max(Box.MaxX, X1); Box.MaxY = std::max(Box.MaxY, Y1);
            }
        }
        for (int Part = 0; Part < 4; ++Part)
        {
            const GhostBox &Box = Boxes[Part];
            if (Box.MaxX < 0) continue;
            const int Ox = TileOriginX(Part), Oy = TileOriginY(Part);
            ParallelCanvasRows(Box.MinY, Box.MaxY + 1, [&](int Py)
            {
                for (int Px = Box.MinX; Px <= Box.MaxX; ++Px)
                {
                    const size_t At = size_t(Oy + Py) * Extent + (Ox + Px);
                    const int32_t Owner = PaintCanvasOwners[At];
                    if (Owner < 0) continue;
                    const float Uv[2]{float(Part) + (Px + .5f) / Tile, 1.f - (Py + .5f) / Tile};
                    ApplyPreviewLayer(PaintCanvasScratch[At], Ghost, Topology[size_t(Owner)], Uv, nullptr, nullptr, true);
                }
            });
        }
    }
    // Dilation: bleed island-edge colours a couple of texels into the chart gutters so linear
    // filtering across seams does not drag the unpainted base onto the model.
    if (PaintCanvasFilled.size() != size_t(Extent) * Extent) PaintCanvasFilled.assign(size_t(Extent) * Extent, 0);
    for (size_t At = 0; At < PaintCanvasOwners.size(); ++At) PaintCanvasFilled[At] = PaintCanvasOwners[At] >= 0 ? 1 : 0;
    for (int Pass = 0; Pass < 2; ++Pass)
    {
        std::vector<uint8_t> Next = PaintCanvasFilled;
        for (int Part = 0; Part < 4; ++Part)
        {
            const int Ox = TileOriginX(Part), Oy = TileOriginY(Part);
            ParallelCanvasRows(0, Tile, [&](int Py)
            {
                for (int Px = 0; Px < Tile; ++Px)
                {
                    const size_t At = size_t(Oy + Py) * Extent + (Ox + Px);
                    if (PaintCanvasFilled[At]) continue;
                    const int Dx[4]{1, -1, 0, 0}, Dy[4]{0, 0, 1, -1};
                    for (int Direction = 0; Direction < 4; ++Direction)
                    {
                        const int Nx = Px + Dx[Direction], Ny = Py + Dy[Direction];
                        if (Nx < 0 || Ny < 0 || Nx >= Tile || Ny >= Tile) continue;
                        const size_t Near = size_t(Oy + Ny) * Extent + (Ox + Nx);
                        if (PaintCanvasFilled[Near])
                        {
                            PaintCanvasScratch[At] = PaintCanvasScratch[Near];
                            Next[At] = 1;
                            break;
                        }
                    }
                }
            });
        }
        PaintCanvasFilled = std::move(Next);
    }
    // Gutter replication: tile edge texels copied 1px outward so atlas tile borders filter cleanly.
    for (int Part = 0; Part < 4; ++Part)
    {
        const int Ox = TileOriginX(Part), Oy = TileOriginY(Part);
        for (int Px = 0; Px < Tile; ++Px)
        {
            PaintCanvasScratch[size_t(Oy - 1) * Extent + (Ox + Px)] = PaintCanvasScratch[size_t(Oy) * Extent + (Ox + Px)];
            PaintCanvasScratch[size_t(Oy + Tile) * Extent + (Ox + Px)] = PaintCanvasScratch[size_t(Oy + Tile - 1) * Extent + (Ox + Px)];
        }
        for (int Py = -1; Py <= Tile; ++Py)
        {
            PaintCanvasScratch[size_t(Oy + Py) * Extent + (Ox - 1)] = PaintCanvasScratch[size_t(Oy + Py) * Extent + Ox];
            PaintCanvasScratch[size_t(Oy + Py) * Extent + (Ox + Tile)] = PaintCanvasScratch[size_t(Oy + Py) * Extent + (Ox + Tile - 1)];
        }
    }
    PaintCanvasPixels.assign(size_t(Extent) * Extent * 4, 255);
    ParallelCanvasRows(0, Extent, [&](int Y)
    {
        for (int X = 0; X < Extent; ++X)
        {
            const size_t At = size_t(Y) * Extent + X;
            const auto &Texel = PaintCanvasScratch[At];
            PaintCanvasPixels[At * 4 + 0] = uint8_t(std::clamp(Texel[0], 0.f, 1.f) * 255.f);
            PaintCanvasPixels[At * 4 + 1] = uint8_t(std::clamp(Texel[1], 0.f, 1.f) * 255.f);
            PaintCanvasPixels[At * 4 + 2] = uint8_t(std::clamp(Texel[2], 0.f, 1.f) * 255.f);
        }
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PAINT INPUT
//------------------------------------------------------------------------------------------------------------------------

bool TexturePaintWorkspacePanel::DigitalPaintAvailable() const noexcept
{
    // 🧩 useDigitalPainting.js's `available`, verbatim conditions.
    const auto *Layer = FindLayer(SelectedLayerId);
    if (!Layer) return false;
    const bool Mask = MaskSelected && Layer->Mask.Present;
    if (!Mask && Layer->Kind == LayerKind::Folder) return false;
    if (EffectiveLocked(*Layer) || !EffectiveVisible(*Layer)) return false;
    if (View == ViewMode::Wireframe) return false;
    if (View == ViewMode::Uv && UvSource) return false;
    if (!Mask)
    {
        bool AnyChannel = false;
        for (const auto &Channel : Layer->Channels)
            if (Channel != "normal") AnyChannel = true;
        if (!AnyChannel) return false;
    }
    return true;
}

void TexturePaintWorkspacePanel::ApplyPaintAt(const float Uv[2], int Part, int Chart, int Triangle, float RadiusUv)
{
    auto *Layer = FindLayer(StrokeTargetLayerId);
    if (!Layer) return;
    const bool Mask = StrokeTargetMask && Layer->Mask.Present;
    auto &Dabs = Mask ? Layer->Mask.Dabs : Layer->Dabs;
    auto &Fills = Mask ? Layer->Mask.Fills : Layer->Fills;
    const float Strength = Mask && !DigitalActive ? MaskEditor.Opacity / 100.f : Brush.Opacity * Brush.Flow;
    if (Brush.Tool == "fill" && DigitalActive)
    {
        if (LastDabValid) return; // one fill per click, not one per frame the button stays down
        LastDabValid = true;
        PaintFill Fill;
        Fill.Scope = Brush.FillScope;
        Fill.Part = Part;
        Fill.Chart = Chart;
        Fill.Triangle = Triangle;
        Fill.Colour = Mask ? std::array<float, 4>{MaskEditor.InkWhite ? 1.f : 0.f, MaskEditor.InkWhite ? 1.f : 0.f, MaskEditor.InkWhite ? 1.f : 0.f, Strength}
                           : std::array<float, 4>{Brush.Colour[0], Brush.Colour[1], Brush.Colour[2], Strength};
        if (Fills.size() < 512) { Fills.push_back(Fill); StrokeChanged = true; }
        return;
    }
    const bool Erasing = DigitalActive && Brush.Tool == "erase";
    // 🖌️ Physical PaintWorkshop stamps run on their own interpolation, so feed the engine before the dab
    // spacing early-return below can skip a slow drag. Mask painting stays dab-only (coverage, not pigment).
    if (DigitalActive && !Mask) FeedStampEngine(*Layer, Uv, Triangle, RadiusUv, Erasing);
    if (Erasing)
    {
        const size_t Before = Dabs.size();
        Dabs.erase(std::remove_if(Dabs.begin(), Dabs.end(),
                       [&](const PaintDab &Dab)
                       {
                           const float Du = Uv[0] - Dab.U, Dv = Uv[1] - Dab.V;
                           return Dab.Part == Part && (Du * Du + Dv * Dv) < RadiusUv * RadiusUv;
                       }),
            Dabs.end());
        if (Dabs.size() != Before) StrokeChanged = true;
        return;
    }
    if (LastDabValid)
    {
        const float Du = Uv[0] - LastDabUv[0], Dv = Uv[1] - LastDabUv[1];
        const float Spacing = std::max(RadiusUv * Brush.Spacing * 2.f, 0.002f);
        if ((Du * Du + Dv * Dv) < Spacing * Spacing) return;
    }
    if (Dabs.size() >= 2048)
    {
        Status = "Preview paint on this layer reached its 2048-dab session limit; clear paint or use Fill.";
        return;
    }
    PaintDab Dab;
    Dab.U = Uv[0];
    Dab.V = Uv[1];
    Dab.Radius = std::clamp(RadiusUv, 0.004f, 0.4f);
    Dab.Part = Part;
    Dab.Colour = Mask ? std::array<float, 4>{MaskEditor.InkWhite ? 1.f : 0.f, MaskEditor.InkWhite ? 1.f : 0.f, MaskEditor.InkWhite ? 1.f : 0.f, Strength}
                      : std::array<float, 4>{Brush.Colour[0], Brush.Colour[1], Brush.Colour[2], Strength};
    Dabs.push_back(Dab);
    LastDabUv[0] = Uv[0];
    LastDabUv[1] = Uv[1];
    LastDabValid = true;
    StrokeChanged = true;
}
void TexturePaintWorkspacePanel::FinishStroke()
{
    if (!StrokeActive) return;
    StrokeActive = false;
    LastDabValid = false;
    if (StampStrokeActive)
    {
        StampEngine.EndStroke(); // splat bursts fire here, on release
        StampStrokeActive = false;
        if (StampEngine.HasPaint()) StrokeChanged = true;
    }
    if (!StrokeChanged) return;
    StrokeChanged = false;
    if (auto *Layer = FindLayer(StrokeTargetLayerId))
    {
        ++Layer->StrokeCount;
        const bool Mask = StrokeTargetMask;
        const char *Operation = DigitalActive && Brush.Tool == "erase" ? "Erase" : DigitalActive && Brush.Tool == "fill" ? "Fill" : "Paint";
        CommitHistory(std::string(Operation) + (Mask ? " mask" : " channels"),
            Layer->Name + (Mask ? " · Mask coverage · " : " · ") + BrushPresetName(), "stroke");
    }
    MarkDirty();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PAINTWORKSHOP STAMP ENGINE DRIVER
//------------------------------------------------------------------------------------------------------------------------

void TexturePaintWorkspacePanel::EnsureStampEngine()
{
    if (StampCanvasConfigured) return;
    // 🗺️ One canvas over the whole UDIM strip: authoring U spans four part tiles, V the packed 0..1 range.
    StampEngine.Configure(2048, 512, 512, 128, 4.f, 1.f, {235.f, 232.f, 224.f}, false);
    StampCanvasConfigured = true;
}

bool TexturePaintWorkspacePanel::StampToolSelected() const noexcept
{
    return Brush.PhysicalTool != "Digital";
}

void TexturePaintWorkspacePanel::ClearStampCanvas() noexcept
{
    if (StampCanvasConfigured) StampEngine.Clear();
    StampCanvasLayerId.clear();
    StampStrokeActive = false;
}

void TexturePaintWorkspacePanel::SyncStampParams(float WorldRadius) noexcept
{
    auto &Params = StampEngine.Parameters();
    const std::array<float, 3> Rgb{Brush.Colour[0] * 255.f, Brush.Colour[1] * 255.f, Brush.Colour[2] * 255.f};
    const bool ColourChanged = Params.Colour != Rgb;
    Params.Colour = Params.DryColour = Params.DigitalColour = Params.SprayColour = Rgb;
    Params.MarkerColour = Params.SplatColour = Params.SootColour = Rgb;
    Params.DustColour = {Rgb[0] * 0.55f + 74.f, Rgb[1] * 0.55f + 63.f, Rgb[2] * 0.55f + 47.f}; // dust reads as settled film of the hue
    if (ColourChanged) StampEngine.Dip();
    Params.Alpha = std::clamp(Brush.Opacity * Brush.Flow, 0.05f, 1.f);
    Params.DigitalOpacity = Params.Alpha;
    Params.DigitalHardness = Brush.Hardness;
    const float R = std::max(0.002f, WorldRadius);
    Params.Radius = R;
    Params.DigitalSize = R;
    Params.DrySize = R * 1.1f;
    Params.PencilSize = R * 0.55f;
    Params.MarkerSize = R * 0.7f;
    Params.SandSize = R * 1.2f;
    Params.DustSize = R * 1.2f;
    Params.EraserSize = R;
    Params.EraserStrength = std::clamp(Brush.Opacity, 0.1f, 1.f);
    Params.EraserSoftness = 1.f - Brush.Hardness;
}

bool TexturePaintWorkspacePanel::BuildStampHit(const float Uv[2], int Triangle, float RadiusUv, StampHit &Out, float &WorldRadius) const noexcept
{
    // 🧭 Reconstruct the 3D stamp frame (position/normal/UV Jacobian) the HTML got from its raycast, from the
    // picked topology triangle: barycentric position+normal at the authoring UV, dP/dUV from the triangle.
    if (Triangle < 0 || Triangle >= int(Topology.size()) || Assets.Low.Parts.empty()) return false;
    const auto &Tri = Topology[size_t(Triangle)];
    const auto &Mesh = Assets.Low.Parts.front();
    float Pos[3][3], Nrm[3][3];
    for (int Corner = 0; Corner < 3; ++Corner)
    {
        const auto &Vertex = Mesh.Vertices[Tri.VertexIndex[Corner]];
        for (int Axis = 0; Axis < 3; ++Axis) { Pos[Corner][Axis] = Vertex.Position[Axis]; Nrm[Corner][Axis] = Vertex.Normal[Axis]; }
    }
    const float U1 = Tri.AuthoringUv[1][0] - Tri.AuthoringUv[0][0], V1 = Tri.AuthoringUv[1][1] - Tri.AuthoringUv[0][1];
    const float U2 = Tri.AuthoringUv[2][0] - Tri.AuthoringUv[0][0], V2 = Tri.AuthoringUv[2][1] - Tri.AuthoringUv[0][1];
    const float Det = U1 * V2 - V1 * U2;
    if (std::fabs(Det) < 1e-12f) return false;
    const float Du = Uv[0] - Tri.AuthoringUv[0][0], Dv = Uv[1] - Tri.AuthoringUv[0][1];
    const float W1 = (Du * V2 - Dv * U2) / Det, W2 = (U1 * Dv - V1 * Du) / Det;
    const auto Sub = [](const float A[3], const float B[3], float O[3]) noexcept { O[0] = A[0] - B[0]; O[1] = A[1] - B[1]; O[2] = A[2] - B[2]; };
    const auto Dot = [](const float A[3], const float B[3]) noexcept { return A[0] * B[0] + A[1] * B[1] + A[2] * B[2]; };
    float E1[3], E2[3];
    Sub(Pos[1], Pos[0], E1);
    Sub(Pos[2], Pos[0], E2);
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        Out.Point[Axis] = Pos[0][Axis] + E1[Axis] * W1 + E2[Axis] * W2;
        Out.Normal[Axis] = Nrm[0][Axis] + (Nrm[1][Axis] - Nrm[0][Axis]) * W1 + (Nrm[2][Axis] - Nrm[0][Axis]) * W2;
    }
    float NormalLength = std::sqrt(Dot(Out.Normal, Out.Normal));
    if (NormalLength < 1e-6f)
    {   // degenerate vertex normals: fall back to the face normal
        Out.Normal[0] = E1[1] * E2[2] - E1[2] * E2[1];
        Out.Normal[1] = E1[2] * E2[0] - E1[0] * E2[2];
        Out.Normal[2] = E1[0] * E2[1] - E1[1] * E2[0];
        NormalLength = std::sqrt(Dot(Out.Normal, Out.Normal));
        if (NormalLength < 1e-9f) return false;
    }
    for (float &Component : Out.Normal) Component /= NormalLength;
    float DPdU[3], DPdV[3];
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        DPdU[Axis] = (E1[Axis] * V2 - E2[Axis] * V1) / Det;
        DPdV[Axis] = (E2[Axis] * U1 - E1[Axis] * U2) / Det;
    }
    const float LenU = std::sqrt(Dot(DPdU, DPdU)), LenV = std::sqrt(Dot(DPdV, DPdV));
    if (LenU < 1e-9f || LenV < 1e-9f) return false;
    for (int Axis = 0; Axis < 3; ++Axis) Out.AxisX[Axis] = DPdU[Axis] / LenU;
    const float Slide = Dot(DPdV, Out.AxisX);
    for (int Axis = 0; Axis < 3; ++Axis) Out.AxisY[Axis] = DPdV[Axis] - Out.AxisX[Axis] * Slide;
    const float LenY = std::sqrt(Dot(Out.AxisY, Out.AxisY));
    if (LenY < 1e-9f) return false;
    for (float &Component : Out.AxisY) Component /= LenY;
    // UV-per-world-unit along each axis: invert the Gram matrix of [dP/dU dP/dV].
    const float G00 = Dot(DPdU, DPdU), G01 = Dot(DPdU, DPdV), G11 = Dot(DPdV, DPdV);
    const float GDet = G00 * G11 - G01 * G01;
    if (std::fabs(GDet) < 1e-12f) return false;
    const auto AxisGradient = [&](const float Axis[3], float Gradient[2]) noexcept {
        const float B0 = Dot(Axis, DPdU), B1 = Dot(Axis, DPdV);
        Gradient[0] = (G11 * B0 - G01 * B1) / GDet;
        Gradient[1] = (G00 * B1 - G01 * B0) / GDet;
    };
    AxisGradient(Out.AxisX, Out.DUvDx);
    AxisGradient(Out.AxisY, Out.DUvDy);
    // 👁️ Spray obliquity needs a view direction: the orbit camera sits at Yaw/Pitch on a fixed radius.
    const float Eye[3]{std::sin(Camera.Yaw) * std::cos(Camera.Pitch) * 4.5f, std::sin(Camera.Pitch) * 4.5f,
        std::cos(Camera.Yaw) * std::cos(Camera.Pitch) * 4.5f};
    float View[3]{Eye[0] - Out.Point[0], Eye[1] - Out.Point[1], Eye[2] - Out.Point[2]};
    const float ViewLength = std::sqrt(Dot(View, View));
    if (ViewLength > 1e-6f)
        for (float &Component : View) Component /= ViewLength;
    else
        for (int Axis = 0; Axis < 3; ++Axis) View[Axis] = Out.Normal[Axis];
    for (int Axis = 0; Axis < 3; ++Axis) Out.ViewDir[Axis] = View[Axis];
    Out.U = Uv[0];
    Out.V = Uv[1];
    Out.Pressure = 0.f; // no stylus pressure in the panel yet; engine uses its mouse default
    WorldRadius = RadiusUv * 0.5f * (LenU + LenV);
    return true;
}

void TexturePaintWorkspacePanel::FeedStampEngine(const LayerState &Layer, const float Uv[2], int Triangle, float RadiusUv, bool Erasing)
{
    const bool OwnsCanvas = StampCanvasConfigured && !StampCanvasLayerId.empty() && Layer.Id == StampCanvasLayerId;
    // Physical tools always engage; on the canvas-owning layer even Digital paint/erase must go through the
    // canvas or the preview (which samples the canvas there) would ignore the stroke entirely.
    if (!OwnsCanvas && !StampToolSelected()) return;
    if (Erasing && !OwnsCanvas && Brush.PhysicalTool != "Eraser") return;
    EnsureStampEngine();
    if (!Erasing && Layer.Id != StampCanvasLayerId)
    {
        StampEngine.Clear(); // first physical paint moves the single canvas to this layer
        StampCanvasLayerId = Layer.Id;
    }
    StampHit Hit;
    float WorldRadius = 0.f;
    if (!BuildStampHit(Uv, Triangle, RadiusUv, Hit, WorldRadius)) return;
    SyncStampParams(WorldRadius);
    if (!StampStrokeActive)
    {
        StampTool Selected = StampTool::Digital;
        const std::string &Name = Brush.PhysicalTool;
        if (Erasing || Name == "Eraser") Selected = StampTool::Eraser;
        else if (Name == "PaintBrush") Selected = StampTool::PaintBrush;
        else if (Name == "Spray") Selected = StampTool::Spray;
        else if (Name == "Dry") Selected = StampTool::Dry;
        else if (Name == "Pencil") Selected = StampTool::Pencil;
        else if (Name == "Marker") Selected = StampTool::Marker;
        else if (Name == "Sand") Selected = StampTool::Sand;
        else if (Name == "Splat") Selected = StampTool::Splat;
        else if (Name == "Dust") Selected = StampTool::Dust;
        else if (Name == "Soot") Selected = StampTool::Soot;
        StampEngine.BeginStroke(Selected);
        StampStrokeActive = true;
        StrokeChanged = true; // a stamp stroke (incl. a click-aim splat) always earns its history step
    }
    StampEngine.Stamp(Hit, ClockSeconds);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     STAMP LIBRARY + DECAL PLACEMENT
//------------------------------------------------------------------------------------------------------------------------

void TexturePaintWorkspacePanel::EnsureStampLibrary(bool ForceReload)
{
    if (StampLibraryScanned && !ForceReload) return;
    StampLibraryScanned = true;
    std::filesystem::path Directory = StampLibraryDirectory;
    if (Directory.empty() && !ProjectDirectory.empty()) Directory = ProjectDirectory / "Stamps";
    StampLib.Reload(Directory);
    StampThumbCache.clear(); // reloaded pixels invalidate the picker thumbnails too
    StampRasterCache.clear(); // ...and every resolved full-fidelity stamp raster
    StampRasterFallbacks.clear();
    ++StampLibraryGeneration; // reloaded pixels can change placed stamps - recomposite the UV canvas
    if (!StampLib.Find(ActiveStampId))
        ActiveStampId = StampLib.Entries().empty() ? std::string{} : StampLib.Entries().front().Id;
}

const StampLibraryEntry *TexturePaintWorkspacePanel::ActiveStamp() const noexcept
{
    return StampLib.Find(ActiveStampId);
}

void TexturePaintWorkspacePanel::PlaceStampAt(const float Uv[2])
{
    EnsureStampLibrary();
    const auto *Entry = ActiveStamp();
    if (!Entry)
    {
        Status = "No stamp picked - right-click the viewport for the stamp library";
        return;
    }
    if (View == ViewMode::Wireframe)
    {
        Status = "Stamps can't be placed in Wireframe view";
        return;
    }
    if (Layers.size() >= 64)
    {
        Status = "The 64-layer limit has been reached - delete or merge layers to add another stamp";
        return;
    }
    // 🖨️ Every stamp lands on its OWN stamp (Decal) layer, named after the stamp and selected at
    // once, so it stays an independent, draggable object in the stack (the HTML workshop's model)
    // instead of being baked into whichever paint layer happened to be selected.
    StampPlacement Placement;
    Placement.StampId = Entry->Id;
    Placement.U = Uv[0];
    Placement.V = Uv[1];
    Placement.Radius = std::clamp(0.09f * Brush.Size / 24.f, 0.015f, 0.3f); // brush size drives decal size
    Placement.Rotation = 0.f;
    const float Strength = std::clamp(Brush.Opacity * Brush.Flow, 0.05f, 1.f);
    // White preset art takes the brush colour; authored stamps keep their own colours.
    Placement.Tint = Entry->Tintable ? std::array<float, 4>{Brush.Colour[0], Brush.Colour[1], Brush.Colour[2], Strength}
                                     : std::array<float, 4>{1.f, 1.f, 1.f, Strength};
    // 🎯 Born projected: the viewport handed over the surface triangle it picked, so the stamp is a
    // surface projector from the first frame - clean on the UV at any camera angle, no island smear.
    const TopologyTriangle *Anchor = PendingAnchorTriangle;
    PendingAnchorTriangle = nullptr;
    if (!Anchor)
    {
        float Weights[3];
        Anchor = TriangleAtAuthoringUv(Uv, Weights);
    }
    if (Anchor)
    {
        if (PendingAnchorCamera && SceneFrameValid) AnchorStampFromCamera(Placement, *Anchor, Uv);
        else AnchorStampFromUv(Placement, *Anchor, Uv);
    }
    PendingAnchorCamera = false;
    // 🎚️ Per-decal PBR, seeded from the stamp design itself when it carries a vector source: the mean
    // surface-channel strengths of its visible layers become this decal's roughness/metalness/height,
    // so every stamp keeps the material behaviour it was created with (editable in the Inspector).
    if (!Entry->Builtin && !Entry->File.empty())
    {
        try
        {
            const auto Sidecar = Entry->File.parent_path() / (Entry->File.stem().string() + ".stamp.toml");
            std::ifstream Input(Sidecar, std::ios::binary);
            if (Input)
            {
                std::ostringstream Buffer;
                Buffer << Input.rdbuf();
                const auto Design = DecodeVectorAuthoring(Buffer.str());
                float Sum[3]{0, 0, 0};
                int Counted = 0;
                for (const auto &DesignLayer : Design.Layers)
                {
                    if (!DesignLayer.Visible) continue;
                    const auto &Channels = DesignLayer.SurfaceChannels;
                    const auto Strength = [&](VectorAuthoringSurfaceChannel Channel) {
                        const auto &Contribution = Channels[size_t(Channel)];
                        return Contribution.Enabled ? Contribution.Strength : 0.f;
                    };
                    Sum[0] += Strength(VectorAuthoringSurfaceChannel::Roughness);
                    Sum[1] += Strength(VectorAuthoringSurfaceChannel::Metalness);
                    Sum[2] += Strength(VectorAuthoringSurfaceChannel::Height);
                    ++Counted;
                }
                if (Counted > 0)
                {
                    Placement.Roughness = std::clamp(Sum[0] / Counted, 0.f, 1.f);
                    Placement.Metalness = std::clamp(Sum[1] / Counted, 0.f, 1.f);
                    Placement.Height = std::clamp(Sum[2] / Counted, 0.f, 1.f);
                }
            }
        }
        catch (...) { /* an unreadable sidecar leaves the neutral PBR defaults */ }
    }
    LayerState Layer;
    Layer.Id = MakeLayerId(MetaForKind(size_t(LayerKind::Decal)).Id);
    Layer.Kind = LayerKind::Decal;
    Layer.Name = UniqueLayerName(Entry->Name + " stamp");
    Layer.Channels = {"baseColor", "roughness"};
    Layer.Stamps.push_back(Placement);
    Layer.DecalCount = 1;
    size_t InsertAt = 0; // above the current selection, like AddLayer
    if (const auto *Selected = SelectedLayer())
    {
        Layer.ParentId = Selected->ParentId;
        InsertAt = size_t(Selected - Layers.data());
    }
    const std::string NewId = Layer.Id, NewName = Layer.Name, EntryName = Entry->Name;
    Layers.insert(Layers.begin() + intptr_t(InsertAt), std::move(Layer));
    SelectLayer(NewId);
    StampFocusLayerId = NewId;
    StampFocusIndex = 0;
    CommitHistory("Place stamp", NewName + " · " + EntryName, "stamp");
    MarkDirty();
    Status = "Placed '" + EntryName + "' on its own stamp layer - drag it any time with the Transform tool";
}

int TexturePaintWorkspacePanel::StampRasterExtentFor(float Radius) noexcept
{
    // 🖨️ The stamp's diameter in canvas texels (a UDIM tile is PaintCanvasTile texels), 2x supersampled
    // and bucketed: the render resolution follows the footprint on the target texture, so a large decal
    // is never an upscale of a small proxy raster.
    const int Need = int(std::ceil(2.f * Radius * float(PaintCanvasTile) * 2.f));
    for (const int Bucket : {256, 512, 1024, 2048})
        if (Need <= Bucket) return Bucket;
    return 2048;
}

namespace
{
// 🔎 Coarse art fingerprint (48x48 probes): solid-texel coverage + mean colour. Two renders of the
// same stamp agree closely; a stale sidecar describing different art does not.
bool StampArtStats(const StampLibraryEntry &Entry, float &Coverage, float Mean[3]) noexcept
{
    Coverage = 0;
    Mean[0] = Mean[1] = Mean[2] = 0;
    if (Entry.Width <= 0 || Entry.Height <= 0) return false;
    size_t Solid = 0;
    for (int By = 0; By < 48; ++By)
        for (int Bx = 0; Bx < 48; ++Bx)
        {
            const auto Texel = StampLibrary::Sample(Entry, (Bx + .5f) / 48, (By + .5f) / 48);
            if (Texel[3] <= .5f) continue;
            ++Solid;
            for (int Channel = 0; Channel < 3; ++Channel) Mean[Channel] += Texel[size_t(Channel)];
        }
    if (!Solid) return false;
    Coverage = float(Solid) / (48.f * 48.f);
    for (int Channel = 0; Channel < 3; ++Channel) Mean[Channel] /= float(Solid);
    return true;
}
} // namespace

const StampLibraryEntry *TexturePaintWorkspacePanel::ResolveStampEntry(const StampLibraryEntry &Entry, int Extent)
{
    // 🎨 The ACTUAL stamp at the needed size: builtin presets re-render their signed-distance fields;
    // stamps saved from the Stamp workspace re-rasterize their vector document (.stamp.toml sidecar);
    // raster-only stamps keep their stored pixels (their real ceiling). A sidecar re-render must
    // still LOOK like the stamp the library shows - if it disagrees with the stored raster (stale
    // sidecar from an older design, an imported-image component the SVG encoder can't reproduce),
    // the stored raster wins: the decal takes the colour it was created with, never a surrogate.
    if (std::max(Entry.Width, Entry.Height) >= Extent) return &Entry;
    const auto Key = std::pair<std::string, int>{Entry.Id, Extent};
    if (const auto Found = StampRasterCache.find(Key); Found != StampRasterCache.end()) return &Found->second;
    if (StampRasterFallbacks.count(Key)) return &Entry;
    StampLibraryEntry Resolved;
    bool Have = false;
    if (Entry.Builtin)
    {
        Have = StampLibrary::RenderBuiltinAt(Entry.Id, Extent, Resolved);
    }
    else if (!Entry.File.empty())
    {
        try
        {
            const auto Sidecar = Entry.File.parent_path() / (Entry.File.stem().string() + ".stamp.toml");
            std::ifstream Input(Sidecar, std::ios::binary);
            if (Input)
            {
                std::ostringstream Buffer;
                Buffer << Input.rdbuf();
                auto Pixels = RasterizeStampDocument(DecodeVectorAuthoring(Buffer.str()), uint32_t(Extent));
                if (Pixels.size() == size_t(Extent) * Extent * 4)
                {
                    Resolved = Entry;
                    Resolved.Width = Resolved.Height = Extent;
                    Resolved.Rgba = std::move(Pixels);
                    float StoredCoverage = 0, StoredMean[3], FreshCoverage = 0, FreshMean[3];
                    if (StampArtStats(Entry, StoredCoverage, StoredMean) && StoredCoverage > 0.01f &&
                        StampArtStats(Resolved, FreshCoverage, FreshMean))
                    {
                        const float Ratio = FreshCoverage / std::max(StoredCoverage, 1e-4f);
                        float WorstDelta = 0;
                        for (int Channel = 0; Channel < 3; ++Channel)
                            WorstDelta = std::max(WorstDelta, std::fabs(FreshMean[Channel] - StoredMean[Channel]));
                        Have = Ratio > 0.55f && Ratio < 1.8f && WorstDelta <= 0.20f;
                    }
                    else Have = true; // nothing meaningful stored to disagree with
                }
            }
        }
        catch (...) { /* a malformed sidecar keeps the stored raster */ }
    }
    if (Have)
    {
        const auto Inserted = StampRasterCache.emplace(Key, std::move(Resolved));
        return &Inserted.first->second;
    }
    StampRasterFallbacks.insert(Key); // remembered so the attempt doesn't repeat every frame
    return &Entry;
}

void TexturePaintWorkspacePanel::ResolveStampRasters()
{
    for (const auto &Layer : Layers)
        for (const auto &Placement : Layer.Stamps)
            if (const auto *Entry = StampLib.Find(Placement.StampId))
                (void)ResolveStampEntry(*Entry, StampRasterExtentFor(Placement.Radius));
    if (HoverStampActive)
        if (const auto *Entry = StampLib.Find(HoverStamp.StampId))
            (void)ResolveStampEntry(*Entry, StampRasterExtentFor(HoverStamp.Radius));
}

const StampLibraryEntry *TexturePaintWorkspacePanel::ResolvedStampRaster(const StampPlacement &Placement) const noexcept
{
    const auto Found = StampRasterCache.find({Placement.StampId, StampRasterExtentFor(Placement.Radius)});
    if (Found != StampRasterCache.end()) return &Found->second;
    return StampLib.Find(Placement.StampId);
}

bool TexturePaintWorkspacePanel::HitTestStamp(const float Uv[2], float HandleSlop, std::string &OutLayerId,
    size_t &OutIndex, int &OutZone) const noexcept
{
    // 🔎 Topmost-first (vector order), newest placement first: what the eye sees on top is what grabs.
    for (const auto &Layer : Layers)
    {
        if (Layer.Stamps.empty() || Layer.Kind == LayerKind::Folder) continue;
        if (!EffectiveVisible(Layer) || EffectiveLocked(Layer)) continue;
        for (size_t Index = Layer.Stamps.size(); Index-- > 0;)
        {
            const auto &Placement = Layer.Stamps[Index];
            const float Du = Uv[0] - Placement.U, Dv = Uv[1] - Placement.V;
            const float CosR = std::cos(Placement.Rotation), SinR = std::sin(Placement.Rotation);
            // Into stamp-local axes (the same rotation the composite uses); +LocalY is the image top.
            const float LocalX = Du * CosR + Dv * SinR, LocalY = -Du * SinR + Dv * CosR;
            const float R = Placement.Radius;
            if (HandleSlop > 0.f)
            {
                if (std::hypot(LocalX, LocalY - R * 1.35f) <= HandleSlop) // rotate handle above the top edge
                {
                    OutLayerId = Layer.Id; OutIndex = Index; OutZone = 2;
                    return true;
                }
                if (std::fabs(LocalX) <= R + HandleSlop && std::fabs(LocalY) <= R + HandleSlop &&
                    std::fabs(LocalX) >= R - HandleSlop && std::fabs(LocalY) >= R - HandleSlop)
                {
                    OutLayerId = Layer.Id; OutIndex = Index; OutZone = 1; // corner: scale
                    return true;
                }
            }
            if (std::fabs(LocalX) <= R && std::fabs(LocalY) <= R)
            {
                OutLayerId = Layer.Id; OutIndex = Index; OutZone = 0; // body: move
                return true;
            }
        }
    }
    return false;
}

TexturePaintWorkspacePanel::StampPlacement *TexturePaintWorkspacePanel::FocusedStamp(LayerState *&OutLayer) noexcept
{
    OutLayer = nullptr;
    LayerState *Layer = SelectedLayer(); // the gizmo follows the layer-stack selection...
    if (!Layer || Layer->Stamps.empty()) Layer = FindLayer(StampFocusLayerId); // ...else the last grabbed stamp's layer
    if (!Layer || Layer->Stamps.empty()) return nullptr;
    OutLayer = Layer;
    const size_t Index = size_t(std::clamp(StampFocusIndex, 0, int(Layer->Stamps.size()) - 1));
    return &Layer->Stamps[Index];
}

void TexturePaintWorkspacePanel::BeginStampDrag(const std::string &LayerId, size_t Index, int Mode, const float Uv[2])
{
    auto *Layer = FindLayer(LayerId);
    if (!Layer || Index >= Layer->Stamps.size()) return;
    SelectLayer(LayerId); // grabbing a stamp also selects its layer (and leaves mask mode)
    StampFocusLayerId = LayerId;
    StampFocusIndex = int(Index);
    const auto &Placement = Layer->Stamps[Index];
    StampDrag = {};
    StampDrag.Active = true;
    StampDrag.Mode = Mode;
    StampDrag.LayerId = LayerId;
    StampDrag.Index = Index;
    StampDrag.Original = Placement;
    StampDrag.GrabDu = Placement.U - Uv[0];
    StampDrag.GrabDv = Placement.V - Uv[1];
    StampDrag.GrabAngle = std::atan2(Uv[1] - Placement.V, Uv[0] - Placement.U);
    StampDrag.GrabDistance = std::max(1e-5f, std::hypot(Uv[0] - Placement.U, Uv[1] - Placement.V));
}

void TexturePaintWorkspacePanel::UpdateStampDrag(const float Uv[2])
{
    if (!StampDrag.Active) return;
    auto *Layer = FindLayer(StampDrag.LayerId);
    if (!Layer || StampDrag.Index >= Layer->Stamps.size()) { StampDrag.Active = false; return; }
    auto &Placement = Layer->Stamps[StampDrag.Index];
    const auto &Original = StampDrag.Original;
    if (StampDrag.Mode == 0)
    {
        Placement.U = std::clamp(Uv[0] + StampDrag.GrabDu, -0.5f, 4.5f);
        Placement.V = std::clamp(Uv[1] + StampDrag.GrabDv, -0.5f, 1.5f);
    }
    else if (StampDrag.Mode == 1)
    {
        const float Distance = std::hypot(Uv[0] - Original.U, Uv[1] - Original.V);
        Placement.Radius = std::clamp(Original.Radius * Distance / StampDrag.GrabDistance, 0.01f, 0.4f);
    }
    else
    {
        const float Angle = std::atan2(Uv[1] - Original.V, Uv[0] - Original.U);
        Placement.Rotation = Original.Rotation + (Angle - StampDrag.GrabAngle);
    }
    if (Placement.Projected)
    {
        // 🎯 The UV gesture edits the mirrors; re-anchor the projector on the surface under the new
        // centre (UV-tangent frame, so the gizmo the user sees is the frame that stamps). Scale
        // drags make the UV Radius authoritative again.
        if (StampDrag.Mode == 1) Placement.WorldRadius = 0.f;
        const float MirrorUv[2]{Placement.U, Placement.V};
        float Weights[3];
        if (const auto *Anchor = TriangleAtAuthoringUv(MirrorUv, Weights))
            AnchorStampFromUv(Placement, *Anchor, MirrorUv);
        else
            SyncProjectorAxes(Placement); // centre over empty UV: keep frame, pick up rotation changes
    }
    StampDrag.Moved = true;
    MarkDirty(); // fingerprint moves with the placement; the canvas recomposites live
}

void TexturePaintWorkspacePanel::FinishStampDrag()
{
    if (!StampDrag.Active) return;
    StampDrag.Active = false;
    if (!StampDrag.Moved) return;
    const auto *Layer = FindLayer(StampDrag.LayerId);
    CommitHistory(StampDrag.Mode == 0 ? "Move stamp" : StampDrag.Mode == 1 ? "Scale stamp" : "Rotate stamp",
        Layer ? Layer->Name : std::string("stamp"), "stamp");
    MarkDirty();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SURFACE PROJECTION (decal projector frames)
//------------------------------------------------------------------------------------------------------------------------

namespace
{
// Affine UV tangents of a topology triangle: dP/dU and dP/dV in world units (constant per triangle).
bool UvTangents(const float Position[3][3], const float Uvs[3][2], Vec3 &OutU, Vec3 &OutV) noexcept
{
    const auto &P = Position;
    const auto &T = Uvs;
    const float Du1 = T[1][0] - T[0][0], Dv1 = T[1][1] - T[0][1];
    const float Du2 = T[2][0] - T[0][0], Dv2 = T[2][1] - T[0][1];
    const float Det = Du1 * Dv2 - Du2 * Dv1;
    if (std::fabs(Det) < 1e-12f) return false;
    const Vec3 E1{P[1][0] - P[0][0], P[1][1] - P[0][1], P[1][2] - P[0][2]};
    const Vec3 E2{P[2][0] - P[0][0], P[2][1] - P[0][1], P[2][2] - P[0][2]};
    OutU = {(E1.X * Dv2 - E2.X * Dv1) / Det, (E1.Y * Dv2 - E2.Y * Dv1) / Det, (E1.Z * Dv2 - E2.Z * Dv1) / Det};
    OutV = {(E2.X * Du1 - E1.X * Du2) / Det, (E2.Y * Du1 - E1.Y * Du2) / Det, (E2.Z * Du1 - E1.Z * Du2) / Det};
    return true;
}
// Orthonormal stamp base frame on a tangent plane: Right = Direction projected into the plane
// (falling back to any stable tangent when Direction runs along the normal), Up = Normal x Right,
// so (Right, Up, Normal) stays right-handed and the art is never mirrored.
void TangentFrame(const float NormalIn[3], const Vec3 &Direction, float Right[3], float Up[3]) noexcept
{
    const Vec3 N{NormalIn[0], NormalIn[1], NormalIn[2]};
    Vec3 R = Sub(Direction, Vec3{N.X * DotV(N, Direction), N.Y * DotV(N, Direction), N.Z * DotV(N, Direction)});
    if (DotV(R, R) < 1e-8f)
    {
        const Vec3 Seed = std::fabs(N.Y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        R = Sub(Seed, Vec3{N.X * DotV(N, Seed), N.Y * DotV(N, Seed), N.Z * DotV(N, Seed)});
    }
    R = NormalizeV(R);
    const Vec3 U = CrossV(N, R);
    Right[0] = R.X; Right[1] = R.Y; Right[2] = R.Z;
    Up[0] = U.X; Up[1] = U.Y; Up[2] = U.Z;
}
} // namespace

void TexturePaintWorkspacePanel::SyncProjectorAxes(StampPlacement &Placement) noexcept
{
    const float CosR = std::cos(Placement.Rotation), SinR = std::sin(Placement.Rotation);
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        Placement.AxisRight[Axis] = Placement.BaseRight[Axis] * CosR + Placement.BaseUp[Axis] * SinR;
        Placement.AxisUp[Axis] = Placement.BaseUp[Axis] * CosR - Placement.BaseRight[Axis] * SinR;
    }
}

bool TexturePaintWorkspacePanel::BarycentricWeights(const TopologyTriangle &Triangle, const float Uv[2], float Weights[3]) noexcept
{
    const auto &A = Triangle.AuthoringUv[0], &B = Triangle.AuthoringUv[1], &C = Triangle.AuthoringUv[2];
    const float Denom = (B[1] - C[1]) * (A[0] - C[0]) + (C[0] - B[0]) * (A[1] - C[1]);
    if (std::fabs(Denom) < 1e-14f) { Weights[0] = Weights[1] = Weights[2] = 1.f / 3.f; return false; }
    Weights[0] = ((B[1] - C[1]) * (Uv[0] - C[0]) + (C[0] - B[0]) * (Uv[1] - C[1])) / Denom;
    Weights[1] = ((C[1] - A[1]) * (Uv[0] - C[0]) + (A[0] - C[0]) * (Uv[1] - C[1])) / Denom;
    Weights[2] = 1.f - Weights[0] - Weights[1];
    float Sum = 0;
    for (int Corner = 0; Corner < 3; ++Corner) { Weights[Corner] = std::max(Weights[Corner], 0.f); Sum += Weights[Corner]; }
    if (Sum > 1e-8f) for (int Corner = 0; Corner < 3; ++Corner) Weights[Corner] /= Sum;
    return true;
}

float TexturePaintWorkspacePanel::WorldPerUvAt(const TopologyTriangle &Triangle, const float[3]) noexcept
{
    Vec3 TangentU, TangentV;
    if (!UvTangents(Triangle.Position, Triangle.AuthoringUv, TangentU, TangentV)) return 1.f;
    const float Per = (std::sqrt(DotV(TangentU, TangentU)) + std::sqrt(DotV(TangentV, TangentV))) * .5f;
    return Per > 1e-6f ? Per : 1.f;
}

const TexturePaintWorkspacePanel::TopologyTriangle *TexturePaintWorkspacePanel::TriangleAtAuthoringUv(
    const float Uv[2], float Weights[3]) const noexcept
{
    for (const auto &Triangle : Topology)
        if (PointInTriangle(Uv[0], Uv[1], Triangle.AuthoringUv[0], Triangle.AuthoringUv[1], Triangle.AuthoringUv[2], Weights))
            return &Triangle;
    return nullptr;
}

// Shared anchor core: origin on the triangle at Uv, face normal, world/UV radius mirrors kept coherent
// (an existing WorldRadius wins over the UV Radius so dragging across charts cannot change the size).
void TexturePaintWorkspacePanel::AnchorProjector(StampPlacement &Placement, const TopologyTriangle &Triangle,
    const float Uv[2], const float RightDirection[3]) const noexcept
{
    float Weights[3];
    BarycentricWeights(Triangle, Uv, Weights);
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        Placement.Origin[Axis] = Weights[0] * Triangle.Position[0][Axis] +
            Weights[1] * Triangle.Position[1][Axis] + Weights[2] * Triangle.Position[2][Axis];
        Placement.Normal[Axis] = Triangle.Normal[Axis];
    }
    TangentFrame(Placement.Normal, Vec3{RightDirection[0], RightDirection[1], RightDirection[2]},
        Placement.BaseRight, Placement.BaseUp);
    const float WorldPerUv = WorldPerUvAt(Triangle, Weights);
    if (Placement.WorldRadius > 0.f)
        Placement.Radius = std::clamp(Placement.WorldRadius / WorldPerUv, 0.005f, 0.6f);
    else
        Placement.WorldRadius = Placement.Radius * WorldPerUv;
    Placement.U = Uv[0];
    Placement.V = Uv[1];
    Placement.Projected = true;
    SyncProjectorAxes(Placement);
}

void TexturePaintWorkspacePanel::AnchorStampFromUv(StampPlacement &Placement, const TopologyTriangle &Triangle, const float Uv[2]) const noexcept
{
    Vec3 TangentU{1, 0, 0}, TangentV;
    (void)UvTangents(Triangle.Position, Triangle.AuthoringUv, TangentU, TangentV);
    const float Right[3]{TangentU.X, TangentU.Y, TangentU.Z};
    AnchorProjector(Placement, Triangle, Uv, Right);
}

void TexturePaintWorkspacePanel::AnchorStampFromCamera(StampPlacement &Placement, const TopologyTriangle &Triangle, const float Uv[2]) const noexcept
{
    AnchorProjector(Placement, Triangle, Uv, SceneRight);
}

void TexturePaintWorkspacePanel::RefreshHoverStamp(const TopologyTriangle *Hit, const float Uv[2], bool CameraAligned)
{
    // 👻 The ghost is a placement exactly as a click would drop it (same size law, tint, anchor), at
    // 85% strength, rebuilt from the live pick each frame - so what you see following the mouse is
    // precisely what will stamp, projected clean on the surface at any camera angle.
    HoverStampActive = false;
    if (!Hit || StampDrag.Active || ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;
    if (Tool != AuthorTool::Stamp || DigitalActive || MaskSelected || View == ViewMode::Wireframe) return;
    const auto *Entry = ActiveStamp();
    if (!Entry) return;
    HoverStamp = {};
    HoverStamp.StampId = Entry->Id;
    HoverStamp.Radius = std::clamp(0.09f * Brush.Size / 24.f, 0.015f, 0.3f);
    const float Strength = std::clamp(Brush.Opacity * Brush.Flow, 0.05f, 1.f) * 0.85f;
    HoverStamp.Tint = Entry->Tintable ? std::array<float, 4>{Brush.Colour[0], Brush.Colour[1], Brush.Colour[2], Strength}
                                      : std::array<float, 4>{1.f, 1.f, 1.f, Strength};
    if (CameraAligned && SceneFrameValid) AnchorStampFromCamera(HoverStamp, *Hit, Uv);
    else AnchorStampFromUv(HoverStamp, *Hit, Uv);
    HoverStampActive = true;
}

void TexturePaintWorkspacePanel::BeginStampDragScene(const std::string &LayerId, size_t Index, int Mode,
    const TopologyTriangle *Hit, const float Uv[2], float MouseX, float MouseY)
{
    auto *Layer = FindLayer(LayerId);
    if (!Layer || Index >= Layer->Stamps.size()) return;
    SelectLayer(LayerId);
    StampFocusLayerId = LayerId;
    StampFocusIndex = int(Index);
    const auto &Placement = Layer->Stamps[Index];
    StampDrag = {};
    StampDrag.Active = true;
    StampDrag.SceneGesture = true;
    StampDrag.Mode = Mode;
    StampDrag.LayerId = LayerId;
    StampDrag.Index = Index;
    StampDrag.Original = Placement;
    StampDrag.GrabDu = Placement.U - Uv[0];
    StampDrag.GrabDv = Placement.V - Uv[1];
    StampDrag.GrabScreenCentre[0] = StampGizmoScreen[5][0];
    StampDrag.GrabScreenCentre[1] = StampGizmoScreen[5][1];
    StampDrag.GrabAngle = std::atan2(MouseY - StampDrag.GrabScreenCentre[1], MouseX - StampDrag.GrabScreenCentre[0]);
    StampDrag.GrabDistance = std::max(4.f, std::hypot(MouseX - StampDrag.GrabScreenCentre[0], MouseY - StampDrag.GrabScreenCentre[1]));
    if (Hit && Placement.Projected)
    {
        float Weights[3];
        BarycentricWeights(*Hit, Uv, Weights);
        float Point[3]{};
        for (int Axis = 0; Axis < 3; ++Axis)
            Point[Axis] = Weights[0] * Hit->Position[0][Axis] + Weights[1] * Hit->Position[1][Axis] + Weights[2] * Hit->Position[2][Axis];
        const Vec3 D{Point[0] - Placement.Origin[0], Point[1] - Placement.Origin[1], Point[2] - Placement.Origin[2]};
        StampDrag.GrabLocal[0] = DotV(D, Vec3{Placement.AxisRight[0], Placement.AxisRight[1], Placement.AxisRight[2]});
        StampDrag.GrabLocal[1] = DotV(D, Vec3{Placement.AxisUp[0], Placement.AxisUp[1], Placement.AxisUp[2]});
    }
}

void TexturePaintWorkspacePanel::DragStampScene(const TopologyTriangle *Hit, const float Uv[2], float MouseX, float MouseY)
{
    if (!StampDrag.Active || !StampDrag.SceneGesture) return;
    auto *Layer = FindLayer(StampDrag.LayerId);
    if (!Layer || StampDrag.Index >= Layer->Stamps.size()) { StampDrag.Active = false; return; }
    auto &Placement = Layer->Stamps[StampDrag.Index];
    const auto &Original = StampDrag.Original;
    if (!Placement.Projected)
    {
        // Legacy UV-planar stamp: the scene gesture is a move (scale/rotate live in the UV view).
        if (StampDrag.Mode == 0 && Hit)
        {
            Placement.U = std::clamp(Uv[0] + StampDrag.GrabDu, -0.5f, 4.5f);
            Placement.V = std::clamp(Uv[1] + StampDrag.GrabDv, -0.5f, 1.5f);
            StampDrag.Moved = true;
            MarkDirty();
        }
        return;
    }
    if (StampDrag.Mode == 0)
    {
        if (!Hit) return; // off-mesh: hold position, the surface slide resumes when the pick returns
        float Weights[3];
        BarycentricWeights(*Hit, Uv, Weights);
        float Point[3]{};
        for (int Axis = 0; Axis < 3; ++Axis)
            Point[Axis] = Weights[0] * Hit->Position[0][Axis] + Weights[1] * Hit->Position[1][Axis] + Weights[2] * Hit->Position[2][Axis];
        // Keep the grabbed spot of the art under the cursor (offset in the projector's tangent plane),
        // then re-hug the new face: fresh normal, camera-aligned base, same size and twist.
        for (int Axis = 0; Axis < 3; ++Axis)
            Placement.Origin[Axis] = Point[Axis] -
                (Placement.AxisRight[Axis] * StampDrag.GrabLocal[0] + Placement.AxisUp[Axis] * StampDrag.GrabLocal[1]);
        Placement.U = Uv[0] + StampDrag.GrabDu;
        Placement.V = Uv[1] + StampDrag.GrabDv;
        for (int Axis = 0; Axis < 3; ++Axis) Placement.Normal[Axis] = Hit->Normal[Axis];
        TangentFrame(Placement.Normal, Vec3{SceneRight[0], SceneRight[1], SceneRight[2]}, Placement.BaseRight, Placement.BaseUp);
        const float WorldPerUv = WorldPerUvAt(*Hit, Weights);
        Placement.Radius = std::clamp(Placement.WorldRadius / WorldPerUv, 0.005f, 0.6f);
        SyncProjectorAxes(Placement);
    }
    else if (StampDrag.Mode == 1)
    {
        const float Distance = std::hypot(MouseX - StampDrag.GrabScreenCentre[0], MouseY - StampDrag.GrabScreenCentre[1]);
        Placement.WorldRadius = std::clamp(Original.WorldRadius * Distance / StampDrag.GrabDistance, 1e-4f, 10.f);
        const float MirrorUv[2]{Placement.U, Placement.V};
        float Weights[3];
        if (const auto *Anchor = TriangleAtAuthoringUv(MirrorUv, Weights))
            Placement.Radius = std::clamp(Placement.WorldRadius / WorldPerUvAt(*Anchor, Weights), 0.005f, 0.6f);
    }
    else
    {
        const float Angle = std::atan2(MouseY - StampDrag.GrabScreenCentre[1], MouseX - StampDrag.GrabScreenCentre[0]);
        Placement.Rotation = Original.Rotation - (Angle - StampDrag.GrabAngle); // screen Y is down: screen-clockwise = -Rotation
        SyncProjectorAxes(Placement);
    }
    StampDrag.Moved = true;
    MarkDirty();
}

void TexturePaintWorkspacePanel::ConstructStampPicker()
{
    // 🧩 Material-catalogue styling (MaterialLibraryPanel::ConstructCatalogue): 18px heading + count,
    // padded search field, wrapping filter pills, hairline + overline, two-column 125px cards (centred
    // thumb well, name + category, green check disc on the active stamp) in a scrolling grid, footer tag.
    const float S = InterfaceScale;
    if (!ImGui::BeginPopup("StampLibraryPicker")) return;
    auto *Ink = ImGui::GetWindowDrawList();
    const ImVec2 Origin = ImGui::GetCursorScreenPos();
    const float Width = 356 * S;
    ImGui::Dummy(ImVec2(Width, 1)); // pin the popup width to the catalogue column

    Caption(ImVec2(Origin.x + 20 * S, Origin.y + 10 * S), "Stamp library", 18 * S, 0xd3d3c8,
        LightTypeface ? LightTypeface : RegularTypeface);
    const std::string Total = std::to_string(StampLib.Entries().size());
    Caption(ImVec2(Origin.x + 26 * S + CaptionWidth("Stamp library", 18 * S, LightTypeface), Origin.y + 19 * S),
        Total.c_str(), 8 * S, 0x777766);

    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 14 * S, Origin.y + 42 * S));
    ImGui::SetNextItemWidth(Width - 28 * S);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Spectrum(0x191919));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(35 * S, 11.5f * S));
    ImGui::InputTextWithHint("##StampSearch", "Search stamps...", StampSearch, sizeof(StampSearch));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    DrawGlyph("M11 4a7 7 0 1 0 0 14 7 7 0 0 0 0-14ZM21 21l-4.3-4.3", ImVec2(Origin.x + 27 * S, Origin.y + 54 * S), 15 * S, 0x77776f);

    float PillX = 14 * S;
    const char *Filters[]{"All stamps", "Presets", "Saved"};
    for (int Filter = 0; Filter < 3; ++Filter)
    {
        const float PillWidth = CaptionWidth(Filters[Filter], 9.5f * S) + 20 * S;
        ImGui::SetCursorScreenPos(ImVec2(Origin.x + PillX, Origin.y + 90 * S));
        if (PillButton((std::string("StampFilter") + Filters[Filter]).c_str(), Filters[Filter], PillWidth, StampFilter == Filter))
            StampFilter = Filter;
        PillX += PillWidth + 5 * S;
    }

    const std::string Query = Lowered(StampSearch);
    std::vector<const StampLibraryEntry *> Visible;
    for (const auto &Entry : StampLib.Entries())
    {
        if (StampFilter == 1 && !Entry.Builtin) continue;
        if (StampFilter == 2 && Entry.Builtin) continue;
        if (!Query.empty() && Lowered(Entry.Name).find(Query) == std::string::npos) continue;
        Visible.push_back(&Entry);
    }

    Ink->AddLine(ImVec2(Origin.x, Origin.y + 126 * S), ImVec2(Origin.x + Width, Origin.y + 126 * S), Colour32(0xffffff, .051f));
    Caption(ImVec2(Origin.x + 17 * S, Origin.y + 138 * S), "THE STAMP COLLECTION", 8 * S, 0x7e7e70);
    const std::string VisibleCount = std::to_string(Visible.size());
    Caption(ImVec2(Origin.x + Width - 17 * S - CaptionWidth(VisibleCount.c_str(), 8 * S), Origin.y + 138 * S),
        VisibleCount.c_str(), 8 * S, 0x7e7e70);

    const size_t Rows = (Visible.size() + 1) / 2;
    const float GridHeight = std::clamp(float(Rows) * 134 * S, 134 * S, 402 * S);
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 16 * S, Origin.y + 157 * S));
    ImGui::BeginChild("Stamp grid", ImVec2(Width - 32 * S, GridHeight), ImGuiChildFlags_None);
    const ImVec2 GridOrigin = ImGui::GetCursorScreenPos();
    const float Gap = 9 * S, CardHeight = 125 * S;
    const float CardWidth = std::max(32.f, (ImGui::GetContentRegionAvail().x - Gap) * .5f);
    if (Visible.empty())
    {
        if (StampFilter == 2 && StampLib.DiskCount() == 0)
        {
            Caption(ImVec2(GridOrigin.x + 2 * S, GridOrigin.y + 6 * S), "No saved stamps yet - author one in the Stamp workspace,", 8 * S, 0x777770);
            Caption(ImVec2(GridOrigin.x + 2 * S, GridOrigin.y + 20 * S), "then Document actions -> Save to stamp library.", 8 * S, 0x777770);
        }
        else Caption(ImVec2(GridOrigin.x + 2 * S, GridOrigin.y + 6 * S), "No stamps match. Try another filter or search.", 8 * S, 0x777770);
    }
    for (size_t Ordinal = 0; Ordinal < Visible.size(); ++Ordinal)
    {
        const StampLibraryEntry &Entry = *Visible[Ordinal];
        const ImVec2 Position(GridOrigin.x + (Ordinal % 2) * (CardWidth + Gap), GridOrigin.y + (Ordinal / 2) * (CardHeight + Gap));
        const ImVec2 Far(Position.x + CardWidth, Position.y + CardHeight);
        ImGui::SetCursorScreenPos(Position);
        ImGui::PushID(Entry.Id.c_str());
        const bool Clicked = ImGui::InvisibleButton("Stamp card", ImVec2(CardWidth, CardHeight));
        ImGui::PopID();
        const bool Hovered = ImGui::IsItemHovered();
        const bool Active = Entry.Id == ActiveStampId;
        if (Clicked)
        {
            ActiveStampId = Entry.Id;
            Status = "Stamp: " + Entry.Name + " - click the teapot to place it";
            ImGui::CloseCurrentPopup();
        }
        Ink->AddRectFilled(Position, Far, Colour32(0x191919), 15 * S);
        if (Hovered) Ink->AddRect(Position, Far, Colour32(0x707173), 15 * S);
        // Checker well + the stamp's own pixels at display-matched fidelity: a 128-block cache
        // (blocks land sub-pixel at card size, so no chunky upscaling), 2x2-supersampled from the
        // stamp's resolved 512px source raster - the same full-fidelity art that gets stamped.
        const float Thumb = 72 * S;
        const ImVec2 ThumbMin(Position.x + (CardWidth - Thumb) * .5f, Position.y + 6 * S);
        Ink->PushClipRect(Position, Far, true);
        Ink->AddRectFilled(ThumbMin, ImVec2(ThumbMin.x + Thumb, ThumbMin.y + Thumb), Colour32(0x1e1e1e), 8 * S);
        Ink->AddRectFilled(ThumbMin, ImVec2(ThumbMin.x + Thumb * .5f, ThumbMin.y + Thumb * .5f), Colour32(0x2a2a2a), 8 * S);
        Ink->AddRectFilled(ImVec2(ThumbMin.x + Thumb * .5f, ThumbMin.y + Thumb * .5f), ImVec2(ThumbMin.x + Thumb, ThumbMin.y + Thumb), Colour32(0x2a2a2a), 8 * S);
        constexpr int ThumbBlocks = 128;
        auto &ThumbCache = StampThumbCache[Entry.Id];
        if (ThumbCache.empty())
        {
            const StampLibraryEntry *Source = ResolveStampEntry(Entry, 512);
            ThumbCache.resize(ThumbBlocks * ThumbBlocks);
            for (int By = 0; By < ThumbBlocks; ++By)
                for (int Bx = 0; Bx < ThumbBlocks; ++Bx)
                {
                    std::array<float, 4> Texel{0, 0, 0, 0};
                    for (int Sy = 0; Sy < 2; ++Sy)
                        for (int Sx = 0; Sx < 2; ++Sx)
                        {
                            const auto Corner = StampLibrary::Sample(*Source, (Bx + .25f + .5f * Sx) / ThumbBlocks, (By + .25f + .5f * Sy) / ThumbBlocks);
                            for (int Channel = 0; Channel < 4; ++Channel) Texel[size_t(Channel)] += Corner[size_t(Channel)] * .25f;
                        }
                    const uint32_t Packed = (uint32_t(std::clamp(Texel[0], 0.f, 1.f) * 255) << 16) |
                        (uint32_t(std::clamp(Texel[1], 0.f, 1.f) * 255) << 8) | uint32_t(std::clamp(Texel[2], 0.f, 1.f) * 255);
                    ThumbCache[size_t(By) * ThumbBlocks + Bx] = Colour32(Packed, Texel[3]);
                }
        }
        const float Block = Thumb / ThumbBlocks;
        for (int By = 0; By < ThumbBlocks; ++By)
        {
            // Horizontal runs of equal colour merge into one rect (flat art stays a handful of draws).
            int RunStart = -1;
            uint32_t RunColour = 0;
            const auto Flush = [&](int EndX)
            {
                if (RunStart < 0) return;
                Ink->AddRectFilled(ImVec2(ThumbMin.x + RunStart * Block, ThumbMin.y + By * Block),
                    ImVec2(ThumbMin.x + EndX * Block + .5f, ThumbMin.y + (By + 1) * Block + .5f), RunColour);
                RunStart = -1;
            };
            for (int Bx = 0; Bx < ThumbBlocks; ++Bx)
            {
                const uint32_t Pixel = ThumbCache[size_t(By) * ThumbBlocks + Bx];
                if (((Pixel >> IM_COL32_A_SHIFT) & 0xffu) < 8) { Flush(Bx); continue; }
                if (RunStart < 0) { RunStart = Bx; RunColour = Pixel; }
                else if (Pixel != RunColour) { Flush(Bx); RunStart = Bx; RunColour = Pixel; }
            }
            Flush(ThumbBlocks);
        }
        Caption(ImVec2(Position.x + 12 * S, Position.y + 89 * S), Entry.Name.c_str(), 9 * S, 0xc8c8b6);
        Caption(ImVec2(Position.x + 12 * S, Position.y + 105 * S),
            Entry.Builtin ? (Entry.Tintable ? "Preset · tintable" : "Preset") : "Saved stamp", 7 * S, 0x767665);
        if (Active) PanelChrome::CheckDisc(Ink, Position, S);
        Ink->PopClipRect();
        if (Hovered) ImGui::SetTooltip("%s%s", Entry.Name.c_str(), Entry.Tintable ? " - tinted by brush colour" : "");
    }
    ImGui::SetCursorScreenPos(ImVec2(GridOrigin.x, GridOrigin.y + float(Rows) * (CardHeight + Gap)));
    ImGui::Dummy(ImVec2(1, 1));
    ImGui::EndChild();

    const float ActionY = Origin.y + 157 * S + GridHeight + 10 * S;
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 14 * S, ActionY));
    ImGui::BeginDisabled(!ActivateStamp);
    if (PillButton("PickerOpenStampStudio", "Stamp studio", 112 * S, false) && ActivateStamp)
    {
        ImGui::CloseCurrentPopup();
        ActivateStamp();
    }
    ImGui::EndDisabled();
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 134 * S, ActionY));
    if (PillButton("PickerReloadStamps", "Reload", 78 * S, false)) EnsureStampLibrary(true);

    const float FooterY = ActionY + 38 * S;
    const ImVec2 WindowPos = ImGui::GetWindowPos();
    Ink->AddRectFilled(ImVec2(WindowPos.x, FooterY), ImVec2(WindowPos.x + ImGui::GetWindowSize().x, FooterY + 60 * S), Colour32(0x151515));
    DrawGlyph("M5 22H19M12 2V10M8 10H16L17 18H7Z", ImVec2(Origin.x + 19 * S, FooterY + 18 * S), 19 * S, 0x9a9a8e);
    Caption(ImVec2(Origin.x + 51 * S, FooterY + 18 * S), "Placed by the brush.", 9 * S, 0xaeae9b);
    const std::string FooterLine = Total + " stamps · brush size sets scale, colour tints presets";
    Caption(ImVec2(Origin.x + 51 * S, FooterY + 35 * S), FooterLine.c_str(), 7 * S, 0x6b6b60);
    ImGui::SetCursorScreenPos(ImVec2(Origin.x, FooterY + 60 * S));
    ImGui::Dummy(ImVec2(0, 0));
    ImGui::EndPopup();
}
std::string TexturePaintWorkspacePanel::MaskRegionLabel() const
{
    if (!MaskEditor.SelectedRegion) return "No surface selection";
    const auto &Region = *MaskEditor.SelectedRegion;
    const char *Name = Region.Part >= 0 && Region.Part < 4 ? PartTable[size_t(Region.Part)].Name : "Teapot";
    switch (Region.Scope)
    {
        case RegionScope::Object: return std::string(Name) + " · Object";
        case RegionScope::Tile: return std::string(Name) + " · UV Tile " + std::to_string(PartTable[size_t(std::clamp(Region.Part, 0, 3))].Tile);
        case RegionScope::Island: return std::string(Name) + " · UV Island " + std::to_string(Region.Chart + 1);
        case RegionScope::Face: return std::string(Name) + " · Face " + std::to_string(Region.Triangle + 1);
    }
    return "No surface selection";
}
void TexturePaintWorkspacePanel::FillMaskRegion()
{
    auto *Layer = SelectedLayer();
    if (!Layer || !Layer->Mask.Present || !MaskEditor.SelectedRegion || EffectiveLocked(*Layer)) return;
    PaintFill Fill;
    Fill.Scope = MaskEditor.SelectedRegion->Scope;
    Fill.Part = MaskEditor.SelectedRegion->Part;
    Fill.Chart = MaskEditor.SelectedRegion->Chart;
    Fill.Triangle = MaskEditor.SelectedRegion->Triangle;
    const float Ink = MaskEditor.InkWhite ? 1.f : 0.f;
    Fill.Colour = {Ink, Ink, Ink, MaskEditor.Opacity / 100.f};
    if (Layer->Mask.Kind == "fill") Layer->Mask.Kind = "paint";
    Layer->Mask.Fills.push_back(Fill);
    CommitHistory("Fill mask region", Layer->Name + " · " + MaskRegionLabel(), "mask");
    MarkDirty();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     CAMERAS + PICKING
//------------------------------------------------------------------------------------------------------------------------

void TexturePaintWorkspacePanel::SynchronizeCamera()
{
    if (!SmoothedCameraReady)
    {
        SmoothedCamera = Camera;
        SmoothedCameraReady = true;
        return;
    }
    const float Blend = 1 - std::exp(-std::min(ImGui::GetIO().DeltaTime, .1f) * 8);
    SmoothedCamera.Yaw = std::remainder(SmoothedCamera.Yaw + std::remainder(Camera.Yaw - SmoothedCamera.Yaw, 6.2831853f) * Blend, 6.2831853f);
    SmoothedCamera.Pitch += (Camera.Pitch - SmoothedCamera.Pitch) * Blend;
    SmoothedCamera.Zoom += (Camera.Zoom - SmoothedCamera.Zoom) * Blend;
    SmoothedCamera.PanX += (Camera.PanX - SmoothedCamera.PanX) * Blend;
    SmoothedCamera.PanY += (Camera.PanY - SmoothedCamera.PanY) * Blend;
    if (std::abs(std::remainder(Camera.Yaw - SmoothedCamera.Yaw, 6.2831853f)) < .0002f &&
        std::abs(Camera.Pitch - SmoothedCamera.Pitch) < .0002f && std::abs(Camera.Zoom - SmoothedCamera.Zoom) < .0002f &&
        std::abs(Camera.PanX - SmoothedCamera.PanX) < .0002f && std::abs(Camera.PanY - SmoothedCamera.PanY) < .0002f)
        SmoothedCamera = Camera;
}
void TexturePaintWorkspacePanel::FrameCamera() noexcept
{
    ResetMaterialPreviewView(Camera);
    Camera.Yaw = .7f;
    Camera.Pitch = .16f;
}
void TexturePaintWorkspacePanel::FrameUv(float RegionWidth, float RegionHeight)
{
    // 🧩 TextureUVView.frame(): fit all tiles (or the selected part's tile) with the same top/bottom reserve.
    const float Width = (!UvSource && TargetPart >= 0) ? 1.f : (UvSource ? 1.f : 4.f);
    const float Top = RegionHeight < 260 ? 64 : 120;
    const float AvailableHeight = std::max(24.f, RegionHeight - Top - 62.f);
    UvScale = std::max(12.f * InterfaceScale, std::min((RegionWidth - 52.f * InterfaceScale) / Width, AvailableHeight));
    UvFrameScale = UvScale;
    const float CentreU = (!UvSource && TargetPart >= 0) ? float(TargetPart) + .5f : Width * .5f;
    UvOffsetX = RegionWidth * .5f - CentreU * UvScale;
    UvOffsetY = Top + AvailableHeight * .5f - UvScale * .5f; // screen Y of V=1 (top), V grows upward
    UvFrameRequested = false;
}
const TexturePaintWorkspacePanel::PickTriangle *TexturePaintWorkspacePanel::PickScene(float X, float Y, float OutUv[2]) const noexcept
{
    const PickTriangle *Best = nullptr;
    float BestDepth = FLT_MAX;
    float Weights[3];
    for (const auto &Triangle : LastPickTriangles)
    {
        if (PointInTriangle(X, Y, Triangle.Screen[0], Triangle.Screen[1], Triangle.Screen[2], Weights) && Triangle.Depth < BestDepth)
        {
            Best = &Triangle;
            BestDepth = Triangle.Depth;
            OutUv[0] = Weights[0] * Triangle.Uv[0][0] + Weights[1] * Triangle.Uv[1][0] + Weights[2] * Triangle.Uv[2][0];
            OutUv[1] = Weights[0] * Triangle.Uv[0][1] + Weights[1] * Triangle.Uv[1][1] + Weights[2] * Triangle.Uv[2][1];
        }
    }
    return Best;
}
void TexturePaintWorkspacePanel::HandlePaintPointer(bool Hovered, bool IsUv)
{
    (void)IsUv;
    if (!Hovered && !StrokeActive) return;
    const bool MaskBrushArmed = MaskSelected && MaskEditor.ActiveTool == MaskEditorState::Tool::Brush && !DigitalActive;
    const bool Painting = (DigitalActive && DigitalPaintAvailable()) || MaskBrushArmed;
    if (Hovered && !ImGui::GetIO().WantTextInput && Tool == AuthorTool::Stamp && !DigitalActive)
    {
        // 🖨️ Right-click (a click, not a right-drag - that still pans) opens the stamp library grid.
        const ImVec2 Mouse = ImGui::GetIO().MousePos;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            StampRightDownValid = true;
            StampRightDownX = Mouse.x;
            StampRightDownY = Mouse.y;
        }
        if (StampRightDownValid && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            StampRightDownValid = false;
            if (std::hypot(Mouse.x - StampRightDownX, Mouse.y - StampRightDownY) < 5.f)
            {
                EnsureStampLibrary(true); // catch stamps saved since the picker last opened
                ImGui::OpenPopup("StampLibraryPicker");
            }
        }
    }
    else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && Hovered && !ImGui::GetIO().WantTextInput)
        BrushStudioOpen = true; // Alloy: right-click the viewport opens the brush studio
    if (!Painting) return;
    if (Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        StrokeActive = true;
        StrokeChanged = false;
        StrokeTargetLayerId = SelectedLayerId;
        StrokeTargetMask = MaskSelected;
        LastDabValid = false;
    }
    if (StrokeActive && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) FinishStroke();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SHARED UI HELPERS
//------------------------------------------------------------------------------------------------------------------------

PlaneExtent TexturePaintWorkspacePanel::QueryControlExtent(const std::string &Caption) const
{
    const auto Position = ControlExtents.find(Caption);
    return Position == ControlExtents.end() ? PlaneExtent{} : Position->second;
}
void TexturePaintWorkspacePanel::Track(const std::string &Caption)
{
    const ImVec2 Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
    ControlExtents[Caption] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
}
void TexturePaintWorkspacePanel::DrawLayerThumbnail(float MinimumX, float MinimumY, float Size, const LayerState &Layer) const
{
    // 🖼️ TextureLayerStack's LayerThumbnail, native edition: an assigned material shows its colour swatch,
    // a paint layer shows its recorded dabs over the empty-texture checker, everything else the kind icon.
    const float S = InterfaceScale;
    auto *Ink = ImGui::GetWindowDrawList();
    const ImVec2 Minimum(MinimumX, MinimumY), Maximum(MinimumX + Size, MinimumY + Size);
    const float Radius = std::min(9 * S, Size * .26f);
    if (Layer.SourceColour[3] > 0.f)
    {
        const uint32_t Packed = (uint32_t(std::clamp(Layer.SourceColour[0], 0.f, 1.f) * 255) << 16) |
            (uint32_t(std::clamp(Layer.SourceColour[1], 0.f, 1.f) * 255) << 8) | uint32_t(std::clamp(Layer.SourceColour[2], 0.f, 1.f) * 255);
        Ink->AddRectFilledMultiColor(Minimum, Maximum, Colour32(Packed), Colour32(0x232323), Colour32(0x232323), Colour32(0x2c2c2c));
        Ink->AddRect(Minimum, Maximum, Colour32(0xffffff, .05f), Radius);
        return;
    }
    if (Layer.Kind == LayerKind::Paint || !Layer.Stamps.empty())
    {
        Ink->PushClipRect(Minimum, Maximum, true);
        Ink->AddRectFilled(Minimum, Maximum, Colour32(0x1c1c1c));
        const float Cell = Size / 4.f;
        for (int Y = 0; Y < 4; ++Y)
            for (int X = 0; X < 4; ++X)
                if ((X + Y) & 1)
                    Ink->AddRectFilled(ImVec2(Minimum.x + X * Cell, Minimum.y + Y * Cell), ImVec2(Minimum.x + (X + 1) * Cell, Minimum.y + (Y + 1) * Cell), Colour32(0x282824));
        for (const auto &Dab : Layer.Dabs)
            Ink->AddCircleFilled(ImVec2(Minimum.x + Dab.U / 4.f * Size, Minimum.y + (1.f - Dab.V) * Size),
                std::max(1.2f * S, Dab.Radius * Size * .25f),
                IM_COL32(uint8_t(std::clamp(Dab.Colour[0], 0.f, 1.f) * 255), uint8_t(std::clamp(Dab.Colour[1], 0.f, 1.f) * 255),
                    uint8_t(std::clamp(Dab.Colour[2], 0.f, 1.f) * 255), 215), 10);
        // 🖨️ Stamp footprints as tinted rotated quads, so a stamp layer reads at a glance in the stack.
        for (const auto &Placement : Layer.Stamps)
        {
            const float CosR = std::cos(Placement.Rotation), SinR = std::sin(Placement.Rotation);
            const auto At = [&](float Lx, float Ly)
            {
                return ImVec2(Minimum.x + (Placement.U + Lx * CosR - Ly * SinR) / 4.f * Size,
                    Minimum.y + (1.f - (Placement.V + Lx * SinR + Ly * CosR)) * Size);
            };
            const float R = Placement.Radius;
            const ImVec2 Corners[4]{At(-R, -R), At(R, -R), At(R, R), At(-R, R)};
            const ImU32 Tint = IM_COL32(uint8_t(std::clamp(Placement.Tint[0], 0.f, 1.f) * 255), uint8_t(std::clamp(Placement.Tint[1], 0.f, 1.f) * 255),
                uint8_t(std::clamp(Placement.Tint[2], 0.f, 1.f) * 255), 230);
            Ink->AddConvexPolyFilled(Corners, 4, Tint);
        }
        Ink->PopClipRect();
        Ink->AddRect(Minimum, Maximum, Colour32(0xffffff, .05f), Radius);
        return;
    }
    const auto &Kind = MetaForKind(size_t(Layer.Kind));
    Ink->AddRectFilled(Minimum, Maximum, Colour32(0x232323), Radius);
    Ink->AddRect(Minimum, Maximum, Colour32(0xffffff, .05f), Radius);
    DrawGlyph(Kind.Icon, ImVec2(Minimum.x + Size * .25f, Minimum.y + Size * .25f), Size * .5f, Kind.Colour);
}
bool TexturePaintWorkspacePanel::PillButton(const char *Id, const char *Label, float Width, bool Selected, const char *IconPath, uint32_t Accent)
{
    // Shared capsule drawing (PanelChrome::Pill); the panel only adds its extent tracking on top.
    const bool Pressed = PanelChrome::Pill(Id, Label, ImGui::GetCursorScreenPos(), Width, 26 * InterfaceScale, Selected, InterfaceScale, IconPath, Accent);
    Track(Label);
    return Pressed;
}
bool TexturePaintWorkspacePanel::IconButton(const char *Id, const char *IconPath, float Size, bool Selected, uint32_t Colour)
{
    return PanelChrome::BareIconButton(Id, IconPath, Size, InterfaceScale, Selected, Colour);
}
const char *TexturePaintWorkspacePanel::BrushPresetName() const noexcept
{
    for (const auto &Preset : PresetTable)
        if (Brush.Preset == Preset.Id) return Preset.Name;
    return "Studio round";
}
void TexturePaintWorkspacePanel::ApplyBrushPreset(const std::string &PresetId)
{
    // 🧩 selectDigitalBrush: defaults + the preset's property overrides, keeping colour and tool.
    const auto Colour = Brush.Colour;
    const std::string ToolName = Brush.Tool;
    const std::string PhysicalToolName = Brush.PhysicalTool;
    Brush = BrushState{};
    Brush.Colour = Colour;
    Brush.Tool = ToolName;
    Brush.PhysicalTool = PhysicalToolName;
    Brush.Preset = PresetId;
    if (PresetId == "soft") { Brush.Hardness = .1f; Brush.Flow = .3f; Brush.Spacing = .12f; }
    else if (PresetId == "flat") { Brush.Roundness = .3f; Brush.Follow = true; }
    else if (PresetId == "pixel") { Brush.Shape = "square"; Brush.Hardness = 1.f; Brush.Size = 12.f; Brush.Spacing = .08f; }
    else if (PresetId == "grain") { Brush.Grain = "noise"; Brush.GrainAmount = .5f; Brush.GrainScale = 4.f; Brush.Hardness = .8f; }
    CommitHistory("Digital brush settings", BrushPresetName(), "tool");
    MarkDirty();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     HEADER (WORKSPACE SWITCHER)
//------------------------------------------------------------------------------------------------------------------------

void TexturePaintWorkspacePanel::ConstructHeader()
{
    const auto *Viewport = ImGui::GetMainViewport();
    const float S = InterfaceScale;
    ImGui::SetNextWindowPos(Viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(Viewport->WorkSize.x, 43 * S));
    ImGui::Begin("Texture paint header", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoScrollbar);
    const auto Origin = ImGui::GetWindowPos();
    const float Width = ImGui::GetWindowWidth();
    ImGui::GetWindowDrawList()->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + 43 * S), Colour32(0x0b0b0b));
    Caption(ImVec2(Origin.x + 16 * S, Origin.y + 16 * S), DocumentName.c_str(), 10 * S, 0xc5c5b8);
    Caption(ImVec2(Origin.x + 22 * S + CaptionWidth(DocumentName.c_str(), 10 * S), Origin.y + 19 * S), ".texture", 7 * S, 0x67675b);

    Controls.ControlHeight = 24;
    const float NavigationX = Origin.x + std::max(200.f * S, Width - 700.f * S);
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

    // 🧩 StudioHeader's Texture children, right-to-left: close X, "Save project", open-project icon, paint tool.
    float ActionX = Origin.x + Width - 44 * S;
    ImGui::SetCursorScreenPos(ImVec2(ActionX, Origin.y + 9 * S));
    ImGui::BeginDisabled(!ActivateMaterial);
    if (ImGui::Button("X", ImVec2(28 * S, 24 * S)) && ActivateMaterial) ActivateMaterial();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close texture studio");
    ImGui::EndDisabled();
    {
        const float PillWidth = 108 * S;
        ActionX -= PillWidth + 8 * S;
        ImGui::SetCursorScreenPos(ImVec2(ActionX, Origin.y + 9 * S));
        Controls.ControlHeight = 24;
        if (PillButton("SaveProject", "Save project", PillWidth, false, "M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8ZM14 2v6h6")) SaveProject();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save the layer project JSON (sources included, no composited maps).");
    }
    {
        ActionX -= 30 * S;
        ImGui::SetCursorScreenPos(ImVec2(ActionX, Origin.y + 8 * S));
        if (IconButton("OpenProject", "M3 7a2 2 0 0 1 2-2h3.5l1.7 1.7H19a2 2 0 0 1 2 2v7.3a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2Z", 26 * S)) OpenProject();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open layer project JSON");
    }
    {
        const std::string Preset = BrushPresetName();
        const float PillWidth = 34 * S + CaptionWidth(Preset.c_str(), 9.5f * S);
        ActionX -= PillWidth + 4 * S;
        ImGui::SetCursorScreenPos(ImVec2(ActionX, Origin.y + 9 * S));
        if (PillButton("PaintToolMenu", Preset.c_str(), PillWidth, BrushStudioOpen, "M4 20L8 19L20 7L17 4L5 16ZM14 3L21 10"))
            BrushStudioOpen = !BrushStudioOpen;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Digital Paint / Erase / Fill · also right-click the viewport");
        Track("Open paint tool menu");
    }
    ImGui::End();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     LAYER STACK DOCK (LEFT)
//------------------------------------------------------------------------------------------------------------------------

void TexturePaintWorkspacePanel::ConstructLayersDock()
{
    // 📝 Docked by PanelDockspace - no SetNextWindowPos/Size or Begin/End here.
    const float S = InterfaceScale;
    Controls.Scale = S;
    auto *Ink = ImGui::GetWindowDrawList();
    const ImVec2 WindowPosition = ImGui::GetWindowPos();
    const float WindowWidth = ImGui::GetWindowWidth();

    // .tp-pane-heading: 71px, h1 23px/300 "Layers", "{count} / 64", round folder-plus / plus icons at right.
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        Caption(ImVec2(Origin.x + 21 * S, Origin.y + 22 * S), "Layers", 23 * S, 0xe6e6dc, LightTypeface ? LightTypeface : RegularTypeface);
        char Count[24];
        std::snprintf(Count, sizeof(Count), "%zu / 64", Layers.size());
        Caption(ImVec2(Origin.x + 30 * S + CaptionWidth("Layers", 23 * S, LightTypeface), Origin.y + 34 * S), Count, 9 * S, 0x676760);
        ImGui::BeginDisabled(Layers.size() >= 64);
        ImGui::SetCursorScreenPos(ImVec2(WindowPosition.x + WindowWidth - 76 * S, Origin.y + 21 * S));
        if (IconButton("AddLayerFolder",
                "M3 7a2 2 0 0 1 2-2h3.5l1.7 1.7H19a2 2 0 0 1 2 2v7.3a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2ZM12 10v6M9 13h6", 29 * S))
            AddLayer(LayerKind::Folder);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("New folder");
        ImGui::SetCursorScreenPos(ImVec2(WindowPosition.x + WindowWidth - 44 * S, Origin.y + 21 * S));
        if (IconButton("AddLayer", "M12 5v14M5 12h14", 29 * S, AddMenuOpen)) AddMenuOpen = !AddMenuOpen;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Add layer");
        ImGui::EndDisabled();
        Track("Add layer");
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 71 * S));
    }

    // 📝 Add-layer menu (.tp-add-menu): absolute card under the heading listing every TEXTURE_KINDS entry.
    if (AddMenuOpen)
    {
        ImGui::SetNextWindowPos(ImVec2(WindowPosition.x + 13 * S, ImGui::GetCursorScreenPos().y + 19 * S), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(WindowWidth - 26 * S, 0));
        ImGui::OpenPopup("AddTextureLayerMenu");
        AddMenuOpen = false;
    }
    if (ImGui::BeginPopup("AddTextureLayerMenu"))
    {
        const ImVec2 MenuCaptionOrigin = ImGui::GetCursorScreenPos();
        Caption(ImVec2(MenuCaptionOrigin.x + 8 * S, MenuCaptionOrigin.y + 2 * S), "ADD A LAYER", 8 * S, 0x77776e);
        ImGui::Dummy(ImVec2(1, 18 * S));
        for (size_t Index = 0; Index < KindTable.size(); ++Index)
        {
            const auto &Kind = KindTable[Index];
            const ImVec2 RowOrigin = ImGui::GetCursorScreenPos();
            const float RowWidth = ImGui::GetContentRegionAvail().x;
            ImGui::PushID(int(Index));
            const bool Pressed = ImGui::InvisibleButton("##Kind", ImVec2(RowWidth, 44 * S));
            ImGui::PopID();
            if (ImGui::IsItemHovered()) Ink->AddRectFilled(RowOrigin, ImVec2(RowOrigin.x + RowWidth, RowOrigin.y + 44 * S), Colour32(0x2a2a26), 9 * S);
            Ink->AddRectFilled(ImVec2(RowOrigin.x + 5 * S, RowOrigin.y + 7 * S), ImVec2(RowOrigin.x + 35 * S, RowOrigin.y + 37 * S), Colour32(0x232323), 9 * S);
            Ink->AddRect(ImVec2(RowOrigin.x + 5 * S, RowOrigin.y + 7 * S), ImVec2(RowOrigin.x + 35 * S, RowOrigin.y + 37 * S), Colour32(0xffffff, .05f), 9 * S);
            DrawGlyph(Kind.Icon, ImVec2(RowOrigin.x + 12.5f * S, RowOrigin.y + 14.5f * S), 15 * S, Kind.Colour);
            const std::string Label = std::string(Kind.Label) + (Index == 5 ? "" : " layer");
            Caption(ImVec2(RowOrigin.x + 44 * S, RowOrigin.y + 9 * S), Label.c_str(), 11 * S, 0xd6d6ca);
            Caption(ImVec2(RowOrigin.x + 44 * S, RowOrigin.y + 26 * S), Kind.MenuHint, 8 * S, 0x7d7d73);
            Caption(ImVec2(RowOrigin.x + RowWidth - 20 * S, RowOrigin.y + 14 * S), "+", 13 * S, 0x9a9a8c);
            if (Pressed)
            {
                AddLayer(LayerKind(Index));
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    int VisibleCount = 0;
    for (const auto &Layer : Layers)
        if (EffectiveVisible(Layer)) ++VisibleCount;
    const int HiddenCount = int(Layers.size()) - VisibleCount;

    // .tp-layer-stats: two 66px cards (Visible / Hidden) that double as the stack filter, drawn by
    // the shared PanelChrome::VisibilityStatCards (Stamp/Pattern compose theirs from it too).
    {
        int ClickedCard = -1;
        PanelChrome::VisibilityStatCards(ImGui::GetCursorScreenPos(), (WindowWidth - 36 * S) * .5f, 14 * S, S,
            size_t(VisibleCount), size_t(HiddenCount),
            Filter == LayerFilter::Visible ? 0 : Filter == LayerFilter::Hidden ? 1 : -1, ClickedCard, LightTypeface, RegularTypeface);
        if (ClickedCard == 0) Filter = Filter == LayerFilter::Visible ? LayerFilter::All : LayerFilter::Visible;
        else if (ClickedCard == 1) Filter = Filter == LayerFilter::Hidden ? LayerFilter::All : LayerFilter::Hidden;
    }

    // .tp-layer-search: 36px pill with search glyph, text entry and a clear button.
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        const ImVec2 BoxMin(Origin.x + 14 * S, Origin.y + 12 * S), BoxMax(Origin.x + WindowWidth - 14 * S, Origin.y + 48 * S);
        Ink->AddRectFilled(BoxMin, BoxMax, Colour32(0x191919), 18 * S);
        Ink->AddRect(BoxMin, BoxMax, Colour32(0x2c2c2c), 18 * S);
        DrawGlyph("M11 4a7 7 0 1 0 0 14 7 7 0 0 0 0-14ZM21 21l-4.3-4.3", ImVec2(BoxMin.x + 11 * S, BoxMin.y + 11 * S), 14 * S, 0x77776f);
        ImGui::SetCursorScreenPos(ImVec2(BoxMin.x + 33 * S, BoxMin.y + 6 * S));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        ImGui::SetNextItemWidth(BoxMax.x - BoxMin.x - 66 * S);
        ImGui::InputText("##FindLayer", LayerQuery, sizeof(LayerQuery));
        const bool SearchActive = ImGui::IsItemActive();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        Track("Find a layer");
        if (!LayerQuery[0] && !SearchActive)
            Caption(ImVec2(BoxMin.x + 37 * S, BoxMin.y + 12 * S), "Find a layer…", 10 * S, 0x787871);
        if (LayerQuery[0])
        {
            ImGui::SetCursorScreenPos(ImVec2(BoxMax.x - 28 * S, BoxMin.y + 7 * S));
            if (IconButton("ClearLayerSearch", "M6 6L18 18M6 18L18 6", 22 * S)) LayerQuery[0] = '\0';
        }
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 56 * S));
    }
    if (Filter != LayerFilter::All)
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        Caption(ImVec2(Origin.x + 17 * S, Origin.y), Filter == LayerFilter::Visible ? "Visible layers" : "Hidden layers", 9 * S, 0x9b927f);
        ImGui::SetCursorScreenPos(ImVec2(Origin.x + WindowWidth - 96 * S, Origin.y - 3 * S));
        if (PillButton("ClearFilter", "Clear filter", 82 * S, false)) Filter = LayerFilter::All;
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 22 * S));
    }

    // Shown set: folder-collapse, visibility filter and search all apply to the flat top-to-bottom order.
    const std::string Query = Lowered(LayerQuery);
    std::vector<size_t> Shown;
    for (size_t Index = 0; Index < Layers.size(); ++Index)
    {
        const auto &Layer = Layers[Index];
        if (HiddenByCollapsedFolder(Layer)) continue;
        if (Filter == LayerFilter::Visible && !EffectiveVisible(Layer)) continue;
        if (Filter == LayerFilter::Hidden && EffectiveVisible(Layer)) continue;
        if (!Query.empty() && Lowered(Layer.Name).find(Query) == std::string::npos) continue;
        Shown.push_back(Index);
    }

    // .tp-layer-order
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        Caption(ImVec2(Origin.x + 18 * S, Origin.y + 10 * S), "TOP -> BOTTOM", 8 * S, 0x686862);
        char ShownText[32];
        std::snprintf(ShownText, sizeof(ShownText), "%zu shown", Shown.size());
        Caption(ImVec2(Origin.x + WindowWidth - 18 * S - CaptionWidth(ShownText, 8 * S), Origin.y + 10 * S), ShownText, 8 * S, 0x55554f);
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 29 * S));
    }

    const float ReservedBottom = (40 + 34 + 45 + (SystemBaseOpen ? 168 : 0)) * S;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("TextureLayerList", ImVec2(0, std::max(40.f, ImGui::GetContentRegionAvail().y - ReservedBottom)), false);
    ImGui::PopStyleVar();
    const float ListWidth = ImGui::GetContentRegionAvail().x;
    for (const size_t LayerIndex : Shown)
    {
        if (LayerIndex >= Layers.size()) continue; // an in-row action may have shrunk the stack this frame
        const std::string LayerId = Layers[LayerIndex].Id;
        const LayerState LayerSnapshot = Layers[LayerIndex];
        const bool Selected = SelectedLayerId == LayerId && !MaskSelected;
        const bool Locked = EffectiveLocked(LayerSnapshot);
        const int Depth = LayerDepth(LayerSnapshot);
        const ImVec2 RowOrigin = ImGui::GetCursorScreenPos();
        const float RowX = RowOrigin.x + 10 * S + Depth * 10 * S;
        const float RowWidth = ListWidth - 20 * S - Depth * 10 * S;
        constexpr float RowHeightUnits = 62.f;
        ImGui::PushID(LayerId.c_str());
        ImGui::SetCursorScreenPos(ImVec2(RowX + 20 * S, RowOrigin.y));
        const bool RowClicked = ImGui::InvisibleButton("##Select", ImVec2(RowWidth - 68 * S, RowHeightUnits * S));
        const bool RowHovered = ImGui::IsItemHovered();
        if (RowClicked) SelectLayer(LayerId);
        if (Selected) Track("Selected layer row");
        auto *RowInk = ImGui::GetWindowDrawList();
        if (Selected || RowHovered)
            RowInk->AddRectFilled(ImVec2(RowX, RowOrigin.y), ImVec2(RowX + RowWidth, RowOrigin.y + RowHeightUnits * S), Colour32(Selected ? 0x262626 : 0x1b1b1b), 9 * S);
        if (Selected)
        {
            RowInk->AddRect(ImVec2(RowX, RowOrigin.y), ImVec2(RowX + RowWidth, RowOrigin.y + RowHeightUnits * S), Colour32(0x3e3e3c), 9 * S);
            RowInk->AddRectFilled(ImVec2(RowX, RowOrigin.y), ImVec2(RowX + 3 * S, RowOrigin.y + RowHeightUnits * S), Colour32(0xd9b281), 3 * S);
        }
        // Grip dots (drag reorder is pointer-driven in Alloy; native reorder lives on the action bar below).
        for (int DotY = 0; DotY < 3; ++DotY)
            for (int DotX = 0; DotX < 2; ++DotX)
                RowInk->AddCircleFilled(ImVec2(RowX + (7 + DotX * 4) * S, RowOrigin.y + (25 + DotY * 6) * S), 1.1f * S, Colour32(0x55534a), 8);
        // Disclosure chevron.
        ImGui::SetCursorScreenPos(ImVec2(RowX + 14 * S, RowOrigin.y + 19 * S));
        if (IconButton("Disclosure", LayerSnapshot.Expanded ? "m6 9 6 6 6-6" : "m9 6 6 6-6 6", 14 * S, false, 0x707068))
            if (auto *Layer = FindLayer(LayerId)) Layer->Expanded = !Layer->Expanded;
        // LayerThumbnail (34px): material swatch / paint-target checker+dabs / kind icon.
        const auto &Kind = MetaForKind(size_t(LayerSnapshot.Kind));
        const ImVec2 ChipMin(RowX + 32 * S, RowOrigin.y + 14 * S), ChipMax(ChipMin.x + 34 * S, ChipMin.y + 34 * S);
        DrawLayerThumbnail(ChipMin.x, ChipMin.y, 34 * S, LayerSnapshot);
        // Name + "{kind} · {blend} · {opacity}%".
        const bool Dimmed = !EffectiveVisible(LayerSnapshot);
        Caption(ImVec2(ChipMax.x + 7 * S, RowOrigin.y + 15 * S), LayerSnapshot.Name.c_str(), 11 * S, Dimmed ? 0x6a6a62 : Selected ? 0xeeeee8 : 0xc0c0b8);
        char Sub[96];
        if (LayerSnapshot.Kind == LayerKind::Folder)
        {
            int Items = 0;
            for (const auto &Candidate : Layers)
                if (Candidate.ParentId == LayerId) ++Items;
            std::snprintf(Sub, sizeof(Sub), "%d %s · %s · %d%%", Items, Items == 1 ? "item" : "items", LayerSnapshot.Blend.c_str(), int(std::round(LayerSnapshot.Opacity)));
        }
        else std::snprintf(Sub, sizeof(Sub), "%s · %s · %d%%", Kind.Label, LayerSnapshot.Blend.c_str(), int(std::round(LayerSnapshot.Opacity)));
        Caption(ImVec2(ChipMax.x + 7 * S, RowOrigin.y + 34 * S), Sub, 8 * S, Selected ? 0xa29c90 : 0x696964);
        // Eye + lock at the row's right edge.
        ImGui::SetCursorScreenPos(ImVec2(RowX + RowWidth - 46 * S, RowOrigin.y + 19 * S));
        if (IconButton("Eye", LayerSnapshot.Visible ? "M2 12Q12 0 22 12Q12 24 2 12ZM12 9A3 3 0 1 0 12 15A3 3 0 1 0 12 9Z"
                                                     : "M2 12Q12 0 22 12Q12 24 2 12ZM12 9A3 3 0 1 0 12 15A3 3 0 1 0 12 9ZM4 4L20 20",
                20 * S, false, 0x8a8a80))
        {
            if (auto *Layer = FindLayer(LayerId))
            {
                Layer->Visible = !Layer->Visible;
                CommitHistory(Layer->Visible ? "Show layer / folder" : "Hide layer / folder", Layer->Name, "layer");
                MarkDirty();
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s %s", LayerSnapshot.Visible ? "Hide" : "Show", LayerSnapshot.Name.c_str());
        ImGui::SetCursorScreenPos(ImVec2(RowX + RowWidth - 24 * S, RowOrigin.y + 19 * S));
        const bool InheritedLock = Locked && !LayerSnapshot.Locked;
        ImGui::BeginDisabled(InheritedLock);
        if (IconButton("Lock", Locked ? "M6 10H18V21H6ZM8 10V7A4 4 0 0 1 16 7V10" : "M6 10H18V21H6ZM8 10V7A4 4 0 0 1 15.5 4.5", 20 * S, Locked,
                Locked ? 0xbaa789 : 0x8a8a80))
        {
            if (auto *Layer = FindLayer(LayerId))
            {
                Layer->Locked = !Layer->Locked;
                CommitHistory(Layer->Locked ? "Lock layer / folder" : "Unlock layer / folder", Layer->Name, "layer");
                MarkDirty();
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s %s", LayerSnapshot.Locked ? "Unlock" : "Lock", LayerSnapshot.Name.c_str());
        ImGui::PopID();
        ImGui::SetCursorScreenPos(ImVec2(RowOrigin.x, RowOrigin.y + (RowHeightUnits + 2) * S));

        // Mask sub-row.
        if (LayerSnapshot.Mask.Present)
        {
            const bool MaskIsSelected = SelectedLayerId == LayerId && MaskSelected;
            const ImVec2 MaskOrigin = ImGui::GetCursorScreenPos();
            const float MaskX = MaskOrigin.x + 35 * S + Depth * 10 * S;
            const float MaskWidth = ListWidth - 44 * S - Depth * 10 * S;
            ImGui::PushID((LayerId + "-mask").c_str());
            ImGui::SetCursorScreenPos(ImVec2(MaskX, MaskOrigin.y));
            const bool MaskClicked = ImGui::InvisibleButton("##MaskSelect", ImVec2(MaskWidth - 26 * S, 43 * S));
            if (MaskClicked) SelectLayer(LayerId, true);
            if (MaskIsSelected)
            {
                RowInk->AddRectFilled(ImVec2(MaskX, MaskOrigin.y), ImVec2(MaskX + MaskWidth, MaskOrigin.y + 43 * S), Colour32(0x262626), 8 * S);
                RowInk->AddRect(ImVec2(MaskX, MaskOrigin.y), ImVec2(MaskX + MaskWidth, MaskOrigin.y + 43 * S), Colour32(0x3c3c37), 8 * S);
                RowInk->AddRectFilled(ImVec2(MaskX, MaskOrigin.y), ImVec2(MaskX + 3 * S, MaskOrigin.y + 43 * S), Colour32(0xd9b281), 3 * S);
            }
            const ImVec2 MaskChip(MaskX + 8 * S, MaskOrigin.y + 11.5f * S);
            RowInk->AddRectFilled(MaskChip, ImVec2(MaskChip.x + 20 * S, MaskChip.y + 20 * S), Colour32(0x0c0c0c), 5 * S);
            RowInk->AddRect(MaskChip, ImVec2(MaskChip.x + 20 * S, MaskChip.y + 20 * S), Colour32(0x595954), 5 * S);
            uint32_t Inner = LayerSnapshot.Mask.Kind == "color"
                ? (uint32_t(LayerSnapshot.Mask.Colour[0] * 255) << 16 | uint32_t(LayerSnapshot.Mask.Colour[1] * 255) << 8 | uint32_t(LayerSnapshot.Mask.Colour[2] * 255))
                : LayerSnapshot.Mask.Fill >= .5f ? 0xe6e6df : 0x090909;
            if (LayerSnapshot.Mask.Kind == "gradient") Inner = 0x8a8a82;
            RowInk->AddRectFilled(ImVec2(MaskChip.x + 2 * S, MaskChip.y + 2 * S), ImVec2(MaskChip.x + 18 * S, MaskChip.y + 18 * S), Colour32(Inner), 2 * S);
            const char *MaskLabel = LayerSnapshot.Mask.Kind == "color" ? "Colour mask" : LayerSnapshot.Mask.Kind == "gradient" ? "Gradient mask" : "Layer mask";
            Caption(ImVec2(MaskChip.x + 29 * S, MaskOrigin.y + 8 * S), MaskLabel, 10 * S, 0xc8c8ba);
            char MaskSub[64];
            const std::string FillLabel = LayerSnapshot.Mask.Kind == "gradient" ? "Surface field"
                : LayerSnapshot.Mask.Kind == "color" ? HexColour(LayerSnapshot.Mask.Colour)
                : LayerSnapshot.Mask.Inverted ? "Inverted" : LayerSnapshot.Mask.Fill >= .5f ? "White" : "Black";
            std::snprintf(MaskSub, sizeof(MaskSub), "%s · %d%%", FillLabel.c_str(), int(std::round(LayerSnapshot.Mask.Strength)));
            Caption(ImVec2(MaskChip.x + 29 * S, MaskOrigin.y + 25 * S), MaskSub, 8 * S, 0x777770);
            ImGui::SetCursorScreenPos(ImVec2(MaskX + MaskWidth - 24 * S, MaskOrigin.y + 9.5f * S));
            ImGui::BeginDisabled(Locked);
            if (IconButton("MaskEye", LayerSnapshot.Mask.Enabled ? "M2 12Q12 0 22 12Q12 24 2 12ZM12 9A3 3 0 1 0 12 15A3 3 0 1 0 12 9Z"
                                                                 : "M2 12Q12 0 22 12Q12 24 2 12ZM4 4L20 20",
                    20 * S, false, 0x8a8a80))
            {
                if (auto *Layer = FindLayer(LayerId))
                {
                    Layer->Mask.Enabled = !Layer->Mask.Enabled;
                    CommitHistory("Mask properties", Layer->Name, "mask");
                    MarkDirty();
                }
            }
            ImGui::EndDisabled();
            ImGui::PopID();
            ImGui::SetCursorScreenPos(ImVec2(MaskOrigin.x, MaskOrigin.y + 48 * S));
        }

        // Expanded channel-target chips (.tp-layer-expanded).
        if (LayerSnapshot.Expanded && LayerSnapshot.Kind != LayerKind::Folder)
        {
            const ImVec2 Origin2 = ImGui::GetCursorScreenPos();
            const float BlockX = Origin2.x + 27 * S + Depth * 10 * S;
            RowInk->AddLine(ImVec2(BlockX - 10 * S, Origin2.y + 4 * S), ImVec2(BlockX - 10 * S, Origin2.y + 58 * S), Colour32(0x343431));
            Caption(ImVec2(BlockX, Origin2.y + 6 * S), LayerSnapshot.SourceAsset.empty() ? "CHANNEL TARGETS" : "MATERIAL", 7 * S, 0x8f8f86);
            if (!LayerSnapshot.SourceAsset.empty())
            {
                Caption(ImVec2(BlockX, Origin2.y + 22 * S), LayerSnapshot.SourceAsset.c_str(), 9 * S, 0xa8a89a);
                Caption(ImVec2(BlockX, Origin2.y + 40 * S), "Native material · asset link stored on the layer", 8 * S, 0x75756d);
            }
            else
            {
                float ChipX = BlockX;
                for (const auto &Channel : ChannelTable)
                {
                    if (std::find(LayerSnapshot.Channels.begin(), LayerSnapshot.Channels.end(), Channel.Id) == LayerSnapshot.Channels.end()) continue;
                    RowInk->AddRectFilled(ImVec2(ChipX, Origin2.y + 20 * S), ImVec2(ChipX + 22 * S, Origin2.y + 41 * S), Colour32(0x21211f), 6 * S);
                    RowInk->AddRect(ImVec2(ChipX, Origin2.y + 20 * S), ImVec2(ChipX + 22 * S, Origin2.y + 41 * S), Colour32(0xffffff, .035f), 6 * S);
                    Caption(ImVec2(ChipX + (22 * S - CaptionWidth(Channel.Short, 8 * S)) * .5f, Origin2.y + 26 * S), Channel.Short, 8 * S, Channel.Colour);
                    ChipX += 26 * S;
                    if (ChipX > Origin2.x + ListWidth - 60 * S) break;
                }
                if (LayerSnapshot.Channels.empty()) Caption(ImVec2(BlockX, Origin2.y + 24 * S), "No channels enabled", 9 * S, 0x666660);
                const char *Note = LayerSnapshot.StrokeCount > 0 ? "Preview paint · setup visible in viewport"
                    : LayerSnapshot.Kind == LayerKind::Paint ? "Empty texture source · setup only"
                                                              : "Unevaluated source · setup only";
                Caption(ImVec2(BlockX, Origin2.y + 47 * S), Note, 8 * S, 0x75756d);
            }
            ImGui::SetCursorScreenPos(ImVec2(Origin2.x, Origin2.y + 64 * S));
        }
    }
    if (Shown.empty())
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        const float CentreX = Origin.x + ListWidth * .5f;
        DrawGlyph(LayersIconPath, ImVec2(CentreX - 13 * S, Origin.y + 35 * S), 26 * S, 0x6f6f66);
        const char *Title = Layers.empty() ? "Start with a layer" : "No matching layers";
        Caption(ImVec2(CentreX - CaptionWidth(Title, 13 * S) * .5f, Origin.y + 72 * S), Title, 13 * S, 0xb5b5a9);
        const char *Body = Layers.empty() ? "Add a layer to organize your surface." : "Clear the search or visibility filter.";
        Caption(ImVec2(CentreX - CaptionWidth(Body, 9 * S) * .5f, Origin.y + 94 * S), Body, 9 * S, 0x77776f);
        ImGui::SetCursorScreenPos(ImVec2(CentreX - 55 * S, Origin.y + 118 * S));
        if (PillButton("EmptyAction", Layers.empty() ? "+ Add layer" : "Clear filters", 110 * S, false))
        {
            if (Layers.empty()) AddMenuOpen = true;
            else { LayerQuery[0] = '\0'; Filter = LayerFilter::All; }
        }
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 200 * S));
    }
    // 📝 Anchor the scroll child's content boundary at the last reserved row (ImGui errors when a
    // terminal SetCursorScreenPos extends past every submitted item - same fix in each dock below).
    ImGui::Dummy(ImVec2(0, 0));
    ImGui::EndChild();

    // .tp-layer-actions toolbar.
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        Ink->AddLine(ImVec2(Origin.x, Origin.y), ImVec2(Origin.x + WindowWidth, Origin.y), Colour32(0xffffff, .03f));
        const auto *Selected = SelectedLayer();
        const bool LockedSel = !Selected || EffectiveLocked(*Selected);
        const size_t SelectedIndex = Selected ? size_t(Selected - Layers.data()) : 0;
        float X = Origin.x + 13 * S;
        const float Y = Origin.y + 6 * S;
        ImGui::BeginDisabled(LockedSel || !Selected || SelectedIndex == 0);
        ImGui::SetCursorScreenPos(ImVec2(X, Y));
        if (IconButton("MoveLayerUp", "M12 19V5M5 12l7-7 7 7", 28 * S)) MoveSelectedLayer(-1);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Move up");
        X += 34 * S;
        ImGui::SetCursorScreenPos(ImVec2(X, Y));
        if (IconButton("MoveLayerDown", "M12 5v14M5 12l7 7 7-7", 28 * S)) MoveSelectedLayer(1);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Move down");
        ImGui::EndDisabled();
        float RightX = Origin.x + WindowWidth - 13 * S - 28 * S;
        ImGui::BeginDisabled(LockedSel);
        ImGui::SetCursorScreenPos(ImVec2(RightX, Y));
        if (IconButton("DeleteLayer", "M3 6H21M8 6V3H16V6M6 6L7 21H17L18 6M10 10V17M14 10V17", 28 * S)) DeleteSelectedLayer();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", Selected && Selected->Kind == LayerKind::Folder ? "Delete folder and its contents" : "Delete layer");
        RightX -= 34 * S;
        ImGui::BeginDisabled(!Selected || Selected->Mask.Present);
        ImGui::SetCursorScreenPos(ImVec2(RightX, Y));
        if (IconButton("AddColourMask", "M12 21a9 9 0 1 1 9-9c0 2-1.5 3-4 3h-2a2 2 0 0 0-1 3c.3 1-1 3-2 3Z", 28 * S)) AddMaskToSelected(true);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Colour selection mask");
        ImGui::EndDisabled();
        RightX -= 34 * S;
        ImGui::BeginDisabled(!Selected || Selected->Mask.Present);
        ImGui::SetCursorScreenPos(ImVec2(RightX, Y));
        const bool MaskPressed = IconButton("AddLayerMask", "M4 4H20V20H4ZM12 4V20", 28 * S);
        if (MaskPressed) AddMaskToSelected(false);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Add white mask");
        ImGui::EndDisabled();
        RightX -= 34 * S;
        ImGui::BeginDisabled(!Selected || Layers.size() >= 64);
        ImGui::SetCursorScreenPos(ImVec2(RightX, Y));
        if (IconButton("DuplicateLayer", "M8 8H21V21H8ZM16 8V3H3V16H8", 28 * S)) DuplicateSelectedLayer();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Duplicate · Ctrl/⌘ D");
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        Track("Duplicate layer");
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 40 * S));
    }

    // .tp-system-base: locked Layer 0 row + optional info card.
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        Ink->AddLine(ImVec2(Origin.x, Origin.y), ImVec2(Origin.x + WindowWidth, Origin.y), Colour32(0xffffff, .06f));
        ImGui::SetCursorScreenPos(ImVec2(Origin.x + 10 * S, Origin.y + 5 * S));
        const bool Pressed = ImGui::InvisibleButton("SystemLayer0", ImVec2(WindowWidth - 20 * S, 24 * S));
        if (Pressed) SystemBaseOpen = !SystemBaseOpen;
        Ink->AddRectFilled(ImVec2(Origin.x + 10 * S, Origin.y + 11 * S), ImVec2(Origin.x + 22 * S, Origin.y + 23 * S), Colour32(0xffffff), 4 * S);
        DrawGlyph("M6 10H18V21H6ZM8 10V7A4 4 0 0 1 16 7V10", ImVec2(Origin.x + 29 * S, Origin.y + 11 * S), 12 * S, 0x93978d);
        Caption(ImVec2(Origin.x + 48 * S, Origin.y + 12 * S), "Layer 0 · Mesh base", 10 * S, 0x93978d);
        bool Replaced = false;
        for (const auto &Layer : Layers)
            if (Layer.Kind == LayerKind::Material && Layer.SourceColour[3] > 0.f && EffectiveVisible(Layer) && Layer.Opacity >= 99.f) Replaced = true;
        const char *AutoLabel = Replaced ? "Replaced" : "Auto";
        Caption(ImVec2(Origin.x + WindowWidth - 34 * S - CaptionWidth(AutoLabel, 9 * S), Origin.y + 12 * S), AutoLabel, 9 * S, 0xadad99);
        DrawGlyph(SystemBaseOpen ? "m6 15 6-6 6 6" : "m6 9 6 6 6-6", ImVec2(Origin.x + WindowWidth - 28 * S, Origin.y + 11 * S), 12 * S, 0x93978d);
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 34 * S));
        if (SystemBaseOpen)
        {
            const ImVec2 InfoOrigin = ImGui::GetCursorScreenPos();
            Ink->AddRectFilled(ImVec2(InfoOrigin.x + 9 * S, InfoOrigin.y), ImVec2(InfoOrigin.x + WindowWidth - 9 * S, InfoOrigin.y + 162 * S), Colour32(0x252722), 13 * S);
            Ink->AddRect(ImVec2(InfoOrigin.x + 9 * S, InfoOrigin.y), ImVec2(InfoOrigin.x + WindowWidth - 9 * S, InfoOrigin.y + 162 * S), Colour32(0xffffff, .10f), 13 * S);
            ImGui::SetCursorScreenPos(ImVec2(InfoOrigin.x + 23 * S, InfoOrigin.y + 12 * S));
            ImGui::PushTextWrapPos(InfoOrigin.x + WindowWidth - 23 * S);
            ImGui::TextColored(Spectrum(0xdeded0), "Locked · hidden from editable stack");
            ImGui::TextWrapped("%s", Replaced ? "White fallback automatically replaced by opaque materials. Masks, hidden layers or zero opacity restore it where needed."
                                              : "White fallback for Body, Lid, Handle, Spout. Masks, hidden layers or zero opacity restore it where needed.");
            ImGui::TextWrapped("%s", "No baked mesh maps attached. Bake the built-in teapot to attach AO / normal support.");
            ImGui::PopTextWrapPos();
            ImGui::SetCursorScreenPos(ImVec2(InfoOrigin.x + 23 * S, InfoOrigin.y + 118 * S));
            Controls.ControlHeight = 26;
            ImGui::BeginDisabled(!ActivateBaking);
            if (PillButton("BakeTeapotSupport", "Bake matching teapot support · 512", WindowWidth - 64 * S, true, nullptr, 0xbfc19b))
                if (ActivateBaking) ActivateBaking();
            ImGui::EndDisabled();
            ImGui::SetCursorScreenPos(ImVec2(InfoOrigin.x, InfoOrigin.y + 168 * S));
        }
    }

    // .tp-pane-footer
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        Ink->AddRectFilled(ImVec2(Origin.x, Origin.y), ImVec2(Origin.x + WindowWidth, Origin.y + 45 * S), Colour32(0x151515));
        Ink->AddLine(ImVec2(Origin.x, Origin.y), ImVec2(Origin.x + WindowWidth, Origin.y), Colour32(0xffffff, .02f));
        int Decals = 0;
        for (const auto &Layer : Layers) Decals += Layer.DecalCount;
        const auto Stat = [&](float X, const char *Label, const std::string &Value, uint32_t ValueColour)
        {
            Caption(ImVec2(Origin.x + X, Origin.y + 9 * S), Label, 7 * S, 0x5f5f58);
            Caption(ImVec2(Origin.x + X, Origin.y + 23 * S), Value.c_str(), 9 * S, ValueColour);
        };
        Stat(16 * S, "LAYERS", std::to_string(Layers.size()), 0x8eb896);
        Stat(76 * S, "VISIBLE", std::to_string(VisibleCount), 0xa6a699);
        Stat(136 * S, "CONTENT", std::to_string(Decals) + " decals", 0xa6a699);
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 45 * S));
        ImGui::Dummy(ImVec2(0, 0));
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     VIEWPORT DOCK (CENTER)
//------------------------------------------------------------------------------------------------------------------------

void TexturePaintWorkspacePanel::ConstructViewportDock()
{
    const float S = InterfaceScale;
    Controls.Scale = S;
    auto *Ink = ImGui::GetWindowDrawList();
    const float DockWidth = ImGui::GetContentRegionAvail().x;
    // 📝 useMaskEditing resets a UV-tile scope to Object outside the UV view (tile picking needs UV space).
    if (View != ViewMode::Uv && MaskEditor.Scope == RegionScope::Tile) MaskEditor.Scope = RegionScope::Object;

    const auto CompactDropdown = [&](const char *Id, const char *CurrentLabel, float Width, const std::vector<std::string> &Options, int SelectedIndex) -> int
    {
        ImGui::SetNextItemWidth(Width);
        int Chosen = -1;
        if (DropdownControlSurface::BeginSelection(Id, CurrentLabel, S))
        {
            Track(CurrentLabel);
            for (size_t Index = 0; Index < Options.size(); ++Index)
                if (DropdownControlSurface::Option(Options[Index].c_str(), int(Index) == SelectedIndex, S)) Chosen = int(Index);
            DropdownControlSurface::EndSelection();
        }
        return Chosen;
    };

    // .tp-viewport-toolbar
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        const float Height = 48 * S;
        Ink->AddRectFilled(Origin, ImVec2(Origin.x + DockWidth, Origin.y + Height), Colour32(0x151515));
        Ink->AddLine(ImVec2(Origin.x, Origin.y + Height), ImVec2(Origin.x + DockWidth, Origin.y + Height), Colour32(0xffffff, .02f));
        Ink->AddRectFilled(ImVec2(Origin.x + 14 * S, Origin.y + 9 * S), ImVec2(Origin.x + 44 * S, Origin.y + 39 * S), Colour32(0x222222), 9 * S);
        Ink->AddRect(ImVec2(Origin.x + 14 * S, Origin.y + 9 * S), ImVec2(Origin.x + 44 * S, Origin.y + 39 * S), Colour32(0xffffff, .03f), 9 * S);
        DrawGlyph(BoxIconPath, ImVec2(Origin.x + 20.5f * S, Origin.y + 15.5f * S), 17 * S, 0xaaaa9c);
        Caption(ImVec2(Origin.x + 54 * S, Origin.y + 16 * S), "Teapot", 14 * S, 0xd8d8cd, LightTypeface ? LightTypeface : RegularTypeface);
        const float BadgeX = Origin.x + 62 * S + CaptionWidth("Teapot", 14 * S, LightTypeface);
        Ink->AddRect(ImVec2(BadgeX, Origin.y + 17 * S), ImVec2(BadgeX + 46 * S, Origin.y + 31 * S), Colour32(0xffffff, .05f), 6 * S);
        Caption(ImVec2(BadgeX + 6 * S, Origin.y + 20.5f * S), "PREVIEW", 6 * S, 0x8c8776);
        float X = BadgeX + 60 * S;
        const float FieldY = Origin.y + 10 * S;
        const auto HeaderField = [&](const char *Label, float LabelWidth)
        {
            Caption(ImVec2(X, FieldY + 7 * S), Label, 8 * S, 0x747467);
            X += LabelWidth;
            ImGui::SetCursorScreenPos(ImVec2(X, FieldY));
        };
        HeaderField("SHADING", 52 * S);
        {
            const char *Labels[]{"Lit", "Wireframe", "UV / Texture"};
            const int Current = int(View);
            if (const int Chosen = CompactDropdown("##ViewportShading", Labels[Current], 118 * S, {"Lit", "Wireframe", "UV / Texture"}, Current); Chosen >= 0)
            {
                View = ViewMode(Chosen);
                if (View == ViewMode::Uv) UvFrameRequested = true;
                if (View == ViewMode::Wireframe) DigitalActive = false;
            }
            Track("Viewport shading");
            X += 128 * S;
        }
        HeaderField("TARGET", 46 * S);
        {
            std::vector<std::string> Options{"All parts"};
            for (const auto &Part : PartTable) Options.push_back(Part.Name);
            const int Current = TargetPart + 1;
            if (const int Chosen = CompactDropdown("##TextureMeshParts", Options[size_t(Current)].c_str(), 104 * S, Options, Current); Chosen >= 0)
                TargetPart = Chosen - 1;
            Track("Texture mesh parts");
            X += 114 * S;
        }
        if (View == ViewMode::Uv)
        {
            HeaderField("UV SET", 46 * S);
            const int Current = UvSource ? 1 : 0;
            const char *Labels[]{"Authoring UDIMs · 1001–1004", "Source UV0 · overlapping"};
            if (const int Chosen = CompactDropdown("##UvCoordinateSet", Labels[Current], 196 * S, {Labels[0], Labels[1]}, Current); Chosen >= 0)
            {
                UvSource = Chosen == 1;
                UvFrameRequested = true;
                if (UvSource) DigitalActive = false;
            }
            Track("UV coordinate set");
            X += 206 * S;
        }
        // .tp-viewport-end: Frame (F), grid toggle, reset (R).
        float RightX = Origin.x + DockWidth - 14 * S - 30 * S;
        ImGui::SetCursorScreenPos(ImVec2(RightX, Origin.y + 9 * S));
        if (IconButton("ResetView", "M3 12a9 9 0 1 0 3-6.7M3 4v5h5", 30 * S))
        {
            if (View == ViewMode::Uv) UvFrameRequested = true; else FrameCamera();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset view · R");
        RightX -= 35 * S;
        ImGui::SetCursorScreenPos(ImVec2(RightX, Origin.y + 9 * S));
        if (IconButton("ToggleGrid", "M3 3H10V10H3ZM14 3H21V10H14ZM3 14H10V21H3ZM14 14H21V21H14Z", 30 * S, Grid)) Grid = !Grid;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ground grid");
        Track("Toggle viewport grid");
        RightX -= 96 * S;
        ImGui::SetCursorScreenPos(ImVec2(RightX, Origin.y + 10 * S));
        if (PillButton("FrameView", "Frame F", 92 * S, false, "M15 3h6v6M9 21H3v-6M21 3l-7 7M3 21l7-7"))
        {
            if (View == ViewMode::Uv) UvFrameRequested = true; else FrameCamera();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", View == ViewMode::Uv ? "Frame selected UV tile or all tiles · F" : "Frame teapot · F");
        Track(View == ViewMode::Uv ? "Frame UVs" : "Frame teapot");
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + Height));
    }

    // .tp-authoring-bar
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        const float Height = 38 * S;
        Ink->AddRectFilled(Origin, ImVec2(Origin.x + DockWidth, Origin.y + Height), Colour32(0x151515));
        Ink->AddLine(ImVec2(Origin.x, Origin.y + Height), ImVec2(Origin.x + DockWidth, Origin.y + Height), Colour32(0xffffff, .03f));
        float X = Origin.x + 12 * S;
        const float Y = Origin.y + 6 * S;
        const auto *Selected = SelectedLayer();
        const bool LockedSel = !Selected || EffectiveLocked(*Selected);
        ImGui::SetCursorScreenPos(ImVec2(X, Y));
        if (PillButton("ToolNavigate", "Navigate", 92 * S, Tool == AuthorTool::Navigate && !DigitalActive && MaskEditor.ActiveTool != MaskEditorState::Tool::Brush,
                "M4 3L11 21L14 14L21 11Z"))
        {
            Tool = AuthorTool::Navigate;
            DigitalActive = false;
            MaskEditor.ActiveTool = MaskEditorState::Tool::Navigate;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Navigate teapot");
        X += 96 * S;
        // 🖨️ The stamp tool is always selectable: with no suitable layer, the first placed stamp
        // creates (and selects) the first paint layer instead of refusing (see PlaceStampAt).
        ImGui::SetCursorScreenPos(ImVec2(X, Y));
        if (PillButton("ToolStamp", "Stamp", 78 * S, Tool == AuthorTool::Stamp && !DigitalActive, "M5 22H19M12 2V10M8 10H16L17 18H7Z"))
        {
            Tool = AuthorTool::Stamp;
            DigitalActive = false;
            EnsureStampLibrary(true); // presets + anything saved to the project Stamps folder
        }
        Controls.Tooltip("Viewport stamp brush", "Place stamp decals from the library - click the surface to place the picked stamp on its own stamp layer, right-click the viewport for the stamp grid. Drag a stamp on the selected layer to move it. Author new stamps in the Stamp workspace (Stamp studio, right).");
        X += 82 * S;
        ImGui::SetCursorScreenPos(ImVec2(X, Y));
        if (PillButton("ToolTransform", "Transform", 102 * S, Tool == AuthorTool::Transform && !DigitalActive, "M5 9L2 12L5 15M9 5L12 2L15 5M15 19L12 22L9 19M19 9L22 12L19 15M2 12H22M12 2V22"))
        {
            Tool = AuthorTool::Transform;
            DigitalActive = false;
        }
        Controls.Tooltip("Transform stamps", "Drag a placed stamp to move it - in the 3D view it slides over the surface; in UV view drag a corner to scale and the top handle to rotate. Clicking a stamp selects its layer; the Inspector has numeric controls.");
        X += 114 * S;
        Ink->AddLine(ImVec2(X, Y + 5 * S), ImVec2(X, Y + 19 * S), Colour32(0x33332c));
        X += 9 * S;
        // DigitalPaintToolbar: Paint / Erase / Fill + the "{size} px" brush-settings button.
        const bool PaintAvailable = DigitalPaintAvailable();
        const struct { const char *Id; const char *Label; const char *Icon; } DigitalTools[3]{
            {"paint", "Paint", "M4 20L8 19L20 7L17 4L5 16ZM14 3L21 10"},
            {"erase", "Erase", "M7 21L3 17L14 6L20 12L13 19H8ZM14 6L18 2L22 6L20 8"},
            {"fill", "Fill", "M5 11L12 4L19 11L12 18ZM12 18V21M5 21H19"}};
        for (const auto &Entry : DigitalTools)
        {
            ImGui::BeginDisabled(!PaintAvailable);
            ImGui::SetCursorScreenPos(ImVec2(X, Y));
            const float Width = (Entry.Label[0] == 'P' ? 76 : Entry.Label[0] == 'E' ? 76 : 64) * S;
            if (PillButton((std::string("Digital") + Entry.Id).c_str(), Entry.Label, Width, DigitalActive && Brush.Tool == Entry.Id, Entry.Icon))
            {
                if (Brush.Tool != Entry.Id)
                {
                    Brush.Tool = Entry.Id;
                    CommitHistory("Digital tool", BrushPresetName(), "tool");
                }
                DigitalActive = true;
                if (MaskSelected) MaskEditor.ActiveTool = MaskEditorState::Tool::Brush;
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s %s", Entry.Label, MaskSelected ? "mask digitally" : "layer channels");
            X += Width + 4 * S;
        }
        char SizeLabel[24];
        std::snprintf(SizeLabel, sizeof(SizeLabel), "%d px", int(std::round(Brush.Size)));
        ImGui::SetCursorScreenPos(ImVec2(X, Y));
        if (PillButton("DigitalBrushSettings", SizeLabel, 78 * S, BrushStudioOpen, "M4 8H20M4 16H20M8 5V11M16 13V19")) BrushStudioOpen = !BrushStudioOpen;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Digital brush settings");
        Track("Digital brush settings");
        X += 92 * S;
        Ink->AddLine(ImVec2(X, Y + 5 * S), ImVec2(X, Y + 19 * S), Colour32(0x33332c));
        X += 9 * S;
        // Material / Mask preview tabs.
        ImGui::SetCursorScreenPos(ImVec2(X, Y));
        if (PillButton("PreviewMaterial", "Material", 76 * S, !MaskPreview, nullptr, 0x5a4531)) MaskPreview = false;
        X += 80 * S;
        const bool MaskTabAvailable = (MaskSelected && Selected && Selected->Mask.Present) && !(View == ViewMode::Uv && UvSource);
        ImGui::BeginDisabled(!MaskTabAvailable);
        ImGui::SetCursorScreenPos(ImVec2(X, Y));
        if (PillButton("PreviewMask", "Mask", 64 * S, MaskPreview, nullptr, 0x5a4531)) MaskPreview = true;
        ImGui::EndDisabled();
        X += 76 * S;
        // Authoring state + Stamp studio link ride the right edge.
        const auto *CurrentStamp = ActiveStamp();
        const std::string State = MaskSelected ? "Mask mode · " + (Selected ? Selected->Name : std::string())
            : Tool == AuthorTool::Stamp && !DigitalActive
                ? "Stamp mode · " + (CurrentStamp ? CurrentStamp->Name : std::string("no stamp picked"))
            : Tool == AuthorTool::Transform && !DigitalActive ? "Transform mode · drag a stamp"
            : DigitalActive ? "Paint mode · Digital " + Brush.Tool
                            : "Paint mode · Digital tools available";
        const float LinkWidth = 104 * S;
        const float StateX = Origin.x + DockWidth - 12 * S - LinkWidth - 10 * S - CaptionWidth(State.c_str(), 9 * S);
        Caption(ImVec2(std::max(X, StateX), Y + 7 * S), State.c_str(), 9 * S, 0x9a9a8c);
        Track("Texture editing mode");
        ImGui::SetCursorScreenPos(ImVec2(Origin.x + DockWidth - 12 * S - LinkWidth, Y));
        ImGui::BeginDisabled(!ActivateStamp);
        if (PillButton("OpenStampStudio", "Stamp studio", LinkWidth, false) && ActivateStamp) ActivateStamp();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Open Stamp workspace");
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + Height));
        (void)LockedSel;
    }

    // TextureMaskToolbar (only while a mask is the selection).
    if (MaskSelected && SelectedLayer() && SelectedLayer()->Mask.Present)
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        const auto *Layer = SelectedLayer();
        const bool Available = !EffectiveLocked(*Layer) && EffectiveVisible(*Layer) && View != ViewMode::Wireframe && !(View == ViewMode::Uv && UvSource);
        float Y = Origin.y + 7 * S;
        float X = Origin.x + 12 * S;
        const struct { RegionScope Scope; const char *Label; float Width; } Scopes[4]{
            {RegionScope::Face, "Face", 66}, {RegionScope::Tile, "UV Tile", 84}, {RegionScope::Island, "UV Island", 96}, {RegionScope::Object, "Object", 80}};
        for (const auto &Entry : Scopes)
        {
            ImGui::BeginDisabled(!Available || (Entry.Scope == RegionScope::Tile && View != ViewMode::Uv));
            ImGui::SetCursorScreenPos(ImVec2(X, Y));
            if (PillButton((std::string("MaskScope") + Entry.Label).c_str(), Entry.Label, Entry.Width * S,
                    MaskEditor.ActiveTool == MaskEditorState::Tool::Select && MaskEditor.Scope == Entry.Scope, "M4 3L11 21L14 14L21 11Z"))
            {
                MaskEditor.ActiveTool = MaskEditorState::Tool::Select;
                MaskEditor.Scope = Entry.Scope;
                DigitalActive = false;
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", Entry.Scope == RegionScope::Tile ? "UV tile selection is available in packed UV / Texture mode" : "Select a real region on the teapot");
            X += Entry.Width * S + 5 * S;
        }
        ImGui::BeginDisabled(!Available);
        ImGui::SetCursorScreenPos(ImVec2(X, Y));
        if (PillButton("PaintMask", "Paint mask", 104 * S, MaskEditor.ActiveTool == MaskEditorState::Tool::Brush, "M4 20L8 19L20 7L17 4L5 16ZM14 3L21 10"))
            MaskEditor.ActiveTool = MaskEditorState::Tool::Brush;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Paint on the selected mask with the current brush and ink. White reveals the layer; black conceals it.");
        X += 109 * S;
        ImGui::SetCursorScreenPos(ImVec2(X, Y));
        ImGui::BeginDisabled(!Available || !MaskEditor.SelectedRegion);
        if (PillButton("FillMaskSelection", "Fill selection", 108 * S, false)) FillMaskRegion();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Fill the selected face, UV tile, UV island or object region with the active mask ink.");
        X += 113 * S;
        ImGui::SetCursorScreenPos(ImVec2(X, Y));
        ImGui::BeginDisabled(!MaskEditor.SelectedRegion);
        if (PillButton("ClearMaskSelection", "Clear", 64 * S, false)) MaskEditor.SelectedRegion.reset();
        ImGui::EndDisabled();
        X += 69 * S;
        ImGui::SetCursorScreenPos(ImVec2(X, Y));
        if (PillButton("MaskBrushOptions", "Brush settings", 112 * S, MaskEditor.OptionsOpen)) MaskEditor.OptionsOpen = !MaskEditor.OptionsOpen;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show or hide brush settings for the current mask. This does not change the selected layer.");
        ImGui::EndDisabled();
        Y += 33 * S;
        if (MaskEditor.OptionsOpen)
        {
            X = Origin.x + 12 * S;
            ImGui::SetCursorScreenPos(ImVec2(X, Y));
            if (PillButton("MaskInkWhite", "White", 72 * S, MaskEditor.InkWhite)) MaskEditor.InkWhite = true;
            X += 77 * S;
            ImGui::SetCursorScreenPos(ImVec2(X, Y));
            if (PillButton("MaskInkBlack", "Black", 72 * S, !MaskEditor.InkWhite)) MaskEditor.InkWhite = false;
            X += 86 * S;
            const auto InlineSlider = [&](const char *Label, float &Value, float Minimum, float Maximum, const char *Format)
            {
                Caption(ImVec2(X, Y + 6 * S), Label, 9 * S, 0xa8a89a);
                X += CaptionWidth(Label, 9 * S) + 7 * S;
                ImGui::SetCursorScreenPos(ImVec2(X, Y + 2 * S));
                ImGui::SetNextItemWidth(58 * S);
                ImGui::PushID(Label);
                ImGui::SliderFloat("##MaskSlider", &Value, Minimum, Maximum, Format, ImGuiSliderFlags_NoInput);
                ImGui::PopID();
                char Out[16];
                std::snprintf(Out, sizeof(Out), Format, Value);
                Caption(ImVec2(X + 62 * S, Y + 6 * S), Out, 9 * S, 0xc8c8ba);
                X += 104 * S;
            };
            InlineSlider("Size", MaskEditor.SizePercent, .2f, 25.f, "%.1f%%");
            InlineSlider("Opacity", MaskEditor.Opacity, 0.f, 100.f, "%.0f%%");
            InlineSlider("Hardness", MaskEditor.Hardness, 0.f, 100.f, "%.0f%%");
            Y += 33 * S;
        }
        std::string RegionText = MaskRegionLabel();
        if (!MaskEditor.SelectedRegion && MaskEditor.ActiveTool == MaskEditorState::Tool::Brush) RegionText += " · Brush is scoped to the hit component";
        if (!Layer->Mask.Enabled) RegionText += " · Mask bypassed (enable to see coverage)";
        if (UvSource && View == ViewMode::Uv) RegionText += " · Source UVs are read-only";
        if (Layer->Mask.Kind == "color") RegionText += " · Source RGB per UV tile, not shaded material";
        Caption(ImVec2(Origin.x + 12 * S, Y + 2 * S), RegionText.c_str(), 9 * S, 0x93989f);
        Track("Selected mask region");
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Y + 22 * S));
    }

    // .tp-scene-viewport (12px side margins) + .tp-viewport-footer.
    const ImVec2 StageOrigin = ImGui::GetCursorScreenPos();
    const float StageHeight = std::max(80.f, ImGui::GetContentRegionAvail().y - 45 * S);
    const PlaneExtent Region = Spanning(StageOrigin.x + 12 * S, StageOrigin.y, std::max(40.f, DockWidth - 24 * S), StageHeight);
    ImGui::SetCursorScreenPos(ImVec2(Region.MinimumX, Region.MinimumY));
    if (View == ViewMode::Uv) ConstructUvViewport(Region);
    else ConstructSceneViewport(Region);
    ConstructStampPicker(); // polls the right-click popup HandlePaintPointer opens, same ID scope

    // Shared scene overlays (caption / zoom nav / axis / hint) sit above both viewport kinds.
    {
        Ink->AddCircleFilled(ImVec2(Region.MinimumX + 21.5f * S, Region.MinimumY + 25 * S), 1.5f * S, Colour32(0x959d8f), 10);
        Caption(ImVec2(Region.MinimumX + 28 * S, Region.MinimumY + 22 * S), "TEAPOT STUDY", 7 * S, 0x73786f);
        Caption(ImVec2(Region.MinimumX + 35 * S + CaptionWidth("TEAPOT STUDY", 7 * S), Region.MinimumY + 22.5f * S), "01 / DEFAULT MESH", 6 * S, 0x50574f);
        const int ZoomPercent = View == ViewMode::Uv ? int(std::round(UvScale / std::max(UvFrameScale, 1.f) * 100.f)) : int(std::round(Camera.Zoom * 100.f));
        const float NavWidth = 104 * S, NavHeight = 31 * S;
        const ImVec2 NavMin(Region.MaximumX - 18 * S - NavWidth, Region.MinimumY + 18 * S);
        Ink->AddRectFilled(NavMin, ImVec2(NavMin.x + NavWidth, NavMin.y + NavHeight), Colour32(0x161818, .77f), NavHeight * .5f);
        Ink->AddRect(NavMin, ImVec2(NavMin.x + NavWidth, NavMin.y + NavHeight), Colour32(0xffffff, .03f), NavHeight * .5f);
        ImGui::SetCursorScreenPos(ImVec2(NavMin.x + 4 * S, NavMin.y + 4 * S));
        if (IconButton("ZoomOut", "M5 12H19", 23 * S))
        {
            if (View == ViewMode::Uv)
            {
                const float CentreX = Region.Width() * .5f, CentreY = Region.Height() * .5f;
                const float NewScale = std::clamp(UvScale * .8f, 12.f, 4096.f);
                UvOffsetX = CentreX - (CentreX - UvOffsetX) * NewScale / UvScale;
                UvOffsetY = CentreY - (CentreY - UvOffsetY) * NewScale / UvScale;
                UvScale = NewScale;
            }
            else Camera.Zoom = std::clamp(Camera.Zoom * .8f, .5f, 5.f);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", View == ViewMode::Uv ? "Zoom out UVs" : "Zoom out teapot");
        char ZoomText[16];
        std::snprintf(ZoomText, sizeof(ZoomText), "%d%%", ZoomPercent);
        Caption(ImVec2(NavMin.x + (NavWidth - CaptionWidth(ZoomText, 9 * S)) * .5f, NavMin.y + 10 * S), ZoomText, 9 * S, 0xabab9e);
        ImGui::SetCursorScreenPos(ImVec2(NavMin.x + NavWidth - 27 * S, NavMin.y + 4 * S));
        if (IconButton("ZoomIn", "M12 5v14M5 12h14", 23 * S))
        {
            if (View == ViewMode::Uv)
            {
                const float CentreX = Region.Width() * .5f, CentreY = Region.Height() * .5f;
                const float NewScale = std::clamp(UvScale * 1.25f, 12.f, 4096.f);
                UvOffsetX = CentreX - (CentreX - UvOffsetX) * NewScale / UvScale;
                UvOffsetY = CentreY - (CentreY - UvOffsetY) * NewScale / UvScale;
                UvScale = NewScale;
            }
            else Camera.Zoom = std::clamp(Camera.Zoom * 1.25f, .5f, 5.f);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", View == ViewMode::Uv ? "Zoom in UVs" : "Zoom in teapot");
        Track(View == ViewMode::Uv ? "UV zoom" : "Teapot zoom");
        // .tp-view-axis: Alloy's static SVG gizmo, copied 1:1.
        const ImVec2 AxisCentre(Region.MaximumX - 53 * S, Region.MaximumY - 45 * S);
        const ImVec2 AxisY(AxisCentre.x, AxisCentre.y - 27 * S), AxisX(AxisCentre.x + 23 * S, AxisCentre.y + 11 * S), AxisZ(AxisCentre.x - 22 * S, AxisCentre.y + 13 * S);
        Ink->AddLine(AxisCentre, AxisY, Colour32(0x666666));
        Ink->AddLine(AxisCentre, AxisX, Colour32(0x666666));
        Ink->AddLine(AxisCentre, AxisZ, Colour32(0x666666));
        Ink->AddCircleFilled(AxisCentre, 3 * S, Colour32(0xbcbcb9), 12);
        const auto AxisBall = [&](ImVec2 Centre, uint32_t Fill, uint32_t Text, const char *Letter)
        {
            Ink->AddCircleFilled(Centre, 8 * S, Colour32(Fill), 20);
            Caption(ImVec2(Centre.x - CaptionWidth(Letter, 8 * S) * .5f, Centre.y - 4.5f * S), Letter, 8 * S, Text);
        };
        AxisBall(AxisY, 0x253b2d, 0x95bca1, "Y");
        AxisBall(AxisX, 0x44302e, 0xc18e85, "X");
        AxisBall(AxisZ, 0x2b3346, 0x97a7c8, "Z");
        Caption(ImVec2(Region.MinimumX + 23 * S, Region.MaximumY - 30 * S),
            DigitalActive ? "Paint the surface · Navigate to orbit" : "Drag to orbit · scroll to zoom", 8 * S, 0x656d64);
    }

    ImGui::SetCursorScreenPos(ImVec2(StageOrigin.x, StageOrigin.y + StageHeight));
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        Ink->AddCircleFilled(ImVec2(Origin.x + 21.5f * S, Origin.y + 22 * S), 2.5f * S, Colour32(0x8aa27e), 10);
        Caption(ImVec2(Origin.x + 28 * S, Origin.y + 18 * S), "Three.js · WebGL", 8 * S, 0x77786d);
        bool LiveStack = false, AnyDecals = false;
        for (const auto &Layer : Layers)
        {
            if (Layer.SourceColour[3] > 0.f || Layer.StrokeCount > 0) LiveStack = true;
            if (Layer.DecalCount > 0) AnyDecals = true;
        }
        const char *Scope = View == ViewMode::Uv ? "Ordered lit UV appearance · not exported PBR maps"
            : LiveStack ? "Layer stack live · top layers cover lower decals"
            : AnyDecals ? "Layer stack live · decals follow layer order"
                        : "Layer 0 · white fallback · digital tools available";
        Caption(ImVec2(Origin.x + DockWidth - 19 * S - CaptionWidth(Scope, 8 * S), Origin.y + 18 * S), Scope, 8 * S, 0x908c7d);
        ImGui::Dummy(ImVec2(0, 0));
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     3D SCENE VIEWPORT
//------------------------------------------------------------------------------------------------------------------------

void TexturePaintWorkspacePanel::ConstructSceneViewport(PlaneExtent Region)
{
    const float S = InterfaceScale;
    ImGui::InvisibleButton("##TextureSceneViewport", ImVec2(Region.Width(), Region.Height()));
    const bool Hovered = ImGui::IsItemHovered();
    const ImVec2 Mouse = ImGui::GetIO().MousePos;
    HandlePaintPointer(Hovered, false);
    const bool MaskBrushArmed = MaskSelected && MaskEditor.ActiveTool == MaskEditorState::Tool::Brush && !DigitalActive;
    const bool Painting = StrokeActive || (Hovered && ((DigitalActive && DigitalPaintAvailable()) || MaskBrushArmed));
    if (StrokeActive && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        float HitUv[2];
        if (const auto *Hit = PickScene(Mouse.x, Mouse.y, HitUv))
        {
            const float RadiusUv = DigitalActive ? std::clamp(0.035f * Brush.Size / 24.f, .006f, .25f) : MaskEditor.SizePercent / 100.f;
            ApplyPaintAt(HitUv, Hit->Part, Hit->Chart, Hit->Index, RadiusUv);
        }
    }
    // 👻 Hover ghost: while the Stamp tool is armed, the picked stamp rides the surface under the
    // cursor, projected live - position it by eye first, click only commits it.
    if (Hovered && !Painting)
    {
        float HoverUv[2]{0, 0};
        const auto *HoverPick = PickScene(Mouse.x, Mouse.y, HoverUv);
        RefreshHoverStamp(HoverPick && HoverPick->Index >= 0 && HoverPick->Index < int(Topology.size())
                ? &Topology[size_t(HoverPick->Index)] : nullptr, HoverUv, true);
    }
    else HoverStampActive = false;
    if (Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !Painting)
    {
        // 🧲 Pressing on a placed stamp grabs it (Transform tool: any stamp; Stamp tool: stamps on the
        // selected layer, so stamping over other layers still places). The 3D gizmo's handles (cached
        // from last frame's draw) win first - rotate grip, scale corners, body - then the surface pick
        // itself. The grab suppresses click-to-place and the orbit drag.
        bool Grabbed = false;
        if (!DigitalActive && !MaskSelected && (Tool == AuthorTool::Transform || Tool == AuthorTool::Stamp))
        {
            float GrabUv[2]{0, 0};
            const auto *Pick = PickScene(Mouse.x, Mouse.y, GrabUv);
            const TopologyTriangle *PickTri = Pick && Pick->Index >= 0 && Pick->Index < int(Topology.size())
                ? &Topology[size_t(Pick->Index)] : nullptr;
            std::string HitLayerId;
            size_t HitIndex = 0;
            int Zone = -1;
            if (StampGizmoValid && (Tool == AuthorTool::Transform || StampGizmoLayerId == SelectedLayerId))
            {
                const auto Near = [&](int Point) {
                    return std::hypot(Mouse.x - StampGizmoScreen[Point][0], Mouse.y - StampGizmoScreen[Point][1]) < 11 * S;
                };
                if (Near(4)) Zone = 2;
                else for (int Corner = 0; Corner < 4 && Zone < 0; ++Corner)
                    if (Near(Corner)) Zone = 1;
                if (Zone < 0)
                {
                    float Weights[3];
                    if (PointInTriangle(Mouse.x, Mouse.y, StampGizmoScreen[0], StampGizmoScreen[1], StampGizmoScreen[2], Weights) ||
                        PointInTriangle(Mouse.x, Mouse.y, StampGizmoScreen[0], StampGizmoScreen[2], StampGizmoScreen[3], Weights))
                        Zone = 0;
                }
                if (Zone >= 0) { HitLayerId = StampGizmoLayerId; HitIndex = StampGizmoIndex; }
            }
            if (Zone < 0 && PickTri)
            {
                int UvZone = 0;
                if (HitTestStamp(GrabUv, 0.f, HitLayerId, HitIndex, UvZone) &&
                    (Tool == AuthorTool::Transform || HitLayerId == SelectedLayerId))
                    Zone = 0;
            }
            if (Zone >= 0)
            {
                BeginStampDragScene(HitLayerId, HitIndex, Zone, PickTri, GrabUv, Mouse.x, Mouse.y);
                Grabbed = true;
            }
        }
        if (!Grabbed)
        {
            PointerDownValid = true;
            PointerDownX = Mouse.x;
            PointerDownY = Mouse.y;
        }
    }
    if (StampDrag.Active)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            float DragUv[2]{0, 0};
            const auto *Pick = PickScene(Mouse.x, Mouse.y, DragUv);
            const TopologyTriangle *PickTri = Pick && Pick->Index >= 0 && Pick->Index < int(Topology.size())
                ? &Topology[size_t(Pick->Index)] : nullptr;
            if (StampDrag.SceneGesture) DragStampScene(PickTri, DragUv, Mouse.x, Mouse.y);
            else if (PickTri) UpdateStampDrag(DragUv); // off-mesh: keep last spot
        }
        else FinishStampDrag();
    }
    if (PointerDownValid && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        PointerDownValid = false;
        const float Moved = std::hypot(Mouse.x - PointerDownX, Mouse.y - PointerDownY);
        float HitUv[2];
        if (Moved < 4 * S && Hovered)
            if (const auto *Hit = PickScene(Mouse.x, Mouse.y, HitUv))
            {
                if (MaskSelected && MaskEditor.ActiveTool == MaskEditorState::Tool::Select)
                    MaskEditor.SelectedRegion = MaskEditorState::Region{MaskEditor.Scope, Hit->Part, Hit->Chart, Hit->Index};
                else if (Tool == AuthorTool::Stamp && !DigitalActive)
                {
                    // a click (not an orbit drag) drops the picked stamp here, projected from this view
                    PendingAnchorTriangle = Hit->Index >= 0 && Hit->Index < int(Topology.size())
                        ? &Topology[size_t(Hit->Index)] : nullptr;
                    PendingAnchorCamera = true;
                    PlaceStampAt(HitUv);
                }
                else if (!DigitalActive) TargetPart = Hit->Part;
            }
    }
    // 📝 Same orbit gesture mapping as Material/Baking (drag = yaw/pitch, wheel = Zoom, shared clamps);
    // middle/right drag pans. Painting owns the left button while a stroke is armed.
    if (Hovered && !Painting && !StampDrag.Active && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        const ImVec2 Delta = ImGui::GetIO().MouseDelta;
        Camera.Yaw = std::remainder(Camera.Yaw + Delta.x * 0.01f, 6.2831853f);
        Camera.Pitch = std::clamp(Camera.Pitch - Delta.y * 0.01f, -1.45f, 1.45f);
    }
    if (Hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right)))
    {
        const ImVec2 Delta = ImGui::GetIO().MouseDelta;
        Camera.PanX = std::clamp(Camera.PanX + Delta.x * 0.002f, -2.f, 2.f);
        Camera.PanY = std::clamp(Camera.PanY - Delta.y * 0.002f, -2.f, 2.f);
    }
    if (Hovered) Camera.Zoom = std::clamp(Camera.Zoom + ImGui::GetIO().MouseWheel * 0.1f, .5f, 5.f);
    if (Hovered && !ImGui::GetIO().WantTextInput)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_F) || ImGui::IsKeyPressed(ImGuiKey_R)) FrameCamera();
    }
    SynchronizeCamera();

    auto *DrawList = ImGui::GetWindowDrawList();
    DrawList->PushClipRect(ImVec2(Region.MinimumX, Region.MinimumY), ImVec2(Region.MaximumX, Region.MaximumY), true);
    // 🎨 Clear flat viewport field (no decorative rings - the teapot and grid carry the stage).
    DrawList->AddRectFilled(ImVec2(Region.MinimumX, Region.MinimumY), ImVec2(Region.MaximumX, Region.MaximumY), Colour32(0x111212));

    std::vector<PickTriangle> Picks;
    if (!Assets.Low.Parts.empty() && !Topology.empty())
    {
        const auto &Part = Assets.Low.Parts.front();
        float BoundsMinimum[3]{FLT_MAX, FLT_MAX, FLT_MAX}, BoundsMaximum[3]{-FLT_MAX, -FLT_MAX, -FLT_MAX};
        for (const auto &Vertex : Part.Vertices)
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                BoundsMinimum[Axis] = std::min(BoundsMinimum[Axis], Vertex.Position[Axis]);
                BoundsMaximum[Axis] = std::max(BoundsMaximum[Axis], Vertex.Position[Axis]);
            }
        float Pivot[3]{(BoundsMinimum[0] + BoundsMaximum[0]) * .5f, (BoundsMinimum[1] + BoundsMaximum[1]) * .5f,
            (BoundsMinimum[2] + BoundsMaximum[2]) * .5f};
        const float Extent[3]{BoundsMaximum[0] - Pivot[0], BoundsMaximum[1] - Pivot[1], BoundsMaximum[2] - Pivot[2]};
        const float Radius = std::max(.05f, std::sqrt(Extent[0] * Extent[0] + Extent[1] * Extent[1] + Extent[2] * Extent[2]));
        // 📝 Alloy's Texture scene frames the teapot closer than the material capture rig Baking uses
        // (5.8/1.35): at 3.1x the bounding sphere the pot fills roughly half the viewport height.
        const auto Orbit = ConstructOrbitProjection(SmoothedCamera.Yaw, SmoothedCamera.Pitch, SmoothedCamera.Zoom, SmoothedCamera.PanX,
            SmoothedCamera.PanY, Radius, Pivot, 3.1f);
        const Vec3 Eye{Orbit.Eye[0], Orbit.Eye[1], Orbit.Eye[2]};
        const Vec3 Forward{Orbit.Forward[0], Orbit.Forward[1], Orbit.Forward[2]};
        const Vec3 Right{Orbit.Right[0], Orbit.Right[1], Orbit.Right[2]};
        const Vec3 Up{Orbit.Up[0], Orbit.Up[1], Orbit.Up[2]};
        SceneRight[0] = Right.X; SceneRight[1] = Right.Y; SceneRight[2] = Right.Z;
        SceneUp[0] = Up.X; SceneUp[1] = Up.Y; SceneUp[2] = Up.Z;
        SceneFrameValid = true; // camera-aligned stamp anchors read this between interaction and draw
        const float CenterX = (Region.MinimumX + Region.MaximumX) * .5f, CenterY = (Region.MinimumY + Region.MaximumY) * .5f;
        const float FocalLength = std::min(Region.Width(), Region.Height()) * .8f;
        const Vec3 Light = NormalizeV(Vec3{.4f, .8f, .5f});
        const auto Project = [&](const float P[3], ImVec2 &Screen, float &Depth) noexcept
        {
            const Vec3 World = Sub(Vec3{P[0], P[1], P[2]}, Eye);
            const float Cx = DotV(World, Right), Cy = DotV(World, Up), Cz = DotV(World, Forward);
            Depth = Cz;
            const float Z = std::max(Cz, .05f);
            Screen = ImVec2(CenterX + Cx * FocalLength / Z, CenterY - Cy * FocalLength / Z);
        };
        if (Grid)
        {
            // 🌐 Ground grid at the mesh's base plane (Alloy's gridVisible), projected through the same camera.
            const float GroundY = BoundsMinimum[1];
            const float Half = Radius * 1.7f, Step = Radius * .425f;
            for (float At = -Half; At <= Half + 1e-4f; At += Step)
            {
                const float A[3]{Pivot[0] + At, GroundY, Pivot[2] - Half}, B[3]{Pivot[0] + At, GroundY, Pivot[2] + Half};
                const float C[3]{Pivot[0] - Half, GroundY, Pivot[2] + At}, D[3]{Pivot[0] + Half, GroundY, Pivot[2] + At};
                ImVec2 Sa, Sb, Sc, Sd;
                float Da, Db, Dc, Dd;
                Project(A, Sa, Da); Project(B, Sb, Db); Project(C, Sc, Dc); Project(D, Sd, Dd);
                if (Da > .05f && Db > .05f) DrawList->AddLine(Sa, Sb, Colour32(0xffffff, .045f), 1.f);
                if (Dc > .05f && Dd > .05f) DrawList->AddLine(Sc, Sd, Colour32(0xffffff, .045f), 1.f);
            }
        }
        EnsurePreviewCanvas(); // recomposites the UV canvas only when the document actually changed
        const bool Wire = View == ViewMode::Wireframe;
        const bool Textured = !Wire && PreviewCanvasTextured();
        struct DrawTriangle { ImVec2 A, B, C; ImU32 Colour; float Depth; int Part; float Uv[3][2]; };
        std::vector<DrawTriangle> Triangles;
        Triangles.reserve(Topology.size());
        Picks.reserve(Topology.size());
        for (const auto &Triangle : Topology)
        {
            const auto &VA = Part.Vertices[Triangle.VertexIndex[0]];
            const auto &VB = Part.Vertices[Triangle.VertexIndex[1]];
            const auto &VC = Part.Vertices[Triangle.VertexIndex[2]];
            const Vec3 FaceNormal = NormalizeV(CrossV(Sub(Vec3{VB.Position[0], VB.Position[1], VB.Position[2]}, Vec3{VA.Position[0], VA.Position[1], VA.Position[2]}),
                Sub(Vec3{VC.Position[0], VC.Position[1], VC.Position[2]}, Vec3{VA.Position[0], VA.Position[1], VA.Position[2]})));
            const bool Front = DotV(FaceNormal, Forward) <= -0.02f;
            if (!Wire && !Front) continue;
            ImVec2 Sa, Sb, Sc;
            float Da, Db, Dc;
            Project(VA.Position, Sa, Da); Project(VB.Position, Sb, Db); Project(VC.Position, Sc, Dc);
            if (Da <= .05f || Db <= .05f || Dc <= .05f) continue;
            PickTriangle Pick;
            Pick.Screen[0][0] = Sa.x; Pick.Screen[0][1] = Sa.y;
            Pick.Screen[1][0] = Sb.x; Pick.Screen[1][1] = Sb.y;
            Pick.Screen[2][0] = Sc.x; Pick.Screen[2][1] = Sc.y;
            for (int Corner = 0; Corner < 3; ++Corner)
            {
                Pick.Uv[Corner][0] = Triangle.AuthoringUv[Corner][0];
                Pick.Uv[Corner][1] = Triangle.AuthoringUv[Corner][1];
            }
            Pick.Depth = (Da + Db + Dc) / 3.f;
            Pick.Part = Triangle.Part;
            Pick.Chart = Triangle.Chart;
            Pick.Index = Triangle.Index;
            if (Front) Picks.push_back(Pick);
            // 🎨 Flat Lambert shade over the preview composite (same look family as Baking's CPU viewport).
            // Textured path: the UV canvas carries the composite per texel, so the vertex colour is
            // only the shade multiplier (the shader multiplies texture x vertex colour) and no
            // per-triangle evaluation runs. Fallback keeps the centroid composite.
            const float Shade = std::clamp(.30f + .70f * std::max(0.f, DotV(FaceNormal, Light)), 0.f, 1.f);
            ImU32 Packed;
            if (Textured)
            {
                float Dim = Shade;
                if (TargetPart >= 0 && Triangle.Part != TargetPart) Dim *= .82f;
                const uint8_t Grey = uint8_t(std::clamp(Dim, 0.f, 1.f) * 255.f);
                Packed = IM_COL32(Grey, Grey, Grey, 255);
            }
            else
            {
                std::array<float, 3> Colour = EvaluatePreviewColour(Triangle);
                if (TargetPart >= 0)
                {
                    if (Triangle.Part == TargetPart)
                    {
                        const uint32_t Tint = PartTable[size_t(Triangle.Part)].Colour;
                        Colour[0] += (float((Tint >> 16) & 0xff) / 255.f - Colour[0]) * .22f;
                        Colour[1] += (float((Tint >> 8) & 0xff) / 255.f - Colour[1]) * .22f;
                        Colour[2] += (float(Tint & 0xff) / 255.f - Colour[2]) * .22f;
                    }
                    else for (float &Channel : Colour) Channel *= .82f;
                }
                Packed = IM_COL32(uint8_t(std::clamp(Colour[0] * Shade, 0.f, 1.f) * 255.f),
                    uint8_t(std::clamp(Colour[1] * Shade, 0.f, 1.f) * 255.f), uint8_t(std::clamp(Colour[2] * Shade, 0.f, 1.f) * 255.f), 255);
            }
            DrawTriangle Drawn{Sa, Sb, Sc, Packed, Pick.Depth, Triangle.Part, {}};
            for (int Corner = 0; Corner < 3; ++Corner)
            {
                Drawn.Uv[Corner][0] = Triangle.AuthoringUv[Corner][0];
                Drawn.Uv[Corner][1] = Triangle.AuthoringUv[Corner][1];
            }
            Triangles.push_back(Drawn);
        }
        std::sort(Triangles.begin(), Triangles.end(), [](const DrawTriangle &A, const DrawTriangle &B) { return A.Depth > B.Depth; });
        if (Textured) DrawList->PushTexture(ImTextureRef(ImTextureID(QueryPaintCanvasImage().Texture)));
        for (const auto &Triangle : Triangles)
        {
            if (!Wire)
            {
                DrawList->PrimReserve(3, 3);
                ImDrawVert *Vertex = DrawList->_VtxWritePtr;
                ImDrawIdx *IndexCursor = DrawList->_IdxWritePtr;
                ImDrawIdx Base = DrawList->_VtxCurrentIdx;
                const ImVec2 Corners[3]{Triangle.A, Triangle.B, Triangle.C};
                for (int Corner = 0; Corner < 3; ++Corner)
                {
                    Vertex->pos = Corners[Corner];
                    if (Textured)
                    {
                        const auto Cuv = PreviewCanvasUv(Triangle.Part, Triangle.Uv[Corner][0], Triangle.Uv[Corner][1]);
                        Vertex->uv = ImVec2(Cuv[0], Cuv[1]);
                    }
                    else Vertex->uv = DrawList->_Data->TexUvWhitePixel;
                    Vertex->col = Triangle.Colour;
                    ++Vertex;
                    *IndexCursor++ = Base++;
                }
                DrawList->_VtxWritePtr = Vertex;
                DrawList->_IdxWritePtr = IndexCursor;
                DrawList->_VtxCurrentIdx = Base;
            }
            else
            {
                const uint32_t WireColour = PartTable[size_t(std::clamp(Triangle.Part, 0, 3))].Colour;
                DrawList->AddTriangle(Triangle.A, Triangle.B, Triangle.C, Colour32(WireColour, TargetPart == Triangle.Part ? .85f : .4f), 1.f);
            }
        }
        if (Textured) DrawList->PopTexture();
        // 🧲 3D stamp gizmo: the focused stamp's projector quad floating a hair off the surface, with
        // scale corners and a rotate grip - cached in screen space for next frame's hit-testing.
        StampGizmoValid = false;
        if (!Wire && !MaskSelected && (Tool == AuthorTool::Transform || Tool == AuthorTool::Stamp || StampDrag.Active))
        {
            LayerState *FocusLayer = nullptr;
            const StampPlacement *Focus = FocusedStamp(FocusLayer);
            if (Focus && Focus->Projected && FocusLayer && EffectiveVisible(*FocusLayer))
            {
                const float R = Focus->WorldRadius, Lift = R * 0.02f;
                bool Visible = true;
                const auto GizmoPoint = [&](int Index, float Lx, float Ly)
                {
                    float P[3];
                    for (int Axis = 0; Axis < 3; ++Axis)
                        P[Axis] = Focus->Origin[Axis] + Focus->AxisRight[Axis] * Lx + Focus->AxisUp[Axis] * Ly + Focus->Normal[Axis] * Lift;
                    ImVec2 Screen;
                    float Depth;
                    Project(P, Screen, Depth);
                    StampGizmoScreen[Index][0] = Screen.x;
                    StampGizmoScreen[Index][1] = Screen.y;
                    if (Depth <= .05f) Visible = false;
                    return Screen;
                };
                const ImVec2 Corners[4]{GizmoPoint(0, -R, -R), GizmoPoint(1, R, -R), GizmoPoint(2, R, R), GizmoPoint(3, -R, R)};
                const ImVec2 Grip = GizmoPoint(4, 0, R * 1.35f);
                const ImVec2 Centre = GizmoPoint(5, 0, 0);
                const ImVec2 TopMid = ImVec2((Corners[2].x + Corners[3].x) * .5f, (Corners[2].y + Corners[3].y) * .5f);
                if (Visible)
                {
                    StampGizmoValid = true;
                    StampGizmoLayerId = FocusLayer->Id;
                    StampGizmoIndex = size_t(Focus - FocusLayer->Stamps.data());
                    DrawList->AddPolyline(Corners, 4, Colour32(0x000000, .55f), ImDrawFlags_Closed, 3.f * S);
                    DrawList->AddPolyline(Corners, 4, Colour32(0xedc391, .95f), ImDrawFlags_Closed, 1.3f * S);
                    DrawList->AddLine(TopMid, Grip, Colour32(0xedc391, .8f), 1.2f * S);
                    DrawList->AddCircleFilled(Grip, 5.5f * S, Colour32(0x111210), 16);
                    DrawList->AddCircle(Grip, 5.5f * S, Colour32(0xedc391, .95f), 16, 1.4f * S);
                    for (const ImVec2 &Corner : Corners)
                    {
                        const float Half = 4.5f * S;
                        DrawList->AddRectFilled(ImVec2(Corner.x - Half, Corner.y - Half), ImVec2(Corner.x + Half, Corner.y + Half), Colour32(0x111210), 1.5f * S);
                        DrawList->AddRect(ImVec2(Corner.x - Half, Corner.y - Half), ImVec2(Corner.x + Half, Corner.y + Half), Colour32(0xedc391, .95f), 1.5f * S, 0, 1.3f * S);
                    }
                    DrawList->AddLine(ImVec2(Centre.x - 6 * S, Centre.y), ImVec2(Centre.x + 6 * S, Centre.y), Colour32(0xedc391, .9f), 1.2f * S);
                    DrawList->AddLine(ImVec2(Centre.x, Centre.y - 6 * S), ImVec2(Centre.x, Centre.y + 6 * S), Colour32(0xedc391, .9f), 1.2f * S);
                }
            }
        }
    }
    else Caption(ImVec2(Region.MinimumX + 20 * S, Region.MinimumY + 40 * S), "Preparing teapot…", 10 * S, 0x98998e);
    LastPickTriangles = std::move(Picks);
    if (Hovered && Painting)
    {
        const float CursorRadius = DigitalActive ? Brush.Size * S * .5f : MaskEditor.SizePercent * Region.Height() * .005f;
        if (Brush.Shape == "square" && DigitalActive)
            DrawList->AddRect(ImVec2(Mouse.x - CursorRadius, Mouse.y - CursorRadius), ImVec2(Mouse.x + CursorRadius, Mouse.y + CursorRadius), Colour32(0xedc391, .85f), 2 * S);
        else DrawList->AddCircle(Mouse, std::max(CursorRadius, 2.f), Colour32(0xedc391, .85f), 32, 1.2f * S);
    }
    if (MaskSelected && MaskEditor.SelectedRegion && View != ViewMode::Uv)
        Caption(ImVec2(Region.MinimumX + 23 * S, Region.MinimumY + 44 * S), MaskRegionLabel().c_str(), 8 * S, 0x9b927f);
    DrawList->PopClipRect();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     UV / UDIM VIEWPORT
//------------------------------------------------------------------------------------------------------------------------

void TexturePaintWorkspacePanel::ConstructUvViewport(PlaneExtent Region)
{
    const float S = InterfaceScale;
    ImGui::InvisibleButton("##TextureUvViewport", ImVec2(Region.Width(), Region.Height()));
    const bool Hovered = ImGui::IsItemHovered();
    const ImVec2 Mouse = ImGui::GetIO().MousePos;
    if (UvFrameRequested || UvScale <= 0.f) FrameUv(Region.Width(), Region.Height());
    const auto UvToScreen = [&](float U, float V)
    {
        return ImVec2(Region.MinimumX + UvOffsetX + U * UvScale, Region.MinimumY + UvOffsetY + (1.f - V) * UvScale);
    };
    const auto ScreenToUv = [&](float X, float Y, float Out[2])
    {
        Out[0] = (X - Region.MinimumX - UvOffsetX) / UvScale;
        Out[1] = 1.f - (Y - Region.MinimumY - UvOffsetY) / UvScale;
    };
    const auto HitUv = [&](const float Point[2], bool Authoring) -> const TopologyTriangle *
    {
        float Weights[3];
        for (const auto &Triangle : Topology)
        {
            const float (*Coords)[2] = Authoring ? Triangle.AuthoringUv : Triangle.SourceUv;
            if (PointInTriangle(Point[0], Point[1], Coords[0], Coords[1], Coords[2], Weights)) return &Triangle;
        }
        return nullptr;
    };
    HandlePaintPointer(Hovered, true);
    const bool MaskBrushArmed = MaskSelected && MaskEditor.ActiveTool == MaskEditorState::Tool::Brush && !DigitalActive;
    const bool Painting = StrokeActive || (Hovered && ((DigitalActive && DigitalPaintAvailable()) || MaskBrushArmed) && !UvSource);
    // 👻 Hover ghost in UV space too: the stamp previews on the canvas under the cursor, projected
    // onto the surface (watch it stay clean across island borders before committing with a click).
    if (Hovered && !Painting && !UvSource)
    {
        float GhostPoint[2];
        ScreenToUv(Mouse.x, Mouse.y, GhostPoint);
        RefreshHoverStamp(HitUv(GhostPoint, true), GhostPoint, false);
    }
    else HoverStampActive = false;
    if (Hovered && ImGui::GetIO().MouseWheel != 0.f)
    {
        float Before[2];
        ScreenToUv(Mouse.x, Mouse.y, Before);
        UvScale = std::clamp(UvScale * std::exp(ImGui::GetIO().MouseWheel * .12f), 12.f, 4096.f);
        UvOffsetX = (Mouse.x - Region.MinimumX) - Before[0] * UvScale;
        UvOffsetY = (Mouse.y - Region.MinimumY) - (1.f - Before[1]) * UvScale;
    }
    if (Hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || (ImGui::GetIO().KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left))))
    {
        UvOffsetX += ImGui::GetIO().MouseDelta.x;
        UvOffsetY += ImGui::GetIO().MouseDelta.y;
    }
    if (StrokeActive && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !UvSource)
    {
        float Point[2];
        ScreenToUv(Mouse.x, Mouse.y, Point);
        if (const auto *Hit = HitUv(Point, true))
        {
            const float RadiusUv = DigitalActive ? (Brush.Size * S * .5f) / UvScale : MaskEditor.SizePercent / 100.f;
            ApplyPaintAt(Point, Hit->Part, Hit->Chart, Hit->Index, RadiusUv);
        }
    }
    if (Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !Painting && !ImGui::GetIO().KeyAlt)
    {
        // 🧲 Same grab rule as the 3D view (Transform: any stamp; Stamp tool: selected layer's stamps),
        // with handle zones: body moves, a corner scales, the handle above the top edge rotates.
        bool Grabbed = false;
        if (!UvSource && !DigitalActive && !MaskSelected && (Tool == AuthorTool::Transform || Tool == AuthorTool::Stamp))
        {
            float GrabUv[2];
            ScreenToUv(Mouse.x, Mouse.y, GrabUv);
            std::string HitLayerId;
            size_t HitIndex = 0;
            int HitZone = 0;
            if (HitTestStamp(GrabUv, 9 * S / std::max(UvScale, 1.f), HitLayerId, HitIndex, HitZone) &&
                (Tool == AuthorTool::Transform || HitLayerId == SelectedLayerId))
            {
                BeginStampDrag(HitLayerId, HitIndex, HitZone, GrabUv);
                Grabbed = true;
            }
        }
        if (!Grabbed)
        {
            PointerDownValid = true;
            PointerDownX = Mouse.x;
            PointerDownY = Mouse.y;
        }
    }
    if (StampDrag.Active)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            float DragUv[2];
            ScreenToUv(Mouse.x, Mouse.y, DragUv);
            UpdateStampDrag(DragUv);
        }
        else FinishStampDrag();
    }
    if (PointerDownValid && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        PointerDownValid = false;
        if (Hovered && std::hypot(Mouse.x - PointerDownX, Mouse.y - PointerDownY) < 4 * S)
        {
            float Point[2];
            ScreenToUv(Mouse.x, Mouse.y, Point);
            if (const auto *Hit = HitUv(Point, !UvSource))
            {
                if (MaskSelected && MaskEditor.ActiveTool == MaskEditorState::Tool::Select && !UvSource)
                    MaskEditor.SelectedRegion = MaskEditorState::Region{MaskEditor.Scope, Hit->Part, Hit->Chart, Hit->Index};
                else if (Tool == AuthorTool::Stamp && !DigitalActive)
                {
                    // Placements live in authoring-UV space, same as dabs - source-UV view is inspect-only.
                    if (UvSource) Status = "Switch from source UVs to authoring UVs to place stamps";
                    else
                    {
                        PendingAnchorTriangle = Hit; // born projected, aligned to this chart's UV tangents
                        PendingAnchorCamera = false;
                        PlaceStampAt(Point);
                    }
                }
                else if (!DigitalActive) TargetPart = Hit->Part;
            }
        }
    }
    if (Hovered && !ImGui::GetIO().WantTextInput && (ImGui::IsKeyPressed(ImGuiKey_F) || ImGui::IsKeyPressed(ImGuiKey_R)))
        FrameUv(Region.Width(), Region.Height());

    EnsurePreviewCanvas();
    const bool Textured = !UvSource && PreviewCanvasTextured();
    const uint64_t CanvasTexture = Textured ? QueryPaintCanvasImage().Texture : 0;
    auto *DrawList = ImGui::GetWindowDrawList();
    DrawList->PushClipRect(ImVec2(Region.MinimumX, Region.MinimumY), ImVec2(Region.MaximumX, Region.MaximumY), true);
    DrawList->AddRectFilled(ImVec2(Region.MinimumX, Region.MinimumY), ImVec2(Region.MaximumX, Region.MaximumY), Colour32(0x141719));
    const int TileCount = UvSource ? 1 : 4;
    for (int Tile = 0; Tile < TileCount; ++Tile)
    {
        const ImVec2 TopLeft = UvToScreen(float(Tile), 1.f), BottomRight = UvToScreen(float(Tile) + 1.f, 0.f);
        DrawList->AddRectFilled(TopLeft, BottomRight, Colour32(0x1d2226));
        if (Textured)
        {
            const auto UvMin = PreviewCanvasUv(Tile, float(Tile), 1.f), UvMax = PreviewCanvasUv(Tile, float(Tile) + 1.f, 0.f);
            DrawList->AddImage(ImTextureRef(ImTextureID(CanvasTexture)), TopLeft, BottomRight,
                ImVec2(UvMin[0], UvMin[1]), ImVec2(UvMax[0], UvMax[1]));
        }
        if (Grid)
            for (int Step = 1; Step < 10; ++Step)
            {
                const float X = TopLeft.x + (BottomRight.x - TopLeft.x) * Step / 10.f;
                const float Y = TopLeft.y + (BottomRight.y - TopLeft.y) * Step / 10.f;
                DrawList->AddLine(ImVec2(X, TopLeft.y), ImVec2(X, BottomRight.y), Colour32(0xffffff, .03f), 1.f);
                DrawList->AddLine(ImVec2(TopLeft.x, Y), ImVec2(BottomRight.x, Y), Colour32(0xffffff, .03f), 1.f);
            }
        DrawList->AddRect(TopLeft, BottomRight, Colour32(0x5b626b), 0.f, 0, 1.f);
        const std::string TileLabel = UvSource ? "1001 · SOURCE UV0" : std::to_string(PartTable[size_t(Tile)].Tile) + " · " + PartTable[size_t(Tile)].Name;
        Caption(ImVec2(TopLeft.x + 7 * S, BottomRight.y - 18 * S), TileLabel.c_str(), 8 * S, 0x6d7680);
    }
    for (const auto &Triangle : Topology)
    {
        ImVec2 Points[3];
        float MinX = FLT_MAX, MinY = FLT_MAX, MaxX = -FLT_MAX, MaxY = -FLT_MAX;
        for (int Corner = 0; Corner < 3; ++Corner)
        {
            const float *Coords = UvSource ? Triangle.SourceUv[Corner] : Triangle.AuthoringUv[Corner];
            Points[Corner] = UvToScreen(Coords[0], Coords[1]);
            MinX = std::min(MinX, Points[Corner].x); MaxX = std::max(MaxX, Points[Corner].x);
            MinY = std::min(MinY, Points[Corner].y); MaxY = std::max(MaxY, Points[Corner].y);
        }
        if (MaxX < Region.MinimumX || MinX > Region.MaximumX || MaxY < Region.MinimumY || MinY > Region.MaximumY) continue;
        if (!Textured)
        {
            const std::array<float, 3> Colour = EvaluatePreviewColour(Triangle);
            const ImU32 Fill = IM_COL32(uint8_t(Colour[0] * 255), uint8_t(Colour[1] * 255), uint8_t(Colour[2] * 255), 210);
            DrawList->AddTriangleFilled(Points[0], Points[1], Points[2], Fill);
        }
        const uint32_t PartColour = PartTable[size_t(std::clamp(Triangle.Part, 0, 3))].Colour;
        const bool Emphasised = TargetPart == Triangle.Part;
        DrawList->AddTriangle(Points[0], Points[1], Points[2], Colour32(PartColour, Emphasised ? .66f : .33f), Emphasised ? 1.1f : .7f);
        if (MaskEditor.SelectedRegion && MaskSelected)
        {
            const auto &Selected = *MaskEditor.SelectedRegion;
            const bool Matches = (Selected.Scope == RegionScope::Face && Selected.Triangle == Triangle.Index) ||
                (Selected.Scope == RegionScope::Island && Selected.Chart == Triangle.Chart) ||
                ((Selected.Scope == RegionScope::Object || Selected.Scope == RegionScope::Tile) && Selected.Part == Triangle.Part);
            if (Matches) DrawList->AddTriangle(Points[0], Points[1], Points[2], Colour32(0xedc391, .8f), 1.1f);
        }
    }
    if (!UvSource && !Textured)
    {
        // 🎨 Dab footprints, bottom-to-top like the composite they preview (fallback only - the
        // textured canvas already contains the real falloff composite of every dab).
        for (auto Iterator = Layers.rbegin(); Iterator != Layers.rend(); ++Iterator)
        {
            if (!EffectiveVisible(*Iterator)) continue;
            const auto &Dabs = MaskPreview && Iterator->Id == SelectedLayerId ? Iterator->Mask.Dabs : Iterator->Dabs;
            for (const auto &Dab : Dabs)
            {
                const ImVec2 Centre = UvToScreen(Dab.U, Dab.V);
                const float Radius = Dab.Radius * UvScale;
                if (Centre.x + Radius < Region.MinimumX || Centre.x - Radius > Region.MaximumX || Centre.y + Radius < Region.MinimumY || Centre.y - Radius > Region.MaximumY) continue;
                DrawList->AddCircleFilled(Centre, std::max(Radius, 1.f),
                    IM_COL32(uint8_t(Dab.Colour[0] * 255), uint8_t(Dab.Colour[1] * 255), uint8_t(Dab.Colour[2] * 255), uint8_t(std::clamp(Dab.Colour[3], 0.f, 1.f) * 200)), 20);
            }
        }
    }
    if (!UvSource && !MaskSelected && (Tool == AuthorTool::Transform || Tool == AuthorTool::Stamp || StampDrag.Active))
    {
        // 🧲 Stamp gizmo: every stamp on the selected layer gets an outline; the focused stamp gets the
        // full handle set (corners scale, the stalked circle above the top edge rotates, body moves).
        LayerState *FocusLayer = nullptr;
        const StampPlacement *Focus = FocusedStamp(FocusLayer);
        const auto DrawStampQuad = [&](const StampPlacement &Placement, bool WithHandles)
        {
            const float R = Placement.Radius;
            const float CosR = std::cos(Placement.Rotation), SinR = std::sin(Placement.Rotation);
            const auto At = [&](float Lx, float Ly)
            {
                return UvToScreen(Placement.U + Lx * CosR - Ly * SinR, Placement.V + Lx * SinR + Ly * CosR);
            };
            const ImVec2 Corners[4]{At(-R, -R), At(R, -R), At(R, R), At(-R, R)};
            DrawList->AddPolyline(Corners, 4, Colour32(0x000000, .55f), ImDrawFlags_Closed, 3.f * S);
            DrawList->AddPolyline(Corners, 4, Colour32(0xedc391, WithHandles ? .95f : .45f), ImDrawFlags_Closed, 1.3f * S);
            if (!WithHandles) return;
            const ImVec2 Centre = At(0, 0), TopMid = At(0, R), Grip = At(0, R * 1.35f);
            DrawList->AddLine(TopMid, Grip, Colour32(0xedc391, .8f), 1.2f * S);
            DrawList->AddCircleFilled(Grip, 5.5f * S, Colour32(0x111210), 16);
            DrawList->AddCircle(Grip, 5.5f * S, Colour32(0xedc391, .95f), 16, 1.4f * S);
            for (const ImVec2 &Corner : Corners)
            {
                const float Half = 4.5f * S;
                DrawList->AddRectFilled(ImVec2(Corner.x - Half, Corner.y - Half), ImVec2(Corner.x + Half, Corner.y + Half), Colour32(0x111210), 1.5f * S);
                DrawList->AddRect(ImVec2(Corner.x - Half, Corner.y - Half), ImVec2(Corner.x + Half, Corner.y + Half), Colour32(0xedc391, .95f), 1.5f * S, 0, 1.3f * S);
            }
            DrawList->AddLine(ImVec2(Centre.x - 6 * S, Centre.y), ImVec2(Centre.x + 6 * S, Centre.y), Colour32(0xedc391, .9f), 1.2f * S);
            DrawList->AddLine(ImVec2(Centre.x, Centre.y - 6 * S), ImVec2(Centre.x, Centre.y + 6 * S), Colour32(0xedc391, .9f), 1.2f * S);
        };
        if (const auto *Selected = SelectedLayer(); Selected && EffectiveVisible(*Selected))
            for (const auto &Placement : Selected->Stamps)
                if (&Placement != Focus) DrawStampQuad(Placement, false);
        if (Focus && FocusLayer && EffectiveVisible(*FocusLayer)) DrawStampQuad(*Focus, true);
    }
    // .tp-uv-info / source notice / readout.
    const char *InfoTitle = UvSource ? "SOURCE UV0 · OVERLAPPING" : "AUTHORING UDIMS";
    Caption(ImVec2(Region.MinimumX + 20 * S, Region.MinimumY + 52 * S), InfoTitle, 10 * S, 0xd8dde2);
    char Info[96];
    std::snprintf(Info, sizeof(Info), "32 charts · %d %s · %zu triangles", TileCount, TileCount == 1 ? "tile" : "tiles", Topology.size());
    Caption(ImVec2(Region.MinimumX + 20 * S, Region.MinimumY + 68 * S), Info, 9 * S, 0x8b949d);
    if (UvSource)
    {
        const char *Notice = "The original 32 patch UVs overlap in 1001. Inspect only; use Authoring UDIMs for unambiguous placement.";
        DrawList->AddRectFilled(ImVec2(Region.MinimumX + 16 * S, Region.MinimumY + 86 * S),
            ImVec2(Region.MinimumX + 20 * S + CaptionWidth(Notice, 9 * S) + 12 * S, Region.MinimumY + 112 * S), Colour32(0x2a2118, .9f), 8 * S);
        Caption(ImVec2(Region.MinimumX + 26 * S, Region.MinimumY + 95 * S), Notice, 9 * S, 0xd8c49a);
    }
    std::string Readout = "Pan: middle drag / Alt-drag · Zoom: wheel · F: frame";
    if (Hovered)
    {
        float Point[2];
        ScreenToUv(Mouse.x, Mouse.y, Point);
        if (const auto *Hit = HitUv(Point, !UvSource))
        {
            char Buffer[96];
            std::snprintf(Buffer, sizeof(Buffer), "%s · %d · U %.3f / V %.3f", PartTable[size_t(Hit->Part)].Name,
                PartTable[size_t(Hit->Part)].Tile, Point[0], Point[1]);
            Readout = Buffer;
        }
    }
    UvHoverText = Readout;
    Caption(ImVec2(Region.MinimumX + 20 * S, Region.MaximumY - 52 * S), Readout.c_str(), 9 * S, 0xaeb6be);
    const char *Small = Textured ? "UV texture canvas · 512x512 per UDIM tile · live layer composite"
                                  : "UV topology + artwork footprints · mask guides · no baked atlas";
    Caption(ImVec2(Region.MaximumX - 20 * S - CaptionWidth(Small, 8 * S), Region.MaximumY - 52 * S), Small, 8 * S, 0x6d7680);
    if (Hovered && Painting)
    {
        const float CursorRadius = DigitalActive ? Brush.Size * S * .5f : MaskEditor.SizePercent / 100.f * UvScale;
        DrawList->AddCircle(Mouse, std::max(CursorRadius, 2.f), Colour32(0xedc391, .85f), 32, 1.2f * S);
    }
    DrawList->PopClipRect();
    Track("UV texture-space canvas");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     INSPECTOR / HISTORY DOCK (RIGHT)
//------------------------------------------------------------------------------------------------------------------------

void TexturePaintWorkspacePanel::ConstructInspectorDock()
{
    const float S = InterfaceScale;
    Controls.Scale = S;
    auto *Ink = ImGui::GetWindowDrawList();
    // .tp-inspector-tabs: the right dock's own Inspector | History tab strip.
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        const float Width = ImGui::GetContentRegionAvail().x;
        Ink->AddLine(ImVec2(Origin.x, Origin.y + 32 * S), ImVec2(Origin.x + Width, Origin.y + 32 * S), Colour32(0xffffff, .03f));
        ImGui::SetCursorScreenPos(ImVec2(Origin.x + 10 * S, Origin.y + 3 * S));
        if (PillButton("RightTabInspector", "Inspector", 118 * S, RightTab == RightPanelTab::Inspector, "M4 8H20M4 16H20M8 5V11M16 13V19"))
            RightTab = RightPanelTab::Inspector;
        ImGui::SetCursorScreenPos(ImVec2(Origin.x + 132 * S, Origin.y + 3 * S));
        if (PillButton("RightTabHistory", "History", 104 * S, RightTab == RightPanelTab::History, "M3 12a9 9 0 1 0 3-6.7M3 4v5h5M12 7v5l3 3"))
            RightTab = RightPanelTab::History;
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 33 * S));
    }
    if (RightTab == RightPanelTab::History) ConstructHistory();
    else ConstructInspector();
}

void TexturePaintWorkspacePanel::ConstructInspector()
{
    const float S = InterfaceScale;
    Controls.Scale = S;
    auto *Ink = ImGui::GetWindowDrawList();
    const float FooterHeight = 39 * S;
    LayerState *Layer = SelectedLayer();
    const bool Locked = Layer && EffectiveLocked(*Layer);
    const bool InheritedLock = Layer && Locked && !Layer->Locked;
    int SelectedIndex = -1;
    for (size_t Index = 0; Index < Layers.size(); ++Index)
        if (Layers[Index].Id == SelectedLayerId) SelectedIndex = int(Index);

    ImGui::BeginChild("TextureInspectorScroll", ImVec2(0, std::max(40.f, ImGui::GetContentRegionAvail().y - FooterHeight)), false);
    ImGui::Indent(19 * S);
    ImGui::Dummy(ImVec2(1, 22 * S)); // .tp-inspector-scroll padding: 22px 19px 26px
    if (!Layer)
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        const float CentreX = Origin.x + ImGui::GetContentRegionAvail().x * .5f;
        DrawGlyph("M4 8H20M4 16H20M8 5V11M16 13V19", ImVec2(CentreX - 14 * S, Origin.y + 30 * S), 28 * S, 0x6f6f66);
        Caption(ImVec2(CentreX - CaptionWidth("No layer selected", 13 * S) * .5f, Origin.y + 70 * S), "No layer selected", 13 * S, 0xb5b5a9);
        const char *Body = "Add a layer or select an existing one to inspect its settings.";
        Caption(ImVec2(CentreX - CaptionWidth(Body, 9 * S) * .5f, Origin.y + 92 * S), Body, 9 * S, 0x77776f);
    }
    else
    {
        const auto AccentBar = [&](uint32_t Colour, const char *Label, const char *Small = nullptr)
        {
            const ImVec2 Origin = ImGui::GetCursorScreenPos();
            Ink->AddRectFilled(ImVec2(Origin.x - 14 * S, Origin.y - 2 * S), ImVec2(Origin.x - 11 * S, Origin.y + 10 * S), Colour32(Colour), 1.5f * S);
            Caption(Origin, Label, 11 * S, 0xd0d0c3);
            if (Small)
                Caption(ImVec2(Origin.x + ImGui::GetContentRegionAvail().x - CaptionWidth(Small, 8 * S), Origin.y + 1 * S), Small, 8 * S, 0x71716b);
            ImGui::Dummy(ImVec2(1, 32 * S)); // .tp-property-card h2 margin-bottom: 20px
        };
        const auto SectionLabel = [&](const char *Label, const char *Small)
        {
            const ImVec2 Origin = ImGui::GetCursorScreenPos();
            Caption(ImVec2(Origin.x + 3 * S, Origin.y), Label, 8 * S, 0x909080);
            Caption(ImVec2(Origin.x + ImGui::GetContentRegionAvail().x - CaptionWidth(Small, 8 * S), Origin.y), Small, 8 * S, 0x65655c);
            ImGui::Dummy(ImVec2(1, 21 * S)); // .tp-section-label margin-bottom: 11px
        };
        // .chan-note: 8px wrapped helper text, no rule.
        const auto SmallNote = [&](const char *Text)
        {
            if (RegularTypeface) ImGui::PushFont(static_cast<ImFont *>(RegularTypeface), 8 * 1.302f * S);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 4 * S);
            ImGui::TextColored(Spectrum(0x86867b), "%s", Text);
            ImGui::PopTextWrapPos();
            if (RegularTypeface) ImGui::PopFont();
        };
        // .tp-card-note: rule above, 8px / 1.65 line-height, #77776c.
        const auto CardNote = [&](const char *Text)
        {
            ImGui::Dummy(ImVec2(1, 13 * S));
            const ImVec2 Origin = ImGui::GetCursorScreenPos();
            Ink->AddLine(Origin, ImVec2(Origin.x + ImGui::GetContentRegionAvail().x, Origin.y), Colour32(0xffffff, .023f));
            ImGui::Dummy(ImVec2(1, 14 * S));
            if (RegularTypeface) ImGui::PushFont(static_cast<ImFont *>(RegularTypeface), 8 * 1.302f * S);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 4 * S);
            ImGui::TextColored(Spectrum(0x77776c), "%s", Text);
            ImGui::PopTextWrapPos();
            if (RegularTypeface) ImGui::PopFont();
        };
        // .tp-field-row: 10px label left, 128px select right, 20px below.
        const auto FieldDropdown = [&](const char *Label, int &Index, const std::vector<const char *> &Options, float SelectWidth = 128.f)
        {
            const ImVec2 Origin = ImGui::GetCursorScreenPos();
            const float Width = ImGui::GetContentRegionAvail().x;
            Caption(ImVec2(Origin.x, Origin.y + 8 * S), Label, 10 * S, 0xa3a395);
            ImGui::SetCursorScreenPos(ImVec2(Origin.x + Width - SelectWidth * S, Origin.y));
            ImGui::SetNextItemWidth(SelectWidth * S);
            bool Changed = false;
            const std::string Id = std::string("##Field") + Label;
            if (ImGui::BeginCombo(Id.c_str(), Options[size_t(std::clamp(Index, 0, int(Options.size()) - 1))]))
            {
                for (size_t Option = 0; Option < Options.size(); ++Option)
                    if (ImGui::Selectable(Options[Option], int(Option) == Index)) { Index = int(Option); Changed = true; }
                ImGui::EndCombo();
            }
            Track(Label);
            ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + ImGui::GetItemRectSize().y + 20 * S));
            ImGui::Dummy(ImVec2(0, 0));
            return Changed;
        };
        // .tp-amount (MaterialControls Slider): label + value capsule on one row, 24px pill track below.
        // DisplayScale 100 renders a 0..1 value as a percentage (Alloy's `percentage` sliders).
        const auto AmountSlider = [&](const char *Label, float &Value, float Minimum, float Maximum, float DisplayScale, const char *Unit)
        {
            const ImVec2 Origin = ImGui::GetCursorScreenPos();
            const float Width = ImGui::GetContentRegionAvail().x;
            bool Changed = false;
            Caption(ImVec2(Origin.x, Origin.y + 7 * S), Label, 10 * S, 0xaaa99a);
            float Display = Value * DisplayScale;
            const char *Format = DisplayScale != 1.f || (Maximum - Minimum) > 2.5f ? "%.0f" : "%.2f";
            ImGui::SetCursorScreenPos(ImVec2(Origin.x + Width - 64 * S, Origin.y));
            ImGui::SetNextItemWidth(44 * S);
            ImGui::PushID(Label);
            if (ImGui::InputFloat("##AmountValue", &Display, 0.f, 0.f, Format))
            {
                Value = std::clamp(Display / DisplayScale, Minimum, Maximum);
                Changed = true;
            }
            Caption(ImVec2(Origin.x + Width - 16 * S, Origin.y + 8 * S), Unit, 8 * S, 0x7c7c72);
            // Pill track (24px) with 22px thumb; click/drag anywhere on the track.
            const float TrackY = Origin.y + 39 * S;
            ImGui::SetCursorScreenPos(ImVec2(Origin.x, TrackY));
            const bool Held = ImGui::InvisibleButton("##AmountTrack", ImVec2(Width, 24 * S));
            (void)Held;
            const float Fraction = Maximum > Minimum ? std::clamp((Value - Minimum) / (Maximum - Minimum), 0.f, 1.f) : 0.f;
            if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                const float DragFraction = std::clamp((ImGui::GetIO().MousePos.x - Origin.x - 11 * S) / std::max(Width - 22 * S, 1.f), 0.f, 1.f);
                const float Next = Minimum + DragFraction * (Maximum - Minimum);
                if (Next != Value) { Value = Next; Changed = true; }
            }
            const float FillFraction = Maximum > Minimum ? std::clamp((Value - Minimum) / (Maximum - Minimum), 0.f, 1.f) : 0.f;
            Ink->PushClipRect(ImVec2(Origin.x, TrackY), ImVec2(Origin.x + Width, TrackY + 24 * S), true);
            Ink->AddRectFilled(ImVec2(Origin.x, TrackY), ImVec2(Origin.x + Width, TrackY + 24 * S), Colour32(0x171717), 12 * S);
            Ink->AddRectFilled(ImVec2(Origin.x, TrackY), ImVec2(Origin.x + Width * FillFraction, TrackY + 24 * S), Colour32(0x454545));
            Ink->PopClipRect();
            Ink->AddCircleFilled(ImVec2(Origin.x + 11 * S + FillFraction * (Width - 22 * S), TrackY + 12 * S), 11 * S, Colour32(0xd7d7ce), 24);
            ImGui::PopID();
            Track(Label);
            ImGui::SetCursorScreenPos(ImVec2(Origin.x, TrackY + 24 * S));
            ImGui::Dummy(ImVec2(0, 0));
            (void)Fraction;
            return Changed;
        };

        // .tp-inspector-trail + identity + status pills.
        {
            const ImVec2 Origin = ImGui::GetCursorScreenPos();
            DrawGlyph(LayersIconPath, ImVec2(Origin.x, Origin.y), 11 * S, 0x73736a);
            const char *Trail = MaskSelected ? (Layer->Mask.Kind == "color" ? "Colour mask" : Layer->Mask.Kind == "gradient" ? "Gradient mask" : "Layer mask")
                : Layer->Kind == LayerKind::Folder ? "Layer folder" : "Surface layer";
            Caption(ImVec2(Origin.x + 17 * S, Origin.y + 1 * S), "Teapot", 8 * S, 0x73736a);
            Caption(ImVec2(Origin.x + 23 * S + CaptionWidth("Teapot", 8 * S), Origin.y + 1 * S), "/", 8 * S, 0x73736a);
            Caption(ImVec2(Origin.x + 33 * S + CaptionWidth("Teapot", 8 * S), Origin.y + 1 * S), Trail, 8 * S, 0xb1b1a6);
            ImGui::Dummy(ImVec2(1, 30 * S));
        }
        {
            const ImVec2 Origin = ImGui::GetCursorScreenPos();
            Ink->AddRectFilled(Origin, ImVec2(Origin.x + 55 * S, Origin.y + 55 * S), Colour32(0x212121), 17 * S);
            Ink->AddRect(Origin, ImVec2(Origin.x + 55 * S, Origin.y + 55 * S), Colour32(0xffffff, .03f), 17 * S);
            if (MaskSelected)
            {
                // MaskIcon at 32px: effective white/black (fill XOR inverted), gradient field, colour dot.
                const ImVec2 IconMin(Origin.x + 11.5f * S, Origin.y + 11.5f * S), IconMax(IconMin.x + 32 * S, IconMin.y + 32 * S);
                if (Layer->Mask.Kind == "gradient")
                    Ink->AddRectFilledMultiColor(IconMin, IconMax, Colour32(0x090909), Colour32(0xe6e6df), Colour32(0xe6e6df), Colour32(0x090909));
                else
                    Ink->AddRectFilled(IconMin, IconMax, Colour32((Layer->Mask.Fill >= .5f) != Layer->Mask.Inverted ? 0xe6e6df : 0x090909), 6 * S);
                Ink->AddRect(IconMin, IconMax, Colour32(0x595954), 6 * S);
                if (Layer->Mask.Kind == "color")
                    Ink->AddCircleFilled(ImVec2(IconMax.x - 6 * S, IconMax.y - 6 * S), 3.4f * S,
                        IM_COL32(uint8_t(Layer->Mask.Colour[0] * 255), uint8_t(Layer->Mask.Colour[1] * 255), uint8_t(Layer->Mask.Colour[2] * 255), 255), 12);
            }
            else DrawLayerThumbnail(Origin.x + 6.5f * S, Origin.y + 6.5f * S, 42 * S, *Layer);
            std::string Eyebrow = MaskSelected ? "MASK PROPERTIES" : std::string(MetaForKind(size_t(Layer->Kind)).Label) + " LAYER";
            for (auto &Character : Eyebrow) Character = char(std::toupper(uint8_t(Character)));
            const std::string Title = MaskSelected ? (Layer->Mask.Kind == "color" ? "Colour mask" : Layer->Mask.Kind == "gradient" ? "Gradient mask" : "Layer mask")
                                                   : Layer->Name;
            Caption(ImVec2(Origin.x + 67 * S, Origin.y + 2 * S), Eyebrow.c_str(), 7 * S, 0x8e8b7c);
            Caption(ImVec2(Origin.x + 67 * S, Origin.y + 13 * S), Title.c_str(), 22 * S, 0xe6e6dc, LightTypeface ? LightTypeface : RegularTypeface);
            char Position[64];
            if (MaskSelected) std::snprintf(Position, sizeof(Position), "On %s", Layer->Name.c_str());
            else std::snprintf(Position, sizeof(Position), "Position %d of %zu · top to bottom", SelectedIndex + 1, Layers.size());
            Caption(ImVec2(Origin.x + 67 * S, Origin.y + 43 * S), Position, 8 * S, 0x73736b);
            ImGui::Dummy(ImVec2(1, 69 * S));
        }
        {
            const ImVec2 Origin = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(Origin);
            if (PillButton("InspectorVisibility", Layer->Visible ? "Visible" : "Hidden", 96 * S, Layer->Visible,
                    Layer->Visible ? "M2 12Q12 0 22 12Q12 24 2 12ZM12 9A3 3 0 1 0 12 15A3 3 0 1 0 12 9Z" : "M2 12Q12 0 22 12Q12 24 2 12ZM4 4L20 20"))
            {
                Layer->Visible = !Layer->Visible;
                CommitHistory(Layer->Visible ? "Show layer / folder" : "Hide layer / folder", Layer->Name, "layer");
                MarkDirty();
            }
            ImGui::SetCursorScreenPos(ImVec2(Origin.x + 104 * S, Origin.y));
            ImGui::BeginDisabled(InheritedLock);
            if (PillButton("InspectorLock", Locked ? "Locked" : "Unlocked", 108 * S, Locked,
                    Locked ? "M6 10H18V21H6ZM8 10V7A4 4 0 0 1 16 7V10" : "M6 10H18V21H6ZM8 10V7A4 4 0 0 1 15.5 4.5"))
            {
                Layer->Locked = !Layer->Locked;
                CommitHistory(Layer->Locked ? "Lock layer / folder" : "Unlock layer / folder", Layer->Name, "layer");
                MarkDirty();
            }
            ImGui::EndDisabled();
            ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 44 * S));
        }
        if (Locked)
        {
            const ImVec2 Origin = ImGui::GetCursorScreenPos();
            const float Width = ImGui::GetContentRegionAvail().x;
            Ink->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + 34 * S), Colour32(0x292519), 9 * S);
            Ink->AddRect(Origin, ImVec2(Origin.x + Width, Origin.y + 34 * S), Colour32(0xa7905a, .15f), 9 * S);
            DrawGlyph("M6 10H18V21H6ZM8 10V7A4 4 0 0 1 16 7V10", ImVec2(Origin.x + 10 * S, Origin.y + 11 * S), 12 * S, 0xc0ad8b);
            Caption(ImVec2(Origin.x + 29 * S, Origin.y + 12 * S),
                InheritedLock ? "This layer inherits a locked folder. Unlock the folder to edit its contents." : "Unlock this layer to edit its properties.",
                9 * S, 0xc0ad8b);
            ImGui::Dummy(ImVec2(1, 48 * S));
        }

        if (MaskSelected && Layer->Mask.Present)
        {
            SectionLabel("MASK SETTINGS", Layer->Kind == LayerKind::Folder ? "Folder & descendants" : "Layer source");
            Controls.BeginCard("", true);
            {
                const char *Title = Layer->Mask.Kind == "gradient" ? "Gradient coverage" : Layer->Mask.Kind == "color" ? "Colour selection" : "Coverage mask";
                const float HeadingY = ImGui::GetCursorScreenPos().y;
                AccentBar(0x8b9b82, Title);
                // .tp-card-reset: restore mask defaults (keeping the mask kind), clearing the paint overlay.
                {
                    const ImVec2 Flow = ImGui::GetCursorScreenPos();
                    ImGui::BeginDisabled(Locked);
                    ImGui::SetCursorScreenPos(ImVec2(Flow.x + ImGui::GetContentRegionAvail().x - 24 * S, HeadingY - 5 * S));
                    if (IconButton("ResetMaskProperties", "M3 12a9 9 0 1 0 3-6.7M3 4v5h5", 22 * S))
                    {
                        Layer->Mask.Enabled = true;
                        Layer->Mask.Fill = 1.f;
                        Layer->Mask.Inverted = false;
                        Layer->Mask.Strength = 100.f;
                        Layer->Mask.Colour = {.722f, .451f, .2f, 1.f}; // #b87333
                        Layer->Mask.Tolerance = .1f;
                        Layer->Mask.Softness = .1f;
                        Layer->Mask.Dabs.clear();
                        Layer->Mask.Fills.clear();
                        CommitHistory("Mask properties", Layer->Name, "mask");
                        MarkDirty();
                    }
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Reset mask properties");
                    ImGui::SetCursorScreenPos(Flow);
                }
                // Mask type pills.
                const struct { const char *Id; const char *Label; } Types[4]{{"fill", "Fill"}, {"paint", "Paint"}, {"color", "Colour"}, {"gradient", "Gradient"}};
                float X = ImGui::GetCursorScreenPos().x;
                const float Y = ImGui::GetCursorScreenPos().y;
                const float PillWidth = (ImGui::GetContentRegionAvail().x - 12 * S) / 4.f;
                for (const auto &Entry : Types)
                {
                    ImGui::BeginDisabled(Locked);
                    ImGui::SetCursorScreenPos(ImVec2(X, Y));
                    if (PillButton((std::string("MaskType") + Entry.Id).c_str(), Entry.Label, PillWidth, Layer->Mask.Kind == Entry.Id))
                    {
                        Layer->Mask.Kind = Entry.Id;
                        CommitHistory("Mask properties", Layer->Name, "mask");
                        MarkDirty();
                    }
                    ImGui::EndDisabled();
                    X += PillWidth + 4 * S;
                }
                ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, Y + 36 * S));
                ImGui::BeginDisabled(Locked);
                Controls.ControlHeight = 29;
                if (Layer->Mask.Kind == "gradient")
                {
                    Caption(ImGui::GetCursorScreenPos(), "Mask gradient", 9 * S, 0x909084);
                    ImGui::Dummy(ImVec2(1, 16 * S));
                    const ImVec2 Origin = ImGui::GetCursorScreenPos();
                    const float Width = ImGui::GetContentRegionAvail().x;
                    Ink->AddRectFilledMultiColor(Origin, ImVec2(Origin.x + Width, Origin.y + 22 * S), Colour32(0x090909), Colour32(0xe6e6df), Colour32(0xe6e6df), Colour32(0x090909));
                    Ink->AddRect(Origin, ImVec2(Origin.x + Width, Origin.y + 22 * S), Colour32(0x595954), 5 * S);
                    ImGui::Dummy(ImVec2(1, 30 * S));
                    SmallNote("Surface-field gradient: the native preview reads a vertical falloff over each UDIM tile. Point editing is not native yet.");
                }
                else if (Layer->Mask.Kind == "color")
                {
                    if (Controls.Colour("Selected colour", Layer->Mask.Colour)) QueueHistory("Mask properties", Layer->Name, "mask");
                    if (AmountSlider("Tolerance", Layer->Mask.Tolerance, 0.f, 1.f, 100.f, "%")) QueueHistory("Mask properties", Layer->Name, "mask");
                    ImGui::Dummy(ImVec2(1, 12 * S));
                    if (AmountSlider("Softness", Layer->Mask.Softness, 0.f, 1.f, 100.f, "%")) QueueHistory("Mask properties", Layer->Name, "mask");
                    // ColourMaskPreview, native edition: effective coverage per UDIM part under the current
                    // colour/tolerance/softness (the same evaluation the viewport composite uses).
                    ImGui::Dummy(ImVec2(1, 14 * S));
                    const ImVec2 Origin = ImGui::GetCursorScreenPos();
                    const float SwatchWidth = (ImGui::GetContentRegionAvail().x - 9 * S) / 4.f;
                    for (int PartIndex = 0; PartIndex < 4; ++PartIndex)
                    {
                        TopologyTriangle Probe;
                        Probe.Part = PartIndex;
                        const float ProbeUv[2]{PartIndex + .5f, .5f};
                        const float Coverage = MaskCoverageAt(*Layer, Probe, ProbeUv);
                        const ImVec2 SwatchMin(Origin.x + PartIndex * (SwatchWidth + 3 * S), Origin.y);
                        Ink->AddRectFilled(SwatchMin, ImVec2(SwatchMin.x + SwatchWidth, SwatchMin.y + 30 * S), Colour32(PartTable[size_t(PartIndex)].Colour), 7 * S);
                        Ink->AddRectFilled(SwatchMin, ImVec2(SwatchMin.x + SwatchWidth, SwatchMin.y + 30 * S), Colour32(0x0b0b0b, (1.f - Coverage) * .78f), 7 * S);
                        Ink->AddRect(SwatchMin, ImVec2(SwatchMin.x + SwatchWidth, SwatchMin.y + 30 * S), Colour32(0xffffff, .06f), 7 * S);
                        Caption(ImVec2(SwatchMin.x + 6 * S, SwatchMin.y + 19 * S), PartTable[size_t(PartIndex)].Name, 7 * S, 0xd8d8cd);
                    }
                    ImGui::Dummy(ImVec2(1, 38 * S));
                }
                else
                {
                    const ImVec2 Origin = ImGui::GetCursorScreenPos();
                    Ink->AddRectFilled(Origin, ImVec2(Origin.x + 62 * S, Origin.y + 62 * S), Colour32(Layer->Mask.Fill >= .5f ? 0xe6e6df : 0x090909), 6 * S);
                    Ink->AddRect(Origin, ImVec2(Origin.x + 62 * S, Origin.y + 62 * S), Colour32(0x595954), 6 * S);
                    Caption(ImVec2(Origin.x + 74 * S, Origin.y + 8 * S), (Layer->Mask.Fill >= .5f) != Layer->Mask.Inverted ? "White" : "Black", 11 * S, 0xd8d8cd);
                    Caption(ImVec2(Origin.x + 74 * S, Origin.y + 26 * S),
                        Layer->Mask.Inverted ? "Inverted fill · mask background" : "Uniform fill · mask background", 8 * S, 0x8a8a7c);
                    ImGui::Dummy(ImVec2(1, 70 * S));
                    int FillIndex = Layer->Mask.Fill >= .5f ? 0 : 1;
                    if (FieldDropdown("Fill", FillIndex, {"White", "Black"}))
                    {
                        Layer->Mask.Fill = FillIndex == 0 ? 1.f : 0.f;
                        CommitHistory("Mask properties", Layer->Name, "mask");
                        MarkDirty();
                    }
                }
                // .tp-mask-record-summary (any mask kind with a painted overlay).
                if (const size_t Records = Layer->Mask.Dabs.size() + Layer->Mask.Fills.size(); Records)
                {
                    const std::string Summary = "Paint overlay · " + std::to_string(Records) + " records";
                    Caption(ImGui::GetCursorScreenPos(), Summary.c_str(), 10 * S, 0xd8d8cd);
                    ImGui::Dummy(ImVec2(1, 16 * S));
                    SmallNote("UV brush strokes / region fills over this mask's source. White includes; black excludes.");
                    ImGui::Dummy(ImVec2(1, 8 * S));
                    if (PillButton("ClearMaskPaint", "Clear painted mask overlay", 196 * S, false))
                    {
                        Layer->Mask.Dabs.clear();
                        Layer->Mask.Fills.clear();
                        CommitHistory("Mask properties", Layer->Name, "mask");
                        MarkDirty();
                    }
                    ImGui::Dummy(ImVec2(1, 8 * S));
                }
                ImGui::EndDisabled();
                bool Enabled = Layer->Mask.Enabled;
                if (Controls.Toggle("Mask enabled", Enabled))
                {
                    Layer->Mask.Enabled = Enabled;
                    CommitHistory("Mask properties", Layer->Name, "mask");
                    MarkDirty();
                }
                bool Inverted = Layer->Mask.Inverted;
                ImGui::BeginDisabled(Locked);
                if (Controls.Toggle("Invert mask", Inverted))
                {
                    Layer->Mask.Inverted = Inverted;
                    CommitHistory("Mask properties", Layer->Name, "mask");
                    MarkDirty();
                }
                if (AmountSlider("Mask strength", Layer->Mask.Strength, 0.f, 100.f, 1.f, "%")) QueueHistory("Mask properties", Layer->Name, "mask");
                ImGui::EndDisabled();
                CardNote((std::string(Layer->Kind == LayerKind::Folder ? "Saved on this folder for its descendants. " : "Saved on this layer. ") +
                    (Layer->Mask.Kind == "color"
                            ? "Colour matching uses normalized sRGB RGB distance; the preview evaluates source pixels only. "
                            : "White includes and black excludes. ") +
                    "Paint mask in Lit or packed UV space; strokes and region fills drive the native preview composite on the correct part. "
                    "Colour previews sample the source per UDIM part, not the final shaded material. Digital Paint / Erase / Fill also edits this mask.")
                    .c_str());
            }
            Controls.EndCard();
            {
                const ImVec2 Origin = ImGui::GetCursorScreenPos();
                const float Half = (ImGui::GetContentRegionAvail().x - 8 * S) * .5f;
                ImGui::BeginDisabled(Locked);
                ImGui::SetCursorScreenPos(Origin);
                if (PillButton("RemoveMask", "Remove layer mask", Half, false, "M3 6H21M8 6V3H16V6M6 6L7 21H17L18 6M10 10V17M14 10V17")) RemoveSelectedMask();
                ImGui::EndDisabled();
                ImGui::SetCursorScreenPos(ImVec2(Origin.x + Half + 8 * S, Origin.y));
                if (PillButton("BackToLayer", "Back to layer", Half, false)) SelectLayer(Layer->Id, false);
                ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 36 * S));
            }
        }
        else
        {
            SectionLabel("LAYER PROPERTIES", "Authoring setup");
            Controls.BeginCard("", true);
            {
                AccentBar(0x8b9b82, "Composition", "Layer setup");
                Controls.ControlHeight = 29;
                // Name (AuthoringNameField: commits on deactivation, never to an empty string).
                if (LayerNameSelectionId != Layer->Id)
                {
                    std::strncpy(LayerNameBuffer, Layer->Name.c_str(), sizeof(LayerNameBuffer) - 1);
                    LayerNameBuffer[sizeof(LayerNameBuffer) - 1] = '\0';
                    LayerNameSelectionId = Layer->Id;
                }
                ImGui::TextDisabled("Name");
                ImGui::SetNextItemWidth(-1);
                ImGui::BeginDisabled(Locked);
                ImGui::InputText("##LayerName", LayerNameBuffer, sizeof(LayerNameBuffer));
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    const std::string NewName = Trimmed(LayerNameBuffer);
                    if (!NewName.empty() && NewName != Layer->Name)
                    {
                        const std::string Old = Layer->Name;
                        Layer->Name = NewName.substr(0, 80);
                        CommitHistory("Rename layer", Old + " -> " + Layer->Name, "layer");
                        MarkDirty();
                    }
                    LayerNameSelectionId.clear();
                }
                ImGui::EndDisabled();
                Track("Layer name");
                ImGui::Dummy(ImVec2(1, 17 * S)); // .tp-name-field margin-bottom
                // Folder assignment (.tp-field-row).
                {
                    std::vector<std::string> Options{"Root / ungrouped"}, Ids{""};
                    for (const auto &Candidate : Layers)
                    {
                        if (Candidate.Kind != LayerKind::Folder || Candidate.Id == Layer->Id || IsDescendantOf(Candidate.Id, Layer->Id)) continue;
                        Options.push_back(std::string(LayerDepth(Candidate), '-') + (LayerDepth(Candidate) ? " " : "") + Candidate.Name);
                        Ids.push_back(Candidate.Id);
                    }
                    std::vector<const char *> Labels;
                    for (const auto &Option : Options) Labels.push_back(Option.c_str());
                    int FolderIndex = 0;
                    for (size_t Index = 0; Index < Ids.size(); ++Index)
                        if (Ids[Index] == Layer->ParentId) FolderIndex = int(Index);
                    ImGui::BeginDisabled(Locked);
                    if (FieldDropdown("Folder", FolderIndex, Labels)) SetLayerParent(Layer->Id, Ids[size_t(FolderIndex)]);
                    ImGui::EndDisabled();
                    Layer = SelectedLayer(); // SetLayerParent may have moved the record
                    if (!Layer) { Controls.EndCard(); ImGui::Unindent(19 * S); ImGui::EndChild(); return; }
                }
                if (Layer->Kind == LayerKind::Folder)
                {
                    // .tp-folder-summary: descendant count left, Ungroup right, one row.
                    int Descendants = 0;
                    for (const auto &Candidate : Layers)
                        if (IsDescendantOf(Candidate.Id, Layer->Id)) ++Descendants;
                    char Summary[48];
                    std::snprintf(Summary, sizeof(Summary), "%d descendant layers", Descendants);
                    const ImVec2 Origin = ImGui::GetCursorScreenPos();
                    Caption(ImVec2(Origin.x, Origin.y + 7 * S), Summary, 9 * S, 0x8a8a7c);
                    ImGui::BeginDisabled(Locked);
                    ImGui::SetCursorScreenPos(ImVec2(Origin.x + ImGui::GetContentRegionAvail().x - 128 * S, Origin.y));
                    if (PillButton("UngroupFolder", "Ungroup folder", 128 * S, false)) { ImGui::EndDisabled(); UngroupSelectedFolder(); Controls.EndCard(); ImGui::Unindent(19 * S); ImGui::EndChild(); return; }
                    ImGui::EndDisabled();
                    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 46 * S));
                    ImGui::Dummy(ImVec2(0, 0));
                }
                int BlendIndex = 0;
                for (size_t Index = 0; Index < BlendTable.size(); ++Index)
                    if (Layer->Blend == BlendTable[Index]) BlendIndex = int(Index);
                ImGui::BeginDisabled(Locked);
                if (FieldDropdown("Blend mode", BlendIndex, std::vector<const char *>(BlendTable.begin(), BlendTable.end())))
                {
                    Layer->Blend = BlendTable[size_t(BlendIndex)];
                    CommitHistory("Layer blend", Layer->Name + " · " + Layer->Blend, "layer");
                    MarkDirty();
                }
                if (AmountSlider("Layer opacity", Layer->Opacity, 0.f, 100.f, 1.f, "%"))
                {
                    char Detail[96];
                    std::snprintf(Detail, sizeof(Detail), "%s · %d%%", Layer->Name.c_str(), int(std::round(Layer->Opacity)));
                    QueueHistory("Layer opacity", Detail, "layer");
                }
                ImGui::EndDisabled();
                CardNote("Digital strokes, native materials and decals follow stack order, layer opacity and inherited masks. Brush channels evaluate before PBR lighting. Digital colour blends are supported; native material blend modes remain metadata.");
            }
            Controls.EndCard();

            if (Layer->Kind != LayerKind::Folder)
            {
                Controls.BeginCard("", false);
                {
                    char Small[40];
                    std::snprintf(Small, sizeof(Small), "%d segments / fills", Layer->StrokeCount);
                    AccentBar(0xa79171, "Digital painting", Small);
                    CardNote("Paint or erase enabled channels in Lit or packed UV view. Fill a real mesh component, island or face. Digital strokes are portable and undoable.");
                    ImGui::Dummy(ImVec2(1, 12 * S));
                    const ImVec2 Origin = ImGui::GetCursorScreenPos();
                    const float Half = (ImGui::GetContentRegionAvail().x - 8 * S) * .5f;
                    ImGui::SetCursorScreenPos(Origin);
                    if (PillButton("OpenBrushStudio", "Brush studio", Half, BrushStudioOpen, "M4 20L8 19L20 7L17 4L5 16ZM14 3L21 10")) BrushStudioOpen = true;
                    ImGui::BeginDisabled(Locked || (Layer->Dabs.empty() && Layer->Fills.empty()));
                    ImGui::SetCursorScreenPos(ImVec2(Origin.x + Half + 8 * S, Origin.y));
                    if (PillButton("ClearLayerPaint", "Clear paint", Half, false, "M3 12a9 9 0 1 0 3-6.7M3 4v5h5")) ClearSelectedPaint();
                    ImGui::EndDisabled();
                    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 32 * S));
                    ImGui::Dummy(ImVec2(0, 0)); // anchor the card's auto-resize boundary at the reserved extent
                }
                Controls.EndCard();
            }

            if (Layer->Kind == LayerKind::Decal || !Layer->Stamps.empty())
            {
                // 🖨️ The stamp on this layer, numerically: drag it in the viewport (Transform tool), or
                // set centre/size/rotation here. Position reads tile-local so the numbers stay meaningful.
                Controls.BeginCard("", false);
                {
                    char Small[32];
                    std::snprintf(Small, sizeof(Small), "%zu placed", Layer->Stamps.size());
                    AccentBar(0xc4aa7a, "Stamp placement", Layer->Stamps.empty() ? "Stamp layer" : Small);
                    if (Layer->Stamps.empty())
                        CardNote("This stamp layer is empty - place a stamp with the Stamp tool (it gets its own layer), then drag it with Transform.");
                    else
                    {
                        if (StampFocusLayerId != Layer->Id) { StampFocusLayerId = Layer->Id; StampFocusIndex = int(Layer->Stamps.size()) - 1; }
                        StampFocusIndex = std::clamp(StampFocusIndex, 0, int(Layer->Stamps.size()) - 1);
                        if (Layer->Stamps.size() > 1)
                        {
                            const ImVec2 Origin = ImGui::GetCursorScreenPos();
                            const float Half = (ImGui::GetContentRegionAvail().x - 8 * S) * .5f;
                            ImGui::SetCursorScreenPos(Origin);
                            if (PillButton("PrevStamp", "‹ Prev stamp", Half, false)) StampFocusIndex = (StampFocusIndex + int(Layer->Stamps.size()) - 1) % int(Layer->Stamps.size());
                            ImGui::SetCursorScreenPos(ImVec2(Origin.x + Half + 8 * S, Origin.y));
                            if (PillButton("NextStamp", "Next stamp ›", Half, false)) StampFocusIndex = (StampFocusIndex + 1) % int(Layer->Stamps.size());
                            ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 32 * S));
                            ImGui::Dummy(ImVec2(0, 0));
                        }
                        auto &Placement = Layer->Stamps[size_t(StampFocusIndex)];
                        const auto *Entry = StampLib.Find(Placement.StampId);
                        const auto *Raster = ResolvedStampRaster(Placement);
                        const std::string StampName = Entry ? Entry->Name : "Missing stamp";
                        Caption(ImGui::GetCursorScreenPos(), StampName.c_str(), 11 * S, 0xd0d0c3);
                        ImGui::Dummy(ImVec2(1, 16 * S));
                        if (Raster && Entry && Raster != Entry)
                        {
                            char Note[96];
                            std::snprintf(Note, sizeof(Note), "Rendered from the stamp source at %dpx for this size (library raster %dpx).", Raster->Width, Entry->Width);
                            SmallNote(Note);
                        }
                        else if (Entry)
                        {
                            char Note[96];
                            std::snprintf(Note, sizeof(Note), "Painted from the stamp's native %dx%d raster.", Entry->Width, Entry->Height);
                            SmallNote(Note);
                        }
                        ImGui::Dummy(ImVec2(1, 10 * S));
                        const int Tile = std::clamp(int(std::floor(Placement.U)), 0, 3);
                        float CentreX = (Placement.U - float(Tile)) * 100.f;
                        float CentreY = Placement.V * 100.f;
                        float Diameter = Placement.Radius * 200.f;
                        float RotationDeg = Placement.Rotation * 57.29578f;
                        bool Changed = false;
                        ImGui::BeginDisabled(Locked);
                        if (AmountSlider("Centre X", CentreX, 0.f, 100.f, 1.f, "%")) { Placement.U = float(Tile) + CentreX / 100.f; Changed = true; }
                        if (AmountSlider("Centre Y", CentreY, 0.f, 100.f, 1.f, "%")) { Placement.V = CentreY / 100.f; Changed = true; }
                        if (AmountSlider("Size", Diameter, 2.f, 80.f, 1.f, "%")) { Placement.Radius = Diameter / 200.f; Changed = true; }
                        if (AmountSlider("Rotation", RotationDeg, -180.f, 180.f, 1.f, "deg")) { Placement.Rotation = RotationDeg / 57.29578f; Changed = true; }
                        if (AmountSlider("Stamp strength", Placement.Tint[3], 0.f, 1.f, 100.f, "%")) Changed = true;
                        // 🎚️ This decal's own PBR behaviour (seeded from the stamp design's surface
                        // channels on placement; document data exported with the project, like every
                        // other channel value - the preview composite itself is colour-only).
                        if (AmountSlider("Roughness", Placement.Roughness, 0.f, 1.f, 100.f, "%")) Changed = true;
                        if (AmountSlider("Metalness", Placement.Metalness, 0.f, 1.f, 100.f, "%")) Changed = true;
                        if (AmountSlider("Height", Placement.Height, 0.f, 1.f, 100.f, "%")) Changed = true;
                        ImGui::EndDisabled();
                        if (Changed) QueueHistory("Stamp transform", Layer->Name, "stamp");
                        ImGui::Dummy(ImVec2(1, 8 * S));
                        ImGui::BeginDisabled(Locked);
                        const ImVec2 Origin = ImGui::GetCursorScreenPos();
                        if (PillButton("DeleteStamp", "Remove this stamp", ImGui::GetContentRegionAvail().x, false, "M3 6H21M8 6V3H16V6M6 6L7 21H17L18 6M10 10V17M14 10V17"))
                        {
                            Layer->Stamps.erase(Layer->Stamps.begin() + StampFocusIndex);
                            Layer->DecalCount = int(Layer->Stamps.size());
                            StampFocusIndex = Layer->Stamps.empty() ? 0 : std::min(StampFocusIndex, int(Layer->Stamps.size()) - 1);
                            CommitHistory("Remove stamp", Layer->Name, "stamp");
                            MarkDirty();
                        }
                        ImGui::EndDisabled();
                        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 32 * S));
                        ImGui::Dummy(ImVec2(0, 0));
                    }
                }
                Controls.EndCard();
            }

            if (Layer->Kind != LayerKind::Folder && Layer->Kind != LayerKind::Decal)
            {
                // 🧩 ChannelPropertyPanel.jsx: target card (heading + chips + picker) then one folded card
                // per enabled channel. Source settings below are stored authoring configuration (Alloy's
                // own "Not composited" contract) - the native preview does not evaluate channel sources.
                char Enabled[32];
                std::snprintf(Enabled, sizeof(Enabled), "%zu enabled", Layer->Channels.size());
                SectionLabel("CHANNEL TARGETS", Enabled);
                const auto ChannelEnabled = [&](const char *Id)
                {
                    return std::find(Layer->Channels.begin(), Layer->Channels.end(), Id) != Layer->Channels.end();
                };
                const auto ChannelMode = [&](const ChannelMeta &Channel) -> std::string
                {
                    if (std::string(Channel.Edit) == "derived") return "derived";
                    const auto Found = Layer->ChannelModes.find(Channel.Id);
                    return Found != Layer->ChannelModes.end() ? Found->second : "value";
                };
                std::string PendingToggle;
                bool PendingClear = false;
                Controls.BeginCard("", false);
                {
                    // .channel-target-heading: layers glyph, title, count badge, add button.
                    const ImVec2 Origin = ImGui::GetCursorScreenPos();
                    const float Width = ImGui::GetContentRegionAvail().x;
                    const float Right = Origin.x + Width;
                    DrawGlyph(LayersIconPath, ImVec2(Origin.x, Origin.y + 5 * S), 14 * S, 0xaaa99b);
                    Caption(ImVec2(Origin.x + 21 * S, Origin.y + 6 * S), "Channel targets", 11 * S, 0xaaa99b);
                    char Count[8];
                    std::snprintf(Count, sizeof(Count), "%zu", Layer->Channels.size());
                    Ink->AddCircleFilled(ImVec2(Right - 42 * S, Origin.y + 12.5f * S), 10 * S, Colour32(0x3b3b37), 20);
                    Caption(ImVec2(Right - 42 * S - CaptionWidth(Count, 9 * S) * .5f, Origin.y + 7.5f * S), Count, 9 * S, 0xd8d8ca);
                    ImGui::BeginDisabled(Locked);
                    ImGui::SetCursorScreenPos(ImVec2(Right - 25 * S, Origin.y));
                    if (IconButton("ChannelPickerToggle", "M12 5v14M5 12h14", 25 * S, ChannelPickerOpen)) ChannelPickerOpen = !ChannelPickerOpen;
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Add painting channel");
                    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 38 * S));
                    ImGui::Dummy(ImVec2(0, 0));
                    // .channel-chips: one removable chip per enabled channel, wrapped.
                    {
                        const ImVec2 ChipsOrigin = ImGui::GetCursorScreenPos();
                        float ChipX = ChipsOrigin.x, ChipY = ChipsOrigin.y;
                        bool AnyChip = false;
                        for (const auto &Channel : ChannelTable)
                        {
                            if (!ChannelEnabled(Channel.Id)) continue;
                            AnyChip = true;
                            const float ChipWidth = 41 * S + CaptionWidth(Channel.Label, 9 * S);
                            if (ChipX + ChipWidth > Right && ChipX > ChipsOrigin.x) { ChipX = ChipsOrigin.x; ChipY += 30 * S; }
                            Ink->AddRectFilled(ImVec2(ChipX, ChipY), ImVec2(ChipX + ChipWidth, ChipY + 25 * S), Colour32(0x171717), 12.5f * S);
                            Ink->AddRect(ImVec2(ChipX, ChipY), ImVec2(ChipX + ChipWidth, ChipY + 25 * S), Colour32(0xffffff, .047f), 12.5f * S);
                            Ink->AddCircleFilled(ImVec2(ChipX + 11 * S, ChipY + 12.5f * S), 3 * S, Colour32(Channel.Colour), 10);
                            Caption(ImVec2(ChipX + 19 * S, ChipY + 7.5f * S), Channel.Label, 9 * S, 0xb8b8aa);
                            ImGui::BeginDisabled(Locked);
                            ImGui::SetCursorScreenPos(ImVec2(ChipX + ChipWidth - 22 * S, ChipY + 4 * S));
                            ImGui::PushID(Channel.Id);
                            if (ImGui::InvisibleButton("##ChipRemove", ImVec2(17 * S, 17 * S))) PendingToggle = Channel.Id;
                            ImGui::PopID();
                            ImGui::EndDisabled();
                            DrawGlyph("M6 6L18 18M6 18L18 6", ImVec2(ChipX + ChipWidth - 18.5f * S, ChipY + 7.5f * S), 10 * S, 0x86867d);
                            ChipX += ChipWidth + 5 * S;
                        }
                        ImGui::SetCursorScreenPos(ImVec2(ChipsOrigin.x, AnyChip ? ChipY + 25 * S : ChipsOrigin.y));
                        ImGui::Dummy(ImVec2(0, 0));
                    }
                    if (!Layer->Channels.empty())
                    {
                        const float ClearWidth = CaptionWidth("Clear all", 8 * S) + 14 * S;
                        const ImVec2 ClearOrigin(Right - ClearWidth, ImGui::GetCursorScreenPos().y + 10 * S);
                        ImGui::BeginDisabled(Locked);
                        ImGui::SetCursorScreenPos(ClearOrigin);
                        if (ImGui::InvisibleButton("##ChannelClearAll", ImVec2(ClearWidth, 20 * S))) PendingClear = true;
                        ImGui::EndDisabled();
                        Caption(ImVec2(ClearOrigin.x + 7 * S, ClearOrigin.y + 5 * S), "Clear all", 8 * S, 0x828277);
                        ImGui::SetCursorScreenPos(ImVec2(Origin.x, ClearOrigin.y + 20 * S));
                        ImGui::Dummy(ImVec2(0, 0));
                    }
                    // .channel-picker: grouped checkbox rows behind the + toggle.
                    if (ChannelPickerOpen)
                    {
                        ImGui::Dummy(ImVec2(1, 14 * S));
                        const ImVec2 RuleOrigin = ImGui::GetCursorScreenPos();
                        Ink->AddLine(RuleOrigin, ImVec2(Right, RuleOrigin.y), Colour32(0xffffff, .051f));
                        ImGui::Dummy(ImVec2(1, 11 * S));
                        const char *Groups[]{"Surface", "Radiance", "Reflectance", "Scattering"};
                        bool FirstGroup = true;
                        for (const char *Group : Groups)
                        {
                            if (!FirstGroup) ImGui::Dummy(ImVec2(1, 12 * S));
                            FirstGroup = false;
                            std::string Heading = Group;
                            for (auto &Character : Heading) Character = char(std::toupper(uint8_t(Character)));
                            Caption(ImVec2(ImGui::GetCursorScreenPos().x + 3 * S, ImGui::GetCursorScreenPos().y), Heading.c_str(), 8 * S, 0x8b8b7f);
                            ImGui::Dummy(ImVec2(1, 16 * S));
                            for (const auto &Channel : ChannelTable)
                            {
                                if (std::string(Channel.Group) != Group) continue;
                                const bool On = ChannelEnabled(Channel.Id);
                                const ImVec2 RowOrigin = ImGui::GetCursorScreenPos();
                                ImGui::BeginDisabled(Locked);
                                ImGui::PushID(Channel.Id);
                                const bool Clicked = ImGui::InvisibleButton("##PickerRow", ImVec2(Width, 25 * S));
                                ImGui::PopID();
                                ImGui::EndDisabled();
                                if (Clicked && !Locked) PendingToggle = Channel.Id;
                                if (On) Ink->AddRectFilled(RowOrigin, ImVec2(Right, RowOrigin.y + 25 * S), Colour32(0x31392b), 8 * S);
                                else if (ImGui::IsItemHovered()) Ink->AddRectFilled(RowOrigin, ImVec2(Right, RowOrigin.y + 25 * S), Colour32(0xffffff, .031f), 8 * S);
                                Ink->AddCircleFilled(ImVec2(RowOrigin.x + 10 * S, RowOrigin.y + 12.5f * S), 3 * S, Colour32(Channel.Colour), 10);
                                Caption(ImVec2(RowOrigin.x + 20 * S, RowOrigin.y + 7 * S), Channel.Label, 10 * S, On ? 0xd9e6d7 : 0xaaa99c);
                                const ImVec2 BoxMin(Right - 20 * S, RowOrigin.y + 6 * S), BoxMax(BoxMin.x + 13 * S, BoxMin.y + 13 * S);
                                if (On) Ink->AddRectFilled(BoxMin, BoxMax, Colour32(0xbfc8a8), 3 * S);
                                Ink->AddRect(BoxMin, BoxMax, Colour32(0xffffff, .25f), 3 * S);
                                if (On) DrawGlyph("M20 6L9 17l-5-5", ImVec2(BoxMin.x + 2 * S, BoxMin.y + 2 * S), 9 * S, 0x23281f, 2.4f);
                                ImGui::SetCursorScreenPos(ImVec2(RowOrigin.x, RowOrigin.y + 25 * S));
                            }
                        }
                        ImGui::Dummy(ImVec2(0, 0));
                    }
                    if (Layer->Channels.empty())
                    {
                        ImGui::Dummy(ImVec2(1, 10 * S));
                        SmallNote("No painting channels enabled. Existing channel values are retained.");
                    }
                }
                Controls.EndCard();
                if (PendingClear && Layer)
                {
                    Layer->Channels.clear();
                    CommitHistory("Channel targets", Layer->Name + " · 0 enabled", "channel");
                    MarkDirty();
                }
                else if (!PendingToggle.empty() && Layer) ToggleChannel(*Layer, PendingToggle);
                Layer = SelectedLayer();

                // One .chan-panel card per enabled channel.
                if (Layer)
                {
                    const std::vector<std::string> EnabledIds = Layer->Channels;
                    for (const auto &Channel : ChannelTable)
                    {
                        if (std::find(EnabledIds.begin(), EnabledIds.end(), Channel.Id) == EnabledIds.end()) continue;
                        const std::string Mode = ChannelMode(Channel);
                        const bool Folded = ChannelFolded.count(Channel.Id) > 0;
                        ImGui::PushID(Channel.Id);
                        Controls.BeginCard("", false);
                        {
                            const ImVec2 Origin = ImGui::GetCursorScreenPos();
                            const float Width = ImGui::GetContentRegionAvail().x;
                            const float Right = Origin.x + Width;
                            // .chan-head
                            ImGui::SetCursorScreenPos(Origin);
                            if (ImGui::InvisibleButton("##ChanHead", ImVec2(Width, 25 * S)))
                            {
                                if (Folded) ChannelFolded.erase(Channel.Id);
                                else ChannelFolded.insert(Channel.Id);
                            }
                            Ink->AddRectFilled(ImVec2(Origin.x, Origin.y + 6 * S), ImVec2(Origin.x + 3 * S, Origin.y + 18 * S), Colour32(Channel.Colour), 1.5f * S);
                            Caption(ImVec2(Origin.x + 11 * S, Origin.y + 6 * S), Channel.Label, 11 * S, 0xd0d0c3);
                            const char *ModeLabel = Mode == "derived" ? "Height-derived" : Mode == "texture" ? "Texture" : Mode == "generator" ? "Generator" : Mode == "gradient" ? "Gradient" : "Value";
                            Caption(ImVec2(Right - 20 * S - CaptionWidth(ModeLabel, 8 * S), Origin.y + 8 * S), ModeLabel, 8 * S, 0x858579);
                            DrawGlyph(Folded ? "m9 6 6 6-6 6" : "m6 9 6 6 6-6", ImVec2(Right - 14 * S, Origin.y + 6 * S), 12 * S, 0x84847a);
                            ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 25 * S));
                            ImGui::Dummy(ImVec2(0, 0));
                            if (!Folded)
                            {
                                const ImVec2 RuleOrigin = ImGui::GetCursorScreenPos();
                                Ink->AddLine(RuleOrigin, ImVec2(Right, RuleOrigin.y), Colour32(0xffffff, .023f));
                                ImGui::Dummy(ImVec2(1, 12 * S));
                                if (Mode == "derived")
                                {
                                    SmallNote("Normal follows the Height target. The native preview does not derive a normal map from Height.");
                                }
                                else
                                {
                                    // .channel-mode source pills (Gradient is Base color only, as in Alloy).
                                    const struct { const char *Id; const char *Label; const char *Icon; } Modes[4]{
                                        {"value", "Value", "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18Z"},
                                        {"texture", "Texture", "M3 5H21V19H3ZM3 15L8 10L12 14L16 10L21 15"},
                                        {"generator", "Generator", "M12 3L13.8 8.2L19 10L13.8 11.8L12 17L10.2 11.8L5 10L10.2 8.2Z"},
                                        {"gradient", "Gradient", "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18Z"}};
                                    int ModeCount = 0;
                                    for (const auto &Entry : Modes)
                                        if (std::string(Entry.Id) != "gradient" || std::string(Channel.Id) == "baseColor") ++ModeCount;
                                    const float PillW = (Width - (ModeCount - 1) * 4 * S) / ModeCount;
                                    float PillX = Origin.x;
                                    const float PillY = ImGui::GetCursorScreenPos().y;
                                    for (const auto &Entry : Modes)
                                    {
                                        if (std::string(Entry.Id) == "gradient" && std::string(Channel.Id) != "baseColor") continue;
                                        ImGui::BeginDisabled(Locked);
                                        ImGui::SetCursorScreenPos(ImVec2(PillX, PillY));
                                        if (PillButton((std::string("ChanMode") + Entry.Id).c_str(), Entry.Label, PillW, Mode == Entry.Id, Entry.Icon))
                                        {
                                            Layer->ChannelModes[Channel.Id] = Entry.Id;
                                            QueueHistory("Channel properties", Layer->Name + " · " + Channel.Label, "channel");
                                        }
                                        ImGui::EndDisabled();
                                        PillX += PillW + 4 * S;
                                    }
                                    ImGui::SetCursorScreenPos(ImVec2(Origin.x, PillY + 43 * S));
                                    ImGui::Dummy(ImVec2(0, 0));
                                    ImGui::BeginDisabled(Locked);
                                    if (Mode == "gradient")
                                    {
                                        const std::string Label = std::string(Channel.Label) + " gradient";
                                        Caption(ImGui::GetCursorScreenPos(), Label.c_str(), 9 * S, 0x909084);
                                        ImGui::Dummy(ImVec2(1, 16 * S));
                                        const ImVec2 BarOrigin = ImGui::GetCursorScreenPos();
                                        Ink->AddRectFilledMultiColor(BarOrigin, ImVec2(Right, BarOrigin.y + 22 * S), Colour32(0x090909), Colour32(0xe6e6df), Colour32(0xe6e6df), Colour32(0x090909));
                                        Ink->AddRect(BarOrigin, ImVec2(Right, BarOrigin.y + 22 * S), Colour32(0x595954), 5 * S);
                                        ImGui::Dummy(ImVec2(1, 30 * S));
                                        SmallNote("Gradient point editing is not native yet.");
                                    }
                                    else if (Mode == "texture")
                                    {
                                        const ImVec2 BoxOrigin = ImGui::GetCursorScreenPos();
                                        Ink->AddRect(BoxOrigin, ImVec2(Right, BoxOrigin.y + 66 * S), Colour32(0xffffff, .08f), 12 * S);
                                        DrawGlyph("M3 5H21V19H3ZM3 15L8 10L12 14L16 10L21 15",
                                            ImVec2(BoxOrigin.x + Width * .5f - 11.5f * S, BoxOrigin.y + 12 * S), 23 * S, 0x6f6f66);
                                        const char *Empty = "Paint or import a texture source";
                                        Caption(ImVec2(BoxOrigin.x + (Width - CaptionWidth(Empty, 9 * S)) * .5f, BoxOrigin.y + 42 * S), Empty, 9 * S, 0x8a8a7c);
                                        ImGui::Dummy(ImVec2(1, 76 * S));
                                        ImGui::BeginDisabled(true);
                                        PillButton("ImportChannelTexture", "Import texture", 150 * S, false, "M12 16V4M7 9l5-5 5 5M4 20h16");
                                        ImGui::EndDisabled();
                                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Texture import is not native yet");
                                        ImGui::Dummy(ImVec2(1, 10 * S));
                                        SmallNote("PNG, JPEG or WebP · portable source up to 256 px. Texture import and the source canvas are not native yet; this target stores configuration only.");
                                    }
                                    else if (Mode == "generator")
                                    {
                                        std::vector<const char *> Labels{"Choose a generator…", "Material studio · custom source"};
                                        std::vector<std::string> Ids{"", "MaterialStudio"};
                                        for (const auto &Generator : GeneratorTable) { Labels.push_back(Generator.Label); Ids.push_back(Generator.Id); }
                                        const std::string Current = Layer->ChannelGenerators.count(Channel.Id) ? Layer->ChannelGenerators.at(Channel.Id) : "";
                                        int GeneratorIndex = 0;
                                        for (size_t At = 0; At < Ids.size(); ++At)
                                            if (Ids[At] == Current) GeneratorIndex = int(At);
                                        if (FieldDropdown("Generator", GeneratorIndex, Labels, 178.f))
                                        {
                                            Layer->ChannelGenerators[Channel.Id] = Ids[size_t(GeneratorIndex)];
                                            QueueHistory("Channel properties", Layer->Name + " · " + Channel.Label, "channel");
                                        }
                                        const std::string Chosen = Ids[size_t(GeneratorIndex)];
                                        if (Chosen == "MaterialStudio")
                                            SmallNote("Material studio custom sources are not native yet. The choice is stored with the layer; built-in generators keep editable parameters here.");
                                        else if (!Chosen.empty())
                                        {
                                            const GeneratorMeta *Definition = nullptr;
                                            for (const auto &Generator : GeneratorTable)
                                                if (Chosen == Generator.Id) Definition = &Generator;
                                            if (Definition) SmallNote((std::string(Definition->Note) + " · source configuration.").c_str());
                                            ImGui::Dummy(ImVec2(1, 10 * S));
                                            for (const auto &Param : GeneratorParamTable)
                                            {
                                                if (Chosen != Param.GeneratorId) continue;
                                                float ParamValue = Param.Value;
                                                if (const auto ByChannel = Layer->ChannelGeneratorParams.find(Channel.Id); ByChannel != Layer->ChannelGeneratorParams.end())
                                                    if (const auto Found = ByChannel->second.find(Param.Key); Found != ByChannel->second.end()) ParamValue = Found->second;
                                                if (AmountSlider(Param.Label, ParamValue, 0.f, 1.f, 1.f, ""))
                                                {
                                                    Layer->ChannelGeneratorParams[Channel.Id][Param.Key] = ParamValue;
                                                    QueueHistory("Channel properties", Layer->Name + " · " + Channel.Label, "channel");
                                                }
                                                ImGui::Dummy(ImVec2(1, 12 * S));
                                            }
                                        }
                                    }
                                    else // value
                                    {
                                        if (std::string(Channel.Edit) == "color")
                                        {
                                            std::array<float, 4> ColourValue{float((Channel.DefaultColour >> 16) & 0xff) / 255.f,
                                                float((Channel.DefaultColour >> 8) & 0xff) / 255.f, float(Channel.DefaultColour & 0xff) / 255.f, 1.f};
                                            if (const auto Found = Layer->ChannelColours.find(Channel.Id); Found != Layer->ChannelColours.end()) ColourValue = Found->second;
                                            if (Controls.Colour("Colour", ColourValue))
                                            {
                                                Layer->ChannelColours[Channel.Id] = ColourValue;
                                                QueueHistory("Channel properties", Layer->Name + " · " + Channel.Label, "channel");
                                            }
                                            const ImVec2 SwatchOrigin = ImGui::GetCursorScreenPos();
                                            Ink->AddRectFilled(SwatchOrigin, ImVec2(SwatchOrigin.x + 22 * S, SwatchOrigin.y + 22 * S),
                                                IM_COL32(uint8_t(ColourValue[0] * 255), uint8_t(ColourValue[1] * 255), uint8_t(ColourValue[2] * 255), 255), 6 * S);
                                            Caption(ImVec2(SwatchOrigin.x + 30 * S, SwatchOrigin.y + 6 * S), "Value swatch", 9 * S, 0xaaa99c);
                                            Caption(ImVec2(Right - CaptionWidth("Not composited", 8 * S), SwatchOrigin.y + 7 * S), "Not composited", 8 * S, 0x858579);
                                            ImGui::Dummy(ImVec2(1, 28 * S));
                                        }
                                        else
                                        {
                                            float ScalarValue = Channel.DefaultValue;
                                            if (const auto Found = Layer->ChannelValues.find(Channel.Id); Found != Layer->ChannelValues.end()) ScalarValue = Found->second;
                                            if (AmountSlider("Amount", ScalarValue, Channel.Min, Channel.Max, 1.f, Channel.Unit))
                                            {
                                                Layer->ChannelValues[Channel.Id] = ScalarValue;
                                                QueueHistory("Channel properties", Layer->Name + " · " + Channel.Label, "channel");
                                            }
                                            ImGui::Dummy(ImVec2(1, 10 * S));
                                            const float Gray = Channel.Max > Channel.Min ? std::clamp((ScalarValue - Channel.Min) / (Channel.Max - Channel.Min), 0.f, 1.f) : 0.f;
                                            const ImVec2 SwatchOrigin = ImGui::GetCursorScreenPos();
                                            const uint8_t Level = uint8_t(Gray * 255);
                                            Ink->AddRectFilled(SwatchOrigin, ImVec2(SwatchOrigin.x + 22 * S, SwatchOrigin.y + 22 * S), IM_COL32(Level, Level, Level, 255), 6 * S);
                                            Caption(ImVec2(SwatchOrigin.x + 30 * S, SwatchOrigin.y + 6 * S), "Value swatch", 9 * S, 0xaaa99c);
                                            Caption(ImVec2(Right - CaptionWidth("Not composited", 8 * S), SwatchOrigin.y + 7 * S), "Not composited", 8 * S, 0x858579);
                                            ImGui::Dummy(ImVec2(1, 28 * S));
                                        }
                                    }
                                    ImGui::EndDisabled();
                                }
                            }
                        }
                        Controls.EndCard();
                        ImGui::PopID();
                        if (!Layer) break;
                        Layer = SelectedLayer();
                        if (!Layer) break;
                    }
                }
            }

            if (Layer)
            {
                Controls.BeginCard("", false);
                {
                    AccentBar(0xa79171, Layer->Kind == LayerKind::Folder ? "Folder coverage" : "Layer source", "Source setup");
                    const ImVec2 Origin = ImGui::GetCursorScreenPos();
                    DrawLayerThumbnail(Origin.x, Origin.y, 36 * S, *Layer);
                    Caption(ImVec2(Origin.x + 46 * S, Origin.y + 2 * S), MetaForKind(size_t(Layer->Kind)).Label, 11 * S, 0xd8d8cd);
                    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 46 * S, Origin.y + 18 * S));
                    ImGui::PushTextWrapPos(Origin.x + ImGui::GetContentRegionAvail().x + 46 * S);
                    if (!Layer->SourceAsset.empty())
                        ImGui::TextColored(Spectrum(0x93938d), "Assigned %s: %s", Layer->SourceAssetType.c_str(), Layer->SourceAsset.c_str());
                    else ImGui::TextColored(Spectrum(0x93938d), "%s", MetaForKind(size_t(Layer->Kind)).Description);
                    ImGui::PopTextWrapPos();
                    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 52 * S));
                    if (Layer->Mask.Present)
                    {
                        if (PillButton("InspectLayerMask", "Inspect layer mask", 170 * S, false)) SelectLayer(Layer->Id, true);
                        ImGui::Dummy(ImVec2(1, 8 * S));
                    }
                    else
                    {
                        ImGui::BeginDisabled(Locked);
                        if (PillButton("AddLayerMaskInspector", "Add layer mask +", 170 * S, false)) AddMaskToSelected(false);
                        ImGui::Dummy(ImVec2(1, 6 * S));
                        if (PillButton("AddColourMaskInspector", "Add colour mask +", 170 * S, false)) AddMaskToSelected(true);
                        ImGui::EndDisabled();
                        ImGui::Dummy(ImVec2(1, 8 * S));
                    }
                }
                Controls.EndCard();
            }
        }

        // .tp-stage-note
        {
            const ImVec2 Origin = ImGui::GetCursorScreenPos();
            DrawGlyph("M12 3A9 9 0 1 0 12 21A9 9 0 1 0 12 3ZM12 11V16M12 8V8.01", ImVec2(Origin.x, Origin.y + 2 * S), 13 * S, 0x8a8a7c);
            ImGui::SetCursorScreenPos(ImVec2(Origin.x + 21 * S, Origin.y));
            if (RegularTypeface) ImGui::PushFont(static_cast<ImFont *>(RegularTypeface), 8 * 1.302f * S);
            ImGui::PushTextWrapPos(Origin.x + ImGui::GetContentRegionAvail().x);
            ImGui::TextColored(Spectrum(0x86867b), "%s",
                "Materials and decals evaluate bottom to top. Locked Layer 0 supplies white fallback and baked AO / normal support. "
                "Preview paint is evaluated per-triangle on screen; this live surface stack is not a baked UV atlas.");
            ImGui::PopTextWrapPos();
            if (RegularTypeface) ImGui::PopFont();
        }
        ImGui::Dummy(ImVec2(1, 12 * S));
    }
    ImGui::Unindent(19 * S);
    ImGui::EndChild();
    // .tp-inspector-footer
    {
        const ImVec2 WindowPosition = ImGui::GetWindowPos(), WindowSize = ImGui::GetWindowSize();
        const ImVec2 FooterMinimum(WindowPosition.x, WindowPosition.y + WindowSize.y - FooterHeight);
        Ink->AddRectFilled(FooterMinimum, ImVec2(FooterMinimum.x + WindowSize.x, FooterMinimum.y + FooterHeight), Colour32(0x151515));
        Ink->AddLine(FooterMinimum, ImVec2(FooterMinimum.x + WindowSize.x, FooterMinimum.y), Colour32(0xffffff, .02f));
        Ink->AddCircleFilled(ImVec2(FooterMinimum.x + 21.5f * S, FooterMinimum.y + FooterHeight * .5f), 2.5f * S, Colour32(0x8aa27e), 10);
        Caption(ImVec2(FooterMinimum.x + 28 * S, FooterMinimum.y + FooterHeight * .5f - 5 * S), "Layer sources", 8 * S, 0x777d66);
        const char *State = !Layer ? "No selection" : Locked ? "Read only" : "Editable";
        Caption(ImVec2(FooterMinimum.x + WindowSize.x - 19 * S - CaptionWidth(State, 7 * S), FooterMinimum.y + FooterHeight * .5f - 4.5f * S), State, 7 * S, 0x686e59);
    }
}

void TexturePaintWorkspacePanel::ConstructHistory()
{
    const float S = InterfaceScale;
    Controls.Scale = S;
    auto *Ink = ImGui::GetWindowDrawList();
    const float FooterHeight = 39 * S;
    const int Inspected = HistoryChosen >= 0 && HistoryChosen < int(HistoryEntries.size()) ? HistoryChosen : HistoryIndex;
    ImGui::BeginChild("TextureHistoryScroll", ImVec2(0, std::max(40.f, ImGui::GetContentRegionAvail().y - FooterHeight)), false);
    ImGui::Indent(19 * S);
    ImGui::Dummy(ImVec2(1, 16 * S));
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        Caption(Origin, "DOCUMENT TIMELINE", 7 * S, 0x8e8b7c);
        Caption(ImVec2(Origin.x, Origin.y + 12 * S), "History", 22 * S, 0xe6e6dc, LightTypeface ? LightTypeface : RegularTypeface);
        char States[24];
        std::snprintf(States, sizeof(States), "%zu states", HistoryEntries.size());
        Caption(ImVec2(Origin.x + ImGui::GetContentRegionAvail().x - CaptionWidth(States, 8 * S), Origin.y + 16 * S), States, 8 * S, 0x7a7a69);
        ImGui::Dummy(ImVec2(1, 52 * S));
    }
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        const float Half = (ImGui::GetContentRegionAvail().x - 8 * S) * .5f;
        ImGui::BeginDisabled(!CanUndo());
        ImGui::SetCursorScreenPos(Origin);
        if (PillButton("HistoryUndo", "Undo", Half, false, "M9 14L4 9L9 4M4 9H14a6 6 0 0 1 0 12h-3")) Undo();
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!CanRedo());
        ImGui::SetCursorScreenPos(ImVec2(Origin.x + Half + 8 * S, Origin.y));
        if (PillButton("HistoryRedo", "Redo", Half, false, "M15 14L20 9L15 4M20 9H10a6 6 0 0 0 0 12h3")) Redo();
        ImGui::EndDisabled();
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 38 * S));
    }
    // Last-stroke preview card: the inspected entry's real dab records, plotted in UDIM space.
    Controls.BeginCard("", true);
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        Ink->AddRectFilled(ImVec2(Origin.x - 14 * S, Origin.y - 2 * S), ImVec2(Origin.x - 11 * S, Origin.y + 10 * S), Colour32(0x8b9b82), 1.5f * S);
        Caption(Origin, "Last stroke", 11 * S, 0xd8d8cd);
        const bool HasStroke = Inspected >= 0 && HistoryEntries[size_t(Inspected)].Kind == "stroke";
        const char *Small = HasStroke ? (HistoryEntries[size_t(Inspected)].Detail.find("Mask coverage") != std::string::npos ? "Mask coverage" : "Surface channels") : "2D source";
        Caption(ImVec2(Origin.x + ImGui::GetContentRegionAvail().x - CaptionWidth(Small, 8 * S), Origin.y + 1 * S), Small, 8 * S, 0x7a7a69);
        ImGui::Dummy(ImVec2(1, 24 * S));
        if (HasStroke)
        {
            const auto &Entry = HistoryEntries[size_t(Inspected)];
            const ImVec2 PlotOrigin = ImGui::GetCursorScreenPos();
            const float PlotWidth = ImGui::GetContentRegionAvail().x, PlotHeight = 92 * S;
            constexpr float Cell = 11.f;
            for (float Y = PlotOrigin.y; Y < PlotOrigin.y + PlotHeight; Y += Cell * S)
                for (float X = PlotOrigin.x; X < PlotOrigin.x + PlotWidth; X += Cell * S)
                {
                    const int Parity = (int((X - PlotOrigin.x) / (Cell * S)) + int((Y - PlotOrigin.y) / (Cell * S))) & 1;
                    Ink->AddRectFilled(ImVec2(X, Y), ImVec2(std::min(X + Cell * S, PlotOrigin.x + PlotWidth), std::min(Y + Cell * S, PlotOrigin.y + PlotHeight)),
                        Colour32(Parity ? 0x252525 : 0x45453c));
                }
            for (const auto &Layer : Entry.State.Layers)
            {
                const auto Plot = [&](const std::vector<PaintDab> &Dabs)
                {
                    for (const auto &Dab : Dabs)
                        Ink->AddCircleFilled(ImVec2(PlotOrigin.x + Dab.U / 4.f * PlotWidth, PlotOrigin.y + (1.f - Dab.V) * PlotHeight),
                            std::max(1.5f * S, Dab.Radius * PlotWidth / 4.f),
                            IM_COL32(uint8_t(Dab.Colour[0] * 255), uint8_t(Dab.Colour[1] * 255), uint8_t(Dab.Colour[2] * 255), 200), 10);
                };
                Plot(Layer.Dabs);
                Plot(Layer.Mask.Dabs);
            }
            ImGui::Dummy(ImVec2(1, PlotHeight + 10 * S));
            const char *Preset = "Studio round";
            for (const auto &Candidate : PresetTable)
                if (Entry.State.Brush.Preset == Candidate.Id) Preset = Candidate.Name;
            Caption(ImGui::GetCursorScreenPos(), Preset, 10 * S, 0xd8d8cd);
            ImGui::Dummy(ImVec2(1, 16 * S));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 4 * S);
            ImGui::TextColored(Spectrum(0x93938d), "%s\nActual recorded stroke at this history state. Plotted from the committed dab records, not a tool-setting illustration.", Entry.Detail.c_str());
            ImGui::PopTextWrapPos();
        }
        else
        {
            const ImVec2 EmptyOrigin = ImGui::GetCursorScreenPos();
            const float CentreX = EmptyOrigin.x + ImGui::GetContentRegionAvail().x * .5f;
            DrawGlyph("M4 20L8 19L20 7L17 4L5 16ZM14 3L21 10", ImVec2(CentreX - 10.5f * S, EmptyOrigin.y + 8 * S), 21 * S, 0x6f6f66);
            Caption(ImVec2(CentreX - CaptionWidth("No recorded stroke", 11 * S) * .5f, EmptyOrigin.y + 38 * S), "No recorded stroke", 11 * S, 0xb5b5a9);
            const char *Body = "Use digital Paint / Erase / Fill on a layer or mask.";
            Caption(ImVec2(CentreX - CaptionWidth(Body, 8 * S) * .5f, EmptyOrigin.y + 56 * S), Body, 8 * S, 0x77776f);
            ImGui::Dummy(ImVec2(1, 72 * S));
        }
    }
    Controls.EndCard();
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        const float Half = (ImGui::GetContentRegionAvail().x - 8 * S) * .5f;
        ImGui::SetCursorScreenPos(Origin);
        if (PillButton("HistoryFilterAll", "All edits", Half, !HistoryStrokesOnly)) HistoryStrokesOnly = false;
        ImGui::SetCursorScreenPos(ImVec2(Origin.x + Half + 8 * S, Origin.y));
        if (PillButton("HistoryFilterStrokes", "Strokes", Half, HistoryStrokesOnly)) HistoryStrokesOnly = true;
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 38 * S));
    }
    int ShownEntries = 0;
    for (int Index = int(HistoryEntries.size()) - 1; Index >= 0; --Index)
    {
        const auto &Entry = HistoryEntries[size_t(Index)];
        if (HistoryStrokesOnly && Entry.Kind != "stroke") continue;
        ++ShownEntries;
        const bool Current = Index == HistoryIndex;
        const bool Chosen = Index == Inspected;
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        const float Width = ImGui::GetContentRegionAvail().x, Height = 54 * S;
        ImGui::PushID(Index);
        ImGui::SetCursorScreenPos(Origin);
        const bool Clicked = ImGui::InvisibleButton("##HistoryEntry", ImVec2(Width, Height));
        ImGui::PopID();
        if (Clicked) HistoryChosen = Index;
        if (Chosen || Current) Ink->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + Height), Colour32(Current ? 0x26261f : 0x1e1e1c), 10 * S);
        if (Current) Ink->AddRectFilled(Origin, ImVec2(Origin.x + 3 * S, Origin.y + Height), Colour32(0xd9b281), 3 * S);
        const char *Icon = Entry.Kind == "stroke" || Entry.Kind == "tool" ? "M4 20L8 19L20 7L17 4L5 16ZM14 3L21 10"
            : Entry.Kind == "folder" ? "M3 7a2 2 0 0 1 2-2h3.5l1.7 1.7H19a2 2 0 0 1 2 2v7.3a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2Z"
            : Entry.Kind == "channel" ? "M4 8H20M4 16H20M8 5V11M16 13V19"
            : Entry.Kind == "source" ? "M3 5H21V19H3ZM3 15L8 10L12 14L16 10L21 15"
            : Entry.Kind == "project" ? "M3 12a9 9 0 1 0 3-6.7M3 4v5h5M12 7v5l3 3"
                                      : LayersIconPath;
        Ink->AddRectFilled(ImVec2(Origin.x + 8 * S, Origin.y + 10 * S), ImVec2(Origin.x + 42 * S, Origin.y + 44 * S), Colour32(0x232323), 9 * S);
        DrawGlyph(Icon, ImVec2(Origin.x + 18 * S, Origin.y + 20 * S), 14 * S, Index > HistoryIndex ? 0x66665c : 0xa8a89a);
        Caption(ImVec2(Origin.x + 52 * S, Origin.y + 8 * S), Entry.Label.c_str(), 10 * S, Index > HistoryIndex ? 0x7a7a6e : 0xd8d8cd);
        Caption(ImVec2(Origin.x + 52 * S, Origin.y + 24 * S), Entry.Detail.c_str(), 8 * S, 0x8a8a7c);
        char State[64];
        std::snprintf(State, sizeof(State), "%s · %s", Current ? "Current state" : Index > HistoryIndex ? "Undone" : "Applied", Entry.TimeText.c_str());
        Caption(ImVec2(Origin.x + 52 * S, Origin.y + 38 * S), State, 7 * S, 0x66665a);
        char Number[8];
        std::snprintf(Number, sizeof(Number), "%02d", Index);
        Caption(ImVec2(Origin.x + Width - 26 * S, Origin.y + 10 * S), Number, 10 * S, 0x8a8a7c);
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + Height + 4 * S));
    }
    ImGui::Dummy(ImVec2(0, 0));
    if (!ShownEntries)
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        const float CentreX = Origin.x + ImGui::GetContentRegionAvail().x * .5f;
        DrawGlyph("M12 3A9 9 0 1 0 12 21A9 9 0 1 0 12 3ZM12 7V12L15 15", ImVec2(CentreX - 9 * S, Origin.y + 16 * S), 18 * S, 0x6f6f66);
        const char *Body = "No stroke transactions in this session.";
        Caption(ImVec2(CentreX - CaptionWidth(Body, 9 * S) * .5f, Origin.y + 42 * S), Body, 9 * S, 0x77776f);
        ImGui::Dummy(ImVec2(1, 66 * S));
    }
    ImGui::Unindent(19 * S);
    ImGui::EndChild();
    // .tp-history-footer
    {
        const ImVec2 WindowPosition = ImGui::GetWindowPos(), WindowSize = ImGui::GetWindowSize();
        const ImVec2 FooterMinimum(WindowPosition.x, WindowPosition.y + WindowSize.y - FooterHeight);
        Ink->AddRectFilled(FooterMinimum, ImVec2(FooterMinimum.x + WindowSize.x, FooterMinimum.y + FooterHeight), Colour32(0x151515));
        Ink->AddLine(FooterMinimum, ImVec2(FooterMinimum.x + WindowSize.x, FooterMinimum.y), Colour32(0xffffff, .02f));
        Caption(ImVec2(FooterMinimum.x + 19 * S, FooterMinimum.y + FooterHeight * .5f - 4.5f * S), "Session history · 80 undo steps", 7 * S, 0x686e59);
        ImGui::BeginDisabled(Inspected < 0 || Inspected == HistoryIndex);
        ImGui::SetCursorScreenPos(ImVec2(FooterMinimum.x + WindowSize.x - 19 * S - 132 * S, FooterMinimum.y + 6.5f * S));
        if (PillButton("RestoreHistoryState", "Restore this state", 132 * S, false)) RestoreHistoryEntry(Inspected);
        ImGui::EndDisabled();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     DIGITAL BRUSH STUDIO (POPUP)
//------------------------------------------------------------------------------------------------------------------------

void TexturePaintWorkspacePanel::ConstructBrushStudio()
{
    if (!BrushStudioOpen) return;
    const float S = InterfaceScale;
    Controls.Scale = S;
    const auto *Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(Viewport->WorkPos.x + Viewport->WorkSize.x * .5f, Viewport->WorkPos.y + Viewport->WorkSize.y * .5f),
        ImGuiCond_Appearing, ImVec2(.5f, .5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(460 * S, Viewport->WorkSize.x * .94f), std::min(580 * S, Viewport->WorkSize.y * .9f)), ImGuiCond_Appearing);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Spectrum(0x191919));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14 * S);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * S, 14 * S));
    ImGui::Begin("Digital brush studio", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking);
    auto *Ink = ImGui::GetWindowDrawList();
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        DrawGlyph("M4 20L8 19L20 7L17 4L5 16ZM14 3L21 10", Origin, 17 * S, 0xedc391);
        Caption(ImVec2(Origin.x + 24 * S, Origin.y + 1 * S), "Digital brush studio", 14 * S, 0xe8e2d2, LightTypeface ? LightTypeface : RegularTypeface);
        const char *Note = Brush.Tool == "erase" ? "Erase removes authored pixels and reveals the layer below; masks restore their original source."
            : Brush.Tool == "fill" ? "Fill uses channel values and opacity. Click a face, island or mesh component."
                                   : "Paint deposits the brush colour on enabled channels.";
        Caption(ImVec2(Origin.x + 24 * S, Origin.y + 20 * S), Note, 8 * S, 0x8a8474);
        ImGui::SetCursorScreenPos(ImVec2(Origin.x + ImGui::GetContentRegionAvail().x - 28 * S, Origin.y));
        if (IconButton("CloseBrushStudio", "M6 6L18 18M6 18L18 6", 26 * S)) BrushStudioOpen = false;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close digital brush studio");
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 40 * S));
    }
    ImGui::BeginChild("BrushStudioScroll", ImVec2(0, std::max(60.f, ImGui::GetContentRegionAvail().y - 40 * S)), false);
    const auto AccentBar = [&](const char *Label)
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        Ink->AddRectFilled(ImVec2(Origin.x - 14 * S, Origin.y - 2 * S), ImVec2(Origin.x - 11 * S, Origin.y + 10 * S), Colour32(0xd9b281), 1.5f * S);
        Caption(Origin, Label, 11 * S, 0xd8d8cd);
        ImGui::Dummy(ImVec2(1, 26 * S));
    };
    const auto QueueBrush = [&]() { QueueHistory("Digital brush settings", BrushPresetName(), "tool"); };
    // Tools + presets.
    {
        float X = ImGui::GetCursorScreenPos().x;
        const float Y = ImGui::GetCursorScreenPos().y;
        const struct { const char *Id; const char *Label; const char *Icon; } Tools[3]{
            {"paint", "Paint", "M4 20L8 19L20 7L17 4L5 16ZM14 3L21 10"},
            {"erase", "Erase", "M7 21L3 17L14 6L20 12L13 19H8ZM14 6L18 2L22 6L20 8"},
            {"fill", "Fill", "M5 11L12 4L19 11L12 18ZM12 18V21M5 21H19"}};
        for (const auto &Entry : Tools)
        {
            ImGui::SetCursorScreenPos(ImVec2(X, Y));
            if (PillButton((std::string("StudioTool") + Entry.Id).c_str(), Entry.Label, 84 * S, Brush.Tool == Entry.Id, Entry.Icon))
            {
                Brush.Tool = Entry.Id;
                CommitHistory("Digital tool", BrushPresetName(), "tool");
                MarkDirty();
            }
            X += 88 * S;
        }
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, Y + 34 * S));
        Caption(ImGui::GetCursorScreenPos(), "DIGITAL BRUSH PRESETS", 8 * S, 0x909080);
        ImGui::Dummy(ImVec2(1, 18 * S));
        for (const auto &Preset : PresetTable)
        {
            const ImVec2 Origin = ImGui::GetCursorScreenPos();
            const float Width = ImGui::GetContentRegionAvail().x;
            ImGui::PushID(Preset.Id);
            const bool Pressed = ImGui::InvisibleButton("##Preset", ImVec2(Width, 36 * S));
            ImGui::PopID();
            const bool SelectedPreset = Brush.Preset == Preset.Id;
            if (SelectedPreset || ImGui::IsItemHovered())
                Ink->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + 36 * S), Colour32(SelectedPreset ? 0x2c2620 : 0x232320), 9 * S);
            Caption(ImVec2(Origin.x + 10 * S, Origin.y + 5 * S), Preset.Name, 10 * S, SelectedPreset ? 0xf0e9d8 : 0xc8c8ba);
            Caption(ImVec2(Origin.x + 10 * S, Origin.y + 21 * S), Preset.Note, 8 * S, 0x8a8a7c);
            if (Pressed) ApplyBrushPreset(Preset.Id);
            ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 38 * S));
        }
    }
    // 🖌️ PaintWorkshop physical instruments: the HarborPaintWorkshop stamp engine's eleven tools. Digital keeps
    // the dab renderer; every other choice paints the preview through the raster stamp canvas on that layer.
    {
        const float X0 = ImGui::GetCursorScreenPos().x;
        const float Y0 = ImGui::GetCursorScreenPos().y + 4 * S;
        Caption(ImVec2(X0, Y0), "PAINTWORKSHOP INSTRUMENTS", 8 * S, 0x909080);
        const struct { const char *Id; const char *Label; } PhysicalTools[]{
            {"Digital", "Digital"}, {"PaintBrush", "Paint brush"}, {"Spray", "Spray"},
            {"Dry", "Dry media"}, {"Pencil", "Pencil"}, {"Marker", "Marker"},
            {"Sand", "Sand"}, {"Splat", "Splat"}, {"Dust", "Dust"},
            {"Soot", "Soot"}, {"Eraser", "Eraser"}};
        int Slot = 0;
        for (const auto &Entry : PhysicalTools)
        {
            const float X = X0 + (Slot % 3) * 88 * S;
            const float Y = Y0 + 16 * S + (Slot / 3) * 30 * S;
            ImGui::SetCursorScreenPos(ImVec2(X, Y));
            if (PillButton((std::string("StudioPhysical") + Entry.Id).c_str(), Entry.Label, 84 * S, Brush.PhysicalTool == Entry.Id))
            {
                Brush.PhysicalTool = Entry.Id;
                if (Brush.Tool == "fill") Brush.Tool = "paint";
                Status = std::string(Entry.Label) + " - PaintWorkshop stamp engine";
                CommitHistory("PaintWorkshop instrument", Entry.Label, "tool");
                MarkDirty();
            }
            ++Slot;
        }
        const int Rows = (Slot + 2) / 3;
        ImGui::SetCursorScreenPos(ImVec2(X0, Y0 + 16 * S + Rows * 30 * S));
        ImGui::Dummy(ImVec2(0, 0));
    }
    {
        float X = ImGui::GetCursorScreenPos().x;
        const float Y = ImGui::GetCursorScreenPos().y;
        ImGui::SetCursorScreenPos(ImVec2(X, Y));
        if (PillButton("StudioTabBrush", "Brush properties", 150 * S, !BrushStudioChannels, "M4 8H20M4 16H20M8 5V11M16 13V19")) BrushStudioChannels = false;
        X += 154 * S;
        ImGui::SetCursorScreenPos(ImVec2(X, Y));
        if (PillButton("StudioTabChannels", "Channel properties", 164 * S, BrushStudioChannels, LayersIconPath)) BrushStudioChannels = true;
        X += 168 * S;
        ImGui::SetCursorScreenPos(ImVec2(X, Y));
        if (PillButton("StudioResetBrush", "Reset", 72 * S, false)) ApplyBrushPreset(Brush.Preset);
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, Y + 36 * S));
    }
    Controls.ControlHeight = 29;
    if (!BrushStudioChannels)
    {
        if (Brush.Tool == "fill")
        {
            const char *Scopes[]{"Face", "Island", "Object", "Tile"};
            const RegionScope Mapping[]{RegionScope::Face, RegionScope::Island, RegionScope::Object, RegionScope::Tile};
            int Index = 2;
            for (int At = 0; At < 4; ++At)
                if (Mapping[At] == Brush.FillScope) Index = At;
            if (Controls.Dropdown("Fill region", Index, Scopes)) { Brush.FillScope = Mapping[Index]; QueueBrush(); }
        }
        Controls.BeginCard("", false);
        {
            AccentBar("Stroke path");
            if (Controls.Range("Size", Brush.Size, 0.f, 128.f)) QueueBrush();
            if (Controls.Range("Opacity", Brush.Opacity, 0.f, 1.f)) QueueBrush();
            if (Controls.Range("Flow", Brush.Flow, 0.f, 1.f)) QueueBrush();
            if (Controls.Range("Spacing", Brush.Spacing, .05f, 2.f)) QueueBrush();
            if (Controls.Range("Stabilization", Brush.Stabilization, 0.f, 1.f)) QueueBrush();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 4 * S);
            ImGui::TextColored(Spectrum(0x93938d), "%s", "Size is the diameter in screen pixels. Zero size or opacity paints nothing.");
            ImGui::PopTextWrapPos();
        }
        Controls.EndCard();
        Controls.BeginCard("", false);
        {
            AccentBar("Shape");
            const char *Shapes[]{"Round", "Square"};
            int ShapeIndex = Brush.Shape == "square" ? 1 : 0;
            if (Controls.Dropdown("Digital brush tip shape", ShapeIndex, Shapes)) { Brush.Shape = ShapeIndex ? "square" : "round"; QueueBrush(); }
            if (Controls.Range("Hardness", Brush.Hardness, 0.f, 1.f)) QueueBrush();
            if (Controls.Range("Roundness", Brush.Roundness, .05f, 1.f)) QueueBrush();
            if (Controls.Range("Rotation", Brush.Rotation, 0.f, 360.f)) QueueBrush();
            if (Controls.Toggle("Digital brush follow stroke", Brush.Follow)) QueueBrush();
        }
        Controls.EndCard();
        Controls.BeginCard("", false);
        {
            AccentBar("Grain");
            const char *Grains[]{"None", "Noise", "Lines", "Checker"};
            const char *GrainIds[]{"none", "noise", "lines", "checker"};
            int GrainIndex = 0;
            for (int At = 0; At < 4; ++At)
                if (Brush.Grain == GrainIds[At]) GrainIndex = At;
            if (Controls.Dropdown("Digital brush grain texture", GrainIndex, Grains)) { Brush.Grain = GrainIds[GrainIndex]; QueueBrush(); }
            if (Controls.Range("Grain amount", Brush.GrainAmount, 0.f, 1.f)) QueueBrush();
            if (Controls.Range("Grain scale", Brush.GrainScale, 1.f, 32.f)) QueueBrush();
        }
        Controls.EndCard();
        Controls.BeginCard("", false);
        {
            AccentBar("Stroke dynamics");
            if (Controls.Range("Size jitter", Brush.SizeJitter, 0.f, 1.f)) QueueBrush();
            if (Controls.Range("Opacity jitter", Brush.OpacityJitter, 0.f, 1.f)) QueueBrush();
            if (Controls.Range("Scatter", Brush.Scatter, 0.f, 1.f)) QueueBrush();
        }
        Controls.EndCard();
        Controls.BeginCard("", false);
        {
            AccentBar("Colour dynamics");
            if (Controls.Colour("Colour", Brush.Colour)) QueueBrush();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 4 * S);
            ImGui::TextColored(Spectrum(0x93938d), "%s", MaskSelected ? "Mask ink is grayscale; colour applies when painting layer channels." : "The colour Paint and Fill deposit on enabled channels.");
            ImGui::PopTextWrapPos();
        }
        Controls.EndCard();
        Controls.BeginCard("", false);
        {
            AccentBar("Pressure and surface");
            if (Controls.Range("Height strength", Brush.HeightStrength, 0.f, 2.f)) QueueBrush();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 4 * S);
            ImGui::TextColored(Spectrum(0x93938d), "%s", "Stylus pressure curves are configuration only in this native build; pointer input paints at full pressure.");
            ImGui::PopTextWrapPos();
        }
        Controls.EndCard();
    }
    else if (MaskSelected)
    {
        Controls.BeginCard("", false);
        {
            AccentBar("Digital mask target");
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 4 * S);
            ImGui::TextColored(Spectrum(0x93938d), "%s",
                "Paint changes grayscale coverage. Erase removes the painted overlay, restoring the mask's original source. Fill writes the active ink to a real region.");
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(1, 8 * S));
            float X = ImGui::GetCursorScreenPos().x;
            const float Y = ImGui::GetCursorScreenPos().y;
            ImGui::SetCursorScreenPos(ImVec2(X, Y));
            if (PillButton("StudioInkWhite", "White", 84 * S, MaskEditor.InkWhite)) { MaskEditor.InkWhite = true; DigitalActive = DigitalActive; }
            X += 88 * S;
            ImGui::SetCursorScreenPos(ImVec2(X, Y));
            if (PillButton("StudioInkBlack", "Black", 84 * S, !MaskEditor.InkWhite)) MaskEditor.InkWhite = false;
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, Y + 34 * S));
        }
        Controls.EndCard();
    }
    else if (auto *Layer = SelectedLayer())
    {
        Controls.BeginCard("", false);
        {
            AccentBar("Channel targets");
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 4 * S);
            ImGui::TextColored(Spectrum(0x93938d), "%s", "Enable the channels this brush writes. Erase only affects enabled channels.");
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(1, 8 * S));
            for (const auto &Channel : ChannelTable)
            {
                bool On = std::find(Layer->Channels.begin(), Layer->Channels.end(), Channel.Id) != Layer->Channels.end();
                if (Controls.Toggle(Channel.Label, On))
                {
                    ToggleChannel(*Layer, Channel.Id);
                    Layer = SelectedLayer();
                    if (!Layer) break;
                }
            }
        }
        Controls.EndCard();
    }
    else ImGui::TextDisabled("Select a layer to edit its channel targets.");
    ImGui::EndChild();
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        Caption(ImVec2(Origin.x, Origin.y + 8 * S), MaskSelected ? "Painting the selected mask" : "Painting layer channels", 8 * S, 0x8a8474);
        float X = Origin.x + ImGui::GetContentRegionAvail().x - 232 * S;
        ImGui::BeginDisabled(!CanUndo());
        ImGui::SetCursorScreenPos(ImVec2(X, Origin.y));
        if (IconButton("StudioUndo", "M9 14L4 9L9 4M4 9H14a6 6 0 0 1 0 12h-3", 28 * S)) Undo();
        ImGui::EndDisabled();
        X += 32 * S;
        ImGui::BeginDisabled(!CanRedo());
        ImGui::SetCursorScreenPos(ImVec2(X, Origin.y));
        if (IconButton("StudioRedo", "M15 14L20 9L15 4M20 9H10a6 6 0 0 0 0 12h3", 28 * S)) Redo();
        ImGui::EndDisabled();
        X += 40 * S;
        ImGui::SetCursorScreenPos(ImVec2(X, Origin.y));
        if (PillButton("StudioSave", "Save project", 116 * S, false)) SaveProject();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     STATUS BAR + TOP LEVEL
//------------------------------------------------------------------------------------------------------------------------

void TexturePaintWorkspacePanel::ConstructStatusBar()
{
    const float S = InterfaceScale;
    const auto *Viewport = ImGui::GetMainViewport();
    const float Height = 31 * S;
    const float Left = Viewport->WorkPos.x, Right = Left + Viewport->WorkSize.x;
    const float Bottom = Viewport->WorkPos.y + Viewport->WorkSize.y - Height;
    ImGui::SetNextWindowPos(ImVec2(Left, Bottom));
    ImGui::SetNextWindowSize(ImVec2(Right - Left, Height));
    ImGui::Begin("Texture paint status", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar);
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(ImVec2(Left, Bottom), ImVec2(Right, Bottom + Height), Colour32(0x0c0c0c));
    Ink->AddLine(ImVec2(Left, Bottom), ImVec2(Right, Bottom), Colour32(0xffffff, .03f));
    Ink->AddCircleFilled(ImVec2(Left + 18.5f * S, Bottom + Height * .5f), 2.5f * S, Colour32(0x8aa27e), 10);
    const char *Stage = "DIGITAL PAINT / LAYERS";
    const float StageX = Right - 16 * S - CaptionWidth(Stage, 7 * S);
    Ink->PushClipRect(ImVec2(Left, Bottom), ImVec2(StageX - 160 * S, Bottom + Height), true);
    Caption(ImVec2(Left + 26 * S, Bottom + Height * .5f - 4.5f * S), Status.c_str(), 8 * S, 0x74776b);
    Ink->PopClipRect();
    float X = (Left + Right) * .5f - 86 * S;
    const float Y = Bottom + 4.5f * S;
    ImGui::BeginDisabled(!CanUndo());
    ImGui::SetCursorScreenPos(ImVec2(X, Y));
    if (IconButton("StatusUndo", "M9 14L4 9L9 4M4 9H14a6 6 0 0 1 0 12h-3", 22 * S)) Undo();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Undo · Ctrl/⌘ Z");
    X += 26 * S;
    ImGui::BeginDisabled(!CanRedo());
    ImGui::SetCursorScreenPos(ImVec2(X, Y));
    if (IconButton("StatusRedo", "M15 14L20 9L15 4M20 9H10a6 6 0 0 0 0 12h3", 22 * S)) Redo();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Redo · Ctrl/⌘ Shift Z");
    X += 38 * S;
    Ink->AddLine(ImVec2(X - 7 * S, Bottom + 10 * S), ImVec2(X - 7 * S, Bottom + 20 * S), Colour32(0x33332c));
    ImGui::SetCursorScreenPos(ImVec2(X, Y));
    if (PillButton("LayerDocumentHelp", "Layer document", 118 * S, HelpOpen, "M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8ZM14 2v6h6")) HelpOpen = !HelpOpen;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Layer workspace help");
    Caption(ImVec2(StageX, Bottom + Height * .5f - 4 * S), Stage, 7 * S, 0x6c6d62);
    ImGui::End();
}

void TexturePaintWorkspacePanel::ConstructInterface()
{
    // 🧩 Shared studio palette, warm Texture accent (PanelChrome::StudioPalette).
    const auto SavedPalette = ControlKit::Palette();
    const auto Palette = PanelChrome::StudioPalette(SavedPalette, {.20f, .19f, .15f, 1}, {.85f, .70f, .51f, 1});
    ControlKit::ConfigurePalette(Palette);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Spectrum(0x121212));
    ImGui::PushStyleColor(ImGuiCol_Text, ThemeQuad(Palette.Text));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ThemeQuad(Palette.TextDim));
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
    ImGui::PushStyleColor(ImGuiCol_CheckMark, Spectrum(0xd9b281));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, Spectrum(0xd9b281));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, Spectrum(0xedc391));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, .055f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, Spectrum(0x2a2a26));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, Spectrum(0x35352f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, Spectrum(0x3c3c34));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6 * InterfaceScale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * InterfaceScale, 6 * InterfaceScale));
    // 🧩 Shared dock chrome (near-black title/tab colours, square windows, hidden scrollbars, no tab
    // close buttons) - one implementation in PanelChrome for every workspace, pushed last here and
    // popped first in the tail below.
    const auto Chrome = PanelChrome::PushDockChrome(Spectrum(0x090909), Spectrum(0x101010), true, true, InterfaceScale);

    // ⌨️ TexturePaintStudio's shortcuts: undo/redo, duplicate, save (frame keys live with the viewports).
    if (!ImGui::GetIO().WantTextInput)
    {
        const bool Command = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
        if (Command && ImGui::IsKeyPressed(ImGuiKey_Z)) { if (ImGui::GetIO().KeyShift) Redo(); else Undo(); }
        else if (Command && ImGui::IsKeyPressed(ImGuiKey_Y)) Redo();
        else if (Command && ImGui::IsKeyPressed(ImGuiKey_S)) SaveProject();
        else if (Command && ImGui::IsKeyPressed(ImGuiKey_D)) DuplicateSelectedLayer();
    }

    ConstructHeader();
    Panels.ConstructInterface(InterfaceScale);
    ConstructBrushStudio();
    ConstructStatusBar();

    const auto *Viewport = ImGui::GetMainViewport();
    const float S = InterfaceScale;
    const float Left = Viewport->WorkPos.x, Right = Left + Viewport->WorkSize.x;
    const float BottomEdge = Viewport->WorkPos.y + Viewport->WorkSize.y;
    if (HelpOpen)
    {
        ImGui::SetNextWindowPos(ImVec2(Right - 16 * S, BottomEdge - 85 * S), ImGuiCond_Always, ImVec2(1, 1));
        ImGui::SetNextWindowSize(ImVec2(std::min(430 * S, (Right - Left) * .92f), 0));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Spectrum(0x1d1d1a));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14 * S);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * S, 16 * S));
        ImGui::Begin("Layer workspace guide", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoResize);
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        Caption(Origin, "Part materials & UV authoring", 15 * S, 0xe8e2d2, LightTypeface ? LightTypeface : RegularTypeface);
        ImGui::SetCursorScreenPos(ImVec2(Origin.x + ImGui::GetContentRegionAvail().x - 26 * S, Origin.y - 3 * S));
        if (IconButton("CloseHelp", "M6 6L18 18M6 18L18 6", 26 * S)) HelpOpen = false;
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 26 * S));
        ImGui::PushTextWrapPos(Origin.x + ImGui::GetContentRegionAvail().x);
        ImGui::TextColored(Spectrum(0xb8b8ac), "%s",
            "Part-targeted material snapshots are saved with the project but are not shade-evaluated in this native build yet.");
        ImGui::Spacing();
        ImGui::TextColored(Spectrum(0xb8b8ac), "%s",
            "Choose UV / Texture to see the actual authoring charts in UDIMs 1001–1004. Paint there or on the 3D surface and the "
            "preview composite follows on the correct part. Source UV0 is read-only because its patches overlap.");
        ImGui::Spacing();
        ImGui::TextColored(Spectrum(0xb8b8ac), "%s", "Ctrl/⌘ Z Undo · Ctrl/⌘ D Duplicate · Ctrl/⌘ S Save project JSON · F Frame teapot.");
        ImGui::Spacing();
        ImGui::TextColored(Spectrum(0x8a8a7c), "%s",
            "Digital Paint / Erase / Fill works on the selected layer or mask in Lit and packed UV views as a per-triangle preview "
            "composite - not Alloy's GPU paint slices or exported PBR maps. Physical-media instruments are configuration only, not simulated.");
        ImGui::PopTextWrapPos();
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }
    if (!Error.empty())
    {
        const float ToastWidth = std::min(600 * S, (Right - Left) * 0.92f);
        const float TextWidth = std::max(80 * S, ToastWidth - 66 * S);
        const float ToastHeight = std::max(48 * S, ImGui::CalcTextSize(Error.c_str(), nullptr, false, TextWidth).y + 24 * S);
        ImGui::SetNextWindowPos(ImVec2((Left + Right) * .5f, Viewport->WorkPos.y + 55 * S), ImGuiCond_Always, ImVec2(.5f, 0.f));
        ImGui::SetNextWindowSize(ImVec2(ToastWidth, ToastHeight));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Spectrum(0x34281b));
        ImGui::PushStyleColor(ImGuiCol_Border, Spectrum(0x896541));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 13 * S);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * S, 12 * S));
        ImGui::Begin("Texture paint error", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + TextWidth);
        ImGui::TextColored(Spectrum(0xdec3a6), "%s", Error.c_str());
        ImGui::PopTextWrapPos();
        ImGui::SetCursorPos(ImVec2(ToastWidth - 38 * S, 11 * S));
        if (IconButton("DismissTextureError", "M6 6L18 18M6 18L18 6", 24 * S)) Error.clear();
        ImGui::End();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
    }
    if (!Toast.empty() && ClockSeconds - ToastClock < 2.8)
    {
        const float ToastWidth = CaptionWidth(Toast.c_str(), 9 * S) + 52 * S;
        ImGui::SetNextWindowPos(ImVec2((Left + Right) * .5f, BottomEdge - 92 * S), ImGuiCond_Always, ImVec2(.5f, 1.f));
        ImGui::SetNextWindowSize(ImVec2(ToastWidth, 36 * S));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Spectrum(0x23281f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18 * S);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Texture paint toast", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs);
        const ImVec2 Origin = ImGui::GetWindowPos();
        DrawGlyph("M20 6L9 17l-5-5", ImVec2(Origin.x + 14 * S, Origin.y + 11 * S), 13 * S, 0x9dc08d, 2.2f);
        Caption(ImVec2(Origin.x + 34 * S, Origin.y + 12 * S), Toast.c_str(), 9 * S, 0xd8e2cc);
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }
    else if (!Toast.empty()) Toast.clear();

    PanelChrome::PopDockChrome(Chrome);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(20);
    ControlKit::ConfigurePalette(SavedPalette);
}
} // namespace Frontier
