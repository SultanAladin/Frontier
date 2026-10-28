/*==========================================================================================================================================
                                                          SCENE INSPECTOR PANEL CAPTURE
                                     ------------------------------------------------------------------
                              Renders the production C++ panel with a CPU rasterizer so its real pixels can be
==========================================================================================================================================*/

// 🧩 Log Stream panel capture: renders the production C++ panel with a CPU rasterizer so its real pixels can be
//    inspected without a GPU or window. Every widget below is drawn by LogStreamPanelPresentation.cpp itself —
//    this harness only supplies what the Windows host supplies (an ImGui context, a font, a window frame standing
//    in for the dock node, and input events). The dockspace/notch chrome belongs to the host and is not faked here.

#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Engine/Interface/SceneInspectorPanel.h"
#include "Engine/Scene/SceneDirectory.h"
#include "Engine/Scene/SceneRecord.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   CPU RASTERIZER
//------------------------------------------------------------------------------------------------------------------------

int CaptureWidth  = 380;  // [px] - HTML #inspector column width (grid-template-columns: 288px 1fr 380px)
int CaptureHeight = 800;  // [px]

struct Pixel
{
    uint8_t Red, Green, Blue, Alpha;
};

std::vector<Pixel> Framebuffer;

const unsigned char* AtlasPixels = nullptr;
int                  AtlasWidth  = 0;
int                  AtlasHeight = 0;

bool SampleWhiteTexel = false; // Commands bound to non-atlas textures (GPU thumbnails) render as flat colour

float SampleAtlas(float U, float V, int Channel)
{
    const float X = std::clamp(U * static_cast<float>(AtlasWidth) - 0.5f, 0.0f, static_cast<float>(AtlasWidth - 1));
    const float Y = std::clamp(V * static_cast<float>(AtlasHeight) - 0.5f, 0.0f, static_cast<float>(AtlasHeight - 1));
    const int   X0 = static_cast<int>(X);
    const int   Y0 = static_cast<int>(Y);
    const int   X1 = std::min(X0 + 1, AtlasWidth - 1);
    const int   Y1 = std::min(Y0 + 1, AtlasHeight - 1);
    const float Fx = X - static_cast<float>(X0);
    const float Fy = Y - static_cast<float>(Y0);
    const auto  At = [&](int Px, int Py) {
        return static_cast<float>(AtlasPixels[(static_cast<size_t>(Py) * AtlasWidth + Px) * 4 + Channel]) / 255.0f;
    };
    const float Top    = At(X0, Y0) + (At(X1, Y0) - At(X0, Y0)) * Fx;
    const float Bottom = At(X0, Y1) + (At(X1, Y1) - At(X0, Y1)) * Fx;
    return Top + (Bottom - Top) * Fy;
}

void RasterizeTriangle(const ImDrawVert& A, const ImDrawVert& B, const ImDrawVert& C, const ImVec4& Clip)
{
    const float Area = (B.pos.x - A.pos.x) * (C.pos.y - A.pos.y) - (B.pos.y - A.pos.y) * (C.pos.x - A.pos.x);
    if (Area == 0.0f)
    {
        return;
    }
    const int MinX = std::max({static_cast<int>(std::floor(std::min({A.pos.x, B.pos.x, C.pos.x}))),
                               static_cast<int>(Clip.x), 0});
    const int MinY = std::max({static_cast<int>(std::floor(std::min({A.pos.y, B.pos.y, C.pos.y}))),
                               static_cast<int>(Clip.y), 0});
    const int MaxX = std::min({static_cast<int>(std::ceil(std::max({A.pos.x, B.pos.x, C.pos.x}))),
                               static_cast<int>(Clip.z), CaptureWidth});
    const int MaxY = std::min({static_cast<int>(std::ceil(std::max({A.pos.y, B.pos.y, C.pos.y}))),
                               static_cast<int>(Clip.w), CaptureHeight});

    for (int Y = MinY; Y < MaxY; ++Y)
    {
        for (int X = MinX; X < MaxX; ++X)
        {
            const float Px = static_cast<float>(X) + 0.5f;
            const float Py = static_cast<float>(Y) + 0.5f;
            const float W0 = ((B.pos.x - Px) * (C.pos.y - Py) - (B.pos.y - Py) * (C.pos.x - Px)) / Area;
            const float W1 = ((C.pos.x - Px) * (A.pos.y - Py) - (C.pos.y - Py) * (A.pos.x - Px)) / Area;
            const float W2 = 1.0f - W0 - W1;
            if (W0 < 0.0f || W1 < 0.0f || W2 < 0.0f)
            {
                continue;
            }
            const float U = A.uv.x * W0 + B.uv.x * W1 + C.uv.x * W2;
            const float V = A.uv.y * W0 + B.uv.y * W1 + C.uv.y * W2;

            const auto Channel = [](ImU32 Colour, int Shift) {
                return static_cast<float>((Colour >> Shift) & 0xffu) / 255.0f;
            };
            const float Vr = Channel(A.col, IM_COL32_R_SHIFT) * W0 + Channel(B.col, IM_COL32_R_SHIFT) * W1 +
                             Channel(C.col, IM_COL32_R_SHIFT) * W2;
            const float Vg = Channel(A.col, IM_COL32_G_SHIFT) * W0 + Channel(B.col, IM_COL32_G_SHIFT) * W1 +
                             Channel(C.col, IM_COL32_G_SHIFT) * W2;
            const float Vb = Channel(A.col, IM_COL32_B_SHIFT) * W0 + Channel(B.col, IM_COL32_B_SHIFT) * W1 +
                             Channel(C.col, IM_COL32_B_SHIFT) * W2;
            const float Va = Channel(A.col, IM_COL32_A_SHIFT) * W0 + Channel(B.col, IM_COL32_A_SHIFT) * W1 +
                             Channel(C.col, IM_COL32_A_SHIFT) * W2;

            const float Sr = (SampleWhiteTexel ? 1.0f : SampleAtlas(U, V, 0)) * Vr;
            const float Sg = (SampleWhiteTexel ? 1.0f : SampleAtlas(U, V, 1)) * Vg;
            const float Sb = (SampleWhiteTexel ? 1.0f : SampleAtlas(U, V, 2)) * Vb;
            const float Sa = (SampleWhiteTexel ? 1.0f : SampleAtlas(U, V, 3)) * Va;

            Pixel&      Out = Framebuffer[static_cast<size_t>(Y) * CaptureWidth + X];
            const float Inv = 1.0f - Sa;
            Out.Red   = static_cast<uint8_t>(std::clamp(Sr * Sa + (Out.Red / 255.0f) * Inv, 0.0f, 1.0f) * 255.0f);
            Out.Green = static_cast<uint8_t>(std::clamp(Sg * Sa + (Out.Green / 255.0f) * Inv, 0.0f, 1.0f) * 255.0f);
            Out.Blue  = static_cast<uint8_t>(std::clamp(Sb * Sa + (Out.Blue / 255.0f) * Inv, 0.0f, 1.0f) * 255.0f);
            Out.Alpha = 255;
        }
    }
}

void RasterizeDrawData(const ImDrawData& Data)
{
    for (int ListIndex = 0; ListIndex < Data.CmdListsCount; ++ListIndex)
    {
        const ImDrawList* List = Data.CmdLists[ListIndex];
        for (int CmdIndex = 0; CmdIndex < List->CmdBuffer.Size; ++CmdIndex)
        {
            const ImDrawCmd& Cmd = List->CmdBuffer[CmdIndex];
            if (Cmd.UserCallback != nullptr)
            {
                continue;
            }
            SampleWhiteTexel = Cmd.TexRef._TexData == nullptr; // Atlas-owned commands carry texture data
            for (unsigned Element = 0; Element + 2 < Cmd.ElemCount; Element += 3)
            {
                const ImDrawIdx I0 = List->IdxBuffer[static_cast<int>(Cmd.IdxOffset + Element + 0)];
                const ImDrawIdx I1 = List->IdxBuffer[static_cast<int>(Cmd.IdxOffset + Element + 1)];
                const ImDrawIdx I2 = List->IdxBuffer[static_cast<int>(Cmd.IdxOffset + Element + 2)];
                RasterizeTriangle(List->VtxBuffer[static_cast<int>(Cmd.VtxOffset) + I0],
                                  List->VtxBuffer[static_cast<int>(Cmd.VtxOffset) + I1],
                                  List->VtxBuffer[static_cast<int>(Cmd.VtxOffset) + I2], Cmd.ClipRect);
            }
        }
    }
}

bool WritePpm(const char* Path)
{
    std::FILE* File = std::fopen(Path, "wb");
    if (File == nullptr)
    {
        return false;
    }
    std::fprintf(File, "P6\n%d %d\n255\n", CaptureWidth, CaptureHeight);
    for (const Pixel& Value : Framebuffer)
    {
        const uint8_t Rgb[3] = {Value.Red, Value.Green, Value.Blue};
        std::fwrite(Rgb, 1, 3, File);
    }
    std::fclose(File);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     FRAME DRIVER
//------------------------------------------------------------------------------------------------------------------------

} // namespace

// The HTML "Render settings" entity (InspectorPages.js BuildRender, IlluminationHost defaults) declared
// the native way: its constructor builds the Inspector specification under the editor profile.
class RenderSettingsRecord final : public Frontier::RecordEntry
{
  public:
    RenderSettingsRecord() : RecordEntry(Frontier::RecordClassification::Illumination)
    {
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
        DeclareEditorIdentity("Renderer", "Render");
        DeclareEditorDescription("RTX ReSTIR Path Tracer, resolution, reflections & debug views");
        DeclareEditorToggle("render.raytracing", "Raytracing", "", true,
                            [this]() { return RayTracing; }, [this](bool On) { RayTracing = On; }, "Zap");
        DeclareEditorToggle("render.indirect", "Indirect", "", true,
                            [this]() { return Indirect; }, [this](bool On) { Indirect = On; }, "Sparkles");
        DeclareEditorToggle("render.reflections", "Reflections", "", true,
                            [this]() { return Reflection; }, [this](bool On) { Reflection = On; }, "Aperture");
        DeclareEditorToggle("render.denoiser", "Denoiser", "", true,
                            [this]() { return VarianceKernel; }, [this](bool On) { VarianceKernel = On; }, "Layers");
        DeclareEditorToggle("render.taa", "TAA", "", true,
                            [this]() { return Antialias; }, [this](bool On) { Antialias = On; }, "Scan");
        DeclareEditorCard("Clustered geometry", "", -1, "Layers");
        DeclareEditorToggle("render.clusters", "GPU cluster frustum culling", "", false,
                            [this]() { return Clusters; }, [this](bool On) { Clusters = On; });
        DeclareEditorMuted("Lossless 128-triangle spatial clusters for eligible static meshes. GPU-written indirect draws; dynamic/composite geometry retains its established path. No LOD simplification or streaming yet.");
        DeclareEditorStats("render.clusterStats", {{"Cluster candidates", "", []() { return std::string("0"); }},
                                                   {"Index payload (KiB)", "", []() { return std::string("0.0"); }}});
        DeclareEditorCard("RTX \xC2\xB7 ReSTIR Path Tracer", "", 1, "Zap");
        DeclareEditorMetric("render.metric", [this]() { return std::to_string(MaxBounces) + "B GI \xC2\xB7 " + std::to_string(ReflectionBounces) + "B Refl \xC2\xB7 M=" + std::to_string(InitialCandidates); }, "",
                            [this]() { return std::pair<std::string, std::string>{RayTracing ? "RTX raytracing" : "GTX distance field", "live"}; });
        DeclareEditorMuted("Two-level BVH (TLAS over scene occupants + Binned-SAH BLAS per mesh) with multi-bounce BSDF importance sampling, RIS Next-Event Estimation, MIS, and Russian Roulette.");
        DeclareEditorSegmented("render.arch", "Renderer architecture", {"RTX \xC2\xB7 ReSTIR Path Tracer", "GTX \xC2\xB7 Distance-Field GI"},
                               [this]() { return RayTracing ? size_t(0) : size_t(1); },
                               [this](size_t Index) { RayTracing = Index == 0; });
        DeclareEditorSlider("render.bounces", "GI path bounces", "", 1, 4, 1,
                            [this]() { return static_cast<double>(MaxBounces); }, [this](double V) { MaxBounces = static_cast<int>(V); }, 0, "1 bounce", "4 bounces");
        DeclareEditorSlider("render.reflBounces", "Reflection bounces", "", 1, 4, 1,
                            [this]() { return static_cast<double>(ReflectionBounces); }, [this](double V) { ReflectionBounces = static_cast<int>(V); }, 0, "1 bounce", "4 bounces");
        DeclareEditorSlider("render.candidates", "Initial RIS path candidates", "", 1, 4, 1,
                            [this]() { return static_cast<double>(InitialCandidates); }, [this](double V) { InitialCandidates = static_cast<int>(V); }, 0, "Fast", "Low variance");
        DeclareEditorToggle("render.rtShadows", "Ray-traced direct & IES shadows", "Exact BVH visibility for sun cone, street lamps & headlights", false,
                            [this]() { return RayTracedShadows; }, [this](bool On) { RayTracedShadows = On; });
        DeclareEditorToggle("render.rtReflections", "Ray-traced multi-bounce reflections", "GGX VNDF & mirror BVH reflections across car paint, glass & wet asphalt", false,
                            [this]() { return RayTracedReflections; }, [this](bool On) { RayTracedReflections = On; });
        DeclareEditorToggle("render.nee", "Multi-bounce NEE + RIS", "Direct sun, fire & IES light sampling at secondary path vertices", false,
                            [this]() { return MultiBounceNEE; }, [this](bool On) { MultiBounceNEE = On; });
        DeclareEditorToggle("render.roulette", "Russian roulette termination", "Throughput-weighted unbiased path continuation", false,
                            [this]() { return RussianRoulette; }, [this](bool On) { RussianRoulette = On; });
        DeclareEditorToggle("render.firefly", "Firefly & boiling suppression", "Jacobian & luminance outlier clamp", false,
                            [this]() { return FireflyFilter; }, [this](bool On) { FireflyFilter = On; });

        DeclareEditorCard("Ray-traced reflections", "", 2, "Aperture");
        DeclareEditorMetric("render.reflMetric",
                            [this]() {
                                if (!Reflection) return std::string("Off");
                                return (RayTracing && RayTracedReflections)
                                           ? "RTX \xC2\xB7 " + std::to_string(ReflectionBounces) + (ReflectionBounces > 1 ? " bounces" : " bounce")
                                           : std::string("GTX \xC2\xB7 SDF");
                            }, "",
                            [this]() {
                                char Buffer[64] = {};
                                std::snprintf(Buffer, sizeof(Buffer), "rough \xE2\x89\xA4 %.2f", ReflectionRoughness);
                                return std::pair<std::string, std::string>{Buffer, Reflection ? "live" : "warm"};
                            });
        DeclareEditorMuted("Multi-bounce GGX VNDF glossy and mirror reflections traced against the Two-Level BVH with direct NEE, surface-card GI, and SVGF spatio-temporal reconstruction.");
        DeclareEditorToggle("render.reflEnable", "Enable glossy & mirror reflections", "Screen-space reflection target + exterior mirror glass", false,
                            [this]() { return Reflection; }, [this](bool On) { Reflection = On; });
        DeclareEditorToggle("render.reflRtx", "Use RTX BVH reflection rays", "Exact triangle hits instead of SDF sphere-tracing", false,
                            [this]() { return RayTracedReflections; }, [this](bool On) { RayTracedReflections = On; });
        DeclareEditorSlider("render.reflBounces2", "Reflection bounces", "", 1, 4, 1,
                            [this]() { return static_cast<double>(ReflectionBounces); }, [this](double V) { ReflectionBounces = static_cast<int>(V); }, 0, "Single bounce", "4 bounces");
        DeclareEditorSlider("render.reflScale", "Reflection intensity", "\xC3\x97", 0, 3, 0.05,
                            [this]() { return ReflectionScale; }, [this](double V) { ReflectionScale = V; }, 2);
        DeclareEditorSlider("render.reflRough", "Roughness cutoff", "", 0.15, 1.0, 0.05,
                            [this]() { return ReflectionRoughness; }, [this](double V) { ReflectionRoughness = V; }, 2, "Mirror only", "All surfaces");

        DeclareEditorCard("Resolution & quality", "", 3, "Monitor");
        DeclareEditorMetric("render.qualityMetric",
                            [this]() { return std::to_string(ScreenWidth) + "\xC3\x97" + std::to_string(ScreenHeight); }, "px",
                            [this]() {
                                const int Divisor = TraceScale == 0 ? 1 : TraceScale == 1 ? 2 : 4;
                                return std::pair<std::string, std::string>{
                                    "trace " + std::to_string(ScreenWidth / Divisor) + "\xC3\x97" + std::to_string(ScreenHeight / Divisor), "live"};
                            });
        DeclareEditorMuted("Render target after the scale and the pixel ceiling of the quality grade; ReSTIR GI and reflections trace at the trace resolution.");
        DeclareEditorSegmented("render.quality", "Quality grade \xC2\xB7 reloads", {"Low", "Medium", "High"},
                               [this]() { return Quality; }, [this](size_t Index) { Quality = Index; });
        DeclareEditorSegmented("render.traceScale", "Trace resolution", {"Full", "Half", "Quarter"},
                               [this]() { return TraceScale; }, [this](size_t Index) { TraceScale = Index; });
        DeclareEditorSlider("render.renderScale", "Render scale", "\xC3\x97", 0.25, 2.0, 0.05,
                            [this]() { return RenderScale; }, [this](double V) { RenderScale = V; }, 2, "Faster", "Sharper");

        DeclareEditorCard("Global illumination \xC2\xB7 ReSTIR", "", 4, "Sparkles");
        DeclareEditorToggle("render.temporal", "Temporal reuse", "Reservoirs carried through motion vectors", false,
                            [this]() { return TemporalReuse; }, [this](bool On) { TemporalReuse = On; });
        DeclareEditorToggle("render.spatial", "Spatial reuse", "Neighbouring reservoirs merged per pixel", false,
                            [this]() { return SpatialReuse; }, [this](bool On) { SpatialReuse = On; });
        DeclareEditorToggle("render.visibility", "Visibility validation", "Reused samples re-traced for occlusion", false,
                            [this]() { return VisibilityReuse; }, [this](bool On) { VisibilityReuse = On; });
        DeclareEditorToggle("render.refresh", "Refresh reused radiance", "", false,
                            [this]() { return RadianceRefresh; }, [this](bool On) { RadianceRefresh = On; });
        DeclareEditorSlider("render.spatialCount", "Spatial candidates", "", 0, 8, 1,
                            [this]() { return static_cast<double>(SpatialCount); }, [this](double V) { SpatialCount = static_cast<int>(V); }, 0);
        DeclareEditorSlider("render.spatialRadius", "Spatial radius", "px", 2, 48, 1,
                            [this]() { return static_cast<double>(SpatialRadius); }, [this](double V) { SpatialRadius = static_cast<int>(V); }, 0);
        DeclareEditorSlider("render.tally", "Temporal M limit", "", 1, 40, 1,
                            [this]() { return static_cast<double>(TemporalTallyLimit); }, [this](double V) { TemporalTallyLimit = static_cast<int>(V); }, 0, "Responsive", "Stable");
        DeclareEditorSlider("render.indirectScale", "Indirect intensity", "\xC3\x97", 0, 3, 0.05,
                            [this]() { return IndirectScale; }, [this](double V) { IndirectScale = V; }, 2);

        DeclareEditorCard("Exposure & image", "", 5, "Aperture");
        DeclareEditorToggle("render.autoExposure", "Auto exposure", "", false,
                            [this]() { return AutoExposure; }, [this](bool On) { AutoExposure = On; });
        DeclareEditorSlider("render.exposure", "Exposure compensation", "EV", -4, 4, 0.1,
                            [this]() { return ExposureCompensation; }, [this](double V) { ExposureCompensation = V; }, 1, "Darker", "Brighter");
        DeclareEditorToggle("render.sunShadows", "Sun shadows", "Ray-traced BVH cone or cascaded shadow depth", false,
                            [this]() { return DirectShadow; }, [this](bool On) { DirectShadow = On; });
        DeclareEditorToggle("render.glossy", "Glossy reflections", "BVH ray-traced or SDF-traced, SVGF denoised", false,
                            [this]() { return Reflection; }, [this](bool On) { Reflection = On; });
        DeclareEditorToggle("render.denoiser2", "Denoiser (SVGF)", "", false,
                            [this]() { return VarianceKernel; }, [this](bool On) { VarianceKernel = On; });
        DeclareEditorToggle("render.taa2", "Temporal antialiasing", "Also resolves dithered tyre marks", false,
                            [this]() { return Antialias; }, [this](bool On) { Antialias = On; });

        DeclareEditorCard("Debug views", "", 6, "Bug");
        DeclareEditorSelect("render.display", "Display",
                            {"Final image", "Direct sun only", "Indirect diffuse (GI)", "Indirect irradiance (denoised)",
                             "ReSTIR irradiance (raw, no denoise)", "Glossy reflections", "BVH / Distance-field inspection",
                             "Surface cards (texel space)", "Normals", "Albedo", "Clusters: visibility buffer IDs",
                             "Clusters: deferred resolve", "Clusters: Hi-Z pyramid", "Clusters: glass layers (front/back)",
                             "Clusters: LOD levels (near" "\xE2\x86\x92" "far)"},
                            [this]() { return Inspection; }, [this](size_t Index) { Inspection = Index; });
        DeclareEditorSelect("render.distanceView", "BVH / SDF view",
                            {"Triangle / field normals \xC2\xB7 dynamic objects", "BVH / SDF traversal step heat",
                             "Ray-traced NEE / surface cache"},
                            [this]() { return DistanceInspection; }, [this](size_t Index) { DistanceInspection = Index; });
        DeclareEditorToggle("render.hud", "Readout overlay", "H", false,
                            [this]() { return HudVisible; }, [this](bool On) { HudVisible = On; });
        DeclareEditorToggle("render.inset", "Distance slice inset", "J", false,
                            [this]() { return InsetEnabled; }, [this](bool On) { InsetEnabled = On; });
        DeclareEditorSlider("render.slice", "Slice height", "m", 0.05, 4.0, 0.05,
                            [this]() { return SliceHeight; }, [this](double V) { SliceHeight = V; }, 2);

        DeclareEditorCard("Frame timing", "", 7, "Activity");
        DeclareEditorMetric("render.timingMetric", []() { return std::string("60"); }, "fps",
                            []() { return std::pair<std::string, std::string>{"1% low \xE2\x80\x93", ""}; });
        DeclareEditorStats("render.timingStats", {{"Frame", "ms", []() { return std::string("16.7"); }},
                                                  {"Draw runs", "", []() { return std::string("0"); }},
                                                  {"TLAS instances", "", []() { return std::string("0"); }}});
        DeclareEditorTextBlock("render.timingReadout",
                               []() { return std::string("GPU pass timings need the timestamp-query feature;\nthis adapter does not expose it."); });
#endif
    }

