/*==========================================================================================================================================
                                                NATIVE PRESENTATION OF THE INSPECTOR PANEL
                                     ------------------------------------------------------------------
     SceneInspectorPanel drawn 1:1 from the HTML Inspector: FrontierEditor.css #inspector / .inspector-top /
     .inspector-content / .object-header / .property-switch / .card / .control / .switch-row / .segmented /
     .inspector-footer at the CSS pixel values (Experimental/VehicleHmiPrototype @ 6afb6f0, via Reference).
==========================================================================================================================================*/

// Native presentation of the Inspector panel: exact HTML Inspector geometry over the portable view-model.

#include "SceneInspectorPanel.h"

// This translation unit compiles only with the interface SDK present (define FRONTIER_WITH_INTERFACE_SDK).
// Without the SDK it compiles to the empty presentation below so portable builds keep linking.
#if defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/StackRailControls.h"
#include "SceneInspectorPanelRegistration.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace Frontier
{

namespace
{

// 📝 CSS tokens (FrontierEditor.css :root + #inspector rules) — not the Log Stream kit theme.
constexpr ImU32 ColumnBg  = IM_COL32(0x16, 0x16, 0x16, 255);   // --column
constexpr ImU32 Edge      = IM_COL32(0x2a, 0x2a, 0x2a, 255);   // --edge
constexpr ImU32 Text      = IM_COL32(0xdc, 0xdc, 0xdc, 255);   // --text
constexpr ImU32 On        = IM_COL32(0x3e, 0xc4, 0x6d, 255);   // --on
constexpr ImU32 CrumbsInk = IM_COL32(0xbf, 0xbf, 0xbf, 255);
constexpr ImU32 FaintInk  = IM_COL32(0x71, 0x71, 0x71, 255);
constexpr ImU32 SaveInk   = IM_COL32(0x7c, 0x7c, 0x7c, 255);

void DrawText(ImDrawList* Ink, ImFont* Font, float Size, ImVec2 At, ImU32 Colour, const std::string& Value)
{
    Ink->AddText(Font, Size, At, Colour, Value.c_str());
}

float TextWidth(ImFont* Font, float Size, const std::string& Value)
{
    return KitTextWidth(Font, Size, Value.c_str());
}

// CSS letter-spacing has no ImGui equivalent; draw codepoint by codepoint with a fixed tracking
// adjustment per glyph (.metric uses -1.8 px). Returns the tracked advance width.
float DrawTrackedText(ImDrawList* Ink, ImFont* Font, float Size, ImVec2 At, ImU32 Colour,
                      const std::string& Value, float Tracking)
{
    float X = At.x;
    size_t Cursor = 0;
    while (Cursor < Value.size())
    {
        const unsigned char Lead = static_cast<unsigned char>(Value[Cursor]);
        size_t Length = 1;
        if ((Lead & 0xE0) == 0xC0) Length = 2;
        else if ((Lead & 0xF0) == 0xE0) Length = 3;
        else if ((Lead & 0xF8) == 0xF0) Length = 4;
        if (Cursor + Length > Value.size()) Length = 1;
        const std::string Glyph = Value.substr(Cursor, Length);
        Ink->AddText(Font, Size, ImVec2(X, At.y), Colour, Glyph.c_str());
        X += TextWidth(Font, Size, Glyph) + Tracking;
        Cursor += Length;
    }
    return (X - Tracking) - At.x;
}

float TrackedTextWidth(ImFont* Font, float Size, const std::string& Value, float Tracking)
{
    float Width = 0.0f;
    size_t Cursor = 0;
    while (Cursor < Value.size())
    {
        const unsigned char Lead = static_cast<unsigned char>(Value[Cursor]);
        size_t Length = 1;
        if ((Lead & 0xE0) == 0xC0) Length = 2;
        else if ((Lead & 0xF0) == 0xE0) Length = 3;
        else if ((Lead & 0xF8) == 0xF0) Length = 4;
        if (Cursor + Length > Value.size()) Length = 1;
        Width += TextWidth(Font, Size, Value.substr(Cursor, Length)) + Tracking;
        Cursor += Length;
    }
    return Width > 0.0f ? Width - Tracking : 0.0f;
}

std::string FormatSliderValue(double Value, int Digits)
{
    char Buffer[64] = {};
    std::snprintf(Buffer, sizeof(Buffer), "%.*f", std::clamp(Digits, 0, 10), Value);
    return Buffer;
}

// Word-wrapped paragraph (.muted: 10 px, line-height 1.55). Returns the drawn height.
float DrawWrapped(ImDrawList* Ink, ImFont* Font, float Size, ImVec2 At, float Width, ImU32 Colour,
                  const std::string& Value)
{
    const float LineH = Size * 1.55f;
    float       Y     = At.y;
    std::string Line;
    const auto  Flush = [&]() {
        if (!Line.empty()) DrawText(Ink, Font, Size, ImVec2(At.x, Y), Colour, Line);
        Y += LineH;
        Line.clear();
    };
    size_t Cursor = 0;
    while (Cursor < Value.size())
    {
        const size_t Space = Value.find(' ', Cursor);
        const std::string Word = Value.substr(Cursor, (Space == std::string::npos ? Value.size() : Space) - Cursor);
        const std::string Trial = Line.empty() ? Word : Line + " " + Word;
        if (!Line.empty() && TextWidth(Font, Size, Trial) > Width) Flush();
        Line = Line.empty() ? Word : Line + " " + Word;
        Cursor = (Space == std::string::npos) ? Value.size() : Space + 1;
    }
    Flush();
    return Y - At.y;
}

int WrappedLineCount(ImFont* Font, float Size, float Width, const std::string& Value)
{
    int Lines = 1;
    std::string Line;
    size_t Cursor = 0;
    while (Cursor < Value.size())
    {
        const size_t Space = Value.find(' ', Cursor);
        const std::string Word = Value.substr(Cursor, (Space == std::string::npos ? Value.size() : Space) - Cursor);
        const std::string Trial = Line.empty() ? Word : Line + " " + Word;
        if (!Line.empty() && TextWidth(Font, Size, Trial) > Width) { ++Lines; Line = Word; }
        else Line = Trial;
        Cursor = (Space == std::string::npos) ? Value.size() : Space + 1;
    }
    return Lines;
}

// Minimal EditorIcons subset used by property-switch tiles (17 px, ~1.7 stroke in the HTML).
void DrawTileGlyph(ImDrawList* Ink, const std::string& Glyph, ImVec2 Centre, ImU32 Colour)
{
    if (Glyph == "Eye")
    {
        Ink->AddEllipse(Centre, ImVec2(8.0f, 5.0f), Colour, 0.0f, 24, 1.4f);
        Ink->AddCircleFilled(Centre, 2.6f, Colour);
    }
    else if (Glyph == "Zap")
    {
        const ImVec2 Points[] = {ImVec2(Centre.x + 1.5f, Centre.y - 8.0f), ImVec2(Centre.x - 4.5f, Centre.y + 1.0f),
                                 ImVec2(Centre.x - 0.5f, Centre.y + 1.0f), ImVec2(Centre.x - 1.5f, Centre.y + 8.0f),
                                 ImVec2(Centre.x + 4.5f, Centre.y - 1.5f), ImVec2(Centre.x + 0.5f, Centre.y - 1.5f)};
        Ink->AddPolyline(Points, 6, Colour, ImDrawFlags_Closed, 1.4f);
    }
    else if (Glyph == "Aperture")
    {
        Ink->AddCircle(Centre, 7.5f, Colour, 24, 1.4f);
        Ink->AddCircle(Centre, 3.0f, Colour, 16, 1.2f);
    }
    else if (Glyph == "Layers")
    {
        for (int Tier = -1; Tier <= 1; ++Tier)
        {
            const float Y = Centre.y + static_cast<float>(Tier) * 4.5f;
            const ImVec2 Points[] = {ImVec2(Centre.x, Y - 3.0f), ImVec2(Centre.x + 7.0f, Y),
                                     ImVec2(Centre.x, Y + 3.0f), ImVec2(Centre.x - 7.0f, Y)};
            Ink->AddPolyline(Points, 4, Colour, ImDrawFlags_Closed, 1.2f);
        }
    }
    else if (Glyph == "Monitor")
    {
        Ink->AddRect(ImVec2(Centre.x - 8.0f, Centre.y - 6.0f), ImVec2(Centre.x + 8.0f, Centre.y + 4.0f), Colour, 2.0f, 0, 1.4f);
        Ink->AddLine(ImVec2(Centre.x, Centre.y + 4.0f), ImVec2(Centre.x, Centre.y + 7.0f), Colour, 1.4f);
        Ink->AddLine(ImVec2(Centre.x - 4.0f, Centre.y + 7.5f), ImVec2(Centre.x + 4.0f, Centre.y + 7.5f), Colour, 1.4f);
    }
    else if (Glyph == "Activity")
    {
        const ImVec2 Points[] = {ImVec2(Centre.x - 8.0f, Centre.y), ImVec2(Centre.x - 3.0f, Centre.y),
                                 ImVec2(Centre.x - 1.0f, Centre.y - 6.0f), ImVec2(Centre.x + 2.0f, Centre.y + 4.0f),
                                 ImVec2(Centre.x + 4.0f, Centre.y), ImVec2(Centre.x + 8.0f, Centre.y)};
        Ink->AddPolyline(Points, 6, Colour, 0, 1.4f);
    }
    else if (Glyph == "Bug")
    {
        Ink->AddEllipse(Centre, ImVec2(4.5f, 6.0f), Colour, 0.0f, 20, 1.4f);
        Ink->AddLine(ImVec2(Centre.x, Centre.y - 6.0f), ImVec2(Centre.x, Centre.y + 6.0f), Colour, 1.2f);
        Ink->AddCircleFilled(ImVec2(Centre.x, Centre.y - 7.5f), 2.2f, Colour);
        for (int Leg = -1; Leg <= 1; ++Leg)
        {
            const float Y = Centre.y + static_cast<float>(Leg) * 4.0f;
            Ink->AddLine(ImVec2(Centre.x - 4.0f, Y), ImVec2(Centre.x - 8.0f, Y - 2.0f), Colour, 1.2f);
            Ink->AddLine(ImVec2(Centre.x + 4.0f, Y), ImVec2(Centre.x + 8.0f, Y - 2.0f), Colour, 1.2f);
        }
    }
    else if (Glyph == "Scan")
    {
        const float E = 7.0f, C = 3.0f;
        Ink->AddLine(ImVec2(Centre.x - E, Centre.y - E + C), ImVec2(Centre.x - E, Centre.y - E), Colour, 1.4f);
        Ink->AddLine(ImVec2(Centre.x - E, Centre.y - E), ImVec2(Centre.x - E + C, Centre.y - E), Colour, 1.4f);
        Ink->AddLine(ImVec2(Centre.x + E - C, Centre.y - E), ImVec2(Centre.x + E, Centre.y - E), Colour, 1.4f);
        Ink->AddLine(ImVec2(Centre.x + E, Centre.y - E), ImVec2(Centre.x + E, Centre.y - E + C), Colour, 1.4f);
        Ink->AddLine(ImVec2(Centre.x - E, Centre.y + E - C), ImVec2(Centre.x - E, Centre.y + E), Colour, 1.4f);
        Ink->AddLine(ImVec2(Centre.x - E, Centre.y + E), ImVec2(Centre.x - E + C, Centre.y + E), Colour, 1.4f);
        Ink->AddLine(ImVec2(Centre.x + E - C, Centre.y + E), ImVec2(Centre.x + E, Centre.y + E), Colour, 1.4f);
        Ink->AddLine(ImVec2(Centre.x + E, Centre.y + E), ImVec2(Centre.x + E, Centre.y + E - C), Colour, 1.4f);
    }
    else   // Sparkles and any unported glyph: a four-point star
    {
        const ImVec2 Points[] = {ImVec2(Centre.x, Centre.y - 8.0f), ImVec2(Centre.x + 2.0f, Centre.y - 2.0f),
                                 ImVec2(Centre.x + 8.0f, Centre.y), ImVec2(Centre.x + 2.0f, Centre.y + 2.0f),
                                 ImVec2(Centre.x, Centre.y + 8.0f), ImVec2(Centre.x - 2.0f, Centre.y + 2.0f),
                                 ImVec2(Centre.x - 8.0f, Centre.y), ImVec2(Centre.x - 2.0f, Centre.y - 2.0f)};
        Ink->AddPolyline(Points, 8, Colour, ImDrawFlags_Closed, 1.4f);
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PANEL REGISTRATION
//------------------------------------------------------------------------------------------------------------------------

DockPanelSpecification MakeSceneInspectorPanelSpecification(SceneInspectorPanel& Panel)
{
    DockPanelSpecification Specification;
    Specification.Key              = "Inspector";
    Specification.Caption          = "Inspector";
    Specification.ConstructContent = [&Panel]() { Panel.ConstructInterface(); };
    Specification.Open             = true;
    return Specification;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERFACE RECORDING
//------------------------------------------------------------------------------------------------------------------------

void SceneInspectorPanel::ConstructInterface()
{
    const KitFaces Faces = ResolveKitFaces();
    ImDrawList*    Ink   = ImGui::GetWindowDrawList();

    const ImVec2 WindowPosition = ImGui::GetWindowPos();
    const ImVec2 WindowSize     = ImGui::GetWindowSize();
    const float  PL = WindowPosition.x;
    const float  PT = WindowPosition.y;
    const float  PR = PL + WindowSize.x;
    const float  PB = PT + WindowSize.y;

    // #inspector: radial gradient #1b1b1b (80%,0%) → #171717 42% → #141414; drawn as its vertical
    //    three-stop approximation (ImDrawList has no radial fill).
    Ink->AddRectFilledMultiColor(WindowPosition, ImVec2(PR, PT + (PB - PT) * 0.42f),
                                 IM_COL32(0x1b, 0x1b, 0x1b, 255), IM_COL32(0x1b, 0x1b, 0x1b, 255),
                                 IM_COL32(0x17, 0x17, 0x17, 255), IM_COL32(0x17, 0x17, 0x17, 255));
    Ink->AddRectFilledMultiColor(ImVec2(PL, PT + (PB - PT) * 0.42f), ImVec2(PR, PB),
                                 IM_COL32(0x17, 0x17, 0x17, 255), IM_COL32(0x17, 0x17, 0x17, 255),
                                 IM_COL32(0x14, 0x14, 0x14, 255), IM_COL32(0x14, 0x14, 0x14, 255));
    Ink->AddRectFilled(ImVec2(PL, PT), ImVec2(PL + 1.0f, PB), Edge);   // border-left: 1px solid var(--edge)

    // .inspector-top: 44 px, padding 0 18px, bottom hairline.
    {
        float X = PL + 18.0f;
        DrawText(Ink, Faces.Regular, 11.0f, ImVec2(X, PT + 16.5f), CrumbsInk, "Inspector");
        X += TextWidth(Faces.Regular, 11.0f, "Inspector") + 8.0f;
        Ink->AddLine(ImVec2(X + 2.0f, PT + 19.0f), ImVec2(X + 6.0f, PT + 22.5f), FaintInk, 1.4f);   // ChevronRight
        Ink->AddLine(ImVec2(X + 2.0f, PT + 26.0f), ImVec2(X + 6.0f, PT + 22.5f), FaintInk, 1.4f);
        X += 12.0f + 8.0f;
        if (HasSelection) DrawText(Ink, Faces.Regular, 11.0f, ImVec2(X, PT + 16.5f), FaintInk,
                                   GroupName.empty() ? RecordClassificationName(Classification) : GroupName);
        const std::string Status = "Changes apply live";   // .save-status: check glyph + caption, 10 px
        const float       StatusW = 12.0f + 6.0f + TextWidth(Faces.Regular, 10.0f, Status);
        const float       SX      = PR - 18.0f - StatusW;
        Ink->AddLine(ImVec2(SX + 1.0f, PT + 22.5f), ImVec2(SX + 4.5f, PT + 26.0f), SaveInk, 1.6f);
        Ink->AddLine(ImVec2(SX + 4.5f, PT + 26.0f), ImVec2(SX + 11.0f, PT + 18.5f), SaveInk, 1.6f);
        DrawText(Ink, Faces.Regular, 10.0f, ImVec2(SX + 18.0f, PT + 17.5f), SaveInk, Status);
        Ink->AddRectFilled(ImVec2(PL, PT + 44.0f), ImVec2(PR, PT + 45.0f), Edge);
    }

    if (!HasSelection)
    {
        const std::string Message = "Select a record in the Outliner.";
        DrawText(Ink, Faces.Regular, 12.5f,
                 ImVec2(PL + std::max(0.0f, (WindowSize.x - TextWidth(Faces.Regular, 12.5f, Message)) * 0.5f),
                        PT + 44.0f + 40.0f),
                 FaintInk, Message);
        return;
    }

    // .inspector-content: padding 20px 16px 18px, scrolls.
    const float CL = PL + 16.0f;   // [px]
    const float CR = PR - 16.0f;
    const float ContentW = CR - CL;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetCursorScreenPos(ImVec2(CL, PT + 45.0f));
    if (!ImGui::BeginChild("##InspectorContent", ImVec2(ContentW, PB - PT - 45.0f), false))
    {
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        return;
    }
    float Y = PT + 45.0f + 20.0f;   // [px] - content top padding

    // .object-header: eyebrow (classification, 9 px, 1.7 px tracking) over the name (19 px); Enabled pill right.
    {
        std::string Eyebrow = TypeName.empty() ? RecordClassificationName(Classification) : TypeName;
        for (char& Character : Eyebrow) Character = static_cast<char>(std::toupper(static_cast<unsigned char>(Character)));
        KitDrawTracked(Ink, Faces.Medium, 9.0f, ImVec2(CL, Y), IM_COL32(0x89, 0x89, 0x89, 255), Eyebrow.c_str(), 9.0f * 0.189f);
        // .enabled-pill: HTML draws it only for entities with a Toggle — natively, the record whose
        //    visibility tile (Eye glyph) backs the directory eye. Other records get no pill.
        bool HasEnabledPill = false;
        for (const SceneInspectorCard& Card : Cards)
            for (const SceneInspectorControl& Control : Card.Controls)
                if (Control.Category == RecordEditorControlCategory::Toggle && Control.TilePresentation && Control.Glyph == "Eye")
                    HasEnabledPill = true;
        if (HasEnabledPill)
        {
            const std::string Caption = Enabled ? "Enabled" : "Disabled";
            const float PillW = 11.0f + 5.0f + 7.0f + TextWidth(Faces.Regular, 10.0f, Caption) + 11.0f;
            const float PillX = CR - PillW;
            bool PillHover = false;
            if (KitTarget("##InspectorEnabledPill", ImVec2(PillX, Y), ImVec2(PillW, 26.0f), PillHover))
            {
                for (const SceneInspectorCard& Card : Cards)
                {
                    for (const SceneInspectorControl& Control : Card.Controls)
                    {
                        if (Control.Category == RecordEditorControlCategory::Toggle && Control.TilePresentation && Control.Glyph == "Eye")
                            ApplyToggle(Control.Key, !Enabled);
                    }
                }
            }
            KitPillSurface(Ink, ImVec2(PillX, Y), ImVec2(PillX + PillW, Y + 26.0f), IM_COL32(0x25, 0x25, 0x25, 255),
                           IM_COL32(0x3a, 0x3a, 0x3a, 255), 25.0f);
            Ink->AddCircleFilled(ImVec2(PillX + 16.0f, Y + 13.0f), 2.5f, Enabled ? On : IM_COL32(0x81, 0x81, 0x81, 255));
            DrawText(Ink, Faces.Regular, 10.0f, ImVec2(PillX + 23.0f, Y + 8.0f), IM_COL32(0xb2, 0xb2, 0xb2, 255), Caption);
        }
        Y += 6.0f + 9.0f;   // eyebrow + its 6 px margin
        DrawText(Ink, Faces.Regular, 19.0f, ImVec2(CL, Y), Text, Name);
        Y += 19.0f * 1.35f + 18.0f;   // heading line + .object-header margin-bottom
    }

    // .section-label: PROPERTIES left (9 px, 1.5 px tracking), description right (10 px).
    {
        KitDrawTracked(Ink, Faces.Medium, 9.0f, ImVec2(CL, Y), IM_COL32(0x8d, 0x8d, 0x8d, 255), "PROPERTIES", 9.0f * 0.167f);
        float DescriptionLines = 1.0f;
        if (!Description.empty())
        {
            // HTML: the description flex item wraps, right-aligned, beside PROPERTIES.
            const float AvailW = ContentW - 110.0f;
            std::vector<std::string> Lines;
            std::string Line;
            size_t Cursor = 0;
            while (Cursor < Description.size())
            {
                const size_t Space = Description.find(' ', Cursor);
                const std::string Word = Description.substr(Cursor, (Space == std::string::npos ? Description.size() : Space) - Cursor);
                const std::string Trial = Line.empty() ? Word : Line + " " + Word;
                if (!Line.empty() && TextWidth(Faces.Regular, 10.0f, Trial) > AvailW) { Lines.push_back(Line); Line = Word; }
                else Line = Trial;
                Cursor = (Space == std::string::npos) ? Description.size() : Space + 1;
            }
            Lines.push_back(Line);
            DescriptionLines = static_cast<float>(Lines.size());
            for (size_t Index = 0; Index < Lines.size(); ++Index)
            {
                DrawText(Ink, Faces.Regular, 10.0f,
                         ImVec2(CR - TextWidth(Faces.Regular, 10.0f, Lines[Index]), Y - 1.0f + static_cast<float>(Index) * 13.5f),
                         IM_COL32(0x69, 0x69, 0x69, 255), Lines[Index]);
            }
        }
        Y += std::max(9.0f * 1.35f, DescriptionLines * 13.5f) + 12.0f;
    }

    // .property-switches: tile toggles in one flex row (gap 8), each tile padding 12/6/10, radius 16.
    {
        std::vector<const SceneInspectorControl*> Tiles;
        for (const SceneInspectorCard& Card : Cards)
            for (const SceneInspectorControl& Control : Card.Controls)
                if (Control.Category == RecordEditorControlCategory::Toggle && Control.TilePresentation) Tiles.push_back(&Control);
        if (!Tiles.empty())
        {
            const float TileH = 12.0f + 30.0f + 7.0f + 13.0f + 7.0f + 10.0f + 10.0f;   // [px]
            const float TileW = (ContentW - 8.0f * static_cast<float>(Tiles.size() - 1)) / static_cast<float>(Tiles.size());
            for (size_t Index = 0; Index < Tiles.size(); ++Index)
            {
                const SceneInspectorControl& Control = *Tiles[Index];
                const float X = CL + static_cast<float>(Index) * (TileW + 8.0f);
                bool Hover = false;
                char Id[96] = {};
                std::snprintf(Id, sizeof(Id), "##InspectorTile%zu", Index);
                if (KitTarget(Id, ImVec2(X, Y), ImVec2(TileW, TileH), Hover)) ApplyToggle(Control.Key, !Control.ToggleValue);
                KitPillSurface(Ink, ImVec2(X, Y), ImVec2(X + TileW, Y + TileH),
                               Hover ? IM_COL32(0x28, 0x28, 0x28, 255) : IM_COL32(0x20, 0x20, 0x20, 255),
                               Control.ToggleValue ? IM_COL32(0x32, 0x32, 0x32, 255) : IM_COL32(0x49, 0x32, 0x32, 255), 16.0f);
                const float CentreX = X + TileW * 0.5f;
                Ink->AddRectFilled(ImVec2(CentreX - 15.0f, Y + 12.0f), ImVec2(CentreX + 15.0f, Y + 42.0f),
                                   Control.ToggleValue ? IM_COL32(0x2f, 0x6d, 0x45, 255) : IM_COL32(0xcc, 0x76, 0x73, 255), 10.0f);
                DrawTileGlyph(Ink, Control.Glyph, ImVec2(CentreX, Y + 27.0f),
                              Control.ToggleValue ? IM_COL32(0xd9, 0xf5, 0xe3, 255) : IM_COL32(0x42, 0x1e, 0x1d, 255));
                const std::string& Label = Control.Label;
                DrawText(Ink, Faces.Regular, 10.0f, ImVec2(CentreX - TextWidth(Faces.Regular, 10.0f, Label) * 0.5f, Y + 49.0f),
                         IM_COL32(0xcf, 0xcf, 0xcf, 255), Label);
                const std::string State = Control.ToggleValue ? "ON" : "OFF";
                KitDrawTracked(Ink, Faces.Medium, 8.0f,
                               ImVec2(CentreX - (TextWidth(Faces.Medium, 8.0f, State) + 1.2f * 2.0f) * 0.5f, Y + 69.0f),
                               Control.ToggleValue ? IM_COL32(0x7f, 0xd3, 0x9d, 255) : IM_COL32(0xcd, 0x85, 0x80, 255),
                               State.c_str(), 1.2f);
            }
            Y += TileH + 16.0f;   // .property-switches margin-bottom
        }
    }

    // .cards: one column, gap 12; .card gradient #222 → #1f1f1f (125deg), 1 px #2f2f2f, radius 18, padding 18.
    // A card holding only property-switch tiles renders as .property-switches above, never as an empty card.
    size_t DisplayedCard = 0;
    for (size_t CardIndex = 0; CardIndex < Cards.size(); ++CardIndex)
    {
        const SceneInspectorCard& Card = Cards[CardIndex];
        const bool HasBodyControls = std::any_of(Card.Controls.begin(), Card.Controls.end(),
            [](const SceneInspectorControl& Control) {
                return !(Control.Category == RecordEditorControlCategory::Toggle && Control.TilePresentation);
            });
        if (!HasBodyControls) continue;
        ++DisplayedCard;   // retained for parity debugging; the drawn index is the card's explicit HTML Index
        // First pass measures the card so its surface is drawn before its controls.
        float BodyH = 12.0f + 16.0f;   // heading + margin-bottom
        if (!Card.Subtitle.empty()) BodyH += 9.0f * 1.35f + 16.0f - 10.0f;
        for (const SceneInspectorControl& Control : Card.Controls)
        {
            if (Control.Category == RecordEditorControlCategory::Toggle && Control.TilePresentation) continue;
            switch (Control.Category)
            {
                case RecordEditorControlCategory::Slider:    BodyH += 22.0f + 24.0f + 12.0f + 22.0f + (Control.RangeLabelMinimum.empty() ? 0.0f : 6.0f + 9.0f * 1.35f); break;
                case RecordEditorControlCategory::Toggle:    BodyH += 9.0f + std::max(23.0f, 11.0f * 1.35f + (Control.Note.empty() ? 0.0f : 2.0f + 9.0f * 1.35f)) + 9.0f; break;
                case RecordEditorControlCategory::Segmented: BodyH += 14.0f + 10.0f * 1.35f + 8.0f + 4.0f + 7.0f + 10.0f * 1.35f + 7.0f + 4.0f; break;
                case RecordEditorControlCategory::Select:    BodyH += 14.0f + 10.0f * 1.35f + 8.0f + 11.0f * 1.35f + 18.0f; break;
                case RecordEditorControlCategory::Metric:    BodyH += 38.0f; break;
                case RecordEditorControlCategory::Muted:     BodyH += 8.0f + static_cast<float>(WrappedLineCount(Faces.Regular, 10.0f, ContentW - 36.0f, Control.Text)) * 15.5f; break;
                case RecordEditorControlCategory::Stats:     BodyH += 16.0f + 14.0f + 9.0f * 1.35f + 6.0f + 17.0f * 1.35f; break;
                case RecordEditorControlCategory::Readout:   BodyH += 8.0f + 10.0f * 1.35f; break;
                case RecordEditorControlCategory::TextBlock: BodyH += 12.0f + 12.0f + static_cast<float>(std::count(Control.ReadoutText.begin(), Control.ReadoutText.end(), '\n') + 1) * 16.0f; break;
                case RecordEditorControlCategory::Action:    BodyH += 14.0f + 9.0f + 10.0f * 1.35f + 9.0f; break;
            }
        }
        const float CardH = 18.0f + BodyH + 18.0f;
        // 📝 CSS gradient 125deg #222 → #1f1f1f; ImDrawList has no rounded multi-colour rect, so the
        //    gradient's start stop is drawn flat (recorded in Exhibits/SceneDirectory/PARITY.md).
        Ink->AddRectFilled(ImVec2(CL, Y), ImVec2(CR, Y + CardH), IM_COL32(0x22, 0x22, 0x22, 255), 18.0f);
        Ink->AddRect(ImVec2(CL, Y), ImVec2(CR, Y + CardH), IM_COL32(0x2f, 0x2f, 0x2f, 255), 18.0f);
        float CY = Y + 18.0f;
        float TitleX = CL + 18.0f;
        if (!Card.Glyph.empty())
        {
            // .card-heading .glyph: 16 px in var(--accent) at 85% opacity, 9 px gap to the title.
            DrawTileGlyph(Ink, Card.Glyph, ImVec2(TitleX + 8.0f, CY + 8.0f), IM_COL32(0xc5, 0xc5, 0xc5, 217));
            TitleX += 16.0f + 9.0f;
        }
        DrawText(Ink, Faces.Regular, 12.0f, ImVec2(TitleX, CY), IM_COL32(0xc9, 0xc9, 0xc9, 255), Card.Title);
        if (Card.Index >= 0)
        {
            char IndexText[32] = {};
            std::snprintf(IndexText, sizeof(IndexText), "%02d", Card.Index);
            KitDrawTracked(Ink, Faces.Regular, 9.0f, ImVec2(CR - 18.0f - TextWidth(Faces.Regular, 9.0f, IndexText) - 2.0f, CY + 1.5f),
                           IM_COL32(0x4f, 0x4f, 0x4f, 255), IndexText, 1.0f);
        }
        CY += 12.0f * 1.35f + 16.0f;
        if (!Card.Subtitle.empty())
        {
            std::string Subtitle = Card.Subtitle;
            for (char& Character : Subtitle) Character = static_cast<char>(std::toupper(static_cast<unsigned char>(Character)));
            KitDrawTracked(Ink, Faces.Regular, 9.0f, ImVec2(CL + 18.0f, CY), IM_COL32(0x73, 0x73, 0x73, 255), Subtitle.c_str(), 9.0f * 0.156f);
            CY += 9.0f * 1.35f + 6.0f;
        }
        const float InnerL = CL + 18.0f;
        const float InnerR = CR - 18.0f;
        const float InnerW = InnerR - InnerL;

        for (size_t ControlIndex = 0; ControlIndex < Card.Controls.size(); ++ControlIndex)
        {
            const SceneInspectorControl& Control = Card.Controls[ControlIndex];
            if (Control.Category == RecordEditorControlCategory::Toggle && Control.TilePresentation) continue;
            char Id[128] = {};
            std::snprintf(Id, sizeof(Id), "##Inspector%zu-%zu", CardIndex, ControlIndex);
            switch (Control.Category)
            {
                case RecordEditorControlCategory::Slider:
                {
                    // SliderControl.css .frontier-slider: margin-top 22; head min-height 22 / margin-bottom 12
                    // (label 11 px #a5a5a5; value capsule #080808 radius 20, height 24, min-width 78, number
                    // 11 px #e0e0d8 over 54 px, unit 10 px #b2b2aa); track 22 px radius 20, fill #555650
                    // over #171818; thumb 20 px #e2e4d8.
                    CY += 22.0f;
                    DrawText(Ink, Faces.Regular, 11.0f, ImVec2(InnerL, CY + 4.0f), IM_COL32(0xa5, 0xa5, 0xa5, 255), Control.Label);
                    const std::string Value = FormatSliderValue(Control.SliderValue, Control.Digits);
                    const float CapsuleW = std::max(78.0f, 10.0f + 54.0f + (Control.Unit.empty() ? 0.0f : 4.0f + TextWidth(Faces.Regular, 10.0f, Control.Unit)) + 10.0f);
                    KitPillSurface(Ink, ImVec2(InnerR - CapsuleW, CY), ImVec2(InnerR, CY + 24.0f),
                                   IM_COL32(0x08, 0x08, 0x08, 255), 0, 20.0f);
                    float VX = InnerR - 10.0f;
                    if (!Control.Unit.empty())
                    {
                        VX -= TextWidth(Faces.Regular, 10.0f, Control.Unit);
                        DrawText(Ink, Faces.Regular, 10.0f, ImVec2(VX, CY + 7.0f), IM_COL32(0xb2, 0xb2, 0xaa, 255), Control.Unit);
                        VX -= 4.0f;
                    }
                    DrawText(Ink, Faces.Regular, 11.0f, ImVec2(VX - TextWidth(Faces.Regular, 11.0f, Value), CY + 6.5f),
                             IM_COL32(0xe0, 0xe0, 0xd8, 255), Value);
                    CY += 24.0f + 12.0f;
                    const float TrackY = CY;
                    const double Span = Control.Maximum - Control.Minimum;
                    const float Fraction = Span > 0.0 ? static_cast<float>((Control.SliderValue - Control.Minimum) / Span) : 0.0f;
                    bool Hover = false;
                    if (KitTarget(Id, ImVec2(InnerL, TrackY), ImVec2(InnerW, 22.0f), Hover) ||
                        (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)))
                    {
                        const float MouseX = ImGui::GetIO().MousePos.x;
                        const double Next = Control.Minimum + Span * std::clamp((MouseX - InnerL) / InnerW, 0.0f, 1.0f);
                        ApplySlider(Control.Key, Next);
                    }
                    Ink->AddRectFilled(ImVec2(InnerL, TrackY), ImVec2(InnerR, TrackY + 22.0f), IM_COL32(0x17, 0x18, 0x18, 255), 20.0f);
                    if (Fraction > 0.0f)
                    {
                        Ink->PushClipRect(ImVec2(InnerL, TrackY), ImVec2(InnerL + InnerW * Fraction, TrackY + 22.0f), true);
                        Ink->AddRectFilled(ImVec2(InnerL, TrackY), ImVec2(InnerR, TrackY + 22.0f), IM_COL32(0x55, 0x56, 0x50, 255), 20.0f);
                        Ink->PopClipRect();
                    }
                    Ink->AddCircleFilled(ImVec2(InnerL + InnerW * Fraction, TrackY + 11.0f), 10.0f, IM_COL32(0xe2, 0xe4, 0xd8, 255));
                    CY = TrackY + 22.0f;
                    if (!Control.RangeLabelMinimum.empty() || !Control.RangeLabelMaximum.empty())
                    {
                        CY += 6.0f;
                        DrawText(Ink, Faces.Regular, 9.0f, ImVec2(InnerL, CY), IM_COL32(0x84, 0x84, 0x84, 255), Control.RangeLabelMinimum);
                        DrawText(Ink, Faces.Regular, 9.0f, ImVec2(InnerR - TextWidth(Faces.Regular, 9.0f, Control.RangeLabelMaximum), CY),
                                 IM_COL32(0x84, 0x84, 0x84, 255), Control.RangeLabelMaximum);
                        CY += 9.0f * 1.35f;
                    }
                    break;
                }
                case RecordEditorControlCategory::Metric:
                {
                    // .metric-row: value 38 px Light, unit small 14 px #7d7d7d; .small-pill at right.
                    // The card clips overflow (overflow: hidden), so the value is clipped before the pill.
                    float PillReserved = 0.0f;
                    if (!Control.PillText.empty())
                        PillReserved = 9.0f + TextWidth(Faces.Regular, 9.0f, Control.PillText) + 9.0f + 12.0f;
                    Ink->PushClipRect(ImVec2(InnerL, CY), ImVec2(InnerR - PillReserved, CY + 40.0f), true);
                    // .metric letter-spacing: -1.8 px at 38 px.
                    DrawTrackedText(Ink, Faces.Light, 38.0f, ImVec2(InnerL, CY), IM_COL32(0xdf, 0xdf, 0xdf, 255), Control.ReadoutText, -1.8f);
                    float UnitX = InnerL + TrackedTextWidth(Faces.Light, 38.0f, Control.ReadoutText, -1.8f) + 6.0f;
                    if (!Control.Unit.empty())
                        DrawText(Ink, Faces.Light, 14.0f, ImVec2(UnitX, CY + 22.0f), IM_COL32(0x7d, 0x7d, 0x7d, 255), Control.Unit);
                    Ink->PopClipRect();
                    if (!Control.PillText.empty())
                    {
                        const float PillW = 9.0f + TextWidth(Faces.Regular, 9.0f, Control.PillText) + 9.0f;
                        const float PillY = CY + 38.0f - 9.0f * 1.35f - 10.0f;
                        ImU32 PillBg = IM_COL32(0x2c, 0x2c, 0x2c, 255), PillEdge = IM_COL32(0x41, 0x41, 0x41, 255), PillInk = IM_COL32(0xbc, 0xbc, 0xbc, 255);
                        if (Control.PillState == "live") { PillBg = IM_COL32(0x1d, 0x2c, 0x22, 255); PillEdge = IM_COL32(0x2f, 0x6d, 0x45, 255); PillInk = IM_COL32(0x9b, 0xe3, 0xb5, 255); }
                        if (Control.PillState == "warm") { PillBg = IM_COL32(0x2c, 0x24, 0x16, 255); PillEdge = IM_COL32(0x6a, 0x51, 0x24, 255); PillInk = IM_COL32(0xf0, 0xc2, 0x7a, 255); }
                        KitPillSurface(Ink, ImVec2(InnerR - PillW, PillY), ImVec2(InnerR, PillY + 9.0f * 1.35f + 10.0f), PillBg, PillEdge, 20.0f);
                        DrawText(Ink, Faces.Regular, 9.0f, ImVec2(InnerR - PillW + 9.0f, PillY + 5.0f), PillInk, Control.PillText);
                    }
                    CY += 38.0f;
                    break;
                }
                case RecordEditorControlCategory::Muted:
                {
                    CY += 8.0f;   // .muted margin-top
                    CY += DrawWrapped(Ink, Faces.Regular, 10.0f, ImVec2(InnerL, CY), InnerW,
                                      IM_COL32(0x7d, 0x7d, 0x7d, 255), Control.Text);
                    break;
                }
                case RecordEditorControlCategory::Stats:
                {
                    // .stats: margin-top 16, padding-top 14, hairline above; cells label 9 px over 17 px Light.
                    CY += 16.0f;
                    Ink->AddRectFilled(ImVec2(InnerL, CY), ImVec2(InnerR, CY + 1.0f), IM_COL32(0xff, 0xff, 0xff, 0x0b));
                    CY += 14.0f;
                    if (!Control.StatCells.empty())
                    {
                        const float CellW = (InnerW - 12.0f * static_cast<float>(Control.StatCells.size() - 1)) /
                                            static_cast<float>(Control.StatCells.size());
                        for (size_t Cell = 0; Cell < Control.StatCells.size(); ++Cell)
                        {
                            const float X = InnerL + static_cast<float>(Cell) * (CellW + 12.0f);
                            DrawText(Ink, Faces.Regular, 9.0f, ImVec2(X, CY), IM_COL32(0x8a, 0x8a, 0x8a, 255), Control.StatCells[Cell].Label);
                            DrawText(Ink, Faces.Light, 17.0f, ImVec2(X, CY + 9.0f * 1.35f + 6.0f), IM_COL32(0xde, 0xde, 0xde, 255), Control.StatCells[Cell].Value);
                            if (!Control.StatCells[Cell].Unit.empty())
                            {
                                const float UX = X + TextWidth(Faces.Light, 17.0f, Control.StatCells[Cell].Value) + 4.0f;
                                DrawText(Ink, Faces.Regular, 10.0f, ImVec2(UX, CY + 9.0f * 1.35f + 12.0f), IM_COL32(0x85, 0x85, 0x85, 255), Control.StatCells[Cell].Unit);
                            }
                        }
                    }
                    CY += 9.0f * 1.35f + 6.0f + 17.0f * 1.35f;
                    break;
                }
                case RecordEditorControlCategory::Select:
                {
                    // .control (margin-top 14) + .control-line + .select-field (full width, padding 9, radius 9).
                    // The native kit has no dropdown surface wired to this panel yet; a click advances the
                    // choice (recorded in PARITY.md) — the field's pixels are the HTML field's.
                    CY += 14.0f;
                    DrawText(Ink, Faces.Regular, 10.0f, ImVec2(InnerL, CY), IM_COL32(0xa3, 0xa3, 0xa3, 255), Control.Label);
                    CY += 10.0f * 1.35f + 8.0f;
                    bool Hover = false;
                    if (KitTarget(Id, ImVec2(InnerL, CY), ImVec2(InnerW, 11.0f * 1.35f + 18.0f), Hover) && !Control.Options.empty())
                        ApplySegmented(Control.Key, (Control.SegmentedIndex + 1) % Control.Options.size());
                    KitPillSurface(Ink, ImVec2(InnerL, CY), ImVec2(InnerR, CY + 11.0f * 1.35f + 18.0f),
                                   IM_COL32(0x19, 0x19, 0x19, 255), IM_COL32(0x39, 0x39, 0x39, 255), 9.0f);
                    const std::string Current = Control.Options.empty() ? std::string{} : Control.Options[std::min(Control.SegmentedIndex, Control.Options.size() - 1)];
                    DrawText(Ink, Faces.Regular, 11.0f, ImVec2(InnerL + 9.0f, CY + 9.0f), IM_COL32(0xc8, 0xc8, 0xc8, 255), Current);
                    Ink->AddLine(ImVec2(InnerR - 22.0f, CY + 13.0f), ImVec2(InnerR - 16.0f, CY + 19.0f), IM_COL32(0x8b, 0x8b, 0x8b, 255), 1.4f);
                    Ink->AddLine(ImVec2(InnerR - 16.0f, CY + 19.0f), ImVec2(InnerR - 10.0f, CY + 13.0f), IM_COL32(0x8b, 0x8b, 0x8b, 255), 1.4f);
                    CY += 11.0f * 1.35f + 18.0f;
                    break;
                }
                case RecordEditorControlCategory::Toggle:
                {
                    const float TextH = 11.0f * 1.35f + (Control.Note.empty() ? 0.0f : 2.0f + 9.0f * 1.35f);
                    const float RowH  = std::max(23.0f, TextH);
                    DrawText(Ink, Faces.Regular, 11.0f, ImVec2(InnerL, CY + 9.0f), IM_COL32(0xc4, 0xc4, 0xc4, 255), Control.Label);
                    if (!Control.Note.empty())
                        DrawText(Ink, Faces.Regular, 9.0f, ImVec2(InnerL, CY + 9.0f + 11.0f * 1.35f + 2.0f),
                                 IM_COL32(0x72, 0x72, 0x72, 255), Control.Note);
                    bool Hover = false;
                    if (KitTarget(Id, ImVec2(InnerR - 40.0f, CY + 9.0f), ImVec2(40.0f, 23.0f), Hover))
                        ApplyToggle(Control.Key, !Control.ToggleValue);
                    KitPillSurface(Ink, ImVec2(InnerR - 40.0f, CY + 9.0f), ImVec2(InnerR, CY + 32.0f),
                                   Control.ToggleValue ? On : IM_COL32(0x3a, 0x3a, 0x3a, 255),
                                   Control.ToggleValue ? IM_COL32(0x53, 0xd6, 0x84, 255) : IM_COL32(0x48, 0x48, 0x48, 255), 23.0f);
                    Ink->AddCircleFilled(ImVec2(InnerR - 40.0f + (Control.ToggleValue ? 29.5f : 10.5f), CY + 20.5f), 8.5f,
                                         Control.ToggleValue ? IM_COL32(0xf4, 0xf4, 0xf4, 255) : IM_COL32(0x9c, 0x9c, 0x9c, 255));
                    CY += 9.0f + RowH + 9.0f;
                    break;
                }
                case RecordEditorControlCategory::Segmented:
                {
                    CY += 14.0f;   // .control margin-top
                    DrawText(Ink, Faces.Regular, 10.0f, ImVec2(InnerL, CY), IM_COL32(0xa3, 0xa3, 0xa3, 255), Control.Label);
                    CY += 10.0f * 1.35f + 8.0f;
                    KitPillSurface(Ink, ImVec2(InnerL, CY), ImVec2(InnerR, CY + 4.0f + 7.0f + 10.0f * 1.35f + 7.0f + 4.0f),
                                   IM_COL32(0x12, 0x12, 0x12, 255), IM_COL32(0x26, 0x26, 0x26, 255), 11.0f);
                    if (!Control.Options.empty())
                    {
                        const float ButtonW = (InnerW - 8.0f - 4.0f * static_cast<float>(Control.Options.size() - 1)) /
                                              static_cast<float>(Control.Options.size());
                        for (size_t Option = 0; Option < Control.Options.size(); ++Option)
                        {
                            const float X = InnerL + 4.0f + static_cast<float>(Option) * (ButtonW + 4.0f);
                            bool Hover = false;
                            char OptionId[160] = {};
                            std::snprintf(OptionId, sizeof(OptionId), "%s-%zu", Id, Option);
                            if (KitTarget(OptionId, ImVec2(X, CY + 4.0f), ImVec2(ButtonW, 7.0f + 10.0f * 1.35f + 7.0f), Hover))
                                ApplySegmented(Control.Key, Option);
                            if (Option == Control.SegmentedIndex)
                                KitPillSurface(Ink, ImVec2(X, CY + 4.0f), ImVec2(X + ButtonW, CY + 4.0f + 7.0f + 10.0f * 1.35f + 7.0f),
                                               IM_COL32(0x2e, 0x2e, 0x2e, 255), 0, 8.0f);
                            const std::string& Caption = Control.Options[Option];
                            DrawText(Ink, Faces.Regular, 10.0f,
                                     ImVec2(X + (ButtonW - TextWidth(Faces.Regular, 10.0f, Caption)) * 0.5f, CY + 11.0f),
                                     Option == Control.SegmentedIndex ? IM_COL32(0xf0, 0xf0, 0xf0, 255) : IM_COL32(0x8b, 0x8b, 0x8b, 255), Caption);
                        }
                    }
                    CY += 4.0f + 7.0f + 10.0f * 1.35f + 7.0f + 4.0f;
                    break;
                }
                case RecordEditorControlCategory::TextBlock:
                {
                    // .readout: mono 10 px / 1.6, #8f8f8f, margin-top 12, padding-top 12, hairline above.
                    // (The native kit has no mono face in this panel; Regular stands in — see PARITY.md.)
                    CY += 12.0f;
                    Ink->AddRectFilled(ImVec2(InnerL, CY), ImVec2(InnerR, CY + 1.0f), IM_COL32(0xff, 0xff, 0xff, 0x0b));
                    CY += 12.0f;
                    float LineY = CY;
                    size_t Cursor = 0;
                    while (Cursor <= Control.ReadoutText.size())
                    {
                        const size_t End = Control.ReadoutText.find('\n', Cursor);
                        const std::string Line = Control.ReadoutText.substr(Cursor, (End == std::string::npos ? Control.ReadoutText.size() : End) - Cursor);
                        DrawText(Ink, Faces.Regular, 10.0f, ImVec2(InnerL, LineY), IM_COL32(0x8f, 0x8f, 0x8f, 255), Line);
                        LineY += 16.0f;
                        if (End == std::string::npos) break;
                        Cursor = End + 1;
                    }
                    CY = LineY;
                    break;
                }
                case RecordEditorControlCategory::Readout:
                {
                    DrawText(Ink, Faces.Regular, 10.0f, ImVec2(InnerL, CY), IM_COL32(0x8e, 0x8e, 0x8e, 255), Control.Label);
                    DrawText(Ink, Faces.Regular, 10.0f, ImVec2(InnerR - TextWidth(Faces.Regular, 10.0f, Control.ReadoutText), CY),
                             IM_COL32(0xd4, 0xd4, 0xd4, 255), Control.ReadoutText);
                    CY += 10.0f * 1.35f + 8.0f;
                    break;
                }
                case RecordEditorControlCategory::Action:
                {
                    CY += 14.0f;
                    bool Hover = false;
                    if (KitTarget(Id, ImVec2(InnerL, CY), ImVec2(InnerW, 9.0f + 10.0f * 1.35f + 9.0f), Hover)) ApplyAction(Control.Key);
                    KitPillSurface(Ink, ImVec2(InnerL, CY), ImVec2(InnerR, CY + 9.0f + 10.0f * 1.35f + 9.0f),
                                   Hover ? IM_COL32(0x2e, 0x2e, 0x2e, 255) : IM_COL32(0x26, 0x26, 0x26, 255),
                                   Hover ? IM_COL32(0x5a, 0x5a, 0x5a, 255) : IM_COL32(0x38, 0x38, 0x38, 255), 10.0f);
                    DrawText(Ink, Faces.Regular, 10.0f,
                             ImVec2(InnerL + (InnerW - TextWidth(Faces.Regular, 10.0f, Control.Label)) * 0.5f, CY + 9.0f),
                             IM_COL32(0xd0, 0xd0, 0xd0, 255), Control.Label);
                    CY += 9.0f + 10.0f * 1.35f + 9.0f;
                    break;
                }
            }
        }
        Y += CardH + 12.0f;   // .cards gap
    }

    // .inspector-footer: dot + live caption left, "Name / Type" right, 9 px, margin-top 18.
    {
        Y += 6.0f;
        Ink->AddCircleFilled(ImVec2(CL + 2.0f, Y + 4.0f), 2.0f, IM_COL32(0x7f, 0x7f, 0x7f, 255));
        DrawText(Ink, Faces.Regular, 9.0f, ImVec2(CL + 8.0f, Y), IM_COL32(0x7a, 0x7a, 0x7a, 255), "Changes apply in real time");
        const std::string Right = Name + " / " + RecordClassificationName(Classification);
        DrawText(Ink, Faces.Regular, 9.0f, ImVec2(CR - TextWidth(Faces.Regular, 9.0f, Right), Y),
                 IM_COL32(0x7a, 0x7a, 0x7a, 255), Right);
        Y += 9.0f * 1.35f + 18.0f;
    }

    ImGui::SetCursorScreenPos(ImVec2(CL, Y));
    ImGui::Dummy(ImVec2(ContentW, 1.0f));
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

} // namespace Frontier

#else // FRONTIER_WITH_INTERFACE_SDK

namespace Frontier
{

void SceneInspectorPanel::ConstructInterface()
{
    // No interface SDK in this build: the view-model runs (and is validated) headlessly; drawing arrives
    //    with the SDK-backed build. Nothing is faked here.
}

} // namespace Frontier

#endif // FRONTIER_WITH_INTERFACE_SDK
