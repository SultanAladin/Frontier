/*==============================================================================================================================================
                                                          TEXTUREPAINTWORKSPACEPANEL.CPP
==============================================================================================================================================*/
// Blank shared-panel shell for the texture-paint workspace. It deliberately contains only the prototype's pane headers and carousel motion.

#include "TexturePaintWorkspacePanel.h"

#include "imgui.h"
#include "imgui_internal.h"

using namespace Frontier;

namespace TexturePaintWorkspaceValidation
{
namespace
{
    constexpr float CardWidth    = 860.0f;
    constexpr float CardHeight   = 740.0f;
    constexpr float RailWidth    = 420.0f;
    constexpr float PaneHeadH    = 46.0f;
    constexpr float SlideSeconds = 0.38f;
    constexpr float OpenSeconds  = 0.22f;

    const ImU32 CardFill    = IM_COL32(14, 14, 14, 252);
    const ImU32 PaneFill    = IM_COL32(14, 14, 14, 255);
    const ImU32 DetailFill  = IM_COL32(11, 11, 11, 255);
    const ImU32 HeadFill    = IM_COL32(18, 18, 20, 255);
    const ImU32 Border      = IM_COL32(28, 28, 28, 255);
    const ImU32 BorderHard  = IM_COL32(42, 42, 48, 255);
    const ImU32 Text        = IM_COL32(237, 237, 237, 255);
    const ImU32 Muted       = IM_COL32(138, 138, 138, 255);
    const ImU32 Faint       = IM_COL32(106, 106, 106, 255);

    float SolveCubicBezier(float Progress, float X1, float Y1, float X2, float Y2)
    {
        if (Progress <= 0.0f) { return 0.0f; }
        if (Progress >= 1.0f) { return 1.0f; }
        float Low = 0.0f, High = 1.0f, Guess = Progress;
        for (int Iteration = 0; Iteration < 20; ++Iteration)
        {
            const float O = 1.0f - Guess;
            const float X = 3.0f * O * O * Guess * X1 + 3.0f * O * Guess * Guess * X2 + Guess * Guess * Guess;
            if (X < Progress) { Low = Guess; } else { High = Guess; }
            Guess = (Low + High) * 0.5f;
        }
        const float O = 1.0f - Guess;
        return 3.0f * O * O * Guess * Y1 + 3.0f * O * Guess * Guess * Y2 + Guess * Guess * Guess;
    }

    void Advance(float& Value, bool On, float Dt, float Seconds)
    {
        const float Target = On ? 1.0f : 0.0f;
        const float Step = Seconds > 0.0f ? Dt / Seconds : 1.0f;
        Value = Target > Value ? ImMin(Target, Value + Step) : ImMax(Target, Value - Step);
    }

    bool RegionButton(const char* Id, ImVec2 TopLeft, ImVec2 Size)
    {
        ImGui::SetCursorScreenPos(TopLeft);
        return ImGui::InvisibleButton(Id, Size);
    }

    void DrawHeader(ImDrawList* Draw, ImVec2 TopLeft, float Width, const char* Label, const char* Sub,
                    bool Back, bool Forward, const char* ButtonId, bool& Activated)
    {
        const ImVec2 Max(TopLeft.x + Width, TopLeft.y + PaneHeadH);
        Draw->AddRectFilled(TopLeft, Max, HeadFill);
        Draw->AddLine(ImVec2(TopLeft.x, Max.y), Max, Border);

        float TextX = TopLeft.x + 10.0f;
        if (Back)
        {
            Draw->AddText(ImVec2(TextX, TopLeft.y + 14.0f), Muted, "<");
            TextX += 24.0f;
        }
        else
        {
            const ImVec2 IconMin(TextX, TopLeft.y + 10.0f), IconMax(TextX + 26.0f, TopLeft.y + 36.0f);
            Draw->AddRectFilled(IconMin, IconMax, IM_COL32(0, 0, 0, 255), 7.0f);
            Draw->AddRect(IconMin, IconMax, Border, 7.0f);
            Draw->AddCircleFilled(ImVec2(TextX + 13.0f, TopLeft.y + 23.0f), 3.0f, Muted);
            TextX += 35.0f;
        }

        Draw->AddText(ImVec2(TextX, TopLeft.y + 7.0f), Text, Label);
        Draw->AddText(ImVec2(TextX, TopLeft.y + 24.0f), Faint, Sub);
        if (Forward) { Draw->AddText(ImVec2(Max.x - 21.0f, TopLeft.y + 14.0f), Muted, ">"); }

        if (ButtonId != nullptr && RegionButton(ButtonId, TopLeft, ImVec2(Width, PaneHeadH))) { Activated = true; }
    }

