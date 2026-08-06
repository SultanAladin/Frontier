/*==============================================================================================================================================
                                                          TEXTUREPAINTWORKSPACEPANEL.CPP
==============================================================================================================================================*/
// 🧩 The three-column texture-paint workspace, drawn with the REAL panels. The layout mirrors the SLATE grid in
//    Documentation/Prototypes/TexturePaint2.html — a narrow rail, a wide field, a property column — while each third keeps its owning
//    validation's 1:1 port:
//      .stack-rail  -> ConstructLayerStackPanel      (LayerStackValidation's Layers slide, rows + inline expand + mask editor)
//      .paint-field -> the centre field + ConstructTexturePaintSummonedCard (TexturePaintValidation's right-click summon)
//      .chan-panel  -> ConstructChannelPropertyPanel (ChannelPropertyValidation's chips + per-channel cards)
//      .msk-card    -> ConstructLayerPropertiesPanel (the shared EngineContext mask card, Value/Texture/Generator + generator popup)
//    🔴 The rail and column are SCROLL CHILDREN so tall content stays reachable; the centre field is window-scope because the summoned card
//       clips and hit-tests itself. The order is fixed: field + card FIRST, then the children — see the header note.
//    📝 No icons in the rail or channel panels (each is ported from primitives); the summoned card takes the registry + strip store the host
//       brings up, and the shared mask card draws itself from theme controls alone.

#include "TexturePaintWorkspacePanel.h"

#include "EngineContext/Interface/Theme/ColorPaletteDescriptor.h"

#include "imgui.h"

using namespace Frontier;

namespace TexturePaintWorkspaceValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // -- Geometry ---------------------------------------------------------------------------------------------------------
    // 📝 One block, transcribed from the SLATE grid (TexturePaint2.html's 296 / 336 rails) scaled for the wider validation window so a
    //    side-by-side diff against the prototype stays mechanical. Tones come from the theme.
    constexpr float RailWidth   = 340.0f;   // [px] - .stack-rail — the left layer rail
    constexpr float ColumnWidth = 380.0f;   // [px] - the right property column
    constexpr float Gutter      =   8.0f;   // [px] - breathing gap between the three thirds

    const ImU32 FieldFill = IM_COL32(18, 19, 23, 255);   // [-] - the centre field's surface tone

    // 📝 Draw the centre field as a filled surface with a faint crosshair, so it reads as a place with a location rather than a flat void —
    //    the only way to see the summoned card is placed at the CURSOR. The hint line shows until the card stands.
    void InscribePaintField(ImVec2 FieldOrigin, ImVec2 FieldSpan, bool CardSummoned)
    {
        ImDrawList* Canvas = ImGui::GetWindowDrawList();

        const ImVec2 FieldMaximum(FieldOrigin.x + FieldSpan.x, FieldOrigin.y + FieldSpan.y);
        Canvas->AddRectFilled(FieldOrigin, FieldMaximum, FieldFill);

        const ImVec2 FieldCentre(FieldOrigin.x + FieldSpan.x * 0.5f, FieldOrigin.y + FieldSpan.y * 0.5f);
        const float  ArmLength = 9.0f;                                     // [px] - half-span of each crosshair arm
        Canvas->AddLine(ImVec2(FieldCentre.x - ArmLength, FieldCentre.y), ImVec2(FieldCentre.x + ArmLength, FieldCentre.y), IM_COL32(255, 255, 255, 28));
        Canvas->AddLine(ImVec2(FieldCentre.x, FieldCentre.y - ArmLength), ImVec2(FieldCentre.x, FieldCentre.y + ArmLength), IM_COL32(255, 255, 255, 28));

        if (!CardSummoned)
        {
            const char* Hint = "right-click the field to summon the instrument card";
            const ImVec2 HintSpan = ImGui::CalcTextSize(Hint);
            Canvas->AddText(ImVec2(FieldCentre.x - HintSpan.x * 0.5f, FieldCentre.y + 24.0f), IM_COL32(255, 255, 255, 64), Hint);
        }
    }

    // 📝 Open a full-height scroll child and draw one panel into it. The caller sets the cursor first; the child takes the given width and
    //    the host window's whole height. The AlwaysVerticalScrollbar flag keeps the rail and column scrollable the moment content grows.
    template <typename Callback>
    void WithScrollChild(const char* Identifier, float Width, float Height,
                         const ThemeConfiguration& Theme, Callback&& Draw)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (ImGui::BeginChild(Identifier, ImVec2(Width, Height), false, ImGuiWindowFlags_AlwaysVerticalScrollbar))
        {
            Draw(Theme);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InitializeTexturePaintWorkspaceSample(TexturePaintWorkspaceState& State)
{
    LayerStackValidation::InitializeLayerStackSample(State.Layers);
    ChannelPropertyValidation::InitializeChannelPropertySample(State.Channels);
    Frontier::InitializeLayerPropertiesSample(State.Mask);
    TexturePaintValidation::InitializeTexturePaintSummonedCard(State.Summoned);
}


void ConstructTexturePaintWorkspacePanel(const ThemeConfiguration& Theme,
                                         TexturePaintWorkspaceState&        State,
                                         SvgIconRegistry*                   Icons,
                                         PaintIconStore*                    StripStore)
{
    const ColorPaletteDescriptor& Palette = Theme.Palette;

    // ---- the three thirds, resolved from the window each frame so a resize re-lays everything ---------------------------------
    const ImVec2 WindowOrigin = ImGui::GetWindowPos();
    const ImVec2 WindowSpan   = ImGui::GetWindowSize();
    const float  Height       = WindowSpan.y;

    const ImVec2 RailOrigin(WindowOrigin.x, WindowOrigin.y);
    const ImVec2 RailSpan(RailWidth, Height);

    const ImVec2 ColumnOrigin(WindowOrigin.x + WindowSpan.x - ColumnWidth, WindowOrigin.y);
    const ImVec2 ColumnSpan(ColumnWidth, Height);

    const float  FieldLeft   = WindowOrigin.x + RailWidth + Gutter;
    const float  FieldRight  = ColumnOrigin.x - Gutter;
    const ImVec2 FieldOrigin(FieldLeft, WindowOrigin.y);
    const ImVec2 FieldSpan(FieldRight - FieldLeft, Height);

    // ---- 1. the centre field + the summoned card, at WINDOW scope ---------------------------------------------
    //    🔴 Nothing here may open a BeginChild: the card draws through the window draw list into a clip it pushes itself, and its panes
    //       hit-test manually against that clip — an active child would confine both to the child's rectangle. The children come AFTER.
    InscribePaintField(FieldOrigin, FieldSpan, State.Summoned.CardSummoned);

    TexturePaintValidation::ConfineTexturePaintField(State.Summoned, FieldOrigin, FieldSpan);
    TexturePaintValidation::ConstructTexturePaintSummonedCard(Theme, State.Summoned, Icons, StripStore);

    // ---- 2. the two scroll children, AFTER the card so they never clip it ---------------------------------------
    //    🔴 The summon clamp keeps the card inside the field, and the children never reach into it, so their later draw order cannot land
    //       on the box. The left rail takes the layer panel 1:1; the right column stacks the channel cards with the shared mask card.
    ImGui::SetCursorScreenPos(RailOrigin);
    WithScrollChild("##tpp-layer-rail", RailSpan.x, Height, Theme,
                    [&](const ThemeConfiguration& Themed) { LayerStackValidation::ConstructLayerStackPanel(Themed, State.Layers); });

    ImGui::SetCursorScreenPos(ColumnOrigin);
    WithScrollChild("##tpp-properties", ColumnSpan.x, Height, Theme,
                    [&](const ThemeConfiguration& Themed)
                    {
                        ChannelPropertyValidation::ConstructChannelPropertyPanel(Themed, State.Channels);
                        Frontier::ConstructLayerPropertiesPanel(Themed, State.Mask);
                    });

    // ---- 3. the gutters: hairline rules so the three thirds read as one composed surface -------------------------------------
    ImDrawList* Canvas = ImGui::GetWindowDrawList();
    Canvas->AddLine(ImVec2(RailOrigin.x + RailSpan.x, WindowOrigin.y),
                    ImVec2(RailOrigin.x + RailSpan.x, WindowOrigin.y + Height), Palette.PanelBorder, Theme.Metrics.BorderThickness);
    Canvas->AddLine(ImVec2(ColumnOrigin.x, WindowOrigin.y),
                    ImVec2(ColumnOrigin.x, WindowOrigin.y + Height), Palette.PanelBorder, Theme.Metrics.BorderThickness);
}

}   // namespace TexturePaintWorkspaceValidation
