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
#include "Frontier/Engine/Rendering/PlumeSpace.h"
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

    std::printf("Phase3Validation: %d checks, %d failures\n", CheckCount, FailureCount);
    return FailureCount == 0 ? 0 : 1;
}
