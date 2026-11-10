/*==========================================================================================================================================
                                                           WORKSPACEEVIDENCE.CPP
==========================================================================================================================================*/
// 🧩 Headless interaction evidence: the real editor workspace rendered by a CPU rasterizer under a scripted pointer/keyboard.

#include "Engine/Diagnostics/DiagnosticJournal.h"
#include "Engine/Interface/IconPresentation.h"
#include "Input/InputSubstrate.h"
#include "UI/Editor/ControlCentrePanel.h"
#include "Workspace/WorkspaceHost.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// 📝 Every widget on screen is drawn by the production units (PanelDockspace, ControlCentrePanel notch,
//    SceneDirectoryPanel, LogStreamPanel, RecordInspectorPanel, ViewportPanel, InputPanel). This harness
//    only plays the OS host: an ImGui context, synthetic pointer/keyboard events fed to BOTH ImGui IO and
//    the shared InputSubstrate (so F5/F6/Ctrl+L act through the real InputMap), and a CPU rasterizer of
//    the resulting draw data. Widget positions are LEARNED in a dry run (sweep until the view-models
//    report the effect), then the recorded take clicks the measured coordinates — no fake state injection.
//    The white pointer triangle is drawn by the harness (an OS cursor is never in ImGui draw data).

namespace
{

using namespace Frontier;

//----------------------------------------------------------------------------------------------------------------------
//                                                    CPU RASTERIZER
//----------------------------------------------------------------------------------------------------------------------

constexpr int CaptureWidth  = 1600;   // [px]
constexpr int CaptureHeight = 900;    // [px]

struct Pixel
{
    uint8_t Red, Green, Blue, Alpha;
};

std::vector<Pixel>   Framebuffer;
const unsigned char* AtlasPixels  = nullptr;
int                  AtlasWidth   = 0;
int                  AtlasHeight  = 0;
const unsigned char* SamplePixels = nullptr;   // Texture bound for the command being rasterized
int                  SampleWidth  = 0;
int                  SampleHeight = 0;
ImTextureID          AtlasId     = ImTextureID(0xA77A5);

float SampleTexture(float U, float V, int Channel)
{
    const float X  = std::clamp(U * static_cast<float>(SampleWidth) - 0.5f, 0.0f, static_cast<float>(SampleWidth - 1));
    const float Y  = std::clamp(V * static_cast<float>(SampleHeight) - 0.5f, 0.0f, static_cast<float>(SampleHeight - 1));
    const int   X0 = static_cast<int>(X);
    const int   Y0 = static_cast<int>(Y);
    const int   X1 = std::min(X0 + 1, SampleWidth - 1);
    const int   Y1 = std::min(Y0 + 1, SampleHeight - 1);
    const float Fx = X - static_cast<float>(X0);
    const float Fy = Y - static_cast<float>(Y0);
    const auto  At = [&](int Px, int Py) {
        return static_cast<float>(SamplePixels[(static_cast<size_t>(Py) * SampleWidth + Px) * 4 + Channel]) / 255.0f;
    };
    const float Top    = At(X0, Y0) + (At(X1, Y0) - At(X0, Y0)) * Fx;
    const float Bottom = At(X0, Y1) + (At(X1, Y1) - At(X0, Y1)) * Fx;
    return Top + (Bottom - Top) * Fy;
}

void RasterizeTriangle(const ImDrawVert& A, const ImDrawVert& B, const ImDrawVert& C, const ImVec4& Clip, bool UseTexture)
{
    const float Area = (B.pos.x - A.pos.x) * (C.pos.y - A.pos.y) - (B.pos.y - A.pos.y) * (C.pos.x - A.pos.x);
    if (Area == 0.0f)
    {
        return;
    }
    const int MinX = std::max({static_cast<int>(std::floor(std::min({A.pos.x, B.pos.x, C.pos.x}))),
                               static_cast<int>(Clip.x), 0});
    const int MinY = std::max({static_cast<int>(std::floor(std::min({A.pos.y, B.pos.y, C.pos.y}))),
                               static_cast<int>(Clip.y), 0});
    const int MaxX = std::min({static_cast<int>(std::ceil(std::max({A.pos.x, B.pos.x, C.pos.x}))),
                               static_cast<int>(Clip.z), CaptureWidth});
    const int MaxY = std::min({static_cast<int>(std::ceil(std::max({A.pos.y, B.pos.y, C.pos.y}))),
                               static_cast<int>(Clip.w), CaptureHeight});
    for (int Y = MinY; Y < MaxY; ++Y)
    {
        for (int X = MinX; X < MaxX; ++X)
        {
            const float Px = static_cast<float>(X) + 0.5f;
            const float Py = static_cast<float>(Y) + 0.5f;
            const float W0 = ((B.pos.x - Px) * (C.pos.y - Py) - (B.pos.y - Py) * (C.pos.x - Px)) / Area;
            const float W1 = ((C.pos.x - Px) * (A.pos.y - Py) - (C.pos.y - Py) * (A.pos.x - Px)) / Area;
            const float W2 = 1.0f - W0 - W1;
            if (W0 < 0.0f || W1 < 0.0f || W2 < 0.0f)
            {
                continue;
            }
            const float U  = A.uv.x * W0 + B.uv.x * W1 + C.uv.x * W2;
            const float V  = A.uv.y * W0 + B.uv.y * W1 + C.uv.y * W2;
            float       Sr = 1.0f, Sg = 1.0f, Sb = 1.0f, Sa = 1.0f;
            if (UseTexture)
            {
                Sr = SampleTexture(U, V, 0);
                Sg = SampleTexture(U, V, 1);
                Sb = SampleTexture(U, V, 2);
                Sa = SampleTexture(U, V, 3);
            }
            const ImU32 ColA = A.col, ColB = B.col, ColC = C.col;
            const float Vr = ((ColA >> 0) & 0xff) * W0 + ((ColB >> 0) & 0xff) * W1 + ((ColC >> 0) & 0xff) * W2;
            const float Vg = ((ColA >> 8) & 0xff) * W0 + ((ColB >> 8) & 0xff) * W1 + ((ColC >> 8) & 0xff) * W2;
            const float Vb = ((ColA >> 16) & 0xff) * W0 + ((ColB >> 16) & 0xff) * W1 + ((ColC >> 16) & 0xff) * W2;
            const float Va = ((ColA >> 24) & 0xff) * W0 + ((ColB >> 24) & 0xff) * W1 + ((ColC >> 24) & 0xff) * W2;
            const float Alpha = (Va / 255.0f) * Sa;
            if (Alpha <= 0.0f)
            {
                continue;
            }
            Pixel& Dst   = Framebuffer[static_cast<size_t>(Y) * CaptureWidth + X];
            Dst.Red   = static_cast<uint8_t>(std::clamp((Vr / 255.0f) * Sr * Alpha + (Dst.Red / 255.0f) * (1.0f - Alpha), 0.0f, 1.0f) * 255.0f);
            Dst.Green = static_cast<uint8_t>(std::clamp((Vg / 255.0f) * Sg * Alpha + (Dst.Green / 255.0f) * (1.0f - Alpha), 0.0f, 1.0f) * 255.0f);
            Dst.Blue  = static_cast<uint8_t>(std::clamp((Vb / 255.0f) * Sb * Alpha + (Dst.Blue / 255.0f) * (1.0f - Alpha), 0.0f, 1.0f) * 255.0f);
            Dst.Alpha = 255;
        }
    }
}

// The harness is the texture backend: acknowledge atlas creates/updates/destroys so producers
// (IconPresentation) can build and retire their textures exactly as under the Vulkan adapter.
void ServiceTextures()
{
    for (ImTextureData* Texture : ImGui::GetPlatformIO().Textures)
    {
        if (Texture->Status == ImTextureStatus_WantCreate || Texture->Status == ImTextureStatus_WantUpdates)
        {
            Texture->SetStatus(ImTextureStatus_OK);
        }
        else if (Texture->Status == ImTextureStatus_WantDestroy)
        {
            Texture->SetStatus(ImTextureStatus_Destroyed);
        }
    }
}

void RasterizeDrawData(const ImDrawData& DrawData)
{
    for (int ListIndex = 0; ListIndex < DrawData.CmdListsCount; ++ListIndex)
    {
        const ImDrawList* List = DrawData.CmdLists[ListIndex];
        for (const ImDrawCmd& Command : List->CmdBuffer)
        {
            if (Command.UserCallback != nullptr)
            {
                continue;
            }
            const ImVec4 Clip(Command.ClipRect.x - DrawData.DisplayPos.x, Command.ClipRect.y - DrawData.DisplayPos.y,
                              Command.ClipRect.z - DrawData.DisplayPos.x, Command.ClipRect.w - DrawData.DisplayPos.y);
            bool UseTexture = false;
            if (Command.GetTexID() == AtlasId)
            {
                SamplePixels = AtlasPixels;
                SampleWidth  = AtlasWidth;
                SampleHeight = AtlasHeight;
                UseTexture   = SamplePixels != nullptr;
            }
            else
            {
                // Icon atlases and other user textures sample from their own CPU pixels.
                for (ImTextureData* Texture : ImGui::GetPlatformIO().Textures)
                {
                    if (Texture->TexID == Command.GetTexID() && Texture->Format == ImTextureFormat_RGBA32 &&
                        Texture->Pixels != nullptr)
                    {
                        SamplePixels = Texture->Pixels;
                        SampleWidth  = Texture->Width;
                        SampleHeight = Texture->Height;
                        UseTexture   = true;
                        break;
                    }
                }
            }
            for (unsigned int Index = 0; Index + 2 < Command.ElemCount; Index += 3)
            {
                const ImDrawVert& A = List->VtxBuffer[List->IdxBuffer[Command.IdxOffset + Index] + Command.VtxOffset];
                const ImDrawVert& B = List->VtxBuffer[List->IdxBuffer[Command.IdxOffset + Index + 1] + Command.VtxOffset];
                const ImDrawVert& C = List->VtxBuffer[List->IdxBuffer[Command.IdxOffset + Index + 2] + Command.VtxOffset];
                RasterizeTriangle(A, B, C, Clip, UseTexture);
            }
        }
    }
}

void OverlayPointer(float X, float Y)
{
    // Harness-drawn evidence cursor: a white arrow with a dark rim (see file header note).
    const auto Plot = [&](int Px, int Py, Pixel Colour) {
        if (Px >= 0 && Py >= 0 && Px < CaptureWidth && Py < CaptureHeight)
        {
            Framebuffer[static_cast<size_t>(Py) * CaptureWidth + Px] = Colour;
        }
    };
    for (int Row = 0; Row < 16; ++Row)
    {
        const int Span = Row < 11 ? Row * 11 / 16 + 1 : std::max(17 - Row, 2);
        for (int Column = 0; Column < Span; ++Column)
        {
            const bool Rim = Column == 0 || Column == Span - 1 || Row == 0 || Row == 15;
            Plot(static_cast<int>(X) + Column, static_cast<int>(Y) + Row,
                 Rim ? Pixel{10, 10, 10, 255} : Pixel{245, 245, 245, 255});
        }
    }
}

bool WritePpm(const std::filesystem::path& Path)
{
    FILE* File = std::fopen(Path.string().c_str(), "wb");
    if (File == nullptr)
    {
        return false;
    }
    std::fprintf(File, "P6\n%d %d\n255\n", CaptureWidth, CaptureHeight);
    for (const Pixel& Entry : Framebuffer)
    {
        const uint8_t Rgb[3] = {Entry.Red, Entry.Green, Entry.Blue};
        std::fwrite(Rgb, 1, 3, File);
    }
    std::fclose(File);
    return true;
}

//----------------------------------------------------------------------------------------------------------------------
//                                                    SCRIPTED INTERACTION
//----------------------------------------------------------------------------------------------------------------------

ImGuiKey ToImGuiKey(InputKeyCategory Key)
{
    using K = InputKeyCategory;
    if (Key >= K::A && Key <= K::Z)
    {
        return static_cast<ImGuiKey>(ImGuiKey_A + (static_cast<int>(Key) - static_cast<int>(K::A)));
    }
    if (Key >= K::Digit0 && Key <= K::Digit9)
    {
        return static_cast<ImGuiKey>(ImGuiKey_0 + (static_cast<int>(Key) - static_cast<int>(K::Digit0)));
    }
    if (Key >= K::F1 && Key <= K::F12)
    {
        return static_cast<ImGuiKey>(ImGuiKey_F1 + (static_cast<int>(Key) - static_cast<int>(K::F1)));
    }
    switch (Key)
    {
        case K::Escape: return ImGuiKey_Escape;
        case K::Enter:  return ImGuiKey_Enter;
        case K::Space:  return ImGuiKey_Space;
        case K::Tab:    return ImGuiKey_Tab;
        case K::Left:   return ImGuiKey_LeftArrow;
        case K::Right:  return ImGuiKey_RightArrow;
        case K::Up:     return ImGuiKey_UpArrow;
        case K::Down:   return ImGuiKey_DownArrow;
        default:        return ImGuiKey_None;
    }
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    const std::filesystem::path OutputDirectory = ArgumentCount > 1 ? Arguments[1] : "EvidenceFrames";
    const std::filesystem::path ContentRoot     = ArgumentCount > 2 ? Arguments[2] : std::filesystem::current_path();
    std::filesystem::create_directories(OutputDirectory);
    const std::filesystem::path ScratchLayout = OutputDirectory / "evidence-layout.toml";
    const std::filesystem::path ScratchInput  = OutputDirectory / "evidence-input.toml";
    std::error_code             Error;
    std::filesystem::remove(ScratchLayout, Error);
    std::filesystem::remove(ScratchInput, Error);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& Io    = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.DisplaySize = ImVec2(static_cast<float>(CaptureWidth), static_cast<float>(CaptureHeight));
    Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    Io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;   // CPU rasterizer services user textures
    IconPresentation::Attach(ContentRoot / "EngineContent/Icons");
    Io.AddMousePosEvent(-1.0f, -1.0f);
    Framebuffer.resize(static_cast<size_t>(CaptureWidth) * CaptureHeight);

    DiagnosticJournal::Initialize("FrontierEditorEvidence");
    ControlCentrePanel Centre(ContentRoot / "EngineContent/FontArchives", {}, {});
    Centre.ConfigureContentBrowserVisibility(false);   // As both production hosts do; the overlay eats clicks
    if (!Centre.Initialize() || !Centre.Activate())
    {
        std::printf("evidence refused: %s\n", Centre.QueryDiagnostic().c_str());
        return 3;
    }

    InputSubstrate Substrate;
    WorkspaceHost  Workspace(Centre, Substrate, ScratchLayout, ScratchInput);
    (void)Workspace.ConfigurePlatform();

    bool        Recording    = false;
    std::string Segment;
    int         SegmentFrame = 0;
    int         SinceSample  = 0;
    float       CursorX      = -50.0f, CursorY = -50.0f;
    long long   RasterMillis = 0;
    int         RasterCount  = 0;

    const auto RasterizeTo = [&](const std::filesystem::path& Path) {
        const auto Start = std::chrono::steady_clock::now();
        unsigned char* Pixels = nullptr;
        Io.Fonts->GetTexDataAsRGBA32(&Pixels, &AtlasWidth, &AtlasHeight);
        AtlasPixels = Pixels;
        Io.Fonts->SetTexID(AtlasId);
        std::fill(Framebuffer.begin(), Framebuffer.end(), Pixel{11, 16, 14, 255});
        ServiceTextures();
        RasterizeDrawData(*ImGui::GetDrawData());
        OverlayPointer(CursorX, CursorY);
        WritePpm(Path);
        RasterMillis += std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - Start).count();
        ++RasterCount;
    };

