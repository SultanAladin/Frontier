/*==========================================================================================================================================
                                                           SCENEWORLD.CPP
==========================================================================================================================================*/
// 🧩 Dynamic scene world implementation with .levelspace container serialization and composable record simulation.

#include "Engine/Scene/SceneWorld.h"

#include "Engine/Scene/SceneDirectory.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <unordered_map>

namespace Frontier
{

namespace
{

float ReadFloat(const toml::table& Table, std::string_view Key, float Fallback)
{
    if (const auto Value = Table[Key].value<double>())
    {
        return static_cast<float>(*Value);
    }
    if (const auto IntVal = Table[Key].value<int64_t>())
    {
        return static_cast<float>(*IntVal);
    }
    return Fallback;
}

uint32_t ReadUint32(const toml::table& Table, std::string_view Key, uint32_t Fallback)
{
    if (const auto IntVal = Table[Key].value<int64_t>())
    {
        return static_cast<uint32_t>(std::max<int64_t>(0, *IntVal));
    }
    return Fallback;
}

int ReadInt(const toml::table& Table, std::string_view Key, int Fallback)
{
    if (const auto IntVal = Table[Key].value<int64_t>())
    {
        return static_cast<int>(*IntVal);
    }
    return Fallback;
}

bool ReadBool(const toml::table& Table, std::string_view Key, bool Fallback)
{
    if (const auto BoolVal = Table[Key].value<bool>())
    {
        return *BoolVal;
    }
    return Fallback;
}

void WriteFloat3(toml::table& OutTable, std::string_view Key, const float Values[3])
{
    toml::array Array;
    Array.push_back(static_cast<double>(Values[0]));
    Array.push_back(static_cast<double>(Values[1]));
    Array.push_back(static_cast<double>(Values[2]));
    OutTable.insert_or_assign(Key, std::move(Array));
}

void ReadFloat3(const toml::table& InTable, std::string_view Key, float OutValues[3])
{
    if (const toml::array* Array = InTable[Key].as_array())
    {
        for (size_t Index = 0; Index < 3 && Index < Array->size(); ++Index)
        {
            if (const auto Val = (*Array)[Index].value<double>())
            {
                OutValues[Index] = static_cast<float>(*Val);
            }
            else if (const auto IntVal = (*Array)[Index].value<int64_t>())
            {
                OutValues[Index] = static_cast<float>(*IntVal);
            }
        }
    }
}

void SerializeSpatialSubtable(toml::table& OutTable, const SpatialConfiguration& Spatial)
{
    WriteFloat3(OutTable, "position_m", Spatial.PositionMetres);
    WriteFloat3(OutTable, "orientation_deg", Spatial.OrientationDegrees);
    WriteFloat3(OutTable, "scale", Spatial.Scale);
}

void DeserializeSpatialSubtable(const toml::table& InTable, SpatialConfiguration& Spatial)
{
    ReadFloat3(InTable, "position_m", Spatial.PositionMetres);
    ReadFloat3(InTable, "orientation_deg", Spatial.OrientationDegrees);
    ReadFloat3(InTable, "scale", Spatial.Scale);
    Spatial.AdvanceRevision();
}

void SerializePhysicsSubtable(toml::table& OutTable, const PhysicsConfiguration& Physics)
{
    OutTable.insert_or_assign("physics_enabled", Physics.Enabled);
    OutTable.insert_or_assign("mass_kg", static_cast<double>(Physics.Mass.MassKilograms));
    OutTable.insert_or_assign("density_kg_m3", static_cast<double>(Physics.Mass.DensityKgPerCubicMetre));
    OutTable.insert_or_assign("volume_m3", static_cast<double>(Physics.Mass.VolumeCubicMetres));
    WriteFloat3(OutTable, "linear_velocity_mps", Physics.LinearVelocityMps);
    OutTable.insert_or_assign("restitution", static_cast<double>(Physics.Restitution));
    OutTable.insert_or_assign("static_friction", static_cast<double>(Physics.StaticFriction));
    OutTable.insert_or_assign("dynamic_friction", static_cast<double>(Physics.DynamicFriction));
    OutTable.insert_or_assign("linear_damping", static_cast<double>(Physics.LinearDamping));
    OutTable.insert_or_assign("dynamic_body", Physics.DynamicBody);
    OutTable.insert_or_assign("gravity_coupled", Physics.GravityCoupled);
}

void DeserializePhysicsSubtable(const toml::table& InTable, PhysicsConfiguration& Physics)
{
    Physics.Enabled                     = ReadBool(InTable, "physics_enabled", Physics.Enabled);
    Physics.Mass.MassKilograms          = ReadFloat(InTable, "mass_kg", Physics.Mass.MassKilograms);
    Physics.Mass.DensityKgPerCubicMetre = ReadFloat(InTable, "density_kg_m3", Physics.Mass.DensityKgPerCubicMetre);
    Physics.Mass.VolumeCubicMetres      = ReadFloat(InTable, "volume_m3", Physics.Mass.VolumeCubicMetres);
    ReadFloat3(InTable, "linear_velocity_mps", Physics.LinearVelocityMps);
    Physics.Restitution     = ReadFloat(InTable, "restitution", Physics.Restitution);
    Physics.StaticFriction  = ReadFloat(InTable, "static_friction", Physics.StaticFriction);
    Physics.DynamicFriction = ReadFloat(InTable, "dynamic_friction", Physics.DynamicFriction);
    Physics.LinearDamping   = ReadFloat(InTable, "linear_damping", Physics.LinearDamping);
    Physics.DynamicBody     = ReadBool(InTable, "dynamic_body", Physics.DynamicBody);
    Physics.GravityCoupled  = ReadBool(InTable, "gravity_coupled", Physics.GravityCoupled);
    Physics.AdvanceRevision();
}

void SerializeThermalSubtable(toml::table& OutTable, const ThermalConfiguration& Thermal)
{
    OutTable.insert_or_assign("thermal_enabled", Thermal.Enabled);
    OutTable.insert_or_assign("temperature_k", static_cast<double>(Thermal.TemperatureKelvin));
    OutTable.insert_or_assign("specific_heat_j_kg_k", static_cast<double>(Thermal.SpecificHeatJPerKgK));
    OutTable.insert_or_assign("conductivity_w_m_k", static_cast<double>(Thermal.ThermalConductivityWPerMK));
    OutTable.insert_or_assign("emissivity", static_cast<double>(Thermal.SurfaceEmissivity));
    OutTable.insert_or_assign("convection_w_m2_k", static_cast<double>(Thermal.ConvectionCoefficientWPerM2K));
    OutTable.insert_or_assign("conductive_exchange", Thermal.ConductiveExchange);
    OutTable.insert_or_assign("radiative_exchange", Thermal.RadiativeExchange);
}

void DeserializeThermalSubtable(const toml::table& InTable, ThermalConfiguration& Thermal)
{
    Thermal.Enabled                      = ReadBool(InTable, "thermal_enabled", Thermal.Enabled);
    Thermal.TemperatureKelvin            = ReadFloat(InTable, "temperature_k", Thermal.TemperatureKelvin);
    Thermal.SpecificHeatJPerKgK          = ReadFloat(InTable, "specific_heat_j_kg_k", Thermal.SpecificHeatJPerKgK);
    Thermal.ThermalConductivityWPerMK    = ReadFloat(InTable, "conductivity_w_m_k", Thermal.ThermalConductivityWPerMK);
    Thermal.SurfaceEmissivity            = ReadFloat(InTable, "emissivity", Thermal.SurfaceEmissivity);
    Thermal.ConvectionCoefficientWPerM2K = ReadFloat(InTable, "convection_w_m2_k", Thermal.ConvectionCoefficientWPerM2K);
    Thermal.ConductiveExchange           = ReadBool(InTable, "conductive_exchange", Thermal.ConductiveExchange);
    Thermal.RadiativeExchange            = ReadBool(InTable, "radiative_exchange", Thermal.RadiativeExchange);
    Thermal.AdvanceRevision();
}

void SerializeChemicalSubtable(toml::table& OutTable, const ChemicalConfiguration& Chemical)
{
    OutTable.insert_or_assign("chemical_enabled", Chemical.Enabled);
    OutTable.insert_or_assign("molar_mass_g_mol", static_cast<double>(Chemical.MolarMassGramsPerMole));
    OutTable.insert_or_assign("amount_mol", static_cast<double>(Chemical.AmountMoles));
    OutTable.insert_or_assign("concentration_mol_l", static_cast<double>(Chemical.ConcentrationMolPerLitre));
    OutTable.insert_or_assign("activation_energy_kj_mol", static_cast<double>(Chemical.ActivationEnergyKjPerMol));
    OutTable.insert_or_assign("enthalpy_delta_kj_mol", static_cast<double>(Chemical.EnthalpyDeltaKjPerMol));
    OutTable.insert_or_assign("matter_phase", static_cast<int64_t>(Chemical.MatterPhaseIndex));
    OutTable.insert_or_assign("reversible", Chemical.ReversibleEquilibrium);
    OutTable.insert_or_assign("catalyzed", Chemical.CatalyzedPathway);
}

void DeserializeChemicalSubtable(const toml::table& InTable, ChemicalConfiguration& Chemical)
{
    Chemical.Enabled                  = ReadBool(InTable, "chemical_enabled", Chemical.Enabled);
    Chemical.MolarMassGramsPerMole    = ReadFloat(InTable, "molar_mass_g_mol", Chemical.MolarMassGramsPerMole);
    Chemical.AmountMoles              = ReadFloat(InTable, "amount_mol", Chemical.AmountMoles);
    Chemical.ConcentrationMolPerLitre = ReadFloat(InTable, "concentration_mol_l", Chemical.ConcentrationMolPerLitre);
    Chemical.ActivationEnergyKjPerMol = ReadFloat(InTable, "activation_energy_kj_mol", Chemical.ActivationEnergyKjPerMol);
    Chemical.EnthalpyDeltaKjPerMol    = ReadFloat(InTable, "enthalpy_delta_kj_mol", Chemical.EnthalpyDeltaKjPerMol);
    Chemical.MatterPhaseIndex         = std::clamp(ReadInt(InTable, "matter_phase", Chemical.MatterPhaseIndex), 0, 3);
    Chemical.ReversibleEquilibrium    = ReadBool(InTable, "reversible", Chemical.ReversibleEquilibrium);
    Chemical.CatalyzedPathway         = ReadBool(InTable, "catalyzed", Chemical.CatalyzedPathway);
    Chemical.AdvanceRevision();
}

void SerializeOpticsSubtable(toml::table& OutTable, const SurfaceOpticsConfiguration& Optics)
{
    OutTable.insert_or_assign("optics_enabled", Optics.Enabled);
    WriteFloat3(OutTable, "albedo_rgb", Optics.AlbedoRgb);
    OutTable.insert_or_assign("roughness", static_cast<double>(Optics.Roughness));
    OutTable.insert_or_assign("metallic", static_cast<double>(Optics.Metallic));
    OutTable.insert_or_assign("ior", static_cast<double>(Optics.IndexOfRefraction));
    OutTable.insert_or_assign("cast_shadows", Optics.CastShadows);
    OutTable.insert_or_assign("receive_gi", Optics.ReceiveGlobalIllumination);
}

void DeserializeOpticsSubtable(const toml::table& InTable, SurfaceOpticsConfiguration& Optics)
{
    Optics.Enabled = ReadBool(InTable, "optics_enabled", Optics.Enabled);
    ReadFloat3(InTable, "albedo_rgb", Optics.AlbedoRgb);
    Optics.Roughness                 = ReadFloat(InTable, "roughness", Optics.Roughness);
    Optics.Metallic                  = ReadFloat(InTable, "metallic", Optics.Metallic);
    Optics.IndexOfRefraction         = ReadFloat(InTable, "ior", Optics.IndexOfRefraction);
    Optics.CastShadows               = ReadBool(InTable, "cast_shadows", Optics.CastShadows);
    Optics.ReceiveGlobalIllumination = ReadBool(InTable, "receive_gi", Optics.ReceiveGlobalIllumination);
    Optics.AdvanceRevision();
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCENERECORDBASE
//------------------------------------------------------------------------------------------------------------------------

SceneRecordBase::SceneRecordBase(RecordClassification InClassification,
                                 std::string_view     InTypeIdentifier,
                                 std::string          InDisplayName)
    : RecordEntry(InClassification)
    , TypeIdentifier(InTypeIdentifier)
    , StoredName(std::move(InDisplayName))
{
#if FRONTIER_EDITOR
    if (!StoredName.empty())
    {
        SceneDirectory::Instance().ConfigureName(Token(), StoredName);
    }
#endif
}

void SceneRecordBase::AssignRecordName(std::string NewName)
{
    StoredName = std::move(NewName);
#if FRONTIER_EDITOR
    SceneDirectory::Instance().ConfigureName(Token(), StoredName);
#endif
}

void SceneRecordBase::AssignParentToken(RecordToken ParentToken)
{
    StoredParent = ParentToken;
#if FRONTIER_EDITOR
    SceneDirectory::Instance().ConfigureParent(Token(), ParentToken);
#endif
}

void SceneRecordBase::ConfigureEditorPresentation(const char* GlyphIdentifier,
                                                  const char* GroupName,
                                                  const char* SubtitleCategory)
{
    (void)GlyphIdentifier;
    (void)GroupName;
    (void)SubtitleCategory;
#if FRONTIER_EDITOR
    SceneDirectoryMetadata Meta = SceneDirectory::Instance().QueryMetadata(Token());
    if (GlyphIdentifier != nullptr && GlyphIdentifier[0] != '\0')
    {
        Meta.GlyphIdentifier = GlyphIdentifier;
    }
    if (GroupName != nullptr && GroupName[0] != '\0')
    {
        Meta.GroupName = GroupName;
    }
    if (SubtitleCategory != nullptr && SubtitleCategory[0] != '\0')
    {
        Meta.SubtitleCategory = SubtitleCategory;
    }
    SceneDirectory::Instance().ConfigureMetadata(Token(), std::move(Meta));
#endif
}

//------------------------------------------------------------------------------------------------------------------------
//                                       SINGLE-CONFIGURATION SERIALIZATION SPECIALIZATIONS
//------------------------------------------------------------------------------------------------------------------------

template <>
void DiagnosticsRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("timing_overlay", Parameters.TimingOverlayEnabled);
    OutTable.insert_or_assign("memory_overlay", Parameters.MemoryOverlayEnabled);
}

template <>
void DiagnosticsRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.TimingOverlayEnabled = ReadBool(InTable, "timing_overlay", Parameters.TimingOverlayEnabled);
    Parameters.MemoryOverlayEnabled = ReadBool(InTable, "memory_overlay", Parameters.MemoryOverlayEnabled);
    Parameters.AdvanceRevision();
}

template <>
void RenderPipelineRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("hardware_ray_query", Parameters.HardwareRayQueryTier);
    OutTable.insert_or_assign("quality_tier", static_cast<int64_t>(Parameters.QualityTier));
    OutTable.insert_or_assign("inspection_mode", static_cast<int64_t>(Parameters.InspectionModeIndex));
    OutTable.insert_or_assign("exposure_ev", static_cast<double>(Parameters.ExposureCompensationEv));
    OutTable.insert_or_assign("bloom_intensity", static_cast<double>(Parameters.BloomIntensity));
    OutTable.insert_or_assign("global_illumination", Parameters.GlobalIllumination);
    OutTable.insert_or_assign("temporal_aa", Parameters.TemporalAntiAliasing);
}

