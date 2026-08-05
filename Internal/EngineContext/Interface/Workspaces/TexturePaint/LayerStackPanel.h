/*==============================================================================================================================================
                                                              LAYERSTACKPANEL.H
==============================================================================================================================================*/
// 🧩 The paint-layer stack rail: an ordered column of layer rows, each of which folds open into an inline properties card. Ported 1:1 from the
//    approved prototype Documentation/Prototypes/TexturePaintLayerStack.html, which is the SOLE source of truth for every metric, tone and
//    behaviour here. UI-ONLY: this panel owns ordering, focus, naming, visibility, opacity, blend vocabulary and the mask's authored intent, and
//    NOTHING about GPU atlases — a row records what the user asked for, and the paint extension allocates storage when it is wired.
//
//    🔴 A ROW AND ITS OPEN CARD ARE ONE CARD, not a row above a panel. The ROW IS THE CARD'S HEADER — it is the thing the user clicks to open the
//       card, and it already carries the name, the classification/blend/channel line, the opacity pill and the visibility eye. There is
//       deliberately NO second header bar inside the body: one was built during the port review and the user rejected it as a duplicate header
//       stacked under the real one. The body opens straight onto its section cards. Every "two stacked boxes" reading is the bug.
//
//    🔴 Ordinal 0 is the TOP of the stack, matching the prototype and every paint tool's presentation. A compositor therefore walks the store
//       BACKWARD. Storing bottom-first would read more naturally in a compositor and would invert the rail on screen, which is the more
//       visible wrong.
//
// 📝 Three fold LEVELS nest here (row card → section card → Layermask disclosure) and all three run on the SAME eased-fraction mechanism that
//    PropertyPanelBase.cpp uses for its cards: intent is a caller-owned bool, the eased 0..1 fraction and the measured natural body height live
//    beside it, and the body is clipped to Height * Fraction. 🔴 They CANNOT be BeginPropertyCard, which keeps its pending-card state in file-
//    static single slots and opens an ImDrawList channel split — so it does not nest. LayerStackPanel.cpp therefore carries its own nestable
//    fold built on the same FoldRate, rather than a second animation model.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_TEXTUREPAINT_LAYERSTACKPANEL_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACES_TEXTUREPAINT_LAYERSTACKPANEL_H

#include "../../Theme/ThemeConfiguration.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier { struct SvgIconRegistry; }

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                              TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 A stable, stale-detecting identity for one layer (SKILL-Naming §4). Issued by a monotonic counter in the state and never reused, so a
//    deleted layer's token can never alias a live one. 0 is the null token.
//
// 🔴 Every rail action addresses a layer by TOKEN, never by its ordinal in the store. A reorder or a delete renumbers every ordinal below the
//    change, so an ordinal captured before a mutation names a different layer afterwards — which is exactly how a drag ends up deleting the
//    wrong row.
using PaintLayerToken = std::uint32_t;


//------------------------------------------------------------------------------------------------------------------------
//                                                          CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Layers are capped because each one costs three full channel atlases once the paint extension is wired. At 1024² that is 12 MiB per layer,
//    so twelve layers is 144 MiB of paint — already past what a modest integrated adapter hands out without complaint. The cap is a guard rail
//    with a clear readout in the footer, not a silent allocation failure. Ported verbatim from the prototype's LayerCapacity.
constexpr int PaintLayerCapacity = 12;

// 🔴 The ORDER of this table IS the composite shader's blend enum: a layer stores the ordinal, and a shader switches on that integer. Inserting
//    or reordering an entry silently remaps every layer's blend mode in every existing document. Append only.
constexpr int PaintBlendModeCount = 7;

// 📝 How many kinds the add-layer catalogue offers. Ported from the prototype's LAYER_KIND_ORDER.
constexpr int PaintLayerCategoryCount = 4;

// 📝 How many identity hues the rail tag cycles through before repeating. Ported from LAYER_COLOUR_PALETTE, whose hues alternate around the
//    wheel so CONSECUTIVE entries contrast in a 4px swatch.
constexpr int PaintIdentityTintCount = 12;