    bool RayTracing = true, Indirect = true, Reflection = true, VarianceKernel = true, Antialias = true;
    bool Clusters = true, RayTracedShadows = true, RayTracedReflections = true, MultiBounceNEE = true;
    bool RussianRoulette = true, FireflyFilter = true;
    bool TemporalReuse = true, SpatialReuse = true, VisibilityReuse = true, RadianceRefresh = true;
    bool AutoExposure = true, DirectShadow = true, HudVisible = false, InsetEnabled = false;
    int  MaxBounces = 3, ReflectionBounces = 2, InitialCandidates = 2;
    int  SpatialCount = 5, SpatialRadius = 20, TemporalTallyLimit = 20, ScreenWidth = 1920, ScreenHeight = 1080;
    double ReflectionScale = 1.0, ReflectionRoughness = 0.80, RenderScale = 1.0, IndirectScale = 1.0;
    double ExposureCompensation = 0.0, SliceHeight = 0.6;
    size_t Quality = 2, TraceScale = 1, Inspection = 0, DistanceInspection = 1;
};

// ---------------------------------------------------------------------------
// The remaining HTML inspector pages, declared the same native way. Control
// lists, ranges and defaults are transcribed from the pinned HTML sources
// (InspectorPages.js BuildSun/BuildField/BuildCards, EnvironmentInspector.js
// entries, NativeFlarePanel.js, NativeWindPanel.js,
// MoonInspectorInfographics.js, ParticleWorkspace.js + PrecipitationSequence.js,
// EnvironmentSettings.js / CloudSettings.js defaults). Bespoke canvas
// instruments (sun arc, moon globe, flare/wind diagrams) are not drawn by
// the generic vocabulary yet — see PARITY.md.
// ---------------------------------------------------------------------------

class FieldRecord final : public Frontier::RecordEntry
{
  public:
    FieldRecord() : RecordEntry(Frontier::RecordClassification::Illumination)
    {
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
        DeclareEditorIdentity("Acceleration structures", "Render");
        DeclareEditorDescription("Two-Level BVH (TLAS + BLAS) & toroidal SDF cascades");
        DeclareEditorCard("Two-Level BVH (TLAS + BLAS)", "", 1, "Zap");
        DeclareEditorMetric("field.instances", []() { return std::string("0"); }, "instances",
                            []() { return std::pair<std::string, std::string>{"0 triangles", "live"}; });
        DeclareEditorStats("field.stats", {{"TLAS nodes", "", []() { return std::string("0"); }},
                                           {"BLAS nodes", "", []() { return std::string("0"); }},
                                           {"Triangles", "", []() { return std::string("0"); }}});
        DeclareEditorMuted("TLAS is rebuilt over active occupants each frame; per-mesh Binned-SAH BLAS hierarchies are built at bring-up and extended dynamically on fracture.");
        DeclareEditorCard("Cascades", "", 2, "Layers");
        DeclareEditorMetric("field.bricks", []() { return std::string("0"); }, "bricks",
                            [this]() { return std::pair<std::string, std::string>{Freeze ? "frozen" : "updating", Freeze ? "warm" : "live"}; });
        DeclareEditorMuted("Bricks rewritten since start: scrolling windows and moving occupants only.");
        DeclareEditorToggle("field.freeze", "Freeze field updates", "", false, [this]() { return Freeze; }, [this](bool On) { Freeze = On; });
        DeclareEditorSelect("field.view", "Field view (display: global distance field)",
                            {"Triangle / field normals \xC2\xB7 dynamic objects", "BVH / SDF traversal step heat", "Ray-traced NEE / surface cache"},
                            [this]() { return View; }, [this](size_t Index) { View = Index; });
        DeclareEditorToggle("field.inset", "Distance slice inset", "", false, [this]() { return Inset; }, [this](bool On) { Inset = On; });
        DeclareEditorSlider("field.slice", "Slice height", "m", 0.05, 4.0, 0.05, [this]() { return Slice; }, [this](double V) { Slice = V; }, 2);
#endif
    }
    bool Freeze = false, Inset = false;
    double Slice = 0.6;
    size_t View = 1;
};

class SurfaceCardsRecord final : public Frontier::RecordEntry
{
  public:
    SurfaceCardsRecord() : RecordEntry(Frontier::RecordClassification::Illumination)
    {
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
        DeclareEditorIdentity("Surface cards", "Render");
        DeclareEditorDescription("Card radiosity & relighting budget");
        DeclareEditorCard("Surface cache", "", 1, "Layers");
        DeclareEditorMetric("cards.tiles", []() { return std::string("0"); }, "tiles",
                            []() { return std::pair<std::string, std::string>{"relit this frame", ""}; });
        DeclareEditorToggle("cards.radiosity", "Card radiosity", "Multi-bounce inside the cache", false,
                            [this]() { return Radiosity; }, [this](bool On) { Radiosity = On; });
        DeclareEditorSlider("cards.budget", "Static tiles per frame", "", 0, 8192, 64, [this]() { return Budget; }, [this](double V) { Budget = V; }, 0, "Lazy", "Eager");
#endif
    }
    bool Radiosity = true;
    double Budget = 2048.0;
};

class ParticlesRecord final : public Frontier::RecordEntry
{
  public:
    ParticlesRecord() : RecordEntry(Frontier::RecordClassification::Content)
    {
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
        DeclareEditorIdentity("Particle authoring workspace", "Effects");
        DeclareEditorDescription("Particle Editor, Flux, Fracture and GPU precipitation");
        DeclareEditorCard("Precipitation \xC2\xB7 celestial GPU port", "", 1, "Layers");
        DeclareEditorMuted("Celestial demo behavior, ported to GPU: 45 m camera-local falling volume, terminal-speed drag, round snow/hail and rain streaks. Hail rebounds up to three times, comes to rest, then becomes opaque 3D mesh grains. No white surface coating.");
        DeclareEditorAction("particles.togglePrecip", "Start precipitation", [this]() { PrecipEnabled = !PrecipEnabled; });
        DeclareEditorAction("particles.clearPrecip", "Clear precipitation & settled meshes", []() {});
        DeclareEditorToggle("particles.spawn", "Spawn from clouds", "Cloud-masked camera-local volume, as in the demo. Off: uniform volume up to 60 m above camera.", false,
                            [this]() { return SpawnFromClouds; }, [this](bool On) { SpawnFromClouds = On; });
        DeclareEditorSelect("particles.cloudSource", "Cloud source", {"Auto \xC2\xB7 global, otherwise local", "Global cloud layer", "Local cloud volume"},
                            [this]() { return CloudSource; }, [this](size_t Index) { CloudSource = Index; });
        DeclareEditorSlider("particles.rate", "Intensity", "mm/h", 0, 100, 0.5, [this]() { return Rate; }, [this](double V) { Rate = V; }, 1);
        DeclareEditorSlider("particles.density", "Density", "\xC3\x97", 0.1, 6, 0.1, [this]() { return Density; }, [this](double V) { Density = V; }, 1);
        DeclareEditorSlider("particles.size", "Particle size", "\xC3\x97", 0.3, 3, 0.05, [this]() { return SizeScale; }, [this](double V) { SizeScale = V; }, 2);
        DeclareEditorSlider("particles.drift", "Wind drift", "", 0, 1, 0.01, [this]() { return WindDrift; }, [this](double V) { WindDrift = V; }, 2);
        DeclareEditorSlider("particles.restitution", "Restitution", "", 0, 1, 0.01, [this]() { return Restitution; }, [this](double V) { Restitution = V; }, 2);
        DeclareEditorSlider("particles.rest", "Rest time before mesh conversion", "s", 0.1, 10, 0.1, [this]() { return RestTime; }, [this](double V) { RestTime = V; }, 1);
        DeclareEditorSlider("particles.accum", "Accumulation", "", 0, 1, 0.01, [this]() { return Accumulation; }, [this](double V) { Accumulation = V; }, 2);
        DeclareEditorSlider("particles.streak", "Motion streak", "", 0, 1, 0.01, [this]() { return Streak; }, [this](double V) { Streak = V; }, 2);
        DeclareEditorSlider("particles.opacity", "Opacity", "", 0.1, 1, 0.01, [this]() { return Opacity; }, [this](double V) { Opacity = V; }, 2);
        DeclareEditorToggle("particles.follow", "Follow shared Wind Field", "", false, [this]() { return FollowWind; }, [this](bool On) { FollowWind = On; });
        DeclareEditorTextBlock("particles.readout", []() { return std::string("0 GPU particles \xC2\xB7 0 rebounding \xC2\xB7 0 at rest\n0 bounce events \xC2\xB7 0 / 65536 settled mesh grains\n0 ground conversions \xC2\xB7 0 vehicle conversions \xC2\xB7 0 retained at budget"); });
        DeclareEditorMuted("Snow/ice converts only after final contact, zero velocity and the rest interval. Rain/drizzle rest splashes retire instead of creating ice. Settled grains are opaque triangle meshes, not a connected height-field blanket or decal. Vehicle grains stay body-local. Stop drains existing particles; Clear explicitly removes everything.");
        DeclareEditorMuted("Bounded GPU pools: 32768 moving slots reserved; 65536 stopped mesh instances. Mesh-budget overflow keeps stopped slots instead of deleting their ice. Ground receiving map stays world-fixed (128 \xC3\x97 128 m on first Start); body receiver assumes authored upper surfaces. No arbitrary obstacle collision, mesh union, sliding/melting, snow physics or disk persistence yet.");
        DeclareEditorCard("Particle authoring", "", 2, "Sparkles");
        DeclareEditorMuted("The original Particle Editor and Flux Particle Studio, together in one authoring workspace. Edit real GPU particles using their original outliners, inspectors and previews.");
        DeclareEditorAction("particles.openPrecip", "Open Precipitation component", []() {});
        DeclareEditorAction("particles.openEditor", "Open Particle Editor", []() {});
        DeclareEditorAction("particles.openLightning", "Author lightning \xE2\x86\x92 apply to scene", []() {});
        DeclareEditorAction("particles.openFlux", "Open Flux Particle Studio", []() {});
        DeclareEditorAction("particles.openFracture", "Open Fracture Editor", []() {});
        DeclareEditorMuted("Fracture opens the supplied original editor with its own geometry generation, local storage and Export fracture controls. It does not yet replace driving-scene meshes.");
        DeclareEditorReadout("particles.sceneSystems", "Particle scene:", []() { return std::string("1 saved systems"); });
        DeclareEditorReadout("particles.fluxLayers", "Flux composition:", []() { return std::string("0 saved layers"); });
        DeclareEditorMuted("The driving scene pauses while authoring. Documents autosave locally and can be imported/exported together. Lightning shape/timing can now be explicitly applied to the scene. Other emitters still render only in their authoring previews. Existing Derez effects remain unchanged.");
#endif
    }
    bool PrecipEnabled = false, SpawnFromClouds = true, FollowWind = false;
    double Rate = 25.0, Density = 1.0, SizeScale = 1.0, WindDrift = 0.5, Restitution = 0.35, RestTime = 1.6, Accumulation = 0.5, Streak = 0.7, Opacity = 0.85;
    size_t CloudSource = 0;
};


// ---------------------------------------------------------------------------
// The remaining generic-vocabulary HTML pages (InspectorPages.js BuildDerez /
// BuildFractureGlobal / BuildTraining / BuildCones / BuildLevel / BuildStress
// and DiagnosticsInspector.js), declared as native records with the HTML
// defaults. ActionRow buttons render as stacked actions and drill rows /
// link buttons as readouts + actions in the generic vocabulary (PARITY.md).
// ---------------------------------------------------------------------------

class DerezRecord final : public Frontier::RecordEntry
{
  public:
    DerezRecord() : RecordEntry(Frontier::RecordClassification::Content)
    {
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
        DeclareEditorIdentity("Particle effect", "Effects");
        DeclareEditorDescription("Particle Editor presets \xC2\xB7 recovery & respawn");
        DeclareEditorToggle("derez.recovery", "Auto recovery", "", true, [this]() { return RecoveryEnabled; }, [this](bool On) { RecoveryEnabled = On; }, "Shield");
        DeclareEditorCard("Preset", "From the Particle Editor", 1, "Sparkles");
        DeclareEditorMuted("The car comes apart cell by cell on one release clock: the surface raster glows each cell in its own facet's colour and drops it the instant the effect raster launches its cubes \xE2\x80\x94 each cube the colour of the face it came from, glowing in it, leaving with the car's velocity at that moment (the car keeps moving while it dissolves). Rez runs the clock backwards.");
        DeclareEditorSegmented("derez.preset", "Look", {"Derez cube", "Reverse derez", "Glitch dissolve"},
                               [this]() { return Preset; }, [this](size_t Index) { Preset = Index; });
        DeclareEditorAction("derez.respawn", "Derez & respawn", []() {});
        DeclareEditorAction("derez.burst", "Burst nearest cone", []() {});
        DeclareEditorCard("Cubes", "", 2, "Box");
        DeclareEditorSlider("derez.hold", "Hold before the wave", "s", 0, 2, 0.05, [this]() { return Hold; }, [this](double V) { Hold = V; }, 2);
        DeclareEditorSlider("derez.wave", "Wave spread", "s", 0.3, 4, 0.05, [this]() { return Wave; }, [this](double V) { Wave = V; }, 2, "Snap", "Slow sweep");
        DeclareEditorSlider("derez.burst2", "Burst along the normal", "m/s", 0, 4, 0.05, [this]() { return Burst; }, [this](double V) { Burst = V; }, 2);
        DeclareEditorSlider("derez.smin", "Slowest launch", "m/s", 0, 4, 0.05, [this]() { return SMin; }, [this](double V) { SMin = V; }, 2);
        DeclareEditorSlider("derez.smax", "Fastest launch", "m/s", 0, 8, 0.05, [this]() { return SMax; }, [this](double V) { SMax = V; }, 2);
        DeclareEditorSlider("derez.drag", "Drag", "/s", 0.05, 3, 0.05, [this]() { return Drag; }, [this](double V) { Drag = V; }, 2);
        DeclareEditorSlider("derez.life", "Cube life", "s", 0.5, 6, 0.1, [this]() { return Life; }, [this](double V) { Life = V; }, 1);
        DeclareEditorSlider("derez.size", "Cube size", "m", 0.02, 0.2, 0.005, [this]() { return Size; }, [this](double V) { Size = V; }, 3);
        DeclareEditorSlider("derez.flight", "Rez flight", "s", 0.4, 4, 0.05, [this]() { return Flight; }, [this](double V) { Flight = V; }, 2);
        DeclareEditorSegmented("derez.style", "Style", {"Cubes", "Glitch"}, [this]() { return Style; }, [this](size_t Index) { Style = Index; });
        DeclareEditorCard("Glow", "", 3, "Palette");
        DeclareEditorSegmented("derez.faces", "Cube colour", {"Car's own colours", "Palette"}, [this]() { return Faces ? size_t(0) : size_t(1); }, [this](size_t Index) { Faces = Index == 0; });
        DeclareEditorSlider("derez.glowStrength", "Face glow", "\xC3\x97 albedo", 0, 20, 0.5, [this]() { return GlowStrength; }, [this](double V) { GlowStrength = V; }, 1);
        DeclareEditorToggle("derez.momentum", "Keep the car's momentum", "It coasts on while it comes apart; cubes leave at its velocity", false,
                            [this]() { return Momentum; }, [this](bool On) { Momentum = On; });
        DeclareEditorReadout("derez.glowColour", "Glow", []() { return std::string("#35D6FF"); });
        DeclareEditorReadout("derez.hotColour", "Hot edge", []() { return std::string("#FFFFFF"); });
        DeclareEditorReadout("derez.glitchColour", "Glitch", []() { return std::string("#FF2D6F"); });
        DeclareEditorSlider("derez.intensity", "Intensity", "\xC3\x97", 0, 4, 0.05, [this]() { return Intensity; }, [this](double V) { Intensity = V; }, 2);
        DeclareEditorCard("Recovery", "Derez \xE2\x86\x92 hidden \xE2\x86\x92 move \xE2\x86\x92 rez", 4, "Shield");
        DeclareEditorMuted("An overturned car, a car tumbling through the air, or one that has left the grounds derezzes and rezzes back \xE2\x80\x94 upright where it lay, or at the last safe spot. R and every teleport use the same sequence.");
        DeclareEditorToggle("derez.autoRecovery", "Automatic recovery", "", false, [this]() { return RecoveryEnabled; }, [this](bool On) { RecoveryEnabled = On; });
        DeclareEditorSlider("derez.overturn", "Overturned for", "s", 0.3, 5, 0.1, [this]() { return Overturn; }, [this](double V) { Overturn = V; }, 1);
        DeclareEditorSlider("derez.airborne", "Airborne for", "s", 1, 6, 0.1, [this]() { return Airborne; }, [this](double V) { Airborne = V; }, 1);
        DeclareEditorSlider("derez.tumble", "Tumbling for", "s", 0.3, 3, 0.1, [this]() { return Tumble; }, [this](double V) { Tumble = V; }, 1);
        DeclareEditorSlider("derez.tumbleRate", "Tumble rate", "rad/s", 2, 12, 0.5, [this]() { return TumbleRate; }, [this](double V) { TumbleRate = V; }, 1);
#endif
    }
    bool RecoveryEnabled = true, Faces = true, Momentum = true;
    double Hold = 0.3, Wave = 1.0, Burst = 0.5, SMin = 0.6, SMax = 2.6, Drag = 0.4, Life = 3.5, Size = 0.075, Flight = 1.2;
    double GlowStrength = 6.0, Intensity = 1.0, Overturn = 1.5, Airborne = 2.5, Tumble = 0.9, TumbleRate = 5.0;
    size_t Preset = 0, Style = 0;
};

