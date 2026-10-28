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

class SunRecord final : public Frontier::RecordEntry
{
  public:
    SunRecord() : RecordEntry(Frontier::RecordClassification::Environment)
    {
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
        DeclareEditorIdentity("Directional light", "Environment");
        DeclareEditorDescription("Light, direction & atmosphere");
        DeclareEditorToggle("sun.shadows", "Shadows", "", true, [this]() { return DirectShadow; }, [this](bool On) { DirectShadow = On; }, "Sun");
        DeclareEditorToggle("sun.autoExposure", "Auto exposure", "", true, [this]() { return AutoExposure; }, [this](bool On) { AutoExposure = On; }, "Aperture");
        DeclareEditorCard("Lighting preset", "", 1, "Sun");
        DeclareEditorSegmented("sun.preset", "", {"Noon", "Golden", "Dusk", "Night"},
                               [this]() { return PresetIndex; },
                               [this](size_t Index) { PresetIndex = Index; const double E[][2] = {{64,145},{10,160},{1.5,172},{-14,172}}; SunElevation = E[Index][0]; SunAzimuth = E[Index][1]; });
        DeclareEditorMuted("Night drops the sun below the horizon: the lamps, the hangar strips and the tunnel light the course through the cards.");
        DeclareEditorCard("Sun direction", "", 2, "Orbit");
        DeclareEditorMetric("sun.elevation", [this]() { char B[32]; std::snprintf(B, sizeof(B), "%.1f", SunElevation); return std::string(B); }, "\xC2\xB0",
                            [this]() { return std::pair<std::string, std::string>{SunElevation < 0 ? "below horizon" : "above horizon", SunElevation < 0 ? "warm" : "live"}; });
        DeclareEditorSlider("sun.elev", "Elevation", "\xC2\xB0", -90, 90, 0.5, [this]() { return SunElevation; }, [this](double V) { SunElevation = V; }, 1, "Night", "Zenith");
        DeclareEditorSlider("sun.azim", "Azimuth", "\xC2\xB0", -180, 180, 1, [this]() { return SunAzimuth; }, [this](double V) { SunAzimuth = V; }, 0);
        DeclareEditorSlider("sun.radius", "Disc angular radius", "\xC2\xB0", 0.05, 2, 0.01, [this]() { return SunRadius; }, [this](double V) { SunRadius = V; }, 2);
        DeclareEditorCard("Light intensity", "", 3, "Lightbulb");
        DeclareEditorSlider("sun.temp", "Temperature", "K", 2000, 10000, 50, [this]() { return SunTemperature; }, [this](double V) { SunTemperature = V; }, 0);
        DeclareEditorSlider("sun.solar", "Solar intensity", "", 1, 30, 0.5, [this]() { return SolarIlluminance; }, [this](double V) { SolarIlluminance = V; }, 1);
        DeclareEditorSlider("sun.sky", "Sky intensity", "\xC3\x97", 0, 3, 0.05, [this]() { return SkyScale; }, [this](double V) { SkyScale = V; }, 2);
        DeclareEditorSlider("sun.emission", "Emitters (lamps, flames)", "\xC3\x97", 0, 4, 0.05, [this]() { return EmissionScale; }, [this](double V) { EmissionScale = V; }, 2);
        DeclareEditorCard("3D Clustered Lighting", "", 4, "Sparkles");
        DeclareEditorMetric("sun.lights", []() { return std::string("0"); }, "active lights",
                            []() { return std::pair<std::string, std::string>{"0/1024 cells", "live"}; });
        DeclareEditorStats("sun.lightStats", {{"Point lights", "", []() { return std::string("0"); }},
                                              {"Street lamps", "", []() { return std::string("0"); }},
                                              {"Vehicle beams", "", []() { return std::string("0"); }}});
        DeclareEditorMuted("16\xC3\x97" "16\xC3\x97" "4 3D spatial grid with 64-bit per-cell light bitmasks and hardware firstTrailingBit traversal alongside emissive surface cards.");
        DeclareEditorToggle("sun.cull", "3D cluster bitmask culling", "Skip out-of-range lights per 3D cell across Direct, Card & Path passes", false,
                            [this]() { return CullEnabled; }, [this](bool On) { CullEnabled = On; });
#endif
    }
    bool DirectShadow = true, AutoExposure = true, CullEnabled = true;
    double SunElevation = 38.0, SunAzimuth = 145.0, SunRadius = 0.27, SunTemperature = 5778.0;
    double SolarIlluminance = 12.0, SkyScale = 1.0, EmissionScale = 1.0;
    size_t PresetIndex = 99;   // HTML seeds LightingPreset "Custom": no preset button active
};

