/*============================================================================================================================================
                                                          BAKINGWORKSPACEPANEL.CPP
============================================================================================================================================*/
#include "UI/TextureStudio/BakingWorkspacePanel.h"
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

//------------------------------------------------------------------------------------------------------------------------
//                                                     MESH MAP METADATA (1:1 WITH meshMaps.js)
//------------------------------------------------------------------------------------------------------------------------

struct MeshMapMeta
{
    const char *Key;
    const char *Label;
    const char *Family;
    const char *Description;
};
// 📝 Order and grouping mirror Experimental/TextureStudio/src/baking/meshMaps.js exactly; families render as
// section headers in the tile grid in this same order (Normals, Surface, Coordinates & IDs, Masks, Color).
const std::vector<MeshMapMeta> &MeshMapTable()
{
    static const std::vector<MeshMapMeta> Table{
        {"normal", "Tangent normal", "Normals", "Projected high-surface normal in the low mesh UV tangent frame."},
        {"world-normal", "World normal", "Normals", "Projected high-surface normal in scene/world axes."},
        {"object-normal", "Object normal", "Normals", "Projected normal in the low target's original local axes."},
        {"bent-normal", "Bent tangent normal", "Normals", "Average unoccluded hemisphere direction, tangent frame."},
        {"bent-world-normal", "Bent world normal", "Normals", "Average unoccluded hemisphere direction, world axes."},
        {"bent-object-normal", "Bent object normal", "Normals", "Average unoccluded hemisphere direction, object axes."},
        {"bevel-normal", "Bevel normal", "Normals", "Geometry-derived rounded-edge normal approximation."},
        {"occlusion", "Ambient occlusion", "Surface", "Hemisphere visibility against the high mesh."},
        {"occlusion-secondary", "Secondary AO", "Surface", "A second hemisphere-visibility bake, its own distance."},
        {"curvature", "Curvature", "Surface", "Signed local normal variation estimate."},
        {"convexity", "Convexity", "Surface", "Positive signed curvature isolated as a bright-edge mask."},
        {"cavity", "Cavity / concavity", "Surface", "Negative signed curvature isolated as a bright-recess mask."},
        {"thickness", "Thickness", "Surface", "Inward high-mesh ray distance divided by thickness range."},
        {"height", "Projection height", "Surface", "Signed high-to-low distance along the low normal."},
        {"vector-displacement", "Vector displacement", "Surface", "Full projected high-minus-low XYZ offset."},
        {"position", "Position", "Coordinates & IDs", "Projected high-surface XYZ normalized to the world bbox."},
        {"id", "Object ID", "Coordinates & IDs", "24-bit source-object identity, little-endian RGB."},
        {"uv-island", "UV island ID", "Coordinates & IDs", "A distinct display colour per connected low UV chart."},
        {"uv", "UV coordinates", "Coordinates & IDs", "Low target UV0 coordinates in R/G, blue = 1."},
        {"wireframe", "UV wireframe", "Coordinates & IDs", "Low triangle edges in texture space."},
        {"coverage", "UV coverage", "Masks", "White on covered low UV texels; black outside."},
        {"alpha", "Alpha / hit mask", "Masks", "White where a high surface was projected, black for misses."},
        {"bevel-mask", "Bevel mask", "Masks", "Angular change from rounded-edge normal averaging."},
        {"dust", "Dust mask", "Masks", "Derived upward-facing deposition mask."},
        {"dirt", "Dirt mask", "Masks", "Derived occlusion plus concavity mask."},
        {"edge-wear", "Edge wear mask", "Masks", "Derived convexity weighted by surface exposure."},
        {"base-color", "Vertex color", "Color", "Interpolated high vertex colour, sRGB; white if none."},
    };
    return Table;
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
void BakingWorkspacePanel::IntegrateContent(double) noexcept { PollBake(); }

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

void BakingWorkspacePanel::ConstructMeshMapGrid(float Width)
{
    ImGui::TextColored(Spectrum(0x9a9a8f), "MESH MAPS");
    ImGui::SameLine();
    const char *Presets[]{"Essential", "Masks", "Normals", "All", "Clear"};
    for (const char *Preset : Presets)
    {
        ImGui::SameLine();
        if (ImGui::SmallButton(Preset)) ApplyPreset(Preset);
        Track(std::string("Preset ") + Preset);
    }
    ImGui::Separator();
    const char *LastFamily = "";
    const uint32_t PerRow = std::max(1u, uint32_t(Width / (84 * InterfaceScale)));
    uint32_t Column = 0;
    for (const auto &Entry : MeshMapTable())
    {
        if (std::strcmp(LastFamily, Entry.Family) != 0)
        {
            if (*LastFamily) ImGui::NewLine();
            ImGui::TextColored(Spectrum(0x716f62), "%s", Entry.Family);
            LastFamily = Entry.Family;
            Column = 0;
        }
        const bool Implemented = IsMeshMapImplemented(Entry.Key);
        const bool Enabled = std::find(Settings.Channels.begin(), Settings.Channels.end(), std::string(Entry.Key)) != Settings.Channels.end();
        ImGui::PushID(Entry.Key);
        ImGui::BeginDisabled(!Implemented);
        ImGui::PushStyleColor(ImGuiCol_Button, Enabled ? Spectrum(0x3f7a4d) : Spectrum(0x1c1c1c));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Enabled ? Spectrum(0x4a9a5c) : Spectrum(0x2a2a2a));
        if (ImGui::Button(Entry.Label, ImVec2(80 * InterfaceScale, 34 * InterfaceScale)))
        {
            InspectMapKey = Entry.Key;
            if (Enabled) Settings.Channels.erase(std::remove(Settings.Channels.begin(), Settings.Channels.end(), std::string(Entry.Key)), Settings.Channels.end());
            else Settings.Channels.push_back(Entry.Key);
        }
        Track(std::string("Tile ") + Entry.Key);
        ImGui::PopStyleColor(2);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\n%s", Entry.Description, Implemented ? (Enabled ? "Enabled · will be baked" : "Disabled") : "Not yet implemented in this native build.");
        ImGui::PopID();
        if (++Column < PerRow) ImGui::SameLine();
        else Column = 0;
    }
    ImGui::NewLine();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ASSETS DOCK (LEFT)
//------------------------------------------------------------------------------------------------------------------------

void BakingWorkspacePanel::ConstructAssetsDock(PlaneExtent Region)
{
    ImGui::SetNextWindowPos(ImVec2(Region.MinimumX, Region.MinimumY));
    ImGui::SetNextWindowSize(ImVec2(Region.Width(), Region.Height()));
    ImGui::Begin("Bake project", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextColored(Spectrum(0xc5c5b8), "Baking");
    ImGui::TextColored(Spectrum(0x716f62), "Map-only workflow");
    ImGui::Separator();
    const char *Modes[]{"Mesh maps", "Material patch", "Volume SDF"};
    for (int Index = 0; Index < 3; ++Index)
    {
        if (Index) ImGui::SameLine();
        const bool Selected = int(Mode) == Index;
        ImGui::PushStyleColor(ImGuiCol_Button, Selected ? Spectrum(0x2e2e2e) : Spectrum(0x161616));
        if (ImGui::Button(Modes[Index])) Mode = StudioMode(Index);
        ImGui::PopStyleColor();
    }
    ImGui::Separator();
    ImGui::TextColored(Spectrum(0x9a9a8f), "SOURCE GEOMETRY");
    ImGui::TextDisabled("%s", Assets.Low.Source.c_str());

    const auto LowSummary = SummarizeMesh(Assets.Low);
    ImGui::Text("Low UV targets (%u objects)", LowSummary.Objects);
    ImGui::TextColored(Spectrum(0xd9d4c4), "%llu triangles", (unsigned long long)LowSummary.Triangles);
    ImGui::TextDisabled("UV0 %s", LowSummary.HasUv ? "present" : "missing");
    ImGui::BeginDisabled(BakeRunning.load());
    if (ImGui::Button("Import low mesh", ImVec2(-1, 0)))
        Info = "Mesh import (OBJ/GLB/PLY/STL) is not implemented in this native build yet; use a built-in study mesh.";
    ImGui::EndDisabled();
    for (const auto &Part : Assets.Low.Parts)
    {
        bool Checked = std::find(SelectedTargets.begin(), SelectedTargets.end(), Part.Name) != SelectedTargets.end();
        ImGui::BeginDisabled(BakeRunning.load());
        if (ImGui::Checkbox(Part.Name.c_str(), &Checked))
        {
            if (Checked) SelectedTargets.push_back(Part.Name);
            else SelectedTargets.erase(std::remove(SelectedTargets.begin(), SelectedTargets.end(), Part.Name), SelectedTargets.end());
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%u tris", Part.TriangleCount());
    }
    ImGui::Spacing();
    const auto HighSummary = SummarizeMesh(Assets.High);
    ImGui::Text("High source (%u objects)", HighSummary.Objects);
    ImGui::TextColored(Spectrum(0xd9d4c4), "%llu triangles", (unsigned long long)HighSummary.Triangles);
    ImGui::BeginDisabled(BakeRunning.load());
    if (ImGui::Button("Import high mesh", ImVec2(-1, 0)))
        Info = "Mesh import (OBJ/GLB/PLY/STL) is not implemented in this native build yet; use a built-in study mesh.";
    ImGui::EndDisabled();
    ImGui::TextWrapped("OBJ · GLB / embedded glTF · PLY · high-only STL. No external asset requests.");
    ImGui::Spacing();

    ImGui::TextUnformatted("Built-in study");
    ImGui::SetNextItemWidth(-1);
    ImGui::BeginDisabled(BakeRunning.load());
    if (ImGui::BeginCombo("##Demo", DemoShape.c_str()))
    {
        for (const char *Shape : {"Teapot", "Cube", "Sphere"})
            if (ImGui::Selectable(Shape, DemoShape == Shape)) SelectDemo(Shape);
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::Spacing();

    if (Mode == StudioMode::Mesh) ConstructMeshMapGrid(ImGui::GetContentRegionAvail().x);
    ImGui::Separator();
    ImGui::TextColored(Spectrum(0x716f62), "CPU BVH preview · %zu texture sets", Assets.Low.Parts.size());
    ImGui::End();
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
void BakingWorkspacePanel::ConstructMapPreview(PlaneExtent Region)
{
    auto *DrawList = ImGui::GetWindowDrawList();
    DrawList->AddRectFilled(ImVec2(Region.MinimumX, Region.MinimumY), ImVec2(Region.MaximumX, Region.MaximumY), ImGui::GetColorU32(Spectrum(0x111010)));
    const BakeMapImage *Image = nullptr;
    uint32_t Resolution = 0;
    for (const auto &Result : Results)
        if (Result.Name == PreviewSetName)
        {
            Resolution = Result.Resolution;
            for (const auto &Map : Result.Maps)
                if (Map.Key == PreviewMapKey) Image = &Map;
        }
    if (!Image)
    {
        DrawList->AddText(ImVec2(Region.MinimumX + 10, Region.MinimumY + 10), ImGui::GetColorU32(Spectrum(0x716f62)), "No baked maps yet.");
        return;
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
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PREVIEW DOCK (CENTER)
//------------------------------------------------------------------------------------------------------------------------

void BakingWorkspacePanel::ConstructPreviewDock(PlaneExtent Region)
{
    ImGui::SetNextWindowPos(ImVec2(Region.MinimumX, Region.MinimumY));
    ImGui::SetNextWindowSize(ImVec2(Region.Width(), Region.Height()));
    ImGui::Begin("Baking viewport", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::Text("%s", (PreviewSetName.empty() ? Assets.Low.Label : PreviewSetName).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(View == ViewMode::Preview3D ? "3D preview" : "2D maps")) {}
    ImGui::SameLine();
    if (ImGui::SmallButton("3D")) View = ViewMode::Preview3D;
    Track("View3D");
    ImGui::SameLine();
    if (ImGui::SmallButton("2D")) View = ViewMode::Maps2D;
    Track("View2D");

    if (View == ViewMode::Preview3D)
    {
        if (ImGui::SmallButton("Frame")) ResetCamera();
        ImGui::SameLine();
        ImGui::Checkbox("High overlay", &Overlay);
        ImGui::SameLine();
        ImGui::Checkbox("Wireframe", &Wireframe);
        ImGui::SameLine();
        ImGui::TextDisabled("Orbit · scroll to zoom");
        const ImVec2 CursorScreen = ImGui::GetCursorScreenPos();
        const ImVec2 Available = ImGui::GetContentRegionAvail();
        const float ViewportHeight = std::max(80.f, Available.y - 90.f * InterfaceScale);
        // 📝 ConstructViewport3D's own ImGui::InvisibleButton already reserves (Available.x, ViewportHeight)
        // of layout space - an extra Dummy() here would double-reserve it and push everything below (the
        // Bake button, results, status bar) far off-screen.
        ConstructViewport3D(Spanning(CursorScreen.x, CursorScreen.y, Available.x, ViewportHeight));
    }
    else
    {
        ImGui::SetNextItemWidth(160);
        if (ImGui::BeginCombo("##Set", PreviewSetName.c_str()))
        {
            for (const auto &Result : Results)
                if (ImGui::Selectable(Result.Name.c_str(), Result.Name == PreviewSetName)) PreviewSetName = Result.Name;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160);
        if (ImGui::BeginCombo("##Map", FindMeshMap(PreviewMapKey) ? FindMeshMap(PreviewMapKey)->Label : PreviewMapKey.c_str()))
        {
            for (const auto &Key : Settings.Channels)
            {
                const auto *Meta = FindMeshMap(Key);
                if (ImGui::Selectable(Meta ? Meta->Label : Key.c_str(), Key == PreviewMapKey)) PreviewMapKey = Key;
            }
            ImGui::EndCombo();
        }
        const ImVec2 CursorScreen = ImGui::GetCursorScreenPos();
        const ImVec2 Available = ImGui::GetContentRegionAvail();
        const float ImageSize = std::max(64.f, std::min(Available.x, Available.y) - 28.f);
        ConstructMapPreview(Spanning(CursorScreen.x, CursorScreen.y, ImageSize, ImageSize));
        ImGui::Dummy(ImVec2(ImageSize, ImageSize + 4));
    }

    ImGui::Separator();
    if (BakeRunning.load())
    {
        const float Fraction = BakeTotal.load() ? float(double(BakeDone.load()) / double(BakeTotal.load())) : 0.f;
        ImGui::ProgressBar(Fraction, ImVec2(-1, 0), BakeProgressLabel.c_str());
    }
    ImGui::BeginDisabled(BakeRunning.load() || SelectedTargets.empty() || Settings.Channels.empty());
    if (ImGui::Button(SelectedTargets.size() > 1 ? "Bake all selected meshes" : "Bake mesh maps", ImVec2(220 * InterfaceScale, 0))) StartBake();
    Track("Bake");
    ImGui::EndDisabled();
    if (!Results.empty())
    {
        ImGui::SameLine();
        if (ImGui::Button("Export maps (PNG)")) ExportResults();
        Track("Export");
        ImGui::SameLine();
        ImGui::TextColored(Spectrum(0x9fd9a8), "%zu result%s ready", Results.size(), Results.size() == 1 ? "" : "s");
        for (const auto &Result : Results)
            ImGui::TextDisabled("%s: %u charts, %llu hits, %llu misses", Result.Name.c_str(), Result.Stats.Charts,
                (unsigned long long)Result.Stats.Hits, (unsigned long long)Result.Stats.Misses);
    }
    ImGui::End();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     INSPECTOR (RIGHT)
//------------------------------------------------------------------------------------------------------------------------

void BakingWorkspacePanel::ConstructInspector(PlaneExtent Region)
{
    ImGui::SetNextWindowPos(ImVec2(Region.MinimumX, Region.MinimumY));
    ImGui::SetNextWindowSize(ImVec2(Region.Width(), Region.Height()));
    ImGui::Begin("Baking inspector", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    if (Mode == StudioMode::Material) { ImGui::TextWrapped("Material patch mode is not yet native — ported in a later pass."); ImGui::End(); return; }
    if (Mode == StudioMode::Volume) { ImGui::TextWrapped("Volume SDF mode is not yet native — ported in a later pass."); ImGui::End(); return; }

    ImGui::TextColored(Spectrum(0x716f62), "STATIC HIGH -> LOW");
    ImGui::Text("Mesh map bake");
    ImGui::TextDisabled("%zu selected texture set%s", SelectedTargets.size(), SelectedTargets.size() == 1 ? "" : "s");
    ImGui::Separator();
    ImGui::TextColored(Spectrum(0x9a9a8f), "TEXTURE SETTINGS");
    int ResolutionIndex = 0;
    const int Resolutions[]{64, 128, 256, 512, 1024};
    for (int Index = 0; Index < 5; ++Index) if (Resolutions[Index] == int(Settings.Resolution)) ResolutionIndex = Index;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##Resolution", (std::to_string(Settings.Resolution) + " x " + std::to_string(Settings.Resolution)).c_str()))
    {
        for (int Value : Resolutions)
        {
            const std::string Label = std::to_string(Value) + " x " + std::to_string(Value);
            if (ImGui::Selectable(Label.c_str(), Value == int(Settings.Resolution))) Settings.Resolution = uint32_t(Value);
        }
        ImGui::EndCombo();
    }
    int Padding = int(Settings.Padding);
    if (ImGui::SliderInt("Padding", &Padding, 0, 32)) Settings.Padding = uint32_t(std::clamp(Padding, 0, 32));
    const char *NormalFormats[]{"OpenGL +Y", "DirectX -Y"};
    int NormalIndex = Settings.NormalPlusY ? 0 : 1;
    if (ImGui::Combo("Normal format", &NormalIndex, NormalFormats, 2)) Settings.NormalPlusY = NormalIndex == 0;

    ImGui::Spacing();
    ImGui::TextColored(Spectrum(0x9a9a8f), "MAP INSPECTOR");
    const auto *Meta = FindMeshMap(InspectMapKey);
    if (Meta)
    {
        ImGui::TextColored(Spectrum(0xd9d4c4), "%s", Meta->Label);
        ImGui::TextWrapped("%s", Meta->Description);
        const bool Enabled = std::find(Settings.Channels.begin(), Settings.Channels.end(), InspectMapKey) != Settings.Channels.end();
        ImGui::TextDisabled("%s", IsMeshMapImplemented(InspectMapKey) ? (Enabled ? "Enabled - will be baked" : "Disabled - click its tile to enable")
                                                                       : "Not yet implemented in this native build.");
    }

    ImGui::Spacing();
    ImGui::TextColored(Spectrum(0x9a9a8f), "HIGH-TO-LOW PROJECTION");
    const char *Methods[]{"Normal ray", "Nearest surface"};
    int MethodIndex = Settings.ProjectionRay ? 0 : 1;
    if (ImGui::Combo("Method", &MethodIndex, Methods, 2)) Settings.ProjectionRay = MethodIndex == 0;
    ImGui::DragFloat("Front distance", &Settings.Front, 0.01f, 0.0001f, 10.f);
    ImGui::DragFloat("Back distance", &Settings.Back, 0.01f, 0.0001f, 10.f);
    ImGui::DragFloat("AO distance", &Settings.AoDistance, 0.01f, 0.0001f, 10.f);
    ImGui::DragFloat("Secondary AO distance", &Settings.SecondaryAoDistance, 0.01f, 0.0001f, 10.f);
    ImGui::DragFloat("Thickness range", &Settings.ThicknessRange, 0.01f, 0.0001f, 10.f);
    ImGui::DragFloat("Curvature radius", &Settings.CurvatureRadius, 0.001f, 0.0001f, 10.f);
    int Samples = int(Settings.Samples);
    const int SampleOptions[]{4, 8, 16, 32, 64};
    if (ImGui::BeginCombo("AO samples", std::to_string(Samples).c_str()))
    {
        for (int Value : SampleOptions) if (ImGui::Selectable(std::to_string(Value).c_str(), Value == Samples)) Settings.Samples = uint32_t(Value);
        ImGui::EndCombo();
    }
    ImGui::Checkbox("Match objects by name", &Settings.MatchByName);
    ImGui::TextWrapped("Distances use source scene units. Ray front/back limits act as a uniform projection envelope, not a custom cage mesh.");

    ImGui::Spacing();
    ImGui::TextColored(Spectrum(0x9a9a8f), "BAKE CONTRACT");
    ImGui::TextWrapped("Each low object is its own UV texture set. Misses, chart count and coverage are reported rather than hidden.");
    ImGui::TextWrapped("Tangent normal, world normal, UV coverage and the alpha/hit mask are evaluated by a real CPU BVH projection in "
                        "this native build. The remaining mesh maps in the 27-map contract are disabled tiles, not fabricated pixels.");
    ImGui::End();
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