class FractureRecord final : public Frontier::RecordEntry
{
  public:
    FractureRecord() : RecordEntry(Frontier::RecordClassification::Physics)
    {
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
        DeclareEditorIdentity("Breakable props", "Destruction");
        DeclareEditorDescription("Energy-limited fracture \xC2\xB7 fragment bodies");
        DeclareEditorToggle("fracture.breakable", "Breakable", "", true, [this]() { return Enabled; }, [this](bool On) { Enabled = On; }, "Hammer");
        DeclareEditorToggle("fracture.respawn2", "Respawn", "", true, [this]() { return Respawn; }, [this](bool On) { Respawn = On; }, "RotateCcw");
        DeclareEditorCard("Breakable props", "", 1, "Boxes");
        DeclareEditorMetric("fracture.intact", []() { return std::string("0"); }, "/ 0 intact",
                            []() { return std::pair<std::string, std::string>{"0 breaks", ""}; });
        DeclareEditorMuted("Each prop is a closed convex solid with a Fracture-editor recipe. Strike one with more than its threshold energy and it fractures; below that it stops the car.");
        DeclareEditorStats("fracture.stats", {{"Fragments", "", []() { return std::string("0"); }},
                                              {"Asleep", "", []() { return std::string("0"); }},
                                              {"In the field", "", []() { return std::string("0"); }}});
        DeclareEditorAction("fracture.reassemble", "Reassemble all", []() {});
        DeclareEditorAction("fracture.breakAll", "Break all", []() {});
        DeclareEditorCard("Debris", "Convex rigid bodies \xC2\xB7 stays where it lands", 2, "Gem");
        DeclareEditorMuted("Every fragment is a convex hull in the shared rigid-body world: against the course, the walls, every other fragment and cone, and the car both ways. Large fragments get a distance volume of their own the frame they are born and join the global distance field, so the debris shadows and bounces light in the GI.");
        DeclareEditorToggle("fracture.respawn", "Reassemble after the lifetime", "Off: debris stays until reset (T)", false,
                            [this]() { return Respawn; }, [this](bool On) { Respawn = On; });
        DeclareEditorSlider("fracture.lifetime", "Lifetime before reassembly", "s", 3, 120, 1, [this]() { return Lifetime; }, [this](double V) { Lifetime = V; }, 0);
        DeclareEditorSlider("fracture.separation", "Separation", "\xC3\x97", 0, 3, 0.05, [this]() { return Separation; }, [this](double V) { Separation = V; }, 2, "Crumble", "Explode");
        DeclareEditorSlider("fracture.share", "Impact share (Dynamic)", "", 0.05, 1.0, 0.01, [this]() { return ImpactShare; }, [this](double V) { ImpactShare = V; }, 2, "Few pieces", "Many");
        DeclareEditorSlider("fracture.bounce", "Bounce", "\xC3\x97", 0, 3, 0.05, [this]() { return Bounce; }, [this](double V) { Bounce = V; }, 2);
        DeclareEditorSlider("fracture.friction", "Friction", "\xC3\x97", 0, 3, 0.05, [this]() { return Friction; }, [this](double V) { Friction = V; }, 2);
        DeclareEditorSlider("fracture.fieldRadius", "Distance field from radius", "voxels", 0.25, 4, 0.05, [this]() { return FieldRadius; }, [this](double V) { FieldRadius = V; }, 2, "More pieces", "Fewer");
        DeclareEditorToggle("fracture.carCollides", "Car collides with debris", "Both ways", false,
                            [this]() { return CarCollides; }, [this](bool On) { CarCollides = On; });
        DeclareEditorStats("fracture.bodyStats", {{"Bodies", "", []() { return std::string("0"); }},
                                                  {"Awake", "", []() { return std::string("0"); }},
                                                  {"Step", "ms", []() { return std::string("0.00"); }}});
        DeclareEditorStats("fracture.contactStats", {{"Contacts", "", []() { return std::string("0"); }},
                                                     {"Pool volumes", "", []() { return std::string("0"); }},
                                                     {"Computed", "", []() { return std::string("0"); }}});
        DeclareEditorCard("Bake \xC2\xB7 all props", "", 3, "Layers");
        DeclareEditorMuted("Dynamic cuts fragments on the spot at the true impact point with the impact's share of energy (one per frame, ~10 ms); Baked uses the stored pattern \xE2\x80\x94 no cost at impact.");
        DeclareEditorAction("fracture.allDynamic", "All dynamic", []() {});
        DeclareEditorAction("fracture.allBaked", "All baked", []() {});
#endif
    }
    bool Enabled = true, Respawn = false, CarCollides = true;
    double Lifetime = 25.0, Separation = 1.0, ImpactShare = 0.35, Bounce = 1.0, Friction = 1.0, FieldRadius = 0.75;
};

class TrainingRecord final : public Frontier::RecordEntry
{
  public:
    TrainingRecord() : RecordEntry(Frontier::RecordClassification::Content)
    {
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
        DeclareEditorIdentity("Drills", "Level");
        DeclareEditorDescription("Timed stations \xC2\xB7 best times");
        DeclareEditorCard("Now running", "", 1, "Timer");
        DeclareEditorTextBlock("training.readout", []() { return std::string("No drill running."); });
        DeclareEditorMuted("Drive into any station and its drill starts on its own; or start one here \xE2\x80\x94 the car derezzes to the start and the cones the last run knocked over rez back onto their marks. R returns to the running drill's start, T resets the whole arena.");
        DeclareEditorAction("training.reset", "Reset arena", []() {});
        DeclareEditorAction("training.stop", "Stop drill", []() {});
        DeclareEditorCard("Stations", "Best times are kept in this browser", 2, "Flag");
        DeclareEditorReadout("training.skidpad", "Skidpad \xC2\xB7 Hold the 14 m ring", []() { return std::string(""); });
        DeclareEditorAction("training.startSkidpad", "Start Skidpad", []() {});
        DeclareEditorReadout("training.jump", "Jump \xC2\xB7 Airtime and distance past the lip", []() { return std::string(""); });
        DeclareEditorAction("training.startJump", "Start Jump", []() {});
        DeclareEditorReadout("training.smash", "Smash alley \xC2\xB7 Breakable props", []() { return std::string(""); });
        DeclareEditorAction("training.startSmash", "Start Smash alley", []() {});
        DeclareEditorReadout("training.slalom", "Slalom \xC2\xB7 8 cones agility course", []() { return std::string(""); });
        DeclareEditorAction("training.startSlalom", "Start Slalom", []() {});
        DeclareEditorAction("training.clearBest", "Clear best times", []() {});
#endif
    }
};

class ConesRecord final : public Frontier::RecordEntry
{
  public:
    ConesRecord() : RecordEntry(Frontier::RecordClassification::Physics)
    {
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
        DeclareEditorIdentity("Rigid bodies", "Level");
        DeclareEditorDescription("Knockable cones \xC2\xB7 rez back on reset");
        DeclareEditorCard("Cones", "70 cm slalom cone \xC2\xB7 3.2 kg", 1, "TrafficCone");
        DeclareEditorMetric("cones.displaced", []() { return std::string("0"); }, "displaced",
                            []() { return std::pair<std::string, std::string>{"0 moving", ""}; });
        DeclareEditorMuted("A fluorescent PVC cone with two retroreflective collars on a chamfered rubber base, weighted low like a real one so it rocks before it topples. At rest each cone is a sleeping rigid body and a static occupant; a knocked cone follows its body every frame until it settles.");
        DeclareEditorAction("cones.rez", "Rez all back", []() {});
        DeclareEditorAction("cones.snap", "Snap back", []() {});
#endif
    }
};

class LevelRecord final : public Frontier::RecordEntry
{
  public:
    LevelRecord() : RecordEntry(Frontier::RecordClassification::Content)
    {
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
        DeclareEditorIdentity("Level", "Level");
        DeclareEditorDescription("Zones, teleports & scene budget");
        DeclareEditorCard("Proving grounds", "Teleport the car", 1, "Map");
        DeclareEditorMuted("A 216 \xC3\x97 120 m asphalt pad in a grass surround. West: the main straight with kerbs and a jump, a skidpad, a concrete burnout box, a smash alley of breakable props, a slatted hangar for bounce light, a container yard and a lit tunnel. East: the training arena \xE2\x80\x94 launch strip, slalom, moose test, cone bowling, spin box and a figure-8.");
        DeclareEditorAction("level.startGrid", "Start grid \xC2\xB7 Main straight", []() {});
        DeclareEditorAction("level.garage", "Garage \xC2\xB7 Hangar & vehicle workshop", []() {});
        DeclareEditorAction("level.tunnel", "Tunnel \xC2\xB7 Lamps in the dark", []() {});
        DeclareEditorAction("level.east", "East Course \xC2\xB7 Open proving grounds", []() {});
        DeclareEditorAction("level.smash", "Smash Alley \xC2\xB7 Breakable objects", []() {});
        DeclareEditorAction("level.skidpad", "Skidpad \xC2\xB7 14 m drift ring", []() {});
        DeclareEditorCard("Scene budget", "", 2, "Cpu");
        DeclareEditorStats("level.stats1", {{"Occupants", "", []() { return std::string("0"); }},
                                            {"In the field", "", []() { return std::string("0"); }},
                                            {"Volumes", "", []() { return std::string("0"); }}});
        DeclareEditorStats("level.stats2", {{"Cards", "", []() { return std::string("0"); }},
                                            {"Draw runs", "", []() { return std::string("0"); }},
                                            {"Volume atlas", "MB", []() { return std::string("0"); }}});
#endif
    }
};

class StressRecord final : public Frontier::RecordEntry
{
  public:
    StressRecord() : RecordEntry(Frontier::RecordClassification::Content)
    {
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
        DeclareEditorIdentity("Object pool", "Level");
        DeclareEditorDescription("Pre-built pool \xC2\xB7 union, cards & draws under load");
        DeclareEditorCard("Stress test", "", 1, "Boxes");
        DeclareEditorMetric("stress.active", []() { return std::string("0"); }, "objects",
                            []() { return std::pair<std::string, std::string>{"0 moving", ""}; });
        DeclareEditorMuted("The pool is built with the scene; enabling objects lengthens the union span, grows the draw runs, joins the relighting lists and adds colliders \xE2\x80\x94 bookkeeping, not construction.");
        DeclareEditorAction("stress.add64", "+64", []() {});
        DeclareEditorAction("stress.add256", "+256", []() {});
        DeclareEditorAction("stress.add1024", "+1024", []() {});
        DeclareEditorAction("stress.fill", "Fill pool", []() {});
        DeclareEditorAction("stress.clear", "Clear", []() {});
        DeclareEditorSegmented("stress.motion", "Motion", {"Static", "One in ten", "All moving"},
                               [this]() { return Motion; }, [this](size_t Index) { Motion = Index; });
#endif
    }
    size_t Motion = 0;
};

class DiagnosticsRecord final : public Frontier::RecordEntry
{
  public:
    DiagnosticsRecord() : RecordEntry(Frontier::RecordClassification::Interface)
    {
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
        DeclareEditorIdentity("Session telemetry", "Render");
        DeclareEditorDescription("Session recording, frame charts, GPU passes and CSV export");
        DeclareEditorCard("Session recorder", "", 1, "Activity");
        DeclareEditorAction("diagnostics.refresh", "Pause live editor refresh (A/B)", []() {});
        DeclareEditorMuted("A/B switch pauses live readouts, charts, outliner and gizmo redraw, not the renderer or recorder. It does not remove the DOM or all browser UI cost.");
        DeclareEditorAction("diagnostics.record", "Record / pause", []() {});
        DeclareEditorAction("diagnostics.clear", "Clear", []() {});
        DeclareEditorTextBlock("diagnostics.readout", []() { return std::string("0 / 10,000 records \xC2\xB7 0s session\nGPU timestamps unavailable \xE2\x80\x94 not reported as zero"); });
        DeclareEditorAction("diagnostics.copy", "Copy all CSV", []() {});
        DeclareEditorAction("diagnostics.download", "Download CSV", []() {});
        DeclareEditorAction("diagnostics.remove", "Remove recorder & discard data", []() {});
        DeclareEditorMuted("In memory until export; reload loses the session. Storage stops at the limit, never silently overwrites. Charts show recent samples; CSV includes everything, including settings and events.");
        DeclareEditorCard("Frame pacing", "", 2, "Activity");
        DeclareEditorTextBlock("diagnostics.frames", []() { return std::string("Waiting for frame samples"); });
        DeclareEditorCard("CPU attribution \xC2\xB7 latest frame", "", 3, "Activity");
        DeclareEditorTextBlock("diagnostics.cpu", []() { return std::string("Waiting for CPU phase samples"); });
        DeclareEditorMuted("UI subphases are nested inside CPU / hud_editor, not additional costs. Physics/effects is an aggregate; steps and GPU queue skips are also exported.");
        DeclareEditorCard("Editor & recorder CPU", "", 3, "Monitor");
        DeclareEditorMuted("JavaScript duration only. Inspector-build events and page long tasks are in CSV. Layout, paint, compositor work and uninstrumented input handlers are not attributed as UI time. Recorder values are partial self-timings, not its complete system overhead.");
        DeclareEditorCard("GPU workload", "", 4, "Layers");
        DeclareEditorSelect("diagnostics.pass", "GPU pass", {"Clouds"}, [this]() { return Pass; }, [this](size_t Index) { Pass = Index; });
        DeclareEditorMuted("Heatmap: recent samples left to right; green \xE2\x86\x92 red relative to each pass maximum; grey = unavailable.");
        DeclareEditorMuted("Per-pass raw available samples, not total frame GPU time. Missing samples are gaps. Cloud presentation is inside the existing display pass.");
        DeclareEditorCard("Capture context", "", 5, "Monitor");
        DeclareEditorTextBlock("diagnostics.context", []() { return std::string("No settings snapshot"); });
        DeclareEditorCard("Settings & session events", "", 6, "List");
        DeclareEditorMuted("CSV settings rows preserve all host settings, quality and actual screen/cloud dimensions. Frame/GPU rows reference their settings revision. Browser long tasks are observed where supported; editor JS timing does not include layout, paint, compositor time or all input handlers.");
#endif
    }
    size_t Pass = 0;
};

class ClustersRecord final : public Frontier::RecordEntry
{
  public:
    ClustersRecord() : RecordEntry(Frontier::RecordClassification::Illumination)
    {
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
        DeclareEditorIdentity("Cluster diagnostics", "Render");
        DeclareEditorDescription("Actual GPU visibility, bounds overlay and rebuild");
        DeclareEditorCard("Geometry clusters", "", 1, "Layers");
        DeclareEditorToggle("clusters.culling", "GPU cluster culling", "Lossless original triangles; not simplified Nanite LOD", false,
                            [this]() { return Culling; }, [this](bool On) { Culling = On; });
        DeclareEditorSegmented("clusters.budget", "Maximum triangles per leaf \xC2\xB7 rebuilds", {"64", "128", "256"},
                               [this]() { return Budget; }, [this](size_t Index) { Budget = Index; });
        DeclareEditorToggle("clusters.overlay", "Cluster bounds overlay", "Green: GPU-visible \xC2\xB7 red: GPU-culled \xC2\xB7 grey: pending", false,
                            [this]() { return Overlay; }, [this](bool On) { Overlay = On; });
        DeclareEditorAction("clusters.rebuild", "Rebuild clusters", []() {});
        DeclareEditorTextBlock("clusters.readout", []() { return std::string("0 candidates \xC2\xB7 0 indices \xC2\xB7 revision 0\nEnable overlay for asynchronous GPU visibility readback\nOverlay: 0 / 0 bounds (display cap only)"); });
        DeclareEditorMuted("Bounds are cluster AABBs, not triangle wireframes. Visibility comes from actual indirect GPU commands, sampled at most 4 Hz. Move the camera to watch culling; rebuild refreshes partitions. Hidden clusters may lie outside the viewport. Overlay/readback has an optional diagnostic cost.");
#endif
    }
    bool Culling = true, Overlay = false;
    size_t Budget = 1;
};