template <>
void RenderPipelineRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled                = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.HardwareRayQueryTier   = ReadBool(InTable, "hardware_ray_query", Parameters.HardwareRayQueryTier);
    Parameters.QualityTier            = std::clamp(ReadInt(InTable, "quality_tier", Parameters.QualityTier), 0, 2);
    Parameters.InspectionModeIndex    = std::clamp(ReadInt(InTable, "inspection_mode", Parameters.InspectionModeIndex), 0, 14);
    Parameters.ExposureCompensationEv = ReadFloat(InTable, "exposure_ev", Parameters.ExposureCompensationEv);
    Parameters.BloomIntensity         = ReadFloat(InTable, "bloom_intensity", Parameters.BloomIntensity);
    Parameters.GlobalIllumination     = ReadBool(InTable, "global_illumination", Parameters.GlobalIllumination);
    Parameters.TemporalAntiAliasing   = ReadBool(InTable, "temporal_aa", Parameters.TemporalAntiAliasing);
    Parameters.AdvanceRevision();
}

template <>
void DistanceFieldRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("cascade0_m", static_cast<double>(Parameters.CascadeZeroRadiusMetres));
    OutTable.insert_or_assign("cascade1_m", static_cast<double>(Parameters.CascadeOneRadiusMetres));
    OutTable.insert_or_assign("cascade2_m", static_cast<double>(Parameters.CascadeTwoRadiusMetres));
    OutTable.insert_or_assign("cascade3_m", static_cast<double>(Parameters.CascadeThreeRadiusMetres));
    OutTable.insert_or_assign("max_cone_steps", static_cast<int64_t>(Parameters.MaxConeSteps));
}

