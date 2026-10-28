/*============================================================================================================================================
                                                         STAMPWORKSPACEPANEL.CPP
============================================================================================================================================*/
// 🧩 Native Stamp (decal) authoring: the same toolbar/canvas/inspector feel as Pattern, retargeted at one bounded
//    24-layer decal stack (Shape/Text/Vector/Image components) instead of a tileable preset library.

#include "StampWorkspacePanel.h"
#include "PanelChrome.h"
#include "StampLibrary.h"
#include "VectorMaskControls.h"
#include "PatternThumbnailRaster.h"
#include "WorkspaceZoom.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include "Engine/Interface/ControlKit.h"
#include "Engine/Interface/DropdownControls.h"
#include "Engine/Interface/GlyphSpace.h"
#include "Engine/Interface/PixelSpace.h"
#include "Engine/ContentInterchange/StampTextOutline.h"
#include "Engine/ContentInterchange/StampVectorImport.h"
#include "Engine/ContentInterchange/VectorAuthoringEditing.h"
#include "Engine/ContentInterchange/VectorGradientStops.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <numbers>
#include <sstream>

namespace Frontier
{
// Shared panel UI primitives (PanelChrome.h) - one implementation for all five workspaces.
using PanelChrome::BareIconButton;
using PanelChrome::Caption;
using PanelChrome::CaptionWidth;
using PanelChrome::DrawGlyph;
using PanelChrome::Ink32;
using PanelChrome::Spectrum;
using PanelChrome::SpectrumQuad;
using PanelChrome::ThemeQuad;
namespace
{
constexpr const char *ShapeCaptions[]{"Rectangle", "Ellipse", "Diamond", "Triangle", "Polygon", "Star"};
constexpr const char *ShapeNames[]{"rect", "ellipse", "diamond", "triangle", "polygon", "star"};
constexpr const char *ShapePaths[]{"M4 4H20V20H4Z", "M12 3A9 9 0 1 0 12 21A9 9 0 1 0 12 3Z", "M12 3L21 12L12 21L3 12Z", "M12 3L22 21H2Z", "M7 3H17L22 12L17 21H7L2 12Z", "M12 2L15 8L22 9L17 14L18 22L12 18L6 22L7 14L2 9L9 8Z"};
constexpr const char *SelectPath = "M4 3L11 21L14 14L21 11Z";
constexpr const char *PlusPath = "M12 5V19M5 12H19";
constexpr const char *VisiblePath = "M2 12Q12 0 22 12Q12 24 2 12ZM12 9A3 3 0 1 0 12 15A3 3 0 1 0 12 9Z";
constexpr const char *LockPath = "M6 10H18V21H6ZM8 10V7A4 4 0 0 1 16 7V10";
constexpr const char *TrashPath = "M3 6H21M8 6V3H16V6M6 6L7 21H17L18 6M10 10V17M14 10V17";
constexpr const char *CopyPath = "M8 8H21V21H8ZM16 8V3H3V16H8";
constexpr const char *TextIconPath = "M4 5H20M12 5V20M8 20H16";
constexpr const char *VectorIconPath = "M4 19L9 5L13 15L20 4";
constexpr const char *ImageIconPath = "M3 5H21V19H3ZM3 15L8 10L12 14L16 10L21 15M9 9A1 1 0 1 0 9 9.01";

constexpr const char *HiddenPath = "M2 12Q12 0 22 12Q12 24 2 12ZM12 9A3 3 0 1 0 12 15A3 3 0 1 0 12 9ZM4 4L20 20";

// 🎨 The two document-space control points of a gradient-filled shape's own axis/radius handles — the
// exact inverse of VectorCoverage.slang's EvaluateVectorPoint Fraction formula's LocalNorm construction,
// reading Source.GradientAxis (linear: the ramp's literal start/end; radial: centre + a point on the
// circle's edge) directly instead of deriving a fixed corner-to-corner line from an angle, so these two
// points can be dragged anywhere relative to the shape (see Gesture 10/11 below) exactly like Illustrator/
// Figma's gradient tool - e.g. placed at a text layer's left/right or top/bottom instead of its box corners.
std::array<PlanePoint, 2> QueryGradientAxisEndpoints(const VectorCoverageInstruction &Source, const std::array<float, 4> &Bounds, float ShapeRotation)
{
    const auto ToPlane = [&](float LocalNormX, float LocalNormY) -> PlanePoint
    {
        const float LocalX = LocalNormX * Bounds[2], LocalY = LocalNormY * Bounds[3];
        return {Bounds[0] + LocalX * std::cos(ShapeRotation) - LocalY * std::sin(ShapeRotation),
                Bounds[1] + LocalX * std::sin(ShapeRotation) + LocalY * std::cos(ShapeRotation)};
    };
    return {ToPlane(Source.GradientAxis[0], Source.GradientAxis[1]), ToPlane(Source.GradientAxis[2], Source.GradientAxis[3])};
}

// 📝 The exact inverse of QueryGradientAxisEndpoints' ToPlane: turns a document-space point (e.g. the live
// mouse position while dragging an axis handle) back into the shape's own LocalNorm frame, ready to write
// straight into GradientAxis.
std::array<float, 2> QueryGradientLocalNorm(PlanePoint Point, const std::array<float, 4> &Bounds, float ShapeRotation)
{
    const float Δx = Point.X - Bounds[0], Δy = Point.Y - Bounds[1];
    const float Cosine = std::cos(ShapeRotation), Sine = std::sin(ShapeRotation);
    return {(Cosine * Δx + Sine * Δy) / Bounds[2], (-Sine * Δx + Cosine * Δy) / Bounds[3]};
}

// 📝 Projects Point onto the Start→End segment and returns how far along it landed, unclamped — reused
// to turn a mouse position into a gradient Fraction (clamp at each call site, since the drag case clamps
// to neighbouring stops instead of a flat 0..1).
float ProjectAlongSegment(PlanePoint Point, PlanePoint Start, PlanePoint End)
{
    const float Length2 = std::max(1e-4f, (End.X - Start.X) * (End.X - Start.X) + (End.Y - Start.Y) * (End.Y - Start.Y));
    return ((Point.X - Start.X) * (End.X - Start.X) + (Point.Y - Start.Y) * (End.Y - Start.Y)) / Length2;
}

// 📝 Rounded, padded modal chrome, matching Pattern's own close/unsaved-changes prompt and the
// new Add Text/Vector/Image popups — ConstructInterface zeroes WindowRounding/WindowPadding for the
// whole docked workspace, so a modal needs its own scope or it inherits flush, square edges.
struct StampModalScope
{
    explicit StampModalScope(float Scale)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20 * Scale, 18 * Scale));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10 * Scale);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.f);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, .10f));
    }
    ~StampModalScope()
    {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }
};

std::string FoldCaption(std::string CaptionText)
{
    for (auto &Letter : CaptionText) if (Letter >= 'A' && Letter <= 'Z') Letter = char(Letter + 32);
    return CaptionText;
}

// 📝 A tiny, self-contained PNG IHDR sniff (independent of MaterialAuthoring's validator) so the Add
// Image popup can size its placeholder rectangle to the pasted image's real aspect ratio before the
// engine's own ValidateVectorAuthoringOrganization pass fully validates/accepts the embedded bytes.
std::optional<std::array<uint32_t, 2>> SniffEmbeddedImageExtent(std::string_view DataUrl)
{
    constexpr std::string_view Prefix = "data:image/png;base64,";
    if (!DataUrl.starts_with(Prefix)) return std::nullopt;
    const std::string_view Encoded = DataUrl.substr(Prefix.size());
    constexpr const char *Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::array<uint8_t, 36> Header{};
    size_t Produced = 0;
    uint32_t Accumulator = 0;
    int Bits = 0;
    for (const char Letter : Encoded.substr(0, 48))
    {
        if (Letter == '=') break;
        const char *Position = std::strchr(Alphabet, Letter);
        if (!Position) return std::nullopt;
        Accumulator = (Accumulator << 6) | uint32_t(Position - Alphabet);
        Bits += 6;
        if (Bits >= 8) { Bits -= 8; if (Produced < Header.size()) Header[Produced++] = uint8_t((Accumulator >> Bits) & 0xff); }
    }
    if (Produced < 24) return std::nullopt;
    constexpr std::array<uint8_t, 8> Signature{137, 80, 78, 71, 13, 10, 26, 10};
    if (!std::equal(Signature.begin(), Signature.end(), Header.begin())) return std::nullopt;
    const auto ReadExtent = [&Header](size_t Offset)
    { return (uint32_t(Header[Offset]) << 24) | (uint32_t(Header[Offset + 1]) << 16) | (uint32_t(Header[Offset + 2]) << 8) | uint32_t(Header[Offset + 3]); };
    return std::array<uint32_t, 2>{ReadExtent(16), ReadExtent(20)};
}
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     DOCUMENT AND PRESENTATION LIFETIME
//------------------------------------------------------------------------------------------------------------------------

StampWorkspacePanel::StampWorkspacePanel()
{
    VectorAuthoringDocument Blank;
    Blank.Stamp = true;
    Blank.Name = "Untitled stamp";
    ConfigureDocument(Blank);
    Controls.RegisterExtent = [&](std::string_view CaptionText, PlaneExtent Extent) { ControlExtents[std::string(CaptionText)] = Extent; };
    (void)Panels.ConfigureDockspaceKey("TextureWorkspace Stamp panels");
    Panels.ConfigureWorkspaceInsets(43, 0);
    Panels.ConfigureTabExtent(130, 26);
    Panels.Register({"stamp-library", "Stamp library", [&]() { ConstructLibrary(); }});
    Panels.Register({"stamp-layers", "Layers", [&]() { ConstructLayers(); }});
    Panels.Register({"stamp-canvas", "2D viewport", [&]() { ConstructCanvas(); }});
    Panels.Register({"stamp-inspector", "Inspector", [&]() { ConstructInspector(); }});
    // 📝 Same left-column arrangement as Pattern: the library stacks above Layers (32% of the column),
    // so the saved-stamp catalogue and the open decal's stack stay visible together.
    (void)Panels.ConfigureStackedPanels("stamp-library", "stamp-layers", 32.f);
    Panels.ConfigureColumns({{"stamp-layers", 278.f / 1436}, {"stamp-canvas", 820.f / 1436},
                             {"stamp-inspector", 338.f / 1436}});
}

void StampWorkspacePanel::ConfigureInterfaceScale(float Replacement)
{
    Scale = std::isfinite(Replacement) ? std::clamp(Replacement, .75f, 1.5f) : 1;
    Controls.Scale = Scale;
}

void StampWorkspacePanel::ConfigureTypeface(void *Regular, void *Light)
{
    RegularTypeface = Regular;
    LightTypeface = Light;
}

void StampWorkspacePanel::ConfigureDocument(VectorAuthoringDocument Document)
{
    if (!Document.Stamp) throw std::runtime_error("Open a native Stamp document, not a Pattern document.");
    History.Reset(std::move(Document));
    SavedDocument = History.QueryDocument();
    Selection.clear();
    UndoSelections.clear(); RedoSelections.clear();
    GestureDraft.reset(); GestureSource.reset(); Gesture = 0; GesturePrimitiveToken = 0; ++GestureRevision;
    MaskSelection = 0;
    LibraryFile.clear(); // a freshly configured document is unbound until opened from / saved to the library
    Diagnostic.clear();
}

void StampWorkspacePanel::RefreshLayerAtlas()
{
    // 📝 Mirrors Pattern's own per-document layer atlas exactly (same rasterizer, same bake-on-
    // revision-change contract); the library-wide gallery atlas lives in RefreshLibraryAtlas below.
    const uint64_t CurrentRevision = QueryRevision();
    if (CurrentRevision == LayerAtlasBakedRevision) return;
    LayerAtlasBakedRevision = CurrentRevision;
    const auto &Document = QueryDocument();
    if (Document.Layers.empty()) { LayerAtlasPixels.clear(); return; }
    // 📝 The dark gallery-card well (near-black) that the library-browsing grid uses is wrong here:
    // a component's authored ink is routinely dark too (the Text tool's own default fill is
    // {.08,.08,.08,1}, ~rgb(20,20,20)), which was rendering at effectively zero contrast against a
    // ~rgb(20,23,24) well — a seemingly "blank"/missing row preview that was actually rasterizing
    // correctly, just invisibly. Match the real 2D viewport's own board colour (Spectrum(0xede7d7),
    // see ConstructCanvas) instead, so a row's thumbnail always matches what the canvas itself shows.
    constexpr uint32_t CardWell = uint32_t(255) << 24 | uint32_t(215) << 16 | uint32_t(231) << 8 | uint32_t(237);
    const size_t Slotted = std::min(Document.Layers.size(), LayerAtlasCapacity);
    const std::vector<uint8_t> Blank = RasterizePatternThumbnailSvg(VectorAuthoringDocument{}, LayerAtlasTile, CardWell);
    std::vector<std::vector<uint8_t>> Tiles;
    Tiles.reserve(Slotted);
    std::map<uint32_t, std::pair<std::string, std::vector<uint8_t>>> FreshCache;
    for (size_t Slot = 0; Slot < Slotted; ++Slot)
    {
        const uint32_t Token = Document.Layers[Slot].Token;
        try
        {
            std::string Markup = EncodePatternLayerThumbnailSvg(Document, Token, CardWell);
            const auto Cached = LayerThumbnailCache.find(Token);
            if (Cached != LayerThumbnailCache.end() && Cached->second.first == Markup)
                Tiles.push_back(Cached->second.second);
            else
                Tiles.push_back(RasterizePatternLayerThumbnail(Document, Token, LayerAtlasTile, CardWell));
            FreshCache.emplace(Token, std::make_pair(std::move(Markup), Tiles.back()));
        }
        catch (const std::exception &) { Tiles.push_back(Blank); }
    }
    LayerThumbnailCache = std::move(FreshCache);
    // 📝 Mirrors Pattern's own guard: a blit failure (bounded-grid rejection or allocation
    // exhaustion) must never propagate uncaught into the render loop — reordering/adding/removing
    // layers bumps the document revision every time, re-entering this call until it bakes cleanly.
    try { LayerAtlasPixels = BlitPatternThumbnailAtlas(Tiles, LayerAtlasTile, LayerAtlasColumns, CardWell); ++LayerAtlasRevision; }
    catch (const std::exception &Failure) { Frontier::DiagnosticJournal::Append("StampLayerAtlasBlitError", Failure.what()); }
}

uint64_t StampWorkspacePanel::QueryLayerAtlasRevision()
{
    RefreshLayerAtlas();
    return LayerAtlasRevision;
}

const std::vector<uint8_t> &StampWorkspacePanel::QueryLayerAtlasPixels()
{
    RefreshLayerAtlas();
    return LayerAtlasPixels;
}

void StampWorkspacePanel::EnsureStampLibrary(bool ForceReload)
{
    if (LibraryScanned && !ForceReload) return;
    LibraryScanned = true;
    Library.Reload(StampLibraryDirectory);
    ++LibraryScanGeneration;
    // 📝 Reopenability (a .stamp.toml vector sidecar beside the PNG) is probed once per scan, never
    // per frame per card — cards just read the cached flag for their slot.
    LibraryEntryReopenable.clear();
    for (const auto &Entry : Library.Entries())
    {
        bool Reopenable = false;
        if (!Entry.Builtin && !Entry.File.empty())
        {
            std::error_code Failure;
            Reopenable = std::filesystem::exists(
                Entry.File.parent_path() / (Entry.File.stem().string() + ".stamp.toml"), Failure);
        }
        LibraryEntryReopenable.push_back(Reopenable ? 1 : 0);
    }
}

void StampWorkspacePanel::RefreshLibraryAtlas()
{
    // 📝 One cover-fit tile per library entry (slot = entry index, so searches/filters only re-point
    // card UVs), composited over the stamp picker's two-tone checker (0x1e1e1e/0x2a2a2a): white
    // preset art and dark authored ink both stay visible, and a card here matches the same stamp's
    // card in Texture Paint's picker. Rebakes wholesale per scan generation — tiles are bilinear
    // downsamples of rasters the library already holds, so no Disk cache (unlike Pattern's atlas).
    EnsureStampLibrary();
    if (LibraryAtlasBakedGeneration == LibraryScanGeneration && !LibraryAtlasPixels.empty()) return;
    LibraryAtlasBakedGeneration = LibraryScanGeneration;
    constexpr uint32_t CardWell = uint32_t(255) << 24 | uint32_t(30) << 16 | uint32_t(30) << 8 | uint32_t(30);
    const auto &Entries = Library.Entries();
    const size_t Slotted = std::min(Entries.size(), LibraryAtlasCapacity);
    std::vector<std::vector<uint8_t>> Tiles;
    Tiles.reserve(Slotted);
    for (size_t Slot = 0; Slot < Slotted; ++Slot)
    {
        const auto &Entry = Entries[Slot];
        const float SourceAspect = Entry.Height > 0 ? float(Entry.Width) / float(Entry.Height) : 1.f;
        std::vector<uint8_t> Tile(size_t(LibraryAtlasTile) * LibraryAtlasTile * 4);
        for (uint32_t Y = 0; Y < LibraryAtlasTile; ++Y)
            for (uint32_t X = 0; X < LibraryAtlasTile; ++X)
            {
                float U = (X + .5f) / LibraryAtlasTile, V = (Y + .5f) / LibraryAtlasTile;
                if (SourceAspect > 1) U = .5f + (U - .5f) / SourceAspect;
                else if (SourceAspect < 1) V = .5f + (V - .5f) * SourceAspect;
                const auto Texel = StampLibrary::Sample(Entry, U, V);
                const float Well = ((X < LibraryAtlasTile / 2) == (Y < LibraryAtlasTile / 2)) ? 42.f : 30.f;
                uint8_t *Target = &Tile[(size_t(Y) * LibraryAtlasTile + X) * 4];
                for (int Channel = 0; Channel < 3; ++Channel)
                    Target[Channel] = uint8_t(std::clamp(Texel[size_t(Channel)] * Texel[3] +
                        (Well / 255.f) * (1.f - Texel[3]), 0.f, 1.f) * 255.f + .5f);
                Target[3] = 255;
            }
        Tiles.push_back(std::move(Tile));
    }
    // 📝 Same guard as the layer atlas: a blit failure must never reach the render loop uncaught;
    // the next scan generation simply retries the bake.
    try { LibraryAtlasPixels = BlitPatternThumbnailAtlas(Tiles, LibraryAtlasTile, LibraryAtlasColumns, CardWell); ++LibraryAtlasRevision; }
    catch (const std::exception &Failure) { Frontier::DiagnosticJournal::Append("StampLibraryAtlasBlitError", Failure.what()); }
}

uint64_t StampWorkspacePanel::QueryLibraryAtlasRevision()
{
    RefreshLibraryAtlas();
    return LibraryAtlasRevision;
}

const std::vector<uint8_t> &StampWorkspacePanel::QueryLibraryAtlasPixels()
{
    RefreshLibraryAtlas();
    return LibraryAtlasPixels;
}

std::vector<size_t> StampWorkspacePanel::QueryLibraryMatches() const
{
    std::string Query = LibrarySearchText.data();
    const auto Trimmed = Query.find_last_not_of(" \t\n\r");
    Query = Trimmed == std::string::npos ? "" : Query.substr(0, Trimmed + 1);
    Query = PanelChrome::Lowered(Query);
    std::vector<size_t> Matches;
    const auto &Entries = Library.Entries();
    for (size_t Index = 0; Index < Entries.size(); ++Index)
    {
        const auto &Entry = Entries[Index];
        if (LibraryFilter == 1 && !Entry.Builtin) continue;
        if (LibraryFilter == 2 && Entry.Builtin) continue;
        if (!Query.empty() && PanelChrome::Lowered(Entry.Name).find(Query) == std::string::npos) continue;
        Matches.push_back(Index);
    }
    return Matches;
}