//------------------------------------------------------------------------------------------------------------------------
//                                                              ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 What a layer's content IS, which decides its glyph, its tint, which channels it deposits into, and whether creating it floods the surface
//    at full coverage. Ported from the prototype's LAYER_KINDS. The integer values are serialized, so they are pinned.
enum class PaintLayerCategory : int
{
    Brushwork = 0,   // [-] - Stroked by hand; starts empty and earns storage on its first dab
    Flood     = 1,   // [-] - A uniform authored value across the whole surface; flooded on creation
    Material  = 2,   // [-] - An instance of an authored material preset; flooded on creation
    Generator = 3    // [-] - Procedurally evaluated from parameters; starts empty
};


// 📝 The tone a fresh mask floods with. 🔴 A new mask is WHITE — fully revealing — so enabling one changes nothing on screen until the user
//    paints into it. A Black default would make the layer vanish the moment a mask was added, which reads as a bug rather than as a tool.
enum class PaintMaskFill : int
{
    White = 0,   // [-] - Fully revealing
    Black = 1    // [-] - Fully concealing
};


//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One layer's mask as the card's Layermask disclosure presents it. UI-side only: no atlas, no painted texels — the disclosure records the
//    authored intent and the paint extension realises it once wired.
struct PaintLayerMask
{
    bool          Present;      // [-]   - false leaves the section offering only "Add mask"
    PaintMaskFill Fill;         // [-]   - The flooded tone
    bool          Inverted;     // [-]   - An independent flip layered on top of Fill (they are NOT one three-way choice)
    float         Strength;     // [0-1] - How strongly the mask acts
};


// 📝 One layer as the rail presents it. This is the UI-side record: ordering, identity and the properties a row's card edits. No atlas, no view,
//    no mask storage — those belong to the paint extension and are deliberately absent from this port.
struct PaintLayerEntry
{
    PaintLayerToken    Token;             // [-]   - Stable identity issued by the state's token cursor
    std::string        Label;             // [-]   - Display name (editable via inline rename)
    PaintLayerCategory Category;          // [-]   - What the layer's content is (glyph + tint + channels + flood behaviour)
    int                BlendOrdinal;      // [idx] - Index into the blend-mode table; 0 is Normal
    float              Opacity;           // [0-1] - How strongly the layer composites over what is beneath it
    int                ChannelCount;      // [-]   - How many channels the layer deposits into (rail readout + Surface section)
    std::uint32_t      IssueOrdinal;      // [-]   - Creation ordinal; drives the identity tint (see ResolveLayerIdentityTint)
    bool              ConcealedState;     // [-]   - Visibility toggle (true = suppressed, the eye is struck through)

    // 📝 The AUTHORED base colour, 0-1 RGB — what the row's 30x30 thumbnail shows as a flat swatch. 🔴 Only meaningful when the category
    //    deposits baseColour; PaintsBaseColour reports that, and a layer that paints none falls back to a classification glyph in the thumb.
    float              BaseColour[3];     // [0-1] - Authored base colour
    bool               PaintsBaseColour;  // [-]   - Whether BaseColour reaches an atlas at all

    // 📝 The per-layer IDENTITY tint the 4px rail tag carries, packed ImU32. Seeded from the palette walk at creation and overridable by the
    //    card's Tag field. 🔴 Distinct from the CATEGORY tint: the tag answers "WHICH layer", the thumbnail answers "WHAT KIND".
    std::uint32_t      IdentityTint;      // [-]   - Packed ImU32 rail-tag colour
    bool               IdentityTintAuthored; // [-] - true once the user picked one, so the palette walk stops answering

    PaintLayerMask     Mask;              // [-]   - The Layermask disclosure's state
};


// 📝 One fold's animation state: the eased reveal fraction plus the measured natural height of the body it discloses. One of these exists per
//    row card, per section card and per Layermask — the three nesting levels all run on this same shape.
//
// 🔴 `Seeded` is load-bearing, not an optimisation: it snaps the fraction to the intent the FIRST time a fold is seen, so a card that is shown
//    already-open does not animate in. Without it every rebuild replays the whole open animation. This is exactly PropertyPanelBase.cpp's
//    CardFoldState guard, and the prototype's SeededExpands / SeededSections / SeededMaskFolds sets are the CSS equivalent of it.
struct PaintFoldMotion
{
    float Fraction = 0.0f;     // [0-1] - Eased reveal fraction
    float BodyEdge = 0.0f;     // [px]  - Measured natural (unfolded) body height
    bool  Seeded   = false;    // [-]   - The fraction was aligned to the intent once
};


