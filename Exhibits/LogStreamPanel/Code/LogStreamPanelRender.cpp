/*====================================================================================================================================
                                                   LOGSTREAMPANELRENDER.CPP
====================================================================================================================================*/
// 🧩 Actual rendered evidence for the native Log Stream panel: real ImGui context + the production
//    LogStreamPanel, CPU-rasterized from ImDrawData (font-atlas sampled triangles) into a PPM.
//    No mockup: every pixel comes from the panel's own ImGui draw lists. Output PPM path = argv[1].

#include "Engine/Interface/LogStreamChannel.h"
#include "UI/Editor/LogStreamPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Frontier;

struct Colour
{
    uint8_t R, G, B, A;
};

int main(int ArgumentCount, char **Arguments)
{
    const char *OutputPath = ArgumentCount > 1 ? Arguments[1] : "log-stream-panel-render.ppm";
    constexpr int Width = 1160, Height = 680;

    LogStreamChannel Stream;
    Stream.Emit(LogLevel::Info, "Startup", 0, "sequence complete: platform, Vulkan device, Control Centre, Log Stream panel");
    Stream.Emit(LogLevel::Info, "RenderGraph", 118671, "frame graph compiled: 14 units, 3 culled");
    Stream.Emit(LogLevel::Warning, "Streaming", 118671, "page queue pressure: coarser tiers substituted");
    Stream.Emit(LogLevel::Debug, "GeometryKernel", 118672, "cut selected 312 clusters, projected error under 1.0 px");
    Stream.Emit(LogLevel::Error, "BVH", 118672, "non-finite transform skipped, occupant flagged");
    Stream.Emit(LogLevel::Info, "ReSTIR", 118673, "reservoir reuse temporal 92%");
    Stream.Emit(LogLevel::Trace, "RenderGraph", 118673, "transient aliasing saved 212 MB this frame");
    Stream.Emit(LogLevel::Warning, "Streaming", 118673, "2 pages queued behind the record pool");
    Stream.Emit(LogLevel::Info, "EditorUI", 118673, "log stream panel refresh within budget");
    Stream.Emit(LogLevel::Debug, "BVH", 118673, "tlas refit 412 instances, no rebuild");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &Io = ImGui::GetIO();
    Io.DisplaySize = ImVec2(float(Width), float(Height));
    Io.DeltaTime = 1.0f / 60.0f;
    Io.IniFilename = nullptr;
    unsigned char *AtlasPixels = nullptr;
    int AtlasWidth = 0, AtlasHeight = 0;
    Io.Fonts->GetTexDataAsRGBA32(&AtlasPixels, &AtlasWidth, &AtlasHeight);
    Io.Fonts->TexID = (ImTextureID)(intptr_t)1; // sentinel: the only texture in this scene is the font atlas
    ImGui::StyleColorsDark();
    ImGuiStyle &Style = ImGui::GetStyle();
    Style.WindowRounding = 14.0f;
    Style.ChildRounding = 10.0f;
    Style.FrameRounding = 7.0f;
    Style.WindowBorderSize = 0.0f;
    Style.Colors[ImGuiCol_WindowBg] = ImVec4(0.078f, 0.078f, 0.086f, 1.0f);
    Style.Colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.0f);

    LogStreamPanel Panel(Stream);
    if (!Panel.Initialize() || !Panel.Activate()) { std::printf("RESULT: FAIL panel activation\n"); return 1; }

    ImDrawData *Draw = nullptr;
    for (int Frame = 0; Frame < 6; ++Frame)
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(14, 14), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(Width - 28, Height - 28), ImGuiCond_Always);
        Panel.RecordInterface();
        ImGui::Render();
        Draw = ImGui::GetDrawData();
    }
    if (!Draw || Draw->TotalVtxCount == 0) { std::printf("RESULT: FAIL no draw data\n"); return 1; }

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
                        const uint32_t Packed = uint32_t(W0 * ((V[0]->col >> 0) & 0xFF)) << 0; // placeholder, replaced below
                        (void)Packed;
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
    std::printf("rendered %dx%d, vertices=%d indices=%d -> %s\nRESULT: PASS\n", Width, Height,
                Draw->TotalVtxCount, Draw->TotalIdxCount, OutputPath);
    ImGui::DestroyContext();
    return 0;
}