template <>
void DistanceFieldRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled                  = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.CascadeZeroRadiusMetres  = ReadFloat(InTable, "cascade0_m", Parameters.CascadeZeroRadiusMetres);
    Parameters.CascadeOneRadiusMetres   = ReadFloat(InTable, "cascade1_m", Parameters.CascadeOneRadiusMetres);
    Parameters.CascadeTwoRadiusMetres   = ReadFloat(InTable, "cascade2_m", Parameters.CascadeTwoRadiusMetres);
    Parameters.CascadeThreeRadiusMetres = ReadFloat(InTable, "cascade3_m", Parameters.CascadeThreeRadiusMetres);
    Parameters.MaxConeSteps             = ReadUint32(InTable, "max_cone_steps", Parameters.MaxConeSteps);
    Parameters.AdvanceRevision();
}

template <>
void SurfaceCardRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("atlas_px", static_cast<int64_t>(Parameters.AtlasResolutionPx));
    OutTable.insert_or_assign("bounce_gain", static_cast<double>(Parameters.IndirectBounceGain));
    OutTable.insert_or_assign("cards_per_object", static_cast<int64_t>(Parameters.CardsPerObject));
}

template <>
void SurfaceCardRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled            = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.AtlasResolutionPx  = ReadUint32(InTable, "atlas_px", Parameters.AtlasResolutionPx);
    Parameters.IndirectBounceGain = ReadFloat(InTable, "bounce_gain", Parameters.IndirectBounceGain);
    Parameters.CardsPerObject     = ReadUint32(InTable, "cards_per_object", Parameters.CardsPerObject);
    Parameters.AdvanceRevision();
}

