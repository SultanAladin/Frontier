/*============================================================================================================================================
                                                        VECTORAUTHORINGCOVERAGERENDERING.CPP
============================================================================================================================================*/
// 🧩 Track cached sampling serials and bounded retired output slots; authoring descriptors remain independent of GPU resource lifetime.

#include "VectorAuthoringCoverageRendering.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include <algorithm>
#include <fstream>

namespace Frontier
{
namespace
{
bool EquivalentProgram(const VectorCoverageProgram &First, const VectorCoverageProgram &Second)
{
    return First.Root == Second.Root && First.Instructions == Second.Instructions && First.Segments == Second.Segments;
}
constexpr std::array<uint32_t, 4> PooledExtents{128, 256, 512, 1024};
size_t IndexForExtent(uint32_t Extent)
{
    for (size_t Index = 0; Index < PooledExtents.size(); ++Index)
        if (PooledExtents[Index] == Extent) return Index;
    return Extent <= 128 ? 0 : Extent <= 256 ? 1 : Extent <= 512 ? 2 : 3;
}
}

void VectorAuthoringCoverageRendering::Initialize(const VulkanRasterAccess &Access, RasterShader Shader)
{
    RetainedAccess = Access;
    RetainedShaderWords.assign(Shader.Words.begin(), Shader.Words.end());
    RetainedShaderEntry = Shader.Entry ? Shader.Entry : "";
}

void VectorAuthoringCoverageRendering::RefitSlot(size_t Index, size_t Segments)
{
    // 📝 The canvas runs the same unguarded curve loop as the bridge, single
    // channel; retired slots re-bracket before dense documents dispatch.
    const uint32_t Needed = VectorCoverageRasterization::SelectBakeExtent(Segments, 1);
    auto &Slot = Slots[Index];
    const size_t Which = IndexForExtent(Needed);
    if (Which == Slot.Active) return;
    if (!Slot.Extents[Which])
    {
        auto Fresh = std::make_unique<VectorCoverageRasterization>();
        Fresh->Initialize(RetainedAccess, RasterShader{RetainedShaderWords, RetainedShaderEntry.c_str()}, Needed, Needed);
        Slot.Extents[Which] = std::move(Fresh);
        DiagnosticJournal::Append("PatternBake", "canvas slot " + std::to_string(Index) + " pooled extent " +
            std::to_string(Needed) + " for " + std::to_string(Segments) + " segments");
    }
    Slot.Active = Which;
}

VectorCoverageRasterization &VectorAuthoringCoverageRendering::ActiveRasterization(size_t Index)
{
    auto *Active = Slots.at(Index).Extents.at(Slots.at(Index).Active).get();
    if (!Active) throw std::runtime_error("Authoring coverage slot has no fitted rasterization.");
    return *Active;
}

const VectorCoverageRasterization *VectorAuthoringCoverageRendering::QueryActiveRasterization(size_t Slot) const
{
    const auto &Entry = Slots.at(Slot);
    return Entry.Active < Entry.Extents.size() ? Entry.Extents[Entry.Active].get() : nullptr;
}

void VectorAuthoringCoverageRendering::Initialize(const VulkanRasterAccess &Access, const std::filesystem::path &ShaderArchive)
{
    std::ifstream Input(ShaderArchive, std::ios::binary | std::ios::ate);
    const auto Bytes = Input.tellg();
    if (!Input || Bytes < 20 || Bytes > 16777216 || Bytes % 4) throw std::runtime_error("Missing or malformed vector shader archive.");
    std::vector<uint32_t> Words(size_t(Bytes) / 4);
    Input.seekg(0);
    if (!Input.read(reinterpret_cast<char *>(Words.data()), Bytes) || Words[0] != 0x07230203) throw std::runtime_error("Invalid vector SPIR-V archive.");
    Initialize(Access, RasterShader{Words, "main"});
}

int VectorAuthoringCoverageRendering::SelectProgram(const VectorCoverageProgram &Program)
{
    ValidateVectorCoverage(Program);
    if (Selected >= 0 && Slots[size_t(Selected)].Program && EquivalentProgram(*Slots[size_t(Selected)].Program, Program))
    {
        SampleRequested = true;
        return Selected;
    }
    for (size_t Index = 0; Index < Slots.size(); ++Index)
    {
        auto &Slot = Slots[Index];
        if (Slot.SampledSerial > Completed) continue;
        RefitSlot(Index, Program.Segments.size());
        Slot.Program = Program;
        Slot.SurfaceProgram.reset();
        Slot.Pending = true;
        Selected = int(Index);
        SampleRequested = true;
        return Selected;
    }
    // 📝 Keep the last valid image visible until a different slot retires; never block the device for an edit.
    SampleRequested = Selected >= 0;
    return Selected;
}

int VectorAuthoringCoverageRendering::SelectSurfaceProgram(const VectorSurfaceProgram &Program)
{
    ValidateVectorSurface(Program);
    if (Selected >= 0 && Slots[size_t(Selected)].SurfaceProgram && *Slots[size_t(Selected)].SurfaceProgram == Program)
    {
        SampleRequested = true;
        return Selected;
    }
    for (size_t Index = 0; Index < Slots.size(); ++Index)
    {
        auto &Slot = Slots[Index];
        if (Slot.SampledSerial > Completed) continue;
        RefitSlot(Index, Program.Coverage.Segments.size());
        Slot.SurfaceProgram = Program;
        Slot.Program.reset();
        Slot.Pending = true;
        Selected = int(Index);
        SampleRequested = true;
        return Selected;
    }
    SampleRequested = Selected >= 0;
    return Selected;
}

void VectorAuthoringCoverageRendering::ConstructCommands(VkCommandBuffer Commands, uint64_t CompletedSerial, uint64_t SubmissionSerial)
{
    Completed = std::max(Completed, CompletedSerial);
    PendingSerial = 0;
    if (!SampleRequested || Selected < 0) return;
    auto &Slot = Slots[size_t(Selected)];
    if (Slot.Pending)
    {
        if (Slot.SampledSerial > Completed) throw std::runtime_error("Authoring coverage output is still sampled by an earlier submission.");
        if (Slot.SurfaceProgram) ActiveRasterization(size_t(Selected)).ConfigureRetiredSurface(*Slot.SurfaceProgram, Completed);
        else ActiveRasterization(size_t(Selected)).ConfigureRetiredProgram(*Slot.Program, Completed);
        Slot.Pending = false;
    }
    (void)ActiveRasterization(size_t(Selected)).ConstructCommands(Commands, Completed, SubmissionSerial);
    PendingSerial = SubmissionSerial;
}

void VectorAuthoringCoverageRendering::ConfirmSubmission()
{
    if (PendingSerial && Selected >= 0)
    {
        auto &Slot = Slots[size_t(Selected)];
        ActiveRasterization(size_t(Selected)).ConfirmSubmission(PendingSerial);
        Slot.SampledSerial = PendingSerial;
    }
    PendingSerial = 0;
    SampleRequested = false;
}

VkImage VectorAuthoringCoverageRendering::QueryImage(size_t Slot) const
{
    const auto *Active = QueryActiveRasterization(Slot);
    return Active ? Active->QueryImage() : VK_NULL_HANDLE;
}

VkImageView VectorAuthoringCoverageRendering::QueryImageView(size_t Slot) const
{
    const auto *Active = QueryActiveRasterization(Slot);
    return Active ? Active->QueryImageView() : VK_NULL_HANDLE;
}

uint32_t VectorAuthoringCoverageRendering::QuerySlotExtent(size_t Slot) const
{
    const auto &Entry = Slots.at(Slot);
    return Entry.Active < PooledExtents.size() ? PooledExtents[Entry.Active] : 0;
}

bool VectorAuthoringCoverageRendering::QuerySelectedReady() const noexcept
{
    if (Selected < 0) return false;
    const auto &Slot = Slots[size_t(Selected)];
    return !Slot.Pending && Slot.SampledSerial != 0 && Slot.SampledSerial <= Completed;
}

uint64_t VectorAuthoringCoverageRendering::QueryDispatchCount() const noexcept
{
    uint64_t Count = 0;
    for (const auto &Slot : Slots)
        for (const auto &Extent : Slot.Extents)
            if (Extent) Count += Extent->QueryDispatchCount();
    return Count;
}

void VectorAuthoringCoverageRendering::Finalize() noexcept
{
    for (auto &Slot : Slots)
    {
        for (auto &Extent : Slot.Extents)
            if (Extent) Extent->Finalize();
        Slot.Extents = {};
        Slot.Active = PooledExtents.size();
        Slot.Program.reset();
        Slot.SurfaceProgram.reset();
        Slot.SampledSerial = 0;
        Slot.Pending = false;
    }
    Completed = PendingSerial = 0;
    Selected = -1;
    SampleRequested = false;
}
}
