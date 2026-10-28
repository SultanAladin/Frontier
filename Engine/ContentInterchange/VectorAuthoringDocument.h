/*============================================================================================================================================
                                                       VECTORAUTHORINGDOCUMENT.H
============================================================================================================================================*/
// 🧩 Retain editable GPU expressions and bounded undo snapshots independently of raster resources and workspace presentation.

#pragma once
#include "Engine/Render/VectorCoverage.h"
#include "VectorAuthoringProperties.h"
#include "VectorAuthoringArrayPlacement.h"
#include <string>
#include <optional>
#include <algorithm>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     SHARED AUTHORING PROPERTIES
//------------------------------------------------------------------------------------------------------------------------

struct VectorAuthoringLayer
{
    uint32_t Token = 0, Root = 0;
    std::string Name;
    bool Visible = true, Locked = false;
    std::optional<VectorCoverageInstruction> Mask;
    bool MaskInverted = false, MaskLuminance = false, MaskEnabled = true;
    uint32_t EnclosureToken = 0;
    VectorAuthoringSurfaceChannels SurfaceChannels = ConstructVectorAuthoringSurfaceChannels();
    VectorAuthoringSurfaceStructure SurfaceStructure = VectorAuthoringSurfaceStructure::Freeform;
    VectorAuthoringTextileGeometry TextileGeometry = VectorAuthoringTextileGeometry::Unclassified;
    float SurfaceRelief = 1.f;
    // 📝 Stamp-only provenance: ComponentKind stays Freeform (and SourceMarkup/EmbeddedImage stay
    // empty) for every ordinary Pattern layer, so this adds no footprint outside Stamp documents.
    VectorAuthoringComponentKind ComponentKind = VectorAuthoringComponentKind::Freeform;
    std::string SourceMarkup;     // Text run contents, or the original sanitized SVG markup
    std::string EmbeddedImage;    // "data:image/png;base64,..." for ImportedImage components, same shape as MaterialAuthoringTexture::DataUrl
    // 📝 The point size a Text component's outline was last (re)baked at, so editing content/size
    // later (Inspector field or double-click-to-re-edit on canvas) can regenerate the glyph outline
    // at a known, user-adjustable size instead of guessing one back from the baked bounds.
    float TextPointSize = 64.f;
    // 📝 Blender-style modifier stack, evaluated live (never mutating this layer's own Root/Instructions)
    // by ConstructEvaluatedVectorAuthoringDocument whenever at least one entry is Enabled. Empty for every
    // ordinary layer - only populated once a user adds a modifier (Array, Mirror, Noise, Offset, Colour)
    // from the Inspector's Modifiers section. Applying an entry bakes it into real
    // duplicate/replacement layers via the matching Generate* function and erases it; Deleting just erases.
    std::vector<VectorAuthoringModifier> Modifiers;
    bool operator==(const VectorAuthoringLayer &) const = default;
};
inline constexpr size_t VectorAuthoringSourceMarkupLimit = 32000; // [B] - Matches the HTML outline-command/markup bound
struct VectorAuthoringDocument
{
    std::string Name = "Untitled vector";
    bool Stamp = false;
    std::vector<VectorCoverageInstruction> Instructions;
    std::vector<VectorCubicSegment> Segments;
    std::vector<VectorAuthoringLayer> Layers;
    uint32_t TokenClock = 0;
    std::vector<VectorAuthoringEnclosure> Enclosures;
    bool operator==(const VectorAuthoringDocument &) const = default;
};
void ValidateVectorAuthoringOrganization(const VectorAuthoringDocument &Document);
bool QueryVectorAuthoringVisibility(const VectorAuthoringDocument &Document, const VectorAuthoringLayer &Layer);
bool QueryVectorAuthoringProtection(const VectorAuthoringDocument &Document, const VectorAuthoringLayer &Layer);

