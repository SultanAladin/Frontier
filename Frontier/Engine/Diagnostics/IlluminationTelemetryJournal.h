/*==========================================================================================================================================
                                             ILLUMINATION TELEMETRY JOURNAL
                                     ------------------------------------------------------------------
             Editor & Development ONLY (#if FRONTIER_EDITOR) GPU compute & viewport performance telemetry journal.
             Strictly compiled out under FRONTIER_SHIPPING_PROFILE (FRONTIER_EDITOR == 0) with zero Shipping symbols.
==========================================================================================================================================*/
// 🧩 Writes Saved/Logs/EverestDiagnostics.log (adapter, VkPipelineCache, SPIR-V compile ms, p50/p95/p99 rollups)
//    and Saved/Logs/EverestPerformance.csv (per-frame kernel GPU timings, resolution, quality settings, camera pose).

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"

#if FRONTIER_EDITOR

#include "Engine/DeviceExchange/VulkanComputeExchange.h"
#include "Engine/Rendering/SpatiotemporalIllumination.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace Frontier
{

struct IlluminationFrameJournalSample
{
    double                       ElapsedSeconds    = 0.0;
    uint32_t                     RenderWidthPx     = 0u;
    uint32_t                     RenderHeightPx    = 0u;
    uint32_t                     ResolutionPercent = 100u;
    bool                         PreviewRasterUsed = false;
    uint32_t                     QualityTier       = 2u;
    float                        WallFrameMs       = 16.6f;
    std::array<float, 3>         CameraOrigin{0.0f, 0.0f, 0.0f};
    float                        CameraYawDeg      = 0.0f;
    float                        CameraPitchDeg    = 0.0f;
    uint64_t                     DeviceVramBytes   = 0u;
    SpatiotemporalRenderConfig   Config{};
    SpatiotemporalFrameTelemetry Telemetry{};
};

class IlluminationTelemetryJournal
{
  public:
    IlluminationTelemetryJournal() = default;
    ~IlluminationTelemetryJournal() noexcept;

    IlluminationTelemetryJournal(const IlluminationTelemetryJournal&)            = delete;
    IlluminationTelemetryJournal& operator=(const IlluminationTelemetryJournal&) = delete;

    bool OpenSession(const std::filesystem::path& LogDirectoryPath,
                     const VulkanComputeExchange& Exchange,
                     const SpatiotemporalIllumination& Renderer);

    void RecordSceneUploadSummary(const SpatiotemporalFrameTelemetry& SceneSummary,
                                  uint64_t                            DeviceVramBytes) noexcept;

    void RecordFrameSample(const IlluminationFrameJournalSample& Sample) noexcept;

    void CloseSession(const VulkanComputeExchange* OptionalExchange = nullptr) noexcept;

    [[nodiscard]] bool                         IsOpen() const noexcept { return CsvHandle_ != nullptr || LogHandle_ != nullptr; }
    [[nodiscard]] const std::filesystem::path& CsvFilePath() const noexcept { return CsvPath_; }
    [[nodiscard]] const std::filesystem::path& LogFilePath() const noexcept { return LogPath_; }
    [[nodiscard]] uint64_t                     LoggedFrameCount() const noexcept { return LoggedFrames_; }

  private:
    void EmitPeriodicRollup() noexcept;

    std::FILE*            CsvHandle_    = nullptr;
    std::FILE*            LogHandle_    = nullptr;
    std::filesystem::path CsvPath_{};
    std::filesystem::path LogPath_{};
    uint64_t              LoggedFrames_ = 0u;

    std::vector<float> WindowGpuTotalsMs_{};
    std::vector<float> WindowWallFramesMs_{};
    float              WindowRadSumMs_   = 0.0f;
    float              WindowPrimSumMs_  = 0.0f;
    float              WindowDirSumMs_   = 0.0f;
    float              WindowIndSumMs_   = 0.0f;
    float              WindowSpatSumMs_  = 0.0f;
    float              WindowAtrSumMs_   = 0.0f;
    float              WindowCpuSumMs_   = 0.0f;
    uint32_t           LastWidthPx_      = 0u;
    uint32_t           LastHeightPx_     = 0u;
    uint32_t           LastMode_         = 0u;
    std::array<float, 3> LastCamPos_{0.0f, 0.0f, 0.0f};
};

} // namespace Frontier

#endif // FRONTIER_EDITOR
