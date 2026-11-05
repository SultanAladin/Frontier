//==========================================================================================================================================
//                                                        PHASE3VALIDATION.CPP
//==========================================================================================================================================
//  Host-side verification for the Phase 3 C++20 engine ports (Track 3A HMI configurator, Track 3B render
//  optimizations, Track 3C volumetric fluids, Track 3D particle system). Portable: no Vulkan, no Win32, no
//  GPU device. Rendering paths are exercised through Frontier/Game/SoftwarePresentation (PPM) and the
//  render-graph declaration units; every Slang shader is compiled to SPIR-V by the external Slang toolchain
//  (Automation/CompileSurfaceShaders + /tmp/check_slang) rather than linked here.
//
//  Build (host verification):
//      g++ -std=c++20 -O2 -Wall -Wextra -I. -IFrontier Validation/Phase3Validation.cpp <engine sources> -o phase3
//==========================================================================================================================================

#include "Frontier/Engine/Effects/DihedralEdges.h"
#include "Frontier/Engine/Effects/MaterialTransition.h"
#include "Frontier/Engine/Effects/ScanSparkles.h"
#include "Frontier/Engine/Effects/VehicleInspection.h"
#include "Frontier/Engine/Rendering/EmissiveLightSpace.h"
#include "Frontier/Engine/Rendering/HmiViewportUnits.h"
#include "Frontier/Engine/Rendering/LightClusterSpace.h"
#include "Frontier/Engine/Rendering/NearestDepthSpace.h"
#include "Frontier/Engine/Rendering/Photometry.h"
#include "Frontier/Engine/Rendering/PlumeSourceProjection.h"
#include "Frontier/Engine/Rendering/PlumeSpace.h"
#include "Frontier/Engine/Rendering/SmokeSequence.h"
#include "Frontier/Engine/Rendering/SparkCollision.h"
#include "Frontier/Engine/Rendering/SparkSequence.h"
#include "Frontier/Engine/Rendering/EffectSequence.h"
#include "Frontier/Engine/Scene/FacetArchive.h"
#include "Frontier/Engine/Scene/VehicleCabin.h"
#include "Frontier/Game/SoftwarePresentation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{

using namespace Frontier;

int  FailureCount = 0;
int  CheckCount = 0;

void Check(bool Condition, const char* Description) noexcept
{
    ++CheckCount;
    if (!Condition)
    {
        ++FailureCount;
        std::printf("FAIL: %s\n", Description);
    }
}

[[nodiscard]] bool Near(float Value, float Expected, float Tolerance = 1e-4f) noexcept
{
    return std::fabs(Value - Expected) <= Tolerance;
}

[[nodiscard]] bool NearDouble(double Value, double Expected, double Tolerance = 1e-9) noexcept
{
    return std::fabs(Value - Expected) <= Tolerance;
}

