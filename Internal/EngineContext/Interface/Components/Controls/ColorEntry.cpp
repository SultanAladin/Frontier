/*==============================================================================================================================================
                                                                COLORENTRY.CPP
==============================================================================================================================================*/
// 🧩 A colour row in the ControlsPreview.html style: a "[ ◉ rgba(…) | ▾ ]" swatch pill that toggles an inline Figma-style picker — an SV
//    gradient box, a rainbow hue bar, a checkerboard alpha bar with a colour-to-transparent gradient, a hex field and an A% readout. All
//    hand-drawn with the window draw list so it matches Figma exactly. Channels (0-1 RGB or RGBA) are caller-owned; the picker's working HSV
//    and open flag live in ImGui's per-widget storage so the control itself stays stateless.

#include "ColorEntry.h"

#include "ControlLayout.h"

#include "imgui.h"

#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    float Clamp01(float V) { return V < 0.0f ? 0.0f : (V > 1.0f ? 1.0f : V); }

    // 📝 Drag helper: while an invisible button over Area is held, feed the pointer position back through OnMove. Returns true on any move.
    template <typename Fn>
    bool DragRegion(const char* Id, ImVec2 Min, ImVec2 Size, Fn OnMove)
    {
        ImGui::SetCursorScreenPos(Min);
        ImGui::InvisibleButton(Id, ImVec2(Size.x > 1.0f ? Size.x : 1.0f, Size.y > 1.0f ? Size.y : 1.0f));
        if (ImGui::IsItemActive())
        {
            const ImVec2 M = ImGui::GetIO().MousePos;
            OnMove(Clamp01((M.x - Min.x) / (Size.x > 1.0f ? Size.x : 1.0f)),
                   Clamp01((M.y - Min.y) / (Size.y > 1.0f ? Size.y : 1.0f)));
            return true;
        }
        return false;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool ConstructColorEntry(const ThemeConfiguration& Theme, const ColorEntryDescriptor& Descriptor)
{
    if (Descriptor.Channels == nullptr)
    {
        return false;
    }

    ImGui::PushID(Descriptor.Label);
    if (!Descriptor.Enabled)
    {
        ImGui::BeginDisabled();
    }

    const float FieldWidth = BeginControlRow(Theme, Descriptor.Label);
    const float Height     = ResolvePillHeight(Theme);
    const ImVec2 Origin    = ImGui::GetCursorScreenPos();
    ImDrawList*  Draw      = ImGui::GetWindowDrawList();

    // 📝 Per-widget working state: the open flag and a mirrored HSV so hue/value survive when saturation hits an achromatic edge.
    ImGuiStorage* Store   = ImGui::GetStateStorage();
    const ImGuiID OpenKey = ImGui::GetID("##open");
    const ImGuiID SeedKey = ImGui::GetID("##seeded");
    const ImGuiID HueKey  = ImGui::GetID("##h");
    const ImGuiID SatKey  = ImGui::GetID("##s");
    const ImGuiID ValKey  = ImGui::GetID("##v");

    float* R = &Descriptor.Channels[0];
    float* G = &Descriptor.Channels[1];
    float* B = &Descriptor.Channels[2];
    float  A = Descriptor.IncludeAlpha ? Descriptor.Channels[3] : 1.0f;

    // 📝 Seed HSV from RGB once; thereafter HSV is authoritative for the picker so dragging is stable.
    float H = Store->GetFloat(HueKey, 0.0f);
    float S = Store->GetFloat(SatKey, 0.0f);
    float V = Store->GetFloat(ValKey, 0.0f);
    if (Store->GetInt(SeedKey, 0) == 0)
    {
        ImGui::ColorConvertRGBtoHSV(*R, *G, *B, H, S, V);
        Store->SetInt(SeedKey, 1);
        Store->SetFloat(HueKey, H); Store->SetFloat(SatKey, S); Store->SetFloat(ValKey, V);
    }

    bool Changed = false;
    auto CommitFromHsv = [&]()
    {
        ImGui::ColorConvertHSVtoRGB(H, S, V, *R, *G, *B);
        Store->SetFloat(HueKey, H); Store->SetFloat(SatKey, S); Store->SetFloat(ValKey, V);
        Changed = true;
    };

    // -- Swatch pill: [ circle rgba(...) | caret ] -------------------------------------------------------------------
    const ValuePillLayout Pill = DrawValuePill(Theme, Origin, ImVec2(FieldWidth, Height), nullptr, ">", Descriptor.Enabled);
    {
        const float  CircR = Height * 0.30f;
        const ImVec2 CircC(Pill.NumberMin.x + 14.0f + CircR, (Pill.NumberMin.y + Pill.NumberMax.y) * 0.5f);
        const ImU32  Swatch = ImGui::ColorConvertFloat4ToU32(ImVec4(*R, *G, *B, A));
        Draw->AddCircleFilled(CircC, CircR, Swatch, 24);
        Draw->AddCircle(CircC, CircR, ImGui::GetColorU32(IM_COL32(255, 255, 255, 40)), 24, 1.0f);

        char Caption[64];
        std::snprintf(Caption, sizeof(Caption), "rgba(%d, %d, %d, %.2f)",
                      int(*R * 255.0f + 0.5f), int(*G * 255.0f + 0.5f), int(*B * 255.0f + 0.5f), A);
        Draw->AddText(ImVec2(CircC.x + CircR + 12.0f, Pill.NumberMin.y + (Height - ImGui::GetFontSize()) * 0.5f),
                      Theme.Palette.ValueText, Caption);
    }

    ImGui::SetCursorScreenPos(Origin);
    if (ImGui::InvisibleButton("##swatch", ImVec2(FieldWidth, Height)) && Descriptor.Enabled)
    {
        Store->SetInt(OpenKey, Store->GetInt(OpenKey, 0) == 0 ? 1 : 0);
    }

    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + Height));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));

    // -- Inline Figma picker -----------------------------------------------------------------------------------------
    if (Store->GetInt(OpenKey, 0) != 0)
    {
        const float PadX     = 4.0f;
        const float PickW     = FieldWidth - PadX * 2.0f;
        const float SvH       = PickW * 0.62f;
        const float BarH      = Height * 0.34f;
        const float BarGap    = Theme.Metrics.ControlSpacing;
        const ImVec2 PBase    = ImGui::GetCursorScreenPos();
        const ImVec2 PanelMin(PBase.x, PBase.y + 4.0f);

        // 📝 Panel backdrop.
        float PanelH = 4.0f + SvH + BarGap + BarH + BarGap + BarH + BarGap + Height + 8.0f;
        Draw->AddRectFilled(PanelMin, ImVec2(PanelMin.x + FieldWidth, PanelMin.y + PanelH),
                            Theme.Palette.ControlBackground, ImGui::GetStyle().FrameRounding + 6.0f);

        float CursorY = PanelMin.y + 8.0f;

        // -- SV box: hue backdrop, white->transparent (x), transparent->black (y) ------------------------------------
        const ImVec2 SvMin(PanelMin.x + PadX + 8.0f, CursorY);
        const ImVec2 SvSize(PickW - 16.0f, SvH);
        const ImVec2 SvMax(SvMin.x + SvSize.x, SvMin.y + SvSize.y);
        float HueR, HueG, HueB;
        ImGui::ColorConvertHSVtoRGB(H, 1.0f, 1.0f, HueR, HueG, HueB);
        const ImU32 HueCol = ImGui::ColorConvertFloat4ToU32(ImVec4(HueR, HueG, HueB, 1.0f));
        Draw->AddRectFilledMultiColor(SvMin, SvMax, IM_COL32(255,255,255,255), HueCol, HueCol, IM_COL32(255,255,255,255));
        Draw->AddRectFilledMultiColor(SvMin, SvMax, IM_COL32(0,0,0,0), IM_COL32(0,0,0,0), IM_COL32(0,0,0,255), IM_COL32(0,0,0,255));
        Draw->AddRect(SvMin, SvMax, Theme.Palette.ValueOutline, ImGui::GetStyle().FrameRounding);
        // SV knob
        const ImVec2 SvKnob(SvMin.x + S * SvSize.x, SvMin.y + (1.0f - V) * SvSize.y);
        Draw->AddCircleFilled(SvKnob, 6.0f, ImGui::ColorConvertFloat4ToU32(ImVec4(*R, *G, *B, 1.0f)), 16);
        Draw->AddCircle(SvKnob, 6.0f, IM_COL32(255,255,255,255), 16, 2.0f);
        if (Descriptor.Enabled && DragRegion("##sv", SvMin, SvSize, [&](float u, float v){ S = u; V = 1.0f - v; }))
        {
            CommitFromHsv();
        }
        CursorY += SvH + BarGap;

        // -- Hue bar (rainbow) ---------------------------------------------------------------------------------------
        const ImVec2 HueMin(SvMin.x, CursorY);
        const ImVec2 HueSize(SvSize.x, BarH);
        {
            const int Segments = 6;
            static const ImU32 Stops[7] =
            { IM_COL32(255,0,0,255), IM_COL32(255,255,0,255), IM_COL32(0,255,0,255), IM_COL32(0,255,255,255),
              IM_COL32(0,0,255,255), IM_COL32(255,0,255,255), IM_COL32(255,0,0,255) };
            const float Step = HueSize.x / Segments;
            for (int I = 0; I < Segments; ++I)
            {
                const ImVec2 A0(HueMin.x + Step * I, HueMin.y);
                const ImVec2 A1(HueMin.x + Step * (I + 1), HueMin.y + HueSize.y);
                Draw->AddRectFilledMultiColor(A0, A1, Stops[I], Stops[I + 1], Stops[I + 1], Stops[I]);
            }
            Draw->AddRect(HueMin, ImVec2(HueMin.x + HueSize.x, HueMin.y + HueSize.y), Theme.Palette.ValueOutline, HueSize.y * 0.5f);
            const ImVec2 HK(HueMin.x + (H) * HueSize.x, HueMin.y + HueSize.y * 0.5f);
            Draw->AddCircleFilled(HK, HueSize.y * 0.55f, IM_COL32(255,255,255,255), 16);
            Draw->AddCircle(HK, HueSize.y * 0.55f, IM_COL32(27,27,30,255), 16, 2.0f);
        }
        if (Descriptor.Enabled && DragRegion("##hue", HueMin, HueSize, [&](float u, float){ H = u; }))
        {
            CommitFromHsv();
        }
        CursorY += BarH + BarGap;

        // -- Alpha bar (checkerboard + colour->transparent gradient) -------------------------------------------------
        if (Descriptor.IncludeAlpha)
        {
            const ImVec2 AlMin(SvMin.x, CursorY);
            const ImVec2 AlSize(SvSize.x, BarH);
            const ImVec2 AlMax(AlMin.x + AlSize.x, AlMin.y + AlSize.y);
            // checkerboard
            const float Cell = BarH * 0.5f;
            for (float y = AlMin.y; y < AlMax.y; y += Cell)
            {
                for (float x = AlMin.x; x < AlMax.x; x += Cell)
                {
                    const bool Dark = (int((x - AlMin.x) / Cell) + int((y - AlMin.y) / Cell)) % 2 == 0;
                    Draw->AddRectFilled(ImVec2(x, y), ImVec2(x + Cell < AlMax.x ? x + Cell : AlMax.x, y + Cell < AlMax.y ? y + Cell : AlMax.y),
                                        Dark ? IM_COL32(128,128,128,255) : IM_COL32(192,192,192,255));
                }
            }
            const ImU32 Opaque = ImGui::ColorConvertFloat4ToU32(ImVec4(*R, *G, *B, 1.0f));
            const ImU32 Clear  = ImGui::ColorConvertFloat4ToU32(ImVec4(*R, *G, *B, 0.0f));
            Draw->AddRectFilledMultiColor(AlMin, AlMax, Clear, Opaque, Opaque, Clear);
            Draw->AddRect(AlMin, AlMax, Theme.Palette.ValueOutline, AlSize.y * 0.5f);
            const ImVec2 AK(AlMin.x + A * AlSize.x, AlMin.y + AlSize.y * 0.5f);
            Draw->AddCircleFilled(AK, AlSize.y * 0.55f, IM_COL32(255,255,255,255), 16);
            Draw->AddCircle(AK, AlSize.y * 0.55f, IM_COL32(27,27,30,255), 16, 2.0f);
            if (Descriptor.Enabled && DragRegion("##alpha", AlMin, AlSize, [&](float u, float){ A = u; }))
            {
                Descriptor.Channels[3] = A;
                Changed = true;
            }
            CursorY += BarH + BarGap;
        }

        // -- Hex field + A% readout ----------------------------------------------------------------------------------
        {
            const ImVec2 HexMin(SvMin.x, CursorY);
            char Hex[8];
            std::snprintf(Hex, sizeof(Hex), "#%02X%02X%02X",
                          int(*R * 255.0f + 0.5f), int(*G * 255.0f + 0.5f), int(*B * 255.0f + 0.5f));

            ImGui::SetCursorScreenPos(HexMin);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, Theme.Palette.ValueNumberSegment);
            ImGui::PushStyleColor(ImGuiCol_Text,    Theme.Palette.ValueText);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, (Height - ImGui::GetFontSize()) * 0.5f));
            ImGui::SetNextItemWidth(SvSize.x * 0.5f);
            if (Descriptor.Enabled && ImGui::InputText("##hex", Hex, sizeof(Hex),
                                                       ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue))
            {
                // 📝 Parse "#RRGGBB" / "RRGGBB" by hand (portable, no sscanf) — accept exactly 6 hex digits.
                const char* P = (Hex[0] == '#') ? Hex + 1 : Hex;
                unsigned int Rgb   = 0;
                int          Count = 0;
                bool         Valid = true;
                for (; P[Count] != '\0'; ++Count)
                {
                    const char C = P[Count];
                    int Nibble;
                    if      (C >= '0' && C <= '9') { Nibble = C - '0'; }
                    else if (C >= 'a' && C <= 'f') { Nibble = C - 'a' + 10; }
                    else if (C >= 'A' && C <= 'F') { Nibble = C - 'A' + 10; }
                    else { Valid = false; break; }
                    Rgb = (Rgb << 4) | static_cast<unsigned int>(Nibble);
                }
                if (Valid && Count == 6)
                {
                    *R = ((Rgb >> 16) & 0xFF) / 255.0f;
                    *G = ((Rgb >> 8)  & 0xFF) / 255.0f;
                    *B = ( Rgb        & 0xFF) / 255.0f;
                    ImGui::ColorConvertRGBtoHSV(*R, *G, *B, H, S, V);
                    Store->SetFloat(HueKey, H); Store->SetFloat(SatKey, S); Store->SetFloat(ValKey, V);
                    Changed = true;
                }
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);

            char ARead[16];
            std::snprintf(ARead, sizeof(ARead), "A %d%%", int(A * 100.0f + 0.5f));
            const ImVec2 ASize = ImGui::CalcTextSize(ARead);
            Draw->AddText(ImVec2(HexMin.x + SvSize.x - ASize.x, HexMin.y + (Height - ASize.y) * 0.5f),
                          Theme.Palette.TextMuted, ARead);

            CursorY += Height + 8.0f;
        }

        ImGui::SetCursorScreenPos(ImVec2(Origin.x, PanelMin.y + PanelH));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }

    EndControlRow(Theme);

    if (!Descriptor.Enabled)
    {
        ImGui::EndDisabled();
    }
    ImGui::PopID();

    return Changed && Descriptor.Enabled;
}

}   // namespace Frontier