// Shared text/rect helpers for the bespoke environment dashboards below.
struct PageInk
{
    ImDrawList* Ink;
    ImFont* Font;
    void Text(float X, float Y, float Size, ImU32 Colour, const std::string& Value) const
    { Ink->AddText(Font, Size, ImVec2(X, Y), Colour, Value.c_str()); }
    float Width(float Size, const std::string& Value) const
    { return Font->CalcTextSizeA(Size, 1e9f, 0.0f, Value.c_str()).x; }
    void TextR(float Right, float Y, float Size, ImU32 Colour, const std::string& Value) const
    { Text(Right - Width(Size, Value), Y, Size, Colour, Value); }
    void TextC(float Centre, float Y, float Size, ImU32 Colour, const std::string& Value) const
    { Text(Centre - Width(Size, Value) * 0.5f, Y, Size, Colour, Value); }
    void Rect(float X, float Y, float R, float B, ImU32 Fill, float Radius) const
    { Ink->AddRectFilled(ImVec2(X, Y), ImVec2(R, B), Fill, Radius); }
    void RectLine(float X, float Y, float R, float B, ImU32 Edge, float Radius) const
    { Ink->AddRect(ImVec2(X, Y), ImVec2(R, B), Edge, Radius, 0, 1.0f); }
    // .frontier-slider (SliderControl.css): head label + value capsule, 22 px track, 20 px thumb.
    // Returns the Y below the control. NumberSize/UnitInk allow dashboard overrides.
    float Slider(float L, float R, float Y, const char* Label, const std::string& Value, const char* Unit,
                 float Fraction, float NumberSize = 11.0f, ImU32 LabelInk = IM_COL32(0xa5, 0xa5, 0xa5, 255),
                 ImU32 FillInk = IM_COL32(0x55, 0x56, 0x50, 255), ImU32 CapsuleInk = IM_COL32(0x08, 0x08, 0x08, 255)) const
    {
        Text(L, Y + 4.0f, 11.0f, LabelInk, Label);
        const float CapW = 88.0f;
        Rect(R - CapW, Y, R, Y + 24.0f, CapsuleInk, 20.0f);
        TextR(R - 22.0f, Y + 12.0f - NumberSize * 0.62f, NumberSize, IM_COL32(0xe0, 0xe0, 0xd8, 255), Value);
        Text(R - 20.0f, Y + 8.0f, 10.0f, IM_COL32(0xb2, 0xb2, 0xaa, 255), Unit);
        Y += 24.0f + 12.0f;
        Rect(L, Y, R, Y + 22.0f, IM_COL32(0x17, 0x18, 0x18, 255), 20.0f);
        const float FillR = L + (R - L) * Fraction;
        if (FillR > L + 20.0f) Rect(L, Y, FillR, Y + 22.0f, FillInk, 20.0f);
        Ink->AddCircleFilled(ImVec2(std::max(L + 11.0f, FillR - 11.0f), Y + 11.0f), 10.0f, IM_COL32(0xe2, 0xe4, 0xd8, 255));
        return Y + 22.0f;
    }
    // .inspector-categories tablist; ActiveInk/ActiveFill per dashboard family.
    float Tabs(float L, float R, float Y, const char* const* Labels, int Count, ImU32 ActiveFill, ImU32 ActiveInk) const
    {
        const float TW = (R - L - 4.0f * (Count - 1)) / Count;
        for (int I = 0; I < Count; ++I)
        {
            const float X = L + I * (TW + 4.0f);
            if (I == 0) Rect(X, Y, X + TW, Y + 33.0f, ActiveFill, 23.0f);
            TextC(X + TW * 0.5f, Y + 11.0f, 10.0f, I == 0 ? ActiveInk : IM_COL32(0x91, 0xa0, 0xad, 255), Labels[I]);
        }
        return Y + 33.0f;
    }
    void TopBar(const char* Group) const
    {
        Rect(0, 0, 380, 44, IM_COL32(0x14, 0x14, 0x14, 255), 0);
        Ink->AddRectFilled(ImVec2(0, 44), ImVec2(380, 45), IM_COL32(0x2a, 0x2a, 0x2a, 255));
        Text(18, 16, 11, IM_COL32(0xbf, 0xbf, 0xbf, 255), "Inspector");
        const float GX = 18 + Width(11, "Inspector") + 8;
        Text(GX, 16, 11, IM_COL32(0x71, 0x71, 0x71, 255), ">");
        Text(GX + Width(11, ">") + 8, 16, 11, IM_COL32(0x71, 0x71, 0x71, 255), Group);
        TextR(362, 17, 10, IM_COL32(0x7c, 0x7c, 0x7c, 255), "Changes apply live");
    }
};