    const std::function<void(double)> Step = [&](double Seconds) {
        Centre.PrepareInterface();
        if (!Io.Fonts->IsBuilt())
        {
            Io.Fonts->Build();
            Io.Fonts->SetTexID(AtlasId);
        }
        Io.DeltaTime = static_cast<float>(std::max(Seconds, 1.0 / 240.0));
        const float InterfaceScale =
            std::clamp(Centre.QueryAppearance().QueryApplied().InterfaceScale / 100.0f, 0.6f, 2.0f);
        ImGui::NewFrame();
        (void)Centre.Integrate(Seconds);
        Substrate.BeginCycle();
        Workspace.Synchronize(Seconds);
        Workspace.ConstructInterface(InterfaceScale);
        Centre.ConstructInterface();
        ImGui::Render();
        if (Recording && ++SinceSample >= 3)
        {
            SinceSample = 0;
            char Name[256];
            std::snprintf(Name, sizeof(Name), "%s-%04d.ppm", Segment.c_str(), SegmentFrame++);
            RasterizeTo(OutputDirectory / Name);
        }
    };
    const auto Settle = [&](int Steps) {
        for (int Index = 0; Index < Steps; ++Index)
        {
            Step(1.0 / 60.0);
        }
    };
    const auto MoveMouse = [&](float X, float Y) {
        CursorX = X;
        CursorY = Y;
        Io.AddMousePosEvent(X, Y);
        Substrate.InjectPointerPosition(X, Y);
        Step(1.0 / 60.0);
    };
    const auto MouseDown = [&]() {
        Io.AddMouseButtonEvent(0, true);
        Substrate.InjectPointerButtonTransition(PointerButtonCategory::Left, true);
        Step(1.0 / 60.0);
    };
    const auto MouseUp = [&]() {
        Io.AddMouseButtonEvent(0, false);
        Substrate.InjectPointerButtonTransition(PointerButtonCategory::Left, false);
        Step(1.0 / 60.0);
    };
    const std::function<void(float, float)> Click = [&](float X, float Y) {
        MoveMouse(X, Y);
        MouseDown();
        MouseUp();
    };
    const auto DragMouse = [&](float ToX, float ToY, int Steps) {
        const float FromX = CursorX, FromY = CursorY;
        for (int Index = 1; Index <= Steps; ++Index)
        {
            const float T = static_cast<float>(Index) / static_cast<float>(Steps);
            MoveMouse(FromX + (ToX - FromX) * T, FromY + (ToY - FromY) * T);
        }
    };
    const auto KeyEventBoth = [&](InputKeyCategory Key, bool Down, InputModifiers Modifiers) {
        Substrate.InjectModifiers(Modifiers);
        Substrate.InjectKeyTransition(Key, Down, false);
        Io.AddKeyEvent(ImGuiKey_LeftCtrl, Modifiers.Control && Down);
        Io.AddKeyEvent(ImGuiKey_LeftShift, Modifiers.Shift && Down);
        if (Down)
        {
            Io.AddKeyEvent(ImGuiKey_LeftCtrl, Modifiers.Control);
            Io.AddKeyEvent(ImGuiKey_LeftShift, Modifiers.Shift);
        }
        Io.AddKeyEvent(ToImGuiKey(Key), Down);
    };
    const auto KeyTap = [&](InputKeyCategory Key, InputModifiers Modifiers = {}) {
        KeyEventBoth(Key, true, Modifiers);
        Step(1.0 / 60.0);
        KeyEventBoth(Key, false, Modifiers);
        Step(1.0 / 60.0);
    };
    const auto TypeText = [&](const char* Text) {
        for (const char* Cursor = Text; *Cursor != '\0'; ++Cursor)
        {
            Io.AddInputCharacter(static_cast<unsigned int>(*Cursor));
            Step(1.0 / 60.0);
            Step(1.0 / 60.0);
        }
    };
    const auto FindPanel = [&](const char* CaptionAndKey) -> ImGuiWindow* {
        return ImGui::FindWindowByName(CaptionAndKey);
    };
    const auto Still = [&](const char* Name) {
        char File[256];
        std::snprintf(File, sizeof(File), "%s.ppm", Name);
        RasterizeTo(OutputDirectory / File);
        std::printf("still -> %s\n", File);
    };

