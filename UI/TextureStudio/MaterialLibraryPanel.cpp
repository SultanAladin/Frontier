/*==========================================================================================================================================
                                                        MATERIALLIBRARYPANEL.CPP
==========================================================================================================================================*/
// 🧩 Native material discovery, selection, edit/apply/revert and explicit disk operations; no simulated renderer thumbnails.

#include "MaterialLibraryPanel.h"
#include "Engine/Interface/ContentDirectory.h"
#include "Engine/Interface/DropdownControls.h"
#include <algorithm>
#include <cstdio>
#include <imgui.h>
#include <imgui_internal.h>
#include <set>
#include <optional>

namespace Frontier
{
namespace
{
ImU32 EncodeMaterialInk(uint32_t Rgb, unsigned Alpha = 255)
{
    return IM_COL32((Rgb >> 16) & 255, (Rgb >> 8) & 255, Rgb & 255, Alpha);
}
ColorQuad DecodeMaterialSpectrum(uint32_t Rgb)
{
    return {float((Rgb >> 16) & 255) / 255, float((Rgb >> 8) & 255) / 255, float(Rgb & 255) / 255, 1};
}
void ConstructMaterialCaption(float X, float Y, float Size, uint32_t Rgb, const char *Caption, ImFont *Typeface = nullptr)
{
    ImGui::GetWindowDrawList()->AddText(Typeface ? Typeface : ImGui::GetFont(), Size * 1.302f, ImVec2(X, Y - Size * .15f), EncodeMaterialInk(Rgb, unsigned(255 * ImGui::GetStyle().Alpha)), Caption);
}
bool ConstructMaterialPill(const char *Caption, ImVec2 Position, ImVec2 Extent, bool Selected = false)
{
    ImGui::SetCursorScreenPos(Position);
    const bool Pressed = ImGui::InvisibleButton(Caption, Extent);
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(Position, ImVec2(Position.x + Extent.x, Position.y + Extent.y),
        EncodeMaterialInk(Selected ? 0x282825 : ImGui::IsItemHovered() ? 0x2e2e2b : 0x181818), Extent.y * .5f);
    Ink->AddRect(Position, ImVec2(Position.x + Extent.x, Position.y + Extent.y), EncodeMaterialInk(0xffffff, Selected ? 24 : 8), Extent.y * .5f);
    const char *CaptionEnd = ImGui::FindRenderedTextEnd(Caption);
    const auto CaptionExtent = ImGui::CalcTextSize(Caption, CaptionEnd);
    Ink->AddText(ImVec2(Position.x + (Extent.x - CaptionExtent.x) * .5f, Position.y + (Extent.y - CaptionExtent.y) * .5f),
        EncodeMaterialInk(Selected ? 0xcccdbb : 0x858577), Caption, CaptionEnd);
    return Pressed;
}
float *QueryPaintCoefficient(AutomotivePaint &Paint, std::string_view Key)
{
    if (Key == "flakes") return &Paint.Distribution[0];
    if (Key == "flakeSize") return &Paint.Distribution[1];
    if (Key == "flakeScale") return &Paint.Distribution[2];
    if (Key == "depth") return &Paint.Distribution[3];
    if (Key == "flakeRoughnessMin") return &Paint.Finish[0];
    if (Key == "flakeRoughnessMax") return &Paint.Finish[1];
    if (Key == "flakeMetalnessMin") return &Paint.Finish[2];
    if (Key == "flakeMetalnessMax") return &Paint.Finish[3];
    if (Key == "flakeTilt") return &Paint.Relief[0];
    if (Key == "flakeLayerDepth") return &Paint.Relief[1];
    if (Key == "orangePeel") return &Paint.Relief[2];
    if (Key == "orangePeelScale") return &Paint.Relief[3];
    return nullptr;
}
std::optional<float> QueryRecipeCoefficient(MaterialDefinition &Definition, std::string_view Key)
{
    const uint32_t Index = Key.starts_with("coat") ? 1 : 0;
    if (Index >= Definition.Scattering.Illumination.Configuration[0]) return {};
    auto &Stratum = Definition.Scattering.Illumination.Strata[Index];
    if (Key == "roughness" || Key == "coatRoughness") return Stratum.AlbedoRoughness[3];
    if (Key == "metalness") return Stratum.Optics[0];
    if (Key == "ior" || Key == "coatIor") return Stratum.Optics[1];
    if (Key == "coat") return Stratum.Optics[2];
    if (Key == "anisotropy") return Definition.Scattering.DirectionalOptics[Index][0];
    if (Key == "specular") return Definition.Scattering.DirectionalOptics[Index][3];
    if (Key == "heightStrength") return Definition.Scattering.StratumNormal[Index][1];
    if (Key == "normalStrength") return Definition.Scattering.StratumNormal[Index][0];
    if (Definition.Scattering.Paint.Configuration[3])
    {
        if (Key == "flakeLayers") return float(Definition.Scattering.Paint.Configuration[1]);
        if (const auto *Coefficient = QueryPaintCoefficient(Definition.Scattering.Paint, Key)) return *Coefficient;
    }
    return {};
}
const MaterialCatalogueEntry *QuerySelectedMaterial(const MaterialLibrary &Library)
{
    for (const auto &Entry : Library.QueryEntries())
        if (Entry.RelativePath == Library.QuerySelection()) return &Entry;
    return nullptr;
}
bool ConstructMaterialSelection(const char *Caption, const char *Preview, const char *const *Options,
    uint32_t Count, int &Selection, float Scale)
{
    bool Changed = false;
    ImGui::PushID(Caption);
    if (DropdownControlSurface::BeginSelection("##MaterialSelection", Preview, Scale))
    {
        for (uint32_t Index = 0; Index < Count; ++Index)
            if (DropdownControlSurface::Option(Options[Index], Selection == int(Index), Scale))
            {
                Selection = int(Index);
                Changed = true;
                ImGui::CloseCurrentPopup();
            }
        DropdownControlSurface::EndSelection();
    }
    ImGui::PopID();
    return Changed;
}
bool ConstructMaterialToggle(const char *Caption, bool &Enabled, float Scale)
{
    const auto Origin = ImGui::GetCursorScreenPos();
    const float Width = ImGui::GetContentRegionAvail().x;
    const ImVec2 Extent(Width, 32 * Scale);
    const bool Pressed = ImGui::InvisibleButton(Caption, Extent);
    if (Pressed) Enabled = !Enabled;
    const auto Minimum = ImVec2(Origin.x + Width - 46 * Scale, Origin.y + 3 * Scale);
    const auto Maximum = ImVec2(Minimum.x + 46 * Scale, Minimum.y + 26 * Scale);
    auto *Ink = ImGui::GetWindowDrawList();
    const ImU32 Track = ImGui::GetColorU32(Enabled ? ImVec4(.31f,.67f,.48f,1) : ImVec4(.23f,.24f,.25f,1));
    Ink->AddText(ImVec2(Origin.x, Origin.y + 7 * Scale), ImGui::GetColorU32(ImGuiCol_Text), Caption);
    Ink->AddRectFilled(Minimum, Maximum, Track, 13 * Scale);
    const float Centre = Enabled ? Maximum.x - 13 * Scale : Minimum.x + 13 * Scale;
    Ink->AddCircleFilled(ImVec2(Centre, (Minimum.y + Maximum.y) * .5f), 9 * Scale, IM_COL32(245,245,240,255));
    return Pressed;
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     MATERIAL OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

MaterialLibraryPanel::MaterialLibraryPanel() : SourceText(1048577, '\0')
{
    if (ImGui::GetCurrentContext()) ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    (void)Panels.ConfigureDockspaceKey("TextureWorkspace Material panels");
    (void)Panels.Register({"MaterialLibrary", "Material library", [this]() { ConstructCatalogue(); }, true, {}, {}});
    (void)Panels.Register({"MaterialViewport", "Viewport", [this]() { ConstructViewport(); }, true, {}, {}});
    (void)Panels.Register({"MaterialInspector", "Inspector", [this]() { ConstructInspector(); }, true, {}, {}});
    Panels.ConfigureWorkspaceInsets(79, 0);
    Panels.ConfigureTabExtent(140, 26);
    (void)Panels.ConfigureColumns({{"MaterialLibrary", .26f}, {"MaterialViewport", .48f}, {"MaterialInspector", .26f}});
    std::snprintf(NameText.data(), NameText.size(), "%s", "Untitled material");
    std::snprintf(DestinationText.data(), DestinationText.size(), "%s", "Untitled.material");
}

void MaterialLibraryPanel::ConfigureInterfaceScale(float Scale) noexcept
{
    if (std::isfinite(Scale)) InterfaceScale = std::clamp(Scale, .75f, 1.5f);
}
float MaterialLibraryPanel::QueryInterfaceScale() const noexcept
{
    return InterfaceScale * ImGui::GetStyle().FontScaleDpi;
}

bool MaterialLibraryPanel::ConfigureDirectory(const std::filesystem::path &Directory)
{
    if (Library.QueryModified() || !Library.ConfigureDirectory(Directory)) return false;
    std::snprintf(DirectoryText.data(), DirectoryText.size(), "%s", EncodeContentPath(Library.QueryDirectory()).c_str());
    SynchronizeSource();
    return true;
}

bool MaterialLibraryPanel::SelectMaterial(const std::filesystem::path &RelativePath)
{
    if (Library.QueryModified() || !Library.OpenMaterial(RelativePath)) return false;
    SynchronizeSource();
    return true;
}

void MaterialLibraryPanel::ReplaceSource(std::string Source)
{
    Library.ReplaceSource(std::move(Source));
    SynchronizeSource();
}

bool MaterialLibraryPanel::RequestClose() noexcept
{
    if (!Library.QueryModified()) return true;
    ClosingRequested = true;
    return false;
}

bool MaterialLibraryPanel::ConsumeClose() noexcept
{
    const bool Authorized = CloseAuthorized;
    CloseAuthorized = false;
    return Authorized;
}

void MaterialLibraryPanel::SynchronizeSource()
{
    std::snprintf(SourceText.data(), SourceText.size(), "%s", Library.QuerySource().c_str());
}

void MaterialLibraryPanel::FinalizeContent() noexcept
{
    ActivatePattern = {};
    Library = MaterialLibrary{};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     LIBRARY AND SOURCE EDITOR
//------------------------------------------------------------------------------------------------------------------------

void MaterialLibraryPanel::ConstructCatalogue()
{
    const float Scale = QueryInterfaceScale();
    const auto Origin = ImGui::GetCursorScreenPos();
    const float Width = ImGui::GetContentRegionAvail().x;
    const float Bottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
    const auto At = [&](float X, float Y) { return ImVec2(Origin.x + X * Scale, Origin.y + Y * Scale); };
    ImGui::PushFont(nullptr, 9 * 1.302f * Scale);
    if (ConstructMaterialPill("Workspace", At(14, 10), ImVec2((Width - 34 * Scale) * .5f, 27 * Scale), !CollectionVisible)) CollectionVisible = false;
    const auto CollectionCaption = "My collection  " + std::to_string(Favourites.size());
    if (ConstructMaterialPill(CollectionCaption.c_str(), ImVec2(Origin.x + Width * .5f + 3 * Scale, Origin.y + 10 * Scale),
        ImVec2((Width - 34 * Scale) * .5f, 27 * Scale), CollectionVisible)) CollectionVisible = true;
    ConstructMaterialCaption(At(20, 56).x, At(20, 56).y, 18 * Scale, 0xd3d3c8, "Material library", static_cast<ImFont *>(HeadingTypeface));
    ConstructMaterialCaption(At(155, 61).x, At(155, 61).y, 8 * Scale, 0x777766, std::to_string(Library.QueryEntries().size()).c_str());
    if (ConstructMaterialPill("##New material", ImVec2(Origin.x + Width - 40 * Scale, At(0, 52).y), ImVec2(24 * Scale, 24 * Scale))) Creating = true;
    ConstructIcon(IconSymbol::EditorAdd, Origin.x + Width - 35 * Scale, At(0, 57).y, 14 * Scale);
    ImGui::SetCursorScreenPos(At(14, 96));
    ImGui::SetNextItemWidth(Width - 28 * Scale);
    ImGui::PushFont(nullptr, 10 * 1.302f * Scale);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(25.f / 255, 25.f / 255, 25.f / 255, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(35 * Scale, 11.5f * Scale));
    ImGui::InputTextWithHint("##Search", "Search materials...", SearchText.data(), SearchText.size());
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ConstructIcon(IconSymbol::EditorSearch, At(27, 108).x, At(27, 108).y, 15 * Scale);
    Library.ConfigureFilter(SearchText.data());
    const char *Categories[]{"All materials", "Paint", "Metal", "Ceramic", "Fabric", "Rubber", "Glass", "Plastic", "Leather", "Clay", "Wax", "Skin", "Paper", "Technical", "Stone", "Wall", "Nature"};
    float X = 14, Y = 144;
    ImGui::PushFont(nullptr, 8 * 1.302f * Scale);
    for (const char *Category : Categories)
    {
        const float Edge = ImGui::CalcTextSize(Category).x / Scale + 16;
        if (X + Edge > Width / Scale - 14) { X = 14; Y += 29; }
        if (ConstructMaterialPill(Category, At(X, Y), ImVec2(Edge * Scale, 24 * Scale), CategoryFilter == Category)) CategoryFilter = Category;
        X += Edge + 5;
    }
    Y += 24 + 8;
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddLine(At(0, Y), At(Width / Scale, Y), EncodeMaterialInk(0xffffff, 13));
    ConstructMaterialCaption(At(17, Y + 12).x, At(17, Y + 12).y, 8 * Scale, 0x7e7e70, "THE ESSENTIAL COLLECTION");
    std::vector<size_t> Visible;
    for (size_t Index : Library.QueryVisible())
    {
        const auto &Entry = Library.QueryEntries()[Index];
        if (CategoryFilter != "All materials" && Entry.Category != CategoryFilter) continue;
        if (CollectionVisible && !Favourites.contains(EncodeContentPath(Entry.RelativePath))) continue;
        Visible.push_back(Index);
    }
    ConstructMaterialCaption(Origin.x + Width - 30 * Scale, At(0, Y + 12).y, 8 * Scale, 0x7e7e70, std::to_string(Visible.size()).c_str());
    ImGui::PopFont();
    const float FooterY = Bottom - 71 * Scale;
    ImGui::SetCursorScreenPos(At(16, Y + 31));
    ImGui::BeginChild("Material grid", ImVec2(Width - 32 * Scale, std::max(1.f, FooterY - At(0, Y + 31).y)), ImGuiChildFlags_None);
    const float Gap = 9 * Scale;
    const float CardWidth = std::max(32.f, (ImGui::GetContentRegionAvail().x - 20 * Scale - Gap) * .5f);
    const float CardHeight = 125 * Scale;
    const auto GridOrigin = ImGui::GetCursorScreenPos();
    const float ClipTop = ImGui::GetWindowPos().y, ClipBottom = ClipTop + ImGui::GetWindowSize().y;
    std::vector<std::filesystem::path> Requests;
    for (size_t Ordinal = 0; Ordinal < Visible.size(); ++Ordinal)
    {
        const float Top = GridOrigin.y + (Ordinal / 2) * (CardHeight + Gap);
        if (Top + CardHeight >= ClipTop && Top < ClipBottom) Requests.push_back(Library.QueryEntries()[Visible[Ordinal]].RelativePath);
    }
    // 📝 Keep the inspector thumbnail resident even when its selected card scrolls out of view.
    if (!Library.QuerySelection().empty() && std::find(Requests.begin(), Requests.end(), Library.QuerySelection()) == Requests.end())
        Requests.push_back(Library.QuerySelection());
    if (RequestThumbnails) RequestThumbnails(Requests);
    for (size_t Ordinal = 0; Ordinal < Visible.size(); ++Ordinal)
    {
        const auto &Entry = Library.QueryEntries()[Visible[Ordinal]];
        const ImVec2 Position(GridOrigin.x + 12 * Scale + (Ordinal % 2) * (CardWidth + Gap), GridOrigin.y + (Ordinal / 2) * (CardHeight + Gap));
        if (Position.y + CardHeight < ClipTop || Position.y > ClipBottom) continue;
        ImGui::SetCursorScreenPos(Position);
        ImGui::PushID(int(Visible[Ordinal]));
        ImGui::BeginDisabled(Library.QueryModified());
        if (ImGui::InvisibleButton("Material card", ImVec2(CardWidth, CardHeight))) SelectMaterial(Entry.RelativePath);
        ImGui::EndDisabled();
        const bool Hovered = ImGui::IsItemHovered();
        PixelSpace Surface;
        (void)Surface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, Scale);
        const PlaneExtent Card{Position.x / Scale, Position.y / Scale, (Position.x + CardWidth) / Scale, (Position.y + CardHeight) / Scale};
        (void)ControlKit::SectionCard(Surface, Card, 15);
        auto *CardInk = ImGui::GetWindowDrawList();
        if (Hovered) CardInk->AddRect(Position, ImVec2(Position.x + CardWidth, Position.y + CardHeight), EncodeMaterialInk(0x707173), 15 * Scale);
        const auto Image = QueryThumbnail ? QueryThumbnail(Entry.RelativePath) : MaterialPreviewImage{};
        CardInk->PushClipRect(Position, ImVec2(Position.x + CardWidth, Position.y + CardHeight), true);
        if (Image.Texture)
            CardInk->AddImage(ImTextureRef(Image.Texture), ImVec2(Position.x + (CardWidth - 85 * Scale) * .5f, Position.y),
                ImVec2(Position.x + (CardWidth + 85 * Scale) * .5f, Position.y + 85 * Scale),
                ImVec2(Image.MinimumU, Image.MinimumV), ImVec2(Image.MaximumU, Image.MaximumV));
        ConstructMaterialCaption(Position.x + 12 * Scale, Position.y + 89 * Scale, 9 * Scale, 0xc8c8b6, Entry.Name.c_str());
        ConstructMaterialCaption(Position.x + 12 * Scale, Position.y + 105 * Scale, 7 * Scale, 0x767665, Entry.Category.empty() ? "Layered material" : Entry.Category.c_str());
        if (Library.QuerySelection() == Entry.RelativePath)
        {
            CardInk->AddCircleFilled(ImVec2(Position.x + 14 * Scale, Position.y + 14 * Scale), 6 * Scale, EncodeMaterialInk(0xa5b99c));
            CardInk->AddLine(ImVec2(Position.x + 11 * Scale, Position.y + 14 * Scale), ImVec2(Position.x + 13 * Scale, Position.y + 16 * Scale), EncodeMaterialInk(0x2a3927));
            CardInk->AddLine(ImVec2(Position.x + 13 * Scale, Position.y + 16 * Scale), ImVec2(Position.x + 18 * Scale, Position.y + 11 * Scale), EncodeMaterialInk(0x2a3927));
        }
        CardInk->PopClipRect();
        if (Hovered) ImGui::SetTooltip("%s\n%s", Entry.Name.c_str(), Entry.Description.c_str());
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(GridOrigin.x, GridOrigin.y + ((Visible.size() + 1) / 2) * (CardHeight + Gap)));
    ImGui::Dummy(ImVec2(1, 1));
    ImGui::EndChild();
    Ink->AddRectFilled(ImVec2(Origin.x, FooterY), ImVec2(Origin.x + Width, Bottom), EncodeMaterialInk(0x151515));
    ConstructIcon(IconSymbol::Stars, Origin.x + 19 * Scale, FooterY + 20 * Scale, 19 * Scale);
    ConstructMaterialCaption(Origin.x + 51 * Scale, FooterY + 20 * Scale, 9 * Scale, 0xaeae9b, "Beautiful by calculation.");
    const auto Footer = std::to_string(Library.QueryEntries().size()) + " presets. Vector + procedural surfaces.";
    ConstructMaterialCaption(Origin.x + 51 * Scale, FooterY + 37 * Scale, 7 * Scale, 0x6b6b60, Footer.c_str());
    ImGui::PopFont();
}

void MaterialLibraryPanel::ConstructViewport()
{
    const float Scale = QueryInterfaceScale();
    const auto Origin = ImGui::GetCursorScreenPos();
    const auto Available = ImGui::GetContentRegionAvail();
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilledMultiColor(Origin, ImVec2(Origin.x + Available.x, Origin.y + Available.y),
        EncodeMaterialInk(0x202222), EncodeMaterialInk(0x202222), EncodeMaterialInk(0x303235), EncodeMaterialInk(0x303235));
    const auto Image = QueryPreview ? QueryPreview() : MaterialPreviewImage{};
    // 📐 The subject occupies the whole viewport panel between the 42px header and the 45px footer. It is fitted to that
    //    rectangle rather than centred in a fixed square, so no part of the sphere is ever clipped by the panel edge.
    const float ViewportTop = Origin.y + 42 * Scale;
    const float ViewportBottom = Origin.y + Available.y - 45 * Scale;
    const float ViewportHeight = std::max(1.f, ViewportBottom - ViewportTop);
    const float ViewportWidth = std::max(1.f, Available.x);
    const ImVec2 Minimum(Origin.x, ViewportTop);
    const ImVec2 Maximum(Origin.x + ViewportWidth, ViewportBottom);
    // 📐 Keep the subject's true proportions: fit the sphere inside the viewport rectangle instead of stretching it.
    if (ConfigurePreviewExtent)
        ConfigurePreviewExtent(uint32_t(std::lround(ViewportWidth)), uint32_t(std::lround(ViewportHeight)));
    if (Image.Texture) Ink->AddImage(ImTextureRef(Image.Texture), Minimum, Maximum,
        ImVec2(Image.MinimumU, Image.MinimumV), ImVec2(Image.MaximumU, Image.MaximumV));
    Ink->AddRectFilled(Origin, ImVec2(Origin.x + Available.x, Origin.y + 42 * Scale), EncodeMaterialInk(0x151515));
    ConstructMaterialCaption(Origin.x + 18 * Scale, Origin.y + 15 * Scale, 7 * Scale, 0x898a78, "LIVE PREVIEW   PBR");
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + std::max(4.f, Available.x - 92 * Scale), Origin.y + 7 * Scale));
    if (ImGui::Button("Fullscreen##MaterialPreview", ImVec2(84 * Scale, 27 * Scale)))
    {
        FullscreenPreview = true;
        ResetMaterialPreviewView(Inspection);
        if (ConfigurePreview) ConfigurePreview(Inspection);
    }
    const auto FullscreenMinimum = ImGui::GetItemRectMin(), FullscreenMaximum = ImGui::GetItemRectMax();
    FullscreenPreviewExtent = {FullscreenMinimum.x, FullscreenMinimum.y, FullscreenMaximum.x, FullscreenMaximum.y};
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open a fitted, full-work-area material preview (Escape to close).");
    const auto *Entry = QuerySelectedMaterial(Library);
    ConstructMaterialCaption(Origin.x + 22 * Scale, Origin.y + 68 * Scale, 8 * Scale, 0x92917c,
        Entry && !Entry->Category.empty() ? ("01  /  " + Entry->Category + " COLLECTION").c_str() : "MATERIAL COLLECTION");
    ConstructMaterialCaption(Origin.x + 22 * Scale, Origin.y + 94 * Scale, 30 * Scale, 0xdcdccd, Entry ? Entry->Name.c_str() : "Material explorer", static_cast<ImFont *>(HeadingTypeface));
    if (Entry) ConstructMaterialCaption(Origin.x + 22 * Scale, Origin.y + 136 * Scale, 9 * Scale, 0x7b7c6d, Entry->Description.c_str());
    const float Bottom = Origin.y + Available.y;
    Ink->AddRectFilled(ImVec2(Origin.x, Bottom - 45 * Scale), ImVec2(Origin.x + Available.x, Bottom), EncodeMaterialInk(0x151515));
    ConstructMaterialCaption(Origin.x + 18 * Scale, Bottom - 27 * Scale, 7 * Scale, 0x7e7e70, "Drag orbit  /  Shift-MMB pan  /  Wheel zoom");
    if (Available.x > 500 * Scale) ConstructMaterialCaption(Origin.x + Available.x - 75 * Scale, Bottom - 27 * Scale, 7 * Scale, 0x7e7e70, "VULKAN");
    const auto Previous = Inspection;
    const auto ConfigureExtent = [&](size_t Index)
    {
        const auto Minimum = ImGui::GetItemRectMin();
        const auto Maximum = ImGui::GetItemRectMax();
        InspectionExtents[Index] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
    };
    ImGui::PushID("MaterialInspection");
    ImGui::BeginDisabled(!ConfigurePreview);
    // 📝 One canvas owns gestures until release; widgets and popup menus cannot also drive the camera.
    InspectionExtents[7] = {};
    const ImVec2 CanvasMinimum(Origin.x, Origin.y + 165 * Scale);
    const ImVec2 CanvasMaximum(Origin.x + Available.x, Bottom - 205 * Scale);
    if (CanvasMaximum.y > CanvasMinimum.y)
    {
        ImGui::SetCursorScreenPos(CanvasMinimum);
        ImGui::InvisibleButton("InspectionCanvas", ImVec2(Available.x, CanvasMaximum.y - CanvasMinimum.y),
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
        ConfigureExtent(7);
        const auto &Input = ImGui::GetIO();
        const bool Hovered = ImGui::IsItemHovered();
        const auto Δ = EvaluateNavigationDelta ? EvaluateNavigationDelta({Input.MouseDelta.x, Input.MouseDelta.y}) : PlanePoint{Input.MouseDelta.x, Input.MouseDelta.y};
        const float Sensitivity = EvaluateNavigationDelta ? EvaluateNavigationDelta({1,0}).X : 1;
        if (ImGui::IsItemActive())
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || (Input.KeyShift && ImGui::IsMouseDown(ImGuiMouseButton_Left)))
            {
                Inspection.PanX = std::clamp(Inspection.PanX + Input.MouseDelta.x * Sensitivity * 2 / ViewportWidth, -2.f, 2.f);
                Inspection.PanY = std::clamp(Inspection.PanY + Input.MouseDelta.y * Sensitivity * 2 / ViewportHeight, -2.f, 2.f);
            }
            else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
                Inspection.Zoom = std::clamp(Inspection.Zoom * std::exp(-Input.MouseDelta.y * Sensitivity * .01f), .5f, 2.f);
            else if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                Inspection.Yaw = std::remainder(Inspection.Yaw + Δ.X * .01f, 6.2831853f);
                Inspection.Pitch = std::clamp(Inspection.Pitch + Δ.Y * .01f, -1.45f, 1.45f);
            }
        }
        if (Hovered && !Input.WantTextInput)
        {
            if (Input.MouseWheel != 0) Inspection.Zoom = std::clamp(Inspection.Zoom * std::pow(1.1f, Input.MouseWheel), .5f, 2.f);
            if (ImGui::IsKeyPressed(ImGuiKey_F) || ImGui::IsKeyPressed(ImGuiKey_Home) || ImGui::IsMouseDoubleClicked(0))
                ResetMaterialPreviewView(Inspection);
            if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) Inspection.Zoom = std::min(2.f, Inspection.Zoom + .1f);
            if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) Inspection.Zoom = std::max(.5f, Inspection.Zoom - .1f);
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) Inspection.PanX = std::max(-2.f, Inspection.PanX - .05f);
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) Inspection.PanX = std::min(2.f, Inspection.PanX + .05f);
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) Inspection.PanY = std::max(-2.f, Inspection.PanY - .05f);
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) Inspection.PanY = std::min(2.f, Inspection.PanY + .05f);
        }
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * Scale, 5 * Scale));
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 18 * Scale, Bottom - 155 * Scale));
    ImGui::SetNextItemWidth(std::min(148 * Scale, Available.x * .42f));
    const char *ShapeOptions[]{"Shader ball", "Sphere", "Plane", "Cube"};
    int Shape = int(Inspection.Shape);
    ImGui::SetNextItemWidth(std::min(148 * Scale, Available.x * .42f));
    if (ConstructMaterialSelection("Preview shape", ShapeOptions[Shape], ShapeOptions, 4, Shape, Scale))
        Inspection.Shape = MaterialPreviewShape(Shape);
    ConfigureExtent(3);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Change preview object; catalogue thumbnails remain unchanged.");
    ImGui::SameLine();
    const char *EnvironmentOptions[]{"Studio softbox", "Warm courtyard", "Cool rim", "Overcast"};
    int Environment = int(Inspection.Environment);
    ImGui::SetNextItemWidth(std::min(160 * Scale, Available.x * .44f));
    if (ConstructMaterialSelection("Preview environment", EnvironmentOptions[Environment], EnvironmentOptions, 4, Environment, Scale))
        Inspection.Environment = MaterialPreviewEnvironment(Environment);
    ConfigureExtent(4);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Environment lighting preset");
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 18 * Scale, Bottom - 117 * Scale));
    const float ExposureWidth = std::min(320 * Scale, std::max(30.f, Available.x - 36 * Scale));
    const auto ExposureOrigin = ImGui::GetCursorScreenPos();
    ImGui::BeginChild("InspectionExposure", ImVec2(ExposureWidth, 64 * Scale), false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    (void)ConstructProperty("Exposure", .25f, 4.f, Inspection.Exposure, "×", true, .01f);
    ImGui::EndChild();
    InspectionExtents[5] = {ExposureOrigin.x, ExposureOrigin.y + 34 * Scale,
        ExposureOrigin.x + ExposureWidth, ExposureOrigin.y + 58 * Scale};
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + std::max(8.f, (Available.x - 240 * Scale) * .5f), Bottom - 196 * Scale));
    const auto ZoomOrigin = ImGui::GetCursorScreenPos();
    Ink->AddRectFilled(ZoomOrigin, ImVec2(ZoomOrigin.x + 140 * Scale, ZoomOrigin.y + 28 * Scale),
        EncodeMaterialInk(0x181818), 14 * Scale);
    Ink->AddRect(ZoomOrigin, ImVec2(ZoomOrigin.x + 140 * Scale, ZoomOrigin.y + 28 * Scale),
        EncodeMaterialInk(0xffffff, 20), 14 * Scale);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
    ImGui::BeginGroup();
    if (ImGui::Button("-##ZoomOut", ImVec2(32 * Scale, 28 * Scale))) Inspection.Zoom = std::max(.5f, Inspection.Zoom - .1f);
    ConfigureExtent(0);
    ImGui::SameLine(0, 0);
    const auto ZoomCaption = std::to_string(int(std::round(Inspection.Zoom * 100))) + "%##ZoomReset";
    if (ImGui::Button(ZoomCaption.c_str(), ImVec2(76 * Scale, 28 * Scale))) Inspection.Zoom = 1;
    ConfigureExtent(1);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset zoom to 100%%");
    ImGui::SameLine(0, 0);
    if (ImGui::Button("+##ZoomIn", ImVec2(32 * Scale, 28 * Scale))) Inspection.Zoom = std::min(2.f, Inspection.Zoom + .1f);
    ConfigureExtent(2);
    ImGui::SameLine();
    if (ImGui::Button("Reset view", ImVec2(90 * Scale, 28 * Scale))) ResetMaterialPreviewView(Inspection);
    ConfigureExtent(6);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Centre / fit and reset orbit. F or Home over preview.");
    ImGui::EndGroup();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::EndDisabled();
    if (Previous != Inspection && ConfigurePreview) ConfigurePreview(Inspection);
    ImGui::PopID();
}