template <>
void PolygonClusterRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("hierarchy_depth", static_cast<int64_t>(Parameters.HierarchyDepth));
    OutTable.insert_or_assign("max_cluster_triangles", static_cast<int64_t>(Parameters.MaxClusterTriangles));
    OutTable.insert_or_assign("hiz_culling", Parameters.HiZOcclusionCulling);
}

template <>
void PolygonClusterRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled             = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.HierarchyDepth      = ReadUint32(InTable, "hierarchy_depth", Parameters.HierarchyDepth);
    Parameters.MaxClusterTriangles = ReadUint32(InTable, "max_cluster_triangles", Parameters.MaxClusterTriangles);
    Parameters.HiZOcclusionCulling = ReadBool(InTable, "hiz_culling", Parameters.HiZOcclusionCulling);
    Parameters.AdvanceRevision();
}

template <>
void SolarRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("elevation_deg", static_cast<double>(Parameters.ElevationDegrees));
    OutTable.insert_or_assign("azimuth_deg", static_cast<double>(Parameters.AzimuthDegrees));
    OutTable.insert_or_assign("illuminance_klx", static_cast<double>(Parameters.IlluminanceKilolux));
    OutTable.insert_or_assign("kelvin", static_cast<double>(Parameters.KelvinTemperature));
}

template <>
void SolarRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled            = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.ElevationDegrees   = ReadFloat(InTable, "elevation_deg", Parameters.ElevationDegrees);
    Parameters.AzimuthDegrees     = ReadFloat(InTable, "azimuth_deg", Parameters.AzimuthDegrees);
    Parameters.IlluminanceKilolux = ReadFloat(InTable, "illuminance_klx", Parameters.IlluminanceKilolux);
    Parameters.KelvinTemperature  = ReadFloat(InTable, "kelvin", Parameters.KelvinTemperature);
    Parameters.AdvanceRevision();
}

template <>
void OpticalFlareRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("intensity", static_cast<double>(Parameters.Intensity));
    OutTable.insert_or_assign("aperture_blades", static_cast<int64_t>(Parameters.ApertureBladeCount));
}

template <>
void OpticalFlareRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled            = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.Intensity          = ReadFloat(InTable, "intensity", Parameters.Intensity);
    Parameters.ApertureBladeCount = ReadUint32(InTable, "aperture_blades", Parameters.ApertureBladeCount);
    Parameters.AdvanceRevision();
}

template <>
void AtmosphereRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("rayleigh", static_cast<double>(Parameters.RayleighScattering));
    OutTable.insert_or_assign("mie_turbidity", static_cast<double>(Parameters.MieTurbidity));
    OutTable.insert_or_assign("ozone", static_cast<double>(Parameters.OzoneAbsorptionScale));
}

template <>
void AtmosphereRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled              = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.RayleighScattering   = ReadFloat(InTable, "rayleigh", Parameters.RayleighScattering);
    Parameters.MieTurbidity         = ReadFloat(InTable, "mie_turbidity", Parameters.MieTurbidity);
    Parameters.OzoneAbsorptionScale = ReadFloat(InTable, "ozone", Parameters.OzoneAbsorptionScale);
    Parameters.AdvanceRevision();
}

template <>
void CloudRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("local_volume", Parameters.LocalVolume);
    OutTable.insert_or_assign("coverage", static_cast<double>(Parameters.Coverage));
    OutTable.insert_or_assign("base_altitude_m", static_cast<double>(Parameters.BaseAltitudeMetres));
    OutTable.insert_or_assign("thickness_m", static_cast<double>(Parameters.LayerThicknessMetres));
}

template <>
void CloudRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled              = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.LocalVolume          = ReadBool(InTable, "local_volume", Parameters.LocalVolume);
    Parameters.Coverage             = ReadFloat(InTable, "coverage", Parameters.Coverage);
    Parameters.BaseAltitudeMetres   = ReadFloat(InTable, "base_altitude_m", Parameters.BaseAltitudeMetres);
    Parameters.LayerThicknessMetres = ReadFloat(InTable, "thickness_m", Parameters.LayerThicknessMetres);
    Parameters.AdvanceRevision();
}

template <>
void StarRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("hipparcos_count", static_cast<int64_t>(Parameters.HipparcosCount));
    OutTable.insert_or_assign("magnitude_gain", static_cast<double>(Parameters.MagnitudeGain));
}

template <>
void StarRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled        = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.HipparcosCount = ReadUint32(InTable, "hipparcos_count", Parameters.HipparcosCount);
    Parameters.MagnitudeGain  = ReadFloat(InTable, "magnitude_gain", Parameters.MagnitudeGain);
    Parameters.AdvanceRevision();
}

template <>
void MoonRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("primary_phase", static_cast<double>(Parameters.PrimaryPhase));
    OutTable.insert_or_assign("secondary_enabled", Parameters.SecondaryMoonEnabled);
}

template <>
void MoonRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled              = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.PrimaryPhase         = ReadFloat(InTable, "primary_phase", Parameters.PrimaryPhase);
    Parameters.SecondaryMoonEnabled = ReadBool(InTable, "secondary_enabled", Parameters.SecondaryMoonEnabled);
    Parameters.AdvanceRevision();
}

template <>
void FogRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("local_volume", Parameters.LocalVolume);
    OutTable.insert_or_assign("base_density", static_cast<double>(Parameters.BaseDensity));
    OutTable.insert_or_assign("height_falloff", static_cast<double>(Parameters.HeightFalloff));
}

template <>
void FogRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled       = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.LocalVolume   = ReadBool(InTable, "local_volume", Parameters.LocalVolume);
    Parameters.BaseDensity   = ReadFloat(InTable, "base_density", Parameters.BaseDensity);
    Parameters.HeightFalloff = ReadFloat(InTable, "height_falloff", Parameters.HeightFalloff);
    Parameters.AdvanceRevision();
}

template <>
void LightningRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("strikes_per_minute", static_cast<double>(Parameters.StrikesPerMinute));
    OutTable.insert_or_assign("auto_discharge", Parameters.AutoStormDischarge);
}

template <>
void LightningRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled            = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.StrikesPerMinute   = ReadFloat(InTable, "strikes_per_minute", Parameters.StrikesPerMinute);
    Parameters.AutoStormDischarge = ReadBool(InTable, "auto_discharge", Parameters.AutoStormDischarge);
    Parameters.AdvanceRevision();
}