    int Failures = 0;
    const auto Expect = [&](bool Condition, const char* Description) {
        std::printf("%s %s\n", Condition ? "ok  " : "FAIL", Description);
        if (!Condition)
        {
            ++Failures;
        }
    };

    Settle(90);   // Dock seating, tab motion and scroll prime
    {
        ImGuiContext* Context = ImGui::GetCurrentContext();
        for (ImGuiWindow* Window : Context->Windows)
        {
            std::printf("window '%s' at (%.0f, %.0f) size (%.0f, %.0f)\n", Window->Name, static_cast<double>(Window->Pos.x),
                        static_cast<double>(Window->Pos.y), static_cast<double>(Window->Size.x),
                        static_cast<double>(Window->Size.y));
        }
    }
    Step(1.0 / 60.0);
    Still("still-workspace-overview");

    ImGuiWindow* OutlinerWindow = FindPanel("Outliner###FrontierPanel-Outliner");
    ImGuiWindow* LogWindow      = FindPanel("Log Stream###FrontierPanel-LogStream");
    ImGuiWindow* InspectorWindow = FindPanel("Inspector###FrontierPanel-Inspector");
    ImGuiWindow* ViewportWindow = FindPanel("Viewport###FrontierPanel-Viewport");
    Expect(OutlinerWindow != nullptr && LogWindow != nullptr && InspectorWindow != nullptr && ViewportWindow != nullptr,
           "all four workspace panel windows seated");
    if (OutlinerWindow == nullptr || LogWindow == nullptr)
    {
        return 4;
    }

