/*============================================================================================================================================
                                                     VECTORSURFACEMATERIALBRIDGE.H
============================================================================================================================================*/
// 🧩 Stage five retired Float32 vector channels into coherent PBR material-map pyramids.

#pragma once
#include "Engine/Render/SurfaceMaterial.h"
#include "Engine/Render/VectorCoverageRasterization.h"
#include "Engine/Render/VectorSurfaceCompilation.h"

namespace Frontier
{
class VectorSurfaceMaterialBridge
{
public:
    VectorSurfaceMaterialBridge();
    ~VectorSurfaceMaterialBridge();
    VectorSurfaceMaterialBridge(const VectorSurfaceMaterialBridge &) = delete;
    VectorSurfaceMaterialBridge &operator=(const VectorSurfaceMaterialBridge &) = delete;
    void Initialize(const VulkanRasterAccess &Access, RasterShader Shader, uint32_t Width, uint32_t Height);
    void Initialize(const VulkanRasterAccess &Access, const std::filesystem::path &ShaderArchive, uint32_t Width, uint32_t Height);
    bool ConfigureDocument(const VectorAuthoringDocument &Document,uint64_t CompletedSerial);
    bool ConstructCommands(VkCommandBuffer Commands,uint64_t CompletedSerial,uint64_t SubmissionSerial);
    void ConfirmSubmission() noexcept;
    [[nodiscard]] std::array<MaterialTexturePyramid,5> RetrieveAuthoringMaps(uint64_t CompletedSerial);
    void Finalize() noexcept;
private:
    std::array<VectorCoverageRasterization,5> Rasterizations;
    std::array<VectorSurfaceProgram,5> Programs;
    std::optional<std::array<MaterialTexturePyramid,5>> CompiledMaps;
    uint64_t LastSubmission = 0,PendingSubmission = 0;
    bool Initialized = false,ProgramsConfigured = false,Dirty = false,Recorded = false,ReadbacksPending = false;
};
}
