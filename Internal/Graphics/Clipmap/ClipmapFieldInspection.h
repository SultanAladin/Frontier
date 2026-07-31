/*==============================================================================================================================================
                                                         CLIPMAPFIELDINSPECTION.H
==============================================================================================================================================*/
// 🧩 Development-only GPU visualization of a ToroidalClipmapField: the world cell lattice drawn as instanced wire-cubes (resident cells one
//    colour, vacant cells another, per-level tint so the near/far resolution ladder reads at a glance), cells holding scene geometry drawn
//    highlighted, and the per-cell relight-ramp STUB drawn as point markers that brighten as a scrolled-in cell relights. One host-visible
//    record buffer is filled from the field each frame and both pipelines instance off it — the caller never issues a per-cell draw. The whole
//    unit compiles only under FRONTIER_DEVELOPMENT_PROFILE; a shipping build carries none of it.
// 📝 The substrate's colour scope has NO depth attachment, so these cells composite as an X-RAY overlay (cells behind geometry stay visible).
//    That is the wanted behaviour for inspecting residency — a depth-tested lattice would hide exactly the cells worth checking.

#pragma once
#ifndef FRONTIER_GRAPHICS_CLIPMAP_CLIPMAPFIELDINSPECTION_H
#define FRONTIER_GRAPHICS_CLIPMAP_CLIPMAPFIELDINSPECTION_H

// 📝 Included FIRST and OUTSIDE the profile gate: this header is what RESOLVES the build profile, defaulting to development when the build
//    system names neither. Gating on FRONTIER_DEVELOPMENT_PROFILE before it is included would test a macro nothing has defined yet, and the
//    whole unit would silently compile away to nothing even in a development build.
#include "Graphics/RenderExtension/Diagnostics/DiagnosticArchive.h"

#ifdef FRONTIER_DEVELOPMENT_PROFILE

#include "Graphics/RenderExtension/Device/VulkanHost.h"
#include "EngineContext/Math/LinearAlgebra_Float32.h"
#include "EngineContext/SpatialAcceleration/ToroidalClipmapField.h"

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Upper bound on cells submitted in one frame. A full 32³ level is 32768 cells and four levels would be 131072 — far more lattice than is
//    readable on screen and a needless vertex load. The filler emits only the cells that carry information (occupied, or a thin shell around
//    the camera) and stops at this cap, reporting the drop count so a silent truncation never reads as full coverage.
constexpr uint32_t ClipmapInspectionCellCapacity = 20000;

// Cell display category — drives the colour picked in the shader. Occupied outranks resident, resident outranks vacant.
enum class ClipmapCellCategory : uint32_t
{
    Vacant   = 0,   // scrolled in, awaiting fill
    Resident = 1,   // cached and valid
    Occupied = 2,   // resident AND overlapping scene geometry
};

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One cell handed to the GPU, std430-compatible (two vec4-sized rows, 32 bytes). CentreMetres is the cell's world centre; EdgeMetres its
//    world edge length at that level. Category selects the colour; LevelIndex tints it; RelightRamp drives the probe marker's brightness.
struct ClipmapCellRecord
{
    float    CentreMetres[3] = { 0.0f, 0.0f, 0.0f };   // [m] - world centre of the cell
    float    EdgeMetres      = 0.0f;                   // [m] - world edge length (level cell size)
    uint32_t Category        = 0;                      // [-] - ClipmapCellCategory
    uint32_t LevelIndex      = 0;                      // [-] - which clipmap level this cell belongs to
    float    RelightRamp     = 0.0f;                   // [-] - 0..1 stub payload; drives probe-marker brightness
    float    Reserved        = 0.0f;                   // [-] - std430 pad to 32 bytes
};

// 📝 Per-frame push data shared by both pipelines. Byte-compatible with the ClipmapInspectionConstants block in the debug shaders: one mat4
//    then eight tightly packed floats (64 + 32 = 96 bytes, inside the 256-byte Pascal push limit). The lattice draws each cube edge as a
//    screen-space quad (no wideLines device feature needed), so it needs the viewport size to turn a pixel width into an NDC offset; the
//    per-level width scaling lives in the shader. ViewportPixels is refreshed each frame in RecordClipmapInspection from the colour extent.
struct ClipmapInspectionConstants
{
    Matrix4f ViewProjection;                 // [-]  - world → clip
    float    LineOpacity      = 0.75f;       // [-]  - wire-cube alpha
    float    ProbePixelRadius = 3.0f;        // [px] - probe marker radius in screen pixels
    float    ProbeOpacity     = 0.95f;       // [-]  - probe marker alpha
    float    VacantOpacity    = 0.35f;       // [-]  - extra dimming applied to vacant cells
    float    LatticeBaseWidth = 1.2f;        // [px] - level-0 (camera grid) edge thickness; the occupied cage scales up (shader table)
    float    ViewportWidth    = 1280.0f;     // [px] - colour-target width  (turns a pixel width → NDC x offset)
    float    ViewportHeight   = 720.0f;      // [px] - colour-target height (turns a pixel width → NDC y offset)
    float    ConstantsPad0    = 0.0f;        // [-]  - keep the tail float count even
};