class MoonRecord final : public Frontier::RecordEntry
{
  public:
    MoonRecord() : RecordEntry(Frontier::RecordClassification::Environment)
    {
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
        DeclareEditorIdentity("Environment", "Environment");
        DeclareEditorDescription("Scene moons \xC2\xB7 body catalogue");
        DeclareEditorCard("Moon settings", "", -1, "Moon");
        DeclareEditorToggle("moon.visible", "Visible", "", false, [this]() { return MoonEnabled; }, [this](bool On) { MoonEnabled = On; });
        DeclareEditorToggle("moon.follow", "Follow sky", "", false, [this]() { return MoonFollowSky; }, [this](bool On) { MoonFollowSky = On; });
        DeclareEditorSelect("moon.instance", "Active moon instance", {"1 \xC2\xB7 Luna", "2 \xC2\xB7 Luna", "3 \xC2\xB7 Luna", "4 \xC2\xB7 Luna"},
                            [this]() { return Instance; }, [this](size_t Index) { Instance = Index; });
        DeclareEditorCard("Lunar phase", "", 1, "Moon");
        DeclareEditorReadout("moon.age", "Phase age", [this]() { char B[32]; std::snprintf(B, sizeof(B), "%.1f d", MoonPhase); return std::string(B); });
        DeclareEditorReadout("moon.phaseName", "Phase", [this]() { return std::string(MoonPhase < 1.8 ? "New moon" : MoonPhase < 5.5 ? "Waxing crescent" : MoonPhase < 9.2 ? "First quarter" : MoonPhase < 12.9 ? "Waxing gibbous" : MoonPhase < 16.6 ? "Full moon" : MoonPhase < 20.3 ? "Waning gibbous" : MoonPhase < 24.0 ? "Last quarter" : "Waning crescent"); });
        DeclareEditorSlider("moon.phase", "Phase age", "days", 0, 29.53, 0.01, [this]() { return MoonPhase; }, [this](double V) { MoonPhase = V; }, 2);
        DeclareEditorAction("moon.stopNew", "New", [this]() { MoonPhase = 0.0; });
        DeclareEditorAction("moon.stopFirst", "First", [this]() { MoonPhase = 29.53 / 4; });
        DeclareEditorAction("moon.stopFull", "Full", [this]() { MoonPhase = 29.53 / 2; });
        DeclareEditorAction("moon.stopLast", "Last", [this]() { MoonPhase = 29.53 * 3 / 4; });
        DeclareEditorMuted("Original textured sphere \xC2\xB7 isolated phase preview. Editing phase switches off Follow sky.");
        DeclareEditorCard("Moonlight", "", 2, "Moon");
        DeclareEditorMetric("moon.brightMetric", [this]() { char B[32]; std::snprintf(B, sizeof(B), "%.2f", MoonBrightness); return std::string(B); }, "\xC3\x97",
                            []() { return std::pair<std::string, std::string>{"", ""}; });
        DeclareEditorSlider("moon.bright", "Disc brightness", "\xC3\x97", 0, 5, 0.01, [this]() { return MoonBrightness; }, [this](double V) { MoonBrightness = V; }, 2);
        DeclareEditorSlider("moon.glow", "Lunar glow", "\xC3\x97", 0, 3, 0.01, [this]() { return MoonGlow; }, [this](double V) { MoonGlow = V; }, 2);
        DeclareEditorToggle("moon.light", "Illuminate scene", "", false, [this]() { return MoonLightEnabled; }, [this](bool On) { MoonLightEnabled = On; });
        DeclareEditorSlider("moon.lightPower", "Moonlight power", "\xC3\x97", 0, 5, 0.01, [this]() { return MoonLightIntensity; }, [this](double V) { MoonLightIntensity = V; }, 2);
        DeclareEditorMuted("Primary Moon supplies the existing directional scene light. Phase, horizon and atmosphere attenuate it; artistic size does not increase its power.");
        DeclareEditorCard("Moon size", "", 3, "Moon");
        DeclareEditorMetric("moon.sizeMetric", [this]() { char B[32]; std::snprintf(B, sizeof(B), "%.2f", MoonSize); return std::string(B); }, "\xC2\xB0",
                            []() { return std::pair<std::string, std::string>{"", ""}; });
        DeclareEditorSlider("moon.size", "Angular diameter", "\xC2\xB0", 0.1, 180, 0.01, [this]() { return MoonSize; }, [this](double V) { MoonSize = V; }, 2);
        DeclareEditorAction("moon.earthRef", "Earth Moon reference / 0.52\xC2\xB0", [this]() { MoonSize = 0.52; });
        DeclareEditorCard("Lunar position", "", 4, "Orbit");
        DeclareEditorMuted("Drag the celestial globe to place this body.");
        DeclareEditorSlider("moon.azim", "Moon azimuth", "\xC2\xB0", -180, 180, 0.1, [this]() { return MoonAzimuth; }, [this](double V) { MoonAzimuth = V; }, 1);
        DeclareEditorSlider("moon.elev", "Moon elevation", "\xC2\xB0", -90, 90, 0.1, [this]() { return MoonElevation; }, [this](double V) { MoonElevation = V; }, 1);
        DeclareEditorCard("Moon rotation", "", 5, "Moon");
        DeclareEditorMuted("Surface orientation is independent of sky position.");
        DeclareEditorSlider("moon.roll", "Surface roll", "\xC2\xB0", -180, 180, 0.1, [this]() { return MoonRoll; }, [this](double V) { MoonRoll = V; }, 1);
        DeclareEditorSlider("moon.pitch", "Surface pitch", "\xC2\xB0", -180, 180, 0.1, [this]() { return MoonPitch; }, [this](double V) { MoonPitch = V; }, 1);
        DeclareEditorSlider("moon.spin", "Surface spin", "\xC2\xB0", -180, 180, 0.1, [this]() { return MoonSpin; }, [this](double V) { MoonSpin = V; }, 1);
        DeclareEditorAction("moon.resetFrame", "Reset surface frame", [this]() { MoonRoll = MoonPitch = MoonSpin = 0.0; });
        DeclareEditorCard("Bright planets \xC2\xB7 orbital placement", "", 6, "Orbit");
        DeclareEditorToggle("moon.planets", "Planets", "", false, [this]() { return PlanetsEnabled; }, [this](bool On) { PlanetsEnabled = On; });
        DeclareEditorSlider("moon.planetBright", "Brightness", "\xC3\x97", 0, 5, 0.01, [this]() { return PlanetBrightness; }, [this](double V) { PlanetBrightness = V; }, 2);
        DeclareEditorMuted("Low-precision orbital positions, not decorative Moon presets. Date and observer location are shared with the Sun. Suitable for visual placement, not navigation.");
#endif
    }
    bool MoonEnabled = true, MoonFollowSky = false, MoonLightEnabled = true, PlanetsEnabled = true;
    double MoonPhase = 14.765, MoonAzimuth = -8.0, MoonElevation = 38.0, MoonRoll = 0.0, MoonPitch = 0.0, MoonSpin = 0.0;
    double MoonSize = 25.0, MoonBrightness = 1.0, MoonGlow = 0.2, MoonLightIntensity = 1.0, PlanetBrightness = 1.0;
    size_t Instance = 0;
};

