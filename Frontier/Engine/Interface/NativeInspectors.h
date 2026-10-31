/*==========================================================================================================================================
                                                        NATIVEINSPECTORS.H
==========================================================================================================================================*/
// 🧩 Modular native Engine RecordInspector classes for Spatial, Physics, Thermal, Chemical, Surface, Environment, and Render domains.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/EngineConfigurations.h"

#include <cstddef>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSPECTION BUFFER MODES
//------------------------------------------------------------------------------------------------------------------------

struct RenderInspectionModes
{
    static constexpr size_t  ModeCount                    = 15;
    static const char* const ModeNames[ModeCount];
    static constexpr size_t  DistanceCount                = 3;
    static const char* const DistanceNames[DistanceCount];
};

#if FRONTIER_EDITOR

//------------------------------------------------------------------------------------------------------------------------
//                                              COMPOSABLE PROPERTY INSPECTORS
//------------------------------------------------------------------------------------------------------------------------

class SpatialInspector final : public RecordInspector
{
  public:
    explicit SpatialInspector(SpatialConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "SPATIAL · TRANSFORM"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "Position · Orientation · Scale"; }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      ConstructInspector() override;

  private:
    SpatialConfiguration& Parameters;
};

class PhysicsInspector final : public RecordInspector
{
  public:
    explicit PhysicsInspector(PhysicsConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "MECHANICS · MASS & DYNAMICS"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "MassConfiguration + Kinematics"; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    PhysicsConfiguration& Parameters;
};

class ThermalInspector final : public RecordInspector
{
  public:
    explicit ThermalInspector(ThermalConfiguration& InParameters, const MassConfiguration* InOptionalMass = nullptr) noexcept
        : Parameters(InParameters)
        , OptionalMass(InOptionalMass)
    {
    }

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "THERMODYNAMICS · HEAT TRANSFER"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "Conduction & Stefan-Boltzmann"; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    ThermalConfiguration&    Parameters;
    const MassConfiguration* OptionalMass = nullptr;
};

class ChemicalInspector final : public RecordInspector
{
  public:
    explicit ChemicalInspector(ChemicalConfiguration& InParameters, const ThermalConfiguration* InOptionalThermal = nullptr) noexcept
        : Parameters(InParameters)
        , OptionalThermal(InOptionalThermal)
    {
    }

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "CHEMISTRY · STOICHIOMETRY & KINETICS"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "Arrhenius Rate & Enthalpy"; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    ChemicalConfiguration&      Parameters;
    const ThermalConfiguration* OptionalThermal = nullptr;
};

class SurfaceOpticsInspector final : public RecordInspector
{
  public:
    explicit SurfaceOpticsInspector(SurfaceOpticsConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "OPTICS · SURFACE SCATTERING"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "BSDF Albedo & Microfacet"; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    SurfaceOpticsConfiguration& Parameters;
};

//------------------------------------------------------------------------------------------------------------------------
//                                               RENDER PIPELINE & DIAGNOSTICS INSPECTORS
//------------------------------------------------------------------------------------------------------------------------

class DiagnosticsInspector final : public RecordInspector
{
  public:
    explicit DiagnosticsInspector(DiagnosticsConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "DIAGNOSTICS · FRAME TELEMETRY"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "Frame budget"; }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    DiagnosticsConfiguration& Parameters;
};

class RenderPipelineInspector final : public RecordInspector
{
  public:
    explicit RenderPipelineInspector(RenderPipelineConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "RENDER · PIPELINE"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override
    {
        return Parameters.HardwareRayQueryTier ? "RTX · RayQuery + ReSTIR" : "GTX · 4-Cascade SDF + BVH";
    }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    RenderPipelineConfiguration& Parameters;
};

class DistanceFieldInspector final : public RecordInspector
{
  public:
    explicit DistanceFieldInspector(DistanceFieldConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "RENDER · DISTANCE FIELD"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "4-cascade clipmap"; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    DistanceFieldConfiguration& Parameters;
};

class SurfaceCardInspector final : public RecordInspector
{
  public:
    explicit SurfaceCardInspector(SurfaceCardConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "RENDER · SURFACE CARDS"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "Multi-bounce radiance"; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    SurfaceCardConfiguration& Parameters;
};

class PolygonClusterInspector final : public RecordInspector
{
  public:
    explicit PolygonClusterInspector(PolygonClusterConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "RENDER · POLYGON CLUSTERS"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "LOD DAG + Hi-Z"; }
    [[nodiscard]] const char* QueryEditorButtonLabel() const noexcept override { return "Cluster Editor"; }
    void                      ConstructEditor(RecordToken Token) override;
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    PolygonClusterConfiguration& Parameters;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENVIRONMENT INSPECTORS
//------------------------------------------------------------------------------------------------------------------------

class SolarInspector final : public RecordInspector
{
  public:
    explicit SolarInspector(SolarConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "ENVIRONMENT · SOLAR"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "Directional emitter + shafts"; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    SolarConfiguration& Parameters;
};

class OpticalFlareInspector final : public RecordInspector
{
  public:
    explicit OpticalFlareInspector(OpticalFlareConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "ENVIRONMENT · OPTICAL FLARE"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "Aperture diffraction"; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    OpticalFlareConfiguration& Parameters;
};

class AtmosphereInspector final : public RecordInspector
{
  public:
    explicit AtmosphereInspector(AtmosphereConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "ENVIRONMENT · ATMOSPHERE"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "Rayleigh + Mie + O3"; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    AtmosphereConfiguration& Parameters;
};

class CloudInspector final : public RecordInspector
{
  public:
    explicit CloudInspector(CloudConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override
    {
        return Parameters.LocalVolume ? "ENVIRONMENT · LOCAL CLOUD" : "ENVIRONMENT · CLOUDS";
    }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "3D Perlin-Worley raymarch"; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    CloudConfiguration& Parameters;
};

class StarInspector final : public RecordInspector
{
  public:
    explicit StarInspector(StarConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "ENVIRONMENT · STARS"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "Hipparcos J2000 catalogue"; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      ConstructInspector() override;

  private:
    StarConfiguration& Parameters;
};

class MoonInspector final : public RecordInspector
{
  public:
    explicit MoonInspector(MoonConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "ENVIRONMENT · MOONS"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "Dual-body orbital albedo"; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    MoonConfiguration& Parameters;
};

class FogInspector final : public RecordInspector
{
  public:
    explicit FogInspector(FogConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override
    {
        return Parameters.LocalVolume ? "ENVIRONMENT · LOCAL FOG" : "ENVIRONMENT · FOG";
    }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "Froxel Henyey-Greenstein"; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    FogConfiguration& Parameters;
};

class LightningInspector final : public RecordInspector
{
  public:
    explicit LightningInspector(LightningConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "ENVIRONMENT · LIGHTNING"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "Dialectric discharge"; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    LightningConfiguration& Parameters;
};

class WindInspector final : public RecordInspector
{
  public:
    explicit WindInspector(WindConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "ENVIRONMENT · WIND FIELD"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "3D curl-noise advection"; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    WindConfiguration& Parameters;
};

//------------------------------------------------------------------------------------------------------------------------
//                                               CONTINUUM & PARTICLE INSPECTORS
//------------------------------------------------------------------------------------------------------------------------

class FluidPlumeInspector final : public RecordInspector
{
  public:
    explicit FluidPlumeInspector(FluidPlumeConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "SOLVER · FLUID PLUME"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "128x64x128 Eulerian"; }
    [[nodiscard]] const char* QueryEditorButtonLabel() const noexcept override { return "Plume Editor"; }
    void                      ConstructEditor(RecordToken Token) override;
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    FluidPlumeConfiguration& Parameters;
};

class FractureInspector final : public RecordInspector
{
  public:
    explicit FractureInspector(FractureConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "SOLVER · VORONOI FRACTURE"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "Convex hull decomposition"; }
    [[nodiscard]] const char* QueryEditorButtonLabel() const noexcept override { return "Fracture Editor"; }
    void                      ConstructEditor(RecordToken Token) override;
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    FractureConfiguration& Parameters;
};

class ParticleInspector final : public RecordInspector
{
  public:
    explicit ParticleInspector(ParticleConfiguration& InParameters) noexcept : Parameters(InParameters) {}

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "SOLVER · GPU PARTICLES"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "Ballistic & precipitation"; }
    [[nodiscard]] const char* QueryEditorButtonLabel() const noexcept override { return "Particle Editor"; }
    void                      ConstructEditor(RecordToken Token) override;
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Parameters.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Parameters.Enabled = Enabled; Parameters.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Parameters.Revision; }
    void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) override;
    void                      ConstructInspector() override;

  private:
    ParticleConfiguration& Parameters;
};

#endif // FRONTIER_EDITOR

} // namespace Frontier
