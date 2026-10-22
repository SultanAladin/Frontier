/*==========================================================================================================================================
                                                      MATERIALLIBRARYRENDERING.CPP
==========================================================================================================================================*/
// 🧩 Bake one requested dirty tile per submission using shared Slang programs, GPU scratch rasterization and one sampled atlas.

#include "MaterialLibraryRendering.h"
#include "MaterialPreviewGeometry.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace Frontier
{
namespace
{
uint64_t ComputeMaterialFingerprint(const CompiledSurfaceMaterial &Material)
{
    uint64_t Digest = 14695981039346656037ull;
    const auto Accumulate = [&](const void *Bytes, size_t Count)
    {
        const auto *Octets = static_cast<const unsigned char *>(Bytes);
        for (size_t Index = 0; Index < Count; ++Index) { Digest ^= Octets[Index]; Digest *= 1099511628211ull; }
    };
    Accumulate(&Material.Scattering, sizeof(Material.Scattering));
    const auto AccumulatePyramid = [&](const MaterialTexturePyramid &Texture)
    {
        Accumulate(&Texture.Category, sizeof(Texture.Category));
        Accumulate(&Texture.Layers, sizeof(Texture.Layers));
        for (const auto &Level : Texture.Levels)
        {
            Accumulate(&Level.Width, sizeof(Level.Width));
            Accumulate(&Level.Height, sizeof(Level.Height));
        }
        Accumulate(Texture.Rgba.data(), Texture.Rgba.size());
        Accumulate(Texture.FloatRgba.data(), Texture.FloatRgba.size() * sizeof(float));
    };
    for (const auto &Texture : Material.Textures) AccumulatePyramid(Texture);
    // 📝 Native Pattern maps can change while the preview path and PBR coefficients remain identical.
    for (const auto &Texture : Material.AuthoringMaps) AccumulatePyramid(Texture);
    return Digest;
}
// 📝 Cheap change test for baked tiles: digest the authored source bytes plus size/write-time of every
// referenced Textures/ map. Library tiles are a baked atlas — re-evaluation must never decode or rebake
// a material whose inputs are unchanged. Returns 0 when the inputs cannot be authenticated (rebake).
uint64_t ComputeMaterialSourceDigest(const std::filesystem::path &Directory, const std::filesystem::path &RelativePath)
{
    std::error_code Error;
    std::ifstream Input(Directory / RelativePath, std::ios::binary | std::ios::ate);
    const auto Bytes = Input.tellg();
    if (!Input || Bytes <= 0 || Bytes > 1048576) return 0;
    std::string Source(size_t(Bytes), '\0');
    Input.seekg(0);
    if (!Input.read(Source.data(), Bytes)) return 0;
    uint64_t Digest = 14695981039346656037ull;
    const auto Accumulate = [&](const void *Data, size_t Count)
    {
        const auto *Octets = static_cast<const unsigned char *>(Data);
        for (size_t Index = 0; Index < Count; ++Index) { Digest ^= Octets[Index]; Digest *= 1099511628211ull; }
    };
    Accumulate(Source.data(), Source.size());
    size_t Search = 0;
    uint32_t Referenced = 0;
    while ((Search = Source.find("Textures/", Search)) != std::string::npos)
    {
        const auto End = Source.find('"', Search);
        if (End == std::string::npos) return 0;
        const auto TexturePath = (Directory / RelativePath.parent_path() /
            Source.substr(Search, End - Search)).lexically_normal();
        const auto Size = std::filesystem::file_size(TexturePath, Error);
        if (Error) return 0;
        const auto Written = std::filesystem::last_write_time(TexturePath, Error).time_since_epoch().count();
        if (Error) return 0;
        Accumulate(&Size, sizeof(Size));
        Accumulate(&Written, sizeof(Written));
        Search = End + 1;
        if (++Referenced > 64) return 0;
    }
    return Digest ? Digest : 1;
}
std::string DescribeMaterialMaps(const CompiledSurfaceMaterial &Material)
{
    std::ostringstream Line;
    const size_t Strata = Material.Scattering.Illumination.Configuration[0];
    Line << "strata=" << Strata << " masks=[";
    for (size_t Index = 0; Index < Strata && Index < 8; ++Index)
    {
        if (Index) Line << ',';
        Line << Material.Scattering.TextureUse[Index][0];
    }
    Line << "] slices=[";
    for (size_t Index = 0; Index < Strata && Index < 8; ++Index)
    {
        if (Index) Line << ';';
        Line << Material.Scattering.TextureUse[Index][1] << '/' << Material.Scattering.TextureUse[Index][2]
             << '/' << Material.Scattering.TextureUse[Index][3];
    }
    Line << "] maps=[";
    for (size_t Slot = 0; Slot < 3; ++Slot)
    {
        const auto &Pyramid = Material.Textures[Slot];
        if (Slot) Line << ',';
        if (Pyramid.Levels.empty()) { Line << "none"; continue; }
        const auto &Level = Pyramid.Levels[0];
        Line << Level.Width << 'x' << Level.Height;
        bool Varied = false;
        if (Level.Width > 1 || Level.Height > 1)
        {
            const size_t Count = size_t(Level.Width) * Level.Height;
            const size_t Step = Count / 2048 + 1;
            uint8_t Min[3] = {255, 255, 255}, Max[3] = {0, 0, 0};
            for (size_t Pixel = 0; Pixel < Count; Pixel += Step)
            {
                const size_t At = Level.Offset + Pixel * 4;
                if (At + 4 > Pyramid.Rgba.size()) break;
                for (size_t Channel = 0; Channel < 3; ++Channel)
                {
                    Min[Channel] = std::min(Min[Channel], Pyramid.Rgba[At + Channel]);
                    Max[Channel] = std::max(Max[Channel], Pyramid.Rgba[At + Channel]);
                }
            }
            for (size_t Channel = 0; Channel < 3; ++Channel)
                if (int(Max[Channel]) - int(Min[Channel]) > 8) Varied = true;
        }
        Line << (Varied ? '*' : '-');
    }
    Line << ']';
    return Line.str();
}
std::vector<uint32_t> DecodePreviewShader(const std::filesystem::path &Path)
{
    std::ifstream Input(Path, std::ios::binary | std::ios::ate);
    const auto Bytes = Input.tellg();
    if (!Input || Bytes < 20 || Bytes > 16777216 || Bytes % 4) throw std::runtime_error("Missing or malformed material shader archive.");
    std::vector<uint32_t> Words(size_t(Bytes) / 4);
    Input.seekg(0);
    if (!Input.read(reinterpret_cast<char *>(Words.data()), Bytes) || Words[0] != 0x07230203)
        throw std::runtime_error("Invalid material shader archive.");
    return Words;
}
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SHARED RESIDENCY
//------------------------------------------------------------------------------------------------------------------------

struct MaterialLibraryRendering::RenderingAllocation
{
    SurfaceRasterPrograms Programs;
    SurfaceGeometryResidence Geometry;
    SurfaceRasterization Tile, Preview;
    MaterialThumbnailAtlas Atlas;
    struct MaterialGpuResidence
    {
        SurfaceMaterialResidence Textures;
        SurfaceScatteringResidence Parameters;
    };
    struct ThumbnailSlot
    {
        std::filesystem::path Path;
        uint64_t Generation = 0, Fingerprint = 0, Access = 0;
        uint64_t SourceDigest = 0;
        bool Ready = false, Pinned = false, Dirty = false;
        std::string Diagnostic;
    };
    struct PendingResidence
    {
        uint64_t Serial;
        std::shared_ptr<MaterialGpuResidence> Material;
    };
    struct PreviewRetirement
    {
        uint64_t Serial;
        std::shared_ptr<SurfaceScatteringResidence> Parameters;
    };
    std::vector<PreviewRetirement> PreviewRetirements;
    struct RetiredPreviewTarget
    {
        uint64_t Serial;
        std::unique_ptr<SurfaceRasterization> Target;
    };
    std::vector<RetiredPreviewTarget> RetiredPreviews;
    uint32_t PendingPreviewExtent = 0;
    MaterialScattering PreviewScattering;
    MaterialPreviewConfiguration Inspection;
    std::shared_ptr<SurfaceScatteringResidence> PreviewParameters;
    // 📝 One completed spare parameter residence is recycled ping-pong style so continuous slider
    // edits stop allocating a fresh descriptor pool/set/buffer per input tick.
    std::shared_ptr<SurfaceScatteringResidence> SparePreviewParameters;
    uint64_t SparePreviewSerial = 0, CompletedSerial = 0;
    std::array<std::array<uint32_t, 2>, 5> ShapeIndexRanges{};
    std::vector<ThumbnailSlot> Slots;
    std::vector<PendingResidence> InFlight;
    std::filesystem::path Directory, Selection;
    std::shared_ptr<MaterialGpuResidence> Selected;
    uint64_t Clock = 0, GenerationClock = 0, SelectedFingerprint = 0, SubmittedSerial = 0, BakeCount = 0;
    bool PreviewDirty = false, PreviewReady = false, RecordedPreview = false;
    int RecordedSlot = -1;
    std::string Diagnostic;
    SurfaceNormalProjection Projection;

    std::shared_ptr<MaterialGpuResidence> CompileResidence(const CompiledSurfaceMaterial &Material)
    {
        auto Residence = std::make_shared<MaterialGpuResidence>();
        auto Scattering = Material.Scattering;
        Scattering.PreviewConfiguration = {1,1,0,0};
        Scattering.Illumination.EnvironmentHorizon = {.10f,.105f,.11f,1};
        Scattering.Illumination.EnvironmentZenith = {.32f,.33f,.34f,0};
        // 📝 Thumbnails share the Alloy capture rig (34° perspective, per-pixel view); the fixed tile
        // projection below is the same default camera, so cards match the capture reference.
        Scattering.Illumination.Configuration[2] = 1;
        Scattering.Illumination.ViewFromSurface =
            ConstructMaterialPreviewProjection(MaterialPreviewConfiguration{}).ViewFromSurface;
        Residence->Textures.Initialize(Programs, Material.Textures, Material.AuthoringMaps);
        Residence->Parameters.Initialize(Programs, Scattering);
        return Residence;
    }
    void ReconfigurePreview()
    {
        if (!Selected) return;
        auto Scattering = PreviewScattering;
        Scattering.PreviewConfiguration = {uint32_t(Inspection.Shape) + 1,
            Inspection.Environment == MaterialPreviewEnvironment::StudioSoftbox ? 1u : 0u,0,0};
        Scattering.Illumination.ViewFromSurface = ConstructMaterialPreviewProjection(Inspection).ViewFromSurface;
        Scattering.Illumination.Configuration[2] = 1;
        // 📝 Preview rigs are owned by the selected environment, never inherited from arbitrary authored source lighting.
        Scattering.Illumination.LightColourExposure = {1,1,1,Inspection.Exposure};
        Scattering.Illumination.LightDirectionIntensity = {-.4f,.7f,.6f,.35f};
        switch (Inspection.Environment)
        {
        case MaterialPreviewEnvironment::StudioSoftbox:
            Scattering.Illumination.EnvironmentHorizon = {.10f,.105f,.11f,.8f};
            Scattering.Illumination.EnvironmentZenith = {.32f,.33f,.34f,0};
            break;
        case MaterialPreviewEnvironment::WarmCourtyard:
            Scattering.Illumination.EnvironmentHorizon = {.22f,.12f,.055f,1.2f};
            Scattering.Illumination.EnvironmentZenith = {.65f,.43f,.22f,0};
            Scattering.Illumination.LightColourExposure = {1,.78f,.52f,Inspection.Exposure};
            Scattering.Illumination.LightDirectionIntensity = {-.65f,.55f,.5f,4};
            break;
        case MaterialPreviewEnvironment::CoolRim:
            Scattering.Illumination.EnvironmentHorizon = {.035f,.075f,.15f,1.5f};
            Scattering.Illumination.EnvironmentZenith = {.18f,.35f,.65f,0};
            Scattering.Illumination.LightColourExposure = {.55f,.75f,1,Inspection.Exposure};
            Scattering.Illumination.LightDirectionIntensity = {.8f,.25f,.35f,5};
            break;
        case MaterialPreviewEnvironment::Overcast:
            Scattering.Illumination.EnvironmentHorizon = {.35f,.35f,.35f,1.5f};
            Scattering.Illumination.EnvironmentZenith = {.8f,.8f,.8f,0};
            Scattering.Illumination.LightDirectionIntensity = {-.2f,.9f,.4f,.7f};
            break;
        }
        std::shared_ptr<SurfaceScatteringResidence> Replacement;
        if (SparePreviewParameters && SparePreviewSerial <= CompletedSerial)
        {
            // 📝 Every submission that sampled the spare has fenced; reuse its object instead of
            // allocating another pool/set/buffer for this edit tick.
            Replacement = std::move(SparePreviewParameters);
            Replacement->Finalize();
        }
        else Replacement = std::make_shared<SurfaceScatteringResidence>();
        Replacement->Initialize(Programs, Scattering);
        if (PreviewParameters)
        {
            if (!SparePreviewParameters)
            {
                SparePreviewParameters = std::move(PreviewParameters);
                SparePreviewSerial = SubmittedSerial;
            }
            else PreviewRetirements.push_back({SubmittedSerial, std::move(PreviewParameters)});
        }
        PreviewParameters = std::move(Replacement);
        PreviewDirty = true;
        PreviewReady = false;
    }
    CompiledSurfaceMaterial DecodeLibraryMaterial(const std::filesystem::path &RelativePath)
    {
        const auto Path = std::filesystem::canonical(Directory / RelativePath);
        const auto Contained = Path.lexically_relative(Directory);
        if (Contained.empty() || Contained.is_absolute() || *Contained.begin() == "..")
            throw std::runtime_error("Thumbnail material escapes ContentDirectory.");
        std::ifstream Input(Path, std::ios::binary | std::ios::ate);
        const auto Bytes = Input.tellg();
        if (!Input || Bytes < 0 || Bytes > 1048576) throw std::runtime_error("Thumbnail material source missing or oversized.");
        std::string Source(size_t(Bytes), '\0');
        Input.seekg(0);
        if (!Input.read(Source.data(), Bytes)) throw std::runtime_error("Thumbnail source read failed.");
        auto Definition = DecodeSurfaceMaterial(Source);
        EnsureProceduralChannelBakes(Definition, 256);
        return CompileSurfaceMaterial(Definition, Directory);
    }
};

MaterialLibraryRendering::MaterialLibraryRendering() = default;
MaterialLibraryRendering::~MaterialLibraryRendering() = default;
void MaterialLibraryRendering::Initialize(const VulkanRasterAccess &Access, const std::filesystem::path &ShaderDirectory)
{
    if (Allocation) throw std::runtime_error("Material library rendering already initialized.");
    DiagnosticJournal::Append("MaterialGPU", "Initialize shader directory: " + DiagnosticJournal::EncodePath(ShaderDirectory));
    auto Resident = std::make_unique<RenderingAllocation>();
    const std::array<const char *, 10> Names{"CadSurfaceVertex", "CadSurfaceFragment", "CadEdgeVertex", "CadEdgeFragment",
        "MatCapSurfaceVertex", "MatCapSurfaceFragment", "PreviewPbrSurfaceVertex", "PreviewPbrSurfaceFragment", "MaterialSurfaceVertex", "MaterialSurfaceFragment"};
    std::array<std::vector<uint32_t>, 10> Words;
    for (size_t Index = 0; Index < Names.size(); ++Index)
    {
        Words[Index] = DecodePreviewShader(ShaderDirectory / (std::string(Names[Index]) + ".spv"));
        DiagnosticJournal::Append("Shader", std::string(Names[Index]) + " bytes=" + std::to_string(Words[Index].size() * 4));
    }
    Resident->Programs.Initialize(Access, {{Words[0],"main"},{Words[1],"main"},{Words[2],"main"},{Words[3],"main"},
        {Words[4],"main"},{Words[5],"main"},{Words[6],"main"},{Words[7],"main"},{Words[8],"main"},{Words[9],"main"}});
    auto Inspection = ConstructMaterialInspectionGeometryWithRanges();
    Resident->ShapeIndexRanges = Inspection.ShapeIndexRanges;
    Resident->Geometry.Initialize(Access, Inspection.Geometry);
    Resident->Tile.Initialize(Resident->Programs, Resident->Geometry, 256, 256);
    Resident->Preview.Initialize(Resident->Programs, Resident->Geometry, 512, 512);
    // 📝 Tiles always bake the shader ball; the preview re-selects its shape range per recording.
    Resident->Tile.ConfigureIndexRange(Inspection.ShapeIndexRanges[0][0], Inspection.ShapeIndexRanges[0][1]);
    Resident->Preview.ConfigureIndexRange(Inspection.ShapeIndexRanges[0][0], Inspection.ShapeIndexRanges[0][1]);
    Resident->Tile.ConfigureTransparentBackground(true);
    Resident->Preview.ConfigureTransparentBackground(true);
    Resident->Atlas.Initialize(Access, 256, 8);
    Resident->Slots.resize(Resident->Atlas.QueryCapacity());
    Resident->Projection.ClipFromSurface =
        ConstructMaterialPreviewProjection(MaterialPreviewConfiguration{}).ClipFromSurface;
    Allocation = std::move(Resident);
}
void MaterialLibraryRendering::RequestLibrary(const std::filesystem::path &Directory, const std::vector<std::filesystem::path> &Visible)
{
    if (!Allocation) throw std::runtime_error("Material rendering is not initialized.");
    auto &Rendering = *Allocation;
    if (Directory.empty()) return;
    const auto Canonical = std::filesystem::canonical(Directory);
    if (Canonical != Rendering.Directory)
    {
        Rendering.Directory = Canonical;
        for (auto &Slot : Rendering.Slots) Slot = {};
        Rendering.Selection.clear();
        if (Rendering.Selected) Rendering.InFlight.push_back({Rendering.SubmittedSerial, std::move(Rendering.Selected)});
        Rendering.SelectedFingerprint = 0;
        Rendering.PreviewReady = Rendering.PreviewDirty = false;
    }
    ++Rendering.Clock;
    // 📝 Pin all existing visible slots before allocation, so a later card cannot lose its tile mid-layout.
    for (auto &Slot : Rendering.Slots)
        Slot.Pinned = std::find(Visible.begin(), Visible.end(), Slot.Path) != Visible.end();
    for (const auto &Path : Visible)
    {
        auto Existing = std::find_if(Rendering.Slots.begin(), Rendering.Slots.end(), [&](const auto &Slot) { return Slot.Path == Path; });
        if (Existing == Rendering.Slots.end())
        {
            Existing = std::min_element(Rendering.Slots.begin(), Rendering.Slots.end(), [](const auto &Left, const auto &Right)
            {
                if (Left.Pinned != Right.Pinned) return !Left.Pinned;
                return Left.Access < Right.Access;
            });
            if (Existing->Pinned) continue;
            const auto Generation = ++Rendering.GenerationClock;
            *Existing = {};
            Existing->Path = Path;
            Existing->Generation = Generation;
            Existing->Dirty = Existing->Pinned = true;
        }
        Existing->Access = Rendering.Clock;
    }
}
void MaterialLibraryRendering::ConfigureSelection(const std::filesystem::path &Path, const CompiledSurfaceMaterial &Material)
{
    if (!Allocation) throw std::runtime_error("Material rendering is not initialized.");
    auto &Rendering = *Allocation;
    const auto Digest = ComputeMaterialFingerprint(Material);
    if (Rendering.Selection == Path && Rendering.SelectedFingerprint == Digest) return;
    auto Replacement = Rendering.CompileResidence(Material);
    if (Rendering.Selected) Rendering.InFlight.push_back({Rendering.SubmittedSerial, std::move(Rendering.Selected)});
    Rendering.Selected = std::move(Replacement);
    Rendering.PreviewScattering = Material.Scattering;
    Rendering.ReconfigurePreview();
    DiagnosticJournal::Append("MaterialSelection", DiagnosticJournal::EncodePath(Path) + " fingerprint=" + std::to_string(Digest));
    DiagnosticJournal::Append("MaterialMaps", DiagnosticJournal::EncodePath(Path) + " " + DescribeMaterialMaps(Material));
    Rendering.SelectedFingerprint = Digest;
    Rendering.Selection = Path;
    Rendering.PreviewDirty = true;
    // 📝 Selection edits deliberately do NOT dirty the catalogue tile: the atlas is baked content and
    // only re-renders when the material is saved (InvalidateThumbnail) or the library is refreshed.
}
void MaterialLibraryRendering::ConfigurePreview(const MaterialPreviewConfiguration &Configuration)
{
    if (!Allocation || !ValidateMaterialPreviewConfiguration(Configuration)) throw std::runtime_error("Invalid material inspection configuration.");
    auto &Rendering = *Allocation;
    if (Rendering.Inspection == Configuration) return;
    Rendering.Inspection = Configuration;
    Rendering.ReconfigurePreview();
    DiagnosticJournal::Append("PreviewConfiguration", "shape=" + std::to_string(uint32_t(Configuration.Shape)) +
        " environment=" + std::to_string(uint32_t(Configuration.Environment)) + " zoom=" + std::to_string(Configuration.Zoom) +
        " exposure=" + std::to_string(Configuration.Exposure) +
        " pan=" + std::to_string(Configuration.PanX) + "," + std::to_string(Configuration.PanY) +
        " orbit=" + std::to_string(Configuration.Yaw) + "," + std::to_string(Configuration.Pitch));
}
MaterialPreviewConfiguration MaterialLibraryRendering::QueryPreviewConfiguration() const
{
    return Allocation ? Allocation->Inspection : MaterialPreviewConfiguration{};
}
void MaterialLibraryRendering::ConfigurePreviewExtent(uint32_t Extent)
{
    if (!Allocation) throw std::runtime_error("Material rendering is not initialized.");
    auto &Rendering = *Allocation;
    const uint32_t Clamped = std::clamp(Extent, 256u, 2048u);
    if (Clamped != Rendering.Preview.QueryExtent().width) Rendering.PendingPreviewExtent = Clamped;
}
uint32_t MaterialLibraryRendering::QueryPreviewExtent() const noexcept
{
    return Allocation ? Allocation->Preview.QueryExtent().width : 0;
}
void MaterialLibraryRendering::ConstructCommands(VkCommandBuffer Commands, uint64_t CompletedSerial, uint64_t SubmissionSerial)
{
    if (!Allocation) throw std::runtime_error("Material rendering is not initialized.");
    auto &Rendering = *Allocation;
    Rendering.CompletedSerial = std::max(Rendering.CompletedSerial, CompletedSerial);
    std::erase_if(Rendering.PreviewRetirements, [&](const auto &Residence) { return Residence.Serial <= CompletedSerial; });
    std::erase_if(Rendering.InFlight, [&](const auto &Residence) { return Residence.Serial <= CompletedSerial; });
    std::erase_if(Rendering.RetiredPreviews, [&](const auto &Retired) { return Retired.Serial <= CompletedSerial; });
    if (Rendering.PendingPreviewExtent && Rendering.PendingPreviewExtent != Rendering.Preview.QueryExtent().width)
    {
        auto Retired = std::make_unique<SurfaceRasterization>(std::move(Rendering.Preview));
        Rendering.RetiredPreviews.push_back({SubmissionSerial, std::move(Retired)});
        Rendering.Preview.Initialize(Rendering.Programs, Rendering.Geometry, Rendering.PendingPreviewExtent,
            Rendering.PendingPreviewExtent);
        Rendering.Preview.ConfigureTransparentBackground(true);
        const auto &ReplacementRange = Rendering.ShapeIndexRanges[size_t(Rendering.Inspection.Shape)];
        Rendering.Preview.ConfigureIndexRange(ReplacementRange[0], ReplacementRange[1]);
        DiagnosticJournal::Append("PreviewExtent", "preview target=" + std::to_string(Rendering.PendingPreviewExtent));
        Rendering.PendingPreviewExtent = 0;
        Rendering.PreviewDirty = true;
        Rendering.PreviewReady = false;
    }
    Rendering.SubmittedSerial = SubmissionSerial;
    Rendering.RecordedSlot = -1;
    Rendering.RecordedPreview = false;
    if (Rendering.Selected && Rendering.PreviewDirty)
    {
        auto Projection = Rendering.Projection;
        const auto Camera = ConstructMaterialPreviewProjection(Rendering.Inspection);
        Projection.ClipFromSurface = Camera.ClipFromSurface;
        const auto &View = Camera.ViewFromSurface;
        Projection.ViewNormalRow0 = {View[0],View[4],View[8],0};
        Projection.ViewNormalRow1 = {View[1],View[5],View[9],0};
        Projection.ViewNormalRow2 = {View[2],View[6],View[10],0};
        // 📝 Only the selected specimen is drawn; the other four share this upload 16 units apart and
        // otherwise swing into frame while orbiting (the reported "two shapes" viewport defect).
        const auto &Range = Rendering.ShapeIndexRanges[size_t(Rendering.Inspection.Shape)];
        Rendering.Preview.ConfigureIndexRange(Range[0], Range[1]);
        Rendering.Preview.ConstructCommands(Commands, Projection, Rendering.Selected->Textures, *Rendering.PreviewParameters);
        DiagnosticJournal::Append("PreviewSubmit", "serial=" + std::to_string(SubmissionSerial));
        Rendering.RecordedPreview = true;
        // 📝 First-use geometry/texture uploads belong to one target recording until accepted submission.
        // Alternate preview work and tile work rather than recording the same pending transfers twice.
        return;
    }
    const auto Dirty = std::find_if(Rendering.Slots.begin(), Rendering.Slots.end(), [](const auto &Slot) { return Slot.Dirty && Slot.Pinned; });
    if (Dirty == Rendering.Slots.end()) return;
    // 📝 Baked-atlas contract: an already-baked tile whose authored inputs are unchanged keeps its
    // pixels. The digest costs one small file read instead of a full decode/procedural-bake/compile.
    const auto SourceDigest = Dirty->Ready ? ComputeMaterialSourceDigest(Rendering.Directory, Dirty->Path) : 0;
    if (Dirty->Ready && SourceDigest && SourceDigest == Dirty->SourceDigest)
    {
        Dirty->Dirty = false;
        Dirty->Diagnostic.clear();
        return;
    }
    std::shared_ptr<RenderingAllocation::MaterialGpuResidence> Material;
    uint64_t Digest = Rendering.SelectedFingerprint;
    std::optional<CompiledSurfaceMaterial> Compiled;
    if (Dirty->Path == Rendering.Selection && Rendering.Selected) Material = Rendering.Selected;
    else
    {
        try
        {
            Compiled = Rendering.DecodeLibraryMaterial(Dirty->Path);
            Digest = ComputeMaterialFingerprint(*Compiled);
        }
        catch (const std::exception &Failure)
        {
            Dirty->Dirty = false;
            Dirty->Diagnostic = Rendering.Diagnostic = Failure.what();
            DiagnosticJournal::Append("MaterialTileRefused", DiagnosticJournal::EncodePath(Dirty->Path) + ": " + Rendering.Diagnostic);
            return;
        }
    }
    if (Dirty->Ready && Digest == Dirty->Fingerprint)
    {
        Dirty->Dirty = false;
        Dirty->Diagnostic.clear();
        if (SourceDigest) Dirty->SourceDigest = SourceDigest;
        return;
    }
    // ⚠️ GPU allocation/recording failures propagate to the application's severe-failure presentation.
    if (!Material) Material = Rendering.CompileResidence(*Compiled);
    Rendering.InFlight.push_back({SubmissionSerial, Material});
    Rendering.Tile.ConstructCommands(Commands, Rendering.Projection, Material->Textures, Material->Parameters);
    Rendering.Atlas.ConstructCopy(Commands, Rendering.Tile, uint32_t(Dirty - Rendering.Slots.begin()));
    Dirty->Fingerprint = Digest;
    Dirty->SourceDigest = SourceDigest ? SourceDigest : ComputeMaterialSourceDigest(Rendering.Directory, Dirty->Path);
    Rendering.RecordedSlot = int(Dirty - Rendering.Slots.begin());
    DiagnosticJournal::Append("AtlasBake", DiagnosticJournal::EncodePath(Dirty->Path) + " slot=" + std::to_string(Rendering.RecordedSlot));
    Dirty->Diagnostic.clear();
    Rendering.Diagnostic.clear();
}
void MaterialLibraryRendering::ConfirmSubmission() noexcept
{
    if (!Allocation) return;
    auto &Rendering = *Allocation;
    if (Rendering.RecordedPreview)
    {
        Rendering.Preview.ConfirmSubmission();
        Rendering.PreviewReady = true;
        Rendering.PreviewDirty = false;
    }
    if (Rendering.RecordedSlot >= 0)
    {
        Rendering.Tile.ConfirmSubmission();
        Rendering.Atlas.ConfirmSubmission();
        auto &Slot = Rendering.Slots[size_t(Rendering.RecordedSlot)];
        ++Rendering.BakeCount;
        Slot.Ready = true;
        Slot.Dirty = false;
    }
}
MaterialThumbnailSelection MaterialLibraryRendering::QueryThumbnail(const std::filesystem::path &Path) const
{
    if (!Allocation) return {};
    const auto &Rendering = *Allocation;
    const auto Slot = std::find_if(Rendering.Slots.begin(), Rendering.Slots.end(), [&](const auto &Entry) { return Entry.Path == Path; });
    if (Slot == Rendering.Slots.end() || !Slot->Ready) return {};
    return {Slot->Generation, Rendering.Atlas.QueryUv(uint32_t(Slot - Rendering.Slots.begin())), true};
}
void MaterialLibraryRendering::InvalidateThumbnails() noexcept
{
    // 📝 Marks every resident tile for source re-authentication; unchanged inputs keep their baked
    // pixels via the source digest, so a library refresh no longer rebakes the whole atlas.
    if (Allocation) for (auto &Slot : Allocation->Slots) if (!Slot.Path.empty()) Slot.Dirty = true;
}
void MaterialLibraryRendering::InvalidateThumbnail(const std::filesystem::path &RelativePath) noexcept
{
    // 📝 The only edit-path rebake: the material was saved to disk, so its tile (if resident) re-bakes.
    if (!Allocation) return;
    for (auto &Slot : Allocation->Slots)
        if (Slot.Path == RelativePath)
        {
            Slot.Dirty = true;
            Slot.SourceDigest = 0;
        }
}
bool MaterialLibraryRendering::AuthenticateThumbnail(const std::filesystem::path &Path, uint64_t Generation) const
{
    const auto Current = QueryThumbnail(Path);
    return Generation && Current.Ready && Current.Generation == Generation;
}
uint64_t MaterialLibraryRendering::QueryBakeCount() const noexcept { return Allocation ? Allocation->BakeCount : 0; }
VkImage MaterialLibraryRendering::QueryPreviewImage() const noexcept { return Allocation ? Allocation->Preview.QueryImage() : VK_NULL_HANDLE; }
VkImage MaterialLibraryRendering::QueryAtlasImage() const noexcept { return Allocation ? Allocation->Atlas.QueryImage() : VK_NULL_HANDLE; }
uint32_t MaterialLibraryRendering::QueryAtlasExtent() const noexcept { return Allocation ? Allocation->Atlas.QueryExtent() : 0; }
std::string MaterialLibraryRendering::QueryThumbnailDiagnostic(const std::filesystem::path &Path) const
{
    if (Allocation) for (const auto &Slot : Allocation->Slots) if (Slot.Path == Path) return Slot.Diagnostic;
    return {};
}
VkImageView MaterialLibraryRendering::QueryAtlasView() const noexcept { return Allocation ? Allocation->Atlas.QueryImageView() : VK_NULL_HANDLE; }
VkImageView MaterialLibraryRendering::QueryPreviewView() const noexcept { return Allocation ? Allocation->Preview.QueryImageView() : VK_NULL_HANDLE; }
bool MaterialLibraryRendering::QueryPreviewReady() const noexcept { return Allocation && Allocation->PreviewReady; }
const std::string &MaterialLibraryRendering::QueryDiagnostic() const noexcept
{
    static const std::string Empty;
    return Allocation ? Allocation->Diagnostic : Empty;
}
} // namespace Frontier