// 📝 The per-layer fold intents + their motion. Keyed by token in the state's FoldStore, so a delete drops the whole record and a token that is
//    never reissued cannot mis-seed a future layer.
//
// 📝 Section intents default to OPEN, matching BeginPropertyCard's Expanded bool starting true in ControlsGalleryState — so a freshly built
//    card needs no seeding pass. The prototype stores the exception (`SectionShut`) for the same reason.
struct PaintLayerFoldRecord
{
    bool            CardOpen        = false;   // [-] - The row's caret folded its properties card open
    bool            AppearanceOpen  = true;    // [-] - The Appearance section card
    bool            SurfaceOpen     = true;    // [-] - The Surface section card
    bool            MaskSectionOpen = true;    // [-] - The Mask section card
    bool            LayermaskOpen   = false;   // [-] - The Layermask disclosure INSIDE the Mask section

    PaintFoldMotion CardMotion;                // [-] - Motion of the row's properties card
    PaintFoldMotion AppearanceMotion;          // [-] - Motion of the Appearance section
    PaintFoldMotion SurfaceMotion;             // [-] - Motion of the Surface section
    PaintFoldMotion MaskSectionMotion;         // [-] - Motion of the Mask section
    PaintFoldMotion LayermaskMotion;           // [-] - Motion of the Layermask disclosure
};


// 📝 One token's fold record, paired for the flat association store below. A flat vector beats a map here: the stack is capped at 12, so a
//    linear scan is faster than hashing and keeps the state trivially copyable for a document snapshot.
struct PaintLayerFoldSlot
{
    PaintLayerToken      Token;    // [-] - Which layer the record belongs to
    PaintLayerFoldRecord Record;   // [-] - Its fold intents + motion
};


// 📝 All caller-owned state the rail edits in place. One instance lives in the driver for the life of the window; the panel reads and writes only
//    through it, so the drawing itself stays stateless and a second instance draws a second document.
struct LayerStackPanelState
{
    // -- The backing store + its identity cursor --
    //
    // 🔴 Ordinal 0 is the TOP of the stack (see the file header).
    std::vector<PaintLayerEntry> LayerStore;                  // [-]   - Ordered layers, top-first
    PaintLayerToken              NextToken       = 1;         // [-]   - Monotonic token cursor (0 reserved as null)
    std::uint32_t                IssuedOrdinal   = 0;         // [-]   - Layers ever created; drives the identity-tint walk

    // -- Focus: the ONE layer a stroke would land on --
    //
    // 🔴 Never falls back to "the top layer" when unset. A stroke with no target must be refused, not silently redirected onto whatever happens
    //    to sit on top — the prototype's Stack.FocusToken carries the same contract, and the validation host prints it to prove so.
    PaintLayerToken              FocusToken      = 0;         // [-]   - Focused layer (0 = none)

    // -- Fold intents + motion, one record per live token --
    //
    // 🔴 A SET of open cards, not a single token: several rows can be open at once, so every fold operation is scoped to the token it belongs
    //    to. Pruned against the live store each cycle so a deleted layer leaks nothing.
    std::vector<PaintLayerFoldSlot> FoldStore;                // [-]   - Per-layer fold state

    // -- Inline rename in flight --
    PaintLayerToken              RenameTarget    = 0;         // [-]   - Token under inline rename (0 = none)
    char                         RenameBuffer[64] = "";       // [-]   - Scratch text for the active rename
    bool                         RenameJustOpened = false;    // [-]   - One-shot keyboard-focus request for the rename field

    // -- Opacity scrub in flight (a horizontal drag on the row's percentage pill) --
    //
    // 📝 The grabbed value is latched when the drag opens so the scrub accumulates against the ORIGINAL opacity. Reading the live value each
    //    frame instead compounds every delta into the next frame's base, which accelerates the drag the longer it runs.
    PaintLayerToken              ScrubTarget     = 0;         // [-]   - Token whose opacity is being dragged (0 = none)
    float                        ScrubGrabbedOpacity = 0.0f;  // [0-1] - Opacity latched when the drag opened
    float                        ScrubOriginX    = 0.0f;      // [px]  - Absolute x the drag opened at