// ---------------------------------------------------------------------------
// The Sun inspector as the HTML actually builds it: BuildSunInspectorDashboard
// (SunInspectorDashboard.js) in the sun-observatory CSS family (SunInspector.css
// + the shared co-* rules in CloudInspector.css, second design pass). This is
// NOT the generic card vocabulary — it has its own header, tablist, KPI
// readouts, canvas solar-vector instrument, ObservatorySliders and preset row.
// Drawn here with the Overview pane active, as the HTML opens. State seeded
// from IlluminationHost defaults (elevation 38, azimuth 145, preset "Custom").
// ---------------------------------------------------------------------------
void DrawSunDashboard(int Pane = 0)
{
    ImDrawList* Ink = ImGui::GetWindowDrawList();
    ImFont* Font = ImGui::GetFont();
    const float W = 380.0f;
    auto Text = [&](float X, float Y, float Size, ImU32 Colour, const std::string& Value) {
        Ink->AddText(Font, Size, ImVec2(X, Y), Colour, Value.c_str());
    };
    auto TextR = [&](float Right, float Y, float Size, ImU32 Colour, const std::string& Value) {
        Text(Right - Font->CalcTextSizeA(Size, 1e9f, 0.0f, Value.c_str()).x, Y, Size, Colour, Value);
    };
    auto TextC = [&](float Centre, float Y, float Size, ImU32 Colour, const std::string& Value) {
        Text(Centre - Font->CalcTextSizeA(Size, 1e9f, 0.0f, Value.c_str()).x * 0.5f, Y, Size, Colour, Value);
    };
    auto TextWidthOf = [&](ImFont* F, float Size, const std::string& Value) {
        return F->CalcTextSizeA(Size, 1e9f, 0.0f, Value.c_str()).x;
    };
    auto Rect = [&](float X, float Y, float R, float B, ImU32 Fill, float Radius) {
        Ink->AddRectFilled(ImVec2(X, Y), ImVec2(R, B), Fill, Radius);
    };
    auto RectLine = [&](float X, float Y, float R, float B, ImU32 Edge, float Radius) {
        Ink->AddRect(ImVec2(X, Y), ImVec2(R, B), Edge, Radius, 0, 1.0f);
    };
    // Shell top bar (#inspector-top of the generic shell) + observatory ground.
    Rect(0, 0, W, 5000, IM_COL32(0x19, 0x19, 0x17, 255), 0);
    Rect(0, 0, W, 44, IM_COL32(0x14, 0x14, 0x14, 255), 0);
    Ink->AddRectFilled(ImVec2(0, 44), ImVec2(W, 45), IM_COL32(0x2a, 0x2a, 0x2a, 255));
    Text(18, 16, 11, IM_COL32(0xbf, 0xbf, 0xbf, 255), "Inspector");
    Text(18 + Font->CalcTextSizeA(11, 1e9f, 0.0f, "Inspector").x + 8, 16, 11, IM_COL32(0x71, 0x71, 0x71, 255), ">");
    Text(18 + Font->CalcTextSizeA(11, 1e9f, 0.0f, "Inspector > ").x + 8, 16, 11, IM_COL32(0x71, 0x71, 0x71, 255), "Environment");
    TextR(W - 18, 17, 10, IM_COL32(0x7c, 0x7c, 0x7c, 255), "Changes apply live");
    const ImU32 InkMain = IM_COL32(0xe8, 0xe7, 0xdf, 255), Muted = IM_COL32(0xa1, 0x97, 0x86, 255);
    const float L = 18.0f, R = W - 18.0f;
    float Y = 44.0f + 22.0f;
    // .co-header
    Text(L, Y, 9, IM_COL32(0xa0, 0x96, 0x80, 255), "E N V I R O N M E N T   /   0 2");
    Y += 17.0f + 11.0f;
    Text(L, Y, 31, InkMain, "Sun");
    const float ResetW = Font->CalcTextSizeA(9, 1e9f, 0.0f, "Reset \xE2\x86\x97").x + 20.0f;
    Rect(R - ResetW, Y + 5, R, Y + 5 + 28, IM_COL32(0xe8, 0xc7, 0x8b, 0x0b), 22);
    RectLine(R - ResetW, Y + 5, R, Y + 5 + 28, IM_COL32(0xe8, 0xc7, 0x8b, 0x22), 22);
    TextC(R - ResetW * 0.5f, Y + 5 + 9, 9, IM_COL32(0xd5, 0xbf, 0x95, 255), "Reset \xE2\x86\x97");
    Y += 36.0f + 9.0f;
    Text(L, Y, 11, Muted, "Directional light / position & colour.");
    Y += 15.0f + 20.0f;
    // .co-tabs (Overview active)
    {
        const char* Tabs[] = {"Overview", "Daylight", "Light & colour", "Disc & optics"};
        const float TW = (R - L - 9.0f) / 4.0f;
        for (int I = 0; I < 4; ++I)
        {
            const float X = L + I * (TW + 3.0f);
            if (I == Pane) Rect(X, Y, X + TW, Y + 33, IM_COL32(0x34, 0x33, 0x29, 255), 24);
            TextC(X + TW * 0.5f, Y + 11, 10, I == Pane ? IM_COL32(0xe7, 0xdf, 0xc8, 255) : IM_COL32(0x8d, 0x93, 0x86, 255), Tabs[I]);
        }
        Y += 33.0f + 20.0f;
    }
    if (Pane == 0)
    {
    // Card: Solar vector (01 / DIRECTION)
    {
        const float CardY = Y;
        Rect(L, CardY, R, CardY + 316, IM_COL32(0x23, 0x24, 0x1e, 255), 24);
        Text(L + 18, CardY + 16, 12, IM_COL32(0xe7, 0xdf, 0xcd, 255), "Solar vector");
        TextR(R - 18, CardY + 18, 8, IM_COL32(0xa0, 0x96, 0x7d, 255), "01 / DIRECTION");
        // Floating readout (.so-vector-top, absolute at card top + 49)
        Text(L + 18, CardY + 49, 9, IM_COL32(0xad, 0x9f, 0x83, 255), "Elevation");
        Text(L + 18, CardY + 62, 37, InkMain, "38.0");
        Text(L + 18 + Font->CalcTextSizeA(37, 1e9f, 0.0f, "38.0").x + 3, CardY + 62 + 16, 17, Muted, "\xC2\xB0");
        const float TagW = Font->CalcTextSizeA(9, 1e9f, 0.0f, "Manual \xE2\x86\x97").x + 20.0f;
        Rect(R - 18 - TagW, CardY + 49, R - 18, CardY + 49 + 29, IM_COL32(0x30, 0x31, 0x28, 255), 20);
        TextC(R - 18 - TagW * 0.5f, CardY + 49 + 9, 9, IM_COL32(0xe4, 0xdb, 0xbd, 255), "Manual \xE2\x86\x97");
        // Diagram (640x420 canvas scaled to inner width)
        const float DX = L + 18.0f, DW = R - L - 36.0f, Scale = DW / 640.0f, DY = CardY + 49 + 14.0f;
        const ImVec2 O(DX + 320.0f * Scale, DY + 210.0f * Scale);
        for (float Radius : {45.0f, 90.0f, 135.0f, 180.0f})
            Ink->AddCircle(O, Radius * Scale, Radius == 90.0f ? IM_COL32(0xd9, 0xc2, 0x9b, 0x70) : IM_COL32(0xd9, 0xc2, 0x9b, 0x1c), 64, 1.0f);
        for (int I = 0; I < 24; ++I)
        {
            const float A = I / 24.0f * 6.2831853f;
            Ink->AddLine(ImVec2(O.x + std::cos(A) * 174.0f * Scale, O.y + std::sin(A) * 174.0f * Scale),
                         ImVec2(O.x + std::cos(A) * 180.0f * Scale, O.y + std::sin(A) * 180.0f * Scale), IM_COL32(0xe8, 0xc7, 0x8b, 0x55), 1.0f);
        }
        for (float T = 0.0f; T < 1.0f; T += 0.06f)   // dashed cross
        {
            Ink->AddLine(ImVec2(DX + (120.0f + T * 400.0f) * Scale, O.y), ImVec2(DX + (120.0f + (T + 0.03f) * 400.0f) * Scale, O.y), IM_COL32(0xe8, 0xc7, 0x8b, 0x20), 1.0f);
            Ink->AddLine(ImVec2(O.x, DY + (15.0f + T * 390.0f) * Scale), ImVec2(O.x, DY + (15.0f + (T + 0.03f) * 390.0f) * Scale), IM_COL32(0xe8, 0xc7, 0x8b, 0x20), 1.0f);
        }
        const float Az = 145.0f * 3.14159265f / 180.0f, Rad = (90.0f - 38.0f) * Scale;
        const ImVec2 Q(O.x + std::cos(Az) * Rad, O.y - std::sin(Az) * Rad);
        Ink->AddLine(O, Q, IM_COL32(0xe8, 0xc7, 0x8b, 255), 1.5f);
        Ink->AddCircleFilled(O, 4.0f * Scale + 1.0f, IM_COL32(0xd9, 0xca, 0xb0, 255));
        Ink->AddCircle(Q, 20.0f * Scale, IM_COL32(0xe8, 0xc7, 0x8b, 0x70), 32, 1.0f);
        Ink->AddCircleFilled(Q, 20.0f * Scale, IM_COL32(0xe8, 0xc7, 0x8b, 0x16));
        Ink->AddCircleFilled(Q, 6.0f * Scale + 1.5f, IM_COL32(0xf0, 0xd6, 0x9f, 255));
        TextC(O.x, DY + 2, 8, IM_COL32(0xa4, 0x98, 0x86, 255), "+Y / N");
        Text(DX + 532.0f * Scale, O.y + 2, 8, IM_COL32(0xa4, 0x98, 0x86, 255), "+X / E");
        TextR(DX + 108.0f * Scale, O.y + 2, 8, IM_COL32(0xa4, 0x98, 0x86, 255), "W");
        TextC(O.x, DY + 408.0f * Scale - 8, 8, IM_COL32(0xa4, 0x98, 0x86, 255), "S");
        Text(DX + 419.0f * Scale, O.y - 16.0f * Scale, 7, IM_COL32(0xab, 0x9d, 0x88, 255), "0\xC2\xB0 HORIZON");
        Text(DX + 24.0f * Scale, DY + 389.0f * Scale, 7, IM_COL32(0xab, 0x9d, 0x88, 255), "DRAG VECTOR");
        // .co-atlas-footer
        const float FootY = CardY + 316 - 30.0f;
        Ink->AddRectFilled(ImVec2(L + 18, FootY), ImVec2(R - 18, FootY + 1), IM_COL32(0xe8, 0xc7, 0x8b, 0x12));
        Text(L + 18, FootY + 11, 8, IM_COL32(0xad, 0x9f, 0x83, 255), "WORLD XYZ \xC2\xB7 Z UP");
        TextR(R - 18, FootY + 11, 8, IM_COL32(0xad, 0x9f, 0x83, 255), "-0.65 / 0.45 / 0.62");
        Y = CardY + 316 + 14.0f;
    }
    // .co-kpis
    {
        const float KW = (R - L - 14.0f) / 3.0f;
        struct Kpi { const char* Label; const char* Value; const char* Unit; };
        const Kpi Kpis[] = {{"Azimuth", "145.0", "\xC2\xB0 / +X east"}, {"Source tint", "5778 K", "source setting"}, {"Sunlight", "12.0", "\xC3\x97 multiplier"}};
        for (int I = 0; I < 3; ++I)
        {
            const float X = L + I * (KW + 7.0f);
            const bool Accent = I == 2;
            Rect(X, Y, X + KW, Y + 66, Accent ? IM_COL32(0xd7, 0xdb, 0xb8, 255) : IM_COL32(0x26, 0x27, 0x20, 255), 17);
            Text(X + 10, Y + 12, 8, Accent ? IM_COL32(0x62, 0x6b, 0x51, 255) : IM_COL32(0xad, 0x9f, 0x83, 255), Kpis[I].Label);
            Text(X + 10, Y + 25, 24, Accent ? IM_COL32(0x29, 0x2e, 0x20, 255) : InkMain, Kpis[I].Value);
            Text(X + 10, Y + 52, 8, Accent ? IM_COL32(0x62, 0x6b, 0x51, 255) : Muted, Kpis[I].Unit);
        }
        Y += 66.0f + 12.0f;
    }
    // Quick tiles (.ne-tiles) — Sunlight / Sun disc on, Day cycle off
    {
        const float TW = (R - L - 18.0f) / 3.0f;
        struct Tile { const char* Name; bool On; };
        const Tile Tiles[] = {{"Sunlight", true}, {"Sun disc", true}, {"Day cycle", false}};
        for (int I = 0; I < 3; ++I)
        {
            const float X = L + I * (TW + 9.0f);
            Rect(X, Y, X + TW, Y + 106, IM_COL32(0x20, 0x20, 0x20, 255), 16);
            RectLine(X, Y, X + TW, Y + 106, Tiles[I].On ? IM_COL32(0x32, 0x32, 0x32, 255) : IM_COL32(0x49, 0x32, 0x32, 255), 16);
            const ImVec2 C(X + TW * 0.5f, Y + 13 + 15.5f);
            Ink->AddCircleFilled(C, 15.5f, Tiles[I].On ? IM_COL32(0x69, 0xc8, 0x84, 255) : IM_COL32(0xcc, 0x76, 0x73, 255));
            Ink->AddCircle(C, 5.0f, Tiles[I].On ? IM_COL32(0x16, 0x32, 0x1e, 255) : IM_COL32(0x37, 0x1d, 0x1c, 255), 16, 1.4f);
            for (int Ray = 0; Ray < 8; ++Ray)
            {
                const float A = Ray / 8.0f * 6.2831853f;
                Ink->AddLine(ImVec2(C.x + std::cos(A) * 7.0f, C.y + std::sin(A) * 7.0f), ImVec2(C.x + std::cos(A) * 10.0f, C.y + std::sin(A) * 10.0f), Tiles[I].On ? IM_COL32(0x16, 0x32, 0x1e, 255) : IM_COL32(0x37, 0x1d, 0x1c, 255), 1.2f);
            }
            TextC(X + TW * 0.5f, Y + 58, 10, IM_COL32(0xc8, 0xc8, 0xc8, 255), Tiles[I].Name);
            TextC(X + TW * 0.5f, Y + 78, 8, Tiles[I].On ? IM_COL32(0x69, 0xc8, 0x84, 255) : IM_COL32(0xcc, 0x76, 0x73, 255), Tiles[I].On ? "ON" : "OFF");
        }
        Y += 106.0f + 16.0f;
    }
    // Direction controls (02 / AIM) — transparent card, top hairline
    {
        Ink->AddRectFilled(ImVec2(L, Y), ImVec2(R, Y + 1), IM_COL32(0xff, 0xff, 0xff, 0x13));
        Y += 17.0f;
        Text(L, Y, 12, IM_COL32(0xe0, 0xe3, 0xd8, 255), "Direction controls");
        TextR(R, Y + 2, 8, IM_COL32(0x8c, 0x94, 0x82, 255), "02 / AIM");
        Y += 13.0f + 11.0f;
        auto Slider = [&](const char* Label, const char* Value, const char* Unit, float Fraction) {
            Text(L, Y + 4, 11, IM_COL32(0xb3, 0xa5, 0x8a, 255), Label);
            const float CapW = 88.0f;
            Rect(R - CapW, Y, R, Y + 24, IM_COL32(0x08, 0x08, 0x08, 255), 20);
            const std::string Num = Value;
            TextR(R - 22, Y + 5, 18, IM_COL32(0xe9, 0xe9, 0xe9, 255), Num);
            Text(R - 20, Y + 8, 10, IM_COL32(0xb2, 0xb2, 0xaa, 255), Unit);
            Y += 24.0f + 12.0f;
            Rect(L, Y, R, Y + 22, IM_COL32(0x17, 0x18, 0x18, 255), 20);
            const float FillR = L + (R - L) * Fraction;
            if (FillR > L + 20.0f) Rect(L, Y, FillR, Y + 22, IM_COL32(0x55, 0x56, 0x50, 255), 20);
            Ink->AddCircleFilled(ImVec2(FillR - 11.0f, Y + 11.0f), 10.0f, IM_COL32(0xe2, 0xe4, 0xd8, 255));
            Y += 22.0f + 9.0f;
        };
        Slider("Azimuth", "145.0", "\xC2\xB0", (145.0f + 180.0f) / 360.0f);
        Slider("Elevation", "38.0", "\xC2\xB0", (38.0f + 90.0f) / 180.0f);
        // .co-caption (wrapped at 10 px / 1.6 in 344 px)
        Text(L, Y, 10, IM_COL32(0xa3, 0x97, 0x7e, 255), "Inner horizon ring: 0\xC2\xB0. Centre: zenith. Outer region: below horizon.");
        Text(L, Y + 16, 10, IM_COL32(0xa3, 0x97, 0x7e, 255), "Editing direction stops the day cycle and switches to manual.");
        Y += 32.0f + 13.0f;
    }
    // Preset row (.so-presets)
    {
        const char* Names[] = {"Noon", "Golden", "Dusk", "Night"};
        const float BW = (R - L - 18.0f) / 4.0f;
        for (int I = 0; I < 4; ++I)
        {
            const float X = L + I * (BW + 6.0f);
            Rect(X, Y, X + BW, Y + 34, IM_COL32(0x25, 0x23, 0x1c, 255), 15);
            RectLine(X, Y, X + BW, Y + 34, IM_COL32(0xe8, 0xc7, 0x8b, 0x12), 15);
            TextC(X + BW * 0.5f, Y + 12, 10, IM_COL32(0xbb, 0xae, 0x91, 255), Names[I]);
        }
        Y += 34.0f + 12.0f;
    }
    }
    else if (Pane == 1)
    {
        // Solar day (03 / EPHEMERIS)
        const float CardY = Y, InnerL = L + 18.0f, InnerR = R - 18.0f;
        float CY = CardY + 18.0f;
        Rect(L, CardY, R, CardY + 560, IM_COL32(0x22, 0x24, 0x1f, 255), 24);
        Text(InnerL, CY, 12, IM_COL32(0xe7, 0xdf, 0xcd, 255), "Solar day");
        TextR(InnerR, CY + 2, 8, IM_COL32(0xa0, 0x96, 0x7d, 255), "03 / EPHEMERIS");
        CY += 12.0f + 14.0f;
        Text(InnerL, CY, 9, IM_COL32(0xad, 0x9f, 0x83, 255), "Local time");
        Text(InnerL, CY + 14, 31, InkMain, "12:00");
        {
            const std::string Tag = "Manual direction";
            const float TW2 = TextWidthOf(Font, 9, Tag) + 20.0f;
            Rect(InnerR - TW2, CY + 4, InnerR, CY + 4 + 27, IM_COL32(0xe8, 0xc7, 0x8b, 0x0c), 20);
            RectLine(InnerR - TW2, CY + 4, InnerR, CY + 4 + 27, IM_COL32(0xe8, 0xc7, 0x8b, 0x28), 20);
            TextC(InnerR - TW2 * 0.5f, CY + 13, 9, IM_COL32(0xe8, 0xc7, 0x8b, 255), Tag);
        }
        CY += 31.0f + 14.0f;
        // Day curve 640x320 (spherical model: lat -26.18, declination -10.3, date 2026-10-20)
        {
            const float DW = InnerR - InnerL, Scale = DW / 640.0f, DH = 320.0f * Scale;
            const double Phi = -26.18 * 3.14159265 / 180.0, Dec = -10.3 * 3.14159265 / 180.0;
            auto Elev = [&](double Hour) {
                const double H = (Hour - 12.0) * 15.0 * 3.14159265 / 180.0;
                return std::asin(std::sin(Phi) * std::sin(Dec) + std::cos(Phi) * std::cos(Dec) * std::cos(H)) * 180.0 / 3.14159265;
            };
            auto YOf = [&](double E) { return CY + (160.0 - E / 90.0 * 110.0) * Scale; };
            for (double E : {-60.0, 0.0, 60.0})
            {
                const float GY = (float)YOf(E);
                for (float T2 = 0; T2 < 1.0f; T2 += 0.05f)
                    Ink->AddLine(ImVec2(InnerL + (42.0f + T2 * 564.0f) * Scale, GY), ImVec2(InnerL + (42.0f + (T2 + 0.025f) * 564.0f) * Scale, GY), E == 0 ? IM_COL32(0xe3, 0xca, 0x99, 0x5c) : IM_COL32(0xe3, 0xca, 0x99, 0x1a), 1.0f);
                char Buf[16]; std::snprintf(Buf, sizeof(Buf), "%.0f", E);
                TextR(InnerL + 32.0f * Scale, GY - 4, 7, IM_COL32(0x9c, 0x94, 0x7f, 255), std::string(Buf) + "\xC2\xB0");
            }
            ImVec2 Prev; bool Have = false, HaveLit = false; ImVec2 PrevLit;
            for (int I = 0; I <= 96; ++I)
            {
                const double Hour = I / 4.0, E = Elev(Hour);
                const ImVec2 Pt(InnerL + (42.0f + Hour / 24.0 * 564.0f) * Scale, (float)YOf(E));
                if (Have) Ink->AddLine(Prev, Pt, IM_COL32(0x9f, 0x99, 0x7a, 0x99), 1.2f);
                if (E >= 0) { if (HaveLit) Ink->AddLine(PrevLit, Pt, IM_COL32(0xef, 0xd3, 0x97, 255), 2.0f); PrevLit = Pt; HaveLit = true; }
                else HaveLit = false;
                Prev = Pt; Have = true;
            }
            const float PX = InnerL + (42.0f + 12.0f / 24.0f * 564.0f) * Scale;
            for (float T2 = 0; T2 < 1.0f; T2 += 0.08f)
                Ink->AddLine(ImVec2(PX, CY + (30.0f + T2 * 250.0f) * Scale), ImVec2(PX, CY + (30.0f + (T2 + 0.04f) * 250.0f) * Scale), IM_COL32(0xe8, 0xc7, 0x8b, 0x66), 1.0f);
            for (int Hour : {0, 6, 12, 18, 24})
            {
                char Buf[8]; std::snprintf(Buf, sizeof(Buf), "%02d:00", Hour);
                TextC(InnerL + (42.0f + Hour / 24.0 * 564.0f) * Scale, CY + 304.0f * Scale - 6.0f, 7, IM_COL32(0xa3, 0x99, 0x82, 255), Buf);
            }
            CY += DH + 12.0f;
            // .so-day-metrics
            Ink->AddRectFilled(ImVec2(InnerL, CY), ImVec2(InnerR, CY + 1), IM_COL32(0xe8, 0xc7, 0x8b, 0x12));
            CY += 13.0f;
            const double CosH = -std::tan(Phi) * std::tan(Dec);
            const double Hh = std::acos(std::max(-1.0, std::min(1.0, CosH))) * 180.0 / 3.14159265 / 15.0;
            auto Clock = [](double H) { const int M = (int)std::lround(H * 60); char B[8]; std::snprintf(B, sizeof(B), "%02d:%02d", (M / 60) % 24, M % 60); return std::string(B); };
            struct M { const char* Label; std::string Value; const char* Unit; };
            const M Ms[] = {{"Horizon rise", Clock(12.0 - Hh), ""}, {"Horizon set", Clock(12.0 + Hh), ""}, {"Above horizon", "", "h"}};
            const float MW = (InnerR - InnerL - 20.0f) / 3.0f;
            for (int I = 0; I < 3; ++I)
            {
                const float X = InnerL + I * (MW + 10.0f);
                Text(X, CY, 9, IM_COL32(0xad, 0x9f, 0x83, 255), Ms[I].Label);
                std::string V = Ms[I].Value;
                if (I == 2) { char B[16]; std::snprintf(B, sizeof(B), "%.1f", 2 * Hh); V = B; }
                Text(X, CY + 14, 22, InkMain, V);
                if (Ms[I].Unit[0]) Text(X + TextWidthOf(Font, 22, V) + 4, CY + 24, 9, Muted, Ms[I].Unit);
            }
            CY += 14.0f + 22.0f + 13.0f;
        }
        Text(InnerL, CY, 10, IM_COL32(0xa3, 0x97, 0x7e, 255), "Date/location solver \xC2\xB7 geometric 0\xC2\xB0 crossings, without");
        Text(InnerL, CY + 16, 10, IM_COL32(0xa3, 0x97, 0x7e, 255), "refraction or terrain. This path is a reference while");
        Text(InnerL, CY + 32, 10, IM_COL32(0xa3, 0x97, 0x7e, 255), "direction is manual, not recorded telemetry.");
        CY += 32.0f + 16.0f + 11.0f;
        auto Slider = [&](const char* Label, const char* Value, const char* Unit, float Fraction) {
            Text(InnerL, CY + 4, 11, IM_COL32(0xb3, 0xa5, 0x8a, 255), Label);
            Rect(InnerR - 88, CY, InnerR, CY + 24, IM_COL32(0x08, 0x08, 0x08, 255), 20);
            TextR(InnerR - 22, CY + 5, 18, IM_COL32(0xe9, 0xe9, 0xe9, 255), Value);
            Text(InnerR - 20, CY + 8, 10, IM_COL32(0xb2, 0xb2, 0xaa, 255), Unit);
            CY += 36.0f;
            Rect(InnerL, CY, InnerR, CY + 22, IM_COL32(0x17, 0x18, 0x18, 255), 20);
            const float FillR = InnerL + (InnerR - InnerL) * Fraction;
            if (FillR > InnerL + 20.0f) Rect(InnerL, CY, FillR, CY + 22, IM_COL32(0x55, 0x56, 0x50, 255), 20);
            Ink->AddCircleFilled(ImVec2(FillR - 11.0f, CY + 11.0f), 10.0f, IM_COL32(0xe2, 0xe4, 0xd8, 255));
            CY += 31.0f;
        };
        Slider("Local solar control time", "12.00", "h", 0.5f);
        Slider("Full day duration", "6.00", "real h", 6.0f / 24.0f);
        Y = CardY + 560 + 14.0f;
        // Time & location (04 / COORDINATES)
        const float Card2Y = Y;
        float LY = Card2Y + 18.0f;
        Rect(L, Card2Y, R, Card2Y + 330, IM_COL32(0x20, 0x22, 0x1e, 255), 24);
        Text(InnerL, LY, 12, IM_COL32(0xe7, 0xdf, 0xcd, 255), "Time & location");
        TextR(InnerR, LY + 2, 8, IM_COL32(0xa0, 0x96, 0x7d, 255), "04 / COORDINATES");
        LY += 12.0f + 14.0f;
        Ink->AddRectFilled(ImVec2(InnerL, LY), ImVec2(InnerR, LY + 1), IM_COL32(0xe8, 0xc7, 0x8b, 0x10));
        LY += 11.0f;
        Text(InnerL, LY + 6, 10, IM_COL32(0xb8, 0xad, 0x96, 255), "Calendar date");
        Rect(InnerR - 150, LY, InnerR, LY + 26, IM_COL32(0x17, 0x16, 0x10, 255), 9);
        RectLine(InnerR - 150, LY, InnerR, LY + 26, IM_COL32(0xe8, 0xc7, 0x8b, 0x25), 9);
        Text(InnerR - 150 + 8, LY + 7, 12, IM_COL32(0xe7, 0xd8, 0xb9, 255), "2026-10-20");
        LY += 26.0f + 11.0f;
        auto LocSlider = [&](const char* Label, const char* Value, const char* Unit, float Fraction) {
            Text(InnerL, LY + 4, 11, IM_COL32(0xb3, 0xa5, 0x8a, 255), Label);
            Rect(InnerR - 88, LY, InnerR, LY + 24, IM_COL32(0x08, 0x08, 0x08, 255), 20);
            TextR(InnerR - 22, LY + 5, 18, IM_COL32(0xe9, 0xe9, 0xe9, 255), Value);
            Text(InnerR - 20, LY + 8, 10, IM_COL32(0xb2, 0xb2, 0xaa, 255), Unit);
            LY += 36.0f;
            Rect(InnerL, LY, InnerR, LY + 22, IM_COL32(0x17, 0x18, 0x18, 255), 20);
            const float FillR = InnerL + (InnerR - InnerL) * Fraction;
            if (FillR > InnerL + 20.0f) Rect(InnerL, LY, FillR, LY + 22, IM_COL32(0x55, 0x56, 0x50, 255), 20);
            Ink->AddCircleFilled(ImVec2(std::max(InnerL + 11.0f, FillR - 11.0f), LY + 11.0f), 10.0f, IM_COL32(0xe2, 0xe4, 0xd8, 255));
            LY += 31.0f;
        };
        LocSlider("Latitude", "-26.18", "\xC2\xB0", (90.0f - 26.18f) / 180.0f);
        LocSlider("Longitude", "28.32", "\xC2\xB0", (28.32f + 180.0f) / 360.0f);
        LocSlider("UTC offset", "2.0", "h", (2.0f + 12.0f) / 26.0f);
        Text(InnerL, LY + 4, 10, IM_COL32(0xa3, 0x97, 0x7e, 255), "Changing time, date or location links the Sun to the");
        Text(InnerL, LY + 20, 10, IM_COL32(0xa3, 0x97, 0x7e, 255), "existing celestial solver. The day-cycle switch stays");
        Text(InnerL, LY + 36, 10, IM_COL32(0xa3, 0x97, 0x7e, 255), "in the Overview quick toggles.");
        Y = Card2Y + 330 + 14.0f;
    }
    else if (Pane == 2)
    {
        // Light source (05 / RADIANCE)
        const float CardY = Y, InnerL = L + 18.0f, InnerR = R - 18.0f;
        float CY = CardY + 18.0f;
        Rect(L, CardY, R, CardY + 400, IM_COL32(0x23, 0x24, 0x1e, 255), 24);
        Text(InnerL, CY, 12, IM_COL32(0xe7, 0xdf, 0xcd, 255), "Light source");
        TextR(InnerR, CY + 2, 8, IM_COL32(0xa0, 0x96, 0x7d, 255), "05 / RADIANCE");
        CY += 12.0f + 14.0f;
        Text(InnerL, CY, 9, IM_COL32(0xad, 0x9f, 0x83, 255), "Sunlight intensity");
        Text(InnerL, CY + 14, 31, InkMain, "12.0");
        Text(InnerL + TextWidthOf(Font, 31, "12.0") + 4, CY + 14 + 15, 10, Muted, "\xC3\x97");
        CY += 31.0f + 14.0f + 11.0f;
        auto Slider = [&](const char* Label, const char* Value, const char* Unit, float Fraction, float NumSize = 18.0f) {
            Text(InnerL, CY + 4, 11, IM_COL32(0xb3, 0xa5, 0x8a, 255), Label);
            Rect(InnerR - 88, CY, InnerR, CY + 24, IM_COL32(0x08, 0x08, 0x08, 255), 20);
            TextR(InnerR - 22, CY + 12 - NumSize * 0.62f, NumSize, IM_COL32(0xe9, 0xe9, 0xe9, 255), Value);
            Text(InnerR - 20, CY + 8, 10, IM_COL32(0xb2, 0xb2, 0xaa, 255), Unit);
            CY += 36.0f;
            Rect(InnerL, CY, InnerR, CY + 22, IM_COL32(0x17, 0x18, 0x18, 255), 20);
            const float FillR = InnerL + (InnerR - InnerL) * Fraction;
            if (FillR > InnerL + 20.0f) Rect(InnerL, CY, FillR, CY + 22, IM_COL32(0x55, 0x56, 0x50, 255), 20);
            Ink->AddCircleFilled(ImVec2(std::max(InnerL + 11.0f, FillR - 11.0f), CY + 11.0f), 10.0f, IM_COL32(0xe2, 0xe4, 0xd8, 255));
            CY += 31.0f;
        };
        Slider("Sunlight multiplier", "12.0", "\xC3\x97", 12.0f / 60.0f);
        CY += 6.0f;
        Text(InnerL, CY, 10, IM_COL32(0xa3, 0x97, 0x7e, 255), "Exhibit intensity multiplier, not a measured lux or");
        Text(InnerL, CY + 16, 10, IM_COL32(0xa3, 0x97, 0x7e, 255), "irradiance value. Atmospheric transmission is applied");
        Text(InnerL, CY + 32, 10, IM_COL32(0xa3, 0x97, 0x7e, 255), "by the renderer.");
        CY += 32.0f + 16.0f;
        // Colour source select (.ne-select)
        Text(InnerL, CY, 11, IM_COL32(0xb3, 0xa5, 0x8a, 255), "Colour source");
        CY += 16.0f + 9.0f;
        Rect(InnerL, CY, InnerR, CY + 32, IM_COL32(0x21, 0x1e, 0x17, 255), 12);
        RectLine(InnerL, CY, InnerR, CY + 32, IM_COL32(0xe8, 0xc7, 0x8b, 0x22), 12);
        Text(InnerL + 12, CY + 10, 11, IM_COL32(0xe9, 0xd7, 0xb6, 255), "Temperature");
        TextR(InnerR - 12, CY + 10, 11, Muted, "v");
        CY += 32.0f + 11.0f;
        // Sun Tint row (.so-tint-pill)
        Ink->AddRectFilled(ImVec2(InnerL, CY), ImVec2(InnerR, CY + 1), IM_COL32(0xe8, 0xc7, 0x8b, 0x10));
        CY += 11.0f;
        Text(InnerL, CY + 10, 10, IM_COL32(0xb8, 0xad, 0x96, 255), "Sun Tint");
        const float PillW = 145.0f;
        Rect(InnerR - PillW, CY, InnerR, CY + 34, IM_COL32(0xe8, 0xc7, 0x8b, 0x0c), 20);
        RectLine(InnerR - PillW, CY, InnerR, CY + 34, IM_COL32(0xe8, 0xc7, 0x8b, 0x30), 20);
        Ink->AddCircleFilled(ImVec2(InnerR - PillW + 20, CY + 17), 8.0f, IM_COL32(0xff, 0xff, 0xff, 255));
        Text(InnerR - PillW + 36, CY + 11, 11, IM_COL32(0xe5, 0xd8, 0xba, 255), "#FFFFFF");
        TextR(InnerR - 12, CY + 11, 11, IM_COL32(0xe5, 0xd8, 0xba, 255), "\xE2\x86\x97");
        CY += 34.0f + 11.0f;
        Slider("Colour temperature", "5778", "K", (5778.0f - 2000.0f) / 8000.0f);
        Y = CardY + 400 + 14.0f;
        // Source spectrum (06 / REFERENCE)
        const float Card2Y = Y;
        float SY = Card2Y + 18.0f;
        Rect(L, Card2Y, R, Card2Y + 430, IM_COL32(0x25, 0x25, 0x1f, 255), 24);
        Text(InnerL, SY, 12, IM_COL32(0xe7, 0xdf, 0xcd, 255), "Source spectrum");
        TextR(InnerR, SY + 2, 8, IM_COL32(0xa0, 0x96, 0x7d, 255), "06 / REFERENCE");
        SY += 12.0f + 14.0f + 10.0f;
        Text(InnerL, SY, 9, IM_COL32(0xad, 0x9f, 0x83, 255), "Thermal reference");
        Text(InnerL, SY + 14, 43, InkMain, "5778");
        Text(InnerL + TextWidthOf(Font, 43, "5778") + 5, SY + 14 + 24, 10, Muted, "K");
        {
            const std::string Tag = "Temperature source";
            const float TW2 = TextWidthOf(Font, 9, Tag) + 20.0f;
            Rect(InnerR - TW2, SY + 10, InnerR, SY + 10 + 27, IM_COL32(0xe8, 0xc7, 0x8b, 0x0c), 20);
            RectLine(InnerR - TW2, SY + 10, InnerR, SY + 10 + 27, IM_COL32(0xe8, 0xc7, 0x8b, 0x28), 20);
            TextC(InnerR - TW2 * 0.5f, SY + 19, 9, IM_COL32(0xe8, 0xc7, 0x8b, 255), Tag);
        }
        SY += 43.0f + 14.0f + 12.0f;
        // Spectrum diagram 640x280 (normalised Planck curve at 5778 K)
        {
            const float DW = InnerR - InnerL, Scale = DW / 640.0f;
            auto Planck = [](double Nm, double T) {
                const double Lm = Nm * 1e-9;
                return 1.0 / (std::pow(Lm, 5.0) * std::expm1(0.01438776877 / (Lm * T)));
            };
            const double Peak = Planck(501.5, 5778.0);
            for (float GY : {28.0f, 80.0f, 132.0f, 184.0f})
            {
                const float Y2 = SY + GY * Scale;
                for (float T2 = 0; T2 < 1.0f; T2 += 0.05f)
                    Ink->AddLine(ImVec2(InnerL + (32.0f + T2 * 576.0f) * Scale, Y2), ImVec2(InnerL + (32.0f + (T2 + 0.025f) * 576.0f) * Scale, Y2), IM_COL32(0xea, 0xd6, 0xad, 0x1a), 1.0f);
            }
            const ImU32 Stops[] = {IM_COL32(0x8f, 0x81, 0xb5, 255), IM_COL32(0x82, 0xa7, 0xd5, 255), IM_COL32(0x91, 0xbd, 0xac, 255), IM_COL32(0xd6, 0xc7, 0x7e, 255), IM_COL32(0xca, 0x8a, 0x76, 255)};
            ImVec2 Prev; bool Have = false;
            for (int I = 0; I <= 64; ++I)
            {
                const double Nm = 380.0 + I / 64.0 * 400.0;
                const ImVec2 Pt(InnerL + (32.0f + (Nm - 380.0) / 400.0 * 576.0f) * Scale, SY + (204.0f - Planck(Nm, 5778.0) / Peak * 165.0f) * Scale);
                if (Have)
                {
                    const int Seg = std::min(4, (int)(I / 64.0f * 4.99f));
                    Ink->AddLine(Prev, Pt, Stops[Seg], 1.6f);
                }
                Prev = Pt; Have = true;
            }
            const float PeakX = InnerL + (32.0f + (501.5f - 380.0f) / 400.0 * 576.0f) * Scale;
            for (float T2 = 0; T2 < 1.0f; T2 += 0.1f)
                Ink->AddLine(ImVec2(PeakX, SY + (28.0f + T2 * 176.0f) * Scale), ImVec2(PeakX, SY + (28.0f + (T2 + 0.05f) * 176.0f) * Scale), IM_COL32(0xee, 0xd6, 0xa6, 0x70), 1.0f);
            Ink->AddCircleFilled(ImVec2(PeakX, SY + 39.0f * Scale), 2.5f, IM_COL32(0xf0, 0xd4, 0x9d, 255));
            TextC(PeakX, SY + 19.0f * Scale - 8.0f, 7, IM_COL32(0xc3, 0xb5, 0x96, 255), "MAX");
            for (int I = 0; I < 24; ++I)   // wavelength strip
            {
                const int Seg = std::min(4, (int)(I / 24.0f * 4.99f));
                Rect(InnerL + (32.0f + I / 24.0f * 576.0f) * Scale, SY + 218.0f * Scale, InnerL + (32.0f + (I + 1) / 24.0f * 576.0f) * Scale, SY + 225.0f * Scale, Stops[Seg], 0);
            }
            for (int Nm : {380, 480, 580, 680, 780})
            {
                char Buf[8]; std::snprintf(Buf, sizeof(Buf), "%d", Nm);
                TextC(InnerL + (32.0f + (Nm - 380) / 400.0f * 576.0f) * Scale, SY + 252.0f * Scale - 6.0f, 7, IM_COL32(0xac, 0x9f, 0x86, 255), Buf);
            }
            TextC(InnerL + DW * 0.5f, SY + 277.0f * Scale - 6.0f, 7, IM_COL32(0x87, 0x7e, 0x6a, 255), "VISIBLE WAVELENGTH / nm");
            SY += 280.0f * Scale + 12.0f;
        }
        Ink->AddRectFilled(ImVec2(InnerL, SY), ImVec2(InnerR, SY + 1), IM_COL32(0xe8, 0xc7, 0x8b, 0x19));
        SY += 16.0f;
        const float SW = (InnerR - InnerL - 12.0f) / 2.0f;
        Text(InnerL, SY, 9, IM_COL32(0xad, 0x9f, 0x83, 255), "Blackbody peak");
        Text(InnerL, SY + 14, 24, InkMain, "502");
        Text(InnerL + TextWidthOf(Font, 24, "502") + 4, SY + 28, 10, Muted, "nm");
        Text(InnerL + SW + 12.0f, SY, 9, IM_COL32(0xad, 0x9f, 0x83, 255), "Visible window");
        Text(InnerL + SW + 12.0f, SY + 14, 24, InkMain, "380-780");
        Text(InnerL + SW + 12.0f + TextWidthOf(Font, 24, "380-780") + 4, SY + 28, 10, Muted, "nm");
        SY += 14.0f + 24.0f + 12.0f;
        Text(InnerL, SY, 10, IM_COL32(0xa3, 0x97, 0x7e, 255), "Normalised thermal reference, not measured radiance.");
        Text(InnerL, SY + 16, 10, IM_COL32(0xa3, 0x97, 0x7e, 255), "The renderer uses its existing RGB approximation; RGB");
        Text(InnerL, SY + 32, 10, IM_COL32(0xa3, 0x97, 0x7e, 255), "mode uses your custom tint. The blackbody peak may");
        Text(InnerL, SY + 48, 10, IM_COL32(0xa3, 0x97, 0x7e, 255), "fall outside the visible window.");
        Y = Card2Y + 430 + 14.0f;
    }
    else if (Pane == 3)
    {
        // Apparent solar disc (07 / ANGULAR SIZE)
        const float CardY = Y, InnerL = L + 18.0f, InnerR = R - 18.0f;
        float CY = CardY + 18.0f;
        Rect(L, CardY, R, CardY + 400, IM_COL32(0x24, 0x25, 0x1f, 255), 24);
        Text(InnerL, CY, 12, IM_COL32(0xe7, 0xdf, 0xcd, 255), "Apparent solar disc");
        TextR(InnerR, CY + 2, 8, IM_COL32(0xa0, 0x96, 0x7d, 255), "07 / ANGULAR SIZE");
        CY += 12.0f + 14.0f;
        Text(InnerL, CY, 9, IM_COL32(0xad, 0x9f, 0x83, 255), "Angular diameter");
        Text(InnerL, CY + 14, 31, InkMain, "0.54");
        Text(InnerL + TextWidthOf(Font, 31, "0.54") + 4, CY + 14 + 15, 10, Muted, "\xC2\xB0");
        CY += 31.0f + 14.0f;
        // Disc diagram 640x280
        {
            const float DW = InnerR - InnerL, Scale = DW / 640.0f, DH = 280.0f * Scale;
            const ImVec2 O(InnerL + 320.0f * Scale, CY + 130.0f * Scale);
            for (float Radius : {45.0f, 85.0f, 118.0f})
                Ink->AddCircle(O, Radius * Scale, IM_COL32(0xe8, 0xc7, 0x8b, 0x22), 48, 1.0f);
            for (float T2 = 0; T2 < 1.0f; T2 += 0.06f)
            {
                Ink->AddLine(ImVec2(InnerL + (180.0f + T2 * 280.0f) * Scale, O.y), ImVec2(InnerL + (180.0f + (T2 + 0.03f) * 280.0f) * Scale, O.y), IM_COL32(0xe8, 0xc7, 0x8b, 0x2a), 1.0f);
                Ink->AddLine(ImVec2(O.x, CY + (5.0f + T2 * 250.0f) * Scale), ImVec2(O.x, CY + (5.0f + (T2 + 0.03f) * 250.0f) * Scale), IM_COL32(0xe8, 0xc7, 0x8b, 0x2a), 1.0f);
            }
            const float Rad = (12.0f + 0.27f / 2.0f * 76.0f) * Scale;
            Ink->AddCircleFilled(O, Rad + 20.0f * Scale, IM_COL32(0xe8, 0xc7, 0x8b, 0x12));
            Ink->AddCircleFilled(O, Rad, IM_COL32(0xf1, 0xd7, 0xa6, 255));
            Ink->AddLine(ImVec2(O.x - Rad, O.y), ImVec2(O.x + Rad, O.y), IM_COL32(0xff, 0xf3, 0xcf, 0x66), 1.0f);
            Text(InnerL + 24.0f * Scale, CY + 261.0f * Scale - 6.0f, 7, IM_COL32(0xac, 0x9e, 0x83, 255), "ANGULAR SIZE / EXAGGERATED");
            CY += DH + 12.0f;
        }
        auto Slider = [&](const char* Label, const char* Value, const char* Unit, float Fraction) {
            Text(InnerL, CY + 4, 11, IM_COL32(0xb3, 0xa5, 0x8a, 255), Label);
            Rect(InnerR - 88, CY, InnerR, CY + 24, IM_COL32(0x08, 0x08, 0x08, 255), 20);
            TextR(InnerR - 22, CY + 5, 18, IM_COL32(0xe9, 0xe9, 0xe9, 255), Value);
            Text(InnerR - 20, CY + 8, 10, IM_COL32(0xb2, 0xb2, 0xaa, 255), Unit);
            CY += 36.0f;
            Rect(InnerL, CY, InnerR, CY + 22, IM_COL32(0x17, 0x18, 0x18, 255), 20);
            const float FillR = InnerL + (InnerR - InnerL) * Fraction;
            if (FillR > InnerL + 20.0f) Rect(InnerL, CY, FillR, CY + 22, IM_COL32(0x55, 0x56, 0x50, 255), 20);
            Ink->AddCircleFilled(ImVec2(std::max(InnerL + 11.0f, FillR - 11.0f), CY + 11.0f), 10.0f, IM_COL32(0xe2, 0xe4, 0xd8, 255));
            CY += 31.0f;
        };
        Slider("Angular diameter", "0.54", "\xC2\xB0", (0.27f - 0.05f) / 1.95f);
        Slider("Optical aureole", "1.00", "\xC3\x97", 1.0f / 3.0f);
        Text(InnerL, CY + 4, 10, IM_COL32(0xa3, 0x97, 0x7e, 255), "Disc diagram is exaggerated for editing; the sky uses");
        Text(InnerL, CY + 20, 10, IM_COL32(0xa3, 0x97, 0x7e, 255), "the actual angular diameter. The Sun disc quick toggle");
        Text(InnerL, CY + 36, 10, IM_COL32(0xa3, 0x97, 0x7e, 255), "remains in Overview.");
        Y = CardY + 400 + 14.0f;
        // Connected optics (08 / SUBCOMPONENT)
        const float Card2Y = Y;
        float OY = Card2Y + 18.0f;
        Rect(L, Card2Y, R, Card2Y + 150, IM_COL32(0, 0, 0, 0), 18);
        RectLine(L, Card2Y, R, Card2Y + 150, IM_COL32(0xff, 0xff, 0xff, 0x15), 18);
        Text(InnerL, OY, 12, IM_COL32(0xe7, 0xdf, 0xcd, 255), "Connected optics");
        TextR(InnerR, OY + 2, 8, IM_COL32(0xa0, 0x96, 0x7d, 255), "08 / SUBCOMPONENT");
        OY += 12.0f + 14.0f;
        Text(InnerL, OY, 10, IM_COL32(0xa3, 0x97, 0x7e, 255), "Lens flare is a separate optical subcomponent. Opening");
        Text(InnerL, OY + 16, 10, IM_COL32(0xa3, 0x97, 0x7e, 255), "it does not change sunlight or exposure.");
        OY += 32.0f + 13.0f;
        Rect(InnerL, OY, InnerR, OY + 42, IM_COL32(0xe8, 0xc7, 0x8b, 0x0c), 18);
        RectLine(InnerL, OY, InnerR, OY + 42, IM_COL32(0xe8, 0xc7, 0x8b, 0x22), 18);
        TextC((InnerL + InnerR) * 0.5f, OY + 15, 10, IM_COL32(0xe8, 0xc7, 0x8b, 255), "Open Lens Flare   \xE2\x86\x97");
        Y = Card2Y + 150 + 14.0f;
    }
    // .co-footer
    Ink->AddRectFilled(ImVec2(L, Y), ImVec2(R, Y + 1), IM_COL32(0xe8, 0xc7, 0x8b, 0x10));
    Text(L, Y + 15, 8, IM_COL32(0xa2, 0x94, 0x73, 255), "DIRECTIONAL LIGHT / Z UP");
    TextR(R, Y + 15, 8, IM_COL32(0xa2, 0x94, 0x73, 255), "Settings apply live \xE2\x86\x97");
}