    // Dry-run learning (unrecorded): sweep until the view-models report the effect -------------------
    SceneDirectoryPanel& Outliner = Workspace.QueryOutlinerPanel();
    const auto FindRowToken = [&](const char* Name) {
        for (const SceneDirectoryRow& Row : Outliner.QueryRows())
        {
            if (Row.Name == Name)
            {
                return Row.Token;
            }
        }
        return RecordToken{};
    };
    const auto LearnRowY = [&](const char* Name) -> float {
        const RecordToken Target = FindRowToken(Name);
        for (float Y = OutlinerWindow->Pos.y + 175.0f; Y < OutlinerWindow->Pos.y + OutlinerWindow->Size.y - 40.0f; Y += 4.0f)
        {
            Click(OutlinerWindow->Pos.x + 130.0f, Y);
            if (Outliner.QuerySelected() == Target)
            {
                std::printf("learned row '%s' at y=%.0f\n", Name, static_cast<double>(Y));
                return Y;
            }
        }
        return -1.0f;
    };
    const float FrontLeftY = LearnRowY("Front Left");
    const float VehicleY   = LearnRowY("VehicleHMI Test Vehicle");
    Expect(FrontLeftY > 0 && VehicleY > 0, "outliner row positions learned");

    const auto RowShown = [&](const char* Name) {
        for (const SceneDirectoryRow& Row : Outliner.QueryVisible())
        {
            if (Row.Name == Name)
            {
                return true;
            }
        }
        return false;
    };
    float ChevronY = -1.0f;
    if (VehicleY > 0)
    {
        for (float Y = VehicleY - 4.0f; Y < VehicleY + 30.0f && ChevronY < 0; Y += 3.0f)
        {
            Click(OutlinerWindow->Pos.x + 32.0f, Y);
            Settle(3);
            if (!RowShown("Front Left"))
            {
                ChevronY = Y;
                std::printf("learned vehicle chevron at y=%.0f\n", static_cast<double>(Y));
            }
        }
        if (ChevronY > 0)
        {
            Click(OutlinerWindow->Pos.x + 32.0f, ChevronY);   // Re-expand for the recorded take
            Settle(4);
        }
    }
    Expect(ChevronY > 0 && RowShown("Front Left"), "vehicle chevron learned (collapse + expand)");

