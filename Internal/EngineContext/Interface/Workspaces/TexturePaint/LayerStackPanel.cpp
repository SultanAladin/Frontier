/*==============================================================================================================================================
                                                             LAYERSTACKPANEL.CPP
==============================================================================================================================================*/
// 🧩 The paint-layer stack rail's vocabulary tables, ordering verbs and drawing. Ported UI-only from the PaintingSurface prototype's stack rail:
//    the row anatomy (identity tag · kind glyph · name · meta line · opacity readout · eye · caret), the add-layer catalogue, the reorder drag and
//    the capacity footer. Every colour and metric comes from the passed ThemeConfiguration, so the rail matches the ControlsGallery styling with
//    no palette of its own.

#include "LayerStackPanel.h"

#include "../../Icons/SvgIconRegistry.h"

#include "imgui.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      VOCABULARY TABLES
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 🔴 The ORDER here IS the composite shader's blend enum — see PaintBlendModeCount in the header. Append only.
    const char* const BlendModeTable[PaintBlendModeCount] =
    {
        "Normal", "Multiply", "Screen", "Overlay", "Add", "Darken", "Linear Dodge"
    };

    // 📝 One row per category: label, glyph tint, glyph key, and whether creation floods the surface. Ported from LAYER_KINDS.
    struct CategoryRow
    {
        const char*   Label;            // [-] - Display name in the rail + the add catalogue
        std::uint32_t GlyphTint;        // [-] - Packed tint the row's glyph is drawn in
        const char*   IconKey;          // [-] - SvgIconRegistry key of the glyph
        bool          FloodCondition;   // [-] - Creation floods the whole surface at full coverage
    };

    const CategoryRow CategoryTable[PaintLayerCategoryCount] =
    {
        { "Paint",     IM_COL32(249, 115,  22, 255), "paint-brushwork", false },   // Brushwork — #f97316
        { "Fill",      IM_COL32( 59, 130, 246, 255), "paint-flood",     true  },   // Flood     — #3b82f6
        { "Material",  IM_COL32(139,  92, 246, 255), "paint-material",  true  },   // Material  — #8b5cf6
        { "Generator", IM_COL32( 16, 185, 129, 255), "paint-generator", false }    // Generator — #10b981
    };

    // 🔴 Hues are ordered so CONSECUTIVE entries CONTRAST: the table alternates around the wheel rather than walking it, because issue ordinals
    //    are handed out consecutively and two neighbouring tags differing by 20° of hue read as the same colour in a 4 px-wide swatch. Ported
    //    verbatim from the prototype's LAYER_COLOUR_PALETTE.
    constexpr int IdentityTintCount = 12;
    const std::uint32_t IdentityTintTable[IdentityTintCount] =
    {
        IM_COL32( 79, 178, 134, 255),   // teal    #4fb286
        IM_COL32(224, 160,  60, 255),   // amber   #e0a03c
        IM_COL32(124, 108, 240, 255),   // violet  #7c6cf0
        IM_COL32(224, 104,  95, 255),   // coral   #e0685f
        IM_COL32( 63, 169, 212, 255),   // cyan    #3fa9d4
        IM_COL32(199, 111, 184, 255),   // orchid  #c76fb8
        IM_COL32(157, 189,  79, 255),   // olive   #9dbd4f
        IM_COL32(217, 120,  74, 255),   // rust    #d9784a
        IM_COL32( 91, 140, 255, 255),   // blue    #5b8cff
        IM_COL32( 87, 192, 122, 255),   // green   #57c07a
        IM_COL32(176, 118,  58, 255),   // bronze  #b0763a
        IM_COL32(141, 143, 168, 255)    // slate   #8d8fa8
    };

    // Clamp a category to the table so a corrupt serialized integer draws as Paint rather than reading past the end.
    int ResolveCategoryOrdinal(PaintLayerCategory Category)
    {
        const int Ordinal = (int)Category;
        return (Ordinal < 0 || Ordinal >= PaintLayerCategoryCount) ? 0 : Ordinal;
    }
}

const char* const* ResolveBlendModeTable() { return BlendModeTable; }

const char* ResolveLayerCategoryLabel(PaintLayerCategory Category)
{
    return CategoryTable[ResolveCategoryOrdinal(Category)].Label;
}

std::uint32_t ResolveLayerCategoryTint(PaintLayerCategory Category)
{
    return CategoryTable[ResolveCategoryOrdinal(Category)].GlyphTint;
}

const char* ResolveLayerCategoryIconKey(PaintLayerCategory Category)
{
    return CategoryTable[ResolveCategoryOrdinal(Category)].IconKey;
}

bool ResolveCategoryFloodCondition(PaintLayerCategory Category)
{
    return CategoryTable[ResolveCategoryOrdinal(Category)].FloodCondition;
}

std::uint32_t ResolveLayerIdentityTint(std::uint32_t IssueOrdinal)
{
    return IdentityTintTable[IssueOrdinal % (std::uint32_t)IdentityTintCount];
}


//------------------------------------------------------------------------------------------------------------------------
//                                                        STORE RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