void MaterialLibraryPanel::ConstructFullscreenPreview()
{
    if (!FullscreenPreview) return;
    const auto *Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(Viewport->WorkPos);
    ImGui::SetNextWindowSize(Viewport->WorkSize);
    ImGui::SetNextWindowViewport(Viewport->ID);
    ImGui::SetNextWindowBgAlpha(1);
    constexpr ImGuiWindowFlags Flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!ImGui::Begin("Fullscreen material preview###MaterialPreviewFullscreen", nullptr, Flags))
    {
        ImGui::End();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) FullscreenPreview = false;
    const ImVec2 Origin = ImGui::GetWindowPos();
    const ImVec2 Size = ImGui::GetWindowSize();
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(Origin, ImVec2(Origin.x + Size.x, Origin.y + Size.y), EncodeMaterialInk(0x17191a));
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 28, Origin.y + 20));
    ImGui::TextUnformatted(QuerySelectedMaterial(Library) ? QuerySelectedMaterial(Library)->Name.c_str() : "Material preview");
    ImGui::SameLine();
    ImGui::TextDisabled("PBR · fullscreen fit · Escape to close");
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + Size.x - 150, Origin.y + 14));
    if (ImGui::Button("Close preview  ×", ImVec2(132, 32))) FullscreenPreview = false;
    // 📐 Fullscreen fills the work area between the caption row and the zoom row, so the subject is never clipped there either.
    const float FullscreenTop = Origin.y + 60.f;
    const float FullscreenBottom = Origin.y + Size.y - 92.f;
    const float FullscreenWidth = std::max(1.f, Size.x - 48.f);
    const float FullscreenHeight = std::max(1.f, FullscreenBottom - FullscreenTop);
    const ImVec2 ImagePosition(Origin.x + 24.f, FullscreenTop);
    if (ConfigurePreviewExtent)
        ConfigurePreviewExtent(uint32_t(std::lround(FullscreenWidth)), uint32_t(std::lround(FullscreenHeight)));
    const auto Image = QueryPreview ? QueryPreview() : MaterialPreviewImage{};
    ImGui::SetCursorScreenPos(ImagePosition);
    ImGui::InvisibleButton("Fullscreen inspection canvas", ImVec2(FullscreenWidth, FullscreenHeight),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
    bool Changed = false;
    const auto &Input = ImGui::GetIO();
    const bool Hovered = ImGui::IsItemHovered();
    const auto Δ = EvaluateNavigationDelta ? EvaluateNavigationDelta({Input.MouseDelta.x, Input.MouseDelta.y}) : PlanePoint{Input.MouseDelta.x, Input.MouseDelta.y};
    const float Sensitivity = EvaluateNavigationDelta ? EvaluateNavigationDelta({1,0}).X : 1;
    if (ImGui::IsItemActive())
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || (Input.KeyShift && ImGui::IsMouseDown(ImGuiMouseButton_Left)))
        {
            Inspection.PanX = std::clamp(Inspection.PanX + Input.MouseDelta.x * Sensitivity * 2 / FullscreenWidth, -2.f, 2.f);
            Inspection.PanY = std::clamp(Inspection.PanY + Input.MouseDelta.y * Sensitivity * 2 / FullscreenHeight, -2.f, 2.f);
            Changed = true;
        }
        else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            Inspection.Zoom = std::clamp(Inspection.Zoom * std::exp(-Input.MouseDelta.y * Sensitivity * .01f), .5f, 2.f);
            Changed = true;
        }
        else if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            Inspection.Yaw = std::remainder(Inspection.Yaw + Δ.X * .01f, 6.2831853f);
            Inspection.Pitch = std::clamp(Inspection.Pitch + Δ.Y * .01f, -1.45f, 1.45f);
            Changed = true;
        }
    }
    if (Hovered && Input.MouseWheel != 0)
    {
        Inspection.Zoom = std::clamp(Inspection.Zoom * std::pow(1.1f, Input.MouseWheel), .5f, 2.f);
        Changed = true;
    }
    if (Image.Texture)
        Ink->AddImage(ImTextureRef(Image.Texture), ImagePosition, ImVec2(ImagePosition.x + FullscreenWidth, ImagePosition.y + FullscreenHeight),
            ImVec2(Image.MinimumU, Image.MinimumV), ImVec2(Image.MaximumU, Image.MaximumV));
    else Ink->AddRectFilled(ImagePosition, ImVec2(ImagePosition.x + FullscreenWidth, ImagePosition.y + FullscreenHeight), EncodeMaterialInk(0x202222));
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + (Size.x - 300) * .5f, Origin.y + Size.y - 58));
    if (ImGui::Button("−##FullscreenZoomOut", ImVec2(42, 30))) { Inspection.Zoom = std::max(.5f, Inspection.Zoom - .1f); Changed = true; }
    ImGui::SameLine();
    const std::string ZoomLabel = std::to_string(int(std::round(Inspection.Zoom * 100))) + "%##FullscreenZoomReset";
    if (ImGui::Button(ZoomLabel.c_str(), ImVec2(80, 30))) { Inspection.Zoom = 1; Changed = true; }
    ImGui::SameLine();
    if (ImGui::Button("+##FullscreenZoomIn", ImVec2(42, 30))) { Inspection.Zoom = std::min(2.f, Inspection.Zoom + .1f); Changed = true; }
    ImGui::SameLine();
    if (ImGui::Button("Fit object", ImVec2(92, 30))) { ResetMaterialPreviewView(Inspection); Changed = true; }
    if (Changed && ConfigurePreview) ConfigurePreview(Inspection);
    ImGui::End();
}