template <>
void WindRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("direction_deg", static_cast<double>(Parameters.DirectionDegrees));
    OutTable.insert_or_assign("speed_mps", static_cast<double>(Parameters.SpeedMetresPerSecond));
    OutTable.insert_or_assign("turbulence", static_cast<double>(Parameters.CurlNoiseTurbulence));
}

template <>
void WindRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled              = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.DirectionDegrees     = ReadFloat(InTable, "direction_deg", Parameters.DirectionDegrees);
    Parameters.SpeedMetresPerSecond = ReadFloat(InTable, "speed_mps", Parameters.SpeedMetresPerSecond);
    Parameters.CurlNoiseTurbulence  = ReadFloat(InTable, "turbulence", Parameters.CurlNoiseTurbulence);
    Parameters.AdvanceRevision();
}

template <>
void FluidPlumeRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("buoyancy_lift", static_cast<double>(Parameters.BuoyancyLift));
    OutTable.insert_or_assign("vorticity", static_cast<double>(Parameters.VorticityConfinement));
    OutTable.insert_or_assign("blackbody_k", static_cast<double>(Parameters.BlackbodyCoreKelvin));
}

template <>
void FluidPlumeRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled              = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.BuoyancyLift         = ReadFloat(InTable, "buoyancy_lift", Parameters.BuoyancyLift);
    Parameters.VorticityConfinement = ReadFloat(InTable, "vorticity", Parameters.VorticityConfinement);
    Parameters.BlackbodyCoreKelvin  = ReadFloat(InTable, "blackbody_k", Parameters.BlackbodyCoreKelvin);
    Parameters.AdvanceRevision();
}

template <>
void FractureRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("voronoi_cells", static_cast<int64_t>(Parameters.VoronoiCellCount));
    OutTable.insert_or_assign("impulse_threshold_ns", static_cast<double>(Parameters.ImpulseThresholdNs));
}

template <>
void FractureRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled            = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.VoronoiCellCount   = ReadUint32(InTable, "voronoi_cells", Parameters.VoronoiCellCount);
    Parameters.ImpulseThresholdNs = ReadFloat(InTable, "impulse_threshold_ns", Parameters.ImpulseThresholdNs);
    Parameters.AdvanceRevision();
}

template <>
void ParticleRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("spark_rate", static_cast<double>(Parameters.SparkRatePerSecond));
    OutTable.insert_or_assign("rain_density", static_cast<double>(Parameters.RainDropDensity));
    OutTable.insert_or_assign("snow_density", static_cast<double>(Parameters.SnowFlakeDensity));
}

template <>
void ParticleRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled            = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.SparkRatePerSecond = ReadFloat(InTable, "spark_rate", Parameters.SparkRatePerSecond);
    Parameters.RainDropDensity    = ReadFloat(InTable, "rain_density", Parameters.RainDropDensity);
    Parameters.SnowFlakeDensity   = ReadFloat(InTable, "snow_density", Parameters.SnowFlakeDensity);
    Parameters.AdvanceRevision();
}

//------------------------------------------------------------------------------------------------------------------------
//                                      COMPOSABLE MULTI-CONFIGURATION SCENE RECORDS
//------------------------------------------------------------------------------------------------------------------------

SpatialNodeRecord::SpatialNodeRecord(std::string InDisplayName)
    : SceneRecordBase(RecordClassification::Geometry, "SpatialNode", std::move(InDisplayName))
{
    ConfigureEditorPresentation("Compass", "SCENE OBJECTS", "SpatialNode");
#if FRONTIER_EDITOR
    InspectorRuntime::Instance().BindInspector(QueryToken(), std::make_unique<SpatialInspector>(Spatial));
#endif
}

void SpatialNodeRecord::SerializeConfiguration(toml::table& OutTable) const
{
    SerializeSpatialSubtable(OutTable, Spatial);
}

void SpatialNodeRecord::DeserializeConfiguration(const toml::table& InTable)
{
    DeserializeSpatialSubtable(InTable, Spatial);
}

RigidBodyRecord::RigidBodyRecord(std::string InDisplayName)
    : SceneRecordBase(RecordClassification::Geometry, "RigidBody", std::move(InDisplayName))
{
    ConfigureEditorPresentation("Box", "SCENE OBJECTS", "RigidBody");
#if FRONTIER_EDITOR
    InspectorRuntime::Instance().BindInspector(QueryToken(), std::make_unique<SpatialInspector>(Spatial));
    InspectorRuntime::Instance().AttachInspector(QueryToken(), std::make_unique<PhysicsInspector>(Physics));
    InspectorRuntime::Instance().AttachInspector(QueryToken(), std::make_unique<SurfaceOpticsInspector>(Optics));
#endif
}

void RigidBodyRecord::SerializeConfiguration(toml::table& OutTable) const
{
    SerializeSpatialSubtable(OutTable, Spatial);
    SerializePhysicsSubtable(OutTable, Physics);
    SerializeOpticsSubtable(OutTable, Optics);
}

void RigidBodyRecord::DeserializeConfiguration(const toml::table& InTable)
{
    DeserializeSpatialSubtable(InTable, Spatial);
    DeserializePhysicsSubtable(InTable, Physics);
    DeserializeOpticsSubtable(InTable, Optics);
}

void RigidBodyRecord::StepSimulation(float DeltaSeconds)
{
    if (!Physics.Enabled || !Physics.DynamicBody || DeltaSeconds <= 0.0f)
    {
        return;
    }
    const float Damping = std::max(0.0f, 1.0f - Physics.LinearDamping * DeltaSeconds);
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        Physics.LinearVelocityMps[Axis] *= Damping;
        Spatial.PositionMetres[Axis] += Physics.LinearVelocityMps[Axis] * DeltaSeconds;
    }
    if (Physics.GravityCoupled && Spatial.PositionMetres[1] > 0.5f)
    {
        Physics.LinearVelocityMps[1] -= 9.80665f * DeltaSeconds;
        Spatial.PositionMetres[1] += Physics.LinearVelocityMps[1] * DeltaSeconds;
        if (Spatial.PositionMetres[1] <= 0.5f)
        {
            Spatial.PositionMetres[1]    = 0.5f;
            Physics.LinearVelocityMps[1] = -Physics.LinearVelocityMps[1] * Physics.Restitution;
        }
    }
}

