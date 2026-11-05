/*==========================================================================================================================================
                                                        NEAREST DEPTH SPACE
                                     ------------------------------------------------------------------
                   The nearest-depth pyramid over the screen — recursively halved tiles, each
                     holding its nearest depth — kept two deep (this frame and the last).
==========================================================================================================================================*/

// 🧩 The nearest-depth pyramid: the portable (CPU) form of the demo's NearestDepthSpace.js, 1:1.
//
// 📝 Rebuilt from the depth attachment every frame into the slot of the frame's parity, so the other
//    slot still holds the previous frame's pyramid: the screen traces walk the current one and confirm
//    reprojections against the prior one. Level extents follow max(1, ⌊extent / 2^L⌋) down to a single
//    texel, the chain WebGPU itself defines.
//
// 📝 Reversed depth stores larger values nearer the eye, so "nearest" is a maximum, and the clear value
//    0 (nothing rastered) never wins over a surface. A level-L texel therefore bounds every pixel of
//    its 2^L × 2^L tile from in front: a ray in front of that value is in front of the whole tile and
//    may cross it in a single step.
//
//    The Reduce() below is the CPU mirror of NearestDepthPyramid.slang; it exists so host validation can
//    prove the pyramid bounds what the shader claims it bounds, without a device.

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace Frontier
{

class NearestDepthSpace
{
  public:
    // 📝 Rebuilds the level table for a screen extent. LevelCount = ⌊log2(max(W, H))⌋ + 1.
    void Configure(uint32_t Width, uint32_t Height);

    [[nodiscard]] uint32_t LevelCount() const noexcept { return Levels; }
    [[nodiscard]] std::array<uint32_t, 2> LevelExtent(uint32_t Level) const noexcept;

    // 📝 The dispatch of the level-0 transfer (one workgroup per 8 × 8 texels).
    [[nodiscard]] std::array<uint32_t, 3> TransferDispatch() const noexcept;
    // 📝 The dispatch of the reduction that builds Level from Level − 1.
    [[nodiscard]] std::array<uint32_t, 3> ReductionDispatch(uint32_t Level) const noexcept;

    // 📝 The slot the next frame writes (0 or 1); the other slot holds the previous frame's pyramid.
    [[nodiscard]] uint32_t Parity() const noexcept { return Slot; }
    void AdvanceFrame() noexcept { Slot = 1u - Slot; }

    uint32_t Width = 0;
    uint32_t Height = 0;

    // 📝 Builds the whole pyramid on the host from a reversed-depth buffer (Width × Height, row-major,
    //    0 = nothing rastered). Levels[0] is the depth itself; Levels[L] is the max of each 2^L tile.
    //    Returns one flat array per level, in level order — the exact values the Slang chain produces.
    [[nodiscard]] static std::vector<std::vector<float>> Reduce(const float* ReversedDepth, uint32_t Width, uint32_t Height);

    // 📝 The invariant the screen walk depends on: a level-L texel is ≥ every depth in its tile, and
    //    every pixel of the source is covered by exactly one tile of that level.
    [[nodiscard]] static bool VerifyBounds(const std::vector<std::vector<float>>& Pyramid,
                                           uint32_t Width,
                                           uint32_t Height);

  private:
    uint32_t Levels = 0;
    uint32_t Slot = 0;
    std::vector<std::array<uint32_t, 2>> Extents;
};

} // namespace Frontier
