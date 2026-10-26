/*==========================================================================================================================================
                                                           MESHBAKEGPUMIRROR.H
==========================================================================================================================================*/
// 🧩 Vulkan-free C++ transcription of Engine/Render/Shader/MeshBakeCompute.slang, structured line for line the
// same way (same helper function names, same flat-buffer indexing, same per-channel gating) so the two can be
// read side by side. This exists purely to let the GPU kernel be regression-tested on a machine with no Vulkan
// driver at all: Exhibits/Renderer/Code/MeshBakeGpuMirrorValidation.cpp runs this against the same
// MeshBakeGpuPlan the real Vulkan dispatch would consume and diffs it, texel for texel, against
// Engine/ContentInterchange/BakeMesh.cpp's trusted RunMeshBake CPU reference. A pass here does not prove the
// compiled SPIR-V executes correctly on an actual GPU (nothing can prove that without one) - it proves the
// *algorithm* this shader encodes, and the plan-building that feeds it, are numerically faithful to the
// reference. The shader itself was separately compiled to valid SPIR-V (see MeshBakeGpu.h's header comment).

#pragma once
#include "Engine/Render/MeshBakeGpu.h"

namespace Frontier
{
// 📝 Resolution/channels/geometry all come from Plan itself (see BuildMeshBakeGpuPlan) - this function takes
// no other input, exactly like the compute shader only reads its bound buffers.
[[nodiscard]] std::vector<BakeMapImage> RunMeshBakeGpuMirror(const MeshBakeGpuPlan &Plan);
} // namespace Frontier