ThermalBodyRecord::ThermalBodyRecord(std::string InDisplayName)
    : SceneRecordBase(RecordClassification::Physics, "ThermalBody", std::move(InDisplayName))
{
    ConfigureEditorPresentation("Flame", "SIMULATION", "ThermalBody");
#if FRONTIER_EDITOR
    InspectorRuntime::Instance().BindInspector(QueryToken(), std::make_unique<SpatialInspector>(Spatial));
    InspectorRuntime::Instance().AttachInspector(QueryToken(), std::make_unique<PhysicsInspector>(Physics));
    InspectorRuntime::Instance().AttachInspector(QueryToken(), std::make_unique<ThermalInspector>(Thermal, &Physics.Mass));
    InspectorRuntime::Instance().AttachInspector(QueryToken(), std::make_unique<SurfaceOpticsInspector>(Optics));
#endif
}

void ThermalBodyRecord::SerializeConfiguration(toml::table& OutTable) const
{
    SerializeSpatialSubtable(OutTable, Spatial);
    SerializePhysicsSubtable(OutTable, Physics);
    SerializeThermalSubtable(OutTable, Thermal);
    SerializeOpticsSubtable(OutTable, Optics);
}

void ThermalBodyRecord::DeserializeConfiguration(const toml::table& InTable)
{
    DeserializeSpatialSubtable(InTable, Spatial);
    DeserializePhysicsSubtable(InTable, Physics);
    DeserializeThermalSubtable(InTable, Thermal);
    DeserializeOpticsSubtable(InTable, Optics);
}

void ThermalBodyRecord::StepSimulation(float DeltaSeconds)
{
    if (!Thermal.Enabled || DeltaSeconds <= 0.0f)
    {
        return;
    }
    constexpr float AmbientKelvin = 293.15f;
    const float     HeatCap       = std::max(1.0f, Physics.Mass.MassKilograms * Thermal.SpecificHeatJPerKgK);
    const float     ConvectiveW   = Thermal.ConvectionCoefficientWPerM2K * (AmbientKelvin - Thermal.TemperatureKelvin);
    Thermal.TemperatureKelvin     = std::max(1.0f, Thermal.TemperatureKelvin + (ConvectiveW / HeatCap) * DeltaSeconds);
}

ChemicalVesselRecord::ChemicalVesselRecord(std::string InDisplayName)
    : SceneRecordBase(RecordClassification::Physics, "ChemicalVessel", std::move(InDisplayName))
{
    ConfigureEditorPresentation("FlaskConical", "SIMULATION", "ChemicalVessel");
#if FRONTIER_EDITOR
    InspectorRuntime::Instance().BindInspector(QueryToken(), std::make_unique<SpatialInspector>(Spatial));
    InspectorRuntime::Instance().AttachInspector(QueryToken(), std::make_unique<ThermalInspector>(Thermal));
    InspectorRuntime::Instance().AttachInspector(QueryToken(), std::make_unique<ChemicalInspector>(Chemical, &Thermal));
#endif
}

void ChemicalVesselRecord::SerializeConfiguration(toml::table& OutTable) const
{
    SerializeSpatialSubtable(OutTable, Spatial);
    SerializeThermalSubtable(OutTable, Thermal);
    SerializeChemicalSubtable(OutTable, Chemical);
}

void ChemicalVesselRecord::DeserializeConfiguration(const toml::table& InTable)
{
    DeserializeSpatialSubtable(InTable, Spatial);
    DeserializeThermalSubtable(InTable, Thermal);
    DeserializeChemicalSubtable(InTable, Chemical);
}

