/*====================================================================================================================================
                                                   FRONTIERWORKSPACERENDER.CPP
====================================================================================================================================*/
// 🧩 Off-screen evidence from the production Frontier UI stack. This invokes the copied EditorWorkspace,
//    the copied ControlCentrePanel (its actual notch), PanelDockspace, and the production LogStreamPanel.
//    Only presentation is headless: the real ImGui draw lists are CPU-rasterized to a PPM.

#include "Engine/Interface/LogStreamChannel.h"
#include "UI/Editor/ControlCentrePanel.h"
#include "UI/Editor/EditorWorkspace.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Frontier;

struct Colour
{
    uint8_t R, G, B, A;
};

int main(int ArgumentCount, char **Arguments)
{
    const bool OpenCentre = ArgumentCount > 2 && std::strcmp(Arguments[2], "--open") == 0;
    const char *OutputPath = ArgumentCount > 1 ? Arguments[1] :
        (OpenCentre ? "frontier-workspace-notch-open.ppm" : "frontier-workspace-with-notch.ppm");
    constexpr int Width = 1600, Height = 1000;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &Io = ImGui::GetIO();
    Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    Io.DisplaySize = ImVec2(float(Width), float(Height));
    Io.DisplayFramebufferScale = ImVec2(1, 1);
    Io.DeltaTime = 1.0f / 60.0f;
    Io.IniFilename = nullptr;

    // Match the app's real project-mode Control Centre initialization, using the shipped blank template.
    ActivationOwnership AppearanceOwnership(new ControlCentrePanel(
        "EngineContent/FontArchives", {}, "EngineContent/Defaults/BlankProject/Blank.projectspace"));
    auto *Appearance = static_cast<ControlCentrePanel *>(AppearanceOwnership.get());
    Appearance->ConfigureContentBrowserVisibility(false); // same host setting as TextureWorkspace
    if (!Appearance->Initialize() || !Appearance->Activate())
    {
        std::printf("RESULT: FAIL Control Centre activation: %s\n", Appearance->QueryDiagnostic().c_str());
        ImGui::DestroyContext();
        return 1;
    }

    // TextureWorkspace's host calls PrepareInterface before NewFrame, and the project shell constructs
    // EditorWorkspace before the notch. Keep exactly that ordering here.
    Appearance->PrepareInterface();
    std::unique_ptr<EditorWorkspace> Workspace;
    try
    {
        Workspace = std::make_unique<EditorWorkspace>(*Appearance);
        EditorProjectSummary Project;
        Project.Name = "Blank Project";
        Workspace->ConfigureProject(std::move(Project));
    }
    catch (const std::exception &Failure)
    {
        std::printf("RESULT: FAIL EditorWorkspace construction: %s\n", Failure.what());
        AppearanceOwnership.reset();
        ImGui::DestroyContext();
        return 1;
    }

    if (OpenCentre)
        Appearance->ActivateSettingsPage(ControlCentrePageCategory::QuickControls);

    const auto Add = [&](LogLevel Level, const char *Division, uint64_t Frame, const char *Message)
    {
        Workspace->QueryStream().Emit(Level, Division, Frame, Message);
    };
    Add(LogLevel::Info, "Startup", 0, "project window initialized; Control Centre and Log Stream ready");
    Add(LogLevel::Info, "RenderGraph", 118671, "frame graph compiled: 14 units, 3 culled");
    Add(LogLevel::Warning, "Streaming", 118671, "page queue pressure: coarser tiers substituted");
    Add(LogLevel::Debug, "GeometryKernel", 118672, "cut selected 312 clusters, projected error under 1.0 px");
    Add(LogLevel::Error, "BVH", 118672, "non-finite transform skipped, occupant flagged");
    Add(LogLevel::Info, "ReSTIR", 118673, "reservoir reuse temporal 92%");
    Add(LogLevel::Trace, "RenderGraph", 118673, "transient aliasing saved 212 MB this frame");
    Add(LogLevel::Warning, "Streaming", 118673, "2 pages queued behind the record pool");
    Add(LogLevel::Info, "EditorUI", 118673, "log stream panel refresh within budget");
    Add(LogLevel::Debug, "BVH", 118673, "tlas refit 412 instances, no rebuild");

    // The running app calls ConfigurePlatform each frame before NewFrame.
    if (!Workspace->ConfigurePlatform())
    {
        std::printf("RESULT: FAIL PanelDockspace platform configuration\n");
        Workspace.reset();
        AppearanceOwnership.reset();
        ImGui::DestroyContext();
        return 1;
    }

    unsigned char *AtlasPixels = nullptr;
    int AtlasWidth = 0, AtlasHeight = 0;
    Io.Fonts->GetTexDataAsRGBA32(&AtlasPixels, &AtlasWidth, &AtlasHeight);
    Io.Fonts->SetTexID((ImTextureID)(intptr_t)1); // CPU rasterizer sentinel for the actual ImGui font atlas.

    ImDrawData *Draw = nullptr;
    const int FrameCount = OpenCentre ? 180 : 8; // settle the real Control Centre notch animation for the open-state evidence
    for (int Frame = 0; Frame < FrameCount; ++Frame)
    {
        Io.DeltaTime = 1.0f / 60.0f;
        Appearance->PrepareInterface();
        ImGui::NewFrame();
        (void)Appearance->Integrate(Io.DeltaTime);
        Workspace->ConstructInterface();
        Appearance->ConstructInterface(); // exact ControlCentre panel, including the real Frontier notch
        ImGui::Render();
        Draw = ImGui::GetDrawData();
    }
    if (!Draw || Draw->TotalVtxCount == 0)
    {
        std::printf("RESULT: FAIL empty ImGui draw data\n");
        Workspace.reset();
        AppearanceOwnership.reset();
        ImGui::DestroyContext();
        return 1;
    }

    std::vector<Colour> Image(size_t(Width * Height), Colour{ 10, 10, 12, 255 });
    const auto SampleAtlas = [&](float U, float V) -> Colour
    {
        const int X = std::clamp(int(U * float(AtlasWidth)), 0, AtlasWidth - 1);
        const int Y = std::clamp(int(V * float(AtlasHeight)), 0, AtlasHeight - 1);
        const unsigned char *Pixel = AtlasPixels + size_t(Y * AtlasWidth + X) * 4;
        return Colour{ Pixel[0], Pixel[1], Pixel[2], Pixel[3] };
    };
    const auto Blend = [&](int X, int Y, Colour Source)
    {
        if (X < 0 || Y < 0 || X >= Width || Y >= Height || Source.A == 0) return;
        Colour &Target = Image[size_t(Y * Width + X)];
        const float A = Source.A / 255.0f;
        Target.R = uint8_t(Source.R * A + Target.R * (1 - A));
        Target.G = uint8_t(Source.G * A + Target.G * (1 - A));
        Target.B = uint8_t(Source.B * A + Target.B * (1 - A));
    };

    for (int ListIndex = 0; ListIndex < Draw->CmdListsCount; ++ListIndex)
    {
        const ImDrawList *List = Draw->CmdLists[ListIndex];
        for (int CommandIndex = 0; CommandIndex < List->CmdBuffer.Size; ++CommandIndex)
        {
            const ImDrawCmd &Command = List->CmdBuffer[CommandIndex];
            if (Command.UserCallback) continue;
            const int ClipX0 = std::max(0, int(Command.ClipRect.x)), ClipY0 = std::max(0, int(Command.ClipRect.y));
            const int ClipX1 = std::min(Width, int(Command.ClipRect.z)), ClipY1 = std::min(Height, int(Command.ClipRect.w));
            for (unsigned int Element = Command.IdxOffset; Element + 2 < Command.IdxOffset + Command.ElemCount; Element += 3)
            {
                const ImDrawVert *V[3];
                for (int Corner = 0; Corner < 3; ++Corner)
                    V[Corner] = &List->VtxBuffer[List->IdxBuffer[Element + Corner]];
                const float Denominator = (V[1]->pos.y - V[2]->pos.y) * (V[0]->pos.x - V[2]->pos.x) +
                                          (V[2]->pos.x - V[1]->pos.x) * (V[0]->pos.y - V[2]->pos.y);
                if (std::fabs(Denominator) < 1e-9f) continue;
                const int MinX = std::max(ClipX0, int(std::floor(std::min({ V[0]->pos.x, V[1]->pos.x, V[2]->pos.x }))));
                const int MaxX = std::min(ClipX1 - 1, int(std::ceil(std::max({ V[0]->pos.x, V[1]->pos.x, V[2]->pos.x }))));
                const int MinY = std::max(ClipY0, int(std::floor(std::min({ V[0]->pos.y, V[1]->pos.y, V[2]->pos.y }))));
                const int MaxY = std::min(ClipY1 - 1, int(std::ceil(std::max({ V[0]->pos.y, V[1]->pos.y, V[2]->pos.y }))));
                for (int Y = MinY; Y <= MaxY; ++Y)
                    for (int X = MinX; X <= MaxX; ++X)
                    {
                        const float PX = X + 0.5f, PY = Y + 0.5f;
                        const float W0 = ((V[1]->pos.y - V[2]->pos.y) * (PX - V[2]->pos.x) +
                                          (V[2]->pos.x - V[1]->pos.x) * (PY - V[2]->pos.y)) / Denominator;
                        const float W1 = ((V[2]->pos.y - V[0]->pos.y) * (PX - V[2]->pos.x) +
                                          (V[0]->pos.x - V[2]->pos.x) * (PY - V[2]->pos.y)) / Denominator;
                        const float W2 = 1 - W0 - W1;
                        if (W0 < 0 || W1 < 0 || W2 < 0) continue;
                        const float U = W0 * V[0]->uv.x + W1 * V[1]->uv.x + W2 * V[2]->uv.x;
                        const float VCoord = W0 * V[0]->uv.y + W1 * V[1]->uv.y + W2 * V[2]->uv.y;
                        const auto Mix = [&](int Shift) {
                            return uint8_t(std::clamp(W0 * ((V[0]->col >> Shift) & 0xFF) + W1 * ((V[1]->col >> Shift) & 0xFF) +
                                                      W2 * ((V[2]->col >> Shift) & 0xFF), 0.0f, 255.0f));
                        };
                        Colour Vertex{ Mix(0), Mix(8), Mix(16), Mix(24) };
                        Colour Source = Vertex;
                        if (Command.GetTexID() == (ImTextureID)(intptr_t)1)
                        {
                            const Colour Texel = SampleAtlas(U, VCoord);
                            Source.R = uint8_t(Vertex.R * Texel.R / 255);
                            Source.G = uint8_t(Vertex.G * Texel.G / 255);
                            Source.B = uint8_t(Vertex.B * Texel.B / 255);
                            Source.A = uint8_t(Vertex.A * Texel.A / 255);
                        }
                        Blend(X, Y, Source);
                    }
            }
        }
    }

    std::FILE *File = std::fopen(OutputPath, "wb");
    if (!File) { std::printf("RESULT: FAIL cannot open %s\n", OutputPath); return 1; }
    std::fprintf(File, "P6\n%d %d\n255\n", Width, Height);
    for (const Colour &Pixel : Image)
    {
        const uint8_t Bytes[3] = { Pixel.R, Pixel.G, Pixel.B };
        std::fwrite(Bytes, 1, 3, File);
    }
    std::fclose(File);
    std::printf("rendered production EditorWorkspace + ControlCentre %s notch + Log Stream: %dx%d, vertices=%d indices=%d -> %s\nRESULT: PASS\n",
                OpenCentre ? "open" : "closed", Width, Height, Draw->TotalVtxCount, Draw->TotalIdxCount, OutputPath);
    Workspace.reset();
    AppearanceOwnership.reset();
    ImGui::DestroyContext();
    return 0;
}