// ---------------------------------------------------------------------------
// The Moon inspector as the HTML builds it: BuildMoonInspectorInfographics in
// the moon-cards CSS family (MoonCards.css). Header + Moon settings card +
// InspectorCategories (Phase pane active, as the HTML opens) with the Lunar
// phase card. The phase preview is the HTML's textured-sphere Diagram; the
// registered moon texture is not in this harness, so the disc is shaded by
// phase geometry instead (see PARITY.md). Defaults: EnvironmentSettings
// (phase 14.765 d = Full stop pressed, azimuth -8, elevation 38, size 25).
// ---------------------------------------------------------------------------
void DrawMoonDashboard()
{
    PageInk P{ImGui::GetWindowDrawList(), ImGui::GetFont()};
    P.Rect(0, 0, 380, 5000, IM_COL32(0x19, 0x1b, 0x1e, 255), 0);
    P.TopBar("Environment");
    const ImU32 InkMain = IM_COL32(0xe1, 0xe5, 0xeb, 255), Muted = IM_COL32(0x97, 0xa3, 0xb3, 255);
    const float L = 18.0f, R = 362.0f;
    float Y = 44.0f + 22.0f;
    // .ne-header
    P.Text(L, Y, 9, IM_COL32(0x84, 0x90, 0xa1, 255), "Inspector / Environment");
    Y += 13.0f + 9.0f;
    P.Text(L, Y, 29, InkMain, "Moons");
    Y += 34.0f + 8.0f;
    P.Text(L, Y, 11, Muted, "Scene bodies \xC2\xB7 phase \xC2\xB7 surface light");
    Y += 15.0f + 20.0f;
    // Moon settings card (fixed 178 px): title, instance select (top right), tiles
    {
        const float CardY = Y, CardH = 178.0f;
        P.Rect(L, CardY, R, CardY + CardH, IM_COL32(0x24, 0x27, 0x2b, 255), 22);
        P.RectLine(L, CardY, R, CardY + CardH, IM_COL32(0xb1, 0xc7, 0xe1, 0x1b), 22);
        P.Text(L + 18, CardY + 16, 12, InkMain, "Moon settings");
        // .ne-select (123 px, top right)
        const float SX = R - 18 - 123.0f;
        P.Rect(SX, CardY + 14, R - 18, CardY + 14 + 32, IM_COL32(0x28, 0x28, 0x28, 255), 12);
        P.RectLine(SX, CardY + 14, R - 18, CardY + 14 + 32, IM_COL32(0x3e, 0x3e, 0x3e, 255), 12);
        P.Text(SX + 12, CardY + 14 + 10, 11, IM_COL32(0xe9, 0xe9, 0xe9, 255), "1 \xC2\xB7 Luna");
        P.TextR(R - 18 - 10, CardY + 14 + 10, 11, IM_COL32(0x91, 0x91, 0x91, 255), "v");
        // .ne-tiles (top 52): Visible on, Follow sky off
        const float TW = (R - L - 36.0f - 9.0f) / 2.0f;
        struct Tile { const char* Name; bool On; };
        const Tile Tiles[] = {{"Visible", true}, {"Follow sky", false}};
        for (int I = 0; I < 2; ++I)
        {
            const float X = L + 18 + I * (TW + 9.0f), TY = CardY + 52.0f;
            P.Rect(X, TY, X + TW, TY + 106, IM_COL32(0x20, 0x20, 0x20, 255), 16);
            P.RectLine(X, TY, X + TW, TY + 106, Tiles[I].On ? IM_COL32(0x32, 0x32, 0x32, 255) : IM_COL32(0x49, 0x32, 0x32, 255), 16);
            const ImVec2 C(X + TW * 0.5f, TY + 13 + 15.5f);
            P.Ink->AddCircleFilled(C, 15.5f, Tiles[I].On ? IM_COL32(0x69, 0xc8, 0x84, 255) : IM_COL32(0xcc, 0x76, 0x73, 255));
            P.Ink->AddCircle(C, 5.0f, Tiles[I].On ? IM_COL32(0x16, 0x32, 0x1e, 255) : IM_COL32(0x37, 0x1d, 0x1c, 255), 16, 1.4f);
            P.TextC(X + TW * 0.5f, TY + 58, 10, IM_COL32(0xc8, 0xc8, 0xc8, 255), Tiles[I].Name);
            P.TextC(X + TW * 0.5f, TY + 78, 8, Tiles[I].On ? IM_COL32(0x69, 0xc8, 0x84, 255) : IM_COL32(0xcc, 0x76, 0x73, 255), Tiles[I].On ? "ON" : "OFF");
        }
        Y = CardY + CardH + 15.0f;
    }
    // InspectorCategories: Phase / Placement / Light & size / Bodies
    {
        const char* Tabs[] = {"Phase", "Placement", "Light & size", "Bodies"};
        Y = P.Tabs(L, R, Y + 3.0f, Tabs, 4, IM_COL32(0x34, 0x3d, 0x46, 255), IM_COL32(0xe1, 0xea, 0xf4, 255)) + 20.0f;
    }
    // Lunar phase card
    {
        const float CardY = Y;
        const float InnerL = L + 18.0f, InnerR = R - 18.0f;
        float CY = CardY + 18.0f;
        P.Rect(L, CardY, R, CardY + 470, IM_COL32(0x24, 0x27, 0x2b, 255), 22);
        P.RectLine(L, CardY, R, CardY + 470, IM_COL32(0xb1, 0xc7, 0xe1, 0x1b), 22);
        P.Text(InnerL, CY, 12, InkMain, "Lunar phase");
        CY += 12.0f + 20.0f;
        // .mc-phase-summary: age strong, phase name, lit fraction
        P.Text(InnerL, CY, 29, InkMain, "14.8 d");
        P.Text(InnerL + 130.0f, CY + 17, 10, IM_COL32(0xa5, 0xb6, 0xca, 255), "Full moon");
        P.TextR(InnerR, CY + 17, 10, IM_COL32(0xd7, 0xe3, 0xf1, 255), "100.0% lit");
        CY += 29.0f + 12.0f;
        // Diagram 640x440: textured disc at (135,10,370,370), body name at (320,424).
        const float DW = InnerR - InnerL, Scale = DW / 640.0f, DH = 440.0f * Scale;
        const ImVec2 C(InnerL + 320.0f * Scale, CY + 195.0f * Scale);
        const float Rad = 185.0f * Scale;
        P.Ink->AddCircleFilled(C, Rad + 10.0f, IM_COL32(0xa8, 0xbc, 0xd9, 0x10));
        P.Ink->AddCircleFilled(C, Rad, IM_COL32(0xd7, 0xe3, 0xf1, 255));
        P.Ink->AddCircle(C, Rad, IM_COL32(0xa8, 0xbc, 0xd9, 0x60), 48, 1.0f);
        for (int I = 0; I < 7; ++I)   // crater shading stand-in for the registered texture
        {
            const float A = I * 2.39f, D = Rad * (0.25f + 0.11f * (I % 3));
            P.Ink->AddCircleFilled(ImVec2(C.x + std::cos(A) * D, C.y + std::sin(A) * D * 0.8f), Rad * (0.10f + 0.03f * (I % 3)), IM_COL32(0xb9, 0xc7, 0xda, 0x50));
        }
        P.TextC(C.x, CY + 424.0f * Scale - 4.0f, 11, IM_COL32(0xac, 0xbd, 0xd3, 255), "Luna");
        CY += DH + 13.0f;
        CY = P.Slider(InnerL, InnerR, CY, "Phase age", "14.76", "days", 14.765f / 29.53f, 13.0f,
                      IM_COL32(0x95, 0xa4, 0xb7, 255), IM_COL32(0x55, 0x56, 0x50, 255), IM_COL32(0x18, 0x1d, 0x23, 255)) + 13.0f;
        // .mc-phase-stops (Full pressed: day == cycle/2)
        const char* Stops[] = {"New", "First", "Full", "Last"};
        const float BW = (InnerR - InnerL - 18.0f) / 4.0f;
        for (int I = 0; I < 4; ++I)
        {
            const float X = InnerL + I * (BW + 6.0f);
            const bool Pressed = I == 2;
            P.Rect(X, CY, X + BW, CY + 30, Pressed ? IM_COL32(0x45, 0x56, 0x6a, 255) : IM_COL32(0x20, 0x25, 0x2b, 255), 16);
            P.RectLine(X, CY, X + BW, CY + 30, IM_COL32(0xb5, 0xc9, 0xe3, 0x1a), 16);
            P.TextC(X + BW * 0.5f, CY + 10, 10, Pressed ? IM_COL32(0xe2, 0xeb, 0xf7, 255) : IM_COL32(0x9d, 0xaf, 0xc7, 255), Stops[I]);
        }
        CY += 30.0f + 13.0f;
        P.Text(InnerL, CY, 10, Muted, "Original textured sphere \xC2\xB7 isolated phase preview. Editing");
        P.Text(InnerL, CY + 16, 10, Muted, "phase switches off Follow sky.");
    }
}