class FlareRecord final : public Frontier::RecordEntry
{
  public:
    FlareRecord() : RecordEntry(Frontier::RecordClassification::Environment)
    {
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
        DeclareEditorIdentity("Environment", "Environment");
        DeclareEditorDescription("Optical effect subcomponent");
        DeclareEditorToggle("flare.enabled", "Flare", "", true, [this]() { return FlareEnabled; }, [this](bool On) { FlareEnabled = On; }, "Sparkles");
        DeclareEditorCard("Light stack", "", 1, "Aperture");
        DeclareEditorSelect("flare.preset", "Optical preset", {"Cinematic", "Anamorphic", "Starburst", "Halo", "Custom layers"},
                            [this]() { return Preset; }, [this](size_t Index) { Preset = Index; });
        DeclareEditorSlider("flare.intensity", "Intensity", "\xC3\x97", 0, 5, 0.01, [this]() { return Intensity; }, [this](double V) { Intensity = V; }, 2);
        DeclareEditorSlider("flare.spread", "Spread", "\xC3\x97", 0.25, 2, 0.01, [this]() { return Spread; }, [this](double V) { Spread = V; }, 2);
        DeclareEditorSlider("flare.chromatic", "Chromatic separation", "", 0, 1, 0.01, [this]() { return Chromatic; }, [this](double V) { Chromatic = V; }, 2);
        DeclareEditorMuted("Presets retain their original renderer behaviour. Editing layer geometry or a layer switch selects Custom layers; global intensity, spread and chromatic separation do not.");
        DeclareEditorCard("Internal reflections", "", 2, "Aperture");
        DeclareEditorSelect("flare.shape", "Ghost shape", {"Round", "Hexagon", "Octagon"},
                            [this]() { return GhostShape; }, [this](size_t Index) { GhostShape = Index; Preset = 4; });
        DeclareEditorSlider("flare.ghosts", "Ghost count", "", 0, 24, 1, [this]() { return Ghosts; }, [this](double V) { Ghosts = V; }, 0);
        DeclareEditorSlider("flare.ghostBright", "Ghost brightness", "\xC3\x97", 0, 2, 0.01, [this]() { return GhostBrightness; }, [this](double V) { GhostBrightness = V; }, 2);
        DeclareEditorSlider("flare.ghostSpacing", "Ghost spacing", "\xC3\x97", 0, 3, 0.01, [this]() { return GhostSpacing; }, [this](double V) { GhostSpacing = V; }, 2);
        DeclareEditorMuted("Positions follow the actual kernel's optical axis; outlines are a geometry guide, not radiance or exact ghost sizes. Off-sensor centres are omitted.");
        DeclareEditorCard("Annular response", "", 3, "Aperture");
        DeclareEditorSlider("flare.haloRadius", "Halo radius", "", 0.01, 0.6, 0.01, [this]() { return HaloRadius; }, [this](double V) { HaloRadius = V; }, 2);
        DeclareEditorSlider("flare.haloWidth", "Halo width", "", 0.005, 0.2, 0.001, [this]() { return HaloWidth; }, [this](double V) { HaloWidth = V; }, 3);
        DeclareEditorSlider("flare.haloBright", "Halo brightness", "\xC3\x97", 0, 2, 0.01, [this]() { return HaloBrightness; }, [this](double V) { HaloBrightness = V; }, 2);
        DeclareEditorCard("Diffraction & streaks", "", 4, "Sparkles");
        DeclareEditorToggle("flare.anamorphic", "Anamorphic", "", false, [this]() { return Anamorphic; }, [this](bool On) { Anamorphic = On; });
        DeclareEditorToggle("flare.warm", "Warm streak", "", false, [this]() { return WarmStreak; }, [this](bool On) { WarmStreak = On; });
        DeclareEditorToggle("flare.burst", "Starburst", "", false, [this]() { return Burst; }, [this](bool On) { Burst = On; });
        DeclareEditorSlider("flare.rotation", "Rotation", "\xC2\xB0", 0, 360, 1, [this]() { return Rotation; }, [this](double V) { Rotation = V; }, 0);
        DeclareEditorSlider("flare.rays", "Ray pairs", "", 2, 12, 1, [this]() { return Rays; }, [this](double V) { Rays = V; }, 0);
        DeclareEditorSlider("flare.streak", "Streak gain", "\xC3\x97", 0, 3, 0.01, [this]() { return Streak; }, [this](double V) { Streak = V; }, 2);
        DeclareEditorSlider("flare.aperture", "Aperture blades", "", 2, 12, 1, [this]() { return Aperture; }, [this](double V) { Aperture = V; }, 0);
        DeclareEditorMuted("Preset starbursts use Aperture blades; Custom layers uses Ray pairs. Rotation turns the warm streak and burst, while the cool anamorphic streak stays horizontal, matching the renderer.");
#endif
    }
    bool FlareEnabled = true, Anamorphic = true, WarmStreak = false, Burst = true;
    double Intensity = 0.3, Spread = 1.0, Chromatic = 0.2, Ghosts = 6.0, GhostBrightness = 0.3, GhostSpacing = 1.0;
    double HaloRadius = 0.22, HaloWidth = 0.025, HaloBrightness = 0.2, Rotation = 0.0, Rays = 6.0, Streak = 0.3, Aperture = 6.0;
    size_t Preset = 0, GhostShape = 1;   // EnvironmentDefaults FlareGhostShape 6 = Hexagon
};

