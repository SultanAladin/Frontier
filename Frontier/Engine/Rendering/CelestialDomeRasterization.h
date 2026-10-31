/*==========================================================================================================================================
                                              CELESTIAL DOME RASTERIZATION
                                     ------------------------------------------------------------------
             GPU sky-view + 6-pt twilight coupling, 2048x1024x6 sRGB 6-body lunar sphere atlas,
             32x32 octahedral Hipparcos star index + scintillation + Milky Way, ringed planets, and suns.
==========================================================================================================================================*/
// 🧩 Unit 2 of 5: Celestial Dome GPU compute rasterization across all active realms and portals.

#pragma once

#include "Engine/Rendering/AtmosphereLutSubmission.h"
#include "Engine/Rendering/CelestialDomeModel.h"
#include "Engine/Rendering/VulkanComputeSubstrate.h"

#include <cstdint>
#include <string>

namespace Frontier
{

class CelestialDomeRasterization
{
  public:
    bool Initialize(VulkanComputeSubstrate& Substrate,
                    const std::string&      ShaderBinaryDirectory,
                    const std::string&      LunaSourceJpgPath,
                    const std::string&      HipparcosCsvPath,
                    std::string*            OutDiagnostic = nullptr);
    void Shutdown(VulkanComputeSubstrate& Substrate) noexcept;

    bool Execute(VulkanComputeSubstrate&        Substrate,
                 const VulkanBufferSlot&        EnsembleBuffer,
                 const AtmosphereLutSubmission& AtmosphereLuts,
                 const VulkanImageSlot&         GbufferArray,
                 VulkanImageSlot&               HdrFrameTarget,
                 uint32_t                       ViewportWidth,
                 uint32_t                       ViewportHeight);

    [[nodiscard]] uint32_t                  IndexedStarCount() const noexcept { return IndexedStarCount_; }
    [[nodiscard]] const CelestialDomeModel& HostDomeModel() const noexcept { return HostDomeModel_; }

  private:
    VulkanComputeProgram Program_{};
    VulkanImageSlot      LunarAtlasImage_{};
    VulkanBufferSlot     StarCellBuffer_{};
    VulkanBufferSlot     StarRecordBuffer_{};
    CelestialDomeModel   HostDomeModel_{};
    uint32_t             IndexedStarCount_ = 0u;
};

} // namespace Frontier
