/*==========================================================================================================================================
                                         ILLUMINATION TELEMETRY JOURNAL DEFINITION
==========================================================================================================================================*/
// 🧩 Editor & Development ONLY (#if FRONTIER_EDITOR) structured CSV + human-readable GPU diagnostic telemetry journal.

#include "Engine/Diagnostics/IlluminationTelemetryJournal.h"

#if FRONTIER_EDITOR

#include <algorithm>
#include <cmath>
#include <ctime>

namespace Frontier
{
namespace
{

float PercentileSorted(const std::vector<float>& Sorted, float Quantile) noexcept
{
    if (Sorted.empty())
    {
        return 0.0f;
    }
    const float  Pos = std::clamp(Quantile, 0.0f, 1.0f) * static_cast<float>(Sorted.size() - 1u);
    const size_t Lo  = static_cast<size_t>(std::floor(Pos));
    const size_t Hi  = std::min(Sorted.size() - 1u, Lo + 1u);
    const float  T   = Pos - static_cast<float>(Lo);
    return Sorted[Lo] + (Sorted[Hi] - Sorted[Lo]) * T;
}

} // namespace

IlluminationTelemetryJournal::~IlluminationTelemetryJournal() noexcept
{
    CloseSession(nullptr);
}

bool IlluminationTelemetryJournal::OpenSession(const std::filesystem::path&      LogDirectoryPath,
                                               const VulkanComputeExchange&      Exchange,
                                               const SpatiotemporalIllumination& Renderer)
{
    CloseSession(nullptr);

    std::error_code ErrorCode;
    std::filesystem::create_directories(LogDirectoryPath, ErrorCode);

    CsvPath_   = LogDirectoryPath / "EverestPerformance.csv";
    LogPath_   = LogDirectoryPath / "EverestDiagnostics.log";
    CsvHandle_ = std::fopen(CsvPath_.string().c_str(), "w");
    LogHandle_ = std::fopen(LogPath_.string().c_str(), "w");
    if (CsvHandle_ == nullptr || LogHandle_ == nullptr)
    {
        CloseSession(nullptr);
        return false;
    }

    std::fprintf(CsvHandle_,
                 "Frame,ElapsedS,WidthPx,HeightPx,ScalePct,Renderer,Inspection,QualityTier,"
                 "RisM,SpatialK,GiRays,HistoryCap,AtrousPasses,"
                 "RadiosityMs,PrimaryMs,DirectRisMs,IndirectGiMs,SpatialReflMs,AtrousMs,TotalGpuMs,"
                 "CpuSubmitMs,WallFrameMs,CamX,CamY,CamZ,YawDeg,PitchDeg,"
                 "OctetBlocks,Triangles,SurfaceCards,LuminousFacets,VramMiB\n");
    std::fflush(CsvHandle_);

    const std::time_t NowTime = std::time(nullptr);
    char              TimeBuf[64]{};
    if (const std::tm* UtcTm = std::gmtime(&NowTime))
    {
        std::strftime(TimeBuf, sizeof(TimeBuf), "%Y-%m-%d %H:%M:%S UTC", UtcTm);
    }

    const uint32_t ApiVer = Exchange.ApiVersion();
    const uint32_t DrvVer = Exchange.DriverVersion();
    std::fprintf(LogHandle_,
                 "========================================================================================================\n"
                 "  PROJECT EVEREST — GTX-FIRST OPTION A VULKAN 1.3 COMPUTE DIAGNOSTICS JOURNAL\n"
                 "  Started           : %s\n"
                 "  Adapter           : %s (Vendor=0x%04X, Device=0x%04X)\n"
                 "  Vulkan API / Drv  : %u.%u.%u / 0x%08X\n"
                 "  Pipeline Archive  : Loaded=%llu B\n"
                 "========================================================================================================\n",
                 TimeBuf[0] != '\0' ? TimeBuf : "unknown",
                 Exchange.DeviceName().c_str(),
                 Exchange.VendorIdentifier(),
                 Exchange.DeviceIdentifier(),
                 (ApiVer >> 22u) & 0x7FFu,
                 (ApiVer >> 12u) & 0x3FFu,
                 ApiVer & 0xFFFu,
                 DrvVer,
                 static_cast<unsigned long long>(Exchange.QueryPipelineArchiveLoadedBytes()));

    std::fprintf(LogHandle_, "\n[Compute Pipeline Compilation Summary]\n");
    double TotalCompileMs = 0.0;
    for (const SpatiotemporalProgramCompileInfo& Stat : Renderer.ProgramCompileStats())
    {
        TotalCompileMs += static_cast<double>(Stat.ShaderModuleMilliseconds + Stat.PipelineCompileMilliseconds);
        std::fprintf(LogHandle_,
                     "  %-32s | SPIR-V=%6u B | FNV64=0x%016llX | Module=%6.2f ms | Pipeline=%7.2f ms\n",
                     Stat.EntryName.c_str(),
                     Stat.SpirvByteCount,
                     static_cast<unsigned long long>(Stat.SpirvFingerprintFnv64),
                     Stat.ShaderModuleMilliseconds,
                     Stat.PipelineCompileMilliseconds);
    }
    std::fprintf(LogHandle_, "  Total 6-Kernel Compile Time: %.2f ms\n\n", TotalCompileMs);
    std::fflush(LogHandle_);

    WindowGpuTotalsMs_.reserve(120u);
    WindowWallFramesMs_.reserve(120u);
    LoggedFrames_ = 0u;
    return true;
}

void IlluminationTelemetryJournal::RecordSceneUploadSummary(const SpatiotemporalFrameTelemetry& SceneSummary,
                                                            uint64_t                            DeviceVramBytes) noexcept
{
    if (LogHandle_ == nullptr)
    {
        return;
    }
    std::fprintf(LogHandle_,
                 "[Scene Upload Summary]\n"
                 "  BottomArchives=%u | TopInstances=%u | TopTlasCells=%u | Octet8WideBlocks=%u | Triangles=%u\n"
                 "  SurfaceCards=%u | LuminousFacets=%u | AuthoredTexLayers=%u | RangeClipmap=%s | VRAM=%.2f MiB\n\n",
                 SceneSummary.BottomArchiveCount,
                 SceneSummary.TopInstanceCount,
                 SceneSummary.BvhCellCount,
                 SceneSummary.OctetBlockCount,
                 SceneSummary.BvhTriangleCount,
                 SceneSummary.SurfacePatchCount,
                 SceneSummary.LuminousFacetCount,
                 SceneSummary.AuthoredTextureLayers,
                 SceneSummary.RangeClipmapActive ? "ON (4-Cascade)" : (SceneSummary.SdfVolumeActive ? "ON (.sdfspace)" : "OFF"),
                 static_cast<double>(DeviceVramBytes) / (1024.0 * 1024.0));
    std::fflush(LogHandle_);
}

void IlluminationTelemetryJournal::RecordFrameSample(const IlluminationFrameJournalSample& S) noexcept
{
    if (CsvHandle_ == nullptr)
    {
        return;
    }

    const double VramMiB = static_cast<double>(S.DeviceVramBytes) / (1024.0 * 1024.0);
    std::fprintf(CsvHandle_,
                 "%llu,%.4f,%u,%u,%u,%s,%u,%u,%u,%u,%u,%u,%u,"
                 "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
                 "%.3f,%.3f,%.3f,%.2f,%.2f,%u,%u,%u,%u,%.2f\n",
                 static_cast<unsigned long long>(LoggedFrames_),
                 S.ElapsedSeconds,
                 S.RenderWidthPx,
                 S.RenderHeightPx,
                 S.ResolutionPercent,
                 S.PreviewRasterUsed ? "PreviewRaster" : (S.Config.FullRaytracingMode ? "FullRaytracing" : "HybridSDF_RT"),
                 S.Config.InspectionMode,
                 S.QualityTier,
                 S.Config.RisCandidateCount,
                 S.Config.SpatialNeighbors,
                 S.Config.SdfIndirectRays,
                 S.Config.TemporalHistoryLimit,
                 S.Config.AtrousIterations,
                 S.Telemetry.RadiosityGpuMs,
                 S.Telemetry.PrimaryGpuMs,
                 S.Telemetry.DirectRisGpuMs,
                 S.Telemetry.IndirectGiGpuMs,
                 S.Telemetry.SpatialReflectGpuMs,
                 S.Telemetry.AtrousResolveGpuMs,
                 S.Telemetry.TotalGpuMs,
                 S.Telemetry.CpuSubmissionMs,
                 S.WallFrameMs,
                 S.CameraOrigin[0],
                 S.CameraOrigin[1],
                 S.CameraOrigin[2],
                 S.CameraYawDeg,
                 S.CameraPitchDeg,
                 S.Telemetry.OctetBlockCount,
                 S.Telemetry.BvhTriangleCount,
                 S.Telemetry.SurfacePatchCount,
                 S.Telemetry.LuminousFacetCount,
                 VramMiB);

    ++LoggedFrames_;
    if ((LoggedFrames_ % 30u) == 0u)
    {
        std::fflush(CsvHandle_);
    }

    WindowGpuTotalsMs_.push_back(S.Telemetry.TotalGpuMs);
    WindowWallFramesMs_.push_back(S.WallFrameMs);
    WindowRadSumMs_ += S.Telemetry.RadiosityGpuMs;
    WindowPrimSumMs_ += S.Telemetry.PrimaryGpuMs;
    WindowDirSumMs_ += S.Telemetry.DirectRisGpuMs;
    WindowIndSumMs_ += S.Telemetry.IndirectGiGpuMs;
    WindowSpatSumMs_ += S.Telemetry.SpatialReflectGpuMs;
    WindowAtrSumMs_ += S.Telemetry.AtrousResolveGpuMs;
    WindowCpuSumMs_ += S.Telemetry.CpuSubmissionMs;
    LastWidthPx_  = S.RenderWidthPx;
    LastHeightPx_ = S.RenderHeightPx;
    LastMode_     = S.Config.InspectionMode;
    LastCamPos_   = S.CameraOrigin;

    if (WindowGpuTotalsMs_.size() >= 60u)
    {
        EmitPeriodicRollup();
    }
}

void IlluminationTelemetryJournal::EmitPeriodicRollup() noexcept
{
    if (LogHandle_ == nullptr || WindowGpuTotalsMs_.empty())
    {
        return;
    }

    const float        InvN      = 1.0f / static_cast<float>(WindowGpuTotalsMs_.size());
    std::vector<float> SortedGpu = WindowGpuTotalsMs_;
    std::vector<float> SortedWall= WindowWallFramesMs_;
    std::sort(SortedGpu.begin(), SortedGpu.end());
    std::sort(SortedWall.begin(), SortedWall.end());

    const float MedWall = std::max(0.1f, PercentileSorted(SortedWall, 0.50f));
    std::fprintf(LogHandle_,
                 "[Rollup @ %6llu frames | %ux%u | Mode=%u | Cam=(%.1f, %.1f, %.1f)]\n"
                 "  GPU Total ms : min=%6.2f | p50=%6.2f | p95=%6.2f | p99=%6.2f | max=%6.2f\n"
                 "  Kernels Avg  : Rad=%.2f | Prim=%.2f | DI=%.2f | GI=%.2f | SpatRefl=%.2f | Atrous=%.2f | CpuSub=%.2f\n"
                 "  Wall Frame   : p50=%6.2f ms (%.1f FPS) | p95=%6.2f ms | p99=%6.2f ms\n",
                 static_cast<unsigned long long>(LoggedFrames_),
                 LastWidthPx_,
                 LastHeightPx_,
                 LastMode_,
                 LastCamPos_[0],
                 LastCamPos_[1],
                 LastCamPos_[2],
                 SortedGpu.front(),
                 PercentileSorted(SortedGpu, 0.50f),
                 PercentileSorted(SortedGpu, 0.95f),
                 PercentileSorted(SortedGpu, 0.99f),
                 SortedGpu.back(),
                 WindowRadSumMs_ * InvN,
                 WindowPrimSumMs_ * InvN,
                 WindowDirSumMs_ * InvN,
                 WindowIndSumMs_ * InvN,
                 WindowSpatSumMs_ * InvN,
                 WindowAtrSumMs_ * InvN,
                 WindowCpuSumMs_ * InvN,
                 MedWall,
                 1000.0f / MedWall,
                 PercentileSorted(SortedWall, 0.95f),
                 PercentileSorted(SortedWall, 0.99f));
    std::fflush(LogHandle_);

    WindowGpuTotalsMs_.clear();
    WindowWallFramesMs_.clear();
    WindowRadSumMs_  = 0.0f;
    WindowPrimSumMs_ = 0.0f;
    WindowDirSumMs_  = 0.0f;
    WindowIndSumMs_  = 0.0f;
    WindowSpatSumMs_ = 0.0f;
    WindowAtrSumMs_  = 0.0f;
    WindowCpuSumMs_  = 0.0f;
}

void IlluminationTelemetryJournal::CloseSession(const VulkanComputeExchange* OptionalExchange) noexcept
{
    if (!WindowGpuTotalsMs_.empty())
    {
        EmitPeriodicRollup();
    }
    if (LogHandle_ != nullptr)
    {
        if (OptionalExchange != nullptr)
        {
            std::fprintf(LogHandle_,
                         "\n[Session Closed] TotalFrames=%llu | PipelineArchiveSaved=%llu B\n",
                         static_cast<unsigned long long>(LoggedFrames_),
                         static_cast<unsigned long long>(OptionalExchange->QueryPipelineArchiveSavedBytes()));
        }
        else
        {
            std::fprintf(LogHandle_, "\n[Session Closed] TotalFrames=%llu\n", static_cast<unsigned long long>(LoggedFrames_));
        }
        std::fclose(LogHandle_);
        LogHandle_ = nullptr;
    }
    if (CsvHandle_ != nullptr)
    {
        std::fflush(CsvHandle_);
        std::fclose(CsvHandle_);
        CsvHandle_ = nullptr;
    }
}

} // namespace Frontier

#endif // FRONTIER_EDITOR