    float FilterX = -1.0f, FilterY = -1.0f;
    for (float Y = OutlinerWindow->Pos.y + 135.0f; Y < OutlinerWindow->Pos.y + 175.0f && FilterY < 0; Y += 4.0f)
    {
        Click(OutlinerWindow->Pos.x + 120.0f, Y);
        if (Io.WantTextInput)
        {
            FilterX = OutlinerWindow->Pos.x + 120.0f;
            FilterY = Y;
            std::printf("learned filter field at (%.0f, %.0f)\n", static_cast<double>(FilterX), static_cast<double>(FilterY));
        }
    }
    Expect(FilterY > 0, "outliner filter field learned");
    KeyTap(InputKeyCategory::Escape);
    // Clear side effects of the learning sweeps (pill clicks set classification filters).
    Outliner.ConfigureClassificationFilter(std::nullopt);
    Outliner.ConfigureTextFilter("");
    Outliner.ClearSelection();
    Settle(4);

    float AddX = -1.0f, AddY = -1.0f, SpawnItemX = -1.0f, SpawnItemY = -1.0f;
    {
        const auto PopupOpen = []() {
            ImGuiContext* Context = ImGui::GetCurrentContext();
            return Context != nullptr && Context->OpenPopupStack.Size > 0;
        };
        for (float Y = OutlinerWindow->Pos.y + 34.0f; Y < OutlinerWindow->Pos.y + 80.0f && AddY < 0; Y += 4.0f)
        {
            for (float X = OutlinerWindow->Pos.x + OutlinerWindow->Size.x - 60.0f;
                 X < OutlinerWindow->Pos.x + OutlinerWindow->Size.x - 6.0f && AddY < 0; X += 6.0f)
            {
                Click(X, Y);
                if (PopupOpen())
                {
                    AddX = X;
                    AddY = Y;
                    std::printf("learned add button at (%.0f, %.0f)\n", static_cast<double>(X), static_cast<double>(Y));
                }
            }
        }
        if (AddY > 0)
        {
            Step(1.0 / 60.0);
            Still("still-spawn-menu-debug");
            const auto FindPopup = []() -> ImGuiWindow* {
                ImGuiContext* Context = ImGui::GetCurrentContext();
                if (Context == nullptr) return nullptr;
                for (ImGuiWindow* Window : Context->Windows)
                {
                    if (Window != nullptr && Window->WasActive && (Window->Flags & ImGuiWindowFlags_Popup) != 0)
                        return Window;
                }
                return nullptr;
            };
            ImGuiWindow* Popup = FindPopup();
            if (Popup != nullptr)
            {
                std::printf("spawn popup '%s' at (%.0f, %.0f) size (%.0f, %.0f)\n", Popup->Name,
                            static_cast<double>(Popup->Pos.x), static_cast<double>(Popup->Pos.y),
                            static_cast<double>(Popup->Size.x), static_cast<double>(Popup->Size.y));
                const float  SweepX = Popup->Pos.x + Popup->Size.x * 0.5f;
                const float  Top    = Popup->Pos.y + 6.0f;
                const float  Bottom = Popup->Pos.y + Popup->Size.y - 4.0f;
                const size_t Before = Workspace.QueryFixture().QueryRecordCount();
                for (float Y = Top; Y < Bottom && SpawnItemY < 0; Y += 4.0f)
                {
                    Click(SweepX, Y);
                    if (Workspace.QueryFixture().QueryRecordCount() != Before)
                    {
                        SpawnItemX = SweepX;
                        SpawnItemY = Y;
                        std::printf("learned spawn item at (%.0f, %.0f)\n", static_cast<double>(SpawnItemX),
                                    static_cast<double>(SpawnItemY));
                    }
                    else if (!PopupOpen())
                    {
                        Click(AddX, AddY);   // Miss closed the menu; reopen and keep sweeping
                    }
                }
            }
            else
            {
                std::printf("spawn popup window not found\n");
            }
        }
    }
    Expect(AddY > 0 && SpawnItemY > 0, "outliner spawn flow learned");
    Outliner.ClearSelection();
    Settle(10);

