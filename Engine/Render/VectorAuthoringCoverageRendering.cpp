/*============================================================================================================================================
                                                        VECTORAUTHORINGCOVERAGERENDERING.CPP
============================================================================================================================================*/
// 🧩 Track cached sampling serials and bounded retired output slots; authoring descriptors remain independent of GPU resource lifetime.

#include "VectorAuthoringCoverageRendering.h"
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
}

void VectorAuthoringCoverageRendering::Initialize(const VulkanRasterAccess &Access, RasterShader Shader)
{
    for (auto &Slot : Slots) Slot.Rasterization.Initialize(Access, Shader, 512, 512);
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
        if (Slot.SurfaceProgram) Slot.Rasterization.ConfigureRetiredSurface(*Slot.SurfaceProgram, Completed);
        else Slot.Rasterization.ConfigureRetiredProgram(*Slot.Program, Completed);
        Slot.Pending = false;
    }
    (void)Slot.Rasterization.ConstructCommands(Commands, Completed, SubmissionSerial);
    PendingSerial = SubmissionSerial;
}

void VectorAuthoringCoverageRendering::ConfirmSubmission()
{
    if (PendingSerial && Selected >= 0)
    {
        auto &Slot = Slots[size_t(Selected)];
        Slot.Rasterization.ConfirmSubmission(PendingSerial);
        Slot.SampledSerial = PendingSerial;
    }
    PendingSerial = 0;
    SampleRequested = false;
}

VkImage VectorAuthoringCoverageRendering::QueryImage(size_t Slot) const
{
    return Slots.at(Slot).Rasterization.QueryImage();
}

VkImageView VectorAuthoringCoverageRendering::QueryImageView(size_t Slot) const
{
    return Slots.at(Slot).Rasterization.QueryImageView();
}

uint64_t VectorAuthoringCoverageRendering::QueryDispatchCount() const noexcept
{
    uint64_t Count = 0;
    for (const auto &Slot : Slots) Count += Slot.Rasterization.QueryDispatchCount();
    return Count;
}

void VectorAuthoringCoverageRendering::Finalize() noexcept
{
    for (auto &Slot : Slots)
    {
        Slot.Rasterization.Finalize();
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