// ---------------------------------------------------------------------------
// The Lens Flare inspector as the HTML builds it: BuildNativeFlare in the
// lens-optics CSS family (LensOptics.css). Header + actions, Optical
// composite instrument (procedural preview plate), InspectorCategories
// (Compose pane active) with the Light stack. The composite plate here is
// shaded from the same source position/ghost layout law; the HTML renders
// it in a worker (see PARITY.md). Defaults: EnvironmentSettings.
// ---------------------------------------------------------------------------
void DrawFlareDashboard()
{
    PageInk P{ImGui::GetWindowDrawList(), ImGui::GetFont()};
    P.Rect(0, 0, 380, 5000, IM_COL32(0x16, 0x16, 0x16, 255), 0);
    P.TopBar("Environment");
    const ImU32 InkMain = IM_COL32(0xd6, 0xd3, 0xdb, 255), Muted = IM_COL32(0xa1, 0x9a, 0xa6, 255);
    const float L = 18.0f, R = 362.0f;
    float Y = 44.0f + 12.0f;
    // .lo-header
    P.Text(L + 2, Y, 9, IM_COL32(0x99, 0x93, 0xa2, 255), "E N V I R O N M E N T  /  C A M E R A  O P T I C S");
    Y += 13.0f + 9.0f;
    P.Text(L + 2, Y, 31, InkMain, "Lens flare");
    Y += 36.0f + 9.0f;
    P.Text(L + 2, Y, 12, Muted, "Reflections, diffraction, and light across the sensor.");
    Y += 20.0f + 18.0f;
    // .lo-actions: Enabled toggle, Export HDR (right)
    {
        const float TW = P.Width(11, "Enabled") + 40.0f;
        P.Rect(L, Y, L + TW, Y + 32, IM_COL32(0x28, 0x28, 0x28, 255), 22);
        P.RectLine(L, Y, L + TW, Y + 32, IM_COL32(0x3e, 0x3e, 0x3e, 255), 22);
        P.Ink->AddCircleFilled(ImVec2(L + 16.0f, Y + 16.0f), 3.5f, InkMain);
        P.Text(L + 26.0f, Y + 10, 11, InkMain, "Enabled");
        const std::string Export = "Export HDR \xE2\x86\x97";
        const float EW = P.Width(11, Export) + 28.0f;
        P.Rect(R - EW, Y, R, Y + 32, IM_COL32(0x28, 0x28, 0x28, 255), 22);
        P.RectLine(R - EW, Y, R, Y + 32, IM_COL32(0x3e, 0x3e, 0x3e, 255), 22);
        P.TextC(R - EW * 0.5f, Y + 10, 11, IM_COL32(0xde, 0xce, 0xb0, 255), Export);
        Y += 32.0f + 20.0f;
    }
    // Optical composite (.lo-instrument)
    {
        const float CardY = Y, InnerL = L + 19.0f, InnerR = R - 19.0f;
        float CY = CardY + 19.0f;
        P.Rect(L, CardY, R, CardY + 348, IM_COL32(0x20, 0x1f, 0x24, 255), 22);
        P.RectLine(L, CardY, R, CardY + 348, IM_COL32(0xc5, 0xb6, 0xd2, 0x22), 22);
        P.Text(InnerL, CY, 9, IM_COL32(0x99, 0x93, 0xa2, 255), "0 1  /  L I V E  P R O C E D U R A L  R E F E R E N C E");
        CY += 9.0f + 9.0f;
        P.Text(InnerL, CY, 18, InkMain, "Optical composite");
        CY += 22.0f + 19.0f;
        // Preview plate 640x280 at source (0.28, 0.43)
        const float DW = InnerR - InnerL, Scale = DW / 640.0f, DH = 280.0f * Scale;
        P.Rect(InnerL, CY, InnerR, CY + DH, IM_COL32(0x08, 0x08, 0x08, 255), 14);
        const ImVec2 Src(InnerL + 0.28f * DW, CY + 0.43f * DH);
        const ImVec2 Centre(InnerL + DW * 0.5f, CY + DH * 0.5f);
        P.Ink->AddCircleFilled(Src, 42.0f * Scale + 16.0f, IM_COL32(0xf3, 0xde, 0xb2, 0x18));
        P.Ink->AddCircleFilled(Src, 20.0f * Scale + 7.0f, IM_COL32(0xf3, 0xde, 0xb2, 0x30));
        P.Ink->AddCircleFilled(Src, 4.0f, IM_COL32(0xf3, 0xde, 0xb2, 255));
        P.Ink->AddCircle(Centre, 34.0f, IM_COL32(0xe5, 0xd5, 0xab, 0x40), 40, 1.2f);   // halo ring guide
        for (int I = 0; I < 6; ++I)   // ghost layout: K = 1-(.6+(i+1)/N*(1.7+spacing))
        {
            const float K = 1.0f - (0.6f + (I + 1) / 6.0f * (1.7f + 1.0f));
            const ImVec2 G(Centre.x + (Src.x - Centre.x) * K, Centre.y + (Src.y - Centre.y) * K);
            if (G.x > InnerL + 4 && G.x < InnerR - 4 && G.y > CY + 4 && G.y < CY + DH - 4)
            {
                const float GR = 9.0f + (I % 4) * 4.0f;
                P.Ink->AddCircleFilled(G, GR, IM_COL32(0xc3, 0xaf, 0xd2, 0x10));
                P.Ink->AddCircle(G, GR, IM_COL32(0xc3, 0xaf, 0xd2, 0x55), 24, 1.0f);
            }
        }
        CY += DH + 10.0f;
        // .lo-preview-footer
        P.Text(InnerL, CY + 5, 9, IM_COL32(0xa6, 0x9d, 0xaf, 255), "S O U R C E  0 . 2 8  /  0 . 4 3");
        const std::string Reset = "Reset source";
        const float RW = P.Width(10, Reset) + 20.0f;
        P.Rect(InnerR - RW, CY, InnerR, CY + 26, IM_COL32(0, 0, 0, 0), 20);
        P.RectLine(InnerR - RW, CY, InnerR, CY + 26, IM_COL32(0x3e, 0x3e, 0x3e, 255), 20);
        P.TextC(InnerR - RW * 0.5f, CY + 8, 10, InkMain, Reset);
        CY += 26.0f + 15.0f;
        P.Text(InnerL, CY, 11, IM_COL32(0xa3, 0x9b, 0xa9, 255), "Drag the source. Authoring preview stays visible when scene");
        P.Text(InnerL, CY + 19, 11, IM_COL32(0xa3, 0x9b, 0xa9, 255), "flare is disabled; scene flare follows the Sun, camera");
        P.Text(InnerL, CY + 38, 11, IM_COL32(0xa3, 0x9b, 0xa9, 255), "occlusion and cabin transmission. Display-mapped");
        P.Text(InnerL, CY + 57, 11, IM_COL32(0xa3, 0x9b, 0xa9, 255), "thumbnail \xC2\xB7 export retains linear HDR.");
        Y = CardY + 348 + 18.0f;
    }
    // InspectorCategories: Compose / Ghosts / Halo / Rays & streaks
    {
        const char* Tabs[] = {"Compose", "Ghosts", "Halo", "Rays & streaks"};
        Y = P.Tabs(L, R, Y, Tabs, 4, IM_COL32(0x40, 0x38, 0x46, 255), IM_COL32(0xed, 0xdf, 0xeb, 255)) + 18.0f;
    }
    // Light stack (.lo-instrument)
    {
        const float CardY = Y, InnerL = L + 19.0f, InnerR = R - 19.0f;
        float CY = CardY + 19.0f;
        P.Rect(L, CardY, R, CardY + 440, IM_COL32(0x20, 0x1f, 0x24, 255), 22);
        P.RectLine(L, CardY, R, CardY + 440, IM_COL32(0xc5, 0xb6, 0xd2, 0x22), 22);
        P.Text(InnerL, CY, 9, IM_COL32(0x99, 0x93, 0xa2, 255), "0 2  /  S O U R C E  &  P R E S E T");
        CY += 9.0f + 9.0f;
        P.Text(InnerL, CY, 18, InkMain, "Light stack");
        CY += 22.0f + 19.0f;
        // .ne-select Optical preset
        P.Text(InnerL, CY, 11, IM_COL32(0x91, 0x91, 0x91, 255), "Optical preset");
        CY += 16.0f + 9.0f;
        P.Rect(InnerL, CY, InnerR, CY + 32, IM_COL32(0x16, 0x16, 0x1b, 255), 18);
        P.RectLine(InnerL, CY, InnerR, CY + 32, IM_COL32(0x3e, 0x3e, 0x3e, 255), 18);
        P.Text(InnerL + 12, CY + 10, 11, IM_COL32(0xd8, 0xd0, 0xdf, 255), "Cinematic");
        P.TextR(InnerR - 12, CY + 10, 11, Muted, "v");
        CY += 32.0f + 18.0f;
        CY = P.Slider(InnerL, InnerR, CY, "Intensity", "0.30", "\xC3\x97", 0.3f / 5.0f) + 18.0f;
        CY = P.Slider(InnerL, InnerR, CY, "Spread", "1.00", "\xC3\x97", (1.0f - 0.25f) / 1.75f) + 18.0f;
        CY = P.Slider(InnerL, InnerR, CY, "Chromatic separation", "0.20", "", 0.2f) + 15.0f;
        P.Text(InnerL, CY, 11, IM_COL32(0xa3, 0x9b, 0xa9, 255), "Presets retain their original renderer behaviour. Editing");
        P.Text(InnerL, CY + 19, 11, IM_COL32(0xa3, 0x9b, 0xa9, 255), "layer geometry or a layer switch selects Custom layers;");
        P.Text(InnerL, CY + 38, 11, IM_COL32(0xa3, 0x9b, 0xa9, 255), "global intensity, spread and chromatic separation do not.");
    }
}

// ---------------------------------------------------------------------------
// The Wind inspector as the HTML builds it: BuildWindField in the
// wind-instruments CSS family (WindInstruments.css). Header + power toggle,
// InspectorCategories (Flow pane active) with the Direction-of-travel card:
// summary readout, 640x480 compass Plot (replicated from the HTML draw
// code), ObservatorySliders and note, plus the live-vector line. Defaults:
// CloudSettings (speed 7, bearing 90 -> FROM 270, gust .25, turbulence .15).
// ---------------------------------------------------------------------------
void DrawWindDashboard()
{
    PageInk P{ImGui::GetWindowDrawList(), ImGui::GetFont()};
    P.Rect(0, 0, 380, 5000, IM_COL32(0x19, 0x1c, 0x19, 255), 0);
    P.TopBar("Environment");
    const ImU32 InkMain = IM_COL32(0xd2, 0xdd, 0xc7, 255), Muted = IM_COL32(0x9a, 0xab, 0x90, 255);
    const float L = 18.0f, R = 362.0f;
    float Y = 44.0f + 27.0f;
    // .wi-header
    P.Text(L + 2, Y, 9, IM_COL32(0x8e, 0x9f, 0x82, 255), "F O R C E F I E L D  /  S H A R E D  W E A T H E R");
    Y += 12.0f + 9.0f;
    P.Text(L + 2, Y, 32, InkMain, "Wind");
    // .wi-power (absolute right, top 40 of header)
    {
        const std::string Label = "On";
        const float PW = P.Width(11, Label) + 42.0f;
        P.Rect(R - PW, 44.0f + 45.0f, R, 44.0f + 45.0f + 30, IM_COL32(0x28, 0x28, 0x28, 255), 24);
        P.RectLine(R - PW, 44.0f + 45.0f, R, 44.0f + 45.0f + 30, IM_COL32(0x3e, 0x3e, 0x3e, 255), 24);
        P.Ink->AddCircleFilled(ImVec2(R - PW + 15.0f, 44.0f + 45.0f + 15.0f), 3.5f, InkMain);
        P.Text(R - PW + 25.0f, 44.0f + 45.0f + 9, 11, InkMain, Label);
    }
    Y += 40.0f + 8.0f;
    P.Text(L + 2, Y, 12, Muted, "A direction, a pulse, a changing column of air.");
    Y += 20.0f + 22.0f;
    // InspectorCategories: Flow / Variability / Altitude / Receivers
    {
        const char* Tabs[] = {"Flow", "Variability", "Altitude", "Receivers"};
        Y = P.Tabs(L, R, Y, Tabs, 4, IM_COL32(0x3d, 0x4b, 0x34, 255), IM_COL32(0xe1, 0xea, 0xcd, 255)) + 15.0f;
    }
    // Direction of travel (.wi-card)
    {
        const float CardY = Y, InnerL = L + 20.0f, InnerR = R - 20.0f;
        float CY = CardY + 20.0f;
        P.Rect(L, CardY, R, CardY + 700, IM_COL32(0x21, 0x25, 0x20, 255), 24);
        P.RectLine(L, CardY, R, CardY + 700, IM_COL32(0xb6, 0xce, 0x9b, 0x21), 24);
        P.Text(InnerL, CY, 9, IM_COL32(0x8e, 0x9f, 0x82, 255), "0 1  /  B A S E  F L O W");
        CY += 10.0f + 10.0f;
        P.Text(InnerL, CY, 18, InkMain, "Direction of travel");
        CY += 22.0f + 19.0f;
        // .wi-summary
        P.Text(InnerL, CY, 53, InkMain, "7.0");
        const float UnitX = InnerL + P.Width(53, "7.0") + 10.0f;
        P.Text(UnitX, CY + 36, 12, IM_COL32(0x9d, 0xb0, 0x8e, 255), "m/s");
        P.TextR(InnerR, CY + 38, 10, IM_COL32(0x9d, 0xb0, 0x8e, 255), "FROM 270\xC2\xB0");
        CY += 53.0f + 15.0f;
        // Compass Plot 640x480 (HTML draw code, bearing 90 -> U = [1, 0])
        const float DW = InnerR - InnerL, Scale = DW / 640.0f, DH = 480.0f * Scale;
        const float OX = InnerL + 320.0f * Scale, OY = CY + 235.0f * Scale;
        P.Ink->AddCircle(ImVec2(OX, OY), 155.0f * Scale, IM_COL32(0xc1, 0xd6, 0xb8, 0x2d), 64, 1.0f);
        P.Ink->AddCircle(ImVec2(OX, OY), 98.0f * Scale, IM_COL32(0xc1, 0xd6, 0xb8, 0x14), 48, 1.0f);
        for (int I = 0; I < 24; ++I)
        {
            const float A = I / 24.0f * 6.2831853f;
            P.Ink->AddLine(ImVec2(OX + std::sin(A) * 150.0f * Scale, OY - std::cos(A) * 150.0f * Scale),
                           ImVec2(OX + std::sin(A) * 155.0f * Scale, OY - std::cos(A) * 155.0f * Scale), IM_COL32(0xbf, 0xd1, 0xb2, 0x58), 1.0f);
        }
        for (int J = -2; J <= 2; ++J)
        {
            const float Across = J * 39.0f, Len = std::sqrt(std::max(0.0f, 135.0f * 135.0f - Across * Across));
            ImVec2 Prev; bool Have = false;
            for (int I = 0; I <= 36; ++I)
            {
                const float Along = -Len + 2.0f * Len * I / 36.0f;
                const ImVec2 Pt(OX + Along * Scale, OY + (Across + std::sin(I / 36.0f * 3.14159265f) * J * 3.0f) * Scale);
                if (Have) P.Ink->AddLine(Prev, Pt, IM_COL32(0xb9, 0xc9, 0xa1, 0x2a), 1.2f);
                Prev = Pt; Have = true;
            }
            const ImVec2 End(OX + Len * 0.48f * Scale, OY + Across * Scale);
            const ImVec2 Back(OX + (Len * 0.48f - 12.0f) * Scale, OY + Across * Scale);
            P.Ink->AddLine(Back, End, IM_COL32(0xc7, 0xd8, 0xac, 255), 1.6f);
            P.Ink->AddLine(ImVec2(OX + (Len * 0.48f - 9.0f) * Scale, OY + (Across - 5.0f) * Scale), End, IM_COL32(0xc7, 0xd8, 0xac, 255), 1.6f);
            P.Ink->AddLine(ImVec2(OX + (Len * 0.48f - 9.0f) * Scale, OY + (Across + 5.0f) * Scale), End, IM_COL32(0xc7, 0xd8, 0xac, 255), 1.6f);
        }
        const ImVec2 From(OX - 155.0f * 7.0f / 40.0f * Scale, OY);
        for (float T = 0.0f; T < 1.0f; T += 0.12f)
            P.Ink->AddLine(ImVec2(OX + (From.x - OX) * T, OY), ImVec2(OX + (From.x - OX) * (T + 0.06f), OY), IM_COL32(0xdf, 0xe4, 0xba, 0x66), 1.0f);
        P.Ink->AddCircleFilled(From, 9.0f * Scale + 2.0f, IM_COL32(0xe1, 0xe5, 0xbc, 255));
        P.Ink->AddCircleFilled(ImVec2(OX, OY), 4.0f * Scale + 1.0f, IM_COL32(0x94, 0xab, 0x8a, 255));
        P.TextC(OX, CY + 40.0f * Scale, 9, IM_COL32(0xa7, 0xb6, 0x9b, 255), "N / +Y");
        P.Text(InnerL + 516.0f * Scale, OY, 9, IM_COL32(0xa7, 0xb6, 0x9b, 255), "E");
        P.TextC(OX, CY + 426.0f * Scale, 9, IM_COL32(0xa7, 0xb6, 0x9b, 255), "S");
        P.TextR(InnerL + 126.0f * Scale, OY, 9, IM_COL32(0xa7, 0xb6, 0x9b, 255), "W");
        P.TextC(OX, CY + 468.0f * Scale, 8, IM_COL32(0x91, 0xa3, 0x8c, 255), "DOT = WIND FROM / ARROWS = TRAVEL TO");
        CY += DH + 20.0f;
        const ImU32 WindFill = IM_COL32(0xc8, 0xd8, 0xae, 255);
        CY = P.Slider(InnerL, InnerR, CY, "Base speed", "7.0", "m/s", 7.0f / 40.0f, 11.0f, Muted, WindFill, IM_COL32(0x14, 0x1b, 0x15, 255)) + 19.0f;
        CY = P.Slider(InnerL, InnerR, CY, "Bearing from", "270", "\xC2\xB0", 270.0f / 360.0f, 11.0f, Muted, WindFill, IM_COL32(0x14, 0x1b, 0x15, 255)) + 18.0f;
        P.Text(InnerL, CY, 11, IM_COL32(0x95, 0xa6, 0x8b, 255), "Meteorological bearing is where wind comes FROM; the");
        P.Text(InnerL, CY + 19, 11, IM_COL32(0x95, 0xa6, 0x8b, 255), "arrows show where it travels TO. Drag the source dot");
        P.Text(InnerL, CY + 38, 11, IM_COL32(0x95, 0xa6, 0x8b, 255), "radially for base speed. Base-only reference: gusts,");
        P.Text(InnerL, CY + 57, 11, IM_COL32(0x95, 0xa6, 0x8b, 255), "altitude and turbulence are excluded.");
        Y = CardY + 700 + 15.0f;
    }
    P.Text(L + 8, Y, 9, IM_COL32(0xa4, 0xb5, 0x94, 255), "LIVE BASE VECTOR \xC2\xB7 7.00 m/s \xC2\xB7 current shared gust clock");
}


