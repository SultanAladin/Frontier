/*==========================================================================================================================================
                                                        EVERESTWORKSPACE.CPP
==========================================================================================================================================*/
// 🧩 Project Everest workspace implementation: EverestApplication runtime + #if FRONTIER_EDITOR docked authoring interface.

#include "EverestWorkspace.h"

#include "Engine/Diagnostics/LogCategories.h"
#include "Engine/Diagnostics/LogStream.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Interface/LogStreamPanelRegistration.h"
#include "Engine/Interface/NativeInspectors.h"
#include "Engine/Interface/SceneDirectoryPanelRegistration.h"
#include "Engine/Interface/SceneInspectorPanel.h"
#include "Engine/Scene/SceneDirectory.h"
#include <imgui.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace Everest
{

namespace
{

uint32_t EnsureProjectLogCategory(std::string_view CategoryName)
{
    return Frontier::LogCategories().Register(CategoryName, Frontier::LogColour::Mint, true);
}

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

float MeasureLabelWidth(ImFont* Font, float Size, const char* Text) noexcept
{
    if (Font == nullptr || Text == nullptr || Text[0] == '\0')
    {
        return 0.0f;
    }
    return Font->CalcTextSizeA(Size, 1.0e9f, 0.0f, Text).x;
}

#endif

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTRUCTION & LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

EverestWorkspace::EverestWorkspace(Frontier::ControlCentrePanel& ControlCentre,
                                   std::filesystem::path         ProjectRootPath)
    : Centre(ControlCentre)
    , InspectorPanel(OutlinerPanel)
    , ProjectRoot(std::move(ProjectRootPath))
{
    InitializeWorld();

    OutlinerPanel.ConfigureConstructibleTypes(QuerySceneWorld().EnumerateRegisteredTypes());
    OutlinerPanel.Synchronize(Frontier::SceneDirectory::Instance());

    if (Frontier::SceneRecordBase* Solar = QuerySceneWorld().FindFirstByType("Solar"))
    {
        OutlinerPanel.Select(Solar->QueryToken());
        OutlinerPanel.Synchronize(Frontier::SceneDirectory::Instance());
    }

    Dockspace.ConfigureDockspaceKey("EverestWorkspaceDockspace");
    Dockspace.ConfigureWorkspaceInsets(50.0f, 0.0f);

    if (!Dockspace.Register(Frontier::MakeSceneDirectoryPanelSpecification(OutlinerPanel)) ||
        !Dockspace.Register({"Viewport", "Scene Viewport", [this]() { ConstructViewportSurface(); }, true}) ||
        !Dockspace.Register(Frontier::MakeLogStreamPanelSpecification(LogPanel)) ||
        !Dockspace.Register(Frontier::MakeSceneInspectorPanelSpecification(InspectorPanel)))
    {
        throw std::runtime_error("EverestWorkspace dockspace registration refused.");
    }

    (void)Dockspace.ConfigureStackedPanels("Viewport", "LogStream", 520.0f);
    (void)Dockspace.ConfigureColumns({
        {"Outliner", 0.200f},
        {"LogStream", 0.545f},
        {"Inspector", 0.255f},
    });

    ImGui::GetIO().IniFilename = nullptr;
    ObservedLayoutRevision     = Centre.QueryLayoutResetRevision();
}

#else

EverestWorkspace::EverestWorkspace(std::filesystem::path ProjectRootPath)
    : ProjectRoot(std::move(ProjectRootPath))
{
    InitializeWorld();
}

#endif

EverestWorkspace::~EverestWorkspace() = default;

void EverestWorkspace::InitializeWorld()
{
    const uint32_t ProjectCategory = EnsureProjectLogCategory("Everest");
    (void)EnsureProjectLogCategory("Physics");

    const std::filesystem::path ProjectSpacePath = ProjectRoot / "Everest.projectspace";
    if (std::filesystem::exists(ProjectSpacePath))
    {
        if (Application.Initialize(ProjectSpacePath, &Diagnostic))
        {
            ActiveLevelPath = QuerySceneWorld().QueryLoadedLevelPath();
#if FRONTIER_EDITOR
            Frontier::SceneDirectory::Instance().ConfigureActiveSceneHeader(
                ActiveLevelPath.stem().string(), "Frontier LevelSpace");
#endif
            Frontier::WriteLogFormat(ProjectCategory, Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log,
                                     "EverestWorkspace", "Loaded '%s' (%zu scene records).",
                                     ActiveLevelPath.filename().string().c_str(),
                                     QuerySceneWorld().QueryRecordCount());
        }
        else
        {
            Frontier::WriteLogFormat(ProjectCategory, Frontier::LogSeverity::Error, Frontier::LogVerbosity::Log,
                                     "EverestWorkspace", "Project startup failed: %s", Diagnostic.c_str());
        }
    }
    else
    {
        ActiveLevelPath = ProjectRoot / "Content" / "Levels" / "Default.levelspace";
        RegisterEverestSimulationTypes(QuerySceneWorld());
        if (std::filesystem::exists(ActiveLevelPath) && QuerySceneWorld().LoadLevelSpace(ActiveLevelPath, &Diagnostic))
        {
#if FRONTIER_EDITOR
            Frontier::SceneDirectory::Instance().ConfigureActiveSceneHeader(
                ActiveLevelPath.stem().string(), "Frontier LevelSpace");
#endif
        }
        else
        {
            Diagnostic = "Project container missing: " + ProjectSpacePath.string();
            Frontier::WriteLogFormat(ProjectCategory, Frontier::LogSeverity::Warning, Frontier::LogVerbosity::Log,
                                     "EverestWorkspace", "Initialized empty SceneWorld (%s).", Diagnostic.c_str());
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    RUNTIME SIMULATION
//------------------------------------------------------------------------------------------------------------------------

void EverestWorkspace::IntegrateSimulation(double DeltaSeconds)
{
    const float ClampedDt = static_cast<float>(std::clamp(DeltaSeconds, 0.0, 0.25));
    ElapsedSeconds += static_cast<double>(ClampedDt);

    Application.StepFrame(ClampedDt);

    if (auto* DiagRecord = dynamic_cast<Frontier::DiagnosticsRecord*>(QuerySceneWorld().FindFirstByType("Diagnostics")))
    {
        if (ClampedDt > 1.0e-5f)
        {
            auto& Diag           = DiagRecord->AccessConfiguration();
            const float Instant  = ClampedDt * 1000.0f;
            Diag.FrameTimeMs     = Diag.FrameTimeMs * 0.9f + Instant * 0.1f;
            Diag.FramesPerSecond = 1000.0f / std::max(0.25f, Diag.FrameTimeMs);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    EDITOR INTERFACE
//------------------------------------------------------------------------------------------------------------------------

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

bool EverestWorkspace::ConfigurePlatform() noexcept
{
    Dockspace.ConfigureDetachedWindows(Centre.QueryDetachedWindows());
    return Dockspace.ConfigurePlatform();
}

void EverestWorkspace::ServiceDynamicSceneRequests()
{
    const uint32_t ProjectCategory = EnsureProjectLogCategory("Everest");
    bool           Mutated         = false;

    while (const auto RequestedType = OutlinerPanel.ConsumeConstructRequest())
    {
        Frontier::RecordToken ParentToken{};
        if (*RequestedType == "OpticalFlare")
        {
            if (Frontier::SceneRecordBase* Solar = QuerySceneWorld().FindFirstByType("Solar"))
            {
                ParentToken = Solar->QueryToken();
            }
        }
        const Frontier::RecordToken Created = QuerySceneWorld().ConstructRecord(*RequestedType, {}, ParentToken);
        if (Created.IsValid())
        {
            OutlinerPanel.Synchronize(Frontier::SceneDirectory::Instance());
            OutlinerPanel.Select(Created);
            Mutated = true;
            Frontier::WriteLogFormat(ProjectCategory, Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log,
                                     "EverestWorkspace", "Added record '%s' (type '%s', token #%llu).",
                                     Frontier::SceneDirectory::Instance().QueryName(Created).c_str(),
                                     RequestedType->c_str(),
                                     static_cast<unsigned long long>(Created.Value));
        }
    }

    while (const auto DeleteToken = OutlinerPanel.ConsumeDeleteRequest())
    {
        const std::string OldName = Frontier::SceneDirectory::Instance().QueryName(*DeleteToken);
        if (QuerySceneWorld().DestroyRecord(*DeleteToken))
        {
            Mutated = true;
            Frontier::WriteLogFormat(ProjectCategory, Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log,
                                     "EverestWorkspace", "Removed record '%s' (token #%llu).",
                                     OldName.c_str(),
                                     static_cast<unsigned long long>(DeleteToken->Value));
        }
    }

    if (Mutated)
    {
        OutlinerPanel.Synchronize(Frontier::SceneDirectory::Instance());
    }
}

void EverestWorkspace::ConstructInterface(double DeltaSeconds)
{
    IntegrateSimulation(DeltaSeconds);

    if (ObservedLayoutRevision != Centre.QueryLayoutResetRevision())
    {
        ObservedLayoutRevision = Centre.QueryLayoutResetRevision();
        Dockspace.ResetLayout();
        (void)Dockspace.ConfigureStackedPanels("Viewport", "LogStream", 520.0f);
        (void)Dockspace.ConfigureColumns({
            {"Outliner", 0.200f},
            {"LogStream", 0.545f},
            {"Inspector", 0.255f},
        });
    }

    ServiceDynamicSceneRequests();
    OutlinerPanel.Synchronize(Frontier::SceneDirectory::Instance());
    LogPanel.Synchronize(Frontier::LogStore(), Frontier::LogCategories(), DeltaSeconds);

    const float Scale = Centre.QueryAppearance().QueryApplied().InterfaceScale / 100.0f;
    ConstructTopToolbar(Scale);
    Dockspace.ConstructInterface(Scale);
    Frontier::InspectorRuntime::Instance().ConstructActiveEditor();
}

void EverestWorkspace::ConstructTopToolbar(float Scale)
{
    const Frontier::InspectorPalette Palette = Frontier::InspectorPalette::Resolve();
    const Frontier::EditorTypefaces  Faces   = Frontier::EditorTypefaces::Resolve();
    const ImGuiViewport*             MainVp  = ImGui::GetMainViewport();
    const ImVec2                     BarPos  = MainVp->Pos;
    const ImVec2                     BarSize(MainVp->Size.x, 50.0f);

    ImGui::SetNextWindowPos(BarPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(BarSize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(Palette.ColumnBg));
    if (ImGui::Begin("##EverestTopToolbar", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus))
    {
        ImDrawList* DrawList = ImGui::GetWindowDrawList();
        DrawList->AddLine(ImVec2(BarPos.x, BarPos.y + BarSize.y - 1.0f),
                          ImVec2(BarPos.x + BarSize.x, BarPos.y + BarSize.y - 1.0f),
                          Palette.EdgeBorder, 1.0f);

        float       CursorX = BarPos.x + 14.0f;
        const float BtnY    = BarPos.y + 10.0f;
        const float BtnH    = 30.0f;

        {
            const float BackW = 104.0f;
            ImGui::SetCursorScreenPos(ImVec2(CursorX, BtnY));
            if (ImGui::InvisibleButton("##ReturnProjectsBtn", ImVec2(BackW, BtnH)) && EvaluateReturnToBrowser)
            {
                EvaluateReturnToBrowser();
            }
            const bool Hover = ImGui::IsItemHovered();
            DrawList->AddRectFilled(ImVec2(CursorX, BtnY), ImVec2(CursorX + BackW, BtnY + BtnH),
                                    Hover ? Palette.SegmentedActiveBg : Palette.CountCardBg, 8.0f);
            DrawList->AddRect(ImVec2(CursorX, BtnY), ImVec2(CursorX + BackW, BtnY + BtnH), Palette.CountCardBorder, 8.0f);
            Frontier::ConstructEditorGlyph(DrawList, ImVec2(CursorX + 15.0f, BtnY + BtnH * 0.5f), 4.5f,
                                           Palette.TextSecondary, "ChevronRight", 1.4f);
            DrawList->AddText(Faces.Medium, 11.5f * Scale, ImVec2(CursorX + 26.0f, BtnY + 8.5f),
                              Palette.TextPrimary, "Projects");
            CursorX += BackW + 14.0f;
        }

        Frontier::ConstructEditorGlyph(DrawList, ImVec2(CursorX + 8.0f, BarPos.y + 25.0f), 7.0f,
                                       Palette.ThemeAccent, "Layers", 1.5f);
        DrawList->AddText(Faces.SemiBold, 13.5f, ImVec2(CursorX + 22.0f, BarPos.y + 17.0f),
                          Palette.TextPrimary, "Everest");
        CursorX += 22.0f + MeasureLabelWidth(Faces.SemiBold, 13.5f, "Everest") + 12.0f;

        const std::string LevelFilename = ActiveLevelPath.empty()
                                              ? std::string("Default.levelspace")
                                              : ActiveLevelPath.filename().string();
        char LevelBadge[128]{};
        std::snprintf(LevelBadge, sizeof(LevelBadge), "%s · %zu records",
                      LevelFilename.c_str(), QuerySceneWorld().QueryRecordCount());
        const float BadgeW = MeasureLabelWidth(Faces.Monospace, 10.5f, LevelBadge) + 18.0f;
        DrawList->AddRectFilled(ImVec2(CursorX, BtnY + 3.0f), ImVec2(CursorX + BadgeW, BtnY + BtnH - 3.0f),
                                Palette.InsetFieldBg, 6.0f);
        DrawList->AddRect(ImVec2(CursorX, BtnY + 3.0f), ImVec2(CursorX + BadgeW, BtnY + BtnH - 3.0f),
                          Palette.InsetFieldBorder, 6.0f);
        DrawList->AddText(Faces.Monospace, 10.5f, ImVec2(CursorX + 9.0f, BtnY + 9.0f),
                          Palette.TextSecondary, LevelBadge);

        float RightX = BarPos.x + BarSize.x - 14.0f;

        {
            const float SettingsW = 148.0f;
            RightX -= SettingsW;
            ImGui::SetCursorScreenPos(ImVec2(RightX, BtnY));
            if (ImGui::InvisibleButton("##EverestOpenAppearanceBtn", ImVec2(SettingsW, BtnH)))
            {
                Centre.ActivateSettingsPage(Frontier::ControlCentrePageCategory::Appearance);
            }
            const bool Hover = ImGui::IsItemHovered();
            DrawList->AddRectFilled(ImVec2(RightX, BtnY), ImVec2(RightX + SettingsW, BtnY + BtnH),
                                    Hover ? Palette.SegmentedActiveBg : Palette.CountCardBg, 8.0f);
            DrawList->AddRect(ImVec2(RightX, BtnY), ImVec2(RightX + SettingsW, BtnY + BtnH),
                              Palette.CountCardBorder, 8.0f);
            Frontier::ConstructEditorGlyph(DrawList, ImVec2(RightX + 16.0f, BtnY + BtnH * 0.5f), 6.0f,
                                           Palette.ThemeAccent, "Sliders", 1.4f);
            DrawList->AddText(Faces.Medium, 11.0f, ImVec2(RightX + 29.0f, BtnY + 9.0f),
                              Palette.TextPrimary, "Control Centre");
            RightX -= 10.0f;
        }

        {
            const float SaveW = 132.0f;
            RightX -= SaveW;
            ImGui::SetCursorScreenPos(ImVec2(RightX, BtnY));
            if (ImGui::InvisibleButton("##EverestSaveLevelBtn", ImVec2(SaveW, BtnH)))
            {
                std::string    SaveDiag;
                const uint32_t ProjectCategory = EnsureProjectLogCategory("Everest");
                const std::filesystem::path TargetPath = ActiveLevelPath.empty()
                                                             ? (ProjectRoot / "Content" / "Levels" / "Default.levelspace")
                                                             : ActiveLevelPath;
                if (QuerySceneWorld().SaveLevelSpace(TargetPath, &SaveDiag))
                {
                    ActiveLevelPath = TargetPath;
                    Frontier::WriteLogFormat(ProjectCategory, Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log,
                                             "EverestWorkspace", "Saved '%s' (%zu records).",
                                             TargetPath.filename().string().c_str(),
                                             QuerySceneWorld().QueryRecordCount());
                }
                else
                {
                    Diagnostic = SaveDiag;
                    Frontier::WriteLogFormat(ProjectCategory, Frontier::LogSeverity::Error, Frontier::LogVerbosity::Log,
                                             "EverestWorkspace", "Save failed: %s", SaveDiag.c_str());
                }
            }
            const bool Hover = ImGui::IsItemHovered();
            DrawList->AddRectFilled(ImVec2(RightX, BtnY), ImVec2(RightX + SaveW, BtnY + BtnH),
                                    Hover ? Palette.SegmentedActiveBg : Palette.CountCardBg, 8.0f);
            DrawList->AddRect(ImVec2(RightX, BtnY), ImVec2(RightX + SaveW, BtnY + BtnH),
                              Palette.CountCardBorder, 8.0f);
            DrawList->AddText(Faces.Medium, 11.0f, ImVec2(RightX + 14.0f, BtnY + 9.0f),
                              Palette.TextSecondary, "Save .levelspace");
            RightX -= 10.0f;
        }

        {
            const auto* PipelineRecord = dynamic_cast<const Frontier::RenderPipelineRecord*>(
                const_cast<Frontier::SceneWorld&>(QuerySceneWorld()).FindFirstByType("RenderPipeline"));
            const char* TierText = "TIER: RTX (VK_KHR_ray_query + ReSTIR)";
            ImU32       TierTint = Palette.LiveGreen;
            if (PipelineRecord != nullptr && !PipelineRecord->QueryConfiguration().HardwareRayQueryTier)
            {
                TierText = "TIER: GTX (4-Cascade SDF + SAH-2 BVH)";
                TierTint = Palette.WarmAmber;
            }
            const float PillW = MeasureLabelWidth(Faces.Monospace, 10.0f, TierText) + 26.0f;
            RightX -= PillW;
            DrawList->AddRectFilled(ImVec2(RightX, BtnY + 3.0f), ImVec2(RightX + PillW, BtnY + BtnH - 3.0f),
                                    Frontier::ModulateSpectrumAlpha(TierTint, 0.14f), 6.0f);
            DrawList->AddRect(ImVec2(RightX, BtnY + 3.0f), ImVec2(RightX + PillW, BtnY + BtnH - 3.0f),
                              Frontier::ModulateSpectrumAlpha(TierTint, 0.45f), 6.0f);
            DrawList->AddCircleFilled(ImVec2(RightX + 10.0f, BtnY + BtnH * 0.5f), 3.0f, TierTint);
            DrawList->AddText(Faces.Monospace, 10.0f, ImVec2(RightX + 18.0f, BtnY + 9.0f), TierTint, TierText);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void EverestWorkspace::ConstructViewportSurface()
{
    const Frontier::InspectorPalette Palette = Frontier::InspectorPalette::Resolve();
    const Frontier::EditorTypefaces  Faces   = Frontier::EditorTypefaces::Resolve();

    const ImVec2 Avail = ImGui::GetContentRegionAvail();
    const float  W     = std::max(64.0f, Avail.x);
    const float  H     = std::max(64.0f, Avail.y);
    const ImVec2 Min   = ImGui::GetCursorScreenPos();
    const ImVec2 Max(Min.x + W, Min.y + H);
    ImGui::InvisibleButton("##SceneViewportCanvas", ImVec2(W, H));

    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    DrawList->PushClipRect(Min, Max, true);

    float SunElevation = 38.0f;
    float SunAzimuth   = 132.0f;
    if (const auto* SolarRec = dynamic_cast<const Frontier::SolarRecord*>(QuerySceneWorld().FindFirstByType("Solar")))
    {
        SunElevation = SolarRec->QueryConfiguration().ElevationDegrees;
        SunAzimuth   = SolarRec->QueryConfiguration().AzimuthDegrees;
    }

    const float DayFactor  = std::clamp((SunElevation + 6.0f) / 55.0f, 0.05f, 1.0f);
    const ImU32 ZenithCol  = IM_COL32(static_cast<int>(14 + 28 * DayFactor),
                                      static_cast<int>(22 + 54 * DayFactor),
                                      static_cast<int>(38 + 92 * DayFactor), 255);
    const ImU32 HorizonCol = IM_COL32(static_cast<int>(28 + 86 * DayFactor),
                                      static_cast<int>(38 + 96 * DayFactor),
                                      static_cast<int>(54 + 108 * DayFactor), 255);
    const ImU32 GroundNear = IM_COL32(19, 22, 27, 255);
    const ImU32 GroundFar  = IM_COL32(28, 33, 41, 255);

    const float HorizonY = Min.y + H * 0.56f;
    DrawList->AddRectFilledMultiColor(Min, ImVec2(Max.x, HorizonY), ZenithCol, ZenithCol, HorizonCol, HorizonCol);
    DrawList->AddRectFilledMultiColor(ImVec2(Min.x, HorizonY), Max, GroundFar, GroundFar, GroundNear, GroundNear);
    DrawList->AddLine(ImVec2(Min.x, HorizonY), ImVec2(Max.x, HorizonY),
                      Frontier::ModulateSpectrumAlpha(Palette.TextSecondary, 0.35f), 1.0f);

    // Analytical directional sun disc above the horizon.
    if (SunElevation > -2.0f)
    {
        const float AzNorm = std::fmod(SunAzimuth, 360.0f) / 360.0f;
        const float SunX   = Min.x + W * (0.18f + 0.64f * AzNorm);
        const float SunY   = HorizonY - (std::clamp(SunElevation, 0.0f, 90.0f) / 90.0f) * (HorizonY - Min.y - 36.0f);
        DrawList->AddCircleFilled(ImVec2(SunX, SunY), 22.0f, IM_COL32(255, 222, 148, 38), 32);
        DrawList->AddCircleFilled(ImVec2(SunX, SunY), 8.5f, IM_COL32(255, 244, 214, 235), 24);
    }

    // Perspective metric reference plane (clean general-purpose 3D viewport floor grid).
    const float  VanishX  = (Min.x + Max.x) * 0.5f;
    const ImU32  GridLine = IM_COL32(130, 145, 168, 34);
    const ImU32  AxisX    = IM_COL32(214, 92, 92, 130);
    const ImU32  AxisZ    = IM_COL32(88, 152, 224, 130);
    for (int LineIdx = -12; LineIdx <= 12; ++LineIdx)
    {
        const float TopOffset    = static_cast<float>(LineIdx) * (W * 0.014f);
        const float BottomOffset = static_cast<float>(LineIdx) * (W * 0.095f);
        const ImU32 Tint         = (LineIdx == 0) ? AxisZ : GridLine;
        DrawList->AddLine(ImVec2(VanishX + TopOffset, HorizonY),
                          ImVec2(VanishX + BottomOffset, Max.y),
                          Tint, LineIdx == 0 ? 1.5f : 1.0f);
    }
    for (int Row = 1; Row <= 10; ++Row)
    {
        const float T  = static_cast<float>(Row) / 10.0f;
        const float Y  = HorizonY + (T * T) * (Max.y - HorizonY);
        const ImU32 C  = (Row == 4) ? AxisX : GridLine;
        DrawList->AddLine(ImVec2(Min.x, Y), ImVec2(Max.x, Y), C, Row == 4 ? 1.4f : 1.0f);
    }

    // Minimal viewport technical readout card.
    int InspectionMode = 0;
    if (const auto* PipeRec = dynamic_cast<const Frontier::RenderPipelineRecord*>(QuerySceneWorld().FindFirstByType("RenderPipeline")))
    {
        InspectionMode = std::clamp(PipeRec->QueryConfiguration().InspectionModeIndex, 0, 14);
    }
    char OverlayLine[128]{};
    std::snprintf(OverlayLine, sizeof(OverlayLine), "VIEW: %s  |  GRID: 5.0 m  |  SIM: %.1f s",
                  Frontier::RenderInspectionModes::ModeNames[InspectionMode], ElapsedSeconds);
    const float CardW = MeasureLabelWidth(Faces.Monospace, 10.0f, OverlayLine) + 20.0f;
    DrawList->AddRectFilled(ImVec2(Min.x + 12.0f, Min.y + 12.0f),
                            ImVec2(Min.x + 12.0f + CardW, Min.y + 36.0f),
                            IM_COL32(14, 17, 22, 196), 6.0f);
    DrawList->AddRect(ImVec2(Min.x + 12.0f, Min.y + 12.0f),
                      ImVec2(Min.x + 12.0f + CardW, Min.y + 36.0f),
                      Palette.CountCardBorder, 6.0f);
    DrawList->AddText(Faces.Monospace, 10.0f, ImVec2(Min.x + 22.0f, Min.y + 18.5f),
                      Palette.TextSecondary, OverlayLine);

    DrawList->PopClipRect();
}

#endif // FRONTIER_EDITOR && FRONTIER_WITH_INTERFACE_SDK

} // namespace Everest