void MaterialLibraryPanel::ConstructIcon(IconSymbol Symbol, float X, float Y, float Size, uint32_t Rgb)
{
    if (PresentIcon) (void)PresentIcon(Symbol, {X,Y}, Size, DecodeMaterialSpectrum(Rgb));
}

bool MaterialLibraryPanel::ConstructProperty(const char *Caption, float Minimum, float Maximum, float &Position,
    const char *Unit, bool Logarithmic, float Step, const char *Unavailable)
{
    const float Scale = QueryInterfaceScale();
    const auto Origin = ImGui::GetCursorScreenPos();
    const float Width = std::max(28.f, ImGui::GetContentRegionAvail().x);
    const bool Enabled = (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) == 0;
    ConstructMaterialCaption(Origin.x, Origin.y + 5 * Scale, 10 * Scale, 0xa3a393, Caption);
    ImGui::PushID(Caption);
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + Width - 76 * Scale, Origin.y));
    ImGui::SetNextItemWidth(76 * Scale);
    ImGui::PushFont(nullptr, 9 * 1.302f * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * Scale, 5 * Scale));
    const bool Percentage = std::string_view(Unit) == "%";
    const float Multiplier = Percentage ? 100.f : 1.f;
    const char *Format = Percentage ? "%.0f %%" : std::string_view(Unit) == "µm" ? "%.1f µm" :
        std::string_view(Unit) == "×" ? "%.2f ×" : Step >= 1 ? "%.0f" : "%.3f";
    float Display = Position * Multiplier;
    bool Changed = ImGui::InputFloat("##Position", &Display, 0, 0, Format);
    if (Changed && std::isfinite(Display)) Position = std::clamp(Display / Multiplier, Minimum, Maximum);
    else Changed = false;
    bool Hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    ImGui::PopStyleVar();
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 34 * Scale));
    ImGui::InvisibleButton("Property slider", ImVec2(Width, 24 * Scale));
    const bool Active = Enabled && ImGui::IsItemActive();
    Hovered = Hovered || ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    const auto &IO = ImGui::GetIO();
    ControlPointer Pointer{IO.MousePos.x / Scale, IO.MousePos.y / Scale, Enabled && IO.MouseDown[0],
        Enabled && ImGui::IsItemActivated(), Enabled && ImGui::IsMouseReleased(0), Enabled && (Active || ImGui::IsItemHovered()), IO.MouseWheel};
    PixelSpace Surface;
    (void)Surface.Begin(SurfaceLayer::Window, IO.DisplaySize.x, IO.DisplaySize.y, Scale);
    const PlaneExtent Extent{Origin.x / Scale, Origin.y / Scale + 34, (Origin.x + Width) / Scale, Origin.y / Scale + 58};
    const bool LogarithmicRange = Logarithmic && Minimum > 0 && Maximum > Minimum;
    const float Fraction = LogarithmicRange ? std::clamp(std::log(std::max(Position, Minimum) / Minimum) / std::log(Maximum / Minimum), 0.f, 1.f) : Position;
    float Replacement = Fraction;
    (void)ControlKit::Slider(Surface, Extent, LogarithmicRange ? 0.f : Minimum, LogarithmicRange ? 1.f : Maximum,
        Fraction, Active, Pointer, Replacement, ImGui::GetStyle().Alpha, 24, 22);
    if (std::string_view(Caption) == "Roughness" || std::string_view(Caption) == "Paint roughness")
        RoughnessExtent = {Origin.x, Origin.y + 34 * Scale, Origin.x + Width, Origin.y + 58 * Scale};
    if (Active && Replacement != Fraction)
    {
        Position = LogarithmicRange ? Minimum * std::pow(Maximum / Minimum, Replacement) : Replacement;
        Changed = true;
    }
    if (Changed && Step >= 1) Position = std::clamp(std::round(Position / Step) * Step, Minimum, Maximum);
    if (Unavailable && Hovered) ImGui::SetTooltip("%s", Unavailable);
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 84 * Scale));
    ImGui::Dummy(ImVec2(1, 0));
    return Changed && Enabled;
}