    // -- Reorder drag in flight --
    //
    // 📝 DragMoved distinguishes a click from a drag: without it, pressing a row to focus it also counts as a zero-distance reorder and the
    //    click handler is swallowed.
    PaintLayerToken              DragToken       = 0;         // [-]   - Token being dragged (0 = none)
    bool                         DragMoved       = false;     // [-]   - The pointer travelled far enough to count as a drag
    int                          DropOrdinal     = -1;        // [idx] - Where a completed drop would seat it (-1 = nowhere)

    // -- The add-layer catalogue + the row context menu --
    bool                         AddMenuRequested = false;    // [-]   - Open the add-layer popup this cycle
    PaintLayerToken              RowMenuTarget   = 0;         // [-]   - Layer whose context menu should open this cycle

    // -- The blend-mode popup, opened by a row's caret --
    //
    // 📝 Two fields, not one: the REQUEST is a one-shot that opens the popup on the cycle the caret is clicked, while the TARGET outlives it
    //    for as long as the popup is up so the menu keeps editing the row it was opened from. Collapsing them into a single token loses the
    //    open edge and the popup never appears.
    PaintLayerToken              BlendMenuTarget   = 0;       // [-]   - Layer the open blend popup edits (0 = closed)
    bool                         BlendMenuRequested = false;  // [-]   - Open the blend popup this cycle