PaintLayerEntry* ResolveLayer(LayerStackPanelState& State, PaintLayerToken Token)
{
    if (Token == 0) return nullptr;

    for (PaintLayerEntry& Entry : State.LayerStore)
    {
        if (Entry.Token == Token) return &Entry;
    }
    return nullptr;
}

int ResolveLayerOrdinal(const LayerStackPanelState& State, PaintLayerToken Token)
{
    if (Token == 0) return -1;

    for (int Ordinal = 0; Ordinal < (int)State.LayerStore.size(); Ordinal += 1)
    {
        if (State.LayerStore[Ordinal].Token == Token) return Ordinal;
    }
    return -1;
}

// 🔴 No fallback to the top layer — an unset focus resolves to nullptr so a caller must refuse the stroke.
PaintLayerEntry* ResolveFocusedLayer(LayerStackPanelState& State)
{
    return ResolveLayer(State, State.FocusToken);
}

int AccumulateConcealedCount(const LayerStackPanelState& State)
{
    int Concealed = 0;
    for (const PaintLayerEntry& Entry : State.LayerStore)
    {
        if (Entry.ConcealedState) Concealed += 1;
    }
    return Concealed;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                        ORDERING VERBS
//------------------------------------------------------------------------------------------------------------------------

PaintLayerToken IntegrateLayer(LayerStackPanelState& State, PaintLayerCategory Category, const char* Label)
{
    // 🔴 The cap is enforced HERE, in the verb, not at the call site. A rail that only greys its button still creates layers through the
    //    keyboard path or a second caller, and the allocation the cap exists to prevent happens anyway.
    if ((int)State.LayerStore.size() >= PaintLayerCapacity) return 0;

    PaintLayerEntry Entry  = {};
    Entry.Token            = State.NextToken;
    Entry.Label            = (Label != nullptr && Label[0] != '\0') ? Label : ResolveLayerCategoryLabel(Category);
    Entry.Category         = Category;
    Entry.BlendOrdinal     = 0;                                              // Normal
    Entry.Opacity          = 1.0f;
    Entry.ChannelCount     = ResolveCategoryFloodCondition(Category) ? 3 : 1;
    Entry.IdentityTint     = ResolveLayerIdentityTint(State.IssuedOrdinal);
    Entry.ConcealedState   = false;

    State.NextToken     += 1;
    State.IssuedOrdinal += 1;

    // 📝 A new layer lands ABOVE the focused one, which is where a painter expects the next layer to go. With focus unset it lands on top.
    const int FocusOrdinal = ResolveLayerOrdinal(State, State.FocusToken);
    const int Seat         = (FocusOrdinal < 0) ? 0 : FocusOrdinal;

    State.LayerStore.insert(State.LayerStore.begin() + Seat, Entry);
    State.FocusToken = Entry.Token;
    return Entry.Token;
}

bool ReclaimLayer(LayerStackPanelState& State, PaintLayerToken Token)
{
    const int Ordinal = ResolveLayerOrdinal(State, Token);
    if (Ordinal < 0) return false;

    // 🔴 The bottom layer is the substrate every other layer composites over. Removing it leaves nothing opaque underneath and the resolve shows
    //    through to the clear colour, so it is refused rather than allowed to produce a confusing result.
    if (State.LayerStore.size() <= 1) return false;

    State.LayerStore.erase(State.LayerStore.begin() + Ordinal);

    // Re-home focus onto whichever layer took its place, or the one above when it was the last.
    if (State.FocusToken == Token)
    {
        const int Landing = (Ordinal < (int)State.LayerStore.size()) ? Ordinal : (int)State.LayerStore.size() - 1;
        State.FocusToken  = (Landing >= 0) ? State.LayerStore[Landing].Token : 0;
    }

    // Any interaction addressing the dropped token has to be dropped with it, or the next frame scrubs / renames a layer that is gone.
    if (State.RenameTarget    == Token) State.RenameTarget    = 0;
    if (State.ScrubTarget     == Token) State.ScrubTarget     = 0;
    if (State.DragToken       == Token) State.DragToken       = 0;
    if (State.BlendMenuTarget == Token) State.BlendMenuTarget = 0;
    return true;
}

bool ReorderLayer(LayerStackPanelState& State, PaintLayerToken Token, int Direction)
{
    const int Ordinal = ResolveLayerOrdinal(State, Token);
    if (Ordinal < 0) return false;

    const int Target = Ordinal + Direction;
    if (Target < 0 || Target >= (int)State.LayerStore.size()) return false;

    const PaintLayerEntry Lifted = State.LayerStore[Ordinal];
    State.LayerStore.erase(State.LayerStore.begin() + Ordinal);
    State.LayerStore.insert(State.LayerStore.begin() + Target, Lifted);
    return true;
}

bool RelocateLayer(LayerStackPanelState& State, PaintLayerToken Token, int TargetOrdinal)
{
    const int Ordinal = ResolveLayerOrdinal(State, Token);
    if (Ordinal < 0) return false;
    if (TargetOrdinal < 0 || TargetOrdinal >= (int)State.LayerStore.size()) return false;
    if (TargetOrdinal == Ordinal) return true;

    const PaintLayerEntry Lifted = State.LayerStore[Ordinal];
    State.LayerStore.erase(State.LayerStore.begin() + Ordinal);
    State.LayerStore.insert(State.LayerStore.begin() + TargetOrdinal, Lifted);
    return true;
}

bool AlignFocus(LayerStackPanelState& State, PaintLayerToken Token)
{
    if (ResolveLayer(State, Token) == nullptr) return false;

    State.FocusToken = Token;
    return true;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                        SEEDED SAMPLE
//------------------------------------------------------------------------------------------------------------------------

void InitializeLayerStackSample(LayerStackPanelState& State)
{
    State.LayerStore.clear();
    State.NextToken     = 1;
    State.IssuedOrdinal = 0;
    State.FocusToken    = 0;

    // 🔴 Seeded BOTTOM-FIRST because IntegrateLayer inserts above focus (and at ordinal 0 while focus is unset). Adding these in listed order
    //    would stand the stack on its head and the white base would cover the paint layer completely.
    //
    // 📝 The base is a FLOOD, not a brushwork layer: its content is one authored value across the whole surface, including the gutters between UV
    //    islands. A base that stopped at the island edges would let the clear colour bleed in under bilinear sampling.
    const PaintLayerToken Base = IntegrateLayer(State, PaintLayerCategory::Flood, "Base \xE2\x80\x94 White");
    PaintLayerEntry* BaseEntry = ResolveLayer(State, Base);
    if (BaseEntry != nullptr) { BaseEntry->ChannelCount = 3; }

    // An empty brushwork layer on top, so the very first stroke has somewhere legal to land.
    const PaintLayerToken Brushwork = IntegrateLayer(State, PaintLayerCategory::Brushwork, "Paint 1");

    AlignFocus(State, Brushwork);
}


//------------------------------------------------------------------------------------------------------------------------
//                                                        ROW GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Every horizontal band of a row, measured once so the drawing and the hit-testing can never disagree. A row that drew its eye from one
    //    expression and tested the click against another is the classic "the icon moved but the hot zone didn't" defect.
    struct RowGeometry
    {
        ImVec2 RowMin;          // [px] - Top-left of the whole row
        ImVec2 RowMax;          // [px] - Bottom-right of the whole row
        float  CentreY;         // [px] - Vertical midline every glyph aligns to
        float  TagX;            // [px] - Left edge of the identity tag stripe
        float  GlyphX;          // [px] - Left edge of the category glyph
        float  TextX;           // [px] - Left edge of the name + meta column
        float  TextRight;       // [px] - Right edge the name clips against
        float  OpacityX;        // [px] - Left edge of the opacity readout
        float  EyeCentreX;      // [px] - Centre of the visibility eye
        float  CaretCentreX;    // [px] - Centre of the blend-mode caret
    };

    constexpr float TagWidth      = 4.0f;    // [px] - Identity stripe width
    constexpr float GlyphBox      = 18.0f;   // [px] - Category glyph edge
    constexpr float OpacityWidth  = 40.0f;   // [px] - "100%" readout column
    constexpr float EyeZone       = 24.0f;   // [px] - Eye hit band
    constexpr float CaretZone     = 20.0f;   // [px] - Caret hit band
    constexpr float RowGutter     = 8.0f;    // [px] - Gap between row bands

    RowGeometry ResolveRowGeometry(ImVec2 CursorTop, float RowLeft, float RowRight, float RowHeight)
    {
        RowGeometry Geometry  = {};
        Geometry.RowMin       = ImVec2(RowLeft,  CursorTop.y);
        Geometry.RowMax       = ImVec2(RowRight, CursorTop.y + RowHeight);
        Geometry.CentreY      = CursorTop.y + RowHeight * 0.5f;

        Geometry.TagX         = RowLeft + 6.0f;
        Geometry.GlyphX       = Geometry.TagX + TagWidth + RowGutter;
        Geometry.TextX        = Geometry.GlyphX + GlyphBox + RowGutter;

        Geometry.CaretCentreX = RowRight - 6.0f  - CaretZone * 0.5f;
        Geometry.EyeCentreX   = Geometry.CaretCentreX - CaretZone * 0.5f - EyeZone * 0.5f;
        Geometry.OpacityX     = Geometry.EyeCentreX - EyeZone * 0.5f - OpacityWidth;
        Geometry.TextRight    = Geometry.OpacityX - RowGutter;
        return Geometry;
    }

    // Draw one category glyph from the registry, falling back to a procedural mark so the rail still reads when the registry never started.
    void ConstructCategoryGlyph(ImDrawList*            DrawList,
                                const SvgIconRegistry* IconRegistry,
                                const char*            IconKey,
                                ImVec2                 Origin,
                                ImU32                  Tint)
    {
        const ImTextureID Texture = (IconRegistry != nullptr) ? ResolveIconTexture(*IconRegistry, IconKey) : (ImTextureID)0;
        if (Texture != 0)
        {
            DrawList->AddImage(Texture, Origin, ImVec2(Origin.x + GlyphBox, Origin.y + GlyphBox),
                               ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), Tint);
            return;
        }

        // 📝 The fallback is deliberately NOT a copy of the artwork — a rounded swatch with a struck corner reads as "glyph missing" rather than
        //    passing for a real icon, so a failed registry is visible instead of quietly shipping worse art.
        const ImVec2 Corner = ImVec2(Origin.x + GlyphBox, Origin.y + GlyphBox);
        DrawList->AddRectFilled(Origin, Corner, (Tint & 0x00FFFFFF) | (60 << 24), 3.0f);
        DrawList->AddRect(Origin, Corner, Tint, 3.0f, 0, 1.4f);
    }

    // 📝 The eye, matched to the outliner's so one visibility affordance reads the same everywhere it appears.
    void ConstructVisibilityEye(ImDrawList* DrawList, ImVec2 Centre, ImU32 Tone, bool Concealed)
    {
        DrawList->AddEllipse(Centre, ImVec2(6.0f, 4.0f), Tone, 0.0f, 0, 1.2f);
        if (Concealed)
        {
            DrawList->AddLine(ImVec2(Centre.x - 6.0f, Centre.y - 5.0f), ImVec2(Centre.x + 6.0f, Centre.y + 5.0f), Tone, 1.3f);
        }
        else
        {
            DrawList->AddCircle(Centre, 1.8f, Tone, 0, 1.2f);
        }
    }

    void ConstructCaret(ImDrawList* DrawList, ImVec2 Centre, ImU32 Tone)
    {
        DrawList->AddLine(ImVec2(Centre.x - 3.5f, Centre.y - 2.0f), ImVec2(Centre.x, Centre.y + 2.0f), Tone, 1.6f);
        DrawList->AddLine(ImVec2(Centre.x, Centre.y + 2.0f), ImVec2(Centre.x + 3.5f, Centre.y - 2.0f), Tone, 1.6f);
    }

    bool WithinBand(float MouseX, float Centre, float Width)
    {
        return MouseX >= Centre - Width * 0.5f && MouseX <= Centre + Width * 0.5f;
    }

    // Case-insensitive substring test for the name filter, so "BASE" finds "Base — White".
    bool LabelMatchesFilter(const std::string& Label, const char* Filter)
    {
        if (Filter == nullptr || Filter[0] == '\0') return true;

        const size_t LabelLength  = Label.size();
        const size_t FilterLength = std::strlen(Filter);
        if (FilterLength > LabelLength) return false;

        for (size_t Start = 0; Start + FilterLength <= LabelLength; Start += 1)
        {
            size_t Offset = 0;
            while (Offset < FilterLength)
            {
                const char Left  = (char)std::tolower((unsigned char)Label[Start + Offset]);
                const char Right = (char)std::tolower((unsigned char)Filter[Offset]);
                if (Left != Right) break;
                Offset += 1;
            }
            if (Offset == FilterLength) return true;
        }
        return false;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                        RAIL DRAWING
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 What one row's interaction decided, applied AFTER the row loop closes. A row must never erase or reorder the store it is being iterated
    //    over — 🔴 a mid-loop mutation invalidates the reference the row is still drawing from, and the loop reads freed memory.
    struct RailVerdict
    {
        PaintLayerToken ReclaimToken   = 0;   // [-]   - Layer to drop
        PaintLayerToken RelocateToken  = 0;   // [-]   - Layer a completed drag lifted
        int             RelocateSeat   = -1;  // [idx] - Ordinal the drag dropped it onto
    };

    void ConstructFilterField(const ThemeConfiguration& Theme, LayerStackPanelState& State, float FieldWidth)
    {
        const ColorPaletteDescriptor& Palette = Theme.Palette;

        ImGui::SetNextItemWidth(FieldWidth);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Palette.ControlBackground);
        ImGui::PushStyleColor(ImGuiCol_Text,    Palette.TextPrimary);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Theme.Metrics.CornerRounding);
        ImGui::InputTextWithHint("##layerfilter", "Filter layers", State.FilterText, sizeof(State.FilterText));
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }

    void ConstructRow(const ThemeConfiguration& Theme,
                      LayerStackPanelState&     State,
                      const SvgIconRegistry*    IconRegistry,
                      PaintLayerEntry&          Entry,
                      int                       Ordinal,
                      bool                      FilterActive,
                      float                     RowLeft,
                      float                     RowRight,
                      float                     RowHeight,
                      RailVerdict&              Verdict)
    {
        const ColorPaletteDescriptor& Palette = Theme.Palette;
        ImDrawList* DrawList = ImGui::GetWindowDrawList();

        const ImVec2      CursorTop = ImGui::GetCursorScreenPos();
        const RowGeometry Geometry  = ResolveRowGeometry(CursorTop, RowLeft, RowRight, RowHeight);
        const bool        Focused   = (State.FocusToken == Entry.Token);
        const bool        Concealed = Entry.ConcealedState;

        ImGui::PushID((int)Entry.Token);
        ImGui::SetCursorScreenPos(Geometry.RowMin);
        ImGui::InvisibleButton("##layerrow", ImVec2(RowRight - RowLeft, RowHeight));
        const bool Hovered = ImGui::IsItemHovered();

        // -- Backgrounds: focus carries an accent bar, hover only a tint --
        if (Focused)
        {
            DrawList->AddRectFilled(Geometry.RowMin, Geometry.RowMax, Palette.ControlActive, Theme.Metrics.CornerRounding);
            DrawList->AddRectFilled(Geometry.RowMin, ImVec2(Geometry.RowMin.x + 2.0f, Geometry.RowMax.y), Palette.AccentPrimary);
        }
        else if (Hovered)
        {
            DrawList->AddRectFilled(Geometry.RowMin, Geometry.RowMax, Palette.ControlHovered, Theme.Metrics.CornerRounding);
        }

        // -- Reorder drag --
        //
        // 🔴 Suppressed while a filter is active: the ordinals on screen are not the ordinals in the store, so a drop lands somewhere the user
        //    did not aim at. A filtered rail is a lookup surface, not an editing one.
        if (!FilterActive)
        {
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
            {
                State.DragToken = Entry.Token;
                PaintLayerToken Dragged = Entry.Token;
                ImGui::SetDragDropPayload("PAINT_LAYER_ROW", &Dragged, sizeof(PaintLayerToken));
                ImGui::TextUnformatted(Entry.Label.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget())
            {
                DrawList->AddRectFilled(Geometry.RowMin, Geometry.RowMax, Palette.AccentSubtle, Theme.Metrics.CornerRounding);
                DrawList->AddRect(Geometry.RowMin, Geometry.RowMax, Palette.AccentPrimary, Theme.Metrics.CornerRounding, 0, 1.0f);

                const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("PAINT_LAYER_ROW");
                if (Payload != nullptr && Payload->DataSize == (int)sizeof(PaintLayerToken))
                {
                    Verdict.RelocateToken = *(const PaintLayerToken*)Payload->Data;
                    Verdict.RelocateSeat  = Ordinal;
                    State.DragToken       = 0;
                }
                ImGui::EndDragDropTarget();
            }
        }

        // -- Identity tag: WHICH layer this is, held for the layer's whole life (see ResolveLayerIdentityTint) --
        const ImU32 TagTone = Concealed ? ((Entry.IdentityTint & 0x00FFFFFF) | (90 << 24)) : Entry.IdentityTint;
        DrawList->AddRectFilled(ImVec2(Geometry.TagX, Geometry.RowMin.y + 4.0f),
                                ImVec2(Geometry.TagX + TagWidth, Geometry.RowMax.y - 4.0f), TagTone, 2.0f);

        // -- Category glyph: what KIND of layer this is --
        ImU32 GlyphTone = ResolveLayerCategoryTint(Entry.Category);
        if (Concealed) { GlyphTone = (GlyphTone & 0x00FFFFFF) | (100 << 24); }
        ConstructCategoryGlyph(DrawList, IconRegistry, ResolveLayerCategoryIconKey(Entry.Category),
                               ImVec2(Geometry.GlyphX, Geometry.CentreY - GlyphBox * 0.5f), GlyphTone);

        // -- Name (or the inline rename field) + the meta line beneath it --
        const float NameY = Geometry.CentreY - ImGui::GetFontSize() - 1.0f;
        if (State.RenameTarget == Entry.Token)
        {
            ImGui::SetCursorScreenPos(ImVec2(Geometry.TextX, Geometry.RowMin.y + 4.0f));
            ImGui::SetNextItemWidth(Geometry.TextRight - Geometry.TextX);
            if (State.RenameJustOpened) { ImGui::SetKeyboardFocusHere(); State.RenameJustOpened = false; }

            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 255));
            ImGui::PushStyleColor(ImGuiCol_Border,  Palette.AccentPrimary);
            const bool Committed = ImGui::InputText("##layerrename", State.RenameBuffer, sizeof(State.RenameBuffer),
                                                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            ImGui::PopStyleColor(2);

            // 📝 A blur commits as well as Enter. Discarding on blur loses a rename the user believes they finished, which is the worse surprise.
            if (Committed || ImGui::IsItemDeactivatedAfterEdit())
            {
                if (State.RenameBuffer[0] != '\0') { Entry.Label = State.RenameBuffer; }
                State.RenameTarget = 0;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { State.RenameTarget = 0; }
        }
        else
        {
            const ImU32 NameTone = Concealed ? Palette.TextMuted : Palette.TextPrimary;
            DrawList->PushClipRect(ImVec2(Geometry.TextX, Geometry.RowMin.y), ImVec2(Geometry.TextRight, Geometry.RowMax.y), true);
            DrawList->AddText(ImVec2(Geometry.TextX, NameY), NameTone, Entry.Label.c_str());

            // "Paint · Multiply · 3 ch" — the three facts a painter checks without opening anything.
            char MetaLine[96];
            std::snprintf(MetaLine, sizeof(MetaLine), "%s \xC2\xB7 %s \xC2\xB7 %d ch",
                          ResolveLayerCategoryLabel(Entry.Category),
                          ResolveBlendModeTable()[(Entry.BlendOrdinal < 0 || Entry.BlendOrdinal >= PaintBlendModeCount) ? 0 : Entry.BlendOrdinal],
                          Entry.ChannelCount);
            DrawList->AddText(ImVec2(Geometry.TextX, Geometry.CentreY + 1.0f), Palette.TextMuted, MetaLine);
            DrawList->PopClipRect();
        }

        // -- Opacity readout, scrubbed by dragging horizontally across it --
        char OpacityLabel[16];
        std::snprintf(OpacityLabel, sizeof(OpacityLabel), "%d%%", (int)(Entry.Opacity * 100.0f + 0.5f));
        const ImVec2 OpacityExtent = ImGui::CalcTextSize(OpacityLabel);
        const bool   Scrubbing     = (State.ScrubTarget == Entry.Token);
        const bool   OverOpacity   = Hovered && ImGui::GetIO().MousePos.x >= Geometry.OpacityX
                                             && ImGui::GetIO().MousePos.x <= Geometry.OpacityX + OpacityWidth;

        if (Scrubbing || OverOpacity)
        {
            DrawList->AddRectFilled(ImVec2(Geometry.OpacityX, Geometry.CentreY - 9.0f),
                                    ImVec2(Geometry.OpacityX + OpacityWidth, Geometry.CentreY + 9.0f),
                                    Palette.SliderTrack, 3.0f);
            DrawList->AddRectFilled(ImVec2(Geometry.OpacityX, Geometry.CentreY - 9.0f),
                                    ImVec2(Geometry.OpacityX + OpacityWidth * Entry.Opacity, Geometry.CentreY + 9.0f),
                                    Palette.SliderFill, 3.0f);
        }
        DrawList->AddText(ImVec2(Geometry.OpacityX + OpacityWidth - OpacityExtent.x - 3.0f, Geometry.CentreY - OpacityExtent.y * 0.5f),
                          Concealed ? Palette.TextMuted : Palette.ValueText, OpacityLabel);

        if (OverOpacity && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            // 🔴 The grabbed opacity is latched HERE, once. Re-reading the live value each frame compounds every delta into the next frame's base
            //    and the drag accelerates the longer it runs.
            State.ScrubTarget          = Entry.Token;
            State.ScrubGrabbedOpacity  = Entry.Opacity;
            State.ScrubOriginX         = ImGui::GetIO().MousePos.x;
        }
        if (Scrubbing)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                const float Travel  = ImGui::GetIO().MousePos.x - State.ScrubOriginX;
                const float Settled = State.ScrubGrabbedOpacity + Travel / 160.0f;   // 160 px spans the full 0-1 range
                Entry.Opacity = (Settled < 0.0f) ? 0.0f : ((Settled > 1.0f) ? 1.0f : Settled);
            }
            else
            {
                State.ScrubTarget = 0;
            }
        }

        // -- Visibility eye --
        const ImVec2 EyeCentre = ImVec2(Geometry.EyeCentreX, Geometry.CentreY);
        if (Hovered || Concealed)
        {
            ConstructVisibilityEye(DrawList, EyeCentre, Concealed ? Palette.TextMuted : Palette.TextPrimary, Concealed);
        }

        // -- Blend-mode caret --
        const ImVec2 CaretCentre = ImVec2(Geometry.CaretCentreX, Geometry.CentreY);
        if (Hovered || State.BlendMenuTarget == Entry.Token)
        {
            ConstructCaret(DrawList, CaretCentre, Palette.TextMuted);
        }

        // -- Click routing: the narrow bands win, the rest of the row focuses --
        //
        // 📝 Tested right-to-left so the eye and caret claim their bands before the row-wide focus does. Ordering these the other way makes the
        //    eye unclickable, because the whole-row branch swallows the press first.
        if (Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const float MouseX = ImGui::GetIO().MousePos.x;
            if (WithinBand(MouseX, Geometry.CaretCentreX, CaretZone))
            {
                State.BlendMenuTarget    = Entry.Token;
                State.BlendMenuRequested = true;
            }
            else if (WithinBand(MouseX, Geometry.EyeCentreX, EyeZone))
            {
                Entry.ConcealedState = !Entry.ConcealedState;
            }
            else if (!OverOpacity && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                AlignFocus(State, Entry.Token);
            }
        }
        if (Hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
            && ImGui::GetIO().MousePos.x >= Geometry.TextX && ImGui::GetIO().MousePos.x <= Geometry.TextRight)
        {
            State.RenameTarget = Entry.Token;
            State.RenameJustOpened = true;
            std::snprintf(State.RenameBuffer, sizeof(State.RenameBuffer), "%s", Entry.Label.c_str());
        }
        if (Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            AlignFocus(State, Entry.Token);
            ImGui::OpenPopup("##layerrowmenu");
        }

        // -- Per-row context menu: delete lives here rather than as a row glyph, which would put a destructive action one stray click away --
        if (ImGui::BeginPopup("##layerrowmenu"))
        {
            if (ImGui::MenuItem("Rename"))
            {
                State.RenameTarget = Entry.Token;
                State.RenameJustOpened = true;
                std::snprintf(State.RenameBuffer, sizeof(State.RenameBuffer), "%s", Entry.Label.c_str());
            }
            if (ImGui::MenuItem("Raise", nullptr, false, Ordinal > 0))
            {
                Verdict.RelocateToken = Entry.Token;
                Verdict.RelocateSeat  = Ordinal - 1;
            }
            if (ImGui::MenuItem("Lower", nullptr, false, Ordinal + 1 < (int)State.LayerStore.size()))
            {
                Verdict.RelocateToken = Entry.Token;
                Verdict.RelocateSeat  = Ordinal + 1;
            }
            ImGui::Separator();

            // The bottom layer is the substrate; ReclaimLayer refuses it, so the menu shows that refusal rather than offering a no-op.
            const bool Removable = State.LayerStore.size() > 1;
            if (ImGui::MenuItem("Delete", nullptr, false, Removable))
            {
                Verdict.ReclaimToken = Entry.Token;
            }
            ImGui::EndPopup();
        }

        ImGui::PopID();
        ImGui::SetCursorScreenPos(ImVec2(CursorTop.x, CursorTop.y + RowHeight));
    }
}


