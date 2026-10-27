/*============================================================================================================================================
                                                  STAMPLIBRARY.CPP
============================================================================================================================================*/

#include "UI/TextureStudio/StampLibrary.h"
#include <stb_image.h>       // declarations only; the implementation lives in FrontierSurfaceRasterization
#include <stb_image_write.h> // declarations only; the implementation is compiled into this library
#include <algorithm>
#include <cmath>
#include <fstream>

namespace Frontier
{
namespace
{
constexpr int PresetExtent = 256;
constexpr int MaxStampExtent = 2048;

std::string Slugify(const std::string &Name)
{
    std::string Slug;
    for (const char Raw : Name)
    {
        const unsigned char C = static_cast<unsigned char>(Raw);
        if ((C >= 'a' && C <= 'z') || (C >= '0' && C <= '9')) Slug += char(C);
        else if (C >= 'A' && C <= 'Z') Slug += char(C - 'A' + 'a');
        else if (!Slug.empty() && Slug.back() != '-') Slug += '-';
        if (Slug.size() >= 48) break;
    }
    while (!Slug.empty() && Slug.back() == '-') Slug.pop_back();
    return Slug.empty() ? "stamp" : Slug;
}

std::string DisplayName(const std::string &Stem)
{
    std::string Name = Stem;
    for (char &C : Name)
        if (C == '-' || C == '_') C = ' ';
    return Name.empty() ? "Stamp" : Name;
}

bool HasPngExtension(const std::filesystem::path &Path)
{
    std::string Extension = Path.extension().string();
    for (char &C : Extension) C = char(std::tolower(static_cast<unsigned char>(C)));
    return Extension == ".png";
}

std::vector<uint8_t> ReadFileBytes(const std::filesystem::path &Path, size_t Limit)
{
    std::ifstream Input(Path, std::ios::binary | std::ios::ate);
    if (!Input) return {};
    const auto Bytes = Input.tellg();
    if (Bytes <= 0 || size_t(Bytes) > Limit) return {};
    std::vector<uint8_t> Data;
    Data.resize(static_cast<size_t>(Bytes));
    Input.seekg(0);
    if (!Input.read(reinterpret_cast<char *>(Data.data()), Bytes)) return {};
    return Data;
}

// ── procedural preset coverage fields: signed distance (pixels, <=0 inside) -> 1px-AA alpha ──────
float CoverageFromDistance(float Distance) noexcept
{
    return std::clamp(0.5f - Distance, 0.f, 1.f);
}
float Length2(float X, float Y) noexcept { return std::hypot(X, Y); }

StampLibraryEntry MakePreset(const char *Slug, const char *Name, const std::vector<float> &Coverage)
{
    StampLibraryEntry Entry;
    Entry.Id = std::string("builtin:") + Slug;
    Entry.Name = Name;
    Entry.Builtin = true;
    Entry.Tintable = true;
    Entry.Width = Entry.Height = PresetExtent;
    Entry.Rgba.resize(size_t(PresetExtent) * PresetExtent * 4);
    for (size_t Pixel = 0; Pixel < Coverage.size(); ++Pixel)
    {
        uint8_t *Target = &Entry.Rgba[Pixel * 4];
        Target[0] = Target[1] = Target[2] = 255;
        Target[3] = uint8_t(std::clamp(Coverage[Pixel], 0.f, 1.f) * 255.f + 0.5f);
    }
    return Entry;
}

template <typename Field>
std::vector<float> EvaluateField(Field &&Fn)
{
    std::vector<float> Coverage(size_t(PresetExtent) * PresetExtent);
    const float Centre = (PresetExtent - 1) * 0.5f;
    for (int Y = 0; Y < PresetExtent; ++Y)
        for (int X = 0; X < PresetExtent; ++X)
            Coverage[size_t(Y) * PresetExtent + X] = Fn((X - Centre) / Centre, (Y - Centre) / Centre); // -1..1
    return Coverage;
}

bool PointInPolygon(float X, float Y, const std::vector<std::array<float, 2>> &Polygon) noexcept
{
    bool Inside = false;
    for (size_t I = 0, J = Polygon.size() - 1; I < Polygon.size(); J = I++)
    {
        const auto &A = Polygon[I], &B = Polygon[J];
        if ((A[1] > Y) != (B[1] > Y) && X < (B[0] - A[0]) * (Y - A[1]) / (B[1] - A[1]) + A[0]) Inside = !Inside;
    }
    return Inside;
}
float PolygonEdgeDistance(float X, float Y, const std::vector<std::array<float, 2>> &Polygon) noexcept
{
    float Best = 1e9f;
    for (size_t I = 0, J = Polygon.size() - 1; I < Polygon.size(); J = I++)
    {
        const auto &A = Polygon[J], &B = Polygon[I];
        const float Dx = B[0] - A[0], Dy = B[1] - A[1];
        const float LenSq = Dx * Dx + Dy * Dy;
        float T = LenSq > 0 ? ((X - A[0]) * Dx + (Y - A[1]) * Dy) / LenSq : 0.f;
        T = std::clamp(T, 0.f, 1.f);
        Best = std::min(Best, Length2(X - (A[0] + Dx * T), Y - (A[1] + Dy * T)));
    }
    return Best;
}
std::vector<float> PolygonField(const std::vector<std::array<float, 2>> &Polygon)
{
    const float PixelInField = 2.f / PresetExtent;
    return EvaluateField([&](float X, float Y) {
        const float Edge = PolygonEdgeDistance(X, Y, Polygon) / PixelInField;
        return CoverageFromDistance(PointInPolygon(X, Y, Polygon) ? -Edge : Edge);
    });
}
} // namespace

std::vector<StampLibraryEntry> StampLibrary::BuiltinPresets()
{
    std::vector<StampLibraryEntry> Presets;
    const float PixelInField = 2.f / PresetExtent;
    // Ring
    Presets.push_back(MakePreset("ring", "Ring", EvaluateField([&](float X, float Y) {
        return CoverageFromDistance((std::fabs(Length2(X, Y) - 0.68f) - 0.13f) / PixelInField);
    })));
    // Five-point star
    {
        std::vector<std::array<float, 2>> Star;
        for (int I = 0; I < 10; ++I)
        {
            const float Angle = -1.5707963f + I * 0.6283185f;
            const float Radius = (I % 2 == 0) ? 0.82f : 0.38f;
            Star.push_back({std::cos(Angle) * Radius, std::sin(Angle) * Radius});
        }
        Presets.push_back(MakePreset("star", "Star", PolygonField(Star)));
    }
    // Up arrow (head + shaft as one polygon, pointing toward -Y = image top)
    Presets.push_back(MakePreset("arrow", "Arrow", PolygonField({
        {0.f, -0.85f}, {0.45f, -0.25f}, {0.18f, -0.25f}, {0.18f, 0.8f},
        {-0.18f, 0.8f}, {-0.18f, -0.25f}, {-0.45f, -0.25f}})));
    // Cross / plus
    Presets.push_back(MakePreset("cross", "Cross", EvaluateField([&](float X, float Y) {
        const float Vertical = std::max(std::fabs(X) - 0.22f, std::fabs(Y) - 0.8f);
        const float Horizontal = std::max(std::fabs(X) - 0.8f, std::fabs(Y) - 0.22f);
        return CoverageFromDistance(std::min(Vertical, Horizontal) / PixelInField);
    })));
    // Hazard band: diagonal stripes inside a rounded frame (dim stripes stay faintly present)
    Presets.push_back(MakePreset("hazard", "Hazard stripes", EvaluateField([&](float X, float Y) {
        const float BoxX = std::fabs(X) - 0.86f, BoxY = std::fabs(Y) - 0.5f;
        const float RoundedBox = Length2(std::max(BoxX, 0.f), std::max(BoxY, 0.f)) + std::min(std::max(BoxX, BoxY), 0.f) - 0.06f;
        const float Frame = CoverageFromDistance(RoundedBox / PixelInField);
        const float Stripe = std::fmod(std::fabs(X * 0.9f + Y) * 3.2f, 1.f) < 0.5f ? 1.f : 0.22f;
        return Frame * Stripe;
    })));
    // Checker disc
    Presets.push_back(MakePreset("checker-disc", "Checker disc", EvaluateField([&](float X, float Y) {
        const float Disc = CoverageFromDistance((Length2(X, Y) - 0.8f) / PixelInField);
        const int CellX = int(std::floor((X + 1.f) * 4.f)), CellY = int(std::floor((Y + 1.f) * 4.f));
        return Disc * (((CellX + CellY) % 2 == 0) ? 1.f : 0.18f);
    })));
    return Presets;
}

const StampLibraryEntry *StampLibrary::Find(const std::string &Id) const noexcept
{
    for (const auto &Entry : LibraryEntries)
        if (Entry.Id == Id) return &Entry;
    return nullptr;
}

size_t StampLibrary::DiskCount() const noexcept
{
    size_t Count = 0;
    for (const auto &Entry : LibraryEntries)
        if (!Entry.Builtin) ++Count;
    return Count;
}

void StampLibrary::Reload(const std::filesystem::path &Directory)
{
    LibraryDirectory = Directory;
    LibraryEntries = BuiltinPresets();
    if (Directory.empty()) return;
    std::error_code Failure;
    if (!std::filesystem::is_directory(Directory, Failure)) return;
    std::vector<std::filesystem::path> Files;
    for (const auto &Item : std::filesystem::directory_iterator(Directory, Failure))
    {
        if (Failure) break;
        if (Item.is_regular_file(Failure) && HasPngExtension(Item.path())) Files.push_back(Item.path());
    }
    std::sort(Files.begin(), Files.end());
    for (const auto &File : Files)
    {
        const std::vector<uint8_t> Bytes = ReadFileBytes(File, 32u * 1024u * 1024u);
        if (Bytes.empty()) continue;
        int Width = 0, Height = 0, Channels = 0;
        uint8_t *Pixels = stbi_load_from_memory(Bytes.data(), int(Bytes.size()), &Width, &Height, &Channels, 4);
        if (!Pixels) continue;
        const bool Usable = Width > 0 && Height > 0 && Width <= MaxStampExtent && Height <= MaxStampExtent;
        if (Usable)
        {
            StampLibraryEntry Entry;
            Entry.Id = "disk:" + File.filename().string();
            Entry.Name = DisplayName(File.stem().string());
            Entry.Width = Width;
            Entry.Height = Height;
            Entry.Rgba.assign(Pixels, Pixels + size_t(Width) * Height * 4);
            Entry.File = File;
            LibraryEntries.push_back(std::move(Entry));
        }
        stbi_image_free(Pixels);
    }
}

bool StampLibrary::SaveRaster(const std::filesystem::path &Directory, const std::string &Name,
    int Width, int Height, const std::vector<uint8_t> &Rgba, const std::string &SourceToml,
    std::string &OutId, std::string &Diagnostic)
{
    if (Directory.empty()) { Diagnostic = "No stamp library directory is configured."; return false; }
    if (Width <= 0 || Height <= 0 || Width > MaxStampExtent || Height > MaxStampExtent ||
        Rgba.size() != size_t(Width) * Height * 4)
    {
        Diagnostic = "Stamp raster is empty or out of bounds.";
        return false;
    }
    std::error_code Failure;
    std::filesystem::create_directories(Directory, Failure);
    if (Failure) { Diagnostic = "Could not create the stamp library directory."; return false; }
    const std::string Base = Slugify(Name);
    std::filesystem::path Target;
    for (int Version = 1; Version <= 99; ++Version)
    {
        const std::string Stem = Version == 1 ? Base : Base + "-" + std::to_string(Version);
        Target = Directory / (Stem + ".png");
        if (!std::filesystem::exists(Target, Failure)) break;
        if (Version == 99) { Diagnostic = "Too many stamps share this name."; return false; }
    }
    if (!stbi_write_png(Target.string().c_str(), Width, Height, 4, Rgba.data(), Width * 4))
    {
        Diagnostic = "Could not write the stamp PNG.";
        return false;
    }
    if (!SourceToml.empty())
    {
        const std::filesystem::path Sidecar = Target.parent_path() / (Target.stem().string() + ".stamp.toml");
        std::ofstream Output(Sidecar, std::ios::binary | std::ios::trunc);
        if (Output) Output << SourceToml; // sidecar is best-effort provenance; the PNG is the stamp
    }
    OutId = "disk:" + Target.filename().string();
    return true;
}

std::array<float, 4> StampLibrary::Sample(const StampLibraryEntry &Entry, float U, float V) noexcept
{
    if (Entry.Width <= 0 || Entry.Height <= 0 || Entry.Rgba.size() != size_t(Entry.Width) * Entry.Height * 4)
        return {1.f, 1.f, 1.f, 0.f};
    const float X = std::clamp(U, 0.f, 1.f) * (Entry.Width - 1);
    const float Y = std::clamp(V, 0.f, 1.f) * (Entry.Height - 1);
    const int X0 = int(std::floor(X)), Y0 = int(std::floor(Y));
    const int X1 = std::min(Entry.Width - 1, X0 + 1), Y1 = std::min(Entry.Height - 1, Y0 + 1);
    const float Fx = X - X0, Fy = Y - Y0;
    const auto At = [&](int Px, int Py, int Channel) noexcept {
        return Entry.Rgba[(size_t(Py) * Entry.Width + Px) * 4 + Channel] / 255.f;
    };
    std::array<float, 4> Result{};
    for (int Channel = 0; Channel < 4; ++Channel)
    {
        const float Top = At(X0, Y0, Channel) + (At(X1, Y0, Channel) - At(X0, Y0, Channel)) * Fx;
        const float Bottom = At(X0, Y1, Channel) + (At(X1, Y1, Channel) - At(X0, Y1, Channel)) * Fx;
        Result[size_t(Channel)] = Top + (Bottom - Top) * Fy;
    }
    return Result;
}
} // namespace Frontier
