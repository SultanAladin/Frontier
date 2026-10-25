/*============================================================================================================================================
                                                          BAKINGWORKSPACEPANEL.CPP
============================================================================================================================================*/
#include "UI/TextureStudio/BakingWorkspacePanel.h"
#include "Engine/Interface/ControlKit.h"
#include "Engine/Interface/GlyphSpace.h"
#include <stb_image_write.h>
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier
{
namespace
{
ImVec4 Spectrum(uint32_t Colour, float Alpha = 1.f) noexcept
{
    return ImVec4(float((Colour >> 16) & 0xff) / 255.f, float((Colour >> 8) & 0xff) / 255.f, float(Colour & 0xff) / 255.f, Alpha);
}
// 🎨 Same hex colour, but as the ColorQuad GlyphSpace::Stroke's GlyphPlacement expects (its own hand-drawn
// icons, e.g. the .bk-empty stage glyph, are not ImGui-styled widgets going through ConvertSpectrum).
ColorQuad SpectrumQuad(uint32_t Colour, float Alpha = 1.f) noexcept
{
    return ColorQuad{float((Colour >> 16) & 0xff) / 255.f, float((Colour >> 8) & 0xff) / 255.f, float(Colour & 0xff) / 255.f, Alpha};
}
// 🎨 Same helper as Pattern/Stamp's ThemeQuad: lets this panel's own card chrome read straight from
// ControlKit::Palette() so it tracks the applied theme instead of a hard-coded colour set.
ImVec4 ThemeQuad(const ColorQuad &Colour, float AlphaOverride = -1.0f) noexcept
{
    return {Colour.Red, Colour.Green, Colour.Blue, AlphaOverride >= 0.0f ? AlphaOverride : Colour.Alpha};
}
// 🎨 Absolute-position draw-list text, matching the Caption() helper duplicated in Pattern/StampWorkspacePanel.cpp -
// lets headings/captions sit at exact pixel offsets instead of flowing through ImGui's sequential layout, which is
// how the approved native ports reproduce the web reference's fixed typographic grid.
void Caption(ImVec2 Position, const char *Text, float Size, uint32_t Colour, void *Typeface = nullptr)
{
    ImGui::GetWindowDrawList()->AddText(Typeface ? static_cast<ImFont *>(Typeface) : ImGui::GetFont(), Size * 1.302f, Position, ImGui::GetColorU32(Spectrum(Colour)), Text);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     MESH MAP METADATA (1:1 WITH meshMaps.js)
//------------------------------------------------------------------------------------------------------------------------

struct MeshMapMeta
{
    const char *Key;
    const char *Label;
    const char *Family;
    const char *Description;
    const char *Swatch; // 📝 1:1 with meshMaps.js's `swatch:` key - drives DrawMapSwatch's gradient family below.
    // 📝 1:1 with MeshMapGrid.jsx's <small> tile caption logic (m.derived ? "Derived mask / effect" : m.space ?
    // `${m.space} space` : m.type === "scalar" ? "Grayscale" : m.type === "color" ? "RGB" : "Vector data") - the
    // tile itself only ever shows this short classifier; the full Description reads in the inspector's Map
    // inspector section and the tooltip, exactly like the web reference's terse-tile/verbose-inspector split.
    const char *Hint;
};
// 📝 Order, grouping and swatch family all mirror Experimental/TextureStudio/src/baking/meshMaps.js exactly;
// families render as section headers in the tile grid in this same order (Normals, Surface, Coordinates & IDs,
// Masks, Color), and each tile's preview swatch uses the matching .bk-swatch-* gradient family from studioTheme.css.
const std::vector<MeshMapMeta> &MeshMapTable()
{
    static const std::vector<MeshMapMeta> Table{
        {"normal", "Tangent normal", "Normals", "Projected high-surface normal in the low mesh UV tangent frame.", "normal", "Tangent space"},
        {"world-normal", "World normal", "Normals", "Projected high-surface normal in scene/world axes.", "normal-world", "World space"},
        {"object-normal", "Object normal", "Normals", "Projected normal in the low target's original local axes.", "normal-object", "Object space"},
        {"bent-normal", "Bent tangent normal", "Normals", "Average unoccluded hemisphere direction, tangent frame.", "normal", "Tangent space"},
        {"bent-world-normal", "Bent world normal", "Normals", "Average unoccluded hemisphere direction, world axes.", "normal-world", "World space"},
        {"bent-object-normal", "Bent object normal", "Normals", "Average unoccluded hemisphere direction, object axes.", "normal-object", "Object space"},
        {"bevel-normal", "Bevel normal", "Normals", "Geometry-derived rounded-edge normal approximation.", "normal", "Derived mask / effect"},
        {"occlusion", "Ambient occlusion", "Surface", "Hemisphere visibility against the high mesh.", "ao", "Grayscale"},
        {"occlusion-secondary", "Secondary AO", "Surface", "A second hemisphere-visibility bake, its own distance.", "ao", "Grayscale"},
        {"curvature", "Curvature", "Surface", "Signed local normal variation estimate.", "curvature", "Grayscale"},
        {"convexity", "Convexity", "Surface", "Positive signed curvature isolated as a bright-edge mask.", "curvature", "Grayscale"},
        {"cavity", "Cavity / concavity", "Surface", "Negative signed curvature isolated as a bright-recess mask.", "ao", "Grayscale"},
        {"thickness", "Thickness", "Surface", "Inward high-mesh ray distance divided by thickness range.", "height", "Grayscale"},
        {"height", "Projection height", "Surface", "Signed high-to-low distance along the low normal.", "height", "Grayscale"},
        {"vector-displacement", "Vector displacement", "Surface", "Full projected high-minus-low XYZ offset.", "normal-object", "World space"},
        {"position", "Position", "Coordinates & IDs", "Projected high-surface XYZ normalized to the world bbox.", "position", "Vector data"},
        {"id", "Object ID", "Coordinates & IDs", "24-bit source-object identity, little-endian RGB.", "id", "RGB"},
        {"uv-island", "UV island ID", "Coordinates & IDs", "A distinct display colour per connected low UV chart.", "id", "RGB"},
        {"uv", "UV coordinates", "Coordinates & IDs", "Low target UV0 coordinates in R/G, blue = 1.", "position", "Vector data"},
        {"wireframe", "UV wireframe", "Coordinates & IDs", "Low triangle edges in texture space.", "wireframe", "Grayscale"},
        {"coverage", "UV coverage", "Masks", "White on covered low UV texels; black outside.", "coverage", "Grayscale"},
        {"alpha", "Alpha / hit mask", "Masks", "White where a high surface was projected, black for misses.", "coverage", "Grayscale"},
        {"bevel-mask", "Bevel mask", "Masks", "Angular change from rounded-edge normal averaging.", "curvature", "Derived mask / effect"},
        {"dust", "Dust mask", "Masks", "Derived upward-facing deposition mask.", "dust", "Derived mask / effect"},
        {"dirt", "Dirt mask", "Masks", "Derived occlusion plus concavity mask.", "ao", "Derived mask / effect"},
        {"edge-wear", "Edge wear mask", "Masks", "Derived convexity weighted by surface exposure.", "curvature", "Derived mask / effect"},
        {"base-color", "Vertex color", "Color", "Interpolated high vertex colour, sRGB; white if none.", "color", "RGB"},
    };
    return Table;
}
// 🎨 Approximates the matching .bk-swatch-* CSS gradient from studioTheme.css as a 2-4 stop linear blend drawn
// with AddRectFilledMultiColor (plus a couple of cheap overlay shapes for the conic/radial-looking families) -
// the same visual family per mesh-map group the web reference uses for its placeholder tile art before a real
// bake thumbnail exists.
void DrawMapSwatch(ImDrawList *Ink, ImVec2 Minimum, ImVec2 Maximum, const char *Swatch) noexcept
{
    const auto Colour = [](uint32_t Hex) { return IM_COL32((Hex >> 16) & 0xff, (Hex >> 8) & 0xff, Hex & 0xff, 255); };
    const auto Diagonal = [&](uint32_t A, uint32_t B) { Ink->AddRectFilledMultiColor(Minimum, Maximum, Colour(A), Colour(B), Colour(B), Colour(A)); };
    const std::string Key = Swatch ? Swatch : "";
    if (Key == "normal") Diagonal(0xed7ce5, 0x8397ff);
    else if (Key == "normal-world") Diagonal(0x93cf98, 0xcf779d);
    else if (Key == "normal-object") Diagonal(0xd37390, 0x81c996);
    else if (Key == "id") Diagonal(0xdc976f, 0x81a6cc);
    else if (Key == "position") Diagonal(0xa2bf93, 0x9dbac7);
    else if (Key == "ao") Ink->AddRectFilled(Minimum, Maximum, Colour(0x60635f)), Ink->AddCircleFilled(ImVec2((Minimum.x + Maximum.x) * .5f, (Minimum.y + Maximum.y) * .5f), (Maximum.x - Minimum.x) * .32f, Colour(0xc5c5be), 24);
    else if (Key == "height") Diagonal(0x343a37, 0xdadad1);
    else if (Key == "curvature")
    {
        Ink->AddRectFilled(Minimum, Maximum, Colour(0x252824));
        Ink->AddCircleFilled(ImVec2((Minimum.x + Maximum.x) * .5f, (Minimum.y + Maximum.y) * .5f), (Maximum.x - Minimum.x) * .3f, Colour(0xd9dcd2), 24);
    }
    else if (Key == "coverage") Ink->AddRectFilled(Minimum, Maximum, Colour(0x111811)), Ink->AddCircleFilled(ImVec2((Minimum.x + Maximum.x) * .5f, (Minimum.y + Maximum.y) * .5f), (Maximum.x - Minimum.x) * .4f, Colour(0xe4e8df), 24);
    else if (Key == "dust") Diagonal(0xd1cbb2, 0x2a3328);
    else if (Key == "wireframe") { Ink->AddRectFilled(Minimum, Maximum, Colour(0x1c251e)); for (float X = Minimum.x; X < Maximum.x + (Maximum.y - Minimum.y); X += 10.f) Ink->AddLine(ImVec2(X, Maximum.y), ImVec2(X - (Maximum.y - Minimum.y), Minimum.y), Colour(0x98a38d), 1.f); }
    else if (Key == "color") Diagonal(0xac815c, 0x709a8e);
    else Diagonal(0x565958, 0x1b1d1c);
}
const MeshMapMeta *FindMeshMap(const std::string &Key)
{
    for (const auto &Entry : MeshMapTable())
        if (Key == Entry.Key) return &Entry;
    return nullptr;
}
const std::map<std::string, std::vector<std::string>> &MeshMapPresets()
{
    static const std::map<std::string, std::vector<std::string>> Presets{
        {"Essential", {"normal", "world-normal", "occlusion", "curvature", "position", "thickness"}},
        {"Masks", {"occlusion", "curvature", "convexity", "cavity", "uv-island", "dust", "dirt", "edge-wear", "bevel-mask", "alpha"}},
        {"Normals",
            {"normal", "world-normal", "object-normal", "bent-normal", "bent-world-normal", "bent-object-normal", "bevel-normal"}},
    };
    return Presets;
}
constexpr const char *kDefaultChannels[]{"normal", "world-normal", "occlusion", "curvature", "thickness", "position", "id", "height",
    "coverage"};

//------------------------------------------------------------------------------------------------------------------------
//                                                     MINIMAL 3D CAMERA MATH
//------------------------------------------------------------------------------------------------------------------------

struct Vec3 { float X = 0, Y = 0, Z = 0; };
Vec3 Sub(Vec3 A, Vec3 B) noexcept { return {A.X - B.X, A.Y - B.Y, A.Z - B.Z}; }
Vec3 CrossV(Vec3 A, Vec3 B) noexcept { return {A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X}; }
float DotV(Vec3 A, Vec3 B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
Vec3 NormalizeV(Vec3 V) noexcept
{
    const float Length = std::sqrt(DotV(V, V));
    return Length > 1e-9f ? Vec3{V.X / Length, V.Y / Length, V.Z / Length} : V;
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

BakingWorkspacePanel::BakingWorkspacePanel() { Settings.Channels.assign(std::begin(kDefaultChannels), std::end(kDefaultChannels)); }

bool BakingWorkspacePanel::InitializeContent() noexcept
{
    try
    {
        SelectDemo("Teapot");
        Info = "Source meshes stay in this session. Recipes contain settings, not geometry.";
        return true;
    }
    catch (...) { return false; }
}
void BakingWorkspacePanel::FinalizeContent() noexcept
{
    if (BakeFuture.valid())
    {
        try { BakeFuture.wait(); } catch (...) {}
    }
    ControlExtents.clear();
}
void BakingWorkspacePanel::IntegrateContent(double DeltaSeconds) noexcept { SettingsCoalesceClockSeconds += DeltaSeconds; PollBake(); }

//------------------------------------------------------------------------------------------------------------------------
//                                                     DOCUMENT / BAKE OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

void BakingWorkspacePanel::SelectDemo(const std::string &Shape)
{
    if (BakeRunning.load()) return;
    Assets = BuildDemoBakeAssets(Shape);
    DemoShape = Shape;
    SelectedTargets.clear();
    for (const auto &Part : Assets.Low.Parts) SelectedTargets.push_back(Part.Name);
    PreviewSetName = Assets.Low.Parts.empty() ? "" : Assets.Low.Parts.front().Name;
    ClearResults();
    Info = "Built-in low meshes use non-overlapping UVs. Imported low meshes require their own UV layouts.";
    ResetCamera();
}
void BakingWorkspacePanel::ClearResults() noexcept { Results.clear(); }

PlaneExtent BakingWorkspacePanel::QueryControlExtent(const std::string &Caption) const
{
    const auto Position = ControlExtents.find(Caption);
    return Position == ControlExtents.end() ? PlaneExtent{} : Position->second;
}

void BakingWorkspacePanel::Track(const std::string &Caption)
{
    const ImVec2 Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
    ControlExtents[Caption] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
}
// 🧩 1:1 behaviour with BakingStudio.jsx's travel()/setSettingsDocument() history: every inspector field change
// is coalesced into a single undo step while it is still being actively edited (continuous drags/typing), and a
// new step only starts once 650ms have passed since the last push - not one undo entry per dragged pixel.
bool BakingWorkspacePanel::SettingsFieldsEqual(const BakeSettings &A, const BakeSettings &B) noexcept
{
    return A.Resolution == B.Resolution && A.Padding == B.Padding && A.NormalPlusY == B.NormalPlusY &&
        A.ProjectionRay == B.ProjectionRay && A.Front == B.Front && A.Back == B.Back && A.AoDistance == B.AoDistance &&
        A.SecondaryAoDistance == B.SecondaryAoDistance && A.ThicknessRange == B.ThicknessRange &&
        A.CurvatureRadius == B.CurvatureRadius && A.Samples == B.Samples && A.MatchByName == B.MatchByName &&
        A.Channels == B.Channels;
}
void BakingWorkspacePanel::CoalesceSettingsUndo()
{
    if (SettingsFieldsEqual(Settings, SettingsAtFrameStart)) return;
    if (SettingsUndoStack.empty() || SettingsCoalesceClockSeconds - LastSettingsPushSeconds > 0.65)
        SettingsUndoStack.push_back(SettingsAtFrameStart);
    LastSettingsPushSeconds = SettingsCoalesceClockSeconds;
    SettingsRedoStack.clear();
}
void BakingWorkspacePanel::UndoSettings()
{
    if (SettingsUndoStack.empty()) return;
    SettingsRedoStack.push_back(Settings);
    Settings = SettingsUndoStack.back();
    SettingsUndoStack.pop_back();
    LastSettingsPushSeconds = -1000.0;
}
void BakingWorkspacePanel::RedoSettings()
{
    if (SettingsRedoStack.empty()) return;
    SettingsUndoStack.push_back(Settings);
    Settings = SettingsRedoStack.back();
    SettingsRedoStack.pop_back();
    LastSettingsPushSeconds = -1000.0;
}
void BakingWorkspacePanel::StartBake()
{
    if (BakeRunning.load() || SelectedTargets.empty() || Settings.Channels.empty()) return;
    std::vector<const BakePart *> Targets;
    for (const auto &Name : SelectedTargets)
        for (const auto &Part : Assets.Low.Parts)
            if (Part.Name == Name) Targets.push_back(&Part);
    if (Targets.empty()) return;
    BakeRunning.store(true);
    BakeDone.store(0);
    BakeTotal.store(1);
    Error.clear();
    BakeProgressLabel = "Baking mesh maps…";
    SettingsAtLastBake = Settings;
    // 📝 Copy by value into the worker: Assets/Settings must outlive the bake even if the panel is torn
    // down mid-bake (FinalizeContent waits on BakeFuture first, so this is belt-and-braces, not load-bearing).
    const BakeMeshSet HighCopy = Assets.High;
    const BakeSettings SettingsCopy = Settings;
    std::vector<BakePart> TargetCopies;
    for (const auto *Part : Targets) TargetCopies.push_back(*Part);
    BakeFuture = std::async(std::launch::async, [this, HighCopy, SettingsCopy, TargetCopies]() mutable
    {
        std::vector<const BakePart *> Pointers;
        for (const auto &Part : TargetCopies) Pointers.push_back(&Part);
        return RunMeshBake(Pointers, HighCopy, SettingsCopy, [this](const BakeProgress &Progress)
        {
            BakeDone.store(Progress.Done);
            BakeTotal.store(std::max<uint64_t>(1, Progress.Total));
        });
    });
}
void BakingWorkspacePanel::PollBake()
{
    if (!BakeRunning.load() || !BakeFuture.valid()) return;
    if (BakeFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try
    {
        Results = BakeFuture.get();
        uint64_t Misses = 0;
        for (const auto &Result : Results) Misses += Result.Stats.Misses;
        char Buffer[256];
        std::snprintf(Buffer, sizeof(Buffer), "%zu texture set%s baked. %llu missed projection%s%s.", Results.size(),
            Results.size() == 1 ? "" : "s", (unsigned long long)Misses, Misses == 1 ? "" : "s",
            Misses ? " — check ray distance/alignment" : "");
        Info = Buffer;
        if (!Results.empty())
        {
            PreviewSetName = Results.front().Name;
            PreviewMapKey = Settings.Channels.empty() ? "normal" : Settings.Channels.front();
            View = ViewMode::Maps2D;
        }
    }
    catch (const std::exception &Failure) { Error = Failure.what(); }
    BakeRunning.store(false);
}
void BakingWorkspacePanel::ExportResults()
{
    if (Results.empty()) { Error = "Bake mesh maps before exporting."; return; }
    try
    {
        std::filesystem::path Directory = ExportDirectory.empty() ? std::filesystem::temp_directory_path() / "FrontierBakeExport" : ExportDirectory;
        std::filesystem::create_directories(Directory);
        uint32_t Written = 0;
        for (const auto &Result : Results)
            for (const auto &Map : Result.Maps)
            {
                const auto Path = Directory / (Result.Name + "-" + Map.Key + ".png");
                if (stbi_write_png(Path.string().c_str(), int(Result.Resolution), int(Result.Resolution), 4, Map.Rgba.data(), int(Result.Resolution) * 4))
                    ++Written;
            }
        LastExportDirectory = Directory.string();
        char Buffer[320];
        std::snprintf(Buffer, sizeof(Buffer), "%u map PNGs written to %s", Written, LastExportDirectory.c_str());
        Info = Buffer;
    }
    catch (const std::exception &Failure) { Error = std::string("Export failed: ") + Failure.what(); }
}
void BakingWorkspacePanel::ApplyPreset(const std::string &Preset)
{
    const auto &Presets = MeshMapPresets();
    if (Preset == "Clear") { Settings.Channels.clear(); return; }
    if (Preset == "All")
    {
        Settings.Channels.clear();
        for (const auto &Entry : MeshMapTable())
            if (IsMeshMapImplemented(Entry.Key)) Settings.Channels.push_back(Entry.Key);
        return;
    }
    const auto Found = Presets.find(Preset);
    if (Found == Presets.end()) return;
    Settings.Channels = Found->second;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     HEADER (WORKSPACE SWITCHER)
//------------------------------------------------------------------------------------------------------------------------

void BakingWorkspacePanel::ConstructHeader()
{
    const auto *Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(Viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(Viewport->WorkSize.x, 43 * InterfaceScale));
    ImGui::Begin("Baking header", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoScrollbar);
    const auto Origin = ImGui::GetWindowPos();
    const float Width = ImGui::GetWindowWidth();
    ImGui::GetWindowDrawList()->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + 43 * InterfaceScale), ImGui::GetColorU32(Spectrum(0x0b0b0b)));
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 16 * InterfaceScale, Origin.y + 13 * InterfaceScale));
    ImGui::TextColored(Spectrum(0xc5c5b8), "%s", Assets.Low.Label.c_str());
    ImGui::SameLine();
    ImGui::TextColored(Spectrum(0x67675b), ".bake");

    const float NavigationX = Origin.x + std::max(230.f * InterfaceScale, Width - 620.f * InterfaceScale);
    const char *Workspaces[]{"Material", "Pattern", "Texture", "Stamp", "Baking"};
    const float Widths[]{64, 58, 54, 52, 64};
    float PositionX = NavigationX;
    for (size_t Index = 0; Index < 5; ++Index)
    {
        const bool Available = Index == 0 || Index == 1 || Index == 3 || Index == 4;
        ImGui::SetCursorScreenPos(ImVec2(PositionX, Origin.y + 9 * InterfaceScale));
        ImGui::BeginDisabled(!Available || (Index == 0 && !ActivateMaterial) || (Index == 1 && !ActivatePattern) || (Index == 3 && !ActivateStamp));
        ImGui::PushStyleColor(ImGuiCol_Button, Index == 4 ? Spectrum(0x2a2a2a) : Spectrum(0x161616));
        if (ImGui::Button(Workspaces[Index], ImVec2(Widths[Index] * InterfaceScale, 24 * InterfaceScale)))
        {
            if (Index == 0 && ActivateMaterial) ActivateMaterial();
            if (Index == 1 && ActivatePattern) ActivatePattern();
            if (Index == 3 && ActivateStamp) ActivateStamp();
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", Available ? "Switch workspace without losing your document." : "This native workspace is not hosted yet.");
        ImGui::EndDisabled();
        PositionX += Widths[Index] * InterfaceScale;
    }
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + Width - 44 * InterfaceScale, Origin.y + 9 * InterfaceScale));
    ImGui::BeginDisabled(!ActivateMaterial);
    if (ImGui::Button("X", ImVec2(28 * InterfaceScale, 24 * InterfaceScale)) && ActivateMaterial) ActivateMaterial();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close baking studio");
    ImGui::EndDisabled();
    ImGui::End();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     MESH MAP GRID
//------------------------------------------------------------------------------------------------------------------------

// 🧩 1:1 with Experimental/TextureStudio/src/baking/MeshMapGrid.jsx + its .bk-map-tile/.bk-map-swatch chrome
// in studioTheme.css: a 2-column grid of rounded 13px tile cards, each with a 51px gradient swatch (a green
// check badge when selected), a bold label and a small caption, grouped under uppercase family headers -
// not a row of plain ImGui buttons.
void BakingWorkspacePanel::ConstructMeshMapGrid(float Width)
{
    const float S = InterfaceScale;
    Caption(ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + 2 * S), "MESH MAPS", 13 * S, 0xd0d0bb, LightTypeface ? LightTypeface : RegularTypeface);
    ImGui::Dummy(ImVec2(1, 18 * S));
    const char *Presets[]{"Essential", "Masks", "Normals", "All", "Clear"};
    Controls.ControlHeight = 22;
    for (const char *Preset : Presets)
    {
        if (Controls.Button(Preset, 0, false, {}, true)) ApplyPreset(Preset);
        Track(std::string("Preset ") + Preset);
        ImGui::SameLine(0, 5 * S);
    }
    ImGui::NewLine();
    ImGui::Dummy(ImVec2(1, 10 * S));

    const float Gap = 7 * S;
    const uint32_t Columns = 2;
    const float TileWidth = (Width - Gap * float(Columns - 1)) / float(Columns);
    const char *LastFamily = "";
    uint32_t Column = 0;
    ImVec2 RowOrigin = ImGui::GetCursorScreenPos();
    auto *Ink = ImGui::GetWindowDrawList();
    for (const auto &Entry : MeshMapTable())
    {
        if (std::strcmp(LastFamily, Entry.Family) != 0)
        {
            if (Column != 0) { ImGui::NewLine(); Column = 0; }
            if (*LastFamily) ImGui::Dummy(ImVec2(1, 8 * S));
            const ImVec2 HeadingPosition = ImGui::GetCursorScreenPos();
            Caption(ImVec2(HeadingPosition.x, HeadingPosition.y + 2 * S), Entry.Family, 9 * S, 0xaaa999);
            ImGui::Dummy(ImVec2(1, 17 * S));
            LastFamily = Entry.Family;
            RowOrigin = ImGui::GetCursorScreenPos();
        }
        const bool Implemented = IsMeshMapImplemented(Entry.Key);
        const bool Enabled = std::find(Settings.Channels.begin(), Settings.Channels.end(), std::string(Entry.Key)) != Settings.Channels.end();
        const ImVec2 TileMinimum(RowOrigin.x + Column * (TileWidth + Gap), RowOrigin.y);
        const float TileHeight = 82 * S;
        const ImVec2 TileMaximum(TileMinimum.x + TileWidth, TileMinimum.y + TileHeight);

        ImGui::PushID(Entry.Key);
        ImGui::SetCursorScreenPos(TileMinimum);
        ImGui::InvisibleButton("Tile", ImVec2(TileWidth, TileHeight));
        const bool Hovered = Implemented && ImGui::IsItemHovered();
        if (Implemented && ImGui::IsItemClicked())
        {
            InspectMapKey = Entry.Key;
            if (Enabled) Settings.Channels.erase(std::remove(Settings.Channels.begin(), Settings.Channels.end(), std::string(Entry.Key)), Settings.Channels.end());
            else Settings.Channels.push_back(Entry.Key);
        }
        Track(std::string("Tile ") + Entry.Key);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\n%s", Entry.Description, Implemented ? (Enabled ? "Enabled · will be baked" : "Disabled") : "Not yet implemented in this native build.");

        const uint32_t TileFill = Enabled ? 0x163427 : Hovered ? 0x272b26 : 0x181918;
        const uint32_t TileBorder = Enabled ? 0x63c68e : Hovered ? 0x6b8c75 : 0x3a3a35;
        const float Alpha = Implemented ? 1.f : 0.4f;
        Ink->AddRectFilled(TileMinimum, TileMaximum, ImGui::GetColorU32(Spectrum(TileFill, Alpha)), 13 * S);
        Ink->AddRect(TileMinimum, TileMaximum, ImGui::GetColorU32(Spectrum(TileBorder, Alpha)), 13 * S);

        const ImVec2 SwatchMinimum(TileMinimum.x + 8 * S, TileMinimum.y + 8 * S), SwatchMaximum(TileMaximum.x - 8 * S, TileMinimum.y + 8 * S + 51 * S);
        Ink->PushClipRect(SwatchMinimum, SwatchMaximum, true);
        DrawMapSwatch(Ink, SwatchMinimum, SwatchMaximum, Entry.Swatch);
        Ink->PopClipRect();
        Ink->AddRect(SwatchMinimum, SwatchMaximum, ImGui::GetColorU32(Spectrum(0x000000, 0.25f * Alpha)), 8 * S);
        if (Enabled)
        {
            const float Radius = 7 * S;
            const ImVec2 BadgeCentre(SwatchMaximum.x - 4 * S - Radius, SwatchMinimum.y + 4 * S + Radius);
            Ink->AddCircleFilled(BadgeCentre, Radius, ImGui::GetColorU32(Spectrum(0x72d79d)));
            Ink->AddLine(ImVec2(BadgeCentre.x - 3 * S, BadgeCentre.y), ImVec2(BadgeCentre.x - 1 * S, BadgeCentre.y + 2.5f * S), ImGui::GetColorU32(Spectrum(0x082412)), 1.6f * S);
            Ink->AddLine(ImVec2(BadgeCentre.x - 1 * S, BadgeCentre.y + 2.5f * S), ImVec2(BadgeCentre.x + 3.5f * S, BadgeCentre.y - 3 * S), ImGui::GetColorU32(Spectrum(0x082412)), 1.6f * S);
        }
        Caption(ImVec2(TileMinimum.x + 8 * S, SwatchMaximum.y + 6 * S), Entry.Label, 10 * S, Enabled ? 0xdbf6e4 : Implemented ? 0xb8b9ad : 0x6b6b61);
        Caption(ImVec2(TileMinimum.x + 8 * S, SwatchMaximum.y + 20 * S), Implemented ? Entry.Hint : "Not yet native", 8 * S,
            Enabled ? 0x99c9ab : 0x828a7f);
        ImGui::PopID();

        if (++Column >= Columns) { Column = 0; RowOrigin.y += TileHeight + Gap; }
    }
    if (Column != 0) RowOrigin.y += 82 * S + Gap;
    ImGui::SetCursorScreenPos(ImVec2(RowOrigin.x, RowOrigin.y));
    ImGui::Dummy(ImVec2(1, 1));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ASSETS DOCK (LEFT)
//------------------------------------------------------------------------------------------------------------------------

// 🧩 1:1 with BakingStudio.jsx's <aside className="bk-dock bk-assets"> + studioTheme.css's .bk-pane-heading/
// .bk-mode-switch/.bk-card chrome: a big light-weight "Baking" heading, a pill-shaped 3-way mode switch, then
// rounded gradient "Low UV targets"/"High source" cards with a big triangle-count numeral - not plain
// sequential ImGui text/buttons.
void BakingWorkspacePanel::ConstructAssetsDock(PlaneExtent Region)
{
    const float S = InterfaceScale;
    ImGui::SetNextWindowPos(ImVec2(Region.MinimumX, Region.MinimumY));
    ImGui::SetNextWindowSize(ImVec2(Region.Width(), Region.Height()));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Spectrum(0x121212));
    ImGui::Begin("Bake project", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    Controls.Scale = S;

    // .bk-pane-heading: h1 23px/weight-300 + small 8px muted subtitle, baseline-aligned.
    const ImVec2 HeadingOrigin = ImGui::GetCursorScreenPos();
    Caption(ImVec2(HeadingOrigin.x + 20 * S, HeadingOrigin.y + 6 * S), "Baking", 23 * S, 0xe8e8db, LightTypeface ? LightTypeface : RegularTypeface);
    Caption(ImVec2(HeadingOrigin.x + 20 * S, HeadingOrigin.y + 32 * S), "Map-only workflow", 8 * S, 0x727264);
    ImGui::Dummy(ImVec2(1, 48 * S));

    // .bk-mode-switch: a pill capsule bar with three equal-width segments.
    {
        const float BarX = ImGui::GetCursorScreenPos().x + 6 * S, BarY = ImGui::GetCursorScreenPos().y;
        const float BarWidth = ImGui::GetContentRegionAvail().x - 12 * S, BarHeight = 30 * S;
        auto *Ink = ImGui::GetWindowDrawList();
        Ink->AddRectFilled(ImVec2(BarX, BarY), ImVec2(BarX + BarWidth, BarY + BarHeight), ImGui::GetColorU32(Spectrum(0x0f0f0d)), BarHeight * .5f);
        Ink->AddRect(ImVec2(BarX, BarY), ImVec2(BarX + BarWidth, BarY + BarHeight), ImGui::GetColorU32(Spectrum(0xffffff, 0.03f)), BarHeight * .5f);
        const char *Modes[]{"Mesh maps", "Material patch", "Volume SDF"};
        const float SegmentWidth = (BarWidth - 6 * S) / 3.f;
        for (int Index = 0; Index < 3; ++Index)
        {
            const ImVec2 SegMin(BarX + 3 * S + Index * SegmentWidth, BarY + 3 * S), SegMax(SegMin.x + SegmentWidth - 2 * S, BarY + BarHeight - 3 * S);
            const bool Selected = int(Mode) == Index;
            ImGui::SetCursorScreenPos(SegMin);
            ImGui::PushID(Index);
            ImGui::InvisibleButton("Mode", ImVec2(SegMax.x - SegMin.x, SegMax.y - SegMin.y));
            if (ImGui::IsItemClicked()) Mode = StudioMode(Index);
            if (Selected) Ink->AddRectFilled(SegMin, SegMax, ImGui::GetColorU32(Spectrum(0x2a2a26)), (SegMax.y - SegMin.y) * .5f);
            const auto Extent = ImGui::CalcTextSize(Modes[Index]);
            Caption(ImVec2(SegMin.x + ((SegMax.x - SegMin.x) - Extent.x * 0.72f) * .5f, SegMin.y + ((SegMax.y - SegMin.y) - Extent.y * 0.72f) * .5f), Modes[Index],
                9 * S, Selected ? 0xdddec7 : 0x8d8d7e);
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(ImVec2(HeadingOrigin.x, BarY + BarHeight + 15 * S));
    }

    const ImVec2 SectionPosition = ImGui::GetCursorScreenPos();
    Caption(ImVec2(SectionPosition.x + 17 * S, SectionPosition.y + 2 * S), "SOURCE GEOMETRY", 8 * S, 0x8e8e7b);
    Caption(ImVec2(SectionPosition.x + 17 * S, SectionPosition.y + 13 * S), Assets.Low.Source.c_str(), 7 * S, 0x666657);
    ImGui::SetCursorScreenPos(ImVec2(SectionPosition.x + 8 * S, SectionPosition.y + 26 * S));
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - 16 * S, 1));
    ImGui::PopStyleVar();

    const auto LowSummary = SummarizeMesh(Assets.Low);
    Controls.BeginCard("", true);
    {
        auto *Ink = ImGui::GetWindowDrawList();
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        Ink->AddRectFilled(ImVec2(Origin.x - 14 * S, Origin.y - 14 * S), ImVec2(Origin.x - 11 * S, Origin.y - 2 * S), ImGui::GetColorU32(Spectrum(0x8b9b82)), 1.5f * S);
        Caption(ImVec2(Origin.x, Origin.y - 12 * S), "Low UV targets", 11 * S, 0xd0d0bb);
        char ObjectsLabel[32]; std::snprintf(ObjectsLabel, sizeof(ObjectsLabel), "%u objects", LowSummary.Objects);
        const auto HeadingWidth = ImGui::CalcTextSize("Low UV targets").x;
        Caption(ImVec2(Origin.x + HeadingWidth + 8 * S, Origin.y - 11 * S), ObjectsLabel, 8 * S, 0x7a7a69);
        ImGui::Dummy(ImVec2(1, 14 * S));
        char TriangleLabel[32]; std::snprintf(TriangleLabel, sizeof(TriangleLabel), "%llu", (unsigned long long)LowSummary.Triangles);
        Caption(ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y), TriangleLabel, 24 * S, 0xd3d6bf, LightTypeface ? LightTypeface : RegularTypeface);
        Caption(ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + 30 * S), LowSummary.HasUv ? "triangles · UV0 present" : "triangles · UV0 missing", 7 * S, 0x82846c);
        ImGui::Dummy(ImVec2(1, 48 * S));
        ImGui::BeginDisabled(BakeRunning.load());
        Controls.ControlHeight = 30;
        if (Controls.Button("Import low mesh", ImGui::GetContentRegionAvail().x, false, {}, false))
            Info = "Mesh import (OBJ/GLB/PLY/STL) is not implemented in this native build yet; use a built-in study mesh.";
        ImGui::EndDisabled();
        ImGui::Dummy(ImVec2(1, 10 * S));
        for (const auto &Part : Assets.Low.Parts)
        {
            bool Checked = std::find(SelectedTargets.begin(), SelectedTargets.end(), Part.Name) != SelectedTargets.end();
            ImGui::BeginDisabled(BakeRunning.load());
            ImGui::PushStyleColor(ImGuiCol_Text, Spectrum(0xb9bca3));
            if (ImGui::Checkbox(Part.Name.c_str(), &Checked))
            {
                if (Checked) SelectedTargets.push_back(Part.Name);
                else SelectedTargets.erase(std::remove(SelectedTargets.begin(), SelectedTargets.end(), Part.Name), SelectedTargets.end());
            }
            ImGui::PopStyleColor();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextColored(Spectrum(0x757967), "%u tris", Part.TriangleCount());
        }
    }
    Controls.EndCard();

    const auto HighSummary = SummarizeMesh(Assets.High);
    Controls.BeginCard("", true);
    {
        auto *Ink = ImGui::GetWindowDrawList();
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        Ink->AddRectFilled(ImVec2(Origin.x - 14 * S, Origin.y - 14 * S), ImVec2(Origin.x - 11 * S, Origin.y - 2 * S), ImGui::GetColorU32(Spectrum(0xa79171)), 1.5f * S);
        Caption(ImVec2(Origin.x, Origin.y - 12 * S), "High source", 11 * S, 0xd0d0bb);
        char ObjectsLabel[32]; std::snprintf(ObjectsLabel, sizeof(ObjectsLabel), "%u objects", HighSummary.Objects);
        const auto HeadingWidth = ImGui::CalcTextSize("High source").x;
        Caption(ImVec2(Origin.x + HeadingWidth + 8 * S, Origin.y - 11 * S), ObjectsLabel, 8 * S, 0x7a7a69);
        ImGui::Dummy(ImVec2(1, 14 * S));
        char TriangleLabel[32]; std::snprintf(TriangleLabel, sizeof(TriangleLabel), "%llu", (unsigned long long)HighSummary.Triangles);
        Caption(ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y), TriangleLabel, 24 * S, 0xd3d6bf, LightTypeface ? LightTypeface : RegularTypeface);
        Caption(ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + 30 * S), "triangles · world transforms retained", 7 * S, 0x82846c);
        ImGui::Dummy(ImVec2(1, 48 * S));
        ImGui::BeginDisabled(BakeRunning.load());
        Controls.ControlHeight = 30;
        if (Controls.Button("Import high mesh", ImGui::GetContentRegionAvail().x, false, {}, false))
            Info = "Mesh import (OBJ/GLB/PLY/STL) is not implemented in this native build yet; use a built-in study mesh.";
        ImGui::EndDisabled();
        ImGui::Dummy(ImVec2(1, 8 * S));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextColored(Spectrum(0x868872), "OBJ · GLB / embedded glTF · PLY · high-only STL. No external asset requests.");
        ImGui::PopTextWrapPos();
    }
    Controls.EndCard();
    ImGui::EndGroup();

    Caption(ImVec2(ImGui::GetCursorScreenPos().x + 8 * S, ImGui::GetCursorScreenPos().y + 2 * S), "Built-in study", 9 * S, 0xa3a68c);
    ImGui::Dummy(ImVec2(1, 18 * S));
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x + 8 * S, ImGui::GetCursorScreenPos().y));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 16 * S);
    ImGui::BeginDisabled(BakeRunning.load());
    if (ImGui::BeginCombo("##Demo", DemoShape.c_str()))
    {
        for (const char *Shape : {"Teapot", "Cube", "Sphere"})
            if (ImGui::Selectable(Shape, DemoShape == Shape)) SelectDemo(Shape);
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(1, 18 * S));

    if (Mode == StudioMode::Mesh)
    {
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x + 8 * S, ImGui::GetCursorScreenPos().y));
        ConstructMeshMapGrid(ImGui::GetContentRegionAvail().x - 16 * S);
    }

    // .bk-pane-footer: a fixed 39px strip, status dot + "CPU BVH / WebGL preview" on the left, texture-set
    // count on the right - pinned to the window's own bottom edge, not inline with the scrolling content above.
    {
        const ImVec2 WindowPosition = ImGui::GetWindowPos(), WindowSize = ImGui::GetWindowSize();
        const float FooterHeight = 39 * S;
        const ImVec2 FooterMinimum(WindowPosition.x, WindowPosition.y + WindowSize.y - FooterHeight), FooterMaximum(WindowPosition.x + WindowSize.x, WindowPosition.y + WindowSize.y);
        auto *Ink = ImGui::GetWindowDrawList();
        Ink->AddRectFilled(FooterMinimum, FooterMaximum, ImGui::GetColorU32(Spectrum(0x151515)));
        Ink->AddLine(FooterMinimum, ImVec2(FooterMaximum.x, FooterMinimum.y), ImGui::GetColorU32(Spectrum(0xffffff, 0.02f)));
        Ink->AddCircleFilled(ImVec2(FooterMinimum.x + 16 * S, (FooterMinimum.y + FooterMaximum.y) * .5f), 2.5f * S, ImGui::GetColorU32(Spectrum(0x8aa27e)));
        Caption(ImVec2(FooterMinimum.x + 26 * S, (FooterMinimum.y + FooterMaximum.y) * .5f - 5 * S), "CPU BVH / WebGL preview", 8 * S, 0x777d66);
        char SetsLabel[32]; std::snprintf(SetsLabel, sizeof(SetsLabel), "%zu texture sets", Assets.Low.Parts.size());
        const auto Extent = ImGui::CalcTextSize(SetsLabel);
        Caption(ImVec2(FooterMaximum.x - 16 * S - Extent.x * 0.54f, (FooterMinimum.y + FooterMaximum.y) * .5f - 4 * S), SetsLabel, 7 * S, 0x686e59);
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     3D ORBIT VIEWPORT
//------------------------------------------------------------------------------------------------------------------------

void BakingWorkspacePanel::ConstructViewport3D(PlaneExtent Region)
{
    ImGui::InvisibleButton("##BakeViewport", ImVec2(Region.Width(), Region.Height()));
    const bool Hovered = ImGui::IsItemHovered();
    if (Hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        const ImVec2 Delta = ImGui::GetIO().MouseDelta;
        Camera.Yaw += Delta.x * 0.01f;
        Camera.Pitch = std::clamp(Camera.Pitch - Delta.y * 0.01f, -1.5f, 1.5f);
    }
    if (Hovered) Camera.Distance = std::clamp(Camera.Distance - ImGui::GetIO().MouseWheel * 0.3f, 1.2f, 12.f);

    const BakePart *Part = nullptr;
    for (const auto &Candidate : Assets.Low.Parts)
        if (Candidate.Name == (PreviewSetName.empty() ? SelectedTargets.empty() ? "" : SelectedTargets.front() : PreviewSetName)) { Part = &Candidate; break; }
    if (!Part && !Assets.Low.Parts.empty()) Part = &Assets.Low.Parts.front();

    auto *DrawList = ImGui::GetWindowDrawList();
    DrawList->PushClipRect(ImVec2(Region.MinimumX, Region.MinimumY), ImVec2(Region.MaximumX, Region.MaximumY), true);
    DrawList->AddRectFilled(ImVec2(Region.MinimumX, Region.MinimumY), ImVec2(Region.MaximumX, Region.MaximumY), ImGui::GetColorU32(Spectrum(0x111010)));
    if (Part && !Part->Indices.empty())
    {
        const Vec3 Eye{Camera.Distance * std::cos(Camera.Pitch) * std::sin(Camera.Yaw), Camera.Distance * std::sin(Camera.Pitch),
            Camera.Distance * std::cos(Camera.Pitch) * std::cos(Camera.Yaw)};
        const Vec3 Forward = NormalizeV(Sub(Vec3{}, Eye));
        const Vec3 Right = NormalizeV(CrossV(Forward, Vec3{0, 1, 0}));
        const Vec3 Up = CrossV(Right, Forward);
        const float CenterX = (Region.MinimumX + Region.MaximumX) * 0.5f, CenterY = (Region.MinimumY + Region.MaximumY) * 0.5f;
        const float FocalLength = std::min(Region.Width(), Region.Height()) * 0.8f;
        const Vec3 Light = NormalizeV(Vec3{0.4f, 0.8f, 0.5f});

        const auto Project = [&](const float P[3], ImVec2 &Screen, float &Depth) noexcept
        {
            const Vec3 World = Sub(Vec3{P[0], P[1], P[2]}, Eye);
            const float Cx = DotV(World, Right), Cy = DotV(World, Up), Cz = DotV(World, Forward);
            Depth = Cz;
            const float Z = std::max(Cz, 0.05f);
            Screen = ImVec2(CenterX + Cx * FocalLength / Z, CenterY - Cy * FocalLength / Z);
        };

        struct DrawTriangle { ImVec2 A, B, C; float Shade; float Depth; };
        std::vector<DrawTriangle> Triangles;
        Triangles.reserve(Part->Indices.size() / 3);
        for (size_t Index = 0; Index + 2 < Part->Indices.size(); Index += 3)
        {
            const auto &VA = Part->Vertices[Part->Indices[Index + 0]];
            const auto &VB = Part->Vertices[Part->Indices[Index + 1]];
            const auto &VC = Part->Vertices[Part->Indices[Index + 2]];
            ImVec2 Sa, Sb, Sc; float Da, Db, Dc;
            Project(VA.Position, Sa, Da);
            Project(VB.Position, Sb, Db);
            Project(VC.Position, Sc, Dc);
            if (Da <= 0.05f || Db <= 0.05f || Dc <= 0.05f) continue;
            const Vec3 FaceNormal = NormalizeV(CrossV(Sub(Vec3{VB.Position[0], VB.Position[1], VB.Position[2]}, Vec3{VA.Position[0], VA.Position[1], VA.Position[2]}),
                Sub(Vec3{VC.Position[0], VC.Position[1], VC.Position[2]}, Vec3{VA.Position[0], VA.Position[1], VA.Position[2]})));
            if (DotV(FaceNormal, Forward) > -0.02f) continue; // backface cull (camera looks along +Forward into the scene)
            const float Shade = std::clamp(0.25f + 0.75f * std::max(0.f, DotV(FaceNormal, Light)), 0.f, 1.f);
            Triangles.push_back({Sa, Sb, Sc, Shade, (Da + Db + Dc) / 3.f});
        }
        std::sort(Triangles.begin(), Triangles.end(), [](const DrawTriangle &A, const DrawTriangle &B) { return A.Depth > B.Depth; });
        const ImU32 BaseColour = ImGui::GetColorU32(Spectrum(0x8f8fe0));
        for (const auto &Triangle_ : Triangles)
        {
            const uint8_t Level = uint8_t(Triangle_.Shade * 255.f);
            const ImU32 Fill = IM_COL32(uint8_t(((BaseColour & 0xff) * Level) / 255), uint8_t((((BaseColour >> 8) & 0xff) * Level) / 255),
                uint8_t((((BaseColour >> 16) & 0xff) * Level) / 255), 255);
            DrawList->AddTriangleFilled(Triangle_.A, Triangle_.B, Triangle_.C, Fill);
            if (Wireframe) DrawList->AddTriangle(Triangle_.A, Triangle_.B, Triangle_.C, IM_COL32(20, 20, 20, 160), 1.f);
        }
    }
    else ImGui::GetWindowDrawList()->AddText(ImVec2(Region.MinimumX + 12, Region.MinimumY + 12), ImGui::GetColorU32(Spectrum(0x716f62)), "No mesh selected");
    DrawList->PopClipRect();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     2D MAP PREVIEW (BLOCK RASTER)
//------------------------------------------------------------------------------------------------------------------------

// 📝 Draws the selected bake result directly from its real RGBA8 pixels via a coarse block grid (ImGui's draw
// list, same technique as the 3D viewport) — a genuine representation of the baked data without requiring a
// new GPU-texture upload path. ExportResults() below writes the full-resolution PNGs to disk for pixel-exact use.
// 📝 1:1 with .bk-map-image's `repeating-conic-gradient(#23251f 0% 25%, #191c16 0% 50%) 50% / 16px 16px`
// transparency checkerboard plus its 1px #ffffff0d / 5px-radius border - the reference never shows a baked
// map floating on a flat panel background, so neither does this. Returns false (and draws nothing) when
// PreviewSetName/PreviewMapKey resolve to no map yet; the caller substitutes the full .bk-empty screen instead.
bool BakingWorkspacePanel::ConstructMapPreview(PlaneExtent Region)
{
    auto *DrawList = ImGui::GetWindowDrawList();
    const BakeMapImage *Image = nullptr;
    uint32_t Resolution = 0;
    for (const auto &Result : Results)
        if (Result.Name == PreviewSetName)
        {
            Resolution = Result.Resolution;
            for (const auto &Map : Result.Maps)
                if (Map.Key == PreviewMapKey) Image = &Map;
        }
    if (!Image) return false;
    const float CheckerCell = 16 * InterfaceScale;
    DrawList->PushClipRect(ImVec2(Region.MinimumX, Region.MinimumY), ImVec2(Region.MaximumX, Region.MaximumY), true);
    for (float Cy = Region.MinimumY; Cy < Region.MaximumY; Cy += CheckerCell)
        for (float Cx = Region.MinimumX; Cx < Region.MaximumX; Cx += CheckerCell)
        {
            const int Parity = (int(std::round((Cx - Region.MinimumX) / CheckerCell)) + int(std::round((Cy - Region.MinimumY) / CheckerCell))) & 1;
            DrawList->AddRectFilled(ImVec2(Cx, Cy), ImVec2(Cx + CheckerCell, Cy + CheckerCell), ImGui::GetColorU32(Spectrum(Parity ? 0x191c16 : 0x23251f)));
        }
    constexpr uint32_t Blocks = 96; // coarse in-app preview grid; ExportResults() keeps the real resolution
    const float CellWidth = Region.Width() / float(Blocks), CellHeight = Region.Height() / float(Blocks);
    for (uint32_t By = 0; By < Blocks; ++By)
        for (uint32_t Bx = 0; Bx < Blocks; ++Bx)
        {
            const uint32_t SampleX = std::min(Resolution - 1, uint32_t((float(Bx) + 0.5f) / float(Blocks) * float(Resolution)));
            const uint32_t SampleY = std::min(Resolution - 1, uint32_t((float(By) + 0.5f) / float(Blocks) * float(Resolution)));
            const size_t Offset = (size_t(SampleY) * Resolution + SampleX) * 4;
            const ImU32 Colour = IM_COL32(Image->Rgba[Offset + 0], Image->Rgba[Offset + 1], Image->Rgba[Offset + 2], 255);
            const float X0 = Region.MinimumX + Bx * CellWidth, Y0 = Region.MinimumY + By * CellHeight;
            DrawList->AddRectFilled(ImVec2(X0, Y0), ImVec2(X0 + CellWidth + 0.5f, Y0 + CellHeight + 0.5f), Colour);
        }
    DrawList->PopClipRect();
    DrawList->AddRect(ImVec2(Region.MinimumX, Region.MinimumY), ImVec2(Region.MaximumX, Region.MaximumY), ImGui::GetColorU32(Spectrum(0xffffff, 0.05f)), 5 * InterfaceScale);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PREVIEW DOCK (CENTER)
//------------------------------------------------------------------------------------------------------------------------

// 🧩 1:1 with BakingStudio.jsx's <section className="bk-dock bk-preview"> + .bk-preview-toolbar/.bk-view-tabs/
// .bk-mesh-toolbar chrome: a 73px title strip (16px/weight-300 part name + muted subtitle, pill 3D/2D tabs on
// the right) and, in 3D mode, a second Frame/overlay/wireframe toolbar row - not inline SmallButtons.
void BakingWorkspacePanel::ConstructPreviewDock(PlaneExtent Region)
{
    const float S = InterfaceScale;
    ImGui::SetNextWindowPos(ImVec2(Region.MinimumX, Region.MinimumY));
    ImGui::SetNextWindowSize(ImVec2(Region.Width(), Region.Height()));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Spectrum(0x141414));
    ImGui::Begin("Baking viewport", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    Controls.Scale = S;

    // .bk-preview-toolbar
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        const float ToolbarHeight = 73 * S, ToolbarWidth = ImGui::GetContentRegionAvail().x;
        auto *Ink = ImGui::GetWindowDrawList();
        Ink->AddRectFilled(Origin, ImVec2(Origin.x + ToolbarWidth, Origin.y + ToolbarHeight), ImGui::GetColorU32(Spectrum(0x151515)));
        Ink->AddLine(ImVec2(Origin.x, Origin.y + ToolbarHeight), ImVec2(Origin.x + ToolbarWidth, Origin.y + ToolbarHeight), ImGui::GetColorU32(Spectrum(0xffffff, 0.02f)));
        const std::string PartLabel = PreviewSetName.empty() ? Assets.Low.Label : PreviewSetName;
        Caption(ImVec2(Origin.x + 19 * S, Origin.y + 20 * S), PartLabel.c_str(), 16 * S, 0xd0d4be, LightTypeface ? LightTypeface : RegularTypeface);
        const char *Subtitle = Mode == StudioMode::Volume ? "High mesh · 3D distance volume" : Mode == StudioMode::Material ? "Procedural flat-patch export" :
            SettingsStale() ? "Previous bake · settings changed" : "Mesh-map authoring";
        Caption(ImVec2(Origin.x + 19 * S, Origin.y + 45 * S), Subtitle, 8 * S, 0x7d856b);

        const float TabsWidth = 160 * S, TabsHeight = 34 * S;
        const ImVec2 TabsMinimum(Origin.x + ToolbarWidth - 19 * S - TabsWidth, Origin.y + (ToolbarHeight - TabsHeight) * .5f);
        Ink->AddRectFilled(TabsMinimum, ImVec2(TabsMinimum.x + TabsWidth, TabsMinimum.y + TabsHeight), ImGui::GetColorU32(Spectrum(0x0f100e)), TabsHeight * .5f);
        Ink->AddRect(TabsMinimum, ImVec2(TabsMinimum.x + TabsWidth, TabsMinimum.y + TabsHeight), ImGui::GetColorU32(Spectrum(0xffffff, 0.03f)), TabsHeight * .5f);
        const char *Tabs[]{"3D preview", "2D maps"};
        const float TabWidth = (TabsWidth - 8 * S) / 2.f;
        for (int Index = 0; Index < 2; ++Index)
        {
            const ImVec2 TabMinimum(TabsMinimum.x + 4 * S + Index * TabWidth, TabsMinimum.y + 4 * S);
            const ImVec2 TabMaximum(TabMinimum.x + TabWidth - 2 * S, TabsMinimum.y + TabsHeight - 4 * S);
            ImGui::SetCursorScreenPos(TabMinimum);
            ImGui::PushID(100 + Index);
            ImGui::InvisibleButton("Tab", ImVec2(TabMaximum.x - TabMinimum.x, TabMaximum.y - TabMinimum.y));
            const bool Selected = (Index == 0) == (View == ViewMode::Preview3D);
            if (ImGui::IsItemClicked()) View = Index == 0 ? ViewMode::Preview3D : ViewMode::Maps2D;
            Track(Index == 0 ? "View3D" : "View2D");
            if (Selected) Ink->AddRectFilled(TabMinimum, TabMaximum, ImGui::GetColorU32(Spectrum(0x2c3025)), (TabMaximum.y - TabMinimum.y) * .5f);
            const auto Extent = ImGui::CalcTextSize(Tabs[Index]);
            Caption(ImVec2(TabMinimum.x + ((TabMaximum.x - TabMinimum.x) - Extent.x * 0.69f) * .5f, TabMinimum.y + ((TabMaximum.y - TabMinimum.y) - Extent.y * 0.69f) * .5f), Tabs[Index],
                9 * S, Selected ? 0xdce4c9 : 0x8c907b);
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + ToolbarHeight));
    }

    if (View == ViewMode::Preview3D)
    {
        // .bk-mesh-toolbar
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        const float RowHeight = 38 * S, RowWidth = ImGui::GetContentRegionAvail().x;
        auto *Ink = ImGui::GetWindowDrawList();
        Ink->AddRectFilled(Origin, ImVec2(Origin.x + RowWidth, Origin.y + RowHeight), ImGui::GetColorU32(Spectrum(0x151515)));
        float CursorX = Origin.x + 18 * S;
        const auto ToolbarButton = [&](const char *Label, bool Active, bool Toggle, const char *IconPath = nullptr) -> bool
        {
            const auto Extent = ImGui::CalcTextSize(Label);
            const float IconGutter = IconPath ? 15 * S : 0.f;
            const float ButtonWidth = Extent.x * 0.72f + 18 * S + IconGutter, ButtonHeight = 24 * S;
            const ImVec2 Minimum(CursorX, Origin.y + (RowHeight - ButtonHeight) * .5f), Maximum(Minimum.x + ButtonWidth, Minimum.y + ButtonHeight);
            ImGui::SetCursorScreenPos(Minimum);
            ImGui::PushID(Label);
            ImGui::InvisibleButton("Mesh", ImVec2(ButtonWidth, ButtonHeight));
            const bool Clicked = ImGui::IsItemClicked();
            if (Toggle && Active) Ink->AddRectFilled(Minimum, Maximum, ImGui::GetColorU32(Spectrum(0x33392b)), 6 * S);
            const uint32_t LabelColour = Toggle && Active ? 0xd0dfb8 : 0xa8ab98;
            if (IconPath)
            {
                PixelSpace IconSurface;
                (void)IconSurface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 1);
                GlyphSpace::Stroke(IconSurface, IconPath, {Minimum.x + 9 * S, Minimum.y + (ButtonHeight - 11 * S) * .5f, 11 * S, 1.7f, SpectrumQuad(LabelColour)});
            }
            Caption(ImVec2(Minimum.x + 9 * S + IconGutter, Minimum.y + (ButtonHeight - 8 * S) * .4f), Label, 8 * S, LabelColour);
            ImGui::PopID();
            CursorX += ButtonWidth + 6 * S;
            return Clicked;
        };
        if (ToolbarButton("Frame", false, false, "M15 3h6v6M9 21H3v-6M21 3l-7 7M3 21l7-7")) ResetCamera();
        if (ToolbarButton("High overlay", Overlay, true)) Overlay = !Overlay;
        if (ToolbarButton("Wireframe", Wireframe, true)) Wireframe = !Wireframe;
        const char *Hint = "Orbit · scroll to zoom";
        const auto HintExtent = ImGui::CalcTextSize(Hint);
        Caption(ImVec2(Origin.x + RowWidth - 18 * S - HintExtent.x * 0.54f, Origin.y + (RowHeight - 7 * S) * .5f), Hint, 7 * S, 0x697459);
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + RowHeight));

        const ImVec2 CursorScreen = ImGui::GetCursorScreenPos();
        const ImVec2 Available = ImGui::GetContentRegionAvail();
        const float ViewportHeight = std::max(80.f, Available.y - 92.f * InterfaceScale);
        // 📝 ConstructViewport3D's own ImGui::InvisibleButton reserves exactly (Available.x, ViewportHeight) of
        // layout space starting at the CURRENT cursor position - the Region passed in must start there too
        // (no manual offset) and no extra Dummy() may follow, or the reservation doubles and shoves the Bake
        // button/results/status strip below the window.
        ConstructViewport3D(Spanning(CursorScreen.x, CursorScreen.y, Available.x, ViewportHeight));
    }
    else
    {
        // .bk-map-toolbar: 11px/18px padded row - the set/map pickers on the left, a right-aligned icon-only
        // PNG download button for whichever single map is on screen (studioTheme.css's `margin-left: auto`
        // button), only present once a map actually exists to download - not a bare two-combo row.
        const ImVec2 ToolbarOrigin = ImGui::GetCursorScreenPos();
        const float ToolbarRowHeight = 11 * S + 30 * S + 11 * S;
        ImGui::SetCursorScreenPos(ImVec2(ToolbarOrigin.x + 18 * S, ToolbarOrigin.y + 11 * S));
        ImGui::SetNextItemWidth(170 * S);
        if (ImGui::BeginCombo("##Set", PreviewSetName.c_str()))
        {
            for (const auto &Result : Results)
                if (ImGui::Selectable(Result.Name.c_str(), Result.Name == PreviewSetName)) PreviewSetName = Result.Name;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(170 * S);
        if (ImGui::BeginCombo("##Map", FindMeshMap(PreviewMapKey) ? FindMeshMap(PreviewMapKey)->Label : PreviewMapKey.c_str()))
        {
            for (const auto &Key : Settings.Channels)
            {
                const auto *Meta = FindMeshMap(Key);
                if (ImGui::Selectable(Meta ? Meta->Label : Key.c_str(), Key == PreviewMapKey)) PreviewMapKey = Key;
            }
            ImGui::EndCombo();
        }

        const BakeMapImage *CurrentImage = nullptr;
        uint32_t CurrentResolution = 0;
        for (const auto &Result : Results)
            if (Result.Name == PreviewSetName)
            {
                CurrentResolution = Result.Resolution;
                for (const auto &Map : Result.Maps)
                    if (Map.Key == PreviewMapKey) CurrentImage = &Map;
            }
        if (CurrentImage)
        {
            const float ButtonSize = 26 * S;
            ImGui::SetCursorScreenPos(ImVec2(Region.MaximumX - 18 * S - ButtonSize, ToolbarOrigin.y + 11 * S + 2 * S));
            Controls.ControlHeight = 26;
            if (Controls.Button("Download this map as PNG", ButtonSize, false,
                "M12 15V3M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4M7 10l5 5 5-5", true))
            {
                try
                {
                    std::filesystem::path Directory = ExportDirectory.empty() ? std::filesystem::temp_directory_path() / "FrontierBakeExport" : ExportDirectory;
                    std::filesystem::create_directories(Directory);
                    const auto Path = Directory / (PreviewSetName + "-" + PreviewMapKey + ".png");
                    if (stbi_write_png(Path.string().c_str(), int(CurrentResolution), int(CurrentResolution), 4, CurrentImage->Rgba.data(), int(CurrentResolution) * 4))
                    {
                        LastExportDirectory = Directory.string();
                        Info = "Map PNG written to " + Path.string();
                    }
                }
                catch (const std::exception &Failure) { Error = std::string("PNG export failed: ") + Failure.what(); }
            }
            Controls.Tooltip("Download this map as PNG", "Writes just the map on screen, not the whole bake's set.");
            Track("DownloadMapPNG");
        }
        ImGui::SetCursorScreenPos(ImVec2(ToolbarOrigin.x, ToolbarOrigin.y + ToolbarRowHeight));

        const ImVec2 CursorScreen = ImGui::GetCursorScreenPos();
        const ImVec2 Available = ImGui::GetContentRegionAvail();
        // 📝 Leave the same ~92*S margin below the image as the 3D branch leaves below its viewport, so the
        // Bake/Export row and result captions that follow (outside this if/else) always have room instead of
        // being clipped against the window's bottom edge.
        if (CurrentImage)
        {
            // .bk-map-image: 17px padding around a checkerboard-backed, bordered square, with a caption line
            // below it ("{res} × {res} · {colorSpace} · current/previous bake") - not a bare edge-to-edge raster.
            const float ImageSize = std::max(64.f, std::min(Available.x - 34.f * S, Available.y - 92.f * S - 26.f * S) - 8.f);
            const ImVec2 ImageOrigin(CursorScreen.x + (Available.x - ImageSize) * .5f, CursorScreen.y + 17 * S);
            ConstructMapPreview(Spanning(ImageOrigin.x, ImageOrigin.y, ImageSize, ImageSize));
            const bool Stale = SettingsStale();
            const char *ColourSpace = PreviewMapKey == "base-color" ? "sRGB" : "linear";
            char CaptionLine[160];
            std::snprintf(CaptionLine, sizeof(CaptionLine), "%u × %u · %s · %s", CurrentResolution, CurrentResolution, ColourSpace, Stale ? "previous recipe" : "current bake");
            const auto LineExtent = ImGui::CalcTextSize(CaptionLine);
            Caption(ImVec2(CursorScreen.x + (Available.x - LineExtent.x * 0.69f) * .5f, ImageOrigin.y + ImageSize + 12 * S), CaptionLine, 9 * S, 0x7c8a6a);
            ImGui::Dummy(ImVec2(Available.x, ImageSize + 17 * S + 26 * S));
        }
        else
        {
            // .bk-empty: centred stack icon + "No baked maps yet" + the same explainer paragraph as the web
            // reference, filling the whole stage instead of a one-line draw-list caption in the corner.
            const float StageHeight = std::max(120.f, Available.y - 92.f * S);
            const ImVec2 StageCentre(CursorScreen.x + Available.x * .5f, CursorScreen.y + StageHeight * .5f);
            auto *Ink = ImGui::GetWindowDrawList();
            PixelSpace IconSurface;
            (void)IconSurface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 1);
            GlyphSpace::Stroke(IconSurface, "M12 2L2 7L12 12L22 7ZM2 17L12 22L22 17M2 12L12 17L22 12",
                {StageCentre.x - 15 * S, StageCentre.y - 58 * S, 30 * S, 1.6f, SpectrumQuad(0x748463)});
            const char *Heading = "No baked maps yet";
            const auto HeadingExtent = ImGui::CalcTextSize(Heading);
            Caption(ImVec2(StageCentre.x - HeadingExtent.x * 0.64f, StageCentre.y - 16 * S), Heading, 16 * S, 0xbdcba9, LightTypeface ? LightTypeface : RegularTypeface);
            ImGui::SetCursorScreenPos(ImVec2(StageCentre.x - 190 * S, StageCentre.y + 10 * S));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 380 * S);
            ImGui::PushStyleColor(ImGuiCol_Text, Spectrum(0x748463));
            ImGui::TextWrapped("Choose your low UV targets and high source, then bake the selected maps. "
                "Material-patch mode downloads its own six-map ZIP.");
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
            (void)Ink;
            ImGui::SetCursorScreenPos(CursorScreen);
            ImGui::Dummy(ImVec2(Available.x, StageHeight));
        }
    }

    ImGui::Dummy(ImVec2(1, 10 * S));
    if (BakeRunning.load())
    {
        const float Fraction = BakeTotal.load() ? float(double(BakeDone.load()) / double(BakeTotal.load())) : 0.f;
        ImGui::ProgressBar(Fraction, ImVec2(-1, 0), BakeProgressLabel.c_str());
    }
    // .bk-run-actions: Bake (bk-primary) + Export on the left, .bk-settings-history's Undo/Redo pair pinned to
    // the row's far right via BakingStudio.jsx's travel("undo"/"redo") - backed by the real coalesced settings
    // history CoalesceSettingsUndo() builds in ConstructInspector, not decorative disabled buttons.
    ImGui::BeginDisabled(BakeRunning.load() || SelectedTargets.empty() || Settings.Channels.empty());
    Controls.ControlHeight = 32;
    if (Controls.Button(SelectedTargets.size() > 1 ? "Bake all selected meshes" : "Bake mesh maps", 220 * S)) StartBake();
    Track("Bake");
    ImGui::EndDisabled();
    if (!Results.empty())
    {
        ImGui::SameLine();
        Controls.ControlHeight = 32;
        if (Controls.Button("Export maps (PNG)", 0)) ExportResults();
        Track("Export");
    }
    {
        const float HistoryWidth = 25 * S * 2 + 4 * S;
        ImGui::SameLine(0, 0);
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + 8 * S, ImGui::GetWindowContentRegionMax().x - HistoryWidth));
        Controls.ControlHeight = 25;
        ImGui::BeginDisabled(SettingsUndoStack.empty());
        if (Controls.Button("Undo bake settings", 25 * S, false,
            "M9 14L4 9l5-5M4 9h10.5a5.5 5.5 0 0 1 5.5 5.5a5.5 5.5 0 0 1-5.5 5.5H11", true)) UndoSettings();
        ImGui::EndDisabled();
        Controls.Tooltip("Undo bake settings", "Steps back through the inspector's resolution/projection history.");
        Track("UndoSettings");
        ImGui::SameLine(0, 4 * S);
        ImGui::BeginDisabled(SettingsRedoStack.empty());
        if (Controls.Button("Redo bake settings", 25 * S, false,
            "M15 14l5-5-5-5M20 9H9.5A5.5 5.5 0 0 0 4 14.5A5.5 5.5 0 0 0 9.5 20H13", true)) RedoSettings();
        ImGui::EndDisabled();
        Controls.Tooltip("Redo bake settings", "Replays a step the undo button just stepped back from.");
        Track("RedoSettings");
    }
    // .bk-result-summary: one Check-prefixed line per bake result, below the actions row - "N results ready"
    // is not in the web reference, so this native build no longer invents that heading either.
    if (!Results.empty())
    {
        ImGui::Dummy(ImVec2(1, 10 * S));
        for (const auto &Result : Results)
        {
            const ImVec2 LineOrigin = ImGui::GetCursorScreenPos();
            PixelSpace IconSurface;
            (void)IconSurface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 1);
            GlyphSpace::Stroke(IconSurface, "M20 6L9 17l-5-5", {LineOrigin.x, LineOrigin.y + 1 * S, 9 * S, 1.8f, SpectrumQuad(0x8b9d76)});
            char Line[200];
            std::snprintf(Line, sizeof(Line), "%s · %u charts · %llu hits · %llu misses", Result.Name.c_str(), Result.Stats.Charts,
                (unsigned long long)Result.Stats.Hits, (unsigned long long)Result.Stats.Misses);
            Caption(ImVec2(LineOrigin.x + 15 * S, LineOrigin.y), Line, 8.5f * S, 0x8b9d76);
            ImGui::Dummy(ImVec2(1, 14 * S));
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     INSPECTOR (RIGHT)
//------------------------------------------------------------------------------------------------------------------------

// 🧩 1:1 with BakingStudio.jsx's <aside className="bk-dock bk-inspector"> + studioTheme.css's
// .bk-inspector-identity/.bk-section-caption/.bk-card/.bk-field chrome: an icon-chip identity block, an
// "OUTPUT" section caption, then three accent-barred cards (Texture settings / Map inspector / High-to-low
// projection) built from Controls.Dropdown/Number/Range/Toggle field rows, a Bake-contract notes card, and a
// pinned .bk-pane-footer - not raw sequential ImGui combos/sliders/checkboxes.
void BakingWorkspacePanel::ConstructInspector(PlaneExtent Region)
{
    const float S = InterfaceScale;
    ImGui::SetNextWindowPos(ImVec2(Region.MinimumX, Region.MinimumY));
    ImGui::SetNextWindowSize(ImVec2(Region.Width(), Region.Height()));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Spectrum(0x121212));
    // 📝 Unlike the Assets/Preview docks, this panel's card stack can comfortably exceed the inspector's
    // height (especially with "Map inspector" populated) - NoDecoration also implies NoScrollbar, so build
    // the flag set without it here and let ImGui auto-scroll; the .bk-pane-footer strip below is still drawn
    // last against the window's fixed bottom edge, so it stays pinned on top regardless of scroll offset.
    ImGui::Begin("Baking inspector", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    Controls.Scale = S;
    SettingsAtFrameStart = Settings;
    const float FooterHeight = 39 * S;
    // 📝 The card stack scrolls inside its own child (height = window height minus the footer strip), clipped
    // to that child's rect - so it can never paint over the pinned .bk-pane-footer below it the way unclipped
    // auto-flow content drawn straight into the outer window could.
    ImGui::BeginChild("InspectorScroll", ImVec2(0, ImGui::GetContentRegionAvail().y - FooterHeight), false);
    ImGui::Indent(19 * S);
    ImGui::Dummy(ImVec2(1, 5 * S));

    if (Mode == StudioMode::Material || Mode == StudioMode::Volume)
    {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 19 * S);
        ImGui::TextWrapped("%s", Mode == StudioMode::Material ? "Material patch mode is not yet native - ported in a later pass."
                                                                : "Volume SDF mode is not yet native - ported in a later pass.");
        ImGui::PopTextWrapPos();
        ImGui::Unindent(19 * S);
        ImGui::EndChild();
        ImGui::End();
        ImGui::PopStyleColor();
        return;
    }

    // .bk-inspector-identity: a rounded icon chip + "STATIC HIGH -> LOW" eyebrow + big "Mesh map bake" title.
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        auto *Ink = ImGui::GetWindowDrawList();
        const float ChipSize = 51 * S;
        Ink->AddRectFilled(Origin, ImVec2(Origin.x + ChipSize, Origin.y + ChipSize), ImGui::GetColorU32(Spectrum(0x21231d)), 16 * S);
        Ink->AddRect(Origin, ImVec2(Origin.x + ChipSize, Origin.y + ChipSize), ImGui::GetColorU32(Spectrum(0xffffff, 0.03f)), 16 * S);
        const float Cx = Origin.x + ChipSize * .5f, Cy = Origin.y + ChipSize * .5f;
        Ink->AddTriangleFilled(ImVec2(Cx - 9 * S, Cy - 7 * S), ImVec2(Cx - 9 * S, Cy + 7 * S), ImVec2(Cx + 3 * S, Cy), ImGui::GetColorU32(Spectrum(0xa5b38a)));
        Ink->AddLine(ImVec2(Cx - 2 * S, Cy), ImVec2(Cx + 10 * S, Cy), ImGui::GetColorU32(Spectrum(0xa5b38a)), 2 * S);
        const float TextX = Origin.x + ChipSize + 12 * S;
        Caption(ImVec2(TextX, Origin.y + 1 * S), "STATIC HIGH -> LOW", 7 * S, 0x859271);
        Caption(ImVec2(TextX, Origin.y + 13 * S), "Mesh map bake", 22 * S, 0xd8dfc7, LightTypeface ? LightTypeface : RegularTypeface);
        char SetsLabel[64];
        std::snprintf(SetsLabel, sizeof(SetsLabel), "%zu selected texture set%s", SelectedTargets.size(), SelectedTargets.size() == 1 ? "" : "s");
        Caption(ImVec2(TextX, Origin.y + 40 * S), SetsLabel, 8 * S, 0x728060);
        ImGui::Dummy(ImVec2(1, ChipSize + 17 * S));
    }

    // .bk-section-caption "OUTPUT" + muted format subcaption, right-aligned.
    {
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        Caption(Origin, "OUTPUT", 8 * S, 0x8e8e7b);
        const char *Sub = "8-bit PNG + manifest";
        const auto Extent = ImGui::CalcTextSize(Sub);
        Caption(ImVec2(Origin.x + ImGui::GetContentRegionAvail().x - Extent.x * 0.56f, Origin.y + 0.5f * S), Sub, 7 * S, 0x666657);
        ImGui::Dummy(ImVec2(1, 19 * S));
    }

    const auto AccentBar = [&](uint32_t Colour, const char *Label)
    {
        auto *Ink = ImGui::GetWindowDrawList();
        const ImVec2 Origin = ImGui::GetCursorScreenPos();
        Ink->AddRectFilled(ImVec2(Origin.x - 14 * S, Origin.y - 2 * S), ImVec2(Origin.x - 11 * S, Origin.y + 10 * S), ImGui::GetColorU32(Spectrum(Colour)), 1.5f * S);
        Caption(ImVec2(Origin.x, Origin.y), Label, 11 * S, 0xd0d0bb);
        ImGui::Dummy(ImVec2(1, 26 * S));
    };

    Controls.ControlHeight = 29;

    // "Texture settings" card.
    Controls.BeginCard("", true);
    {
        AccentBar(0x8b9b82, "Texture settings");
        const char *ResolutionLabels[]{"64 x 64", "128 x 128", "256 x 256", "512 x 512", "1024 x 1024"};
        const int Resolutions[]{64, 128, 256, 512, 1024};
        int ResolutionIndex = 2;
        for (int Index = 0; Index < 5; ++Index) if (Resolutions[Index] == int(Settings.Resolution)) ResolutionIndex = Index;
        if (Controls.Dropdown("Resolution", ResolutionIndex, ResolutionLabels)) Settings.Resolution = uint32_t(Resolutions[ResolutionIndex]);
        float Padding = float(Settings.Padding);
        if (Controls.Number("Padding", Padding, 0, 32)) Settings.Padding = uint32_t(std::clamp(Padding, 0.f, 32.f));
        const char *NormalFormats[]{"OpenGL +Y", "DirectX -Y"};
        int NormalIndex = Settings.NormalPlusY ? 0 : 1;
        if (Controls.Dropdown("Normal format", NormalIndex, NormalFormats)) Settings.NormalPlusY = NormalIndex == 0;
    }
    Controls.EndCard();

    // "Map inspector" card.
    Controls.BeginCard("", true);
    {
        AccentBar(0x82939e, "Map inspector");
        const auto *Meta = FindMeshMap(InspectMapKey);
        if (Meta)
        {
            Caption(ImGui::GetCursorScreenPos(), Meta->Label, 10 * S, 0xd9d4c4);
            ImGui::Dummy(ImVec2(1, 16 * S));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
            ImGui::TextColored(Spectrum(0xa3a68c), "%s", Meta->Description);
            ImGui::PopTextWrapPos();
            const bool Enabled = std::find(Settings.Channels.begin(), Settings.Channels.end(), InspectMapKey) != Settings.Channels.end();
            ImGui::TextColored(Spectrum(0x82846c), "%s", IsMeshMapImplemented(InspectMapKey) ? (Enabled ? "Enabled - will be baked" : "Disabled - click its tile to enable")
                                                                                              : "Not yet implemented in this native build.");
            ImGui::Dummy(ImVec2(1, 6 * S));
        }
    }
    Controls.EndCard();

    // "High-to-low projection" card.
    Controls.BeginCard("", true);
    {
        AccentBar(0xa79171, "High-to-low projection");
        const char *Methods[]{"Normal ray", "Nearest surface"};
        int MethodIndex = Settings.ProjectionRay ? 0 : 1;
        if (Controls.Dropdown("Method", MethodIndex, Methods)) Settings.ProjectionRay = MethodIndex == 0;
        Controls.Number("Front distance", Settings.Front, 0.0001f, 10.f);
        Controls.Number("Back distance", Settings.Back, 0.0001f, 10.f);
        Controls.Number("AO distance", Settings.AoDistance, 0.0001f, 10.f);
        Controls.Number("Secondary AO distance", Settings.SecondaryAoDistance, 0.0001f, 10.f);
        Controls.Number("Thickness range", Settings.ThicknessRange, 0.0001f, 10.f);
        Controls.Number("Curvature radius", Settings.CurvatureRadius, 0.0001f, 10.f);
        const char *SampleLabels[]{"4", "8", "16", "32", "64"};
        const int SampleOptions[]{4, 8, 16, 32, 64};
        int SampleIndex = 2;
        for (int Index = 0; Index < 5; ++Index) if (SampleOptions[Index] == int(Settings.Samples)) SampleIndex = Index;
        if (Controls.Dropdown("AO samples", SampleIndex, SampleLabels)) Settings.Samples = uint32_t(SampleOptions[SampleIndex]);
        Controls.Toggle("Match objects by name", Settings.MatchByName);
        ImGui::Dummy(ImVec2(1, 2 * S));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextColored(Spectrum(0x868872), "Distances use source scene units. Ray front/back limits act as a uniform projection envelope, not a custom cage mesh.");
        ImGui::PopTextWrapPos();
    }
    Controls.EndCard();

    // "Bake contract" notes card.
    Controls.BeginCard("", true);
    {
        AccentBar(0x8b9b82, "Bake contract");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextColored(Spectrum(0x8e9c7d), "Each low object is its own UV texture set. Misses, chart count and coverage are reported rather than hidden.");
        ImGui::Dummy(ImVec2(1, 8 * S));
        ImGui::TextColored(Spectrum(0x8e9c7d), "Tangent normal, world normal, UV coverage, the alpha/hit mask, ambient occlusion and curvature are evaluated "
                            "by a real CPU BVH projection in this native build. The remaining mesh maps in the 27-map contract are "
                            "disabled tiles, not fabricated pixels.");
        ImGui::PopTextWrapPos();
    }
    Controls.EndCard();
    ImGui::Unindent(19 * S);
    CoalesceSettingsUndo();
    ImGui::EndChild();

    // .bk-pane-footer: pinned 39px strip - "Settings + selected maps" / "Source meshes: session only" - drawn
    // OUTSIDE the scrolling child above, against the outer window's own rect, so it can never be painted over
    // by (or clipped awkwardly against) the card stack's scroll content.
    {
        const ImVec2 WindowPosition = ImGui::GetWindowPos(), WindowSize = ImGui::GetWindowSize();
        const ImVec2 FooterMinimum(WindowPosition.x, WindowPosition.y + WindowSize.y - FooterHeight), FooterMaximum(WindowPosition.x + WindowSize.x, WindowPosition.y + WindowSize.y);
        auto *Ink = ImGui::GetWindowDrawList();
        Ink->AddRectFilled(FooterMinimum, FooterMaximum, ImGui::GetColorU32(Spectrum(0x151515)));
        Ink->AddLine(FooterMinimum, ImVec2(FooterMaximum.x, FooterMinimum.y), ImGui::GetColorU32(Spectrum(0xffffff, 0.02f)));
        Caption(ImVec2(FooterMinimum.x + 19 * S, (FooterMinimum.y + FooterMaximum.y) * .5f - 5 * S), "Settings + selected maps", 8 * S, 0x777d66);
        const char *Sub = "Source meshes: session only";
        const auto Extent = ImGui::CalcTextSize(Sub);
        Caption(ImVec2(FooterMaximum.x - 19 * S - Extent.x * 0.54f, (FooterMinimum.y + FooterMaximum.y) * .5f - 4 * S), Sub, 7 * S, 0x686e59);
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     TOP-LEVEL LAYOUT
//------------------------------------------------------------------------------------------------------------------------

void BakingWorkspacePanel::ConstructInterface()
{
    PollBake();
    ConstructHeader();
    const auto *Viewport = ImGui::GetMainViewport();
    const float HeaderHeight = 43 * InterfaceScale, StatusHeight = 24 * InterfaceScale;
    const float Top = Viewport->WorkPos.y + HeaderHeight;
    const float Bottom = Viewport->WorkPos.y + Viewport->WorkSize.y - StatusHeight;
    const float Left = Viewport->WorkPos.x, Right = Viewport->WorkPos.x + Viewport->WorkSize.x;
    const float AssetsWidth = std::clamp(Viewport->WorkSize.x * 0.22f, 260.f, 420.f) * InterfaceScale;
    const float InspectorWidth = std::clamp(Viewport->WorkSize.x * 0.22f, 260.f, 420.f) * InterfaceScale;

    ConstructAssetsDock(Spanning(Left, Top, AssetsWidth, Bottom - Top));
    ConstructPreviewDock(Spanning(Left + AssetsWidth, Top, Right - Left - AssetsWidth - InspectorWidth, Bottom - Top));
    ConstructInspector(Spanning(Right - InspectorWidth, Top, InspectorWidth, Bottom - Top));

    ImGui::SetNextWindowPos(ImVec2(Left, Bottom));
    ImGui::SetNextWindowSize(ImVec2(Right - Left, StatusHeight));
    ImGui::Begin("Baking status", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    ImGui::TextColored(Spectrum(0x9a9a8f), "%s", Info.c_str());
    ImGui::SameLine(ImGui::GetWindowWidth() - 220 * InterfaceScale);
    ImGui::TextColored(Spectrum(0x716f62), "BOUNDED STATIC MESH BAKER");
    ImGui::End();

    if (!Error.empty())
    {
        ImGui::SetNextWindowPos(ImVec2(Left + 24, Bottom - 64), ImGuiCond_Always);
        ImGui::Begin("Baking error", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextColored(Spectrum(0xe08080), "%s", Error.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Dismiss")) Error.clear();
        ImGui::End();
    }
}
} // namespace Frontier