// ---------------------------------------------------------------------------
// The Height fog inspector as the HTML builds it: BuildFogInspectorDashboard
// in the fog-dashboard CSS family (FogDashboard.css), Visibility pane active.
// Probe seeded as the HTML (distance 200 m, height 2 m); the retained-contrast
// readout and target fading use the HTML's own FogPathTransmission law on the
// seeded settings (density 0.002, falloff 30 m, FogEnabled false).
// ---------------------------------------------------------------------------
void DrawFogDashboard()
{
    PageInk P{ImGui::GetWindowDrawList(), ImGui::GetFont()};
    P.Rect(0, 0, 380, 5000, IM_COL32(0x15, 0x17, 0x15, 255), 0);
    P.TopBar("Environment");
    const ImU32 InkMain = IM_COL32(0xc9, 0xd8, 0xcf, 255), Muted = IM_COL32(0x8c, 0x9f, 0x95, 255);
    const float L = 18.0f, R = 362.0f;
    float Y = 44.0f + 10.0f;
    // .fg-header
    P.Text(L + 4, Y, 9, Muted, "E N V I R O N M E N T  /  G E O M E T R Y  M E D I A");
    Y += 12.0f + 8.0f;
    P.Text(L + 4, Y, 30, InkMain, "Height fog");
    {
        const std::string Label = "Off";
        const float PW = P.Width(11, Label) + 42.0f;
        const float PY = 44.0f + 45.0f;
        P.Rect(R - PW, PY, R, PY + 30, IM_COL32(0x28, 0x28, 0x28, 255), 24);
        P.RectLine(R - PW, PY, R, PY + 30, IM_COL32(0x3e, 0x3e, 0x3e, 255), 24);
        P.Ink->AddCircle(ImVec2(R - PW + 15.0f, PY + 15.0f), 3.5f, IM_COL32(0xd4, 0xe0, 0xd4, 255), 12, 1.2f);
        P.Text(R - PW + 25.0f, PY + 9, 11, IM_COL32(0xd4, 0xe0, 0xd4, 255), Label);
    }
    Y += 38.0f + 8.0f;
    P.Text(L + 4, Y, 12, IM_COL32(0x91, 0xa3, 0x98, 255), "A low-lying medium. A measurable loss of contrast.");
    Y += 19.0f + 22.0f;
    // InspectorCategories: Visibility / Medium / Colour & sun
    {
        const char* Tabs[] = {"Visibility", "Medium", "Colour & sun"};
        Y = P.Tabs(L, R, Y, Tabs, 3, IM_COL32(0x34, 0x3d, 0x46, 255), IM_COL32(0xe1, 0xea, 0xf4, 255)) + 14.0f;
    }
    // A walk into the fog (.fg-transect section)
    {
        const float CardY = Y, InnerL = L + 20.0f, InnerR = R - 20.0f;
        float CY = CardY + 20.0f;
        P.Rect(L, CardY, R, CardY + 650, IM_COL32(0x1c, 0x20, 0x1e, 255), 22);
        P.RectLine(L, CardY, R, CardY + 650, IM_COL32(0xaa, 0xc4, 0xb6, 0x1a), 22);
        P.Text(InnerL, CY, 17, InkMain, "A walk into the fog");
        CY += 20.0f + 7.0f;
        P.Text(InnerL, CY, 9, Muted, "N E U T R A L  C O N T R A S T  T A R G E T S");
        CY += 9.0f + 12.0f + 20.0f;
        // .fg-readout (HTML law: OD = 0.002*200*exp(-2/30) = 0.374; retained = exp(-OD^2) = 86.9%)
        P.Text(InnerL, CY, 52, InkMain, "86.9");
        P.Text(InnerL + P.Width(52, "86.9") + 12.0f, CY + 8, 11, Muted, "% contrast");
        P.Text(InnerL + P.Width(52, "86.9") + 12.0f, CY + 24, 11, Muted, "retained");
        CY += 52.0f + 12.0f;
        // Transect diagram 640x425: six neutral targets fading with distance
        {
            const float DW = InnerR - InnerL, Scale = DW / 640.0f, DH = 425.0f * Scale;
            auto Transmit = [](double Distance) {
                const double OD = 0.002 * Distance * std::exp(-2.0 / 30.0);
                return std::exp(-OD * OD);
            };
            const ImVec2 A(InnerL + 45.0f * Scale, CY + 350.0f * Scale), B(InnerL + 305.0f * Scale, CY + 110.0f * Scale), Cc(InnerL + 590.0f * Scale, CY + 350.0f * Scale);
            P.Ink->AddLine(A, B, IM_COL32(0xb1, 0xc7, 0xc3, 0x28), 1.0f);
            P.Ink->AddLine(B, Cc, IM_COL32(0xb1, 0xc7, 0xc3, 0x28), 1.0f);
            P.Ink->AddLine(ImVec2(InnerL + 40.0f * Scale, CY + 350.0f * Scale), ImVec2(InnerL + 600.0f * Scale, CY + 350.0f * Scale), IM_COL32(0xb1, 0xc7, 0xc3, 0x36), 1.0f);
            for (int I = 5; I >= 0; --I)
            {
                const double Distance = 500.0 * I / 5.0, T = Transmit(Distance);
                const float Sc = 1.0f / (1.0f + I * 0.38f), Wd = 90.0f * Sc * Scale, Ht = 145.0f * Sc * Scale;
                const float CX = InnerL + (100.0f + I * 90.0f) * Scale, CYB = CY + (315.0f - I * 28.0f) * Scale;
                auto Chan = [&](double Linear) {
                    const int V = (int)std::lround(std::pow(Linear * T + 0.16 * (1.0 - T), 1.0 / 2.2) * 255.0);
                    return IM_COL32(V, V, V, 255);
                };
                P.Rect(CX - Wd / 2 - 4, CYB - Ht - 4, CX + Wd / 2 + 4, CYB + 4, IM_COL32(0x13, 0x1b, 0x1c, 255), 7);
                P.Rect(CX - Wd / 2, CYB - Ht, CX + Wd / 2, CYB, Chan(0.08), 3);
                P.Rect(CX - Wd / 2, CYB - Ht, CX, CYB, Chan(0.8), 3);
                char Buf[16];
                if (Distance >= 1000) std::snprintf(Buf, sizeof(Buf), "%.1f km", Distance / 1000);
                else std::snprintf(Buf, sizeof(Buf), "%.0f m", Distance);
                P.TextC(CX, CYB + 23.0f * Scale, 8, IM_COL32(0xa8, 0xbb, 0xb7, 255), Buf);
            }
            const float PX = InnerL + (45.0f + 200.0f / 500.0f * 550.0f) * Scale;
            P.Ink->AddLine(ImVec2(InnerL + 45.0f * Scale, CY + 390.0f * Scale), ImVec2(InnerL + 595.0f * Scale, CY + 390.0f * Scale), IM_COL32(0xb4, 0xcf, 0xca, 0x44), 2.0f);
            P.Ink->AddCircleFilled(ImVec2(PX, CY + 390.0f * Scale), 5.0f, IM_COL32(0xd5, 0xe4, 0xdc, 255));
            P.Text(InnerL + 45.0f * Scale, CY + 36.0f * Scale - 8.0f, 9, IM_COL32(0x94, 0xaa, 0xa4, 255), "NEAR / CLEAR");
            P.TextR(InnerL + 595.0f * Scale, CY + 36.0f * Scale - 8.0f, 9, IM_COL32(0x94, 0xaa, 0xa4, 255), "FAR / ATTENUATED");
            CY += DH + 20.0f;
        }
        CY = P.Slider(InnerL, InnerR, CY, "Probe distance", "200", "m", 200.0f / 500.0f, 18.0f, IM_COL32(0xb4, 0xc5, 0xb9, 255)) + 17.0f;
        CY = P.Slider(InnerL, InnerR, CY, "Probe height", "2", "m", 2.0f / 1000.0f, 18.0f, IM_COL32(0xb4, 0xc5, 0xb9, 255)) + 16.0f;
        P.Text(InnerL, CY, 11, IM_COL32(0x8c, 0x9f, 0x95, 255), "Fixed neutral targets under fixed reference light, not a");
        P.Text(InnerL, CY + 19, 11, IM_COL32(0x8c, 0x9f, 0x95, 255), "scene render. Horizontal paths use the renderer's");
        P.Text(InnerL, CY + 38, 11, IM_COL32(0x8c, 0x9f, 0x95, 255), "extinction law at the chosen world height. Probe edits");
        P.Text(InnerL, CY + 57, 11, IM_COL32(0x8c, 0x9f, 0x95, 255), "do not change the scene.");
        Y = CardY + 650 + 14.0f;
    }
    P.Text(L + 9, Y, 11, IM_COL32(0xb3, 0xc3, 0xb7, 255), "Scene medium is off \xC2\xB7 the diagnostic still shows its configured extinction.");
}


int main(int ArgumentCount, char** Arguments)
{
    using namespace Frontier;
    if (ArgumentCount < 2) { std::fprintf(stderr, "usage: InspectorCapture <output.ppm> [font.ttf] [height] [page]\n"); return 2; }
    if (ArgumentCount >= 4) CaptureHeight = std::max(200, std::atoi(Arguments[3]));   // [font] [height] [page]
    const std::string Page = ArgumentCount >= 5 ? Arguments[4] : "render";
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& Io = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.DisplaySize = ImVec2((float)CaptureWidth, (float)CaptureHeight);
    Io.AddMousePosEvent(-1.0f, -1.0f);
    // DM Sans (the HTML face): when a TTF is supplied it becomes the atlas default, so ResolveKitFaces'
    //    fallback — and therefore every panel size — renders in DM Sans, not ImGui's default face.
    if (ArgumentCount >= 3)
    {
        // ImGui's default glyph ranges omit the symbols the HTML text uses (≤ → – ·); extend them.
        static ImVector<ImWchar> Ranges;
        ImFontGlyphRangesBuilder Builder;
        Builder.AddRanges(Io.Fonts->GetGlyphRangesDefault());
        static const ImWchar Extra[] = {0x2010, 0x2019, 0x201C, 0x201E, 0x2026, 0x2026, 0x2190, 0x21FF, 0x2200, 0x22FF, 0x2500, 0x25FF, 0};
        Builder.AddRanges(Extra);
        Builder.BuildRanges(&Ranges);
        ImFontConfig Config;
        Config.GlyphRanges = Ranges.Data;
        Io.Fonts->AddFontFromFileTTF(Arguments[2], 15.0f, &Config);
    }
    else Io.Fonts->AddFontDefault();
    unsigned char* OwnedAtlas = nullptr;
    Io.Fonts->GetTexDataAsRGBA32(&OwnedAtlas, &AtlasWidth, &AtlasHeight);
    AtlasPixels = OwnedAtlas;
    ImGui::StyleColorsDark();
    // HTML scrollbar (.inspector-content::-webkit-scrollbar): 8 px, thumb #2c2c2c radius 8, transparent track.
    ImGui::GetStyle().ScrollbarSize = 8.0f;
    ImGui::GetStyle().ScrollbarRounding = 8.0f;
    ImGui::GetStyle().Colors[ImGuiCol_ScrollbarBg] = ImVec4(0,0,0,0);
    ImGui::GetStyle().Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0x2c/255.0f,0x2c/255.0f,0x2c/255.0f,1);
    ImGui::GetStyle().Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0x2c/255.0f,0x2c/255.0f,0x2c/255.0f,1);
    ImGui::GetStyle().Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0x2c/255.0f,0x2c/255.0f,0x2c/255.0f,1);
    Framebuffer.resize((size_t)CaptureWidth * CaptureHeight);

    std::unique_ptr<RecordEntry> PageRecord;
    std::string PageName = "Render settings";
    const bool SunDashboard = Page == "sun";
    const bool BespokeDashboard = SunDashboard || Page == "moon" || Page == "flare" || Page == "wind" || Page == "sun-daylight" || Page == "sun-light" || Page == "sun-disc" || Page == "fog";
    if      (BespokeDashboard)   { /* bespoke dashboard draw; no generic record */ }
    else if (Page == "field")     { PageRecord = std::make_unique<FieldRecord>();        PageName = "BVH & distance field"; }
    else if (Page == "cards")     { PageRecord = std::make_unique<SurfaceCardsRecord>(); PageName = "Surface cache"; }
    else if (Page == "derez")     { PageRecord = std::make_unique<DerezRecord>();        PageName = "Derez \xC2\xB7 VFX"; }
    else if (Page == "fracture")  { PageRecord = std::make_unique<FractureRecord>();     PageName = "Fracture"; }
    else if (Page == "training")  { PageRecord = std::make_unique<TrainingRecord>();     PageName = "Training arena"; }
    else if (Page == "cones")     { PageRecord = std::make_unique<ConesRecord>();        PageName = "Cones"; }
    else if (Page == "level")     { PageRecord = std::make_unique<LevelRecord>();        PageName = "Proving grounds"; }
    else if (Page == "stress")    { PageRecord = std::make_unique<StressRecord>();       PageName = "Stress test"; }
    else if (Page == "diagnostics") { PageRecord = std::make_unique<DiagnosticsRecord>(); PageName = "Diagnostics"; }
    else if (Page == "clusters")  { PageRecord = std::make_unique<ClustersRecord>();     PageName = "Geometry clusters"; }
    else if (Page == "particles") { PageRecord = std::make_unique<ParticlesRecord>();    PageName = "Particles"; }
    else                          { PageRecord = std::make_unique<RenderSettingsRecord>(); }
    SceneDirectory& Directory = SceneDirectory::Instance();
    if (PageRecord) (void)Directory.ConfigureName(PageRecord->Token(), PageName);

    SceneDirectoryPanel Outliner;
    SceneInspectorPanel Inspector;
    if (PageRecord)
    {
        Outliner.Synchronize(Directory);
        Outliner.Select(PageRecord->Token());
        Outliner.Synchronize(Directory);
        Inspector.Synchronize(Directory, Outliner);
    }

    for (int Frame = 0; Frame < 4; ++Frame)
    {
        Io.DeltaTime = 1.0f/60.0f;
        if (!Io.Fonts->IsBuilt()) Io.Fonts->Build();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0,0));
        ImGui::SetNextWindowSize(ImVec2((float)CaptureWidth,(float)CaptureHeight));
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse|ImGuiWindowFlags_NoBackground);
        if      (Page == "sun")   DrawSunDashboard(0);
        else if (Page == "sun-daylight") DrawSunDashboard(1);
        else if (Page == "sun-light")    DrawSunDashboard(2);
        else if (Page == "sun-disc")     DrawSunDashboard(3);
        else if (Page == "moon")  DrawMoonDashboard();
        else if (Page == "flare") DrawFlareDashboard();
        else if (Page == "wind")  DrawWindDashboard();
        else if (Page == "fog")   DrawFogDashboard();
        else Inspector.ConstructInterface();
        ImGui::End();
        ImGui::Render();
        if (Frame == 3)
        {
            unsigned char* Pixels=nullptr; Io.Fonts->GetTexDataAsRGBA32(&Pixels,&AtlasWidth,&AtlasHeight); AtlasPixels=Pixels;
            std::fill(Framebuffer.begin(), Framebuffer.end(), Pixel{11,13,12,255});
            RasterizeDrawData(*ImGui::GetDrawData());
        }
    }
    size_t ControlCount = 0;
    for (const SceneInspectorCard& Card : Inspector.QueryCards()) ControlCount += Card.Controls.size();
    std::printf("inspector cards: %zu (incl. tile host card), controls %zu\n", Inspector.QueryCards().size(), ControlCount);
    const bool Wrote = WritePpm(Arguments[1]);
    std::printf("inspector capture: %s (%dx%d)\n", Wrote?"written":"FAILED", CaptureWidth, CaptureHeight);
    return Wrote?0:1;
}