inline VectorCoverageProgram ConstructVectorAuthoringCoverage(const VectorAuthoringDocument &Document)
{
    if (Document.Layers.size() > (Document.Stamp ? 24u : 64u) || Document.Name.size() > 80) throw std::runtime_error("VectorAuthoring exceeds workspace limits.");
    ValidateVectorAuthoringOrganization(Document);
    VectorCoverageProgram Program{Document.Instructions, Document.Segments, 0};
    std::vector<uint32_t> Tokens;
    std::optional<uint32_t> Output;
    for (const auto &Layer : Document.Layers)
    {
        if (!Layer.Token || Layer.Name.size() > 80 || Layer.Root >= Document.Instructions.size() || std::find(Tokens.begin(), Tokens.end(), Layer.Token) != Tokens.end()) throw std::runtime_error("Invalid vector layer identity or source.");
        if (Layer.Token > Document.TokenClock) throw std::runtime_error("VectorAuthoring identity clock precedes a layer token.");
        Tokens.push_back(Layer.Token);
        if (Layer.Mask) ValidateVectorCoverage(VectorCoverageProgram{{*Layer.Mask}, Document.Segments, 0});
        if (!QueryVectorAuthoringVisibility(Document, Layer)) continue;
        uint32_t Root = Layer.Root;
        if (Layer.Mask && Layer.MaskEnabled)
        {
            const uint32_t Mask = uint32_t(Program.Instructions.size());
            Program.Instructions.push_back(*Layer.Mask);
            VectorCoverageInstruction Operation;
            Operation.Topology = {Layer.MaskLuminance ? 25u : 24u, Root, Mask, Layer.MaskInverted ? 1u : 0u};
            Root = uint32_t(Program.Instructions.size()); Program.Instructions.push_back(Operation);
        }
        if (Output)
        {
            VectorCoverageInstruction Operation; Operation.Topology = {28, *Output, Root, 0};
            Root = uint32_t(Program.Instructions.size()); Program.Instructions.push_back(Operation);
        }
        Output = Root;
    }
    if (!Output)
    {
        if (!Program.Instructions.empty()) ValidateVectorCoverage(Program);
        VectorCoverageInstruction Transparent; Transparent.Transform[2] = 0;
        Program.Instructions = {Transparent}; Program.Segments.clear(); Output = 0;
    }
    Program.Root = *Output; ValidateVectorCoverage(Program); return Program;
}
class VectorAuthoringHistory
{
public:
    explicit VectorAuthoringHistory(bool Stamp = false) { Document.Stamp = Stamp; }
    bool Finalize(VectorAuthoringDocument Replacement)
    {
        if (Replacement.Stamp != Document.Stamp) throw std::runtime_error("VectorAuthoring history cannot change its workspace contract.");
        (void)ConstructVectorAuthoringCoverage(Replacement);
        if (Replacement == Document) return false;
        UndoSequence.push_back(Document);
        if (UndoSequence.size() > 80) UndoSequence.erase(UndoSequence.begin());
        RedoSequence.clear(); Document = std::move(Replacement); ++Revision; LastCoalesceTag = 0; return true;
    }
    // 📝 Live-edit variant of Finalize for continuous controls (modifier sliders): the first call with a
    // fresh CoalesceTag records one undo entry exactly like Finalize; subsequent calls carrying the
    // SAME tag replace the current document in place (still ++Revision, still re-rendered) without
    // piling an undo entry per drag frame - so one slider drag is one undo step, Blender-style.
    // Returns 0 = unchanged, 1 = replaced live (no new undo entry), 2 = recorded (new undo entry).
    // Any untagged Finalize/RestoreHistory/Reset breaks the chain (LastCoalesceTag reset), and the
    // caller rotates tags between distinct drags so separate gestures stay separate undo steps.
    int FinalizeCoalesced(VectorAuthoringDocument Replacement, uint64_t CoalesceTag)
    {
        if (Replacement.Stamp != Document.Stamp) throw std::runtime_error("VectorAuthoring history cannot change its workspace contract.");
        (void)ConstructVectorAuthoringCoverage(Replacement);
        if (Replacement == Document) return 0;
        if (CoalesceTag != 0 && CoalesceTag == LastCoalesceTag && !UndoSequence.empty())
        {
            RedoSequence.clear(); Document = std::move(Replacement); ++Revision; return 1;
        }
        UndoSequence.push_back(Document);
        if (UndoSequence.size() > 80) UndoSequence.erase(UndoSequence.begin());
        RedoSequence.clear(); Document = std::move(Replacement); ++Revision; LastCoalesceTag = CoalesceTag; return 2;
    }
    bool RestoreHistory(bool Redo)
    {
        auto &Source = Redo ? RedoSequence : UndoSequence;
        auto &Destination = Redo ? UndoSequence : RedoSequence;
        if (Source.empty()) return false;
        Destination.push_back(Document); Document = std::move(Source.back()); Source.pop_back(); ++Revision; LastCoalesceTag = 0; return true;
    }
    void Reset(VectorAuthoringDocument Replacement)
    {
        if (Replacement.Stamp != Document.Stamp) throw std::runtime_error("VectorAuthoring history cannot change workspace contract.");
        (void)ConstructVectorAuthoringCoverage(Replacement);
        Document = std::move(Replacement);
        UndoSequence.clear();
        RedoSequence.clear();
        LastCoalesceTag = 0;
        ++Revision;
    }
    [[nodiscard]] bool QueryUndoAvailable() const noexcept { return !UndoSequence.empty(); }
    [[nodiscard]] bool QueryRedoAvailable() const noexcept { return !RedoSequence.empty(); }
    [[nodiscard]] const VectorAuthoringDocument &QueryDocument() const noexcept { return Document; }
    [[nodiscard]] uint64_t QueryRevision() const noexcept { return Revision; }
private:
    VectorAuthoringDocument Document;
    std::vector<VectorAuthoringDocument> UndoSequence, RedoSequence;
    uint64_t Revision = 0;
    uint64_t LastCoalesceTag = 0; // tag of the latest FinalizeCoalesced record; 0 = no live chain
};
std::string EncodeVectorAuthoring(const VectorAuthoringDocument &Document);
VectorAuthoringDocument DecodeVectorAuthoring(std::string_view Source);
}