    void DrawSlide(ImDrawList* Draw, ImVec2 TopLeft, const char* LeftLabel, const char* LeftSub,
                   const char* RightLabel, const char* RightSub, bool Back, bool Forward,
                   const char* ButtonId, bool& Activated)
    {
        const float DetailWidth = CardWidth - RailWidth;
        Draw->AddRectFilled(TopLeft, ImVec2(TopLeft.x + RailWidth, TopLeft.y + CardHeight), PaneFill);
        Draw->AddRectFilled(ImVec2(TopLeft.x + RailWidth, TopLeft.y),
                            ImVec2(TopLeft.x + CardWidth, TopLeft.y + CardHeight), DetailFill);
        Draw->AddLine(ImVec2(TopLeft.x + RailWidth, TopLeft.y),
                      ImVec2(TopLeft.x + RailWidth, TopLeft.y + CardHeight), Border);

        bool Ignored = false;
        DrawHeader(Draw, TopLeft, RailWidth, LeftLabel, LeftSub, Back, false, Back ? ButtonId : nullptr, Activated);
        DrawHeader(Draw, ImVec2(TopLeft.x + RailWidth, TopLeft.y), DetailWidth,
                   RightLabel, RightSub, false, Forward, Forward ? ButtonId : nullptr, Forward ? Activated : Ignored);
    }
}

void InitializeTexturePaintWorkspaceSample(TexturePaintWorkspaceState& State)
{
    State = TexturePaintWorkspaceState{};
    TexturePaintValidation::InitializeTexturePaintSummonedCard(State.PaintTools);
}

void ConstructTexturePaintWorkspacePanel(const ThemeConfiguration& Theme,
                                         TexturePaintWorkspaceState& State,
                                         SvgIconRegistry* Icons,
                                         PaintIconStore* StripStore)
{
    const ImGuiIO& Io = ImGui::GetIO();
    const ImVec2 WindowOrigin = ImGui::GetWindowPos();
    const ImVec2 WindowSpan = ImGui::GetWindowSize();

    // Right-click remains exclusively the paint-tool opener across the blank viewport.
    TexturePaintValidation::ConfineTexturePaintField(State.PaintTools, WindowOrigin, WindowSpan);
    TexturePaintValidation::ConstructTexturePaintSummonedCard(Theme, State.PaintTools, Icons, StripStore);

    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false) && !Io.WantTextInput)
    {
        if (!State.PanelOpen)
        {
            State.PanelRequested = true;
            State.RequestX = Io.MousePos.x;
            State.RequestY = Io.MousePos.y;
        }
        else { State.SlideForward = !State.SlideForward; }
    }
    if (State.PanelOpen && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        if (State.SlideForward) { State.SlideForward = false; }
        else { State.PanelOpen = false; }
    }

    if (State.PanelRequested)
    {
        const float Pad = 12.0f;
        float Left = State.RequestX + 10.0f;
        float Top = State.RequestY + 10.0f;
        if (Left + CardWidth > Io.DisplaySize.x - Pad) { Left = ImMax(Pad, Io.DisplaySize.x - Pad - CardWidth); }
        if (Top + CardHeight > Io.DisplaySize.y - Pad) { Top = ImMax(Pad, Io.DisplaySize.y - Pad - CardHeight); }
        State.PanelX = Left;
        State.PanelY = Top;
        State.PanelOpen = true;
        State.SlideForward = false;
        State.SlideTravel = 0.0f;
        State.OpenAge = 0.0f;
        State.PanelRequested = false;
    }

    Advance(State.SlideTravel, State.SlideForward, Io.DeltaTime, SlideSeconds);
    if (State.PanelOpen) { State.OpenAge = ImMin(OpenSeconds, State.OpenAge + Io.DeltaTime); }
    if (!State.PanelOpen) { return; }

    const ImVec2 CardTL(State.PanelX, State.PanelY);
    const ImVec2 CardBR(CardTL.x + CardWidth, CardTL.y + CardHeight);
    const bool OverCard = Io.MousePos.x >= CardTL.x && Io.MousePos.x <= CardBR.x &&
                          Io.MousePos.y >= CardTL.y && Io.MousePos.y <= CardBR.y;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !OverCard) { State.PanelOpen = false; return; }

    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), Io.DisplaySize, IM_COL32(0, 0, 0, 90));

    ImGui::SetNextWindowPos(CardTL);
    ImGui::SetNextWindowSize(ImVec2(CardWidth, CardHeight));
    if (State.OpenAge <= 0.0f) { ImGui::SetNextWindowFocus(); }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    ImGui::Begin("##TexturePaintWorkspaceCard", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);

    ImDrawList* Draw = ImGui::GetWindowDrawList();
    const float Pop = SolveCubicBezier(State.OpenAge / OpenSeconds, 0.16f, 1.0f, 0.3f, 1.0f);
    const float Scale = 0.98f + 0.02f * Pop;
    const ImVec2 Centre((CardTL.x + CardBR.x) * 0.5f, (CardTL.y + CardBR.y) * 0.5f);
    const ImVec2 PopTL(Centre.x - CardWidth * 0.5f * Scale, Centre.y - CardHeight * 0.5f * Scale);
    const ImVec2 PopBR(Centre.x + CardWidth * 0.5f * Scale, Centre.y + CardHeight * 0.5f * Scale);
    Draw->AddRectFilled(PopTL, PopBR, CardFill, 14.0f);
    Draw->AddRect(PopTL, PopBR, BorderHard, 14.0f);

    Draw->PushClipRect(CardTL, CardBR, true);
    const float Travel = SolveCubicBezier(State.SlideTravel, 0.4f, 0.0f, 0.2f, 1.0f);
    const float Dx = -Travel * CardWidth;
    bool Forward = false, Back = false;
    // 📝 Blank shared-panel shell: the pane-head chrome renders with NO labels yet — the layer-stack / properties / channels content they
    //    used to name is deliberately absent, and their replacement is awaiting further instructions. Only the header bands and the
    //    carousel motion survive the strip.
    DrawSlide(Draw, ImVec2(CardTL.x + Dx, CardTL.y),
              "", "", "", "",
              false, true, "##tpw-forward", Forward);
    DrawSlide(Draw, ImVec2(CardTL.x + Dx + CardWidth, CardTL.y),
              "", "", "", "",
              true, false, "##tpw-back", Back);
    Draw->PopClipRect();

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    if (Forward) { State.SlideForward = true; }
    if (Back) { State.SlideForward = false; }
}

} // namespace TexturePaintWorkspaceValidation
