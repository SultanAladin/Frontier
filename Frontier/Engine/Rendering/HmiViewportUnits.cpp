/*==========================================================================================================================================
                                                     HMI VIEWPORT FRAME DEFINITION
==========================================================================================================================================*/

#include "HmiViewportUnits.h"

#include <string>
#include <unordered_map>

namespace Frontier
{

namespace
{

std::string PrefixHmiTarget(const std::string& BaseName)
{
    return "Hmi_" + BaseName;
}

SubmissionUnitDeclaration NamespaceHmiUnit(const SubmissionUnitDeclaration& Source)
{
    SubmissionUnitDeclaration Out;
    Out.Name = "Hmi_" + Source.Name;
    Out.Touches.reserve(Source.Touches.size());
    for (const TargetTouch& Touch : Source.Touches)
    {
        Out.Touches.push_back(TargetTouch{ PrefixHmiTarget(Touch.Target), Touch.Usage });
    }
    Out.After.reserve(Source.After.size());
    for (const std::string& Dep : Source.After)
    {
        Out.After.push_back("Hmi_" + Dep);
    }
    return Out;
}

void MergeHmiTargets(std::vector<RenderTargetDeclaration>& Into, const std::vector<RenderTargetDeclaration>& From)
{
    std::unordered_map<std::string, bool> Seen;
    for (const RenderTargetDeclaration& Existing : Into)
        Seen[Existing.Name] = true;

    for (const RenderTargetDeclaration& Candidate : From)
    {
        RenderTargetDeclaration Renamed = Candidate;
        Renamed.Name = PrefixHmiTarget(Candidate.Name);
        if (Seen.find(Renamed.Name) == Seen.end())
        {
            Into.push_back(Renamed);
            Seen[Renamed.Name] = true;
        }
    }
}

} // namespace

std::vector<uint8_t> ResolveSingleVehicleExclusionMask(
    const std::vector<HmiOccupantDescriptor>& Occupants,
    uint32_t HeroVehicleIdentifier)
{
    std::vector<uint8_t> Mask(Occupants.size(), 0u);
    for (size_t Index = 0; Index < Occupants.size(); ++Index)
    {
        const uint32_t Id = Occupants[Index].VehicleIdentifier;
        if (Id != 0u && Id != HeroVehicleIdentifier)
            Mask[Index] = 1u;
    }
    return Mask;
}

HmiPreviewFrameDeclaration DeclareHmiPreviewFrame(
    uint32_t Width,
    uint32_t Height,
    HmiPreviewFrameOptions Options)
{
    const IlluminationFrameDeclaration Illumination = DeclareIlluminationFrame(Width, Height);
    const EnvironmentFrameDeclaration  Environment  = DeclareEnvironmentFrame(Width, Height);

    HmiPreviewFrameDeclaration Frame;

    // 1. Single-vehicle G-buffer raster (with solid dissolve + 3D material transition ripple).
    Frame.Units.push_back(SubmissionUnitDeclaration{
        "Hmi_SurfaceRaster",
        {
            TargetTouch{ "Hmi_SurfaceAttributes", TargetUsage::ColourWrite },
            TargetTouch{ "Hmi_SurfaceDepth", TargetUsage::DepthWrite },
        },
        {},
    });

    // 2. Full ReSTIR GI + SVGF chain isolated to Hmi_* persistent reservoir & moment histories.
    for (const SubmissionUnitDeclaration& Unit : Illumination.Units)
        Frame.Units.push_back(NamespaceHmiUnit(Unit));

    // 3. Studio/sky environment radiance for the orbiting configurator camera.
    for (const SubmissionUnitDeclaration& Unit : Environment.Units)
        Frame.Units.push_back(NamespaceHmiUnit(Unit));

    // 4. Temporal lighting history feedback for Hmi_VarianceTemporal.
    Frame.Units.push_back(SubmissionUnitDeclaration{
        "Hmi_LightingHistoryUpdateSubmission",
        {
            TargetTouch{ "Hmi_FilteredLighting", TargetUsage::Sampled },
            TargetTouch{ "Hmi_LightingHistory", TargetUsage::StorageWrite },
        },
        {},
    });

    // 5. Recombine filtered ReSTIR GI + Environment into HmiPreviewTexture.
    Frame.Units.push_back(SubmissionUnitDeclaration{
        "Hmi_DisplayRecombination",
        {
            TargetTouch{ "Hmi_FilteredLighting", TargetUsage::Sampled },
            TargetTouch{ "Hmi_EnvironmentRadiance", TargetUsage::Sampled },
            TargetTouch{ "Hmi_SurfaceDepth", TargetUsage::DepthRead },
            TargetTouch{ "HmiPreviewTexture", TargetUsage::ColourWrite },
        },
        {},
    });

    // 6. Optional additive X-Ray Scan overlay (Ghost shell + 14°/28° Dihedral Wireframe + Curtains + 130 Sparkles).
    if (Options.IncludeScanOverlay)
    {
        Frame.Units.push_back(SubmissionUnitDeclaration{
            "Hmi_VehicleScanOverlay",
            {
                TargetTouch{ "Hmi_SurfaceDepth", TargetUsage::DepthRead },
                TargetTouch{ "HmiPreviewTexture", TargetUsage::ColourWrite },
            },
            { "Hmi_DisplayRecombination" },
        });
    }

    // 7. Transition HmiPreviewTexture to Sampled so the main cabin pass can bind it onto the dashboard tablet.
    Frame.Units.push_back(SubmissionUnitDeclaration{
        "Hmi_TabletTexturePublish",
        {
            TargetTouch{ "HmiPreviewTexture", TargetUsage::Sampled },
        },
        { Options.IncludeScanOverlay ? "Hmi_VehicleScanOverlay" : "Hmi_DisplayRecombination" },
    });

    MergeHmiTargets(Frame.Targets, Illumination.Targets);
    MergeHmiTargets(Frame.Targets, Environment.Targets);
    Frame.Targets.push_back(
        RenderTargetDeclaration{ "HmiPreviewTexture", TargetFormat::Rgba8Unorm, Width, Height, true, TargetUsage::Sampled });

    return Frame;
}

} // namespace Frontier