// 📝 The inspection unit's device resources, built once. Two pipelines (wire-cube lattice + probe markers) share one push layout, one
//    descriptor layout, and one host-visible record buffer. Holds no per-field state — any ToroidalClipmapField can be handed to
//    RefreshClipmapInspection, so one instance serves every field and every frame.
struct ClipmapFieldInspection
{
    const VulkanHost*     Host                 = nullptr;          // [-] - not owned; supplies device / physical device / allocator

    VkBuffer              CellBuffer           = VK_NULL_HANDLE;   // [-] - host-visible SSBO of ClipmapCellRecord
    VkDeviceMemory        CellMemory           = VK_NULL_HANDLE;   // [-] - backing allocation, persistently mapped
    void*                 CellMapping          = nullptr;          // [-] - persistent map pointer for the per-frame refill

    VkDescriptorSetLayout DescriptorLayout     = VK_NULL_HANDLE;   // [-] - { storage buffer: the cell records }
    VkDescriptorPool      DescriptorPool       = VK_NULL_HANDLE;   // [-] - one set
    VkDescriptorSet       CellSet              = VK_NULL_HANDLE;   // [-] - points at CellBuffer
    VkPipelineLayout      PipelineLayout       = VK_NULL_HANDLE;   // [-] - descriptor layout + ClipmapInspectionConstants push range
    VkPipeline            LatticePipeline      = VK_NULL_HANDLE;   // [-] - instanced wire-cube line list
    VkPipeline            ProbePipeline        = VK_NULL_HANDLE;   // [-] - instanced point-sprite probe markers

    uint32_t              CellCount            = 0;                // [-] - records filled this frame (instance count)
    uint32_t              DroppedCellCount     = 0;                // [-] - cells the capacity cap refused this frame
    uint32_t              RemoteCellCount      = 0;                // [-] - occupied cells the display radius withheld (policy, not a shortfall)
    bool                  ReadyCondition       = false;            // [-] - true once both pipelines built
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Build both pipelines, the record buffer, and the descriptor set for the given colour attachment format. Loads the four SPIR-V modules from
// ShaderDirectory. Returns false (ReadyCondition left false) when dynamic rendering is absent or a module is missing; the caller then simply
// skips the inspection draw.
bool InitializeClipmapFieldInspection(ClipmapFieldInspection& Inspection,
                                      const VulkanHost&       Host,
                                      VkFormat                ColourFormat,
                                      const char*             ShaderDirectory);

// Walk the field and refill the record buffer for this frame. Emits the OCCUPIED cells (those in OccupiedCells, which the caller derives from its
// scene instances) that lie within OccupiedRadiusCells of the camera cell ON THE OCCUPANCY LEVEL, plus resident and vacant cells within
// ShellRadiusCells of the camera cell on the fine level, so the lattice stays readable instead of drowning the view.
//
// 📝 Both radii are Chebyshev cell counts, and they are SEPARATE because they measure in different cell sizes and answer different questions:
//    ShellRadiusCells sizes the reference grid that follows the camera on level 0, while OccupiedRadiusCells bounds how much scene surface is
//    cage-drawn at the (coarser) occupancy level. That second bound is a frame-cost control — see the note at the occupied loop.
//
// Sets CellCount / DroppedCellCount / RemoteCellCount. A no-op when the unit is not ready.
void RefreshClipmapInspection(ClipmapFieldInspection&              Inspection,
                              const ToroidalClipmapField&          Field,
                              Vector3f                             CameraPosition,
                              const std::vector<CellCoordinate>&    OccupiedCells,
                              uint32_t                             OccupiedLevel,
                              int32_t                              ShellRadiusCells,
                              int32_t                              OccupiedRadiusCells);

// Record the lattice + probe draws into an already-open dynamic-rendering colour scope. Binds each pipeline, pushes the constants, and issues
// one instanced draw per visualization (72 triangle vertices per cell for the screen-space-quad wire-cube; 1 point per cell for the probe
// marker). A no-op when the unit is not ready or no cells were filled this frame.
void RecordClipmapInspection(const ClipmapFieldInspection&    Inspection,
                             VkCommandBuffer                  CommandBuffer,
                             VkExtent2D                       Extent,
                             const ClipmapInspectionConstants& Constants);

// Destroy both pipelines, the layouts, the descriptor pool, and the record buffer. Safe on partially-initialized state.
void FinalizeClipmapFieldInspection(ClipmapFieldInspection& Inspection);

} // namespace Frontier

#endif // FRONTIER_DEVELOPMENT_PROFILE

#endif