    // (Inspector is intentionally blank in this step; there is no rename field to learn.)

    // Log Stream pause toggle.
    float PauseX = -1.0f, PauseY = -1.0f;
    {
        LogStreamPanel& LogPanel = Workspace.QueryLogPanel();
        for (float Y = LogWindow->Pos.y + 36.0f; Y < LogWindow->Pos.y + 90.0f && PauseY < 0; Y += 4.0f)
        {
            for (float X = LogWindow->Pos.x + LogWindow->Size.x - 170.0f;
                 X < LogWindow->Pos.x + LogWindow->Size.x - 8.0f && PauseY < 0; X += 6.0f)
            {
                Click(X, Y);
                if (LogPanel.QueryPaused())
                {
                    PauseX = X;
                    PauseY = Y;
                    std::printf("learned log pause at (%.0f, %.0f)\n", static_cast<double>(X), static_cast<double>(Y));
                    Click(X, Y);   // Resume for the recorded take
                }
            }
        }
    }
    Expect(PauseY > 0, "log stream pause button learned");

    // Segment 1: Outliner + Inspector ---------------------------------------------------------------
    Outliner.ClearSelection();
    Settle(6);
    Segment = "outliner-inspector";
    SegmentFrame = 0;
    SinceSample = 2;
    Recording = true;
    MoveMouse(300.0f, 300.0f);
    Settle(24);
    Click(OutlinerWindow->Pos.x + 130.0f, FrontLeftY);
    Settle(48);
    Step(1.0 / 60.0);
    Still("still-inspector-front-left");
    Click(OutlinerWindow->Pos.x + 130.0f, VehicleY);
    Settle(48);
    if (ChevronY > 0)
    {
        // Fold the vehicle subtree by its chevron, prove the children leave the rows, unfold again.
        Click(OutlinerWindow->Pos.x + 32.0f, ChevronY);
        Settle(36);
        Expect(!RowShown("Front Left"), "chevron collapse hid the vehicle children");
        Step(1.0 / 60.0);
        Still("still-outliner-collapsed");
        Click(OutlinerWindow->Pos.x + 32.0f, ChevronY);
        Settle(36);
        Expect(RowShown("Front Left"), "chevron expand restored the vehicle children");
    }
    if (FilterY > 0)
    {
        Click(FilterX, FilterY);
        TypeText("tyre");
        Settle(42);
        // Clear the filter again (Ctrl+A, Backspace on the ImGui side).
        Io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
        Io.AddKeyEvent(ImGuiKey_A, true);
        Step(1.0 / 60.0);
        Io.AddKeyEvent(ImGuiKey_A, false);
        Io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
        Io.AddKeyEvent(ImGuiKey_Backspace, true);
        Step(1.0 / 60.0);
        Io.AddKeyEvent(ImGuiKey_Backspace, false);
        Step(1.0 / 60.0);
        KeyTap(InputKeyCategory::Escape);
        Settle(24);
    }
    if (AddY > 0 && SpawnItemY > 0)
    {
        const size_t Before = Workspace.QueryFixture().QueryRecordCount();
        Click(AddX, AddY);
        Settle(30);
        Click(SpawnItemX, SpawnItemY);
        Settle(36);
        Expect(Workspace.QueryFixture().QueryRecordCount() == Before + 1, "recorded spawn created a record");
    }
    Recording = false;
    std::printf("segment %s: %d frames\n", Segment.c_str(), SegmentFrame);