class WindRecord final : public Frontier::RecordEntry
{
  public:
    WindRecord() : RecordEntry(Frontier::RecordClassification::Environment)
    {
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
        DeclareEditorIdentity("ForceField", "Forcefield");
        DeclareEditorDescription("Shared weather force field");
        DeclareEditorToggle("wind.enabled", "Wind", "", true, [this]() { return WindEnabled; }, [this](bool On) { WindEnabled = On; }, "Wind");
        DeclareEditorCard("Direction of travel", "", 1, "Wind");
        DeclareEditorMetric("wind.speed", [this]() { char B[32]; std::snprintf(B, sizeof(B), "%.1f", Speed); return std::string(B); }, "m/s",
                            [this]() { char B[48]; std::snprintf(B, sizeof(B), "FROM %.0f\xC2\xB0", std::fmod(Bearing + 180.0 + 360.0, 360.0)); return std::pair<std::string, std::string>{B, WindEnabled ? "live" : ""}; });
        DeclareEditorSlider("wind.speedS", "Base speed", "m/s", 0, 40, 0.1, [this]() { return Speed; }, [this](double V) { Speed = V; }, 1);
        DeclareEditorSlider("wind.bearing", "Bearing from", "\xC2\xB0", 0, 360, 1, [this]() { return std::fmod(Bearing + 180.0 + 360.0, 360.0); }, [this](double V) { Bearing = std::fmod(V + 180.0 + 360.0, 360.0); }, 0);
        DeclareEditorMuted("Meteorological bearing is where wind comes FROM; the arrows show where it travels TO. Drag the source dot radially for base speed. Base-only reference: gusts, altitude and turbulence are excluded.");
        DeclareEditorCard("The gust envelope", "", 2, "Activity");
        DeclareEditorSlider("wind.gust", "Gust strength", "", 0, 1, 0.01, [this]() { return Gust; }, [this](double V) { Gust = V; }, 2);
        DeclareEditorMuted("Exact two-sine speed modulation under fixed settings, starting at phase zero. Shading is the guaranteed \xC2\xB1gust bound, not a measured history or a forecast of the running clock.");
        DeclareEditorCard("Cross-flow perturbation", "", 3, "Activity");
        DeclareEditorSlider("wind.turb", "Turbulence", "", 0, 1, 0.01, [this]() { return Turbulence; }, [this](double V) { Turbulence = V; }, 2);
        DeclareEditorMuted("Normalized sea-level traces at fixed base speed; labels give velocity bounds. X/Y use the existing .31/.27 rad/s oscillations; vertical motion uses .22 rad/s at 12% amplitude. Live gusts scale the actual perturbation.");
        DeclareEditorCard("Wind through altitude", "", 4, "Activity");
        DeclareEditorSlider("wind.shear", "Speed gain per km", "\xC3\x97/km", 0, 2, 0.01, [this]() { return Shear; }, [this](double V) { Shear = V; }, 2);
        DeclareEditorSlider("wind.veer", "Direction veer per km", "\xC2\xB0/km", -60, 60, 1, [this]() { return Veer; }, [this](double V) { Veer = V; }, 0);
        DeclareEditorMuted("Steady speed = base \xC3\x97 (1 + shear \xC3\x97 altitude in km). Travel bearing turns by veer \xC3\x97 altitude in km. Negative altitude is clamped to sea level. Gusts and turbulence are excluded here; vectors are plan-view glyphs, not vertically tilted air.");
        DeclareEditorCard("Where the field goes", "", 5, "Wind");
        DeclareEditorToggle("wind.cloudFollow", "Clouds", "Density travel at cloud-base altitude", false, [this]() { return CloudFollow; }, [this](bool On) { CloudFollow = On; });
        DeclareEditorToggle("wind.localCloudFollow", "Local cloud", "Placed bounds stay fixed; density travels", false, [this]() { return LocalCloudFollow; }, [this](bool On) { LocalCloudFollow = On; });
        DeclareEditorToggle("wind.localFogFollow", "Local fog", "Unlinked uses its own authored drift", false, [this]() { return LocalFogFollow; }, [this](bool On) { LocalFogFollow = On; });
        DeclareEditorSlider("wind.drift", "Precipitation wind drift", "", 0, 1, 0.01, [this]() { return Drift; }, [this](double V) { Drift = V; }, 2);
        DeclareEditorMuted("Linking a receiver does not enable it. Cloud/fog follow switches affect integrated density travel, not placement. Static height fog has no advected density. Tyre smoke/fire retains its separate gas-wind controls; no fake shared-wind binding is shown.");
#endif
    }
    bool WindEnabled = true, CloudFollow = true, LocalCloudFollow = true, LocalFogFollow = true;
    double Speed = 7.0, Bearing = 90.0, Gust = 0.25, Turbulence = 0.15, Shear = 0.15, Veer = 8.0, Drift = 0.5;
};

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

