/*==========================================================================================================================================
                                                       NATIVEINSPECTORS.CPP
==========================================================================================================================================*/
// 🧩 Modular native Engine RecordInspector implementations using generic DiagramControls and standalone <Domain>Editor classes.

#include "Engine/Interface/NativeInspectors.h"

#if FRONTIER_EDITOR

#include "Engine/Interface/DiagramControls.h"
#include "Engine/Interface/FluidPlumeEditor.h"
#include "Engine/Interface/FractureEditor.h"
#include "Engine/Interface/InspectorControls.h"
#include "Engine/Interface/ParticleEditor.h"
#include "Engine/Interface/PolygonClusterEditor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#endif

namespace Frontier
{

const char* const RenderInspectionModes::ModeNames[RenderInspectionModes::ModeCount] = {
    "Final Lit",
    "Albedo",
    "World Normal",
    "Roughness",
    "Metallic",
    "Ambient Occlusion",
    "Direct Sun",
    "Indirect GI",
    "Reflections",
    "Distance Field",
    "Surface Cards",
    "Polygon Clusters",
    "Motion Vectors",
    "Volumetric Scattering",
    "Thermal / Radiance",
};

const char* const RenderInspectionModes::DistanceNames[RenderInspectionModes::DistanceCount] = {
    "Near (32 m)",
    "Medium (160 m)",
    "Far (Full)",
};

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

//------------------------------------------------------------------------------------------------------------------------
//                                                    SPATIAL INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

void SpatialInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    InspectorControls::ConstructSectionDivider(Palette, Faces, "TRANSLATION", "Metres");
    bool Changed = false;
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PosX", "Position X", Parameters.PositionMetres[0], -200.0f, 200.0f, "%.2f m");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PosY", "Position Y", Parameters.PositionMetres[1], -50.0f, 200.0f, "%.2f m");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PosZ", "Position Z", Parameters.PositionMetres[2], -200.0f, 200.0f, "%.2f m");

    InspectorControls::ConstructSectionDivider(Palette, Faces, "ORIENTATION & SCALE");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##RotX", "Pitch", Parameters.OrientationDegrees[0], -180.0f, 180.0f, "%.1f deg");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##RotY", "Yaw", Parameters.OrientationDegrees[1], -180.0f, 180.0f, "%.1f deg");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##RotZ", "Roll", Parameters.OrientationDegrees[2], -180.0f, 180.0f, "%.1f deg");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##SclX", "Scale X", Parameters.Scale[0], 0.05f, 25.0f, "%.2f x");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##SclY", "Scale Y", Parameters.Scale[1], 0.05f, 25.0f, "%.2f x");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##SclZ", "Scale Z", Parameters.Scale[2], 0.05f, 25.0f, "%.2f x");
    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PHYSICS INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

void PhysicsInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Dynamic Body", "Rigid solver", &Parameters.DynamicBody});
    OutSwitches.push_back({"Gravity", "-9.81 m/s2", &Parameters.GravityCoupled});
}

void PhysicsInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    InspectorControls::ConstructSectionDivider(Palette, Faces, "MASS CONFIGURATION", "m = rho * V");
    bool Changed = false;
    if (InspectorControls::ConstructSliderRow(Palette, Faces, "##MassKg", "Mass", Parameters.Mass.MassKilograms, 0.01f, 5000.0f, "%.2f kg"))
    {
        Parameters.Mass.SyncVolumeFromMassAndDensity();
        Changed = true;
    }
    if (InspectorControls::ConstructSliderRow(Palette, Faces, "##Density", "Density", Parameters.Mass.DensityKgPerCubicMetre, 1.0f, 20000.0f, "%.0f kg/m3"))
    {
        Parameters.Mass.SyncMassFromDensityAndVolume();
        Changed = true;
    }
    if (InspectorControls::ConstructSliderRow(Palette, Faces, "##Volume", "Volume", Parameters.Mass.VolumeCubicMetres, 0.0001f, 25.0f, "%.4f m3"))
    {
        Parameters.Mass.SyncMassFromDensityAndVolume();
        Changed = true;
    }

    InspectorControls::ConstructSectionDivider(Palette, Faces, "KINEMATICS & CONTACT");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##VelX", "Linear Vel X", Parameters.LinearVelocityMps[0], -80.0f, 80.0f, "%.2f m/s");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##VelY", "Linear Vel Y", Parameters.LinearVelocityMps[1], -80.0f, 80.0f, "%.2f m/s");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##VelZ", "Linear Vel Z", Parameters.LinearVelocityMps[2], -80.0f, 80.0f, "%.2f m/s");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Restit", "Restitution", Parameters.Restitution, 0.0f, 1.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##StatFric", "Static Friction", Parameters.StaticFriction, 0.0f, 1.5f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##DynFric", "Dynamic Friction", Parameters.DynamicFriction, 0.0f, 1.5f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##LinDamp", "Linear Damping", Parameters.LinearDamping, 0.0f, 2.0f, "%.2f");

    const float Speed    = Parameters.ComputeLinearSpeedMps();
    const float KineticJ = Parameters.Mass.ComputeKineticEnergyJoules(Speed);
    const float Momentum = Parameters.Mass.ComputeLinearMomentumNs(Speed);
    char        KineticBuf[48]{};
    char        MomentumBuf[48]{};
    std::snprintf(KineticBuf, sizeof(KineticBuf), "%.2f J", KineticJ);
    std::snprintf(MomentumBuf, sizeof(MomentumBuf), "%.2f N*s", Momentum);
    const std::array<InspectorReadoutCell, 2> Cells = {{
        {"KINETIC ENERGY", KineticBuf},
        {"MOMENTUM", MomentumBuf},
    }};
    InspectorControls::ConstructReadoutStrip(Palette, Faces, Cells.data(), Cells.size());

    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THERMAL INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

void ThermalInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Conduction", "Fourier flux", &Parameters.ConductiveExchange});
    OutSwitches.push_back({"Radiation", "Stefan-Boltzmann", &Parameters.RadiativeExchange});
}

void ThermalInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    const std::array<ImU32, 5> ThermalStops = {{
        ParseHexSpectrum("#3b76c4", Palette.ThemeAccent),
        ParseHexSpectrum("#4fa3a5", Palette.LiveGreen),
        ParseHexSpectrum("#e2a23b", Palette.WarmAmber),
        ParseHexSpectrum("#e85d2a", Palette.WarmAmber),
        ParseHexSpectrum("#fff2cc", Palette.TextPrimary),
    }};
    const float MarkerFraction = std::clamp((Parameters.TemperatureKelvin - 150.0f) / (2500.0f - 150.0f), 0.0f, 1.0f);
    char        HeaderLabel[64]{};
    std::snprintf(HeaderLabel, sizeof(HeaderLabel), "THERMAL STATE · %.1f K (%.1f C)",
                  Parameters.TemperatureKelvin, Parameters.QueryCelsius());
    DiagramControls::ConstructSpectrumBar(Palette, Faces, HeaderLabel,
                                          ThermalStops.data(), ThermalStops.size(), MarkerFraction);

    bool Changed = false;
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##TempK", "Temperature", Parameters.TemperatureKelvin, 150.0f, 2500.0f, "%.1f K");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##SpecHeat", "Specific Heat cp", Parameters.SpecificHeatJPerKgK, 100.0f, 4500.0f, "%.0f J/kgK");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Conduct", "Conductivity k", Parameters.ThermalConductivityWPerMK, 0.02f, 450.0f, "%.2f W/mK");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Emissiv", "Emissivity", Parameters.SurfaceEmissivity, 0.01f, 1.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Convect", "Convection h", Parameters.ConvectionCoefficientWPerM2K, 0.5f, 250.0f, "%.1f W/m2K");

    char FluxBuf[48]{};
    char EnthalpyBuf[48]{};
    std::snprintf(FluxBuf, sizeof(FluxBuf), "%.1f W/m2", Parameters.ComputeRadiativeFluxWPerM2());
    const float MassKg = OptionalMass ? OptionalMass->MassKilograms : 1.0f;
    std::snprintf(EnthalpyBuf, sizeof(EnthalpyBuf), "%.2f kJ", Parameters.ComputeSensibleHeatJoules(MassKg) * 1.0e-3f);
    const std::array<InspectorReadoutCell, 2> Cells = {{
        {"RADIATIVE FLUX", FluxBuf},
        {"SENSIBLE HEAT", EnthalpyBuf},
    }};
    InspectorControls::ConstructReadoutStrip(Palette, Faces, Cells.data(), Cells.size());

    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CHEMICAL INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

void ChemicalInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Reversible", "Dynamic eq.", &Parameters.ReversibleEquilibrium});
    OutSwitches.push_back({"Catalyzed", "-35% Ea", &Parameters.CatalyzedPathway});
}

void ChemicalInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    const float ReferenceKelvin = OptionalThermal ? OptionalThermal->TemperatureKelvin : 298.15f;
    std::array<float, 32> ArrheniusSamples{};
    const float MaxRate = std::max(1.0e-6f, Parameters.ComputeArrheniusRateConstant(900.0f));
    for (size_t Index = 0; Index < ArrheniusSamples.size(); ++Index)
    {
        const float SampleK     = 250.0f + (static_cast<float>(Index) / 31.0f) * 650.0f;
        const float NormalizedK = Parameters.ComputeArrheniusRateConstant(SampleK) / MaxRate;
        ArrheniusSamples[Index] = std::pow(std::clamp(NormalizedK, 0.0f, 1.0f), 0.35f);
    }
    char RateHeader[80]{};
    std::snprintf(RateHeader, sizeof(RateHeader), "ARRHENIUS KINETICS · k(%.0fK)=%.2e /s",
                  ReferenceKelvin, Parameters.ComputeArrheniusRateConstant(ReferenceKelvin));
    DiagramControls::ConstructCurvePlot(Palette, Faces, RateHeader,
                                        ArrheniusSamples.data(), ArrheniusSamples.size(),
                                        ParseHexSpectrum("#62c3a5", Palette.LiveGreen));

    static const char* const PhaseLabels[4] = {"Solid", "Liquid", "Gas", "Plasma"};
    bool Changed = false;
    Changed |= InspectorControls::ConstructSegmentedRow(Palette, Faces, "##MatterPhase", "Phase", Parameters.MatterPhaseIndex, PhaseLabels, 4);
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##MolMass", "Molar Mass", Parameters.MolarMassGramsPerMole, 1.0f, 500.0f, "%.2f g/mol");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Moles", "Amount", Parameters.AmountMoles, 0.01f, 200.0f, "%.2f mol");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Conc", "Concentration", Parameters.ConcentrationMolPerLitre, 0.001f, 20.0f, "%.3f mol/L");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##ActEa", "Activation Ea", Parameters.ActivationEnergyKjPerMol, 2.0f, 250.0f, "%.1f kJ/mol");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Enthalpy", "Reaction dH", Parameters.EnthalpyDeltaKjPerMol, -500.0f, 500.0f, "%.1f kJ/mol");

    char MassBuf[48]{};
    char HeatBuf[48]{};
    std::snprintf(MassBuf, sizeof(MassBuf), "%.4f kg", Parameters.ComputeSubstanceMassKilograms());
    std::snprintf(HeatBuf, sizeof(HeatBuf), "%.2f kJ", Parameters.ComputeReactionHeatKilojoules());
    const std::array<InspectorReadoutCell, 2> Cells = {{
        {"REAGENT MASS", MassBuf},
        {"REACTION HEAT", HeatBuf},
    }};
    InspectorControls::ConstructReadoutStrip(Palette, Faces, Cells.data(), Cells.size());

    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                SURFACE OPTICS INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

void SurfaceOpticsInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Shadows", "Direct ray/SDF", &Parameters.CastShadows});
    OutSwitches.push_back({"Indirect GI", "Bounce history", &Parameters.ReceiveGlobalIllumination});
}

void SurfaceOpticsInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    bool Changed = false;
    Changed |= InspectorControls::ConstructColourRow(Palette, Faces, "##AlbedoRgb", "Base Albedo", Parameters.AlbedoRgb);
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Rough", "Roughness", Parameters.Roughness, 0.02f, 1.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Metal", "Metallic", Parameters.Metallic, 0.0f, 1.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Ior", "Index of Refraction", Parameters.IndexOfRefraction, 1.0f, 2.6f, "%.2f");
    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           RENDER PIPELINE & DIAGNOSTICS INSPECTORS
//------------------------------------------------------------------------------------------------------------------------

void DiagnosticsInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Timing HUD", "GPU pass ms", &Parameters.TimingOverlayEnabled});
    OutSwitches.push_back({"Memory HUD", "VRAM residency", &Parameters.MemoryOverlayEnabled});
}

void DiagnosticsInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    char FpsHeader[80]{};
    std::snprintf(FpsHeader, sizeof(FpsHeader), "FRAME BUDGET · %.1f FPS (%.2f ms)",
                  Parameters.FramesPerSecond, Parameters.FrameTimeMs);
    DiagramControls::ConstructTimeSeriesPlot(Palette, Faces, FpsHeader,
                                             Parameters.FrameHistoryMs.data(), Parameters.FrameHistoryMs.size(),
                                             0.0f, 33.3f, 16.67f, Palette.LiveGreen);

    char RayBuf[32]{};
    char LightBuf[32]{};
    char VolBuf[32]{};
    char DrawBuf[32]{};
    std::snprintf(RayBuf, sizeof(RayBuf), "%.2f ms", Parameters.GpuRayQueryMs);
    std::snprintf(LightBuf, sizeof(LightBuf), "%.2f ms", Parameters.GpuLightingMs);
    std::snprintf(VolBuf, sizeof(VolBuf), "%.2f ms", Parameters.GpuVolumetricMs);
    std::snprintf(DrawBuf, sizeof(DrawBuf), "%u", Parameters.ActiveDrawCount);

    const std::array<InspectorReadoutCell, 4> Cells = {{
        {"RAY / SDF", RayBuf},
        {"LIGHTING", LightBuf},
        {"VOLUMETRICS", VolBuf},
        {"CLUSTERS", DrawBuf},
    }};
    InspectorControls::ConstructReadoutStrip(Palette, Faces, Cells.data(), Cells.size());
}

void RenderPipelineInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"RTX Hardware", "RayQuery+ReSTIR", &Parameters.HardwareRayQueryTier});
    OutSwitches.push_back({"Indirect GI", "Multi-bounce", &Parameters.GlobalIllumination});
    OutSwitches.push_back({"Temporal AA", "Subpixel jitter", &Parameters.TemporalAntiAliasing});
    OutSwitches.push_back({"Reflections", "BVH / RayQuery", &Parameters.ScreenSpaceReflections});
}

void RenderPipelineInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    static const char* const TierLabels[2]    = {"GTX · SDF+BVH", "RTX · RayQuery"};
    static const char* const QualityLabels[3] = {"Low", "Medium", "Ultra"};

    int  TierIndex = Parameters.HardwareRayQueryTier ? 1 : 0;
    bool Changed   = false;
    InspectorControls::ConstructSectionDivider(Palette, Faces, "DUAL-TIER GPU ARCHITECTURE");
    if (InspectorControls::ConstructSegmentedRow(Palette, Faces, "##GpuTier", "GPU Path", TierIndex, TierLabels, 2))
    {
        Parameters.HardwareRayQueryTier = (TierIndex == 1);
        Changed                         = true;
    }
    Changed |= InspectorControls::ConstructSegmentedRow(Palette, Faces, "##QualityTier", "Quality", Parameters.QualityTier, QualityLabels, 3);

    InspectorControls::ConstructSectionDivider(Palette, Faces, "BUFFER INSPECTION & EXPOSURE");
    uint32_t ModeIdx = static_cast<uint32_t>(std::clamp(Parameters.InspectionModeIndex, 0, 14));
    if (InspectorControls::ConstructIntegerSliderRow(Palette, Faces, "##InspMode", "Buffer Index", ModeIdx, 0u, 14u))
    {
        Parameters.InspectionModeIndex = static_cast<int>(ModeIdx);
        Changed                        = true;
    }
    const InspectorReadoutCell BufferCell{"ACTIVE BUFFER", RenderInspectionModes::ModeNames[std::clamp(Parameters.InspectionModeIndex, 0, 14)]};
    InspectorControls::ConstructReadoutStrip(Palette, Faces, &BufferCell, 1);

    Changed |= InspectorControls::ConstructSegmentedRow(Palette, Faces, "##InspDist", "Probe Range",
                                                        Parameters.InspectionDistanceMode,
                                                        RenderInspectionModes::DistanceNames,
                                                        static_cast<int>(RenderInspectionModes::DistanceCount));
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##ExposureEv", "Exposure EV", Parameters.ExposureCompensationEv, -4.0f, 4.0f, "%+.2f EV");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##BloomInt", "Bloom Intensity", Parameters.BloomIntensity, 0.0f, 1.5f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##RestirHist", "ReSTIR M-Cap", Parameters.RestirTemporalHistory, 1.0f, 32.0f, "%.0fx");
    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

void DistanceFieldInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Analytical SDF", "Exact primitives", &Parameters.AnalyticalPrimitives});
    OutSwitches.push_back({"Cascade Blend", "Trilinear seam", &Parameters.TrilinearCascadeBlend});
}

void DistanceFieldInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    static const char* const CascadeTiers[4] = {
        "C3 Horizon Clipmap",
        "C2 Far Clipmap",
        "C1 Mid Clipmap",
        "C0 Near Clipmap",
    };
    DiagramControls::ConstructStrataPlot(Palette, Faces,
                                         ParseHexSpectrum("#1f3a52", Palette.InsetFieldBg),
                                         ParseHexSpectrum("#2f6f62", Palette.LiveGreen),
                                         CascadeTiers, 4);

    bool Changed = false;
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##C0Rad", "Cascade 0 Radius", Parameters.CascadeZeroRadiusMetres, 4.0f, 32.0f, "%.0f m");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##C1Rad", "Cascade 1 Radius", Parameters.CascadeOneRadiusMetres, 16.0f, 96.0f, "%.0f m");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##C2Rad", "Cascade 2 Radius", Parameters.CascadeTwoRadiusMetres, 64.0f, 256.0f, "%.0f m");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##C3Rad", "Cascade 3 Radius", Parameters.CascadeThreeRadiusMetres, 128.0f, 640.0f, "%.0f m");
    Changed |= InspectorControls::ConstructIntegerSliderRow(Palette, Faces, "##ConeSteps", "Max Cone Steps", Parameters.MaxConeSteps, 8u, 128u);
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##ConeScale", "Step Scale", Parameters.ConeTraceStepScale, 0.25f, 2.0f, "%.2f");
    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

void SurfaceCardInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Temporal History", "Multi-frame", &Parameters.TemporalAccumulate});
    OutSwitches.push_back({"SH Probes", "Sky irradiance", &Parameters.IrradianceProbes});
}

void SurfaceCardInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    bool Changed = false;
    Changed |= InspectorControls::ConstructIntegerSliderRow(Palette, Faces, "##CardAtlas", "Atlas Resolution", Parameters.AtlasResolutionPx, 256u, 4096u, " px");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##CardBounce", "Indirect Bounce Gain", Parameters.IndirectBounceGain, 0.0f, 3.0f, "%.2f");
    Changed |= InspectorControls::ConstructIntegerSliderRow(Palette, Faces, "##CardSides", "Projections / Object", Parameters.CardsPerObject, 2u, 6u);
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##SkyVis", "Sky Visibility Weight", Parameters.SkyVisibilityWeight, 0.0f, 2.0f, "%.2f");
    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

void PolygonClusterInspector::ConstructEditor(RecordToken Token)
{
    PolygonClusterEditor::ConstructInterface(Token, *this);
}

void PolygonClusterInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Hi-Z Occlusion", "Pyramid cull", &Parameters.HiZOcclusionCulling});
    OutSwitches.push_back({"Freeze Frustum", "Lock cut", &Parameters.FreezeCullingFrustum});
}

void PolygonClusterInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    char VisBuf[32]{};
    char CullBuf[32]{};
    std::snprintf(VisBuf, sizeof(VisBuf), "%u", Parameters.VisibleClusterCount);
    std::snprintf(CullBuf, sizeof(CullBuf), "%u", Parameters.CulledClusterCount);
    const std::array<InspectorReadoutCell, 2> Cells = {{
        {"VISIBLE CLUSTERS", VisBuf},
        {"HI-Z CULLED", CullBuf},
    }};
    InspectorControls::ConstructReadoutStrip(Palette, Faces, Cells.data(), Cells.size());

    bool Changed = false;
    Changed |= InspectorControls::ConstructIntegerSliderRow(Palette, Faces, "##DagDepth", "Hierarchy Depth", Parameters.HierarchyDepth, 2u, 12u);
    Changed |= InspectorControls::ConstructIntegerSliderRow(Palette, Faces, "##MaxTris", "Triangles / Cluster", Parameters.MaxClusterTriangles, 32u, 256u);
    Changed |= InspectorControls::ConstructIntegerSliderRow(Palette, Faces, "##LodErr", "Error Threshold", Parameters.LodErrorThresholdPx, 1u, 8u, " px");
    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENVIRONMENT INSPECTORS
//------------------------------------------------------------------------------------------------------------------------

void SolarInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Ray Shadows", "Soft penumbra", &Parameters.CastRaytracedShadow});
    OutSwitches.push_back({"Light Shafts", "Froxel shafts", &Parameters.VolumetricLightShaft});
}

void SolarInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    char ArcHeader[64]{};
    std::snprintf(ArcHeader, sizeof(ArcHeader), "HEMISPHERE ELEVATION · %.1f deg", Parameters.ElevationDegrees);
    const float AzimuthRatio   = std::clamp(Parameters.AzimuthDegrees / 360.0f, 0.0f, 1.0f);
    const float ElevationRatio = std::clamp((Parameters.ElevationDegrees + 15.0f) / 105.0f, 0.0f, 1.0f);
    DiagramControls::ConstructArcPlot(Palette, Faces, ArcHeader, "HORIZON PLANE",
                                      AzimuthRatio, ElevationRatio,
                                      ParseHexSpectrum("#f2b84b", Palette.WarmAmber));

    bool Changed = false;
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##SunEl", "Elevation", Parameters.ElevationDegrees, -15.0f, 90.0f, "%.1f deg");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##SunAz", "Azimuth", Parameters.AzimuthDegrees, 0.0f, 360.0f, "%.0f deg");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##SunLux", "Illuminance", Parameters.IlluminanceKilolux, 0.0f, 130.0f, "%.1f klx");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##SunAng", "Angular Disc", Parameters.AngularDiameterDeg, 0.2f, 2.5f, "%.2f deg");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##SunKelvin", "Blackbody Temp", Parameters.KelvinTemperature, 2200.0f, 9000.0f, "%.0f K");
    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

void OpticalFlareInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Anamorphic", "Horizontal streak", &Parameters.AnamorphicStreak});
    OutSwitches.push_back({"Starburst", "Blade spikes", &Parameters.DiffractionStarburst});
}

void OpticalFlareInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    char BladeLabel[48]{};
    std::snprintf(BladeLabel, sizeof(BladeLabel), "%u-BLADE APERTURE IRIS", Parameters.ApertureBladeCount);
    DiagramControls::ConstructPolygonDiagram(Palette, Faces, BladeLabel,
                                             Parameters.ApertureBladeCount,
                                             std::clamp(Parameters.HaloRadius, 0.15f, 0.95f),
                                             Parameters.ChromaticDispersion,
                                             ParseHexSpectrum("#f0a954", Palette.WarmAmber));

    bool Changed = false;
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##FlareInt", "Intensity", Parameters.Intensity, 0.0f, 2.5f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##FlareThr", "Threshold", Parameters.LuminanceThreshold, 0.5f, 6.0f, "%.2f");
    Changed |= InspectorControls::ConstructIntegerSliderRow(Palette, Faces, "##FlareBlades", "Aperture Blades", Parameters.ApertureBladeCount, 3u, 12u);
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##FlareHalo", "Halo Radius", Parameters.HaloRadius, 0.05f, 1.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##FlareDisp", "Chromatic Dispersion", Parameters.ChromaticDispersion, 0.0f, 1.0f, "%.2f");
    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

void AtmosphereInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Multi-Scatter", "Precomputed LUT", &Parameters.MultiScatteringLut});
    OutSwitches.push_back({"Aerial Fog", "Camera volume", &Parameters.AerialPerspective});
}

void AtmosphereInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    static const char* const StrataTiers[3] = {
        "Ozone Absorption (18-50 km)",
        "Rayleigh Molecular (3-18 km)",
        "Mie Aerosol Boundary (0-3 km)",
    };
    DiagramControls::ConstructStrataPlot(Palette, Faces,
                                         ParseHexSpectrum("#2a4880", Palette.ThemeAccent),
                                         ParseHexSpectrum("#8d6b48", Palette.WarmAmber),
                                         StrataTiers, 3);

    bool Changed = false;
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Rayleigh", "Rayleigh Scale", Parameters.RayleighScattering, 0.0f, 4.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##MieTurb", "Mie Turbidity", Parameters.MieTurbidity, 1.0f, 10.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##MieG", "Mie Anisotropy g", Parameters.MieAnisotropyG, 0.0f, 0.98f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Ozone", "Ozone Absorption", Parameters.OzoneAbsorptionScale, 0.0f, 3.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##AtmoThick", "Atmosphere Height", Parameters.AtmosphereThicknessKm, 20.0f, 120.0f, "%.0f km");
    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

void CloudInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Local Volume", "Bounded box", &Parameters.LocalVolume});
    OutSwitches.push_back({"Temporal Reproj", "Quarter-res", &Parameters.TemporalReprojection});
    OutSwitches.push_back({"Ground Shadows", "Transmittance", &Parameters.CastGroundShadows});
}

void CloudInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    static const char* const CloudTiers[3] = {
        "Anvil Top Envelope",
        "Cumulus Perlin-Worley Deck",
        "Sub-Cloud Clear Air",
    };
    DiagramControls::ConstructStrataPlot(Palette, Faces,
                                         ParseHexSpectrum("#4f6e96", Palette.ThemeAccent),
                                         ParseHexSpectrum("#232c38", Palette.InsetFieldBg),
                                         CloudTiers, 3);

    bool Changed = false;
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##CldCov", "Coverage", Parameters.Coverage, 0.0f, 1.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##CldExt", "Extinction Density", Parameters.ExtinctionDensity, 0.05f, 3.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##CldBase", "Base Altitude", Parameters.BaseAltitudeMetres, 200.0f, 5000.0f, "%.0f m");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##CldThick", "Layer Thickness", Parameters.LayerThicknessMetres, 200.0f, 4000.0f, "%.0f m");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##CldWind", "Drift Speed", Parameters.WindDriftSpeed, 0.0f, 45.0f, "%.1f m/s");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##CldSilv", "Silver Lining", Parameters.SilverLiningPower, 0.0f, 2.0f, "%.2f");
    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

void StarInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    bool Changed = false;
    Changed |= InspectorControls::ConstructIntegerSliderRow(Palette, Faces, "##StarCnt", "Hipparcos Count", Parameters.HipparcosCount, 500u, 9110u);
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##StarGain", "Magnitude Gain", Parameters.MagnitudeGain, 0.1f, 3.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##MilkyWay", "Galactic Band", Parameters.MilkyWayLuminance, 0.0f, 2.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Sidereal", "Sidereal Rate", Parameters.SiderealSpeed, 0.0f, 20.0f, "%.2f x");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##Scint", "Scintillation", Parameters.AtmosphericScintillate, 0.0f, 1.0f, "%.2f");
    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

void MoonInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Secondary Body", "Companion orbit", &Parameters.SecondaryMoonEnabled});
}

void MoonInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    static constexpr std::array<float, 8> PhaseSamples = {{0.0f, 0.22f, 0.50f, 0.78f, 1.0f, 0.78f, 0.50f, 0.22f}};
    const int ActiveDisc = std::clamp(static_cast<int>(std::lround(Parameters.PrimaryPhase * 7.0f)), 0, 7);
    DiagramControls::ConstructDiscSequence(Palette, Faces, "ORBITAL ILLUMINATION SEQUENCE",
                                           PhaseSamples.data(), PhaseSamples.size(),
                                           ActiveDisc, ParseHexSpectrum("#dce6f7", Palette.TextPrimary));

    bool Changed = false;
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##MoonPhase", "Primary Phase", Parameters.PrimaryPhase, 0.0f, 1.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##MoonOrb", "Primary Orbit", Parameters.PrimaryOrbitDegrees, 0.0f, 360.0f, "%.0f deg");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##MoonAlb", "Surface Albedo", Parameters.PrimaryAlbedo, 0.02f, 0.60f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##EarthSh", "Planetshine", Parameters.EarthshineLuminance, 0.0f, 1.0f, "%.2f");
    if (Parameters.SecondaryMoonEnabled)
    {
        Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##MoonSecOrb", "Secondary Orbit", Parameters.SecondaryOrbitDegrees, 0.0f, 360.0f, "%.0f deg");
    }
    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

void FogInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Local Volume", "Bounded box", &Parameters.LocalVolume});
    OutSwitches.push_back({"Froxel Grid", "Camera frustum", &Parameters.VolumetricFroxelGrid});
    OutSwitches.push_back({"Ray Shadows", "Sun shafts", &Parameters.ReceiveRayShadow});
}

void FogInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    std::array<float, 32> ExtinctionSamples{};
    for (size_t Index = 0; Index < ExtinctionSamples.size(); ++Index)
    {
        const float HeightMetres = static_cast<float>(Index) * 4.0f;
        ExtinctionSamples[Index] = std::exp(-Parameters.HeightFalloff * HeightMetres * 0.25f) *
                                   std::clamp(Parameters.BaseDensity * 35.0f, 0.05f, 1.0f);
    }
    char FogHeader[64]{};
    std::snprintf(FogHeader, sizeof(FogHeader), "EXPONENTIAL EXTINCTION · sigma=%.3f", Parameters.BaseDensity);
    DiagramControls::ConstructCurvePlot(Palette, Faces, FogHeader,
                                        ExtinctionSamples.data(), ExtinctionSamples.size(),
                                        ParseHexSpectrum("#7db8d6", Palette.ThemeAccent));

    bool Changed = false;
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##FogDens", "Base Density", Parameters.BaseDensity, 0.0f, 0.12f, "%.4f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##FogFall", "Height Falloff", Parameters.HeightFalloff, 0.01f, 1.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##FogAniso", "Phase Anisotropy", Parameters.PhaseAnisotropy, -0.8f, 0.95f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##FogStart", "Start Distance", Parameters.StartDistanceMetres, 0.0f, 100.0f, "%.1f m");
    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

void LightningInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Auto Discharge", "Stochastic", &Parameters.AutoStormDischarge});
    OutSwitches.push_back({"Shockwave", "Delayed impulse", &Parameters.AcousticShockwave});
}

void LightningInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    bool Changed = false;
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##LgtRate", "Strikes / Minute", Parameters.StrikesPerMinute, 0.0f, 30.0f, "%.1f /min");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##LgtBrn", "Branching Factor", Parameters.BranchingFactor, 0.0f, 1.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##LgtAmp", "Peak Current", Parameters.PeakCurrentKiloamps, 5.0f, 120.0f, "%.0f kA");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##LgtSheet", "Sheet Luminance", Parameters.CloudSheetLuminance, 0.0f, 10.0f, "%.2f");
    if (InspectorControls::ConstructActionButton(Palette, Faces, "##TriggerStrike", "Trigger Discharge"))
    {
        ++Parameters.TriggeredStrikeCount;
        Changed = true;
    }
    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

void WindInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Advect Clouds", "Sky deck", &Parameters.AdvectClouds});
    OutSwitches.push_back({"Advect Plumes", "Eulerian grid", &Parameters.AdvectPlumes});
    OutSwitches.push_back({"Advect Particles", "GPU drag", &Parameters.AdvectParticles});
}

void WindInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    char WindNote[64]{};
    std::snprintf(WindNote, sizeof(WindNote), "ADVECTION %.0f deg · %.1f m/s",
                  Parameters.DirectionDegrees, Parameters.SpeedMetresPerSecond);
    const float Magnitude = std::clamp(Parameters.SpeedMetresPerSecond / 35.0f, 0.08f, 1.0f);
    bool        Changed   = false;
    Changed |= DiagramControls::ConstructPolarDial(Palette, Faces, "##WindPolarDial",
                                                   WindNote, Parameters.DirectionDegrees, Magnitude,
                                                   ParseHexSpectrum("#62c3a5", Palette.LiveGreen));

    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##WndDir", "Heading", Parameters.DirectionDegrees, 0.0f, 360.0f, "%.0f deg");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##WndSpd", "Mean Speed", Parameters.SpeedMetresPerSecond, 0.0f, 35.0f, "%.1f m/s");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##WndGust", "Gust Amplitude", Parameters.GustAmplitude, 0.0f, 20.0f, "%.1f m/s");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##WndCurl", "Curl Turbulence", Parameters.CurlNoiseTurbulence, 0.0f, 1.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##WndFreq", "Curl Frequency", Parameters.CurlFrequencyHz, 0.02f, 2.0f, "%.2f Hz");
    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           CONTINUUM & PARTICLE SOLVER INSPECTORS
//------------------------------------------------------------------------------------------------------------------------

void FluidPlumeInspector::ConstructEditor(RecordToken Token)
{
    FluidPlumeEditor::ConstructInterface(Token, *this);
}

void FluidPlumeInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Combustion", "Blackbody core", &Parameters.CombustionEnabled});
    OutSwitches.push_back({"Self-Shadow", "Beer-Lambert", &Parameters.SelfShadowing});
}

void FluidPlumeInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    const std::array<ImU32, 5> BlackbodyStops = {{
        ParseHexSpectrum("#2a120a", Palette.InsetFieldBg),
        ParseHexSpectrum("#8f2412", Palette.WarmAmber),
        ParseHexSpectrum("#e86820", Palette.WarmAmber),
        ParseHexSpectrum("#ffbe44", Palette.WarmAmber),
        ParseHexSpectrum("#fff7df", Palette.TextPrimary),
    }};
    const float CoreFraction = std::clamp((Parameters.BlackbodyCoreKelvin - 800.0f) / (3200.0f - 800.0f), 0.0f, 1.0f);
    char        CoreHeader[64]{};
    std::snprintf(CoreHeader, sizeof(CoreHeader), "BLACKBODY EMISSION · %.0f K", Parameters.BlackbodyCoreKelvin);
    DiagramControls::ConstructSpectrumBar(Palette, Faces, CoreHeader,
                                          BlackbodyStops.data(), BlackbodyStops.size(), CoreFraction);

    bool Changed = false;
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PlmBuoy", "Buoyancy Lift", Parameters.BuoyancyLift, 0.0f, 6.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PlmVort", "Vorticity Confinement", Parameters.VorticityConfinement, 0.0f, 5.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PlmDiss", "Dissipation", Parameters.DissipationRate, 0.01f, 1.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PlmFuel", "Fuel Injection", Parameters.FuelInjectionRate, 0.0f, 4.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PlmKelvin", "Blackbody Core", Parameters.BlackbodyCoreKelvin, 800.0f, 3200.0f, "%.0f K");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PlmSoot", "Soot Extinction", Parameters.SootExtinction, 0.0f, 4.0f, "%.2f");
    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

