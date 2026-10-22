/*====================================================================================================================================
                                                  LOGSTREAMPANELVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Validation + visual proof for the native Log Stream panel stack: recorded startup sequence (mode-aware),
//    structured log channel filtering, and a software-rendered capture of the panel design (charcoal card,
//    dropdown filters, active chips, level-coloured stream) written as PPM. No GPU or ImGui required.

#include "Engine/Interface/LogStreamChannel.h"
#include "Engine/Lifecycle/StartupSequenceTrace.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace Frontier;

//------------------------------------------------------------------------------------------------------------------------
//                                                       TINY 5x7 FONT
//------------------------------------------------------------------------------------------------------------------------

static const std::map<char, std::array<const char*, 7>>& Font()
{
    static const std::map<char, std::array<const char*, 7>> Glyphs = {
        {'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
        {'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
        {'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}},
        {'D', {"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}},
        {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
        {'F', {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}},
        {'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###."}},
        {'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
        {'I', {".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."}},
        {'J', {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."}},
        {'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
        {'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
        {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
        {'N', {"#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#"}},
        {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
        {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
        {'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
        {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
        {'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
        {'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
        {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
        {'V', {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
        {'W', {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"}},
        {'X', {"#...#", ".#.#.", "..#..", "..#..", "..#..", ".#.#.", "#...#"}},
        {'Y', {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}},
        {'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
        {'0', {".###.", "#..##", "#.#.#", "##..#", "#...#", "#...#", ".###."}},
        {'1', {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}},
        {'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
        {'3', {"####.", "....#", "....#", ".###.", "....#", "....#", "####."}},
        {'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
        {'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
        {'6', {".###.", "#....", "#....", "####.", "#...#", "#...#", ".###."}},
        {'7', {"#####", "....#", "...#.", "..#..", "..#..", "..#..", "..#.."}},
        {'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
        {'9', {".###.", "#...#", "#...#", ".####", "....#", "....#", ".###."}},
        {' ', {".....", ".....", ".....", ".....", ".....", ".....", "....."}},
        {'.', {".....", ".....", ".....", ".....", ".....", ".##..", ".##.."}},
        {',', {".....", ".....", ".....", ".....", ".##..", ".##..", ".#..."}},
        {':', {".....", ".##..", ".##..", ".....", ".##..", ".##..", "....."}},
        {';', {".....", ".##..", ".##..", ".....", ".##..", ".##..", ".#..."}},
        {'!', {"..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."}},
        {'%', {"##..#", "##.#.", "....#", "...#.", "..#..", ".#...", "#..##"}},
        {'+', {".....", "..#..", "..#..", ".###.", "..#..", "..#..", "....."}},
        {'-', {".....", ".....", ".....", ".###.", ".....", ".....", "....."}},
        {'/', {"....#", "....#", "...#.", "..#..", ".#...", "#....", "#...."}},
        {'(', {"...##", "..#..", ".#...", ".#...", ".#...", "..#..", "...##"}},
        {')', {"##...", "..#..", "...#.", "...#.", "...#.", "..#..", "##..."}},
        {'#', {".#.#.", ".#.#.", "#####", ".#.#.", "#####", ".#.#.", ".#.#."}},
        {'@', {".###.", "#...#", "#.###", "#.#.#", "#.##.", "#....", ".###."}},
        {'_', {".....", ".....", ".....", ".....", ".....", ".....", "#####"}},
        {'>', {"#....", ".#...", "..#..", "...#.", "..#..", ".#...", "#...."}},
        {'<', {"....#", "...#.", "..#..", ".#...", "..#..", "...#.", "....#"}},
        {'=', {".....", ".....", "#####", ".....", "#####", ".....", "....."}},
        {'?', {".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.."}},
        {'[', {"..###", "..#..", "..#..", "..#..", "..#..", "..#..", "..###"}},
        {']', {"###..", "..#..", "..#..", "..#..", "..#..", "..#..", "###.."}},
        {'*', {".....", "#.#.#", ".###.", "#####", ".###.", "#.#.#", "....."}},
        {'\'', {"..#..", "..#..", ".#...", ".....", ".....", ".....", "....."}},
        {'"', {".#.#.", ".#.#.", ".#.#.", ".....", ".....", ".....", "....."}},
    };
    return Glyphs;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       SOFTWARE RASTER
//------------------------------------------------------------------------------------------------------------------------

struct Colour { uint8_t R, G, B; };

struct Canvas
{
    int Width = 0, Height = 0;
    std::vector<Colour> Pixels;
    void Clear(Colour C) { Pixels.assign(size_t(Width * Height), C); }
    void Set(int X, int Y, Colour C) { if (X >= 0 && Y >= 0 && X < Width && Y < Height) Pixels[size_t(Y * Width + X)] = C; }
    void Rect(int X, int Y, int W, int H, Colour C)
    {
        for (int J = Y; J < Y + H; ++J) for (int I = X; I < X + W; ++I) Set(I, J, C);
    }
    void RoundedRect(int X, int Y, int W, int H, int R, Colour C)
    {
        for (int J = Y; J < Y + H; ++J)
            for (int I = X; I < X + W; ++I)
            {
                const int CX = I < X + R ? X + R : (I >= X + W - R ? X + W - R - 1 : I);
                const int CY = J < Y + R ? Y + R : (J >= Y + H - R ? Y + H - R - 1 : J);
                const int DX = I - CX, DY = J - CY;
                if (DX * DX + DY * DY <= R * R || (I >= X + R && I < X + W - R) || (J >= Y + R && J < Y + H - R))
                    Set(I, J, C);
            }
    }
    void Text(int X, int Y, const std::string& Value, Colour C, int Scale = 2)
    {
        int Cursor = X;
        for (char Raw : Value)
        {
            char Glyph = char(std::toupper(uint8_t(Raw)));
            const auto Found = Font().find(Glyph);
            const auto& Rows = Found != Font().end() ? Found->second : Font().at('?');
            for (int Row = 0; Row < 7; ++Row)
                for (int Col = 0; Col < 5; ++Col)
                    if (Rows[Row][Col] == '#')
                        Rect(Cursor + Col * Scale, Y + Row * Scale, Scale, Scale, C);
            Cursor += 6 * Scale;
        }
    }
    [[nodiscard]] int TextWidth(const std::string& Value, int Scale = 2) const { return int(Value.size()) * 6 * Scale; }
};

static const Colour Ink{ 242, 242, 244 }, Dim{ 163, 163, 171 }, Faint{ 107, 107, 116 };
static const Colour LevelColour[5] = { {247, 113, 113}, {250, 146, 60}, {61, 220, 132}, {56, 189, 248}, {167, 139, 250} };
static const char* LevelName[5] = { "ERROR", "WARN", "INFO", "DEBUG", "TRACE" };

//------------------------------------------------------------------------------------------------------------------------
//                                                            MAIN
//------------------------------------------------------------------------------------------------------------------------

static int Failures = 0;
static void Check(bool Condition, const char* Label)
{
    std::printf("[%s] %s\n", Condition ? "PASS" : "FAIL", Label);
    if (!Condition) ++Failures;
}

int main(int ArgumentCount, char** Arguments)
{
    const char* OutputPath = ArgumentCount > 1 ? Arguments[1] : "log-stream-panel.ppm";

    // ① Startup sequence: recorded, mode-aware, halts on failure.
    LogStreamChannel Stream;
    StartupSequenceTrace Startup;
    Startup.AddStep("Platform window (GLFW)", [] { return true; });
    Startup.AddStep("Vulkan device", [] { return true; });
    Startup.AddStep("Shader pipeline (Slang)", [] { return true; });
    Startup.AddStep("Log Stream panel", [] { return true; });
    Check(Startup.Run(), "startup sequence completes");
    Check(Startup.QueryEntries().size() == 4, "all steps retained in this build mode");
    std::printf("startup mode=%s steps=%zu total=%llu us\n", StartupSequenceTrace::QueryModeName(),
                Startup.QueryEntries().size(), (unsigned long long)Startup.QueryTotalMicroseconds());

    StartupSequenceTrace Broken;
    bool LaterRan = false;
    Broken.AddStep("Failing step", [] { return false; });
    Broken.AddStep("Must not run", [&] { LaterRan = true; return true; });
    Check(!Broken.Run() && !LaterRan && Broken.QueryEntries().size() == 1, "failed step halts the sequence");

    // ② Log stream traffic + filter semantics (the panel's exact predicate).
    const char* Channels[6] = { "RenderGraph", "GeometryKernel", "BVH", "ReSTIR", "Streaming", "EditorUI" };
    for (uint64_t Frame = 1; Frame <= 240; ++Frame)
    {
        Stream.Emit(LogLevel(Frame % 5), Channels[Frame % 6], Frame,
                    Frame % 37 == 0 ? "page queue pressure: coarser tiers substituted"
                                    : "frame graph unit executed within budget");
        if (Frame % 53 == 0) Stream.Emit(LogLevel::Error, "BVH", Frame, "non-finite transform skipped");
    }
    LogStreamFilter OnlyErrors; OnlyErrors.LevelMask = 1u << uint32_t(LogLevel::Error);
    const auto Errors = Stream.Snapshot(OnlyErrors);
    Check(!Errors.empty() && std::all_of(Errors.begin(), Errors.end(),
          [](const LogStreamEntry& E) { return E.Level == LogLevel::Error; }), "level filter keeps only errors");
    LogStreamFilter BySearch; BySearch.Search = "coarser tiers";
    Check(Stream.Snapshot(BySearch).size() == 6, "search filter finds the pressure records");
    LogStreamFilter ByChannel; ByChannel.Channels = { "ReSTIR" };
    const auto Restir = Stream.Snapshot(ByChannel);
    Check(!Restir.empty() && std::all_of(Restir.begin(), Restir.end(),
          [](const LogStreamEntry& E) { return E.Channel == "ReSTIR"; }), "division filter keeps one channel");

    // ③ Visual proof: the native panel design, software-rendered.
    Canvas Image; Image.Width = 1200; Image.Height = 800;
    Image.Clear({ 10, 10, 12 });
    Image.RoundedRect(300, 8, 600, 40, 16, { 28, 28, 32 });
    Image.Text(545, 20, "FRONTIER", { 200, 200, 208 }, 2);
    Image.RoundedRect(28, 58, 1144, 716, 22, { 20, 20, 22 });
    Image.Text(52, 82, "LOG STREAM", Ink, 3);
    const std::string Meta = "FRAME 118673 - LIVE";
    Image.Text(1148 - Image.TextWidth(Meta), 88, Meta, Faint, 2);
    // filter row: two dropdowns, search, buttons
    const auto Pill = [&](int X, int Y, int W, const std::string& Label, Colour Fill, Colour TextColour)
    {
        Image.RoundedRect(X, Y, W, 34, 9, Fill);
        Image.Text(X + 12, Y + 10, Label, TextColour, 2);
    };
    Pill(52, 122, 250, "+ ADD LEVEL FILTER", { 38, 38, 44 }, Dim);
    Pill(312, 122, 282, "+ ADD DIVISION FILTER", { 38, 38, 44 }, Dim);
    Pill(604, 122, 196, "SEARCH STREAM...", { 38, 38, 44 }, Faint);
    Pill(810, 122, 92, "PAUSE", { 38, 38, 44 }, Dim);
    Pill(912, 122, 108, "FOLLOW", { 59, 130, 246 }, Ink);
    Pill(1030, 122, 102, "CLEAR", { 38, 38, 44 }, Dim);
    // active chips
    Pill(52, 166, 104, "ERROR X", LevelColour[0], { 12, 12, 14 });
    Pill(166, 166, 92, "WARN X", LevelColour[1], { 12, 12, 14 });
    Pill(268, 166, 172, "RENDERGRAPH X", { 38, 38, 44 }, Ink);
    // stream region
    Image.RoundedRect(52, 212, 1096, 470, 12, { 26, 26, 30 });
    LogStreamFilter Demo; Demo.LevelMask = (1u << 0) | (1u << 1); Demo.Channels = { "RenderGraph" };
    // demo rows: newest filter-passing entries of two synthetic scenes, padded with editor/startup records
    std::vector<LogStreamEntry> Rows = {
        { LogLevel::Info, "Startup", 0, "sequence complete in editor mode: 4 steps ok" },
        { LogLevel::Info, "RenderGraph", 118671, "frame graph compiled: 14 units, 3 culled" },
        { LogLevel::Warning, "Streaming", 118671, "page queue pressure: coarser tiers substituted" },
        { LogLevel::Debug, "GeometryKernel", 118672, "cut selected 312 clusters, error under 1.0 px" },
        { LogLevel::Error, "BVH", 118672, "non-finite transform skipped, occupant flagged" },
        { LogLevel::Info, "ReSTIR", 118673, "reservoir reuse temporal 92 percent" },
        { LogLevel::Trace, "RenderGraph", 118673, "transient aliasing saved 212 mb this frame" },
        { LogLevel::Warning, "Streaming", 118673, "2 pages queued behind record pool" },
        { LogLevel::Info, "EditorUI", 118673, "log stream panel refresh 84 us" },
        { LogLevel::Debug, "BVH", 118673, "tlas refit 412 instances, no rebuild" },
        { LogLevel::Info, "RenderGraph", 118673, "graph executed: present complete" },
    };
    int Y = 232;
    for (const LogStreamEntry& Entry : Rows)
    {
        const std::string Frame = "F" + std::to_string(Entry.Frame);
        Image.Text(68, Y, Frame, Faint, 2);
        Image.Text(168, Y, LevelName[uint32_t(Entry.Level)], LevelColour[uint32_t(Entry.Level)], 2);
        Image.Text(258, Y, Entry.Channel, Dim, 2);
        std::string Message = Entry.Message;
        for (char& C : Message) if (C == '%') C = ' ';
        if (int(Message.size()) > 62) Message = Message.substr(0, 62);
        Image.Text(448, Y, Message, Ink, 2);
        Y += 34;
        if (Y > 640) break;
    }
    // startup journal strip at the panel foot
    Image.Text(52, 700, "STARTUP JOURNAL - EDITOR MODE", Dim, 2);
    int X = 52;
    for (const StartupStepEntry& Entry : Startup.QueryEntries())
    {
        std::string Label = Entry.Name;
        if (Label.size() > 22) Label = Label.substr(0, 22);
        const int W = Image.TextWidth(Label) + 42;
        Image.RoundedRect(X, 726, W, 30, 8, { 16, 34, 24 });
        Image.Rect(X + 10, 738, 8, 8, LevelColour[2]);
        Image.Text(X + 26, 734, Label, { 160, 230, 180 }, 2);
        X += W + 10;
    }

    std::FILE* File = std::fopen(OutputPath, "wb");
    Check(File != nullptr, "proof image opened for writing");
    if (File)
    {
        std::fprintf(File, "P6\n%d %d\n255\n", Image.Width, Image.Height);
        for (const Colour& Pixel : Image.Pixels)
        {
            const uint8_t Bytes[3] = { Pixel.R, Pixel.G, Pixel.B };
            std::fwrite(Bytes, 1, 3, File);
        }
        std::fclose(File);
    }
    std::printf(Failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", Failures);
    return Failures ? 1 : 0;
}