void StampWorkspacePanel::OpenLibraryStamp(const StampLibraryEntry &Entry, bool Reopenable)
{
    LibrarySelected = Entry.Id;
    if (!Reopenable)
    {
        // 📝 Preset art and raster-only stamps have no vector source to author - say so plainly
        // instead of opening the grey placeholder box an imported-image component would show.
        Diagnostic = Entry.Builtin
            ? "'" + Entry.Name + "' is preset art - place it from Texture Paint's stamp tool. Stamps you save from this workspace reopen here as editable vector designs."
            : "'" + Entry.Name + "' is a raster-only stamp (no saved vector source) - place it from Texture Paint's stamp tool.";
        return;
    }
    try
    {
        const auto Sidecar = Entry.File.parent_path() / (Entry.File.stem().string() + ".stamp.toml");
        std::ifstream Input(Sidecar, std::ios::binary);
        if (!Input) throw std::runtime_error("The stamp's vector source could not be read.");
        std::ostringstream Buffer;
        Buffer << Input.rdbuf();
        ConfigureDocument(DecodeVectorAuthoring(Buffer.str()));
        LibraryFile = Entry.File; // Save now updates this very library stamp in place
        Diagnostic = "Opened '" + Entry.Name + "' from the stamp library.";
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

const VectorAuthoringDocument &StampWorkspacePanel::QueryDocument() const
{
    return GestureDraft ? *GestureDraft : History.QueryDocument();
}

PlaneExtent StampWorkspacePanel::QueryControlExtent(const std::string &CaptionText) const
{
    const auto Position = ControlExtents.find(CaptionText);
    return Position == ControlExtents.end() ? PlaneExtent{} : Position->second;
}

PlaneExtent StampWorkspacePanel::QueryPanelExtent(const std::string &CaptionText) const
{
    const auto Position = PanelExtents.find(CaptionText);
    return Position == PanelExtents.end() ? PlaneExtent{} : Position->second;
}

void StampWorkspacePanel::CapturePanel(const char *CaptionText)
{
    const auto Position = ImGui::GetWindowPos(), Extent = ImGui::GetWindowSize();
    PanelExtents[CaptionText] = {Position.x, Position.y, Position.x + Extent.x, Position.y + Extent.y};
}

void StampWorkspacePanel::Finalize(VectorAuthoringDocument Document, std::vector<uint32_t> Tokens)
{
    const auto PreviousSelection = GestureSource ? GestureSelection : Selection;
    if (History.Finalize(std::move(Document)))
    {
        UndoSelections.push_back(PreviousSelection);
        if (UndoSelections.size() > 80) UndoSelections.erase(UndoSelections.begin());
        RedoSelections.clear();
    }
    ++GestureRevision;
    Selection = std::move(Tokens);
    std::erase_if(Selection, [&](uint32_t Token) { return std::none_of(History.QueryDocument().Layers.begin(), History.QueryDocument().Layers.end(), [Token](const auto &Layer) { return Layer.Token == Token; }); });
    if (std::find(Selection.begin(), Selection.end(), MaskSelection) == Selection.end()) MaskSelection = 0;
    Diagnostic.clear();
}

void StampWorkspacePanel::EvaluateEdit(const std::function<void(VectorAuthoringDocument &)> &Edit)
{
    try
    {
        auto Document = History.QueryDocument();
        Edit(Document);
        Finalize(std::move(Document), Selection);
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

void StampWorkspacePanel::TransferSelection(bool Remove)
{
    try
    {
        if (Gesture != 0 || MaskSelection) throw std::runtime_error("Finish the current gesture and select components before copying.");
        auto Fragment = ExtractVectorAuthoringSelection(History.QueryDocument(), Selection);
        if (Remove)
        {
            auto Document = History.QueryDocument();
            PartitionVectorAuthoringSelection(Document, Selection);
            Finalize(std::move(Document), {});
        }
        SelectionClipboard = std::move(Fragment);
        Diagnostic.clear();
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

void StampWorkspacePanel::InsertSelection(bool InPlace)
{
    try
    {
        if (Gesture != 0 || MaskSelection || !SelectionClipboard) throw std::runtime_error("Copy components before pasting; finish any active gesture first.");
        auto Document = History.QueryDocument();
        auto Tokens = IntegrateVectorAuthoringSelection(Document, *SelectionClipboard,
            InPlace ? std::array<float, 2>{0, 0} : std::array<float, 2>{18, 18});
        Finalize(std::move(Document), std::move(Tokens));
        Tool = VectorAuthoringTool::Select;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

void StampWorkspacePanel::ReplicateSelection()
{
    try
    {
        if (Gesture != 0 || MaskSelection) throw std::runtime_error("Finish the current gesture before duplicating a component.");
        auto Document = History.QueryDocument();
        TransformVectorAuthoringSelection(Document, Selection, {});
        const auto Fragment = ExtractVectorAuthoringSelection(Document, Selection);
        auto Tokens = IntegrateVectorAuthoringSelection(Document, Fragment);
        Finalize(std::move(Document), std::move(Tokens));
        Tool = VectorAuthoringTool::Select;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

void StampWorkspacePanel::SelectEditableVectors()
{
    Selection.clear(); MaskSelection = 0;
    for (const auto &Layer : History.QueryDocument().Layers)
        if (QueryVectorAuthoringVisibility(History.QueryDocument(), Layer) && !QueryVectorAuthoringProtection(History.QueryDocument(), Layer))
            Selection.push_back(Layer.Token);
}

void StampWorkspacePanel::ApplySourcePatch(const VectorAuthoringSourcePatch &Patch)
{
    EvaluateEdit([&](auto &Document) { TransformVectorAuthoringSelection(Document, Selection, Patch); });
}

void StampWorkspacePanel::RestoreHistory(bool Redo)
{
    GestureDraft.reset(); GestureSource.reset(); Gesture = 0; GesturePrimitiveToken = 0; ++GestureRevision;
    auto &Source = Redo ? RedoSelections : UndoSelections;
    auto &Destination = Redo ? UndoSelections : RedoSelections;
    if (History.RestoreHistory(Redo) && !Source.empty())
    {
        ++GestureRevision;
        Destination.push_back(Selection);
        Selection = std::move(Source.back());
        Source.pop_back();
        Diagnostic.clear();
    }
    std::erase_if(Selection, [&](uint32_t Token) { return std::none_of(QueryDocument().Layers.begin(), QueryDocument().Layers.end(), [Token](const auto &Layer) { return Layer.Token == Token; }); });
    MaskSelection = 0;
}

void StampWorkspacePanel::Select(uint32_t Token, bool Extend)
{
    if (!Extend) Selection.clear();
    const auto Position = std::find(Selection.begin(), Selection.end(), Token);
    if (Extend && Position != Selection.end()) Selection.erase(Position);
    else Selection.push_back(Token);
    MaskSelection = 0;
}

std::array<float, 4> StampWorkspacePanel::CanvasInkColour() const noexcept
{
    // Fresh vectors must contrast whatever the board shows: transparent -> white, a dark board
    // -> white, a light board -> black. (Board base is 0xede7d7; see ConstructCanvas.)
    if (TransparentCanvas) return {1, 1, 1, 1};
    constexpr uint32_t Board = 0xede7d7;
    const float Luminance = (.2126f * float((Board >> 16) & 255) + .7152f * float((Board >> 8) & 255)
        + .0722f * float(Board & 255)) / 255.f;
    return Luminance > .5f ? std::array<float, 4>{0, 0, 0, 1} : std::array<float, 4>{1, 1, 1, 1};
}

void StampWorkspacePanel::ConstructPrimitive(uint32_t Category, const char *CaptionText)
{
    try
    {
        auto Document = History.QueryDocument();
        VectorCoverageInstruction Source;
        Source.Topology[0] = Category;
        Source.Bounds = {256, 256, 160, 160};
        Source.Colour = {.8627f, .4627f, .3529f, 1};
        Source.ColourEnd = {.35f, .55f, .45f, 1};
        Source.StrokeColour = CanvasInkColour();
        Source.Topology[3] |= VectorCoverageStrokeColourExplicit;
        if (Document.Layers.empty()) Document.Name = "Custom stamp";
        const auto Token = ConstructVectorAuthoringPrimitive(Document, Source, CaptionText == std::string_view(ShapeCaptions[Category]) ? ShapeNames[Category] : CaptionText);
        Finalize(std::move(Document), {Token});
        SearchText[0] = 0; Filter = 0;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

//------------------------------------------------------------------------------------------------------------------------
//                                               PEN, POLYLINE AND POINTS TOOLS
//------------------------------------------------------------------------------------------------------------------------

void StampWorkspacePanel::CancelPen()
{
    PenSource.reset(); PenPreview.reset(); PendingPath = {}; PenSelection.clear(); ++GestureRevision;
    PenToken = 0; PenAnchorFloor = 0; PenReversed = false; IndependentPenContinuation = false; PolylineSession = false;
    Diagnostic.clear();
    if (Gesture == 12) Gesture = 0;
}

void StampWorkspacePanel::ResumePen(uint32_t Token, bool FromStart)
{
    const auto &Source = History.QueryDocument();
    const auto Position = std::find_if(Source.Layers.begin(), Source.Layers.end(), [Token](const auto &Layer) { return Layer.Token == Token; });
    if (Position == Source.Layers.end() || MaskSelection || QueryVectorAuthoringProtection(Source, *Position)
        || !QueryVectorAuthoringVisibility(Source, *Position)) throw std::runtime_error("Select one visible, editable open cubic source to continue.");
    auto Path = DecodeCubicPath(Source, Token);
    if (Path.Closed || Path.Anchors.size() >= 400) throw std::runtime_error("Continue an open path below its 400-anchor budget.");
    if (FromStart) ReverseCubicPath(Path);
    auto &Endpoint = Path.Anchors.back();
    for (size_t Axis = 0; Axis < 2; ++Axis) Endpoint.Outgoing[Axis] = 2 * Endpoint.Position[Axis] - Endpoint.Incoming[Axis];
    CancelPen();
    PenSource = Source;
    PenSelection = Selection;
    PendingPath = std::move(Path);
    PenToken = Token; PenAnchorFloor = PendingPath.Anchors.size(); PenReversed = FromStart;
    Tool = VectorAuthoringTool::Pen;
}

void StampWorkspacePanel::RefreshPenPreview()
{
    PenPreview.reset();
    ++GestureRevision;
    if (!PenSource || PendingPath.Anchors.size() < 2) return;
    if (PenToken && PendingPath.Anchors.size() == PenAnchorFloor && !PendingPath.Closed) return;
    auto Draft = *PenSource;
    if (PenToken)
    {
        auto Path = PendingPath;
        if (PenReversed) ReverseCubicPath(Path);
        ReplaceCubicPath(Draft, PenToken, Path);
        PenPreview = std::move(Draft);
        return;
    }
    VectorCoverageInstruction Style;
    Style.Transform[1] = 1.4f;
    // New pen/polyline paths draw in canvas-contrast ink (the instruction default is white,
    // which vanishes on the light board); recolour any time from the Inspector.
    Style.Colour = CanvasInkColour();
    Style.StrokeColour = Style.Colour;
    Style.Topology[3] |= VectorCoverageStrokeColourExplicit;
    Style.Paint[3] = PendingPath.Closed ? 1.f : 0.f;
    auto Path = PendingPath;
    if (AutomaticPen && !PolylineSession)
    {
        std::vector<size_t> Indices;
        for (size_t Index = 0; Index < Path.Anchors.size(); ++Index) Indices.push_back(Index);
        ConfigureAutomaticCubicTangents(Path, Indices);
    }
    ConstructCubicPath(Draft, Path, Style, "Pen contour");
    PenPreview = std::move(Draft);
}

bool StampWorkspacePanel::FinishPen(bool Closed)
{
    if (!PenSource) return true;
    try
    {
        if (PenToken && PendingPath.Anchors.size() == PenAnchorFloor && !Closed) { CancelPen(); return true; }
        auto Path = PendingPath; Path.Closed = Closed;
        if (AutomaticPen && !PenToken && !PolylineSession)
        {
            std::vector<size_t> Indices;
            for (size_t Index = 0; Index < Path.Anchors.size(); ++Index) Indices.push_back(Index);
            ConfigureAutomaticCubicTangents(Path, Indices);
        }
        if (PenReversed) ReverseCubicPath(Path);
        auto Draft = *PenSource;
        VectorCoverageInstruction Style;
        Style.Transform[1] = 1.4f;
        Style.Colour = CanvasInkColour();
        Style.StrokeColour = Style.Colour;
        Style.Topology[3] |= VectorCoverageStrokeColourExplicit;
        // 📝 Seed the closed/filled state's own independent outline width (Paint[0]) to match the
        // line's starting thickness, purely for visual continuity the instant a brand-new path first
        // closes into a shape - from here on the two widths are edited and stored completely separately.
        if (Closed) Style.Paint[0] = Style.Transform[1];
        uint32_t Token = PenToken;
        if (Token) ReplaceCubicPath(Draft, Token, Path);
        else Token = ConstructCubicPath(Draft, Path, Style, "Pen contour");
        Selection = PenSelection;
        CancelPen();
        Finalize(std::move(Draft), {Token});
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); return false; }
}

bool StampWorkspacePanel::ConstructPenInteraction(PlaneExtent Canvas, PlaneExtent Board, bool Hovered)
{
    if (Tool != VectorAuthoringTool::Pen && Tool != VectorAuthoringTool::Polyline && !PenSource) return false;
    const auto &Input = ImGui::GetIO();
    if (PenSource && Tool != VectorAuthoringTool::Pen && Tool != VectorAuthoringTool::Polyline)
    {
        if (!FinishPen(false)) Tool = PolylineSession ? VectorAuthoringTool::Polyline : VectorAuthoringTool::Pen;
        return true;
    }
    if (PenSource && (Input.AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape)))
    {
        CancelPen(); return true;
    }
    if (PenSource && !Input.WantTextInput)
    {
        if (Input.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) { Save(); return true; }
        if (Input.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) { CancelPen(); return true; }
        if (Gesture == 0 && !Input.KeyCtrl)
        {
            std::optional<VectorAuthoringTool> Requested;
            if (ImGui::IsKeyPressed(ImGuiKey_V)) Requested = VectorAuthoringTool::Select;
            if (ImGui::IsKeyPressed(ImGuiKey_A)) Requested = VectorAuthoringTool::Points;
            if (ImGui::IsKeyPressed(ImGuiKey_R)) Requested = VectorAuthoringTool::Rectangle;
            if (ImGui::IsKeyPressed(ImGuiKey_O)) Requested = VectorAuthoringTool::Ellipse;
            if (!PolylineSession && ImGui::IsKeyPressed(ImGuiKey_L)) Requested = VectorAuthoringTool::Polyline;
            if (PolylineSession && ImGui::IsKeyPressed(ImGuiKey_P)) Requested = VectorAuthoringTool::Pen;
            if (Requested) { if (FinishPen(false)) Tool = *Requested; return true; }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter)) { FinishPen(false); return true; }
        if (Gesture == 0 && ImGui::IsKeyPressed(ImGuiKey_Backspace))
        {
            if (PenToken && PendingPath.Anchors.size() <= PenAnchorFloor) { CancelPen(); return true; }
            PendingPath.Anchors.pop_back();
            if (PendingPath.Anchors.empty()) CancelPen();
            else RefreshPenPreview();
            return true;
        }
    }
    if (MaskSelection) return false;
    const auto DocumentPoint = [&]()
    {
        std::array<float, 2> Coordinate{(Input.MousePos.x - Board.MinimumX) / Board.Width() * 512,
            (Input.MousePos.y - Board.MinimumY) / Board.Height() * 512};
        if (SnappingEnabled) for (auto &Position : Coordinate) Position = std::round(Position / 16) * 16;
        return Coordinate;
    };
    const auto Project = [&](std::array<float, 2> Coordinate)
    {
        return ImVec2(Board.MinimumX + Coordinate[0] / 512 * Board.Width(), Board.MinimumY + Coordinate[1] / 512 * Board.Height());
    };
    uint32_t ResumeToken = 0;
    bool ResumeStart = false;
    if (!PenSource && Selection.size() == 1)
    {
        const auto &Source = History.QueryDocument();
        const auto Position = std::find_if(Source.Layers.begin(), Source.Layers.end(), [&](const auto &Layer) { return Layer.Token == Selection[0]; });
        if (Position != Source.Layers.end() && !QueryVectorAuthoringProtection(Source, *Position)
            && QueryVectorAuthoringVisibility(Source, *Position) && Source.Instructions[Position->Root].Topology[0] == 6)
        {
            try
            {
                const auto Path = DecodeCubicPath(Source, Position->Token);
                if (!Path.Closed && Path.Anchors.size() < 400)
                {
                    auto *Ink = ImGui::GetWindowDrawList();
                    Ink->PushClipRect(ImVec2(Canvas.MinimumX, Canvas.MinimumY), ImVec2(Canvas.MaximumX, Canvas.MaximumY), true);
                    for (bool FromStart : {true, false})
                    {
                        const auto Endpoint = Project(FromStart ? Path.Anchors.front().Position : Path.Anchors.back().Position);
                        const float Radius = 7 * Scale;
                        ControlExtents[FromStart ? "Resume start endpoint" : "Resume end endpoint"] = {Endpoint.x - Radius, Endpoint.y - Radius, Endpoint.x + Radius, Endpoint.y + Radius};
                        Ink->AddCircle(Endpoint, 5 * Scale, IM_COL32(204, 230, 181, 255));
                        if (std::hypot(Input.MousePos.x - Endpoint.x, Input.MousePos.y - Endpoint.y) <= Radius)
                        {
                            ResumeToken = Position->Token; ResumeStart = FromStart;
                        }
                    }
                    Ink->PopClipRect();
                }
            }
            catch (const std::exception &) {}
        }
    }
    uint32_t JoinToken = 0;
    bool JoinEnd = false;
    if (PenSource && PenToken && Gesture == 0)
    {
        auto *Ink = ImGui::GetWindowDrawList();
        Ink->PushClipRect(ImVec2(Canvas.MinimumX, Canvas.MinimumY), ImVec2(Canvas.MaximumX, Canvas.MaximumY), true);
        float Nearest = 7 * Scale;
        for (auto Layer = PenSource->Layers.rbegin(); Layer != PenSource->Layers.rend(); ++Layer)
        {
            if (Layer->Token == PenToken || !QueryVectorAuthoringVisibility(*PenSource, *Layer)
                || PenSource->Instructions[Layer->Root].Topology[0] != 6) continue;
            CubicPath Path;
            try { Path = DecodeCubicPath(*PenSource, Layer->Token); }
            catch (const std::exception &) { continue; } // Compound contours remain visible but are not endpoint targets.
            if (Path.Closed) continue;
            for (bool End : {false, true})
            {
                const auto Endpoint = Project(End ? Path.Anchors.back().Position : Path.Anchors.front().Position);
                const float Radius = 7 * Scale;
                ControlExtents[std::string(End ? "Join end endpoint:" : "Join start endpoint:") + std::to_string(Layer->Token)]
                    = {Endpoint.x - Radius, Endpoint.y - Radius, Endpoint.x + Radius, Endpoint.y + Radius};
                Ink->AddCircle(Endpoint, 5 * Scale, IM_COL32(204, 230, 181, 255));
                const float Distance = std::hypot(Input.MousePos.x - Endpoint.x, Input.MousePos.y - Endpoint.y);
                if (Distance < Nearest)
                {
                    Nearest = Distance; JoinToken = Layer->Token; JoinEnd = End;
                }
            }
        }
        Ink->PopClipRect();
    }
    bool Consumed = Gesture == 12;
    try
    {
        if (Gesture == 0 && Hovered && ImGui::IsMouseClicked(0) && !ImGui::IsKeyDown(ImGuiKey_Space))
        {
            Consumed = true;
            if (!PenSource && ResumeToken && !Input.KeyCtrl)
            {
                ResumePen(ResumeToken, ResumeStart);
                IndependentPenContinuation = false;
                PointerOrigin = {(Input.MousePos.x - Board.MinimumX) / Board.Width() * 512,
                    (Input.MousePos.y - Board.MinimumY) / Board.Height() * 512};
                Gesture = 12;
                return true;
            }
            if (PenSource && !PendingPath.Anchors.empty())
            {
                const auto Last = Project(PendingPath.Anchors.back().Position);
                if (!PolylineSession && Input.KeyAlt && std::hypot(Input.MousePos.x - Last.x, Input.MousePos.y - Last.y) <= 7 * Scale)
                {
                    if (AutomaticPen && !PenToken) throw std::runtime_error("Disable automatic Pen curves before breaking an outgoing tangent.");
                    // 📝 Keep the incoming curve; only the following segment receives this corner or independent drag.
                    PendingPath.Anchors.back().Outgoing = PendingPath.Anchors.back().Position;
                    IndependentPenContinuation = true;
                    PointerOrigin = {(Input.MousePos.x - Board.MinimumX) / Board.Width() * 512,
                        (Input.MousePos.y - Board.MinimumY) / Board.Height() * 512};
                    Gesture = 12;
                    RefreshPenPreview();
                    return true;
                }
                const auto First = Project(PendingPath.Anchors.front().Position);
                if (std::hypot(Input.MousePos.x - First.x, Input.MousePos.y - First.y) <= 7 * Scale)
                {
                    if (PendingPath.Anchors.size() < 3) throw std::runtime_error("A closed path needs at least three anchors.");
                    FinishPen(true); return true;
                }
            }
            if (JoinToken && !Input.KeyCtrl)
            {
                // 📝 Build and validate the entire continuation/join before consuming either retained source.
                auto Draft = *PenSource;
                auto Path = PendingPath;
                if (PenReversed) ReverseCubicPath(Path);
                ReplaceCubicPath(Draft, PenToken, Path);
                const auto Token = IntegrateCubicPaths(Draft, PenToken, JoinToken, PenReversed, JoinEnd);
                Selection = PenSelection;
                CancelPen();
                Finalize(std::move(Draft), {Token});
                return true;
            }
            if (PendingPath.Anchors.size() >= 400) throw std::runtime_error("Pen paths support at most 400 anchors.");
            if (!PenSource)
            {
                PenSource = History.QueryDocument(); PenSelection = Selection;
                PendingPath = {};
                PolylineSession = Tool == VectorAuthoringTool::Polyline;
            }
            auto Position = DocumentPoint();
            if (Input.KeyShift && !PendingPath.Anchors.empty()) Position = ConstrainCubicDirection(PendingPath.Anchors.back().Position, Position);
            IndependentPenContinuation = false;
            PendingPath.Anchors.push_back({Position, Position, Position});
            PointerOrigin = {(Input.MousePos.x - Board.MinimumX) / Board.Width() * 512,
                (Input.MousePos.y - Board.MinimumY) / Board.Height() * 512};
            Gesture = 12;
            RefreshPenPreview();
        }
        if (Gesture == 12 && PenSource && ImGui::IsKeyDown(ImGuiKey_Space) && ImGui::IsMousePosValid())
        {
            // 📝 Move only the newly placed anchor, never the retained endpoint being resumed.
            if (!PenToken || PendingPath.Anchors.size() > PenAnchorFloor)
            {
                auto &Anchor = PendingPath.Anchors.back();
                const std::array<float, 2> Displacement{Input.MouseDelta.x / Board.Width() * 512,
                    Input.MouseDelta.y / Board.Height() * 512};
                for (auto *Point : {&Anchor.Position, &Anchor.Incoming, &Anchor.Outgoing})
                    for (size_t Axis = 0; Axis < 2; ++Axis) (*Point)[Axis] += Displacement[Axis];
                PointerOrigin.X += Displacement[0]; PointerOrigin.Y += Displacement[1];
                RefreshPenPreview();
            }
        }
        else
        if (Gesture == 12 && PenSource && (ImGui::IsMouseDragging(0, 2) || ImGui::IsMouseReleased(0)) && ImGui::IsMousePosValid()
            && std::hypot((Input.MousePos.x - Board.MinimumX) / Board.Width() * 512 - PointerOrigin.X,
                (Input.MousePos.y - Board.MinimumY) / Board.Height() * 512 - PointerOrigin.Y) * Board.Width() / 512 >= 2 * Scale)
        {
            if (!PolylineSession)
            {
                auto &Anchor = PendingPath.Anchors.back();
                auto Position = DocumentPoint();
                if (Input.KeyShift) Position = ConstrainCubicDirection(Anchor.Position, Position);
                Anchor.Outgoing = Position;
                // 📝 The resumed endpoint's incoming control belongs to an existing segment; never mirror over it.
                if (!IndependentPenContinuation && (!PenToken || PendingPath.Anchors.size() > PenAnchorFloor))
                    for (size_t Axis = 0; Axis < 2; ++Axis)
                        Anchor.Incoming[Axis] = Input.KeyAlt ? Anchor.Position[Axis] : 2 * Anchor.Position[Axis] - Position[Axis];
                RefreshPenPreview();
            }
        }
        if (PenSource)
        {
            auto *Ink = ImGui::GetWindowDrawList();
            Ink->PushClipRect(ImVec2(Canvas.MinimumX, Canvas.MinimumY), ImVec2(Canvas.MaximumX, Canvas.MaximumY), true);
            auto Guides = PendingPath;
            if (AutomaticPen && !PenToken && !PolylineSession && Guides.Anchors.size() >= 2)
            {
                std::vector<size_t> Indices;
                for (size_t Index = 0; Index < Guides.Anchors.size(); ++Index) Indices.push_back(Index);
                ConfigureAutomaticCubicTangents(Guides, Indices);
            }
            for (size_t Index = 0; Index < Guides.Anchors.size(); ++Index)
            {
                const auto &Anchor = Guides.Anchors[Index];
                const auto Position = Project(Anchor.Position);
                ControlExtents["Pen anchor " + std::to_string(Index)] = {Position.x - 6 * Scale, Position.y - 6 * Scale, Position.x + 6 * Scale, Position.y + 6 * Scale};
                Ink->AddCircle(Position, 4 * Scale, IM_COL32(204, 230, 181, 255));
                if (Index + 1 == PendingPath.Anchors.size())
                {
                    Ink->AddLine(Project(Anchor.Incoming), Project(Anchor.Outgoing), IM_COL32(170, 197, 150, 210));
                    for (auto Coordinate : {Anchor.Incoming, Anchor.Outgoing}) Ink->AddCircleFilled(Project(Coordinate), 3 * Scale, IM_COL32(204, 230, 181, 255));
                }
            }
            Ink->PopClipRect();
        }
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
    return Consumed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     RETAINED POINT GUIDES AND INPUT
//------------------------------------------------------------------------------------------------------------------------

bool StampWorkspacePanel::ConstructPointInteraction(PlaneExtent Canvas, PlaneExtent Board, bool Hovered)
{
    if ((Tool != VectorAuthoringTool::Points && Gesture != 13 && Gesture != 14) || MaskSelection || Selection.size() != 1) return false;
    const auto Snapshot = QueryDocument();
    const auto Position = std::find_if(Snapshot.Layers.begin(), Snapshot.Layers.end(), [&](const auto &Layer) { return Layer.Token == Selection[0]; });
    // 📝 Any non-Boolean source (0-5 primitives, or an existing 6=cubic path) can be driven by this
    // tool: dragging a primitive's corner the first time implicitly converts it via DerivePrimitiveCubicPath,
    // exactly what the Inspector's own "Convert to cubic path" button does explicitly.
    if (Position == Snapshot.Layers.end() || Snapshot.Instructions[Position->Root].Topology[0] > 6
        || QueryVectorAuthoringProtection(Snapshot, *Position) || !QueryVectorAuthoringVisibility(Snapshot, *Position)) return false;
    if (PointToken != Position->Token) { PointToken = Position->Token; ActiveAnchor = SIZE_MAX; SelectedAnchors.clear(); }
    const auto &Input = ImGui::GetIO();
    try
    {
        const auto Category = Snapshot.Instructions[Position->Root].Topology[0];
        const auto Path = Category == 6 ? DecodeCubicPath(Snapshot, PointToken) : DerivePrimitiveCubicPath(Snapshot, PointToken);
        if (ActiveAnchor >= Path.Anchors.size()) ActiveAnchor = SIZE_MAX;
        std::erase_if(SelectedAnchors, [&](size_t Index) { return Index >= Path.Anchors.size(); });
        const auto Project = [&](std::array<float, 2> Coordinate)
        {
            return ImVec2(Board.MinimumX + Coordinate[0] / 512 * Board.Width(), Board.MinimumY + Coordinate[1] / 512 * Board.Height());
        };
        auto *Ink = ImGui::GetWindowDrawList();
        Ink->PushClipRect(ImVec2(Canvas.MinimumX, Canvas.MinimumY), ImVec2(Canvas.MaximumX, Canvas.MaximumY), true);
        size_t Hit = SIZE_MAX;
        CubicControl HitControl = CubicControl::Anchor;
        const auto Grip = [&](size_t Index, CubicControl Control, const char *Caption, std::array<float, 2> Coordinate)
        {
            const auto Point = Project(Coordinate);
            const float Radius = 6 * Scale;
            ControlExtents[std::string(Caption) + std::to_string(Index)] = {Point.x - Radius, Point.y - Radius, Point.x + Radius, Point.y + Radius};
            const bool Selected = std::find(SelectedAnchors.begin(), SelectedAnchors.end(), Index) != SelectedAnchors.end();
            Ink->AddCircleFilled(Point, (Control == CubicControl::Anchor ? 4 : 3) * Scale,
                Selected ? IM_COL32(230, 246, 211, 255) : IM_COL32(130, 153, 117, 255));
            Ink->AddCircle(Point, (Control == CubicControl::Anchor ? 4 : 3) * Scale, IM_COL32(45, 65, 40, 255));
            if (std::hypot(Input.MousePos.x - Point.x, Input.MousePos.y - Point.y) <= Radius)
            {
                Hit = Index; HitControl = Control;
            }
        };
        if (ActiveAnchor != SIZE_MAX)
        {
            const auto &Anchor = Path.Anchors[ActiveAnchor];
            for (auto Control : {CubicControl::Incoming, CubicControl::Outgoing})
            {
                if ((!Path.Closed && ActiveAnchor == 0 && Control == CubicControl::Incoming)
                    || (!Path.Closed && ActiveAnchor + 1 == Path.Anchors.size() && Control == CubicControl::Outgoing)) continue;
                const auto Coordinate = Control == CubicControl::Incoming ? Anchor.Incoming : Anchor.Outgoing;
                Ink->AddLine(Project(Anchor.Position), Project(Coordinate), IM_COL32(170, 197, 150, 210));
                Grip(ActiveAnchor, Control, Control == CubicControl::Incoming ? "Path incoming " : "Path outgoing ", Coordinate);
            }
        }
        // 📝 Anchors take hit precedence over coincident corner tangents.
        for (size_t Index = 0; Index < Path.Anchors.size(); ++Index) Grip(Index, CubicControl::Anchor, "Path anchor ", Path.Anchors[Index].Position);
        Ink->PopClipRect();
        const std::array<float, 2> Pointer{(Input.MousePos.x - Board.MinimumX) / Board.Width() * 512,
            (Input.MousePos.y - Board.MinimumY) / Board.Height() * 512};
        if (Gesture == 0 && Hovered && Hit == SIZE_MAX && ImGui::IsMousePosValid() && !ImGui::IsKeyDown(ImGuiKey_Space))
        {
            const auto Projection = ProjectCubicSegment(Path, Pointer);
            if (Projection.Distance * Board.Width() / 512 <= 6 * Scale && Projection.Parameter >= .02f && Projection.Parameter <= .98f)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (ImGui::IsMouseClicked(0))
                {
                    SegmentProjection = Projection; SegmentBending = false; PointDragSource = Path;
                    GestureSource = History.QueryDocument(); GestureSelection = Selection;
                    PointerOrigin = {Pointer[0], Pointer[1]}; Gesture = 14;
                }
            }
        }
        if (Gesture == 14 && GestureSource)
        {
            if (std::hypot(Pointer[0] - PointerOrigin.X, Pointer[1] - PointerOrigin.Y) * Board.Width() / 512 >= 2 * Scale)
                SegmentBending = true;
            if (SegmentBending || ImGui::IsMouseReleased(0))
            {
                auto Edited = PointDragSource;
                if (SegmentBending) BendCubicSegment(Edited, SegmentProjection.SegmentIndex, SegmentProjection.Parameter,
                    {Pointer[0] - PointerOrigin.X, Pointer[1] - PointerOrigin.Y});
                else
                {
                    SubdivideCubicSegment(Edited, SegmentProjection.SegmentIndex, SegmentProjection.Parameter);
                    ActiveAnchor = SegmentProjection.SegmentIndex + 1; SelectedAnchors = {ActiveAnchor};
                }
                auto Draft = *GestureSource; ReplaceCubicPath(Draft, PointToken, Edited); GestureDraft = std::move(Draft); ++GestureRevision;
            }
            return true;
        }
        if (Hovered && Hit != SIZE_MAX) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (Gesture == 0 && Hovered && Hit != SIZE_MAX && ImGui::IsMouseClicked(0) && !ImGui::IsKeyDown(ImGuiKey_Space))
        {
            if (HitControl == CubicControl::Anchor)
            {
                const auto Included = std::find(SelectedAnchors.begin(), SelectedAnchors.end(), Hit);
                if (Input.KeyShift && Included != SelectedAnchors.end())
                {
                    SelectedAnchors.erase(Included);
                    ActiveAnchor = SelectedAnchors.empty() ? SIZE_MAX : SelectedAnchors.back();
                    return true;
                }
                if (Included == SelectedAnchors.end())
                {
                    if (!Input.KeyShift) SelectedAnchors.clear();
                    SelectedAnchors.push_back(Hit);
                }
            }
            ActiveAnchor = Hit; ActiveControl = HitControl;
            PointDragSource = Path;
            GestureSource = History.QueryDocument(); GestureSelection = Selection;
            PointerOrigin = {(Input.MousePos.x - Board.MinimumX) / Board.Width() * 512,
                (Input.MousePos.y - Board.MinimumY) / Board.Height() * 512};
            Gesture = 13;
        }
        if (Gesture == 13 && GestureSource && (ImGui::IsMouseDragging(0, 2) || ImGui::IsMouseReleased(0)) && ImGui::IsMousePosValid()
            && std::hypot((Input.MousePos.x - Board.MinimumX) / Board.Width() * 512 - PointerOrigin.X,
                (Input.MousePos.y - Board.MinimumY) / Board.Height() * 512 - PointerOrigin.Y) * Board.Width() / 512 >= 2 * Scale)
        {
            const auto &Anchor = PointDragSource.Anchors.at(ActiveAnchor);
            const auto Origin = ActiveControl == CubicControl::Anchor ? Anchor.Position
                : ActiveControl == CubicControl::Incoming ? Anchor.Incoming : Anchor.Outgoing;
            std::array<float, 2> Destination{Origin[0] + (Input.MousePos.x - Board.MinimumX) / Board.Width() * 512 - PointerOrigin.X,
                Origin[1] + (Input.MousePos.y - Board.MinimumY) / Board.Height() * 512 - PointerOrigin.Y};
            if (SnappingEnabled) for (auto &Coordinate : Destination) Coordinate = std::round(Coordinate / 16) * 16;
            if (Input.KeyShift && ActiveControl != CubicControl::Anchor) Destination = ConstrainCubicDirection(Anchor.Position, Destination);
            auto Edited = PointDragSource;
            if (ActiveControl == CubicControl::Anchor)
                TranslateCubicAnchors(Edited, SelectedAnchors, {Destination[0] - Origin[0], Destination[1] - Origin[1]});
            else TranslateCubicControl(Edited, ActiveAnchor, ActiveControl, Destination, Input.KeyAlt);
            auto Draft = *GestureSource;
            ReplaceCubicPath(Draft, PointToken, Edited);
            GestureDraft = std::move(Draft);
            ++GestureRevision;
        }
        return Gesture == 13;
    }
    catch (const std::exception &Failure)
    {
        Diagnostic = Failure.what();
        if (Gesture == 13 || Gesture == 14) { GestureDraft.reset(); GestureSource.reset(); Gesture = 0; ++GestureRevision; }
        return true;
    }
}

bool StampWorkspacePanel::Save()
{
    bool DocumentSaved = false;
    if (SaveSource)
    {
        try
        {
            if (!SaveSource(EncodeVectorAuthoring(QueryDocument()))) { Diagnostic = "The host refused the document source (too large or unwritable)."; return false; }
            SavedDocument = QueryDocument();
            DocumentSaved = true;
        }
        catch (const std::exception &Failure) { Diagnostic = Failure.what(); return false; }
    }
    // 📝 Saving in this workspace also publishes the decal into the shared stamp library - updating
    // the library file this document is bound to, or creating its first entry - so the stamp shows
    // in the Stamp library tab and in Texture Paint the moment it is saved, with no second command
    // to discover. "Save to stamp library" (Document actions) remains the explicit save-a-copy path.
    if (PublishToLibrary(false))
    {
        Diagnostic = "Saved '" + QueryDocument().Name + "' - it now shows in the Stamp library tab and Texture Paint's stamp tool.";
        return true;
    }
    if (DocumentSaved)
    {
        // PublishToLibrary left its refusal in Diagnostic; keep it visible next to the good news.
        Diagnostic = "Document saved; stamp library not updated - " + Diagnostic;
        return true;
    }
    return false;
}

bool StampWorkspacePanel::PublishToLibrary(bool AsNewStamp)
{
    if (StampLibraryDirectory.empty()) { Diagnostic = "No stamp library directory is configured by the host."; return false; }
    const auto &Document = QueryDocument();
    if (Document.Layers.empty()) { Diagnostic = "Add a component before saving this stamp to the library."; return false; }
    try
    {
        // 📝 ThorVG's ABGR8888S target lands in memory as R,G,B,A bytes (little-endian word), which is
        // exactly the straight-alpha RGBA the library PNG stores - no swizzle, no unpremultiply.
        constexpr uint32_t Extent = 512;
        const std::vector<uint8_t> Pixels = RasterizeStampDocument(Document, Extent);
        bool AnyArt = false;
        for (size_t Pixel = 3; Pixel < Pixels.size(); Pixel += 4)
            if (Pixels[Pixel] > 0) { AnyArt = true; break; }
        if (!AnyArt) { Diagnostic = "The stamp rasterized fully transparent - check component visibility."; return false; }
        const std::string SourceToml = EncodeVectorAuthoring(Document);
        std::error_code PathFailure;
        const bool Bound = !AsNewStamp && !LibraryFile.empty() &&
            std::filesystem::equivalent(LibraryFile.parent_path(), StampLibraryDirectory, PathFailure);
        std::string Id;
        const bool Written = Bound
            ? StampLibrary::SaveRasterToFile(LibraryFile, int(Extent), int(Extent), Pixels, SourceToml, Id, Diagnostic)
            : StampLibrary::SaveRaster(StampLibraryDirectory, Document.Name, int(Extent), int(Extent), Pixels, SourceToml, Id, Diagnostic);
        if (!Written) return false;
        // Bind (or re-bind) this document to the file just written, so the next Save updates the same
        // library card instead of adding another versioned copy; the tab rescans and shows it at once.
        LibraryFile = StampLibraryDirectory / Id.substr(5); // strip the "disk:" prefix
        LibraryScanned = false;
        LibrarySelected = Id;
        return true;
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); return false; }
}

bool StampWorkspacePanel::SaveToLibrary()
{
    if (!PublishToLibrary(true)) return false;
    Diagnostic = "Saved '" + QueryDocument().Name + "' to the stamp library as a new stamp - pick it from the Stamp library tab or Texture Paint's stamp tool.";
    return true;
}

bool StampWorkspacePanel::RequestClose()
{
    if (GestureSource) { Selection = GestureSelection; GestureSource.reset(); GestureDraft.reset(); Gesture = 0; }
    if (!QueryModified()) return true;
    CloseRequested = true;
    return false;
}

bool StampWorkspacePanel::ConsumeMaterialApplyRequest() noexcept
{
    const bool Requested = ApplyToMaterialRequested;
    ApplyToMaterialRequested = false;
    return Requested;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     HEADER AND ADD-COMPONENT MENU
//------------------------------------------------------------------------------------------------------------------------

void StampWorkspacePanel::ConstructHeader()
{
    const auto *Viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(Viewport->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(Viewport->WorkSize.x, 43 * Scale));
    ImGui::Begin("Stamp header", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar);
    const auto Origin = ImGui::GetWindowPos();
    const float Width = ImGui::GetWindowWidth();
    ImGui::GetWindowDrawList()->AddRectFilled(Origin, ImVec2(Origin.x + Width, Origin.y + 43 * Scale), ImGui::GetColorU32(Spectrum(0x0b0b0b)));
    Caption(ImVec2(Origin.x + 16 * Scale, Origin.y + 16 * Scale), QueryDocument().Name.c_str(), 10 * Scale, 0xc5c5b8);
    Caption(ImVec2(Origin.x + 22 * Scale + ImGui::CalcTextSize(QueryDocument().Name.c_str()).x, Origin.y + 19 * Scale), ".stamp", 7 * Scale, 0x67675b);
    Controls.ControlHeight = 24;
    const float NavigationX = Origin.x + std::max(230 * Scale, Width - 620 * Scale);
    const char *Workspaces[]{"Material", "Pattern", "Texture", "Stamp", "Baking"};
    const float Widths[]{64, 58, 54, 52, 64};
    float PositionX = NavigationX;
    for (size_t Index = 0; Index < 5; ++Index)
    {
        const bool Available = true;
        ImGui::SetCursorScreenPos(ImVec2(PositionX, Origin.y + 9 * Scale));
        ImGui::BeginDisabled((Index == 0 && !ActivateMaterial) || (Index == 1 && !ActivatePattern) || (Index == 2 && !ActivateTexture) || (Index == 4 && !ActivateBaking));
        if (Controls.Button(Workspaces[Index], Widths[Index] * Scale, Index == 3, {}, true))
        {
            if (Index == 0 && ActivateMaterial) ActivateMaterial();
            if (Index == 1 && ActivatePattern) ActivatePattern();
            if (Index == 2 && ActivateTexture) ActivateTexture();
            if (Index == 4 && ActivateBaking) ActivateBaking();
        }
        Controls.Tooltip(Workspaces[Index], Available ? "Switch workspace without losing your document." : "This native workspace is not hosted yet.");
        ImGui::EndDisabled();
        PositionX += Widths[Index] * Scale;
    }
    const auto HeaderButton = [&](const char *Label, float X, const char *Path, bool Enabled)
    {
        ImGui::SetCursorScreenPos(ImVec2(Origin.x + Width + X * Scale, Origin.y + 9 * Scale));
        ImGui::BeginDisabled(!Enabled);
        const bool Pressed = Controls.Button(Label, 28 * Scale, false, Path);
        Controls.Tooltip(Label, Label);
        ImGui::EndDisabled();
        return Pressed;
    };
    if (HeaderButton("Undo", -308, "M3 10L7 6L11 10M7 6V15A5 5 0 0 0 17 15", History.QueryUndoAvailable())) RestoreHistory(false);
    if (HeaderButton("Redo", -272, "M21 10L17 6L13 10M17 6V15A5 5 0 0 1 7 15", History.QueryRedoAvailable())) RestoreHistory(true);
    if (HeaderButton("Save document", -236, "M12 3V16M7 11L12 16L17 11M4 17V21H20V17", bool(SaveSource))) Save();
    if (HeaderButton("Document actions", -200, "M4 12H5M11 12H12M18 12H19", true)) ImGui::OpenPopup("Stamp document actions");
    if (DropdownControlSurface::BeginMenu("Stamp document actions", Scale))
    {
        if (DropdownControlSurface::Option("New stamp", false, Scale))
        {
            if (QueryModified()) Diagnostic = "Save or close the current document before creating a new stamp.";
            else { VectorAuthoringDocument Blank; Blank.Stamp = true; Blank.Name = "Untitled stamp"; ConfigureDocument(Blank); }
        }
        if (DropdownControlSurface::Option("Open native stamp", false, Scale, bool(LoadSource)))
        {
            if (QueryModified()) Diagnostic = "Save or close the current document before opening another.";
            else try { if (const auto Source = LoadSource()) ConfigureDocument(DecodeVectorAuthoring(*Source)); }
                catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
        }
        const auto DocumentOption = [&](const char *CaptionText, bool Enabled)
        {
            const bool Activated = DropdownControlSurface::Option(CaptionText, false, Scale, Enabled);
            const auto Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
            ControlExtents[std::string("Document ") + CaptionText] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
            return Activated;
        };
        if (DocumentOption("Save document", bool(SaveSource))) Save();
        // 🖨️ Rasterises this document into the project Stamps folder so Texture Paint's stamp tool
        // can place it (its right-click grid rescans the folder every time it opens).
        if (DocumentOption("Save to stamp library", true)) SaveToLibrary();
        const auto EditOption = [&](const char *CaptionText, bool Enabled)
        {
            const bool Activated = DropdownControlSurface::Option(CaptionText, false, Scale, Enabled);
            const auto Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
            ControlExtents[std::string("Document ") + CaptionText] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
            return Activated;
        };
        const bool Idle = Gesture == 0 && !MaskSelection;
        const bool Selected = Idle && !Selection.empty();
        bool Editable = Selected;
        for (const auto &Layer : History.QueryDocument().Layers)
            if (std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end()
                && (QueryVectorAuthoringProtection(History.QueryDocument(), Layer) || !QueryVectorAuthoringVisibility(History.QueryDocument(), Layer))) Editable = false;
        if (EditOption("Copy selection", Selected)) TransferSelection(false);
        if (EditOption("Cut selection", Editable)) TransferSelection(true);
        if (EditOption("Paste selection", Idle && bool(SelectionClipboard))) InsertSelection(false);
        if (EditOption("Paste in place", Idle && bool(SelectionClipboard))) InsertSelection(true);
        if (EditOption("Duplicate selection", Editable)) ReplicateSelection();
        if (EditOption("Select all editable", Idle)) SelectEditableVectors();
        const char *OrderCaptions[]{"Send to back", "Send backward", "Bring forward", "Bring to front"};
        for (uint32_t Index = 0; Index < 4; ++Index)
            if (EditOption(OrderCaptions[Index], Editable))
                EvaluateEdit([&](auto &Document) { ReorderVectorAuthoringSelection(Document, Selection, VectorSelectionOrder(Index)); });
        ImGui::TextDisabled("Clipboard: %zu components (editor session)", SelectionClipboard ? SelectionClipboard->Layers.size() : size_t(0));
        ImGui::TextDisabled("Native authoring uses TOML, not HTML JSON.");
        DropdownControlSurface::EndMenu();
    }
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + Width - 168 * Scale, Origin.y + 9 * Scale));
    if (Controls.Button("Apply to material", 116 * Scale)) ApplyToMaterialRequested = true;
    Controls.Tooltip(
        "Apply to material",
        "Bake the decal (shape/text/vector + channel compositing) onto the selected material preview. "
        "Stamp edits never touch materials until applied.");
    if (HeaderButton("Close stamp studio", -44, "M6 6L18 18M6 18L18 6", bool(ActivateMaterial)))
        if (RequestClose()) ActivateMaterial();
    Controls.ControlHeight = 30;
    ImGui::End();
}

void StampWorkspacePanel::ConstructAddComponentMenu()
{
    const auto &Snapshot = QueryDocument();
    const bool AtCap = Snapshot.Layers.size() >= 24;
    ImGui::TextDisabled("Shapes");
    for (uint32_t Category = 0; Category < 6; ++Category)
    {
        ImGui::BeginDisabled(AtCap);
        if (DropdownControlSurface::Option(ShapeCaptions[Category], false, Scale)) { ConstructPrimitive(Category, ShapeCaptions[Category]); ImGui::CloseCurrentPopup(); }
        ImGui::EndDisabled();
        const auto Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
        ControlExtents[ShapeCaptions[Category]] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
    }
    ImGui::Separator();
    ImGui::TextDisabled("Decal components");
    const auto DecalOption = [&](const char *CaptionText, const char *PopupKey)
    {
        ImGui::BeginDisabled(AtCap);
        const bool Activated = DropdownControlSurface::Option(CaptionText, false, Scale);
        ImGui::EndDisabled();
        if (Activated)
        {
            ComponentEditToken = 0;
            ComponentTextBuffer[0] = 0;
            ComponentSvgBuffer.clear();
            ComponentImageBuffer.clear();
            ImGui::CloseCurrentPopup();
            // 📝 Do not OpenPopup(PopupKey) here: this lambda runs inside the "Add a stamp component"
            // dropdown's own popup window, a different ID-stack scope than where the modal is polled.
            // Defer to the stable scope in ConstructLayers() instead (see PendingComponentPopup).
            PendingComponentPopup = PopupKey;
        }
    };
    DecalOption("Add vector (SVG)...", "Add stamp vector");
    DecalOption("Add image...", "Add stamp image");
    ImGui::TextDisabled("Use the Text tool in the canvas toolbar to draw and write a text component.");
    if (AtCap) ImGui::TextWrapped("Stamp decals are capped at 24 components (Pattern allows 64).");
}

// 📝 Finds the largest point size whose word-wrapped glyph-outline layout still fits inside
// {BoxWidth, BoxHeight} (into a throwaway scratch document, never touching the real one) — the
// "fit to box" behaviour an SVG/Illustrator-style text tool gives when you drag out a frame and
// type into it. Text wraps onto as many lines as needed within BoxWidth, so a longer caption grows
// downward with more lines instead of shrinking indefinitely onto one ever-thinner line; the point
// size only shrinks once the wrapped block's own height would overflow the box. Binary-searches the
// real glyph bounds directly (reading the baked instruction's own ink extents, never the selection
// query's visible-canvas-clamped bounds, since this scratch text is never actually placed on a
// canvas) because wrapping makes line count — and so required height — a step function of point
// size, not the simple linear ratio a single probe size could extrapolate from.
float MeasureStampTextFitSize(const std::string &Content, float BoxWidth, float BoxHeight, const std::filesystem::path &ContentRoot)
{
    const std::string TypefacePath = ContentRoot.empty() ? std::string("EngineContent/FontArchives/DMSans/DMSans-Regular.ttf")
        : (ContentRoot / "EngineContent/FontArchives/DMSans/DMSans-Regular.ttf").string();
    const float Width = std::max(1.f, BoxWidth), Height = std::max(1.f, BoxHeight);
    const auto Fits = [&](float PointSize)
    {
        VectorAuthoringDocument Scratch; Scratch.Stamp = true;
        const uint32_t Root = AppendStampTextOutline(Scratch, Content, PointSize, {0, 0, 0, 1}, TypefacePath, Width);
        const auto &Instruction = Scratch.Instructions[Root];
        return Instruction.Bounds[2] <= Width && Instruction.Bounds[3] <= Height;
    };
    float Low = 4.f, High = 512.f;
    if (!Fits(Low)) return Low; // Even the smallest permitted size cannot fit; still return something sane.
    for (int Step = 0; Step < 28; ++Step)
    {
        const float Mid = (Low + High) * .5f;
        if (Fits(Mid)) Low = Mid; else High = Mid;
    }
    return std::clamp(Low, 4.f, 512.f);
}

void StampWorkspacePanel::RebakeTextLayer(uint32_t Token, const std::string &Content, float PointSize, float WrapWidth)
{
    if (Content.empty()) { Diagnostic = "Text content cannot be empty."; return; }
    try
    {
        EvaluateEdit([&](auto &Document)
        {
            const auto Existing = std::find_if(Document.Layers.begin(), Document.Layers.end(), [&](const auto &Layer) { return Layer.Token == Token; });
            if (Existing == Document.Layers.end()) throw std::runtime_error("The edited text component no longer exists.");
            const auto OldBounds = QueryVectorAuthoringSelectionBounds(Document, std::array<uint32_t, 1>{Token});
            const std::array<float, 4> Colour = Document.Instructions[Existing->Root].Colour;
            // 📝 Resolve the glyph-outline typeface off the app's own executable-relative content
            // root when the host configured one, instead of trusting the process's current working
            // directory to happen to be the EngineContent-containing folder.
            const uint32_t NewRoot = ContentRoot.empty() ? AppendStampTextOutline(Document, Content, PointSize, Colour, "EngineContent/FontArchives/DMSans/DMSans-Regular.ttf", WrapWidth)
                : AppendStampTextOutline(Document, Content, PointSize, Colour,
                      (ContentRoot / "EngineContent/FontArchives/DMSans/DMSans-Regular.ttf").string(), WrapWidth);
            for (auto &Layer : Document.Layers) if (Layer.Token == Token) { Layer.Root = NewRoot; Layer.SourceMarkup = Content; Layer.TextPointSize = PointSize; }
            ReclaimVectorAuthoringSources(Document);
            const auto NewBounds = QueryVectorAuthoringSelectionBounds(Document, std::array<uint32_t, 1>{Token});
            VectorAuthoringSourcePatch Patch;
            Patch.TranslationX = OldBounds[0] - NewBounds[0];
            Patch.TranslationY = OldBounds[1] - NewBounds[1];
            TransformVectorAuthoringSelection(Document, std::array<uint32_t, 1>{Token}, Patch);
        });
        Diagnostic.clear();
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

void StampWorkspacePanel::CommitTextComposer()
{
    std::string Content = ComponentTextBuffer.data();
    const auto NotSpace = [](unsigned char Character) { return !std::isspace(Character); };
    Content.erase(Content.begin(), std::find_if(Content.begin(), Content.end(), NotSpace));
    Content.erase(std::find_if(Content.rbegin(), Content.rend(), NotSpace).base(), Content.end());
    const uint32_t EditToken = ComponentEditToken;
    const auto Bounds = TextEditBounds;
    Gesture = 0; ComponentTextBuffer[0] = 0; ComponentEditToken = 0;
    if (Content.empty()) { Diagnostic.clear(); return; } // Nothing was written; discard the box silently.
    try
    {
        const float PointSize = MeasureStampTextFitSize(Content, Bounds[2], Bounds[3], ContentRoot);
        if (EditToken) { RebakeTextLayer(EditToken, Content, PointSize, Bounds[2]); return; }
        EvaluateEdit([&](auto &Document)
        {
            if (Document.TokenClock == UINT32_MAX) throw std::runtime_error("VectorAuthoring identity clock exhausted.");
            const uint32_t NewRoot = ContentRoot.empty() ? AppendStampTextOutline(Document, Content, PointSize, {.08f, .08f, .08f, 1}, "EngineContent/FontArchives/DMSans/DMSans-Regular.ttf", Bounds[2])
                : AppendStampTextOutline(Document, Content, PointSize, {.08f, .08f, .08f, 1},
                      (ContentRoot / "EngineContent/FontArchives/DMSans/DMSans-Regular.ttf").string(), Bounds[2]);
            VectorAuthoringLayer Layer;
            Layer.Token = ++Document.TokenClock;
            Layer.Root = NewRoot;
            Layer.Name = Content.size() > 24 ? Content.substr(0, 24) + "..." : Content;
            Layer.ComponentKind = VectorAuthoringComponentKind::Text;
            Layer.SourceMarkup = Content;
            Layer.TextPointSize = PointSize;
            if (Document.Layers.empty()) Document.Name = "Custom stamp";
            Document.Layers.push_back(Layer);
            ReclaimVectorAuthoringSources(Document);
            const auto NewBounds = QueryVectorAuthoringSelectionBounds(Document, std::array<uint32_t, 1>{Layer.Token});
            VectorAuthoringSourcePatch Patch;
            Patch.TranslationX = Bounds[0] - NewBounds[0];
            Patch.TranslationY = Bounds[1] - NewBounds[1];
            TransformVectorAuthoringSelection(Document, std::array<uint32_t, 1>{Layer.Token}, Patch);
            Selection = {Layer.Token};
        });
        Diagnostic.clear();
    }
    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
}

void StampWorkspacePanel::ConstructTextComposer(float BoardMinimumX, float BoardMinimumY, float BoardSize)
{
    // 📝 An SVG/Illustrator-style text tool: the box was already drawn (or an existing text
    // component's bounds were loaded) into TextEditBounds, so this just overlays a borderless,
    // auto-focused text field exactly over that box — typing feels like writing directly onto the
    // canvas. Escape cancels; Enter or clicking anywhere else commits (CommitTextComposer bakes the
    // real glyph-outline component, auto-fit to the drawn box, once there is something to bake).
    const ImVec2 Minimum(BoardMinimumX + (TextEditBounds[0] - TextEditBounds[2] * .5f) / 512 * BoardSize,
                          BoardMinimumY + (TextEditBounds[1] - TextEditBounds[3] * .5f) / 512 * BoardSize);
    const ImVec2 Maximum(BoardMinimumX + (TextEditBounds[0] + TextEditBounds[2] * .5f) / 512 * BoardSize,
                          BoardMinimumY + (TextEditBounds[1] + TextEditBounds[3] * .5f) / 512 * BoardSize);
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(Minimum, Maximum, IM_COL32(255, 255, 255, 240));
    Ink->AddRect(Minimum, Maximum, IM_COL32(134, 164, 112, 255), 0, 0, std::max(1.f, 1.5f * Scale));
    ImGui::SetCursorScreenPos(Minimum);
    ImGui::PushID("StampTextComposer");
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.06f, .06f, .05f, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
    // 📝 Keep the live text already at the size the commit will actually bake at — recomputed (via
    // the same MeasureStampTextFitSize the commit itself calls) only when the composed text changes,
    // never every frame. Without this the field typed at a small, generic caret size and only jumped
    // to the real auto-fit size (often much larger) the instant the box committed.
    const std::string CurrentContent = ComponentTextBuffer.data();
    char BoxKeySuffix[64]{};
    std::snprintf(BoxKeySuffix, sizeof(BoxKeySuffix), "\x01%.3f\x01%.3f", double(TextEditBounds[2]), double(TextEditBounds[3]));
    const std::string CacheKey = CurrentContent + BoxKeySuffix;
    if (CacheKey != TextComposerFitCacheKey)
    {
        TextComposerFitCacheKey = CacheKey;
        try { TextComposerFitPointSize = CurrentContent.empty() ? 0.f : MeasureStampTextFitSize(CurrentContent, TextEditBounds[2], TextEditBounds[3], ContentRoot); }
        catch (const std::exception &) { TextComposerFitPointSize = 0.f; }
    }
    const float LivePixelSize = TextComposerFitPointSize > 0
        ? std::clamp(TextComposerFitPointSize * (BoardSize / 512.f), 8.f, 400.f) : 16.f * Scale;
    ImGui::PushFont(static_cast<ImFont *>(RegularTypeface ? RegularTypeface : LightTypeface), LivePixelSize);
    if (TextEditFocusPending) { ImGui::SetKeyboardFocusHere(); TextEditFocusPending = false; }
    // 📝 ImGui's multiline default is backwards for this use case (plain Enter inserts a newline;
    // Ctrl+Enter validates) — CtrlEnterForNewLine flips it so plain Enter commits the component like
    // the spec calls for, and Ctrl+Enter is the escape hatch for an actual multi-line caption.
    const bool EnterCommit = ImGui::InputTextMultiline("##Compose", ComponentTextBuffer.data(), ComponentTextBuffer.size(),
        ImVec2(Maximum.x - Minimum.x, Maximum.y - Minimum.y), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);
    const bool Deactivated = ImGui::IsItemDeactivated();
    ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::PopID();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { Gesture = 0; ComponentTextBuffer[0] = 0; ComponentEditToken = 0; Diagnostic.clear(); }
    else if (EnterCommit || Deactivated) CommitTextComposer();
}

void StampWorkspacePanel::ConstructVectorPopup()
{
    ImGui::SetNextWindowSize(ImVec2(520 * Scale, 0));
    const StampModalScope ModalScope(Scale);
    if (ImGui::BeginPopupModal("Add stamp vector", nullptr, ImGuiWindowFlags_NoResize))
    {
        ImGui::TextWrapped(ComponentEditToken ? "Replace this component's SVG markup." : "Paste sanitized SVG markup. Every solid-filled shape imports with its own colour and transform; gradients fall back to a neutral grey; scripts and external references are never evaluated.");
        ImGui::Spacing();
        ComponentSvgBuffer.resize(std::max<size_t>(ComponentSvgBuffer.size(), VectorAuthoringSourceMarkupLimit));
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextMultiline("##StampSvgContent", ComponentSvgBuffer.data(), ComponentSvgBuffer.size(), ImVec2(0, 160 * Scale));
        if (!Diagnostic.empty()) ImGui::TextWrapped("%s", Diagnostic.c_str());
        ImGui::Spacing();
        if (Controls.Button("Cancel", 120 * Scale)) { ImGui::CloseCurrentPopup(); Diagnostic.clear(); }
        ImGui::SameLine(0, 8 * Scale);
        if (Controls.Button(ComponentEditToken ? "Apply" : "Import", 120 * Scale, true))
        {
            const std::string Markup = ComponentSvgBuffer.c_str();
            const uint32_t EditToken = ComponentEditToken;
            try
            {
                EvaluateEdit([&](auto &Document)
                {
                    std::array<float, 2> AnchorCentre{256, 256};
                    if (EditToken)
                    {
                        const auto Existing = std::find_if(Document.Layers.begin(), Document.Layers.end(), [&](const auto &Layer) { return Layer.Token == EditToken; });
                        if (Existing == Document.Layers.end()) throw std::runtime_error("The edited vector component no longer exists.");
                        const auto OldBounds = QueryVectorAuthoringSelectionBounds(Document, std::array<uint32_t, 1>{EditToken});
                        AnchorCentre = {OldBounds[0], OldBounds[1]};
                    }
                    const uint32_t NewRoot = AppendStampVectorImport(Document, Markup);
                    if (EditToken)
                        for (auto &Layer : Document.Layers) if (Layer.Token == EditToken) { Layer.Root = NewRoot; Layer.SourceMarkup = Markup; }
                    else
                    {
                        if (Document.TokenClock == UINT32_MAX) throw std::runtime_error("VectorAuthoring identity clock exhausted.");
                        VectorAuthoringLayer Layer;
                        Layer.Token = ++Document.TokenClock;
                        Layer.Root = NewRoot;
                        Layer.Name = "Imported vector";
                        Layer.ComponentKind = VectorAuthoringComponentKind::ImportedVector;
                        Layer.SourceMarkup = Markup;
                        if (Document.Layers.empty()) Document.Name = "Custom stamp";
                        Document.Layers.push_back(Layer);
                    }
                    ReclaimVectorAuthoringSources(Document);
                    const uint32_t FinalToken = EditToken ? EditToken : Document.Layers.back().Token;
                    const auto NewBounds = QueryVectorAuthoringSelectionBounds(Document, std::array<uint32_t, 1>{FinalToken});
                    VectorAuthoringSourcePatch Patch;
                    Patch.TranslationX = AnchorCentre[0] - NewBounds[0];
                    Patch.TranslationY = AnchorCentre[1] - NewBounds[1];
                    TransformVectorAuthoringSelection(Document, std::array<uint32_t, 1>{FinalToken}, Patch);
                    Selection = {FinalToken};
                });
                if (Diagnostic.empty()) { ImGui::CloseCurrentPopup(); ComponentEditToken = 0; }
            }
            catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
        }
        ImGui::EndPopup();
    }
}

void StampWorkspacePanel::ConstructImagePopup()
{
    ImGui::SetNextWindowSize(ImVec2(520 * Scale, 0));
    const StampModalScope ModalScope(Scale);
    if (ImGui::BeginPopupModal("Add stamp image", nullptr, ImGuiWindowFlags_NoResize))
    {
        ImGui::TextWrapped(
            "Paste a PNG data URL (data:image/png;base64,...), up to 256\u00d7256 px. The native compute "
            "shader does not sample image pixels yet, so this renders as a placeholder box sized to the "
            "image's own aspect ratio until pixel sampling ships; the embedded bytes still round-trip "
            "through save/load and the HTML Stamp Studio.");
        ImGui::Spacing();
        ComponentImageBuffer.resize(std::max<size_t>(ComponentImageBuffer.size(), 300064));
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextMultiline("##StampImageContent", ComponentImageBuffer.data(), ComponentImageBuffer.size(), ImVec2(0, 100 * Scale));
        if (!Diagnostic.empty()) ImGui::TextWrapped("%s", Diagnostic.c_str());
        ImGui::Spacing();
        if (Controls.Button("Cancel", 120 * Scale)) { ImGui::CloseCurrentPopup(); Diagnostic.clear(); }
        ImGui::SameLine(0, 8 * Scale);
        if (Controls.Button(ComponentEditToken ? "Apply" : "Import", 120 * Scale, true))
        {
            const std::string DataUrl = ComponentImageBuffer.c_str();
            const uint32_t EditToken = ComponentEditToken;
            try
            {
                const auto Extent = SniffEmbeddedImageExtent(DataUrl);
                if (!Extent) throw std::runtime_error("That is not a recognizable PNG data URL.");
                const float Aspect = float((*Extent)[0]) / std::max(1.f, float((*Extent)[1]));
                EvaluateEdit([&](auto &Document)
                {
                    if (EditToken)
                    {
                        const auto Existing = std::find_if(Document.Layers.begin(), Document.Layers.end(), [&](const auto &Layer) { return Layer.Token == EditToken; });
                        if (Existing == Document.Layers.end()) throw std::runtime_error("The edited image component no longer exists.");
                        auto &Placeholder = Document.Instructions[Existing->Root];
                        const float Height = Placeholder.Bounds[3];
                        Placeholder.Bounds[2] = std::clamp(Height * Aspect, 4.f, 2048.f);
                        Existing->EmbeddedImage = DataUrl;
                    }
                    else
                    {
                        if (Document.TokenClock == UINT32_MAX) throw std::runtime_error("VectorAuthoring identity clock exhausted.");
                        VectorCoverageInstruction Source;
                        Source.Topology[0] = 0;
                        const float Height = 160.f;
                        Source.Bounds = {256, 256, std::clamp(Height * Aspect, 4.f, 2048.f), Height};
                        Source.Colour = {.55f, .55f, .55f, 1};
                        Source.StrokeColour = {.23f, .24f, .28f, 1};
                        const auto Token = ConstructVectorAuthoringPrimitive(Document, Source, "Imported image");
                        for (auto &Layer : Document.Layers) if (Layer.Token == Token)
                        {
                            Layer.ComponentKind = VectorAuthoringComponentKind::ImportedImage;
                            Layer.EmbeddedImage = DataUrl;
                        }
                        if (Document.Layers.size() == 1) Document.Name = "Custom stamp";
                        Selection = {Token};
                    }
                });
                if (Diagnostic.empty()) { ImGui::CloseCurrentPopup(); ComponentEditToken = 0; }
            }
            catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
        }
        ImGui::EndPopup();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     STAMP LIBRARY
//------------------------------------------------------------------------------------------------------------------------

void StampWorkspacePanel::ConstructLibraryCard(const StampLibraryEntry &Entry, float Width, bool Selected, size_t Slot, bool Reopenable)
{
    // Stamp card, identical to Texture Paint's stamp picker: fixed 125px box, centred 72px square
    // thumb over a two-tone checker, 9px name + 7px provenance, green check disc when selected.
    const ImVec2 Origin = ImGui::GetCursorScreenPos();
    const ImVec2 Size(Width, 125 * Scale);
    const ImVec2 Far(Origin.x + Size.x, Origin.y + Size.y);
    ImGui::InvisibleButton(("##StampCard-" + Entry.Id).c_str(), Size);
    const bool Clicked = ImGui::IsItemClicked(), Hovered = ImGui::IsItemHovered();
    ControlExtents[Entry.Name] = {Origin.x, Origin.y, Far.x, Far.y};
    // Fully offscreen cards register their extent but skip all drawing (mirrors the Pattern card).
    const float ClipTop = ImGui::GetWindowPos().y, ClipBottom = ClipTop + ImGui::GetWindowSize().y;
    if (Far.y < ClipTop || Origin.y > ClipBottom)
    {
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Far.y));
        return;
    }
    Controls.Tooltip(Entry.Name.c_str(), Reopenable ? "Open as an editable vector stamp."
        : (Entry.Builtin ? "Preset art for Texture Paint's stamp tool." : "Raster stamp for Texture Paint's stamp tool."));
    ImDrawList *Draw = ImGui::GetWindowDrawList();
    Draw->AddRectFilled(Origin, Far, Ink32(0x191919), 15 * Scale);
    const float Thumb = 72 * Scale;
    const ImVec2 ThumbMin(Origin.x + (Size.x - Thumb) * .5f, Origin.y + 6 * Scale);
    const ImVec2 ThumbMax(ThumbMin.x + Thumb, ThumbMin.y + Thumb);
    StampAtlasImage Atlas{};
    if (QueryLibraryThumbnailAtlas) Atlas = QueryLibraryThumbnailAtlas();
    if (Atlas.Texture != 0 && Slot < LibraryAtlasCapacity)
    {
        // Atlas fast path: one textured quad (checker already baked under the art); the host returns
        // the slot-0 rectangle and each card offsets it by its stable library slot.
        const float Step = float(LibraryAtlasTile + 2) / LibraryAtlasExtent;
        const float Span = float(LibraryAtlasTile) / LibraryAtlasExtent;
        const float U0 = Atlas.MinimumU + (Slot % LibraryAtlasColumns) * Step;
        const float V0 = Atlas.MinimumV + (Slot / LibraryAtlasColumns) * Step;
        Draw->AddImageRounded(ImTextureRef(Atlas.Texture), ThumbMin, ThumbMax, ImVec2(U0, V0),
            ImVec2(U0 + Span, V0 + Span), IM_COL32_WHITE, 8 * Scale);
    }
    else
    {
        // CPU fallback while the host atlas is unuploaded: checker plus directly sampled blocks.
        Draw->AddRectFilled(ThumbMin, ThumbMax, Ink32(0x1e1e1e), 8 * Scale);
        Draw->AddRectFilled(ThumbMin, ImVec2(ThumbMin.x + Thumb * .5f, ThumbMin.y + Thumb * .5f), Ink32(0x2a2a2a), 8 * Scale);
        Draw->AddRectFilled(ImVec2(ThumbMin.x + Thumb * .5f, ThumbMin.y + Thumb * .5f), ThumbMax, Ink32(0x2a2a2a), 8 * Scale);
        constexpr int Blocks = 36;
        const float Block = Thumb / Blocks;
        for (int By = 0; By < Blocks; ++By)
            for (int Bx = 0; Bx < Blocks; ++Bx)
            {
                const auto Texel = StampLibrary::Sample(Entry, (Bx + .5f) / Blocks, (By + .5f) / Blocks);
                if (Texel[3] < .03f) continue;
                const uint32_t Packed = (uint32_t(std::clamp(Texel[0], 0.f, 1.f) * 255) << 16) |
                    (uint32_t(std::clamp(Texel[1], 0.f, 1.f) * 255) << 8) | uint32_t(std::clamp(Texel[2], 0.f, 1.f) * 255);
                Draw->AddRectFilled(ImVec2(ThumbMin.x + Bx * Block, ThumbMin.y + By * Block),
                    ImVec2(ThumbMin.x + (Bx + 1) * Block + .5f, ThumbMin.y + (By + 1) * Block + .5f),
                    PanelChrome::Colour32(Packed, Texel[3]));
            }
    }
    Caption(ImVec2(Origin.x + 12 * Scale, Origin.y + 89 * Scale), Entry.Name.c_str(), 9 * Scale, 0xc8c8b6);
    Caption(ImVec2(Origin.x + 12 * Scale, Origin.y + 105 * Scale),
        Entry.Builtin ? (Entry.Tintable ? "Preset \xc2\xb7 tintable" : "Preset")
                      : (Reopenable ? "Saved stamp \xc2\xb7 vector" : "Saved stamp"),
        7 * Scale, 0x767665);
    if (Selected) PanelChrome::CheckDisc(Draw, Origin, Scale);
    if (Hovered) Draw->AddRect(Origin, Far, Ink32(0x707173), 15 * Scale);
    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Far.y));
    if (Clicked) OpenLibraryStamp(Entry, Reopenable);
}

void StampWorkspacePanel::ConstructLibrary()
{
    // Stamp library gallery in the Material workspace's catalogue design (the same page chrome as
    // the Pattern library): two shortcut pills, 18px heading + count, padded search, wrapping
    // collection pills, hairline + overline, then every match as a two-column 125px card inside one
    // scrolling grid (no paging), closing footer tag. The entries are the shared StampLibrary -
    // presets plus everything in the library directory - exactly what Texture Paint's stamp tool
    // places; clicking a saved stamp reopens its vector design here for editing.
    CapturePanel("Library");
    if (ImGui::GetContentRegionAvail().y < 35 * Scale) return;
    EnsureStampLibrary();
    RefreshLibraryAtlas();
    const auto Origin = ImGui::GetCursorScreenPos();
    const float Width = ImGui::GetContentRegionAvail().x;
    const float Bottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
    const auto At = [&](float X, float Y) { return ImVec2(Origin.x + X * Scale, Origin.y + Y * Scale); };
    const auto SmallFont = [&](float Pixels)
    {
        if (RegularTypeface) ImGui::PushFont(static_cast<ImFont *>(RegularTypeface), Pixels * 1.302f * Scale);
    };
    const auto PopSmallFont = [&]() { if (RegularTypeface) ImGui::PopFont(); };
    // Shared capsule pill (PanelChrome::Pill) at this library's 8.5px label size, clipped to the capsule.
    const auto Pill = [&](const char *Id, const char *Label, ImVec2 Position, float PillWidth, float PillHeight, bool Selected) {
        return PanelChrome::Pill(Id, Label, Position, PillWidth, PillHeight, Selected, Scale, nullptr, 0, 8.5f, true);
    };

    // Top shortcut row, mirroring Material's Workspace / My collection pills.
    const float HalfPill = (Width - 34 * Scale) * .5f;
    if (Pill("##ShortcutPresets", "Preset stamps", At(14, 10), HalfPill, 27 * Scale, LibraryFilter == 1))
    { LibraryFilter = 1; LibrarySearchText[0] = 0; }
    if (Pill("##ShortcutMine", "My stamps", ImVec2(Origin.x + Width * .5f + 3 * Scale, At(0, 10).y), HalfPill, 27 * Scale, LibraryFilter == 2))
    { LibraryFilter = 2; LibrarySearchText[0] = 0; }

    Caption(At(20, 50), "Stamp library", 18 * Scale, 0xd3d3c8, LightTypeface);
    Caption(ImVec2(Origin.x + 28 * Scale + CaptionWidth("Stamp library", 18 * Scale, LightTypeface), At(0, 59).y),
        std::to_string(Library.Entries().size()).c_str(), 8 * Scale, 0x777766);

    ImGui::SetCursorScreenPos(At(14, 82));
    ImGui::SetNextItemWidth(Width - 28 * Scale);
    SmallFont(11);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Spectrum(0x191919));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(35 * Scale, 11.5f * Scale));
    ImGui::InputTextWithHint("##StampLibrarySearch", "Search rings, badges, marks\xe2\x80\xa6", LibrarySearchText.data(), LibrarySearchText.size());
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    PopSmallFont();
    DrawGlyph("M11 4a7 7 0 1 0 0 14 7 7 0 0 0 0-14ZM21 21l-4.3-4.3", At(27, 94), 15 * Scale, 0x77776f);

    // Collection filter as wrapping pills instead of a dropdown, like Material's category row;
    // Reload rides the same row as an unlatched action pill and rescans the library directory.
    float Y = 130;
    float PillX = 14;
    const auto RowPill = [&](const char *Id, const char *Label, bool Selected) {
        const float Edge = CaptionWidth(Label, 8.5f * Scale) / Scale + 18;
        if (PillX + Edge > Width / Scale - 14) { PillX = 14; Y += 29; }
        const bool Pressed = Pill(Id, Label, At(PillX, Y), Edge * Scale, 24 * Scale, Selected);
        PillX += Edge + 5;
        return Pressed;
    };
    if (RowPill("All stamps", "All stamps", LibraryFilter == 0)) LibraryFilter = 0;
    if (RowPill("Presets", "Presets", LibraryFilter == 1)) LibraryFilter = 1;
    if (RowPill("My stamps", "My stamps", LibraryFilter == 2)) LibraryFilter = 2;
    if (RowPill("##LibraryReload", "Reload", false)) EnsureStampLibrary(true);
    Y += 24 + 8;

    const auto Matches = QueryLibraryMatches();
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddLine(At(0, Y), ImVec2(Origin.x + Width, At(0, Y).y), Ink32(0xffffff, .051f));
    Caption(At(17, Y + 12), "THE STAMP COLLECTION", 8 * Scale, 0x7e7e70);
    const std::string MatchCount = std::to_string(Matches.size());
    Caption(ImVec2(Origin.x + Width - 17 * Scale - CaptionWidth(MatchCount.c_str(), 8 * Scale), At(0, Y + 12).y),
        MatchCount.c_str(), 8 * Scale, 0x7e7e70);

    const float FooterY = Bottom - 71 * Scale;
    ImGui::SetCursorScreenPos(At(16, Y + 31));
    ImGui::BeginChild("Stamp grid", ImVec2(Width - 32 * Scale, std::max(1.f, FooterY - At(0, Y + 31).y)), ImGuiChildFlags_None);
    const float Gap = 9 * Scale, CardHeight = 125 * Scale;
    const float CardWidth = std::max(32.f, (ImGui::GetContentRegionAvail().x - Gap) * .5f);
    const auto GridOrigin = ImGui::GetCursorScreenPos();
    for (size_t Ordinal = 0; Ordinal < Matches.size(); ++Ordinal)
    {
        const ImVec2 Position(GridOrigin.x + (Ordinal % 2) * (CardWidth + Gap), GridOrigin.y + (Ordinal / 2) * (CardHeight + Gap));
        ImGui::SetCursorScreenPos(Position);
        const size_t Slot = Matches[Ordinal];
        const bool Reopenable = Slot < LibraryEntryReopenable.size() && LibraryEntryReopenable[Slot] != 0;
        const auto &Entry = Library.Entries()[Slot];
        ConstructLibraryCard(Entry, CardWidth, LibrarySelected == Entry.Id, Slot, Reopenable);
    }
    const size_t Rows = (Matches.size() + 1) / 2;
    ImGui::SetCursorScreenPos(ImVec2(GridOrigin.x, GridOrigin.y + float(Rows) * (CardHeight + Gap) + 2 * Scale));
    SmallFont(10);
    ImGui::PushTextWrapPos(GridOrigin.x + ImGui::GetContentRegionAvail().x);
    if (Matches.empty())
        ImGui::TextColored(ImVec4(147 / 255.f, 158 / 255.f, 153 / 255.f, 1), LibraryFilter == 2
            ? "No saved stamps yet. Design a decal, then Save (Ctrl+S) to publish it here."
            : "No matches. Try another collection or search.");
    ImGui::TextColored(ImVec4(147 / 255.f, 158 / 255.f, 153 / 255.f, 1),
        "Every stamp here is placeable from Texture Paint's stamp tool. Clicking one of your saved stamps reopens its editable vector design.");
    ImGui::PopTextWrapPos();
    PopSmallFont();
    ImGui::Dummy(ImVec2(1, 1));
    ImGui::EndChild();

    Ink->AddRectFilled(ImVec2(Origin.x, FooterY), ImVec2(Origin.x + Width, Bottom), Ink32(0x151515));
    DrawGlyph("M5 22H19M12 2V10M8 10H16L17 18H7Z", ImVec2(Origin.x + 19 * Scale, FooterY + 20 * Scale), 19 * Scale, 0x9a9a8e);
    Caption(ImVec2(Origin.x + 51 * Scale, FooterY + 20 * Scale), "Decals for Texture Paint.", 9 * Scale, 0xaeae9b);
    const std::string Footer = std::to_string(Library.Entries().size()) + " stamps \xc2\xb7 presets and your saved designs.";
    Caption(ImVec2(Origin.x + 51 * Scale, FooterY + 37 * Scale), Footer.c_str(), 7 * Scale, 0x6b6b60);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     LAYER STACK
//------------------------------------------------------------------------------------------------------------------------

void StampWorkspacePanel::ConstructLayers()
{
    CapturePanel("Layers");
    // 📝 Controls.Register is private; restyled bare buttons record the same captions/extents the old
    // capsule buttons did, straight into the panel map the RegisterExtent callback writes.
    const auto NoteExtent = [&](const std::string &CaptionText) {
        const auto Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
        ControlExtents[CaptionText] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
    };
    const auto Origin = ImGui::GetCursorScreenPos();
    const float Width = ImGui::GetContentRegionAvail().x;
    Caption(ImVec2(Origin.x + 16 * Scale, Origin.y + 15 * Scale), "Components", 23 * Scale, 0xd9d9cc, LightTypeface);
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + Width - 44 * Scale, Origin.y + 14 * Scale));
    const auto &Snapshot = QueryDocument();
    if (Controls.Button("Add stamp component", 28 * Scale, false, PlusPath)) ImGui::OpenPopup("Add a stamp component");
    if (DropdownControlSurface::BeginMenu("Add a stamp component", Scale))
    {
        ConstructAddComponentMenu();
        DropdownControlSurface::EndMenu();
    }
    // 📝 Open the requested add-component popup here, in the same stable ID-stack scope that
    // ConstructVectorPopup/ConstructImagePopup poll with BeginPopupModal below — see
    // PendingComponentPopup for why this cannot happen inside ConstructAddComponentMenu itself.
    if (PendingComponentPopup)
    {
        ImGui::OpenPopup(PendingComponentPopup);
        PendingComponentPopup = nullptr;
    }
    ConstructVectorPopup();
    ConstructImagePopup();
    ImGui::SetCursorScreenPos(ImVec2(Origin.x + 8 * Scale, Origin.y + 64 * Scale));
    ImGui::BeginChild("Stamp layers body", ImVec2(Width - 16 * Scale, -45 * Scale), ImGuiChildFlags_None);
    size_t Visible = 0;
    for (const auto &Layer : Snapshot.Layers) if (Layer.Visible) ++Visible;
    const size_t Counts[]{Visible, Snapshot.Layers.size() - Visible};
    // .tp-layer-stats treatment: Texture Paint's two 66px filter cards (Visible / Hidden).
    {
        // Shared Visible/Hidden stat cards (PanelChrome::VisibilityStatCards).
        int ClickedCard = -1;
        PanelChrome::VisibilityStatCards(ImGui::GetCursorScreenPos(), (ImGui::GetContentRegionAvail().x - 20 * Scale) / 2.f, 6 * Scale, Scale,
            Counts[0], Counts[1], Filter == 1 ? 0 : Filter == 2 ? 1 : -1, ClickedCard, LightTypeface, nullptr, NoteExtent);
        if (ClickedCard >= 0) Filter = Filter == ClickedCard + 1 ? 0 : ClickedCard + 1;
    }
    // .tp-layer-search treatment: 36px pill, search glyph, frameless entry, clear icon.
    {
        const ImVec2 SearchOrigin = ImGui::GetCursorScreenPos();
        const float InnerWidth = ImGui::GetContentRegionAvail().x;
        const ImVec2 BoxMin(SearchOrigin.x + 6 * Scale, SearchOrigin.y + 12 * Scale), BoxMax(SearchOrigin.x + InnerWidth - 6 * Scale, SearchOrigin.y + 48 * Scale);
        auto *Ink = ImGui::GetWindowDrawList();
        Ink->AddRectFilled(BoxMin, BoxMax, Ink32(0x191919), 18 * Scale);
        Ink->AddRect(BoxMin, BoxMax, Ink32(0x2c2c2c), 18 * Scale);
        DrawGlyph("M11 4a7 7 0 1 0 0 14 7 7 0 0 0 0-14ZM21 21l-4.3-4.3", ImVec2(BoxMin.x + 11 * Scale, BoxMin.y + 11 * Scale), 14 * Scale, 0x77776f);
        ImGui::SetCursorScreenPos(ImVec2(BoxMin.x + 33 * Scale, BoxMin.y + 6 * Scale));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        ImGui::SetNextItemWidth(BoxMax.x - BoxMin.x - 66 * Scale);
        ImGui::InputText("##FindComponent", SearchText.data(), SearchText.size());
        const bool SearchActive = ImGui::IsItemActive();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        if (!SearchText[0] && !SearchActive)
            Caption(ImVec2(BoxMin.x + 37 * Scale, BoxMin.y + 12 * Scale), "Find a component...", 10 * Scale, 0x787871);
        if (SearchText[0])
        {
            ImGui::SetCursorScreenPos(ImVec2(BoxMax.x - 28 * Scale, BoxMin.y + 7 * Scale));
            if (BareIconButton("ClearComponentSearch", "M6 6L18 18M6 18L18 6", 22 * Scale, Scale, false, 0x8a8a80)) SearchText[0] = '\0';
        }
        ImGui::SetCursorScreenPos(ImVec2(SearchOrigin.x, SearchOrigin.y + 56 * Scale));
    }
    // .tp-layer-order treatment.
    {
        size_t Shown = 0;
        for (const auto &Layer : Snapshot.Layers)
        {
            if (Filter == 1 && !Layer.Visible) continue;
            if (Filter == 2 && Layer.Visible) continue;
            if (FoldCaption(Layer.Name).find(FoldCaption(SearchText.data())) == std::string::npos) continue;
            ++Shown;
        }
        const ImVec2 OrderOrigin = ImGui::GetCursorScreenPos();
        const float InnerWidth = ImGui::GetContentRegionAvail().x;
        Caption(ImVec2(OrderOrigin.x + 10 * Scale, OrderOrigin.y + 2 * Scale), "TOP -> BOTTOM", 8 * Scale, 0x686862);
        char ShownText[32];
        std::snprintf(ShownText, sizeof(ShownText), "%zu shown", Shown);
        Caption(ImVec2(OrderOrigin.x + InnerWidth - 10 * Scale - CaptionWidth(ShownText, 8 * Scale), OrderOrigin.y + 2 * Scale), ShownText, 8 * Scale, 0x55554f);
        ImGui::SetCursorScreenPos(ImVec2(OrderOrigin.x, OrderOrigin.y + 21 * Scale));
        ImGui::Dummy(ImVec2(0, 0));
    }
    const char *KindCaptions[]{"", "Text", "Imported vector", "Imported image"};
    for (auto Position = Snapshot.Layers.rbegin(); Position != Snapshot.Layers.rend(); ++Position)
    {
        const auto &Layer = *Position;
        if (Filter == 1 && !Layer.Visible) continue;
        if (Filter == 2 && Layer.Visible) continue;
        if (FoldCaption(Layer.Name).find(FoldCaption(SearchText.data())) == std::string::npos) continue;
        // 🎨 Row chrome mirrors Texture Paint's layer rows: 62px, hover/selected washes only, gold
        // accent bar, grip dots, 34px thumbnail chip, bare eye/lock glyphs at the right edge.
        const auto RowOrigin = ImGui::GetCursorScreenPos();
        const float RowX = RowOrigin.x + 2 * Scale;
        const float RowWidth = ImGui::GetContentRegionAvail().x - 4 * Scale;
        constexpr float RowHeightUnits = 62.f;
        const bool Selected = !MaskSelection && std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end();
        auto *Ink = ImGui::GetWindowDrawList();
        ImGui::PushID(int(Layer.Token));
        const std::string SelectCaption = "Select:" + std::to_string(Layer.Token);
        ImGui::SetCursorScreenPos(ImVec2(RowX + 20 * Scale, RowOrigin.y));
        const bool RowClicked = ImGui::InvisibleButton(SelectCaption.c_str(), ImVec2(std::max(30.f, RowWidth - 68 * Scale), RowHeightUnits * Scale));
        const bool RowHovered = ImGui::IsItemHovered();
        if (RowClicked) Select(Layer.Token, ImGui::GetIO().KeyShift);
        ControlExtents[SelectCaption] = {RowX, RowOrigin.y, RowX + RowWidth - 48 * Scale, RowOrigin.y + RowHeightUnits * Scale};
        if (Selected || RowHovered)
            Ink->AddRectFilled(ImVec2(RowX, RowOrigin.y), ImVec2(RowX + RowWidth, RowOrigin.y + RowHeightUnits * Scale), Ink32(Selected ? 0x262626 : 0x1b1b1b), 9 * Scale);
        if (Selected)
        {
            Ink->AddRect(ImVec2(RowX, RowOrigin.y), ImVec2(RowX + RowWidth, RowOrigin.y + RowHeightUnits * Scale), Ink32(0x3e3e3c), 9 * Scale);
            Ink->AddRectFilled(ImVec2(RowX, RowOrigin.y), ImVec2(RowX + 3 * Scale, RowOrigin.y + RowHeightUnits * Scale), Ink32(0xd9b281), 3 * Scale);
        }
        for (int DotY = 0; DotY < 3; ++DotY)
            for (int DotX = 0; DotX < 2; ++DotX)
                Ink->AddCircleFilled(ImVec2(RowX + (7 + DotX * 4) * Scale, RowOrigin.y + (25 + DotY * 6) * Scale), 1.1f * Scale, Ink32(0x55534a), 8);
        // 34px thumbnail chip: the component's own-bounds atlas raster once baked (square, centred).
        const ImVec2 ChipMin(RowX + 20 * Scale, RowOrigin.y + 14 * Scale), ChipMax(ChipMin.x + 34 * Scale, ChipMin.y + 34 * Scale);
        Ink->AddRectFilled(ChipMin, ChipMax, Ink32(0x161614), 9 * Scale);
        {
            StampAtlasImage LayerAtlas{};
            if (QueryLayerThumbnailAtlas) LayerAtlas = QueryLayerThumbnailAtlas();
            const size_t Slot = size_t(&Layer - Snapshot.Layers.data());
            bool ThumbnailDrawn = false;
            if (LayerAtlas.Texture != 0 && Slot < LayerAtlasCapacity && QueryLayerAtlasRevision() > 0)
            {
                const float Step = float(LayerAtlasTile + 2) / LayerAtlasExtent;
                const float Span = float(LayerAtlasTile) / LayerAtlasExtent;
                const float U0 = LayerAtlas.MinimumU + (Slot % LayerAtlasColumns) * Step;
                const float V0 = LayerAtlas.MinimumV + (Slot / LayerAtlasColumns) * Step;
                Ink->AddImageRounded(ImTextureRef(LayerAtlas.Texture), ChipMin, ChipMax, ImVec2(U0, V0),
                    ImVec2(U0 + Span, V0 + Span), IM_COL32_WHITE, 9 * Scale);
                ThumbnailDrawn = true;
            }
            if (!ThumbnailDrawn)
                Ink->AddRect(ImVec2(ChipMin.x + 10 * Scale, ChipMin.y + 10 * Scale), ImVec2(ChipMin.x + 24 * Scale, ChipMin.y + 24 * Scale),
                    ImGui::GetColorU32(Spectrum(0xc7d9b8)), 2 * Scale);
        }
        const auto &Source = Snapshot.Instructions[Layer.Root];
        Ink->PushClipRect(ImVec2(ChipMax.x + 7 * Scale, RowOrigin.y), ImVec2(RowX + RowWidth - 52 * Scale, RowOrigin.y + RowHeightUnits * Scale), true);
        Caption(ImVec2(ChipMax.x + 7 * Scale, RowOrigin.y + 15 * Scale), Layer.Name.c_str(), 11 * Scale,
            !Layer.Visible ? 0x6a6a62 : Selected ? 0xeeeee8 : 0xc0c0b8);
        const char *Kind = Layer.ComponentKind != VectorAuthoringComponentKind::Freeform ? KindCaptions[uint32_t(Layer.ComponentKind)]
            : Source.Topology[0] < 6 ? ShapeCaptions[Source.Topology[0]] : "Retained vector";
        char Sub[96];
        std::snprintf(Sub, sizeof(Sub), "%s · %s%s%s", Kind, Layer.Visible ? "Visible" : "Hidden",
            Layer.Locked ? " · Locked" : "", Layer.Mask ? " · Mask" : "");
        Caption(ImVec2(ChipMax.x + 7 * Scale, RowOrigin.y + 34 * Scale), Sub, 8 * Scale, Selected ? 0xa29c90 : 0x696964);
        Ink->PopClipRect();
        if (!Layer.Locked && ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("FrontierStampComponentLayer", &Layer.Token, sizeof(Layer.Token));
            ImGui::TextUnformatted(Layer.Name.c_str());
            ImGui::EndDragDropSource();
        }
        if (!Layer.Locked && ImGui::BeginDragDropTarget())
        {
            if (const auto *Payload = ImGui::AcceptDragDropPayload("FrontierStampComponentLayer"); Payload && Payload->IsDelivery())
            {
                const auto SourceToken = *static_cast<const uint32_t *>(Payload->Data);
                EvaluateEdit([&](auto &Document) { ReorderVectorAuthoringLayerByDrag(Document, SourceToken, Layer.Token); });
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SetCursorScreenPos(ImVec2(RowX + RowWidth - 46 * Scale, RowOrigin.y + 19 * Scale));
        const std::string VisibilityCaption = "Visibility:" + std::to_string(Layer.Token);
        const bool ToggleVisible = BareIconButton(VisibilityCaption.c_str(), Layer.Visible ? VisiblePath : HiddenPath, 20 * Scale, Scale, false, 0x8a8a80);
        NoteExtent(VisibilityCaption);
        if (ToggleVisible) EvaluateEdit([&](auto &Document) { for (auto &Candidate : Document.Layers) if (Candidate.Token == Layer.Token) Candidate.Visible = !Candidate.Visible; });
        ImGui::SetCursorScreenPos(ImVec2(RowX + RowWidth - 24 * Scale, RowOrigin.y + 19 * Scale));
        const std::string LockCaption = "Lock:" + std::to_string(Layer.Token);
        const bool ToggleLock = BareIconButton(LockCaption.c_str(), LockPath, 20 * Scale, Scale, Layer.Locked, Layer.Locked ? 0xbaa789 : 0x8a8a80);
        NoteExtent(LockCaption);
        if (ToggleLock) EvaluateEdit([&](auto &Document) { for (auto &Candidate : Document.Layers) if (Candidate.Token == Layer.Token) Candidate.Locked = !Candidate.Locked; });
        ImGui::SetCursorScreenPos(ImVec2(RowOrigin.x, RowOrigin.y + (RowHeightUnits + 2) * Scale));
        ImGui::Dummy(ImVec2(0, 0));
        if (Layer.Mask)
        {
            const ImVec2 MaskOrigin = ImGui::GetCursorScreenPos();
            const float MaskX = MaskOrigin.x + 27 * Scale;
            const float MaskWidth = ImGui::GetContentRegionAvail().x - 29 * Scale;
            const bool MaskSelectedRow = MaskSelection == Layer.Token;
            ImGui::SetCursorScreenPos(ImVec2(MaskX, MaskOrigin.y));
            const bool MaskClicked = ImGui::InvisibleButton("##MaskSelect", ImVec2(MaskWidth, 43 * Scale));
            NoteExtent("Mask · " + Layer.Name);
            if (MaskClicked) { Selection = {Layer.Token}; MaskSelection = Layer.Token; }
            if (MaskSelectedRow || ImGui::IsItemHovered())
                Ink->AddRectFilled(ImVec2(MaskX, MaskOrigin.y), ImVec2(MaskX + MaskWidth, MaskOrigin.y + 43 * Scale), Ink32(MaskSelectedRow ? 0x262626 : 0x1b1b1b), 8 * Scale);
            if (MaskSelectedRow)
            {
                Ink->AddRect(ImVec2(MaskX, MaskOrigin.y), ImVec2(MaskX + MaskWidth, MaskOrigin.y + 43 * Scale), Ink32(0x3c3c37), 8 * Scale);
                Ink->AddRectFilled(ImVec2(MaskX, MaskOrigin.y), ImVec2(MaskX + 3 * Scale, MaskOrigin.y + 43 * Scale), Ink32(0xd9b281), 3 * Scale);
            }
            const ImVec2 MaskChip(MaskX + 8 * Scale, MaskOrigin.y + 11.5f * Scale);
            Ink->AddRectFilled(MaskChip, ImVec2(MaskChip.x + 20 * Scale, MaskChip.y + 20 * Scale), Ink32(0x0c0c0c), 5 * Scale);
            Ink->AddRect(MaskChip, ImVec2(MaskChip.x + 20 * Scale, MaskChip.y + 20 * Scale), Ink32(0x595954), 5 * Scale);
            Ink->AddRectFilled(ImVec2(MaskChip.x + 2 * Scale, MaskChip.y + 2 * Scale), ImVec2(MaskChip.x + 18 * Scale, MaskChip.y + 18 * Scale), Ink32(0xe6e6df), 2 * Scale);
            Caption(ImVec2(MaskChip.x + 29 * Scale, MaskOrigin.y + 8 * Scale), "Layer mask", 10 * Scale, 0xc8c8ba);
            Caption(ImVec2(MaskChip.x + 29 * Scale, MaskOrigin.y + 25 * Scale), Layer.MaskEnabled ? "Owned mask" : "Owned mask · off", 8 * Scale, 0x777770);
            ImGui::SetCursorScreenPos(ImVec2(MaskOrigin.x, MaskOrigin.y + 45 * Scale));
            ImGui::Dummy(ImVec2(0, 0));
        }
        ImGui::PopID();
    }
    Controls.ControlHeight = 30;
    if (Snapshot.Layers.empty()) ImGui::TextWrapped("Add your first component to start a decal.");
    ImGui::EndChild();
    // .tp-pane-footer treatment: stack details on the left, selection controls on the right.
    {
        const ImVec2 FooterOrigin(Origin.x, ImGui::GetCursorScreenPos().y);
        auto *Ink = ImGui::GetWindowDrawList();
        Ink->AddRectFilled(FooterOrigin, ImVec2(Origin.x + Width, FooterOrigin.y + 45 * Scale), Ink32(0x151515));
        Ink->AddLine(FooterOrigin, ImVec2(Origin.x + Width, FooterOrigin.y), Ink32(0xffffff, .02f));
        const auto Stat = [&](float X, const char *Label, const std::string &Value, uint32_t ValueColour)
        {
            Caption(ImVec2(Origin.x + X, FooterOrigin.y + 9 * Scale), Label, 7 * Scale, 0x5f5f58);
            Caption(ImVec2(Origin.x + X, FooterOrigin.y + 23 * Scale), Value.c_str(), 9 * Scale, ValueColour);
        };
        Stat(12 * Scale, "COMPONENTS", std::to_string(Snapshot.Layers.size()), 0x8eb896);
        Stat(72 * Scale, "VISIBLE", std::to_string(Visible), 0xa6a699);
        Stat(122 * Scale, "SELECTED", std::to_string(Selection.size()), 0xa6a699);
        const bool FooterEditable = !Selection.empty() && !MaskSelection && !PenSource && Gesture == 0 &&
            std::none_of(Snapshot.Layers.begin(), Snapshot.Layers.end(), [&](const auto &Layer)
            {
                return std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end() &&
                    (QueryVectorAuthoringProtection(Snapshot, Layer) || !QueryVectorAuthoringVisibility(Snapshot, Layer));
            });
        float ButtonX = Origin.x + Width - 112 * Scale;
        const float ButtonY = FooterOrigin.y + 11.5f * Scale;
        ImGui::BeginDisabled(!FooterEditable);
        ImGui::SetCursorScreenPos(ImVec2(ButtonX, ButtonY));
        if (BareIconButton("LayerFooterDuplicate", CopyPath, 22 * Scale, Scale, false, 0x8a8a80)) ReplicateSelection();
        NoteExtent("Layer footer duplicate");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Duplicate selection");
        ButtonX += 26 * Scale;
        ImGui::SetCursorScreenPos(ImVec2(ButtonX, ButtonY));
        if (BareIconButton("LayerFooterBringForward", "M12 19V5M6 11L12 5L18 11", 22 * Scale, Scale, false, 0x8a8a80))
            EvaluateEdit([&](auto &Document) { ReorderVectorAuthoringSelection(Document, Selection, VectorSelectionOrder::Forward); });
        NoteExtent("Layer footer bring forward");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Bring selection forward");
        ButtonX += 26 * Scale;
        ImGui::SetCursorScreenPos(ImVec2(ButtonX, ButtonY));
        if (BareIconButton("LayerFooterSendBackward", "M12 5V19M6 13L12 19L18 13", 22 * Scale, Scale, false, 0x8a8a80))
            EvaluateEdit([&](auto &Document) { ReorderVectorAuthoringSelection(Document, Selection, VectorSelectionOrder::Backward); });
        NoteExtent("Layer footer send backward");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Send selection backward");
        ButtonX += 26 * Scale;
        ImGui::SetCursorScreenPos(ImVec2(ButtonX, ButtonY));
        if (BareIconButton("LayerFooterDelete", TrashPath, 22 * Scale, Scale, false, 0x8a8a80))
            EvaluateEdit([&](auto &Document) { PartitionVectorAuthoringSelection(Document, Selection); });
        NoteExtent("Layer footer delete");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Delete selection");
        ImGui::EndDisabled();
        ImGui::SetCursorScreenPos(ImVec2(FooterOrigin.x, FooterOrigin.y + 45 * Scale));
        ImGui::Dummy(ImVec2(0, 0));
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     GPU CANVAS, VIEW NAVIGATION AND GESTURES
//------------------------------------------------------------------------------------------------------------------------

void StampWorkspacePanel::ConstructCanvas()
{
    CapturePanel("Viewport");
    const auto ContentOrigin = ImGui::GetCursorScreenPos();
    const float Width = ImGui::GetContentRegionAvail().x;
    const float AvailableHeight = ImGui::GetContentRegionAvail().y;
    const auto At = [&](float X, float Y) { return ImVec2(ContentOrigin.x + X * Scale, ContentOrigin.y + Y * Scale); };
    // Slim document row (overline + compact rename field + saved state): the former Design/3D mode
    // buttons, strapline and 20pt name heading are gone so the canvas owns the viewport height.
    const char *Overline = MaskSelection ? "OWNED MASK / 512 UNITS" : "DECAL CANVAS / 512 UNITS";
    Caption(At(24, 16), Overline, 8 * Scale, 0x8d8d7b);
    auto Name = QueryDocument().Name;
    std::snprintf(NameText.data(), NameText.size(), "%s", Name.c_str());
    ImGui::SetCursorScreenPos(At(24 + CaptionWidth(Overline, 8 * Scale) / Scale + 12, 6));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushFont(static_cast<ImFont *>(LightTypeface ? LightTypeface : RegularTypeface), 11 * 1.302f * Scale);
    ImGui::SetNextItemWidth(220 * Scale);
    if (ImGui::InputText("##StampName", NameText.data(), NameText.size())) EvaluateEdit([&](auto &Document) { Document.Name = NameText.data(); });
    ImGui::PopFont(); ImGui::PopStyleColor();
    Controls.Tooltip("Stamp name", "Names the saved document and its stamp-library PNG.");
    Caption(At(Width / Scale - 132, 16), QueryModified() ? "Unsaved changes" : "Document saved", 9 * Scale, 0x969b81);
    auto *Ink = ImGui::GetWindowDrawList();
    Ink->AddRectFilled(At(24, 44), At(Width / Scale - 24, 94), ImGui::GetColorU32(Spectrum(0x191919)), 8 * Scale);
    ImGui::SetCursorScreenPos(At(32, 53));
    Controls.ControlHeight = 32;
    if (Controls.Button("Select tool (V)", 32 * Scale, Tool == VectorAuthoringTool::Select, SelectPath)) Tool = VectorAuthoringTool::Select;
    Controls.Tooltip("Select", "Select visible GPU contours; Shift-click to extend. Drag once for one undoable source edit.");
    ImGui::SameLine(0, 4 * Scale);
    if (Controls.Button("Point tool (A)", 32 * Scale, Tool == VectorAuthoringTool::Points, "M4 3L11 21L14 14L21 11Z")) Tool = VectorAuthoringTool::Points;
    Controls.Tooltip("Point editing", "Select one non-Boolean source (a primitive is converted to an editable path on first drag). Click a segment to split; drag it to bend with fixed anchors. Drag anchors/tangents; Alt moves tangents independently. Escape cancels.");
    for (uint32_t Category = 0; Category < 2; ++Category)
    {
        ImGui::SameLine(0, 4 * Scale);
        if (Controls.Button(Category ? "Ellipse tool (O)" : "Rectangle tool (R)", 32 * Scale, Tool == (Category ? VectorAuthoringTool::Ellipse : VectorAuthoringTool::Rectangle), ShapePaths[Category])) Tool = Category ? VectorAuthoringTool::Ellipse : VectorAuthoringTool::Rectangle;
        Controls.Tooltip(ShapeCaptions[Category], "Drag on the canvas to author an editable source shape.");
    }
    // Polygon + Star are first-class toolbar tools (they used to hide in "More tools").
    for (uint32_t Category = 4; Category < 6; ++Category)
    {
        ImGui::SameLine(0, 4 * Scale);
        const VectorAuthoringTool ShapeTool = VectorAuthoringTool(uint32_t(VectorAuthoringTool::Diamond) + Category - 2);
        if (Controls.Button(Category == 4 ? "Polygon tool" : "Star tool", 32 * Scale, Tool == ShapeTool, ShapePaths[Category])) Tool = ShapeTool;
        const auto ItemMinimum = ImGui::GetItemRectMin(), ItemMaximum = ImGui::GetItemRectMax();
        ControlExtents[ShapeCaptions[Category]] = {ItemMinimum.x, ItemMinimum.y, ItemMaximum.x, ItemMaximum.y};
        Controls.Tooltip(ShapeCaptions[Category], "Drag on the canvas to author an editable source shape.");
    }
    ImGui::SameLine(0, 4 * Scale);
    // 📝 Text is a real drag-to-draw tool like Rectangle/Ellipse, not a modal trigger: drag a box on
    // the canvas (a plain click uses a sensible default box), then write directly into it — see the
    // Gesture == 2 (drawing the box) / Gesture == 8 (composing text, ConstructTextComposer) handling
    // further down in this function.
    const bool AtComponentCap = QueryDocument().Layers.size() >= 24;
    ImGui::BeginDisabled(AtComponentCap);
    if (Controls.Button("Text tool (T)", 32 * Scale, Tool == VectorAuthoringTool::Text, TextIconPath)) Tool = VectorAuthoringTool::Text;
    ImGui::EndDisabled();
    Controls.Tooltip("Text", AtComponentCap ? "Stamp decals are capped at 24 components (Pattern allows 64)." : "Drag a box on the canvas and write directly into it. Real glyph-outline geometry, read from the bundled DM Sans font.");
    ImGui::SameLine(0, 4 * Scale);
    if (Controls.Button("Pen tool (P)", 32 * Scale, Tool == VectorAuthoringTool::Pen, "M12 3L21 12L15 21L3 9ZM3 9L12 12L15 21M12 12V3")) Tool = VectorAuthoringTool::Pen;
    Controls.Tooltip("Pen", "Click anchors; drag tangents for curves. Hold Space during placement to reposition the new anchor and controls. Alt-click the last pending anchor to break its outgoing tangent; drag independently. Click an open endpoint to continue; while continuing, click another path endpoint to join. Ctrl bypasses endpoint joining or starts a separate path. Shift constrains direction; Alt moves independently. First anchor closes; Enter finishes; Escape cancels.");
    ImGui::SameLine(0, 4 * Scale);
    if (Controls.Button("Polyline tool (L)", 32 * Scale, Tool == VectorAuthoringTool::Polyline, "M4 19L9 7L15 16L20 5")) Tool = VectorAuthoringTool::Polyline;
    Controls.Tooltip("Polyline", "Same as Pen, but every anchor stays a sharp corner - straight segments only, no curve tangents. Click the first anchor again to close it into a filled shape; Enter finishes it open as a plain line. Shift constrains direction; Enter finishes; Escape cancels.");
    ImGui::SameLine(0, 4 * Scale);
    if (Controls.Button("Pan tool", 32 * Scale, Tool == VectorAuthoringTool::Pan, "M7 12V6Q8 3 9 6V11V4Q11 1 12 4V11V5Q15 2 15 5V12V8Q18 5 18 8V16Q16 23 10 21L3 14Q2 10 5 12L7 14")) Tool = VectorAuthoringTool::Pan;
    ImGui::SameLine(0, 4 * Scale);
    if (Controls.Button("More tools", 78 * Scale)) ImGui::OpenPopup("Stamp more tools");
    if (DropdownControlSurface::BeginMenu("Stamp more tools", Scale))
    {
        for (uint32_t Category = 2; Category < 4; ++Category)
        {
            if (DropdownControlSurface::Option(ShapeCaptions[Category], false, Scale)) { Tool = VectorAuthoringTool(uint32_t(VectorAuthoringTool::Diamond) + Category - 2); ImGui::CloseCurrentPopup(); }
            const auto Minimum = ImGui::GetItemRectMin(), Maximum = ImGui::GetItemRectMax();
            ControlExtents[ShapeCaptions[Category]] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
        }
        Controls.Toggle("Transparent canvas", TransparentCanvas);
        Controls.Toggle("Show grid", GridEnabled);
        Controls.Toggle("Snap to grid", SnappingEnabled);
        DropdownControlSurface::EndMenu();
    }
    Controls.ControlHeight = 24;
    Ink->AddRectFilled(At(24, 94), At(Width / Scale - 24, 132), ImGui::GetColorU32(ThemeQuad(ControlKit::Palette().Raised)));
    Caption(At(36, 109), "VECTOR CANVAS    512 \u00d7 512", 8 * Scale, 0x888888);
    ImGui::SetCursorScreenPos(At(Width / Scale - 219, 101));
    if (Controls.Button("Zoom out", 26 * Scale, false, "M5 12H19")) Zoom = ApplyWorkspaceZoomStep(Zoom, 1.f / 1.25f);
    ImGui::SameLine(0, 4 * Scale);
    char ZoomText[32]; std::snprintf(ZoomText, sizeof(ZoomText), "%.0f%%", Zoom * 100);
    Controls.Button(ZoomText, 62 * Scale, false, {}, true);
    ImGui::SameLine(0, 4 * Scale);
    // 📝 Raised from 8x (800%) to 10x (1000%): the bake ceiling both canvases clamp their
    // on-screen device-pixel request to is 4096, so even a fit-sized board at 1000% zoom just
    // clamps its coverage request to that ceiling instead of growing unbounded.
    if (Controls.Button("Zoom in", 26 * Scale, false, PlusPath)) Zoom = ApplyWorkspaceZoomStep(Zoom, 1.25f);
    ImGui::SameLine(0, 4 * Scale);
    if (Controls.Button("Fit", 46 * Scale, false, {}, true)) { Zoom = 1; Pan = {}; }
    Controls.ControlHeight = 30;
    // The canvas owns whatever height the dock gives it (minus the hint bar): at Zoom 1 ("Fit") the
    // board fills the region's short side, grows/shrinks with the window, and the panel never scrolls.
    const ImVec2 Minimum = At(25, 131);
    const ImVec2 Maximum(At(Width / Scale - 25, 0).x, std::max(Minimum.y + 120 * Scale, ContentOrigin.y + AvailableHeight - 41 * Scale));
    Ink->AddRectFilled(Minimum, Maximum, ImGui::GetColorU32(ThemeQuad(ControlKit::Palette().Inset)));
    const float FitSize = std::max(32.f, std::min(Maximum.x - Minimum.x, Maximum.y - Minimum.y) - 56 * Scale);
    const float BoardSize = FitSize * Zoom;
    const ImVec2 Centre((Minimum.x + Maximum.x) * .5f + Pan.X, (Minimum.y + Maximum.y) * .5f + Pan.Y);
    const ImVec2 BoardMinimum(Centre.x - BoardSize * .5f, Centre.y - BoardSize * .5f), BoardMaximum(Centre.x + BoardSize * .5f, Centre.y + BoardSize * .5f);
    BoardExtent = {BoardMinimum.x, BoardMinimum.y, BoardMaximum.x, BoardMaximum.y};
    Ink->PushClipRect(Minimum, Maximum, true);
    Ink->AddRectFilled(BoardMinimum, BoardMaximum, ImGui::GetColorU32(Spectrum(0xede7d7)));
    if (TransparentCanvas)
    {
        for (float Y = BoardMinimum.y; Y < BoardMaximum.y; Y += 12 * Scale)
            for (float X = BoardMinimum.x; X < BoardMaximum.x; X += 12 * Scale)
                if ((int((X - BoardMinimum.x) / (12 * Scale)) + int((Y - BoardMinimum.y) / (12 * Scale))) % 2)
                    Ink->AddRectFilled(ImVec2(X, Y), ImVec2(std::min(X + 12 * Scale, BoardMaximum.x), std::min(Y + 12 * Scale, BoardMaximum.y)), ImGui::GetColorU32(Spectrum(0xb5b8ae)));
    }
    // 📏 The ruler/grid used to step through content units in a fixed 64-unit stride. That reads fine
    // at 100% zoom, but at high zoom each 64-unit cell spans hundreds of screen pixels, so once panned
    // even slightly, the whole -256..768 sweep can land zero ruler ticks (and zero interior grid lines,
    // further down) inside the currently-visible window — the grid appears to "vanish" and whatever's
    // drawn on the board (shapes, the baked texture) reads as sitting on top of nothing. Deriving the
    // step from the live pixels-per-unit density keeps a tick/line on screen at every zoom level.
    float GridStepUnits = 64.0f;
    const float MaxRulerPixelSpacing = 140.0f * Scale, MinRulerPixelSpacing = 28.0f * Scale;
    while (GridStepUnits / 512.0f * BoardSize > MaxRulerPixelSpacing && GridStepUnits > 4.0f) GridStepUnits *= 0.5f;
    while (GridStepUnits / 512.0f * BoardSize < MinRulerPixelSpacing && GridStepUnits < 512.0f) GridStepUnits *= 2.0f;
    const auto &Snapshot = QueryDocument();
    const uint64_t HistoryRevision = History.QueryRevision();
    if (!CachedCoverageValid || CachedCoverageMask != MaskSelection || CachedHistoryRevision != HistoryRevision ||
        CachedGestureRevision != GestureRevision)
    {
        // 📝 Future-proofing: evaluate any modifier stack (e.g. Array) before rendering, same as Pattern -
        // currently always a no-op here since Stamp documents never populate Layer.Modifiers.
        CachedCoverageProgram = ConstructVectorAuthoringCoverage(ConstructEvaluatedVectorAuthoringDocument(Snapshot));
        if (MaskSelection)
        {
            const auto Position = std::find_if(Snapshot.Layers.begin(), Snapshot.Layers.end(), [&](const auto &Layer) { return Layer.Token == MaskSelection; });
            if (Position != Snapshot.Layers.end() && Position->Mask)
                CachedCoverageProgram = {{*Position->Mask}, Snapshot.Segments, 0};
        }
        CachedCoverageMask = MaskSelection;
        CachedHistoryRevision = HistoryRevision;
        CachedGestureRevision = GestureRevision;
        CachedCoverageValid = true;
    }
    const float DisplayPixels = BoardSize * ImGui::GetIO().DisplayFramebufferScale.x;
    const uint32_t RequestedExtent = uint32_t(std::clamp(std::ceil(DisplayPixels), 1.f, 4096.f));
    const auto Texture = QueryCoverage ? QueryCoverage(CachedCoverageProgram, RequestedExtent) : 0;
    if (Texture) Ink->AddImage(ImTextureRef(Texture), BoardMinimum, BoardMaximum);
    else Caption(ImVec2(BoardMinimum.x + 12 * Scale, BoardMinimum.y + 12 * Scale), "GPU coverage unavailable", 11 * Scale, 0x68695a);
    const bool BakePending = Texture && QueryCoverageReady && !QueryCoverageReady();
    if (BakePending) { if (PendingBakeSince < 0) PendingBakeSince = ImGui::GetTime(); }
    else PendingBakeSince = -1;
    if (BakePending && PendingBakeSince >= 0 && ImGui::GetTime() - PendingBakeSince > 0.15)
    {
        const char *Badge = "Baking decal...";
        const ImVec2 BadgeExtent = ImGui::CalcTextSize(Badge);
        const ImVec2 BadgeMaximum(BoardMaximum.x - 10 * Scale, BoardMinimum.y + 10 * Scale + BadgeExtent.y + 10 * Scale);
        const ImVec2 BadgeMinimum(BadgeMaximum.x - BadgeExtent.x - 20 * Scale, BoardMinimum.y + 10 * Scale);
        Ink->AddRectFilled(BadgeMinimum, BadgeMaximum, IM_COL32(26, 30, 24, 225), 6 * Scale);
        Ink->AddRect(BadgeMinimum, BadgeMaximum, IM_COL32(191, 216, 169, 255), 6 * Scale);
        Ink->AddText(ImVec2(BadgeMinimum.x + 10 * Scale, BadgeMinimum.y + 5 * Scale), IM_COL32(212, 232, 192, 255), Badge);
    }
    // 🧱 Drawn after the coverage/shape image above (and after the "Baking..." badge), so the grid is
    // always the topmost layer on the board — shapes can never paint over it. Reuses GridStepUnits
    // (recomputed above from the live zoom) instead of a fixed 64-unit stride, so there's always a
    // visible line on screen no matter how far in the user has zoomed.
    if (GridEnabled)
    {
        for (float Coordinate = 0; Coordinate <= 512; Coordinate += GridStepUnits)
        {
            const float Pixel = Coordinate / 512 * BoardSize;
            Ink->AddLine(ImVec2(BoardMinimum.x + Pixel, BoardMinimum.y), ImVec2(BoardMinimum.x + Pixel, BoardMaximum.y), IM_COL32(80, 80, 80, 75));
            Ink->AddLine(ImVec2(BoardMinimum.x, BoardMinimum.y + Pixel), ImVec2(BoardMaximum.x, BoardMinimum.y + Pixel), IM_COL32(80, 80, 80, 75));
        }
    }
    // 🧱 The top/left ruler strips (and their tick marks) are fixed UI chrome anchored to the
    // viewport's own edges, not to the board — but they used to be drawn before the coverage image,
    // so once the board was zoomed in past the point where it fully covers the visible viewport (any
    // zoom level much past 100%, depending on pan), the baked shape painted straight over them and they
    // read as having "disappeared". Drawing them last, after the shape and the interior grid, keeps
    // them visible at every zoom level, exactly like the interior grid above.
    Ink->AddRectFilled(Minimum, ImVec2(Maximum.x, Minimum.y + 21 * Scale), ImGui::GetColorU32(ThemeQuad(ControlKit::Palette().Panel)));
    Ink->AddRectFilled(Minimum, ImVec2(Minimum.x + 23 * Scale, Maximum.y), ImGui::GetColorU32(ThemeQuad(ControlKit::Palette().Panel)));
    const int GridStep = std::max(1, int(GridStepUnits));
    for (int Coordinate = -256; Coordinate <= 768; Coordinate += GridStep)
    {
        const float Horizontal = BoardMinimum.x + float(Coordinate) / 512 * BoardSize;
        const float Vertical = BoardMinimum.y + float(Coordinate) / 512 * BoardSize;
        char Label[16]; std::snprintf(Label, sizeof(Label), "%d", Coordinate);
        if (Horizontal > Minimum.x + 24 * Scale && Horizontal < Maximum.x)
        {
            Ink->AddLine(ImVec2(Horizontal, Minimum.y), ImVec2(Horizontal, Minimum.y + 20 * Scale), IM_COL32(136, 136, 136, 190));
            Caption(ImVec2(Horizontal + 4 * Scale, Minimum.y + 4 * Scale), Label, 7 * Scale, 0x888888);
        }
        if (Vertical > Minimum.y + 22 * Scale && Vertical < Maximum.y)
        {
            Ink->AddLine(ImVec2(Minimum.x, Vertical), ImVec2(Minimum.x + 22 * Scale, Vertical), IM_COL32(136, 136, 136, 190));
            Caption(ImVec2(Minimum.x + 3 * Scale, Vertical + 3 * Scale), Label, 7 * Scale, 0x888888);
        }
    }
    std::array<int, 2> HoveredResize{};
    bool HoveredRotation = false;
    int GradientHoverSlot = -1;
    float GradientLineHoverT = -1;
    int GradientAxisHoverEnd = -1;
    if (!Selection.empty() && !MaskSelection)
    {
        try
        {
            const auto Extent = QueryVectorSelectionExtent(Snapshot, Selection);
            const auto &Bounds = Extent.Bounds;
            const auto ScreenPoint = [&](float X, float Y)
            {
                const float θ = Extent.Rotation;
                return ImVec2(BoardMinimum.x + (Bounds[0] + X * std::cos(θ) - Y * std::sin(θ)) / 512 * BoardSize,
                              BoardMinimum.y + (Bounds[1] + X * std::sin(θ) + Y * std::cos(θ)) / 512 * BoardSize);
            };
            const std::array<ImVec2, 4> Corners{ScreenPoint(-Bounds[2] * .5f, -Bounds[3] * .5f),
                ScreenPoint(Bounds[2] * .5f, -Bounds[3] * .5f), ScreenPoint(Bounds[2] * .5f, Bounds[3] * .5f),
                ScreenPoint(-Bounds[2] * .5f, Bounds[3] * .5f)};
            Ink->AddPolyline(Corners.data(), 4, IM_COL32(191, 216, 169, 255), ImDrawFlags_Closed, 1.3f * Scale);
            bool Editable = true;
            for (const auto &Layer : Snapshot.Layers)
                if (std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end() &&
                    (QueryVectorAuthoringProtection(Snapshot, Layer) || !QueryVectorAuthoringVisibility(Snapshot, Layer))) Editable = false;
            if (Editable)
            {
                if (Tool == VectorAuthoringTool::Select)
                {
                    const auto RegisterGrip = [&](const char *Name, ImVec2 Point)
                    {
                        const float Radius = 6 * Scale;
                        ControlExtents[Name] = {Point.x - Radius, Point.y - Radius, Point.x + Radius, Point.y + Radius};
                        const auto Mouse = ImGui::GetIO().MousePos;
                        return std::abs(Mouse.x - Point.x) <= Radius && std::abs(Mouse.y - Point.y) <= Radius;
                    };
                    const ImVec2 Rotate = ScreenPoint(0, -Bounds[3] * .5f - 29 * Scale * 512 / BoardSize);
                    Ink->AddLine(ScreenPoint(0, -Bounds[3] * .5f), Rotate, IM_COL32(191, 216, 169, 255));
                    Ink->AddCircleFilled(Rotate, 4 * Scale, IM_COL32(212, 232, 192, 255));
                    Ink->AddCircle(Rotate, 4 * Scale, IM_COL32(40, 56, 43, 255));
                    HoveredRotation = RegisterGrip("Rotate selection", Rotate);
                    for (int Y = -1; Y <= 1; ++Y) for (int X = -1; X <= 1; ++X)
                    {
                        if (!X && !Y) continue;
                        const ImVec2 Point = ScreenPoint(float(X) * Bounds[2] * .5f, float(Y) * Bounds[3] * .5f);
                        Ink->AddRectFilled(ImVec2(Point.x - 3 * Scale, Point.y - 3 * Scale), ImVec2(Point.x + 3 * Scale, Point.y + 3 * Scale), IM_COL32(203, 225, 184, 255), Scale);
                        Ink->AddRect(ImVec2(Point.x - 3 * Scale, Point.y - 3 * Scale), ImVec2(Point.x + 3 * Scale, Point.y + 3 * Scale), IM_COL32(40, 60, 40, 255), Scale);
                        const std::string Name = std::string("Resize:") + (Y < 0 ? "n" : Y > 0 ? "s" : "") + (X < 0 ? "w" : X > 0 ? "e" : "");
                        if (RegisterGrip(Name.c_str(), Point)) HoveredResize = {X, Y};
                    }
                }
                // 🎨 Interactive gradient colour-stop markers: only for a single selected shape whose fill
                // is a gradient. Lets the ramp be edited directly on the artwork — click the axis line to
                // drop a new stop where you clicked, drag an existing marker to retime it, click one to
                // select it for colour-editing in the Inspector's ramp editor (kept in sync both ways).
                // Shown under EVERY tool, not just Select: the natural flow is "make text, give it a
                // gradient in the Inspector, drag its points" without first switching tools - gating this
                // on Select (and drawing it plain white on the pale board) made the points undiscoverable.
                // The click dispatch below already gives these grips priority over tool actions.
                if (Selection.size() == 1)
                {
                    const auto GradientLayer = std::find_if(Snapshot.Layers.begin(), Snapshot.Layers.end(), [&](const auto &Layer) { return Layer.Token == Selection[0]; });
                    if (GradientLayer != Snapshot.Layers.end() && GradientLayer->Root < Snapshot.Instructions.size())
                    {
                        const auto &GradientSource = Snapshot.Instructions[GradientLayer->Root];
                        if (uint32_t(GradientSource.Transform[3]) == 1 || uint32_t(GradientSource.Transform[3]) == 2)
                        {
                            const auto Axis = QueryGradientAxisEndpoints(GradientSource, Bounds, Extent.Rotation);
                            const ImVec2 AxisStart = ScreenPoint(Axis[0].X - Bounds[0], Axis[0].Y - Bounds[1]);
                            const ImVec2 AxisEnd = ScreenPoint(Axis[1].X - Bounds[0], Axis[1].Y - Bounds[1]);
                            // Dual-tone (dark halo under a white core) so the axis reads on the pale
                            // board, over white gradient stops and over dark ink alike; grips below match.
                            Ink->AddLine(AxisStart, AxisEnd, IM_COL32(30, 34, 30, 200), 3.6f * Scale);
                            Ink->AddLine(AxisStart, AxisEnd, IM_COL32(255, 255, 255, 230), 1.6f * Scale);
                            // 🎨 Two square "axis" handles at the ramp's own A/B control points (see GradientAxis
                            // on VectorCoverageInstruction) — distinct from the round colour-stop markers below,
                            // which sit ON this line at their own Fraction. Dragging a square repositions the
                            // whole line/radius (Gesture 10/11); dragging a circle only retimes that stop's
                            // Fraction along whatever line is already there (Gesture 9). A fresh gradient's
                            // first/last stops sit exactly at A and B, so each square is drawn and hit-tested a
                            // fixed few pixels further out along the line's own direction - just past its
                            // coincident stop circle, like a small flag past the end of the line - rather than
                            // directly on top of it, so both stay independently grabbable even when stacked.
                            const auto Mouse = ImGui::GetIO().MousePos;
                            const ImVec2 LineDelta(AxisEnd.x - AxisStart.x, AxisEnd.y - AxisStart.y);
                            const float LineLength = std::hypot(LineDelta.x, LineDelta.y);
                            const ImVec2 LineDirection = LineLength > 1e-4f ? ImVec2(LineDelta.x / LineLength, LineDelta.y / LineLength) : ImVec2(1, 0);
                            constexpr float HandleStandoff = 11.f;
                            const ImVec2 AxisHandleA(AxisStart.x - LineDirection.x * HandleStandoff * Scale, AxisStart.y - LineDirection.y * HandleStandoff * Scale);
                            const ImVec2 AxisHandleB(AxisEnd.x + LineDirection.x * HandleStandoff * Scale, AxisEnd.y + LineDirection.y * HandleStandoff * Scale);
                            for (int End = 0; End < 2; ++End)
                            {
                                const ImVec2 &LineEnd = End == 0 ? AxisStart : AxisEnd;
                                const ImVec2 &Point = End == 0 ? AxisHandleA : AxisHandleB;
                                Ink->AddLine(LineEnd, Point, IM_COL32(30, 34, 30, 200), 3 * Scale);
                                Ink->AddLine(LineEnd, Point, IM_COL32(255, 255, 255, 190), 1.2f * Scale);
                                const float HalfExtent = 5 * Scale;
                                Ink->AddRectFilled(ImVec2(Point.x - HalfExtent, Point.y - HalfExtent), ImVec2(Point.x + HalfExtent, Point.y + HalfExtent), IM_COL32(245, 248, 240, 255), 1.5f * Scale);
                                Ink->AddRect(ImVec2(Point.x - HalfExtent, Point.y - HalfExtent), ImVec2(Point.x + HalfExtent, Point.y + HalfExtent), IM_COL32(30, 34, 30, 255), 1.5f * Scale, 0, 1.8f * Scale);
                            }
                            const auto Stops = QueryVectorGradientStops(GradientSource);
                            SelectedGradientStop = std::clamp(SelectedGradientStop, 0, int(Stops.size()) - 1);
                            for (size_t Index = 0; Index < Stops.size(); ++Index)
                            {
                                const ImVec2 Point(AxisStart.x + (AxisEnd.x - AxisStart.x) * Stops[Index].Position, AxisStart.y + (AxisEnd.y - AxisStart.y) * Stops[Index].Position);
                                const bool IsSelected = int(Index) == SelectedGradientStop;
                                const float Radius = (IsSelected ? 7.f : 5.5f) * Scale;
                                const auto &C = Stops[Index].Colour;
                                const ImU32 StopColour = IM_COL32(uint8_t(std::clamp(C[0], 0.f, 1.f) * 255), uint8_t(std::clamp(C[1], 0.f, 1.f) * 255), uint8_t(std::clamp(C[2], 0.f, 1.f) * 255), 255);
                                Ink->AddCircleFilled(Point, Radius + 1.8f * Scale, IM_COL32(30, 34, 30, 215));
                                Ink->AddCircleFilled(Point, Radius, StopColour);
                                Ink->AddCircle(Point, Radius, IsSelected ? IM_COL32(255, 255, 255, 255) : IM_COL32(20, 20, 20, 220), 0, (IsSelected ? 2.4f : 1.4f) * Scale);
                                const std::string Name = std::string("Gradient stop:") + std::to_string(Index);
                                const float HitRadius = 9 * Scale;
                                ControlExtents[Name] = {Point.x - HitRadius, Point.y - HitRadius, Point.x + HitRadius, Point.y + HitRadius};
                                if (std::abs(Mouse.x - Point.x) <= HitRadius && std::abs(Mouse.y - Point.y) <= HitRadius) GradientHoverSlot = int(Index);
                            }
                            {
                                // 📝 Standing a fixed standoff away from the line's own coincident stop keeps
                                // this independently hit-testable without needing to arbitrate priority against
                                // the stop-marker loop above (its own hit box never reaches this far out).
                                const float AxisHitRadius = 9 * Scale;
                                for (int End = 0; End < 2; ++End)
                                {
                                    const ImVec2 &Point = End == 0 ? AxisHandleA : AxisHandleB;
                                    const std::string Name = std::string("Gradient axis:") + std::to_string(End);
                                    ControlExtents[Name] = {Point.x - AxisHitRadius, Point.y - AxisHitRadius, Point.x + AxisHitRadius, Point.y + AxisHitRadius};
                                    if (std::abs(Mouse.x - Point.x) <= AxisHitRadius && std::abs(Mouse.y - Point.y) <= AxisHitRadius) GradientAxisHoverEnd = End;
                                }
                            }
                            if (GradientHoverSlot < 0 && GradientAxisHoverEnd < 0 && Stops.size() < size_t(VectorGradientStopCapacity))
                            {
                                const float Along = ProjectAlongSegment({Mouse.x, Mouse.y}, {AxisStart.x, AxisStart.y}, {AxisEnd.x, AxisEnd.y});
                                if (Along > .02f && Along < .98f)
                                {
                                    const ImVec2 Nearest(AxisStart.x + Along * (AxisEnd.x - AxisStart.x), AxisStart.y + Along * (AxisEnd.y - AxisStart.y));
                                    if (std::hypot(Mouse.x - Nearest.x, Mouse.y - Nearest.y) <= 10 * Scale) GradientLineHoverT = Along;
                                }
                            }
                        }
                    }
                }
            }
        }
        catch (const std::exception &) {}
    }
    if (Gesture == 4)
    {
        const ImVec2 Origin(BoardMinimum.x + PointerOrigin.X / 512 * BoardSize, BoardMinimum.y + PointerOrigin.Y / 512 * BoardSize);
        const auto Mouse = ImGui::GetIO().MousePos;
        const ImVec2 Lower(std::min(Origin.x, Mouse.x), std::min(Origin.y, Mouse.y));
        const ImVec2 Upper(std::max(Origin.x, Mouse.x), std::max(Origin.y, Mouse.y));
        Ink->AddRectFilled(Lower, Upper, IM_COL32(191, 216, 169, 24));
        Ink->AddRect(Lower, Upper, IM_COL32(191, 216, 169, 220));
    }
    if (Gesture == 2 && Tool == VectorAuthoringTool::Text)
    {
        const ImVec2 Origin(BoardMinimum.x + PointerOrigin.X / 512 * BoardSize, BoardMinimum.y + PointerOrigin.Y / 512 * BoardSize);
        const ImVec2 Current(BoardMinimum.x + TextBoxCurrent.X / 512 * BoardSize, BoardMinimum.y + TextBoxCurrent.Y / 512 * BoardSize);
        const ImVec2 Lower(std::min(Origin.x, Current.x), std::min(Origin.y, Current.y));
        const ImVec2 Upper(std::max(Origin.x, Current.x), std::max(Origin.y, Current.y));
        Ink->AddRectFilled(Lower, Upper, IM_COL32(134, 164, 112, 40));
        Ink->AddRect(Lower, Upper, IM_COL32(134, 164, 112, 230));
    }
    Ink->PopClipRect();
    ImGui::SetCursorScreenPos(Minimum);
    ImGui::InvisibleButton("Stamp canvas", ImVec2(Maximum.x - Minimum.x, Maximum.y - Minimum.y), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    ControlExtents["Stamp canvas"] = {Minimum.x, Minimum.y, Maximum.x, Maximum.y};
    const auto &Input = ImGui::GetIO();
    const bool Hovered = ImGui::IsItemHovered();
    // 📝 Submitted after the invisible canvas button (not during the earlier Ink-drawing pass) so the
    // composer's real InputTextMultiline sits on top for mouse hit-testing — otherwise the button
    // underneath it would steal clicks meant for placing the text cursor inside the box.
    if (Gesture == 8) ConstructTextComposer(BoardMinimum.x, BoardMinimum.y, BoardSize);
    const auto DocumentPoint = [&](ImVec2 Point, bool Snap = true)
    {
        PlanePoint Position{(Point.x - BoardMinimum.x) / BoardSize * 512, (Point.y - BoardMinimum.y) / BoardSize * 512};
        if (SnappingEnabled && Snap) { Position.X = std::round(Position.X / 16) * 16; Position.Y = std::round(Position.Y / 16) * 16; }
        return Position;
    };
    if (Hovered && Input.MouseWheel != 0 && Gesture == 0)
    {
        const float Previous = Zoom;
        Zoom = ApplyWorkspaceWheelZoom(Zoom, Input.MouseWheel);
        Pan.X += (Input.MousePos.x - Centre.x) * (1 - Zoom / Previous);
        Pan.Y += (Input.MousePos.y - Centre.y) * (1 - Zoom / Previous);
    }
    if (Hovered && Tool == VectorAuthoringTool::Select)
    {
        if (HoveredRotation || Gesture == 6) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        else if (HoveredResize[0] || HoveredResize[1]) ImGui::SetMouseCursor(!HoveredResize[0] ? ImGuiMouseCursor_ResizeNS : !HoveredResize[1] ? ImGuiMouseCursor_ResizeEW : HoveredResize[0] == HoveredResize[1] ? ImGuiMouseCursor_ResizeNWSE : ImGuiMouseCursor_ResizeNESW);
    }
    // The gradient grips are live under every tool, so their hover cursor is too.
    if (Hovered && (GradientAxisHoverEnd >= 0 || GradientHoverSlot >= 0 || GradientLineHoverT >= 0 || Gesture == 9 || Gesture == 10 || Gesture == 11))
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    const bool PenInput = ConstructPenInteraction({Minimum.x, Minimum.y, Maximum.x, Maximum.y}, BoardExtent, Hovered);
    const bool PointInput = ConstructPointInteraction({Minimum.x, Minimum.y, Maximum.x, Maximum.y}, BoardExtent, Hovered);
    bool TextReEditEntered = false;
    if (Hovered && Gesture == 0 && Tool == VectorAuthoringTool::Select && ImGui::IsMouseDoubleClicked(0) && QuerySelectionToken)
    {
        // 📝 Double-clicking an existing text component re-enters the same on-canvas composer a
        // freshly drawn box would, seeded with its current content/bounds, instead of only being
        // editable through the Inspector.
        try
        {
            const auto &Source = History.QueryDocument();
            if (const auto Hit = QuerySelectionToken(Source, DocumentPoint(Input.MousePos, false)); Hit && *Hit)
            {
                const auto Position = std::find_if(Source.Layers.begin(), Source.Layers.end(), [&](const auto &Layer) { return Layer.Token == *Hit; });
                if (Position != Source.Layers.end() && Position->ComponentKind == VectorAuthoringComponentKind::Text &&
                    !QueryVectorAuthoringProtection(Source, *Position) && QueryVectorAuthoringVisibility(Source, *Position))
                {
                    TextEditBounds = QueryVectorAuthoringSelectionBounds(Source, std::array<uint32_t, 1>{*Hit});
                    ComponentEditToken = *Hit;
                    std::snprintf(ComponentTextBuffer.data(), ComponentTextBuffer.size(), "%s", Position->SourceMarkup.c_str());
                    Selection = {*Hit};
                    Gesture = 8; TextEditFocusPending = true;
                    TextReEditEntered = true;
                }
            }
        }
        catch (const std::exception &) {}
    }
    if (!PenInput && !PointInput && Hovered && Gesture == 0 && !TextReEditEntered && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(2)))
    {
        GestureSelection = Selection;
        PointerOrigin = DocumentPoint(Input.MousePos, false);
        if (ImGui::IsMouseClicked(2) || Tool == VectorAuthoringTool::Pan || ImGui::IsKeyDown(ImGuiKey_Space)) { Gesture = 3; PanOrigin = Pan; }
        else if (MaskSelection) { /* Mask descriptor interaction remains in the Inspector. */ }
        else if (GradientAxisHoverEnd >= 0)
        {
            Gesture = GradientAxisHoverEnd == 0 ? 10 : 11;
            GestureSource = History.QueryDocument();
        }
        else if (GradientHoverSlot >= 0)
        {
            Gesture = 9;
            GradientDragSlot = GradientHoverSlot;
            SelectedGradientStop = GradientHoverSlot;
            GestureSource = History.QueryDocument();
        }
        else if (GradientLineHoverT >= 0)
        {
            // 📝 A single immediate edit (like the Inspector's own "Add mask" button), not a drag: the
            // new stop's colour starts as a blend of whichever two stops it landed between, so it reads
            // as inserted into the ramp rather than appearing as a jarring flat band.
            try
            {
                const auto &Live = History.QueryDocument();
                const auto GradientLayer = std::find_if(Live.Layers.begin(), Live.Layers.end(), [&](const auto &Layer) { return Layer.Token == Selection[0]; });
                if (GradientLayer != Live.Layers.end())
                {
                    auto Stops = QueryVectorGradientStops(Live.Instructions[GradientLayer->Root]);
                    const auto Colour = EvaluateVectorGradientColour(Stops, GradientLineHoverT);
                    Stops.push_back({std::clamp(GradientLineHoverT, 0.f, 1.f), Colour});
                    std::sort(Stops.begin(), Stops.end(), [](const auto &A, const auto &B) { return A.Position < B.Position; });
                    const auto Inserted = std::find_if(Stops.begin(), Stops.end(), [&](const auto &Stop) { return Stop.Position == std::clamp(GradientLineHoverT, 0.f, 1.f); });
                    SelectedGradientStop = int(Inserted - Stops.begin());
                    VectorAuthoringSourcePatch Patch; ApplyVectorGradientStops(Patch, Stops);
                    ApplySourcePatch(Patch);
                }
            }
            catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
        }
        else if (HoveredRotation || HoveredResize[0] || HoveredResize[1])
        {
            Gesture = HoveredRotation ? 6 : 5;
            ResizeDirection = HoveredResize;
            GestureSource = History.QueryDocument();
        }
        else if (Tool != VectorAuthoringTool::Select)
        {
            Gesture = 2;
            GestureSource = History.QueryDocument();
            GestureDraft.reset();
            GesturePrimitiveToken = 0;
            TextBoxCurrent = PointerOrigin; // Seeds the Text tool's drawn box so a plain click (no drag) still has a point to measure from.
        }
        else
        {
            SelectionExtend = Input.KeyShift;
            GestureSource = History.QueryDocument();
            SelectionPointer = PointerOrigin; SelectionTravel = 0; SelectionReleased = false;
            Gesture = 7;
        }
    }
    if (Gesture == 7 && (ImGui::IsKeyPressed(ImGuiKey_Escape) || Input.AppFocusLost || !ImGui::IsMousePosValid()))
    {
        Selection = GestureSelection; GestureSource.reset(); Gesture = 0;
    }
    if (Gesture == 7)
    {
        if (!SelectionReleased)
        {
            SelectionPointer = DocumentPoint(Input.MousePos, false);
            SelectionTravel = std::max(SelectionTravel, std::hypot(SelectionPointer.X - PointerOrigin.X, SelectionPointer.Y - PointerOrigin.Y) * BoardSize / 512);
            SelectionReleased = !ImGui::IsMouseDown(0);
        }
        try
        {
            if (!GestureSource || History.QueryDocument() != *GestureSource)
                throw std::runtime_error("Source changed while GPU selection was pending; click again.");
            if (!QuerySelectionToken) throw std::runtime_error("GPU contour selection is unavailable; use the layer list.");
            if (const auto Hit = QuerySelectionToken(*GestureSource, PointerOrigin))
            {
                if (*Hit)
                {
                    const auto Position = std::find_if(GestureSource->Layers.begin(), GestureSource->Layers.end(), [&](const auto &Layer) { return Layer.Token == *Hit; });
                    if (Position == GestureSource->Layers.end() || QueryVectorAuthoringProtection(*GestureSource, *Position) || !QueryVectorAuthoringVisibility(*GestureSource, *Position))
                        throw std::runtime_error("GPU selection token no longer authenticates to an editable source.");
                    if (SelectionExtend || std::find(Selection.begin(), Selection.end(), *Hit) == Selection.end()) Select(*Hit, SelectionExtend);
                    Gesture = Selection.empty() ? 0 : 1;
                    if (Selection.empty()) GestureSource.reset();
                }
                else
                {
                    MarqueeAdditive = SelectionExtend;
                    Gesture = 4;
                    if (!MarqueeAdditive) Selection.clear();
                }
                Diagnostic.clear();
            }
        }
        catch (const std::exception &Failure)
        {
            Diagnostic = Failure.what(); GestureDraft.reset(); GestureSource.reset(); Gesture = 0; ++GestureRevision;
        }
    }
    if (Gesture == 3 && (ImGui::IsMouseDown(0) || ImGui::IsMouseDown(2)))
    {
        const auto Δ = EvaluateNavigationDelta ? EvaluateNavigationDelta({Input.MouseDelta.x, Input.MouseDelta.y}) : PlanePoint{Input.MouseDelta.x, Input.MouseDelta.y};
        Pan.X += Δ.X; Pan.Y += Δ.Y;
    }
    if ((Gesture == 1 || Gesture == 2 || Gesture == 5 || Gesture == 6 || Gesture == 9 || Gesture == 10 || Gesture == 11) && (ImGui::IsMouseDragging(0, 2) || (Gesture == 1 && SelectionReleased && SelectionTravel >= 2 * Scale)) && GestureSource)
    {
        auto Position = DocumentPoint(Input.MousePos, Gesture != 6);
        if (Gesture == 1 && SelectionReleased)
        {
            Position = SelectionPointer;
            if (SnappingEnabled) { Position.X = std::round(Position.X / 16) * 16; Position.Y = std::round(Position.Y / 16) * 16; }
        }
        try
        {
            if (Gesture == 2 && Tool == VectorAuthoringTool::Text)
            {
                // 📝 Text never creates a GestureDraft primitive while dragging; it only tracks the
                // box endpoint for the preview rectangle below and for sizing the composer on release.
                TextBoxCurrent = Position;
            }
            else if (Gesture == 2)
            {
                VectorCoverageInstruction Source;
                Source.Topology[0] = Tool == VectorAuthoringTool::Rectangle ? 0u : Tool == VectorAuthoringTool::Ellipse ? 1u : uint32_t(Tool) - uint32_t(VectorAuthoringTool::Diamond) + 2;
                // Ctrl draws the shape out from the drag start as its centre (mouse = centre).
                if (Input.KeyCtrl || Input.KeySuper)
                    Source.Bounds = {PointerOrigin.X, PointerOrigin.Y,
                        std::max(1.f, 2.f * std::abs(Position.X - PointerOrigin.X)), std::max(1.f, 2.f * std::abs(Position.Y - PointerOrigin.Y))};
                else
                    Source.Bounds = {(Position.X + PointerOrigin.X) * .5f, (Position.Y + PointerOrigin.Y) * .5f,
                        std::max(1.f, std::abs(Position.X - PointerOrigin.X)), std::max(1.f, std::abs(Position.Y - PointerOrigin.Y))};
                Source.Colour = {.8627f, .4627f, .3529f, 1};
                Source.StrokeColour = CanvasInkColour();
                Source.Topology[3] |= VectorCoverageStrokeColourExplicit;
                // 📝 Paint[2]/Paint[1] default-initialize to zero, which the renderer floors to a 3-sided
                // sliver - seed a sensible pentagon/5-point-star so a freshly drawn Polygon or Star reads as
                // one immediately, before the Inspector's own Sides/Inner radius fields are ever touched.
                if (Source.Topology[0] == 4) Source.Paint[2] = 5;
                else if (Source.Topology[0] == 5) { Source.Paint[2] = 5; Source.Paint[1] = .5f; }
                if (!GestureDraft)
                {
                    auto Draft = *GestureSource;
                    GesturePrimitiveToken = ConstructVectorAuthoringPrimitive(Draft, Source, ShapeNames[Source.Topology[0]]);
                    GestureDraft = std::move(Draft);
                    Selection = {GesturePrimitiveToken};
                }
                else
                {
                    auto &Draft = *GestureDraft;
                    const auto Layer = std::find_if(Draft.Layers.begin(), Draft.Layers.end(), [&](const auto &Candidate)
                        { return Candidate.Token == GesturePrimitiveToken; });
                    if (Layer == Draft.Layers.end()) throw std::runtime_error("The in-progress shape lost its authenticated layer token.");
                    Draft.Instructions.at(Layer->Root).Bounds = Source.Bounds;
                }
                ++GestureRevision;
            }
            else if (Gesture == 9)
            {
                // 📝 Reprojects the live mouse position onto the gradient's own axis (recomputed from
                // GestureSource so a drag mid-flight always measures against the pre-drag shape, exactly
                // like Gesture 1/5/6 above) to get a Fraction, then clamps it strictly between its two
                // immediate neighbours (plus a small gap) so dragging a marker can never cross another
                // one and silently reorder the packed stop slots underneath the user.
                auto Draft = *GestureSource;
                const auto GradientLayer = std::find_if(Draft.Layers.begin(), Draft.Layers.end(), [&](const auto &Layer) { return Layer.Token == Selection[0]; });
                if (GradientLayer == Draft.Layers.end()) throw std::runtime_error("Gradient selection lost its authenticated layer token.");
                const auto &GradientSource = Draft.Instructions.at(GradientLayer->Root);
                const auto Extent = QueryVectorSelectionExtent(Draft, Selection);
                const auto Axis = QueryGradientAxisEndpoints(GradientSource, Extent.Bounds, Extent.Rotation);
                const float Along = std::clamp(ProjectAlongSegment(Position, Axis[0], Axis[1]), 0.f, 1.f);
                auto Stops = QueryVectorGradientStops(GradientSource);
                if (GradientDragSlot < 0 || GradientDragSlot >= int(Stops.size())) throw std::runtime_error("Gradient stop drag lost its slot.");
                constexpr float Gap = .01f;
                const float LowerBound = GradientDragSlot > 0 ? Stops[GradientDragSlot - 1].Position + Gap : 0.f;
                const float UpperBound = GradientDragSlot + 1 < int(Stops.size()) ? Stops[GradientDragSlot + 1].Position - Gap : 1.f;
                Stops[GradientDragSlot].Position = std::clamp(Along, std::min(LowerBound, UpperBound), std::max(LowerBound, UpperBound));
                VectorAuthoringSourcePatch Patch; Patch.GradientStopPositions = {Stops.size() > 0 ? Stops[0].Position : 0.f, Stops.size() > 1 ? Stops[1].Position : 1.f,
                    Stops.size() > 2 ? Stops[2].Position : -1.f, Stops.size() > 3 ? Stops[3].Position : -1.f};
                TransformVectorAuthoringSelection(Draft, Selection, Patch);
                GestureDraft = std::move(Draft);
                ++GestureRevision;
            }
            else if (Gesture == 10 || Gesture == 11)
            {
                // 📝 Repositions the ramp's own A (Gesture 10) or B (Gesture 11) control point to wherever
                // the mouse is now, in the shape's own LocalNorm frame (QueryGradientLocalNorm, the exact
                // inverse of the marker-drawing math) — re-anchored from the frozen pre-drag GestureSource,
                // same convention as every other drag gesture here. Per-stop Fractions are left untouched,
                // so the ramp itself doesn't visually jump; only where its line/radius sits does.
                auto Draft = *GestureSource;
                const auto GradientLayer = std::find_if(Draft.Layers.begin(), Draft.Layers.end(), [&](const auto &Layer) { return Layer.Token == Selection[0]; });
                if (GradientLayer == Draft.Layers.end()) throw std::runtime_error("Gradient selection lost its authenticated layer token.");
                const auto &GradientSource = Draft.Instructions.at(GradientLayer->Root);
                const auto Extent = QueryVectorSelectionExtent(Draft, Selection);
                const auto LocalNorm = QueryGradientLocalNorm(Position, Extent.Bounds, Extent.Rotation);
                auto Axis = GradientSource.GradientAxis;
                if (Gesture == 10) { Axis[0] = LocalNorm[0]; Axis[1] = LocalNorm[1]; }
                else { Axis[2] = LocalNorm[0]; Axis[3] = LocalNorm[1]; }
                VectorAuthoringSourcePatch Patch; Patch.GradientAxis = Axis;
                TransformVectorAuthoringSelection(Draft, Selection, Patch);
                GestureDraft = std::move(Draft);
                ++GestureRevision;
            }
            else
            {
                auto Draft = *GestureSource;
                if (Gesture == 1)
                {
                    VectorAuthoringSourcePatch Patch; Patch.TranslationX = Position.X - PointerOrigin.X; Patch.TranslationY = Position.Y - PointerOrigin.Y;
                    TransformVectorAuthoringSelection(Draft, Selection, Patch);
                }
                else
                {
                    const auto Patch = Gesture == 5
                        ? EvaluateVectorSelectionResize(Draft, Selection, ResizeDirection, {Position.X, Position.Y}, Input.KeyShift, Input.KeyAlt)
                        : EvaluateVectorSelectionRotation(Draft, Selection, {PointerOrigin.X, PointerOrigin.Y}, {Position.X, Position.Y}, Input.KeyShift || SnappingEnabled);
                    TransformVectorAuthoringSelection(Draft, Selection, Patch);
                }
                GestureDraft = std::move(Draft);
                ++GestureRevision;
            }
            Diagnostic.clear();
        }
        catch (const std::exception &Failure) { GestureDraft.reset(); GesturePrimitiveToken = 0; ++GestureRevision; Diagnostic = Failure.what(); }
    }
    if ((ImGui::IsKeyPressed(ImGuiKey_Escape) || Input.AppFocusLost || !ImGui::IsMousePosValid()) && Gesture)
    {
        Selection = GestureSelection; GestureDraft.reset(); GestureSource.reset(); Gesture = 0; GesturePrimitiveToken = 0; ++GestureRevision;
    }
    else if (Gesture == 2 && Tool == VectorAuthoringTool::Text && !ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(2))
    {
        // 📝 The box is drawn; hand off to the on-canvas composer (Gesture 8) instead of the generic
        // shape-finalize path below, which only knows how to finalize a GestureDraft primitive.
        constexpr float MinimumBoxExtent = 8.f, DefaultBoxWidth = 220.f, DefaultBoxHeight = 64.f;
        const float Width = std::abs(TextBoxCurrent.X - PointerOrigin.X), Height = std::abs(TextBoxCurrent.Y - PointerOrigin.Y);
        if (Width < MinimumBoxExtent && Height < MinimumBoxExtent)
            TextEditBounds = {PointerOrigin.X, PointerOrigin.Y, DefaultBoxWidth, DefaultBoxHeight};
        else
            TextEditBounds = {(PointerOrigin.X + TextBoxCurrent.X) * .5f, (PointerOrigin.Y + TextBoxCurrent.Y) * .5f,
                std::max(MinimumBoxExtent, Width), std::max(MinimumBoxExtent, Height)};
        ComponentEditToken = 0;
        ComponentTextBuffer[0] = 0;
        GestureSource.reset(); Gesture = 8; TextEditFocusPending = true;
    }
    // 📝 Gesture 7 (marquee) and Gesture 8 (on-canvas text composer) both legitimately persist across
    // many idle-mouse frames after the mouse that opened them is released — Gesture 8 especially, since
    // the whole point is to keep the InputTextMultiline mounted and focused while the user types with the
    // mouse sitting untouched. Excluding only 7 here let this catch-all drag-release cleanup fire on the
    // very next frame after the Text tool handed off to the composer, snapping Gesture back to 0 (with no
    // GestureDraft to finalize) before a single character could be typed — the composer box vanished
    // instantly, nothing reached the layer stack, exactly the reported "Text tool is broken" symptom.
    else if (Gesture && Gesture != 7 && Gesture != 8 && !ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(2))
    {
        if (Gesture == 4 && GestureSource)
        {
            const auto Position = SelectionReleased ? SelectionPointer : DocumentPoint(Input.MousePos, false);
            if (std::hypot(Position.X - PointerOrigin.X, Position.Y - PointerOrigin.Y) * BoardSize / 512 >= 3 * Scale)
                Selection = QueryVectorSelectionRegion(*GestureSource, {PointerOrigin.X, PointerOrigin.Y}, {Position.X, Position.Y}, MarqueeAdditive ? std::span<const uint32_t>(GestureSelection) : std::span<const uint32_t>{});
        }
        if (GestureDraft) { auto Document = std::move(*GestureDraft); GestureDraft.reset(); Finalize(std::move(Document), Selection); }
        GestureSource.reset(); Gesture = 0;
    }
    Ink->AddRectFilled(ImVec2(At(24, 0).x, Maximum.y), ImVec2(At(Width / Scale - 24, 0).x, Maximum.y + 33 * Scale), ImGui::GetColorU32(Spectrum(0x191c16)), 6 * Scale);
    Caption(ImVec2(At(35, 0).x, Maximum.y + 11 * Scale), "Marquee to select · Shift constrain · Alt centred resize · Ctrl centre draw · Space pan", 8 * Scale, 0x909a7c);
    Caption(ImVec2(At(Width / Scale - 114, 0).x, Maximum.y + 11 * Scale), (std::to_string(Selection.size()) + " selected").c_str(), 8 * Scale, 0xc6d2b5);
    // 📝 The standalone "Decal preview on a plain surface" panel that used to live here was removed —
    // it duplicated the main board above with no extra information. Content height is exactly the
    // dock's available height (see Maximum), so the panel never scrolls: canvas + hint bar always fit.
    ImGui::SetCursorScreenPos(ImVec2(At(Width / Scale - 24, 0).x, ContentOrigin.y + AvailableHeight - 1));
    ImGui::Dummy(ImVec2(1, 1));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SELECTION-AWARE INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

void StampWorkspacePanel::ConstructInspector()
{
    CapturePanel("Inspector");
    const auto Position = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(Position.x + 18 * Scale, Position.y + 30 * Scale));
    ImGui::BeginChild("Stamp inspector scroll", ImVec2(ImGui::GetContentRegionAvail().x - 18 * Scale, 0));
    if (PenSource)
    {
        Controls.BeginCard(PolylineSession ? "Pending polyline path" : "Pending pen path", true);
        ImGui::TextWrapped(PolylineSession
            ? "Click to place straight-corner anchors. Ctrl-click a retained endpoint to start a separate path. While continuing a retained path, click another endpoint to join; Ctrl bypasses joining. Hold Space during placement to reposition the new anchor. Click the first anchor to close it into a filled shape. Enter finishes it open as a plain line; Escape cancels; Backspace removes the last anchor."
            : "Click to place anchors; drag tangents. Ctrl-click a retained endpoint to start a separate path. While continuing a retained path, click another endpoint to join; Ctrl bypasses joining. Joins require adjacent matching opaque strokes, matching group/channels and no owned masks. Hold Space during placement to reposition the new anchor. Alt-click the last anchor to break its outgoing tangent (manual curves). Click the first anchor to close. Enter finishes open; Escape cancels; Backspace removes the last anchor.");
        if (Controls.Button("Finish open path", 200 * Scale)) FinishPen(false);
        if (Controls.Button(PolylineSession ? "Cancel polyline path" : "Cancel pen path", 200 * Scale)) CancelPen();
        Controls.EndCard();
        ImGui::EndChild();
        return;
    }
    const auto Snapshot = QueryDocument();
    const auto SelectedTokens = Selection;
    const auto Selected = [&](const auto &Layer) { return std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end(); };
    const auto First = std::find_if(Snapshot.Layers.begin(), Snapshot.Layers.end(), Selected);
    const bool Protected = std::any_of(Snapshot.Layers.begin(), Snapshot.Layers.end(), [&](const auto &Layer) { return Selected(Layer) && QueryVectorAuthoringProtection(Snapshot, Layer); });
    if (First == Snapshot.Layers.end())
    {
        Controls.BeginCard("Your next component", true);
        ImGui::TextWrapped("Pick a tool and draw a shape, or add Text/Vector/Image with the + beside Components.");
        Controls.EndCard();
        ImGui::EndChild();
        return;
    }
    if (!MaskSelection && Selection.size() == 1 && First->ComponentKind != VectorAuthoringComponentKind::Freeform)
    {
        const bool IsText = First->ComponentKind == VectorAuthoringComponentKind::Text;
        const bool IsVector = First->ComponentKind == VectorAuthoringComponentKind::ImportedVector;
        Controls.BeginCard(IsText ? "Text content" : IsVector ? "Imported vector" : "Imported image", true);
        ImGui::BeginDisabled(Protected);
        if (IsText)
        {
            // 📝 Text properties live inline here (content + font size), not behind a modal: editing
            // either commits (rebakes the real glyph outline, anchor held at its current centre) the
            // moment you leave the field, via IsItemDeactivatedAfterEdit — not on every keystroke.
            if (InspectorTextToken != First->Token)
            {
                std::snprintf(InspectorTextBuffer.data(), InspectorTextBuffer.size(), "%s", First->SourceMarkup.c_str());
                InspectorTextToken = First->Token;
            }
            ImGui::TextDisabled("Content");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextMultiline("##StampTextInspectorContent", InspectorTextBuffer.data(), InspectorTextBuffer.size(), ImVec2(0, 56 * Scale));
            const bool ContentCommit = ImGui::IsItemDeactivatedAfterEdit();
            float PointSize = First->TextPointSize;
            Controls.Number("Font size", PointSize, 4, 512);
            const bool SizeCommit = ImGui::IsItemDeactivatedAfterEdit();
            if (ContentCommit || SizeCommit) RebakeTextLayer(First->Token, InspectorTextBuffer.data(), PointSize);
            if (Controls.Button("Edit on canvas...", 160 * Scale))
            {
                TextEditBounds = QueryVectorAuthoringSelectionBounds(Snapshot, std::array<uint32_t, 1>{First->Token});
                ComponentEditToken = First->Token;
                std::snprintf(ComponentTextBuffer.data(), ComponentTextBuffer.size(), "%s", First->SourceMarkup.c_str());
                Gesture = 8; TextEditFocusPending = true;
            }
        }
        else if (IsVector)
        {
            ImGui::Text("%zu bytes of SVG markup", First->SourceMarkup.size());
            if (Controls.Button("Replace SVG...", 160 * Scale))
            {
                ComponentEditToken = First->Token;
                ComponentSvgBuffer = First->SourceMarkup;
                ImGui::OpenPopup("Add stamp vector");
            }
            ImGui::TextWrapped("Gradients fall back to a neutral grey; clip-paths and masks are not ported.");
        }
        else
        {
            ImGui::Text("%zu bytes embedded (PNG data URL)", First->EmbeddedImage.size());
            ImGui::TextWrapped("Pixel sampling is not implemented yet; this renders as a placeholder box.");
            if (Controls.Button("Replace image...", 160 * Scale))
            {
                ComponentEditToken = First->Token;
                ComponentImageBuffer = First->EmbeddedImage;
                ImGui::OpenPopup("Add stamp image");
            }
        }
        ImGui::EndDisabled();
        Controls.EndCard();
        ConstructVectorPopup();
        ConstructImagePopup();
        if (Selection != SelectedTokens) { ImGui::EndChild(); return; }
    }
    if (!MaskSelection)
    {
        Controls.BeginCard(Selection.size() == 1 ? "Selected component" : "Selected components");
        ImGui::BeginDisabled(Protected);
        ImGui::TextUnformatted(Selection.size() == 1 ? "Align to canvas" : "Align selection");
        for (int Axis = 0; Axis < 2; ++Axis)
        {
            for (int Boundary = 0; Boundary < 3; ++Boundary)
            {
                if (Boundary) ImGui::SameLine(0, 6 * Scale);
                const char *Captions[]{"Align left", "Centre horizontally", "Align right", "Align top", "Centre vertically", "Align bottom"};
                if (Controls.Button(Captions[Axis * 3 + Boundary], 30 * Scale, false, Axis == 0 ? "M3 3V21M7 7H16V17H7Z" : "M3 3H21M7 7H17V16H7Z"))
                {
                    const auto Bounds = QueryVectorAuthoringSelectionBounds(Snapshot, Selection);
                    const float Centre = Axis ? Bounds[1] : Bounds[0], Extent = Axis ? Bounds[3] : Bounds[2];
                    const float Destination = Boundary == 0 ? Extent * .5f : Boundary == 1 ? 256 : 512 - Extent * .5f;
                    VectorAuthoringSourcePatch Patch; if (Axis) Patch.TranslationY = Destination - Centre; else Patch.TranslationX = Destination - Centre;
                    ApplySourcePatch(Patch);
                }
            }
        }
        if (Selection.size() >= 2)
        {
            bool Eligible = !Protected && std::all_of(Snapshot.Layers.begin(), Snapshot.Layers.end(), [&](const auto &Layer) { return !Selected(Layer) || !Layer.Mask; });
            if (const auto Operation = ConstructVectorBooleanControls(Eligible))
            {
                try { auto Document = Snapshot; const auto Token = IntegrateVectorAuthoringBoolean(Document, Selection, VectorCoverageCategory(*Operation)); Finalize(std::move(Document), {Token}); }
                catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
            }
        }
        ImGui::Spacing();
        if (Controls.Button("Duplicate selection", 30 * Scale, false, CopyPath)) ReplicateSelection();
        Controls.Tooltip("Duplicate", "Retain editable source, mask and channel settings in an independent copy.");
        ImGui::SameLine(0, 6 * Scale);
        if (Controls.Button("Delete selection", 30 * Scale, false, TrashPath)) EvaluateEdit([&](auto &Document) { PartitionVectorAuthoringSelection(Document, Selection); });
        ImGui::EndDisabled();
        Controls.EndCard();
        if (Selection != SelectedTokens) { ImGui::EndChild(); return; }
        Controls.ControlHeight = 48;
        if (Controls.Button(("Surface channels · " + std::to_string(Selection.size()) + " selected").c_str(), ImGui::GetContentRegionAvail().x, ChannelsOpen, {}, true)) ChannelsOpen = !ChannelsOpen;
        Controls.ControlHeight = 30;
        if (ChannelsOpen)
        {
            ImGui::TextWrapped("For a plastic/film sticker over a decal: enable Transmission + Clearcoat + Roughness on the "
                "film layer, and Height/Normal on the decal layer beneath it for embossed text or bump.");
            if (Selection.size() > 1)
                ImGui::TextDisabled("Toggling a channel applies the displayed setting to every selected component.");
            const char *Channels[]{"Colour", "Height", "Normal", "Roughness", "Metalness", "Opacity", "Ambient occlusion", "Transmission", "Clearcoat"};
            ImGui::BeginDisabled(Protected);
            for (size_t Index = 0; Index < std::size(Channels); ++Index)
            {
                bool Enabled = First->SurfaceChannels[Index].Enabled;
                if (Controls.Toggle(Channels[Index], Enabled))
                    EvaluateEdit([&](auto &Document)
                    {
                        for (auto &Layer : Document.Layers)
                            if (std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end())
                                Layer.SurfaceChannels[Index].Enabled = Enabled;
                    });
                float Strength = First->SurfaceChannels[Index].Strength;
                const std::string StrengthCaption = std::string(Channels[Index]) + " strength";
                const float Maximum = Index == size_t(VectorAuthoringSurfaceChannel::Normal) ? 4.f : 1.f;
                if (Controls.Range(StrengthCaption.c_str(), Strength, 0.f, Maximum))
                    EvaluateEdit([&](auto &Document)
                    {
                        for (auto &Layer : Document.Layers)
                            if (std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end())
                                Layer.SurfaceChannels[Index].Strength = Strength;
                    });
            }
            ImGui::EndDisabled();
            float SurfaceRelief = First->SurfaceRelief;
            ImGui::BeginDisabled(Protected);
            if (Controls.Range("Surface relief", SurfaceRelief, -1.f, 1.f))
                EvaluateEdit([&](auto &Document)
                {
                    for (auto &Layer : Document.Layers)
                        if (std::find(Selection.begin(), Selection.end(), Layer.Token) != Selection.end())
                            Layer.SurfaceRelief = SurfaceRelief;
                });
            ImGui::EndDisabled();
            Controls.Tooltip("Surface relief", "Signed height amplitude used by the Height map and its derived tangent normals.");
        }
        ImGui::Dummy(ImVec2(0, 0));
    }
    if (!MaskSelection && Selection.size() == 1 && Tool == VectorAuthoringTool::Points)
    {
        Controls.BeginCard("Path points", true);
        const auto Category = Snapshot.Instructions[First->Root].Topology[0];
        ImGui::BeginDisabled(Protected || Gesture != 0);
        if (Category < 6)
        {
            ImGui::TextWrapped("Convert this primitive to an editable cubic path. Ellipses use a four-curve approximation; Polygon/Star keep their own corner count.");
            if (Controls.Button("Convert to cubic path", 200 * Scale))
                EvaluateEdit([&](auto &Document) { ReplaceCubicPath(Document, First->Token, DerivePrimitiveCubicPath(Document, First->Token)); });
        }
        else if (Category == 6)
        {
            ImGui::TextWrapped("Shift-click anchors to select or deselect several. Drag or nudge selected anchors together. Automatic tangents use neighbours; Delete removes selected anchors. Escape cancels a drag.");
            ImGui::Text("%zu anchors selected", SelectedAnchors.size());
            if (Controls.Button("Select all anchors", 200 * Scale))
            {
                try
                {
                    const auto Path = DecodeCubicPath(Snapshot, First->Token);
                    PointToken = First->Token; SelectedAnchors.clear();
                    for (size_t Index = 0; Index < Path.Anchors.size(); ++Index) SelectedAnchors.push_back(Index);
                    ActiveAnchor = SelectedAnchors.back();
                }
                catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
            }
            ImGui::BeginDisabled(PointToken != First->Token || SelectedAnchors.empty());
            if (Controls.Button("Automatic selected tangents", 220 * Scale))
                EvaluateEdit([&](auto &Document)
                {
                    auto Path = DecodeCubicPath(Document, First->Token);
                    ConfigureAutomaticCubicTangents(Path, SelectedAnchors);
                    ReplaceCubicPath(Document, First->Token, Path);
                });
            Controls.Tooltip("Automatic selected tangents", "Derive aligned tangents from neighbouring anchor positions. Lengths use one third of each adjacent chord. Only selected anchors change; one undo restores the source.");
            for (bool Smooth : {false, true})
                if (Controls.Button(SelectedAnchors.size() > 1
                    ? (Smooth ? "Smooth selected anchors" : "Corner selected anchors")
                    : (Smooth ? "Smooth active anchor" : "Corner active anchor"), 200 * Scale))
                    EvaluateEdit([&](auto &Document)
                    {
                        auto Path = DecodeCubicPath(Document, First->Token);
                        for (size_t Index : SelectedAnchors) ConfigureCubicTangent(Path, Index, Smooth);
                        ReplaceCubicPath(Document, First->Token, Path);
                    });
            bool SegmentEligible = false;
            try
            {
                const auto Path = DecodeCubicPath(Snapshot, First->Token);
                SegmentEligible = ActiveAnchor < Path.Anchors.size() && Path.Anchors.size() < 400
                    && (Path.Closed || ActiveAnchor + 1 < Path.Anchors.size());
            }
            catch (const std::exception &) {}
            ImGui::BeginDisabled(!SegmentEligible);
            if (Controls.Button("Subdivide following segment", 220 * Scale))
                EvaluateEdit([&](auto &Document)
                {
                    auto Path = DecodeCubicPath(Document, First->Token);
                    SubdivideCubicSegment(Path, ActiveAnchor);
                    ReplaceCubicPath(Document, First->Token, Path);
                    ++ActiveAnchor; SelectedAnchors = {ActiveAnchor};
                });
            Controls.Tooltip("Subdivide segment", "Insert an anchor halfway along the following cubic using de Casteljau subdivision. The curve is preserved; one undo restores the source.");
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            bool ResumeEligible = false;
            try
            {
                const auto Path = DecodeCubicPath(Snapshot, First->Token);
                ResumeEligible = !Path.Closed && Path.Anchors.size() < 400;
            }
            catch (const std::exception &) {}
            ImGui::BeginDisabled(!ResumeEligible);
            for (bool FromStart : {true, false})
                if (Controls.Button(FromStart ? "Resume from start" : "Resume from end", 200 * Scale))
                {
                    try { ResumePen(First->Token, FromStart); }
                    catch (const std::exception &Failure) { Diagnostic = Failure.what(); }
                }
            ImGui::EndDisabled();
        }
        else ImGui::TextWrapped("Select an independent source path; aggregate expressions are not flattened for point editing.");
        ImGui::EndDisabled();
        Controls.EndCard();
        if (Selection != SelectedTokens) { ImGui::EndChild(); return; }
    }
    if (Selection != SelectedTokens) { ImGui::EndChild(); return; }
    Controls.BeginCard(MaskSelection ? "Vector mask" : "Selected component", true);
    ImGui::BeginDisabled(Protected);
    Controls.BeginCard("Mask", false, true);
    auto EditedLayer = *First;
    if (!EditedLayer.Mask)
    {
        if (Controls.Button("Add mask")) EvaluateEdit([&](auto &Document)
        {
            for (auto &Layer : Document.Layers) if (Layer.Token == First->Token)
            {
                auto Mask = Snapshot.Instructions[First->Root];
                if (Mask.Topology[0] > 6) { Mask = VectorCoverageInstruction{}; Mask.Bounds = QueryVectorAuthoringSelectionBounds(Snapshot, Selection); }
                Mask.Colour = Mask.ColourEnd = {1, 1, 1, 1}; Layer.Mask = Mask;
            }
        });
    }
    else
    {
        if (Controls.Button("Edit mask")) MaskSelection = First->Token;
        bool Changed = Controls.Toggle("Enable vector mask", EditedLayer.MaskEnabled);
        Changed |= Controls.Toggle("Invert vector mask", EditedLayer.MaskInverted);
        Changed |= Controls.Toggle("Luminance mask", EditedLayer.MaskLuminance);
        int Category = EditedLayer.Mask->Topology[0] == 1 ? 1 : 0;
        const char *Categories[]{"Rectangle", "Ellipse"};
        if (EditedLayer.Mask->Topology[0] <= 1 && Controls.Dropdown("Mask shape", Category, Categories)) { EditedLayer.Mask->Topology[0] = uint32_t(Category); Changed = true; }
        Changed |= Controls.Number("Mask opacity", EditedLayer.Mask->Transform[2], 0, 1);
        Changed |= Controls.Number("Mask X", EditedLayer.Mask->Bounds[0], -512, 1024);
        Changed |= Controls.Number("Mask Y", EditedLayer.Mask->Bounds[1], -512, 1024);
        Changed |= Controls.Number("Mask width", EditedLayer.Mask->Bounds[2], 1, 1024);
        Changed |= Controls.Number("Mask height", EditedLayer.Mask->Bounds[3], 1, 1024);
        if (Changed) EvaluateEdit([&](auto &Document) { for (auto &Layer : Document.Layers) if (Layer.Token == First->Token) Layer = EditedLayer; });
        if (Controls.Button("Remove mask")) EvaluateEdit([&](auto &Document) { for (auto &Layer : Document.Layers) if (Layer.Token == First->Token) Layer.Mask.reset(); });
        if (MaskSelection && Controls.Button("Done editing mask")) MaskSelection = 0;
        ImGui::TextDisabled("Paint/image mask authoring is not ported yet.");
    }
    Controls.EndCard();
    if (!MaskSelection)
    {
        Controls.BeginCard("Transform", false, true);
        char LayerName[81]; std::snprintf(LayerName, sizeof(LayerName), "%s", First->Name.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##Layer name", LayerName, sizeof(LayerName))) EvaluateEdit([&](auto &Document) { for (auto &Layer : Document.Layers) if (Layer.Token == First->Token) Layer.Name = LayerName; });
        const auto Bounds = QueryVectorAuthoringSelectionBounds(Snapshot, Selection);
        if (ImGui::BeginTable("Transform fields", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
        {
        ImGui::TableSetupColumn("Horizontal", ImGuiTableColumnFlags_WidthStretch, 1);
        ImGui::TableSetupColumn("Vertical", ImGuiTableColumnFlags_WidthStretch, 1);
        for (size_t Index = 0; Index < 4; ++Index)
        {
            if (Index % 2 == 0) ImGui::TableNextRow(ImGuiTableRowFlags_None, 80 * Scale);
            ImGui::TableNextColumn();
            float Quantity = Bounds[Index];
            const char *Captions[]{"X", "Y", "Width", "Height"};
            if (Controls.Number(Captions[Index], Quantity, Index < 2 ? -512.f : 1.f, 1024))
            {
                VectorAuthoringSourcePatch Patch;
                if (Index == 0) Patch.TranslationX = Quantity - Bounds[0];
                if (Index == 1) Patch.TranslationY = Quantity - Bounds[1];
                if (Index == 2) Patch.ScaleX = Quantity / std::max(.01f, Bounds[2]);
                if (Index == 3) Patch.ScaleY = Quantity / std::max(.01f, Bounds[3]);
                ApplySourcePatch(Patch);
            }
        }
        ImGui::EndTable();
        }
        const auto &Source = Snapshot.Instructions[First->Root];
        float Rotation = Source.Topology[0] <= 6 ? Source.Transform[0] * 180 / std::numbers::pi_v<float> : 0;
        if (Controls.Number("Rotation", Rotation, -360, 360)) { VectorAuthoringSourcePatch Patch; Patch.Rotation = Rotation * std::numbers::pi_v<float> / 180 - Source.Transform[0]; ApplySourcePatch(Patch); }
        // 📝 Polygon/Star stay this parametric corner-count category only until a corner is first dragged
        // with the Points tool - that one-time edit bakes it into a plain cubic path (see CubicPathEditing's
        // ReplaceCubicPath), after which it is an ordinary freeform shape and this field naturally disappears,
        // exactly like "outlining" a parametric shape in other vector editors.
        if (Source.Topology[0] == 4 || Source.Topology[0] == 5)
        {
            float Sides = std::clamp(Source.Paint[2], 3.f, 35.f);
            if (Controls.Range(Source.Topology[0] == 4 ? "Sides" : "Points", Sides, 3, 35))
            {
                VectorAuthoringSourcePatch Patch; Patch.PolygonSides = uint32_t(std::lround(Sides)); ApplySourcePatch(Patch);
            }
        }
        if (Source.Topology[0] == 5)
        {
            float InnerRadius = std::clamp(Source.Paint[1], .05f, .95f) * 100;
            if (Controls.Range("Inner radius %", InnerRadius, 5, 95))
            {
                VectorAuthoringSourcePatch Patch; Patch.StarInnerRadius = std::clamp(InnerRadius / 100.f, .05f, .95f); ApplySourcePatch(Patch);
            }
        }
        Controls.EndCard();
        Controls.BeginCard("Fill", false, true);
        int Category = int(Source.Transform[3]);
        const char *Sources[]{"Solid colour", "Linear gradient", "Radial gradient"};
        if (Controls.Dropdown("Component fill source", Category, Sources))
        {
            // 📝 Re-seeds the ramp's own axis/radius to the category-appropriate default (a radial
            // gradient centred on the shape vs. a linear one spanning it horizontally) every time the fill
            // source actually changes, rather than carrying over whatever the other mode's handles were
            // last dragged to - a bare centre-to-edge default would otherwise misplace a freshly chosen
            // radial gradient if the shape was just a linear one a moment ago, and vice versa.
            VectorAuthoringSourcePatch Patch; Patch.PaintCategory = uint32_t(Category);
            Patch.GradientAxis = Category == 2 ? std::array<float, 4>{0.f, 0.f, .5f, 0.f} : std::array<float, 4>{-.5f, 0.f, .5f, 0.f};
            ApplySourcePatch(Patch);
        }
        if (!Category)
        {
            auto Colour = Source.Colour;
            if (Controls.Colour("Fill colour", Colour)) { VectorAuthoringSourcePatch Patch; Patch.Colour = Colour; ApplySourcePatch(Patch); }
        }
        else
        {
            // 🎨 Interactive colour-ramp editor (mirrors MaterialLibraryPanel's flake-colour ramp): a
            // blended gradient preview bar with one draggable-on-canvas marker per stop underneath it,
            // click a marker to select it, Add/Remove stop buttons, and a colour + position field bound
            // to whichever stop is selected — kept in sync with the matching markers drawn on the shape
            // itself over in ConstructInterface (SelectedGradientStop is shared by both).
            auto Stops = QueryVectorGradientStops(Source);
            SelectedGradientStop = std::clamp(SelectedGradientStop, 0, int(Stops.size()) - 1);
            const float CardWidth = ImGui::GetContentRegionAvail().x;
            const ImVec2 BarOrigin = ImGui::GetCursorScreenPos();
            const float BarHeight = 18 * Scale;
            auto *RampInk = ImGui::GetWindowDrawList();
            const auto RampColour = [](const std::array<float, 4> &C) { return ImGui::GetColorU32(ImVec4(C[0], C[1], C[2], 1)); };
            RampInk->AddRectFilled(BarOrigin, ImVec2(BarOrigin.x + CardWidth, BarOrigin.y + BarHeight), IM_COL32(16, 16, 16, 255), 4 * Scale);
            for (size_t Index = 0; Index + 1 < Stops.size(); ++Index)
            {
                const float X0 = BarOrigin.x + Stops[Index].Position * CardWidth, X1 = BarOrigin.x + Stops[Index + 1].Position * CardWidth;
                RampInk->AddRectFilledMultiColor(ImVec2(X0, BarOrigin.y), ImVec2(X1, BarOrigin.y + BarHeight),
                    RampColour(Stops[Index].Colour), RampColour(Stops[Index + 1].Colour), RampColour(Stops[Index + 1].Colour), RampColour(Stops[Index].Colour));
            }
            RampInk->AddRect(BarOrigin, ImVec2(BarOrigin.x + CardWidth, BarOrigin.y + BarHeight), IM_COL32(255, 255, 255, 26), 4 * Scale);
            for (size_t Index = 0; Index < Stops.size(); ++Index)
            {
                const float MarkerX = BarOrigin.x + Stops[Index].Position * CardWidth;
                ImGui::SetCursorScreenPos(ImVec2(MarkerX - 6 * Scale, BarOrigin.y + BarHeight + 2 * Scale));
                ImGui::PushID(int(Index) + 500);
                if (ImGui::InvisibleButton("##GradientStop", ImVec2(12 * Scale, 8 * Scale))) SelectedGradientStop = int(Index);
                ImGui::PopID();
                const bool IsSelected = int(Index) == SelectedGradientStop;
                RampInk->AddTriangleFilled(ImVec2(MarkerX, BarOrigin.y + BarHeight + 2 * Scale),
                    ImVec2(MarkerX - 5 * Scale, BarOrigin.y + BarHeight + 9 * Scale), ImVec2(MarkerX + 5 * Scale, BarOrigin.y + BarHeight + 9 * Scale),
                    IsSelected ? IM_COL32(255, 255, 255, 255) : RampColour(Stops[Index].Colour));
            }
            ImGui::SetCursorScreenPos(ImVec2(BarOrigin.x, BarOrigin.y + BarHeight + 18 * Scale));
            auto SelectedColour = Stops[SelectedGradientStop].Colour;
            char StopLabel[24]; std::snprintf(StopLabel, sizeof(StopLabel), "Stop %d colour", SelectedGradientStop + 1);
            if (Controls.Colour(StopLabel, SelectedColour))
            {
                Stops[SelectedGradientStop].Colour = SelectedColour;
                VectorAuthoringSourcePatch Patch; ApplyVectorGradientStops(Patch, Stops); ApplySourcePatch(Patch);
            }
            float StopPosition = Stops[SelectedGradientStop].Position * 100;
            const float LowerBound = SelectedGradientStop > 0 ? Stops[SelectedGradientStop - 1].Position * 100 + 1 : 0.f;
            const float UpperBound = SelectedGradientStop + 1 < int(Stops.size()) ? Stops[SelectedGradientStop + 1].Position * 100 - 1 : 100.f;
            if (Controls.Number("Stop position %", StopPosition, LowerBound, UpperBound))
            {
                Stops[SelectedGradientStop].Position = std::clamp(StopPosition / 100.f, 0.f, 1.f);
                VectorAuthoringSourcePatch Patch; ApplyVectorGradientStops(Patch, Stops); ApplySourcePatch(Patch);
            }
            ImGui::BeginDisabled(Stops.size() >= size_t(VectorGradientStopCapacity));
            if (Controls.Button("Add stop", (CardWidth - 8 * Scale) * .5f))
            {
                float Previous = 0.f, BestGap = 0.f, BestPosition = .5f;
                for (size_t Index = 0; Index <= Stops.size(); ++Index)
                {
                    const float Current = Index < Stops.size() ? Stops[Index].Position : 1.f;
                    if (Current - Previous > BestGap) { BestGap = Current - Previous; BestPosition = (Current + Previous) * .5f; }
                    Previous = Current;
                }
                const auto NewColour = EvaluateVectorGradientColour(Stops, BestPosition);
                Stops.push_back({BestPosition, NewColour});
                std::sort(Stops.begin(), Stops.end(), [](const auto &A, const auto &B) { return A.Position < B.Position; });
                const auto Inserted = std::find_if(Stops.begin(), Stops.end(), [&](const auto &Stop) { return Stop.Position == BestPosition; });
                SelectedGradientStop = int(Inserted - Stops.begin());
                VectorAuthoringSourcePatch Patch; ApplyVectorGradientStops(Patch, Stops); ApplySourcePatch(Patch);
            }
            ImGui::EndDisabled();
            ImGui::SameLine(0, 8 * Scale);
            ImGui::BeginDisabled(Stops.size() <= 2);
            if (Controls.Button("Remove stop", (CardWidth - 8 * Scale) * .5f))
            {
                Stops.erase(Stops.begin() + SelectedGradientStop);
                SelectedGradientStop = std::clamp(SelectedGradientStop, 0, int(Stops.size()) - 1);
                VectorAuthoringSourcePatch Patch; ApplyVectorGradientStops(Patch, Stops); ApplySourcePatch(Patch);
            }
            ImGui::EndDisabled();
        }
        bool Filled = Source.Paint[3] != 0;
        if (Controls.Toggle("Fill enabled", Filled)) { VectorAuthoringSourcePatch Patch; Patch.FillEnabled = Filled; ApplySourcePatch(Patch); }
        Controls.EndCard();
        Controls.BeginCard("Stroke", false, true);
        // 📝 A category-6 cubic path (Pen/Polyline) alone can swing between an open line and a closed
        // filled shape during its own lifetime, so while it is closed+filled this edits its own independent
        // outline width (Paint[0]) rather than Transform[1] (the open line's own thickness) - closing a line
        // into a shape, or reopening it, never silently inherits or overwrites the other state's width.
        const bool ClosedFilledPath = Source.Topology[0] == 6 && Source.Paint[3] != 0;
        float Stroke = ClosedFilledPath ? Source.Paint[0] : Source.Transform[1];
        if (Controls.Number(ClosedFilledPath ? "Outline width" : "Stroke width", Stroke, 0, 25))
        {
            VectorAuthoringSourcePatch Patch;
            if (ClosedFilledPath) Patch.ClosedOutlineWidth = Stroke; else Patch.StrokeWidth = Stroke;
            ApplySourcePatch(Patch);
        }
        auto StrokeColour = Source.StrokeColour;
        if (Controls.Colour(ClosedFilledPath ? "Outline colour" : "Stroke colour", StrokeColour)) { VectorAuthoringSourcePatch Patch; Patch.StrokeColour = StrokeColour; ApplySourcePatch(Patch); }
        ImGui::TextWrapped(ClosedFilledPath
            ? "This outline width is independent of the same path's thickness while it was (or becomes) an open line."
            : "Fill and stroke colours are independent. Stroke image sources remain deferred.");
        Controls.EndCard();
    }
    ImGui::EndDisabled();
    Controls.EndCard();
    if (!Diagnostic.empty()) ImGui::TextWrapped("%s", Diagnostic.c_str());
    ImGui::EndChild();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SHARED HOSTING AND KEYBOARD INPUT
//------------------------------------------------------------------------------------------------------------------------

void StampWorkspacePanel::ConstructCloseDialogue()
{
    if (CloseRequested && !ImGui::IsPopupOpen("Unsaved stamp")) ImGui::OpenPopup("Unsaved stamp");
    ImGui::SetNextWindowSize(ImVec2(460 * Scale, 0));
    const StampModalScope ModalScope(Scale);
    if (ImGui::BeginPopupModal("Unsaved stamp", nullptr, ImGuiWindowFlags_NoResize))
    {
        const auto Dialogue = ImGui::GetCursorScreenPos();
        Caption(ImVec2(Dialogue.x, Dialogue.y + 4 * Scale), "UNSAVED STAMP", 7 * Scale, 0x8b8d79);
        Caption(ImVec2(Dialogue.x, Dialogue.y + 26 * Scale), "Save this stamp?", 17 * Scale, 0xdcdccd, LightTypeface);
        ImGui::Dummy(ImVec2(1, 58 * Scale));
        const auto &Name = QueryDocument().Name;
        ImGui::TextWrapped("\u201c%s\u201d has unsaved changes. Save keeps them; Discard restores the last save.",
            Name.empty() ? "Untitled stamp" : Name.c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        const float ButtonWidth = 128 * Scale, ButtonGap = 8 * Scale;
        ImGui::SetCursorPosX(ImGui::GetWindowSize().x - 3 * ButtonWidth - 2 * ButtonGap - 16 * Scale);
        if (Controls.Button("Cancel", ButtonWidth)) { CloseRequested = false; ImGui::CloseCurrentPopup(); if (CancelClose) CancelClose(); }
        ImGui::SameLine(0, ButtonGap);
        if (Controls.Button("Discard", ButtonWidth)) { History.Reset(SavedDocument); CloseRequested = false; ImGui::CloseCurrentPopup(); if (ActivateMaterial) ActivateMaterial(); }
        ImGui::SameLine(0, ButtonGap);
        if (Controls.Button("Save and close", ButtonWidth, true) && Save()) { CloseRequested = false; ImGui::CloseCurrentPopup(); if (ActivateMaterial) ActivateMaterial(); }
        ImGui::EndPopup();
    }
}

void StampWorkspacePanel::SuspendContent() noexcept
{
    if (GestureSource) Selection = std::move(GestureSelection);
    GestureSource.reset(); GestureDraft.reset(); Gesture = 0; GesturePrimitiveToken = 0; ++GestureRevision;
}

void StampWorkspacePanel::FinalizeContent() noexcept
{
    SuspendContent();
    QueryCoverage = {}; QueryCoverageReady = {}; QuerySelectionToken = {}; SaveSource = {}; LoadSource = {}; ActivateMaterial = {}; ActivatePattern = {}; ActivateBaking = {}; CancelClose = {}; EvaluateNavigationDelta = {};
}

void StampWorkspacePanel::ConstructInterface()
{
    if (QueryPhase() != ActivationPhase::Active) return;
    ControlExtents.clear(); PanelExtents.clear();
    const auto SavedPalette = ControlKit::Palette();
    const auto Palette = PanelChrome::StudioPalette(SavedPalette, {.20f, .208f, .173f, 1}, {.39f, .45f, .31f, 1});
    ControlKit::ConfigurePalette(Palette);
    ImGui::PushFont(static_cast<ImFont *>(RegularTypeface), 11 * 1.302f * Scale);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Spectrum(0x121212));
    ImGui::PushStyleColor(ImGuiCol_Text, ThemeQuad(Palette.Text));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ThemeQuad(Palette.TextDim));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Spectrum(0x090909));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, Spectrum(0x191919));
    ImGui::PushStyleColor(ImGuiCol_Header, Spectrum(0x191919));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Spectrum(0x272923));
    ImGui::PushStyleColor(ImGuiCol_Button, Spectrum(0x222222));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Spectrum(0x2e2e28));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Spectrum(0x33352c));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, .055f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(32, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6 * Scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10 * Scale, 9 * Scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6 * Scale, 10 * Scale));
    // Shared dock chrome (PanelChrome) - pushed last here, popped first in the tail below.
    const auto Chrome = PanelChrome::PushDockChrome(Spectrum(0x090909), Spectrum(0x101010), true, true, Scale);
    ConstructHeader();
    Panels.ConstructInterface(Scale);
    ConstructCloseDialogue();
    const auto &Input = ImGui::GetIO();
    if (Gesture == 0 && !Input.WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
    {
        const bool Command = Input.KeyCtrl || Input.KeySuper;
        if (Command && ImGui::IsKeyPressed(ImGuiKey_C)) TransferSelection(false);
        if (Command && ImGui::IsKeyPressed(ImGuiKey_X)) TransferSelection(true);
        if (Command && ImGui::IsKeyPressed(ImGuiKey_V)) InsertSelection(Input.KeyShift);
        if (Command && ImGui::IsKeyPressed(ImGuiKey_D)) ReplicateSelection();
        if (Command && ImGui::IsKeyPressed(ImGuiKey_A)) SelectEditableVectors();
        if (!Command && (ImGui::IsKeyPressed(ImGuiKey_LeftBracket) || ImGui::IsKeyPressed(ImGuiKey_RightBracket)))
        {
            const bool Forward = ImGui::IsKeyPressed(ImGuiKey_RightBracket);
            const auto Order = Input.KeyShift ? (Forward ? VectorSelectionOrder::Front : VectorSelectionOrder::Back)
                : (Forward ? VectorSelectionOrder::Forward : VectorSelectionOrder::Backward);
            if (!Selection.empty() && !MaskSelection) EvaluateEdit([&](auto &Document) { ReorderVectorAuthoringSelection(Document, Selection, Order); });
        }
        if (Input.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) RestoreHistory(Input.KeyShift);
        if (Input.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) RestoreHistory(true);
        if (Input.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) Save();
        if (!Command && ImGui::IsKeyPressed(ImGuiKey_V)) Tool = VectorAuthoringTool::Select;
        if (!Command && ImGui::IsKeyPressed(ImGuiKey_R)) Tool = VectorAuthoringTool::Rectangle;
        if (!Command && ImGui::IsKeyPressed(ImGuiKey_O)) Tool = VectorAuthoringTool::Ellipse;
        if (!Command && ImGui::IsKeyPressed(ImGuiKey_T)) Tool = VectorAuthoringTool::Text;
        if (!Command && ImGui::IsKeyPressed(ImGuiKey_A)) Tool = VectorAuthoringTool::Points;
        if (!Command && ImGui::IsKeyPressed(ImGuiKey_P)) Tool = VectorAuthoringTool::Pen;
        if (!Command && ImGui::IsKeyPressed(ImGuiKey_L)) Tool = VectorAuthoringTool::Polyline;
        if (ImGui::IsKeyPressed(ImGuiKey_0)) { Pan = {}; Zoom = 1; }
        if (!Selection.empty() && ImGui::IsKeyPressed(ImGuiKey_Delete))
        {
            if (MaskSelection)
            {
                EvaluateEdit([&](auto &Document) { for (auto &Layer : Document.Layers) if (Layer.Token == MaskSelection && !QueryVectorAuthoringProtection(Document, Layer)) Layer.Mask.reset(); });
                MaskSelection = 0;
            }
            else EvaluateEdit([&](auto &Document) { PartitionVectorAuthoringSelection(Document, Selection); });
        }
        if (!Selection.empty())
        {
            VectorAuthoringSourcePatch Patch;
            const float Step = Input.KeyShift ? 10.f : 1.f;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) Patch.TranslationX -= Step;
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) Patch.TranslationX += Step;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) Patch.TranslationY -= Step;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) Patch.TranslationY += Step;
            if (Patch.TranslationX || Patch.TranslationY) ApplySourcePatch(Patch);
        }
    }
    PanelChrome::PopDockChrome(Chrome);
    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor(11);
    ImGui::PopFont();
    ControlKit::ConfigurePalette(SavedPalette);
}
}