void FractureInspector::ConstructEditor(RecordToken Token)
{
    FractureEditor::ConstructInterface(Token, *this);
}

void FractureInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Armed", "Impulse trigger", &Parameters.FractureArmed});
    OutSwitches.push_back({"Impact Dust", "Debris burst", &Parameters.ImpactDustBurst});
}

void FractureInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    bool Changed = false;
    Changed |= InspectorControls::ConstructIntegerSliderRow(Palette, Faces, "##VorCells", "Voronoi Cell Count", Parameters.VoronoiCellCount, 4u, 256u);
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##ImpThr", "Impulse Threshold", Parameters.ImpulseThresholdNs, 1.0f, 500.0f, "%.1f N*s");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##RadBias", "Impact Radial Bias", Parameters.ImpactRadialBias, 0.0f, 1.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##DebrisLife", "Debris Lifetime", Parameters.DebrisLifetimeSeconds, 1.0f, 60.0f, "%.1f s");
    if (InspectorControls::ConstructActionButton(Palette, Faces, "##PreFracture", "Recompute Convex Cells"))
    {
        ++Parameters.PrefractureTriggerCount;
        Changed = true;
    }
    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

void ParticleInspector::ConstructEditor(RecordToken Token)
{
    ParticleEditor::ConstructInterface(Token, *this);
}

void ParticleInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches)
{
    OutSwitches.push_back({"Precipitation", "Weather volume", &Parameters.GpuWeatherPrecipitation});
    OutSwitches.push_back({"Collisions", "Depth buffer", &Parameters.DepthBufferCollisions});
}

void ParticleInspector::ConstructInspector()
{
    const InspectorPalette Palette = InspectorPalette::Resolve();
    const EditorTypefaces  Faces   = EditorTypefaces::Resolve();

    bool Changed = false;
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PrtSpk", "Emission Rate", Parameters.SparkRatePerSecond, 0.0f, 2000.0f, "%.0f /s");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PrtRain", "Rain Density", Parameters.RainDropDensity, 0.0f, 1.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PrtSnow", "Snow Density", Parameters.SnowFlakeDensity, 0.0f, 1.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PrtGrav", "Gravity Scale", Parameters.GravityMultiplier, -2.0f, 3.0f, "%.2f g");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PrtDrag", "Aerodynamic Drag", Parameters.AerodynamicDrag, 0.0f, 2.0f, "%.2f");
    Changed |= InspectorControls::ConstructSliderRow(Palette, Faces, "##PrtBnc", "Bounce Restitution", Parameters.BounceRestitution, 0.0f, 1.0f, "%.2f");
    if (Changed)
    {
        Parameters.AdvanceRevision();
    }
}

#elif FRONTIER_EDITOR

void SpatialInspector::ConstructInspector() {}
void PhysicsInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void PhysicsInspector::ConstructInspector() {}
void ThermalInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void ThermalInspector::ConstructInspector() {}
void ChemicalInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void ChemicalInspector::ConstructInspector() {}
void SurfaceOpticsInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void SurfaceOpticsInspector::ConstructInspector() {}
void DiagnosticsInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void DiagnosticsInspector::ConstructInspector() {}
void RenderPipelineInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void RenderPipelineInspector::ConstructInspector() {}
void DistanceFieldInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void DistanceFieldInspector::ConstructInspector() {}
void SurfaceCardInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void SurfaceCardInspector::ConstructInspector() {}
void PolygonClusterInspector::ConstructEditor(RecordToken) {}
void PolygonClusterInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void PolygonClusterInspector::ConstructInspector() {}
void SolarInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void SolarInspector::ConstructInspector() {}
void OpticalFlareInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void OpticalFlareInspector::ConstructInspector() {}
void AtmosphereInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void AtmosphereInspector::ConstructInspector() {}
void CloudInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void CloudInspector::ConstructInspector() {}
void StarInspector::ConstructInspector() {}
void MoonInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void MoonInspector::ConstructInspector() {}
void FogInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void FogInspector::ConstructInspector() {}
void LightningInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void LightningInspector::ConstructInspector() {}
void WindInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void WindInspector::ConstructInspector() {}
void FluidPlumeInspector::ConstructEditor(RecordToken) {}
void FluidPlumeInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void FluidPlumeInspector::ConstructInspector() {}
void FractureInspector::ConstructEditor(RecordToken) {}
void FractureInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void FractureInspector::ConstructInspector() {}
void ParticleInspector::ConstructEditor(RecordToken) {}
void ParticleInspector::EnumerateSwitches(std::vector<InspectorSwitchEntry>&) {}
void ParticleInspector::ConstructInspector() {}

#endif

} // namespace Frontier