void MaterialLibraryPanel::ConstructActions()
{
    const auto Extent = []()
    {
        const auto Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
        return PlaneExtent{Minimum.x, Minimum.y, Maximum.x, Maximum.y};
    };
    if (ImGui::Button("Apply")) (void)Library.ApplySource();
    ApplyExtent = Extent();
    ImGui::SameLine();
    if (ImGui::Button("Revert") && Library.RevertSource()) SynchronizeSource();
    RevertExtent = Extent();
    ImGui::SameLine();
    if (ImGui::Button("Save")) (void)Library.SaveMaterial();
    SaveExtent = Extent();
    if (ImGui::Button("Duplicate")) Duplicating = true;
    if (Library.QueryModified()) ImGui::TextWrapped("Unsaved edits. Save or Revert before switching materials.");
    if (!Library.QueryDiagnostic().empty()) ImGui::TextWrapped("%s", Library.QueryDiagnostic().c_str());
}

void MaterialLibraryPanel::ConstructInspector()
{
    if (Library.QuerySelection().empty()) { ImGui::TextUnformatted("Select a material"); return; }
    if (SourceVisible) { ConstructSourceEditor(); return; }
    const float Scale = QueryInterfaceScale();
    const auto Origin = ImGui::GetCursorScreenPos();
    const float Width = ImGui::GetContentRegionAvail().x;
    const float Bottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
    const auto At = [&](float X, float Y) { return ImVec2(Origin.x + X * Scale, Origin.y + Y * Scale); };
    const auto *Entry = QuerySelectedMaterial(Library);
    ConstructMaterialCaption(At(20, 19).x, At(20, 19).y, 18 * Scale, 0xd3d3c8, "Material inspector", static_cast<ImFont *>(HeadingTypeface));
    const auto Image = QueryThumbnail ? QueryThumbnail(Library.QuerySelection()) : MaterialPreviewImage{};
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(At(20, 65), At(69, 114), EncodeMaterialInk(0x22221d), 16 * Scale);
    if (Image.Texture) Ink->AddImage(ImTextureRef(Image.Texture), At(22, 67), At(67, 112),
        ImVec2(Image.MinimumU, Image.MinimumV), ImVec2(Image.MaximumU, Image.MaximumV));
    ConstructMaterialCaption(At(80, 71).x, At(80, 71).y, 18 * Scale, 0xdadacc, Entry ? Entry->Name.c_str() : "Material", static_cast<ImFont *>(HeadingTypeface));
    ConstructMaterialCaption(At(90, 100).x, At(90, 100).y, 7 * Scale, 0x777766, Entry ? Entry->Label.c_str() : "LAYERED MATERIAL");
    ImGui::PushFont(nullptr, 11 * 1.302f * Scale);
    const auto Selection = EncodeContentPath(Library.QuerySelection());
    if (ConstructMaterialPill("##Favourite", ImVec2(Origin.x + Width - 40 * Scale, At(0, 76).y), ImVec2(27 * Scale, 27 * Scale)))
    {
        if (!Favourites.erase(Selection)) Favourites.insert(Selection);
    }
    ConstructIcon(IconSymbol::EditorFavourite, Origin.x + Width - 34 * Scale, At(0, 82).y, 15 * Scale, Favourites.contains(Selection) ? 0xc5d2aa : 0x858577);
    ImGui::PopFont();
    ImGui::PushFont(nullptr, 9 * 1.302f * Scale);
    if (ConstructMaterialPill("    Properties", At(21, 137), ImVec2(90 * Scale, 34 * Scale), !LayersVisible)) LayersVisible = false;
    if (ConstructMaterialPill("    Layers", At(117, 137), ImVec2(85 * Scale, 34 * Scale), LayersVisible)) LayersVisible = true;
    ConstructIcon(IconSymbol::EditorSliders, At(31, 147).x, At(31, 147).y, 13 * Scale);
    ConstructIcon(IconSymbol::EditorLayers, At(130, 147).x, At(130, 147).y, 13 * Scale);
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(At(17, 191));
    ImGui::BeginChild("Material properties", ImVec2(Width - 34 * Scale, std::max(40.f, Bottom - At(0, 191).y - 94 * Scale)));
    try
    {
        auto Definition = DecodeSurfaceMaterial(Library.QuerySource());
        bool Changed = false;
        if (LayersVisible)
        {
            for (uint32_t Index = 0; Index < Definition.Scattering.Illumination.Configuration[0]; ++Index)
            {
                ImGui::PushID(int(Index));
                auto &Stratum = Definition.Scattering.Illumination.Strata[Index];
                const float CardHeight = 264 * Scale;
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 16 * Scale);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * Scale, 13 * Scale));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.082f,.082f,.086f,1));
                ImGui::BeginChild("Material layer card", ImVec2(0, CardHeight), ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                const char *LayerName = Stratum.Classification[0] ? "Clearcoat" : "Surface";
                ImGui::Text("Layer %u", Index + 1);
                ImGui::SameLine();
                ImGui::TextDisabled("%s", LayerName);
                ImGui::Separator();
                if (ConstructProperty("Roughness", .045f, 1.f, Stratum.AlbedoRoughness[3]))
                    Changed = Library.ConfigureStratumCoefficient(Index, "Roughness", Stratum.AlbedoRoughness[3]) || Changed;
                if (Stratum.Classification[0] != 0)
                {
                    if (ConstructProperty("Coverage", 0.f, 1.f, Stratum.Optics[2]))
                        Changed = Library.ConfigureStratumCoefficient(Index, "Coverage", Stratum.Optics[2]) || Changed;
                }
                else if (ConstructProperty("Metallic", 0.f, 1.f, Stratum.Optics[0]))
                    Changed = Library.ConfigureStratumCoefficient(Index, "Metallic", Stratum.Optics[0]) || Changed;
                if (Stratum.Classification[0] == 0)
                {
                    if (ConstructProperty("Specular", 0.f, 1.f, Definition.Scattering.DirectionalOptics[Index][3]))
                        Changed = Library.ConfigureStratumCoefficient(Index, "Specular", Definition.Scattering.DirectionalOptics[Index][3]) || Changed;
                    if (ConstructProperty("Normal strength", 0.f, 4.f, Definition.Scattering.StratumNormal[Index][0]))
                        Changed = Library.ConfigureStratumCoefficient(Index, "NormalStrength", Definition.Scattering.StratumNormal[Index][0]) || Changed;
                    if (ConstructProperty("Height strength", 0.f, 4.f, Definition.Scattering.StratumNormal[Index][1]))
                        Changed = Library.ConfigureStratumCoefficient(Index, "HeightStrength", Definition.Scattering.StratumNormal[Index][1]) || Changed;
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar(2);
                ImGui::Dummy(ImVec2(1, 10 * Scale));
                ImGui::PopID();
            }
        }
        else
        {
            const auto Start = ImGui::GetCursorScreenPos();
            const float CardWidth = std::max(110.f, ImGui::GetContentRegionAvail().x);
            ConstructMaterialCaption(Start.x + 3 * Scale, Start.y + 8 * Scale, 7 * Scale, 0x8b8d79, "MATERIAL-AWARE CONTROLS");
            ConstructMaterialCaption(Start.x + 3 * Scale, Start.y + 37 * Scale, 17 * Scale, 0xbabbab,
                Entry && !Entry->RecipeTitle.empty() ? Entry->RecipeTitle.c_str() : "Layered material", static_cast<ImFont *>(HeadingTypeface));
            const char *Description = Entry && !Entry->RecipeCaption.empty() ? Entry->RecipeCaption.c_str() : "Physical surface parameters and independent clearcoat.";
            const float DescriptionSize = 9 * 1.302f * Scale;
            const float DescriptionHeight = ImGui::GetFont()->CalcTextSizeA(DescriptionSize, FLT_MAX, CardWidth - 6 * Scale, Description).y;
            ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), DescriptionSize, ImVec2(Start.x + 3 * Scale, Start.y + 67 * Scale),
                EncodeMaterialInk(0x868676), Description, nullptr, CardWidth - 6 * Scale);
            float CardY = Start.y + std::max(104.f * Scale, 82.f * Scale + DescriptionHeight);
            std::vector<std::string> Groups{"finish"};
            if (Entry)
                for (const auto &Control : Entry->RecipeControls)
                    if (std::find(Groups.begin(), Groups.end(), Control.Group) == Groups.end()) Groups.push_back(Control.Group);
            for (const auto &Group : Groups)
            {
                std::vector<const MaterialRecipeControl *> Controls;
                if (Entry)
                    for (const auto &Control : Entry->RecipeControls) if (Control.Group == Group) Controls.push_back(&Control);
                const bool Finish = Group == "finish", Flakes = Group == "flakes" && Definition.Scattering.Paint.Configuration[3];
                const uint32_t PaletteColumns = uint32_t(std::max(1.f, std::floor((CardWidth / Scale - 28) / 29)));
                const uint32_t PaletteRows = Flakes ? (Definition.Scattering.Paint.Configuration[3] + PaletteColumns - 1) / PaletteColumns : 1;
                const float ControlsTop = Finish ? 140 : Flakes ?
                    (Definition.Scattering.Paint.Configuration[2] == 1 ? 230.f : 155.f + 29 * (PaletteRows - 1)) : 62;
                const float Rows = float((Controls.empty() && Finish ? 3 : Controls.size()) + (Finish ? 3 : 0));
                const bool Collapsed = FoldedInspectorCards.contains(Group);
                const float CardHeight = (Collapsed ? 52.f : ControlsTop + Rows * 84 + 48) * Scale;
                PixelSpace Surface;
                (void)Surface.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, Scale);
                const auto SavedPalette = ControlKit::Palette();
                auto CardPalette = SavedPalette;
                CardPalette.Inset = DecodeMaterialSpectrum(0x222222);
                ControlKit::ConfigurePalette(CardPalette);
                (void)ControlKit::SectionCard(Surface, {Start.x / Scale, CardY / Scale, (Start.x + CardWidth) / Scale, (CardY + CardHeight) / Scale}, 23);
                ControlKit::ConfigurePalette(SavedPalette);
                ConstructIcon(Finish ? IconSymbol::EditorSliders : Flakes ? IconSymbol::TextureFlakes : IconSymbol::TextureClearcoat,
                    Start.x + 17 * Scale, CardY + 18 * Scale, 16 * Scale);
                ConstructMaterialCaption(Start.x + 42 * Scale, CardY + 20 * Scale, 11 * Scale, 0xcacabd,
                    Finish ? "Color & finish" : Flakes ? "Metallic flakes" : Group == "detail" ? "Surface detail" : Group.c_str());
                ImGui::PushID(Group.c_str());
                ImGui::SetCursorScreenPos(ImVec2(Start.x + 6 * Scale, CardY + 6 * Scale));
                if (ImGui::InvisibleButton("Fold inspector card", ImVec2(CardWidth - 12 * Scale, 39 * Scale)))
                {
                    if (!FoldedInspectorCards.erase(Group)) FoldedInspectorCards.insert(Group);
                }
                ConstructIcon(Collapsed ? IconSymbol::EditorChevronRight : IconSymbol::EditorChevronDown,
                    Start.x + CardWidth - 31 * Scale, CardY + 20 * Scale, 12 * Scale, 0x858577);
                if (Collapsed)
                {
                    ImGui::PopID();
                    CardY += CardHeight + 15 * Scale;
                    ImGui::SetCursorScreenPos(ImVec2(Start.x, CardY));
                    ImGui::Dummy(ImVec2(CardWidth, 1));
                    continue;
                }
                if (Finish)
                {
                    ConstructMaterialCaption(Start.x + 17 * Scale, CardY + 55 * Scale, 10 * Scale, 0xa3a393,
                        Entry && Entry->Category == "Paint" ? "Paint color" : "Surface color");
                    ImGui::SetCursorScreenPos(ImVec2(Start.x + 17 * Scale, CardY + 75 * Scale));
                    ImGui::SetNextItemWidth(CardWidth - 34 * Scale);
                    auto &Albedo = Definition.Scattering.Illumination.Strata[0].AlbedoRoughness;
                    float Spectrum[3]{};
                    for (size_t Channel = 0; Channel < 3; ++Channel)
                        Spectrum[Channel] = Albedo[Channel] <= .0031308f ? 12.92f * Albedo[Channel] : 1.055f * std::pow(Albedo[Channel], 1.f / 2.4f) - .055f;
                    if (ImGui::ColorEdit3("##Colour", Spectrum, ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_NoLabel))
                    {
                        std::array<float, 3> Linear{};
                        for (size_t Channel = 0; Channel < 3; ++Channel)
                            Linear[Channel] = Spectrum[Channel] <= .04045f ? Spectrum[Channel] / 12.92f : std::pow((Spectrum[Channel] + .055f) / 1.055f, 2.4f);
                        Changed = Library.ConfigureStratumColour(0, Linear) || Changed;
                    }
                }
                if (Flakes)
                {
                    auto &Paint = Definition.Scattering.Paint;
                    ImGui::SetCursorScreenPos(ImVec2(Start.x + 17 * Scale, CardY + 52 * Scale));
                    bool Enabled = Paint.Configuration[0] != 0;
                    ImGui::SetCursorScreenPos(ImVec2(Start.x + 17 * Scale, CardY + 50 * Scale));
                    if (ConstructMaterialToggle("Procedural paint", Enabled, Scale))
                    {
                        Paint.Configuration[0] = Enabled ? 1 : 0;
                        Changed = Library.ConfigurePaint(Paint) || Changed;
                    }
                    ImGui::SetCursorScreenPos(ImVec2(Start.x + 17 * Scale, CardY + 82 * Scale));
                    ImGui::SetNextItemWidth(CardWidth - 34 * Scale);
                    const char *ModeOptions[]{"Palette", "Ramp", "Single"};
                    int Mode = int(Paint.Configuration[2]);
                    if (ConstructMaterialSelection("Paint colour mode", ModeOptions[Mode], ModeOptions, 3, Mode, Scale))
                    {
                        Paint.Configuration[2] = uint32_t(Mode);
                        if (Mode == 1) std::stable_sort(Paint.Palette.begin(), Paint.Palette.begin() + Paint.Configuration[3],
                            [](const auto &First, const auto &Second) { return First[3] < Second[3]; });
                        Changed = Library.ConfigurePaint(Paint) || Changed;
                    }
                    const uint32_t PaletteCount = Paint.Configuration[3];
                    const uint32_t VisibleColours = Mode == 2 ? std::min(1u, PaletteCount) : PaletteCount;
                    const float PaletteY = CardY + 116 * Scale;
                    if (Mode == 1 && PaletteCount > 1)
                    {
                        const float StripWidth = CardWidth - 34 * Scale;
                        const float StripHeight = 20 * Scale;
                        const ImVec2 StripMinimum(Start.x + 17 * Scale, PaletteY);
                        auto *Ink = ImGui::GetWindowDrawList();
                        for (uint32_t Index = 1; Index < PaletteCount; ++Index)
                        {
                            const float FirstPosition = std::clamp(Paint.Palette[Index - 1][3], 0.f, 1.f);
                            const float SecondPosition = std::clamp(Paint.Palette[Index][3], FirstPosition, 1.f);
                            const auto ToInk = [&](uint32_t Stop)
                            {
                                const auto &Colour = Paint.Palette[Stop];
                                return IM_COL32(int(std::pow(Colour[0], 1.f/2.2f) * 255), int(std::pow(Colour[1], 1.f/2.2f) * 255),
                                    int(std::pow(Colour[2], 1.f/2.2f) * 255), 255);
                            };
                            Ink->AddRectFilledMultiColor(ImVec2(StripMinimum.x + FirstPosition * StripWidth, StripMinimum.y),
                                ImVec2(StripMinimum.x + SecondPosition * StripWidth, StripMinimum.y + StripHeight),
                                ToInk(Index - 1), ToInk(Index), ToInk(Index), ToInk(Index - 1));
                        }
                        ImGui::SetCursorScreenPos(StripMinimum);
                        ImGui::InvisibleButton("Ramp gradient preview", ImVec2(StripWidth, StripHeight));
                        ConstructMaterialCaption(StripMinimum.x, StripMinimum.y + 25 * Scale, 8 * Scale, 0xa3a393,
                            "Gradient stops below · select a swatch to edit its colour");
                    }
                    for (uint32_t Index = 0; Index < VisibleColours; ++Index)
                    {
                        ImGui::PushID(int(Index));
                        const float SwatchWidth = 29 * Scale;
                        const float SwatchX = Start.x + (17 + Index * 34) * Scale;
                        const float SwatchY = PaletteY + (Mode == 1 && PaletteCount > 1 ? 40 * Scale : 0);
                        ImGui::SetCursorScreenPos(ImVec2(SwatchX, SwatchY));
                        float Spectrum[3]{};
                        for (size_t Channel = 0; Channel < 3; ++Channel)
                        {
                            const float Linear = Paint.Palette[Index][Channel];
                            Spectrum[Channel] = Linear <= .0031308f ? 12.92f * Linear : 1.055f * std::pow(Linear, 1.f / 2.4f) - .055f;
                        }
                        if (ImGui::ColorEdit3("##Flake tint", Spectrum, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs |
                            ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoBorder))
                        {
                            for (size_t Channel = 0; Channel < 3; ++Channel)
                                Paint.Palette[Index][Channel] = Spectrum[Channel] <= .04045f ? Spectrum[Channel] / 12.92f :
                                    std::pow((Spectrum[Channel] + .055f) / 1.055f, 2.4f);
                            Changed = Library.ConfigurePaint(Paint) || Changed;
                        }
                        if (Mode == 1 && PaletteCount > 1)
                        {
                            ImGui::SetCursorScreenPos(ImVec2(SwatchX, SwatchY + SwatchWidth + 2 * Scale));
                            float Position = Paint.Palette[Index][3];
                            ImGui::SetNextItemWidth(SwatchWidth);
                            if (ImGui::DragFloat("##Ramp stop", &Position, .005f, 0, 1, "%.2f"))
                            {
                                Paint.Palette[Index][3] = Position;
                                std::stable_sort(Paint.Palette.begin(), Paint.Palette.begin() + PaletteCount,
                                    [](const auto &First, const auto &Second) { return First[3] < Second[3]; });
                                Changed = Library.ConfigurePaint(Paint) || Changed;
                            }
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::SetCursorScreenPos(ImVec2(Start.x + 17 * Scale, CardY + ControlsTop * Scale));
                ImGui::BeginChild("Coefficients", ImVec2(std::max(20.f, CardWidth - 34 * Scale), Rows * 84 * Scale + 1), ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                for (const auto *Control : Controls)
                {
                    float Position = Control->Position;
                    bool Supported = !Control->Targets.empty();
                    for (const auto &Target : Control->Targets) Supported = QueryRecipeCoefficient(Definition, Target.Key).has_value() && Supported;
                    if (Supported)
                    {
                        const auto &Target = Control->Targets.front();
                        const float Coefficient = *QueryRecipeCoefficient(Definition, Target.Key);
                        Position = Control->Direct ? Coefficient : std::abs(Target.Maximum - Target.Minimum) > .000001f ?
                            std::clamp((Coefficient - Target.Minimum) / (Target.Maximum - Target.Minimum), 0.f, 1.f) : Control->Position;
                    }
                    float NativeMinimum = Control->Minimum, NativeMaximum = Control->Maximum;
                    bool RoughnessLimited = false;
                    for (const auto &Target : Control->Targets)
                        if (Target.Key == "roughness" || Target.Key == "coatRoughness")
                        {
                            if (Control->Direct) NativeMinimum = std::max(NativeMinimum, .045f);
                            else if (std::abs(Target.Maximum - Target.Minimum) > .000001f)
                            {
                                const float Boundary = (.045f - Target.Minimum) / (Target.Maximum - Target.Minimum);
                                if (Target.Maximum > Target.Minimum) NativeMinimum = std::max(NativeMinimum, Boundary);
                                else NativeMaximum = std::min(NativeMaximum, Boundary);
                            }
                            RoughnessLimited = NativeMinimum != Control->Minimum || NativeMaximum != Control->Maximum;
                        }
                    Supported = Supported && NativeMinimum <= NativeMaximum;
                    ImGui::BeginDisabled(!Supported);
                    const char *Unavailable = !Supported ? "This recipe parameter is retained, but its procedural shader family is not ported yet." :
                        RoughnessLimited ? "The native preview supports roughness of 0.045 or higher; this control is bounded to valid physical values." : nullptr;
                    if (ConstructProperty(Control->Caption.c_str(), NativeMinimum, NativeMaximum, Position,
                        Control->Direct ? Control->Unit.c_str() : "%", Control->Logarithmic, Control->Step, Unavailable))
                    {
                        bool PaintChanged = false;
                        for (const auto &Target : Control->Targets)
                        {
                            const float Coefficient = Control->Direct ? Position : Target.Minimum + Position * (Target.Maximum - Target.Minimum);
                            if (Definition.Scattering.Paint.Configuration[3] && Target.Key == "flakeLayers")
                            {
                                Definition.Scattering.Paint.Configuration[1] = uint32_t(std::clamp(std::round(Coefficient), 1.f, 4.f));
                                PaintChanged = true;
                            }
                            else if (Definition.Scattering.Paint.Configuration[3] && QueryPaintCoefficient(Definition.Scattering.Paint, Target.Key))
                            {
                                *QueryPaintCoefficient(Definition.Scattering.Paint, Target.Key) = Coefficient;
                                PaintChanged = true;
                            }
                            else
                            {
                                const uint32_t Index = Target.Key.starts_with("coat") ? 1 : 0;
                                const char *Field = Target.Key == "roughness" || Target.Key == "coatRoughness" ? "Roughness" :
                                    Target.Key == "metalness" ? "Metallic" : Target.Key == "ior" || Target.Key == "coatIor" ? "Ior" :
                                    Target.Key == "coat" ? "Coverage" : Target.Key == "specular" ? "Specular" :
                                    Target.Key == "heightStrength" ? "HeightStrength" :
                                    Target.Key == "normalStrength" ? "NormalStrength" : "Anisotropy";
                                Changed = Library.ConfigureStratumCoefficient(Index, Field, std::string_view(Field) == "Roughness" ? std::max(.045f, Coefficient) : Coefficient) || Changed;
                            }
                        }
                        if (PaintChanged) Changed = Library.ConfigurePaint(Definition.Scattering.Paint) || Changed;
                    }
                    ImGui::EndDisabled();
                }
                if (Controls.empty() && Finish)
                {
                    auto &Stratum = Definition.Scattering.Illumination.Strata[0];
                    if (ConstructProperty("Roughness", .045f, 1.f, Stratum.AlbedoRoughness[3])) Changed = Library.ConfigureStratumCoefficient(0, "Roughness", Stratum.AlbedoRoughness[3]) || Changed;
                    if (ConstructProperty("Metallic", 0.f, 1.f, Stratum.Optics[0])) Changed = Library.ConfigureStratumCoefficient(0, "Metallic", Stratum.Optics[0]) || Changed;
                    if (ConstructProperty("Anisotropy", -.9f, .9f, Definition.Scattering.DirectionalOptics[0][0])) Changed = Library.ConfigureStratumCoefficient(0, "Anisotropy", Definition.Scattering.DirectionalOptics[0][0]) || Changed;
                }
                if (Finish)
                {
                    auto &Specular = Definition.Scattering.DirectionalOptics[0][3];
                    auto &NormalStrength = Definition.Scattering.StratumNormal[0][0];
                    auto &HeightStrength = Definition.Scattering.StratumNormal[0][1];
                    if (ConstructProperty("Specular", 0.f, 1.f, Specular))
                        Changed = Library.ConfigureStratumCoefficient(0, "Specular", Specular) || Changed;
                    if (ConstructProperty("Normal strength", 0.f, 4.f, NormalStrength))
                        Changed = Library.ConfigureStratumCoefficient(0, "NormalStrength", NormalStrength) || Changed;
                    if (ConstructProperty("Height strength", 0.f, 4.f, HeightStrength))
                        Changed = Library.ConfigureStratumCoefficient(0, "HeightStrength", HeightStrength) || Changed;
                }
                ImGui::EndChild();
                ConstructMaterialCaption(Start.x + 17 * Scale, CardY + CardHeight - 29 * Scale, 8 * Scale, 0x7b7b6b,
                    Finish ? "Material-specific ranges. Physical values stay in balance." : Flakes ? "Object-space particles. Filtered on the GPU." : "Independent surface and clearcoat detail.");
                ImGui::PopID();
                CardY += CardHeight + 15 * Scale;
                ImGui::SetCursorScreenPos(ImVec2(Start.x, CardY));
                ImGui::Dummy(ImVec2(CardWidth, 1));
            }
        }
        if (Changed) SynchronizeSource();
    }
    catch (const std::exception &Failure) { ImGui::TextWrapped("%s", Failure.what()); }
    ImGui::EndChild();
    if (Library.QueryModified() || !Library.QueryDiagnostic().empty())
    {
        ImGui::SetCursorScreenPos(ImVec2(Origin.x + 17 * Scale, Bottom - 88 * Scale));
        ConstructActions();
        ImGui::SameLine();
        if (ImGui::Button("Source")) SourceVisible = true;
    }
    else
    {
        ImGui::PushFont(nullptr, 10 * 1.302f * Scale);
        if (ConstructMaterialPill("+  Save as preset", ImVec2(Origin.x + 17 * Scale, Bottom - 63 * Scale), ImVec2(Width - 34 * Scale, 34 * Scale))) Duplicating = true;
        ImGui::PopFont();
    }
}

