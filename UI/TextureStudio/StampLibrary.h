/*============================================================================================================================================
                                                  STAMPLIBRARY.H
============================================================================================================================================*/
// 🖨️ The decal library shared by the Stamp workspace (which authors and saves stamps) and the Texture
//    Paint workspace (which places them): built-in procedural presets plus user stamps saved as PNGs in
//    one directory (the host points both panels at <library>/Stamps). A Disk stamp saved from the Stamp
//    workspace also keeps a <name>.stamp.toml sidecar with its vector source, so it stays re-openable.

#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Frontier
{
// 📝 One decal: straight-alpha RGBA pixels, row-major from the top-left (image v grows downward).
struct StampLibraryEntry
{
    std::string Id;   // "builtin:<slug>" or "disk:<filename>"
    std::string Name; // display name for pickers/status lines
    bool Builtin = false;
    bool Tintable = false; // white art: Texture Paint tints it with the brush colour when placing
    int Width = 0, Height = 0;
    std::vector<uint8_t> Rgba;
    std::filesystem::path File; // PNG path for Disk entries, empty for built-ins
};

class StampLibrary
{
  public:
    // 🔄 (Re)scan Directory for *.png stamps; built-in presets always lead the list. Missing/unreadable
    // files are skipped, never fatal - a half-saved library still opens.
    void Reload(const std::filesystem::path &Directory);
    [[nodiscard]] const std::vector<StampLibraryEntry> &Entries() const noexcept { return LibraryEntries; }
    [[nodiscard]] const StampLibraryEntry *Find(const std::string &Id) const noexcept;
    [[nodiscard]] const std::filesystem::path &Directory() const noexcept { return LibraryDirectory; }
    [[nodiscard]] size_t DiskCount() const noexcept;

    // 💾 Write <slug>.png (and <slug>.stamp.toml when SourceToml is non-empty) into Directory, creating
    // it if needed; filename collisions get a numeric suffix. Returns the new entry's id via OutId.
    static bool SaveRaster(const std::filesystem::path &Directory, const std::string &Name,
        int Width, int Height, const std::vector<uint8_t> &Rgba, const std::string &SourceToml,
        std::string &OutId, std::string &Diagnostic);
    // 🎨 The six procedural presets (white art at 256px, tintable): ring, star, arrow, cross, hazard, checker disc.
    [[nodiscard]] static std::vector<StampLibraryEntry> BuiltinPresets();
    // 🔬 Bilinear sample with uv in [0,1]^2 (v downward from the image top); returns straight rgba 0..1.
    [[nodiscard]] static std::array<float, 4> Sample(const StampLibraryEntry &Entry, float U, float V) noexcept;

  private:
    std::filesystem::path LibraryDirectory;
    std::vector<StampLibraryEntry> LibraryEntries;
};
} // namespace Frontier