    // Segment 2: docking + input panel ----------------------------------------------------------------
    Segment = "docking-input";
    SegmentFrame = 0;
    SinceSample = 2;
    Recording = true;
    Expect(Workspace.QueryPanels().QueryOpen("Input"), "Input panel open at segment start");
    KeyTap(InputKeyCategory::F6);   // Editor.ToggleInputPanel through the InputMap
    Settle(36);
    Expect(!Workspace.QueryPanels().QueryOpen("Input"), "F6 closed the Input panel through the input map");
    KeyTap(InputKeyCategory::F6);
    Settle(48);
    ImGuiWindow* InputWindow = FindPanel("Input###FrontierPanel-Input");
    Expect(InputWindow != nullptr && Workspace.QueryPanels().QueryOpen("Input"),
           "F6 reopened the Input panel through the input map");
    Step(1.0 / 60.0);
    Still("still-input-panel");
    if (InputWindow != nullptr)
    {
        // Learn the Reassign button for Editor.ToggleLogStream (3rd chord row), cancelling with Esc.
        InputPanel& Panel = Workspace.QueryInputPanel();
        float       ReassignY = -1.0f;
        int         ChordRowsSeen = 0;
        for (float Y = InputWindow->Pos.y + 200.0f;
             Y < InputWindow->Pos.y + InputWindow->Size.y - 20.0f && ReassignY < 0; Y += 3.0f)
        {
            Click(InputWindow->Pos.x + InputWindow->Size.x - 40.0f, Y);
            if (Panel.QueryCapturing())
            {
                ++ChordRowsSeen;
                KeyTap(InputKeyCategory::Escape);   // Cancel; substrate Esc reaches the panel capture
                if (ChordRowsSeen == 3)
                {
                    ReassignY = Y;
                }
                Y += 12.0f;   // Step past this row before sweeping on
            }
        }
        Expect(ReassignY > 0, "reassign button for Editor.ToggleLogStream learned");
        if (ReassignY > 0)
        {
            Click(InputWindow->Pos.x + InputWindow->Size.x - 40.0f, ReassignY);
            Settle(24);
            KeyTap(InputKeyCategory::L, InputModifiers{true, false, false, false});
            Settle(42);
            bool Reassigned = false;
            for (const KeyAssignment& Assignment : Workspace.QueryInputMap().QueryAssignments())
            {
                if (Assignment.Action == "Editor.ToggleLogStream" && Assignment.Chord.Key == InputKeyCategory::L &&
                    Assignment.Chord.Control)
                {
                    Reassigned = true;
                }
            }
            Expect(Reassigned, "press-to-assign rebound Editor.ToggleLogStream to Ctrl+L");
            Step(1.0 / 60.0);
            Still("still-input-reassigned");
            KeyTap(InputKeyCategory::L, InputModifiers{true, false, false, false});   // Close Log Stream
            Settle(42);
            Expect(!Workspace.QueryPanels().QueryOpen("LogStream"), "Ctrl+L closed the Log Stream panel");
            KeyTap(InputKeyCategory::L, InputModifiers{true, false, false, false});   // Reopen
            Settle(42);
            Expect(Workspace.QueryPanels().QueryOpen("LogStream"), "Ctrl+L reopened the Log Stream panel");
        }
    }
    // Drag the Outliner|Viewport splitter.
    {
        ImGuiWindow* Current = FindPanel("Outliner###FrontierPanel-Outliner");
        if (Current != nullptr)
        {
            const float SplitX = Current->Pos.x + Current->Size.x;
            const float SplitY = Current->Pos.y + Current->Size.y * 0.5f;
            MoveMouse(SplitX, SplitY);
            MouseDown();
            DragMouse(SplitX + 90.0f, SplitY, 36);
            MouseUp();
            Settle(30);
            MoveMouse(SplitX + 90.0f, SplitY);
            MouseDown();
            DragMouse(SplitX + 20.0f, SplitY, 28);
            MouseUp();
            Settle(24);
        }
    }
    if (PauseY > 0)
    {
        ImGuiWindow* CurrentLog = FindPanel("Log Stream###FrontierPanel-LogStream");
        (void)CurrentLog;
        Click(PauseX, PauseY);
        Settle(48);
        Expect(Workspace.QueryLogPanel().QueryPaused(), "recorded pause held the stream");
        Click(PauseX, PauseY);
        Settle(36);
    }
    Recording = false;
    std::printf("segment %s: %d frames\n", Segment.c_str(), SegmentFrame);

