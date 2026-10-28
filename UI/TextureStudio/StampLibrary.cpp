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
    // Soft disc: radial falloff for gentle decal fades and airbrush-style spots
    Presets.push_back(MakePreset("soft-disc", "Soft disc", EvaluateField([](float X, float Y) {
        return std::clamp((0.85f - Length2(X, Y)) / 0.45f, 0.f, 1.f);
    })));
    // Crescent moon: a disc with an offset bite
    Presets.push_back(MakePreset("crescent", "Crescent", EvaluateField([&](float X, float Y) {
        const float Outer = Length2(X, Y) - 0.78f;
        const float Bite = Length2(X - 0.34f, Y + 0.10f) - 0.64f;
        return CoverageFromDistance(std::max(Outer, -Bite) / PixelInField);
    })));
    // Heart: classic parametric outline sampled to a polygon (+Y is image-down, hence the flip)
    {
        std::vector<std::array<float, 2>> Heart;
        for (int I = 0; I < 72; ++I)
        {
            const float T = float(I) / 72.f * 6.2831853f;
            const float S = std::sin(T);
            const float Hx = 16.f * S * S * S;
            const float Hy = 13.f * std::cos(T) - 5.f * std::cos(2 * T) - 2.f * std::cos(3 * T) - std::cos(4 * T);
            Heart.push_back({Hx / 19.f, -Hy / 19.f});
        }
        Presets.push_back(MakePreset("heart", "Heart", PolygonField(Heart)));
    }
    // Lightning bolt
    Presets.push_back(MakePreset("bolt", "Bolt", PolygonField({
        {0.18f, -0.85f}, {-0.32f, 0.08f}, {-0.02f, 0.08f}, {-0.18f, 0.85f}, {0.34f, -0.12f}, {0.03f, -0.12f}})));
    // Ring presets share an outer-minus-inner polygon field
    const auto RingField = [](const std::vector<std::array<float, 2>> &Outer, const std::vector<std::array<float, 2>> &Inner) {
        auto Field = PolygonField(Outer);
        const auto Cut = PolygonField(Inner);
        for (size_t I = 0; I < Field.size(); ++I) Field[I] = std::clamp(Field[I] - Cut[I], 0.f, 1.f);
        return Field;
    };
    // Hex badge: hexagon ring
    {
        std::vector<std::array<float, 2>> Outer, Inner;
        for (int I = 0; I < 6; ++I)
        {
            const float Angle = 0.5235988f + I * 1.0471976f;
            Outer.push_back({std::cos(Angle) * 0.82f, std::sin(Angle) * 0.82f});
            Inner.push_back({std::cos(Angle) * 0.60f, std::sin(Angle) * 0.60f});
        }
        Presets.push_back(MakePreset("hex-badge", "Hex badge", RingField(Outer, Inner)));
    }
    // Warning: triangle ring
    Presets.push_back(MakePreset("warning", "Warning", RingField(
        {{0.f, -0.80f}, {0.78f, 0.62f}, {-0.78f, 0.62f}},
        {{0.f, -0.48f}, {0.50f, 0.44f}, {-0.50f, 0.44f}})));
    // Splatter: a union of deterministic blobs
    Presets.push_back(MakePreset("splatter", "Splatter", EvaluateField([&](float X, float Y) {
        static constexpr std::array<std::array<float, 3>, 9> Blobs{{
            {0.f, 0.f, .42f}, {-.38f, -.20f, .20f}, {.34f, -.30f, .16f}, {.42f, .24f, .13f}, {-.30f, .34f, .15f},
            {.08f, -.50f, .10f}, {-.55f, .05f, .08f}, {.60f, -.05f, .07f}, {-.12f, .56f, .09f}}};
        float Best = 1e9f;
        for (const auto &Blob : Blobs) Best = std::min(Best, Length2(X - Blob[0], Y - Blob[1]) - Blob[2]);
        return CoverageFromDistance(Best / PixelInField);
    })));
    // Paw print: elliptical pad plus four toes
    Presets.push_back(MakePreset("paw", "Paw print", EvaluateField([&](float X, float Y) {
        float Best = (Length2(X / 0.46f, (Y - 0.28f) / 0.34f) - 1.f) * 0.34f; // scaled-space ellipse distance, approx.
        static constexpr std::array<std::array<float, 3>, 4> Toes{{
            {-.48f, -.18f, .15f}, {-.17f, -.36f, .16f}, {.17f, -.36f, .16f}, {.48f, -.18f, .15f}}};
        for (const auto &Toe : Toes) Best = std::min(Best, Length2(X - Toe[0], Y - Toe[1]) - Toe[2]);
        return CoverageFromDistance(Best / PixelInField);
    })));
    // Sunburst: twelve alternating rays inside a disc with a solid heart
    Presets.push_back(MakePreset("sunburst", "Sunburst", EvaluateField([&](float X, float Y) {
        const float Radius = Length2(X, Y);
        const float Disc = CoverageFromDistance((Radius - 0.82f) / PixelInField);
        const float Sector = std::fmod(std::atan2(Y, X) * 1.9098593f + 12.f, 2.f);
        return Disc * (Radius < 0.20f ? 1.f : (Sector < 1.f ? 1.f : 0.16f));
    })));
    // Shield: flat top, sides sweeping to a bottom point
    Presets.push_back(MakePreset("shield", "Shield", PolygonField({
        {-0.55f, -0.78f}, {0.55f, -0.78f}, {0.55f, 0.0f}, {0.42f, 0.42f},
        {0.f, 0.82f}, {-0.42f, 0.42f}, {-0.55f, 0.0f}})));
    // Cog: eight-tooth gear (radial sector approximation) with a centre bore
    Presets.push_back(MakePreset("cog", "Cog", EvaluateField([&](float X, float Y) {
        const float Radius = Length2(X, Y);
        const float Sector = std::fmod(std::atan2(Y, X) * 1.2732395f + 8.f, 1.f); // 8 teeth around 2pi
        const float Outer = Sector < 0.5f ? 0.84f : 0.64f;
        const float Body = Radius - Outer;
        const float Bore = Radius - 0.26f;
        return CoverageFromDistance(std::max(Body, -Bore) / PixelInField);
    })));
    // Droplet: round belly with straight tangents rising to a tip
    {
        std::vector<std::array<float, 2>> Drop{{0.f, -0.88f}};
        for (int I = 0; I <= 40; ++I)
        {
            const float Angle = (-50.f + I * 7.f) * 0.0174533f; // -50deg .. 230deg, the long way under
            Drop.push_back({std::cos(Angle) * 0.56f, 0.28f + std::sin(Angle) * 0.56f});
        }
        Presets.push_back(MakePreset("droplet", "Droplet", PolygonField(Drop)));
    }
    // Leaf: vesica (two-disc intersection) on the diagonal with a centre vein slit
    Presets.push_back(MakePreset("leaf", "Leaf", EvaluateField([&](float X, float Y) {
        const float Lens = std::max(Length2(X + 0.30f, Y + 0.30f) - 0.95f, Length2(X - 0.30f, Y - 0.30f) - 0.95f);
        const float Vein = std::fabs(X + Y) * 0.7071068f - 0.018f;
        return CoverageFromDistance(std::max(Lens, -Vein) / PixelInField);
    })));
    // Diamond: gem silhouette with a bright table, slanted crown facets and a split pavilion
    Presets.push_back(MakePreset("diamond", "Diamond", EvaluateField([&](float X, float Y) {
        static const std::vector<std::array<float, 2>> Gem{
            {-0.45f, -0.55f}, {0.45f, -0.55f}, {0.80f, -0.10f}, {0.f, 0.75f}, {-0.80f, -0.10f}};
        const float Edge = PolygonEdgeDistance(X, Y, Gem) / PixelInField;
        const float Base = CoverageFromDistance(PointInPolygon(X, Y, Gem) ? -Edge : Edge);
        float Facet = 1.f;
        if (Y > -0.10f) Facet = std::fabs(X) < 0.30f * (0.75f - Y) / 0.85f ? 0.45f : 0.78f; // pavilion facets
        else if (std::fabs(X) > 0.45f) Facet = 0.80f;                                       // crown slants
        return Base * Facet;
    })));
    // Radiation trefoil: centre disc plus three 60-degree annular blades (top, lower left/right)
    Presets.push_back(MakePreset("radiation", "Radiation", EvaluateField([&](float X, float Y) {
        const float Radius = Length2(X, Y);
        const float Angle = std::atan2(Y, X);
        float Nearest = 1e9f;
        for (float Centre : {-1.5707963f, 0.5235988f, 2.6179939f})
        {
            float Delta = std::fabs(Angle - Centre);
            if (Delta > 3.1415927f) Delta = 6.2831853f - Delta;
            Nearest = std::min(Nearest, Delta);
        }
        const float Band = std::max(Radius - 0.82f, 0.30f - Radius);
        const float Blade = std::max(Band, (Nearest - 0.5235988f) * Radius);
        return CoverageFromDistance(std::min(Radius - 0.18f, Blade) / PixelInField);
    })));
    // Target: alternating concentric bands around a solid bullseye
    Presets.push_back(MakePreset("target", "Target", EvaluateField([&](float X, float Y) {
        const float Radius = Length2(X, Y);
        const float Disc = CoverageFromDistance((Radius - 0.82f) / PixelInField);
        const int Band = int(Radius / 0.205f);
        return Disc * (Band % 2 == 0 ? 1.f : 0.22f);
    })));
    // Chevrons: a bright rank chevron over a dimmer echo
    {
        auto Upper = PolygonField({{-0.72f, 0.0f}, {0.f, -0.72f}, {0.72f, 0.0f},
            {0.72f, 0.22f}, {0.f, -0.50f}, {-0.72f, 0.22f}});
        const auto Lower = PolygonField({{-0.72f, 0.50f}, {0.f, -0.22f}, {0.72f, 0.50f},
            {0.72f, 0.72f}, {0.f, 0.0f}, {-0.72f, 0.72f}});
        for (size_t I = 0; I < Upper.size(); ++I) Upper[I] = std::max(Upper[I], Lower[I] * 0.6f);
        Presets.push_back(MakePreset("chevrons", "Chevrons", Upper));
    }
    // Snowflake: six-fold folded spokes with two branch pairs, joined by a centre disc
    Presets.push_back(MakePreset("snowflake", "Snowflake", EvaluateField([&](float X, float Y) {
        const auto Segment = [](float Px, float Py, float Ax, float Ay, float Bx, float By) {
            const float Dx = Bx - Ax, Dy = By - Ay;
            const float T = std::clamp(((Px - Ax) * Dx + (Py - Ay) * Dy) / (Dx * Dx + Dy * Dy), 0.f, 1.f);
            return Length2(Px - (Ax + Dx * T), Py - (Ay + Dy * T));
        };
        const float Radius = Length2(X, Y);
        const float Folded = std::fmod(std::atan2(Y, X) + 3.1415927f, 1.0471976f) - 0.5235988f;
        const float LocalX = Radius * std::cos(Folded), LocalY = std::fabs(Radius * std::sin(Folded));
        float Best = Segment(LocalX, LocalY, 0.06f, 0.f, 0.82f, 0.f) - 0.045f;
        Best = std::min(Best, Segment(LocalX, LocalY, 0.44f, 0.f, 0.67f, 0.193f) - 0.035f);
        Best = std::min(Best, Segment(LocalX, LocalY, 0.64f, 0.f, 0.78f, 0.116f) - 0.030f);
        Best = std::min(Best, Radius - 0.11f);
        return CoverageFromDistance(Best / PixelInField);
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
