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
#include "Frontier/Engine/Rendering/HmiViewportUnits.h"
#include "Frontier/Engine/Scene/FacetArchive.h"
#include "Frontier/Engine/Scene/VehicleCabin.h"
#include "Frontier/Game/SoftwarePresentation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
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

    std::printf("Phase3Validation: %d checks, %d failures\n", CheckCount, FailureCount);
    return FailureCount == 0 ? 0 : 1;
}