class FogRecord final : public Frontier::RecordEntry
{
  public:
    FogRecord() : RecordEntry(Frontier::RecordClassification::Environment)
    {
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
        DeclareEditorIdentity("Environment", "Environment");
        DeclareEditorDescription("Exponential vertical density");
        DeclareEditorToggle("fog.enabled", "Fog", "", true, [this]() { return FogEnabled; }, [this](bool On) { FogEnabled = On; }, "CloudFog");
        DeclareEditorCard("Height fog", "", 1, "CloudFog");
        DeclareEditorSlider("fog.density", "Density", "", 0, 0.01, 0.0001, [this]() { return Density; }, [this](double V) { Density = V; }, 4);
        DeclareEditorSlider("fog.height", "Height", "m", 0, 200, 1, [this]() { return Height; }, [this](double V) { Height = V; }, 0);
        DeclareEditorSlider("fog.scatter", "Scatter", "", 0, 1, 0.01, [this]() { return Scatter; }, [this](double V) { Scatter = V; }, 2);
        DeclareEditorCard("Atmospheric fog", "", 2, "CloudFog");
        DeclareEditorToggle("fog.aerial", "Aerial perspective", "", false, [this]() { return AerialEnabled; }, [this](bool On) { AerialEnabled = On; });
        DeclareEditorSlider("fog.aerialDensity", "Density", "", 0, 0.001, 0.00001, [this]() { return AerialDensity; }, [this](double V) { AerialDensity = V; }, 5);
        DeclareEditorSlider("fog.aerialStart", "Start", "m", 0, 1000, 1, [this]() { return AerialStart; }, [this](double V) { AerialStart = V; }, 0);
        DeclareEditorSlider("fog.aerialMie", "Mie", "", 0, 1, 0.01, [this]() { return AerialMie; }, [this](double V) { AerialMie = V; }, 2);
#endif
    }
    bool FogEnabled = false, AerialEnabled = true;
    double Density = 0.002, Height = 30.0, Scatter = 0.5, AerialDensity = 0.00004, AerialStart = 100.0, AerialMie = 0.5;
};


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
        static const ImWchar Extra[] = {0x2010, 0x2019, 0x201C, 0x201E, 0x2026, 0x2026, 0x2190, 0x21FF, 0x2200, 0x22FF, 0};
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
    if      (Page == "sun")       { PageRecord = std::make_unique<SunRecord>();          PageName = "Sun"; }
    else if (Page == "moon")      { PageRecord = std::make_unique<MoonRecord>();         PageName = "Moons"; }
    else if (Page == "flare")     { PageRecord = std::make_unique<FlareRecord>();        PageName = "Lens Flare"; }
    else if (Page == "wind")      { PageRecord = std::make_unique<WindRecord>();         PageName = "Wind"; }
    else if (Page == "field")     { PageRecord = std::make_unique<FieldRecord>();        PageName = "BVH & distance field"; }
    else if (Page == "cards")     { PageRecord = std::make_unique<SurfaceCardsRecord>(); PageName = "Surface cache"; }
    else if (Page == "particles") { PageRecord = std::make_unique<ParticlesRecord>();    PageName = "Particles"; }
    else if (Page == "fog")       { PageRecord = std::make_unique<FogRecord>();          PageName = "Height Fog"; }
    else                          { PageRecord = std::make_unique<RenderSettingsRecord>(); }
    SceneDirectory& Directory = SceneDirectory::Instance();
    (void)Directory.ConfigureName(PageRecord->Token(), PageName);

    SceneDirectoryPanel Outliner;
    SceneInspectorPanel Inspector;
    Outliner.Synchronize(Directory);
    Outliner.Select(PageRecord->Token());
    Outliner.Synchronize(Directory);
    Inspector.Synchronize(Directory, Outliner);

    for (int Frame = 0; Frame < 4; ++Frame)
    {
        Io.DeltaTime = 1.0f/60.0f;
        if (!Io.Fonts->IsBuilt()) Io.Fonts->Build();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0,0));
        ImGui::SetNextWindowSize(ImVec2((float)CaptureWidth,(float)CaptureHeight));
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse|ImGuiWindowFlags_NoBackground);
        Inspector.ConstructInterface();
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