void ChemicalVesselRecord::StepSimulation(float DeltaSeconds)
{
    if (!Chemical.Enabled || DeltaSeconds <= 0.0f || Chemical.AmountMoles <= 1.0e-5f)
    {
        return;
    }
    const float RateConstant = Chemical.ComputeArrheniusRateConstant(Thermal.TemperatureKelvin);
    const float ReactedFrac  = 1.0f - std::exp(-std::min(RateConstant, 10.0f) * DeltaSeconds);
    const float DeltaMoles   = Chemical.AmountMoles * ReactedFrac;
    Chemical.AmountMoles     = std::max(0.0f, Chemical.AmountMoles - DeltaMoles);
    if (Thermal.Enabled)
    {
        const float ReleasedJoules = -DeltaMoles * Chemical.EnthalpyDeltaKjPerMol * 1000.0f;
        const float HeatCapacity   = std::max(10.0f, Chemical.ComputeSubstanceMassKilograms() * Thermal.SpecificHeatJPerKgK);
        Thermal.TemperatureKelvin  = std::clamp(Thermal.TemperatureKelvin + ReleasedJoules / HeatCapacity, 50.0f, 3500.0f);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCENEWORLD
//------------------------------------------------------------------------------------------------------------------------

SceneWorld::SceneWorld()
{
    RegisterNativeEngineTypes();
}

SceneWorld::~SceneWorld()
{
    Clear();
}

void SceneWorld::RegisterType(SceneTypeRegistration Registration)
{
    for (SceneTypeRegistration& Existing : TypeRegistry)
    {
        if (Existing.TypeIdentifier == Registration.TypeIdentifier)
        {
            Existing = std::move(Registration);
            return;
        }
    }
    TypeRegistry.push_back(std::move(Registration));
}

std::vector<std::pair<std::string, std::string>> SceneWorld::EnumerateRegisteredTypes() const
{
    std::vector<std::pair<std::string, std::string>> Result;
    Result.reserve(TypeRegistry.size());
    for (const SceneTypeRegistration& Entry : TypeRegistry)
    {
        Result.emplace_back(Entry.TypeIdentifier, Entry.DisplayCategoryName);
    }
    return Result;
}

void SceneWorld::RegisterNativeEngineTypes()
{
    auto RegisterSimple = [this]<typename RecordType>(const char*          TypeId,
                                                      const char*          CategoryLabel,
                                                      RecordClassification Classification,
                                                      const char*          IconName,
                                                      const char*          GroupName) {
        RegisterType({
            TypeId,
            CategoryLabel,
            Classification,
            [Classification, TypeId, IconName, GroupName](std::string DisplayName) -> std::unique_ptr<SceneRecordBase> {
                return std::make_unique<RecordType>(Classification, TypeId, std::move(DisplayName), IconName, GroupName);
            },
        });
    };

    // Composable Object & Scientific Simulation Types
    RegisterType({"SpatialNode", "Spatial · Coordinate Node", RecordClassification::Geometry,
                  [](std::string Name) { return std::make_unique<SpatialNodeRecord>(std::move(Name)); }});
    RegisterType({"RigidBody", "Mechanics · Rigid Body", RecordClassification::Geometry,
                  [](std::string Name) { return std::make_unique<RigidBodyRecord>(std::move(Name)); }});
    RegisterType({"ThermalBody", "Thermodynamics · Thermal Body", RecordClassification::Physics,
                  [](std::string Name) { return std::make_unique<ThermalBodyRecord>(std::move(Name)); }});
    RegisterType({"ChemicalVessel", "Chemistry · Reaction Vessel", RecordClassification::Physics,
                  [](std::string Name) { return std::make_unique<ChemicalVesselRecord>(std::move(Name)); }});

    // Environment Types
    RegisterSimple.operator()<SolarRecord>("Solar", "Environment · Solar", RecordClassification::Illumination, "Sun", "ENVIRONMENT");
    RegisterSimple.operator()<OpticalFlareRecord>("OpticalFlare", "Environment · Optical Flare", RecordClassification::Illumination, "Sparkles", "ENVIRONMENT");
    RegisterSimple.operator()<AtmosphereRecord>("Atmosphere", "Environment · Atmosphere", RecordClassification::Environment, "Globe", "ENVIRONMENT");
    RegisterSimple.operator()<CloudRecord>("Clouds", "Environment · Clouds", RecordClassification::Environment, "Cloud", "ENVIRONMENT");
    RegisterSimple.operator()<StarRecord>("Stars", "Environment · Stars", RecordClassification::Environment, "Star", "ENVIRONMENT");
    RegisterSimple.operator()<MoonRecord>("Moons", "Environment · Moons", RecordClassification::Environment, "Moon", "ENVIRONMENT");
    RegisterSimple.operator()<FogRecord>("Fog", "Environment · Fog", RecordClassification::Environment, "CloudFog", "ENVIRONMENT");
    RegisterSimple.operator()<LightningRecord>("Lightning", "Environment · Lightning", RecordClassification::Illumination, "Zap", "ENVIRONMENT");
    RegisterSimple.operator()<WindRecord>("Wind", "Environment · Wind Field", RecordClassification::Environment, "Wind", "ENVIRONMENT");

    // Render Engine & Continuum Solver Types
    RegisterSimple.operator()<DiagnosticsRecord>("Diagnostics", "Render · Diagnostics", RecordClassification::Interface, "Gauge", "RENDER ENGINE");
    RegisterSimple.operator()<RenderPipelineRecord>("RenderPipeline", "Render · Pipeline", RecordClassification::Illumination, "Sliders", "RENDER ENGINE");
    RegisterSimple.operator()<DistanceFieldRecord>("DistanceField", "Render · Distance Field", RecordClassification::Geometry, "Layers", "RENDER ENGINE");
    RegisterSimple.operator()<SurfaceCardRecord>("SurfaceCards", "Render · Surface Cards", RecordClassification::Illumination, "Grid", "RENDER ENGINE");
    RegisterSimple.operator()<PolygonClusterRecord>("PolygonClusters", "Render · Polygon Clusters", RecordClassification::Geometry, "Boxes", "RENDER ENGINE");
    RegisterSimple.operator()<FluidPlumeRecord>("FluidPlume", "Solver · Fluid Plume", RecordClassification::Physics, "Flame", "SOLVERS");
    RegisterSimple.operator()<FractureRecord>("Fracture", "Solver · Voronoi Fracture", RecordClassification::Physics, "Hammer", "SOLVERS");
    RegisterSimple.operator()<ParticleRecord>("Particles", "Solver · GPU Particles", RecordClassification::Physics, "Sparkles", "SOLVERS");
}

RecordToken SceneWorld::ConstructRecord(std::string_view TypeIdentifier,
                                        std::string      DisplayName,
                                        RecordToken      ParentToken)
{
    for (const SceneTypeRegistration& Registration : TypeRegistry)
    {
        if (Registration.TypeIdentifier == TypeIdentifier)
        {
            if (DisplayName.empty())
            {
                DisplayName = std::string(TypeIdentifier) + " " + std::to_string(OwnedRecords.size() + 1);
            }
            std::unique_ptr<SceneRecordBase> Created = Registration.ConstructCallback(std::move(DisplayName));
            if (!Created)
            {
                return {};
            }
            if (ParentToken.IsValid())
            {
                Created->AssignParentToken(ParentToken);
            }
            const RecordToken Issued = Created->QueryToken();
            OwnedRecords.push_back(std::move(Created));
            return Issued;
        }
    }
    return {};
}

bool SceneWorld::DestroyRecord(RecordToken Token)
{
    if (!Token.IsValid())
    {
        return false;
    }

    for (std::unique_ptr<SceneRecordBase>& Candidate : OwnedRecords)
    {
        if (Candidate && Candidate->QueryParentToken() == Token)
        {
            Candidate->AssignParentToken(RecordToken{});
        }
    }

    const auto It = std::find_if(OwnedRecords.begin(), OwnedRecords.end(),
                                 [Token](const std::unique_ptr<SceneRecordBase>& Entry) {
                                     return Entry && Entry->QueryToken() == Token;
                                 });
    if (It == OwnedRecords.end())
    {
        return false;
    }
    OwnedRecords.erase(It);
    return true;
}

void SceneWorld::Clear()
{
    while (!OwnedRecords.empty())
    {
        OwnedRecords.pop_back();
    }
}

void SceneWorld::AdvanceSimulation(float DeltaSeconds)
{
    for (std::unique_ptr<SceneRecordBase>& Record : OwnedRecords)
    {
        if (Record)
        {
            Record->StepSimulation(DeltaSeconds);
        }
    }
}

SceneRecordBase* SceneWorld::FindRecord(RecordToken Token) noexcept
{
    for (std::unique_ptr<SceneRecordBase>& Entry : OwnedRecords)
    {
        if (Entry && Entry->QueryToken() == Token)
        {
            return Entry.get();
        }
    }
    return nullptr;
}

SceneRecordBase* SceneWorld::FindFirstByType(std::string_view TypeIdentifier) noexcept
{
    for (std::unique_ptr<SceneRecordBase>& Entry : OwnedRecords)
    {
        if (Entry && Entry->QueryTypeIdentifier() == TypeIdentifier)
        {
            return Entry.get();
        }
    }
    return nullptr;
}

bool SceneWorld::LoadLevelSpace(const std::filesystem::path& LevelSpacePath, std::string* OutError)
{
    if (LevelSpacePath.extension() != LevelSpaceExtension)
    {
        if (OutError)
        {
            *OutError = "Level container must carry the .levelspace extension: " + LevelSpacePath.string();
        }
        return false;
    }

    toml::table Document;
    try
    {
        Document = toml::parse_file(LevelSpacePath.string());
    }
    catch (const toml::parse_error& Error)
    {
        if (OutError)
        {
            *OutError = std::string("Failed to parse .levelspace: ") + std::string(Error.description());
        }
        return false;
    }

    Clear();
    LoadedLevelPath = LevelSpacePath;

    if (const toml::table* LevelHeader = Document["level"].as_table())
    {
        if (const auto Name = (*LevelHeader)["name"].value<std::string>())
        {
            WorldName = *Name;
        }
    }

    std::unordered_map<std::string, RecordToken> IdentityMap;
    if (const toml::array* RecordsArray = Document["records"].as_array())
    {
        for (const toml::node& Node : *RecordsArray)
        {
            const toml::table* EntryTable = Node.as_table();
            if (!EntryTable)
            {
                continue;
            }
            const std::string TypeId   = (*EntryTable)["type"].value_or(std::string{});
            const std::string Name     = (*EntryTable)["name"].value_or(TypeId);
            const std::string Identity = (*EntryTable)["identity"].value_or(std::string{});
            const std::string ParentId = (*EntryTable)["parent"].value_or(std::string{});
            if (TypeId.empty())
            {
                continue;
            }

            RecordToken ParentToken{};
            if (!ParentId.empty())
            {
                if (const auto Found = IdentityMap.find(ParentId); Found != IdentityMap.end())
                {
                    ParentToken = Found->second;
                }
            }

            const RecordToken Created = ConstructRecord(TypeId, Name, ParentToken);
            if (!Created.IsValid())
            {
                continue;
            }
            if (!Identity.empty())
            {
                IdentityMap.emplace(Identity, Created);
            }
            if (SceneRecordBase* Record = FindRecord(Created))
            {
                if (const toml::table* ConfigTable = (*EntryTable)["configuration"].as_table())
                {
                    Record->DeserializeConfiguration(*ConfigTable);
                }
            }
        }
    }

    return true;
}

bool SceneWorld::SaveLevelSpace(const std::filesystem::path& LevelSpacePath, std::string* OutError) const
{
    if (LevelSpacePath.extension() != LevelSpaceExtension)
    {
        if (OutError)
        {
            *OutError = "Level container must carry the .levelspace extension: " + LevelSpacePath.string();
        }
        return false;
    }

    std::error_code FsErr;
    if (LevelSpacePath.has_parent_path())
    {
        std::filesystem::create_directories(LevelSpacePath.parent_path(), FsErr);
    }

    toml::table Root;
    toml::table Header;
    Header.insert_or_assign("format_version", int64_t{1});
    Header.insert_or_assign("name", WorldName);
    Root.insert_or_assign("level", std::move(Header));

    std::unordered_map<uint64_t, std::string> TokenIdentities;
    for (size_t Index = 0; Index < OwnedRecords.size(); ++Index)
    {
        if (OwnedRecords[Index])
        {
            TokenIdentities.emplace(OwnedRecords[Index]->QueryToken().Value, "rec_" + std::to_string(Index + 1));
        }
    }

    toml::array RecordsArray;
    for (const std::unique_ptr<SceneRecordBase>& Record : OwnedRecords)
    {
        if (!Record)
        {
            continue;
        }
        toml::table Entry;
        Entry.insert_or_assign("identity", TokenIdentities[Record->QueryToken().Value]);
        Entry.insert_or_assign("type", Record->QueryTypeIdentifier());
        Entry.insert_or_assign("name", Record->QueryRecordName());
        const RecordToken Parent = Record->QueryParentToken();
        if (Parent.IsValid())
        {
            if (const auto Found = TokenIdentities.find(Parent.Value); Found != TokenIdentities.end())
            {
                Entry.insert_or_assign("parent", Found->second);
            }
        }
        toml::table Config;
        Record->SerializeConfiguration(Config);
        Entry.insert_or_assign("configuration", std::move(Config));
        RecordsArray.push_back(std::move(Entry));
    }
    Root.insert_or_assign("records", std::move(RecordsArray));

    std::ofstream Output(LevelSpacePath, std::ios::binary | std::ios::trunc);
    if (!Output)
    {
        if (OutError)
        {
            *OutError = "Unable to open .levelspace for writing: " + LevelSpacePath.string();
        }
        return false;
    }
    Output << Root << "\n";
    return Output.good();
}

bool SceneWorld::LoadStartupLevelFromProject(const std::filesystem::path& ProjectSpacePath, std::string* OutError)
{
    toml::table ProjectDoc;
    try
    {
        ProjectDoc = toml::parse_file(ProjectSpacePath.string());
    }
    catch (const toml::parse_error& Error)
    {
        if (OutError)
        {
            *OutError = std::string("Failed to parse .projectspace: ") + std::string(Error.description());
        }
        return false;
    }

    const std::filesystem::path ProjectRoot = ProjectSpacePath.parent_path();
    std::string                 StartupId;
    if (const toml::table* EngineTable = ProjectDoc["engine"].as_table())
    {
        StartupId = (*EngineTable)["startup_level"].value_or(std::string{});
    }

    if (!StartupId.empty())
    {
        if (const toml::array* Levels = ProjectDoc["levels"].as_array())
        {
            for (const toml::node& Node : *Levels)
            {
                const toml::table* LevelEntry = Node.as_table();
                if (!LevelEntry)
                {
                    continue;
                }
                if ((*LevelEntry)["identity"].value_or(std::string{}) == StartupId)
                {
                    const std::string RelPath = (*LevelEntry)["path"].value_or(std::string{});
                    if (!RelPath.empty())
                    {
                        return LoadLevelSpace(ProjectRoot / RelPath, OutError);
                    }
                }
            }
        }
    }

    const std::filesystem::path DefaultLevel = ProjectRoot / "Content" / "Levels" / "Default.levelspace";
    if (std::filesystem::exists(DefaultLevel))
    {
        return LoadLevelSpace(DefaultLevel, OutError);
    }

    if (OutError)
    {
        *OutError = "No startup .levelspace found for project: " + ProjectSpacePath.string();
    }
    return false;
}

} // namespace Frontier