    // Segment 3: notch + simulation capture -----------------------------------------------------------
    Segment = "notch-simulation";
    SegmentFrame = 0;
    SinceSample = 2;
    Recording = true;
    Settle(18);
    {
        const PlaneExtent Tab  = Centre.QueryExpansionTab().QueryTabExtent();
        const PlaneExtent Grip = Centre.QueryExpansionTab().QueryGripExtent();
        const float       TabX = (Tab.MinimumX + Tab.MaximumX) * 0.5f;
        const float       TabY = (Tab.MinimumY + Tab.MaximumY) * 0.5f;
        std::printf("notch tab centre (%.0f, %.0f); grip centre (%.0f, %.0f)\n", static_cast<double>(TabX),
                    static_cast<double>(TabY),
                    static_cast<double>((Grip.MinimumX + Grip.MaximumX) * 0.5f),
                    static_cast<double>((Grip.MinimumY + Grip.MaximumY) * 0.5f));
        const auto WaitOpen = [&](int Frames) {
            for (int Wait = 0; Wait < Frames && !Centre.QueryExpansionTab().QueryOpen(); ++Wait)
            {
                Step(1.0 / 60.0);
            }
            return Centre.QueryExpansionTab().QueryOpen();
        };
        const auto TryPull = [&](float StartY) {
            MoveMouse(TabX, StartY);
            MouseDown();
            Settle(3);   // Let contact resolve before travel starts
            for (int PullStep = 1; PullStep <= 30; ++PullStep)
            {
                MoveMouse(TabX, StartY + 10.0f * static_cast<float>(PullStep));
            }
            Settle(3);
            MouseUp();
            return WaitOpen(60);
        };
        const auto DismissCentre = [&]() {
            const PlaneExtent OpenGrip = Centre.QueryExpansionTab().QueryGripExtent();
            const float       OpenX    = (OpenGrip.MinimumX + OpenGrip.MaximumX) * 0.5f;
            const float       OpenY    = (OpenGrip.MinimumY + OpenGrip.MaximumY) * 0.5f;
            MoveMouse(OpenX, OpenY);
            MouseDown();
            Settle(3);
            DragMouse(OpenX, std::max(OpenY - 340.0f, 10.0f), 22);
            MouseUp();
            Settle(60);
            if (Centre.QueryExpansionTab().QueryOpen())
            {
                KeyTap(InputKeyCategory::Escape);
                Settle(45);
            }
        };
        // Learn which start offset catches the tab (unrecorded), dismiss, then replay cleanly on record.
        Recording               = false;
        float NotchY            = -1.0f;
        bool  NotchOpensOnClick = false;
        const float ClickY = std::clamp(TabY, 2.0f, 40.0f);
        Click(TabX, ClickY);
        if (WaitOpen(50))
        {
            NotchY            = ClickY;
            NotchOpensOnClick = true;
        }
        if (NotchY < 0)
        {
            const float Candidates[] = {TabY, 4.0f, 8.0f, 12.0f, 18.0f, 26.0f};
            for (const float Candidate : Candidates)
            {
                if (Candidate < 0.0f || Candidate > 60.0f)
                {
                    continue;
                }
                if (TryPull(Candidate))
                {
                    NotchY = Candidate;
                    break;
                }
                Settle(10);
            }
        }
        std::printf("notch learned: y=%.0f click=%s\n", static_cast<double>(NotchY),
                    NotchOpensOnClick ? "yes" : "no");
        if (Centre.QueryExpansionTab().QueryOpen())
        {
            DismissCentre();
        }
        Recording = true;
        Settle(12);
        if (NotchY >= 0 && !Centre.QueryExpansionTab().QueryOpen())
        {
            if (NotchOpensOnClick)
            {
                Click(TabX, NotchY);
                (void)WaitOpen(60);
            }
            else
            {
                (void)TryPull(NotchY);
            }
        }
        Settle(40);
        Expect(Centre.QueryExpansionTab().QueryOpen(), "notch pull opened the Control Centre");
        Step(1.0 / 60.0);
        Still("still-control-centre");
        const PlaneExtent Tile = Centre.QueryQuickTileExtent(0);
        if (Tile.MaximumX > Tile.MinimumX)
        {
            Click((Tile.MinimumX + Tile.MaximumX) * 0.5f, (Tile.MinimumY + Tile.MaximumY) * 0.5f);
            Settle(36);
        }
        DismissCentre();
        std::printf("control centre open after dismissal gesture: %s\n",
                    Centre.QueryExpansionTab().QueryOpen() ? "yes" : "no");
    }
    // Capture the simulation from the canvas and drive with the keyboard.
    ViewportWindow = FindPanel("Viewport###FrontierPanel-Viewport");
    if (ViewportWindow != nullptr)
    {
        Click(ViewportWindow->Pos.x + ViewportWindow->Size.x * 0.5f, ViewportWindow->Pos.y + 160.0f);
        Settle(30);
        Expect(Workspace.QuerySimulationCaptured(), "canvas click captured the Simulation context");
        Step(1.0 / 60.0);
        Still("still-simulation-captured");
        KeyEventBoth(InputKeyCategory::W, true, {});
        Settle(50);
        Expect(Workspace.QueryInputMap().QueryAxis("Simulation.Throttle") > 0.99f, "W held throttle at full");
        KeyEventBoth(InputKeyCategory::A, true, {});
        Settle(26);
        KeyEventBoth(InputKeyCategory::A, false, {});
        KeyEventBoth(InputKeyCategory::D, true, {});
        Settle(26);
        KeyEventBoth(InputKeyCategory::D, false, {});
        KeyTap(InputKeyCategory::Space);
        Settle(20);
        KeyEventBoth(InputKeyCategory::W, false, {});
        Settle(16);
        KeyTap(InputKeyCategory::Escape);
        Settle(36);
        Expect(!Workspace.QuerySimulationCaptured(), "Escape released the Simulation context");
    }
    Recording = false;
    std::printf("segment %s: %d frames\n", Segment.c_str(), SegmentFrame);

    Step(1.0 / 60.0);
    Still("still-workspace-final");
    std::printf("rasterized %d frames in %lld ms (avg %lld ms)\n", RasterCount, RasterMillis,
                RasterCount > 0 ? RasterMillis / RasterCount : 0);
    std::printf("evidence complete: %d failures\n", Failures);

    Workspace.Finalize();
    ImGui::DestroyContext();
    return Failures == 0 ? 0 : 1;
}