    // -- Live name-substring filter --
    //
    // 🔴 A filtered rail is a LOOKUP, not an editing surface: reorder drag is suppressed AND no card folds open while a filter is active,
    //    because the ordinals the user can see are not the ordinals the store holds and a drop would land somewhere they did not aim at.
    char                         FilterText[64]  = "";        // [-]   - Empty shows the whole stack
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The blend-mode table. 🔴 Its ORDER is the shader enum — see PaintBlendModeCount. Returns a borrowed static array of PaintBlendModeCount
//    labels, so a caller can name an ordinal without owning the vocabulary.
const char* const* ResolveBlendModeTable();

// 📝 The display label of one layer category ("Paint", "Fill", "Material", "Generator"). Out-of-range yields "Paint".
const char* ResolveLayerCategoryLabel(PaintLayerCategory Category);

// 📝 The CLASSIFICATION label the row's meta line leads with ("Brushwork", "Flood", "Material", "Generator") — the prototype's
//    CLASSIFICATION_LABEL. Distinct from the category label above, which names the kind the user picked from the catalogue.
const char* ResolveLayerClassificationLabel(PaintLayerCategory Category);

// 📝 The packed tint of one layer category — what the row's THUMBNAIL glyph is drawn in when the layer paints no base colour. Shared by every
//    layer of that kind, so it answers "what KIND of layer is this" and deliberately cannot serve as the per-layer marker.
std::uint32_t ResolveLayerCategoryTint(PaintLayerCategory Category);

// 📝 How many channels a category deposits into. Ported from LAYER_KINDS' Channels arrays: Flood + Material carry baseColour, roughness and
//    metallic; Brushwork + Generator carry baseColour alone.
int ResolveCategoryChannelCount(PaintLayerCategory Category);

// 📝 Whether a category deposits baseColour at all — which decides whether the card's Surface section offers a Paint row. 🔴 A layer with no
//    baseColour channel has no colour to author, so the row is OMITTED rather than shown editing a value that reaches no atlas.
bool ResolveCategoryBaseColourCondition(PaintLayerCategory Category);

// 📝 The per-layer IDENTITY tint — the rail tag that answers "WHICH layer is this", distinct from the category tint above.
//
// 🔴 Indexed off the issue ORDINAL, never off the layer's position in the store. A position-derived colour re-shuffles every tag on a reorder or
//    a delete, so the marker the user just learned to recognise jumps to a different layer — the one thing an identity colour must never do. An
//    issue ordinal is handed out once and never reused, so a layer keeps its colour for life.
std::uint32_t ResolveLayerIdentityTint(std::uint32_t IssueOrdinal);

// 📝 Whether creating a layer of this category floods the whole surface at full coverage (Flood + Material) or leaves it empty (Brushwork +
//    Generator). UI-side only in this port: the rail records the intent, and the paint extension performs the flood once wired.
bool ResolveCategoryFloodCondition(PaintLayerCategory Category);

// 📝 The greyscale tone a mask resolves to, reading Fill and Inverted together — the row's mask chip and the Layermask's tone dot. 🔴 Both of
//    those readouts exist because a folded-away mask would otherwise be invisible, so a shut Layermask would look identical whether it was
//    revealing everything or hiding everything.
std::uint32_t ResolveMaskChipTone(const PaintLayerMask& Mask);


// 📝 Resolve a token to its entry, or nullptr when the token is stale / null. The terse form every rail action opens with.
PaintLayerEntry* ResolveLayer(LayerStackPanelState& State, PaintLayerToken Token);

// 📝 The ordinal of a token in the store, or -1 when absent. 🔴 Never cached across a mutation (see PaintLayerToken).
int ResolveLayerOrdinal(const LayerStackPanelState& State, PaintLayerToken Token);

// 📝 The focused layer, or nullptr when focus is unset. 🔴 Never falls back to the top layer.
PaintLayerEntry* ResolveFocusedLayer(LayerStackPanelState& State);

// 📝 Resolve (lazily creating) the fold record for one token. Returns nullptr only for the null token.
PaintLayerFoldRecord* ResolveLayerFold(LayerStackPanelState& State, PaintLayerToken Token);


// 📝 Insert one layer of the given category ABOVE the focused layer (or at the top when focus is unset), focus it, and return its token.
//    Returns 0 when the stack is already at PaintLayerCapacity — the caller reports the cap, nothing is created.
//
// 🔴 The cap is enforced HERE, in the verb, not at the call site: a rail that only greys its button still creates layers through any second
//    caller, and the allocation the cap exists to prevent happens anyway.
PaintLayerToken IntegrateLayer(LayerStackPanelState& State, PaintLayerCategory Category, const char* Label);

// 📝 Drop one layer and re-home focus onto the layer that took its place (or the one above, when it was the last).
//
// 🔴 The last remaining layer is refused: it is the substrate every other layer composites over, and removing it lets the resolve show through
//    to the clear colour. The prototype's ReclaimLayer carries the same guard. Returns false when refused or the token is stale.
bool ReclaimLayer(LayerStackPanelState& State, PaintLayerToken Token);

// 📝 Move one layer by a single position: Direction -1 raises it toward the top (ordinal 0), +1 lowers it. Returns false when the move would
//    leave the store.
bool ReorderLayer(LayerStackPanelState& State, PaintLayerToken Token, int Direction);

// 📝 Lift one layer out of its ordinal and re-seat it at TargetOrdinal, sliding everything between. This is what a completed drag applies.
//    Returns false when either end is out of range.
bool RelocateLayer(LayerStackPanelState& State, PaintLayerToken Token, int TargetOrdinal);

// 📝 Point focus at one layer — the layer a stroke would land on. Returns false when the token is stale.
bool AlignFocus(LayerStackPanelState& State, PaintLayerToken Token);

// 📝 How many layers are currently concealed — the footer's "n hidden" readout.
int AccumulateConcealedCount(const LayerStackPanelState& State);


// 📝 Seed the rail's opening document: a flooded white base plus one empty brushwork layer above it, with focus on the brushwork layer.
//
// 🔴 A CLEAN WHITE SURFACE, not a pre-dressed demo stack. A seeded material stack looks better in a screenshot but it is the wrong starting
//    point for a painting tool: every first stroke lands on top of somebody else's material and the user cannot tell their own paint from the
//    seed. White also keeps the brush colour honest — paint over grey and every colour reads dark. Ported from SeedLayerStack.
void InitializeLayerStackSample(LayerStackPanelState& State);

// 📝 Draw the whole rail inside the current ImGui window: the "Layers" head with its capacity count, the Add Layer strip, the filter, the layer
//    rows with their inline properties cards, and the footer tally. Handles focus, inline rename, the visibility eye, the opacity scrub, reorder
//    drag, delete, the add-layer + context popups, and all three fold levels.
//
//    IconRegistry supplies the resolved SVG glyph textures for the eye and the chevron (global "g-" tier); a null / empty registry falls back to
//    procedural strokes so the rail still renders when the registry failed to start. 🔴 The mask and plus marks are ALWAYS procedural — the
//    global tier carries no g-mask or g-plus, whatever the prototype's comment claimed.
void ConstructLayerStackPanel(const ThemeConfiguration& Theme,
                              LayerStackPanelState&     State,
                              const SvgIconRegistry*    IconRegistry);

}   // namespace Frontier

#endif