void ConstructLayerStackPanel(const ThemeConfiguration& Theme,
                              LayerStackPanelState&     State,
                              const SvgIconRegistry*    IconRegistry)
{
    const ColorPaletteDescriptor& Palette = Theme.Palette;
    const ThemeMetrics&           Metrics = Theme.Metrics;

    const float Padding   = Metrics.PanelPadding;
    const float RowHeight = Metrics.RowHeight > 0.0f ? Metrics.RowHeight * 1.6f : 34.0f;   // Two text lines per row, so taller than an outliner row

    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    const ImVec2 PanelMin = ImGui::GetCursorScreenPos();
    const float  PanelWidth = ImGui::GetContentRegionAvail().x;

    DrawList->AddRectFilled(PanelMin, ImVec2(PanelMin.x + PanelWidth, PanelMin.y + ImGui::GetContentRegionAvail().y),
                            Palette.PanelBackground);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Metrics.ControlSpacing, Metrics.ControlSpacing));
    ImGui::Dummy(ImVec2(0.0f, Padding * 0.5f));

    // -- Header strip --
    {
        const ImVec2 HeaderMin = ImVec2(PanelMin.x, ImGui::GetCursorScreenPos().y);
        const ImVec2 HeaderMax = ImVec2(PanelMin.x + PanelWidth, HeaderMin.y + Metrics.ControlHeight);
        DrawList->AddRectFilled(HeaderMin, HeaderMax, Palette.PanelHeader);
        DrawList->AddText(ImVec2(HeaderMin.x + Padding, HeaderMin.y + (Metrics.ControlHeight - ImGui::GetFontSize()) * 0.5f),
                          Palette.TextPrimary, "Layers");

        char Tally[32];
        std::snprintf(Tally, sizeof(Tally), "%d / %d", (int)State.LayerStore.size(), PaintLayerCapacity);
        const ImVec2 TallyExtent = ImGui::CalcTextSize(Tally);
        DrawList->AddText(ImVec2(HeaderMax.x - Padding - TallyExtent.x, HeaderMin.y + (Metrics.ControlHeight - TallyExtent.y) * 0.5f),
                          Palette.TextMuted, Tally);

        ImGui::Dummy(ImVec2(PanelWidth, Metrics.ControlHeight));
    }

    // -- Filter + add affordance --
    const bool AtCapacity = (int)State.LayerStore.size() >= PaintLayerCapacity;
    {
        const float AddWidth = 74.0f;
        ImGui::SetCursorScreenPos(ImVec2(PanelMin.x + Padding, ImGui::GetCursorScreenPos().y + Metrics.ControlSpacing));
        ConstructFilterField(Theme, State, PanelWidth - Padding * 2.0f - AddWidth - Metrics.ControlSpacing);

        ImGui::SameLine(0.0f, Metrics.ControlSpacing);
        ImGui::BeginDisabled(AtCapacity);
        ImGui::PushStyleColor(ImGuiCol_Button,        Palette.AccentPrimary);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Palette.AccentSubtle);
        ImGui::PushStyleColor(ImGuiCol_Text,          Palette.TextOnAccent);
        if (ImGui::Button("Add", ImVec2(AddWidth, 0.0f)))
        {
            State.AddMenuRequested = true;
        }
        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();
    }

    // -- The add-layer catalogue --
    if (State.AddMenuRequested)
    {
        ImGui::OpenPopup("##addlayer");
        State.AddMenuRequested = false;
    }
    if (ImGui::BeginPopup("##addlayer"))
    {
        for (int Ordinal = 0; Ordinal < PaintLayerCategoryCount; Ordinal += 1)
        {
            const PaintLayerCategory Category = (PaintLayerCategory)Ordinal;
            char CatalogueLabel[64];
            std::snprintf(CatalogueLabel, sizeof(CatalogueLabel), "%s%s",
                          ResolveLayerCategoryLabel(Category),
                          ResolveCategoryFloodCondition(Category) ? "   (fills surface)" : "");
            if (ImGui::MenuItem(CatalogueLabel))
            {
                // Named "Paint 3" / "Fill 2" off the issue ordinal, so a name never repeats even after deletions renumber the stack.
                char SeededLabel[64];
                std::snprintf(SeededLabel, sizeof(SeededLabel), "%s %u", ResolveLayerCategoryLabel(Category), State.IssuedOrdinal + 1u);
                IntegrateLayer(State, Category, SeededLabel);
            }
        }
        ImGui::EndPopup();
    }

    ImGui::Dummy(ImVec2(0.0f, Metrics.ControlSpacing * 0.5f));

    // -- The rows --
    const bool  FilterActive = (State.FilterText[0] != '\0');
    RailVerdict Verdict      = {};
    {
        const float RowLeft  = PanelMin.x + Padding * 0.5f;
        const float RowRight = PanelMin.x + PanelWidth - Padding * 0.5f;

        ImGui::BeginChild("##layerrows", ImVec2(0.0f, -Metrics.ControlHeight - Metrics.ControlSpacing), false,
                          ImGuiWindowFlags_NoBackground);

        int Shown = 0;
        for (int Ordinal = 0; Ordinal < (int)State.LayerStore.size(); Ordinal += 1)
        {
            PaintLayerEntry& Entry = State.LayerStore[Ordinal];
            if (!LabelMatchesFilter(Entry.Label, State.FilterText)) continue;

            ConstructRow(Theme, State, IconRegistry, Entry, Ordinal, FilterActive, RowLeft, RowRight, RowHeight, Verdict);
            Shown += 1;
        }

        if (Shown == 0)
        {
            ImGui::SetCursorScreenPos(ImVec2(RowLeft + Padding, ImGui::GetCursorScreenPos().y + Padding));
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Palette.TextMuted), "No layer matches the filter.");
        }

        // 💡 The shared outliner panel aborts on an empty tree without a trailing zero-size item; the same guard is cheap here.
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        ImGui::EndChild();
    }

    // -- The blend-mode popup, opened by a row's caret --
    if (State.BlendMenuRequested)
    {
        ImGui::OpenPopup("##blendmode");
        State.BlendMenuRequested = false;
    }
    if (ImGui::BeginPopup("##blendmode"))
    {
        PaintLayerEntry* Target = ResolveLayer(State, State.BlendMenuTarget);
        if (Target == nullptr)
        {
            // The layer went away while the popup was open — close rather than edit a stale token.
            ImGui::CloseCurrentPopup();
        }
        else
        {
            const char* const* BlendTable = ResolveBlendModeTable();
            for (int Ordinal = 0; Ordinal < PaintBlendModeCount; Ordinal += 1)
            {
                if (ImGui::MenuItem(BlendTable[Ordinal], nullptr, Target->BlendOrdinal == Ordinal))
                {
                    Target->BlendOrdinal = Ordinal;
                }
            }
        }
        ImGui::EndPopup();
    }
    if (!ImGui::IsPopupOpen("##blendmode") && State.BlendMenuTarget != 0)
    {
        State.BlendMenuTarget = 0;
    }

    // -- Footer tally --
    {
        const int    Concealed = AccumulateConcealedCount(State);
        const ImVec2 FooterMin = ImGui::GetCursorScreenPos();
        const ImVec2 FooterMax = ImVec2(PanelMin.x + PanelWidth, FooterMin.y + Metrics.ControlHeight);
        DrawList->AddRectFilled(ImVec2(PanelMin.x, FooterMin.y), FooterMax, Palette.PanelHeader);
        DrawList->AddLine(ImVec2(PanelMin.x, FooterMin.y), ImVec2(FooterMax.x, FooterMin.y), Palette.PanelBorder,
                          Metrics.BorderThickness);

        char Footer[96];
        if (Concealed > 0)
        {
            std::snprintf(Footer, sizeof(Footer), "%d / %d layers \xC2\xB7 %d hidden",
                          (int)State.LayerStore.size(), PaintLayerCapacity, Concealed);
        }
        else
        {
            std::snprintf(Footer, sizeof(Footer), "%d / %d layers", (int)State.LayerStore.size(), PaintLayerCapacity);
        }
        DrawList->AddText(ImVec2(PanelMin.x + Padding, FooterMin.y + (Metrics.ControlHeight - ImGui::GetFontSize()) * 0.5f),
                          AtCapacity ? Palette.TextPrimary : Palette.TextMuted, Footer);

        if (AtCapacity)
        {
            const char*  Notice = "at capacity";
            const ImVec2 Extent = ImGui::CalcTextSize(Notice);
            DrawList->AddText(ImVec2(FooterMax.x - Padding - Extent.x, FooterMin.y + (Metrics.ControlHeight - Extent.y) * 0.5f),
                              Palette.AccentPrimary, Notice);
        }
        ImGui::Dummy(ImVec2(PanelWidth, Metrics.ControlHeight));
    }

    ImGui::PopStyleVar();

    // -- Deferred mutations --
    //
    // 🔴 Applied only now that the row loop has closed. Erasing or re-seating inside the loop invalidates both the iteration and the reference the
    //    row is still drawing from.
    if (Verdict.RelocateToken != 0 && Verdict.RelocateSeat >= 0)
    {
        RelocateLayer(State, Verdict.RelocateToken, Verdict.RelocateSeat);
    }
    if (Verdict.ReclaimToken != 0)
    {
        ReclaimLayer(State, Verdict.ReclaimToken);
    }
}

}   // namespace Frontier