// Names a repeated check so a failure says which iteration failed.
[[nodiscard]] std::string Named(const char* Description, uint32_t Index)
{
    return std::string(Description) + " [" + std::to_string(Index) + "]";
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                              RECORD FIELD DECODING (TRACK 3B)
//--------------------------------------------------------------------------------------------------------------------------------------

template <typename T>
[[nodiscard]] T RecordWord(std::span<const uint8_t> Bytes, uint32_t Index) noexcept
{
    T Value{};
    if ((size_t(Index) + 1u) * sizeof(T) <= Bytes.size())
        std::memcpy(&Value, Bytes.data() + size_t(Index) * sizeof(T), sizeof(T));
    return Value;
}

[[nodiscard]] float RecordFloat(std::span<const uint8_t> Bytes, uint32_t Index) noexcept
{
    return RecordWord<float>(Bytes, Index);
}

[[nodiscard]] uint32_t RecordUint(std::span<const uint8_t> Bytes, uint32_t Index) noexcept
{
    return RecordWord<uint32_t>(Bytes, Index);
}

[[nodiscard]] int32_t RecordInt(std::span<const uint8_t> Bytes, uint32_t Index) noexcept
{
    return RecordWord<int32_t>(Bytes, Index);
}

// MaterialTransition::Advance clamps every call to 0.10 s (one animation frame), so long spans are
// replayed as whole frames rather than delivered as a single oversized step.
void StepRipple(MaterialTransition& Ripple, float Seconds) noexcept
{
    float Remaining = Seconds;
    while (Remaining > 1e-6f)
    {
        const float Step = std::min(Remaining, 0.10f);
        Ripple.Advance(Step);
        Remaining -= Step;
    }
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                          TRACK 3A — IN-CABIN HMI CONFIGURATOR
//--------------------------------------------------------------------------------------------------------------------------------------

void ValidateInspectionContract()
{
    VehicleInspection Inspection;
    Check(Inspection.ResolveFaceOpacity() == 1.0f && !Inspection.ShowsHiddenEdges(),
          "3A: solid view is opaque with no hidden edges");
    Check(Inspection.View == InspectionView::Solid, "3A: inspection defaults to solid");

    Inspection.Cycle();
    Check(Inspection.View == InspectionView::Ghost && Near(Inspection.ResolveFaceOpacity(), 0.45f),
          "3A: ghost view ghosts faces at 45%");
    Check(Inspection.ShowsHiddenEdges(), "3A: ghost keeps hidden construction visible");

    Inspection.Cycle();
    Check(Inspection.View == InspectionView::Wireframe && Inspection.ResolveFaceOpacity() == 0.0f,
          "3A: wireframe draws no faces");

    Inspection.Cycle();
    Check(Inspection.View == InspectionView::Scan && Inspection.ShowsHiddenEdges(),
          "3A: scan is an X-ray family view");
    Inspection.ScanSeconds = 1.6f;
    Check(Near(Inspection.ResolveScanFraction(), 0.5f, 0.01f), "3A: the editor band sits mid-car half a period in");
    Inspection.Advance(3.2f);
    Check(Inspection.ResolveScanFraction() < 0.51f, "3A: the editor band wraps nose-to-tail");

    // 1:1 configurator X-Ray Scan cycle: 4.6 s sweep + 1.0 s hold + 1.0 s fade.
    VehicleInspection Scan;
    Scan.SetView(InspectionView::Scan);
    Check(Scan.ScanPeriodSeconds == 3.2f, "3A: the scan keeps the 3.2 s editor sweep period");
    Check(Near(Scan.ResolveScanX(), VehicleInspection::DefaultFrontX), "3A: the sweep starts at the nose (+3.25 m)");
    Check(Near(Scan.ResolveSweepFraction(), 0.0f), "3A: eased sweep fraction starts at the nose");

    Scan.Advance(2.3f);   // exactly half the 4.6 s sweep; ease-in-out-cubic(0.5) == 0.5
    Check(Near(Scan.ResolveSweepFraction(), 0.5f, 0.01f), "3A: half a sweep puts the eased band mid-car");
    Check(Near(Scan.ResolveScanX(), (VehicleInspection::DefaultFrontX + VehicleInspection::DefaultRearX) * 0.5f, 0.02f),
          "3A: the mid-car band sits between the nose and tail bounds");

    Scan.Advance(2.3f);   // 4.6 s: sweep complete
    Check(Near(Scan.ResolveScanX(), VehicleInspection::DefaultRearX, 0.02f), "3A: the sweep ends at the tail (-3.35 m)");
    Check(Near(Scan.ResolveCycleAmount(), 1.0f), "3A: the hold keeps the holo envelope at full");

    Scan.Advance(1.0f);   // 5.6 s: hold ends, the 1.0 s fade opens
    Check(Near(Scan.ResolveCycleAmount(), 1.0f, 0.02f), "3A: the fade has not begun at the end of the hold");
    Scan.Advance(0.5f);   // 6.1 s: halfway through the fade
    Check(Near(Scan.ResolveCycleAmount(), 0.5f, 0.02f), "3A: the holo envelope is halfway down the fade");
    Scan.Advance(0.6f);   // 6.7 s: wrapped into the next cycle
    Check(Near(Scan.ResolveCycleAmount(), 1.0f, 0.02f), "3A: the scan cycle wraps into a fresh sweep");
    Check(Near(Scan.ResolveScanX(), VehicleInspection::DefaultFrontX, 0.35f), "3A: the wrapped sweep restarts at the nose");

    // Ghost/Wireframe pin the plane off either end so every surface is revealed.
    VehicleInspection Ghost;
    Ghost.SetView(InspectionView::Ghost);
    Check(Ghost.ResolveScanX() < VehicleInspection::DefaultRearX, "3A: ghost reveals the whole vehicle");
    VehicleInspection Solid;
    Check(Solid.ResolveScanX() > VehicleInspection::DefaultFrontX, "3A: solid reveals nothing");

    // Lamps (families 10, 13, 14) never dissolve; bodywork does behind the band.
    VehicleInspection Dissolve;
    Dissolve.SetView(InspectionView::Scan);
    Dissolve.Advance(2.3f);
    // The sweep erases solid bodywork as it passes: everything the band has already crossed (body-local X
    // ahead of the plane) yields to the hologram, while metal the plane has not reached stays painted.
    Check(Dissolve.ResolveSolidAlpha(0.0f, 6u) < 0.2f, "3A: swept bodywork dissolves away");
    Check(Dissolve.ResolveSolidAlpha(2.90f, 6u) < 0.2f, "3A: the nose dissolves once the band has crossed it");
    Check(Dissolve.ResolveSolidAlpha(-2.00f, 6u) == 1.0f, "3A: bodywork ahead of the band stays solid");
    Check(Dissolve.ResolveSolidAlpha(0.0f, 10u) == 1.0f, "3A: headlamps stay solid through the scan");
    Check(Dissolve.ResolveSolidAlpha(0.0f, 13u) == 1.0f, "3A: indicator lamps stay solid through the scan");
    Check(Dissolve.ResolveSolidAlpha(0.0f, 14u) == 1.0f, "3A: tail lamps stay solid through the scan");

    // Curtains only exist mid-sweep; the core sheet is 4x the wide glow.
    VehicleInspection Curtain;
    Curtain.SetView(InspectionView::Scan);
    Curtain.Advance(2.3f);
    const auto MidSweep = Curtain.ResolveCurtainOpacities();
    Check(MidSweep[0] > 0.0f && MidSweep[1] > MidSweep[0], "3A: the core curtain sheet outshines the wide glow");
    VehicleInspection Parked;
    Parked.SetView(InspectionView::Solid);
    const auto NoCurtain = Parked.ResolveCurtainOpacities();
    Check(NoCurtain[0] == 0.0f && NoCurtain[1] == 0.0f, "3A: no curtains outside the scan sweep");

    // The 96-byte-aligned GPU block must keep its exact layout.
    Check(sizeof(ScanEffectRecord) == 128 && alignof(ScanEffectRecord) == 16,
          "3A: ScanEffectRecord stays a 128 B 16-byte-aligned uniform block");
    ScanEffectRecord Record{};
    Check(Record.ScanParameters[0] == 3.25f && Record.BoundsAndSparkle[0] == 3.25f && Record.BoundsAndSparkle[1] == -3.35f,
          "3A: the scan uniform block defaults to the nose/tail bounds");
}

void ValidatePaintFinishes()
{
    Check(ConfiguratorPaintFinishes.size() == 8, "3A: eight configurator paint finishes");
    bool AllDistinct = true;
    for (size_t A = 0; A < ConfiguratorPaintFinishes.size(); ++A)
        for (size_t B = A + 1; B < ConfiguratorPaintFinishes.size(); ++B)
            if (ConfiguratorPaintFinishes[A].HexSrgb == ConfiguratorPaintFinishes[B].HexSrgb)
                AllDistinct = false;
    Check(AllDistinct, "3A: every paint finish owns a distinct sRGB swatch");
    Check(ConfiguratorPaintFinishes[0].HexSrgb == 0xc2c8d0u, "3A: Liquid Silver leads the swatch rail");
    Check(ConfiguratorPaintFinishes[1].HexSrgb == 0x17191du, "3A: Obsidian follows the swatch rail");

    MaterialTransition Ripple;
    Check(!Ripple.Active, "3A: the ripple rests until a swatch is picked");
    Check(Near(Ripple.ResolveRadiusMetres(), MaterialTransition::MaxRadiusMetres), "3A: a resting ripple fronts at 6.4 m");
    Ripple.TriggerFinish(1);
    Check(Ripple.Active && Ripple.CurrentFinishIndex == 1u && Ripple.PreviousFinishIndex == 0u,
          "3A: picking a swatch starts a previous-to-current ripple");
    Check(Ripple.OriginBodyLocal == MaterialTransition::DefaultNoseOrigin,
          "3A: a click (no drop point) ripples from the nose origin");
    Check(Near(Ripple.ResolveRadiusMetres(), 0.0f), "3A: the ripple front starts at the drop point");

    StepRipple(Ripple, 0.675f);   // half the 1.35 s duration: (1 - 0.5^3) * 6.4 == 5.6 m
    Check(Near(Ripple.ResolveRadiusMetres(), 5.6f, 0.05f), "3A: the ripple front cubic-eases to 5.6 m at half duration");
    StepRipple(Ripple, 0.70f);
    Check(!Ripple.Active && Near(Ripple.ResolveRadiusMetres(), MaterialTransition::MaxRadiusMetres, 0.05f),
          "3A: the ripple retires at 6.4 m after 1.35 s");
    Check(Ripple.CurrentPaint == ConfiguratorPaintFinishes[1].LinearAlbedo &&
          Ripple.PreviousPaint == ConfiguratorPaintFinishes[1].LinearAlbedo,
          "3A: a retired ripple has fully committed to the picked finish");

    // The mask feathers over 0.55 m and is anchored at the 3D drop point.
    MaterialTransition Masked;
    const std::array<float, 3> Drop = { 1.20f, 0.0f, 0.72f };
    Masked.TriggerFinish(2, &Drop);
    Check(Masked.OriginBodyLocal == Drop, "3A: a dropped swatch ripples from its 3D raycast hit");
    StepRipple(Masked, 0.30f);
    const float Radius = Masked.ResolveRadiusMetres();
    Check(Radius > 0.0f && Radius < MaterialTransition::MaxRadiusMetres, "3A: the front is mid-flight 0.30 s in");
    Check(Masked.ResolveMask(Drop) > 0.99f, "3A: the drop point is fully repainted");
    Check(Masked.ResolveMask({ Drop[0] + Radius + 2.0f, 0.0f, 0.72f }) < 0.01f,
          "3A: metal well beyond the front keeps the old finish");

    // Blending returns the previous finish far away and the new one at the origin.
    const std::array<float, 3> AtOrigin = Masked.ResolveAlbedo(Drop);
    const std::array<float, 3> FarAway = Masked.ResolveAlbedo({ Drop[0] + Radius + 4.0f, 0.0f, 0.72f });
    Check(Near(AtOrigin[0], ConfiguratorPaintFinishes[2].LinearAlbedo[0], 0.02f), "3A: the new finish lands at the drop point");
    Check(Near(FarAway[0], ConfiguratorPaintFinishes[0].LinearAlbedo[0], 0.02f), "3A: the old finish survives ahead of the front");
}

void ValidateVehicleCabin()
{
    GarageConfiguratorState Garage;
    Check(GarageConfiguratorState::PaintSwatchCount == 8u, "3A: the garage offers eight paint swatches");
    Check(Garage.AutoOrbit && !Garage.OrbitDragging, "3A: the garage auto-orbits until the driver grabs it");
    Check(Near(Garage.OrbitYawDegrees, 34.0f) && Near(Garage.OrbitDistanceMetres, 6.35f),
          "3A: the garage camera rests at its framing pose");

    const float StartYaw = Garage.OrbitYawDegrees;
    Garage.Advance(1.0f);
    Check(Near(Garage.OrbitYawDegrees, StartYaw + GarageConfiguratorState::DefaultAutoOrbitDegreesPerSecond, 0.01f),
          "3A: auto-orbit sweeps at 11.5 deg/s");
    Garage.OrbitDragging = true;
    const float GrabbedYaw = Garage.OrbitYawDegrees;
    Garage.Advance(1.0f);
    Check(Near(Garage.OrbitYawDegrees, GrabbedYaw), "3A: dragging suspends auto-orbit");
    Garage.OrbitDragging = false;

    Garage.OrbitBy(0.0f, -100.0f);
    Check(Near(Garage.OrbitPitchDegrees, 4.0f), "3A: pitch clamps to 4 deg looking down at the sill");
    Garage.OrbitBy(0.0f, +100.0f);
    Check(Near(Garage.OrbitPitchDegrees, 52.0f), "3A: pitch clamps to 52 deg looking down from above");
    Garage.OrbitBy(-720.0f, 0.0f);
    Check(Garage.OrbitYawDegrees >= 0.0f && Garage.OrbitYawDegrees < 360.0f, "3A: yaw stays wrapped into [0, 360)");

    VehicleTablet Tablet;
    Check(Tablet.IsEmpty() && Tablet.Shown == VehicleTablet::Content::Empty && Tablet.Powered,
          "3A: the tablet boots empty and powered");
    Tablet.Shown = VehicleTablet::Content::Garage;
    Check(!Tablet.IsEmpty(), "3A: opening the garage app leaves the empty state");

    Check(sizeof(SteeringWheel) > 0 && sizeof(VehicleCabin) > 0, "3A: cabin fittings remain layout-complete");
}

void ValidateSparkles()
{
    const auto Cloud = BuildScanSparkleCloud();
    Check(Cloud.size() == DefaultScanSparkleCount, "3A: 130 holographic sparkles surround the hero car");
    const auto Repeat = BuildScanSparkleCloud();
    bool Identical = Cloud.size() == Repeat.size();
    for (size_t Index = 0; Identical && Index < Cloud.size(); ++Index)
        Identical = Cloud[Index].BaseOffset == Repeat[Index].BaseOffset && Cloud[Index].Phase == Repeat[Index].Phase;
    Check(Identical, "3A: the sparkle cloud is deterministic across rebuilds");

    bool InBounds = true;
    for (const ScanSparkleSeed& Seed : Cloud)
    {
        InBounds &= std::fabs(Seed.BaseOffset[0]) <= 3.75f + 1e-3f;
        InBounds &= std::fabs(Seed.BaseOffset[1]) <= 2.10f + 1e-3f;
        InBounds &= Seed.BaseOffset[2] >= -0.78f && Seed.BaseOffset[2] <= 2.42f;
        InBounds &= Seed.Speed > 0.0f && Seed.Scale > 0.0f;
    }
    Check(InBounds, "3A: every sparkle sits inside the 7.5 x 4.2 x 3.2 m holo cloud");

    const EvaluatedSparkle At0 = EvaluateScanSparkle(Cloud[0], 0.0f);
    const EvaluatedSparkle At1 = EvaluateScanSparkle(Cloud[0], 1.7f);
    Check(At0.RadiusMetres > 0.0f && At0.Brightness >= 0.0f, "3A: sparkles evaluate to a positive disc");
    Check(At0.LocalPosition != At1.LocalPosition, "3A: sparkles drift over the scan clock");
}

void ValidateHmiRenderGraph()
{
    std::vector<HmiOccupantDescriptor> Occupants;
    for (uint32_t Index = 0; Index < 8; ++Index)
        Occupants.push_back({ Index, 0u });                 // static studio geometry
    Occupants.push_back({ 8u, 1u });                        // hero vehicle
    for (uint32_t Index = 0; Index < 6; ++Index)
        Occupants.push_back({ 9u + Index, 40u + Index });    // other players' vehicles

    const auto Mask = ResolveSingleVehicleExclusionMask(Occupants, 1u);
    Check(Mask.size() == Occupants.size(), "3A: the exclusion mask covers every occupant");
    Check(Mask[8] == 0u, "3A: the hero vehicle is never excluded");
    bool StaticsKept = true;
    for (uint32_t Index = 0; Index < 8; ++Index)
        StaticsKept &= Mask[Index] == 0u;
    Check(StaticsKept, "3A: studio geometry stays in the HMI preview");
    bool GuestsDropped = true;
    for (uint32_t Index = 9; Index < Mask.size(); ++Index)
        GuestsDropped &= Mask[Index] == 1u;
    Check(GuestsDropped, "3A: every guest vehicle is excluded from the HMI preview");

    const auto Frame = DeclareHmiPreviewFrame();
    Check(!Frame.Units.empty() && !Frame.Targets.empty(), "3A: the HMI frame declares units and targets");
    bool PreviewPresent = false;
    bool PreviewPersistent = false;
    for (const RenderTargetDeclaration& Target : Frame.Targets)
    {
        if (Target.Name == "HmiPreviewTexture")
        {
            PreviewPresent = true;
            PreviewPersistent = Target.Persistent;
            Check(Target.Width == HmiPreviewDefaultWidth && Target.Height == HmiPreviewDefaultHeight,
                  "3A: the HMI preview target is 1024x640");
        }
    }
    Check(PreviewPresent, "3A: the frame writes HmiPreviewTexture");
    Check(PreviewPersistent, "3A: HmiPreviewTexture is persistent (sampled by the tablet UI)");

    ResolvedSubmissionOrder Order;
    std::string Diagnostic;
    Check(ResolveSubmissionOrder(Frame.Units, Order, Diagnostic), "3A: the HMI frame graph is acyclic");
    Check(Order.Order.size() == Frame.Units.size(), "3A: every HMI unit is scheduled");

    const auto PositionOf = [&](const std::string& Name)
    {
        for (size_t Slot = 0; Slot < Order.Order.size(); ++Slot)
            if (Frame.Units[Order.Order[Slot]].Name == Name)
                return Slot;
        return Order.Order.size();
    };
    const size_t OverlayPosition = PositionOf("Hmi_VehicleScanOverlay");
    const size_t RecombinationPosition = PositionOf("Hmi_DisplayRecombination");
    const size_t PublishPosition = PositionOf("Hmi_TabletTexturePublish");
    const size_t RasterPosition = PositionOf("Hmi_SurfaceRaster");
    const size_t CloudPresentPosition = PositionOf("Hmi_CloudPresentRasterization");

    Check(OverlayPosition < Frame.Units.size(), "3A: the X-Ray Scan overlay is scheduled");
    Check(RasterPosition == 0u, "3A: the single-vehicle G-buffer raster opens the HMI frame");
    Check(CloudPresentPosition < RecombinationPosition,
          "3A: the sky/cloud environment resolves before the display recombination");
    Check(RecombinationPosition < OverlayPosition, "3A: the scan overlay composites after the recombination");
    Check(PublishPosition == Order.Order.size() - 1u,
          "3A: publishing HmiPreviewTexture to the tablet is the final unit");

    HmiPreviewFrameOptions WithoutOverlay;
    WithoutOverlay.IncludeScanOverlay = false;
    const auto Bare = DeclareHmiPreviewFrame(HmiPreviewDefaultWidth, HmiPreviewDefaultHeight, WithoutOverlay);
    Check(Bare.Units.size() < Frame.Units.size(), "3A: the scan overlay is an optional tail unit");
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                    TRACK 3A — SOFTWARE PRESENTATION (PORTABLE RASTER)
//--------------------------------------------------------------------------------------------------------------------------------------

struct PpmImage
{
    int    Width = 0;
    int    Height = 0;
    std::vector<uint8_t> Pixels;

    [[nodiscard]] bool Valid() const noexcept { return Width > 0 && Height > 0 && Pixels.size() == size_t(Width) * Height * 3; }
    [[nodiscard]] float LumaAt(int X, int Y) const noexcept
    {
        const size_t Base = (size_t(Y) * Width + X) * 3;
        return (Pixels[Base] + Pixels[Base + 1] + Pixels[Base + 2]) / (3.0f * 255.0f);
    }
    [[nodiscard]] float MeanLuma() const noexcept
    {
        double Total = 0.0;
        for (size_t Index = 0; Index < Pixels.size(); ++Index)
            Total += Pixels[Index];
        return float(Total / double(Pixels.size() * 255u));
    }
    [[nodiscard]] uint64_t Checksum() const noexcept
    {
        uint64_t Hash = 0xcbf29ce484222325ull;
        for (uint8_t Byte : Pixels)
        {
            Hash ^= Byte;
            Hash *= 0x100000001b3ull;
        }
        return Hash;
    }
};

[[nodiscard]] PpmImage LoadPpm(const std::string& Path)
{
    PpmImage Image;
    std::ifstream Stream(Path, std::ios::binary);
    if (!Stream)
        return Image;
    std::string Magic;
    Stream >> Magic;
    if (Magic != "P6")
        return Image;
    int MaxValue = 0;
    Stream >> Image.Width >> Image.Height >> MaxValue;
    Stream.get();
    if (Image.Width <= 0 || Image.Height <= 0 || MaxValue != 255)
        return Image;
    Image.Pixels.resize(size_t(Image.Width) * Image.Height * 3);
    Stream.read(reinterpret_cast<char*>(Image.Pixels.data()), std::streamsize(Image.Pixels.size()));
    if (!Stream)
        Image.Pixels.clear();
    return Image;
}

void ValidateSoftwarePresentation(const std::string& ArchiveDirectory)
{
    const FacetArchiveData Body = LoadFacetArchive(ArchiveDirectory + "/ControlVehicleBody.facet");
    const FacetArchiveData Tyre = LoadFacetArchive(ArchiveDirectory + "/ControlVehicleTyre.facet");
    const FacetArchiveData Rim = LoadFacetArchive(ArchiveDirectory + "/ControlVehicleRim.facet");
    if (!Body.IsValid())
    {
        Check(false, "3A: the vehicle facet archives load for the software presentation");
        return;
    }
    Check(Tyre.IsValid() && Rim.IsValid(), "3A: tyre and rim archives load for the software presentation");
    Check(Body.FacetCount > 1000u, "3A: the hero body archive carries its full facet set");

    // 14° body creases and 28° wheel creases, matching the dihedral wireframe thresholds.
    const DihedralEdgeSet BodyEdges = ExtractDihedralEdges(Body, 14.0f);
    const DihedralEdgeSet WheelEdges = ExtractDihedralEdges(Rim, 28.0f);
    Check(BodyEdges.ThresholdDegrees == 14.0f && WheelEdges.ThresholdDegrees == 28.0f,
          "3A: the dihedral extractor records its threshold");
    Check(BodyEdges.Count() > 400u, "3A: the body yields 400+ hard creases at 14 deg");
    Check(WheelEdges.Count() > 200u, "3A: the wheel yields 200+ hard creases at 28 deg");
    const DihedralEdgeSet StrictBody = ExtractDihedralEdges(Body, 60.0f);
    Check(StrictBody.Count() < BodyEdges.Count(), "3A: raising the threshold sheds creases");

    std::vector<PresentationVehicle> Fleet;
    PresentationVehicle Hero{};
    Hero.VehicleIdentifier = 1;
    Hero.Body = &Body;
    Hero.Tyre = &Tyre;
    Hero.Rim = &Rim;
    Hero.PositionMetres = { 0.0f, 0.0f, 0.53285f };
    Hero.Paint = ConfiguratorPaintFinishes[0].LinearAlbedo;
    Fleet.push_back(Hero);
    for (uint32_t Guest = 0; Guest < 4; ++Guest)
    {
        PresentationVehicle Other = Hero;
        Other.VehicleIdentifier = 40u + Guest;
        Other.PositionMetres = { 6.0f + float(Guest) * 4.0f, -5.0f, 0.53285f };
        Fleet.push_back(Other);
    }

    GarageConfiguratorState Garage;
    Garage.AutoOrbit = false;

    const std::string Scratch = "/tmp/frontier_phase3";
    const std::string SolidPath = Scratch + "_garage_solid.ppm";
    const std::string ScanPath = Scratch + "_garage_scan.ppm";
    const std::string RipplePath = Scratch + "_garage_ripple.ppm";

    Garage.Inspection.SetView(InspectionView::Solid);
    Check(RenderHmiGaragePpm(SolidPath, Fleet, 1u, Garage), "3A: the HMI garage renders a solid PPM");
    const PpmImage Solid = LoadPpm(SolidPath);
    Check(Solid.Valid(), "3A: the solid HMI PPM is a readable 24-bit frame");
    Check(Solid.Width == 1024 && Solid.Height == 640, "3A: the HMI garage frame is 1024x640");
    Check(Solid.MeanLuma() > 0.02f && Solid.MeanLuma() < 0.98f, "3A: the solid frame is exposed, not black or blown out");
    Check(Solid.LumaAt(2, 2) != Solid.LumaAt(Solid.Width / 2, Solid.Height / 2 + 200),
          "3A: the solid frame carries real shading variation");

    // The single-vehicle HMI preview must exclude every guest vehicle.
    std::vector<PresentationVehicle> HeroOnly;
    HeroOnly.push_back(Hero);
    const std::string HeroOnlyPath = Scratch + "_garage_hero_only.ppm";
    Garage.Inspection.SetView(InspectionView::Solid);
    Check(RenderHmiGaragePpm(HeroOnlyPath, HeroOnly, 1u, Garage), "3A: the hero-only HMI garage renders");
    const PpmImage HeroOnlyImage = LoadPpm(HeroOnlyPath);
    Check(HeroOnlyImage.Valid() && HeroOnlyImage.Checksum() == Solid.Checksum(),
          "3A: the HMI preview renders only the hero vehicle (guests excluded)");

    // X-Ray Scan: the same camera, mid-sweep, must change the picture and brighten the reveal.
    Garage.Inspection.SetView(InspectionView::Scan);
    Garage.Inspection.Advance(2.3f);
    Check(RenderHmiGaragePpm(ScanPath, Fleet, 1u, Garage), "3A: the HMI garage renders a mid-sweep X-Ray PPM");
    const PpmImage Scan = LoadPpm(ScanPath);
    Check(Scan.Valid(), "3A: the X-Ray HMI PPM is a readable frame");
    Check(Scan.Checksum() != Solid.Checksum(), "3A: the X-Ray scan visibly changes the HMI frame");
    Check(std::fabs(Scan.MeanLuma() - Solid.MeanLuma()) > 1e-4f,
          "3A: the X-Ray scan adds holographic energy to the frame");

    // Ghost and Wireframe are distinct presentations of the same pose.
    const std::string GhostPath = Scratch + "_garage_ghost.ppm";
    const std::string WirePath = Scratch + "_garage_wire.ppm";
    Garage.Inspection.SetView(InspectionView::Ghost);
    Check(RenderHmiGaragePpm(GhostPath, Fleet, 1u, Garage), "3A: the HMI garage renders a Ghost PPM");
    Garage.Inspection.SetView(InspectionView::Wireframe);
    Check(RenderHmiGaragePpm(WirePath, Fleet, 1u, Garage), "3A: the HMI garage renders a Wireframe PPM");
    const PpmImage Ghost = LoadPpm(GhostPath);
    const PpmImage Wire = LoadPpm(WirePath);
    Check(Ghost.Valid() && Wire.Valid(), "3A: Ghost and Wireframe PPMs are readable frames");
    Check(Ghost.Checksum() != Solid.Checksum() && Wire.Checksum() != Ghost.Checksum(),
          "3A: Ghost and Wireframe are distinct presentations");

    // 3D paint ripple: dropping a swatch on the body repaints from the raycast hit point.
    Garage.Inspection.SetView(InspectionView::Solid);
    Garage.SelectPaintSwatch(2);
    Check(Garage.PaintRipple.Active, "3A: dropping a swatch starts the 3D paint ripple");
    for (int Frame = 0; Frame < 8; ++Frame)
        Garage.Advance(0.10f);   // 0.8 s into the 1.35 s ripple: the front is across the body
    Check(Garage.PaintRipple.ResolveRadiusMetres() > 3.0f, "3A: the ripple front sweeps the hero body");
    Check(RenderHmiGaragePpm(RipplePath, Fleet, 1u, Garage), "3A: the HMI garage renders a rippling repaint PPM");
    const PpmImage Ripple = LoadPpm(RipplePath);
    Check(Ripple.Valid(), "3A: the ripple PPM is a readable frame");
    Check(Ripple.Checksum() != Solid.Checksum(), "3A: the paint ripple repaints the hero body");

    // Raycast body picking drives the ripple origin.
    const BodyRaycastHit Hit = PickVehicleBodyPoint(Body, { 8.0f, 0.0f, 0.80f }, { -1.0f, 0.0f, 0.0f });
    Check(Hit.Hit, "3A: a nose-on ray hits the hero body");
    Check(Hit.BodyPoint[0] > 0.0f, "3A: the raycast hit lands on the front half of the body");
    const BodyRaycastHit Miss = PickVehicleBodyPoint(Body, { 0.0f, 0.0f, 6.0f }, { 0.0f, 0.0f, 1.0f });
    Check(!Miss.Hit, "3A: a ray fired wide of the car misses");
    const BodyRaycastHit Tail = PickVehicleBodyPoint(Body, { -8.0f, 0.0f, 0.80f }, { 1.0f, 0.0f, 0.0f });
    Check(Tail.Hit && Tail.BodyPoint[0] < 0.0f, "3A: a tail-on ray hits the rear of the body");
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                       TRACK 3B — CLUSTERED LIGHTS AND PHOTOMETRY
//--------------------------------------------------------------------------------------------------------------------------------------

void ValidatePhotometry()
{
    std::string Diagnostic;
    const std::vector<uint8_t> Atlas = LoadPhotometricAtlas("EngineContent/Photometry", Diagnostic);

    Check(PhotometryBytes == 23584u, "3B: the photometric atlas pads to the 23584 bytes the record reserves");
    Check(PhotometryTexelCount == 23569u && PhotometryVectorCount * 16u == PhotometryBytes,
          "3B: the atlas is 7 profiles x 37 azimuths x 91 polar angles, one byte each");
    Check(Atlas.size() == PhotometryBytes, "3B: the seven shipped profiles pack into the atlas");
    Check(Atlas.size() == PhotometryBytes && Diagnostic.empty(), "3B: every shipped profile parses without a diagnostic");

    // 📝 Determinism: the same seven files must pack to the same bytes, or the renderer's beam shapes
    //    would drift between runs.
    const std::vector<uint8_t> Again = LoadPhotometricAtlas("EngineContent/Photometry", Diagnostic);
    Check(Atlas == Again, "3B: the photometric atlas packs deterministically");

    uint32_t Nonzero = 0u;
    for (uint8_t Byte : Atlas)
        if (Byte != 0u)
            ++Nonzero;
    Check(Nonzero > 8000u, "3B: every shipped profile contributes light across the atlas");

    // 📝 Each profile is scaled so that its strongest TEXEL is 255: the shader reads texels as
    //    "relative to the profile's peak", so the grid's maximum is the peak it can see.
    for (uint32_t Profile = 0u; Profile < PhotometryProfileCount; ++Profile)
    {
        uint32_t Strongest = 0u;
        for (uint32_t Texel = 0u; Texel < PhotometryTexelsPerProfile; ++Texel)
            Strongest = std::max<uint32_t>(Strongest, Atlas[Profile * PhotometryTexelsPerProfile + Texel]);
        Check(Strongest == 255u, Named("3B: every profile is peak-normalised over its own grid", Profile + 1u).c_str());
    }

    // 📝 The per-profile means and on-axis intensities the reference implementation produces, to the
    //    last bit of the packed byte. Only the real Area file (a manufacturer's area luminaire with an
    //    upward component) emits straight behind its optical axis; every beam does not.
    const std::array<double, 7> ExpectedMean = {{ 0.159572, 0.417281, 0.059680, 0.072334, 0.058290, 0.147942, 0.062702 }};
    const std::array<double, 7> ExpectedAxial = {{ 0.454902, 1.000000, 0.752941, 0.996078, 0.835294, 1.000000, 1.000000 }};
    const std::array<double, 7> ExpectedBehind = {{ 0.000000, 0.188235, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000 }};
    for (uint32_t Profile = 0u; Profile < PhotometryProfileCount; ++Profile)
    {
        double Sum = 0.0;
        for (uint32_t Texel = 0u; Texel < PhotometryTexelsPerProfile; ++Texel)
            Sum += double(Atlas[Profile * PhotometryTexelsPerProfile + Texel]) / 255.0;
        const double Mean = Sum / double(PhotometryTexelsPerProfile);
        Check(NearDouble(Mean, ExpectedMean[Profile], 5.0e-4),
              Named("3B: the photometric atlas reproduces the reference profile mean", Profile + 1u).c_str());

        const double Axial = SamplePhotometricAtlas(Atlas, Profile + 1u, 0.0, 0.0);
        Check(NearDouble(Axial, ExpectedAxial[Profile], 1.0e-3),
              Named("3B: the atlas reproduces the reference on-axis intensity", Profile + 1u).c_str());

        const double Behind = SamplePhotometricAtlas(Atlas, Profile + 1u, 0.0, 180.0);
        Check(NearDouble(Behind, ExpectedBehind[Profile], 1.0e-3),
              Named("3B: the atlas reproduces the reference intensity straight behind the axis", Profile + 1u).c_str());
    }

    // 📝 The parser itself, on an authored two-plane grid.
    const std::string Synthetic =
        "IESNA:LM-63-1995\n"
        "[TEST] synthetic two-plane grid\n"
        "TILT=NONE\n"
        "1 1000 1.0 3 2 1 2 0.3 0.3 0.2 1.0 0.0 12.0\n"
        "0 90 180\n"
        "0 180\n"
        "100 50 0\n"
        "200 100 0\n";
    std::string Error;
    const std::optional<IesPhotometricProfile> Parsed = ParseIes(Synthetic, Error);
    Check(Parsed.has_value(), "3B: a well-formed LM-63 Type C file parses");
    if (Parsed)
    {
        Check(Parsed->Vertical.size() == 3u && Parsed->Vertical.front() == 0.0 && Parsed->Vertical.back() == 180.0,
              "3B: the vertical grid runs from 0 to 180 degrees");
        Check(Parsed->Horizontal.size() == 2u && Parsed->Horizontal.front() == 0.0 && Parsed->Horizontal.back() == 180.0,
              "3B: the horizontal grid is read as the 0-180 plane pair");
        Check(NearDouble(Parsed->Peak, 200.0), "3B: the grid peak is the strongest candela value");
        Check(NearDouble(SampleIes(*Parsed, 0.0, 0.0), 0.5), "3B: SampleIes normalises by the grid peak");
        Check(NearDouble(SampleIes(*Parsed, 180.0, 0.0), 1.0), "3B: SampleIes reads the C180 plane");
        Check(NearDouble(SampleIes(*Parsed, 360.0, 0.0), 0.5), "3B: the azimuth wraps at 360 degrees");
        Check(NearDouble(SampleIes(*Parsed, 0.0, 45.0), 0.375), "3B: the polar axis interpolates linearly");
        Check(NearDouble(SampleIes(*Parsed, 90.0, 0.0), 0.75), "3B: a grid reaching 180 mirrors about C180");
        Check(SampleIes(*Parsed, 0.0, 180.0) == 0.0, "3B: the last measured polar angle is the last that emits");
        Check(SampleIes(*Parsed, 0.0, 180.1) == 0.0, "3B: beyond the last measured polar angle there is no light");
    }

    // 📝 A single plane is rotational: every azimuth reads the same curve.
    const std::string Rotational =
        "IESNA:LM-63-1995\nTILT=NONE\n"
        "1 1000 1.0 2 1 1 2 0 0 0 1 0 10\n"
        "0 90\n"
        "0\n"
        "300 0\n";
    const std::optional<IesPhotometricProfile> Round = ParseIes(Rotational, Error);
    Check(Round.has_value(), "3B: a single-plane (rotationally symmetric) file parses");
    if (Round)
        Check(NearDouble(SampleIes(*Round, 0.0, 0.0), 1.0) && NearDouble(SampleIes(*Round, 137.0, 0.0), 1.0),
              "3B: a single plane is rotationally symmetric");

    // 📝 Everything the reference parser refuses.
    Check(!ParseIes("TILT=INCLUDE\n1 2 3\n", Error).has_value(), "3B: an external tilt table is refused");
    Check(!ParseIes("[TEST] nothing\n1 2 3\n", Error).has_value(), "3B: a file without a TILT line is refused");
    Check(!ParseIes("TILT=NONE\n1 2\n", Error).has_value(), "3B: an incomplete photometric header is refused");
    Check(!ParseIes("TILT=NONE\n1 1 1 3 2 1 2 0 0 0 1 0 1\n0 90 180\n0 180\n0 0 0\n0 0 0\n", Error).has_value(),
          "3B: a grid with no light in it is refused");
    Check(!ParseIes("TILT=NONE\n1 1 1 3 2 2 2 0 0 0 1 0 1\n0 90 180\n0 180\n1 1 1\n1 1 1\n", Error).has_value(),
          "3B: a non-Type-C photometric file is refused");
}

void ValidateLightClusterSpace()
{
    LightClusterSpace Clusters;
    Check(Clusters.Bytes().size() == LightClusterBytes, "3B: the clustered-light record is one 60992-byte binding");
    Check(LightClusterBytes == 60992u, "3B: the whole structure stays inside the 64 KiB uniform binding limit");
    Check(LightClusterCellCount == 1024u, "3B: the light grid is 16 x 16 x 4 = 1024 cells");
    Check(!Clusters.IsPhotometric(), "3B: lights use their analytic beam until photometry is uploaded");
    Check(Clusters.QueryEmissiveClusterCount() == 0u, "3B: the emissive structure starts switched off");

    // 📝 Two lights: one on the pad at the origin, one absurdly far away.
    std::vector<float> Lights(size_t(MaxSceneLights) * LightRecordFloats, 0.0f);
    const auto Place = [&](uint32_t Index, std::array<float, 3> Centre, float Reach)
    {
        const size_t Base = size_t(Index) * LightRecordFloats;
        Lights[Base + 0u] = Centre[0];
        Lights[Base + 1u] = Centre[1];
        Lights[Base + 2u] = Centre[2];
        Lights[Base + 7u] = Reach;
        Lights[Base + 15u] = 1.0f;                     // Params.w: the active flag
    };
    Place(0u, { 0.0f, 0.0f, 5.0f }, 20.0f);
    Place(3u, { 1.0e5f, 1.0e5f, 1.0e5f }, 1.0f);
    Clusters.Update(Lights);

    const uint32_t Centre = Clusters.CellMask(4u, 8u, 0u);      // the cell that contains (0, 0, 5)
    Check((Centre & 1u) != 0u, "3B: a light is listed in the cell that contains it");
    Check((Centre & (1u << 3u)) == 0u, "3B: a light a hundred kilometres away is listed nowhere");
    Check(Clusters.CellMask(15u, 15u, 3u) == 0u, "3B: the far corner of the pad lists no light");

    // 📝 A receiver reads the mask of its own cell, and everything outside the grid.
    Check(Clusters.QueryCandidates({ 0.0f, 0.0f, 5.0f }) == Centre, "3B: a receiver reads its own cell's mask");
    Check(Clusters.QueryCandidates({ -1000.0f, 0.0f, 0.0f }) == 0xFFFFFFFFu,
          "3B: a point outside the grid takes every light, so none is ever skipped");
    Check(Clusters.CellMask(16u, 0u, 0u) == 0xFFFFFFFFu, "3B: an out-of-range cell index reads as all lights");

    // 📝 The cull is conservative: the host-side recomputation over the same spheres never finds a
    //    light the grid dropped.
    bool Conservative = true;
    for (uint32_t Sample = 0u; Sample < 512u; ++Sample)
    {
        const std::array<float, 3> Probe = { -60.0f + 216.0f * float(Sample % 16u) / 16.0f,
                                             -60.0f + 120.0f * float((Sample / 16u) % 16u) / 16.0f,
                                             -2.0f + 30.0f * float((Sample / 256u) % 4u) / 4.0f };
        const uint32_t Candidates = Clusters.QueryCandidates(Probe);
        const uint32_t Exact = Clusters.ResolveConservativeMask(Probe);
        if ((Candidates & Exact) != Exact)
            Conservative = false;
    }
    Check(Conservative, "3B: the clustered cull never drops a light that reaches a cell");

    // 📝 Shrinking the reach proves the cull is doing real work (and that the test above can fail).
    uint32_t Before = 0u;
    uint32_t After = 0u;
    for (uint32_t Cell = 0u; Cell < LightClusterCellCount; ++Cell)
        if ((Clusters.CellMask(Cell) & 1u) != 0u)
            ++Before;
    Clusters.ReachScale = 0.02f;
    Clusters.Update(Lights);
    for (uint32_t Cell = 0u; Cell < LightClusterCellCount; ++Cell)
        if ((Clusters.CellMask(Cell) & 1u) != 0u)
            ++After;
    Clusters.ReachScale = 1.0f;
    Clusters.Update(Lights);
    Check(Before > 1u && After < Before, "3B: shrinking a light's reach shrinks the cells that list it");

    // 📝 Photometry travels inside the same record; a wrong-sized upload is ignored.
    std::string Diagnostic;
    const std::vector<uint8_t> Atlas = LoadPhotometricAtlas("EngineContent/Photometry", Diagnostic);
    Clusters.AssignPhotometry(std::vector<uint8_t>(PhotometryBytes - 16u, 0u));
    Check(!Clusters.IsPhotometric(), "3B: a wrong-sized photometric atlas is refused");
    Clusters.AssignPhotometry(Atlas);
    Check(Clusters.IsPhotometric(), "3B: uploading the atlas turns the photometric beams on");
    Check(RecordFloat(Clusters.Bytes(), 3u) == 1.0f, "3B: Minimum.w flags the atlas to the shaders");
    uint32_t Matching = 0u;
    for (uint32_t Byte = 0u; Byte < PhotometryBytes; ++Byte)
        if (Clusters.Bytes()[LightClusterPhotometryOffset + Byte] == Atlas[Byte])
            ++Matching;
    Check(Matching == PhotometryBytes, "3B: the atlas lands byte-for-byte at its record offset");

    // 📝 The emissive structure shares the record; Extent.w is its switch.
    std::vector<uint8_t> ClusterTable(size_t(EmissiveCapacity::Clusters) * EmissiveClusterStride, 0u);
    std::vector<uint8_t> CellTable(size_t(LightClusterCellCount) * EmissiveCellStride, 0u);
    Clusters.AssignEmissive(ClusterTable, ClusterTable, CellTable, 7u);
    Check(Clusters.QueryEmissiveClusterCount() == 7u, "3B: Extent.w carries the emissive cluster-instance count");
    Check(RecordFloat(Clusters.Bytes(), 7u) == 7.0f, "3B: the cluster count is readable at Extent.w");

    Clusters.Reset();
    Check(Clusters.QueryEmissiveClusterCount() == 0u, "3B: Reset drops the whole emissive structure");
    Check(Clusters.IsPhotometric(), "3B: Reset keeps the photometric atlas");
    Check(RecordFloat(Clusters.Bytes(), 3u) == 1.0f, "3B: Reset keeps the photometric flag");
    Check(Clusters.CellMask(4u, 8u, 0u) == 0u, "3B: Reset clears every cell mask");
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                       TRACK 3B — EMISSIVE SURFACES AS LIGHTS
//--------------------------------------------------------------------------------------------------------------------------------------

void ValidateEmissivePartition()
{
    // 📝 The invariant the shader's cluster lookup depends on: every cluster is a contiguous run of at
    //    most eight triangles, and the runs together cover each triangle exactly once.
    for (uint32_t Count : { 1u, 7u, 8u, 9u, 40u, 155u })
    {
        std::vector<std::array<float, 3>> Centroids(Count);
        for (uint32_t Index = 0u; Index < Count; ++Index)
        {
            const double Angle = 2.39996323 * double(Index);        // golden angle: no degeneracies
            const double Radius = std::sqrt(double(Index) / double(Count));
            Centroids[Index] = { float(Radius * std::cos(Angle)), float(Radius * std::sin(Angle)), float(Index) * 0.01f };
        }

        const std::vector<std::vector<uint32_t>> Runs = PartitionEmissiveTriangles(Centroids, EmissiveClusterSize);
        bool Bounded = true;
        bool Covered = true;
        std::vector<uint32_t> Seen(Count, 0u);
        for (const std::vector<uint32_t>& Run : Runs)
        {
            if (Run.empty() || Run.size() > EmissiveClusterSize)
                Bounded = false;
            for (uint32_t Index : Run)
            {
                if (Index >= Count)
                    Bounded = false;
                else
                    ++Seen[Index];
            }
        }
        for (uint32_t Index = 0u; Index < Count; ++Index)
            if (Seen[Index] != 1u)
                Covered = false;
        Check(Bounded, Named("3B: every emissive cluster is a non-empty run of at most 8 triangles", Count).c_str());
        Check(Covered, Named("3B: the cluster runs cover every emissive triangle exactly once", Count).c_str());
    }
}

void ValidateEmissiveLightSpace()
{
    // 📝 A street-lamp head: nine emissive triangles (so one set needs two clusters), plus one family
    //    below the emissive floor that must not become a light at all.
    std::vector<std::array<float, 3>> Materials;
    Materials.push_back({ 40.0f, 34.0f, 26.0f });      // family 0: a warm lamp lens
    Materials.push_back({ 0.10f, 0.10f, 0.12f });      // family 1: dim trim, below the floor

    std::vector<EmissiveSetSource> Sets(1);
    for (uint32_t Index = 0u; Index < 9u; ++Index)
    {
        EmissiveTriangleSource Facet;
        const float Offset = float(Index) * 0.05f;
        Facet.A = { Offset, 0.0f, 0.0f };
        Facet.B = { Offset + 0.04f, 0.0f, 0.0f };
        Facet.C = { Offset, 0.04f, 0.0f };
        Facet.Family = 0u;
        Facet.BvhSlot = 1000u + Index;
        Sets[0].Facets.push_back(Facet);
    }
    EmissiveTriangleSource Dim;
    Dim.A = { 1.0f, 0.0f, 0.0f };
    Dim.B = { 1.04f, 0.0f, 0.0f };
    Dim.C = { 1.0f, 0.04f, 0.0f };
    Dim.Family = 1u;
    Dim.BvhSlot = 2000u;
    Sets[0].Facets.push_back(Dim);

    EmissiveLightSpace Emissive;
    const uint32_t Built = Emissive.Construct(Sets, Materials);
    Check(Built == 1u, "3B: a set with emissive triangles is represented");
    Check(Emissive.TriangleCount() == 9u, "3B: only families above the emissive floor become lights");
    Check(Emissive.Statistics().Triangles == 9u, "3B: the statistics count the triangles that became lights");

    // 📝 The triangle table: A, E1, E2, with the BVH index in A.w and the family in E2.w.
    const std::span<const uint8_t> Triangles = Emissive.TriangleBytes();
    bool Packing = true;
    std::vector<uint32_t> BvhIndices;
    for (uint32_t Index = 0u; Index < 9u; ++Index)
    {
        const uint32_t Bvh = RecordUint(Triangles, Index * 12u + 3u);
        const uint32_t Family = RecordUint(Triangles, Index * 12u + 11u);
        const float Area = RecordFloat(Triangles, Index * 12u + 7u);
        BvhIndices.push_back(Bvh);
        if (Family != 0u || !(Area > 0.0f))
            Packing = false;
    }
    Check(Packing, "3B: the emissive triangle table carries family, area and a positive area");
    Check(std::find(BvhIndices.begin(), BvhIndices.end(), 1000u) != BvhIndices.end(),
          "3B: the triangle table names each triangle by its BVH index");
    Check(std::find(BvhIndices.begin(), BvhIndices.end(), 2000u) == BvhIndices.end(),
          "3B: a triangle below the emissive floor is not in the table at all");

    // 📝 Two occupants of the same set, one static and one dynamic (the car's lamp).
    std::vector<EmissiveOccupantSource> Occupants(2);
    Occupants[0].FacetSet = 0u;
    Occupants[0].Roles = OccupantRole::Raster;
    Occupants[0].Transform[12] = -12.0f;
    Occupants[1].FacetSet = 0u;
    Occupants[1].Roles = OccupantRole::Raster | OccupantRole::Dynamic;
    Occupants[1].Transform[12] = 14.0f;

    const bool Changed = Emissive.Update(Occupants, Materials, 1.0f);
    Check(Changed, "3B: the first update populates the emissive structure");
    Check(Emissive.Count() == 4u, "3B: every cluster of every occupant becomes a cluster instance");
    Check(Emissive.Statistics().Instances == Emissive.Count(), "3B: the statistics report the instance count");
    Check(Emissive.Represents(0u) && Emissive.Represents(1u), "3B: both occupants are represented");
    Check((Occupants[0].Roles & OccupantRole::EmissiveCluster) != 0u
          && (Occupants[1].Roles & OccupantRole::EmissiveCluster) != 0u,
          "3B: the EmissiveCluster role is mirrored into the occupant records");

    // 📝 Idempotence: an unchanged scene must not rewrite the tables.
    Check(!Emissive.Update(Occupants, Materials, 1.0f), "3B: an unchanged scene does not rewrite the emissive tables");
    Check(Emissive.Update(Occupants, Materials, 2.0f), "3B: changing the emission scale rewrites the ranking");

    // 📝 The cluster table: world centre, radius, occupant, first triangle, count and power.
    const std::span<const uint8_t> ClusterBytes = Emissive.ClusterBytes();
    bool Clusters = true;
    for (uint32_t Slot = 0u; Slot < Emissive.Count(); ++Slot)
    {
        const float CentreX = RecordFloat(ClusterBytes, Slot * 8u + 0u);
        const float Radius = RecordFloat(ClusterBytes, Slot * 8u + 3u);
        const uint32_t Occupant = RecordUint(ClusterBytes, Slot * 8u + 4u);
        const uint32_t First = RecordUint(ClusterBytes, Slot * 8u + 5u);
        const uint32_t Count = RecordUint(ClusterBytes, Slot * 8u + 6u);
        const float Power = RecordFloat(ClusterBytes, Slot * 8u + 7u);
        if (Occupant > 1u || First + Count > Emissive.TriangleCount() || Count == 0u || Count > EmissiveClusterSize
            || !(Radius > 0.0f) || !(Power > 0.0f))
            Clusters = false;
        if (Occupant == 0u && !(CentreX < 0.0f))
            Clusters = false;                            // the static lamp sits at x = -12
        if (Occupant == 1u && !(CentreX > 0.0f))
            Clusters = false;                            // the car's lamp sits at x = +14
    }
    Check(Clusters, "3B: the cluster table carries world centre, radius, occupant, run and power");

    // 📝 The cell lists: the cell that contains a cluster lists it, strongest first, and empty slots
    //    read as 0x7FFF.
    const auto CellOfPoint = [](std::array<float, 3> Point)
    {
        std::array<uint32_t, 3> Cell = {};
        for (uint32_t Axis = 0u; Axis < 3u; ++Axis)
        {
            const float Local = (Point[Axis] - LightClusterBounds::Minimum[Axis]) / LightClusterBounds::Extent[Axis];
            Cell[Axis] = std::min<uint32_t>(uint32_t(std::floor(Local * float(LightClusterBounds::Resolution[Axis]))),
                                            LightClusterBounds::Resolution[Axis] - 1u);
        }
        return Cell[0] + LightClusterBounds::Resolution[0] * (Cell[1] + LightClusterBounds::Resolution[1] * Cell[2]);
    };

    const uint32_t StaticCell = CellOfPoint({ -12.0f, 0.0f, 2.0f });
    const std::array<uint16_t, EmissiveCapacity::Slots> StaticSlots = Emissive.CellSlots(StaticCell);
    Check(StaticSlots[0] != EmissiveEmptySlot, "3B: the cell that contains the static lamp lists a cluster");
    bool Ranked = true;
    for (uint32_t Slot = 1u; Slot < EmissiveCapacity::Slots; ++Slot)
        if (StaticSlots[Slot - 1u] == EmissiveEmptySlot && StaticSlots[Slot] != EmissiveEmptySlot)
            Ranked = false;
    Check(Ranked, "3B: a cell's cluster list is packed from the strongest slot down");

    const uint32_t EmptyCell = CellOfPoint({ 150.0f, 55.0f, 27.0f });
    const std::array<uint16_t, EmissiveCapacity::Slots> EmptySlots = Emissive.CellSlots(EmptyCell);
    Check(EmptySlots[0] == EmissiveEmptySlot, "3B: a cell no cluster reaches lists nothing");
    Check(Emissive.CellSlots(LightClusterCellCount)[0] == EmissiveEmptySlot, "3B: a cell index out of range is empty");

    // 📝 Moving an occupant moves its cluster and rewrites the lists it touches.
    const uint32_t PriorCount = Emissive.Count();
    Occupants[1].Transform[12] = 40.0f;
    Check(Emissive.Update(Occupants, Materials, 1.0f), "3B: moving an occupant rewrites the emissive lists");
    Check(Emissive.Count() == PriorCount, "3B: moving an occupant does not change the instance count");
    const uint32_t MovedCell = CellOfPoint({ 40.0f, 0.0f, 2.0f });
    Check(Emissive.CellSlots(MovedCell)[0] != EmissiveEmptySlot, "3B: the moved cluster is listed in its new cell");

    // 📝 An inactive occupant, one with a material override, and one that does not raster are all left out.
    std::vector<EmissiveOccupantSource> Mixed(3);
    Mixed[0].FacetSet = 0u;
    Mixed[0].Roles = OccupantRole::Raster;
    Mixed[1].FacetSet = 0u;
    Mixed[1].Roles = OccupantRole::Raster;
    Mixed[1].Inactive = true;
    Mixed[2].FacetSet = 0u;
    Mixed[2].Roles = OccupantRole::Raster | OccupantRole::Dynamic;
    Mixed[2].MaterialOverride = 0;
    Emissive.Update(Mixed, Materials, 1.0f);
    Check(Emissive.Count() > 0u && Emissive.Represents(0u) && !Emissive.Represents(1u) && !Emissive.Represents(2u),
          "3B: inactive, overridden and non-rastering occupants are left out");
    Check((Mixed[1].Roles & OccupantRole::EmissiveCluster) == 0u,
          "3B: the EmissiveCluster role is cleared on an occupant that left the table");
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                            TRACK 3B — NEAREST-DEPTH PYRAMID
//--------------------------------------------------------------------------------------------------------------------------------------

void ValidateNearestDepthSpace()
{
    NearestDepthSpace Space;
    Space.Configure(1600u, 900u);
    Check(Space.LevelCount() == 11u, "3B: a 1600x900 screen builds an 11-level pyramid");
    Check(Space.LevelExtent(0u)[0] == 1600u && Space.LevelExtent(0u)[1] == 900u, "3B: level 0 is the screen itself");
    Check(Space.LevelExtent(1u)[0] == 800u && Space.LevelExtent(1u)[1] == 450u, "3B: level 1 halves the screen");
    Check(Space.LevelExtent(10u)[0] == 1u && Space.LevelExtent(10u)[1] == 1u, "3B: the deepest level is a single texel");
    Check(Space.TransferDispatch()[0] == 200u && Space.TransferDispatch()[1] == 113u,
          "3B: the level-0 transfer dispatches one 8x8 workgroup per tile");
    Check(Space.ReductionDispatch(1u)[0] == 100u && Space.ReductionDispatch(1u)[1] == 57u,
          "3B: each reduction dispatches over its target level");
    Check(Space.Parity() == 0u, "3B: the pyramid starts in slot 0");
    Space.AdvanceFrame();
    Check(Space.Parity() == 1u, "3B: the pyramid alternates slots so the previous frame survives");
    Space.AdvanceFrame();
    Check(Space.Parity() == 0u, "3B: the pyramid returns to slot 0 after two frames");

    // 📝 The host mirror of the reduction chain, on a synthetic reversed-depth buffer. Reversed depth
    //    stores larger values nearer the eye, so "nearest" is a maximum and the clear value 0 never wins.
    for (auto Size : { std::array<uint32_t, 2>{ 64u, 64u }, std::array<uint32_t, 2>{ 97u, 53u }, std::array<uint32_t, 2>{ 1600u, 900u } })
    {
        const uint32_t Width = Size[0];
        const uint32_t Height = Size[1];
        std::vector<float> Depth(size_t(Width) * Height, 0.0f);
        for (uint32_t Y = 0u; Y < Height; ++Y)
            for (uint32_t X = 0u; X < Width; ++X)
                Depth[size_t(Y) * Width + X] = 0.10f + 0.80f * (float((X * 7u + Y * 13u) % 101u) / 100.0f);

        const std::vector<std::vector<float>> Pyramid = NearestDepthSpace::Reduce(Depth.data(), Width, Height);
        const std::string Tag = " (" + std::to_string(Width) + "x" + std::to_string(Height) + ")";

        Check(!Pyramid.empty() && Pyramid[0] == Depth, ("3B: level 0 of the pyramid is the depth itself" + Tag).c_str());
        Check(NearestDepthSpace::VerifyBounds(Pyramid, Width, Height),
              ("3B: every level-L texel bounds its whole 2^L tile from in front" + Tag).c_str());

        float Global = 0.0f;
        for (float Value : Depth)
            Global = std::max(Global, Value);
        Check(Near(Pyramid.back()[0], Global, 1e-6f),
              ("3B: the pyramid's single top texel is the nearest depth on the screen" + Tag).c_str());

        // 📝 A tile whose only surface is one pixel must still report it: a cleared (zero) depth never
        //    wins a maximum, so no pixel is left unbounded.
        std::vector<float> Sparse(size_t(Width) * Height, 0.0f);
        Sparse[size_t(Height) * Width - 1u] = 0.9f;
        const std::vector<std::vector<float>> SparsePyramid = NearestDepthSpace::Reduce(Sparse.data(), Width, Height);
        Check(Near(SparsePyramid.back()[0], 0.9f, 1e-6f),
              ("3B: a single rastered pixel is never lost to the clear value" + Tag).c_str());
        Check(NearestDepthSpace::VerifyBounds(SparsePyramid, Width, Height),
              ("3B: the bound holds for a sparse depth buffer" + Tag).c_str());

        // 📝 A truncated pyramid, or one whose level 0 is the wrong size, is rejected.
        Check(!NearestDepthSpace::VerifyBounds(std::vector<std::vector<float>>(Pyramid.begin(), Pyramid.end() - 1u), Width, Height),
              ("3B: a pyramid missing a level is rejected" + Tag).c_str());
    }

    Check(NearestDepthSpace::Reduce(nullptr, 64u, 64u).empty(), "3B: the reduction refuses a null depth buffer");
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                        TRACK 3B — SMOKE-AND-FIRE PLUME LATTICES
//--------------------------------------------------------------------------------------------------------------------------------------

void ValidatePlumeSpace()
{
    PlumeSpace Plume(PlumeGrades[0]);          // Low: 64 x 64 x 20 fine cells at 40 cm
    Check(Plume.ThermoExtent()[0] == 64u && Plume.ThermoExtent()[1] == 64u && Plume.ThermoExtent()[2] == 20u,
          "3B: the plume grade sets the fine (thermo) lattice extent");
    Check(Plume.VelocityExtent()[0] == 32u && Plume.VelocityExtent()[1] == 32u && Plume.VelocityExtent()[2] == 10u,
          "3B: the velocity lattice is half the thermo lattice on every axis");
    Check(Plume.CoarseDispatch()[0] == 8u && Plume.CoarseDispatch()[2] == 3u, "3B: the coarse dispatch is one 4^3 group per tile");
    Check(Plume.FineDispatch()[0] == 16u && Plume.FineDispatch()[1] == 16u && Plume.FineDispatch()[2] == 5u,
          "3B: the fine dispatch is one 4^3 group per tile");
    Check(Plume.MarchDispatch({ 1600u, 900u })[0] == 200u && Plume.MarchDispatch({ 1600u, 900u })[1] == 113u
          && Plume.MarchDispatch({ 1600u, 900u })[2] == 1u, "3B: the march dispatches one 8x8 group per trace tile");
    Check(!Plume.IsResident(), "3B: no plume is resident before a source appears");
    Check(Plume.SourceBytes().size() == size_t(PlumeSourceCapacity) * PlumeSourceStride * sizeof(float),
          "3B: the source table holds 24 sources of 12 floats");

    // 📝 A burning tyre: one source, 0.4 m across, injecting smoke, heat, flame and soot.
    PlumeSettings Settings;
    std::vector<float> Sources(PlumeSourceStride, 0.0f);
    Sources[0] = -1.20f; Sources[1] = 0.80f; Sources[2] = 0.25f; Sources[3] = 0.40f;      // position and radius
    Sources[4] = 2.0f; Sources[5] = 0.0f; Sources[6] = 1.5f;                              // injected momentum
    Sources[8] = 6.0f; Sources[9] = 900.0f; Sources[10] = 1.2f; Sources[11] = 3.0f;       // smoke, heat, flame, soot per second

    Plume.Schedule(0.5f, 1.0f / 60.0f, { 0.0f, 0.0f, 1.0f }, Sources, 1u, Settings, { 0.3f, 0.4f, 0.86f });
    Check(Plume.IsResident(), "3B: a source makes the plume resident");
    Check(Plume.IsStepPending(), "3B: the frame a source appears runs a step");

    const std::span<const uint8_t> Record = Plume.RecordBytes();
    Check(Record.size() == PlumeRecordBytes, "3B: the plume step record is 256 bytes (sixteen vec4)");
    Check(RecordUint(Record, 8u) == 32u && RecordUint(Record, 9u) == 32u && RecordUint(Record, 10u) == 10u,
          "3B: the record carries the coarse lattice extent");
    Check(RecordUint(Record, 12u) == 64u && RecordUint(Record, 13u) == 64u && RecordUint(Record, 14u) == 20u,
          "3B: the record carries the fine lattice extent");
    Check(Near(RecordFloat(Record, 16u), 0.40f) && Near(RecordFloat(Record, 17u), 0.80f),
          "3B: the record carries the fine and coarse cell edges");
    Check(Near(RecordFloat(Record, 18u), PlumeFloorHeight), "3B: the window's floor sits under the course");
    Check(Near(RecordFloat(Record, 19u), 1.0f / 30.0f, 1e-5f),
          "3B: a long frame is one step clamped to 1/30 s, not a spiral of short ones");
    Check(RecordInt(Record, 7u) == 1, "3B: the record carries the source count for the transport");
    Check(Near(RecordFloat(Record, 35u), 1.0f / 60.0f, 1e-4f),
          "3B: SourceMinimum.w carries the frame duration for a moving solid's point velocity");
    Check(Near(RecordFloat(Record, 44u), 0.3f) && Near(RecordFloat(Record, 46u), 0.86f),
          "3B: the record carries the sun direction for the plume's shadowing");
    Check(RecordFloat(Record, 43u) == float(PlumeGrades[0].MarchSamples),
          "3B: the march sample ceiling comes from the grade");
    Check(RecordUint(Record, 15u) == 0u, "3B: the lattice inspection view is off by default");

    // 📝 The window: centred on the focus when the plume appears, in coarse cells.
    Check(Plume.Origin()[0] == -16 && Plume.Origin()[1] == -16, "3B: the window opens centred on the focus");
    Check(RecordInt(Record, 0u) == -16 && RecordInt(Record, 1u) == -16 && RecordInt(Record, 2u) == 0,
          "3B: the record carries the window's coarse-cell origin");
    Check(RecordInt(Record, 4u) == PlumeRestartOrigin, "3B: a fresh plume's prior window lies nowhere, so nothing backtraces");

    Plume.CommitStep();
    Check(Plume.State() == 1u && Plume.StepsTaken() == 1u, "3B: committing a step flips the lattice parity");
    Check(!Plume.IsStepPending(), "3B: the step is no longer pending once committed");

    Plume.Schedule(1.0f / 60.0f, 1.0f / 60.0f, { 0.0f, 0.0f, 1.0f }, Sources, 1u, Settings);
    Check(Plume.IsStepPending(), "3B: a frame at or past 1/60 s steps the plume");
    Check(RecordInt(Record, 4u) == -16, "3B: after the first step the prior window is the one just written");

    // 📝 Below 1/60 s no step runs: a 120 Hz display steps every other frame.
    Plume.CommitStep();
    Plume.Schedule(1.0f / 240.0f, 1.0f / 240.0f, { 0.0f, 0.0f, 1.0f }, Sources, 1u, Settings);
    Check(!Plume.IsStepPending(), "3B: a frame shorter than 1/60 s accumulates instead of stepping");
    Plume.Schedule(1.0f / 240.0f, 1.0f / 240.0f, { 0.0f, 0.0f, 1.0f }, Sources, 1u, Settings);
    Check(!Plume.IsStepPending(), "3B: a second short frame still has not reached a step");
    Plume.Schedule(1.0f / 240.0f, 1.0f / 240.0f, { 0.0f, 0.0f, 1.0f }, Sources, 1u, Settings);
    Check(!Plume.IsStepPending(), "3B: three 240 Hz frames (1/80 s) have still not reached a step");
    Plume.Schedule(1.0f / 240.0f, 1.0f / 240.0f, { 0.0f, 0.0f, 1.0f }, Sources, 1u, Settings);
    Check(Plume.IsStepPending(), "3B: four 240 Hz frames accumulate into one 1/60 s step");

    // 📝 The window follows the car: it only relabels texels, never copies them.
    Plume.CommitStep();
    Plume.Schedule(1.0f / 60.0f, 1.0f / 60.0f, { 40.0f, 24.0f, 1.0f }, Sources, 1u, Settings);
    const std::array<int32_t, 2> Moved = Plume.Origin();
    Check(Moved[0] > -16 && Moved[1] > -16, "3B: the toroidal window follows the car without a copy");
    Check(RecordInt(Record, 0u) == Moved[0] && RecordInt(Record, 1u) == Moved[1],
          "3B: the record carries the window the transport writes in");

    // 📝 Quiet: once the sources stop the plume fades, and after PlumeFadeCeiling nothing is recorded.
    Plume.CommitStep();
    Plume.Schedule(1.0f, 1.0f / 60.0f, { 40.0f, 24.0f, 1.0f }, std::span<const float>{}, 0u, Settings);
    Check(Plume.IsResident(), "3B: the plume stays resident while it fades");
    Check(Plume.IsStepPending(), "3B: a fading plume keeps stepping: the step's retention carries the fade out");
    Check(RecordFloat(Record, 24u) < 1.0f && RecordFloat(Record, 24u) > 0.0f,
          "3B: the fade drives the smoke retention below one once the sources stop");
    Plume.Schedule(PlumeFadeCeiling + 1.0f, 1.0f / 60.0f, { 40.0f, 24.0f, 1.0f }, std::span<const float>{}, 0u, Settings);
    Check(!Plume.IsResident(), "3B: a plume quiet for longer than the fade ceiling stops costing anything at all");
    Check(!Plume.IsStepPending(), "3B: a plume that is no longer resident records no pass");

    // 📝 Clear() forgets the plume at once: the next source restarts a clean lattice.
    Plume.Schedule(0.5f, 1.0f / 60.0f, { 0.0f, 0.0f, 1.0f }, Sources, 1u, Settings);
    Plume.CommitStep();
    Plume.Clear();
    Check(!Plume.IsResident() && !Plume.IsStepPending(), "3B: Clear drops the plume at once");
    Check(Plume.PriorOrigin()[0] == PlumeRestartOrigin, "3B: a cleared plume restarts from a clean lattice");
    Plume.Schedule(0.5f, 1.0f / 60.0f, { 0.0f, 0.0f, 1.0f }, Sources, 1u, Settings);
    Check(Plume.IsResident() && Plume.IsStepPending(), "3B: the next source after a clear restarts the plume");

    // 📝 The slider.
    Plume.Schedule(0.5f, 1.0f / 60.0f, { 0.0f, 0.0f, 1.0f }, Sources, 1u, Settings);
    Check(Plume.IsResident(), "3B: a source revives the plume");
    Plume.CommitStep();
    Settings.PlumeEnabled = false;
    Plume.Schedule(0.5f, 1.0f / 60.0f, { 0.0f, 0.0f, 1.0f }, Sources, 1u, Settings);
    Check(!Plume.IsResident(), "3B: switching the plume off drops it at once");

    // 📝 A grade change reallocates the lattices without touching the programs.
    Settings.PlumeEnabled = true;
    Plume.ReconstructGrade(PlumeGrades[2]);
    Check(Plume.ThermoExtent()[0] == 128u && Plume.ThermoExtent()[2] == 32u, "3B: a grade change resizes the fine lattice");
    Check(Plume.VelocityExtent()[0] == 64u && Plume.VelocityExtent()[2] == 16u, "3B: a grade change resizes the coarse lattice");
    Check(!Plume.IsResident(), "3B: a grade change starts from a clean plume");
    Plume.Schedule(0.5f, 1.0f / 60.0f, { 0.0f, 0.0f, 1.0f }, Sources, 1u, Settings);
    Check(RecordUint(Plume.RecordBytes(), 12u) == 128u, "3B: the record follows the new grade's extent");
    Check(!Plume.QueryDescription().empty() && Plume.QueryDescription().find("128x128x32") != std::string::npos,
          "3B: the editor readout describes the resident lattices");

    // 📝 Fire lights: the brightest four of six, written every frame for the direct-radiance pass.
    std::vector<PlumeFireLight> Lights(6);
    for (uint32_t Index = 0u; Index < 6u; ++Index)
    {
        Lights[Index].Position = { float(Index), 0.0f, 0.5f };
        Lights[Index].Radius = 0.25f;
        const float Brightness = 10.0f + float(Index);
        Lights[Index].Radiance = { Brightness, 0.4f * Brightness, 0.1f * Brightness };
    }
    Plume.AssignFireLights(Lights);
    Check(Plume.FireLightBytes().size() == PlumeFireLightBytes, "3B: the fire-light record is 144 bytes");
    Check(Plume.QueryFireLightCount() == PlumeFireLightLimit, "3B: the fire-light record keeps at most four lights");
    Check(RecordUint(Plume.FireLightBytes(), 0u) == PlumeFireLightLimit, "3B: the record's first word is the light count");
    Check(Near(RecordFloat(Plume.FireLightBytes(), 4u), 0.0f) && Near(RecordFloat(Plume.FireLightBytes(), 7u), 0.25f),
          "3B: the first fire light's position and radius land in the record");
    Plume.AssignFireLights(std::span<const PlumeFireLight>{});
    Check(Plume.QueryFireLightCount() == 0u, "3B: no fire lights at all is a valid record");

    // 📝 The tiling detail volume: periodic in every axis, so a repeat sampler never shows a seam.
    const std::vector<uint8_t> Detail = PlumeSpace::ConstructDetailVolume();
    Check(Detail.size() == size_t(PlumeDetailExtent) * PlumeDetailExtent * PlumeDetailExtent * 4u,
          "3B: the detail volume is 32^3 RGBA texels");
    // 📝 A tiling volume has no seam: the jump from the last texel of a row back to its first is just
    //    another adjacent-texel step, never an outlier against the worst step inside the row.
    int WorstInterior = 0;
    int WorstWrap = 0;
    for (uint32_t Z = 0u; Z < PlumeDetailExtent; ++Z)
        for (uint32_t Y = 0u; Y < PlumeDetailExtent; ++Y)
        {
            const size_t Row = (size_t(Z) * PlumeDetailExtent + Y) * PlumeDetailExtent * 4u;
            for (uint32_t X = 0u; X + 1u < PlumeDetailExtent; ++X)
                for (uint32_t Channel = 0u; Channel < 4u; ++Channel)
                    WorstInterior = std::max(WorstInterior,
                                             std::abs(int(Detail[Row + X * 4u + Channel]) - int(Detail[Row + (X + 1u) * 4u + Channel])));
            for (uint32_t Channel = 0u; Channel < 4u; ++Channel)
                WorstWrap = std::max(WorstWrap,
                                     std::abs(int(Detail[Row + Channel]) - int(Detail[Row + (PlumeDetailExtent - 1u) * 4u + Channel])));
        }
    Check(WorstInterior > 0, "3B: the detail volume carries structure at the texel scale");
    Check(WorstWrap <= WorstInterior, "3B: the detail volume tiles without a seam across X");
    Check(Detail[0] != Detail[1] || Detail[4] != Detail[5], "3B: the detail volume carries real structure");
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                       TRACK 3C — TYRE PLUME SOURCES AND FIRE LIGHT
//--------------------------------------------------------------------------------------------------------------------------------------

void ValidatePlumeSourceProjection()
{
    // 📝 The turning operators: a quarter turn about +Z sends +X to +Y.
    const Rotor Quarter = AxisRotor({ 0.0f, 0.0f, 1.0f }, 1.5707963267948966f);
    const Vec3 Turned = RotateByRotor(Quarter, { 1.0f, 0.0f, 0.0f });
    Check(Near(Turned.X, 0.0f, 1e-5f) && Near(Turned.Y, 1.0f, 1e-5f) && Near(Turned.Z, 0.0f, 1e-5f),
          "3C: a quarter turn about the upright sends the forward axis to the left axis");
    const Vec3 Unturned = RotateByRotor(Rotor{}, { 0.3f, -0.7f, 1.1f });
    Check(Near(Unturned.X, 0.3f) && Near(Unturned.Y, -0.7f) && Near(Unturned.Z, 1.1f),
          "3C: the identity rotor leaves a vector alone");
    const Rotor Twice = ComposeRotor(Quarter, Quarter);
    const Vec3 Halved = RotateByRotor(Twice, { 1.0f, 0.0f, 0.0f });
    Check(Near(Halved.X, -1.0f, 1e-5f) && Near(Halved.Y, 0.0f, 1e-5f),
          "3C: composing two quarter turns is a half turn");

    // 📝 The onset hysteresis: entry needs power AND slip speed sustained; release has grace; losing
    //    contact cuts the source at once.
    PlumeSourceProjection Projection;
    Check(!Projection.AdvanceOnset(0u, 5000.0f, 5.0f, true, 0.01f),
          "3C: a single short frame above the onset power does not start a tyre smoking");
    Check(Projection.AdvanceOnset(0u, 5000.0f, 5.0f, true, 0.06f),
          "3C: the onset power held for the onset time starts a tyre smoking");
    Check(Projection.AdvanceOnset(0u, 150.0f, 5.0f, true, 0.02f),
          "3C: power above the release ratio but below the onset holds a smoking tyre smoking");
    Check(Projection.AdvanceOnset(0u, 10.0f, 5.0f, true, 0.10f) == false,
          "3C: power below the release ratio for the release time stops the tyre smoking");
    Check(!Projection.AdvanceOnset(1u, 5000.0f, 5.0f, true, 0.06f) == false,
          "3C: each wheel keeps its own onset");
    Check(Projection.AdvanceOnset(1u, 5000.0f, 5.0f, true, 0.06f), "3C: a second wheel can smoke on its own");
    Check(!Projection.AdvanceOnset(1u, 5000.0f, 5.0f, false, 0.06f),
          "3C: losing contact cuts the source at once, with no grace");
    Check(!Projection.AdvanceOnset(0u, 5000.0f, 0.1f, true, 0.06f),
          "3C: power without slip speed never starts a tyre smoking");

    // 📝 A four-wheel burnout: every wheel past the onset, hot enough to vapourise.
    Projection.Reset();
    PlumeTelemetry Telemetry;
    Telemetry.Position = { -1.2f, 0.8f, 0.0f };
    Telemetry.Velocity = { 0.0f, 0.0f, 0.0f };
    for (uint32_t Wheel = 0u; Wheel < 4u; ++Wheel)
    {
        PlumeWheelReading Reading;
        Reading.ContactPoint = { -1.2f + 0.3f * float(Wheel % 2u), 0.7f - 0.9f * float(Wheel / 2u), 0.0f };
        Reading.HubPosition = Reading.ContactPoint + Vec3{ 0.0f, 0.0f, 0.35f };
        Reading.ContactVelocity = { 0.0f, 0.0f, 0.0f };
        Reading.Longitudinal = { 1.0f, 0.0f, 0.0f };
        Reading.SlipSpeed = 18.0f;
        Reading.SlidingPower = 9000.0f;
        Reading.SurfaceSpeed = 18.0f;
        Reading.Radius = 0.35f;
        Reading.InContact = true;
        Reading.Thermal.SurfaceTemperature = 420.0f;              // well past the warm base
        Telemetry.Wheels.push_back(Reading);
    }

    PlumeProjectionOptions Options;
    const uint32_t Count = Projection.Project(Telemetry, 0.10f, Options);
    Check(Count == 8u, "3C: a four-wheel burnout sheds a patch source and an arch source per tyre");
    Check(Projection.ActiveTyres() == 4u, "3C: all four tyres are smoking");
    Check(Projection.Records().size() == size_t(Count) * PlumeSourceStride, "3C: the source table is 12 floats per source");

    const PlumeSourceEntry Patch = Projection.Source(0u);
    Check(Near(Patch.Radius, SmokeSpecification::PatchRadius * 1.4f), "3C: the patch source is 1.4 patch radii across");
    Check(Near(Patch.Position.Z, 0.04f, 1e-5f), "3C: the patch source sits 4 cm above the contact point");
    Check(Patch.Smoke > 0.0f && Patch.Heat > 0.0f && Patch.Flame == 0.0f && Patch.Soot == 0.0f,
          "3C: a sliding tyre sheds smoke and heat, no flame and no soot");
    Check(Near(Patch.Velocity.X, -SmokeFlingShare * 18.0f, 1e-4f),
          "3C: the patch smoke is thrown rearward along the tread with a share of the surface slip");
    Check(Near(Projection.Source(1u).Smoke, SmokeArchShare * Patch.Smoke, 1e-3f),
          "3C: the wheel arch sheds a third of the patch's smoke");

    // 📝 Smoke follows the tread's heat: a cold tyre barely smokes.
    PlumeSourceProjection Cold = Projection;
    PlumeTelemetry ColdTelemetry = Telemetry;
    for (PlumeWheelReading& Wheel : ColdTelemetry.Wheels)
        Wheel.Thermal.SurfaceTemperature = 293.15f;
    Cold.Project(ColdTelemetry, 0.10f, Options);
    // 📝 The Warm factor: 0.35 at the floor, 1.0 once the tread vapourises, so a tread at 420 K sheds
    //    about 1.85 times what a cold one does.
    Check(Near(Cold.Source(0u).Smoke, SmokeSpecification::DensityRate * (0.35f) * 1.0f * 0.0f
                                      + SmokeSpecification::DensityRate * 0.5f * 0.35f, 1e-3f)
          || Cold.Source(0u).Smoke < Patch.Smoke * 0.6f, "3C: a cold tread smokes far less than a hot one");
    Check(Near(Patch.Smoke / Cold.Source(0u).Smoke, (0.35f + 0.65f * (420.0f - SmokeWarmBase) / SmokeWarmSpan) / 0.35f, 1e-3f),
          "3C: the smoke a tread sheds follows its surface temperature");

    // 📝 Burning rubber: two flame seats per tyre, black soot instead of white rubber smoke, a fire
    //    light and an ember emitter.
    PlumeTelemetry BurningTelemetry = Telemetry;
    for (PlumeWheelReading& Wheel : BurningTelemetry.Wheels)
    {
        Wheel.Thermal.Ignited = true;
        Wheel.Thermal.CombustionPower = 42000.0f;
    }
    PlumeSourceProjection Fire;
    const uint32_t FireCount = Fire.Project(BurningTelemetry, 0.10f, Options);
    Check(FireCount == 16u, "3C: four burning tyres shed a patch, an arch and two flame seats each");
    Check(Fire.Lights().size() == 4u, "3C: every burning tyre publishes one fire light");
    Check(Fire.Embers().size() == 4u, "3C: every burning tyre publishes one ember emitter");
    Check(Near(Fire.Lights()[0].Radiance.Y, SmokeFireColour[1] * Fire.Lights()[0].Radiance.X, 1e-3f),
          "3C: the fire light is the colour of a sooty rubber flame");

    bool Seats = true;
    uint32_t SootSources = 0u;
    for (uint32_t Index = 0u; Index < FireCount; ++Index)
    {
        const PlumeSourceEntry Entry = Fire.Source(Index);
        if (Entry.Soot > 0.0f)
        {
            ++SootSources;
            if (Entry.Flame <= 0.0f)
                Seats = false;
        }
    }
    Check(SootSources == 8u && Seats, "3C: the eight flame seats shed soot and incandescence");

    // 📝 A shredded tyre has no rubber left to vapourise: the telemetry zeroes its sliding power, so
    //    it never passes the onset.
    PlumeTelemetry BareTelemetry = Telemetry;
    for (PlumeWheelReading& Wheel : BareTelemetry.Wheels)
        Wheel.SlidingPower = 0.0f;
    PlumeSourceProjection Bare;
    Check(Bare.Project(BareTelemetry, 0.10f, Options) == 0u && Bare.ActiveTyres() == 0u,
          "3C: a shredded tyre sheds nothing");

    // 📝 Afterfire pops: the window opens, a tip fires, and the pop dies out.
    PlumeTelemetry ExhaustTelemetry = Telemetry;
    ExhaustTelemetry.Wheels.clear();
    ExhaustTelemetry.AfterfireSeconds = 2.0f;
    ExhaustTelemetry.Exhausts.push_back(PlumeExhaustReading{ { -2.0f, 0.0f, 0.4f }, { -1.0f, 0.0f, 0.0f } });
    PlumeProjectionOptions Popping;
    Popping.Afterfire = true;
    PlumeSourceProjection Exhaust;
    uint32_t Onsets = 0u;
    uint32_t Sounding = 0u;
    bool WasPopping = false;
    for (uint32_t Frame = 0u; Frame < 400u; ++Frame)
    {
        Exhaust.Project(ExhaustTelemetry, 1.0f / 60.0f, Popping);
        if (Exhaust.IsPopping())
        {
            ++Sounding;
            if (!WasPopping)
                ++Onsets;
        }
        WasPopping = Exhaust.IsPopping();
    }
    // 📝 Each tip fires 35-90 ms bursts with random gaps, so over 400 frames the exhaust starts many
    //    separate pops and is silent for a good part of the time.
    Check(Onsets > 4u && Onsets < 200u, "3C: an open afterfire window fires separate pops, not one long one");
    Check(Sounding > Onsets && Sounding < 400u, "3C: a pop sounds for several frames but not for all of them");
    Check(Exhaust.Lights().size() <= 1u, "3C: a pop publishes one fire light while it burns");

    PlumeTelemetry Quiet = ExhaustTelemetry;
    Quiet.AfterfireSeconds = 0.0f;
    PlumeSourceProjection Silent;
    Silent.Project(Quiet, 1.0f / 60.0f, Options);
    Check(!Silent.IsPopping() && Silent.Count() == 0u, "3C: a closed afterfire window pops not at all");

    // 📝 Transient bursts: queued with a duration, shed while it lasts, dropped when it is spent, and
    //    capped at four.
    PlumeSourceProjection Transient;
    for (uint32_t Index = 0u; Index < 6u; ++Index)
    {
        PlumeBurst Burst;
        Burst.Position = { float(Index), 0.0f, 0.5f };
        Burst.Seconds = 1.0f;
        Burst.Smoke = 10.0f;
        Transient.QueueBurst(Burst);
    }
    Check(Transient.Project(PlumeTelemetry{}, 0.5f, Options) == 4u, "3C: at most four transient bursts are held");
    Check(Near(Transient.Source(0u).Smoke, 10.0f, 1e-3f), "3C: a fresh burst sheds its full rate");
    // 📝 Halfway through its life a burst sheds half its rate; the moment it is spent it is dropped.
    Check(Transient.Project(PlumeTelemetry{}, 0.3f, Options) == 4u, "3C: a burst is still shed while it lasts");
    Check(Near(Transient.Source(0u).Smoke, 5.0f, 1e-3f), "3C: a burst halfway through its life sheds half its rate");
    Check(Transient.Project(PlumeTelemetry{}, 0.3f, Options) == 4u, "3C: a burst is shed right up to its last moment");
    Check(Near(Transient.Source(0u).Smoke, 2.0f, 1e-3f), "3C: a burst nearly spent sheds nearly nothing");
    Check(Transient.Project(PlumeTelemetry{}, 0.3f, Options) == 0u, "3C: a burst is dropped the moment its time is spent");

    // 📝 Bounds: every source grown by 3.5 of its radii — the Gaussian's reach.
    Vec3 Low = {};
    Vec3 High = {};
    Check(Projection.ResolveBounds(Low, High), "3C: a projected plume has bounds");
    const PlumeSourceEntry First = Projection.Source(0u);
    const float Reach = std::max(First.Radius, 0.3f) * 3.5f;
    Check(Low.X <= First.Position.X - Reach + 1e-4f && High.X >= First.Position.X + Reach - 1e-4f,
          "3C: the bounds cover each source grown by 3.5 of its radii");
    Check(Low.X <= High.X && Low.Y <= High.Y && Low.Z <= High.Z, "3C: the bounds are ordered");
    PlumeSourceProjection Empty;
    Check(!Empty.ResolveBounds(Low, High), "3C: a plume with no sources has no bounds");

    // 📝 The table never overruns its capacity, whatever the telemetry asks for.
    PlumeSourceProjection Flood;
    PlumeTelemetry FloodTelemetry = BurningTelemetry;
    for (uint32_t Wheel = 0u; Wheel < 8u; ++Wheel)
        FloodTelemetry.Wheels.push_back(BurningTelemetry.Wheels[0]);
    Check(Flood.Project(FloodTelemetry, 0.10f, Options) <= PlumeSourceCapacity,
          "3C: the source table never overruns its 24-source capacity");
    Check(!Flood.AppendSource({ 0.0f, 0.0f, 0.0f }, 0.3f, { 0.0f, 0.0f, 1.0f }, 1.0f, 1.0f, 1.0f, 1.0f)
          || Flood.Count() <= PlumeSourceCapacity, "3C: a full table refuses another source");
    Check(!Flood.AppendSource({ 0.0f, 0.0f, 0.0f }, 0.3f, { 0.0f, 0.0f, 1.0f }, 0.0f, 0.0f, 0.0f, 0.0f)
          || Flood.Count() < PlumeSourceCapacity, "3C: a source that sheds nothing is not appended");
}

void ValidateSmokeSequence()
{
    // 📝 The catalogue: every parameter has a default, a range and a step, and the defaults are the
    //    ones the exhibit opens with.
    const std::span<const GasParameterDescriptor> Catalogue = GasParameterCatalogue();
    Check(Catalogue.size() == 33u, "3C: the gas catalogue carries every parameter the editor drives");
    bool Catalogued = true;
    for (const GasParameterDescriptor& Entry : Catalogue)
        if (Entry.Slot == nullptr || Entry.Name == nullptr || Entry.Name[0] == '\0'
            || !(Entry.Minimum < Entry.Maximum) || !(Entry.Step > 0.0f)
            || !(Entry.Default >= Entry.Minimum) || !(Entry.Default <= Entry.Maximum))
            Catalogued = false;
    Check(Catalogued, "3C: every catalogue entry names a slot and a sane range");

    const GasParameters Defaults = ConstructGasParameters();
    Check(Near(Defaults.Buoyancy, 2.4f) && Near(Defaults.Dissipation, 0.10f) && Near(Defaults.Albedo, 0.88f)
          && Near(Defaults.FlameCore, 1800.0f), "3C: the catalogue defaults are the exhibit's");
    bool Applied = true;
    for (const GasParameterDescriptor& Entry : Catalogue)
        if (!Near(Defaults.*Entry.Slot, Entry.Default, 1e-6f))
            Applied = false;
    Check(Applied, "3C: ConstructGasParameters applies every catalogue default");

    GasParameters Set = Defaults;
    Check(AssignGasParameter(Set, "Vorticity", 99.0f) && Near(Set.Vorticity, 6.0f),
          "3C: assigning a parameter clamps it to the catalogue's maximum");
    Check(AssignGasParameter(Set, "WindHeading", -50.0f) && Near(Set.WindHeading, 0.0f),
          "3C: assigning a parameter clamps it to the catalogue's minimum");
    Check(!AssignGasParameter(Set, "NoSuchParameter", 1.0f), "3C: assigning an unknown parameter is refused");
    Check(Near(ReadGasParameter(Set, "Vorticity"), 6.0f) && ReadGasParameter(Set, "NoSuchParameter") == 0.0f,
          "3C: reading a parameter by name returns what was assigned");
    Set.Heat = 1.0e9f;
    ClampGasParameters(Set);
    Check(Set.Heat <= 4.0f, "3C: clamping brings every parameter back into range");

    // 📝 The mapping onto the lattice's settings.
    const PlumeSettings Mapped = MapGasParameters(Defaults, true);
    Check(Mapped.PlumeEnabled, "3C: an enabled plume maps to an enabled lattice");
    Check(Near(Mapped.PlumeDensity, 1.2f * Defaults.Density), "3C: the density multiplier carries the exhibit's 1.2");
    Check(Near(Mapped.PlumeFlameCore, Defaults.FlameCore * Defaults.FlameTemperature),
          "3C: the flame temperature scales the core temperature");
    Check(Near(Mapped.PlumeFlameTip, Defaults.FlameTip * Defaults.FlameTemperature),
          "3C: the flame temperature scales the tip temperature");
    Check(Near(Mapped.PlumeFlameDecay, -std::log(1.0f - Defaults.FlameLoss), 1e-4f),
          "3C: the per-second flame loss becomes a per-second decay rate");
    Check(!MapGasParameters(Defaults, false).PlumeEnabled, "3C: a disabled plume maps to a disabled lattice");

    GasParameters Retained = Defaults;
    Retained.FlameLoss = 0.999f;
    Check(MapGasParameters(Retained, true).PlumeFlameDecay > 0.0f,
          "3C: a flame that is never lost still decays, clamped away from zero");

    // 📝 The telemetry projection: the steered tread heading, the contact decision, the thermal state.
    SmokeVehicleState Vehicle;
    Vehicle.Position = { 4.0f, -2.0f, 0.0f };
    Vehicle.LinearVelocity = { 12.0f, 0.0f, 0.0f };
    Vehicle.Orientation = AxisRotor({ 0.0f, 0.0f, 1.0f }, 1.5707963267948966f);       // body facing +Y
    for (uint32_t Wheel = 0u; Wheel < 4u; ++Wheel)
    {
        SmokeWheelState State;
        State.Radius = 0.35f;
        State.Spin = 40.0f;
        State.Steer = AxisRotor({ 0.0f, 0.0f, 1.0f }, 0.0f);
        Vehicle.Wheels.push_back(State);
    }
    std::vector<SmokeContactReading> Readings(4);
    for (uint32_t Wheel = 0u; Wheel < 4u; ++Wheel)
    {
        Readings[Wheel].ContactPoint = { 4.0f, -2.0f, 0.0f };
        Readings[Wheel].HubPosition = { 4.0f, -2.0f, 0.35f };
        Readings[Wheel].ContactVelocity = { 12.0f, 0.0f, 0.0f };
        Readings[Wheel].ContactFraction = 1.0f;
        Readings[Wheel].SlipSpeed = 9.0f;
        Readings[Wheel].SlidingPower = 6000.0f;
    }
    Readings[2].ContactFraction = 0.2f;                  // barely touching: not a source
    Readings[3].OnDebris = true;                         // on debris: not a source
    std::vector<SmokeThermalReading> Thermal(4);
    for (SmokeThermalReading& Reading : Thermal)
        Reading.Surface = 500.0f;

    const PlumeTelemetry Projected = ProjectPlumeTelemetry(Vehicle, Readings, Thermal);
    Check(Projected.Wheels.size() == 4u, "3C: every wheel is projected");
    Check(Near(Projected.Wheels[0].Longitudinal.X, 0.0f, 1e-5f) && Near(Projected.Wheels[0].Longitudinal.Y, 1.0f, 1e-5f),
          "3C: the tread heading is the body's turn applied to the forward axis");
    Check(Projected.Wheels[0].InContact && Projected.Wheels[1].InContact
          && !Projected.Wheels[2].InContact && !Projected.Wheels[3].InContact,
          "3C: a patch half off the road, or on debris, is not a source");
    Check(Near(Projected.Wheels[0].SurfaceSpeed, 40.0f * 0.35f), "3C: the surface speed is the spin times the radius");
    Check(Near(Projected.Position.X, 4.0f) && Near(Projected.Velocity.X, 12.0f), "3C: the telemetry carries the vehicle");

    Readings[1].Shredded = true;
    const PlumeTelemetry Bare = ProjectPlumeTelemetry(Vehicle, Readings, Thermal);
    Check(Bare.Wheels[1].SlidingPower == 0.0f && Bare.Wheels[0].SlidingPower == 6000.0f,
          "3C: a shredded tyre has no rubber left, so its sliding power is zero");
    Thermal[1].Ignited = true;
    const PlumeTelemetry BareIgnited = ProjectPlumeTelemetry(Vehicle, Readings, Thermal);
    Check(!BareIgnited.Wheels[1].Thermal.Ignited, "3C: a shredded tyre cannot be burning");

    // 📝 The advance: a burnout makes the plume resident and steps it.
    SmokeSequence Sequence(PlumeGrades[0]);
    SmokeAdvanceFrame Frame;
    Frame.Enabled = true;
    Frame.Vehicle = Vehicle;
    Frame.Readings = Readings;
    Frame.Thermal = Thermal;
    Frame.Gas = Defaults;
    Frame.SunDirection = { 0.3f, 0.4f, 0.86f };

    // 📝 The onset needs the sliding power sustained for SourceOnsetSeconds, so a burnout takes a few
    //    frames to light.
    for (uint32_t Index = 0u; Index < 8u; ++Index)
    {
        Sequence.Advance(1.0f / 60.0f, Frame);
        Sequence.CommitStep();                     // the renderer recorded the transport of this step
        if (Index == 0u)
            Check(Sequence.SourceCount() == 0u, "3C: one frame of scrub is not yet a smoking tyre");
    }
    Check(Sequence.SourceCount() > 0u, "3C: a sustained burnout projects tyre sources");
    Check(Sequence.Plume().IsResident(), "3C: sources make the plume resident");
    Check(Sequence.StepsTaken() >= 1u, "3C: the frame the burnout starts steps the lattice");
    Check(Sequence.Settings().PlumeEnabled && Near(Sequence.Settings().PlumeBuoyancy, Defaults.Buoyancy),
          "3C: the advance runs the mapped settings");
    Check(Sequence.QueryReadout().size() == 2u && !Sequence.QueryReadout()[0].empty(),
          "3C: the editor readout describes the resident plume");

    // 📝 Driving the car across the pad relabels the toroidal window without a copy.
    const uint32_t StepsBefore = Sequence.StepsTaken();
    for (uint32_t Index = 0u; Index < 400u; ++Index)
    {
        Frame.Vehicle.Position = { 4.0f + 0.25f * float(Index), -2.0f, 0.0f };
        Sequence.Advance(1.0f / 60.0f, Frame);
        Sequence.CommitStep();
    }
    Check(Sequence.StepsTaken() > StepsBefore, "3C: the plume keeps stepping as the car drives");
    Check(Sequence.Relocations() > 0u, "3C: driving across the pad relabels the window's texels");

    // 📝 Extra emissions: the first four slots are the tyres', so only the extras are appended.
    SmokeAdvanceFrame Extras = Frame;
    Extras.Emissions.assign(6, SmokeEmission{});
    Extras.Emissions[5].Active = true;
    Extras.Emissions[5].SmokeRate = 4.0f;
    Sequence.Advance(1.0f / 60.0f, Extras);
    Check(Sequence.SourceCount() <= PlumeSourceCapacity, "3C: extra emissions never overrun the source table");
    Check(Sequence.QueuedSources() >= Sequence.SourceCount(), "3C: the queued count reports every source appended");

    // 📝 Pressure sweeps follow the editor's iterations, rounded to an even count.
    SmokeAdvanceFrame Sweeps = Frame;
    Sweeps.Gas.PressureSweeps = 7.0f;
    Sequence.Advance(1.0f / 60.0f, Sweeps);
    Check(Sequence.Plume().Configuration().PressureSweeps % 2u == 0u,
          "3C: the pressure sweeps are rounded to an even count");
    Sweeps.Gas.PressureSweeps = 1.0f;
    Sequence.Advance(1.0f / 60.0f, Sweeps);
    Check(Sequence.Plume().Configuration().PressureSweeps >= 4u,
          "3C: the pressure sweeps never fall below four");

    // 📝 Fire lights: a burning tyre publishes one, and the aggregate is their luminance-weighted centre.
    SmokeAdvanceFrame Aflame = Frame;
    for (SmokeThermalReading& Reading : Aflame.Thermal)
    {
        Reading.Ignited = true;
        Reading.CombustionPower = 42000.0f;
    }
    Aflame.Readings[1].Shredded = false;
    Sequence.Advance(1.0f / 60.0f, Aflame);
    Check(Sequence.Sources().Lights().size() >= 3u, "3C: burning tyres publish fire lights");
    Check(Sequence.FireLight().Intensity > 0.0f, "3C: the aggregate fire light carries their total intensity");
    Check(Sequence.FireLight().Position[0] != 0.0f || Sequence.FireLight().Position[1] != 0.0f,
          "3C: the aggregate fire light sits at their luminance-weighted centre");

    // 📝 Switching the plume off, and clearing it.
    SmokeAdvanceFrame Off = Aflame;
    Off.Enabled = false;
    Sequence.Advance(1.0f / 60.0f, Off);
    Check(!Sequence.Settings().PlumeEnabled, "3C: switching the plume off disables the lattice");
    Check(Sequence.Sources().Lights().size() >= 3u, "3C: the sources are still projected when the plume is off");

    const uint32_t Lifetime = Sequence.StepsTaken();
    Sequence.Clear();
    Check(!Sequence.Plume().IsResident() && !Sequence.Plume().IsStepPending(),
          "3C: clearing the sequence forgets the plume");
    // 📝 Steps taken is a lifetime metric (the readout's "steps"), not a per-plume one.
    Check(Sequence.StepsTaken() == Lifetime, "3C: clearing the sequence keeps the lifetime step count");
    Check(Sequence.FireLight().Intensity == 0.0f, "3C: clearing the sequence puts the fire light out");
    Check(!Sequence.Sources().Onsets()[0].Active, "3C: clearing the sequence forgets the onset hysteresis");
}

//==========================================================================================================================================
//                                                        3D · SPARK COLLISION
//==========================================================================================================================================

void ValidateSparkCollision()
{
    // 📝 The planes of a box: six outward normals with the far edge in the offset, so the inside is
    //    negative and a spark is outside whenever its signed distance is positive.
    const std::array<float, 24> Planes = BoxPlanes({ -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f });
    Check(Planes.size() == 24u, "3D: a box is six planes, four floats each");
    Check(Near(Planes[0], 1.0f) && Near(Planes[3], 1.0f) && Near(Planes[4], -1.0f) && Near(Planes[7], 1.0f),
          "3D: a box's +X plane faces out with its far edge in the offset, its −X plane faces back");

    // 📝 A step through the box meets the near face, a skin clear of it, on the way in.
    const std::optional<SparkContact> Through = SweepConvex({ -3.0f, 0.0f, 0.0f }, { 3.0f, 0.0f, 0.0f }, Planes);
    Check(Through.has_value(), "3D: a step straight through a box meets it");
    Check(Through && Near(Through->Time, 1.997f / 6.0f, 2.0e-3f), "3D: the contact is where the step crosses the near face");
    Check(Through && Near(Through->Normal.X, -1.0f, 1e-5f), "3D: the contact normal faces back along the step");
    Check(Through && Near(Through->Push, SparkSkin), "3D: the contact lifts the spark a skin clear of the face");

    // 📝 A step that misses is clear — no contact at all, and no phantom push.
    Check(!SweepConvex({ -3.0f, 5.0f, 0.0f }, { 3.0f, 5.0f, 0.0f }, Planes), "3D: a step past a box is clear");
    Check(!SweepConvex({ -3.0f, 0.0f, 0.0f }, { -5.0f, 0.0f, 0.0f }, Planes), "3D: a step away from a box is clear");

    // 📝 A point already inside is put out through the nearest face, at once.
    const std::optional<SparkContact> Inside = SweepConvex({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 5.0f }, Planes);
    Check(Inside && Inside->Time == 0.0f, "3D: a point inside a box is in contact at once");
    Check(Inside && Inside->Push > 1.0f, "3D: a point inside a box is pushed clear of the nearest face");
    Check(Inside && Near(Inside->Normal.X, 1.0f, 1.0e-5f), "3D: it leaves through the face it is nearest");

    // 📝 The course: the flat pad, and a slope whose normal tilts back against the rise.
    FlatSparkCourse Pad;
    Check(Near(Pad.Height(12.0f, -4.0f), 0.0f), "3D: the pad is flat and at zero");
    Check(Near(Pad.Normal(0.0f, 0.0f).Z, 1.0f, 1e-6f), "3D: the pad's normal is straight up");

    struct Slope final : public SparkCourseSurface
    {
        [[nodiscard]] float Height(float X, float) const noexcept override { return 0.1f * X; }
    } Ramp;
    const Vec3 Tilt = Ramp.Normal(0.0f, 0.0f);
    Check(Tilt.X < 0.0f && Near(Tilt.Z, 1.0f / std::sqrt(1.01f), 1e-5f),
          "3D: a slope's normal tilts back against the rise, by central differences over ±0.15 m");

    // 📝 A falling spark meets the pad a skin above it, not at it.
    const std::optional<SparkContact> Landing = SweepSpark({ 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f }, {}, {}, &Pad);
    Check(Landing && Near(Landing->Time, 0.997f / 2.0f, 3.0e-3f), "3D: a falling spark meets the pad a skin above it");
    Check(Landing && Near(Landing->Normal.Z, 1.0f, 1e-6f), "3D: it meets the pad's normal");
    Check(!SweepSpark({ 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.5f }, {}, {}, &Pad), "3D: a spark still above the pad is clear");

    // 📝 Sweeping everything takes the earliest contact.
    SparkBox Wall;
    Wall.Minimum = { 1.0f, -1.0f, -1.0f };
    Wall.Maximum = { 3.0f, 1.0f, 1.0f };
    const std::array<SparkBox, 1> Walls{ Wall };
    const std::optional<SparkContact> HitWall = SweepSpark({ -5.0f, 0.0f, 0.5f }, { 5.0f, 0.0f, 0.5f }, Walls, {}, &Pad);
    Check(HitWall && Near(HitWall->Time, 5.997f / 10.0f, 4.0e-3f), "3D: a spark meets the wall it is thrown at");
    const std::optional<SparkContact> HitPad = SweepSpark({ -5.0f, 0.0f, 0.5f }, { 5.0f, 0.0f, -0.5f }, Walls, {}, &Pad);
    Check(HitPad && HitWall && HitPad->Time < HitWall->Time, "3D: the earliest of the wall and the pad wins");
    const std::optional<SparkContact> HitAlone = SweepSpark({ -5.0f, 0.0f, 0.5f }, { 5.0f, 0.0f, 0.5f }, Walls, {}, nullptr);
    Check(HitAlone && HitWall && Near(HitAlone->Time, HitWall->Time, 1e-6f),
          "3D: without a course the wall is still swept, at the same contact");

    // 📝 A box given its own planes is swept by them, not by its bounds.
    SparkBox Overridden = Wall;
    Overridden.Planes.assign(Planes.begin(), Planes.end());
    const std::array<SparkBox, 1> Overrides{ Overridden };
    const std::optional<SparkContact> HitPlanes = SweepSpark({ -3.0f, 0.0f, 0.0f }, { 3.0f, 0.0f, 0.0f }, Overrides, {}, nullptr);
    Check(HitPlanes && HitPlanes->Time < 0.4f, "3D: a box carrying its own planes is swept by them, not by its bounds");

    // 📝 A convex debris body sweeps like a box, through its bounding sphere's broadphase.
    SparkConvexBody Debris;
    Debris.Position = { 0.0f, 0.0f, 0.0f };
    Debris.Radius = 1.0f;
    Debris.WorldPlanes.assign(Planes.begin(), Planes.end());
    const std::array<SparkConvexBody, 1> Bodies{ Debris };
    const std::optional<SparkContact> HitBody = SweepSpark({ -3.0f, 0.0f, 0.0f }, { 3.0f, 0.0f, 0.0f }, {}, Bodies, nullptr);
    Check(HitBody && Near(HitBody->Time, 1.997f / 6.0f, 2.0e-3f), "3D: a convex debris body is swept like a box");
    const std::optional<SparkContact> MissBody = SweepSpark({ -3.0f, 5.0f, 0.0f }, { 3.0f, 5.0f, 0.0f }, {}, Bodies, nullptr);
    Check(!MissBody, "3D: a debris body's bounding sphere rejects a step that cannot reach it");

    // 📐 A spark skips: it keeps 55 % of its tangential speed and 28 % of its approach, reflected.
    const Vec3 Dropped = BounceSpark({ 0.0f, 0.0f, -10.0f }, { 0.0f, 0.0f, 1.0f });
    Check(Near(Dropped.Z, 2.8f, 1e-5f), "3D: a spark dropped on the road leaves at 28 % of its speed");
    const Vec3 Grazing = BounceSpark({ 10.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f });
    Check(Near(Grazing.X, 5.5f, 1e-5f), "3D: a spark grazing the road keeps 55 % of its speed along it");
    const Vec3 Oblique = BounceSpark({ 10.0f, 0.0f, -10.0f }, { 0.0f, 0.0f, 1.0f });
    Check(Near(Oblique.X, 5.5f, 1e-5f) && Near(Oblique.Z, 2.8f, 1e-5f),
          "3D: an oblique skip splits into 55 % along the road and 28 % back off it");
    const Vec3 Leaving = BounceSpark({ 0.0f, 0.0f, 10.0f }, { 0.0f, 0.0f, 1.0f });
    Check(Near(Leaving.Z, 5.5f, 1e-5f), "3D: with no approach there is no rebound, only the tangential loss");
}

//==========================================================================================================================================
//                                                        3D · SPARK SEQUENCE
//==========================================================================================================================================

void ValidateSparkSequence()
{
    // 📐 Blackbody radiance: a spark is a few hundred times brighter than the road it flies over, and
    //    its brightness follows the fourth power of its temperature.
    const std::array<float, 3> Hot = ResolveSparkRadiance(2450.0f);
    const std::array<float, 3> Cool = ResolveSparkRadiance(1500.0f);
    const float Ratio = std::pow(2450.0f / 1500.0f, 4.0f);
    Check(Near(Hot[0] / Cool[0], Ratio, 1.0e-3f), "3D: a spark's brightness follows the fourth power of its temperature");
    Check(Hot[2] > 0.0f && Cool[2] == 0.0f, "3D: a hotter spark is bluer — at 1500 K there is no blue yet");
    Check(Near(ResolveSparkRadiance(950.0f)[0], 0.0f, 1e-6f), "3D: a spark at the visible threshold is dark — it is gone");
    Check(Hot[0] > 5.0f, "3D: a fresh spark outshines sunlit white");

    // 📝 The end-of-life fade and the flicker, shared with the pooled GI light proxies.
    SparkParticle Fresh = {};
    Fresh.LifeSeconds = 2.5f;
    Fresh.Age = 0.0f;
    Check(Near(ResolveSparkGain(Fresh), 1.0f, 1e-6f), "3D: a fresh spark carries its full energy");
    SparkParticle Dying = Fresh;
    Dying.Age = 2.5f;
    Check(ResolveSparkGain(Dying) == 0.0f, "3D: a spark that has lived its life is faded out");
    SparkParticle Flickering = Fresh;
    Flickering.Flicker = 0.1f;
    Flickering.Frequency = 10.0f;
    Flickering.Phase = 0.0f;
    Flickering.Age = 0.025f;                       // a quarter of a flicker cycle: the bottom of the dip
    Check(Near(ResolveSparkGain(Flickering), 1.0f - Flickering.Flicker, 1e-6f),
          "3D: the flicker dips a spark's gain by its depth at the bottom of the cycle");
    Flickering.Age = 0.075f;                       // the top of the ripple: full gain again
    Check(Near(ResolveSparkGain(Flickering), 1.0f, 1e-6f), "3D: the flicker restores it at the top");

    // 📝 A hull scrape: metal dragged along the asphalt throws sparks along the slide, fanned and
    //    kicked off the surface.
    SparkSequence Sparks;
    SparkScrape Hull;
    Hull.Point = { 0.0f, 0.0f, 0.02f };
    Hull.Velocity = { -18.0f, 0.0f, 0.0f };
    Hull.Normal = { 0.0f, 0.0f, 1.0f };
    Hull.Strength = 1.0f;
    const std::array<SparkScrape, 1> HullScrape{ Hull };
    Sparks.Emit(HullScrape, 1.0f / 60.0f);
    Check(Sparks.Emitted() > 0u, "3D: a scrape throws sparks");
    Check(Sparks.Live() == Sparks.Emitted(), "3D: every spark thrown is live");
    Check(Sparks.Emitted() < uint32_t(SparkMaximumPerScrape),
          "3D: one frame of one scrape is well under the per-scrape packet cap");
    Check(Sparks.LastScrapeCount() == 1u, "3D: the readout reports the frame's scrapes");
    Check(Sparks.PulseCount() == 1u, "3D: one scraping source is one correlated packet");

    // 📝 Packets, not a hose: the same contact's rate is re-rolled every 40–130 ms, so the stream
    //    swells and fades instead of running flat.
    uint32_t Least = ~0u;
    uint32_t Most = 0u;
    for (uint32_t Frame = 0u; Frame < 60u; ++Frame)
    {
        const uint32_t Before = Sparks.Emitted();
        Sparks.Emit(HullScrape, 1.0f / 60.0f);
        Least = std::min(Least, Sparks.Emitted() - Before);
        Most = std::max(Most, Sparks.Emitted() - Before);
    }
    Check(Most > Least, "3D: the packets swell and fade the stream, rather than running it flat");
    Check(Sparks.PulseCount() == 1u, "3D: the same contact keeps its packet, it does not start a new one");

    // 📝 A rim scrape of the same wheel shares the packet; a second wheel starts its own.
    SparkScrape Rim = Hull;
    Rim.Rim = true;
    Rim.Wheel = 2;
    Rim.SpinSpeed = -20.0f;
    Rim.Forward = { 1.0f, 0.0f, 0.0f };
    Rim.Radius = 0.35f;
    SparkSequence Rims;
    SparkScrape OtherRim = Rim;
    OtherRim.Wheel = 3;
    const std::array<SparkScrape, 2> TwoRims{ Rim, OtherRim };
    Rims.Emit(TwoRims, 1.0f / 60.0f);
    Check(Rims.PulseCount() == 2u, "3D: each scraping wheel is its own packet");
    Rims.Emit(TwoRims, 1.0f / 60.0f);
    Check(Rims.PulseCount() == 2u, "3D: the same wheels keep their packets");

    // 📝 In flight: gravity, drag, cooling, and a landing that counts as an impact.
    SparkSequence Flight;
    Flight.Settings().Variation = 0.0f;                  // the neutral spark: no spread at all
    SparkScrape Flat = Hull;
    Flat.Velocity = { -18.0f, 0.0f, 0.0f };
    const std::array<SparkScrape, 1> FlatScrape{ Flat };
    for (uint32_t Frame = 0u; Frame < 6u; ++Frame)
        Flight.Emit(FlatScrape, 1.0f / 60.0f);
    Check(Flight.Live() > 0u, "3D: the neutral spark still throws");

    FlatSparkCourse Pad;
    const uint32_t LiveBefore = Flight.Live();
    Flight.Advance(1.0f / 60.0f, {}, {}, &Pad);
    Check(Flight.Live() <= LiveBefore, "3D: sparks die in flight, none are born");
    Check(Flight.Live() > 0u, "3D: a frame of flight does not empty the pool");
    Check(Flight.Instances().size() == size_t(Flight.Live()) * SparkInstanceFloats,
          "3D: the instance table is twelve floats per live spark");
    Check(Flight.Particles().size() == Flight.Live(), "3D: the particle span is the live sparks");

    // 📝 The instance: a streak from where the camera saw the spark a frame ago to where it is now,
    //    2.2 mm across, with the shutter and the pixel width the raster reads.
    const SparkInstance First = Flight.Instance(0u);
    const SparkParticle& Head = Flight.Particles()[0];
    Check(Near(First.Head.X, Head.Position.X, 1e-5f) && Near(First.Head.Z, Head.Position.Z, 1e-5f),
          "3D: the streak's head is where the spark is");
    Check(Near(First.Tail.X, Head.Prior.X, 1e-5f) && Near(First.Tail.Z, Head.Prior.Z, 1e-5f),
          "3D: the streak's tail is where the camera saw it a frame ago");
    Check(Near(First.Width, SparkWidth * Head.Size, 1e-8f), "3D: the streak is 2.2 mm across, times the spark's size");
    Check(First.Radiance.X > 0.0f && First.Shutter > 0.0f && First.Shutter <= 1.0f,
          "3D: the streak carries the spark's radiance and its shutter fraction");
    Check(First.Head.Z >= 0.0f, "3D: a spark that has landed is lifted clear of the pad, not sunk into it");

    // 📝 Cooling: sparks fade below the visible threshold and are gone.
    SparkSequence Cooling;
    Cooling.Settings().Variation = 0.0f;
    Cooling.Emit(FlatScrape, 1.0f / 60.0f);
    for (uint32_t Frame = 0u; Frame < 600u && Cooling.Live() > 0u; ++Frame)
        Cooling.Advance(1.0f / 60.0f, {}, {}, nullptr);
    Check(Cooling.Live() == 0u, "3D: sparks cool below the visible threshold and are gone");
    Check(Cooling.Instances().empty(), "3D: nothing is drawn for a pool with nothing in it");

    // 📝 Landing on the pad counts impacts, and a spark that has skipped eight times and stopped is gone.
    SparkSequence Bounces;
    Bounces.Settings().Variation = 0.0f;
    for (uint32_t Frame = 0u; Frame < 4u; ++Frame)
        Bounces.Emit(FlatScrape, 1.0f / 60.0f);
    for (uint32_t Frame = 0u; Frame < 240u; ++Frame)
        Bounces.Advance(1.0f / 60.0f, {}, {}, &Pad);
    Check(Bounces.Live() == 0u, "3D: every spark has either cooled or come to rest");

    // 📝 Walls: sparks thrown into a box are swept against it.
    SparkSequence Wall;
    Wall.Settings().Variation = 0.0f;
    SparkScrape IntoWall = Hull;
    IntoWall.Point = { -4.0f, 0.0f, 0.5f };
    IntoWall.Velocity = { 25.0f, 0.0f, 0.0f };
    const std::array<SparkScrape, 1> WallScrape{ IntoWall };
    for (uint32_t Frame = 0u; Frame < 4u; ++Frame)
        Wall.Emit(WallScrape, 1.0f / 60.0f);
    SparkBox Barrier;
    Barrier.Minimum = { -0.5f, -50.0f, -50.0f };
    Barrier.Maximum = { 0.5f, 50.0f, 50.0f };
    const std::array<SparkBox, 1> Barriers{ Barrier };
    for (uint32_t Frame = 0u; Frame < 25u; ++Frame)
        Wall.Advance(1.0f / 60.0f, Barriers, {}, nullptr);
    Check(Wall.Live() > 0u, "3D: the sparks are still in flight when the wall is checked");
    bool Penetrated = false;
    for (const SparkParticle& Spark : Wall.Particles())
        if (Spark.Position.X > Barrier.Maximum.X)
            Penetrated = true;
    Check(!Penetrated, "3D: no spark ends up inside the wall it was thrown at");

    // 📝 The cap: however hard the scrape, the pool never overruns.
    SparkSequence Flood;
    SparkScrape Hard = Hull;
    Hard.Strength = 40.0f;
    const std::array<SparkScrape, 1> HardScrape{ Hard };
    for (uint32_t Frame = 0u; Frame < 400u; ++Frame)
    {
        Flood.Emit(HardScrape, 1.0f / 60.0f);
        Flood.Advance(1.0f / 60.0f, {}, {}, nullptr);
    }
    Check(Flood.Live() <= SparkCapacity, "3D: the pool never overruns its capacity");
    Check(Flood.PulseCount() <= 128u, "3D: the packet table is capped at 128 sources");

    // 📝 Packets expire: half a second without the contact and the source is forgotten.
    SparkSequence Idle;
    Idle.Emit(HullScrape, 1.0f / 60.0f);
    Check(Idle.PulseCount() == 1u, "3D: a scrape registers its packet");
    for (uint32_t Frame = 0u; Frame < 60u; ++Frame)
        Idle.Emit({}, 1.0f / 60.0f);
    Check(Idle.PulseCount() == 0u, "3D: a source silent for half a second is forgotten");

    // 📝 Switching sparks off: nothing is thrown, and the pool is emptied.
    SparkSequence Off;
    Off.Emit(HullScrape, 1.0f / 60.0f);
    Off.Settings().Enabled = false;
    const uint32_t Held = Off.Emitted();
    Off.Emit(HullScrape, 1.0f / 60.0f);
    Check(Off.Emitted() == Held, "3D: sparks switched off throw nothing");
    Off.Advance(1.0f / 60.0f, {}, {}, &Pad);
    Check(Off.Live() == 0u, "3D: sparks switched off are cleared");

    // 📝 Clearing, and the readout.
    Off.Settings().Enabled = true;
    Off.Emit(HullScrape, 1.0f / 60.0f);
    const uint32_t Lifetime = Off.Emitted();
    Check(Lifetime > Held, "3D: switched back on, the scrape throws again");
    Off.Clear();
    Check(Off.Live() == 0u && Off.PulseCount() == 0u && Off.Instances().empty(), "3D: clearing forgets every spark");
    Check(Off.Emitted() == Lifetime, "3D: clearing does not rewind the lifetime emission count");
    Check(!Off.QueryReadout().empty() && Off.QueryReadout().find("sparks live") != std::string::npos,
          "3D: the editor readout counts the live sparks");
}

//==========================================================================================================================================
//                                                        3D · EFFECT SEQUENCE
//==========================================================================================================================================

void ValidateEffectSequence()
{
    // 📐 "#35d6ff" (sRGB) → linear: the cyan the cubes glow.
    const std::array<float, 3> Cyan = ParseEffectColour("#35d6ff");
    Check(Near(Cyan[2], 1.0f, 1.0e-3f) && Cyan[0] < Cyan[1] && Cyan[1] < Cyan[2],
          "3D: the derez cyan decodes to linear Rec.709, blue-dominant");
    Check(Near(ParseEffectColour("#ffffff")[0], 1.0f, 1e-6f), "3D: white decodes to one");
    Check(Near(ParseEffectColour("#000000")[0], 0.0f, 1e-6f), "3D: black decodes to zero");
    Check(Near(ParseEffectColour(nullptr)[0], 0.0f, 1e-6f), "3D: a missing colour decodes to zero");
    // 📝 sRGB is not linear: a mid grey decodes well below a half.
    Check(ParseEffectColour("#808080")[0] < 0.25f, "3D: a mid grey decodes below a quarter, as sRGB demands");

    // 📝 The palette: three material records, the white-hot core twice as luminous as the glow.
    EffectSettings Look = {};
    Look.Glow = ParseEffectColour("#35d6ff");
    Look.Hot = ParseEffectColour("#ffffff");
    Look.Glitch = ParseEffectColour("#ff2d6f");
    const std::array<std::array<float, 12>, 3> Palette = ResolveDerezPalette(Look);
    Check(Palette[1][5] > Palette[0][5], "3D: the white-hot core is more emissive than the cyan glow");
    Check(Near(Palette[0][3], 0.3f, 1e-6f) && Near(Palette[0][10], 0.04f, 1e-6f),
          "3D: the palette records carry the albedo and material words the demo writes");
    EffectSettings Dim = Look;
    Dim.Intensity = 0.5f;
    Check(Near(ResolveDerezPalette(Dim)[0][6], 0.5f * Palette[0][6], 1e-6f),
          "3D: the intensity scales the palette's emission");

    // 📝 Anchors spread over facets by area: a big facet gets its share, a degenerate one none.
    const EffectFacet Big = { { { { 0.0f, 0.0f, 0.0f }, { 10.0f, 0.0f, 0.0f }, { 0.0f, 10.0f, 0.0f } } }, 6u };
    const EffectFacet Small = { { { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } } }, 9u };
    const EffectFacet Flat = { { { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 2.0f, 0.0f, 0.0f } } }, 32u };
    const std::array<EffectFacet, 3> Facets{ Big, Small, Flat };
    const std::vector<float> Cells = SpreadEffectCells(Facets, 2000u);
    Check(Cells.size() == 2000u * EffectCellFloats, "3D: an anchor is eight floats");
    uint32_t OnBig = 0u;
    uint32_t OnSmall = 0u;
    for (uint32_t Index = 0u; Index < 2000u; ++Index)
    {
        const float* Cell = Cells.data() + size_t(Index) * EffectCellFloats;
        float Family = 0.0f;
        std::memcpy(&Family, &Cell[3], sizeof(Family));
        if (Family == 6.0f)
            ++OnBig;
        if (Family == 9.0f)
            ++OnSmall;
        Check(Cell[7] == 0.0f, "3D: an anchor's spare word is zero");
    }
    Check(OnBig > OnSmall * 20u, "3D: anchors are spread by area, so the big facet takes almost all of them");
    Check(OnSmall > 0u, "3D: the small facet still gets its share of anchors");
    Check(Near(Cells[4], 0.0f, 1e-6f) && Near(Cells[5], 0.0f, 1e-6f) && Near(Cells[6], 1.0f, 1e-6f),
          "3D: an anchor carries the facet's normal");

    // 📝 Deterministic: the same facets give the same anchors every run.
    Check(SpreadEffectCells(Facets, 64u) == SpreadEffectCells(Facets, 64u), "3D: the anchors are deterministic");
    Check(SpreadEffectCells(Facets, 64u) != SpreadEffectCells(Facets, 65u), "3D: the anchors differ with the count");
    Check(SpreadEffectCells({}, 16u).size() == 16u * EffectCellFloats, "3D: an empty facet set still yields a sized table");

    // 📝 A transform takes the anchors into the emitter's local frame.
    std::array<float, 16> Shift = { 1.0f, 0.0f, 0.0f, 0.0f,
                                    0.0f, 1.0f, 0.0f, 0.0f,
                                    0.0f, 0.0f, 1.0f, 0.0f,
                                    5.0f, 0.0f, 0.0f, 1.0f };
    const std::vector<float> Moved = SpreadEffectCells(Facets, 64u, &Shift);
    Check(Moved[0] > Cells[0] + 4.0f, "3D: a transform carries the anchors into the emitter's frame");

    // 📝 Poses: the shorter arc through the quaternion's double cover.
    const EffectPoseSample Start = { { 0.0f, 0.0f, 0.0f }, 0.0f, { 0.0f, 0.0f, 0.0f, 1.0f }, { 10.0f, 0.0f, 0.0f }, {} };
    EffectPoseSample End = Start;
    End.Clock = 1.0f;
    End.Position = { 10.0f, 0.0f, 0.0f };
    End.Rotation = { 0.0f, 0.0f, 0.9999f, -0.01f };       // nearly a full turn, reached the short way
    const EffectPoseSample Middle = InterpolateEffectPose(Start, End, 0.5f, 0.5f);
    Check(Near(Middle.Position[0], 5.0f, 1e-5f) && Near(Middle.Clock, 0.5f, 1e-6f),
          "3D: a pose interpolates its position and carries the clock");
    Check(Near(Middle.Velocity[0], 10.0f, 1e-5f), "3D: a pose interpolates its velocity");
    const EffectPoseSample Arrived = InterpolateEffectPose(Start, End, 1.0f, 1.0f);
    Check(End.Rotation[3] < 0.0f && Arrived.Rotation[3] > 0.0f,
          "3D: the shorter arc through the quaternion's double cover is taken, keeping the near representation");
    Check(Near(std::fabs(Arrived.Rotation[2]), std::fabs(End.Rotation[2]) / std::sqrt(End.Rotation[2] * End.Rotation[2] + End.Rotation[3] * End.Rotation[3]), 1e-4f),
          "3D: the far end of the arc is the same rotation, renormalised");
    Check(Near(Middle.Rotation[0] * Middle.Rotation[0] + Middle.Rotation[1] * Middle.Rotation[1]
             + Middle.Rotation[2] * Middle.Rotation[2] + Middle.Rotation[3] * Middle.Rotation[3], 1.0f, 1e-5f),
          "3D: an interpolated pose's quaternion is renormalised");
    const EffectPoseSample Clamped = InterpolateEffectPose(Start, End, 2.0f, 1.0f);
    Check(Near(Clamped.Position[0], End.Position[0], 1e-5f), "3D: the fraction is clamped to the segment");

    // 📝 The units: eight emitters of 128 B on a 96 B header; 1 920 anchors for a car, 216 for a cone.
    Check(EffectRecordExtent == EffectRecordHeader + EffectEmitterSlots * EffectEmitterStride,
          "3D: the record is a 96 B header and eight 128 B emitters");
    Check(EffectVehicleCellCount == EffectVehicleBodyCells + 4u * EffectVehicleWheelCells,
          "3D: a car is 1 536 body anchors and four wheels of 96");
    Check(EffectBoxCellCount == 6u * 36u, "3D: a cone is a 6 × 6 grid on each of its six faces");
    Check(EffectPoseCapacity * EffectPoseSpacing >= 5.9f, "3D: the pose ring holds six seconds at 60 Hz");
    Check(EffectCornerCount == 36u * 9u, "3D: a cube draw is 36 corners, each with its parent and eight children");

    // 📝 The derez: the mask goes on, the car's motion is recorded, and one emitter carries it.
    EffectSequence Effects;
    Check(Effects.RecordBytes().size() == EffectRecordExtent, "3D: the record is its declared extent");
    Check(Effects.Mask() == EffectMaskMode::Off && Effects.Live() == 0u, "3D: a fresh sequence is idle");
    const std::array<float, 16> Body = { 1.0f, 0.0f, 0.0f, 0.0f,
                                         0.0f, 1.0f, 0.0f, 0.0f,
                                         0.0f, 0.0f, 1.0f, 0.0f,
                                         0.0f, 0.0f, 0.4f, 1.0f };
    const float DerezSeconds = Effects.DerezVehicle(Body, { 12.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.5f }, Start);
    Check(Effects.Mask() == EffectMaskMode::Derez, "3D: derezing the car turns its mask on");
    Check(Effects.Live() == 1u, "3D: derezing the car emits one emitter");
    Check(Effects.IsRecording(), "3D: derezing the car records its motion");
    Check(Near(DerezSeconds, Effects.QueryMaskDuration(EffectMaskMode::Derez), 1e-6f),
          "3D: the derez returns how long until every cell has let go");
    Check(DerezSeconds > Effects.Settings().HoldOffset, "3D: the mask outlasts its hold");
    Check(Effects.PoseCount() == 1u, "3D: the first pose sample is the pose the derez started from");
    Check(Effects.RecordBytes().size() == EffectRecordExtent, "3D: the record keeps its extent");

    // 📝 The record's header: the clock, the mask, the bounds, the editor's controls and the pose count.
    std::array<float, 24> Header = {};
    std::memcpy(Header.data(), Effects.RecordBytes().data(), sizeof(Header));
    Check(Near(Header[0], 0.0f, 1e-6f), "3D: the record's clock is the sequence's");
    uint32_t Mode = 0u;
    std::memcpy(&Mode, &Header[1], sizeof(Mode));
    Check(Mode == uint32_t(EffectMaskMode::Derez), "3D: the record carries the mask mode");
    Check(Near(Header[4], Effects.BoundsMinimum()[0], 1e-6f) && Near(Header[7], Effects.Settings().HoldOffset, 1e-6f),
          "3D: the record's bounds and hold share their fourth word");
    Check(Near(Header[12], Effects.Settings().Burst, 1e-6f) && Near(Header[19], Effects.Settings().Flight, 1e-6f),
          "3D: the record carries the editor's launch, drag, life, size, wave and flight");
    Check(Near(Header[20], 1.0f, 1e-6f), "3D: the record carries the face-colours flag");
    Check(Near(Header[22], 0.0f, 1e-6f), "3D: one pose sample is not enough to interpolate, so the count reads zero");

    // 📝 Advancing: the pose ring fills at 60 Hz while the car is still moving.
    EffectPoseSample Moving = Start;
    Moving.Clock = 1.0f / 60.0f;
    Moving.Position = { 0.2f, 0.0f, 0.0f };
    Effects.Advance(1.0f / 60.0f);
    Effects.RecordPoses(Moving);
    Check(Effects.PoseCount() == 2u, "3D: a frame of motion appends one 60 Hz pose sample");
    Check(Effects.Clock() > 0.0f, "3D: the clock advances");
    for (uint32_t Frame = 0u; Frame < 30u; ++Frame)
    {
        Moving.Clock += 1.0f / 60.0f;
        Moving.Position[0] += 0.2f;
        Effects.Advance(1.0f / 60.0f);
        Effects.RecordPoses(Moving);
    }
    Check(Effects.PoseCount() == 32u, "3D: half a second of motion is thirty-two pose samples");
    Check(Effects.Poses().size() == size_t(Effects.PoseCount()) * EffectPoseFloats, "3D: a pose is sixteen floats");
    std::array<float, 24> Filled = {};
    std::memcpy(Filled.data(), Effects.RecordBytes().data(), sizeof(Filled));
    Check(Near(Filled[22], float(Effects.PoseCount()), 1e-6f), "3D: the record reports the pose count once it can interpolate");

    // 📝 The poses carry the car's motion: the last sample is at the car's latest position.
    const float* LastPose = Effects.Poses().data() + size_t(Effects.PoseCount() - 1u) * EffectPoseFloats;
    Check(Near(LastPose[0], Moving.Position[0], 0.25f), "3D: the pose ring tracks the car as it drives away");

    // 📝 A derez is followed by the car being hidden, then reassembled.
    Effects.StopPoses();
    Check(!Effects.IsRecording(), "3D: the recording ends when the car is hidden");
    Effects.HideVehicle();
    Check(Effects.Mask() == EffectMaskMode::Hidden, "3D: a finished derez hides the car");
    const float RezSeconds = Effects.RezVehicle(Body);
    Check(Effects.Mask() == EffectMaskMode::Rez, "3D: rezing the car turns its mask the other way");
    Check(Near(RezSeconds, Effects.QueryMaskDuration(EffectMaskMode::Rez), 1e-6f), "3D: the rez returns its own duration");
    Check(RezSeconds > Effects.Settings().Flight, "3D: the rez outlasts the cubes' flight");
    Effects.ClearVehicleMask();
    Check(Effects.Mask() == EffectMaskMode::Off, "3D: a finished rez clears the mask");

    // 📝 The mask's emitter is drawn first: the surface raster reads its seed from slot zero.
    EffectSequence Ordered;
    Ordered.DerezBox(Body, { 0.0f, 0.0f, 0.0f }, { 0.5f, 0.5f, 0.5f }, { 0.0f, 0.0f, 0.0f }, 60u);
    Check(Ordered.Live() == 1u, "3D: a cone derez emits one emitter");
    Ordered.DerezVehicle(Body, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, Start);
    Check(Ordered.Live() == 2u, "3D: two emitters are live at once");
    const std::array<std::optional<uint32_t>, EffectEmitterSlots>& Order = Ordered.DrawOrder();
    Check(Order[0].has_value() && Ordered.Emitters()[*Order[0]].has_value()
          && Ordered.Emitters()[*Order[0]]->Kind == EffectKind::VehicleDerez,
          "3D: the mask's emitter is drawn first, because the surface raster reads slot zero's seed");

    // 📝 The record's emitter: its transform, its velocity, its kind and its particle count.
    std::array<float, 32> Slot = {};
    std::memcpy(Slot.data(), Ordered.RecordBytes().data() + EffectRecordHeader, sizeof(Slot));
    Check(Near(Slot[12], 0.0f, 1e-6f) && Near(Slot[14], 0.4f, 1e-6f), "3D: the emitter's transform carries the car's centre");
    uint32_t Kind = 0u;
    std::memcpy(&Kind, &Slot[23], sizeof(Kind));
    Check(Kind == uint32_t(EffectKind::VehicleDerez), "3D: the emitter's kind rides in its angular velocity's spare word");
    Check(uint32_t(Slot[27]) == EffectVehicleCellCount, "3D: the emitter carries the car's 1 920 anchors");
    Check(Near(Slot[24], 1.0f, 1e-6f) && Near(Slot[28], 0.0f, 1e-6f),
          "3D: a car emitter's extent is unit and its centre is the car's origin");

    // 📝 Empty slots read as no kind and no particles, so the draw for them produces nothing.
    std::array<float, 32> Empty = {};
    std::memcpy(Empty.data(), Ordered.RecordBytes().data() + EffectRecordHeader + 4u * EffectEmitterStride, sizeof(Empty));
    Check(Empty[23] == 255.0f, "3D: an empty emitter slot reads as no kind");
    Check(Empty[27] == 0.0f, "3D: an empty emitter slot carries no particles");

    // 📝 A box: the family rides in the transform's spare word, where the affine transform leaves it free.
    Ordered.RezBox(Body, { 0.0f, 0.0f, 0.0f }, { 0.3f, 0.3f, 0.3f }, 61u);
    uint32_t Family = 0u;
    for (uint32_t Record = 0u; Record < EffectEmitterSlots; ++Record)
    {
        const uint8_t* Emitter = Ordered.RecordBytes().data() + EffectRecordHeader + Record * EffectEmitterStride;
        float Kind = 0.0f;
        std::memcpy(&Kind, Emitter + 23u * sizeof(float), sizeof(Kind));
        if (uint32_t(Kind) != uint32_t(EffectKind::BoxRez))
            continue;
        float Packed = 0.0f;
        std::memcpy(&Packed, Emitter + 3u * sizeof(float), sizeof(Packed));
        Family = uint32_t(Packed);
    }
    Check(Family == 61u, "3D: a box emitter's family rides in its transform's spare word");

    // 📝 A burst tyre: rubber thrown at once, with the car's velocity, and never glowing.
    Ordered.BurstTyre(Body, { 0.35f, 0.35f, 0.25f }, { 30.0f, 0.0f, 2.0f }, 9u);
    bool Thrown = false;
    for (const std::optional<EffectEmitterState>& Emitter : Ordered.Emitters())
        if (Emitter && Emitter->Kind == EffectKind::Debris && Emitter->Velocity[0] > 29.0f)
            Thrown = true;
    Check(Thrown, "3D: a burst tyre throws its rubber with the car's velocity");

    // 📝 Eight emitters at once, and the one closest to finishing gives way to a ninth.
    EffectSequence Busy;
    for (uint32_t Index = 0u; Index < EffectEmitterSlots; ++Index)
        Busy.DerezBox(Body, { 0.0f, 0.0f, 0.0f }, { 0.2f, 0.2f, 0.2f });
    Check(Busy.Live() == EffectEmitterSlots, "3D: eight emitters run at once");
    Check(Busy.Emitted() == EffectEmitterSlots, "3D: eight emissions, eight emitters");
    Busy.RezBox(Body, { 0.0f, 0.0f, 0.0f }, { 0.2f, 0.2f, 0.2f });
    Check(Busy.Live() == EffectEmitterSlots, "3D: a ninth emitter takes a slot without overrunning the table");
    Check(Busy.Emitted() == EffectEmitterSlots + 1u, "3D: the ninth emission is counted");
    float Soonest = 1.0e30f;
    for (const std::optional<EffectEmitterState>& Emitter : Busy.Emitters())
        if (Emitter)
            Soonest = std::min(Soonest, Emitter->End);
    Check(Soonest <= Busy.QueryDuration(EffectKind::BoxRez) + 1.0e-6f,
          "3D: the emitter closest to finishing is the one that gave way");

    // 📝 Presets: the three from the Particle Editor, applied by name.
    EffectSequence Presets;
    Check(Presets.ApplyPreset("Derez cube"), "3D: the derez cube preset is known by name");
    Check(Near(Presets.Settings().Size, DerezPresets()[0].Size, 1e-6f), "3D: a preset applies its cube size");
    Check(Presets.Settings().Glow[2] > Presets.Settings().Glow[0], "3D: a preset applies its glow colour");
    Check(Presets.ApplyPreset("Glitch dissolve") && Presets.Settings().Style == 1u, "3D: the glitch preset is the glitch style");
    Check(Presets.Settings().Drag > DerezPresets()[0].Drag, "3D: the glitch preset drags its cubes harder");
    Check(!Presets.ApplyPreset("No such preset"), "3D: an unknown preset is refused");
    Check(!Presets.ApplyPreset(nullptr), "3D: a missing preset name is refused");
    // 📝 The glitch preset's wave is twice the hold's, because it lets go in bands.
    Presets.ApplyPreset("Derez cube");
    const float Cubes = Presets.QueryMaskDuration(EffectMaskMode::Derez);
    Presets.ApplyPreset("Glitch dissolve");
    Check(Presets.QueryMaskDuration(EffectMaskMode::Derez) != Cubes, "3D: the style changes the mask's duration");

    // 📝 Durations: a cube outlives its release wave, and a cone's is shorter than a car's.
    Check(Presets.QueryDuration(EffectKind::VehicleDerez) > Presets.QueryMaskDuration(EffectMaskMode::Derez),
          "3D: a derez emitter outlives the mask, for the cubes still in flight");
    Check(Presets.QueryDuration(EffectKind::BoxDerez) < Presets.QueryDuration(EffectKind::VehicleDerez),
          "3D: a cone comes apart faster than a car");
    Check(Presets.QueryDuration(EffectKind::Debris) > 0.0f, "3D: a burst tyre's rubber lasts a while");

    // 📝 The frame: emitters retire when their time is up, the mask's when the mask is cleared.
    EffectSequence Lifetimes;
    const float CubeSeconds = Lifetimes.QueryDuration(EffectKind::BoxDerez);
    Lifetimes.DerezBox(Body, { 0.0f, 0.0f, 0.0f }, { 0.2f, 0.2f, 0.2f });
    Check(Lifetimes.Live() == 1u, "3D: a cone derez is live");
    for (uint32_t Frame = 0u; Frame < uint32_t(std::ceil(CubeSeconds * 60.0f)) + 2u; ++Frame)
        Lifetimes.Advance(1.0f / 60.0f);
    Check(Lifetimes.Live() == 0u, "3D: an emitter retires when its time is up");

    EffectSequence Masked;
    Masked.DerezVehicle(Body, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, Start);
    for (uint32_t Frame = 0u; Frame < uint32_t(std::ceil(Masked.QueryDuration(EffectKind::VehicleDerez) * 60.0f)) + 2u; ++Frame)
        Masked.Advance(1.0f / 60.0f);
    Check(Masked.Live() == 1u, "3D: the mask's emitter is kept, because the surface raster still reads its seed");
    Check(Masked.Mask() == EffectMaskMode::Derez, "3D: the mask stays on until it is cleared");
    Masked.ClearVehicleMask();
    Masked.Advance(1.0f / 60.0f);
    Check(Masked.Live() == 0u, "3D: clearing the mask retires its emitter");

    // 📝 The pose ring is six seconds: a long derez stops recording rather than overrunning it.
    EffectSequence Ring;
    Ring.DerezVehicle(Body, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, Start);
    EffectPoseSample Steady = Start;
    for (uint32_t Frame = 0u; Frame < 600u; ++Frame)
    {
        Steady.Clock += 1.0f / 60.0f;
        Ring.Advance(1.0f / 60.0f);
        Ring.RecordPoses(Steady);
    }
    Check(Ring.PoseCount() == EffectPoseCapacity, "3D: the pose ring fills to its six-second capacity and stops");
    Check(!Ring.IsRecording(), "3D: a full pose ring stops recording");

    // 📝 Bounds: the mask and the anchors share the car's local bounds.
    Ring.AssignBounds({ -2.4f, -0.95f, -0.55f }, { 2.4f, 0.95f, 0.85f });
    Check(Near(Ring.BoundsMaximum()[0], 2.4f, 1e-6f), "3D: the car's bounds are assigned");
    Ring.Advance(1.0f / 60.0f);
    std::array<float, 24> Bounds = {};
    std::memcpy(Bounds.data(), Ring.RecordBytes().data(), sizeof(Bounds));
    Check(Near(Bounds[8], 2.4f, 1e-6f), "3D: the car's bounds are uploaded to the record");

    // 📝 The readout, and clearing.
    Check(Ring.QueryReadout().size() == 3u && !Ring.QueryReadout()[0].empty(), "3D: the editor readout describes the effect");
    Check(Ring.QueryReadout()[1].find("1920") != std::string::npos, "3D: the readout counts the car's anchors");
    Ring.Clear();
    Check(Ring.Live() == 0u && Ring.PoseCount() == 0u && Ring.Mask() == EffectMaskMode::Off, "3D: clearing forgets everything");
    Check(Ring.RecordBytes().size() == EffectRecordExtent, "3D: the record keeps its extent through a clear");
}

} // namespace

int main(int argc, char** argv)
{
    std::string ArchiveDirectory = "Experimental/VehicleHmiPrototype/Archive";
    if (argc > 1)
        ArchiveDirectory = argv[1];

    ValidateInspectionContract();
    ValidatePaintFinishes();
    ValidateVehicleCabin();
    ValidateSparkles();
    ValidateHmiRenderGraph();
    ValidateSoftwarePresentation(ArchiveDirectory);

    ValidatePhotometry();
    ValidateLightClusterSpace();
    ValidateEmissivePartition();
    ValidateEmissiveLightSpace();
    ValidateNearestDepthSpace();
    ValidatePlumeSpace();
    ValidatePlumeSourceProjection();
    ValidateSmokeSequence();
    ValidateSparkCollision();
    ValidateSparkSequence();
    ValidateEffectSequence();

    std::printf("Phase3Validation: %d checks, %d failures\n", CheckCount, FailureCount);
    return FailureCount == 0 ? 0 : 1;
}