void MaterialLibraryPanel::ConstructSourceEditor()
{
    if (ImGui::Button("Properties")) SourceVisible = false;
    if (Library.QuerySelection().empty())
    {
        ImGui::TextUnformatted("Select a material");
        ImGui::TextWrapped(".material files contain versioned TOML. Texture references use the selected library directory as ContentDirectory.");
        return;
    }
    ImGui::Text("%s%s", EncodeContentPath(Library.QuerySelection()).c_str(), Library.QueryModified() ? " *" : "");
    const auto *Applied = Library.QueryApplied();
    if (Applied)
    {
        ImGui::Text("Applied: %s / %u layers", Applied->Name.c_str(), Applied->Scattering.Illumination.Configuration[0]);

    }
    ImGui::Separator();
    const float Height = std::max(24.f, ImGui::GetContentRegionAvail().y-150*QueryInterfaceScale());
    if (ImGui::InputTextMultiline("##MaterialSource", SourceText.data(), SourceText.size(), ImVec2(-1, Height)))
        Library.ReplaceSource(SourceText.data());
    ConstructActions();
}

void MaterialLibraryPanel::ConstructCreation()
{
    if ((Creating || Duplicating) && !ImGui::IsPopupOpen("Material destination")) ImGui::OpenPopup("Material destination");
    if (ImGui::BeginPopupModal("Material destination", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(Duplicating ? "Duplicate current authored material" : "Create material");
        ImGui::TextWrapped("Destination is relative to ContentDirectory; existing files are never overwritten.");
        ImGui::InputText("Destination .material", DestinationText.data(), DestinationText.size());
        if (!Duplicating) ImGui::InputText("Material name", NameText.data(), NameText.size());
        if (ImGui::Button(Duplicating ? "Create duplicate" : "Create"))
        {
            const auto Destination = DecodeContentPath(DestinationText.data());
            const bool Complete = Duplicating ? Library.DuplicateMaterial(Destination) : Library.CreateMaterial(Destination, NameText.data());
            if (Complete)
            {
                SynchronizeSource();
                Creating = Duplicating = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) { Creating = Duplicating = false; ImGui::CloseCurrentPopup(); }
        if (!Library.QueryDiagnostic().empty()) ImGui::TextWrapped("%s", Library.QueryDiagnostic().c_str());
        ImGui::EndPopup();
    }
}

void MaterialLibraryPanel::ConstructInterface()
{
    if (QueryPhase() != ActivationPhase::Active) return;
    const auto *Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(Viewport->WorkPos);
    const float Scale = QueryInterfaceScale();
    ImGui::SetNextWindowSize(ImVec2(Viewport->WorkSize.x, 79 * Scale));
    ImGui::SetNextWindowViewport(Viewport->ID);
    const auto SavedPalette = ControlKit::Palette();
    auto Palette = SavedPalette;
    Palette.Panel = DecodeMaterialSpectrum(0x121212);
    Palette.Inset = DecodeMaterialSpectrum(0x1c1c1a);
    Palette.Stroke = {1, 1, 1, .032f};
    Palette.Text = DecodeMaterialSpectrum(0xdeded5);
    Palette.TextDim = DecodeMaterialSpectrum(0x85857b);
    Palette.TextFaint = DecodeMaterialSpectrum(0x777766);
    Palette.SliderTrack = DecodeMaterialSpectrum(0x171717);
    Palette.SliderFill = DecodeMaterialSpectrum(0x454545);
    Palette.SliderThumb = DecodeMaterialSpectrum(0xd7d7ce);
    ControlKit::ConfigurePalette(Palette);
    const auto Colour = [](ColorQuad Spectrum) { return ImVec4(Spectrum.Red, Spectrum.Green, Spectrum.Blue, Spectrum.Alpha); };
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Colour(Palette.Panel));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(.11f, .11f, .10f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Colour(Palette.Panel));
    ImGui::PushStyleColor(ImGuiCol_Text, Colour(Palette.Text));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.133f, .133f, .133f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.18f, .18f, .17f, 1));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(.035f, .035f, .035f, 1));
    ImGui::PushStyleColor(ImGuiCol_Header, Colour(Palette.Selected));
    for (const auto Slot : {ImGuiCol_Tab, ImGuiCol_TabHovered, ImGuiCol_TabSelected, ImGuiCol_TabDimmed, ImGuiCol_TabDimmedSelected})
        ImGui::PushStyleColor(Slot, Colour(Palette.Panel));
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelectedOverline, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(.035f, .035f, .035f, 1));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(.035f, .035f, .035f, 1));
    const auto SavedColourButtonPosition = ImGui::GetStyle().ColorButtonPosition;
    ImGui::GetStyle().ColorButtonPosition = ImGuiDir_Left;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_DockingSeparatorSize, 2 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 4 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6 * Scale, 8.49f * Scale));
    // 📝 STB pixel height spans ascent/descent; CSS sizes the em. DM Sans hhea spans 1302/1000 em.
    ImGui::PushFont(static_cast<ImFont *>(ReferenceTypeface), 10 * 1.302f * Scale);
    ImGui::Begin("TextureWorkspace", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking);
    auto *Ink = ImGui::GetWindowDrawList();
    const auto Origin = Viewport->WorkPos;
    const float Width = Viewport->WorkSize.x;
    const auto *Entry = QuerySelectedMaterial(Library);
    Ink->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + 43 * Scale), EncodeMaterialInk(0x0b0b0b));
    Ink->AddRectFilled(ImVec2(Origin.x, Origin.y + 43 * Scale), ImVec2(Origin.x + Width, Origin.y + 79 * Scale), EncodeMaterialInk(0x111111));
    ConstructMaterialCaption(Origin.x + 16 * Scale, Origin.y + 17 * Scale, 10 * Scale, 0xcecec2, Entry ? Entry->Name.c_str() : "Material explorer", static_cast<ImFont *>(HeadingTypeface));
    const float NameWidth = ImGui::GetFont()->CalcTextSizeA(10 * 1.302f * Scale, 300 * Scale, 0, Entry ? Entry->Name.c_str() : "Material explorer").x;
    ConstructMaterialCaption(Origin.x + 22 * Scale + NameWidth, Origin.y + 20 * Scale, 7 * Scale, 0x767669, ".material");
    ImGui::PushFont(nullptr, 10 * 1.302f * Scale);
    if (ConstructMaterialPill("Material", ImVec2(Origin.x + Width - 300 * Scale, Origin.y + 8 * Scale), ImVec2(64 * Scale, 27 * Scale), true)) SourceVisible = false;
    ImGui::BeginDisabled(!ActivatePattern);
    if (ConstructMaterialPill("Pattern", ImVec2(Origin.x + Width - 374 * Scale, Origin.y + 8 * Scale), ImVec2(64 * Scale, 27 * Scale)) && ActivatePattern) ActivatePattern();
    ImGui::EndDisabled();
    if (ConstructMaterialPill("Open Library", ImVec2(Origin.x + Width - 225 * Scale, Origin.y + 8 * Scale), ImVec2(94 * Scale, 27 * Scale))) DirectoryRequested = true;
    if (ConstructMaterialPill("Refresh", ImVec2(Origin.x + Width - 120 * Scale, Origin.y + 8 * Scale), ImVec2(100 * Scale, 27 * Scale)))
    {
        (void)Library.Refresh();
        if (!Library.QueryModified() && !Library.QuerySelection().empty())
        {
            const auto Selection = Library.QuerySelection();
            if (Library.OpenMaterial(Selection)) SynchronizeSource();
        }
        if (InvalidateThumbnails) InvalidateThumbnails();
    }
    ConstructIcon(IconSymbol::EditorMaterial, Origin.x + 17 * Scale, Origin.y + 52 * Scale, 15 * Scale);
    ConstructMaterialCaption(Origin.x + 40 * Scale, Origin.y + 56 * Scale, 9 * Scale, 0x8f8f81, "Automotive essentials     >     Material explorer");
    if (ImGui::BeginPopupContextWindow("Material document operations"))
    {
        if (ImGui::MenuItem("Source")) SourceVisible = true;
        if (ImGui::MenuItem("Properties")) SourceVisible = false;
        if (ImGui::MenuItem("New material", nullptr, false, !Library.QueryModified())) Creating = true;
        ImGui::EndPopup();
    }
    ImGui::PopFont();
    if (DirectoryRequested) { ImGui::OpenPopup("Open material library"); DirectoryRequested = false; }
    if (ImGui::BeginPopupModal("Open material library", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputTextWithHint("##Directory", "Material library folder", DirectoryText.data(), DirectoryText.size());
        ImGui::BeginDisabled(Library.QueryModified());
        if (ImGui::Button("Open") && ConfigureDirectory(DecodeContentPath(DirectoryText.data()))) ImGui::CloseCurrentPopup();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        if (!Library.QueryDiagnostic().empty()) ImGui::TextWrapped("%s", Library.QueryDiagnostic().c_str());
        ImGui::EndPopup();
    }
    ConstructCreation();
    if (ClosingRequested && !ImGui::IsPopupOpen("Unsaved material")) ImGui::OpenPopup("Unsaved material");
    if (ImGui::BeginPopupModal("Unsaved material", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Save your material before closing TextureWorkspace?");
        if (ImGui::Button("Save and close") && Library.SaveMaterial()) CloseAuthorized = true;
        ImGui::SameLine();
        if (ImGui::Button("Discard and close")) CloseAuthorized = true;
        ImGui::SameLine();
        if (ImGui::Button("Cancel closing")) { ClosingRequested = false; ImGui::CloseCurrentPopup(); }
        if (CloseAuthorized) { ClosingRequested = false; ImGui::CloseCurrentPopup(); }
        if (!Library.QueryDiagnostic().empty()) ImGui::TextWrapped("%s", Library.QueryDiagnostic().c_str());
        ImGui::EndPopup();
    }
    if (!Library.QueryDiagnostic().empty()) ImGui::TextWrapped("%s", Library.QueryDiagnostic().c_str());
    ImGui::End();
    if (!Panels.QuerySeated())
    {
        const float Width = std::max(1.f, Viewport->WorkSize.x), Scale = QueryInterfaceScale();
        const float Left = std::min(.30f, 278 * Scale / Width), Right = std::min(.34f, 338 * Scale / Width);
        (void)Panels.ConfigureColumns({{"MaterialLibrary", Left}, {"MaterialViewport", 1 - Left - Right}, {"MaterialInspector", Right}});
    }
    Panels.ConstructInterface(QueryInterfaceScale());
    ConstructFullscreenPreview();
    ImGui::PopFont();
    ImGui::PopStyleVar(6);
    ImGui::PopStyleColor(17);
    ControlKit::ConfigurePalette(SavedPalette);
    ImGui::GetStyle().ColorButtonPosition = SavedColourButtonPosition;
}
} // namespace Frontier
